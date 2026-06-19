/**
 * @file pill.c
 * @brief Status pill component.
 */

#include <ui/pill.h>
#include <ui/theme.h>
#include <ui/dimensions.h>
#include <ui/fonts.h>
#include <prim/prim.h>
#include "internal/layout.h"

int16_t ui_pill_measure(const ui_pill_t *pill)
{
    if (pill == NULL) return 0;
    int16_t w = 2 * UI_DIM_PILL_PAD_X;
    bool has_glyph = pill->has_led || (pill->icon_render != NULL);
    if (has_glyph) {
        int16_t s = pill->has_led ? UI_DIM_PILL_INNER_GAP + 8
                                  : pill->icon_size;
        w = (int16_t)(w + s + UI_DIM_PILL_INNER_GAP);
    }
    if (pill->label) {
        w = (int16_t)(w + prim_text_width(pill->label, &ui_font_mono_14)
                        + UI_DIM_PILL_INNER_GAP);
    }
    if (pill->value) {
        w = (int16_t)(w + prim_text_width(pill->value, &ui_font_mono_16));
    }
    return w;
}

void ui_pill_render(ui_pill_t *pill)
{
    if (pill == NULL) return;
    ui_pill_style_t st = ui_pill_style(pill->variant);
    int16_t w = ui_pill_measure(pill);
    pill->computed_width = w;

    prim_rect_t r = {pill->x, pill->y, w, UI_DIM_PILL_H};
    prim_fill_rect_rounded(r, UI_DIM_PILL_RADIUS, st.bg, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(r, UI_DIM_PILL_RADIUS, 1, st.border);

    int16_t cx = (int16_t)(pill->x + UI_DIM_PILL_PAD_X);
    int16_t cy_mid = (int16_t)(pill->y + UI_DIM_PILL_H / 2);

    if (pill->has_led) {
        prim_point_t c = {(int16_t)(cx + 4), cy_mid};
        prim_glow_rect((prim_rect_t){(int16_t)(c.x - 3), (int16_t)(c.y - 3), 6, 6},
                       4, UI_COLOR_OK, 60);
        prim_fill_circle(c, 4, UI_COLOR_OK_SOFT);
        cx = (int16_t)(cx + 8 + UI_DIM_PILL_INNER_GAP);
    } else if (pill->icon_render) {
        pill->icon_render((prim_point_t){cx, (int16_t)(cy_mid - pill->icon_size / 2)},
                          pill->icon_size, pill->icon_color);
        cx = (int16_t)(cx + pill->icon_size + UI_DIM_PILL_INNER_GAP);
    }

    int16_t text_y = (int16_t)(cy_mid + 5);   /* approx baseline */
    if (pill->label) {
        prim_draw_text((prim_point_t){cx, text_y}, pill->label,
                       &ui_font_mono_14, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
        cx = (int16_t)(cx + prim_text_width(pill->label, &ui_font_mono_14)
                         + UI_DIM_PILL_INNER_GAP);
    }
    if (pill->value) {
        prim_draw_text((prim_point_t){cx, text_y}, pill->value,
                       &ui_font_mono_16, st.value, PRIM_ALIGN_LEFT);
    }
}
