/**
 * @file    meas_present.c
 * @brief   Prezentace měření (#67): perioda, odchylka/jednotky, nominál,
 *          statistika N vzorků, TFOM. Pure-logic; viz meas_present.h.
 */
#include "meas_present.h"
#include <math.h>
#include <string.h>   /* memset — selftest filtru */

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
/* ── Filtr merení (viz meas_present.h) ─────────────────────────────────────── */
void mp_filt_reset(mp_filt_state_t *f, mp_filt_t mode, uint8_t win)
{
    if (f == NULL) return;
    f->n = 0; f->head = 0;
    f->mode = (mode < MP_FILT_N) ? mode : MP_FILT_OFF;
    if (win < 1) win = 1;
    if (win > MP_FILT_MAX) win = MP_FILT_MAX;
    f->win = win;
}

const char *mp_filt_label(mp_filt_t m)
{
    switch (m) {
    case MP_FILT_AVG: return "PRUM";
    case MP_FILT_MED: return "MED";
    default:          return "VYP";
    }
}

double mp_filt_add(mp_filt_state_t *f, double x)
{
    if (f == NULL || f->mode == MP_FILT_OFF) return x;
    if (f->win < 1 || f->win > MP_FILT_MAX) return x;      /* nezinicializovany */

    f->buf[f->head] = x;
    f->head = (uint8_t)((f->head + 1u) % f->win);
    if (f->n < f->win) f->n++;

    if (f->mode == MP_FILT_AVG) {
        double s = 0.0;
        for (uint8_t i = 0; i < f->n; i++) s += f->buf[i];
        return s / (double)f->n;
    }

    /* Median: kopie + insertion sort. n <= 16, takze O(n²) je levnejsi nez
     * cokoli chytrejsiho a hlavne bez alokace. */
    double t[MP_FILT_MAX];
    for (uint8_t i = 0; i < f->n; i++) t[i] = f->buf[i];
    for (uint8_t i = 1; i < f->n; i++) {
        double v = t[i]; int j = (int)i - 1;
        while (j >= 0 && t[j] > v) { t[j + 1] = t[j]; j--; }
        t[j + 1] = v;
    }
    /* Sudy pocet -> prumer dvou prostrednich (jinak by se vysledek pri kazdem
     * dalsim vzorku skokove prehazoval mezi dvema hodnotami). */
    return (f->n & 1u) ? t[f->n / 2]
                       : 0.5 * (t[f->n / 2 - 1] + t[f->n / 2]);
}

/* ── Rozpocet nejistoty (viz meas_present.h) ────────────────────────────────── */
void mp_budget(double hz, double gate_s, double tdc_ps, double sigma_y,
               double ref_ppb, mp_budget_t *o)
{
    if (o == NULL) return;
    memset(o, 0, sizeof *o);
    if (gate_s <= 0.0) gate_s = 1.0;

    /* Rozliseni: kvantizace na obou hranach hradla -> sqrt(2)·tdc/gate. */
    o->u_res_rel = 1.41421356 * (tdc_ps * 1e-12) / gate_s;
    o->u_sta_rel = (sigma_y > 0.0) ? sigma_y : 0.0;
    o->u_ref_rel = ref_ppb * 1e-9;

    double s = o->u_res_rel * o->u_res_rel
             + o->u_sta_rel * o->u_sta_rel
             + o->u_ref_rel * o->u_ref_rel;
    o->u_tot_rel = sqrt(s);
    o->u_tot_hz  = o->u_tot_rel * ((hz > 0.0) ? hz : 0.0);

    /* Kolik cifer ma smysl ukazovat: log10(1/u). Napr. u = 1e-9 -> 9 cifer.
     * Strop 15 = mez double; podlaha 1 = aspon jedna cifra. */
    if (o->u_tot_rel > 0.0) {
        double d = -log10(o->u_tot_rel);
        o->digits = (int)(d + 0.5);
        if (o->digits < 1)  o->digits = 1;
        if (o->digits > 15) o->digits = 15;
    } else {
        o->digits = 15;
    }
}

/* ── Linearni proklad (drift/aging i tempco) ─────────────────────────────────
 * Akumulujeme sumy, ne body — pamet O(1) i pro tisice vzorku. */
void mp_fit_reset(mp_fit_t *f)
{
    if (f) memset(f, 0, sizeof *f);
}

void mp_fit_add(mp_fit_t *f, double x, double y)
{
    if (f == NULL) return;
    f->n++;
    f->sx  += x;
    f->sy  += y;
    f->sxx += x * x;
    f->syy += y * y;
    f->sxy += x * y;
}

int mp_fit_solve(mp_fit_t *f)
{
    if (f == NULL || f->n < 3u) return 0;
    double n = (double)f->n;
    double dx = n * f->sxx - f->sx * f->sx;      /* n·Sxx - Sx² */
    /* Nulovy rozptyl X (vsechny vzorky ve stejnem case/teplote) -> smernice
     * neni definovana. Radeji "nevim" nez deleni skoro nulou. */
    if (dx <= 0.0 || dx < 1e-30) return 0;

    f->b = (n * f->sxy - f->sx * f->sy) / dx;
    f->a = (f->sy - f->b * f->sx) / n;

    double dy = n * f->syy - f->sy * f->sy;
    /* Konstantni Y (dokonaly, ale nulovy signal) -> korelace nedefinovana; b je
     * pritom platne (0), takze vratime uspech s r = 0. */
    f->r = (dy > 0.0) ? ((n * f->sxy - f->sx * f->sy) / sqrt(dx * dy)) : 0.0;
    if (f->r >  1.0) f->r =  1.0;                /* zaokrouhlovaci prestrel */
    if (f->r < -1.0) f->r = -1.0;
    return 1;
}

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

    /* ── Filtr merení ─────────────────────────────────────────────────────── */
    {   mp_filt_state_t f;
        /* VYP musi vratit vstup beze zmeny. */
        mp_filt_reset(&f, MP_FILT_OFF, 8);
        ok &= (mp_filt_add(&f, 42.0) == 42.0);

        /* PRUMER: 1..4 -> 1; 1,5; 2; 2,5 */
        mp_filt_reset(&f, MP_FILT_AVG, 4);
        ok &= (mp_filt_add(&f, 1.0) == 1.0);
        ok &= (mp_filt_add(&f, 2.0) == 1.5);
        ok &= (mp_filt_add(&f, 3.0) == 2.0);
        ok &= (mp_filt_add(&f, 4.0) == 2.5);

        /* MEDIAN musi VYSTRELEK ZAHODIT — to je cely duvod, proc existuje.
         * Okno 5, hodnoty 10,10,10,10 + jeden skok 1000 -> median zustava 10,
         * zatimco prumer by vyskocil na ~208. */
        mp_filt_reset(&f, MP_FILT_MED, 5);
        (void)mp_filt_add(&f, 10.0); (void)mp_filt_add(&f, 10.0);
        (void)mp_filt_add(&f, 10.0); (void)mp_filt_add(&f, 10.0);
        ok &= (mp_filt_add(&f, 1000.0) == 10.0);

        /* Sudy pocet vzorku -> prumer dvou prostrednich (bez skokoveho prepinani). */
        mp_filt_reset(&f, MP_FILT_MED, 4);
        (void)mp_filt_add(&f, 1.0); (void)mp_filt_add(&f, 2.0);
        (void)mp_filt_add(&f, 3.0);
        ok &= (mp_filt_add(&f, 4.0) == 2.5);

        /* Nezinicializovany stav nesmi nic pokazit (win = 0). */
        mp_filt_state_t z; memset(&z, 0, sizeof z); z.mode = MP_FILT_AVG;
        ok &= (mp_filt_add(&z, 7.0) == 7.0);
    }

    /* ── Rozpocet nejistoty ───────────────────────────────────────────────── */
    {   mp_budget_t bd;
        /* Cisty prispevek rozliseni: TDC 2500 ps, hradlo 1 s, zadny jiny zdroj.
         * u = sqrt(2)·2,5e-9 / 1 = 3,54e-9 */
        mp_budget(1e7, 1.0, 2500.0, 0.0, 0.0, &bd);
        ok &= (bd.u_res_rel > 3.5e-9 && bd.u_res_rel < 3.6e-9);
        ok &= (bd.u_tot_rel > 3.5e-9 && bd.u_tot_rel < 3.6e-9);
        /* Delsi hradlo musi rozliseni ZLEPSIT primo umerne. */
        mp_budget_t bd10;
        mp_budget(1e7, 10.0, 2500.0, 0.0, 0.0, &bd10);
        ok &= (bd10.u_res_rel < bd.u_res_rel * 0.11);

        /* Kvadraticky soucet: 3-4-5 trojuhelnik (3e-9 a 4e-9 -> 5e-9). */
        mp_budget(1e7, 1.0, 0.0, 3e-9, 4.0, &bd);
        ok &= (bd.u_tot_rel > 4.99e-9 && bd.u_tot_rel < 5.01e-9);
        ok &= (bd.u_tot_hz  > 4.99e-2 && bd.u_tot_hz  < 5.01e-2);   /* x 1e7 Hz */
        /* Pocet platnych cifer z u = 5e-9 -> log10(1/u) = 8,3 -> 8 */
        ok &= (bd.digits == 8);
    }

    /* ── Linearni proklad ─────────────────────────────────────────────────── */
    {   mp_fit_t ft;
        /* Presna primka y = 2 + 3x -> a=2, b=3, r=1 */
        mp_fit_reset(&ft);
        for (int i = 0; i < 10; i++) mp_fit_add(&ft, (double)i, 2.0 + 3.0 * (double)i);
        ok &= (mp_fit_solve(&ft) == 1);
        ok &= (ft.b > 2.999 && ft.b < 3.001);
        ok &= (ft.a > 1.999 && ft.a < 2.001);
        ok &= (ft.r > 0.999);
        /* Klesajici primka -> r = -1 */
        mp_fit_reset(&ft);
        for (int i = 0; i < 10; i++) mp_fit_add(&ft, (double)i, -5.0 * (double)i);
        ok &= (mp_fit_solve(&ft) == 1 && ft.r < -0.999);
        /* Malo bodu -> odmitnout (radeji nic nez smernice ze dvou vzorku). */
        mp_fit_reset(&ft);
        mp_fit_add(&ft, 0.0, 0.0); mp_fit_add(&ft, 1.0, 1.0);
        ok &= (mp_fit_solve(&ft) == 0);
        /* Nulovy rozptyl X (vse ve stejnem case) -> smernice nedefinovana. */
        mp_fit_reset(&ft);
        for (int i = 0; i < 5; i++) mp_fit_add(&ft, 7.0, (double)i);
        ok &= (mp_fit_solve(&ft) == 0);
        /* Konstantni Y -> b = 0 a r = 0, ale VYPOCET PROJDE (validni vysledek). */
        mp_fit_reset(&ft);
        for (int i = 0; i < 5; i++) mp_fit_add(&ft, (double)i, 3.0);
        ok &= (mp_fit_solve(&ft) == 1 && ft.b > -1e-9 && ft.b < 1e-9);
    }

    return ok;
}
