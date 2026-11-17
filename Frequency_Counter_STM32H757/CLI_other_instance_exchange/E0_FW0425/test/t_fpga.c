/* host test: zahrnuje fpga_freq.c (pristup ke statickym funkcim), audit 2026-10-20 optimalizace 3 */
#include "../fpga_freq.c"
#include <stdlib.h>
extern uint8_t g_test_rx[128]; extern int g_test_rx_on;
static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("  OK    %s\n", msg); else { printf("  CHYBA %s\n", msg); fails++; } } while (0)
static void wr(uint8_t *p, uint64_t v, int n) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t s_seq = 100;
/* ramec v2: CH_A f2 (z edge/dt), regrese s relativni odchylkou d, CH_B fb */
static void mk(uint8_t *rx, double f2, double d, double fb, uint16_t fw)
{
    memset(rx, 0, 128);
    rx[0] = 0xA5; rx[1] = 2; rx[2] = 0x80; rx[3] = 0x03; wr(rx + 4, ++s_seq, 4);
    uint8_t *p = rx + 12;
    const double G = 0.25, u = 1.0 / (16384.0 * 100e6);
    uint64_t edges = (uint64_t)(f2 * G + 0.5);
    uint64_t dtu = (uint64_t)((double)edges / f2 / u + 0.5);
    wr(p + 8, edges, 8);                         /* abs 20 edge_count */
    wr(p + 48, fw, 2);                           /* abs 60 fw */
    uint16_t caps = FPGA_CAP_DT | FPGA_CAP_REGR | FPGA_CAP_SELFCHK | FPGA_CAP_CLK | FPGA_CAP_PPS | FPGA_CAP_DIAG0;
    wr(p + 50, caps, 2);
    p[52] = 0x01;                                /* clk OK */
    p[87] = 100;                                 /* clk_mhz */
    wr(p + 106, dtu, 6);                         /* abs 118 dt_a */
    /* regrese: segmenty A, B; xm v 1/256, ym v 1/16 jednotky */
    double fr = f2 * (1.0 + d);
    double xa = 0.125 * edges, xb = 0.875 * edges;
    double ya = xa / f2, yb = ya + (xb - xa) / fr;
    wr(p + 56, 100000, 3); wr(p + 59, (uint64_t)(xa * 256.0), 5); wr(p + 64, (uint64_t)(ya / u * 16.0), 6);
    wr(p + 70, 100000, 3); wr(p + 73, (uint64_t)(xb * 256.0), 5); wr(p + 78, (uint64_t)(yb / u * 16.0), 6);
    p[84] = 3;
    if (fb > 0.0) { uint64_t eb = (uint64_t)(fb * G + 0.5); wr(p + 89, (uint64_t)((double)eb / fb / u + 0.5), 6); wr(p + 96, eb, 4); }
    uint16_t c = crc16_ccitt(rx, FR_CRC_LEN); rx[126] = (uint8_t)c; rx[127] = (uint8_t)(c >> 8);
}
static double gauss(void) { double a = (rand() + 1.0) / (RAND_MAX + 2.0), b = (rand() + 1.0) / (RAND_MAX + 2.0); return sqrt(-2 * log(a)) * cos(6.283185307 * b); }

int main(void)
{
    srand(7);
    printf("T1 puvodni selftesty driveru\n");
    CHECK(fpga_freq_crc_selftest(), "fpga_freq_crc_selftest");
    CHECK(fpga_freq_select_selftest(), "fpga_freq_select_selftest (vcetne regrese, okna, seq)");
    printf("T2 fazovy zaznam\n");
    CHECK(fpga_phrec_selftest() == 1, "fpga_phrec_selftest");
    printf("T3 adaptivni mez regrese (parse_data)\n");
    fpga_freq_regr_reset();
    uint8_t rx[128]; fpga_meas_t m; int acc = 0, n = 0;
    for (int k = 0; k < 300; k++) { mk(rx, 10e6, 2e-10 * gauss(), 0, 0x0425); parse_data(rx, &m); acc += m.regr_used; n++; }
    CHECK(acc >= 298, "stabilni sum 2e-10: prijato >= 298/300");
    double lim = fpga_freq_regr_limit();
    printf("        mez po zahrati = %.2e (ocekavano 5e-9 = dolni mez)\n", lim);
    CHECK(fabs(lim - 5e-9) < 1e-12, "mez = dolni mez 5e-9");
    mk(rx, 10e6, 2e-8, 0, 0x0425); parse_data(rx, &m);
    CHECK(!m.regr_used, "odlehle okno 2e-8 odmitnuto (pevna mez 5e-8 by ho prijala)");
    acc = 0; n = 0; int acc_last = 0;
    for (int k = 0; k < 400; k++) { mk(rx, 10e6, 1.5e-8 * gauss(), 0, 0x0425); parse_data(rx, &m); acc += m.regr_used; if (k >= 200) acc_last += m.regr_used; }
    printf("        skok sumu na 1,5e-8: prijato celkem %d/400, poslednich 200: %d\n", acc, acc_last);
    CHECK(acc_last >= 190, "po skoku sumu se mez znovu nauci (zadne uzamceni): >= 190/200");
    printf("T4 mez primeho mereni (fpga_freq_poll)\n");
    g_init_ok = 1; s_sim_on = 0; g_test_rx_on = 1; g_last_seq = 0xFFFFFFFFu;
    fpga_meas_t o;
    mk(g_test_rx, 39e6, 0, 0, 0x0425); fpga_freq_poll(&o); CHECK(s_poll_chk == FPGA_CHK_OK, "39 MHz, FW 0x0425: prijato");
    mk(g_test_rx, 45e6, 0, 0, 0x0425); fpga_freq_poll(&o); CHECK(s_poll_chk == FPGA_CHK_FMAX, "45 MHz, FW 0x0425: FPGA_CHK_FMAX");
    mk(g_test_rx, 35e6, 0, 0, 0x0424); fpga_freq_poll(&o); CHECK(s_poll_chk == FPGA_CHK_FMAX, "35 MHz, FW 0x0424: FPGA_CHK_FMAX (mez 33 MHz)");
    mk(g_test_rx, 10e6, 0, 45e6, 0x0425); fpga_freq_poll(&o);
    CHECK(s_poll_chk == FPGA_CHK_OK && o.freq16_x100000 == 0u && o.edges_b == 0u, "CH_B 45 MHz: sekundarni slot zneplatnen, primarni plati");
    mk(g_test_rx, 10e6, 0, 20e6, 0x0425); fpga_freq_poll(&o);
    CHECK(o.freq16_x100000 > 0u, "CH_B 20 MHz: sekundarni slot plati");
    uint32_t fm, ch; fpga_freq_check_stats2(&fm, &ch); printf("        check_stats2: fmax=%u coh=%u\n", fm, ch);
    /* koherence: narust coh_cnt -> FPGA_CHK_COH */
    mk(g_test_rx, 10e6, 0, 0, 0x0425); g_test_rx[11] = 0; { uint16_t c = crc16_ccitt(g_test_rx, FR_CRC_LEN); g_test_rx[126]=(uint8_t)c; g_test_rx[127]=(uint8_t)(c>>8);} fpga_freq_poll(&o);
    mk(g_test_rx, 10e6, 0, 0, 0x0425); g_test_rx[11] = 1; { uint16_t c = crc16_ccitt(g_test_rx, FR_CRC_LEN); g_test_rx[126]=(uint8_t)c; g_test_rx[127]=(uint8_t)(c>>8);} fpga_freq_poll(&o);
    CHECK(s_poll_chk == FPGA_CHK_COH, "narust coh_cnt: FPGA_CHK_COH");
    printf("\nVYSLEDEK: %s (%d chyb)\n", fails ? "SELHALO" : "VSE PROSLO", fails);
    return fails ? 1 : 0;
}
