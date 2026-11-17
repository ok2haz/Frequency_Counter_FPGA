// ============================================================
// tb_stmcal.sv -- FW 0x0427: KONCOVY test vyrobniho toku kalibrace TDC (AUTOCAL = 0): sber histogramu ve FPGA ->
// "STM" (BFM po SPI, jako fpga_freq.c) precte histogram ramci CAL, spocita tabulku, zapise ji ramci TYPE 0x0A LUT_WRITE,
// overi pocitadlo + kontrolni soucet z DATA ramce (abs 112..115), potvrdi SET_CONFIG 0x06 = 1 -> cal_valid.
// Cesta: spi_slave_phy -> spi_app (RX decode, pocitadlo, soucet, prepinaci bity) -> lut_wr_sync (CDC 10 -> 100 MHz) -> tdc_chan.
// PASS = tabulky obou kanalu v BRAM (ca.lut / cb.lut) == vypocet z histogramu (vzorec z tb_lutequiv), pocitadlo i soucet sedi,
// cal_valid po potvrzeni, cal_wait pred potvrzenim.
// ============================================================
`timescale 1ns/1ps
`include "tb_clk.vh"
module tb_stmcal;
    localparam CAL_LOG2 = 15;
    localparam NC = 256;
    reg clk100 = 0; always #(`TB_THALF_PS/1000.0) clk100 = ~clk100;
    reg clk10  = 0; always #50 clk10  = ~clk10;
    reg [47:0] tick_ps = 48'd0; always @(posedge clk100) tick_ps <= tick_ps + 48'd16384;

    // ---- dva kanaly TDC, spolecny ring oscilator ----
    wire ro, use_ro_a, use_ro_b;
    ring_osc #(.DIV_LOG2(2)) u_ro (.en(use_ro_a | use_ro_b), .out(ro));
    reg [27:0] tmo = 0; reg abort = 0;
    always @(posedge clk100) begin abort <= 0; if (use_ro_a | use_ro_b) begin tmo <= tmo + 1; if (&tmo) abort <= 1; end else tmo <= 0; end
    reg cal_req = 0, sig = 0;
    wire busy_a, valid_a, fail_a, wait_a, busy_b, valid_b, fail_b, wait_b;
    wire [31:0] ovf_a, peak_a, ovf_b, peak_b; wire [15:0] nz_a, last_a, mt_a, nz_b, last_b, mt_b;
    wire [23:0] hq_a, hq_b; wire [9:0] hk;
    wire lw_en_a, lw_en_b, lw_cm_p; wire [9:0] lw_a_q; wire [13:0] lw_n_q;
    tdc_chan #(.CAL_LOG2(CAL_LOG2), .AUTOCAL(0)) ca (
        .clk(clk100), .sig_raw(sig), .ro(ro), .tick_ps(tick_ps), .want(1'b0), .want_seg(1'b0), .cal_req(cal_req), .cal_abort(abort),
        .lw_en(lw_en_a), .lw_a(lw_a_q), .lw_neg(lw_n_q), .lw_commit(lw_cm_p), .cal_wait(wait_a),
        .use_ro(use_ro_a), .cal_busy(busy_a), .cal_valid(valid_a), .cal_fail(fail_a),
        .d_ovf(ovf_a), .d_peak(peak_a), .d_nz(nz_a), .d_last(last_a), .d_maxtap(mt_a), .dump_k(hk), .dump_q(hq_a));
    tdc_chan #(.CAL_LOG2(CAL_LOG2), .AUTOCAL(0)) cb (
        .clk(clk100), .sig_raw(sig), .ro(ro), .tick_ps(tick_ps), .want(1'b0), .want_seg(1'b0), .cal_req(cal_req), .cal_abort(abort),
        .lw_en(lw_en_b), .lw_a(lw_a_q), .lw_neg(lw_n_q), .lw_commit(lw_cm_p), .cal_wait(wait_b),
        .use_ro(use_ro_b), .cal_busy(busy_b), .cal_valid(valid_b), .cal_fail(fail_b),
        .d_ovf(ovf_b), .d_peak(peak_b), .d_nz(nz_b), .d_last(last_b), .d_maxtap(mt_b), .dump_k(hk), .dump_q(hq_b));

    // ---- SPI: PHY + aplikace + CDC zapisu (shodne s top.v) ----
    reg sck = 0, cs = 1, mosi = 0; wire miso;
    wire        fe_tgl, tx_half, tx_we, phy_busy, phy_base;
    wire [7:0]  tx_waddr, tx_wdata, rb0, rb1, rb2, rp0, rp1, rp2;
    wire [31:0] rseq; wire [15:0] rcc, rcr, dmc, dsc;
    spi_slave_phy phy(.clk(clk100), .clk_app(clk10), .sck_pin(sck), .cs_pin(cs), .mosi_pin(mosi), .miso(miso),
        .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata), .tx_base(phy_base), .busy(phy_busy),
        .rx_b0(rb0), .rx_b1(rb1), .rx_b2(rb2), .rx_seq(rseq), .rx_p0(rp0), .rx_p1(rp1), .rx_p2(rp2),
        .rx_crc_calc(rcc), .rx_crc_recv(rcr), .frame_end_tgl(fe_tgl), .dbg_mosi_cnt(dmc), .dbg_sck_cnt(dsc));
    reg [2:0] fe = 0; always @(posedge clk10) fe <= {fe[1:0], fe_tgl}; wire rxv = fe[2] ^ fe[1];
    reg [1:0] cwa_s = 0, cwb_s = 0; always @(posedge clk10) begin cwa_s <= {cwa_s[0], wait_a}; cwb_s <= {cwb_s[0], wait_b}; end
    wire lw_tgl, lw_ch, lw_commit_tgl; wire [9:0] lw_a; wire [13:0] lw_neg;
    lut_wr_sync u_lws (.clk(clk100), .lw_tgl(lw_tgl), .lw_commit_tgl(lw_commit_tgl), .lw_ch(lw_ch), .lw_a(lw_a), .lw_neg(lw_neg),
        .lw_en_a(lw_en_a), .lw_en_b(lw_en_b), .lw_cm_p(lw_cm_p), .lw_a_q(lw_a_q), .lw_n_q(lw_n_q));
    wire cal_mode; wire [2:0] bw; wire [7:0] dbg;
    spi_app app(.clk(clk10), .meas_freq_x100000(64'd0), .meas_periods(32'd0), .meas_gate_ns(64'd0),
        .meas_timestamp(64'd0), .meas_error_flags(32'd0), .meas_channel(8'd0), .new_meas(1'b0), .signal_lost(1'b0),
        .meas_freq16_x100000(64'd0), .meas_phase_status(8'd0), .meas_status2(8'd0), .meas_dt_a_ps(48'd0),
        .meas_dt_b_ps(48'd0), .meas_periods_b(32'd0), .meas_tdc_status(8'h00), .meas_cal_diag(192'd0),
        .meas_tdc_cfg(16'h0123), .rg_n_a(24'd0), .rg_n_b(24'd0), .rg_x0_a(26'd0), .rg_x0_b(26'd0), .rg_sx_a(42'd0), .rg_sx_b(42'd0),
        .rg_y0_a(48'd0), .rg_y0_b(48'd0), .rg_sy_a(56'd0), .rg_sy_b(56'd0), .rg_ok_a(1'b0), .rg_ok_b(1'b0),
        .cd_a_end(9'd0), .cd_a_st(9'd0), .cd_b_end(9'd0), .cd_b_st(9'd0), .hist_k(hk), .meas_hist_a(hq_a), .meas_hist_b(hq_b),
        .dbg_mosi_cnt(dmc), .dbg_sck_cnt(dsc),
        .rx_valid(rxv), .rx_b0(rb0), .rx_b1(rb1), .rx_b2(rb2), .rx_seq(rseq), .rx_p0(rp0), .rx_p1(rp1), .rx_p2(rp2),
        .rx_crc_calc(rcc), .rx_crc_recv(rcr), .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata),
        .phy_busy(phy_busy), .phy_base(phy_base), .dbg_status(dbg), .cal_mode(cal_mode), .base_win(bw), .meas_clk_mhz(8'd100),
        .meas_pps_ts(32'd0), .meas_pps_seq(8'd0),
        .lw_tgl(lw_tgl), .lw_a(lw_a), .lw_neg(lw_neg), .lw_ch(lw_ch), .lw_commit_tgl(lw_commit_tgl),
        .cal_wait_a(cwa_s[1]), .cal_wait_b(cwb_s[1]));

    // ---- BFM (shodne s tb_link) ----
    function [15:0] crc(input [15:0] c0, input [7:0] d);
        integer b; reg [15:0] c; begin c = c0 ^ {d, 8'd0}; for (b = 0; b < 8; b = b + 1) c = c[15] ? ((c << 1) ^ 16'h1021) : (c << 1); crc = c; end endfunction
    reg [7:0] tx [0:127]; reg [7:0] rx [0:127];
    integer i, j, errors = 0; reg [15:0] cc;
    integer half_ns = 25, gap_ns = 2000, pause_ns = 25000;     // 20 MHz, mezera mezi ramci 25 us jako FPGA_FRAME_GAP_US
    task build(input [7:0] ty, input [7:0] p0, input [7:0] p1, input [7:0] p2);
        begin for (i = 0; i < 128; i = i + 1) tx[i] = 0; tx[0] = 8'hA5; tx[1] = 8'h02; tx[2] = ty; tx[12] = p0; tx[13] = p1; tx[14] = p2;
            cc = 16'hFFFF; for (i = 0; i < 126; i = i + 1) cc = crc(cc, tx[i]); tx[126] = cc[7:0]; tx[127] = cc[15:8]; end endtask
    task xfer;
        begin cs = 0; #(gap_ns);
            for (i = 0; i < 128; i = i + 1) begin rx[i] = 0;
                for (j = 7; j >= 0; j = j - 1) begin mosi = tx[i][j]; #(half_ns); sck = 1; rx[i][j] = miso; #(half_ns); sck = 0; end end
            #(gap_ns); cs = 1; #(pause_ns); end endtask

    // ---- "STM" ----
    reg [23:0] hA [0:NC-1]; reg [23:0] hB [0:NC-1];
    reg [15:0] lA [0:NC-1]; reg [15:0] lB [0:NC-1];
    reg [15:0] sumx; integer cntx;
    integer k, ch, tries;
    longint cum, xv, xsh, x14; reg [13:0] neg; reg [1:0] amt;
    task calc(input integer which);
        integer a; reg [23:0] hh;
        begin cum = 0;
            for (a = 0; a < NC; a = a + 1) begin
                hh = which ? hB[a] : hA[a];
                xv = 2 * cum + hh; cum = cum + hh;
                xsh = xv >> (CAL_LOG2 - 13); x14 = (xsh > 16383) ? 16383 : xsh;
                neg = (14'd0 - x14[13:0]); amt = 2'd1 + ((x14 != 0) ? 2'd1 : 2'd0);
                if (which) lB[a] = {amt, neg}; else lA[a] = {amt, neg};
            end
            if (cum !== (1 << CAL_LOG2)) begin errors = errors + 1; $display("FAIL: soucet histogramu kanalu %0d = %0d", which, cum); end
        end endtask

    reg [7:0] p12; reg [15:0] ent;
    initial begin
        #200000;
        @(posedge clk100); cal_req <= 1; @(posedge clk100); cal_req <= 0;
        // 1) FPGA sebere histogram a prejde do cekani (valid = 0, busy = 0, wait = 1)
        wait (wait_a && wait_b);
        repeat (20) @(posedge clk100);
        if (valid_a || valid_b) begin errors = errors + 1; $display("FAIL: cal_valid pred zapisem tabulky"); end
        // 2) STM zjisti cekani z DATA ramce (abs 112 bit0/1)
        tries = 0;
        build(8'h06, 0, 0, 0); xfer; build(8'h06, 0, 0, 0); xfer;
        while ((rx[112][1:0] !== 2'b11) && tries < 20) begin build(8'h06, 0, 0, 0); xfer; tries = tries + 1; end
        if (rx[112][1:0] !== 2'b11) begin errors = errors + 1; $display("FAIL: DATA ramec nehlasi cekani (abs112=%h)", rx[112]); end
        // 3) zacatek zapisu (SET_CONFIG 0x06 = 0)
        build(8'h01, 8'h06, 8'h00, 0); xfer;
        // 4) histogramy
        for (k = 0; k < NC; k = k + 1) begin
            p12 = 8'd1 | (((k >> 8) & 3) << 1);
            build(8'hA0, p12, 8'd0, k[7:0]); xfer;
            build(8'h06, 0, 0, 0); xfer;
            if (rx[2] !== 8'hA0 || rx[38] !== p12 || rx[39] !== k[7:0]) begin errors = errors + 1; if (errors < 8) $display("FAIL: hist k=%0d typ=%h [38]=%h [39]=%h", k, rx[2], rx[38], rx[39]); end
            hA[k] = {rx[42], rx[41], rx[40]}; hB[k] = {rx[45], rx[44], rx[43]};
        end
        calc(0); calc(1);
        // 5) zapis tabulek
        sumx = 16'd0; cntx = 0;
        for (k = 0; k < NC; k = k + 1) for (ch = 0; ch < 2; ch = ch + 1) begin
            ent = ch ? lB[k] : lA[k];
            build(8'h0A, k[7:0], {ch[0], k[8], ent[13:8]}, ent[7:0]); xfer;
            sumx = sumx + ({1'b0, ch[0], k[8:0], 5'd0} ^ {2'd0, ent[13:0]});
            cntx = cntx + 1;
        end
        // 6) pocitadlo a soucet v DATA ramci (odpoved na posledni zapis prijde az v dalsim prenosu)
        build(8'h06, 0, 0, 0); xfer; build(8'h06, 0, 0, 0); xfer;
        $display("pocitadlo FPGA %0d (ocek %0d), soucet %04x (ocek %04x)", {rx[112][7:2], rx[113]}, cntx, {rx[115], rx[114]}, sumx);
        if ({rx[112][7:2], rx[113]} !== cntx[13:0] || {rx[115], rx[114]} !== sumx) begin errors = errors + 1; $display("FAIL: pocitadlo/soucet"); end
        // 7) potvrzeni
        if (valid_a || valid_b) begin errors = errors + 1; $display("FAIL: cal_valid pred potvrzenim"); end
        build(8'h01, 8'h06, 8'h01, 0); xfer;
        repeat (50) @(posedge clk100);
        if (!(valid_a && valid_b) || wait_a || wait_b) begin errors = errors + 1; $display("FAIL: po potvrzeni valid=%b/%b wait=%b/%b", valid_a, valid_b, wait_a, wait_b); end
        // 8) obsah tabulek v BRAM
        for (k = 0; k < NC; k = k + 1) begin
            if (ca.lut[k] !== lA[k]) begin errors = errors + 1; if (errors < 8) $display("FAIL: A lut[%0d] = %04x ocek %04x", k, ca.lut[k], lA[k]); end
            if (cb.lut[k] !== lB[k]) begin errors = errors + 1; if (errors < 8) $display("FAIL: B lut[%0d] = %04x ocek %04x", k, cb.lut[k], lB[k]); end
        end
        // 9) po potvrzeni se dalsi zapis ignoruje (FPGA uz neceka)
        build(8'h0A, 8'd5, 8'd0, 8'h55); xfer; repeat (30) @(posedge clk100);
        if (ca.lut[5] !== lA[5]) begin errors = errors + 1; $display("FAIL: zapis mimo cekani se provedl"); end
        if (errors == 0) $display("PASS: tb_stmcal (%0d adres x 2 kanaly, %0d zapisu)", NC, cntx); else $display("FAIL: tb_stmcal (%0d chyb)", errors);
        $finish;
    end
    initial begin #(64'd2_000_000_000); $display("FAIL: timeout"); $finish; end   // 2 s sim casu
endmodule
