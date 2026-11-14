// ============================================================
// tb_lutequiv.sv -- FW 0x0427: vzorec kalibracni tabulky TDC ze histogramu (to, co pocita STM, fpga_tdccal.c) == vzorec
// ve FPGA (AUTOCAL = 1, puvodni vypocet). Jeden kanal tdc_chan s ring oscilatorem se zkalibruje SAM (AUTOCAL = 1, jen sim);
// pak se z jeho histogramu (hierarchicky ca.hist) spocita tabulka nezavislym SV vzorcem a porovna s ca.lut (VSECHNY adresy).
// Navic vypise histogram a tabulku jako radky "HIST;k;h" / "LUT;k;v" (python/fpga_tdccal selftest: zlate vektory).
// ============================================================
`timescale 1ps/1ps
`include "tb_clk.vh"
module tb_lutequiv;
    localparam CAL_LOG2 = 15;
    reg clk = 0; always #(`TB_THALF_PS) clk = ~clk;
    reg [47:0] tick_ps = 48'd0; always @(posedge clk) tick_ps <= tick_ps + 48'd16384;
    reg sig = 0;
    wire ro, use_ro;
    ring_osc #(.DIV_LOG2(2)) u_ro (.en(use_ro), .out(ro));
    reg [27:0] tmo = 0; reg abort = 0;
    always @(posedge clk) begin abort <= 0; if (use_ro) begin tmo <= tmo + 1; if (&tmo) abort <= 1; end else tmo <= 0; end
    reg cal_req = 0;
    wire rise_c, rise_s, trig, tsv, busy, valid, fail, wait_o, bub;
    wire [47:0] ts; wire [31:0] ovf, peak; wire [15:0] nz, last, maxtap; wire [23:0] hq;
    tdc_chan #(.CAL_LOG2(CAL_LOG2), .AUTOCAL(1)) ca (
        .clk(clk), .sig_raw(sig), .ro(ro), .tick_ps(tick_ps), .want(1'b0), .want_seg(1'b0),
        .cal_req(cal_req), .cal_abort(abort), .lw_en(1'b0), .lw_a(10'd0), .lw_neg(14'd0), .lw_commit(1'b0), .cal_wait(wait_o),
        .rise_c(rise_c), .rise_s(rise_s), .trig_ack(trig), .ts_valid(tsv), .ts_ps(ts), .use_ro(use_ro), .cal_busy(busy),
        .cal_valid(valid), .cal_fail(fail), .d_ovf(ovf), .d_peak(peak), .d_nz(nz), .d_last(last), .d_maxtap(maxtap),
        .dump_k(10'd0), .dump_q(hq), .bub_o(bub));
    localparam NC = 256;
    integer a, errs = 0;
    longint cum, xv, xsh, x14, h;
    reg [13:0] neg; reg [1:0] amt; reg [15:0] exp_v;
    initial begin
        #200000;
        @(posedge clk); cal_req <= 1; @(posedge clk); cal_req <= 0;
        wait (valid || fail);
        repeat (50) @(posedge clk);
        if (fail) begin $display("FAIL: kalibrace selhala"); $finish; end
        cum = 0;
        for (a = 0; a < NC; a = a + 1) begin
            h   = ca.hist[a];
            xv  = 2 * cum + h;
            cum = cum + h;
            xsh = xv >> (CAL_LOG2 - 13);
            x14 = (xsh > 16383) ? 16383 : xsh;
            neg = (14'd0 - x14[13:0]);
            amt = 2'd1 + ((x14 != 0) ? 2'd1 : 2'd0);
            exp_v = {amt, neg};
            $display("HIST;%0d;%0d", a, h);
            $display("LUT;%0d;%0d", a, ca.lut[a]);
            if (ca.lut[a] !== exp_v) begin
                errs = errs + 1;
                if (errs < 10) $display("RUZNOST a=%0d hist=%0d lut=%04x ocekavano=%04x", a, h, ca.lut[a], exp_v);
            end
        end
        $display("SUM hist=%0d (2^%0d = %0d)", cum, CAL_LOG2, 1 << CAL_LOG2);
        if (errs == 0) $display("PASS: tb_lutequiv (%0d adres)", NC); else $display("FAIL: tb_lutequiv (%0d rozdilu)", errs);
        $finish;
    end
    initial begin #(64'd5_000_000_000); $display("FAIL: timeout"); $finish; end
endmodule
