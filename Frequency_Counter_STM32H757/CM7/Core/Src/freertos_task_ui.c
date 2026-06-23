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

#include "i2c.h"          /* hi2c4 (touch) */
#include "ft5x06.h"
#include "app_gpsdo.h"
#include "freertos_shared.h"

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

    /* Dotek pro tlacitka (FT5x06 @ I2C4). Panel zrcadleny X i Y -> (799-x,479-y).
     * ⚠️ MUSI se cist CELY 31B ramec (ft5x06_read_touch) — castecne cteni controller
     * DRZI I2C -> freeze. Gate ~66 ms (15 Hz). Hranove spousteni (jen zacatek doteku). */
    static uint8_t was_down = 0;
    static uint32_t last_touch = 0;
    if (HAL_GetTick() - last_touch >= 66) {
      last_touch = HAL_GetTick();
      ft5x06_touch_t t; int got = 0;
      if (osMutexAcquire(i2c4MutexHandle, 20) == osOK) {
        got = ft5x06_read_touch(&hi2c4, &t);
        osMutexRelease(i2c4MutexHandle);
      }
      if (got) {
        if (t.valid && !was_down)
          app_gpsdo_handle_touch((int16_t)(799 - t.x), (int16_t)(479 - t.y));
        was_down = (uint8_t)t.valid;
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

    /* GPSDO statistika: vzorkovani frakcni odchylky 2x/s; trend+offset+sigma
     * prekreslit 2x/s, Allan (tezsi render) 1x/s. */
    static uint32_t last_stat_s = 0, last_stat_d = 0, last_allan = 0;
    if (HAL_GetTick() - last_stat_s >= 500) {
      last_stat_s = HAL_GetTick();
      app_gpsdo_tick_stats_sample();
    }
    if (HAL_GetTick() - last_stat_d >= 500) {
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
