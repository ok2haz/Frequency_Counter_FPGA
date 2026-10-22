// tb_regr_full.sv -- regr_acc v PLNE velikosti (segment 31,25 ms = 312 500 znacek po 100 ns, cas 163 840 jednotek/znacka):
// cvici horni bity rozdilu casu dt_r[39:32] a prenos sy_lo -> sy_hi, ktere maly tb_regr_acc nedosahne.
// Znacka kazdych 10 taktu (10 MHz signal). Porovnava n, xm, ym s referenci (longint).
`timescale 1ps/1ps
`include "tb_clk.vh"
module tb_regr_full;
    reg clk = 0; always #(`TB_THALF_PS) clk = ~clk;
    reg seg_a = 0, seg_b = 0, samp_v = 0, samp_b = 0;
    reg [47:0] ref_ts = 0, samp_ts = 0; reg [25:0] samp_x = 0;
    wire [23:0] n_a, n_b; wire [25:0] x0_a, x0_b; wire [41:0] sx_a, sx_b; wire [47:0] y0_a, y0_b; wire [55:0] sy_a, sy_b; wire ok_a, ok_b;   // FW 0x0426: surove soucty
    regr_acc dut (.clk(clk), .seg_a(seg_a), .seg_b(seg_b), .ref_ts(ref_ts), .samp_v(samp_v), .samp_b(samp_b),
        .samp_x(samp_x), .samp_ts(samp_ts), .n_a(n_a), .n_b(n_b), .x0_a(x0_a), .x0_b(x0_b), .sx_a(sx_a), .sx_b(sx_b), .y0_a(y0_a), .y0_b(y0_b), .sy_a(sy_a), .sy_b(sy_b),
        .ok_a(ok_a), .ok_b(ok_b));
    longint unsigned x0, t0, sx, sy, nn, x, ts, xg, yg;
    integer j, errs = 0, NS;
    task run_seg(input bit isb, input longint unsigned xstart, input longint unsigned tstart);
        begin
            sx = 0; sy = 0; nn = 0;
            samp_b = isb;                                            // jako v desce: zname pred znackou (act je registrovane)
            @(negedge clk); if (isb) seg_b = 1; else seg_a = 1;
            repeat (3) @(negedge clk);
            for (j = 0; j < NS; j = j + 1) begin
                x  = xstart + j;
                ts = tstart + j * 163840 + ((j * 7919) % 64) - 32;    // perioda 100 ns + maly "sum"
                if (j == 0) begin x0 = x; t0 = ts; end
                nn = nn + 1;
                if (j > 0) begin sx = sx + (x - x0); sy = sy + (ts - t0); end
                samp_x = x[25:0]; samp_ts = ts[47:0]; samp_b = isb; samp_v = 1;
                @(negedge clk); samp_v = 0; repeat (9) @(negedge clk);
            end
            if (isb) seg_b = 0; else seg_a = 0;
            repeat (700) @(negedge clk);
            xg = sx; yg = (t0 - ref_ts);   // FW 0x0426: delic odstranen -> porovnavaji se SUROVE soucty (deli STM)
            if (isb) begin
                $display("B: n=%0d/%0d  x0=%0d/%0d  sx=%0d/%0d  y0=%0d/%0d  sy3=%0d/%0d", n_b, nn, x0_b, x0, sx_b, sx, y0_b, yg, sy_b, sy >> 3);
                if (!ok_b || n_b !== nn[23:0] || x0_b !== x0[25:0] || sx_b !== sx[41:0] || y0_b !== yg[47:0] || sy_b !== sy[58:3]) errs = errs + 1;
            end else begin
                $display("A: n=%0d/%0d  x0=%0d/%0d  sx=%0d/%0d  y0=%0d/%0d  sy3=%0d/%0d", n_a, nn, x0_a, x0, sx_a, sx, y0_a, yg, sy_a, sy >> 3);
                if (!ok_a || n_a !== nn[23:0] || x0_a !== x0[25:0] || sx_a !== sx[41:0] || y0_a !== yg[47:0] || sy_a !== sy[58:3]) errs = errs + 1;
            end
        end
    endtask
    initial begin
`ifdef NSEG
        NS = `NSEG;
`else
        NS = 312500;
`endif
        ref_ts = 48'd123456789012;
        repeat (5) @(posedge clk);
        run_seg(0, 156251, ref_ts + 48'd25600000000);     // A: od G/16
        run_seg(1, 2031251, ref_ts + 48'd332800000000);   // B: od 13G/16
        if (errs == 0) $display("PASS: tb_regr_full"); else $display("FAIL: tb_regr_full (%0d)", errs);
        $finish;
    end
endmodule
