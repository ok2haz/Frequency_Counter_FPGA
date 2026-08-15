#pragma once
/**
 * @file button.h
 * @brief Footer button: gradient body, border, one or two text lines.
 */

#include <prim/types.h>
#include <ui/api.h>

typedef enum {
    UI_BUTTON_NORMAL,
    UI_BUTTON_ACTIVE,
    UI_BUTTON_RUN,
    UI_BUTTON_STOP,   /**< cervena — destruktivni/zastavujici akce (footer STOP) */
} ui_button_variant_t;

typedef struct {
    prim_rect_t rect;
    ui_button_variant_t variant;
    const char *label;
    const char *value;              /**< NULL = single line; else two lines */
} ui_button_t;

UI_API void ui_button_render(const ui_button_t *btn);
