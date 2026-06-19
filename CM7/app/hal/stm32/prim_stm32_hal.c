/**
 * @file prim_stm32_hal.c
 * @brief Register-level DMA2D backend + framebuffer binding for libprim.
 *
 * DMA2D modes: R2M solid fill and M2M copy, both RGB565. Mirrors the approach
 * already used by the project's gfx.c (register-level, coexists with LTDC).
 * Installs the backend through libprim's public prim/accel.h injection API, so
 * libprim itself stays hardware-independent. Buffers live in WT-cacheable
 * SDRAM, so DMA2D reads are coherent.
 */

#include "prim_stm32_hal.h"
#include "main.h"                       /* HAL + CMSIS (DMA2D, RCC) */
#include <prim/accel.h>                 /* public DMA2D backend injection */

#define DMA2D_PFC_RGB565 0x2u

static void d2d_wait(void)
{
    while (DMA2D->CR & DMA2D_CR_START) { /* busy */ }
    DMA2D->IFCR = DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTEIF | DMA2D_IFCR_CCTCIF;
}

static void d2d_fill(prim_pixel_t *dst, int16_t stride_px, int16_t w, int16_t h,
                     prim_pixel_t color)
{
    d2d_wait();
    DMA2D->CR     = (0x3u << DMA2D_CR_MODE_Pos);          /* R2M */
    DMA2D->OPFCCR = DMA2D_PFC_RGB565;
    DMA2D->OCOLR  = color;
    DMA2D->OMAR   = (uint32_t)dst;
    DMA2D->OOR    = (uint32_t)(stride_px - w);
    DMA2D->NLR    = ((uint32_t)w << DMA2D_NLR_PL_Pos) | (uint32_t)h;
    DMA2D->CR    |= DMA2D_CR_START;
}

static void d2d_blit(prim_pixel_t *dst, int16_t dst_stride, const prim_pixel_t *src,
                     int16_t src_stride, int16_t w, int16_t h)
{
    d2d_wait();
    DMA2D->CR      = (0x0u << DMA2D_CR_MODE_Pos);         /* M2M */
    DMA2D->FGPFCCR = DMA2D_PFC_RGB565;
    DMA2D->FGMAR   = (uint32_t)src;
    DMA2D->FGOR    = (uint32_t)(src_stride - w);
    DMA2D->OPFCCR  = DMA2D_PFC_RGB565;
    DMA2D->OMAR    = (uint32_t)dst;
    DMA2D->OOR     = (uint32_t)(dst_stride - w);
    DMA2D->NLR     = ((uint32_t)w << DMA2D_NLR_PL_Pos) | (uint32_t)h;
    DMA2D->CR     |= DMA2D_CR_START;
}

static const prim_dma2d_backend_t g_stm32_backend = {
    .fill_rect = d2d_fill,
    .blit      = d2d_blit,
    .wait      = d2d_wait,
};

void prim_stm32_use_dma2d(int enable)
{
    prim_set_dma2d_backend(enable ? &g_stm32_backend : 0);
}

void prim_stm32_init(prim_fb_t *fb)
{
    __HAL_RCC_DMA2D_CLK_ENABLE();
    prim_fb_init(fb, (prim_pixel_t *)PRIM_FB_ADDR,
                 800, 480, 800 * (int16_t)sizeof(prim_pixel_t));
    prim_set_target(fb);
    prim_stm32_use_dma2d(1);
}
