/**
  ******************************************************************************
  * @file    httpd_min.h
  * @brief   Minimalni HTTP server (port 80) — `GET /api/state` (JSON) +
  *          `POST /api/scpi` (SCPI pres HTTP). W4 / STATUS.md #26.
  ******************************************************************************
  */
#ifndef __HTTPD_MIN_H__
#define __HTTPD_MIN_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Otevre naslouchajici socket na portu 80. Vola se JEDNOU z `lwip_app_init()`,
 *  stejne jako `scpi_tcp_init()` — nezavisi na stavu linky/DHCP. */
void httpd_min_init(void);

/** Periodicka obsluha (v12): dokonci ODLOZENE odpovedi `/api/log` (cekaji na
 *  data z CM7 pres IPC datalog kanal) a posle SSE udalosti do drzenych
 *  `/api/stream` spojeni. Vola se z hlavni smycky CM4 (rychla cast, ~1 ms). */
void httpd_min_poll(void);

/* ── Pure-logic cast (zadne site, testovatelne na cili) ─────────────────────── */

typedef struct {
    char   method[8];        /* "GET", "POST", ... (oriznuto, vzdy 0-terminovano) */
    char   path[80];         /* "/api/state" apod. (oriznuto, vzdy 0-terminovano) */
    long   content_length;   /* -1 = hlavicka chybi/nejde precist */
    size_t header_len;       /* bajtu az VCETNE prazdneho radku za hlavickami */
    char   auth_b64[64];     /* base64 z "Authorization: Basic <...>"; "" = chybi */
} http_req_t;

/** Rozparsuje request-line + hlavicky z bufferu prijateho DOSUD (muze byt
 *  neuplny). @return 1 = hlavicky kompletni (naslo se \r\n\r\n, `out` platny),
 *  0 = potreba vic dat, -1 = spatny request-line (chybi mezera/metoda/cesta). */
int httpd_parse_request(const char *buf, size_t len, http_req_t *out);

/** Pure-logic selftest (par vektoru na `httpd_parse_request`). @return 1 = PASS. */
int httpd_min_selftest(void);

#ifdef __cplusplus
}
#endif
#endif /* __HTTPD_MIN_H__ */
