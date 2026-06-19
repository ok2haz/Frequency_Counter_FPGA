#pragma once
/**
 * @file glow.h
 * @brief Soft glow around rectangles and lines (separable box blur).
 */

#include <prim/api.h>
#include <prim/types.h>

PRIM_API void prim_glow_rect(prim_rect_t rect, int16_t blur_radius,
                             prim_color_t color, uint8_t intensity_pct);

PRIM_API void prim_glow_line(prim_point_t from, prim_point_t to,
                             int16_t blur_radius, prim_color_t color,
                             uint8_t intensity_pct);
