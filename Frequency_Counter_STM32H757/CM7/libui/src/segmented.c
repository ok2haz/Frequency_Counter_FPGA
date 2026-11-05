/**
 * @file segmented.c
 * @brief Viz segmented.h — segmentovy prepinac N segmentu.
 */

#include <ui/segmented.h>
#include <ui/theme.h>
#include <ui/fonts.h>
#include <prim/prim.h>

/* Levy okraj segmentu i (rovnomerne deleni bez driftu zaokrouhleni). */
static int16_t seg_x(const ui_segmented_t *sc, int i)
{
    return (int16_t)(sc->rect.x + (int32_t)sc->rect.w * i / sc->n);
}

static ui_segmented_observer_t s_seg_obs;

void ui_segmented_set_observer(ui_segmented_observer_t obs) { s_seg_obs = obs; }

void ui_segmented_render(const ui_segmented_t *sc)
{
    if (sc == NULL || sc->n < 2) return;
    int16_t rad = 12;   /* mirne zaobleni (NE stadium h/2) -> vyber vypada stejne na kraji i uprostred */

    /* Track. */
    prim_fill_rect_rounded(sc->rect, rad, UI_COLOR_BG_0, PRIM_BLEND_OVER);

    /* Oddelovace na VSECH vnitrnich hranach — konzistentni bez ohledu na vyber
     * (drivejsi "jen mezi nevybranymi" hrany pribliky/mizely = nekonzistentni). */
    for (int i = 1; i < sc->n; i++)
        prim_fill_rect((prim_rect_t){seg_x(sc, i), (int16_t)(sc->rect.y + 8), 1,
                                     (int16_t)(sc->rect.h - 16)}, UI_COLOR_LINE, PRIM_BLEND_OVER);

    int16_t by = (int16_t)(sc->rect.y + sc->rect.h / 2 + 6);   /* baseline mono_16 ~centr */
    for (int i = 0; i < sc->n; i++) {
        int16_t x0 = seg_x(sc, i), x1 = seg_x(sc, i + 1);
        int16_t cx = (int16_t)((x0 + x1) / 2);
        prim_color_t ink = UI_COLOR_INK_3;
        if (i == sc->selected) {
            /* Vybrany: inset accent vypln (rad 8) — floating uvnitr segmentu, stejne na kazde pozici. */
            prim_rect_t inner = {(int16_t)(x0 + 3), (int16_t)(sc->rect.y + 3),
                                 (int16_t)(x1 - x0 - 6), (int16_t)(sc->rect.h - 6)};
            prim_fill_rect_rounded(inner, 8, UI_COLOR_ACC, PRIM_BLEND_OVER);
            ink = UI_COLOR_BG_0;
        }
        prim_draw_text((prim_point_t){cx, by}, sc->labels[i],
                       &ui_font_mono_16, ink, PRIM_ALIGN_CENTER);
        /* Registrace segmentu pro fokus encoderu (viz `ui_segmented_set_observer`). */
        if (s_seg_obs != NULL) {
            prim_rect_t sr = {x0, sc->rect.y, (int16_t)(x1 - x0), sc->rect.h};
            s_seg_obs(&sr);
        }
    }

    prim_stroke_rect_rounded(sc->rect, rad, 1, UI_COLOR_LINE);   /* obrys tracku */
}

int ui_segmented_hit(const ui_segmented_t *sc, int16_t x, int16_t y)
{
    if (sc == NULL) return -1;
    if (x < sc->rect.x || x >= sc->rect.x + sc->rect.w ||
        y < sc->rect.y || y >= sc->rect.y + sc->rect.h) return -1;
    for (int i = 0; i < sc->n; i++) {
        if (x < seg_x(sc, i + 1)) return i;
    }
    return sc->n - 1;
}
