// ============================================================
// File: tdc_vernier_4phase.v
// Vernier/TDC with 4x 100MHz phases (2.5ns ticks => 400MHz).
//
// Výstupy (jen co je funkčně potřeba):
//   last_fine_code : 0..3 (fáze, která hranu zachytila první)
//   err_flags      : bit0 = finalize bez platného minima
//   event_tgl      : toggluje 1x za finalizované měření (čítání kmitočtu)
//
// Po deduplikaci: 4 fázové záchyty řeší jeden submodul `phase_capture`
// instancovaný 4x, CDC zpět do P0 + detekce hran řeší generate smyčka.
//
// TIMING (100 MHz): coarse CW=16 (ALU carry chain), gray2bin 2-stupňový,
// výběr minima pipelinován s izolovanými komparátory.
// ============================================================

module tdc_vernier_4phase #(
    parameter CW = 16,          // šířka coarse čítače (wrap ~655 us >> 40 ns okno)
    parameter TW = CW + 2       // šířka tick (coarse*4 + phase)
) (
    input  wire        clk_p0_100m,
    input  wire        clk_p2p5_100m,
    input  wire        clk_p5_100m,
    input  wire        clk_p7p5_100m,

    input  wire        sig_in,

    output reg  [1:0]  last_fine_code,
    output reg  [31:0] err_flags,
    output reg         event_tgl
);

    // -----------------------------
    // Coarse counter v P0 (Gray pro bezpečné CDC do fázových domén)
    // -----------------------------
    reg [CW-1:0] coarse_bin  = {CW{1'b0}};
    reg [CW-1:0] coarse_gray = {CW{1'b0}};

    function [CW-1:0] bin2gray;
        input [CW-1:0] b;
        begin
            bin2gray = (b >> 1) ^ b;
        end
    endfunction

    // Parallel prefix gray->bin rozdělené do 2 pipeline stupňů (každý 2 úrovně).
    function [CW-1:0] g2b_hi;          // >>8, >>4  (S_CONV1)
        input [CW-1:0] g;
        reg [CW-1:0] b;
        begin
            b = g;
            b = b ^ (b >> 8);
            b = b ^ (b >> 4);
            g2b_hi = b;
        end
    endfunction

    function [CW-1:0] g2b_lo;          // >>2, >>1  (S_CONV2)
        input [CW-1:0] g;
        reg [CW-1:0] b;
        begin
            b = g;
            b = b ^ (b >> 2);
            b = b ^ (b >> 1);
            g2b_lo = b;
        end
    endfunction

    // Čítač oddělený od gray (ALU carry chain); 1taktové zpoždění gray
    // je nepodstatné - používá se jen pro RELATIVNÍ časové značky.
    always @(posedge clk_p0_100m) begin
        coarse_bin  <= coarse_bin + 1'b1;
        coarse_gray <= bin2gray(coarse_bin);
    end

    // -----------------------------
    // 4x fázový záchyt (deduplikováno do submodulu phase_capture)
    //   fáze 0 = P0  (SYNC=0, coarse_gray je v téže doméně)
    //   fáze 1-3      (SYNC=1, coarse_gray se synchronizuje z P0)
    // -----------------------------
    wire [CW-1:0] cap_g0, cap_g1, cap_g2, cap_g3;
    wire          tgl0,   tgl1,   tgl2,   tgl3;

    phase_capture #(.CW(CW), .SYNC(0)) u_cap0 (
        .clk(clk_p0_100m),   .sig_in(sig_in), .coarse_gray(coarse_gray),
        .cap_g(cap_g0), .tgl(tgl0));
    phase_capture #(.CW(CW), .SYNC(1)) u_cap1 (
        .clk(clk_p2p5_100m), .sig_in(sig_in), .coarse_gray(coarse_gray),
        .cap_g(cap_g1), .tgl(tgl1));
    phase_capture #(.CW(CW), .SYNC(1)) u_cap2 (
        .clk(clk_p5_100m),   .sig_in(sig_in), .coarse_gray(coarse_gray),
        .cap_g(cap_g2), .tgl(tgl2));
    phase_capture #(.CW(CW), .SYNC(1)) u_cap3 (
        .clk(clk_p7p5_100m), .sig_in(sig_in), .coarse_gray(coarse_gray),
        .cap_g(cap_g3), .tgl(tgl3));

    wire [3:0]    tgl_arr = {tgl3, tgl2, tgl1, tgl0};
    wire [CW-1:0] capg_arr [0:3];
    assign capg_arr[0] = cap_g0;
    assign capg_arr[1] = cap_g1;
    assign capg_arr[2] = cap_g2;
    assign capg_arr[3] = cap_g3;

    // -----------------------------
    // CDC do P0: 2FF sync toggle + záchytové sběrnice + detekce hran (generate)
    //   (sync zpět je pro všechny 4 fáze uniformní - pro P0 neškodí,
    //    protože cap_g je mezi hranami stabilní)
    // -----------------------------
    reg [1:0]    tsync   [0:3];
    reg [CW-1:0] capg_r1 [0:3];
    reg [CW-1:0] capg_r2 [0:3];
    wire [3:0]   ev;

    genvar gi;
    generate
        for (gi = 0; gi < 4; gi = gi + 1) begin : g_phsync
            always @(posedge clk_p0_100m) begin
                tsync[gi]   <= {tsync[gi][0], tgl_arr[gi]};
                capg_r1[gi] <= capg_arr[gi];
                capg_r2[gi] <= capg_r1[gi];
            end
            assign ev[gi] = tsync[gi][1] ^ tsync[gi][0];
        end
    endgenerate

    // -----------------------------
    // Sběrné okno + pipelinovaný výběr minima (P0)
    // -----------------------------
    localparam [3:0] S_IDLE  = 4'd0;
    localparam [3:0] S_WIN   = 4'd1;
    localparam [3:0] S_CONV1 = 4'd2; // gray->bin část 1 (>>8,>>4)
    localparam [3:0] S_CONV2 = 4'd3; // gray->bin část 2 (>>2,>>1) -> ticks
    localparam [3:0] S_CMP1  = 4'd4; // izolovaná porovnání (0,1) a (2,3)
    localparam [3:0] S_RED   = 4'd5; // mux pairwise min
    localparam [3:0] S_CMP2  = 4'd6; // izolované finální porovnání
    localparam [3:0] S_MIN   = 4'd7; // mux finální fine
    localparam [3:0] S_FIN   = 4'd8; // fine + event

    reg [3:0] state = S_IDLE;
    reg [2:0] win_cnt = 3'd0;

    reg          v0, v1, v2, v3;
    reg [CW-1:0] wg0, wg1, wg2, wg3;
    reg [CW-1:0] hb0, hb1, hb2, hb3;
    reg [TW-1:0] tb0, tb1, tb2, tb3;

    reg          le_ab, le_cd;
    reg [TW-1:0] cand_a_t, cand_b_t;
    reg [1:0]    cand_a_f, cand_b_f;
    reg          cand_a_v, cand_b_v;
    reg          le_final;

    reg [1:0]    min_fine;
    reg          min_valid;

    function [TW-1:0] make_ticks;       // dokončí gray->bin (g2b_lo) -> (bin<<2)|phase
        input [CW-1:0] hb;
        input [1:0]    ph;
        reg [CW-1:0] b;
        begin
            b = g2b_lo(hb);
            make_ticks = {b, 2'b00} | {{(TW-2){1'b0}}, ph};
        end
    endfunction

    always @(posedge clk_p0_100m) begin
        err_flags <= 32'd0;

        case (state)
            S_IDLE: begin
                v0 <= 1'b0; v1 <= 1'b0; v2 <= 1'b0; v3 <= 1'b0;
                win_cnt <= 3'd0;

                if (ev[0] || ev[1] || ev[2] || ev[3]) begin
                    state   <= S_WIN;
                    win_cnt <= 3'd4; // ~40ns okno @100MHz

                    if (ev[0]) begin v0 <= 1'b1; wg0 <= capg_r2[0]; end
                    if (ev[1]) begin v1 <= 1'b1; wg1 <= capg_r2[1]; end
                    if (ev[2]) begin v2 <= 1'b1; wg2 <= capg_r2[2]; end
                    if (ev[3]) begin v3 <= 1'b1; wg3 <= capg_r2[3]; end
                end
            end

            S_WIN: begin
                if (ev[0] && !v0) begin v0 <= 1'b1; wg0 <= capg_r2[0]; end
                if (ev[1] && !v1) begin v1 <= 1'b1; wg1 <= capg_r2[1]; end
                if (ev[2] && !v2) begin v2 <= 1'b1; wg2 <= capg_r2[2]; end
                if (ev[3] && !v3) begin v3 <= 1'b1; wg3 <= capg_r2[3]; end

                if (win_cnt != 0)
                    win_cnt <= win_cnt - 3'd1;
                if (win_cnt == 3'd1)
                    state <= S_CONV1;
            end

            S_CONV1: begin
                hb0 <= g2b_hi(wg0);
                hb1 <= g2b_hi(wg1);
                hb2 <= g2b_hi(wg2);
                hb3 <= g2b_hi(wg3);
                state <= S_CONV2;
            end

            S_CONV2: begin
                tb0 <= make_ticks(hb0, 2'd0);
                tb1 <= make_ticks(hb1, 2'd1);
                tb2 <= make_ticks(hb2, 2'd2);
                tb3 <= make_ticks(hb3, 2'd3);
                state <= S_CMP1;
            end

            S_CMP1: begin
                le_ab <= (tb0 <= tb1);
                le_cd <= (tb2 <= tb3);
                state <= S_RED;
            end

            S_RED: begin
                cand_a_v <= v0 | v1;
                if (v0 && v1) begin
                    cand_a_t <= le_ab ? tb0 : tb1;
                    cand_a_f <= le_ab ? 2'd0 : 2'd1;
                end else if (v0) begin
                    cand_a_t <= tb0; cand_a_f <= 2'd0;
                end else begin
                    cand_a_t <= tb1; cand_a_f <= 2'd1;
                end

                cand_b_v <= v2 | v3;
                if (v2 && v3) begin
                    cand_b_t <= le_cd ? tb2 : tb3;
                    cand_b_f <= le_cd ? 2'd2 : 2'd3;
                end else if (v2) begin
                    cand_b_t <= tb2; cand_b_f <= 2'd2;
                end else begin
                    cand_b_t <= tb3; cand_b_f <= 2'd3;
                end

                state <= S_CMP2;
            end

            S_CMP2: begin
                le_final <= (cand_a_t <= cand_b_t);
                state    <= S_MIN;
            end

            S_MIN: begin
                if (cand_a_v && cand_b_v) begin
                    min_fine  <= le_final ? cand_a_f : cand_b_f;
                    min_valid <= 1'b1;
                end else if (cand_a_v) begin
                    min_fine <= cand_a_f; min_valid <= 1'b1;
                end else if (cand_b_v) begin
                    min_fine <= cand_b_f; min_valid <= 1'b1;
                end else begin
                    min_valid <= 1'b0;
                end
                state <= S_FIN;
            end

            S_FIN: begin
                err_flags <= {31'd0, ~min_valid};
                if (min_valid) begin
                    last_fine_code <= min_fine;
                    event_tgl      <= ~event_tgl;
                end
                state <= S_IDLE;
            end

            default: state <= S_IDLE;
        endcase
    end

    integer k;
    initial begin
        last_fine_code = 2'd0;
        err_flags      = 32'd0;
        event_tgl      = 1'b0;
        v0 = 1'b0; v1 = 1'b0; v2 = 1'b0; v3 = 1'b0;
        cand_a_v = 1'b0; cand_b_v = 1'b0; min_valid = 1'b0;
        for (k = 0; k < 4; k = k + 1) begin
            tsync[k]   = 2'b00;
            capg_r1[k] = {CW{1'b0}};
            capg_r2[k] = {CW{1'b0}};
        end
    end

endmodule


// ============================================================
// phase_capture: detekce náběžné hrany sig_in v dané hodinové doméně,
// záchyt coarse_gray + toggle. SYNC=1 synchronizuje coarse_gray z cizí
// domény (2FF) před záchytem; SYNC=0 jej bere přímo (stejná doména).
// ============================================================
module phase_capture #(
    parameter CW   = 16,
    parameter SYNC = 1
) (
    input  wire          clk,
    input  wire          sig_in,
    input  wire [CW-1:0] coarse_gray,
    output reg  [CW-1:0] cap_g,
    output reg           tgl
);
    reg [1:0]    in_sync = 2'b00;
    reg [CW-1:0] g_s1 = {CW{1'b0}};
    reg [CW-1:0] g_s2 = {CW{1'b0}};

    initial begin
        cap_g = {CW{1'b0}};
        tgl   = 1'b0;
    end

    always @(posedge clk) begin
        g_s1 <= coarse_gray;
        g_s2 <= g_s1;

        in_sync <= {in_sync[0], sig_in};
        if (in_sync == 2'b01) begin
            cap_g <= SYNC ? g_s2 : coarse_gray;
            tgl   <= ~tgl;
        end
    end
endmodule
