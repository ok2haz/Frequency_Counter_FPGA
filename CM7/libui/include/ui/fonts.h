#pragma once
/**
 * @file fonts.h
 * @brief Extern declarations of the libui font descriptors.
 *
 * Definitions live in the generated src/fonts C files, produced from TTF by
 * tools/font_gen/gen_fonts.js into the prim_font_t format. The Unicode subset
 * matches libprim text.h section 9 (ASCII, Latin-ext diacritics, math, Greek,
 * superscripts, arrows).
 */

#include <prim/text.h>
#include <ui/api.h>

UI_API extern const prim_font_t ui_font_mono_14;
UI_API extern const prim_font_t ui_font_mono_16;
UI_API extern const prim_font_t ui_font_mono_18;
UI_API extern const prim_font_t ui_font_mono_20;
UI_API extern const prim_font_t ui_font_mono_25;
UI_API extern const prim_font_t ui_font_mono_30;
UI_API extern const prim_font_t ui_font_mono_75;
UI_API extern const prim_font_t ui_font_mono_52;
UI_API extern const prim_font_t ui_font_sans_14;
UI_API extern const prim_font_t ui_font_sans_16;
UI_API extern const prim_font_t ui_font_sans_17;
UI_API extern const prim_font_t ui_font_sans_20;
UI_API extern const prim_font_t ui_font_sans_32;
