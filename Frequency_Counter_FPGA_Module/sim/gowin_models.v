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
