// ============================================================
// tb_calpin.sv -- 2026-10-31 (FW 0x0509, STATUS #347 krok 1): kalibrace TDC ze VSTUPNIHO PINU na urovni kanalu tdc_chan3
// (konfigurace jako u_tdca / u_tdcb v top_v3: TAPS 288, NTAP 268, STRIDE 1, KD 1, BANKS 1, TSPIPE 1, tabulku zapisuje STM).
// Rychly test (sekundy) misto topv3pin (cely top, desitky minut): sber z pinu (cal_pin = 1) i z ring oscilatoru (0).
// Kontroly: (1) pri sberu krmi retez pin, resp. ring oscilator (sig_eff), (2) sber skonci (cal_wait) a histogram ma
// 2^CAL_LOG2 udalosti, (3) z pinu se nesoudelnym kmitoctem (11,603104 MHz, krok faze ~ zlaty rez) pokryje retez jako
// z ring oscilatoru (pocet neprazdnych kodu), (4) zapis tabulky + potvrzeni -> cal_valid, (5) soudelny signal (10 MHz)
// da histogram jen v par kodech (proto ho STM pred zapisem odmitne, fpga_v3_calpin_check).
// ============================================================
`timescale 1ps/1ps
`include "tb_clk.vh"
module tb_calpin;
    localparam CAL_LOG2 = 12;
    localparam TAPS = 288, NTAP = 268, STRIDE = 1, KD = 1;
    localparam NC = 512;
    reg clk = 0; always #(`TB_THALF_PS) clk = ~clk;
    reg [47:0] tick_ps = 48'd0;
    always @(posedge clk) tick_ps <= tick_ps + 48'd16384;
    reg ro = 0;
    integer seed = 20261031;
    // kalibracni zdroj (nesoudelny s clk, nahodna pulperioda 30..37 ns jako ring oscilator s delicem LFSR; s periodou ~4 ns by
    // posledni hrana v retezu byla vzdy do ~2 ns a histogram by pokryl jen kus retezu)
    always begin #(30000 + ({$random(seed)} % 7000)); ro = ~ro; end
    // vstupni pin: perioda per_ps (meni test), strida ~50 %
    real per_ps = 86183.0;
    reg  sig = 0;
    always begin #(per_ps / 2.0); sig = ~sig; end
    reg cal_pin = 0, cal_req = 0, lw_en = 0, lw_commit = 0, bank_sw = 0;
    reg [9:0]  lw_a = 10'd0, dump_k = 10'd0;
    reg [13:0] lw_neg = 14'd0;
    wire        rc, rs, ta, tv, ur, cb, cv, cf, bu, cw, bo;
    wire [47:0] ts;
    wire [31:0] ov, pk;
    wire [15:0] nz, la, mt;
    wire [23:0] dq;
    wire [8:0]  co;
    tdc_chan3 #(.EXT_TR(0), .CAL_LOG2(CAL_LOG2), .TAPS(TAPS), .STRIDE(STRIDE), .KD(KD), .DUAL(0), .NTAP(NTAP), .AUTOCAL(0),
                .BANKS(1), .TSPIPE(1)) u_dut (
        .clk(clk), .sig_raw(sig), .ro(ro), .cal_pin(cal_pin), .tick_ps(tick_ps), .tr1_ext(34'd0), .tr2_ext(34'd0), .want(1'b0),
        .want_seg(1'b0), .cal_req(cal_req), .cal_abort(1'b0), .lw_en(lw_en), .lw_a(lw_a), .lw_neg(lw_neg), .lw_commit(lw_commit),
        .cal_wait(cw), .rise_c(rc), .rise_s(rs), .trig_ack(ta), .ts_valid(tv), .ts_ps(ts), .use_ro(ur), .cal_busy(cb),
        .cal_valid(cv), .cal_fail(cf), .d_ovf(ov), .d_peak(pk), .d_nz(nz), .d_last(la), .d_maxtap(mt),
        .dump_k(dump_k), .dump_q(dq), .code_o(co), .bub_o(bu), .bank_sw(bank_sw), .bank_o(bo));

    integer errors = 0, n_eff = 0, n_sig = 0, n_ro = 0;
    task chk(input cond, input [8*64-1:0] what);
        begin if (!cond) begin errors = errors + 1; $display("  CHYBA: %0s", what); end end endtask
    // (1) zdroj retezu pri sberu: pocet nabeznych hran vstupu retezu (sig_eff) proti hranam pinu a ring oscilatoru
    // (pocitani misto okamzite rovnosti: hrana zdroje ve stejnem casovem kroku jako hodiny by dala zavod v simulatoru)
    always @(posedge u_dut.sig_eff) if (u_dut.s_col) n_eff = n_eff + 1;
    always @(posedge sig)           if (u_dut.s_col) n_sig = n_sig + 1;
    always @(posedge ro)            if (u_dut.s_col) n_ro  = n_ro + 1;

    integer a, sum, nzc, t;
    task collect(input pin, output integer nz_out);
        begin
            cal_pin = pin; n_eff = 0; n_sig = 0; n_ro = 0;
            @(posedge clk); cal_req <= 1; @(posedge clk); cal_req <= 0;
            t = 0;
            while (cw !== 1'b1 && t < 2000000) begin @(posedge clk); t = t + 1; end
            chk(cw === 1'b1, "sber skoncil (cal_wait)");
            chk(pin ? (n_eff >= n_sig - 2 && n_eff <= n_sig + 2 && n_sig > 1000)
                    : (n_eff >= n_ro - 2 && n_eff <= n_ro + 2 && n_ro > 1000), "pri sberu krmi retez spravny zdroj (sig_eff)");
            sum = 0; nzc = 0;
            for (a = 0; a < NC; a = a + 1) begin sum = sum + u_dut.hist[a]; if (u_dut.hist[a] != 0) nzc = nzc + 1; end
            chk(sum == (1 << CAL_LOG2), "histogram ma 2^CAL_LOG2 udalosti");
            nz_out = nzc;
            $display("  sber z %s: %0d taktu, udalosti %0d, neprazdnych kodu %0d, hran vstupu retezu %0d (pin %0d, RO %0d)",
                     pin ? "PINU" : "ring oscilatoru", t, sum, nzc, n_eff, n_sig, n_ro);
        end
    endtask
    task write_lut;
        begin
            repeat (20) @(posedge clk);
            for (a = 0; a < NC; a = a + 1) begin
                lw_a <= a; lw_neg <= 14'd0 - ((a * 61 + 37) % 16384); lw_en <= 1; @(posedge clk); lw_en <= 0; @(posedge clk);
            end
            lw_commit <= 1; @(posedge clk); lw_commit <= 0;
            repeat (37) @(posedge clk);
            bank_sw <= 1; @(posedge clk); bank_sw <= 0;
            repeat (10) @(posedge clk);
            chk(cv === 1'b1 && cw === 1'b0, "po zapisu a potvrzeni tabulka platna, kanal neceka");
        end
    endtask
    integer nz_ro, nz_pin, nz_com;
    initial begin
        repeat (50) @(posedge clk);
        collect(1'b0, nz_ro);  write_lut;            // nabeh: ring oscilator
        collect(1'b1, nz_pin); write_lut;            // z pinu, 11,603104 MHz
        chk(nz_pin * 10 >= nz_ro * 9, "pin (nesoudelny) pokryje retez jako ring oscilator (>= 90 % kodu)");
        per_ps = 100000.0;                           // 10 MHz = soudelny se 100 MHz
        repeat (100) @(posedge clk);
        collect(1'b1, nz_com); write_lut;
        chk(nz_com * 4 < nz_ro, "soudelny signal (10 MHz): jen par kodu (STM ho odmitne)");
        per_ps = 86183.0;
        collect(1'b0, nz_ro);  write_lut;            // zpet na ring oscilator
        if (errors == 0) $display("PASS: tb_calpin"); else $display("FAIL: tb_calpin (%0d chyb)", errors);
        $finish;
    end
    initial begin #(64'd500_000_000_000); $display("FAIL: tb_calpin timeout"); $finish; end
endmodule
