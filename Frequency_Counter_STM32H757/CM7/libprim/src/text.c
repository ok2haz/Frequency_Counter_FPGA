/**
 * @file text.c
 * @brief UTF-8 text rendering: glyph lookup + A8 alpha blit over RGB565.
 */

#include <prim/text.h>
#include <stddef.h>
#include "internal/fb_impl.h"
#include "internal/font_impl.h"
#include "internal/alpha_blend.h"     /* prim_blend565 — primy blend v CPU smycce */
#include "internal/dma2d_backend.h"   /* prim_dma2d() — volitelny HW glyph blend */

/* Pod touto vyskou glyfu se HW (DMA2D) cesta nevyplati (rezie spusteni > CPU). */
#define PRIM_GLYPH_HW_MIN_H 24

/* HW glyph blend je default VYPNUTY -> text jede CPU. Cilene se zapina jen kolem
 * velkeho textu (mereny kmitocet) pres prim_set_glyph_accel(). */
static int s_glyph_accel = 0;
void prim_set_glyph_accel(int enable) { s_glyph_accel = enable ? 1 : 0; }

uint32_t prim_internal_utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp;
    /* ⚠️ Pokracovaci bajty se kontroluji proti '\0' (p[k] != 0) — useknuta
     * multibyte sekvence na konci retezce by jinak precetla az 3 bajty ZA
     * terminatorem. Nevalidni/useknuty lead byte -> preskoc 1 bajt a vrat U+FFFD
     * (glyf se stejne nenajde -> vykresli se nic). Pro platny UTF-8 chovani beze
     * zmeny; prim_text_width i draw_text sdili tenhle dekoder -> zustavaji v syncu. */
    if (p[0] < 0x80u) {
        cp = p[0]; if (p[0]) (*s)++;
    } else if ((p[0] & 0xE0u) == 0xC0u && p[1]) {
        cp = ((uint32_t)(p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        *s += 2;
    } else if ((p[0] & 0xF0u) == 0xE0u && p[1] && p[2]) {
        cp = ((uint32_t)(p[0] & 0x0Fu) << 12) | ((uint32_t)(p[1] & 0x3Fu) << 6) |
             (p[2] & 0x3Fu);
        *s += 3;
    } else if ((p[0] & 0xF8u) == 0xF0u && p[1] && p[2] && p[3]) {
        cp = ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3Fu) << 12) |
             ((uint32_t)(p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
        *s += 4;
    } else {
        cp = 0xFFFDu;                 /* neplatny/useknuty -> replacement char */
        (*s)++;                       /* posun o 1 bajt (nikdy za '\0') */
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

    /* Volitelny HW glyph blend (DMA2D): vyplati se jen u velkych glyfu plne uvnitr
     * clipu a u nepruhledne barvy (HW cesta moduluje jen coverage). Jinak CPU. */
    const prim_dma2d_backend_t *acc = prim_dma2d();
    int hw_ok = (s_glyph_accel && acc != NULL && acc->draw_glyph != NULL &&
                 (color & 0xFF000000u) == 0xFF000000u);

    /* pos.y is the baseline. */
    const char *s = utf8;
    while (*s) {
        uint32_t cp = prim_internal_utf8_next(&s);
        const prim_glyph_t *g = prim_internal_glyph(font, cp);
        if (g == NULL) continue;
        const uint8_t *bm = font->bitmap_data + g->bitmap_offset;
        int16_t gx0 = (int16_t)(pen + g->ox);
        int16_t gy0 = (int16_t)(pos.y - g->oy);

        if (hw_ok && g->h >= PRIM_GLYPH_HW_MIN_H) {
            prim_fb_t *fb = prim_internal_target();
            prim_rect_t gr = { gx0, gy0, g->w, g->h }, cr;
            /* jen kdyz je glyf CELY viditelny (neorezany) -> presny obdelnik */
            if (fb != NULL && prim_internal_clip_rect(gr, &cr) &&
                cr.x == gx0 && cr.y == gy0 && cr.w == g->w && cr.h == g->h &&
                acc->draw_glyph(&fb->pixels[(int)gy0 * fb->stride_px + gx0],
                                fb->stride_px, bm, font->bpp, g->w, g->h, color)) {
                pen = (int16_t)(pen + g->advance);
                continue;
            }
        }

        /* CPU cesta: glyf se orizne JEDNOU (rect ∩ clip ∩ fb), pak tesna smycka
         * s primym blendem do radku. Drivejsi prim_internal_blend_px delal
         * clip_test (vc. volani prim_internal_clip) na KAZDY pixel — dominantni
         * rezie CPU textu. Vystup je pixel-identicky (stejny prim_blend565). */
        {
            prim_fb_t *fb = prim_internal_target();
            prim_rect_t gr = { gx0, gy0, g->w, g->h }, cr;
            if (fb != NULL && prim_internal_clip_rect(gr, &cr)) {
                int ox = cr.x - gx0;                 /* offset oriznuti uvnitr glyfu */
                int oy = cr.y - gy0;
                for (int ry = 0; ry < cr.h; ry++) {
                    prim_pixel_t *row = &fb->pixels[(cr.y + ry) * fb->stride_px + cr.x];
                    int gy = oy + ry;
                    for (int rx = 0; rx < cr.w; rx++) {
                        uint8_t cov = glyph_cov(bm, ox + rx, gy, g->w, font->bpp);
                        if (cov) row[rx] = prim_blend565(row[rx], color, cov);
                    }
                }
            }
        }
        pen = (int16_t)(pen + g->advance);
    }
}
