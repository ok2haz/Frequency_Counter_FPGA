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

#include "i2c.h"          /* hi2c4 */
#include "ft5x06.h"
#include "app_gpsdo.h"
#include "freertos_shared.h"

extern LTDC_HandleTypeDef hltdc;

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
      HAL_LTDC_SetAddress(&hltdc, 0xC0000000u, 0);   /* LTDC scanuje nas FB */
      if (req == 4) app_gpsdo_clear();
      else          app_gpsdo_render_main();
    }

    /* Dotek pro tlacitka (FT5x06 @ I2C4, pod sdilenym mutexem). Panel je
     * zrcadleny v X i Y -> screen = (799-x, 479-y). Hranove spousteni
     * (reaguje jen na nabeznou hranu doteku, ne na drzeni). */
    static uint8_t was_down = 0;
    ft5x06_touch_t t;
    if (osMutexAcquire(i2c4MutexHandle, 20) == osOK) {
      uint8_t ok = ft5x06_read_touch(&hi2c4, &t);
      osMutexRelease(i2c4MutexHandle);
      if (ok && t.valid && !was_down) {
        app_gpsdo_handle_touch((int16_t)(799 - t.x), (int16_t)(479 - t.y));
      }
      was_down = (uint8_t)(ok && t.valid);
    }

    /* Obnova zivych hodnot v diagnostice ~2x/s (na hlavni obrazovce no-op). */
    static uint32_t last_tick = 0;
    if (HAL_GetTick() - last_tick >= 500) {
      last_tick = HAL_GetTick();
      app_gpsdo_tick();
    }

    osDelay(40);
  }
}
