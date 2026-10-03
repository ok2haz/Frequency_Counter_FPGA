// ============================================================
// tb_phy_equiv.sv -- ekvivalence spi_slave_phy (nova, predpocitany posun TX)
// proti puvodnimu modulu (sim/_phy_old.v = kopie pred upravou, modul
// spi_slave_phy_old). Stejny nahodny SPI provoz (vc. neobvyklych: CS drzene
// dole, kratke/dlouhe ramce, SCK mimo CS, zmena tx_valid), porovnani vsech
// vystupu KAZDY takt.
// ============================================================
`timescale 1ns/1ps
module tb_phy_equiv;
    reg clk = 0; always #5 clk = ~clk;      // 100 MHz
    reg sck = 0, cs = 1, mosi = 0;
    reg [1023:0] txf = 0; reg txv = 1;
    wire miso_n, miso_o; wire [1023:0] rx_n, rx_o; wire tgl_n, tgl_o; wire [10:0] bc_n, bc_o;
    spi_slave_phy     dn (.clk(clk), .sck_pin(sck), .cs_pin(cs), .mosi_pin(mosi), .miso(miso_n),
                          .tx_frame_flat(txf), .tx_valid(txv), .rx_frame_flat(rx_n),
                          .frame_end_tgl(tgl_n), .rx_bit_count(bc_n));
    spi_slave_phy_old do_(.clk(clk), .sck_pin(sck), .cs_pin(cs), .mosi_pin(mosi), .miso(miso_o),
                          .tx_frame_flat(txf), .tx_valid(txv), .rx_frame_flat(rx_o),
                          .frame_end_tgl(tgl_o), .rx_bit_count(bc_o));
    integer errors = 0, cyc = 0;
    always @(posedge clk) begin
        cyc = cyc + 1;
        if (cyc > 5 && (miso_n !== miso_o || rx_n !== rx_o || tgl_n !== tgl_o || bc_n !== bc_o)) begin
            if (errors < 5) $display("ROZDIL @%0t: miso %b/%b tgl %b/%b bc %0d/%0d rx_eq=%b",
                                      $time, miso_n, miso_o, tgl_n, tgl_o, bc_n, bc_o, rx_n === rx_o);
            errors = errors + 1;
        end
    end
    integer i, f, nb, half, r;
    task spi_frame(input integer nbits, input integer halfper, input integer gap);
        begin
            cs = 0; #(gap);
            for (i = 0; i < nbits; i = i + 1) begin
                mosi = $urandom; #(halfper); sck = 1; #(halfper); sck = 0;
            end
            #(gap); cs = 1; #(gap*2);
        end
    endtask
    initial begin
        for (f = 0; f < 40; f = f + 1) begin
            txf = {$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,
                   $urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,
                   $urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,
                   $urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,
                   $urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom,$urandom};
            txv = ($urandom % 4) != 0;
            r = $urandom % 5;
            half = 50 + ($urandom % 200);                 // SCK 1..10 MHz
            if (r == 0)      nb = 1024;
            else if (r == 1) nb = 1030;                   // preteceni
            else if (r == 2) nb = 1 + ($urandom % 1000);  // kratky ramec
            else             nb = 1024;
            spi_frame(nb, half, 100 + ($urandom % 100));
            if (f % 7 == 3) begin                         // tx_valid se meni uprostred
                cs = 0; #300; txv = 0; #300; txv = 1;
                for (i = 0; i < 20; i = i + 1) begin mosi = $urandom; #60; sck = 1; #60; sck = 0; end
                cs = 1; #500;
            end
        end
        #2000;
        if (errors == 0) $display("PASS: tb_phy_equiv (%0d taktu porovnano)", cyc);
        else $display("FAIL: tb_phy_equiv (%0d rozdilu)", errors);
        $finish;
    end
endmodule
