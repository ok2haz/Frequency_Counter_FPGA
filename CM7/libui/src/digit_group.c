/**
 * @file digit_group.c
 * @brief Digit segments at certainty levels with an accent underline + glow.
 */

#include <ui/digit_group.h>
#include <ui/theme.h>
#include <prim/prim.h>
#include "internal/layout.h"

int16_t ui_digit_group_width(const ui_digit_group_t *g)
{
    if (g == NULL) return 0;
    int32_t w = 0;
    for (int16_t i = 0; i < g->segment_count; i++) {
        w += prim_text_width(g->segments[i].text, g->font);
    }
    return (int16_t)w;
}

void ui_digit_group_render(const ui_digit_group_t *g)
{
    if (g == NULL) return;
    int16_t x = g->x;
    for (int16_t i = 0; i < g->segment_count; i++) {
        const ui_digit_segment_t *seg = &g->segments[i];
        prim_color_t col = ui_level_color(seg->level);
        prim_draw_text((prim_point_t){x, g->y}, seg->text, g->font,
                       col, PRIM_ALIGN_LEFT);
        int16_t sw = prim_text_width(seg->text, g->font);
        if (seg->with_underline) {
            int16_t uy = (int16_t)(g->y + 4);
            prim_glow_line((prim_point_t){x, uy}, (prim_point_t){(int16_t)(x + sw), uy},
                           4, UI_COLOR_ACC, 70);
            prim_draw_line((prim_point_t){x, uy},
                           (prim_point_t){(int16_t)(x + sw), uy}, 2, UI_COLOR_ACC);
        }
        x = (int16_t)(x + sw);
    }
}
