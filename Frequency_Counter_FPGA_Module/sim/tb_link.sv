// tb_link.sv -- end-to-end SPI: spi_slave_phy + spi_app (STM->FPGA ACK a zadost o CAL).
// Spusteni: iverilog -g2012 -o t.vvp sim/tb_link.sv src/spi_slave_phy.v src/spi_app.v ; vvp t.vvp
// Ocekavano: ACK flags=43 (bez bitu 5), CAL req -> rx type=a0. Se starou PHY (nulovani
// rx_shadow po CS rise) vyjde flags=23 a type=80 -- presne symptom z desky 2026-10-03.
`timescale 1ns/1ps
module tb_link;
  reg clk100=0; always #5 clk100=~clk100;
  reg clk10=0;  always #50 clk10=~clk10;
  reg sck=0, cs=1, mosi=0; wire miso;
  wire [1023:0] rxf, txf; wire tgl; wire [10:0] bc; wire txv;
  spi_slave_phy phy(.clk(clk100),.sck_pin(sck),.cs_pin(cs),.mosi_pin(mosi),.miso(miso),
     .tx_frame_flat(txf),.tx_valid(tv_s),.rx_frame_flat(rxf),.frame_end_tgl(tgl),.rx_bit_count(bc));
  reg [2:0] txv_s=0; always @(posedge clk100) txv_s<={txv_s[1:0],txv}; wire tv_s=txv_s[2];
  reg [2:0] fe=0; always @(posedge clk10) fe<={fe[1:0],tgl}; wire rxv=fe[2]^fe[1];
  wire cal_mode; wire [1:0] bw; wire [7:0] dbg;
  spi_app app(.clk(clk10),.meas_freq_x100000(64'd0),.meas_periods(32'd0),.meas_gate_ns(64'd0),
     .meas_timestamp(64'd0),.meas_error_flags(32'd0),.meas_channel(8'd0),.new_meas(1'b0),.signal_lost(1'b0),
     .meas_freq16_x100000(64'd0),.meas_phase_status(8'd0),.meas_status2(8'd0),.meas_dt_a_ps(48'd0),
     .meas_dt_b_ps(48'd0),.meas_periods_b(32'd0),.meas_tdc_status(8'h03),.meas_cal_diag(192'd0),
     .rx_frame_flat(rxf),.rx_valid(rxv),.tx_frame_flat(txf),.tx_valid(txv),.dbg_status(dbg),
     .cal_mode(cal_mode),.base_win(bw));
  function [15:0] crc(input [15:0] c0, input [7:0] d);
    integer b; reg [15:0] c; begin c=c0^{d,8'd0}; for(b=0;b<8;b=b+1) c=c[15]?((c<<1)^16'h1021):(c<<1); crc=c; end endfunction
  reg [7:0] tx[0:127]; reg [7:0] rx[0:127];
  integer i,j; reg [15:0] cc;
  task build(input [7:0] ty, input [7:0] p0, input [7:0] p1);
    begin for(i=0;i<128;i=i+1) tx[i]=0; tx[0]=8'hA5; tx[1]=8'h02; tx[2]=ty; tx[12]=p0; tx[13]=p1;
      cc=16'hFFFF; for(i=0;i<126;i=i+1) cc=crc(cc,tx[i]); tx[126]=cc[7:0]; tx[127]=cc[15:8]; end endtask
  task xfer;
    begin cs=0; #2500;
      for(i=0;i<128;i=i+1) begin rx[i]=0;
        for(j=7;j>=0;j=j-1) begin mosi=tx[i][j]; #100; sck=1; rx[i][j]=miso; #100; sck=0; end end
      #2500; cs=1; #25000; end endtask
  initial begin
    #5000; // po startu
    build(8'h06,0,0); xfer; xfer;
    $display("ACK: rx type=%h flags=%h  (bit5=rx_crc_error)", rx[2], rx[3]);
    build(8'hA0,0,0); xfer; build(8'h06,0,0); xfer;
    $display("CAL req->: rx type=%h (ocek A0) flags=%h", rx[2], rx[3]);
    $finish;
  end
endmodule
