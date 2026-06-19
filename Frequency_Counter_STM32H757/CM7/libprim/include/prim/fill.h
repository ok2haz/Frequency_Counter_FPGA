#pragma once
/**
 * @file fill.h
 * @brief Rectangle fills and blits. DMA2D-accelerated where a backend exists.
 */

#include <prim/api.h>
#include <prim/types.h>

/**
 * @brief Fill an axis-aligned rectangle.
 * @param rect   Target rect, clipped to the current clip region.
 * @param color  ARGB8888 color.
 * @param blend  REPLACE writes RGB565 directly; OVER alpha-blends per pixel.
 * @note Uses DMA2D (Register-to-Memory) for REPLACE when a backend is installed.
 */
PRIM_API void prim_fill_rect(prim_rect_t rect, prim_color_t color,
                             prim_blend_t blend);

/**
 * @brief Fill a rounded rectangle (center via DMA2D, 4 corners via AA software).
 */
PRIM_API void prim_fill_rect_rounded(prim_rect_t rect, int16_t radius,
                                     prim_color_t color, prim_blend_t blend);

/**
 * @brief Stroke the outline of a rounded rectangle.
 */
PRIM_API void prim_stroke_rect_rounded(prim_rect_t rect, int16_t radius,
                                       int16_t thickness, prim_color_t color);

/**
 * @brief Blit an RGB565 source buffer into the target framebuffer (no blend).
 * @param dst         Destination rect; w/h define the copied region.
 * @param src         RGB565 source pixels.
 * @param src_stride  Source bytes per row.
 * @note DMA2D Memory-to-Memory when available; straight copy otherwise.
 */
PRIM_API void prim_blit(prim_rect_t dst, const prim_pixel_t *src,
                        int16_t src_stride);
