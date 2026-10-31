/**
  ******************************************************************************
  * @file    lwip_app.c
  * @brief   lwIP (NO_SYS=1) na CM4 — init, obsluha ze smycky, DHCP klient.
  *
  * PROC NO_SYS=1 (rozhodnuti F4): CM4 je dnes holá `while(1)` smycka bez RTOS.
  * Raw API + polling je tedy nejmensi mozny zasah — nepridava FreeRTOS, jeho
  * heap ani dalsi stacky do 128 KB, ktere CM4 ma. Kdyby pozdeji prisel webserver
  * (F7) a chtel blokujici sockety, da se prejit na NO_SYS=0; do te doby by RTOS
  * byl jen rezie navic.
  *
  * ⚠️ DEGRADACE: kdyz ETH nenabehlo (`heth.gState != READY`, typicky stoji RMII
  * REF_CLK), `ethernetif_init` necha rozhrani "down" a tenhle modul dal jen tise
  * tika. CM4 tim NEUMIRA — konektivita smi chybet, viz `g_init_nonfatal` v main.c.
  *
  * ⚠️ Staticka IP zatim NENI: okno SIT na CM7 (s_view=35) sice uklada DHCP on/off
  * a rucni adresu do syscfg, ale IPC snapshot tahle pole zatim nenese. Do te doby
  * jede vzdy DHCP. Doplnit spolu s rozsirenim snapshotu.
  ******************************************************************************
  */

#include "lwip_app.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/dhcp.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "netif/etharp.h"
#include <string.h>

#include "ethernetif.h"
#include "eth.h"        /* heth — vlastni ho generovany eth.c */
#include "ipc_cm4.h"    /* publikace stavu linky/IP do CM7 */
#include "scpi_tcp.h"   /* SCPI server port 5025 (F6 / W3) */
#include "httpd_min.h"  /* HTTP server port 80: /api/state + /api/scpi (W4) */

/* Sitove rozhrani. Jedno, natvrdo — CM4 ma jeden ETH. */
static struct netif s_netif;

/* ⚠️ 1 = `low_level_init` se dostal az za `LAN8742_RegisterBusIO`, tedy PHY driver ma
 * naplnene IO callbacky. Bez tehle pojistky by degradovana cesta CM4 ZABILA:
 * kdyz ETH nenabehne (typicky stoji RMII REF_CLK), `low_level_init` se vraci JESTE PRED
 * registraci IO — jenze `ethernet_link_check_state()` se z `lwip_app_process()` vola dal
 * kazdych 200 ms a `LAN8742_GetLinkState()` dela `pObj->IO.ReadReg(...)` BEZ kontroly na
 * NULL (viz lan8742.c). Volani NULL ukazatele = HardFault do ctvrt sekundy, tedy presne
 * to, cemu mela degradace zabranit. */
static uint8_t s_if_ok;

/* Kontrola linky se nedela kazdou iteraci: `LAN8742_GetLinkState` je nekolik
 * MDIO transakci (kazda ~desitky us) a stav linky se nemeni casteji nez lidsky.
 * 200 ms je dost rychle pro UI a levne pro smycku. */
#define LINK_CHECK_MS   200u
static uint32_t s_link_next_ms;

/* Posledni publikovany stav — publikuje se jen pri ZMENE, at se po IPC
 * netlucou zbytecne zapisy (CM7 to stejne cte az 2x/s). */
static uint8_t  s_pub_link = 0xFFu, s_pub_speed = 0xFFu, s_pub_duplex = 0xFFu;
static uint32_t s_pub_ip   = 0xFFFFFFFFu;

/* ══════════════ mDNS responder (`gpsdo.local`) — v12 (#2) ═══════════════════
 * Rucne psany (jako httpd/scpi_tcp), protoze vendorovany lwIP `mdns` modul neni
 * v projektu (jen hlavicky). Odpovida na A-dotaz pro `gpsdo.local` na
 * 224.0.0.251:5353 -> na siti se pristroj najde jmenem misto IP.
 * ⚠️ BEST-EFFORT, NEOVERENO NA HW: zavisi na tom, ze MAC prijme multicast
 * (PassAllMulticast, nastaveno nize) a ze igmp join projde. Kdyz cokoli selze,
 * CM4 bezi dal (jako `gps glonass` je opt-in a neskodny pri NAKu). */
#define MDNS_HOST   "gpsdo"           /* -> gpsdo.local */
static struct udp_pcb *s_mdns;
static uint8_t s_mdns_joined;

/* Porovna QNAME v dotazu s "gpsdo"+"local"+root. `q` ukazuje na zacatek QNAME,
 * `end` je konec paketu. Vraci ukazatel ZA QNAME (na QTYPE) nebo NULL pri neshode.
 * Nepodporuje kompresi (0xC0) v dotazu — dotazy ji nepouzivaji. */
static const uint8_t *mdns_match_name(const uint8_t *q, const uint8_t *end)
{
    static const char *lab[2] = { MDNS_HOST, "local" };
    for (int i = 0; i < 2; i++) {
        if (q >= end) return NULL;
        size_t ln = strlen(lab[i]);
        if ((size_t)*q != ln) return NULL;                 /* jina delka labelu */
        q++;
        if (q + ln > end) return NULL;
        for (size_t k = 0; k < ln; k++) {
            char c = (char)q[k];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');   /* case-insensitive */
            if (c != lab[i][k]) return NULL;
        }
        q += ln;
    }
    if (q >= end || *q != 0) return NULL;                  /* root label (0) */
    return q + 1;
}

static void mdns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port)
{
    (void)arg; (void)addr; (void)port;
    if (p == NULL) return;
    uint8_t buf[256];
    u16_t n = (p->tot_len < sizeof buf) ? p->tot_len : (u16_t)sizeof buf;
    pbuf_copy_partial(p, buf, n, 0);
    pbuf_free(p);
    if (n < 12) return;

    uint16_t flags = (uint16_t)((buf[2] << 8) | buf[3]);
    if (flags & 0x8000u) return;                            /* je to odpoved, ne dotaz */
    uint16_t qd = (uint16_t)((buf[4] << 8) | buf[5]);
    if (qd == 0) return;

    const uint8_t *q = buf + 12, *end = buf + n;
    const uint8_t *after = mdns_match_name(q, end);
    if (after == NULL || after + 4 > end) return;
    uint16_t qtype = (uint16_t)((after[0] << 8) | after[1]);
    if (qtype != 1 && qtype != 255) return;                 /* jen A nebo ANY */
    /* >> QCLASS SE CTE (audit F-0061). Driv se overoval jen QTYPE, prestoze
     * `after + 4 > end` uz misto na QCLASS vyhradilo — odpovidalo se tedy i na dotaz
     * s jinou tridou nez IN. Nejvyssi bit je QU (unicast-response request, RFC 6762
     * §5.4), takze se maskuje. */
    uint16_t qclass = (uint16_t)(((after[2] << 8) | after[3]) & 0x7FFFu);
    if (qclass != 1u && qclass != 255u) return;             /* jen IN nebo ANY */

    /* >> OMEZENI TEMPA (audit F-0061). Bez nej mohl kdokoli na LAN dotazy na 5353
     * rozesilat odpovedi do multicast skupiny (zesileni ~2x). Reflexni utok na treti
     * stranu z toho nesel — odpovida se na MULTICAST, ne na odesilatele — ale zatezovat
     * sit vlastnimi pakety v tempu cizich dotazu je zbytecne.
     * ⚠️ 250 ms je kompromis: prohlizec pri hledani `gpsdo.local` posle 2-3 dotazy
     * rychle za sebou a na prvni se odpovi hned, takze se to na pouzitelnosti neprojevi.
     * ⚠️ Plna konformita s RFC 6762 (probing, detekce konfliktu jmena, nahodny rozptyl)
     * je vlastni projekt a vedome se nedela — modul je v hlavicce oznaceny jako
     * best-effort. */
    {
        static uint32_t s_mdns_next_ms;
        uint32_t now = HAL_GetTick();
        if (s_mdns_next_ms != 0u && (int32_t)(now - s_mdns_next_ms) < 0) return;
        s_mdns_next_ms = now + 250u;
        if (s_mdns_next_ms == 0u) s_mdns_next_ms = 1u;       /* 0 znamena "jeste nikdy" */
    }

    uint32_t ip = netif_ip4_addr(&s_netif)->addr;           /* sitove poradi bajtu */
    if (ip == 0u) return;                                   /* jeste nemame IP */

    /* Sestav odpoved: hlavicka + 1 answer (A). */
    uint8_t r[64]; size_t o = 0;
    r[o++]=0; r[o++]=0;                    /* id = 0 (mDNS) */
    r[o++]=0x84; r[o++]=0x00;              /* flags: response + authoritative */
    r[o++]=0; r[o++]=0;                    /* qdcount = 0 */
    r[o++]=0; r[o++]=1;                    /* ancount = 1 */
    r[o++]=0; r[o++]=0;                    /* nscount */
    r[o++]=0; r[o++]=0;                    /* arcount */
    /* Answer name: gpsdo.local. */
    r[o++]=(uint8_t)strlen(MDNS_HOST); memcpy(&r[o], MDNS_HOST, strlen(MDNS_HOST)); o+=strlen(MDNS_HOST);
    r[o++]=5; memcpy(&r[o], "local", 5); o+=5;
    r[o++]=0;
    r[o++]=0; r[o++]=1;                    /* TYPE A */
    r[o++]=0x80; r[o++]=1;                 /* CLASS IN + cache-flush */
    r[o++]=0; r[o++]=0; r[o++]=0; r[o++]=120;   /* TTL 120 s */
    r[o++]=0; r[o++]=4;                    /* RDLENGTH */
    memcpy(&r[o], &ip, 4); o+=4;           /* RDATA = IP (uz sitove poradi) */

    struct pbuf *out = pbuf_alloc(PBUF_TRANSPORT, (u16_t)o, PBUF_RAM);
    if (out == NULL) return;
    memcpy(out->payload, r, o);
    ip_addr_t mc; IP_ADDR4(&mc, 224, 0, 0, 251);
    (void)udp_sendto(pcb, out, &mc, 5353);
    pbuf_free(out);
}

/* Vstup do multicast skupiny (idempotentni; volat pri UP). MAC filtr se nastavuje
 * jednou v initu (PassAllMulticast). */
static void mdns_join(void)
{
    if (s_mdns == NULL || s_mdns_joined) return;
    ip_addr_t mc; IP_ADDR4(&mc, 224, 0, 0, 251);
    if (igmp_joingroup_netif(&s_netif, ip_2_ip4(&mc)) == ERR_OK) s_mdns_joined = 1u;
}

/* ── Callback zmeny linky: DHCP se startuje AZ pri UP a zastavuje pri DOWN.
 * Bez toho by DHCP klient posilal DISCOVER do odpojeneho kabelu a po pripojeni
 * cekal na svuj backoff (lwIP zvedá interval az na desitky sekund). */
static void link_changed(struct netif *netif)
{
    if (netif_is_link_up(netif)) {
        dhcp_start(netif);
        mdns_join();            /* v12: (re)vstup do multicast skupiny pro `gpsdo.local` */
    } else {
        dhcp_stop(netif);
        /* Zahod adresu — jinak by UI dal ukazovalo IP, kterou uz nemame. */
        netif_set_addr(netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
        s_mdns_joined = 0u;     /* pri dalsim UP se pripojime znovu */
    }
}

void lwip_app_init(void)
{
    ip4_addr_t any;
    ip4_addr_set_zero(&any);        /* vse z DHCP */

    lwip_init();

    /* `ethernet_input` = vstupni bod pro ethernetove ramce (NO_SYS varianta). */
    if (netif_add(&s_netif, &any, &any, &any, NULL, &ethernetif_init, &ethernet_input) == NULL) {
        return;                     /* degradovane: bez rozhrani, CM4 bezi dal */
    }
    netif_set_default(&s_netif);
    netif_set_link_callback(&s_netif, link_changed);

    /* SCPI/TCP 5025 (F6/W3): naslouchat muze uz ted — `tcp_bind/listen` je cistě
     * stavova operace lwIP stacku, ETH hardware ani link nepotrebuje. Spojeni proste
     * nikdo nenaváže, dokud nebude IP (DHCP) a funkcni driver, ale server je pripraven
     * hned, ne az po prvnim uspesnem DHCP. */
    scpi_tcp_init();
    httpd_min_init();   /* HTTP port 80 (W4) — stejny duvod: pripraveny hned, nezavisi na linku */

    /* `gState` je RESET jen kdyz MX_ETH_Init neproslo — presne ten pripad, kdy
     * `low_level_init` skoncil pred registraci IO callbacku PHY driveru. */
    s_if_ok = (heth.gState != HAL_ETH_STATE_RESET) ? 1u : 0u;
    if (!s_if_ok) {
        ipc_cm4_set_net(0u, 0u, 0u, 0u);   /* at CM7 vi, ze sit nejede (a proc, viz eth_init_ok) */
        return;
    }

    /* v12 (#2): mDNS — MAC musi prijmout multicast (jinak 224.0.0.251 spadne uz na
     * filtru), pak UDP pcb na 5353. Vstup do skupiny az pri link UP (mdns_join).
     * ⚠️ Vse best-effort: kdyz kterykoli krok selze, CM4 bezi dal bez mDNS. */
    {
        ETH_MACFilterConfigTypeDef fc = {0};
        if (HAL_ETH_GetMACFilterConfig(&heth, &fc) == HAL_OK) {
            fc.PassAllMulticast = ENABLE;
            (void)HAL_ETH_SetMACFilterConfig(&heth, &fc);
        }
        s_mdns = udp_new_ip_type(IPADDR_TYPE_V4);
        if (s_mdns != NULL) {
            if (udp_bind(s_mdns, IP4_ADDR_ANY, 5353) == ERR_OK) {
                udp_recv(s_mdns, mdns_recv, NULL);
            } else { udp_remove(s_mdns); s_mdns = NULL; }
        }
    }

    /* `ethernetif_init` uz zavolal `ethernet_link_check_state`, takze kdyz byl
     * kabel v pri startu, je link uz UP — a callback se tim padem NEspusti.
     * Dorovnej to rucne, jinak by DHCP nikdy nezacalo. */
    if (netif_is_link_up(&s_netif)) {
        dhcp_start(&s_netif);
        mdns_join();
    }
    s_link_next_ms = HAL_GetTick() + LINK_CHECK_MS;
}

/* Rychlost/duplex se ctou z MAC konfigurace (nastavil ji `ethernet_link_check_state`
 * podle vyjednani PHY) — nemusime tak sahat do vnitrku ethernetif.c. */
static void publish_state(void)
{
    uint8_t  link = netif_is_link_up(&s_netif) ? 1u : 0u;
    uint8_t  speed = 0u, duplex = 0u;
    uint32_t ip = 0u;

    if (link) {
        ETH_MACConfigTypeDef c = {0};
        if (HAL_ETH_GetMACConfig(&heth, &c) == HAL_OK) {
            speed  = (c.Speed == ETH_SPEED_100M) ? 100u : 10u;
            duplex = (c.DuplexMode == ETH_FULLDUPLEX_MODE) ? 1u : 0u;
        }
        /* lwIP drzi adresu v sitovem poradi bajtu -> na ARM (little-endian) je
         * bajt0 uz prvni oktet, coz je presne format `net_ip` v IPC. */
        ip = netif_ip4_addr(&s_netif)->addr;
    }

    if (link != s_pub_link || speed != s_pub_speed ||
        duplex != s_pub_duplex || ip != s_pub_ip) {
        ipc_cm4_set_net(link, speed, duplex, ip);
        s_pub_link = link; s_pub_speed = speed; s_pub_duplex = duplex; s_pub_ip = ip;
    }
}

void lwip_app_process(void)
{
    uint32_t now;

    /* ETH nenabehlo -> nic nedelej. Viz `s_if_ok` (jinak HardFault v LAN8742_GetLinkState). */
    if (!s_if_ok) return;

    now = HAL_GetTick();

    /* 1) Vyber prijate ramce (zero-copy, pbuf z RX poolu v SRAM3). */
    ethernetif_input(&s_netif);

    /* 2) lwIP timery — tady bezi DHCP stavovy automat, ARP a TCP retransmise. */
    sys_check_timeouts();

    /* 3) Stav linky (kabel ven/dovnitr) + publikace do CM7. */
    if ((int32_t)(now - s_link_next_ms) >= 0) {
        s_link_next_ms = now + LINK_CHECK_MS;
        ethernet_link_check_state(&s_netif);
        publish_state();
    }
}

/* ⚠️ `lwip_app_has_ip()` / `lwip_app_ip()` ODSTRANENY (revize 2026-08-26): nikdo
 * je nikdy nevolal. Stav linky i adresa se ven dostavaji `publish_state()` ->
 * `ipc_cm4_set_net()` do IPC, odkud je cte CM7 (System Health, UART `status`)
 * i web. Druha cesta ke stejnemu udaji by se jen mohla rozejit. */

/* ══ Webhook alarmu pres sit (TODO #249, v0.41.0) ═══════════════════════════════════════════════════════════════════════════
 * CM4 vidi ve snapshotu (5 Hz) pocitadla alarmu z CM7 (`alarm_*`); kdyz nektere vzroste a webhook je zapnuty (IPC blok `push`,
 * nastavuje CM7 prikazem `alarmpush`), posle HTTP POST s JSON na IPv4:port/cesta (alarm_push.h). NEBLOKUJE: raw lwIP TCP, kazdy
 * krok je callback nebo kratky `tick`. Jedno spojeni najednou, nejvyse jedna udalost v letu, ostatni cekaji v bitove masce.
 *   - Pokus: connect -> POST -> prvni radek odpovedi "HTTP/1.x 2xx" = OK. Spojeni se po odpovedi ZRUSI (tcp_abort = RST) a nezavira
 *     se pres FIN: aktivni zavreni by nechalo pcb ~120 s v TIME_WAIT a vycerpalo by 10 PCB v poolu (http 5 + scpi 4 + tento).
 *   - Chyba/timeout (4 s) -> do 3 pokusu po 5 s, pak udalost zahozena (`failed`++), aby zavesena cilova adresa neblokovala vse.
 *   - Prvni snapshot po startu jen nastavi vychozi hodnoty pocitadel (zadna lavina po restartu); pokles pocitadla (restart CM7)
 *     znamena nove vychozi hodnoty, ne udalost. Pocitadla ve snapshotu jsou uint16 saturovana, rozdil se pocita modulo 2^16.
 *   - Vysledky jsou v g_ipc.push (sent/failed/last/pending), cte je CM7 (`alarmpush`). */
#include "lwip/tcp.h"
#include "../../../CM7/Core/Inc/alarm_push.h"

#define APUSH_TMO_MS     4000u
#define APUSH_RETRY_MS   5000u
#define APUSH_TRIES      3u
#define APUSH_EV_REAL    6u            /* pocitadla ve snapshotu: fpga, gps, limit, vbat, ocxo, adev */

static struct tcp_pcb *s_ap_pcb;
static uint8_t   s_ap_busy;            /* 1 = spojeni ve vzduchu */
static uint8_t   s_ap_ev;              /* udalost ve vzduchu */
static uint8_t   s_ap_tries;
static uint8_t   s_ap_base;            /* 1 = vychozi hodnoty pocitadel nactene */
static uint16_t  s_ap_pending;
static uint8_t   s_ap_cm7_prev;        /* 1 = CM7 zil v minulem ticku */
static uint8_t   s_ap_cm7_down_sent;   /* 1 = "cm7_stalled" bylo (alespon) zarazeno -> po obnove posli "cm7_recovered" */
static uint16_t  s_ap_cm7_cnt;
static uint16_t  s_ap_last[APUSH_EV_REAL];
static uint16_t  s_ap_cnt[APUSH_EV_N];
static uint16_t  s_ap_test_seen;
static uint32_t  s_ap_t0, s_ap_next_ms;
static char      s_ap_req[320];
static int       s_ap_len;
static uint16_t  s_ap_sent, s_ap_failed;

static void ap_publish(uint8_t last)
{
    g_ipc.push.sent    = s_ap_sent;
    g_ipc.push.failed  = s_ap_failed;
    g_ipc.push.pending = s_ap_pending;
    if (last) { g_ipc.push.last = last; g_ipc.push.last_ms = HAL_GetTick(); }
}

/* tcp_abort() zavola u pcb i err callback (ERR_ABRT). Proto se callbacky nejdriv odregistruji, jinak by se vysledek zapocital
 * dvakrat (jednou z err callbacku, podruhe z volajiciho). */
static void ap_kill(struct tcp_pcb *pcb)
{
    tcp_err(pcb, NULL); tcp_recv(pcb, NULL); tcp_sent(pcb, NULL); tcp_poll(pcb, NULL, 0);
    tcp_abort(pcb);
}

/* Konec pokusu o udalost `s_ap_ev` s vysledkem `res` (spojeni uz je zruseno/zavrene volajicim). */
static void ap_finish(uint8_t res)
{
    s_ap_pcb = NULL;
    s_ap_busy = 0u;
    if (res == APUSH_RES_OK) {
        s_ap_pending &= (uint16_t)~(1u << s_ap_ev);
        s_ap_sent++;
        s_ap_tries = 0u;
        s_ap_next_ms = HAL_GetTick();                       /* dalsi cekajici udalost hned */
    } else {
        if (++s_ap_tries >= APUSH_TRIES) {                  /* vycerpano -> zahodit, nezdrzovat dalsi */
            s_ap_pending &= (uint16_t)~(1u << s_ap_ev);
            s_ap_failed++;
            s_ap_tries = 0u;
        }
        s_ap_next_ms = HAL_GetTick() + APUSH_RETRY_MS;
    }
    ap_publish(res);
}

static void ap_err_cb(void *arg, err_t err)
{
    (void)arg; (void)err;
    /* lwIP pcb uz uvolnil (reset, odmitnuti): NESAHAT na nej. */
    ap_finish(APUSH_RES_CONNECT);
}

static err_t ap_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (p == NULL) {                                   /* protistrana zavrela bez (uplne) odpovedi */
        ap_kill(pcb);
        ap_finish(APUSH_RES_HTTP);
        return ERR_ABRT;
    }
    if (err != ERR_OK) { pbuf_free(p); return err; }
    /* "HTTP/1.x 2xx": minimalne 12 znaku v prvnim pbuf (odpoved na webhook je kratka; jinak se bere jako chyba HTTP) */
    uint8_t res = APUSH_RES_HTTP;
    if (p->len >= 12u) {
        const char *s = (const char *)p->payload;
        if (s[0] == 'H' && s[1] == 'T' && s[2] == 'T' && s[3] == 'P' && s[4] == '/' && s[9] == '2') res = APUSH_RES_OK;
    }
    pbuf_free(p);
    ap_kill(pcb);                                    /* viz hlavicka bloku: bez TIME_WAIT */
    ap_finish(res);
    return ERR_ABRT;
}

static err_t ap_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK) { ap_kill(pcb); ap_finish(APUSH_RES_CONNECT); return ERR_ABRT; }
    tcp_recv(pcb, ap_recv_cb);
    if (tcp_write(pcb, s_ap_req, (u16_t)s_ap_len, TCP_WRITE_FLAG_COPY) != ERR_OK || tcp_output(pcb) != ERR_OK) {
        ap_kill(pcb);
        ap_finish(APUSH_RES_MEM);
        return ERR_ABRT;
    }
    return ERR_OK;
}

/* Spusti odeslani udalosti `ev` (volat jen kdyz !s_ap_busy). */
static void ap_start(unsigned ev, const uint8_t ip[4], uint16_t port, const char *path, uint32_t uptime_s)
{
    s_ap_len = apush_build_request(s_ap_req, sizeof s_ap_req, ip, port, path, ev, s_ap_cnt[ev], uptime_s);
    s_ap_ev = (uint8_t)ev;
    if (s_ap_len < 0) { ap_finish(APUSH_RES_MEM); return; }
    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL) { ap_finish(APUSH_RES_MEM); return; }
    ip_addr_t dst;
    IP4_ADDR(&dst, ip[0], ip[1], ip[2], ip[3]);
    tcp_err(pcb, ap_err_cb);
    s_ap_pcb = pcb;
    s_ap_busy = 1u;
    s_ap_t0 = HAL_GetTick();
    if (tcp_connect(pcb, &dst, port, ap_connected_cb) != ERR_OK) {
        s_ap_busy = 0u;
        ap_kill(pcb);
        ap_finish(APUSH_RES_MEM);
    }
}

/* Vola main.c z POMALE casti smycky (5 Hz) s platnym snapshotem. */
/* `snap` = NULL, kdyz CM7 nepublikuje (zamrzl / jeste nenabehl): pocitadla alarmu se pak nevyhodnocuji, ale hlidac CM7 ano. */
void lwip_app_push_tick(const ipc_snapshot_t *snap, uint32_t now)
{
    /* nastaveni ze sdileneho bloku (seqlock CM7, omezeny pocet pokusu) */
    uint8_t en = 0u, ip[4] = {0}, cfg_ok = 0u; uint16_t port = 80u, test_seq = 0u; char path[APUSH_PATH_MAX];
    for (int tr = 0; tr < 4; tr++) {
        uint32_t g1 = g_ipc.push.gen;
        if (g1 & 1u) continue;
        __DMB();
        en = g_ipc.push.en; port = g_ipc.push.port; test_seq = g_ipc.push.test_seq;
        for (int i = 0; i < 4; i++) ip[i] = g_ipc.push.ip[i];
        for (unsigned i = 0; i < APUSH_PATH_MAX; i++) path[i] = g_ipc.push.path[i];
        __DMB();
        if (g_ipc.push.gen == g1) { cfg_ok = 1u; break; }
    }
    if (!cfg_ok) return;
    path[APUSH_PATH_MAX - 1u] = '\0';

    /* hlidac CM7 (TODO #250): hrany "snapshot publikuje" -> "nepublikuje" a zpet. Prvni zivy CM7 po startu se NEhlasi
     * (s_ap_cm7_prev startuje na 0 a "cm7_recovered" se posila jen po drive zarazenem "cm7_stalled"). */
    {   int e = apush_cm7_edge(&s_ap_cm7_prev, &s_ap_cm7_down_sent, snap ? 1u : 0u);
        if (e == APUSH_EV_CM7_STALL) {
            s_ap_cm7_cnt++;
            if (en) { s_ap_pending |= (uint16_t)(1u << APUSH_EV_CM7_STALL); s_ap_cnt[APUSH_EV_CM7_STALL] = s_ap_cm7_cnt; }
        } else if (e == APUSH_EV_CM7_OK) {
            s_ap_pending &= (uint16_t)~(1u << APUSH_EV_CM7_STALL);       /* stihl se obnovit driv, nez se zprava odeslala */
            if (en) { s_ap_pending |= (uint16_t)(1u << APUSH_EV_CM7_OK); s_ap_cnt[APUSH_EV_CM7_OK] = s_ap_cm7_cnt; }
        }
    }

    /* hrany pocitadel alarmu */
    if (snap) {
    const uint16_t cur[APUSH_EV_REAL] = { snap->alarm_fpga_lost, snap->alarm_gps_lost, snap->alarm_limit_fail,
                                          snap->alarm_vbat, snap->alarm_ocxo, snap->alarm_adev };
    if (!s_ap_base) {
        for (unsigned i = 0; i < APUSH_EV_REAL; i++) s_ap_last[i] = cur[i];
        s_ap_test_seen = test_seq;
        s_ap_base = 1u;
    } else {
        for (unsigned i = 0; i < APUSH_EV_REAL; i++) {
            uint16_t d = (uint16_t)(cur[i] - s_ap_last[i]);
            if (d != 0u && d < 0x8000u && en) { s_ap_pending |= (uint16_t)(1u << i); s_ap_cnt[i] = cur[i]; }
            s_ap_last[i] = cur[i];                    /* pokles = restart CM7 -> nove vychozi hodnoty */
        }
        if (test_seq != s_ap_test_seen) {
            s_ap_test_seen = test_seq;
            s_ap_pending |= (uint16_t)(1u << APUSH_EV_TEST); s_ap_cnt[APUSH_EV_TEST] = test_seq;
        }
    }
    }   /* if (snap) */
    if (!en) s_ap_pending &= (uint16_t)(1u << APUSH_EV_TEST);    /* vypnuto: cekajici alarmy pryc, testovaci ev zustane */

    /* timeout rozdelaneho spojeni */
    if (s_ap_busy && (int32_t)(now - s_ap_t0) > (int32_t)APUSH_TMO_MS) {
        if (s_ap_pcb) ap_kill(s_ap_pcb);
        ap_finish(APUSH_RES_TIMEOUT);
    }

    /* dalsi udalost */
    if (!s_ap_busy && s_ap_pending && (int32_t)(now - s_ap_next_ms) >= 0) {
        int valid = (ip[0] | ip[1] | ip[2] | ip[3]) != 0u && port != 0u && path[0] == '/';
        if (!netif_is_link_up(&s_netif) || ip4_addr_isany_val(*netif_ip4_addr(&s_netif))) {
            ap_publish(APUSH_RES_NONET);
            s_ap_next_ms = now + APUSH_RETRY_MS;
        } else if (valid) {
            unsigned ev = 0u;
            while (ev < APUSH_EV_N && !(s_ap_pending & (1u << ev))) ev++;
            if (ev < APUSH_EV_N) ap_start(ev, ip, port, path, now / 1000u);
        } else {
            s_ap_pending = 0u;                         /* neplatny cil: nic neposilat */
        }
    }
    ap_publish(0u);
}
