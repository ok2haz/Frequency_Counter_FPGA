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
#include "sensor_stat.h"   /* g_sensors[] — agregace chyb do SYS pilulky */
#include <prim/prim.h>
#include "gps.h"     /* gps_get() — zive GNSS lock / pocet druzic / cas+datum v headeru */
#include <stdio.h>   /* snprintf pro cas/datum */
#include <string.h>  /* strncpy */
#include <math.h>    /* sqrtf/log10f/fabsf/powf/ceilf/floorf — GPSDO statistika (cold path, 1/s) */

/* RTC cas (defaultTask zapise pres rtc_app_tick) — hodiny v headeru z RTC, ne
 * GPS-direct: tikaji plynule i pri ztrate fixu (RTC bezi z LSE). */
extern volatile char    g_rtc_text[24];   /* "YYYY-MM-DD HH:MM:SS" */
extern volatile uint8_t g_sound_muted;    /* 1 = mute -> ikona v headeru */
/* Zdroje pro barvu SYS pilulky (agregace vsech chyb). */
extern volatile uint8_t  g_spi_ok;         /* FPGA link */
extern volatile uint8_t  g_freq_stale;     /* FPGA SIGNAL_LOST / mrtvy link */
extern volatile uint8_t  g_si5356_ok;      /* Si5356 status precten */
extern volatile uint8_t  g_si5356_status;  /* reg218: bit0 SYS_CAL, bit2 LOS_XTAL, bit3 LOS_CLKIN, bit4 PLL_LOL (AN565) */
extern volatile uint8_t  g_selftest_res;   /* 0=--- 1=PASS 2=FAIL */
extern volatile uint8_t  g_reset_bad;      /* 1 = posledni reset = watchdog/crash */
extern volatile uint8_t  g_cm4_absent;     /* 1 = CM4 (D2) nenabehl -> degradovane */
extern volatile uint8_t g_rtc_synced;     /* 1 = uz srovnano z GPS */
extern volatile char    g_rtc_text_local[24];  /* cas v lokalni zone (rtc_app_tick) */
extern volatile char    g_tz_label[8];         /* "UTC" / "UTC+2" (label zony k datu) */

/* Ulozene UI nastaveni (persist v RTC BKP): nacte se jednou v screen_main_init,
 * pakuje se pri zmene tlacitka; defaultTask ho zapise do BKP. bit0 mode / bit1
 * chan / bity2:3 gate / bit4 running. */
extern volatile uint8_t g_ui_cfg;
extern volatile uint8_t g_ui_cfg_dirty;

/* Vytahne cas "HH:MM:SS" a datum "YYYY-MM-DD" z g_rtc_text_local (cas v
 * ZVOLENE casove zone z Nastaveni; UTC kdyz je zona 0 — label zony k tomu dava
 * g_tz_label). Dokud nebyl RTC srovnan z GPS, vraci placeholdery
 * "--:--:--" / "no GPS". time8/date10 = char[16]. */
static void rtc_time_date(char *time8, char *date10)
{
    char rt[24];
    strncpy(rt, (const char *)g_rtc_text_local, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
    if (g_rtc_synced && strlen(rt) >= 19) {
        snprintf(time8,  16, "%.8s",  rt + 11);   /* "HH:MM:SS" */
        snprintf(date10, 16, "%.10s", rt);        /* "YYYY-MM-DD" */
    } else {
        snprintf(time8,  16, "--:--:--");
        snprintf(date10, 16, "no GPS");
    }
}

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

/* Agregace zdravi systemu do SYS pilulky: 0=OK(zelena) 1=warn(amber) 2=chyba(cervena).
 * Cerpa ze VSECH chybovych zdroju. AMBER = degradace (funguje), RED = kriticke. */
/* Si5356 reg 218 — bitova mapa OVERENA z AN565 (drivejsi bit2=LOS_CLKIN byla
 * chyba; bit2 je LOS_XTAL = krystal, ktery na desce NENI osazen -> trvale 1,
 * benigni, IGNORUJE se). Skutecnou ztratu 10 MHz na CLKIN hlasi bit3 — a POZOR,
 * PLL_LOL se pri fyzicke ztrate vstupu NEasertuje (AN565: LOL = rozdil >5000 ppm
 * na PFD, ne odpojeny vstup) -> LOS_CLKIN je HLAVNI indikator ztraty reference. */
#define SI_SYS_CAL   0x01u
#define SI_LOS_XTAL  0x04u   /* bit2: bez krystalu trvale 1 — nehodnotit */
#define SI_LOS_CLKIN 0x08u   /* bit3: ztrata 10 MHz na CLKIN (pin 4) */
#define SI_PLL_LOL   0x10u
static int compute_sys_level(void)
{
    int lvl = 0;
    /* AMBER: degradovane, ale funguje */
    if (g_freq_stale || !g_spi_ok) lvl = 1;               /* FPGA SIGNAL_LOST / mrtvy link */
    if (!g_si5356_ok || (g_si5356_status & SI_SYS_CAL)) lvl = 1;  /* ref necteno / kalibruje */
    if (g_reset_bad) lvl = 1;                             /* watchdog/crash reset (zotaveno) */
    if (g_cm4_absent) lvl = 1;                            /* CM4 (D2) nenabehl -> degradovane */
    for (int i = 0; i < SENS_COUNT; i++)
        if (i != (int)SENS_T4A && g_sensors[i].err_streak > 0) { lvl = 1; break; }  /* 0x4A neosazen */
    /* RED: kriticke (prebiji). LOS_CLKIN (bit3) = ztrata 10 MHz reference — LOL se
     * pri fyzicke ztrate vstupu NEasertuje (viz komentar u SI_* vyse), takze LOS je
     * tady nutny. SI_LOS_XTAL (bit2) se zamerne NEhodnoti (bez krystalu trvale 1). */
    if (g_si5356_ok && (g_si5356_status & (SI_LOS_CLKIN | SI_PLL_LOL))) lvl = 2;
    if (g_selftest_res == 2) lvl = 2;                     /* selftest FAIL */
    return lvl;
}

static int s_sys_level = -1;   /* posledni vykreslena uroven (pro poll zmeny) */

/* Vrati 1 pokud se uroven SYS zdravi zmenila od posledniho render_header -> volajici
 * (UiTask na hl. obrazovce) pak zavola screen_main_redraw_header. */
int screen_main_sys_poll(void)
{
    return compute_sys_level() != s_sys_level;
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
    default: return;                                      /* 4 = MENU: nic k ulozeni */
    }
    /* Zapamatuj nastaveni -> defaultTask ho persistne do BKP (prezije warm reset). */
    g_ui_cfg = (uint8_t)((st.mode & 1) | ((st.chan & 1) << 1)
                         | ((st.gate & 3) << 2) | ((st.running ? 1 : 0) << 4));
    g_ui_cfg_dirty = 1;
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
    /* Jednou po bootu: aplikuj ulozene nastaveni (g_ui_cfg nacetl MX_RTC_Init z BKP
     * pred schedulerem). Guard je nezavisly na cache -> screen_main_invalidate
     * (reset cache) uz nastaveni znovu neprepise. */
    static bool s_cfg_loaded = false;
    if (!s_cfg_loaded) {
        s_cfg_loaded = true;
        uint8_t c   = g_ui_cfg;
        st.mode     = (int8_t)( c        & 1);
        st.chan     = (int8_t)((c >> 1)  & 1);
        st.gate     = (int8_t)((c >> 2)  & 3);
        st.running  = ((c >> 4) & 1) != 0;
    }
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

    /* Zive GPS: GNSS lock pill + pocet druzic (SAT pill) + datum z GPS. */
    gps_data_t g;
    gps_get(&g);
    char sat_v[8], date_v[16], hdop_v[8];
    const char *gnss_s; ui_pill_variant_t gnss_var;
    if      (g.valid && g.fix_mode == 3) { gnss_s = "GNSS 3D";  gnss_var = UI_PILL_OK; }
    else if (g.fix_quality > 0)          { gnss_s = "GNSS FIX"; gnss_var = UI_PILL_OK; }
    else if (g.sats_in_view > 0)         { gnss_s = "ACQUIRE";  gnss_var = UI_PILL_WARN; }
    else                                 { gnss_s = "NO GNSS";  gnss_var = UI_PILL_BAD; }
    snprintf(sat_v, sizeof sat_v, "%u", g.num_sat);
    /* HDOP z GPS (GGA/GSA): 1 des. misto, ceska carka. Bez fixu "--".
     * Cap 99,9 (vyssi HDOP = nesmyslny fix; zaroven omezi rozsah pro snprintf). */
    if (g.fix_quality > 0 && g.hdop > 0.0f) {
        int h10 = (int)(g.hdop * 10.0f + 0.5f);
        if (h10 < 0) h10 = 0; else if (h10 > 999) h10 = 999;   /* bound [0,999] -> snprintf bezpecne */
        snprintf(hdop_v, sizeof hdop_v, "%d,%d", h10 / 10, h10 % 10);
    } else {
        snprintf(hdop_v, sizeof hdop_v, "--");
    }
    /* GNSS/SAT pilulky zustavaji z GPS (odrazi fix); datum bere RTC (tika i bez fixu). */
    { char tdummy[16]; rtc_time_date(tdummy, date_v); }   /* header chce jen datum */

    p = (ui_pill_t){.x = x, .y = y, .variant = gnss_var,
                    .value = gnss_s, .has_led = true};
    ui_pill_render(&p);
    s_gnss_pill_rect = (prim_rect_t){p.x, p.y, p.computed_width, UI_DIM_PILL_H};  /* tap -> GPS okno */
    x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    /* SYS pilulka barevne dle agregovaneho zdravi (zelena/amber/cervena). */
    s_sys_level = compute_sys_level();
    ui_pill_variant_t sysv = (s_sys_level == 0) ? UI_PILL_OK
                           : (s_sys_level == 1) ? UI_PILL_WARN : UI_PILL_BAD;
    const char *sysl = (s_sys_level == 0) ? "SYS OK"
                     : (s_sys_level == 1) ? "SYS !" : "SYS ERR";
    p = (ui_pill_t){.x = x, .y = y, .variant = sysv, .value = sysl};
    ui_pill_render(&p);
    s_sys_pill_rect = (prim_rect_t){p.x, p.y, p.computed_width, UI_DIM_PILL_H};  /* tap -> System Health */
    x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_NORMAL, .value = sat_v,
                    .icon_render = ui_icon_sat_dish, .icon_size = 22,
                    .icon_color = UI_COLOR_OK_SOFT};
    ui_pill_render(&p); x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_NORMAL,
                    .label = SCR_S_HDOP_L, .value = hdop_v};   /* reálné HDOP z GPS */
    ui_pill_render(&p); x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    p = (ui_pill_t){.x = x, .y = y, .variant = UI_PILL_NORMAL,
                    .label = SCR_S_CAL_L, .value = SCR_S_CAL_V};
    ui_pill_render(&p); x = (int16_t)(x + p.computed_width + UI_DIM_PILL_GAP);

    /* HOLD pilulka: AMBER pri holdoveru (fix ztracen pote, co uz nekdy byl) —
     * nahrazuje drivejsi zvlastni "H" u casu (kolidovalo s UTC/pilulkou). */
    int hold = (!g.valid && g.fixes > 0);
    p = (ui_pill_t){.x = x, .y = y, .variant = hold ? UI_PILL_WARN : UI_PILL_NORMAL,
                    .label = SCR_S_HOLD_L, .value = SCR_S_HOLD_V};
    ui_pill_render(&p);

    int16_t time_x = UI_DIM_SCREEN_W - UI_DIM_PADDING_X / 2;   /* half the margin */
    prim_draw_text((prim_point_t){time_x, 23}, s_time_buf, &ui_font_mono_25,
                   UI_COLOR_INK, PRIM_ALIGN_RIGHT);
    /* Label zony ("UTC"/"UTC+2") na radek DATA (vpravo dole) — mimo pilulky. */
    char dutc[24]; snprintf(dutc, sizeof dutc, "%s %s", date_v, (const char *)g_tz_label);
    prim_draw_text((prim_point_t){time_x, 46}, dutc, &ui_font_sans_14,
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
static uint8_t            s_seg_len[8]; /* delka textu kazde skupiny (konst. -> cache misto strlen v hot-path) */

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
        s_seg_len[i] = (uint8_t)L;            /* cache pro freq_fill_segments (hot-path) */
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
    memset(d, '0', sizeof d);   /* hardening: kdyby Σ s_seg_len > s_freq_total, cti '0' (ne smeti) */
    uint64_t v = s_freq_n;
    for (int i = s_freq_total - 1; i >= 0; i--) { d[i] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    int p = 0, n = s_num.segment_count;
    for (int s = 0; s < n; s++) {
        int L = s_seg_len[s];                /* cachovana delka (num_build) misto strlen */
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
static void trend_feed(float v);    /* fwd — decimacni pyramida (dlouhodoby trend) */

static uint32_t s_stats_ver = 0;          /* verze dat: roste s kazdym vzorkem (change-key oken) */

static void stats_sample(void)
{
    /* off_n = odchylka v LSB (LSB=1e-7 Hz), f0=1e7 Hz -> y = off_n*1e-14 */
    int64_t off_n = (int64_t)s_freq_n - (int64_t)s_freq_center;
    float y = (float)off_n * 1e-14f;
    s_y[s_y_head] = y;                    /* plochy ring (kratkodobe) */
    s_y_head = (s_y_head + 1) % STAT_N;
    if (s_y_count < STAT_N) s_y_count++;
    adev_feed(y);                         /* decimacni pyramida (dlouhodoby Allan) */
    trend_feed(y);                        /* decimacni pyramida (dlouhodoby trend, az ~60 dni) */
    s_stats_ver++;                        /* histogram okno prekresli jen pri zmene */
}

/* Verze statistickych dat — histogram okno se prekresli jen kdyz se zmeni
 * (v okne se nevzorkuje -> obsah je konstantni, zadne 2x/s prazdne redraws). */
uint32_t screen_main_stats_version(void) { return s_stats_ver; }

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
        if (have) { float d = bs - prev; acc += (double)d * (double)d; nd++; }
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
        adev_stage_t *sg = &s_adev[s];    /* 'sg', ne 'st' — nekolidovat s globalnim UI stavem */
        sg->ring[sg->head] = v;
        sg->head = (int16_t)((sg->head + 1) % ADEV_RING);
        if (sg->count < ADEV_RING) sg->count++;
        sg->acc += v;
        if (++sg->acc_n < 10) return;             /* dalsi stage jeste nema co krmit */
        v = sg->acc / 10.0f; sg->acc = 0; sg->acc_n = 0;   /* dekadovy prumer -> dal */
    }
}

static float adev_rat(const adev_stage_t *sg, int i)       /* i-ty nejstarsi prvek */
{
    int idx = (sg->head - sg->count + i + 2 * ADEV_RING) % ADEV_RING;
    return sg->ring[idx];
}

/* ── Decimacni pyramida pro DLOUHODOBY TREND (okno az ~60 dni) ────────────────
 * Plochy ring s_y[] pokryje jen 120 s; okno 30 dni by v nem chtelo 2,6 M vzorku
 * (~10 MB). Stejny princip jako ADEV pyramida vyse, ale JEMNEJSI decimace (×4
 * misto ×10) a delsi ringy -> hladsi krivka i u dlouhych oken. Stage s ma
 * rozliseni 4^s sekund a rozsah TR_RING*4^s:
 *   s=0: krok 1 s  rozsah 128 s      s=5: krok 1024 s  rozsah 1,5 dne
 *   s=1: krok 4 s  rozsah 8,5 min    s=6: krok 4096 s  rozsah 6,1 dne
 *   s=2: krok 16 s rozsah 34 min     s=7: krok 16384 s rozsah 24 dni
 *   s=3: krok 64 s rozsah 2,3 h      s=8: krok 65536 s rozsah 97 dni
 *   s=4: krok 256 s rozsah 9,1 h
 * Pamet ~4,7 kB (9 x 128 float) v RAM_D1 (512 kB) — zanedbatelne. */
#define TR_STAGES 9
#define TR_RING   128
#define TR_DECIM  4
typedef struct { float ring[TR_RING]; int16_t head, count; float acc; int16_t acc_n; } tr_stage_t;
static tr_stage_t s_tr[TR_STAGES];

static void trend_feed(float v)
{
    for (int s = 0; s < TR_STAGES; s++) {
        tr_stage_t *sg = &s_tr[s];
        sg->ring[sg->head] = v;
        sg->head = (int16_t)((sg->head + 1) % TR_RING);
        if (sg->count < TR_RING) sg->count++;
        sg->acc += v;
        if (++sg->acc_n < TR_DECIM) return;            /* vyssi stage jeste nema co krmit */
        v = sg->acc / (float)TR_DECIM; sg->acc = 0; sg->acc_n = 0;
    }
}

static int32_t tr_res(int s)            /* rozliseni stage [s/vzorek] = 4^s */
{
    int32_t r = 1; for (int i = 0; i < s; i++) r *= TR_DECIM; return r;
}

/* Nejnizsi (= nejjemnejsi) stage, jehoz rozsah pokryje pozadovane okno [s]. */
static int tr_pick(int32_t win_s)
{
    for (int s = 0; s < TR_STAGES; s++)
        if ((int64_t)TR_RING * tr_res(s) >= (int64_t)win_s) return s;
    return TR_STAGES - 1;
}

static float tr_at(int s, int age)      /* age 0 = nejnovejsi */
{
    const tr_stage_t *sg = &s_tr[s];
    int idx = (sg->head - 1 - age + 2 * TR_RING) % TR_RING;
    return sg->ring[idx];
}

/* Doba [s] -> kompaktni text ("45 s" / "10 min" / "6 h" / "30 d"). */
static void fmt_dur(char *b, int n, int32_t s)
{
    if      (s < 60)    snprintf(b, n, "%ld s",   (long)s);
    else if (s < 3600)  snprintf(b, n, "%ld min", (long)(s / 60));
    else if (s < 86400) snprintf(b, n, "%ld h",   (long)(s / 3600));
    else                snprintf(b, n, "%ld d",   (long)(s / 86400));
}
void screen_main_fmt_dur(char *b, int n, int32_t s) { fmt_dur(b, n, s); }

/* Non-overlapping ADEV stage s pri decimaci m (tau = m*10^s s). */
static float adev_stage(int s, int m)
{
    const adev_stage_t *sg = &s_adev[s];
    int blocks = sg->count / m;
    if (blocks < 2) return 0.0f;
    float prev = 0; int have = 0; double acc = 0; int nd = 0;
    for (int b = 0; b < blocks; b++) {
        float bs = 0;
        for (int j = 0; j < m; j++) bs += adev_rat(sg, b * m + j);
        bs /= (float)m;
        if (have) { float d = bs - prev; acc += (double)d * (double)d; nd++; }
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
    if (M >= 10) { M = 1; m = 0; e--; }   /* 9,96 -> zaokrouhleni pres dekadu: 10,0×10⁻⁹ -> 1,0×10⁻⁸ */
    int ae = (e < 0) ? -e : e;
    char es[12]; es[0] = '\0';
    if (ae >= 10) strcat(es, SUP[(ae / 10) % 10]);
    strcat(es, SUP[ae % 10]);
    const char *sign    = (v < 0) ? "-" : (with_sign ? "+" : "");
    const char *expsign = (e < 0) ? "⁺" : "⁻";
    /* %10u: M je 1..9 a m 0..9 vzdy — modulo to rekne i kompilatoru (zadny
     * teoreticky -Wformat-truncation na 11mistny int) */
    snprintf(buf, len, "%s%u,%u×10%s%s", sign, (unsigned)M % 10u, (unsigned)m % 10u,
             expsign, es);
}

/* Selftest cistych UI helperu: fmt_frac vektory (vc. zaokrouhleni pres dekadu)
 * + hist_h invarianty (peak=plna vyska, log zveda slabe biny). Zadny sdileny
 * stav -> bezpecne z UartTasku za behu. Soucast UART "selftest". */
static int16_t hist_h(float count, int peak, int16_t H, bool logy);   /* fwd */
bool screen_main_selftest(void)
{
    char b[24]; int ok = 1;
    fmt_frac(b, sizeof b, 1.23e-8f, 0);   ok &= (strcmp(b, "1,2×10⁻⁸") == 0);
    fmt_frac(b, sizeof b, -4.56e-11f, 0); ok &= (strcmp(b, "-4,6×10⁻¹¹") == 0);
    fmt_frac(b, sizeof b, 9.96e-9f, 0);   ok &= (strcmp(b, "1,0×10⁻⁸") == 0);   /* pres dekadu */
    fmt_frac(b, sizeof b, 0.0f, 0);       ok &= (strcmp(b, "0") == 0);
    fmt_frac(b, sizeof b, 2.5e-9f, 1);    ok &= (strcmp(b, "+2,5×10⁻⁹") == 0);
    ok &= (hist_h(0.0f, 10, 100, false) == 0);
    ok &= (hist_h(10.0f, 10, 100, false) == 100);
    ok &= (hist_h(10.0f, 10, 100, true) == 100);
    ok &= (hist_h(1.0f, 10, 100, true) > hist_h(1.0f, 10, 100, false));
    printf("ui: fmt_frac+hist_h selftest %s\n", ok ? "OK" : "FAIL");
    return ok != 0;
}

/* Rect karet pro zive prekresleni (zachyceno pri full renderu). */
static prim_rect_t s_allan_rect = {0,0,0,0};
static prim_rect_t s_trend_rect = {0,0,0,0};
static prim_rect_t s_small_rect = {0,0,0,0};

/* Tap do Allan karty (hlavni obrazovka) -> otevre histogram okno (analyza mereni). */
bool screen_main_hit_allan(int16_t x, int16_t y)
{
    return s_allan_rect.w != 0 && pt_in(x, y, s_allan_rect);
}

/* Tap do trend karty -> fullscreen trend (cela historie ringu, ne jen 60 s). */
bool screen_main_hit_trend(int16_t x, int16_t y)
{
    return s_trend_rect.w != 0 && pt_in(x, y, s_trend_rect);
}

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
    /* Ztlumene '↗' hned za titulkem = naznak ze karta je klikaci (tap -> histogram okno). */
    int16_t hx = (int16_t)(rect.x + UI_DIM_CARD_PAD_X
                           + prim_text_width("Allan σy(τ)", &ui_font_sans_18) + 6);
    prim_draw_text((prim_point_t){hx, (int16_t)(rect.y + UI_DIM_CARD_PAD_Y + 16)},
                   "↗", &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
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
    /* Ztlumene '↗' za titulkem = naznak klikatelnosti (tap -> fullscreen trend). */
    int16_t hx = (int16_t)(rect.x + UI_DIM_CARD_PAD_X
                           + prim_text_width(SCR_S_TREND_L, &ui_font_sans_18) + 6);
    prim_draw_text((prim_point_t){hx, (int16_t)(rect.y + UI_DIM_CARD_PAD_Y + 16)},
                   "↗", &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
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

/* Signal bargraf = REALNY vstupni vykon z AD8307 log-detektoru (ADS1115 AIN1;
 * SensorsTask fast-path ~10 Hz), zobrazeny v dBm. Prevod mV->dBm dela volajici
 * (app_gpsdo_tick_signal, konstanty AD8307). Drzime rect + hodnotu pro partial redraw. */
static prim_rect_t s_signal_rect = {0, 0, 0, 0};
static int16_t     s_signal_pct  = 0;
static int32_t     s_signal_dbm10 = -100000;  /* posl. zobrazene dBm×10 (<-99999 = jeste nic) */

/* Naformatuje dBm×10 do "-45.5 dBm" (bez %f); pro sentinel "--- dBm". */
static void fmt_dbm(char *buf, int n, int32_t dbm10)
{
    if (dbm10 <= -100000) { snprintf(buf, n, "--- dBm"); return; }
    long a = dbm10 < 0 ? -dbm10 : dbm10;
    snprintf(buf, n, "%s%ld.%ld dBm", dbm10 < 0 ? "-" : "", a / 10, a % 10);
}

/* Plne vykresleni signal karty (chrome + label + bar) — pro full render. */
static void draw_signal_card(prim_rect_t rect, int16_t pct)
{
    static char val[24];
    ui_card_t card = {.rect = rect};
    ui_card_render_chrome(&card);
    prim_rect_t inner = ui_card_inner_rect(&card);
    fmt_dbm(val, sizeof val, s_signal_dbm10);
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

/* Cas + datum z RTC (LSE, disciplinovany GPS UTC). Tika plynule 1×/s i pri
 * ztrate fixu; pred prvnim GPS syncem "--:--:--" / "no GPS". Prekresli JEN oblast
 * casu a JEN kdyz se zmeni sekunda nebo datum (zadny zbytecny redraw -> zadny "px sum"). */
int screen_main_redraw_time(uint32_t ms_since_boot)
{
    (void)ms_since_boot;
    static char     last_time[16] = "";
    static char     last_date[26] = "";   /* "YYYY-MM-DD UTC+14" (datum + label zony) */
    static uint8_t  last_icons = 0xFF;

    /* Cas + datum z RTC (LSE, disciplinovany GPS) -> tika plynule i pri ztrate
     * fixu. Dokud nebyl srovnan z GPS, ukazuje "--:--:--" / "no GPS". */
    char tb[16], db[16];
    rtc_time_date(tb, db);

    /* Mikro-ikona mute (preskrtnuty reproduktor) primo vlevo od casu. Holdover
     * ukazuje AMBER "HOLD" pilulka (render_header), UTC je na radku data -> zadna
     * kolize s pilulkami (drivejsi "UTC"/"H" u casu do HOLD pilulky narazely). */
    uint8_t icons = (uint8_t)(g_sound_muted ? 1u : 0u);

    /* Radek data nese i label zony ("UTC+2") -> klic je SLOZENY retezec, aby se
     * prekreslil i pri zmene zony v Nastaveni beze zmeny samotneho data. */
    char dl[26]; snprintf(dl, sizeof dl, "%s %s", db, (const char *)g_tz_label);
    int tchg = (strcmp(tb, last_time) != 0);
    int dchg = (strcmp(dl, last_date) != 0);
    int ichg = (icons != last_icons);
    if (!tchg && !dchg && !ichg) return 0;
    strncpy(last_time, tb, sizeof last_time - 1); last_time[sizeof last_time - 1] = '\0';
    strncpy(last_date, dl, sizeof last_date - 1); last_date[sizeof last_date - 1] = '\0';
    last_icons = icons;
    strncpy(s_time_buf, tb, sizeof s_time_buf - 1); s_time_buf[sizeof s_time_buf - 1] = '\0';

    int16_t time_x = UI_DIM_SCREEN_W - UI_DIM_PADDING_X / 2;
    if (tchg || ichg) {   /* cas: baseline 23, glyf y 1..34 (datum @46 se nedotkne) */
        int16_t tw = prim_text_width(s_time_buf, &ui_font_mono_25);
        blit_bg_region((prim_rect_t){(int16_t)(time_x - tw - 26), 1, (int16_t)(tw + 32), 33});
        prim_draw_text((prim_point_t){time_x, 23}, s_time_buf, &ui_font_mono_25,
                       UI_COLOR_INK, PRIM_ALIGN_RIGHT);
        if (icons & 1u)   /* mute: preskrtnuty reproduktor tesne vlevo od casu */
            ui_icon_speaker_muted((prim_point_t){(int16_t)(time_x - tw - 22), 4}, 18, UI_COLOR_BAD);
    }
    if (dchg) {   /* datum + label zony (baseline 46, vpravo dole — mimo pilulky).
                   * Clear FIXNI sirkou (150 px > nejdelsi "YYYY-MM-DD UTC+14"):
                   * pri zkraceni labelu (UTC+14 -> UTC) by clear dle nove sirky
                   * nechal zbytky stareho delsiho textu. */
        blit_bg_region((prim_rect_t){(int16_t)(time_x - 150), 35, 156, 18});
        prim_draw_text((prim_point_t){time_x, 46}, dl, &ui_font_sans_14,
                       UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
    }
    return 1;
}

/* Partial redraw horni listy z GPS: blitne header strip z bg cache a prekresli
 * pilulky (GNSS lock + pocet druzic) + cas/datum. Volat jen pri zmene GPS stavu
 * (sat/fix) — render_header cte gps_get() sam. */
int screen_main_redraw_header(void)
{
    blit_bg_region((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_HEADER_H});
    render_header();
    return 1;
}

/* Incremental prekresleni signal bargrafu (animace ~10x/s, dBm krok 1): chrome/
 * pozadi/label/stopa jsou staticke z render_main; prekresli jen segmenty zmenene
 * od minula + value text (dBm s 1 des. mistem). Misto ~20 fillu jen rozdil. Vrati 1. */
int screen_main_redraw_signal(int16_t pct, int32_t dbm10)
{
    if (s_signal_rect.w == 0) return 0;
    int16_t old = s_signal_pct;       /* hodnota aktualne na obrazovce */
    s_signal_pct = pct;
    ui_card_t card = {.rect = s_signal_rect};
    prim_rect_t inner = ui_card_inner_rect(&card);
    int drew = 0;

    /* Value text (dBm z AD8307): prekresli jen pri zmene o >=0.1 dB (sum ADS
     * necha text v klidu; bar ma vlastni 1% granularitu). */
    if (s_signal_dbm10 <= -100000 || dbm10 != s_signal_dbm10) {
        s_signal_dbm10 = dbm10;
        char val[24];
        fmt_dbm(val, sizeof val, dbm10);
        /* siroky clear box (122 px) — pokryva i nejdelsi variantu textu */
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
/* Krok simulace BEZ kresleni — voláno mimo hlavni obrazovku (jine okno /
 * screensaver), aby statistika (ring + ADEV pyramida) rostla 24/7 a Allan
 * dosahl dlouhych tau. Kresli se az zase na main. */
void screen_main_freq_sim_step(void)
{
    freq_step();
}

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

/* Prepinac lin/log Y osy histogramu (log zviditelni slabe biny/chvosty). */
static bool s_hist_logy = false;
bool screen_main_hist_logy(void)        { return s_hist_logy; }
void screen_main_hist_toggle_logy(void) { s_hist_logy = !s_hist_logy; }

/* Vyska sloupce/krivky v px: linearni (count/peak) nebo log (log10(1+count)/log10(1+peak)). */
static int16_t hist_h(float count, int peak, int16_t H, bool logy)
{
    if (count <= 0.0f) return 0;
    float frac = logy ? (log10f(1.0f + count) / log10f(1.0f + (float)peak))
                      : (count / (float)peak);
    if (frac > 1.0f) frac = 1.0f;
    int16_t h = (int16_t)(frac * (float)H);
    return (h < 1) ? 1 : h;
}

/* Histogram distribuce frakcni odchylky y (poslednich s_y_count vzorku, τ0=1s).
 * Auto-range [min..max], svisle sloupce; mean (zelena) + median (amber) cara +
 * Gaussova referencni krivka (ocekavane cetnosti pri normalnim rozdeleni) +
 * overlay N/mean/sigma/median. Lin/log Y (screen_main_hist_toggle_logy). Kresli
 * do 'rect' (uvnitr karty histogram okna) a sam si ho vycisti -> lze volat 2x/s.
 * Data plni stats_sample nezavisle na okne (sim krokuje i mimo main) -> ZIVY
 * (change-key v app_gpsdo prekresli pri kazdem novem vzorku, ~1x/s). */
void screen_main_render_histogram(prim_rect_t rect)
{
    prim_fill_rect(rect, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);   /* clear + dirty-rect */

    int n = s_y_count;
    if (n < 4) {
        prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w / 2),
                                      (int16_t)(rect.y + rect.h / 2)},
                       "Waiting for data...", &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
        return;
    }

    /* Jedna kopie ringu do lokalniho pole (stat_at dela modulo — dal uz jen
     * linearni pristupy); poradi je pro min/max/mean/biny/median nepodstatne. */
    float srt[STAT_N];
    for (int i = 0; i < n; i++) srt[i] = stat_at(i);

    float mn = srt[0], mx = mn, sum = 0.0f;
    for (int i = 0; i < n; i++) { float v = srt[i]; if (v < mn) mn = v; if (v > mx) mx = v; sum += v; }
    float mean = sum / (float)n;
    float span = mx - mn;
    if (span < 1e-18f) span = 1e-18f;                    /* degenerace -> vyhni se /0 */

    /* binovani + rozptyl (sample stddev distribuce, ne ADEV) */
    #define HIST_BINS 24
    int bins[HIST_BINS] = {0};
    double var = 0.0;
    for (int i = 0; i < n; i++) {
        float v = srt[i];
        int b = (int)((v - mn) / span * (float)HIST_BINS);
        if (b < 0) b = 0; else if (b >= HIST_BINS) b = HIST_BINS - 1;
        bins[b]++;
        double d = (double)(v - mean); var += d * d;
    }
    float sd = (n > 1) ? (float)sqrt(var / (double)n) : 0.0f;
    int peak = 1;
    for (int b = 0; b < HIST_BINS; b++) if (bins[b] > peak) peak = bins[b];

    /* median: insertion sort in-place (n<=120, cold path — kresli se jen pri zmene dat) */
    for (int i = 1; i < n; i++) {
        float k = srt[i]; int j = i - 1;
        while (j >= 0 && srt[j] > k) { srt[j + 1] = srt[j]; j--; }
        srt[j + 1] = k;
    }
    float median = (n & 1) ? srt[n / 2] : 0.5f * (srt[n / 2 - 1] + srt[n / 2]);
    bool logy = s_hist_logy;

    /* plot area: leva rezerva na peak-count, dolni na y-popisky */
    prim_rect_t in = {(int16_t)(rect.x + 34), (int16_t)(rect.y + 26),
                      (int16_t)(rect.w - 42), (int16_t)(rect.h - 48)};

    prim_draw_line((prim_point_t){in.x, (int16_t)(in.y + in.h)},          /* baseline */
                   (prim_point_t){(int16_t)(in.x + in.w), (int16_t)(in.y + in.h)}, 1, UI_COLOR_LINE);
    char cb[16]; snprintf(cb, sizeof cb, "%d%s", peak, logy ? " log" : "");
    prim_draw_text((prim_point_t){(int16_t)(in.x - 6), (int16_t)(in.y + 6)}, cb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);

    int16_t bw = (int16_t)(in.w / HIST_BINS); if (bw < 2) bw = 2;
    for (int b = 0; b < HIST_BINS; b++) {
        if (bins[b] == 0) continue;
        int16_t h = hist_h((float)bins[b], peak, in.h, logy);
        int16_t bx = (int16_t)(in.x + b * bw);
        prim_fill_rect((prim_rect_t){bx, (int16_t)(in.y + in.h - h),
                                     (int16_t)(bw - 1), h}, UI_COLOR_ACC, PRIM_BLEND_OVER);
    }

    /* Gaussova referencni krivka: ocekavane cetnosti N*binW*pdf(v) pri normalnim
     * rozdeleni (mean,sd) -> porovnani tvaru (pretazeni / tezke chvosty). */
    if (sd > 1e-18f) {
        const float binW = span / (float)HIST_BINS;
        const float kk = (float)n * binW / (sd * 2.5066283f);     /* N*binW/(sd*sqrt(2π)) */
        int16_t py_prev = 0, have_prev = 0;
        for (int px = 0; px <= in.w; px += 3) {
            float v = mn + (float)px / (float)in.w * span;
            float z = (v - mean) / sd;
            int16_t hy = hist_h(kk * expf(-0.5f * z * z), peak, in.h, logy);
            int16_t py = (int16_t)(in.y + in.h - hy);
            int16_t cx = (int16_t)(in.x + px);
            if (have_prev)
                prim_draw_line((prim_point_t){(int16_t)(cx - 3), py_prev},
                               (prim_point_t){cx, py}, 1, UI_COLOR_INK_2);
            py_prev = py; have_prev = 1;
        }
    }

    /* mean (zelena) + median (amber) svisle cary */
    int16_t mpx = (int16_t)(in.x + (int16_t)((mean - mn) / span * (float)in.w));
    prim_draw_line((prim_point_t){mpx, in.y}, (prim_point_t){mpx, (int16_t)(in.y + in.h)},
                   1, UI_COLOR_OK);
    int16_t dpx = (int16_t)(in.x + (int16_t)((median - mn) / span * (float)in.w));
    prim_draw_line((prim_point_t){dpx, in.y}, (prim_point_t){dpx, (int16_t)(in.y + in.h)},
                   1, UI_COLOR_WARN);

    /* X popisky: min (vlevo) / max (vpravo) ve frac notaci */
    char lb[24], rb[24];
    fmt_frac(lb, sizeof lb, mn, 1);
    fmt_frac(rb, sizeof rb, mx, 1);
    prim_draw_text((prim_point_t){in.x, (int16_t)(in.y + in.h + 16)}, lb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(in.x + in.w), (int16_t)(in.y + in.h + 16)}, rb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);

    /* overlay: N/mean/sigma (radek 1, vpravo nahore) + median (radek 2, amber) */
    char ov[80], mb[24], sb[24];
    fmt_frac(mb, sizeof mb, mean, 1);
    fmt_frac(sb, sizeof sb, sd, 0);
    snprintf(ov, sizeof ov, "N=%d  x=%s  s=%s", n, mb, sb);
    prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w), (int16_t)(rect.y + 16)}, ov,
                   &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_RIGHT);
    char db[28]; fmt_frac(mb, sizeof mb, median, 1);
    snprintf(db, sizeof db, "med=%s", mb);
    prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w), (int16_t)(rect.y + 36)}, db,
                   &ui_font_mono_14, UI_COLOR_WARN, PRIM_ALIGN_RIGHT);
    #undef HIST_BINS
}

/* σy(τ) tabulka: Allanova deviace pro dekadova τ (1..10k s). τ=1s z kratkeho ringu
 * (stats_adev), delsi z decimacni pyramidy (adev_stage). "--" dokud neni dost dat.
 * Kresli do 'rect' + sam vycisti -> volatelne 2x/s vedle histogramu. */
void screen_main_render_stats_table(prim_rect_t rect)
{
    prim_fill_rect(rect, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    /* Baseline uvnitr cisteneho rectu (ascent 16 px) — jinak by AA hrany nad
     * rectem pri 2x/s refreshi postupne tuhly (kresli se mimo clear oblast). */
    prim_draw_text((prim_point_t){rect.x, (int16_t)(rect.y + 18)}, "σy(τ)  Allan",
                   &ui_font_sans_16, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    static const char *TL[5] = {"1 s", "10 s", "100 s", "1 ks", "10 ks"};
    int16_t ry = (int16_t)(rect.y + 46);
    for (int i = 0; i < 5; i++) {
        float a = (i == 0) ? stats_adev(1) : adev_stage(i, 1);
        char vb[24];
        if (a > 0.0f) fmt_frac(vb, sizeof vb, a, 0);
        else { vb[0] = vb[1] = '-'; vb[2] = '\0'; }
        int16_t ty = (int16_t)(ry + i * 30);
        prim_draw_text((prim_point_t){rect.x, ty}, TL[i],
                       &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w), ty}, vb,
                       &ui_font_mono_14, (a > 0.0f) ? UI_COLOR_INK_2 : UI_COLOR_INK_4,
                       PRIM_ALIGN_RIGHT);
    }
}

/* Casove okno fullscreen trendu [s]. Prepinatelne tlacitky (presety 1 min..60 dni;
 * dlouha okna se kresli z decimacni pyramidy s_tr[], viz trend_feed). */
static int32_t s_trend_secs = 120;
void screen_main_trend_set_secs(int s) { if (s > 0) s_trend_secs = (int32_t)s; }
int  screen_main_trend_secs(void)      { return (int)s_trend_secs; }

/* Fullscreen trend: posledních s_trend_secs z decimacni pyramidy — vybere se
 * nejjemnejsi stage, ktery okno pokryje (tr_pick). Kdyz jeste nema dost dat
 * (vyssi stage se plni pomalu — napr. stage 8 az po 18 h behu), spadne se na
 * nejvyssi stage s aspon 2 vzorky, aby bylo VZDY videt neco misto "waiting".
 * Auto-scale s min/max popisky (fmt_frac), mrizka, overlay okno/krok/drift. */
void screen_main_render_trend_big(prim_rect_t rect)
{
    prim_fill_rect(rect, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);

    /* 'ts', ne 'st' — 'st' je globalni UI stav (stejny duvod jako 'sg' u ADEV). */
    int ts = tr_pick(s_trend_secs);
    while (ts > 0 && s_tr[ts].count < 2) ts--;   /* fallback na to, co uz mame */
    int32_t res = tr_res(ts);
    int32_t want = s_trend_secs / res;           /* kolik vzorku okno potrebuje */
    if (want > TR_RING) want = TR_RING;
    int n = s_tr[ts].count;
    if ((int32_t)n > want) n = (int)want;

    if (n < 2) {
        prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w / 2),
                                      (int16_t)(rect.y + rect.h / 2)},
                       "Waiting for data...", &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
        return;
    }
    float mn = tr_at(ts, 0), mx = mn;
    for (int i = 1; i < n; i++) { float v = tr_at(ts, i); if (v < mn) mn = v; if (v > mx) mx = v; }
    float span = mx - mn;
    if (span < 1e-18f) span = 1e-18f;

    prim_rect_t in = {(int16_t)(rect.x + 8), (int16_t)(rect.y + 26),
                      (int16_t)(rect.w - 16), (int16_t)(rect.h - 52)};
    /* mrizka: 3 horizontaly (min/stred/max) */
    for (int j = 0; j <= 2; j++) {
        int16_t yy = (int16_t)(in.y + (int32_t)j * in.h / 2);
        prim_draw_line((prim_point_t){in.x, yy}, (prim_point_t){(int16_t)(in.x + in.w), yy},
                       1, UI_COLOR_LINE);
    }
    /* krivka: nejstarsi vlevo -> nejnovejsi vpravo */
    int16_t px_prev = 0, py_prev = 0;
    for (int i = 0; i < n; i++) {
        float v = tr_at(ts, n - 1 - i);
        int16_t px = (int16_t)(in.x + (int32_t)i * in.w / (n - 1));
        int16_t py = (int16_t)(in.y + in.h - (int16_t)((v - mn) / span * (float)in.h));
        if (i) prim_draw_line((prim_point_t){px_prev, py_prev}, (prim_point_t){px, py},
                              2, UI_COLOR_ACC);
        px_prev = px; py_prev = py;
    }
    /* posledni bod zvyrazneny */
    prim_fill_circle((prim_point_t){px_prev, py_prev}, 4, UI_COLOR_ACC_SOFT);

    /* Y popisky: max nahore / min dole (frac notace) */
    char lb[24];
    fmt_frac(lb, sizeof lb, mx, 1);
    prim_draw_text((prim_point_t){in.x, (int16_t)(in.y - 6)}, lb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    fmt_frac(lb, sizeof lb, mn, 1);
    prim_draw_text((prim_point_t){in.x, (int16_t)(in.y + in.h + 16)}, lb,
                   &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    /* overlay: okno + skutecne pokryty cas + krok decimace (vpravo nahore).
     * "pokryto" = n*res muze byt MENE nez okno, dokud stage nenasbira data. */
    char ov[96], fb2[24], dw[16], dr[16], dc[16];
    fmt_dur(dw, sizeof dw, s_trend_secs);
    fmt_dur(dr, sizeof dr, res);
    fmt_dur(dc, sizeof dc, (int32_t)n * res);
    if ((int32_t)n * res < s_trend_secs - res)      /* jeste se sbira -> ukaz oboji */
        snprintf(ov, sizeof ov, "okno %s · zatim %s · krok %s", dw, dc, dr);
    else
        snprintf(ov, sizeof ov, "okno %s · krok %s · N=%d", dw, dr, n);
    prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w - 6), (int16_t)(in.y - 6)}, ov,
                   &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_RIGHT);
    fmt_frac(fb2, sizeof fb2, stats_drift(), 1);
    snprintf(ov, sizeof ov, "drift %s/min", fb2);
    prim_draw_text((prim_point_t){(int16_t)(rect.x + rect.w - 6), (int16_t)(in.y + in.h + 16)}, ov,
                   &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
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
