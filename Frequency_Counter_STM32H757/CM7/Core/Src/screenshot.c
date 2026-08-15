/**
 * @file    screenshot.c
 * @brief   Export obrazovky do BMP — přes USB CDC nebo na SD kartu (viz screenshot.h).
 */
#include "screenshot.h"
#include "usb_console.h"
#include "hal/stm32/prim_stm32_hal.h"   /* prim_stm32_front_addr */
#include "cmsis_os2.h"
#include "sd_export.h"     /* sd_export_mount / stav karty — varianta "uloz na SD" */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define SS_FATFS 1
#  endif
#endif

#define SS_W 800
#define SS_H 480

static void le32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* Odešle blok + hned pumpne do CDC (nechá ring odtéct). */
static void emit(const uint8_t *d, uint16_t n) { usb_console_tx(d, n); usb_console_tx_pump(); }

/* Naplni 54B BITMAPINFOHEADER hlavicku BMP (24 bpp, bottom-up). Sdili ji obe
 * cesty (USB i SD), aby se format nemohl rozejit. */
static void bmp_header(uint8_t *hdr, uint32_t imgsize)
{
    memset(hdr, 0, 54);
    hdr[0] = 'B'; hdr[1] = 'M';
    le32(hdr + 2, 54u + imgsize);
    le32(hdr + 10, 54);
    le32(hdr + 14, 40);
    le32(hdr + 18, SS_W);
    le32(hdr + 22, SS_H);       /* kladna vyska = bottom-up */
    hdr[26] = 1;
    hdr[28] = 24;
    le32(hdr + 34, imgsize);
}

/* Sdileny radkovy buffer (2400 B). ⚠️ Jeden pro OBE cesty (USB i SD) — driv mel
 * kazda funkce vlastni `static row[]`, tedy 4800 B v .bss zbytecne. Obe bezi
 * VYHRADNE z UartTasku a ten je zpracovava seriove, takze se nemohou potkat. */
static uint8_t s_row[SS_W * 3];

/* Prevede jeden radek RGB565 -> BGR888 (poradi slozek dle BMP). */
static void row_565_to_bgr(const uint16_t *src, uint8_t *dst)
{
    int p = 0;
    for (int x = 0; x < SS_W; x++) {
        uint16_t px = src[x];
        dst[p++] = (uint8_t)((px & 0x1F) << 3);          /* B */
        dst[p++] = (uint8_t)(((px >> 5) & 0x3F) << 2);   /* G */
        dst[p++] = (uint8_t)(((px >> 11) & 0x1F) << 3);  /* R */
    }
}

void screenshot_emit_bmp(void)
{
    /* ⚠️ Snímá se AKTUÁLNÍ front buffer. UiTask může během ~sekundového exportu
     * flipnout na jiný buffer (triple buffering) → u ANIMOVANÉ obrazovky může
     * snímek nést pruhy ze dvou framů. U statické obrazovky (bez tiku) je to OK.
     * Řešení do budoucna: zamknout/zkopírovat FB před exportem (rozpracováno). */
    const uint16_t *fb = (const uint16_t *)prim_stm32_front_addr();
    if (!fb) return;
    uint32_t rowbytes = SS_W * 3u;             /* 2400 (násobek 4 -> bez paddingu) */
    uint32_t imgsize  = rowbytes * SS_H;
    uint32_t filesize = 54u + imgsize;

    uint8_t hdr[54]; memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    le32(hdr + 2, filesize);                   /* velikost souboru */
    le32(hdr + 10, 54);                        /* offset pixelů */
    le32(hdr + 14, 40);                        /* BITMAPINFOHEADER */
    le32(hdr + 18, SS_W);
    le32(hdr + 22, SS_H);                      /* kladná výška = bottom-up */
    hdr[26] = 1;                               /* planes */
    hdr[28] = 24;                              /* bpp */
    le32(hdr + 34, imgsize);
    emit(hdr, 54);

    for (int y = SS_H - 1; y >= 0; y--) {      /* BMP jde zdola nahoru */
        row_565_to_bgr(fb + (uint32_t)y * SS_W, s_row);
        for (uint32_t off = 0; off < rowbytes; off += 256u) {
            uint32_t c = rowbytes - off; if (c > 256u) c = 256u;
            emit(s_row + off, (uint16_t)c);
        }
        osDelay(1);                            /* nech USB odtéct (UartTask nemonitorován) */
    }
}

/* ── Uložení na SD kartu ─────────────────────────────────────────────────────
 * ⚠️ ANTI-TEARING: front buffer se NEJDŘÍV zkopíruje do SDRAM scratche a teprve
 * z té kopie se zapisuje. Zápis 1,15 MB na kartu trvá jednotky sekund a UiTask
 * mezitím klidně několikrát flipne (triple buffering) — bez kopie by snímek nesl
 * pruhy ze dvou i tří framů. Kopie 750 kB v SDRAM je proti tomu jednotky ms.
 *
 * Scratch = SDRAM region 1 (`0xC0400000`, 4 MB WBWA cached). Sdílí ho jen UART
 * příkaz `sdram write/read` (ruční diagnostika), takže ke kolizi může dojít jen
 * tím, že si uživatel oba příkazy pustí zároveň z jedné konzole — což nejde,
 * UartTask je zpracovává sériově.
 *
 * ⚠️ BLOKUJE — jen z UartTasku (viz screenshot.h). */
#define SS_SCRATCH ((uint16_t *)0xC0400000u)

int screenshot_save_sd(char *name_out, unsigned name_sz)
{
#ifndef SS_FATFS
    (void)name_out; (void)name_sz;
    return -1;                       /* FatFs není v buildu */
#else
    const uint16_t *fb = (const uint16_t *)prim_stm32_front_addr();
    if (!fb) return -1;
    if (!sd_export_mount()) return -2;

    /* 1) Zmraz snímek (viz anti-tearing výše). */
    uint16_t *snap = SS_SCRATCH;
    memcpy(snap, fb, (size_t)SS_W * SS_H * sizeof(uint16_t));

    /* 2) Najdi volné jméno SHOTnnn.BMP (8.3 — `_USE_LFN` je 0). */
    char name[16];
    int found = 0;
    for (unsigned i = 1; i <= 999u; i++) {
        FILINFO fno;
        snprintf(name, sizeof name, "SHOT%03u.BMP", i);
        if (f_stat(name, &fno) == FR_NO_FILE) { found = 1; break; }
    }
    if (!found) return -3;           /* 999 snímků na kartě — ať si uživatel uklidí */

    /* 3) Zapiš. `FIL` staticky (nese 512B sektorový buffer — na stack UartTasku
     *    nepatří, viz stejné pravidlo v sd_export.c). */
    static FIL f;
    if (f_open(&f, name, FA_CREATE_NEW | FA_WRITE) != FR_OK) return -4;

    uint32_t rowbytes = SS_W * 3u;                 /* 2400 = násobek 4 -> BMP bez paddingu */
    uint8_t  hdr[54];
    UINT bw;
    bmp_header(hdr, rowbytes * SS_H);
    if (f_write(&f, hdr, sizeof hdr, &bw) != FR_OK || bw != sizeof hdr) { f_close(&f); return -5; }

    for (int y = SS_H - 1; y >= 0; y--) {          /* BMP jde zdola nahoru */
        row_565_to_bgr(snap + (uint32_t)y * SS_W, s_row);
        if (f_write(&f, s_row, rowbytes, &bw) != FR_OK || bw != rowbytes) { f_close(&f); return -6; }
    }
    /* `f_sync` před `f_close`: kdyby zápis selhal, `f_close` už vlastní sync
     * neudělá a adresářová položka by zůstala nedopsaná (stejné poučení jako
     * u `sd test`). */
    if (f_sync(&f) != FR_OK) { f_close(&f); return -7; }
    if (f_close(&f) != FR_OK) return -8;

    if (name_out && name_sz) snprintf(name_out, name_sz, "%s", name);
    return 0;
#endif
}
