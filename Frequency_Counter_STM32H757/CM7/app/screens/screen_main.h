#pragma once
/**
 * @file screen_main.h
 * @brief GPSDO main screen (libui composition). Static iteration-1 data.
 *
 * App layer: composes ui_* components into the v9 layout. Only prim_blit /
 * prim_draw_text / prim_set_target / prim_get_target / prim_text_width /
 * prim_fb_init are used from libprim directly (cache management); everything
 * visual goes through libui.
 */

#include <prim/types.h>
#include <ui/dimensions.h>   /* UI_DIM_* used in the layout macros below */
#include <ui/digit_group.h>

/* ── Screen-specific layout constants (beyond UI_DIM_*). ─────── */
#define SCR_MAIN_HEADER_X          5       /* pill row near the left edge */
#define SCR_MAIN_TITLE_Y           (UI_DIM_BODY_Y + 20)
/* Number + everything below position (tuned). */
#define SCR_MAIN_NUMBER_Y_BASELINE (UI_DIM_BODY_Y + 94)
#define SCR_MAIN_GRID_Y            (UI_DIM_BODY_Y + 110)
#define SCR_MAIN_GRID_GAP          14
#define SCR_MAIN_GRID_LEFT_RATIO   53      /* percent (≈1.15 : 1) */

#define SCR_MAIN_CARD_SECTION_GAP  11
#define SCR_MAIN_SMALL_CARD_H      56

#define SCR_MAIN_BG_CACHE_W        UI_DIM_SCREEN_W
#define SCR_MAIN_BG_CACHE_H        UI_DIM_SCREEN_H

/* ── Render API ─────────────────────────────────────────────── */
void screen_main_init(void);        /* pre-render static caches (once) */
void screen_main_render(void);      /* full render into current target */
void screen_main_invalidate(void);  /* force cache rebuild */
const prim_pixel_t *screen_main_bg(void);    /* shared background cache (RGB565) */
int  screen_main_hit_button(int16_t x, int16_t y);  /* footer button idx or -1 */
void screen_main_button_action(int idx);            /* apply toggle/cycle for button idx */
void screen_main_redraw_title(void);                /* redraw only the title row */
void screen_main_redraw_button(int idx);            /* redraw only one footer button */
int  screen_main_redraw_time(uint32_t ms_since_boot);  /* cas+datum z GPS; vrati 1 pokud prekreslil */
int  screen_main_redraw_header(void);                  /* horni lista: GNSS lock + pocet druzic + cas/datum z GPS */
int  screen_main_redraw_signal(int16_t pct, int32_t dbm10); /* RF vykon z AD8307 bargraf (pct + dBm×10); vrati 1 */
int  screen_main_redraw_freq(void);                    /* simulovany kmitocet (per-segment dirty); vrati 1 */
void screen_main_freq_sim_step(void);                  /* krok simulace BEZ kresleni (mimo main obrazovku) */
void screen_main_stats_sample(void);                   /* navzorkuj frakcni odchylku (~1x/s) */
int  screen_main_redraw_stats(void);                   /* zivy trend + offset/sigma (~1x/s); vrati 1 */
int  screen_main_redraw_allan(void);                   /* zivy Allan graf (~1x/s); vrati 1 */
bool screen_main_is_running(void);                     /* RUN/STOP: bezi mereni? */
bool screen_main_hit_gnss(int16_t x, int16_t y);       /* tap do GNSS pill v hlavicce? */
bool screen_main_hit_sys(int16_t x, int16_t y);        /* tap do SYS pill v hlavicce? */
int  screen_main_sys_poll(void);                       /* 1 = zmena SYS zdravi -> prekresli header */
bool screen_main_hit_allan(int16_t x, int16_t y);      /* tap do Allan karty -> histogram okno? */
bool screen_main_hit_trend(int16_t x, int16_t y);      /* tap do trend karty -> fullscreen trend? */
void screen_main_render_trend_big(prim_rect_t rect);   /* fullscreen trend (okno s_trend_secs) do rect */
void screen_main_trend_set_secs(int s);                /* nastav casove okno trendu [s] */
int  screen_main_trend_secs(void);                     /* aktualni okno trendu [s] */
void screen_main_render_histogram(prim_rect_t rect);   /* histogram distribuce y do rect (okno) */
void screen_main_render_stats_table(prim_rect_t rect);  /* σy(τ) Allan tabulka do rect (okno) */
bool screen_main_hist_logy(void);                       /* stav lin/log Y osy histogramu */
void screen_main_hist_toggle_logy(void);                /* prepni lin<->log Y osu histogramu */
uint32_t screen_main_stats_version(void);               /* verze dat (change-key histogram okna) */
bool screen_main_selftest(void);                        /* fmt_frac+hist_h vektory (UART "selftest") */

/* ── Static data (defined in screen_main_data.c) ────────────── */
/* GNSS lock / pocet druzic / cas / datum jsou ZIVE z GPS (ne staticke). */
extern const char *SCR_S_SYS_READY;
extern const char *SCR_S_HDOP_L;   /* HDOP hodnota je ZIVE z GPS (render_header), ne staticka */
extern const char *SCR_S_CAL_L, *SCR_S_CAL_V, *SCR_S_HOLD_L, *SCR_S_HOLD_V;
extern const char *SCR_S_TITLE_RIGHT;
extern const ui_digit_segment_t SCR_MAIN_DIGITS[];
extern const int16_t SCR_MAIN_DIGIT_COUNT;
extern const char *SCR_MAIN_SEPS, *SCR_S_UNIT_HZ;
/* Offset/sigma/trend hodnoty jsou POCITANE ze statistiky (screen_main.c), jen labely zde. */
extern const char *SCR_S_OFFSET_L;
extern const char *SCR_S_TREND_L;
extern const char *SCR_S_SIGNAL_L;   /* signal bargraph label (hodnota+% simulovane) */
/* Allan: osy staticke, krivka POCITANA (ADEV) v render_card_allan. */
extern const char *SCR_ALLAN_Y_TICKS[];   /* X popisky (τ) generuje render_card_allan dynamicky */
extern const char *SCR_ALLAN_X_LABEL;
extern const char *SCR_S_BTN_RUN, *SCR_S_BTN_GATE_L,
                  *SCR_S_BTN_CHAN_L, *SCR_S_BTN_MENU;
