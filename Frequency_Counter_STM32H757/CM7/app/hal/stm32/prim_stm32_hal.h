#pragma once
/**
 * @file prim_stm32_hal.h
 * @brief STM32H757 HAL bridge for libprim: triple-buffered, tearing-free RGB565
 *        display + DMA2D backend + off-screen canvases.
 *
 * Triple framebuffer in WT-cacheable SDRAM (MPU region 0, 4 MB). The render
 * target is always the hidden "back" buffer; prim_stm32_present() flips it to the
 * LTDC scan-out at the vertical blanking interval (no tearing) and copies the
 * shown frame forward, so the incremental/partial-redraw renderer keeps working.
 *
 * The display pipeline (LTDC/DSI/TC358762, RGB565 BURST) is otherwise untouched.
 */

#include <stdint.h>
#include <prim/fb.h>
#include <prim/types.h>

/* Triple framebuffer base addresses (SDRAM, WT region 0, 1 MB stride). */
#ifndef PRIM_FB_ADDR
#define PRIM_FB_ADDR 0xC0000000u   /* FB0; LTDC scans this at boot */
#endif
#define PRIM_FB0_ADDR 0xC0000000u
#define PRIM_FB1_ADDR 0xC0100000u
#define PRIM_FB2_ADDR 0xC0200000u
#define PRIM_CANVAS_POOL_ADDR 0xC0300000u  /* off-screen canvas pool (1 MB) */
#define PRIM_CANVAS_POOL_BYTES 0x100000u

/**
 * @brief Initialize the triple-buffered display + DMA2D backend.
 * @param fb  Caller-owned descriptor; bound to the current back buffer and kept
 *            in sync by prim_stm32_present(). Call once at boot.
 *
 * Clears all three buffers (black), binds the libprim target to the back buffer,
 * and installs the DMA2D backend. LTDC keeps scanning FB0 until the first present.
 */
void prim_stm32_init(prim_fb_t *fb);

/**
 * @brief Present the back buffer: flip it to LTDC at vblank (tearing-free),
 *        rotate buffers, and copy the now-shown frame into the new back buffer
 *        (so the next partial redraw builds on the displayed image). Re-binds the
 *        libprim target to the new back buffer. Call once after each frame's draws.
 */
void prim_stm32_present(void);

/** Descriptor of the current back buffer (the live render target). */
prim_fb_t *prim_stm32_backfb(void);

/**
 * @brief Bind an off-screen canvas in the WT canvas pool (compose without flicker).
 * @return PRIM_OK, or PRIM_ERR_INVALID_ARG if w*h*2 exceeds the pool.
 *
 * Render into it via prim_set_target(canvas); blit it onto the back buffer with
 * prim_stm32_canvas_blit(). Single shared pool → one canvas at a time.
 */
prim_status_t prim_stm32_canvas(prim_fb_t *canvas, int16_t w, int16_t h);

/** DMA2D-blit an off-screen canvas onto the current back buffer at (x, y). */
void prim_stm32_canvas_blit(const prim_fb_t *canvas, int16_t x, int16_t y);

/** Enable (default) or disable the register-level DMA2D backend at runtime. */
void prim_stm32_use_dma2d(int enable);
