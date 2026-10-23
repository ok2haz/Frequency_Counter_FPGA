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
    h->seq = (uint32_t)rdn(f + 4, 4);
    h->frame_len = (uint16_t)rdn(f + 8, 2); h->nblk = f[10];
    if (h->frame_len != FPGA_V3_LEN || h->nblk != FPGA_V3_NBLK) return FPGA_V3_E_LEN;
    h->build_time = (uint32_t)rdn(f + 12, 4); h->build_git = (uint32_t)rdn(f + 16, 4);
    h->fw_version = (uint16_t)rdn(f + 20, 2); h->clk_mhz = f[22]; h->seg_len_ms = f[23];
    h->caps = (uint32_t)rdn(f + 24, 4); h->ts10m = (uint32_t)rdn(f + 28, 4);
    h->rec_head = (uint32_t)rdn(f + 32, 4); h->rec_first = (uint32_t)rdn(f + 36, 4);
    h->nrec = f[40]; h->ring_fill = (uint16_t)rdn(f + 42, 2);
    h->clk_status = f[44]; h->clk_loss = f[45]; h->coh_st = f[46]; h->coh_cnt = f[47];
    h->tdc_status[0] = f[48]; h->tdc_status[1] = f[49]; h->pps_status = f[50]; h->cal_wait = f[51];
    h->rx_crc_err = (uint16_t)rdn(f + 52, 2); h->rx_len_err = (uint16_t)rdn(f + 54, 2);
    h->rx_last = (uint32_t)rdn(f + 56, 4);
    h->lut_cnt = (uint16_t)(rdn(f + 60, 2) & 0x3FFFu); h->lut_sum = (uint16_t)rdn(f + 62, 2);
    h->lut_cnt_b = (uint16_t)(rdn(f + 122, 2) & 0x3FFFu);
    h->deep_n = (uint32_t)rdn(f + 124, 3); h->deep_st = f[128]; h->deep_wsel = f[129];   /* v0.39.0: PSRAM */
    h->deep_win = (uint16_t)rdn(f + 130, 2);
    h->presc[0] = f[64]; h->presc[1] = f[65]; h->cfg_echo = f[66];
    for (int i = 0; i < 2; i++) {
        const uint8_t *q = f + 68 + 16 * i;
        h->pps[i].pseq = (uint32_t)rdn(q, 4); h->pps[i].pts = rdn(q + 4, 6); h->pps[i].pflags = q[10];
        h->pps[i].d = (int8_t)q[11];
        h->pps_coarse[i] = (uint32_t)rdn(f + 100 + 4 * i, 4);
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
    }
    return FPGA_V3_OK;
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
    e->seq++;
    f[0] = FPGA_V3_MAGIC; f[1] = FPGA_V3_VER; f[2] = FPGA_V3_T_DATA;
    f[3] = (uint8_t)((nrec ? FPGA_V3_F_NEWREC : 0u) | (e->ovf ? FPGA_V3_F_RINGOVF : 0u));
    wrn(f + 4, e->seq, 4);
    wrn(f + 8, FPGA_V3_LEN, 2); f[10] = (uint8_t)FPGA_V3_NBLK;
    wrn(f + 20, 0x0500u, 2);                                         /* FW emulatoru */
    f[22] = (uint8_t)e->clk_mhz; f[23] = (uint8_t)e->seg_ms;
    wrn(f + 24, (1ull << 16) | FPGA_V3_CAP_PRESC, 4);                /* CAPS bit16 = v3 zaznamy, bit18 predelicka */
    for (int c = 0; c < 2; c++) {                                    /* v0.38.0: PRESC, sonda /16 za 1 ms */
        f[64 + c] = (uint8_t)(e->p_log2[c] | ((e->p_req[c] != e->p_log2[c]) ? FPGA_V3_PRESC_PEND : 0u));
        const double pr = e->f_hz[c] > 0.0 ? e->f_hz[c] / 16000.0 : 0.0;
        wrn(f + 108 + 2 * c, pr > 65535.0 ? 65535u : (uint32_t)pr, 2);
    }
    wrn(f + 32, e->rec_head ? e->rec_head - 1u : 0u, 4);
    wrn(f + 36, e->rec_ack, 4);
    f[40] = (uint8_t)nrec;
    wrn(f + 42, avail > 0xFFFFu ? 0xFFFFu : avail, 2);
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
        const uint32_t a = (uint32_t)rdn(f + 12, 4);
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

int fpga_v3_grp_feed(fpga_v3_grp_t *g, const fpga_v3_rec_t *r, uint32_t clk_mhz, fpga_v3_win_t *out)
{
    const uint8_t ch = (uint8_t)(r->ch & 1u);
    const uint8_t pl = FPGA_V3_REC_PLOG2(r);           /* v0.38.0: X pocita delene hrany (P = 2^pl) */
    fpga_v3_grp_ch_t *c = &g->c[ch];
    int closed = 0;

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
    if (c->open && r->seg_idx != (uint16_t)(c->seg_last + 1u)) c->open = 0u;
    if (c->open && pl != c->p_log2) c->open = 0u;       /* zmena P: okno pres zmenu meritka X nesmi */
    c->seg_last = r->seg_idx;
    if (r->flags & FPGA_V3_R_INVALID) { c->open = 0u; return 0; }
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
        return 0;
    }
    /* odhad periody pro limit mezery: z tohoto zaznamu (ST/SX), jinak od predchozi hrany v temze okne */
    if (r->n >= 2u && r->sx) c->per_u = (double)r->st / (double)r->sx;
    else if (hb && c->open && x > xb) c->per_u = (double)(t - tb) / (double)(x - xb);
    else c->per_u = 0.0;
    c->empty = 0u;

    if (c->open && c->k >= g->k_target) {            /* tento zaznam zacina dalsi okno -> predchozi uzavrit */
        const double up = fpga_v3_unit_ps(clk_mhz);
        out->ch = ch; out->seg_first = c->seg_first; out->nseg = c->k; out->n_marks = c->n_tot;
        out->edges = (x - c->x_first) << c->p_log2; out->gate_units = t - c->t_first;   /* periody = hrany x P */
        out->f_2pt_hz = (out->gate_units) ? (double)out->edges * 1e12 / ((double)out->gate_units * up) : 0.0;
        const double mx = c->swx / c->sw, mt = c->swt / c->sw;
        const double vxx = c->swxx / c->sw - mx * mx, vxt = c->swxt / c->sw - mx * mt;
        out->lsq_ok = (uint8_t)(c->k >= 2u && vxx > 0.0 && vxt > 0.0);
        out->f_lsq_hz = out->lsq_ok ? (double)(1u << c->p_log2) * 1e12 / ((vxt / vxx) * up) : 0.0;   /* sklon t(x) = P perioda */
        out->flags_or = c->flags_or; out->xc0 = c->xc0 * (double)(1u << c->p_log2); out->tc0 = c->tc0;
        out->x_first = c->x_first << c->p_log2; out->t_first = c->t_first; out->n0 = c->n0; out->p_log2 = c->p_log2;
        closed = 1;
        c->open = 0u;
    }
    if (!c->open) {                                   /* zacatek okna */
        c->open = 1u; c->k = 0u; c->n_tot = 0u; c->seg_first = r->seg_idx; c->p_log2 = pl;
        c->x_first = x; c->t_first = t;
        c->sw = c->swx = c->swt = c->swxx = c->swxt = 0.0; c->flags_or = 0u;
        c->xc0 = (double)x + (double)r->sx / (double)r->n;
        c->n0  = r->n;
        c->tc0 = (double)t + (double)r->st / (double)r->n;
    }
    /* teziste segmentu relativne k prvni znacce okna; vaha = pocet znacek */
    const double w = (double)r->n;
    const double X = (double)(x - c->x_first) + (double)r->sx / w;
    const double T = (double)(t - c->t_first) + (double)r->st / w;
    c->sw += w; c->swx += w * X; c->swt += w * T; c->swxx += w * X * X; c->swxt += w * X * T;
    c->k++; c->n_tot += r->n; c->flags_or |= r->flags;
    return closed;
}

/* ══════════════ selftest ══════════════ */
static fpga_v3_emu_t s_e;
static fpga_v3_grp_t s_g;
static uint8_t       s_f[FPGA_V3_LEN], s_m[FPGA_V3_LEN];
static fpga_v3_rec_t s_rec[FPGA_V3_REC_MAX];

/* Projede nseg segmentu emulatorem: kazde 2 segmenty jeden ramec, parser, okna, ACK (jako STM). Volitelne v ramci
 * c. bad_frame poskodi blok 2. Vraci pocet oken kanalu A a jejich nejvetsi relativni chybu proti f_true;
 * LSQ jen u oken s lsq_ok (jejich pocet v *nlsq). */
static uint32_t s_nlsq;
static uint32_t st_run(uint32_t nseg, int bad_frame, double f_true, double *maxrel_2pt, double *maxrel_lsq,
                       uint32_t *gaps)
{
    uint32_t nwin = 0, fr = 0, expect = s_e.rec_ack;
    *maxrel_2pt = *maxrel_lsq = 0.0; *gaps = 0; s_nlsq = 0;
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
    V3OK((nw >= 8u) && (gaps == 0u) && (r2 < 1e-11) && (rl < 1e-11));   /* i teziste (LSQ): vadny zaznam se nesmi vzit */

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

    return ok;
}
