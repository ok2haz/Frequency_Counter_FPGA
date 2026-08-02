/**
 * @file bargraph.c
 * @brief Level bar: label/value row + a rounded segmented track (H or V).
 */

#include <ui/bargraph.h>
#include <ui/theme.h>
#include <ui/fonts.h>
#include <prim/prim.h>

#define UI_BAR_TRACK_H 10   /* tlustsi o ~20% (bylo 8) */
#define UI_BAR_RADIUS  3
#define UI_BAR_SEGS    20   /* default pocet segmentu (kdyz b->segs==0) */
#define UI_BAR_GAP     2
#define UI_BAR_VGAP    1    /* mezera mezi VERTIKALNIMI segmenty (mensi -> vic segmentu se vejde jemne) */

/* ── Sdilena geometrie (render i incremental update musi sedet 1:1) ─────────── */
static int16_t bar_lit_n(int16_t pct, int16_t segs)
{
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    return (int16_t)((pct * segs + 50) / 100);
}
static int16_t bar_lit(int16_t pct) { return bar_lit_n(pct, UI_BAR_SEGS); }

/* Zony HORIZONTALNIHO baru: prvni 2 segmenty cervene, dalsi 2 zlute, zbytek zelene. */
static prim_color_t bar_seg_color(int16_t i)
{
    return (i < 2) ? UI_COLOR_BAD : (i < 4) ? UI_COLOR_WARN : UI_COLOR_OK;
}

/* Obdelnik vyplne HORIZONTALNIHO segmentu i uvnitr stopy (segs segmentu). */
static prim_rect_t bar_seg_rect_h(const prim_rect_t *rect, int16_t i, int16_t segs)
{
    int16_t ty    = (int16_t)(rect->y + 20);
    int16_t seg_w = (int16_t)((rect->w - (segs - 1) * UI_BAR_GAP) / segs);
    if (seg_w < 1) seg_w = 1;
    int16_t sx = (int16_t)(rect->x + i * (seg_w + UI_BAR_GAP));
    return (prim_rect_t){ (int16_t)(sx + 1), (int16_t)(ty + 2),
                          seg_w, (int16_t)(UI_BAR_TRACK_H - 4) };
}
/* Puvodni fixni (20 segmentu) — jen pro incremental fast-path. */
static prim_rect_t bar_seg_rect(const prim_rect_t *rect, int16_t i)
{ return bar_seg_rect_h(rect, i, UI_BAR_SEGS); }

/* Obdelnik VERTIKALNIHO segmentu i (i=0 = spodni), skladano zdola nahoru.
 * Cela `rect` je stopa (bez label/value radku — text kresli volajici). */
static prim_rect_t bar_seg_rect_v(const prim_rect_t *rect, int16_t i, int16_t segs)
{
    int16_t seg_h = (int16_t)((rect->h - (segs - 1) * UI_BAR_VGAP) / segs);
    if (seg_h < 1) seg_h = 1;
    int16_t sy = (int16_t)(rect->y + rect->h - seg_h - i * (seg_h + UI_BAR_VGAP));
    return (prim_rect_t){ rect->x, sy, rect->w, seg_h };
}

/* Value text vpravo nahore (oddelene, aby slo prekreslit jen ono pri update). */
void ui_bargraph_value(const prim_rect_t *rect, const char *text, prim_color_t color)
{
    if (text == NULL) return;
    prim_draw_text((prim_point_t){(int16_t)(rect->x + rect->w), (int16_t)(rect->y + 12)},
                   text, &ui_font_mono_18, color, PRIM_ALIGN_RIGHT);
}

/* ── Vertikalni track: stopa (ram) + segmenty zdola + nominal marker ────────── */
static void bargraph_render_v(const ui_bargraph_t *b, int16_t segs)
{
    /* Stopa (podklad) — jednoduchy zaobleny ram na celou vysku. */
    prim_fill_rect_rounded(b->rect, UI_BAR_RADIUS, UI_COLOR_INK_5, PRIM_BLEND_OVER);
    prim_stroke_rect_rounded(b->rect, UI_BAR_RADIUS, 1, UI_COLOR_LINE);

    int16_t lit = bar_lit_n(b->value_pct, segs);
    for (int16_t i = 0; i < segs; i++)
        prim_fill_rect(bar_seg_rect_v(&b->rect, i, segs),
                       (i < lit) ? b->color : UI_COLOR_INK_5, PRIM_BLEND_OVER);

    /* Nominal marker: segment na nominalni hodnote zvyraznen (accent), at je
     * na prvni pohled videt odchylka aktualni hodnoty od ocekavane. */
    if (b->nominal_pct > 0) {
        int16_t ni = bar_lit_n(b->nominal_pct, segs);
        if (ni >= segs) ni = (int16_t)(segs - 1);
        prim_fill_rect(bar_seg_rect_v(&b->rect, ni, segs), UI_COLOR_ACC, PRIM_BLEND_OVER);
    }
}

void ui_bargraph_render(const ui_bargraph_t *b)
{
    if (b == NULL) return;
    int16_t segs = (b->segs > 0) ? b->segs : UI_BAR_SEGS;

    if (b->vertical) { bargraph_render_v(b, segs); return; }

    /* ── HORIZONTALNI (default, puvodni chovani) ────────────────────────────── */
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
    int16_t lit = bar_lit_n(b->value_pct, segs);
    for (int16_t i = 0; i < lit; i++)
        prim_fill_rect(bar_seg_rect_h(&b->rect, i, segs), bar_seg_color(i), PRIM_BLEND_OVER);

    /* Nominal marker (i horizontalne — zvyrazneny segment na ocekavane hodnote). */
    if (b->nominal_pct > 0) {
        int16_t ni = bar_lit_n(b->nominal_pct, segs);
        if (ni >= segs) ni = (int16_t)(segs - 1);
        prim_fill_rect(bar_seg_rect_h(&b->rect, ni, segs), UI_COLOR_ACC, PRIM_BLEND_OVER);
    }
}

/* Incremental update: prekresli JEN segmenty, ktere se mezi old_pct a new_pct
 * zmenily (rozsvitily/zhasly). Stopa, ramecek ani ostatni segmenty se netknou ->
 * misto ~20 fillu jen rozdil (typicky 1). Value text si volajici prekresli sam
 * (ui_bargraph_value). Vraci pocet zmenenych segmentu (0 = neflipovat).
 * ⚠️ Jen HORIZONTALNI mod, fixnich 20 segmentu (fast-path). */
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
