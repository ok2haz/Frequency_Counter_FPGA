/**
 * @file    ipc.c
 * @brief   CM7 strana IPC (STATUS.md #19/#20): init + publikace snapshotu do
 *          SRAM4 (seqlock) + servis cmd ringu + pure-logic selftest.
 *
 * Protokol a datove struktury jsou v `ipc_shared.h` (ZDROJ PRAVDY pro OBA jadra);
 * tady je CM7-strana logiky, ktera je HW-nezavisla a bezi uz ted, jeste nez
 * nabehne CM4.
 *
 * ⚠️ STAV DVOUJADRA: CM4 (domena D2, bank2) zatim NEBEZI zamerne — `g_cm4_absent`
 * (viz freertos_shared.h). Publisher presto bezi na CM7: SRAM4 je non-cacheable
 * (MPU region 2, `main.c`) + vyhrazena linkerem (sekce `.ipc_shared` @RAM_D3), takze
 * snapshot je konzistentne pripraveny a jakmile se CM4 flashne, zacne ho cist BEZ
 * jakekoli zmeny na CM7. Cmd/resp servis je zatim skeleton (echo NOP) — realny
 * dispatch SETu (GATE/RUN/CHAN/LOG) dozraje s CM4 bring-upem, kdy ho lze otestovat.
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
#include "freertos_shared.h"  /* g_spi_ok, g_freq_stale, g_si5356_*, g_ui_cfg, g_uptime_s, ... */
#include "cmsis_os2.h"        /* osKernelGetTickCount — throttle bez HAL zavislosti */
#include <string.h>

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
    static uint32_t s_last_ms;
    uint32_t now = osKernelGetTickCount();
    if ((now - s_last_ms) < 500u) return;        /* ~2 Hz */
    s_last_ms = now;

    /* Posbirej realny stav MIMO seqlock (kratke drzeni licheho seq = min. retry CM4). */
    fpga_meas_t m;
    int have_meas = fpga_freq_get_last(&m);
    int meas_ok   = have_meas && (m.measurement_status & 0x01u)
                    && !(m.error_flags & FPGA_ERR_SIGNAL_LOST);

    gps_data_t g; gps_get(&g);

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
    /* rtc_unix: zdroj UTC->unix zije v datalog/rtc vrstve; doplni se s MathTaskem. */

    g_ipc.snap.t_ocxo_c100  = (int16_t)(g_sensors[SENS_T49].last * 100.0f);
    g_ipc.snap.t_board_c100 = (int16_t)(g_sensors[SENS_T48].last * 100.0f);
    g_ipc.snap.ocxo_vc_mv   = (uint16_t)(g_sensors[SENS_ADS0].valid ? g_sensors[SENS_ADS0].last : 0.0f);
    g_ipc.snap.rf_mv        = (uint16_t)(g_sensors[SENS_ADS1].valid ? g_sensors[SENS_ADS1].last : 0.0f);

    g_ipc.snap.flags        = flags;
    g_ipc.snap.sys_level    = sysl;
    g_ipc.snap.uptime_s     = g_uptime_s;
    g_ipc.snap.cm7_cpu_pct  = g_rtos_cpu_pct;
    g_ipc.snap.reset_cause  = g_reset_rsr;

    ipc_snap_publish_end();
}

/* ── Servis prikazu z CM4 (cmd ring -> odpoved do resp ringu). Vola defaultTask.
 * ⚠️ Dnes SKELETON: NOP echuje OK; realny dispatch SETu (GATE/RUN/CHAN/LOG) zameni
 * stav vlastneny UiTaskem -> doplni se az s CM4 bring-upem (aby sel otestovat proti
 * zivemu producentovi). Neznamy/zatim neimplementovany prikaz -> status=1. Vraci
 * pocet zpracovanych prikazu (0 = prazdny ring = bezna cesta, kdyz CM4 nebezi). */
/* Jadro servisu nad DANYMI ringy (→ testovatelne na lokalni kopii). */
static int ipc_service_rings(volatile ipc_cmd_ring_t *cmd, volatile ipc_resp_ring_t *resp)
{
    int handled = 0;
    ipc_cmd_t c;
    while (ipc_ring_cmd_pop(cmd, &c)) {
        ipc_resp_t r = { .id = c.id, .status = 0u, ._pad = 0, .value = c.arg };
        switch (c.type) {
            case IPC_CMD_NOP:  break;                 /* zdravi ringu (M5 test) — echo OK */
            default:           r.status = 1u; break;  /* zatim neimplementovano */
        }
        ipc_ring_resp_push(resp, &r);   /* pri plnem resp ringu se odpoved zahodi (CM4 si vyzada znovu) */
        handled++;
    }
    return handled;
}
int ipc_service(void) { return ipc_service_rings(&g_ipc.cmd, &g_ipc.resp); }

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
    ipc_cmd_t c = { .type = IPC_CMD_NOP, ._pad = 0, .id = 0, .arg = 0 };
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

    /* servis: NOP echuje status=0, neznamy typ status=1. */
    ipc_stamp(&t);
    ipc_cmd_t nop = { .type = IPC_CMD_NOP, ._pad = 0, .id = 55, .arg = 9 };
    ipc_ring_cmd_push(&t.cmd, &nop);
    ok &= (ipc_service_rings(&t.cmd, &t.resp) == 1);
    ipc_resp_t sr;
    ok &= ipc_ring_resp_pop(&t.resp, &sr) && sr.id == 55 && sr.status == 0 && sr.value == 9;
    ipc_cmd_t bad = { .type = 0xFE, ._pad = 0, .id = 56, .arg = 0 };
    ipc_ring_cmd_push(&t.cmd, &bad);
    ipc_service_rings(&t.cmd, &t.resp);
    ok &= ipc_ring_resp_pop(&t.resp, &sr) && sr.id == 56 && sr.status == 1;

    return ok;
}
