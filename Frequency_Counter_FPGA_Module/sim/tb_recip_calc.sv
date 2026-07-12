// Self-checking TB pro recip_calc (+ divu_seq).
// freq_x100000 = round(periods * 1.6e14 / dt), gate_ns = dt*2.5, err=(dt==0).
`timescale 1ns/1ps
module tb_recip_calc;
    reg         clk = 1'b0;
    reg         start = 1'b0;
    reg  [31:0] periods = 32'd0;
    reg  [33:0] dt = 34'd0;
    wire [63:0] freq_x100000;
    wire [63:0] gate_ns;
    wire        valid;
    wire        err;

    integer errors = 0;

    recip_calc dut (
        .clk(clk), .start(start), .periods(periods), .dt(dt),
        .freq_x100000(freq_x100000), .gate_ns(gate_ns),
        .valid(valid), .err(err)
    );

    always #5 clk = ~clk;   // 100 ns perioda (10 MHz)

    task run_case(input [31:0] p, input [33:0] d,
                  input [63:0] exp_freq, input [63:0] exp_gate, input exp_err);
        begin
            @(negedge clk); periods = p; dt = d; start = 1'b1;
            @(negedge clk); start = 1'b0;
            // počkej na valid (max ~100 taktů kvůli 72bit děličce)
            fork : wait_v
                begin repeat (200) @(posedge clk); end
                begin wait (valid); disable wait_v; end
            join
            @(negedge clk);
            if (freq_x100000 !== exp_freq) begin
                $display("FAIL freq: p=%0d dt=%0d got=%0d exp=%0d", p, d, freq_x100000, exp_freq);
                errors = errors + 1;
            end
            if (gate_ns !== exp_gate) begin
                $display("FAIL gate: dt=%0d got=%0d exp=%0d", d, gate_ns, exp_gate);
                errors = errors + 1;
            end
            if (err !== exp_err) begin
                $display("FAIL err: dt=%0d got=%0b exp=%0b", d, err, exp_err);
                errors = errors + 1;
            end
        end
    endtask

    initial begin
        repeat (4) @(negedge clk);
        // 1) 1 perioda 1 us (400 tiku) -> 4 MHz real -> 4e11
        run_case(32'd1, 34'd400, 64'd400000000000, 64'd1000, 1'b0);
        // 2) 0,25 s okno, 1 MHz na pinu -> stejny vysledek, gate 250 ms
        run_case(32'd250000, 34'd100000000, 64'd400000000000, 64'd250000000, 1'b0);
        // 3) strop ~8 MHz na pinu (32 MHz real) -> 3.2e12
        run_case(32'd2000000, 34'd100000000, 64'd3200000000000, 64'd250000000, 1'b0);
        // 4) zaokrouhleni: 1.6e14/7 = ...142.857 -> ...143
        run_case(32'd1, 34'd7, 64'd22857142857143, 64'd17, 1'b0);
        // 5) dt==0 -> err, freq=0
        run_case(32'd123, 34'd0, 64'd0, 64'd0, 1'b1);

        if (errors == 0) $display("PASS");
        else             $display("FAIL: %0d chyb", errors);
        $finish;
    end
endmodule
