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

/* Perioda [s] PŘÍMO z reciproké dvojice. Reciproční čítač měří `T = Δt/N`, takže
 * podíl `gate_ns / (edges·mul)` je to, co přístroj SKUTEČNĚ naměřil — kdežto
 * `1/f` se počítá z kmitočtu, který už byl zaokrouhlen na 5 desetin
 * (`frequency_x100000`), a ten převod tedy zbytečně ztrácí platné číslice.
 * ⚠️ `mul` MUSÍ pocházet z `fpga_freq_hires_mul()` — `edge_count` může být počet
 * period dělené větve i neděleného signálu a kdo si násobitel odvozuje sám,
 * zopakuje chybu „×4" (viz fpga_freq.h). `mul==0` = žádný nesedí.
 * Když dvojice není použitelná, degraduje na `1/hz_fallback`.
 * @return perioda v sekundách; 0 když ji nelze určit. */
double mp_period_sample_s(uint64_t edges, uint64_t dt_ps, uint32_t mul,   /* dt_ps: F-0186 */
                          double hz_fallback);

/* Čitelná časová jednotka pro |sec|: vrací "s"/"ms"/"us"/"ns"/"ps" a do *scale
 * uloží převod, takže `hodnota_v_jednotce = sec / *scale`. `scale` smí být NULL.
 * Pro sec == 0 vrací "s". Používá okno MĚŘENÍ pro statistiku periody, aby se
 * průměr i σ zobrazily každý ve své vlastní smysluplné dekádě. */
const char *mp_time_unit(double sec, double *scale);

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
    /* 🔴 F-0159: 0 = hradlo neznámé (`gate_s <= 0`, měření neběží). Pak jsou
     * `u_res_rel`, `u_tot_*` i `digits` spočtené z NÁHRADNÍHO hradla 1 s jen
     * kvůli numerické bezpečnosti a volající je NESMÍ zobrazit jako výsledek.
     * Do 2026-09-26 příznak neexistoval a okno ANALÝZA ukazovalo „Nejistotu U"
     * i „Platných cifer" z tohoto vymyšleného hradla. Příznak cestuje S VÝSLEDKEM,
     * aby na něj volající nemohl zapomenout tak, jako zapomněl dva ze tří řádků. */
    int    valid;
} mp_budget_t;

/** Spočítá rozpočet nejistoty. `sigma_y` = σy(τ) pro τ ≈ `gate_s` (0 = neznámá,
 *  příspěvek se vynechá), `ref_ppb` = systematická nejistota reference.
 *  Při `gate_s <= 0` nastaví `valid = 0` — viz `mp_budget_t.valid`. */
void mp_budget(double hz, double gate_s, double tdc_ps, double sigma_y,
               double ref_ppb, mp_budget_t *out);

/* 🔴 JEDINÝ ZDROJ obou konstant rozpočtu nejistoty. Dřív byl krok TDC na TŘECH
 * místech (`FREQ_TDC_PS`, literál v okně ANALÝZA, testovací vektory) — přesně
 * případ pravidla „duplicitní fakta = riziko rozjetí" (SKILL.md §5).
 * ⚠️ Bydlí v `Core/Inc`, protože je vidí **i CM4** (`-I../../CM7/Core/Inc`) a
 * web je servíruje v `/api/state` — jinak by vznikla čtvrtá kopie v `httpd_min.c`.
 * ⚠️ Nová deska má carry chain ~50 ps bin (~22 ps single-shot), tedy o dva řády
 * jinde — při přechodu změnit TADY a nikde jinde. */
#define MP_TDC_PS      57.0     /* carry-chain TDC: krok jednoho tapu (STA model GW1NR-9C 57 ps; HYPOTÉZA do měření na desce, viz CAL report) */
#define MP_REF_PPB     1.0      /* systematická nejistota GPSDO reference vůči UTC */

/* ══════════════ AD8307: mV → dBm — JEDINÝ zdroj převodu (F-0165) ═══════════
 * 🔴 Do 2026-09-26 byl vzorec `dBm = mV / slope + intercept` v projektu 8×
 * (5× app_gpsdo.c, 2× scpi.c, 1× httpd_min.c) a pro neplatnou strmost měly
 * kopie TŘI politiky: „nevím" (web, `MEAS:POW?`), tiše dosadit 25 mV/dB
 * (`MMEM:DATA?`, tři kopie v UI) a žádnou pojistku (dvě kopie) — plus dva různé
 * prahy (`> 1.0` vs `< 1e-3`). Export datalogu tak při rozbité kalibraci vyrobil
 * věrohodné dBm, zatímco `MEAS:POW?` poctivě řekl „nevím".
 * Politika je teď JEDNA: neplatná strmost → „nevím" (rozhodnutí uživatele).
 * ⚠️ `static inline` v hlavičce, NE funkce v `.c`: hlavičku vidí i CM4
 * (`-I../../CM7/Core/Inc`), kde se `meas_present.c` nepřekládá.
 * ⚠️ Forma `!(slope > MIN && slope < MAX)` chytí i NaN. */
#define MP_AD8307_SLOPE_MIN   1.0f      /* mV/dB — pod tím kalibrace neplatí */
#define MP_AD8307_SLOPE_MAX   1000.0f   /* typicky 25 mV/dB; nad tím nesmysl */

static inline int mp_ad8307_slope_ok(float slope)
{
    return (slope > MP_AD8307_SLOPE_MIN && slope < MP_AD8307_SLOPE_MAX) ? 1 : 0;
}

/** Převod syrového napětí AD8307 [mV] na úroveň [dBm].
 *  @return 1 = platné (`*dbm` zapsáno), 0 = strmost neplatná → volající MUSÍ
 *          zobrazit „nevím" (`--` / `null` / SCPI `9.91E37`), ne náhradní číslo. */
static inline int mp_ad8307_dbm(float mv, float slope, float intercept, float *dbm)
{
    if (!mp_ad8307_slope_ok(slope)) return 0;
    if (dbm) *dbm = mv / slope + intercept;
    return 1;
}

/* ══════════════ Lineární proklad (drift / aging / tempco) ═══════════════════
 * Jeden estimátor pro dvě různé úlohy (#3 drift v čase, #4 tempco vůči teplotě)
 * — je to táž matematika, jen jiná osa X. Obyčejná least-squares přímka
 * y = a + b·x + Pearsonův korelační koeficient r.
 * ⚠️ `r` je tu důležitější než `b`: bez něj nepoznáš, jestli spočtená směrnice
 * něco znamená, nebo je to proklad šumu. O průkaznosti rozhoduje
 * `mp_fit_significant` (t-test) — NE pevný práh na |r|, protože stejné r je při
 * 200 bodech vysoce průkazné a při 4 bodech vůbec (F-0169).
 * ⚠️ Akumulátory jsou CENTROVANÉ na první bod (x0, y0). Bez toho je vzorec
 * `n·Σxx − (Σx)²` numericky nestabilní: pro drift kmitočtu (x = unix čas
 * ~1,76e9 s, y ≈ 1e7 Hz) se rozptyl y odečtením dvou obřích čísel propadl na
 * ≤ 0 a dokonalá přímka vyšla jako r = 0 „neprůkazné" (F-0169, ověřeno na hostu). */
typedef struct {
    uint32_t n;
    double   a, b;                 /* výsledek: y = a + b·x (platný po mp_fit_solve) */
    double   r;                    /* Pearson, -1..+1 */
    double   x0, y0;               /* střed akumulace = první bod (F-0169) */
    double   sx, sy, sxx, syy, sxy;/* akumulátory (x-x0, y-y0) — paměť O(1) */
    /* F-0173: lag-1 autokorelace reziduí, v POŘADÍ PŘIDÁVÁNÍ bodů (= čas).
     * Počítá se jedním průchodem ze součtů sousedních dvojic (reziduum je
     * lineární v X, Y, takže Σe_i·e_{i-1} jde rozepsat na tyto součty) —
     * ANALÝZA čte body z logu jen jednou (blokující QSPI). */
    double   px, py;               /* předchozí bod (centrovaný) */
    double   l_yy, l_xx, l_xy, l_yx;       /* ΣY_i·Y_{i-1}, ΣX_i·X_{i-1}, ΣX_i·Y_{i-1}, ΣY_i·X_{i-1} */
    double   l_ylead, l_ylag, l_xlead, l_xlag;  /* ΣY_i, ΣY_{i-1}, ΣX_i, ΣX_{i-1} (i >= 1) */
    double   rho;                  /* výsledek: lag-1 autokorelace reziduí (po mp_fit_solve) */
} mp_fit_t;

void mp_fit_reset(mp_fit_t *f);
void mp_fit_add(mp_fit_t *f, double x, double y);
/** Dopočítá a/b/r z akumulátorů. @return 1 = použitelné (n>=3 a rozptyl X > 0). */
int  mp_fit_solve(mp_fit_t *f);
/** Je směrnice statisticky průkazná? Dvoustranný t-test korelace na hladině 5 %:
 *  t = |r|·√df/√(1−r²) proti kritické hodnotě pro df (tabulka do 30, nad tím
 *  konzervativně 2,042). Volat až po úspěšném `mp_fit_solve`.
 *  🔴 F-0173: df = ⌊n_eff⌋ − 2, kde n_eff = n·(1−ρ)/(1+ρ) pro ρ > 0 (lag-1
 *  autokorelace reziduí; Santer et al. 2000). S df = n − 2 test předpokládal
 *  NEZÁVISLÁ rezidua — Vc a teplota z datalogu ale putují pomalu, a simulace
 *  (`docs/audit/sim/2026-09-26_ttest_autokorelace.js`) ukázala při 200 bodech
 *  bez driftu falešný „průkazný drift" v 63 % (AR(1) ρ = 0,9) a 91 % (náhodná
 *  procházka); s n_eff 7 % a 28 %. ⚠️ Náhodnou procházku přímka principiálně
 *  od driftu neodliší — „průkazné" tedy znamená „neodpovídá bílému ani slabě
 *  korelovanému šumu", ne „prokázaný lineární drift".
 *  Webové dvojče `fitSig` (SPA) musí dávat totéž (`tools/spa/stat_test.js`).
 *  @return 1 = průkazná, 0 = neprůkazná (nebo málo bodů / NaN). */
int  mp_fit_significant(const mp_fit_t *f);

/* Pure-logic unit test (perioda/nominál/jednotky/statistika/TFOM/filtr/
 * rozpočet nejistoty/proklad) — 1 = PASS. */
int mp_selftest(void);

#ifdef __cplusplus
}
#endif
