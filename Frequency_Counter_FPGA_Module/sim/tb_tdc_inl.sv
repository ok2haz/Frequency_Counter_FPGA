// ============================================================
// tb_tdc_inl.sv -- jeden kanal TDC: histogram kalibrace + CHYBA CASOVE ZNACKY KAZDE HRANY jako funkce faze.
// Na desce bez rizeni reference tohle neumime zmerit (faze musi projet vsechny kody); v simulaci
// je skutecny cas hrany znamy, takze chyba = ts - t_hrany je primo INL/DNL i skoky.
//
// Modely krzemiku (defs): -DSIM_IDX -DSIM_TAP_MEAN=39 -DSIM_TAP_SPREAD=15 -DSIM_BREAK_IDX=268 -DSIM_T0_PS=1870
//   (A: min. kod 24, obri bin 1435 ps, maxtap 133; B: -DSIM_T0_PS=5300)
// Vystup: histogram (kody, sirky binu), tabulka chyb po 500 ps faze, pocet skoku, sigma.
// Vyhodnoceni PASS/FAIL: -DEXPECT_SIGMA_PS=<limit>, -DEXPECT_MAXJUMP=<max skoku ve %>
// ============================================================
`timescale 1ps/1ps
`ifndef CAL_LOG2_TB
`define CAL_LOG2_TB 16
`endif
`ifndef NEDGES
`define NEDGES 3000
`endif
`ifndef PTAPS
`define PTAPS 256
`endif
`ifndef PSTRIDE
`define PSTRIDE 2
`endif
`ifndef PDUAL
`define PDUAL 0
`endif
`ifndef PKD
`define PKD 2
`endif
module tb_tdc_inl;
    localparam CAL_LOG2 = `CAL_LOG2_TB;
    localparam CW = $clog2(`PTAPS);
    localparam AW = CW + `PDUAL;
    localparam NC = 32'd1 << AW;
    localparam TSIG     = 97123;                 // perioda signalu [ps]; posun faze na hranu 7123 ps (pokryti)

    reg clk = 0;
    always #5000 clk = ~clk;                     // 100 MHz
    reg [47:0] tick_ps = 48'd0;
    always @(posedge clk) tick_ps <= tick_ps + 48'd16384;

    reg  sig = 0;
    wire ro, use_ro;
    ring_osc #(.DIV_LOG2(2)) u_ro (.en(use_ro), .out(ro));
    reg [27:0] tmo = 0; reg abort = 0;
    always @(posedge clk) begin
        abort <= 0;
        if (use_ro) begin tmo <= tmo + 1; if (&tmo) abort <= 1; end else tmo <= 0;
    end

    reg cal_req = 0, want = 0;
    wire rise_c, rise_s, trig, tsv, busy, valid, fail;
    wire [47:0] ts;
    wire [31:0] ovf, peak; wire [15:0] nz, last, maxtap;
    reg  [9:0] dump_k = 0; wire [23:0] dump_q;
    tdc_chan #(.CAL_LOG2(CAL_LOG2), .TAPS(`PTAPS), .STRIDE(`PSTRIDE), .KD(`PKD), .DUAL(`PDUAL)) ca (
        .clk(clk), .sig_raw(sig), .ro(ro), .tick_ps(tick_ps), .want(want), .want_seg(1'b0),
        .cal_req(cal_req), .cal_abort(abort), .rise_c(rise_c), .rise_s(rise_s), .trig_ack(trig),
        .ts_valid(tsv), .ts_ps(ts), .use_ro(use_ro), .cal_busy(busy),
        .cal_valid(valid), .cal_fail(fail),
        .d_ovf(ovf), .d_peak(peak), .d_nz(nz), .d_last(last), .d_maxtap(maxtap),
        .dump_k(dump_k), .dump_q(dump_q));

    // ---- skutecne casy hran (znama) ----
    localparam T0 = 123;                         // prvni nabezna hrana [ps]
    // ---- sber chyb ----
    integer nrec = 0;
    real    terr [0:`NEDGES-1];
    real    tph  [0:`NEDGES-1];
    integer ei = 0;                              // index hrany prirazene dalsimu ts_valid
    real    t_edge;

    // hrana k nastane v T0 + k*TSIG; ts_valid k-te hrany po zapnuti `want` = k-ta hrana PO zapnuti
    integer k_first = 0;
    always @(posedge clk) if (tsv && nrec < `NEDGES) begin
        t_edge = T0 + (k_first + nrec) * 1.0 * TSIG;
        terr[nrec] = $itor(ts) * 625.0 / 1024.0 - t_edge;
        tph[nrec]  = t_edge - 10000.0 * $floor(t_edge / 10000.0);
        nrec = nrec + 1;
    end

    initial begin : gen
        #(T0);
        forever begin sig = 1; #(TSIG/2); sig = 0; #(TSIG - TSIG/2); end
    end

    initial begin
        #(64'd400_000_000_000); $display("FAIL: timeout"); $finish;
    end

    // ---- hlavni scenar ----
    integer i, k, nb, nj, nbin = 20;
    real tot, w, mu, m2, s_all, mean_all, med;
    real bsum [0:19]; real bsq [0:19]; integer bn [0:19]; integer bj [0:19];
    integer h [0:1023]; integer hsum, firstk, lastk, nonz, maxk, half, n0, n1;
    real cum;
    initial begin
        #200000;
        @(posedge clk); cal_req <= 1; @(posedge clk); cal_req <= 0;
        wait (valid || fail);
        $display("CAL: valid=%0d fail=%0d ovf=%0d peak=%0d nz=%0d last=%0d maxtap=%0d (t=%0t ps)", valid, fail, ovf, peak, nz, last, maxtap, $time);
        // ---- histogram ----
        hsum = 0;
        for (k = 0; k < NC; k = k + 1) begin
            dump_k = k[9:0]; repeat (6) @(posedge clk); h[k] = dump_q; hsum = hsum + h[k];
        end
        firstk = -1; lastk = 0; nonz = 0; maxk = 0;
        for (k = 0; k < NC; k = k + 1) if (h[k] != 0) begin
            if (firstk < 0) firstk = k; lastk = k; nonz = nonz + 1; if (h[k] > h[maxk]) maxk = k;
        end
        $display("HIST: N=%0d, adresy %0d..%0d, obsazenych %0d, nejvetsi bin a=%0d = %0.0f ps (%0.1f %% periody)",
                 hsum, firstk, lastk, nonz, maxk, 10000.0 * h[maxk] / hsum, 100.0 * h[maxk] / hsum);
        if (`PDUAL != 0) begin
            n0 = 0; n1 = 0;
            for (k = 0; k < NC / 2; k = k + 1) begin n0 = n0 + h[k]; n1 = n1 + h[k + NC / 2]; end
            $display("HIST: sada R %0.1f %% udalosti (w_lo = %0.0f ps), sada F %0.1f %% (w_hi = %0.0f ps)",
                     100.0 * n0 / hsum, 10000.0 * n0 / hsum, 100.0 * n1 / hsum, 10000.0 * n1 / hsum);
        end
        // sigma kvantizace 1 hrany z histogramu (jako tools/tdc_hist_analysis.py)
        tot = 0.0;
        for (k = 0; k < NC; k = k + 1) begin w = 10000.0 * h[k] / hsum; tot = tot + (w / 10000.0) * w * w / 12.0; end
        $display("HIST: sigma kvantizace 1 hrany = %0.0f ps (okno = x sqrt2 = %0.0f ps)", $sqrt(tot), $sqrt(2.0 * tot));
        // ---- mereni: want=1 -> kazda hrana dostane presny cas ----
        k_first = 0;
        @(posedge clk); want <= 1;
        // hrana, ktera spusti prvni ts, neni znama dopredu -> zjisti z prvniho ts (viz nize); pocka na NEDGES znacek
        wait (nrec >= `NEDGES);
        want <= 0;
        // urceni konstantniho posunu: ts - t_hrany ma byt konstantni; posun = median chyb, ale hrana k_first muze
        // byt zamenena o cele periody -> sjednot modulo TSIG
        for (i = 0; i < nbin; i = i + 1) begin bsum[i] = 0; bsq[i] = 0; bn[i] = 0; bj[i] = 0; end
        // cyklus 1: odhad ofsetu = median chyb po odecteni cele periody TSIG (korekce k_first)
        mean_all = 0;
        for (i = 0; i < `NEDGES; i = i + 1) mean_all = mean_all + terr[i];
        mean_all = mean_all / `NEDGES;
        // oprava o cele periody: nejblizsi nasobek TSIG
        mean_all = mean_all - TSIG * $floor(mean_all / TSIG + 0.5);
        // ofset taktu (cyklus m+6 ...) je konstantni, ale neznamy: ber median chyb po korekci
        nj = 0; s_all = 0; nb = 0;
        // median pres jednoduche razeni vyberem (3000 prvku staci)
        begin : med_blk
            real tmp [0:`NEDGES-1]; real sw; integer a, b;
            for (a = 0; a < `NEDGES; a = a + 1) begin
                tmp[a] = terr[a] - TSIG * $floor((terr[a] - mean_all) / TSIG + 0.5);
            end
            for (a = 0; a < `NEDGES; a = a + 1) for (b = a + 1; b < `NEDGES; b = b + 1)
                if (tmp[b] < tmp[a]) begin sw = tmp[a]; tmp[a] = tmp[b]; tmp[b] = sw; end
            med = tmp[`NEDGES / 2];
        end
        for (i = 0; i < `NEDGES; i = i + 1) begin
            w = terr[i] - TSIG * $floor((terr[i] - mean_all) / TSIG + 0.5) - med;    // chyba znacky vuci medianu
            nb = $rtoi(tph[i] / (10000.0 / nbin)); if (nb > nbin - 1) nb = nbin - 1;
            if (w > 2000.0 || w < -2000.0) begin nj = nj + 1; bj[nb] = bj[nb] + 1; end
            else begin bsum[nb] = bsum[nb] + w; bsq[nb] = bsq[nb] + w * w; bn[nb] = bn[nb] + 1; s_all = s_all + w * w; end
        end
        $display("\nINL: faze [ps] | n | stred [ps] | sigma [ps] | skoky>2ns");
        for (i = 0; i < nbin; i = i + 1) begin
            if (bn[i] > 0) begin
                mu = bsum[i] / bn[i]; m2 = bsq[i] / bn[i] - mu * mu;
                $display("%5d-%5d %4d %8.1f %8.1f %4d", i * 10000 / nbin, (i + 1) * 10000 / nbin, bn[i], mu, (m2 > 0) ? $sqrt(m2) : 0.0, bj[i]);
            end else $display("%5d-%5d %4d      ---      --- %4d", i * 10000 / nbin, (i + 1) * 10000 / nbin, 0, bj[i]);
        end
        $display("\nSHRNUTI: hran %0d, skoky >2 ns: %0d (%0.1f %%), sigma mimo skoky %0.1f ps", `NEDGES, nj, 100.0 * nj / `NEDGES,
                 $sqrt(s_all / (`NEDGES - nj)));
`ifdef EXPECT_SIGMA_PS
        if ($sqrt(s_all / (`NEDGES - nj)) > `EXPECT_SIGMA_PS) begin $display("FAIL: sigma > %0d ps", `EXPECT_SIGMA_PS); $finish; end
`endif
`ifdef EXPECT_MAXJUMP
        if (100.0 * nj / `NEDGES > `EXPECT_MAXJUMP) begin $display("FAIL: skoku vic nez %0d %%", `EXPECT_MAXJUMP); $finish; end
`endif
        $display("PASS: tb_tdc_inl");
        $finish;
    end
endmodule
