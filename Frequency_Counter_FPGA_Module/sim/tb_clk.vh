// tb_clk.vh -- referencni hodiny FPGA v testbenchich: 100 MHz (top.v CLK_MHZ; FW 0x0421/0x0422 bezely na 125 MHz).
// Prepsat na prikazove radce: iverilog -DTB_CLK_MHZ=... (hlidac hodin a gate_div v RTL plati jen pro 100 MHz).
`ifndef TB_CLK_MHZ
`define TB_CLK_MHZ 100
`endif
`define TB_THALF_PS   (500000/`TB_CLK_MHZ)                     // pulperioda hodin [ps]
`define TB_TCLK_R     (1.0e6/`TB_CLK_MHZ)                      // perioda hodin [ps], real
`define TB_PS_PER_UNIT (1.0e6/(`TB_CLK_MHZ*16384.0))           // jednotka casu v ramci T_clk/16384 [ps]
