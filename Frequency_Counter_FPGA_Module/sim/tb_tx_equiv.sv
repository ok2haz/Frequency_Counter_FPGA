// tb_tx_equiv.sv -- novy plochy `case` multiplexer TX bajtu (spi_app) == puvodni retez porovnani rozsahu
// (tx_old_ref.v = spi_app pred prepisem). Nahodny stav, vsech 126 bajtu, DATA i CAL. Rozdily jen tam, kde
// se zmena ZAMERNE lisi (CAL bajty 50/51 = konfigurace TDC; DATA 60..63 = FW_VERSION/CAPS, 65 + 68..99 = window stream nahrazen regresnim blokem v FW 0x0411;
// FW 0x041F: DATA 12..19 = identita bitstreamu misto freq_x100000, 64/65 = hlidac 100 MHz, 97/98 = kontrolni pocitani;
// FW 0x0424: DATA 52..59 = GPS 1PPS misto freq16_x100000 -- overeno zvlast proti vstupum meas_pps_*;
// FW 0x0425: 10/11, 57..59 = diagnostika E0 (nove); 112..117/124/125 = DBG_RX vypnuty -> overuje se, ze jsou nuly;
// FW 0x0426: regresni blok = surove soucty v 24..43, 66..96, 107 -> overuje se proti ocekavanemu rozlozeni ze vstupu rg_*).
`timescale 1ps/1ps
module tb_tx_equiv;
    reg clk = 0;   // hodiny STOJI: porovnava se jen kombinacni `tb`, FSM nesmi menit idx/registry
    reg [63:0] meas_freq = 0, meas_gate = 0, meas_ts = 0, meas_freq16 = 0;
    reg [31:0] meas_periods = 0, meas_err = 0, meas_periods_b = 0, meas_seq = 0;
    reg [7:0]  meas_phase = 0, meas_st2 = 0, meas_tdc_status = 0, meas_ch = 0;
    reg [47:0] dt_a = 0, dt_b = 0;
    reg [191:0] cal_diag = 0; reg [15:0] mt_a = 0, mt_b = 0, dm = 0, ds = 0;
    reg [23:0] ha = 0, hb = 0;
    reg [31:0] pps_ts = 0; reg [7:0] pps_seq = 0;   // FW 0x0424
    reg cwa_t = 0, cwb_t = 0;                       // FW 0x0427
    // FW 0x0426: surove soucty regrese na vstupech + ocekavane bajty (er/em: hodnota / bajt patri bloku)
    reg [23:0] rn_a = 0, rn_b = 0; reg [25:0] rx0_a = 0, rx0_b = 0; reg [41:0] rsx_a = 0, rsx_b = 0;
    reg [47:0] ry0_a = 0, ry0_b = 0; reg [55:0] rsy_a = 0, rsy_b = 0; reg rok_a = 0, rok_b = 0;
    reg [7:0] er [0:127]; reg em [0:127]; integer k;
    task put(input integer a, input [7:0] v); begin er[a] = v; em[a] = 1'b1; end endtask
    task build_rg;
        reg [31:0] xa, xb; reg [47:0] sxa, sxb;
        begin
            for (k = 0; k < 128; k = k + 1) em[k] = 1'b0;
            xa = {rok_a, 5'd0, rx0_a}; xb = {rok_b, 5'd0, rx0_b}; sxa = {6'd0, rsx_a}; sxb = {6'd0, rsx_b};
            for (k = 0; k < 3; k = k + 1) begin put(66 + k, rn_a[8*k +: 8]); put(24 + k, rn_b[8*k +: 8]); end
            for (k = 0; k < 4; k = k + 1) begin put(69 + k, xa[8*k +: 8]);   put(27 + k, xb[8*k +: 8]); end
            for (k = 0; k < 6; k = k + 1) begin put(73 + k, sxa[8*k +: 8]);  put(31 + k, sxb[8*k +: 8]);
                                                put(79 + k, ry0_a[8*k +: 8]); put(37 + k, ry0_b[8*k +: 8]); end
            for (k = 0; k < 7; k = k + 1) put(85 + k, rsy_a[8*k +: 8]);
            put(112, {u_new.lut_cnt[13:8], cwb_t, cwa_t}); put(113, u_new.lut_cnt[7:0]);      // FW 0x0427: stav zapisu tabulky TDC
            put(114, u_new.lut_sum[7:0]); put(115, u_new.lut_sum[15:8]);
            put(43, rsy_b[7:0]); put(92, rsy_b[15:8]); put(93, rsy_b[23:16]); put(94, rsy_b[31:24]);
            put(95, rsy_b[39:32]); put(96, rsy_b[47:40]); put(107, rsy_b[55:48]);
        end
    endtask
    wire [9:0] hk_n; wire [7:0] hk_o;
    wire [7:0] ds_n, ds_o; wire cm_n, cm_o; wire [2:0] bw_n; wire [1:0] bw_o;   // novy spi_app ma base_win 3 b (FW 0x041E), stara reference 2 b
    wire th_n, th_o, we_n, we_o; wire [7:0] wa_n, wa_o, wd_n, wd_o;

    spi_app u_new (.clk(clk), .meas_freq_x100000(meas_freq), .meas_periods(meas_periods), .meas_gate_ns(meas_gate),
        .meas_timestamp(meas_ts), .meas_error_flags(meas_err), .meas_channel(meas_ch), .new_meas(1'b0), .signal_lost(1'b0),
        .meas_freq16_x100000(meas_freq16), .meas_phase_status(meas_phase), .meas_status2(meas_st2),
        .meas_dt_a_ps(dt_a), .meas_dt_b_ps(dt_b), .meas_periods_b(meas_periods_b), .meas_tdc_status(meas_tdc_status),
        .meas_cal_diag(cal_diag), .meas_tdc_cfg(16'h0123), .rg_n_a(rn_a), .rg_n_b(rn_b), .rg_x0_a(rx0_a), .rg_x0_b(rx0_b),
        .rg_sx_a(rsx_a), .rg_sx_b(rsx_b), .rg_y0_a(ry0_a), .rg_y0_b(ry0_b), .rg_sy_a(rsy_a), .rg_sy_b(rsy_b), .rg_ok_a(rok_a), .rg_ok_b(rok_b),
        .cd_a_end(9'd0), .cd_a_st(9'd0), .cd_b_end(9'd0), .cd_b_st(9'd0), .meas_maxtap_a(mt_a), .meas_maxtap_b(mt_b), .hist_k(hk_n),
        .meas_hist_a(ha), .meas_hist_b(hb), .dbg_mosi_cnt(dm), .dbg_sck_cnt(ds),
        .meas_clk_status(8'h01), .meas_clk_loss(8'd0), .meas_clk_mhz(8'd100), .meas_ccd_p(8'd0), .meas_ccd_s(8'd0),
        .meas_pps_ts(pps_ts), .meas_pps_seq(pps_seq),
        .cal_wait_a(cwa_t), .cal_wait_b(cwb_t),   // FW 0x0427: stav cekani na tabulku (abs 112 bit0/1)
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
    integer dcnt [0:251];
    initial for (i = 0; i < 252; i = i + 1) dcnt[i] = 0;
    initial begin
        #1000;
        for (r = 0; r < 300; r = r + 1) begin
            meas_freq = {$urandom, $urandom}; meas_gate = {$urandom, $urandom}; meas_ts = {$urandom, $urandom};
            meas_freq16 = {$urandom, $urandom}; meas_periods = $urandom; meas_err = $urandom; meas_periods_b = $urandom;
            meas_phase = $urandom; meas_st2 = $urandom; meas_tdc_status = $urandom; meas_ch = $urandom;
            dt_a = {$urandom, $urandom}; dt_b = {$urandom, $urandom};
            cal_diag = {$urandom, $urandom, $urandom, $urandom, $urandom, $urandom};
            mt_a = $urandom; mt_b = $urandom; dm = $urandom; ds = $urandom; ha = $urandom; hb = $urandom;
            pps_ts = $urandom; pps_seq = $urandom;
            rn_a = $urandom; rn_b = $urandom; rx0_a = $urandom; rx0_b = $urandom; rsx_a = {$urandom, $urandom}; rsx_b = {$urandom, $urandom};
            ry0_a = {$urandom, $urandom}; ry0_b = {$urandom, $urandom}; rsy_a = {$urandom, $urandom}; rsy_b = {$urandom, $urandom};
            rok_a = $urandom; rok_b = $urandom; cwa_t = $urandom; cwb_t = $urandom;
            u_new.lut_cnt = $urandom; u_new.lut_sum = $urandom;
            build_rg;
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
                    if (cal == 0 && i >= 52 && i <= 56) begin   // FW 0x0424: 1PPS (ocekavana hodnota ze vstupu, ne ze stare reference)
                        cmp = cmp + 1;
                        if (u_new.tb !== ((i <= 55) ? pps_ts[8*(i-52) +: 8] : pps_seq)) begin
                            errs = errs + 1;
                            if (errs < 8) $display("RUZNOST 1PPS r=%0d idx=%0d: new=%02x", r, i, u_new.tb);
                        end
                    end else if (cal == 0 && em[i]) begin   // FW 0x0426: regresni blok (surove soucty) proti ocekavanemu rozlozeni
                        cmp = cmp + 1;
                        if (u_new.tb !== er[i]) begin
                            errs = errs + 1;
                            if (errs < 8) $display("RUZNOST REGR r=%0d idx=%0d: new=%02x ocekavano=%02x", r, i, u_new.tb, er[i]);
                            dcnt[i] = dcnt[i] + 1;
                        end
                    end else if (cal == 0 && ((i >= 116 && i <= 117) || i == 124 || i == 125)) begin
                        // FW 0x0425: ladici bajty prijmu (DBG_RX = 0) se ZAMERNE posilaji jako nuly -- kontroluje se to, ne preskakuje
                        cmp = cmp + 1;
                        if (u_new.tb !== 8'd0) begin
                            errs = errs + 1;
                            if (errs < 8) $display("RUZNOST DBG_RX r=%0d idx=%0d: new=%02x (ocekavano 00)", r, i, u_new.tb);
                            dcnt[i] = dcnt[i] + 1;
                        end
                    end else if (!(cal == 1 && (i == 50 || i == 51)) && !(cal == 0 && ((i >= 12 && i <= 19) || (i >= 60 && i <= 65) || (i >= 68 && i <= 99) || i == 10 || i == 11 || (i >= 57 && i <= 59) || (i >= 112 && i <= 115)))) begin   // 50/51 CAL = konfigurace TDC, 60..63 = FW_VERSION/CAPS, 65 + 68..99 = window stream -> regresni blok (zamerne nove); 10/11 = hlidac koherence, 57..59 = bubliny/znacky pri CS (FW 0x0425, caps bit14, zamerne nove -- tx_old_ref.v je nezna)
                        cmp = cmp + 1;
                        if (u_new.tb !== u_old.tb) begin
                            errs = errs + 1;
                            if (errs < 8) $display("RUZNOST r=%0d cal=%0d idx=%0d: new=%02x old=%02x", r, cal, i, u_new.tb, u_old.tb);
                            dcnt[cal*126 + i] = dcnt[cal*126 + i] + 1;
                        end
                    end
                end
            end
        end
        for (i = 0; i < 252; i = i + 1)   // souhrn: VSECHNY indexy s rozdilem (vypis vyse je omezeny na 8 radku)
            if (dcnt[i] != 0) $display("  rozdil cal=%0d idx=%0d: %0d x", i / 126, i % 126, dcnt[i]);
        if (errs == 0) $display("PASS: tb_tx_equiv (%0d porovnani)", cmp); else $display("FAIL: tb_tx_equiv %0d rozdilu", errs);
        $finish;
    end
endmodule
