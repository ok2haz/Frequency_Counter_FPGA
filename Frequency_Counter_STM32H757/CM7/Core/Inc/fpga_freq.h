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
    uint16_t caps;                /* abs 62-63: bit0=window stream, bit1=SET_CONFIG, bit4=Λ/fine */
    uint8_t  clk_status;          /* abs 64: bit0=10MHz pritomen, bit1=PLL/DLL lock */
    uint8_t  win_count;           /* abs 65: kolik window zaznamu (0..2) je platnych (dnes nevyuzito) */
    /* ── 2026-10-03: skutecny carry-chain TDC (FW >= 0x0400, caps bit5) ──────── */
    uint64_t gate_ps;             /* PRESNE okno CH_A [ps] — jediny zdroj delky okna pro VSECHNY
                                   * vypocty (hi-res, akumulatory, statistika). Novy FW: z dt v
                                   * jednotkach T_clk/16384 (abs 118) * 625/1024; stary FW /
                                   * emulator: z gate_time_ns pres `fpga_freq_dt_ticks` (F-0186) */
    uint64_t dt_b_ps;             /* okno CH_B [ps] (abs 101..106); 0 = neznamo */
    uint32_t edges_b;             /* periody CH_B v okne (abs 108) */
    uint8_t  tdc_status;          /* abs 100: viz FPGA_TDC_* */
} fpga_meas_t;

/* caps bity (abs 62-63) */
#define FPGA_CAP_DT           (1u << 5)   /* ramec nese dt_a/dt_b v jednotkach T_clk/16384 */
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

/** "uptime od posledni CRC chyby" v sekundach (0 = zadna chyba nebyla). */
uint32_t fpga_freq_crc_last_age_s(void);

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
    uint16_t last[2];     /* nejvyssi neprazdny kod */
    uint8_t  status;      /* FPGA_TDC_* */
    uint8_t  cal_mode;    /* posledni SET_CONFIG 0x02 */
} fpga_tdc_cal_t;
/** Spusti kalibraci TDC (SET_CONFIG 0x02: 0 pak 1 = nabezna hrana). FPGA ji
 *  dokonci za ~0,5-1 s a behem ni nemeri. @return false pri chybe prenosu. */
bool fpga_freq_tdc_cal_start(void);
/** Precte CAL report (TYPE 0xA0, diagnostika kalibrace). Dve transakce pod
 *  jednim zamkem SPI. @return false = nedorazil platny CAL ramec. */
bool fpga_freq_tdc_report(fpga_tdc_cal_t *out);
/* Vypis histogramu kalibrace (FW >= 0x040B): hist[k] obou kanalu (pocet udalosti
 * kalibrace s kodem k; sirka binu = hist[k] / N * 10 ns). Diagnostika DNL.
 * Vraci false, kdyz odpoved neprisla nebo nese jiny kod. */
bool fpga_freq_tdc_hist(uint8_t k, uint32_t *a, uint32_t *b);

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
bool fpga_freq_set_window(uint8_t mode);

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

#endif /* FPGA_FREQ_H */
