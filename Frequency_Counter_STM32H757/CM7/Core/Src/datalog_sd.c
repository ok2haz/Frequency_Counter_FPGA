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

static bool sd_probe(void)
{
    /* TODO: po zapnuti SDMMC1 v .ioc: HAL_SD_Init(&hsd1) == HAL_OK
     *       (+ pripadne card-detect GPIO). Do te doby SD "neni". */
    return false;
}

static bool sd_read(uint32_t off, uint8_t *buf, uint32_t len)
{
    (void)off; (void)buf; (void)len;
    return false;   /* TODO: HAL_SD_ReadBlocks + rozbaleni 512B bloku (bod 3 vyse) */
}

static bool sd_write(uint32_t off, const uint8_t *buf, uint32_t len)
{
    (void)off; (void)buf; (void)len;
    return false;   /* TODO: read-modify-write 512B bloku (bod 3 vyse) */
}

/* erase_size = 0 -> `erase` se nikdy nevola (SD maze implicitne pri zapisu). */
const datalog_backend_t datalog_backend_sd = {
    .name = "SD", .probe = sd_probe, .read = sd_read, .write = sd_write,
    .erase = NULL, .erase_size = 0u, .capacity = 0u,
};
