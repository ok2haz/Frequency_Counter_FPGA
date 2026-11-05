// Pouze pro mereni zdroju (gw_sh syn): obali seg_acc ve dvou kanalech + pin_cnt ve dvou kanalech + ides_pps. NENI soucasti designu.
module v3_top_synth(input clk, input pinA, input pinB, input pps, input hold, input seg_tick, input ts_valid_a, input ts_valid_b,
    input [47:0] ts_a, input [47:0] ts_b, input [39:0] xa, input [39:0] xb, input bub, input cs, input [2:0] sel,
    output [159:0] recA_o, output [159:0] recB_o, output sigA, output sigB, output [19:0] prA, output [19:0] prB,
    output [47:0] pps_ts, output pps_t, output [7:0] pps_s, output lock);
  wire tA, tB; wire [23:0] nA, nB; wire [39:0] x0A, x0B; wire [47:0] t0A, t0B, sxA, sxB; wire [55:0] stA, stB; wire [7:0] bA, bB; wire [3:0] oA, oB; wire badA, badB;
  seg_acc u_a(.clk(clk),.hold(hold),.seg_tick(seg_tick),.ts_valid(ts_valid_a),.ts_ps(ts_a),.x_abs(xa),.bub(bub),.cs_act(cs),.rec_tgl(tA),.rec_n(nA),.rec_x0(x0A),.rec_t0(t0A),.rec_sx(sxA),.rec_st(stA),.rec_bub(bA),.rec_ovl(oA),.rec_bad(badA));
  seg_acc u_b(.clk(clk),.hold(hold),.seg_tick(seg_tick),.ts_valid(ts_valid_b),.ts_ps(ts_b),.x_abs(xb),.bub(bub),.cs_act(cs),.rec_tgl(tB),.rec_n(nB),.rec_x0(x0B),.rec_t0(t0B),.rec_sx(sxB),.rec_st(stB),.rec_bub(bB),.rec_ovl(oB),.rec_bad(badB));
  assign recA_o = {tA, badA, nA, x0A[39:0], t0A[47:0], sxA[47:0], stA[55:0], bA, oA} ^ {sxA, stA[47:0]};
  assign recB_o = {tB, badB, nB, x0B[39:0], t0B[47:0], sxB[47:0], stB[55:0], bB, oB} ^ {sxB, stB[47:0]};
  pin_cnt u_pa(.pin(pinA),.clk(clk),.sel(sel),.sig_out(sigA),.seg_tick(seg_tick),.probe(prA));
  pin_cnt u_pb(.pin(pinB),.clk(clk),.sel(sel),.sig_out(sigB),.seg_tick(seg_tick),.probe(prB));
  ides_pps u_pps(.clk100(clk),.pin(pps),.clk(clk),.rec_tgl(pps_t),.rec_ts(pps_ts),.rec_seq(pps_s),.lock_o(lock));
endmodule
