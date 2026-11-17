/* Audit 2026-11-17 (prezkum v0.45.23): NULA intervalu A-B pri spolecnem signalu na A i B.
 * Krmi SKUTECNOU fpga_v3_ab_feed (CM7/Core/Src/fpga_v3.c) udalostmi souhrnu (BURST) obou kanalu tak, jak je dava
 * fpga_v3_ev_from_rec: v kazdem segmentu 25 ms prvni casovana hrana A (start) a prvni casovana hrana B (T0 souhrnu)
 * se stredni periodou. Signal 10 MHz na obou vstupech, B = A + delta (rozdil cest), sum znacky sigma_m na kazde znacce.
 * Vypise, co by dala `fpga ab zero` / tlacitko NULA A-B (stredni interval) a jak vypadaji prijate intervaly. */
#include "fpga_v3.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

static uint32_t rs = 12345u;
static double urand(void) { rs = rs * 1664525u + 1013904223u; return ((rs >> 8) + 0.5) / 16777216.0; }
static double grand(void) { double u1 = urand(), u2 = urand(); return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2); }

static fpga_v3_ev_t evk(uint8_t ch, uint64_t t_u, uint32_t n, double per_u)
{
    fpga_v3_ev_t e;
    memset(&e, 0, sizeof e);
    e.ch = ch; e.kind = (n >= 3u) ? FPGA_V3_EV_BURST : FPGA_V3_EV_EDGE; e.n = n; e.t_u = t_u; e.per_u = per_u;
    return e;
}

static void run(double delta_ps, double sig_m_ps, double per_ps, uint32_t nseg)
{
    const double U = 10000.0 / 16384.0;          /* ps na jednotku T/16384 pri 100 MHz */
    const double L_ps = 25e9;                    /* segment 25 ms */
    fpga_v3_ab_t P;
    fpga_v3_ab_init(&P, (uint64_t)(10e12 / U));   /* start bez stopu do 10 s */
    int64_t dt[4];
    double sum = 0, sum2 = 0; uint32_t n = 0, nlo = 0, nhi = 0;
    for (uint32_t s = 0; s < nseg; s++) {
        const double S = 1e12 + (double)s * L_ps;            /* zacatek segmentu [ps] */
        const double a = S + urand() * per_ps;               /* prvni hrana A po zacatku segmentu (faze nahodna) */
        double b = a + delta_ps;                             /* odpovidajici hrana B */
        if (b - per_ps >= S) b -= per_ps;                    /* prvni hrana B v segmentu muze byt ta PREDCHOZI */
        if (b < S) b += per_ps;                              /* ... nebo az NASLEDUJICI */
        const double ta = a + sig_m_ps * grand(), tb = b + sig_m_ps * grand();
        const uint32_t nm = (uint32_t)(L_ps / per_ps) - 1u;
        fpga_v3_ev_t e[2];
        e[0] = evk(0u, (uint64_t)(ta / U + 0.5), nm, per_ps / U);
        e[1] = evk(1u, (uint64_t)(tb / U + 0.5), nm, per_ps / U);
        uint32_t k = fpga_v3_ab_feed(&P, e, 2u, dt, 4u);
        for (uint32_t i = 0; i < k; i++) {
            const double ps = (double)dt[i] * U;
            sum += ps; sum2 += ps * ps; n++;
            if (ps < per_ps / 2) nlo++; else nhi++;
        }
    }
    const double m = n ? sum / n : 0.0, sd = n > 1 ? sqrt((sum2 - n * m * m) / (n - 1)) : 0.0;
    printf("delta %+7.0f ps  sigma znacky %4.0f ps | prijato %5u (u nuly %5u, u periody %5u), zahozeno %5u | "
           "NULA = stredni %10.1f ps, sigma %9.1f ps\n",
           delta_ps, sig_m_ps, (unsigned)n, (unsigned)nlo, (unsigned)nhi, (unsigned)P.lost, m, sd);
}

int main(void)
{
    const double per = 100000.0;   /* 10 MHz */
    static const double D[] = { 500.0, 200.0, 0.0, -90.0, -200.0, -500.0, -1000.0 };
    static const double SG[] = { 70.0, 250.0 };   /* sigma znacky; sigma intervalu = sqrt(2) x */
    printf("10 MHz na A i B (rozbocovac), 20000 segmentu po 25 ms, fpga_v3_ab_feed z CM7/Core/Src/fpga_v3.c\n");
    for (unsigned j = 0; j < 2; j++)
        for (unsigned i = 0; i < sizeof D / sizeof D[0]; i++) run(D[i], SG[j], per, 20000u);
    return 0;
}
