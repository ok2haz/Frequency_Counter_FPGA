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

static w25q_store_t s_store;

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
    return ok;
}
