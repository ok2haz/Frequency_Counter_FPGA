/* t_fpga_v3.c -- host test modulu fpga_v3 (protokol v3, docs/PROTOKOL_V3.md), 2026-10-22.
 * Preklad a beh (z adresare Frequency_Counter_STM32H757):
 *   cc -std=c11 -O2 -Wall -Wextra -ICM7/Core/Inc CM7/Core/Src/fpga_v3.c CM7/Core/Src/fpga_crc.c \
 *      CM7/Core/Src/fpga_tdccal.c tests/host/t_fpga_v3.c -lm -o /tmp/t_fpga_v3 && /tmp/t_fpga_v3
 * Overuje: (1) fpga_v3_selftest() = tytez testy jako na cili; (2) scenare s vypisem (kmitocty 100 Hz .. 30 MHz, sum
 * 0 / 30 / 100 ps, kanal B jiny kmitocet); (3) dlouhy beh 10 MHz, sum 100 ps, 400 oken: rozptyl odhadu z tezist
 * segmentu (LSQ) musi byt vyrazne mensi nez dvoubodovy (smysl v3); (4) pocitadlo CRC na znamem vektoru;
 * (5) fpga_v3_grp_break (preruseni okna jednoho kanalu). */
#include "fpga_v3.h"
#include "fpga_crc.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static fpga_v3_emu_t E;
static fpga_v3_grp_t G;
static uint8_t F[FPGA_V3_LEN], M[FPGA_V3_LEN];
static fpga_v3_rec_t R[FPGA_V3_REC_MAX];
static int fails = 0;

#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  CHYBA: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { int nwin[2]; double sum2[2], sumL[2], ss2[2], ssL[2], max2[2], maxL[2]; int gaps; } res_t;

static void run(double fa, double fb, double sig, uint32_t nseg, uint32_t k, uint32_t seed, int bad_frame, res_t *o)
{
    memset(o, 0, sizeof *o);
    fpga_v3_emu_init(&E, fa, fb, sig, seed);
    fpga_v3_grp_init(&G, k);
    uint32_t expect = 0, fr = 0;
    const double ft[2] = { fa, fb };
    for (uint32_t s = 0; s < nseg; s += 6) {                   /* 6 segmentu = 12 zaznamu na ramec: obsazene bloky 1 a 2 */
        for (int q = 0; q < 6; q++) fpga_v3_emu_segment(&E);
        fpga_v3_emu_frame(&E, F);
        if ((int)fr == bad_frame) F[2 * FPGA_V3_BLK + 2 + 14] ^= 0x40u;   /* blok 2, zaznam 0 (kanal A), bajt T0 */
        fr++;
        fpga_v3_hdr_t h; uint32_t n_ok = 0;
        int rc = fpga_v3_parse(F, &h, R, &n_ok);
        if (rc != FPGA_V3_OK) { printf("  parse rc %d\n", rc); fails++; return; }
        if (h.rec_first != expect) o->gaps++;
        for (uint32_t i = 0; i < n_ok; i++) {
            fpga_v3_win_t w;
            if (fpga_v3_grp_feed(&G, &R[i], h.clk_mhz, &w)) {
                const int c = w.ch;
                const double e2 = w.f_2pt_hz / ft[c] - 1.0, eL = w.f_lsq_hz / ft[c] - 1.0;
                o->nwin[c]++; o->sum2[c] += e2; o->sumL[c] += eL; o->ss2[c] += e2 * e2; o->ssL[c] += eL * eL;
                if (fabs(e2) > o->max2[c]) o->max2[c] = fabs(e2);
                if (fabs(eL) > o->maxL[c]) o->maxL[c] = fabs(eL);
            }
        }
        expect = h.rec_first + n_ok;
        fpga_v3_build_ack(M, fr, expect);
        fpga_v3_emu_rx(&E, M);
    }
}

static double sd(double s, double ss, int n) { return n > 1 ? sqrt((ss - s * s / n) / (n - 1)) : 0.0; }

int main(void)
{
    printf("== fpga_v3 host test ==\n");
    { const uint8_t v[] = "123456789"; uint16_t c = fpga_crc16_sw(v, 9); CHECK(c == 0x29B1u, "CRC vektor 0x%04X", c); }

    int st = fpga_v3_selftest();
    printf("fpga_v3_selftest(): %s (radek %d)\n", st ? "PASS" : "FAIL", fpga_v3_selftest_fail_line());
    CHECK(st, "fpga_v3_selftest");

    static const struct { double fa, fb, sig; const char *nm; } SC[] = {
        { 10e6, 0.0, 0.0, "10 MHz, bez sumu" }, { 12345678.9, 1e6, 30.0, "12,345 MHz + B 1 MHz, 30 ps" },
        { 30e6, 0.0, 100.0, "30 MHz, 100 ps" }, { 1000.0, 0.0, 100.0, "1 kHz, 100 ps" },
        { 100.0, 0.0, 100.0, "100 Hz (2,5 hrany/segment), 100 ps" }, { 2e6 + 0.123, 17e6, 50.0, "2,000000123 MHz + B 17 MHz" },
    };
    for (unsigned i = 0; i < sizeof SC / sizeof SC[0]; i++) {
        res_t o; run(SC[i].fa, SC[i].fb, SC[i].sig, 200u, 10u, 10u + i, -1, &o);
        printf("  %-40s oken A %3d  max|e| 2pt %.2e  LSQ %.2e | B %3d  max|e| 2pt %.2e  LSQ %.2e | mezery %d\n", SC[i].nm,
               o.nwin[0], o.max2[0], o.maxL[0], o.nwin[1], o.max2[1], o.maxL[1], o.gaps);
        CHECK(o.nwin[0] >= 15 && o.gaps == 0, "%s: oken %d, mezery %d", SC[i].nm, o.nwin[0], o.gaps);
        const double tol = (SC[i].sig > 0.0) ? 20.0 * SC[i].sig * 1e-12 / 0.25 : 1e-11;   /* 20 sigma dvoubodove */
        CHECK(o.max2[0] < tol && o.maxL[0] < tol, "%s: chyba nad %.1e", SC[i].nm, tol);
        if (SC[i].fb > 0.0) CHECK(o.nwin[1] >= 15 && o.max2[1] < tol && o.maxL[1] < tol, "%s: kanal B", SC[i].nm);
    }

    /* vadny blok v kazdem 5. ramci: opakovani od ACK, zadna mezera, okna dal */
    { res_t o; run(5e6, 0.0, 0.0, 400u, 10u, 77u, 5, &o);
      printf("  vadny blok 2 v ramci 5: oken %d, mezery %d, max|e| %.2e\n", o.nwin[0], o.gaps, o.max2[0]);
      CHECK(o.gaps == 0 && o.nwin[0] >= 35 && o.maxL[0] < 1e-11, "vadny blok: oken %d mezery %d LSQ %.2e", o.nwin[0], o.gaps, o.maxL[0]); }

    /* dlouhy beh: rozptyl LSQ vs dvoubodovy (10 MHz, 100 ps, 400 oken po 0,25 s) */
    { res_t o; run(10e6, 0.0, 100.0, 4010u, 10u, 99u, -1, &o);
      const double s2 = sd(o.sum2[0], o.ss2[0], o.nwin[0]), sL = sd(o.sumL[0], o.ssL[0], o.nwin[0]);
      const double teor2 = sqrt(2.0) * 100e-12 / 0.25;
      printf("  10 MHz, 100 ps, %d oken: sigma 2pt %.3e (teorie %.3e), sigma LSQ %.3e, pomer %.0fx\n",
             o.nwin[0], s2, teor2, sL, s2 / sL);
      CHECK(o.nwin[0] >= 390, "dlouhy beh: oken %d", o.nwin[0]);
      CHECK(s2 > 0.7 * teor2 && s2 < 1.3 * teor2, "dvoubodova sigma %.3e mimo teorii %.3e", s2, teor2);
      CHECK(sL < s2 / 10.0, "LSQ neni vyrazne lepsi nez dvoubodovy (%.3e vs %.3e)", sL, s2); }


    /* preteceni kruhu za behu (STM chvili necte): okno NESMI preklenout mezeru -> kazde okno ma presne K segmentu */
    {
        fpga_v3_emu_init(&E, 3e6, 0.0, 0.0, 55u); fpga_v3_grp_init(&G, 10u);
        uint32_t expect = 0, fr = 0; int nwin = 0, bad_len = 0, gaps = 0;
        const double up = fpga_v3_unit_ps(100u);
        for (uint32_t s = 0; s < 600u; s += 2) {
            fpga_v3_emu_segment(&E); fpga_v3_emu_segment(&E);
            if (s >= 200u && s < 300u) continue;                    /* 100 segmentu bez cteni -> kruh (64) pretece */
            fpga_v3_emu_frame(&E, F); fr++;
            fpga_v3_hdr_t h; uint32_t n_ok = 0;
            if (fpga_v3_parse(F, &h, R, &n_ok) != FPGA_V3_OK) { fails++; break; }
            if (h.rec_first != expect) gaps++;
            for (uint32_t i = 0; i < n_ok; i++) {
                fpga_v3_win_t w;
                if (fpga_v3_grp_feed(&G, &R[i], h.clk_mhz, &w) && w.ch == 0u) {
                    nwin++;
                    if (fabs((double)w.gate_units * up * 1e-12 - 0.25) > 2.0 / 3e6) bad_len++;
                }
            }
            expect = h.rec_first + n_ok;
            fpga_v3_build_ack(M, fr, expect); fpga_v3_emu_rx(&E, M);
        }
        printf("  preteceni kruhu: oken %d, mezer %d, oken s delkou != K*L %d\n", nwin, gaps, bad_len);
        CHECK(gaps >= 1 && nwin >= 40 && bad_len == 0, "preteceni: mezery %d, oken %d, spatna delka %d", gaps, nwin, bad_len);
    }

    /* kanal 30 s bez signalu (mezi znackami > 2^40 jednotek = 26,8 s) a pak zase signal: rozbaleni T0 musi vzit 48 b */
    {
        fpga_v3_emu_init(&E, 2e6, 0.0, 0.0, 66u); fpga_v3_grp_init(&G, 10u);
        uint32_t expect = 0, fr = 0; int nwin_after = 0; double maxe = 0.0;
        double tc_before = 0.0, tc_after = 0.0; uint32_t sf_before = 0, sf_after = 0; int have_after = 0;
        for (uint32_t s = 0; s < 1500u; s += 2) {
            if (s == 100u) E.f_hz[0] = 0.0;                         /* signal pryc na 30 s */
            if (s == 1300u) { E.f_hz[0] = 2e6; E.t_next[0] = 1.234e-7; }
            fpga_v3_emu_segment(&E); fpga_v3_emu_segment(&E);
            fpga_v3_emu_frame(&E, F); fr++;
            fpga_v3_hdr_t h; uint32_t n_ok = 0;
            if (fpga_v3_parse(F, &h, R, &n_ok) != FPGA_V3_OK) { fails++; break; }
            for (uint32_t i = 0; i < n_ok; i++) {
                fpga_v3_win_t w;
                const int got = fpga_v3_grp_feed(&G, &R[i], h.clk_mhz, &w);
                if (got && w.ch == 0u && s < 100u) { tc_before = w.tc0; sf_before = w.seg_first; }
                else if (got && w.ch == 0u && s > 1300u) {
                    if (!have_after) { tc_after = w.tc0; sf_after = w.seg_first; have_after = 1; }
                    nwin_after++;
                    if (fabs(w.f_2pt_hz / 2e6 - 1.0) > maxe) maxe = fabs(w.f_2pt_hz / 2e6 - 1.0);
                }
            }
            expect = h.rec_first + n_ok;
            fpga_v3_build_ack(M, fr, expect); fpga_v3_emu_rx(&E, M);
        }
        /* absolutni cas: rozdil tezist prvnich segmentu = uplynule segmenty * L (+- 1 perioda + faze vlozena pri navratu) */
        const double dt_s = (tc_after - tc_before) * fpga_v3_unit_ps(100u) * 1e-12;
        const double expect_s = (double)(uint16_t)(sf_after - sf_before) * 0.025;
        printf("  30 s bez signalu a zpet: oken po navratu %d, max|e| %.2e, posun tc0 %.6f s (segmenty %.6f s)\n",
               nwin_after, maxe, dt_s, expect_s);
        CHECK(have_after && fabs(dt_s - expect_s) < 1e-3, "absolutni cas tc0 nenavazuje pres mezeru (%.6f vs %.6f s)", dt_s, expect_s);
        CHECK(nwin_after >= 15 && maxe < 1e-10, "po dlouhe mezere: oken %d, max|e| %.2e", nwin_after, maxe);
    }

    /* kanal B se vyhodnocuje taky (poskozeny zaznam kanalu A v bloku 2 + B s jinym kmitoctem) */
    { res_t o; run(4e6, 7e5, 0.0, 400u, 10u, 88u, 7, &o);
      printf("  vadny blok + kanal B: oken A %d B %d, mezery %d, max|e| A %.2e B %.2e\n", o.nwin[0], o.nwin[1], o.gaps, o.max2[0], o.max2[1]);
      CHECK(o.gaps == 0 && o.nwin[0] >= 35 && o.nwin[1] >= 35 && o.max2[0] < 1e-11 && o.max2[1] < 1e-11, "vadny blok + B"); }

    /* fpga_v3_grp_break: rozpracovane okno kanalu zahodi, druhy kanal necha. Synteticke zaznamy 40 kHz (1000 hran
     * na segment, perioda 4,096e7 jednotek, 10 znacek), K = 4: bez preruseni prvni okno = segmenty 0..3; s prerusenim
     * kanalu A pred segmentem 2 = segmenty 2..5; preruseni kanalu B kanal A neovlivni. */
    for (int brk = 0; brk < 3; brk++) {             /* 0 = bez preruseni, 1 = kanal A, 2 = kanal B */
        fpga_v3_grp_init(&G, 4u);
        fpga_v3_win_t w; int got = 0; uint32_t sf = 0xFFFFFFFFu, ns = 0; double f2 = 0.0;
        for (uint32_t s = 0; s < 8u && !got; s++) {
            if (s == 2u && brk) fpga_v3_grp_break(&G, (uint8_t)(brk - 1));
            const uint64_t P = 40960000ull, n = 10u;
            fpga_v3_rec_t r = { (uint16_t)s, 0u, 0u, (uint32_t)n, 1000ull * s, 1000ull * P * s,
                                n * (n - 1u) / 2u, P * n * (n - 1u) / 2u };
            if (fpga_v3_grp_feed(&G, &r, 100u, &w)) { got = 1; sf = w.seg_first; ns = w.nseg; f2 = w.f_2pt_hz; }
        }
        const uint32_t sf_exp = (brk == 1) ? 2u : 0u;
        printf("  grp_break %s: prvni okno seg %u..%u, f %.6f Hz\n", brk == 0 ? "zadny " : brk == 1 ? "kanal A" : "kanal B",
               (unsigned)sf, (unsigned)(sf + ns - 1u), f2);
        CHECK(got && sf == sf_exp && ns == 4u && fabs(f2 - 40000.0) < 1e-6,
              "grp_break %d: okno seg_first %u (cekano %u), nseg %u, f %.6f", brk, (unsigned)sf, (unsigned)sf_exp,
              (unsigned)ns, f2);
    }

    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
