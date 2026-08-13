/*
 * freertos_task_ui.c
 *
 * GPSDO UI task (StartUiTask) — vyčleněno z freertos.c.
 * Kreslí obrazovku z primitiv (libprim/libui) a obsluhuje dotyk tlačítek.
 * Běží VÝHRADNĚ zde (knihovny nejsou thread-safe).
 */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"

#include <stdio.h>        /* printf — hlaseni I2C4 recovery */
#include "i2c.h"          /* hi2c4 (touch) */
#include "ft5x06.h"
#include "ws_panel.h"     /* ws_panel_set_backlight — jas displeje */
#include "app_gpsdo.h"
#include "freertos_shared.h"
#include "watchdog.h"     /* watchdog_kick_ui — heartbeat */
#include "alarm.h"        /* alarm_click — zvukova odezva doteku */
#include "beeper.h"       /* beeper_boot_melody — startovni jingle */

/* (LTDC adresu ridi prim_stm32_present() v app/hal -> hltdc tu uz netreba.) */

/* ── I2C4 bus recovery (zachranna sit pro zaseknutou sbernici) ──────────────
 * ATTINY (0x45, bit-bang slave) umi po nestastne transakci drzet SDA ->
 * touch (0x38) i TMP117 (0x48) na TEZE sbernici umrou. 9 SCL pulzu na PH11
 * docvaka drzeny bajt, pak inline HAL_I2C_Init (⚠️ NIKDY MX_I2C4_Init —
 * ma Error_Handler trap; selhani ignorujeme, zkusi se priste). Stejny vzor
 * jako provereny i2c1_recover v freertos_task_sensors.c. Volat POD mutexem
 * a JEN pri prokazatelne mrtvem busu (touch HAL-fail streak). */
static void i2c4_recover(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_11;                       /* SCL rucne (open-drain) */
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL;                      /* pull-upy jsou externi (panel) */
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOH, &g);
    g.Pin = GPIO_PIN_12; g.Mode = GPIO_MODE_INPUT;   /* SDA jen sledujeme */
    HAL_GPIO_Init(GPIOH, &g);
    for (int i = 0; i < 9 && HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_12) == GPIO_PIN_RESET; i++) {
        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_RESET); osDelay(1);
        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_SET);   osDelay(1);
    }
    g.Pin = GPIO_PIN_11 | GPIO_PIN_12;         /* zpet na AF4 (jako MSP init) */
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_NOPULL;
    g.Alternate = GPIO_AF4_I2C4;
    HAL_GPIO_Init(GPIOH, &g);
    __HAL_I2C_DISABLE(&hi2c4);
    HAL_I2C_Init(&hi2c4);                      /* navratovou hodnotu zamerne neresime */
}

/* Runtime IDLE tasku + celkovy runtime (pro vypocet zatize CPU). configUSE_TRACE_
 * FACILITY + configGENERATE_RUN_TIME_STATS jsou zapnute (viz FreeRTOSConfig). */
static uint32_t ui_idle_runtime(uint32_t *total_out)
{
  static TaskStatus_t ts[12];
  uint32_t total = 0;
  UBaseType_t n = uxTaskGetSystemState(ts, 12, &total);
  uint32_t idle = 0;
  for (UBaseType_t i = 0; i < n; i++)
    if (ts[i].pcTaskName[0] == 'I' && ts[i].pcTaskName[1] == 'D') {
      idle = ts[i].ulRunTimeCounter; break;   /* "IDLE" */
    }
  if (total_out) *total_out = total;
  return idle;
}

void StartUiTask(void *argument)
{
  (void)argument;

  /* Boot splash: logo + build + prubeh selftestu (misto tmave obrazovky behem
   * initu). Drzi ~1,4 s (nebo do dokonceni selftestu z defaultTasku), pak main.
   * Kresli VYHRADNE tento task (libprim/libui nejsou thread-safe). */
  app_gpsdo_boot_splash();
  if (!g_sound_muted) beeper_boot_melody();   /* vzestupny "power-on" jingle (~0,5 s), pokud neni mute */
  for (int i = 0; i < 10; i++) {           /* ~1 s + melodie ~= 1,5 s splash celkem */
    app_gpsdo_boot_splash_tick();
    osDelay(100);
  }

  /* Pak rovnou do GPSDO obrazovky. */
  g_screen_req = 3;

  for (;;) {
    watchdog_kick_ui();   /* heartbeat pro IWDG (zatuhnuti UiTasku -> reset) */
    /* UART prikazy "screen main"/"clear"/"ui" nastavi g_screen_req; zde se
     * obslouzi (req 3 = hlavni obrazovka, 4 = smazani). */
    uint8_t req = g_screen_req;
    if (req) {
      g_screen_req = 0;
      /* LTDC scan-out adresu ridi prim_stm32_present() (page-flip pri vblanku). */
      if (req == 4) app_gpsdo_clear();
      else          app_gpsdo_render_main();
    }

    /* Dotek pro tlacitka (FT5x06 @ I2C4). Panel zrcadleny X i Y -> (799-x,479-y).
     * ⚠️ MUSI se cist CELY 31B ramec (ft5x06_read_touch) — castecne cteni controller
     * DRZI I2C -> freeze. Gate ~66 ms (15 Hz). Hranove spousteni (jen zacatek doteku). */
    static uint8_t was_down = 0;
    static uint8_t s_touch_primed = 0;     /* 0 = po bootu, dokud nevidime "prst nahoře" -> ignoruj */
    static uint32_t last_touch = 0;
    static uint32_t s_last_activity = 0;   /* posledni dotek (pro auto-dim) */
    static uint8_t  s_dimmed = 0;          /* 1 = podsviceni ztlumene necinnosti */
    static uint8_t  s_i2c4_fail = 0;    /* po sobe jdouci HAL selhani touch cteni */
    static uint32_t s_i2c4_rec_t = 0;   /* cas posledniho pokusu o recovery */
    static uint8_t  s_i2c4_busy = 0;    /* mutex se nepodarilo vzit (drive tise ignorovano) */
    static uint32_t s_touch_grace = 0;  /* po HW resetu TP: ~400 ms nepocitat chyby */
    static uint32_t s_touch_resets = 0; /* kolikrat uz se TP resetoval (diagnostika) */
    /* ADAPTIVNI gate: 33 ms (30 Hz) BEHEM a ~1,5 s PO interakci (svizne tapy —
     * hlavne opakovane +/- v Nastaveni), jinak 66 ms (15 Hz) v klidu. Touch cteni
     * je 31 B ~3 ms @100 kHz -> 30 Hz = ~9 % CPU, 15 Hz = ~4,5 %. V typickem stavu
     * (sledovani hlavni obrazovky, zadny dotek) se tak usetri ~4,5 % CPU; jediny
     * dopad je latence PRVNIHO tapu po klidu az 66 ms (jednorazova, neznatelna).
     * Cteni je vzdy CELY ramec -> zadne riziko freeze (viz varovani vyse). */
    uint32_t touch_gate = (was_down || (HAL_GetTick() - s_last_activity) < 1500u) ? 33u : 66u;
    if (HAL_GetTick() - last_touch >= touch_gate) {
      last_touch = HAL_GetTick();
      ft5x06_touch_t t; int got = 0, attempted = 0;
      if (osMutexAcquire(i2c4MutexHandle, 20) == osOK) {
        attempted = 1;
        got = ft5x06_read_touch(&hi2c4, &t);
        osMutexRelease(i2c4MutexHandle);
      }
      /* Detekce mrtve sbernice: pocitej JEN skutecna HAL selhani (mutex timeout
       * neni chyba busu — napr. bezici `scanner`). Po ~0,5 s souvislych chyb
       * zkus recovery (max 1x/5 s). */
      if (attempted) { if (got) s_i2c4_fail = 0; else if (s_i2c4_fail < 250) s_i2c4_fail++; }
      else if (s_i2c4_busy < 250) s_i2c4_busy++;   /* mutex se nepodarilo vzit — viditelne v `status` */

      if (s_i2c4_fail >= 8 && (HAL_GetTick() - s_i2c4_rec_t) > 5000u) {
        s_i2c4_rec_t = HAL_GetTick();
        printf("touch: I2C4 nereaguje (%u chyb, %u x mutex busy) -> recovery #%lu (bus + HW reset TP)\n",
               s_i2c4_fail, s_i2c4_busy, (unsigned long)(s_touch_resets + 1u));
        if (osMutexAcquire(i2c4MutexHandle, 100) == osOK) {
          /* 1) Uvolnit sbernici, kdyby ji nekdo drzel. ⚠️ `i2c4_recover` pulzuje SCL
           *    JEN kdyz je SDA v nule; kdyz sbernice volna je, jen reinicializuje I2C4. */
          i2c4_recover();

          /* 2) HARDWAROVY RESET DOTYKOVEHO RADICE pres ATTINY (nalez 2026-08-13).
           * Merenim na cili se ukazalo, ze pri mrtvem touchi je sbernice VOLNA
           * (SCL=1, SDA=1) a I2C4 bez chybovych priznaku — FT5x06 proste neACKuje
           * svou adresu. Pulzy na SCL tedy leci neco, co se tu vubec nedeje.
           * Skutecne dosazitelne priciny jsou dve a OBE spravi tenhle krok:
           *   a) FT5x06 je zaseknuty/uspany (nema zadnou spravu power mode),
           *   b) ATTINY ma poskozeny `PORTC` a drzi `RST_TP_N` v nule — sousedni
           *      zapis podsviceni (auto-dim!) jde po TEZE sbernici a ATTINY je
           *      bit-bang slave, ktery muze ztratit synchronizaci (viz varovani
           *      u zapisu jasu nize).
           * Prepsanim PORTC na znamou dobrou hodnotu se opravi (b) a pulz
           * `RST_TP_N` resetuje (a). Ostatni bity (LCD/bridge/backlight) zustavaji
           * v provoznim stavu, takze obraz problikne nanejvys dotykovym radicem. */
          ws_panel_set_portc(&hi2c4, (uint8_t)(WS_PC_RUN & ~WS_PC_RST_TP_N));
          osDelay(5);                       /* FT5x06 nRST potrebuje >1 ms */
          ws_panel_set_portc(&hi2c4, WS_PC_RUN);
          osMutexRelease(i2c4MutexHandle);
        }
        /* FT5x06 po resetu nabiha ~300 ms — do te doby by kazde cteni selhalo a
         * hned by si vyzadalo dalsi recovery. */
        s_touch_grace = HAL_GetTick();
        s_i2c4_fail = 0;
        s_touch_resets++;
      }
      /* Behem nabehu po resetu chyby nepocitej (jinak by se recovery retezila). */
      if ((HAL_GetTick() - s_touch_grace) < 400u) s_i2c4_fail = 0;
      if (got) {
        /* ⚠️ Boot-priming: FT5x06 NENI resetem nulovan a uzivatel muze pri
         * Menu->Restart drzet prst na "Ano" pres reset -> prvni poll by videl
         * "down" jako HRANU a spustil akci na souradnici "Ano", ktera se na hl.
         * obrazovce zrcadli na kartu Trend ("bootuje do trendu"). Dokud
         * nevidime "prst nahoře" (t.valid==0), doteky IGNORUJEME (jen sledujeme
         * was_down) -> rezidualni/drzeny dotek se absorbuje. */
        if (!s_touch_primed) {
          if (!t.valid) s_touch_primed = 1;   /* prvni uvolneni -> od ted prijimame */
        } else if (t.valid && !was_down) {
          s_last_activity = HAL_GetTick();
          if (s_dimmed) {                  /* probuzeni: prvni dotek jen rozsviti, nespusti akci */
            s_dimmed = 0;
            app_gpsdo_exit_screensaver();  /* zpet na okno, ktere bylo pred usnutim */
          } else if (app_gpsdo_handle_touch((int16_t)(799 - t.x), (int16_t)(479 - t.y))) {
            alarm_click();                 /* zvukova odezva obslouzeneho tlacitka (mute-aware) */
          }
        }
        was_down = (uint8_t)t.valid;
      }
    }

    /* Auto-dim + aplikace jasu: app (okno Nastaveni) meni jen g_brightness; HW zapis
     * (ATTINY backlight @ I2C4 pod mutexem) je zde. Po g_autodim_sec necinnosti ztlumi na
     * AUTODIM_LEVEL (dotek probudi); vypinatelne g_autodim_en. Zapise se jen pri zmene. */
    #define AUTODIM_LEVEL  20u      /* ztlumeny jas (nikdy uplna tma) */
    static int s_bl = -1;
    if (s_bl < 0) {
      s_bl = g_brightness;              /* prevezmi bootovaci hodnotu (uz nastavena v main.c) */
      s_last_activity = HAL_GetTick();  /* pocitej necinnost od bootu */
    }
    uint32_t autodim_ms = (uint32_t)g_autodim_sec * 1000u;   /* prodleva z Nastaveni */
    if (!g_autodim_en) {
      if (s_dimmed) { s_dimmed = 0; app_gpsdo_exit_screensaver(); }   /* vypnuto za behu */
    } else if (!s_dimmed && (HAL_GetTick() - s_last_activity) > autodim_ms) {
      s_dimmed = 1;
      app_gpsdo_enter_screensaver();   /* ztlumeny displej ukazuje velke RTC hodiny */
    }
    uint8_t bl_target = s_dimmed ? AUTODIM_LEVEL : g_brightness;
    if ((uint8_t)s_bl != bl_target) {
      if (osMutexAcquire(i2c4MutexHandle, 20) == osOK) {
        /* ⚠️ ATTINY = softwarovy (bit-bang) I2C slave a soucasne generuje PWM
         * podsviceni — runtime zapis potrebuje KLIDOVOU mezeru na sbernici
         * pred i po transakci, jinak hrozi ztrata synchronizace slave
         * automatu -> ATTINY drzi SDA -> mrtvy touch (stejna sbernice). */
        osDelay(2);
        ws_panel_set_backlight(&hi2c4, bl_target);
        osDelay(2);
        osMutexRelease(i2c4MutexHandle);
        s_bl = bl_target;
      }
    }

    /* Obnova diagnostiky + RTOS zdravi 2x/s (senzory cteny take 2x/s).
     * Na hlavni obrazovce je app_gpsdo_tick no-op (jen diag). */
    static uint32_t last_tick = 0;
    if (HAL_GetTick() - last_tick >= 500) {
      last_tick = HAL_GetTick();
      g_uptime_s       = HAL_GetTick() / 1000u;
      g_rtos_heap_free = (uint32_t)xPortGetFreeHeapSize();
      g_rtos_heap_min  = (uint32_t)xPortGetMinimumEverFreeHeapSize();
      static uint32_t prev_total = 0, prev_idle = 0;
      uint32_t total = 0, idle = ui_idle_runtime(&total);
      uint32_t dt = total - prev_total, di = idle - prev_idle;
      prev_total = total; prev_idle = idle;
      if (dt) g_rtos_cpu_pct = (di < dt) ? (uint32_t)(100u - (uint64_t)di * 100u / dt) : 0u;
      app_gpsdo_tick();
    }

    /* Cas na hlavni obrazovce: kontrola ~kazdych 100 ms, prekresli jen pri zmene sekundy. */
    static uint32_t last_clock = 0;
    if (HAL_GetTick() - last_clock >= 100) {
      last_clock = HAL_GetTick();
      app_gpsdo_tick_clock(HAL_GetTick());
    }

    /* Animace simulovaneho signal bargrafu 10x/s (dBm krok po jednotkach). */
    static uint32_t last_sig = 0;
    if (HAL_GetTick() - last_sig >= 100) {
      last_sig = HAL_GetTick();
      app_gpsdo_tick_signal();
    }

    /* Simulace kmitoctu 20x/s (spojita zmena, per-segment dirty redraw). */
    static uint32_t last_freq = 0;
    if (HAL_GetTick() - last_freq >= 50) {
      last_freq = HAL_GetTick();
      app_gpsdo_tick_freq();
    }

    /* Animace/demo okno 20x/s (ease-out krok bargrafu; no-op mimo s_view=24). */
    static uint32_t last_anim = 0;
    if (HAL_GetTick() - last_anim >= 50) {
      last_anim = HAL_GetTick();
      app_gpsdo_tick_anim();
    }

    /* GPSDO statistika: vzorkovani frakcni odchylky 1x/s (τ0=1s -> dekadova osa);
     * trend+offset+σy+drift prekreslit 1x/s, Allan (tezsi render) 1x/s. */
    static uint32_t last_stat_s = 0, last_stat_d = 0, last_allan = 0;
    if (HAL_GetTick() - last_stat_s >= 1000) {       /* vzorkovani 1/s (τ0=1s, dekady) */
      last_stat_s = HAL_GetTick();
      app_gpsdo_tick_stats_sample();
    }
    if (HAL_GetTick() - last_stat_d >= 1000) {       /* trend/offset/σy/drift 1/s */
      last_stat_d = HAL_GetTick();
      app_gpsdo_tick_stats_draw();
    }
    if (HAL_GetTick() - last_allan >= 1000) {
      last_allan = HAL_GetTick();
      app_gpsdo_tick_allan_draw();
    }

    /* Present coalescing: ticky vyse jen renderuji (znaci dirty); jeden flip na
     * ~30 Hz gate slouci vsechny zmeny -> mene VBR flipu + sjednoceny copy-forward. */
    static uint32_t last_present = 0;
    if (HAL_GetTick() - last_present >= 33) {
      last_present = HAL_GetTick();
      app_gpsdo_flush();
    }

    osDelay(10);   /* smycka ~100 Hz (jemne gate): freq 20x/s, bargraf 10x/s, touch 15x/s */
  }
}
