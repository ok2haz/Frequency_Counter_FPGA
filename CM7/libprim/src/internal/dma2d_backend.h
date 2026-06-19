#pragma once
/**
 * @file dma2d_backend.h
 * @brief Internal accessor for the injected DMA2D backend.
 *
 * The backend struct and the public setter live in prim/accel.h. This header
 * adds only the library-internal getter used by the primitives.
 */

#include <prim/accel.h>

/** NULL when no backend installed → callers use the software path. */
PRIM_INTERNAL const prim_dma2d_backend_t *prim_dma2d(void);
