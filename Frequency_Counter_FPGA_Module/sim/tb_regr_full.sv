// tb_regr_full.sv -- regr_acc v PLNE velikosti (segment 31,25 ms = 312 500 znacek po 100 ns, cas 163 840 jednotek/znacka):
// cvici horni bity rozdilu casu dt_r[39:32] a prenos sy_lo -> sy_hi, ktere maly tb_regr_acc nedosahne.
// Znacka kazdych 10 taktu (10 MHz signal). Porovnava n, xm, ym s referenci (longint).
`timescale 1ps/1ps
module tb_regr_full;
    reg clk = 0; always #5000 clk = ~clk;
    reg seg_a = 0, seg_b = 0, samp_v = 0, samp_b = 0;
    reg [47:0] ref_ts = 0, samp_ts = 0; reg [25:0] samp_x = 0;
    wire [23:0] n_a, n_b; wire [39:0] xm_a, xm_b; wire [47:0] ym_a, ym_b; wire ok_a, ok_b;
    regr_acc dut (.clk(clk), .seg_a(seg_a), .seg_b(seg_b), .ref_ts(ref_ts), .samp_v(samp_v), .samp_b(samp_b),
        .samp_x(samp_x), .samp_ts(samp_ts), .n_a(n_a), .n_b(n_b), .xm_a(xm_a), .xm_b(xm_b), .ym_a(ym_a), .ym_b(ym_b),
        .ok_a(ok_a), .ok_b(ok_b));
    longint unsigned x0, t0, sx, sy, nn, x, ts, xg, yg;
    integer j, errs = 0, NS;
    task run_seg(input bit isb, input longint unsigned xstart, input longint unsigned tstart);
        begin
            sx = 0; sy = 0; nn = 0;
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
            xg = (x0 << 8) + ((sx << 8) / nn);
            yg = ((t0 - ref_ts) << 4) + ((sy << 4) / nn);
            if (isb) begin
                $display("B: n=%0d/%0d  xm=%0d/%0d  ym=%0d/%0d", n_b, nn, xm_b, xg, ym_b, yg);
                if (!ok_b || n_b !== nn[23:0] || xm_b !== xg[39:0] || ym_b !== yg[47:0]) errs = errs + 1;
            end else begin
                $display("A: n=%0d/%0d  xm=%0d/%0d  ym=%0d/%0d", n_a, nn, xm_a, xg, ym_a, yg);
                if (!ok_a || n_a !== nn[23:0] || xm_a !== xg[39:0] || ym_a !== yg[47:0]) errs = errs + 1;
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
