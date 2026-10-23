/* t_fpga_v3.c -- host test modulu fpga_v3 (protokol v3, docs/PROTOKOL_V3.md), 2026-10-22.
 * Preklad a beh (z adresare Frequency_Counter_STM32H757):
 *   cc -std=c11 -O2 -Wall -Wextra -ICM7/Core/Inc CM7/Core/Src/fpga_v3.c CM7/Core/Src/fpga_crc.c \
 *      CM7/Core/Src/fpga_tdccal.c tests/host/t_fpga_v3.c -lm -o /tmp/t_fpga_v3 && /tmp/t_fpga_v3
 * Overuje: (1) fpga_v3_selftest() = tytez testy jako na cili; (2) scenare s vypisem (kmitocty 100 Hz .. 30 MHz, sum
 * 0 / 30 / 100 ps, kanal B jiny kmitocet); (3) dlouhy beh 10 MHz, sum 100 ps, 400 oken: rozptyl odhadu z tezist
 * segmentu (LSQ) musi byt vyrazne mensi nez dvoubodovy (smysl v3); (4) pocitadlo CRC na znamem vektoru;
 * (5) fpga_v3_grp_break (preruseni okna jednoho kanalu); (6) nizke kmitocty 33 Hz .. 0,3 Hz a vypadek signalu;
 * (7) prijem se zpozdenym ACK jako u RTL (fpga_v3_link_take: duplicity, mezera, reset FPGA);
 * (8) model smycky FpgaTasku: hradlo 50 ms .. 1 s, kazde okno prave jednou a souvisle, transakce za sekundu;
 * (9) jednorazove deje: casy udalosti, interval A->B (start/stop), zpozdeni A/B, 200 s ticho (T0 pretece);
 * (10) jemny cas 1PPS (FW 0x0502): PFLAGS, PPS_STATUS, d, volba jemny / hruby (fpga_v3_pps_fine);
 * (11) automaticky rozsah predelicky (fpga_v3_range_select); (12) emulator s predelickou (FW 0x0503): okna v periodach;
 * (13) hlavicka PSRAM (FW 0x0505): DEEP_N, DEEP_ST, latence, okno, potvrzeni T_RAW (bajt 127). */
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

typedef struct { int nwin[2], nlsq[2]; double sum2[2], sumL[2], ss2[2], ssL[2], max2[2], maxL[2]; int gaps; } res_t;

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
                o->nwin[c]++; o->sum2[c] += e2; o->ss2[c] += e2 * e2;
                if (fabs(e2) > o->max2[c]) o->max2[c] = fabs(e2);
                if (w.lsq_ok) {                                  /* LSQ jen s >= 2 segmenty s hranou */
                    o->nlsq[c]++; o->sumL[c] += eL; o->ssL[c] += eL * eL;
                    if (fabs(eL) > o->maxL[c]) o->maxL[c] = fabs(eL);
                }
            }
        }
        expect = h.rec_first + n_ok;
        fpga_v3_build_ack(M, fr, expect);
        fpga_v3_emu_rx(&E, M);
    }
}

static double sd(double s, double ss, int n) { return n > 1 ? sqrt((ss - s * s / n) / (n - 1)) : 0.0; }

/* udalost pro test fpga_v3_ab_feed; N >= 3 = souhrn (presna jen prvni hrana) */
static fpga_v3_ev_t evk(uint8_t ch, uint64_t t_u, uint32_t n, double per_u)
{
    fpga_v3_ev_t e;
    memset(&e, 0, sizeof e);
    e.ch = ch; e.kind = (n >= 3u) ? FPGA_V3_EV_BURST : FPGA_V3_EV_EDGE; e.n = n; e.t_u = t_u; e.per_u = per_u;
    return e;
}

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
        CHECK(o.nwin[0] >= 15 && o.gaps == 0 && o.nlsq[0] == o.nwin[0], "%s: oken %d (LSQ %d), mezery %d", SC[i].nm,
              o.nwin[0], o.nlsq[0], o.gaps);
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

    /* (10) v0.37.0 (FW 0x0502): jemny cas 1PPS. Ramec z emulatoru, do hlavicky PTS s nenulovym zlomkem, PFLAGS, PPS_STATUS,
     * d a hruby citac; jemny cas se pouzije jen s CAPS bit17 + PCLK bezi + PFLAGS bit1, jinak hruby (zlomek 0). */
    {
        fpga_v3_emu_init(&E, 10e6, 0.0, 0.0, 77u);
        fpga_v3_emu_frame(&E, F);
        const uint64_t pts = (0x2ABCDEF01ull << 14) | (5u << 11);          /* takt 0x2ABCDEF01, poloha IDES8 5 */
        for (int i = 0; i < 6; i++) { F[72 + i] = (uint8_t)(pts >> (8 * i)); F[88 + i] = (uint8_t)((pts - (100000000ull << 14)) >> (8 * i)); }
        F[68] = 7u; F[84] = 6u; F[78] = 3u; F[94] = 1u; F[79] = 0xFEu; F[95] = 2u;
        for (int i = 0; i < 4; i++) { F[100 + i] = (uint8_t)(0xABCDEF03u >> (8 * i)); F[104 + i] = (uint8_t)(0xA6FA2B03u >> (8 * i)); }
        F[26] |= 0x02u; F[50] = 0x33u;                                     /* CAPS bit17; epocha 3, bit1 platne, bit0 PCLK */
        fpga_v3_blk_seal(F);
        fpga_v3_hdr_t h; uint32_t n_ok = 0, tk = 0; uint16_t fr = 0;
        CHECK(fpga_v3_parse(F, &h, R, &n_ok) == FPGA_V3_OK, "1PPS jemny: parse");
        CHECK(h.pps[0].d == -2 && h.pps[1].d == 2 && h.pps[0].pflags == 3u && FPGA_V3_PPS_EPOCH(h.pps_status) == 3u,
              "1PPS jemny: d %d/%d, PFLAGS %u, epocha %u", h.pps[0].d, h.pps[1].d, h.pps[0].pflags, FPGA_V3_PPS_EPOCH(h.pps_status));
        CHECK(fpga_v3_pps_fine(&h, 0, &tk, &fr) == 1 && tk == 0xABCDEF01u && fr == (5u << 11),
              "1PPS jemny: takty 0x%08lX zlomek %u", (unsigned long)tk, (unsigned)fr);
        CHECK(fpga_v3_pps_fine(&h, 1, &tk, &fr) == 0 && tk == 0xA6FA2B03u && fr == 0u, "1PPS predchozi hruby (PFLAGS bit1 = 0)");
        fpga_v3_hdr_t h2 = h; h2.caps &= ~FPGA_V3_CAP_PPS_IDES;
        CHECK(fpga_v3_pps_fine(&h2, 0, &tk, &fr) == 0 && tk == 0xABCDEF03u && fr == 0u, "1PPS bez CAPS bit17 = hruby");
        h2 = h; h2.pps_status &= (uint8_t)~FPGA_V3_PPS_ST_LOCK;
        CHECK(fpga_v3_pps_fine(&h2, 0, &tk, &fr) == 0 && tk == 0xABCDEF03u, "1PPS bez PCLK = hruby");
    }
    /* (13) v0.39.0 (FW 0x0505): hlavicka PSRAM -- DEEP_N (u24), DEEP_ST, latence zapisu, posledni okno */
    {
        fpga_v3_emu_init(&E, 10e6, 0.0, 0.0, 99u);
        fpga_v3_emu_frame(&E, F);
        F[124] = 0x2Au; F[125] = 0xAAu; F[126] = 0x0Au; F[127] = 0x85u;      /* bajt 127: kanal B, 5 prijatych (Gray) */
        F[128] = (uint8_t)(FPGA_V3_DS_RDY | FPGA_V3_DS_WDONE); F[129] = 4u; F[130] = 0x55u; F[131] = 0x05u;
        fpga_v3_blk_seal(F);
        fpga_v3_hdr_t h; uint32_t n_ok = 0;
        CHECK(fpga_v3_parse(F, &h, R, &n_ok) == FPGA_V3_OK, "PSRAM: parse");
        CHECK(h.deep_n == 0x0AAA2Au && h.deep_st == (FPGA_V3_DS_RDY | FPGA_V3_DS_WDONE) && h.deep_wsel == 4u && h.deep_win == 0x0555u,
              "PSRAM: DEEP_N 0x%06lX, stav 0x%02X, latence %u, okno 0x%04X", (unsigned long)h.deep_n, (unsigned)h.deep_st,
              (unsigned)h.deep_wsel, (unsigned)h.deep_win);
        CHECK(h.deep_ack == 0x85u && FPGA_V3_DACK_N(h.deep_ack) == 5u && (h.deep_ack & FPGA_V3_DACK_CH),
              "PSRAM: bajt 127 = 0x%02X (potvrzeni T_RAW, audit F-0272)", (unsigned)h.deep_ack);
    }

    /* (11) v0.38.0: automaticky rozsah predelicky -- rozmitani 0,1 Hz .. 420 MHz nahoru a dolu: f/P <= 33 MHz (krome /16),
     * vysledek stabilni (znovu stejna volba), dolu az pod 27 MHz na P/2; pevne body a hystereze */
    {
        int bad = 0; uint8_t p = 0u;
        for (double f = 0.1; f < 420e6; f *= 1.01) {
            const uint8_t q = fpga_v3_range_select(p, f);
            if (q < FPGA_V3_RANGE_MAXLOG2 && f / (double)(1u << q) > FPGA_V3_RANGE_UP_HZ) bad++;
            if (fpga_v3_range_select(q, f) != q) bad++;
            p = q;
        }
        for (double f = 420e6; f > 0.1; f /= 1.01) {
            const uint8_t q = fpga_v3_range_select(p, f);
            if (q < FPGA_V3_RANGE_MAXLOG2 && f / (double)(1u << q) > FPGA_V3_RANGE_UP_HZ) bad++;
            if (q > 0u && f / (double)(1u << (q - 1u)) < FPGA_V3_RANGE_DOWN_HZ) bad++;
            p = q;
        }
        CHECK(bad == 0, "rozsah: rozmitani, %d chyb", bad);
        CHECK(fpga_v3_range_select(0u, 10e6) == 0u && fpga_v3_range_select(0u, 200e6) == 3u && fpga_v3_range_select(0u, 400e6) == 4u,
              "rozsah: 10 / 200 / 400 MHz -> %u %u %u", fpga_v3_range_select(0u, 10e6), fpga_v3_range_select(0u, 200e6),
              fpga_v3_range_select(0u, 400e6));
        CHECK(fpga_v3_range_select(2u, 60e6) == 2u && fpga_v3_range_select(1u, 60e6) == 1u, "rozsah: hystereze 60 MHz drzi P 2 i 4");
        CHECK(fpga_v3_range_select(3u, 0.0) == 3u, "rozsah: bez odhadu beze zmeny");
    }

    /* (12) v0.38.0: emulator s predelickou jako RTL. B 143,3 MHz: SET_CONFIG 0x07 B = 3 (/8), pozdeji 4 (/16); okna B maji
     * f = P x hrany / cas (2pt i LSQ), p_log2 okna, zadne okno pres zmenu P; PRESC a PROBE v hlavicce, 2 neplatne B. */
    {
        const double fb = 143.3e6;
        fpga_v3_emu_init(&E, 10e6, fb, 30.0, 88u); fpga_v3_grp_init(&G, 4u);
        fpga_v3_build_cfg(M, 1u, FPGA_V3_CFG_PRESC, 1u, 3u); fpga_v3_emu_rx(&E, M);
        uint32_t expect = 0, fr = 0; int nw[5] = { 0 }, ninv = 0, badw = 0, mixed = 0; double maxe = 0.0;
        uint8_t pr_seen = 0xFFu; uint16_t probe_b = 0u;
        for (uint32_t s = 0; s < 240u; s += 6) {
            if (s == 120u) { fpga_v3_build_cfg(M, 2u, FPGA_V3_CFG_PRESC, 1u, 4u); fpga_v3_emu_rx(&E, M); }
            for (int q = 0; q < 6; q++) fpga_v3_emu_segment(&E);
            fpga_v3_emu_frame(&E, F); fr++;
            fpga_v3_hdr_t h; uint32_t n_ok = 0;
            if (fpga_v3_parse(F, &h, R, &n_ok) != FPGA_V3_OK) { badw++; break; }
            if (s == 60u) { pr_seen = h.presc[1]; probe_b = h.probe[1]; }
            for (uint32_t i = 0; i < n_ok; i++) {
                if ((R[i].ch & 1u) && (R[i].flags & FPGA_V3_R_INVALID)) ninv++;
                fpga_v3_win_t w;
                if (fpga_v3_grp_feed(&G, &R[i], h.clk_mhz, &w) && w.ch == 1u) {
                    nw[w.p_log2 <= 4u ? w.p_log2 : 0u]++;
                    const double e2 = fabs(w.f_2pt_hz / fb - 1.0), eL = w.lsq_ok ? fabs(w.f_lsq_hz / fb - 1.0) : 0.0;
                    if (e2 > maxe) maxe = e2;
                    if (eL > maxe) maxe = eL;
                    if (w.p_log2 != 3u && w.p_log2 != 4u) mixed++;
                }
            }
            expect = h.rec_first + n_ok;
            fpga_v3_build_ack(M, fr + 10u, expect); fpga_v3_emu_rx(&E, M);
        }
        printf("  predelicka: B 143,3 MHz, oken P/8 %d, P/16 %d, neplatnych B %d, max |e| %.1e, PRESC B 0x%02X, PROBE B %u\n",
               nw[3], nw[4], ninv, maxe, (unsigned)pr_seen, (unsigned)probe_b);
        CHECK(badw == 0 && nw[3] >= 10 && nw[4] >= 10 && mixed == 0, "predelicka: okna P/8 %d P/16 %d (jine P %d)", nw[3], nw[4], mixed);
        CHECK(maxe < 1e-8, "predelicka: kmitocet okna B %.2e", maxe);
        CHECK(ninv == 4, "predelicka: neplatne zaznamy B kolem 2 zmen = 4 (je %d)", ninv);
        CHECK(pr_seen == 3u && probe_b == (uint16_t)(fb / 16000.0), "predelicka: PRESC B 0x%02X, PROBE B %u", (unsigned)pr_seen, (unsigned)probe_b);
    }

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

    /* nizke kmitocty (perioda > L = 25 ms, vetsina segmentu N = 0): okno se prodlouzi do prvni hrany po K segmentech
     * (reciprocni citac), kmitocet dvoubodove presne; LSQ jen kdyz okno ma >= 2 segmenty s hranou. Pod 0,4 Hz
     * (perioda > 2,5 s) ztrata signalu jako ve v2 -> zadne okno. */
    {
        static const struct { double f; uint32_t nseg; int nmin, nmax; const char *nm; } LF[] = {
            { 33.0, 1200u, 100, 125, "33 Hz (N 0/1)" }, { 10.0, 1200u, 85, 105, "10 Hz" },
            { 1.0, 2400u, 55, 60, "1 Hz" }, { 0.5, 2400u, 25, 30, "0,5 Hz" }, { 0.3, 2400u, 0, 0, "0,3 Hz (ztrata)" },
        };
        for (unsigned i = 0; i < sizeof LF / sizeof LF[0]; i++) {
            res_t o; run(LF[i].f, 0.0, 30.0, LF[i].nseg, 10u, 40u + i, -1, &o);
            const double tol = 20.0 * sqrt(2.0) * 30e-12 / 0.25;
            printf("  %-40s oken %3d (LSQ %3d)  max|e| 2pt %.2e  LSQ %.2e | mezery %d\n", LF[i].nm, o.nwin[0], o.nlsq[0],
                   o.max2[0], o.maxL[0], o.gaps);
            CHECK(o.nwin[0] >= LF[i].nmin && o.nwin[0] <= LF[i].nmax && o.gaps == 0, "%s: oken %d (cekano %d..%d)",
                  LF[i].nm, o.nwin[0], LF[i].nmin, LF[i].nmax);
            CHECK(o.max2[0] < tol && o.maxL[0] < tol, "%s: chyba 2pt %.2e LSQ %.2e nad %.1e", LF[i].nm, o.max2[0], o.maxL[0], tol);
        }
    }

    /* vypadek signalu: okno, ktere by ho preklenulo, ma spatny kmitocet (chybi hrany) -> musi se zahodit.
     * (a) 5 MHz, 1 segment bez signalu (3 periody << L -> kazdy prazdny segment = ztrata);
     * (b) 1 Hz, 4 s bez signalu (> 3 periody i > 2,5 s). Kazde okno musi mit spravny kmitocet. */
    for (int v = 0; v < 2; v++) {
        const double f = v ? 1.0 : 5e6;
        const uint32_t off0 = v ? 800u : 100u, off1 = v ? 960u : 101u, nseg = v ? 2400u : 400u;
        fpga_v3_emu_init(&E, f, 0.0, 0.0, 90u + (uint32_t)v); fpga_v3_grp_init(&G, 10u);
        uint32_t expect = 0, fr = 0; int nwin = 0; double maxe = 0.0;
        for (uint32_t s = 0; s < nseg; s += 6) {
            for (int q = 0; q < 6; q++) {
                const uint32_t si = s + (uint32_t)q;
                E.f_hz[0] = (si >= off0 && si < off1) ? 0.0 : f;   /* emulator: bez signalu hrany nepribyvaji */
                fpga_v3_emu_segment(&E);
            }
            fpga_v3_emu_frame(&E, F); fr++;
            fpga_v3_hdr_t h; uint32_t n_ok = 0;
            if (fpga_v3_parse(F, &h, R, &n_ok) != FPGA_V3_OK) { fails++; break; }
            for (uint32_t i = 0; i < n_ok; i++) {
                fpga_v3_win_t w;
                if (fpga_v3_grp_feed(&G, &R[i], h.clk_mhz, &w) && w.ch == 0u) {
                    nwin++;
                    if (fabs(w.f_2pt_hz / f - 1.0) > maxe) maxe = fabs(w.f_2pt_hz / f - 1.0);
                }
            }
            expect = h.rec_first + n_ok;
            fpga_v3_build_ack(M, fr, expect); fpga_v3_emu_rx(&E, M);
        }
        printf("  vypadek signalu %s: oken %d, max|e| %.2e\n", v ? "1 Hz, 4 s   " : "5 MHz, 25 ms", nwin, maxe);
        CHECK(nwin >= (v ? 50 : 30) && maxe < 1e-9, "vypadek %d: oken %d, max|e| %.2e (okno preklenulo vypadek)", v, nwin, maxe);
    }

    /* prijem se zpozdenym ACK (model RTL): kazdy zaznam prave jednou; preteceni (STM 3 s necte) = jedna mezera s poctem
     * ztracenych zaznamu; reset FPGA = resynchronizace */
    {
        static fpga_v3_link_t L;
        fpga_v3_link_init(&L);
        fpga_v3_emu_init(&E, 5e6, 2e6, 0.0, 123u);
        fpga_v3_emu_frame(&E, F);
        uint32_t got = 0, bad = 0, lost_expect = 0;
        for (uint32_t t = 0; t < 400u; t++) {
            const int pause = (t >= 100u && t < 102u);                    /* 2 transakce vynechane = 2 x 60 segmentu */
            if (t == 300u) { fpga_v3_emu_init(&E, 5e6, 2e6, 0.0, 124u); fpga_v3_emu_frame(&E, F); }
            if (!pause) {
                fpga_v3_hdr_t h; uint32_t n_ok = 0, i0 = 0; int gap = 0;
                if (fpga_v3_parse(F, &h, R, &n_ok) != FPGA_V3_OK) { bad++; break; }
                const uint32_t n = fpga_v3_link_take(&L, &h, n_ok, &i0, &gap);
                for (uint32_t i = 0; i < n; i++)
                    if (R[i0 + i].seg_idx != (uint16_t)((h.rec_first + i0 + i) >> 1) || R[i0 + i].ch != ((h.rec_first + i0 + i) & 1u)) bad++;
                got += n;
                fpga_v3_build_ack(M, t, L.expect);
            }
            const int nseg = pause ? 60 : 4;                               /* 4 segmenty = 8 zaznamu na transakci */
            for (int q = 0; q < nseg; q++) fpga_v3_emu_segment(&E);
            if (pause && t == 100u) lost_expect = E.rec_head;              /* (hrube; presny pocet ztracenych z mezery) */
            fpga_v3_emu_frame(&E, F);
            if (!pause) fpga_v3_emu_rx(&E, M);
        }
        printf("  zpozdeny ACK: zaznamu %u, duplicit %u, mezer %u (ztraceno %u), resync %u, chyb %u\n",
               (unsigned)got, (unsigned)L.dups, (unsigned)L.gaps, (unsigned)L.gap_recs, (unsigned)L.resync, (unsigned)bad);
        (void)lost_expect;
        CHECK(bad == 0 && L.dups > 1000 && L.gaps == 1 && L.gap_recs > 0 && L.resync == 1, "zpozdeny ACK / mezera / resync");
        CHECK(got + L.gap_recs >= 2400u, "prijato + ztraceno = vse (%u + %u)", (unsigned)got, (unsigned)L.gap_recs);
    }

    /* model smycky FpgaTasku (poll_v3): volani po `delay` ms (FpgaTask: hradlo <= 50 ms -> 10 ms, <= 100 ms -> 25 ms, jinak 50 ms),
     * transakce jen pri prazdne fronte a nejdriv `pms` ms po predchozi, fronta 8 oken, jedno okno na volani, emulator se
     * zpozdenym ACK (jako RTL). Hradlo 50 ms (K = 2) a 1 s (K = 40), 60 s: kazde okno prave jednou a souvisle. */
    {
        static const struct { uint32_t k, delay, pms; } TC[] = { { 2u, 10u, 40u }, { 2u, 10u, 0u }, { 40u, 50u, 40u }, { 4u, 25u, 40u } };
        for (unsigned c = 0; c < sizeof TC / sizeof TC[0]; c++) {
            static fpga_v3_link_t L;
            static fpga_v3_win_t Q[8];
            fpga_v3_link_init(&L);
            fpga_v3_emu_init(&E, 10e6, 3.3e6, 30.0, 300u + c);
            fpga_v3_grp_init(&G, TC[c].k);
            fpga_v3_emu_frame(&E, F);
            uint32_t qr = 0, qw = 0, qdrop = 0, xfers = 0, wins = 0, bad = 0, gap = 0, last_x = 0, t_last = 0;
            uint32_t seg_ms = 0, next_end = 0; int have = 0; double maxe = 0.0;
            for (uint32_t t = 0; t < 60000u; t += TC[c].delay + 1u) {           /* +1 ms: prenos 1024 B + zpracovani */
                while (seg_ms + 25u <= t) { fpga_v3_emu_segment(&E); seg_ms += 25u; }   /* FPGA: segment po 25 ms */
                if (qr == qw && (t - t_last) >= TC[c].pms) {
                    t_last = t; xfers++;
                    fpga_v3_hdr_t h; uint32_t n_ok = 0, i0 = 0; int g = 0;
                    if (fpga_v3_parse(F, &h, R, &n_ok) != FPGA_V3_OK) { bad++; break; }
                    const uint32_t n = fpga_v3_link_take(&L, &h, n_ok, &i0, &g);
                    if (g && L.frames > 1u) gap++;
                    for (uint32_t i = 0; i < n; i++) {
                        fpga_v3_win_t w;
                        if (fpga_v3_grp_feed(&G, &R[i0 + i], 100u, &w) && w.ch == 0u) {
                            if ((uint32_t)(qw - qr) >= 8u) { qr++; qdrop++; }
                            Q[qw % 8u] = w; qw++;
                        }
                    }
                    fpga_v3_build_ack(M, xfers, L.expect);
                    fpga_v3_emu_frame(&E, F);                                   /* dalsi ramec PRED zpracovanim ACK (RTL) */
                    fpga_v3_emu_rx(&E, M);
                }
                if (qr != qw) {                                                 /* FpgaTask dostane jedno okno */
                    const fpga_v3_win_t *w = &Q[qr % 8u]; qr++; wins++;
                    if (have && (uint16_t)(w->seg_first - next_end) != 0u) bad++;   /* souvislost: konec = zacatek dalsiho */
                    if (have && w->x_first != last_x) bad++;                     /* dvoubodove teleskopuje: prvni znacka = konec predchoziho */
                    next_end = (uint16_t)(w->seg_first + w->nseg); last_x = w->x_first + w->edges; have = 1;
                    if (fabs(w->f_2pt_hz / 10e6 - 1.0) > maxe) maxe = fabs(w->f_2pt_hz / 10e6 - 1.0);
                }
            }
            const uint32_t expect_w = 60000u / (25u * TC[c].k) - 2u;
            printf("  FpgaTask K %2u (hradlo %4u ms), volani po %2u ms, transakce >= %2u ms: oken %u (cekano >= %u), transakci %.1f/s, "
                   "duplicit %u, mezer %u, zahozeno %u, chyb %u, max|e| %.1e\n", (unsigned)TC[c].k, (unsigned)(25u * TC[c].k),
                   (unsigned)TC[c].delay, (unsigned)TC[c].pms, (unsigned)wins, (unsigned)expect_w, xfers / 60.0, (unsigned)L.dups,
                   (unsigned)gap, (unsigned)qdrop, (unsigned)bad, maxe);
            CHECK(bad == 0 && gap == 0 && qdrop == 0 && wins >= expect_w && maxe < 1e-8,
                  "FpgaTask K %u: oken %u, mezer %u, zahozeno %u, chyb %u", (unsigned)TC[c].k, (unsigned)wins, (unsigned)gap, (unsigned)qdrop, (unsigned)bad);
        }
    }

    /* fpga_v3_ab_feed po krocich (casy v jednotkach T/16384): (1) B predchozi periody v teze davce pred dalsim A -- bez
     * serazeni by A prisel pred stopem (ignorovan) a start by se ztratil; (2) stop jen PO startu (B v tomtez okamziku
     * neni stop); (3) souhrn B zacal pred startem a trva pres nej -> start zahodit (prvni hrana B po startu neznama) */
    {
        static fpga_v3_ab_t P; fpga_v3_ev_t e[2]; int64_t dt[4]; uint32_t n1, n2, n3; int64_t d2;
        fpga_v3_ab_init(&P, 1000000u);
        e[0] = evk(0u, 1000u, 1u, 0.0);
        n1 = fpga_v3_ab_feed(&P, e, 1u, dt, 4u);                               /* start 1000 */
        e[0] = evk(0u, 2000u, 1u, 0.0); e[1] = evk(1u, 1500u, 1u, 0.0);       /* poradi ovladace: A, pak B */
        n2 = fpga_v3_ab_feed(&P, e, 2u, dt, 4u); d2 = dt[0];                   /* stop 1500, start 2000 */
        e[0] = evk(1u, 2500u, 1u, 0.0);
        n3 = fpga_v3_ab_feed(&P, e, 1u, dt, 4u);                               /* stop 2500 */
        CHECK(n1 == 0u && n2 == 1u && d2 == 500 && n3 == 1u && dt[0] == 500 && P.lost == 0u,
              "ab_feed serazeni: %u %u %u, dt %lld", (unsigned)n1, (unsigned)n2, (unsigned)n3, (long long)d2);
        fpga_v3_ab_init(&P, 1000000u);
        e[0] = evk(0u, 3000u, 1u, 0.0); e[1] = evk(1u, 3000u, 1u, 0.0);
        n1 = fpga_v3_ab_feed(&P, e, 2u, dt, 4u);                               /* B soucasne se startem: neni stop */
        e[0] = evk(1u, 3300u, 1u, 0.0);
        n2 = fpga_v3_ab_feed(&P, e, 1u, dt, 4u);
        CHECK(n1 == 0u && n2 == 1u && dt[0] == 300 && P.n == 1u, "ab_feed stop po startu: %u %u, dt %lld",
              (unsigned)n1, (unsigned)n2, (long long)dt[0]);
        fpga_v3_ab_init(&P, 1000000u);
        e[0] = evk(0u, 5000u, 1u, 0.0); e[1] = evk(1u, 4900u, 5u, 100.0);     /* souhrn B 4900..5300 pres start 5000 */
        n1 = fpga_v3_ab_feed(&P, e, 2u, dt, 4u);
        e[0] = evk(1u, 6000u, 1u, 0.0);
        n2 = fpga_v3_ab_feed(&P, e, 1u, dt, 4u);
        CHECK(n1 == 0u && n2 == 0u && P.lost == 1u && !P.armed, "ab_feed souhrn B pres start: %u %u, ztraceno %u",
              (unsigned)n1, (unsigned)n2, (unsigned)P.lost);
    }

    /* fpga_v3_pr_rec (zaznamy -> parovani jako v ovladaci): B se znackami pri CS = stop mohl chybet -> start pryc; po
     * prepnuti zdroje B (fpga_v3_pr_switch) az za zaznamem B s drzenim (FLAGS bit0); A a B ruznych segmentu */
    {
        static fpga_v3_pr_t Q; fpga_v3_rec_t ra, rb; fpga_v3_ev_t e1[1]; int64_t dt[4]; uint32_t n1, n2, n3;
        memset(&ra, 0, sizeof ra); memset(&rb, 0, sizeof rb); rb.ch = 1u;
        fpga_v3_pr_init(&Q, 1000000u);
        ra.seg_idx = 10u; e1[0] = evk(0u, 1000u, 1u, 0.0); n1 = fpga_v3_pr_rec(&Q, &ra, e1, 1u, 0, 1.0, dt, 4u);
        rb.seg_idx = 10u; rb.flags = FPGA_V3_R_CS; e1[0] = evk(1u, 1200u, 1u, 0.0); n2 = fpga_v3_pr_rec(&Q, &rb, e1, 1u, 0, 1.0, dt, 4u);
        rb.seg_idx = 11u; rb.flags = 0u; e1[0] = evk(1u, 2200u, 1u, 0.0); n3 = fpga_v3_pr_rec(&Q, &rb, e1, 1u, 0, 1.0, dt, 4u);
        CHECK(n1 == 0u && n2 == 0u && n3 == 0u && !Q.ab.armed, "pr_rec B pri CS: %u %u %u", (unsigned)n1, (unsigned)n2, (unsigned)n3);
        ra.seg_idx = 12u; e1[0] = evk(0u, 3000u, 1u, 0.0); (void)fpga_v3_pr_rec(&Q, &ra, e1, 1u, 0, 1.0, dt, 4u);
        rb.seg_idx = 13u; e1[0] = evk(1u, 3400u, 1u, 0.0); n1 = fpga_v3_pr_rec(&Q, &rb, e1, 1u, 0, 1.0, dt, 4u);
        CHECK(n1 == 1u && dt[0] == 400, "pr_rec A a B ruznych segmentu: %u, dt %lld", (unsigned)n1, (long long)dt[0]);
        fpga_v3_pr_switch(&Q);
        ra.seg_idx = 14u; e1[0] = evk(0u, 4000u, 1u, 0.0); (void)fpga_v3_pr_rec(&Q, &ra, e1, 1u, 0, 1.0, dt, 4u);
        rb.seg_idx = 14u; e1[0] = evk(1u, 4100u, 1u, 0.0); n1 = fpga_v3_pr_rec(&Q, &rb, e1, 1u, 0, 1.0, dt, 4u);  /* stary zdroj */
        ra.seg_idx = 15u; e1[0] = evk(0u, 5000u, 1u, 0.0); (void)fpga_v3_pr_rec(&Q, &ra, e1, 1u, 0, 1.0, dt, 4u);
        rb.seg_idx = 15u; rb.flags = FPGA_V3_R_INVALID; n2 = fpga_v3_pr_rec(&Q, &rb, e1, 0u, 0, 1.0, dt, 4u);        /* drzeni */
        rb.flags = 0u;
        ra.seg_idx = 16u; e1[0] = evk(0u, 6000u, 1u, 0.0); (void)fpga_v3_pr_rec(&Q, &ra, e1, 1u, 0, 1.0, dt, 4u);
        rb.seg_idx = 16u; e1[0] = evk(1u, 6300u, 1u, 0.0); n3 = fpga_v3_pr_rec(&Q, &rb, e1, 1u, 0, 1.0, dt, 4u);
        CHECK(n1 == 0u && n2 == 0u && n3 == 1u && dt[0] == 300, "pr_rec prepnuti zdroje B: %u %u %u, dt %lld",
              (unsigned)n1, (unsigned)n2, (unsigned)n3, (long long)dt[0]);
    }

    /* jednorazove deje (fpga_v3_ev_from_rec, fpga_v3_ab_feed, fpga_v3_skew_add) nad emulatorem se zpozdenym ACK: zaznamy jako
     * v ovladaci (fpga_v3_pr_rec: udalosti teze hranice segmentu, A pak B, se paruji spolu). Scenare: (a) 7 Hz, B = A +
     * 1,234 ms (jednotlive hrany, i pres hranici segmentu); (b) 1 kHz, B = A + 123,456 us (souhrny N = 25: presne jen prvni
     * hrany); (c) B = A: kalibrace zpozdeni a interval (souhrn B pres start -> zahodit); (d) A 2 Hz, 200 s bez signalu (T0 pretece), pak zase -> cas udalosti navazuje; (e) 7 Hz, B = A +
     * 140 ms (B pred dalsim A v tomtez segmentu: serazeni). Bez ztrat startu: intervalu = udalosti A. */
    {
        static const struct { double f, d_s; uint32_t nseg, silent0, silent1; int skew; const char *nm; } EV[] = {
            { 7.0, 1.234e-3, 1600u, 0u, 0u, 0, "7 Hz, B = A + 1,234 ms" },
            { 1000.0, 123.456e-6, 800u, 0u, 0u, 0, "1 kHz, B = A + 123,456 us (souhrny)" },
            { 1000.0, 0.0, 400u, 0u, 0u, 1, "1 kHz, B = A: kalibrace zpozdeni" },
            { 1000.0, 0.0, 400u, 0u, 0u, 0, "1 kHz, B = A: interval" },
            { 2.0, 5.0e-3, 9200u, 400u, 8400u, 0, "2 Hz, B = A + 5 ms, 200 s ticho" },
            { 7.0, 140.0e-3, 1600u, 0u, 0u, 0, "7 Hz, B = A + 140 ms (B pred dalsim A)" },
        };
        for (unsigned c = 0; c < sizeof EV / sizeof EV[0]; c++) {
            static fpga_v3_link_t L; static fpga_v3_evu_t U; static fpga_v3_pr_t PR; static fpga_v3_stat_t ST;
            fpga_v3_link_init(&L); fpga_v3_evu_init(&U); fpga_v3_pr_init(&PR, 2000000000000ull);   /* max 1,22 s */
            fpga_v3_stat_init(&ST);
            fpga_v3_emu_init(&E, EV[c].f, EV[c].f, 30.0, 500u + c);
            fpga_v3_emu_set_phase(&E, 1, E.t_next[0] + EV[c].d_s);
            fpga_v3_emu_frame(&E, F);
            fpga_v3_ev_t ea[2];
            uint32_t nev = 0, bad = 0, t_ms = 0; double t_prev_a = -1.0, maxgap = 0.0;
            const double up = fpga_v3_unit_ps(100u);
            for (uint32_t s = 0; s < EV[c].nseg; s += 4) {
                for (int q = 0; q < 4; q++) {
                    const uint32_t si = s + (uint32_t)q;
                    const double f = (si >= EV[c].silent0 && si < EV[c].silent1) ? 0.0 : EV[c].f;
                    if (E.f_hz[0] == 0.0 && f > 0.0) { E.t_next[0] = 0.0101; E.t_next[1] = 0.0101 + EV[c].d_s; }
                    E.f_hz[0] = f; E.f_hz[1] = f;
                    fpga_v3_emu_segment(&E);
                }
                t_ms += 100u;
                fpga_v3_hdr_t h; uint32_t n_ok = 0, i0 = 0; int g = 0;
                if (fpga_v3_parse(F, &h, R, &n_ok) != FPGA_V3_OK) { bad++; break; }
                const uint32_t n = fpga_v3_link_take(&L, &h, n_ok, &i0, &g);
                for (uint32_t i = 0; i < n; i++) {
                    const fpga_v3_rec_t *r = &R[i0 + i];
                    const uint32_t na = fpga_v3_ev_from_rec(&U, r, 100u, t_ms, ea);
                    if (r->ch == 0u) {
                        for (uint32_t k = 0; k < na; k++) {            /* cas udalosti A proti skutecnosti: navaznost */
                            const double ts = (double)ea[k].t_u * up * 1e-12;
                            if (EV[c].silent1 && r->seg_idx == (uint16_t)EV[c].silent1 && k == 0) {
                                /* prvni hrana po tichu: absolutni cas obnovy = zacatek segmentu silent1 + 10,1 ms (T0 mezitim
                                 * pretekl -- 200 s > 171,8 s; rozbaleni z casu STM) */
                                const double tex = (double)EV[c].silent1 * 0.025 + 0.0101;
                                if (fabs(ts - tex) > fabs(maxgap)) maxgap = ts - tex;
                                t_prev_a = -1.0;
                            }
                            if (t_prev_a >= 0.0) {
                                const double per = 1.0 / EV[c].f, gap = ts - t_prev_a, kk = floor(gap / per + 0.5);
                                if (fabs(gap - kk * per) > fabs(maxgap)) maxgap = gap - kk * per;
                            }
                            t_prev_a = ts; nev++;
                        }
                    }
                    int64_t dt[4];
                    const uint32_t nd = fpga_v3_pr_rec(&PR, r, ea, na, EV[c].skew, up, dt, 4u);
                    for (uint32_t k = 0; k < nd; k++) {
                        const double ps = (double)dt[k] * up;
                        fpga_v3_stat_add(&ST, ps);
                        if (fabs(ps - EV[c].d_s * 1e12) > 300.0) bad++;
                    }
                }
                fpga_v3_build_ack(M, s, L.expect);
                fpga_v3_emu_frame(&E, F);
                fpga_v3_emu_rx(&E, M);
            }
            const double mean = fpga_v3_stat_mean(&ST), sd = fpga_v3_stat_sd(&ST);
            printf("  deje %-38s udalosti A %5u, intervalu %4u (ztraceno %u), stredni %.6f us sigma %.1f ps, zpozdeni %4u x %.1f ps, chyb %u, max odchylka casu A %.1e s\n",
                   EV[c].nm, (unsigned)nev, (unsigned)ST.n, (unsigned)PR.ab.lost, mean * 1e-6, sd, (unsigned)PR.sk.n, fpga_v3_stat_mean(&PR.sk),
                   (unsigned)bad, fabs(maxgap));
            CHECK(bad == 0 && fabs(maxgap) < 1e-9, "%s: chyb %u, odchylka casu %.2e s", EV[c].nm, (unsigned)bad, maxgap);
            if (EV[c].skew) {
                CHECK(PR.sk.n >= 100u && fabs(fpga_v3_stat_mean(&PR.sk)) < 10.0 && ST.n == 0u, "%s: zpozdeni %u x %.1f ps",
                      EV[c].nm, (unsigned)PR.sk.n, fpga_v3_stat_mean(&PR.sk));
            } else if (EV[c].d_s == 0.0) {
                CHECK(ST.n >= 100u, "%s: intervalu %u", EV[c].nm, (unsigned)ST.n);
            } else {
                CHECK(ST.n >= (c == 1 ? 25u : 8u) && fabs(mean - EV[c].d_s * 1e12) < 30.0 && sd < 80.0,
                      "%s: intervalu %u, stredni %.1f ps (ceka %.1f), sigma %.1f", EV[c].nm, (unsigned)ST.n, mean, EV[c].d_s * 1e12, sd);
                CHECK(ST.n + 2u >= nev && PR.ab.lost == 0u, "%s: intervalu %u z %u startu, ztraceno %u", EV[c].nm, (unsigned)ST.n,
                      (unsigned)nev, (unsigned)PR.ab.lost);
            }
        }
    }

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
