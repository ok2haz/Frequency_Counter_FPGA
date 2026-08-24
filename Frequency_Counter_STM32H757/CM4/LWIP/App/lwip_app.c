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

int lwip_app_has_ip(void)
{
    return (netif_is_up(&s_netif) && netif_ip4_addr(&s_netif)->addr != 0u) ? 1 : 0;
}

uint32_t lwip_app_ip(void)
{
    return netif_ip4_addr(&s_netif)->addr;
}
