/**
 * @file app_gpsdo.c
 * @brief Glue: init libprim+HAL, drive the main and diagnostics screens, and
 *        route touch to the MENU / back buttons. Single-context (UiTask).
 */

#include "app_gpsdo.h"
#include "screens/screen_main.h"
#include "hal/stm32/prim_stm32_hal.h"
#include <prim/prim.h>
#include <ui/ui.h>
#include <stdio.h>

/* Firmware sensor globals (defined in freertos.c). */
extern float            g_CurrentTemperature;   /* TMP117 @ 0x48 (I2C4) */
extern float            g_temp49, g_temp4A;      /* TMP117 @ 0x49/0x4A (I2C1) */
extern int32_t          g_ads_mv[4];             /* ADS1115 channels, mV */
extern volatile char    g_spi_text[64];          /* FPGA SPI status line */
extern volatile uint8_t g_spi_ok;                /* 1 = link alive */

static prim_fb_t s_fb;
static int s_inited = 0;
static int s_view = 0;          /* 0 = main, 1 = diagnostics */

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
}

/* ── Diagnostics screen ─────────────────────────────────────── */

/* Format a temperature without %f (nano.specs may omit float printf). */
static void fmt_temp(char *buf, size_t n, float v)
{
    int t = (int)(v * 10000.0f + (v >= 0.0f ? 0.5f : -0.5f));
    int w = t / 10000, f = t % 10000;
    if (f < 0) f = -f;
    snprintf(buf, n, "%d.%04d C", w, f);
}

/* Fixed value baselines (cards at fixed Y → ui_card_inner_rect is deterministic). */
#define DIAG_RX     (UI_DIM_SCREEN_W - 45)   /* right edge of numeric values */
#define DIAG_LBL_X  31                       /* left labels / SPI line */

/* A static text label drawn once (left, sans). */
static void diag_label(int16_t y, const char *s)
{
    prim_draw_text((prim_point_t){DIAG_LBL_X, y}, s, &ui_font_sans_16,
                   UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
}

/* A right-aligned live value: clear its box (card bg) then redraw → no flicker. */
static void put_val(int16_t baseline, const char *v)
{
    prim_fill_rect((prim_rect_t){(int16_t)(DIAG_RX - 170), (int16_t)(baseline - 17),
                                 170, 22}, UI_COLOR_BG_CARD, PRIM_BLEND_REPLACE);
    prim_draw_text((prim_point_t){DIAG_RX, baseline}, v, &ui_font_mono_18,
                   UI_COLOR_INK, PRIM_ALIGN_RIGHT);
}

/* Redraw ONLY the dynamic values (called on entry and on each tick). */
static void draw_diag_values(void)
{
    char buf[64];   /* holds g_spi_text[64] */
    /* SPI status line (left, card c1). */
    prim_fill_rect((prim_rect_t){DIAG_LBL_X, 94, 430, 22}, UI_COLOR_BG_CARD,
                   PRIM_BLEND_REPLACE);
    snprintf(buf, sizeof(buf), "%s", (const char *)g_spi_text);
    prim_draw_text((prim_point_t){DIAG_LBL_X, 111}, buf, &ui_font_mono_18,
                   UI_COLOR_INK_2, PRIM_ALIGN_LEFT);
    /* Temperatures (card c2). */
    fmt_temp(buf, sizeof(buf), g_CurrentTemperature); put_val(173, buf);
    fmt_temp(buf, sizeof(buf), g_temp49);             put_val(199, buf);
    fmt_temp(buf, sizeof(buf), g_temp4A);             put_val(225, buf);
    /* ADC (card c3). */
    for (int k = 0; k < 4; k++) {
        snprintf(buf, sizeof(buf), "%ld mV", (long)g_ads_mv[k]);
        put_val((int16_t)(279 + k * 28), buf);
    }
}

void app_gpsdo_render_diag(void)
{
    app_gpsdo_init();
    prim_set_target(&s_fb);
    prim_reset_clip();

    if (s_view != 1) {
        /* First entry: draw the static chrome + labels exactly once. */
        s_view = 1;
        int16_t cw = UI_DIM_SCREEN_W - 50;
        prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                  screen_main_bg(), UI_DIM_SCREEN_W * (int16_t)sizeof(prim_pixel_t));
        ui_button_t back = {.rect = BACK_RECT, .variant = UI_BUTTON_NORMAL,
                            .label = "< ZPET"};
        ui_button_render(&back);
        prim_draw_text((prim_point_t){UI_DIM_SCREEN_W / 2, 38}, "DIAGNOSTIKA",
                       &ui_font_mono_25, UI_COLOR_ACC, PRIM_ALIGN_CENTER);

        ui_card_t c1 = {.rect = {25, 60, cw, 56}, .header_label = "Komunikace FPGA",
                        .header_right = g_spi_ok ? "LINK OK" : "LINK --",
                        .header_right_accent = g_spi_ok ? UI_COLOR_OK : UI_COLOR_BAD};
        ui_card_render_chrome(&c1);

        ui_card_t c2 = {.rect = {25, 122, cw, 100}, .header_label = "Teploty TMP117"};
        ui_card_render_chrome(&c2);
        diag_label(173, "0x48 (displej, I2C4)");
        diag_label(199, "0x49 (FPGA, I2C1)");
        diag_label(225, "0x4A (FPGA, I2C1)");

        ui_card_t c3 = {.rect = {25, 228, cw, 176}, .header_label = "ADC ADS1115"};
        ui_card_render_chrome(&c3);
        diag_label(279, "AIN0");
        diag_label(307, "AIN1");
        diag_label(335, "AIN2 (12V vetev)");
        diag_label(363, "AIN3 (5V vetev)");
    }
    draw_diag_values();
}

void app_gpsdo_clear(void)
{
    app_gpsdo_init();
    s_view = 0;
    prim_set_target(&s_fb);
    prim_reset_clip();
    prim_fill_rect((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
                   UI_COLOR_BG_0, PRIM_BLEND_REPLACE);
}

void app_gpsdo_tick(void)
{
    if (s_view == 1) app_gpsdo_render_diag();   /* live refresh of diagnostics */
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
