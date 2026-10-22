/* fpga_tdccal.h -- kalibracni tabulka TDC pocitana v STM (FPGA FW >= 0x0427, caps bit4), audit 2026-10-21 S1/S2.
 *
 * FPGA po sberu 2^FPGA_TDC_CAL_LOG2 udalosti ring oscilatoru drzi HISTOGRAM kodu (BRAM) a ceka (tdc_status / abs 112 bit0,1);
 * STM ho precte (`fpga_freq_tdc_hist`), spocita tabulku TIMTO vzorcem, zapise ji (LUT_WRITE, 1 polozka na ramec) a potvrdi
 * (SET_CONFIG 0x06 = 1). Vzorec je 1:1 puvodni vypocet ve FPGA (tdc.v, AUTOCAL = 1; overeno sim/tb_lutequiv.sv):
 *   V[a] = 16384 * (cum_pred + hist[a]/2) / N  =  (2*cum_pred + hist[a]) >> (LOG2 - 13),  x14 = min(V, 16383)
 *   polozka {amt[1:0], neg[13:0]}:  neg = (-x14) mod 2^14,  amt = r_entry + (x14 != 0),  r_entry = 1 (sada R / DUAL = 0)
 * Funkce jsou cista logika bez HW (testovatelne selftestem).
 */
#ifndef FPGA_TDCCAL_H
#define FPGA_TDCCAL_H
#include <stdint.h>
#include <stdbool.h>

#define FPGA_TDCCAL_MAX_NC   1024u     /* nejvetsi tabulka ({sada, kod} do 10 bitu) */

typedef struct {
    uint32_t ovf;      /* hist[posledni adresa]: udalosti za koncem retezu */
    uint32_t peak;     /* nejvetsi hist[k] (mezi neprazdnymi) */
    uint16_t nz;       /* pocet neprazdnych kodu */
    uint16_t last;     /* nejvyssi neprazdny kod */
    uint32_t total;    /* soucet histogramu (ma byt 2^log2n) */
} fpga_tdccal_stats_t;

/** Tabulka z histogramu. `hist[0..nc-1]`, `log2n` = log2 poctu udalosti, `dual` = 1: horni bit adresy = sada (R = 0, F = 1).
 *  `lut[a]` = {amt, neg}. @return soucet histogramu (kontrola: musi byt 1 << log2n) */
uint64_t fpga_tdccal_lut(const uint32_t *hist, uint32_t nc, uint32_t log2n, uint8_t dual, uint16_t *lut);
/** Statistiky retezu (puvodni d_ovf/d_peak/d_nz/d_last z FPGA). */
void fpga_tdccal_stats(const uint32_t *hist, uint32_t nc, fpga_tdccal_stats_t *s);
/** Kontrolni slovo jednoho zapisu (stejne jako ve FPGA, spi_app.v): {0, ch, a9[8:0], 5'b0} ^ neg14; soucet mod 2^16. */
uint16_t fpga_tdccal_chk(uint8_t ch, uint16_t addr, uint16_t neg);
/** Selftest proti zlatym vektorum z FPGA simulace (sim/tb_lutequiv.sv). @return 1 = ok */
int fpga_tdccal_selftest(void);

#endif
