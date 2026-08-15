/**
 * @file    datalog.h
 * @brief   Append-only zaznam stability mereni (STATUS.md TODO #6).
 *
 * Perioda DATALOG_PERIOD_S (10 s), zaznam DATALOG_REC_SIZE (32 B) -> ~270 kB/den
 * (8640 zaznamu/den). Datalog vyuziva ZAMERNE jen ~1/3 W25Q DATA regionu
 * (`W25Q_DATALOG_SIZE` ~22,3 MB = 696 960 zaznamu) => **~80 dni** kruhoveho logu;
 * po naplneni se prepisuje NEJSTARSI zaznam (log nikdy "nedojde"). Zbyle 2/3 DATA
 * regionu zustavaji volne pro dalsi bulk pouziti. (Plny region by byl ~242 dni —
 * driv tu chybne stalo „~600".) Ucel: rekonstrukce Allanovy deviace / driftu /
 * holdoveru po bootu a dlouhodoba analyza mimo pristroj.
 *
 * ── ULOZISTE JE ZA ABSTRAKCI (`datalog_backend_t`) ─────────────────────────
 * Backend je jen "blokove zarizeni" (read/write/erase + kapacita). Implementovany
 * je W25Q (QSPI NOR, `datalog_backend_w25q`); pripraveny, ale zatim NEOSAZENY je
 * SD karta (`datalog_backend_sd` v datalog_sd.c — probe() vraci false, dokud
 * nebude SDMMC v .ioc, viz tam TODO). `datalog_init` zkusi SD a pri neuspechu
 * spadne na W25Q -> az bude SD osazena, log se na ni prepne BEZ zmeny volajicich.
 * Rozdil NOR vs SD je jen `erase_size` (NOR musi pred zapisem mazat sektor,
 * SD ne -> erase_size = 0 a `erase` smi byt NULL).
 *
 * ⚠️ Vlakno: `datalog_tick()` vola VYHRADNE defaultTask (stejne jako
 * syscfg_flash_tick). Sbernici sdili pres `qspiMutexHandle` s KRATKYM timeoutem —
 * pri obsazene flash zapis proste vynecha (defaultTask krmi watchdog, nesmi cekat).
 */
#ifndef INC_DATALOG_H_
#define INC_DATALOG_H_

#include <stdint.h>
#include <stdbool.h>

#define DATALOG_REC_SIZE   32u    /* bajtu na zaznam (pevne, viz datalog_rec_t) */
#define DATALOG_PERIOD_S   10u    /* perioda vzorkovani [s] */
#define DATALOG_SEQ_EMPTY  0xFFFFFFFFu   /* smazana flash (0xFF) = volny slot */

/* Jeden zaznam. Serializuje se RUCNE (little-endian, viz pack_rec/unpack_rec
 * v datalog.c) — NE memcpy struktury: na flash musi byt layout nezavisly na
 * kompilatoru/paddingu, aby se log dal cist i jinym nastrojem. */
typedef struct {
    uint32_t seq;              /* monotonni poradove cislo (od 1) */
    uint32_t t_unix;           /* UTC z RTC [s od 1970]; 0 = RTC nesynchronizovano */
    uint64_t freq_x100000;     /* kmitocet x 1e5 (zvoleny zdroj /4 nebo /16) */
    int16_t  t_ocxo_c100;      /* teplota OCXO [0,01 C]; DATALOG_INVALID16 = neplatne */
    int16_t  t_board_c100;     /* teplota STM desky [0,01 C] */
    int16_t  ocxo_vc_mv;       /* ladici napeti OCXO [mV] (ADS AIN0) */
    int16_t  rf_dbm10;         /* uroven RF [0,1 dBm] (AD8307 pres ADS AIN1) */
    uint8_t  flags;            /* viz DATALOG_F_* */
    uint8_t  sats;             /* pocet pouzitych druzic (GGA) */
    uint8_t  hdop10;           /* HDOP x10; 255 = n/a */
} datalog_rec_t;

#define DATALOG_INVALID16   ((int16_t)0x8000)   /* sentinel neplatne hodnoty */

#define DATALOG_F_GPS_VALID   (1u << 0)   /* RMC status 'A' */
#define DATALOG_F_FIX_3D      (1u << 1)   /* GSA fix_mode >= 3 */
#define DATALOG_F_FPGA_LINK   (1u << 2)   /* SPI link ziva */
#define DATALOG_F_SIGNAL_LOST (1u << 3)   /* FPGA SIGNAL_LOST / mrtvy link */
#define DATALOG_F_DIV16       (1u << 4)   /* mereno pres odbocku /16 (jinak /4) */
#define DATALOG_F_HOLDOVER    (1u << 5)   /* fix ztracen po tom, co uz jednou byl */

/* ── Backend uloziste (W25Q / SD / ...) ────────────────────────────────────── */
typedef struct {
    const char *name;                                        /* "W25Q" / "SD" */
    bool     (*probe)(void);                                 /* true = zarizeni je k dispozici */
    bool     (*read)(uint32_t off, uint8_t *buf, uint32_t len);
    bool     (*write)(uint32_t off, const uint8_t *buf, uint32_t len);
    bool     (*erase)(uint32_t off);   /* smaze blok obsahujici off; NULL kdyz erase_size==0 */
    uint32_t erase_size;               /* velikost erase bloku [B]; 0 = bez erase (SD) */
    uint32_t capacity;                 /* pouzitelna kapacita logu [B] */
} datalog_backend_t;

extern const datalog_backend_t datalog_backend_w25q;
/* ⚠️ NENI `const` (na rozdil od W25Q): `capacity` se da zjistit az z vlozene karty,
 * takze ji vyplni `probe()` po `HAL_SD_GetCardInfo`. Konzumenti drzi ukazatel na
 * `const datalog_backend_t`, takze se pro ne nic nemeni. */
extern datalog_backend_t datalog_backend_sd;   /* probe()==false dokud neni SDMMC1, viz datalog_sd.c */

/** Je v slotu vlozena SD karta? (card-detect PE3, debounced.)
 *  Nezavisi na tom, jestli je SD backend aktivni — je to ciste GPIO, takze UI
 *  muze hlasit pritomnost karty i pri `DATALOG_SD_RAW_OK == 0`.
 *  ⚠️ Debounce je "N shodnych cteni po sobe" -> casovou konstantu urcuje kadence
 *  volajiciho. Volat pravidelne (napr. 2 Hz z defaultTasku / pri prekresleni okna). */
bool datalog_sd_card_present(void);

/** Syrova uroven card-detect pinu: 0 = LOW (= karta vlozena dle zapojeni J13),
 *  1 = HIGH (prazdny slot). Pro diagnostiku z konzole — UART `sd det`. */
int  datalog_sd_det_raw(void);

/** ⚠️ Override detekce. Card-detect je jen pomucka; jestli karta opravdu je,
 *  rozhodne az `HAL_SD_Init`. Kdyz spinac v socketu nefunguje, `sd force on`
 *  detekci obejde a SD cesta se da presto vyzkouset.
 *  `datalog_sd_det_forced()` vraci int (ne bool) — vola se i z generovaneho
 *  `fatfs_platform.c`, kde neni kam pridat include. */
void datalog_sd_det_force(bool on);
int  datalog_sd_det_forced(void);

/** ⚠️ Prohozeni polarity detekce za behu (`sd det invert on`), bez reflashe.
 *  Vychozi: LOW = karta vlozena. Nektere sokety maji spinac obracene. */
void datalog_sd_det_invert(bool on);
int  datalog_sd_det_inverted(void);

/** Vyhodnoceni detekce BEZ debounce: 1 = karta pritomna. Zohlednuje force
 *  i invert. Vola to i generovany `fatfs_platform.c`, aby FatFs videl totez. */
int  datalog_sd_detect_status(void);

/* Pure-logic test 512B RMW layeru SD backendu (proti RAM fake bloku, bez HW).
 * Volá ho datalog_selftest → součást UART "selftest". @return true = OK. */
bool datalog_sd_selftest(void);

/* ── Stav pro UI/UART ──────────────────────────────────────────────────────── */
typedef struct {
    const char *backend;      /* jmeno aktivniho backendu; "--" = zadny */
    bool     ready;           /* init probehl a uloziste odpovida */
    bool     enabled;         /* logovani zapnuto */
    uint32_t records;         /* pocet ulozenych zaznamu (po wrapu = kapacita) */
    uint32_t capacity_rec;    /* kolik zaznamu se vejde nez se zacne prepisovat */
    uint32_t last_seq;        /* seq posledniho zapsaneho zaznamu */
    uint32_t write_errors;    /* pocet neuspesnych zapisu (flash chyba / mutex) */
    bool     wrapped;         /* true = kruh uz obehl (prepisuje se nejstarsi) */
} datalog_status_t;

/** Najde backend (SD -> fallback W25Q), obnovi pozici zapisu z uloziste.
 *  Vola se JEDNOU po startu scheduleru z defaultTask (po syscfg_load). */
void datalog_init(void);

/** Periodicky tick z defaultTask (~100 Hz). Uvnitr throttle na DATALOG_PERIOD_S;
 *  pri vypnutem logovani / nepripravenem ulozisti okamzity return. */
void datalog_tick(void);

/** Zapne/vypne logovani (okno Datalog, UART). Persistuje se v syscfg (W25Q CONFIG). */
void datalog_set_enabled(bool en);
bool datalog_enabled(void);

/** Aktualni stav pro zobrazeni. */
void datalog_get_status(datalog_status_t *out);

/** Precte N-ty zaznam od NEJNOVEJSIHO (0 = posledni zapsany). false = neni.
 *  Urceno pro export/analyzu; cte pod QSPI mutexem, volatelne z UI/UART. */
bool datalog_read_back(uint32_t from_newest, datalog_rec_t *out);

/** Jednoradkovy stav: "DATALOG W25Q ON 1234/2043136 rec seq:1234 err:0". */
void datalog_format_status(char *buf, int buflen);

/** ⚠️ DESTRUKTIVNI: smaze cely log (erase vsech bloku) a zacne od nuly.
 *  Blokujici (radove sekundy pro velky region) -> volat jen z UART/UI akce. */
bool datalog_erase_all(void);

/** Pure-logic selftest (serializace zaznamu + prevod data na unix cas).
 *  Bez HW a bez sdileneho stavu -> soucast UART "selftest". */
bool datalog_selftest(void);

#endif /* INC_DATALOG_H_ */
