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

    /* >> JEDNA IMPLEMENTACE PRO OBE CESTY (audit F-0123). Do 2026-09-19 si tady
     * USB cesta tech deset radku opisovala ZNOVU, prestoze komentar nad
     * `bmp_header()` tvrdil, ze ji pouzivaji obe cesty „aby se format nemohl
     * rozejit". Tvrzeni bylo nepravdive a duplicita zivá — presne to, co `L-0018`
     * zada slucovat, ne dokumentovat dvakrat. */
    uint8_t hdr[54];
    bmp_header(hdr, imgsize);
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
 * Scratch = SDRAM region 1 (`0xC0400000`, 4 MB WBWA cached).
 * >> KDO JESTE TU PAMET POUZIVA (audit F-0124 — `membench` tu chybel, a je to
 * zrovna ten nejdulezitejsi):
 *   - UART `membench` — blok `0xC0400000` (512 kB) DESTRUKTIVNE prepisuje peti
 *     vzory a retencnim testem. Kdyz ho pustis behem ukladani snimku, snimek bude
 *     poskozeny (a naopak `membench` nahlasi chybne bity, ktere zpusobil screenshot).
 *   - UART `sdram write/read` — rucni diagnostika, jednotlive slova.
 * >> Ke kolizi tedy muze dojit jen tim, ze si uzivatel dva prikazy pusti zaroven
 * z jedne konzole — a to nejde, UartTask je zpracovava SERIOVE. Prave proto tu
 * zadny zamek neni; kdyby se ale kterakoli z tech cest presunula do jine ulohy,
 * tenhle predpoklad PADA.
 *
 * ⚠️ BLOKUJE — jen z UartTasku (viz screenshot.h). */
#define SS_SCRATCH ((uint16_t *)0xC0400000u)

#ifdef SS_FATFS
/* `forced_name` != NULL: pouzij PRESNE tenhle nazev (prepise, kdyz uz existuje —
 * pouziva to `screenshot_save_all_sd()`, kde je nazev odvozeny od cisla okna a
 * opakovany beh ma umet stary snimek nahradit). `forced_name` == NULL: puvodni
 * chovani, najdi prvni volne SHOTnnn.BMP (8.3 — `_USE_LFN` je 0). */
static int screenshot_save_sd_body(const char *forced_name, char *name_out, unsigned name_sz)
{
    const uint16_t *fb = (const uint16_t *)prim_stm32_front_addr();
    if (!fb) return -1;
    if (!sd_export_mount()) return -2;

    /* 1) Zmraz snímek (viz anti-tearing výše). */
    uint16_t *snap = SS_SCRATCH;
    memcpy(snap, fb, (size_t)SS_W * SS_H * sizeof(uint16_t));

    char name[16];
    if (forced_name) {
        snprintf(name, sizeof name, "%s", forced_name);
    } else {
        /* Najdi volné jméno SHOTnnn.BMP. */
        int found = 0;
        for (unsigned i = 1; i <= 999u; i++) {
            FILINFO fno;
            snprintf(name, sizeof name, "SHOT%03u.BMP", i);
            if (f_stat(name, &fno) == FR_NO_FILE) { found = 1; break; }
        }
        if (!found) return -3;       /* 999 snímků na kartě — ať si uživatel uklidí */
    }

    /* 3) Zapiš. `FIL` staticky (nese 512B sektorový buffer — na stack UartTasku
     *    nepatří, viz stejné pravidlo v sd_export.c).
     * ⚠️ `FA_CREATE_ALWAYS`, ne `FA_CREATE_NEW`: `forced_name` (export vsech
     * oken) se pri opakovanem behu MA prepsat, ne selhat na "uz existuje". */
    static FIL f;
    if (f_open(&f, name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return -4;

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
}
#endif /* SS_FATFS */

/* ⚠️ OBALKA, ne telo. `s_busy` v `sd_export.c` musi drzet po CELOU dobu zapisu
 * vcetne vsech OSMI chybovych navratu — proto je telo vyclenene, presne jako u
 * `sd_export_run()`/`_selftest()`. Bez toho defaultTask pri vytazeni karty
 * odmountuje svazek a `ff_del_syncobj()` smaze semafor, ktery tenhle task prave
 * drzi -> zapis do uvolnene haldy FreeRTOS (audit F-0026).
 * ⚠️ `sd_export_busy_*` a `sd_blocking_*` se volaji OBOJE a resi ruzne veci —
 * viz hlavicka `sd_export.h`. `sd_blocking_*` nastavuje volajici (UART prikaz). */
int screenshot_save_sd(char *name_out, unsigned name_sz)
{
#ifndef SS_FATFS
    (void)name_out; (void)name_sz;
    return -1;                       /* FatFs není v buildu */
#else
    sd_export_busy_begin();
    int r = screenshot_save_sd_body(NULL, name_out, name_sz);
    sd_export_busy_end();
    return r;
#endif
}

/* Jako `screenshot_save_sd()`, ale pod PRESNYM jmenem (pouziva `screenshot all`
 * — export vsech oken, kazde pod jmenem odvozenym od cisla okna). Existujici
 * soubor se PREPISE (viz komentar u `FA_CREATE_ALWAYS` v tele). */
int screenshot_save_sd_named(const char *name)
{
#ifndef SS_FATFS
    (void)name;
    return -1;                       /* FatFs není v buildu */
#else
    sd_export_busy_begin();
    int r = screenshot_save_sd_body(name, NULL, 0);
    sd_export_busy_end();
    return r;
#endif
}
