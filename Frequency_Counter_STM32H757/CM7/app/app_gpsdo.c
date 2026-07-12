/**
 * @file app_gpsdo.c
 * @brief Glue: init libprim+HAL, drive the main and diagnostics screens, and
 *        route touch to the MENU / back buttons. Single-context (UiTask).
 */

#include "app_gpsdo.h"
#include "screens/screen_main.h"
#include "hal/stm32/prim_stm32_hal.h"
#include "sensor_stat.h"        /* g_sensors[] (hodnota + valid + statistika) */
#include "gps.h"                /* gps_get() — zive GPS data do GNSS okna */
#include "w25q.h"               /* w25q_read_jedec — externi flash v okne PAMET */
#include "w25q_map.h"           /* W25Q_DATA_BASE/SIZE — okno Datalog */
#include "alarm.h"              /* g_alarm_* pocitadla — okno Alarmy */
#include "cmsis_os2.h"          /* osThreadGetStackSpace (volny stack tasku) */
#include <prim/prim.h>
#include <ui/ui.h>
#include <stdio.h>
#include <string.h>
#include <math.h>    /* sinf/cosf — sky plot druzic (GPS okno) */
#include "version.h" /* FW_VERSION_FULL — okno "O pristroji" + splash (== UART) */

/* Firmware sensor globals (defined in freertos.c). */
extern volatile char    g_spi_text[64];          /* FPGA SPI status line */
extern volatile uint8_t g_spi_ok;                /* 1 = link alive */
extern volatile char    g_freq_info[64];         /* FPGA quality: gate/PH/SEQ line */
extern volatile uint8_t g_si5356_status;         /* reg 218: SYS_CAL/LOS_CLKIN/PLL_LOL */
extern volatile uint8_t g_si5356_ok;             /* 1 = status read OK */
extern volatile uint32_t g_rtos_heap_free;       /* free heap [B] */
extern volatile uint32_t g_rtos_heap_min;        /* min-ever-free heap [B] */
extern volatile uint32_t g_rtos_cpu_pct;         /* CPU load [%] */
extern volatile uint32_t g_uptime_s;             /* uptime [s] */
extern volatile char     g_rtc_text[24];         /* "YYYY-MM-DD HH:MM:SS" (RTC z LSE, sync z GPS) */
extern volatile uint8_t  g_rtc_synced;           /* 1 = RTC srovnan z GPS */
extern volatile uint8_t  g_brightness;           /* jas displeje 0-255 (okno Nastaveni) */
extern volatile uint8_t  g_sound_muted;          /* 1 = zvuk vypnut */
extern volatile uint8_t  g_autodim_en;           /* 1 = auto-dim po necinnosti */
extern volatile uint16_t g_autodim_sec;          /* prodleva auto-dim [s] (preset 15..600) */
extern volatile uint8_t  g_theme_light;          /* 0 = tmave schema, 1 = svetle */
extern volatile uint8_t  g_lang_en;              /* 0 = cesky, 1 = english (texty postupne) */
extern volatile uint8_t  g_sys_cfg_dirty;        /* 1 = zmena jas/mute/dim -> persist do BKP */
extern volatile char     g_reset_text[12];       /* pricina posledniho resetu (main.c) */
extern volatile uint8_t  g_reset_bad;            /* 1 = watchdog reset (cervene) */
extern volatile char     g_crash_text[16];       /* crash black-box z BKP ("stack:UiTask") */
extern volatile uint8_t  g_selftest_res;         /* boot selftest: 0=--- 1=PASS 2=FAIL */

/* FreeRTOS task handles (defined in freertos.c) — pro volny stack v System Health. */
extern osThreadId_t UiTaskHandle, FpgaTaskHandle, UartTaskHandle,
                    I2C4TaskHandle, defaultTaskHandle;

/* Linker symboly (adresy) pro vyuziti interni FLASH/RAM v okne PAMET. */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

/* Si5356 status bits (reg 218 / 0xDA). */
#define SI5356_SYS_CAL    (1u << 0)
#define SI5356_LOS_CLKIN  (1u << 2)
#define SI5356_PLL_LOL    (1u << 4)

static prim_fb_t s_fb;
static int s_inited = 0;
static int s_view = 0;          /* 0=main 1=diag 2=gps 3=health 4=senzory 5=pamet (podmenu health) */

/* Present coalescing: vysokofrekvencni ticky (clock/signal/freq) jen renderuji a
 * nastavi s_dirty; jeden flip pak udela app_gpsdo_flush() (UiTask ho vola na ~30Hz
 * gate). Snizi pocet VBR flipu + sjednoti copy-forward. Vzacne udalosti (touch,
 * prepnuti obrazovky, render/clear) prezentuji hned pres present_now(). */
static int s_dirty = 0;
static void present_now(void) { prim_stm32_present(); s_dirty = 0; }

/* Back button on the diagnostics screen. */
/* Back button lives in the bottom bar, in the same slot as the main MENU. */
static const prim_rect_t BACK_RECT = {650, 417, 133, 61};
/* "SENZORY" tlacitko v System Health (bottom-left) -> podmenu vsech senzoru. */
static const prim_rect_t SENS_BTN_RECT = {18, 417, 180, 61};
/* "PAMET" tlacitko v System Health (bottom-mid) -> podokno vyuziti pameti. */
static const prim_rect_t MEM_BTN_RECT = {210, 417, 180, 61};
/* Histogram okno: lin/log Y toggle (bottom-left) + plot (leva cast) + σy(τ) tabulka (prava). */
static const prim_rect_t LOGY_RECT       = {18, 417, 180, 61};
/* Trend okno: RELATIVNI +/- casove okno (presety 10/20/30/60/120 s), hodnota mezi. */
static const prim_rect_t TREND_MINUS = {18, 417, 90, 61};
static const prim_rect_t TREND_PLUS  = {214, 417, 90, 61};
static const int16_t TREND_PRESETS[] = {10, 20, 30, 60, 120};
#define TREND_PRESET_N ((int)(sizeof(TREND_PRESETS)/sizeof(TREND_PRESETS[0])))
static const prim_rect_t HIST_PLOT_RECT  = {26, 96, 540, 300};
static const prim_rect_t HIST_TABLE_RECT = {582, 100, 180, 292};
/* "NASTAVENI" tlacitko v System Health (bottom, vedle SENZORY/PAMET). */
static const prim_rect_t SET_BTN_RECT = {402, 417, 180, 61};
/* Ovladace v okne Nastaveni (2 sloupce jako diag): levy = Zvuk / Jas / Auto-dim,
 * pravy = Vzhled (schema) / Jazyk (+ rezerva na dalsi polozky). */
static const prim_rect_t MUTE_RECT   = {230, 74, 148, 56};    /* Zvuk: zap/vyp */
static const prim_rect_t BR_MINUS    = {30, 188, 72, 56};     /* Jas - */
static const prim_rect_t BR_PLUS     = {110, 188, 72, 56};    /* Jas + */
static const prim_rect_t ADEN_RECT   = {30, 300, 140, 56};    /* Auto-dim zap/vyp */
static const prim_rect_t DIM_MINUS   = {186, 300, 56, 56};    /* prodleva - */
static const prim_rect_t DIM_PLUS    = {316, 300, 56, 56};    /* prodleva + */
static const prim_rect_t THEME_RECT  = {602, 74, 160, 56};    /* Vzhled: TMAVE/SVETLE */
static const prim_rect_t LANG_RECT   = {602, 166, 160, 56};   /* Jazyk: CESKY/ENGLISH */
static const prim_rect_t ABOUT_RECT  = {410, 340, 372, 56};   /* O pristroji (dolni pravy) */

static bool in_rect(int16_t x, int16_t y, prim_rect_t r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* ── Navigacni zasobnik (BACK se vraci k tomu, odkud okno OTEVRENO) ──────────
 * Pri kazdem forward prechodu se pushne aktualni s_view; BACK popne a vykresli.
 * Umoznuje "z okna oteviraneho z Menu -> BACK zpet do Menu" i vnoreni
 * (Menu->Nastaveni->O pristroji). app_gpsdo_render_main resetuje (koren). */
static uint8_t s_nav_stack[6];
static int     s_nav_sp = 0;
static void nav_push(uint8_t from) { if (s_nav_sp < 6) s_nav_stack[s_nav_sp++] = from; }
static void goto_view(uint8_t v)
{
    switch (v) {
    case 3:  app_gpsdo_render_health();   break;   /* Health (spawnuje senzory/pamet/nastaveni) */
    case 7:  app_gpsdo_render_settings(); break;   /* Nastaveni (spawnuje O pristroji) */
    case 12: app_gpsdo_render_menu();     break;   /* Menu rozcestnik */
    default: app_gpsdo_render_main();     break;   /* koren */
    }
}
static void nav_back(void)
{
    uint8_t v = (s_nav_sp > 0) ? s_nav_stack[--s_nav_sp] : 0;
    goto_view(v);
}

void app_gpsdo_init(void)
{
    if (s_inited) return;
    ui_theme_select(g_theme_light);   /* ulozene schema (BKP_DR6) PRED prvnim renderem */
    prim_stm32_init(&s_fb);
    screen_main_init();
    s_inited = 1;
}

void app_gpsdo_render_main(void)
{
    app_gpsdo_init();
    s_view = 0;
    s_nav_sp = 0;    /* hlavni obrazovka = koren navigace */
    prim_set_target(&s_fb);
    prim_reset_clip();
    screen_main_render();
    present_now();          /* flip hotovy snimek na displej (tearing-free) */
}

/* ── Diagnostics screen ─────────────────────────────────────── */

/* Format a temperature without %f (nano.specs may omit float printf). 2 des. mista. */
static void fmt_temp(char *buf, size_t n, float v)
{
    int t = (int)(v * 100.0f + (v >= 0.0f ? 0.5f : -0.5f));
    int w = t / 100, f = t % 100;
    if (f < 0) f = -f;
    snprintf(buf, n, "%d.%02d C", w, f);
}

/* ── Dvousloupcový layout diagnostiky ──────────────────────────────────── */
#define DG_MX    18                              /* outer margin */
#define DG_GAP   12                              /* column gap */
#define DG_COLW  376                             /* column width */
#define DG_LX    DG_MX                           /* left column x */
#define DG_RX    (DG_MX + DG_COLW + DG_GAP)      /* right column x */
#define DG_LLBL  (DG_LX + 12)                    /* left col label x */
#define DG_RLBL  (DG_RX + 12)                    /* right col label x */
#define DG_LVAL  (DG_LX + DG_COLW - 14)          /* left col value right edge */
#define DG_RVAL  (DG_RX + DG_COLW - 14)          /* right col value right edge */

/* GPS okno ma NEsymetricke sloupce: levy (Druzice: sky/bar prepinatelne dotykem)
 * je siroky, pravy (cas/poloha/timepulse/prijimac) zmenseny ~1/3. */
#define GPS_LX    DG_MX                           /* 18 */
#define GPS_LW    502                             /* levy sloupec (Druzice) — siroky */
#define GPS_LLBL  (GPS_LX + 12)                   /* 30 */
#define GPS_RX    532                             /* pravy sloupec x (right-aligned na 782) */
#define GPS_RW    250                             /* pravy sloupec sirka (~2/3 z 376) */
#define GPS_RLBL  (GPS_RX + 12)                   /* 544 */

/* GPS okno: karta Druzice — prepinani zobrazeni (bargraf <-> sky plot) dotykem.
 * Roztazena az po spodni okraj (160..478). */
static const prim_rect_t GPS_SAT_RECT = {GPS_LX, 160, GPS_LW, 318};
static bool s_gps_polar = false;   /* false = bargraf C/N0 (default), true = polarni sky plot */

/* A static label drawn once into the chrome (left, sans). */
/* Popisek (menu) řádku diagnostiky: STEJNÝ font jako živá hodnota (mono_18),
 * odlišený jen barvou — tlumená (INK_3) vs světlá hodnota (INK). */
static void dlabel(int16_t x, int16_t y, const char *s)
{
    prim_draw_text((prim_point_t){x, y}, s, &ui_font_mono_18, UI_COLOR_INK_3,
                   PRIM_ALIGN_LEFT);
}

/* Right-aligned live value: clear its box then redraw. valid==0 → dimmed + red "!".
 * Kresli se pod CLIPEM boxu — text delsi nez boxw se orizne, misto aby pretekl
 * doleva pres label (dtext/dtext_c clipuji odjakziva, dval byl jediny bez). */
static void dval(int16_t xr, int16_t baseline, int16_t boxw, const char *v, int valid)
{
    prim_rect_t box = {(int16_t)(xr - boxw), (int16_t)(baseline - 17), boxw, 22};
    prim_fill_rect(box, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_set_clip(box);
    prim_draw_text((prim_point_t){xr, baseline}, v, &ui_font_mono_18,
                   valid ? UI_COLOR_INK : UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
    prim_reset_clip();
    if (!valid)
        prim_draw_text((prim_point_t){(int16_t)(xr - boxw + 2), baseline}, "!",
                       &ui_font_mono_18, UI_COLOR_BAD, PRIM_ALIGN_LEFT);
}

/* Left-aligned live text in a cleared box (status lines, colorized).
 * Text se OŘÍZNE na šířku boxu -> dlouhý řetězec (SPI stav, velké SEQ/CRC)
 * nepřeteče kartu. */
static void dtext(int16_t x, int16_t baseline, int16_t boxw, const char *v,
                  prim_color_t col, const prim_font_t *font)
{
    prim_rect_t box = {x, (int16_t)(baseline - 16), boxw, 22};
    prim_fill_rect(box, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_set_clip(box);
    prim_draw_text((prim_point_t){x, baseline}, v, font, col, PRIM_ALIGN_LEFT);
    prim_reset_clip();
}

/* Center-aligned live text in a cleared box (GPS okno). */
static void dtext_c(int16_t cx, int16_t baseline, int16_t boxw, const char *v,
                    prim_color_t col, const prim_font_t *font)
{
    prim_rect_t box = {(int16_t)(cx - boxw / 2), (int16_t)(baseline - 16), boxw, 22};
    prim_fill_rect(box, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_set_clip(box);
    prim_draw_text((prim_point_t){cx, baseline}, v, font, col, PRIM_ALIGN_CENTER);
    prim_reset_clip();
}

/* GPS souradnice -> "dd.ddddddH" (bez float v printf, integer extrakce). */
static void fmt_ll(float v, char pos, char neg, char *out, size_t n)
{
    char h = (v >= 0.0f) ? pos : neg;
    if (v < 0.0f) v = -v;
    long ud = (long)(v * 1000000.0f + 0.5f);
    snprintf(out, n, "%ld.%06ld%c", ud / 1000000, ud % 1000000, h);
}

/* float DOP/1-desetinne -> "1.7" (bez %f). */
static void fmt_d1(float v, char *out, size_t n)
{
    if (v <= 0.0f) { snprintf(out, n, "--"); return; }
    long t = (long)(v * 10.0f + 0.5f);
    snprintf(out, n, "%ld.%ld", t / 10, t % 10);
}

/* Round a float reading to long without pulling in <math.h>. */
static long lround_f(float v) { return (long)(v >= 0.0f ? v + 0.5f : v - 0.5f); }

/* Maidenhead lokator (10 znaku, 5 paru: field/square/subsquare/ext/ext2),
 * napr. "JN89NS85KN". Vstup = zem. sirka/delka [°]. Pary 3+5 velkymi pismeny. */
static void fmt_locator(float lat, float lon, char *out, size_t n)
{
    double lo = (double)lon + 180.0;   /* 0..360 */
    double la = (double)lat + 90.0;    /* 0..180 */
    if (lo < 0) lo = 0; else if (lo >= 360) lo = 359.999999;
    if (la < 0) la = 0; else if (la >= 180) la = 179.999999;
    char b[11];
    b[0] = (char)('A' + (int)(lo / 20.0));  b[1] = (char)('A' + (int)(la / 10.0));
    lo = fmod(lo, 20.0);  la = fmod(la, 10.0);
    b[2] = (char)('0' + (int)(lo / 2.0));   b[3] = (char)('0' + (int)(la / 1.0));
    lo = fmod(lo, 2.0);   la = fmod(la, 1.0);
    b[4] = (char)('A' + (int)(lo / (2.0 / 24.0)));  b[5] = (char)('A' + (int)(la / (1.0 / 24.0)));
    lo = fmod(lo, 2.0 / 24.0);   la = fmod(la, 1.0 / 24.0);
    b[6] = (char)('0' + (int)(lo / (2.0 / 240.0))); b[7] = (char)('0' + (int)(la / (1.0 / 240.0)));
    lo = fmod(lo, 2.0 / 240.0);  la = fmod(la, 1.0 / 240.0);
    b[8] = (char)('A' + (int)(lo / (2.0 / 5760.0)));b[9] = (char)('A' + (int)(la / (1.0 / 5760.0)));
    b[10] = '\0';
    snprintf(out, n, "%s", b);
}

/* Selftest cistych app helperu (Maidenhead lokator — nova, snadno chybova logika).
 * Bez sdileneho stavu -> bezpecne za behu; soucast UART "selftest"/boot selftestu. */
bool app_gpsdo_selftest(void)
{
    char loc[16]; int ok = 1;
    fmt_locator(49.52f, 17.55f, loc, sizeof loc);   /* interni bod -> field/square/subsquare */
    ok &= (strncmp(loc, "JN89SM", 6) == 0);
    fmt_locator(0.0f, 0.0f, loc, sizeof loc);        /* rovnik + nulty poledník -> pole "JJ" */
    ok &= (loc[0] == 'J' && loc[1] == 'J');
    printf("app: fmt_locator selftest %s (%s)\n", ok ? "OK" : "FAIL", loc);
    return ok != 0;
}

/* Compact "min/max" with 1 decimal from a sensor's stats. */
static void fmt_minmax(char *buf, size_t n, const sensor_stat_t *s)
{
    if (s->samples == 0) { snprintf(buf, n, "--/--"); return; }
    int lo = (int)(s->min * 10.0f + (s->min >= 0 ? 0.5f : -0.5f));
    int hi = (int)(s->max * 10.0f + (s->max >= 0 ? 0.5f : -0.5f));
    /* clamp na realny rozsah senzoru (teploty ±125.0) — ohranici i GCC
     * -Wformat-truncation analyzu (jinak pocita s 11mistnym intem) */
    if (lo > 9999) lo = 9999;
    if (lo < -9999) lo = -9999;
    if (hi > 9999) hi = 9999;
    if (hi < -9999) hi = -9999;
    int lf = lo % 10, hf = hi % 10; if (lf < 0) lf = -lf; if (hf < 0) hf = -hf;
    snprintf(buf, n, "%d.%d/%d.%d", lo / 10, lf, hi / 10, hf);
}

/* Change-detect: vrati 1 (a aktualizuje cache) kdyz se 'now' lisi od 'cache'. */
static int dchg(char *cache, size_t n, const char *now)
{
    if (strncmp(cache, now, n) == 0) return 0;
    strncpy(cache, now, n - 1);
    cache[n - 1] = '\0';
    return 1;
}

/* Redraw dynamic values. force=1 -> prekresli VSE (po blitu chrome jsou hodnoty
 * smazane); force=0 (tick) -> jen pole, ktera se ZMENILA -> usetri DMA2D fill +
 * cache invalidaci + CPU text u nemennych poli (vetsina). */
/* Vrati 1 pokud se NECO prekreslilo (-> volajici flipne present), jinak 0. */
static int draw_diag_values(int force)
{
    static char c_tv[4][20], c_tm[4][20], c_adc[4][20], c_mcu[2][20];
    static char c_spi[68], c_fpga[68], c_si[20], c_sys[5][20];
    char buf[24], key[26];
    int drew = force;   /* force -> vse se kresli */

    /* ── Levy sloupec: teploty (hodnota + min/max), poradi = labely v chrome:
     * STM board (0x48) / MCU jadro (ADC3) / OCXO (0x49) / FPGA board (0x4A). ── */
    static const sensor_id_t tid[4] = { SENS_T48, SENS_CORE_T, SENS_T49, SENS_T4A };
    static const int16_t     ty[4]  = { 104, 130, 156, 182 };
    for (int i = 0; i < 4; i++) {
        const sensor_stat_t *s = &g_sensors[tid[i]];
        fmt_minmax(buf, sizeof(buf), s);
        if (force || dchg(c_tm[i], sizeof(c_tm[i]), buf)) {
            dtext((int16_t)(DG_LLBL + 96), ty[i], 118, buf, UI_COLOR_INK_3, &ui_font_sans_16); drew = 1; }
        fmt_temp(buf, sizeof(buf), s->last);
        snprintf(key, sizeof(key), "%c%s", s->valid ? 'V' : 'X', buf);  /* vykresleni zalezi i na valid */
        if (force || dchg(c_tv[i], sizeof(c_tv[i]), key)) {
            dval(DG_LVAL, ty[i], 104, buf, s->valid); drew = 1; }
    }

    /* Napeti: ADS1115 AIN0..3 (258/284/310/336) + MCU VREF/VBAT (362/388). Roztec 26. */
    for (int k = 0; k < 4; k++) {
        const sensor_stat_t *a = &g_sensors[SENS_ADS0 + k];
        snprintf(buf, sizeof(buf), "%ld mV", lround_f(a->last));
        snprintf(key, sizeof(key), "%c%s", a->valid ? 'V' : 'X', buf);
        if (force || dchg(c_adc[k], sizeof(c_adc[k]), key)) {
            dval(DG_LVAL, (int16_t)(258 + k * 26), 120, buf, a->valid); drew = 1; }
    }
    for (int k = 0; k < 2; k++) {
        const sensor_stat_t *mv = &g_sensors[SENS_VDDA + k];
        snprintf(buf, sizeof(buf), "%ld mV", lround_f(mv->last));
        snprintf(key, sizeof(key), "%c%s", mv->valid ? 'V' : 'X', buf);
        if (force || dchg(c_mcu[k], sizeof(c_mcu[k]), key)) {
            dval(DG_LVAL, (int16_t)(362 + k * 26), 120, buf, mv->valid); drew = 1; }
    }

    /* ── Pravy sloupec ── */
    /* FPGA: SPI status (barva dle g_spi_ok -> klic vc. ok) + merici kvalita. */
    char sig[68];
    snprintf(sig, sizeof(sig), "%c%s", g_spi_ok ? 'O' : 'X', (const char *)g_spi_text);
    if (force || dchg(c_spi, sizeof(c_spi), sig)) {
        /* mono_14 ZAMERNE (jediny 14px text v oknech): NOLINK radek ma az 38 zn
         * (38x10=380 px @16 > box 352 -> orizl by se RX0/CRC ocas; @14 = 304 px OK). */
        dtext(DG_RLBL, 104, DG_COLW - 24, (const char *)g_spi_text,
              g_spi_ok ? UI_COLOR_OK : UI_COLOR_BAD, &ui_font_mono_14); drew = 1; }
    if (force || dchg(c_fpga, sizeof(c_fpga), (const char *)g_freq_info)) {
        dtext(DG_RLBL, 132, DG_COLW - 24, (const char *)g_freq_info, UI_COLOR_INK_2, &ui_font_sans_16); drew = 1; }

    /* Reference Si5356: lock status (retezec 1:1 se statusem -> staci porovnat si). */
    const char *si; prim_color_t sic;
    if (!g_si5356_ok)                                   { si = "N/A (I2C)";   sic = UI_COLOR_INK_3; }
    else if (g_si5356_status & SI5356_LOS_CLKIN)        { si = "LOS CLKIN!";  sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_PLL_LOL)          { si = "PLL UNLOCK!"; sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_SYS_CAL)          { si = "CALIB...";    sic = UI_COLOR_VIOLET; }
    else                                                { si = "LOCK OK";     sic = UI_COLOR_OK; }
    if (force || dchg(c_si, sizeof(c_si), si)) {
        dtext(DG_RLBL, 206, DG_COLW - 24, si, sic, &ui_font_mono_18); drew = 1; }

    /* System / RTOS / RTC. */
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_free);
    if (force || dchg(c_sys[0], sizeof(c_sys[0]), buf)) { dval(DG_RVAL, 288, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_min);
    if (force || dchg(c_sys[1], sizeof(c_sys[1]), buf)) { dval(DG_RVAL, 314, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu %%", (unsigned long)g_rtos_cpu_pct);
    if (force || dchg(c_sys[2], sizeof(c_sys[2]), buf)) { dval(DG_RVAL, 340, 150, buf, 1); drew = 1; }
    { uint32_t s = g_uptime_s;
      snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu",
               (unsigned long)(s / 3600u), (unsigned long)((s / 60u) % 60u),
               (unsigned long)(s % 60u)); }
    if (force || dchg(c_sys[3], sizeof(c_sys[3]), buf)) { dval(DG_RVAL, 366, 150, buf, 1); drew = 1; }

    /* RTC: cas HH:MM:SS z g_rtc_text ("YYYY-MM-DD HH:MM:SS"). synced=0 -> ztlumeny
     * + "no GPS" (jeste nesrovnano z GPS). Klic vc. sync stavu (rozhoduje o barve). */
    { char rt[24]; uint8_t rsy;
      strncpy(rt, (const char *)g_rtc_text, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
      rsy = g_rtc_synced;
      if (rsy && strlen(rt) >= 19) snprintf(buf, sizeof(buf), "%.8s", rt + 11);
      else                         snprintf(buf, sizeof(buf), "no GPS");
      snprintf(key, sizeof(key), "%c%s", rsy ? 'V' : 'X', buf);
      if (force || dchg(c_sys[4], sizeof(c_sys[4]), key)) { dval(DG_RVAL, 392, 150, buf, rsy); drew = 1; } }

    return drew;
}

void app_gpsdo_render_diag(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();

    int first = (s_view != 1);
    if (first) {
        /* First entry: draw the static chrome + labels exactly once. */
        s_view = 1;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL,
                            .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "DIAGNOSTIKA",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);

        /* Left column: Teploty (vc. MCU jadra) + Napeti (ADS1115 + MCU).
         * ⚠️ FOOTER PRAVIDLO: spodni lista (y >= 416) je VZDY dedikovana
         * tlacitkum -> obsah konci <= 404. (Drivejsi 3. karta "MCU" sahala
         * do 472 = do listy; jeji radky jsou slouceny sem, roztec 26 px.) */
        ui_card_t c_temp = {.rect = {DG_LX, 58, DG_COLW, 144},
                            .header_label = "Teploty  (last  min/max)"};
        ui_card_render_chrome(&c_temp);
        dlabel(DG_LLBL, 104, "STM board");    /* TMP117 0x48 (I2C4) */
        dlabel(DG_LLBL, 130, "MCU jadro");    /* ADC3 interni senzor */
        dlabel(DG_LLBL, 156, "OCXO");         /* TMP117 0x49 (I2C1, FPGA deska) */
        dlabel(DG_LLBL, 182, "FPGA board");   /* TMP117 0x4A (I2C1, neosazen) */

        ui_card_t c_adc = {.rect = {DG_LX, 212, DG_COLW, 192},
                           .header_label = "Napeti (ADS1115 + MCU)"};
        ui_card_render_chrome(&c_adc);
        dlabel(DG_LLBL, 258, "OCXO_VC");      /* AIN0: ladici napeti OCXO */
        dlabel(DG_LLBL, 284, "RF_Level");     /* AIN1: uroven vstupniho signalu */
        dlabel(DG_LLBL, 310, "AIN2 (12V)");
        dlabel(DG_LLBL, 336, "AIN3 (5V)");
        dlabel(DG_LLBL, 362, "VREF");
        dlabel(DG_LLBL, 388, "VBAT");

        /* Right column: FPGA + Reference + System. */
        ui_card_t c_fpga = {.rect = {DG_RX, 58, DG_COLW, 92},
                            .header_label = "Komunikace + mereni FPGA"};
        ui_card_render_chrome(&c_fpga);

        ui_card_t c_ref = {.rect = {DG_RX, 160, DG_COLW, 72},
                           .header_label = "Reference Si5356 (4x100MHz)"};
        ui_card_render_chrome(&c_ref);

        ui_card_t c_sys = {.rect = {DG_RX, 242, DG_COLW, 162},
                           .header_label = "System / RTOS / RTC"};
        ui_card_render_chrome(&c_sys);
        dlabel(DG_RLBL, 288, "Heap free");
        dlabel(DG_RLBL, 314, "Heap min");
        dlabel(DG_RLBL, 340, "CPU");
        dlabel(DG_RLBL, 366, "Uptime");
        dlabel(DG_RLBL, 392, "RTC (UTC)");
    }
    /* present (flip) jen kdyz se neco prekreslilo (first=1 vzdy kresli chrome+vse). */
    if (draw_diag_values(first)) present_now();
}

/* Zive hodnoty GPS okna (s_view=2). force=1 po vykresleni chrome -> vse;
 * force=0 (tick) -> jen zmenena pole. Vrati 1 pokud se neco prekreslilo. */
static int draw_gps_values(int force)
{
    static char c_fix[16], c_sat[24], c_bars[64], c_dop[28], c_time[20], c_date[16];
    static char c_lat[24], c_lon[24], c_alt[20], c_tp[20], c_loc[24], c_rx[40];
    char buf[64], a[16], b[16];
    gps_data_t g;
    gps_get(&g);
    int drew = force;

    /* ── Radek 1 karty FIX (bez nadpisu): "FIX: 3D/2D/No signal" (velke, vlevo) +
     * Time Pulse (vpravo). Time Pulse: s fixem 100 kHz (GPSDO PLL ref, disc. na
     * GNSS), bez fixu 10 Hz (hold VC / holdover). ── */
    const char *fs; prim_color_t fc;
    if      (g.valid && g.fix_mode == 3) { fs = "FIX: 3D";        fc = UI_COLOR_OK; }
    else if (g.valid && g.fix_mode == 2) { fs = "FIX: 2D";        fc = UI_COLOR_OK; }
    else if (g.fix_quality > 0)          { fs = "FIX: OK";        fc = UI_COLOR_OK; }
    else                                 { fs = "FIX: No signal"; fc = UI_COLOR_INK_3; }
    if (force || dchg(c_fix, sizeof c_fix, fs)) {
        prim_fill_rect((prim_rect_t){GPS_LLBL, 74, 288, 30}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_draw_text((prim_point_t){GPS_LLBL, 96}, fs, &ui_font_mono_25, fc, PRIM_ALIGN_LEFT);
        drew = 1; }

    const char *tp; prim_color_t tc;
    if (g.fix_quality) { tp = "Time Pulse 100 kHz"; tc = UI_COLOR_OK; }
    else               { tp = "Time Pulse 10 Hz";   tc = UI_COLOR_WARN; }
    if (force || dchg(c_tp, sizeof c_tp, tp)) {
        prim_fill_rect((prim_rect_t){300, 74, (int16_t)(GPS_LX + GPS_LW - 14 - 300), 30},
                       UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_draw_text((prim_point_t){(int16_t)(GPS_LX + GPS_LW - 14), 96}, tp,
                       &ui_font_mono_18, tc, PRIM_ALIGN_RIGHT); drew = 1; }

    /* ── Radek 2 karty FIX: pocet druzic (vlevo) + HDOP/PDOP (vpravo, presunuto sem) ── */
    snprintf(buf, sizeof buf, "%u / %u druzic", g.num_sat, g.sats_in_view);
    if (force || dchg(c_sat, sizeof c_sat, buf)) {
        dtext(GPS_LLBL, 134, 200, buf, UI_COLOR_INK_3, &ui_font_sans_16); drew = 1; }
    fmt_d1(g.hdop, a, sizeof a); fmt_d1(g.pdop, b, sizeof b);
    snprintf(buf, sizeof buf, "HDOP %s   PDOP %s", a, b);
    if (force || dchg(c_dop, sizeof c_dop, buf)) {
        prim_fill_rect((prim_rect_t){300, 118, (int16_t)(GPS_LX + GPS_LW - 14 - 300), 24},
                       UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_draw_text((prim_point_t){(int16_t)(GPS_LX + GPS_LW - 14), 134}, buf,
                       &ui_font_mono_16, UI_COLOR_INK_3, PRIM_ALIGN_RIGHT); drew = 1; }
    /* naznak prepinani zobrazeni druzic (staticky) vpravo v hlavicce karty Druzice */
    if (force)
        prim_draw_text((prim_point_t){(int16_t)(GPS_LX + GPS_LW - 14), 186},
                       "TAP: bar/sky", &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);

    /* Druzice: JEDNO zobrazeni na plnou sirku sloupce — bargraf C/N0 nebo polarni
     * sky plot (az/el), prepinatelne dotykem (s_gps_polar). Barva = C/N0. */
    {
        gps_sat_t sv[GPS_MAX_SATS];
        int nsv = g.sat_count;                        /* uint8_t 0..GPS_MAX_SATS -> vzdy >=0 */
        if (nsv > GPS_MAX_SATS) nsv = GPS_MAX_SATS;   /* pojistka proti pretekani sv[] */
        for (int i = 0; i < nsv; i++) sv[i] = g.sats[i];
        for (int i = 1; i < nsv; i++) {               /* insertion sort podle snr desc */
            gps_sat_t t = sv[i]; int j = i - 1;
            while (j >= 0 && sv[j].snr < t.snr) { sv[j + 1] = sv[j]; j--; }
            sv[j + 1] = t;
        }
        int n = nsv > 14 ? 14 : nsv;                  /* bary: az 14 (siroky sloupec) */
        /* zmenovy klic: mod + pocet + hash az/el/snr VSECH druzic (+ C/N0 baru) */
        uint32_t skyh = s_gps_polar ? 0xA5u : 0x5Au;
        for (int i = 0; i < nsv; i++)
            skyh = skyh * 31u + sv[i].prn + (uint32_t)(sv[i].azim / 4u) * 7u
                 + (uint32_t)(sv[i].elev / 4u) * 13u + (sv[i].snr / 8u);
        char key[64]; int kp = snprintf(key, sizeof key, "%d_%08lx", n, (unsigned long)skyh);
        for (int i = 0; i < n && kp < (int)sizeof key - 5; i++)
            kp += snprintf(key + kp, sizeof key - kp, ".%u", sv[i].snr);
        if (force || dchg(c_bars, sizeof c_bars, key)) {
            /* clear cele plochy druzic (194..476) — karta jde az po spodni okraj */
            prim_fill_rect((prim_rect_t){GPS_LLBL, 194, GPS_LW - 24, 282},
                           UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
            if (nsv == 0) {
                dtext_c((int16_t)(GPS_LX + GPS_LW / 2), 330, GPS_LW - 24,
                        "Hledam druzice...", UI_COLOR_WARN, &ui_font_sans_16);
            } else if (s_gps_polar) {
                /* ── Sky plot na plnou plochu: velky kruh r=132, stred=zenit, N nahore ── */
                const int16_t scx = (int16_t)(GPS_LX + GPS_LW / 2), scy = 332, sr = 132;
                prim_draw_arc((prim_point_t){scx, scy}, sr,                   1, UI_COLOR_LINE, 0, 360);
                prim_draw_arc((prim_point_t){scx, scy}, (int16_t)(sr * 2 / 3), 1, UI_COLOR_LINE, 0, 360);
                prim_draw_arc((prim_point_t){scx, scy}, (int16_t)(sr / 3),     1, UI_COLOR_LINE, 0, 360);
                prim_draw_line((prim_point_t){(int16_t)(scx - sr), scy},
                               (prim_point_t){(int16_t)(scx + sr), scy}, 1, UI_COLOR_LINE);
                prim_draw_line((prim_point_t){scx, (int16_t)(scy - sr)},
                               (prim_point_t){scx, (int16_t)(scy + sr)}, 1, UI_COLOR_LINE);
                prim_draw_text((prim_point_t){scx, (int16_t)(scy - sr + 15)}, "N",
                               &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
                prim_draw_text((prim_point_t){(int16_t)(scx + sr - 12), (int16_t)(scy + 5)}, "E",
                               &ui_font_mono_14, UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);
                for (int i = 0; i < nsv; i++) {       /* tecky: azimut 0=N po smeru hodin */
                    float azr = (float)sv[i].azim * 0.0174533f;
                    float rr  = (float)sr * (float)(90 - (sv[i].elev > 90 ? 90 : sv[i].elev)) / 90.0f;
                    int16_t px = (int16_t)(scx + sinf(azr) * rr);
                    int16_t py = (int16_t)(scy - cosf(azr) * rr);
                    uint8_t snr = sv[i].snr;
                    prim_color_t col = (snr >= 38) ? UI_COLOR_OK :
                                       (snr >= 25) ? UI_COLOR_WARN :
                                       (snr > 0)   ? UI_COLOR_BAD : UI_COLOR_INK_4;
                    prim_fill_circle((prim_point_t){px, py}, (int16_t)(snr > 0 ? 6 : 3), col);
                    if (snr > 0) {                    /* PRN vedle tecky */
                        char pr[6]; snprintf(pr, sizeof pr, "%u", sv[i].prn);
                        prim_draw_text((prim_point_t){(int16_t)(px + 8), (int16_t)(py + 4)}, pr,
                                       &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
                    }
                }
            } else {
                /* ── C/N0 bargraf na plnou plochu (az 14 nejsilnejsich) ── */
                const int16_t base = 452;             /* dolni hrana sloupcu (u spodniho okraje) */
                const int16_t maxh = 238;             /* vyska pro C/N0 = 55 dB-Hz */
                const int16_t area = (int16_t)(GPS_LW - 24);
                int16_t slot = (int16_t)(area / n);
                int16_t bw = (int16_t)(slot * 2 / 3);
                if (bw < 3) bw = 3;
                for (int i = 0; i < n; i++) {
                    uint8_t snr = sv[i].snr;
                    prim_color_t col = (snr >= 38) ? UI_COLOR_OK :
                                       (snr >= 25) ? UI_COLOR_WARN :
                                       (snr > 0)   ? UI_COLOR_BAD : UI_COLOR_INK_4;
                    int16_t cx = (int16_t)(GPS_LLBL + i * slot + slot / 2);
                    int16_t h  = (int16_t)((snr > 55 ? 55 : snr) * maxh / 55);
                    if (h < 2) h = 2;
                    prim_fill_rect((prim_rect_t){(int16_t)(cx - bw / 2), (int16_t)(base - h),
                                   bw, h}, col, PRIM_BLEND_REPLACE);
                    if (snr > 0) {                    /* C/N0 nad sloupcem */
                        char sn[6]; snprintf(sn, sizeof sn, "%u", snr);
                        prim_draw_text((prim_point_t){cx, (int16_t)(base - h - 4)}, sn,
                                       &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
                    }
                    char pr[6]; snprintf(pr, sizeof pr, "%u", sv[i].prn);
                    prim_draw_text((prim_point_t){cx, 468}, pr, &ui_font_mono_14,
                                   UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
                }
            }
            drew = 1;
        }
    }

    /* (HDOP/PDOP presunuty nahoru k FIX — radek 2 karty FIX.) */

    /* cas + datum z RTC (LSE, disciplinovany GPS). RTC tika i bez fixu -> karta
     * je vzdy zive; synced=0 (volny beh od bootu) ztlumime. g_rtc_text =
     * "YYYY-MM-DD HH:MM:SS" -> [0..9] datum, [11..18] cas. */
    char rt[24]; uint8_t rsy;
    strncpy(rt, (const char *)g_rtc_text, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
    rsy = g_rtc_synced;
    snprintf(buf, sizeof buf, "%s UTC", (strlen(rt) >= 19) ? rt + 11 : "--:--:--");
    if (force || dchg(c_time, sizeof c_time, buf)) {
        dtext(GPS_RLBL, 104, GPS_RW - 24, buf, rsy ? UI_COLOR_INK : UI_COLOR_INK_3,
              &ui_font_mono_16); drew = 1; }
    snprintf(buf, sizeof buf, "%.10s", rt);   /* datum "YYYY-MM-DD" (sync stav nese barva casu) */
    if (force || dchg(c_date, sizeof c_date, buf)) {
        dtext(GPS_RLBL, 126, GPS_RW - 24, buf, UI_COLOR_INK_3, &ui_font_sans_16); drew = 1; }

    /* poloha */
    if (g.valid) fmt_ll(g.lat_deg, 'N', 'S', a, sizeof a); else snprintf(a, sizeof a, "--");
    snprintf(buf, sizeof buf, "Lat  %s", a);
    if (force || dchg(c_lat, sizeof c_lat, buf)) {
        dtext(GPS_RLBL, 190, GPS_RW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_16); drew = 1; }
    if (g.valid) fmt_ll(g.lon_deg, 'E', 'W', a, sizeof a); else snprintf(a, sizeof a, "--");
    snprintf(buf, sizeof buf, "Lon  %s", a);
    if (force || dchg(c_lon, sizeof c_lon, buf)) {
        dtext(GPS_RLBL, 214, GPS_RW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_16); drew = 1; }
    if (g.fix_quality) snprintf(buf, sizeof buf, "Alt  %ld m", lround_f(g.alt_m));
    else               snprintf(buf, sizeof buf, "Alt  --");
    if (force || dchg(c_alt, sizeof c_alt, buf)) {
        dtext(GPS_RLBL, 238, GPS_RW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_16); drew = 1; }

    /* Lokator (Maidenhead grid) — karta bez nadpisu, jen "Locator <hodnota>"
     * (vetsim pismem, vycentrovano ve volne karte 252..320). */
    char loc[16];
    if (g.valid) fmt_locator(g.lat_deg, g.lon_deg, loc, sizeof loc);
    else         snprintf(loc, sizeof loc, "----------");
    snprintf(buf, sizeof buf, "Locator %s", loc);
    if (force || dchg(c_loc, sizeof c_loc, buf)) {
        dtext(GPS_RLBL, 294, GPS_RW - 24, buf, g.valid ? UI_COLOR_ACC : UI_COLOR_INK_3,
              &ui_font_mono_18); drew = 1; }

    /* Prijimac: zive statistiky linky (naparsovane vety + platne fixy) — rostou,
     * dokud GPS tece -> dukaz zivosti (staticke "NEO-7M" je v chrome). */
    snprintf(buf, sizeof buf, "Vet:%lu Fix:%lu",
             (unsigned long)g.sentences, (unsigned long)g.fixes);
    if (force || dchg(c_rx, sizeof c_rx, buf)) {
        dtext(GPS_RLBL, 384, GPS_RW - 24, buf, UI_COLOR_INK_2, &ui_font_mono_16); drew = 1; }

    return drew;
}

/* GPS / GNSS okno (tap na GNSS pill, ZPET zpet na main). Zive (refresh ~2x/s
 * v app_gpsdo_tick). First entry kresli chrome, pak jen zmenena pole. */
void app_gpsdo_render_gps(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();

    int first = (s_view != 2);
    if (first) {
        s_view = 2;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "GNSS / GPS",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);

        /* Levy (siroky) sloupec: FIX (bez nadpisu — FIX/druzice/DOP/TimePulse jsou
         * uvnitr) + Druzice roztazena AZ PO SPODNI OKRAJ (vyjimka z footer pravidla —
         * BACK je vpravo, levy sloupec ho nekryje). */
        ui_card_t c_fix = {.rect = {GPS_LX, 58, GPS_LW, 96}};   /* bez header_label */
        ui_card_render_chrome(&c_fix);
        ui_card_t c_sat = {.rect = {GPS_LX, 160, GPS_LW, 318},   /* 160..478 = spodni okraj */
                           .header_label = "Druzice"};
        ui_card_render_chrome(&c_sat);
        /* Pravy (uzsi) sloupec: cas/poloha/lokator/prijimac. */
        ui_card_t c_time = {.rect = {GPS_RX, 58, GPS_RW, 76}, .header_label = "Cas / datum (UTC)"};
        ui_card_render_chrome(&c_time);
        ui_card_t c_pos = {.rect = {GPS_RX, 144, GPS_RW, 98}, .header_label = "Poloha"};
        ui_card_render_chrome(&c_pos);
        ui_card_t c_loc = {.rect = {GPS_RX, 252, GPS_RW, 68}};   /* Lokator — bez nadpisu */
        ui_card_render_chrome(&c_loc);
        ui_card_t c_rx = {.rect = {GPS_RX, 330, GPS_RW, 74},
                          .header_label = "Prijimac NEO-7M"};
        ui_card_render_chrome(&c_rx);
    }
    if (draw_gps_values(first)) present_now();
}

/* ── System Health okno (s_view=3) ────────────────────────────────────────
 * Otevre se tapem na SYS pill (vedle GNSS). Hlubsi pohled nez diagnostika:
 * RTOS pamet+CPU, volny stack tasku (osThreadGetStackSpace), I2C chybovost
 * (z g_sensors err citacu), stav linku (FPGA/Si5356/senzory) a napajeci vetve.
 * Live refresh ~2x/s pres app_gpsdo_tick (stejny first/values split jako diag). */

/* Agreguje I2C chybovost ze skupiny senzoru: streak = max souvislych chyb,
 * total = soucet chyb od bootu. (Health = streak==0 -> ted OK.) */
static void i2c_health(const sensor_id_t *ids, int n, uint16_t *streak, uint32_t *total)
{
    uint16_t st = 0; uint32_t tot = 0;
    for (int i = 0; i < n; i++) {
        const sensor_stat_t *s = &g_sensors[ids[i]];
        if (s->err_streak > st) st = s->err_streak;
        tot += s->err_total;
    }
    *streak = st; *total = tot;
}

/* Prekresli dynamicke hodnoty System Health. force=1 -> vse (po blitu chrome),
 * force=0 (tick) -> jen zmenena pole. Vrati 1 pokud neco kreslil. */
static int draw_health_values(int force)
{
    static char c_rtos[3][20], c_stk[5][16], c_i2c[2][40], c_lnk[3][40];
    char buf[36], key[40];   /* "I2C1 FPGA: CHYBA (<u32>)" az 29 zn. -> bez truncation */
    int drew = force;

    /* ── Levy sloupec: RTOS pamet / CPU / uptime ── */
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_free);
    if (force || dchg(c_rtos[0], sizeof(c_rtos[0]), buf)) { dval(DG_LVAL, 104, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_min);
    if (force || dchg(c_rtos[1], sizeof(c_rtos[1]), buf)) { dval(DG_LVAL, 132, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu %%", (unsigned long)g_rtos_cpu_pct);
    if (force || dchg(c_rtos[2], sizeof(c_rtos[2]), buf)) { dval(DG_LVAL, 160, 150, buf, 1); drew = 1; }
    /* Uptime presunut do karty "System" (pravy sloupec) — c_rtos[3] nevyuzito. */

    /* ── Levy sloupec: volny stack tasku (high-water headroom, bajty) ── */
    osThreadId_t thr[5] = { UiTaskHandle, FpgaTaskHandle, UartTaskHandle,
                            I2C4TaskHandle, defaultTaskHandle };
    static const int16_t sy[5] = { 264, 292, 320, 348, 376 };
    for (int i = 0; i < 5; i++) {
        uint32_t fb = thr[i] ? osThreadGetStackSpace(thr[i]) : 0;
        snprintf(buf, sizeof(buf), "%lu B", (unsigned long)fb);
        /* < 64 B headroom = varovani (cerveny '!'); jinak svetla hodnota. */
        if (force || dchg(c_stk[i], sizeof(c_stk[i]), buf)) {
            dval(DG_LVAL, sy[i], 120, buf, fb >= 64); drew = 1; }
    }

    /* ── Pravy sloupec: I2C chybovost (celobarevne radky) ── */
    static const sensor_id_t i2c1_ids[5] = { SENS_T49, SENS_ADS0, SENS_ADS1,
                                             SENS_ADS2, SENS_ADS3 };  /* T4A vynechan (neosazen) */
    static const sensor_id_t i2c4_ids[1] = { SENS_T48 };
    uint16_t s1, s4; uint32_t t1, t4;
    i2c_health(i2c1_ids, 5, &s1, &t1);
    i2c_health(i2c4_ids, 1, &s4, &t4);
    snprintf(buf, sizeof(buf), "I2C1 FPGA: %s (%lu)", s1 ? "CHYBA" : "OK", (unsigned long)t1);
    snprintf(key, sizeof(key), "%c%s", s1 ? 'X' : 'O', buf);
    if (force || dchg(c_i2c[0], sizeof(c_i2c[0]), key)) {
        dtext(DG_RLBL, 104, DG_COLW - 24, buf, s1 ? UI_COLOR_BAD : UI_COLOR_OK, &ui_font_mono_16); drew = 1; }
    snprintf(buf, sizeof(buf), "I2C4 panel: %s (%lu)", s4 ? "CHYBA" : "OK", (unsigned long)t4);
    snprintf(key, sizeof(key), "%c%s", s4 ? 'X' : 'O', buf);
    if (force || dchg(c_i2c[1], sizeof(c_i2c[1]), key)) {
        dtext(DG_RLBL, 132, DG_COLW - 24, buf, s4 ? UI_COLOR_BAD : UI_COLOR_OK, &ui_font_mono_16); drew = 1; }

    /* ── Pravy sloupec: periferie / linky ── */
    snprintf(buf, sizeof(buf), "FPGA SPI: %s", g_spi_ok ? "LINK OK" : "NO LINK");
    snprintf(key, sizeof(key), "%c%s", g_spi_ok ? 'O' : 'X', buf);
    if (force || dchg(c_lnk[0], sizeof(c_lnk[0]), key)) {
        dtext(DG_RLBL, 200, DG_COLW - 24, buf, g_spi_ok ? UI_COLOR_OK : UI_COLOR_BAD, &ui_font_mono_16); drew = 1; }
    const char *si; prim_color_t sic;
    if (!g_si5356_ok)                            { si = "N/A (I2C)";   sic = UI_COLOR_INK_3; }
    else if (g_si5356_status & SI5356_LOS_CLKIN) { si = "LOS CLKIN!";  sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_PLL_LOL)   { si = "PLL UNLOCK!"; sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_SYS_CAL)   { si = "CALIB...";    sic = UI_COLOR_VIOLET; }
    else                                         { si = "LOCK OK";     sic = UI_COLOR_OK; }
    snprintf(buf, sizeof(buf), "Ref Si5356: %s", si);
    if (force || dchg(c_lnk[1], sizeof(c_lnk[1]), buf)) {
        dtext(DG_RLBL, 228, DG_COLW - 24, buf, sic, &ui_font_mono_16); drew = 1; }
    int nok = 0; for (int i = 0; i < SENS_COUNT; i++) if (g_sensors[i].valid) nok++;
    snprintf(buf, sizeof(buf), "Senzory: %d/%d OK", nok, (int)SENS_COUNT);
    snprintf(key, sizeof(key), "%c%s", (nok >= SENS_COUNT - 1) ? 'O' : 'X', buf);  /* -1: 0x4A neosazen */
    if (force || dchg(c_lnk[2], sizeof(c_lnk[2]), key)) {
        dtext(DG_RLBL, 256, DG_COLW - 24, buf,
              (nok >= SENS_COUNT - 1) ? UI_COLOR_OK : UI_COLOR_WARN, &ui_font_mono_16); drew = 1; }

    /* ── Pravy sloupec: karta System (napajeni souhrnne / uptime / reset / selftest).
     * Konkretni napeti vetvi jsou v Diagnostice + SENZORY; tady jen verdikt. ── */
    static char c_sys2[4][40];
    { const sensor_stat_t *v12 = &g_sensors[SENS_ADS2];
      const sensor_stat_t *v5  = &g_sensors[SENS_ADS3];
      const char *ps; prim_color_t pc;
      if (v12->samples == 0 || v5->samples == 0 || !v12->valid || !v5->valid) {
          ps = "Unkn"; pc = UI_COLOR_INK_3;
      } else {
          long m12 = lround_f(v12->last), m5 = lround_f(v5->last);
          int ok12 = (m12 > 10800 && m12 < 13200);    /* 12 V ±10 % */
          int ok5  = (m5  > 4500  && m5  < 5500);     /* 5 V ±10 % */
          if (ok12 && ok5) { ps = "OK";   pc = UI_COLOR_OK; }
          else             { ps = "FAIL"; pc = UI_COLOR_BAD; }
      }
      snprintf(buf, sizeof(buf), "Power supplies: %s", ps);
      /* mono_14 + radky od 328: hlavicka karty ("System") ma baseline 305 —
       * s mono_16 od 316 se prekryvaly (ascent 16 sahal na 300). */
      if (force || dchg(c_sys2[0], sizeof(c_sys2[0]), buf)) {
          dtext(DG_RLBL, 328, DG_COLW - 24, buf, pc, &ui_font_mono_14); drew = 1; } }
    { uint32_t s = g_uptime_s;
      snprintf(buf, sizeof(buf), "Uptime: %lu:%02lu:%02lu", (unsigned long)(s / 3600u),
               (unsigned long)((s / 60u) % 60u), (unsigned long)(s % 60u));
      if (force || dchg(c_sys2[1], sizeof(c_sys2[1]), buf)) {
          dtext(DG_RLBL, 350, DG_COLW - 24, buf, UI_COLOR_INK_2, &ui_font_mono_14); drew = 1; } }
    if (g_crash_text[0])
        snprintf(buf, sizeof(buf), "Reset: %s %s", (const char *)g_reset_text,
                 (const char *)g_crash_text);
    else
        snprintf(buf, sizeof(buf), "Reset: %s", (const char *)g_reset_text);
    int rbad = g_reset_bad || g_crash_text[0];
    if (force || dchg(c_sys2[2], sizeof(c_sys2[2]), buf)) {
        dtext(DG_RLBL, 372, DG_COLW - 24, buf,
              rbad ? UI_COLOR_BAD : UI_COLOR_INK_3, &ui_font_mono_14); drew = 1; }
    snprintf(buf, sizeof(buf), "Selftest: %s",
             g_selftest_res == 1 ? "PASS" : (g_selftest_res == 2 ? "FAIL" : "---"));
    if (force || dchg(c_sys2[3], sizeof(c_sys2[3]), buf)) {
        dtext(DG_RLBL, 394, DG_COLW - 24, buf,
              g_selftest_res == 1 ? UI_COLOR_OK
              : (g_selftest_res == 2 ? UI_COLOR_BAD : UI_COLOR_INK_4), &ui_font_mono_14);
        drew = 1; }

    return drew;
}

void app_gpsdo_render_health(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();

    int first = (s_view != 3);
    if (first) {
        s_view = 3;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        ui_button_t sens = {.rect = SENS_BTN_RECT, .variant = UI_BUTTON_NORMAL, .label = "SENZORY"};
        ui_button_render(&sens);
        ui_button_t mem = {.rect = MEM_BTN_RECT, .variant = UI_BUTTON_NORMAL, .label = "PAMET"};
        ui_button_render(&mem);
        ui_button_t set = {.rect = SET_BTN_RECT, .variant = UI_BUTTON_NORMAL, .label = "NASTAVENI"};
        ui_button_render(&set);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "SYSTEM HEALTH",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);

        /* Levy: RTOS + Stack tasku. */
        ui_card_t c_rtos = {.rect = {DG_LX, 58, DG_COLW, 122},
                            .header_label = "RTOS / Pamet"};
        ui_card_render_chrome(&c_rtos);
        dlabel(DG_LLBL, 104, "Heap free");
        dlabel(DG_LLBL, 132, "Heap min");
        dlabel(DG_LLBL, 160, "CPU");
        /* Uptime presunut do karty "System" (pravy sloupec). */

        ui_card_t c_stk = {.rect = {DG_LX, 218, DG_COLW, 186},
                           .header_label = "Stack tasku (volno)"};
        ui_card_render_chrome(&c_stk);
        dlabel(DG_LLBL, 264, "UiTask");
        dlabel(DG_LLBL, 292, "FpgaTask");
        dlabel(DG_LLBL, 320, "UartTask");
        dlabel(DG_LLBL, 348, "I2C4Task");
        dlabel(DG_LLBL, 376, "Default");

        /* Pravy: I2C + Linky + Napajeni. */
        ui_card_t c_i2c = {.rect = {DG_RX, 58, DG_COLW, 86},
                           .header_label = "I2C sbernice (chyby)"};
        ui_card_render_chrome(&c_i2c);

        ui_card_t c_lnk = {.rect = {DG_RX, 154, DG_COLW, 116},
                           .header_label = "Periferie / linky"};
        ui_card_render_chrome(&c_lnk);

        ui_card_t c_pwr = {.rect = {DG_RX, 280, DG_COLW, 124},
                           .header_label = "System"};
        ui_card_render_chrome(&c_pwr);
        /* vsechny radky (Napajeni/Uptime/Reset/Selftest) kresli draw_health_values
         * celobarevne (dtext) — konkretni napeti jsou v Diagnostice/SENZORY */
    }
    if (draw_health_values(first)) present_now();
}

/* ── Podmenu vsech senzoru (s_view=4), otevre se z System Health ──────────
 * Cisty prehled AKTUALNICH hodnot, dva sloupce jako diagnostika: vlevo Teploty,
 * vpravo Napeti. Jmena (dlabel) kresli render_sensors jednou; zive hodnoty
 * (dval, zarovnane vpravo) tady. Bez min/max/avg/err — jen aktualni hodnota. */
#define SENS_R0   132                /* y prvniho radku */
#define SENS_DY    32                /* rozteč radku */

/* Rozmisteni: id senzoru + sloupec (xr value) + radek (y) + zda je to teplota. */
static const struct { uint8_t id; int16_t xr; int16_t y; uint8_t temp; }
SENS_ROW[SENS_COUNT] = {
    { SENS_T48,    DG_LVAL, SENS_R0 + 0 * SENS_DY, 1 },   /* leva: Teploty (poradi = labely: */
    { SENS_CORE_T, DG_LVAL, SENS_R0 + 1 * SENS_DY, 1 },   /* STM board / MCU jadro /         */
    { SENS_T49,    DG_LVAL, SENS_R0 + 2 * SENS_DY, 1 },   /* OCXO / FPGA board)              */
    { SENS_T4A,    DG_LVAL, SENS_R0 + 3 * SENS_DY, 1 },
    { SENS_ADS0,   DG_RVAL, SENS_R0 + 0 * SENS_DY, 0 },   /* prava: Napeti */
    { SENS_ADS1,   DG_RVAL, SENS_R0 + 1 * SENS_DY, 0 },
    { SENS_ADS2,   DG_RVAL, SENS_R0 + 2 * SENS_DY, 0 },
    { SENS_ADS3,   DG_RVAL, SENS_R0 + 3 * SENS_DY, 0 },
    { SENS_VDDA,   DG_RVAL, SENS_R0 + 4 * SENS_DY, 0 },
    { SENS_VBAT,   DG_RVAL, SENS_R0 + 5 * SENS_DY, 0 },
};

static int draw_sensors_values(int force)
{
    static char c[SENS_COUNT][20];
    char buf[20], key[24];
    int drew = force;
    for (int i = 0; i < SENS_COUNT; i++) {
        const sensor_stat_t *s = &g_sensors[SENS_ROW[i].id];
        if (s->samples == 0)       snprintf(buf, sizeof buf, "---");
        else if (SENS_ROW[i].temp) fmt_temp(buf, sizeof buf, s->last);        /* "23.45 C" */
        else                       snprintf(buf, sizeof buf, "%ld mV", lround_f(s->last));
        snprintf(key, sizeof key, "%c%s", s->valid ? 'V' : 'X', buf);   /* redraw i pri zmene valid */
        if (force || dchg(c[i], sizeof c[i], key)) {
            dval(SENS_ROW[i].xr, SENS_ROW[i].y, 150, buf, s->valid); drew = 1;
        }
    }
    return drew;
}

void app_gpsdo_render_sensors(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();
    int first = (s_view != 4);
    if (first) {
        s_view = 4;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "SENZORY",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
        ui_card_t c = {.rect = {DG_LX, 58, 764, 346},
                       .header_label = "Aktualni hodnoty senzoru"};
        ui_card_render_chrome(&c);

        /* podnadpisy sloupcu */
        prim_draw_text((prim_point_t){DG_LLBL, 104}, "TEPLOTY  [C]", &ui_font_mono_16,
                       UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_RLBL, 104}, "NAPETI  [mV]", &ui_font_mono_16,
                       UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        /* jmena senzoru (poradi musi sedet se SENS_ROW) */
        dlabel(DG_LLBL, SENS_R0 + 0 * SENS_DY, "STM board");
        dlabel(DG_LLBL, SENS_R0 + 1 * SENS_DY, "MCU jadro");
        dlabel(DG_LLBL, SENS_R0 + 2 * SENS_DY, "OCXO");
        dlabel(DG_LLBL, SENS_R0 + 3 * SENS_DY, "FPGA board");
        dlabel(DG_RLBL, SENS_R0 + 0 * SENS_DY, "OCXO_VC");
        dlabel(DG_RLBL, SENS_R0 + 1 * SENS_DY, "RF_Level");
        dlabel(DG_RLBL, SENS_R0 + 2 * SENS_DY, "12V vetev");
        dlabel(DG_RLBL, SENS_R0 + 3 * SENS_DY, "5V vetev");
        dlabel(DG_RLBL, SENS_R0 + 4 * SENS_DY, "VREF");
        dlabel(DG_RLBL, SENS_R0 + 5 * SENS_DY, "VBAT");
    }
    if (draw_sensors_values(first)) present_now();
}

/* ── Podokno PAMET (s_view=5), otevre se z System Health ──────────────────
 * Vyuziti pameti: interni FLASH/RAM (staticky z linker symbolu), RTOS heap
 * (live), externi SDRAM 32 MB + W25Q 64 MB (JEDEC). Staticke hodnoty se kresli
 * jednou (first); live se refreshuje jen RTOS heap. JEDEC se cte 1x pri otevreni
 * (⚠️ QSPI zatim bez mutexu — kolize s UART qspi* je nepravdepodobna, viz TODO). */
static int draw_mem_values(int force)
{
    static char c[20];
    char buf[24];
    int drew = force;
    /* POUZITE heap (konzistentni s "pouzite/celkem" hlavickou i FLASH/RAM sloupci):
     * used = celkem - volne. 32 KB = configTOTAL_HEAP_SIZE. */
    uint32_t total = 32u * 1024u;
    uint32_t used  = (total > g_rtos_heap_free) ? (total - g_rtos_heap_free) : 0u;
    snprintf(buf, sizeof buf, "%lu/32 KB", (unsigned long)(used / 1024u));
    if (force || dchg(c, sizeof c, buf)) {
        dval(DG_LVAL, SENS_R0 + 2 * SENS_DY, 175, buf, 1); drew = 1;   /* RTOS heap pouzite */
    }
    return drew;
}

void app_gpsdo_render_mem(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();
    int first = (s_view != 5);
    if (first) {
        s_view = 5;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "PAMET",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
        ui_card_t card = {.rect = {DG_LX, 58, 764, 346},
                          .header_label = "Vyuziti pameti  (pouzite / celkem)"};
        ui_card_render_chrome(&card);

        prim_draw_text((prim_point_t){DG_LLBL, 104}, "INTERNI", &ui_font_mono_16,
                       UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_RLBL, 104}, "EXTERNI", &ui_font_mono_16,
                       UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        dlabel(DG_LLBL, SENS_R0 + 0 * SENS_DY, "FLASH (CM7)");
        dlabel(DG_LLBL, SENS_R0 + 1 * SENS_DY, "RAM D1");
        dlabel(DG_LLBL, SENS_R0 + 2 * SENS_DY, "RTOS heap");
        dlabel(DG_RLBL, SENS_R0 + 0 * SENS_DY, "SDRAM (FMC)");
        dlabel(DG_RLBL, SENS_R0 + 1 * SENS_DY, "QSPI W25Q");
        dlabel(DG_RLBL, SENS_R0 + 2 * SENS_DY, "  JEDEC");

        char b[24];
        /* interni FLASH (CM7 bank 1024 KB): image = _sidata + velikost .data - 0x08000000 */
        uint32_t fl = ((uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata)) - 0x08000000u;
        snprintf(b, sizeof b, "%lu/1024 KB", (unsigned long)(fl / 1024u));
        dval(DG_LVAL, SENS_R0 + 0 * SENS_DY, 175, b, 1);
        /* interni RAM_D1 (512 KB): staticky .data + .bss */
        uint32_t rm = ((uint32_t)&_edata - (uint32_t)&_sdata) + ((uint32_t)&_ebss - (uint32_t)&_sbss);
        snprintf(b, sizeof b, "%lu/512 KB", (unsigned long)(rm / 1024u));
        dval(DG_LVAL, SENS_R0 + 1 * SENS_DY, 175, b, 1);
        /* externi (staticke velikosti) */
        dval(DG_RVAL, SENS_R0 + 0 * SENS_DY, 175, "32 MB", 1);   /* SDRAM FMC */
        dval(DG_RVAL, SENS_R0 + 1 * SENS_DY, 175, "64 MB", 1);   /* W25Q */
        uint32_t id = w25q_read_jedec();
        snprintf(b, sizeof b, "%06lX %s", (unsigned long)id, id == W25Q_JEDEC_ID ? "OK" : "--");
        dval(DG_RVAL, SENS_R0 + 2 * SENS_DY, 175, b, id == W25Q_JEDEC_ID);
    }
    if (draw_mem_values(first)) present_now();
}

/* Tlacitko lin/log Y v histogram okne (label odrazi aktualni stav osy). */
static void render_logy_btn(void)
{
    ui_button_t b = {.rect = LOGY_RECT, .variant = UI_BUTTON_NORMAL,
                     .label = screen_main_hist_logy() ? "Y: LOG" : "Y: LIN"};
    ui_button_render(&b);
}

/* ── Histogram okno (s_view=6): otevre se tapem na Allan kartu (hlavni obrazovka).
 * Leva cast = histogram distribuce y (mean/median/Gauss, lin/log Y), prava cast =
 * σy(τ) Allan tabulka. Ploty dela screen_main (ma data ring + ADEV pyramidu).
 * Change-key skip: prekresli se JEN pri zmene dat (stats_version, ~1x/s pri
 * RUN) nebo lin/log osy — pri STOPu tick nic nedela (zadny sort/Gauss/ADEV
 * naprazdno, zadny zbytecny flip). Vzorkovani bezi nezavisle na okne. */
void app_gpsdo_render_histogram(void)
{
    static uint32_t s_hist_key;
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();
    int first = (s_view != 6);
    if (first) {
        s_view = 6;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "HISTOGRAM",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
        ui_card_t card = {.rect = {DG_LX, 58, 764, 346},
                          .header_label = "Rozdeleni y = (f-f0)/f0   |   Allan σy(τ)"};
        ui_card_render_chrome(&card);
        /* svisly delic mezi plotem a tabulkou */
        prim_draw_line((prim_point_t){572, 92}, (prim_point_t){572, 398}, 1, UI_COLOR_LINE);
    }
    uint32_t key = screen_main_stats_version()
                 ^ (screen_main_hist_logy() ? 0x80000000u : 0u);
    if (first || key != s_hist_key) {
        s_hist_key = key;
        render_logy_btn();                       /* label sleduje lin/log stav */
        screen_main_render_histogram(HIST_PLOT_RECT);
        screen_main_render_stats_table(HIST_TABLE_RECT);
        present_now();
    }
}

/* Krok relativniho casoveho okna trendu (mezi presety). */
static void trend_secs_step(int dir)
{
    int cur = screen_main_trend_secs(), idx = 0;
    for (int k = 0; k < TREND_PRESET_N; k++) if (TREND_PRESETS[k] <= cur) idx = k;
    idx += dir;
    if (idx < 0) idx = 0; else if (idx >= TREND_PRESET_N) idx = TREND_PRESET_N - 1;
    screen_main_trend_set_secs(TREND_PRESETS[idx]);
}

/* -/+ tlacitka + hodnota okna mezi nimi (dolni lista trend okna). */
static void render_trend_scale_btns(void)
{
    ui_button_t m = {.rect = TREND_MINUS, .variant = UI_BUTTON_NORMAL, .label = "-"};
    ui_button_t p = {.rect = TREND_PLUS,  .variant = UI_BUTTON_NORMAL, .label = "+"};
    ui_button_render(&m);
    ui_button_render(&p);
    prim_fill_rect_rounded((prim_rect_t){112, 419, 98, 57}, 6, UI_COLOR_BG_CARD, PRIM_BLEND_OVER);
    char v[16]; int s = screen_main_trend_secs();
    if (s >= 60 && s % 60 == 0) snprintf(v, sizeof v, "%d min", s / 60);
    else                        snprintf(v, sizeof v, "%d s", s);
    prim_draw_text((prim_point_t){161, 455}, v, &ui_font_mono_22, UI_COLOR_INK, PRIM_ALIGN_CENTER);
}

/* ── Trend fullscreen okno (s_view=9): tap na trend kartu na hlavni obrazovce.
 * Posledni s_trend_secs (30/60/120 s, tlacitka dole). Change-key skip. */
void app_gpsdo_render_trend(void)
{
    static uint32_t s_trend_key;
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();
    int first = (s_view != 9);
    if (first) {
        s_view = 9;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        render_trend_scale_btns();
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "TREND  y = (f-f0)/f0",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
        ui_card_t card = {.rect = {DG_LX, 58, 764, 346},
                          .header_label = "Frakcni odchylka v case"};
        ui_card_render_chrome(&card);
    }
    if (first || screen_main_stats_version() != s_trend_key) {
        s_trend_key = screen_main_stats_version();
        screen_main_render_trend_big((prim_rect_t){(int16_t)(DG_LX + 8), 96, (int16_t)(764 - 16), 300});
        present_now();
    }
}

/* ── Okno "O pristroji" (s_view=10): z Nastaveni. FW verze/build, autori, uptime,
 * selftest, (sériové cislo pozdeji z CALIB store). Staticke + uptime tick. */
void app_gpsdo_render_about(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();
    int first = (s_view != 10);
    if (first) {
        s_view = 10;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "O PRISTROJI",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
        ui_card_t c1 = {.rect = {DG_LX, 62, 764, 200}, .header_label = "GPSDO / citac kmitoctu"};
        ui_card_render_chrome(&c1);
        prim_draw_text((prim_point_t){DG_LLBL, 108}, "Firmware:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 108}, FW_VERSION_FULL, &ui_font_mono_18, UI_COLOR_INK, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 140}, "Build:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 140}, __DATE__ " " __TIME__, &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 172}, "Autori:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 172}, "OK2JNJ & OK2HAZ", &ui_font_mono_18, UI_COLOR_ACC, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 204}, "MCU:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 204}, "STM32H757 (CM7+CM4) @ 480 MHz", &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 236}, "Serial:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 160), 236}, "(neprideleno)", &ui_font_mono_16, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);

        ui_card_t c2 = {.rect = {DG_LX, 274, 764, 130}, .header_label = "Stav"};
        ui_card_render_chrome(&c2);
        prim_draw_text((prim_point_t){DG_LLBL, 320}, "Uptime:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 356}, "Selftest:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    }
    /* zive: uptime (1x/s staci) + selftest verdikt */
    static char c_up[20], c_st[16];
    char buf[24];
    uint32_t s = g_uptime_s;
    snprintf(buf, sizeof buf, "%lu:%02lu:%02lu", (unsigned long)(s / 3600u),
             (unsigned long)((s / 60u) % 60u), (unsigned long)(s % 60u));
    if (first || dchg(c_up, sizeof c_up, buf)) {
        dtext((int16_t)(DG_LLBL + 160), 320, 300, buf, UI_COLOR_INK, &ui_font_mono_18);
        snprintf(buf, sizeof buf, "%s", g_selftest_res == 1 ? "PASS" : (g_selftest_res == 2 ? "FAIL" : "---"));
        dtext((int16_t)(DG_LLBL + 160), 356, 200, buf,
              g_selftest_res == 1 ? UI_COLOR_OK : (g_selftest_res == 2 ? UI_COLOR_BAD : UI_COLOR_INK_4),
              &ui_font_mono_18);
        (void)c_st;
        present_now();
    }
}

/* Zmena jasu o delta (krok), clamp [25..255] (nikdy uplna tma -> vzdy videt na ovladani).
 * Zapisuje g_brightness; HW aplikaci dela UiTask pod I2C4 mutexem. */
static void brightness_step(int delta)
{
    int v = (int)g_brightness + delta;
    if (v < 25) v = 25;
    if (v > 255) v = 255;
    g_brightness = (uint8_t)v;
    g_sys_cfg_dirty = 1;
}

/* Preset prodlevy auto-dim [s]; -/+ kroci mezi nimi. */
static const uint16_t DIM_PRESETS[] = {15, 30, 60, 120, 300, 600};
#define DIM_PRESET_N ((int)(sizeof(DIM_PRESETS) / sizeof(DIM_PRESETS[0])))

static void autodim_step(int dir)
{
    int i = 0;
    for (int k = 0; k < DIM_PRESET_N; k++) if (DIM_PRESETS[k] <= g_autodim_sec) i = k;
    i += dir;
    if (i < 0) i = 0; else if (i >= DIM_PRESET_N) i = DIM_PRESET_N - 1;
    g_autodim_sec = DIM_PRESETS[i];
    g_sys_cfg_dirty = 1;
}

/* ── Partial updaty ovladacu Nastaveni ──────────────────────────────────────
 * Tap na +/- apod. NEprekresluje cele okno (bg blit + 5 karet ~40-80 ms =
 * citelna latence), jen dotceny ovladac (~jednotky ms). Kazdy helper si vycisti
 * svou oblast (BG_CARD) POD hlavickou karty a prekresli button/hodnotu. */
static void settings_upd_mute(void)
{
    prim_fill_rect((prim_rect_t){28, 88, 184, 52}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    bool muted = g_sound_muted;
    if (muted) ui_icon_speaker_muted((prim_point_t){32, 84}, 40, UI_COLOR_BAD);
    else       ui_icon_speaker((prim_point_t){32, 84}, 40, UI_COLOR_OK);
    prim_draw_text((prim_point_t){88, 112}, muted ? "vypnut" : "zapnut",
                   &ui_font_sans_18, muted ? UI_COLOR_BAD : UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    ui_button_t mb = {.rect = MUTE_RECT, .variant = UI_BUTTON_NORMAL,
                      .label = muted ? "ZAPNOUT" : "VYPNOUT"};
    ui_button_render(&mb);
}

static void settings_upd_jas(void)
{
    prim_fill_rect((prim_rect_t){194, 196, 188, 42}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_rect_t track = {196, 200, 118, 32};
    prim_fill_rect(track, UI_COLOR_BG_0, PRIM_BLEND_REPLACE);
    int16_t fillw = (int16_t)((int32_t)track.w * g_brightness / 255);
    if (fillw > 0)
        prim_fill_rect((prim_rect_t){track.x, track.y, fillw, track.h}, UI_COLOR_ACC, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(track, 2, 1, UI_COLOR_LINE);
    char pb[8]; snprintf(pb, sizeof pb, "%d%%", (int)g_brightness * 100 / 255);
    prim_draw_text((prim_point_t){324, 224}, pb, &ui_font_mono_22, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
}

static void settings_upd_dim(void)
{
    ui_button_t adb = {.rect = ADEN_RECT, .variant = UI_BUTTON_NORMAL,
                       .label = g_autodim_en ? "ZAPNUTO" : "VYPNUTO"};
    ui_button_render(&adb);
    prim_fill_rect((prim_rect_t){244, 306, 72, 38}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    char tb[12]; snprintf(tb, sizeof tb, "%u s", (unsigned)g_autodim_sec);
    prim_draw_text((prim_point_t){279, 336}, tb, &ui_font_mono_22,
                   g_autodim_en ? UI_COLOR_INK_2 : UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
}

static void settings_upd_lang(void)
{
    ui_button_t lb = {.rect = LANG_RECT, .variant = UI_BUTTON_NORMAL,
                      .label = g_lang_en ? "ENGLISH" : "CESKY"};
    ui_button_render(&lb);
}

/* ── Okno Nastaveni (s_view=7): otevre se z System Health -> "NASTAVENI".
 * DVOUSLOUPCOVE (jako diag): levy = Zvuk / Jas / Auto-dim, pravy = Vzhled
 * (tmave/svetle schema, runtime prepnuti palety) / Jazyk (infrastruktura;
 * texty se prepinaji postupne). Staticke (neni v ticku), prekresli se cele
 * pri tapu. Zapisuje g_* + dirty pro BKP persist (DR2 + DR6). */
void app_gpsdo_render_settings(void)
{
    app_gpsdo_init();
    s_view = 7;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
              screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
    ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
    ui_button_render(&back);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 34}, "NASTAVENI",
                   &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);

    /* ── Levy sloupec: Zvuk ── */
    ui_card_t c1 = {.rect = {DG_LX, 58, DG_COLW, 88},
                    .header_label = "Zvuk (alarmy)"};
    ui_card_render_chrome(&c1);
    settings_upd_mute();

    /* ── Levy sloupec: Jas ── */
    ui_card_t c2 = {.rect = {DG_LX, 156, DG_COLW, 100}, .header_label = "Jas displeje"};
    ui_card_render_chrome(&c2);
    ui_button_t bmin = {.rect = BR_MINUS, .variant = UI_BUTTON_NORMAL, .label = "-"};
    ui_button_t bplus = {.rect = BR_PLUS, .variant = UI_BUTTON_NORMAL, .label = "+"};
    ui_button_render(&bmin);
    ui_button_render(&bplus);
    settings_upd_jas();

    /* ── Levy sloupec: Auto-dim (zap/vyp + prodleva -/+) ── */
    ui_card_t c3 = {.rect = {DG_LX, 266, DG_COLW, 102},
                    .header_label = "Auto-dim (hodiny po necinnosti)"};
    ui_card_render_chrome(&c3);
    ui_button_t dmin = {.rect = DIM_MINUS, .variant = UI_BUTTON_NORMAL, .label = "-"};
    ui_button_t dplus = {.rect = DIM_PLUS, .variant = UI_BUTTON_NORMAL, .label = "+"};
    ui_button_render(&dmin);
    ui_button_render(&dplus);
    settings_upd_dim();

    /* ── Pravy sloupec: Vzhled (barevne schema) ── */
    ui_card_t c4 = {.rect = {DG_RX, 58, DG_COLW, 88}, .header_label = "Vzhled"};
    ui_card_render_chrome(&c4);
    prim_draw_text((prim_point_t){(int16_t)(DG_RX + 14), 112}, "Schema:",
                   &ui_font_sans_18, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    ui_button_t thb = {.rect = THEME_RECT, .variant = UI_BUTTON_NORMAL,
                       .label = g_theme_light ? "SVETLE" : "TMAVE"};
    ui_button_render(&thb);

    /* ── Pravy sloupec: Jazyk ── */
    ui_card_t c5 = {.rect = {DG_RX, 156, DG_COLW, 82}, .header_label = "Jazyk / Language"};
    ui_card_render_chrome(&c5);
    settings_upd_lang();

    /* ── O pristroji (tlacitko dolni pravy) ── */
    ui_button_t ab = {.rect = ABOUT_RECT, .variant = UI_BUTTON_NORMAL, .label = "O PRISTROJI >"};
    ui_button_render(&ab);

    present_now();
}

/* ── Screensaver hodiny (s_view=8): pri auto-dim misto ztlumene obrazovky velke
 * RTC hodiny na CERNEM pozadi (temer zhasnute pixely + jas 20/255 = setri panel).
 * Pozice se posouva s minutou (anti burn-in styl). Exit obnovi predchozi okno. */
static uint8_t s_prev_view = 0;
static char    s_saver_hms[10] = "";        /* zmenovy klic (prekresli 1x/s) */
static prim_rect_t s_saver_rect = {0,0,0,0}; /* minule kreslena oblast (k smazani) */

static void saver_draw(void)
{
    prim_set_target(&s_fb);
    prim_reset_clip();
    char rt[24];
    strncpy(rt, (const char *)g_rtc_text, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
    /* "YYYY-MM-DD HH:MM:SS" -> datum [0..9], cas [11..18] */
    if (strlen(rt) < 19) return;
    if (strncmp(s_saver_hms, rt + 11, 8) == 0) return;   /* stejna sekunda */
    memcpy(s_saver_hms, rt + 11, 8); s_saver_hms[8] = '\0';

    /* drift pozice: kazdou minutu jinde (male kruzeni ±24 px) */
    int mi = (rt[14] - '0') * 10 + (rt[15] - '0');
    int16_t ox = (int16_t)(((mi % 5) - 2) * 12);
    int16_t oy = (int16_t)((((mi / 5) % 5) - 2) * 10);
    int16_t cx = (int16_t)(400 + ox), by = (int16_t)(250 + oy);   /* stred / baseline */

    /* smaz minulou oblast, pak spocitej novou (cas + datum pod nim).
     * Font hodin = ui_font_mono_75 (stejny jako headline kmitoctu na main). */
    if (s_saver_rect.w) prim_fill_rect(s_saver_rect, PRIM_RGB(0,0,0), PRIM_BLEND_REPLACE);
    char hh[3] = {rt[11], rt[12], 0}, mm[3] = {rt[14], rt[15], 0}, ss[3] = {rt[17], rt[18], 0};
    int16_t dw = prim_text_width("00", &ui_font_mono_75);
    int16_t colw = 28;                                   /* mezera na dvojtecku */
    int16_t total = (int16_t)(3 * dw + 2 * colw);
    int16_t x = (int16_t)(cx - total / 2);
    s_saver_rect = (prim_rect_t){(int16_t)(x - 4), (int16_t)(by - 82),
                                 (int16_t)(total + 8), 146};
    prim_draw_text((prim_point_t){x, by}, hh, &ui_font_mono_75, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(x + dw + colw), by}, mm,
                   &ui_font_mono_75, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(x + 2 * (dw + colw)), by}, ss,
                   &ui_font_mono_75, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    /* dvojtecky rucne (mono_75 ma jen cislice) */
    for (int c = 0; c < 2; c++) {
        int16_t dx = (int16_t)(x + dw * (c + 1) + colw * c + colw / 2 - 5);
        prim_fill_rect((prim_rect_t){dx, (int16_t)(by - 50), 10, 10}, UI_COLOR_INK_3, PRIM_BLEND_OVER);
        prim_fill_rect((prim_rect_t){dx, (int16_t)(by - 24), 10, 10}, UI_COLOR_INK_3, PRIM_BLEND_OVER);
    }
    rt[10] = '\0';                                       /* datum */
    prim_draw_text((prim_point_t){cx, (int16_t)(by + 40)}, rt,
                   &ui_font_mono_18, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
    present_now();
}

void app_gpsdo_enter_screensaver(void)
{
    app_gpsdo_init();
    if (s_view == 8) return;
    s_prev_view = s_view;
    s_view = 8;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_fill_rect((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                   PRIM_RGB(0, 0, 0), PRIM_BLEND_REPLACE);
    s_saver_hms[0] = '\0';
    s_saver_rect = (prim_rect_t){0, 0, 0, 0};
    saver_draw();
    present_now();   /* i kdyby RTC text jeste nebyl platny (saver_draw return) */
}

void app_gpsdo_exit_screensaver(void)
{
    if (s_view != 8) return;
    switch (s_prev_view) {                 /* obnov okno, ktere bylo pred usnutim */
    case 1:  app_gpsdo_render_diag();      break;
    case 2:  app_gpsdo_render_gps();       break;
    case 3:  app_gpsdo_render_health();    break;
    case 4:  app_gpsdo_render_sensors();   break;
    case 5:  app_gpsdo_render_mem();       break;
    case 6:  app_gpsdo_render_histogram(); break;
    case 7:  app_gpsdo_render_settings();  break;
    case 9:  app_gpsdo_render_trend();     break;
    case 10: app_gpsdo_render_about();     break;
    case 12: app_gpsdo_render_menu();      break;
    default: app_gpsdo_render_main();      break;
    }
}

/* ── Boot splash: logo + FW/build + prubeh selftestu. Cerne pozadi, velky
 * nazev + akcentni linka; radek "Selftest" se prekresluje z g_selftest_res. */
static void splash_status(void)   /* prekresli JEN status radek (selftest) */
{
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_fill_rect((prim_rect_t){0, 300, UI_DIM_SCREEN_W, 40}, PRIM_RGB(0, 0, 0), PRIM_BLEND_REPLACE);
    const char *st; prim_color_t sc;
    if      (g_selftest_res == 1) { st = "Selftest: PASS";  sc = UI_COLOR_OK; }
    else if (g_selftest_res == 2) { st = "Selftest: FAIL";  sc = UI_COLOR_BAD; }
    else                          { st = "Selftest ...";    sc = UI_COLOR_INK_3; }
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 326}, st,
                   &ui_font_mono_18, sc, PRIM_ALIGN_CENTER);
    present_now();
}

void app_gpsdo_boot_splash(void)
{
    app_gpsdo_init();
    s_view = 11;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_fill_rect((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                   PRIM_RGB(0, 0, 0), PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 180}, "GPSDO",
                   &ui_font_mono_75, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 218}, "citac kmitoctu",
                   &ui_font_sans_18, UI_COLOR_INK_2, PRIM_ALIGN_CENTER);
    prim_draw_line((prim_point_t){260, 240}, (prim_point_t){540, 240}, 2, UI_COLOR_LINE_HI);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 268}, FW_VERSION_FULL "   " __DATE__,
                   &ui_font_mono_16, UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 452}, "OK2JNJ & OK2HAZ",
                   &ui_font_mono_16, UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
    splash_status();
}

void app_gpsdo_boot_splash_tick(void)
{
    if (s_view == 11) splash_status();
}

/* ── Menu (rozcestnik, s_view=12): z hlavni obrazovky tlacitkem MENU. Mrizka 3×4.
 * Obsahuje SYSTEM/NASTROJE (kontextova okna GPS/Histogram/Trend jsou dostupna
 * primo z hl. obrazovky pres pilulku/tap, NEjsou tu). Staticke (neni v ticku). */
extern volatile uint8_t g_reboot_req;
static void app_gpsdo_render_reference(void);       /* fwd (volano z menu_activate) */
static void app_gpsdo_render_kalib(void);
static void app_gpsdo_render_holdover(void);
static void app_gpsdo_render_datalog(void);
static void app_gpsdo_render_alarms(void);
static void app_gpsdo_render_confirm_restart(void);
enum { ACT_DIAG = 1, ACT_SETTINGS, ACT_HEALTH, ACT_SENZORY, ACT_PAMET,
       ACT_REFERENCE, ACT_KALIB, ACT_HOLDOVER, ACT_DATALOG, ACT_ALARMS,
       ACT_ABOUT, ACT_RESTART };
/* Menu 3×4 = 12 dlazdic. w=248, gap 14 (x=24/286/548); h=74, gap 12 (y=72/158/244/330). */
#define MENU_N 12
static const struct { prim_rect_t rect; const char *label; uint8_t act; } MENU_ITEMS[MENU_N] = {
    { {24,  72, 248, 74}, "Diagnostika",   ACT_DIAG },
    { {286, 72, 248, 74}, "Nastaveni",     ACT_SETTINGS },
    { {548, 72, 248, 74}, "System Health", ACT_HEALTH },
    { {24, 158, 248, 74}, "Senzory",       ACT_SENZORY },
    { {286,158, 248, 74}, "Pamet",         ACT_PAMET },
    { {548,158, 248, 74}, "Reference",     ACT_REFERENCE },
    { {24, 244, 248, 74}, "Holdover",      ACT_HOLDOVER },
    { {286,244, 248, 74}, "Datalog",       ACT_DATALOG },
    { {548,244, 248, 74}, "Alarmy",        ACT_ALARMS },
    { {24, 330, 248, 74}, "Kalibrace",     ACT_KALIB },
    { {286,330, 248, 74}, "O pristroji",   ACT_ABOUT },
    { {548,330, 248, 74}, "Restart",       ACT_RESTART },
};

static void menu_activate(uint8_t act)
{
    switch (act) {
    case ACT_DIAG:      app_gpsdo_render_diag();      break;
    case ACT_SETTINGS:  app_gpsdo_render_settings();  break;
    case ACT_HEALTH:    app_gpsdo_render_health();    break;
    case ACT_SENZORY:   app_gpsdo_render_sensors();   break;
    case ACT_PAMET:     app_gpsdo_render_mem();        break;
    case ACT_REFERENCE: app_gpsdo_render_reference(); break;
    case ACT_KALIB:     app_gpsdo_render_kalib();     break;
    case ACT_HOLDOVER:  app_gpsdo_render_holdover();  break;
    case ACT_DATALOG:   app_gpsdo_render_datalog();   break;
    case ACT_ALARMS:    app_gpsdo_render_alarms();    break;
    case ACT_ABOUT:     app_gpsdo_render_about();     break;
    default: break;   /* ACT_RESTART resi confirm okno (s_view=13), ne zde */
    }
}

void app_gpsdo_render_menu(void)
{
    app_gpsdo_init();
    s_view = 12;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
              screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
    ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
    ui_button_render(&back);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "MENU",
                   &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
    for (int i = 0; i < MENU_N; i++) {
        ui_button_t b = {.rect = MENU_ITEMS[i].rect, .label = MENU_ITEMS[i].label,
                         .variant = (MENU_ITEMS[i].act == ACT_RESTART) ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL};
        ui_button_render(&b);
    }
    present_now();
}

/* ── Potvrzeni restartu (s_view=13): modalni box "Opravdu restartovat?" Ano/Ne. ── */
static const prim_rect_t CONFIRM_NO  = {230, 250, 150, 64};
static const prim_rect_t CONFIRM_YES = {420, 250, 150, 64};
static void app_gpsdo_render_confirm_restart(void)
{
    app_gpsdo_init();
    s_view = 13;
    prim_set_target(&s_fb);
    prim_reset_clip();
    /* ztlumene pozadi (ponech menu) + centralni box */
    prim_fill_rect((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                   PRIM_RGB(0, 0, 0), PRIM_BLEND_REPLACE);
    prim_fill_rect_rounded((prim_rect_t){190, 150, 420, 200}, UI_DIM_CARD_RADIUS,
                           UI_COLOR_BG_CARD, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded((prim_rect_t){190, 150, 420, 200}, UI_DIM_CARD_RADIUS, 1, UI_COLOR_WARN);
    prim_draw_text((prim_point_t){400, 210}, "Opravdu restartovat?",
                   &ui_font_mono_25, UI_COLOR_INK, PRIM_ALIGN_CENTER);
    ui_button_t no  = {.rect = CONFIRM_NO,  .variant = UI_BUTTON_NORMAL, .label = "NE"};
    ui_button_t yes = {.rect = CONFIRM_YES, .variant = UI_BUTTON_ACTIVE, .label = "ANO"};
    ui_button_render(&no);
    ui_button_render(&yes);
    present_now();
}

/* ── Reference (s_view=14): stav Si5356 + konfigurace 4×100 MHz vernier hodin. ── */
static void app_gpsdo_render_reference(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();
    int first = (s_view != 14);
    static char c_lock[24];
    if (first) {
        s_view = 14;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "REFERENCE  Si5356",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
        ui_card_t c = {.rect = {DG_LX, 62, 764, 300}, .header_label = "Vernier reference (4-fazovy TDC)"};
        ui_card_render_chrome(&c);
        prim_draw_text((prim_point_t){DG_LLBL, 112}, "Vstup:",  &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL+150), 112}, "10 MHz -> VCO 2,2 GHz (N=220) /22", &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 148}, "Vystup:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL+150), 148}, "4x 100 MHz, faze 0/90/180/270 (2,5 ns)", &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 184}, "Pouziti:",&ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL+150), 184}, "reciproky citac FPGA, jemny krok 2,5 ns", &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 240}, "Stav (reg 218):", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 300}, "Presnost = ppm vstupnich 10 MHz (Si5356).", &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
        c_lock[0] = '\0';
    }
    /* zivy lock status */
    const char *st; prim_color_t sc;
    if      (!g_si5356_ok)                        { st = "N/A (I2C)";   sc = UI_COLOR_INK_3; }
    else if (g_si5356_status & SI5356_LOS_CLKIN)  { st = "LOS CLKIN!";  sc = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_PLL_LOL)    { st = "PLL UNLOCK!"; sc = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_SYS_CAL)    { st = "CALIB...";    sc = UI_COLOR_VIOLET; }
    else                                          { st = "LOCK OK";     sc = UI_COLOR_OK; }
    if (first || dchg(c_lock, sizeof c_lock, st))
        { dtext((int16_t)(DG_LLBL + 200), 240, 300, st, sc, &ui_font_mono_18); present_now(); }
}

/* ── Kalibrace (s_view=15): aktualni kalibr. konstanty (read-only; editace -> CALIB store TODO). ── */
static void app_gpsdo_render_kalib(void)
{
    app_gpsdo_init();
    s_view = 15;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
              screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
    ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
    ui_button_render(&back);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "KALIBRACE",
                   &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
    ui_card_t c = {.rect = {DG_LX, 62, 764, 300}, .header_label = "Kalibracni konstanty (read-only)"};
    ui_card_render_chrome(&c);
    struct { const char *k, *v; } rows[] = {
        { "AD8307 slope",     "25,0 mV/dB" },
        { "AD8307 intercept", "-84,0 dBm" },
        { "ADS 12V delic",    "x13417/2814 (13,417 V @ 2,814 V)" },
        { "ADS 5V delic",     "x4978/2526 (4,978 V @ 2,526 V)" },
        { "ADC3 VREF",        "VREFINT_CAL x 3300 / data (16-bit)" },
        { "TDC jemny krok",   "2,5 ns (Si5356 90 faze)" },
    };
    for (int i = 0; i < 6; i++) {
        int16_t yy = (int16_t)(104 + i * 40);
        prim_draw_text((prim_point_t){DG_LLBL, yy}, rows[i].k, &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 230), yy}, rows[i].v, &ui_font_mono_16, UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    }
    prim_draw_text((prim_point_t){DG_LLBL, 348}, "Editace + ulozeni do CALIB store (W25Q) = TODO.",
                   &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    present_now();
}

/* Maly radek "label: hodnota" v kartach novych oken. */
static void kv_row(int16_t y, const char *k, const char *v, prim_color_t vc)
{
    prim_draw_text((prim_point_t){DG_LLBL, y}, k, &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 250), y}, v, &ui_font_mono_16, vc, PRIM_ALIGN_LEFT);
}

/* ── Holdover (s_view=16): stav disciplinace GPSDO (WARMUP/LOCK/HOLDOVER) z GPS
 * fixu + FPGA linku + timepulse. Zive (uptime tik). Zdroj OCXO teploty = 0x49. ── */
static void app_gpsdo_render_holdover(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();
    int first = (s_view != 16);
    static char c_st[16], c_gps[24], c_tp[16], c_ocxo[16], c_since[20];
    static uint32_t s_state_since;
    static int s_last_state = -1;
    if (first) {
        s_view = 16;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "HOLDOVER",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
        ui_card_t c = {.rect = {DG_LX, 62, 764, 300}, .header_label = "Stav disciplinace GPSDO"};
        ui_card_render_chrome(&c);
        prim_draw_text((prim_point_t){DG_LLBL, 116}, "Rezim:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 344},
                       "WARMUP=nabeh OCXO  LOCK=disc. z GNSS  HOLDOVER=drzi VC bez fixu",
                       &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
        c_st[0] = c_gps[0] = c_tp[0] = c_ocxo[0] = c_since[0] = '\0';
        s_last_state = -1;
    }
    gps_data_t g; gps_get(&g);
    /* stav: WARMUP (boot < 180 s a jeste nezamknuto) -> LOCK (fix+link) -> HOLDOVER (ztrata po locku) */
    int st; const char *sl; prim_color_t sc;
    int lock = (g.valid && g_spi_ok);
    if (lock)                              { st = 1; sl = "LOCK";     sc = UI_COLOR_OK; }
    else if (g.fixes > 0)                  { st = 2; sl = "HOLDOVER"; sc = UI_COLOR_WARN; }
    else if (g_uptime_s < 180u)            { st = 0; sl = "WARMUP";   sc = UI_COLOR_VIOLET; }
    else                                   { st = 3; sl = "NO LOCK";  sc = UI_COLOR_BAD; }
    if (st != s_last_state) { s_last_state = st; s_state_since = g_uptime_s; }

    if (first || dchg(c_st, sizeof c_st, sl)) {
        prim_fill_rect((prim_rect_t){(int16_t)(DG_LLBL + 120), 92, 260, 34}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_draw_text((prim_point_t){(int16_t)(DG_LLBL + 120), 118}, sl, &ui_font_mono_25, sc, PRIM_ALIGN_LEFT);
    }
    char b[24];
    snprintf(b, sizeof b, "%s", g.valid ? (g.fix_mode == 3 ? "3D fix" : "2D fix") : (g.fixes > 0 ? "ztracen" : "zadny"));
    if (first || dchg(c_gps, sizeof c_gps, b)) kv_row(160, "GPS lock:", b, g.valid ? UI_COLOR_OK : UI_COLOR_INK_2);
    snprintf(b, sizeof b, "%s", g.fix_quality ? "100 kHz (disc.)" : "10 Hz (hold)");
    if (first || dchg(c_tp, sizeof c_tp, b)) kv_row(196, "Timepulse:", b, g.fix_quality ? UI_COLOR_OK : UI_COLOR_WARN);
    { const sensor_stat_t *o = &g_sensors[SENS_T49];
      if (o->valid) snprintf(b, sizeof b, "%ld,%01ld C", (long)o->last, (long)((o->last - (long)o->last) * 10)); else snprintf(b, sizeof b, "--");
      if (first || dchg(c_ocxo, sizeof c_ocxo, b)) kv_row(232, "OCXO tepl.:", b, UI_COLOR_INK_2); }
    snprintf(b, sizeof b, "%lu s", (unsigned long)(g_uptime_s - s_state_since));
    if (first || dchg(c_since, sizeof c_since, b)) kv_row(268, "V rezimu:", b, UI_COLOR_INK_2);
    present_now();
}

/* ── Datalog (s_view=17): stav logovani do W25Q DATA regionu. Zatim neaktivni
 * (roadmap [[w25q-flash]]) — okno je vstupni bod, ukazuje kapacitu + JEDEC. ── */
static void app_gpsdo_render_datalog(void)
{
    app_gpsdo_init();
    s_view = 17;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
              screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
    ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
    ui_button_render(&back);
    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "DATALOG",
                   &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
    ui_card_t c = {.rect = {DG_LX, 62, 764, 300}, .header_label = "Zaznam mereni do W25Q (DATA region)"};
    ui_card_render_chrome(&c);
    char b[32];
    kv_row(116, "Stav:",     "NEAKTIVNI (planovano)", UI_COLOR_WARN);
    snprintf(b, sizeof b, "0x%06lX", (unsigned long)W25Q_DATA_BASE);
    kv_row(152, "DATA base:", b, UI_COLOR_INK_2);
    snprintf(b, sizeof b, "%lu MB", (unsigned long)(W25Q_DATA_SIZE / (1024u * 1024u)));
    kv_row(188, "Kapacita:", b, UI_COLOR_INK_2);
    kv_row(224, "Zaznam:",   "~32 B / 10 s -> ~600 dni", UI_COLOR_INK_2);
    uint32_t id = w25q_read_jedec();
    snprintf(b, sizeof b, "%06lX %s", (unsigned long)id, id == W25Q_JEDEC_ID ? "OK" : "--");
    kv_row(260, "Flash ID:", b, id == W25Q_JEDEC_ID ? UI_COLOR_OK : UI_COLOR_BAD);
    prim_draw_text((prim_point_t){DG_LLBL, 344},
                   "Append-only log f/teplot/locku; rekonstrukce Allanu po bootu.",
                   &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
    present_now();
}

/* ── Alarmy (s_view=18): monitor alarmovych udalosti (co je hlidano + pocitadla).
 * Doplnuje Nastaveni (jen globalni mute) o PREHLED co spousti alarm + historii. ── */
static void app_gpsdo_render_alarms(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();
    int first = (s_view != 18);
    static char c_mute[12], c_f[12], c_g[12];
    if (first) {
        s_view = 18;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL, .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "ALARMY",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);
        ui_card_t c = {.rect = {DG_LX, 62, 764, 300}, .header_label = "Zvukove alarmy (beeper) — co je hlidano"};
        ui_card_render_chrome(&c);
        kv_row(116, "FPGA SIGNAL_LOST:", "hlidano (3x pip)", UI_COLOR_INK_2);
        kv_row(152, "Ztrata GPS locku:", "hlidano (2x pip)", UI_COLOR_INK_2);
        kv_row(188, "Frekv. limit:",     "TODO (Faze 0)",    UI_COLOR_INK_4);
        prim_draw_text((prim_point_t){DG_LLBL, 260}, "Udalosti od startu:", &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        prim_draw_text((prim_point_t){DG_LLBL, 348},
                       "Vypnuti zvuku globalne v Nastaveni; mute plati i pro alarmy.",
                       &ui_font_sans_16, UI_COLOR_INK_4, PRIM_ALIGN_LEFT);
        c_mute[0] = c_f[0] = c_g[0] = '\0';
    }
    char b[12];
    snprintf(b, sizeof b, "%s", g_sound_muted ? "MUTE" : "zapnut");
    if (first || dchg(c_mute, sizeof c_mute, b)) kv_row(224, "Zvuk:", b, g_sound_muted ? UI_COLOR_BAD : UI_COLOR_OK);
    snprintf(b, sizeof b, "%u", g_alarm_fpga_lost);
    if (first || dchg(c_f, sizeof c_f, b)) kv_row(292, "  FPGA ztrat:", b, g_alarm_fpga_lost ? UI_COLOR_WARN : UI_COLOR_INK_2);
    snprintf(b, sizeof b, "%u", g_alarm_gps_lost);
    if (first || dchg(c_g, sizeof c_g, b)) kv_row(320, "  GPS ztrat:", b, g_alarm_gps_lost ? UI_COLOR_WARN : UI_COLOR_INK_2);
    present_now();
}

void app_gpsdo_clear(void)
{
    app_gpsdo_init();
    s_view = 0;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_fill_rect((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                   UI_COLOR_BG_0, PRIM_BLEND_REPLACE);
    present_now();
}

void app_gpsdo_tick(void)
{
    if (s_view == 1) app_gpsdo_render_diag();     /* live refresh of diagnostics */
    else if (s_view == 2) app_gpsdo_render_gps();     /* live refresh of GPS/GNSS okna */
    else if (s_view == 3) app_gpsdo_render_health();  /* live refresh of system health */
    else if (s_view == 4) app_gpsdo_render_sensors(); /* live refresh of senzory podmenu */
    else if (s_view == 5) app_gpsdo_render_mem();     /* live refresh (RTOS heap) okna PAMET */
    else if (s_view == 6) app_gpsdo_render_histogram(); /* live/snapshot histogram mereni */
    else if (s_view == 8) saver_draw();               /* screensaver hodiny (1x/s dle RTC) */
    else if (s_view == 9) app_gpsdo_render_trend();   /* fullscreen trend (zivy) */
    else if (s_view == 10) app_gpsdo_render_about();  /* O pristroji (uptime tick) */
    else if (s_view == 14) app_gpsdo_render_reference(); /* Reference (zivy Si5356 lock) */
    else if (s_view == 16) app_gpsdo_render_holdover();  /* Holdover (zivy stav) */
    else if (s_view == 18) app_gpsdo_render_alarms();    /* Alarmy (zivy mute + pocitadla) */
}

/* Hodinovy tik (~kazdych 100 ms): na hlavni obrazovce prekresli cas/datum z GPS
 * a (pri zmene sat/fix) horni listu (GNSS lock + pocet druzic). */
void app_gpsdo_tick_clock(uint32_t ms_since_boot)
{
    if (s_view != 0) return;
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_time(ms_since_boot)) s_dirty = 1;   /* flip odlozen na flush */

    /* Horni lista (GNSS lock + druzice + HDOP) jen pri ZMENE GPS stavu — sat/fix/HDOP
     * se meni pomalu (~1 Hz z GGA), takze redraw headeru bezi vzacne (ne kazdy tik). */
    static int last_sat = -1, last_fixq = -1, last_hdop10 = -1;
    gps_data_t g;
    gps_get(&g);
    int hdop10 = (int)(g.hdop * 10.0f + 0.5f);   /* HDOP na 1 des. misto -> change-detect */
    if ((int)g.num_sat != last_sat || (int)g.fix_quality != last_fixq || hdop10 != last_hdop10
        || screen_main_sys_poll()) {   /* + zmena agregovaneho SYS zdravi -> prebarvi pilulku */
        last_sat = (int)g.num_sat;
        last_fixq = (int)g.fix_quality;
        last_hdop10 = hdop10;
        if (screen_main_redraw_header()) s_dirty = 1;
    }
}

/* RF vstupni vykon z AD8307 log-detektoru: ADS1115 AIN1 (SensorsTask fast-path
 * ~10 Hz -> g_sensors[SENS_ADS1], mV). Volat 10x/s z UiTasku, jen na hlavni
 * obrazovce. Flip jen pri zmene. AD8307: Vout ~ log(Pin), slope ~25 mV/dB,
 * intercept ~-84 dBm -> dBm = mV/slope + intercept. Bargraf mapuje pasmo
 * RF_DBM_MIN..MAX. ⚠️ slope/intercept jsou typicke z datasheetu — presna
 * KALIBRACE (offset+strmost dle desky) prijde do CALIB store. */
#define AD8307_SLOPE_MV_DB    25.0f     /* mV / dB */
#define AD8307_INTERCEPT_DBM  (-84.0f)  /* dBm pri 0 V */
#define RF_DBM_MIN            (-80)      /* spodek bargrafu */
#define RF_DBM_MAX            (10)       /* vrch bargrafu (AD8307 zvlada az ~+17) */
void app_gpsdo_tick_signal(void)
{
    if (s_view != 0) return;             /* RF level je zivy HW udaj (bez RUN gate) */
    const sensor_stat_t *rf = &g_sensors[SENS_ADS1];
    if (rf->samples == 0) return;        /* jeste zadne mereni */
    float mv = rf->last; if (mv < 0.0f) mv = 0.0f;
    float dbm = mv / AD8307_SLOPE_MV_DB + AD8307_INTERCEPT_DBM;
    int32_t dbm10 = (int32_t)lround_f(dbm * 10.0f);
    int16_t pct = (int16_t)((dbm - (float)RF_DBM_MIN) * 100.0f / (float)(RF_DBM_MAX - RF_DBM_MIN));
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;

    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_signal(pct, dbm10)) s_dirty = 1;   /* flip odlozen na flush */
}

/* Simulace kmitoctu (~20x/s, jen hlavni obrazovka): per-segment dirty redraw. */
void app_gpsdo_tick_freq(void)
{
    if (!screen_main_is_running()) return;    /* STOP -> cislo i statistika zamrznou */
    if (s_view != 0) {
        /* Mimo hlavni obrazovku (okno/screensaver): simulace bezi dal (jinak by
         * ADEV pyramida nikdy nedosahla dlouhych tau — vzorkuje se z s_freq_n),
         * jen se nekresli. */
        screen_main_freq_sim_step();
        return;
    }
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_freq()) s_dirty = 1;   /* flip odlozen na flush */
}

/* GPSDO statistika (jen hlavni obrazovka, jen RUN): vzorkovani frakcni odchylky (~1x/s). */
void app_gpsdo_tick_stats_sample(void)
{
    /* Vzorkuje se VZDY kdyz mereni bezi — nezavisle na zobrazenem okne (drive
     * jen na main -> Allan/histogram se zastavily pri screensaveru/oknech a
     * nikdy nedosahly dlouhych tau). Kresleni je gatovane zvlast (draw ticky). */
    if (!screen_main_is_running()) return;   /* STOP -> trend/Allan zamrznou */
    screen_main_stats_sample();
}

/* GPSDO statistika: zive prekresleni trend + offset/sigma (~1x/s, jen RUN). */
void app_gpsdo_tick_stats_draw(void)
{
    if (s_view != 0 || !screen_main_is_running()) return;
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_stats()) s_dirty = 1;   /* flip odlozen na flush */
}

/* GPSDO statistika: zive prekresleni Allan grafu (~1x/s, tezsi render, jen RUN). */
void app_gpsdo_tick_allan_draw(void)
{
    if (s_view != 0 || !screen_main_is_running()) return;
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_allan()) s_dirty = 1;
}

/* Present coalescing: jeden flip pro vsechny nahromadene zmeny (clock/signal/freq).
 * Vola UiTask na ~30Hz gate. Vrati 1 pokud flipnul. */
int app_gpsdo_flush(void)
{
    if (!s_dirty) return 0;
    present_now();
    return 1;
}

bool app_gpsdo_handle_touch(int16_t x, int16_t y)
{
    if (s_view == 0) {
        if (screen_main_hit_gnss(x, y)) { nav_push(0); app_gpsdo_render_gps(); return true; }   /* GNSS pill */
        if (screen_main_hit_sys(x, y))  { nav_push(0); app_gpsdo_render_health(); return true; }  /* SYS pill */
        if (screen_main_hit_allan(x, y)) { nav_push(0); app_gpsdo_render_histogram(); return true; }  /* Allan -> histogram */
        if (screen_main_hit_trend(x, y)) { nav_push(0); app_gpsdo_render_trend(); return true; }      /* trend -> fullscreen */
        int b = screen_main_hit_button(x, y);
        if (b == 4) { nav_push(0); app_gpsdo_render_menu(); return true; }   /* MENU -> rozcestnik */
        if (b >= 0) {                                /* PERIOD/RUN/GATE/CHAN */
            screen_main_button_action(b);
            prim_set_target(&s_fb);
            prim_reset_clip();
            screen_main_redraw_button(b);            /* only the pressed button */
            if (b != 1) screen_main_redraw_title();  /* RUN doesn't change the title */
            present_now();
            return true;
        }
    } else {
        /* System Health -> tap na "SENZORY" / "PAMET" / "NASTAVENI" otevre podokno. */
        if (s_view == 3 && in_rect(x, y, SENS_BTN_RECT)) {
            nav_push(3); app_gpsdo_render_sensors();
            return true;
        }
        if (s_view == 3 && in_rect(x, y, MEM_BTN_RECT)) {
            nav_push(3); app_gpsdo_render_mem();
            return true;
        }
        if (s_view == 3 && in_rect(x, y, SET_BTN_RECT)) {   /* Health -> Nastaveni */
            nav_push(3); app_gpsdo_render_settings();
            return true;
        }
        if (s_view == 7) {                                  /* okno Nastaveni: ovladace */
            /* Partial updaty (latence): prekresli se JEN dotceny ovladac + flip.
             * Cele okno se kresli jen pri vstupu a pri zmene schematu. */
            #define SETTINGS_UPD(fn) do { prim_set_target(&s_fb); prim_reset_clip(); \
                                          fn(); present_now(); } while (0)
            if (in_rect(x, y, MUTE_RECT)) {
                g_sound_muted = g_sound_muted ? 0 : 1;
                g_sys_cfg_dirty = 1;
                SETTINGS_UPD(settings_upd_mute);
                return true;
            }
            if (in_rect(x, y, BR_MINUS)) { brightness_step(-26); SETTINGS_UPD(settings_upd_jas); return true; }
            if (in_rect(x, y, BR_PLUS))  { brightness_step(+26); SETTINGS_UPD(settings_upd_jas); return true; }
            if (in_rect(x, y, ADEN_RECT)) {
                g_autodim_en = g_autodim_en ? 0 : 1;
                g_sys_cfg_dirty = 1;
                SETTINGS_UPD(settings_upd_dim);
                return true;
            }
            if (in_rect(x, y, DIM_MINUS)) { autodim_step(-1); SETTINGS_UPD(settings_upd_dim); return true; }
            if (in_rect(x, y, DIM_PLUS))  { autodim_step(+1); SETTINGS_UPD(settings_upd_dim); return true; }
            if (in_rect(x, y, THEME_RECT)) {                /* tmave <-> svetle schema */
                g_theme_light = g_theme_light ? 0 : 1;
                g_sys_cfg_dirty = 1;
                ui_theme_select(g_theme_light);
                screen_main_invalidate();                   /* bg_cache je v barvach stareho schematu */
                screen_main_init();                         /* prestavet HNED (settings bg blituje) */
                app_gpsdo_render_settings();
                return true;
            }
            if (in_rect(x, y, LANG_RECT)) {                 /* CZ <-> EN (texty postupne) */
                g_lang_en = g_lang_en ? 0 : 1;
                g_sys_cfg_dirty = 1;
                SETTINGS_UPD(settings_upd_lang);
                return true;
            }
            if (in_rect(x, y, ABOUT_RECT)) { nav_push(7); app_gpsdo_render_about(); return true; }
            #undef SETTINGS_UPD
        }
        if (s_view == 2 && in_rect(x, y, GPS_SAT_RECT)) {  /* GPS: prepni bargraf <-> sky plot */
            s_gps_polar = !s_gps_polar;
            app_gpsdo_render_gps();                        /* change-key prekresli kartu Druzice */
            return true;
        }
        if (s_view == 12) {                                /* Menu rozcestnik: dlazdice */
            for (int i = 0; i < MENU_N; i++)
                if (in_rect(x, y, MENU_ITEMS[i].rect)) {
                    if (MENU_ITEMS[i].act == ACT_RESTART) {
                        app_gpsdo_render_confirm_restart();   /* potvrzeni (bez nav_push) */
                    } else {
                        nav_push(12); menu_activate(MENU_ITEMS[i].act);
                    }
                    return true;
                }
        }
        if (s_view == 13) {                                /* potvrzeni restartu */
            if (in_rect(x, y, CONFIRM_YES)) { g_reboot_req = 1; return true; }   /* Ano -> defaultTask reset */
            if (in_rect(x, y, CONFIRM_NO))  { app_gpsdo_render_menu(); return true; }  /* Ne -> zpet do Menu */
        }
        if (s_view == 9) {                                 /* trend: relativni +/- casove okno */
            int step = 0;
            if (in_rect(x, y, TREND_MINUS)) step = -1;
            else if (in_rect(x, y, TREND_PLUS)) step = +1;
            if (step) {
                trend_secs_step(step);
                prim_set_target(&s_fb); prim_reset_clip();
                render_trend_scale_btns();                 /* prekresli hodnotu okna */
                screen_main_render_trend_big((prim_rect_t){(int16_t)(DG_LX + 8), 96,
                                                           (int16_t)(764 - 16), 300});
                present_now();
                return true;
            }
        }
        if (s_view == 6 && in_rect(x, y, LOGY_RECT)) {     /* histogram: prepni lin/log Y */
            screen_main_hist_toggle_logy();
            app_gpsdo_render_histogram();   /* zmena osy zmeni change-key -> prekresli */
            return true;
        }
        if (in_rect(x, y, BACK_RECT)) { nav_back(); return true; }   /* zpet k tomu, odkud otevreno */
    }
    return false;
}
