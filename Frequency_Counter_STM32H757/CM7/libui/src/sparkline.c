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

/* Vypln pod polyline (area chart): per-sloupec svisly gradient stroke_color —
 * nejsilnejsi u cary, slabne dolu k pruhlednu ("fade-to-transparent"). Kresli se
 * PRED polyline (cara jde nahoru). Zapina volajici pres s->fill_below. Bez
 * float, alfa OVER (CPU fill). */
#define SPARK_FILL_BANDS  3     /* 3 pasma (bylo 5) — mene fillu/sloupec = svizneji */
#define SPARK_FILL_ALPHA  0x3C
static void spark_fill_below(const ui_sparkline_t *s)
{
    const prim_rect_t *in = &s->inner;
    int16_t bottom = (int16_t)(in->y + in->h - 1);
    int16_t px = in->x, py = map_y(in, s->y_values[0]);
    for (int16_t i = 1; i < s->count; i++) {
        int16_t x = (int16_t)(in->x + (int32_t)i * (in->w - 1) / (s->count - 1));
        int16_t y = map_y(in, s->y_values[i]);
        int16_t cols = (int16_t)(x - px);
        if (cols < 1) { px = x; py = y; continue; }
        for (int16_t c = 0; c < cols; c++) {
            int16_t cx = (int16_t)(px + c);
            int16_t ly = (int16_t)(py + (int32_t)(y - py) * c / cols);   /* interpolace cary */
            int16_t h  = (int16_t)(bottom - ly);
            if (h <= 0) continue;
            for (int b = 0; b < SPARK_FILL_BANDS; b++) {
                int16_t y0 = (int16_t)(ly + (int32_t)b * h / SPARK_FILL_BANDS);
                int16_t y1 = (int16_t)(ly + (int32_t)(b + 1) * h / SPARK_FILL_BANDS);
                if (y1 <= y0) continue;
                uint8_t a = (uint8_t)(SPARK_FILL_ALPHA * (SPARK_FILL_BANDS - b) / SPARK_FILL_BANDS);
                prim_fill_rect((prim_rect_t){cx, y0, 1, (int16_t)(y1 - y0)},
                               PRIM_ALPHA(s->stroke_color, a), PRIM_BLEND_OVER);
            }
        }
        px = x; py = y;
    }
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

    if (s->fill_below) spark_fill_below(s);

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
