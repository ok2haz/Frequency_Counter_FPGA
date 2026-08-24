/**
 * @file    fpga_freq.h
 * @brief   SPI2 master driver pro FPGA citac kmitoctu (FPGA = SPI slave).
 *
 * Protokol: pevny 64B full-duplex ramec, STM32 generuje SCK+CS. FPGA vraci
 * posledni hotove mereni v ramci stejneho prenosu. Handshake in-band pres
 * STATUS/FLAGS. ACK (TYPE 0x06) potvrzuje prijatou SEQUENCE.
 *
 * SPI: mode 0, MSB first, 8-bit, CS=PB12 active-low (manualni GPIO),
 *      bring-up ~1 MHz (viz FPGA_SCK_TARGET_HZ).
 */
#ifndef FPGA_FREQ_H
#define FPGA_FREQ_H

#include <stdint.h>
#include <stdbool.h>

/* Jedno mereni (payload TYPE=0x80 DATA).
 * 4-fazove reciproke mereni, dva preddelice: pin28 /4 (primar, nejlepsi rozliseni),
 * pin27 /16 (vyssi rozsah / cross-check). freq*_x100000 = realny kmitocet x 100000
 * (delicka uz zahrnuta ve FPGA -> NENASOBIT 4 ani 16). */
typedef struct {
    uint64_t frequency_x100000;   /* pin28 /4: kmitocet v jednotkach 1/100000 Hz (5 des. mist) */
    uint64_t edge_count;          /* pocet period v okne (pin28, diagnostika) */
    uint64_t gate_time_ns;        /* skutecne Dt okna [ns] ~250e6 (mirne kolisa) */
    uint64_t timestamp_ticks;     /* 10 MHz ticky */
    uint64_t freq16_x100000;      /* pin27 /16: kmitocet x 100000 (vyssi rozsah) */
    uint32_t error_flags;         /* bit0=meas err(/4), bit1=SIGNAL_LOST, bit2=overflow */
    uint32_t sequence;
    uint8_t  channel_id;
    uint8_t  measurement_status;  /* bit0=DATA_VALID, bit1=DATA_FRESH */
    uint8_t  status_flags;        /* STATUS/FLAGS byte z ramce (offset 3) */
    uint8_t  phase_status;        /* bity3:0=present[3:0] (zive faze), bity7:4=fine_seen[3:0] */
    uint8_t  status2;             /* bit0 = chyba deleni pin27 (/16) */
} fpga_meas_t;

/* error_flags bity */
#define FPGA_ERR_MEAS         (1u << 0)   /* pin28 (/4): Dt==0 / zadny signal v okne */
#define FPGA_ERR_SIGNAL_LOST  (1u << 1)   /* watchdog ~2.5 s bez mereni; zaroven VALID=0 */
#define FPGA_ERR_OVERFLOW     (1u << 2)   /* okno > ~21.5 s (extremne nizky f) */
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

/** Jeden 64B full-duplex prenos (posle ACK posledni seq, prijme aktualni ramec).
 *  @return true pokud prislo NOVE platne cerstve mereni (CRC ok, VALID, FRESH, nova SEQUENCE). */
bool fpga_freq_poll(fpga_meas_t *out);

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

/** Diagnostika: jeden 64B prenos (posle ACK), syrova odpoved do rx64 (min. 64 B).
 *  @return true pokud HAL prenos prosel (rika nic o platnosti dat). */
bool fpga_freq_raw_xfer(uint8_t *rx64);

/** Naformatuje stav SPI + komunikace: "SPI <x.xx>MHZ LINK:OK SEQ:<n> CRC:<n>".
 *  (rychlost SCK, stav linky, posledni potvrzena SEQ, pocet CRC chyb). */
void fpga_freq_format_status(char *buf, int buflen);

/* ══════════════ Emulator FPGA ramcu (vyvoj bez osazene FPGA desky) ══════════
 * Misto `xfer()` slozi SYNTETICKY 64B DATA ramec. Vsechno za tim — kontrola
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
 *  @return 0 = nezname jmeno poruchy. */
int  fpga_sim_fault(const char *what);

#endif /* FPGA_FREQ_H */
