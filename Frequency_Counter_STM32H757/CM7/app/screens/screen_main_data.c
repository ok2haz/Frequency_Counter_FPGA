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
const char *SCR_S_HDOP_L    = "HDOP";   /* hodnota se bere ZIVE z GPS (g.hdop) */
const char *SCR_S_HOLD_L    = "HOLD";
const char *SCR_S_HOLD_V    = "2h 14m";

/* ── Title row (mode/channel/gate now come from the live UI state in
 *    screen_main.c; only the static right-hand annotation stays here). ── */
const char *SCR_S_TITLE_RIGHT = "N 312 · Ω regrese";

/* ── Main number ─────────────────────────────────────────────────────────────
 * ⚠️ Staticka predloha cislic (`SCR_MAIN_DIGITS`/`_DIGIT_COUNT`/`SCR_MAIN_SEPS`)
 * byla ODSTRANENA (#1, 2026-08-25): format velkeho cisla se od napojeni realnych
 * dat stavi ZA BEHU podle magnitudy mereni — viz `num_layout()`/`num_build_for()`
 * v screen_main.c. Zustava jen jednotka, ktera na mereni nezavisi. */
const char *SCR_S_UNIT_HZ = "Hz";

/* ── Cards (jen LABELY; hodnoty offset/sigma/trend jsou POCITANE ze statistiky
 *    simulovaneho kmitoctu — viz screen_main.c) ──────────────────────────── */
const char *SCR_S_OFFSET_L = "Offset";   /* σy 1s/10s labely jsou literaly v screen_main.c */
const char *SCR_S_TREND_L  = "Trend 60 s";
const char *SCR_S_SIGNAL_L = "Signál vstupu";


/* ── Footer button labels (values like gate time / channel come from the
 *    live UI state in screen_main.c). ── */
const char *SCR_S_BTN_RUN      = "▶ RUN";
const char *SCR_S_BTN_GATE_L   = "GATE";
const char *SCR_S_BTN_CHAN_L   = "CHAN";
const char *SCR_S_BTN_MENU     = "≡ MENU";
