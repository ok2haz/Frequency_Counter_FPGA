#pragma once
/**
 * @file fb.h
 * @brief Framebuffer target and clip-rectangle management.
 *
 * A prim_fb_t wraps an RGB565 pixel buffer (the hardware framebuffer in SDRAM,
 * or a software cache buffer). All draw operations write into the *current
 * target* framebuffer and are clipped to the *current clip* rectangle, which
 * enables partial redraw in the application layer.
 *
 * @note Not thread-safe. The caller must serialize all libprim access from a
 *       single render context.
 */

#include <prim/api.h>
#include <prim/types.h>

/**
 * @brief Framebuffer descriptor (public POD — the caller owns and may stack-
 *        allocate it, e.g. for off-screen cache buffers). Treat the fields as
 *        read-only after prim_fb_init().
 */
typedef struct prim_fb {
    prim_pixel_t *pixels;     /**< RGB565 storage. */
    int16_t       width;
    int16_t       height;
    int16_t       stride_px;  /**< Row stride in pixels. */
} prim_fb_t;

/**
 * @brief Initialize a framebuffer descriptor over an existing RGB565 buffer.
 * @param fb            Caller-owned descriptor to initialize.
 * @param pixels        RGB565 pixel storage (width*height pixels, 16bpp).
 * @param width         Width in pixels.
 * @param height        Height in pixels.
 * @param stride_bytes  Bytes per row (>= width*2; allows padded rows).
 * @return PRIM_OK or an error code.
 */
PRIM_API prim_status_t prim_fb_init(prim_fb_t *fb, prim_pixel_t *pixels,
                                    int16_t width, int16_t height,
                                    int16_t stride_bytes);

PRIM_API void        prim_set_target(prim_fb_t *fb);
PRIM_API prim_fb_t  *prim_get_target(void);

/** Direct addressing helpers (used by the SW rasterizer and the fb_impl). */
PRIM_API prim_pixel_t *prim_fb_pixels(prim_fb_t *fb);
PRIM_API int16_t       prim_fb_width(const prim_fb_t *fb);
PRIM_API int16_t       prim_fb_height(const prim_fb_t *fb);
PRIM_API int16_t       prim_fb_stride(const prim_fb_t *fb);

PRIM_API void        prim_set_clip(prim_rect_t rect);
PRIM_API void        prim_reset_clip(void);
PRIM_API prim_rect_t prim_get_clip(void);
