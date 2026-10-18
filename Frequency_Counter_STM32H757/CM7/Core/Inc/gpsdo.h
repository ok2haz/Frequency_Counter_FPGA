/**
 * @file    gpsdo.h
 * @brief   Smycka GPSDO: FPGA meri hranu GPS 1PPS proti 100 MHz (= OCXO), STM32 ladi OCXO pres AD5693R
 *          na presnych 10 MHz (STATUS #305; navazuje na #36, #82, #38, #277).
 *
 * Retez: FPGA `pps_cap` (FW 0x0424, hruby citac 100 MHz, BEZ TDC) -> ramec abs 52..56 -> `fpga_pps_get`
 * (shoda dvou ramcu) -> gpsdo_tick (defaultTask, 100 Hz) -> povel kodu DAC (`g_gpsdo_code_q16`, Q16.16)
 * -> SensorsTask (jediny vlastnik I2C1) zapisuje AD5693R s ditherem 1. radu (DacTask, vychozi 100 Hz; zaloha 2 Hz v SensorsTasku) -> OCXO EFC.
 *
 * Mereni: za n sekund mezi dvema hranami nabehne n * 1e8 * (1 + y) taktu, y = relativni odchylka OCXO
 * vuci GPS (kladne = OCXO rychly). Chyba periody e = takty - n * 1e8 [takt = 10 ns], faze x = soucet e.
 * Sawtooth (F3): od e se odecita zmena kvantizacni chyby hrany z UBX-TIM-TP; znamenko a to, ze oprava
 * vubec pomaha, si smycka OVERUJE SAMA (rozptyl druhych diferenci) -- z dokumentace v repu doloženo neni.
 *
 * Stavy: OFF (vypnuto) -> WAIT (chybi DAC/1PPS/fix/hodiny FPGA) -> FLL (rozbeh: prumer kmitoctu v okne
 * 16 -> 256 s, krok kodu -y/K) -> LOCK (PLL 2. radu na fazi, tau vychozi 1000 s) -> HOLD (ztrata 1PPS:
 * drzi posledni kod, po navratu pokracuje). K = relativni zmena kmitoctu OCXO na 1 LSB DAC (namereno
 * 2026-10-16: 4,3-4,6 Hz/V x 76,3 uV/LSB -> ~3,4e-11; kladne = vyssi kod, rychlejsi OCXO).
 *
 * Vlakna: gpsdo_tick a cely stav VYHRADNE defaultTask; SensorsTask cte jen `g_gpsdo_drive`/`_code_q16`
 * (32bitove, atomicke). UART/UI meni nastaveni pres gpsdo_enable/_set_* (prosty zapis, prevezme se
 * v dalsim tiku). ⬜ NEOVERENO NA HW (kod 2026-10-16).
 */
#ifndef GPSDO_H
#define GPSDO_H

#include <stdint.h>

enum {
    GPSDO_OFF = 0,     /* vypnuto uzivatelem (nebo po divergenci) -- DAC se neridi */
    GPSDO_WAIT,        /* zapnuto, ale chybi vstup (viz `why`) -- DAC se neridi */
    GPSDO_FLL,         /* rozbeh: ladeni podle prumerneho kmitoctu */
    GPSDO_LOCK,        /* PLL na fazi 1PPS */
    GPSDO_HOLD         /* holdover: 1PPS chybi, drzi se posledni kod */
};

/* Duvod stavu WAIT/OFF (`why`). */
enum {
    GPSDO_WHY_NONE = 0,
    GPSDO_WHY_NODAC,       /* AD5693R nenalezen / bez control registru */
    GPSDO_WHY_NOPPS,       /* FPGA nehlasi 1PPS (FW < 0x0424 nebo zadna hrana) */
    GPSDO_WHY_NOFIX,       /* GPS bez fixu (bez fixu chodi 10 Hz, ne 1PPS) */
    GPSDO_WHY_REFEXT,      /* FPGA bezi z externi reference -- neciti se OCXO */
    GPSDO_WHY_CLK,         /* hodiny FPGA hlasi vypadek / zotaveni */
    GPSDO_WHY_DIVERGE,     /* FLL: odchylka po kroku rostla -> zastaveno (znamenko K?) */
    GPSDO_WHY_USER         /* vypnuto uzivatelem (`gpsdo off`, rucni `dac`) */
};

/* Rezim opravy sawtooth (`saw`). */
enum { GPSDO_SAW_OFF = 0, GPSDO_SAW_PLUS, GPSDO_SAW_MINUS, GPSDO_SAW_NODATA };

typedef struct {
    uint8_t  state;        /* GPSDO_* */
    uint8_t  why;          /* GPSDO_WHY_* */
    uint8_t  locked;       /* 1 = LOCK a |faze| < 200 ns a |y| < 1e-9 aspon 60 s */
    uint8_t  saw;          /* GPSDO_SAW_* (rozhodnuto naposledy) */
    double   y;            /* posledni odhad relativni odchylky OCXO (FLL: okno, LOCK: filtr) */
    double   x_ns;         /* filtrovana fazova chyba [ns] (LOCK) */
    double   x_raw_ns;     /* SUROVA fazova chyba (TIE) [ns] od zamceni / navratu z HOLD; v OFF pasivni monitor od vypnuti (STATUS #319); jinak 0 */
    double   code;         /* povel DAC [LSB] vcetne zlomku */
    double   kdac;         /* K [relativne / LSB] */
    double   tau_s;        /* casova konstanta PLL [s] */
    uint32_t fll_win_s;    /* aktualni okno FLL [s] */
    uint32_t state_s;      /* sekund v aktualnim stavu */
    uint32_t pps_ok, pps_rej, pps_gap;   /* prijate / odmitnute intervaly 1PPS, diry (n > 1) */
    float    saw_var[3];   /* rozptyl 2. diferenci faze [ns^2]: bez opravy / +q / -q (posledni okno) */
    uint32_t saw_n;        /* pocet rozhodnuti sawtooth */
} gpsdo_status_t;

/* ── Historie smycky (TIE graf, okno TI/GPSDO na displeji): 1 bod / 10 s, 720 bodu = 2 h ────────────────
 * Zapisuje VYHRADNE defaultTask (gpsdo_tick), cte UiTask pres gpsdo_hist_at (bez zamku -- zapis meni jen
 * jeden slot, roztrzeny bod pri precteni by byl nanejvys jednou vadny pixel). `x_ns` je surova fazova chyba
 * smycky (platna jen ve stavu LOCK), `y_ppt` odhad odchylky OCXO [1e-12], `code_q16` povel DAC [Q16.16]. */
#define GPSDO_HIST_N         720u
#define GPSDO_HIST_PERIOD_S  10u
typedef struct { int16_t x_ns; int32_t y_ppt; uint32_t code_q16; uint8_t state; } gpsdo_hist_pt_t;
uint16_t gpsdo_hist_count(void);                       /* 0..GPSDO_HIST_N */
int      gpsdo_hist_at(uint16_t i, gpsdo_hist_pt_t *p); /* i = 0 nejstarsi; @return 0 = mimo rozsah */

/* ── NEJKRATSI rozumna casova konstanta smycky z mereni (STATUS #308 C; jen ZOBRAZENI, smycku to nemeni) ───
 * Optimalni tau PLL lezi na prusecku sumu 1PPS a sumu OCXO. Z dvojice OCXO–GPS ale NEJDE oddelit sum OCXO
 * od putovani casu GPS (multipath, ionosfera) — oboje se ve fazi projevi stejne. Proto se pocita jen
 * DOLNI MEZ: tau, od ktereho se ve fazi objevi prebytek nad bilym sumem 1PPS. Je-li prebytek z OCXO,
 * optimum lezi prave tam; je-li z GPS, lezi vys. (Nocni beh 2026-10-17: prebytek ~4e-11 pri 100 s, coz
 * je rad nad tim, co OCXO teto tridy obvykle ma — spis GPS. ⬜ HYPOTEZA; rozhodne treti reference.)
 * Pocita se CTENIM historie (posledni souvisly usek LOCK, 1 bod / 10 s), gpsdo_tick se nemeni:
 *  - a = HDEV faze pri 10 s x 10 s (predpoklad: na 10 s prevlada bily PM 1PPS, HDEV = a/tau);
 *  - tau <= tau_smycky/3 (vys smycka fazi drzi): mez = kde HDEV faze dosahne sqrt(2) x a/tau, interpolace
 *    log-log mezi body mrizky. HDEV (ne ADEV): linearni drift OCXO by ADEV zvedl, PLL 2. radu ho ale
 *    sleduje bez chyby kmitoctu. HEURISTIKA, ⬜ neovereno na desce. */
#define GPSDO_ADV_NT 8u                       /* mrizka tau: 10 20 50 100 200 500 1000 2000 s */
typedef struct {
    uint8_t  ok;          /* 1 = spocteno (dost bodu LOCK) */
    uint8_t  bound;       /* 0 = prebytek nalezen (tau_rec = mez), 1 = zadny prebytek do tau_smycky/3
                             (mez = posledni hodnocene tau), 2 = malo dat (< 30 min LOCK) */
    uint16_t n;           /* bodu LOCK pouzitych (po 10 s) */
    float    pps10;       /* HDEV sumu 1PPS pri 10 s (= a / 10 s) */
    float    tau_rec;     /* nejkratsi rozumne tau PLL [s] (orezano na 100..20000); 0 = nic */
    float    sx[GPSDO_ADV_NT];  /* HDEV z faze (0 = nespocteno) */
} gpsdo_tau_adv_t;
void     gpsdo_tau_advise(gpsdo_tau_adv_t *out);   /* UiTask / UartTask; vysledek se drzi 10 s (cache) */

void     gpsdo_tick(void);                 /* VYHRADNE defaultTask, ~100 Hz */
/* Nejistota reference [ppb] podle stavu smycky z ZMERENYCH hodnot (FLL: |y| okna, LOCK: ADEV faze, HOLD: ADEV + drift kodu DAC), jinak
 * z nahradni tabulky (meas_present.h). `measured` (nepovinne) = 1 pro zmereno. Smi volat kdokoli. */
double   gpsdo_ref_ppb(uint8_t *measured);
void     gpsdo_get(gpsdo_status_t *out);
void     gpsdo_enable(int on);             /* on = 1: zapnout (z OFF jde pres WAIT); 0: vypnout, DAC se uz neridi */
int      gpsdo_enabled(void);
void     gpsdo_set_tau(uint32_t tau_s);    /* 100..20000 s */
void     gpsdo_set_kdac(double k);         /* 1e-12..1e-9 relativne / LSB */
const char *gpsdo_state_name(uint8_t st);
const char *gpsdo_why_text(uint8_t why);
/* Emulator 1PPS (fpga_freq.c) ladi OCXO podle kodu, ktery by skutecne platil: realny DAC, je-li, jinak povel. */
uint16_t gpsdo_sim_dac_code(void);
/* Ciste logicky test regulatoru nad modelem OCXO (bez HW). @return 1 = PASS. */
int      gpsdo_selftest(void);

/* Pro SensorsTask (vlastnik I2C1/AD5693R): 1 = ridit DAC povelem, kod v Q16.16 (16 b cela cast, 16 b zlomek). */
extern volatile uint8_t  g_gpsdo_drive;
extern volatile uint32_t g_gpsdo_code_q16;
extern volatile uint8_t  g_gpsdo_lockflag;   /* 1 = LOCK (SensorsTask podle toho uklada kod do flash 1x/10 min) */

#endif /* GPSDO_H */
