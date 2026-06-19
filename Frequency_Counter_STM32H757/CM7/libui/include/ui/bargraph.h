#pragma once
/**
 * @file bargraph.h
 * @brief Horizontal level bar (e.g. input signal intensity) with label + value.
 */

#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    prim_rect_t  rect;         /**< full area (label/value row + track) */
    int16_t      value_pct;    /**< filled fraction, 0..100 */
    prim_color_t color;        /**< fill color of the active portion */
    const char  *label;        /**< NULL = none (top-left) */
    const char  *value_text;   /**< NULL = none (top-right, e.g. "−60 dBm") */
} ui_bargraph_t;

UI_API void ui_bargraph_render(const ui_bargraph_t *b);
