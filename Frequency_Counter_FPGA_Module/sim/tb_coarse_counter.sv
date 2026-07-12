// ============================================================
// File: sim/tb_coarse_counter.sv
// Self-checking testbench pro coarse_counter.
// Ověřuje: volný běh čítače + zapadnutí značky na edge_valid.
// ============================================================
`timescale 1ns/1ps

module tb_coarse_counter;

    localparam W = 32;

    logic         clk = 0;
    logic         rst = 1;
    logic         edge_valid = 0;
    logic [W-1:0] coarse_t;
    logic [W-1:0] count;

    int errors = 0;

    coarse_counter #(.W(W)) dut (
        .coarse_clk (clk),
        .rst        (rst),
        .edge_valid (edge_valid),
        .coarse_t   (coarse_t),
        .count      (count)
    );

    // 100 MHz
    always #5 clk = ~clk;

    // jeden takt edge_valid a kontrola, že coarse_t = count před inkrementem
    task automatic strobe_and_check();
        logic [W-1:0] expected;
        @(negedge clk);
        expected     = count;       // hodnota, kterou má coarse_t zachytit
        edge_valid   = 1'b1;
        @(posedge clk);             // tady DUT zachytí count -> coarse_t
        @(negedge clk);
        edge_valid   = 1'b0;
        if (coarse_t !== expected) begin
            $error("coarse_t=%0d, ocekavano %0d", coarse_t, expected);
            errors++;
        end else begin
            $display("OK: coarse_t zachytil %0d", coarse_t);
        end
    endtask

    initial begin
        // reset
        repeat (3) @(posedge clk);
        @(negedge clk); rst = 0;

        // počítá volně?
        repeat (10) @(posedge clk);
        if (count == 0) begin $error("citac nebezi"); errors++; end

        // tři zachycení v různých časech
        strobe_and_check();
        repeat (7)  @(posedge clk);
        strobe_and_check();
        repeat (20) @(posedge clk);
        strobe_and_check();

        if (errors == 0) $display("=== TB_COARSE_COUNTER: PASS ===");
        else             $display("=== TB_COARSE_COUNTER: FAIL (%0d chyb) ===", errors);
        $finish;
    end

endmodule
