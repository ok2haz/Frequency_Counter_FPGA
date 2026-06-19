/**
 * @file dma2d_backend.c
 * @brief Storage for the injected DMA2D backend (NULL = software path).
 */

#include <stddef.h>
#include <prim/accel.h>
#include "internal/dma2d_backend.h"

static const prim_dma2d_backend_t *g_backend = NULL;

void prim_set_dma2d_backend(const prim_dma2d_backend_t *backend)
{
    g_backend = backend;
}

const prim_dma2d_backend_t *prim_dma2d(void) { return g_backend; }
