// tb_phy_v3.sv -- PHY protokolu v3 (src/spi_slave_phy_v3.v): transakce 1024 B, CRC po blocich 256 B (2026-10-22).
// Overuje, pri SCK 5 / 25 / 30 MHz:
//  1) MISO: bajt 0 = MAGIC, bajty z TX RAM, na pozicich 254/255 KAZDEHO bloku CRC-16/CCITT-FALSE bajtu 0..253
//     bloku (LE) spocitane PHY za letu -- TX RAM tam schvalne obsahuje smeti, ktere se NESMI vyslat.
//  2) RX: rx_blk_ok = 1111 u spravneho ramce, hlavicka (MAGIC, VER, TYPE, SEQ, p0..p3), cely ramec v RX RAM
//     (polovina rx_rd_half), rx_len_ok = 1, dbg_sck_cnt = 8192.
//  3) Poskozeny bajt v bloku 2 -> rx_blk_ok = 1011 (jen ten blok).
//  4) Delka: 1023 B a 1025 B -> rx_len_ok = 0; 1024 B -> 1.
//  5) Prepnuti poloviny TX BEHEM transakce: probihajici ramec cely ze stare poloviny, dalsi z nove.
//  6) RX RAM stridave: po ramci B je v rx_rd_half ramec B a v druhe polovine porad ramec A.
// Spusteni: iverilog -g2012 -s tb_phy_v3 -o t.vvp src/spi_slave_phy_v3.v sim/tb_phy_v3.sv ; vvp t.vvp
`timescale 1ns/1ps
module tb_phy_v3;
  reg clk100 = 0; always #5  clk100 = ~clk100;
  reg clk10  = 0; always #50 clk10  = ~clk10;
  reg sck = 0, cs = 1, mosi = 0; wire miso;
  reg tx_half = 0, tx_we = 0; reg [10:0] tx_waddr = 0; reg [7:0] tx_wdata = 0; reg [9:0] rx_raddr = 0;
  wire tx_base, busy, fe_tgl, len_ok, rd_half; wire [3:0] blk_ok;
  wire [7:0] rb0, rb1, rb2, rp0, rp1, rp2, rp3, rx_rdata; wire [31:0] rseq; wire [15:0] dsc;
  spi_slave_phy_v3 phy(.clk(clk100), .clk_app(clk10), .sck_pin(sck), .cs_pin(cs), .mosi_pin(mosi), .miso(miso),
      .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata), .tx_base(tx_base), .busy(busy),
      .rx_b0(rb0), .rx_b1(rb1), .rx_b2(rb2), .rx_seq(rseq), .rx_p0(rp0), .rx_p1(rp1), .rx_p2(rp2), .rx_p3(rp3),
      .rx_blk_ok(blk_ok), .rx_len_ok(len_ok), .frame_end_tgl(fe_tgl),
      .rx_rd_half(rd_half), .rx_raddr(rx_raddr), .rx_rdata(rx_rdata), .dbg_sck_cnt(dsc));

  function [15:0] crc(input [15:0] c0, input [7:0] d);
    integer b; reg [15:0] c; begin c = c0 ^ {d, 8'd0}; for (b = 0; b < 8; b = b + 1) c = c[15] ? ((c << 1) ^ 16'h1021) : (c << 1); crc = c; end
  endfunction

  reg [7:0] ram0[0:1023], ram1[0:1023];   // obsah TX RAM v polovine 0 / 1 (jak ho zapsala „aplikace")
  reg [7:0] mo[0:1199], mi[0:1199], prevA[0:1023];
  integer i, j, k, b, n, errors = 0, fe_prev, fe_cnt = 0; reg [15:0] cc;
  integer half_ns = 20;
  reg switch_mid = 0;

  always @(fe_tgl) fe_cnt = fe_cnt + 1;

  task chk(input cond, input [8*72-1:0] what);
    begin if (!cond) begin errors = errors + 1; $display("  CHYBA: %0s", what); end end
  endtask
  task wr(input h, input [9:0] a, input [7:0] d);
    begin @(posedge clk10); tx_waddr <= {h, a}; tx_wdata <= d; tx_we <= 1; @(posedge clk10); tx_we <= 0; end
  endtask
  task fill_half(input h, input integer seed);
    begin for (i = 0; i < 1024; i = i + 1) begin
        k = (i * 131 + seed * 17 + 5) & 8'hFF;
        if (h) ram1[i] = k; else ram0[i] = k;
        wr(h, i[9:0], k[7:0]);
      end end
  endtask
  // MOSI ramec v3 se spravnym CRC po blocich (+ volitelne poskozeni)
  task build_mosi(input integer seed, input integer bad_blk);
    begin for (i = 0; i < 1024; i = i + 1) mo[i] = (i * 53 + seed * 29 + 7) & 8'hFF;
      mo[0] = 8'hA5; mo[1] = 8'h03; mo[2] = 8'h06; mo[3] = 0;
      mo[4] = seed[7:0]; mo[5] = 8'h22; mo[6] = 8'h33; mo[7] = 8'h44; mo[12] = 8'hC1; mo[13] = 8'hC2; mo[14] = 8'hC3; mo[15] = 8'hC4;
      for (b = 0; b < 4; b = b + 1) begin
        cc = 16'hFFFF; for (i = 0; i < 254; i = i + 1) cc = crc(cc, mo[b * 256 + i]);
        mo[b * 256 + 254] = cc[7:0]; mo[b * 256 + 255] = cc[15:8];
      end
      if (bad_blk >= 0) mo[bad_blk * 256 + 100] = mo[bad_blk * 256 + 100] ^ 8'h10;
    end
  endtask
  task xfer(input integer nbytes);
    begin cs = 0; #1000;
      for (i = 0; i < nbytes; i = i + 1) begin mi[i] = 0;
        if (switch_mid && i == 300) fork begin tx_half = ~tx_half; end join_none
        for (j = 7; j >= 0; j = j - 1) begin mosi = mo[i][j]; #(half_ns); sck = 1; mi[i][j] = miso; #(half_ns); sck = 0; end
      end
      #1000; cs = 1; #3000;
    end
  endtask
  // ocekavany MISO z dane poloviny: bajt 0 MAGIC, 254/255 CRC bloku z vyslanych bajtu
  task check_miso(input h, input [8*24-1:0] tag);
    begin n = 0;
      for (b = 0; b < 4; b = b + 1) begin
        cc = 16'hFFFF;
        for (i = 0; i < 254; i = i + 1) begin
          k = (b == 0 && i == 0) ? 8'hA5 : (h ? ram1[b * 256 + i] : ram0[b * 256 + i]);
          if (mi[b * 256 + i] !== k[7:0]) n = n + 1;
          cc = crc(cc, mi[b * 256 + i]);
        end
        if (mi[b * 256 + 254] !== cc[7:0] || mi[b * 256 + 255] !== cc[15:8]) begin
          n = n + 1; $display("  (%0s: blok %0d CRC na drate %h%h, ocekavano %h)", tag, b, mi[b*256+255], mi[b*256+254], cc);
        end
      end
      chk(n == 0, "MISO neodpovida TX RAM / CRC bloku");
      if (n) $display("  (%0s: %0d chybnych bajtu)", tag, n);
    end
  endtask
  task read_rx(input h_expect_cur, input [8*24-1:0] tag);   // porovna RX RAM (rx_rd_half) s mo[]
    begin n = 0;
      for (i = 0; i < 1024; i = i + 1) begin
        @(posedge clk10); rx_raddr <= i[9:0]; @(posedge clk10); @(posedge clk10);
        if (rx_rdata !== mo[i]) n = n + 1;
      end
      chk(n == 0, "RX RAM neodpovida prijatemu ramci");
      if (n) $display("  (%0s: RX RAM %0d chyb)", tag, n);
    end
  endtask

  integer sp; integer HALF[0:2];
  initial begin
    HALF[0] = 100; HALF[1] = 20; HALF[2] = 17;          // SCK 5 / 25 / ~30 MHz
    fill_half(0, 1); fill_half(1, 2);
    // smeti na pozicich, ktere PHY prepisuje (bajt 0, 254/255 bloku) -- nesmi se vyslat
    for (b = 0; b < 4; b = b + 1) begin wr(1, b * 256 + 254, 8'hEE); ram1[b*256+254] = 8'hEE; wr(1, b * 256 + 255, 8'hEE); ram1[b*256+255] = 8'hEE; end
    wr(1, 0, 8'h00); ram1[0] = 8'h00;

    for (sp = 0; sp < 3; sp = sp + 1) begin
      half_ns = HALF[sp];
      $display("--- SCK %0d MHz ---", 500 / half_ns);
      // 1 + 2: TX z poloviny 1, spravny MOSI ramec
      tx_half = 1; #2000;
      build_mosi(10 + sp, -1); fe_prev = fe_cnt; xfer(1024);
      check_miso(1, "1024 B");
      chk(blk_ok == 4'b1111, "rx_blk_ok u spravneho ramce neni 1111");
      chk(len_ok == 1'b1 && dsc == 16'd8192, "rx_len_ok / dbg_sck_cnt u 1024 B");
      chk(rb0 == 8'hA5 && rb1 == 8'h03 && rb2 == 8'h06 && rseq == {8'h44, 8'h33, 8'h22, mo[4]}, "RX hlavicka / SEQ");
      chk({rp3, rp2, rp1, rp0} == 32'hC4C3C2C1, "RX p0..p3 (REC_ACK)");
      chk(fe_cnt == fe_prev + 1, "frame_end_tgl se neprepnul prave jednou");
      read_rx(1, "ramec A");
      for (i = 0; i < 1024; i = i + 1) prevA[i] = mo[i];
      // 3: poskozeny blok 2
      build_mosi(20 + sp, 2); xfer(1024);
      chk(blk_ok == 4'b1011, "poskozeny blok 2: rx_blk_ok neni 1011");
      // 6: v druhe polovine RX RAM porad ramec A (ramec B je ten poskozeny, A o dva ramce zpet -> zopakovat A)
      for (i = 0; i < 1024; i = i + 1) mo[i] = prevA[i];
      xfer(1024);                                     // A znovu (polovina X)
      build_mosi(30 + sp, -1); xfer(1024);            // B (polovina Y)
      read_rx(0, "ramec B");
      // druha polovina RX RAM (primo z pameti modelu, jen v testbenchi) -> musi tam byt A
      n = 0;
      for (i = 0; i < 1024; i = i + 1) if (phy.rxram[{~rd_half, i[9:0]}] !== prevA[i]) n = n + 1;
      chk(n == 0, "druha polovina RX RAM neobsahuje predchozi ramec");
      // 4: delka
      build_mosi(40 + sp, -1); xfer(1023); chk(len_ok == 1'b0, "1023 B: rx_len_ok neni 0");
      xfer(1025); chk(len_ok == 1'b0, "1025 B: rx_len_ok neni 0");
      xfer(1024); chk(len_ok == 1'b1, "1024 B po chybne delce: rx_len_ok neni 1");
      // 5: prepnuti poloviny uprostred transakce (z 1 na 0 na bajtu 300)
      tx_half = 1; #2000; switch_mid = 1; xfer(1024); switch_mid = 0;
      check_miso(1, "behem prepnuti");
      chk(tx_half == 1'b0, "tb: tx_half se neprepnul");
      xfer(1024); check_miso(0, "po prepnuti");
    end

    if (errors == 0) $display("PASS: tb_phy_v3 (1024 B, CRC po blocich, delka, poloviny TX/RX; SCK 5/25/30 MHz)");
    else $display("FAIL: tb_phy_v3 (%0d chyb)", errors);
    $finish;
  end
endmodule
