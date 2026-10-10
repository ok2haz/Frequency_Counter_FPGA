/**
 * @file glow.c
 * @brief Soft glow via a separable box blur of a coverage mask.
 *
 * The shape is rasterized into a pre-allocated 8-bit coverage scratch buffer,
 * blurred (2 separable passes), then blended as the given color scaled by
 * intensity. No allocation per call. The scratch buffer dimensions cap the
 * glow region; larger requests are clipped.
 */

#include <prim/glow.h>
#include "internal/fb_impl.h"
#include "internal/alpha_blend.h"

/* 🔴 Strop oblasti glow. Drive 800x480, tedy DVE masky po 375 kB = **750 kB
 * SDRAM** — a to na dve volani v celem projektu: LED tecka na pilulce
 * (`rect 6x6, blur 4` -> oblast **14x14 = 196 B**) a podtrzeni posledni
 * duveryhodne cislice (`blur 6` -> **(sirka segmentu + 13) x 13**, tedy ~0,7-2 kB).
 * Skutecna potreba je pod 2 kB; 375 kB byla cista rezerva.
 *
 * Zmenseno NESOUMERNE (audit 2026-09-05): sirka zustava 800, protoze podtrzeni
 * muze jit pres celou sirku cisla (`FREQ_MAX_W` = 780), ale VYSKA staci 64 —
 * to je stale ~5x nad nejhorsim znamym pripadem (13 radku).
 *   800 x 64 = 51 kB / maska -> **102 kB celkem, uspora ~648 kB SDRAM**
 * Vedlejsi zisk: `box_blur_h/v` projde mene dat.
 *
 * ⚠️ Prekroceni stropu je TICHE (funkce jen `return`) — proto se pocita do
 * `g_prim_glow_skipped` a vypisuje ve `status`. Kdyby ten citac zacal rust,
 * je strop maly a nekde mizi glow; NEZVYSUJ ho naslepo, nejdriv zjisti KDE. */
#ifndef PRIM_GLOW_MAX_W
#define PRIM_GLOW_MAX_W 800
#endif
#ifndef PRIM_GLOW_MAX_H
#define PRIM_GLOW_MAX_H 64
#endif

/* Two scratch planes for separable blur. Placed in .sdram on target via the
 * linker (the section attribute is a no-op on host). */
#if defined(__GNUC__) && !defined(PRIM_HOST_BUILD)
  #define PRIM_GLOW_SECTION __attribute__((section(".sdram"), aligned(32)))
#else
  #define PRIM_GLOW_SECTION
#endif

static uint8_t g_mask_a[PRIM_GLOW_MAX_W * PRIM_GLOW_MAX_H] PRIM_GLOW_SECTION;
static uint8_t g_mask_b[PRIM_GLOW_MAX_W * PRIM_GLOW_MAX_H] PRIM_GLOW_SECTION;

/* 🔴 Kolikrat se glow NEVYKRESLIL, protoze oblast prekrocila strop masky.
 * Bez tohohle citace je to TICHE SELHANI: funkce jen `return`, nic se nenakresli
 * a nikde se to neobjevi (trida chyb ze SKILL.md §3). Citac vypisuje `status`.
 * ⚠️ MUSI zustat 0 — kdyz roste, je strop maly a glow nekde mizi. */
uint32_t g_prim_glow_skipped;

static void box_blur_h(const uint8_t *src, uint8_t *dst, int w, int h, int rad)
{
    int win = 2 * rad + 1;
    for (int y = 0; y < h; y++) {
        const uint8_t *s = &src[y * w];
        uint8_t *d = &dst[y * w];
        int acc = 0;
        for (int x = -rad; x <= rad; x++) acc += s[x < 0 ? 0 : (x >= w ? w - 1 : x)];
        for (int x = 0; x < w; x++) {
            d[x] = (uint8_t)(acc / win);
            int add = x + rad + 1; add = add >= w ? w - 1 : add;
            int sub = x - rad;     sub = sub < 0 ? 0 : sub;
            acc += s[add] - s[sub];
        }
    }
}

static void box_blur_v(const uint8_t *src, uint8_t *dst, int w, int h, int rad)
{
    int win = 2 * rad + 1;
    for (int x = 0; x < w; x++) {
        int acc = 0;
        for (int y = -rad; y <= rad; y++)
            acc += src[(y < 0 ? 0 : (y >= h ? h - 1 : y)) * w + x];
        for (int y = 0; y < h; y++) {
            dst[y * w + x] = (uint8_t)(acc / win);
            int add = y + rad + 1; add = add >= h ? h - 1 : add;
            int sub = y - rad;     sub = sub < 0 ? 0 : sub;
            acc += src[add * w + x] - src[sub * w + x];
        }
    }
}

/* Common path: region rect (clamped), mask already rasterized into g_mask_a. */
static void emit_glow(prim_rect_t region, int16_t blur_radius,
                      prim_color_t color, uint8_t intensity_pct)
{
    int w = region.w, h = region.h;
    if (w <= 0 || h <= 0) return;
    if (w > PRIM_GLOW_MAX_W || h > PRIM_GLOW_MAX_H) { g_prim_glow_skipped++; return; }
    if (blur_radius < 1) blur_radius = 1;

    box_blur_h(g_mask_a, g_mask_b, w, h, blur_radius);
    box_blur_v(g_mask_b, g_mask_a, w, h, blur_radius);

    for (int16_t y = 0; y < h; y++) {
        for (int16_t x = 0; x < w; x++) {
            uint8_t cov = g_mask_a[y * w + x];
            if (cov == 0) continue;
            uint8_t a = (uint8_t)((int)cov * intensity_pct / 100);
            prim_internal_blend_px((int16_t)(region.x + x),
                                   (int16_t)(region.y + y), color, a);
        }
    }
}

void prim_glow_rect(prim_rect_t rect, int16_t blur_radius, prim_color_t color,
                    uint8_t intensity_pct)
{
    prim_rect_t region = {(int16_t)(rect.x - blur_radius),
                          (int16_t)(rect.y - blur_radius),
                          (int16_t)(rect.w + 2 * blur_radius),
                          (int16_t)(rect.h + 2 * blur_radius)};
    int w = region.w, h = region.h;
    if (w <= 0 || h <= 0) return;
    if (w > PRIM_GLOW_MAX_W || h > PRIM_GLOW_MAX_H) { g_prim_glow_skipped++; return; }

    for (int i = 0; i < w * h; i++) g_mask_a[i] = 0;
    for (int16_t y = 0; y < rect.h; y++) {
        int my = y + blur_radius;
        for (int16_t x = 0; x < rect.w; x++) {
            g_mask_a[my * w + (x + blur_radius)] = 255;
        }
    }
    emit_glow(region, blur_radius, color, intensity_pct);
}

void prim_glow_line(prim_point_t from, prim_point_t to, int16_t blur_radius,
                    prim_color_t color, uint8_t intensity_pct)
{
    /* Bounding box of the line + blur margin. */
    int16_t minx = (from.x < to.x ? from.x : to.x) - blur_radius;
    int16_t miny = (from.y < to.y ? from.y : to.y) - blur_radius;
    int16_t maxx = (from.x > to.x ? from.x : to.x) + blur_radius;
    int16_t maxy = (from.y > to.y ? from.y : to.y) + blur_radius;
    prim_rect_t region = {minx, miny, (int16_t)(maxx - minx + 1),
                          (int16_t)(maxy - miny + 1)};
    int w = region.w, h = region.h;
    if (w <= 0 || h <= 0) return;
    if (w > PRIM_GLOW_MAX_W || h > PRIM_GLOW_MAX_H) { g_prim_glow_skipped++; return; }

    for (int i = 0; i < w * h; i++) g_mask_a[i] = 0;
    /* Rasterize the line into the mask (simple DDA, 1px). */
    int dx = to.x - from.x, dy = to.y - from.y;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int px = from.x + dx * i / steps - region.x;
        int py = from.y + dy * i / steps - region.y;
        if (px >= 0 && px < w && py >= 0 && py < h) g_mask_a[py * w + px] = 255;
    }
    emit_glow(region, blur_radius, color, intensity_pct);
}
