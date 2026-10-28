/**
 * @file    fpga_freq.h
 * @brief   SPI2 master driver pro FPGA citac kmitoctu (FPGA = SPI slave).
 *
 * Protokol: pevny 128B full-duplex ramec (v2, FPGA_PROTOCOL_V2_NAVRH.md),
 * STM32 generuje SCK+CS. FPGA vraci posledni hotove mereni v ramci stejneho
 * prenosu. Handshake in-band pres STATUS/FLAGS. ACK (TYPE 0x06) potvrzuje
 * prijatou SEQUENCE.
 *
 * 🔴 MIGRACE v1->v2 (2026-09-30): puvodni rámec byl 64B/VERSION=0x01. Payload
 * offsety 12..59 jsou 1:1 shodne s v1 (zadna zmena parse_data), jen rámec
 * narostl na 128B a CRC pokryva 0..125 (bylo 0..61), CRC je na 126/127
 * (bylo 62/63). Nutne kvuli FPGA strane: `spi_app.v` (nova deska, dual-channel
 * bring-up) uz implementuje v2 beze zmeny — kdyby STM driver zustal na v1,
 * 64 hodinovych pulzu proti FPGA cekajici 128 by CS zvedlo v polovine ramce
 * a CRC by nikdy nesedelo (viz FPGA_PROTOCOL_V2_NAVRH.md).
 *
 * SPI: mode 0, MSB first, 8-bit, CS=PB12 active-low (manualni GPIO),
 *      bring-up ~1 MHz (viz FPGA_SCK_TARGET_HZ).
 */
#ifndef FPGA_FREQ_H
#define FPGA_FREQ_H

#include <stdint.h>
#include <stdbool.h>

/* 🔑 JEDINY zdroj pravdy o delce ramce — sdileji `fpga_freq.c` (FR_LEN) i
 * UART diagnostika (`fpgaraw`/`fpgaloop` v freertos_task_uart.c). Driv byl
 * FR_LEN soukromy v .c a UART mel vlastni `uint8_t rx[64]` — pri migraci na
 * v2 by to byl presne ten "zapomenuty sourozenec" (L-0012): HAL_SPI_
 * TransmitReceive by zapsal 128 B do 64B bufferu na zasobniku. */
#define FPGA_FRAME_LEN  128u

/* Jedno mereni (payload TYPE=0x80 DATA).
 * 4-fazove reciproke mereni, dva preddelice: pin28 /4 (primar, nejlepsi rozliseni),
 * pin27 /16 (vyssi rozsah / cross-check). freq*_x100000 = realny kmitocet x 100000
 * (delicka uz zahrnuta ve FPGA -> NENASOBIT 4 ani 16).
 * ⚠️ NA NOVE DESCE (dual-channel bring-up, FPGA_PROTOCOL_V3_NAVRH.md) je tenhle
 * 4fazovy/16-odbockovy popis uz jen historicky — `top.v` tam posila CH_A do
 * primarniho slotu a CH_B do "/16" slotu (viz poznamka u fpga_freq_format_info).
 * Pole zustavaji stejna, jen jejich VYZNAM je prechodne jiny. */
typedef struct {
    uint64_t frequency_x100000;   /* pin28 /4: kmitocet v jednotkach 1/100000 Hz (5 des. mist) */
    uint64_t edge_count;          /* pocet period v okne (pin28, diagnostika) */
    uint64_t gate_time_ns;        /* Dt okna [ns] ~250e6 — ⚠️ FLOOR z ticku 2,5 ns, presne Dt
                                   * jen pres `fpga_freq_dt_ticks` (F-0186) */
    uint64_t timestamp_ticks;     /* 10 MHz ticky */
    uint64_t freq16_x100000;      /* pin27 /16: kmitocet x 100000 (vyssi rozsah) */
    uint32_t error_flags;         /* bit0=meas err(/4), bit1=SIGNAL_LOST, bit2=overflow */
    uint32_t sequence;
    uint8_t  channel_id;
    uint8_t  measurement_status;  /* bit0=DATA_VALID, bit1=DATA_FRESH */
    uint8_t  status_flags;        /* STATUS/FLAGS byte z ramce (offset 3) */
    uint8_t  phase_status;        /* bity3:0=present[3:0] (zive faze), bity7:4=fine_seen[3:0] */
    uint8_t  status2;             /* bit0 = chyba deleni pin27 (/16) */
    /* ── v2 rozsireni (abs offset v ramci, viz FPGA_PROTOCOL_V2_NAVRH.md) ──── */
    uint16_t fw_version;          /* abs 60-61: verze bitstreamu (0 = neznama/stary FW) */
    uint16_t caps;                /* abs 62-63: bit1=SET_CONFIG, bit5=dt, bit6=CAL, bit7=REGR (bit0 window stream uz neni) */
    uint8_t  clk_status;          /* abs 64: od FW 0x041F (caps bit10) viz FPGA_CLK_*; starsi FW konstanta 0x01 */
    uint8_t  clk_loss;            /* abs 65: FW 0x041F pocet vypadku hodin FPGA od nahrani (saturuje 255); driv 0 */
    uint8_t  clk_mhz;             /* abs 99: od FW 0x0421 (caps bit12) kmitocet referencnich hodin FPGA [MHz] (dnes 100;
                                   * FW 0x0421/0x0422 bezely na 125); bez bitu (FW <= 0x0420): 100.
                                   * Jednotka casu v ramci je T_clk/16384 -> ps = jednotky * 15625 / (256 * clk_mhz) */
    /* ── FW >= 0x041F (caps bit10): samokontrola ──────────────────────────────────── */
    uint32_t build_time;          /* abs 12..15: cas sestaveni bitstreamu [unix s]; 0 = neznamy */
    uint32_t build_git;           /* abs 16..19: [27:0] git hash zdroju, [31] = neulozene zmeny */
    int8_t   ccd_p, ccd_s;        /* abs 97/98: kontrolni minus merny pocet hran okna (primarni/sekundarni slot) */
    /* ── 2026-10-03: skutecny carry-chain TDC (FW >= 0x0400, caps bit5) ──────── */
    uint64_t gate_ps;             /* PRESNE okno CH_A [ps] — jediny zdroj delky okna pro VSECHNY
                                   * vypocty (hi-res, akumulatory, statistika). Novy FW: z dt v
                                   * jednotkach T_clk/16384 (abs 118) * 15625/(256*clk_mhz); stary FW /
                                   * emulator: z gate_time_ns pres `fpga_freq_dt_ticks` (F-0186) */
    uint64_t dt_b_ps;             /* okno CH_B [ps] (abs 101..106); 0 = neznamo */
    uint32_t edges_b;             /* periody CH_B v okne (abs 108) */
    uint8_t  tdc_status;          /* abs 100: viz FPGA_TDC_* */
    /* ── 2026-10-07: regresni blok (FW >= 0x0411, caps bit7), abs 68..96 ───────────────
     * Stredni hodnoty casovych znacek VNITRNICH hran ve dvou segmentech okna (A = zacatek, B = konec).
     * Sklon primky mezi jejich stredy je kmitocet; zisk proti dvoum krajnim bodum je jen u signalu, jehoz
     * faze vuci hodinam TDC "prohazuje" (viz fpga_freq_regr_*, docs/audit/2026-10-07_tdc-architektura.md). */
    uint32_t rg_n[2];             /* pocet znacek v segmentu A/B */
    uint64_t rg_xm[2];            /* prumerny index hrany v okne * 256 */
    uint64_t rg_ym[2];            /* prumerny cas hrany * 16 [T/16384], vztazeny k ZACATKU okna */
    uint8_t  rg_ok;               /* bit0 = segment A platny, bit1 = B */
    uint8_t  regr_used;           /* 1 = `gate_ps` je EFEKTIVNI delka N / f_regr (ne dvoubodova) */
    uint64_t gate2_ps;            /* dvoubodova delka okna [ps] (diagnostika; =gate_ps, kdyz regrese neni) */
    double   f_regr_hz;           /* kmitocet z regrese [Hz]; 0 = neni platny / zamitnut */
    /* FW >= 0x0418 (caps bit8), abs 68..75: kody TDC uzaviraci (end) a zahajovaci (st) hrany okna.
     * Chyba okna = e(code_end) - e(code_st), kde e() je INL tabulky -> zaklad korekce INL. */
    uint16_t code_a_end, code_a_st, code_b_end, code_b_st;
    uint8_t  codes_ok;            /* 1 = kody jsou v ramci (caps bit8) */
    /* FW >= 0x0424 (caps bit13, FPGA_CAP_PPS), abs 52..56: GPS 1PPS (STATUS #305). Cas posledni hrany v taktech
     * hodin FPGA (100 MHz = OCXO pres Si5356, mod 2^32) a pocet hran (mod 256). FPGA je cte ZIVE (bez snimku),
     * proto se pouziva az po shode dvou po sobe jdoucich ramcu (`fpga_pps_get`). */
    uint32_t pps_ts;
    uint8_t  pps_seq;
    uint8_t  pps_ok;              /* 1 = ramec 1PPS nese (caps bit13) */
    uint16_t pps_frac;            /* v0.37.0: zlomek taktu hrany [1/16384], jen pri pps_fine */
    uint8_t  pps_fine;            /* 1 = pps_ts + pps_frac je jemny cas (v3 FW 0x0502, IDES8, krok 1,25 ns) */
    uint8_t  pps_epoch;           /* osa jemneho casu (PPS_STATUS 7:4); zmena = jina osa */
    uint8_t  presc_log2;          /* v0.38.0: log2 predelicky okna (v3 FW 0x0503); edge_count je v periodach */
    /* FW >= 0x0425 (caps bit14, FPGA_CAP_DIAG0, audit 2026-10-20): diagnostika O4 a hlidac koherence hodin.
     * abs 10 coh_st | 11 coh_cnt | 57 bub_p | 58 bub_s | 59 {ovl_s[3:0], ovl_p[3:0]} */
    uint16_t rg_xdiv;             /* audit 4: delitel xm (256 nebo 4096 podle caps bit15); 0 = 256 */
    uint8_t  lut_wait;            /* FW >= 0x0427 (abs 112 bit0/1): FPGA ceka na kalibracni tabulku kanalu A/B od STM */
    uint16_t lut_cnt;             /* abs 112[7:2]:113 = pocet prijatych LUT_WRITE od SET_CONFIG 0x06 = 0 */
    uint16_t lut_sum;             /* abs 114..115 = kontrolni soucet prijatych polozek */
    uint8_t  diag0_ok;            /* 1 = pole nize jsou platna */
    uint8_t  coh_st;              /* bit0 platne, bit1 posledni epocha OK, bit2 od nahrani chyba, bit3 cesta (0 nabezna, 1 sestupna) */
    uint8_t  coh_cnt;             /* pocet epoch 102,4 us s chybou koherence 100/10 MHz (saturuje 255; nova FPGA = 0) */
    uint8_t  bub_p, bub_s;        /* casovane hrany okna s bublinou (primarni / sekundarni slot) */
    uint8_t  ovl_p, ovl_s;        /* casovane hrany okna pri aktivnim CS SPI (0..15) */
    uint64_t dt_units;            /* surove dt primarniho okna [T_clk/16384] (fazovy zaznam: presny soucet bez zaokrouhleni) */
    /* Audit 2026-10-20 (E1, fpga_phrec): kmitocet mezi tezisti segmentu A sousednich oken (teleskopuje -> spravna ADEV) */
    uint8_t  cc_ok;               /* 1 = f_cc_hz/gate_cc_ps platne (navazujici okno s regresi) */
    double   f_cc_hz;
    uint64_t gate_cc_ps;          /* delka okna tak, aby edge_count / gate_cc_ps = f_cc_hz */
} fpga_meas_t;

/* caps bity (abs 62-63) */
#define FPGA_CAP_DT           (1u << 5)   /* ramec nese dt_a/dt_b v jednotkach T_clk/16384 */
#define FPGA_CAP_CODES        (1u << 8)   /* ramec nese kody TDC okna (abs 68..75), jen kdyz neni REGR */
#define FPGA_CAP_REGR         (1u << 7)   /* ramec nese regresni blok (abs 68..96); bit0 window stream uz neni */
#define FPGA_CAP_CHAN         (1u << 9)   /* FW >= 0x041E: SET_CONFIG 0x03 (vyber kanalu), 5 hradel, echo hradla v phase_status & 7 */
#define FPGA_CAP_REFCTL       (1u << 11)  /* FW >= 0x0420: SET_CONFIG 0x04 = vyber reference, stav v clk_status bity 3..5 */
#define FPGA_CAP_SELFCHK      (1u << 10)  /* FW >= 0x041F: identita bitstreamu (abs 12..19), hlidac hodin (64/65),
                                           * kontrolni pocitani hran (97/98) */
#define FPGA_CAP_CLK          (1u << 12)  /* FW >= 0x0421: abs 99 nese kmitocet hodin FPGA [MHz]; bez bitu = 100 MHz */
#define FPGA_CAP_DIAG0        (1u << 14)  /* FW >= 0x0425: abs 10/11 hlidac koherence, abs 57..59 bubliny + znacky pri CS, SET_CONFIG 0x05 */
#define FPGA_CAP_XM12         (1u << 15)  /* FW >= 0x0425 (audit 4): regresni xm v 1/4096 hrany (driv 1/256) */
#define FPGA_CAP_STMCAL       (1u << 4)   /* FW >= 0x0427: kalibracni tabulku TDC pocita a zapisuje STM (fpga_tdccal.c); abs 112..115 = stav zapisu */
#define FPGA_CAP_RAWREG       (1u << 3)   /* FW >= 0x0426: regresni blok nese SUROVE SOUCTY (n, x0, sum(x-x0), t0-ref, sum(t-t0)>>3) v abs
                                           * 24..43 / 66..96 / 107, deli STM (delic ve FPGA dava chybne dolni bity, L-0187); edge_count je u32 */
#define FPGA_CAP_PPS          (1u << 13)  /* FW >= 0x0424: abs 52..56 nese GPS 1PPS (cas hrany v taktech hodin + poradi) */

/* ── GPS 1PPS z FPGA (FW >= 0x0424, STATUS #305) ─────────────────────────────────────────────────
 * Udalost = nova hrana 1PPS potvrzena SHODOU dvou po sobe jdoucich ramcu ({seq, ts} stejne): FPGA bajty
 * 52..56 neskladuje do snimku, takze hrana prave behem skladani ramce muze dat roztrzenou hodnotu, ktera
 * se ve dvou ramcich nezopakuje. `first_ms` = HAL_GetTick, kdy se nova hodnota objevila POPRVE (hrana
 * nastala nejvys ~1 periodu pollu predtim -- pro prirazeni opravy sawtooth z UBX-TIM-TP). */
typedef struct {
    uint32_t ts;          /* takty hodin FPGA pri hrane (mod 2^32) */
    uint8_t  seq;         /* poradi hrany (mod 256) */
    uint8_t  clk_mhz;     /* kmitocet hodin FPGA [MHz] (z ramce) */
    uint8_t  ref_ext;     /* 1 = FPGA bezi z EXTERNI reference (cita se ext., ne OCXO) */
    uint8_t  clk_bad;     /* 1 = hodiny FPGA hlasi vypadek/zotaveni (clk_status) */
    uint32_t first_ms;    /* HAL_GetTick prvniho vyskytu teto hodnoty */
    uint32_t n;           /* poradove cislo udalosti od startu (pro konzumenta: zmena = nova udalost) */
    uint16_t frac;        /* v0.37.0: zlomek taktu [1/16384] (jen fine) */
    uint8_t  fine;        /* 1 = ts + frac je jemny cas (IDES8); prechod jemny/hruby = jina osa */
    uint8_t  epoch;       /* osa jemneho casu; zmena = jina osa (nove zamceni PLL ve FPGA) */
} fpga_pps_t;
/* Posledni potvrzena udalost 1PPS. @return false = zadna (FPGA bez caps bit13 nebo jeste zadna hrana). */
bool     fpga_pps_get(fpga_pps_t *out);
uint32_t fpga_pps_torn(void);   /* pocet zahozenych (neshodnych) cteni -- diagnostika */
/* clk_status bity (abs 64, jen s FPGA_CAP_SELFCHK) */
#define FPGA_CLK_OK           (1u << 0)   /* hodiny FPGA v poslednim okne hlidace (102 us) v poradku (kmitocet jako clk_mhz) */
#define FPGA_CLK_FAULT        (1u << 1)   /* od nahrani aspon jeden vypadek nebo zmena kmitoctu hodin */
#define FPGA_CLK_RECOVER      (1u << 2)   /* po vypadku: FPGA drzi mereni a znovu kalibruje TDC */
/* FW >= 0x0420 (caps bit11, FPGA_CAP_REFCTL): vyber reference OCXO / externi 10 MHz (pin Ref_Ctrl, prepinac U19) */
#define FPGA_CLK_REF_EXT      (1u << 3)   /* vybrana EXTERNI reference (jinak OCXO) */
#define FPGA_CLK_EXT_PRESENT  (1u << 4)   /* na externim vstupu (Ext_Ref_Sens) jsou hrany */
#define FPGA_CLK_EXT_LOST     (1u << 5)   /* externi vypadla, kdyz byla vybrana: FPGA SAMA prepnula zpet na OCXO */

/* ── Samokontrola okna (STATUS #282/#283) ─────────────────────────────────────
 * Do 2026-10-10 FPGA, ktera merila spatne (L-0141: 1 578 628 Hz misto 10 000 008 Hz v 6 ze 7 nahrani),
 * posilala ramce, ktere vypadaly zdrave -- CRC, VALID, SEQUENCE v poradku -- a spatne cislo slo na displej,
 * do statistiky i datalogu. Tyto kontroly okno, ktere nesedi, ZAMITNOU (stejne jako miscount: nezobrazi se,
 * nejde do statistiky) a zapocitaji ho do `status`. */
#define FPGA_CHK_OK       0
#define FPGA_CHK_COUNT    1   /* kontrolni pocitani hran FPGA nesedi s mernym (|ccd| > 1) */
#define FPGA_CHK_CLOCK    2   /* hlidac 100 MHz: neni OK / probiha zotaveni / pribyl vypadek */
#define FPGA_CHK_WINDOW   3   /* delka okna neodpovida nastavenemu hradlu */
#define FPGA_CHK_FMAX     4   /* FW 0x0425 (audit 2026-10-20): kmitocet nad mezi PRIMEHO mereni (logika okna / vzorkovani 100 MHz) */
#define FPGA_CHK_COH      5   /* FW 0x0425: hlidac koherence 100/10 MHz nasel chybejici / navic pulz hodin */
/** Mez primeho mereni [Hz]: FW < 0x0425 hrana v close+2 se ztratila nad 33,3 MHz (kontrola |ccd| <= 1 ji propustila);
 *  FW >= 0x0425 opraveno, mez dava vzorkovani 100 MHz a delka retezu (vysoka i nizka uroven > ~10,5 ns): 40 MHz pri stride 45-55 %. */
#define FPGA_DIRECT_MAX_HZ_OLD  33.0e6
#define FPGA_DIRECT_MAX_HZ      40.0e6
/* Mez odchylky delky okna od hradla nad nejistotu uzavirani (perioda signalu): synchronizace hradla
 * do 100 MHz (konstantni, hodiny jsou koherentni) + uzavreni o hranu pozdeji, kdyz je TDC prave obsazeny. */
#define FPGA_WIN_TOL_PS   1000000ull
/** Sedi delka okna `dt_ps` s hradlem `gate_ps`? Okno konci prvni hranou po kazdem tiku hradla, takze
 *  |dt - G| < 3 periody signalu + FPGA_WIN_TOL_PS. Kdyz signal nema aspon 2 hrany na hradlo, okno se
 *  legitimne protahuje pres vic hradel -> nelze rozhodnout (OK). Ciste-logicke (selftest #1).
 *  @return FPGA_CHK_OK nebo FPGA_CHK_WINDOW. */
int fpga_freq_window_check(uint64_t edges, uint64_t dt_ps, uint64_t gate_ps);
/** Delka hradla [ps] podle kodu okna FPGA (FPGA_WIN_*, echo v phase_status & 7); 0 = neznamy kod. */
uint64_t fpga_win_code_ps(uint8_t win);
/** Vysledek samokontroly okna, ktere naposledy vratil `fpga_freq_poll` (FPGA_CHK_*). Nenulove = volajici
 *  ho NESMI zobrazit ani dat do statistiky (jako `fpga_freq_poll_miscount`). */
int  fpga_freq_poll_check(void);
/** Soucty zamitnutych oken od bootu pro `status`. */
void fpga_freq_check_stats(uint32_t *count, uint32_t *clock, uint32_t *window);
/* 2026-10-21: posledni okno zamitnute pocitanim nebo delkou (kdy a s jakym rozdilem) + kolik zamitnuti prislo az po rozbehu */
typedef struct {
    uint32_t ms, seq;        /* HAL_GetTick a SEQUENCE posledniho zamitnuti (0 = zadne) */
    uint32_t nth;            /* poradi noveho mereni od bootu, ve kterem prislo */
    int8_t   ccd;            /* rozdil kontrolniho pocitani v tom okne */
    uint32_t late;           /* zamitnuti po rozbehu (nth > startup_meas) */
    uint32_t startup_meas;
} fpga_chk_last_t;
void fpga_freq_check_last(fpga_chk_last_t *o);
void fpga_freq_check_stats2(uint32_t *fmax, uint32_t *coh);
uint8_t fpga_freq_phrec_selftest_ok(void);   /* audit 2026-10-20: 1 = selftest fazoveho zaznamu prosel pri startu */   /* FW 0x0425: FPGA_CHK_FMAX / FPGA_CHK_COH */
/** v22: surovy posledni ramec FPGA (vcetne zamitnutych oken) + pocitadla do `w[0..n-1]` (IPC `fpgadiag`).
 *  Poradi slov viz telo funkce a `build_fpga_json` v CM4/LWIP/App/httpd_min.c. */
void fpga_freq_diag_fill(uint32_t *w, uint32_t n);
/** "2026-10-10 14:03:05 UTC git d526c00+" (+ = neulozene zmeny); bez identity "neznama". Ciste-logicke. */
void fpga_freq_format_build(uint32_t build_time, uint32_t build_git, char *buf, int buflen);
/** Pozada FPGA o referenci: 0 = OCXO, 1 = externi 10 MHz (SET_CONFIG 0x04, FW >= 0x0420). FPGA externi pusti jen kdyz na vstupu
 *  jsou hrany a pri jejim vypadku se SAMA vrati na OCXO; po zapnuti je vzdy OCXO (nic se neuklada).
 *  @return true = povel odeslan (ne ze se prepnuto; to hlasi clk_status v ramci). */
bool fpga_freq_set_ref(uint8_t ext);
/** Posledni platny DATA ramec VCETNE odmitnuteho mereni -- jen pro diagnostiku (`status`: verze, identita,
 *  hlidac hodin, TDC). Mereni z nej NEBRAT: k tomu je `fpga_freq_get_last`, kam se odmitnute okno nedostane. */
bool fpga_freq_get_frame(fpga_meas_t *out);
/* tdc_status bity (abs 100) */
#define FPGA_TDC_CAL_A        (1u << 0)   /* tabulka kanalu A platna */
#define FPGA_TDC_CAL_B        (1u << 1)
#define FPGA_TDC_CAL_FAIL     (1u << 2)   /* posledni kalibrace selhala (timeout kruhu) */
#define FPGA_TDC_CAL_BUSY     (1u << 3)   /* probiha kalibrace -> mereni blokovana */
#define FPGA_TDC_SHORT_A      (1u << 4)   /* retez A kratky: > 1/16 udalosti za jeho koncem */
#define FPGA_TDC_SHORT_B      (1u << 5)

/* error_flags bity */
#define FPGA_ERR_MEAS         (1u << 0)   /* pin28 (/4): Dt==0 / zadny signal v okne */
#define FPGA_ERR_SIGNAL_LOST  (1u << 1)   /* watchdog ~2.5 s bez mereni; zaroven VALID=0 */
#define FPGA_ERR_OVERFLOW     (1u << 2)   /* okno > ~21.5 s (extremne nizky f) */
#define FPGA_ERR_ALIAS        (1u << 3)   /* okno bez hrany > ~25 s: dt neplatne (FW >= 0x0400) */
#define FPGA_ERR_TDC          (1u << 4)   /* TDC CH_A nezkalibrovan / probiha kalibrace (FW >= 0x0400) */
/* status2 bity */
#define FPGA_ST2_DIV16_ERR    (1u << 0)   /* pin27 (/16): Dt==0 */

/* ── Dva SLOTY ramce vs. dva KANALY (FW >= 0x041E, `FPGA_CAP_CHAN`) ────────────────────────────────
 * Ramec ma PRIMARNI slot (`frequency_x100000`, `edge_count`, `gate_ps`, `error_flags`) a SEKUNDARNI slot
 * (`freq16_x100000`, `edges_b`, `dt_b_ps`, `status2`). Do primarniho FPGA dava kanal vybrany SET_CONFIG 0x03
 * (`channel_id` v ramci = 0 A / 1 B), do sekundarniho ten druhy; NOVE MERENI (SEQUENCE) spousti dokonceni okna
 * PRIMARNIHO kanalu. Headline, statistika, datalog, IPC i SCPI proto cti primarni slot beze zmeny (= vybrany
 * kanal). Kdo potrebuje ABSOLUTNI kanal (okno Dvojkanal), pouzije tyto pomocniky. Starsi FW a emulator:
 * primarni = A, sekundarni = B (jako dosud). */
static inline int fpga_meas_chb_primary(const fpga_meas_t *m)
{
    return (m->caps & FPGA_CAP_CHAN) && (m->channel_id == 1u);
}
/** Kmitocet kanalu `ch` (0 = A, 1 = B) ve formatu x1e5. */
static inline uint64_t fpga_meas_freq_ch(const fpga_meas_t *m, int ch)
{
    return ((ch != 0) == (fpga_meas_chb_primary(m) != 0)) ? m->frequency_x100000 : m->freq16_x100000;
}
/** Chybovy priznak okna (Dt == 0) kanalu `ch`; nenulove = chyba. */
static inline uint32_t fpga_meas_err_ch(const fpga_meas_t *m, int ch)
{
    return ((ch != 0) == (fpga_meas_chb_primary(m) != 0)) ? (m->error_flags & FPGA_ERR_MEAS)
                                                          : (m->status2 & FPGA_ST2_DIV16_ERR);
}

/** Akceptacni krok 1: overi crc16("123456789")==0x29B1 (nase CRC == FPGA CRC).
 *  @return true = OK. Pri false se SPI komunikace nesmi zahajit. */
bool fpga_freq_crc_selftest(void);

/** Rekonfiguruje SPI2 na cilovou rychlost, nastavi CS idle-high a posle START.
 *  Nejdriv DWT init + CRC self-test; pri selhani self-testu komunikaci nezahaji. */
void fpga_freq_init(void);

/** Znovu posle START (re-arm kontinualniho mereni) - kdyz FPGA bootl pozdeji/resetoval. */
void fpga_freq_restart(void);

/** true = posledni poll dostal platny ramec (MAGIC+CRC ok), tj. link je ziva. */
bool fpga_freq_link_ok(void);

/** Pocet ramcu se spatnym CRC od bootu (diagnostika linky, UART `status`). */
uint32_t fpga_freq_crc_count(void);

/** Takt SPI2 za behu (STATUS #314, UART `spi`): aktualni [Hz]; stupen i (0 = kernel/2 ...) a kernel; nastaveni nejvyssiho stupne <= cil
 *  (vraci skutecny takt, 0 = zamitnuto). Nepersistuje se. */
uint32_t fpga_freq_sck_hz(void);
uint32_t fpga_freq_sck_stage(uint32_t i, uint32_t *kernel_hz);
uint32_t fpga_freq_set_sck(uint32_t target_hz);
/** Rychlost vystupu SCK (PI1) a MOSI (PB15) z OSPEEDR: 0 LOW, 1 MEDIUM, 2 HIGH, 3 VERY_HIGH (2026-10-22, protokol v3). */
void     fpga_freq_spi_pin_speed(uint8_t *sck, uint8_t *mosi);
/** v0.40.8 (L-0242): rychlost vystupu SCK / MOSI / CS (0 LOW .. 3 VERY_HIGH) za behu; which 0 = SCK, 1 = MOSI, 2 = CS; nepersistuje se. */
void     fpga_freq_spi_pins_get(uint8_t out[3]);
int      fpga_freq_spi_pin_set(int which, uint8_t speed);
/** v0.40.8 (FW 0x0508, CAPS bit23): zahozene pulzy na CS podle FPGA = aktivni 1-2, 3-4, 5-8, 9+ taktu 100 MHz, v klidu (saturuji na 255).
 *  Vraci delku filtru CS [takty 100 MHz], 0 = FPGA citace nema. */
uint8_t  fpga_cs_gl_get(uint8_t gl[5]);

/** "uptime od posledni CRC chyby" v sekundach (0 = zadna chyba nebyla). */
uint32_t fpga_freq_crc_last_age_s(void);

/* ── Diagnostika chyb SPI v3 (v0.40.7, STATUS #343): kdy, ktery blok, serie, tvar poskozeni ────────────────────────
 * Jen zaznam, na chovani nema vliv. Zapisuje FpgaTask (poll_v3), cte UartTask (`spi err`). */
#define FPGA_SPI_ERR_RING   16u    /* poslednich N chyb (kruh) */
#define FPGA_SPI_ERR_BAD     4u    /* poslednich N vadnych bloku v surove podobe (256 B) */
typedef struct {
    uint32_t t_ms;           /* HAL_GetTick() pri chybe */
    uint32_t frame_no;       /* poradi transakce poll_v3 od boot (nebo od `spi err reset`) */
    uint32_t seq;            /* SEQ z hlavicky tak, jak prisla (u uzke hlavicky jen dolnich 16 b; pri vadnem bloku 0 nejiste) */
    uint16_t since_pps_ms;   /* ms od posledni zmeny PSEQ videne STM (presnost +- perioda pollu), 0xFFFF = neznamo */
    uint16_t gap_ms;         /* ms od zacatku predchozi transakce */
    uint8_t  blk_ok;         /* bit k = CRC bloku k sedi */
    uint8_t  kind;           /* 1 = vadny CRC bloku 0 (ramec zahozen), 2 = vadny CRC bloku 1-3 (zaznamy znovu),
                              * 3 = MAGIC/VER spatne (parser skonci pred kontrolou CRC, do `CRC err` se NEzapocte), 4 = FRAME_LEN/NBLK */
    uint8_t  run;            /* kolik ramcu za sebou je vadnych (vcetne tohoto) */
    uint8_t  bad_slot;       /* 0..BAD-1 = index v poli vadnych bloku, 0xFF = obsah neulozen (prepsan novejsim) */
} fpga_spi_err_t;
typedef struct {
    uint32_t frames;         /* transakce poll_v3 */
    uint32_t err;            /* ramce s chybou (kind 1..4); `CRC err` v status = kind 1 + 2 */
    uint32_t blk[4];         /* chyby CRC podle bloku (vadny blok se zapocte kazdy zvlast) */
    uint32_t kind[5];        /* [1..4] */
    uint32_t bursts, isolated, max_run;   /* serie vadnych ramcu za sebou; izolovana = serie delky 1 */
    uint32_t xfer_us_max;    /* nejdelsi transakce vcetne mezery a cekani na mutex [us] */
    uint32_t gap_ms_max;     /* nejdelsi odstup zacatku dvou transakci [ms] */
    uint32_t inj_left;       /* zbyva vstrikovanych chyb (`spi err inject`) */
} fpga_spi_errstat_t;
void     fpga_spi_err_stats(fpga_spi_errstat_t *s);
/** i-ta nejnovejsi chyba (0 = posledni); false = neni. */
bool     fpga_spi_err_get(uint32_t i, fpga_spi_err_t *e);
/** Vadny blok ulozeny pod slotem (0..BAD-1), vraci index bloku v ramci (0..3) nebo -1; data do dst[256]. */
int      fpga_spi_err_block(uint32_t slot, uint8_t *dst);
void     fpga_spi_err_reset(void);
/** Zkouska diagnostiky: n dalsich prijatych ramcu se pred zpracovanim zkazi (preklopeni jednoho bitu v bloku blk 0..3). */
void     fpga_spi_err_inject(uint32_t blk, uint32_t n);
/** Nejkratsi odstup transakci poll_v3 [ms] (vychozi 40, rozsah 10..200, nepersistuje se); vraci platnou hodnotu. */
uint32_t fpga_freq_poll_ms(int set_ms);

/** Jeden FPGA_FRAME_LEN-B full-duplex prenos (posle ACK posledni seq, prijme
 *  aktualni ramec).
 *  @return true pokud prislo NOVE platne cerstve mereni (CRC ok, VALID, FRESH, nova SEQUENCE). */
bool fpga_freq_poll(fpga_meas_t *out);

/* ── Souvislost SEQUENCE (F-0193) ─────────────────────────────────────────────
 * FPGA zvedne SEQUENCE s KAZDYM merenim a nepotvrzene mereni prepise dalsim
 * (`spi_app.v:190`). Kdyz FpgaTask ramec nestihne (> ~250 ms, typicky UART
 * `fpgaraw`/`fpgaloop` drzi SPI mutex), mereni je navzdy pryc — a vzorek
 * statistiky, do ktereho by dira padla, by mel uvnitr mrtvou dobu. Dosud se to
 * nepoznalo (porovnavala se jen ZMENA SEQUENCE) a nikde nepocitalo. */
#define FPGA_SEQ_GAP_MAX  64u            /* vetsi skok = resync, ne zmeskana mereni */
#define FPGA_SEQ_RESYNC   0xFFFFFFFFu   /* skok/navrat SEQUENCE (reset FPGA, start emulace) */
/** Kolik mereni chybi mezi `prev` a `cur` (ciste-logicke, selftest #1).
 *  0 = navazuje nebo `prev` jeste neni (0xFFFFFFFF), 1..63 = dira,
 *  FPGA_SEQ_RESYNC = skok vetsi nez FPGA_SEQ_GAP_MAX nebo navrat zpet.
 *  Pocita modulo 2^32, takze preteceni SEQUENCE diru nedela. */
uint32_t fpga_seq_gap(uint32_t prev, uint32_t cur);
/** Dira pred merenim, ktere naposledy vratil `fpga_freq_poll` (hodnota jako
 *  `fpga_seq_gap`). Vola FpgaTask hned po `fpga_freq_poll`. */
uint32_t fpga_freq_seq_gap(void);
/** Miscount mereni, ktere naposledy vratil `fpga_freq_poll` (k = -2..+2, 0 = OK).
 *  Nenulove = okno ma o k hran vic/min nez ma -> volajici ho NESMI dat do
 *  statistiky ani zobrazit. Do `fpga_freq_get_last` se takove okno nedostane. */
int  fpga_freq_poll_miscount(void);
/** Soucty od bootu pro `status`: oken s hranou navic / chybejici hranou. */
void fpga_freq_miscount_stats(uint32_t *pos, uint32_t *neg);
/** Soucty od bootu pro `status`: pocet der, zmeskanych mereni a resyncu. */
void fpga_freq_seq_stats(uint32_t *gaps, uint32_t *missed, uint32_t *resync);

/** Naformatuje kmitocet x100000: "123.456.789,01234Hz" (tecky=tisice, carka=des., 5 mist). */
void fpga_freq_format_val(uint64_t freq_x100000, char *buf, int buflen);

/** Vybere zobrazovany zdroj: /4 (nejlepsi rozliseni) dokud je bez chyby a pod
 *  stropem, jinak /16. S HYSTEREZI (nahoru ~380 MHz, dolu ~360 MHz) a sticky
 *  stavem — drzi zdroj, dokud neni duvod prepnout (zadne prebliknuti u prahu).
 *  ⚠️ Ma vnitrni stav -> volat z JEDINEHO kontextu (FpgaTask).
 *  Vrati zvoleny freq_x100000; *used16 (smi byt NULL) = 1 kdyz /16. */
uint64_t fpga_freq_select(const fpga_meas_t *m, int *used16);

/** Ciste jadro volby /4<->/16 (bez stavu; stav drzi wrapper fpga_freq_select).
 *  Vraci novy use16 pro predchozi stav + mereni. Unit-testovatelne. */
int fpga_freq_select_core(int use16, const fpga_meas_t *m);

/** Ktery nasobitel patri k `edge_count`, aby `edges*MUL*1e9/gate_ns` sedel
 *  s autoritativnim `frequency_x100000` z ramce (do 0,1 %).
 *
 *  ⚠️ NASOBITEL SE NEPREDPOKLADA, ALE OVERUJE. `edge_count` muze byt pocet
 *  period DELENE vetve (/4) NEBO neděleného signalu — emulator `fpgasim` ho
 *  pocita nedeleny. Pevne "×4" ukazovalo pri `fpgasim on 10000000` kmitocet
 *  40 MHz (chyba opravena commitem a6c0128, znovu zavlecena a znovu opravena
 *  2026-08-30 pri psani datove cache). Kdo si nasobitel odvozuje sam, tu chybu
 *  si zopakuje — proto je tahle funkce JEDINY zdroj pravdy.
 *  @return 1, 4 nebo 16; 0 = nesedi zadny -> hi-res se NESMI pouzit. */
uint32_t fpga_freq_hires_mul(uint64_t x100000, uint64_t edges, uint64_t gate_ps);

/** Kmitocet v µHz z reciproke dvojice s OVERENYM nasobitelem (viz vyse).
 *  Kdyz zadny nasobitel nesedi, degraduje na `x100000` (tj. 5 desetin). */
uint64_t fpga_freq_hires_uhz(uint64_t x100000, uint64_t edges, uint64_t gate_ps);

/** Chybne napocitane okno (2026-10-06): kmitocet `uhz` se lisi od reference
 *  `uhz_ref` (posledni prijate okno) o CELY nasobek kroku jedne hrany
 *  `mul · 1e18 / gate_ps` [µHz] (+-1, +-2), s toleranci 1/8 kroku. Takovy skok
 *  neni zmena signalu ani sum TDC (ten je o rady mensi), ale hrana navic nebo
 *  chybejici v `edge_count` -- typicky metastabilita detekce hrany ve FPGA
 *  (`tdc.v` t0/t0p). Pri 10 MHz a 0,25 s je to presne +-4 Hz.
 *  Ciste-logicke (selftest #1).
 *  @return k = -2..+2 (0 = okno je v poradku nebo nelze rozhodnout). */
int fpga_freq_miscount(uint64_t uhz, uint64_t uhz_ref, uint64_t gate_ps, uint32_t mul);

/** Kmitocet [Hz] z reciproke dvojice s OVERENYM nasobitelem, v `double` (F-0180).
 *  Presnost ~1e-16 relativne na libovolnem kmitoctu — tim se lisi od `x100000`
 *  (krok 10 µHz = 1e-8 pri 1 kHz, vic nez podlaha citace) i od µHz vyse.
 *  @return 0.0 = nasobitel neoveren (vetev /16, stary ramec) -> volajici pouzije
 *  `x100000` a NEPREDSTIRA vic cislic, nez mereni nese. */
double fpga_freq_hires_hz(uint64_t x100000, uint64_t edges, uint64_t gate_ps);

/* ── Presna delka okna ─────────────────────────────────────────────────────
 * 🔴 2026-10-03: delka okna se VSUDE bere z `fpga_meas_t.gate_ps` [ps].
 *  * Novy FW (caps bit5, carry-chain TDC): FPGA posila dt v jednotkach
 *    T_clk/16384 = 0,6103515625 ps (1,6384e12 jednotek/s); `gate_ps` =
 *    round(dt * 625 / 1024) (chyba <= 0,5 ps = 2e-12 pri 0,25 s). FPGA uz
 *    NEPOCITA frequency_x100000 — tu vypocte parser z (edge_count, gate_ps).
 *  * Stary FW / emulator: `gate_time_ns` je FLOOR(dt * 2,5 ns) (F-0186), presne
 *    dt = round(gate_ns / 2,5 ns) (`fpga_freq_dt_ticks`) -> gate_ps = dt * 2500.
 * Kdo deli `gate_time_ns` sam, zopakuje F-0186 (floor) i nepresnost ns. */
#define FPGA_LEGACY_TICK_PS  2500u                        /* 4fazovy vernier (stary FW) */
#define FPGA_PS_PER_S        1000000000000ull
/** LEGACY: pocet ticku 2,5 ns okna z `gate_time_ns` stareho FW (zaokrouhleni
 *  na nejblizsi tick). Pro novy FW se NEPOUZIVA — pouzij `gate_ps`. */
uint64_t fpga_freq_dt_ticks(uint64_t gate_ns);
/** floor(n * 1e12 * 10^frac / t_ps) bez 128bitoveho deleni (dlouhe deleni po
 *  cislicich). `n` <= 4e9 (n*1e6 se musi vejit do uint64), `t_ps` <= ~1e16.
 *  Pro n = pocet period, t_ps = okno: kmitocet [Hz] * 10^frac. */
uint64_t fpga_freq_scaled(uint64_t n, uint64_t t_ps, int frac);

/* ── TDC (kalibrace a diagnostika, FW >= 0x0400) ───────────────────────────── */
typedef struct {
    uint32_t ovf[2];      /* [A,B] udalosti kalibrace za koncem retezu (kod 255) */
    uint32_t peak[2];     /* nejvetsi pocet udalosti na jeden kod */
    uint16_t nz[2];       /* pocet neprazdnych kodu (~175 = retez pokryva 10 ns) */
    uint16_t last[2];     /* nejvyssi neprazdny kod (= nejvyssi pozice PRVNI nuly) */
    uint16_t maxtap[2];   /* B1a: nejvyssi KDY navzorkovany tap; maxtap>>last = bubliny nad prvni nulou */
    uint8_t  status;      /* FPGA_TDC_* */
    uint8_t  cal_mode;    /* posledni SET_CONFIG 0x02 */
    /* FW >= 0x0410 (CAL bajty 50..51): konfigurace TDC. Starsi FW je nenese -> naddr = 256, dual = 0. */
    uint16_t naddr;       /* pocet adres tabulky kalibrace ({sada, kod}) */
    uint16_t taps;        /* pocet vzorkovanych tapu (256 / 288) */
    uint8_t  dual;        /* 1 = vzorky i na sestupnou hranu hodin (adresy 0..naddr/2-1 = sada R, zbytek F) */
    uint8_t  stride1;     /* 1 = vzorek kazdeho ALU (jemnejsi biny) */
    uint8_t  cfg_valid;   /* 1 = FW konfiguraci hlasi */
} fpga_tdc_cal_t;
/** Spusti kalibraci TDC (SET_CONFIG 0x02: 0 pak 1 = nabezna hrana). FPGA ji
 *  dokonci za ~0,5-1 s a behem ni nemeri. @return false pri chybe prenosu. */
bool fpga_freq_tdc_cal_start(void);
/** Precte CAL report (TYPE 0xA0, diagnostika kalibrace). Dve transakce pod
 *  jednim zamkem SPI. @return false = nedorazil platny CAL ramec. */
bool fpga_freq_tdc_report(fpga_tdc_cal_t *out);

/* ── Kalibrace TDC v STM (FW >= 0x0427, caps bit4; audit 2026-10-21 S1/S2, L-0188) ──
 * FPGA po sberu ceka (lut_wait); FpgaTask zavola `fpga_freq_tdccal_poll` po kazdem pollu: cekani potvrzene ve 2 ramcich za sebou
 * (nebo 1 kanal > ~1 s) spusti `fpga_freq_tdccal_run`: precte histogramy, spocita tabulky (fpga_tdccal.c), zapise je, overi
 * pocitadlo a soucet z DATA ramce a potvrdi (SET_CONFIG 0x06 = 1). Blokuje ~0,6 s (hlida watchdog_kick_fpga). */
typedef struct {
    uint32_t runs, fails, retries;   /* uspesnych behu / selhani / opakovani zapisu od startu */
    uint32_t last_ms;                /* doba posledniho behu [ms] */
    uint32_t last_tick;              /* HAL_GetTick posledniho uspesneho behu (0 = zatim zadny) */
    uint16_t naddr;                  /* velikost tabulky */
    uint8_t  last_err;               /* 0 = ok, 1 = histogram (soucet), 2 = zapis (pocitadlo/soucet), 3 = prenos, 4 = potvrzeni */
    uint8_t  stats_valid;            /* ovf/peak/nz/last pocitane z posledniho histogramu (pro `tdc` / rezervu retezu) */
} fpga_tdccal_info_t;
bool fpga_freq_tdccal_poll(void);   /* @return true = probehl beh kalibrace (uspesne i ne) */
void fpga_freq_tdccal_info(fpga_tdccal_info_t *o);

/* ── Rezerva TDC retezu (2026-10-14, audit 2026-10-10_volba-kmitoctu-hodin.md kap. 10) ─────────────────────────
 * Retez ma nejvys TDC_NTAP (268) tapu a musi pokryt celou periodu hodin. Rezerva na 100 MHz je jen ~4-6 % (B po
 * studenem startu 258 z 268). Hrany za koncem retezu padnou do JEDNOHO posledniho binu, ktery kalibrace nastavi na
 * jeho stred, takze degradace je pozvolna (chyba ±s/2), ale nic ji dosud nehlasilo drive nez FPGA priznak SHORT
 * (> 1/16 udalosti za koncem). Tohle je VCASNE varovani, jen z CAL reportu — nic nemeni v mereni ani ve FPGA.
 * Vyhodnocuje se jednou po kazde kalibraci (report je ulozen v `fpga_freq_tdc_report`). */
#define FPGA_TDC_REZ_OK       0u    /* >= FPGA_TDC_REZ_OK_TAPS tapu, nic za koncem */
#define FPGA_TDC_REZ_POZOR    1u    /* 8..14 tapu */
#define FPGA_TDC_REZ_KRIT     2u    /* < 8 tapu, nebo aspon jedna udalost za koncem (FPGA priznak SHORT az od 1/16) */
#define FPGA_TDC_REZ_OK_TAPS    15
#define FPGA_TDC_REZ_WARN_TAPS  8
/* Pocet udalosti kalibrace = 2^CAL_LOG2 ve FPGA (top.v: CAL_LOG2 = 20). Jediny zdroj je FPGA; zmena tam = zmena tady. */
#define FPGA_TDC_CAL_LOG2     20u
typedef struct {
    int16_t  rez_tap[2];   /* [A,B] zbyvajici tapy = taps - last; 0, kdyz uz jsou udalosti za koncem (last je pak max. kod) */
    uint32_t ovf_ev[2];    /* udalosti kalibrace za koncem retezu */
    uint32_t ovf_ppm[2];   /* totez v ppm vsech udalosti */
    uint8_t  lvl[2];       /* FPGA_TDC_REZ_* */
    uint16_t taps;         /* fyzicke tapy z reportu */
    uint8_t  valid;        /* 1 = report aspon jednou precten */
    uint32_t n;            /* kolikaty report (pocita jen uspesna cteni) */
} fpga_tdc_rez_t;
/** Ciste vyhodnoceni CAL reportu (bez stavu, testovatelne). */
void fpga_tdc_rez_eval(const fpga_tdc_cal_t *c, fpga_tdc_rez_t *r);
/** Posledni ulozene vyhodnoceni (z jakehokoli uspesneho `fpga_freq_tdc_report`). @return r->valid */
bool fpga_tdc_rez_get(fpga_tdc_rez_t *r);
/* Vypis histogramu kalibrace (FW >= 0x040B): hist[k] obou kanalu (pocet udalosti
 * kalibrace s adresou k; sirka binu = hist[k] / N * 10 ns). Diagnostika DNL.
 * `k` je od FW 0x0410 10bitove ({sada, kod}; starsi FW bere jen k < 256).
 * Vraci false, kdyz odpoved neprisla nebo nese jiny kod. */
bool fpga_freq_tdc_hist(uint16_t k, uint32_t *a, uint32_t *b);

/* ── Regrese (FW >= 0x0411): statistika pro UART `regr` ────────────────────────
 * Pro kazde okno s platnym regresnim blokem se porovna f_regr s dvoubodovym f_2pt. Rozdil je
 * relativni (ppb): u signalu s "prohazujici" fazi ma byt maly a sigma(f_regr) mensi nez sigma(f_2pt);
 * u signalu s konstantni fazi (GPSDO 10 MHz na 100 MHz) zisk neni, ale nesmi vzniknout posun. */
typedef struct {
    uint32_t windows;       /* oken s regresnim blokem (ok A i B) */
    uint32_t used;          /* oken, kde se regrese pouzila (konzistentni s dvoubodovym odhadem) */
    uint32_t rejected;      /* oken zamitnutych pro nekonzistenci (|f_regr/f_2pt - 1| > mez) nebo malo znacek */
    uint32_t n_last[2];     /* pocet znacek v poslednim okne A/B */
    double   f_regr_last;   /* posledni f_regr [Hz] */
    double   f_2pt_last;    /* posledni dvoubodovy f [Hz] */
    double   d_mean_ppb;    /* stredni rozdil f_regr - f_2pt [ppb] */
    double   d_sigma_ppb;   /* sigma rozdilu [ppb] */
} fpga_regr_stat_t;
void fpga_freq_regr_stat(fpga_regr_stat_t *out);

/* ── Zaznamnik oken pro INL (FW >= 0x0418): kazde nove okno bez miscountu ─────── */
typedef struct {
    uint32_t seq;
    uint32_t edges;
    uint64_t gate_ps;
    uint16_t code_end, code_st;   /* kanal A */
    uint16_t gap;                 /* 1 = pred oknem byla dira v SEQ (code_st neplati) */
    uint16_t pad;
} fpga_inl_rec_t;
#define FPGA_INL_N 2048u
uint32_t fpga_freq_inl_count(void);                       /* pocet zaznamu (max FPGA_INL_N) */
int      fpga_freq_inl_get(uint32_t i, fpga_inl_rec_t *r);  /* i = 0 nejstarsi; 0 = mimo rozsah */
void     fpga_freq_inl_reset(void);
/** Kmitocet [Hz] ze sklonu primky mezi stredy segmentu A a B (cisty vypocet). 0.0 = nelze (B <= A). */
double fpga_freq_regr_hz2(uint64_t xm_a, uint64_t xm_b, uint64_t ym_a, uint64_t ym_b, uint32_t clk_mhz, uint16_t xdiv);  /* audit 4 */
double fpga_freq_regr_hz(uint64_t xm_a, uint64_t xm_b, uint64_t ym_a, uint64_t ym_b, uint32_t clk_mhz);
void fpga_freq_regr_reset(void);
/** Aktualni mez prijeti regrese (relativne) -- diagnostika `status`. */
double fpga_freq_regr_limit(void);
/** FW >= 0x0425: SET_CONFIG 0x05 -- omezeni ruseni / diagnostika. mask_cs = 1: vnitrni znacky (segmenty regrese) se pri aktivnim
 *  CS nespousteji (vychozi FPGA 1); led_hb = 1: LED na PIN29 blika s merenim (vychozi FPGA 0, PIN29 je v bance 2 s CH_A/B). */
bool fpga_freq_set_diag(uint8_t mask_cs, uint8_t led_hb);
/** Mez konzistence regrese s dvoubodovym odhadem (relativne) a minimum znacek v segmentu. */
#define FPGA_REGR_MAX_REL   5.0e-8     /* ~40 sigma dvoubodoveho odhadu (sigma ~1,2e-9 pri 215 ps na znacku, okno 0,25 s) */
/* Audit 2026-10-20: ADAPTIVNI mez prijeti regrese. Pevnych 5e-8 je ~40 sigma -> propusti i chyby ~1e-8 (napr. N1 pri 17-25 MHz).
 * Mez = max(FPGA_REGR_K_SIGMA * sigma_d, FPGA_REGR_MIN_REL), shora FPGA_REGR_MAX_REL; sigma_d = robustni (EWMA |d| * 1,2533)
 * odchylka rozdilu d = f_regr/f_2pt - 1 z PRIJATYCH oken. Prvnich FPGA_REGR_WARMUP oken plati pevna mez. */
#define FPGA_REGR_K_SIGMA   6.0
#define FPGA_REGR_MIN_REL   5.0e-9
#define FPGA_REGR_WARMUP    32u
#define FPGA_REGR_MIN_N     8u

/* ── Akumulátor měření: průměr za okno konzumenta (F-0171/F-0172) ───────────
 * 🔴 FPGA dává ~4 měření/s po 0,25 s, ale statistika vzorkuje 1×/s a datalog
 * 1× za periodu. Do 2026-09-26 si oba brali jen POSLEDNÍ měření a zbylá
 * zahodili — mrtvá doba 75 % (statistika) a ~97 % (datalog 10 s). Allanova
 * odchylka pak vycházela 2× (bílý FM) až 23× (bílý PM) vysoko a šum přístroje
 * dostal sklon bílého FM (simulace `docs/audit/sim/2026-09-26_mrtva_doba.js`).
 * Teď FpgaTask sčítá cykly a hradla VŠECH platných měření do akumulátoru
 * každého konzumenta a ten si při odběru vezme reciproký průměr
 * `Σcykly / Σhradla` — přesný kmitočet sjednocení oken.
 * ⚠️ Mrtvá doba zmizí úplně jen tehdy, když FPGA dává NAVAZUJÍCÍ okna
 * (HYPOTÉZA pro novou desku — ověřit ve firmwaru FPGA); jinak aspoň klesne.
 * ⚠️ Každý konzument má VLASTNÍ akumulátor a odebírá ho sám — sdílený by si
 * navzájem vyjídali okna. */
#define FPGA_ACC_STATS    0   /* statistika stability — ODEBIRA SE pres `fpga_stat_pop` */
#define FPGA_ACC_DATALOG  1   /* datalog (defaultTask, 1× za periodu) */
#define FPGA_ACC_N        2

/** Přičte jedno platné měření do všech akumulátorů. Volá VÝHRADNĚ FpgaTask. */
void fpga_acc_add(uint64_t x100000, uint64_t edges, uint64_t gate_ps);   /* gate_ps = `fpga_meas_t.gate_ps` */
/* Cilova delka vzorku statistiky (prumerovani): delsi = lepsi rozliseni (lin.),
 * kratsi az 250 ms (jedno FPGA okno) = rychlejsi. UART `gatestat <ms>`. */
void     fpga_stat_set_target_ms(uint32_t ms);
uint32_t fpga_stat_target_ms(void);

/* Zakladni okno FPGA (SET_CONFIG 0x01): delsi = lepsi syrove rozliseni, mene SPI. */
#define FPGA_WIN_100MS 0u
#define FPGA_WIN_250MS 1u
#define FPGA_WIN_1S    2u
#define FPGA_WIN_50MS  3u      /* FW >= 0x041E */
#define FPGA_WIN_500MS 4u      /* FW >= 0x041E */
bool fpga_freq_set_window(uint8_t mode);                 /* 0..4 */

/* Vyber kanalu a hradla z UI (FW >= 0x041E, `FPGA_CAP_CHAN`). Index hradla = IPC_UICFG_GATE (0,05 / 0,1 /
 * 0,25 / 0,5 / 1 s) -> kod `FPGA_WIN_*`. */
uint8_t  fpga_gate_idx_to_win(uint8_t idx);
uint32_t fpga_gate_idx_ms(uint8_t idx);                  /* delka hradla [ms] — FpgaTask podle ni voli periodu cteni */
bool     fpga_freq_set_channel(uint8_t ch);              /* SET_CONFIG 0x03: 0 = A, 1 = B primarni */
/** Idempotentni synchronizace: porovna ECHO v poslednim ramci (`channel_id`, `phase_status & 7`) s pozadavkem
 *  a pripadne ho znovu posle (FPGA po power-cyklu/reconfiguraci startuje s A a 0,25 s). Rate-limit 1,5 s,
 *  protoze echo se obnovuje az s novym merenim. Volat z FpgaTasku. @return 1 = neco se poslalo. */
bool     fpga_freq_cfg_sync(uint8_t want_chan, uint8_t want_gate_idx);
/** 1 = ramec je jeste z JINEHO kanalu, nez uzivatel zvolil (prechod) -> nepouzit ho pro zobrazeni ani statistiku. */
bool     fpga_freq_chan_pending(const fpga_meas_t *m, uint8_t want_chan);

/** Odebere a vynuluje akumulátor `which`. `*hz` (smí být NULL) = reciproký
 *  průměr za okno od minulého odběru, `*gate_s` (smí být NULL) = celková délka
 *  sečtených oken [s] — skutečné τ0 vzorku (bod 6). @return počet měření v okně;
 *  0 = žádné nové měření (pak `*hz` = `*gate_s` = 0). */
uint32_t fpga_acc_take(int which, double *hz, double *gate_s);

/* ── Hotove vzorky statistiky PODLE POCTU mereni (#27, 2026-09-26) ─────────
 * Statistika nebere svuj akumulator podle 1s tiku, ale FpgaTask ho UZAVRE, jakmile
 * soucet oken dosahne 1 s (presneji: Σhradel >= 1 s - posledni hradlo/2), a hotovy
 * vzorek da do fronty. Vsechny vzorky tak maji STEJNY pocet mereni. Casovy tik
 * UiTasku (~1,01 s, latence smycky) obcas pobral o mereni vic a pri nizkem
 * kmitoctu se delky vzorku stridaly: simulace (`docs/audit/sim/2026-09-26_tau0_
 * cas_vs_pocet.js`) rozptyl delky 3,5 % -> 0,00 % pri 10 MHz, 9,8 % -> 1,4 % pri
 * 40 Hz, 21 % -> 14 % pri 2,5 Hz. Fronta 16 vzorku; pri preteceni se zahodi
 * nejstarsi a pocita se (`fpga_stat_drops`). */

/** Odebere nejstarsi hotovy vzorek. @return 1 = `*hz` [Hz] a `*tau_s` (delka
 *  jeho oken) vyplneny, 0 = fronta prazdna. Vola VYHRADNE UiTask. */
int fpga_stat_pop(double *hz, double *tau_s);
/** Rozpracovany vzorek zahodit — okna uz nenavazuji: neplatne mereni, dira
 *  v SEQUENCE nebo ztrata signalu/linku (F-0193). FpgaTask. */
void fpga_stat_break(void);
/** F-0188: rozpracovany vzorek I celou frontu zahodit — pri nulovani statistiky
 *  kvuli zmene signalu. Jinak by prvni 1-2 vzorky nove pyramidy byly ze STAREHO
 *  nebo smiseneho signalu (sim/2026-09-27_reset_fronta.js: 99,6 % prepnuti).
 *  Pod PRIMASK, smi ji volat UiTask. */
void fpga_stat_flush(void);
/** Kolik hotovych vzorku se ztratilo preplnenim fronty (UiTask neodebiral). */
uint32_t fpga_stat_drops(void);

/** Selftest hystereze volby zdroje na syntetickych ramcich (UART "selftest").
 *  Nemeni runtime stav. @return true = vsechny kroky OK. */
bool fpga_freq_select_selftest(void);

/** Naformatuje vedlejsi udaje: "<src> PH:<present>/<fine> GATE:<ns>NS SEQ:<n>[ chyba]".
 *  use16 != 0 -> zdroj "/16", jinak "/4". */
void fpga_freq_format_info(const fpga_meas_t *m, int use16, char *buf, int buflen);

/** true = posledni DATA ramec hlasil ztratu signalu (error_flags bit1 SIGNAL_LOST).
 *  Autoritativni z FPGA (watchdog ~2.5 s); funguje i kdyz mereni neni VALID. */
bool fpga_freq_signal_lost(void);

/** Kopie posledniho naparsovaneho DATA ramce (latch — i nefresh/invalid, takze
 *  error_flags/phase_status jsou videt i pri ztrate signalu). Kopiruje pod
 *  kratkym IRQ-off -> bezpecne z jineho tasku nez FpgaTask (okno Citac v UiTasku).
 *  @return false = zadny DATA ramec zatim nedorazil (out se nemeni). */
bool fpga_freq_get_last(fpga_meas_t *out);

/** Diagnostika: jeden FPGA_FRAME_LEN-B prenos (posle ACK), syrova odpoved do
 *  rx_frame (min. FPGA_FRAME_LEN B — volajici pole musi mit tuto velikost).
 *  @return true pokud HAL prenos prosel (rika nic o platnosti dat). */
bool fpga_freq_raw_xfer(uint8_t *rx_frame);

/** Naformatuje stav SPI + komunikace: "SPI <x.xx>MHZ LINK:OK SEQ:<n> CRC:<n>".
 *  (rychlost SCK, stav linky, posledni potvrzena SEQ, pocet CRC chyb). */
void fpga_freq_format_status(char *buf, int buflen);

/* ══════════════ Emulator FPGA ramcu (vyvoj bez osazene FPGA desky) ══════════
 * Misto `xfer()` slozi SYNTETICKY DATA ramec (FPGA_FRAME_LEN B; v1-kompatibilni
 * payload 0..49, v2 rozsireni necha na nule — viz sim_build_frame). Vsechno za tim — kontrola
 * MAGICu, overeni CRC, `parse_data`, latch, VALID/FRESH/SEQ, hystereze /4-/16 —
 * bezi PRESNE jako s hardwarem, takze se testuje skutecna datova cesta, ne jeji
 * obejiti. Nahrazuje tim horsi simulaci, ktera dosud zila az v UI vrstve
 * (`screen_main.c` nahodna prochazka) a celou tuhle cestu preskakovala.
 *
 * ⚠️⚠️ NEVALIDUJE: dratovou vrstvu (CS/SCK/MISO), casovani ani logiku samotne
 * FPGA. Overuje NASI polovinu kontraktu — to je presne to, co jinak nejde.
 *
 * ⚠️ POJISTKY proti zamene za realne mereni (simulace, ktera vypada verohodne,
 * je horsi nez zadna): vychozi VYPNUTO, NEPERSISTUJE se, datalog zaznamy nesou
 * `DATALOG_F_SIM`, info radek zacina "SIM", `status` to hlasi a SCPI ma
 * `DIAG:SIM?`. */

/** Zapne/vypne emulator. `hz` = nominalni kmitocet [Hz], `noise_ppb` = bila
 *  slozka (+-) v ppb, `drift_ppb_h` = linearni stárnutí v ppb za hodinu.
 *  Volat z UartTasku (`fpgasim`). */
void fpga_sim_set(int on, double hz, float noise_ppb, float drift_ppb_h);

/** 1 = emulator aktivni (cte UI, datalog, SCPI, `status`). */
int  fpga_sim_active(void);

/** Nominalni kmitocet emulatoru [Hz] (0 = vypnuto). */
double fpga_sim_hz(void);

/** Vnutit poruchu, kterou na stole nevyrobis. `what`:
 *   "none"   zadna
 *   "lost"   SIGNAL_LOST + DATA_VALID=0 (test ztlumeni + alarmu)
 *   "crc"    poskozene CRC (musi ho chytit prijem, ne parse)
 *   "div16"  chyba deleni /16 (status2 bit0 -> test vyberu odbocky)
 *   "phase"  dira ve `phase_status` (chybejici faze TDC)
 *   "gap"    JEDNORAZOVE preskoci 3 mereni (dira v SEQUENCE, F-0193) —
 *            totez, co udela skutecna FPGA, kdyz FpgaTask ramec nestihne;
 *            aktivni porucha zustava beze zmeny
 *  @return 0 = nezname jmeno poruchy. */
int  fpga_sim_fault(const char *what);

/* ── Protokol v3 (docs/PROTOKOL_V3.md, krok 4c) ───────────────────────────────────────────────────────────────
 * Ovladac zjisti verzi FPGA prvni transakci 1024 B (kap. 6.2) a podle ni bezi v2 (128 B, beze zmeny) nebo v3 (zaznamy
 * segmentu -> okna v STM, fpga_meas_t se stejnymi poli). */
/* Krok 7: SPI2 + DMA za behu (1 zapnout, 0 vypnout, -1 zjistit); vychozi vypnuto. @return 1 = DMA se pouziva. */
int  fpga_freq_spi_dma(int on);
typedef struct {
    uint8_t  proto, sim, chan, flags;
    uint32_t frames, recs, dups, gaps, gap_recs, resync, wins, qdrop, pages;
    uint32_t k, seg_ms, expect;
    uint16_t fw, ring_fill, rx_crc_err, rx_len_err;
    uint32_t caps, rec_head, rec_first;
    uint8_t  tdc[2], cal_wait;
    uint8_t  pps_status, pps_pflags;   /* v0.37.0: bajt 50, PFLAGS posledni hrany */
    uint8_t  presc[2], pr_mode[2];     /* v0.38.0: PRESC A/B (bit7 ceka), volba (0xFF auto, jinak pevne log2 P) */
    uint16_t probe[2];                 /* sonda /16 za 1 ms */
    uint32_t pr_sw[2];                 /* pocet prepnuti P */
    uint8_t  deep_st, deep_wsel;       /* v0.39.0: PSRAM (FPGA_V3_DS_*, latence zapisu) */
    uint32_t deep_n;
    int8_t   pps_d;                    /* jemny - hruby [takty] */
    uint8_t  narrow;                   /* v0.40.0: uzka hlavicka (FW 0x0507, CAPS bit22) */
    uint32_t wrap_restarts;            /* restartu FPGA poznanych pri rozbalovani 16b poli */
    uint32_t wins_b, lsq_b;            /* v0.42.3: mereni s druhym kanalem / z toho s LSQ delkou okna B */
} fpga_v3_info_t;
void fpga_freq_v3_info(fpga_v3_info_t *o);
/* v0.38.0 (FW 0x0503): predelicka kanalu ch (0 A, 1 B): mode 0xFF = automaticky podle sondy, 0..4 = pevne log2 P.
 * @return 1 ok, 0 FPGA bez CAPS bit18 (volba se ulozi, uplatni se, az ji FPGA umi), -1 spatny parametr */
int  fpga_freq_presc(uint8_t ch, uint8_t mode);
/** Emulator FPGA v3 (`fpgasim v3 on <Hz> [Hz_B] [faze_B_ns] [uzka]`): ramce 1024 B ze zaznamu segmentu (fpga_v3_emu), ACK
 *  zpracovany az po slozeni dalsiho ramce jako u RTL; 1PPS z emulatoru v2 (smycka GPSDO). ph_b_s > 0 = prvni hrana B
 *  o tolik po prvni hrane A (jednorazove deje). on = 0 vypne. */
void fpga_sim_v3_set(int on, double hz_a, double hz_b, double ph_b_s);
int  fpga_sim_v3_active(void);
/** v0.40.0: dalsi `fpga_sim_v3_set(1, ...)` sklada uzkou hlavicku jako FW 0x0507 (CAPS bit22) a citace zacinaji tesne
 *  pod 2^16 (SEQ, REC_HEAD/REC_FIRST pretecou do nekolika sekund) -- rozbaleni ve STM se tak overi i bez FPGA. */
void fpga_sim_v3_narrow(int on);

/* ── Jednorazove deje (v0.36.0, protokol v3, FW 0x0501; docs/PROTOKOL_V3.md kap. 10.5) ───────────────────────────────
 * Ze zaznamu segmentu: presny cas prvni hrany segmentu (N = 2 i druhe), pri N >= 3 souhrn (pocet, stredni perioda).
 * Casuji se jen hrany >= 50 ns od predchozi casovane. Delsi sekvenci zachyti RAW_CAPTURE (512 znacek). */
#define FPGA_EVP_OFF   0u     /* parovani vypnuto */
#define FPGA_EVP_AB    1u     /* interval: start = hrana A, stop = prvni hrana B po startu */
#define FPGA_EVP_PW    2u     /* sirka pulzu: B = pin A invertovany (SET_CONFIG 0x09 = 3) */
#define FPGA_EVP_SKEW  3u     /* kalibrace zpozdeni A/B: B = pin A (0x09 = 1) */
typedef struct {
    uint64_t t_ps;            /* cas hrany na ose FPGA [ps] (rozbaleny; osa od startu FPGA) */
    int64_t  pps_ps;          /* cas od 1PPS #pseq [ps] (zaporny = pred ni) */
    double   per_ps;          /* souhrn: stredni perioda [ps] */
    uint32_t n, pseq;         /* hran (hrana: 1) / poradi 1PPS */
    uint16_t seg;             /* SEG_IDX */
    uint8_t  ch, kind, flags, pps_ok;   /* kind FPGA_V3_EV_EDGE / _BURST, flags = FLAGS zaznamu */
} fpga_ev_rec_t;
typedef struct {
    uint8_t  proto, sim, ev_on, pair, bsrc, echo;     /* bsrc = odeslany SET_CONFIG 0x09, echo = bajt 66 bit3..4 */
    uint16_t fw;
    uint32_t caps, pps_seq;
    uint32_t ev_total, ev_fill, ab_fill;
    uint32_t ab_n, ab_lost, sk_n;
    double   ab_mean, ab_sd, ab_min, ab_max;          /* intervaly [ps] */
    double   sk_mean, sk_sd, sk_min, sk_max;          /* zpozdeni B - A [ps] */
} fpga_ev_info_t;
void fpga_freq_ev_enable(int on);                      /* kruh udalosti (zapnuti = vynulovani) */
/** Parovani FPGA_EVP_* a zdroj kanalu B (SET_CONFIG 0x09). @return 1 ok, 2 emulator (zdroj B se neprepina),
 *  0 SET_CONFIG selhal, -1 FPGA bez CAPS bit20 (FW < 0x0501), -2 neni protokol v3 */
int  fpga_freq_ev_pair(uint8_t mode);
void fpga_freq_ev_info(fpga_ev_info_t *o);
int  fpga_freq_ev_get(uint32_t back, fpga_ev_rec_t *o);   /* back 0 = nejnovejsi; 0 = neni */
int  fpga_freq_ab_get(uint32_t back, double *ps);           /* posledni intervaly [ps]; 0 = neni */
/** RAW_CAPTURE (TYPE 0x0B): 512 po sobe jdoucich casovanych znacek kanalu ch. Blokuje volajiciho (UartTask) do timeout_ms
 *  + cteni 5 stranek; cb (muze byt NULL) dostane kazdou znacku [ps, 48 b osa] a rozdil proti predchozi [ps].
 *  @return 1 = 512 znacek, 0 = mene (timeout), -1 stranka selhala, -2 neni v3 / emulator, -3 SPI,
 *  -5 FPGA prikaz odmitlo (bezi hluboky zaznam / nacitani okna; FW >= 0x0505) */
typedef void (*fpga_raw_cb_t)(void *ctx, uint32_t i, uint64_t mark_ps, double d_ps);
typedef struct {
    uint32_t n;                                       /* precteno znacek */
    uint32_t cnt;                                     /* pocet podle FPGA (souhrnna stranka polozka 5) */
    double   d_mean, d_sd, d_min, d_max, span_ps;     /* rozdily po sobe jdoucich znacek, prvni..posledni [ps] */
    uint8_t  dst;                                     /* DEEP_ST po zaznamu (FPGA_V3_DS_GAP = v zaznamu mezery) */
} fpga_raw_info_t;
int  fpga_freq_v3_raw(uint8_t ch, uint32_t timeout_ms, fpga_raw_cb_t cb, void *ctx, fpga_raw_info_t *st);
/** v0.39.0 (FW 0x0505, CAPS bit21): HLUBOKY RAW -- n po sobe jdoucich znacek kanalu ch do PSRAM FPGA (nejvys 699 050),
 *  cekani na konec (nejvys timeout_ms), cteni po oknech 512 znacek (rezim 2 + stranky druhu 3); statistika rozdilu
 *  navazuje pres okna, cb jako u fpga_freq_v3_raw. *deep_st = DEEP_ST na konci zaznamu (FPGA_V3_DS_OVF = zahozene).
 *  @return 1 = vsech n, 0 = mene (timeout / preteceni), -1 stranka / okno selhalo, -2 neni v3 / emulator, -3 SPI,
 *  -4 FPGA bez PSRAM (CAPS bit21) nebo PSRAM neni pripravena, -5 FPGA prikaz odmitlo nebo zaznam je z jineho kanalu.
 *  Zaznam z drivejsiho volani, ktery jeste bezi, se nejdriv zastavi; po timeoutu se zaznam zastavi a prectou se okna
 *  toho, co se zapsalo (navrat 0). */
int  fpga_freq_v3_raw_deep(uint8_t ch, uint32_t n, uint32_t timeout_ms, fpga_raw_cb_t cb, void *ctx, fpga_raw_info_t *st,
                           uint8_t *deep_st);

#endif /* FPGA_FREQ_H */
