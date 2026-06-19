#pragma once
/**
 * @file font_impl.h
 * @brief Internal font helpers. The prim_font_t / prim_glyph_t layout is public
 *        (prim/font_data.h) because clients define font instances.
 */

#include <prim/types.h>
#include <prim/text.h>

/** Binary-search a glyph by codepoint; NULL if absent. */
PRIM_INTERNAL const prim_glyph_t *prim_internal_glyph(const prim_font_t *font,
                                                      uint32_t codepoint);

/** Decode the next UTF-8 code point; advances *s past it. */
PRIM_INTERNAL uint32_t prim_internal_utf8_next(const char **s);
