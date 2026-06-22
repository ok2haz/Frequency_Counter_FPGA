#pragma once
/**
 * @file big_number.h
 * @brief Large frequency readout: digit groups + separators + decimal + unit.
 */

#include <prim/text.h>
#include <ui/digit_group.h>
#include <ui/api.h>

typedef struct {
    int16_t x_center;
    int16_t y_baseline;
    const prim_font_t *main_font;    /**< valid digits, e.g. ui_font_mono_75 */
    const prim_font_t *fade_font;    /**< invalid digits (σ/floor), smaller; NULL = main_font */
    const prim_font_t *sep_font;     /**< e.g. ui_font_mono_25 (thousands dots) */
    const prim_font_t *decimal_font; /**< e.g. ui_font_mono_30 (comma) */
    const prim_font_t *unit_font;    /**< e.g. ui_font_sans_20 (Hz) */
    const ui_digit_segment_t *segments;
    int16_t segment_count;
    const char *separators;          /**< e.g. "··,·" — one per gap */
    prim_color_t sep_color;
    prim_color_t decimal_color;
    const char *unit;
    prim_color_t unit_color;
} ui_big_number_t;

UI_API void ui_big_number_render(const ui_big_number_t *n);

/** Celkova sirka cisla (vc. separatoru a jednotky) — pro clear oblast pri partial redraw. */
UI_API int16_t ui_big_number_width(const ui_big_number_t *n);

/** x, kde zacina segment 'from' (pro partial redraw koncovych cislic). */
UI_API int16_t ui_big_number_seg_x(const ui_big_number_t *n, int16_t from);

/** Vykresli jen segmenty [from..konec] + jednotku (partial redraw menicich se cislic). */
UI_API void ui_big_number_render_tail(const ui_big_number_t *n, int16_t from);
