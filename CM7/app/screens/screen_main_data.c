/**
 * @file screen_main_data.c
 * @brief Static iteration-1 data for the GPSDO main screen (no live values).
 */

#include "screen_main.h"
#include <ui/digit_group.h>
#include <ui/theme.h>

/* ── Header ─────────────────────────────────────────────────── */
const char *SCR_S_GNSS_LOCK = "GNSS LOCK";
const char *SCR_S_SYS_READY = "System";
const char *SCR_S_SAT_VAL   = "9";
const char *SCR_S_HDOP_L    = "HDOP";
const char *SCR_S_HDOP_V    = "0,8";
const char *SCR_S_CAL_L     = "CAL";
const char *SCR_S_CAL_V     = "4 min";
const char *SCR_S_HOLD_L    = "HOLD";
const char *SCR_S_HOLD_V    = "2h 14m";
const char *SCR_S_TIME      = "14:32:07";
const char *SCR_S_DATE      = "Út · 2026-06-16 · UTC";

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

/* ── Cards ──────────────────────────────────────────────────── */
const char *SCR_S_OFFSET_L = "Offset";
const char *SCR_S_OFFSET_V = "+1,2×10⁻¹¹";
const char *SCR_S_SIGMA_L  = "σy @ 1 s";
const char *SCR_S_SIGMA_V  = "±2,1×10⁻¹²";
const char *SCR_S_TREND_L  = "Trend 60 s";
const char *SCR_S_TREND_R  = "● stabilní · ±2,4×10⁻¹² p–p";
/* Input signal intensity bargraph (replaces Perioda T / Reference cards). */
const char *SCR_S_SIGNAL_L = "Signál vstupu";
const char *SCR_S_SIGNAL_V = "-60 dBm";
const int16_t SCR_SIGNAL_PCT = 67;      /* fill to ~2/3 (green ends here) */

/* ── Allan curve — pixels inside the chart inner rect (320×130) ── */
const prim_point_t SCR_ALLAN_CURVE[] = {
    {30, 16}, {66, 30}, {100, 50}, {135, 60}, {170, 66},
    {205, 72}, {240, 76}, {275, 76}, {310, 74},
};
const int16_t SCR_ALLAN_CURVE_COUNT = 9;

const char *SCR_ALLAN_X_TICKS[] = {"0,1", "1", "10", "100", "1k"};
const char *SCR_ALLAN_Y_TICKS[] = {"10⁻⁹", "10⁻¹⁰", "10⁻¹¹", "10⁻¹²", "10⁻¹³"};
const char *SCR_ALLAN_X_LABEL   = "τ [s]";
const char *SCR_ALLAN_CURSOR_L  = "τ = 1 s";

/* ── Sparkline (21 samples, normalized 0..255) ──────────────── */
const int16_t SCR_SPARKLINE_VALUES[] = {
    115, 140, 102, 153, 122, 148, 97, 140, 166, 115,
    148, 128, 153, 133, 107, 140, 128, 153, 122, 140, 133,
};
const int16_t SCR_SPARKLINE_COUNT     = 21;
const int16_t SCR_SPARKLINE_SIGMA_MIN = 107;
const int16_t SCR_SPARKLINE_SIGMA_MAX = 148;

/* ── Footer button labels (values like gate time / channel come from the
 *    live UI state in screen_main.c). ── */
const char *SCR_S_BTN_RUN      = "▶ RUN";
const char *SCR_S_BTN_GATE_L   = "GATE";
const char *SCR_S_BTN_CHAN_L   = "CHAN";
const char *SCR_S_BTN_MENU     = "≡ MENU";
