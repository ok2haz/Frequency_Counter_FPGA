/* fpga_phrec.c -- fazovy zaznam z teziste segmentu regrese (viz fpga_phrec.h). Audit 2026-10-20, etapa E1. */
#include "fpga_phrec.h"
#include <math.h>
#include <string.h>

typedef struct {
    uint8_t  have;               /* je predchozi okno useku */
    uint8_t  win;                /* hradlo (echo phase_status & 7) predchoziho okna */
    uint8_t  chan, clk;
    uint16_t xdiv;               /* audit 4: delitel xm (256 / 4096) useku */
    uint64_t prev_dt_u;          /* dt PREDCHOZIHO okna [T/16384] */
    uint64_t prev_per;           /* periody predchoziho okna */
    uint64_t prev_xm, prev_ym;   /* teziste A predchoziho okna: xm [1/xdiv hrany], ym [1/16 jednotky] od zacatku okna */
    double   f_ref, u_s, x;      /* referencni kmitocet, sekund na jednotku, akumulovana faze [s] */
    double   tau0;
    double   g_res;              /* audit 4: zbytek zaokrouhleni gate_cc_ps [ps] prenaseny do dalsiho okna (Σ zustane presny) */
} phr_state_t;

static phr_state_t s_p;
static float       s_x[FPGA_PHREC_N];   /* optimalizace 5: float (8 kB); |x| << 1 us -> rozliseni < 0,1 ps, ADEV jen z rozdilu */
static uint32_t    s_head, s_n, s_runs, s_fed, s_used;

static void ring_clear(void) { s_head = 0u; s_n = 0u; }
static void ring_push(double v)
{
    s_x[s_head] = (float)v;
    s_head = (s_head + 1u) % FPGA_PHREC_N;
    if (s_n < FPGA_PHREC_N) s_n++;
}
static double ring_at(uint32_t i)          /* i = 0 nejstarsi */
{
    uint32_t idx = (s_head + FPGA_PHREC_N - s_n + i) % FPGA_PHREC_N;
    return (double)s_x[idx];
}

void fpga_phrec_reset(void)
{
    memset(&s_p, 0, sizeof s_p);
    ring_clear();
    s_runs = 0u; s_fed = 0u; s_used = 0u;
}

static void run_break(void)
{
    if (s_p.have || s_n) s_runs++;
    s_p.have = 0u;
    ring_clear();
}

void fpga_phrec_feed(fpga_meas_t *m, uint32_t gap, int ok)
{
    if (m == NULL) return;
    m->cc_ok = 0u; m->f_cc_hz = 0.0; m->gate_cc_ps = 0u;
    s_fed++;
    uint8_t win  = (m->caps & FPGA_CAP_CHAN) ? (uint8_t)(m->phase_status & 7u) : 0xFEu;
    int usable = ok && m->regr_used && m->rg_ok == 3u && (m->caps & FPGA_CAP_DT) && m->dt_units > 0u
                 && m->edge_count > 0u && m->clk_mhz > 0u;
    uint16_t xdiv = m->rg_xdiv ? m->rg_xdiv : 256u;
    if (!usable || gap != 0u || (s_p.have && (win != s_p.win || m->channel_id != s_p.chan || m->clk_mhz != s_p.clk || xdiv != s_p.xdiv))) {
        run_break();
        if (!usable) return;
    }
    s_used++;
    if (!s_p.have) {                                   /* prvni okno useku = fazova kotva */
        s_p.have = 1u; s_p.win = win; s_p.chan = m->channel_id; s_p.clk = m->clk_mhz; s_p.xdiv = xdiv;
        s_p.u_s  = 1.0 / (16384.0 * (double)m->clk_mhz * 1e6);
        s_p.f_ref = m->f_regr_hz;
        s_p.tau0 = (double)m->dt_units * s_p.u_s;
        s_p.x = 0.0; s_p.g_res = 0.0;
        ring_push(0.0);
    } else {
        /* teziste A(k-1) -> A(k): cas = dt(k-1) + (ym(k) - ym(k-1))/16, hrany = per(k-1) + (xm(k) - xm(k-1))/xdiv (256 nebo 4096) */
        int64_t dT16  = (int64_t)(s_p.prev_dt_u * 16u) + ((int64_t)m->rg_ym[0] - (int64_t)s_p.prev_ym);
        int64_t dNx = (int64_t)(s_p.prev_per * (uint64_t)xdiv) + ((int64_t)m->rg_xm[0] - (int64_t)s_p.prev_xm);
        if (dT16 <= 0 || dNx <= 0) { run_break(); return; }
        double dT = (double)dT16 / 16.0 * s_p.u_s;     /* [s] */
        double dN = (double)dNx / (double)xdiv;         /* [hrany] */
        m->f_cc_hz    = dN / dT;
        /* delka okna pro statistiku tak, aby edge_count / gate_cc = f_cc (Σ pres okna se pak teleskopicky zuzi) */
        /* audit 4: zaokrouhleni na cele ps v kazdem okne by se v Σ scitalo jako nahodna prochazka (0,29 ps * sqrt(m): po 4000
         * oknech ~18 ps = 1,8e-14 @ 1000 s, nad cilovou ADEV z faze) -> zbytek se prenasi, Σ gate_cc_ps sedi na < 1 ps */
        double g_ex = (double)m->edge_count / m->f_cc_hz * 1e12 + s_p.g_res;
        m->gate_cc_ps = (uint64_t)(g_ex + 0.5);
        s_p.g_res = g_ex - (double)m->gate_cc_ps;
        m->cc_ok      = 1u;
        s_p.x += dT - dN / s_p.f_ref;                   /* fazova odchylka proti referencnimu kmitoctu useku */
        ring_push(s_p.x);
    }
    s_p.prev_dt_u = m->dt_units; s_p.prev_per = m->edge_count;
    s_p.prev_xm = m->rg_xm[0];   s_p.prev_ym = m->rg_ym[0];
}

double fpga_phrec_adev(uint32_t mm, uint32_t *terms)
{
    if (terms) *terms = 0u;
    if (mm == 0u || s_n < 2u * mm + 3u || s_p.tau0 <= 0.0) return 0.0;
    uint32_t nt = s_n - 2u * mm;
    double acc = 0.0;
    for (uint32_t i = 0; i < nt; i++) {
        double d = ring_at(i + 2u * mm) - 2.0 * ring_at(i + mm) + ring_at(i);
        acc += d * d;
    }
    double tau = (double)mm * s_p.tau0;
    if (terms) *terms = nt;
    return sqrt(acc / (2.0 * tau * tau * (double)nt));
}

double fpga_phrec_mdev(uint32_t mm, uint32_t *terms)
{
    if (terms) *terms = 0u;
    if (mm == 0u || s_n < 3u * mm + 1u || s_p.tau0 <= 0.0) return 0.0;
    uint32_t nt = s_n - 3u * mm + 1u;
    /* klouzavy soucet druhych diferenci delky mm */
    double win = 0.0, acc = 0.0;
    for (uint32_t i = 0; i < mm; i++) win += ring_at(i + 2u * mm) - 2.0 * ring_at(i + mm) + ring_at(i);
    for (uint32_t j = 0; j < nt; j++) {
        acc += win * win;
        if (j + 1u < nt) {
            win -= ring_at(j + 2u * mm) - 2.0 * ring_at(j + mm) + ring_at(j);
            uint32_t k = j + mm;
            win += ring_at(k + 2u * mm) - 2.0 * ring_at(k + mm) + ring_at(k);
        }
    }
    double tau = (double)mm * s_p.tau0;
    if (terms) *terms = nt;
    return sqrt(acc / (2.0 * (double)mm * (double)mm * tau * tau * (double)nt));
}

void fpga_phrec_info(fpga_phrec_info_t *o)
{
    if (o == NULL) return;
    o->n = s_n; o->runs = s_runs; o->fed = s_fed; o->used = s_used;
    o->tau0_s = s_p.tau0; o->f_ref_hz = s_p.f_ref;
    o->x_last_s = s_n ? ring_at(s_n - 1u) : 0.0;
}

/* Selftest: 10 MHz, okno 0,25 s, teziste segmentu A s bilym sumem sigma = 20 ps (LCG + Box-Muller, deterministicke).
 * Ocekavani: ADEV(m) = sqrt(3)*sigma/(m*tau0) (+-25 %), f_cc = 10 MHz (+-1e-9), sklon ADEV mezi m=1 a m=16 ~ tau^-1. */
static uint32_t st_lcg;
static double st_rnd(void) { st_lcg = st_lcg * 1664525u + 1013904223u; return ((double)(st_lcg >> 8) + 0.5) / 16777216.0; }
static double st_gauss(void) { double u1 = st_rnd(), u2 = st_rnd(); return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2); }

int fpga_phrec_selftest(void)
{
    /* vola se pri startu (pred daty); stav se na konci vynuluje */
    fpga_phrec_reset();
    st_lcg = 12345u;
    const double f = 10e6, G = 0.25, u = 1.0 / (16384.0 * 100e6), sig = 20e-12;
    const uint64_t per = 2500000u;                     /* period na okno */
    const uint64_t dtu = (uint64_t)(G / u + 0.5);      /* dt okna v jednotkach (presne G pri 10 MHz) */
    int ok = 1;
    double fcc_err = 0.0;
    for (int k = 0; k < 1200; k++) {
        fpga_meas_t m; memset(&m, 0, sizeof m);
        m.caps = FPGA_CAP_DT | FPGA_CAP_REGR | FPGA_CAP_CHAN; m.phase_status = 1u; m.channel_id = 0u; m.clk_mhz = 100u;
        m.regr_used = 1u; m.rg_ok = 3u; m.edge_count = per; m.dt_units = dtu; m.f_regr_hz = f;
        double xbar = 351562.5;                         /* stredni index hrany segmentu A (od zacatku okna) */
        double ybar = xbar / f + sig * st_gauss();      /* jeho cas [s] od zacatku okna (zacatek = presne hranice period) */
        m.rg_xm[0] = (uint64_t)(xbar * 256.0 + 0.5);
        m.rg_ym[0] = (uint64_t)(ybar / u * 16.0 + 0.5);
        fpga_phrec_feed(&m, 0u, 1);
        if (k > 0) { ok &= m.cc_ok; double e = fabs(m.f_cc_hz / f - 1.0); if (e > fcc_err) fcc_err = e; }
    }
    uint32_t t1, t16;
    double a1 = fpga_phrec_adev(1u, &t1), a16 = fpga_phrec_adev(16u, &t16);
    double e1 = sqrt(3.0) * sig / (1.0 * G), e16 = sqrt(3.0) * sig / (16.0 * G);
    ok &= (a1 > 0.75 * e1 && a1 < 1.25 * e1);
    ok &= (a16 > 0.6 * e16 && a16 < 1.4 * e16);       /* mene clenu -> sirsi tolerance */
    ok &= (fcc_err < 1e-9);
    uint32_t tm; double md = fpga_phrec_mdev(4u, &tm);
    ok &= (md > 0.0 && md < a1);                       /* MDEV bileho PM klesa rychleji nez ADEV */
    /* preruseni kontinuity diru v SEQ */
    { fpga_meas_t m; memset(&m, 0, sizeof m); m.caps = FPGA_CAP_DT | FPGA_CAP_REGR; m.regr_used = 1u; m.rg_ok = 3u;
      m.edge_count = per; m.dt_units = dtu; m.clk_mhz = 100u; m.f_regr_hz = f;
      uint32_t r0 = s_runs; fpga_phrec_feed(&m, 2u, 1); ok &= (s_runs == r0 + 1u) && (s_n == 1u); }
    fpga_phrec_reset();
    return ok;
}
