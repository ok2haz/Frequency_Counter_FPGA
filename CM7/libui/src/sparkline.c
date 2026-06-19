/**
 * @file sparkline.c
 * @brief Trend polyline with optional sigma band and endpoint marker.
 */

#include <ui/sparkline.h>
#include <ui/theme.h>
#include <prim/prim.h>

/* Map a normalized 0..255 sample to a y pixel inside inner (top=0). */
static inline int16_t map_y(const prim_rect_t *in, int16_t v)
{
    if (v < 0) v = 0; else if (v > 255) v = 255;
    return (int16_t)(in->y + in->h - 1 - ((int32_t)v * (in->h - 1)) / 255);
}

void ui_sparkline_render(const ui_sparkline_t *s)
{
    if (s == NULL || s->y_values == NULL || s->count < 2) return;
    const prim_rect_t *in = &s->inner;

    if (s->show_sigma_band) {
        int16_t y0 = map_y(in, s->sigma_max);   /* higher value = upper */
        int16_t y1 = map_y(in, s->sigma_min);
        prim_fill_rect((prim_rect_t){in->x, y0, in->w, (int16_t)(y1 - y0 + 1)},
                       PRIM_ALPHA(UI_COLOR_ACC, 0x20), PRIM_BLEND_OVER);
    }

    int16_t prev_x = in->x, prev_y = map_y(in, s->y_values[0]);
    for (int16_t i = 1; i < s->count; i++) {
        int16_t x = (int16_t)(in->x + (int32_t)i * (in->w - 1) / (s->count - 1));
        int16_t y = map_y(in, s->y_values[i]);
        prim_draw_line((prim_point_t){prev_x, prev_y}, (prim_point_t){x, y},
                       2, s->stroke_color);
        prev_x = x; prev_y = y;
    }

    if (s->show_endpoint_marker) {
        prim_fill_circle((prim_point_t){prev_x, prev_y}, 3, s->stroke_color);
    }
}
