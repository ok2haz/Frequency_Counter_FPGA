// ============================================================
// File: coarse_counter.v
// Volně běžící čítač hrubého času v doméně coarse_clk (100 MHz).
// Na edge_valid zapadne aktuální hodnotu do coarse_t (časová značka hrany).
//
// Reciproční měřič: coarse_t * N_fine + fine_t = celková značka v jednotkách
// jemného kroku (1,25 ns @ MODE_45).
// ============================================================

module coarse_counter #(
    parameter W = 32           // šířka hrubého čítače
) (
    input  wire         coarse_clk,
    input  wire         rst,            // synchronní, active-high
    input  wire         edge_valid,     // 1-takt strobe: zachyť značku
    output reg  [W-1:0] coarse_t,       // zapadnutá značka při edge_valid
    output reg  [W-1:0] count           // volně běžící čítač (debug/expose)
);

    always @(posedge coarse_clk) begin
        if (rst) begin
            count    <= {W{1'b0}};
            coarse_t <= {W{1'b0}};
        end else begin
            count <= count + 1'b1;
            if (edge_valid)
                coarse_t <= count;       // hodnota PŘED inkrementem tohoto taktu
        end
    end

endmodule
