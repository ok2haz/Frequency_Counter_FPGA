/**
 * @file    calib.c
 * @brief   Viz calib.h.
 */
#include "calib.h"
#include "w25q.h"
#include "w25q_store.h"
#include "w25q_map.h"
#include "freertos_shared.h"   /* qspiMutexHandle — W25Q sdili vic tasku */
#include "cmsis_os2.h"         /* osMutexAcquire/Release */
#include "errlog.h"   /* udalost: ulozena kalibrace */

/* Timeouty QSPI mutexu: boot (calib_load) i ULOZIT (calib_save) bezi v UiTask
 * na explicitni akci uzivatele, takze si muzou pockat i na bezici erase (~400 ms). */
#define CALIB_LOCK_MS 1000u

/* Vychozi (datasheet) hodnoty - stejne cisla jako drivejsi #define konstanty
 * v app_gpsdo.c / freertos_task_sensors.c, ted jen jednou zde. */
#define CALIB_DEFAULT_AD8307_SLOPE      25.0f
#define CALIB_DEFAULT_AD8307_INTERCEPT  (-84.0f)
#define CALIB_DEFAULT_GAIN_12V          (13417.0f / 2814.0f)   /* ~4.768 */
#define CALIB_DEFAULT_GAIN_5V           (4978.0f  / 2526.0f)   /* ~1.971 */

volatile calib_t g_calib = {
    CALIB_DEFAULT_AD8307_SLOPE, CALIB_DEFAULT_AD8307_INTERCEPT,
    CALIB_DEFAULT_GAIN_12V, CALIB_DEFAULT_GAIN_5V,
};

/* Ulozeny format (store payload) - VERZOVANY magicem, nezavisly na sizeof(calib_t)
 * (budouci pole se pridaji na konec, magic se zmeni pri nekompatibilni zmene
 * layoutu). Store sam uz overuje CRC16 payloadu -> magic tu jen potvrzuje, ze
 * blob patri kalibraci (region by teoreticky mohl driv drzet neco jineho). */
#define CALIB_BLOB_MAGIC 0x43414C31u   /* "CAL1" */
typedef struct {
    uint32_t magic;
    float    ad8307_slope_mv_db;
    float    ad8307_intercept_dbm;
    float    gain_12v;
    float    gain_5v;
} calib_blob_t;

/* Viz stejný `_Static_assert` v `syscfg.c` (audit F-0097, lekce L-0026). Dnes 20 B
 * ze 4080 — rezerva je obrovská, ale kalibrační MATICE nové desky (koeficient na
 * každou kombinaci cesty, útlumu a amplitudy, viz CLAUDE.md) se do jednoho sektoru
 * vejít NEMUSÍ, a tohle je místo, kde se to pozná při překladu. */
_Static_assert(sizeof(calib_blob_t) <= W25Q_STORE_MAX_BLOB,
               "kalibracni blob se nevejde do jednoho sektoru W25Q (W25Q_STORE_MAX_BLOB)");

static w25q_store_t s_store;

/* F-0098: bez tohohle nebylo jak poznat, ze CALIB store nenabehl — `g_calib` pak
 * zustane na datasheetovych vychozich a RF v dBm i vetve 12 V/5 V jsou
 * NEKALIBROVANE, aniz by to cokoli ohlasilo. */
int calib_store_ready(void) { return s_store.ready ? 1 : 0; }

void calib_load(void)
{
    /* Init + cteni pod jednim zamkem (w25q_init resetuje cip — mezi tim a ctenim
     * nesmi vlezt jiny kontext). */
    if (osMutexAcquire(qspiMutexHandle, CALIB_LOCK_MS) != osOK) return;
    calib_blob_t b;
    uint32_t n = 0;
    if (w25q_init()) {   /* flash nedostupna -> g_calib zustava na vychozich */
        w25q_store_init(&s_store, W25Q_CALIB_BASE, W25Q_CALIB_SECTORS);
        n = w25q_store_read(&s_store, &b, sizeof b);
    }
    osMutexRelease(qspiMutexHandle);

    if (n == sizeof(b) && b.magic == CALIB_BLOB_MAGIC) {
        g_calib.ad8307_slope_mv_db   = b.ad8307_slope_mv_db;
        g_calib.ad8307_intercept_dbm = b.ad8307_intercept_dbm;
        g_calib.gain_12v             = b.gain_12v;
        g_calib.gain_5v              = b.gain_5v;
    }
    /* jinak: zadny/nevalidni zaznam (nova/vymazana flash) -> vychozi hodnoty */
}

bool calib_save(void)
{
    if (!s_store.ready) return false;   /* calib_load nevolan nebo flash nedostupna */
    calib_blob_t b = {
        CALIB_BLOB_MAGIC,
        g_calib.ad8307_slope_mv_db,
        g_calib.ad8307_intercept_dbm,
        g_calib.gain_12v,
        g_calib.gain_5v,
    };
    if (osMutexAcquire(qspiMutexHandle, CALIB_LOCK_MS) != osOK) return false;
    bool ok = w25q_store_write(&s_store, &b, sizeof b);
    osMutexRelease(qspiMutexHandle);

    /* Zmena kalibrace posune VSECHNY nasledujici prepocty (RF dBm, 12V/5V) —
     * bez zaznamu by skok v logu vypadal jako zmena mereneho signalu.
     * 🔴 AZ TEDY, a jen pri uspechu (audit F-0094). Do 2026-09-17 bylo tohle
     * volani PRVNIM prikazem funkce, tedy pred kontrolou `s_store.ready`, pred
     * zamkem i pred zapisem — na vsech trech chybovych cestach pak v TRVALE
     * historii zustala veta „ulozena kalibrace napeti" o zmene, ktera se
     * neprovedla. Analyza pozdejsiho skoku v datech by pak hledala pricinu na
     * nespravnem miste. Neuspesny zapis patri pod ERRLOG_K_STORAGE (tam uz
     * hlasi `datalog_tick`), ne pod „nastaveni se zmenilo". */
    if (ok) (void)errlog_put(ERRLOG_K_CFG, ERRLOG_CFG_CALIB, 0u, 0u, "kalib");
    return ok;
}
