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
 * 2) **DMA nebo IDMA**: SDMMC na H7 ma vlastni interni DMA (IDMA), ale pouzivaji ho
 *    jen `_DMA`/`_IT` varianty.
 *    ⚠️⚠️ **OPRAVENO 2026-08-13 — drive tu stalo, ze IDMA pouzivaji i blokujici
 *    `HAL_SD_ReadBlocks`/`WriteBlocks`. NENI TO PRAVDA** a stalo to hodne casu:
 *    blokujici varianta na H7 prehazuje data **procesorem pres FIFO**
 *    (`SDMMC_ReadFIFO()` ve smycce, viz `stm32h7xx_hal_sd.c`). Z toho plyne:
 *    - **Cache maintenance kolem nich je nejen zbytecna, ale u cteni SKODLIVA.**
 *      Data pise CPU, takze lezi v D-cache jako DIRTY; `SCB_InvalidateDCache_by_Addr`
 *      po cteni je bez zapisu zpet ZAHODI a z bufferu se precte obsah RAM = nuly.
 *      Presne tak `sd fs` hlasil "karta neni naformatovana" u karty, kterou
 *      `f_mount` (ten jde pres `_DMA`) namountoval bez problemu.
 *    - **DTCM omezeni se blokujici cesty netyka** (CPU dosahne vsude). Plati jen
 *      pro `_DMA` varianty, ktere pouziva `sd_diskio.c` pod FatFs — tam uz cache
 *      maintenance je a je spravne (zarovnany buffer / `scratch`, clean pred
 *      zapisem, invalidate po cteni).
 *    Pravidlo: **cache maintenance patri VYHRADNE k `_DMA`/`_IT` variantam.**
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
 * Poznamka k volbe: W25Q staci na ~80 dni pri 32 B/10 s (datalog ma vyhrazenou
 * 1/3 DATA regionu, `W25Q_DATALOG_SIZE`), takze SD NENI
 * nutnost — je to komfort (vyjmout, precist na PC, prakticky neomezena kapacita).
 */
#include "datalog.h"
#include "main.h"            /* SD_DET_Pin/_GPIO_Port (z .ioc) + stm32h7xx_hal.h */
#include <stddef.h>   /* NULL (erase = NULL, SD mazani nepotrebuje) */
#include <string.h>   /* memcpy/memset — 512B RMW layer + selftest */
#include <stdio.h>    /* snprintf/printf — CSV zrcadlo nize (bez %f — nano.specs) */

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
/* SD_DET_STABLE_N — viz datalog.h (sdileno se sd_export.c, ktery na ni stavi
 * vlastni zaruku proti falesnemu pipnuti pri bootu; jeden zdroj hodnoty,
 * ne dve nezavisle "3"). */

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

/* ── Debounce detekce karty: DOTAZ a AKTUALIZACE jsou oddelene (audit F-0030) ──
 * 🔴 Do 2026-09-11 aktualizoval stav KAZDY dotaz — a dotazuji se TRI ulohy:
 * defaultTask (`sd_export_tick`), UiTask (`sd_export_ui_info`) a UartTask
 * (`sd det`, `sd_export_mount/unmount`, `export_body`, `sd_export_format`).
 * Melo to dva nasledky: (a) `cnt`/`stable` je neatomicky read-modify-write nad
 * sdilenym stavem, (b) casova konstanta debounce byla NEDEFINOVANA — tri
 * nezavisle kadence se scitaly, takze `SD_DET_STABLE_N = 3` neodpovidalo zadnemu
 * skutecnemu casu, ackoli komentar tvrdil „casovou konstantu urcuje kadence
 * volajiciho" (coz platilo, dokud byl volajici jeden).
 * Ted stav posouva VYHRADNE `datalog_sd_det_tick()` z defaultTasku; dotaz uz jen
 * cte. Kadence je tim jedna a znama: `sd_export_tick` -> preklopeni do 3 tiku. */
static uint8_t s_det_stable, s_det_cnt;

void datalog_sd_det_tick(void)
{
    if (s_det_force) { s_det_stable = 1u; s_det_cnt = 0u; return; }
    uint8_t now = datalog_sd_detect_status() ? 1u : 0u;
    if (now == s_det_stable)             s_det_cnt = 0;
    else if (++s_det_cnt >= SD_DET_STABLE_N) { s_det_stable = now; s_det_cnt = 0; }
}

bool datalog_sd_card_present(void)
{
    if (s_det_force) return true;
    return s_det_stable != 0u;
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
    /* ⚠️ ZADNA cache maintenance — `HAL_SD_ReadBlocks` je CPU/FIFO cesta (viz rozbor
     * v hlavicce souboru). Invalidace po cteni by ZAHODILA prave nactena data. */
    if (HAL_SD_ReadBlocks(&hsd1, s_bounce, lba, 1u, SD_XFER_MS) != HAL_OK) return false;
    memcpy(b, s_bounce, SD_BLK);
    return true;
}

static bool sd_hal_wr(void *ctx, uint32_t lba, const uint8_t *b)
{
    (void)ctx;
    if (!sd_still_there() || !s_sd_ok) return false;
    memcpy(s_bounce, b, SD_BLK);
    /* ⚠️ Zadny clean — `HAL_SD_WriteBlocks` cte buffer procesorem (FIFO), takze
     * si data vezme z vlastni cache. Viz rozbor v hlavicce souboru. */
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

    /* ⚠️ `HAL_SD_Init` děláme TADY, ne v `MX_SDMMC1_SD_Init()` — ta má na selhání
     * `Error_Handler()` (= `bootled_fail()`, mrtvý přístroj), a `HAL_SD_Init`
     * selže pokaždé, když není vložená karta. Proto je generovaná funkce vyřazená
     * early-returnem v USER CODE (viz sdmmc.c) a lifecycle SD vlastní tenhle soubor.
     * Chybějící karta = `probe()` vrátí false = datalog jede dál na W25Q.
     *
     * `hsd1.Init` se tu ale UŽ NEPLNÍ. Vyplní ho `MX_SDMMC1_SD_Init()` volaná
     * z `main.c` (jen vyplní handle a vrátí se), takže je hotový dřív, než sem
     * kdy dojde řízení. Dřív tu byla TŘETÍ kopie těch hodnot — nedosažitelná,
     * a přitom třetí místo, které se muselo ručně srovnávat s `.ioc`. Smazána;
     * zdroj pravdy je `.ioc` → `sdmmc.c`. */
    if (hsd1.Instance == NULL) return false;   /* early-return v sdmmc.c chybí/rozbitý */

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

/* ═══════════════════════════════════════════════════════════════════════════
 * Automaticky rostouci CSV zrcadlo na SD kartu (2026-09-23)
 * ═══════════════════════════════════════════════════════════════════════════
 * PROC takhle, ne "SD jako primarni uloziste": viz `sd_export.h` "ARCHITEKTURA"
 * (2026-08-11) — W25Q zustava JEDINY autoritativni zdroj (kontinuita `seq`,
 * zadna ztrata dat pri vytazeni karty). Tohle zrcadlo jen PRUBEZNE DOPISUJE
 * nove W25Q zaznamy do rostouciho CSV souboru na karte, kdyz je pripravena —
 * vytazeni karty export jen pozastavi, W25Q log bezi dal beze zmeny.
 *
 * ⚠️ Bezi VYHRADNE z UartTasku (`datalog_mirror_service()`, volana vedle
 * `sd_export_service()`) — FatFs zapis blokuje (`f_write`/`f_sync` az stovky
 * ms), defaultTask/UiTask NESMI (viz sd_export.h "VLAKNA").
 *
 * Identita karty: HW seriove cislo z CID (`HAL_SD_GetCardCID`), NE FAT volume
 * serial — `_USE_LABEL` je v tomhle projektu vypnuty (ffconf.h), a i kdyby
 * nebyl, volume serial se zmeni pri kazdem `f_mkfs`, zatimco CID je vypalene
 * v cipu karty. Diky tomu prezije i preformatovani TEZE karty (spravne se
 * zacne znovu od nuly — stary soubor uz neexistuje).
 *
 * Vodotisk (posledni exportovany `seq`) + identita karty se persistuji v
 * syscfg (`datalog_mirror_restore`/`_seq`/`_vsn`), takze export po
 * power-cyklu pokracuje presne tam, kde skoncil.
 */
#ifdef HAL_SD_MODULE_ENABLED
#if defined(__has_include)
#  if __has_include("ff.h")
#    define DATALOG_MIRROR_FATFS 1
#  endif
#endif
#endif

#ifdef DATALOG_MIRROR_FATFS
#include "ff.h"
#include "sd_export.h"   /* sd_export_csv_header/_row (JEDEN zdroj CSV formatu),
                           * sd_export_ui_info (stav mountu), sd_blocking_*,
                           * sd_export_busy_* — viz jejich vlastni hlavicky */

#define DL_MIRROR_FILE          "GPSDOLOG.CSV"
#define DL_MIRROR_TICK_MS       2000u   /* jak casto kontrolovat nove zaznamy */
#define DL_MIRROR_FREE_CHECK_MS (10u * 60u * 1000u)  /* prehodnoceni volneho mista */
#define DL_MIRROR_FREE_MIN_MB   20u     /* pod touto hranici se export pozastavi */
#define DL_MIRROR_BATCH_MAX     64u     /* zaznamu za jeden tik (~11 ms QSPI) */
#define DL_MIRROR_TAIL_BUF     640u     /* > 4x nejdelsi radek (~155 B) — F-0142 */

static bool     s_mirror_en;         /* zapnuto uzivatelem (persist syscfg) */
static bool     s_mirror_open;       /* soubor na AKTUALNI karte je otevreny */
static uint32_t s_mirror_seq;        /* vodotisk: posledni seq v souboru (persist) */
static uint32_t s_mirror_vsn;        /* CID karty, ke ktere `s_mirror_seq` patri (persist) */
static uint32_t s_mirror_next_ms;    /* HAL_GetTick() dalsi kontroly */
static uint32_t s_mirror_free_ms;    /* HAL_GetTick() dalsi kontroly volneho mista */
static bool     s_mirror_low_space;
static char     s_mirror_msg[40] = "vypnuto";
static FIL      s_mirror_fil;        /* staticky — viz pravidlo "FIL nikdy na stacku" */

void datalog_mirror_set_enabled(bool on)
{
    s_mirror_en = on;
    if (!on) {
        /* 🔴 F-0146: predtim se tu jen nastavilo `s_mirror_open=false` BEZ
         * `f_close()` — `s_mirror_fil` tak zustal v FatFs vedeny jako otevreny
         * objekt. Pristi `mirror_open()` (po znovu-zapnuti) pak `f_open()` se
         * STEJNYM jmenem selhal (nalezeno na HW 2026-09-24 po pridani FA_READ
         * pro F-0142 — kombinovany rezim ctení+zapis narazil na zamek uz
         * drzeny tim starym, nikdy nezavrenym objektem). `f_close()` na
         * nikdy neotevrenem/uz zavrenem `FIL` je bezpecny (FatFs vrati chybu,
         * kterou tu zamerne ignorujeme — neni co delat jinak). Volajici
         * (UART `datalog mirror off`) bezi z UartTasku, kde je blokovani OK. */
        if (s_mirror_open) {
            sd_blocking_begin();
            sd_export_busy_begin();
            f_close(&s_mirror_fil);
            sd_export_busy_end();
            sd_blocking_end();
        }
        s_mirror_open = false;   /* dalsi zapnuti zacne cistym otevrenim */
        snprintf(s_mirror_msg, sizeof s_mirror_msg, "vypnuto");
    }
}
bool datalog_mirror_enabled(void) { return s_mirror_en; }

uint32_t datalog_mirror_seq(void) { return s_mirror_seq; }
uint32_t datalog_mirror_vsn(void) { return s_mirror_vsn; }
void datalog_mirror_restore(bool en, uint32_t seq, uint32_t vsn)
{
    s_mirror_en  = en;
    s_mirror_seq = seq;
    s_mirror_vsn = vsn;
}

const datalog_mirror_status_t *datalog_mirror_status(void)
{
    static datalog_mirror_status_t st;
    st.active = (s_mirror_en && s_mirror_open) ? 1u : 0u;
    st.exported_seq = s_mirror_seq;
    datalog_status_t ds; datalog_get_status(&ds);
    st.pending = (ds.last_seq > s_mirror_seq) ? (ds.last_seq - s_mirror_seq) : 0u;
    snprintf(st.msg, sizeof st.msg, "%s", s_mirror_msg);
    return &st;
}

/* HW identita karty (CID product serial) — viz zduvodneni v hlavicce bloku.
 * @return true = precteno. */
static bool mirror_card_vsn(uint32_t *vsn)
{
    HAL_SD_CardCIDTypeDef cid;
    if (HAL_SD_GetCardCID(&hsd1, &cid) != HAL_OK) return false;
    *vsn = cid.ProdSN;
    return true;
}

/* F-0142: vodotisk `s_mirror_seq` v syscfg je debouncovany (`syscfg_flash_tick`
 * ceka 1,5 s klidu) a je SOUCASTI stejneho blobu, ktery se meni pri KAZDE
 * uspesne davce — pri dlouhem dohaneni zalohy (kazdy tik `datalog_mirror_service`
 * ~2 s) blob tedy nikdy neztichne a debounced zapis nenabehne, dokud dohaneni
 * neskonci. Vypadek napajeni v tom okne by pak s ulozenym (starym) vodotiskem
 * znovu zapsal radky, ktere uz v souboru JSOU (duplicity).
 * Reseni: pri kazdem otevreni na STEJNE karte precist POSLEDNI KOMPLETNI radek
 * souboru (obsah, ne ulozeny pointer — stejny princip jako `find_head()` ve
 * W25Q) a pouzit vyssi z dvojice (ulozeny vodotisk, precteny seq).
 * ⚠️ Kdyz posledni radek NENI zakonceny "\r\n" (utrzeny zapis pri predchozim
 * vypadku napajeni uprostred `f_write`), orizne se (`f_truncate`) — jinak by
 * soubor navzdy drzel polovicni radek uprostred (na konci) souboru.
 * @return true = precteno (i kdyz vyslo 0 — prazdny/jen-hlavickovy soubor). */
static bool mirror_recover_seq_from_file(uint32_t *out_seq)
{
    *out_seq = 0;
    FSIZE_t sz = f_size(&s_mirror_fil);
    if (sz == 0) return true;   /* prazdny soubor -> 0 je spravne */

    FSIZE_t start = (sz > DL_MIRROR_TAIL_BUF) ? (sz - DL_MIRROR_TAIL_BUF) : 0;
    if (f_lseek(&s_mirror_fil, start) != FR_OK) return false;
    static char buf[DL_MIRROR_TAIL_BUF + 1];   /* staticky — ne na stack UartTasku */
    UINT br = 0;
    if (f_read(&s_mirror_fil, buf, (UINT)(sz - start), &br) != FR_OK || br == 0u)
        return false;

    /* Dopredny pruchod oknem: najdi konec POSLEDNIHO a PREDPOSLEDNIHO
     * kompletniho radku ("\r\n"). Predposledni = zacatek posledniho radku. */
    UINT prev_end = 0, last_end = 0, n_lines = 0;
    for (UINT i = 1; i < br; i++) {
        if (buf[i - 1] == '\r' && buf[i] == '\n') {
            prev_end = last_end;
            last_end = i + 1u;
            n_lines++;
        }
    }
    if (n_lines == 0u) return false;   /* zadny kompletni radek v okne -> vzdat to */

    if (last_end < br) {
        /* Za poslednim kompletnim radkem jsou jeste bajty = utrzeny zapis. */
        if (f_lseek(&s_mirror_fil, start + (FSIZE_t)last_end) != FR_OK) return false;
        if (f_truncate(&s_mirror_fil) != FR_OK) return false;
        printf("datalog mirror: utrzeny radek na konci souboru orinut (%u B, F-0142)\n",
               (unsigned)(br - last_end));
    }

    UINT line_start = (n_lines >= 2u) ? prev_end : 0u;
    uint32_t v = 0; bool any = false;
    for (UINT i = line_start; i < last_end && buf[i] != ';'; i++) {
        if (buf[i] < '0' || buf[i] > '9') { any = false; break; }
        v = v * 10u + (uint32_t)(buf[i] - '0');
        any = true;
    }
    if (!any) return false;   /* hlavicka "seq;..." nebo poskozeny radek -> nepouzitelne */
    *out_seq = v;
    return true;
}

/* Otevre/vytvori soubor na AKTUALNI karte. Pri jine karte nez naposledy
 * (jina CID) zacina cistym souborem — stary vodotisk na ni neplati.
 * ⚠️ BLOKUJE — volat jen obaleno sd_blocking_begin/end + sd_export_busy_*. */
static bool mirror_open(void)
{
    uint32_t vsn;
    if (!mirror_card_vsn(&vsn)) {
        snprintf(s_mirror_msg, sizeof s_mirror_msg, "CID se nepodarilo precist");
        return false;
    }
    /* `s_mirror_vsn == 0` = jeste nikdy neidentifikovana karta (cerstvy blob) ->
     * vzdy zacit cistym souborem, i kdyby aktualni `vsn` nahodou taky vysel 0. */
    bool same_card = (s_mirror_vsn != 0u) && (vsn == s_mirror_vsn);

    if (same_card) {
        if (f_open(&s_mirror_fil, DL_MIRROR_FILE, FA_OPEN_ALWAYS | FA_READ | FA_WRITE) != FR_OK) {
            snprintf(s_mirror_msg, sizeof s_mirror_msg, "otevreni souboru selhalo");
            return false;
        }
        /* F-0142: verit OBSAHU souboru, ne jen ulozenemu vodotisku (viz
         * zduvodneni u `mirror_recover_seq_from_file`). */
        uint32_t file_seq = 0;
        if (mirror_recover_seq_from_file(&file_seq) && file_seq > s_mirror_seq) {
            printf("datalog mirror: soubor ma novejsi seq (%lu) nez ulozeny vodotisk (%lu)"
                   " -> obnoveno ze souboru (F-0142)\n",
                   (unsigned long)file_seq, (unsigned long)s_mirror_seq);
            s_mirror_seq = file_seq;
        }
        if (f_lseek(&s_mirror_fil, f_size(&s_mirror_fil)) != FR_OK) {
            f_close(&s_mirror_fil);
            snprintf(s_mirror_msg, sizeof s_mirror_msg, "seek na konec selhal");
            return false;
        }
    } else {
        if (f_open(&s_mirror_fil, DL_MIRROR_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
            snprintf(s_mirror_msg, sizeof s_mirror_msg, "vytvoreni souboru selhalo");
            return false;
        }
        char line[160];
        int n = sd_export_csv_header(line, sizeof line);
        UINT bw;
        if (f_write(&s_mirror_fil, line, (UINT)n, &bw) != FR_OK || bw != (UINT)n) {
            f_close(&s_mirror_fil);
            snprintf(s_mirror_msg, sizeof s_mirror_msg, "zapis hlavicky selhal");
            return false;
        }
        s_mirror_vsn = vsn;
        s_mirror_seq = 0u;   /* nova karta -> nova historie, i kdyby stary vodotisk neco tvrdil */
    }
    s_mirror_open = true;
    return true;
}

/* Dopise davku novych zaznamu (nejstarsi-nejdriv, jako `export_body`).
 * @return true = zapsano (i 0 novych je uspech), false = chyba (volajici
 * zavre soubor a zkusi znovu pristi tik). */
static bool mirror_write_batch(void)
{
    datalog_status_t ds; datalog_get_status(&ds);
    if (!ds.ready || ds.last_seq <= s_mirror_seq) return true;   /* nic noveho */

    /* Kdyz kruh W25Q uz prepsal to, co jsme jeste nestihli exportovat, chybejici
     * useky nejdou dohnat — posun vodotisk na nejstarsi DOSTUPNY zaznam a jednou
     * to rekni (ne spamovat kazdy tik). */
    uint32_t oldest_avail = (ds.records > 0u) ? (ds.last_seq - ds.records + 1u) : ds.last_seq + 1u;
    if (s_mirror_seq + 1u < oldest_avail) {
        printf("datalog mirror: mezera v exportu (seq %lu..%lu jiz prepsano ve W25Q)\n",
               (unsigned long)(s_mirror_seq + 1u), (unsigned long)(oldest_avail - 1u));
        s_mirror_seq = oldest_avail - 1u;
    }

    uint32_t pending = ds.last_seq - s_mirror_seq;
    uint32_t batch_n = (pending > DL_MIRROR_BATCH_MAX) ? DL_MIRROR_BATCH_MAX : pending;

    for (uint32_t k = pending; k-- > pending - batch_n; ) {
        datalog_rec_t r;
        if (!datalog_read_back(k, &r)) continue;   /* poskozeny slot -> preskoc */
        char line[160];
        int n = sd_export_csv_row(line, sizeof line, &r);
        UINT bw;
        if (f_write(&s_mirror_fil, line, (UINT)n, &bw) != FR_OK || bw != (UINT)n) {
            snprintf(s_mirror_msg, sizeof s_mirror_msg, "zapis zaznamu selhal (seq %lu)",
                     (unsigned long)r.seq);
            return false;
        }
        s_mirror_seq = r.seq;
    }
    if (f_sync(&s_mirror_fil) != FR_OK) {
        snprintf(s_mirror_msg, sizeof s_mirror_msg, "f_sync selhal");
        return false;
    }
    snprintf(s_mirror_msg, sizeof s_mirror_msg, "OK, seq %lu", (unsigned long)s_mirror_seq);
    return true;
}

/* Prubezna kontrola volneho mista — VLASTNI, protoze `sd_export_ui_info()`
 * drzi hodnotu jen z posledniho mountu/operace, ne zivou (viz sd_export.c
 * `ui_refresh_capacity_body`). Vola se zridka (viz DL_MIRROR_FREE_CHECK_MS) —
 * `f_getfree` muze byt pomaly (cely sken FAT u FAT16/vadneho FSINFO). */
static void mirror_check_freespace(void)
{
    FATFS *fs; DWORD fre_clust;
    if (f_getfree("", &fre_clust, &fs) != FR_OK) return;   /* nech puvodni stav */
    uint32_t free_mb = (uint32_t)(((uint64_t)fre_clust * fs->csize * 512u) >> 20);
    s_mirror_low_space = (free_mb < DL_MIRROR_FREE_MIN_MB);
    if (s_mirror_low_space)
        snprintf(s_mirror_msg, sizeof s_mirror_msg, "malo mista (%lu MB)", (unsigned long)free_mb);
}

void datalog_mirror_service(void)
{
    if (!s_mirror_en) return;

    const sd_ui_info_t *sd = sd_export_ui_info();
    if (sd->busy || sd->state != SD_EXP_MOUNTED) {
        /* Karta pryc / neni namountovana / bezi jiny SD ukol -> pockej.
         * Handle na predchozi kartu je uz neplatny (f_mount(NULL,..) ho
         * zneplatnil pri odmountovani), takze se na nej dal nesaha. */
        s_mirror_open = false;
        return;
    }

    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - s_mirror_next_ms) < 0) return;
    s_mirror_next_ms = now + DL_MIRROR_TICK_MS;

    sd_blocking_begin();
    sd_export_busy_begin();

    /* F-0143: W25Q log byl mezitim smazan (`datalog erase`) -> `seq` tam zacina
     * znovu od 1, tedy KLESLO pod stary vodotisk. Bez tohohle by `pending`
     * zustalo 0 (vypadalo by to jako "hotovo") a zrcadlo by tise cekalo tydny
     * az mesice, nez novy `seq` znovu doroste na starou hodnotu (L-0011 vzor —
     * status nesmi tvrdit "hotovo", co ve skutecnosti neprobehlo). Novy soubor,
     * protoze stara seq cisla se budou OPAKOVAT (nejednoznacnost v historii).
     * `s_mirror_vsn = 0` vynuti vetev FA_CREATE_ALWAYS v `mirror_open()` —
     * stejny mechanismus, jaky uz existuje pro "jina karta".
     * 🔴 F-0148 (audit 2026-09-24, nalezeno pri prezkumu VLASTNI opravy
     * F-0143): puvodni verze tohohle bloku nastavovala `s_mirror_open=false`
     * BEZ `f_close()` — presne ta chyba, kterou F-0146 o par radku vys v
     * TOMTEZ souboru opravilo. `s_mirror_fil` by zustal v FatFs `_FS_LOCK`
     * tabulce (zapnuta, `ffconf.h` FS_LOCK=2) veden jako otevreny, a nasledny
     * `mirror_open()` (FA_CREATE_ALWAYS na stejne jmeno) by pravdepodobne
     * selhal na FR_LOCKED — natrvalo, protoze `datalog_mirror_set_enabled(false)`
     * `f_close()` vola jen `if (s_mirror_open)`, ktere uz by bylo false.
     * Blok se proto presunul POD `sd_blocking_begin/sd_export_busy_begin`
     * (byvaly nekolik radku niz), aby mohl bezpecne zavrit soubor stejnym
     * zpusobem jako F-0146. */
    datalog_status_t ds0; datalog_get_status(&ds0);
    if (ds0.ready && s_mirror_seq > 0u && ds0.last_seq < s_mirror_seq) {
        printf("datalog mirror: W25Q log byl smazan (seq %lu -> %lu), zacina se novym souborem (F-0143)\n",
               (unsigned long)s_mirror_seq, (unsigned long)ds0.last_seq);
        if (s_mirror_open) f_close(&s_mirror_fil);
        s_mirror_seq = 0u;
        s_mirror_vsn = 0u;
        s_mirror_open = false;
    }

    bool ok = true;
    if (!s_mirror_open) ok = mirror_open();
    if (ok && (int32_t)(now - s_mirror_free_ms) >= 0) {
        s_mirror_free_ms = now + DL_MIRROR_FREE_CHECK_MS;
        mirror_check_freespace();
    }
    if (ok && !s_mirror_low_space) ok = mirror_write_batch();

    if (!ok) {
        f_close(&s_mirror_fil);   /* handle uz je nedoveryhodny -> zavri, zkus znova pristi tik */
        s_mirror_open = false;
    }

    sd_export_busy_end();
    sd_blocking_end();
}

#else  /* !DATALOG_MIRROR_FATFS — bez FatFs/SDMMC1 zrcadlo nic nedela */

void datalog_mirror_set_enabled(bool on) { (void)on; }
bool datalog_mirror_enabled(void)        { return false; }
void datalog_mirror_service(void)        { }
uint32_t datalog_mirror_seq(void)        { return 0u; }
uint32_t datalog_mirror_vsn(void)        { return 0u; }
void datalog_mirror_restore(bool en, uint32_t seq, uint32_t vsn) { (void)en; (void)seq; (void)vsn; }
const datalog_mirror_status_t *datalog_mirror_status(void)
{
    static datalog_mirror_status_t st;
    memset(&st, 0, sizeof st);
    snprintf(st.msg, sizeof st.msg, "bez FatFs");
    return &st;
}

#endif /* DATALOG_MIRROR_FATFS */
