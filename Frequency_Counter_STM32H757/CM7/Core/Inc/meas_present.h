#pragma once
/**
 * @file    meas_present.h
 * @brief   Prezentace měření (SW cluster, STATUS.md #67): perioda, odchylka od
 *          nominálu ve volitelných jednotkách (Hz/ppm/ppb/ppt/rel), automatický
 *          nominál, statistika N vzorků (mean/σ/min/max/p-p) a TFOM.
 *
 * Čistě logická vrstva — žádný HW, žádný sdílený stav uvnitř funkcí. Idiom
 * projektu: testuje se na targetu přes `selftest` (`mp_selftest`, 11. test).
 * Zdrojem kmitočtu je `screen_main_freq_hz()` (dnes SIMULACE headline — plný
 * smysl po reálném SPI linku #2, stejně jako Math/limity #43/#44).
 *
 * Zobrazuje okno MĚŘENÍ (s_view=34) — viz app_gpsdo.c. TFOM lze ukázat i v Holdoveru.
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Jednotky odchylky od nominálu. */
typedef enum {
    MP_UNIT_HZ = 0, MP_UNIT_PPM, MP_UNIT_PPB, MP_UNIT_PPT, MP_UNIT_REL, MP_UNIT_N
} mp_unit_t;

/* Running statistika (Welford pro mean/variance + min/max). Bez pole vzorků. */
typedef struct {
    uint32_t n;
    double   mean;    /* running průměr */
    double   m2;      /* Σ(x-mean)² akumulace (Welford) */
    double   min, max;
} mp_stats_t;

void   mp_stats_reset(mp_stats_t *s);
void   mp_stats_add(mp_stats_t *s, double x);
double mp_stats_sd(const mp_stats_t *s);    /* výběrová směr. odchylka (n-1); 0 pro n<2 */
double mp_stats_p2p(const mp_stats_t *s);   /* peak-to-peak = max-min; 0 pro n==0 */

/* Perioda [s] z kmitočtu [Hz] (0 pro f<=0). */
double mp_period_s(double hz);

/* Nejbližší „kulatý" nominál k f (mantisa snap na 1/2/2.5/5/10 × 10^n); 0 pro f<=0. */
double mp_nominal_auto(double hz);

/* Odchylka f od nominálu v dané jednotce. rel = (f-nom)/nom; HZ = f-nom.
 * Guard: nominal==0 → vrací 0 pro relativní jednotky (HZ vrací f-0=f). */
double      mp_deviation(double hz, double nominal, mp_unit_t unit);
const char *mp_unit_label(mp_unit_t u);     /* "Hz"/"ppm"/"ppb"/"ppt"/"rel" */

/* TFOM (Time Figure of Merit) 0..9, nižší = lepší (konvence GPSDO/NMEA).
 * ⚠️ ODHAD z kvality GPS + holdover/warmup — skutečná časová chyba až po 1PPS TIC
 * (#36). Vstup: gps_valid, fix_mode (0/2/3), sats, hdop, holdover(1=bez GPS po locku),
 * warmup(1=OCXO ještě nestabilní). */
typedef struct { uint8_t level; const char *label; } mp_tfom_t;
mp_tfom_t mp_tfom(int gps_valid, int fix_mode, int sats, float hdop,
                  int holdover, int warmup);

/* Pure-logic unit test (perioda/nominál/jednotky/statistika/TFOM) — 1 = PASS. */
int mp_selftest(void);

#ifdef __cplusplus
}
#endif
