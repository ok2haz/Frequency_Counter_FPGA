#pragma once
/**
 * @file card.h
 * @brief Card chrome: rounded background, border and header row.
 */

#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    prim_rect_t rect;
    const char *header_label;       /**< sans 20, INK_3; NULL = no header */
    const char *header_right;       /**< sans 17; NULL = none */
    prim_color_t header_right_accent;
} ui_card_t;

UI_API void        ui_card_render_chrome(const ui_card_t *card);
UI_API prim_rect_t ui_card_inner_rect(const ui_card_t *card);
