#pragma once
/**
 * @file chart.h
 * @brief Log-log chart (Allan deviation): grid, floor line, curve, cursor.
 *
 * Curve points are given in pixel coordinates of the inner rect, not in log
 * grid units — the caller does the mapping, keeping libui free of float math.
 */

#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    prim_rect_t inner;
    int16_t x_decade_count;
    int16_t y_decade_count;
    int8_t  x_min_exp;
    int8_t  y_min_exp;
    const char  *x_label;
    const char **x_tick_labels;
    const char **y_tick_labels;
} ui_chart_loglog_t;

UI_API void ui_chart_loglog_render_grid(const ui_chart_loglog_t *c);

UI_API void ui_chart_loglog_render_floor(const ui_chart_loglog_t *c,
                                         int16_t y_pixel, const char *label);

UI_API void ui_chart_loglog_render_curve(const ui_chart_loglog_t *c,
                                         const prim_point_t *points,
                                         int16_t count,
                                         prim_color_t stroke_color,
                                         bool fill_below);

UI_API void ui_chart_loglog_render_cursor(const ui_chart_loglog_t *c,
                                           prim_point_t pos, const char *label);
