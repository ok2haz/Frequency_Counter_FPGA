/**
 * @file    gpsdo.h
 * @brief   Smycka GPSDO: FPGA meri hranu GPS 1PPS proti 100 MHz (= OCXO), STM32 ladi OCXO pres AD5693R
 *          na presnych 10 MHz (STATUS #305; navazuje na #36, #82, #38, #277).
 *
 * Retez: FPGA `pps_cap` (FW 0x0424, hruby citac 100 MHz, BEZ TDC) -> ramec abs 52..56 -> `fpga_pps_get`
 * (shoda dvou ramcu) -> gpsdo_tick (defaultTask, 100 Hz) -> povel kodu DAC (`g_gpsdo_code_q16`, Q16.16)
 * -> SensorsTask (jediny vlastnik I2C1) zapisuje AD5693R s ditherem 1. radu (2 Hz) -> OCXO EFC.
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
    double   code;         /* povel DAC [LSB] vcetne zlomku */
    double   kdac;         /* K [relativne / LSB] */
    double   tau_s;        /* casova konstanta PLL [s] */
    uint32_t fll_win_s;    /* aktualni okno FLL [s] */
    uint32_t state_s;      /* sekund v aktualnim stavu */
    uint32_t pps_ok, pps_rej, pps_gap;   /* prijate / odmitnute intervaly 1PPS, diry (n > 1) */
    float    saw_var[3];   /* rozptyl 2. diferenci faze [ns^2]: bez opravy / +q / -q (posledni okno) */
    uint32_t saw_n;        /* pocet rozhodnuti sawtooth */
} gpsdo_status_t;

void     gpsdo_tick(void);                 /* VYHRADNE defaultTask, ~100 Hz */
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
