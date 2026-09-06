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
#include "encoder.h"    /* Faze A: rotacni encoder + gesta */
#include "screens/screen_main.h"   /* screen_main_stats_reset — g_stats_reset_req */
#include "freertos_shared.h"
#include "watchdog.h"     /* watchdog_kick_ui — heartbeat */
#include "alarm.h"        /* alarm_click — zvukova odezva doteku */
#include "beeper.h"       /* beeper_boot_melody — startovni jingle */

/* (LTDC adresu ridi prim_stm32_present() v app/hal -> hltdc tu uz netreba.) */

/* ── 🔬 Smi recovery SAHAT NA ATTINY? (experiment k mrtve I2C4, 2026-08-30) ────
 * Recovery od `7a4e100` (13.8.) po uvolneni sbernice jeste ZAPISE do ATTINY
 * `PORTC` (HW reset dotykoveho radice). Podezreni: prave tenhle zapis promeni
 * PRECHODNY vypadek v TRVALOU smrt sbernice.
 *
 * ⚠️⚠️ STAV DUKAZU (necti to jako potvrzenou pricinu — NENI):
 *   - Zapis na ATTINY SAM O SOBE sbernici NEZABIJI. Zmereno zatezovym testem
 *     (`tools/attiny_stress.ps1`): **40 OVERENYCH zapisu jasu -> 0 chyb**.
 *     Potvrzuje to i praxe — rucni zmena jasu sbernici nikdy neshodila.
 *   - Objem provozu to taky NENI: pred 2.8. se pollovalo pevnych 30 Hz,
 *     dnes 15 Hz v klidu (PULKA) a zlobilo to porad.
 *   - Co PROKAZATELNE zabiji: **halt cile ladici sondou**. Ze zdrave sbernice
 *     `STM32_Programmer_CLI -r32` -> 6 / 13 / 22 chyb a `scanner` uz nenajde nic
 *     (`tools/probe_test.ps1`). Halt uprostred transakce nechá SCL v nule a
 *     bit-bang slave to neprezije. ⚠️ Tim byla znacna cast merení 2026-08-30
 *     KONTAMINOVANA — zavery delej jen z behu bez sondy.
 *   - Recovery je porad podezrela jen tim, ze pise do ATTINY, kdyz uz sbernice
 *     zlobi (8 chyb v rade) — tedy do cipu, ktery muze byt uprostred ztraty
 *     synchronizace. NEOVERENO. Zaroven ten HW reset TP nikdy prokazatelne
 *     nepomohl (`s_touch_resets` doslo na 6 i 18 a sbernice byla mrtva stejne).
 *
 * 0 = recovery dela JEN sbernicovou cast (pulzy SCL + re-init), na ATTINY nesaha.
 * 1 = puvodni chovani (vcetne HW resetu TP pres ATTINY). */
#define I2C4_RECOVERY_TOUCHES_ATTINY 0

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

    /* 🔴🔴 ODR MUSI byt 1 JESTE PRED prepnutim pinu do OUTPUT_OD.
     * `HAL_GPIO_Init` na ODR NESAHA, takze pri ODR=0 pin v okamziku prepnuti
     * OKAMZITE STAHNE SCL k zemi. A protoze se dole pulzuje jen kdyz SDA drzi
     * slave, staci aby byla SDA vysoko — telo smycky se NEPROVEDE,
     * `WritePin(SET)` se nikdy nezavola a SCL zustane drzena dole NATRVALO.
     * Recovery pak sbernici misto zachrany ZABIJI, a to pri kazdem dalsim
     * pokusu znovu (rate-limit ji jen zpomali).
     * ⚠️ HW nalez 2026-08-30 sondou: `GPIOH IDR` SCL(PH11)=0, SDA(PH12)=1,
     * `GPIOH ODR`=0x0 (ODR11=0), MODER PH11=AF, `I2C4 ISR`=0x8001 (BUSY).
     * Projev: I2C4 umrela ~7 s po bootu (TMP117 0x48 melo 14 platnych cteni
     * a pak 8351 chyb v rade), dotyk mrtvy, `s_touch_resets`=18 bez efektu.
     * Uzivatel to videl az u sporice — driv dotyk nepotreboval. */
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_SET);   /* SCL uvolnena (OD: 1 = pull-up) */

    g.Pin = GPIO_PIN_11;                       /* SCL rucne (open-drain) */
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL;                      /* pull-upy jsou externi (panel) */
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOH, &g);
    g.Pin = GPIO_PIN_12; g.Mode = GPIO_MODE_INPUT;   /* SDA jen sledujeme */
    HAL_GPIO_Init(GPIOH, &g);

    /* Pulzy jen kdyz SDA opravdu drzi slave (klasicke docvakani drzeneho bajtu). */
    for (int i = 0; i < 9 && HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_12) == GPIO_PIN_RESET; i++) {
        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_RESET); osDelay(1);
        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_SET);   osDelay(1);
    }
    /* ⚠️ SCL se uvolnuje BEZPODMINECNE — i kdyz smycka nebezela (viz vyse). */
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_SET);

    g.Pin = GPIO_PIN_11 | GPIO_PIN_12;         /* zpet na AF4 (jako MSP init) */
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_NOPULL;
    g.Alternate = GPIO_AF4_I2C4;
    HAL_GPIO_Init(GPIOH, &g);
    __HAL_I2C_DISABLE(&hi2c4);                 /* PE=0 resetuje stavovy automat a pusti linky */
    HAL_I2C_Init(&hi2c4);                      /* navratovou hodnotu zamerne neresime */
}

/* Stav linek I2C4 pro diagnostiku (`status`): bit0 SCL, bit1 SDA, bit2 I2C BUSY.
 * Cte se primo z IDR/ISR, takze rekne PRAVDU i kdyz je sbernice zaseknuta —
 * bez teto informace se „mrtvy dotyk" nedal odlisit od „zaseknuty UiTask". */
/* 1 = recovery ma ZAKAZANO sahat na ATTINY (bezici experiment, viz
 * I2C4_RECOVERY_TOUCHES_ATTINY). `status` to hlasi, aby bylo poznat, ze
 * recovery nedela HW reset dotykoveho radice. Jas funguje normalne. */
int i2c4_diag_no_attiny_write(void) { return !I2C4_RECOVERY_TOUCHES_ATTINY; }

/* ── Pocitadla zapisu na ATTINY + stav ztlumeni (pro `status`) ────────────────
 * ⚠️ Bez nich se NEDA overit, jestli test zapisu na ATTINY vubec neco zapsal:
 * pri aktivnim sporici je `bl_target` = AUTODIM_LEVEL bez ohledu na
 * `g_brightness`, takze zmena jasu (SCPI `DISP:BRIG`) NEVYVOLA zadny zapis.
 * Presne na tohle jsem naletel pri prvnim mereni (50 "zapisu" -> 0 chyb, ale
 * nejspis se nezapsalo nic). Merit se da jen to, co je videt. */
static volatile uint32_t s_bl_writes_ok;   /* uspesnych zapisu jasu na ATTINY */
static volatile uint32_t s_bl_writes_skip; /* preskoceno (ATTINY neodpovedela na probe) */
static volatile uint8_t  s_bl_dimmed;      /* zrcadlo s_dimmed pro diagnostiku */

void i2c4_bl_stats(uint32_t *ok, uint32_t *skip, uint8_t *dimmed)
{
    if (ok)     *ok     = s_bl_writes_ok;
    if (skip)   *skip   = s_bl_writes_skip;
    if (dimmed) *dimmed = s_bl_dimmed;
}

uint8_t i2c4_line_state(void)
{
    uint8_t s = 0;
    if (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_11) == GPIO_PIN_SET) s |= 1u;   /* SCL */
    if (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_12) == GPIO_PIN_SET) s |= 2u;   /* SDA */
    if (hi2c4.Instance->ISR & I2C_ISR_BUSY)                   s |= 4u;
    return s;
}

/* #90 — akcelerace auto-repeatu drzeneho +/- doteku: interval [ms] podle poctu uz
 * vyvolanych opakovani (klesa 350->70). Protejsek adaptivniho kroku encoderu. */
static uint32_t touch_rep_interval(uint8_t n)
{
  static const uint16_t iv[] = { 350u, 300u, 250u, 200u, 150u, 110u, 80u, 70u };
  return (n < (sizeof iv / sizeof iv[0])) ? iv[n] : iv[(sizeof iv / sizeof iv[0]) - 1];
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
  /* 10 -> 5 tiku (2026-08-15): cely start byl 4,2 s od power-on a splash z toho
   * delal ~1,5 s. Fade ma SPLASH_FADE_TICKS=5, takze 5 tiku ho prave dokonci. */
  for (int i = 0; i < 5; i++) {            /* ~0,5 s + melodie ~= 1 s splash celkem */
    app_gpsdo_boot_splash_tick();
    osDelay(100);
  }

  /* Rotacni encoder (Faze A). Idempotentni, piny si nastavi sam — v `.ioc`
   * neni nic z toho (regen-safe, stejny vzor jako CS ve `fpga_freq_init`). */
  encoder_init();

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
    /* UART "meas reset" nastavi g_stats_reset_req; screen_main.c stav smi menit
     * jen UiTask (viz g_screen_req vyse). Alarm citace resetuje primo volajici
     * task — g_alarm_* jsou proste volatile globaly, stejne bezzamkove jako
     * jinde v projektu. */
    if (g_stats_reset_req) {
      g_stats_reset_req = 0;
      screen_main_stats_reset();
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
    static uint32_t s_bl_settle = 0;    /* cas posledniho zapisu jasu do ATTINY — po nem
                                         * ~150 ms NEcteme FT5x06 (viz auto-dim blok nize) */
    static uint32_t s_touch_ok_ms   = 0;   /* cas posledniho USPESNEHO cteni dotyku */
    static uint32_t s_banner_ms     = 0;   /* posledni vykresleni hlasky "dotyk nedostupny" */
    static uint8_t  s_touch_is_dead = 0;   /* 1 = hlaska je na displeji */
    /* #90 — dotykova parita s encoderem: drzeni prstu -> dlouhy stisk + auto-repeat
     * s akceleraci. Bez toho byl dotyk ciste hranovy (jen nastup) a chybel protejsek
     * dlouheho stisku i adaptivniho kroku. ⚠️ Auto-repeat se zapne JEN kdyz app
     * oznaci hit jako +/- (app_gpsdo_touch_repeat_armed) -> RUN/STOP/MENU se neopakuji.
     * Repeat i long-press bezi jen pri STABILNIM prstu (pohyb <= TOL) a jsou tazene
     * uspesnymi polly, takze pri I2C4 back-offu (2 Hz) se same zpomali. */
    static uint32_t s_down_ms   = 0;       /* cas nastupni hrany doteku */
    static int16_t  s_down_x    = 0, s_down_y = 0;
    static uint8_t  s_long_fired = 0;      /* dlouhy stisk uz vyvolan (1x za drzeni) */
    static uint8_t  s_rep_armed  = 0;      /* drzeny prvek je opakovatelny +/- */
    static uint32_t s_rep_next   = 0;      /* kdy vyvolat dalsi opakovani */
    static uint8_t  s_rep_n      = 0;      /* pocet uz vyvolanych opakovani (akcelerace) */
    #define TOUCH_LONG_MS    600u          /* prah dlouheho stisku (>=600 kvuli 2 Hz back-offu, #90) */
    #define TOUCH_REP_DELAY  500u          /* prodleva pred prvnim opakovanim */
    #define TOUCH_MOVE_TOL   40            /* max posun prstu, aby to stale bylo „drzeni" [px] */
    /* ADAPTIVNI gate: 33 ms (30 Hz) BEHEM a ~1,5 s PO interakci (svizne tapy —
     * hlavne opakovane +/- v Nastaveni), jinak 66 ms (15 Hz) v klidu. Touch cteni
     * je 31 B ~3 ms @100 kHz -> 30 Hz = ~9 % CPU, 15 Hz = ~4,5 %. V typickem stavu
     * (sledovani hlavni obrazovky, zadny dotek) se tak usetri ~4,5 % CPU; jediny
     * dopad je latence PRVNIHO tapu po klidu az 66 ms (jednorazova, neznatelna).
     * Cteni je vzdy CELY ramec -> zadne riziko freeze (viz varovani vyse). */
    uint32_t touch_gate = (was_down || (HAL_GetTick() - s_last_activity) < 1500u) ? 33u : 66u;
    /* 🔴 BACK-OFF PRI MRTVE SBERNICI (2026-08-30) — I2C1 tenhle idiom uz mel
     * (`i2c1_backoff_ms` v SensorsTask), I2C4 NE, a stalo to desitky % CPU.
     * ZMERENO na desce: zdrava I2C4 -> `stats` UiTask ~60 %; mrtva I2C4 ->
     * **UiTask 91 %, IDLE 3 %**. Duvod: NEUSPESNE cteni dotyku neni zadarmo —
     * `HAL_I2C_Mem_Read` ceka na priznaky (timeout az 100 ms) a je to CISTY SPIN,
     * ktery neustupuje scheduleru. Pri 15-30 Hz to sezere skoro celou CPU
     * ZBYTECNE: dotyk stejne nefunguje, dokud se sbernice nevzpamatuje.
     * Prvni USPESNE cteni nuluje `s_i2c4_fail` -> kadence se okamzite vrati na
     * 30/15 Hz, takze v beznem provozu se NIC nemeni (guard se zapne az po 8
     * chybach v rade, tedy po ~0,5 s prokazatelne mrtve sbernice).
     * ⚠️ Recovery bezi na vlastnim casovaci, tohle ji nezpomali. */
    if (s_i2c4_fail >= 8u) touch_gate = 500u;   /* 2 Hz misto 15-30 Hz */
    /* ⚠️ Po zapisu jasu do ATTINY drz I2C4 ~150 ms v klidu, nez na ni pustis
     * dalsiho mastera (FT5x06). ATTINY je bit-bang I2C slave + PWM podsviceni;
     * zapis jasu tesne nasledovany START-em touch cteni mu rozhodi slave automat
     * -> drzi SDA -> mrtva I2C4 az do power-cyclu. Toto okno klidu je hlavni
     * pojistka, ktera vraci ztlumeni podsviceni ve screensaveru bezpecne. */
    if (HAL_GetTick() - last_touch >= touch_gate &&
        (HAL_GetTick() - s_bl_settle) >= 150u) {
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
      if (attempted) { if (got) { s_i2c4_fail = 0; s_touch_ok_ms = HAL_GetTick(); }
                       else if (s_i2c4_fail < 250) s_i2c4_fail++; }
      else if (s_i2c4_busy < 250) s_i2c4_busy++;
      /* ⚠️ Nulovat i pri uspechu — jinak je to SOUCET OD STARTU, ktery se jen
       * nasytí na 250 a v logu pak strasi "250 x mutex busy" i kdyz je zrovna
       * vsechno v poradku. Takhle to hlasi AKTUALNI serii, coz je pouzitelne. */
      if (attempted && got) s_i2c4_busy = 0;

      /* ⚠️⚠️ PREPSANO 2026-08-13 po nalezu na HW. Prvni verze delala recovery
       * kazdych 5 s DONEKONECNA a pokazde hned po `HAL_I2C_Init` zapisovala do
       * ATTINY dva PORTC bajty BEZ klidovych mezer. Na cili se ukazalo, ze pak
       * neodpovida NIC na I2C4 (ani ATTINY 0x45, ani FT5x06) pri VOLNE sbernici
       * (SCL=1, SDA=1) a bez chybovych priznaku — tedy same NACKy. Jinymi slovy:
       * zachrana s velkou pravdepodobnosti ROZBIJELA bit-bang slave automat
       * ATTINY, pred cimz kod na jinem miste sam varuje.
       *
       * Nova pravidla:
       *  1) ZPOMALOVANI misto bušení: 5 s -> 30 s -> 5 min. Kdyz to nepomohlo
       *     poteti, uz se ATTINY NEDOTYKAME vubec (viz bod 3).
       *  2) Klidove mezery kolem KAZDEHO zapisu do ATTINY (stejne jako u jasu).
       *  3) Nejdriv se ZEPTAME, jestli ATTINY vubec odpovida
       *     (`HAL_I2C_IsDeviceReady`). Kdyz ne, zapis by stejne neprosel a jen
       *     by dal mlatil do zaseknuteho automatu -> preskocit.
       * Zaseknuty ATTINY uz z principu nejde ozivit po sbernici, kterou sam
       * neobsluhuje — to umi jen power-cycle. Cilem je NEZHORSOVAT. */
      uint32_t rec_gap = (s_touch_resets < 2u) ? 5000u
                       : (s_touch_resets < 5u) ? 30000u : 300000u;
      if (s_i2c4_fail >= 8 && (HAL_GetTick() - s_i2c4_rec_t) > rec_gap) {
        s_i2c4_rec_t = HAL_GetTick();
        s_touch_resets++;
        printf("touch: I2C4 nereaguje (%u chyb, %u x mutex busy) -> recovery #%lu\n",
               s_i2c4_fail, s_i2c4_busy, (unsigned long)s_touch_resets);
        if (osMutexAcquire(i2c4MutexHandle, 100) == osOK) {
          i2c4_recover();          /* pasivni: pulzy SCL jen kdyz SDA drzi dole + re-init */
#if !I2C4_RECOVERY_TOUCHES_ATTINY
          /* 🔬 Experiment: recovery dela JEN sbernicovou cast (pulzy SCL + re-init).
           * Na ATTINY nesaha — viz zduvodneni u I2C4_RECOVERY_TOUCHES_ATTINY. */
          (void)0;
#else
          /* Sahat na ATTINY jen kdyz opravdu odpovida, a jen prvnich par pokusu. */
          if (s_touch_resets <= 5u &&
              HAL_I2C_IsDeviceReady(&hi2c4, WS_PANEL_I2C_ADDR, 2, 20) == HAL_OK) {
            osDelay(2);
            ws_panel_set_portc(&hi2c4, (uint8_t)(WS_PC_RUN & ~WS_PC_RST_TP_N));
            osDelay(5);            /* FT5x06 nRST potrebuje >1 ms */
            ws_panel_set_portc(&hi2c4, WS_PC_RUN);
            osDelay(2);
            s_touch_grace = HAL_GetTick();   /* radic nabiha ~300 ms */
          } else if (s_touch_resets == 6u) {
            printf("touch: ATTINY neodpovida ani po 5 pokusech -> davam ruce pryc.\n"
                   "       I2C4 (touch + TMP117 0x48 + podsviceni) je mrtva az do power-cyclu.\n");
          }
#endif
          osMutexRelease(i2c4MutexHandle);
        }
        s_i2c4_fail = 0;
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
        } else {
          uint32_t now = HAL_GetTick();
          int16_t  tx  = (int16_t)(799 - t.x), ty = (int16_t)(479 - t.y);
          if (t.valid && !was_down) {
            /* ── nastupni hrana = tap ── */
            s_last_activity = now;
            if (s_dimmed) {                /* probuzeni: prvni dotek jen rozsviti, nespusti akci */
              s_dimmed = 0;
              app_gpsdo_exit_screensaver();/* zpet na okno, ktere bylo pred usnutim */
              s_rep_armed = 0;
            } else {
              s_down_ms = now; s_down_x = tx; s_down_y = ty;
              s_long_fired = 0; s_rep_n = 0;
              if (app_gpsdo_handle_touch(tx, ty)) {
                alarm_click();             /* zvukova odezva obslouzeneho tlacitka (mute-aware) */
                /* auto-repeat jen na opakovatelnem +/- ovladaci (jinak by se drzeni
                 * RUN/STOP/MENU spoustelo znovu a znovu). */
                s_rep_armed = app_gpsdo_touch_repeat_armed() ? 1u : 0u;
                s_rep_next  = now + TOUCH_REP_DELAY;
              } else {
                s_rep_armed = 0;
              }
            }
          } else if (t.valid && was_down && !s_dimmed) {
            /* ── drzeni: long-press + auto-repeat, jen kdyz prst STOJI ── */
            s_last_activity = now;         /* drzeni je take cinnost -> nesmi usnout */
            int dx = tx - s_down_x; if (dx < 0) dx = -dx;
            int dy = ty - s_down_y; if (dy < 0) dy = -dy;
            if (dx <= TOUCH_MOVE_TOL && dy <= TOUCH_MOVE_TOL) {
              if (s_rep_armed && (int32_t)(now - s_rep_next) >= 0) {
                (void)app_gpsdo_handle_touch(tx, ty);   /* dalsi krok, BEZ alarm_click */
                if (!app_gpsdo_touch_repeat_armed()) {
                  s_rep_armed = 0;                       /* uz to neni +/- (napr. dorazil na kraj) */
                } else {
                  if (s_rep_n < 255u) s_rep_n++;
                  s_rep_next = now + touch_rep_interval(s_rep_n);
                }
              }
              /* dlouhy stisk = protejsek encoderu; jen mimo opakovatelny +/-. */
              if (!s_rep_armed && !s_long_fired && (now - s_down_ms) >= TOUCH_LONG_MS) {
                s_long_fired = 1;
                (void)app_gpsdo_handle_touch_long(tx, ty);
              }
            }
          }
        }
        was_down = (uint8_t)t.valid;
      }
    }

    /* ── Trvale mrtva I2C4 -> hlaska na displeji ───────────────────────────────
     * Kriterium je CAS od posledniho USPESNEHO cteni, ne pocet chyb: prechodne
     * vypadky se zotavi do ~1 s (zmereno 2026-08-30: err 2 / v rade 0, `scanner`
     * vzapeti nasel vsechna 3 zarizeni), kdezto skutecna smrt uz nikdy neprestane.
     * 30 s je proto bezpecne nad sumem a pod hranici, kdy by to uzivatele mateio.
     * ⚠️ Banner se MUSI oplacet periodicky — okna se pod nim dal renderuji
     * (tick/clock prekresli patku), takze jednorazove vykresleni by zmizelo. */
    if (s_touch_ok_ms == 0u) s_touch_ok_ms = HAL_GetTick();   /* start = "zatim ok" */
    uint8_t dead_now = ((HAL_GetTick() - s_touch_ok_ms) > 30000u) ? 1u : 0u;
    if (dead_now != s_touch_is_dead) {
      s_touch_is_dead = dead_now;
      app_gpsdo_touch_dead((int)dead_now);          /* hrana: vykresli / uklid */
      s_banner_ms = HAL_GetTick();
    } else if (dead_now && (HAL_GetTick() - s_banner_ms) >= 2000u) {
      app_gpsdo_touch_dead(1);                      /* obnov pres prekreslena okna */
      s_banner_ms = HAL_GetTick();
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
    /* ── Auto-dim podsviceni (ATTINY @ I2C4) — BEZPECNY zapis ─────────────────
     * Screensaver ZASE ztlumuje podsviceni na AUTODIM_LEVEL (spotreba), ale zapis
     * do ATTINY je zdokumentovany spousteci moment "mrtveho touche" (bit-bang
     * I2C slave + PWM; rozhozeny automat drzi SDA -> mrtva I2C4 do power-cyclu).
     * Tri pojistky:
     *   1) `HAL_I2C_IsDeviceReady` probe — kdyz ATTINY neACKne, zapis by stejne
     *      neprosel; jen zkusit znovu za 200 ms (ne bušit kazdych 10 ms).
     *   2) klidove mezery osDelay(3) pred i po transakci (slave resync).
     *   3) `s_bl_settle` -> touch poll ~150 ms po zapisu NEcte FT5x06 (aby ATTINY
     *      po zapisu jasu neschytal hned START od jineho mastera na tomtez busu). */
    uint8_t  bl_target = s_dimmed ? AUTODIM_LEVEL : g_brightness;
    s_bl_dimmed = s_dimmed;                    /* zrcadlo pro `status` */
    static uint32_t s_bl_try = 0;
    if ((uint8_t)s_bl != bl_target && (HAL_GetTick() - s_bl_try) >= 200u) {
      s_bl_try = HAL_GetTick();
      if (osMutexAcquire(i2c4MutexHandle, 20) == osOK) {
        if (HAL_I2C_IsDeviceReady(&hi2c4, WS_PANEL_I2C_ADDR, 2, 10) == HAL_OK) {
          osDelay(3);
          /* ⚠️ Zapis na ATTINY BEZ MOZNOSTI PREEMPCE. `HAL_I2C_Master_Transmit`
           * je pollovaci a UiTask ma prioritu BelowNormal — vyssi task ho muze
           * preemptnout MEZI BAJTY, kdy periferie DRZI SCL V NULE. Bit-bang
           * slave takove protazeni nemusi prezit; presne tenhle efekt ma i halt
           * ladici sondou (zmereno: 1 cteni sondou = 6 chyb na I2C4).
           * `vTaskSuspendAll` NEvypina preruseni, jen prepinani tasku — a
           * `HAL_GetTick` jede z TIM6, takze timeouty uvnitr HAL dal funguji.
           * Zapis jsou 2 bajty @100 kHz ≈ 200 us, tedy zanedbatelne zdrzeni. */
          vTaskSuspendAll();
          bool blok = ws_panel_set_backlight(&hi2c4, bl_target);
          xTaskResumeAll();
          osDelay(3);
          if (blok) s_bl_writes_ok++; else s_bl_writes_skip++;
          s_bl = bl_target;
          s_bl_settle = HAL_GetTick();   /* drz touch poll ~150 ms dal od busu */
        } else {
          s_bl_writes_skip++;            /* ATTINY neodpovedela na probe */
        }
        osMutexRelease(i2c4MutexHandle);
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

    /* Encoder (Faze A): gesta -> fokus / navigace. Levne (cteni TIM1 + jednoho
     * GPIO), proto kazdou iteraci ~100 Hz — dlouhy stisk 1 s a dvojklik 400 ms
     * potrebuji jemne vzorkovani. ⚠️ Kresli, takze smi bezet JEN tady v UiTasku.
     * ⚠️ Bez tohohle volani se obsluha cela zahodi linkerem (`--gc-sections`)
     * a encoder mlci, aniz by cokoli ohlasilo chybu — presne to se 2026-08-31 stalo.
     *
     * 🔴 ENCODER PROBOUZI SYSTEM STEJNE JAKO DOTYK (2026-09-02). Do te doby na
     * `s_last_activity` ani `s_dimmed` vubec nesahal, takze:
     *   - otaceni knoflikem NEBRANILO usnuti (sporic naskocil uprostred prace),
     *   - a ze sporice uz encoder systém NEPROBUDIL vubec — slo to jen dotykem.
     * To porusovalo pravidlo DVOU UPLNYCH OVLADACICH CEST (kazda funkce musi jit
     * encoderem SAMOTNYM i dotykem SAMOTNYM).
     *
     * ⚠️ `encoder_poll()` se vola PRAVE TADY a jen jednou — je to
     * JEDNOKONZUMENTOVE API (druhy konzument si udalosti krade, zazito
     * 2026-08-31 u prikazu `enc`). Obsluha proto udalost dostava parametrem.
     * ⚠️ Prvni udalost po probuzeni se ZAHAZUJE, stejne jako prvni dotek —
     * v tme by uzivatel naslepo prestavil hodnotu, kterou nevidi. */
    {
      encoder_ev_t eev;
      encoder_poll(&eev);
      if (eev.steps || eev.short_press || eev.long_press || eev.double_click) {
        s_last_activity = HAL_GetTick();     /* jakykoli pohyb knoflikem = cinnost */
        if (s_dimmed) {
          s_dimmed = 0;
          app_gpsdo_exit_screensaver();      /* zpet na okno pred usnutim */
        } else {
          app_gpsdo_handle_encoder(&eev);
        }
      }
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
