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

void ui_icon_led(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    prim_glow_rect((prim_rect_t){ICON_PT(8, 8).x, ICON_PT(8, 8).y,
                                 ICON_SC(8), ICON_SC(8)}, ICON_SC(4), color, 60);
    prim_fill_circle(ICON_PT(12, 12), ICON_SC(5), color);
}

void ui_icon_menu(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    prim_draw_line(ICON_PT(4, 7),  ICON_PT(20, 7),  2, color);
    prim_draw_line(ICON_PT(4, 12), ICON_PT(20, 12), 2, color);
    prim_draw_line(ICON_PT(4, 17), ICON_PT(20, 17), 2, color);
}

void ui_icon_play(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    prim_path_t *p = prim_path_create(5);
    if (p) {
        prim_path_move_to(p, ICON_PT(7, 5));
        prim_path_line_to(p, ICON_PT(19, 12));
        prim_path_line_to(p, ICON_PT(7, 19));
        prim_path_close(p);
        prim_path_fill(p, color);
        prim_path_destroy(p);
    }
}

void ui_icon_temperature(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    prim_draw_line(ICON_PT(11, 4),  ICON_PT(11, 15), 2, color);
    prim_draw_line(ICON_PT(14, 4),  ICON_PT(14, 15), 2, color);
    prim_draw_line(ICON_PT(11, 4),  ICON_PT(14, 4),  2, color);
    prim_fill_circle(ICON_PT(12, 17), ICON_SC(4), color);
}

void ui_icon_gear(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    prim_point_t c = ICON_PT(12, 12);
    prim_draw_circle(c, ICON_SC(7), 2, color);
    prim_fill_circle(c, ICON_SC(3), color);
    for (int a = 0; a < 360; a += 60) {
        float r = a * 0.0174532925f;
        float cs = cosf(r), sn = sinf(r);
        prim_point_t i = {(int16_t)(c.x + ICON_SC(7) * cs),
                          (int16_t)(c.y - ICON_SC(7) * sn)};
        prim_point_t o = {(int16_t)(c.x + ICON_SC(10) * cs),
                          (int16_t)(c.y - ICON_SC(10) * sn)};
        prim_draw_line(i, o, 2, color);
    }
}

void ui_icon_chart_line(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    prim_draw_line(ICON_PT(4, 20), ICON_PT(4, 4),   1, color);
    prim_draw_line(ICON_PT(4, 20), ICON_PT(20, 20), 1, color);
    prim_draw_line(ICON_PT(5, 16), ICON_PT(10, 11), 2, color);
    prim_draw_line(ICON_PT(10, 11), ICON_PT(14, 14), 2, color);
    prim_draw_line(ICON_PT(14, 14), ICON_PT(20, 6),  2, color);
}

void ui_icon_chart_histo(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    prim_fill_rect((prim_rect_t){ICON_PT(5, 14).x, ICON_PT(5, 14).y,
                                 ICON_SC(3), ICON_SC(6)}, color, PRIM_BLEND_OVER);
    prim_fill_rect((prim_rect_t){ICON_PT(11, 9).x, ICON_PT(11, 9).y,
                                 ICON_SC(3), ICON_SC(11)}, color, PRIM_BLEND_OVER);
    prim_fill_rect((prim_rect_t){ICON_PT(17, 12).x, ICON_PT(17, 12).y,
                                 ICON_SC(3), ICON_SC(8)}, color, PRIM_BLEND_OVER);
}

void ui_icon_warning(prim_point_t pos, int16_t size, prim_color_t color)
{
    ICON_BEGIN(pos, size);
    prim_path_t *p = prim_path_create(5);
    if (p) {
        prim_path_move_to(p, ICON_PT(12, 4));
        prim_path_line_to(p, ICON_PT(21, 20));
        prim_path_line_to(p, ICON_PT(3, 20));
        prim_path_close(p);
        prim_path_stroke(p, 2, color);
        prim_path_destroy(p);
    }
    prim_draw_line(ICON_PT(12, 10), ICON_PT(12, 15), 2, color);
    prim_fill_circle(ICON_PT(12, 18), ICON_SC(1), color);
}
