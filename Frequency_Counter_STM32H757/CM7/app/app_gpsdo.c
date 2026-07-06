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
#include "cmsis_os2.h"          /* osThreadGetStackSpace (volny stack tasku) */
#include <prim/prim.h>
#include <ui/ui.h>
#include <stdio.h>
#include <string.h>

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

static bool in_rect(int16_t x, int16_t y, prim_rect_t r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void app_gpsdo_init(void)
{
    if (s_inited) return;
    prim_stm32_init(&s_fb);
    screen_main_init();
    s_inited = 1;
}

void app_gpsdo_render_main(void)
{
    app_gpsdo_init();
    s_view = 0;
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

/* Compact "min/max" with 1 decimal from a sensor's stats. */
static void fmt_minmax(char *buf, size_t n, const sensor_stat_t *s)
{
    if (s->samples == 0) { snprintf(buf, n, "--/--"); return; }
    int lo = (int)(s->min * 10.0f + (s->min >= 0 ? 0.5f : -0.5f));
    int hi = (int)(s->max * 10.0f + (s->max >= 0 ? 0.5f : -0.5f));
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
    static char c_tv[3][20], c_tm[3][20], c_adc[4][20], c_mcu[3][20];
    static char c_spi[68], c_fpga[68], c_si[20], c_sys[5][20];
    char buf[24], key[26];
    int drew = force;   /* force -> vse se kresli */

    /* ── Levy sloupec: teploty (hodnota + min/max) ── */
    static const sensor_id_t tid[3] = { SENS_T48, SENS_T49, SENS_T4A };
    static const int16_t     ty[3]  = { 104, 130, 156 };
    for (int i = 0; i < 3; i++) {
        const sensor_stat_t *s = &g_sensors[tid[i]];
        fmt_minmax(buf, sizeof(buf), s);
        if (force || dchg(c_tm[i], sizeof(c_tm[i]), buf)) {
            dtext((int16_t)(DG_LLBL + 96), ty[i], 118, buf, UI_COLOR_INK_3, &ui_font_sans_16); drew = 1; }
        fmt_temp(buf, sizeof(buf), s->last);
        snprintf(key, sizeof(key), "%c%s", s->valid ? 'V' : 'X', buf);  /* vykresleni zalezi i na valid */
        if (force || dchg(c_tv[i], sizeof(c_tv[i]), key)) {
            dval(DG_LVAL, ty[i], 104, buf, s->valid); drew = 1; }
    }

    /* MCU teplota jadra (4. radek karty Teploty, y=182). */
    { const sensor_stat_t *mt = &g_sensors[SENS_CORE_T];
      fmt_temp(buf, sizeof(buf), mt->last);
      snprintf(key, sizeof(key), "%c%s", mt->valid ? 'V' : 'X', buf);
      if (force || dchg(c_mcu[0], sizeof(c_mcu[0]), key)) { dval(DG_LVAL, 182, 104, buf, mt->valid); drew = 1; } }

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
        if (force || dchg(c_mcu[1 + k], sizeof(c_mcu[1 + k]), key)) {
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
        dlabel(DG_LLBL, 104, "0x48");
        dlabel(DG_LLBL, 130, "0x49");
        dlabel(DG_LLBL, 156, "0x4A");
        dlabel(DG_LLBL, 182, "MCU");

        ui_card_t c_adc = {.rect = {DG_LX, 212, DG_COLW, 192},
                           .header_label = "Napeti (ADS1115 + MCU)"};
        ui_card_render_chrome(&c_adc);
        dlabel(DG_LLBL, 258, "AIN0");
        dlabel(DG_LLBL, 284, "AIN1");
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
    static char c_lat[24], c_lon[24], c_alt[20], c_tp[20], c_rx[40];
    char buf[64], a[16], b[16];
    gps_data_t g;
    gps_get(&g);
    int drew = force;

    /* FIX status */
    const char *fs; prim_color_t fc;
    if      (g.valid && g.fix_mode == 3) { fs = "FIX 3D"; fc = UI_COLOR_OK; }
    else if (g.valid && g.fix_mode == 2) { fs = "FIX 2D"; fc = UI_COLOR_OK; }
    else if (g.fix_quality > 0)          { fs = "FIX";    fc = UI_COLOR_OK; }
    else                                 { fs = "NO FIX"; fc = UI_COLOR_INK_3; }
    if (force || dchg(c_fix, sizeof c_fix, fs)) {
        /* FIX je mono_25 (glyfy ~30 px) -> standardni dtext clear (22 px) by nechal
         * zbytky nad/pod -> vlastni vyssi clear box (jako hodiny v headeru). */
        prim_fill_rect((prim_rect_t){DG_LLBL, 90, 220, 30}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
        prim_draw_text((prim_point_t){DG_LLBL, 112}, fs, &ui_font_mono_25, fc, PRIM_ALIGN_LEFT);
        drew = 1; }

    snprintf(buf, sizeof buf, "%u / %u druzic", g.num_sat, g.sats_in_view);
    if (force || dchg(c_sat, sizeof c_sat, buf)) {
        dtext(DG_LLBL, 138, 220, buf, UI_COLOR_INK_3, &ui_font_sans_16); drew = 1; }

    /* VERTIKALNI signal bars per druzice (setrizene podle C/N0 sestupne), barevne
     * dle sily. Nahrazuje textovy souhrn -> graficky prehled (jako u-center). */
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
        int n = nsv > 12 ? 12 : nsv;                  /* kresli se jen 12 nejsilnejsich (sirka karty) */
        /* zmenovy klic jen z KRESLENYCH sloupcu (pocet + jejich C/N0) -> zadny
         * falesny redraw pri zmene slabe, nezobrazene druzice; key <= c_bars */
        char key[48]; int kp = snprintf(key, sizeof key, "%d", n);
        for (int i = 0; i < n && kp < (int)sizeof key - 5; i++)
            kp += snprintf(key + kp, sizeof key - kp, ".%u", sv[i].snr);
        if (force || dchg(c_bars, sizeof c_bars, key)) {
            /* clear jen oblast baru (190..370) — NESMI zasahnout HDOP/PDOP @392 */
            prim_fill_rect((prim_rect_t){DG_LLBL, 190, DG_COLW - 24, 180},
                           UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
            if (n == 0) {
                dtext_c((int16_t)(DG_LX + DG_COLW / 2), 274, DG_COLW - 24,
                        "Hledam druzice...", UI_COLOR_WARN, &ui_font_sans_16);
            } else {
                const int16_t base = 346;             /* dolni hrana sloupcu */
                const int16_t maxh = 140;             /* vyska pro C/N0 = 55 dB-Hz */
                const int16_t area = DG_COLW - 24;    /* 352 px sirka */
                int16_t slot = (int16_t)(area / n);   /* DYNAMICKA sirka slotu dle poctu druzic (n>=1) */
                int16_t bw = (int16_t)(slot * 2 / 3); /* sloupec ~2/3 slotu, zbytek mezera */
                if (bw < 3) bw = 3;
                int16_t startx = DG_LLBL;             /* bary vyplni celou sirku karty */
                for (int i = 0; i < n; i++) {          /* i < n <= nsv <= GPS_MAX_SATS */
                    uint8_t snr = sv[i].snr;
                    prim_color_t col = (snr >= 38) ? UI_COLOR_OK :
                                       (snr >= 25) ? UI_COLOR_WARN :
                                       (snr > 0)   ? UI_COLOR_BAD : UI_COLOR_INK_4;
                    int16_t cx = (int16_t)(startx + i * slot + slot / 2);
                    int16_t h  = (int16_t)((snr > 55 ? 55 : snr) * maxh / 55);
                    if (h < 2) h = 2;                  /* min pahyl */
                    prim_fill_rect((prim_rect_t){(int16_t)(cx - bw / 2), (int16_t)(base - h),
                                   bw, h}, col, PRIM_BLEND_REPLACE);
                    char pr[6]; snprintf(pr, sizeof pr, "%u", sv[i].prn);   /* PRN 0..255 -> max 3 zn */
                    prim_draw_text((prim_point_t){cx, 360}, pr, &ui_font_mono_16,
                                   UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
                }
            }
            drew = 1;
        }
    }

    fmt_d1(g.hdop, a, sizeof a); fmt_d1(g.pdop, b, sizeof b);
    snprintf(buf, sizeof buf, "HDOP %s   PDOP %s", a, b);
    if (force || dchg(c_dop, sizeof c_dop, buf)) {
        dtext(DG_LLBL, 392, DG_COLW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_18); drew = 1; }

    /* cas + datum z RTC (LSE, disciplinovany GPS). RTC tika i bez fixu -> karta
     * je vzdy zive; synced=0 (volny beh od bootu) ztlumime. g_rtc_text =
     * "YYYY-MM-DD HH:MM:SS" -> [0..9] datum, [11..18] cas. */
    char rt[24]; uint8_t rsy;
    strncpy(rt, (const char *)g_rtc_text, sizeof rt - 1); rt[sizeof rt - 1] = '\0';
    rsy = g_rtc_synced;
    snprintf(buf, sizeof buf, "%s UTC", (strlen(rt) >= 19) ? rt + 11 : "--:--:--");
    if (force || dchg(c_time, sizeof c_time, buf)) {
        dtext(DG_RLBL, 104, 220, buf, rsy ? UI_COLOR_INK : UI_COLOR_INK_3,
              &ui_font_mono_16); drew = 1; }
    snprintf(buf, sizeof buf, "%.10s", rt);   /* datum "YYYY-MM-DD" (sync stav nese barva casu) */
    if (force || dchg(c_date, sizeof c_date, buf)) {
        dtext(DG_RLBL, 126, 220, buf, UI_COLOR_INK_3, &ui_font_sans_16); drew = 1; }

    /* poloha */
    if (g.valid) fmt_ll(g.lat_deg, 'N', 'S', a, sizeof a); else snprintf(a, sizeof a, "--");
    snprintf(buf, sizeof buf, "Lat   %s", a);
    if (force || dchg(c_lat, sizeof c_lat, buf)) {
        dtext(DG_RLBL, 190, DG_COLW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_18); drew = 1; }
    if (g.valid) fmt_ll(g.lon_deg, 'E', 'W', a, sizeof a); else snprintf(a, sizeof a, "--");
    snprintf(buf, sizeof buf, "Lon   %s", a);
    if (force || dchg(c_lon, sizeof c_lon, buf)) {
        dtext(DG_RLBL, 214, DG_COLW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_18); drew = 1; }
    if (g.fix_quality) snprintf(buf, sizeof buf, "Alt   %ld m", lround_f(g.alt_m));
    else               snprintf(buf, sizeof buf, "Alt   --");
    if (force || dchg(c_alt, sizeof c_alt, buf)) {
        dtext(DG_RLBL, 238, DG_COLW - 24, buf, UI_COLOR_INK_3, &ui_font_mono_18); drew = 1; }

    /* TIMEPULSE: s fixem 100 kHz (GPSDO PLL reference, disciplinovany na GNSS);
     * bez fixu 10 Hz (frekvence = lock indikator pro desku -> hold VC / holdover). */
    const char *tp; prim_color_t tc;
    if (g.fix_quality) { tp = "100 kHz (disc.)"; tc = UI_COLOR_OK; }
    else               { tp = "10 Hz (hold)";    tc = UI_COLOR_WARN; }
    if (force || dchg(c_tp, sizeof c_tp, tp)) {
        dtext(DG_RLBL, 298, 200, tp, tc, &ui_font_mono_16); drew = 1; }

    /* Prijimac: zive statistiky linky (naparsovane vety + platne fixy) — rostou,
     * dokud GPS tece -> dukaz zivosti (staticke "NEO-7M 9600 8N1" je v chrome). */
    snprintf(buf, sizeof buf, "Vet: %lu   Fix: %lu",
             (unsigned long)g.sentences, (unsigned long)g.fixes);
    if (force || dchg(c_rx, sizeof c_rx, buf)) {
        dtext(DG_RLBL, 384, DG_COLW - 24, buf, UI_COLOR_INK_2, &ui_font_mono_16); drew = 1; }

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

        ui_card_t c_fix = {.rect = {DG_LX, 58, DG_COLW, 90}, .header_label = "FIX"};
        ui_card_render_chrome(&c_fix);
        ui_card_t c_sat = {.rect = {DG_LX, 158, DG_COLW, 246},
                           .header_label = "Druzice"};
        ui_card_render_chrome(&c_sat);
        ui_card_t c_time = {.rect = {DG_RX, 58, DG_COLW, 76}, .header_label = "Cas / datum (UTC)"};
        ui_card_render_chrome(&c_time);
        ui_card_t c_pos = {.rect = {DG_RX, 144, DG_COLW, 98}, .header_label = "Poloha"};
        ui_card_render_chrome(&c_pos);
        ui_card_t c_tp = {.rect = {DG_RX, 252, DG_COLW, 68}, .header_label = "TIMEPULSE"};
        ui_card_render_chrome(&c_tp);
        /* Model do HEADERU (staticky) -> karta unese 1 zivy radek statistik bez
         * prekryvu (74px na 3 radky nestacilo -> header se kryl s modelem). */
        ui_card_t c_rx = {.rect = {DG_RX, 330, DG_COLW, 74},
                          .header_label = "Prijimac  NEO-7M 9600 8N1"};
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
    static char c_rtos[4][20], c_stk[5][16], c_i2c[2][40], c_lnk[3][40], c_pwr[2][16];
    char buf[36], key[40];   /* "I2C1 FPGA: CHYBA (<u32>)" az 29 zn. -> bez truncation */
    int drew = force;

    /* ── Levy sloupec: RTOS pamet / CPU / uptime ── */
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_free);
    if (force || dchg(c_rtos[0], sizeof(c_rtos[0]), buf)) { dval(DG_LVAL, 104, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_min);
    if (force || dchg(c_rtos[1], sizeof(c_rtos[1]), buf)) { dval(DG_LVAL, 132, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu %%", (unsigned long)g_rtos_cpu_pct);
    if (force || dchg(c_rtos[2], sizeof(c_rtos[2]), buf)) { dval(DG_LVAL, 160, 150, buf, 1); drew = 1; }
    { uint32_t s = g_uptime_s;
      snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", (unsigned long)(s / 3600u),
               (unsigned long)((s / 60u) % 60u), (unsigned long)(s % 60u)); }
    if (force || dchg(c_rtos[3], sizeof(c_rtos[3]), buf)) { dval(DG_LVAL, 188, 150, buf, 1); drew = 1; }

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

    /* ── Pravy sloupec: napajeci vetve (z ADS, mV -> V se 2 desetinami) ── */
    static const struct { sensor_id_t id; int16_t y; } pwr[2] = {
        { SENS_ADS2, 340 }, { SENS_ADS3, 372 } };
    for (int i = 0; i < 2; i++) {
        const sensor_stat_t *a = &g_sensors[pwr[i].id];
        long mv = lround_f(a->last);
        snprintf(buf, sizeof(buf), "%ld.%02ld V", mv / 1000, (mv % 1000) / 10);
        if (force || dchg(c_pwr[i], sizeof(c_pwr[i]), buf)) {
            dval(DG_RVAL, pwr[i].y, 130, buf, a->valid); drew = 1; }
    }

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
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "SYSTEM HEALTH",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);

        /* Levy: RTOS + Stack tasku. */
        ui_card_t c_rtos = {.rect = {DG_LX, 58, DG_COLW, 150},
                            .header_label = "RTOS / Pamet"};
        ui_card_render_chrome(&c_rtos);
        dlabel(DG_LLBL, 104, "Heap free");
        dlabel(DG_LLBL, 132, "Heap min");
        dlabel(DG_LLBL, 160, "CPU");
        dlabel(DG_LLBL, 188, "Uptime");

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
                           .header_label = "Napajeni"};
        ui_card_render_chrome(&c_pwr);
        dlabel(DG_RLBL, 340, "12V vetev");
        dlabel(DG_RLBL, 372, "5V vetev");
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
    { SENS_T48,    DG_LVAL, SENS_R0 + 0 * SENS_DY, 1 },   /* leva: Teploty */
    { SENS_T49,    DG_LVAL, SENS_R0 + 1 * SENS_DY, 1 },
    { SENS_T4A,    DG_LVAL, SENS_R0 + 2 * SENS_DY, 1 },
    { SENS_CORE_T, DG_LVAL, SENS_R0 + 3 * SENS_DY, 1 },
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
        dlabel(DG_LLBL, SENS_R0 + 0 * SENS_DY, "TMP 0x48");
        dlabel(DG_LLBL, SENS_R0 + 1 * SENS_DY, "TMP 0x49");
        dlabel(DG_LLBL, SENS_R0 + 2 * SENS_DY, "TMP 0x4A");
        dlabel(DG_LLBL, SENS_R0 + 3 * SENS_DY, "MCU jadro");
        dlabel(DG_RLBL, SENS_R0 + 0 * SENS_DY, "ADS AIN0");
        dlabel(DG_RLBL, SENS_R0 + 1 * SENS_DY, "ADS AIN1");
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
    if ((int)g.num_sat != last_sat || (int)g.fix_quality != last_fixq || hdop10 != last_hdop10) {
        last_sat = (int)g.num_sat;
        last_fixq = (int)g.fix_quality;
        last_hdop10 = hdop10;
        if (screen_main_redraw_header()) s_dirty = 1;
    }
}

/* Animace signal bargrafu (SIMULACE): hodnota miri k nahodnemu CILI po krocich
 * 1 % (= 1 dBm, dbm=pct-80). Volat 10x/s z UiTasku. Jen na hlavni obrazovce.
 * Flip jen kdyz se hodnota zmenila. */
void app_gpsdo_tick_signal(void)
{
    if (s_view != 0 || !screen_main_is_running()) return;   /* STOP -> zamrzne */
    static int16_t  pct = 0, target = 0;
    static uint32_t seed = 0x1234567u;

    if (pct == target) {                 /* cil dosazen -> novy (0..100 %) */
        seed = seed * 1103515245u + 12345u;
        target = (int16_t)((seed >> 16) % 101u);   /* 0..100 */
    }
    int16_t old = pct;
    if (pct < target)      pct += 1;     /* krok 1 % = 1 dBm */
    else if (pct > target) pct -= 1;
    if (pct == old) return;              /* nic se nezmenilo -> neflipovat */

    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_signal(pct)) s_dirty = 1;   /* flip odlozen na flush */
}

/* Simulace kmitoctu (~20x/s, jen hlavni obrazovka): per-segment dirty redraw. */
void app_gpsdo_tick_freq(void)
{
    if (s_view != 0 || !screen_main_is_running()) return;   /* STOP -> cislo zamrzne */
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_freq()) s_dirty = 1;   /* flip odlozen na flush */
}

/* GPSDO statistika (jen hlavni obrazovka, jen RUN): vzorkovani frakcni odchylky (~1x/s). */
void app_gpsdo_tick_stats_sample(void)
{
    if (s_view != 0 || !screen_main_is_running()) return;   /* STOP -> trend/Allan zamrznou */
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
        if (screen_main_hit_gnss(x, y)) { app_gpsdo_render_gps(); return true; }   /* GNSS pill */
        if (screen_main_hit_sys(x, y))  { app_gpsdo_render_health(); return true; }  /* SYS pill */
        int b = screen_main_hit_button(x, y);
        if (b == 4) { app_gpsdo_render_diag(); return true; }   /* MENU */
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
        /* System Health -> tap na "SENZORY" / "PAMET" otevre podokno. */
        if (s_view == 3 && in_rect(x, y, SENS_BTN_RECT)) {
            app_gpsdo_render_sensors();
            return true;
        }
        if (s_view == 3 && in_rect(x, y, MEM_BTN_RECT)) {
            app_gpsdo_render_mem();
            return true;
        }
        if (in_rect(x, y, BACK_RECT)) {
            if (s_view == 4 || s_view == 5) app_gpsdo_render_health();  /* z podokna zpet na Health */
            else app_gpsdo_render_main();                               /* jinak na hlavni obrazovku */
            return true;
        }
    }
    return false;
}
