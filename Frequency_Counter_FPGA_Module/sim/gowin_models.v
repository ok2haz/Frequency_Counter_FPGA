// ============================================================
// gowin_models.v -- BEHAVIORALNI SIMULACNI modely Gowin primitiv pro Icarus.
// Jen pro sim/ (do syntézy NEPATRI -- tam jsou skutecne primitivy).
//
// ALU (jen ALU_MODE 0 = ADD, tak jak ho pouziva tdc_chain):
//     SUM = I0 ^ I1 ^ CIN,  COUT = (I0&I1) | (CIN&(I0|I1))
//   Zpozdeni na instanci: TAP_MEAN_PS +- TAP_SPREAD_PS (nahodne, per instance,
//   deterministicke) + "pilovy" prispevek kazdych 8 stupnu (u skutecneho
//   carry retezu byva skok na hranici slice). Model SUM i COUT zpozduje stejne.
// LUT1/LUT2: zpozdeni LUT_PS (ring oscilator).
// ============================================================
`timescale 1ps/1ps
`ifndef SIM_LUT_PS
`define SIM_LUT_PS 300
`endif
`ifndef SIM_JIT_PS
`define SIM_JIT_PS 25
`endif
// 2026-10-07: model KRZEMIKU (viz docs/audit/2026-10-07_tdc-vlastni-reference.md): na desce
// retez FYZICKY KONCI na ~268. ALU (maxtap 133) a vzorky maji ~39 ps/ALU. Vychozi hodnoty
// zustavaji puvodni (32 +- 12 ps, bez zlomu), aby se stare testy nezmenily.
`ifndef SIM_TAP_MEAN
`define SIM_TAP_MEAN 32
`endif
`ifndef SIM_TAP_SPREAD
`define SIM_TAP_SPREAD 12
`endif
`ifndef SIM_BREAK_IDX
`define SIM_BREAK_IDX (-2)           // index ALU se zlomem (extra zpozdeni); -2 = zadny (nepredany IDX je -1, nesmi se rovnat!)
`endif
`ifndef SIM_BREAK_PS
`define SIM_BREAK_PS 30000
`endif

module ALU (output SUM, output COUT, input I0, input I1, input I3, input CIN);
    parameter ALU_MODE = 0;
    parameter ID = "";
    // 2026-10-03: na krzemiku zmereno ~32 ps/stupen (CAL report: 256 stupnu = ~8,3 ns),
    // STA model rikal 57 ps. Simulace drzi KRZEMIK, ne STA.
    parameter TAP_MEAN_PS   = `SIM_TAP_MEAN;
    parameter TAP_SPREAD_PS = `SIM_TAP_SPREAD;       // +- (rovnomerne)
    parameter integer IDX   = -1;       // poradi ALU v retezu (jen simulace, do syntezy se nepredava)
    integer d;
    integer seed;
    initial begin
        seed = $unsigned($time) + 1;
        d = TAP_MEAN_PS - TAP_SPREAD_PS + ($urandom % (2*TAP_SPREAD_PS + 1));
        if (d < 5) d = 5;
        if (IDX == `SIM_BREAK_IDX) d = d + `SIM_BREAK_PS;   // zlom retezu (P&R skok mezi radami)
    end
    reg s_r, c_r;
    wire s_c = I0 ^ I1 ^ CIN;
    wire c_c = (I0 & I1) | (CIN & (I0 | I1));
    initial begin s_r = 1'b0; c_r = 1'b0; end
    always @(s_c) s_r <= #(d) s_c;
    always @(c_c) c_r <= #(d) c_c;
    assign SUM  = s_r;
    assign COUT = c_r;
endmodule

module LUT1 (output F, input I0);
    parameter INIT = 2'b01;
    parameter LUT_PS = `SIM_LUT_PS;
    parameter JIT_PS = `SIM_JIT_PS;
    wire f_c = I0 ? INIT[1] : INIT[0];
    reg  f_r = 1'b0;   // 2026-10-07: pevny start (drive `initial #1; f_r = f_c` zavodilo -> kruh se obcas zasekl v X)
    integer dly;
    // Transportni zpozdeni s JITTEREM +-JIT_PS (skutecny kruh ma jitter; bez nej
    // je model racionalni a udalosti lezi na mrizce). Startuje z definovaneho
    // stavu (en=0), takze v kruhu je jedina hrana.
    always @(f_c) begin
        dly = LUT_PS + ((JIT_PS > 0) ? ($urandom % (2*JIT_PS + 1)) - JIT_PS : 0);
        f_r <= #(dly) f_c;
    end
    assign F = f_r;
endmodule

module LUT2 (output F, input I0, input I1);
    parameter INIT = 4'b0111;
    parameter LUT_PS = `SIM_LUT_PS;
    parameter JIT_PS = `SIM_JIT_PS;
    // ternary misto INIT[{I1,I0}]: pri X na jednom vstupu vrati definovanou hodnotu,
    // kdyz jsou obe vetve stejne (NAND(0,X)=1) -> kruh se rozbehne z X.
    wire f_c = I0 ? (I1 ? INIT[3] : INIT[1]) : (I1 ? INIT[2] : INIT[0]);
    reg  f_r = 1'b0;   // 2026-10-07: pevny start (drive `initial #1; f_r = f_c` zavodilo -> kruh se obcas zasekl v X)
    integer dly;
    always @(f_c) begin
        dly = LUT_PS + ((JIT_PS > 0) ? ($urandom % (2*JIT_PS + 1)) - JIT_PS : 0);
        f_r <= #(dly) f_c;
    end
    assign F = f_r;
endmodule


// OSC (vnitrni oscilator FPGA): f = 250 MHz / FREQ_DIV (UG286 6.1.1, GW1NR-9C). Behavioralni model pro Icarus.
module OSC #(parameter FREQ_DIV = 100, parameter DEVICE = "GW1N-4") (output reg OSCOUT);
    initial OSCOUT = 1'b0;
    always #(FREQ_DIV * 2000) OSCOUT = ~OSCOUT;     // pulperioda [ps] = FREQ_DIV * 2 ns
endmodule

// ------------------------------------------------------------
// 2026-10-23 (E4, 1PPS pres IDES8): rPLL, CLKDIV a IDES8 -- jen to, co pouziva pps_ides3 (v3_core.v). Behavioralne,
// casovani zjednodusene; poradi bitu IDES8 (Q0 = nejdrivejsi vzorek) je PREDPOKLAD -- overit na desce (PROTOKOL_V3 kap. 10).
// rPLL: CLKOUT = CLKIN * (FBDIV_SEL+1) / (IDIV_SEL+1), fazove zavesena na nabeznou hranu CLKIN posunutou o SIM_PLL_PS
// (skutecna PLL s vnitrni zpetnou vazbou ma pevny, neznamy posun vuci CLKIN -- test ho musi snest). LOCK po 16 periodach.
`ifndef SIM_PLL_PS
`define SIM_PLL_PS 700
`endif
module rPLL #(
    parameter FCLKIN = "100", parameter DEVICE = "GW1NR-9C",
    parameter integer IDIV_SEL = 0, parameter integer FBDIV_SEL = 0, parameter integer ODIV_SEL = 8,
    parameter DYN_IDIV_SEL = "false", parameter DYN_FBDIV_SEL = "false", parameter DYN_ODIV_SEL = "false",
    parameter PSDA_SEL = "0000", parameter DUTYDA_SEL = "1000", parameter DYN_DA_EN = "false",
    parameter CLKOUT_FT_DIR = 1'b1, parameter CLKOUTP_FT_DIR = 1'b1, parameter integer CLKOUT_DLY_STEP = 0,
    parameter integer CLKOUTP_DLY_STEP = 0, parameter CLKFB_SEL = "internal", parameter CLKOUT_BYPASS = "false",
    parameter CLKOUTP_BYPASS = "false", parameter CLKOUTD_BYPASS = "false", parameter integer DYN_SDIV_SEL = 2,
    parameter CLKOUTD_SRC = "CLKOUT", parameter CLKOUTD3_SRC = "CLKOUT"
)(
    output reg CLKOUT, output reg LOCK, output CLKOUTP, output CLKOUTD, output CLKOUTD3,
    input RESET, input RESET_P, input CLKIN, input CLKFB,
    input [5:0] FBDSEL, input [5:0] IDSEL, input [5:0] ODSEL, input [3:0] PSDA, input [3:0] DUTYDA, input [3:0] FDLY
);
    localparam integer M = (FBDIV_SEL + 1) / (IDIV_SEL + 1);      // vystupnich period na periodu CLKIN (celociselne)
    initial begin CLKOUT = 1'b0; LOCK = 1'b0; end
    assign CLKOUTP = CLKOUT; assign CLKOUTD = 1'b0; assign CLKOUTD3 = 1'b0;
    time    t_last = 0, per = 0;
    integer n = 0, k;
    always @(posedge CLKIN) begin
        if (t_last != 0) per = $time - t_last;
        t_last = $time;
        if (RESET) begin n = 0; LOCK <= 1'b0; end
        else begin
            if (n < 16) n = n + 1;
            LOCK <= (n >= 16);
            if (n >= 4 && per > 0)
                for (k = 0; k < 2 * M; k = k + 1)
                    CLKOUT <= #(`SIM_PLL_PS + (k * per) / (2 * M)) ((k % 2) == 0);
        end
    end
endmodule

// CLKDIV (DIV_MODE "4"): CLKOUT nabezne kazdou 4. nabeznou hranu HCLKIN po uvolneni RESETN, strida 1:1, 1 ps za HCLKIN.
// V resetu CLKOUT = 0 (PCLK stoji -> asynchronni reset logiky PCLK se uvolni bez metastability).
module CLKDIV #(parameter DIV_MODE = "4", parameter GSREN = "false") (input HCLKIN, input RESETN, input CALIB, output reg CLKOUT);
    integer c = 0;
    initial CLKOUT = 1'b0;
    always @(posedge HCLKIN or negedge RESETN) begin
        if (!RESETN) begin c = 0; CLKOUT <= 1'b0; end
        else begin
            c = (c + 1) % 4;
            if (c == 1) CLKOUT <= #1 1'b1;
            if (c == 3) CLKOUT <= #1 1'b0;
        end
    end
endmodule

// IDES8: D vzorkovano obema hranami FCLK (DDR, 8 vzorku za periodu PCLK = 4 x FCLK), pri nabezne PCLK vystup slova
// Q0..Q7 = vzorky od nejstarsiho po nejnovejsi (PREDPOKLAD, viz vyse). RESET = 1: Q = 0.
module IDES8 #(parameter GSREN = "false", parameter LSREN = "true") (
    input D, input FCLK, input PCLK, input CALIB, input RESET,
    output reg Q0, output reg Q1, output reg Q2, output reg Q3, output reg Q4, output reg Q5, output reg Q6, output reg Q7
);
    reg [7:0] sh = 8'd0;
    initial {Q7, Q6, Q5, Q4, Q3, Q2, Q1, Q0} = 8'd0;
    always @(posedge FCLK or negedge FCLK) sh <= {D, sh[7:1]};      // sh[7] = nejnovejsi
    always @(posedge PCLK or posedge RESET)
        if (RESET) {Q7, Q6, Q5, Q4, Q3, Q2, Q1, Q0} <= 8'd0;
        else       {Q7, Q6, Q5, Q4, Q3, Q2, Q1, Q0} <= sh;
endmodule
