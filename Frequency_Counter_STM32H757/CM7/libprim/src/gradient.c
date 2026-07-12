/**
 * @file gradient.c
 * @brief Linear and radial gradients (integer channel interpolation).
 */

#include <prim/gradient.h>
#include <prim/fill.h>
#include "internal/fb_impl.h"

/* Floor integer sqrt (Newton) — O(~log) misto per-pixel lineárního hledání.
 * Vraci floor(sqrt(v)) shodne s drivejsim `while ((d+1)*(d+1)<=v) d++`. */
static inline int32_t isqrt32(int32_t v)
{
    if (v <= 0) return 0;
    int32_t x = v, y = (x + 1) >> 1;
    while (y < x) { x = y; y = (x + v / x) >> 1; }
    return x;   /* floor pro v>=1 */
}

static inline prim_color_t lerp_argb(prim_color_t a, prim_color_t b, int32_t t,
                                     int32_t span)
{
    if (span <= 0) return a;
    int32_t ar = PRIM_R(a), ag = PRIM_G(a), ab = PRIM_B(a);
    int32_t br = PRIM_R(b), bg = PRIM_G(b), bb = PRIM_B(b);
    int32_t r = ar + (br - ar) * t / span;
    int32_t g = ag + (bg - ag) * t / span;
    int32_t bl = ab + (bb - ab) * t / span;
    return PRIM_RGB(r, g, bl);
}

void prim_fill_gradient_linear(prim_rect_t rect, prim_color_t start,
                               prim_color_t end, prim_grad_dir_t dir)
{
    prim_rect_t r;
    if (rect.w <= 0 || rect.h <= 0) return;
    if (!prim_internal_clip_rect(rect, &r)) return;
    prim_fb_t *fb = prim_internal_target();

    if (dir == PRIM_GRAD_VERTICAL) {
        for (int16_t y = 0; y < r.h; y++) {
            int32_t t = (r.y - rect.y) + y;
            prim_pixel_t c = prim_argb_to_565(lerp_argb(start, end, t, rect.h - 1));
            prim_pixel_t *row = &fb->pixels[(r.y + y) * fb->stride_px + r.x];
            for (int16_t x = 0; x < r.w; x++) row[x] = c;
        }
    } else {
        for (int16_t x = 0; x < r.w; x++) {
            int32_t t = (r.x - rect.x) + x;
            prim_pixel_t c = prim_argb_to_565(lerp_argb(start, end, t, rect.w - 1));
            for (int16_t y = 0; y < r.h; y++) {
                fb->pixels[(r.y + y) * fb->stride_px + r.x + x] = c;
            }
        }
    }
}

void prim_fill_gradient_radial(prim_rect_t rect, prim_point_t center,
                               int16_t inner_r, int16_t outer_r,
                               prim_color_t inner, prim_color_t outer)
{
    prim_rect_t r;
    if (rect.w <= 0 || rect.h <= 0 || outer_r <= inner_r) {
        if (rect.w > 0 && rect.h > 0) prim_fill_rect(rect, inner, PRIM_BLEND_REPLACE);
        return;
    }
    if (!prim_internal_clip_rect(rect, &r)) return;
    prim_fb_t *fb = prim_internal_target();
    int32_t span = outer_r - inner_r;
    int32_t outer2 = (int32_t)outer_r * outer_r;
    int32_t inner2 = (int32_t)inner_r * inner_r;

    for (int16_t y = 0; y < r.h; y++) {
        int32_t dy = (r.y + y) - center.y;
        prim_pixel_t *row = &fb->pixels[(r.y + y) * fb->stride_px + r.x];
        for (int16_t x = 0; x < r.w; x++) {
            int32_t dx = (r.x + x) - center.x;
            int32_t d2 = dx * dx + dy * dy;
            prim_color_t c;
            if (d2 <= inner2) {
                c = inner;
            } else if (d2 >= outer2) {
                c = outer;
            } else {
                int32_t d = isqrt32(d2);   /* rychly floor sqrt (bylo per-pixel O(r)) */
                c = lerp_argb(inner, outer, d - inner_r, span);
            }
            row[x] = prim_argb_to_565(c);
        }
    }
}
