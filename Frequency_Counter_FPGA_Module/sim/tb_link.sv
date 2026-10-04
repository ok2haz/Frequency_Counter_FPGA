// tb_link.sv -- end-to-end SPI: spi_slave_phy (SCK domena + blokova RAM) + spi_app.
// Zapojeni shodne s top.v (fe_s synchronizator konce ramce).
// Spusteni: iverilog -g2012 -o t.vvp sim/tb_link.sv src/spi_slave_phy.v src/spi_app.v ; vvp t.vvp
//
// Overuje (2026-10-04, #264):
//  1) ACK prijat (flags bit6 ack_ok, bit5 rx_crc_error = 0), CAL report na zadost,
//     vypis histogramu (kod 77 -> hodnoty A/B).
//  2) ZATEZ: ramce tesne za sebou (mezera CS 1 us), nova mereni v NAHODNYCH okamzicich
//     (sestavovani ramce behem prenosu), SCK 5 / 20 / 30 MHz. U KAZDEHO prijateho ramce:
//     MAGIC + CRC + KONZISTENCE snimku (edges, dt_a a SEQUENCE patri ke stejnemu mereni:
//     dt_a = edges * 7 + 3, seq z ramce = poradi mereni). Roztrzeny snimek nebo
//     rozpracovany ramec na drate = FAIL.
`timescale 1ns/1ps
module tb_link;
  reg clk100 = 0; always #5  clk100 = ~clk100;
  reg clk10  = 0; always #50 clk10  = ~clk10;
  reg sck = 0, cs = 1, mosi = 0; wire miso;

  wire        fe_tgl, tx_half, tx_we, phy_busy, phy_base;
  wire [7:0]  tx_waddr, tx_wdata, rb0, rb1, rb2, rp0, rp1, rp2;
  wire [31:0] rseq; wire [15:0] rcc, rcr;
  wire [15:0] dmc, dsc;
  spi_slave_phy phy(.clk(clk100), .clk_app(clk10), .sck_pin(sck), .cs_pin(cs), .mosi_pin(mosi), .miso(miso),
      .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata),
      .tx_base(phy_base), .busy(phy_busy),
      .rx_b0(rb0), .rx_b1(rb1), .rx_b2(rb2), .rx_seq(rseq), .rx_p0(rp0), .rx_p1(rp1), .rx_p2(rp2),
      .rx_crc_calc(rcc), .rx_crc_recv(rcr), .frame_end_tgl(fe_tgl), .dbg_mosi_cnt(dmc), .dbg_sck_cnt(dsc));
  reg [2:0] fe = 0; always @(posedge clk10) fe <= {fe[1:0], fe_tgl}; wire rxv = fe[2] ^ fe[1];

  // mereni: konzistentni snimek (dt = edges*7+3), pulz new_meas v nahodnych okamzicich
  reg        new_meas = 0;
  reg [31:0] m_edges = 32'd1000;
  wire [47:0] m_dt = {16'd0, m_edges} * 48'd7 + 48'd3;

  wire cal_mode; wire [1:0] bw; wire [7:0] dbg; wire [7:0] hk;
  spi_app app(.clk(clk10), .meas_freq_x100000(64'd0), .meas_periods(m_edges), .meas_gate_ns(64'd0),
     .meas_timestamp(64'd0), .meas_error_flags(32'd0), .meas_channel(8'd0), .new_meas(new_meas), .signal_lost(1'b0),
     .meas_freq16_x100000(64'd0), .meas_phase_status(8'd0), .meas_status2(8'd0), .meas_dt_a_ps(m_dt),
     .meas_dt_b_ps(48'd0), .meas_periods_b(32'd0), .meas_tdc_status(8'h03), .meas_cal_diag(192'd0),
     .hist_k(hk), .meas_hist_a(24'h123456), .meas_hist_b(24'h0ABCDE),
     .dbg_mosi_cnt(dmc), .dbg_sck_cnt(dsc),
     .rx_valid(rxv), .rx_b0(rb0), .rx_b1(rb1), .rx_b2(rb2), .rx_seq(rseq), .rx_p0(rp0), .rx_p1(rp1), .rx_p2(rp2),
     .rx_crc_calc(rcc), .rx_crc_recv(rcr),
     .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata),
     .phy_busy(phy_busy), .phy_base(phy_base),
     .dbg_status(dbg), .cal_mode(cal_mode), .base_win(bw));

  function [15:0] crc(input [15:0] c0, input [7:0] d);
    integer b; reg [15:0] c; begin c=c0^{d,8'd0}; for(b=0;b<8;b=b+1) c=c[15]?((c<<1)^16'h1021):(c<<1); crc=c; end endfunction
  reg [7:0] tx[0:127]; reg [7:0] rx[0:127];
  integer i, j, errors = 0; reg [15:0] cc;
  integer half_ns = 100;            // pul periody SCK
  integer gap_ns  = 2500;           // CS dole -> 1. SCK a posledni SCK -> CS nahoru
  integer pause_ns = 25000;         // mezera mezi ramci

  task build(input [7:0] ty, input [7:0] p0, input [7:0] p1, input [7:0] p2, input [31:0] sq);
    begin for(i=0;i<128;i=i+1) tx[i]=0; tx[0]=8'hA5; tx[1]=8'h02; tx[2]=ty;
      tx[4]=sq[7:0]; tx[5]=sq[15:8]; tx[6]=sq[23:16]; tx[7]=sq[31:24];
      tx[12]=p0; tx[13]=p1; tx[14]=p2;
      cc=16'hFFFF; for(i=0;i<126;i=i+1) cc=crc(cc,tx[i]); tx[126]=cc[7:0]; tx[127]=cc[15:8]; end endtask
  // Mode 0: master meni MOSI pri SCK dole, vzorkuje MISO na nabezne hrane
  task xfer;
    begin cs=0; #(gap_ns);
      for(i=0;i<128;i=i+1) begin rx[i]=0;
        for(j=7;j>=0;j=j-1) begin mosi=tx[i][j]; #(half_ns); sck=1; rx[i][j]=miso; #(half_ns); sck=0; end end
      #(gap_ns); cs=1; #(pause_ns); end endtask

  // kontrola prijateho DATA ramce: MAGIC, CRC, konzistence snimku
  integer nframes = 0, last_seq = 0;
  task check_frame;
    reg [15:0] c; reg [31:0] e, sq; reg [47:0] d;
    begin
      c = 16'hFFFF; for (i = 0; i < 126; i = i + 1) c = crc(c, rx[i]);
      if (rx[0] !== 8'hA5 || c !== {rx[127], rx[126]}) begin
        errors = errors + 1; if (errors < 6) $display("FAIL: ramec MAGIC=%h CRC %h vs %h", rx[0], c, {rx[127], rx[126]});
      end else if (rx[2] == 8'h80) begin
        e  = {rx[23], rx[22], rx[21], rx[20]};
        d  = {rx[123], rx[122], rx[121], rx[120], rx[119], rx[118]};
        sq = {rx[7], rx[6], rx[5], rx[4]};
        if (e != 32'd0 && d !== {16'd0, e} * 48'd7 + 48'd3) begin
          errors = errors + 1; if (errors < 6) $display("FAIL: roztrzeny snimek edges=%0d dt=%0d seq=%0d", e, d, sq);
        end
        if (e != 32'd0 && sq != e - 32'd999) begin
          errors = errors + 1; if (errors < 6) $display("FAIL: SEQUENCE %0d nepatri k mereni edges=%0d", sq, e);
        end
        nframes = nframes + 1;
      end
    end endtask

  // generator novych mereni v nahodnych okamzicich
  reg stress = 0;
  initial forever begin
    #(1000 + ($urandom % 40000));
    if (stress) begin @(posedge clk10); m_edges <= m_edges + 1; new_meas <= 1; @(posedge clk10); new_meas <= 0; end
  end

  integer r, sp;
  initial begin
    #5000;
    // ---- 1) funkce ----
    build(8'h06,0,0,0,0); xfer; xfer;
    $display("ACK: type=%h flags=%h", rx[2], rx[3]);
    if (rx[3][5] || !rx[3][6]) begin $display("FAIL: ACK neprijat (flags=%h)", rx[3]); errors = errors + 1; end
    build(8'hA0,0,0,0,0); xfer; build(8'h06,0,0,0,0); xfer;
    $display("CAL req->: type=%h (ocek a0)", rx[2]);
    if (rx[2] !== 8'hA0) begin $display("FAIL: CAL report"); errors = errors + 1; end
    build(8'hA0,8'd1,8'd0,8'd77,0); xfer; build(8'h06,0,0,0,0); xfer;
    $display("HIST: type=%h [38]=%h k=%0d A=%h%h%h B=%h%h%h", rx[2], rx[38], rx[39], rx[42],rx[41],rx[40], rx[45],rx[44],rx[43]);
    if (!(rx[2] == 8'hA0 && rx[38] == 8'h01 && rx[39] == 8'd77 && {rx[42],rx[41],rx[40]} == 24'h123456 && {rx[45],rx[44],rx[43]} == 24'h0ABCDE)) begin
      $display("FAIL: vypis histogramu"); errors = errors + 1; end
    $display("DIAG: hrany SCK %0d (ocek 1024), MOSI %0d", dsc, dmc);
    if (dsc != 16'd1024) begin $display("FAIL: pocet hran SCK"); errors = errors + 1; end

    // ---- 2) zatez: 3 rychlosti SCK, mala mezera, nahodna nova mereni ----
    stress = 1;
    for (sp = 0; sp < 3; sp = sp + 1) begin
      half_ns  = (sp == 0) ? 100 : (sp == 1) ? 25 : 16;     // 5 / 20 / ~31 MHz
      gap_ns   = 1000; pause_ns = 1000;
      for (r = 0; r < 40; r = r + 1) begin
        build(8'h06, 0, 0, 0, {rx[7], rx[6], rx[5], rx[4]}); xfer; check_frame;
      end
      $display("SCK %0d MHz: ramcu DATA %0d, chyb zatim %0d", 1000 / (2 * half_ns), nframes, errors);
    end
    if (errors == 0) $display("PASS: tb_link"); else $display("FAIL: tb_link (%0d chyb)", errors);
    $finish;
  end
endmodule
