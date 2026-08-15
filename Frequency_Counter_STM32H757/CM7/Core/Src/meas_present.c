/**
 * @file    meas_present.c
 * @brief   Prezentace měření (#67): perioda, odchylka/jednotky, nominál,
 *          statistika N vzorků, TFOM. Pure-logic; viz meas_present.h.
 */
#include "meas_present.h"
#include <math.h>

/* ── Running statistika (Welford) ────────────────────────────────────────── */
void mp_stats_reset(mp_stats_t *s)
{
    s->n = 0; s->mean = 0.0; s->m2 = 0.0; s->min = 0.0; s->max = 0.0;
}

void mp_stats_add(mp_stats_t *s, double x)
{
    if (s->n == 0) { s->min = s->max = x; }
    else { if (x < s->min) s->min = x; if (x > s->max) s->max = x; }
    s->n++;
    double d = x - s->mean;
    s->mean += d / (double)s->n;
    s->m2   += d * (x - s->mean);          /* Welford: (x-mean_old)*(x-mean_new) */
}

double mp_stats_sd(const mp_stats_t *s)
{
    if (s->n < 2) return 0.0;
    return sqrt(s->m2 / (double)(s->n - 1));   /* výběrová (n-1) */
}

double mp_stats_p2p(const mp_stats_t *s)
{
    return (s->n > 0) ? (s->max - s->min) : 0.0;
}

/* ── Perioda ─────────────────────────────────────────────────────────────── */
double mp_period_s(double hz)
{
    return (hz > 0.0) ? (1.0 / hz) : 0.0;
}

/* ── Automatický nominál (nejbližší kulatá reference) ────────────────────── */
double mp_nominal_auto(double hz)
{
    if (hz <= 0.0) return 0.0;
    double e    = floor(log10(hz));
    double base = pow(10.0, e);
    double m    = hz / base;               /* mantisa v [1,10) */
    static const double snaps[] = { 1.0, 2.0, 2.5, 5.0, 10.0 };
    double best = 1.0, bd = 1e300;
    for (int i = 0; i < 5; i++) {
        double dd = fabs(m - snaps[i]);
        if (dd < bd) { bd = dd; best = snaps[i]; }
    }
    return best * base;                    /* 10.0*base = 1×10^(e+1), korektní */
}

/* ── Odchylka ve zvolené jednotce ────────────────────────────────────────── */
double mp_deviation(double hz, double nominal, mp_unit_t unit)
{
    if (unit == MP_UNIT_HZ) return hz - nominal;
    if (nominal == 0.0) return 0.0;        /* guard pro relativní jednotky */
    double rel = (hz - nominal) / nominal;
    switch (unit) {
    case MP_UNIT_PPM: return rel * 1e6;
    case MP_UNIT_PPB: return rel * 1e9;
    case MP_UNIT_PPT: return rel * 1e12;
    case MP_UNIT_REL: return rel;
    default:          return hz - nominal;
    }
}

const char *mp_unit_label(mp_unit_t u)
{
    switch (u) {
    case MP_UNIT_HZ:  return "Hz";
    case MP_UNIT_PPM: return "ppm";
    case MP_UNIT_PPB: return "ppb";
    case MP_UNIT_PPT: return "ppt";
    case MP_UNIT_REL: return "rel";
    default:          return "?";
    }
}

/* ── TFOM (odhad z kvality GPS) ──────────────────────────────────────────── */
mp_tfom_t mp_tfom(int gps_valid, int fix_mode, int sats, float hdop,
                  int holdover, int warmup)
{
    mp_tfom_t t;
    if (warmup)                          { t.level = 9; t.label = "WARMUP"; }
    else if (holdover)                   { t.level = 6; t.label = "HOLDOVER"; }
    else if (!gps_valid || fix_mode < 2) { t.level = 8; t.label = "NO LOCK"; }
    else if (fix_mode < 3)               { t.level = 4; t.label = "2D FIX"; }
    else {                               /* 3D fix — kvalita dle HDOP + počtu družic */
        if (hdop > 0.0f && hdop <= 1.0f && sats >= 6)      { t.level = 1; t.label = "LOCK"; }
        else if (hdop > 0.0f && hdop <= 2.0f && sats >= 4) { t.level = 2; t.label = "LOCK"; }
        else                                               { t.level = 3; t.label = "LOCK"; }
    }
    return t;
}

/* ── Selftest ────────────────────────────────────────────────────────────── */
int mp_selftest(void)
{
    int ok = 1;

    /* Perioda. */
    ok &= (fabs(mp_period_s(10e6) - 1e-7) < 1e-15);
    ok &= (mp_period_s(0.0) == 0.0);

    /* Automatický nominál (tolerantně — pow(10,e) nemusí být bit-přesné; snapy jsou MHz od sebe). */
    ok &= (fabs(mp_nominal_auto(9.9999e6) - 10e6) < 1.0);
    ok &= (fabs(mp_nominal_auto(10.0e6)   - 10e6) < 1.0);
    ok &= (fabs(mp_nominal_auto(4.9e6)    - 5e6)  < 1.0);
    ok &= (fabs(mp_nominal_auto(2.4e6)    - 2.5e6) < 1.0);

    /* Odchylka v jednotkách: 1 Hz na 10 MHz = 1e-7 = 0,1 ppm = 100 ppb = 100000 ppt. */
    ok &= (fabs(mp_deviation(10000001.0, 10000000.0, MP_UNIT_HZ)  - 1.0)      < 1e-9);
    ok &= (fabs(mp_deviation(10000001.0, 10000000.0, MP_UNIT_PPM) - 0.1)      < 1e-9);
    ok &= (fabs(mp_deviation(10000001.0, 10000000.0, MP_UNIT_PPB) - 100.0)    < 1e-6);
    ok &= (fabs(mp_deviation(10000001.0, 10000000.0, MP_UNIT_PPT) - 100000.0) < 1e-3);
    ok &= (mp_deviation(1.0, 0.0, MP_UNIT_PPM) == 0.0);   /* nominal 0 guard */

    /* Statistika {2,4,4,4,5}: n=5, mean=3,8, p-p=3, s=sqrt(1,2). */
    mp_stats_t s; mp_stats_reset(&s);
    double xs[5] = { 2.0, 4.0, 4.0, 4.0, 5.0 };
    for (int i = 0; i < 5; i++) mp_stats_add(&s, xs[i]);
    ok &= (s.n == 5);
    ok &= (fabs(s.mean - 3.8) < 1e-9);
    ok &= (fabs(mp_stats_p2p(&s) - 3.0) < 1e-9);
    ok &= (fabs(mp_stats_sd(&s) - sqrt(1.2)) < 1e-6);
    ok &= (mp_stats_sd(&s) > 0.0);            /* n>=2 */

    /* TFOM. */
    ok &= (mp_tfom(0, 0, 0, 0.0f, 0, 1).level == 9);   /* warmup */
    ok &= (mp_tfom(1, 3, 8, 0.8f, 0, 0).level == 1);   /* dobrý lock */
    ok &= (mp_tfom(0, 0, 0, 0.0f, 1, 0).level == 6);   /* holdover */
    ok &= (mp_tfom(0, 0, 0, 0.0f, 0, 0).level == 8);   /* no lock */

    return ok;
}
