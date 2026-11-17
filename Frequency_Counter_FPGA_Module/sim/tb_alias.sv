// ============================================================
// tb_alias.sv -- mez aliasu okna podle hradla (FW 0x0425, E0 audit 2026-10-20, kap. 2.2 a 2.3).
//   Chyba FW <= 0x0424: okno bez uzavirajici hrany se oznacilo r_dt_alias az po 250 hradlech. Pri hradle 1 s je to 250 s, ale
//   48b r_dt pretece po 171,8 s -> okno 171,8 .. 250 s se TISE zabalilo (kratsi dt = falesne vysoky kmitocet bez priznaku).
//   0x0425: vstup alias_lim (top.v: 170 pri hradle 1 s, jinak 250).
//   Test na samotnem win_recip (REGR = 0), znacky TDC rizene testbenchem:
//     alias_lim = 170: K = 169 hradel bez hrany -> alias 0;  K = 170 -> alias 1;  K = 200 -> alias 1
//     alias_lim = 250: K = 200 -> alias 0 (ostatni hradla beze zmeny proti 0x0424)
//   Negativni kontrola: -DOLD_RTL proti src_fw0424_backup (port alias_lim neexistuje, pevne 250) -> K = 170 a 200 dava 0 -> FAIL.
// ============================================================
`timescale 1ps/1fs
module tb_alias;
    reg clk = 0;
    always #5000 clk = ~clk;
    reg rise_s = 0, trig_ack = 0, ts_valid = 0, gate_tick = 0, sig_pin = 0;
    reg [47:0] ts_ps = 48'd0;
    reg [7:0]  alias_lim = 8'd170;
    wire want, want_seg, r_dt_alias, res_tgl;
    wire [25:0] r_periods;
    wire [47:0] r_dt;
    win_recip #(.REGR(0)) dut (
        .clk(clk), .sig_pin(sig_pin), .rise_s(rise_s), .trig_ack(trig_ack), .ts_valid(ts_valid), .ts_ps(ts_ps),
        .gate_tick(gate_tick), .code_i(9'd0), .hold(1'b0), .seg_a(1'b0), .seg_b(1'b0),
`ifndef OLD_RTL
        .alias_lim(alias_lim), .bub(1'b0), .cs_act(1'b0), .mask_cs(1'b0),
`endif
        .want(want), .want_seg(want_seg), .r_periods(r_periods), .r_dt(r_dt), .r_dt_alias(r_dt_alias), .res_tgl(res_tgl));

    // jedna hrana se znackou: rise + trig v tomtez taktu, ts_valid o 8 taktu pozdeji (jako tdc_chan)
    task one_edge(input [47:0] t);
        begin
            @(negedge clk); sig_pin = 1; rise_s = 1; trig_ack = 1;
            @(negedge clk); rise_s = 0; trig_ack = 0;
            @(negedge clk); sig_pin = 0;
            repeat (6) @(negedge clk);
            ts_ps = t; ts_valid = 1;
            @(negedge clk); ts_valid = 0;
            repeat (10) @(negedge clk);
        end
    endtask
    task gates(input integer k);
        integer i;
        begin
            for (i = 0; i < k; i = i + 1) begin @(negedge clk); gate_tick = 1; @(negedge clk); gate_tick = 0; repeat (8) @(negedge clk); end
        end
    endtask

    integer errs = 0;
    reg tg;
    task window(input integer k, input exp_alias, input [8*24-1:0] what);
        begin
            tg = res_tgl;
            gates(k);
            one_edge(48'd1000000 + 48'd16384 * k);
            if (res_tgl === tg) begin errs = errs + 1; $display("  CHYBA %0s: okno se neuzavrelo", what); end
            else if (r_dt_alias !== exp_alias) begin errs = errs + 1; $display("  CHYBA %0s: K=%0d alias=%0d, ocekavano %0d", what, k, r_dt_alias, exp_alias); end
            else $display("  OK %0s: K=%0d alias=%0d", what, k, r_dt_alias);
        end
    endtask

    initial begin
        repeat (10) @(negedge clk);
        gates(1); one_edge(48'd500000);                 // prvni uzavirajici hrana (zahaji pocitani, vystup se nevydava)
        alias_lim = 8'd170;
        window(169, 1'b0, "lim 170");
        window(170, 1'b1, "lim 170");
        window(200, 1'b1, "lim 170");
        alias_lim = 8'd250;
        window(200, 1'b0, "lim 250");
        if (errs == 0) $display("PASS: tb_alias"); else $display("FAIL: tb_alias (%0d chyb)", errs);
        $finish;
    end
endmodule
