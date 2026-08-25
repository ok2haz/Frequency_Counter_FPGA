/**
 * @file    ipc.c
 * @brief   CM7 strana IPC (STATUS.md #19/#20): init + publikace snapshotu do
 *          SRAM4 (seqlock) + servis cmd ringu + pure-logic selftest.
 *
 * Protokol a datove struktury jsou v `ipc_shared.h` (ZDROJ PRAVDY pro OBA jadra);
 * tady je CM7-strana logiky, ktera je HW-nezavisla a bezi uz ted, jeste nez
 * nabehne CM4.
 *
 * ✅ STAV DVOUJADRA (2026-08-14): CM4 BEZI a round-trip je HW-OVERENY — CM4 cte
 * snapshot (overil magic+verzi) a publikuje heartbeat zpet (`CM4: alive`).
 * SRAM4 je non-cacheable (MPU region 2, `main.c`) + vyhrazena linkerem (sekce
 * `.ipc_shared` @RAM_D3). ⚠️ CM4 testuj VZDY po power-cyklu BEZ ladici sondy —
 * pripojeny debugger rozbiji boot handshake a CM4 pak vypada jako nenabehly.
 * Cmd/resp servis: `IPC_CMD_CFG_SET` (Math/limity) je funkcni, zbytek (GATE/RUN/
 * CHAN/LOG) dozraje se SCPI/webem na CM4 — tam vznikne producent prikazu.
 *
 * ⚠️ ZLATE PRAVIDLO (STATUS.md): do snapshotu se plni JEN realna data. Statistika
 * (sigma_tau/offset/drift) se ZATIM NEPUBLIKUJE — jejich zdroj je dnes simulace
 * headline (#2). Doplni se, az je bude pocitat MathTask z realnych dat FPGA (#27).
 * Pole zustavaji vynulovana (init), aby CM4 nikdy neservoval simulaci jako pravdu.
 */
#include "ipc_shared.h"
#include "fpga_freq.h"        /* fpga_freq_get_last — REALNY kmitocet /4 i /16 */
#include "gps.h"              /* gps_get — GPS cast snapshotu */
#include "sensor_stat.h"      /* g_sensors[] — teploty + napajeni + RF */
#include "calib.h"            /* g_calib — AD8307 slope/intercept do snapshotu (v2) */
#include "meas_math.h"        /* g_meas_cfg, meas_cfg_t, meas_math_capture_null — config sync (v3) */
#include "datalog.h"          /* datalog_set_enabled — IPC_CMD_LOG z CM4 (W1); v12 datalog_read_back/status */
#include "alarm.h"            /* g_alarm_*, g_mon_*_bad — dashboard STAV karta (v12, #4) */
#include "scpi.h"             /* JEN pro _Static_assert SCPI_CFG_* == IPC_CFG_* (viz nize) */
#include <stddef.h>           /* offsetof — kontrola layoutu ipc_sat_t vs gps_sat_t */
#include "freertos_shared.h"  /* g_spi_ok, g_freq_stale, g_si5356_*, g_ui_cfg, g_uptime_s, ... */
#include "FreeRTOS.h"         /* taskENTER_CRITICAL — atomicky commit g_meas_cfg */
#include "task.h"
#include "cmsis_os2.h"        /* osKernelGetTickCount — throttle bez HAL zavislosti */
#include <string.h>

/* ── v12: `ipc_sat_t` (ipc_shared.h, bez gps.h) MUSI mit shodny layout s
 * `gps_sat_t` (gps.h) — publikace druzic je proste `memcpy`. Kdyby se rozesly,
 * web by kreslil sky plot ze smetĺ. Ty dva headery se jinak nepotkaji v jedne TU. */
_Static_assert(IPC_GPS_MAX_SATS == GPS_MAX_SATS, "IPC/GPS pocet druzic se rozesel");
_Static_assert(sizeof(ipc_sat_t) == sizeof(gps_sat_t), "ipc_sat_t != gps_sat_t velikost");
_Static_assert(offsetof(ipc_sat_t, prn)     == offsetof(gps_sat_t, prn),     "sat.prn offset");
_Static_assert(offsetof(ipc_sat_t, elev)    == offsetof(gps_sat_t, elev),    "sat.elev offset");
_Static_assert(offsetof(ipc_sat_t, snr)     == offsetof(gps_sat_t, snr),     "sat.snr offset");
_Static_assert(offsetof(ipc_sat_t, constel) == offsetof(gps_sat_t, constel), "sat.constel offset");
_Static_assert(offsetof(ipc_sat_t, azim)    == offsetof(gps_sat_t, azim),    "sat.azim offset");

/* ── Razitko: vynuluj celou sdilenou strukturu (seq=0 sude, ringy prazdne) a
 * orazitkuj snapshot (magic/verze/velikost). Pracuje nad DANOU instanci → sdili
 * ho ipc_init (g_ipc) i selftest (lokalni kopie), zadny duplikat. */
static void ipc_stamp(volatile ipc_shared_t *p)
{
    memset((void *)p, 0, sizeof *p);
    p->snap.magic   = IPC_MAGIC;
    p->snap.version = (uint16_t)IPC_VERSION;
    p->snap.size    = (uint16_t)sizeof(ipc_snapshot_t);
    IPC_DMB();
}

/* ── Init: orazitkuj sdileny g_ipc. Volat JEDNOU z CM7 pred rozjezdem publikace
 * (defaultTask, pred smyckou). CM4 po bootu overi magic+version+size; nesouhlas
 * -> IPC vypne a jede degradovane. */
void ipc_init(void) { ipc_stamp(&g_ipc); }

/* Minimalni agregace zdravi z REALNYCH globalu (samostatna od app compute_sys_level,
 * ktera zije v UI vrstve). 0=OK, 1=warn (degradovano, meri dal), 2=err (kriticke). */
static uint8_t ipc_sys_level(const gps_data_t *g)
{
    /* Kriticke = ztrata 10 MHz reference (Si5356 bit3 LOS_CLKIN / bit4 PLL_LOL). */
    if (g_si5356_ok && (g_si5356_status & ((1u << 3) | (1u << 4)))) return 2;
    /* Degradace = mrtvy SPI link, ztrata signalu, nebo bez GPS fixu. */
    if (!g_spi_ok || g_freq_stale || !(g->valid && g->fix_mode >= 2)) return 1;
    return 0;
}

/* ── Publikace snapshotu (CM7 -> CM4). Throttle ~2 Hz uvnitr — snapshot ma byt
 * cerstvy pro budouci dashboard, ale 100 Hz smycka defaultTasku by byla plytvani.
 * Vola defaultTask v hlavni smycce. */
void ipc_publish(void)
{
    static uint32_t s_last_ms, s_last_seq_meas;
    uint32_t now = osKernelGetTickCount();

    /* Posbirej realny stav MIMO seqlock (kratke drzeni licheho seq = min. retry CM4). */
    fpga_meas_t m;
    int have_meas = fpga_freq_get_last(&m);

    /* EVENT-DRIVEN: publikuj na NOVE mereni (seq_meas se zmeni) NEBO periodicky (>=2 Hz
     * heartbeat, aby snapshot seq stale rostl -> CM4 pozna zivy CM7). FPGA meri ~4/s
     * (gate 0,25 s) -> pevny 2 Hz throttle by mereni PODVZORKOVAL; takhle snapshot chytne
     * kazde mereni s min. latenci (~jeden defaultTask tik po tom, co ho FpgaTask zverejni). */
    int meas_new = have_meas && (m.sequence != s_last_seq_meas);
    if (!meas_new && (now - s_last_ms) < 500u) return;
    s_last_ms = now;
    if (have_meas) s_last_seq_meas = m.sequence;

    int meas_ok = have_meas && (m.measurement_status & 0x01u)
                  && !(m.error_flags & FPGA_ERR_SIGNAL_LOST);

    gps_data_t g; gps_get(&g);
    /* Math/limit cfg kopie MIMO seqlock (kriticka sekce kvuli double) — zkracuje seq-odd
     * okno (drive byla uvnitr begin/end -> maskovala IRQ behem publikace = vic retry CM4). */
    meas_cfg_t mc; taskENTER_CRITICAL(); mc = g_meas_cfg; taskEXIT_CRITICAL();

    uint32_t flags = 0;
    if (g_spi_ok)                        flags |= IPC_F_FPGA_LINK;
    if (g_freq_stale)                    flags |= IPC_F_SIGNAL_LOST;
    if (g.valid)                         flags |= IPC_F_GPS_VALID;
    if (!g.valid && g.fixes > 0)         flags |= IPC_F_HOLDOVER;   /* fix byl a ztratil se */
    if (g_si5356_ok && (g_si5356_status & (1u << 3))) flags |= IPC_F_SI5356_LOS;
    if (g_ui_cfg & (1u << 4))            flags |= IPC_F_RUNNING;    /* bit4 = RUN (BKP_DR1) */

    uint8_t sysl = ipc_sys_level(&g);

    /* ── Atomicka publikace (seqlock: liche seq behem zapisu). ── */
    ipc_snap_publish_begin();

    if (meas_ok) {
        g_ipc.snap.freq4_x100000  = m.frequency_x100000;
        g_ipc.snap.freq16_x100000 = (m.status2 & FPGA_ST2_DIV16_ERR) ? 0u : m.freq16_x100000;
        g_ipc.snap.freq_x100000   = m.frequency_x100000;   /* zvoleny zdroj = /4 (vyber /16 je app vrstva) */
        g_ipc.snap.gate_ns        = (uint32_t)m.gate_time_ns;
        g_ipc.snap.seq_meas       = m.sequence;
    } else {
        g_ipc.snap.freq4_x100000 = g_ipc.snap.freq16_x100000 = g_ipc.snap.freq_x100000 = 0u;
        g_ipc.snap.gate_ns = 0u;
    }

    /* ⚠️ sigma_tau/tau_s/offset/drift ZAMERNE neplnime (zdroj = simulace #2). */

    g_ipc.snap.gps_lat_e7   = (int32_t)(g.lat_deg * 1e7f);
    g_ipc.snap.gps_lon_e7   = (int32_t)(g.lon_deg * 1e7f);
    g_ipc.snap.gps_alt_cm   = (int32_t)(g.alt_m * 100.0f);
    g_ipc.snap.gps_hdop     = g.hdop;
    g_ipc.snap.gps_valid    = g.valid;
    g_ipc.snap.gps_fix_mode = g.fix_mode;
    g_ipc.snap.gps_num_sat  = g.num_sat;
    /* rtc_unix = aktualni UTC z RTC (0 = nesynchronizovano). Bez toho web/SCPI
     * ukazoval CAS UTC "00:00:00" (pole zustavalo 0 z memsetu). ipc_publish bezi
     * v defaultTasku, stejne jako pisatel g_rtc_text -> cteni je konzistentni.
     * ✅ Overeno na HW 2026-08-25 (cas na webu spravne). */
    g_ipc.snap.rtc_unix     = datalog_now_unix();

    /* ⚠️ Hodnota + BIT PLATNOSTI musi vzniknout ZAROVEN a stejnym pravidlem jako
     * ve `scpi_src_load_cm7()`, jinak by tentyz pristroj rekl pres USB neco jineho
     * nez pres TCP. Dokud bit neni nastaven, obsah pole je nezavazny (drzime tam
     * posledni dobrou hodnotu — pro trendy se hodi, jako mereni se servirovat NESMI).
     * Do v3 se neplatna napeti publikovala jako 0 (nerozeznatelne od skutecne nuly)
     * a neplatne teploty vubec neoznacene. */
    uint32_t sv = 0u;
    #define IPC_PUB_SENS(id, field, scale, bit)                                   \
        do { g_ipc.snap.field = (typeof(g_ipc.snap.field))(g_sensors[id].last * (scale)); \
             if (g_sensors[id].valid) sv |= (bit); } while (0)

    IPC_PUB_SENS(SENS_T49,    t_ocxo_c100,  100.0f, IPC_V_T_OCXO);
    IPC_PUB_SENS(SENS_T48,    t_board_c100, 100.0f, IPC_V_T_BOARD);
    IPC_PUB_SENS(SENS_CORE_T, t_mcu_c100,   100.0f, IPC_V_T_MCU);
    IPC_PUB_SENS(SENS_T4A,    t_fpga_c100,  100.0f, IPC_V_T_FPGA);   /* 0x4A dnes neosazen */
    IPC_PUB_SENS(SENS_ADS0,   ocxo_vc_mv,     1.0f, IPC_V_VC);
    IPC_PUB_SENS(SENS_ADS1,   rf_mv,          1.0f, IPC_V_RF);
    IPC_PUB_SENS(SENS_ADS2,   v_12v_mv,       1.0f, IPC_V_V12);
    IPC_PUB_SENS(SENS_ADS3,   v_5v_mv,        1.0f, IPC_V_V5);
    IPC_PUB_SENS(SENS_VDDA,   vref_mv,        1.0f, IPC_V_VREF);
    IPC_PUB_SENS(SENS_VBAT,   vbat_mv,        1.0f, IPC_V_VBAT);
    #undef IPC_PUB_SENS

    /* Mereni + GPS do tehoz slova — CM4 pak jen priradi `src->valid = snap.sens_valid`.
     * Podminky MUSI doslova odpovidat `scpi_src_load_cm7()` (scpi.c):
     *   FRAME  = `fpga_freq_get_last()` vratil ramec (zdejsi `have_meas`),
     *   FREQ   = k tomu measurement_status bit0 a zadny SIGNAL_LOST (`meas_ok`),
     *   DIV16  = k tomu bez FPGA_ST2_DIV16_ERR.
     * ⚠️ FREQ zamerne NEvyzaduje novou SEQ — `scpi_src_load_cm7` ji taky nekontroluje
     * (staleness hlasi zvlast `MEAS:FREQ:STAL?`). */
    if (have_meas) {
        sv |= IPC_V_FRAME;
        if (meas_ok) {
            sv |= IPC_V_FREQ;
            if (!(m.status2 & FPGA_ST2_DIV16_ERR)) sv |= IPC_V_DIV16;
        }
    }
    if (g.valid) sv |= IPC_V_GPS;
    g_ipc.snap.sens_valid = sv;
    g_ipc.snap.channel_id   = meas_ok ? m.channel_id : 0u;
    /* v11: NASTAVENI mereni (brana/kanal/RUN). ⚠️ Zamerne `g_ui_cfg`, ne `g_ui_cfg_req`:
     * publikuje se to, co UiTask uz APLIKOVAL, ne cekajici pozadavek — jinak by readback
     * hlasil zmenu drive, nez se projevi na pristroji. Radek nad tim (`channel_id`) je
     * neco jineho: kanal, ktery ohlasil FPGA ramec. */
    g_ipc.snap.ui_cfg       = g_ui_cfg;
    g_ipc.snap.si5356_status = g_si5356_status;
    g_ipc.snap.si5356_ok    = g_si5356_ok;
    /* Kalibrace RF (aby CM4 spocital MEAS:POW? dBm z rf_mv) — g_calib je volatile float. */
    g_ipc.snap.ad8307_slope_mv_db  = g_calib.ad8307_slope_mv_db;
    g_ipc.snap.ad8307_intercept_dbm = g_calib.ad8307_intercept_dbm;

    g_ipc.snap.flags        = flags;
    g_ipc.snap.sys_level    = sysl;
    g_ipc.snap.uptime_s     = g_uptime_s;
    g_ipc.snap.cm7_cpu_pct  = g_rtos_cpu_pct;
    g_ipc.snap.reset_cause  = g_reset_rsr;

    /* Math/limit cfg mirror (v3) — z predem posbirane kopie `mc` (mimo seqlock, viz vyse). */
    g_ipc.snap.math_m   = mc.m;   g_ipc.snap.math_b   = mc.b;   g_ipc.snap.null_ref = mc.null_ref;
    g_ipc.snap.lim_lo   = mc.lo;  g_ipc.snap.lim_hi   = mc.hi;
    g_ipc.snap.math_en  = mc.math_en; g_ipc.snap.null_en = mc.null_en; g_ipc.snap.limit_en = mc.limit_en;

    /* v8 (W3): hlavni vypinac vzdaleneho OVLADANI (okno PRISTUP) — TCP SCPI server na
     * CM4 ho kontroluje pred pripojenim `set_cfg`. Cteni je vzdy povolene, bez ohledu
     * na tenhle bit. */
    g_ipc.snap.web_ctrl_en = g_web_ctrl_en;

    /* v10 (W5): prihlasovaci udaje pro HTTP Basic Auth na CM4. `g_web_user/g_web_pass`
     * i `g_ipc.snap.*` jsou volatile (`g_ipc` makro), ale `strncpy` bere `char *` —
     * cast je bezpecny, protoze poradi zapisu vuci CM4 hlida VYHRADNE `IPC_DMB()`
     * v `ipc_snap_publish_begin/end` (viz seqlock), ne `volatile` samotne. Vzdy
     * 0-terminovano (rezerva -1 v cilove velikosti + explicitni zapis posl. bajtu). */
    strncpy((char *)g_ipc.snap.web_user, (const char *)g_web_user, sizeof g_ipc.snap.web_user - 1);
    ((char *)g_ipc.snap.web_user)[sizeof g_ipc.snap.web_user - 1] = '\0';
    strncpy((char *)g_ipc.snap.web_pass, (const char *)g_web_pass, sizeof g_ipc.snap.web_pass - 1);
    ((char *)g_ipc.snap.web_pass)[sizeof g_ipc.snap.web_pass - 1] = '\0';

    /* v12 (#4): alarmy/prahy/selftest pro dashboard. Pocitadla saturuj na 0xFFFF. */
    #define IPC_SAT16(x) ((uint16_t)((x) > 0xFFFFu ? 0xFFFFu : (x)))
    g_ipc.snap.alarm_fpga_lost  = IPC_SAT16(g_alarm_fpga_lost);
    g_ipc.snap.alarm_gps_lost   = IPC_SAT16(g_alarm_gps_lost);
    g_ipc.snap.alarm_limit_fail = IPC_SAT16(g_alarm_limit_fail);
    g_ipc.snap.alarm_vbat       = IPC_SAT16(g_alarm_vbat);
    g_ipc.snap.alarm_ocxo       = IPC_SAT16(g_alarm_ocxo);
    g_ipc.snap.alarm_adev       = IPC_SAT16(g_alarm_adev);
    #undef IPC_SAT16
    g_ipc.snap.mon_vbat_bad = g_mon_vbat_bad;
    g_ipc.snap.mon_ocxo_bad = g_mon_ocxo_bad;
    g_ipc.snap.mon_adev_bad = g_mon_adev_bad;
    g_ipc.snap.selftest_res = g_selftest_res;

    /* v12 (#5): GPS druzice pro sky plot. Layout ipc_sat_t == gps_sat_t (assert
     * vyse) -> proste memcpy platnych polozek. */
    {
        uint8_t nsat = (g.sat_count > IPC_GPS_MAX_SATS) ? IPC_GPS_MAX_SATS : g.sat_count;
        g_ipc.snap.gps_sat_count = nsat;
        memcpy((void *)g_ipc.snap.gps_sats, g.sats, (size_t)nsat * sizeof(ipc_sat_t));
    }

    ipc_snap_publish_end();
}

/* ── v12 (#6): obsluha datalog transfer kanalu (CM4 -> CM7 -> CM4). ────────────
 * CM4 zapise pozadavek (req_from/count/step) a zvedne `req_gen`; my precteme az
 * IPC_LOG_CHUNK zaznamu z W25Q datalogu (s decimaci `req_step`) a nastavime
 * `resp_gen = req_gen`. Datalog cte JEN CM7 (flash je fyzicky tady) — proto tudy.
 * ⚠️ BLOKUJICI (W25Q cteni pod qspiMutexHandle) -> vola VYHRADNE defaultTask, jako
 * datalog_tick. Jedno cteni chunku (<= 96 zaznamu) je radove ms; probiha jen kdyz
 * uzivatel na webu vyzada dlouhou historii, ne periodicky. `datalog_read_back` ma
 * kratky mutex timeout a pri obsazene flash zaznam vynecha (nezdrzi watchdog). */
void ipc_datalog_service(void)
{
    uint32_t req = g_ipc.log.req_gen;
    if (req == g_ipc.log.resp_gen) return;          /* zadny novy pozadavek */

    datalog_status_t st;
    datalog_get_status(&st);
    g_ipc.log.resp_total = st.records;

    uint16_t want = g_ipc.log.req_count;
    if (want > IPC_LOG_CHUNK) want = IPC_LOG_CHUNK;
    uint16_t step = g_ipc.log.req_step ? g_ipc.log.req_step : 1u;
    uint32_t from = g_ipc.log.req_from;             /* 0 = nejnovejsi */

    uint16_t got = 0;
    for (uint16_t i = 0; i < want; i++) {
        datalog_rec_t r;
        uint32_t idx = from + (uint32_t)i * step;
        if (idx >= st.records) break;
        if (!datalog_read_back(idx, &r)) break;     /* mimo rozsah / flash obsazena */
        ipc_log_rec_t *o = (ipc_log_rec_t *)&g_ipc.log.rec[got];
        o->t_unix       = r.t_unix;
        o->freq_x100000 = r.freq_x100000;
        o->t_ocxo_c100  = r.t_ocxo_c100;
        o->t_board_c100 = r.t_board_c100;
        o->ocxo_vc_mv   = (uint16_t)r.ocxo_vc_mv;
        o->rf_mv        = (uint16_t)r.rf_mv;
        o->vbat_mv      = (uint16_t)r.vbat_mv;
        o->flags        = r.flags;
        o->sats         = r.sats;
        o->hdop10       = r.hdop10;
        o->_pad         = 0u;
        got++;
    }
    g_ipc.log.resp_count = got;
    IPC_DMB();
    g_ipc.log.resp_gen = req;                        /* az PO naplneni rec[] -> CM4 vidi konzistentne */
}

/* Posledni REALNY kmitocet /4 [Hz] (pro NULL_ACQ). @return 1 = platne. */
static int ipc_real_freq_hz(double *hz)
{
    fpga_meas_t m;
    if (fpga_freq_get_last(&m) && (m.measurement_status & 0x01u)
        && !(m.error_flags & FPGA_ERR_SIGNAL_LOST)) {
        *hz = (double)m.frequency_x100000 / 100000.0;
        return 1;
    }
    return 0;
}

/* Aplikuje JEDEN CFG_SET klic na PREDANOU cfg (mirror scpi_calc_set — testovatelne na
 * lokalni cfg). @return 1 = klic rozpoznan a aplikovan. NULL_ACQ potrebuje platny freq_hz
 * (>0); jinak 0 (neni co nulovat). Bool klice berou `arg`, double klice `argd`. */
/* ⚠️⚠️ `SCPI_CFG_*` (scpi.h) a `IPC_CFG_*` (ipc_shared.h) jsou DVA paralelní
 * výčty, které MUSÍ sedět 1:1. CM4 SCPI parser vyrobí `SCPI_CFG_*` klíč, pošle
 * ho cmd ringem jako `ipc_cmd_t.key` a `ipc_cfg_apply()` ho níže přečte jako
 * `IPC_CFG_*` — mezi nimi NENÍ žádný převod, je to holý přenos čísla.
 * Kdyby někdo do jednoho výčtu vložil položku, hodnoty by se tiše posunuly a
 * třeba `CALC:LIM:LOW` by zapsal do `null_ref`: bez chyby, bez varování,
 * jen špatná data. Doteď to nešlo odhalit ani teoreticky — ty dvě hlavičky se
 * nikdy nepotkaly v jedné translation unit. `scpi.h` se sem includuje
 * VÝHRADNĚ kvůli téhle kontrole.
 * (Alternativa = smazat tenhle duplikát a volat rovnou `scpi_cfg_apply()`,
 * které je veřejné a pure. Je to čistší, ale mění dvoujádrový kontrakt —
 * záměrně to nedělám bez zadání.) */
_Static_assert((int)SCPI_CFG_MATH_EN  == (int)IPC_CFG_MATH_EN,  "SCPI/IPC klic MATH_EN se rozesel");
_Static_assert((int)SCPI_CFG_MATH_M   == (int)IPC_CFG_MATH_M,   "SCPI/IPC klic MATH_M se rozesel");
_Static_assert((int)SCPI_CFG_MATH_B   == (int)IPC_CFG_MATH_B,   "SCPI/IPC klic MATH_B se rozesel");
_Static_assert((int)SCPI_CFG_NULL_EN  == (int)IPC_CFG_NULL_EN,  "SCPI/IPC klic NULL_EN se rozesel");
_Static_assert((int)SCPI_CFG_NULL_ACQ == (int)IPC_CFG_NULL_ACQ, "SCPI/IPC klic NULL_ACQ se rozesel");
_Static_assert((int)SCPI_CFG_LIM_EN   == (int)IPC_CFG_LIM_EN,   "SCPI/IPC klic LIM_EN se rozesel");
_Static_assert((int)SCPI_CFG_LIM_LO   == (int)IPC_CFG_LIM_LO,   "SCPI/IPC klic LIM_LO se rozesel");
_Static_assert((int)SCPI_CFG_LIM_HI   == (int)IPC_CFG_LIM_HI,   "SCPI/IPC klic LIM_HI se rozesel");
_Static_assert((int)SCPI_CFG_GATE     == (int)IPC_CFG_GATE,     "SCPI/IPC klic GATE se rozesel");
_Static_assert((int)SCPI_CFG_CHAN     == (int)IPC_CFG_CHAN,     "SCPI/IPC klic CHAN se rozesel");
_Static_assert((int)SCPI_CFG_RUN      == (int)IPC_CFG_RUN,      "SCPI/IPC klic RUN se rozesel");

/* ⚠️ Totez pro masku platnosti: `snap.sens_valid` se na CM4 priradi PRIMO do
 * `scpi_src_t.valid`, takze bitove pozice musi sedet 1:1 se `SCPI_V_*`.
 * Rozejiti by nebylo videt jako chyba — jen by treba `MEAS:VOLT? P5` hlasilo
 * NaN misto hodnoty (nebo hur: hodnotu misto NaN). */
_Static_assert((int)SCPI_V_FREQ    == (int)IPC_V_FREQ,    "SCPI/IPC bit FREQ se rozesel");
_Static_assert((int)SCPI_V_DIV16   == (int)IPC_V_DIV16,   "SCPI/IPC bit DIV16 se rozesel");
_Static_assert((int)SCPI_V_FRAME   == (int)IPC_V_FRAME,   "SCPI/IPC bit FRAME se rozesel");
_Static_assert((int)SCPI_V_T_OCXO  == (int)IPC_V_T_OCXO,  "SCPI/IPC bit T_OCXO se rozesel");
_Static_assert((int)SCPI_V_T_BOARD == (int)IPC_V_T_BOARD, "SCPI/IPC bit T_BOARD se rozesel");
_Static_assert((int)SCPI_V_T_MCU   == (int)IPC_V_T_MCU,   "SCPI/IPC bit T_MCU se rozesel");
_Static_assert((int)SCPI_V_T_FPGA  == (int)IPC_V_T_FPGA,  "SCPI/IPC bit T_FPGA se rozesel");
_Static_assert((int)SCPI_V_VC      == (int)IPC_V_VC,      "SCPI/IPC bit VC se rozesel");
_Static_assert((int)SCPI_V_RF      == (int)IPC_V_RF,      "SCPI/IPC bit RF se rozesel");
_Static_assert((int)SCPI_V_V12     == (int)IPC_V_V12,     "SCPI/IPC bit V12 se rozesel");
_Static_assert((int)SCPI_V_V5      == (int)IPC_V_V5,      "SCPI/IPC bit V5 se rozesel");
_Static_assert((int)SCPI_V_VREF    == (int)IPC_V_VREF,    "SCPI/IPC bit VREF se rozesel");
_Static_assert((int)SCPI_V_VBAT    == (int)IPC_V_VBAT,    "SCPI/IPC bit VBAT se rozesel");
_Static_assert((int)SCPI_V_GPS     == (int)IPC_V_GPS,     "SCPI/IPC bit GPS se rozesel");

/* ── W1 (2026-08-22): stav MERENI (brana/kanal/RUN) z CM4 ────────────────────
 * ⚠️ Tohle NEPATRI do `ipc_cfg_apply` (to je cista funkce nad `meas_cfg_t`, kterou
 * pousti i selftest nad lokalni kopii) — stav mereni vlastni UiTask, ne `g_meas_cfg`.
 * `ipc_service` bezi v defaultTasku, takze se sem zapisuje jen POZADAVEK a UiTask ho
 * aplikuje (`screen_main_apply_cfg_req`) vcetne prekresleni footeru a persistence.
 *
 * Je to ZAMERNE tentyz most, jaky uz pouziva SCPI pres USB (`scpi_src_set_cfg_cm7`) —
 * ne druha cesta, ktera by se casem rozesla. Skladame na AKTUALNI hodnotu, aby dva
 * SETy za sebou nesmazaly jeden druhy.
 * ⚠️ Funkce je NECISTA (globaly) → nesmi se volat z `ipc_selftest`.
 * ⚠️ Do W1 tyhle klice spadaly do `default: return 0`, takze z CM4 NESLO RUN/STOP,
 * branu ani kanal — dokumentace tvrdila opak (viz WEB_UI_PLAN.md, nalez 1.1).
 * @return 1 = prijato. */
static int ipc_ui_cfg_apply(uint8_t key, uint32_t arg)
{
    uint8_t cur = g_ui_cfg_req_pend ? g_ui_cfg_req : g_ui_cfg;
    switch (key) {
        case IPC_CFG_GATE:                              /* arg = index presetu 0..3 */
            if (arg > 3u) return 0;
            cur = (uint8_t)((cur & ~(3u << 2)) | ((arg & 3u) << 2));
            break;
        case IPC_CFG_CHAN:                              /* mame jen kanal 0/1 */
            if (arg > 1u) return 0;
            cur = (uint8_t)((cur & ~(1u << 1)) | ((arg & 1u) << 1));
            break;
        case IPC_CFG_RUN:
            /* ⚠️ Na rozdil od GATE/CHAN se validace pridava az ted (2026-08-23, HW test
             * odhalil): `arg` != 0/1 se drive tise bralo jako "bez", stejny (existujici)
             * vzor jako CM7 SCPI backend `scpi_src_set_cfg_cm7`. Tady se ale utahuje,
             * protoze `ipccmd run <cislo>` je primy vstup z UART bez SCPI bool parseru
             * (ON/OFF/1/0) pred sebou — bez teto kontroly `run 9` tise projde jako "run". */
            if (arg > 1u) return 0;
            cur = (uint8_t)((cur & ~(1u << 4)) | ((arg ? 1u : 0u) << 4));
            break;
        default: return 0;
    }
    g_ui_cfg_req = cur;
    g_ui_cfg_req_pend = 1;      /* az teprve ted -> UiTask cte hotovou hodnotu */
    return 1;
}

static int ipc_cfg_apply(meas_cfg_t *c, uint8_t key, uint32_t arg, double argd, double freq_hz)
{
    switch (key) {
        case IPC_CFG_MATH_EN:  c->math_en  = arg ? 1u : 0u; return 1;
        case IPC_CFG_MATH_M:   c->m  = argd; return 1;
        case IPC_CFG_MATH_B:   c->b  = argd; return 1;
        case IPC_CFG_NULL_EN:  c->null_en  = arg ? 1u : 0u; return 1;
        case IPC_CFG_NULL_ACQ: if (freq_hz > 0.0) { meas_math_capture_null(c, freq_hz); return 1; } return 0;
        case IPC_CFG_LIM_EN:   c->limit_en = arg ? 1u : 0u; return 1;
        case IPC_CFG_LIM_LO:   c->lo = argd; return 1;
        case IPC_CFG_LIM_HI:   c->hi = argd; return 1;
        default:               return 0;
    }
}

/* ── Servis prikazu z CM4 (cmd ring -> odpoved do resp ringu). Vola defaultTask.
 * NOP echuje OK; CFG_SET aplikuje Math/limit config na predanou cfg; neznamy typ/klic
 * -> status=1. Jadro nad DANYMI ringy+cfg → testovatelne na lokalni kopii. */
static int ipc_service_rings(volatile ipc_cmd_ring_t *cmd, volatile ipc_resp_ring_t *resp,
                             meas_cfg_t *cfg, double freq_hz)
{
    int handled = 0;
    ipc_cmd_t c;
    while (ipc_ring_cmd_pop(cmd, &c)) {
        ipc_resp_t r = { .id = c.id, .status = 0u, ._pad = 0, .value = c.arg };
        switch (c.type) {
            case IPC_CMD_NOP:      break;                       /* zdravi ringu — echo OK */
            case IPC_CMD_CFG_SET:
                /* Klice stavu mereni jdou mostem do UiTasku, Math/limity na predanou cfg.
                 * ⚠️ Rozdeleni je nutne: `ipc_cfg_apply` je cista funkce nad `meas_cfg_t`
                 * (bezi i v selftestu nad lokalni kopii), kdezto GATE/CHAN/RUN sahaji na globaly. */
                if (c.key == IPC_CFG_GATE || c.key == IPC_CFG_CHAN || c.key == IPC_CFG_RUN) {
                    if (!ipc_ui_cfg_apply(c.key, c.arg)) r.status = 1u;
                } else if (!ipc_cfg_apply(cfg, c.key, c.arg, c.argd, freq_hz)) {
                    r.status = 1u;
                }
                break;
            /* Starsi „prikazove" varianty tehoz (CM4 muze poslat obojí). */
            case IPC_CMD_GATE:     if (!ipc_ui_cfg_apply(IPC_CFG_GATE, c.arg)) r.status = 1u; break;
            case IPC_CMD_CHAN:     if (!ipc_ui_cfg_apply(IPC_CFG_CHAN, c.arg)) r.status = 1u; break;
            case IPC_CMD_RUNSTOP:  if (!ipc_ui_cfg_apply(IPC_CFG_RUN,  c.arg)) r.status = 1u; break;
            case IPC_CMD_LOG:      datalog_set_enabled(c.arg ? 1 : 0); break;
            default:               r.status = 1u; break;
        }
        ipc_ring_resp_push(resp, &r);   /* pri plnem resp ringu se odpoved zahodi (CM4 si vyzada znovu) */
        handled++;
    }
    return handled;
}

/* Produkcni servis: kopie g_meas_cfg -> local, aplikace CFG_SET, commit JEN pri realne
 * zmene (jinak by 100Hz commit klobrcoval soubezne zapisy UI/USB SCPI do g_meas_cfg).
 * Zbytkovy race (UI zmeni mezi kopii a commitem) je stejny jako u scpi.c SET — vzacny,
 * config je user-driven. NULL_ACQ dostane realny kmitocet z FPGA. */
int ipc_service(void)
{
    /* ⚠️ Fast-path: prazdny cmd ring = bezna cesta (100 Hz z defaultTasku, CM4 posila
     * prikazy zridka) -> okamzity return. Bez toho by se KAZDY tik delala 2× kopie
     * g_meas_cfg + fpga_freq_get_last (oboji IRQ-off) + memcmp naprazdno. SPSC empty
     * check (head==tail) je jen porovnani dvou volatile citacu; pripadny stale head
     * jen o tik zpozdi zpracovani (pop uvnitr ma spravne DMB). */
    if (g_ipc.cmd.head == g_ipc.cmd.tail) return 0;

    meas_cfg_t before, cfg;
    taskENTER_CRITICAL(); before = cfg = g_meas_cfg; taskEXIT_CRITICAL();
    double fhz = 0.0; (void)ipc_real_freq_hz(&fhz);
    int n = ipc_service_rings(&g_ipc.cmd, &g_ipc.resp, &cfg, fhz);
    if (memcmp(&cfg, &before, sizeof cfg) != 0) {   /* commit jen kdyz CFG_SET neco zmenil */
        taskENTER_CRITICAL(); g_meas_cfg = cfg; taskEXIT_CRITICAL();
    }
    return n;
}

/* ── Liveness CM4: heartbeat ve snapshotu CM4 roste ~1/s. @return 1 = zivy
 * (pokrocil za posledni ~3 s). Bez beziciho CM4 vraci vzdy 0 (spravne). */
int ipc_cm4_alive(void)
{
    static uint32_t s_last_hb, s_last_ms;
    uint32_t hb  = g_ipc.cm4.heartbeat;
    uint32_t now = osKernelGetTickCount();
    if (g_ipc.cm4.magic != IPC_MAGIC) return 0;   /* CM4 jeste nezapsal magic */
    if (hb != s_last_hb) { s_last_hb = hb; s_last_ms = now; return 1; }
    return (now - s_last_ms) < 3000u;
}

/* ── CM4 vlastni zatez [%] z heartbeatu (pro "CM4:xx%" v headeru). Bez CM4 (magic
 * nezapsan) vraci 0. Clamp 0..100. */
uint32_t ipc_cm4_cpu_pct(void)
{
    if (g_ipc.cm4.magic != IPC_MAGIC) return 0u;
    uint32_t p = g_ipc.cm4.cm4_cpu_pct;
    return (p > 100u) ? 100u : p;
}

/* ── Stav ETH linky z CM4 (v5, F1). @return 1 = link UP. Vyplni volitelne
 * speed [Mbps], duplex (0=half/1=full) a IP (oktety a.b.c.d v uint32). Bez zapsaneho
 * CM4 magicu vraci 0 a nuluje vystupy (degradovane "NET: down"). */
int ipc_cm4_net(uint8_t *speed_mbps, uint8_t *duplex, uint32_t *ip)
{
    if (g_ipc.cm4.magic != IPC_MAGIC) {
        if (speed_mbps) *speed_mbps = 0;
        if (duplex)     *duplex     = 0;
        if (ip)         *ip         = 0;
        return 0;
    }
    if (speed_mbps) *speed_mbps = g_ipc.cm4.net_speed_mbps;
    if (duplex)     *duplex     = g_ipc.cm4.net_duplex;
    if (ip)         *ip         = g_ipc.cm4.net_ip;
    return g_ipc.cm4.net_link ? 1 : 0;
}

/* ── Vysledek ETH bring-upu na CM4 (v6, F3). @return 1 = HAL_ETH_Init proslo.
 * `phy_id` = PHYID1<<16|PHYID2 (LAN8742A 0x0007C131), 0 = neprecteno.
 * Stejna degradace jako ipc_cm4_net: bez CM4 magicu 0 + vynulovany vystup.
 * ⚠️ init_ok == 0 znamena "CM4 se k MDIO nedostala" (typicky stoji RMII REF_CLK),
 * NE "PHY mlci" — PHY ID se cte az po uspesnem initu (nastaveny MDIO CSR clock). */
int ipc_cm4_eth(uint32_t *phy_id)
{
    if (g_ipc.cm4.magic != IPC_MAGIC) {
        if (phy_id) *phy_id = 0;
        return 0;
    }
    if (phy_id) *phy_id = g_ipc.cm4.eth_phy_id;
    return g_ipc.cm4.eth_init_ok ? 1 : 0;
}

/* ── IPC_VERSION obrazu CM4 (v6). 0 = CM4 nezapsala magic, nebo bezi starsi obraz,
 * ktery verzi nehlasi. Existuje proto, ze nesoulad bank byl do ted TICHY: CM4 pri
 * neshode jen prestane cist snapshot, ale heartbeat publikuje dal -> `ipc_cm4_alive()`
 * hlasi ZIVOU CM4 a v headeru svití "4:xx%", takze to vypada v poradku. */
uint8_t ipc_cm4_ipc_version(void)
{
    if (g_ipc.cm4.magic != IPC_MAGIC) return 0u;
    return g_ipc.cm4.cm4_ipc_version;
}

/* ── Vysledek `scpi_selftest()` na CM4 (v7, W2). Bez ziveho CM4 vraci 0 (stejna
 * degradace jako ostatni ipc_cm4_* gettery). */
uint8_t ipc_cm4_scpi_selftest(void)
{
    if (g_ipc.cm4.magic != IPC_MAGIC) return 0u;
    return g_ipc.cm4.scpi_selftest_ok;
}

/* ── Vysledek `httpd_min_selftest()` na CM4 (v9, W4). Stejna degradace. */
uint8_t ipc_cm4_httpd_selftest(void)
{
    if (g_ipc.cm4.magic != IPC_MAGIC) return 0u;
    return g_ipc.cm4.httpd_selftest_ok;
}

/* ── Pure-logic selftest: seqlock parita + cteni-retry + cmd/resp ring
 * (push/pop/wrap/full/empty) + servis dispatch. Bezi nad LOKALNI instanci `t`
 * (static → nezatezuje stack malych tasku), takze NEsaha na zivy g_ipc → zadny
 * soubeh s ipc_publish/ipc_service z defaultTasku, kdyz se selftest spusti za
 * behu (UART "selftest"). Neni reentrantni (run_selftests je serializovany). */
int ipc_selftest(void)
{
    static ipc_shared_t t;   /* lokalni IPC instance (bss, ~0,5 kB), NE g_ipc */
    int ok = 1;

    /* Razitko: magic/verze/velikost + seq sude (konzistentni). */
    ipc_stamp(&t);
    ok &= (t.snap.magic == IPC_MAGIC);
    ok &= (t.snap.version == (uint16_t)IPC_VERSION);
    ok &= (t.snap.size == (uint16_t)sizeof(ipc_snapshot_t));
    ok &= ((t.snap.seq & 1u) == 0u);

    /* Seqlock parita: begin -> liche, end -> sude a +2. */
    uint32_t s0 = t.snap.seq;
    ipc_snap_wr_begin(&t.snap);
    ok &= (t.snap.seq & 1u);                       /* probiha zapis */
    ipc_snap_wr_end(&t.snap);
    ok &= ((t.snap.seq & 1u) == 0u) && (t.snap.seq == s0 + 2u);

    /* Ctenarsky protokol: mimo zapis nedava retry; rozecteny zapis (liche) ano. */
    { uint32_t s = ipc_snap_rd_begin(&t.snap); ok &= (ipc_snap_rd_retry(&t.snap, s) == 0); }
    ipc_snap_wr_begin(&t.snap);
    { uint32_t s = ipc_snap_rd_begin(&t.snap); ok &= (ipc_snap_rd_retry(&t.snap, s) != 0); }
    ipc_snap_wr_end(&t.snap);

    /* cmd ring: naplnit pres kapacitu -> drzi presne IPC_RING_N, FIFO poradi. */
    ipc_stamp(&t);
    ipc_cmd_t c = { .type = IPC_CMD_NOP, .key = 0, .id = 0, .arg = 0, .argd = 0 };
    int pushed = 0;
    for (int i = 0; i < IPC_RING_N + 3; i++) { c.id = (uint16_t)i; if (ipc_ring_cmd_push(&t.cmd, &c)) pushed++; }
    ok &= (pushed == IPC_RING_N);
    ipc_cmd_t o; int popped = 0, fifo = 1;
    while (ipc_ring_cmd_pop(&t.cmd, &o)) { if (o.id != (uint16_t)popped) fifo = 0; popped++; }
    ok &= (popped == IPC_RING_N) && fifo;
    ok &= (ipc_ring_cmd_pop(&t.cmd, &o) == 0);     /* prazdno */

    /* wrap pres hranici: push+pop opakovane posune head/tail za IPC_RING_N. */
    for (int i = 0; i < IPC_RING_N * 3; i++) {
        c.id = (uint16_t)(1000 + i);
        ok &= ipc_ring_cmd_push(&t.cmd, &c);
        ok &= ipc_ring_cmd_pop(&t.cmd, &o) && (o.id == (uint16_t)(1000 + i));
    }

    /* resp ring symetricky. */
    ipc_resp_t r = { .id = 7, .status = 0, ._pad = 0, .value = 42 }, rr;
    ok &= ipc_ring_resp_push(&t.resp, &r);
    ok &= ipc_ring_resp_pop(&t.resp, &rr) && rr.id == 7 && rr.value == 42;
    ok &= (ipc_ring_resp_pop(&t.resp, &rr) == 0);

    /* servis nad lokalni cfg: NOP echo, CFG_SET aplikace, neznamy typ status=1. */
    ipc_stamp(&t);
    meas_cfg_t tc; meas_math_defaults(&tc);
    ipc_cmd_t nop = { .type = IPC_CMD_NOP, .key = 0, .id = 55, .arg = 9, .argd = 0 };
    ipc_ring_cmd_push(&t.cmd, &nop);
    ok &= (ipc_service_rings(&t.cmd, &t.resp, &tc, 1e7) == 1);
    ipc_resp_t sr;
    ok &= ipc_ring_resp_pop(&t.resp, &sr) && sr.id == 55 && sr.status == 0 && sr.value == 9;
    /* CFG_SET M=2 pres ring -> aplikuje se na lokalni cfg. */
    ipc_cmd_t cs = { .type = IPC_CMD_CFG_SET, .key = IPC_CFG_MATH_M, .id = 56, .arg = 0, .argd = 2.0 };
    ipc_ring_cmd_push(&t.cmd, &cs);
    ipc_service_rings(&t.cmd, &t.resp, &tc, 1e7);
    ok &= ipc_ring_resp_pop(&t.resp, &sr) && sr.id == 56 && sr.status == 0;
    ok &= (tc.m > 1.99 && tc.m < 2.01);
    ipc_cmd_t bad = { .type = 0xFE, .key = 0, .id = 57, .arg = 0, .argd = 0 };
    ipc_ring_cmd_push(&t.cmd, &bad);
    ipc_service_rings(&t.cmd, &t.resp, &tc, 1e7);
    ok &= ipc_ring_resp_pop(&t.resp, &sr) && sr.id == 57 && sr.status == 1;

    /* ipc_cfg_apply primo — vsechny klice + edge (neznamy klic, NULL_ACQ bez/s freq). */
    {
        meas_cfg_t c2; meas_math_defaults(&c2);
        ok &= (ipc_cfg_apply(&c2, IPC_CFG_MATH_M, 0, 2.0, 0) == 1 && c2.m > 1.99 && c2.m < 2.01);
        ok &= (ipc_cfg_apply(&c2, IPC_CFG_MATH_B, 0, 100.0, 0) == 1 && c2.b > 99.9 && c2.b < 100.1);
        ok &= (ipc_cfg_apply(&c2, IPC_CFG_MATH_EN, 1, 0, 0) == 1 && c2.math_en == 1);
        ok &= (ipc_cfg_apply(&c2, IPC_CFG_LIM_LO, 0, 9.9e6, 0) == 1 && c2.lo > 9.8e6 && c2.lo < 10.0e6);
        ok &= (ipc_cfg_apply(&c2, IPC_CFG_LIM_HI, 0, 1.011e7, 0) == 1 && c2.hi > 1.010e7);
        ok &= (ipc_cfg_apply(&c2, IPC_CFG_LIM_EN, 1, 0, 0) == 1 && c2.limit_en == 1);
        ok &= (ipc_cfg_apply(&c2, 0xEE, 0, 0, 0) == 0);                      /* neznamy klic */
        ok &= (ipc_cfg_apply(&c2, IPC_CFG_NULL_ACQ, 0, 0, 0.0) == 0);        /* bez platneho freq */
        ok &= (ipc_cfg_apply(&c2, IPC_CFG_NULL_ACQ, 0, 0, 1e7) == 1 && c2.null_en == 1);  /* s freq */
    }

    return ok;
}

/* ══════════════ SCPI nad IPC snapshotem (priprava CM4 backendu, #25) ═════════
 * Naplni `scpi_src_t` VYHRADNE ze snapshotu — presne to, co bude delat CM4.
 * Viz komentar u deklarace v ipc_shared.h.
 *
 * ⚠️ Bity platnosti se prenaseji PRIMYM prirazenim (`valid = sens_valid`), ne
 * prekladem — SCPI_V_* a IPC_V_* maji ZAMERNE shodne pozice a hlidaji to
 * staticke asserty nize. Kdyby se rozesly, preklad by neprosel.
 *
 * ⚠️ Co snapshot NEMA, zustava nulove/neplatne — a to je spravne: lepe "nevim"
 * nez vymysleny udaj. Konkretne datalog (MMEM:*) a `selftest_pass` snapshot
 * dnes nenese, takze CM4 na ne odpovi prazdno; az to bude potreba, doplni se do
 * snapshotu s bumpem IPC_VERSION (dnes by to byla mrtva vaha). */
_Static_assert((int)SCPI_V_FREQ    == (int)IPC_V_FREQ,    "SCPI/IPC bit FREQ se rozesel");
_Static_assert((int)SCPI_V_DIV16   == (int)IPC_V_DIV16,   "SCPI/IPC bit DIV16 se rozesel");
_Static_assert((int)SCPI_V_FRAME   == (int)IPC_V_FRAME,   "SCPI/IPC bit FRAME se rozesel");
_Static_assert((int)SCPI_V_T_OCXO  == (int)IPC_V_T_OCXO,  "SCPI/IPC bit T_OCXO se rozesel");
_Static_assert((int)SCPI_V_T_BOARD == (int)IPC_V_T_BOARD, "SCPI/IPC bit T_BOARD se rozesel");
_Static_assert((int)SCPI_V_T_MCU   == (int)IPC_V_T_MCU,   "SCPI/IPC bit T_MCU se rozesel");
_Static_assert((int)SCPI_V_T_FPGA  == (int)IPC_V_T_FPGA,  "SCPI/IPC bit T_FPGA se rozesel");
_Static_assert((int)SCPI_V_VC      == (int)IPC_V_VC,      "SCPI/IPC bit VC se rozesel");
_Static_assert((int)SCPI_V_RF      == (int)IPC_V_RF,      "SCPI/IPC bit RF se rozesel");
_Static_assert((int)SCPI_V_V12     == (int)IPC_V_V12,     "SCPI/IPC bit V12 se rozesel");
_Static_assert((int)SCPI_V_V5      == (int)IPC_V_V5,      "SCPI/IPC bit V5 se rozesel");
_Static_assert((int)SCPI_V_VREF    == (int)IPC_V_VREF,    "SCPI/IPC bit VREF se rozesel");
_Static_assert((int)SCPI_V_VBAT    == (int)IPC_V_VBAT,    "SCPI/IPC bit VBAT se rozesel");
_Static_assert((int)SCPI_V_GPS     == (int)IPC_V_GPS,     "SCPI/IPC bit GPS se rozesel");

/* ⚠️ `ipc_scpi_src_from_snap` se PRESTEHOVALA do `ipc_scpi.c` — je to cista funkce
 * (snapshot -> scpi_src_t) a linkuje se do OBOU jader, aby CM4 i testovaci cesta
 * `scpi ipc` na CM7 pouzivaly doslova tentyz kod a nemohly se rozejit. */
