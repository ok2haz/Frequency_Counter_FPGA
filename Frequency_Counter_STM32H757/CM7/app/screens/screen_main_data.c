/**
 * @file screen_main_data.c
 * @brief Static iteration-1 data for the GPSDO main screen (no live values).
 */

#include "screen_main.h"
#include <ui/digit_group.h>
#include <ui/theme.h>

/* ── Header ─────────────────────────────────────────────────── */
/* GNSS lock pill, pocet druzic, cas i datum jsou ZIVE z GPS (render_header /
 * screen_main_redraw_time v screen_main.c ctou gps_get()). Drivejsi staticke
 * SCR_S_GNSS_LOCK / SCR_S_SAT_VAL / SCR_S_DATE proto odstraneny.
 * HDOP je take ZIVE z GPS (render_header formatuje g.hdop). CAL/HOLD zustavaji
 * statickymi placeholdery (zatim bez dat). */
const char *SCR_S_SYS_READY = "System";
const char *SCR_S_HDOP_L    = "HDOP";   /* hodnota se bere ZIVE z GPS (g.hdop) */
const char *SCR_S_CAL_L     = "CAL";
const char *SCR_S_CAL_V     = "4 min";
const char *SCR_S_HOLD_L    = "HOLD";
const char *SCR_S_HOLD_V    = "2h 14m";

/* ── Title row (mode/channel/gate now come from the live UI state in
 *    screen_main.c; only the static right-hand annotation stays here). ── */
const char *SCR_S_TITLE_RIGHT = "N 312 · Ω regrese";

/* ── Main number: 99 999 999 999 88 5 6 Hz, 12 certain digits ── */
const ui_digit_segment_t SCR_MAIN_DIGITS[] = {
    {"99",  UI_DIGIT_CERTAIN, false},
    {"999", UI_DIGIT_CERTAIN, false},
    {"999", UI_DIGIT_CERTAIN, false},
    {"999", UI_DIGIT_CERTAIN, false},
    {"88",  UI_DIGIT_CERTAIN, true},
    {"5",   UI_DIGIT_SIGMA,   false},
    {"6",   UI_DIGIT_FLOOR,   false},
};
const int16_t SCR_MAIN_DIGIT_COUNT = 7;
const char *SCR_MAIN_SEPS = "..,.";   /* 4 separators between 5 groups */
const char *SCR_S_UNIT_HZ = "Hz";

/* ── Cards (jen LABELY; hodnoty offset/sigma/trend jsou POCITANE ze statistiky
 *    simulovaneho kmitoctu — viz screen_main.c) ──────────────────────────── */
const char *SCR_S_OFFSET_L = "Offset";   /* σy 1s/10s labely jsou literaly v screen_main.c */
const char *SCR_S_TREND_L  = "Trend 60 s";
const char *SCR_S_SIGNAL_L = "Signál vstupu";

/* ── Allan graf: Y popisky (dekady, horni indexy — mono fonty maji plny charset).
 * Magnituda ~1e-8 -> 10⁻⁶ (nahore) .. 10⁻¹⁰ (dole). Pouziva je sdileny renderer
 * `allan_plot` (karta na hlavni obrazovce mono_14, okno ALLAN mono_16). X osa je
 * DYNAMICKA — popisky τ si graf generuje sam. Osu "τ [s]" vysvetluje header
 * karty okna (drivejsi SCR_ALLAN_X_LABEL odstranen). */
const char *SCR_ALLAN_Y_TICKS[] = {"10⁻⁶", "10⁻⁷", "10⁻⁸", "10⁻⁹", "10⁻¹⁰"};

/* ── Footer button labels (values like gate time / channel come from the
 *    live UI state in screen_main.c). ── */
const char *SCR_S_BTN_RUN      = "▶ RUN";
const char *SCR_S_BTN_GATE_L   = "GATE";
const char *SCR_S_BTN_CHAN_L   = "CHAN";
const char *SCR_S_BTN_MENU     = "≡ MENU";
