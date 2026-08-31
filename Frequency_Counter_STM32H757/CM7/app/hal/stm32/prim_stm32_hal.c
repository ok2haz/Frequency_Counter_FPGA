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

/* Adresa aktualne ZOBRAZENEHO (front) bufferu — pro screenshot export (RGB565). */
const void *prim_stm32_front_addr(void) { return (const void *)s_fb_addr[s_front]; }

int prim_stm32_fb_count(void) { return NUM_FB; }

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

/* do_inval=0: vynech zneplatneni D-cache (jen kdyz cilovou oblast NIKDO CPU necte
 * pred prepsanim — viz copy-forward nize). Jinak 1 (CPU AA blend by cetl stara data). */
static void d2d_blit_ex(prim_pixel_t *dst, int16_t dst_stride, const prim_pixel_t *src,
                        int16_t src_stride, int16_t w, int16_t h, int do_inval)
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
    if (do_inval) d2d_inval(dst, dst_stride, w, h);
}

static void d2d_blit(prim_pixel_t *dst, int16_t dst_stride, const prim_pixel_t *src,
                     int16_t src_stride, int16_t w, int16_t h)
{
    d2d_blit_ex(dst, dst_stride, src, src_stride, w, h, 1);   /* libprim blit: CPU smi cist -> inval */
}

/* ── DMA2D glyph blend (velky text bez CPU rasterizace) ────────────────────────
 * Fonty drzi glyfy jako packed coverage (1/2/4/8 bpp). Misto CPU per-pixel blendu
 * (text.c) expandujeme glyf JEDNOU do A8 dlazdice v SDRAM atlasu (Device pamet,
 * non-cached -> DMA2D cte primo, jako bg_cache) a pak ho per-snimek blendujeme
 * pres pozadi cistym DMA2D (FG=A8 alfa, barva z FGCOLR). Cache: klic = ukazatel
 * na coverage data (stabilni, unikatni per glyf). Mark_dirty ZAMERNE neni — text
 * vzdy nasleduje po clear (fill/blit), jehoz dirty rect ho pokryva (jako CPU text). */
#ifndef PRIM_HOST_BUILD
#  define GLYPH_SDRAM __attribute__((section(".sdram"), aligned(32)))
#else
#  define GLYPH_SDRAM
#endif
#define GLYPH_CACHE_N      96
#define GLYPH_ATLAS_BYTES  (256u * 1024u)
static uint8_t  s_glyph_atlas[GLYPH_ATLAS_BYTES] GLYPH_SDRAM;
static uint32_t s_glyph_used;
static struct { const uint8_t *key; uint8_t *tile; } s_gc[GLYPH_CACHE_N];
static int      s_gc_n;

/* Jeden coverage vzorek (0..255) z packed bitmapy — jako glyph_cov v text.c. */
static uint8_t glyph_cov_a8(const uint8_t *bm, uint32_t idx, uint8_t bpp)
{
    switch (bpp) {
    case 8: return bm[idx];
    case 4: { uint8_t b = bm[idx >> 1]; uint8_t v = (idx & 1) ? (b & 0x0Fu) : (b >> 4);
              return (uint8_t)(v * 17u); }
    case 2: { uint8_t b = bm[idx >> 2]; uint8_t sh = (uint8_t)(6 - 2 * (idx & 3));
              return (uint8_t)(((b >> sh) & 3u) * 85u); }
    case 1: { uint8_t b = bm[idx >> 3]; return ((b >> (7 - (idx & 7))) & 1u) ? 255u : 0u; }
    default: return 0u;
    }
}

/* Vrati A8 dlazdici glyfu z cache; pri promahu ji expanduje do atlasu (jednou).
 * NULL = atlas/cache plny -> volajici spadne na CPU. */
static uint8_t *glyph_tile(const uint8_t *cov, uint8_t bpp, int16_t w, int16_t h)
{
    for (int i = 0; i < s_gc_n; i++)
        if (s_gc[i].key == cov) return s_gc[i].tile;        /* hit */
    if (s_gc_n >= GLYPH_CACHE_N) return 0;
    uint32_t bytes = (uint32_t)w * (uint32_t)h;
    if (s_glyph_used + bytes > GLYPH_ATLAS_BYTES) return 0;
    uint8_t *tile = &s_glyph_atlas[s_glyph_used];
    for (uint32_t idx = 0; idx < bytes; idx++) tile[idx] = glyph_cov_a8(cov, idx, bpp);
    s_gc[s_gc_n].key = cov; s_gc[s_gc_n].tile = tile; s_gc_n++;
    s_glyph_used += bytes;
    return tile;
}

static int d2d_draw_glyph(prim_pixel_t *dst, int16_t stride_px, const uint8_t *cov,
                          uint8_t bpp, int16_t w, int16_t h, prim_color_t color)
{
    if (w <= 0 || h <= 0) return 0;
    uint8_t *tile = glyph_tile(cov, bpp, w, h);
    if (tile == 0) return 0;                                /* nevejde se -> CPU */
    d2d_wait();
    /* M2M + PFC + blending: FG = A8 dlazdice (alfa), barva z FGCOLR; BG = OUT = dst. */
    DMA2D->CR      = (0x2u << DMA2D_CR_MODE_Pos);
    DMA2D->FGMAR   = (uint32_t)tile;
    DMA2D->FGOR    = 0;                                     /* dlazdice tesne (stride=w) */
    DMA2D->FGPFCCR = 0x9u;                                  /* CM = A8 (alfa, barva z FGCOLR) */
    DMA2D->FGCOLR  = (uint32_t)(color & 0x00FFFFFFu);       /* 0xRRGGBB */
    DMA2D->BGMAR   = (uint32_t)dst;
    DMA2D->BGOR    = (uint32_t)(stride_px - w);
    DMA2D->BGPFCCR = DMA2D_PFC_RGB565;
    DMA2D->OMAR    = (uint32_t)dst;
    DMA2D->OOR     = (uint32_t)(stride_px - w);
    DMA2D->OPFCCR  = DMA2D_PFC_RGB565;
    DMA2D->NLR     = ((uint32_t)w << DMA2D_NLR_PL_Pos) | (uint32_t)h;
    DMA2D->CR     |= DMA2D_CR_START;
    d2d_wait();
    d2d_inval(dst, stride_px, w, h);
    return 1;
}

static const prim_dma2d_backend_t g_stm32_backend = {
    .fill_rect  = d2d_fill,
    .blit       = d2d_blit,
    .wait       = d2d_wait,
    .draw_glyph = d2d_draw_glyph,
};

void prim_stm32_use_dma2d(int enable)
{
    prim_set_dma2d_backend(enable ? &g_stm32_backend : 0);
}

/* ── Page-flip + dirty copy-forward ────────────────────────────────────────── */

/* Zkopiruje jeden obdelnik z front -> back (s_in_present potlaci re-marking).
 * BEZ inval: copy-forward cilove pixely CPU nikdy necte ve stale stavu — bud je
 * cte LTDC/DMA2D primo ze SDRAM, nebo je pristi snimek prepise; a CPU AA text
 * vzdy kresli pres CERSTVY clear teho snimku (ne pres copy-forward). Usetri
 * ~860 cache-line invalidaci/rect (number-tail) × ~60×/s. */
static void copy_rect(const prim_rect_t *r)
{
    int off = (int)r->y * FB_W + r->x;
    d2d_blit_ex(fb_px(s_back) + off, FB_W, fb_px(s_front) + off, FB_W, r->w, r->h, 0);
}

/* `a` plne obsahuje `b` (vc. shody). Int aritmetika (int16 souradnice). */
static int rect_covers(const prim_rect_t *a, const prim_rect_t *b)
{
    return b->x >= a->x && b->y >= a->y &&
           (int)b->x + b->w <= (int)a->x + a->w &&
           (int)b->y + b->h <= (int)a->y + a->h;
}

/* Copy-forward dirty prev+cur s DEDUP. prev a cur se casto prekryvaji nebo shoduji
 * (napr. freq rect / stat karta se prekresluji kazdy snimek -> jsou v prev I cur)
 * -> bez dedup se STEJNA oblast kopiruje vICEkrat (DMA2D blit + wait navic/rect).
 * Vyhodime SHODNE a plne OBSAZENE obdelniky. ⚠️ Kopirovana mnozina = PRESNE
 * sjednoceni vstupu (keep drzi jen puvodni obdelniky, nikdy vetsi): rect se prida
 * jen kdyz ho zadny drzeny nekryje, a ty ktere on kryje se odeberou -> pokryti se
 * NIKDY nezmensi ani nezvetsi mimo dirty => zadne riziko problikavani/duchu.
 * ZADNY bbox-merge (ten by kopiroval mimo dirty). O(n^2), n<=2*MAX_DIRTY, 1x/present. */
static void copy_forward_dedup(void)
{
    static prim_rect_t keep[2 * MAX_DIRTY];
    int nk = 0;
    for (int pass = 0; pass < 2; pass++) {
        const prim_rect_t *src = pass ? d_cur : d_prev;
        int n = pass ? nd_cur : nd_prev;
        for (int i = 0; i < n; i++) {
            prim_rect_t r = src[i];
            int skip = 0;
            for (int j = 0; j < nk; j++)
                if (rect_covers(&keep[j], &r)) { skip = 1; break; }   /* uz pokryto */
            if (skip) continue;
            int w = 0;                                                /* odeber pokryte novym r */
            for (int j = 0; j < nk; j++)
                if (!rect_covers(&r, &keep[j])) keep[w++] = keep[j];
            nk = w;
            keep[nk++] = r;
        }
    }
    for (int i = 0; i < nk; i++) copy_rect(&keep[i]);
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
        d2d_blit_ex(fb_px(s_back), FB_W, fb_px(s_front), FB_W, FB_W, FB_H, 0);  /* copy-forward: bez inval */
    } else {
        copy_forward_dedup();          /* sjednoceni prev+cur bez redundantnich blitu */
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
