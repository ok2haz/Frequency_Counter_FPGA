/**
 * @file    syscfg.c
 * @brief   Viz syscfg.h.
 */
#include "syscfg.h"
#include "w25q.h"
#include "w25q_store.h"
#include "w25q_map.h"
#include "freertos_shared.h"   /* g_brightness, g_theme_light, g_tz_*, g_ui_cfg, qspiMutexHandle */
#include "datalog.h"
#include "datalog.h"   /* datalog_sd_det_force/forced — persist override PE3 */           /* datalog_enabled/set_enabled — persist zap/vyp logovani */
#include "meas_math.h"         /* g_meas_cfg — persist Math/limity (#43/#44) */
#include "alarm.h"             /* g_mon_cfg — persist prahoveho monitoru */
#include "cmsis_os2.h"         /* osMutexAcquire/Release — QSPI zamek */
#include "stm32h7xx_hal.h"     /* HAL_GetTick */
#include <string.h>

/* Verzovany blob (magic se zmeni pri nekompatibilni zmene layoutu; store sam
 * overuje CRC16 -> magic jen potvrzuje ze bajty patri syscfg). Pole zabalena
 * bez paddingu-zavislosti (cteme/zapisujeme celou strukturu, kompilator stejny). */
/* ⚠️ Magic se MUSI zmenit pri kazde zmene layoutu blobu — jinak by se stary
 * zaznam nacetl jako novy a pole by se posunula. 2026-07-20 pribylo datalog_en
 * -> "SCFG" -> "SCF2". 2026-07-21 pribylo anim_en -> "SCF2" -> "SCF3", pak jeste
 * tyz den digit_anim_en -> "SCF3" -> "SCF4". 2026-07-24 pribyla bitmaska efektu
 * fx_en -> "SCF4" -> "SCF5". 2026-07-25 ODEBRAN digit_anim_en (zvyrazneni cislic
 * zruseno) -> "SCF5" -> "SCF6". 2026-08-01 pribyla persistence Math/limity
 * (g_meas_cfg, #43/#44) -> "SCF6" -> "SCF7", pak vysledek self-survey (poloha)
 * -> "SCF7" -> "SCF8". 2026-08-17 pribyl prahovy monitor (mon_cfg: VBAT/OCXO/ADEV)
 * -> "SCFA" -> "SCFB". Dusledek: prvni boot po teto zmene najde neznamy magic,
 * nastaveni se vrati na vychozi a pri prvni zmene se ulozi uz v novem formatu. */
#define SYSCFG_BLOB_MAGIC   0x53434642u   /* "SCFB" (2026-08-17: + prahovy monitor mon_cfg) */
#define SYSCFG_DEBOUNCE_MS  1500u         /* klid pred flash zapisem */
/* Timeouty QSPI mutexu. Boot (UiTask) muze pockat; auto-save z defaultTask NE —
 * defaultTask krmi watchdog (watchdog_supervise) a drenuje GPS frontu, takze pri
 * obsazene flash radeji hned odejde a zkusi to za dalsi tick (pending zustane). */
#define SYSCFG_LOCK_LOAD_MS 1000u
#define SYSCFG_LOCK_SAVE_MS 10u

typedef struct {
    uint32_t magic;
    uint8_t  brightness;
    uint8_t  sound_muted;
    uint8_t  autodim_en;
    uint16_t autodim_sec;
    uint8_t  theme_light;
    uint8_t  lang_en;
    int8_t   tz_offset_h;
    uint8_t  tz_auto;
    uint8_t  ui_cfg;
    uint8_t  datalog_en;   /* 1 = zaznam stability bezi (okno Datalog) */
    uint8_t  anim_en;      /* 1 = animace zapnute (okno Animace) */
    uint16_t fx_en;        /* bitmaska grafickych efektu (FX_*), viz freertos_shared.h */
    /* Math/limity (#43/#44). Flash je jediny zdroj (nejsou v BKP) -> aplikuji se
     * VZDY (jako fx_en). Doubles az na konci = 8B alignment padding neni problem
     * (cely blob se cte/pise stejnym kompilatorem, CRC kryje). */
    uint8_t  meas_math_en;
    uint8_t  meas_null_en;
    uint8_t  meas_limit_en;
    uint8_t  meas_alarm_en;
    double   meas_m;
    double   meas_b;
    double   meas_null_ref;
    double   meas_lo;
    double   meas_hi;
    /* Vysledek self-survey (poloha) — aplikuje se VZDY (jako fx/meas). */
    uint8_t  survey_valid;
    uint32_t survey_n;
    double   survey_lat, survey_lon;
    float    survey_alt, survey_spread;
    /* Sit (okno Sit, s_view=35). Dnes se JEN UKLADA — ETH je blokovana HW
     * (PHY dostava 10 MHz misto 25). Az prijde lwIP, cte se odsud. */
    uint8_t  net_dhcp;
    uint32_t net_ip, net_mask, net_gw;
    /* Override card-detect (PE3). Na teto desce cte PE3 HIGH i se zasunutou
     * kartou, takze bez override neprojde ani `BSP_SD_Init` (vrati NENI KARTA).
     * Persist proto, ze jinak by se `sd force on` muselo psat po kazdem bootu
     * a auto-mount i tlacitko EXPORT by byly k nicemu. */
    uint8_t  sd_det_force;
    /* Prahovy monitor realnych velicin (okno PRAHY, s_view=39). NENI v BKP ->
     * flash je jediny zdroj, takze se aplikuje VZDY (jako fx/meas/survey). */
    uint8_t  mon_vbat_en, mon_ocxo_en, mon_adev_en;
    float    mon_vbat_lo_mv;
    float    mon_ocxo_lo_c, mon_ocxo_hi_c;
    float    mon_adev_max;
} syscfg_blob_t;

static w25q_store_t s_store;

/* Naplni blob z aktualnich g_* globalu. */
static void pack(syscfg_blob_t *b)
{
    /* ⚠️ Vynuluj VCETNE paddingu: syscfg_flash_tick porovnava cely blob memcmp,
     * a s u16/double poli vznikaji mezi u8 poli padding bajty. Bez vynulovani by
     * padding drzel nahodne stack smeti -> memcmp by hlasil "zmenu" a spoustel
     * zbytecne flash zapisy (wear). (Pred pridanim doublů #43/#44 latentni.) */
    memset(b, 0, sizeof *b);
    b->magic        = SYSCFG_BLOB_MAGIC;
    b->brightness   = g_brightness;
    b->sound_muted  = g_sound_muted;
    b->autodim_en   = g_autodim_en;
    b->autodim_sec  = g_autodim_sec;
    b->theme_light  = g_theme_light;
    b->lang_en      = g_lang_en;
    b->tz_offset_h  = g_tz_offset_h;
    b->tz_auto      = g_tz_auto;
    b->ui_cfg       = g_ui_cfg;
    b->datalog_en   = datalog_enabled() ? 1u : 0u;
    b->anim_en      = g_anim_enabled ? 1u : 0u;
    b->fx_en        = (uint16_t)(g_fx_enabled & FX_ALL);
    b->meas_math_en  = g_meas_cfg.math_en ? 1u : 0u;
    b->meas_null_en  = g_meas_cfg.null_en ? 1u : 0u;
    b->meas_limit_en = g_meas_cfg.limit_en ? 1u : 0u;
    b->meas_alarm_en = g_meas_cfg.alarm_en ? 1u : 0u;
    b->meas_m        = g_meas_cfg.m;
    b->meas_b        = g_meas_cfg.b;
    b->meas_null_ref = g_meas_cfg.null_ref;
    b->meas_lo       = g_meas_cfg.lo;
    b->meas_hi       = g_meas_cfg.hi;
    b->survey_valid  = g_survey_valid ? 1u : 0u;
    b->survey_n      = g_survey_n;
    b->survey_lat    = g_survey_lat;
    b->survey_lon    = g_survey_lon;
    b->survey_alt    = g_survey_alt;
    b->survey_spread = g_survey_spread;
    b->net_dhcp      = g_net_dhcp ? 1u : 0u;
    b->net_ip        = g_net_ip;
    b->net_mask      = g_net_mask;
    b->net_gw        = g_net_gw;
    b->sd_det_force  = datalog_sd_det_forced() ? 1u : 0u;
    b->mon_vbat_en    = g_mon_cfg.vbat_en ? 1u : 0u;
    b->mon_ocxo_en    = g_mon_cfg.ocxo_en ? 1u : 0u;
    b->mon_adev_en    = g_mon_cfg.adev_en ? 1u : 0u;
    b->mon_vbat_lo_mv = g_mon_cfg.vbat_lo_mv;
    b->mon_ocxo_lo_c  = g_mon_cfg.ocxo_lo_c;
    b->mon_ocxo_hi_c  = g_mon_cfg.ocxo_hi_c;
    b->mon_adev_max   = g_mon_cfg.adev_max;
}

void syscfg_load(void)
{
    /* Vychozi prahy JAKO PRVNI — `g_mon_cfg` je prosty globál (vynulovany), takze
     * bez tohohle by pri prazdne flash / starem magicu / neuspesnem zamku platily
     * nuly: `vbat_lo_mv = 0` = nikdy nealarmuje, `ocxo_hi_c = 0` = alarmuje vzdy.
     * Nastavit je MUSIME i na chybovych cestach, proto pred zamkem. */
    mon_cfg_defaults(&g_mon_cfg);

    /* Cely init+cteni pod jednim zamkem — mezi w25q_init (SW reset cipu) a ctenim
     * nesmi vlezt jiny kontext, jinak by cetl z prave resetovaneho cipu. */
    if (osMutexAcquire(qspiMutexHandle, SYSCFG_LOCK_LOAD_MS) != osOK) return;

    syscfg_blob_t b;
    uint32_t n = 0;
    if (w25q_init() && w25q_store_init(&s_store, W25Q_CONFIG_BASE, W25Q_CONFIG_SECTORS)) {
        /* Cteme VZDY (i warm reset): g_fx_enabled NENI v BKP, flash je jeho jediny
         * zdroj. Ostatni pole se z flash aplikuji jen pri studenem startu (nize). */
        n = w25q_store_read(&s_store, &b, sizeof b);
    }
    osMutexRelease(qspiMutexHandle);

    if (n != sizeof(b) || b.magic != SYSCFG_BLOB_MAGIC) return;   /* prazdno/nova flash/stary magic */

    /* Efektove flagy: aplikuj VZDY (BKP je nedrzi -> flash autoritativni i pri warm
     * resetu; max o debounce okno starsi nez by byl BKP, kdyby ho drzel). */
    g_fx_enabled = (uint16_t)(b.fx_en & FX_ALL);

    /* Math/limity: taky NENI v BKP -> aplikuj VZDY (jako fx). Preset indexy (M,
     * pasmo) v UI se dopocitaji z g_meas_cfg pri otevreni okna (math_sync_idx). */
    g_meas_cfg.math_en  = b.meas_math_en ? 1 : 0;
    g_meas_cfg.null_en  = b.meas_null_en ? 1 : 0;
    g_meas_cfg.limit_en = b.meas_limit_en ? 1 : 0;
    g_meas_cfg.alarm_en = b.meas_alarm_en ? 1 : 0;
    g_meas_cfg.m        = (b.meas_m != 0.0) ? b.meas_m : 1.0;   /* 0 by byl mrtvy scale */
    g_meas_cfg.b        = b.meas_b;
    g_meas_cfg.null_ref = b.meas_null_ref;
    g_meas_cfg.lo       = b.meas_lo;
    g_meas_cfg.hi       = b.meas_hi;
    /* Self-survey poloha: NENI v BKP -> aplikuj VZDY (jako fx/meas). */
    g_survey_valid  = b.survey_valid ? 1 : 0;
    g_survey_n      = b.survey_n;
    g_survey_lat    = b.survey_lat;
    g_survey_lon    = b.survey_lon;
    g_survey_alt    = b.survey_alt;
    g_survey_spread = b.survey_spread;
    /* Prahovy monitor: NENI v BKP -> aplikuj VZDY (jako fx/meas/survey).
     * Sanitizace: nesmyslny rozsah by monitor zablokoval (nikdy nealarmuje) nebo
     * naopak rozdrncal (alarmuje porad) — pri nesmyslu zustavaji defaulty. */
    g_mon_cfg.vbat_en = b.mon_vbat_en ? 1 : 0;
    g_mon_cfg.ocxo_en = b.mon_ocxo_en ? 1 : 0;
    g_mon_cfg.adev_en = b.mon_adev_en ? 1 : 0;
    if (b.mon_vbat_lo_mv > 1000.0f && b.mon_vbat_lo_mv < 3600.0f)
        g_mon_cfg.vbat_lo_mv = b.mon_vbat_lo_mv;
    if (b.mon_ocxo_lo_c < b.mon_ocxo_hi_c &&
        b.mon_ocxo_lo_c > -40.0f && b.mon_ocxo_hi_c < 125.0f) {
        g_mon_cfg.ocxo_lo_c = b.mon_ocxo_lo_c;
        g_mon_cfg.ocxo_hi_c = b.mon_ocxo_hi_c;
    }
    if (b.mon_adev_max > 0.0f) g_mon_cfg.adev_max = b.mon_adev_max;

    g_net_dhcp      = b.net_dhcp ? 1u : 0u;
    g_net_ip        = b.net_ip;
    g_net_mask      = b.net_mask;
    g_net_gw        = b.net_gw;
    datalog_sd_det_force(b.sd_det_force ? 1 : 0);

    /* Ostatni pole: pri WARM resetu ma prednost BKP (uz drzi nejnovejsi) -> nechat. */
    if (g_syscfg_bkp_valid) return;

    /* Sanitizace (CRC uz sedi, ale kdyby layout/verze poskodila rozsah). */
    g_brightness  = (b.brightness < 25) ? 25 : b.brightness;
    g_sound_muted = b.sound_muted ? 1 : 0;
    g_autodim_en  = b.autodim_en ? 1 : 0;
    g_autodim_sec = (b.autodim_sec >= 15 && b.autodim_sec <= 600) ? b.autodim_sec : 300;   /* default 5 min */
    g_theme_light = b.theme_light ? 1 : 0;
    g_lang_en     = b.lang_en ? 1 : 0;
    g_tz_offset_h = (b.tz_offset_h < -12) ? -12 : (b.tz_offset_h > 14 ? 14 : b.tz_offset_h);
    g_tz_auto     = b.tz_auto ? 1 : 0;
    g_ui_cfg      = b.ui_cfg;
    datalog_set_enabled(b.datalog_en != 0);
    g_anim_enabled = b.anim_en ? 1 : 0;
}

bool syscfg_save(void)
{
    if (!s_store.ready) return false;   /* syscfg_load nevolan / flash nedostupna */
    syscfg_blob_t b;
    pack(&b);
    /* Kratky timeout: volajici (syscfg_flash_tick z defaultTask) pri neuspechu
     * jen nechá pending=1 a zkusi to za dalsi tick — zadne blokovani watchdogu. */
    if (osMutexAcquire(qspiMutexHandle, SYSCFG_LOCK_SAVE_MS) != osOK) return false;
    bool ok = w25q_store_write(&s_store, &b, sizeof b);
    osMutexRelease(qspiMutexHandle);
    return ok;
}

void syscfg_flash_tick(void)
{
    if (!s_store.ready) return;

    static syscfg_blob_t snap;
    static uint8_t  have_snap = 0;
    static uint8_t  pending   = 0;
    static uint32_t change_ms = 0;

    syscfg_blob_t cur;
    pack(&cur);

    if (!have_snap) { snap = cur; have_snap = 1; return; }   /* baseline pri bootu, bez zapisu */

    if (memcmp(&cur, &snap, sizeof cur) != 0) {
        snap = cur; change_ms = HAL_GetTick(); pending = 1;   /* zmena -> resetuj debounce */
    } else if (pending && (uint32_t)(HAL_GetTick() - change_ms) >= SYSCFG_DEBOUNCE_MS) {
        if (syscfg_save()) pending = 0;   /* po klidu jeden zapis; pri chybe zkusi priste */
    }
}
