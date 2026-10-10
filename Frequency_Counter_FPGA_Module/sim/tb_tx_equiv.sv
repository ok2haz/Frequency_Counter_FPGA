// tb_tx_equiv.sv -- novy plochy `case` multiplexer TX bajtu (spi_app) == puvodni retez porovnani rozsahu
// (tx_old_ref.v = spi_app pred prepisem). Nahodny stav, vsech 126 bajtu, DATA i CAL. Rozdily jen tam, kde
// se zmena ZAMERNE lisi (CAL bajty 50/51 = konfigurace TDC; DATA 60..63 = FW_VERSION/CAPS, 65 + 68..99 = window stream nahrazen regresnim blokem v FW 0x0411;
// FW 0x041F: DATA 12..19 = identita bitstreamu misto freq_x100000, 64/65 = hlidac 100 MHz, 97/98 = kontrolni pocitani).
`timescale 1ps/1ps
module tb_tx_equiv;
    reg clk = 0;   // hodiny STOJI: porovnava se jen kombinacni `tb`, FSM nesmi menit idx/registry
    reg [63:0] meas_freq = 0, meas_gate = 0, meas_ts = 0, meas_freq16 = 0;
    reg [31:0] meas_periods = 0, meas_err = 0, meas_periods_b = 0, meas_seq = 0;
    reg [7:0]  meas_phase = 0, meas_st2 = 0, meas_tdc_status = 0, meas_ch = 0;
    reg [47:0] dt_a = 0, dt_b = 0;
    reg [191:0] cal_diag = 0; reg [15:0] mt_a = 0, mt_b = 0, dm = 0, ds = 0;
    reg [23:0] ha = 0, hb = 0;
    wire [9:0] hk_n; wire [7:0] hk_o;
    wire [7:0] ds_n, ds_o; wire cm_n, cm_o; wire [1:0] bw_n, bw_o;
    wire th_n, th_o, we_n, we_o; wire [7:0] wa_n, wa_o, wd_n, wd_o;

    spi_app u_new (.clk(clk), .meas_freq_x100000(meas_freq), .meas_periods(meas_periods), .meas_gate_ns(meas_gate),
        .meas_timestamp(meas_ts), .meas_error_flags(meas_err), .meas_channel(meas_ch), .new_meas(1'b0), .signal_lost(1'b0),
        .meas_freq16_x100000(meas_freq16), .meas_phase_status(meas_phase), .meas_status2(meas_st2),
        .meas_dt_a_ps(dt_a), .meas_dt_b_ps(dt_b), .meas_periods_b(meas_periods_b), .meas_tdc_status(meas_tdc_status),
        .meas_cal_diag(cal_diag), .meas_tdc_cfg(16'h0123), .rg_n_a(24'd0), .rg_n_b(24'd0), .rg_xm_a(40'd0), .rg_xm_b(40'd0), .rg_ym_a(48'd0), .rg_ym_b(48'd0), .rg_ok_a(1'b0), .rg_ok_b(1'b0), .cd_a_end(9'd0), .cd_a_st(9'd0), .cd_b_end(9'd0), .cd_b_st(9'd0), .meas_maxtap_a(mt_a), .meas_maxtap_b(mt_b), .hist_k(hk_n),
        .meas_hist_a(ha), .meas_hist_b(hb), .dbg_mosi_cnt(dm), .dbg_sck_cnt(ds),
        .meas_clk_status(8'h01), .meas_clk_loss(8'd0), .meas_ccd_p(8'd0), .meas_ccd_s(8'd0),
        .rx_valid(1'b0), .rx_b0(8'd0), .rx_b1(8'd0), .rx_b2(8'd0), .rx_seq(32'd0), .rx_p0(8'd0), .rx_p1(8'd0), .rx_p2(8'd0),
        .rx_crc_calc(16'd0), .rx_crc_recv(16'd0), .tx_half(th_n), .tx_we(we_n), .tx_waddr(wa_n), .tx_wdata(wd_n),
        .phy_busy(1'b0), .phy_base(1'b0), .dbg_status(ds_n), .cal_mode(cm_n), .base_win(bw_n));
    spi_app_old u_old (.clk(clk), .meas_freq_x100000(meas_freq), .meas_periods(meas_periods), .meas_gate_ns(meas_gate),
        .meas_timestamp(meas_ts), .meas_error_flags(meas_err), .meas_channel(meas_ch), .new_meas(1'b0), .signal_lost(1'b0),
        .meas_freq16_x100000(meas_freq16), .meas_phase_status(meas_phase), .meas_status2(meas_st2),
        .meas_dt_a_ps(dt_a), .meas_dt_b_ps(dt_b), .meas_periods_b(meas_periods_b), .meas_tdc_status(meas_tdc_status),
        .meas_cal_diag(cal_diag), .meas_maxtap_a(mt_a), .meas_maxtap_b(mt_b), .hist_k(hk_o),
        .meas_hist_a(ha), .meas_hist_b(hb), .dbg_mosi_cnt(dm), .dbg_sck_cnt(ds),
        .rx_valid(1'b0), .rx_b0(8'd0), .rx_b1(8'd0), .rx_b2(8'd0), .rx_seq(32'd0), .rx_p0(8'd0), .rx_p1(8'd0), .rx_p2(8'd0),
        .rx_crc_calc(16'd0), .rx_crc_recv(16'd0), .tx_half(th_o), .tx_we(we_o), .tx_waddr(wa_o), .tx_wdata(wd_o),
        .phy_busy(1'b0), .phy_base(1'b0), .dbg_status(ds_o), .cal_mode(cm_o), .base_win(bw_o));

reg [63:0] tmp;
`define SB(n, v) tmp = v; u_new.n = tmp; u_old.n = tmp;   // v se vyhodnoti JEDNOU (jinak by $urandom dal novemu a staremu ruzne hodnoty)
    integer r, i, cal, errs = 0, cmp = 0;
    initial begin
        #1000;
        for (r = 0; r < 300; r = r + 1) begin
            meas_freq = {$urandom, $urandom}; meas_gate = {$urandom, $urandom}; meas_ts = {$urandom, $urandom};
            meas_freq16 = {$urandom, $urandom}; meas_periods = $urandom; meas_err = $urandom; meas_periods_b = $urandom;
            meas_phase = $urandom; meas_st2 = $urandom; meas_tdc_status = $urandom; meas_ch = $urandom;
            dt_a = {$urandom, $urandom}; dt_b = {$urandom, $urandom};
            cal_diag = {$urandom, $urandom, $urandom, $urandom, $urandom, $urandom};
            mt_a = $urandom; mt_b = $urandom; dm = $urandom; ds = $urandom; ha = $urandom; hb = $urandom;
            tmp = {$urandom, $urandom}; u_old.h_freq = tmp; `SB(h_edge, {$urandom, $urandom}) `SB(h_gate, {$urandom, $urandom})
            `SB(h_freq16, {$urandom, $urandom}) `SB(h_ts, {$urandom, $urandom}) `SB(h_ch, $urandom) `SB(h_phase, $urandom)
            `SB(h_status2, $urandom) `SB(h_err, $urandom) `SB(h_dt_a, {$urandom, $urandom}) `SB(h_dt_b, {$urandom, $urandom})
            `SB(h_edge_b, $urandom) tmp = $urandom; u_old.win_cnt = tmp; `SB(d_rx0, $urandom) `SB(d_rx1, $urandom) `SB(d_rx2, $urandom)
            `SB(d_ccl, $urandom) `SB(d_cch, $urandom) `SB(d_rcl, $urandom) `SB(d_rch, $urandom)
            tmp = $urandom; u_old.w0_seq = tmp; tmp = $urandom; u_old.w0_edges = tmp; tmp = {$urandom, $urandom}; u_old.w0_dt = tmp;
            tmp = $urandom; u_old.w1_seq = tmp; tmp = $urandom; u_old.w1_edges = tmp; tmp = {$urandom, $urandom}; u_old.w1_dt = tmp;
            `SB(seq, $urandom) `SB(data_valid, $urandom) `SB(data_fresh, $urandom) `SB(ack_ok, $urandom)
            `SB(rx_crc_error, $urandom) `SB(cal_mode_r, $urandom) `SB(hist_mode, $urandom)
            u_new.hist_k_r = $urandom % 256; u_old.hist_k_r = u_new.hist_k_r;
            for (cal = 0; cal < 2; cal = cal + 1) begin
                `SB(is_cal, cal[0])
                for (i = 0; i < 126; i = i + 1) begin
                    `SB(idx, i)
                    #1;
                    if (!(cal == 1 && (i == 50 || i == 51)) && !(cal == 0 && ((i >= 12 && i <= 19) || (i >= 60 && i <= 65) || (i >= 68 && i <= 99)))) begin   // 50/51 CAL = konfigurace TDC, 60..63 = FW_VERSION/CAPS, 65 + 68..99 = window stream -> regresni blok (zamerne nove)
                        cmp = cmp + 1;
                        if (u_new.tb !== u_old.tb) begin
                            errs = errs + 1;
                            if (errs < 8) $display("RUZNOST r=%0d cal=%0d idx=%0d: new=%02x old=%02x", r, cal, i, u_new.tb, u_old.tb);
                        end
                    end
                end
            end
        end
        if (errs == 0) $display("PASS: tb_tx_equiv (%0d porovnani)", cmp); else $display("FAIL: tb_tx_equiv %0d rozdilu", errs);
        $finish;
    end
endmodule
