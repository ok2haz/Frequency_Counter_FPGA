/**
 * @file big_number.c
 * @brief Large frequency readout: groups + thousands/decimal separators + unit.
 *
 * Lays out the digit segments centered on x_center, interleaving the separator
 * characters from `separators` between groups, then appends the unit.
 */

#include <ui/big_number.h>
#include <ui/theme.h>
#include <prim/prim.h>
#include <string.h>
#include "internal/layout.h"

/* Font for a segment: smaller fade_font for invalid (σ/floor) digits. */
static const prim_font_t *seg_font(const ui_big_number_t *n, ui_digit_level_t lv)
{
    return (lv != UI_DIGIT_CERTAIN && n->fade_font) ? n->fade_font : n->main_font;
}

/* Separator char for segment index i, or 0 if none (bounds-checked).
 * `UI_BIGNUM_SEP_NONE` = explicitni "zadny separator" uprostred retezce. */
static char sep_at(const ui_big_number_t *n, int16_t i)
{
    if (!n->separators) return 0;
    if ((size_t)i >= strlen(n->separators)) return 0;
    char c = n->separators[i];
    return (c == UI_BIGNUM_SEP_NONE) ? 0 : c;
}

/* Total advance width with separators and a trailing unit. */
static int16_t total_width(const ui_big_number_t *n)
{
    int32_t w = 0;
    for (int16_t i = 0; i < n->segment_count; i++) {
        w += prim_text_width(n->segments[i].text,
                             seg_font(n, n->segments[i].level));
        char s = sep_at(n, i);
        if (s) {
            char tmp[2] = {s, 0};
            const prim_font_t *f = (s == ',') ? n->decimal_font : n->sep_font;
            w += prim_text_width(tmp, f);
        }
    }
    if (n->unit) w += prim_text_width(n->unit, n->unit_font) + 8;
    return (int16_t)w;
}

/* Celkova sirka cisla (vc. separatoru a jednotky) — pro vypocet clear oblasti
 * pri partial prekresleni (simulovany kmitocet). */
int16_t ui_big_number_width(const ui_big_number_t *n)
{
    return n ? total_width(n) : 0;
}

/* x, kde zacina segment 'from' (po naskladani segmentu+separatoru 0..from-1). */
int16_t ui_big_number_seg_x(const ui_big_number_t *n, int16_t from)
{
    if (n == NULL) return 0;
    int16_t x = (int16_t)(n->x_center - total_width(n) / 2);
    for (int16_t i = 0; i < from && i < n->segment_count; i++) {
        x = (int16_t)(x + prim_text_width(n->segments[i].text,
                                          seg_font(n, n->segments[i].level)));
        char s = sep_at(n, i);
        if (s) {
            char tmp[2] = {s, 0};
            const prim_font_t *f = (s == ',') ? n->decimal_font : n->sep_font;
            x = (int16_t)(x + prim_text_width(tmp, f));
        }
    }
    return x;
}

/* Vykresli JEN segmenty [from..konec] + jednotku (pro partial redraw menicich se
 * koncovych cislic). Pozice se napocita pres preskocene segmenty (seg_x). */
void ui_big_number_render_tail(const ui_big_number_t *n, int16_t from)
{
    if (n == NULL) return;
    if (from < 0) from = 0;
    int16_t x = ui_big_number_seg_x(n, from);
    int16_t y = n->y_baseline;

    for (int16_t i = from; i < n->segment_count; i++) {
        const ui_digit_segment_t *seg = &n->segments[i];
        const prim_font_t *f = seg_font(n, seg->level);
        prim_draw_text((prim_point_t){x, y}, seg->text, f, ui_level_color(seg->level),
                       PRIM_ALIGN_LEFT);
        int16_t sw = prim_text_width(seg->text, f);
        if (seg->with_underline) {
            int16_t uy = (int16_t)(y + 8);
            prim_glow_line((prim_point_t){x, uy}, (prim_point_t){(int16_t)(x + sw), uy},
                           6, UI_COLOR_ACC, 70);
            prim_draw_line((prim_point_t){x, uy},
                           (prim_point_t){(int16_t)(x + sw), uy}, 3, UI_COLOR_ACC);
        }
        x = (int16_t)(x + sw);
        char s = sep_at(n, i);
        if (s) {
            char tmp[2] = {s, 0};
            bool is_decimal = (s == ',');
            const prim_font_t *sf = is_decimal ? n->decimal_font : n->sep_font;
            prim_color_t c = is_decimal ? n->decimal_color : n->sep_color;
            prim_draw_text((prim_point_t){x, y}, tmp, sf, c, PRIM_ALIGN_LEFT);
            x = (int16_t)(x + prim_text_width(tmp, sf));
        }
    }
    if (n->unit) {
        prim_draw_text((prim_point_t){(int16_t)(x + 8), y}, n->unit,
                       n->unit_font, n->unit_color, PRIM_ALIGN_LEFT);
    }
}

void ui_big_number_render(const ui_big_number_t *n)
{
    ui_big_number_render_tail(n, 0);   /* cele cislo = ocas od segmentu 0 */
}
