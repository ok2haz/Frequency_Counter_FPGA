#pragma once
/**
 * @file alpha_blend.h
 * @brief Branch-light RGB565 compositing helpers (software path).
 *
 * Everything works on RGB565 destination pixels; the ARGB source is decomposed
 * on the fly. No RGB888 buffers are ever materialized — only transient channel
 * math in registers.
 */

#include <prim/types.h>

/**
 * @brief Composite ARGB source over an RGB565 destination by an 8-bit weight.
 * @param dst565  Current destination pixel.
 * @param src     ARGB8888 source color.
 * @param weight  Final coverage 0..255 (multiplied by the source alpha).
 * @return New RGB565 pixel.
 */
static inline prim_pixel_t prim_blend565(prim_pixel_t dst565, prim_color_t src,
                                         uint8_t weight)
{
    uint32_t a = ((uint32_t)PRIM_A(src) * (uint32_t)weight + 127u) / 255u;
    if (a == 0u) {
        return dst565;
    }
    if (a >= 255u) {
        return prim_argb_to_565(src);
    }

    /* Decompose destination RGB565 to 8-bit channels. */
    uint32_t dr = (uint32_t)((dst565 >> 11) & 0x1Fu);
    uint32_t dg = (uint32_t)((dst565 >> 5) & 0x3Fu);
    uint32_t db = (uint32_t)(dst565 & 0x1Fu);
    dr = (dr << 3) | (dr >> 2);
    dg = (dg << 2) | (dg >> 4);
    db = (db << 3) | (db >> 2);

    uint32_t sr = PRIM_R(src);
    uint32_t sg = PRIM_G(src);
    uint32_t sb = PRIM_B(src);

    uint32_t ia = 255u - a;
    uint32_t r = (sr * a + dr * ia + 127u) / 255u;
    uint32_t g = (sg * a + dg * ia + 127u) / 255u;
    uint32_t b = (sb * a + db * ia + 127u) / 255u;

    return (prim_pixel_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}
