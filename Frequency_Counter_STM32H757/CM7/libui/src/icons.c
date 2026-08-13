/**
 * @file icons.c
 * @brief Vector icons drawn purely from libprim primitives (24x24 viewbox).
 */

#include <ui/icons.h>
#include <prim/prim.h>
#include <math.h>

/* Per-call icon origin/size used by the ICON_PT/ICON_SC macros. */
static prim_point_t g_icon_pos;
static int16_t      g_icon_size;

#include "internal/icon_helpers.h"

#define ICON_BEGIN(pos, size) do { g_icon_pos = (pos); g_icon_size = (size); } while (0)

/* Satellite: a chain of solar-array panels along the ↗ diagonal plus signal
 * waves radiating to the bottom-right (matches GNSS_icon.png). */
void ui_icon_sat_dish(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    static const int16_t panel[3][8] = {
        {13, 7, 18, 2, 22, 6, 17, 11},   /* upper-right */
        { 9, 11, 14, 6, 18, 10, 13, 15}, /* middle body */
        { 3, 17,  8, 12, 12, 16,  7, 21},/* lower-left */
    };
    for (int k = 0; k < 3; k++) {
        prim_path_t *p = prim_path_create(6);
        if (!p) continue;
        prim_path_move_to(p, ICON_PT(panel[k][0], panel[k][1]));
        prim_path_line_to(p, ICON_PT(panel[k][2], panel[k][3]));
        prim_path_line_to(p, ICON_PT(panel[k][4], panel[k][5]));
        prim_path_line_to(p, ICON_PT(panel[k][6], panel[k][7]));
        prim_path_close(p);
        prim_path_fill(p, color);
        prim_path_destroy(p);
    }
    prim_point_t c = ICON_PT(10, 14);
    prim_draw_arc(c, ICON_SC(6),  1, color, -78, 72);
    prim_draw_arc(c, ICON_SC(9),  1, color, -78, 72);
    prim_draw_arc(c, ICON_SC(12), 1, color, -78, 72);
}

/* Reproduktor: magnet (rect) + kuzel (trapez). Sdilene telo pro on/muted. */
static void speaker_body(prim_color_t color)
{
    prim_fill_rect((prim_rect_t){ICON_PT(3, 9).x, ICON_PT(3, 9).y,
                                 ICON_SC(4), ICON_SC(6)}, color, PRIM_BLEND_OVER);
    prim_path_t *p = prim_path_create(5);
    if (p) {
        prim_path_move_to(p, ICON_PT(7, 9));
        prim_path_line_to(p, ICON_PT(12, 4));
        prim_path_line_to(p, ICON_PT(12, 20));
        prim_path_line_to(p, ICON_PT(7, 15));
        prim_path_close(p);
        prim_path_fill(p, color);
        prim_path_destroy(p);
    }
}

/* Reproduktor se zvukovymi vlnami (zvuk zapnut). */
void ui_icon_speaker(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    speaker_body(color);
    prim_draw_arc(ICON_PT(12, 12), ICON_SC(5), 2, color, -55, 55);
    prim_draw_arc(ICON_PT(12, 12), ICON_SC(8), 2, color, -55, 55);
}

/* Preskrtnuty reproduktor (zvuk vypnut) — schematicka znacka mute. */
void ui_icon_speaker_muted(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    speaker_body(color);
    prim_draw_line(ICON_PT(3, 4), ICON_PT(21, 21), 2, color);   /* diagonalni preskrtnuti */
}
