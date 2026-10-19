// ============================================================
// tb_pps.sv -- self-checking test modulu pps_cap (FW 0x0424, STATUS #305).
// Overuje: (1) cap = hodnota citace v okamziku hrany + konstantni zpozdeni synchronizatoru
// (2 nebo 3 takty podle faze hrany vuci hodinam, nikdy jinak), (2) seq roste o 1 na kazdou
// nabeznou hranu, (3) sestupna hrana ani dlouhy puls nic nezachyti, (4) rozdil dvou hran
// odpovida casu mezi nimi s chybou nejvys 1 takt (kvantizace 10 ns).
// Spusteni: .\sim\run.ps1 pps
// ============================================================
`timescale 1ns/1ps
module tb_pps;
    reg clk = 1'b0;
    always #5 clk = ~clk;                  // 100 MHz

    reg [31:0] tick = 32'd0;
    always @(posedge clk) tick <= tick + 32'd1;

    reg pin = 1'b0;
    wire [31:0] cap;
    wire [7:0]  seq;
    pps_cap dut (.clk(clk), .pin(pin), .tick(tick), .cap(cap), .seq(seq));

    integer errors = 0;
    integer k;
    real    t_edge, t_prev;
    reg [31:0] cap_prev;
    reg [31:0] tick_at_edge;

    // Hrana v libovolne fazi vuci hodinam: zpozdeni od hrany do zachyceni musi byt 2..3 takty.
    task pulse(input real phase_ns, input real width_ns);
        begin
            #(phase_ns);
            tick_at_edge = tick;           // citac v okamziku hrany (hodnota platna od posledni nabezne hrany clk)
            t_edge = $realtime;
            pin = 1'b1;
            #(width_ns);
            pin = 1'b0;
        end
    endtask

    initial begin
        #100;
        cap_prev = 32'd0; t_prev = 0.0;
        for (k = 0; k < 40; k = k + 1) begin
            // hrany ~1 us od sebe (zkracena "sekunda"), faze rozprostrena pres celou periodu hodin
            pulse(1000.0 + (k * 0.37) - $floor(k * 0.37 / 10.0) * 10.0, 200.0);
            #100;
            if (seq !== (k + 1) % 256) begin
                $display("CHYBA: seq=%0d, cekano %0d", seq, (k + 1) % 256); errors = errors + 1;
            end
            if (!((cap - tick_at_edge) >= 32'd2 && (cap - tick_at_edge) <= 32'd3)) begin
                $display("CHYBA: zpozdeni zachyceni %0d taktu (cekano 2..3)", cap - tick_at_edge); errors = errors + 1;
            end
            if (k > 0) begin
                // rozdil po sobe jdoucich hran vs. skutecny cas: chyba <= 1 takt
                real dt_true; integer d;
                dt_true = (t_edge - t_prev) / 10.0;
                d = cap - cap_prev;
                if ((d - dt_true) > 1.01 || (dt_true - d) > 1.01) begin
                    $display("CHYBA: rozdil %0d taktu, skutecne %f", d, dt_true); errors = errors + 1;
                end
            end
            cap_prev = cap; t_prev = t_edge;
        end
        // sestupna hrana / drzena uroven: nic noveho
        begin : no_fall
            reg [7:0] s0; s0 = seq;
            #1000; pin = 1'b1; #5000;      // dlouhy puls = jedna hrana
            if (seq !== s0 + 8'd1) begin $display("CHYBA: dlouhy puls nezapocten jednou"); errors = errors + 1; end
            pin = 1'b0; #2000;
            if (seq !== s0 + 8'd1) begin $display("CHYBA: sestupna hrana zapoctena"); errors = errors + 1; end
        end
        if (errors == 0) $display("PASS tb_pps");
        else             $display("FAIL tb_pps: %0d chyb", errors);
        $finish;
    end
endmodule
