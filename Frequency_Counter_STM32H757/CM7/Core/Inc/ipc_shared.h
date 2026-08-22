#pragma once
/**
 * @file ipc_shared.h
 * @brief Sdilena pamet + protokol CM7 <-> CM4 (SRAM4 / domena D3, 0x38000000).
 *
 * ZDROJ PRAVDY pro OBA jadra. Bez OpenAMP — jednoduchy auditovatelny protokol
 * (styl FPGA ramce). Ctyri kanaly:
 *   - ipc_snapshot_t  (CM7 -> CM4)  mereni+stav, SEQLOCK (lock-free cteni)
 *   - cmd ring        (CM4 -> CM7)  SCPI/web SETy (GATE, RUN/STOP, CHAN, log)
 *   - resp ring       (CM7 -> CM4)  odpovedi + async eventy
 *   - cm4 status      (CM4 -> CM7)  heartbeat (liveness) + vlastni CPU %
 *
 * ⚠️ UMISTENI: pevna adresa `IPC_BASE` (= zacatek SRAM4). Oba cory sahaji pres
 * makro `g_ipc` -> SHODNA adresa nezavisle na linkeru. Region je NON-CACHEABLE
 * (MPU region 2, `main.c` `MPU_Config`) + vyhrazeny linkerem (sekce `.ipc_shared`
 * @RAM_D3). Bez non-cacheable by cteni videlo stara data z D-cache a IPC by
 * "skoro fungovalo" a padalo nahodne.
 *
 * ⚠️ LAYOUT struktury MUSI byt identicky na obou jadrech — stejny header + ARM
 * EABI zarovnani to zaruci. Pri ZMENE layoutu zvednout `IPC_VERSION` (CM4 overi
 * `magic`+`version`+`size` po bootu; nesouhlas -> IPC vypnout, jet degradovane).
 *
 * ✅ STAV (2026-08-14): OBE strany bezi na HW — CM7 publikuje, CM4 cte a posila
 * heartbeat zpet. Header sdili CM4 pres RELATIVNI include
 * (`../../../CM7/Core/Inc/ipc_shared.h`) — regen-safe, bez zasahu do include path.
 *
 * Migrace: krok M4 zebriku (STATUS.md) je timto splneny.
 */
#include <stdint.h>

#define IPC_BASE     0x38000000u   /* SRAM4 / D3 — viz linker sekce .ipc_shared + MPU region 2 */
#define IPC_MAGIC    0x31435049u   /* "IPC1" (LE) */
#define IPC_VERSION  5u             /* v2: plna sada senzoru+kalibrace; v3 (2026-08-09): Math/limit
                                       cfg mirror ve snapshotu + IPC_CMD_CFG_SET (config sync CM4<->CM7);
                                       v4 (2026-08-13): sens_valid (maska platnosti) + t_fpga_c100;
                                       v5 (2026-08-22, F1): stav ETH linky/IP v ipc_cm4_status_t */

/* ── Maska platnosti hodnot ve snapshotu (`sens_valid`) ──────────────────────
 * ⚠️ Bitove pozice jsou ZAMERNE SHODNE s `SCPI_V_*` (scpi.h), aby CM4 SCPI
 * backend mohl udelat proste `src->valid = snap.sens_valid;` a choval se
 * BIT ZA BIT stejne jako CM7 na USB. Shodu hlida `_Static_assert` v ipc.c —
 * ty dva hlavickove soubory se jinak nepotkaji v jedne translation unit.
 *
 * Duvod existence: do v3 se neplatna napeti publikovala jako 0 a neplatne
 * teploty dokonce jako POSLEDNI DOBRA hodnota (bez priznaku, ze je stara).
 * CM7 pritom na USB vraci `9.91E37`. `MEAS:VOLT?` by tedy pres USB rekl NaN
 * a pres TCP „0.00 V" — dve ruzne pravdy o tomtez pristroji. */
#define IPC_V_FREQ    (1u << 0)   /* platne mereni /4 (CRC+VALID+FRESH+nova SEQ) */
#define IPC_V_DIV16   (1u << 1)   /* platna /16 vetev */
#define IPC_V_FRAME   (1u << 2)   /* existuje posledni DATA ramec (gate/kanal) */
#define IPC_V_T_OCXO  (1u << 3)
#define IPC_V_T_BOARD (1u << 4)
#define IPC_V_T_MCU   (1u << 5)
#define IPC_V_T_FPGA  (1u << 6)
#define IPC_V_VC      (1u << 7)
#define IPC_V_RF      (1u << 8)
#define IPC_V_V12     (1u << 9)
#define IPC_V_V5      (1u << 10)
#define IPC_V_VREF    (1u << 11)
#define IPC_V_VBAT    (1u << 12)
#define IPC_V_GPS     (1u << 13)  /* GPS fix (cas/poloha platne) */

#define IPC_ADEV_PTS 12            /* ADEV bodu ve snapshotu (tau pyramida) */
#define IPC_RING_N   16            /* slotu v cmd/resp ringu — MUSI byt mocnina 2 */

/* ── Snapshot mereni + stavu: CM7 -> CM4 (~0,3 kB). SEQLOCK: `seq` licha = zapis. */
typedef struct {
    uint32_t magic;                 /* IPC_MAGIC — CM4 overi po bootu */
    uint16_t version;               /* IPC_VERSION */
    uint16_t size;                  /* sizeof(ipc_snapshot_t) — sanity check */
    volatile uint32_t seq;          /* seqlock (lichy = probiha zapis) */

    /* Mereni (freq × 1e5, delicka /4 i /16 uz zahrnuta — jako FPGA protokol). */
    uint64_t freq_x100000;          /* zvoleny zdroj (/4 nebo /16) */
    uint64_t freq4_x100000;         /* pin28 /4 */
    uint64_t freq16_x100000;        /* pin27 /16 */
    uint32_t gate_ns;               /* ≈250e6, kolisa */
    uint32_t seq_meas;              /* SEQUENCE posledniho platneho DATA ramce */

    /* Statistika (float — CM4 jen zobrazuje/serviruje, POCITA CM7 (double FPU)). */
    float    sigma_tau[IPC_ADEV_PTS]; /* ADEV σy(τ) body */
    float    tau_s[IPC_ADEV_PTS];     /* odpovidajici τ [s] */
    float    offset;                  /* frakcni offset (f-f0)/f0 */
    float    drift;                   /* drift / den */

    /* GPS. */
    int32_t  gps_lat_e7;            /* stupne × 1e7 */
    int32_t  gps_lon_e7;
    int32_t  gps_alt_cm;
    uint32_t rtc_unix;             /* UTC cas (unix) */
    float    gps_hdop;
    uint8_t  gps_valid;
    uint8_t  gps_fix_mode;         /* 0 / 2 / 3 */
    uint8_t  gps_num_sat;
    uint8_t  _pad_gps;

    /* Senzory — PLNA sada (v2), aby CM4 (SCPI/web) obslouzil dotazy BEZ pristupu ke
     * g_sensors/g_calib (na CM4 nejsou). Teploty × 100 °C, napeti mV, kalibrace RF. */
    float    ad8307_slope_mv_db;   /* AD8307 kalibrace (aby CM4 spocital MEAS:POWer? dBm) */
    float    ad8307_intercept_dbm;
    int16_t  t_ocxo_c100;          /* OCXO (TMP117 0x49) × 100 [°C] */
    int16_t  t_board_c100;         /* STM deska (TMP117 0x48) */
    int16_t  t_mcu_c100;           /* MCU jadro (ADC3) */
    int16_t  t_fpga_c100;          /* FPGA deska (TMP117 0x4A — dnes NEOSAZEN -> bit v sens_valid = 0).
                                    * Do v3 pole chybelo uplne, takze `SYST:TEMP? FPGA` na CM4
                                    * nesla vubec zodpovedet — dalsi rozdil proti USB. */
    uint16_t ocxo_vc_mv;           /* EFC ladici napeti (AIN0) */
    uint16_t rf_mv;                /* RF level SYROVE mV (AD8307, AIN1) */
    uint16_t v_12v_mv;             /* 12V vetev (AIN2, uz po gain) */
    uint16_t v_5v_mv;              /* 5V vetev (AIN3) */
    uint16_t vref_mv;              /* VREF+ ~2,5 V (ADC3) */
    uint16_t vbat_mv;              /* VBAT (ADC3) */
    uint8_t  channel_id;           /* aktivni kanal FPGA */
    uint8_t  si5356_status;        /* Si5356 reg 218 (reference lock: LOS_CLKIN/PLL_LOL) */
    uint8_t  si5356_ok;            /* 1 = status precten */
    uint8_t  _pad_s;
    uint32_t sens_valid;           /* IPC_V_* — KTERE hodnoty vyse jsou platne (v4).
                                    * Hodnota bez nastaveneho bitu je NEPLATNA a nesmi se
                                    * servirovat jako mereni (SCPI -> 9.91E37). */

    /* Zdravi / stav. */
    uint32_t flags;                /* IPC_F_* */
    uint8_t  sys_level;            /* 0=OK 1=warn 2=err (agregace do SYS pilulky) */
    uint8_t  alarm_active;
    uint16_t _pad_h;
    uint32_t uptime_s;
    uint32_t cm7_cpu_pct;
    uint32_t reset_cause;          /* RCC->RSR (raw) */

    /* Math/limit konfigurace (g_meas_cfg mirror — CM4 pro CALC: readback + CALC:DATA?/LIM?).
     * ⚠️ Zapis z CM4 jde OPACNE pres cmd ring (IPC_CMD_CFG_SET -> CM7 aplikuje na g_meas_cfg),
     * projevi se pak tady. Snapshot je z pohledu CM4 READ-ONLY. Odpovida meas_cfg_t (meas_math.h). */
    double   math_m, math_b, null_ref, lim_lo, lim_hi;
    uint8_t  math_en, null_en, limit_en, _pad_cfg;
} ipc_snapshot_t;

/* ── Prikaz CM4 -> CM7 + odpoved CM7 -> CM4. */
typedef struct {
    uint8_t  type;                 /* IPC_CMD_* */
    uint8_t  key;                  /* pro IPC_CMD_CFG_SET: ktere pole (IPC_CFG_*) */
    uint16_t id;                   /* pro parovani s odpovedi */
    uint32_t arg;                  /* celociselny arg (GATE index / CHAN / bool 0|1) */
    double   argd;                 /* double arg (config: m/b/lo/hi) — cely rozsah kmitoctu */
} ipc_cmd_t;

typedef struct {
    uint16_t id;                   /* echo id prikazu */
    uint8_t  status;               /* 0=OK, jinak chybovy kod */
    uint8_t  _pad;
    uint32_t value;                /* navratova hodnota */
} ipc_resp_t;

/* SPSC ring (jeden producent, jeden konzument). head/tail = volne bezici citace. */
typedef struct { volatile uint32_t head, tail; ipc_cmd_t  slot[IPC_RING_N]; } ipc_cmd_ring_t;
typedef struct { volatile uint32_t head, tail; ipc_resp_t slot[IPC_RING_N]; } ipc_resp_ring_t;

/* ── CM4 -> CM7: heartbeat (liveness) + vlastni zatez (pro "CM4:xx%" v headeru). */
typedef struct {
    uint32_t magic;                /* IPC_MAGIC — potvrdi, ze CM4 opravdu zapisuje */
    volatile uint32_t heartbeat;   /* CM4 inkrementuje ~1/s; CM7 hlida stari (liveness) */
    uint32_t cm4_cpu_pct;          /* CM4 idle-based zatez [%] */
    uint32_t cm4_uptime_s;
    /* v5 (F1 ETH, 2026-08-22): stav ETH linky (CM4 -> CM7). Dnes CM4 hlasi natvrdo
     * down/0 (lwIP prijde az F5); zobrazovaci retez (System Health) se ladi uz tady. */
    uint32_t net_ip;               /* IPv4 jako oktety: bajt0=a .. bajt3=d (a.b.c.d); 0 = zadna IP */
    uint8_t  net_link;             /* 0 = down, 1 = up */
    uint8_t  net_speed_mbps;       /* 10 / 100 / 0 (neznamo) */
    uint8_t  net_duplex;           /* 0 = half, 1 = full */
    uint8_t  net_rsvd;             /* zarovnani na 4 */
} ipc_cm4_status_t;

/* ── Cela sdilena struktura (musi se vejit do 64 KB SRAM4). */
typedef struct {
    ipc_snapshot_t   snap;         /* CM7 -> CM4 */
    ipc_cmd_ring_t   cmd;          /* CM4 -> CM7 */
    ipc_resp_ring_t  resp;         /* CM7 -> CM4 */
    ipc_cm4_status_t cm4;          /* CM4 -> CM7 */
} ipc_shared_t;

_Static_assert(sizeof(ipc_shared_t) <= 65536, "IPC struktura se nevejde do SRAM4 (64 KB)");

/* Pristup k pevne adrese (oba cory -> shodne). */
#define g_ipc (*(volatile ipc_shared_t *)IPC_BASE)

/* ── Bity `flags`. */
#define IPC_F_FPGA_LINK    (1u << 0)   /* SPI link ziva */
#define IPC_F_SIGNAL_LOST  (1u << 1)   /* FPGA SIGNAL_LOST */
#define IPC_F_DIV16_ACTIVE (1u << 2)   /* zobrazeny zdroj = /16 */
#define IPC_F_GPS_VALID    (1u << 3)
#define IPC_F_HOLDOVER     (1u << 4)
#define IPC_F_SI5356_LOS   (1u << 5)   /* ztrata 10 MHz reference (bit3 reg218) */
#define IPC_F_DATALOG_ON   (1u << 6)
#define IPC_F_RUNNING      (1u << 7)   /* mereni bezi (RUN) */

/* ── Typy prikazu (CM4 -> CM7). */
enum {
    IPC_CMD_NOP = 0,   /* zdravi ringu (test M5) — CM7 jen odpovi echo */
    IPC_CMD_GATE,      /* arg = index 0..3 (0,1 / 1 / 10 / 100 s) */
    IPC_CMD_RUNSTOP,   /* arg = 0 stop / 1 run */
    IPC_CMD_CHAN,      /* arg = kanal */
    IPC_CMD_LOG,       /* arg = 0 off / 1 on (datalog) */
    IPC_CMD_CFG_SET,   /* Math/limit config-set: key=IPC_CFG_*, hodnota v arg (bool) nebo argd (double) */
};

/* ── Klice pro IPC_CMD_CFG_SET (config sync Math/limity, CM4 SCPI/web -> g_meas_cfg na CM7).
 * CM7 aplikuje pres ipc_cfg_apply (mirror scpi.c CALC SET). Cteni zpet = snapshot cfg mirror. */
enum {
    IPC_CFG_MATH_EN = 0,  /* arg 0/1 */
    IPC_CFG_MATH_M,       /* argd */
    IPC_CFG_MATH_B,       /* argd */
    IPC_CFG_NULL_EN,      /* arg 0/1 */
    IPC_CFG_NULL_ACQ,     /* akce: zachyt aktualni kmitocet jako null_ref (potrebuje platne mereni) */
    IPC_CFG_LIM_EN,       /* arg 0/1 */
    IPC_CFG_LIM_LO,       /* argd */
    IPC_CFG_LIM_HI,       /* argd */
    /* Instrument SET (2026-08-15) — stav mereni, ne Math. ⚠️ Poradi 1:1 se `SCPI_CFG_*`.
     * Rozsireni VYCTU nemeni layout `ipc_cmd_t` (klic je uint8_t), takze `IPC_VERSION`
     * se NEZVYSUJE: stara CM4 nove klice neposila a nova CM7 jim rozumi. */
    IPC_CFG_GATE,         /* arg = index brany 0..3 */
    IPC_CFG_CHAN,         /* arg = kanal 0/1 */
    IPC_CFG_RUN,          /* arg = 0 STOP / 1 RUN */
};

/* ── Pametova bariera (core-agnostic; funguje na CM7 i CM4, bez CMSIS zavislosti). */
#ifndef IPC_DMB
#define IPC_DMB() __asm volatile ("dmb 0xF" ::: "memory")
#endif

/* ── SEQLOCK jadro — pracuje nad DANYM snapshotem (ne jen g_ipc) → testovatelne
 * na lokalni kopii bez sdileneho stavu. Publikace JEN CM7, cteni JEN CM4. */
static inline void ipc_snap_wr_begin(volatile ipc_snapshot_t *s) { s->seq++; IPC_DMB(); }
static inline void ipc_snap_wr_end  (volatile ipc_snapshot_t *s) { IPC_DMB(); s->seq++; }
static inline uint32_t ipc_snap_rd_begin(volatile ipc_snapshot_t *s)             { uint32_t v = s->seq; IPC_DMB(); return v; }
static inline int      ipc_snap_rd_retry(volatile ipc_snapshot_t *s, uint32_t v) { IPC_DMB(); return (v & 1u) || (v != s->seq); }

/* ── SPSC ring jadro (nad danym ringem). Vraci 1 = uspech. */
static inline int ipc_ring_cmd_push(volatile ipc_cmd_ring_t *rg, const ipc_cmd_t *c) {
    uint32_t h = rg->head;
    if ((h - rg->tail) >= IPC_RING_N) return 0;            /* plno */
    rg->slot[h & (IPC_RING_N - 1u)] = *c;
    IPC_DMB(); rg->head = h + 1u;                          /* zverejni az PO zapisu slotu */
    return 1;
}
static inline int ipc_ring_cmd_pop(volatile ipc_cmd_ring_t *rg, ipc_cmd_t *c) {
    uint32_t t = rg->tail;
    if (rg->head == t) return 0;                           /* prazdno */
    IPC_DMB(); *c = rg->slot[t & (IPC_RING_N - 1u)];
    IPC_DMB(); rg->tail = t + 1u;
    return 1;
}
static inline int ipc_ring_resp_push(volatile ipc_resp_ring_t *rg, const ipc_resp_t *r) {
    uint32_t h = rg->head;
    if ((h - rg->tail) >= IPC_RING_N) return 0;
    rg->slot[h & (IPC_RING_N - 1u)] = *r;
    IPC_DMB(); rg->head = h + 1u;
    return 1;
}
static inline int ipc_ring_resp_pop(volatile ipc_resp_ring_t *rg, ipc_resp_t *r) {
    uint32_t t = rg->tail;
    if (rg->head == t) return 0;
    IPC_DMB(); *r = rg->slot[t & (IPC_RING_N - 1u)];
    IPC_DMB(); rg->tail = t + 1u;
    return 1;
}

/* ── g_ipc-vazane zkratky pro PRODUKCNI kod (call-sites beze zmeny).
 *   SEQLOCK PUBLIKACE (JEN CM7): ipc_snap_publish_begin(); ...zapis poli...; ipc_snap_publish_end();
 *   SEQLOCK CTENI (JEN CM4): do { s = ipc_snap_read_begin(); local = g_ipc.snap; } while (ipc_snap_read_retry(s));
 *   cmd ring: CM4 push / CM7 pop.  resp ring: CM7 push / CM4 pop. */
static inline void ipc_snap_publish_begin(void) { ipc_snap_wr_begin(&g_ipc.snap); }
static inline void ipc_snap_publish_end(void)   { ipc_snap_wr_end(&g_ipc.snap); }
static inline uint32_t ipc_snap_read_begin(void)       { return ipc_snap_rd_begin(&g_ipc.snap); }
static inline int      ipc_snap_read_retry(uint32_t s) { return ipc_snap_rd_retry(&g_ipc.snap, s); }
static inline int ipc_cmd_push(const ipc_cmd_t *c)   { return ipc_ring_cmd_push(&g_ipc.cmd, c); }
static inline int ipc_cmd_pop(ipc_cmd_t *c)          { return ipc_ring_cmd_pop(&g_ipc.cmd, c); }
static inline int ipc_resp_push(const ipc_resp_t *r) { return ipc_ring_resp_push(&g_ipc.resp, r); }
static inline int ipc_resp_pop(ipc_resp_t *r)        { return ipc_ring_resp_pop(&g_ipc.resp, r); }

/* ── CM7-strana IPC (ipc.c). CM4 si implementuje vlastni konzumenta; tyto
 * funkce bezi na CM7. Viz ipc.c. */
#ifdef __cplusplus
extern "C" {
#endif
void ipc_init(void);        /* orazitkuj snapshot + vynuluj ringy (1x pri bootu, pred publikaci) */
void ipc_publish(void);     /* CM7 -> CM4 snapshot pres seqlock (throttle ~2 Hz uvnitr) */
int  ipc_service(void);     /* zpracuj cmd ring -> resp ring; @return pocet prikazu */
int  ipc_cm4_alive(void);   /* 1 = CM4 heartbeat ziva (< ~3 s); bez CM4 vraci 0 */
uint32_t ipc_cm4_cpu_pct(void); /* CM4 vlastni zatez [%] z heartbeatu (0..100); 0 bez CM4 */
int  ipc_cm4_net(uint8_t *speed_mbps, uint8_t *duplex, uint32_t *ip); /* 1=link UP, ETH stav z CM4 (v5,F1) */
int  ipc_selftest(void);    /* pure-logic: seqlock parita + ring push/pop/wrap; 1 = PASS */

/* ── CM4 -> CM7: publikace stavu ETH linky (v5, F1). Vola CM4 (dnes natvrdo down,
 * po lwIP realne). speed_mbps=10/100/0, duplex 0=half/1=full, ip=oktety a.b.c.d. */
void ipc_cm4_set_net(uint8_t link_up, uint8_t speed_mbps, uint8_t duplex, uint32_t ip);

/* ── SCPI nad IPC snapshotem (priprava CM4 backendu, #25) ────────────────────
 * Naplni `scpi_src_t` VYHRADNE z IPC snapshotu — presne to, co bude delat CM4,
 * az na nem SCPI pojede pres TCP. Deklarace je `void *`, aby `ipc_shared.h`
 * nemusel tahnout `scpi.h` (a naopak) — implementace v `ipc.c`, kde jsou oba.
 *
 * ⚠️ SMYSL: nejvetsi riziko TCP poloviny #25 neni socket, ale otazka "nese
 * snapshot vsechno, co SCPI potrebuje, a sedi bity platnosti?". Tohle se da
 * overit UZ TED na CM7 — `scpi ipc <cmd>` proti `scpi <cmd>` musi dat SHODNOU
 * odpoved. Staticke asserty hlidaji, ze SCPI_V_* == IPC_V_*, ale runtime dukaz
 * do ted neexistoval.
 *
 * @param snap  ukazatel na `ipc_snapshot_t` (typicky prectena kopie).
 * @return 1 = snapshot vypada platne (magic/verze), 0 = nepouzitelny. */
int ipc_scpi_src_from_snap(void *src_out, const void *snap);
#ifdef __cplusplus
}
#endif
