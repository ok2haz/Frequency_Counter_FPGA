/**
 * @file    ipc_scpi.c
 * @brief   SDILENY SCPI backend nad IPC snapshotem — linkuje se do OBOU jader.
 *
 * PROC SDILENY: CM4 obsluhuje SCPI pres TCP a CM7 tutez cestu testuje prikazem
 * `scpi ipc <cmd>` (porovnava odpoved se `scpi <cmd>` z realnych globalu). Kdyby
 * kazde jadro melo vlastni kopii, testovaci cesta by prestala testovat to, co
 * skutecne bezi na CM4. Proto je tenhle soubor v obou buildech.
 *
 * ⚠️ Musi zustat PROSTY: jen `ipc_shared.h` + `scpi.h`, zadny HAL, FreeRTOS ani
 * globaly jednoho jadra — jinak se do CM4 nedostane.
 *
 * Deleni prace:
 *   CTENI  — `ipc_scpi_src_from_snap()`: snapshot -> `scpi_src_t` (cista funkce).
 *   ZAPIS  — `ipc_scpi_set_cfg()`: SCPI SET -> `IPC_CMD_CFG_SET` do cmd ringu,
 *            CM7 ho vyridi v `ipc_service` (viz W1 v WEB_UI_PLAN.md).
 */
#include "ipc_shared.h"
#include "scpi.h"
#include <string.h>

int ipc_scpi_src_from_snap(void *src_out, const void *snap_in)
{
    scpi_src_t *s = (scpi_src_t *)src_out;
    const ipc_snapshot_t *sn = (const ipc_snapshot_t *)snap_in;
    if (s == NULL || sn == NULL) return 0;
    memset(s, 0, sizeof *s);
    if (sn->magic != IPC_MAGIC || sn->version != IPC_VERSION) return 0;

    s->valid = sn->sens_valid;          /* pozice bitu jsou shodne, viz asserty vyse */

    s->freq4_x100000  = sn->freq4_x100000;
    s->freq16_x100000 = sn->freq16_x100000;
    s->gate_ns        = sn->gate_ns;
    s->channel_id     = sn->channel_id;

    s->t_ocxo_c100  = sn->t_ocxo_c100;
    s->t_board_c100 = sn->t_board_c100;
    s->t_mcu_c100   = sn->t_mcu_c100;
    s->t_fpga_c100  = sn->t_fpga_c100;

    s->ocxo_vc_mv = sn->ocxo_vc_mv;
    s->rf_mv      = sn->rf_mv;
    s->v_12v_mv   = sn->v_12v_mv;
    s->v_5v_mv    = sn->v_5v_mv;
    s->vref_mv    = sn->vref_mv;
    s->vbat_mv    = sn->vbat_mv;
    s->ad8307_slope_mv_db   = sn->ad8307_slope_mv_db;
    s->ad8307_intercept_dbm = sn->ad8307_intercept_dbm;

    s->gps_fix_mode = sn->gps_fix_mode;
    s->gps_num_sat  = sn->gps_num_sat;
    s->gps_lat_deg  = (float)sn->gps_lat_e7 * 1e-7f;
    s->gps_lon_deg  = (float)sn->gps_lon_e7 * 1e-7f;
    s->gps_alt_m    = (float)sn->gps_alt_cm * 0.01f;
    /* Cas: snapshot nese unix, `scpi_src_t` hodiny/minuty/sekundy. Prevod je
     * ciste modularni — datum SCPI z tohohle pole necte (`SYST:GPS:TIME?`). */
    { uint32_t sod = sn->rtc_unix % 86400u;
      s->gps_hour = (uint8_t)(sod / 3600u);
      s->gps_min  = (uint8_t)((sod / 60u) % 60u);
      s->gps_sec  = (uint8_t)(sod % 60u); }

    s->si5356_status = sn->si5356_status;
    s->si5356_ok     = sn->si5356_ok;
    s->uptime_s      = sn->uptime_s;
    s->spi_ok        = (sn->flags & IPC_F_FPGA_LINK) ? 1u : 0u;
    s->freq_err      = (sn->flags & IPC_F_SIGNAL_LOST) ? 1u : 0u;
    s->sim_active    = (sn->flags & IPC_F_SIM) ? 1u : 0u;   /* emulace, ne mereni (DIAG:SIM?) */
    /* ⚠️ NASTAVENI (brana/kanal/RUN) se cte z `ui_cfg` (v11), presne stejnym dekodem
     * jako CM7 backend `scpi_src_load_cm7_ex` — jinak by tentyz dotaz vracel pres USB
     * neco jineho nez pres TCP/HTTP. Do v10 se `set_gate_idx` neplnilo VUBEC (zustalo
     * 0 z memsetu => `SENS:FREQ:GATE?` vzdy 0,1 s) a `set_chan` se bralo z
     * `channel_id`, coz je kanal HLASENY FPGA RAMCEM — pri mrtvem linku 0, takze
     * `CHAN?` hlasilo 0 i po uspesnem `CHAN 1`. SET pritom fungoval (stejny most jako
     * RUN), takze to vypadalo jako „nejde nastavit", ale slo o SLEPY READBACK. */
    s->set_chan      = (uint8_t)((sn->ui_cfg >> 1) & 1u);
    s->set_gate_idx  = (uint8_t)((sn->ui_cfg >> 2) & 3u);
    s->set_running   = (sn->flags & IPC_F_RUNNING) ? 1u : 0u;   /* tentyz bit4 `g_ui_cfg`, jen uz zabaleny ve flags */

    /* Math/limit cfg mirror (CALC readbacky). */
    s->meas.math_en  = sn->math_en ? 1 : 0;
    s->meas.null_en  = sn->null_en ? 1 : 0;
    s->meas.limit_en = sn->limit_en ? 1 : 0;
    s->meas.m        = sn->math_m;
    s->meas.b        = sn->math_b;
    s->meas.null_ref = sn->null_ref;
    s->meas.lo       = sn->lim_lo;
    s->meas.hi       = sn->lim_hi;

    /* set_cfg/read_log zustavaji NULL: zapis konfigurace jde z CM4 cmd ringem
     * (jiny mechanismus) a datalog snapshot nenese. NULL = "nepodporovano",
     * parser to korektne odmitne misto vymysleni hodnoty. */
    return 1;
}


/* ── ZAPISOVA PULKA: SCPI SET -> cmd ring (W2) ───────────────────────────────
 * Vola ji SCPI, kdyz prijde SET (`CALC:*`, `SENS:FREQ:GATE/CHAN`, `INIT`/`ABOR`).
 *
 * ⚠️ VALIDUJE SE LOKALNE, PRED odeslanim. Duvod: cekat na odpoved z resp ringu by
 * znamenalo mit uspavaci primitivum (`osDelay` na CM7 vs `HAL_Delay` na CM4), coz
 * by tenhle soubor prestal byt jadrove neutralni — a busy-wait v obsluze TCP je
 * horsi nez lokalni kontrola. Vsechno, co muze selhat smyslu prikazu (brana mimo
 * presety, kanal > 1), se pozna i tady; jedina zbyla chyba je "ring plny".
 *
 * ⚠️ Latence: prikaz se projevi az ho vyridi `ipc_service` na CM7 (~10 ms) a
 * u GATE/CHAN/RUN jeste UiTask (~0,2 s). Volajici NESMI hned cist zpet a cekat
 * novou hodnotu — snapshot ji ponese az za chvili.
 *
 * @return 1 = prijato k odeslani. */
int ipc_scpi_set_cfg(scpi_src_t *s, uint8_t key, uint32_t vu, double vd)
{
    ipc_cmd_t c = { .type = IPC_CMD_CFG_SET, .key = key, .id = 0, .arg = vu, .argd = vd };

    /* Lokalni validace + zrcadlo do `src`, aby compound "SET;READBACK?" v jedne
     * zprave sedelo hned (jinak by readback vratil starou hodnotu). */
    switch (key) {
        case SCPI_CFG_GATE: {
            int gi = scpi_gate_idx_from_s(vd);
            if (gi < 0) return 0;                 /* mimo presety -> SCPI -222 */
            c.arg = (uint32_t)gi;                 /* pres ring jde INDEX, ne sekundy */
            s->set_gate_idx = (uint8_t)gi;
            break;
        }
        case SCPI_CFG_CHAN:
            if (vu > 1u) return 0;
            s->set_chan = (uint8_t)vu;
            break;
        case SCPI_CFG_RUN:
            s->set_running = vu ? 1u : 0u;
            break;
        default:
            /* Math/limity: aplikuj i na lokalni zrcadlo (`scpi_cfg_apply` je cista
             * funkce nad `meas_cfg_t`), aby readback ve stejne zprave sedel. */
            if (!scpi_cfg_apply(&s->meas, key, vu, vd, 0.0)) return 0;
            break;
    }

    if (!ipc_cmd_push(&c)) return 0;              /* ring plny -> SCPI ohlasi chybu */

    /* Vysyp odpovedi, at resp ring nepretece — na vysledek necekame (viz vyse). */
    ipc_resp_t r;
    while (ipc_resp_pop(&r)) { /* zahazujeme */ }
    return 1;
}
