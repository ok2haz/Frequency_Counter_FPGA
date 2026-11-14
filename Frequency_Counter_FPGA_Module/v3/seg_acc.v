// ============================================================
// seg_acc.v -- v3 NAVRH (audit 2026-10-20, etapa E2), NEZAPOJENO, NEOVERENO SIMULACI ANI P&R.
// SPOJITY SEGMENTOVY ZAZNAM (rozhodnuti uzivatele 2026-10-20: segmenty 25 ms). Cas je rozdelen na souvisle segmenty spolecne
// vsem kanalum (seg_tick z mrizky taktu 100 MHz, L = 2 500 000 taktu). Pro kazdy segment vznikne zaznam:
//   n      pocet casovanych hran            x0  absolutni index prvni hrany (40 b)   t0 absolutni znacka prvni hrany (48 b)
//   sx     sum(x - x0) (48 b)               st  sum(t - t0) (56 b; pri 20 Mzn/s a 25 ms typicky ~1e16 < 2^54 -> lze ALU54D)
//   flags  bubliny, znacky pri CS, kontrolni rozdil pocitani
// Hranice: po seg_tick se GAP taktu NEsci (zapisovac cte soucty primo, bez stinovych registru): ztrata ~3 znacek z ~500 000
// (zanedbatelne), prvni znacka po mezere je x0/t0 noveho segmentu. Prazdny segment (n = 0) se posila taky (souvislost casu).
// STM z toho pocita: dvoubodovy odhad (t0 sousednich segmentu), regresi (nejmensi ctverce pres teziste), fazi, ADEV/MDEV/TDEV,
// interval a fazi A-B ze soubeznych segmentu.
// ============================================================
module seg_acc #(
    parameter GAP = 16
)(
    input  wire        clk,
    input  wire        hold,        // kalibrace / neplatny TDC / zmena P: segment se oznaci neplatny
    input  wire        seg_tick,    // 1 takt: konec segmentu
    input  wire        ts_valid,
    input  wire [47:0] ts_ps,       // znacka [T_clk/16384]
    input  wire [39:0] x_abs,       // absolutni index prave casovane hrany (citac period vcetne P)
    input  wire        bub,         // bublina u znacky
    input  wire        cs_act,      // CS aktivni (diagnostika; maskovani resi tdc_chan.want)
    // zaznam uzavreneho segmentu (drzi se do dalsiho seg_tick; zapisovac ho cte v GAP taktech po rec_tgl)
    output reg         rec_tgl,
    output reg  [23:0] rec_n,
    output reg  [39:0] rec_x0,
    output reg  [47:0] rec_t0,
    output reg  [47:0] rec_sx,
    output reg  [55:0] rec_st,
    output reg  [7:0]  rec_bub,
    output reg  [3:0]  rec_ovl,
    output reg         rec_bad      // segment zasazeny hold
);
    reg [23:0] n  = 24'd0;
    reg [39:0] x0 = 40'd0;
    reg [47:0] t0 = 48'd0;
    reg [47:0] sx = 48'd0;
    reg [55:0] st = 56'd0;
    reg [7:0]  bc = 8'd0;
    reg [3:0]  oc = 4'd0;
    reg        bad = 1'b0;
    reg [4:0]  gap = 5'd0;
    // stupen 1: rozdily (registrovane), stupen 2: akumulace
    reg        v1 = 1'b0, f1 = 1'b0;
    reg [31:0] dx1 = 32'd0;
    reg [39:0] dt1 = 40'd0;
    initial begin rec_tgl = 1'b0; rec_n = 0; rec_x0 = 0; rec_t0 = 0; rec_sx = 0; rec_st = 0; rec_bub = 0; rec_ovl = 0; rec_bad = 0; end
    wire take = ts_valid & (gap == 5'd0);
    always @(posedge clk) begin
        v1 <= 1'b0; f1 <= 1'b0;
        if (seg_tick) begin
            // posledni znacka segmentu muze byt ve stupni 2 prave v taktu seg_tick -> pricist do zaznamu (jinak by n nesedelo se soucty)
            rec_n <= n; rec_x0 <= x0; rec_t0 <= t0;
            rec_sx <= sx + ((v1 && !f1) ? {16'd0, dx1} : 48'd0);
            rec_st <= st + ((v1 && !f1) ? {16'd0, dt1} : 56'd0);
            rec_bub <= bc; rec_ovl <= oc; rec_bad <= bad | hold;
            rec_tgl <= ~rec_tgl;
            n <= 24'd0; sx <= 48'd0; st <= 56'd0; bc <= 8'd0; oc <= 4'd0; bad <= hold;
            gap <= GAP[4:0];
        end else begin
            if (gap != 5'd0) gap <= gap - 5'd1;
            if (hold) bad <= 1'b1;
            if (take) begin
                if (n == 24'd0) begin x0 <= x_abs; t0 <= ts_ps; f1 <= 1'b1; end
                else begin dx1 <= x_abs[31:0] - x0[31:0]; dt1 <= ts_ps[39:0] - t0[39:0]; end
                v1 <= 1'b1;
                if (n != 24'hFFFFFF) n <= n + 24'd1;
                if (bub && bc != 8'hFF) bc <= bc + 8'd1;
                if (cs_act && oc != 4'hF) oc <= oc + 4'd1;
            end
            if (v1 && !f1) begin                   // prvni znacka ma rozdil 0
                sx <= sx + {16'd0, dx1};
                st <= st + {16'd0, dt1};
            end
        end
    end
endmodule
