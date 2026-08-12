/**
 * @file    sd_export.c
 * @brief   SD export přes FatFs — mount/unmount + výpis datalogu do CSV. Viz sd_export.h.
 */
#include "sd_export.h"
#include "datalog.h"          /* datalog_read_back / datalog_get_status / card-detect */
#include <stdio.h>            /* snprintf (bez %f — nano.specs) */
#include <stdlib.h>           /* abs() — desetinná část záporných teplot */
#include <string.h>

/* FatFs se objeví, až ho zapneš v CubeMX (Middleware → FATFS → SD Card).
 * Do té doby se přeloží degradovaná varianta a nic se nerozbije. */
#if defined(__has_include)
#  if __has_include("ff.h")
#    define SD_EXPORT_FATFS 1
#  endif
#endif

#ifdef SD_EXPORT_FATFS
#include "ff.h"
#include "bsp_driver_sd.h"   /* BSP_SD_Init — izolace HW vrstvy pri diagnostice */
#include "sdmmc.h"           /* hsd1 */
#endif

#define SD_EXPORT_FILE   "GPSDO.CSV"
/* ⚠️ Oddělovač `;`, ne `,`: české Excel/LibreOffice s českým národním nastavením
 * očekává středník, jinak naskládá celý řádek do jednoho sloupce. */
#define SD_CSV_SEP       ";"

static sd_export_state_t s_state = SD_EXP_NO_FATFS;

#ifdef SD_EXPORT_FATFS
static FATFS s_fs;
static bool  s_mounted;
#endif

/* ⚠️ SOUBĚH DVOU TASKŮ (nález z auditu 2026-08-12). `sd_export_tick()` běží
 * v defaultTasku a při vytažení karty volá `f_mount(NULL,…)` — jenže dlouhé
 * operace (`sd export`, `sd test`) běží v UartTasku a můžou být zrovna uvnitř
 * `f_write`. Odmountování pod rukama by rozbilo rozdělaný zápis.
 * `_FS_REENTRANT=1` chrání operace nad svazkem mutexem, ale deregistraci
 * svazku ne. Proto tenhle příznak: po dobu blokující operace se auto-unmount
 * přeskočí (karta stejně fyzicky zmizela, zápis doběhne s chybou a uklidí se). */
static volatile bool s_busy;

/* ── Levný tik (defaultTask ~2 Hz) ───────────────────────────────────────────
 * Dělá JEN to, co je rychlé: čtení GPIO + při vytažení karty odmountování
 * (`f_mount(NULL,…)` jen zahodí ukazatel, na médium nesahá).
 * ⚠️ Auto-MOUNT tu ZÁMĚRNĚ není — `HAL_SD_Init` trvá desítky až stovky ms a
 * defaultTask krmí watchdog (pravidlo „žádný spin > ~10 ms", CLAUDE.md). */
void sd_export_tick(void)
{
#ifndef SD_EXPORT_FATFS
    s_state = SD_EXP_NO_FATFS;
#else
    bool present = datalog_sd_card_present();

    if (!present) {
        /* ⚠️ Neodmountovávej pod rukama UartTasku, když zrovna běží export/test. */
        if (s_mounted && !s_busy) {
            f_mount(NULL, "", 0);      /* rychlé — jen odpojí FS z ukazatele */
            s_mounted = false;
        }
        s_state = SD_EXP_ABSENT;
        return;
    }
    /* Karta je tam. Stav ERROR držíme, dokud ji uživatel nevytáhne — jinak bychom
     * po každém neúspěšném mountu zkoušeli znovu a mlátili do vadné karty. */
    if (s_state == SD_EXP_ERROR) return;
    s_state = s_mounted ? SD_EXP_MOUNTED : SD_EXP_PRESENT;
#endif
}

sd_export_state_t sd_export_state(void) { return s_state; }

const char *sd_export_state_str(void)
{
    switch (s_state) {
        case SD_EXP_ABSENT:  return "bez karty";
        case SD_EXP_PRESENT: return "karta (nemount.)";
        case SD_EXP_MOUNTED: return "namountovano";
        case SD_EXP_ERROR:   return "CHYBA";
        default:             return "bez FatFs";
    }
}

/* ── Blokující operace — VÝHRADNĚ z UartTasku ────────────────────────────────── */

bool sd_export_mount(void)
{
#ifndef SD_EXPORT_FATFS
    return false;
#else
    if (s_mounted) return true;
    if (!datalog_sd_card_present()) { s_state = SD_EXP_ABSENT; return false; }

    /* opt=1 = mountuj hned (ne líně) — chceme vědět TEĎ, jestli je na kartě
     * čitelný souborový systém, ne až při prvním f_open. */
    if (f_mount(&s_fs, "", 1) != FR_OK) {
        s_state = SD_EXP_ERROR;        /* nejčastěji: karta není naformátovaná (FAT32) */
        return false;
    }
    s_mounted = true;
    s_state   = SD_EXP_MOUNTED;
    return true;
#endif
}

void sd_export_unmount(void)
{
#ifdef SD_EXPORT_FATFS
    if (s_mounted) { f_mount(NULL, "", 0); s_mounted = false; }
    s_state = datalog_sd_card_present() ? SD_EXP_PRESENT : SD_EXP_ABSENT;
#endif
}

#ifdef SD_EXPORT_FATFS
/* Jeden CSV řádek. ⚠️ Bez `%f` a bez `%llu` (nano.specs): kmitočet se rozloží na
 * celé Hz + 5 desetinných míst, obojí se vejde do uint32 (1,4 GHz × 1e5 by uint32
 * přeteklo, proto se dělí ještě v uint64). Sentinel DATALOG_INVALID16 → prázdná buňka. */
static int fmt_row(char *b, size_t n, const datalog_rec_t *r)
{
    uint32_t hz  = (uint32_t)(r->freq_x100000 / 100000u);
    uint32_t frc = (uint32_t)(r->freq_x100000 % 100000u);
    char toc[12] = "", tbo[12] = "", rf[12] = "";
    if (r->t_ocxo_c100  != DATALOG_INVALID16)
        snprintf(toc, sizeof toc, "%d.%02u", r->t_ocxo_c100 / 100, (unsigned)(abs(r->t_ocxo_c100) % 100));
    if (r->t_board_c100 != DATALOG_INVALID16)
        snprintf(tbo, sizeof tbo, "%d.%02u", r->t_board_c100 / 100, (unsigned)(abs(r->t_board_c100) % 100));
    if (r->rf_dbm10     != DATALOG_INVALID16)
        snprintf(rf,  sizeof rf,  "%d.%01u", r->rf_dbm10 / 10, (unsigned)(abs(r->rf_dbm10) % 10));

    return snprintf(b, n,
        "%lu" SD_CSV_SEP "%lu" SD_CSV_SEP "%lu.%05lu" SD_CSV_SEP "%s" SD_CSV_SEP "%s"
        SD_CSV_SEP "%d" SD_CSV_SEP "%s" SD_CSV_SEP "0x%02X" SD_CSV_SEP "%u" SD_CSV_SEP "%u\r\n",
        (unsigned long)r->seq, (unsigned long)r->t_unix,
        (unsigned long)hz, (unsigned long)frc,
        toc, tbo, (int)r->ocxo_vc_mv, rf,
        (unsigned)r->flags, (unsigned)r->sats, (unsigned)r->hdop10);
}
#endif

/* ── Diagnostika SD (UART `sd diag`) ─────────────────────────────────────────
 * Smysl: rozlišit, KDE to stojí. `sd mount` sám o sobě řekne jen „FAIL", ale
 * příčiny jsou dvě úplně jiné třídy:
 *   - `HAL_SD_Init` neprojde  -> HW: karta, SDMMC, hodiny, zapojení (STATUS #69)
 *   - `f_mount` neprojde      -> na kartě není čitelný FAT (stačí naformátovat)
 * Proto se nejdřív zkusí `f_mount`, a teprve při selhání se voláním `BSP_SD_Init()`
 * izoluje, jestli vůbec naběhla HW vrstva.
 * ⚠️ BLOKUJE — jen z UartTasku. */
#ifdef SD_EXPORT_FATFS
static const char *fr_str(FRESULT r)
{
    switch (r) {
        case FR_OK:                  return "OK";
        case FR_DISK_ERR:            return "DISK_ERR (nizka vrstva selhala)";
        case FR_NOT_READY:           return "NOT_READY (disk_initialize selhal)";
        case FR_NO_FILESYSTEM:       return "NO_FILESYSTEM (karta neni FAT32!)";
        case FR_INVALID_DRIVE:       return "INVALID_DRIVE";
        case FR_NOT_ENABLED:         return "NOT_ENABLED";
        case FR_WRITE_PROTECTED:     return "WRITE_PROTECTED";
        case FR_TIMEOUT:             return "TIMEOUT";
        default:                     return "?";
    }
}
#endif

void sd_export_diag(void)
{
#ifndef SD_EXPORT_FATFS
    printf("SD DIAG: FatFs neni v buildu\n");
#else
    printf("SD DIAG:\n");
    printf("  PE3 syrove : %s\n", datalog_sd_det_raw() ? "HIGH" : "LOW");
    printf("  polarita   : %s  -> vyhodnoceno: %s%s\n",
           datalog_sd_det_inverted() ? "OBRACENA (HIGH=karta)" : "vychozi (LOW=karta)",
           datalog_sd_detect_status() ? "KARTA" : "prazdno",
           datalog_sd_det_forced() ? "   [force ZAPNUT]" : "");
    printf("  tip: kdyz `sd det` hlasi opak reality, zkus `sd det invert on`\n");

    FRESULT fr = f_mount(&s_fs, "", 1);          /* opt=1 = mountuj hned */
    printf("  f_mount    : %d %s\n", (int)fr, fr_str(fr));

    if (fr == FR_OK) {
        s_mounted = true; s_state = SD_EXP_MOUNTED;
        HAL_SD_CardInfoTypeDef ci;
        if (HAL_SD_GetCardInfo(&hsd1, &ci) == HAL_OK) {
            uint32_t mb = (uint32_t)(((uint64_t)ci.LogBlockNbr * ci.LogBlockSize) >> 20);
            printf("  karta      : typ=%lu bloku=%lu x %lu B = %lu MB\n",
                   (unsigned long)ci.CardType, (unsigned long)ci.LogBlockNbr,
                   (unsigned long)ci.LogBlockSize, (unsigned long)mb);
        }
        DWORD fre_clust; FATFS *fs;
        if (f_getfree("", &fre_clust, &fs) == FR_OK) {
            uint32_t free_mb = (uint32_t)(((uint64_t)fre_clust * fs->csize * 512u) >> 20);
            printf("  volne      : %lu MB\n", (unsigned long)free_mb);
        }
        printf("  => SD FUNGUJE, lze exportovat (`sd export`)\n");
        return;
    }

    /* Mount selhal — izoluj, jestli vubec nabehla HW vrstva. */
    uint8_t st = BSP_SD_Init();
    printf("  BSP_SD_Init: %u %s\n", (unsigned)st,
           st == MSD_OK ? "OK (HW jede)" :
           (st == MSD_ERROR_SD_NOT_PRESENT ? "NENI KARTA (detect!)" : "CHYBA (HAL_SD_Init)"));
    if (st == MSD_OK) {
        printf("  card state : %lu (4 = TRANSFER)\n",
               (unsigned long)HAL_SD_GetCardState(&hsd1));
        printf("  => HW je v poradku, problem je souborovy system.\n");
        printf("     Naformatuj kartu na PC jako FAT32 (ne exFAT!).\n");
    } else if (st == MSD_ERROR_SD_NOT_PRESENT) {
        /* ⚠️ Tohle NENI zavada hardwaru — jen detekcni pin hlasi prazdno.
         * Drive tu byla hlaska „HW nenabehl, viz #69", ktera posilala uplne
         * spatnym smerem (nalez z auditu 2026-08-12). */
        printf("  => Zastavilo se to na DETEKCI, hardware se ani nezkousel.\n");
        printf("     Spust `sd force on` a `sd diag` znovu — tim se detekce obejde.\n");
        printf("     (Kdyz `sd det` hlasi opak reality, zkus `sd det invert on`.)\n");
    } else {
        printf("  => HW vrstva NENABEHLA. Zkontroluj: vlozena karta, ClockDiv,\n");
        printf("     zapojeni SDMMC1 (PC8-12/PD2). Viz STATUS #69.\n");
    }
    s_state = SD_EXP_ERROR;
#endif
}

/* ── Rozšířený test: skutečný zápis + čtení na kartu (UART `sd test`) ─────────
 * Tohle je to, co `sd diag` neumí — prověří CELOU cestu až na médium:
 * FatFs -> diskio -> BSP_SD -> HAL_SD -> IDMA -> karta a zpět.
 * Zapisuje **8 KB** (16 bloků po 512 B), tedy víc než jeden blok — tím se
 * vyzkouší i multi-blokový přenos a scratch buffer / cache maintenance
 * (`ENABLE_SD_DMA_CACHE_MAINTENANCE` + `ENABLE_SCRATCH_BUFFER`, viz STATUS #69).
 * Kdyby cache maintenance chyběla, čtená data se rozejdou se zapsanými a test
 * to pozná — proto se ověřuje obsah, ne jen návratové kódy.
 * ⚠️ BLOKUJE (sekundy) — jen z UartTasku. Soubor po sobě uklidí. */
#define SD_TEST_FILE   "SDTEST.BIN"
#define SD_TEST_CHUNK  512u
#define SD_TEST_CHUNKS 16u

/* ⚠️ Telo je vyclenene ZAMERNE. `s_busy` se nastavuje a rusi jen ve wrapperu
 * nize, takze ho NELZE zapomenout uvolnit na nektere z ~8 chybovych cest.
 * Prvni verze tuhle chybu presne udelala (audit 2026-08-12): priznak zustal
 * viset a natrvalo vypnul auto-unmount. Stejny vzor pouziva i `sd_export_run`. */
#ifdef SD_EXPORT_FATFS
static bool selftest_body(void)
{
    static uint8_t buf[SD_TEST_CHUNK];   /* static: 512 B na stack UartTasku je zbytecne */
    FIL f; UINT bw, br; FRESULT fr;

    /* --- zapis --- */
    fr = f_open(&f, SD_TEST_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) { printf("SD TEST: f_open(w) = %d %s\n", (int)fr, fr_str(fr)); return false; }
    for (uint32_t c = 0; c < SD_TEST_CHUNKS; c++) {
        for (uint32_t i = 0; i < SD_TEST_CHUNK; i++)
            buf[i] = (uint8_t)(c * 7u + i * 31u + 0x5Au);   /* deterministicky vzor */
        if (f_write(&f, buf, SD_TEST_CHUNK, &bw) != FR_OK || bw != SD_TEST_CHUNK) {
            printf("SD TEST: f_write blok %lu selhal (zapsano %u)\n", (unsigned long)c, (unsigned)bw);
            f_close(&f); return false;
        }
    }
    f_close(&f);   /* flush + adresar */

    /* --- cteni a overeni obsahu --- */
    fr = f_open(&f, SD_TEST_FILE, FA_READ);
    if (fr != FR_OK) { printf("SD TEST: f_open(r) = %d %s\n", (int)fr, fr_str(fr)); return false; }
    if (f_size(&f) != (FSIZE_t)(SD_TEST_CHUNK * SD_TEST_CHUNKS)) {
        printf("SD TEST: spatna velikost souboru: %lu B\n", (unsigned long)f_size(&f));
        f_close(&f); return false;
    }
    uint32_t bad = 0;
    for (uint32_t c = 0; c < SD_TEST_CHUNKS; c++) {
        if (f_read(&f, buf, SD_TEST_CHUNK, &br) != FR_OK || br != SD_TEST_CHUNK) {
            printf("SD TEST: f_read blok %lu selhal\n", (unsigned long)c);
            f_close(&f); return false;
        }
        for (uint32_t i = 0; i < SD_TEST_CHUNK; i++)
            if (buf[i] != (uint8_t)(c * 7u + i * 31u + 0x5Au)) { bad++; break; }
    }
    f_close(&f);
    f_unlink(SD_TEST_FILE);            /* uklid */

    if (bad) {
        printf("SD TEST: ❌ NESHODA v %lu z %u bloku\n", (unsigned long)bad, (unsigned)SD_TEST_CHUNKS);
        printf("  -> data se pri prenosu rozesla. Podezreni: chybi cache maintenance\n");
        printf("     (ENABLE_SD_DMA_CACHE_MAINTENANCE + ENABLE_SCRATCH_BUFFER v sd_diskio.c)\n");
        return false;
    }
    printf("SD TEST: ✅ OK — %u KB zapsano a precteno zpet bit po bitu shodne\n",
           (unsigned)(SD_TEST_CHUNK * SD_TEST_CHUNKS / 1024u));
    printf("  cela cesta FatFs -> BSP_SD -> HAL_SD -> IDMA -> karta funguje\n");
    return true;
}
#endif /* SD_EXPORT_FATFS */

bool sd_export_selftest(void)
{
#ifndef SD_EXPORT_FATFS
    printf("SD TEST: FatFs neni v buildu\n");
    return false;
#else
    if (!sd_export_mount()) { printf("SD TEST: mount selhal (%s)\n", sd_export_state_str()); return false; }

    s_busy = true;                 /* drzi auto-unmount v `sd_export_tick()` po dobu testu */
    bool ok = selftest_body();
    s_busy = false;                /* jedina cesta ven -> nelze zapomenout */
    return ok;
#endif
}

#ifdef SD_EXPORT_FATFS
/* Telo exportu — `s_busy` resi wrapper, viz komentar u `selftest_body()`. */
static int32_t export_body(uint32_t max_rec)
{
    datalog_status_t st;
    datalog_get_status(&st);
    uint32_t total = st.records;
    if (total == 0u) return 0;
    if (max_rec != 0u && max_rec < total) total = max_rec;

    FIL f;
    if (f_open(&f, SD_EXPORT_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        s_state = SD_EXP_ERROR;
        return -1;
    }

    char line[160];
    UINT bw;
    int n = snprintf(line, sizeof line,
        "seq" SD_CSV_SEP "t_unix" SD_CSV_SEP "freq_hz" SD_CSV_SEP "t_ocxo_C" SD_CSV_SEP
        "t_board_C" SD_CSV_SEP "ocxo_vc_mV" SD_CSV_SEP "rf_dBm" SD_CSV_SEP
        "flags" SD_CSV_SEP "sats" SD_CSV_SEP "hdop10\r\n");
    f_write(&f, line, (UINT)n, &bw);

    /* Chronologicky (nejstarší první) — `datalog_read_back(0)` je NEJNOVĚJŠÍ,
     * takže jdeme od konce. Pro log v souboru je vzestupný čas přirozenější. */
    int32_t written = 0;
    for (uint32_t i = total; i-- > 0; ) {
        datalog_rec_t r;
        if (!datalog_read_back(i, &r)) continue;   /* poškozený CRC / prázdný slot → přeskoč */
        n = fmt_row(line, sizeof line, &r);
        if (n <= 0) continue;
        if (f_write(&f, line, (UINT)n, &bw) != FR_OK || bw != (UINT)n) {
            /* Nejčastější příčina: karta vytažená za běhu exportu. */
            f_close(&f);
            s_state = datalog_sd_card_present() ? SD_EXP_ERROR : SD_EXP_ABSENT;
            return -1;
        }
        written++;
    }
    f_close(&f);      /* flush + aktualizace adresáře — bez toho je soubor prázdný */
    return written;
}
#endif /* SD_EXPORT_FATFS */

int32_t sd_export_run(uint32_t max_rec)
{
#ifndef SD_EXPORT_FATFS
    (void)max_rec;
    return -1;
#else
    if (!sd_export_mount()) return -1;

    s_busy = true;                 /* drzi auto-unmount po dobu zapisu */
    int32_t r = export_body(max_rec);
    s_busy = false;                /* jedina cesta ven -> nelze zapomenout */
    return r;
#endif
}
