#pragma once
/**
 * @file prim_stm32_hal.h
 * @brief STM32H757 HAL bridge for libprim: single-buffer RGB565 framebuffer +
 *        DMA2D backend.
 *
 * LTDC scans FB0 in WT-cacheable SDRAM; libprim renders in-place into it. The
 * DMA2D backend (fill/blit) invalidates the destination D-cache after each op so
 * subsequent CPU text anti-aliasing reads coherent pixels. The display pipeline
 * (LTDC/DSI/TC358762, RGB565 BURST) is otherwise untouched.
 */

#include <stdint.h>
#include <prim/fb.h>
#include <prim/types.h>

#ifndef PRIM_FB_ADDR
#define PRIM_FB_ADDR 0xC0000000u   /* FB0; LTDC scan-out (RGB565) */
#endif

/** Initialize libprim over FB0 + install the DMA2D backend. Call once at boot. */
void prim_stm32_init(prim_fb_t *fb);

/** No-op (single-buffer). Ponechano kvuli volajicim; pri jednom bufferu neni co
 *  prehazovat — kresli se primo do FB0 (WT region -> LTDC vidi data). */
void prim_stm32_present(void);

/** Enable (default) or disable the register-level DMA2D backend at runtime. */
void prim_stm32_use_dma2d(int enable);
