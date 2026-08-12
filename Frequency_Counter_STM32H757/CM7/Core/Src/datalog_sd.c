/**
 * @file    datalog_sd.c
 * @brief   SD karta jako uloziste datalogu — PRIPRAVENO, ZATIM NEAKTIVNI.
 *
 * `datalog_backend_sd.probe()` vraci **false**, takze `datalog_init()` spadne na
 * W25Q. Az bude SD osazena a rozchozena, staci doplnit telo funkci nize — zadny
 * volajici se nemeni (viz abstrakce v datalog.h).
 *
 * ── STAV 2026-08-11 (#28) ───────────────────────────────────────────────────
 * ✅ HOTOVO: 512B RMW vrstva, `sd_hal_rd/wr` (bounce buffer + cache maintenance),
 *    `sd_probe` (kapacita z karty), selftest. Kod je za `#ifdef HAL_SD_MODULE_ENABLED`
 *    a **aktivuje se sam**, jakmile se v CubeMX zapne SDMMC1.
 * ⬅ ZBYVA (mimo tento soubor): **bod 1** (CubeMX — bez nej neni ani HAL SD driver
 *    na disku) a **rozhodnuti RAW vs FatFs** (bod 4, viz `DATALOG_SD_RAW_OK`).
 * 💡 **Symptom z STATUS #69** („init projde, karta se vidi, pak IDMA prenos selze")
 *    presne odpovida pasti v bodu 2 nize — nemusi to byt elektronika. Nova
 *    implementace `sd_hal_*` tuhle pricinu adresuje (bounce buffer mimo DTCM,
 *    zarovnany na 32 B, s clean/invalidate).
 *
 * ── PUVODNI SEZNAM (kontext k jednotlivym bodum) ─────────────────────────────
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
#include "main.h"            /* SD_DET_Pin/_GPIO_Port (z .ioc) + stm32h7xx_hal.h */
#include <stddef.h>   /* NULL (erase = NULL, SD mazani nepotrebuje) */
#include <string.h>   /* memcpy/memset — 512B RMW layer + selftest */

/* ── Card-detect (přítomnost karty + hot-plug) ───────────────────────────────
 * Socket J13 `Micro_SD_DM3AT` (schéma list 7/7): mechanický spínač mezi
 * **DET_A (pin 10) = GND** a **DET_B (pin 9) = net `SDMMC1_DET` = PE3**, který má
 * na desce **47k pull-up** na +3V3 (jeden z R56–R61).
 *   → **karta vložena = LOW**, prázdný slot = HIGH.
 *
 * ⚠️ Pin si konfigurujeme SAMI (idempotentně) — stejný regen-safe vzor jako CS pin
 * ve `fpga_freq_init`. Funguje to tedy i bez zápisu v `.ioc`; ten je stejně vhodné
 * doplnit, aby PE3 nikdo omylem nepřiřadil jinam (viz CUBEMX_CHECKLIST.md).
 *
 * Tahle část je ZÁMĚRNĚ mimo `#ifdef HAL_SD_MODULE_ENABLED`: je to čisté GPIO,
 * takže UI umí hlásit „karta vložena" i když je SD vrstva vypnutá. */
/* Pin bereme přednostně z `.ioc` (`GPIO_Label = SD_DET` → `main.h`), ať se
 * nedubluje. Fallback na PE3 natvrdo, kdyby ho někdo z `.ioc` odstranil —
 * modul pak zůstane funkční sám o sobě. */
#ifdef SD_DET_Pin
#  define SD_DET_PORT    SD_DET_GPIO_Port
#  define SD_DET_PIN     SD_DET_Pin
#else
#  define SD_DET_PORT    GPIOE
#  define SD_DET_PIN     GPIO_PIN_3
#endif
#define SD_DET_STABLE_N  3u    /* kolik po sobě jdoucích odlišných čtení překlopí stav */

static void sd_det_init(void)
{
    static bool done;
    if (done) return;
    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = SD_DET_PIN;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;          /* externí pull-up je, interní nic nestojí */
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SD_DET_PORT, &g);
    done = true;
}

/* ⚠️ OVERRIDE detekce (`sd force on`). Card-detect je jen POMŮCKA — jestli karta
 * opravdu je, definitivně řekne až `HAL_SD_Init`. Když spínač v socketu není
 * osazený/zapojený (nebo je pin jinde), nesmí to zablokovat celou SD cestu.
 * Měřeno 2026-08-12: PE3 zůstal HIGH i při zasunuté kartě → tohle je únik. */
static bool s_det_force;

/* ⚠️ POLARITA. Výchozí předpoklad (dle J13 DET_A=GND / DET_B=PE3 + pull-up):
 * **LOW = karta vložena**. Některé sokety mají ale spínač obráceně (sepnutý,
 * dokud karta NENÍ). Měřeno 2026-08-12: PE3 = HIGH se zasunutou kartou — což
 * sedí buď na „spínač nereaguje", nebo právě na **obrácenou polaritu**.
 * `sd det invert on` to přepne za běhu, bez reflashe. */
static bool s_det_invert;

void datalog_sd_det_force(bool on)  { s_det_force = on; }
int  datalog_sd_det_forced(void)    { return s_det_force ? 1 : 0; }
void datalog_sd_det_invert(bool on) { s_det_invert = on; }
int  datalog_sd_det_inverted(void)  { return s_det_invert ? 1 : 0; }

/* Syrová úroveň pinu (0 = LOW, 1 = HIGH) — diagnostika z konzole bez debuggeru.
 * Dle zapojení J13 (spínač DET_A=GND / DET_B=PE3 + 47k pull-up) má být
 * **LOW = karta vložena**, HIGH = prázdný slot. */
int datalog_sd_det_raw(void)
{
    sd_det_init();
    return (HAL_GPIO_ReadPin(SD_DET_PORT, SD_DET_PIN) == GPIO_PIN_RESET) ? 0 : 1;
}

/* Vyhodnocení detekce BEZ debounce: 1 = karta přítomna.
 * Používá ho i generovaný `fatfs_platform.c` (přes extern deklaraci), aby FatFs
 * a naše API viděly totéž — jinak by `sd force`/`sd det invert` platily jen na
 * půlku cesty. */
int datalog_sd_detect_status(void)
{
    if (s_det_force) return 1;
    int raw = datalog_sd_det_raw();          /* 0 = LOW, 1 = HIGH */
    return s_det_invert ? raw : !raw;        /* výchozí: LOW = karta vložena */
}

/* Debounce: stav se překlopí až po SD_DET_STABLE_N shodných opačných čteních.
 * Časovou konstantu určuje kadence volajícího (mechanický spínač zakmitá ~ms). */
bool datalog_sd_card_present(void)
{
    static uint8_t stable, cnt;
    if (s_det_force) return true;
    uint8_t now = datalog_sd_detect_status() ? 1u : 0u;
    if (now == stable)               cnt = 0;
    else if (++cnt >= SD_DET_STABLE_N) { stable = now; cnt = 0; }
    return stable != 0u;
}

/* ── 512B blokový read-modify-write layer (bod 3 výše) ───────────────────────
 * Datalog pracuje s libovolným byte-offsetem/délkou (32B záznamy), SD ale čte/píše
 * po 512B blocích. Tato vrstva překládá byte-rozsah na blokové operace: čtení přes
 * hranice bloků, zápis částečného bloku = načti-uprav-zapiš (aby se nepřepsali
 * sousedé). Je **generická nad `blk_io_t`** (fn ptr na 512B rd/wr) → testovatelná
 * proti RAM fake bloku bez HW (`datalog_sd_selftest`). HAL_SD adaptér = `sd_hal_*`
 * níže (DOPLNIT po zapnutí SDMMC1, bod 1). */
#define SD_BLK 512u

/* `capacity` je uint32_t → strop 4 GB; karty bývají větší. 2 GiB při 32 B/10 s
 * vystačí na ~20 let, takže víc stejně nemá smysl adresovat. */
#define DATALOG_SD_MAX_BYTES  (2u * 1024u * 1024u * 1024u - 1u)

/* ⚠️⚠️ RAW REŽIM JE DESTRUKTIVNÍ — PROTO VÝCHOZÍ 0.
 *
 * Datalog je blokové zařízení: píše od offsetu 0, tedy od **LBA 0 = MBR karty**.
 * První zápis tím zlikviduje tabulku oddílů i souborový systém — karta, kterou
 * uživatel vytáhne a strčí do PC, bude „nenaformátovaná". Zapnout datalog s
 * vloženou kartou by tedy tiše smazalo její obsah. To se nesmí stát omylem.
 *
 * Než se tohle zapne, je potřeba rozhodnout (viz bod 4 v hlavičce souboru):
 *   (a) RAW  — nastavit `DATALOG_SD_RAW_OK 1`. Rychlé, ale karta je čitelná jen
 *              tímhle přístrojem (`datalog dump` / export přes USB, #46).
 *   (b) FatFs — doporučený směr: jeden rostoucí soubor `GPSDO.LOG` se stejnými
 *              32B záznamy. PC ji přečte. Backend pak není blokové zařízení, ale
 *              tenká vrstva nad `f_write`/`f_lseek` a `capacity` = volné místo.
 * 512B RMW vrstva i `sd_hal_*` níže se hodí pro OBĚ varianty (FatFs potřebuje
 * přesně `disk_read`/`disk_write` po 512 B), takže tahle práce není zahozená. */
#ifndef DATALOG_SD_RAW_OK
#define DATALOG_SD_RAW_OK 0
#endif

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

/* ── HAL 512B adaptér ────────────────────────────────────────────────────────
 * Aktivuje se SÁM, jakmile je v CubeMX zapnutý SDMMC1 (tím se definuje
 * `HAL_SD_MODULE_ENABLED` a vygeneruje `sdmmc.c` s `hsd1`). Bez toho se přeloží
 * prázdná varianta níže a `probe()` vrací false → datalog jede dál na W25Q.
 * ⚠️ Handle ani piny tu ZÁMĚRNĚ neinicializujeme — dělá to CubeMX (`MX_SDMMC1_Init`).
 * Vlastní init by se s generovaným tloukl o `hsd1` a duplicitní symbol. */
#ifdef HAL_SD_MODULE_ENABLED

#include "sdmmc.h"      /* hsd1 (generuje CubeMX po zapnutí SDMMC1) */
#include "cmsis_os2.h"  /* osDelay — ustoupit scheduleru místo spinu */

#define SD_READY_MS  200u   /* čekání na CARD_TRANSFER (krátké — viz pravidlo níže) */
#define SD_XFER_MS   500u   /* timeout jednoho 512B přenosu */

/* ⚠️⚠️ BOUNCE BUFFER — tohle je jádro celého problému s IDMA na H7.
 *
 * Dvě nezávislé pasti (obě popsané v hlavičce souboru, bod 2):
 *   1) **IDMA NEDOSÁHNE na DTCM** (`0x20000000`). Init/CMD fáze projde, ale první
 *      ReadBlocks/WriteBlocks tiše selže → přesně symptom „init OK, karta se vidí,
 *      DMA nejede" (STATUS #69!). Tenhle buffer je v `.bss` = **RAM_D1 AXI SRAM
 *      `0x24000000`**, kam IDMA dosáhne.
 *   2) AXI SRAM je **cacheable (WB)** → nutná cache maintenance. Ta ale pracuje po
 *      32B linkách, takže ji NELZE dělat nad bufferem volajícího: `blk_read`/
 *      `blk_write` mají `uint8_t blk[512]` na **stacku bez zarovnání**, a invalidace
 *      by zasáhla sousední linky = poškození okolních dat na stacku.
 * → Vlastní STATICKÝ buffer zarovnaný na 32 B + `memcpy`. 512 B v `.bss` je levné
 *   a alignment hazard tím mizí úplně.
 *
 * Reentrance: používá se JEN z reálné SD cesty (`datalog_tick` = výhradně
 * defaultTask, viz datalog.h). Selftest jede přes `ram_rd`/`ram_wr`, takže na
 * tenhle buffer nesáhne → žádný souběh. */
static uint8_t s_bounce[SD_BLK] __attribute__((aligned(32)));
static bool    s_sd_ok;

/* ⚠️ Volá se z defaultTasku, který krmí watchdog → **žádný spin delší než ~10 ms**
 * (stejné pravidlo jako `w25q.c wait_ready`, viz CLAUDE.md). Proto `osDelay(1)`. */
static bool sd_wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - t0 > timeout_ms) return false;
        if (osKernelGetState() == osKernelRunning) osDelay(1);
    }
    return true;
}

/* Hot-removal: kartu mohl uživatel vytáhnout mezi dvěma operacemi. Kontrola PŘED
 * každým blokem je levná (jedno čtení GPIO) a ušetří ~200 ms čekání na timeout
 * mrtvé karty. Po vytažení shodíme `s_sd_ok` → další zápisy rovnou selžou a
 * datalog je počítá do `write_errors`, místo aby blokoval defaultTask. */
static bool sd_still_there(void)
{
    if (datalog_sd_card_present()) return true;
    if (s_sd_ok) { s_sd_ok = false; HAL_SD_DeInit(&hsd1); }   /* uvolni handle pro re-init */
    return false;
}

static bool sd_hal_rd(void *ctx, uint32_t lba, uint8_t *b)
{
    (void)ctx;
    if (!sd_still_there()) return false;
    if (!s_sd_ok || !sd_wait_ready(SD_READY_MS)) return false;
    /* Clean+Invalidate PŘED přenosem: kdyby v cache zůstala špinavá linka, mohla by
     * se během DMA vyplavit přes čerstvá data. Invalidate PO: aby CPU nečetl staré. */
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)(void *)s_bounce, SD_BLK);
    if (HAL_SD_ReadBlocks(&hsd1, s_bounce, lba, 1u, SD_XFER_MS) != HAL_OK) return false;
    SCB_InvalidateDCache_by_Addr((uint32_t *)(void *)s_bounce, SD_BLK);
    memcpy(b, s_bounce, SD_BLK);
    return true;
}

static bool sd_hal_wr(void *ctx, uint32_t lba, const uint8_t *b)
{
    (void)ctx;
    if (!sd_still_there() || !s_sd_ok) return false;
    memcpy(s_bounce, b, SD_BLK);
    SCB_CleanDCache_by_Addr((uint32_t *)(void *)s_bounce, SD_BLK);   /* data do RAM, ať je IDMA vidí */
    if (!sd_wait_ready(SD_READY_MS)) return false;
    if (HAL_SD_WriteBlocks(&hsd1, s_bounce, lba, 1u, SD_XFER_MS) != HAL_OK) return false;
    return sd_wait_ready(SD_READY_MS);   /* zápis musí doběhnout, než pustíme další */
}

static bool sd_probe(void)
{
    if (s_sd_ok) return true;
    if (!DATALOG_SD_RAW_OK) return false;   /* ⚠️ viz varování u DATALOG_SD_RAW_OK */
    /* Bez karty se `HAL_SD_Init` ani nezkouší — trval by stovky ms a stejně selže.
     * Tohle je zároveň to, co dělá „běh bez karty" zadarmo. */
    if (!datalog_sd_card_present()) return false;

    /* ⚠️ Init děláme TADY, ne v `MX_SDMMC1_SD_Init()` — ta má na selhání
     * `Error_Handler()` (= `bootled_fail()`, mrtvý přístroj), a `HAL_SD_Init`
     * selže pokaždé, když není vložená karta. Proto je generovaná funkce vyřazená
     * early-returnem v USER CODE (viz sdmmc.c) a lifecycle SD vlastní tenhle soubor.
     * Chybějící karta = `probe()` vrátí false = datalog jede dál na W25Q.
     *
     * ⚠️ Hodnoty MUSÍ sedět s `MX_SDMMC1_SD_Init()` v sdmmc.c (zdroj pravdy = .ioc).
     * Při změně konfigurace v CubeMX je srovnej. ClockDiv=2 → SDMMC_CK =
     * 64 MHz / (2 × 2) = **16 MHz** (viz CUBEMX_CHECKLIST.md, sekce SDMMC1). */
    if (hsd1.Instance == NULL) {
        hsd1.Instance            = SDMMC1;
        hsd1.Init.ClockEdge      = SDMMC_CLOCK_EDGE_RISING;
        hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
        hsd1.Init.BusWide        = SDMMC_BUS_WIDE_4B;
        hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
        hsd1.Init.ClockDiv       = 2;
    }
    /* Karta nemusí být vložená → HAL_SD_Init smí selhat, není to chyba. */
    if (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if (HAL_SD_Init(&hsd1) != HAL_OK) return false;
    }
    HAL_SD_CardInfoTypeDef ci;
    if (HAL_SD_GetCardInfo(&hsd1, &ci) != HAL_OK) return false;

    uint64_t bytes = (uint64_t)ci.LogBlockNbr * (uint64_t)ci.LogBlockSize;
    if (bytes > DATALOG_SD_MAX_BYTES) bytes = DATALOG_SD_MAX_BYTES;
    if (bytes < DATALOG_REC_SIZE) return false;
    datalog_backend_sd.capacity = (uint32_t)bytes;

    s_sd_ok = true;
    return true;
}

#else  /* SDMMC1 není v .ioc → SD backend neaktivní, datalog spadne na W25Q */

static bool sd_hal_rd(void *ctx, uint32_t lba, uint8_t *b)       { (void)ctx; (void)lba; (void)b; return false; }
static bool sd_hal_wr(void *ctx, uint32_t lba, const uint8_t *b) { (void)ctx; (void)lba; (void)b; return false; }
static bool sd_probe(void)                                       { return false; }

#endif /* HAL_SD_MODULE_ENABLED */

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

/* erase_size = 0 -> `erase` se nikdy nevola (SD maze implicitne pri zapisu).
 * NENI const: `capacity` vyplni sd_probe() az z vlozene karty (viz datalog.h). */
datalog_backend_t datalog_backend_sd = {
    .name = "SD", .probe = sd_probe, .read = sd_read, .write = sd_write,
    .erase = NULL, .erase_size = 0u, .capacity = 0u,
};
