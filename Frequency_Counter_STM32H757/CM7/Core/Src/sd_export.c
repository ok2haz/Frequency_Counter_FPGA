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
#include "ff_gen_drv.h"      /* Disk_drvTypeDef — reset is_initialized pri unmountu */
#include "cmsis_os2.h"       /* osDelay — polite polling v BSP_SD_GetCardState */
#endif

/* ⚠️ Rolling index misto pevneho nazvu (2026-08-13). Drive se psalo vzdy do
 * `GPSDO.CSV` s `FA_CREATE_ALWAYS`, takze kazdy dalsi export PREPSAL ten minuly —
 * snadno se tak prislo o data, ktera uz na karte byla.
 * Format `GPSDOnnn.CSV` se vejde do 8.3, takze NEPOTREBUJE `_USE_LFN` (dnes 0);
 * casove razitko v nazvu by ho vyzadovalo. Datum a cas nese sam soubor
 * (`get_fattime` z RTC), takze se poradi da urcit i tak. */
#define SD_EXPORT_MAX_IDX  999u
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

/* Auto-mount se zada jen JEDNOU po vlozeni karty (viz `sd_export_tick`). */
static uint8_t s_mount_tried;

/* ── Ochrana UiTasku pred zaseknutym HAL ─────────────────────────────────────
 * ⚠️⚠️ HAL SD je plny TESNYCH SMYCEK BEZ YIELDU, jejichz timeout je
 * `SDMMC_SWDATATIMEOUT` / `SDMMC_DATATIMEOUT` = **0xFFFFFFFF ≈ 49 dni**, tedy
 * prakticky nekonecno (`SD_SendSDStatus`, zaverecne "verify ready" v
 * `HAL_SD_Init`, cekani na `DPSMACT`). Jsou ve vendor kodu, takze je nejde
 * ohranicit ani prinutit ustoupit scheduleru.
 *
 * Kdyz se do takove smycky dostane UartTask (Normal), vyhladovi UiTask
 * (BelowNormal) -> prestane fungovat dotyk, `watchdog_kick_ui()` zestarne a
 * IWDG shodi CELOU desku. Presne to delalo `sd fs` na karte, ktera se zasekla.
 *
 * Reseni: po dobu blokujici prace se volajici task srazi na `osPriorityLow`,
 * tedy POD UiTask. Zaseknuty HAL pak zere jen zbytkovy cas — displej, dotyk
 * i watchdog bezi dal a pristroj prezije i mrtvou kartu. Zustane viset jen
 * konzole, coz je oproti resetu desky nesrovnatelne lepsi.
 * ⚠️ Kdyz se task zasekne natrvalo, prioritu uz nikdo nevrati — a to je
 * ZAMER: presne v tom stavu ji potrebujeme nizkou. */
static osPriority_t s_prio_saved;
static uint8_t      s_prio_depth;   /* vnoreni (selftest -> mount) */

void sd_blocking_begin(void)
{
    if (osKernelGetState() != osKernelRunning) return;
    if (s_prio_depth++ == 0) {
        osThreadId_t me = osThreadGetId();
        s_prio_saved = osThreadGetPriority(me);
        (void)osThreadSetPriority(me, osPriorityLow);
    }
}

void sd_blocking_end(void)
{
    if (osKernelGetState() != osKernelRunning) return;
    if (s_prio_depth && --s_prio_depth == 0)
        (void)osThreadSetPriority(osThreadGetId(), s_prio_saved);
}

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
        s_mount_tried = 0;   /* pri pristim vlozeni se zkusi znovu */
        s_state = SD_EXP_ABSENT;
        return;
    }
    /* Karta je tam. Stav ERROR držíme, dokud ji uživatel nevytáhne — jinak bychom
     * po každém neúspěšném mountu zkoušeli znovu a mlátili do vadné karty. */
    if (s_state == SD_EXP_ERROR) return;
    /* AUTO-MOUNT (2026-08-13): tik jen POZADA, mountuje UartTask. Sam mountovat
     * nesmi — `f_mount(opt=1)` vola `BSP_SD_Init` (desitky az stovky ms) a tenhle
     * kod bezi v defaultTasku, ktery krmi watchdog (pravidlo "zadny spin > 10 ms").
     * Zada se jen na HRANE (nenamountovano -> karta prítomna), takze pri trvale
     * nemountovatelne karte se to neopakuje: neuspesny mount nastavi ERROR a ten
     * se drzi do vytazeni. */
    /* ⚠️ Jen JEDNOU po vlozeni. Puvodne se zadalo pri kazdem tiku, dokud nebylo
     * namountovano — pri karte, ktera mountovat nejde, to znamenalo pokus 2x za
     * sekundu donekonecna a michalo se to do jine prace se SD. */
    if (!s_mounted && !s_mount_tried && g_sd_req == SD_REQ_NONE) {
        s_mount_tried = 1;
        g_sd_req = SD_REQ_MOUNT;
    }
    /* ⚠️ `s_mount_tried` se NEresetuje pri namountovani — jinak by rucni ODPOJIT
     * (s_mounted->false) hned spustil auto-mount a karta by se do 0,5 s vratila
     * ("odpojeni nereaguje"). Guard se povoli az VYTAZENIM karty (radek vyse:
     * !present -> s_mount_tried = 0), tedy auto-mount jen pri NOVEM vlozeni. */
    s_state = s_mounted ? SD_EXP_MOUNTED : SD_EXP_PRESENT;
#endif
}

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
    /* ⚠️ `ff_gen_drv.c` si pamatuje, ze `disk_initialize()` uz probehl, a podruhe
     * ho NEZAVOLA. Bez tohohle by mount bez karty skoncil az chybou cteni
     * (`FR_DISK_ERR`) misto cisteho `FR_NOT_READY` — tedy zavadejici diagnostika.
     * (Prevzato z postupu Frantiska, `SD_franta.md`.) */
    extern Disk_drvTypeDef disk;
    disk.is_initialized[0] = 0;
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

    /* VBAT: prazdna bunka pro zaznamy porizene pred 2026-08-17 (nezaznamenano). */
    char vb[12] = "";
    if (r->vbat_mv != DATALOG_INVALID16) snprintf(vb, sizeof vb, "%d", (int)r->vbat_mv);

    return snprintf(b, n,
        "%lu" SD_CSV_SEP "%lu" SD_CSV_SEP "%lu.%05lu" SD_CSV_SEP "%s" SD_CSV_SEP "%s"
        SD_CSV_SEP "%d" SD_CSV_SEP "%s" SD_CSV_SEP "0x%02X" SD_CSV_SEP "%u" SD_CSV_SEP "%u"
        SD_CSV_SEP "%s\r\n",
        (unsigned long)r->seq, (unsigned long)r->t_unix,
        (unsigned long)hz, (unsigned long)frc,
        toc, tbo, (int)r->ocxo_vc_mv, rf,
        (unsigned)r->flags, (unsigned)r->sats, (unsigned)r->hdop10, vb);
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

volatile uint8_t g_sd_req = SD_REQ_NONE;

/* Snapshot pro UI — plni VYHRADNE UartTask (`sd_export_service`) + levny tik. */
static sd_ui_info_t s_ui;

const sd_ui_info_t *sd_export_ui_info(void)
{
    /* Levna pole se daji obnovit kdykoli; pomale (kapacita) plni jen service. */
    s_ui.state   = (uint8_t)s_state;
    s_ui.busy    = s_busy ? 1u : 0u;
#ifdef SD_EXPORT_FATFS
    s_ui.present = datalog_sd_card_present() ? 1u : 0u;
#else
    s_ui.present = 0u;
#endif
    return &s_ui;
}

/* Zjisti typ FS + kapacitu/volne misto. ⚠️ BLOKUJE (`f_getfree` u FAT16 nebo
 * neplatneho FSINFO projde celou FAT) -> jen z UartTasku, jen po zmene stavu. */
static void ui_refresh_capacity(void)
{
#ifdef SD_EXPORT_FATFS
    s_ui.total_mb = 0; s_ui.free_mb = 0; s_ui.fs[0] = '\0';
    if (!s_mounted) return;
    FATFS *fs; DWORD fre_clust;
    if (f_getfree("", &fre_clust, &fs) != FR_OK) return;
    /* Klastry -> MB. `csize` je v sektorech, sektor 512 B -> /2048 = MB. */
    DWORD tot_clust = (fs->n_fatent - 2);
    s_ui.total_mb = (uint32_t)(((uint64_t)tot_clust * fs->csize) / 2048u);
    s_ui.free_mb  = (uint32_t)(((uint64_t)fre_clust * fs->csize) / 2048u);
    const char *n = (fs->fs_type == FS_FAT12) ? "FAT12"
                  : (fs->fs_type == FS_FAT16) ? "FAT16"
                  : (fs->fs_type == FS_FAT32) ? "FAT32" : "?";
    snprintf(s_ui.fs, sizeof s_ui.fs, "%s", n);
#endif
}

/* ⚠️⚠️ DESTRUKTIVNI: naformatuje CELOU kartu na FAT32 (f_mkfs) -> smaze vsechna
 * data. Vola se JEN po dvojim potvrzeni v UI (nebo `sd format yes yes`). BLOKUJE
 * (u velke karty i sekundy) -> vyhradne z UartTasku pres `sd_export_service`.
 * @return true = OK. */
#ifdef SD_EXPORT_FATFS
static bool sd_export_format(void)
{
    static uint8_t work[512];            /* f_mkfs work buffer (>= _MAX_SS) */
    if (!datalog_sd_card_present()) { s_state = SD_EXP_ABSENT; return false; }

    sd_export_unmount();                 /* svazek nesmi byt namountovany */
    printf("SD: FORMATUJI na FAT32 (smaze vse), cekej...\r\n");
    /* FM_FAT32 bez FM_SFD -> partitioned (MBR + jedna FAT32 particie), jako mel
     * puvodni layout karty; au=0 -> automaticka velikost clusteru. */
    FRESULT fr = f_mkfs("", FM_FAT32, 0, work, sizeof work);
    if (fr != FR_OK) {
        printf("SD: f_mkfs = %d %s\r\n", (int)fr, fr_str(fr));
        return false;
    }
    printf("SD: format OK\r\n");
    return true;
}
#endif

void sd_export_service(void)
{
    uint8_t r = g_sd_req;
    if (r == SD_REQ_NONE) return;
    g_sd_req = SD_REQ_NONE;          /* vzit driv, nez se zacne pracovat */
    sd_blocking_begin();             /* po dobu prace pod UiTask (viz sd_export.h) */
    if (r == SD_REQ_MOUNT) {
        bool ok = sd_export_mount();
        printf("SD: mount: %s (%s)\r\n", ok ? "OK" : "FAIL", sd_export_state_str());
        snprintf(s_ui.msg, sizeof s_ui.msg, "%s", ok ? "Namountovano" : "Mount selhal");
        ui_refresh_capacity();
    } else if (r == SD_REQ_UNMOUNT) {
        sd_export_unmount();
        printf("SD: odmountovano\r\n");
        snprintf(s_ui.msg, sizeof s_ui.msg, "Odmountovano - lze vyjmout");
        ui_refresh_capacity();
    } else if (r == SD_REQ_EXPORT) {
        int32_t w = sd_export_run(0);
        if (w < 0) { printf("SD: export FAIL (%s)\r\n", sd_export_state_str());
                     snprintf(s_ui.msg, sizeof s_ui.msg, "Export SELHAL"); }
        else       { printf("SD: exportovano %ld zaznamu\r\n", (long)w);
                     snprintf(s_ui.msg, sizeof s_ui.msg, "Exportovano %ld zaznamu", (long)w); }
        ui_refresh_capacity();
    } else if (r == SD_REQ_TEST) {
        bool ok = sd_export_selftest();
        if (ok && s_ui.test_w_kbs)
            snprintf(s_ui.msg, sizeof s_ui.msg, "Test OK  W:%lu R:%lu KB/s",
                     (unsigned long)s_ui.test_w_kbs, (unsigned long)s_ui.test_r_kbs);
        else
            snprintf(s_ui.msg, sizeof s_ui.msg, "%s", ok ? "Test zapis/cteni PROSEL"
                                                         : "Test SELHAL - viz konzole");
        ui_refresh_capacity();
    } else if (r == SD_REQ_FORMAT) {
#ifdef SD_EXPORT_FATFS
        bool ok = sd_export_format();
        if (ok) {                       /* po formatu rovnou namountuj */
            (void)sd_export_mount();
            snprintf(s_ui.msg, sizeof s_ui.msg, "Naformatovano FAT32");
        } else {
            snprintf(s_ui.msg, sizeof s_ui.msg, "Format SELHAL - viz konzole");
        }
        ui_refresh_capacity();
#endif
    }
    sd_blocking_end();
}

/* ── `sd fs` — CO JE NA KARTE DOOPRAVDY ──────────────────────────────────────
 * Doted jsme o formatu jen SPEKULOVALI ("karta 14,5 GB, mozna exFAT"). Tohle to
 * MERI: precte LBA 0 mimo FatFs (primo pres BSP_SD) a dekoduje ho.
 *
 * Proc to rozhoduje: `ffconf.h` ma `_FS_EXFAT = 0`, takze exFAT kartu `f_mount`
 * odmitne s `FR_NO_FILESYSTEM` — a to vypada uplne stejne jako "rozbita karta".
 * Rozliseni je jednorazova informace, ktera usetri hodiny hadani.
 *
 * ⚠️ BLOKUJE (BSP_SD_Init az stovky ms) — jen z UartTasku.
 * ⚠️ Buffer je staticky a zarovnany na 32 B + invalidace D-cache: SDMMC jede na
 *    H7 pres IDMA, ktera obchazi cache (viz `sd_hal_rd` v datalog_sd.c). */
#ifdef SD_EXPORT_FATFS
static uint8_t s_fsbuf[512] __attribute__((aligned(32)));

static void fs_show_vbr(const uint8_t *b, const char *what)
{
    /* exFAT ma "EXFAT   " na offsetu 3, FAT32 "FAT32   " na 0x52, FAT12/16 na 0x36. */
    if (memcmp(b + 3, "EXFAT   ", 8) == 0) {
        printf("  %s: exFAT  ⚠️ FatFs ho NEUMI (_FS_EXFAT = 0) -> naformatuj FAT32\r\n", what);
    } else if (memcmp(b + 0x52, "FAT32   ", 8) == 0) {
        printf("  %s: FAT32  ✅ tohle FatFs umi\r\n", what);
    } else if (memcmp(b + 0x36, "FAT16   ", 8) == 0 || memcmp(b + 0x36, "FAT12   ", 8) == 0) {
        printf("  %s: %.8s  ✅ tohle FatFs umi\r\n", what, (const char *)(b + 0x36));
    } else if (memcmp(b + 3, "NTFS    ", 8) == 0) {
        printf("  %s: NTFS  ⚠️ nepodporovano -> naformatuj FAT32\r\n", what);
    } else {
        printf("  %s: neznamy (jump=%02X%02X%02X, sig=%02X%02X)\r\n",
               what, b[0], b[1], b[2], b[510], b[511]);
    }
}
#endif

/* ── Prepsani slabeho `BSP_SD_Init()` z bsp_driver_sd.c ──────────────────────
 * Prevzato z postupu, kterym Frantisek rozchodil SD na TEMZE hardwaru
 * (`SD_franta.md`). Generovana verze ma dve slabiny:
 *
 * 1) `HAL_SD_Init()` na svem konci vola `HAL_SD_ConfigWideBusOperation(Init.BusWide)`.
 *    Kdyz je `Init.BusWide` rovnou 4B (jako u nas z `.ioc`), probehne prepnuti na
 *    4 bity JESTE UVNITR identifikace — a kdyz selze, **spadne cela inicializace**,
 *    prestoze karta v 1-bit rezimu funguje bez problemu.
 *    -> Identifikace proto bezi VZDY v 1-bit a na 4 bity se prepina az potom,
 *       s fallbackem zpet na 1-bit.
 *    ⚠️ V `.ioc` musi `SD_4_bits_Wide_bus` ZUSTAT — jen diky nemu `HAL_SD_MspInit`
 *    nakonfiguruje piny D1..D3. Lisi se pouze runtime hodnota `Init.BusWide`.
 *
 * 2) `hsd1.ErrorCode` je v HAL "sticky" (prirazuje se pres `|=`) a
 *    `HAL_SD_ConfigWideBusOperation()` na konci kontroluje jeho CELY obsah.
 *    Bez vynulovani by i uspesny fallback na 1-bit vratil chybu zdedenou
 *    z neuspesneho pokusu o 4-bit.
 *
 * `BSP_SD_Init` je v generovanem souboru `__weak`, takze tohle je regen-safe. */
#ifdef SD_EXPORT_FATFS
/* ⚠️ DAT linky (PC8..PC11 = D0..D3) maji v .ioc/`HAL_SD_MspInit` `GPIO_NOPULL`,
 * a deska nema spolehlivy EXTERNI pull-up na DAT — dle erratum (`SD_franta.md`)
 * sedi pull-up urceny pro CMD omylem na CLK, takze CMD jede na INTERNIM pull-upu
 * (PD2 = `GPIO_PULLUP`) a na DAT nezbylo nic. Plovouci DAT0 -> host nedetekuje
 * start bit datoveho bloku -> `DPSMACT` visi bez jedineho bajtu (`STA=0x1000`,
 * zmereno na 2 kartach 2026-08-14: prikazy OK, data 0). SD spec pull-upy na DAT
 * vyzaduje. Zapneme tedy INTERNI pull-up na D0..D3 — tentyz vzor jako u CMD.
 * CK (PC12) NECHAVAME NOPULL: je to hostem buzeny push-pull, pull-up nepotrebuje.
 * Idempotentni + regen-safe (nesaha na .ioc, jen prekonfiguruje piny po MspInit). */
static void sd_dat_pullup_enable(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;   /* D0..D3, NE CK(PC12) */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF12_SDIO1;
    HAL_GPIO_Init(GPIOC, &g);
}

/* ⚠️⚠️ KLIC K DATOVE CESTE (2026-08-14): naplni `hsd1.Init` PRESNE jako funkcni
 * Frantuv projekt na TEMZE HW (`H757_SDcard_01/sdcard.c sd_apply_config`).
 * Nejdulezitejsi je `HardwareFlowControl = ENABLE` — nase `.ioc` ho NEMA
 * (`SDMMC1.IPParameters=ClockDiv` bez HWFC), takze CLKCR bit17 HWFC_EN=0
 * (zmereno `CLKCR=0x51`). Na H7 SDMMC bez flow controlu datova cesta selhava:
 * blokovy prenos nedostane ani bajt, `DPSMACT` visi (prikazy pritom jedou) —
 * presne nas symptom. Franta ma HWFC zapnuty a cte/zapisuje. Nastavujeme cely
 * Init jako on. Regen-safe (runtime, nesaha na .ioc — spravne reseni je doplnit
 * HWFC i do .ioc pres CubeMX, viz CUBEMX_CHECKLIST). */
static void sd_apply_init_config(void)
{
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;   /* <<< chybelo */
    /* SDMMC_CK = 64 MHz / (2 x ClockDiv). 1 -> 32 MHz (2026-08-16, po HW uprave:
     * R60 = pull-up na CK odstranen, bulk kondik na SD VDD 10 uF). Drive 2 = 16 MHz.
     * ⚠️ 0 NEPOUZIVAT — bypass delicky by dal 64 MHz, nad SD HS limitem 50 MHz. */
    hsd1.Init.ClockDiv            = 1;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_1B; /* identifikace vzdy 1-bit */
}

/* Prepne kartu i host na 4-bit sbernici BEZ `HAL_SD_ConfigWideBusOperation`
 * (ta cte SCR neohranicenou datovou smyckou). CMD55 + ACMD6 = bezdatove prikazy
 * (5 s CMDTIMEOUT) -> bezpecne. Poradi je dulezite: nejdriv rekni KARTE (ACMD6),
 * teprve pak prepni WIDBUS na HOSTU (jinak by host cetl 4 linky, kdyz karta jeste
 * budi jen DAT0). @return true = 4-bit; false = zustava 1-bit. */
#ifdef SD_EXPORT_FATFS
static bool sd_try_4bit(void)
{
    /* CMD55 APP_CMD s RCA karty */
    if (SDMMC_CmdAppCommand(SDMMC1, (uint32_t)(hsd1.SdCard.RelCardAdd << 16)) != HAL_SD_ERROR_NONE)
        return false;
    /* ACMD6 SET_BUS_WIDTH, arg 2 = 4-bit (0 = 1-bit) */
    if (SDMMC_CmdBusWidth(SDMMC1, 2u) != HAL_SD_ERROR_NONE)
        return false;
    hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
    (void)SDMMC_Init(SDMMC1, hsd1.Init);              /* host WIDBUS = 4-bit */
    return true;
}
#endif

uint8_t BSP_SD_Init(void)
{
    if (BSP_SD_IsDetected() != SD_PRESENT) return MSD_ERROR_SD_NOT_PRESENT;

    /* ⚠️ NVIC pro SDMMC1 — bez nej `sd_diskio.c` ceka 30 s na zpravu, kterou
     * nema kdo poslat (podrobne v `stm32h7xx_it.c`, `SDMMC1_IRQHandler`).
     * Nastavuje se tady, ne v `.ioc`: je to idempotentni a regen-safe, stejnym
     * vzorem jako CS pin ve `fpga_freq_init`. Priorita 5 = strop pro obsluhy,
     * ktere smi volat FreeRTOS API. */
    HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SDMMC1_IRQn);

    /* ⚠️⚠️ ZAMERNE NEVOLAME `HAL_SD_Init()` (zmena 2026-08-13). Ta uvnitr dela
     * `HAL_SD_GetCardStatus()` (ACMD13, 64B datovy prenos) a zaverecne cekani na
     * TRANSFER — obojí s timeoutem `SDMMC_SWDATATIMEOUT`/`SDMMC_DATATIMEOUT`
     * = 0xFFFFFFFF ≈ 49 dni, tedy prakticky nekonecnou tesnou smyckou. Kdyz karta
     * to ACMD13 neodbavi, `sd fs` zamrzne a IWDG shodi desku. Presne to se stalo.
     * `HAL_SD_GetCardStatus` slouzi v `HAL_SD_Init` JEN k urceni `CardSpeed`,
     * kterou tady nikdo nepouziva -> preskocit ji nic nestoji.
     *
     * Skladame proto init z casti s rozumnym chovanim — presne tu sekvenci,
     * kterou `sd init` na teto karte proveril krok po kroku:
     *   MspInit (hodiny+piny) -> HAL_SD_InitCard -> OHRANICENE cekani na TRANSFER
     *   -> ConfigWideBusOperation(4B) s fallbackem na 1B.
     * ⚠️ `HAL_SD_InitCard` MspInit NEvola (dela to jen `HAL_SD_Init`), takze si
     * ho musime zavolat sami — jinak nebezi hodiny SDMMC1 ani nejsou piny v AF. */
    if (hsd1.State == HAL_SD_STATE_RESET) {
        hsd1.Lock = HAL_UNLOCKED;
        HAL_SD_MspInit(&hsd1);
    }
    sd_dat_pullup_enable();                         /* interni pull-up na DAT0..3 (pojistka) */
    sd_apply_init_config();                         /* HWFC ENABLE + Init jako Franta (KLIC) */

    hsd1.State = HAL_SD_STATE_PROGRAMMING;
    if (HAL_SD_InitCard(&hsd1) != HAL_OK) {
        uint32_t ec = hsd1.ErrorCode;
        hsd1.State = HAL_SD_STATE_READY;
        printf("SD: HAL_SD_Init selhal, ErrorCode=0x%08lX = %s\r\n", (unsigned long)ec,
               (ec & SDMMC_ERROR_CMD_RSP_TIMEOUT) ? "CMD_RSP_TIMEOUT (karta NEODPOVIDA na prikaz)" :
               (ec & SDMMC_ERROR_CMD_CRC_FAIL)    ? "CMD_CRC_FAIL (odpoved prisla, ale s chybnym CRC)" :
               (ec & SDMMC_ERROR_DATA_TIMEOUT)    ? "DATA_TIMEOUT" : "jiny");
        /* Registry uz cist SMIME — `HAL_SD_MspInit()` probehl uvnitr `HAL_SD_Init`,
         * takze hodiny SDMMC1 bezi. Tohle rozlisi "karta tam neni" od "je tam,
         * ale nekomunikuje": `POWER` ukaze, jestli radic vubec napajel kartu,
         * `CLKCR` skutecny takt a sirku sbernice. */
        printf("  SDMMC1 POWER=0x%08lX CLKCR=0x%08lX STA=0x%08lX\r\n",
               (unsigned long)SDMMC1->POWER, (unsigned long)SDMMC1->CLKCR,
               (unsigned long)SDMMC1->STA);
        if (ec & SDMMC_ERROR_CMD_RSP_TIMEOUT) {
            printf("  => Karta na CMD0/CMD8 vubec neodpovedela. V tomhle poradi zkontroluj:\r\n");
            printf("     1) je karta OPRAVDU zasunuta a dosednuta? PE3 hlasi 'prazdno' a\r\n");
            printf("        tenhle timeout s tim SOUHLASI — muze mit pravdu.\r\n");
            printf("     2) kontakty socketu J13 / jina karta\r\n");
            printf("     3) napajeni karty (deska ma na SD VDD jen C75 100n, chybi bulk)\r\n");
            printf("     Pozn.: pres GDB uz karta jednou nabehla (SDHC 14,5 GB, TRANSFER),\r\n");
            printf("     takze slot JAKO TAKOVY funguje — ukazuje to spis na kontakt.\r\n");
        }
        return MSD_ERROR;
    }

    /* OHRANICENE cekani na TRANSFER (HAL by tu tocil ~49 dni). 1 s bohate staci —
     * karta po identifikaci prechazi do TRANSFER v jednotkach ms; kdyz ne, je
     * zaseknuta a dalsi cekani uz nic nezmeni. */
    uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if ((HAL_GetTick() - t0) > 1000u) {
            printf("SD: karta se po identifikaci nedostala do TRANSFER (zaseknuta)\r\n");
            printf("  -> vyjmi a znovu zasun kartu; kdyz to trva, precti ji v PC\r\n");
            hsd1.State = HAL_SD_STATE_READY;
            return MSD_ERROR;
        }
        if (osKernelGetState() == osKernelRunning) osDelay(1);
    }

    /* ⚠️⚠️ KLIC (2026-08-14): aplikuj TRANSFER konfiguraci do CLKCR sami.
     * `HAL_SD_InitCard` nechava CLKCR na INIT hodinach (`ClockDiv=81` @400kHz, HWFC=0);
     * transfer ClockDiv + HWFC se v normalnim `HAL_SD_Init` nastavi az uvnitr
     * `HAL_SD_ConfigWideBusOperation` (na jeho konci `SDMMC_Init(hsd->Init)`), kterou
     * jsme vynechali kvuli SCR zaseknuti. Bez toho zustane HWFC_EN=0 a data nejdou
     * (zmereno: `CLKCR=0x51`). `SDMMC_Init` saha VYHRADNE na CLKCR (jen clock/HWFC/
     * WIDBUS), na stav karty ne -> bezpecne po identifikaci. Tim se zapne HWFC,
     * transfer takt 16 MHz i WIDBUS=1B. */
    (void)SDMMC_Init(SDMMC1, hsd1.Init);

    /* 4-bit sbernice (2026-08-14). ⚠️ NE `HAL_SD_ConfigWideBusOperation` — ta cte
     * pred prepnutim SCR registr karty DATOVYM prenosem s NEohranicenou smyckou
     * (`SDMMC_DATATIMEOUT` ~49 dni) a driv (bez HWFC) o to zasekla desku. Prepneme
     * rucne: CMD55 + ACMD6 (bezdatove prikazy, 5s CMDTIMEOUT) rekne karte 4-bit,
     * pak SDMMC_Init nastavi WIDBUS na hostu. Fallback: pri chybe zustan v 1-bit
     * (karta funguje dal). Viz sd_try_4bit(). */
    if (!sd_try_4bit()) {
        hsd1.Init.BusWide = SDMMC_BUS_WIDE_1B;
        (void)SDMMC_Init(SDMMC1, hsd1.Init);
    }
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    hsd1.Context   = SD_CONTEXT_NONE;
    hsd1.State     = HAL_SD_STATE_READY;
    return MSD_OK;
}
#endif

/* ── Prepsani `BSP_SD_ReadBlocks_DMA` / `BSP_SD_WriteBlocks_DMA` ─────────────
 * Obe jsou v `bsp_driver_sd.c` `__weak`, takze tohle je regen-safe.
 *
 * PROC: `sd_diskio.c` vede kazdy prenos pres tyhle dve funkce a pak ceka na
 * zpravu ve fronte `SDQueueID`, kterou posila obsluha preruseni SDMMC1. Cela ta
 * asynchronni masinerie ale prinasi tri problemy, ktere pro nas nemaji zadnou
 * protihodnotu:
 *   - IDMA nedosahne na DTCM a AXI SRAM je cacheable -> nutna rucni cache
 *     maintenance, kterou ST dela nad NEZAROVNANYMI buffery volajiciho,
 *   - „scratch" cesta, ktera to mela resit, ma v zapisove vetvi dve chyby
 *     (chybejici `Clean` a cekani na `READ_CPLT_MSG`) -> tise selhavajici zapis,
 *   - bez obsluhy preruseni cekala kazda operace `SD_TIMEOUT` = 30 s.
 *
 * Blokujici `HAL_SD_ReadBlocks`/`WriteBlocks` na H7 prehazuji data **procesorem
 * pres FIFO** (`SDMMC_ReadFIFO()` ve smycce) — tedy zadne IDMA, zadny pozadavek
 * na zarovnani, zadna cache maintenance, zadne preruseni. Za to platime tim, ze
 * je CPU po dobu prenosu vytizeny: 512 B pri 4-bit / 16 MHz ~ 64 us, takze
 * export stovek kB stoji desitky ms. Bezi to VYHRADNE z UartTasku, ktery
 * watchdog nehlida, takze je to prijatelna cena za jednoduchost a spolehlivost.
 * (Stejnou cestou uz jede `datalog_sd.c` a overene funguje.)
 *
 * Dokonceni si hlasime sami — `sd_diskio.c` pak najde zpravu uz ve fronte
 * a jeho `osMessageQueueGet` se vrati okamzite. */
#ifdef SD_EXPORT_FATFS
/* ⚠️⚠️ ÚKLID PO NEÚSPĚCHU (převzato z `H757_SDcard_01`, 2026-08-15).
 * `HAL_SD_ReadBlocks` při timeoutu sice vrátí `hsd->State` na READY, ale
 * **datový automat (DPSM) a karta v tom přenosu můžou pokračovat** — karta
 * zůstane ve stavu "sending data" a KAŽDÝ další příkaz pak selže, dokud se
 * nevypne napájení. `HAL_SD_Abort()` pošle CMD12 (STOP_TRANSMISSION) a uklidí
 * periferii, takže jedna neúspěšná operace neshodí SD až do restartu.
 * Bez tohohle byl každý timeout fakticky trvalý. */
static uint8_t sd_xfer_fail(void)
{
    (void)HAL_SD_Abort(&hsd1);
    /* Dej kartě chvíli na návrat do TRANSFER; ohraničeně (200 ms) a s yieldem,
     * ať to nezdrží volajícího víc, než je nutné. */
    uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if ((HAL_GetTick() - t0) > 200u) break;
        if (osKernelGetState() == osKernelRunning) osDelay(1);
    }
    return MSD_ERROR;
}

uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks)
{
    /* Timeout skaluje s poctem bloku; 1 s zakladu bohate staci i pomale karte. */
    uint32_t to = 1000u + NumOfBlocks * 100u;
    if (HAL_SD_ReadBlocks(&hsd1, (uint8_t *)pData, ReadAddr, NumOfBlocks, to) != HAL_OK)
        return sd_xfer_fail();
    BSP_SD_ReadCpltCallback();     /* posle READ_CPLT_MSG do SDQueueID */
    return MSD_OK;
}

uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks)
{
    uint32_t to = 1000u + NumOfBlocks * 100u;
    if (HAL_SD_WriteBlocks(&hsd1, (uint8_t *)pData, WriteAddr, NumOfBlocks, to) != HAL_OK)
        return sd_xfer_fail();
    BSP_SD_WriteCpltCallback();    /* posle WRITE_CPLT_MSG do SDQueueID */
    return MSD_OK;
}
#endif

/* ── Prepsani `BSP_SD_GetCardState()` — POLITE POLLING ───────────────────────
 * `sd_diskio.c` na ni ceka v NEKOLIKA tesnych smyckach bez jakehokoli yieldu:
 *     while (osKernelGetTickCount() - timer < SD_TIMEOUT)
 *         if (BSP_SD_GetCardState() == SD_TRANSFER_OK) break;
 * a `SD_TIMEOUT` je **30 sekund**. Kdyz karta z jakehokoli duvodu nedojede,
 * UartTask (Normal) tim na 30 s uplne vyhladovi UiTask (BelowNormal) —
 * prestane fungovat DOTYK a `watchdog_kick_ui()` zestarne natolik, ze IWDG
 * shodi desku. `SD_TIMEOUT` je bohuzel mimo USER CODE blok, takze ho snizit
 * regen-safe nejde; tahle funkce ale `__weak` je.
 *
 * Reseni: kdyz karta NENI pripravena, ustoupime scheduleru na 1 ms. V typickem
 * (zdravem) pripade se vraci `SD_TRANSFER_OK` hned a zadne zdrzeni nevznikne.
 * Stejny idiom jako `w25q.c wait_ready()` a stejne pravidlo projektu:
 * „zadny spin delsi nez ~10 ms v tasku, ktery neco blokuje". */
#ifdef SD_EXPORT_FATFS
uint8_t BSP_SD_GetCardState(void)
{
    if (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER) return SD_TRANSFER_OK;
    if (osKernelGetState() == osKernelRunning) osDelay(1);   /* nehladov UiTask */
    return SD_TRANSFER_BUSY;
}
#endif

/* ── Ohraniceny probe: 1 blok (512 B) pres DPSM_ENABLE ───────────────────────
 * Rozhodujici experiment (2026-08-14). `HAL_SD_ReadBlocks` startuje data pres
 * CMDTRANS (config.DPSM=DISABLE + __SDMMC_CMDTRANS_ENABLE) a na teto karte
 * timeoutuje bez jedineho bajtu. Naproti tomu SCR cteni (`SD_FindSCR`), ktere
 * na teto karte JEDNOU proslo (GDB 2026-08-12 -> 4-bit), pouziva klasicky
 * DPSM_ENABLE. Tenhle probe cte plny 512B blok STEJNYM mechanismem jako SCR:
 *   - projde-li -> problem je v CMDTRANS ceste (SW-opravitelne: blokovy read si
 *     napiseme sami pres DPSM_ENABLE, jako to dela `SD_FindSCR`),
 *   - selze-li stejne (DATA_TIMEOUT) -> DAT linka neunese 512B prenos (HW).
 * Vendor smycka je nahrazena vlastni s SW timeoutem, takze se nezasekne. */
#ifdef SD_EXPORT_FATFS
static HAL_StatusTypeDef sd_probe_read_dpsm(uint8_t *buf, uint32_t timeout_ms, uint32_t *out_sta)
{
    SDMMC_DataInitTypeDef config;
    uint32_t *p = (uint32_t *)(void *)buf;
    uint32_t dataremaining = 512U;
    uint32_t t0;

    if (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) { *out_sta = SDMMC1->STA; return HAL_ERROR; }

    __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_DATA_FLAGS);
    SDMMC1->DCTRL = 0U;
    __SDMMC_CMDTRANS_DISABLE(SDMMC1);               /* NE CMDTRANS -> klasicky DPSM */

    config.DataTimeOut   = SDMMC_DATATIMEOUT;       /* HW DTIMER velky, hlida nas SW timeout */
    config.DataLength    = 512U;
    config.DataBlockSize = SDMMC_DATABLOCK_SIZE_512B;
    config.TransferDir   = SDMMC_TRANSFER_DIR_TO_SDMMC;
    config.TransferMode  = SDMMC_TRANSFER_MODE_BLOCK;
    config.DPSM          = SDMMC_DPSM_ENABLE;        /* <<< jako SD_FindSCR */
    (void)SDMMC_ConfigData(SDMMC1, &config);

    if (SDMMC_CmdReadSingleBlock(SDMMC1, 0U) != HAL_SD_ERROR_NONE) {
        *out_sta = SDMMC1->STA;
        SDMMC1->DCTRL = 0U;
        __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_DATA_FLAGS);
        return HAL_ERROR;                           /* CMD17 R1 chyba */
    }

    t0 = HAL_GetTick();
    while (!__HAL_SD_GET_FLAG(&hsd1, SDMMC_FLAG_RXOVERR | SDMMC_FLAG_DCRCFAIL |
                              SDMMC_FLAG_DTIMEOUT | SDMMC_FLAG_DATAEND)) {
        if (__HAL_SD_GET_FLAG(&hsd1, SDMMC_FLAG_RXFIFOHF) && dataremaining >= SDMMC_FIFO_SIZE) {
            for (uint32_t i = 0; i < SDMMC_FIFO_SIZE / 4U; i++)
                *p++ = SDMMC_ReadFIFO(SDMMC1);
            dataremaining -= SDMMC_FIFO_SIZE;
        }
        if ((HAL_GetTick() - t0) >= timeout_ms) {
            *out_sta = SDMMC1->STA;
            SDMMC1->DCTRL = 0U;
            __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_DATA_FLAGS);
            return HAL_TIMEOUT;                      /* zadna data v case */
        }
    }
    *out_sta = SDMMC1->STA;
    SDMMC1->DCTRL = 0U;
    HAL_StatusTypeDef r = ((*out_sta & SDMMC_FLAG_DATAEND) && dataremaining == 0U) ? HAL_OK : HAL_ERROR;
    __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_DATA_FLAGS);
    return r;
}
#endif

/* ── `sd init` — SD inicializace PO KROCICH ──────────────────────────────────
 * `sd fs` restartuje desku uvnitr `HAL_SD_Init()`. Crash black-box ukazal
 * PC v `xTaskIncrementTick` (SysTick) pri cteni `pxCurrentTCB->uxPriority`,
 * BFAR = 0xC54D0A40 (mimo osazenou SDRAM) — tedy PREPSANOU strukturu FreeRTOS,
 * ne chybu v samotnem SD kodu. Zasobnik to neni (cely retezec ~880 B, UartTask
 * ma 4 kB), takze je potreba zjistit, KTERY HAL krok to zpusobi.
 *
 * Tenhle prikaz proto nevola `HAL_SD_Init()` jako celek, ale jeho casti zvlast
 * a mezi nimi tiskne znacku. Posledni vypsana znacka = krok, ktery to shodil.
 * ⚠️ BLOKUJE — jen z UartTasku. */
void sd_export_init_steps(void)
{
#ifndef SD_EXPORT_FATFS
    printf("SD INIT: FatFs neni v buildu\r\n");
#else
    printf("SD INIT po krocich (posledni vypsana znacka = kde to spadlo):\r\n");

    printf("  [a] detekce karty...\r\n");
    if (BSP_SD_IsDetected() != SD_PRESENT) {
        printf("      NENI KARTA -> konec\r\n"); return;
    }

    sd_dat_pullup_enable();     /* interni pull-up na DAT0..3 (pojistka pro nepatrny pull-up) */
    sd_apply_init_config();     /* HWFC ENABLE + Init jako funkcni Frantuv projekt */
    printf("  [a2] Init: HWFC=ENABLE, ClockDiv=2, 1-bit (dle funkcniho H757_SDcard_01)\r\n");

    printf("  [b] HAL_SD_InitCard (CMD0/CMD8/ACMD41/CMD2/CMD3, 1-bit)...\r\n");
    HAL_StatusTypeDef r = HAL_SD_InitCard(&hsd1);
    printf("      navrat=%d ErrorCode=0x%08lX\r\n", (int)r, (unsigned long)hsd1.ErrorCode);
    if (r != HAL_OK) return;

    printf("  [c] GetCardCID/CSD...\r\n");
    HAL_SD_CardCIDTypeDef cid; HAL_SD_CardCSDTypeDef csd;
    (void)HAL_SD_GetCardCID(&hsd1, &cid);
    (void)HAL_SD_GetCardCSD(&hsd1, &csd);
    HAL_SD_CardInfoTypeDef ci;
    if (HAL_SD_GetCardInfo(&hsd1, &ci) == HAL_OK) {
        printf("      typ=%lu bloku=%lu x %lu B (~%lu MB)\r\n",
               (unsigned long)ci.CardType, (unsigned long)ci.LogBlockNbr,
               (unsigned long)ci.LogBlockSize,
               (unsigned long)(((uint64_t)ci.LogBlockNbr * ci.LogBlockSize) >> 20));
    }

    /* [c2] KLIC: aplikuj transfer takt + HWFC do CLKCR (HAL_SD_InitCard ho nechal
     * na init hodinach 400kHz/HWFC=0). SDMMC_Init saha jen na CLKCR. */
    printf("  [c2] aplikuji transfer takt + HWFC (SDMMC_Init)...\r\n");
    (void)SDMMC_Init(SDMMC1, hsd1.Init);
    printf("      CLKCR=0x%08lX  (HWFC_EN bit17=%lu, CLKDIV=%lu)\r\n",
           (unsigned long)SDMMC1->CLKCR,
           (unsigned long)((SDMMC1->CLKCR >> 17) & 1u),
           (unsigned long)(SDMMC1->CLKCR & 0x3FFu));

    /* ⚠️ [d] uz NENI `ConfigWideBusOperation` — ta se na teto karte zasekla:
     * SCR cteni je neohranicena datova smycka (~49 dni) -> IWDG (HW 2026-08-14).
     * Misto toho ohraniceny PROBE datove cesty: precti 1 blok (LBA 0) v 1-bit
     * s timeoutem 1 s. `HAL_SD_ReadBlocks` bere timeout od nas, takze se VRATI
     * misto zaseknuti a rekne, jestli datova cesta vubec jede (dosud neovereno —
     * GDB testoval jen prikazy, ne data). Blokujici HAL na H7 tahne data CPU pres
     * FIFO (ne IDMA) -> zadna cache maintenance netreba; buffer je stejne aligned. */
    printf("  [d] datova cesta: ctu 1 blok (LBA 0) v 1-bit, timeout 1 s...\r\n");
    static uint8_t s_probe[512] __attribute__((aligned(32)));
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    r = HAL_SD_ReadBlocks(&hsd1, s_probe, 0, 1, 1000);
    printf("      navrat=%d ErrorCode=0x%08lX -> %s\r\n", (int)r, (unsigned long)hsd1.ErrorCode,
           r == HAL_OK ? "OK - DATOVA CESTA JEDE" :
           (hsd1.ErrorCode & SDMMC_ERROR_DATA_TIMEOUT)  ? "DATA_TIMEOUT - data z karty NEDORAZILA (HW datovych linek / kontakt / napajeni)" :
           (hsd1.ErrorCode & SDMMC_ERROR_DATA_CRC_FAIL) ? "DATA_CRC_FAIL - data dorazila POSKOZENA (integrita linek / rychlost)" :
                                                          "jina chyba (viz ErrorCode)");
    if (r == HAL_OK)
        printf("      prvni bajty: %02X %02X %02X %02X ... [510]=%02X [511]=%02X (55 AA = MBR/boot sektor)\r\n",
               s_probe[0], s_probe[1], s_probe[2], s_probe[3], s_probe[510], s_probe[511]);

    /* [d2] TENTYZ blok pres DPSM_ENABLE (jako SCR, ktere kdysi proslo) — odlisi
     * CMDTRANS cestu (SW) od mrtve DAT linky (HW). Viz `sd_probe_read_dpsm`. */
    printf("  [d2] tyz blok pres DPSM_ENABLE (mechanismus SCR), timeout 1 s...\r\n");
    memset(s_probe, 0, sizeof s_probe);
    uint32_t sta = 0;
    HAL_StatusTypeDef r2 = sd_probe_read_dpsm(s_probe, 1000, &sta);
    printf("      navrat=%d STA=0x%08lX -> %s\r\n", (int)r2, (unsigned long)sta,
           r2 == HAL_OK                          ? "OK - DPSM cesta JEDE (CMDTRANS byl viník -> SW oprava)" :
           (sta & SDMMC_FLAG_DTIMEOUT)           ? "DTIMEOUT - data NEDORAZILA i pres DPSM (HW DAT linky / kontakt)" :
           (sta & SDMMC_FLAG_DCRCFAIL)           ? "DCRCFAIL - data dorazila POSKOZENA (integrita linek)" :
           (sta & SDMMC_FLAG_RXOVERR)            ? "RXOVERR - CPU nestihl FIFO (takt moc rychly)" :
                                                   "SW timeout - zadna data (DAT linka / kontakt)");
    if (r2 == HAL_OK)
        printf("      prvni bajty: %02X %02X %02X %02X ... [510]=%02X [511]=%02X (55 AA = MBR/boot sektor)\r\n",
               s_probe[0], s_probe[1], s_probe[2], s_probe[3], s_probe[510], s_probe[511]);

    printf("  [e] hotovo. CLKCR=0x%08lX POWER=0x%08lX stav=%lu (4=TRANSFER)\r\n",
           (unsigned long)SDMMC1->CLKCR, (unsigned long)SDMMC1->POWER,
           (unsigned long)HAL_SD_GetCardState(&hsd1));
#endif
}

void sd_export_fs(void)
{
#ifndef SD_EXPORT_FATFS
    printf("SD FS: FatFs neni v buildu\r\n");
#else
    printf("SD FS: ctu LBA 0 primo pres BSP_SD (mimo FatFs)\r\n");
    /* ⚠️ Znacky prubehu: `sd fs` shodil desku HardFaultem a bez nich nejde
     * poznat, JESTLI to bylo v init, nebo az ve cteni. Kazda se vypise a
     * dojde na UART DRIV, nez se pokracuje. */
    /* ⚠️⚠️ NESAHAT na registry SDMMC1 pred uspesnym `HAL_SD_Init()`.
     * `MX_SDMMC1_SD_Init()` u nas jen vyplni handle a vrati se, takze
     * `HAL_SD_MspInit()` — a s nim ZAPNUTI HODIN periferie — probehne az uvnitr
     * prvniho `HAL_SD_Init()`. Cteni `SDMMC1->*` driv konci BusFaultem.
     * Puvodne tu jako "optimalizace" bylo `HAL_SD_GetCardState()`, coz je presne
     * ten pripad. Pravidlo prevzato z `SD_franta.md`. */
    printf("  [1] BSP_SD_Init...\r\n");
    uint8_t init_st = BSP_SD_Init();
    printf("  [1] navrat=%u (0=OK, 2=NENI KARTA)\r\n", (unsigned)init_st);
    if (init_st != MSD_OK) {
        printf("  -> karta neni, nebo nenabehla HW vrstva (zkus `sd force on`)\r\n");
        return;
    }
    printf("  [2] cteni LBA 0...\r\n");
    if (BSP_SD_ReadBlocks((uint32_t *)(void *)s_fsbuf, 0, 1, 2000) != MSD_OK) {
        printf("  cteni LBA 0 SELHALO -> HW vrstva nebo karta\r\n");
        return;
    }
    /* ⚠️⚠️ ZDE NESMI BYT `SCB_InvalidateDCache_by_Addr` (odstraneno 2026-08-13).
     * `BSP_SD_ReadBlocks` -> `HAL_SD_ReadBlocks` je na H7 **CPU cteni z FIFO**
     * (`SDMMC_ReadFIFO` v smycce), NE IDMA — data tedy pise procesor a lezi
     * v D-cache jako DIRTY. Invalidace bez clean je zahodi a z bufferu se pak
     * precte to, co bylo v RAM = same nuly. Presne to hlasilo
     * "LBA 0 nema podpis 0x55AA (0000) -> karta neni naformatovana", zatimco
     * `f_mount` (jde pres `BSP_SD_ReadBlocks_DMA`, tam je cache maintenance
     * na miste) tutez kartu namountoval bez problemu.
     * Pravidlo: cache maintenance patri jen k `_DMA`/`_IT` variantam. */

    if (s_fsbuf[510] != 0x55u || s_fsbuf[511] != 0xAAu) {
        printf("  LBA 0 nema podpis 0x55AA (%02X%02X) -> karta neni naformatovana\r\n",
               s_fsbuf[510], s_fsbuf[511]);
        return;
    }
    /* MBR vs. rovnou boot sektor: VBR zacina skokovou instrukci EB/E9. */
    if (s_fsbuf[0] == 0xEBu || s_fsbuf[0] == 0xE9u) {
        printf("  LBA 0 je rovnou boot sektor (bez particni tabulky)\r\n");
        fs_show_vbr(s_fsbuf, "format");
        return;
    }
    printf("  LBA 0 je MBR, particni tabulka:\r\n");
    uint32_t first_lba = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = s_fsbuf + 0x1BE + 16 * i;
        uint8_t  type = e[4];
        uint32_t lba  = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                        ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t cnt  = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                        ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
        if (type == 0u) continue;
        const char *nm = (type == 0x0Bu || type == 0x0Cu) ? "FAT32"
                       : (type == 0x07u)                  ? "exFAT/NTFS"
                       : (type == 0x06u || type == 0x0Eu) ? "FAT16" : "?";
        printf("   #%d typ=0x%02X (%s) LBA=%lu velikost=%lu MB\r\n",
               i, type, nm, (unsigned long)lba, (unsigned long)(cnt / 2048u));
        if (first_lba == 0u) first_lba = lba;
    }
    if (first_lba == 0u) { printf("  zadna particni polozka -> nenaformatovano\r\n"); return; }

    if (BSP_SD_ReadBlocks((uint32_t *)(void *)s_fsbuf, first_lba, 1, 2000) != MSD_OK) {
        printf("  cteni boot sektoru particie SELHALO\r\n"); return;
    }
    /* ⚠️ Zadna invalidace — viz komentar u cteni LBA 0 vyse. */
    fs_show_vbr(s_fsbuf, "particie 1");
#endif
}

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
        /* ⚠️ SKUTECNA sirka sbernice a takt z CLKCR, ne z toho, co jsme CHTELI
         * nastavit: `BSP_SD_Init` prepina na 4-bit s fallbackem na 1-bit, takze
         * bez tohohle vypisu nejde poznat, ze fallback nastal (a proc je prenos
         * pomalejsi). Prevzato z pristupu `H757_SDcard_01`, ktery sirku hlasi taky.
         * SDMMC_CK = kernel_clk / (2 x CLKDIV); kernel je 64 MHz z PLL1Q. */
        {
            uint32_t clkcr  = SDMMC1->CLKCR;
            uint32_t div    = clkcr & 0x3FFu;
            uint32_t widbus = (clkcr >> 14) & 3u;          /* 0=1-bit, 1=4-bit, 2=8-bit */
            uint32_t khz    = div ? (64000u / (2u * div)) : 64000u;
            printf("  sbernice   : %s, SDMMC_CK %lu.%03lu MHz%s\n",
                   (widbus == 1u) ? "4-bit" : (widbus == 2u) ? "8-bit" : "1-bit",
                   (unsigned long)(khz / 1000u), (unsigned long)(khz % 1000u),
                   (widbus == 0u) ? "   <-- FALLBACK na 1-bit (4-bit se nepodaril)" : "");
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

#define SD_SPEED_FILE  "SDSPEED.BIN"
#define SD_SPEED_BUF   32768u          /* 32 KB blok — doporucena velikost (amortizuje rezii
                                        * prikazu/FAT, karta programuje sekvencne; nad 64 KB
                                        * uz zisk plochne). Blokujici FIFO -> na umisteni bufferu
                                        * nezalezi; 32 KB staticky v .bss je OK. */
#define SD_SPEED_KB    1024u           /* 1 MB testovaci soubor (stabilni mereni pri 4-bit) */

/* ── Test rychlosti zapisu/cteni ─────────────────────────────────────────────
 * Zapise a zpetne precte SD_SPEED_KB velky soubor po SD_SPEED_BUF blocich,
 * zmeri cas (`HAL_GetTick`, ms) a spocita KB/s. Best-effort — vola se az po
 * uspesnem verify testu, takze samotny prenos uz je proveren. Rychlosti do
 * *out_w_kbs / *out_r_kbs (0 = nezmereno). @return false jen pri chybe I/O. */
#ifdef SD_EXPORT_FATFS
static bool sd_speed_test(uint32_t *out_w_kbs, uint32_t *out_r_kbs)
{
    static uint8_t sbuf[SD_SPEED_BUF];   /* static: 32 KB na stack UartTasku nepatri */
    /* ⚠️ `FIL` je STATICKY, ne na stacku: pri `_FS_TINY=0` v sobe nese vlastni
     * 512B sektorovy buffer (~560 B). Na stacku delal ze `selftest_body` 1176 B
     * a z `export_body` 920 B, takze po SD operacich klesla rezerva UartTasku
     * na ~660 B (zmereno `status` 2026-08-15). Vsechny tyhle funkce bezi
     * VYHRADNE z UartTasku a nejsou vnorene, takze staticka instance je
     * bezpecna; kazda ma vlastni, aby se nemohly potkat (`sd_speed_test` se
     * vola z konce `selftest_body`). RAM_D1 ma ~386 KB volnych. */
    static FIL f; UINT bw, br; FRESULT fr;
    const uint32_t iters = (SD_SPEED_KB * 1024u) / SD_SPEED_BUF;
    uint32_t t0, dt;

    for (uint32_t i = 0; i < SD_SPEED_BUF; i++) sbuf[i] = (uint8_t)(i * 37u + 0x5Au);

    /* --- zapis --- */
    fr = f_open(&f, SD_SPEED_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) { printf("SD SPEED: f_open(w)=%d %s\n", (int)fr, fr_str(fr)); return false; }
    /* PREDALOKACE: souvisly blok dopredu -> zapis pak nesaha do FAT (zadne pomale
     * jednoblokove updaty pri rustu souboru). Best-effort: kdyz neni souvisle misto,
     * jede se normalne (jen pomaleji). */
    if (f_expand(&f, (FSIZE_t)SD_SPEED_KB * 1024u, 1) != FR_OK)
        printf("SD SPEED: f_expand se nepodaril (fragmentace?) -> zapis bez predalokace\n");
    t0 = HAL_GetTick();
    for (uint32_t i = 0; i < iters; i++) {
        if (f_write(&f, sbuf, SD_SPEED_BUF, &bw) != FR_OK || bw != SD_SPEED_BUF) {
            printf("SD SPEED: f_write %lu selhal\n", (unsigned long)i);
            f_close(&f); f_unlink(SD_SPEED_FILE); return false;
        }
    }
    fr = f_sync(&f);                     /* flush pred zmerenim casu */
    dt = HAL_GetTick() - t0; if (dt == 0u) dt = 1u;
    *out_w_kbs = (SD_SPEED_KB * 1000u) / dt;
    if (fr != FR_OK) { printf("SD SPEED: f_sync=%d %s\n", (int)fr, fr_str(fr));
                       f_close(&f); f_unlink(SD_SPEED_FILE); return false; }
    f_close(&f);

    /* --- cteni --- */
    fr = f_open(&f, SD_SPEED_FILE, FA_READ);
    if (fr != FR_OK) { printf("SD SPEED: f_open(r)=%d %s\n", (int)fr, fr_str(fr));
                       f_unlink(SD_SPEED_FILE); return false; }
    t0 = HAL_GetTick();
    for (uint32_t i = 0; i < iters; i++) {
        if (f_read(&f, sbuf, SD_SPEED_BUF, &br) != FR_OK || br != SD_SPEED_BUF) {
            printf("SD SPEED: f_read %lu selhal\n", (unsigned long)i);
            f_close(&f); f_unlink(SD_SPEED_FILE); return false;
        }
    }
    dt = HAL_GetTick() - t0; if (dt == 0u) dt = 1u;
    *out_r_kbs = (SD_SPEED_KB * 1000u) / dt;
    f_close(&f);
    f_unlink(SD_SPEED_FILE);

    printf("SD SPEED: zapis %lu KB/s (%lu.%02lu MB/s), cteni %lu KB/s (%lu.%02lu MB/s), soubor %lu KB\n",
           (unsigned long)*out_w_kbs, (unsigned long)(*out_w_kbs / 1024u),
           (unsigned long)((*out_w_kbs % 1024u) * 100u / 1024u),
           (unsigned long)*out_r_kbs, (unsigned long)(*out_r_kbs / 1024u),
           (unsigned long)((*out_r_kbs % 1024u) * 100u / 1024u),
           (unsigned long)SD_SPEED_KB);
    return true;
}
#endif /* SD_EXPORT_FATFS */

/* ⚠️ Telo je vyclenene ZAMERNE. `s_busy` se nastavuje a rusi jen ve wrapperu
 * nize, takze ho NELZE zapomenout uvolnit na nektere z ~8 chybovych cest.
 * Prvni verze tuhle chybu presne udelala (audit 2026-08-12): priznak zustal
 * viset a natrvalo vypnul auto-unmount. Stejny vzor pouziva i `sd_export_run`. */
#ifdef SD_EXPORT_FATFS
static bool selftest_body(void)
{
    static uint8_t buf[SD_TEST_CHUNK];   /* static: 512 B na stack UartTasku je zbytecne */
    /* ⚠️ `FIL` je STATICKY, ne na stacku: pri `_FS_TINY=0` v sobe nese vlastni
     * 512B sektorovy buffer (~560 B). Na stacku delal ze `selftest_body` 1176 B
     * a z `export_body` 920 B, takze po SD operacich klesla rezerva UartTasku
     * na ~660 B (zmereno `status` 2026-08-15). Vsechny tyhle funkce bezi
     * VYHRADNE z UartTasku a nejsou vnorene, takze staticka instance je
     * bezpecna; kazda ma vlastni, aby se nemohly potkat (`sd_speed_test` se
     * vola z konce `selftest_body`). RAM_D1 ma ~386 KB volnych. */
    static FIL f; UINT bw, br; FRESULT fr;

    /* --- zapis --- */
    fr = f_open(&f, SD_TEST_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        printf("SD TEST: f_open(w) = %d %s\n", (int)fr, fr_str(fr));
        /* U FR_DISK_ERR rekni, co hlasi spodni vrstva — jinak se hada, jestli je
         * problem v karte, v prenosu, nebo v souborovem systemu. */
        if (fr == FR_DISK_ERR)
            printf("  HAL: ErrorCode=0x%08lX stav=%lu (4=TRANSFER) STA=0x%08lX\n",
                   (unsigned long)hsd1.ErrorCode, (unsigned long)HAL_SD_GetCardState(&hsd1),
                   (unsigned long)SDMMC1->STA);
        return false;
    }
    for (uint32_t c = 0; c < SD_TEST_CHUNKS; c++) {
        for (uint32_t i = 0; i < SD_TEST_CHUNK; i++)
            buf[i] = (uint8_t)(c * 7u + i * 31u + 0x5Au);   /* deterministicky vzor */
        if (f_write(&f, buf, SD_TEST_CHUNK, &bw) != FR_OK || bw != SD_TEST_CHUNK) {
            printf("SD TEST: f_write blok %lu selhal (zapsano %u)\n", (unsigned long)c, (unsigned)bw);
            f_close(&f); return false;
        }
    }
    /* ⚠️ Navratovou hodnotu `f_close` KONTROLOVAT. Prave tady se zapisuje
     * adresarova polozka — kdyz to selze, `f_write` uz hlasilo OK a chyba se
     * projevi az o kus dal jako zavadejici FR_NO_FILE pri otevreni pro cteni
     * (presne tak vypadalo selhani rozbite scratch cesty v `sd_diskio.c`). */
    fr = f_close(&f);
    if (fr != FR_OK) {
        printf("SD TEST: f_close(w) = %d %s -> adresarova polozka se nezapsala\n",
               (int)fr, fr_str(fr));
        return false;
    }

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

    /* Po overeni integrity jeste zmer rychlost (best-effort — verdikt testu
     * urcuje verify vyse, ne rychlost). Vysledek do UI snapshotu. */
    s_ui.test_w_kbs = 0; s_ui.test_r_kbs = 0;
    (void)sd_speed_test(&s_ui.test_w_kbs, &s_ui.test_r_kbs);
    return true;
}
#endif /* SD_EXPORT_FATFS */

bool sd_export_selftest(void)
{
#ifndef SD_EXPORT_FATFS
    printf("SD TEST: FatFs neni v buildu\n");
    return false;
#else
    /* ⚠️ `s_busy` se nastavuje PRED mountem, ne po nem. `sd_export_mount()` uvnitr
     * nastavi `s_mounted = true` a kdyby priznak jeste neplatil, mohl by tik v
     * defaultTasku v tom okamziku videt `s_mounted && !s_busy` a svazek pod nami
     * odmountovat. Okno je male, ale zadarmo se zavre timhle poradim. */
    s_busy = true;                 /* drzi auto-unmount v `sd_export_tick()` po dobu testu */
    bool mounted = sd_export_mount();
    bool ok      = mounted ? selftest_body() : false;
    s_busy = false;                /* jedina cesta ven -> nelze zapomenout */

    if (!mounted) printf("SD TEST: mount selhal (%s)\n", sd_export_state_str());
    return ok;
#endif
}

#ifdef SD_EXPORT_FATFS
/* Telo exportu — `s_busy` resi wrapper, viz komentar u `selftest_body()`. */
/* Najde prvni volny `GPSDOnnn.CSV`. @return 1 = nalezeno (jmeno v `out`). */
static int export_next_name(char *out, size_t n)
{
    for (uint32_t i = 1; i <= SD_EXPORT_MAX_IDX; i++) {
        FILINFO fno;
        snprintf(out, n, "GPSDO%03lu.CSV", (unsigned long)i);
        if (f_stat(out, &fno) == FR_NO_FILE) return 1;
    }
    return 0;   /* 999 exportu na karte — at si uzivatel uklidi */
}

static int32_t export_body(uint32_t max_rec)
{
    datalog_status_t st;
    datalog_get_status(&st);
    uint32_t total = st.records;
    if (total == 0u) return 0;
    if (max_rec != 0u && max_rec < total) total = max_rec;

    char fname[16];
    if (!export_next_name(fname, sizeof fname)) {
        printf("SD: na karte uz je %lu exportu, uvolni misto\r\n",
               (unsigned long)SD_EXPORT_MAX_IDX);
        s_state = SD_EXP_ERROR;
        return -1;
    }
    static FIL f;   /* ⚠️ staticky, ne na stacku — viz duvod u `selftest_body` */
    /* `FA_CREATE_NEW` (ne ALWAYS): kdyby se mezi `f_stat` a `f_open` soubor
     * objevil, radeji selzeme, nez abychom neco prepsali. */
    if (f_open(&f, fname, FA_CREATE_NEW | FA_WRITE) != FR_OK) {
        s_state = SD_EXP_ERROR;
        return -1;
    }
    printf("SD: zapisuji %s\r\n", fname);

    char line[160];
    UINT bw;
    int n = snprintf(line, sizeof line,
        "seq" SD_CSV_SEP "t_unix" SD_CSV_SEP "freq_hz" SD_CSV_SEP "t_ocxo_C" SD_CSV_SEP
        "t_board_C" SD_CSV_SEP "ocxo_vc_mV" SD_CSV_SEP "rf_dBm" SD_CSV_SEP
        "flags" SD_CSV_SEP "sats" SD_CSV_SEP "hdop10" SD_CSV_SEP "vbat_mV\r\n");
    f_write(&f, line, (UINT)n, &bw);

    /* Chronologicky (nejstarší první) — `datalog_read_back(0)` je NEJNOVĚJŠÍ,
     * takže jdeme od konce. Pro log v souboru je vzestupný čas přirozenější. */
    int32_t written = 0;
    /* Prubeh pro UI: UiTask ho cte 2x/s ze snapshotu. Zapis dvou uint32 je levny,
     * takze se aktualizuje kazdy zaznam bez throttlingu. */
    s_ui.prog_done = 0; s_ui.prog_total = total;
    for (uint32_t i = total; i-- > 0; ) {
        s_ui.prog_done = total - i;
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
    /* ⚠️ `s_busy` PRED mountem — viz duvod u `sd_export_selftest()`. */
    s_busy = true;                 /* drzi auto-unmount po dobu zapisu */
    int32_t r = sd_export_mount() ? export_body(max_rec) : -1;
    s_busy = false;                /* jedina cesta ven -> nelze zapomenout */
    s_ui.prog_total = 0;           /* 0 = zadny export nebezi (UI schova radek prubehu) */
    return r;
#endif
}
