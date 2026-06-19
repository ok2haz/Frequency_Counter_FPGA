/**
 * @file card.c
 * @brief Card chrome: background, border, header row.
 */

#include <ui/card.h>
#include <ui/theme.h>
#include <ui/dimensions.h>
#include <ui/fonts.h>
#include <prim/prim.h>

#define UI_CARD_HEADER_H 26

void ui_card_render_chrome(const ui_card_t *card)
{
    if (card == NULL) return;
    prim_fill_rect_rounded(card->rect, UI_DIM_CARD_RADIUS,
                           UI_COLOR_BG_CARD, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(card->rect, UI_DIM_CARD_RADIUS, 1, UI_COLOR_LINE);

    int16_t tx = (int16_t)(card->rect.x + UI_DIM_CARD_PAD_X);
    int16_t ty = (int16_t)(card->rect.y + UI_DIM_CARD_PAD_Y + 16);
    if (card->header_label) {
        prim_draw_text((prim_point_t){tx, ty}, card->header_label,
                       &ui_font_sans_20, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    }
    if (card->header_right) {
        prim_draw_text((prim_point_t){(int16_t)(card->rect.x + card->rect.w
                                                 - UI_DIM_CARD_PAD_X), ty},
                       card->header_right, &ui_font_sans_17,
                       card->header_right_accent, PRIM_ALIGN_RIGHT);
    }
}

prim_rect_t ui_card_inner_rect(const ui_card_t *card)
{
    int16_t top = UI_DIM_CARD_PAD_Y;
    if (card->header_label || card->header_right) top = (int16_t)(top + UI_CARD_HEADER_H);
    return (prim_rect_t){
        (int16_t)(card->rect.x + UI_DIM_CARD_PAD_X),
        (int16_t)(card->rect.y + top),
        (int16_t)(card->rect.w - 2 * UI_DIM_CARD_PAD_X),
        (int16_t)(card->rect.h - top - UI_DIM_CARD_PAD_Y),
    };
}
