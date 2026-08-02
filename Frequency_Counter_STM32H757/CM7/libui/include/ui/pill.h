#pragma once
/**
 * @file pill.h
 * @brief Status pill: rounded badge with optional LED/icon, label and value.
 */

#include <prim/types.h>
#include <ui/api.h>

typedef enum {
    UI_PILL_NORMAL,
    UI_PILL_OK,
    UI_PILL_WARN,
    UI_PILL_BAD,
} ui_pill_variant_t;

typedef void (*ui_icon_render_fn)(prim_point_t pos, int16_t size,
                                  prim_color_t color);

typedef struct {
    int16_t x, y;                  /**< input: top-left position */
    int16_t computed_width;        /**< output: width after render/measure */
    ui_pill_variant_t variant;
    const char *label;             /**< NULL = no label */
    const char *value;             /**< NULL = no value */
    ui_icon_render_fn icon_render; /**< NULL = no icon */
    int16_t icon_size;
    prim_color_t icon_color;
    bool has_led;                  /**< green LED with glow on the left */
    /* Prepis stylu: pri override_style=true se misto barev varianty pouziji
     * ovr_* (bg/border/value). Slouzi pro plynule prolinani barvy zvenci
     * (napr. SYS pilulka cross-fade v screen_main). */
    bool         override_style;
    prim_color_t ovr_bg, ovr_border, ovr_value;
} ui_pill_t;

UI_API void    ui_pill_render(ui_pill_t *pill);
UI_API int16_t ui_pill_measure(const ui_pill_t *pill);

/** Barvy varianty (bg/border/value) — pro externi prolinani (cross-fade).
 *  Kterykoli vystupni ukazatel smi byt NULL. */
UI_API void ui_pill_variant_colors(ui_pill_variant_t v, prim_color_t *bg,
                                    prim_color_t *border, prim_color_t *value);
