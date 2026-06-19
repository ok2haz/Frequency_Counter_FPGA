#pragma once
/**
 * @file font_data.h
 * @brief Public font descriptor layout.
 *
 * Font glyph data is inherently client-supplied data (like prim_rect_t), so the
 * layout is public: the client (libui) defines const prim_font_t instances in
 * its own translation units, generated from TTF (see libui/tools/font_gen).
 *
 * Coverage bitmaps are packed MSB-first, row-major, continuous (no per-row byte
 * padding), at `bpp` bits per pixel. libprim reads them in prim_draw_text().
 */

#include <stdint.h>

typedef struct {
    uint32_t codepoint;      /**< Unicode code point. */
    uint32_t bitmap_offset;  /**< Byte offset into prim_font.bitmap_data. */
    uint8_t  w;              /**< Glyph box width in px. */
    uint8_t  h;              /**< Glyph box height in px. */
    int8_t   ox;             /**< X offset from pen to bitmap left. */
    int8_t   oy;             /**< Y offset from baseline to bitmap top (up +). */
    uint8_t  advance;        /**< Horizontal advance in px. */
} prim_glyph_t;

struct prim_font {
    const uint8_t      *bitmap_data;  /**< Packed coverage, all glyphs. */
    const prim_glyph_t *glyphs;       /**< Sorted by codepoint (binary search). */
    int16_t             glyph_count;
    int16_t             line_height;
    int16_t             baseline;     /**< Distance from line top to baseline. */
    int16_t             ascent;
    int16_t             descent;
    uint8_t             bpp;          /**< Coverage bits per pixel (1/2/4/8). */
};

typedef struct prim_font prim_font_t;
