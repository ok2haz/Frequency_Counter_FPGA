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

/* ⚠️ KOHERENCE: DMA2D zapisuje primo do SDRAM a OBCHAZI CPU D-cache. Po fill/blit
 * je proto cache cilove oblasti ZASTARALA -> nasledne CPU kresleni textu (AA blend
 * cte cilove pixely) by cetlo stara data -> "px sum"/spatne stinovani (videt hlavne
 * u casu, mení se 1×/s). Zneplatnime cache dotcene oblasti. WT region -> zadne
 * dirty radky k zahozeni (bezpecne). Bounding-box (vc. mezer mezi radky) staci. */
static void d2d_inval(const void *dst, int16_t stride_px, int16_t w, int16_t h)
{
    if (h <= 0 || w <= 0) return;
    uint32_t span = ((uint32_t)(h - 1) * (uint32_t)stride_px + (uint32_t)w) * 2u;
    SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)dst, (int32_t)span);
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
    d2d_wait();                                  /* dokonci pred invalidaci */
    d2d_inval(dst, stride_px, w, h);
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
    d2d_wait();                                  /* dokonci pred invalidaci */
    d2d_inval(dst, dst_stride, w, h);
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

/* ── Single-buffer in-place rendering ────────────────────────────────────────
 * LTDC scanuje FB0 (PRIM_FB_ADDR); kreslime primo do nej. MPU region 0 = Write-
 * Through -> LTDC vidi data bez flipu. (Triple buffering / page-flip bylo zkouseno,
 * ale pro mostly-staticke UI nema smysl a zpusobovalo problemy -> odstraneno;
 * k dohledani v git historii.) */

#define FB_W  800
#define FB_H  480

/* Ponechano kvuli volajicim v app vrstve (render/clear/touch/tick). Single-
 * buffer -> neni co prehazovat (kresli se primo do FB0, WT region). */
void prim_stm32_present(void) { }

void prim_stm32_init(prim_fb_t *fb)
{
    __HAL_RCC_DMA2D_CLK_ENABLE();
    prim_fb_init(fb, (prim_pixel_t *)PRIM_FB_ADDR, FB_W, FB_H,
                 FB_W * (int16_t)sizeof(prim_pixel_t));
    prim_set_target(fb);
    prim_stm32_use_dma2d(1);
}
