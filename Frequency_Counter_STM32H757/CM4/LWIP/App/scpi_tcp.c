/**
  ******************************************************************************
  * @file    scpi_tcp.c
  * @brief   SCPI server na TCP portu 5025 (F6 / W3) — raw lwIP API, NO_SYS=1.
  *
  * Stejne SCPI jadro jako USB (`scpi.c`, `scpi_process_ctx`) — jen jiny transport.
  * Zadna vlastni prikazova sada, viz WEB_UI_PLAN.md §2 ("SCPI jako jedina ovladaci
  * plocha"). Kazde spojeni ma VLASTNI `scpi_ctx_t` (chybova fronta + status registry
  * per session, presne proto existuje).
  *
  * ⚠️ Zdroj dat je CTEN ZNOVU pro kazdy prikaz (`ipc_cm4_read` + `ipc_scpi_src_from_snap`),
  * ne jednou pri prijeti spojeni — snapshot se meni ~2 Hz na CM7 a klient muze poslat
  * prikazy s odstupem minut. `set_cfg` se prirazuje jen kdyz `snap.web_ctrl_en` je 1
  * (okno PRISTUP na CM7, W0); jinak zustane NULL a parser SET tise odmitne vlastni
  * existujici ochranou (zadna nova chybova cesta, viz `WEB_UI_PLAN.md` W3).
  *
  * ⚠️ `read_log` je zamerne NULL (MMEM:DATA? pres TCP jeste nejde — chce IPC okno
  * pro cteni datalogu, #26, odlozeno). Parser to hlasi cistou SCPI chybou, ne padem.
  ******************************************************************************
  */
#include "scpi_tcp.h"

#include "lwip/tcp.h"
#include <string.h>

#include "scpi.h"
#include "ipc_cm4.h"
#include "main.h"          /* HAL_GetTick */

#define SCPI_TCP_PORT       5025u
#define SCPI_TCP_MAX_CONN   4u      /* soubezna spojeni; MEMP_NUM_TCP_PCB=10 ma rezervu */
#define SCPI_TCP_RXLINE_MAX 96u     /* stejny rad jako UART RX_BUF_SIZE — jeden prikaz/radek */
#define SCPI_TCP_TXBUF_MAX  200u    /* SCPI odpovedi jsou kratke (stejny rad jako `scpi ipc` 160 B) */

typedef struct {
    struct tcp_pcb *pcb;    /* NULL = slot volny */
    scpi_ctx_t ctx;
    char     rxbuf[SCPI_TCP_RXLINE_MAX];
    uint16_t rxlen;
} scpi_conn_t;

/* Staticky pool — ZADNY malloc/mem_malloc navic mimo to, co uz lwIP dela pro pbuf/pcb.
 * CM4 nema heap rezervu na dynamickou alokaci per-spojeni. */
static scpi_conn_t s_conn[SCPI_TCP_MAX_CONN];

static scpi_conn_t *conn_alloc(void)
{
    for (unsigned i = 0; i < SCPI_TCP_MAX_CONN; i++)
        if (s_conn[i].pcb == NULL) return &s_conn[i];
    return NULL;
}

static void conn_free(scpi_conn_t *c)
{
    c->pcb = NULL;
    c->rxlen = 0;
}

/* Zpracuje jeden radek (uz bez LF/CR) a odesle odpoved. Cte snapshot ZNOVU pro
 * kazdy prikaz — viz komentar u souboru. */
static void process_line(scpi_conn_t *c)
{
    scpi_src_t src;
    ipc_snapshot_t snap;
    int have = ipc_cm4_ready() && ipc_cm4_cm7_alive(HAL_GetTick()) && ipc_cm4_read(&snap);

    if (have) {
        ipc_scpi_src_from_snap(&src, &snap);
    } else {
        memset(&src, 0, sizeof src);   /* vse neplatne -> SCPI NaN, ne pad ani stara data */
    }
    src.read_log = NULL;                                          /* #26, odlozeno */
    src.set_cfg  = (have && snap.web_ctrl_en) ? ipc_scpi_set_cfg : NULL;

    static char txbuf[SCPI_TCP_TXBUF_MAX];
    size_t n = scpi_process_ctx(&c->ctx, &src, c->rxbuf, txbuf, sizeof(txbuf) - 2u);
    if (n == 0) return;                          /* akce bez odpovedi (*RST apod.) */

    txbuf[n++] = '\r'; txbuf[n++] = '\n';         /* SCPI-99 socket transport konvence */

    if (c->pcb != NULL && tcp_sndbuf(c->pcb) >= n) {
        /* TCP_WRITE_FLAG_COPY: `txbuf` je jen jeden static buffer sdileny vsemi
         * spojenimi (bezpecne — NO_SYS=1, vse na jednom „vlakne"), lwIP si musi
         * obsah zkopirovat driv, nez ho pretezeme dalsim prikazem. */
        if (tcp_write(c->pcb, txbuf, (u16_t)n, TCP_WRITE_FLAG_COPY) == ERR_OK) {
            tcp_output(c->pcb);
        }
        /* ⚠️ Zadne oseteni ERR_MEM/kratkeho sndbuf: TCP_SND_BUF = 4*MSS (~5,8 kB,
         * viz lwipopts.h) je o rady vetsi nez SCPI_TCP_TXBUF_MAX, takze zamitnuti
         * zapisu by znamenalo rozbite spojeni, ne normalni provoz — nestoji za
         * frontu/retry mechanismus, ktery by W4 (velke HTTP odpovedi) uz potrebuje. */
    }
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    scpi_conn_t *c = (scpi_conn_t *)arg;

    if (p == NULL) {                              /* protejsek zavrel spojeni */
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_close(pcb);
        conn_free(c);
        return ERR_OK;
    }
    if (err != ERR_OK) { pbuf_free(p); return err; }

    tcp_recved(pcb, p->tot_len);

    for (struct pbuf *q = p; q != NULL; q = q->next) {
        const char *d = (const char *)q->payload;
        for (u16_t i = 0; i < q->len; i++) {
            char ch = d[i];
            if (ch == '\n' || ch == '\r') {
                if (c->rxlen > 0) {
                    c->rxbuf[c->rxlen] = '\0';
                    process_line(c);
                    c->rxlen = 0;
                }
            } else if (c->rxlen < SCPI_TCP_RXLINE_MAX - 1u) {
                c->rxbuf[c->rxlen++] = ch;
            } else {
                /* Radek delsi nez buffer — zahod a cekej na dalsi LF/CR, at se
                 * neprocesuje uriznuty (a tedy jinak znejici) prikaz. */
                c->rxlen = 0;
            }
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    (void)err;
    /* PCB uz je v tuhle chvili od lwIP neplatny — jen uvolni slot. */
    if (arg != NULL) conn_free((scpi_conn_t *)arg);
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || newpcb == NULL) return ERR_VAL;

    scpi_conn_t *c = conn_alloc();
    if (c == NULL) {
        /* Strop soubeznych spojeni dosazen — slusne odmitni, nenechavej viset. */
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    c->pcb = newpcb;
    c->rxlen = 0;
    scpi_ctx_init(&c->ctx);

    tcp_arg(newpcb, c);
    tcp_recv(newpcb, on_recv);
    tcp_err(newpcb, on_err);
    tcp_nagle_disable(newpcb);   /* kratke prikazy/odpovedi — latence pred propustnosti */
    return ERR_OK;
}

void scpi_tcp_init(void)
{
    for (unsigned i = 0; i < SCPI_TCP_MAX_CONN; i++) s_conn[i].pcb = NULL;

    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL) return;                       /* degradovane: bez PCB zadny server */
    if (tcp_bind(pcb, IP_ADDR_ANY, SCPI_TCP_PORT) != ERR_OK) { tcp_close(pcb); return; }

    struct tcp_pcb *lpcb = tcp_listen_with_backlog(pcb, SCPI_TCP_MAX_CONN);
    if (lpcb == NULL) { tcp_close(pcb); return; }
    tcp_accept(lpcb, on_accept);
}
