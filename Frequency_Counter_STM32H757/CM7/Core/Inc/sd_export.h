/**
 * @file    sd_export.h
 * @brief   SD karta jako EXPORTNI medium (FatFs) — mount/unmount + zapis logu do CSV.
 *
 * ── ARCHITEKTURA (rozhodnuto 2026-08-11) ─────────────────────────────────────
 * **W25Q je autoritativni uloziste datalogu, SD je jen export.**
 *   - W25Q DATA region uveze ~600 dni pri 32 B/10 s -> o data se nikdy neprijde
 *   - SD slouzi k tomu, k cemu ma: vytahnout a precist na PC
 *
 * Tim odpadaji tri problemy, ktere by melo "SD jako primarni uloziste":
 *   1) **kontinuita `seq`** — log by se pri vytazeni/vlozeni karty roztrhl mezi dve media
 *   2) **prepis MBR** — RAW blokovy zapis od LBA 0 znicil kartu pro PC (viz `DATALOG_SD_RAW_OK`)
 *   3) **ztrata dat pri vyjmuti** — takhle se preresi jen export, log bezi dal
 *
 * ── VLAKNA (kriticke) ────────────────────────────────────────────────────────
 * ⚠️ `sd_export_tick()` je LEVNY (jedno cteni GPIO + pripadny rychly unmount) ->
 *    vola ho **defaultTask** ~2 Hz. Drzi pravidlo "zadny spin > ~10 ms" (CLAUDE.md).
 * ⚠️ `sd_export_mount()` a `sd_export_run()` **BLOKUJI** (HAL_SD_Init = desitky az
 *    stovky ms, zapis souboru sekundy) -> volat **VYHRADNE z UartTasku**, ktery
 *    NENI hlidany watchdogem. Z defaultTask/UiTask/FpgaTask je NEVOLAT.
 *    (Proto je export dnes jen UART prikaz — tlacitko v UI by bezelo v UiTasku a
 *    potrebovalo by worker; stejne omezeni jako `calib_save()`.)
 *
 * ⚠️ FatFs zatim NENI v projektu. Kod je za `__has_include("ff.h")` a **aktivuje se
 *    sam**, jakmile se v CubeMX zapne Middleware -> FATFS -> SD Card. Do te doby
 *    `sd_export_*` vraci "nedostupne" a nic se nerozbije.
 */
#ifndef INC_SD_EXPORT_H_
#define INC_SD_EXPORT_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SD_EXP_NO_FATFS = 0,  /* FatFs neni v buildu (CubeMX) */
    SD_EXP_ABSENT,        /* slot prazdny (card-detect PE3) */
    SD_EXP_PRESENT,       /* karta vlozena, ale nenamountovana */
    SD_EXP_MOUNTED,       /* FatFs namountovan, lze exportovat */
    SD_EXP_ERROR,         /* mount/zapis selhal (spatny FS, vadna karta) */
} sd_export_state_t;

/** Levny tik: detekce karty + AUTO-UNMOUNT pri vytazeni. Vola defaultTask ~2 Hz.
 *  Auto-MOUNT se ZAMERNE nedela — je blokujici (viz hlavicka). */
void sd_export_tick(void);

const char       *sd_export_state_str(void);

/* ── Pozadavky na BLOKUJICI operace SD ───────────────────────────────────────
 * ⚠️ `sd_export_mount()` i `sd_export_run()` blokuji stovky ms az sekundy, takze
 * je NESMI volat UiTask (hlidany watchdogem) ani defaultTask (krmi watchdog).
 * UI i auto-mount proto jen nastavi tenhle priznak a skutecnou praci udela
 * UartTask, ktery hlidany neni. Stejny vzor jako `g_screen_req` pro kresleni. */
#define SD_REQ_NONE    0u
#define SD_REQ_MOUNT   1u
#define SD_REQ_EXPORT  2u
extern volatile uint8_t g_sd_req;

/** Obslouzi cekajici pozadavek (vola VYHRADNE UartTask ve sve smycce). */
void sd_export_service(void);

/** ⚠️ BLOKUJE (desitky az stovky ms) — jen z UartTasku. @return true = namountovano. */
bool sd_export_mount(void);

/** Podrobna diagnostika (UART `sd diag`): rozlisi, jestli selhala HW vrstva
 *  (`HAL_SD_Init` — karta/SDMMC/hodiny) nebo souborovy system (`f_mount` — karta
 *  neni FAT32). ⚠️ BLOKUJE — jen z UartTasku. */
void sd_export_diag(void);
/** `sd fs` — precte LBA 0 mimo FatFs a rekne, JAKY format na karte je. */
void sd_export_fs(void);
/** `sd init` — SD inicializace po krocich se znackami (hledani mista padu). */
void sd_export_init_steps(void);

/** Rozsireny test (UART `sd test`): zapise 8 KB na kartu, precte zpet a overi
 *  OBSAH — tim prověří celou cestu FatFs -> BSP_SD -> HAL_SD -> IDMA -> karta,
 *  vcetne multi-blokoveho prenosu a cache maintenance (STATUS #69).
 *  Soubor `SDTEST.BIN` po sobe smaze. ⚠️ BLOKUJE — jen z UartTasku. */
bool sd_export_selftest(void);

/** Odmountuje (rychle). Bezpecne volat i kdyz neni namountovano. */
void sd_export_unmount(void);

/** Exportuje datalog z W25Q do CSV na karte. Mountuje sam, kdyz je potreba.
 *  ⚠️ BLOKUJE (sekundy az desitky sekund) — jen z UartTasku.
 *  @param max_rec  0 = vse, jinak jen N nejnovejsich zaznamu
 *  @return pocet zapsanych zaznamu, nebo -1 pri chybe. */
int32_t sd_export_run(uint32_t max_rec);

#endif /* INC_SD_EXPORT_H_ */
