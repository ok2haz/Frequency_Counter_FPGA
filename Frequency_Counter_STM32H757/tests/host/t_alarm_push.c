/* Host test alarm_push.h (TODO #249): parser URL a sestaveni HTTP POST.
 * cc -std=c11 -O2 -Wall -Wextra -ICM7/Core/Inc tests/host/t_alarm_push.c -o /tmp/t_alarm_push && /tmp/t_alarm_push */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "alarm_push.h"

static int fails;
#define CK(c) do { if (!(c)) { printf("FAIL radek %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int parse(const char *s, uint8_t ip[4], uint16_t *port, char *path)
{
    return apush_parse_url(s, ip, port, path, APUSH_PATH_MAX);
}

int main(void)
{
    uint8_t ip[4]; uint16_t port; char path[APUSH_PATH_MAX];

    /* platne */
    CK(parse("http://10.0.0.5:8080/hook", ip, &port, path) && ip[0] == 10 && ip[3] == 5 && port == 8080 && !strcmp(path, "/hook"));
    CK(parse("192.168.1.20", ip, &port, path) && ip[0] == 192 && ip[2] == 1 && port == 80 && !strcmp(path, "/"));
    CK(parse("http://1.2.3.4/a/b?x=1", ip, &port, path) && port == 80 && !strcmp(path, "/a/b?x=1"));
    CK(parse("10.0.0.5:65535", ip, &port, path) && port == 65535);
    CK(parse("0.0.0.0:1/", ip, &port, path) && port == 1);

    /* neplatne */
    CK(!parse("", ip, &port, path));
    CK(!parse("http://", ip, &port, path));
    CK(!parse("10.0.0", ip, &port, path));
    CK(!parse("10.0.0.256", ip, &port, path));
    CK(!parse("10.0.0.1:0", ip, &port, path));
    CK(!parse("10.0.0.1:65536", ip, &port, path));
    CK(!parse("10.0.0.1:", ip, &port, path));
    CK(!parse("10.0.0.1:80x", ip, &port, path));
    CK(!parse("gpsdo.lan/x", ip, &port, path));          /* DNS neni podporovano */
    CK(!parse("10.0.0.1x", ip, &port, path));
    CK(!parse("10.0.0.1/a b", ip, &port, path));         /* mezera v ceste */
    CK(!parse("10.0.0.1/a\r\nX: y", ip, &port, path));   /* injekce hlavicky */
    CK(!parse("10.0.0.1/\x7f", ip, &port, path));
    CK(!parse("1111.0.0.1", ip, &port, path));
    CK(!parse(NULL, ip, &port, path));
    {   /* delka cesty: max APUSH_PATH_MAX-1 znaku vcetne '/' */
        char u[160];
        strcpy(u, "10.0.0.1/");
        memset(u + 9, 'a', APUSH_PATH_MAX - 2u); u[9 + APUSH_PATH_MAX - 2u] = '\0';   /* 1 + 46 = 47 znaku = vejde */
        CK(parse(u, ip, &port, path) && strlen(path) == APUSH_PATH_MAX - 1u);
        u[9 + APUSH_PATH_MAX - 2u] = 'a'; u[9 + APUSH_PATH_MAX - 1u] = '\0';          /* 48 znaku = nevejde */
        CK(!parse(u, ip, &port, path));
    }

    /* sestaveni pozadavku */
    {
        char rq[320];
        uint8_t a[4] = { 10, 0, 0, 5 };
        int n = apush_build_request(rq, sizeof rq, a, 8080, "/hook", APUSH_EV_VBAT, 3, 1234);
        CK(n > 0 && (size_t)n == strlen(rq));
        CK(strncmp(rq, "POST /hook HTTP/1.1\r\nHost: 10.0.0.5:8080\r\n", 42) == 0);
        const char *body = strstr(rq, "\r\n\r\n");
        CK(body != NULL);
        if (body) {
            body += 4;
            CK(!strcmp(body, "{\"device\":\"gpsdo\",\"event\":\"vbat_low\",\"count\":3,\"uptime_s\":1234}"));
            const char *cl = strstr(rq, "Content-Length: ");
            CK(cl && atoi(cl + 16) == (int)strlen(body));    /* Content-Length musi sedet na delku tela */
        }
        CK(strstr(rq, "Connection: close\r\n") != NULL);
        char tiny[40];
        CK(apush_build_request(tiny, sizeof tiny, a, 80, "/", APUSH_EV_TEST, 0, 0) == -1);   /* nevejde se */
        for (unsigned e = 0; e < APUSH_EV_N; e++) {
            CK(apush_build_request(rq, sizeof rq, a, 80, "/", e, 65535, 4294967295u) > 0);
            CK(strcmp(apush_ev_name(e), "?") != 0);
        }
        CK(!strcmp(apush_ev_name(APUSH_EV_N), "?"));
    }

    /* hlidac CM7 (TODO #250) */
    {
        uint8_t prev = 0, ds = 0;
        CK(apush_cm7_edge(&prev, &ds, 0) == -1);                          /* CM7 jeste nenabehl: ticho */
        CK(apush_cm7_edge(&prev, &ds, 1) == -1);                          /* prvni zivy CM7 po startu: nehlasit */
        CK(apush_cm7_edge(&prev, &ds, 1) == -1);
        CK(apush_cm7_edge(&prev, &ds, 0) == APUSH_EV_CM7_STALL);          /* zamrzl */
        CK(apush_cm7_edge(&prev, &ds, 0) == -1);                          /* porad stoji: neopakovat */
        CK(apush_cm7_edge(&prev, &ds, 1) == APUSH_EV_CM7_OK);             /* obnoven */
        CK(apush_cm7_edge(&prev, &ds, 1) == -1);
        CK(apush_cm7_edge(&prev, &ds, 0) == APUSH_EV_CM7_STALL);          /* znovu */
        CK(APUSH_EV_N <= 16);                                              /* maska `pending` je uint16 */
    }

    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
