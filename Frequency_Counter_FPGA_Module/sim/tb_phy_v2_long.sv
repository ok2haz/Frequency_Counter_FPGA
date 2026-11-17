// tb_phy_v2_long.sv -- PHY protokolu v2 (128 B) pri transakci 1024 B, jakou posle STM s protokolem v3.
// Overuje predpoklad detekce verze (docs/PROTOKOL_V3.md kap. 6.2), 2026-10-22:
//  1) MISO: prvnich 128 B = platny ramec v2 (bajt 0 MAGIC natvrdo, ostatni z TX RAM), dalsi bajty = TYZ ramec
//     dokola (index TX RAM je 7bitovy) -> STM pozna v2 podle VER = 0x02 a CRC 0..125 v prvnich 128 B.
//  2) RX: zpracuji se jen prvni 128 B (rcnt saturuje na 1024 hranach), CRC 0..125, dbg_sck_cnt = 1024;
//     ramec v3 (VER 0x03, CRC po blocich) tedy v2 aplikace odmitne (CRC nesedi) a nic neprovede.
//  3) Po dlouhe transakci funguje normalni transakce 128 B beze zmeny (stav PHY nic nerozbilo).
// Spusteni: iverilog -g2012 -s tb_phy_v2_long -o t.vvp src/spi_slave_phy.v sim/tb_phy_v2_long.sv ; vvp t.vvp
`timescale 1ns/1ps
module tb_phy_v2_long;
  reg clk100 = 0; always #5  clk100 = ~clk100;
  reg clk10  = 0; always #50 clk10  = ~clk10;
  reg sck = 0, cs = 1, mosi = 0; wire miso;
  reg tx_half = 0, tx_we = 0; reg [7:0] tx_waddr = 0, tx_wdata = 0;
  wire tx_base, busy, fe_tgl; wire [7:0] rb0, rb1, rb2, rp0, rp1, rp2; wire [31:0] rseq; wire [15:0] rcc, rcr, dmc, dsc;
  spi_slave_phy phy(.clk(clk100), .clk_app(clk10), .sck_pin(sck), .cs_pin(cs), .mosi_pin(mosi), .miso(miso),
      .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata), .tx_base(tx_base), .busy(busy),
      .rx_b0(rb0), .rx_b1(rb1), .rx_b2(rb2), .rx_seq(rseq), .rx_p0(rp0), .rx_p1(rp1), .rx_p2(rp2),
      .rx_crc_calc(rcc), .rx_crc_recv(rcr), .frame_end_tgl(fe_tgl), .dbg_mosi_cnt(dmc), .dbg_sck_cnt(dsc));

  function [15:0] crc(input [15:0] c0, input [7:0] d);
    integer b; reg [15:0] c; begin c = c0 ^ {d, 8'd0}; for (b = 0; b < 8; b = b + 1) c = c[15] ? ((c << 1) ^ 16'h1021) : (c << 1); crc = c; end
  endfunction

  reg [7:0] ram[0:127];            // ramec v2 v TX RAM (polovina 1)
  reg [7:0] mo[0:1023];            // MOSI (STM)
  reg [7:0] mi[0:1023];            // prijate MISO
  integer i, j, n, errors = 0; reg [15:0] cc;
  integer half_ns = 20;            // SCK 25 MHz

  task wr_ram(input [6:0] a, input [7:0] d);
    begin @(posedge clk10); tx_waddr <= {1'b1, a}; tx_wdata <= d; tx_we <= 1; @(posedge clk10); tx_we <= 0; end
  endtask
  task xfer(input integer nbytes);
    begin cs = 0; #1000;
      for (i = 0; i < nbytes; i = i + 1) begin mi[i] = 0;
        for (j = 7; j >= 0; j = j - 1) begin mosi = mo[i][j]; #(half_ns); sck = 1; mi[i][j] = miso; #(half_ns); sck = 0; end
      end
      #1000; cs = 1; #2000;
    end
  endtask
  task chk(input cond, input [8*64-1:0] what);
    begin if (!cond) begin errors = errors + 1; $display("  CHYBA: %0s", what); end end
  endtask

  initial begin
    // ramec v2 s platnym CRC do poloviny 1, PHY prepnout na ni
    for (i = 0; i < 128; i = i + 1) ram[i] = (i * 37 + 11) & 8'hFF;
    ram[0] = 8'hA5; ram[1] = 8'h02; ram[2] = 8'h80;
    cc = 16'hFFFF; for (i = 0; i < 126; i = i + 1) cc = crc(cc, ram[i]); ram[126] = cc[7:0]; ram[127] = cc[15:8];
    for (i = 0; i < 128; i = i + 1) wr_ram(i[6:0], ram[i]);
    tx_half = 1; #2000;
    chk(tx_base == 1'b1, "tx_base nesleduje tx_half v klidu");

    // 1) transakce 1024 B s ramcem ve stylu v3 (VER 0x03, nahodna data)
    for (i = 0; i < 1024; i = i + 1) mo[i] = (i * 53 + 7) & 8'hFF;
    mo[0] = 8'hA5; mo[1] = 8'h03; mo[2] = 8'h06; mo[4] = 8'h11; mo[5] = 8'h22; mo[6] = 8'h33; mo[7] = 8'h44;
    mo[12] = 8'hC1; mo[13] = 8'hC2; mo[14] = 8'hC3;
    xfer(1024);
    n = 0;
    for (i = 0; i < 1024; i = i + 1) if (mi[i] != ram[i % 128]) n = n + 1;
    chk(n == 0, "MISO pri 1024 B neni ramec v2 dokola");
    if (n) $display("  (MISO: %0d bajtu jinak; mi[0..3] = %h %h %h %h, mi[128] = %h)", n, mi[0], mi[1], mi[2], mi[3], mi[128]);
    cc = 16'hFFFF; for (i = 0; i < 126; i = i + 1) cc = crc(cc, mi[i]);
    chk(cc == {mi[127], mi[126]}, "CRC prvnich 128 B MISO nesedi (detekce v2 by selhala)");
    chk(mi[1] == 8'h02, "VER v MISO neni 0x02");
    chk(rb0 == 8'hA5 && rb1 == 8'h03 && rb2 == 8'h06, "RX hlavicka (bajty 0..2) jinak");
    chk(rseq == 32'h44332211, "RX SEQUENCE jinak");
    chk(rp0 == 8'hC1 && rp1 == 8'hC2 && rp2 == 8'hC3, "RX payload 12..14 jinak");
    cc = 16'hFFFF; for (i = 0; i < 126; i = i + 1) cc = crc(cc, mo[i]);
    chk(rcc == cc, "RX CRC neni pres bajty 0..125");
    chk(rcr == {mo[127], mo[126]}, "RX prijate CRC neni z bajtu 126..127");
    chk(dsc == 16'd1024, "dbg_sck_cnt po 1024 B neni 1024 (saturace)");
    $display("  1024 B: MISO = ramec v2 dokola (%0d chyb), RX bere jen bajty 0..127, dbg_sck_cnt = %0d", n, dsc);

    // 2) normalni transakce 128 B hned potom
    for (i = 0; i < 128; i = i + 1) mo[i] = (i * 29 + 3) & 8'hFF;
    mo[0] = 8'hA5; mo[1] = 8'h02; mo[2] = 8'h06;
    cc = 16'hFFFF; for (i = 0; i < 126; i = i + 1) cc = crc(cc, mo[i]); mo[126] = cc[7:0]; mo[127] = cc[15:8];
    xfer(128);
    n = 0; for (i = 0; i < 128; i = i + 1) if (mi[i] != ram[i]) n = n + 1;
    chk(n == 0, "MISO normalni transakce po dlouhe jinak");
    chk(rcc == rcr, "CRC normalni transakce po dlouhe nesedi");
    chk(rb1 == 8'h02 && dsc == 16'd128 * 8, "normalni transakce: VER nebo pocet hran jinak");
    $display("  128 B po dlouhe: MISO %0d chyb, CRC %s, dbg_sck_cnt = %0d", n, (rcc == rcr) ? "OK" : "NESEDI", dsc);

    if (errors == 0) $display("PASS: tb_phy_v2_long (PHY v2 pri transakci 1024 B, SCK 25 MHz)");
    else $display("FAIL: tb_phy_v2_long (%0d chyb)", errors);
    $finish;
  end
endmodule
