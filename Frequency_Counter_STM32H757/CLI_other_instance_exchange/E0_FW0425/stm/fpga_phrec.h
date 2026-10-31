/* fpga_phrec.h -- FAZOVY ZAZNAM z teziste segmentu regrese (audit 2026-10-20, etapa E1, otevreny bod O1)
 *
 * Proc: regresni okna nesdileji zadnou znacku, jejich chyby jsou nezavisle a prumer m oken klesa jen jako 1/sqrt(m):
 * bily PM pristroje se v ADEV z regresnich kmitoctu jevi jako bily FM (sklon tau^-1/2; namereno 2,85e-11 @ 0,25 s ...
 * 6,1e-13 @ 400 s presne podle 2,85e-11*sqrt(0,25/tau)). Teziste segmentu A je ale fazovy bod s presnosti segmentoveho
 * prumeru; kmitocet mezi tezisti SOUSEDNICH oken (cc = centroid-to-centroid) sdili body, takze prumer m takovych kmitoctu
 * se TELESKOPICKY zuzi na dva body vzdalene m oken -> bily PM zase dava 1/tau, s sumem teziste misto sumu znacky.
 *
 * Presnost: casy se scitaji CELOCISELNE v jednotkach T_clk/16384 (dt okna, surove z ramce) a 1/16 jednotky (ym);
 * indexy v 1/256 hrany (xm). Hranicni znacka okna se v rozdilu tezisti presne vyrusi (ym je prumer t - ref_ts okna a
 * ref_ts = konec predchoziho okna = soucet dt), takze do cc nevstupuje dvoubodovy sum (~105 ps).
 *
 * Kontinuita: zaznam se prerusi (novy usek) pri dire v SEQ, odmitnutem okne, okne bez regrese, zmene hradla/kanalu/hodin.
 * Kanal: jen primarni slot s platnou regresi (regrese je dnes jen na CH_A).
 *
 * Pouziti:
 *  - fpga_phrec_feed() vola fpga_freq_poll() pro KAZDE nove mereni (vyplni m->cc_ok, m->f_cc_hz, m->gate_cc_ps);
 *  - fpga_phrec_adev()/fpga_phrec_mdev() pocitaji z kruhu poslednich FPGA_PHREC_N fazovych bodu (UART, overeni E1);
 *  - FPGA_STATS_USE_CC = 1 (fpga_freq.c) prepne delku okna pro statistiku na gate_cc_ps -> stavajici pyramida Allana
 *    (Σcykly/Σhradla) pak teleskopuje sama. Vychozi 0, dokud E1 offline nepotvrdi zisk (viz dokument auditu).
 */
#ifndef FPGA_PHREC_H
#define FPGA_PHREC_H
#include <stdint.h>
#include "fpga_freq.h"

#define FPGA_PHREC_N  2048u     /* fazovych bodu v kruhu (8,5 min pri 0,25 s); 8 kB (float) */

typedef struct {
    uint32_t n;          /* bodu v aktualnim useku (max FPGA_PHREC_N) */
    uint32_t runs;       /* pocet useku od startu (preruseni kontinuity) */
    uint32_t fed, used;  /* mereni predano / pouzito */
    double   tau0_s;     /* rozestup bodu (delka okna) */
    double   f_ref_hz;   /* referencni kmitocet useku (prvni okno) */
    double   x_last_s;   /* posledni fazova odchylka [s] */
} fpga_phrec_info_t;

void   fpga_phrec_reset(void);
/** @param gap pocet chybejicich mereni pred timto (fpga_seq_gap), @param ok mereni proslo samokontrolou a miscountem. */
void   fpga_phrec_feed(fpga_meas_t *m, uint32_t gap, int ok);
/** Overlapping ADEV z fazoveho zaznamu pri tau = mm * tau0. @return 0 = malo dat (< 3 cleny). */
double fpga_phrec_adev(uint32_t mm, uint32_t *terms);
/** Modifikovana ADEV (MDEV) z fazoveho zaznamu, tau = mm * tau0. @return 0 = malo dat. */
double fpga_phrec_mdev(uint32_t mm, uint32_t *terms);
void   fpga_phrec_info(fpga_phrec_info_t *o);
/** @return 1 = ok. Synteticky bily PM: ADEV(m) ~ sqrt(3)*sigma/(m*tau0) a MDEV ~ ADEV/sqrt(m)*..., viz .c */
int    fpga_phrec_selftest(void);

#endif
