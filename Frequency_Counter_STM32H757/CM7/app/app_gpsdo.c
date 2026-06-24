/**
 * @file app_gpsdo.c
 * @brief Glue: init libprim+HAL, drive the main and diagnostics screens, and
 *        route touch to the MENU / back buttons. Single-context (UiTask).
 */

#include "app_gpsdo.h"
#include "screens/screen_main.h"
#include "hal/stm32/prim_stm32_hal.h"
#include "sensor_stat.h"        /* g_sensors[] (hodnota + valid + statistika) */
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

/* Si5356 status bits (reg 218 / 0xDA). */
#define SI5356_SYS_CAL    (1u << 0)
#define SI5356_LOS_CLKIN  (1u << 2)
#define SI5356_PLL_LOL    (1u << 4)

static prim_fb_t s_fb;
static int s_inited = 0;
static int s_view = 0;          /* 0 = main, 1 = diagnostics */

/* Present coalescing: vysokofrekvencni ticky (clock/signal/freq) jen renderuji a
 * nastavi s_dirty; jeden flip pak udela app_gpsdo_flush() (UiTask ho vola na ~30Hz
 * gate). Snizi pocet VBR flipu + sjednoti copy-forward. Vzacne udalosti (touch,
 * prepnuti obrazovky, render/clear) prezentuji hned pres present_now(). */
static int s_dirty = 0;
static void present_now(void) { prim_stm32_present(); s_dirty = 0; }

/* Back button on the diagnostics screen. */
/* Back button lives in the bottom bar, in the same slot as the main MENU. */
static const prim_rect_t BACK_RECT = {650, 417, 133, 61};

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

/* Right-aligned live value: clear its box then redraw. valid==0 → dimmed + red "!". */
static void dval(int16_t xr, int16_t baseline, int16_t boxw, const char *v, int valid)
{
    prim_fill_rect((prim_rect_t){(int16_t)(xr - boxw), (int16_t)(baseline - 17),
                                 boxw, 22}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){xr, baseline}, v, &ui_font_mono_18,
                   valid ? UI_COLOR_INK : UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
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
    static char c_tv[3][20], c_tm[3][20], c_adc[4][20];
    static char c_spi[68], c_fpga[68], c_si[20], c_sys[4][20];
    char buf[24], key[26];
    int drew = force;   /* force -> vse se kresli */

    /* ── Levy sloupec: teploty (hodnota + min/max) ── */
    static const sensor_id_t tid[3] = { SENS_T48, SENS_T49, SENS_T4A };
    static const int16_t     ty[3]  = { 104, 132, 160 };
    for (int i = 0; i < 3; i++) {
        const sensor_stat_t *s = &g_sensors[tid[i]];
        fmt_minmax(buf, sizeof(buf), s);
        if (force || dchg(c_tm[i], sizeof(c_tm[i]), buf)) {
            dtext((int16_t)(DG_LLBL + 96), ty[i], 118, buf, UI_COLOR_INK_4, &ui_font_sans_14); drew = 1; }
        fmt_temp(buf, sizeof(buf), s->last);
        snprintf(key, sizeof(key), "%c%s", s->valid ? 'V' : 'X', buf);  /* vykresleni zalezi i na valid */
        if (force || dchg(c_tv[i], sizeof(c_tv[i]), key)) {
            dval(DG_LVAL, ty[i], 104, buf, s->valid); drew = 1; }
    }

    /* ADC napeti. */
    for (int k = 0; k < 4; k++) {
        const sensor_stat_t *a = &g_sensors[SENS_ADS0 + k];
        snprintf(buf, sizeof(buf), "%ld mV", lround_f(a->last));
        snprintf(key, sizeof(key), "%c%s", a->valid ? 'V' : 'X', buf);
        if (force || dchg(c_adc[k], sizeof(c_adc[k]), key)) {
            dval(DG_LVAL, (int16_t)(236 + k * 28), 120, buf, a->valid); drew = 1; }
    }

    /* ── Pravy sloupec ── */
    /* FPGA: SPI status (barva dle g_spi_ok -> klic vc. ok) + merici kvalita. */
    char sig[68];
    snprintf(sig, sizeof(sig), "%c%s", g_spi_ok ? 'O' : 'X', (const char *)g_spi_text);
    if (force || dchg(c_spi, sizeof(c_spi), sig)) {
        dtext(DG_RLBL, 104, DG_COLW - 24, (const char *)g_spi_text,
              g_spi_ok ? UI_COLOR_OK : UI_COLOR_BAD, &ui_font_mono_14); drew = 1; }
    if (force || dchg(c_fpga, sizeof(c_fpga), (const char *)g_freq_info)) {
        dtext(DG_RLBL, 132, DG_COLW - 24, (const char *)g_freq_info, UI_COLOR_INK_2, &ui_font_sans_14); drew = 1; }

    /* Reference Si5356: lock status (retezec 1:1 se statusem -> staci porovnat si). */
    const char *si; prim_color_t sic;
    if (!g_si5356_ok)                                   { si = "N/A (I2C)";   sic = UI_COLOR_INK_3; }
    else if (g_si5356_status & SI5356_LOS_CLKIN)        { si = "LOS CLKIN!";  sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_PLL_LOL)          { si = "PLL UNLOCK!"; sic = UI_COLOR_BAD; }
    else if (g_si5356_status & SI5356_SYS_CAL)          { si = "CALIB...";    sic = UI_COLOR_VIOLET; }
    else                                                { si = "LOCK OK";     sic = UI_COLOR_OK; }
    if (force || dchg(c_si, sizeof(c_si), si)) {
        dtext(DG_RLBL, 206, DG_COLW - 24, si, sic, &ui_font_mono_18); drew = 1; }

    /* System / RTOS. */
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_free);
    if (force || dchg(c_sys[0], sizeof(c_sys[0]), buf)) { dval(DG_RVAL, 288, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)g_rtos_heap_min);
    if (force || dchg(c_sys[1], sizeof(c_sys[1]), buf)) { dval(DG_RVAL, 316, 150, buf, 1); drew = 1; }
    snprintf(buf, sizeof(buf), "%lu %%", (unsigned long)g_rtos_cpu_pct);
    if (force || dchg(c_sys[2], sizeof(c_sys[2]), buf)) { dval(DG_RVAL, 344, 150, buf, 1); drew = 1; }
    { uint32_t s = g_uptime_s;
      snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu",
               (unsigned long)(s / 3600u), (unsigned long)((s / 60u) % 60u),
               (unsigned long)(s % 60u)); }
    if (force || dchg(c_sys[3], sizeof(c_sys[3]), buf)) { dval(DG_RVAL, 372, 150, buf, 1); drew = 1; }

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

        /* Left column: Teploty + ADC. */
        ui_card_t c_temp = {.rect = {DG_LX, 58, DG_COLW, 122},
                            .header_label = "Teploty TMP117  (last  min/max)"};
        ui_card_render_chrome(&c_temp);
        dlabel(DG_LLBL, 104, "0x48");
        dlabel(DG_LLBL, 132, "0x49");
        dlabel(DG_LLBL, 160, "0x4A");

        ui_card_t c_adc = {.rect = {DG_LX, 190, DG_COLW, 214},
                           .header_label = "ADC ADS1115"};
        ui_card_render_chrome(&c_adc);
        dlabel(DG_LLBL, 236, "AIN0");
        dlabel(DG_LLBL, 264, "AIN1");
        dlabel(DG_LLBL, 292, "AIN2 (12V)");
        dlabel(DG_LLBL, 320, "AIN3 (5V)");

        /* Right column: FPGA + Reference + System. */
        ui_card_t c_fpga = {.rect = {DG_RX, 58, DG_COLW, 92},
                            .header_label = "Komunikace + mereni FPGA"};
        ui_card_render_chrome(&c_fpga);

        ui_card_t c_ref = {.rect = {DG_RX, 160, DG_COLW, 72},
                           .header_label = "Reference Si5356 (4x100MHz)"};
        ui_card_render_chrome(&c_ref);

        ui_card_t c_sys = {.rect = {DG_RX, 242, DG_COLW, 162},
                           .header_label = "System / RTOS"};
        ui_card_render_chrome(&c_sys);
        dlabel(DG_RLBL, 288, "Heap free");
        dlabel(DG_RLBL, 316, "Heap min");
        dlabel(DG_RLBL, 344, "CPU");
        dlabel(DG_RLBL, 372, "Uptime");
    }
    /* present (flip) jen kdyz se neco prekreslilo (first=1 vzdy kresli chrome+vse). */
    if (draw_diag_values(first)) present_now();
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
    if (s_view == 1) app_gpsdo_render_diag();   /* live refresh of diagnostics */
}

/* Hodinovy tik (~kazdych 100 ms): jen na hlavni obrazovce prekresli simulovany cas. */
void app_gpsdo_tick_clock(uint32_t ms_since_boot)
{
    if (s_view != 0) return;
    prim_set_target(&s_fb);
    prim_reset_clip();
    if (screen_main_redraw_time(ms_since_boot)) s_dirty = 1;   /* flip odlozen na flush */
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
        if (in_rect(x, y, BACK_RECT)) {
            app_gpsdo_render_main();
            return true;
        }
    }
    return false;
}
