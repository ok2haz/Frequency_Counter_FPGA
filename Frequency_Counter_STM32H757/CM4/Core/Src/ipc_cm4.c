/**
 * @file    ipc_cm4.c
 * @brief   CM4 strana IPC (viz ipc_cm4.h): cteni snapshotu CM7->CM4 (seqlock) +
 *          publikace heartbeatu CM4->CM7. Pure (jen ipc_shared.h + stdint, bez HAL).
 *
 * ⚠️ Boot poradi: CM7 uvolni CM4 pres HSEM uz po SystemClock_Config (brzy), ale
 * `ipc_init` (razitko snapshotu) dela az v StartDefaultTask (po scheduleru). Takze
 * CM4 muze chvili cist snapshot bez magicu -> ipc_cm4_check vraci 0, CM4 zkousi
 * dal. Az CM7 orazitkuje, header sedne.
 * ✅ Od 2026-09-19 (audit F-0017) plati: **kazde jadro nuluje svuj blok.** CM7
 * `ipc_init()` uz na blok `cm4` NESAHA (nuluje jen `snap`/`cmd`/`resp`/`log`/
 * `errlog`); `cm4` si nuluje CM4 tady v `ipc_cm4_init()`, kde je jedinym
 * zapisovatelem a jeste nepublikovala — tedy bez race.
 * Do te doby delal CM7 `memset` pres CELOU strukturu ze `StartDefaultTask`, tedy
 * sekundy po bootu, zatimco CM4 publikuje uz ~1,3 s po bootu: jednorazovy zapis
 * se mohl TISE ztratit navzdy (doloženo na HW 2026-08-30 — memset dopadl mezi
 * publikaci httpd a eth). Jedina vyjimka dnes: kdyz vyprsi boot gate a CM4 tedy
 * prokazatelne nenabehla, vycisti blok CM7 (`ipc_clear_cm4_block`).
 * ⚠️ Razitkovani jednorazovych hodnot v heartbeatu (`s_scpi_ok`/`s_httpd_ok`)
 * tim prestalo byt NUTNE, ale zustava jako pojistka — viz blok „JEDNORAZOVE
 * hodnoty" nize.
 */
#include "ipc_cm4.h"
#include <stddef.h>   /* NULL */

/* Linker symboly vlastniho obrazu CM4 (STM32H757BITX_FLASH.ld) — stejny
 * vzorec, jaky uz CM7 pouziva ve svem okne PAMET (`app_gpsdo.c`). Zustava to
 * "pure" (jen adresy, zadny HAL) jako zbytek souboru. */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

static uint8_t  s_ready;    /* 1 = snapshot header (magic/verze/size) overen */
static uint32_t s_hb;       /* heartbeat citac (roste kazdym publikovanim) */
/* Jednorazove hodnoty publikovane do bloku `cm4` — drzi se lokalne, aby se
 * daly v heartbeatu razitkovat znovu. ⚠️ Od F-0017 uz to NENI nutne (CM7 blok
 * `cm4` nemaze), ale zustava to jako pojistka — viz blok „JEDNORAZOVE hodnoty". */
static uint8_t  s_scpi_ok, s_httpd_ok;   /* 0 = jeste nebezelo */

void ipc_cm4_init(void)
{
    s_ready = 0;
    s_hb    = 0;
    /* 🔴 CM4 nuluje SVUJ blok SAMA (audit F-0017). Do 2026-09-19 to za ni delal
     * `ipc_stamp()` na CM7 — memsetem pres CELOU sdilenou strukturu, a ten bezi
     * ze `StartDefaultTask`, tedy SEKUNDY po bootu (az za bring-upem displeje),
     * zatimco CM4 publikuje uz ~1,3 s po bootu. Memset tedy mohl dopadnout
     * DOPROSTRED publikovani a jednorazovy zapis tise smazat navzdy.
     * Ze to neni teorie: pri HW pruchodu 2026-08-30 dopadl MEZI publikaci httpd
     * a eth, takze `status` hlasil „SCPI(CM4): jeste nedobehl", prestoze
     * selftest probehl a PROSEL.
     *
     * ⚠️ Nulovat se to MUSI: pri STUDENEM startu je SRAM4 nahodna, takze bez
     * tohohle by tu bylo smeti a `magic` by mohlo nahodou sednout. Tady je to
     * bezpecne, protoze CM4 je jediny zapisovatel tohohle bloku a jeste
     * nepublikovala — zadny race.
     * ⚠️ `magic` se nastavuje AZ v heartbeatu, po datech; dokud je nula, CM7
     * blok ignoruje (vsechny `ipc_cm4_*` accessory gate-uji na magicu).
     * ⚠️ Crash black-box CM4 (`cm4_fault_*`) se tim maze taky — a je to spravne:
     * novy beh CM4 nema vydavat fault z predchoziho za svuj. Naopak PRI
     * SAMOSTATNEM RESETU CM7 uz ho nikdo nesmaze, takze zaznam prezije. */
    for (uint32_t i = 0; i < sizeof g_ipc.cm4; i++)
        ((volatile uint8_t *)&g_ipc.cm4)[i] = 0u;
    IPC_DMB();
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

int ipc_cm4_read_stab(ipc_stab_t *out)
{
    if (!s_ready || out == NULL) return 0;
    uint32_t s0, s1;
    int tries = 0;
    /* Seqlock jako u snapshotu: UiTask publikuje ~1x/s, retry je vzacny; strop 8. */
    do {
        s0 = g_ipc.stab.seq;
        IPC_DMB();
        *out = g_ipc.stab;
        IPC_DMB();
        s1 = g_ipc.stab.seq;
    } while (((s0 & 1u) || s0 != s1) && ++tries < 8);
    if ((s0 & 1u) || s0 != s1) return 0;
    if (out->np > IPC_STAB_PTS) out->np = IPC_STAB_PTS;
    return 1;
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
     * Razitkuje se v kazdem heartbeatu (ne jen jednou). ⚠️ Puvodni duvod (memset
     * cele struktury v `ipc_init` na CM7) od F-0017 UZ NEPLATI — CM7 blok `cm4`
     * nemaze. Razitkovani zustava jako pojistka a stoji jeden zapis za sekundu. */
    g_ipc.cm4.cm4_ipc_version = (uint8_t)IPC_VERSION;
    g_ipc.cm4.cm4_cpu_pct  = cpu_pct;
    g_ipc.cm4.cm4_uptime_s = uptime_s;
    /* Re-stamp JEDNORAZOVYCH hodnot (viz komentar u `s_scpi_ok` nize). ⚠️ Od F-0017
     * uz to NENI nutne (CM7 blok `cm4` nemaze), zustava jako pojistka. Nuly se
     * nepisou, aby se nepretlacovalo "jeste nedobehl" pres pozdejsi zapis. */
    if (s_scpi_ok)  g_ipc.cm4.scpi_selftest_ok  = s_scpi_ok;
    if (s_httpd_ok) g_ipc.cm4.httpd_selftest_ok = s_httpd_ok;
    /* v16: velikost obrazu — staticka po celou dobu behu, ale razitkuje se
     * znovu pri kazdem heartbeatu ze stejneho duvodu jako `cm4_ipc_version`
     * vyse (pojistka; puvodni memset v ipc_init() uz sem nesaha — F-0017). Vzorec
     * shodny s CM7 oknem PAMET: image = _sidata + (.data velikost) - baze
     * flash banky; RAM = (.data + .bss). */
    g_ipc.cm4.cm4_flash_bytes = ((uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata))
                              - 0x08100000u;   /* CM4 flash bank2 */
    g_ipc.cm4.cm4_ram_bytes   = ((uint32_t)&_edata - (uint32_t)&_sdata)
                              + ((uint32_t)&_ebss  - (uint32_t)&_sbss);
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

/* ── JEDNORAZOVE hodnoty (vysledky selftestu) ─────────────────────────────────
 * Drzi se LOKALNE a razitkuji se ZNOVU v kazdem heartbeatu.
 *
 * ⚠️ PUVODNI DUVOD OD 2026-09-19 UZ NEPLATI (audit F-0017). Do te doby delal CM7
 * v `ipc_init()` **`memset` CELE** sdilene struktury vcetne bloku `cm4`, ktery
 * vlastni CM4 — a to az ze `StartDefaultTask`, tedy po pomale inicializaci
 * displeje (~sekundy), zatimco CM4 (bare-metal) publikuje uz ~1,3 s po bootu,
 * hned po pipaci melodii. Kdo vyhral, zaviselo na nabehu -> jednorazovy zapis se
 * mohl TISE ZTRATIT a uz nikdy se nevratit. Presne to se stalo pri HW pruchodu
 * 2026-08-30 (studeny start): `memset` dopadl MEZI publikaci httpd a eth, takze
 * `status` hlasil „SCPI(CM4): jeste nedobehl" + „HTTP(CM4): jeste nedobehl", ale
 * „ETH(CM4): init OK" — presne v poradi, v jakem to CM4 zapisuje (viz main.c).
 * Dnes CM7 na blok `cm4` nesaha a nuluje si ho CM4 sama v `ipc_cm4_init()`.
 *
 * 🔑 Razitkovani proto zustava jako POJISTKA, ne jako podminka spravnosti — stoji
 * par zapisu za sekundu a chrani proti tomu, kdyby na `cm4` zacal sahat nekdo jiny.
 * ⚠️ Nova jednorazova hodnota v bloku `cm4` uz tedy NEMUSI jit touhle cestou, ale
 * je to porad ta bezpecnejsi varianta — a kdyby se `ipc_stamp()` na CM7 kdy vratilo
 * k plnemu memsetu, je to jedina vec, ktera to prezije. */

/* v6 (F3): vysledek ETH bring-upu. Hodnota napred, priznak platnosti naposled
 * (CM7 na `eth_init_ok` gate-uje zobrazeni PHY ID). Publikuje se OPAKOVANE
 * z hlavni smycky CM4, takze vlastni re-stamp v heartbeatu nepotrebuje. */
void ipc_cm4_set_eth(uint8_t init_ok, uint32_t phy_id)
{
    g_ipc.cm4.eth_phy_id  = phy_id;
    IPC_DMB();
    g_ipc.cm4.eth_init_ok = init_ok ? 1u : 0u;
}

/* v18 (audit F-0138): pocitadla vyslani. SATURUJI — volne bezici citac by po
 * pretoceni ukazal 0, tedy presne tu hodnotu, ktera znamena „nevyslal jsem nic".
 * Saturace znamena „aspon tolik" a nulu drzi vyhradne pro „nikdy", cimz zustane
 * zachovana ta jedina odpoved, pro kterou pole existuje.
 * ⚠️ Vola se OPAKOVANE ze smycky (stejne jako `ipc_cm4_set_eth`) — jednorazovy
 * zapis by smazal `memset` v `ipc_init()` na CM7. */
void ipc_cm4_set_eth_tx(uint32_t tx_ok, uint32_t tx_err)
{
    g_ipc.cm4.eth_tx_ok  = (tx_ok  > 65535u) ? 65535u : (uint16_t)tx_ok;
    g_ipc.cm4.eth_tx_err = (tx_err > 255u)   ? 255u   : (uint8_t)tx_err;
}

/* v7 (W2): vysledek `scpi_selftest()`. Vola se jednou pri bootu (viz main.c). */
void ipc_cm4_set_scpi_selftest(uint8_t ok)
{
    s_scpi_ok = ok ? 1u : 2u;
    g_ipc.cm4.scpi_selftest_ok = s_scpi_ok;
}

/* v9 (W4): vysledek `httpd_min_selftest()`. Stejny vzor jako scpi. */
void ipc_cm4_set_httpd_selftest(uint8_t ok)
{
    s_httpd_ok = ok ? 1u : 2u;
    g_ipc.cm4.httpd_selftest_ok = s_httpd_ok;
}
