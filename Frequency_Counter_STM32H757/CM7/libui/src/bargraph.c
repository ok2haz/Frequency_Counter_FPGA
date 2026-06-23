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

/* ── Sdilena geometrie (render i incremental update musi sedet 1:1) ─────────── */
static int16_t bar_lit(int16_t pct)
{
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    return (int16_t)((pct * UI_BAR_SEGS + 50) / 100);
}

/* Zony: prvni 2 segmenty cervene, dalsi 2 zlute, zbytek zelene. */
static prim_color_t bar_seg_color(int16_t i)
{
    return (i < 2) ? UI_COLOR_BAD : (i < 4) ? UI_COLOR_WARN : UI_COLOR_OK;
}

/* Obdelnik vyplne segmentu i uvnitr stopy. */
static prim_rect_t bar_seg_rect(const prim_rect_t *rect, int16_t i)
{
    int16_t ty    = (int16_t)(rect->y + 20);
    int16_t seg_w = (int16_t)((rect->w - (UI_BAR_SEGS - 1) * UI_BAR_GAP) / UI_BAR_SEGS);
    if (seg_w < 1) seg_w = 1;
    int16_t sx = (int16_t)(rect->x + i * (seg_w + UI_BAR_GAP));
    return (prim_rect_t){ (int16_t)(sx + 1), (int16_t)(ty + 2),
                          seg_w, (int16_t)(UI_BAR_TRACK_H - 4) };
}

/* Value text vpravo nahore (oddelene, aby slo prekreslit jen ono pri update). */
void ui_bargraph_value(const prim_rect_t *rect, const char *text, prim_color_t color)
{
    if (text == NULL) return;
    prim_draw_text((prim_point_t){(int16_t)(rect->x + rect->w), (int16_t)(rect->y + 12)},
                   text, &ui_font_mono_18, color, PRIM_ALIGN_RIGHT);
}

void ui_bargraph_render(const ui_bargraph_t *b)
{
    if (b == NULL) return;

    /* Label / value row. */
    if (b->label) {
        prim_draw_text((prim_point_t){b->rect.x, (int16_t)(b->rect.y + 12)},
                       b->label, &ui_font_sans_16, UI_COLOR_INK_3, PRIM_ALIGN_LEFT);
    }
    ui_bargraph_value(&b->rect, b->value_text, b->color);

    /* Track. */
    int16_t ty = (int16_t)(b->rect.y + 20);
    prim_rect_t track = {b->rect.x, ty, b->rect.w, UI_BAR_TRACK_H};
    prim_fill_rect_rounded(track, UI_BAR_RADIUS, UI_COLOR_INK_5, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(track, UI_BAR_RADIUS, 1, UI_COLOR_LINE);

    /* Segmented fill: lit segments up to value_pct. */
    int16_t lit = bar_lit(b->value_pct);
    for (int16_t i = 0; i < lit; i++)
        prim_fill_rect(bar_seg_rect(&b->rect, i), bar_seg_color(i), PRIM_BLEND_OVER);
}

/* Incremental update: prekresli JEN segmenty, ktere se mezi old_pct a new_pct
 * zmenily (rozsvitily/zhasly). Stopa, ramecek ani ostatni segmenty se netknou ->
 * misto ~20 fillu jen rozdil (typicky 1). Value text si volajici prekresli sam
 * (ui_bargraph_value). Vraci pocet zmenenych segmentu (0 = neflipovat). */
int ui_bargraph_update(const prim_rect_t *rect, int16_t old_pct, int16_t new_pct)
{
    int16_t o = bar_lit(old_pct), n = bar_lit(new_pct);
    if (n > o)                                   /* rozsvitit [o..n) */
        for (int16_t i = o; i < n; i++)
            prim_fill_rect(bar_seg_rect(rect, i), bar_seg_color(i), PRIM_BLEND_OVER);
    else if (n < o)                              /* zhasnout [n..o) -> barva stopy */
        for (int16_t i = n; i < o; i++)
            prim_fill_rect(bar_seg_rect(rect, i), UI_COLOR_INK_5, PRIM_BLEND_OVER);
    return (n > o) ? (n - o) : (o - n);
}
