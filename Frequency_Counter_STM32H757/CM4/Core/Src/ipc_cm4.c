/**
 * @file    ipc_cm4.c
 * @brief   CM4 strana IPC (viz ipc_cm4.h): cteni snapshotu CM7->CM4 (seqlock) +
 *          publikace heartbeatu CM4->CM7. Pure (jen ipc_shared.h + stdint, bez HAL).
 *
 * ⚠️ Boot poradi: CM7 uvolni CM4 pres HSEM uz po SystemClock_Config (brzy), ale
 * `ipc_init` (razitko snapshotu) dela az v StartDefaultTask (po scheduleru). Takze
 * CM4 muze chvili cist snapshot bez magicu -> ipc_cm4_check vraci 0, CM4 zkousi
 * dal. Az CM7 orazitkuje, header sedne. (CM7 ipc_init navic JEDNOU vynuluje i
 * cm4 oblast -> CM4 heartbeat se hned dalsim tikem obnovi; benigni boot efekt.)
 */
#include "ipc_cm4.h"
#include <stddef.h>   /* NULL */

static uint8_t  s_ready;    /* 1 = snapshot header (magic/verze/size) overen */
static uint32_t s_hb;       /* heartbeat citac (roste kazdym publikovanim) */

void ipc_cm4_init(void)
{
    s_ready = 0;
    s_hb    = 0;
}

int ipc_cm4_check(void)
{
    /* Header (magic/verze/size) se za behu nemeni -> staci primy odecet (bez seqlock). */
    s_ready = (g_ipc.snap.magic   == IPC_MAGIC
            && g_ipc.snap.version == (uint16_t)IPC_VERSION
            && g_ipc.snap.size    == (uint16_t)sizeof(ipc_snapshot_t));
    return s_ready;
}

int ipc_cm4_ready(void) { return s_ready; }

int ipc_cm4_read(ipc_snapshot_t *out)
{
    if (!s_ready || out == NULL) return 0;
    uint32_t s;
    int retry, tries = 0;
    /* Seqlock: opakuj dokud cteni neprobehne mimo zapis CM7 (liche seq / zmena
     * seq behem kopie). Zapis CM7 je ~2 Hz na mikrosekundy -> retry extremne
     * vzacny; strop 8 (CM4 se NESMI kvuli CM7 zaseknout, viz NAVRH §11.4). */
    do {
        s = ipc_snap_rd_begin(&g_ipc.snap);
        *out = g_ipc.snap;                        /* kopie cele struktury */
        retry = ipc_snap_rd_retry(&g_ipc.snap, s);
    } while (retry && ++tries < 8);
    /* ⚠️ Per-read kontrola magicu: ipc_init (razitko g_ipc na CM7) dela memset BEZ
     * seqlocku -> pri bootu uzke okno, kdy je snap vynulovan (seq=0, sude, "konzistentni")
     * a magic=0. Bez teto kontroly by CM4 vratil vynulovana data jako platna. Odmitnutim
     * na magicu je cteni robustni vuci memsetu i re-initu za behu. */
    return !retry && out->magic == IPC_MAGIC;     /* 1 = konzistentni a platny snapshot */
}

int ipc_cm4_cm7_alive(uint32_t now_ms)
{
    /* Liveness CM7 z pohledu CM4: snapshot `seq` roste (CM7 publikuje na kazde mereni
     * + >=2 Hz heartbeat). Zamrzly seq = zaseknuty CM7 -> CM4 NESMI servirovat stara
     * data jako aktualni (SCPI/web = chyba/offline), NAVRH §11.4. Symetricke k CM7
     * ipc_cm4_alive. Bez tohoto by ipc_cm4_read vracel posledni snapshot jako platny
     * i po zaseknuti CM7 (seqlock je konzistentni, jen zamrzly). */
    static uint32_t s_last_seq, s_last_ms;
    if (!s_ready) return 0;
    uint32_t seq = g_ipc.snap.seq;
    if (seq != s_last_seq) { s_last_seq = seq; s_last_ms = now_ms; return 1; }
    return (now_ms - s_last_ms) < 2000u;   /* seq nezmenen >2 s -> CM7 zamrzly */
}

void ipc_cm4_heartbeat(uint32_t cpu_pct, uint32_t uptime_s)
{
    g_ipc.cm4.magic        = IPC_MAGIC;           /* potvrdi CM7, ze CM4 opravdu zapisuje */
    g_ipc.cm4.cm4_cpu_pct  = cpu_pct;
    g_ipc.cm4.cm4_uptime_s = uptime_s;
    IPC_DMB();                                    /* data viditelna PRED inkrementem heartbeatu */
    g_ipc.cm4.heartbeat    = ++s_hb;              /* CM7 sleduje rust -> liveness */
}
