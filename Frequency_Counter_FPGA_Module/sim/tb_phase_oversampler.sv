// Self-checking TB pro phase_oversampler: 4 fáze 100 MHz (90°), test signál,
// ověř počet detekovaných náběžných hran.
`timescale 1ns/1ps
module tb_phase_oversampler;
    reg clk_p0 = 1'b0, clk_p2p5 = 1'b0, clk_p5 = 1'b0, clk_p7p5 = 1'b0;
    reg sig = 1'b0;
    wire sig_rise;
    wire [1:0] fine;
    integer rise_cnt = 0;

    phase_oversampler dut (
        .clk_p0(clk_p0), .clk_p2p5(clk_p2p5), .clk_p5(clk_p5), .clk_p7p5(clk_p7p5),
        .sig_in(sig), .sig_rise(sig_rise), .fine(fine)
    );

    // 100 MHz (perioda 10 ns), posuny 0 / 2,5 / 5 / 7,5 ns
    always #5 clk_p0 = ~clk_p0;
    initial begin #2.5; forever #5 clk_p2p5 = ~clk_p2p5; end
    initial begin #5.0; forever #5 clk_p5   = ~clk_p5;   end
    initial begin #7.5; forever #5 clk_p7p5 = ~clk_p7p5; end

    always @(posedge clk_p0) if (sig_rise) rise_cnt = rise_cnt + 1;

    initial begin
        sig = 1'b0;
        #103;
        // 20 hran @ 1 MHz (500 ns H / 500 ns L)
        repeat (20) begin sig = 1'b1; #500; sig = 1'b0; #500; end
        #300;
        $display("rise_cnt=%0d (ocekavano ~20)", rise_cnt);
        if (rise_cnt >= 19 && rise_cnt <= 21) $display("PASS");
        else                                  $display("FAIL");
        $finish;
    end
endmodule
