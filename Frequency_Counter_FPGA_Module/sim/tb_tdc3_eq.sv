// ============================================================
// tb_tdc3_eq.sv -- 2026-10-25 (plocha, ts_hq bez vyberu): tdc_chan3 (v3 kopie v v3_core.v) proti PUVODNIMU tdc_chan z tdc.v
// takt po taktu, VSECHNY vystupy, konfigurace jako u_tdca / u_tdcb v top_v3 (TAPS 288, NTAP 268, STRIDE 1, KD 1, DUAL 0,
// BANKS 1, TSPIPE 1, EXT_TR 0, tabulku zapisuje STM pres lw_*).
// Proc zvlast: ts_hq se v kopii plni v tv_y z tr1 a v tv_z se jen pri amt = 1 (lut_q3[15] = 0) prepise. amt = 1 maji jen polozky
// s x14 = 0 (nejnizsi kody, pred kterymi v histogramu skoro nic neni) -- testy topv3* je mericimi hranami nevyvolaji, takze
// mutaci "ts_hq pri amt 1 bez prepisu" (horni cast znacky o takt = 16384 jednotek starsi) nechytily. Tady ma tabulka amt = 1
// na tretine kodu (dve tabulky s jinym rozlozenim, druha po prekalibraci do druhe banky).
// Retez (tdc_chain z tdc.v, beze zmeny) predmetem testu neni: modely ALU maji zpozdeni nahodne na instanci, proto DUT dostane
// teplomer a ec z retezu reference (force) -- obe kopie logiky kanalu vidi tytez vzorky. Hrany vstupu v nahodne fazi (ps),
// rozestupy 2..33 taktu (i hrany ve zmrazeni, ktere se nespusti), want / want_seg nahodne ve 2. faze.
// Kontrola: kazdy takt vsechny vystupy shodne; pokryti >= 300 znacek s amt = 1 a >= 300 s amt = 2, z toho >= 20 se znackou
// presne 5 taktu po predchozi (nejkratsi rozestup znacek tv_y -> tv_y; predchozi znacka se z ts_hq cte v tv_w = tv_y + 2).
// ============================================================
`timescale 1ps/1ps
`include "tb_clk.vh"
module tb_tdc3_eq;
    localparam CAL_LOG2 = 8;
    localparam TAPS = 288, NTAP = 268, STRIDE = 1, KD = 1;   // top_v3: TDC_STRIDE = 1
    localparam NC = 512;                                       // 2^clog2(288)
    localparam NE = 2500;                                      // hran v kazde fazi mereni
    reg clk = 0; always #(`TB_THALF_PS) clk = ~clk;
    // tick_hi (34 b) pretece behem 1. faze mereni
    reg [47:0] tick_ps = 48'd0 - 48'd16384 * 48'd9000;
    always @(posedge clk) tick_ps <= tick_ps + 48'd16384;
    reg ro = 0;
    integer seed = 20261025;
    always begin #(1800 + ({$random(seed)} % 500)); ro = ~ro; end   // kalibracni zdroj (nesouhlasny s clk)
    reg sig = 0, want = 0, want_seg = 0, cal_req = 0, lw_en = 0, lw_commit = 0, bank_sw = 0;
    reg [9:0]  lw_a = 10'd0, dump_k = 10'd0;
    reg [13:0] lw_neg = 14'd0;

    wire        rc_d, rs_d, ta_d, tv_d, ur_d, cb_d, cv_d, cf_d, bu_d, cw_d, bo_d;
    wire        rc_r, rs_r, ta_r, tv_r, ur_r, cb_r, cv_r, cf_r, bu_r, cw_r, bo_r;
    wire [47:0] ts_d, ts_r;
    wire [31:0] ov_d, pk_d, ov_r, pk_r;
    wire [15:0] nz_d, la_d, mt_d, nz_r, la_r, mt_r;
    wire [23:0] dq_d, dq_r;
    wire [8:0]  co_d, co_r;
    tdc_chan3 #(.EXT_TR(0), .CAL_LOG2(CAL_LOG2), .TAPS(TAPS), .STRIDE(STRIDE), .KD(KD), .DUAL(0), .NTAP(NTAP), .AUTOCAL(0),
                .BANKS(1), .TSPIPE(1)) u_dut (
        .clk(clk), .sig_raw(sig), .ro(ro), .cal_pin(1'b0), .tick_ps(tick_ps), .tr1_ext(34'd0), .tr2_ext(34'd0), .want(want), .want_seg(want_seg),
        .cal_req(cal_req), .cal_abort(1'b0), .lw_en(lw_en), .lw_a(lw_a), .lw_neg(lw_neg), .lw_commit(lw_commit), .cal_wait(cw_d),
        .rise_c(rc_d), .rise_s(rs_d), .trig_ack(ta_d), .ts_valid(tv_d), .ts_ps(ts_d), .use_ro(ur_d), .cal_busy(cb_d),
        .cal_valid(cv_d), .cal_fail(cf_d), .d_ovf(ov_d), .d_peak(pk_d), .d_nz(nz_d), .d_last(la_d), .d_maxtap(mt_d),
        .dump_k(dump_k), .dump_q(dq_d), .code_o(co_d), .bub_o(bu_d), .bank_sw(bank_sw), .bank_o(bo_d));
    tdc_chan  #(.EXT_TR(0), .CAL_LOG2(CAL_LOG2), .TAPS(TAPS), .STRIDE(STRIDE), .KD(KD), .DUAL(0), .NTAP(NTAP), .AUTOCAL(0),
                .BANKS(1), .TSPIPE(1)) u_ref (
        .clk(clk), .sig_raw(sig), .ro(ro), .tick_ps(tick_ps), .tr1_ext(34'd0), .tr2_ext(34'd0), .want(want), .want_seg(want_seg),
        .cal_req(cal_req), .cal_abort(1'b0), .lw_en(lw_en), .lw_a(lw_a), .lw_neg(lw_neg), .lw_commit(lw_commit), .cal_wait(cw_r),
        .rise_c(rc_r), .rise_s(rs_r), .trig_ack(ta_r), .ts_valid(tv_r), .ts_ps(ts_r), .use_ro(ur_r), .cal_busy(cb_r),
        .cal_valid(cv_r), .cal_fail(cf_r), .d_ovf(ov_r), .d_peak(pk_r), .d_nz(nz_r), .d_last(la_r), .d_maxtap(mt_r),
        .dump_k(dump_k), .dump_q(dq_r), .code_o(co_r), .bub_o(bu_r), .bank_sw(bank_sw), .bank_o(bo_r));
    initial begin
        force u_dut.th_r  = u_ref.th_r;
        force u_dut.th_f  = u_ref.th_f;
        force u_dut.ec    = u_ref.ec;
        force u_dut.src_l = u_ref.src_l;
    end
    wire [229:0] o_d = {rc_d, rs_d, ta_d, tv_d, ts_d, ur_d, cb_d, cv_d, cf_d, ov_d, pk_d, nz_d, la_d, mt_d, dq_d, co_d, bu_d, cw_d, bo_d};
    wire [229:0] o_r = {rc_r, rs_r, ta_r, tv_r, ts_r, ur_r, cb_r, cv_r, cf_r, ov_r, pk_r, nz_r, la_r, mt_r, dq_r, co_r, bu_r, cw_r, bo_r};

    // ---------------- kontrola kazdy takt + pokryti ----------------
    integer errors = 0, ncyc = 0, n_ts = 0, n_a1 = 0, n_a2 = 0, n_a1_5 = 0, n_a2_5 = 0, n_bank = 0, gcnt = 1000, y_gap = 1000;
    reg     a_pend = 1'b0, prev_bank = 1'b0;
    always @(negedge clk) begin
        ncyc = ncyc + 1;
        if (o_d !== o_r) begin
            errors = errors + 1;
            if (errors < 12)
                $display("  CHYBA t = %0t: vystupy tdc_chan3 != tdc_chan (ts_valid %b/%b ts_ps %h/%h code %0d/%0d cal_valid %b/%b)",
                         $time, tv_d, tv_r, ts_d, ts_r, co_d, co_r, cv_d, cv_r);
        end
        // amt polozky, ze ktere se sklada znacka: lut_q3 v taktu tv_z (tv_y + 1), znacka vychazi s ts_valid (tv_z + 2)
        gcnt = gcnt + 1;
        if (u_ref.tv_y) begin y_gap = gcnt; gcnt = 0; end
        if (u_ref.tv_z) begin
            a_pend = ~u_ref.lut_q3[15];
            // rozestup 5 taktu od predchozi znacky (nejkratsi mozny: zmrazeni fz 1..4 + spusteni)
            if (y_gap == 5) begin if (a_pend) n_a1_5 = n_a1_5 + 1; else n_a2_5 = n_a2_5 + 1; end
        end
        if (tv_r) begin
            n_ts = n_ts + 1;
            if (a_pend) n_a1 = n_a1 + 1; else n_a2 = n_a2 + 1;
        end
        if (bo_r != prev_bank) n_bank = n_bank + 1;
        prev_bank = bo_r;
    end

    // ---------------- tabulky: amt = 1 (neg = 0) na tretine kodu ----------------
    function [13:0] negv(input integer a, input integer t);
        integer x;
        begin
            x = (a * 61 + 37 + 1000 * t) % 16384;
            if (((a + t) % 3) == 0 || x == 0) x = 0;               // x14 = 0 -> neg = 0 -> amt = 1 (lw_amt v kanalu)
            negv = 14'd0 - x[13:0];
        end
    endfunction
    integer a, e, g, hw, t;
    task calib(input integer tab);
        begin
            @(posedge clk); cal_req <= 1; @(posedge clk); cal_req <= 0;
            wait (cw_r === 1'b1);
            repeat (20) @(posedge clk);
            for (a = 0; a < NC; a = a + 1) begin
                lw_a <= a; lw_neg <= negv(a, tab); lw_en <= 1; @(posedge clk); lw_en <= 0; @(posedge clk);
            end
            lw_commit <= 1; @(posedge clk); lw_commit <= 0;
            repeat (37) @(posedge clk);
            bank_sw <= 1; @(posedge clk); bank_sw <= 0;
            repeat (10) @(posedge clk);
        end
    endtask
    task edges(input integer n, input integer rnd_want);
        begin
            for (e = 0; e < n; e = e + 1) begin
                // rozestup 2..33 taktu + nahodna faze (ps); nejcasteji 5..7 taktu (nejkratsi spusteni za sebou)
                g  = ({$random(seed)} % 4 == 0) ? (2 + {$random(seed)} % 32) : (5 + {$random(seed)} % 3);
                hw = 3000 + {$random(seed)} % 9000;
                #(g * 2 * `TB_THALF_PS - hw + ({$random(seed)} % (2 * `TB_THALF_PS)) - `TB_THALF_PS);
                if (rnd_want) begin want = {$random(seed)} & 1; want_seg = ({$random(seed)} % 4) != 0; end
                sig = 1; #(hw); sig = 0;
            end
        end
    endtask
    initial begin
        repeat (50) @(posedge clk);
        calib(0);                                   // tabulka 0 do banky 1 -> aktivni
        want_seg = 1;
        edges(NE, 0);
        want = 0; want_seg = 0;
        repeat (40) @(posedge clk);
        calib(1);                                   // prekalibrace (v3: cal_valid plati dal), tabulka 1 do banky 0
        edges(NE, 1);
        repeat (40) @(posedge clk);
        $display("taktu %0d, znacek %0d: amt 1 %0d (z toho 5 taktu po predchozi %0d), amt 2 %0d (5 taktu po predchozi %0d), prepnuti banky %0d",
                 ncyc, n_ts, n_a1, n_a1_5, n_a2, n_a2_5, n_bank);
        if (n_a1 < 300 || n_a2 < 300 || n_a1_5 < 20 || n_a2_5 < 20 || n_bank < 2) begin
            errors = errors + 1; $display("  CHYBA: nedostatecne pokryti (amt 1 / amt 2 / rozestup 5 taktu / banky)");
        end
        if (errors == 0) $display("PASS: tb_tdc3_eq"); else $display("FAIL: tb_tdc3_eq (%0d chyb)", errors);
        $finish;
    end
    initial begin #(64'd200_000_000_000); $display("FAIL: tb_tdc3_eq timeout"); $finish; end
endmodule
