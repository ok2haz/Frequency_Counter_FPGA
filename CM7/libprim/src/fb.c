/**
 * @file fb.c
 * @brief Framebuffer target, clip state, and clipped pixel writes (RGB565).
 */

#include <prim/fb.h>
#include <stddef.h>
#include "internal/fb_impl.h"
#include "internal/alpha_blend.h"

/* Single-threaded render context (see coding_standards §7). */
static prim_fb_t  *g_prim_target = NULL;
static prim_rect_t g_prim_clip   = {0, 0, 0, 0};
static bool        g_prim_clip_set = false;

prim_status_t prim_fb_init(prim_fb_t *fb, prim_pixel_t *pixels,
                           int16_t width, int16_t height, int16_t stride_bytes)
{
    if (fb == NULL || pixels == NULL) {
        return PRIM_ERR_NULL;
    }
    if (width <= 0 || height <= 0 || stride_bytes < (int16_t)(width * 2)) {
        return PRIM_ERR_INVALID_ARG;
    }
    fb->pixels    = pixels;
    fb->width     = width;
    fb->height    = height;
    fb->stride_px = (int16_t)(stride_bytes / 2);
    return PRIM_OK;
}

void prim_set_target(prim_fb_t *fb)
{
    g_prim_target = fb;
    /* Default clip = whole target. */
    if (fb != NULL) {
        g_prim_clip = (prim_rect_t){0, 0, fb->width, fb->height};
    }
    g_prim_clip_set = false;
}

prim_fb_t *prim_get_target(void) { return g_prim_target; }

prim_pixel_t *prim_fb_pixels(prim_fb_t *fb) { return fb ? fb->pixels : NULL; }
int16_t prim_fb_width(const prim_fb_t *fb)  { return fb ? fb->width : 0; }
int16_t prim_fb_height(const prim_fb_t *fb) { return fb ? fb->height : 0; }
int16_t prim_fb_stride(const prim_fb_t *fb) { return fb ? fb->stride_px : 0; }

void prim_set_clip(prim_rect_t rect)
{
    g_prim_clip = rect;
    g_prim_clip_set = true;
}

void prim_reset_clip(void)
{
    if (g_prim_target != NULL) {
        g_prim_clip = (prim_rect_t){0, 0, g_prim_target->width,
                                    g_prim_target->height};
    }
    g_prim_clip_set = false;
}

prim_rect_t prim_get_clip(void) { return g_prim_clip; }

/* ── Internal accessors ─────────────────────────────────────── */

prim_fb_t *prim_internal_target(void) { return g_prim_target; }

prim_rect_t prim_internal_clip(void)
{
    if (g_prim_clip_set || g_prim_target == NULL) {
        return g_prim_clip;
    }
    return (prim_rect_t){0, 0, g_prim_target->width, g_prim_target->height};
}

bool prim_internal_clip_rect(prim_rect_t in, prim_rect_t *out)
{
    prim_rect_t c = prim_internal_clip();
    int16_t x0 = (in.x > c.x) ? in.x : c.x;
    int16_t y0 = (in.y > c.y) ? in.y : c.y;
    int16_t x1 = (int16_t)((in.x + in.w < c.x + c.w) ? in.x + in.w : c.x + c.w);
    int16_t y1 = (int16_t)((in.y + in.h < c.y + c.h) ? in.y + in.h : c.y + c.h);
    if (g_prim_target != NULL) {
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > g_prim_target->width)  x1 = g_prim_target->width;
        if (y1 > g_prim_target->height) y1 = g_prim_target->height;
    }
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }
    *out = (prim_rect_t){x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
    return true;
}

static inline bool clip_test(int16_t x, int16_t y)
{
    prim_rect_t c = prim_internal_clip();
    if (g_prim_target == NULL) return false;
    if (x < c.x || y < c.y || x >= c.x + c.w || y >= c.y + c.h) return false;
    if (x < 0 || y < 0 || x >= g_prim_target->width || y >= g_prim_target->height)
        return false;
    return true;
}

void prim_internal_put_px(int16_t x, int16_t y, prim_color_t color,
                          prim_blend_t blend)
{
    if (!clip_test(x, y)) return;
    prim_pixel_t *p = &g_prim_target->pixels[y * g_prim_target->stride_px + x];
    if (blend == PRIM_BLEND_REPLACE || PRIM_A(color) == 0xFFu) {
        *p = prim_argb_to_565(color);
    } else {
        *p = prim_blend565(*p, color, 0xFFu);
    }
}

void prim_internal_blend_px(int16_t x, int16_t y, prim_color_t color,
                            uint8_t coverage)
{
    if (coverage == 0u || !clip_test(x, y)) return;
    prim_pixel_t *p = &g_prim_target->pixels[y * g_prim_target->stride_px + x];
    *p = prim_blend565(*p, color, coverage);
}
