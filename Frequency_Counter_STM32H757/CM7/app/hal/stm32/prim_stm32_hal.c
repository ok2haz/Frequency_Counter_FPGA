/**
 * @file prim_stm32_hal.c
 * @brief Register-level DMA2D backend + framebuffer binding for libprim.
 *
 * DMA2D modes: R2M solid fill and M2M copy, both RGB565. Register-level,
 * coexists with LTDC.
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
    /* Guard: kdyby DMA2D z jakehokoli duvodu nedoběhl (špatná konfigurace/chyba),
     * NEzaseknout tu UiTask navždy (jinak zamrzne i obsluha doteku). */
    uint32_t guard = 0;
    while ((DMA2D->CR & DMA2D_CR_START) && ++guard < 2000000u) { /* busy */ }
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

/* ── Triple-buffered, tearing-free display ───────────────────────────────── */

#define FB_W       800
#define FB_H       480
#define FB_STRIDE  FB_W
#define FB_BYTES   ((uint32_t)FB_W * FB_H * 2u)   /* 768000 B, 32B-aligned */
#define NUM_FB     3

static const uint32_t s_fb_addr[NUM_FB] = {
    PRIM_FB0_ADDR, PRIM_FB1_ADDR, PRIM_FB2_ADDR
};
static int        s_front = 0;   /* buffer currently scanned by LTDC */
static int        s_back  = 1;   /* buffer we render into */
static prim_fb_t *s_appfb = 0;   /* caller's descriptor, kept pointing at back */

static prim_pixel_t *fb_px(int i) { return (prim_pixel_t *)s_fb_addr[i]; }

prim_fb_t *prim_stm32_backfb(void) { return s_appfb; }

void prim_stm32_present(void)
{
    /* ⚠️ TRIPLE BUFFERING DOČASNĚ VYPNUTO. Page-flip (LTDC VBR) + copy-forward +
     * cache invalidate způsoboval systémový freeze HNED po naběhnutí obrazovky
     * (zamrzl i FpgaTask/SPI -> HardFault/halt). NETESTOVÁNO na HW, k dořešení.
     * Mezitím: single-buffer IN-PLACE — LTDC scanuje FB0, kreslíme přímo do něj
     * (WT region -> LTDC vidí data bez flipu). present() je no-op. */
    (void)s_front; (void)s_back; (void)s_appfb;
}

prim_status_t prim_stm32_canvas(prim_fb_t *canvas, int16_t w, int16_t h)
{
    if (w <= 0 || h <= 0) return PRIM_ERR_INVALID_ARG;
    if ((uint32_t)w * (uint32_t)h * 2u > PRIM_CANVAS_POOL_BYTES)
        return PRIM_ERR_INVALID_ARG;
    return prim_fb_init(canvas, (prim_pixel_t *)PRIM_CANVAS_POOL_ADDR,
                        w, h, (int16_t)(w * (int16_t)sizeof(prim_pixel_t)));
}

void prim_stm32_canvas_blit(const prim_fb_t *canvas, int16_t x, int16_t y)
{
    if (!canvas || !canvas->pixels) return;
    d2d_wait();
    prim_pixel_t *dst = fb_px(s_back) + (int)y * FB_STRIDE + x;
    d2d_blit(dst, FB_STRIDE, canvas->pixels, canvas->stride_px,
             canvas->width, canvas->height);
    d2d_wait();
    /* Canvas blit obesel D-cache na dotcenem obdelniku -> zneplatnit po radcich. */
    for (int16_t row = 0; row < canvas->height; row++)
        SCB_InvalidateDCache_by_Addr(
            (uint32_t *)(dst + (int)row * FB_STRIDE),
            (int32_t)((uint32_t)canvas->width * 2u));
}

void prim_stm32_init(prim_fb_t *fb)
{
    __HAL_RCC_DMA2D_CLK_ENABLE();

    /* Single-buffer in-place na FB0 (= PRIM_FB_ADDR), které scanuje LTDC. Triple
     * buffering vypnuto (viz prim_stm32_present). Minimální init = jako původní. */
    s_front = 0;
    s_back  = 0;
    s_appfb = fb;
    prim_fb_init(fb, fb_px(0), FB_W, FB_H, FB_STRIDE * (int16_t)sizeof(prim_pixel_t));
    prim_set_target(fb);
    prim_stm32_use_dma2d(1);
}
