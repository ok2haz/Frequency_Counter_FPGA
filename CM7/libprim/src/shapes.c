/**
 * @file shapes.c
 * @brief Lines, circles and arcs with anti-aliased edges (software).
 *
 * Angle convention: degrees measured from +x (east), counter-clockwise in math
 * orientation. On screen (y down) a point at angle a is
 * (cx + r*cos a, cy - r*sin a), so 90° is up. Arcs sweep CCW.
 */

#include <prim/shapes.h>
#include <prim/fill.h>
#include <math.h>
#include <stdlib.h>
#include "internal/fb_impl.h"

/* AA coverage from a signed distance to an edge (in pixels), ~1px ramp. */
static inline uint8_t edge_cov(float dist)
{
    float c = 0.5f - dist;       /* inside (dist<0) → toward 1 */
    if (c <= 0.0f) return 0u;
    if (c >= 1.0f) return 255u;
    return (uint8_t)(c * 255.0f + 0.5f);
}

void prim_draw_line(prim_point_t from, prim_point_t to, int16_t thickness,
                    prim_color_t color)
{
    if (thickness <= 0) thickness = 1;

    /* Axis-aligned fast paths via DMA2D-capable fills. */
    if (from.y == to.y) {
        int16_t x0 = from.x < to.x ? from.x : to.x;
        int16_t w  = (int16_t)(abs(to.x - from.x) + 1);
        prim_fill_rect((prim_rect_t){x0, (int16_t)(from.y - thickness / 2),
                                     w, thickness}, color, PRIM_BLEND_OVER);
        return;
    }
    if (from.x == to.x) {
        int16_t y0 = from.y < to.y ? from.y : to.y;
        int16_t h  = (int16_t)(abs(to.y - from.y) + 1);
        prim_fill_rect((prim_rect_t){(int16_t)(from.x - thickness / 2), y0,
                                     thickness, h}, color, PRIM_BLEND_OVER);
        return;
    }

    /* General: per-pixel distance-to-segment band with AA. */
    float ax = from.x, ay = from.y, bx = to.x, by = to.y;
    float dx = bx - ax, dy = by - ay;
    float len2 = dx * dx + dy * dy;
    float half = thickness * 0.5f;

    int16_t minx = (int16_t)floorf(fminf(ax, bx) - half - 1);
    int16_t maxx = (int16_t)ceilf(fmaxf(ax, bx) + half + 1);
    int16_t miny = (int16_t)floorf(fminf(ay, by) - half - 1);
    int16_t maxy = (int16_t)ceilf(fmaxf(ay, by) + half + 1);

    for (int16_t y = miny; y <= maxy; y++) {
        for (int16_t x = minx; x <= maxx; x++) {
            float px = x - ax, py = y - ay;
            float t = (px * dx + py * dy) / len2;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            float cx = px - t * dx, cy = py - t * dy;
            float d = sqrtf(cx * cx + cy * cy) - half;
            uint8_t cov = edge_cov(d);
            if (cov) prim_internal_blend_px(x, y, color, cov);
        }
    }
}

void prim_draw_line_dashed(prim_point_t from, prim_point_t to, int16_t thickness,
                           prim_color_t color, int16_t dash_len, int16_t gap_len)
{
    if (dash_len <= 0) dash_len = 1;
    if (gap_len < 0) gap_len = 0;
    float dx = to.x - from.x, dy = to.y - from.y;
    float total = sqrtf(dx * dx + dy * dy);
    if (total < 1.0f) return;
    float ux = dx / total, uy = dy / total;
    float pos = 0.0f;
    while (pos < total) {
        float seg_end = pos + dash_len;
        if (seg_end > total) seg_end = total;
        prim_point_t a = {(int16_t)(from.x + ux * pos),  (int16_t)(from.y + uy * pos)};
        prim_point_t b = {(int16_t)(from.x + ux * seg_end), (int16_t)(from.y + uy * seg_end)};
        prim_draw_line(a, b, thickness, color);
        pos = seg_end + gap_len;
    }
}

/* Annular band + optional angular sector. */
static void ring_sector(prim_point_t c, float r_out, float r_in,
                        prim_color_t color, bool sector,
                        float a_start_deg, float a_sweep_deg)
{
    int16_t minx = (int16_t)floorf(c.x - r_out - 1);
    int16_t maxx = (int16_t)ceilf(c.x + r_out + 1);
    int16_t miny = (int16_t)floorf(c.y - r_out - 1);
    int16_t maxy = (int16_t)ceilf(c.y + r_out + 1);

    float a0 = a_start_deg;
    float a1 = a_start_deg + a_sweep_deg;

    for (int16_t y = miny; y <= maxy; y++) {
        for (int16_t x = minx; x <= maxx; x++) {
            float dx = x - c.x, dy = y - c.y;
            float d = sqrtf(dx * dx + dy * dy);
            /* Coverage = inside outer AND outside inner. */
            float cov_out = 0.5f - (d - r_out);
            float cov_in  = 0.5f - (r_in - d);
            float cov = cov_out < cov_in ? cov_out : cov_in;
            if (r_in <= 0.0f) cov = cov_out;     /* solid disk */
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;

            if (sector) {
                float ang = atan2f(-dy, dx) * 57.2957795f; /* deg, y-up */
                while (ang < a0) ang += 360.0f;
                if (ang > a1) continue;
            }
            prim_internal_blend_px(x, y, color, (uint8_t)(cov * 255.0f + 0.5f));
        }
    }
}

void prim_draw_circle(prim_point_t center, int16_t radius, int16_t thickness,
                      prim_color_t color)
{
    if (radius <= 0) return;
    if (thickness <= 0) thickness = 1;
    ring_sector(center, (float)radius, (float)(radius - thickness),
                color, false, 0.0f, 360.0f);
}

void prim_fill_circle(prim_point_t center, int16_t radius, prim_color_t color)
{
    if (radius <= 0) return;
    ring_sector(center, (float)radius, 0.0f, color, false, 0.0f, 360.0f);
}

void prim_draw_arc(prim_point_t center, int16_t radius, int16_t thickness,
                   prim_color_t color, int16_t start_angle_deg,
                   int16_t sweep_angle_deg)
{
    if (radius <= 0) return;
    if (thickness <= 0) thickness = 1;
    ring_sector(center, (float)radius, (float)(radius - thickness),
                color, true, (float)start_angle_deg, (float)sweep_angle_deg);
}
