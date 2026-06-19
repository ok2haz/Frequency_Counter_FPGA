/**
 * @file fill.c
 * @brief Rectangle fills, rounded rects and blits.
 *
 * REPLACE fills and blits prefer the DMA2D backend; OVER fills, rounded corners
 * and the host build use the software path. All storage is RGB565.
 */

#include <prim/fill.h>
#include <prim/shapes.h>
#include <stddef.h>
#include "internal/fb_impl.h"
#include "internal/alpha_blend.h"
#include "internal/dma2d_backend.h"

static void sw_fill(prim_rect_t r, prim_color_t color, prim_blend_t blend)
{
    prim_fb_t *fb = prim_internal_target();
    prim_pixel_t packed = prim_argb_to_565(color);
    for (int16_t y = 0; y < r.h; y++) {
        prim_pixel_t *row = &fb->pixels[(r.y + y) * fb->stride_px + r.x];
        if (blend == PRIM_BLEND_REPLACE || PRIM_A(color) == 0xFFu) {
            for (int16_t x = 0; x < r.w; x++) row[x] = packed;
        } else {
            for (int16_t x = 0; x < r.w; x++) row[x] = prim_blend565(row[x], color, 0xFFu);
        }
    }
}

void prim_fill_rect(prim_rect_t rect, prim_color_t color, prim_blend_t blend)
{
    if (rect.w <= 0 || rect.h <= 0) return;
    prim_rect_t r;
    if (!prim_internal_clip_rect(rect, &r)) return;

    const prim_dma2d_backend_t *d = prim_dma2d();
    if (d != NULL && d->fill_rect != NULL &&
        (blend == PRIM_BLEND_REPLACE || PRIM_A(color) == 0xFFu)) {
        prim_fb_t *fb = prim_internal_target();
        d->fill_rect(&fb->pixels[r.y * fb->stride_px + r.x], fb->stride_px,
                     r.w, r.h, prim_argb_to_565(color));
        if (d->wait) d->wait();
        return;
    }
    sw_fill(r, color, blend);
}

/* Anti-aliased quarter-circle corner. (cx,cy) is the rounding-circle CENTER
 * (inner corner of the radius square); pixels are plotted outward by (qx,qy)
 * and filled when within radius r of the center. Sampled 4x for coverage. */
static void aa_corner(int16_t cx, int16_t cy, int16_t r, int16_t qx, int16_t qy,
                      prim_color_t color)
{
    int32_t r2x16 = (int32_t)r * r * 16;
    for (int16_t dy = 0; dy <= r; dy++) {
        for (int16_t dx = 0; dx <= r; dx++) {
            int16_t cov = 0;
            for (int s = 0; s < 4; s++) {
                /* sub-sample distance from the center (offsets 0.25/0.75 px) */
                int32_t sx = dx * 4 + ((s & 1) ? 3 : 1);
                int32_t sy = dy * 4 + ((s & 2) ? 3 : 1);
                if (sx * sx + sy * sy <= r2x16) cov++;
            }
            if (cov == 0) continue;
            int16_t px = (int16_t)(cx + qx * dx);
            int16_t py = (int16_t)(cy + qy * dy);
            prim_internal_blend_px(px, py, color, (uint8_t)(cov * 255 / 4));
        }
    }
}

void prim_fill_rect_rounded(prim_rect_t rect, int16_t radius,
                            prim_color_t color, prim_blend_t blend)
{
    if (rect.w <= 0 || rect.h <= 0) return;
    if (radius <= 0) { prim_fill_rect(rect, color, blend); return; }
    if (radius * 2 > rect.w) radius = (int16_t)(rect.w / 2);
    if (radius * 2 > rect.h) radius = (int16_t)(rect.h / 2);

    /* Center cross: top strip, middle, bottom strip via plain fills. */
    prim_fill_rect((prim_rect_t){rect.x, (int16_t)(rect.y + radius),
                                 rect.w, (int16_t)(rect.h - 2 * radius)},
                   color, blend);
    prim_fill_rect((prim_rect_t){(int16_t)(rect.x + radius), rect.y,
                                 (int16_t)(rect.w - 2 * radius), radius},
                   color, blend);
    prim_fill_rect((prim_rect_t){(int16_t)(rect.x + radius),
                                 (int16_t)(rect.y + rect.h - radius),
                                 (int16_t)(rect.w - 2 * radius), radius},
                   color, blend);

    int16_t r = radius;
    aa_corner((int16_t)(rect.x + r),              (int16_t)(rect.y + r),
              r, -1, -1, color);
    aa_corner((int16_t)(rect.x + rect.w - 1 - r), (int16_t)(rect.y + r),
              r, +1, -1, color);
    aa_corner((int16_t)(rect.x + r),              (int16_t)(rect.y + rect.h - 1 - r),
              r, -1, +1, color);
    aa_corner((int16_t)(rect.x + rect.w - 1 - r), (int16_t)(rect.y + rect.h - 1 - r),
              r, +1, +1, color);
}

void prim_stroke_rect_rounded(prim_rect_t rect, int16_t radius,
                              int16_t thickness, prim_color_t color)
{
    if (thickness <= 0) thickness = 1;
    if (radius < 0) radius = 0;
    /* Clamp radius to the box (same as the fill) so arcs match the shape. */
    if (radius * 2 > rect.w) radius = (int16_t)(rect.w / 2);
    if (radius * 2 > rect.h) radius = (int16_t)(rect.h / 2);
    /* Straight edges as thin fills. */
    prim_fill_rect((prim_rect_t){(int16_t)(rect.x + radius), rect.y,
                                 (int16_t)(rect.w - 2 * radius), thickness},
                   color, PRIM_BLEND_OVER);
    prim_fill_rect((prim_rect_t){(int16_t)(rect.x + radius),
                                 (int16_t)(rect.y + rect.h - thickness),
                                 (int16_t)(rect.w - 2 * radius), thickness},
                   color, PRIM_BLEND_OVER);
    prim_fill_rect((prim_rect_t){rect.x, (int16_t)(rect.y + radius),
                                 thickness, (int16_t)(rect.h - 2 * radius)},
                   color, PRIM_BLEND_OVER);
    prim_fill_rect((prim_rect_t){(int16_t)(rect.x + rect.w - thickness),
                                 (int16_t)(rect.y + radius),
                                 thickness, (int16_t)(rect.h - 2 * radius)},
                   color, PRIM_BLEND_OVER);
    /* Rounded corners: four 90° AA arcs. Angle 0°=east, CCW positive. */
    if (radius > 0) {
        prim_draw_arc((prim_point_t){(int16_t)(rect.x + radius),
                                     (int16_t)(rect.y + radius)},
                      radius, thickness, color, 90, 90);    /* top-left */
        prim_draw_arc((prim_point_t){(int16_t)(rect.x + rect.w - 1 - radius),
                                     (int16_t)(rect.y + radius)},
                      radius, thickness, color, 0, 90);     /* top-right */
        prim_draw_arc((prim_point_t){(int16_t)(rect.x + radius),
                                     (int16_t)(rect.y + rect.h - 1 - radius)},
                      radius, thickness, color, 180, 90);   /* bottom-left */
        prim_draw_arc((prim_point_t){(int16_t)(rect.x + rect.w - 1 - radius),
                                     (int16_t)(rect.y + rect.h - 1 - radius)},
                      radius, thickness, color, 270, 90);   /* bottom-right */
    }
}

void prim_blit(prim_rect_t dst, const prim_pixel_t *src, int16_t src_stride)
{
    if (src == NULL || dst.w <= 0 || dst.h <= 0) return;
    prim_rect_t r;
    if (!prim_internal_clip_rect(dst, &r)) return;

    prim_fb_t *fb = prim_internal_target();
    int16_t src_stride_px = (int16_t)(src_stride / 2);
    int16_t off_x = (int16_t)(r.x - dst.x);
    int16_t off_y = (int16_t)(r.y - dst.y);
    const prim_pixel_t *s0 = src + off_y * src_stride_px + off_x;

    const prim_dma2d_backend_t *d = prim_dma2d();
    if (d != NULL && d->blit != NULL) {
        d->blit(&fb->pixels[r.y * fb->stride_px + r.x], fb->stride_px,
                s0, src_stride_px, r.w, r.h);
        if (d->wait) d->wait();
        return;
    }
    for (int16_t y = 0; y < r.h; y++) {
        const prim_pixel_t *srow = s0 + y * src_stride_px;
        prim_pixel_t *drow = &fb->pixels[(r.y + y) * fb->stride_px + r.x];
        for (int16_t x = 0; x < r.w; x++) drow[x] = srow[x];
    }
}
