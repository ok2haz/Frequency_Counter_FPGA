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

/** Render the GPS/GNSS info screen (live: FIX/sats/HDOP/time/pos; GNSS pill). */
void app_gpsdo_render_gps(void);

/** Render the System Health screen (RTOS/heap/stacks/I2C/links; SYS pill). Live. */
void app_gpsdo_render_health(void);

/** Render the all-sensors submenu (opened from System Health "SENZORY >"). Live. */
void app_gpsdo_render_sensors(void);

/** Render the memory-usage submenu (opened from System Health "PAMET >"). Live.
 *  Interni FLASH/RAM (linker), RTOS heap, externi SDRAM 32 MB + W25Q 64 MB (JEDEC). */
void app_gpsdo_render_mem(void);

/** Render the histogram window (opened by tapping the Allan card on main).
 *  Distribution of fractional deviation y = (f-f0)/f0. */
void app_gpsdo_render_histogram(void);

/** Render the settings window (opened from System Health "NASTAVENI").
 *  Sound mute (alarm), display brightness, auto-dim on/off + delay. RTC BKP persist. */
void app_gpsdo_render_settings(void);

/** Render the fullscreen trend window (tap trend card on main): full ring history. */
void app_gpsdo_render_trend(void);

/** Render the "About" window (from Settings): FW/build/authors/uptime/selftest. */
void app_gpsdo_render_about(void);

/** Render the Menu hub (from main MENU button): grid of buttons to all windows. */
void app_gpsdo_render_menu(void);

/** Selftest of pure app helpers (Maidenhead locator). Part of UART "selftest". */
bool app_gpsdo_selftest(void);

/** Screensaver hodiny (auto-dim): velke RTC hodiny na cernem pozadi. UiTask
 *  vola enter pri usnuti (dim) a exit pri probuzeni — exit obnovi predchozi okno. */
void app_gpsdo_enter_screensaver(void);
void app_gpsdo_exit_screensaver(void);

/** Boot splash: logo + FW/build + prubeh selftestu. UiTask vola jednou pri startu
 *  (app_gpsdo_boot_splash), pak periodicky _tick (aktualizuje jen selftest radek). */
void app_gpsdo_boot_splash(void);
void app_gpsdo_boot_splash_tick(void);

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

/** Hodinovy tik (~10 Hz z UiTask): na hlavni obrazovce prekresli cas/datum z GPS
 *  + horni listu (GNSS lock / pocet druzic) pri zmene fixu. */
void app_gpsdo_tick_clock(uint32_t ms_since_boot);

/** Animace simulovaneho signal bargrafu (~10 Hz z UiTask, jen RUN, hlavni obrazovka). */
void app_gpsdo_tick_signal(void);

/** Simulace kmitoctu (~20 Hz z UiTask, jen hlavni obrazovka): per-segment dirty redraw. */
void app_gpsdo_tick_freq(void);

/** GPSDO statistika ze simulovaneho kmitoctu: vzorkovani frakcni odchylky (~1 Hz). */
void app_gpsdo_tick_stats_sample(void);

/** GPSDO statistika: zive prekresleni trend + offset/sigma (~1 Hz). */
void app_gpsdo_tick_stats_draw(void);

/** GPSDO statistika: zive prekresleni Allan grafu (~1 Hz, tezsi render). */
void app_gpsdo_tick_allan_draw(void);

/**
 * @brief Present coalescing: jeden flip pro vsechny nahromadene zmeny z ticku.
 *        Vola UiTask na ~30 Hz gate (ticky uz neflipuji samy). Vrati 1 pri flipu.
 */
int app_gpsdo_flush(void);
