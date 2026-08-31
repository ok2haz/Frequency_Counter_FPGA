#pragma once
/**
 * @file    phase_noise.h
 * @brief   Odhad fazoveho sumu L(f) z FFT frakcnich fluktuaci kmitocty (STATUS #45).
 *
 * Vstup = casova rada frakcni odchylky y(t) = (f - f0)/f0 (to, co uz sbira
 * `screen_main` do sveho ringu, 1 vzorek/s). Postup (klasicke IEEE 1139 vztahy):
 *
 *   1) jednostranne PSD frakcni frekvence:  Sy(f_k) = 2·|Y_k|² / (fs·Σw²)
 *      (Y_k = DFT okenkovaneho y; Σw² = vykon okna; faktor 2 = jednostranne,
 *       mimo DC a Nyquist)
 *   2) spektralni hustota fazoveho sumu:     Sφ(f) = (f0/f)² · Sy(f)   [rad²/Hz]
 *   3) SSB fazovy sum (male uhly):           L(f)  = 10·log10( Sφ(f)/2 )  [dBc/Hz]
 *
 * ⚠️ Cistě LOGICKA vrstva — zadny HW, zadny sdileny stav; testuje se na targetu
 *    pres `selftest` (`pn_selftest`). Pouziva double FFT (STM32H7 ma FPU s double).
 * ⚠️ Vzorkovani ~1 Hz -> Nyquist 0,5 Hz, takze jen NIZKO-offsetove L(f)
 *    (f ≈ fs/N .. fs/2). Vyssi offsety (do kHz/MHz) potrebuji gap-free
 *    timestamping z FPGA (#62) — tohle je zaklad, na kterem to pak pojede.
 * ⚠️ Dokud headline zene SIMULACE (#2), je L(f) spoctene ze simulovaneho sumu —
 *    stejna vyhrada jako u ADEV. Mechanika je spravna, cisla az s realnymi daty.
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Delka FFT = mocnina 2 (radix-2). 64 vzorku @ ~1 Hz = 64 s okno, rozliseni
 * fs/N ≈ 0,0156 Hz. Vic (128) by chtelo delsi naplneny ring (STAT_N=120). */
#define PN_NFFT   64
#define PN_NBINS  (PN_NFFT / 2 - 1)   /* pouzitelne biny k=1..N/2-1 (bez DC a Nyquistu) */

typedef struct {
    double f_hz;     /* offset od nosne [Hz] */
    double l_dbc;    /* L(f) [dBc/Hz] */
} pn_point_t;

/**
 * Spocita L(f) pro biny k=1..N/2-1 z poslednich PN_NFFT vzorku `y`.
 * @param y       frakcni fluktuace y=(f-f0)/f0 (chronologicky; PSD je na poradi
 *                invariantni, ale okno se aplikuje na poslednich PN_NFFT)
 * @param n       kolik vzorku v `y` je platnych
 * @param f0_hz   nosna [Hz] (>0)
 * @param fs_hz   vzorkovaci frekvence [Hz] (>0)
 * @param out     vystupni body (f_hz rostouci)
 * @param max_pts kapacita `out`
 * @return pocet zapsanych bodu; 0 kdyz n < PN_NFFT nebo neplatne parametry.
 */
int pn_compute(const float *y, int n, double f0_hz, double fs_hz,
               pn_point_t *out, int max_pts);

/* Pure-logic unit test (FFT korektnost + PSD normalizace + L(f) prevod). 1=PASS. */
int pn_selftest(void);

#ifdef __cplusplus
}
#endif
