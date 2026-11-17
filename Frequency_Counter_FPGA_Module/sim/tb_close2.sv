// ============================================================
// tb_close2.sv -- CELY top.v (SIM_TOP): oprava close+2 ve win_recip (FW 0x0425, E0 audit 2026-10-20, kap. 2.2).
//   Chyba FW <= 0x0424: hrana, ktera prisla v taktu nulovani citace (cl_q2), se ztratila. Projevi se jen kdyz jsou hrany
//   >= 2 takty od sebe a casto (nad ~33 MHz); kontrola |ccd| <= 1 ji propustila (model m_win_count.py: chybna okna
//   6 % pri 34 MHz, 60 % pri 40, 96 % pri 49).
//   Test: CH_A = TB_FA_MHZ * (1 + 813 ppm) (asynchronne k 100 MHz, faze "prochazi"), N_WIN oken po ustaleni kalibrace.
//   Kazde okno: kmitocet z (periods, dt) proti skutecnemu s mezi REL_TOL (v SIM_TOP je hradlo kratke: ztrata 1 hrany
//   pri 40 MHz = ~1e-5, kvantizace zjednodusene TDC tabulky SIM_TOP az ~3e-6) a |ccd| <= 1.
//   0x0425 musi projit; 0x0424 musi pri 40 a 49 MHz selhat (negativni kontrola, spousti se rucne proti zaloze RTL).
// ============================================================
`timescale 1ps/1fs
`include "tb_clk.vh"
`ifndef TB_FA_MHZ
`define TB_FA_MHZ 40.0
`endif
`ifndef N_WIN
`define N_WIN 8
`endif
module tb_close2;
    // Mez 5e-6: ztrata 1 hrany = 1/N ~ 1e-5 (N ~ 1e5 v kratkem SIM_TOP okne); zjednodusena TDC tabulka SIM_TOP (SIM_LUT_PS=800)
    // dava kvantizaci znacky az ~3e-6 (2,5 / 7,5 ns) i bez chyby -- zmereno 2026-10-20 (0x0425: max 3,0e-6; 0x0424: -7e-6 a -9e-6).
    localparam real REL_TOL = 5.0e-6;
    reg clk10 = 0, clk100 = 0;
    always #50000 clk10 = ~clk10;
    initial begin #2300; forever begin #(`TB_THALF_PS); clk100 = ~clk100; end end
    real TA = 1.0e6 / (`TB_FA_MHZ * (1.0 + 813e-6));   // [ps]
    real TB = 200000.0 / (1.0 - 300e-6);                // CH_B: 5 MHz (sekundarni slot)
    reg ch_a = 0, ch_b = 0;
    initial begin #12345; forever begin ch_a = 1; #(TA/2.0); ch_a = 0; #(TA/2.0); end end
    initial begin #23456; forever begin ch_b = 1; #(TB/2.0); ch_b = 0; #(TB/2.0); end end
    wire miso, led;
    top dut (.clk_ref_10m(clk10), .clk_p0_100m(clk100), .ch_a(ch_a), .ch_b(ch_b), .led_tx(led), .ref_ctrl(), .ext_ref_sens(1'b0),
             .spi_sck(1'b0), .spi_cs_n(1'b1), .spi_mosi(1'b0), .spi_miso(miso));

    real fa, fp, rel;
    integer win = 0, errs = 0, skip = 2, ccd;
    initial fa = 1.0e12 / TA;

    function real hz(input [31:0] per, input [47:0] dt);
        hz = (dt == 0) ? 0.0 : real'(per) / (real'(dt) * `TB_PS_PER_UNIT) * 1.0e12;
    endfunction

    always @(posedge clk10) begin
        if (dut.valid_a && dut.cal_valid_a && !dut.cal_busy_a && dut.cal_valid_b && !dut.cal_busy_b) begin
            fp  = hz(dut.periods_p, dut.dt_p);
            ccd = $signed(dut.ccd_p);
            rel = fp / fa - 1.0;
            if (skip > 0) begin
                skip = skip - 1;                     // prvni okna po kalibraci (rozpracovane okno) se nehodnoti
            end else begin
                win = win + 1;
                $display("okno %0d: N=%0d  f=%.3f Hz  chyba %.3e  ccd=%0d", win, dut.periods_p, fp, rel, ccd);
                if (rel > REL_TOL || rel < -REL_TOL) begin errs = errs + 1; $display("  CHYBA: kmitocet (ztracena/navic hrana?)"); end
                if (ccd > 1 || ccd < -1) begin errs = errs + 1; $display("  CHYBA: ccd"); end
                if (win == `N_WIN) begin
                    if (errs == 0) $display("PASS: tb_close2 (%0.1f MHz, %0d oken)", `TB_FA_MHZ, win);
                    else           $display("FAIL: tb_close2 (%0.1f MHz, %0d chyb v %0d oknech)", `TB_FA_MHZ, errs, win);
                    $finish;
                end
            end
        end
    end

    initial begin #(64'd400_000_000_000); $display("FAIL: timeout (win=%0d)", win); $finish; end
endmodule
