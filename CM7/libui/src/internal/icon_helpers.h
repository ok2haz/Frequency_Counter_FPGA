#pragma once
/**
 * @file icon_helpers.h
 * @brief 24x24 viewbox scaling helpers for vector icons.
 */

#include <prim/types.h>

#define UI_ICON_VIEWBOX 24

/* Scale a raw 0..24 coordinate to the target size. */
static inline int16_t ui_icon_scale(int16_t raw, int16_t size)
{
    return (int16_t)((int32_t)raw * size / UI_ICON_VIEWBOX);
}

/* Point in icon space → absolute, given pos (top-left) and size. */
#define ICON_PT(rx, ry) \
    ((prim_point_t){(int16_t)(g_icon_pos.x + ui_icon_scale((rx), g_icon_size)), \
                    (int16_t)(g_icon_pos.y + ui_icon_scale((ry), g_icon_size))})
#define ICON_SC(rv) ui_icon_scale((rv), g_icon_size)
