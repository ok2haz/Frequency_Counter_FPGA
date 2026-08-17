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
/* 2026-07-19: obvod hlavicky vyuzit az na doraz (byl 5 px vlevo, 12 px vpravo
 * u hodin — cisty nevyuzity okraj obrazovky, nikdo tam nic nekresli).
 * ⚠️ Rozpoctem rady pilulek NENI levy okraj TEXTU hodin, ale levy okraj CLEAR
 * ZON sekundoveho redrawu casu/data (x=648/644, viz screen_main_redraw_time)
 * -> rada je omezena HDR_PILL_LIMIT + fit-checkem v render_header a mezery
 * pilulek zustavaji kompaktni 4/5 (viz dimensions.h; docasny navrat na 5/6
 * revize tehoz dne vratila — s 5/6 by uz TYPICKA rada zasahla do clear zony). */
#define SCR_MAIN_HEADER_X          2       /* pill row near the left edge (bylo 5) */
#define SCR_MAIN_CLOCK_MARGIN      6       /* hodiny/zona od praveho okraje (bylo 12 pres UI_DIM_PADDING_X/2) */
#define SCR_MAIN_TITLE_Y           (UI_DIM_BODY_Y + 20)
/* Number + everything below position (tuned). */
#define SCR_MAIN_NUMBER_Y_BASELINE (UI_DIM_BODY_Y + 94)
#define SCR_MAIN_GRID_Y            (UI_DIM_BODY_Y + 110)
#define SCR_MAIN_GRID_GAP          14
/* Vnejsi okraj mrizky vlevo i vpravo. 12 -> 4 (2026-07-20): 12 px po obou
 * stranach byl cisty nevyuzity pruh — Allan zleva i pravy sloupec (trend/drift)
 * zprava do nej jen "koukaly". 4 px staci, aby se zaobleny roh karty
 * (UI_DIM_CARD_RADIUS 16) neslepil s hranou panelu. Zisk 16 px sirky se dle
 * SCR_MAIN_GRID_LEFT_RATIO deli mezi Allan (+8) a pravy sloupec (+8).
 * Plati pro OBA layouty (v1 i v2). */
#define SCR_MAIN_GRID_MARGIN       4
/* 47 % (372 px pri okraji 4) Allan vlevo | 406 px pravy sloupec (2026-07-19;
 * bylo 53 % — zuzeno, aby karty statistik (1/3 z praveho sloupce) unesly hodnoty
 * mono_18: nejhorsi "+9,9×10⁻¹⁰" = 100 px). Pri okraji 4 je vnitrek stat karty
 * 1/3 z 406 = 128 px (bylo 125) -> rezerva na mono_18 se JESTE zvetsila. */
#define SCR_MAIN_GRID_LEFT_RATIO   47

#define SCR_MAIN_CARD_SECTION_GAP  11
/* (SCR_MAIN_SMALL_CARD_H odstranen 2026-07-19 — mrtva konstanta, vysku
 * stat karet urcuje render_body_grid_v1/v2 lokalne.) */

#define SCR_MAIN_BG_CACHE_W        UI_DIM_SCREEN_W
#define SCR_MAIN_BG_CACHE_H        UI_DIM_SCREEN_H

/* ── Render API ─────────────────────────────────────────────── */
void screen_main_init(void);        /* pre-render static caches (once) */
void screen_main_render(void);      /* full render into current target */
void screen_main_invalidate(void);  /* force cache rebuild */
const prim_pixel_t *screen_main_bg(void);    /* shared background cache (RGB565) */
int  screen_main_hit_button(int16_t x, int16_t y);  /* footer button idx or -1 */
void screen_main_button_action(int idx);            /* apply toggle/cycle for button idx */
/* ── Dalkove ovladani (SCPI). Stav mereni vlastni UiTask — SCPI jen zapise
 * `g_ui_cfg_req` a UiTask ho aplikuje timhle. @return 1 = zmenilo se. */
int    screen_main_apply_cfg_req(void);
void screen_main_redraw_title(void);                /* redraw only the title row */
void screen_main_redraw_button(int idx);            /* redraw only one footer button */
void screen_main_button_flash_start(int idx);       /* micro-flash overlay pri stisku (item 3) */
int  screen_main_button_flash_tick(void);           /* ~20 Hz krok; 1 = prekreslil (odezni flash) */
int  screen_main_redraw_time(uint32_t ms_since_boot);  /* cas+datum z GPS; vrati 1 pokud prekreslil */
int  screen_main_redraw_header(void);                  /* horni lista: GNSS lock + pocet druzic + cas/datum z GPS */
int  screen_main_redraw_cpu(int force);                /* blok vytizeni CPU (CM7/CM4) v headeru; vrati 1 pokud kreslil */
int  screen_main_redraw_signal(int16_t pct, int32_t dbm10); /* RF vykon z AD8307 bargraf (pct + dBm×10); vrati 1 */
int  screen_main_redraw_freq(void);                    /* simulovany kmitocet (per-segment dirty); vrati 1 */
void screen_main_redraw_freq_area(void);               /* cela zona kmitoctu vc. RUN/STOP podbarveni (pri prepnuti RUN/STOP) */
void screen_main_freq_sim_step(void);                  /* krok simulace BEZ kresleni (mimo main obrazovku) */
float screen_main_freq_dev_unit(void);                 /* frakcni odchylka -> 0..1 (0,5=stred), pro spektrogram */
double screen_main_freq_hz(void);                      /* aktualni kmitocet [Hz] (Math/limity #43/#44) */
void screen_main_stats_sample(void);                   /* navzorkuj frakcni odchylku (~1x/s) */
int  screen_main_redraw_stats(void);                   /* zivy trend + offset/sigma (~1x/s); vrati 1 */
int  screen_main_tick_stats_anim(void);                /* ~20 Hz: eased dojezd Offset/σ/Drift (item 2, jen v2) */
int  screen_main_tick_trend_anim(void);                /* ~20 Hz: eased dojezd trend sparkline (item 4, jen v2) */
int  screen_main_tick_sys_xfade(void);                 /* ~20 Hz: prolinani barvy SYS pilulky (FX_SYS_XFADE) */
int  screen_main_redraw_allan(void);                   /* zivy Allan graf (~1x/s); vrati 1 */
bool screen_main_is_running(void);                     /* RUN/STOP: bezi mereni? */
bool screen_main_hit_gnss(int16_t x, int16_t y);       /* tap do GNSS pill v hlavicce? */
bool screen_main_hit_sys(int16_t x, int16_t y);        /* tap do SYS pill v hlavicce? */
int  screen_main_sys_poll(void);                       /* 1 = zmena SYS zdravi -> prekresli header */
bool screen_main_hit_allan(int16_t x, int16_t y);      /* tap do Allan nahledu -> ALLAN okno? */
bool screen_main_hit_trend(int16_t x, int16_t y);      /* tap do trend karty -> fullscreen trend? */
void screen_main_render_allan_big(prim_rect_t rect);   /* fullscreen Allan log-log graf (okno) */
void screen_main_set_allan_metric(int m);              /* 0=ADEV,1=TDEV,2=MTIE (prepinac v okne ALLAN) */
int  screen_main_allan_metric(void);                   /* aktualni metrika 0/1/2 */
void screen_main_render_trend_big(prim_rect_t rect);   /* fullscreen trend (okno s_trend_secs) do rect */
void screen_main_trend_set_secs(int s);                /* nastav casove okno trendu [s] */
int  screen_main_trend_secs(void);                     /* aktualni okno trendu [s] */
/* Doba [s] -> kompaktni text ("45 s" / "10 min" / "6 h" / "30 d"). Sdileno s app
 * vrstvou, aby popisek tlacitka a overlay trendu formatovaly stejne. */
void screen_main_fmt_dur(char *buf, int len, int32_t secs);
void screen_main_render_histogram(prim_rect_t rect);   /* histogram distribuce y do rect (okno) */
void screen_main_render_stats_table(prim_rect_t rect);  /* σy(τ) Allan tabulka do rect (okno) */
bool screen_main_hist_logy(void);                       /* stav lin/log Y osy histogramu */
void screen_main_hist_toggle_logy(void);                /* prepni lin<->log Y osu histogramu */
uint32_t screen_main_stats_version(void);               /* verze dat (change-key histogram okna) */
void screen_main_stats_reset(void);                     /* vynuluj Allan/Histogram/Trend akumulaci (UART "meas reset" + UI) */
bool screen_main_selftest(void);                        /* fmt_frac+hist_h vektory (UART "selftest") */

/* ── DOCASNA A/B srovnavaci vetev hlavni mrizky (2026-07-19, k odstraneni
 * po vyhodnoceni — viz STATUS.md TODO a komentar u screen_main_toggle_layout
 * v screen_main.c). Prepina footer tlacitko slotu 0 (docasne "Main SW"). ── */
void screen_main_toggle_layout(void);                   /* prepni stary/novy layout hlavni mrizky */
bool screen_main_layout_is_old(void);                   /* 1 = aktivni stary (pred 4,3" audit) layout */

/* ── Static data (defined in screen_main_data.c) ────────────── */
/* GNSS lock / pocet druzic / cas / datum jsou ZIVE z GPS (ne staticke). */
extern const char *SCR_S_HDOP_L;   /* HDOP hodnota je ZIVE z GPS (render_header), ne staticka */
extern const char *SCR_S_HOLD_L, *SCR_S_HOLD_V;
extern const char *SCR_S_TITLE_RIGHT;
extern const ui_digit_segment_t SCR_MAIN_DIGITS[];
extern const int16_t SCR_MAIN_DIGIT_COUNT;
extern const char *SCR_MAIN_SEPS, *SCR_S_UNIT_HZ;
/* Offset/sigma/trend hodnoty jsou POCITANE ze statistiky (screen_main.c), jen labely zde. */
extern const char *SCR_S_OFFSET_L;
extern const char *SCR_S_TREND_L;
extern const char *SCR_S_SIGNAL_L;   /* signal bargraph label (hodnota+% simulovane) */
extern const char *SCR_S_BTN_RUN, *SCR_S_BTN_GATE_L,
                  *SCR_S_BTN_CHAN_L, *SCR_S_BTN_MENU;
