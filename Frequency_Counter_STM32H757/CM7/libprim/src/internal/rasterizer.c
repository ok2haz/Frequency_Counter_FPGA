/**
 * @file rasterizer.c
 * @brief Scanline polygon fill (even-odd), software, no allocation.
 */

#include "internal/rasterizer.h"
#include "internal/fb_impl.h"

void prim_internal_fill_polygon(const prim_point_t *pts, int16_t count,
                                prim_color_t color)
{
    if (pts == NULL || count < 3) return;

    int16_t ymin = pts[0].y, ymax = pts[0].y;
    for (int16_t i = 1; i < count; i++) {
        if (pts[i].y < ymin) ymin = pts[i].y;
        if (pts[i].y > ymax) ymax = pts[i].y;
    }

    /* Crossings buffer, fixed size (no malloc in fill). */
    int16_t xs[PRIM_RASTER_MAX_PTS];

    for (int16_t y = ymin; y <= ymax; y++) {
        int16_t n = 0;
        for (int16_t i = 0, j = (int16_t)(count - 1); i < count; j = i++) {
            int16_t yi = pts[i].y, yj = pts[j].y;
            if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
                int32_t xi = pts[i].x, xj = pts[j].x;
                int32_t x = xi + (int32_t)(y - yi) * (xj - xi) / (yj - yi);
                if (n < PRIM_RASTER_MAX_PTS) xs[n++] = (int16_t)x;
            }
        }
        /* Sort crossings ascending (insertion; n is small). */
        for (int16_t a = 1; a < n; a++) {
            int16_t v = xs[a], b = (int16_t)(a - 1);
            while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; }
            xs[b + 1] = v;
        }
        for (int16_t k = 0; k + 1 < n; k += 2) {
            for (int16_t x = xs[k]; x <= xs[k + 1]; x++) {
                prim_internal_put_px(x, y, color, PRIM_BLEND_OVER);
            }
        }
    }
}
