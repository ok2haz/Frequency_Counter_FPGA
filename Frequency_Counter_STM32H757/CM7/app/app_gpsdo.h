#pragma once
/**
 * @file app_gpsdo.h
 * @brief Entry points for the libui-based GPSDO screens (main + diagnostics).
 *
 * Rendering and touch handling must happen from a single context (UiTask).
 */

#include <stdbool.h>
#include <stdint.h>

/** One-time init: bind libprim to the framebuffer, build static caches. */
void app_gpsdo_init(void);

/** Render the main screen into the hardware framebuffer (selects main view). */
void app_gpsdo_render_main(void);

/** Render the diagnostics screen (comm test, TMP117 temps, ADC). */
void app_gpsdo_render_diag(void);

/** Clear the framebuffer to the background color. */
void app_gpsdo_clear(void);

/**
 * @brief Handle a screen-space touch-down at (x,y). Switches between main and
 *        diagnostics via the MENU / back buttons.
 * @return true if a button was hit and the screen was re-rendered.
 */
bool app_gpsdo_handle_touch(int16_t x, int16_t y);

/** Periodic tick (~2 Hz from UiTask): refreshes the diagnostics values. */
void app_gpsdo_tick(void);

/** Hodinovy tik (~10 Hz z UiTask): na hlavni obrazovce prekresli simulovany cas. */
void app_gpsdo_tick_clock(uint32_t ms_since_boot);
