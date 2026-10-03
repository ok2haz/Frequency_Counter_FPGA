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

module ALU (output SUM, output COUT, input I0, input I1, input I3, input CIN);
    parameter ALU_MODE = 0;
    parameter ID = "";
    parameter TAP_MEAN_PS   = 57;
    parameter TAP_SPREAD_PS = 20;       // +- (rovnomerne)
    integer d;
    integer seed;
    initial begin
        seed = $unsigned($time) + 1;
        d = TAP_MEAN_PS - TAP_SPREAD_PS + ($urandom % (2*TAP_SPREAD_PS + 1));
        if (d < 5) d = 5;
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
    reg  f_r;
    integer dly;
    // Transportni zpozdeni s JITTEREM +-JIT_PS (skutecny kruh ma jitter; bez nej
    // je model racionalni a udalosti lezi na mrizce). Startuje z definovaneho
    // stavu (en=0), takze v kruhu je jedina hrana.
    initial begin #1; f_r = f_c; end
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
    reg  f_r;
    integer dly;
    initial begin #1; f_r = f_c; end
    always @(f_c) begin
        dly = LUT_PS + ((JIT_PS > 0) ? ($urandom % (2*JIT_PS + 1)) - JIT_PS : 0);
        f_r <= #(dly) f_c;
    end
    assign F = f_r;
endmodule
