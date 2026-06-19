#pragma once
/**
 * @file shapes.h
 * @brief Lines, circles and arcs with anti-aliasing.
 */

#include <prim/api.h>
#include <prim/types.h>

PRIM_API void prim_draw_line(prim_point_t from, prim_point_t to,
                             int16_t thickness, prim_color_t color);

PRIM_API void prim_draw_line_dashed(prim_point_t from, prim_point_t to,
                                    int16_t thickness, prim_color_t color,
                                    int16_t dash_len, int16_t gap_len);

PRIM_API void prim_draw_circle(prim_point_t center, int16_t radius,
                               int16_t thickness, prim_color_t color);

PRIM_API void prim_fill_circle(prim_point_t center, int16_t radius,
                               prim_color_t color);

PRIM_API void prim_draw_arc(prim_point_t center, int16_t radius,
                            int16_t thickness, prim_color_t color,
                            int16_t start_angle_deg, int16_t sweep_angle_deg);
