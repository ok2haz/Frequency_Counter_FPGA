/* alarm_push.h - webhook alarmu pres sit (TODO #249, v0.41.0). Cista logika bez zavislosti na HAL/lwIP, sdilena CM7 (UART
 * `alarmpush`, nastaveni) a CM4 (odesilani) a testovana na PC (tests/host/t_alarm_push.c).
 *
 * Princip: CM7 vyhodnocuje alarmy (`alarm.c`, pocitadla `alarm_*` ve snapshotu) a drzi nastaveni (URL, zap/vyp) v syscfg;
 * CM4 vidi, ze se pocitadlo zvedlo, a posle HTTP POST s JSON na zadanou IPv4 adresu (raw lwIP, neblokujici). Jen HTTP bez TLS
 * a bez DNS (IPv4 literal): cil ma byt v LAN (ntfy, Home Assistant, vlastni sber). Viz docs/WEB_HTTPS.md pro TLS pres proxy.
 * Odesilani NEZAVISI na mute (g_sound_muted): mute umlcuje jen pipnuti. */
#ifndef ALARM_PUSH_H
#define ALARM_PUSH_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define APUSH_PATH_MAX   48u     /* cesta vcetne zacatecniho '/' a NUL */

/* Vysledek posledniho pokusu o odeslani (ipc_push_t.last). */
enum { APUSH_RES_NONE = 0, APUSH_RES_OK = 1, APUSH_RES_CONNECT = 2, APUSH_RES_TIMEOUT = 3, APUSH_RES_NONET = 4,
       APUSH_RES_HTTP = 5, APUSH_RES_MEM = 6 };

/* Druhy udalosti = indexy pocitadel alarmu ve snapshotu + testovaci udalost. */
enum { APUSH_EV_FPGA = 0, APUSH_EV_GPS, APUSH_EV_LIMIT, APUSH_EV_VBAT, APUSH_EV_OCXO, APUSH_EV_ADEV, APUSH_EV_TEST, APUSH_EV_N };

static inline const char *apush_ev_name(unsigned ev)
{
    static const char *const nm[APUSH_EV_N] = { "fpga_signal_lost", "gps_lock_lost", "limit_fail", "vbat_low", "ocxo_temp", "adev_high", "test" };
    return (ev < APUSH_EV_N) ? nm[ev] : "?";
}

/* "http://a.b.c.d[:port][/cesta]" (predpona "http://" nepovinna). Port 1..65535 (vychozi 80), cesta vychozi "/".
 * Vraci 1 = platne, 0 = chyba. Zadny znak <= ' ' ani > '~' v ceste (zadne CR/LF/mezery = zadna injekce hlavicek),
 * IPv4 jen ctyri desitkove oktety bez vedoucich znaku navic. */
static inline int apush_parse_url(const char *s, uint8_t ip[4], uint16_t *port, char *path, size_t pathmax)
{
    if (!s || !ip || !port || !path || pathmax < 2u) return 0;
    if (s[0] == 'h' && s[1] == 't' && s[2] == 't' && s[3] == 'p' && s[4] == ':' && s[5] == '/' && s[6] == '/') s += 7;
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') return 0;
        unsigned v = 0, nd = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10u + (unsigned)(*s - '0'); s++; if (++nd > 3u || v > 255u) return 0; }
        ip[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    unsigned p = 80u;
    if (*s == ':') {
        s++;
        if (*s < '0' || *s > '9') return 0;
        p = 0;
        unsigned nd = 0;
        while (*s >= '0' && *s <= '9') { p = p * 10u + (unsigned)(*s - '0'); s++; if (++nd > 5u || p > 65535u) return 0; }
        if (p == 0u) return 0;
    }
    *port = (uint16_t)p;
    if (*s == '\0') { path[0] = '/'; path[1] = '\0'; return 1; }
    if (*s != '/') return 0;
    size_t n = 0;
    while (*s) {
        if ((unsigned char)*s <= ' ' || (unsigned char)*s > '~') return 0;
        if (n + 1u >= pathmax) return 0;
        path[n++] = *s++;
    }
    path[n] = '\0';
    return 1;
}

/* Sestavi cely HTTP/1.1 POST s JSON tělem. Vraci delku (bez NUL) nebo -1, kdyz se nevejde do `n`. */
static inline int apush_build_request(char *out, size_t n, const uint8_t ip[4], uint16_t port, const char *path,
                                      unsigned ev, uint16_t count, uint32_t uptime_s)
{
    char body[96];
    int bl = snprintf(body, sizeof body, "{\"device\":\"gpsdo\",\"event\":\"%s\",\"count\":%u,\"uptime_s\":%lu}",
                      apush_ev_name(ev), (unsigned)count, (unsigned long)uptime_s);
    if (bl < 0 || (size_t)bl >= sizeof body) return -1;
    int rl = snprintf(out, n, "POST %s HTTP/1.1\r\nHost: %u.%u.%u.%u:%u\r\nContent-Type: application/json\r\n"
                      "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                      path, (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3], (unsigned)port, bl, body);
    if (rl < 0 || (size_t)rl >= n) return -1;
    return rl;
}

#endif /* ALARM_PUSH_H */
