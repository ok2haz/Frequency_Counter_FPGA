#ifndef INC_ERRLOG_H_
#define INC_ERRLOG_H_

/* ── Trvaly zaznamnik chyb (append-only, W25Q region ERRLOG) ─────────────────
 *
 * PROC: crash black-box v BKP drzi **jediny** zaznam a `rtc.c` ho po prectení
 * MAZE, takze `status` rekne „proc jsem se restartoval naposledy" a nic vic.
 * Historie chyb neexistovala — nesla zodpovedet otazka „stava se to casteji?"
 * ani „co se delo pred tydnem". Tenhle modul ji doplnuje.
 *
 * 🔴 DVOUSTUPNOVY ZAPIS — a je to nutnost, ne pohodli:
 *   1) `errlog_put()` smi bezet i z ISR / z `Error_Handler` / z `configASSERT`,
 *      takze zapisuje JEN do maleho RAM ringu (kratka kriticka sekce).
 *      NIKDY nesaha na QSPI: `w25q wait_ready()` ustupuje scheduleru, coz
 *      v exception kontextu zatuhne (stejny duvod, proc `flightrec` nezapisuje
 *      z HardFault handleru).
 *   2) `errlog_tick()` (defaultTask, 1 Hz) ring vylije do flash pod
 *      `qspiMutexHandle` s KRATKYM timeoutem — kdyz je flash zrovna obsazena,
 *      zaznamy pockaji v ringu do dalsiho tiku.
 *
 * 🔑 FATALNI CHYBY jdou pres BKP, ne pres ring: HardFault/stack/malloc/assert
 * skonci resetem drive, nez by stage 2 stihla bezet. Proto je pri startu
 * `errlog_boot_record()` — precte, co `rtc.c` dekodoval z BKP, a teprve TED
 * to zapise do flash (v defaultTasku, kde je na to klid).
 *
 * ⚠️ RATE-LIMIT JE POVINNY. Mrtva I2C4 vyrabi chybu 2x za sekundu donekonecna;
 * bez omezeni by zaplnila region za hodinu a prepsala vse zajimave. Kazdy druh
 * ma proto prodlevu (`ERRLOG_COOLDOWN_MS`) a opakovani se scitaji do pole
 * `repeat` jedineho zaznamu.
 */

#include <stdint.h>
#include <stdbool.h>

/* Druh udalosti. ⚠️ Cisla jsou v zapsanych datech — NEPRECISLOVAT, jen pridavat. */
typedef enum {
    ERRLOG_K_BOOT    = 1u,   /* start pristroje; a = duvod resetu (RSR), sub = krok bring-upu */
    ERRLOG_K_CRASH   = 2u,   /* z crash black-boxu; sub = kind (1 stack/2 malloc/3 stall/4 HF/5 hal/6 assert) */
    ERRLOG_K_I2C     = 3u,   /* sbernice mrtva / recovery; sub = cislo sbernice (1 nebo 4) */
    ERRLOG_K_UART    = 4u,   /* chyby na GPS lince; a = ORE, b = FE|NE<<8|PE<<16 */
    ERRLOG_K_SENSOR  = 5u,   /* senzor prestal odpovidat; sub = sensor_id_t */
    ERRLOG_K_FPGA    = 6u,   /* CRC chyba / ztrata linku */
    ERRLOG_K_REF     = 7u,   /* Si5356 LOS_CLKIN / PLL_LOL */
    ERRLOG_K_STORAGE = 8u,   /* chyba zapisu SD / W25Q */
    ERRLOG_K_GPIO    = 9u,   /* hlidac opravil pin (zavod jader o GPIOG) */
    ERRLOG_K_NET     = 10u,  /* ETH / CM4 */
    /* 🔑 Zmena nastaveni, ktera meni SOUMERITELNOST mereni (brana, kanal,
     * perioda/uloziste logu, ulozena kalibrace). Bez toho vypada skok ve
     * statistice jako HW udalost, ackoli to byl zasah uzivatele. Zvlast u brany:
     * datalog `gate_time_ns` neuklada (nema volny bajt), takze starsi zaznamy
     * po zmene uz nejsou soumeritelne a NIC jineho to nepripomene.
     * `sub` = ERRLOG_CFG_*, `a` = nova hodnota, `b` = stara. */
    ERRLOG_K_CFG     = 11u,
} errlog_kind_t;

/* Podtypy pro ERRLOG_K_CFG (pole `sub`). */
#define ERRLOG_CFG_GATE     1u
#define ERRLOG_CFG_CHAN     2u
#define ERRLOG_CFG_LOGPER   3u
#define ERRLOG_CFG_LOGSTORE 4u
#define ERRLOG_CFG_CALIB    5u

#define ERRLOG_TAG_LEN   6u
#define ERRLOG_REC_SIZE  32u

typedef struct {
    uint32_t seq;                    /* monotonni; 0xFFFFFFFF = volny slot */
    uint32_t t_unix;                 /* UTC z RTC (0 = nesynchronizovano) */
    uint32_t uptime_s;
    uint32_t a;                      /* vyznam dle `kind` */
    uint32_t b;
    uint16_t repeat;                 /* kolikrat se to od minuleho zapisu opakovalo */
    uint8_t  kind;
    uint8_t  sub;
    char     tag[ERRLOG_TAG_LEN];    /* kratky popisek, nemusi byt zakoncen NUL */
} errlog_rec_t;

/* Najde hlavu ve flash. Vola se JEDNOU z defaultTask pred hlavni smyckou. */
void errlog_init(void);

/* Zapise do flash zaznam o startu + pripadny crash z BKP. Vola se hned po
 * `errlog_init()`. ⚠️ Musi byt AZ po `MX_RTC_Init` (dekoduje black-box). */
void errlog_boot_record(void);

/* ISR-SAFE. Ulozi udalost do RAM ringu; do flash ji dostane `errlog_tick`.
 * `tag` smi byt NULL. Vraci false, kdyz ji rate-limit spolkl (nebo je ring plny). */
bool errlog_put(uint8_t kind, uint8_t sub, uint32_t a, uint32_t b, const char *tag);

/* Vylije RAM ring do flash. Vola VYHRADNE defaultTask (~1 Hz), pod QSPI mutexem. */
void errlog_tick(void);

/* Cteni od NEJNOVEJSIHO (idx 0 = posledni zapsany). Vraci false na konci. */
bool errlog_read_back(uint32_t idx_from_newest, errlog_rec_t *out);

uint32_t errlog_count(void);          /* kolik zaznamu je v logu */
uint32_t errlog_dropped(void);        /* kolik jich ring zahodil (byl plny) */
void     errlog_erase(void);          /* ⚠️ destruktivni; jen z UartTasku */
const char *errlog_kind_name(uint8_t kind);

#endif /* INC_ERRLOG_H_ */
