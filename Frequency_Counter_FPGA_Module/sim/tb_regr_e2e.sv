// ============================================================
// tb_regr_e2e.sv -- KONCOVY test regresniho bloku (FW 0x0411): tdc_chan (model krzemiku) + win_recip(REGR=1)
// na signalu, jehoz faze vuci hodinam TDC "prohazuje" (TSIG mod 10 ns != 0). Pro kazde okno porovna
//   f_2pt  = periody / dt            (dva krajni body, dosavadni odhad)
//   f_reg  = (x_B - x_A) / (y_B - y_A)  (sklon mezi stredy segmentu A a B)
// proti ZNAME pravde f = 1/TSIG. Hradlo je zkracene (60 us), segmenty 1/8 hradla (jako top.v: A = [G/16,3G/16),
// B = [13G/16,15G/16)). Zisk regrese roste s poctem znacek v segmentu (zde ~30 -> ocekavano ~3x).
// PASS: vsechna okna maji ok_A & ok_B, sigma_reg < sigma_2pt / 1.8, |stred_reg| < 3*sigma_reg/sqrt(n).
// ============================================================
`timescale 1ps/1ps
`ifndef CAL_LOG2_TB
`define CAL_LOG2_TB 14
`endif
`ifndef PTAPS
`define PTAPS 320
`endif
`ifndef PSTRIDE
`define PSTRIDE 1
`endif
`ifndef PKD
`define PKD 1
`endif
`ifndef TSIG_PS
`define TSIG_PS 97123
`endif
`ifndef NWIN
`define NWIN 36
`endif
module tb_regr_e2e;
    localparam CAL_LOG2 = `CAL_LOG2_TB;
    localparam TSIG = `TSIG_PS;
    localparam GP = 6000;                          // hradlo [takty 100 MHz] = 60 us; G/16 musi byt > ~300 taktu (deleni v regr_acc)

    reg clk = 0; always #5000 clk = ~clk;
    reg [47:0] tick_ps = 48'd0;
    always @(posedge clk) tick_ps <= tick_ps + 48'd16384;

    // ---- hradlo + segmenty (jako top.v, ale v taktech 100 MHz) ----
    reg [12:0] gph = 0; reg gate_tick = 0, run = 0;
    reg seg_a = 0, seg_b = 0;
    always @(posedge clk) begin
        gate_tick <= 1'b0;
        if (run) begin
            if (gph == GP - 1) begin gph <= 0; gate_tick <= 1'b1; end else gph <= gph + 1;
            seg_a <= (gph >= GP / 16)      && (gph < 3 * GP / 16);
            seg_b <= (gph >= 13 * GP / 16) && (gph < 15 * GP / 16);
        end
    end

    reg  sig = 0;
    wire ro, use_ro;
    ring_osc #(.DIV_LOG2(2)) u_ro (.en(use_ro), .out(ro));
    reg [27:0] tmo = 0; reg abort = 0;
    always @(posedge clk) begin abort <= 0; if (use_ro) begin tmo <= tmo + 1; if (&tmo) abort <= 1; end else tmo <= 0; end

    reg cal_req = 0;
    wire rise_c, rise_s, trig, tsv, want, want_seg, busy, valid, fail;
    wire [47:0] ts;
    wire [31:0] ovf, peak; wire [15:0] nz, last, maxtap;
    tdc_chan #(.CAL_LOG2(CAL_LOG2), .TAPS(`PTAPS), .STRIDE(`PSTRIDE), .KD(`PKD), .DUAL(0)) ca (
        .clk(clk), .sig_raw(sig), .ro(ro), .tick_ps(tick_ps), .want(want), .want_seg(want_seg),
        .cal_req(cal_req), .cal_abort(abort), .rise_c(rise_c), .rise_s(rise_s), .trig_ack(trig),
        .ts_valid(tsv), .ts_ps(ts), .use_ro(use_ro), .cal_busy(busy), .cal_valid(valid), .cal_fail(fail),
        .d_ovf(ovf), .d_peak(peak), .d_nz(nz), .d_last(last), .d_maxtap(maxtap), .dump_k(10'd0), .dump_q());

    wire [25:0] per; wire [47:0] dt; wire al, tgl;
    wire [23:0] rg_n_a, rg_n_b; wire [39:0] rg_xm_a, rg_xm_b; wire [47:0] rg_ym_a, rg_ym_b; wire rg_ok_a, rg_ok_b;
    win_recip #(.REGR(1)) wr (.clk(clk), .rise_s(rise_s), .trig_ack(trig), .ts_valid(tsv), .ts_ps(ts),
        .gate_tick(gate_tick), .hold(busy | ~valid), .seg_a(seg_a), .seg_b(seg_b), .want(want), .want_seg(want_seg),
        .r_periods(per), .r_dt(dt), .r_dt_alias(al), .res_tgl(tgl),
        .rg_n_a(rg_n_a), .rg_n_b(rg_n_b), .rg_xm_a(rg_xm_a), .rg_xm_b(rg_xm_b),
        .rg_ym_a(rg_ym_a), .rg_ym_b(rg_ym_b), .rg_ok_a(rg_ok_a), .rg_ok_b(rg_ok_b));

    initial begin : gen
        #123;
        forever begin sig = 1; #(TSIG/2); sig = 0; #(TSIG - TSIG/2); end
    end
    initial begin #(64'd600_000_000_000); $display("FAIL: timeout (valid=%0d)", valid); $finish; end

    // ---- sber: pri kazdem nove uzavrenem okne (res_tgl) se zachyti i regresni blok ----
    real f_true;
    real e2 [0:`NWIN-1]; real er [0:`NWIN-1];
    integer nw = 0, nok = 0, nsk = 0;
    reg tg_p = 0;
    real f2, fr, dxm, dym, s2, sr, m2, mr;
    integer i, started = 0;
    always @(posedge clk) begin
        tg_p <= tgl;
        if (tgl !== tg_p && started) begin
            // regresni vystupy platne pro PRAVE uzavrene okno (segment B skoncil pred jeho koncem)
            f2 = ($itor(per) / ($itor(dt) * 625.0 / 1024.0)) * 1.0e12;
            if (rg_ok_a && rg_ok_b && rg_n_a >= 2 && rg_n_b >= 2) begin
                dxm = ($itor(rg_xm_b) - $itor(rg_xm_a)) / 256.0;
                dym = ($itor(rg_ym_b) - $itor(rg_ym_a)) / 16.0 * 625.0 / 1024.0;      // ps
                fr  = dxm / dym * 1.0e12;
                if (nw < `NWIN) begin e2[nw] = f2 / f_true - 1.0; er[nw] = fr / f_true - 1.0; nw = nw + 1; end
                if (nw <= 4) $display("  okno %0d: n_A=%0d n_B=%0d  2pt %0.1f ppb  regr %0.1f ppb", nw, rg_n_a, rg_n_b,
                                      (f2 / f_true - 1.0) * 1e9, (fr / f_true - 1.0) * 1e9);
            end else nsk = nsk + 1;
        end
    end

    initial begin
        f_true = 1.0e12 / TSIG;
        #200000;
        @(posedge clk); cal_req <= 1; @(posedge clk); cal_req <= 0;
        wait ((valid && !busy) || fail);
        $display("CAL: valid=%0d fail=%0d nz=%0d last=%0d (t=%0t ps)", valid, fail, nz, last, $time);
        repeat (50) @(posedge clk);
        run = 1; repeat (2 * GP) @(posedge clk); started = 1;       // 2 okna na rozjezd (prvni uzavre jen start)
        wait (nw >= `NWIN);
        s2 = 0; sr = 0; m2 = 0; mr = 0;
        for (i = 0; i < `NWIN; i = i + 1) begin m2 = m2 + e2[i]; mr = mr + er[i]; end
        m2 = m2 / `NWIN; mr = mr / `NWIN;
        for (i = 0; i < `NWIN; i = i + 1) begin s2 = s2 + (e2[i] - m2) * (e2[i] - m2); sr = sr + (er[i] - mr) * (er[i] - mr); end
        s2 = $sqrt(s2 / (`NWIN - 1)); sr = $sqrt(sr / (`NWIN - 1));
        $display("\nSHRNUTI %0d oken (preskoceno bez regr. dat: %0d), f_true = %0.3f Hz, f ~ %0.2f MHz", `NWIN, nsk, f_true, f_true / 1e6);
        $display("  2 body : stred %9.2f ppb, sigma %9.2f ppb", m2 * 1e9, s2 * 1e9);
        $display("  regrese: stred %9.2f ppb, sigma %9.2f ppb  -> ZISK sigma: %0.2fx", mr * 1e9, sr * 1e9, s2 / sr);
        if (s2 / sr < 1.8) begin $display("FAIL: zisk regrese < 1.8x"); $finish; end
        if ($abs(mr) > 3.0 * sr / $sqrt(`NWIN * 1.0)) begin $display("FAIL: regrese ma posun stredu (%0.2f ppb)", mr * 1e9); $finish; end
        $display("PASS: tb_regr_e2e");
        $finish;
    end
endmodule
