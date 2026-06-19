#pragma once
/**
 * @file layout.h
 * @brief Private layout/style helpers shared across libui components.
 */

#include <prim/types.h>
#include <ui/theme.h>
#include <ui/digit_group.h>
#include <ui/pill.h>

/* Map a digit certainty level to its ink color. */
static inline prim_color_t ui_level_color(ui_digit_level_t level)
{
    switch (level) {
    case UI_DIGIT_CERTAIN: return UI_COLOR_INK;     /* bright — valid digits */
    case UI_DIGIT_SIGMA:   return UI_COLOR_INK_4;   /* dim grey — below σ */
    case UI_DIGIT_FLOOR:   return UI_COLOR_INK_5;   /* very dim — below floor */
    default:               return UI_COLOR_INK;
    }
}

/* Pill background / border / value colors by variant. */
typedef struct {
    prim_color_t bg;
    prim_color_t border;
    prim_color_t value;
} ui_pill_style_t;

static inline ui_pill_style_t ui_pill_style(ui_pill_variant_t v)
{
    switch (v) {
    case UI_PILL_OK:
        return (ui_pill_style_t){UI_COLOR_OK_BG, UI_COLOR_OK_BORDER, UI_COLOR_OK_SOFT};
    case UI_PILL_WARN:
        return (ui_pill_style_t){UI_COLOR_BG_CARD, UI_COLOR_LINE_HI, UI_COLOR_WARN};
    case UI_PILL_BAD:
        return (ui_pill_style_t){UI_COLOR_BG_CARD, UI_COLOR_LINE_HI, UI_COLOR_BAD};
    case UI_PILL_NORMAL:
    default:
        return (ui_pill_style_t){UI_COLOR_BG_CARD, UI_COLOR_LINE, UI_COLOR_INK};
    }
}
