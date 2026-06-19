/**
 * @file bezier.c
 * @brief Quadratic Bezier flattening (8.8 fixed-point parameter).
 */

#include "internal/bezier.h"
#include <stdlib.h>

int16_t prim_internal_quad_seg_count(prim_point_t a, prim_point_t c,
                                     prim_point_t b, int16_t max_seg)
{
    /* Estimate from control-polygon length: |a-c| + |c-b|. */
    int32_t l = abs(c.x - a.x) + abs(c.y - a.y) +
                abs(b.x - c.x) + abs(b.y - c.y);
    int16_t n = (int16_t)(l / 6 + 2);     /* ~1 segment per 6 px */
    if (n < 2) n = 2;
    if (n > max_seg) n = max_seg;
    return n;
}

prim_point_t prim_internal_quad_eval(prim_point_t a, prim_point_t c,
                                     prim_point_t b, int32_t t_q8)
{
    int32_t u = 256 - t_q8;
    /* B(t) = u^2*a + 2*u*t*c + t^2*b, all in 16.16 then >>16. */
    int32_t uu = u * u;        /* 16.16 */
    int32_t tt = t_q8 * t_q8;
    int32_t ut2 = 2 * u * t_q8;
    int32_t x = (uu * a.x + ut2 * c.x + tt * b.x) >> 16;
    int32_t y = (uu * a.y + ut2 * c.y + tt * b.y) >> 16;
    return (prim_point_t){(int16_t)x, (int16_t)y};
}
