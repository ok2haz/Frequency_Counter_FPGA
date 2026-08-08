/**
 * @file    datalog_sd.c
 * @brief   SD karta jako uloziste datalogu — PRIPRAVENO, ZATIM NEAKTIVNI.
 *
 * `datalog_backend_sd.probe()` vraci **false**, takze `datalog_init()` spadne na
 * W25Q. Az bude SD osazena a rozchozena, staci doplnit telo funkci nize — zadny
 * volajici se nemeni (viz abstrakce v datalog.h).
 *
 * ── CO JE POTREBA DODELAT ───────────────────────────────────────────────────
 * 1) **CubeMX (.ioc)**: zapnout **SDMMC1** (4-bit, CM7 kontext). Na desce
 *    STM32H747BIT jsou piny v listu `USB_SD_FLASH` schematu: PC8-PC11 = D0..D3,
 *    PC12 = CK, PD2 = CMD. Card-detect pin OVERIT ve schematu (u nekterych
 *    osazeni chybi -> pak se detekce dela jen pres `HAL_SD_Init`).
 *    ⚠️ SDMMC1 clock jde z PLL1Q / PLL2R — zkontrolovat, ze deleni da <= 25 MHz
 *    pro init fazi (`ClockDiv`), teprve pak zvysovat.
 * 2) **DMA nebo IDMA**: SDMMC na H7 ma vlastni interni DMA (IDMA). ⚠️ **I blokujici
 *    `HAL_SD_ReadBlocks`/`WriteBlocks` na H7 pouziva IDMA pro datovou fazi** (ne jen
 *    `_DMA` varianty) → nasledujici plati VZDY:
 *    - ⚠️⚠️ **IDMA NEDOSAHNE na DTCM (`0x20000000`).** Init (CMD/response) projde,
 *      ale prvni ReadBlocks/WriteBlocks s bufferem v DTCM tise selze/DTIMEOUT
 *      ("init OK, karta videt, DMA nejede"). V TOMTO projektu je vsak default
 *      `.data/.bss`/stack v **RAM_D1 = AXI SRAM `0x24000000`** (viz linker),
 *      takze buffer tam IDMA dosahne — pokud ho nekdo NEumisti explicitne do DTCM.
 *    - ⚠️ AXI SRAM `0x24000000` je ale **cacheable (D-cache WB)** → nutna koherence:
 *      buffer `__attribute__((aligned(32)))`, delka zaokrouhlena na 32 B,
 *      `SCB_CleanDCache_by_Addr` PRED WriteBlocks, `SCB_InvalidateDCache_by_Addr`
 *      PO ReadBlocks (jinak "DMA probehne" ale data jsou zkazena/CRC error).
 *      Nejcistsi: buffer v **ne-cachovane MPU oblasti** (jako plan pro SRAM4/D3,
 *      viz CLAUDE.md MPU) → zadna rucni maintenance. (Stejny problem jako DMA2D
 *      vs D-cache, viz CLAUDE.md "Cache koherence".)
 *    - Elektrika (viz schema list 7/7): pull-upy R56-R61 na CMD/DAT jsou, ale na
 *      SD VDD je JEN C75 100n (chybi bulk 4.7-10uF → propad pri zapisovem burstu)
 *      a na SDMMC1_CK neni serioovy tlumici odpor (~22-33R) → prekmity pri vyssim
 *      hodinovem kmitoctu = "nejede az od nejake rychlosti". Staged: init 400 kHz
 *      → 1-bit ~12-16 MHz → 4-bit → zvysovat (SDMMC ker. clk 64 MHz).
 * 3) **Blokova granularita**: SD cte/pise po 512 B blocich, NE po 32 B. Proto
 *    `erase_size = 0` (SD nepotrebuje mazani) a read/write nize musi delat
 *    **read-modify-write** jednoho 512B bloku (nebo drzet 512B cache v RAM a
 *    splachnout ji po 16 zaznamech). Bez toho by kazdy zaznam prepsal cely blok.
 * 4) **Souborovy system**: pro vyjimatelnou kartu je RAW zapis nepohodlny
 *    (PC ji neprecte). Doporuceny smer = FatFs (`Middlewares/Third_Party/FatFs`)
 *    + jeden rostouci soubor `GPSDO.LOG` se stejnymi 32B zaznamy. Pak by tento
 *    backend nebyl "blokove zarizeni", ale tenka vrstva nad `f_write`/`f_lseek`
 *    a `capacity` by se bral z volneho mista na karte.
 * 5) **Vyjmuti karty za behu**: probe() se vola jen v `datalog_init()`. Az bude
 *    SD zive, je potreba osetrit vypadek za behu (write chyba -> `s_errors`
 *    roste; zvazit fallback zpet na W25Q nebo hlaseni v okne Datalog).
 *
 * Poznamka k volbe: W25Q (64 MB) staci na ~600 dni pri 32 B/10 s, takze SD NENI
 * nutnost — je to komfort (vyjmout, precist na PC, prakticky neomezena kapacita).
 */
#include "datalog.h"
#include <stddef.h>   /* NULL (erase = NULL, SD mazani nepotrebuje) */
#include <string.h>   /* memcpy/memset — 512B RMW layer + selftest */

/* ── 512B blokový read-modify-write layer (bod 3 výše) ───────────────────────
 * Datalog pracuje s libovolným byte-offsetem/délkou (32B záznamy), SD ale čte/píše
 * po 512B blocích. Tato vrstva překládá byte-rozsah na blokové operace: čtení přes
 * hranice bloků, zápis částečného bloku = načti-uprav-zapiš (aby se nepřepsali
 * sousedé). Je **generická nad `blk_io_t`** (fn ptr na 512B rd/wr) → testovatelná
 * proti RAM fake bloku bez HW (`datalog_sd_selftest`). HAL_SD adaptér = `sd_hal_*`
 * níže (DOPLNIT po zapnutí SDMMC1, bod 1). */
#define SD_BLK 512u

typedef struct {
    bool (*rd)(void *ctx, uint32_t lba, uint8_t *buf512);        /* přečte 1 blok */
    bool (*wr)(void *ctx, uint32_t lba, const uint8_t *buf512);  /* zapíše 1 blok */
    void *ctx;
} blk_io_t;

static bool blk_read(const blk_io_t *io, uint32_t off, uint8_t *buf, uint32_t len)
{
    uint8_t blk[SD_BLK];
    while (len) {
        uint32_t lba = off / SD_BLK, in = off % SD_BLK;
        uint32_t chunk = SD_BLK - in; if (chunk > len) chunk = len;
        if (!io->rd(io->ctx, lba, blk)) return false;
        memcpy(buf, blk + in, chunk);
        buf += chunk; off += chunk; len -= chunk;
    }
    return true;
}

static bool blk_write(const blk_io_t *io, uint32_t off, const uint8_t *buf, uint32_t len)
{
    uint8_t blk[SD_BLK];
    while (len) {
        uint32_t lba = off / SD_BLK, in = off % SD_BLK;
        uint32_t chunk = SD_BLK - in; if (chunk > len) chunk = len;
        if (chunk != SD_BLK) {                 /* částečný blok -> nejdřív načti (RMW) */
            if (!io->rd(io->ctx, lba, blk)) return false;
        }
        memcpy(blk + in, buf, chunk);          /* full blok: in==0,chunk==512 -> přepíše celý */
        if (!io->wr(io->ctx, lba, blk)) return false;
        buf += chunk; off += chunk; len -= chunk;
    }
    return true;
}

/* HAL 512B adaptér — DOPLNIT po zapnutí SDMMC1 (HAL_SD_ReadBlocks/WriteBlocks,
 * cache maintenance dle bodu 2). Do té doby vrací false → sd_read/write selžou,
 * ale probe() je stejně false, takže SD backend není aktivní. */
static bool sd_hal_rd(void *ctx, uint32_t lba, uint8_t *b)       { (void)ctx; (void)lba; (void)b; return false; }
static bool sd_hal_wr(void *ctx, uint32_t lba, const uint8_t *b) { (void)ctx; (void)lba; (void)b; return false; }

static bool sd_probe(void)
{
    /* TODO: po zapnuti SDMMC1 v .ioc: HAL_SD_Init(&hsd1) == HAL_OK
     *       (+ pripadne card-detect GPIO). Do te doby SD "neni". */
    return false;
}

static bool sd_read(uint32_t off, uint8_t *buf, uint32_t len)
{
    blk_io_t io = { sd_hal_rd, sd_hal_wr, NULL };
    return blk_read(&io, off, buf, len);   /* RMW plumbing hotové; 512B primitiva = TODO */
}

static bool sd_write(uint32_t off, const uint8_t *buf, uint32_t len)
{
    blk_io_t io = { sd_hal_rd, sd_hal_wr, NULL };
    return blk_write(&io, off, buf, len);
}

/* ── Selftest RMW layeru proti RAM fake bloku (bez HW) ───────────────────────── */
typedef struct { uint8_t *mem; uint32_t nblk; } ram_blk_t;
static bool ram_rd(void *c, uint32_t lba, uint8_t *b)
{ ram_blk_t *r = c; if (lba >= r->nblk) return false; memcpy(b, r->mem + lba * SD_BLK, SD_BLK); return true; }
static bool ram_wr(void *c, uint32_t lba, const uint8_t *b)
{ ram_blk_t *r = c; if (lba >= r->nblk) return false; memcpy(r->mem + lba * SD_BLK, b, SD_BLK); return true; }

bool datalog_sd_selftest(void)
{
    static uint8_t mem[SD_BLK * 4];              /* 4 bloky (2 KB) */
    memset(mem, 0xAB, sizeof mem);               /* známá výplň = detekce korupce sousedů */
    ram_blk_t rb = { mem, 4 };
    blk_io_t io = { ram_rd, ram_wr, &rb };
    uint8_t wr[64], rd[64];

    /* a) zápis uvnitř bloku 0 (off 100, len 32); sousedé musí zůstat 0xAB */
    for (int i = 0; i < 32; i++) wr[i] = (uint8_t)(i + 1);
    if (!blk_write(&io, 100, wr, 32)) return false;
    if (!blk_read(&io, 100, rd, 32) || memcmp(wr, rd, 32) != 0) return false;
    if (mem[99] != 0xAB || mem[132] != 0xAB) return false;

    /* b) zápis PŘES hranici bloku 0/1 (off 500, len 32 -> 12 B do b0, 20 B do b1) */
    for (int i = 0; i < 32; i++) wr[i] = (uint8_t)(0x40 + i);
    if (!blk_write(&io, 500, wr, 32)) return false;
    if (!blk_read(&io, 500, rd, 32) || memcmp(wr, rd, 32) != 0) return false;
    if (mem[499] != 0xAB || mem[532] != 0xAB) return false;   /* okraje netknuty */

    /* c) čtení přes 2 bloky vrátí přesně obsah paměti */
    if (!blk_read(&io, 500, rd, 40) || memcmp(mem + 500, rd, 40) != 0) return false;

    /* d) mimo rozsah (lba >= nblk) -> chyba, ne tichý přepis */
    if (blk_write(&io, SD_BLK * 4u, wr, 8)) return false;
    return true;
}

/* erase_size = 0 -> `erase` se nikdy nevola (SD maze implicitne pri zapisu). */
const datalog_backend_t datalog_backend_sd = {
    .name = "SD", .probe = sd_probe, .read = sd_read, .write = sd_write,
    .erase = NULL, .erase_size = 0u, .capacity = 0u,
};
