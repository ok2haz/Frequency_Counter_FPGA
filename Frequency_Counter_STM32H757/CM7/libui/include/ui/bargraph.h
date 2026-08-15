#pragma once
/**
 * @file bargraph.h
 * @brief Level bar (e.g. input signal intensity) with label + value.
 *
 * Dva mody (TODO #32):
 *  - HORIZONTALNI (default) — puvodni chovani, barevne zony (2 cervene / 2 zlute
 *    / zbytek zelene), label vlevo nahore + value vpravo nahore. Nedotcene: main
 *    screen RF bargraf i incremental fast-path ui_bargraph_update() jedou dal.
 *  - VERTIKALNI (`vertical=1`) — segmenty se skladaji zdola nahoru, vyplni se
 *    JEDNOTNOU barvou `color` (ne zony), text NEkresli (label/value si umistuje
 *    volajici okolo uzkeho sloupce). Vhodne pro husty sloupec vic barů (okno Grafy).
 *
 * `segs` (0 = default 20) — jemnejsi granularita mereni.
 * `nominal_pct` (<=0 = zadny) — segment na nominalni/prumerne hodnote se vykresli
 * ZVYRAZNENE (accent), aby byla odchylka od ocekavane hodnoty videt na prvni pohled.
 */

#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    prim_rect_t  rect;         /**< full area (label/value row + track) */
    int16_t      value_pct;    /**< filled fraction, 0..100 */
    prim_color_t color;        /**< horizontal: fill color of value text; vertical: lit segment color */
    const char  *label;        /**< NULL = none (top-left); ignorovano ve vertical modu */
    const char  *value_text;   /**< NULL = none (top-right); ignorovano ve vertical modu */
    uint8_t      vertical;     /**< 0 = horizontalni (default), 1 = vertikalni */
    int16_t      segs;         /**< pocet segmentu; 0 = default (20) */
    int16_t      nominal_pct;  /**< <=0 = zadny; jinak segment na te hodnote zvyraznen (accent) */
} ui_bargraph_t;

UI_API void ui_bargraph_render(const ui_bargraph_t *b);

/** Vykresli jen value text (vpravo nahore) — pro partial update bez full renderu. */
UI_API void ui_bargraph_value(const prim_rect_t *rect, const char *text, prim_color_t color);

/** Incremental: prekresli jen segmenty zmenene mezi old_pct a new_pct (rozsvic/zhasni).
 *  Stopa/ramecek/value text se netknou. Vraci pocet zmenenych segmentu.
 *  ⚠️ Jen HORIZONTALNI mod, fixnich 20 segmentu (fast-path main screen / anim demo). */
UI_API int ui_bargraph_update(const prim_rect_t *rect, int16_t old_pct, int16_t new_pct);
