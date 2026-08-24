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
    /* ⚠️ CM7 jeste ANI JEDNOU nepublikoval. Bez teto vetve by se to tvarilo jako
     * ziva CM7: `s_last_seq` i `s_last_ms` startuji na nule, takze `seq == s_last_seq`
     * (0 == 0) spadne rovnou na `(now_ms - 0) < 2000` = pravda. CM4 by prvni ~2 s po
     * bootu serviroval PRAZDNY snapshot jako aktualni data (magic uz orazitkoval
     * `ipc_init`, takze `ipc_cm4_read` ho propusti). "Jeste nepublikoval" NENI
     * "publikoval a zamrzl" — pro SCPI/web to musi byt offline, ne nuly.
     * `seq == 0` je spolehlivy priznak: seqlock ho pri prvnim publish zvedne na 2
     * a k pretoceni uint32 by pri ~4 publish/s doslo za ~34 let. */
    if (seq == 0u) return 0;
    if (seq != s_last_seq) { s_last_seq = seq; s_last_ms = now_ms; return 1; }
    return (now_ms - s_last_ms) < 2000u;   /* seq nezmenen >2 s -> CM7 zamrzly */
}

void ipc_cm4_heartbeat(uint32_t cpu_pct, uint32_t uptime_s)
{
    g_ipc.cm4.magic        = IPC_MAGIC;           /* potvrdi CM7, ze CM4 opravdu zapisuje */
    /* Verze, se kterou je prelozen TENTO obraz CM4 -> CM7 pozna nesoulad bank.
     * Razitkuje se v kazdem heartbeatu (ne jen jednou), aby to prezilo samostatny
     * reset CM7 — ten pri `ipc_init` dela memset cele sdilene struktury. */
    g_ipc.cm4.cm4_ipc_version = (uint8_t)IPC_VERSION;
    g_ipc.cm4.cm4_cpu_pct  = cpu_pct;
    g_ipc.cm4.cm4_uptime_s = uptime_s;
    IPC_DMB();                                    /* data viditelna PRED inkrementem heartbeatu */
    g_ipc.cm4.heartbeat    = ++s_hb;              /* CM7 sleduje rust -> liveness */
}

/* Publikace stavu ETH linky (v5, F1). Dnes CM4 vola s down/0 (lwIP az F5), pak
 * realne z netif. Nezavisle na heartbeatu (link se meni ridceji nez 1/s). */
void ipc_cm4_set_net(uint8_t link_up, uint8_t speed_mbps, uint8_t duplex, uint32_t ip)
{
    g_ipc.cm4.net_ip         = ip;
    g_ipc.cm4.net_speed_mbps = speed_mbps;
    g_ipc.cm4.net_duplex     = duplex;
    IPC_DMB();
    g_ipc.cm4.net_link       = link_up ? 1u : 0u;   /* link naposled (CM7 na nej gate-uje) */
}

/* v6 (F3): vysledek ETH bring-upu. Stejny vzor jako set_net — hodnota napred,
 * priznak platnosti naposled (CM7 na `eth_init_ok` gate-uje zobrazeni PHY ID). */
void ipc_cm4_set_eth(uint8_t init_ok, uint32_t phy_id)
{
    g_ipc.cm4.eth_phy_id  = phy_id;
    IPC_DMB();
    g_ipc.cm4.eth_init_ok = init_ok ? 1u : 0u;
}

/* v7 (W2): vysledek `scpi_selftest()`. Vola se jednou pri bootu (viz main.c) — na
 * rozdil od eth/net se sem nic pozdeji nevraci, takze se NEPUBLIKUJE opakovane
 * ve smycce; hodnota po zapisu uz je definitivni pro cely beh. */
void ipc_cm4_set_scpi_selftest(uint8_t ok)
{
    g_ipc.cm4.scpi_selftest_ok = ok ? 1u : 2u;
}

/* v9 (W4): vysledek `httpd_min_selftest()`. Stejny vzor jako scpi — jednorazovy
 * zapis pri bootu, nepublikuje se opakovane. */
void ipc_cm4_set_httpd_selftest(uint8_t ok)
{
    g_ipc.cm4.httpd_selftest_ok = ok ? 1u : 2u;
}
