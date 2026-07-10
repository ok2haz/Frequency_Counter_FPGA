#pragma once
/**
 * @file icons.h
 * @brief Vector icons — drawn purely from libprim primitives. No bitmaps.
 *
 * Each icon is authored in a 24x24 viewbox and scaled to `size`.
 */

#include <prim/types.h>
#include <ui/api.h>

UI_API void ui_icon_sat_dish   (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_led        (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_menu       (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_play       (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_temperature(prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_gear       (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_chart_line (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_chart_histo(prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_warning    (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_speaker    (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_speaker_muted(prim_point_t pos, int16_t size, prim_color_t color);
