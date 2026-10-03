// ============================================================
// tb_tdc.sv -- self-checking test: 2 kanaly TDC + okno (win_recip)
//   1. samokalibrace obou kanalu spolecnym ring oscilatorem
//      (CAL_LOG2 zmenseno a kruh zpomalen pro rychlost simulace)
//   2. mereni znameho asynchronniho signalu: dt okna = N * T presne;
//      chyba dt (ps) musi byt mala (PRESNOST) a kanaly A/B se musi shodovat
//      (SYMETRIE) -- kazdy ma VLASTNI nahodna zpozdeni tapu (gowin_models.v)
//   3. kanal B dostava signal s jinym fazovym posunem (jina poloha hrany
//      vuci taktu) -- vysledek se nesmi zmenit
// ============================================================
`timescale 1ps/1ps
module tb_tdc;
    localparam CAL_LOG2 = 15;
    localparam TSIG     = 31337;                 // perioda signalu [ps] (~31,9 MHz)
    localparam B_SKEW   = 3333;                  // B je opozdeno vuci A [ps]

    reg clk = 0;
    always #5000 clk = ~clk;                     // 100 MHz

    reg [47:0] tick_ps = 48'd0;
    always @(posedge clk) tick_ps <= tick_ps + 48'd16384;   // jednotka T_clk/16384

    // hradlo: puls kazdych 2500 taktu (25 us)
    reg [11:0] gcnt = 0; reg gate_tick = 0;
    always @(posedge clk) begin
        gate_tick <= 1'b0;
        if (gcnt == 12'd2499) begin gcnt <= 0; gate_tick <= 1'b1; end
        else gcnt <= gcnt + 1;
    end

    reg  sig_a = 0, sig_b = 0;
    wire ro, use_ro_a, use_ro_b;
    ring_osc #(.DIV_LOG2(2)) u_ro (.en(use_ro_a | use_ro_b), .out(ro));

    // spolecny timeout kalibrace (jako top.v)
    reg [27:0] tmo = 0; reg abort = 0;
    always @(posedge clk) begin
        abort <= 0;
        if (use_ro_a | use_ro_b) begin tmo <= tmo + 1; if (&tmo) abort <= 1; end else tmo <= 0;
    end

    reg cal_req = 0;
    wire rise_a, trig_a, tsv_a, want_a, rise_b, trig_b, tsv_b, want_b;
    wire [47:0] ts_a, ts_b;
    wire busy_a, valid_a, fail_a, busy_b, valid_b, fail_b;
    wire [31:0] ovf_a, peak_a, ovf_b, peak_b;
    wire [15:0] nz_a, last_a, nz_b, last_b;

    tdc_chan #(.CAL_LOG2(CAL_LOG2)) ca (
        .clk(clk), .sig_raw(sig_a), .ro(ro), .tick_ps(tick_ps), .want(want_a),
        .cal_req(cal_req), .cal_abort(abort), .rise_c(rise_a), .trig_ack(trig_a),
        .ts_valid(tsv_a), .ts_ps(ts_a), .use_ro(use_ro_a), .cal_busy(busy_a),
        .cal_valid(valid_a), .cal_fail(fail_a),
        .d_ovf(ovf_a), .d_peak(peak_a), .d_nz(nz_a), .d_last(last_a));
    tdc_chan #(.CAL_LOG2(CAL_LOG2)) cb (
        .clk(clk), .sig_raw(sig_b), .ro(ro), .tick_ps(tick_ps), .want(want_b),
        .cal_req(cal_req), .cal_abort(abort), .rise_c(rise_b), .trig_ack(trig_b),
        .ts_valid(tsv_b), .ts_ps(ts_b), .use_ro(use_ro_b), .cal_busy(busy_b),
        .cal_valid(valid_b), .cal_fail(fail_b),
        .d_ovf(ovf_b), .d_peak(peak_b), .d_nz(nz_b), .d_last(last_b));

    wire [25:0] per_a, per_b;
    wire [47:0] dt_a, dt_b;
    wire        al_a, al_b, tgl_a, tgl_b;
    win_recip wa (.clk(clk), .rise_c(rise_a), .trig_ack(trig_a), .ts_valid(tsv_a), .ts_ps(ts_a),
                  .gate_tick(gate_tick), .hold(busy_a | ~valid_a), .want(want_a),
                  .r_periods(per_a), .r_dt(dt_a), .r_dt_alias(al_a), .res_tgl(tgl_a));
    win_recip wb (.clk(clk), .rise_c(rise_b), .trig_ack(trig_b), .ts_valid(tsv_b), .ts_ps(ts_b),
                  .gate_tick(gate_tick), .hold(busy_b | ~valid_b), .want(want_b),
                  .r_periods(per_b), .r_dt(dt_b), .r_dt_alias(al_b), .res_tgl(tgl_b));

    integer errors = 0;

    // ---- signal: periodicky, A od casu 0, B opozdene o B_SKEW ----
    // nezavisle generatory (nemeni se behem kalibrace -- tam signal nema vliv)
    initial begin : gen_a
        #123;
        forever begin sig_a = 1; #(TSIG/2); sig_a = 0; #(TSIG - TSIG/2); end
    end
    initial begin : gen_b
        #(123 + B_SKEW);
        forever begin sig_b = 1; #(TSIG/2); sig_b = 0; #(TSIG - TSIG/2); end
    end

    // ---- kalibrace + kontrola pokryti ----
    initial begin
        #200000;
        @(posedge clk); cal_req <= 1; @(posedge clk); cal_req <= 0;
        wait ((valid_a && valid_b) || fail_a || fail_b);
        $display("CAL A: valid=%0d ovf=%0d peak=%0d nz=%0d last=%0d | B: valid=%0d ovf=%0d peak=%0d nz=%0d last=%0d (t=%0t ps)",
                 valid_a, ovf_a, peak_a, nz_a, last_a, valid_b, ovf_b, peak_b, nz_b, last_b, $time);
        if (!(valid_a && valid_b)) begin $display("FAIL: kalibrace neprobehla"); errors = errors + 1; end
        if (ovf_a > (1 << (CAL_LOG2 - 4)) || ovf_b > (1 << (CAL_LOG2 - 4))) begin
            $display("FAIL: retez kratky (udalosti za koncem)"); errors = errors + 1;
        end
        // 1 + 40 oken
        run_windows(24);
        if (errors == 0) $display("PASS: tb_tdc"); else $display("FAIL: tb_tdc (%0d chyb)", errors);
        $finish;
    end

    // sber vysledku oken obou kanalu
    reg tga = 0, tgb = 0;
    integer nwa = 0, nwb = 0;
    real sa, sa2, sb, sb2, sab, sab2; integer na_, nb_, nab;
    integer bad;
    task run_windows(input integer nwin);
        real ea, eb, d;
        integer got;
        begin
            sa = 0; sa2 = 0; sb = 0; sb2 = 0; sab = 0; sab2 = 0; na_ = 0; nb_ = 0; nab = 0; bad = 0;
            tga = tgl_a; tgb = tgl_b; got = 0;
            while (got < nwin) begin
                @(posedge clk);
                if (tgl_a !== tga) begin
                    tga = tgl_a;
                    ea = $itor(dt_a) * 625.0 / 1024.0 - $itor(per_a) * TSIG;
                    sa = sa + ea; sa2 = sa2 + ea*ea; na_ = na_ + 1;
                    if (ea > 150.0 || ea < -150.0) bad = bad + 1;
                    if (al_a) begin $display("FAIL: alias A"); errors = errors + 1; end
                end
                if (tgl_b !== tgb) begin
                    tgb = tgl_b;
                    eb = $itor(dt_b) * 625.0 / 1024.0 - $itor(per_b) * TSIG;
                    sb = sb + eb; sb2 = sb2 + eb*eb; nb_ = nb_ + 1;
                    if (eb > 150.0 || eb < -150.0) bad = bad + 1;
                    got = got + 1;
                    // symetrie: stejny pocet period a rozdil dt A vs B v tomtez okne
                    if (per_a !== per_b) $display("  (okno %0d: periody A=%0d B=%0d)", got, per_a, per_b);
                end
            end
            d = (sa / na_) - (sb / nb_);
            $display("PRESNOST A: n=%0d chyba dt: stred=%0.1f ps sigma=%0.1f ps | B: n=%0d stred=%0.1f ps sigma=%0.1f ps | rozdil stredu A-B=%0.1f ps | odlehle(>150ps)=%0d",
                     na_, sa/na_, $sqrt(sa2/na_ - (sa/na_)*(sa/na_)),
                     nb_, sb/nb_, $sqrt(sb2/nb_ - (sb/nb_)*(sb/nb_)), d, bad);
            if ($sqrt(sa2/na_ - (sa/na_)*(sa/na_)) > 40.0 || $sqrt(sb2/nb_ - (sb/nb_)*(sb/nb_)) > 40.0) begin
                $display("FAIL: sigma dt > 40 ps"); errors = errors + 1;
            end
            if (bad > 0) begin $display("FAIL: odlehla chyba > 150 ps"); errors = errors + 1; end
            if (d > 40.0 || d < -40.0) begin $display("FAIL: asymetrie kanalu A/B > 40 ps"); errors = errors + 1; end
            if (na_ < nwin - 2) begin $display("FAIL: kanal A dal %0d oken", na_); errors = errors + 1; end
        end
    endtask
endmodule
