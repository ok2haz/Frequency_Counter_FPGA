#pragma once
/**
 * @file gradient.h
 * @brief Linear and radial gradient fills.
 */

#include <prim/api.h>
#include <prim/types.h>

typedef enum {
    PRIM_GRAD_VERTICAL,
    PRIM_GRAD_HORIZONTAL,
} prim_grad_dir_t;

/**
 * @brief Linear gradient between two colors across a rectangle.
 * @note Vertical/horizontal variants generate a 1px ramp then repeat-blit it.
 */
PRIM_API void prim_fill_gradient_linear(prim_rect_t rect,
                                        prim_color_t start, prim_color_t end,
                                        prim_grad_dir_t dir);

/**
 * @brief Radial gradient from inner to outer color.
 * @note Always software (~5 ms @ 800x480). Cache the result for static use.
 */
PRIM_API void prim_fill_gradient_radial(prim_rect_t rect, prim_point_t center,
                                        int16_t inner_r, int16_t outer_r,
                                        prim_color_t inner, prim_color_t outer);
