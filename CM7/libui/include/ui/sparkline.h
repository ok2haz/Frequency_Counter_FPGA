#pragma once
/**
 * @file sparkline.h
 * @brief Compact trend line with optional sigma band and endpoint marker.
 *
 * Values are integer-normalized 0..255 (no float — faster, deterministic).
 */

#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    prim_rect_t inner;
    const int16_t *y_values;        /**< normalized 0..255 */
    int16_t count;
    bool show_sigma_band;
    int16_t sigma_min;              /**< 0..255 */
    int16_t sigma_max;
    bool show_endpoint_marker;
    bool fill_below;
    prim_color_t stroke_color;
} ui_sparkline_t;

UI_API void ui_sparkline_render(const ui_sparkline_t *s);
