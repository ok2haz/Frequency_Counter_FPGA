#pragma once
/**
 * @file types.h
 * @brief Core value types and color model for libprim.
 *
 * Coordinate system: (0,0) top-left, x grows right, y grows down. Coordinates
 * are int16_t (range -32k..+32k is plenty for any panel).
 *
 * Color model: prim_color_t is ARGB8888 (0xAARRGGBB). This is a *compositing*
 * color used only for in-memory alpha math (glow, anti-aliasing, OVER blend,
 * A8 glyph blit). It is NEVER transmitted to the display: the framebuffer and
 * all cache buffers are RGB565 (see fb.h), and the fb layer converts on write.
 * The display pipeline (LTDC/DSI/bridge) stays RGB565 — RGB888 is intentionally
 * avoided end to end.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>     /* NULL, size_t — used across the whole stack */
#include <prim/api.h>

typedef struct { int16_t x, y;            } prim_point_t;
typedef struct { int16_t x, y, w, h;      } prim_rect_t;

/** ARGB8888 compositing color: 0xAARRGGBB. In-memory only, never sent to panel. */
typedef uint32_t prim_color_t;

/** RGB565 little-endian pixel as stored in the hardware framebuffer/caches. */
typedef uint16_t prim_pixel_t;

#define PRIM_RGB(r, g, b) \
    (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define PRIM_RGBA(r, g, b, a) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define PRIM_ALPHA(c, a) \
    (((c) & 0x00FFFFFFu) | ((uint32_t)(a) << 24))

/* Channel extraction helpers (compositing color). */
#define PRIM_A(c) ((uint8_t)((c) >> 24))
#define PRIM_R(c) ((uint8_t)((c) >> 16))
#define PRIM_G(c) ((uint8_t)((c) >> 8))
#define PRIM_B(c) ((uint8_t)(c))

/**
 * @brief Pack an ARGB8888 compositing color into an opaque RGB565 pixel.
 * @note Alpha is dropped (assumed pre-composited). Inline + branch-free.
 */
static inline prim_pixel_t prim_argb_to_565(prim_color_t c)
{
    return (prim_pixel_t)(((PRIM_R(c) & 0xF8u) << 8) |
                          ((PRIM_G(c) & 0xFCu) << 3) |
                          ((PRIM_B(c)) >> 3));
}

/**
 * @brief Expand an RGB565 pixel back to an opaque ARGB8888 color.
 * @note Replicates the high bits into the low bits for accurate round-trip.
 */
static inline prim_color_t prim_565_to_argb(prim_pixel_t p)
{
    uint32_t r = (uint32_t)((p >> 11) & 0x1Fu);
    uint32_t g = (uint32_t)((p >> 5) & 0x3Fu);
    uint32_t b = (uint32_t)(p & 0x1Fu);
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return PRIM_RGB(r, g, b);
}

typedef enum {
    PRIM_BLEND_REPLACE,   /**< Source overwrites destination (ignores src alpha). */
    PRIM_BLEND_OVER,      /**< Source alpha-blended over destination. */
} prim_blend_t;

typedef enum {
    PRIM_OK = 0,
    PRIM_ERR_NULL            = -1,
    PRIM_ERR_OOM             = -2,
    PRIM_ERR_INVALID_ARG     = -3,
    PRIM_ERR_NOT_INITIALIZED = -4,
} prim_status_t;

typedef enum {
    PRIM_ALIGN_LEFT,
    PRIM_ALIGN_CENTER,
    PRIM_ALIGN_RIGHT,
} prim_align_t;
