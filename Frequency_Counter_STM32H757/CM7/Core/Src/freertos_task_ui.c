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

#include "app_gpsdo.h"
#include "freertos_shared.h"
/* Touch cte SensorsTask (I2C4) -> tady uz zadny i2c.h/ft5x06.h ani I2C4 mutex. */

/* (LTDC adresu ridi prim_stm32_present() v app/hal -> hltdc tu uz netreba.) */

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

  /* Boot rovnou do GPSDO obrazovky z primitiv (libprim/libui). Ta se kresli
   * VYHRADNE z tohoto tasku (knihovny nejsou thread-safe). Stary gfx/touch UI
   * byl odstranen — po startu bezi jen tato obrazovka. */
  g_screen_req = 3;

  for (;;) {
    /* UART prikazy "screen main"/"clear"/"ui" nastavi g_screen_req; zde se
     * obslouzi (req 3 = hlavni obrazovka, 4 = smazani). */
    uint8_t req = g_screen_req;
    if (req) {
      g_screen_req = 0;
      /* LTDC scan-out adresu ridi prim_stm32_present() (page-flip pri vblanku). */
      if (req == 4) app_gpsdo_clear();
      else          app_gpsdo_render_main();
    }

    /* Dotek: FT5x06 cte SensorsTask (I2C4) a pri DOWN edge zapise g_touch_xy +
     * zvysi g_touch_seq. Tady jen DRAINUJEME (porovnani seq) a volame render —
     * handle_touch MUSI byt v UiTasku (libui neni thread-safe). Blokujici 31B I2C
     * cteni je tim mimo render task. Souradnice uz jsou v display-space (zrcadleno). */
    static uint8_t last_touch_seq = 0;
    if (g_touch_seq != last_touch_seq) {
      last_touch_seq = g_touch_seq;
      uint32_t p = g_touch_xy;
      app_gpsdo_handle_touch((int16_t)(p >> 16), (int16_t)(p & 0xFFFFu));
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

    /* Animace simulovaneho signal bargrafu 30x/s (krok max 1 dilek, target-seeking,
     * lean redraw). */
    static uint32_t last_sig = 0;
    if (HAL_GetTick() - last_sig >= 33) {
      last_sig = HAL_GetTick();
      app_gpsdo_tick_signal();
    }

    /* Simulace kmitoctu 20x/s (spojita zmena, per-segment dirty redraw). */
    static uint32_t last_freq = 0;
    if (HAL_GetTick() - last_freq >= 50) {
      last_freq = HAL_GetTick();
      app_gpsdo_tick_freq();
    }

    /* Present coalescing: ticky vyse jen renderuji (znaci dirty); jeden flip na
     * ~30 Hz gate slouci vsechny zmeny -> mene VBR flipu + sjednoceny copy-forward. */
    static uint32_t last_present = 0;
    if (HAL_GetTick() - last_present >= 33) {
      last_present = HAL_GetTick();
      app_gpsdo_flush();
    }

    osDelay(10);   /* smycka ~100 Hz (jemne gate): freq 20x/s, bargraf ~30x/s, touch 15x/s */
  }
}
