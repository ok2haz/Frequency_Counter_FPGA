#pragma once
/**
 * @file prim_stm32_hal.h
 * @brief STM32H757 HAL bridge for libprim (RGB565 framebuffer + DMA2D backend).
 *
 * Binds libprim's render target to the existing hardware framebuffer in SDRAM
 * (RGB565 @ 0xC0000000) and installs a register-level DMA2D acceleration
 * backend. The display pipeline (LTDC/DSI/TC358762, RGB565 BURST) is untouched.
 */

#include <stdint.h>
#include <prim/fb.h>

#ifndef PRIM_FB_ADDR
#define PRIM_FB_ADDR 0xC0000000u   /* FB1, matches project framebuffer */
#endif

/**
 * @brief Initialize libprim over the hardware framebuffer and install DMA2D.
 * @param fb  Caller-owned descriptor (kept alive while rendering).
 *
 * Sets the libprim target to the given descriptor. Call once at boot before any
 * screen render. Safe to call again to re-bind.
 */
void prim_stm32_init(prim_fb_t *fb);

/** Enable (default) or disable the register-level DMA2D backend at runtime. */
void prim_stm32_use_dma2d(int enable);
