/**
 * @file text.c
 * @brief UTF-8 text rendering: glyph lookup + A8 alpha blit over RGB565.
 */

#include <prim/text.h>
#include <stddef.h>
#include "internal/fb_impl.h"
#include "internal/font_impl.h"

uint32_t prim_internal_utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp;
    if (p[0] < 0x80u) {
        cp = p[0]; if (p[0]) (*s)++;
    } else if ((p[0] & 0xE0u) == 0xC0u) {
        cp = ((uint32_t)(p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        *s += 2;
    } else if ((p[0] & 0xF0u) == 0xE0u) {
        cp = ((uint32_t)(p[0] & 0x0Fu) << 12) | ((uint32_t)(p[1] & 0x3Fu) << 6) |
             (p[2] & 0x3Fu);
        *s += 3;
    } else {
        cp = ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3Fu) << 12) |
             ((uint32_t)(p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
        *s += 4;
    }
    return cp;
}

const prim_glyph_t *prim_internal_glyph(const prim_font_t *font, uint32_t cp)
{
    if (font == NULL || font->glyphs == NULL) return NULL;
    int16_t lo = 0, hi = (int16_t)(font->glyph_count - 1);
    while (lo <= hi) {
        int16_t mid = (int16_t)((lo + hi) / 2);
        uint32_t mc = font->glyphs[mid].codepoint;
        if (mc == cp) return &font->glyphs[mid];
        if (mc < cp) lo = (int16_t)(mid + 1); else hi = (int16_t)(mid - 1);
    }
    return NULL;
}

/* Read one coverage sample at (gx,gy) inside a glyph of given bpp. */
static uint8_t glyph_cov(const uint8_t *bm, int gx, int gy, int gw, uint8_t bpp)
{
    uint32_t idx = (uint32_t)gy * gw + gx;
    switch (bpp) {
    case 8: return bm[idx];
    case 4: { uint8_t b = bm[idx >> 1]; uint8_t v = (idx & 1) ? (b & 0x0Fu) : (b >> 4);
              return (uint8_t)(v * 17u); }
    case 2: { uint8_t b = bm[idx >> 2]; uint8_t sh = (uint8_t)(6 - 2 * (idx & 3));
              uint8_t v = (b >> sh) & 0x3u; return (uint8_t)(v * 85u); }
    case 1: { uint8_t b = bm[idx >> 3]; uint8_t v = (b >> (7 - (idx & 7))) & 1u;
              return v ? 255u : 0u; }
    default: return 0u;
    }
}

int16_t prim_text_width(const char *utf8, const prim_font_t *font)
{
    if (utf8 == NULL || font == NULL) return 0;
    int32_t w = 0;
    const char *s = utf8;
    while (*s) {
        uint32_t cp = prim_internal_utf8_next(&s);
        const prim_glyph_t *g = prim_internal_glyph(font, cp);
        if (g) w += g->advance;
    }
    return (int16_t)w;
}

int16_t prim_text_height(const prim_font_t *font)
{
    return font ? font->line_height : 0;
}

void prim_draw_text(prim_point_t pos, const char *utf8, const prim_font_t *font,
                    prim_color_t color, prim_align_t align)
{
    if (utf8 == NULL || font == NULL) return;

    int16_t pen = pos.x;
    if (align != PRIM_ALIGN_LEFT) {
        int16_t tw = prim_text_width(utf8, font);
        if (align == PRIM_ALIGN_CENTER) pen = (int16_t)(pos.x - tw / 2);
        else                            pen = (int16_t)(pos.x - tw);
    }

    /* pos.y is the baseline. */
    const char *s = utf8;
    while (*s) {
        uint32_t cp = prim_internal_utf8_next(&s);
        const prim_glyph_t *g = prim_internal_glyph(font, cp);
        if (g == NULL) continue;
        const uint8_t *bm = font->bitmap_data + g->bitmap_offset;
        int16_t gx0 = (int16_t)(pen + g->ox);
        int16_t gy0 = (int16_t)(pos.y - g->oy);
        for (int gy = 0; gy < g->h; gy++) {
            for (int gx = 0; gx < g->w; gx++) {
                uint8_t cov = glyph_cov(bm, gx, gy, g->w, font->bpp);
                if (cov) prim_internal_blend_px((int16_t)(gx0 + gx),
                                                (int16_t)(gy0 + gy), color, cov);
            }
        }
        pen = (int16_t)(pen + g->advance);
    }
}
