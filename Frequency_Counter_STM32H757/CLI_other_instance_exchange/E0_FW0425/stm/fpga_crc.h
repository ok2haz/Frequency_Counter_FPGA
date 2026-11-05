/* fpga_crc.h -- CRC-16/CCITT-FALSE pro protokol FPGA (polynom 0x1021, init 0xFFFF, bez reflexe, bez xorout).
 * Audit 2026-10-20, optimalizace 6. Dve cesty se STEJNYM vysledkem:
 *   SW: pulbajtova tabulka (vzdy k dispozici, reference),
 *   HW: jednotka CRC STM32H7 (registry POL/INIT/CR/DR), jen kdyz FPGA_CRC_USE_HW = 1 a selftest pri startu prosel.
 * Bezpecnost HW cesty: (1) pri startu porovnani s SW na 33 vektorech, (2) pred KAZDYM vypoctem se jednotka znovu nastavi
 * (jiny modul ji mohl prekonfigurovat), (3) zamek (try-lock, bez cekani -> pri soubehu SW), (4) kontrola PRIJATEHO ramce:
 * kdyz HW rekne "nesedi", prepocita se SW; nesoulad HW/SW = HW se trvale vypne a zapocita. Vychozi FPGA_CRC_USE_HW = 0
 * (chovani beze zmeny). Povoleni: viz 2026-10-20_CRC_DMA_zadani_CLI.md. */
#ifndef FPGA_CRC_H
#define FPGA_CRC_H
#include <stdint.h>
#include <stdbool.h>
#ifndef FPGA_CRC_USE_HW
#define FPGA_CRC_USE_HW 0
#endif
typedef struct { uint32_t hw_calls, sw_calls, hw_busy, mismatch; uint8_t hw_on, selftest_ok; } fpga_crc_stats_t;
void     fpga_crc_init(void);                                  /* pod schedulerem i pred nim; opakovane volani je bezpecne */
uint16_t fpga_crc16_sw(const uint8_t *d, int n);               /* tabulka, vzdy */
uint16_t fpga_crc16(const uint8_t *d, int n);                  /* HW, pokud zapnuto a volne, jinak SW */
bool     fpga_crc16_check(const uint8_t *d, int n, uint16_t expected);   /* kontrola prijateho ramce (pojistka HW/SW) */
void     fpga_crc_stats(fpga_crc_stats_t *o);
#endif
