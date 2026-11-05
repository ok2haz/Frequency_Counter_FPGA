/* fpga_range.h -- v3 (audit 2026-10-20, etapa E5): AUTOMATICKA volba predelicky P kanalu. Uzivatel kmitocet NENASTAVUJE
 * (pozadavek 2026-10-20: cokoli z 0,1 Hz .. maximum ~400 MHz). Vstup = odhad kmitoctu (rozsahova sonda /16 nebo mereni),
 * vystup = log2 P (0..4). Cil: f/P <= 30 MHz (TDC ~20 Mznacek/s, primy strop 40 MHz), hystereze proti preblikavani:
 * nahoru kdyz f/P > 33 MHz, dolu kdyz f/(P/2) < 27 MHz. Prepnuti = jeden zahozeny segment (FPGA zmeni P v hold). */
#ifndef FPGA_RANGE_H
#define FPGA_RANGE_H
#include <stdint.h>
#define FPGA_RANGE_UP_HZ    33.0e6
#define FPGA_RANGE_DOWN_HZ  27.0e6
#define FPGA_RANGE_MAXLOG2  4u        /* /16: 400 MHz -> 25 MHz */
uint8_t fpga_range_select(uint8_t cur_log2p, double f_est_hz);
/** Odhad kmitoctu ze sondy: hrany /16 za segment delky seg_s. */
static inline double fpga_range_probe_hz(uint32_t probe, double seg_s) { return seg_s > 0.0 ? 16.0 * (double)probe / seg_s : 0.0; }
int fpga_range_selftest(void);
#endif
