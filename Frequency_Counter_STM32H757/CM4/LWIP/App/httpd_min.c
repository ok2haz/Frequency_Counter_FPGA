/**
  ******************************************************************************
  * @file    httpd_min.c
  * @brief   Minimalni HTTP server na CM4 (port 80) — raw lwIP API, NO_SYS=1.
  *
  * VLASTNI, ne vendorovany lwIP `httpd`/`fs.c` — stejny duvod jako hand-rolled
  * SCPI (WEB_UI_PLAN.md §1.3): `makefsdata` by pridal krok do buildu, ktery uz
  * dela potize, a CGI/SSI je pro REST nemotorne.
  *
  * `GET /`           — SPA (HTML+CSS+JS pohromade, `.rodata`, zadne externi zdroje).
  * `GET /api/state`  — JSON ze ZIVEHO snapshotu, pres `scpi_src_t` (stejna
  *                     abstrakce jako SCPI) -> stejna validita, ne druha logika.
  * `POST /api/scpi`  — telo = jeden radek SCPI, projde TIMZ `scpi_process_ctx`
  *                     jako TCP 5025 (scpi_tcp.c) — zadna druha ovladaci sada.
  *
  * ⚠️ ASYNCHRONNI ODESILANI (W5, na rozdil od W3/W4): SPA stranka muze byt vetsi
  * nez `TCP_SND_BUF` (~5,8 kB), takze uz nestaci jeden `tcp_write`. Kazde spojeni
  * ma vlastni frontu (hlavicka + telo, `pump_send` + `tcp_sent` callback) — telo
  * pro JSON/SCPI je VZDY v connection-owned bufferu (`c->bodybuf`), pro SPA
  * ukazuje primo do staticke `.rodata` konstanty (bezpecne sdilet mezi spojenimi,
  * nikdy se nemeni). Sdileny staticky scratch buffer (jako ve W4) by NEFUNGOVAL —
  * vice spojeni muze mit rozeslani rozestavane soucasne.
  *
  * AUTENTIZACE (W0+W5): `web_ctrl_en` (okno PRISTUP) je HLAVNI vypinac zapisu
  * pro OBA transporty (TCP 5025 i HTTP). Jmeno+heslo (HTTP Basic) je DALSI
  * podminka navic, kterou pridava jen HTTP — TCP 5025 nema koncept hlavicek,
  * takze si vystaci s vypinacem (viz `scpi_tcp.c`). `GET /api/state` zustava
  * VZDY otevrene (cteni je nezkodne) — auth se kontroluje jen pred pripojenim
  * `set_cfg` v `POST /api/scpi`, a pri nespravnem/chybejicim heslu se `set_cfg`
  * proste nepripoji (existujici NULL-guard v parseru, -230), zadna nova
  * chybova cesta ani unik informace "spatne heslo" vs. "ovladani vypnute".
  ******************************************************************************
  */
#include "httpd_min.h"

#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "scpi.h"
#include "ipc_cm4.h"
#include "main.h"          /* HAL_GetTick */

#define HTTPD_PORT          80u
#define HTTPD_MAX_CONN      5u      /* v12: +2 kvuli drzenym SSE spojenim (/api/stream) */
#define HTTPD_RXBUF_MAX     700u    /* hlavicky bezneho prohlizece (vc. Authorization) + kratke telo */
#define HTTPD_HDRBUF_MAX    160u    /* nase VYSTUPNI HTTP hlavicka (mala, pevna) */
#define HTTPD_BODYBUF_MAX   4096u   /* JSON ze snapshotu / SCPI / v12 dlouha historie /api/log (48 bodu) */
#define HTTPD_BODY_MAX       96u    /* max. PRIJATE telo POST /api/scpi (jeden radek SCPI) */
#define HTTPD_AUTH_MAX       64u    /* base64("user:pass"), 16+1+20 B -> base64 ~50 znaku */

/* v12: rezim odlozene/drzene odpovedi (viz httpd_min_poll). */
#define HCONN_NORMAL   0u   /* jednorazova odpoved (SPA/state/scpi/sats) */
#define HCONN_LOG      1u   /* ceka na data z CM7 (IPC datalog kanal) -> pak odpovi a zavre */
#define HCONN_SSE      2u   /* drzene SSE spojeni (/api/stream) — posila udalosti dokud zije */

typedef struct {
    struct tcp_pcb *pcb;    /* NULL = slot volny */
    char     rxbuf[HTTPD_RXBUF_MAX];
    uint16_t rxlen;
    uint8_t  dispatched;    /* 1 = odpoved uz je zafronteovana/odeslana */

    /* Odchozi fronta — hlavicka a telo se posilaji po kouscich (viz komentar
     * u souboru). `body_ptr` je BUD `bodybuf` (JSON/SCPI, connection-owned),
     * NEBO staticka `.rodata` konstanta (SPA stranka, sdilena bezpecne). */
    char     hdr[HTTPD_HDRBUF_MAX];
    size_t   hdr_len, hdr_sent;
    const char *body_ptr;
    size_t   body_len, body_sent;
    char     bodybuf[HTTPD_BODYBUF_MAX];

    /* v12: odlozene (/api/log) a drzene (/api/stream) rezimy. */
    uint8_t  mode;          /* HCONN_* */
    uint32_t defer_gen;     /* LOG: req_gen, na jehoz odpoved cekame */
    uint32_t defer_ms;      /* LOG: deadline; SSE: cas posledniho pushe */
    uint32_t sse_seq;       /* SSE: posledni odeslany seq_meas */
} http_conn_t;

static http_conn_t s_hconn[HTTPD_MAX_CONN];

/* ── Pure-logic parser (viz httpd_min.h) ─────────────────────────────────────── */

static int ci_eq_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

int httpd_parse_request(const char *buf, size_t len, http_req_t *out)
{
    if (buf == NULL || out == NULL) return -1;
    memset(out, 0, sizeof *out);
    out->content_length = -1;

    /* Hlavicky kompletni, az kdyz je videt prazdny radek. Bez nej = malo dat. */
    const char *hdr_end = NULL;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
            hdr_end = &buf[i]; break;
        }
    }
    if (hdr_end == NULL) return 0;                 /* potreba vic dat */
    out->header_len = (size_t)(hdr_end - buf) + 4u;

    /* Request-line: "METODA CESTA HTTP/1.x". Rucni tokenizace — zadny sscanf(%s)
     * bez omezeni delky. */
    const char *p = buf, *end = hdr_end;
    const char *sp1 = memchr(p, ' ', (size_t)(end - p));
    if (sp1 == NULL) return -1;                     /* chybi mezera -> spatny radek */
    size_t mlen = (size_t)(sp1 - p);
    if (mlen == 0 || mlen >= sizeof out->method) return -1;
    memcpy(out->method, p, mlen); out->method[mlen] = '\0';

    p = sp1 + 1;
    const char *sp2 = memchr(p, ' ', (size_t)(end - p));
    if (sp2 == NULL) return -1;                     /* chybi HTTP verze -> spatny radek */
    size_t plen = (size_t)(sp2 - p);
    if (plen == 0) return -1;
    if (plen >= sizeof out->path) plen = sizeof(out->path) - 1u;  /* orizni, neodmitej */
    memcpy(out->path, p, plen); out->path[plen] = '\0';

    /* Hlavicky: "Content-Length:" a "Authorization:" (case-insensitive), po radcich. */
    const char *line = memchr(buf, '\n', (size_t)(end - buf));
    while (line != NULL && line < end) {
        line++;                                     /* za '\n' predchoziho radku */
        const char *nl = memchr(line, '\n', (size_t)(end - line));
        size_t llen = nl ? (size_t)(nl - line) : (size_t)(end - line);
        if (llen > 15 && ci_eq_n(line, "Content-Length:", 15)) {
            long v = atol(line + 15);
            if (v >= 0) out->content_length = v;
        } else if (llen > 14 && ci_eq_n(line, "If-None-Match:", 14)) {
            const char *v = line + 14;
            size_t vlen = llen - 14u;
            while (vlen > 0 && *v == ' ') { v++; vlen--; }
            while (vlen > 0 && (v[vlen-1] == '\r' || v[vlen-1] == ' ')) vlen--;
            if (vlen >= sizeof out->inm) vlen = sizeof(out->inm) - 1u;
            memcpy(out->inm, v, vlen); out->inm[vlen] = '\0';
        } else if (llen > 14 && ci_eq_n(line, "Authorization:", 14)) {
            const char *v = line + 14;
            size_t vlen = llen - 14u;
            while (vlen > 0 && *v == ' ') { v++; vlen--; }        /* preskoc mezery po ':' */
            if (vlen > 6 && ci_eq_n(v, "Basic ", 6)) {
                v += 6; vlen -= 6;
                /* ⚠️ Mezi schematem a tokenem smi byt VIC mezer (RFC 7235: `1*SP`),
                 * takze se musi preskocit znovu — `ci_eq_n(v,"Basic ",6)` sezere
                 * jen jednu. Bez toho zustala v `auth_b64` vedouci mezera, delka
                 * prestala byt nasobkem 4 a `b64_decode` vratil -1 => 401 pro
                 * kazdeho klienta s extra mezerou. Nalezeno HW pruchodem
                 * 2026-08-30 (httpd_min_selftest FAIL, radek assertu 270). */
                while (vlen > 0 && *v == ' ') { v++; vlen--; }
                while (vlen > 0 && (v[vlen-1] == '\r' || v[vlen-1] == ' ')) vlen--;
                if (vlen >= sizeof out->auth_b64) vlen = sizeof(out->auth_b64) - 1u;
                memcpy(out->auth_b64, v, vlen); out->auth_b64[vlen] = '\0';
            }
        }
        line = nl;
    }
    return 1;
}

/* ── Base64 dekoder (jen pro "Authorization: Basic ..."; zadna zavislost). ──── */
static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
/* @return delka dekodovanych bajtu (0-terminovano navic), nebo -1 pri chybe/preteceni. */
static int b64_decode(const char *in, char *out, size_t out_cap)
{
    size_t n = strlen(in);
    if (n == 0 || (n % 4) != 0) return -1;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 4) {
        int pad = (in[i+2] == '=') + (in[i+3] == '=');
        int v0 = b64_val(in[i]), v1 = b64_val(in[i+1]);
        int v2 = (in[i+2] == '=') ? 0 : b64_val(in[i+2]);
        int v3 = (in[i+3] == '=') ? 0 : b64_val(in[i+3]);
        if (v0 < 0 || v1 < 0 || (pad == 0 && (v2 < 0 || v3 < 0))) return -1;
        uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
        if (o >= out_cap - 1u) return -1;
        out[o++] = (char)((triple >> 16) & 0xFFu);
        if (pad < 2) { if (o >= out_cap - 1u) return -1; out[o++] = (char)((triple >> 8) & 0xFFu); }
        if (pad < 1) { if (o >= out_cap - 1u) return -1; out[o++] = (char)(triple & 0xFFu); }
    }
    out[o] = '\0';
    return (int)o;
}

/* ── Diagnostika POSLEDNIHO pokusu o prihlaseni (W5 HW bring-up, 2026-08-23) ──
 * CM4 nema konzoli — bez tohohle nejde zvenci videt, KDE presne auth padla
 * (chybejici hlavicka / spatny base64 / nesedici delka / nesedici obsah).
 * ⚠️ ZAMERNE se nikam neexportuje SUROVY dekodovany retezec ani heslo ze
 * snapshotu — jen delky a bool vysledky, at se pres `/api/state` nedá vytahat
 * platne heslo. Az bude auth na HW overene, tohle se da odstranit (nebo nechat,
 * je to levne a nikdy neprozrazuje tajemstvi). */
static struct {
    uint8_t header_present;   /* 1 = prislo "Authorization: Basic ..." vubec */
    uint8_t decode_ok;        /* 1 = base64 se rozlouskl (spravny tvar) */
    uint8_t match;            /* 1 = dekodovane == ocekavane */
    uint8_t decoded_len;      /* delka dekodovaneho "user:pass" (0 pri chybe) */
    uint8_t expected_len;     /* delka ocekavaneho "user:pass" ze snapshotu */
} s_auth_dbg;

/* Overi "Authorization: Basic base64(user:pass)" proti snapshotu.
 * @return 1 = shoda, 0 = chybi/spatne (bez rozlisovani duvodu KLIENTOVI — zamerne,
 * viz komentar u souboru; podrobnosti PRO NAS jdou do `s_auth_dbg` vyse).
 * Prazdne `web_pass` NIKDY neprojde (presny bajtovy porovnavac, zadny specialni
 * pripad "prazdne heslo = pust vsechny"). */
static int check_auth(const http_req_t *r, const ipc_snapshot_t *snap)
{
    memset(&s_auth_dbg, 0, sizeof s_auth_dbg);

    char want[40];
    int wn = snprintf(want, sizeof want, "%s:%s", snap->web_user, snap->web_pass);
    if (wn > 0 && (size_t)wn < sizeof want) s_auth_dbg.expected_len = (uint8_t)wn;

    if (r->auth_b64[0] == '\0') return 0;
    s_auth_dbg.header_present = 1;

    char dec[40];   /* max "user:pass" = 15+1+19 = 35 B */
    int dn = b64_decode(r->auth_b64, dec, sizeof dec);
    if (dn < 0) return 0;
    s_auth_dbg.decode_ok = 1;
    s_auth_dbg.decoded_len = (uint8_t)dn;

    if (wn <= 0 || (size_t)wn >= sizeof want) return 0;
    int m = (strcmp(dec, want) == 0);
    s_auth_dbg.match = (uint8_t)m;
    return m;
}

/* ── Selftest (bez site) — kriterium W4/W5 z WEB_UI_PLAN.md ──────────────────── */
/* ⚠️ Radek PRVNIHO neuspesneho assertu (0 = zadny). Stejny idiom jako
 * `scpi_selftest_fail_line()` — bez nej je vysledek jen "FAIL" a na cili
 * (kde se test spousti) neni jak zjistit KTERY vektor spadl. Cte se
 * ladici sondou z CM4 SRAM2, nebo pres IPC. */
static int s_ht_fail_line;
int httpd_min_selftest_fail_line(void) { return s_ht_fail_line; }
#define HT_OK(cond) do { int c_ = (cond) ? 1 : 0; if (!c_ && !s_ht_fail_line) s_ht_fail_line = __LINE__; ok &= c_; } while (0)

int httpd_min_selftest(void)
{
    http_req_t r;
    int ok = 1;
    s_ht_fail_line = 0;

    /* 1) Kompletni GET bez tela. */
    {
        static const char req[] = "GET /api/state HTTP/1.1\r\nHost: x\r\n\r\n";
        HT_OK(httpd_parse_request(req, sizeof(req) - 1, &r) == 1);
        HT_OK(strcmp(r.method, "GET") == 0);
        HT_OK(strcmp(r.path, "/api/state") == 0);
        HT_OK(r.content_length == -1);
        HT_OK(r.auth_b64[0] == '\0');
    }
    /* 2) Kompletni POST s Content-Length (case-insensitive nazev hlavicky) +
     *    Authorization Basic (case-insensitive "basic", extra mezery). */
    {
        static const char req[] =
            "POST /api/scpi HTTP/1.1\r\ncontent-length: 11\r\n"
            "Authorization:  basic  dXNlcjpwYXNz  \r\n\r\nMEAS:FREQ?\n";
        HT_OK(httpd_parse_request(req, sizeof(req) - 1, &r) == 1);
        HT_OK(strcmp(r.method, "POST") == 0);
        HT_OK(strcmp(r.path, "/api/scpi") == 0);
        HT_OK(r.content_length == 11);
        HT_OK(strcmp(r.auth_b64, "dXNlcjpwYXNz") == 0);
    }
    /* 2b) If-None-Match (cache SPA): hodnota se precte i s uvozovkami ETagu. */
    {
        static const char req[] =
            "GET / HTTP/1.1\r\nIf-None-Match: \"abc123\"\r\n\r\n";
        HT_OK(httpd_parse_request(req, sizeof(req) - 1, &r) == 1);
        HT_OK(strcmp(r.inm, "\"abc123\"") == 0);
    }
    /* 3) Neuplna hlavicka (zadny prazdny radek jeste) -> "potreba vic dat". */
    {
        static const char req[] = "GET /api/state HTTP/1.1\r\nHost: x";
        HT_OK(httpd_parse_request(req, sizeof(req) - 1, &r) == 0);
    }
    /* 4) Spatny request-line (chybi mezera) -> malformed. */
    {
        static const char req[] = "GARBAGE\r\n\r\n";
        HT_OK(httpd_parse_request(req, sizeof(req) - 1, &r) == -1);
    }
    /* 5) Prilis dlouha cesta se orizne, neshodi parser. */
    {
        char req[200];
        int n = snprintf(req, sizeof req, "GET /%.150s HTTP/1.1\r\n\r\n",
                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        HT_OK(httpd_parse_request(req, (size_t)n, &r) == 1);
        HT_OK(strlen(r.path) == sizeof(r.path) - 1u);
    }
    /* 6) Base64 + porovnani credentials — "dXNlcjpwYXNz" = "user:pass". */
    {
        ipc_snapshot_t snap; memset(&snap, 0, sizeof snap);
        strcpy(snap.web_user, "user"); strcpy(snap.web_pass, "pass");
        http_req_t rr; memset(&rr, 0, sizeof rr);
        strcpy(rr.auth_b64, "dXNlcjpwYXNz");
        HT_OK(check_auth(&rr, &snap) == 1);
        strcpy(snap.web_pass, "jinak");
        HT_OK(check_auth(&rr, &snap) == 0);           /* spatne heslo -> odmitnout */
        rr.auth_b64[0] = '\0';
        HT_OK(check_auth(&rr, &snap) == 0);            /* zadna hlavicka -> odmitnout */
    }
    return ok;
}
#undef HT_OK

/* ── JSON writer: bezpecne orezavane pripojovani do pevneho bufferu ──────────── */
typedef struct { char *p; size_t cap, used; } jbuf_t;

static void jinit(jbuf_t *j, char *buf, size_t cap) { j->p = buf; j->cap = cap; j->used = 0; buf[0] = '\0'; }

static void jputf(jbuf_t *j, const char *fmt, ...)
{
    if (j->used >= j->cap) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(j->p + j->used, j->cap - j->used, fmt, ap);
    va_end(ap);
    if (n > 0) j->used += ((size_t)n < j->cap - j->used) ? (size_t)n : (j->cap - j->used - 1u);
}

/* Cislo nebo `null` podle bitu platnosti — jadro „zlateho pravidla" (viz
 * WEB_UI_PLAN.md 1.7): neplatna hodnota se NIKDY neservíruje jako mereni. */
static void jnum_hz(jbuf_t *j, const char *key, uint32_t valid_bit, uint64_t x100000)
{
    if (!valid_bit) { jputf(j, "\"%s\":null,", key); return; }
    char b[24];
    fmt_scpi_hz_d((double)x100000 / 100000.0, b, sizeof b);
    jputf(j, "\"%s\":%s,", key, b);
}
static void jnum_c100(jbuf_t *j, const char *key, uint32_t valid_bit, int16_t c100)
{
    if (!valid_bit) { jputf(j, "\"%s\":null,", key); return; }
    int neg = (c100 < 0); int v = neg ? -c100 : c100;
    jputf(j, "\"%s\":%s%d.%02d,", key, neg ? "-" : "", v / 100, v % 100);
}
static void jnum_u(jbuf_t *j, const char *key, uint32_t valid_bit, uint32_t v)
{
    if (!valid_bit) { jputf(j, "\"%s\":null,", key); return; }
    jputf(j, "\"%s\":%lu,", key, (unsigned long)v);
}
static void jbool(jbuf_t *j, const char *key, int v) { jputf(j, "\"%s\":%s,", key, v ? "true" : "false"); }

/* Stupne × 1e7 (int) -> desetinne stupne. Cele cislo, zadny %f (newlib-nano). */
static void jnum_e7(jbuf_t *j, const char *key, uint32_t valid_bit, int32_t e7)
{
    if (!valid_bit) { jputf(j, "\"%s\":null,", key); return; }
    int neg = (e7 < 0);
    uint32_t a = (uint32_t)(neg ? -(int64_t)e7 : (int64_t)e7);
    jputf(j, "\"%s\":%s%lu.%07lu,", key, neg ? "-" : "",
          (unsigned long)(a / 10000000u), (unsigned long)(a % 10000000u));
}

/* Sestavi `GET /api/state` JSON ze ZIVEHO snapshotu do `c->bodybuf`.
 * @return delka (0 = snapshot neni k dispozici -> volajici posle 503). */
static size_t build_state_json(char *out, size_t out_sz, const ipc_snapshot_t *snap)
{
    scpi_src_t s;
    if (!ipc_scpi_src_from_snap(&s, snap)) return 0;

    jbuf_t j; jinit(&j, out, out_sz);
    jputf(&j, "{");
    jnum_hz(&j, "freq_hz",   s.valid & SCPI_V_FREQ,  s.freq4_x100000);
    jnum_hz(&j, "freq16_hz", s.valid & SCPI_V_DIV16, s.freq16_x100000);
    jnum_u (&j, "gate_ns",   s.valid & SCPI_V_FRAME, s.gate_ns);
    /* ⚠️ `channel`/`gate_ns` = co hlasi FPGA RAMEC (pri mrtvem linku nic), kdezto
     * `set_*` = co je na pristroji NAVOLENE (v11 `ui_cfg`). Web ukazuje nastaveni
     * (to plati vzdy) a mereni si hlida zvlast — kdyby se to slilo do jednoho pole,
     * vratili bychom se presne k chybe, kterou v11 opravuje. */
    /* ⚠️ SEQUENCE posledniho platneho DATA ramce. Web z ni pozna, kdy je hodnota
     * SKUTECNE NOVE MERENI a kdy jen tyz vzorek prectenty podruhe (poll bezi 1 Hz,
     * ale mereni chodi jinym tempem podle brany). Bez toho by klientsky pocitany
     * Allan bral opakovane hodnoty jako nezavisle vzorky a vysel by nesmyslne nizky.
     * Bere se PRIMO ze snapshotu — `scpi_src_t` tohle pole nema. */
    jputf(&j, "\"seq_meas\":%lu,", (unsigned long)snap->seq_meas);
    jputf(&j, "\"channel\":%u,", (unsigned)s.channel_id);
    jputf(&j, "\"set_chan\":%u,", (unsigned)s.set_chan);
    jputf(&j, "\"set_gate_idx\":%u,", (unsigned)s.set_gate_idx);
    { char gb[24]; fmt_scpi_hz_d(scpi_gate_s(s.set_gate_idx), gb, sizeof gb);
      jputf(&j, "\"set_gate_s\":%s,", gb); }
    jbool(&j, "running", s.set_running);
    jnum_c100(&j, "temp_ocxo_c",  s.valid & SCPI_V_T_OCXO,  s.t_ocxo_c100);
    jnum_c100(&j, "temp_board_c", s.valid & SCPI_V_T_BOARD, s.t_board_c100);
    jnum_c100(&j, "temp_mcu_c",   s.valid & SCPI_V_T_MCU,   s.t_mcu_c100);
    jnum_c100(&j, "temp_fpga_c",  s.valid & SCPI_V_T_FPGA,  s.t_fpga_c100);
    jnum_u(&j, "vc_mv",   s.valid & SCPI_V_VC,   s.ocxo_vc_mv);
    jnum_u(&j, "rf_mv",   s.valid & SCPI_V_RF,   s.rf_mv);
    /* RF v dBm — ⚠️ TOTOZNY vzorec i podminka jako `MEAS:POWer?` v scpi.c (kalibrace
     * AD8307 ze snapshotu), aby web neukazoval jinou hodnotu nez SCPI. Bez platneho
     * slope by slo o deleni necim blizkym nule -> pak radeji `null`. */
    if ((s.valid & SCPI_V_RF) && s.ad8307_slope_mv_db > 1.0f) {
        char db[24];
        fmt_scpi_hz_d((double)s.rf_mv / (double)s.ad8307_slope_mv_db
                      + (double)s.ad8307_intercept_dbm, db, sizeof db);
        jputf(&j, "\"rf_dbm\":%s,", db);
    } else {
        jputf(&j, "\"rf_dbm\":null,");
    }
    jnum_u(&j, "v12_mv",  s.valid & SCPI_V_V12,  s.v_12v_mv);
    jnum_u(&j, "v5_mv",   s.valid & SCPI_V_V5,   s.v_5v_mv);
    jnum_u(&j, "vref_mv", s.valid & SCPI_V_VREF, s.vref_mv);
    jnum_u(&j, "vbat_mv", s.valid & SCPI_V_VBAT, s.vbat_mv);
    jputf(&j, "\"gps\":{");
    jputf(&j, "\"fix_mode\":%u,\"num_sat\":%u,", (unsigned)s.gps_fix_mode, (unsigned)s.gps_num_sat);
    /* Poloha primo ze snapshotu (celociselne e7/cm — `scpi_src_t` ma jen float). */
    jnum_e7(&j, "lat", s.valid & SCPI_V_GPS, snap->gps_lat_e7);
    jnum_e7(&j, "lon", s.valid & SCPI_V_GPS, snap->gps_lon_e7);
    { int32_t cm = snap->gps_alt_cm; int neg = (cm < 0);
      uint32_t a = (uint32_t)(neg ? -(int64_t)cm : (int64_t)cm);
      if (s.valid & SCPI_V_GPS)
          jputf(&j, "\"alt_m\":%s%lu.%02lu,", neg ? "-" : "",
                (unsigned long)(a / 100u), (unsigned long)(a % 100u));
      else jputf(&j, "\"alt_m\":null,"); }
    { unsigned h10 = (unsigned)(snap->gps_hdop * 10.0f + 0.5f);
      if (h10 > 0u && h10 < 2550u) jputf(&j, "\"hdop\":%u.%u,", h10 / 10u, h10 % 10u);
      else jputf(&j, "\"hdop\":null,"); }
    jputf(&j, "\"time\":\"%02u:%02u:%02u\"", (unsigned)s.gps_hour, (unsigned)s.gps_min, (unsigned)s.gps_sec);
    jputf(&j, "},");
    jbool(&j, "spi_ok", s.spi_ok);
    /* ⚠️ Emulovana data (`fpgasim`) MUSI byt oznacena — jinak by web servíroval
     * emulaci jako mereni, kdezto displej/UART/datalog ji oznacuji. */
    jbool(&j, "sim", s.sim_active);
    jbool(&j, "signal_lost", s.freq_err);
    jbool(&j, "si5356_ok", s.si5356_ok);
    jputf(&j, "\"si5356_status\":%u,", (unsigned)s.si5356_status);
    jputf(&j, "\"uptime_s\":%lu,", (unsigned long)s.uptime_s);
    jputf(&j, "\"math\":{\"en\":%s,\"null_en\":%s,\"limit_en\":%s},",
          s.meas.math_en ? "true" : "false", s.meas.null_en ? "true" : "false",
          s.meas.limit_en ? "true" : "false");
    jbool(&j, "web_ctrl_en", snap->web_ctrl_en);
    /* v12 (#4): alarmy/prahy/selftest — dashboard karta STAV. Cteno PRIMO ze
     * snapshotu (`scpi_src_t` tahle pole nema). */
    jputf(&j, "\"alarms\":{\"fpga\":%u,\"gps\":%u,\"lim\":%u,\"vbat\":%u,\"ocxo\":%u,\"adev\":%u},",
          (unsigned)snap->alarm_fpga_lost, (unsigned)snap->alarm_gps_lost, (unsigned)snap->alarm_limit_fail,
          (unsigned)snap->alarm_vbat, (unsigned)snap->alarm_ocxo, (unsigned)snap->alarm_adev);
    jputf(&j, "\"mon\":{\"vbat\":%u,\"ocxo\":%u,\"adev\":%u},",
          (unsigned)snap->mon_vbat_bad, (unsigned)snap->mon_ocxo_bad, (unsigned)snap->mon_adev_bad);
    jputf(&j, "\"selftest\":%u,", (unsigned)snap->selftest_res);
    jputf(&j, "\"sys_level\":%u,", (unsigned)snap->sys_level);
    jputf(&j, "\"nsat\":%u,", (unsigned)snap->gps_sat_count);
    jputf(&j, "\"cm4\":{\"ipc_version\":%u},", (unsigned)IPC_VERSION);
    /* ⚠️ Docasna diagnostika HW bring-up (W5) — vysledek POSLEDNIHO pokusu o
     * prihlaseni pres /api/scpi. Zamerne jen delky a bool, nikdy surovy obsah
     * (heslo se pres tohle nikdy nevyzradí). Az bude auth overene na HW, dá se
     * klidne nechat — je to levne a nikdy neprozrazuje tajemstvi. */
    jputf(&j, "\"auth_debug\":{\"header_present\":%s,\"decode_ok\":%s,\"match\":%s,"
              "\"decoded_len\":%u,\"expected_len\":%u}",
          s_auth_dbg.header_present ? "true" : "false",
          s_auth_dbg.decode_ok ? "true" : "false",
          s_auth_dbg.match ? "true" : "false",
          (unsigned)s_auth_dbg.decoded_len, (unsigned)s_auth_dbg.expected_len);
    jputf(&j, "}");
    return j.used;
}

/* ── v12 (#5): GPS druzice pro sky plot. `{"n":N,"s":[[prn,elev,azim,snr,constel],..]}` */
static size_t build_sats_json(char *out, size_t out_sz, const ipc_snapshot_t *snap)
{
    jbuf_t j; jinit(&j, out, out_sz);
    unsigned n = snap->gps_sat_count;
    if (n > IPC_GPS_MAX_SATS) n = IPC_GPS_MAX_SATS;
    jputf(&j, "{\"n\":%u,\"s\":[", n);
    for (unsigned i = 0; i < n; i++) {
        const ipc_sat_t *st = &snap->gps_sats[i];
        jputf(&j, "%s[%u,%u,%u,%u,%u]", i ? "," : "",
              (unsigned)st->prn, (unsigned)st->elev, (unsigned)st->azim,
              (unsigned)st->snr, (unsigned)st->constel);
    }
    jputf(&j, "]}");
    return j.used;
}

/* ── v12 (#6): dlouha historie z datalogu (naplneny `g_ipc.log`). Per-bod pole
 * `[t_unix, freq|null, ocxo_C|null, deska_C|null, vc_mV, vbat_mV|null, flags]`.
 * Poradi = jak je vratil CM7 (nejnovejsi prvni); prohlizec si to seradi podle `t`. */
static size_t build_log_json(char *out, size_t out_sz)
{
    jbuf_t j; jinit(&j, out, out_sz);
    unsigned n = g_ipc.log.resp_count;
    if (n > IPC_LOG_CHUNK) n = IPC_LOG_CHUNK;
    jputf(&j, "{\"total\":%lu,\"n\":%u,\"scanned\":%lu,\"full_env\":%s,\"p\":[",
          (unsigned long)g_ipc.log.resp_total, n,
          (unsigned long)g_ipc.log.resp_scanned,
          g_ipc.log.resp_full_env ? "true" : "false");
    for (unsigned i = 0; i < n; i++) {
        const ipc_log_rec_t *r = (const ipc_log_rec_t *)&g_ipc.log.rec[i];
        char fb[24];
        if (r->freq_x100000 != 0u) fmt_scpi_hz_d((double)r->freq_x100000 / 100000.0, fb, sizeof fb);
        else { fb[0] = 'n'; fb[1] = 'u'; fb[2] = 'l'; fb[3] = 'l'; fb[4] = '\0'; }
        jputf(&j, "%s[%lu,%s,", i ? "," : "", (unsigned long)r->t_unix, fb);
        if (r->t_ocxo_c100 != (int16_t)0x8000) {
            int neg = r->t_ocxo_c100 < 0, v = neg ? -r->t_ocxo_c100 : r->t_ocxo_c100;
            jputf(&j, "%s%d.%02d,", neg ? "-" : "", v / 100, v % 100);
        } else jputf(&j, "null,");
        if (r->t_board_c100 != (int16_t)0x8000) {
            int neg = r->t_board_c100 < 0, v = neg ? -r->t_board_c100 : r->t_board_c100;
            jputf(&j, "%s%d.%02d,", neg ? "-" : "", v / 100, v % 100);
        } else jputf(&j, "null,");
        jputf(&j, "%u,", (unsigned)r->ocxo_vc_mv);
        if (r->vbat_mv != 0u && r->vbat_mv != 0x8000u) jputf(&j, "%u,", (unsigned)r->vbat_mv);
        else jputf(&j, "null,");
        jputf(&j, "%u", (unsigned)r->flags);
        /* v13: min/max obalka kmitoctu v bucketu (0 = nedostupna). */
        if (r->freq_min_x100000 != 0u && r->freq_max_x100000 != 0u) {
            char lo[24], hi[24];
            fmt_scpi_hz_d((double)r->freq_min_x100000 / 100000.0, lo, sizeof lo);
            fmt_scpi_hz_d((double)r->freq_max_x100000 / 100000.0, hi, sizeof hi);
            jputf(&j, ",%s,%s", lo, hi);
        }
        jputf(&j, "]");
    }
    jputf(&j, "]}");
    return j.used;
}

/* Precte cele cislo z query stringu: `?...&key=NNN...`. @return hodnota nebo `def`. */
static long qparam(const char *path, const char *key, long def)
{
    const char *q = strchr(path, '?');
    if (q == NULL) return def;
    size_t klen = strlen(key);
    for (const char *p = q + 1; *p; ) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=')
            return atol(p + klen + 1);
        const char *amp = strchr(p, '&');
        if (amp == NULL) break;
        p = amp + 1;
    }
    return def;
}

/* Cesta zacina danym prefixem (pred pripadnym '?')? */
static int path_is(const char *path, const char *base)
{
    size_t bl = strlen(base);
    return strncmp(path, base, bl) == 0 && (path[bl] == '\0' || path[bl] == '?');
}

/* ── SPA: HTML+CSS+JS pohromade v `.rodata`. ─────────────────────────────────
 * ⚠️ ZADNE dvojite uvozovky uvnitr — HTML atributy a JS retezce pouzivaji
 * VYHRADNE jednoduche uvozovky, aby se cely blok dal zapsat jako C retezcovy
 * literal (sousedni retezce se v C spojuji) bez escapovani. Zadny build krok
 * navic (zadny xxd/makefsdata) — presne duvod z komentare v hlavicce souboru. */
static const char SPA_HTML[] =
"<!DOCTYPE html>\n"
"<html lang='cs' data-t='amber'><head><meta charset='utf-8'>\n"
"<meta name='viewport' content='width=device-width,initial-scale=1'>\n"
"<title>GPSDO citac</title>\n"
"<style>\n"
"/* ---- paleta: decentni BIOS (jantar na tmave), + modra a svetla varianta ---- */\n"
":root,:root[data-t='amber']{\n"
"--bg:#0a0d13; --pnl:#111721; --pnl2:#161d29; --line:#26303f; --line2:#3a4759;\n"
"--ink:#d6d2c8; --ink2:#9aa4b2; --dim:#5d6878;\n"
"--acc:#e0a83a; --ok:#7fae5f; --warn:#e0a83a; --bad:#cc5f5f;\n"
"--c0:#e0a83a; --c1:#6b9bd1; --c2:#b08cc4; --c3:#7fae5f;\n"
"--track:#1b2330; --glow:rgba(224,168,58,.18); --hd:#0e1420;\n"
"}\n"
":root[data-t='blue']{\n"
"--bg:#060d1a; --pnl:#0d1a30; --pnl2:#112238; --line:#1f3352; --line2:#2f4a70;\n"
"--ink:#c8d4e4; --ink2:#8ba0bd; --dim:#556d8c;\n"
"--acc:#79b0e8; --ok:#6fbf8f; --warn:#e0b355; --bad:#e07878;\n"
"--c0:#79b0e8; --c1:#e0b355; --c2:#b090d8; --c3:#6fbf8f;\n"
"--track:#12233a; --glow:rgba(121,176,232,.16); --hd:#0a1526;\n"
"}\n"
":root[data-t='light']{\n"
"--bg:#e8e6e1; --pnl:#f7f6f3; --pnl2:#eeece7; --line:#c9c5bc; --line2:#a8a49a;\n"
"--ink:#1e2430; --ink2:#4a5364; --dim:#7b8394;\n"
"--acc:#a06f10; --ok:#3f7a2e; --warn:#a06f10; --bad:#a83232;\n"
"--c0:#a06f10; --c1:#2f6ba8; --c2:#7a4f96; --c3:#3f7a2e;\n"
"--track:#dcd9d2; --glow:rgba(160,111,16,.12); --hd:#e0ddd6;\n"
"}\n"
"*{box-sizing:border-box;margin:0;padding:0}\n"
"body{background:var(--bg);color:var(--ink);\n"
"font:13px/1.5 system-ui,Segoe UI,Roboto,Helvetica,Arial,sans-serif;padding:12px}\n"
".mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-variant-numeric:tabular-nums}\n"
".wrap{max-width:1300px;margin:0 auto;display:flex;flex-direction:column;gap:12px}\n"
"\n"
"/* ---- header ---- */\n"
"header{display:flex;align-items:center;gap:8px;flex-wrap:wrap;\n"
"background:var(--hd);border:1px solid var(--line);border-radius:4px;padding:9px 12px}\n"
".brand{display:flex;align-items:baseline;gap:10px;margin-right:auto}\n"
".brand b{font:600 14px/1 ui-monospace,Consolas,monospace;letter-spacing:.06em;color:var(--acc)}\n"
".brand span{font:11px/1 ui-monospace,Consolas,monospace;color:var(--dim)}\n"
".pill{display:inline-flex;align-items:center;gap:7px;background:var(--pnl);\n"
"border:1px solid var(--line);border-radius:3px;padding:5px 10px;\n"
"font:11px/1 ui-monospace,Consolas,monospace;color:var(--ink2)}\n"
".dot{width:7px;height:7px;border-radius:1px;background:var(--dim);flex:none}\n"
".dot.ok{background:var(--ok)} .dot.warn{background:var(--warn)}\n"
".dot.bad{background:var(--bad);animation:bl 1.4s steps(2) infinite}\n"
"@keyframes bl{50%{opacity:.2}}\n"
".tbtn{background:var(--pnl);border:1px solid var(--line2);color:var(--ink2);border-radius:3px;\n"
"padding:5px 11px;font:11px/1 ui-monospace,Consolas,monospace;cursor:pointer;letter-spacing:.06em}\n"
".tbtn:hover{color:var(--acc);border-color:var(--acc)}\n"
"\n"
"/* ---- panely ---- */\n"
".card{background:var(--pnl);border:1px solid var(--line);border-radius:4px;padding:0 13px 13px}\n"
".ttl{font:11px/1 ui-monospace,Consolas,monospace;letter-spacing:.1em;color:var(--acc);\n"
"display:flex;align-items:center;gap:8px;margin:0 -13px 12px;padding:8px 13px;\n"
"background:var(--hd);border-bottom:1px solid var(--line)}\n"
".ttl .r{margin-left:auto;letter-spacing:0;color:var(--ink2)}\n"
".ttl .zo{color:var(--dim);letter-spacing:0}\n"
"\n"
"/* ---- hero ---- */\n"
".hero{background:radial-gradient(900px 200px at 50% -30px,var(--glow),transparent 70%),var(--pnl);\n"
"border:1px solid var(--line);border-radius:4px;padding:16px 14px 13px;text-align:center}\n"
".hlbl{font:10px/1 ui-monospace,Consolas,monospace;letter-spacing:.24em;color:var(--dim)}\n"
".freq{font-family:ui-monospace,Consolas,monospace;font-variant-numeric:tabular-nums;\n"
"font-size:clamp(28px,7vw,58px);font-weight:600;line-height:1.06;color:var(--acc);\n"
"margin:9px 0 4px;word-break:break-all}\n"
".freq[data-na]{color:var(--dim)}\n"
"/* Emulace: vyrazny pruh u headline, aby se nedala zamenit za mereni. */\n"
".simtag{display:none;margin-left:10px;padding:2px 8px;border-radius:3px;\n"
"background:var(--warn);color:var(--bg);letter-spacing:.08em;font-weight:600}\n"
".simtag[data-on]{display:inline-block}\n"
".hero[data-sim]{border-color:var(--warn)}\n"
".freq u{font-size:.34em;color:var(--ink2);margin-left:.3em;font-weight:400;text-decoration:none}\n"
"/* Headline zrcadli DISPLEJ: posledni duveryhodna cislice modre podtrzena,\n"
"   za ni nejista mista mensim pismem v odstinu sedi. */\n"
".freq b.fc{font-weight:inherit;border-bottom:3px solid var(--acc);padding-bottom:1px}\n"
".freq i.fu{font-style:normal;font-size:.68em;color:var(--dim)}\n"
"/* Kratky zablesk pri NOVEM mereni — dashboard tim dava najevo, ze data zijou. */\n"
"@keyframes hit{from{filter:brightness(1.55)}to{filter:brightness(1)}}\n"
".freq[data-hit]{animation:hit .25s ease-out}\n"
"/* Koncovy bod krivky (aktualni hodnota). HTML overlay, ne SVG: viewBox je\n"
"   roztazeny (preserveAspectRatio none), takze SVG kruh by byl elipsa. */\n"
".eod{position:absolute;width:7px;height:7px;margin:-4px 0 0 -4px;border-radius:50%;\n"
"background:var(--c0);box-shadow:0 0 0 2px var(--pnl2);display:none;pointer-events:none}\n"
".card{transition:border-color .15s}\n"
".card:hover{border-color:var(--line2)}\n"
"button:focus-visible,input:focus-visible{outline:2px solid var(--acc);outline-offset:1px}\n"
".why{font-size:12px;color:var(--warn);min-height:1.3em}\n"
".hgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(112px,1fr));gap:1px;\n"
"background:var(--line);border:1px solid var(--line);margin-top:13px}\n"
".hgrid div{background:var(--pnl2);padding:7px 9px;text-align:left}\n"
".hgrid .k{font:9px/1.4 ui-monospace,Consolas,monospace;color:var(--dim);letter-spacing:.08em}\n"
".hgrid .v{font-family:ui-monospace,Consolas,monospace;font-size:14px;margin-top:2px}\n"
".hgrid .v[data-na]{color:var(--dim)}\n"
"\n"
"/* ---- grafy ---- */\n"
".charts{display:grid;grid-template-columns:repeat(auto-fit,minmax(340px,1fr));gap:12px}\n"
".cw{position:relative;height:128px;border:1px solid var(--line);background:var(--pnl2);\n"
"overflow:hidden;cursor:zoom-in}\n"
".cw:hover{border-color:var(--line2)}\n"
"svg{width:100%;height:100%;display:block}\n"
".g{stroke:var(--line);stroke-width:1;vector-effect:non-scaling-stroke;stroke-dasharray:2 4}\n"
".ln{fill:none;stroke-width:1.6;vector-effect:non-scaling-stroke;stroke-linejoin:round}\n"
".ar{stroke:none;opacity:.13}\n"
".env{fill:var(--c0);opacity:.2;stroke:none}\n"
".fit{stroke-dasharray:5 4;stroke-width:1.2;opacity:.9}\n"
".s0{stroke:var(--c0)} .s1{stroke:var(--c1)} .s2{stroke:var(--c2)} .s3{stroke:var(--c3)}\n"
".f0{fill:var(--c0)} .f1{fill:var(--c1)}\n"
".yl{position:absolute;inset:0;pointer-events:none;\n"
"font:10px/1 ui-monospace,Consolas,monospace;color:var(--dim)}\n"
".yl b{position:absolute;left:5px;font-weight:400}\n"
".yl .t{top:4px} .yl .b{bottom:4px}\n"
".lg{display:flex;gap:11px;flex-wrap:wrap;margin-top:7px;\n"
"font:10px/1.6 ui-monospace,Consolas,monospace;color:var(--ink2)}\n"
".lg i{display:inline-block;width:10px;height:2px;margin-right:5px;vertical-align:middle}\n"
".lg i.k0{background:var(--c0)} .lg i.k1{background:var(--c1)}\n"
".lg i.k2{background:var(--c2)} .lg i.k3{background:var(--c3)}\n"
".nod{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;\n"
"color:var(--dim);font:11px ui-monospace,Consolas,monospace}\n"
"\n"
"/* ---- ovladani ---- */\n"
".ctl{display:flex;gap:16px;flex-wrap:wrap;align-items:flex-end}\n"
".grp{display:flex;flex-direction:column;gap:5px}\n"
".grp>span{font:9px/1 ui-monospace,Consolas,monospace;letter-spacing:.12em;color:var(--dim)}\n"
".seg{display:inline-flex;background:var(--pnl2);border:1px solid var(--line);padding:2px;gap:2px}\n"
".seg button{border:0;background:transparent;color:var(--ink2);\n"
"padding:7px 13px;font:12px/1 ui-monospace,Consolas,monospace;cursor:pointer}\n"
".seg button:hover:not(:disabled){color:var(--ink);background:var(--line)}\n"
".seg button.on{background:var(--acc);color:var(--bg);font-weight:600}\n"
".seg button.run.on{background:var(--ok);color:var(--bg)}\n"
".seg button.stop.on{background:var(--bad);color:var(--bg)}\n"
"button:disabled{opacity:.3;cursor:not-allowed}\n"
".btn{background:var(--pnl2);border:1px solid var(--line2);color:var(--ink);\n"
"padding:8px 15px;font:12px/1 ui-monospace,Consolas,monospace;cursor:pointer}\n"
".btn:hover:not(:disabled){border-color:var(--acc);color:var(--acc)}\n"
"input{background:var(--bg);border:1px solid var(--line2);color:var(--ink);\n"
"padding:8px 10px;font:12px ui-monospace,Consolas,monospace;width:150px}\n"
"input:focus{outline:0;border-color:var(--acc)}\n"
".lock{font:11px ui-monospace,Consolas,monospace;color:var(--warn);margin-top:9px}\n"
"\n"
"/* ---- radky ---- */\n"
".row{display:grid;grid-template-columns:76px 1fr 92px;gap:9px;align-items:center;\n"
"padding:4px 0;font:11px ui-monospace,Consolas,monospace}\n"
".row .n{color:var(--ink2)}\n"
".row .val{text-align:right;font-size:12px}\n"
".row .val[data-na]{color:var(--dim)}\n"
".bar{position:relative;height:8px;background:var(--track);border:1px solid var(--line);overflow:hidden}\n"
".bar i{position:absolute;left:0;top:0;bottom:0;background:var(--acc);transition:width .3s}\n"
".bar u{position:absolute;top:-1px;bottom:-1px;width:2px;background:var(--ink2);opacity:.7}\n"
".bar.ok i{background:var(--ok)} .bar.warn i{background:var(--warn)} .bar.bad i{background:var(--bad)}\n"
"\n"
"/* ---- gps ---- */\n"
".gps{display:flex;gap:15px;align-items:center;flex-wrap:wrap}\n"
".donut{width:92px;height:92px;border-radius:50%;flex:none;display:grid;place-items:center;\n"
"background:conic-gradient(var(--dim) 0deg,var(--track) 0deg)}\n"
".donut>div{width:70px;height:70px;border-radius:50%;background:var(--pnl);display:grid;\n"
"place-items:center;text-align:center;line-height:1.15}\n"
".donut b{font:21px ui-monospace,Consolas,monospace}\n"
".donut i{font:8px/1 ui-monospace,Consolas,monospace;color:var(--dim);font-style:normal;letter-spacing:.1em}\n"
".kv{display:grid;grid-template-columns:auto auto;gap:4px 13px;\n"
"font:11px ui-monospace,Consolas,monospace;align-content:center}\n"
".kv b{color:var(--dim);font-weight:400}\n"
".zoomable{cursor:zoom-in}\n"
".sky{margin-top:11px;text-align:center;cursor:zoom-in}\n"
".sky svg{width:160px;height:160px}\n"
".skc{fill:none;stroke:var(--line);stroke-width:1}\n"
".skt{fill:var(--dim);font:8px ui-monospace,monospace;text-anchor:middle}\n"
".skp{fill:var(--dim);font:7px ui-monospace,monospace;text-anchor:middle}\n"
"/* ---- stav / diagnostika ---- */\n"
".stg{display:grid;grid-template-columns:1fr auto;gap:5px 12px;\n"
"font:11px ui-monospace,Consolas,monospace;align-items:center}\n"
".stg b{color:var(--ink2);font-weight:400}\n"
".stg span{text-align:right;font-size:12px}\n"
".cok{padding:2px 8px;border-radius:3px;background:rgba(127,174,95,.18);color:var(--ok)}\n"
".cwarn{padding:2px 8px;border-radius:3px;background:rgba(224,168,58,.18);color:var(--warn)}\n"
".cbad{padding:2px 8px;border-radius:3px;background:rgba(204,95,95,.2);color:var(--bad)}\n"
"\n"
".tab{display:grid;grid-template-columns:auto 1fr auto;gap:3px 12px;\n"
"font:11px ui-monospace,Consolas,monospace;margin-top:9px}\n"
".tab b{color:var(--dim);font-weight:400}\n"
".tab span{text-align:right}\n"
".tab i{font-style:normal;color:var(--dim);font-size:10px;text-align:right}\n"
".big{display:flex;align-items:baseline;gap:8px;margin:0 0 8px}\n"
".big b{font:600 24px/1 ui-monospace,Consolas,monospace;color:var(--acc)}\n"
".big b[data-na]{color:var(--dim)}\n"
".big span{font:9px/1 ui-monospace,Consolas,monospace;color:var(--dim);letter-spacing:.12em}\n"
".warnrow{font:10px/1.5 ui-monospace,Consolas,monospace;color:var(--warn);margin-top:6px}\n"
".cols{display:grid;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));gap:12px}\n"
".msg{margin-top:9px;font:11px/1.6 ui-monospace,Consolas,monospace;color:var(--ink2);\n"
"white-space:pre-wrap;word-break:break-all;min-height:1.5em}\n"
".msg.err{color:var(--bad)} .msg.ok{color:var(--ok)}\n"
".log{max-height:170px;overflow:auto;margin-top:9px;border-top:1px solid var(--line);padding-top:7px}\n"
".log div{font:11px/1.6 ui-monospace,Consolas,monospace;color:var(--ink2);\n"
"border-bottom:1px solid var(--line);padding:3px 0}\n"
".log div b{color:var(--acc);font-weight:400}\n"
".log div.e{color:var(--bad)}\n"
"footer{color:var(--dim);font:10px ui-monospace,Consolas,monospace;text-align:center;padding:2px 0 10px}\n"
"\n"
"/* ---- detailni okno grafu ---- */\n"
".ovl{position:fixed;inset:0;background:rgba(0,0,0,.72);display:none;z-index:9;\n"
"padding:20px;overflow:auto}\n"
".ovl.on{display:block}\n"
".dlg{max-width:1180px;margin:0 auto;background:var(--pnl);border:1px solid var(--line2)}\n"
".dhead{display:flex;align-items:center;gap:10px;background:var(--hd);\n"
"border-bottom:1px solid var(--line);padding:9px 13px;\n"
"font:12px/1 ui-monospace,Consolas,monospace;letter-spacing:.08em;color:var(--acc)}\n"
".dhead .r{margin-left:auto}\n"
".dbody{padding:13px}\n"
".dcw{position:relative;height:min(52vh,430px);border:1px solid var(--line);\n"
"background:var(--pnl2);margin-bottom:10px}\n"
".dax{position:absolute;inset:0;pointer-events:none;font:10px ui-monospace,Consolas,monospace;color:var(--dim)}\n"
".dax b{position:absolute;right:6px;font-weight:400;transform:translateY(-50%);\n"
"background:var(--pnl2);padding:0 3px}\n"
".dax u{position:absolute;bottom:3px;text-decoration:none;transform:translateX(-50%)}\n"
".dst{display:grid;grid-template-columns:repeat(auto-fit,minmax(128px,1fr));gap:1px;\n"
"background:var(--line);border:1px solid var(--line)}\n"
".dst div{background:var(--pnl2);padding:8px 10px}\n"
".dst .k{font:9px/1.4 ui-monospace,Consolas,monospace;color:var(--dim);letter-spacing:.08em}\n"
".dst .v{font:15px ui-monospace,Consolas,monospace;margin-top:2px}\n"
".dnote{font:10px/1.6 ui-monospace,Consolas,monospace;color:var(--dim);margin-top:9px}\n"
"/* ---- tabulka rad (detail teplot/napajeni/...) ---- */\n"
".dtab{margin-top:10px;border:1px solid var(--line);overflow:auto}\n"
".drow{display:grid;grid-template-columns:96px repeat(5,1fr);gap:8px;align-items:center;\n"
"padding:5px 10px;border-bottom:1px solid var(--line);\n"
"font:11px ui-monospace,Consolas,monospace}\n"
".drow:last-child{border-bottom:0}\n"
".drow[data-hd]{background:var(--hd);color:var(--dim);font-size:9px;letter-spacing:.1em}\n"
".drow b{font-weight:400;display:flex;align-items:center;gap:6px}\n"
".drow b em{width:10px;height:2px;font-style:normal;flex:none}\n"
".drow span{text-align:right}\n"
".drow span.hi{color:var(--ink)}\n"
"/* ---- tabulka druzic (detail GPS) ---- */\n"
".sattab{margin-top:10px;border:1px solid var(--line);max-height:34vh;overflow:auto}\n"
".satrow{display:grid;grid-template-columns:52px 74px 62px 62px 1fr 62px;gap:9px;\n"
"align-items:center;padding:5px 10px;border-bottom:1px solid var(--line);\n"
"font:11px ui-monospace,Consolas,monospace}\n"
".satrow:last-child{border-bottom:0}\n"
".satrow b{color:var(--ink);font-weight:600}\n"
".satrow i{color:var(--dim);font-style:normal;font-size:10px}\n"
".satrow u{color:var(--ink2);text-decoration:none;font-size:10px}\n"
".satrow em{text-align:right;font-style:normal;color:var(--ink)}\n"
".satbar{height:7px;background:var(--track);border:1px solid var(--line);overflow:hidden}\n"
".satbar span{display:block;height:100%}\n"
"</style></head><body><div class='wrap'>\n"
"\n"
"<header>\n"
"<div class='brand'><b>GPSDO / CITAC KMITOCTU</b><span id='ip'></span></div>\n"
"<span class='pill'><i class='dot' id='dLink'></i><span id='tLink'>FPGA</span></span>\n"
"<span class='pill'><i class='dot' id='dGps'></i><span id='tGps'>GPS</span></span>\n"
"<span class='pill'><i class='dot' id='dRef'></i><span id='tRef'>REF</span></span>\n"
"<span class='pill'><i class='dot' id='dRun'></i><span id='tRun'>STAV</span></span>\n"
"<button class='tbtn' id='bTheme'>VZHLED</button>\n"
"</header>\n"
"\n"
"<div class='hero' id='hero'>\n"
"<div class='hlbl'>KMITOCET<span id='simTag' class='simtag'>EMULACE (fpgasim) - NENI MERENI</span></div>\n"
"<div class='freq mono' id='freq' data-na='1'>--</div>\n"
"<div class='why' id='why'></div>\n"
"<div class='hgrid'>\n"
"<div><div class='k'>PERIODA</div><div class='v' id='mPer' data-na='1'>--</div></div>\n"
"<div><div class='k'>VETEV /16</div><div class='v' id='mF16' data-na='1'>--</div></div>\n"
"<div><div class='k'>BRANA NASTAV.</div><div class='v' id='mGset'>--</div></div>\n"
"<div><div class='k'>BRANA ZMERENA</div><div class='v' id='mGact' data-na='1'>--</div></div>\n"
"<div><div class='k'>VSTUP</div><div class='v' id='mChan'>--</div></div>\n"
"<div><div class='k'>DOBA BEHU</div><div class='v' id='mUp'>--</div></div>\n"
"</div>\n"
"</div>\n"
"\n"
"<div class='card'>\n"
"<div class='ttl'>[ OVLADANI ]<span class='r' id='lastUpd'></span></div>\n"
"<div class='ctl'>\n"
"<div class='grp'><span>MERENI</span><div class='seg' id='segRun'>\n"
"<button class='run' data-r='1'>RUN</button><button class='stop' data-r='0'>STOP</button></div></div>\n"
"<div class='grp'><span>BRANA</span><div class='seg' id='segGate'>\n"
"<button data-g='0'>0,1 s</button><button data-g='1'>1 s</button>\n"
"<button data-g='2'>10 s</button><button data-g='3'>100 s</button></div></div>\n"
"<div class='grp'><span>VSTUP</span><div class='seg' id='segChan'>\n"
"<button data-c='0'>A</button><button data-c='1'>B</button></div></div>\n"
"<div class='grp'><span>OKNO GRAFU</span><div class='seg' id='segWin'>\n"
"<button data-w='60'>1 min</button><button data-w='300'>5 min</button>\n"
"<button data-w='900'>15 min</button><button data-w='3600'>1 h</button>\n"
"<button data-w='86400'>24 h</button><button data-w='604800'>7 dni</button>\n"
"<button data-w='2592000'>30 dni</button></div></div>\n"
"<div class='grp'><span>EXPORT</span><button class='btn' id='bCsv'>CSV</button></div>\n"
"</div>\n"
"<div class='lock' id='lock'></div>\n"
"<div class='msg' id='msg'></div>\n"
"<div class='warnrow' id='dlNote'></div>\n"
"</div>\n"
"\n"
"<div class='charts'>\n"
"\n"
"<div class='card'>\n"
"<div class='ttl'>[ KMITOCET ]<span class='zo'>klikni = detail</span><span class='r mono' id='stFreq'>--</span></div>\n"
"<div class='cw' data-z='freq'>\n"
"<svg viewBox='0 0 100 100' preserveAspectRatio='none'>\n"
"<line class='g' x1='0' y1='25' x2='100' y2='25'/><line class='g' x1='0' y1='50' x2='100' y2='50'/>\n"
"<line class='g' x1='0' y1='75' x2='100' y2='75'/>\n"
"<polygon class='env' id='envFreq'/>\n"
"<polygon class='ar f0' id='aFreq'/><polyline class='ln s0' id='lFreq'/>\n"
"<polyline class='ln fit s1' id='lFit'/>\n"
"</svg>\n"
"<div class='yl'><b class='t' id='yFreqT'></b><b class='b' id='yFreqB'></b></div>\n"
"<div class='nod' id='nFreq'>ceka na mereni...</div>\n"
"<i class='eod' id='efreq'></i>\n"
"</div>\n"
"<div class='lg'><span><i class='k0'></i>odchylka od prumeru okna</span>\n"
"<span><i class='k1'></i>linearni proklad (drift)</span></div>\n"
"</div>\n"
"\n"
"<div class='card'>\n"
"<div class='ttl'>[ TEPLOTY ]<span class='zo'>klikni = detail</span><span class='r mono' id='stTemp'>--</span></div>\n"
"<div class='cw' data-z='temp'>\n"
"<svg viewBox='0 0 100 100' preserveAspectRatio='none'>\n"
"<line class='g' x1='0' y1='25' x2='100' y2='25'/><line class='g' x1='0' y1='50' x2='100' y2='50'/>\n"
"<line class='g' x1='0' y1='75' x2='100' y2='75'/>\n"
"<polyline class='ln s0' id='lT0'/><polyline class='ln s1' id='lT1'/>\n"
"<polyline class='ln s2' id='lT2'/><polyline class='ln s3' id='lT3'/>\n"
"</svg>\n"
"<div class='yl'><b class='t' id='yTempT'></b><b class='b' id='yTempB'></b></div>\n"
"<div class='nod' id='nTemp'>ceka na data...</div>\n"
"<i class='eod' id='etemp'></i>\n"
"</div>\n"
"<div class='lg'><span><i class='k0'></i>OCXO</span><span><i class='k1'></i>deska</span>\n"
"<span><i class='k2'></i>MCU</span><span><i class='k3'></i>FPGA</span></div>\n"
"</div>\n"
"\n"
"<div class='card'>\n"
"<div class='ttl'>[ OCXO Vc ]<span class='zo'>klikni = detail</span><span class='r mono' id='stVc'>--</span></div>\n"
"<div class='cw' data-z='vc'>\n"
"<svg viewBox='0 0 100 100' preserveAspectRatio='none'>\n"
"<line class='g' x1='0' y1='25' x2='100' y2='25'/><line class='g' x1='0' y1='50' x2='100' y2='50'/>\n"
"<line class='g' x1='0' y1='75' x2='100' y2='75'/>\n"
"<polygon class='ar f1' id='aVc'/><polyline class='ln s1' id='lVc'/>\n"
"</svg>\n"
"<div class='yl'><b class='t' id='yVcT'></b><b class='b' id='yVcB'></b></div>\n"
"<div class='nod' id='nVc'>ceka na data...</div>\n"
"<i class='eod' id='evc'></i>\n"
"</div>\n"
"<div class='lg'><span><i class='k1'></i>ladici napeti - jak si smycka doladuje OCXO</span></div>\n"
"</div>\n"
"\n"
"<div class='card'>\n"
"<div class='ttl'>[ NAPAJENI ]<span class='zo'>klikni = detail</span><span class='r mono' id='stPwr'>--</span></div>\n"
"<div class='cw' data-z='pwr'>\n"
"<svg viewBox='0 0 100 100' preserveAspectRatio='none'>\n"
"<line class='g' x1='0' y1='25' x2='100' y2='25'/><line class='g' x1='0' y1='50' x2='100' y2='50'/>\n"
"<line class='g' x1='0' y1='75' x2='100' y2='75'/>\n"
"<polyline class='ln s0' id='lP0'/><polyline class='ln s3' id='lP1'/>\n"
"<polyline class='ln s1' id='lP2'/><polyline class='ln s2' id='lP3'/>\n"
"</svg>\n"
"<div class='yl'><b class='t' id='yPwrT'></b><b class='b' id='yPwrB'></b></div>\n"
"<div class='nod' id='nPwr'>ceka na data...</div>\n"
"<i class='eod' id='epwr'></i>\n"
"</div>\n"
"<div class='lg'><span><i class='k0'></i>12 V</span><span><i class='k3'></i>5 V</span>\n"
"<span><i class='k1'></i>VREF</span><span><i class='k2'></i>VBAT</span></div>\n"
"</div>\n"
"\n"
"</div>\n"
"\n"
"<div class='charts'>\n"
"\n"
"<div class='card'>\n"
"<div class='ttl'>[ ALLAN sigma_y(tau) ]<span class='zo'>klikni = detail</span><span class='r mono' id='stAdev'>--</span></div>\n"
"<div class='big'><b id='aHead' data-na='1'>--</b><span id='aHeadL'>SIGMA_Y</span></div>\n"
"<div class='cw' data-z='adev'>\n"
"<svg viewBox='0 0 100 100' preserveAspectRatio='none'>\n"
"<line class='g' x1='0' y1='25' x2='100' y2='25'/><line class='g' x1='0' y1='50' x2='100' y2='50'/>\n"
"<line class='g' x1='0' y1='75' x2='100' y2='75'/>\n"
"<polyline class='ln s3' id='lAdev'/>\n"
"</svg>\n"
"<div class='yl'><b class='t' id='yAdT'></b><b class='b' id='yAdB'></b></div>\n"
"<div class='nod' id='nAdev'>ceka na mereni z FPGA...</div>\n"
"</div>\n"
"<div class='tab' id='tAdev'></div>\n"
"<div class='warnrow' id='aWarn'></div>\n"
"</div>\n"
"\n"
"<div class='card'>\n"
"<div class='ttl'>[ DRIFT A OFFSET ]<span class='r mono' id='stDrift'>--</span></div>\n"
"<div class='big'><b id='dHead' data-na='1'>--</b><span>ZA DEN</span></div>\n"
"<div class='tab' id='tDrift'></div>\n"
"<div class='ctl' style='margin-top:11px'>\n"
"<div class='grp'><span>NOMINAL [Hz]</span><input id='nom' class='mono' placeholder='10000000'></div>\n"
"<div class='grp'><span>&nbsp;</span><button class='btn' id='bNom'>= AKTUALNI</button></div>\n"
"</div>\n"
"<div class='warnrow' id='dWarn'></div>\n"
"</div>\n"
"\n"
"</div>\n"
"\n"
"<div class='cols'>\n"
"<div class='card'><div class='ttl'>[ TEPLOTY ]<span class='zo'>klikni = detail</span></div>\n"
"<div id='gTemp' class='zoomable' data-z='temp'></div></div>\n"
"<div class='card'><div class='ttl'>[ NAPAJENI ]<span class='zo'>klikni = detail</span></div>\n"
"<div id='gPwr' class='zoomable' data-z='pwr'></div></div>\n"
"<div class='card'><div class='ttl'>[ GPS ]<span class='zo'>klikni = detail</span>\n"
"<span class='r mono' id='gHdop'>--</span></div>\n"
"<div class='gps zoomable' data-z='gps'>\n"
"<div class='donut' id='dnut'><div><b id='dnN'>--</b><i>SAT</i></div></div>\n"
"<div class='kv'>\n"
"<b>FIX</b><span id='gFix'>--</span><b>CAS UTC</b><span id='gTime'>--</span>\n"
"<b>POLOHA</b><span id='gPos'>--</span><b>VYSKA</b><span id='gAlt'>--</span>\n"
"<b>REFERENCE</b><span id='gRef'>--</span><b>RF</b><span id='gRf'>--</span>\n"
"</div>\n"
"</div>\n"
"<div class='row' style='margin-top:9px'><div class='n'>RF</div>\n"
"<div class='bar' id='bRf'><i style='width:0%'></i></div><div class='val' id='vRf'>--</div></div>\n"
"<div class='sky' data-z='gps'><svg viewBox='-100 -100 200 200' preserveAspectRatio='xMidYMid meet'>\n"
"<circle class='skc' r='92'/><circle class='skc' r='61'/><circle class='skc' r='31'/>\n"
"<line class='skc' x1='-92' y1='0' x2='92' y2='0'/><line class='skc' x1='0' y1='-92' x2='0' y2='92'/>\n"
"<text class='skt' x='0' y='-95'>S</text><text class='skt' x='0' y='101'>J</text>\n"
"<text class='skt' x='99' y='3'>V</text><text class='skt' x='-99' y='3'>Z</text>\n"
"<g id='skg'></g></svg>\n"
"<div style='font:9px ui-monospace,monospace;color:var(--dim)'>sky plot - stred = zenit, barva = C/N0</div></div>\n"
"</div>\n"
"</div>\n"
"\n"
"<div class='cols'>\n"
"<div class='card'><div class='ttl'>[ STAV / DIAGNOSTIKA ]</div>\n"
"<div class='stg' id='stg'></div></div>\n"
"\n"
"<div class='card'><div class='ttl'>[ MATH / LIMITY ]<span class='r' id='vVerdict'>--</span></div>\n"
"<div class='ctl'><div class='grp'><span>M (nasobic)</span><input id='mM' class='mono' style='width:100px' value='1'></div>\n"
"<div class='grp'><span>B [Hz]</span><input id='mB' class='mono' style='width:100px' value='0'></div>\n"
"<div class='grp'><span>&nbsp;</span><button class='btn' id='bMath'>MATH --</button></div></div>\n"
"<div class='ctl' style='margin-top:9px'><div class='grp'><span>&nbsp;</span><button class='btn' id='bNull'>NULL: zachytit</button></div>\n"
"<div class='grp'><span>&nbsp;</span><button class='btn' id='bNullOff'>NULL: vypnout</button></div></div>\n"
"<div class='ctl' style='margin-top:9px'><div class='grp'><span>LIMIT LO [Hz]</span><input id='mLo' class='mono' style='width:120px'></div>\n"
"<div class='grp'><span>LIMIT HI [Hz]</span><input id='mHi' class='mono' style='width:120px'></div>\n"
"<div class='grp'><span>&nbsp;</span><button class='btn' id='bLim'>LIMITY --</button></div></div>\n"
"<div class='msg' id='mmsg'></div></div>\n"
"\n"
"<div class='card'><div class='ttl'>[ CAS (RTC / UTC) ]</div>\n"
"<div class='ctl'><div class='grp'><span>DATUM (RRRR,MM,DD)</span><input id='rD' class='mono' style='width:150px' placeholder='2026,08,24'></div></div>\n"
"<div class='ctl' style='margin-top:9px'><div class='grp'><span>CAS (HH,MM,SS)</span><input id='rT' class='mono' style='width:150px' placeholder='12,00,00'></div>\n"
"<div class='grp'><span>&nbsp;</span><button class='btn' id='bRtc'>NASTAVIT</button></div></div>\n"
"<div class='msg' id='rmsg'>Rucni cas ma smysl jen bez GPS antény - GPS ho pak zase prepise.</div></div>\n"
"</div>\n"
"\n"
"<div class='card'>\n"
"<div class='ttl'>[ SCPI KONZOLE ]<span class='r'>Enter odesle</span></div>\n"
"<div class='ctl'>\n"
"<div class='grp'><span>PRIKAZ</span><input id='c' style='width:300px' placeholder='MEAS:FREQ?'></div>\n"
"<div class='grp'><span>&nbsp;</span><button class='btn' id='bSend'>ODESLAT</button></div>\n"
"<div class='grp'><span>&nbsp;</span><button class='btn' id='bClr'>VYCISTIT</button></div>\n"
"</div>\n"
"<div class='log' id='log'></div>\n"
"</div>\n"
"\n"
"<div class='card'>\n"
"<div class='ttl'>[ PRISTUP ]</div>\n"
"<div class='ctl'>\n"
"<div class='grp'><span>JMENO</span><input id='u' value='admin'></div>\n"
"<div class='grp'><span>HESLO</span><input id='p' type='password'></div>\n"
"<div class='grp'><span>&nbsp;</span><button class='btn' id='bLogin'>PRIHLASIT</button></div>\n"
"</div>\n"
"<div class='msg' id='amsg'></div>\n"
"</div>\n"
"\n"
"<footer id='ft'></footer>\n"
"</div>\n"
"\n"
"<div class='ovl' id='ovl'><div class='dlg'>\n"
"<div class='dhead'><span id='dTtl'>DETAIL</span>\n"
"<span class='r'><button class='tbtn' id='dX'>ZAVRIT [ESC]</button></span></div>\n"
"<div class='dbody'>\n"
"<div class='dcw'><svg id='dSvg' viewBox='0 0 100 100' preserveAspectRatio='none'></svg>\n"
"<div class='dax' id='dAx'></div></div>\n"
"<div class='dst' id='dSt'></div>\n"
"<div id='dSat'></div>\n"
"<div class='dnote' id='dNote'></div>\n"
"</div></div></div>\n"
"\n"
"<script>\n"
"var $=function(i){return document.getElementById(i);};\n"
"var SVGNS='http://www.w3.org/2000/svg';\n"
"var GATE=[0.1,1,10,100];\n"
"var MAXH=3600, win=300;\n"
"var H={f:[],t0:[],t1:[],t2:[],t3:[],vc:[],p0:[],p1:[],p2:[],p3:[]};\n"
"var NOMV={p0:12000,p1:5000,p2:2500,p3:3300};\n"
"var lastOk=0, zoom=null, pollTimer=0;\n"
"\n"
"/* -- Historie SKUTECNYCH MERENI (oddelena od 1Hz pollingu) ------------------\n"
" * !! Allan a drift se NESMI pocitat z pollovaci historie: poll bezi 1 Hz, ale\n"
" * mereni chodi jinym tempem podle brany, takze bychom tentyz vzorek zapocitali\n"
" * nekolikrat. Opakovane hodnoty vypadaji jako dokonala stabilita -> sigma_y by\n"
" * vysla nesmyslne NIZKA. Proto se sem prida vzorek jen kdyz se zmeni seq_meas.\n"
" * !! Allan take vyzaduje ROVNOMERNE rozestupy, takze pri zmene brany se buffer\n"
" * zahodi (jinak by se michala dve ruzna tau0).\n"
" * !! sigma_tau/offset/drift z IPC snapshotu se ZAMERNE nepouzivaji - CM7 je\n"
" * neplni, protoze jejich zdroj je zatim simulace headline (viz ipc.c). */\n"
"var MAXM=2000, M={t:[],f:[]};\n"
"var lastSeq=-1, lastGate=-1, nom=null, nomAuto=1, lastAdev=null;\n"
"\n"
"/* -- Prezitie historie mereni pres F5 (localStorage) ------------------------\n"
" * Bez toho reload zahodil cely buffer a Allan/drift zacinaly od nuly.\n"
" * !! Data se obnovi JEN kdyz je mezera od posledniho vzorku kratka. Allan\n"
" * potrebuje ROVNOMERNE rozestupy, takze slepit vzorky pres nekolikaminutovou\n"
" * diru (zavreny prohlizec) by dalo nesmyslne sigma_y — radeji zacit znovu.\n"
" * Stejny duvod, proc se buffer zahazuje pri zmene brany. */\n"
"var MGAP=30;   /* max. mezera [s], pres kterou se jeste smi navazat */\n"
"function saveM(){\n"
"  try{ localStorage.setItem('gm',JSON.stringify({t:M.t,f:M.f,g:lastGate,n:nom,na:nomAuto})); }catch(e){}\n"
"}\n"
"function loadM(){\n"
"  try{\n"
"    var o=JSON.parse(localStorage.getItem('gm')||'null');\n"
"    if(!o||!o.t||!o.t.length) return;\n"
"    var gap=Date.now()/1000-o.t[o.t.length-1];\n"
"    if(gap<0||gap>MGAP) return;            /* prilis stara data -> zacni znovu */\n"
"    M.t=o.t; M.f=o.f; lastGate=(o.g===undefined)?-1:o.g;\n"
"    if(o.n){ nom=o.n; nomAuto=o.na?1:0; $('nom').value=String(nom); }\n"
"  }catch(e){}\n"
"}\n"
"/* v12: DL = dlouha historie z datalogu (24h/7d/30d); null = zive okno z H[]. */\n"
"var DL=null, dlWin=0, webCtl=0, mathEn=0, nullEn=0, limEn=0, streaming=0;\n"
"var SAT=null, LAST=null;   /* posledni /api/sats a /api/state — pro detailni okna */\n"
"/* Kompaktni doba [s] -> text. Pouziva se pro osu X i pro rozsah historie. */\n"
"function dur(s){ s=Math.round(s);\n"
"  if(s<120) return s+' s';\n"
"  if(s<7200) return Math.round(s/60)+' min';\n"
"  if(s<172800) return (Math.round(s/360)/10)+' h';\n"
"  return (Math.round(s/8640)/10)+' d'; }\n"
"/* zdroj dat pro maly graf: v rezimu DL z datalogu, jinak zive okno H[]. */\n"
"function src(k){\n"
"  if(DL){ var mp={f:'f',t0:'o',t1:'b',vc:'vc',p3:'vbat'};\n"
"    return (mp[k]!==undefined)?DL[mp[k]]:[]; }\n"
"  return tail(k);\n"
"}\n"
"\n"
"function push(k,v){ var a=H[k]; a.push(v); if(a.length>MAXH) a.shift(); }\n"
"function tail(k){ var a=H[k]; return a.slice(Math.max(0,a.length-win)); }\n"
"function clean(a){ var o=[],i; for(i=0;i<a.length;i++) if(a[i]!==null&&a[i]!==undefined) o.push(a[i]); return o; }\n"
"\n"
"/* -- pomocne -------------------------------------------------------------- */\n"
"function span(arrs){\n"
"  var lo=null,hi=null,i,j,a;\n"
"  for(j=0;j<arrs.length;j++){ a=clean(arrs[j]);\n"
"    for(i=0;i<a.length;i++){ if(lo===null||a[i]<lo)lo=a[i]; if(hi===null||a[i]>hi)hi=a[i]; } }\n"
"  if(lo===null) return null;\n"
"  if(hi-lo<1e-12){ lo-=1; hi+=1; }\n"
"  var pad=(hi-lo)*0.12; return [lo-pad,hi+pad];\n"
"}\n"
"function pointsOf(arr,lo,hi){\n"
"  var n=arr.length,rng=hi-lo,p='',i,x,y,seen=0;\n"
"  if(rng<=0) rng=1;\n"
"  for(i=0;i<n;i++){\n"
"    if(arr[i]===null||arr[i]===undefined) continue;\n"
"    x=(n<2?0:i*100/(n-1)); y=100-(arr[i]-lo)*100/rng;\n"
"    if(y<0)y=0; if(y>100)y=100;\n"
"    p+=x.toFixed(2)+','+y.toFixed(2)+' '; seen++;\n"
"  }\n"
"  return seen>1?p:'';\n"
"}\n"
"function poly(id,arr,lo,hi,fillId){\n"
"  var p=pointsOf(arr,lo,hi);\n"
"  $(id).setAttribute('points',p);\n"
"  if(fillId) $(fillId).setAttribute('points',p?('0,100 '+p+'100,100'):'');\n"
"  return p!=='';\n"
"}\n"
"function ylab(t,b,lo,hi,f){ $(t).textContent=f(hi); $(b).textContent=f(lo); }\n"
"/* Pozice POSLEDNIHO platneho bodu v procentech plochy — pro koncovy bod krivky\n"
" * (kresli se jako HTML overlay, aby ho roztazeny viewBox nezdeformoval na elipsu). */\n"
"function lastXY(arr,lo,hi){\n"
"  var n=arr.length,rng=hi-lo,i;\n"
"  if(rng<=0) rng=1;\n"
"  for(i=n-1;i>=0;i--) if(arr[i]!==null&&arr[i]!==undefined){\n"
"    var y=100-(arr[i]-lo)*100/rng; if(y<0)y=0; if(y>100)y=100;\n"
"    return [(n<2?0:i*100/(n-1)), y];\n"
"  }\n"
"  return null;\n"
"}\n"
"function fx(v,d){ return (v===null||v===undefined)?'--':v.toFixed(d===undefined?2:d); }\n"
"function sci(v){ if(v===null||v===undefined||!isFinite(v)) return '--'; return v.toExponential(2); }\n"
"function stats(a){\n"
"  var c=clean(a),i,s=0,q=0; if(!c.length) return null;\n"
"  for(i=0;i<c.length;i++) s+=c[i]; var m=s/c.length;\n"
"  for(i=0;i<c.length;i++) q+=(c[i]-m)*(c[i]-m);\n"
"  return {n:c.length,min:Math.min.apply(null,c),max:Math.max.apply(null,c),\n"
"          mean:m,sd:Math.sqrt(q/Math.max(1,c.length-1)),last:c[c.length-1]};\n"
"}\n"
"\n"
"/* -- POPIS GRAFU: jeden zdroj pravdy pro maly nahled i detailni okno -------- */\n"
"function spec(k){\n"
"  var i,j,s={k:k,series:[],fmt:function(v){return v.toFixed(2);},note:'',title:''};\n"
"  if(k==='freq'){\n"
"    var fa=src('f'), fc=clean(fa), m=0;\n"
"    for(i=0;i<fc.length;i++) m+=fc[i]; if(fc.length) m/=fc.length;\n"
"    var dev=[]; for(i=0;i<fa.length;i++) dev.push((fa[i]===null||fa[i]===undefined)?null:fa[i]-m);\n"
"    s.title='KMITOCET - odchylka od prumeru okna'; s.base=m;\n"
"    s.series=[{n:'odchylka',c:0,a:dev}];\n"
"    /* Obalka min-max v bucketu (jen dlouha historie): prosta decimace by vykyv\n"
"     * MEZI vzorky neukazala. Nejsou to samostatne rady - kresli se jako pasmo. */\n"
"    if(DL&&DL.lo){\n"
"      var lo=[],hi=[],anyE=0;\n"
"      for(i=0;i<DL.lo.length;i++){\n"
"        var a1=DL.lo[i], b1=DL.hi[i];\n"
"        lo.push((a1===null||a1===undefined)?null:a1-m);\n"
"        hi.push((b1===null||b1===undefined)?null:b1-m);\n"
"        if(a1!==null&&a1!==undefined) anyE=1;\n"
"      }\n"
"      if(anyE){ s.env={lo:lo,hi:hi}; }\n"
"    }\n"
"    s.fmt=function(v){ return (v>=0?'+':'')+v.toFixed(5)+' Hz'; };\n"
"    s.note='Graf ukazuje ODCHYLKU od prumeru okna, ne absolutni hodnotu - v celych Hz by na 10 MHz nebylo videt nic. '\n"
"      +(DL?'Zdroj: datalog z W25Q (decimovano).':'Zdroj: poll 1 Hz z prohlizece.');\n"
"  } else if(k==='temp'){\n"
"    s.title='TEPLOTY';\n"
"    s.series=[{n:'OCXO',c:0,a:src('t0')},{n:'DESKA',c:1,a:src('t1')},\n"
"              {n:'MCU',c:2,a:src('t2')},{n:'FPGA',c:3,a:src('t3')}];\n"
"    s.fmt=function(v){ return v.toFixed(2)+' C'; };\n"
"    s.note='FPGA deska (TMP117 0x4A) neni osazena - jeji rada zustane prazdna.'\n"
"      +(DL?' V dlouhych oknech chybi i MCU: datalog uklada jen OCXO a desku.':'');\n"
"  } else if(k==='vc'){\n"
"    s.title='LADICI NAPETI OCXO (Vc)';\n"
"    s.series=[{n:'Vc',c:1,a:src('vc')}];\n"
"    s.fmt=function(v){ return (v/1000).toFixed(4)+' V'; };\n"
"    s.note='Vc ukazuje, jak si disciplinacni smycka doladuje oscilator. Trvaly beh k jednomu kraji = OCXO dochazi ladici rozsah.';\n"
"  } else {\n"
"    var ks=['p0','p1','p2','p3'], nm=['12 V','5 V','VREF','VBAT'], cc=[0,3,1,2];\n"
"    s.title='NAPAJENI - odchylka od nominalu';\n"
"    for(j=0;j<4;j++){\n"
"      var a=src(ks[j]), nv=NOMV[ks[j]], o=[];\n"
"      for(i=0;i<a.length;i++) o.push((a[i]===null||a[i]===undefined)?null:(a[i]-nv)*100/nv);\n"
"      s.series.push({n:nm[j],c:cc[j],a:o});\n"
"    }\n"
"    s.fmt=function(v){ return (v>=0?'+':'')+v.toFixed(3)+' %'; };\n"
"    s.note='Vetve jsou v PROCENTECH od nominalu - v milivoltech by 12 V zplostilo ostatni na nulu.'\n"
"      +(DL?' V dlouhych oknech je jen VBAT: 12 V/5 V/VREF datalog neuklada.':'');\n"
"  }\n"
"  var arrs=[]; for(j=0;j<s.series.length;j++) arrs.push(s.series[j].a);\n"
"  if(s.env){ arrs.push(s.env.lo); arrs.push(s.env.hi); }   /* pasmo musi byt v meritku */\n"
"  s.span=span(arrs);\n"
"  return s;\n"
"}\n"
"/* Pasmo min-max jako vyplneny polygon: hi zleva doprava, lo zpet. */\n"
"function envPoints(e,lo,hi){\n"
"  var n=e.hi.length,rng=hi-lo,i,up='',dn='',seen=0;\n"
"  if(rng<=0) rng=1;\n"
"  function yy(v){ var y=100-(v-lo)*100/rng; return y<0?0:(y>100?100:y); }\n"
"  for(i=0;i<n;i++){\n"
"    if(e.hi[i]===null||e.hi[i]===undefined||e.lo[i]===null||e.lo[i]===undefined) continue;\n"
"    var x=(n<2?0:i*100/(n-1));\n"
"    up+=x.toFixed(2)+','+yy(e.hi[i]).toFixed(2)+' ';\n"
"    dn=x.toFixed(2)+','+yy(e.lo[i]).toFixed(2)+' '+dn;\n"
"    seen++;\n"
"  }\n"
"  return seen>1?(up+dn):'';\n"
"}\n"
"\n"
"/* -- detailni okno --------------------------------------------------------- */\n"
"function mk(tag,cls){ var e=document.createElementNS(SVGNS,tag); if(cls) e.setAttribute('class',cls); return e; }\n"
"\n"
"/* Rozpad detailu po JEDNOTLIVYCH radach (teploty, napajeci vetve, ...).\n"
" * Souhrn nad grafem ukazuje min/max/prumer jen PRVNI rady, takze u vice senzoru\n"
" * se z nej nedalo poznat, ktery z nich se pohnul. Tady ma kazdy senzor svuj radek.\n"
" * Statistika se pocita z prave zobrazeneho okna, ne za celou dobu behu. */\n"
"function seriesTable(s){\n"
"  /* !! Trida musi byt JEDNOHODNOTOVA (atributy z innerHTML jsou bez uvozovek),\n"
"   * takze hlavickovy radek se znaci data-atributem, ne druhou tridou. */\n"
"  var h='<div class=dtab><div class=drow data-hd=1><b>SENZOR</b><span>AKTUALNI</span>'\n"
"    +'<span>MIN</span><span>MAX</span><span>ROZKMIT</span><span>SMER.ODCH.</span></div>';\n"
"  var any=0,j;\n"
"  for(j=0;j<s.series.length;j++){\n"
"    var st=stats(s.series[j].a);\n"
"    if(!st){\n"
"      h+='<div class=drow><b><em style=background:var(--c'+s.series[j].c+')></em>'\n"
"        +s.series[j].n+'</b><span>--</span><span>--</span><span>--</span>'\n"
"        +'<span>--</span><span>--</span></div>';\n"
"      continue; }\n"
"    any=1;\n"
"    h+='<div class=drow><b><em style=background:var(--c'+s.series[j].c+')></em>'\n"
"      +s.series[j].n+'</b><span class=hi>'+s.fmt(st.last)+'</span><span>'+s.fmt(st.min)\n"
"      +'</span><span>'+s.fmt(st.max)+'</span><span>'+s.fmt(st.max-st.min)\n"
"      +'</span><span>'+s.fmt(st.sd)+'</span></div>';\n"
"  }\n"
"  h+='</div>';\n"
"  return any?h:'';\n"
"}\n"
"function openZoom(k){ zoom=k; $('ovl').className='ovl on'; drawZoom(); }\n"
"function closeZoom(){ zoom=null; $('ovl').className='ovl'; }\n"
"\n"
"function drawZoom(){\n"
"  if(!zoom) return;\n"
"  var svg=$('dSvg'), ax=$('dAx'), i, j;\n"
"  while(svg.firstChild) svg.removeChild(svg.firstChild);\n"
"  ax.innerHTML='';\n"
"  /* Sky plot potrebuje ctvercovy pomer, grafy naopak roztazeny na celou plochu. */\n"
"  if(zoom==='gps'){ svg.setAttribute('viewBox','-100 -100 200 200');\n"
"                    svg.setAttribute('preserveAspectRatio','xMidYMid meet');\n"
"                    drawZoomGps(svg,ax); return; }\n"
"  svg.setAttribute('viewBox','0 0 100 100');\n"
"  svg.setAttribute('preserveAspectRatio','none');\n"
"  $('dSat').innerHTML='';   /* tabulka druzic patri jen do GPS detailu */\n"
"\n"
"  if(zoom==='adev'){ drawZoomAdev(svg,ax); return; }\n"
"\n"
"  var s=spec(zoom);\n"
"  $('dTtl').textContent=s.title;\n"
"  $('dNote').textContent=s.note;\n"
"  if(!s.span){ $('dSt').innerHTML=''; return; }\n"
"  var lo=s.span[0], hi=s.span[1];\n"
"\n"
"  for(i=1;i<10;i++){ var g=mk('line','g');\n"
"    g.setAttribute('x1',0); g.setAttribute('x2',100);\n"
"    g.setAttribute('y1',i*10); g.setAttribute('y2',i*10); svg.appendChild(g); }\n"
"  for(i=0;i<=4;i++){\n"
"    var b=document.createElement('b'); b.style.top=(i*25)+'%';\n"
"    b.textContent=s.fmt(hi-(hi-lo)*i/4); ax.appendChild(b);\n"
"  }\n"
"  /* !! Osa X musi vychazet z CASU, ne z poctu vzorku: v rezimu datalogu je jeden\n"
"   * vzorek step*10 s (u okna 24 h klidne 30 min), takze puvodni 'vzorek = 1 s'\n"
"   * popisovalo 24 h jako '48 s'. V zivem okne vzorek = 1 s (poll 1 Hz). */\n"
"  var n=s.series[0].a.length;\n"
"  var totalS=(DL&&DL.t.length>1)?(DL.t[DL.t.length-1]-DL.t[0]):n;\n"
"  for(i=0;i<=4;i++){\n"
"    var u=document.createElement('u'); u.style.left=(i*25)+'%';\n"
"    var age=totalS*(1-i/4);\n"
"    u.textContent=(age<1?'ted':('-'+dur(age)));\n"
"    ax.appendChild(u);\n"
"  }\n"
"  for(j=0;j<s.series.length;j++){\n"
"    var pl=mk('polyline','ln s'+s.series[j].c);\n"
"    pl.setAttribute('points',pointsOf(s.series[j].a,lo,hi));\n"
"    svg.appendChild(pl);\n"
"  }\n"
"  var h='';\n"
"  for(j=0;j<s.series.length;j++){\n"
"    var st=stats(s.series[j].a);\n"
"    h+='<div><div class=k>'+s.series[j].n+'</div><div class=v>'+(st?s.fmt(st.last):'--')+'</div></div>';\n"
"  }\n"
"  var s0=stats(s.series[0].a);\n"
"  if(s0){\n"
"    h+='<div><div class=k>MIN</div><div class=v>'+s.fmt(s0.min)+'</div></div>';\n"
"    h+='<div><div class=k>MAX</div><div class=v>'+s.fmt(s0.max)+'</div></div>';\n"
"    h+='<div><div class=k>PP</div><div class=v>'+s.fmt(s0.max-s0.min)+'</div></div>';\n"
"    h+='<div><div class=k>PRUMER</div><div class=v>'+s.fmt(s0.mean)+'</div></div>';\n"
"    h+='<div><div class=k>SMER. ODCH.</div><div class=v>'+s.fmt(s0.sd)+'</div></div>';\n"
"    h+='<div><div class=k>VZORKU</div><div class=v>'+s0.n+'</div></div>';\n"
"  }\n"
"  $('dSt').innerHTML=h;\n"
"  $('dSat').innerHTML=seriesTable(s);   /* rozpad po jednotlivych senzorech/vetvich */\n"
"}\n"
"\n"
"/* -- detail GPS: velky sky plot + prehled druzic ---------------------------- */\n"
"var CONST=['GPS','GLONASS','Galileo','BeiDou'];\n"
"function cnCol(cn){ return cn>=40?'var(--ok)':(cn>=25?'var(--warn)':'var(--bad)'); }\n"
"function drawZoomGps(svg,ax){\n"
"  $('dTtl').textContent='GPS - sky plot a druzice';\n"
"  $('dNote').textContent='Stred = zenit (elevace 90 st.), okraj = obzor. Kruznice po 30 st. elevace, '\n"
"    +'azimut 0 st. = sever po smeru hodin. Barva = C/N0: zelena >=40, zluta >=25, cervena niz. '\n"
"    +'Prazdne kolecko = druzice v dosahu, ale netrackovana. Prefix pismene = souhvezdi (G/R/E/C).';\n"
"  var i, e;\n"
"  /* Mrizka: elevacni kruznice + svetove strany. */\n"
"  var rings=[92,61,31];\n"
"  for(i=0;i<rings.length;i++){ e=mk('circle','skc'); e.setAttribute('r',rings[i]); svg.appendChild(e); }\n"
"  e=mk('line','skc'); e.setAttribute('x1',-92); e.setAttribute('y1',0);\n"
"  e.setAttribute('x2',92); e.setAttribute('y2',0); svg.appendChild(e);\n"
"  e=mk('line','skc'); e.setAttribute('x1',0); e.setAttribute('y1',-92);\n"
"  e.setAttribute('x2',0); e.setAttribute('y2',92); svg.appendChild(e);\n"
"  var dirs=[['S',0,-95],['J',0,101],['V',99,3],['Z',-99,3]];\n"
"  for(i=0;i<dirs.length;i++){ e=mk('text','skt'); e.setAttribute('x',dirs[i][1]);\n"
"    e.setAttribute('y',dirs[i][2]); e.textContent=dirs[i][0]; svg.appendChild(e); }\n"
"  var elv=[['60',0,-58],['30',0,-28]];\n"
"  for(i=0;i<elv.length;i++){ e=mk('text','skp'); e.setAttribute('x',elv[i][1]);\n"
"    e.setAttribute('y',elv[i][2]); e.textContent=elv[i][0]; svg.appendChild(e); }\n"
"\n"
"  var S=(SAT&&SAT.s)?SAT.s.slice(0):[];\n"
"  for(i=0;i<S.length;i++){\n"
"    var prn=S[i][0], el=S[i][1], az=S[i][2], cn=S[i][3], co=S[i][4];\n"
"    var r=92*(90-el)/90; if(r<0)r=0;\n"
"    var a=az*Math.PI/180, x=r*Math.sin(a), y=-r*Math.cos(a);\n"
"    e=mk('circle'); e.setAttribute('cx',x.toFixed(1)); e.setAttribute('cy',y.toFixed(1));\n"
"    e.setAttribute('r',cn>0?6:4);\n"
"    e.setAttribute('fill',cn>0?cnCol(cn):'none');\n"
"    e.setAttribute('stroke',cn>0?'var(--pnl)':'var(--dim)');\n"
"    e.setAttribute('stroke-width',cn>0?'0.8':'1.2'); svg.appendChild(e);\n"
"    e=mk('text','skp'); e.setAttribute('x',x.toFixed(1)); e.setAttribute('y',(y-8).toFixed(1));\n"
"    e.textContent=(('GREC').charAt(co)||'G')+prn; svg.appendChild(e);\n"
"  }\n"
"\n"
"  /* Souhrn + tabulka druzic serazenych podle C/N0 (nejsilnejsi prvni). */\n"
"  var g=(LAST&&LAST.gps)?LAST.gps:{}, used=g.num_sat||0;\n"
"  var trk=0, sum=0;\n"
"  for(i=0;i<S.length;i++) if(S[i][3]>0){ trk++; sum+=S[i][3]; }\n"
"  var fm=g.fix_mode||0;\n"
"  var h='';\n"
"  h+='<div><div class=k>FIX</div><div class=v>'+(fm>=3?'3D':(fm===2?'2D':'bez fixu'))+'</div></div>';\n"
"  h+='<div><div class=k>POUZITO / VIDENO</div><div class=v>'+used+' / '+S.length+'</div></div>';\n"
"  h+='<div><div class=k>TRACKOVANO</div><div class=v>'+trk+'</div></div>';\n"
"  h+='<div><div class=k>PRUMER C/N0</div><div class=v>'+(trk?Math.round(sum/trk)+' dB':'--')+'</div></div>';\n"
"  h+='<div><div class=k>HDOP</div><div class=v>'+(g.hdop===null||g.hdop===undefined?'--':g.hdop)+'</div></div>';\n"
"  h+='<div><div class=k>CAS UTC</div><div class=v>'+(g.time||'--')+'</div></div>';\n"
"  h+='<div><div class=k>POLOHA</div><div class=v>'+((g.lat===null||g.lat===undefined)?'--':(g.lat+', '+g.lon))+'</div></div>';\n"
"  h+='<div><div class=k>VYSKA</div><div class=v>'+((g.alt_m===null||g.alt_m===undefined)?'--':(g.alt_m+' m'))+'</div></div>';\n"
"  $('dSt').innerHTML=h;\n"
"\n"
"  S.sort(function(p,q){ return q[3]-p[3]; });\n"
"  var t='<div class=sattab>';\n"
"  for(i=0;i<S.length;i++){\n"
"    var w=Math.min(100,S[i][3]*2);\n"
"    t+='<div class=satrow><b>'+(('GREC').charAt(S[i][4])||'G')+S[i][0]+'</b>'\n"
"      +'<i>'+(CONST[S[i][4]]||'?')+'</i>'\n"
"      +'<u>el '+S[i][1]+' st.</u><u>az '+S[i][2]+' st.</u>'\n"
"      +'<div class=satbar><span style=width:'+w+'%;background:'+cnCol(S[i][3])+'></span></div>'\n"
"      +'<em>'+(S[i][3]>0?S[i][3]+' dB':'--')+'</em></div>';\n"
"  }\n"
"  t+='</div>';\n"
"  if(!S.length) t='<div class=dnote>Zadne druzice - ceka se na prvni data z prijimace.</div>';\n"
"  $('dSat').innerHTML=t;   /* vlastni kontejner -> prepise se, nehromadi se */\n"
"}\n"
"\n"
"function drawZoomAdev(svg,ax){\n"
"  $('dTtl').textContent='ALLAN sigma_y(tau) - log-log';\n"
"  $('dNote').textContent='Prekryvajici se (overlapping) ADEV z realnych mereni z FPGA, tau0 = skutecny rozestup mereni. '\n"
"    +'Cim vic paru, tim duveryhodnejsi odhad - relativni nejistota klesa zhruba jako 1/sqrt(2N).';\n"
"  var A=lastAdev,i;\n"
"  if(!A||A.length<2){ $('dSt').innerHTML=''; return; }\n"
"  var lt=[],ls=[];\n"
"  for(i=0;i<A.length;i++){ lt.push(Math.log(A[i].tau)/Math.LN10);\n"
"    ls.push(A[i].sig>0?Math.log(A[i].sig)/Math.LN10:null); }\n"
"  var cs=clean(ls); if(cs.length<2){ $('dSt').innerHTML=''; return; }\n"
"  var slo=Math.floor(Math.min.apply(null,cs)), shi=Math.ceil(Math.max.apply(null,cs));\n"
"  if(shi-slo<1) shi=slo+1;\n"
"  var tlo=lt[0], thi=lt[lt.length-1];\n"
"  for(i=slo;i<=shi;i++){\n"
"    var y=100-(i-slo)*100/(shi-slo);\n"
"    var g=mk('line','g'); g.setAttribute('x1',0); g.setAttribute('x2',100);\n"
"    g.setAttribute('y1',y); g.setAttribute('y2',y); svg.appendChild(g);\n"
"    var b=document.createElement('b'); b.style.top=y+'%'; b.textContent='1e'+i; ax.appendChild(b);\n"
"  }\n"
"  var p='';\n"
"  for(i=0;i<A.length;i++){\n"
"    if(ls[i]===null) continue;\n"
"    var x=(thi>tlo)?((lt[i]-tlo)*100/(thi-tlo)):0;\n"
"    var yy=100-(ls[i]-slo)*100/(shi-slo);\n"
"    if(yy<0)yy=0; if(yy>100)yy=100;\n"
"    p+=x.toFixed(2)+','+yy.toFixed(2)+' ';\n"
"    var u=document.createElement('u'); u.style.left=x+'%';\n"
"    u.textContent=A[i].tau<1?A[i].tau.toFixed(2):A[i].tau.toFixed(0); ax.appendChild(u);\n"
"  }\n"
"  var pl=mk('polyline','ln s3'); pl.setAttribute('points',p); svg.appendChild(pl);\n"
"  var h='';\n"
"  for(i=0;i<A.length;i++)\n"
"    h+='<div><div class=k>tau '+(A[i].tau<1?A[i].tau.toFixed(2):A[i].tau.toFixed(0))\n"
"      +' s</div><div class=v>'+sci(A[i].sig)+'</div></div>';\n"
"  h+='<div><div class=k>PARU (posl.)</div><div class=v>'+A[A.length-1].n+'</div></div>';\n"
"  $('dSt').innerHTML=h;\n"
"}\n"
"\n"
"/* -- formatovani ----------------------------------------------------------- */\n"
"function grp3(t){ var o='',c=0,i; for(i=t.length-1;i>=0;i--){ o=t.charAt(i)+o; if((++c)%3===0&&i>0)o=' '+o; } return o; }\n"
"function fmtFreq(v){\n"
"  if(v===null||v===undefined) return null;\n"
"  var neg=v<0; if(neg)v=-v;\n"
"  var ip=Math.floor(v), fp=Math.round((v-ip)*100000);\n"
"  if(fp>=100000){ip++;fp=0;}\n"
"  var fs=String(fp); while(fs.length<5) fs='0'+fs;\n"
"  return (neg?'-':'')+grp3(String(ip))+','+fs;\n"
"}\n"
"/* Headline jako na DISPLEJI: posledni duveryhodna cislice modre podtrzena,\n"
" * za ni nejista mista mensim pismem v odstinu sedi. Web tak vypada stejne jako\n"
" * pristroj. (Web dostava 5 desetin z x1e5; displej pri /4 dopocitava 7 z\n"
" * edge_count/gate_ns - je to tataz hodnota, jen jinak zaokrouhlena.) */\n"
"function fmtFreqHtml(v){\n"
"  if(v===null||v===undefined) return null;\n"
"  var neg=v<0; if(neg)v=-v;\n"
"  var ip=Math.floor(v), fp=Math.round((v-ip)*100000);\n"
"  if(fp>=100000){ip++;fp=0;}\n"
"  var fs=String(fp); while(fs.length<5) fs='0'+fs;\n"
"  return (neg?'-':'')+grp3(String(ip))+','+fs.substring(0,2)\n"
"    +'<b class=fc>'+fs.charAt(2)+'</b><i class=fu>'+fs.substring(3)+'</i>';\n"
"}\n"
"function fmtPer(v){\n"
"  if(!v) return null; var p=1/v;\n"
"  if(p<1e-6) return (p*1e9).toFixed(4)+' ns';\n"
"  if(p<1e-3) return (p*1e6).toFixed(4)+' us';\n"
"  if(p<1) return (p*1e3).toFixed(5)+' ms';\n"
"  return p.toFixed(6)+' s';\n"
"}\n"
"function upt(t){ var d=Math.floor(t/86400),h=Math.floor(t%86400/3600),m=Math.floor(t%3600/60),s=t%60;\n"
"  if(d) return d+' d '+h+' h'; if(h) return h+' h '+m+' m'; if(m) return m+' m '+s+' s'; return s+' s'; }\n"
"function setv(id,v){ var e=$(id); if(v===null||v===undefined){e.textContent='--';e.setAttribute('data-na','1');}\n"
"  else {e.textContent=v;e.removeAttribute('data-na');} }\n"
"function pill(d,t,txt,st){ $(d).className='dot'+(st?' '+st:''); $(t).textContent=txt; }\n"
"\n"
"/* -- Allan / proklad ------------------------------------------------------- */\n"
"/* Prekryvajici se (overlapping) sigma_y(tau) z fazi x[k+1] = x[k] + y[k]*tau0:\n"
" *   sigma_y^2(tau) = 1/(2*(N-2m)*tau^2) * SUM (x[i+2m] - 2*x[i+m] + x[i])^2\n"
" * !! Prekryvajici varianta (posun po JEDNOM vzorku) vytezi z tychz dat N-2m\n"
" * clenu misto N/m, takze na delsich tau je odhad radove duveryhodnejsi. */\n"
"function adev(y,tau0){\n"
"  var N=y.length; if(N<4||!(tau0>0)) return [];\n"
"  var x=[0],i; for(i=0;i<N;i++) x.push(x[i]+y[i]*tau0);\n"
"  var out=[],m=1;\n"
"  while(3*m<=N){\n"
"    var s=0,cnt=0,d;\n"
"    for(i=0;i+2*m<x.length;i++){ d=x[i+2*m]-2*x[i+m]+x[i]; s+=d*d; cnt++; }\n"
"    if(cnt>0){ var t=m*tau0; out.push({tau:t,sig:Math.sqrt(s/(2*cnt*t*t)),n:cnt}); }\n"
"    m*=2;\n"
"  }\n"
"  return out;\n"
"}\n"
"/* !! Vraci i korelaci r - bez ni by se proklad SUMU cetl jako zmereny drift. */\n"
"function fit(y,tau0){\n"
"  var N=y.length; if(N<3||!(tau0>0)) return null;\n"
"  var sx=0,sy=0,sxx=0,sxy=0,syy=0,i,t;\n"
"  for(i=0;i<N;i++){ t=i*tau0; sx+=t; sy+=y[i]; sxx+=t*t; sxy+=t*y[i]; syy+=y[i]*y[i]; }\n"
"  var dn=N*sxx-sx*sx; if(Math.abs(dn)<1e-30) return null;\n"
"  var den=Math.sqrt(dn*(N*syy-sy*sy));\n"
"  return {a:(N*sxy-sx*sy)/dn, r:den>0?(N*sxy-sx*sy)/den:0};\n"
"}\n"
"\n"
"/* -- sit ------------------------------------------------------------------- */\n"
"function auth(){ var u=localStorage.getItem('gu')||'', p=localStorage.getItem('gp')||'';\n"
"  if(!u&&!p) return null; return 'Basic '+btoa(u+':'+p); }\n"
"function post(line){\n"
"  var h={'Content-Type':'text/plain'}, a=auth(); if(a) h['Authorization']=a;\n"
"  return fetch('/api/scpi',{method:'POST',headers:h,body:line}).then(function(r){return r.text();});\n"
"}\n"
"function say(id,t,cls){ var e=$(id); e.textContent=t; e.className='msg'+(cls?' '+cls:''); }\n"
"function isErr(t){ return t.charAt(0)==='-'&&t.indexOf(',')>0; }\n"
"function logln(cmd,res,err){\n"
"  var d=document.createElement('div'); if(err) d.className='e';\n"
"  var b=document.createElement('b'); b.textContent=cmd+'  ';\n"
"  d.appendChild(b); d.appendChild(document.createTextNode(res));\n"
"  var L=$('log'); L.insertBefore(d,L.firstChild);\n"
"  while(L.childNodes.length>60) L.removeChild(L.lastChild);\n"
"}\n"
"function cmd(line){\n"
"  say('msg',line+' ...','');\n"
"  post(line).then(function(t){ t=t.trim();\n"
"    say('msg',line+'  ->  '+(t||'OK'),isErr(t)?'err':'ok');\n"
"    logln(line,t||'OK',isErr(t)); setTimeout(poll,260);\n"
"  }).catch(function(e){ say('msg','chyba spojeni: '+e,'err'); });\n"
"}\n"
"function scon(line){\n"
"  post(line).then(function(t){ t=t.trim(); logln(line,t||'(bez odpovedi)',isErr(t)); })\n"
"  .catch(function(e){ logln(line,'chyba spojeni: '+e,1); });\n"
"}\n"
"function login(){\n"
"  var u=$('u').value.trim(), p=$('p').value;\n"
"  if(!u){u='admin';$('u').value=u;}\n"
"  if(!p){ say('amsg','Zadej heslo, ktere ukazuje okno PRISTUP na displeji.','err'); return; }\n"
"  localStorage.setItem('gu',u); localStorage.setItem('gp',p);\n"
"  say('amsg','overuji...','');\n"
"  post('*IDN?').then(function(){ return fetch('/api/state').then(function(r){return r.json();}); })\n"
"  .then(function(s){ var d=s.auth_debug||{};\n"
"    if(!s.web_ctrl_en) say('amsg','Ovladani je na pristroji ZAKAZANE - povol ho v okne PRISTUP.','err');\n"
"    else if(d.match) say('amsg','Prihlaseno, ovladani povoleno.','ok');\n"
"    else if(!d.header_present) say('amsg','Prohlizec neposlal prihlaseni - vypln jmeno i heslo.','err');\n"
"    else say('amsg','Neplatne jmeno nebo heslo (ceka se '+d.expected_len+' B, prislo '+d.decoded_len+' B).','err');\n"
"    render(s);\n"
"  }).catch(function(e){ say('amsg','chyba: '+e,'err'); });\n"
"}\n"
"\n"
"/* -- radky s barem --------------------------------------------------------- */\n"
"function rows(host,items){\n"
"  var h='',i,it;\n"
"  for(i=0;i<items.length;i++){ it=items[i];\n"
"    h+='<div class=row><div class=n>'+it.n+'</div>'\n"
"      +'<div class=bar'+(it.st?' '+it.st:'')+'><i style=width:'+it.pct.toFixed(1)+'%></i>'\n"
"      +(it.mark===undefined?'':'<u style=left:'+it.mark.toFixed(1)+'%></u>')+'</div>'\n"
"      +'<div class=val'+(it.v===null?' data-na=1':'')+'>'+(it.v===null?'--':it.v)+'</div></div>';\n"
"  }\n"
"  $(host).innerHTML=h;\n"
"}\n"
"function pctOf(v,lo,hi){ if(v===null||v===undefined) return 0;\n"
"  var p=(v-lo)*100/(hi-lo); return p<0?0:(p>100?100:p); }\n"
"function railSt(v,nv){ if(v===null||v===undefined) return '';\n"
"  var d=Math.abs(v-nv)/nv; return d<0.05?'ok':(d<0.15?'warn':'bad'); }\n"
"\n"
"/* -- render ---------------------------------------------------------------- */\n"
"function render(s){\n"
"  lastOk=Date.now(); LAST=s;\n"
"\n"
"  var f=fmtFreqHtml(s.freq_hz), fe=$('freq');\n"
"  if(f===null){ fe.textContent='--'; fe.setAttribute('data-na','1'); }\n"
"  else { fe.innerHTML=f+'<u>Hz</u>'; fe.removeAttribute('data-na'); }\n"
"  /* Zablesk pri NOVEM mereni (ne pri kazdem pollu) -> je videt, ze data tecou. */\n"
"  if(s.seq_meas!==undefined&&s.seq_meas!==lastSeq&&f!==null){\n"
"    fe.setAttribute('data-hit','1');\n"
"    setTimeout(function(){ fe.removeAttribute('data-hit'); },260);\n"
"  }\n"
"\n"
"  /* !! Emulace se MUSI poznat na prvni pohled - jinak by web vydaval data z\n"
"   * fpgasim za mereni (displej, UART i datalog je oznacuji). */\n"
"  if(s.sim){ $('simTag').setAttribute('data-on','1'); $('hero').setAttribute('data-sim','1'); }\n"
"  else { $('simTag').removeAttribute('data-on'); $('hero').removeAttribute('data-sim'); }\n"
"\n"
"  var why='';\n"
"  if(!s.running) why='Mereni je zastavene - stiskni RUN.';\n"
"  else if(!s.spi_ok) why='Neni spojeni s FPGA deskou (SPI link DOWN).';\n"
"  else if(s.signal_lost) why='FPGA nehlasi zadny vstupni signal.';\n"
"  else if(f===null) why='Ceka se na prvni platne mereni...';\n"
"  $('why').textContent=why;\n"
"\n"
"  setv('mPer',fmtPer(s.freq_hz));\n"
"  var f16=fmtFreq(s.freq16_hz); setv('mF16',f16===null?null:f16+' Hz');\n"
"  $('mGset').textContent=String(+s.set_gate_s).replace('.',',')+' s';\n"
"  setv('mGact',s.gate_ns===null?null:(s.gate_ns/1e6).toFixed(1)+' ms');\n"
"  $('mChan').textContent=s.set_chan?'B':'A';\n"
"  $('mUp').textContent=upt(s.uptime_s);\n"
"\n"
"  pill('dLink','tLink',s.spi_ok?'FPGA OK':'FPGA DOWN',s.spi_ok?'ok':'bad');\n"
"  var fm=s.gps.fix_mode;\n"
"  pill('dGps','tGps',(fm>=3?'GPS 3D':fm===2?'GPS 2D':'GPS --')+' '+s.gps.num_sat,\n"
"       fm>=3?'ok':(fm===2?'warn':'bad'));\n"
"  var rb=(!s.si5356_ok)||((s.si5356_status&0x18)!==0);\n"
"  pill('dRef','tRef',rb?'REF CHYBA':'REF OK',rb?'bad':'ok');\n"
"  pill('dRun','tRun',s.running?'MERI':'STOP',s.running?'ok':'warn');\n"
"\n"
"  push('f',s.freq_hz); push('t0',s.temp_ocxo_c); push('t1',s.temp_board_c);\n"
"  push('t2',s.temp_mcu_c); push('t3',s.temp_fpga_c); push('vc',s.vc_mv);\n"
"  push('p0',s.v12_mv); push('p1',s.v5_mv); push('p2',s.vref_mv); push('p3',s.vbat_mv);\n"
"\n"
"  if(s.set_gate_idx!==lastGate){ M.t=[]; M.f=[]; lastGate=s.set_gate_idx; }\n"
"  if(s.seq_meas!==undefined&&s.seq_meas!==lastSeq&&s.freq_hz!==null&&s.freq_hz!==undefined){\n"
"    lastSeq=s.seq_meas;\n"
"    M.t.push(Date.now()/1000); M.f.push(s.freq_hz);\n"
"    if(M.f.length>MAXM){ M.t.shift(); M.f.shift(); }\n"
"    if(nomAuto&&nom===null){ nom=autoNom(s.freq_hz); $('nom').value=String(nom); }\n"
"  }\n"
"  drawAll(); drawStab(); if(zoom) drawZoom();\n"
"\n"
"  rows('gTemp',[\n"
"   {n:'OCXO',pct:pctOf(s.temp_ocxo_c,20,80),v:fx(s.temp_ocxo_c)+' C',st:'bad'},\n"
"   {n:'DESKA',pct:pctOf(s.temp_board_c,20,80),v:fx(s.temp_board_c)+' C'},\n"
"   {n:'MCU',pct:pctOf(s.temp_mcu_c,20,80),v:fx(s.temp_mcu_c)+' C',st:'warn'},\n"
"   {n:'FPGA',pct:pctOf(s.temp_fpga_c,20,80),v:s.temp_fpga_c===null?null:fx(s.temp_fpga_c)+' C'}]);\n"
"\n"
"  rows('gPwr',[\n"
"   {n:'12 V',pct:pctOf(s.v12_mv,0,14000),mark:pctOf(12000,0,14000),\n"
"    v:s.v12_mv===null?null:(s.v12_mv/1000).toFixed(3)+' V',st:railSt(s.v12_mv,12000)},\n"
"   {n:'5 V',pct:pctOf(s.v5_mv,0,6000),mark:pctOf(5000,0,6000),\n"
"    v:s.v5_mv===null?null:(s.v5_mv/1000).toFixed(3)+' V',st:railSt(s.v5_mv,5000)},\n"
"   {n:'VREF',pct:pctOf(s.vref_mv,0,3300),mark:pctOf(2500,0,3300),\n"
"    v:s.vref_mv===null?null:(s.vref_mv/1000).toFixed(3)+' V',st:railSt(s.vref_mv,2500)},\n"
"   {n:'VBAT',pct:pctOf(s.vbat_mv,0,3600),mark:pctOf(3300,0,3600),\n"
"    v:s.vbat_mv===null?null:(s.vbat_mv/1000).toFixed(3)+' V',\n"
"    st:(s.vbat_mv!==null&&s.vbat_mv<2800)?'warn':'ok'},\n"
"   {n:'OCXO Vc',pct:pctOf(s.vc_mv,0,3300),v:s.vc_mv===null?null:(s.vc_mv/1000).toFixed(3)+' V'}]);\n"
"\n"
"  var ns=s.gps.num_sat, deg=Math.min(ns,12)*30;\n"
"  var col=fm>=3?'var(--ok)':(fm===2?'var(--warn)':'var(--bad)');\n"
"  $('dnut').style.background='conic-gradient('+col+' 0deg '+deg+'deg,var(--track) '+deg+'deg 360deg)';\n"
"  $('dnN').textContent=ns;\n"
"  $('gFix').textContent=fm>=3?'3D':(fm===2?'2D':'bez fixu');\n"
"  $('gTime').textContent=s.gps.time;\n"
"  var gp=s.gps;\n"
"  $('gPos').textContent=(gp.lat===null||gp.lat===undefined)?'--':(gp.lat+', '+gp.lon);\n"
"  $('gAlt').textContent=(gp.alt_m===null||gp.alt_m===undefined)?'--':(gp.alt_m+' m');\n"
"  $('gHdop').textContent=(gp.hdop===null||gp.hdop===undefined)?'--':('HDOP '+gp.hdop);\n"
"  $('gRef').textContent=rb?'CHYBA':'OK';\n"
"  var dbm=(s.rf_dbm===undefined||s.rf_dbm===null)?null:s.rf_dbm;\n"
"  var rtx=dbm===null?(s.rf_mv===null?'--':s.rf_mv+' mV'):dbm.toFixed(1)+' dBm';\n"
"  $('gRf').textContent=rtx; $('vRf').textContent=rtx;\n"
"  $('bRf').firstChild.style.width=(dbm===null?pctOf(s.rf_mv,0,2000):pctOf(dbm,-80,10)).toFixed(1)+'%';\n"
"\n"
"  segset('segRun','data-r',s.running?1:0);\n"
"  segset('segGate','data-g',s.set_gate_idx);\n"
"  segset('segChan','data-c',s.set_chan);\n"
"  var dis=!s.web_ctrl_en, bs=document.querySelectorAll('#segRun button,#segGate button,#segChan button');\n"
"  for(var i=0;i<bs.length;i++) bs[i].disabled=dis;\n"
"  $('lock').textContent=dis?'Ovladani je na pristroji ZAKAZANE. Povol ho v Nastaveni > SIT > PRISTUP.':'';\n"
"\n"
"  /* v12 (#1): stav ovladani MATH/LIMITY/CAS + zamek dle web_ctrl_en. */\n"
"  webCtl=s.web_ctrl_en?1:0;\n"
"  if(s.math){ mathEn=s.math.en?1:0; nullEn=s.math.null_en?1:0; limEn=s.math.limit_en?1:0; }\n"
"  $('bMath').textContent='MATH '+(mathEn?'ZAP':'VYP');\n"
"  $('bLim').textContent='LIMITY '+(limEn?'ZAP':'VYP');\n"
"  var cb=document.querySelectorAll('#bMath,#bNull,#bNullOff,#bLim,#bRtc,#mM,#mB,#mLo,#mHi,#rD,#rT');\n"
"  for(var k=0;k<cb.length;k++) cb[k].disabled=dis;\n"
"  $('vVerdict').textContent=(nullEn?'NULL ':'')+(mathEn?'MATH ':'')+(limEn?'LIMITY':'')||'vypnuto';\n"
"  renderStg(s);\n"
"\n"
"  $('ft').textContent='IPC v'+s.cm4.ipc_version+'  |  server na jadru CM4 (lwIP)  |  '\n"
"    +(streaming?'push (SSE)':'poll 1 Hz')+'  |  historie '+H.f.length+' vzorku  |  mereni '+M.f.length;\n"
"}\n"
"\n"
"/* v12 (#4): karta STAV / DIAGNOSTIKA — alarmy, prahy, selftest, sys level. */\n"
"function renderStg(s){\n"
"  var sl=s.sys_level, slc=sl>=2?'cbad':(sl>=1?'cwarn':'cok'), slt=sl>=2?'CHYBA':(sl>=1?'DEGRADACE':'OK');\n"
"  var st=s.selftest, stc=st===2?'cbad':(st===1?'cok':'cwarn'), stt=st===2?'FAIL':(st===1?'PASS':'---');\n"
"  var a=s.alarms||{f:0}, m=s.mon||{};\n"
"  var h='';\n"
"  h+='<b>SYSTEM</b><span><span class='+slc+'>'+slt+'</span></span>';\n"
"  h+='<b>SELFTEST</b><span><span class='+stc+'>'+stt+'</span></span>';\n"
"  h+='<b>VBAT prah</b><span><span class='+(m.vbat?'cbad':'cok')+'>'+(m.vbat?'MIMO':'OK')+'</span></span>';\n"
"  h+='<b>OCXO teplota</b><span><span class='+(m.ocxo?'cbad':'cok')+'>'+(m.ocxo?'MIMO':'OK')+'</span></span>';\n"
"  h+='<b>alarmy FPGA / GPS / limit</b><span>'+a.fpga+' / '+a.gps+' / '+a.lim+'</span>';\n"
"  h+='<b>alarmy VBAT / OCXO / ADEV</b><span>'+a.vbat+' / '+a.ocxo+' / '+a.adev+'</span>';\n"
"  $('stg').innerHTML=h;\n"
"}\n"
"\n"
"function drawAll(){\n"
"  var j,k, KS=['freq','temp','vc','pwr'];\n"
"  var IDS={freq:['lFreq'],temp:['lT0','lT1','lT2','lT3'],vc:['lVc'],pwr:['lP0','lP1','lP2','lP3']};\n"
"  var FILL={freq:'aFreq',vc:'aVc'};\n"
"  var NOD={freq:'nFreq',temp:'nTemp',vc:'nVc',pwr:'nPwr'};\n"
"  var YT={freq:['yFreqT','yFreqB'],temp:['yTempT','yTempB'],vc:['yVcT','yVcB'],pwr:['yPwrT','yPwrB']};\n"
"  var ST={freq:'stFreq',temp:'stTemp',vc:'stVc',pwr:'stPwr'};\n"
"  for(k=0;k<KS.length;k++){\n"
"    var key=KS[k], s=spec(key), ids=IDS[key], any=0;\n"
"    /* Pasmo min-max (jen graf kmitoctu v rezimu datalogu). */\n"
"    if(key==='freq') $('envFreq').setAttribute('points',\n"
"      (s.env&&s.span)?envPoints(s.env,s.span[0],s.span[1]):'');\n"
"    if(s.span){\n"
"      for(j=0;j<ids.length;j++)\n"
"        if(poly(ids[j],s.series[j].a,s.span[0],s.span[1],j===0?FILL[key]:null)) any=1;\n"
"    } else for(j=0;j<ids.length;j++) poly(ids[j],[],0,1,j===0?FILL[key]:null);\n"
"    $(NOD[key]).style.display=any?'none':'flex';\n"
"    /* Koncovy bod = aktualni hodnota prvni rady (barva rady). */\n"
"    var dot=$('e'+key), p=any?lastXY(s.series[0].a,s.span[0],s.span[1]):null;\n"
"    if(dot){\n"
"      if(p){ dot.style.display='block'; dot.style.left=p[0]+'%'; dot.style.top=p[1]+'%';\n"
"             dot.style.background='var(--c'+s.series[0].c+')'; }\n"
"      else dot.style.display='none';\n"
"    }\n"
"    if(any){\n"
"      ylab(YT[key][0],YT[key][1],s.span[0],s.span[1],s.fmt);\n"
"      var st=stats(s.series[0].a);\n"
"      $(ST[key]).textContent=st?(s.fmt(st.last)+'  pp '+s.fmt(st.max-st.min)):'--';\n"
"    } else $(ST[key]).textContent='--';\n"
"  }\n"
"}\n"
"\n"
"/* -- stabilita ------------------------------------------------------------- */\n"
"function autoNom(f){\n"
"  if(f>=1e6) return Math.round(f/1e6)*1e6;\n"
"  if(f>=1e3) return Math.round(f/1e3)*1e3;\n"
"  return f;\n"
"}\n"
"function drawStab(){\n"
"  var n=M.f.length,i,j;\n"
"  if(n<4){\n"
"    lastAdev=null;\n"
"    $('nAdev').style.display='flex'; $('stAdev').textContent='--';\n"
"    $('tAdev').innerHTML=''; setv('aHead',null);\n"
"    $('aWarn').textContent=n?('zatim '+n+' mereni - Allan potrebuje aspon 4'):'';\n"
"    $('lFit').setAttribute('points','');\n"
"    setv('dHead',null); $('tDrift').innerHTML=''; $('stDrift').textContent='--'; $('dWarn').textContent='';\n"
"    return;\n"
"  }\n"
"  /* tau0 = SKUTECNY prumerny rozestup mereni (ne nastavena brana): tempo urcuje\n"
"   * FPGA a pri nizkych kmitoctech se reciproke okno legitimne protahne. */\n"
"  var tau0=(M.t[n-1]-M.t[0])/(n-1);\n"
"  var mean=0; for(i=0;i<n;i++) mean+=M.f[i]; mean/=n;\n"
"  var y=[]; for(i=0;i<n;i++) y.push((M.f[i]-mean)/mean);\n"
"\n"
"  var A=adev(y,tau0); lastAdev=A;\n"
"  if(A.length){\n"
"    var lt=[],ls=[];\n"
"    for(j=0;j<A.length;j++){ lt.push(Math.log(A[j].tau)/Math.LN10);\n"
"      ls.push(A[j].sig>0?Math.log(A[j].sig)/Math.LN10:null); }\n"
"    var cs=clean(ls);\n"
"    if(cs.length>1){\n"
"      var slo=Math.min.apply(null,cs), shi=Math.max.apply(null,cs);\n"
"      if(shi-slo<0.5){ slo-=0.5; shi+=0.5; }\n"
"      var tlo=lt[0], thi=lt[lt.length-1], p='';\n"
"      for(j=0;j<A.length;j++){\n"
"        if(ls[j]===null) continue;\n"
"        var xx=(thi>tlo)?((lt[j]-tlo)*100/(thi-tlo)):0;\n"
"        var yy=100-(ls[j]-slo)*100/(shi-slo);\n"
"        if(yy<0)yy=0; if(yy>100)yy=100;\n"
"        p+=xx.toFixed(2)+','+yy.toFixed(2)+' ';\n"
"      }\n"
"      $('lAdev').setAttribute('points',p);\n"
"      $('yAdT').textContent=sci(Math.pow(10,shi)); $('yAdB').textContent=sci(Math.pow(10,slo));\n"
"      $('nAdev').style.display='none';\n"
"    }\n"
"    setv('aHead',sci(A[0].sig));\n"
"    $('aHeadL').textContent='SIGMA_Y @ '+A[0].tau.toFixed(A[0].tau<1?2:0)+' s';\n"
"    $('stAdev').textContent='tau0 '+tau0.toFixed(2)+' s  n='+n;\n"
"    var h='';\n"
"    for(j=0;j<A.length&&j<6;j++)\n"
"      h+='<b>tau '+A[j].tau.toFixed(A[j].tau<1?2:0)+' s</b><span>'+sci(A[j].sig)\n"
"        +'</span><i>'+A[j].n+' paru</i>';\n"
"    $('tAdev').innerHTML=h;\n"
"    var last=A[A.length-1];\n"
"    $('aWarn').textContent=(last.n<10)\n"
"      ? ('nejdelsi tau ma jen '+last.n+' paru - jen orientacni, nech to bezet dele') : '';\n"
"  }\n"
"\n"
"  var F=fit(y,tau0);\n"
"  if(F){\n"
"    var perDay=F.a*86400, ok=Math.abs(F.r)>=0.5;\n"
"    setv('dHead',(perDay>=0?'+':'')+sci(perDay));\n"
"    $('stDrift').textContent='r = '+F.r.toFixed(3);\n"
"    $('dWarn').textContent=ok?'':'|r| < 0,5 -> proklad je NEPRUKAZNY (zatim to vypada jako sum, ne drift)';\n"
"    var fa=tail('f'), fc=clean(fa);\n"
"    if(ok&&fc.length>1&&!DL){\n"
"      var fmn=0; for(i=0;i<fc.length;i++) fmn+=fc[i]; fmn/=fc.length;\n"
"      var dv=[]; for(i=0;i<fa.length;i++) dv.push((fa[i]===null||fa[i]===undefined)?null:fa[i]-fmn);\n"
"      var sp=span([dv]);\n"
"      if(sp){\n"
"        var L=fa.length, q='';\n"
"        for(i=0;i<L;i++){\n"
"          var v=F.a*(i*tau0)*mean, yy2=100-(v-sp[0])*100/(sp[1]-sp[0]);\n"
"          if(yy2<-50||yy2>150){ q=''; break; }\n"
"          if(yy2<0)yy2=0; if(yy2>100)yy2=100;\n"
"          q+=(L<2?0:i*100/(L-1)).toFixed(2)+','+yy2.toFixed(2)+' ';\n"
"        }\n"
"        $('lFit').setAttribute('points',q);\n"
"      }\n"
"    } else $('lFit').setAttribute('points','');\n"
"\n"
"    var off=(nom&&nom>0)?(mean-nom)/nom:null;\n"
"    var hh='<b>PRUMER</b><span>'+fmtFreq(mean)+' Hz</span><i>'+n+' mereni</i>';\n"
"    hh+='<b>OFFSET</b><span>'+(off===null?'--':((off>=0?'+':'')+sci(off)))+'</span><i>'\n"
"      +(off===null?'zadej nominal':((off*1e9).toFixed(1)+' ppb'))+'</i>';\n"
"    hh+='<b>DRIFT</b><span>'+(perDay>=0?'+':'')+sci(perDay)+'</span><i>za den</i>';\n"
"    hh+='<b>ROZSAH</b><span>'+(Math.max.apply(null,M.f)-Math.min.apply(null,M.f)).toFixed(5)\n"
"      +' Hz</span><i>pp</i>';\n"
"    hh+='<b>OKNO</b><span>'+upt(Math.round(M.t[n-1]-M.t[0]))+'</span><i>tau0 '+tau0.toFixed(2)+' s</i>';\n"
"    $('tDrift').innerHTML=hh;\n"
"  }\n"
"}\n"
"\n"
"function segset(id,attr,val){\n"
"  var bs=$(id).getElementsByTagName('button'),i,base;\n"
"  for(i=0;i<bs.length;i++){\n"
"    base=bs[i].getAttribute('data-base');\n"
"    if(base===null){ base=bs[i].className; bs[i].setAttribute('data-base',base); }\n"
"    bs[i].className=(bs[i].getAttribute(attr)===String(val))?(base?base+' on':'on'):base;\n"
"  }\n"
"}\n"
"function poll(){\n"
"  fetch('/api/state').then(function(r){ if(!r.ok) throw new Error('HTTP '+r.status); return r.json(); })\n"
"  .then(render).catch(function(e){\n"
"    pill('dLink','tLink','NEODPOVIDA','bad'); $('lastUpd').textContent='spojeni selhalo';\n"
"  });\n"
"}\n"
"setInterval(function(){\n"
"  if(!lastOk) return;\n"
"  var a=Math.round((Date.now()-lastOk)/1000);\n"
"  $('lastUpd').textContent=a<2?'aktualni':('pred '+a+' s');\n"
"},1000);\n"
"\n"
"function wire(id,attr,fn){\n"
"  $(id).addEventListener('click',function(e){\n"
"    var t=e.target; if(t.tagName!=='BUTTON') return;\n"
"    var v=t.getAttribute(attr); if(v!==null) fn(v);\n"
"  });\n"
"}\n"
"/* v12 (#5): sky plot z /api/sats (stred=zenit, azimut 0=sever nahoru). */\n"
"function drawSky(d){\n"
"  var g=$('skg'); while(g.firstChild) g.removeChild(g.firstChild);\n"
"  if(!d||!d.s) return; var CC='GREC',i;\n"
"  for(i=0;i<d.s.length;i++){ var s=d.s[i], prn=s[0], el=s[1], az=s[2], cn=s[3], co=s[4];\n"
"    var r=92*(90-el)/90; if(r<0)r=0; var a=az*Math.PI/180, x=r*Math.sin(a), y=-r*Math.cos(a);\n"
"    var col=cn>=40?'var(--ok)':(cn>=25?'var(--warn)':'var(--bad)');\n"
"    var c=document.createElementNS(SVGNS,'circle');\n"
"    c.setAttribute('cx',x.toFixed(1)); c.setAttribute('cy',y.toFixed(1));\n"
"    c.setAttribute('r',cn>0?5:3); c.setAttribute('fill',cn>0?col:'var(--dim)');\n"
"    c.setAttribute('stroke','var(--pnl)'); c.setAttribute('stroke-width','0.6'); g.appendChild(c);\n"
"    var t=document.createElementNS(SVGNS,'text'); t.setAttribute('x',x.toFixed(1));\n"
"    t.setAttribute('y',(y-7).toFixed(1)); t.setAttribute('class','skp');\n"
"    t.textContent=(CC.charAt(co)||'G')+prn; g.appendChild(t); }\n"
"}\n"
"function pollSats(){ fetch('/api/sats').then(function(r){return r.json();})\n"
"  .then(function(d){ SAT=d; drawSky(d); if(zoom==='gps') drawZoom(); }).catch(function(){}); }\n"
"\n"
"/* v12/v13 (#6): dlouha historie (24h/7d/30d) z W25Q datalogu pres /api/log.\n"
" * !! Jedna odpoved se vejde do ~48 bodu (strop `bodybuf` na CM4), takze pro delsi\n"
" * radu se posklada z VICE DAVEK pomoci `from` (posun v zaznamech od nejnovejsiho).\n"
" * Davky jdou po sobe, ne soubezne — datalog kanal je jen jeden (jinak 503). */\n"
"var DLCHUNKS=4, DLN=48;\n"
"function fetchLog(w){\n"
"  var acc={t:[],f:[],o:[],b:[],vc:[],vbat:[],lo:[],hi:[]}, meta=null;\n"
"  $('dlNote').textContent='nacitam historii z datalogu (W25Q)...';\n"
"  function chunk(k){\n"
"    if(dlWin!==w) return;                        /* uzivatel mezitim prepnul okno */\n"
"    var step=Math.max(1,Math.floor(w/(DLCHUNKS*DLN*10)));\n"
"    fetch('/api/log?win='+w+'&n='+DLN+'&from='+(k*DLN*step))\n"
"    .then(function(r){ if(!r.ok) throw new Error('HTTP '+r.status); return r.json(); })\n"
"    .then(function(d){\n"
"      if(dlWin!==w) return;\n"
"      if(!meta) meta=d;\n"
"      var P=d.p||[], i;\n"
"      for(i=0;i<P.length;i++){ var p=P[i];\n"
"        acc.t.push(p[0]); acc.f.push(p[1]); acc.o.push(p[2]); acc.b.push(p[3]);\n"
"        acc.vc.push(p[4]); acc.vbat.push(p[5]);\n"
"        acc.lo.push(p.length>7?p[7]:null); acc.hi.push(p.length>8?p[8]:null); }\n"
"      $('dlNote').textContent='nacitam historii... '+acc.t.length+' bodu';\n"
"      if(P.length>=DLN && k+1<DLCHUNKS) chunk(k+1); else done();\n"
"    }).catch(function(e){\n"
"      if(acc.t.length) done();                   /* co doslo, to zobraz */\n"
"      else { $('dlNote').textContent='historie se nenacetla: '+e\n"
"        +' - bezi datalog? (CM7 odpovida pres IPC)'; DL=null; }\n"
"    });\n"
"  }\n"
"  function done(){ finishLog(w,acc,meta); }\n"
"  chunk(0);\n"
"}\n"
"function finishLog(w,acc,d){\n"
"  (function(){\n"
"    var D={t:[],f:[],o:[],b:[],vc:[],vbat:[],lo:[],hi:[]}, P=acc.t.length, i;\n"
"    /* Davky chodi od NEJNOVEJSIHO; graf chce nejstarsi vlevo -> otoc. */\n"
"    for(i=P-1;i>=0;i--){\n"
"      D.t.push(acc.t[i]); D.f.push(acc.f[i]); D.o.push(acc.o[i]); D.b.push(acc.b[i]);\n"
"      D.vc.push(acc.vc[i]); D.vbat.push(acc.vbat[i]);\n"
"      D.lo.push(acc.lo[i]); D.hi.push(acc.hi[i]); }\n"
"    if(!P){ DL=null;\n"
"      $('dlNote').textContent='Datalog je zatim prazdny - zapni ho (okno Datalog / UART datalog on) '\n"
"        +'a nech pristroj bezet; zaznam se uklada kazdych 10 s.';\n"
"      drawAll(); return; }\n"
"    DL=D;\n"
"    /* !! Hlasi se SKUTECNE pokryty rozsah z casovych znacek, ne pozadovane okno:\n"
"     * kdyz log jeste nema tolik historie, CM7 zmensi krok a vrati vse, co ma -\n"
"     * napsat sem '30 dni' by bylo lzive. t=0 => RTC tehdy nebyl srovnany z GPS. */\n"
"    var span0=D.t[0], span1=D.t[D.t.length-1], msg;\n"
"    if(span0>0&&span1>span0) msg='historie '+dur(span1-span0)+' ('+P+' bodu, krok '\n"
"      +dur((span1-span0)/Math.max(1,P-1))+')';\n"
"    else msg='historie: '+P+' bodu (bez casovych znacek - RTC nebyl srovnany z GPS)';\n"
"    if(span0>0&&span1-span0<w*0.9) msg+=' - v logu zatim neni celych '+dur(w);\n"
"    /* Obalka: pasmo min-max v bucketu. Kdyz se cetl jen PODVZOREK, MUSI to byt\n"
"     * videt — obalka z casti zaznamu neni skutecne minimum a maximum. */\n"
"    if(d&&d.scanned) msg+=' | obalka z '+d.scanned+' zaznamu'\n"
"      +(d.full_env?' (uplna)':' (PODVZOREK - skutecne extremy mohou byt vetsi)');\n"
"    $('dlNote').textContent=msg+'. Z '+((d&&d.total)||0)+' zaznamu. Kmitocet null = tehdy nebyl '\n"
"      +'FPGA link. (Napajeni 12/5/VREF a MCU/FPGA teploty datalog neuklada.)';\n"
"    drawAll(); if(zoom) drawZoom();\n"
"  })();\n"
"}\n"
"\n"
"/* v12 (#6): export zobrazenych dat do CSV (bez backslashe kvuli SPA literalu). */\n"
"function exportCsv(){\n"
"  var NL=String.fromCharCode(10), rows=[], i;\n"
"  function q(v){ return (v===null||v===undefined)?'':v; }\n"
"  if(DL){ rows.push('t_unix;freq_Hz;ocxo_C;deska_C;vc_mV;vbat_mV');\n"
"    for(i=0;i<DL.t.length;i++) rows.push(DL.t[i]+';'+q(DL.f[i])+';'+q(DL.o[i])+';'+q(DL.b[i])+';'+q(DL.vc[i])+';'+q(DL.vbat[i])); }\n"
"  else { rows.push('sample;t_unix;freq_Hz'); for(i=0;i<M.f.length;i++) rows.push(i+';'+Math.round(M.t[i])+';'+q(M.f[i])); }\n"
"  var blob=new Blob([rows.join(NL)],{type:'text/csv'});\n"
"  var a=document.createElement('a'); a.href=URL.createObjectURL(blob);\n"
"  a.download=DL?'gpsdo_historie.csv':'gpsdo_mereni.csv'; document.body.appendChild(a); a.click();\n"
"  document.body.removeChild(a); setTimeout(function(){ URL.revokeObjectURL(a.href); },1000);\n"
"}\n"
"\n"
"/* v12 (#3): SSE push s automatickym fallbackem na 1 Hz poll. */\n"
"function startStream(){\n"
"  var es=null, fell=0;\n"
"  function fb(){ if(fell) return; fell=1; streaming=0; if(es) es.close(); if(!pollTimer) pollTimer=setInterval(poll,1000); }\n"
"  if(!window.EventSource){ fb(); return; }\n"
"  try{ es=new EventSource('/api/stream'); }catch(e){ fb(); return; }\n"
"  es.onopen=function(){ streaming=1; if(pollTimer){ clearInterval(pollTimer); pollTimer=0; } };\n"
"  es.onmessage=function(ev){ try{ render(JSON.parse(ev.data)); }catch(e){} };\n"
"  es.onerror=function(){ if(es.readyState===2) fb(); };\n"
"  setTimeout(function(){ if(!streaming) fb(); },4000);\n"
"}\n"
"\n"
"/* v12 (#1): ovladani MATH / LIMITY / NULL / CAS pres SCPI (stejna cesta jako konzole). */\n"
"function fnum(id){ var v=parseFloat($(id).value); return isFinite(v)?v:null; }\n"
"$('bMath').addEventListener('click',function(){ var m=fnum('mM'), b=fnum('mB');\n"
"  if(m!==null) scon('CALC:MATH:M '+m); if(b!==null) scon('CALC:MATH:B '+b);\n"
"  cmd('CALC:MATH:STAT '+(mathEn?'OFF':'ON')); });\n"
"$('bNull').addEventListener('click',function(){ cmd('CALC:NULL:ACQ'); });\n"
"$('bNullOff').addEventListener('click',function(){ cmd('CALC:NULL:STAT OFF'); });\n"
"$('bLim').addEventListener('click',function(){ var lo=fnum('mLo'), hi=fnum('mHi');\n"
"  if(lo!==null) scon('CALC:LIM:LOW '+lo); if(hi!==null) scon('CALC:LIM:UPP '+hi);\n"
"  cmd('CALC:LIM:STAT '+(limEn?'OFF':'ON')); });\n"
"$('bRtc').addEventListener('click',function(){ var d=$('rD').value.trim(), t=$('rT').value.trim();\n"
"  if(d) scon('SYST:DATE '+d); if(t) scon('SYST:TIME '+t);\n"
"  say('rmsg','cas odeslan (uplatni se jen bez GPS fixu)','ok'); });\n"
"$('bCsv').addEventListener('click',exportCsv);\n"
"\n"
"wire('segRun','data-r',function(v){ cmd(v==='1'?'INIT':'ABOR'); });\n"
"wire('segGate','data-g',function(v){ cmd('SENS:FREQ:GATE '+GATE[+v]); });\n"
"wire('segChan','data-c',function(v){ cmd('SENS:FREQ:CHAN '+v); });\n"
"wire('segWin','data-w',function(v){ var w=+v; segset('segWin','data-w',w);\n"
"  if(w>3600){ dlWin=w; fetchLog(w); }\n"
"  else { win=w; DL=null; dlWin=0; $('dlNote').textContent=''; drawAll(); if(zoom) drawZoom(); } });\n"
"$('bLogin').addEventListener('click',login);\n"
"$('bSend').addEventListener('click',function(){ var l=$('c').value.trim(); if(l) scon(l); });\n"
"$('bClr').addEventListener('click',function(){ $('log').innerHTML=''; });\n"
"$('c').addEventListener('keydown',function(e){ if(e.key==='Enter'){ var l=$('c').value.trim(); if(l) scon(l); } });\n"
"$('p').addEventListener('keydown',function(e){ if(e.key==='Enter') login(); });\n"
"$('nom').addEventListener('change',function(){\n"
"  var v=parseFloat($('nom').value); nom=(isFinite(v)&&v>0)?v:null; nomAuto=0; drawStab();\n"
"});\n"
"$('bNom').addEventListener('click',function(){\n"
"  if(!M.f.length) return;\n"
"  var m=0,i; for(i=0;i<M.f.length;i++) m+=M.f[i];\n"
"  nom=m/M.f.length; nomAuto=0; $('nom').value=nom.toFixed(5); drawStab();\n"
"});\n"
"\n"
"/* klik na kterykoli graf -> detailni okno */\n"
"var cws=document.querySelectorAll('[data-z]');\n"
"for(var q=0;q<cws.length;q++)\n"
"  cws[q].addEventListener('click',function(){ openZoom(this.getAttribute('data-z')); });\n"
"$('dX').addEventListener('click',closeZoom);\n"
"$('ovl').addEventListener('click',function(e){ if(e.target===$('ovl')) closeZoom(); });\n"
"document.addEventListener('keydown',function(e){ if(e.key==='Escape') closeZoom(); });\n"
"\n"
"var THN={amber:'JANTAR',blue:'MODRA',light:'SVETLA'}, THO=['amber','blue','light'];\n"
"function setTheme(t){\n"
"  document.documentElement.setAttribute('data-t',t);\n"
"  localStorage.setItem('gt',t); $('bTheme').textContent='VZHLED: '+THN[t];\n"
"}\n"
"$('bTheme').addEventListener('click',function(){\n"
"  var cur=document.documentElement.getAttribute('data-t')||'amber';\n"
"  setTheme(THO[(THO.indexOf(cur)+1)%THO.length]);\n"
"});\n"
"setTheme(localStorage.getItem('gt')||'amber');\n"
"$('ip').textContent=location.host;\n"
"$('u').value=localStorage.getItem('gu')||'admin';\n"
"segset('segWin','data-w',win);\n"
"loadM();                                   /* historie mereni pres F5 (kdyz je mezera mala) */\n"
"setInterval(saveM,15000);                  /* periodicky, at prezije i pad zalozky */\n"
"window.addEventListener('beforeunload',saveM);\n"
"poll(); startStream(); pollSats(); setInterval(pollSats,3000);\n"
"</script></body></html>\n";

/* ── Odesilaci fronta (hlavicka + telo po kouscich pres tcp_sent). ───────────── */

static void pump_send(http_conn_t *c)
{
    if (c->pcb == NULL) return;

    while (c->hdr_sent < c->hdr_len) {
        u16_t avail = tcp_sndbuf(c->pcb);
        if (avail == 0) return;
        size_t remain = c->hdr_len - c->hdr_sent;
        u16_t chunk = (remain < avail) ? (u16_t)remain : avail;
        if (tcp_write(c->pcb, c->hdr + c->hdr_sent, chunk, TCP_WRITE_FLAG_COPY) != ERR_OK) { tcp_output(c->pcb); return; }
        c->hdr_sent += chunk;
        tcp_output(c->pcb);
    }
    while (c->body_ptr != NULL && c->body_sent < c->body_len) {
        u16_t avail = tcp_sndbuf(c->pcb);
        if (avail == 0) return;
        size_t remain = c->body_len - c->body_sent;
        u16_t chunk = (remain < avail) ? (u16_t)remain : avail;
        if (tcp_write(c->pcb, c->body_ptr + c->body_sent, chunk, TCP_WRITE_FLAG_COPY) != ERR_OK) { tcp_output(c->pcb); return; }
        c->body_sent += chunk;
        tcp_output(c->pcb);
    }
    /* Vse zafronteovano (nemusi byt jeste ACKnuto) -> Connection: close muze
     * dobehnout, lwIP zbytek doruci pred FIN. */
    tcp_recv(c->pcb, NULL);
    tcp_close(c->pcb);
    c->pcb = NULL;
}

static void queue_response(http_conn_t *c, int code, const char *code_str,
                            const char *content_type, const char *body, size_t body_len)
{
    int hn = snprintf(c->hdr, sizeof c->hdr,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
        code, code_str, content_type, (unsigned)body_len);
    c->hdr_len = (hn > 0) ? (size_t)hn : 0u;
    c->hdr_sent = 0;
    c->body_ptr = body; c->body_len = body_len; c->body_sent = 0;
    pump_send(c);
}
static void queue_text(http_conn_t *c, int code, const char *code_str, const char *text)
{
    queue_response(c, code, code_str, "text/plain", text, strlen(text));
}

/* ── Cache SPA: stranka ma ~62 kB a bez toho se tahla pri KAZDEM reloadu. ──────
 * ETag = cas prekladu TOHOTO souboru: zmeni se prave tehdy, kdyz se zmenila SPA
 * (jiny soubor se prelozi bez dopadu sem), takze po flashi prohlizec spolehlive
 * natahne novou verzi a jinak dostane 304. Verze firmwaru by nestacila — behem
 * vyvoje se SPA meni bez bumpu verze a prohlizec by drzel starou stranku. */
static const char SPA_ETAG[] = "\"" __DATE__ __TIME__ "\"";

static void queue_spa(http_conn_t *c, const http_req_t *r)
{
    if (r->inm[0] != '\0' && strcmp(r->inm, SPA_ETAG) == 0) {
        int hn = snprintf(c->hdr, sizeof c->hdr,
            "HTTP/1.1 304 Not Modified\r\nETag: %s\r\n"
            "Cache-Control: no-cache\r\nConnection: close\r\n\r\n", SPA_ETAG);
        c->hdr_len = (hn > 0) ? (size_t)hn : 0u; c->hdr_sent = 0;
        c->body_ptr = NULL; c->body_len = 0; c->body_sent = 0;
        pump_send(c);
        return;
    }
    /* `no-cache` (ne `no-store`): prohlizec smi mit kopii, ale MUSI se zeptat —
     * odpoved je pak levny 304 misto 62 kB. */
    int hn = snprintf(c->hdr, sizeof c->hdr,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %u\r\n"
        "ETag: %s\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
        (unsigned)(sizeof(SPA_HTML) - 1u), SPA_ETAG);
    c->hdr_len = (hn > 0) ? (size_t)hn : 0u; c->hdr_sent = 0;
    c->body_ptr = SPA_HTML; c->body_len = sizeof(SPA_HTML) - 1u; c->body_sent = 0;
    pump_send(c);
}

/* ── v12 (#3): SSE (Server-Sent Events) — drzene spojeni, push pri novem mereni ── */
static uint32_t s_log_gen;   /* generace pozadavku na datalog (handshake s CM7) */

/* Dosle nedoposlanou SSE hlavicku (mala, obvykle projde napoprve). */
static void sse_finish_hdr(http_conn_t *c)
{
    if (c->pcb == NULL) return;
    while (c->hdr_sent < c->hdr_len) {
        u16_t avail = tcp_sndbuf(c->pcb);
        if (avail == 0) return;
        size_t remain = c->hdr_len - c->hdr_sent;
        u16_t chunk = (remain < avail) ? (u16_t)remain : avail;
        if (tcp_write(c->pcb, c->hdr + c->hdr_sent, chunk, TCP_WRITE_FLAG_COPY) != ERR_OK) return;
        c->hdr_sent += chunk;
    }
    tcp_output(c->pcb);
}

/* Zacne SSE stream: hlavicka bez Content-Length, spojeni zustane otevrene. */
static void sse_start(http_conn_t *c)
{
    int hn = snprintf(c->hdr, sizeof c->hdr,
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n");
    c->hdr_len = (hn > 0) ? (size_t)hn : 0u; c->hdr_sent = 0;
    c->body_ptr = NULL; c->body_len = 0; c->body_sent = 0;
    c->mode = HCONN_SSE; c->sse_seq = 0xFFFFFFFFu; c->defer_ms = HAL_GetTick();
    sse_finish_hdr(c);
}

/* Odesle jednu SSE udalost `data: <json>\n\n`. Best-effort: kdyz neni misto ve
 * sndbuf, tuhle udalost preskoci (dalsi za chvili). NEzavira spojeni. */
static void sse_push(http_conn_t *c, const char *json, size_t len)
{
    if (c->pcb == NULL || c->hdr_sent < c->hdr_len) return;
    if (tcp_sndbuf(c->pcb) < (u16_t)(len + 8u)) return;
    tcp_write(c->pcb, "data: ", 6, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    tcp_write(c->pcb, json, (u16_t)len, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    tcp_write(c->pcb, "\n\n", 2, TCP_WRITE_FLAG_COPY);
    tcp_output(c->pcb);
}

/* Periodicka obsluha odlozenych/drzenych spojeni. Throttle ~20 Hz (staci pro
 * latenci mereni ~4/s i timeout datalogu 2 s), aby smycka CM4 nebyla zbytecne
 * zatezovana ctenim snapshotu tisickrat za sekundu. */
void httpd_min_poll(void)
{
    static uint32_t s_next_ms;
    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - s_next_ms) < 0) return;
    s_next_ms = now + 50u;

    for (unsigned i = 0; i < HTTPD_MAX_CONN; i++) {
        http_conn_t *c = &s_hconn[i];
        if (c->pcb == NULL) continue;

        if (c->mode == HCONN_LOG) {
            if (g_ipc.log.resp_gen == c->defer_gen) {            /* CM7 naplnil data */
                size_t n = build_log_json(c->bodybuf, sizeof c->bodybuf);
                c->mode = HCONN_NORMAL;
                queue_response(c, 200, "OK", "application/json", c->bodybuf, n);
            } else if ((int32_t)(now - c->defer_ms) >= 0) {      /* timeout */
                c->mode = HCONN_NORMAL;
                queue_text(c, 504, "Gateway Timeout", "datalog (CM7) neodpovedel\n");
            }
        } else if (c->mode == HCONN_SSE) {
            sse_finish_hdr(c);
            ipc_snapshot_t snap;
            if (!(ipc_cm4_ready() && ipc_cm4_cm7_alive(now) && ipc_cm4_read(&snap))) continue;
            if (snap.seq_meas != c->sse_seq || (int32_t)(now - c->defer_ms) >= 1000) {
                size_t n = build_state_json(c->bodybuf, sizeof c->bodybuf, &snap);
                if (n) { sse_push(c, c->bodybuf, n); c->sse_seq = snap.seq_meas; c->defer_ms = now; }
            }
        }
    }
}

/* Zpracuje kompletni pozadavek (hlavicky + pripadne cele telo uz v bufferu). */
static void dispatch(http_conn_t *c, const http_req_t *r)
{
    if (strcmp(r->method, "GET") == 0 && strcmp(r->path, "/") == 0) {
        queue_spa(c, r);   /* s ETag/304 — SPA ma ~62 kB, netahat ji pri kazdem reloadu */
        return;
    }
    if (strcmp(r->method, "GET") == 0 && strcmp(r->path, "/api/state") == 0) {
        ipc_snapshot_t snap;
        int have = ipc_cm4_ready() && ipc_cm4_cm7_alive(HAL_GetTick()) && ipc_cm4_read(&snap);
        size_t n = have ? build_state_json(c->bodybuf, sizeof c->bodybuf, &snap) : 0u;
        if (n == 0) { queue_text(c, 503, "Service Unavailable", "CM7 unreachable\n"); return; }
        queue_response(c, 200, "OK", "application/json", c->bodybuf, n);
        return;
    }
    if (strcmp(r->method, "POST") == 0 && strcmp(r->path, "/api/scpi") == 0) {
        if (r->content_length < 0 || (size_t)r->content_length > HTTPD_BODY_MAX) {
            queue_text(c, 411, "Length Required", "missing/oversized Content-Length\n");
            return;
        }
        size_t avail = c->rxlen - r->header_len;
        if (avail < (size_t)r->content_length) return;   /* telo jeste nedorazilo cele (recv dobehne pozdeji) */

        char line[HTTPD_BODY_MAX + 1u];
        size_t n = (size_t)r->content_length;
        memcpy(line, c->rxbuf + r->header_len, n);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) n--;   /* orizni EOL */
        line[n] = '\0';

        ipc_snapshot_t snap;
        int have = ipc_cm4_ready() && ipc_cm4_cm7_alive(HAL_GetTick()) && ipc_cm4_read(&snap);
        scpi_src_t src;
        if (have) ipc_scpi_src_from_snap(&src, &snap); else memset(&src, 0, sizeof src);
        src.read_log = NULL;
        /* W0+W5: zapis pusti jen kdyz je ovladani povolene A soucasne sedi
         * jmeno+heslo (viz komentar u check_auth — TCP 5025 ma jen prvni podminku). */
        src.set_cfg = (have && snap.web_ctrl_en && check_auth(r, &snap)) ? ipc_scpi_set_cfg : NULL;
        /* ⚠️ Kdyz snapshot MAME, ale zapis je presto zakazany, je to OCHRANA, ne
         * chybejici data -> parser vrati `-203 Command protected` misto -230.
         * Bez toho hlaseni posilalo uzivatele hledat HW poruchu (STATUS #130). */
        src.ctrl_locked = (have && src.set_cfg == NULL) ? 1u : 0u;

        static scpi_ctx_t ctx;              /* jednorazova zprava/spojeni -> staci sdilena */
        scpi_ctx_init(&ctx);
        size_t rn = scpi_process_ctx(&ctx, &src, line, c->bodybuf, sizeof c->bodybuf - 1u);
        if (rn > 0 && rn < sizeof(c->bodybuf) - 1u) { c->bodybuf[rn++] = '\n'; }
        queue_response(c, 200, "OK", "text/plain", c->bodybuf, rn);
        return;
    }
    /* v12 (#5): GPS druzice pro sky plot. */
    if (strcmp(r->method, "GET") == 0 && path_is(r->path, "/api/sats")) {
        ipc_snapshot_t snap;
        int have = ipc_cm4_ready() && ipc_cm4_cm7_alive(HAL_GetTick()) && ipc_cm4_read(&snap);
        if (!have) { queue_text(c, 503, "Service Unavailable", "CM7 unreachable\n"); return; }
        size_t n = build_sats_json(c->bodybuf, sizeof c->bodybuf, &snap);
        queue_response(c, 200, "OK", "application/json", c->bodybuf, n);
        return;
    }
    /* v12 (#3): SSE stream — drzene spojeni, push pri novem mereni. */
    if (strcmp(r->method, "GET") == 0 && path_is(r->path, "/api/stream")) {
        sse_start(c);
        return;
    }
    /* v12 (#6): dlouha historie z W25Q datalogu (24h/7d/30d). Odpoved je ODLOZENA —
     * data prijdou od CM7 pres IPC kanal; dokonci ji httpd_min_poll. */
    if (strcmp(r->method, "GET") == 0 && path_is(r->path, "/api/log")) {
        for (unsigned i = 0; i < HTTPD_MAX_CONN; i++)   /* jen jeden transfer soubezne (sdileny kanal) */
            if (s_hconn[i].mode == HCONN_LOG && &s_hconn[i] != c) {
                queue_text(c, 503, "Service Unavailable", "datalog zaneprazdnen\n"); return;
            }
        long win = qparam(r->path, "win", 3600);
        long np  = qparam(r->path, "n", 48);
        if (np < 1) np = 1;
        if (np > 48) np = 48;                            /* strop kvuli velikosti JSON (bodybuf) */
        if (win < 60) win = 60;
        long step = win / (np * 10);                     /* datalog perioda = 10 s */
        if (step < 1) step = 1;
        /* `from` = odkud (v zaznamech od nejnovejsiho) — klient si tak vyzada dalsi
         * davku a slozi delsi radu, nez se vejde do jednoho `bodybuf`. */
        long from = qparam(r->path, "from", 0);
        if (from < 0) from = 0;
        g_ipc.log.req_from  = (uint32_t)from;
        g_ipc.log.req_count = (uint16_t)np;
        g_ipc.log.req_step  = (uint16_t)step;
        g_ipc.log.req_env   = (qparam(r->path, "env", 1) != 0) ? 1u : 0u;
        IPC_DMB();
        g_ipc.log.req_gen   = ++s_log_gen;               /* az PO parametrech -> CM7 vidi konzistentne */
        /* ⚠️ Timeout 8 s (drive 2 s): s obalkou cte CM7 az IPC_LOG_SCAN_MAX zaznamu
         * po davkach IPC_LOG_SCAN_BUDGET na tik (100 Hz), coz je radove sekundy. */
        c->mode = HCONN_LOG; c->defer_gen = s_log_gen; c->defer_ms = HAL_GetTick() + 8000u;
        return;                                          /* odpoved dokonci httpd_min_poll */
    }
    queue_text(c, 404, "Not Found", "not found\n");
}

/* ── Raw TCP callbacky ─────────────────────────────────────────────────────── */

static http_conn_t *conn_alloc(void)
{
    for (unsigned i = 0; i < HTTPD_MAX_CONN; i++)
        if (s_hconn[i].pcb == NULL) return &s_hconn[i];
    return NULL;
}
static void conn_free(http_conn_t *c) { c->pcb = NULL; c->rxlen = 0; c->dispatched = 0; c->body_ptr = NULL; c->mode = HCONN_NORMAL; }

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    http_conn_t *c = (http_conn_t *)arg;

    if (p == NULL) { tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_close(pcb); conn_free(c); return ERR_OK; }
    if (err != ERR_OK) { pbuf_free(p); return err; }

    tcp_recved(pcb, p->tot_len);
    if (c->dispatched) { pbuf_free(p); return ERR_OK; }   /* uz jsme odpovedeli */

    size_t room = HTTPD_RXBUF_MAX - 1u - c->rxlen;
    size_t take = (p->tot_len < room) ? p->tot_len : room;
    if (take > 0) { pbuf_copy_partial(p, c->rxbuf + c->rxlen, (u16_t)take, 0); c->rxlen += (uint16_t)take; }
    c->rxbuf[c->rxlen] = '\0';
    pbuf_free(p);

    if (take < p->tot_len) {   /* pretekl RX buffer — dal se nevejde, odpovez a zavri */
        c->dispatched = 1;
        queue_text(c, 431, "Request Header Fields Too Large", "request too large\n");
        return ERR_OK;
    }

    http_req_t r;
    int pr = httpd_parse_request(c->rxbuf, c->rxlen, &r);
    if (pr < 0) { c->dispatched = 1; queue_text(c, 400, "Bad Request", "malformed request\n"); return ERR_OK; }
    if (pr == 0) return ERR_OK;                            /* hlavicky jeste neuplne */

    if (r.content_length > 0) {
        size_t avail = (c->rxlen >= r.header_len) ? (c->rxlen - r.header_len) : 0u;
        if (avail < (size_t)r.content_length) return ERR_OK;   /* cekej na zbytek tela */
    }
    c->dispatched = 1;
    dispatch(c, &r);
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)pcb; (void)len;
    http_conn_t *c = (http_conn_t *)arg;
    if (c == NULL) return ERR_OK;
    if (c->mode == HCONN_SSE) { sse_finish_hdr(c); return ERR_OK; }   /* SSE se NEzavira */
    pump_send(c);
    return ERR_OK;
}

static void on_err(void *arg, err_t err) { (void)err; if (arg != NULL) conn_free((http_conn_t *)arg); }

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || newpcb == NULL) return ERR_VAL;

    http_conn_t *c = conn_alloc();
    if (c == NULL) { tcp_abort(newpcb); return ERR_ABRT; }   /* strop spojeni dosazen */
    c->pcb = newpcb; c->rxlen = 0; c->dispatched = 0; c->body_ptr = NULL; c->mode = HCONN_NORMAL;

    tcp_arg(newpcb, c);
    tcp_recv(newpcb, on_recv);
    tcp_sent(newpcb, on_sent);
    tcp_err(newpcb, on_err);
    return ERR_OK;
}

void httpd_min_init(void)
{
    for (unsigned i = 0; i < HTTPD_MAX_CONN; i++) { s_hconn[i].pcb = NULL; s_hconn[i].mode = HCONN_NORMAL; }

    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL) return;
    if (tcp_bind(pcb, IP_ADDR_ANY, HTTPD_PORT) != ERR_OK) { tcp_close(pcb); return; }

    struct tcp_pcb *lpcb = tcp_listen_with_backlog(pcb, HTTPD_MAX_CONN);
    if (lpcb == NULL) { tcp_close(pcb); return; }
    tcp_accept(lpcb, on_accept);
}
