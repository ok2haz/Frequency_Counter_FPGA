#pragma once
/**
 * @file path.h
 * @brief Opaque composite path API (move/line/close + fill).
 *
 * A path allocates its op buffer once at prim_path_create() (outside the hot
 * path) and is freed at prim_path_destroy(). No allocation happens during
 * stroke/fill — those use a pre-allocated internal scanline buffer.
 */

#include <prim/api.h>
#include <prim/types.h>

/** Opaque path object. Full definition in src/internal/path_impl.h. */
typedef struct prim_path prim_path_t;

PRIM_API prim_path_t *prim_path_create(int16_t max_ops);
PRIM_API void         prim_path_destroy(prim_path_t *p);

PRIM_API void prim_path_move_to(prim_path_t *p, prim_point_t pt);
PRIM_API void prim_path_line_to(prim_path_t *p, prim_point_t pt);
PRIM_API void prim_path_close(prim_path_t *p);

PRIM_API void prim_path_fill(prim_path_t *p, prim_color_t color);
