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
