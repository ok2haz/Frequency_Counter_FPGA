/**
 * @file screen_main.c
 * @brief GPSDO main screen layout (libui composition).
 *
 * screen_main_init() pre-renders the static background (radial gradient + header
 * chrome) into a cache and is allowed direct libprim access for that boot-time
 * work. screen_main_render() composes ui_* components and renders the big number
 * directly over the background (no opaque cache block → no seam, no crop).
 *
 * All buffers are RGB565; libprim composites in ARGB and packs to RGB565.
 */

#include "screen_main.h"
#include <ui/ui.h>
#include <prim/prim.h>
#include <stdio.h>   /* snprintf pro simulovany cas */
#include <string.h>  /* strncpy pro simulovane cislice */

#ifndef SCR_SDRAM_SECTION
#  if defined(__GNUC__) && !defined(PRIM_HOST_BUILD)
#    define SCR_SDRAM_SECTION __attribute__((section(".sdram"), aligned(32)))
#  else
#    define SCR_SDRAM_SECTION
#  endif
#endif

static prim_pixel_t bg_cache[SCR_MAIN_BG_CACHE_W * SCR_MAIN_BG_CACHE_H] SCR_SDRAM_SECTION;
static bool cache_initialized = false;

/* Simulovany cas HH:MM:SS (aktualizuje screen_main_redraw_time z uptime). */
static char s_time_buf[16] = "14:32:07";

/* Footer button hit areas, set during footer render. Index: 0=PERIOD/FREQ
 * toggle, 1=RUN/STOP, 2=GATE, 3=CHAN, 4=MENU. */
#define SCR_BTN_COUNT 5
static prim_rect_t s_btn_rect[SCR_BTN_COUNT];

/* Interactive UI state (iteration-1: drives labels/title, no live measurement). */
static const char *MODE_NAME[2] = {"FREQUENCY", "PERIOD"};
static const char *CHAN_NAME[2] = {"CH A", "CH B"};
static const char *GATE_VAL[4]  = {"0,1 s", "1 s", "10 s", "100 s"};
static struct { int8_t mode; int8_t chan; int8_t gate; bool running; }
    st = {0, 1, 1, false};   /* FREQUENCY, CH B, 1 s, stopped (shows "▶ RUN") */

const prim_pixel_t *screen_main_bg(void) { return bg_cache; }

static bool pt_in(int16_t x, int16_t y, prim_rect_t r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int screen_main_hit_button(int16_t x, int16_t y)
{
    for (int i = 0; i < SCR_BTN_COUNT; i++)
        if (pt_in(x, y, s_btn_rect[i])) return i;
    return -1;
}

void screen_main_button_action(int idx)
{
    switch (idx) {
    case 0: st.mode = (int8_t)(st.mode ? 0 : 1); break;   /* FREQ <-> PERIOD */
    case 1: st.running = !st.running;            break;   /* RUN <-> STOP */
    case 2: st.gate = (int8_t)((st.gate + 1) % 4); break; /* cycle gate */
    case 3: st.chan = (int8_t)(st.chan ? 0 : 1); break;   /* CH A <-> CH B */
    default: break;                                       /* 4 = MENU: handled by caller */
    }
}

/* ── Background pre-render (boot) ───────────────────────────── */

static void render_background_to_cache(void)
{
    prim_fb_t cache_fb;
    prim_fb_init(&cache_fb, bg_cache, SCR_MAIN_BG_CACHE_W, SCR_MAIN_BG_CACHE_H,
                 SCR_MAIN_BG_CACHE_W * sizeof(prim_pixel_t));
    prim_fb_t *prev = prim_get_target();
    prim_set_target(&cache_fb);

    prim_fill_gradient_radial(
        (prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
        (prim_point_t){UI_DIM_SCREEN_W / 2, UI_DIM_SCREEN_H / 2},
        0, 540, UI_COLOR_BG_1, UI_COLOR_BG_0);

    prim_fill_gradient_linear((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_HEADER_H},
                              UI_COLOR_BG_HEADER_TOP, UI_COLOR_BG_HEADER_BOT,
                              PRIM_GRAD_VERTICAL);
    prim_draw_line((prim_point_t){0, UI_DIM_HEADER_H - 1},
                   (prim_point_t){UI_DIM_SCREEN_W, UI_DIM_HEADER_H - 1},
                   1, UI_COLOR_LINE);
    prim_draw_line((prim_point_t){0, UI_DIM_SCREEN_H - UI_DIM_FOOTER_H},
                   (prim_point_t){UI_DIM_SCREEN_W, UI_DIM_SCREEN_H - UI_DIM_FOOTER_H},
                   1, UI_COLOR_LINE);

    prim_set_target(prev);
}

void screen_main_invalidate(void) { cache_initialized = false; }

void screen_main_init(void)
{
    if (cache_initialized) return;
    render_background_to_cache();
    cache_initialized = true;
}

/* ── Per-frame composition ──────────────────────────────────── */

static int16_t draw_word(int16_t x, int16_t y, const char *text,
                         const prim_font_t *font, prim_color_t color)
{
    prim_draw_text((prim_point_t){x, y}, text, font, color, PRIM_ALIGN_LEFT);
    return (int16_t)(x + prim_text_width(text, font));
}

/* Header: only the pills that fit (no overflow into the clock area). */
static void render_header(void)
{
    int16_t x = SCR_MAIN_HEADER_X;
    int16_t y = (UI_DIM_HEADER_H - UI_DIM_PILL_H) / 2;
    ui_pill_t p;

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_OK,
                    .value = SCR_S_GNSS_LOCK, .has_led = true};
    ui_pill_render(&p); x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_OK, .value = SCR_S_SYS_READY};
    ui_pill_render(&p); x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_NORMAL, .value = SCR_S_SAT_VAL,
                    .icon_render = ui_icon_sat_dish, .icon_size = 22,
                    .icon_color = UI_COLOR_OK_SOFT};
    ui_pill_render(&p); x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_NORMAL,
                    .label = SCR_S_HDOP_L, .value = SCR_S_HDOP_V};
    ui_pill_render(&p); x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_NORMAL,
                    .label = SCR_S_CAL_L, .value = SCR_S_CAL_V};
    ui_pill_render(&p); x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_NORMAL,
                    .label = SCR_S_HOLD_L, .value = SCR_S_HOLD_V};
    ui_pill_render(&p);

    int16_t time_x = UI_DIM_SCREEN_W - UI_DIM_PADDING_X / 2;   /* half the margin */
    prim_draw_text((prim_point_t){time_x, 23}, s_time_buf, &ui_font_mono_25,
                   UI_COLOR_INK, PRIM_ALIGN_RIGHT);
    prim_draw_text((prim_point_t){time_x, 46}, SCR_S_DATE, &ui_font_sans_14,
                   UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
}

static void render_body_title(void)
{
    int16_t x = UI_DIM_PADDING_X + 4;
    int16_t y = SCR_MAIN_TITLE_Y;
    x = draw_word(x, y, MODE_NAME[st.mode], &ui_font_mono_20, UI_COLOR_ACC);
    x = draw_word(x, y, "  ·  ",            &ui_font_mono_20, UI_COLOR_INK_4);
    x = draw_word(x, y, CHAN_NAME[st.chan], &ui_font_mono_20, UI_COLOR_INK_2);
    x = draw_word(x, y, "  ·  ",            &ui_font_mono_20, UI_COLOR_INK_4);
    x = draw_word(x, y, "GATE ",            &ui_font_mono_20, UI_COLOR_INK_2);
    x = draw_word(x, y, GATE_VAL[st.gate],  &ui_font_mono_20, UI_COLOR_INK_2);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W - UI_DIM_PADDING_X, y},
                   SCR_S_TITLE_RIGHT, &ui_font_mono_18, UI_COLOR_INK_3,
                   PRIM_ALIGN_RIGHT);
}

/* Velke cislo kmitoctu je SIMULOVANE: integer cast staticka, posledni 2 desetinne
 * cislice (sigma/floor) se dithuji (screen_main_redraw_freq, ~10x/s). Drzime
 * mutovatelne segmenty (kopie SCR_MAIN_DIGITS) + cachovany deskriptor a rect. */
static ui_digit_segment_t s_num_seg[8];
static char               s_num_buf[8][8];
static ui_big_number_t    s_num;
static prim_rect_t        s_num_tail_rect = {0, 0, 0, 0};  /* jen ditherovany ocas */
static int                s_num_ready = 0;

#define SCR_FREQ_TAIL_FROM 5   /* od ktereho segmentu se dithuje (posledni 2 cislice) */

static void num_build(void)
{
    int n = SCR_MAIN_DIGIT_COUNT;
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        strncpy(s_num_buf[i], SCR_MAIN_DIGITS[i].text, sizeof(s_num_buf[i]) - 1);
        s_num_buf[i][sizeof(s_num_buf[i]) - 1] = '\0';
        s_num_seg[i].text          = s_num_buf[i];
        s_num_seg[i].level         = SCR_MAIN_DIGITS[i].level;
        s_num_seg[i].with_underline = SCR_MAIN_DIGITS[i].with_underline;
    }
    s_num = (ui_big_number_t){
        .x_center = UI_DIM_SCREEN_W / 2, .y_baseline = SCR_MAIN_NUMBER_Y_BASELINE,
        .main_font = &ui_font_mono_75, .fade_font = &ui_font_mono_52,
        .sep_font = &ui_font_mono_25, .decimal_font = &ui_font_mono_30,
        .unit_font = &ui_font_sans_32, .segments = s_num_seg, .segment_count = (int16_t)n,
        .separators = SCR_MAIN_SEPS, .sep_color = UI_COLOR_INK_3,
        .decimal_color = UI_COLOR_ACC, .unit = SCR_S_UNIT_HZ, .unit_color = UI_COLOR_INK_2,
    };
    /* Tail rect: jen od ditherovaneho segmentu po pravy okraj cisla (vc. jednotky). */
    int16_t w    = ui_big_number_width(&s_num);
    int16_t left = (int16_t)(UI_DIM_SCREEN_W / 2 - w / 2);
    int16_t top  = (int16_t)(SCR_MAIN_NUMBER_Y_BASELINE - 72);
    int16_t tail_x = ui_big_number_seg_x(&s_num, SCR_FREQ_TAIL_FROM);
    s_num_tail_rect = (prim_rect_t){ (int16_t)(tail_x - 2), top,
                                     (int16_t)(left + w - tail_x + 8), 92 };
    s_num_ready = 1;
}

/* Big number rendered directly over the gradient background. */
static void render_body_number(void)
{
    if (!s_num_ready) num_build();   /* jednou; jitterovany stav pak prezije full render */
    ui_big_number_render(&s_num);
}

static void render_card_allan(prim_rect_t rect)
{
    ui_card_t card = {.rect = rect, .header_label = "Allan σy(τ)",
                      .header_right = "@ 1 s · 3,5×10⁻¹²",
                      .header_right_accent = UI_COLOR_ACC};
    ui_card_render_chrome(&card);
    prim_rect_t ci = ui_card_inner_rect(&card);
    /* Reserve a left gutter for Y-axis labels and a bottom strip for X labels,
     * and a small right margin so the last tick ("1k") stays inside. */
    prim_rect_t inner = {(int16_t)(ci.x + 36), ci.y,
                         (int16_t)(ci.w - 36 - 8), (int16_t)(ci.h - 22)};

    ui_chart_loglog_t chart = {.inner = inner, .x_decade_count = 4,
        .y_decade_count = 4, .x_min_exp = -1, .y_min_exp = -13,
        .x_label = SCR_ALLAN_X_LABEL, .x_tick_labels = SCR_ALLAN_X_TICKS,
        .y_tick_labels = SCR_ALLAN_Y_TICKS};
    ui_chart_loglog_render_grid(&chart);
    ui_chart_loglog_render_floor(&chart, (int16_t)(inner.y + (inner.h * 3) / 4),
                                 "GPSDO floor");
    ui_chart_loglog_render_curve(&chart, SCR_ALLAN_CURVE, SCR_ALLAN_CURVE_COUNT,
                                 UI_COLOR_ACC, true);
    ui_chart_loglog_render_cursor(&chart,
        (prim_point_t){(int16_t)(inner.x + inner.w / 4), (int16_t)(inner.y + 40)},
        SCR_ALLAN_CURSOR_L);
}

static void render_two_small_cards(prim_rect_t rect,
                                   const char *la, const char *va, prim_color_t ca,
                                   const char *lb, const char *vb, prim_color_t cb)
{
    int16_t half = (int16_t)((rect.w - SCR_MAIN_CARD_SECTION_GAP) / 2);
    ui_card_t a = {.rect = {rect.x, rect.y, half, rect.h}, .header_label = la};
    ui_card_t b = {.rect = {(int16_t)(rect.x + half + SCR_MAIN_CARD_SECTION_GAP),
                            rect.y, half, rect.h}, .header_label = lb};
    ui_card_render_chrome(&a);
    ui_card_render_chrome(&b);
    prim_rect_t ia = ui_card_inner_rect(&a);
    prim_rect_t ib = ui_card_inner_rect(&b);
    prim_draw_text((prim_point_t){ia.x, (int16_t)(ia.y + 15)}, va,
                   &ui_font_mono_16, ca, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){ib.x, (int16_t)(ib.y + 15)}, vb,
                   &ui_font_mono_16, cb, PRIM_ALIGN_LEFT);
}

static void render_card_trend(prim_rect_t rect)
{
    ui_card_t card = {.rect = rect, .header_label = SCR_S_TREND_L,
                      .header_right = SCR_S_TREND_R,
                      .header_right_accent = UI_COLOR_OK};
    ui_card_render_chrome(&card);
    prim_rect_t inner = ui_card_inner_rect(&card);
    ui_sparkline_t sp = {.inner = inner, .y_values = SCR_SPARKLINE_VALUES,
        .count = SCR_SPARKLINE_COUNT, .show_sigma_band = true,
        .sigma_min = SCR_SPARKLINE_SIGMA_MIN, .sigma_max = SCR_SPARKLINE_SIGMA_MAX,
        .show_endpoint_marker = true, .fill_below = false,
        .stroke_color = UI_COLOR_ACC};
    ui_sparkline_render(&sp);
}

/* Signal bargraf je SIMULOVANY (animovany ~30x/s, viz screen_main_redraw_signal).
 * Drzime jeho rect + aktualni hodnotu, aby sel prekreslit jen on (partial). */
static prim_rect_t s_signal_rect = {0, 0, 0, 0};
static int16_t     s_signal_pct  = 0;   /* simulace ho hned prepise (animace ~30x/s) */

/* dBm z pct: rozsah bargrafu 0..100 % -> -80..+20 dBm (100 dB). */
static int signal_dbm(int16_t pct) { return (int)pct - 80; }

/* Plne vykresleni signal karty (chrome + label + bar) — pro full render. */
static void draw_signal_card(prim_rect_t rect, int16_t pct)
{
    static char val[16];
    ui_card_t card = {.rect = rect};
    ui_card_render_chrome(&card);
    prim_rect_t inner = ui_card_inner_rect(&card);
    snprintf(val, sizeof(val), "%d dBm", signal_dbm(pct));
    ui_bargraph_t bar = {.rect = inner, .value_pct = pct,
                         .color = UI_COLOR_OK, .label = SCR_S_SIGNAL_L,
                         .value_text = val};
    ui_bargraph_render(&bar);
}

static void render_card_signal(prim_rect_t rect)
{
    s_signal_rect = rect;                 /* zapamatuj pro partial redraw */
    draw_signal_card(rect, s_signal_pct);
}

static void render_right_column(prim_rect_t rect)
{
    int16_t gap = SCR_MAIN_CARD_SECTION_GAP;
    int16_t small_h = 54;     /* taller: header + value no longer clips at bottom */
    int16_t signal_h = 43;    /* bargraph fits without bottom overflow */
    int16_t trend_h = (int16_t)(rect.h - small_h - signal_h - 2 * gap);

    int16_t y = rect.y;
    render_two_small_cards((prim_rect_t){rect.x, y, rect.w, small_h},
        SCR_S_OFFSET_L, SCR_S_OFFSET_V, UI_COLOR_OK,
        SCR_S_SIGMA_L, SCR_S_SIGMA_V, UI_COLOR_VIOLET);
    y = (int16_t)(y + small_h + gap);
    render_card_trend((prim_rect_t){rect.x, y, rect.w, trend_h});
    y = (int16_t)(y + trend_h + gap);
    render_card_signal((prim_rect_t){rect.x, y, rect.w, signal_h});
}

static void render_body_grid(void)
{
    int16_t right_margin = 12;
    int16_t allan_left = right_margin;   /* equal margins on both sides */
    int16_t grid_y = SCR_MAIN_GRID_Y;
    int16_t grid_h = (int16_t)(UI_DIM_BODY_H - (SCR_MAIN_GRID_Y - UI_DIM_BODY_Y) - 8);
    int16_t grid_w = (int16_t)(UI_DIM_SCREEN_W - right_margin - allan_left);
    int16_t left_w = (int16_t)((grid_w * SCR_MAIN_GRID_LEFT_RATIO) / 100);
    int16_t right_w = (int16_t)(grid_w - left_w - SCR_MAIN_GRID_GAP);
    int16_t left_x = allan_left;
    int16_t right_x = (int16_t)(left_x + left_w + SCR_MAIN_GRID_GAP);
    render_card_allan((prim_rect_t){left_x, grid_y, left_w, grid_h});
    render_right_column((prim_rect_t){right_x, grid_y, right_w, grid_h});
}

/* Label/value/variant of footer button i, derived from the UI state. */
static void footer_button_def(int i, const char **label, const char **value,
                              ui_button_variant_t *var)
{
    *value = 0;
    switch (i) {
    case 0: *label = MODE_NAME[st.mode ? 0 : 1]; *var = UI_BUTTON_NORMAL; break;
    case 1: *label = st.running ? "STOP" : SCR_S_BTN_RUN;
            *var = st.running ? UI_BUTTON_ACTIVE : UI_BUTTON_RUN; break;
    case 2: *label = SCR_S_BTN_GATE_L; *value = GATE_VAL[st.gate];  *var = UI_BUTTON_NORMAL; break;
    case 3: *label = SCR_S_BTN_CHAN_L; *value = CHAN_NAME[st.chan]; *var = UI_BUTTON_NORMAL; break;
    default: *label = SCR_S_BTN_MENU;  *var = UI_BUTTON_NORMAL; break;
    }
}

/* Footer: PERIOD/FREQ toggle, RUN/STOP, GATE, CHAN, MENU. RUN is 1.5x wide. */
static void render_footer(void)
{
    int16_t fy = UI_DIM_SCREEN_H - UI_DIM_FOOTER_H;
    int16_t pad = 12;
    int16_t total_w = (int16_t)(UI_DIM_SCREEN_W - 2 * pad - 4 * UI_DIM_BUTTON_GAP);
    int16_t unit = (int16_t)(total_w * 10 / 55);            /* 5 slots, RUN = 1.5 */
    int16_t btn_h = (int16_t)(UI_DIM_FOOTER_H * 96 / 100);  /* >95 % of the row */
    int16_t btn_y = (int16_t)(fy + (UI_DIM_FOOTER_H - btn_h) / 2);
    int16_t x = pad;

    for (int i = 0; i < SCR_BTN_COUNT; i++) {
        int16_t w = (i == 1) ? (int16_t)(unit * 15 / 10) : unit;
        const char *l, *v; ui_button_variant_t var;
        footer_button_def(i, &l, &v, &var);
        ui_button_t b = {.rect = {x, btn_y, w, btn_h}, .variant = var,
                         .label = l, .value = v};
        ui_button_render(&b);
        s_btn_rect[i] = b.rect;
        x = (int16_t)(x + w + UI_DIM_BUTTON_GAP);
    }
}

/* Restore a rectangle from the static background cache (partial-redraw clear). */
static void blit_bg_region(prim_rect_t r)
{
    const prim_pixel_t *src = bg_cache + (int)r.y * SCR_MAIN_BG_CACHE_W + r.x;
    prim_blit(r, src, SCR_MAIN_BG_CACHE_W * (int16_t)sizeof(prim_pixel_t));
}

/* Redraw only the title row (clears it from the bg cache first). */
void screen_main_redraw_title(void)
{
    blit_bg_region((prim_rect_t){0, (int16_t)(SCR_MAIN_TITLE_Y - 18),
                                 UI_DIM_SCREEN_W, 28});
    render_body_title();
}

/* Simulovany cas HH:MM:SS (start 14:32:07 + uptime). Prekresli JEN oblast casu
 * a JEN kdyz se zmeni sekunda (zadne zbytecne prekreslovani -> zadny "px sum"). */
int screen_main_redraw_time(uint32_t ms_since_boot)
{
    static uint32_t last_sec = 0xFFFFFFFFu;
    uint32_t sec = 14u * 3600u + 32u * 60u + 7u + ms_since_boot / 1000u;
    if (sec == last_sec) return 0;   /* sekunda se nezmenila -> nekreslit, neflipovat */
    last_sec = sec;
    snprintf(s_time_buf, sizeof(s_time_buf), "%02lu:%02lu:%02lu",
             (unsigned long)((sec / 3600u) % 24u),
             (unsigned long)((sec / 60u) % 60u),
             (unsigned long)(sec % 60u));

    int16_t time_x = UI_DIM_SCREEN_W - UI_DIM_PADDING_X / 2;
    int16_t tw = prim_text_width(s_time_buf, &ui_font_mono_25);   /* monospace -> stabilni */
    /* Vycisti CELOU vysku glyfu (y 1..34), at nezbydou pixely nahore/dole.
     * Datum je na baseline 46 (top ~35) -> 34 se ho nedotkne. */
    blit_bg_region((prim_rect_t){(int16_t)(time_x - tw - 6), 1, (int16_t)(tw + 12), 33});
    prim_draw_text((prim_point_t){time_x, 23}, s_time_buf, &ui_font_mono_25,
                   UI_COLOR_INK, PRIM_ALIGN_RIGHT);
    return 1;   /* prekresleno -> flip */
}

/* LEAN prekresleni signal bargrafu (pro animaci ~30x/s): NEkresli chrome, pozadi
 * ani label (jsou staticke z render_main) -> levne. Smaze jen value text (vpravo)
 * a necha ui_bargraph track-fill prekreslit segmenty. Vrati 1 pokud kreslil. */
int screen_main_redraw_signal(int16_t pct)
{
    if (s_signal_rect.w == 0) return 0;
    s_signal_pct = pct;
    ui_card_t card = {.rect = s_signal_rect};
    prim_rect_t inner = ui_card_inner_rect(&card);
    char val[16];
    snprintf(val, sizeof(val), "%d dBm", signal_dbm(pct));
    /* smaz jen value text vpravo (label vlevo se netkneme -> zustava) */
    prim_fill_rect((prim_rect_t){(int16_t)(inner.x + inner.w - 96), (int16_t)(inner.y - 2),
                                 98, 20}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    ui_bargraph_t bar = {.rect = inner, .value_pct = pct, .color = UI_COLOR_OK,
                         .label = NULL, .value_text = val};   /* label=NULL -> nekresli */
    ui_bargraph_render(&bar);
    return 1;
}

/* Simulace kmitoctu: dithuje posledni 2 desetinne cislice (sigma+floor), integer
 * cast staticka. LEVNE — prekresli jen ditherovany ocas (od SCR_FREQ_TAIL_FROM),
 * ne cele velke cislo (mono_75 je drahy). Obnova pozadi jen pod ocasem. Vrati 1. */
int screen_main_redraw_freq(void)
{
    if (!s_num_ready) return 0;
    static uint32_t seed = 0x9E3779B9u;
    seed = seed * 1664525u + 1013904223u;
    s_num_buf[SCR_FREQ_TAIL_FROM][0]     = (char)('0' + (int)((seed >> 16) % 10u)); /* sigma */
    seed = seed * 1664525u + 1013904223u;
    s_num_buf[SCR_FREQ_TAIL_FROM + 1][0] = (char)('0' + (int)((seed >> 16) % 10u)); /* floor */
    blit_bg_region(s_num_tail_rect);
    ui_big_number_render_tail(&s_num, SCR_FREQ_TAIL_FROM);
    return 1;
}

/* Redraw only one footer button (clears just its rect from the bg cache). */
void screen_main_redraw_button(int idx)
{
    if (idx < 0 || idx >= SCR_BTN_COUNT) return;
    prim_rect_t r = s_btn_rect[idx];
    blit_bg_region(r);
    const char *l, *v; ui_button_variant_t var;
    footer_button_def(idx, &l, &v, &var);
    ui_button_t b = {.rect = r, .variant = var, .label = l, .value = v};
    ui_button_render(&b);
}

void screen_main_render(void)
{
    if (!cache_initialized) screen_main_init();
    prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
              bg_cache, UI_DIM_SCREEN_W * sizeof(prim_pixel_t));
    render_header();
    render_body_title();
    render_body_number();
    render_body_grid();
    render_footer();
}
