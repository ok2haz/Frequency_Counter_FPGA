// ============================================================
// tb_win_regr_full.sv -- win_recip(REGR=1) + regr_acc v PLNE velikosti hradla 0,25 s (25 000 000 taktu),
// segment A = [G/16, 3G/16) jako top.v -> 312 500 znacek s indexy 156 250..468 750 (pres hranice 2^16..2^18).
// Proc (2026-10-21, `phrec dump` z desky): xm_a nesedi na index podle ym -- soucet indexu o PRESNE 2^16 vedle
// v 89 % oken; tb_regr_full krmi regr_acc primo a cestu indexu sg_x ve win_recip vubec necvici.
// Model TDC = casovani tdc_chan: trig_ack v taktu hrany (ec), rise_s o takt pozdeji, ts_valid TS_LAT taktu po
// trig_ack (ts_ps drzeny do dalsi znacky), zmrazeni 5 taktu (pri 10 MHz se neuplatni).
// Kontrola: xm_a/4096 == index podle ym_a (souvisla rada znacek -> frac 0 nebo 0,5) a n_a v ocekavanem rozsahu.
// PASS = vsechna okna bez rozdilu. Jen segment A (B by stal dalsich ~19 M taktu).
// ============================================================
`timescale 1ns/1ps
module tb_win_regr_full;
`ifdef GATE_CLK
    localparam integer G = `GATE_CLK;
`else
    localparam integer G = 25000000;       // 0,25 s pri 100 MHz
`endif
`ifdef NWIN
    localparam integer NW = `NWIN;
`else
    localparam integer NW = 1;
`endif
`ifdef EDGE_PH
    localparam integer PH = `EDGE_PH;      // takt hrany v ramci periody 10 taktu
`else
    localparam integer PH = 3;
`endif
    localparam integer TS_LAT = 8;
    localparam integer UNITS  = 16384;     // jednotek T_clk/16384 na takt

    reg clk = 0; always #5 clk = ~clk;
    reg [63:0] cyc = 0; always @(posedge clk) cyc <= cyc + 1;

    // ---- hradlo + segmenty (jako top.v, v taktech) ----
    reg [31:0] gph = G - 200; reg gate_tick = 0, run = 0, seg_a = 0, seg_b = 0;   // prvni gate_tick hned po startu
    always @(posedge clk) begin
        gate_tick <= 1'b0;
        if (run) begin
            if (gph == G - 1) begin gph <= 0; gate_tick <= 1'b1; end else gph <= gph + 1;
            seg_a <= (gph >= G / 16) && (gph < 3 * G / 16);
            seg_b <= (gph >= 13 * G / 16) && (gph < 15 * G / 16);
        end
    end

    // ---- signal 10 MHz synchronni s hodinami ----
    reg sig = 0;
    always @(posedge clk) sig <= ((cyc % 10) >= PH) && ((cyc % 10) < PH + 5);
    wire ec = ((cyc % 10) == PH);          // takt detekce hrany (jako tdc_chan.ec)

    // ---- model tdc_chan ----
    wire want, want_seg;
    reg  arm_r = 0, idle_r = 1; reg [2:0] fz = 0;
    always @(posedge clk) arm_r <= want | want_seg;
    wire trig_ok = ec & arm_r & idle_r;
    always @(posedge clk) begin
        if (trig_ok) begin fz <= 1; idle_r <= 0; end
        else if (!idle_r) begin if (fz == 4) begin fz <= 0; idle_r <= 1; end else fz <= fz + 1; end
    end
    reg rise_s = 0; always @(posedge clk) rise_s <= ec;
    reg [TS_LAT-1:0] tv_sh = 0; reg [47:0] ts_sh [0:TS_LAT-1];
    reg ts_valid = 0; reg [47:0] ts_ps = 0;
    integer k;
    always @(posedge clk) begin
        tv_sh <= {tv_sh[TS_LAT-2:0], trig_ok};
        ts_sh[0] <= cyc[47:0] * UNITS + 48'd5000;
        for (k = 1; k < TS_LAT; k = k + 1) ts_sh[k] <= ts_sh[k-1];
        ts_valid <= tv_sh[TS_LAT-2];
        if (tv_sh[TS_LAT-2]) ts_ps <= ts_sh[TS_LAT-2];
    end

    wire [25:0] per; wire [47:0] dt; wire al, tgl; wire [8:0] ce, cs; wire [7:0] ccd, rbub; wire [3:0] rovl;
    wire [23:0] n_a, n_b; wire ok_a, ok_b;
    // FW 0x0426: surove soucty; xm/ym dopocita testbench stejne jako STM
    wire [25:0] x0_a, x0_b; wire [41:0] sx_a, sx_b; wire [47:0] y0_a, y0_b; wire [55:0] sy_a, sy_b;
    wire [63:0] xm_a = ({38'd0, x0_a} << 12) + (({22'd0, sx_a} << 12) / (n_a ? n_a : 24'd1));
    wire [63:0] ym_a = ({16'd0, y0_a} << 4) + (({8'd0, sy_a} << 7) / (n_a ? n_a : 24'd1));
    win_recip #(.REGR(1)) dut (.clk(clk), .sig_pin(sig), .rise_s(rise_s), .trig_ack(trig_ok), .ts_valid(ts_valid), .ts_ps(ts_ps),
        .gate_tick(gate_tick), .code_i(9'd0), .hold(~run), .seg_a(seg_a), .seg_b(seg_b),
        .alias_lim(8'd250), .bub(1'b0), .cs_act(1'b0), .mask_cs(1'b1), .r_bub(rbub), .r_ovl(rovl),
        .want(want), .want_seg(want_seg), .r_periods(per), .r_dt(dt), .r_dt_alias(al), .res_tgl(tgl),
        .r_ce(ce), .r_cs(cs), .r_ccd(ccd),
        .rg_n_a(n_a), .rg_n_b(n_b), .rg_x0_a(x0_a), .rg_x0_b(x0_b), .rg_sx_a(sx_a), .rg_sx_b(sx_b),
        .rg_y0_a(y0_a), .rg_y0_b(y0_b), .rg_sy_a(sy_a), .rg_sy_b(sy_b),
        .rg_ok_a(ok_a), .rg_ok_b(ok_b));

    // ---- kontrola po kazdem dokonceni segmentu A ----
    reg ok_p = 0; integer nwin = 0, errs = 0;
    real ix, iy, d;
    always @(posedge clk) begin
        ok_p <= ok_a;
        if (ok_a && !ok_p) begin
            ix = real'(xm_a) / 4096.0;                         // real'() -- $itor bere jen 32 bitu (ym_a ma 48)
            iy = real'(ym_a) / 16.0 / (10.0 * UNITS);          // index podle casu (perioda 10 taktu)
            d  = ix - iy;
            $display("okno %0d: n_a=%0d  xm/4096=%0.4f  index z ym=%0.4f  rozdil %0.4f hrany = chyba souctu %0.0f", nwin, n_a, ix, iy, d, d * n_a);
            if (d > 0.002 || d < -0.002) errs = errs + 1;
            // souvisla rada znacek -> sum(x - x0) = n(n-1)/2 presne (na desce overeno DIAG_SX buildem 2026-10-21)
            $display("  sum(x-x0) = %0d, ocekavano n(n-1)/2 = %0d", sx_a, (longint'(n_a) * (longint'(n_a) - 1)) / 2);
            if (longint'(sx_a) != (longint'(n_a) * (longint'(n_a) - 1)) / 2) errs = errs + 1;
            nwin = nwin + 1;
            if (nwin >= NW) begin
                if (errs == 0) $display("PASS: tb_win_regr_full"); else $display("FAIL: tb_win_regr_full (%0d oken)", errs);
                $finish;
            end
        end
    end
    initial begin
        repeat (20) @(posedge clk);
        run = 1;
    end
endmodule
