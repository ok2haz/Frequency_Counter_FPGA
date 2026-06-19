#pragma once
/**
 * @file rasterizer.h
 * @brief Scanline polygon fill with even-odd rule (software).
 *
 * Used by prim_path_fill. Vertices are flattened path points; the rasterizer
 * fills spans into the current target with a solid ARGB color (alpha honored).
 * A fixed internal edge buffer is used — no allocation in the fill call.
 */

#include <prim/types.h>

#define PRIM_RASTER_MAX_PTS 256

PRIM_INTERNAL void prim_internal_fill_polygon(const prim_point_t *pts,
                                              int16_t count, prim_color_t color);
