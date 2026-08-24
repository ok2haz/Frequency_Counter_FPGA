#pragma once
/**
 * @file    scpi.h
 * @brief   SCPI-99 parser + dispatch (STATUS.md #25) — transportně nezávislý.
 *
 * `scpi_process()` zpracuje JEDNU programovou zprávu (bez CRLF; smí obsahovat
 * víc jednotek oddělených ';') a zapíše odpověď do `out`. Nezná transport →
 * stejné jádro pro **USB CDC konzoli hned** i **TCP 5025 na CM4 později**.
 * Idiom projektu: pure-logic + selftest na targetu (`scpi_selftest`).
 *
 * Podporuje SCPI krátkou/dlouhou formu (MEAS ↔ MEASure), case-insensitive,
 * dvojtečkovou hierarchii (SYSTem:ERRor?), `?` dotazy a **složené zprávy**
 * (jednotky oddělené `;`, IEEE 488.2 — odpovědi dotazů spojené `;`).
 *
 * IEEE 488.2 common commands:
 *   *IDN?  *OPC  *OPC?  *WAI  *TST?  *RST  *CLS
 *   *ESR?  *ESE <m>|*ESE?  *SRE <m>|*SRE?  *STB?   (minimální status model)
 *
 * Podporované příkazy (dotazy vrací hodnotu, akce nic):
 *   SYSTem:VERSion?  SYSTem:UPTime?  SYSTem:ERRor?(=…:NEXT?)  SYSTem:ERRor:COUNt?
 *   SYSTem:TEMPerature? [OCXO|BOARD|MCU|FPGA]  (default OCXO 0x49)
 *   SYSTem:GPS:STATus?  SYSTem:GPS:TIME?  SYSTem:GPS:POSition?
 *   MEASure:FREQuency?  (=FETCh:FREQuency?)  :DIV16?  :ALL?  (reálný /4, /16, oba)
 *   MEASure:VOLTage? [P12|P5|VC|VREF|VBAT]  (default P12)   MEASure:POWer?  (RF dBm)
 *   SENSe:FREQuency:GATE?  SENSe:FREQuency:CHANnel?          (poslední FPGA rámec)
 *   MMEMory:CATalog?  MMEMory:DATA:COUNt?  MMEMory:DATA? <n> (datalog: souhrn/počet/n-tý záznam)
 *   CALCulate:DATA?  CALCulate:LIMit:FAIL?         (Math Mx+B/NULL + limit, viz meas_math)
 *   STATus:OPERation:CONDition?  STATus:QUEStionable:CONDition?
 *
 * SET příkazy (akce, bez výstupu; každý má i `?` dotaz na readback):
 *   CALCulate:MATH:STATe ON|OFF   CALCulate:MATH:M <num>   CALCulate:MATH:B <num>
 *   CALCulate:NULL:STATe ON|OFF   CALCulate:NULL:ACQuire   (zachytí referenci z živého X)
 *   CALCulate:LIMit:STATe ON|OFF  CALCulate:LIMit:LOWer <hz>   CALCulate:LIMit:UPPer <hz>
 *   Argumenty: číslo (`1e6`, `-2.5`, `1.5E-3`, i s jednotkou `10.1MHZ`/`100KHZ`/`1GHZ`)
 *   nebo bool (`ON/OFF/1/0`). Chybný arg → −224.
 *   ⚠️ SET zapisují `g_meas_cfg` z UartTasku → commit celé cfg pod krátkou kritickou
 *   sekcí (UiTask nikdy nevidí roztržený double).
 *
 * ⚠️ **Chybová fronta** (`SYSTem:ERRor?`) + status registry jsou PER-SESSION
 * (`scpi_ctx_t`) — každý transport má vlastní kontext, takže USB CDC a budoucí
 * souběžný TCP 5025 na CM4 mají nezávislé fronty i status. `scpi_process` bez ctx
 * používá jediný sdílený default (USB konzole); TCP volá `scpi_process_ctx` s
 * vlastním kontextem per spojení. `*CLS` frontu i status daného ctx maže.
 *
 * ⚠️ **Zlaté pravidlo (STATUS.md):** `MEASure:FREQuency?` vrací REÁLNÝ FPGA
 * kmitočet (`fpga_freq_get_last`), NE simulovaný headline. Bez platného měření
 * (link down / SIGNAL_LOST) vrací SCPI „not-a-number" `9.91E37`. Ostatní dotazy
 * (teploty, GPS, stav) vracejí reálná data z `g_sensors`/`gps_get`.
 *
 * ⚠️ Žádný `%f`/`%E` — čísla přes celočíselnou extrakci (float-printf je v nano
 * newlibu vypnutý, viz fmt_hz v app vrstvě).
 */
#include <stddef.h>
#include <stdint.h>
#include "meas_math.h"   /* meas_cfg_t — scpi_src_t.meas (CALC subsystem) */
#include "datalog.h"     /* datalog_rec_t — read_log (MMEM:DATA?) */

#ifdef __cplusplus
extern "C" {
#endif

/** Kontext JEDNÉ SCPI session: chybová fronta + IEEE 488.2 status registry.
 *  Každý transport (USB CDC teď, TCP 5025 na CM4 pak) má vlastní instanci →
 *  nezávislé fronty i status. Zeroed = čistý stav (viz scpi_ctx_init). */
#define SCPI_ERRQ_N 8
typedef struct {
    int      err_q[SCPI_ERRQ_N];   /* kruhová fronta chybových kódů */
    uint8_t  err_head, err_count;
    uint8_t  esr;                  /* Standard Event Status Register (latched) */
    uint8_t  ese;                  /* Event Status Enable (*ESE) */
    uint8_t  sre;                  /* Service Request Enable (*SRE) */
    /* ── SCPI-99 OPERation / QUEStionable (doplneno 2026-08-15) ─────────────────
     * `:CONDition` = OKAMZITY stav (pocita se ze zdroje pri kazdem dotazu).
     * `:EVENt` (= holy `STAT:OPER?`) je LATCHED: nabezna hrana condition bitu ho
     * nastavi a **cteni ho vynuluje** — presne proto existuje, aby klient nepropasl
     * kratkou udalost mezi dvema dotazy. `prev` drzi predchozi condition kvuli
     * detekci hrany; latchuje se pri kazdem `scpi_process` (klient se pta periodicky). */
    uint16_t oper_ev, oper_ena, oper_prev;
    uint16_t ques_ev, ques_ena, ques_prev;
} scpi_ctx_t;

/* ── Zdroj dat (abstrakce mezi CM7 globály a CM4 IPC snapshotem) ──────────────
 * Parser/handlery (`scpi.c`) jsou DATA-SOURCE nezávislé — čtou z `scpi_src_t`.
 * Backend ho naplní: CM7 z `g_sensors`/`gps_get`/`fpga_freq`/`g_calib`/`g_meas_cfg`
 * (`scpi_src_load_cm7`), CM4 (výhled TCP) z IPC snapshotu. Bity platnosti (dole)
 * říkají, co je platné — neplatné → dotaz vrátí SCPI NaN `9.91E37`. Akce (config
 * SET, čtení logu) jsou callbacky (na CM7 zápis `g_meas_cfg`/datalog, na CM4 cmd ring). */
#define SCPI_V_FREQ    (1u << 0)   /* platné měření /4 */
#define SCPI_V_DIV16   (1u << 1)   /* platná /16 větev */
#define SCPI_V_FRAME   (1u << 2)   /* existuje poslední DATA rámec (gate/kanál) */
#define SCPI_V_T_OCXO  (1u << 3)
#define SCPI_V_T_BOARD (1u << 4)
#define SCPI_V_T_MCU   (1u << 5)
#define SCPI_V_T_FPGA  (1u << 6)
#define SCPI_V_VC      (1u << 7)
#define SCPI_V_RF      (1u << 8)
#define SCPI_V_V12     (1u << 9)
#define SCPI_V_V5      (1u << 10)
#define SCPI_V_VREF    (1u << 11)
#define SCPI_V_VBAT    (1u << 12)
#define SCPI_V_GPS     (1u << 13)  /* GPS fix (čas/poloha platné) */

/* SCPI CALC SET klíče (nezávislé na transportu/IPC; backend je namapuje). */
enum {
    SCPI_CFG_MATH_EN = 0,  /* vu 0/1 */
    SCPI_CFG_MATH_M,       /* vd */
    SCPI_CFG_MATH_B,       /* vd */
    SCPI_CFG_NULL_EN,      /* vu 0/1 */
    SCPI_CFG_NULL_ACQ,     /* akce (potřebuje platný kmitočet) */
    SCPI_CFG_LIM_EN,       /* vu 0/1 */
    SCPI_CFG_LIM_LO,       /* vd */
    SCPI_CFG_LIM_HI,       /* vd */
    /* ── Instrument SET (2026-08-15): nejdou do `meas_cfg_t`, ale do stavu mereni
     * (`g_ui_cfg`), takze je backend obsluhuje zvlast — `scpi_cfg_apply()` je NEzna.
     * ⚠️ Poradi MUSI sedet s `IPC_CFG_*` (hlida `_Static_assert` v ipc.c). */
    SCPI_CFG_GATE,         /* vu = index brany 0..3 (0,1 / 1 / 10 / 100 s) */
    SCPI_CFG_CHAN,         /* vu = kanal 0/1 (A/B) */
    SCPI_CFG_RUN,          /* vu = 0 STOP / 1 RUN */
};

typedef struct scpi_src scpi_src_t;
struct scpi_src {
    uint32_t valid;                 /* SCPI_V_* */
    /* Kmitočet (×1e5, dělička už zahrnuta). */
    uint64_t freq4_x100000, freq16_x100000;
    uint32_t gate_ns;               /* SKUTECNE zmerene okno z ramce (SENS:FREQ:GATE:ACTual?) */
    uint8_t  channel_id;            /* kanal hlaseny ramcem */
    /* NASTAVENY stav mereni (SET/readback: `SENS:FREQ:GATE?/CHAN?`, `INIT:CONT?`).
     * Zdroj je `g_ui_cfg` (CM7) resp. snapshot (CM4) — NE posledni FPGA ramec. */
    uint8_t  set_gate_idx;          /* 0..3 */
    uint8_t  set_chan;              /* 0/1 */
    uint8_t  set_running;           /* 0 STOP / 1 RUN */
    uint8_t  freq_err;              /* SIGNAL_LOST/MEAS chyba (pro QUEStionable) */
    /* Teploty [0,01 °C]. */
    int16_t  t_ocxo_c100, t_board_c100, t_mcu_c100, t_fpga_c100;
    /* Napětí [mV] + RF kalibrace. */
    uint16_t ocxo_vc_mv, rf_mv, v_12v_mv, v_5v_mv, vref_mv, vbat_mv;
    float    ad8307_slope_mv_db, ad8307_intercept_dbm;
    /* GPS. */
    uint8_t  gps_fix_mode, gps_num_sat, gps_hour, gps_min, gps_sec;
    float    gps_lat_deg, gps_lon_deg, gps_alt_m;
    /* Stav. */
    uint8_t  spi_ok, si5356_status, si5356_ok, selftest_pass;
    uint32_t uptime_s;
    /* Math/limity (CALC readbacky + CALC:DATA?/LIM?). */
    meas_cfg_t meas;
    /* Datalog (MMEM:CAT?/DATA:COUN?). */
    const char *dl_backend;
    uint32_t dl_records, dl_capacity_rec, dl_last_seq, dl_write_errors;
    uint8_t  dl_wrapped;
    /* ── Akce (backend-specifické; NULL = nepodporováno). ── */
    void *be;                       /* backend kontext (např. IPC ring na CM4) */
    /* Aplikuj CALC SET (SCPI_CFG_* klíč, bool vu / double vd). @return 1 = OK.
     * Aktualizuje i src->meas (aby compound SET→readback sedělo). */
    int (*set_cfg)(scpi_src_t *s, uint8_t key, uint32_t vu, double vd);
    /* Přečti n-tý datalog záznam od nejnovějšího (MMEM:DATA?). @return 1 = OK. */
    int (*read_log)(scpi_src_t *s, uint32_t from_newest, datalog_rec_t *out);
};

/** Presety brány [s] -> index 0..3 (0,1 / 1 / 10 / 100 s); <0 = mimo presety.
 *  Vystaveno kvuli sdilenemu IPC backendu (CM4 validuje branu lokalne, viz ipc_scpi.c). */
int scpi_gate_idx_from_s(double sec);

/** Index brány 0..3 -> sekundy (opak `scpi_gate_idx_from_s`). Sdíleno s JSON
 *  v `httpd_min.c`, aby web nedržel vlastní kopii tabulky presetů. */
double scpi_gate_s(uint8_t idx);

/** double -> "±d.ddddd" (5 des. míst, bez `%f`), s ochranou proti přetečení
 *  mimo ±4e9 (viz implementace) — vrací `"9.91E37"` (platné i jako JSON číslo).
 *  Sdíleno mezi `CALC:*?` readbacky a `httpd_min.c` (`GET /api/state`). */
void fmt_scpi_hz_d(double hz, char *out, size_t n);

/** Aplikuje CALC/Math SET (`key`=SCPI_CFG_MATH/NULL/LIM_*) na `meas_cfg_t`. Cista
 *  funkce (zadne globaly) — sdili ji CM7 backend i IPC most na CM4 (ipc_scpi.c).
 *  `freq_hz` jen pro NULL_ACQ (aktualni kmitocet). @return 1 = OK. */
int scpi_cfg_apply(meas_cfg_t *c, uint8_t key, uint32_t vu, double vd, double freq_hz);

/** Vynuluje kontext (prázdná fronta, ESR/ESE/SRE = 0). */
void scpi_ctx_init(scpi_ctx_t *ctx);

/** Zpracuje jednu programovou zprávu (bez CRLF; víc jednotek přes ';') v daném
 *  kontextu nad daným zdrojem dat. Odpověď (vč. '\0') do out; vrací délku (0 = žádná). */
size_t scpi_process_ctx(scpi_ctx_t *ctx, scpi_src_t *src, const char *line, char *out, size_t out_sz);

#if defined(CORE_CM7)
/** CM7 backend: naplní src z globálů + nastaví akce (g_meas_cfg / datalog). */
void   scpi_src_load_cm7(scpi_src_t *src);
/** USB konzole (CM7): načte CM7 zdroj + zpracuje nad SDÍLENÝM default kontextem.
 *  Signatura zachována kvůli volajícímu (freertos_task_uart.c). */
size_t scpi_process(const char *line, char *out, size_t out_sz);
#endif

/** IPC most (ipc_scpi.c, linkuje se do OBOU jader — W2/#25). Ctecí a zapisovací
 *  půlka `scpi_src_t` nad IPC snapshotem/cmd ringem:
 *    - `ipc_scpi_src_from_snap(src, snap)` — čtení: naplní `src` ze snapshotu
 *      (cista funkce, žádné globály). @return 1 = snapshot platný (magic/verze).
 *    - `ipc_scpi_set_cfg` má přesně signaturu `scpi_src_t.set_cfg` — přiřaď ho
 *      přímo (`src->set_cfg = ipc_scpi_set_cfg;`) v CM4 backendu. Validuje lokálně
 *      a pošle `IPC_CMD_CFG_SET` do cmd ringu; CM7 ho vyřídí v `ipc_service`. */
int ipc_scpi_src_from_snap(void *src_out, const void *snap);
int ipc_scpi_set_cfg(scpi_src_t *s, uint8_t key, uint32_t vu, double vd);

/** Pure-logic unit test (parser + status model + config apply) — 1 = PASS. */
int scpi_selftest(void);
/* Radek prvniho neuspesneho assertu ve `scpi_selftest()` (0 = zadny). Test je
 * ~90 kontrol slitych do jedne navratove hodnoty, takze bez tohohle je „FAIL"
 * nedohledatelny — a bez nativniho kompilatoru ho nejde spustit na PC. */
int scpi_selftest_fail_line(void);

#ifdef __cplusplus
}
#endif
