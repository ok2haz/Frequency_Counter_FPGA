/**
 * @file bargraph.c
 * @brief Horizontal level bar: label/value row + a rounded segmented track.
 */

#include <ui/bargraph.h>
#include <ui/theme.h>
#include <ui/fonts.h>
#include <prim/prim.h>

#define UI_BAR_TRACK_H 10   /* tlustsi o ~20% (bylo 8) */
#define UI_BAR_RADIUS  3
#define UI_BAR_SEGS    20
#define UI_BAR_GAP     2

void ui_bargraph_render(const ui_bargraph_t *b)
{
    if (b == NULL) return;

    /* Label / value row. */
    if (b->label) {
        prim_draw_text((prim_point_t){b->rect.x, (int16_t)(b->rect.y + 12)},
                       b->label, &ui_font_sans_16, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    }
    if (b->value_text) {
        prim_draw_text((prim_point_t){(int16_t)(b->rect.x + b->rect.w),
                                      (int16_t)(b->rect.y + 12)},
                       b->value_text, &ui_font_mono_18, b->color, PRIM_ALIGN_RIGHT);
    }

    /* Track. */
    int16_t ty = (int16_t)(b->rect.y + 20);
    prim_rect_t track = {b->rect.x, ty, b->rect.w, UI_BAR_TRACK_H};
    prim_fill_rect_rounded(track, UI_BAR_RADIUS, UI_COLOR_INK_5, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(track, UI_BAR_RADIUS, 1, UI_COLOR_LINE);

    /* Segmented fill: lit segments up to value_pct. */
    int16_t pct = b->value_pct;
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    int16_t lit = (int16_t)((pct * UI_BAR_SEGS + 50) / 100);
    int16_t seg_w = (int16_t)((b->rect.w - (UI_BAR_SEGS - 1) * UI_BAR_GAP) / UI_BAR_SEGS);
    if (seg_w < 1) seg_w = 1;
    /* Zones: first 2 segments red, next 2 yellow, the rest green. */
    for (int16_t i = 0; i < lit; i++) {
        prim_color_t c = (i < 2) ? UI_COLOR_BAD
                       : (i < 4) ? UI_COLOR_WARN : UI_COLOR_OK;
        int16_t sx = (int16_t)(b->rect.x + i * (seg_w + UI_BAR_GAP));
        prim_fill_rect((prim_rect_t){(int16_t)(sx + 1), (int16_t)(ty + 2),
                                     seg_w, (int16_t)(UI_BAR_TRACK_H - 4)},
                       c, PRIM_BLEND_OVER);
    }
}
