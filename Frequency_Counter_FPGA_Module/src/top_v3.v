// ============================================================
// File: top_v3.v -- bitstream PROTOKOLU v3 (FW 0x0500), 2026-10-22. Specifikace: docs/PROTOKOL_V3.md v projektu STM.
// Projekt: v3/Counter_FPGA_v3.gprj (top_module top_v3, timing src/timing_v3.sdc). Bitstream v2 (top.v, FW 0x04xx)
// zustava beze zmeny a dal se sestavuje z Counter_FPGA.gprj -- v3 se na desce overuje vedle nej (ab_test).
//
// Proti top.v (v2):
//   * ODSTRANENO: okna (win_recip), hradlo a jeho segmenty, regresni blok, zamky vysledku, vyber kanalu, hlidac ztraty
//     signalu, 128:1 multiplexer TX ramce (spi_app). Okna, regresi i ztratu signalu dela STM ze zaznamu segmentu.
//   * NOVE: oba kanaly casuji KAZDOU hranu, kterou TDC stihne (>= 5 taktu od predchozi); soucty po segmentech L = 25 ms
//     (seg_chan3, v3_core.v), kruh 512 zaznamu v BSRAM (v3_ring), skladac ramce 1024 B a prikazy (app_v3.v), PHY v3.
//   * Kalibrace TDC STRIDAVE (cal_seq3) a DVE BANKY tabulky (tdc_chan BANKS = 1): po sberu histogramu kanal meri dal
//     se starou tabulkou, nova se aktivuje na hranici segmentu.
//   * BEZE ZMENY (zkopirovano z top.v se stejnymi jmeny instanci kvuli pins.cst a SDC): tdc_chan (u_tdca/u_tdcb),
//     hlidac 100 MHz, nabeh a opakovani kalibrace, ref_guard, hlidac koherence, LED FPGA_OK.
// ⬜ NEOVERENO NA DESCE ani v P&R (Gowin jen na Windows). Simulace: sim/tb_top_v3.sv (run.ps1 test top_v3).
// ============================================================

module top_v3 (
    input  wire clk_ref_10m,
    input  wire clk_p0_100m,
    input  wire ch_a,
    input  wire ch_b,
    output reg  led_tx,
    output reg  fpga_ok,
    output wire ref_ctrl,
    input  wire ext_ref_sens,
    input  wire gps_1pps,
    input  wire spi_sck,
    input  wire spi_cs_n,
    input  wire spi_mosi,
    output wire spi_miso,
    // FW 0x0505: vestavena PSRAM GW1NR-9 (2 cipy x8; pouziva se cip 0 = 4 MB). Vyhrazene vyvody pouzdra -- jmena portu
    // podle IP Gowin (O_psram_* / IO_psram_*), v pins.cst se nepriradi [OVERIT P&R].
    output wire [1:0]  O_psram_ck, O_psram_ck_n, O_psram_cs_n, O_psram_reset_n,
    inout  wire [1:0]  IO_psram_rwds,
    inout  wire [15:0] IO_psram_dq
`ifdef PIN86_BL_OFF
    , output wire bl_pwm
`endif
);
    localparam CLK_MHZ = 100;
`ifndef OSC_DIV
    localparam OSC_DIV = 8;
`else
    localparam OSC_DIV = `OSC_DIV;
`endif
    localparam [7:0] OSC_TMO = (((250 * 8) / OSC_DIV) > 255) ? 8'd255 : ((250 * 8) / OSC_DIV);
    // delka segmentu v taktech 100 MHz a kruh (simulace: segmenty 100x kratsi, volitelne mensi kruh)
`ifdef SIM_TOP
    localparam SEG25 = 25000, SEG10 = 10000;
`else
    localparam SEG25 = 2500000, SEG10 = 1000000;
`endif
`ifdef SIM_RAW
    localparam RAW = `SIM_RAW;
`else
    localparam RAW = 9;
`endif

    reg  [33:0] tick_hi = 34'd0;
    always @(posedge clk_p0_100m) tick_hi <= tick_hi + 34'd1;
    wire [47:0] tick_ps = {tick_hi, 14'd0};

    // GPS 1PPS: poradi a hruby cas posledni a predchozi hrany (hlavicka 68..107)
    wire [31:0] pps_seq;
    wire [33:0] pps_last, pps_prev;
    // FW 0x0502: jemny cas 1PPS (IDES8 v IOB pinu 33, rPLL 400 MHz), zverejneny spolu s hrubym
    wire        ppsf_tgl, ppsf_lock, pps_nofine;
    wire [47:0] ppsf_ts, pps_pts_l, pps_pts_p;
    wire [7:0]  pps_pfl_l, pps_pfl_p, pps_d_l, pps_d_p;
    wire [3:0]  pps_epoch;
    pps_ides3 u_ipps (.clk(clk_p0_100m), .pin(gps_1pps), .tick(tick_hi), .f_tgl(ppsf_tgl), .f_ts(ppsf_ts), .f_lock(ppsf_lock),
                      .epoch(pps_epoch));
    pps_cap3 u_pps (.clk(clk_p0_100m), .pin(gps_1pps), .tick(tick_hi), .f_tgl(ppsf_tgl), .f_ts(ppsf_ts), .f_lock(ppsf_lock),
                    .seq(pps_seq), .last(pps_last), .prev(pps_prev), .pts_l(pps_pts_l), .pts_p(pps_pts_p),
                    .pfl_l(pps_pfl_l), .pfl_p(pps_pfl_p), .d_l(pps_d_l), .d_p(pps_d_p), .nofine(pps_nofine));

`ifdef SIM_TOP
    localparam [31:0] BLD_TIME = 32'd0;
    localparam [31:0] BLD_GIT  = 32'd0;
`else
`include "build_id.vh"
`endif

    // ---- hlidac 100 MHz (z top.v beze zmeny) ----
    reg [4:0]  cdiv = 5'd0;
    always @(posedge clk_p0_100m) cdiv <= cdiv + 5'd1;
    reg [2:0]  cm_s   = 3'b000;
    reg [9:0]  cm_win = 10'd0;
    reg [10:0] cm_cnt = 11'd0;
    reg        clk_ok = 1'b0;
    reg        clk_flt = 1'b0;
    reg [7:0]  clk_loss = 8'd0;
    wire       cm_edge = cm_s[2] ^ cm_s[1];
    localparam [10:0] CM_EXP = (CLK_MHZ * 32) / 5;
    wire       cm_good = (cm_cnt >= CM_EXP - 11'd4) && (cm_cnt <= CM_EXP + 11'd4);
    always @(posedge clk_ref_10m) begin
        cm_s   <= {cm_s[1:0], cdiv[4]};
        cm_win <= cm_win + 10'd1;
        if (&cm_win) begin
            clk_ok <= cm_good;
            if (clk_ok && !cm_good) begin
                clk_flt <= 1'b1;
                if (clk_loss != 8'hFF) clk_loss <= clk_loss + 8'd1;
            end
            cm_cnt <= 11'd0;
        end else if (cm_edge) begin
            cm_cnt <= cm_cnt + 11'd1;
        end
    end
    reg [1:0]  ok_s = 2'b00;
    always @(posedge clk_p0_100m) ok_s <= {ok_s[0], clk_ok};

    // ---- nabeh (z top.v) ----
    reg [23:0] boot_cnt  = 24'd0;
    reg        boot_done = 1'b0;
    reg        cal_boot  = 1'b0;
    reg        boot_tc   = 1'b0;   // boot_cnt == konec (registrovane porovnani s konec - 1, rozlozeni 2026-10-23)
    always @(posedge clk_p0_100m) begin
        cal_boot <= 1'b0;
        boot_tc  <= 1'b0;
        if (!ok_s[1]) begin
            boot_cnt  <= 24'd0;
            boot_done <= 1'b0;
        end else if (!boot_done) begin
            boot_cnt <= boot_cnt + 24'd1;
`ifdef SIM_TOP
            boot_tc  <= (boot_cnt == 24'd1999);
`else
            boot_tc  <= (boot_cnt == 24'hFFFFFE);
`endif
            if (boot_tc) begin boot_done <= 1'b1; cal_boot <= 1'b1; end
        end
    end

`ifdef SIM_TOP
    localparam CAL_LOG2 = 12;
`else
    localparam CAL_LOG2 = 20;
`endif
    localparam TDC_STRIDE = 1;
    localparam TDC_DUAL   = 0;
    localparam TDC_TAPS   = (TDC_STRIDE == 1) ? 288 : 256;
    localparam TDC_NTAP   = (TDC_STRIDE == 1) ? 268 : 256;
    localparam TDC_KD     = (TDC_STRIDE == 1) ? 1 : 2;
`ifdef SIM_SUM_FAB
    localparam SUM_DSP    = 0;   // simulace: porovnani s dosavadnimi soucty v logice
`else
    localparam SUM_DSP    = 1;   // FW 0x0504: soucty segmentu v DSP (ALU54D, rozlozeni: radek R19)
`endif
    localparam TDC_TSPIPE = 1;   // znacka pres registr navic (rozlozeni, audit 2026-10-23; ts_valid T+10, FIFO seg_chan3 snese)
    wire use_ro_a, use_ro_b, ro_sig;
    ring_osc u_ro (.en(use_ro_a | use_ro_b), .out(ro_sig));

    reg [28:0] cal_tmo   = 29'd0;
    reg        cal_abort = 1'b0;
    reg        tmo_h = 1'b0, tmo_l = 1'b0;
    always @(posedge clk_p0_100m) begin
        cal_abort <= 1'b0;
        tmo_h <= &cal_tmo[28:15];
        tmo_l <= &cal_tmo[14:0];
        if (use_ro_a | use_ro_b) begin
            cal_tmo <= cal_tmo + 29'd1;
            if (tmo_h & tmo_l) cal_abort <= 1'b1;
        end else begin
            cal_tmo <= 29'd0;
        end
    end

    // ---- TDC ----
    wire        rise_a, rise_b, trig_a, trig_b, tsv_a, tsv_b;
    wire        rise_sa, rise_sb;
    wire [47:0] ts_a, ts_b;
    wire [8:0]  code_a, code_b;
    wire        bub_a, bub_b;
    wire        cal_busy_a, cal_busy_b, cal_valid_a, cal_valid_b, cal_fail_a, cal_fail_b;
    wire [31:0] da_ovf, da_peak, db_ovf, db_peak;
    wire [15:0] da_nz, da_last, db_nz, db_last;
    wire [15:0] da_maxtap, db_maxtap;
    wire [9:0]  hist_k;
    wire [23:0] hq_a, hq_b;
    wire        cal_wait_a, cal_wait_b;
    wire        lw_tgl, lw_ch, lw_commit_tgl;
    wire [1:0]  lw_cm_mask;
    wire [9:0]  lw_a;  wire [13:0] lw_neg;
    wire [9:0]  lw_a_q;  wire [13:0] lw_n_q;
    wire        lw_en_a, lw_en_b, lw_cm_p;
    lut_wr_sync u_lws (.clk(clk_p0_100m), .lw_tgl(lw_tgl), .lw_commit_tgl(lw_commit_tgl), .lw_ch(lw_ch), .lw_a(lw_a), .lw_neg(lw_neg),
                       .lw_en_a(lw_en_a), .lw_en_b(lw_en_b), .lw_cm_p(lw_cm_p), .lw_a_q(lw_a_q), .lw_n_q(lw_n_q));
    wire        want_a, want_b, cal_req_a, cal_req_b, seg_tick, bank_a, bank_b;
    // FW 0x0501 (SET_CONFIG 0x09): zdroj kanalu B. bit0 = pin A misto pinu B, bit1 = invertovat (retez casuje jen nabezne
    // hrany -> invertovany signal = sestupne hrany). A nabezne + B sestupne tehoz pinu = sirka pulzu bez prepojeni;
    // B = A neinvertovany = kalibrace zpozdeni mezi kanaly. Vyber z registru 100 MHz (z 10 MHz pres 2 FF, kvazistaticky);
    // cesta pin -> LUT -> retez je asynchronni jako dosud (SDC: false_path pres u_tdcb/sig_eff).
    wire [1:0]  bsrc;
    reg  [1:0]  bs1 = 2'b00, bs2 = 2'b00;
    always @(posedge clk_p0_100m) begin bs1 <= bsrc; bs2 <= bs1; end
    // FW 0x0503 (E5): predelicka kazdeho pinu (pin_cnt3 u IOB), log2 P z SET_CONFIG 0x07 (10 MHz -> 2 FF), plati od
    // hranice segmentu. Zdroj B z pinu A (SET_CONFIG 0x09) bere pin A primo (sirka pulzu / zpozdeni jen pri P = 1).
    wire [2:0]  pr_a_req, pr_b_req;
    reg  [2:0]  pa1 = 3'd0, pa2 = 3'd0, pb1 = 3'd0, pb2 = 3'd0, pr_a = 3'd0, pr_b = 3'd0;
    always @(posedge clk_p0_100m) begin
        pa1 <= pr_a_req; pa2 <= pa1; pb1 <= pr_b_req; pb2 <= pb1;
        if (seg_tick) begin pr_a <= pa2; pr_b <= pb2; end
    end
    wire        div_a, div_b;
    wire [15:0] prb_a, prb_b;
    pin_cnt3 u_pca (.pin(ch_a), .clk(clk_p0_100m), .sel(pr_a), .sig_out(div_a), .probe(prb_a));
    pin_cnt3 u_pcb (.pin(ch_b), .clk(clk_p0_100m), .sel(pr_b), .sig_out(div_b), .probe(prb_b));
    wire        sig_a = div_a;
    wire        sig_b = (bs2[0] ? ch_a : div_b) ^ bs2[1];
    // audit 2026-10-23 (F-0269): B z pinu A jde mimo predelicku (P = 1) -> log2 P v zaznamu B musi byt 0, jinak STM
    // nasobi X kanalu B starym P_B (STM sice P_B v tom rezimu stahne na 0, ale az po ozvene a dvou ramcich)
    wire [2:0]  pr_b_eff = bs2[0] ? 3'd0 : pr_b;
    // potvrzeni tabulky jen pro kanaly z masky (stridava kalibrace: druhy kanal muze prave cekat se starym histogramem)
    reg  [1:0]  cmk1 = 2'b11, cmk2 = 2'b11;
    always @(posedge clk_p0_100m) begin cmk1 <= lw_cm_mask; cmk2 <= cmk1; end

    tdc_chan #(.EXT_TR(0), .CAL_LOG2(CAL_LOG2), .TAPS(TDC_TAPS), .STRIDE(TDC_STRIDE), .KD(TDC_KD), .DUAL(TDC_DUAL), .NTAP(TDC_NTAP), .BANKS(1), .TSPIPE(TDC_TSPIPE)) u_tdca (
        .clk(clk_p0_100m), .sig_raw(sig_a), .ro(ro_sig), .tick_ps(tick_ps),
        .want(want_a), .want_seg(1'b0), .cal_req(cal_req_a), .cal_abort(cal_abort),
        .lw_en(lw_en_a), .lw_a(lw_a_q), .lw_neg(lw_n_q), .lw_commit(lw_cm_p & cmk2[0]), .cal_wait(cal_wait_a),
        .rise_c(rise_a), .rise_s(rise_sa), .trig_ack(trig_a), .ts_valid(tsv_a), .ts_ps(ts_a),
        .use_ro(use_ro_a), .cal_busy(cal_busy_a), .cal_valid(cal_valid_a), .cal_fail(cal_fail_a),
        .d_ovf(da_ovf), .d_peak(da_peak), .d_nz(da_nz), .d_last(da_last), .d_maxtap(da_maxtap),
        .dump_k(hist_k), .dump_q(hq_a), .code_o(code_a), .bub_o(bub_a), .tr1_ext(34'd0), .tr2_ext(34'd0),
        .bank_sw(seg_tick), .bank_o(bank_a)
    );
    tdc_chan #(.EXT_TR(0), .CAL_LOG2(CAL_LOG2), .TAPS(TDC_TAPS), .STRIDE(TDC_STRIDE), .KD(TDC_KD), .DUAL(TDC_DUAL), .NTAP(TDC_NTAP), .BANKS(1), .TSPIPE(TDC_TSPIPE)) u_tdcb (
        .clk(clk_p0_100m), .sig_raw(sig_b), .ro(ro_sig), .tick_ps(tick_ps),
        .want(want_b), .want_seg(1'b0), .cal_req(cal_req_b), .cal_abort(cal_abort),
        .lw_en(lw_en_b), .lw_a(lw_a_q), .lw_neg(lw_n_q), .lw_commit(lw_cm_p & cmk2[1]), .cal_wait(cal_wait_b),
        .rise_c(rise_b), .rise_s(rise_sb), .trig_ack(trig_b), .ts_valid(tsv_b), .ts_ps(ts_b),
        .use_ro(use_ro_b), .cal_busy(cal_busy_b), .cal_valid(cal_valid_b), .cal_fail(cal_fail_b),
        .d_ovf(db_ovf), .d_peak(db_peak), .d_nz(db_nz), .d_last(db_last), .d_maxtap(db_maxtap),
        .dump_k(hist_k), .dump_q(hq_b), .code_o(code_b), .bub_o(bub_b), .tr1_ext(34'd0), .tr2_ext(34'd0),
        .bank_sw(seg_tick), .bank_o(bank_b)
    );

    // ---- kalibrace: nabeh (oba), pozadavek STM (maska), opakovani po selhani -- STRIDAVE (cal_seq3) ----
    wire cal_tgl;  wire [1:0] cal_mask;
    reg  [2:0] ct_s = 3'b000;
    reg  [1:0] cm1 = 2'b00, cm2 = 2'b00;
    always @(posedge clk_p0_100m) begin ct_s <= {ct_s[1:0], cal_tgl}; cm1 <= cal_mask; cm2 <= cm1; end
    wire cmd_go  = ct_s[2] ^ ct_s[1];
    wire cmd_a   = cmd_go & ((cm2 == 2'd0) | cm2[0]);
    wire cmd_b   = cmd_go & ((cm2 == 2'd0) | cm2[1]);
    // opakovani po selhani (jako v2 FW 0x0422): po ~8 ms znovu, nejvys 15x za sebou, jen selhany kanal
    wire cfail = cal_fail_a | cal_fail_b;
    reg        cfail_q = 1'b0, rt_run = 1'b0, rt_go = 1'b0;
    reg [19:0] rt_dly  = 20'd0;
    reg        rt_tc   = 1'b0;   // rt_dly == 2^20 - 1 (registrovane porovnani, rozlozeni 2026-10-23)
    reg [3:0]  rt_n    = 4'd0;
    reg [1:0]  rt_tot  = 2'd0;
    always @(posedge clk_p0_100m) begin
        cfail_q <= cfail;
        rt_go   <= 1'b0;
        if (cfail & ~cfail_q & (rt_n != 4'hF)) rt_run <= 1'b1;
        rt_tc   <= rt_run & (rt_dly == 20'hFFFFE);
        if (rt_run) begin
            rt_dly <= rt_dly + 20'd1;
            if (rt_tc) begin
                rt_run <= 1'b0; rt_go <= 1'b1; rt_n <= rt_n + 4'd1;
                if (rt_tot != 2'd3) rt_tot <= rt_tot + 2'd1;
            end
        end
        if (cal_valid_a & cal_valid_b & ~cfail) rt_n <= 4'd0;
    end
    cal_seq3 u_cseq (.clk(clk_p0_100m),
        .go_a(cal_boot | cmd_a | (rt_go & cal_fail_a)), .go_b(cal_boot | cmd_b | (rt_go & cal_fail_b)),
        .busy_a(cal_busy_a), .busy_b(cal_busy_b), .req_a(cal_req_a), .req_b(cal_req_b));

    // ---- mereni: oba kanaly vzdy; hold = kanal nemeri platne ----
    reg hold_a = 1'b1, hold_b = 1'b1;
    // FW 0x0501: po zmene zdroje kanalu B je B 2 hranice segmentu neplatny (rozpracovany segment meri jiny signal; X a
    // kontrolni pocitani se srovnaji az v dalsim segmentu)
    reg  [1:0] bs3 = 2'b00, bchg = 2'd0;
    // FW 0x0503: po zmene predelicky 2 hranice segmentu neplatne (prepnuti vystupu muze dat hranu navic, meritko X)
    reg  [2:0] pra_d = 3'd0, prb_d = 3'd0;
    reg  [1:0] pchg_a = 2'd0, pchg_b = 2'd0;
    always @(posedge clk_p0_100m) begin
        pra_d <= pr_a; prb_d <= pr_b;
        if (pra_d != pr_a) pchg_a <= 2'd2; else if (seg_tick && pchg_a != 2'd0) pchg_a <= pchg_a - 2'd1;
        if (prb_d != pr_b) pchg_b <= 2'd2; else if (seg_tick && pchg_b != 2'd0) pchg_b <= pchg_b - 2'd1;
    end
    always @(posedge clk_p0_100m) begin
        bs3 <= bs2;
        if (bs3 != bs2)                   bchg <= 2'd2;
        else if (seg_tick && bchg != 2'd0) bchg <= bchg - 2'd1;
    end
    always @(posedge clk_p0_100m) begin
        hold_a <= cal_busy_a | ~cal_valid_a | ~boot_done | (pchg_a != 2'd0);
        hold_b <= cal_busy_b | ~cal_valid_b | ~boot_done | (bchg != 2'd0) | (pchg_b != 2'd0);
    end
    wire mask_cs_en, led_hb_en;
    reg  [1:0] csa_s = 2'b00, mk_s = 2'b00;
    always @(posedge clk_p0_100m) begin
        csa_s <= {csa_s[0], ~spi_cs_n};
        mk_s  <= {mk_s[0], mask_cs_en};
    end
    // casovat kazdou hranu, kterou TDC stihne; pri aktivnim CS volitelne ne (ruseni SPI, O4)
    assign want_a = ~hold_a & ~(mk_s[1] & csa_s[1]);
    assign want_b = ~hold_b & ~(mk_s[1] & csa_s[1]);

    wire        freeze, snap, clear, rec_tgl, seg10_act, seg10_req;
    wire [31:0] seg_idx;
    wire [23:0] a_n, b_n;  wire [39:0] a_x0, b_x0;  wire [47:0] a_t0, b_t0, a_sx, b_sx;  wire [55:0] a_st, b_st;
    wire [7:0]  a_fl, b_fl, a_bub, b_bub;  wire [3:0] a_ovl, b_ovl;
    wire [2:0]  a_pr, b_pr;
    seg_chan3 #(.SUM_DSP(SUM_DSP)) u_sa (.clk(clk_p0_100m), .rise_s(rise_sa), .cnt_en(~use_ro_a), .trig_ack(trig_a), .ts_valid(tsv_a), .ts_ps(ts_a),
        .bub(bub_a), .cs_act(csa_s[1]), .hold(hold_a), .sig_pin(sig_a), .freeze(freeze), .snap(snap), .clear(clear), .presc(pr_a),
        .o_n(a_n), .o_x0(a_x0), .o_t0(a_t0), .o_sx(a_sx), .o_st(a_st), .o_flags(a_fl), .o_bub(a_bub), .o_ovl(a_ovl), .o_presc(a_pr));
    seg_chan3 #(.SUM_DSP(SUM_DSP)) u_sb (.clk(clk_p0_100m), .rise_s(rise_sb), .cnt_en(~use_ro_b), .trig_ack(trig_b), .ts_valid(tsv_b), .ts_ps(ts_b),
        .bub(bub_b), .cs_act(csa_s[1]), .hold(hold_b), .sig_pin(sig_b), .freeze(freeze), .snap(snap), .clear(clear), .presc(pr_b_eff),
        .o_n(b_n), .o_x0(b_x0), .o_t0(b_t0), .o_sx(b_sx), .o_st(b_st), .o_flags(b_fl), .o_bub(b_bub), .o_ovl(b_ovl), .o_presc(b_pr));

    wire [RAW+2:0] ring_raddr;  wire [31:0] ring_rdata;
    v3_ring #(.RAW(RAW), .SEG25(SEG25), .SEG10(SEG10)) u_ring (.clk(clk_p0_100m), .seg10_req(seg10_req), .seg10_act(seg10_act),
        .seg_tick(seg_tick), .freeze(freeze), .snap(snap), .clear(clear), .rec_tgl(rec_tgl), .seg_idx(seg_idx),
        .a_n(a_n), .b_n(b_n), .a_x0(a_x0), .b_x0(b_x0), .a_t0(a_t0), .b_t0(b_t0), .a_sx(a_sx), .b_sx(b_sx),
        .a_st(a_st), .b_st(b_st), .a_fl(a_fl), .b_fl(b_fl), .a_p(a_pr), .b_p(b_pr),
        .rclk(clk_ref_10m), .raddr(ring_raddr), .rdata(ring_rdata));

    // diagnostika posledniho segmentu do hlavicky (zachyceno pri nulovani, cte se zive)
    reg [7:0] lb_a = 8'd0, lb_b = 8'd0;  reg [3:0] lo_a = 4'd0, lo_b = 4'd0;
    always @(posedge clk_p0_100m) if (clear) begin lb_a <= a_bub; lb_b <= b_bub; lo_a <= a_ovl; lo_b <= b_ovl; end

    // RAW_CAPTURE
    wire raw_tgl, raw_ch;  wire [8:0] raw_k;  wire [47:0] raw_q;  wire [9:0] raw_n;
    // FW 0x0505: hluboky RAW zaznam do PSRAM (rezim 1) a cteni po oknech 512 znacek (rezim 2)
    wire [1:0]  raw_mode;  wire [23:0] raw_arg, raw_dn;  wire [7:0] raw_dst;  wire [5:0] raw_wsel;  wire [15:0] raw_win;  wire [7:0] raw_dack;
`ifdef SIM_TOP
    localparam PS_INIT = 2000;
`else
    localparam PS_INIT = 15000;   // 150 us (tVCS)
`endif
    raw_cap3 #(.PS_INIT(PS_INIT)) u_raw (.clk(clk_p0_100m), .req_tgl(raw_tgl), .req_ch(raw_ch), .req_mode(raw_mode), .req_arg(raw_arg),
        .tv_a(tsv_a & ~hold_a), .tv_b(tsv_b & ~hold_b), .gap_a(~want_a), .gap_b(~want_b),
        .ts_a(ts_a), .ts_b(ts_b), .n(raw_n), .dn(raw_dn), .dst(raw_dst), .dwsel(raw_wsel), .dwin(raw_win), .dack(raw_dack),
        .rclk(clk_ref_10m), .rk(raw_k), .rq(raw_q),
        .ps_ck(O_psram_ck[0]), .ps_ck_n(O_psram_ck_n[0]), .ps_cs_n(O_psram_cs_n[0]), .ps_rst_n(O_psram_reset_n[0]),
        .ps_dq(IO_psram_dq[7:0]), .ps_rwds(IO_psram_rwds[0]));
    assign O_psram_ck[1] = 1'b0; assign O_psram_ck_n[1] = 1'b1; assign O_psram_cs_n[1] = 1'b1; assign O_psram_reset_n[1] = 1'b0;
    assign IO_psram_dq[15:8] = 8'hzz; assign IO_psram_rwds[1] = 1'bz;   // cip 1 se nepouziva

    // ---- stav do 10 MHz (2FF, urovne) ----
    reg [1:0] cva_s = 2'b00, cvb_s = 2'b00, cfa_s = 2'b00, cfb_s = 2'b00, cba_s = 2'b00, cbb_s = 2'b00;
    reg [1:0] cwa_s = 2'b00, cwb_s = 2'b00, bka_s = 2'b00, bkb_s = 2'b00, s10_s = 2'b00;
    reg [1:0] rt1 = 2'b00, rt2 = 2'b00;
    always @(posedge clk_ref_10m) begin
        cva_s <= {cva_s[0], cal_valid_a};  cvb_s <= {cvb_s[0], cal_valid_b};
        cfa_s <= {cfa_s[0], cal_fail_a};   cfb_s <= {cfb_s[0], cal_fail_b};
        cba_s <= {cba_s[0], cal_busy_a};   cbb_s <= {cbb_s[0], cal_busy_b};
        cwa_s <= {cwa_s[0], cal_wait_a};   cwb_s <= {cwb_s[0], cal_wait_b};
        bka_s <= {bka_s[0], bank_a};       bkb_s <= {bkb_s[0], bank_b};
        s10_s <= {s10_s[0], seg10_act};
        rt1 <= rt_tot; rt2 <= rt1;
    end
    // TDC_STATUS (kap. 3.1): bit0 kalibrace platna, bit1 selhala, bit2 sber histogramu, bit3 retez kratky (v3: pocita STM
    // z histogramu, zde 0), bit4 aktivni banka
    wire [7:0] tdc_st_a = {3'd0, bka_s[1], 1'b0, cba_s[1], cfa_s[1], cva_s[1]};
    wire [7:0] tdc_st_b = {3'd0, bkb_s[1], 1'b0, cbb_s[1], cfb_s[1], cvb_s[1]};
    localparam [8:0] TDC_TAPS9 = TDC_NTAP;
    localparam [4:0] TDC_AW5   = $clog2(TDC_TAPS) + TDC_DUAL;
    wire [15:0] tdc_cfg = {TDC_TAPS9, (TDC_STRIDE == 1) ? 1'b1 : 1'b0, (TDC_DUAL != 0) ? 1'b1 : 1'b0, TDC_AW5};

    reg [1:0]  bd_s = 2'b00;
    always @(posedge clk_ref_10m) bd_s <= {bd_s[0], boot_done};
    wire ref_act, ref_ext_pres, ref_ext_lost;
    reg [1:0] ra_s = 2'b00, rp_s = 2'b00, rl_s = 2'b00;
    always @(posedge clk_ref_10m) begin ra_s <= {ra_s[0], ref_act}; rp_s <= {rp_s[0], ref_ext_pres}; rl_s <= {rl_s[0], ref_ext_lost}; end
    wire [7:0] clk_status = {2'd0, rl_s[1], rp_s[1], ra_s[1], ~bd_s[1], clk_flt, clk_ok};

    // ---- SPI PHY v3 + aplikace ----
    wire          frame_end_tgl, tx_half, tx_we, phy_busy, phy_base, rx_len_ok, rx_rd_half;
    wire [10:0]   tx_waddr;  wire [7:0] tx_wdata;
    wire [7:0]    rx_b0, rx_b1, rx_b2, rx_p0, rx_p1, rx_p2, rx_p3, rx_rdata;
    wire [31:0]   rx_seq;  wire [3:0] rx_blk_ok;  wire [9:0] rx_raddr;  wire [15:0] dbg_sck_cnt;
    spi_slave_phy_v3 u_phy (.clk(clk_p0_100m), .clk_app(clk_ref_10m), .sck_pin(spi_sck), .cs_pin(spi_cs_n), .mosi_pin(spi_mosi),
        .miso(spi_miso), .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata), .tx_base(phy_base),
        .busy(phy_busy), .rx_b0(rx_b0), .rx_b1(rx_b1), .rx_b2(rx_b2), .rx_seq(rx_seq),
        .rx_p0(rx_p0), .rx_p1(rx_p1), .rx_p2(rx_p2), .rx_p3(rx_p3), .rx_blk_ok(rx_blk_ok), .rx_len_ok(rx_len_ok),
        .frame_end_tgl(frame_end_tgl), .rx_rd_half(rx_rd_half), .rx_raddr(rx_raddr), .rx_rdata(rx_rdata),
        .dbg_sck_cnt(dbg_sck_cnt));
    reg [2:0] fe_s = 3'b000;
    always @(posedge clk_ref_10m) fe_s <= {fe_s[1:0], frame_end_tgl};
    wire rx_valid_pulse = fe_s[2] ^ fe_s[1];

    wire ref_sel;
    wire [7:0] coh_st, coh_cnt;
    app_v3 #(.BUILD_TIME(BLD_TIME), .BUILD_GIT(BLD_GIT), .RAW(RAW), .CAL_LOG2(CAL_LOG2)) u_app (
        .clk(clk_ref_10m),
        .rx_valid(rx_valid_pulse), .rx_b0(rx_b0), .rx_b1(rx_b1), .rx_b2(rx_b2), .rx_seq(rx_seq),
        .rx_p0(rx_p0), .rx_p1(rx_p1), .rx_p2(rx_p2), .rx_p3(rx_p3), .rx_blk_ok(rx_blk_ok), .rx_len_ok(rx_len_ok),
        .rx_raddr(rx_raddr), .rx_rdata(rx_rdata),
        .tx_half(tx_half), .tx_we(tx_we), .tx_waddr(tx_waddr), .tx_wdata(tx_wdata), .phy_busy(phy_busy), .phy_base(phy_base),
        .rec_tgl(rec_tgl), .ring_raddr(ring_raddr), .ring_rdata(ring_rdata),
        .st_clk(clk_status), .st_clk_loss(clk_loss), .st_coh(coh_st), .st_coh_cnt(coh_cnt),
        .st_tdc_a(tdc_st_a), .st_tdc_b(tdc_st_b), .st_cal_wait({cwb_s[1], cwa_s[1]}), .st_cal_run(cba_s[1] | cbb_s[1]),
        .st_seg10(s10_s[1]),
        .pps_seq(pps_seq), .pps_last(pps_last), .pps_prev(pps_prev),
        .pps_pts_l(pps_pts_l), .pps_pts_p(pps_pts_p), .pps_pfl_l(pps_pfl_l), .pps_pfl_p(pps_pfl_p),
        .pps_d_l(pps_d_l), .pps_d_p(pps_d_p), .pps_st({pps_epoch, 1'b0, pps_nofine, (pps_seq != 32'd0), ppsf_lock}),
        .bub_a(lb_a), .bub_b(lb_b), .ovl_a(lo_a), .ovl_b(lo_b),
        .tdc_cfg(tdc_cfg), .maxtap_a(da_maxtap), .maxtap_b(db_maxtap), .cal_retry(rt2),
        .hist_k(hist_k), .hist_a(hq_a), .hist_b(hq_b),
        .raw_k(raw_k), .raw_q(raw_q), .raw_n(raw_n),
        .cal_tgl(cal_tgl), .cal_mask(cal_mask), .ref_sel(ref_sel), .mask_cs_en(mask_cs_en), .led_hb_en(led_hb_en),
        .seg10(seg10_req), .raw_tgl(raw_tgl), .raw_ch(raw_ch),
        .raw_mode(raw_mode), .raw_arg(raw_arg), .raw_dn(raw_dn), .raw_dst(raw_dst), .raw_wsel(raw_wsel), .raw_win(raw_win), .raw_dack(raw_dack),
        .lw_tgl(lw_tgl), .lw_a(lw_a), .lw_neg(lw_neg), .lw_ch(lw_ch), .lw_commit_tgl(lw_commit_tgl), .lw_commit_mask(lw_cm_mask), .bsrc(bsrc),
        .pr_a_req(pr_a_req), .pr_b_req(pr_b_req), .pr_a_act(pr_a), .pr_b_act(pr_b), .probe_a(prb_a), .probe_b(prb_b));

    // ---- reference (ref_guard z vnitrniho oscilatoru, z top.v) ----
    wire osc_clk;
    OSC #(.FREQ_DIV(OSC_DIV), .DEVICE("GW1NR-9C")) u_osc (.OSCOUT(osc_clk));
    ref_guard #(.PRES_TMO(OSC_TMO)) u_refg (.osc_clk(osc_clk), .ext_sens(ext_ref_sens), .sel_req(ref_sel), .ref_ctrl(ref_ctrl),
                      .ext_present(ref_ext_pres), .ext_lost(ref_ext_lost));
    assign ref_act = ref_ctrl;

    // ---- LED heartbeat: preklopeni kazdych 10 dvojic zaznamu (250 ms pri L = 25 ms), jen na pozadani (SET_CONFIG 0x05) ----
    reg [2:0] lr_s = 3'b000;
    reg [3:0] lr_n = 4'd0;
    initial led_tx = 1'b1;
    always @(posedge clk_ref_10m) begin
        lr_s <= {lr_s[1:0], rec_tgl};
        if (!led_hb_en) led_tx <= 1'b1;
        else if (lr_s[2] ^ lr_s[1]) begin
            if (lr_n == 4'd9) begin lr_n <= 4'd0; led_tx <= ~led_tx; end
            else lr_n <= lr_n + 4'd1;
        end
    end

    // ---- hlidac koherence 100/10 MHz (z top.v) ----
    reg tgl10 = 1'b0;
    always @(posedge clk_ref_10m) tgl10 <= ~tgl10;
    wire [7:0] coh_gray;
    wire       coh_ok, coh_seen, coh_path, coh_valid;
    reg  [1:0]  rx_s = 2'b00;
    reg  [23:0] coh_hold = 24'hFFFFFF;
    always @(posedge clk_p0_100m) begin
        rx_s <= {rx_s[0], ref_act};
        if (rx_s[1] ^ rx_s[0])     coh_hold <= 24'hFFFFFF;
        else if (coh_hold != 24'd0) coh_hold <= coh_hold - 24'd1;
    end
    reg coh_hz = 1'b0, coh_en = 1'b0;
    always @(posedge clk_p0_100m) begin
        coh_hz <= (coh_hold == 24'd0);
        coh_en <= ok_s[1] & boot_done & ~rx_s[1] & coh_hz;
    end
    coh_mon u_coh (.clk(clk_p0_100m), .tgl10(tgl10), .en(coh_en),
                   .err_gray(coh_gray), .ep_ok(coh_ok), .err_seen(coh_seen), .path_n(coh_path), .valid(coh_valid));
    reg [7:0] cg_s1 = 8'd0, cg_s2 = 8'd0;
    reg [3:0] cst_s1 = 4'd0, cst_s2 = 4'd0;
    always @(posedge clk_ref_10m) begin
        cg_s1  <= coh_gray;  cg_s2  <= cg_s1;
        cst_s1 <= {coh_path, coh_seen, coh_ok, coh_valid}; cst_s2 <= cst_s1;
    end
    genvar gi;
    generate for (gi = 0; gi < 8; gi = gi + 1) begin : g2b
        assign coh_cnt[gi] = ^cg_s2[7:gi];
    end endgenerate
    assign coh_st = {4'd0, cst_s2};
`ifdef PIN86_BL_OFF
    assign bl_pwm = 1'b0;
`endif

    // ---- LED FPGA_OK (z top.v) ----
    wire good10 = clk_ok;
    reg [2:0] t10_s = 3'b000;
    reg [1:0] good_s = 2'b00;
    reg [7:0] t10_tmo = 8'd0;
    initial fpga_ok = 1'b0;
    always @(posedge osc_clk) begin
        t10_s  <= {t10_s[1:0], tgl10};
        good_s <= {good_s[0], good10};
        if (t10_s[2] ^ t10_s[1])      t10_tmo <= OSC_TMO;
        else if (t10_tmo != 8'd0)     t10_tmo <= t10_tmo - 8'd1;
        fpga_ok <= (t10_tmo != 8'd0) & good_s[1];
    end
endmodule
