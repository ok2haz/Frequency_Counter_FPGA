#pragma once
/**
 * @file prim_stm32_hal.h
 * @brief STM32H757 HAL bridge for libprim: triple-buffered, tearing-free RGB565
 *        display + DMA2D backend.
 *
 * 3 framebuffery ve WT-cacheable SDRAM (MPU region 0). Render cílí skrytý "back"
 * buffer; prim_stm32_present() ho flipne na LTDC scan-out při vblanku (tearing-
 * free, non-blocking) a copy-forwarduje do nového back jen ZMĚNĚNÉ oblasti
 * (dirty-rect). DMA2D backend (fill/blit) po každém zápisu invaliduje cílovou
 * D-cache (CPU anti-aliasing pak čte koherentní data). Detaily v prim_stm32_hal.c.
 */

#include <stdint.h>
#include <prim/fb.h>
#include <prim/types.h>

#ifndef PRIM_FB_ADDR
#define PRIM_FB_ADDR 0xC0000000u   /* FB0; LTDC scan-out při bootu (RGB565) */
#endif

/** Init: 3 framebuffery + DMA2D backend; libprim target = back buffer. Volat jednou. */
void prim_stm32_init(prim_fb_t *fb);

/** Flip back bufferu na LTDC (při vblanku, tearing-free) + dirty-rect copy-forward.
 *  Volat po každém snímku — ideálně jen když se něco překreslilo. */
void prim_stm32_present(void);

/** Enable (default) or disable the register-level DMA2D backend at runtime. */
void prim_stm32_use_dma2d(int enable);

/** Adresa aktualne zobrazeneho (front) framebufferu, RGB565 800x480 — screenshot. */
const void *prim_stm32_front_addr(void);

/** Kolik framebufferu se stridá (dnes 3 = triple buffering).
 *
 * ⚠️ Potrebuje to KAZDA optimalizace typu „obsah je stejny, nekresli". Takova
 * optimalizace smi preskocit kresleni teprve tehdy, kdyz uz obsah **ma kazdy
 * buffer** — jinak zustane jen v tom, do ktereho se zrovna kreslilo, a jakmile
 * se cyklus dostane na ostatni, ukazou starsi obsah = PROBLIKAVANI.
 * Copy-forward to nezachrani: kopiruje sjednoceni dirty z poslednich DVOU
 * snimku, takze kdyz se mezitim neflipuje, dirty rect z toho jedineho kresleni
 * z historie vypadne. Konstanta se proto NEDUPLIKUJE do volajicich. */
int prim_stm32_fb_count(void);

/** Pocet podteceni FIFO LTDC od bootu (`FUIF`, cteno pri kazdem flipu).
 *  Nenulove = LTDC nestiha z pameti nacitat pixely -> POSKOZENE SNIMKY na panelu
 *  (typicky nedostatek propustnosti SDRAM, ne chyba kresleni). Vypisuje `status`. */
extern volatile uint32_t g_ltdc_underrun;

/** Diagnostika DMA2D (audit F-0033). Do 2026-09-10 se chyba prenosu mazala, aniz
 *  by ji kdo cetl -> poskozeny obdelnik nezanechal stopu a vypadal jako vada
 *  pameti/panelu. Vsechna tri musi byt za normalniho provozu NULOVA. */
extern volatile uint32_t g_d2d_errors;          /**< TEIF/CEIF na dokoncenem prenosu */
extern volatile uint32_t g_d2d_timeouts;        /**< DMA2D nedobehl do meze -> prenos ZRUSEN */
extern volatile uint32_t g_ltdc_flip_timeouts;  /**< predchozi flip nedobehl do meze */

/** Nejdelsi pozorovane cekani na DMA2D v TAKTECH jadra (audit F-0036).
 *  Meritko rezervy: porovnej s mezi `D2D_WAIT_MS` (100 ms). Kdyz se priblizi,
 *  zmenilo se neco v propustnosti (takt, rozliseni, `d2ddt`) — a pozna se to
 *  TADY, ne az tichym vyprsenim meze. Prevod na us dela `status`. */
extern volatile uint32_t g_d2d_wait_max_cyc;
