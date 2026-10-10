// ============================================================
// tb_dec_equiv.sv -- ekvivalence noveho dekoderu `tdc_dec` (4 registrovane stupne) s puvodni
// kombinacni funkci (8 bloku po 32, prvni nula). Nahodne vektory VCETNE bublin a nasyceni.
// Dekoder je kombinacni; vektor se drzi 2 takty (v tdc_chan jsou vzorky zmrazene >= 5 taktu).
// ============================================================
`timescale 1ps/1ps
module tb_dec_equiv;
    localparam TAPS = `TB_TAPS;
    localparam NB   = TAPS / 32;
    localparam CW   = $clog2(TAPS);

    reg clk = 0; always #5000 clk = ~clk;
    reg  [TAPS-1:0] th = {TAPS{1'b1}};   // ne 0: always @* se jinak pri prvni shodne hodnote nevyhodnoti
    wire [CW-1:0]   code;

    tdc_dec #(.TAPS(TAPS)) dut (.th(th), .code(code));

    // puvodni algoritmus (viz tdc.v do 2026-10-07): vsechny bloky plne -> nasyceni; jinak
    // nejnizsi neuplny blok a v nem nejnizsi nula
    function [CW-1:0] ref_code(input [TAPS-1:0] t);
        integer pg, j;
        reg [5:0] ld;
        reg [CW-1:0] pk;
        reg [5:0] l;
        begin
            pk = {CW{1'b1}};
            for (pg = NB - 1; pg >= 0; pg = pg - 1) begin
                l = 6'd32;
                for (j = 31; j >= 0; j = j - 1) if (!t[32 * pg + j]) l = j[5:0];
                if (l != 6'd32) pk = (pg * 32) + l;
            end
            ref_code = pk;
        end
    endfunction

    integer n, errs = 0, k, front, nb;
    reg [TAPS-1:0] v;
    initial begin
        repeat (4) @(posedge clk);
        for (n = 0; n < 40000; n = n + 1) begin
            // monotonni teplomer s frontou na nahodnem miste + 0..3 nahodnych bublin/chyb
            front = $urandom % (TAPS + 1);
            v = {TAPS{1'b0}};
            for (k = 0; k < TAPS; k = k + 1) v[k] = (k < front);
            nb = $urandom % 4;
            for (k = 0; k < nb; k = k + 1) v[$urandom % TAPS] = $urandom % 2;
            if (n % 97 == 0) v = {TAPS{1'b1}};                         // nasyceni
            if (n % 89 == 0) v = {TAPS{1'b0}};                         // nic
            th = v;
            repeat (2) @(posedge clk);
            if (code !== ref_code(v)) begin
                errs = errs + 1;
                if (errs < 5) $display("RUZNOST n=%0d front=%0d: dut=%0d ref=%0d", n, front, code, ref_code(v));
            end
        end
        if (errs == 0) $display("PASS: tb_dec_equiv (TAPS=%0d, 40000 vektoru)", TAPS);
        else $display("FAIL: tb_dec_equiv %0d rozdilu", errs);
        $finish;
    end
endmodule
