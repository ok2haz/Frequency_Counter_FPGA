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

/* ══════════════ Filtr měření (potlačení výstřelků) ══════════════════════════
 * Standardní výbava továrních čítačů: zobrazovaná hodnota se filtruje přes
 * posledních N měření. Dvě různé role:
 *   PRŮMĚR  — sníží šum o √N, ale JEDINÝ výstřelek se rozprostře do všech N
 *             následujících hodnot,
 *   MEDIÁN  — výstřelek zahodí úplně (robustní statistika), ale šum nesníží.
 * Proto obojí, ne jen jedno: na šum průměr, na rušení medián.
 * ⚠️ Filtr mění POUZE zobrazovanou hodnotu. Do statistiky (Allan/histogram/
 * datalog) jdou dál SYROVÁ měření — filtrovaná data by σy(τ) uměle vylepšila
 * a to je přesně ten druh čísla, kterému by se pak nedalo věřit. */
typedef enum { MP_FILT_OFF = 0, MP_FILT_AVG, MP_FILT_MED, MP_FILT_N } mp_filt_t;

#define MP_FILT_MAX   16          /* strop okna (RAM i doba doběhu) */

typedef struct {
    double   buf[MP_FILT_MAX];
    uint8_t  n;                   /* kolik je zaplněno */
    uint8_t  head;
    uint8_t  win;                 /* šířka okna 1..MP_FILT_MAX */
    mp_filt_t mode;
} mp_filt_state_t;

void        mp_filt_reset(mp_filt_state_t *f, mp_filt_t mode, uint8_t win);
/** Přidá vzorek a vrátí filtrovanou hodnotu (při MP_FILT_OFF vrací vstup). */
double      mp_filt_add(mp_filt_state_t *f, double x);
const char *mp_filt_label(mp_filt_t m);   /* "VYP" / "PRUM" / "MED" */

/* ══════════════ Rozpočet nejistoty (#51/#2) + rozlišení hradla (#7) ═════════
 * Tovární čítač u výsledku vždycky řekne, JAK MOC MU VĚŘIT. Skládá se ze tří
 * nezávislých příspěvků, které se sčítají kvadraticky (nekorelované zdroje):
 *   1) ROZLIŠENÍ hradla — reciproční čítač s TDC krokem `tdc_ps` a hradlem
 *      `gate_s` má kvantizační chybu ~ tdc/gate (relativně). Sečteny obě hrany,
 *      proto √2.
 *   2) STABILITA reference — σy(τ) při τ = délka hradla (z Allanovy pyramidy).
 *   3) PŘESNOST reference — systematický ppb offset GPSDO vůči UTC.
 * ⚠️ Vrací STANDARDNÍ nejistotu (k=1); UI z ní dělá rozšířenou U = k·u (k=2,
 * ~95 %) — a MUSÍ u čísla uvést které k, jinak je údaj nejednoznačný. */
typedef struct {
    double u_res_rel;    /* rozlišení hradla [relativně] */
    double u_sta_rel;    /* stabilita reference (σy@τ) [relativně] */
    double u_ref_rel;    /* přesnost reference [relativně] */
    double u_tot_rel;    /* kvadratický součet [relativně] */
    double u_tot_hz;     /* totéž v Hz při daném kmitočtu */
    int    digits;       /* kolik číslic výsledku je smysluplných */
} mp_budget_t;

/** Spočítá rozpočet nejistoty. `sigma_y` = σy(τ) pro τ ≈ `gate_s` (0 = neznámá,
 *  příspěvek se vynechá), `ref_ppb` = systematická nejistota reference. */
void mp_budget(double hz, double gate_s, double tdc_ps, double sigma_y,
               double ref_ppb, mp_budget_t *out);

/* ══════════════ Lineární proklad (drift / aging / tempco) ═══════════════════
 * Jeden estimátor pro dvě různé úlohy (#3 drift v čase, #4 tempco vůči teplotě)
 * — je to táž matematika, jen jiná osa X. Obyčejná least-squares přímka
 * y = a + b·x + Pearsonův korelační koeficient r.
 * ⚠️ `r` je tu důležitější než `b`: bez něj nepoznáš, jestli spočtená směrnice
 * něco znamená, nebo je to proklad šumu. |r| < ~0,5 → směrnici neinterpretovat. */
typedef struct {
    uint32_t n;
    double   a, b;                 /* výsledek: y = a + b·x (platný po mp_fit_solve) */
    double   r;                    /* Pearson, -1..+1 */
    double   sx, sy, sxx, syy, sxy;/* akumulátory — paměť O(1) i pro tisíce vzorků */
} mp_fit_t;

void mp_fit_reset(mp_fit_t *f);
void mp_fit_add(mp_fit_t *f, double x, double y);
/** Dopočítá a/b/r z akumulátorů. @return 1 = použitelné (n>=3 a rozptyl X > 0). */
int  mp_fit_solve(mp_fit_t *f);

/* Pure-logic unit test (perioda/nominál/jednotky/statistika/TFOM/filtr/
 * rozpočet nejistoty/proklad) — 1 = PASS. */
int mp_selftest(void);

#ifdef __cplusplus
}
#endif
