// tb_regr_acc.sv -- regr_acc proti referencnimu vypoctu (SV longint, celociselne deleni)
`timescale 1ps/1ps
`include "tb_clk.vh"
module tb_regr_big;
    reg clk = 0; always #(`TB_THALF_PS) clk = ~clk;
    reg seg_a = 0, seg_b = 0, samp_v = 0, samp_b = 0;
    reg [47:0] ref_ts = 0, samp_ts = 0; reg [25:0] samp_x = 0;
    wire [23:0] n_a, n_b; wire [25:0] x0_a, x0_b; wire [41:0] sx_a, sx_b; wire [47:0] y0_a, y0_b; wire [55:0] sy_a, sy_b; wire ok_a, ok_b;   // FW 0x0426: surove soucty
    regr_acc dut (.clk(clk), .seg_a(seg_a), .seg_b(seg_b), .ref_ts(ref_ts), .samp_v(samp_v), .samp_b(samp_b),
        .samp_x(samp_x), .samp_ts(samp_ts), .n_a(n_a), .n_b(n_b), .x0_a(x0_a), .x0_b(x0_b), .sx_a(sx_a), .sx_b(sx_b), .y0_a(y0_a), .y0_b(y0_b), .sy_a(sy_a), .sy_b(sy_b), .ok_a(ok_a), .ok_b(ok_b));

    integer errs = 0, trial, j, nsamp;
    longint unsigned x0, t0, sx, sy, xg, yg, x, ts, nn;
    reg [25:0] xr; reg [47:0] tr;
    task run_seg(input bit isb, input integer ns, input bit tight);   // tight: posledni znacka je 1 takt pred koncem segmentu
        begin
            x0 = 0; t0 = 0; sx = 0; sy = 0; nn = 0; x = 50 + ($urandom % 100000);
            samp_b = isb;                                            // jako v desce: zname pred znackou (act je registrovane)
            @(negedge clk); if (isb) seg_b = 1; else seg_a = 1;     // vstupy se meni na sestupne hrane (zadny zavod s DUT)
            repeat (3) @(negedge clk);
            for (j = 0; j < ns; j = j + 1) begin
                x  = x + 1;                        // neekvidistantni indexy (vynechane hrany)
                ts = ref_ts + x * 204800 + ($urandom % 400) - 200;  // ~ perioda 100000 jednotek + sum
                if (j == 0) begin x0 = x; t0 = ts; end
                nn = nn + 1;
                if (j > 0) begin sx = sx + (x - x0); sy = sy + (ts - t0); end
                samp_x = x[25:0]; samp_ts = ts[47:0]; samp_b = isb; samp_v = 1;
                @(negedge clk); samp_v = 0; if (!(tight && j == ns - 1)) repeat (11) @(negedge clk);
            end
            if (isb) seg_b = 0; else seg_a = 0;
            repeat (700) @(negedge clk);                            // deleni: 2 x 68 iteraci x 2 takty + cekani
            xg = sx; yg = (t0 - ref_ts);   // FW 0x0426: delic odstranen -> porovnavaji se SUROVE soucty (deli STM)
            if (isb) begin
                if (!ok_b || n_b !== nn[23:0] || x0_b !== x0[25:0] || sx_b !== sx[41:0] || y0_b !== yg[47:0] || sy_b !== sy[58:3]) begin
                    errs = errs + 1; $display("RUZNOST B: ok=%0d n=%0d/%0d x0=%0d/%0d sx=%0d/%0d y0=%0d/%0d sy3=%0d/%0d", ok_b, n_b, nn, x0_b, x0, sx_b, sx, y0_b, yg, sy_b, sy >> 3); end
            end else begin
                if (!ok_a || n_a !== nn[23:0] || x0_a !== x0[25:0] || sx_a !== sx[41:0] || y0_a !== yg[47:0] || sy_a !== sy[58:3]) begin
                    errs = errs + 1; $display("RUZNOST A: ok=%0d n=%0d/%0d x0=%0d/%0d sx=%0d/%0d y0=%0d/%0d sy3=%0d/%0d", ok_a, n_a, nn, x0_a, x0, sx_a, sx, y0_a, yg, sy_a, sy >> 3); end
            end
        end
    endtask
    initial begin
        repeat (5) @(posedge clk);
        ref_ts = 48'd1_000_000 + ($urandom % 1000000);
        run_seg(0, 312500, 1'b0);
        run_seg(1, 312500, 1'b0);
        if (errs == 0) $display("PASS: tb_regr_big"); else $display("FAIL: tb_regr_big %0d chyb", errs);
        $finish;
    end
endmodule
