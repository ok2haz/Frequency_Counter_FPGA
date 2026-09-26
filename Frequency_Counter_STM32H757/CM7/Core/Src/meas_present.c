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

double mp_period_sample_s(uint64_t edges, uint64_t gate_ns, uint32_t mul,
                          double hz_fallback)
{
    /* Přímá cesta: okno `gate_ns` nanosekund obsahovalo `edges·mul` period
     * vstupního signálu, takže perioda = doba / počet. Žádný mezikrok přes
     * kmitočet -> žádná ztráta na zaokrouhlení (viz meas_present.h). */
    if (mul != 0u && edges != 0u && gate_ns != 0u) {
        double n = (double)edges * (double)mul;
        if (n > 0.0) return ((double)gate_ns * 1e-9) / n;
    }
    return mp_period_s(hz_fallback);
}

const char *mp_time_unit(double sec, double *scale)
{
    double a = fabs(sec), s;
    const char *u;
    if      (a >= 1.0)  { u = "s";  s = 1.0;   }
    else if (a >= 1e-3) { u = "ms"; s = 1e-3;  }
    else if (a >= 1e-6) { u = "us"; s = 1e-6;  }
    else if (a >= 1e-9) { u = "ns"; s = 1e-9;  }
    else if (a > 0.0)   { u = "ps"; s = 1e-12; }
    else                { u = "s";  s = 1.0;   }
    if (scale) *scale = s;
    return u;
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
    /* F-0159: neznámé hradlo se DÁL nahrazuje 1 s, ale jen kvůli numerické
     * bezpečnosti (dělení) — výsledek se označí jako neplatný a volající ho
     * nesmí zobrazit. `!(gate_s > 0)` chytí i NaN. */
    o->valid = (gate_s > 0.0) ? 1 : 0;
    if (!(gate_s > 0.0)) gate_s = 1.0;

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
    /* F-0169: akumuluje se RELATIVNĚ k prvnímu bodu. Směrnice i r jsou vůči
     * posunu invariantní, takže výsledek se nemění — mění se jen to, že se
     * v `n·Σxx − (Σx)²` neodečítají dvě obří skoro stejná čísla. */
    if (f->n == 0u) { f->x0 = x; f->y0 = y; }
    f->n++;
    double dx = x - f->x0, dy = y - f->y0;
    f->sx  += dx;
    f->sy  += dy;
    f->sxx += dx * dx;
    f->syy += dy * dy;
    f->sxy += dx * dy;
    if (f->n >= 2u) {                             /* F-0173: součty sousedních dvojic */
        f->l_yy += dy * f->py;  f->l_xx += dx * f->px;
        f->l_xy += dx * f->py;  f->l_yx += dy * f->px;
        f->l_ylead += dy; f->l_ylag += f->py;
        f->l_xlead += dx; f->l_xlag += f->px;
    }
    f->px = dx; f->py = dy;
}

int mp_fit_solve(mp_fit_t *f)
{
    if (f == NULL || f->n < 3u) return 0;
    double n = (double)f->n;
    double dx = n * f->sxx - f->sx * f->sx;      /* n·Sxx - Sx² */
    /* Nulovy rozptyl X (vsechny vzorky ve stejnem case/teplote) -> smernice
     * neni definovana. Radeji "nevim" nez deleni skoro nulou. Negovana forma
     * chyti i NaN v X (F-0170, L-0087) — puvodni `dx <= 0 || dx < 1e-30` ho
     * propustila. */
    if (!(dx >= 1e-30)) return 0;

    f->b = (n * f->sxy - f->sx * f->sy) / dx;
    if (f->b != f->b) return 0;                  /* NaN v Y -> „nevim", ne NaN smernice */
    /* Průsečík je v centrovaných souřadnicích -> posunout zpět do původních
     * (y = a' + b·(x − x0) + y0  =>  a = a' + y0 − b·x0). */
    f->a = (f->sy - f->b * f->sx) / n + f->y0 - f->b * f->x0;

    double dy = n * f->syy - f->sy * f->sy;
    /* Konstantni Y (dokonaly, ale nulovy signal) -> korelace nedefinovana; b je
     * pritom platne (0), takze vratime uspech s r = 0. */
    f->r = (dy > 0.0) ? ((n * f->sxy - f->sx * f->sy) / sqrt(dx * dy)) : 0.0;
    if (f->r >  1.0) f->r =  1.0;                /* zaokrouhlovaci prestrel */
    if (f->r < -1.0) f->r = -1.0;

    /* F-0173: lag-1 autokorelace reziduí e = Y − ac − b·X (centrované souřadnice,
     * ac = centrovaný průsečík). Obojí se rozepíše na už nasbírané součty:
     *   Σe²       = Syy − 2ac·Sy − 2b·Sxy + n·ac² + 2ac·b·Sx + b²·Sxx
     *   Σe_i·e_i-1 = Lyy − ac(Ly⁺ + Ly⁻) − b(Lyx + Lxy) + (n−1)ac²
     *               + ac·b(Lx⁺ + Lx⁻) + b²·Lxx
     * Když jsou rezidua proti rozptylu Y zanedbatelná (dokonalá přímka), je ρ
     * z odečtu šumem zaokrouhlení — pak 0 (o průkaznosti rozhodne r). */
    {
        double ac  = (f->sy - f->b * f->sx) / n;
        double b   = f->b;
        double see = f->syy - 2.0 * ac * f->sy - 2.0 * b * f->sxy + n * ac * ac
                   + 2.0 * ac * b * f->sx + b * b * f->sxx;
        double se1 = f->l_yy - ac * (f->l_ylead + f->l_ylag) - b * (f->l_yx + f->l_xy)
                   + (n - 1.0) * ac * ac + ac * b * (f->l_xlag + f->l_xlead)
                   + b * b * f->l_xx;
        double yv  = f->syy - f->sy * f->sy / n;       /* Σ(Y − Ȳ)² */
        f->rho = 0.0;
        if (see > 1e-9 * yv && see > 0.0) {
            f->rho = se1 / see;
            if (!(f->rho <= 1.0))  f->rho = (f->rho > 1.0) ? 1.0 : 0.0;   /* i NaN -> 0 */
            if (f->rho < -1.0)     f->rho = -1.0;
        }
    }
    return 1;
}

/* Kritické hodnoty Studentova t, dvoustranně 5 %, df = 1..30 (standardní
 * tabulka). Nad 30 se bere 2,042 (hodnota pro df = 30) — konzervativně: pro
 * velká df je skutečná mez jen nepatrně nižší (df = 200: 1,972). */
static const float T95_2S[30] = {
    12.706f, 4.303f, 3.182f, 2.776f, 2.571f, 2.447f, 2.365f, 2.306f, 2.262f, 2.228f,
     2.201f, 2.179f, 2.160f, 2.145f, 2.131f, 2.120f, 2.110f, 2.101f, 2.093f, 2.086f,
     2.080f, 2.074f, 2.069f, 2.064f, 2.060f, 2.056f, 2.052f, 2.048f, 2.045f, 2.042f
};

/* F-0169: průkaznost směrnice t-testem korelace, místo dřívějšího pevného prahu
 * |r| < 0,5 v okně ANALÝZA. Ten nebral ohled na počet bodů: při n ≈ 200
 * (decimace v `ana_recompute`) je r = 0,3 průkazné na p < 1e-4 a hlásilo se
 * „neprůkazné", při n = 4 není průkazné ani r = 0,9 a hlásilo se jako platné. */
int mp_fit_significant(const mp_fit_t *f)
{
    if (f == NULL || f->n < 3u) return 0;
    /* F-0173: efektivní počet bodů podle autokorelace reziduí (viz hlavička).
     * Záporná ρ se NEuplatňuje — zvýšit n_eff nad n by bylo neopatrné. */
    double ne = (double)f->n;
    if (f->rho > 0.0) ne = ne * (1.0 - f->rho) / (1.0 + f->rho);
    if (!(ne >= 3.0)) return 0;
    uint32_t df = (uint32_t)(ne - 2.0);                /* ⌊n_eff⌋ − 2, >= 1 */
    double tc = (df <= 30u) ? (double)T95_2S[df - 1u] : 2.042;
    /* 🔴 F-0170: NaN MUSÍ padnout do „neprůkazné" (L-0087). Do 2026-09-26 tu
     * stálo jen `if (!(r2 < 1.0)) return 1;` s poznámkou „i NaN-safe" — bez UB
     * to bylo, ale NaN tou větví prošel jako PRŮKAZNÝ. Webové dvojče `fitSig`
     * vrací pro NaN false a obě se musí shodovat (hlídá `tools/spa/stat_test.js`). */
    if (f->r != f->r) return 0;
    double r2 = f->r * f->r;
    if (!(r2 < 1.0)) return 1;                   /* dokonalá přímka */
    double t = fabs(f->r) * sqrt((double)df / (1.0 - r2));
    return (t >= tc) ? 1 : 0;
}

int mp_selftest(void)
{
    int ok = 1;

    /* Perioda. */
    ok &= (fabs(mp_period_s(10e6) - 1e-7) < 1e-15);
    ok &= (mp_period_s(0.0) == 0.0);

    /* Perioda PŘÍMO z reciproké dvojice (#109). 1000 period v okně 100 µs =
     * 100 ns; totéž musí vyjít, když je 250 hran s násobitelem 4. */
    ok &= (fabs(mp_period_sample_s(1000u, 100000u, 1u, 0.0) - 1e-7) < 1e-18);
    ok &= (fabs(mp_period_sample_s(250u,  100000u, 4u, 0.0) - 1e-7) < 1e-18);
    /* Nepoužitelná dvojice -> degradace na 1/f (mul==0 = žádný násobitel nesedí). */
    ok &= (fabs(mp_period_sample_s(250u, 100000u, 0u, 10e6) - 1e-7) < 1e-15);
    ok &= (fabs(mp_period_sample_s(0u,   100000u, 4u, 10e6) - 1e-7) < 1e-15);
    ok &= (fabs(mp_period_sample_s(250u, 0u,      4u, 10e6) - 1e-7) < 1e-15);
    ok &= (mp_period_sample_s(0u, 0u, 0u, 0.0) == 0.0);

    /* 🔑 Proč perioda potřebuje VLASTNÍ akumulátor a nestačí převést statistiku
     * z Hz: průměr period NENÍ převrácený průměr kmitočtů (Jensen). Pro f = 1 a
     * 3 Hz je mean(f) = 2 -> 1/mean = 0,5, ale mean(T) = (1 + 1/3)/2 = 2/3. */
    {   mp_stats_t fs, ps; mp_stats_reset(&fs); mp_stats_reset(&ps);
        double f2[2] = { 1.0, 3.0 };
        for (int i = 0; i < 2; i++) {
            mp_stats_add(&fs, f2[i]);
            mp_stats_add(&ps, mp_period_s(f2[i]));
        }
        ok &= (fabs(ps.mean - (2.0 / 3.0)) < 1e-12);
        ok &= (fabs(1.0 / fs.mean - 0.5)   < 1e-12);
        ok &= (fabs(ps.mean - 1.0 / fs.mean) > 0.1);   /* liší se PROKAZATELNĚ */
    }

    /* Volba časové jednotky. */
    {   double sc = 0.0;
        ok &= (strcmp(mp_time_unit(5.0,    &sc), "s")  == 0 && sc == 1.0);
        ok &= (strcmp(mp_time_unit(2e-3,   &sc), "ms") == 0 && sc == 1e-3);
        ok &= (strcmp(mp_time_unit(4e-6,   &sc), "us") == 0 && sc == 1e-6);
        ok &= (strcmp(mp_time_unit(1e-7,   &sc), "ns") == 0 && sc == 1e-9);
        ok &= (strcmp(mp_time_unit(-1e-7,  &sc), "ns") == 0);   /* dle |sec| */
        ok &= (strcmp(mp_time_unit(1e-13,  &sc), "ps") == 0 && sc == 1e-12);
        ok &= (strcmp(mp_time_unit(0.0,    &sc), "s")  == 0);
        ok &= (strcmp(mp_time_unit(1e-7, NULL), "ns") == 0);    /* scale smí být NULL */
    }

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
        ok &= (mp_fit_significant(&ft) == 0);          /* r = 0 -> neprukazne */

        /* F-0169 (1): NUMERICKA STABILITA. Drift kmitoctu v unix case: x ~1,76e9 s,
         * y ~1e7 Hz, 1 mHz za hodinu, 200 bodu. Puvodni necentrovany vzorec tu dal
         * rozptyl y <= 0 -> r = 0 („dokonala primka je neprukazna"); overeno na
         * hostu v IEEE double. ⚠️ Na cili muze GCC stahnout nasobeni a scitani do
         * FMA, takze presna podoba chyby stareho vzorce se muze lisit — tenhle
         * test proto overuje, ze NOVY vypocet je spravne, ne ze stary selze. */
        mp_fit_reset(&ft);
        for (int i = 0; i < 200; i++)
            mp_fit_add(&ft, 1.76e9 + 3600.0 * (double)i, 1e7 + 1e-3 * (double)i);
        ok &= (mp_fit_solve(&ft) == 1);
        ok &= (fabs(ft.b - 1e-3 / 3600.0) < 1e-12);    /* smernice [Hz/s] */
        ok &= (ft.r > 0.999);
        ok &= (fabs((ft.a + ft.b * 1.76e9) - 1e7) < 1e-3);   /* a posunute zpet */
        ok &= (mp_fit_significant(&ft) == 1);

        /* F-0169 (2): prukaznost zavisi na n, ne na pevnem |r|. Stejne r ~0,4:
         * pri n = 200 prukazne, pri n = 5 ne. Syntetizovano primo pres r, n. */
        {   mp_fit_t fs = {0};
            fs.r = 0.4; fs.n = 200u; ok &= (mp_fit_significant(&fs) == 1);
            fs.n = 5u;               ok &= (mp_fit_significant(&fs) == 0);
            fs.r = 0.99; fs.n = 5u;  ok &= (mp_fit_significant(&fs) == 1);   /* t=12 > 3,18 */
            /* F-0170: NaN -> neprukazne (starý kód vracel 1), shodne s webem. */
            fs.r = (double)NAN; fs.n = 100u; ok &= (mp_fit_significant(&fs) == 0);
            /* F-0173: autokorelace snizi n_eff. n = 200, r = 0,4, rho = 0,9 ->
             * n_eff 10,5 -> df 8 -> t = 1,23 < 2,31 -> neprukazne (bez korekce
             * t = 6,1 -> prukazne; stary kod tenhle pripad NESPLNI). Zaporna rho
             * se neuplatnuje. Tytez vektory ma webove `fitSig` (stat_test.js). */
            fs.r = 0.4; fs.n = 200u; fs.rho = 0.9;  ok &= (mp_fit_significant(&fs) == 0);
            fs.rho = -0.5;                          ok &= (mp_fit_significant(&fs) == 1);
            fs.rho = 0.0;                           ok &= (mp_fit_significant(&fs) == 1);
        }

        /* F-0173: rho z JEDNOHO pruchodu musi sedet s dvouprochodovou referenci
         * (primo z reziduí) — i pri velkem posunu X/Y (unix cas, 1e6 offset). */
        {   static double rxs[64], rys[64];
            mp_fit_reset(&ft);
            for (int i = 0; i < 64; i++) {
                rxs[i] = 1.76e9 + 10.0 * (double)i;
                rys[i] = 1e6 + 0.3 * (double)i + 2.0 * sin(2.0 * M_PI * (double)i / 16.0);
                mp_fit_add(&ft, rxs[i], rys[i]);
            }
            ok &= (mp_fit_solve(&ft) == 1);
            double mx = 0, my = 0;
            for (int i = 0; i < 64; i++) { mx += rxs[i]; my += rys[i]; }
            mx /= 64.0; my /= 64.0;
            double sxx = 0, sxy = 0;
            for (int i = 0; i < 64; i++) { sxx += (rxs[i]-mx)*(rxs[i]-mx); sxy += (rxs[i]-mx)*(rys[i]-my); }
            double bb = sxy / sxx, aa = my - bb * mx, e0 = 0, e1 = 0, ep = 0;
            for (int i = 0; i < 64; i++) {
                double e = rys[i] - aa - bb * rxs[i];
                e0 += e * e; if (i) e1 += e * ep; ep = e;
            }
            ok &= (fabs(ft.rho - e1 / e0) < 1e-6);
        }
        /* strida +1/-1 -> rho ~ -1 (a neuplatni se) */
        mp_fit_reset(&ft);
        for (int i = 0; i < 100; i++) mp_fit_add(&ft, (double)i, (i & 1) ? 1.0 : -1.0);
        ok &= (mp_fit_solve(&ft) == 1 && ft.rho < -0.9);

        /* F-0170: NaN v datech -> proklad „nevim" (0), ne NaN smernice. */
        mp_fit_reset(&ft);
        for (int i = 0; i < 5; i++) mp_fit_add(&ft, (double)i, (i == 2) ? (double)NAN : (double)i);
        ok &= (mp_fit_solve(&ft) == 0);
        mp_fit_reset(&ft);
        for (int i = 0; i < 5; i++) mp_fit_add(&ft, (i == 2) ? (double)NAN : (double)i, (double)i);
        ok &= (mp_fit_solve(&ft) == 0);
    }

    /* ── Rozpocet nejistoty: priznak platnosti (F-0159) ─────────────────────── */
    {   mp_budget_t bv;
        mp_budget(1e7, 0.0, 2500.0, 0.0, 1.0, &bv);   ok &= (bv.valid == 0);
        mp_budget(1e7, 0.25, 2500.0, 0.0, 1.0, &bv);  ok &= (bv.valid == 1);
    }

    return ok;
}
