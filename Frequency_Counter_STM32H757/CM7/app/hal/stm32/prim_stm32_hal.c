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
 * 🔴 UPRESNENI (audit F-0032, 2026-09-10) — veta vyse plati JEN pro NEPRUHLEDNY
 * clear. `mark_dirty` se vola vyhradne z `d2d_fill`/`d2d_blit_ex`, tedy z DMA2D
 * cesty; a `prim_fill_rect` do ni vstoupi jen kdyz je `blend == PRIM_BLEND_REPLACE`
 * NEBO `PRIM_A(color) == 0xFF` (`libprim/src/fill.c:37`). S `PRIM_BLEND_OVER`
 * a alfou < 255 spadne do `sw_fill`, ktery pise primo do framebufferu a dirty
 * rect NEOZNACI. Poloprusvitny fill tedy invariant NESPLNUJE.
 * Dnes to nikde nevadi (jedina poloprusvitna barva v projektu je `freq_stop_bg`
 * a ma pred sebou nepruhledny `blit_bg_region`), ale je to latentni past.
 * ⚠️ Totez plati pro `prim_internal_blend_px` (AA rohy, arc, glow) — ty
 * `mark_dirty` obchazeji VZDY a spolehaji na predchozi clear.
 *
 * DMA2D obchazi D-cache -> po fill/blit se invaliduje cilova oblast (CPU AA blend
 * pak cte cerstva data). Backend injektovan pres prim/accel.h (libprim zustava HW-indep).
 */

#include "prim_stm32_hal.h"
#include "main.h"                       /* HAL + CMSIS (DMA2D, LTDC, RCC) */
#include <prim/accel.h>                 /* public DMA2D backend injection */
#include <string.h>                     /* memcpy */
#include "cmsis_os2.h"                  /* osDelay/osKernelGetState — pomala vetev d2d_wait() */

#define DMA2D_PFC_RGB565 0x2u

/* ── Framebuffery ──────────────────────────────────────────────────────────── */
#define FB_W      800
#define FB_H      480
#define NUM_FB    3
/* Vychozi mrtvy cas DMA2D (viz `prim_stm32_set_deadtime`). Zamerne NIZKY:
 * kazdy takt navic zpomaluje kresleni a UiTask uz tak jede kolem 70 % CPU.
 * Spravna hodnota se hleda na desce pres `d2ddt` proti `LTDC: podteceni FIFO`. */
/* 🔴 ZMERENO NA DESCE 2026-09-06 (`tools/ltdc_knee.ps1`, vynucene plne
 * prekresleni pres `ui`). Podteceni LTDC pri ruznem mrtvem case:
 *      d2ddt   196   204   212   220   240   255
 *      podt.    68    64     0     0     0     0
 * Zlom je tedy kolem 208; od 212 vys je NULA. Overeno i zpetne (255 -> 0,
 * 192 -> 64, 255 -> 0, 192 -> 70), takze to neni nahoda ani drift.
 * ⚠️ Pocet flipu zustal 59-65 nad i pod zlomem -> propustnost kresleni tim
 * NEKLESLA (DMA2D nebyl uzkym hrdlem; tim byla SDRAM).
 * Voli se 240, ne 255: ~15 % rezerva nad zlomem a zbytek do maxima zustava
 * jako DIAGNOSTICKY SIGNAL — kdyby podteceni znovu naskocilo, znamena to, ze
 * se zmenilo neco jineho, ne ze staci pridat.
 * ⚠️ Puvodnich 8 bylo jen odhadem a NIC neresilo (podteceni ~1 na snimek =
 * probliknuti pri KAZDEM prekresleni, presne jak to hlasil uzivatel). */
#define D2D_DEADTIME_DEFAULT  240u
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

/* ── Diagnostika rotace bufferu (F-0140, UART `fbdiff`) ──────────────────────
 * `g_fb_back_count[i]` = kolikrat uz byl buffer `i` CILEM copy-forwardu. Kdyby
 * nektery zustal na nule, vypadl by z rotace a drzel by natrvalo stary obsah —
 * tedy „problikne" pokazde, kdyz na nej pri flipu prijde rada, a `LTDC:
 * podteceni FIFO` by u toho ukazovalo nulu (jde o obsah pameti, ne o scan-out).
 * `g_fb_last_copy_rects` = kolik obdelniku se naposledy kopirovalo
 * (0 = NIC, 0xFFFFFFFF = plna kopie snimku). */
uint32_t g_fb_back_count[NUM_FB];
uint32_t g_fb_last_copy_rects;
uint32_t g_fb_full_copies;

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

/* Diagnostika DMA2D (audit F-0033). Do 2026-09-10 se priznak chyby prenosu
 * (`TEIF`) MAZAL, aniz by ho kdokoli precetl, a vyprseni hlidaci smycky se
 * nikam nezapsalo — poskozeny obdelnik tak nezanechal zadnou stopu a pri
 * vysetrovani vypadal jako vada pameti nebo panelu (viz tabulka „HW OBVINEN —
 * A BYL NEVINNY"). Tyz idiom jako `g_ltdc_underrun` o kus niz: nic se nemeni
 * na chovani, jen prestane byt ticho. Vypisuje `status`. */
volatile uint32_t g_d2d_errors;          /* TEIF/CEIF na dokoncenem prenosu */
volatile uint32_t g_d2d_timeouts;        /* DMA2D nedobehl do meze -> prenos ZRUSEN */
volatile uint32_t g_ltdc_flip_timeouts;  /* predchozi flip nedobehl do meze */
volatile uint32_t g_d2d_wait_max_cyc;    /* nejdelsi pozorovane cekani (takty DWT) */
volatile uint32_t g_d2d_slow_entries;    /* kolikrat cekani padlo do pomale (tick) vetve, viz d2d_wait */

/* ── Meze cekani na DMA2D (audit F-0036) ──────────────────────────────────────
 * 🔴 Do 2026-09-10 tu byla JEDNA konstanta 2 000 000 iteraci spinu, ktera merila
 * naraz dve uplne ruzne veci: „jak dlouho legitimne trva prenos" a „kdy je
 * hardware zatuhly". Ty se lisi o rady, takze je jedna hodnota nutne splete —
 * 2 M iteraci je pri 480 MHz ~12 ms, tedy radove tolik, co celoobrazovkovy
 * prenos (768 kB) s mrtvym casem DMA2D 240. Mez proto vyprsela i ZA NORMALNIHO
 * PROVOZU a volajici pak prepsal registry nad bezicim prenosem.
 * ZMERENO poté, co F-0033 pridalo pocitadlo: 14 vyprseni za 25 s bezu, rostoucich
 * s kreslenim (14 -> 23 -> 25 pres vynucene plne redrawy), pritom `chyb` 0
 * a `LTDC podteceni` 0 — tedy ne chyba prenosu, prenos proste trval dyl nez mez.
 *
 * Ted jsou ty dve veci oddelene:
 *  - `D2D_SPIN_FAST` = levny spin bez cteni casovace; pokryje bezny kratky prenos,
 *    takze v horke ceste nepribyla zadna rezie navic,
 *  - `D2D_WAIT_MS`   = SKUTECNA mez, v milisekundach (nezavisla na taktu CPU
 *    i na mrtvem case DMA2D — presne to, co drive chybelo),
 *  - `D2D_ABORT_SPIN`= backstop pro pripad, ze by stala casova zakladna
 *    (`HAL_GetTick` by nerostl); NENI to mez delky prenosu.
 * ⚠️ Rezerva neni odhad: `g_d2d_wait_max_cyc` (nejdelsi pozorovane cekani) je
 * videt ve `status`, takze se da porovnat s `D2D_WAIT_MS`. Kdyz se nekdy zmeni
 * takt, rozliseni panelu nebo `d2ddt`, pozna se to TAM — ne az tichym vyprsenim. */
/* 🔑 `D2D_WAIT_MS` NENI ODHAD — vychazi z mereni na desce (2026-09-10).
 * Nejdelsi pozorovane cekani (`g_d2d_wait_max_cyc` ve `status`) je pri dnesnim
 * `d2ddt = 240` a rozliseni 800x480 **~61 ms** (celoobrazovkovy M2M prenos
 * 768 kB; mrtvy cas mezi AXI pristupy je presne to, co ho tak prodluzuje).
 * Muj puvodni odhad znel ~12 ms a byl 5x vedle — proto tu ta hodnota stoji
 * s odkazem na meritko, ne na uvahu.
 * 500 ms = ~8x nad namerenym maximem a zaroven 5x POD 2,5 s, ktere ma na
 * heartbeat UiTask (`watchdog_supervise`), takze ani skutecne zatuhnuti
 * neshodi desku driv, nez se prenos zrusi.
 * ⚠️ Pri zmene `d2ddt`, rozliseni nebo taktu se `max cekani` posune — proto se
 * pri `d2ddt <n>` pocitadla nuluji a hodnota se da premerit. */
#define D2D_SPIN_FAST    20000u
#define D2D_WAIT_MS      500u
#define D2D_ABORT_SPIN   1000000u
#define LTDC_FLIP_GUARD  4000000u

static void d2d_wait(void)
{
    uint32_t t0 = DWT->CYCCNT;   /* DWT bezi kvuli runtime statum; kdyz ne, vyjde 0 */

    /* Levny spin — bezny prenos skonci tady a zadny casovac se necte. */
    uint32_t spin = 0;
    while ((DMA2D->CR & DMA2D_CR_START) && ++spin < D2D_SPIN_FAST) { /* busy */ }

    if (DMA2D->CR & DMA2D_CR_START) {
        /* Dlouhy prenos (typicky celoobrazovkovy, od F-0140 i bezny plnosirkovy
         * copy-forward pruh) -> mez v CASE, ne v iteracich.
         * ⚠️ ZMERENO 2026-09-24: F-0140 zvetsil typicky objem copy-forwardu
         * (linearni pruhy misto strided obdelniku), takze i BEZNE partial
         * redrawy zacaly padat sem — a tahle vetev do te doby byla cisty
         * busy-spin. Vysledek: UiTask CPU% vyskocil z ~65 na 80-93 %, beze
         * zmeny skutecne prace (jen delsi cekani na HW se pocitalo jako
         * "zaneprazdnen"). Mezi kontrolami se ted pousti scheduler —
         * `D2D_WAIT_MS` mez v REALNEM case zustava STEJNA (`osDelay(1)` jen
         * meni ZPUSOB cekani, ne kdy se prenos zrusi), takze se na korektnosti
         * F-0140 (LTDC uz nehladovi) nic nemeni. Guard `osKernelGetState()` je
         * DEFENZIVNI pojistka, ne popis dneska (F-0147): jediny volajici se do
         * d2d_wait dostane vyhradne z UiTasku, tedy az za bezicim schedulerem,
         * takze vetev "not running" v soucasnem call-graphu NIKDY neprobehne.
         * Drzi se pro hypotetickou budouci early-boot volaci cestu — jakou
         * `sd_wait_ready`/`w25q.c wait_ready` opravdu maji z `main.c` USER CODE 2
         * pred schedulerem; tam by `osDelay` nesel a spadlo by se na cisty spin. */
        uint32_t tick0 = HAL_GetTick();
        uint32_t hard  = 0;
        g_d2d_slow_entries++;
        while ((DMA2D->CR & DMA2D_CR_START) && ++hard < D2D_ABORT_SPIN) {
            if ((HAL_GetTick() - tick0) > D2D_WAIT_MS) {
                g_d2d_timeouts++;
                /* 🔴 NEPREPROGRAMOVAVAT naslepo (to delal puvodni kod): prepis
                 * konfiguracnich registru za behu prenosu neni definovana operace.
                 * Prenos se zrusi a pocka se na potvrzeni, aby volajici zacinal
                 * nad klidnou periferii. */
                DMA2D->CR |= DMA2D_CR_ABORT;
                uint32_t a = 0;
                while ((DMA2D->CR & DMA2D_CR_START) && ++a < D2D_ABORT_SPIN) { /* rusi se */ }
                break;
            }
            if (osKernelGetState() == osKernelRunning) osDelay(1);
        }
    }

    /* Nejdelsi pozorovane cekani. Drzi se v TAKTECH — prevod na us je delenim
     * a to do horke cesty nepatri; `status` si ho prepocita pri vypisu. */
    uint32_t dt = DWT->CYCCNT - t0;          /* pretece az za ~9 s pri 480 MHz */
    if (dt > g_d2d_wait_max_cyc) g_d2d_wait_max_cyc = dt;

    /* ⚠️ ISR se cte JEDNOU (d2d_wait je v horke ceste — 2x na kazdy fill/blit). */
    uint32_t isr = DMA2D->ISR;
    if (isr & (DMA2D_ISR_TEIF | DMA2D_ISR_CEIF)) g_d2d_errors++;
    /* `CCEIF` se driv nemazal vubec -> konfiguracni chyba zustala viset. */
    DMA2D->IFCR = DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTEIF | DMA2D_IFCR_CCTCIF |
                  DMA2D_IFCR_CCEIF;
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
    /* 🔴 Bariera pred startem (audit F-0035). DMA2D cte pamet, do ktere mohl
     * bezprostredne predtim zapisovat PROCESOR (CPU antialiasing textu, `sw_fill`,
     * `prim_internal_blend_px`) — a u `d2d_draw_glyph` je framebuffer dokonce
     * primo vstupem (`BGMAR`). Framebuffery jsou Normal/Write-Through, takze
     * zapis JDE do pameti (nevznika dirty radek), ale poradi Normal zapisu vuci
     * naslednemu Device zapisu do registru DMA2D neni na ARMv7-M bez `DSB`
     * garantovane. Cena je jedna instrukce proti celemu prenosu. */
    __DSB();
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
    __DSB();                        /* viz `d2d_fill` — bariera pred startem (F-0035) */
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
    /* viz `d2d_fill` (F-0035). Tady je bariera nejpodstatnejsi: `BGMAR` je primo
     * framebuffer, do ktereho CPU kreslilo pozadi tesne predtim. */
    __DSB();
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

/* Copy-forward dirty oblasti (prev+cur) z front -> novy back, jako PLNOSIRKOVE
 * PASY slite podle osy Y.
 *
 * 🔴 PROC PLNA SIRKA A NE PRESNE DIRTY OBDELNIKY (audit F-0140, zmereno 2026-09-22):
 * `d2d_blit_ex` na uzsi obdelnik nastavi `FGOR`/`OOR` = `800 - w`, tedy STRIDED
 * pristup: kazdy radek zacina jinde a SDRAM musi otevrit jinou radu. LTDC pritom
 * cte snimek na panel sekvencne a na otevrenych radach zavisi — strided kopie mu
 * je pod rukama zavira, FIFO podtece a na panel vyleze POSKOZENY SNIMEK.
 * Merena data (injektor `tap 1`, 20 stisku RUN/STOP, `fbdiff` rozklad po fazich):
 *   - podteceni po fazich: kresleni 0 | cekani 0 | flip 0 | COPY-FORWARD 38
 *     -> VSECHNA podteceni vznikala tady, ani jedno pri kresleni aplikace;
 *   - plny render (`ui`) pritom kopiroval 768 kB (VIC dat) a mel podteceni NULA,
 *     protoze jde jednim LINEARNIM prenosem (`FGOR`=`OOR`=0).
 * Vic bajtu linearne je tedy levnejsi nez min bajtu skakave. Pas na plnou sirku
 * ma `FGOR`=`OOR`=0 stejne jako plna kopie.
 *
 * ⚠️ Kopirovat VIC nez dirty je bezpecne: `front` je nejnovejsi hotovy snimek a
 * `back` je o dva snimky starsi VSUDE, takze kazdy zkopirovany pixel navic muze
 * back jen priblizit k front, nikdy naopak. (Drivejsi komentar zakazoval
 * "bbox-merge" — ten ale spojoval i pres osu Y, takze mohl kopirovat temer cely
 * snimek i kvuli dvema malym obdelnikum na opacnych koncich; slevani JEN po Y
 * intervalech tuhle past nema.)
 *
 * ⚠️ BEZ inval: copy-forward cilove pixely CPU nikdy necte ve stale stavu — bud
 * je cte LTDC/DMA2D primo ze SDRAM, nebo je pristi snimek prepise; a CPU AA text
 * vzdy kresli pres CERSTVY clear teho snimku (ne pres copy-forward). */
static void copy_forward_dedup(void)
{
    /* y-intervaly vsech dirty obdelniku (konec je EXKLUZIVNI) */
    static struct { int16_t y0, y1; } band[2 * MAX_DIRTY];
    int nb = 0;
    for (int pass = 0; pass < 2; pass++) {
        const prim_rect_t *src = pass ? d_cur : d_prev;
        int n = pass ? nd_cur : nd_prev;
        for (int i = 0; i < n; i++) {
            int16_t y0 = src[i].y;
            int16_t y1 = (int16_t)(src[i].y + src[i].h);
            if (y0 < 0) y0 = 0;
            if (y1 > FB_H) y1 = FB_H;
            if (y1 <= y0) continue;
            band[nb].y0 = y0; band[nb].y1 = y1; nb++;
        }
    }
    if (nb == 0) { g_fb_last_copy_rects = 0u; return; }

    /* serad podle y0 (insertion sort, nb <= 2*MAX_DIRTY = 96) */
    for (int i = 1; i < nb; i++) {
        int16_t a0 = band[i].y0, a1 = band[i].y1;
        int j = i - 1;
        while (j >= 0 && band[j].y0 > a0) { band[j + 1] = band[j]; j--; }
        band[j + 1].y0 = a0; band[j + 1].y1 = a1;
    }
    /* sluc prekryvajici se i navazujici intervaly */
    int nm = 0;
    for (int i = 0; i < nb; i++) {
        if (nm > 0 && band[i].y0 <= band[nm - 1].y1) {
            if (band[i].y1 > band[nm - 1].y1) band[nm - 1].y1 = band[i].y1;
        } else {
            band[nm].y0 = band[i].y0; band[nm].y1 = band[i].y1; nm++;
        }
    }

    g_fb_last_copy_rects = (uint32_t)nm;            /* diagnostika F-0140 (`fbdiff`) */
    for (int i = 0; i < nm; i++) {
        int16_t h = (int16_t)(band[i].y1 - band[i].y0);
        int off = (int)band[i].y0 * FB_W;
        d2d_blit_ex(fb_px(s_back) + off, FB_W, fb_px(s_front) + off, FB_W, FB_W, h, 0);
    }
}

/* Pocet podteceni FIFO LTDC (`FUIF`). Kdyz LTDC nestihne z pameti nacist pixely
 * pro prave scanovany radek, vyleze na panel POSKOZENY SNIMEK — a je jedno, jak
 * zdrava je jinak SDRAM. Priznak je STICKY (drzi, dokud se nesmaze pres `ICR`),
 * takze staci cist pri kazdem flipu; nic se nezmeskа.
 * ⚠️ Zamerne bez preruseni: NVIC pro LTDC neni v `.ioc` a zapinat ho jen kvuli
 * diagnostice by bylo horsi nez tohle cteni jednoho registru. */
volatile uint32_t g_ltdc_underrun;

/* Precte a SMAZE priznak podteceni FIFO; 1 = od posledniho cteni podteklo.
 * Slouzi k rozkladu podteceni na FAZE `present()` (F-0140) — dokud se cetlo jen
 * jednou za flip, neslo rozlisit, jestli LTDC vyhladovelo KRESLENI aplikace,
 * nebo az copy-forward uvnitr `present()`. To je rozdil mezi „prekresluj min"
 * a „kopiruj jinak". */
static uint32_t ltdc_fuif_take(void)
{
    if (LTDC->ISR & LTDC_ISR_FUIF) { LTDC->ICR = LTDC_ICR_CFUIF; return 1u; }
    return 0u;
}

/* [0] pred flipem (= behem kresleni aplikace a necinnosti od minuleho present)
 * [1] behem cekani na dokonceni kresleni (`d2d_wait`)
 * [2] mezi zadosti o flip a startem copy-forwardu
 * [3] BEHEM copy-forwardu */
uint32_t g_ur_phase[4];

void prim_stm32_present(void)
{
    uint32_t u = ltdc_fuif_take();        /* FIFO podteklo od minula */
    if (u) { g_ltdc_underrun++; g_ur_phase[0]++; }

    /* NON-BLOCKING flip: cekej na PREDCHOZI flip (pri nizke kadenci OKAMZITE), ne
     * na aktualni. Diky 3. bufferu copy-forward nikdy nepise do scanovaneho bufferu. */
    uint32_t guard = 0;
    while ((LTDC->SRCR & LTDC_SRCR_VBR) && ++guard < LTDC_FLIP_GUARD) { /* posl. flip dobiha */ }
    if (guard >= LTDC_FLIP_GUARD) g_ltdc_flip_timeouts++;   /* audit F-0033 */

    d2d_wait();                                   /* dokresli back */
    if (ltdc_fuif_take()) { g_ltdc_underrun++; g_ur_phase[1]++; }

    /* Flip na back pri pristim vblanku (tearing-free), bez cekani. */
    LTDC_Layer1->CFBAR = s_fb_addr[s_back];
    LTDC->SRCR = LTDC_SRCR_VBR;

    s_front = s_back;
    s_back  = (s_front + 1) % NUM_FB;
    if (ltdc_fuif_take()) { g_ltdc_underrun++; g_ur_phase[2]++; }

    /* Copy-forward JEN dirty oblasti (sjednoceni prev+cur) z front -> novy back.
     * Pri full priznaku nebo prilis mnoha obdelnicich kopiruj cely snimek. */
    s_in_present = 1;
    g_fb_back_count[s_back]++;                     /* diagnostika F-0140 (`fbdiff`) */
    if (dfull_prev || dfull_cur) {
        d2d_blit_ex(fb_px(s_back), FB_W, fb_px(s_front), FB_W, FB_W, FB_H, 0);  /* copy-forward: bez inval */
        g_fb_last_copy_rects = 0xFFFFFFFFu;        /* = plna kopie */
        g_fb_full_copies++;
    } else {
        copy_forward_dedup();          /* sjednoceni prev+cur bez redundantnich blitu */
    }
    s_in_present = 0;
    if (ltdc_fuif_take()) { g_ltdc_underrun++; g_ur_phase[3]++; }

    /* Posun historie: prev <- cur, cur <- prazdne. */
    memcpy(d_prev, d_cur, (size_t)nd_cur * sizeof(prim_rect_t));
    nd_prev = nd_cur; dfull_prev = dfull_cur;
    nd_cur = 0; dfull_cur = 0;

    s_appfb->pixels = fb_px(s_back);
    prim_set_target(s_appfb);
}

/* Mrtvy cas DMA2D mezi dvema AXI pristupy (`DMA2D_AMTCR.DT`, jednotka = takty
 * AHB). 0 = vypnuto. Runtime laditelne pres UART `d2ddt <n>`, aby se spravna
 * hodnota dala najit BEZ preflashovani — meritkem je `LTDC: podteceni FIFO`
 * ve `status`, ktere musi klesnout na nulu. */
volatile uint8_t g_d2d_deadtime = D2D_DEADTIME_DEFAULT;

void prim_stm32_set_deadtime(uint8_t dt)
{
    g_d2d_deadtime = dt;
    /* AMTCR se smi prepsat kdykoli; DMA2D si ho cte pri kazdem prenosu. */
    DMA2D->AMTCR = dt ? (((uint32_t)dt << DMA2D_AMTCR_DT_Pos) | DMA2D_AMTCR_EN) : 0u;
}

/* Ceka, dokud LTDC aktivne skenuje panel (`LTDC_CDSR.VDES`==1), tedy dokud
 * NEzacne vertikalni zatemneni (F-0140). Timing tohoto panelu (docs/HW_REFERENCE.md):
 * 25 MHz pixel clock, 850x510 -> perioda snimku ~17,3 ms, z toho blanking jen
 * ~1,0 ms (30 radku). DMA2D burst spousteny NAHODNE vuci fazi snimku ma tedy
 * ~94% sanci startovat uprostred aktivniho skenovani (nejhorsi pripad); pockani
 * na zacatek zatemneni dá kazdemu burstu STEJNOU, nejlepsi moznou fazi mista
 * nahodne.
 * ⚠️ VYJIMKA z pravidla "zadny spin > 10 ms" (ZLATA PRAVIDLA / Vlakna): tahle
 * cekaci smycka smi trvat az ~1 periodu snimku (~17 ms), protoze (a) bezi jen
 * JEDNOU za uzivatelsky dotek, ne v pravidelnem tiku, (b) alternativa je
 * viditelne poskozeny snimek na panelu (F-0140), (c) 17 ms je hluboko pod
 * 2,5s rozpoctem heartbeatu UiTasku. `timeout_ms` je bezpecnostni strop pro
 * pripad, ze by LTDC z nejakeho duvodu neblikalo (radeji pokracovat beze
 * synchronizace nez zamrznout). */
int prim_stm32_wait_vblank(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (LTDC->CDSR & LTDC_CDSR_VDES) {
        if ((HAL_GetTick() - t0) >= timeout_ms) return 0;
    }
    return 1;
}

int prim_stm32_front_index(void) { return s_front; }
int prim_stm32_back_index(void)  { return s_back; }
uint32_t prim_stm32_fb_back_count(int i)
{
    return (i >= 0 && i < NUM_FB) ? g_fb_back_count[i] : 0u;
}
uint32_t prim_stm32_fb_last_copy_rects(void) { return g_fb_last_copy_rects; }
uint32_t prim_stm32_fb_full_copies(void)     { return g_fb_full_copies; }
uint32_t prim_stm32_ur_phase(int i)          { return (i >= 0 && i < 4) ? g_ur_phase[i] : 0u; }

/* Porovna dva framebuffery (diagnostika F-0140, popis u deklarace ve
 * `freertos_shared.h`). ⚠️ Pred ctenim MUSI prijit invalidace D-cache: do
 * framebufferu pise DMA2D mimo cache, takze CPU by jinak porovnaval stara
 * data a hlasil shodu i tam, kde zadna neni. */
uint32_t prim_stm32_fb_compare(int a, int b, int16_t *bx, int16_t *by,
                               int16_t *bw, int16_t *bh)
{
    if (bx) *bx = 0;
    if (by) *by = 0;
    if (bw) *bw = 0;
    if (bh) *bh = 0;
    if (a < 0 || a >= NUM_FB || b < 0 || b >= NUM_FB) return 0xFFFFFFFFu;

    const prim_pixel_t *pa = fb_px(a);
    const prim_pixel_t *pb = fb_px(b);
    const uint32_t bytes = (uint32_t)FB_W * FB_H * sizeof(prim_pixel_t);
    SCB_InvalidateDCache_by_Addr((uint32_t *)(void *)pa, (int32_t)bytes);
    SCB_InvalidateDCache_by_Addr((uint32_t *)(void *)pb, (int32_t)bytes);

    uint32_t n = 0;
    int x0 = FB_W, y0 = FB_H, x1 = -1, y1 = -1;
    for (int y = 0; y < FB_H; y++) {
        const prim_pixel_t *ra = pa + (size_t)y * FB_W;
        const prim_pixel_t *rb = pb + (size_t)y * FB_W;
        for (int x = 0; x < FB_W; x++) {
            if (ra[x] != rb[x]) {
                n++;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    if (x1 >= 0) {
        if (bx) *bx = (int16_t)x0;
        if (by) *by = (int16_t)y0;
        if (bw) *bw = (int16_t)(x1 - x0 + 1);
        if (bh) *bh = (int16_t)(y1 - y0 + 1);
    }
    return n;
}

void prim_stm32_init(prim_fb_t *fb)
{
    __HAL_RCC_DMA2D_CLK_ENABLE();

    /* 🔴 DMA2D MUSI PUSTIT LTDC KE SLOVU (STATUS #139).
     * `prim_stm32_present` spousti copy-forward hned po zadosti o prehozeni
     * bufferu a ZAMERNE na nej neceka — jenze LTDC v te chvili scanuje panel
     * z teze SDRAM. DMA2D je na AXI agresivni (dlouhe bursty), takze LTDC
     * nestihne naplnit FIFO -> podteceni -> POSKOZENY SNIMEK.
     * Zmereno na desce: `flip` +109 a `LTDC podteceni` +109 za 5 s, tedy
     * PRESNE JEDNO podteceni na kazdy flip — deterministicke, ne nahodne
     * vytizeni sbernice.
     * `AMTCR` vklada mezi AXI pristupy DMA2D mrtvy cas, cimz se sbernice
     * uvolni pro LTDC. Cena je pomalejsi DMA2D; proto je hodnota laditelna
     * a vychozi drzena nizko. */
    prim_stm32_set_deadtime(g_d2d_deadtime);

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
