// ============================================================
// tb_ring_inv.vh -- 2026-10-25 (krok: kruh v 16 SDPB): invariant zapisovace kruhu v3_ring, vklada se do testbenche s
// instanci top_v3 "dut" a hodinami clk100 (tb_top_v3.sv, tb_hold_inv.sv). Pri chybe vola chk(0, ...) testbenche.
// Drivejsi zapisovac cetl slovo k zaznamu (k = 8 * kanal + slovo) ve stupni 1 v taktu E+7+k (E = takt seg_tick) a
// FLAGS (inv = hold, f_err) se meni zive. Kontroluje se v KAZDE hranici segmentu:
//   * takt zapisu: wsA (slova A a slova 1..7 B) prave v E+7, wsB0 (slovo 0 B) prave v E+15, clear prave v E+26,
//   * data: kazde zapsane slovo = slovo, ktere by drivejsi zapisovac vybral v E+7+k (nezavisle sestavene podle
//     formatu zaznamu, kap. 3.2: 0 {FLAGS, CH, SEG_IDX}, 1 {X0[7:0], N}, 2 X0[39:8], 3 T0[31:0], 4 {SX[15:0], T0[47:32]},
//     5 SX[47:16], 6 ST[31:0], 7 {0, ST[55:32]}); X0 a T0 jako drive z vnitrku seg_chan3 (first ? 0 : x0 / t0), ne
//     z vystupu o_x0 / o_t0 (ty od teze upravy masku first nemaji).
// ============================================================
    integer    ri_e = -1;                       // takty od seg_tick (0 = takt E), -1 = zatim zadna hranice
    reg [31:0] ri_exp [0:15];                   // slovo k podle drivejsiho zapisovace (vzorek v E+7+k)
    reg [31:0] ri_got [0:15];                   // data zapsana do bloku k
    reg [15:0] ri_wr = 16'd0;                   // ktere bloky se v teto hranici zapsaly
    integer    ri_k, ri_nseg = 0;
    function [31:0] ri_word(input integer kk);
        reg [255:0] d; reg [39:0] x0; reg [47:0] t0;
        begin
            if (kk < 8) begin
                x0 = dut.u_sa.first ? 40'd0 : dut.u_sa.x0; t0 = dut.u_sa.first ? 48'd0 : dut.u_sa.t0;
                d = {{8'd0, dut.u_ring.a_st[55:32]}, dut.u_ring.a_st[31:0], dut.u_ring.a_sx[47:16],
                     {dut.u_ring.a_sx[15:0], t0[47:32]}, t0[31:0], x0[39:8],
                     {x0[7:0], dut.u_ring.a_n}, {dut.u_ring.a_fl, 1'b0, dut.u_ring.a_p, 4'd0, dut.u_ring.seg_idx[15:0]}};
            end else begin
                x0 = dut.u_sb.first ? 40'd0 : dut.u_sb.x0; t0 = dut.u_sb.first ? 48'd0 : dut.u_sb.t0;
                d = {{8'd0, dut.u_ring.b_st[55:32]}, dut.u_ring.b_st[31:0], dut.u_ring.b_sx[47:16],
                     {dut.u_ring.b_sx[15:0], t0[47:32]}, t0[31:0], x0[39:8],
                     {x0[7:0], dut.u_ring.b_n}, {dut.u_ring.b_fl, 1'b0, dut.u_ring.b_p, 4'd1, dut.u_ring.seg_idx[15:0]}};
            end
            ri_word = d[32 * (kk % 8) +: 32];
        end
    endfunction
    always @(posedge clk100) begin
        if (dut.u_ring.seg_tick) begin ri_e = 0; ri_wr = 16'd0; end
        else if (ri_e >= 0 && ri_e < 100) ri_e = ri_e + 1;
        if (ri_e >= 7 && ri_e <= 22) ri_exp[ri_e - 7] = ri_word(ri_e - 7);
        if (dut.u_ring.wsA) begin
            if (ri_e != 7) chk(1'b0, "kruh: zapis slov A / B1..7 mimo takt E+7");
            for (ri_k = 0; ri_k < 16; ri_k = ri_k + 1) if (ri_k != 8) begin
                ri_got[ri_k] = (ri_k < 8) ? dut.u_ring.dA[32 * ri_k +: 32] : dut.u_ring.dB[32 * (ri_k - 8) +: 32];
                ri_wr[ri_k] = 1'b1;
            end
        end
        if (dut.u_ring.wsB0) begin
            if (ri_e != 15) chk(1'b0, "kruh: zapis slova 0 B mimo takt E+15");
            ri_got[8] = dut.u_ring.dB[31:0]; ri_wr[8] = 1'b1;
        end
        if (dut.u_ring.clear && ri_e != 26) chk(1'b0, "kruh: clear mimo takt E+26");
        if (ri_e == 26) begin
            ri_nseg = ri_nseg + 1;
            if (ri_wr != 16'hFFFF) chk(1'b0, "kruh: nezapsany blok zaznamu");
            for (ri_k = 0; ri_k < 16; ri_k = ri_k + 1)
                if (ri_got[ri_k] !== ri_exp[ri_k]) begin
                    chk(1'b0, "kruh: zapsane slovo != vyber drivejsiho zapisovace v E+7+k");
                    $display("  kruh: slovo %0d zapsano %h, ocekavano %h (t = %0t)", ri_k, ri_got[ri_k], ri_exp[ri_k], $time);
                end
        end
    end
