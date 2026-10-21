// ============================================================
// tb_n1.sv -- oprava N1 v tdc_chan (FW 0x0425, E0 audit 2026-10-20, kap. 2.1; model model/m_tdc_ld.py).
//   Chyba FW <= 0x0424: ld = idle_r & ~(ec & arm_r) je 0 po cele zmrazeni (fz 1..4). Hrana, jejiz vzorek prisel presne 5 taktu
//   za vzorkem predchozi spustene hrany, se spustila, ale dekodovala ZMRAZENY vzorek predchozi hrany (chyba az jednotky ns).
//   Projevi se jen pri spousteni hrana za hranou (want_seg, segmenty regrese) a kmitoctu ~16,7..25 MHz.
//   Kontrola (presne jako model): ec v taktu c znamena, ze hrana prosla tapem KD ve VZORKU c-1 -> pri dekodovani (fz == 4)
//   musi qd_r obsahovat q_r z taktu c-1 TEHOZ spusteni. Vzorkuje se na sestupne hrane (hodnoty jsou ustalene).
//   Kmitocty 16,7 / 18 / 20 / 22,5 MHz (+ 10 MHz kontrola) x (1 + 813 ppm), kazdy s nahodnou fazi a tisici spusteni.
//   0x0425: 0 chyb; 0x0424 musi chybovat (negativni kontrola: sim/run_one_tdc.sh proti src_fw0424_backup).
// ============================================================
`timescale 1ps/1fs
module tb_n1;
    localparam TAPS = 288, NTAP = 268, STRIDE = 1, KD = 1, DUAL = 0;
    reg clk = 0;
    always #5000 clk = ~clk;                                   // 100 MHz
    reg [33:0] tick_hi = 34'd0;
    always @(posedge clk) tick_hi <= tick_hi + 34'd1;
    wire [47:0] tick_ps = {tick_hi, 14'd0};

    reg  sig = 0;
    real P = 100000.0;                                         // perioda vstupu [ps]
    reg  run_sig = 1;
    initial begin #3333; forever begin if (run_sig) begin sig = 1; #(P/2.0); sig = 0; #(P/2.0); end else #1000; end end

    wire rise_c, rise_s, trig_ack, ts_valid, use_ro, cal_busy, cal_valid, cal_fail;
    wire [47:0] ts_ps;
    wire [31:0] d_ovf, d_peak;
    wire [15:0] d_nz, d_last, d_maxtap;
    wire [23:0] dump_q;
    wire [8:0]  code_o;
    tdc_chan #(.CAL_LOG2(12), .TAPS(TAPS), .STRIDE(STRIDE), .KD(KD), .DUAL(DUAL), .NTAP(NTAP)) dut (
        .clk(clk), .sig_raw(sig), .ro(1'b0), .tick_ps(tick_ps),
        .want(1'b0), .want_seg(1'b1), .cal_req(1'b0), .cal_abort(1'b0),
        .rise_c(rise_c), .rise_s(rise_s), .trig_ack(trig_ack), .ts_valid(ts_valid), .ts_ps(ts_ps),
        .use_ro(use_ro), .cal_busy(cal_busy), .cal_valid(cal_valid), .cal_fail(cal_fail),
        .d_ovf(d_ovf), .d_peak(d_peak), .d_nz(d_nz), .d_last(d_last), .d_maxtap(d_maxtap),
        .dump_k(10'd0), .dump_q(dump_q), .code_o(code_o));

    // ---- referencni kontrola vzorku ----
    reg [TAPS-1:0] q_prev = {TAPS{1'b1}}, q_exp = {TAPS{1'b1}};
    reg            have_exp = 0, chk_on = 0;
    integer        n_ok = 0, n_bad = 0, tot_bad = 0, tot_ok = 0, errs = 0;
    always @(negedge clk) begin
        if (dut.trig_ok) begin q_exp = q_prev; have_exp = 1; end      // vzorek c-1 tohoto spusteni
        if (dut.fz == 3'd4 && have_exp && chk_on) begin
            if (dut.u_chain.qd_r === q_exp) n_ok = n_ok + 1; else n_bad = n_bad + 1;
            have_exp = 0;
        end
        q_prev = dut.u_chain.q_r;
    end

    task run_f(input real f_mhz, input integer ncyc);
        begin
            run_sig = 0; #(64'd200_000 + ($urandom % 100000));   // nahodna faze proti hodinam
            P = 1.0e6 / (f_mhz * (1.0 + 813e-6));             // asynchronne k 100 MHz: faze vzorku prochazi (pri presnych
                                                                    // 20,0 MHz by byl zastaraly vzorek bit po bitu stejny = neodhalitelny)
            run_sig = 1;
            repeat (50) @(posedge clk);                           // dobeh rozpracovane udalosti
            n_ok = 0; n_bad = 0; chk_on = 1;
            repeat (ncyc) @(posedge clk);
            chk_on = 0;
            $display("  %6.2f MHz: dekodovano %0d, spatny vzorek %0d (%.2f %%)", f_mhz, n_ok + n_bad, n_bad,
                     (n_ok + n_bad) ? 100.0 * n_bad / (n_ok + n_bad) : 0.0);
            tot_bad = tot_bad + n_bad; tot_ok = tot_ok + n_ok;
            if (n_ok + n_bad < 100) begin errs = errs + 1; $display("  CHYBA: malo spusteni"); end
        end
    endtask

    integer r;
    initial begin
        repeat (200) @(posedge clk);
        for (r = 0; r < 3; r = r + 1) begin
            run_f(10.0, 20000);
            run_f(16.7, 20000);
            run_f(18.0, 20000);
            run_f(20.0, 20000);
            run_f(22.5, 20000);
        end
        if (tot_bad == 0 && errs == 0) $display("PASS: tb_n1 (%0d dekodovani, 0 spatnych vzorku)", tot_ok);
        else                           $display("FAIL: tb_n1 (%0d spatnych vzorku z %0d, %0d chyb testu)", tot_bad, tot_ok + tot_bad, errs);
        $finish;
    end
endmodule
