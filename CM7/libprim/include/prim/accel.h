#pragma once
/**
 * @file accel.h
 * @brief Public DMA2D acceleration backend injection.
 *
 * libprim stays hardware-independent: the application installs a concrete
 * backend (e.g. STM32 Chrom-ART) via prim_set_dma2d_backend(). With no backend
 * installed, every primitive uses the software path (host build, or until the
 * backend is enabled). All buffers are RGB565; colors arrive packed.
 */

#include <prim/api.h>
#include <prim/types.h>

typedef struct {
    /** Register-to-Memory solid fill (RGB565). dst already points at (x,y). */
    void (*fill_rect)(prim_pixel_t *dst, int16_t stride_px,
                      int16_t w, int16_t h, prim_pixel_t color);
    /** Memory-to-Memory RGB565 copy (blit). */
    void (*blit)(prim_pixel_t *dst, int16_t dst_stride_px,
                 const prim_pixel_t *src, int16_t src_stride_px,
                 int16_t w, int16_t h);
    /** Block until the engine is idle. */
    void (*wait)(void);
} prim_dma2d_backend_t;

/** Install (or NULL to disable) the DMA2D backend. */
PRIM_API void prim_set_dma2d_backend(const prim_dma2d_backend_t *backend);
