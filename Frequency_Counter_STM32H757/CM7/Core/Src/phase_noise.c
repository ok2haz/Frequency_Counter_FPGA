/**
 * @file    phase_noise.c
 * @brief   Odhad fazoveho sumu L(f) z FFT frakcnich fluktuaci kmitocty (#45).
 *          Viz phase_noise.h pro vztahy a omezeni.
 */
#include "phase_noise.h"
#include <math.h>
#include <string.h>

/* ── Radix-2 iterativni FFT (Cooley-Tukey, in-place, decimation-in-time) ──────
 * N musi byt mocnina 2. re/im prepsany spektrem. Twiddly z sin/cos — pocita se
 * jen na vyzadani (vstup do okna), takze cena libm je bezvyznamna. */
static void pn_fft(double *re, double *im, int n)
{
    /* bit-reversal permutace */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t;
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / (double)len;
        double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;              /* w^0 */
            for (int k = 0; k < len / 2; k++) {
                double ur = re[i + k],           ui = im[i + k];
                double vr = re[i + k + len / 2], vi = im[i + k + len / 2];
                /* v *= w */
                double tr = vr * cr - vi * ci;
                double ti = vr * ci + vi * cr;
                re[i + k]           = ur + tr; im[i + k]           = ui + ti;
                re[i + k + len / 2] = ur - tr; im[i + k + len / 2] = ui - ti;
                /* w *= wlen */
                double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

int pn_compute(const float *y, int n, double f0_hz, double fs_hz,
               pn_point_t *out, int max_pts)
{
    if (y == NULL || out == NULL || max_pts <= 0) return 0;
    if (n < PN_NFFT || f0_hz <= 0.0 || fs_hz <= 0.0) return 0;

    double re[PN_NFFT], im[PN_NFFT];

    /* F-0175: WELCH — prumer periodogramu segmentu po PN_NFFT s 50% prekryvem,
     * od nejnovejsiho dozadu. Do 2026-09-26 se bral JEDINY periodogram poslednich
     * 64 vzorku: jeho odhad ma chi^2 se 2 stupni volnosti, tedy rozptyl ~+-5,6 dB,
     * a okno ANALYZA ho tisklo na 0,1 dB. Web prumeroval segmenty -> dve cisla.
     * Pri STAT_N = 120 vzorcich vyjdou 2 segmenty (offset n-64 a n-96).
     * Mezisoucet vykonu se drzi primo v `out[].l_dbc` (zadny dalsi zasobnik —
     * pn_selftest bezi i na defaultTasku se 2560 B). */
    int nb = PN_NFFT / 2 - 1;
    if (nb > max_pts) nb = max_pts;
    for (int k = 0; k < nb; k++) out[k].l_dbc = 0.0;
    int nseg = 0;
    double wpow = 0.0;
    for (int off = n - PN_NFFT; off >= 0 && nseg < PN_MAX_SEG; off -= PN_NFFT / 2) {
        /* 🔴 F-0161: ODECET STREDNI HODNOTY pred oknem.
         * `y = (f - f0)/f0`, kde f0 je JMENOVITY kmitocet, takze `y` nese
         * stejnosmernou slozku = kmitoctovy offset oscilatoru. Ta neni fazovy sum —
         * a pres okno prosakne do nizkych binu. Do 2026-09-26 se neodecitala:
         * u symetrickeho Hanna vychazel unik DC do binu 6 (0,094 Hz, ten co ukazuje
         * okno ANALYZA) -66,7 dB, do binu 1 -5,8 dB. Dnes to maskuje rozliseni TDC
         * 2,5 ns; s novou deskou (~22 ps) by L(f) vysel o 10-30 dB horsi a meril by
         * offset, ne sum.
         * ⚠️ ZAMERNE JEN PRUMER, NE LINEARNI TREND. Zkouseno a zamitnuto pozitivni
         * kontrolou: ton s celym poctem period ma nenulovou projekci na rampu
         * (Σ(i-i_c)·cos = -N/2), takze proklad primkou z tonu kus "ukousne" a
         * vyrobi vlastni artefakt — v testu bin 1 jen -14,9 dB pod spickou tonu.
         * A skutecny drift je v 64s okne zanedbatelny: starnuti OCXO ~1e-10/den dava
         * ~7e-14 na okno. Kdyby se nekdy merilo neco s rychlou rampou, patri to do
         * samostatneho, zdokumentovaneho kroku, ne sem potichu. */
        double ym = 0.0;
        for (int i = 0; i < PN_NFFT; i++) ym += (double)y[off + i];
        ym /= (double)PN_NFFT;

        /* Hannovo okno na kazdem segmentu (potlaci leakage; bez nej by
         * jedna spicka rozmazala cely spektralni odhad). Σw² = vykon okna pro
         * spravnou normalizaci PSD.
         * ⚠️ PERIODICKY Hann (`2πi/N`), NE symetricky (`2πi/(N-1)`): symetricky je
         * na navrh filtru, pro spektralni odhad ma DFT s unikem i daleko od DC
         * (F-0161). Periodicky ma jen 3 nenulove DFT koeficienty, takze za binem 1
         * je unik presne nulovy. */
        wpow = 0.0;
        for (int i = 0; i < PN_NFFT; i++) {
            double w = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)PN_NFFT));
            re[i] = w * ((double)y[off + i] - ym);
            im[i] = 0.0;
            wpow += w * w;
        }
        pn_fft(re, im, PN_NFFT);
        for (int k = 1; k <= nb; k++) out[k - 1].l_dbc += re[k] * re[k] + im[k] * im[k];
        nseg++;
    }

    /* Jednostranne PSD frakcni frekvence (prumer pres segmenty) a prevod na L(f). */
    double norm = 2.0 / (fs_hz * wpow * (double)nseg);   /* jednostranne (mimo DC/Nyquist) */
    int m = 0;
    for (int k = 1; k <= nb; k++) {
        double p    = out[k - 1].l_dbc;          /* soucet vykonu pres segmenty */
        double sy   = norm * p;                  /* Sy(f_k) [1/Hz] */
        double fk   = (double)k * fs_hz / (double)PN_NFFT;
        double sphi = (f0_hz / fk) * (f0_hz / fk) * sy;   /* Sφ = (f0/f)²·Sy */
        out[m].f_hz  = fk;
        /* L(f) = 10·log10(Sφ/2); guard proti log10(0) u nuloveho binu. */
        out[m].l_dbc = (sphi > 0.0) ? 10.0 * log10(0.5 * sphi) : -400.0;
        m++;
    }
    return m;
}

/* ── Selftest ─────────────────────────────────────────────────────────────────
 * Cistě logicke overeni: (1) n<NFFT -> 0 bodu; (2) FFT najde spravny bin cistého
 * tonu; (3) rostouci osa f; (4) (f0/f)² prevod: zdvojnasobeni f0 zvedne L o
 * 20·log10(2) ≈ 6,02 dB na stejnem binu. */
int pn_selftest(void)
{
    /* ⚠️ Pole `static`, NE na stacku: pn_selftest bezi z run_selftests() i pri
     * bootu na defaultTask stacku (2560 B). Na stacku ma frame 1576 B a jeste
     * vola pn_compute (1144 B) + pn_fft (160 B) = ~2880 B -> pretecen stack ->
     * HardFault -> NVIC_SystemReset -> BOOT LOOP. run_selftests neni reentrantni
     * (ma vlastni zamek), takze `static` je bezpecne — stejny vzor jako
     * gps_selftest / scpi_selftest / ipc_selftest. */
    static pn_point_t pts[PN_NBINS];
    static pn_point_t pts2[PN_NBINS];
    static pn_point_t pts3[PN_NBINS];
    static float few[PN_NFFT - 1];
    static float y[PN_NFFT];
    static float yo[PN_NFFT];

    /* (1) malo dat */
    for (int i = 0; i < PN_NFFT - 1; i++) few[i] = 0.0f;
    if (pn_compute(few, PN_NFFT - 1, 1e7, 1.0, pts, PN_NBINS) != 0) return 0;

    /* Cisty ton na binu k0: y[n] = A·cos(2π k0 n / N). */
    const int   k0 = 8;
    const double A = 2e-9;
    for (int i = 0; i < PN_NFFT; i++)
        y[i] = (float)(A * cos(2.0 * M_PI * (double)k0 * (double)i / (double)PN_NFFT));

    int np = pn_compute(y, PN_NFFT, 1e7, 1.0, pts, PN_NBINS);
    if (np != PN_NFFT / 2 - 1) return 0;

    /* (3) osa f rostouci + spravne hodnoty f_k = k·fs/N */
    for (int i = 0; i < np; i++) {
        double expf = (double)(i + 1) * 1.0 / (double)PN_NFFT;
        if (fabs(pts[i].f_hz - expf) > 1e-9) return 0;
        if (i > 0 && pts[i].f_hz <= pts[i - 1].f_hz) return 0;
    }

    /* (2) argmax L(f) na binu k0 (index k0-1, protoze biny zacinaji od k=1).
     * Hannovo okno muze spicku rozlit o +-1 bin -> tolerance 1. */
    int imax = 0; double lmax = pts[0].l_dbc;
    for (int i = 1; i < np; i++) if (pts[i].l_dbc > lmax) { lmax = pts[i].l_dbc; imax = i; }
    if (imax < (k0 - 1) - 1 || imax > (k0 - 1) + 1) return 0;

    /* (4) f0 x2 -> +6,02 dB na stejnem binu (overeni (f0/f)² clenu). `pts2` viz static nahore. */
    int np2 = pn_compute(y, PN_NFFT, 2e7, 1.0, pts2, PN_NBINS);
    if (np2 != np) return 0;
    double d = pts2[imax].l_dbc - pts[imax].l_dbc;
    if (fabs(d - 20.0 * log10(2.0)) > 0.1) return 0;

    /* (5) F-0161: tentyz ton + VELKY kmitoctovy offset. Offset 1e-7 je 50x nad
     * amplitudou tonu — pomer nedisciplinovaneho OCXO proti jemnemu TDC. Puvodni
     * kod (bez odectu prumeru, symetricky Hann) daval bin k=1 +54 dB NAD
     * spickou tonu (overeno 1:1 prepisem na hostu). Po oprave: (a) spicka tonu
     * se nezmeni, (b) nejnizsi bin zustane hluboko pod ni.
     * ⚠️ Puvodni selftest tohle chytit NEMOHL — mel jen ton s nulovym prumerem
     * (L-0039: kontrola musi obsahovat vadu, kvuli ktere vznikla). */
    for (int i = 0; i < PN_NFFT; i++)
        yo[i] = y[i] + 1e-7f;
    int np3 = pn_compute(yo, PN_NFFT, 1e7, 1.0, pts3, PN_NBINS);
    if (np3 != np) return 0;
    if (fabs(pts3[k0 - 1].l_dbc - pts[k0 - 1].l_dbc) > 0.1) return 0;   /* (a) */
    if (pts3[0].l_dbc > pts3[k0 - 1].l_dbc - 20.0) return 0;            /* (b) */

    /* (6) F-0175: Welch opravdu prumeruje i STARSI segment. 96 vzorku, ton jen
     * v prvnich 32 (patri pouze do segmentu na offsetu 0), zbytek nuly. Stary
     * kod bral jen poslednich 64 (same nuly) -> -400 dB vsude; novy zapocte oba
     * segmenty (offset 32 a 0) -> na binu tonu nenulovy vykon. */
    {   static float y6[PN_NFFT + PN_NFFT / 2];
        for (int i = 0; i < PN_NFFT + PN_NFFT / 2; i++)
            y6[i] = (i < PN_NFFT / 2) ? y[i] : 0.0f;
        int np6 = pn_compute(y6, PN_NFFT + PN_NFFT / 2, 1e7, 1.0, pts3, PN_NBINS);
        if (np6 != np) return 0;
        if (!(pts3[k0 - 1].l_dbc > -300.0)) return 0;
    }

    return 1;
}
