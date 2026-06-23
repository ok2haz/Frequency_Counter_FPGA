/**
 * @file prim_stm32_hal.c
 * @brief Triple-buffered, tearing-free RGB565 display + DMA2D backend for libprim.
 *
 * 3 framebuffery v SDRAM (MPU region 0, WT). Render cili VZDY skryty "back";
 * prim_stm32_present() flipne LTDC na back pri vblanku (tearing-free, non-blocking)
 * a copy-forwarduje do noveho back JEN zmenene oblasti (dirty-rect) -> levne.
 *
 * Dirty-rect: kazdy fill/blit do back bufferu zaznamena svuj obdelnik (mark_dirty).
 * Protoze KAZDA zmena zacina fill/blit (clear boxu, ktery pokryva i nasledny CPU
 * text), je dirty set = sjednoceni fill/blit obdelniku == vsechny zmeny. ⚠️ Kazdy
 * PARTIAL redraw proto MUSI zacit fill/blit (clear), jinak se nezkopiruje dopredu.
 *
 * DMA2D obchazi D-cache -> po fill/blit se invaliduje cilova oblast (CPU AA blend
 * pak cte cerstva data). Backend injektovan pres prim/accel.h (libprim zustava HW-indep).
 */

#include "prim_stm32_hal.h"
#include "main.h"                       /* HAL + CMSIS (DMA2D, LTDC, RCC) */
#include <prim/accel.h>                 /* public DMA2D backend injection */
#include <string.h>                     /* memcpy */

#define DMA2D_PFC_RGB565 0x2u

/* ── Framebuffery ──────────────────────────────────────────────────────────── */
#define FB_W      800
#define FB_H      480
#define NUM_FB    3
#define FB1_ADDR  0xC0100000u
#define FB2_ADDR  0xC0200000u

static const uint32_t s_fb_addr[NUM_FB] = { PRIM_FB_ADDR, FB1_ADDR, FB2_ADDR };
static int        s_front  = 0;          /* buffer scanovany LTDC */
static int        s_back   = 1;          /* buffer do ktereho kreslime */
static prim_fb_t *s_appfb  = 0;          /* app deskriptor (drzime jeho pixels na back) */

static prim_pixel_t *fb_px(int i) { return (prim_pixel_t *)s_fb_addr[i]; }

/* ── Dirty-rect (copy-forward jen zmenenych oblasti) ───────────────────────────
 * Triple buffer: novy back je 2 snimky stary -> kopiruje se sjednoceni dirty
 * z poslednich 2 snimku (prev + cur). Plne prekresleni = velky obdelnik (cely FB)
 * vznikne prirozene (full-screen blit), pri preteceni MAX_DIRTY -> priznak full. */
#define MAX_DIRTY 48
static prim_rect_t d_cur[MAX_DIRTY];  static int nd_cur;  static int dfull_cur;
static prim_rect_t d_prev[MAX_DIRTY]; static int nd_prev; static int dfull_prev;
static int s_in_present = 0;             /* potlaci marking behem copy-forwardu */

static void mark_dirty(const prim_pixel_t *dst, int16_t w, int16_t h)
{
    if (s_in_present || dfull_cur) return;
    const prim_pixel_t *base = fb_px(s_back);
    if (dst < base || dst >= base + (int)FB_W * FB_H) return;   /* neni back buffer */
    if (nd_cur >= MAX_DIRTY) { dfull_cur = 1; return; }          /* prilis -> kopiruj cele */
    int off = (int)(dst - base);
    d_cur[nd_cur].x = (int16_t)(off % FB_W);
    d_cur[nd_cur].y = (int16_t)(off / FB_W);
    d_cur[nd_cur].w = w;
    d_cur[nd_cur].h = h;
    nd_cur++;
}

/* ── DMA2D primitiva ───────────────────────────────────────────────────────── */
static void d2d_wait(void)
{
    /* Guard: kdyby DMA2D nedoběhl, NEzaseknout UiTask navždy. */
    uint32_t guard = 0;
    while ((DMA2D->CR & DMA2D_CR_START) && ++guard < 2000000u) { /* busy */ }
    DMA2D->IFCR = DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTEIF | DMA2D_IFCR_CCTCIF;
}

/* Po DMA2D zapisu zneplatni cilovou D-cache (DMA2D obchazi cache -> CPU by jinak
 * cetlo stara data). WT region -> zadne dirty radky k zahozeni. */
static void d2d_inval(const void *dst, int16_t stride_px, int16_t w, int16_t h)
{
    if (h <= 0 || w <= 0) return;
    uint32_t span = ((uint32_t)(h - 1) * (uint32_t)stride_px + (uint32_t)w) * 2u;
    SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)dst, (int32_t)span);
}

static void d2d_fill(prim_pixel_t *dst, int16_t stride_px, int16_t w, int16_t h,
                     prim_pixel_t color)
{
    mark_dirty(dst, w, h);
    d2d_wait();
    DMA2D->CR     = (0x3u << DMA2D_CR_MODE_Pos);          /* R2M */
    DMA2D->OPFCCR = DMA2D_PFC_RGB565;
    DMA2D->OCOLR  = color;
    DMA2D->OMAR   = (uint32_t)dst;
    DMA2D->OOR    = (uint32_t)(stride_px - w);
    DMA2D->NLR    = ((uint32_t)w << DMA2D_NLR_PL_Pos) | (uint32_t)h;
    DMA2D->CR    |= DMA2D_CR_START;
    d2d_wait();
    d2d_inval(dst, stride_px, w, h);
}

static void d2d_blit(prim_pixel_t *dst, int16_t dst_stride, const prim_pixel_t *src,
                     int16_t src_stride, int16_t w, int16_t h)
{
    mark_dirty(dst, w, h);
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
    d2d_wait();
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

/* ── Page-flip + dirty copy-forward ────────────────────────────────────────── */

/* Zkopiruje jeden obdelnik z front -> back (s_in_present potlaci re-marking). */
static void copy_rect(const prim_rect_t *r)
{
    int off = (int)r->y * FB_W + r->x;
    d2d_blit(fb_px(s_back) + off, FB_W, fb_px(s_front) + off, FB_W, r->w, r->h);
}

void prim_stm32_present(void)
{
    /* NON-BLOCKING flip: cekej na PREDCHOZI flip (pri nizke kadenci OKAMZITE), ne
     * na aktualni. Diky 3. bufferu copy-forward nikdy nepise do scanovaneho bufferu. */
    uint32_t guard = 0;
    while ((LTDC->SRCR & LTDC_SRCR_VBR) && ++guard < 4000000u) { /* posl. flip dobiha */ }

    d2d_wait();                                   /* dokresli back */

    /* Flip na back pri pristim vblanku (tearing-free), bez cekani. */
    LTDC_Layer1->CFBAR = s_fb_addr[s_back];
    LTDC->SRCR = LTDC_SRCR_VBR;

    s_front = s_back;
    s_back  = (s_front + 1) % NUM_FB;

    /* Copy-forward JEN dirty oblasti (sjednoceni prev+cur) z front -> novy back.
     * Pri full priznaku nebo prilis mnoha obdelnicich kopiruj cely snimek. */
    s_in_present = 1;
    if (dfull_prev || dfull_cur) {
        d2d_blit(fb_px(s_back), FB_W, fb_px(s_front), FB_W, FB_W, FB_H);
    } else {
        for (int i = 0; i < nd_prev; i++) copy_rect(&d_prev[i]);
        for (int i = 0; i < nd_cur;  i++) copy_rect(&d_cur[i]);
    }
    s_in_present = 0;

    /* Posun historie: prev <- cur, cur <- prazdne. */
    memcpy(d_prev, d_cur, (size_t)nd_cur * sizeof(prim_rect_t));
    nd_prev = nd_cur; dfull_prev = dfull_cur;
    nd_cur = 0; dfull_cur = 0;

    s_appfb->pixels = fb_px(s_back);
    prim_set_target(s_appfb);
}

void prim_stm32_init(prim_fb_t *fb)
{
    __HAL_RCC_DMA2D_CLK_ENABLE();

    /* Vycisti vsechny 3 buffery na cerno (boot: cisty start misto smeti v SDRAM). */
    for (int i = 0; i < NUM_FB; i++)
        d2d_fill(fb_px(i), FB_W, FB_W, FB_H, 0x0000);

    /* LTDC scanuje FB0 (front). Kreslime do FB1 (back); prvni present flipne. */
    s_front = 0;
    s_back  = 1;
    s_appfb = fb;
    nd_cur = 0; dfull_cur = 0; nd_prev = 0; dfull_prev = 0;   /* reset po init clears */
    prim_fb_init(fb, fb_px(s_back), FB_W, FB_H, FB_W * (int16_t)sizeof(prim_pixel_t));
    prim_set_target(fb);
    prim_stm32_use_dma2d(1);
}
