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

/**
 * @brief Povolit HW (DMA2D) glyph blend pro NASLEDUJICI prim_draw_text volani.
 *        Default VYPNUTO -> text se kresli CPU (overena cesta). Zapina se cilene
 *        jen kolem velkeho textu, kde se HW vyplati (mereny kmitocet). Bezpecne
 *        jen z jednoho rendrovaciho kontextu (UiTask). Vyzaduje nainstalovany
 *        backend s draw_glyph; jinak je no-op (vzdy CPU).
 */
PRIM_API void prim_set_glyph_accel(int enable);

/**
 * @brief Kolikrat se od bootu preskocil glyf, ktery font nema (audit F-0034).
 *
 * `prim_draw_text` chybejici glyf TISE preskoci — text zmizi a nic to nehlasi.
 * Vetsina velkych fontu je subsetovana (`mono_75`/`mono_52` jen cislice,
 * `sans_32` jen `Hzsmunp`), takze je to zivá past: audit 2026-08-29 nasel
 * 15 takto neviditelnych retezcu. Nenulova hodnota = nekde na displeji CHYBI
 * text. Vypisuje `status`.
 * ⚠️ Pocita se jen ve `prim_draw_text`, ne ve `prim_text_width`.
 */
PRIM_API uint32_t prim_text_missing_glyphs(void);
