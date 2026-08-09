#pragma once
/**
 * @file    ipc_cm4.h
 * @brief   CM4 strana IPC: cteni snapshotu z SRAM4 (seqlock) + publikace heartbeatu.
 *
 * Sdileny protokol je v CM7/Core/Inc/ipc_shared.h (ZDROJ PRAVDY obou jader).
 * ⚠️ Header se sdili RELATIVNIM include — CM4 a CM7 jsou sourozenci pod korenem
 * projektu, takze zadna zmena build configu (regen-safe). Az vznikne spolecny
 * include path v obou projektech, nahradit za <ipc_shared.h>.
 *
 * ⚠️ STAV (2026-08-09): CM4 zatim NEBOOTUJE (bank2 neflashnuta, `g_cm4_absent` na
 * CM7). Tento konzument je NAPSANY a kompiluje se pro M4, ale RUNTIME (skutecne
 * mezijaderne cteni snapshotu) se overi az po flashnuti bank2 + boot gate. Logika
 * seqlocku je SDILENA s CM7 (tytez pointer-core helpery `ipc_snap_rd_*`, kryté
 * `ipc_selftest` na CM7) -> primitiva jsou validovana, tady jen tenke pouziti.
 *
 * ⚠️ SRAM4/D3 clock: CM4 NEpovoluje explicitne (symetricky s CM7, ktery to taky
 * nedela — spoleha na default). Kdyby IPC na HW mlcelo, overit AHB4/SRAM4 clock
 * enable na OBOU jadrech (per-core RCC) — bring-up checklist, ne zdejsi bug.
 */
#include "../../../CM7/Core/Inc/ipc_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Reset lokalniho stavu (ready flag, heartbeat citac). Volat jednou po bootu. */
void ipc_cm4_init(void);

/** Overi hlavicku snapshotu (magic/verze/size, ktere CM7 orazitkoval v ipc_init).
 *  Volat dokud nevrati 1 (CM7 muze nabihat pomaleji). @return 1 = kompatibilni. */
int  ipc_cm4_check(void);

/** @return 1 = hlavicka uz byla overena OK (viz ipc_cm4_check). */
int  ipc_cm4_ready(void);

/** Precte aktualni snapshot do `out` seqlock protokolem (bezpecne pri soubeznem
 *  zapisu CM7). @return 1 = konzistentni kopie, 0 = neready / CM7 zapisuje moc casto. */
int  ipc_cm4_read(ipc_snapshot_t *out);

/** Publikuj zivost CM4 do g_ipc.cm4 (CM7 cte pro "4:xx%" v headeru + liveness).
 *  Heartbeat se inkrementuje AZ PO zapisu dat (bariera) -> CM7 nevidi roztrzeno. */
void ipc_cm4_heartbeat(uint32_t cpu_pct, uint32_t uptime_s);

#ifdef __cplusplus
}
#endif
