#pragma once
/**
 * @file fb_impl.h
 * @brief Private definition of prim_fb_t and access to the current render state.
 *
 * Clients only ever see the opaque prim_fb_t pointer (fb.h). The full layout
 * and the global current-target/current-clip live here, visible to libprim .c
 * files only.
 */

#include <prim/types.h>
#include <prim/fb.h>      /* struct prim_fb is now defined here (public POD) */

/* Current render context — internal, never exported as API. */
PRIM_INTERNAL prim_fb_t  *prim_internal_target(void);
PRIM_INTERNAL prim_rect_t prim_internal_clip(void);

/**
 * @brief Intersect a rect with the current clip; returns false if empty.
 */
PRIM_INTERNAL bool prim_internal_clip_rect(prim_rect_t in, prim_rect_t *out);

/**
 * @brief Bounds-checked single-pixel write into the current target (RGB565).
 *        Honors the current clip. color is ARGB; alpha selects REPLACE vs OVER.
 */
PRIM_INTERNAL void prim_internal_put_px(int16_t x, int16_t y, prim_color_t color,
                                        prim_blend_t blend);

/**
 * @brief Coverage-weighted pixel write for anti-aliasing (0..255 coverage).
 *        Blends color over the existing RGB565 destination by coverage*alpha.
 */
PRIM_INTERNAL void prim_internal_blend_px(int16_t x, int16_t y, prim_color_t color,
                                          uint8_t coverage);
