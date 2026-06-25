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
#include <math.h>    /* sqrtf/log10f/fabsf/powf/ceilf/floorf — GPSDO statistika (cold path, 1/s) */

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
    st = {0, 1, 1, true};    /* FREQUENCY, CH B, 1 s, RUNNING po bootu (tlacitko "STOP") */

const prim_pixel_t *screen_main_bg(void) { return bg_cache; }

/* RUN/STOP: ridi, zda bezi simulace mereni (kmitocet, bargraf, statistika). */
bool screen_main_is_running(void) { return st.running; }

/* Hlavickove pilulky — rect zachyceny pri render_header; tap -> okno. */
static prim_rect_t s_gnss_pill_rect = {0, 0, 0, 0};  /* GNSS lock -> GPS okno */
static prim_rect_t s_sys_pill_rect  = {0, 0, 0, 0};  /* SYS ready -> System Health */

static bool pt_in(int16_t x, int16_t y, prim_rect_t r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

bool screen_main_hit_gnss(int16_t x, int16_t y)
{
    return s_gnss_pill_rect.w != 0 && pt_in(x, y, s_gnss_pill_rect);
}

bool screen_main_hit_sys(int16_t x, int16_t y)
{
    return s_sys_pill_rect.w != 0 && pt_in(x, y, s_sys_pill_rect);
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
    ui_pill_render(&p);
    s_gnss_pill_rect = (prim_rect_t){p.x, p.y, p.computed_width, UI_DIM_PILL_H};  /* tap -> GPS okno */
    x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_OK, .value = SCR_S_SYS_READY};
    ui_pill_render(&p);
    s_sys_pill_rect = (prim_rect_t){p.x, p.y, p.computed_width, UI_DIM_PILL_H};  /* tap -> System Health */
    x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

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

/* Velke cislo kmitoctu je SIMULOVANE: hodnota je realisticky kmitocet (~10 MHz),
 * ktery se meni SPOJITE (mean-revert random walk) 20x/s -> nizke cislice zivot,
 * vyssi stabilni. Pocet mist je PEVNY (zero-pad) podle layoutu segmentu.
 * Prekresleni je PER-SEGMENT DIRTY (screen_main_redraw_freq): prekresli jen
 * skupiny cislic, ktere se zmenily (resp. ocas od nich) -> stabilni cela cast se
 * neprekresluje. Drzime zive + predchozi cislice (shadow) + deskriptor. */
static ui_digit_segment_t s_num_seg[8];
static char               s_num_buf[8][8];    /* aktualni cislice (zive) */
static char               s_num_prev[8][8];   /* cislice z minuleho snimku (per-segment dirty) */
static ui_big_number_t    s_num;
static int                s_num_ready = 0;
/* Cachovana geometrie cisla (monospace -> konstantni; spocte se v num_build,
 * redraw_freq uz neprochazi prim_text_width kazdy snimek). */
static int16_t            s_num_w, s_num_left, s_num_top;
static int16_t            s_seg_x[8];   /* x-pozice zacatku kazde skupiny cislic */

/* Simulacni stav kmitoctu (integer matematika, bez float). N = vsechny cislice
 * jako jedno cele cislo, desetinna carka je az v zobrazeni (dana separatory). */
static uint64_t s_freq_n      = 0;   /* aktualni 15(=total)-mistne cislo */
static uint64_t s_freq_center = 0;   /* stred (10 MHz v jednotkach LSB) */
static int      s_freq_total  = 0;   /* celkovy pocet cislic (= sirka, pevna) */

static void freq_fill_segments(void);   /* fwd (num_build naplni pocatecni hodnotu) */

static uint64_t pow10_u64(int e)
{
    uint64_t p = 1;
    while (e-- > 0) p *= 10u;
    return p;
}

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
    /* Cache geometrie (jednou): sirka, levy okraj, top a x-pozice vsech skupin. */
    s_num_w    = ui_big_number_width(&s_num);
    s_num_left = (int16_t)(UI_DIM_SCREEN_W / 2 - s_num_w / 2);
    s_num_top  = (int16_t)(SCR_MAIN_NUMBER_Y_BASELINE - 72);
    for (int i = 0; i < n; i++) s_seg_x[i] = ui_big_number_seg_x(&s_num, (int16_t)i);

    /* Spocti layout cislic z delek segmentu + pozice desetinne carky (sep ',').
     * Stred = 10 MHz zarovnane na celou cast (zbytek = desetinna mista). */
    s_freq_total = 0;
    int int_digits = 0;
    int comma_seen = 0;
    for (int i = 0; i < n; i++) {
        int L = (int)strlen(SCR_MAIN_DIGITS[i].text);
        s_freq_total += L;
        if (!comma_seen) {
            int_digits += L;
            if (SCR_MAIN_SEPS && (size_t)i < strlen(SCR_MAIN_SEPS)
                && SCR_MAIN_SEPS[i] == ',') comma_seen = 1;
        }
    }
    int frac_digits = s_freq_total - int_digits;
    s_freq_center = 10000000ull * pow10_u64(frac_digits);   /* 10 MHz */
    s_freq_n      = s_freq_center;
    freq_fill_segments();                            /* pocatecni 10 MHz do segmentu */
    for (int i = 0; i < n; i++) strcpy(s_num_prev[i], s_num_buf[i]);   /* shadow = init */
    s_num_ready   = 1;
}

/* Spojity krok kmitoctu: mean-revert random walk kolem stredu (10 MHz).
 * Krok ~±0,05 Hz (LSB = 10^-frac Hz), navrat /32 -> hodnota se pohybuje v pasmu
 * ~±0,3 Hz: cele cislo (10.000.000) stabilni, desetinna mista zivot. */
static void freq_step(void)
{
    static uint32_t rng = 0xDEADBEEFu;
    rng = rng * 1664525u + 1013904223u;
    int32_t step = (int32_t)((rng >> 12) & 0xFFFFF) - 0x80000;  /* ±524288 */
    int64_t off  = (int64_t)s_freq_n - (int64_t)s_freq_center;
    off += step - (off / 32);                                   /* random walk + decay (/32) */
    int64_t v = (int64_t)s_freq_center + off;
    if (v < 0) v = 0;
    s_freq_n = (uint64_t)v;
}

/* Rozlozi s_freq_n do segmentu (MSB first, zero-pad na pevnou sirku). */
static void freq_fill_segments(void)
{
    char d[20];
    uint64_t v = s_freq_n;
    for (int i = s_freq_total - 1; i >= 0; i--) { d[i] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    int p = 0, n = s_num.segment_count;
    for (int s = 0; s < n; s++) {
        int L = (int)strlen(SCR_MAIN_DIGITS[s].text);
        for (int k = 0; k < L; k++) s_num_buf[s][k] = d[p++];
        s_num_buf[s][L] = '\0';
    }
}

/* Big number rendered directly over the gradient background. HW (DMA2D) glyph
 * blend zapnut JEN pro tohle velke cislo (mereny kmitocet) — ostatni text jede CPU. */
static void render_body_number(void)
{
    if (!s_num_ready) num_build();   /* jednou; jitterovany stav pak prezije full render */
    prim_set_glyph_accel(1);
    ui_big_number_render(&s_num);
    prim_set_glyph_accel(0);
}

/* ── GPSDO statistika ze SIMULOVANEHO kmitoctu ──────────────────────────────
 * Frakcni odchylka y=(f-f0)/f0 (f0=10 MHz). Poctiva magnituda (~1e-8 dle kolisani
 * simulace). Vzorkuje se 1x/s (jen pri RUN, τ0=1s) do (a) plocheho ring bufferu
 * (kratkodobe: trend 60s, offset, drift, σy@1s) a (b) decimacni pyramidy
 * (dlouhodoby Allan, tau 1..100000+ s, viz adev_feed). Prekresleni 1x/s. Float OK
 * (cold path; mimo no-float pravidlo pro protokol kmitoctu). */
/* Plochy ring = jen kratkodobe (trend 60s, offset, drift, σy@1s). DLOUHODOBY Allan
 * (tau az 100000 s / 100+ dni) resi decimacni pyramida nize. Vzorkuje se 1/s. */
#define STAT_N    120               /* 1/s -> 120 s (trend 60s + drift baseline) */
#define TREND_WIN 60                /* trend sparkline = posledni okno 60 s (1/s) */
static float s_y[STAT_N];
static int   s_y_head = 0, s_y_count = 0;
static void adev_feed(float v);     /* fwd — decimacni pyramida (dlouhodoby Allan) */

static void stats_sample(void)
{
    /* off_n = odchylka v LSB (LSB=1e-7 Hz), f0=1e7 Hz -> y = off_n*1e-14 */
    int64_t off_n = (int64_t)s_freq_n - (int64_t)s_freq_center;
    float y = (float)off_n * 1e-14f;
    s_y[s_y_head] = y;                    /* plochy ring (kratkodobe) */
    s_y_head = (s_y_head + 1) % STAT_N;
    if (s_y_count < STAT_N) s_y_count++;
    adev_feed(y);                         /* decimacni pyramida (dlouhodoby Allan) */
}

static float stat_at(int age)   /* age 0 = nejnovejsi */
{
    int idx = (s_y_head - 1 - age + 2 * STAT_N) % STAT_N;
    return s_y[idx];
}

static float stats_mean(int n)
{
    if (n > s_y_count) n = s_y_count;
    if (n <= 0) return 0.0f;
    float s = 0; for (int i = 0; i < n; i++) s += stat_at(i);
    return s / (float)n;
}

static float stats_pp(int n)
{
    if (n > s_y_count) n = s_y_count;
    if (n <= 0) return 0.0f;
    float mn = stat_at(0), mx = mn;
    for (int i = 1; i < n; i++) { float v = stat_at(i); if (v < mn) mn = v; if (v > mx) mx = v; }
    return mx - mn;
}

/* Non-overlapping ADEV plocheho ringu pro tau = m vzorku (tau0=1 s, 1/s). Pouziva
 * se pro σy@1s (m=1). Dlouhodoby Allan dela pyramida (adev_stage). >=2 bloky. */
static float stats_adev(int m)
{
    int blocks = s_y_count / m;
    if (blocks < 2) return 0.0f;
    float prev = 0; int have = 0; double acc = 0; int nd = 0;
    for (int b = 0; b < blocks; b++) {
        float bs = 0;
        for (int j = 0; j < m; j++) bs += stat_at(b * m + j);
        bs /= (float)m;
        if (have) { float d = bs - prev; acc += (double)d * d; nd++; }
        prev = bs; have = 1;
    }
    return (nd > 0) ? sqrtf((float)(0.5 * acc / (double)nd)) : 0.0f;
}

/* Drift (df/dt) = linearni trend frakcni odchylky pres okno [1/s]. Metoda dvou
 * pulek: (prumer novejsi pulky - prumer starsi pulky) / odstup centroidu. */
static float stats_drift(void)
{
    int h = s_y_count / 2;
    if (h < 1) return 0.0f;
    float nm = 0, om = 0;
    for (int i = 0; i < h; i++) { nm += stat_at(i); om += stat_at(s_y_count - 1 - i); }
    nm /= (float)h; om /= (float)h;
    float dt = (float)h;                  /* odstup centroidu pulek [s] (vzorky × 1 s) */
    return (dt > 0.0f) ? (nm - om) / dt : 0.0f;
}

/* ── Decimacni pyramida pro DLOUHODOBY Allan (tau 1..100000 s, ohranicena pamet) ──
 * Vzorek y (1/s) jde do stage 0; po 10 vzorcich se jejich prumer posune do dalsi
 * stage (tau ×10). Stage s drzi prumery na tau=10^s s. Pokryje 100+ dni v ~640 B
 * (plochy buffer by chtel desitky MB). */
#define ADEV_STAGES 6                 /* tau = 1, 10, 100, 1k, 10k, 100k s */
#define ADEV_RING   24                /* prumeru na stage (na ADEV vypocet) */
typedef struct { float ring[ADEV_RING]; int16_t head, count; float acc; int16_t acc_n; } adev_stage_t;
static adev_stage_t s_adev[ADEV_STAGES];

static void adev_feed(float v)
{
    for (int s = 0; s < ADEV_STAGES; s++) {
        adev_stage_t *st = &s_adev[s];
        st->ring[st->head] = v;
        st->head = (int16_t)((st->head + 1) % ADEV_RING);
        if (st->count < ADEV_RING) st->count++;
        st->acc += v;
        if (++st->acc_n < 10) return;             /* dalsi stage jeste nema co krmit */
        v = st->acc / 10.0f; st->acc = 0; st->acc_n = 0;   /* dekadovy prumer -> dal */
    }
}

static float adev_rat(const adev_stage_t *st, int i)       /* i-ty nejstarsi prvek */
{
    int idx = (st->head - st->count + i + 2 * ADEV_RING) % ADEV_RING;
    return st->ring[idx];
}

/* Non-overlapping ADEV stage s pri decimaci m (tau = m*10^s s). */
static float adev_stage(int s, int m)
{
    const adev_stage_t *st = &s_adev[s];
    int blocks = st->count / m;
    if (blocks < 2) return 0.0f;
    float prev = 0; int have = 0; double acc = 0; int nd = 0;
    for (int b = 0; b < blocks; b++) {
        float bs = 0;
        for (int j = 0; j < m; j++) bs += adev_rat(st, b * m + j);
        bs /= (float)m;
        if (have) { float d = bs - prev; acc += (double)d * d; nd++; }
        prev = bs; have = 1;
    }
    return (nd > 0) ? sqrtf((float)(0.5 * acc / (double)nd)) : 0.0f;
}

/* Format frakcni hodnoty jako "<sign>M,m×10⁻E" s HORNIM INDEXEM exponentu
 * (mono_14/16 maji plny charset vc. ⁰..⁹⁻⁺). with_sign: + pro kladne. */
static void fmt_frac(char *buf, int len, float v, int with_sign)
{
    static const char *const SUP[10] = {"⁰","¹","²","³","⁴","⁵","⁶","⁷","⁸","⁹"};
    float a = fabsf(v);
    if (a < 1e-15f) { snprintf(buf, len, "0"); return; }
    int e = 0;
    while (a < 1.0f && e < 30) { a *= 10.0f; e++; }   /* hodnoty <1 -> e>0 (10⁻e) */
    while (a >= 10.0f) { a /= 10.0f; e--; }
    int M = (int)a;
    int m = (int)((a - (float)M) * 10.0f + 0.5f);
    if (m >= 10) { m = 0; M++; }
    int ae = (e < 0) ? -e : e;
    char es[12]; es[0] = '\0';
    if (ae >= 10) strcat(es, SUP[(ae / 10) % 10]);
    strcat(es, SUP[ae % 10]);
    const char *sign    = (v < 0) ? "-" : (with_sign ? "+" : "");
    const char *expsign = (e < 0) ? "⁺" : "⁻";
    snprintf(buf, len, "%s%d,%d×10%s%s", sign, M, m, expsign, es);
}

/* Rect karet pro zive prekresleni (zachyceno pri full renderu). */
static prim_rect_t s_allan_rect = {0,0,0,0};
static prim_rect_t s_trend_rect = {0,0,0,0};
static prim_rect_t s_small_rect = {0,0,0,0};

static void render_card_allan(prim_rect_t rect)
{
    s_allan_rect = rect;                          /* pro zive prekresleni */
    char hdr[40];
    char sig[24]; fmt_frac(sig, sizeof(sig), stats_adev(1), 0);  /* σy@1s (τ=1s, vzorky 1/s) */
    snprintf(hdr, sizeof(hdr), "@1s %s", sig);
    ui_card_t card = {.rect = rect, .header_label = "Allan σy(τ)",
                      .header_right = hdr,
                      .header_right_accent = UI_COLOR_ACC};
    ui_card_render_chrome(&card);
    prim_rect_t ci = ui_card_inner_rect(&card);
    /* Levy okraj (36) na Y popisky, dolni pruh (22) na X popisky τ, maly pravy okraj. */
    prim_rect_t inner = {(int16_t)(ci.x + 36), ci.y,
                         (int16_t)(ci.w - 36 - 8), (int16_t)(ci.h - 22)};

    /* Vlastni log-log osa: Y pevna (1e-10..1e-6), X = [tau_min..tau_max] v log10 —
     * DYNAMICKA: krivka zacina u tau_min (1 s) a roztahuje se vpravo s nejdelsim
     * dostupnym tau (az 100000+ s pri dlouhem behu). */
    const int Y_MIN = -10, Y_DEC = 4;

    /* Y mrizka + dekadove popisky. */
    for (int j = 0; j <= Y_DEC; j++) {
        int16_t y = (int16_t)(inner.y + (int32_t)j * inner.h / Y_DEC);
        prim_draw_line((prim_point_t){inner.x, y},
                       (prim_point_t){(int16_t)(inner.x + inner.w), y}, 1, UI_COLOR_LINE);
        prim_draw_text((prim_point_t){(int16_t)(inner.x - 6), (int16_t)(y + 4)},
                       SCR_ALLAN_Y_TICKS[j], &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);
    }

    /* ADEV body z decimacni pyramidy: per stage tau = {1,2,5}×10^s s (log spacing
     * 1,2,5,10,20,50,...). Delsi tau nabihaji jak roste historie -> osa se prodluzuje
     * az k 100000+ s (100 dni), pamet ohranicena. */
    static const int SM[] = {1, 2, 5};
    float taus[20], adevs[20]; int np = 0;
    for (int s = 0; s < ADEV_STAGES; s++) {
        float dec = powf(10.0f, (float)s);          /* 1,10,100,1k,10k,100k */
        for (int mi = 0; mi < 3; mi++) {
            float a = adev_stage(s, SM[mi]);
            if (a <= 0.0f) continue;
            taus[np] = dec * (float)SM[mi]; adevs[np] = a; np++;
        }
    }
    if (np < 2) {                                   /* jeste neni dost vzorku -> hlaska */
        prim_draw_text((prim_point_t){(int16_t)(inner.x + inner.w / 2),
                                      (int16_t)(inner.y + inner.h / 2)},
                       "Waiting for data...", &ui_font_sans_14,
                       UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
        return;
    }

    float lmin = log10f(taus[0]);                   /* nejkratsi tau = levy okraj */
    float lmax = log10f(taus[np - 1]);              /* nejdelsi tau = pravy okraj */
    float xspan = lmax - lmin;
    if (xspan < 1e-6f) xspan = 1.0f;

    /* X mrizka + popisky v MOCNINACH 10 (log scale: 1,10,100,1k,10k,100k s) —
     * jen dekady padajici do dynamickeho rozsahu [tau_min..tau_max]. */
    for (int e = (int)ceilf(lmin); e <= (int)floorf(lmax); e++) {
        float fx = ((float)e - lmin) / xspan;
        int16_t x = (int16_t)(inner.x + fx * inner.w);
        prim_draw_line((prim_point_t){x, inner.y},
                       (prim_point_t){x, (int16_t)(inner.y + inner.h)}, 1, UI_COLOR_LINE);
        char dl[8];                                 /* tau_min >= 1 s -> e >= 0 (10^e) */
        long v = 1;
        for (int j = 0; j < e; j++) v *= 10;
        snprintf(dl, sizeof(dl), "%ld", v);
        prim_draw_text((prim_point_t){x, (int16_t)(inner.y + inner.h + 14)},
                       dl, &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
    }

    prim_point_t pts[20];
    for (int i = 0; i < np; i++) {
        float fx = (log10f(taus[i]) - lmin) / xspan;            /* 0..1 pres celou sirku */
        float ly = (log10f(adevs[i]) - (float)Y_MIN) / (float)Y_DEC;
        if (ly < 0.0f) ly = 0.0f;
        if (ly > 1.0f) ly = 1.0f;
        pts[i].x = (int16_t)(inner.x + fx * inner.w);
        pts[i].y = (int16_t)(inner.y + (1.0f - ly) * inner.h);
    }
    for (int i = 1; i < np; i++)
        prim_draw_line(pts[i - 1], pts[i], 2, UI_COLOR_ACC);
    for (int i = 0; i < np; i++)                     /* marker v kazdem tau bode */
        prim_fill_circle(pts[i], 3, UI_COLOR_ACC);
    if (SCR_ALLAN_X_LABEL)
        prim_draw_text((prim_point_t){(int16_t)(inner.x + inner.w),
                                      (int16_t)(inner.y + inner.h + 28)},
                       SCR_ALLAN_X_LABEL, &ui_font_sans_14, UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
}

/* Jedna mala karta: header label + hodnota (mono_16). Hodnota posunuta vys (+11). */
static void draw_stat_card(prim_rect_t r, const char *label, const char *val, prim_color_t c)
{
    ui_card_t card = {.rect = r, .header_label = label};
    ui_card_render_chrome(&card);
    prim_rect_t in = ui_card_inner_rect(&card);
    prim_draw_text((prim_point_t){in.x, (int16_t)(in.y + 11)}, val,
                   &ui_font_mono_16, c, PRIM_ALIGN_LEFT);
}

/* TRI uzke karty ze statistiky: Offset (klouzavy prumer y), σy@1s (ADEV tau=1s),
 * Drift (df/dt — rychlost ujizdeni kmitoctu). */
static void draw_offset_sigma(prim_rect_t rect)
{
    s_small_rect = rect;                          /* pro zive prekresleni */
    int16_t gap = SCR_MAIN_CARD_SECTION_GAP;
    int16_t w   = (int16_t)((rect.w - 2 * gap) / 3);
    char off[24]; fmt_frac(off, sizeof(off), stats_mean(8),   1);
    char s1[24];  fmt_frac(s1,  sizeof(s1),  stats_adev(1),   0);   /* σy@1s (τ=1s, 1/s) */
    char dr[24];  fmt_frac(dr,  sizeof(dr),  stats_drift(),   1);   /* df/dt [1/s] */
    draw_stat_card((prim_rect_t){rect.x, rect.y, w, rect.h}, SCR_S_OFFSET_L, off, UI_COLOR_OK);
    draw_stat_card((prim_rect_t){(int16_t)(rect.x + w + gap), rect.y, w, rect.h},
                   "σy 1s", s1, UI_COLOR_VIOLET);
    draw_stat_card((prim_rect_t){(int16_t)(rect.x + 2 * (w + gap)), rect.y, w, rect.h},
                   "Drift/s", dr, UI_COLOR_ACC);
}

#define SPARK_N 21
static int16_t s_spark[SPARK_N];

static void render_card_trend(prim_rect_t rect)
{
    s_trend_rect = rect;                          /* pro zive prekresleni */

    /* Sparkline: SPARK_N bodu pres POSLEDNICH 60 s (TREND_WIN), vlevo=nejstarsi.
     * (Buffer je delsi kvuli Allan, ale trend zustava 60s okno.) PEVNE meritko ±FS
     * -> stred 128 (neposkakuje pri sliding window). */
    const float FS = 4e-8f;                       /* full-scale frakcni odchylky */
    int win = (s_y_count < TREND_WIN) ? s_y_count : TREND_WIN;   /* posledni 60s okno */
    int n = (win < SPARK_N) ? win : SPARK_N;
    if (n < 2) n = 2;
    float sum = 0;
    for (int k = 0; k < n; k++) {
        int age = (n - 1 - k) * ((win > 1 ? win - 1 : 1)) / (n - 1);
        float v = stat_at(age);
        sum += v;
        float u = (v / FS + 1.0f) * 0.5f;         /* -FS..+FS -> 0..1 */
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
        s_spark[k] = (int16_t)(40.0f + u * 175.0f);
    }
    float mean = sum / (float)n, sd = stats_adev(1);   /* σy@1s pro sigma band */
    float ulo = ((mean - sd) / FS + 1.0f) * 0.5f;
    float uhi = ((mean + sd) / FS + 1.0f) * 0.5f;
    if (ulo < 0.0f) ulo = 0.0f;
    if (uhi > 1.0f) uhi = 1.0f;
    int16_t sig_lo = (int16_t)(40.0f + ulo * 175.0f);
    int16_t sig_hi = (int16_t)(40.0f + uhi * 175.0f);

    /* Header vpravo: p-p celeho 60s okna. */
    static char tr[48];
    char pp[24]; fmt_frac(pp, sizeof(pp), stats_pp(win), 0);
    snprintf(tr, sizeof(tr), "● %s p-p", pp);

    ui_card_t card = {.rect = rect, .header_label = SCR_S_TREND_L,
                      .header_right = tr, .header_right_accent = UI_COLOR_OK};
    ui_card_render_chrome(&card);
    prim_rect_t inner = ui_card_inner_rect(&card);
    ui_sparkline_t sp = {.inner = inner, .y_values = s_spark,
        .count = (int16_t)n, .show_sigma_band = true,
        .sigma_min = sig_lo, .sigma_max = sig_hi,
        .show_endpoint_marker = true, .fill_below = false,
        .stroke_color = UI_COLOR_ACC};
    ui_sparkline_render(&sp);

    /* Tenka regresni (trendova) cara pres syrove vzorky — ukaze smer/drift, aniz by
     * skryla sum. Least-squares fit s_spark[k] vs k; mapovani 1:1 se sparkline. */
    float si = 0, sv = 0, sii = 0, siv = 0;
    for (int k = 0; k < n; k++) {
        si += (float)k; sv += (float)s_spark[k];
        sii += (float)k * (float)k; siv += (float)k * (float)s_spark[k];
    }
    float denom = (float)n * sii - si * si;
    if (denom > 1e-3f || denom < -1e-3f) {
        float slope = ((float)n * siv - si * sv) / denom;
        float inter = (sv - slope * si) / (float)n;
        float v0 = inter, v1 = inter + slope * (float)(n - 1);
        if (v0 < 0) v0 = 0;
        if (v0 > 255) v0 = 255;
        if (v1 < 0) v1 = 0;
        if (v1 > 255) v1 = 255;
        int16_t y0 = (int16_t)(inner.y + inner.h - 1 - (int)(v0 * (inner.h - 1) / 255.0f));
        int16_t y1 = (int16_t)(inner.y + inner.h - 1 - (int)(v1 * (inner.h - 1) / 255.0f));
        prim_draw_line((prim_point_t){inner.x, y0},
                       (prim_point_t){(int16_t)(inner.x + inner.w - 1), y1},
                       1, UI_COLOR_OK);
    }
}

/* Signal bargraf je SIMULOVANY (animovany ~10x/s, krok 1 dBm, zobrazeni s 1
 * desetinou; viz screen_main_redraw_signal). Drzime rect + hodnotu pro partial redraw. */
static prim_rect_t s_signal_rect = {0, 0, 0, 0};
static int16_t     s_signal_pct  = 0;   /* simulace ho hned prepise */

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
    draw_offset_sigma((prim_rect_t){rect.x, y, rect.w, small_h});
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
    case 1: *label = st.running ? SCR_S_BTN_RUN : "STOP";   /* invertovano: label = stav */
            *var = st.running ? UI_BUTTON_RUN : UI_BUTTON_ACTIVE; break;
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

/* Incremental prekresleni signal bargrafu (animace ~10x/s, dBm krok 1): chrome/
 * pozadi/label/stopa jsou staticke z render_main; prekresli jen segmenty zmenene
 * od minula + value text (dBm s 1 des. mistem). Misto ~20 fillu jen rozdil. Vrati 1. */
int screen_main_redraw_signal(int16_t pct)
{
    if (s_signal_rect.w == 0) return 0;
    int16_t old = s_signal_pct;       /* hodnota aktualne na obrazovce */
    s_signal_pct = pct;
    ui_card_t card = {.rect = s_signal_rect};
    prim_rect_t inner = ui_card_inner_rect(&card);
    int drew = 0;

    /* Value text (dBm s 1 desetinnym mistem) vpravo: smaz box + prekresli jen pri
     * zmene celeho dBm (= kazdy krok). Desetina = simulovany sum (0..9). */
    int dbm = signal_dbm(pct);
    static int last_dbm = 0x7FFFFFFF;
    if (dbm != last_dbm) {
        last_dbm = dbm;
        static uint32_t dseed = 1u;
        dseed = dseed * 1103515245u + 12345u;
        int frac = (int)((dseed >> 22) % 10u);
        char val[16];
        snprintf(val, sizeof(val), "%d,%d dBm", dbm, frac);
        /* sirsi clear box (122 px): zaporne "-60,5 dBm" je sirsi nez kladne -> jinak
         * zustane reziduum znamenka "-" vlevo pri prechodu do kladnych. */
        prim_fill_rect((prim_rect_t){(int16_t)(inner.x + inner.w - 120), (int16_t)(inner.y - 2),
                                     122, 20}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        ui_bargraph_value(&inner, val, UI_COLOR_OK);
        drew = 1;
    }

    /* Segmenty: jen rozdil old -> pct (rozsvit/zhasni). */
    if (ui_bargraph_update(&inner, old, pct) > 0) drew = 1;
    return drew;
}

/* Posune simulovany kmitocet o jeden spojity krok, prepise cislice a prekresli
 * cele cislo — ale PER-SEGMENT DIRTY: zmeny jsou v nizkych cislicich, takze staci
 * prekreslit OCAS od prvni zmenene skupiny doprava; stabilni vyssi cislice (cela
 * cast) se neprekresluji. Obraz je pixel-identicky s full redrawem, ale za zlomek
 * zateze (typicky 2-4 mono_75 glyfy misto 15). Volat ~20x/s. Vrati 1 pri zmene. */
int screen_main_redraw_freq(void)
{
    if (!s_num_ready) return 0;
    freq_step();
    freq_fill_segments();

    int from = -1;                                  /* prvni zmenena skupina cislic */
    for (int i = 0; i < s_num.segment_count; i++)
        if (strcmp(s_num_buf[i], s_num_prev[i]) != 0) { from = i; break; }
    if (from < 0) return 0;                          /* nic se nezmenilo -> neflipovat */

    int16_t x0 = s_seg_x[from];                      /* cachovana geometrie (num_build) */
    /* Obnov pozadi od x0 doprava (po pravy okraj cisla vc. jednotky) a prekresli
     * jen ocas. x0-2 zasahne max okraj predchoziho separatoru, jehoz inkoust je
     * uprostred advance -> bez rezidua. */
    /* vyska 88 (ne 92): konci nad horni hranou pravych karet (offset/σ) -> jejich
     * okraj uz neproblikava pri 20Hz prekreslovani kmitoctu. */
    blit_bg_region((prim_rect_t){ (int16_t)(x0 - 2), s_num_top,
                                  (int16_t)(s_num_left + s_num_w - x0 + 8), 88 });
    prim_set_glyph_accel(1);                 /* HW glyfy jen pro mereny kmitocet */
    ui_big_number_render_tail(&s_num, (int16_t)from);
    prim_set_glyph_accel(0);
    for (int i = from; i < s_num.segment_count; i++) strcpy(s_num_prev[i], s_num_buf[i]);
    return 1;
}

/* Navzorkuje aktualni frakcni odchylku do statistiky (plochy ring + pyramida, ~1x/s). */
void screen_main_stats_sample(void)
{
    stats_sample();
}

/* Zive prekresleni trend + offset/sigma (lehke; volat ~1x/s). Vrati 1. */
int screen_main_redraw_stats(void)
{
    if (s_trend_rect.w == 0) return 0;            /* jeste nebyl full render */
    blit_bg_region(s_trend_rect); render_card_trend(s_trend_rect);
    blit_bg_region(s_small_rect); draw_offset_sigma(s_small_rect);
    return 1;
}

/* Zive prekresleni Allan grafu (tezsi: grid + krivka; volat ~1x/s). Vrati 1. */
int screen_main_redraw_allan(void)
{
    if (s_allan_rect.w == 0) return 0;
    blit_bg_region(s_allan_rect); render_card_allan(s_allan_rect);
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
