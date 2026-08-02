/**
 * @file    screenshot.c
 * @brief   Export obrazovky do BMP — viz screenshot.h. ROZPRACOVÁNO.
 */
#include "screenshot.h"
#include "usb_console.h"
#include "hal/stm32/prim_stm32_hal.h"   /* prim_stm32_front_addr */
#include "cmsis_os2.h"
#include <stdint.h>
#include <string.h>

#define SS_W 800
#define SS_H 480

static void le32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* Odešle blok + hned pumpne do CDC (nechá ring odtéct). */
static void emit(const uint8_t *d, uint16_t n) { usb_console_tx(d, n); usb_console_tx_pump(); }

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

    static uint8_t row[SS_W * 3];              /* 2400 B BSS (ne stack) */
    for (int y = SS_H - 1; y >= 0; y--) {      /* BMP jde zdola nahoru */
        const uint16_t *src = fb + (uint32_t)y * SS_W;
        int p = 0;
        for (int x = 0; x < SS_W; x++) {
            uint16_t px = src[x];              /* RGB565 -> BGR888 */
            row[p++] = (uint8_t)((px & 0x1F) << 3);          /* B */
            row[p++] = (uint8_t)(((px >> 5) & 0x3F) << 2);   /* G */
            row[p++] = (uint8_t)(((px >> 11) & 0x1F) << 3);  /* R */
        }
        for (uint32_t off = 0; off < rowbytes; off += 256u) {
            uint32_t c = rowbytes - off; if (c > 256u) c = 256u;
            emit(row + off, (uint16_t)c);
        }
        osDelay(1);                            /* nech USB odtéct (UartTask nemonitorován) */
    }
}
