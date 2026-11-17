#pragma once
/**
 * @file segmented.h
 * @brief Horizontalni segmentovy prepinac (N segmentu, jeden vybrany).
 *
 * Vzhled = "track" (zaobleny pruh BG_0 + obrys LINE) s vybranym segmentem
 * podbarvenym accentem. Text vybraneho = BG_0 -> kontrast v OBOU tematech
 * (tmave: tmave bg0 na jasnem acc; svetle: svetle bg0 na tmavem acc). Nevybrane
 * segmenty maji text INK_3 + jemny oddelovac. Bezstavovy — vybrany index drzi
 * volajici (typicky prepinac rezimu, napr. ADEV/TDEV/MTIE nebo LIN/LOG).
 */

#include <stdint.h>
#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    prim_rect_t rect;              /**< cely pruh */
    const char *const *labels;     /**< pole `n` popisku */
    uint8_t n;                     /**< pocet segmentu (2..6) */
    uint8_t selected;              /**< index vybraneho (0..n-1) */
} ui_segmented_t;

/** Vykresli cely prepinac. */
UI_API void ui_segmented_render(const ui_segmented_t *sc);

/** Index segmentu pod bodem (x,y), nebo -1 mimo pruh. */
UI_API int  ui_segmented_hit(const ui_segmented_t *sc, int16_t x, int16_t y);

/* 🔴 Pozorovatel renderu — vola se pro KAZDY SEGMENT zvlast (stejny vzor jako
 * `ui_button_set_observer`). App si z nej stavi seznam zameritelnych obdelniku
 * pro encoder.
 *
 * PROC to musi byt PER SEGMENT, ne cely prepinac: aktivace jde pres dotyk na
 * STRED zamereneho obdelniku, a `ui_segmented_hit` mapuje x na segment. Kdyby se
 * registroval cely track, stred by trefil vzdy prostredni segment (u ADEV/TDEV/MTIE
 * porad jen TDEV).
 *
 * PROC to vubec je: segmentove prepinace NEJDOU pres `ui_button_render`, takze
 * do registru fokusu nespadly a encoder je NEDOSAHL — v okne ALLAN tim zustaly
 * nedostupne obe zalozky i prepinac metriky (nalezeno 2026-09-01). */
typedef void (*ui_segmented_observer_t)(const prim_rect_t *seg_rect);
UI_API void ui_segmented_set_observer(ui_segmented_observer_t obs);
