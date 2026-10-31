/* fpga_v3.c -- protokol SPI v3 (docs/PROTOKOL_V3.md). Viz fpga_v3.h. Cista logika bez HAL. 2026-10-22. */
#include "fpga_v3.h"
#include "fpga_crc.h"       /* fpga_crc16 (HW jednotka, je-li zapnuta, jinak tabulka) */
#include "fpga_tdccal.h"    /* fpga_tdccal_chk: TENTYZ kontrolni soucet polozky tabulky jako FW 0x0427 */
#include <string.h>
#include <math.h>

#define MASK40  ((1ull << 40) - 1u)
#define MASK48  ((1ull << 48) - 1u)
#define MASK56  ((1ull << 56) - 1u)

static void wrn(uint8_t *p, uint64_t v, int n) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint64_t rdn(const uint8_t *p, int n) { uint64_t v = 0; for (int i = 0; i < n; i++) v |= (uint64_t)p[i] << (8 * i); return v; }

double fpga_v3_unit_ps(uint32_t clk_mhz) { return 1e6 / (16384.0 * (double)(clk_mhz ? clk_mhz : 100u)); }

/* ══════════════ ramec ══════════════ */
uint16_t fpga_v3_blk_crc(const uint8_t *blk) { return fpga_crc16(blk, 254); }

void fpga_v3_blk_seal(uint8_t *blk)
{
    uint16_t c = fpga_v3_blk_crc(blk);
    blk[254] = (uint8_t)c; blk[255] = (uint8_t)(c >> 8);
}

/* v0.38.0 (FW 0x0503): automaticky rozsah predelicky (navrh v3/fpga_range.c) -- viz fpga_v3.h */
uint8_t fpga_v3_range_select(uint8_t cur, double f)
{
    if (!(f > 0.0)) return cur;                         /* bez odhadu nic nemenit */
    if (cur > FPGA_V3_RANGE_MAXLOG2) cur = FPGA_V3_RANGE_MAXLOG2;
    while (cur < FPGA_V3_RANGE_MAXLOG2 && f / (double)(1u << cur) > FPGA_V3_RANGE_UP_HZ) cur++;
    while (cur > 0u && f / (double)(1u << (cur - 1u)) < FPGA_V3_RANGE_DOWN_HZ) cur--;
    return cur;
}

/* audit 2026-10-23 (F-0273): volba ze sondy -- PROBE = 0 znamena f < 16 kHz (hrany /16 za 1 ms), ne "bez odhadu";
 * drive P zustalo (napr. /16 po 400 MHz) a pod 16 kHz melo okno segmentu jen par hran. Bez signalu tedy P = 1: po
 * navratu vysokeho kmitoctu je par segmentu neplatnych (kontrolni pocitani), nez automatika P zvedne. */
uint8_t fpga_v3_range_probe(uint8_t cur, uint16_t probe)
{
    return probe ? fpga_v3_range_select(cur, FPGA_V3_PROBE_HZ(probe)) : 0u;
}

/* v0.37.0 (FW 0x0502): jemny cas 1PPS (IDES8) nebo hruby citac -- viz fpga_v3.h */
int fpga_v3_pps_fine(const fpga_v3_hdr_t *h, int i, uint32_t *ticks, uint16_t *frac)
{
    i &= 1;
    if ((h->caps & FPGA_V3_CAP_PPS_IDES) && (h->pps_status & FPGA_V3_PPS_ST_LOCK) && (h->pps[i].pflags & FPGA_V3_PF_FINE)) {
        *ticks = (uint32_t)(h->pps[i].pts >> 14);
        *frac  = (uint16_t)(h->pps[i].pts & 0x3FFFu);
        return 1;
    }
    *ticks = h->pps_coarse[i];
    *frac  = 0u;
    return 0;
}

uint8_t fpga_v3_blk_ok(const uint8_t *f)
{
    uint8_t m = 0;
    for (uint32_t k = 0; k < FPGA_V3_NBLK; k++) {
        const uint8_t *b = f + k * FPGA_V3_BLK;
        if (fpga_v3_blk_crc(b) == (uint16_t)(b[254] | ((uint16_t)b[255] << 8))) m |= (uint8_t)(1u << k);
    }
    return m;
}

void fpga_v3_rec_pack(uint8_t *d, const fpga_v3_rec_t *r)
{
    wrn(d + 0, r->seg_idx, 2);
    d[2] = r->ch; d[3] = r->flags;
    wrn(d + 4,  r->n & 0xFFFFFFu, 3);
    wrn(d + 7,  r->x0 & MASK40, 5);
    wrn(d + 12, r->t0 & MASK48, 6);
    wrn(d + 18, r->sx & MASK48, 6);
    wrn(d + 24, r->st & MASK56, 7);
}

void fpga_v3_rec_unpack(fpga_v3_rec_t *r, const uint8_t *s)
{
    r->seg_idx = (uint16_t)rdn(s, 2);
    r->ch = s[2]; r->flags = s[3];
    r->n  = (uint32_t)rdn(s + 4, 3);
    r->x0 = rdn(s + 7, 5);
    r->t0 = rdn(s + 12, 6);
    r->sx = rdn(s + 18, 6);
    r->st = rdn(s + 24, 7);
}

void fpga_v3_build(uint8_t *f, uint8_t type, uint32_t seq, const uint8_t *p, uint32_t np)
{
    memset(f, 0, FPGA_V3_LEN);
    f[0] = FPGA_V3_MAGIC; f[1] = FPGA_V3_VER; f[2] = type;
    wrn(f + 4, seq, 4);
    wrn(f + 8, FPGA_V3_LEN, 2);
    f[10] = (uint8_t)FPGA_V3_NBLK;
    if (np > FPGA_V3_P_MAX) np = FPGA_V3_P_MAX;
    if (p != NULL && np) memcpy(f + 12, p, np);
    for (uint32_t k = 0; k < FPGA_V3_NBLK; k++) fpga_v3_blk_seal(f + k * FPGA_V3_BLK);
}

void fpga_v3_build_ack(uint8_t *f, uint32_t seq, uint32_t rec_ack)
{
    uint8_t p[4]; wrn(p, rec_ack, 4);
    fpga_v3_build(f, FPGA_V3_T_ACK, seq, p, 4);
}

void fpga_v3_build_cfg(uint8_t *f, uint32_t seq, uint8_t id, uint8_t v1, uint8_t v2)
{
    const uint8_t p[3] = { id, v1, v2 };
    fpga_v3_build(f, FPGA_V3_T_SETCFG, seq, p, 3);
}

uint32_t fpga_v3_build_lutw(uint8_t *f, uint32_t seq, const uint8_t *ch, const uint16_t *addr, const uint16_t *neg,
                            uint32_t n, uint16_t *sum)
{
    if (n > FPGA_V3_LUTW_MAX) n = FPGA_V3_LUTW_MAX;
    fpga_v3_build(f, FPGA_V3_T_LUTW, seq, NULL, 0);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *b = f + (1u + i / FPGA_V3_LUTW_PER_BLK) * FPGA_V3_BLK;
        uint8_t *e = b + 1u + 3u * (i % FPGA_V3_LUTW_PER_BLK);
        const uint16_t a = (uint16_t)(addr[i] & 0x1FFu), g = (uint16_t)(neg[i] & 0x3FFFu);
        e[0] = (uint8_t)a;                                                           /* kodovani z FW 0x0427 */
        e[1] = (uint8_t)(((ch[i] & 1u) << 7) | (((a >> 8) & 1u) << 6) | ((g >> 8) & 0x3Fu));
        e[2] = (uint8_t)g;
        b[0]++;                                                                      /* pocet polozek v bloku */
        if (sum) *sum = (uint16_t)(*sum + fpga_tdccal_chk(ch[i], a, g));
    }
    for (uint32_t k = 1; k < FPGA_V3_NBLK; k++) fpga_v3_blk_seal(f + k * FPGA_V3_BLK);
    return n;
}

int fpga_v3_parse(const uint8_t *f, fpga_v3_hdr_t *h, fpga_v3_rec_t *rec, uint32_t *n_ok)
{
    memset(h, 0, sizeof *h);
    if (n_ok) *n_ok = 0;
    if (f[0] != FPGA_V3_MAGIC) return FPGA_V3_E_MAGIC;
    if (f[1] != FPGA_V3_VER)   return FPGA_V3_E_VER;
    h->blk_ok = fpga_v3_blk_ok(f);
    if (!(h->blk_ok & 1u))     return FPGA_V3_E_CRC0;
    h->type = f[2]; h->flags = f[3];
    h->caps = (uint32_t)rdn(f + 24, 4);
    h->narrow = (h->caps & FPGA_V3_CAP_NARROW) ? 1u : 0u;
    const int wn = h->narrow ? 2 : 4;                    /* v0.40.0: uzka hlavicka = 16b SEQ, ukazatele kruhu, PSEQ */
    h->seq = (uint32_t)rdn(f + 4, wn);
    h->frame_len = (uint16_t)rdn(f + 8, 2); h->nblk = f[10];
    if (h->frame_len != FPGA_V3_LEN || h->nblk != FPGA_V3_NBLK) return FPGA_V3_E_LEN;
    h->build_time = (uint32_t)rdn(f + 12, 4); h->build_git = (uint32_t)rdn(f + 16, 4);
    h->fw_version = (uint16_t)rdn(f + 20, 2); h->clk_mhz = f[22]; h->seg_len_ms = f[23];
    /* TS10M (28..31) a RX_LAST (56..59) STM nikdy nepouzivalo -- necte se (FW 0x0507 je nuluje) */
    h->rec_head = (uint32_t)rdn(f + 32, wn); h->rec_first = (uint32_t)rdn(f + 36, wn);
    h->nrec = f[40];
    h->ring_fill = h->narrow ? (uint16_t)(h->rec_head - h->rec_first)   /* uzka: REC_HEAD = pocet -> fill bez odhadu */
                             : (uint16_t)rdn(f + 42, 2);
    h->clk_status = f[44]; h->clk_loss = f[45]; h->coh_st = f[46]; h->coh_cnt = f[47];
    h->tdc_status[0] = f[48]; h->tdc_status[1] = f[49]; h->pps_status = f[50]; h->cal_wait = f[51];
    h->rx_crc_err = (uint16_t)rdn(f + 52, 2); h->rx_len_err = (uint16_t)rdn(f + 54, 2);
    h->lut_cnt = (uint16_t)(rdn(f + 60, 2) & 0x3FFFu); h->lut_sum = (uint16_t)rdn(f + 62, 2);
    h->lut_cnt_b = (uint16_t)(rdn(f + 122, 2) & 0x3FFFu);
    h->deep_n = (uint32_t)rdn(f + 124, 3); h->deep_st = f[128]; h->deep_wsel = f[129];   /* v0.39.0: PSRAM */
    h->deep_win = (uint16_t)rdn(f + 130, 2); h->deep_ack = f[127];
    if (h->caps & FPGA_V3_CAP_CSDIAG) {                  /* v0.40.8: citace zahozenych pulzu na CS (FW 0x0508) */
        for (int i = 0; i < 5; i++) h->cs_gl[i] = f[132 + i];
        h->cs_filt = f[137];
    }
    h->presc[0] = f[64]; h->presc[1] = f[65]; h->cfg_echo = f[66];
    for (int i = 0; i < 2; i++) {
        const uint8_t *q = f + 68 + 16 * i;
        h->pps[i].pseq = (uint32_t)rdn(q, wn); h->pps[i].pts = rdn(q + 4, 6); h->pps[i].pflags = q[10];
        h->pps[i].d = (int8_t)q[11];
        h->pps_coarse[i] = (uint32_t)(h->pps[i].pts >> 14);   /* FW >= 0x0506: bajty 100..107 = 0; pts nese hruby cas x 16384 */
        h->probe[i] = (uint16_t)rdn(f + 108 + 2 * i, 2);
        h->bub[i] = f[112 + i]; h->ovl[i] = f[114 + i];
    }
    if (h->type == FPGA_V3_T_DATA && rec != NULL) {
        uint32_t n = h->nrec > FPGA_V3_REC_MAX ? FPGA_V3_REC_MAX : h->nrec, i;
        for (i = 0; i < n; i++) {
            const uint32_t k = 1u + i / FPGA_V3_REC_PER_BLK;
            const uint8_t *b = f + k * FPGA_V3_BLK;
            if (!((h->blk_ok >> k) & 1u) || b[0] != (uint8_t)k || b[1] != (uint8_t)h->seq) break;   /* vadny blok: dal ne */
            fpga_v3_rec_unpack(&rec[i], b + 2u + FPGA_V3_REC_LEN * (i % FPGA_V3_REC_PER_BLK));
        }
        if (n_ok) *n_ok = i;
        if (h->narrow)                                   /* BUB/OVL (112..115) = 0: priznaky nejnovejsiho zaznamu kanalu */
            for (uint32_t j = 0; j < i; j++) {
                const uint8_t c = (uint8_t)(rec[j].ch & 1u);
                h->bub[c] = (rec[j].flags & FPGA_V3_R_BUBBLE) ? 1u : 0u;
                h->ovl[c] = (rec[j].flags & FPGA_V3_R_CS) ? 1u : 0u;
            }
    }
    return FPGA_V3_OK;
}

void fpga_v3_wrap_init(fpga_v3_wrap_t *w) { memset(w, 0, sizeof *w); }

/* 16b hodnota v16 rozbalena k nejblizsi 32b hodnote kolem prev (posun -32768..+32767) */
static uint32_t ext16(uint32_t prev, uint32_t v16)
{
    return prev + (uint32_t)(int32_t)(int16_t)(uint16_t)(v16 - prev);
}

void fpga_v3_unwrap(fpga_v3_wrap_t *w, fpga_v3_hdr_t *h)
{
    if (h->narrow) {
        const uint32_t s16 = h->seq & 0xFFFFu;
        if (w->have && (uint32_t)(uint16_t)(s16 - w->seq) > FPGA_V3_WRAP_SEQ_MAX) {   /* restart FPGA */
            w->have = 0u; w->pps_have = 0u; w->restarts++;
        }
        if (!w->have) {
            w->have = 1u;
            w->seq = s16; w->head = h->rec_head & 0xFFFFu; w->first = h->rec_first & 0xFFFFu; w->pseq = h->pps[0].pseq & 0xFFFFu;
        } else {
            w->seq = ext16(w->seq, s16);
            w->head = ext16(w->head, h->rec_head);
            w->first = ext16(w->first, h->rec_first);
            w->pseq = ext16(w->pseq, h->pps[0].pseq);
        }
        h->seq = w->seq;
        h->rec_head = w->head ? w->head - 1u : 0u;     /* jako FW <= 0x0506: posledni zapsany zaznam */
        h->rec_first = w->first;
        h->pps[0].pseq = w->pseq;
    }
    /* historie 1PPS: posledni a predchozi ZVEREJNENA hrana (pseq se zmenil). Vede se vzdy (u siroke hlavicky jen pro
     * porovnani), pps[1] nahrazuje jen u uzke. Predchozi plati, jen kdyz STM videl hranu pseq - 1 (jinak PFLAGS 0). */
    if (h->pps[0].pflags & 1u) {
        if (!w->pps_have) { w->pps_have = 1u; w->pps_last = h->pps[0]; memset(&w->pps_prev, 0, sizeof w->pps_prev); }
        else if (h->pps[0].pseq != w->pps_last.pseq) {
            w->pps_prev = (h->pps[0].pseq == w->pps_last.pseq + 1u) ? w->pps_last : (fpga_v3_pps_t){ 0 };
            w->pps_last = h->pps[0];
        }
    }
    if (h->narrow) {
        if (w->pps_have && h->pps[0].pseq == w->pps_last.pseq) h->pps[1] = w->pps_prev;
        else memset(&h->pps[1], 0, sizeof h->pps[1]);
    }
}

/* ══════════════ emulator ══════════════ */
static uint64_t xs64(uint64_t *s) { uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; *s = x; return x; }
static double urand(uint64_t *s) { return (double)(xs64(s) >> 11) * (1.0 / 9007199254740992.0); }
static double grand(uint64_t *s)
{
    double u1 = urand(s), u2 = urand(s);
    if (u1 < 1e-300) u1 = 1e-300;
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

void fpga_v3_emu_init(fpga_v3_emu_t *e, double fa_hz, double fb_hz, double sigma_ps, uint32_t seed)
{
    memset(e, 0, sizeof *e);
    e->f_hz[0] = fa_hz; e->f_hz[1] = fb_hz;
    e->sigma_ps = sigma_ps;
    e->seg_ms = 25u; e->clk_mhz = 100u;
    e->rng = ((uint64_t)seed * 0x9E3779B97F4A7C15ull) | 1u;
    for (int c = 0; c < 2; c++) {
        e->x_next[c] = 1000u + 77u * (uint64_t)c;
        e->t_next[c] = (e->f_hz[c] > 0.0) ? urand(&e->rng) / e->f_hz[c] : 0.0;   /* nahodna faze prvni hrany */
    }
}

/* Jeden segment: pro kazdy kanal hrany t_next + i*p v [0, L). Znacka hrany m_i = round(t_i + eps_i) [jednotky],
 * T0 = m_0, ST = sum(m_i - T0) se slozi statisticky presne bez scitani po hranach:
 *   ST = p*N(N-1)/2 + sum_{i>=1} eps_i - (N-1)*eps_0 + sum_{i>=1} q_i - (N-1)*q_0,  q = zaokrouhleni.
 * Zacatek segmentu je v jednotkach PRESNE cele cislo (25 ms = 4,096e10 jednotek pri 100 MHz), cas dalsi hrany
 * se drzi relativne k nemu -> zadna ztrata presnosti ani po hodinach behu. */
void fpga_v3_emu_segment(fpga_v3_emu_t *e)
{
    const double L = (double)e->seg_ms * 1e-3;
    const double unit_s = fpga_v3_unit_ps(e->clk_mhz) * 1e-12;
    const uint64_t seg_units = (uint64_t)e->seg_ms * 16384ull * (uint64_t)e->clk_mhz * 1000ull;
    const uint64_t seg_start = (uint64_t)e->seg_idx * seg_units;
    const double sig_u = e->sigma_ps * 1e-12 / unit_s;
    for (uint8_t c = 0; c < 2; c++) {
        fpga_v3_rec_t r;
        memset(&r, 0, sizeof r);
        if (e->p_req[c] != e->p_log2[c]) { e->p_log2[c] = e->p_req[c]; e->p_inv[c] = 2u; r.flags |= FPGA_V3_R_PRESC; }
        r.seg_idx = (uint16_t)e->seg_idx; r.ch = (uint8_t)(c | (e->p_log2[c] << 4));   /* v0.38.0: log2 P v CH 6:4 */
        if (e->p_inv[c]) { e->p_inv[c]--; r.flags |= FPGA_V3_R_INVALID; }
        const double f = e->f_hz[c] / (double)(1u << e->p_log2[c]);   /* delene hrany */
        if (f > 0.0) {
            const double p = 1.0 / f, t = e->t_next[c];
            uint32_t N = (t < L) ? (uint32_t)ceil((L - t) / p) : 0u;
            if (N > 0u && t + (double)(N - 1u) * p >= L) N--;          /* poistka zaokrouhleni ceil */
            if (N > 0u) {
                const double e0 = grand(&e->rng) * sig_u;
                const double m0 = t / unit_s + e0;
                const double t0r = floor(m0 + 0.5);
                const double q0 = t0r - m0;
                const double nm1 = (double)(N - 1u);
                double st = (p / unit_s) * (double)N * nm1 * 0.5;
                if (N > 1u) st += grand(&e->rng) * sig_u * sqrt(nm1) - nm1 * e0
                                + grand(&e->rng) * sqrt(nm1 / 12.0) - nm1 * q0;
                else st = 0.0;
                r.n  = (N > 0xFFFFFFu) ? 0xFFFFFFu : N;
                if (N > 0xFFFFFFu) r.flags |= FPGA_V3_R_SAT;
                r.x0 = e->x_next[c] & MASK40;
                r.t0 = (seg_start + (uint64_t)t0r) & MASK48;
                r.sx = ((uint64_t)N * (uint64_t)(N - 1u) / 2u) & MASK48;
                r.st = (st > 0.0) ? ((uint64_t)(st + 0.5) & MASK56) : 0u;
            }
            e->x_next[c] += N;
            e->t_next[c] = t + (double)N * p - L;
        }
        e->ring[e->rec_head % FPGA_V3_EMU_RING] = r;
        e->rec_head++;
    }
    e->seg_idx++;
    if (e->rec_head - e->rec_ack > FPGA_V3_EMU_RING) {               /* kruh pretekl: nejstarsi zaznamy jsou pryc */
        e->rec_ack = e->rec_head - FPGA_V3_EMU_RING;
        e->ovf = 1u;
    }
}

void fpga_v3_emu_set_phase(fpga_v3_emu_t *e, uint8_t ch, double t_s) { e->t_next[ch & 1u] = t_s; }

void fpga_v3_emu_frame(fpga_v3_emu_t *e, uint8_t *f)
{
    memset(f, 0, FPGA_V3_LEN);
    const uint32_t avail = e->rec_head - e->rec_ack;
    const uint32_t nrec = avail > FPGA_V3_REC_MAX ? FPGA_V3_REC_MAX : avail;
    const int wn = e->narrow ? 2 : 4;                                /* v0.40.0: uzka hlavicka jako FW 0x0507 */
    e->seq++;
    f[0] = FPGA_V3_MAGIC; f[1] = FPGA_V3_VER; f[2] = FPGA_V3_T_DATA;
    f[3] = (uint8_t)((nrec ? FPGA_V3_F_NEWREC : 0u) | (e->ovf ? FPGA_V3_F_RINGOVF : 0u));
    wrn(f + 4, e->seq, wn);
    wrn(f + 8, FPGA_V3_LEN, 2); f[10] = (uint8_t)FPGA_V3_NBLK;
    wrn(f + 20, e->narrow ? 0x0507u : 0x0500u, 2);                   /* FW emulatoru */
    f[22] = (uint8_t)e->clk_mhz; f[23] = (uint8_t)e->seg_ms;
    wrn(f + 24, (1ull << 16) | FPGA_V3_CAP_PRESC | (e->narrow ? FPGA_V3_CAP_NARROW : 0u), 4);   /* bit16 v3, bit18 predelicka */
    for (int c = 0; c < 2; c++) {                                    /* v0.38.0: PRESC, sonda /16 za 1 ms */
        f[64 + c] = (uint8_t)(e->p_log2[c] | ((e->p_req[c] != e->p_log2[c]) ? FPGA_V3_PRESC_PEND : 0u));
        const double pr = e->f_hz[c] > 0.0 ? e->f_hz[c] / 16000.0 : 0.0;
        wrn(f + 108 + 2 * c, pr > 65535.0 ? 65535u : (uint32_t)pr, 2);
    }
    if (e->narrow) wrn(f + 32, e->rec_head, 2);                      /* uzka: pocet zaznamu, 16 b */
    else           wrn(f + 32, e->rec_head ? e->rec_head - 1u : 0u, 4);
    wrn(f + 36, e->rec_ack, wn);
    f[40] = (uint8_t)nrec;
    if (!e->narrow) wrn(f + 42, avail > 0xFFFFu ? 0xFFFFu : avail, 2);   /* uzka: RING_FILL = 0 */
    f[44] = 0x01u;                                                   /* hodiny OK */
    f[48] = 0x01u; f[49] = 0x01u;                                    /* kalibrace A/B platna */
    for (uint32_t k = 1; k < FPGA_V3_NBLK; k++) {
        uint8_t *b = f + k * FPGA_V3_BLK;
        b[0] = (uint8_t)k; b[1] = (uint8_t)e->seq;
        for (uint32_t m = 0; m < FPGA_V3_REC_PER_BLK; m++) {
            const uint32_t i = (k - 1u) * FPGA_V3_REC_PER_BLK + m;
            if (i < nrec) fpga_v3_rec_pack(b + 2u + FPGA_V3_REC_LEN * m, &e->ring[(e->rec_ack + i) % FPGA_V3_EMU_RING]);
        }
    }
    for (uint32_t k = 0; k < FPGA_V3_NBLK; k++) fpga_v3_blk_seal(f + k * FPGA_V3_BLK);
}

void fpga_v3_emu_rx(fpga_v3_emu_t *e, const uint8_t *f)
{
    if (f[0] != FPGA_V3_MAGIC || f[1] != FPGA_V3_VER || !(fpga_v3_blk_ok(f) & 1u)) return;
    if (f[2] == FPGA_V3_T_ACK) {
        uint32_t a = (uint32_t)rdn(f + 12, 4);
        if (e->narrow)                       /* FW 0x0507: jen dolnich 16 b, posun od REC_ACK modulo 2^16 (jako RTL) */
            a = e->rec_ack + (uint16_t)((uint16_t)a - (uint16_t)e->rec_ack);
        if ((uint32_t)(a - e->rec_ack) <= (uint32_t)(e->rec_head - e->rec_ack)) {   /* jen dopredu, ne za hlavu */
            e->rec_ack = a;
            e->ovf = 0u;
        }
    }
    if (f[2] == FPGA_V3_T_SETCFG && f[12] == FPGA_V3_CFG_PRESC)       /* v0.38.0: predelicka od dalsiho segmentu */
        e->p_req[f[13] & 1u] = (f[14] > FPGA_V3_RANGE_MAXLOG2) ? (uint8_t)FPGA_V3_RANGE_MAXLOG2 : f[14];
}

/* ══════════════ prijem zaznamu (STM) ══════════════ */
void fpga_v3_link_init(fpga_v3_link_t *L) { memset(L, 0, sizeof *L); }

uint32_t fpga_v3_link_take(fpga_v3_link_t *L, const fpga_v3_hdr_t *h, uint32_t n_ok, uint32_t *i0, int *gap)
{
    *i0 = 0u; *gap = 0;
    L->frames++;
    const uint32_t first = h->rec_first;
    const uint32_t head1 = h->rec_head + ((h->rec_head || h->nrec) ? 1u : 0u);   /* pocet zapsanych zaznamu */
    if (!L->have) { L->have = 1u; L->expect = first; *gap = 1; }
    else if ((uint32_t)(L->expect - head1) - 1u < 0x7FFFFFFFu) {                  /* expect > head1: FPGA zacala znovu */
        L->resync++; L->expect = first; *gap = 1;
    } else {
        const uint32_t d = first - L->expect;
        if (d != 0u && d < 0x80000000u) { L->gaps++; L->gap_recs += d; L->expect = first; *gap = 1; }   /* mezera */
        else if (d != 0u) {                                                       /* prekryv: duplicity preskocit */
            const uint32_t back = L->expect - first;
            if (back >= n_ok) { L->dups += n_ok; return 0u; }
            *i0 = back; L->dups += back;
        }
    }
    const uint32_t n = n_ok - *i0;
    L->expect += n; L->recs += n;
    return n;
}

/* ══════════════ jednorazove deje (STM) ══════════════ */
void fpga_v3_evu_init(fpga_v3_evu_t *u) { memset(u, 0, sizeof *u); }

uint32_t fpga_v3_ev_from_rec(fpga_v3_evu_t *u, const fpga_v3_rec_t *r, uint32_t clk_mhz, uint32_t now_ms, fpga_v3_ev_t out[2])
{
    if ((r->flags & FPGA_V3_R_INVALID) || r->n == 0u) return 0u;
    const uint8_t ch = (uint8_t)(r->ch & 1u);
    /* cas: jedna osa pro oba kanaly. Rozdil proti posledni znacce se znamenkem (B muze byt v segmentu pred A) a pocet
     * preteceni 2^48 (171,8 s) z uplynuleho casu STM (chyba casu prijmu ~0,1 s << 171,8 s) */
    if (!u->have) { u->have = 1u; u->t_un = r->t0; }
    else {
        uint64_t draw = (r->t0 - u->t_raw) & MASK48;
        const int64_t ds = (draw & (1ull << 47)) ? (int64_t)draw - (int64_t)(MASK48 + 1u) : (int64_t)draw;
        const double  e_u = (double)(uint32_t)(now_ms - u->ms) * 1e9 / fpga_v3_unit_ps(clk_mhz);
        const double  k   = floor((e_u - (double)ds) / 281474976710656.0 + 0.5);
        u->t_un = (uint64_t)((int64_t)u->t_un + ds + (int64_t)k * (int64_t)(MASK48 + 1u));
    }
    u->t_raw = r->t0; u->ms = now_ms;
    /* index hrany: po kanalech (40 b, kontinuita -- hrany jen pribyvaji) */
    if (!u->xhave[ch]) { u->xhave[ch] = 1u; u->x_un[ch] = r->x0; }
    else u->x_un[ch] += (r->x0 - u->x_raw[ch]) & MASK40;
    u->x_raw[ch] = r->x0;
    fpga_v3_ev_t *e = &out[0];
    memset(e, 0, sizeof *e);
    e->ch = ch; e->seg = r->seg_idx; e->x = u->x_un[ch]; e->t_u = u->t_un;
    if (r->n >= 3u) {
        e->kind = FPGA_V3_EV_BURST; e->n = r->n << FPGA_V3_REC_PLOG2(r);      /* v0.38.0: periody pinu (P x znacky) */
        e->per_u = r->sx ? (double)r->st / (double)r->sx / (double)(1u << FPGA_V3_REC_PLOG2(r)) : 0.0;
        return 1u;
    }
    e->kind = FPGA_V3_EV_EDGE; e->n = 1u;
    if (r->n == 1u) return 1u;
    out[1] = *e;                                                          /* N = 2: druha hrana presne */
    out[1].x = e->x + r->sx;
    out[1].t_u = e->t_u + r->st;
    return 2u;
}

void fpga_v3_ab_init(fpga_v3_ab_t *p, uint64_t max_u) { memset(p, 0, sizeof *p); p->max_u = max_u; }
static void ab_reset(fpga_v3_ab_t *p) { p->armed = 0u; }      /* neplatny zaznam / mezera: rozpracovany start pryc */

uint32_t fpga_v3_ab_feed(fpga_v3_ab_t *p, const fpga_v3_ev_t *ev, uint32_t n, int64_t *dt_u, uint32_t max_out)
{
    const fpga_v3_ev_t *o[8];
    uint32_t m = 0, out = 0;
    for (uint32_t i = 0; i < n && m < 8u; i++) o[m++] = &ev[i];
    for (uint32_t i = 1; i < m; i++)                                       /* serazeni v case (<= 8 udalosti) */
        for (uint32_t j = i; j > 0 && o[j]->t_u < o[j - 1]->t_u; j--) { const fpga_v3_ev_t *t = o[j]; o[j] = o[j - 1]; o[j - 1] = t; }
    for (uint32_t i = 0; i < m; i++) {
        const fpga_v3_ev_t *e = o[i];
        if (p->armed && e->t_u - p->t_start > p->max_u) { p->armed = 0u; p->lost++; }   /* start bez stopu */
        if (!p->armed) {
            if (e->ch == 0u) { p->armed = 1u; p->t_start = e->t_u; }
        } else if (e->ch == 1u && e->t_u > p->t_start) {
            if (out < max_out) dt_u[out++] = (int64_t)(e->t_u - p->t_start);
            p->n++;
            p->armed = 0u;
        }
    }
    /* souhrn B (N >= 3) zacal pred startem a trva pres nej: prvni hrana B PO startu je neznama (presna je jen prvni
     * hrana souhrnu) -> interval by nebyl presny, start zahodit */
    if (p->armed) {
        for (uint32_t i = 0; i < m; i++) {
            const fpga_v3_ev_t *e = o[i];
            if (e->ch == 1u && e->kind == FPGA_V3_EV_BURST && e->t_u <= p->t_start &&
                (double)e->t_u + (double)(e->n - 1u) * e->per_u > (double)p->t_start) { p->armed = 0u; p->lost++; break; }
        }
    }
    return out;
}

void   fpga_v3_stat_init(fpga_v3_stat_t *s) { memset(s, 0, sizeof *s); }
void   fpga_v3_stat_add(fpga_v3_stat_t *s, double v)
{
    if (s->n == 0u || v < s->min) s->min = v;
    if (s->n == 0u || v > s->max) s->max = v;
    s->n++;
    const double d = v - s->mean;          /* Welford: sum(v^2) - n*m^2 ztraci pri m = 5e9 ps desitky ps */
    s->mean += d / (double)s->n;
    s->m2   += d * (v - s->mean);
}
double fpga_v3_stat_mean(const fpga_v3_stat_t *s) { return s->n ? s->mean : 0.0; }
double fpga_v3_stat_sd(const fpga_v3_stat_t *s)
{
    if (s->n < 2u) return 0.0;
    const double v = s->m2 / (double)(s->n - 1u);
    return v > 0.0 ? sqrt(v) : 0.0;
}

int fpga_v3_skew_add(fpga_v3_stat_t *s, const fpga_v3_ev_t *ea, const fpga_v3_ev_t *eb, int64_t lim_u, double unit_ps)
{
    const int64_t d = (int64_t)(eb->t_u - ea->t_u);
    if (d > lim_u || d < -lim_u) return 0;
    fpga_v3_stat_add(s, (double)d * unit_ps);
    return 1;
}

void fpga_v3_pr_init(fpga_v3_pr_t *p, uint64_t max_u)
{
    memset(p, 0, sizeof *p);
    fpga_v3_ab_init(&p->ab, max_u);
    fpga_v3_stat_init(&p->sk);
}
void fpga_v3_pr_switch(fpga_v3_pr_t *p) { p->skip = 1u; p->skip_n = 0u; p->pa_have = 0u; ab_reset(&p->ab); }
void fpga_v3_pr_gap(fpga_v3_pr_t *p)    { p->pa_have = 0u; ab_reset(&p->ab); }

uint32_t fpga_v3_pr_rec(fpga_v3_pr_t *p, const fpga_v3_rec_t *r, const fpga_v3_ev_t *e, uint32_t ne, int skew,
                        double unit_ps, int64_t *dt_u, uint32_t max_out)
{
    const int bad = (r->flags & (FPGA_V3_R_INVALID | FPGA_V3_R_CS)) != 0u;
    uint32_t out = 0u;
    if (ne > 2u) ne = 2u;
    if ((r->ch & 1u) == 0u) {
        if (p->pa_have && !skew && !p->skip) out = fpga_v3_ab_feed(&p->ab, p->pa, p->pan, dt_u, max_out);   /* A bez B */
        p->pa_have = 0u;
        if (bad) { ab_reset(&p->ab); return out; }
        for (uint32_t k = 0; k < ne; k++) p->pa[k] = e[k];
        p->pan = ne; p->pa_seg = r->seg_idx; p->pa_have = 1u;
        return out;
    }
    if (p->skip) {                                       /* prepnuti zdroje B: az za drzenim B */
        if ((r->flags & FPGA_V3_R_INVALID) || ++p->skip_n > 80u) p->skip = 0u;
        ab_reset(&p->ab); p->pa_have = 0u;
        return 0u;
    }
    if (bad) { ab_reset(&p->ab); p->pa_have = 0u; return 0u; }
    const int same = p->pa_have && p->pa_seg == r->seg_idx;
    if (skew) {
        if (same && p->pan && ne) (void)fpga_v3_skew_add(&p->sk, &p->pa[0], &e[0], (int64_t)(50000.0 / unit_ps), unit_ps);
    } else {
        fpga_v3_ev_t ev[4];
        uint32_t m = 0u;
        if (p->pa_have && !same) out = fpga_v3_ab_feed(&p->ab, p->pa, p->pan, dt_u, max_out);
        else if (same) for (uint32_t k = 0; k < p->pan; k++) ev[m++] = p->pa[k];
        for (uint32_t k = 0; k < ne; k++) ev[m++] = e[k];
        if (m) out += fpga_v3_ab_feed(&p->ab, ev, m, dt_u + out, max_out - out);
    }
    p->pa_have = 0u;
    return out;
}

/* ══════════════ segmenty -> okno ══════════════ */
void fpga_v3_grp_init(fpga_v3_grp_t *g, uint32_t k_target)
{
    memset(g, 0, sizeof *g);
    g->k_target = k_target ? k_target : 1u;
    g->seg_ms = 25u;
    g->max_gap_ms = 2500u;
}

void fpga_v3_grp_break(fpga_v3_grp_t *g, uint8_t ch) { g->c[ch & 1u].open = 0u; }

/* v0.42.11 (L-0247): kanal B FW 0x0508 obcas cte index znacky s vynulovanymi bity 6, 7, 32, 33 (noc 2026-10-28:
 * vsech 993 vadnych X0, nikdy se bit nenastavi). V SX se to projevi jako -k*2^32 pri spravnych spodnich 32 b, nebo
 * jako -64*k. Meze SX (fpga_v3_seg_sx_ok) jsou casto uzsi nez 2^32 -> spodni 32 b urcuji SX jednoznacne.
 * @return 1 = *out = jedina hodnota v mezich se spodnimi 32 b jako sx */
static int sx_lo32_fit(uint64_t n, uint64_t dx, uint64_t sx, uint64_t *out)
{
    if (n < 2u || dx < n || dx >= (1ull << 32)) return 0;
    const uint64_t lo = n * (n - 1u) / 2u, hi = (n - 1u) * dx - lo;
    if (hi - lo >= (1ull << 32)) return 0;
    const uint64_t c = lo + ((sx - lo) & 0xFFFFFFFFull);
    if (c > hi) return 0;
    *out = c;
    return 1;
}

/* Oprava SX segmentu a, ktery neprosel kontrolou (a nasleduje b). Kandidati: (1) horni cast SX z mezi; (2) bylo-li X0
 * zaznamu opraveno o dv (X0 = surove - dv), bylo vadne uz ve FPGA registru x0 a kazdy z N-1 clenu SX = sum(xm - x0)
 * je o -dv vetsi -> SX + (N-1)*dv, pak (1). Kandidat plati, jen kdyz s nim projde i kontrola teziste.
 * @return 0 = nelze, 1 = oprava (1), 2 = oprava (2); *r_ps = odchylka teziste s opravenym SX */
static int sx_repair(fpga_v3_rec_t *a, const fpga_v3_rec_t *b, int64_t dv, uint32_t clk_mhz, double *r_ps)
{
    if (a->n < 2u || (a->flags & FPGA_V3_R_SAT)) return 0;
    const uint64_t n = a->n, dx = (b->x0 - a->x0) & MASK40;
    uint64_t cand[2];
    int kind[2], nc = 0;
    if (sx_lo32_fit(n, dx, a->sx, &cand[nc])) kind[nc++] = 1;
    if (dv != 0 && sx_lo32_fit(n, dx, (a->sx + (uint64_t)((int64_t)(n - 1u) * dv)) & MASK48, &cand[nc])) kind[nc++] = 2;
    for (int i = 0; i < nc; i++) {
        fpga_v3_rec_t t = *a;
        double rp; int64_t sd;
        t.sx = cand[i];
        if (fpga_v3_seg_resid(&t, b, clk_mhz, &rp, &sd) && fabs(rp) <= FPGA_V3_SEG_BAD_PS) {
            a->sx = cand[i]; *r_ps = rp;
            return kind[i];
        }
    }
    return 0;
}

int fpga_v3_grp_feed(fpga_v3_grp_t *g, const fpga_v3_rec_t *r, uint32_t clk_mhz, fpga_v3_win_t *out)
{
    const uint8_t ch = (uint8_t)(r->ch & 1u);
    const uint8_t pl = FPGA_V3_REC_PLOG2(r);           /* v0.38.0: X pocita delene hrany (P = 2^pl) */
    fpga_v3_grp_ch_t *c = &g->c[ch];
    int closed = 0;

    /* v0.42.12: spolecne rozbalene cislo segmentu (zaznamy A a B tehoz segmentu jdou po sobe se stejnym SEG_IDX) */
    if (!g->seg_have) { g->seg_u = r->seg_idx; g->seg_have = 1u; }
    else g->seg_u += (uint16_t)(r->seg_idx - g->seg_raw);
    g->seg_raw = r->seg_idx;
    const uint32_t cell = g->seg_u / g->k_target, pos = g->seg_u % g->k_target;

    /* v0.42.7 (L-0247): kanal B FW 0x0508 obcas zachyti X0 o nasobek 64 hran vedle (chybne bity 6.. indexu, T0 spravne).
     * Na hranici oken to posune dvoubodovy odhad obou oken opacnym smerem a kontrola teziste ho neopravi (vadne X0 je
     * i v dvoubodovem odhadu). X0 se predpovi z T0 a periody posledni dvojice zaznamu; u stabilniho signalu sedi na
     * zlomky hrany. Opravi se jen odchylka 64*k (+-0,1 hrany) po ciste predchozi kontrole. Jinak se jen pocita. */
    fpga_v3_rec_t rc;
    int64_t dv_now = 0;                                  /* v0.42.11: oprava X0 tohoto zaznamu (pro opravu SX) */
    if (r->n && c->have_prev) {
        if ((r->flags & FPGA_V3_R_INVALID) || pl != c->pl_prev) { c->per_ref = 0.0; c->clean = 0u; }
        else {
            const uint16_t sg = (uint16_t)(r->seg_idx - c->seg_mk);
            uint64_t dx = (r->x0 - c->x_prev_raw) & MASK40;
            const uint64_t dt = (r->t0 - c->t_prev_raw) & MASK48;
            if (c->per_ref > 0.0 && sg >= 1u) {
                const double pred = (double)dt / c->per_ref, d = (double)dx - pred;
                if (sg > 1u && pred >= 1e4) c->clean = 0u;               /* mezera u rychleho signalu: nepredpovidat */
                else if (fabs(d) < 0.5) c->clean = (uint8_t)(fabs(d) < 0.1);
                else {
                    const double k = floor(d / 64.0 + 0.5);
                    int fix = c->clean && k != 0.0 && fabs(d - 64.0 * k) < 0.1;
                    /* v0.42.10: perioda ST/SX se k oprave nepouziva -- u kanalu B ji casto kazi vadne SX predchoziho
                     * segmentu (namereno: X0 -64,007 hrany po ciste kontrole, ST/SX o 20,7 hrany vedle) */
                    const double pd = (c->pst > 0.0) ? (double)dt / c->pst - pred : 0.0;
                    {   /* v0.42.9: diagnostika */
                        fpga_v3_x0ev_t *e = &g->x0ev[g->nx0ev % FPGA_V3_X0EV];
                        const double dm = d * 1000.0, pm = pd * 1000.0;
                        e->seg = r->seg_idx; e->ch = ch; e->dx = dx; e->dt = dt; e->x0 = r->x0;
                        e->st = (uint8_t)((fix ? 1u : 0u) | (c->clean ? 2u : 0u) | (c->pst > 0.0 ? 4u : 0u));
                        e->d_m  = (int32_t)(dm > 2e9 ? 2e9 : dm < -2e9 ? -2e9 : dm);
                        e->pd_m = (int32_t)(pm > 2e9 ? 2e9 : pm < -2e9 ? -2e9 : pm);
                        g->nx0ev++;
                        if (fabs(d) >= 8.0) { g->x0big[g->nx0big % FPGA_V3_X0EV] = *e; g->nx0big++; }
                    }
                    if (fix) {
                        const int64_t dv = (int64_t)(64.0 * k);
                        rc = *r; rc.x0 = (r->x0 - (uint64_t)dv) & MASK40; r = &rc;
                        dx = (uint64_t)((int64_t)dx - dv) & MASK40;
                        g->x0_fix[ch]++; g->x0_dev[ch] = (int32_t)dv; dv_now = dv;
                    } else { g->x0_odd[ch]++; c->clean = 0u; }
                }
            }
            c->per_ref = dx ? (double)dt / (double)dx : 0.0;
        }
    }
    if (r->n) {
        c->seg_mk = r->seg_idx; c->pl_prev = pl;
        c->pst = (r->n >= 2u && r->sx) ? (double)r->st / (double)r->sx : 0.0;
    }

    /* v0.42.5 (L-0247): predchozi zaznam patri do rozpracovaneho okna (uzavira ho az tento) -> jeho teziste proti primce
     * k prvni znacce tohoto zaznamu. Kanal B FW 0x0508 obcas paruje znacku s indexem o hranu vedle (hrana do ~0,5 ns od
     * hrany taktu) -- T0/X0 segmentu zustavaji spravne, vadne jsou jen soucty ST/SX. */
    {
        double rp; int64_t sd;
        int pbad = 0;
        if (c->open && c->prev_ok && fpga_v3_seg_resid(&c->prev, r, clk_mhz, &rp, &sd)) {
            if (fabs(rp) > FPGA_V3_SEG_BAD_PS || !fpga_v3_seg_sx_ok(&c->prev, r)) {
                /* v0.42.11: pokus o opravu SX (sx_repair); teziste ceka v pX (pend), u prvniho segmentu okna i xc0 */
                const uint64_t sx0 = c->prev.sx;
                const int how = sx_repair(&c->prev, r, c->prev_dv, clk_mhz, &rp);
                if (how) {
                    /* pX a xc0 braly SX jako cislo (< 2^48, v double presne), ne mod 2^48 */
                    const double dsx = (double)c->prev.sx - (double)sx0;
                    if (c->pend) c->pX += dsx / c->pw;
                    if (c->k == 1u) c->xc0 += dsx / (double)c->prev.n;
                    g->sx_rep[ch]++;
                    if (how == 2) g->sx_rep_x0[ch]++;
                } else { c->seg_bad = 1u; pbad = 1; }
            }
            if (fabs(rp) > c->seg_max_ps) c->seg_max_ps = fabs(rp);
        }
        /* v0.42.7: teziste predchoziho segmentu do LSQ az po kontrole; vadne se vyradi (zbytek okna plati dal, misto
         * celeho okna na dvoubodove se sumem dvou znacek). Nelze-li zkontrolovat, plati jako driv. */
        if (c->pend) {
            if (c->open && !pbad) {
                const double w = c->pw, X = c->pX, T = c->pT;
                c->sw += w; c->swx += w * X; c->swt += w * T; c->swxx += w * X * X; c->swxt += w * X * T; c->kl++;
            }
            c->pend = 0u;
        }
        c->prev = *r; c->prev_ok = 1u; c->prev_dv = dv_now;
    }

    /* rozbaleni X0 (40 b) a T0 (48 b) proti predchozimu zaznamu kanalu; jen u zaznamu s hranou */
    const uint64_t xb = c->x_prev, tb = c->t_prev;
    const uint8_t hb = c->have_prev;
    uint64_t x = 0, t = 0;
    if (r->n) {
        if (!c->have_prev) { x = r->x0; t = r->t0; c->have_prev = 1u; }
        else {
            x = c->x_prev + ((r->x0 - c->x_prev_raw) & MASK40);
            t = c->t_prev + ((r->t0 - c->t_prev_raw) & MASK48);
        }
        c->x_prev_raw = r->x0; c->t_prev_raw = r->t0; c->x_prev = x; c->t_prev = t;
    }
    /* souvislost segmentu, neplatny zaznam -> rozpracovane okno zahodit */
    if (r->seg_idx != (uint16_t)(c->seg_last + 1u)) { c->open = 0u; c->clean_ok = 0u; }
    if (c->open && pl != c->p_log2) c->open = 0u;       /* zmena P: okno pres zmenu meritka X nesmi */
    c->seg_last = r->seg_idx;
    if (r->flags & FPGA_V3_R_INVALID) { c->open = 0u; c->clean_ok = 0u; return 0; }
    if (r->n == 0u) {
        /* prazdny segment: pri periode > L normalni, okno pokracuje (bez prispevku k LSQ). Ztrata signalu
         * (docs/PROTOKOL_V3.md kap. 7) = mezera bez hrany > 3 ocekavane periody, nejvys max_gap_ms -> okno zahodit. */
        if (c->open) {
            const uint32_t L = g->seg_ms ? g->seg_ms : 25u;
            uint32_t tol = g->max_gap_ms / L;
            if (c->per_u > 0.0) {
                const double t3 = 3.0 * c->per_u * fpga_v3_unit_ps(clk_mhz) / ((double)L * 1e9);
                if (t3 < (double)tol) tol = (uint32_t)t3;
            }
            c->empty++;
            if (c->empty > tol) c->open = 0u; else c->k++;
        }
        if (!c->open) {                               /* v0.42.12: bunka zatim bez hrany -> okno smi zacit na 1. hrane */
            if (pos == 0u) { c->clean_ok = 1u; c->clean_cell = cell; }
            else if (c->clean_cell != cell) c->clean_ok = 0u;
        }
        return 0;
    }
    /* odhad periody pro limit mezery: z tohoto zaznamu (ST/SX), jinak od predchozi hrany v temze okne */
    if (r->n >= 2u && r->sx) c->per_u = (double)r->st / (double)r->sx;
    else if (hb && c->open && x > xb) c->per_u = (double)(t - tb) / (double)(x - xb);
    else c->per_u = 0.0;
    c->empty = 0u;

    if (c->open && cell != c->cell) {                /* v0.42.12: prvni hrana v dalsi bunce -> okno uzavrit */
        const double up = fpga_v3_unit_ps(clk_mhz);
        out->ch = ch; out->seg_first = c->seg_first; out->nseg = c->k; out->n_marks = c->n_tot;
        out->edges = (x - c->x_first) << c->p_log2; out->gate_units = t - c->t_first;   /* periody = hrany x P */
        out->f_2pt_hz = (out->gate_units) ? (double)out->edges * 1e12 / ((double)out->gate_units * up) : 0.0;
        const double mx = c->swx / c->sw, mt = c->swt / c->sw;
        const double vxx = c->swxx / c->sw - mx * mx, vxt = c->swxt / c->sw - mx * mt;
        out->lsq_ok = (uint8_t)(c->kl >= 2u && vxx > 0.0 && vxt > 0.0);
        out->f_lsq_hz = out->lsq_ok ? (double)(1u << c->p_log2) * 1e12 / ((vxt / vxx) * up) : 0.0;   /* sklon t(x) = P perioda */
        out->flags_or = c->flags_or; out->xc0 = c->xc0 * (double)(1u << c->p_log2); out->tc0 = c->tc0;
        out->x_first = c->x_first << c->p_log2; out->t_first = c->t_first; out->n0 = c->n0; out->p_log2 = c->p_log2;
        out->seg_bad = c->seg_bad; out->seg_max_ps = c->seg_max_ps; out->cell = c->cell;
        closed = 1;
        c->open = 0u;
    }
    if (!c->open) {                                   /* zacatek okna */
        /* v0.42.12: okno zacina jen na hranici bunky (nebo na 1. hrane bunky, jejiz predchozi segmenty prisly bez
         * hrany, nebo na zaznamu, ktery prave uzavrel okno) -> okna A a B pokryvaji tytez segmenty */
        if (!closed && pos != 0u && !(c->clean_ok && c->clean_cell == cell)) { c->clean_ok = 0u; return 0; }
        c->clean_ok = 0u; c->cell = cell;
        c->open = 1u; c->k = 0u; c->n_tot = 0u; c->seg_first = r->seg_idx; c->p_log2 = pl;
        c->x_first = x; c->t_first = t;
        c->sw = c->swx = c->swt = c->swxx = c->swxt = 0.0; c->flags_or = 0u; c->kl = 0u;
        c->seg_bad = 0u; c->seg_max_ps = 0.0;
        c->xc0 = (double)x + (double)r->sx / (double)r->n;
        c->n0  = r->n;
        c->tc0 = (double)t + (double)r->st / (double)r->n;
    }
    /* teziste segmentu relativne k prvni znacce okna; vaha = pocet znacek */
    const double w = (double)r->n;
    c->pw = w;
    c->pX = (double)(x - c->x_first) + (double)r->sx / w;
    c->pT = (double)(t - c->t_first) + (double)r->st / w;
    c->pend = 1u;
    c->k++; c->n_tot += r->n; c->flags_or |= r->flags;
    return closed;
}

int fpga_v3_seg_resid(const fpga_v3_rec_t *a, const fpga_v3_rec_t *b, uint32_t clk_mhz, double *r_ps,
                      int64_t *sx_dev)
{
    if (a->n < 2u || b->n == 0u || ((a->flags | b->flags) & FPGA_V3_R_INVALID)) return 0;
    if ((a->ch & 1u) != (b->ch & 1u) || (a->ch >> 4) != (b->ch >> 4)) return 0;      /* kanal a predelicka */
    if (b->seg_idx != (uint16_t)(a->seg_idx + 1u)) return 0;
    const uint64_t dx = (b->x0 - a->x0) & MASK40, dt = (b->t0 - a->t0) & MASK48;
    if (dx == 0u) return 0;
    const double per = (double)dt / (double)dx;
    *r_ps   = ((double)a->st - per * (double)a->sx) / (double)a->n * fpga_v3_unit_ps(clk_mhz);
    *sx_dev = (int64_t)a->sx - (int64_t)((uint64_t)a->n * (uint64_t)(a->n - 1u) / 2u);
    return 1;
}

int fpga_v3_seg_sx_ok(const fpga_v3_rec_t *a, const fpga_v3_rec_t *b)
{
    if (a->n < 2u || (a->flags & FPGA_V3_R_SAT)) return 1;
    const uint64_t n = a->n, dx = (b->x0 - a->x0) & MASK40;
    if (dx >= (1ull << 32)) return 1;
    if (dx < n) return 0;                              /* vic znacek nez hran */
    const uint64_t lo = n * (n - 1u) / 2u, hi = (n - 1u) * dx - lo;
    return a->sx >= lo && a->sx <= hi;
}

/* ══════════════ v0.43.0: kalibrace TDC ze vstupniho pinu (FW 0x0509, krok 1) ══════════════ */
static int32_t worst8(const double occ[8], double tot)
{
    int32_t worst = 0;
    for (int k = 0; k < 8; k++) {
        const int32_t d = (int32_t)lround(occ[k] * 8000.0 / tot) - 1000;
        if ((d < 0 ? -d : d) > (worst < 0 ? -worst : worst)) worst = d;
    }
    return worst;
}

int fpga_v3_calpin_check(const uint32_t *hist, const uint32_t *ref_h, uint32_t nc, uint16_t nz_ref,
                         uint16_t *nz, int16_t *dev_permil)
{
    double occ[8] = { 0.0 }, tot = 0.0, rtot = 0.0;
    uint32_t n = 0u;
    if (ref_h != NULL) for (uint32_t a = 0; a < nc; a++) rtot += (double)ref_h[a];
    double c0 = 0.0;                                   /* zacatek binu kodu a podle referencniho histogramu [osminy] */
    for (uint32_t a = 0; a < nc; a++) {
        const double h = (double)hist[a];
        const double c1 = (rtot > 0.0) ? c0 + (double)ref_h[a] * 8.0 / rtot : c0;
        if (hist[a] != 0u) {
            n++;
            tot += h;
            if (rtot > 0.0) {
                if (c1 <= c0) occ[(int)c0 > 7 ? 7 : (int)c0] += h;      /* bin bez sirky: bod */
                else for (int k = (int)c0; k < 8 && (double)k < c1; k++) {
                    const double lo = (c0 > (double)k) ? c0 : (double)k, hi = (c1 < (double)(k + 1)) ? c1 : (double)(k + 1);
                    if (hi > lo) occ[k] += h * (hi - lo) / (c1 - c0);
                }
            }
        }
        c0 = c1;
    }
    *nz = (uint16_t)n;
    *dev_permil = 0;
    if (tot <= 0.0) return 3;
    int32_t worst = 0;
    if (rtot > 0.0) { worst = worst8(occ, tot); *dev_permil = (int16_t)worst; }
    const uint32_t nz_min = nz_ref ? ((uint32_t)nz_ref * 9u + 9u) / 10u : 64u;
    if (n < nz_min) return 1;
    if (worst > 100 || worst < -100) return 2;
    return 0;
}

int16_t fpga_v3_calpin_cover(double f_hz, uint32_t n)
{
    if (!(f_hz > 0.0) || n == 0u) return 32767;
    const double r = 100.0e6 / f_hz;                   /* perioda signalu v taktech */
    const double al = r - floor(r);                    /* posun faze hrany vuci taktu na hranu [takty] */
    double ph = 0.0, occ[8] = { 0.0 };
    for (uint32_t i = 0; i < n; i++) {
        int k = (int)(ph * 8.0);
        occ[k > 7 ? 7 : k] += 1.0;
        ph += al;
        if (ph >= 1.0) ph -= 1.0;
    }
    return (int16_t)worst8(occ, (double)n);
}

/* ══════════════ v0.42.12: parovani oken A a B ══════════════ */
void fpga_v3_pair_init(fpga_v3_pair_t *p) { memset(p, 0, sizeof *p); }

static void pair_out(fpga_v3_pair_out_t *o, const fpga_v3_win_t *a, const fpga_v3_win_t *b)
{
    o->a = *a; o->has_b = (uint8_t)(b != NULL);
    if (b) o->b = *b;
}

/* okno a, ke kteremu uz okno druheho kanalu ze stejne bunky neprijde: druhy kanal ma otevrene okno z teze nebo drivejsi
 * bunky i po dalsim okne a (pomaly signal, okno pres vic bunek) -> posledni uzavrene okno druheho kanalu jako do
 * v0.42.11; jinak bez nej */
static void pair_late(fpga_v3_pair_t *p, const fpga_v3_grp_ch_t *cs, const fpga_v3_win_t *a, fpga_v3_pair_out_t *o)
{
    const int slow = cs->open && (int32_t)(cs->cell - a->cell) <= 0 && p->b_ok;
    pair_out(o, a, slow ? &p->b : NULL);
}

uint32_t fpga_v3_pair_feed(fpga_v3_pair_t *p, const fpga_v3_grp_t *g, uint8_t prim, const fpga_v3_rec_t *r,
                           const fpga_v3_win_t *w, fpga_v3_pair_out_t out[2])
{
    uint32_t n = 0;
    prim &= 1u;
    const fpga_v3_grp_ch_t *cs = &g->c[prim ^ 1u];      /* druhy kanal (stav po zaznamu r) */
    if (w && (w->ch & 1u) == prim) {
        if (p->a_ok) { pair_late(p, cs, &p->a, &out[n++]); p->a_ok = 0u; }   /* predchozi se sveho B nedockalo */
        if (p->b_ok && p->b.cell == w->cell) { pair_out(&out[n++], w, &p->b); p->b_ok = 0u; }
        else if (cs->open && cs->cell == w->cell) { p->a = *w; p->a_ok = 1u; }   /* okno B teze bunky se jeste uzavre */
        else pair_late(p, cs, w, &out[n++]);
    } else if (w) {
        p->b = *w; p->b_ok = 1u;
    }
    if ((r->ch & 1u) != prim && p->a_ok) {
        if (p->b_ok && p->b.cell == p->a.cell) { pair_out(&out[n++], &p->a, &p->b); p->a_ok = p->b_ok = 0u; }
        else if (!(cs->open && cs->cell == p->a.cell)) { pair_late(p, cs, &p->a, &out[n++]); p->a_ok = 0u; }
    }
    return n;
}

uint32_t fpga_v3_pair_flush(fpga_v3_pair_t *p, fpga_v3_pair_out_t out[1])
{
    uint32_t n = 0;
    if (p->a_ok) { pair_out(&out[n++], &p->a, NULL); p->a_ok = 0u; }
    p->b_ok = 0u;
    return n;
}

/* ══════════════ selftest ══════════════ */
static fpga_v3_emu_t s_e;
static fpga_v3_grp_t s_g;
static uint8_t       s_f[FPGA_V3_LEN], s_m[FPGA_V3_LEN];
static fpga_v3_rec_t s_rec[FPGA_V3_REC_MAX];

/* Projede nseg segmentu emulatorem: kazde 2 segmenty jeden ramec, parser, okna, ACK (jako STM). Volitelne v ramci
 * c. bad_frame poskodi blok 2. Vraci pocet oken kanalu A a jejich nejvetsi relativni chybu proti f_true;
 * LSQ jen u oken s lsq_ok (jejich pocet v *nlsq). */
static uint32_t s_nlsq, s_nbad;
static uint32_t st_run(uint32_t nseg, int bad_frame, double f_true, double *maxrel_2pt, double *maxrel_lsq,
                       uint32_t *gaps)
{
    uint32_t nwin = 0, fr = 0, expect = s_e.rec_ack;
    *maxrel_2pt = *maxrel_lsq = 0.0; *gaps = 0; s_nlsq = 0; s_nbad = 0;
    for (uint32_t s = 0; s < nseg; s += 6) {                   /* 6 segmentu = 12 zaznamu na ramec: obsazene bloky 1 a 2 */
        for (int q = 0; q < 6; q++) fpga_v3_emu_segment(&s_e);
        fpga_v3_emu_frame(&s_e, s_f);
        if ((int)fr == bad_frame) s_f[2 * FPGA_V3_BLK + 2 + 14] ^= 0x40u;   /* blok 2, zaznam 0 (kanal A), bajt T0 */
        fr++;
        fpga_v3_hdr_t h; uint32_t n_ok = 0;
        if (fpga_v3_parse(s_f, &h, s_rec, &n_ok) != FPGA_V3_OK) return 0;
        if (h.rec_first != expect) (*gaps)++;          /* STM: mezera v poradi zaznamu */
        for (uint32_t i = 0; i < n_ok; i++) {
            fpga_v3_win_t w;
            if (fpga_v3_grp_feed(&s_g, &s_rec[i], h.clk_mhz, &w) && w.ch == 0u) {
                const double r2 = fabs(w.f_2pt_hz / f_true - 1.0), rl = fabs(w.f_lsq_hz / f_true - 1.0);
                if (r2 > *maxrel_2pt) *maxrel_2pt = r2;
                if (w.lsq_ok) { s_nlsq++; if (rl > *maxrel_lsq) *maxrel_lsq = rl; }
                if (w.seg_bad) s_nbad++;
                nwin++;
            }
        }
        expect = h.rec_first + n_ok;
        fpga_v3_build_ack(s_m, fr, expect);
        fpga_v3_emu_rx(&s_e, s_m);
    }
    return nwin;
}

static int s_fail_line;   /* radek prvni neuspesne kontroly selftestu (0 = vse proslo) */
int fpga_v3_selftest_fail_line(void) { return s_fail_line; }
#define V3OK(c) do { if (!(c)) { ok = false; if (!s_fail_line) s_fail_line = __LINE__; } } while (0)

bool fpga_v3_selftest(void)
{
    bool ok = true;
    s_fail_line = 0;
    double r2, rl; uint32_t gaps, nw;

    /* 1) CRC bloku: postaveny ramec ma vsechny bloky v poradku, poskozeni jednoho bajtu shodi prave ten blok */
    fpga_v3_build_cfg(s_m, 7u, FPGA_V3_CFG_LUT, 1u, 0u);
    V3OK((fpga_v3_blk_ok(s_m) == 0x0Fu));
    s_m[3 * FPGA_V3_BLK + 7] ^= 1u;
    V3OK((fpga_v3_blk_ok(s_m) == 0x07u));

    /* 1b) CRC bloku = CRC-16/CCITT-FALSE PRAVE bajtu 0..253 (nezavisly vypocet, jako FPGA; ne jen vnitrni shoda) */
    fpga_v3_build_cfg(s_m, 8u, FPGA_V3_CFG_PRESC, 0u, 3u);         /* cerstvy ramec (krok 1 poskodil blok 3) */
    for (uint32_t k = 0; k < FPGA_V3_NBLK; k++) {
        const uint8_t *b = s_m + k * FPGA_V3_BLK;
        V3OK((fpga_crc16_sw(b, 254) == (uint16_t)(b[254] | ((uint16_t)b[255] << 8))));
    }

    /* 2) zaznam: pack/unpack s maximalnimi hodnotami poli */
    {
        fpga_v3_rec_t a = { 0xFFFFu, 1u, 0x7Fu, 0xFFFFFFu, MASK40, MASK48, MASK48, MASK56 }, b;
        uint8_t buf[FPGA_V3_REC_LEN];
        fpga_v3_rec_pack(buf, &a); fpga_v3_rec_unpack(&b, buf);
        V3OK((memcmp(&a, &b, sizeof a) == 0));
    }

    /* 3) LUT_WRITE: 252 polozek, soucet = soucet fpga_tdccal_chk (stejny vzorec jako LUT_SUM ve FPGA) */
    {
        static uint8_t ch[FPGA_V3_LUTW_MAX]; static uint16_t ad[FPGA_V3_LUTW_MAX], ng[FPGA_V3_LUTW_MAX];
        uint16_t sum = 0, ref = 0;
        for (uint32_t i = 0; i < FPGA_V3_LUTW_MAX; i++) {
            ch[i] = (uint8_t)(i & 1u); ad[i] = (uint16_t)((i * 7u) & 0x1FFu); ng[i] = (uint16_t)((i * 131u) & 0x3FFFu);
            ref = (uint16_t)(ref + fpga_tdccal_chk(ch[i], ad[i], ng[i]));
        }
        V3OK((fpga_v3_build_lutw(s_m, 9u, ch, ad, ng, 300u, &sum) == FPGA_V3_LUTW_MAX));
        V3OK((sum == ref) && (fpga_v3_blk_ok(s_m) == 0x0Fu) && (s_m[FPGA_V3_BLK] == FPGA_V3_LUTW_PER_BLK));
        const uint8_t *e = s_m + 3u * FPGA_V3_BLK + 1u + 3u * 83u;       /* posledni polozka (i = 251) */
        V3OK((e[0] == (uint8_t)ad[251]) && (e[2] == (uint8_t)ng[251]) &&
              (e[1] == (uint8_t)(((ch[251] & 1u) << 7) | (((ad[251] >> 8) & 1u) << 6) | ((ng[251] >> 8) & 0x3Fu))));
    }

    /* 4) emulator -> parser -> okna (K = 10 segmentu = 0,25 s), bez sumu: kmitocet presne */
    fpga_v3_emu_init(&s_e, 10e6, 1234.5, 0.0, 1u);
    fpga_v3_grp_init(&s_g, 10u);
    nw = st_run(100u, -1, 10e6, &r2, &rl, &gaps);
    V3OK((nw >= 8u) && (r2 < 1e-11) && (rl < 1e-11) && (gaps == 0u) && (s_nlsq == nw));

    /* 5) nesoudelny kmitocet + sum znacky 50 ps */
    fpga_v3_emu_init(&s_e, 12345678.9, 0.0, 50.0, 2u);
    fpga_v3_grp_init(&s_g, 10u);
    nw = st_run(100u, -1, 12345678.9, &r2, &rl, &gaps);
    V3OK((nw >= 8u) && (r2 < 2e-9) && (rl < 2e-9) && (gaps == 0u));

    /* 6) vadny blok 2 v ramci c. 3: zaznamy bloku 2-3 se nepotvrdi a prijdou znovu -> zadna mezera, okna dal */
    fpga_v3_emu_init(&s_e, 5e6, 0.0, 0.0, 3u);
    fpga_v3_grp_init(&s_g, 10u);
    nw = st_run(100u, 3, 5e6, &r2, &rl, &gaps);
    V3OK((nw >= 8u) && (gaps == 0u) && (r2 < 1e-11) && (rl < 1e-11) && (s_nbad == 0u));   /* vadny zaznam se nesmi vzit */

    /* 6b) blok ze STARSIHO ramce (platne CRC, znacka SEQ nesedi) se nesmi vzit + ACK za hlavu kruhu se ignoruje */
    fpga_v3_emu_init(&s_e, 1e6, 0.0, 0.0, 6u);
    for (uint32_t s = 0; s < 12u; s++) fpga_v3_emu_segment(&s_e);
    fpga_v3_emu_frame(&s_e, s_m);                                   /* starsi ramec */
    fpga_v3_emu_frame(&s_e, s_f);                                   /* novy (SEQ + 1), stejne zaznamy */
    memcpy(s_f + 2u * FPGA_V3_BLK, s_m + 2u * FPGA_V3_BLK, FPGA_V3_BLK);
    {
        fpga_v3_hdr_t h; uint32_t n_ok;
        V3OK((fpga_v3_parse(s_f, &h, s_rec, &n_ok) == FPGA_V3_OK) && (h.blk_ok == 0x0Fu) && (n_ok == 8u));
    }
    {
        const uint32_t before = s_e.rec_ack;
        fpga_v3_build_ack(s_m, 1u, s_e.rec_head + 5u);
        fpga_v3_emu_rx(&s_e, s_m);
        V3OK((s_e.rec_ack == before));
        fpga_v3_build_ack(s_m, 2u, s_e.rec_head);
        fpga_v3_emu_rx(&s_e, s_m);
        V3OK((s_e.rec_ack == s_e.rec_head));
    }

    /* 7) preteceni kruhu: bez ACK > 64 segmentu -> priznak a REC_FIRST skoci */
    fpga_v3_emu_init(&s_e, 1e6, 0.0, 0.0, 4u);
    for (uint32_t s = 0; s < 70u; s++) fpga_v3_emu_segment(&s_e);
    fpga_v3_emu_frame(&s_e, s_f);
    {
        fpga_v3_hdr_t h; uint32_t n_ok;
        V3OK((fpga_v3_parse(s_f, &h, s_rec, &n_ok) == FPGA_V3_OK) && (h.flags & FPGA_V3_F_RINGOVF) &&
              (h.rec_first == 140u - FPGA_V3_EMU_RING) && (n_ok == FPGA_V3_REC_MAX));
    }

    /* 8) rozbaleni T0 pres 2^48 (171,8 s): zacit tesne pred pretecenim a projet pres nej */
    fpga_v3_emu_init(&s_e, 1e3, 0.0, 0.0, 5u);
    s_e.seg_idx = 6860u;                                            /* 2^48 / 4,096e10 = 6872 segmentu */
    s_e.x_next[0] = MASK40 - 200u;                                  /* a X0 pres 2^40 zaroven */
    fpga_v3_grp_init(&s_g, 10u);
    nw = st_run(60u, -1, 1e3, &r2, &rl, &gaps);
    V3OK((nw >= 3u) && (r2 < 1e-9) && (rl < 1e-9));

    /* 9) nizky kmitocet 2 Hz (perioda 0,5 s = 20 segmentu, 1 hrana / 20 segmentu): okno se prodlouzi do pristi
     * hrany -> 1 perioda na okno, kmitocet presne; 0,3 Hz (perioda 3,3 s > 2,5 s) = ztrata signalu, zadne okno */
    fpga_v3_emu_init(&s_e, 2.0, 0.0, 0.0, 7u);
    fpga_v3_grp_init(&s_g, 10u);
    nw = st_run(240u, -1, 2.0, &r2, &rl, &gaps);
    V3OK((nw >= 9u) && (r2 < 1e-11) && (gaps == 0u));
    fpga_v3_emu_init(&s_e, 0.3, 0.0, 0.0, 8u);
    fpga_v3_grp_init(&s_g, 10u);
    nw = st_run(400u, -1, 0.3, &r2, &rl, &gaps);
    V3OK((nw == 0u));

    /* 10) prijem se zpozdenym ACK (FPGA sklada ramec pred zpracovanim ACK z teze transakce): kazdy zaznam prave jednou,
     *     prekryv = duplicity, zadna mezera; pak reset emulatoru (REC od 0) = resynchronizace */
    {
        static fpga_v3_link_t L;
        fpga_v3_link_init(&L);
        fpga_v3_emu_init(&s_e, 1e6, 0.0, 0.0, 9u);
        fpga_v3_emu_frame(&s_e, s_f);                                 /* ramec pripraveny pred prvni transakci */
        uint32_t got = 0u, bad = 0u;
        for (uint32_t t = 0; t < 40u; t++) {
            if (t == 30u) { fpga_v3_emu_init(&s_e, 1e6, 0.0, 0.0, 10u); fpga_v3_emu_frame(&s_e, s_f); }   /* reset FPGA */
            fpga_v3_hdr_t h; uint32_t n_ok = 0, i0 = 0; int gap = 0;
            if (fpga_v3_parse(s_f, &h, s_rec, &n_ok) != FPGA_V3_OK) { bad++; break; }
            const uint32_t n = fpga_v3_link_take(&L, &h, n_ok, &i0, &gap);
            for (uint32_t i = 0; i < n; i++) if (s_rec[i0 + i].seg_idx != (uint16_t)((h.rec_first + i0 + i) >> 1)) bad++;
            got += n;
            fpga_v3_build_ack(s_m, t, L.expect);                          /* MOSI teto transakce ... */
            for (int q = 0; q < 3; q++) fpga_v3_emu_segment(&s_e);
            fpga_v3_emu_frame(&s_e, s_f);                                 /* ... FPGA slozi dalsi ramec PRED ACK ... */
            fpga_v3_emu_rx(&s_e, s_m);                                    /* ... a ACK zpracuje az pak */
        }
        V3OK((bad == 0u) && (L.dups > 0u) && (L.gaps == 0u) && (L.resync == 1u) && (got >= 40u));
    }

    /* 11) jednorazove deje: N = 2 -> obe hrany presne; interval A->B pres hranici segmentu; rozbaleni T0 pres 2^48 */
    {
        static fpga_v3_evu_t u; static fpga_v3_ab_t ab;
        fpga_v3_ev_t e[4]; int64_t dt[2];
        fpga_v3_evu_init(&u); fpga_v3_ab_init(&ab, 1000000000000ull);
        const fpga_v3_rec_t ra = { 5u, 0u, 0u, 2u, 100u, MASK48 - 500u, 7u, 1000u, }, rb = { 6u, 1u, 0u, 1u, 40u, 300u, 0u, 0u, };
        uint32_t na = fpga_v3_ev_from_rec(&u, &ra, 100u, 0u, e);
        V3OK((na == 2u) && (e[0].kind == FPGA_V3_EV_EDGE) && (e[1].x == 107u) && (e[1].t_u == MASK48 + 500u));
        uint32_t nb = fpga_v3_ev_from_rec(&u, &rb, 100u, 25u, &e[2]);   /* B o segment pozdeji, T0 po preteceni 2^48 */
        V3OK((nb == 1u) && (fpga_v3_ab_feed(&ab, e, 2u, dt, 2u) == 0u) && ab.armed);
        V3OK((fpga_v3_ab_feed(&ab, &e[2], 1u, dt, 2u) == 1u) && (dt[0] == (int64_t)(MASK48 + 1u + 300u - (MASK48 - 500u))));
        fpga_v3_rec_t rp = rb; rp.t0 = 100u;                             /* B po 200 s pauze: T0 pretekl jednou navic */
        V3OK((fpga_v3_ev_from_rec(&u, &rp, 100u, 25u + 200000u, e) == 1u) && (e[0].t_u == (MASK48 + 1u) * 2u + 100u));
    }

    /* 12) v0.40.0 uzka hlavicka (FW 0x0507): citace tesne pod 2^16 -> rozbaleni SEQ, REC_HEAD, REC_FIRST a ACK (FPGA
     *     bere jen dolnich 16 b) pres preteceni; kazdy zaznam prave jednou, bez mezer; pak restart FPGA = resynchronizace */
    {
        static fpga_v3_link_t L; static fpga_v3_wrap_t W;
        fpga_v3_link_init(&L); fpga_v3_wrap_init(&W);
        fpga_v3_emu_init(&s_e, 1e6, 0.0, 0.0, 11u);
        s_e.narrow = 1u; s_e.seq = 0xFFF0u; s_e.rec_head = s_e.rec_ack = 0xFFE0u; s_e.seg_idx = 0x7FF0u;
        fpga_v3_emu_frame(&s_e, s_f);
        uint32_t got = 0u, bad = 0u, seq_max = 0u, head_max = 0u;
        for (uint32_t t = 0; t < 60u; t++) {
            if (t == 45u) { fpga_v3_emu_init(&s_e, 1e6, 0.0, 0.0, 12u); s_e.narrow = 1u; fpga_v3_emu_frame(&s_e, s_f); }
            fpga_v3_hdr_t h; uint32_t n_ok = 0, i0 = 0; int gap = 0;
            if (fpga_v3_parse(s_f, &h, s_rec, &n_ok) != FPGA_V3_OK || !h.narrow) { bad++; break; }
            fpga_v3_unwrap(&W, &h);
            if (h.seq != s_e.seq || (s_e.rec_head && h.rec_head != s_e.rec_head - 1u)) bad++;   /* proti 32b citacum emulatoru */
            if (h.flags & FPGA_V3_F_RINGOVF) bad++;   /* ACK musi kruh drzet (bez nej REC_FIRST posouva preteceni a mezera neni) */
            if (t < 45u) { if (h.seq > seq_max) seq_max = h.seq; if (h.rec_head > head_max) head_max = h.rec_head; }
            if ((uint32_t)h.ring_fill != W.head - h.rec_first) bad++;      /* W.head = pocet (i prazdny kruh) */
            const uint32_t n = fpga_v3_link_take(&L, &h, n_ok, &i0, &gap);
            for (uint32_t i = 0; i < n; i++) if (s_rec[i0 + i].seg_idx != (uint16_t)((h.rec_first + i0 + i) >> 1)) bad++;
            got += n;
            fpga_v3_build_ack(s_m, t, L.expect ^ 0x5A5A0000u);        /* FPGA bere jen dolnich 16 b: horni smeti nevadi */
            for (int q = 0; q < 3; q++) fpga_v3_emu_segment(&s_e);
            fpga_v3_emu_frame(&s_e, s_f);
            fpga_v3_emu_rx(&s_e, s_m);
        }
        V3OK((bad == 0u) && (L.gaps == 0u) && (L.resync == 1u) && (W.restarts == 1u) && (got >= 60u) &&
             (seq_max > 0xFFFFu) && (head_max > 0xFFFFu));
        /* BUB/OVL u uzke hlavicky = priznaky nejnovejsiho zaznamu kanalu v ramci (bajty 112..115 jsou 0) */
        fpga_v3_build_ack(s_m, 99u, s_e.rec_head);                    /* vse potvrdit -> ramec nese jen 6 novych */
        fpga_v3_emu_rx(&s_e, s_m);
        for (int q = 0; q < 3; q++) fpga_v3_emu_segment(&s_e);
        fpga_v3_emu_frame(&s_e, s_f);                                 /* 6 zaznamu: A B A B A B v bloku 1 */
        uint8_t *b1 = s_f + FPGA_V3_BLK + 2u;
        b1[2u * FPGA_V3_REC_LEN + 3u] |= FPGA_V3_R_BUBBLE;            /* zaznam 2 (A) bublina, zaznam 4 (A) ne */
        b1[1u * FPGA_V3_REC_LEN + 3u] |= FPGA_V3_R_CS;                /* zaznam 1 (B) znacky pri CS, nejnovejsi B (5) ne */
        b1[4u * FPGA_V3_REC_LEN + 3u] |= FPGA_V3_R_CS;                /* zaznam 4 (nejnovejsi A) znacky pri CS */
        s_f[112] = 0x55u;                                             /* bajt hlavicky se u uzke nesmi pouzit */
        s_f[6] = s_f[7] = s_f[34] = s_f[35] = s_f[38] = s_f[39] = 0xA5u;   /* horni bajty 16b poli se nectou */
        fpga_v3_blk_seal(s_f); fpga_v3_blk_seal(s_f + FPGA_V3_BLK);
        fpga_v3_hdr_t h; uint32_t n_ok = 0;
        V3OK((fpga_v3_parse(s_f, &h, s_rec, &n_ok) == FPGA_V3_OK) && (n_ok == 6u) && (s_rec[5].ch == 1u));
        V3OK((h.bub[0] == 0u) && (h.bub[1] == 0u) && (h.ovl[0] == 1u) && (h.ovl[1] == 0u));
        V3OK((h.seq <= 0xFFFFu) && (h.rec_head <= 0xFFFFu) && (h.rec_first <= 0xFFFFu));
    }

    /* 13) v0.40.0 historie 1PPS ve STM (uzka hlavicka nenese predchozi hranu): PSEQ pres 2^16, opakovany ramec teze
     *     hrany, vynechana hrana (STM ji nevidel) -> predchozi neplatna */
    {
        static fpga_v3_wrap_t W;
        static const uint16_t ps[6] = { 0xFFFEu, 0xFFFFu, 0xFFFFu, 0x0000u, 0x0002u, 0x0003u };
        static const uint32_t pseq[6] = { 0xFFFEu, 0xFFFFu, 0xFFFFu, 0x10000u, 0x10002u, 0x10003u };
        static const uint64_t pts[6] = { 7u, 1007u, 1007u, 2007u, 4007u, 5007u };
        static const uint64_t prev[6] = { 0u, 7u, 7u, 1007u, 0u, 4007u };       /* 0 = predchozi neplatna */
        fpga_v3_wrap_init(&W);
        for (uint32_t k = 0; k < 6u; k++) {
            fpga_v3_hdr_t h;
            memset(&h, 0, sizeof h);
            h.narrow = 1u; h.seq = k + 1u;
            h.pps[0].pseq = ps[k]; h.pps[0].pts = pts[k]; h.pps[0].pflags = 3u;
            fpga_v3_unwrap(&W, &h);
            V3OK((h.pps[0].pseq == pseq[k]) && (h.pps[1].pts == prev[k]) && ((h.pps[1].pflags != 0u) == (prev[k] != 0u)));
            V3OK((prev[k] == 0u) || (h.pps[1].pseq == h.pps[0].pseq - 1u));
        }
    }

    /* 14) v0.40.8 citace zahozenych pulzu na CS (FW 0x0508, CAPS bit23, bajty 132..137): bez CAPS bitu se bajty nectou */
    {
        fpga_v3_emu_init(&s_e, 10e6, 0.0, 0.0, 3u);
        fpga_v3_emu_frame(&s_e, s_f);
        for (int i = 0; i < 6; i++) s_f[132 + i] = (uint8_t)(10 + i);   /* 10, 11, 12, 13, 14 + filtr 15 */
        fpga_v3_blk_seal(s_f);
        fpga_v3_hdr_t h; uint32_t n_ok = 0;
        V3OK((fpga_v3_parse(s_f, &h, s_rec, &n_ok) == FPGA_V3_OK) && (h.cs_filt == 0u) && (h.cs_gl[0] == 0u) && (h.cs_gl[4] == 0u));
        s_f[26] |= (uint8_t)(FPGA_V3_CAP_CSDIAG >> 16);                 /* CAPS bit23 = bajt 26 bit 7 */
        fpga_v3_blk_seal(s_f);
        V3OK((fpga_v3_parse(s_f, &h, s_rec, &n_ok) == FPGA_V3_OK) && (h.cs_gl[0] == 10u) && (h.cs_gl[1] == 11u) && (h.cs_gl[2] == 12u) &&
             (h.cs_gl[3] == 13u) && (h.cs_gl[4] == 14u) && (h.cs_filt == 15u) && (h.caps & FPGA_V3_CAP_CSDIAG));
    }

    /* 15) v0.42.4 teziste segmentu proti primce (fpga_v3_seg_resid): sum 100 ps -> |r| < 1 ns, ST + 2^34 -> > 1 ns,
     * nesouvisly segment / jiny kanal -> nelze */
    {
        fpga_v3_emu_init(&s_e, 10e6, 10e6, 100.0, 13u);
        fpga_v3_emu_segment(&s_e); fpga_v3_emu_segment(&s_e); fpga_v3_emu_segment(&s_e);
        fpga_v3_rec_t a = s_e.ring[0], b = s_e.ring[2], c = s_e.ring[4];
        double r = 0.0; int64_t sd = 0;
        V3OK(fpga_v3_seg_resid(&a, &b, s_e.clk_mhz, &r, &sd) == 1 && fabs(r) < 1000.0 && sd == 0);
        V3OK(fpga_v3_seg_resid(&s_e.ring[1], &s_e.ring[3], s_e.clk_mhz, &r, &sd) == 1 && fabs(r) < 1000.0);   /* kanal B */
        a.st += 1ull << 34;
        V3OK(fpga_v3_seg_resid(&a, &b, s_e.clk_mhz, &r, &sd) == 1 && fabs(r) > 1000.0);
        V3OK(fpga_v3_seg_resid(&a, &c, s_e.clk_mhz, &r, &sd) == 0);                  /* preskoceny segment */
        V3OK(fpga_v3_seg_resid(&a, &s_e.ring[3], s_e.clk_mhz, &r, &sd) == 0);        /* jiny kanal */
    }

    /* 16) v0.42.5 pojistka okna: ST segmentu 9 (uprostred okna 8..11) kanalu A + 2^30 (~24 ns na teziste) -> prave to
     * okno seg_bad, ostatni okna A i vsechna B ne; od v0.42.7 je vadny segment z LSQ vyrazen -> dvoubodove
     * vadneho okna do 1e-6, LSQ do 1e-9 */
    {
        fpga_v3_emu_init(&s_e, 1e6, 1e6, 50.0, 14u);
        fpga_v3_grp_init(&s_g, 4u);
        uint32_t nbad[2] = { 0u, 0u }, nwin[2] = { 0u, 0u };
        double maxok = 0.0, rel2 = 1.0, rell = 1.0;
        for (uint32_t s = 0; s < 40u; s++) {
            fpga_v3_emu_segment(&s_e);
            for (uint32_t i = s_e.rec_head - 2u; i < s_e.rec_head; i++) {
                fpga_v3_rec_t q = s_e.ring[i % FPGA_V3_EMU_RING];
                if (q.seg_idx == 9u && (q.ch & 1u) == 0u) q.st += 1ull << 30;
                fpga_v3_win_t w;
                if (!fpga_v3_grp_feed(&s_g, &q, s_e.clk_mhz, &w)) continue;
                nwin[w.ch & 1u]++;
                if (w.seg_bad) {
                    nbad[w.ch & 1u]++; rel2 = fabs(w.f_2pt_hz / 1e6 - 1.0);
                    if (w.lsq_ok) rell = fabs(w.f_lsq_hz / 1e6 - 1.0);
                }
                else if (w.seg_max_ps > maxok) maxok = w.seg_max_ps;
            }
        }
        V3OK(nwin[0] >= 8u && nwin[1] >= 8u && nbad[0] == 1u && nbad[1] == 0u && maxok < 1000.0 && rel2 < 1e-6 &&
             rell < 1e-9);
    }

    /* 17) v0.42.6 presne meze SX (fpga_v3_seg_sx_ok): zdravy segment v mezich, SX - 1 a SX + 2^32 mimo, mezera 3 znacek
     * (maska pri CS) v mezich; v okne SX - 5 v segmentu 9 kanalu B (teziste ~0,2 ns, pod mezi 2 ns) -> prave to okno B */
    {
        fpga_v3_emu_init(&s_e, 10e6, 10e6, 100.0, 15u);
        fpga_v3_emu_segment(&s_e); fpga_v3_emu_segment(&s_e);
        fpga_v3_rec_t a = s_e.ring[0];
        const fpga_v3_rec_t b = s_e.ring[2];
        V3OK(fpga_v3_seg_sx_ok(&a, &b) == 1);
        a.sx -= 1u;              V3OK(fpga_v3_seg_sx_ok(&a, &b) == 0);
        a.sx += 1u + (1ull << 32); V3OK(fpga_v3_seg_sx_ok(&a, &b) == 0);
        const uint64_t n = a.n - 3u;
        a.n = (uint32_t)n; a.sx = n * (n - 1u) / 2u + 3u * (n / 2u);
        V3OK(fpga_v3_seg_sx_ok(&a, &b) == 1);

        fpga_v3_emu_init(&s_e, 1e6, 1e6, 50.0, 16u);
        fpga_v3_grp_init(&s_g, 4u);
        uint32_t nbad[2] = { 0u, 0u }, nwin[2] = { 0u, 0u };
        double maxbad = 0.0;
        for (uint32_t s = 0; s < 40u; s++) {
            fpga_v3_emu_segment(&s_e);
            for (uint32_t i = s_e.rec_head - 2u; i < s_e.rec_head; i++) {
                fpga_v3_rec_t q = s_e.ring[i % FPGA_V3_EMU_RING];
                if (q.seg_idx == 9u && (q.ch & 1u) == 1u) q.sx -= 5u;
                fpga_v3_win_t w;
                if (!fpga_v3_grp_feed(&s_g, &q, s_e.clk_mhz, &w)) continue;
                nwin[w.ch & 1u]++;
                if (w.seg_bad) { nbad[w.ch & 1u]++; maxbad = w.seg_max_ps; }
            }
        }
        V3OK(nwin[0] >= 8u && nwin[1] >= 8u && nbad[0] == 0u && nbad[1] == 1u && maxbad < FPGA_V3_SEG_BAD_PS);
    }

    /* 18) v0.42.7 X0 o 64*k hran: X0 kanalu B na hranici oken (segment 8) + 128 -> opraveno, dalsi kontroly ciste,
     * kmitocty vsech oken B do 1e-6; segment 2 + 64 hned po startu (predchozi kontrola neni cista), segment 21 + 37
     * (neni nasobek 64) -> jen pocitano, bez opravy; segment 31 + 64 po vadnem SX segmentu 30 -> od v0.42.10 opraveno
     * (perioda ST/SX se nepouziva, na desce ji kazilo vadne SX) */
    {
        fpga_v3_emu_init(&s_e, 1e6, 1e6, 50.0, 17u);
        fpga_v3_grp_init(&s_g, 4u);
        uint32_t nwin = 0u, o8 = 0u, o12 = 1u, f12 = 0u;
        int32_t  d12 = 0;
        double maxrel = 0.0;
        for (uint32_t s = 0; s < 40u; s++) {
            fpga_v3_emu_segment(&s_e);
            for (uint32_t i = s_e.rec_head - 2u; i < s_e.rec_head; i++) {
                fpga_v3_rec_t q = s_e.ring[i % FPGA_V3_EMU_RING];
                if ((q.ch & 1u) == 1u && q.seg_idx == 2u)  q.x0 = (q.x0 + 64u) & MASK40;
                if ((q.ch & 1u) == 1u && q.seg_idx == 8u)  q.x0 = (q.x0 + 128u) & MASK40;
                if ((q.ch & 1u) == 1u && q.seg_idx == 21u) q.x0 = (q.x0 + 37u) & MASK40;
                if ((q.ch & 1u) == 1u && q.seg_idx == 30u) q.sx += q.sx / 100u + 1u;     /* ST/SX vedle, */
                if ((q.ch & 1u) == 1u && q.seg_idx == 31u) q.x0 = (q.x0 + 64u) & MASK40;  /* X0 presto opravit */
                fpga_v3_win_t w;
                if (!fpga_v3_grp_feed(&s_g, &q, s_e.clk_mhz, &w) || (w.ch & 1u) != 1u || w.seg_first >= 16u) continue;
                nwin++;
                const double f = (w.lsq_ok && !w.seg_bad) ? w.f_lsq_hz : w.f_2pt_hz;
                if (fabs(f / 1e6 - 1.0) > maxrel) maxrel = fabs(f / 1e6 - 1.0);
            }
            if (s == 8u) o8 = s_g.x0_odd[1];
            if (s == 12u) { o12 = s_g.x0_odd[1]; f12 = s_g.x0_fix[1]; d12 = s_g.x0_dev[1]; }
        }
        V3OK(nwin >= 3u && maxrel < 1e-6 && f12 == 1u && d12 == 128 && s_g.x0_fix[1] == 2u && s_g.x0_dev[1] == 64 && o8 >= 1u && o12 == o8 &&
             s_g.x0_odd[1] > o12 && s_g.x0_fix[0] == 0u && s_g.x0_odd[0] == 0u);
    }

    /* 19) v0.42.11 oprava SX (sx_repair) a mez 800 ps, kanal B, okna po 4 segmentech:
     *   seg 9  SX - 3*2^32               -> opraveno z mezi, okno dobre (LSQ do 1e-9)
     *   seg 13 X0 - 128, SX + (N-1)*128  -> X0 opraveno, SX podle X0, okno dobre
     *   seg 16 SX - 2^32 (prvni segment okna) -> opraveno vcetne teziste prvniho segmentu (xc0)
     *   seg 20 SX - 5*64 (spodni bity)   -> neopravitelne, okno vadne
     *   seg 24 SX - 2^32 a ST + 2^30     -> kandidat v mezich, ale teziste mimo -> okno vadne
     *   seg 28 ST + 1,2 ns na teziste    -> pod drivejsi mezi 2 ns, nad 800 ps -> okno vadne */
    {
        fpga_v3_emu_init(&s_e, 1e6, 1e6, 50.0, 18u);
        fpga_v3_grp_init(&s_g, 4u);
        uint32_t nbad[2] = { 0u, 0u }, badmask = 0u, nwin = 0u;
        double maxrel = 0.0, xc16 = 99.0;
        for (uint32_t s = 0; s < 40u; s++) {
            fpga_v3_emu_segment(&s_e);
            for (uint32_t i = s_e.rec_head - 2u; i < s_e.rec_head; i++) {
                fpga_v3_rec_t q = s_e.ring[i % FPGA_V3_EMU_RING];
                if ((q.ch & 1u) == 1u) {
                    const uint64_t n1 = q.n - 1u;
                    if (q.seg_idx == 9u)  q.sx = (q.sx - (3ull << 32)) & MASK48;
                    if (q.seg_idx == 13u) { q.x0 = (q.x0 - 128u) & MASK40; q.sx = (q.sx + n1 * 128u) & MASK48; }
                    if (q.seg_idx == 16u) q.sx = (q.sx - (1ull << 32)) & MASK48;
                    if (q.seg_idx == 20u) q.sx -= 5u * 64u;
                    if (q.seg_idx == 24u) { q.sx = (q.sx - (1ull << 32)) & MASK48; q.st += 1ull << 30; }
                    if (q.seg_idx == 28u) q.st += 49152000u;
                }
                fpga_v3_win_t w;
                if (!fpga_v3_grp_feed(&s_g, &q, s_e.clk_mhz, &w) || w.seg_first >= 36u) continue;
                if (w.seg_bad) { nbad[w.ch & 1u]++; if (w.ch & 1u) badmask |= 1u << (w.seg_first / 4u); continue; }
                if ((w.ch & 1u) == 0u) continue;
                nwin++;
                if (!w.lsq_ok || fabs(w.f_lsq_hz / 1e6 - 1.0) > maxrel) maxrel = w.lsq_ok ? fabs(w.f_lsq_hz / 1e6 - 1.0) : 1.0;
                if (w.seg_first == 16u) xc16 = w.xc0 - (double)w.x_first - 0.5 * (double)(w.n0 - 1u);
            }
        }
        V3OK(nbad[0] == 0u && nbad[1] == 3u && badmask == ((1u << 5) | (1u << 6) | (1u << 7)) && nwin >= 5u &&
             maxrel < 1e-9 && fabs(xc16) < 1.0 && s_g.sx_rep[1] == 3u && s_g.sx_rep_x0[1] == 1u && s_g.x0_fix[1] == 1u &&
             s_g.sx_rep[0] == 0u);
    }

    /* 20) v0.42.12 okna na spolecne mrizce a parovani A/B (K = 4, 40 segmentu): kanal B neplatny v segmentech 0..2
     * (zacne pozdeji) a 20 (okno B 16..19 se rozpadne prave zaznamem, na ktery ceka okno A 16), kanal A neplatny
     * v segmentu 5. Primarni A: okna A 0, 8..32 -> pary 8, 12, 24, 28, 32, bez B 0, 16, 20; primarni B: okna B 4, 8,
     * 12, 24..32 -> 5 paru, bez A 4. Vsechna okna zacinaji na nasobku K, okno bez druheho odejde hned se zaznamem
     * segmentu, kterym skoncilo. */
    for (uint8_t prim = 0u; prim < 2u; prim++) {
        static fpga_v3_pair_t P;
        static fpga_v3_pair_out_t O[2];
        fpga_v3_emu_init(&s_e, 1e6, 1e6, 50.0, 19u);
        fpga_v3_grp_init(&s_g, 4u);
        fpga_v3_pair_init(&P);
        uint32_t npair = 0u, nalone = 0u, bad = 0u, last = 0u, have = 0u;
        for (uint32_t s = 0; s < 40u; s++) {
            fpga_v3_emu_segment(&s_e);
            for (uint32_t i = s_e.rec_head - 2u; i < s_e.rec_head; i++) {
                fpga_v3_rec_t q = s_e.ring[i % FPGA_V3_EMU_RING];
                if (((q.ch & 1u) == 1u && (q.seg_idx <= 2u || q.seg_idx == 20u)) || ((q.ch & 1u) == 0u && q.seg_idx == 5u))
                    q.flags |= FPGA_V3_R_INVALID;
                fpga_v3_win_t w;
                const int got = fpga_v3_grp_feed(&s_g, &q, s_e.clk_mhz, &w);
                const uint32_t n = fpga_v3_pair_feed(&P, &s_g, prim, &q, got ? &w : NULL, O);
                for (uint32_t k = 0; k < n; k++) {
                    const fpga_v3_pair_out_t *o = &O[k];
                    if ((o->a.ch & 1u) != prim || o->a.seg_first % 4u != 0u || o->a.nseg != 4u) bad++;
                    if (have && o->a.seg_first <= last) bad++;                   /* poradi */
                    last = o->a.seg_first; have = 1u;
                    if (o->has_b) { npair++; if (o->b.seg_first != o->a.seg_first || (o->b.ch & 1u) == prim) bad++; }
                    else { nalone++; if (q.seg_idx != o->a.seg_first + o->a.nseg) bad++; }
                }
            }
        }
        V3OK(npair == 5u && nalone == (prim ? 1u : 3u) && bad == 0u);
    }

    /* 21) v0.42.12 parovani pri ruznych kmitoctech: A 1 MHz, B 10 Hz (perioda 4 segmenty), K = 10, 200 segmentu.
     * Okno B zacina az prvni hranou v bunce (seg_first != seg_first A), paruje se podle bunky -> pary vsech oken A
     * krome prvniho (B jeste nema okno), kazdy par ze stejne bunky. */
    {
        static fpga_v3_pair_t P;
        static fpga_v3_pair_out_t O[2];
        fpga_v3_emu_init(&s_e, 1e6, 10.0, 50.0, 20u);
        fpga_v3_grp_init(&s_g, 10u);
        fpga_v3_pair_init(&P);
        uint32_t nwa = 0u, npair = 0u, bad = 0u, nsf = 0u;
        for (uint32_t s = 0; s < 200u; s++) {
            fpga_v3_emu_segment(&s_e);
            for (uint32_t i = s_e.rec_head - 2u; i < s_e.rec_head; i++) {
                const fpga_v3_rec_t q = s_e.ring[i % FPGA_V3_EMU_RING];
                fpga_v3_win_t w;
                const int got = fpga_v3_grp_feed(&s_g, &q, s_e.clk_mhz, &w);
                const uint32_t n = fpga_v3_pair_feed(&P, &s_g, 0u, &q, got ? &w : NULL, O);
                for (uint32_t k = 0; k < n; k++) {
                    nwa++;
                    if (!O[k].has_b) continue;
                    npair++;
                    if (O[k].b.cell != O[k].a.cell || fabs(O[k].b.f_2pt_hz / 10.0 - 1.0) > 1e-6) bad++;
                    if (O[k].b.seg_first != O[k].a.seg_first) nsf++;
                }
            }
        }
        V3OK(nwa >= 18u && npair + 1u >= nwa && bad == 0u && nsf > 0u);
    }

    /* 22) v0.42.12 pomaly druhy kanal: B 1 Hz (okno pres 4 bunky), K = 10, 400 segmentu -> okna A dostanou posledni
     * uzavrene okno B (jako do v0.42.11), po prvnim okne B vsechna */
    {
        static fpga_v3_pair_t P;
        static fpga_v3_pair_out_t O[2];
        fpga_v3_emu_init(&s_e, 1e6, 1.0, 50.0, 21u);
        fpga_v3_grp_init(&s_g, 10u);
        fpga_v3_pair_init(&P);
        uint32_t nwa = 0u, nb = 0u, bad = 0u, seen_b = 0u, miss = 0u;
        for (uint32_t s = 0; s < 400u; s++) {
            fpga_v3_emu_segment(&s_e);
            for (uint32_t i = s_e.rec_head - 2u; i < s_e.rec_head; i++) {
                const fpga_v3_rec_t q = s_e.ring[i % FPGA_V3_EMU_RING];
                fpga_v3_win_t w;
                const int got = fpga_v3_grp_feed(&s_g, &q, s_e.clk_mhz, &w);
                if (got && (w.ch & 1u) == 1u) seen_b = 1u;
                const uint32_t n = fpga_v3_pair_feed(&P, &s_g, 0u, &q, got ? &w : NULL, O);
                for (uint32_t k = 0; k < n; k++) {
                    nwa++;
                    if (O[k].has_b) { nb++; if (fabs(O[k].b.f_2pt_hz - 1.0) > 1e-6) bad++; }
                    else if (seen_b) miss++;
                }
            }
        }
        V3OK(nwa >= 35u && nb >= 25u && bad == 0u && miss <= 1u);
    }

    /* 23) v0.42.12 mrizka pres pretoceni SEG_IDX (16 b): K = 10 (65536 neni nasobek) -> vsechna okna 10 segmentu */
    {
        fpga_v3_emu_init(&s_e, 1e6, 1e6, 50.0, 22u);
        s_e.seg_idx = 65530u;
        fpga_v3_grp_init(&s_g, 10u);
        uint32_t nwr = 0u, bad = 0u;
        for (uint32_t s = 0; s < 45u; s++) {
            fpga_v3_emu_segment(&s_e);
            for (uint32_t i = s_e.rec_head - 2u; i < s_e.rec_head; i++) {
                fpga_v3_win_t w;
                if (!fpga_v3_grp_feed(&s_g, &s_e.ring[i % FPGA_V3_EMU_RING], s_e.clk_mhz, &w)) continue;
                nwr++;
                if (w.nseg != 10u) bad++;
            }
        }
        V3OK(nwr == 8u && bad == 0u);
    }

    return ok;
}
