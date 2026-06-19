#pragma once
/**
 * @file text.h
 * @brief UTF-8 text rendering from 8-bit-alpha vector fonts.
 *
 * Fonts are generated with lv_font_conv and declared extern by the client
 * (libui exposes ui_font_*). libprim reads them through prim_font_t and blits
 * each glyph's A8 coverage as an alpha mask of the given color over RGB565.
 */

#include <prim/api.h>
#include <prim/types.h>
#include <prim/font_data.h>   /* prim_font_t (public POD font descriptor) */

PRIM_API void prim_draw_text(prim_point_t pos, const char *utf8,
                             const prim_font_t *font, prim_color_t color,
                             prim_align_t align);

PRIM_API int16_t prim_text_width(const char *utf8, const prim_font_t *font);
PRIM_API int16_t prim_text_height(const prim_font_t *font);
