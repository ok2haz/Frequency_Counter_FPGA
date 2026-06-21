/**
 * @file button.c
 * @brief Footer button with gradient body, border and 1-2 text lines.
 */

#include <ui/button.h>
#include <ui/theme.h>
#include <ui/dimensions.h>
#include <ui/fonts.h>
#include <prim/prim.h>

typedef struct { prim_color_t top, bot, border, ink; } btn_style_t;

static btn_style_t style_of(ui_button_variant_t v)
{
    switch (v) {
    case UI_BUTTON_RUN:
        return (btn_style_t){UI_COLOR_BTN_RUN_TOP, UI_COLOR_BTN_RUN_BOT,
                             UI_COLOR_BTN_RUN_BORDER, UI_COLOR_OK_SOFT};
    case UI_BUTTON_ACTIVE:
        return (btn_style_t){UI_COLOR_BTN_ACT_TOP, UI_COLOR_BTN_ACT_BOT,
                             UI_COLOR_BTN_ACT_BORDER, UI_COLOR_ACC};
    case UI_BUTTON_NORMAL:
    default:
        return (btn_style_t){UI_COLOR_BG_1, UI_COLOR_BG_0,
                             UI_COLOR_LINE, UI_COLOR_INK_2};
    }
}

void ui_button_render(const ui_button_t *btn)
{
    if (btn == NULL) return;
    btn_style_t st = style_of(btn->variant);

    /* Solid rounded body (a rectangular gradient overlay would bleed square
     * corners past the rounded shape), then border. */
    prim_fill_rect_rounded(btn->rect, UI_DIM_BUTTON_RADIUS, st.top, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(btn->rect, UI_DIM_BUTTON_RADIUS, 1, st.border);

    int16_t cx = (int16_t)(btn->rect.x + btn->rect.w / 2);
    if (btn->value == NULL) {
        /* Jednoradkove tlacitko: popisek mono_22 (mono_20 +10%). */
        int16_t by = (int16_t)(btn->rect.y + btn->rect.h / 2 + 8);
        prim_draw_text((prim_point_t){cx, by}, btn->label,
                       &ui_font_mono_22, st.ink, PRIM_ALIGN_CENTER);
    } else {
        /* Dvouradkove: popisek sans_18 + hodnota mono_22 (+10%). */
        prim_draw_text((prim_point_t){cx, (int16_t)(btn->rect.y + btn->rect.h / 2 - 3)},
                       btn->label, &ui_font_sans_18, UI_COLOR_INK_3, PRIM_ALIGN_CENTER);
        prim_draw_text((prim_point_t){cx, (int16_t)(btn->rect.y + btn->rect.h / 2 + 18)},
                       btn->value, &ui_font_mono_22, st.ink, PRIM_ALIGN_CENTER);
    }
}
