#pragma once
/**
 * @file bezier.h
 * @brief Quadratic Bezier flattening into line segments.
 */

#include <prim/types.h>

/**
 * @brief Number of segments to flatten a quad Bezier into, chosen adaptively
 *        from the control-polygon length. Clamped to [2, max_seg].
 */
PRIM_INTERNAL int16_t prim_internal_quad_seg_count(prim_point_t a, prim_point_t c,
                                                   prim_point_t b, int16_t max_seg);

/**
 * @brief Evaluate a quadratic Bezier at parameter t in [0,256] (8.8 fixed).
 */
PRIM_INTERNAL prim_point_t prim_internal_quad_eval(prim_point_t a, prim_point_t c,
                                                   prim_point_t b, int32_t t_q8);
