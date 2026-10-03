// ============================================================
// File: top.v  (nová deska, dva symetrické kanály, SKUTEČNÝ carry-chain TDC)
//
// 🔴 2026-10-03: CH_A/CH_B měří čas hrany s rozlišením jednotek ps pomocí
// carry-chain TDC (`tdc.v`, modul `tdc_chan`) vzorkovaného clk_p0_100m.
// Nahradilo `carry_tdc` (syntéza ho zredukovala na invertor, F-0201..F-0203).
// Čas hrany je v PIKOSEKUNDÁCH (`tick_ps` + kalibrační tabulka), reciproký
// protokol (dt v rámci) jede v jednotkách T_clk/16384 = 0,6104 ps; kmitočet z (edge_count, dt) počítá
// HOST (frequency_x100000 v rámci = 0). Kalibrace (code density,
// ring oscilátor) běží sama po zapnutí a na požadavek STM (SET_CONFIG 0x02).
//
// Hradlo, SPI PHY a aplikace beze změny. CH_A = primární slot payloadu,
// CH_B = sekundární ("freq16") slot + dt_b_ps/edges_b v rezervě rámce.
//
// OMEZENÍ: v retězu (~14,6 ns) smí být jedna hrana => vstup do ~34 MHz
// (nad tím předdělička / počítání period, mimo tento krok). Λ/Ω regrese
// (S1/S2) odstraněna (viz spi_app.v).
//
// ✅ Simulace: sim/tb_tdc.sv (Icarus, model ALU s náhodným zpožděním).
// ⬜ NEOVĚŘENO NA KŘEMÍKU: skutečné zpoždění tapů, monotónnost a to, že
// ring oscilátor nebude zamknutý na 100 MHz, jde změřit jen na desce
// (STM `fpgaraw`/CAL report: ovf, nz, last).
// Piny: src/pins.cst + ../../citac_zadani_predavaci.md §4.
// ============================================================

module top (
    input  wire clk_ref_10m,     // REF_10MHz (PIN42), gate-window časování
    input  wire clk_p0_100m,     // REF_100MHz (PIN35, GCLKT_4 PRIMARY)

    input  wire ch_a,            // PIN25: kanál A (carry chain A)
    input  wire ch_b,            // PIN27: kanál B (carry chain B)

    output reg  led_tx,          // heartbeat (PIN29)

    // SPI slave (STM32 = master, Mode 0, active-low CS) -- beze změny
    input  wire spi_sck,
    input  wire spi_cs_n,
    input  wire spi_mosi,
    output wire spi_miso
);

    // ----------------------------------------------------------
    // P0 doména: volnoběžný čas v jednotkách T_clk/16384 (= 0,6104 ps),
    // +16384 za takt (mod 2^48 = 172 s). Nižších 14 bitů je konstantně 0.
    // ----------------------------------------------------------
    // +16384 za takt = 2^14 -> spodnich 14 bitu je konstantni 0, cita se jen horni cast
    // (kratsi carry, bez nerízených bitu [13:0]).
    reg  [33:0] tick_hi = 34'd0;
    always @(posedge clk_p0_100m) tick_hi <= tick_hi + 34'd1;
    wire [47:0] tick_ps = {tick_hi, 14'd0};

    // ----------------------------------------------------------
    // Hradlovací okno (SET_CONFIG: 100 ms / 250 ms / 1 s, default 250 ms).
    // Beze změny proti staré desce -- nezávisí na počtu fázových hodin.
    // ----------------------------------------------------------
    wire [1:0]  base_win;
    wire [23:0] gate_lim = (base_win == 2'd0) ? 24'd999999  :   // 100 ms
                           (base_win == 2'd2) ? 24'd9999999 :   // 1 s
                                                24'd2499999;    // 250 ms
    reg  [23:0] gtmr     = 24'd0;
    reg  [1:0]  bw_d     = 2'd1;
    reg         gate_tgl = 1'b0;
    always @(posedge clk_ref_10m) begin
        bw_d <= base_win;
        if (bw_d != base_win) begin
            gtmr <= 24'd0;                 // změna okna: začni čistě
        end else if (gtmr >= gate_lim) begin
            gtmr     <= 24'd0;
            gate_tgl <= ~gate_tgl;
        end else begin
            gtmr <= gtmr + 24'd1;
        end
    end
    reg [2:0] gt_s = 3'b000;
    always @(posedge clk_p0_100m) gt_s <= {gt_s[1:0], gate_tgl};
    wire gate_tick = (gt_s[2] ^ gt_s[1]);  // ~gate puls v P0

    // ----------------------------------------------------------
    // Kalibrace TDC: jeden ring oscilátor pro oba kanály. Spouští se po zapnutí
    // (~168 ms, ať se ustálí napájení/hodiny) a hranou cal_mode ze STM.
    // ----------------------------------------------------------
    reg [23:0] boot_cnt  = 24'd0;
    reg        boot_done = 1'b0;
    reg        cal_boot  = 1'b0;
    always @(posedge clk_p0_100m) begin
        cal_boot <= 1'b0;
        if (!boot_done) begin
            boot_cnt <= boot_cnt + 24'd1;
            if (&boot_cnt) begin boot_done <= 1'b1; cal_boot <= 1'b1; end
        end
    end
    wire cal_mode;                                   // z spi_app (10 MHz doména)
    reg [2:0] calm_s = 3'b000;
    always @(posedge clk_p0_100m) calm_s <= {calm_s[1:0], cal_mode};
    reg  cal_req = 1'b0;                                  // registrovano (kratke cesty)
    always @(posedge clk_p0_100m) cal_req <= (calm_s[1] & ~calm_s[2]) | cal_boot;

    // Pocet udalosti kalibrace = 2^CAL_LOG2. JEDINY zdroj: predava se do obou tdc_chan
    // i do meze OVF_LIM nize (2026-10-03: OVF_LIM mel natvrdo 22, kalibrace 20 ->
    // retez pokryvajici jen ~83 % periody se nenahlasil jako kratky).
    localparam CAL_LOG2 = 20;
    wire use_ro_a, use_ro_b, ro_sig;
    ring_osc u_ro (.en(use_ro_a | use_ro_b), .out(ro_sig));

    // Timeout kalibrace (RO nejde / udalosti nechodi): spolecny pro oba kanaly,
    // ~5,4 s (2^29 taktu) od zacatku sberu; sber 2^20 udalosti trva ~0,1-0,5 s (podle rychlosti kruhu).
    reg [28:0] cal_tmo   = 29'd0;
    reg        cal_abort = 1'b0;
    always @(posedge clk_p0_100m) begin
        cal_abort <= 1'b0;
        if (use_ro_a | use_ro_b) begin
            cal_tmo <= cal_tmo + 29'd1;
            if (&cal_tmo) cal_abort <= 1'b1;
        end else begin
            cal_tmo <= 29'd0;
        end
    end

    // ----------------------------------------------------------
    // 2x TDC kanál (tdc.v): hrana -> sig_rise + ev_ts [ps]
    // ----------------------------------------------------------
    wire        rise_a, rise_b, trig_a, trig_b, tsv_a, tsv_b, want_a, want_b;
    wire [47:0] ts_a, ts_b;
    wire        cal_busy_a, cal_busy_b, cal_valid_a, cal_valid_b, cal_fail_a, cal_fail_b;
    wire [31:0] da_ovf, da_peak, db_ovf, db_peak;
    wire [15:0] da_nz, da_last, db_nz, db_last;

    tdc_chan #(.CAL_LOG2(CAL_LOG2)) u_tdca (
        .clk(clk_p0_100m), .sig_raw(ch_a), .ro(ro_sig), .tick_ps(tick_ps),
        .want(want_a), .cal_req(cal_req), .cal_abort(cal_abort),
        .rise_c(rise_a), .trig_ack(trig_a), .ts_valid(tsv_a), .ts_ps(ts_a),
        .use_ro(use_ro_a), .cal_busy(cal_busy_a),
        .cal_valid(cal_valid_a), .cal_fail(cal_fail_a),
        .d_ovf(da_ovf), .d_peak(da_peak), .d_nz(da_nz), .d_last(da_last)
    );
    tdc_chan #(.CAL_LOG2(CAL_LOG2)) u_tdcb (
        .clk(clk_p0_100m), .sig_raw(ch_b), .ro(ro_sig), .tick_ps(tick_ps),
        .want(want_b), .cal_req(cal_req), .cal_abort(cal_abort),
        .rise_c(rise_b), .trig_ack(trig_b), .ts_valid(tsv_b), .ts_ps(ts_b),
        .use_ro(use_ro_b), .cal_busy(cal_busy_b),
        .cal_valid(cal_valid_b), .cal_fail(cal_fail_b),
        .d_ovf(db_ovf), .d_peak(db_peak), .d_nz(db_nz), .d_last(db_last)
    );

    // ----------------------------------------------------------
    // 2x windowed reciproký čítač (win_recip ze spi_app.v), čas v ps.
    // ----------------------------------------------------------
    wire [25:0] r_periods_a,  r_periods_b;
    wire [47:0] r_dt_a,       r_dt_b;
    wire        res_tgl_a,    res_tgl_b;
    wire        alias_a,      alias_b_nc;

    win_recip u_wra (
        .clk(clk_p0_100m), .rise_c(rise_a), .trig_ack(trig_a), .ts_valid(tsv_a), .ts_ps(ts_a),
        .gate_tick(gate_tick), .want(want_a),
        .hold(cal_busy_a | ~cal_valid_a),
        .r_periods(r_periods_a), .r_dt(r_dt_a), .r_dt_alias(alias_a), .res_tgl(res_tgl_a)
    );
    win_recip u_wrb (
        .clk(clk_p0_100m), .rise_c(rise_b), .trig_ack(trig_b), .ts_valid(tsv_b), .ts_ps(ts_b),
        .gate_tick(gate_tick), .want(want_b),
        .hold(cal_busy_b | ~cal_valid_b),
        .r_periods(r_periods_b), .r_dt(r_dt_b), .r_dt_alias(alias_b_nc), .res_tgl(res_tgl_b)
    );

    // ----------------------------------------------------------
    // CDC do 10 MHz: hrany res_tgl (beze změny proti staré desce)
    // ----------------------------------------------------------
    reg [2:0] resA_s = 3'b000, resB_s = 3'b000;
    always @(posedge clk_ref_10m) begin
        resA_s <= {resA_s[1:0], res_tgl_a};
        resB_s <= {resB_s[1:0], res_tgl_b};
    end
    wire res_valid_a = (resA_s[2] ^ resA_s[1]);
    wire res_valid_b = (resB_s[2] ^ resB_s[1]);

    reg [63:0] timestamp_10m = 64'd0;
    always @(posedge clk_ref_10m) timestamp_10m <= timestamp_10m + 64'd1;

    // Stav TDC do 10 MHz domény (úrovně, 2FF sync)
    reg [1:0] cva_s = 2'b00, cvb_s = 2'b00, cfl_s = 2'b00, cbs_s = 2'b00;
    always @(posedge clk_ref_10m) begin
        cva_s <= {cva_s[0], cal_valid_a};
        cvb_s <= {cvb_s[0], cal_valid_b};
        cfl_s <= {cfl_s[0], cal_fail_a | cal_fail_b};
        cbs_s <= {cbs_s[0], cal_busy_a | cal_busy_b};
    end
    // retez kratky: > 1/16 udalosti kalibrace za koncem retezu (kod 255)
    localparam [31:0] OVF_LIM = 32'd1 << (CAL_LOG2 - 4);
    wire short_a = (da_ovf > OVF_LIM);
    wire short_b = (db_ovf > OVF_LIM);
    wire [7:0] tdc_status = {2'b00, short_b, short_a, cbs_s[1], cfl_s[1], cvb_s[1], cva_s[1]};
    wire [191:0] cal_diag = {db_last, db_nz, db_peak, db_ovf, da_last, da_nz, da_peak, da_ovf};

    // ----------------------------------------------------------
    // Výsledky oken do 10 MHz domény. FPGA kmitočet NEPOČÍTÁ: rámec nese přesné
    // celé hodnoty (periods, dt_ps) a poměr dělá host. Jen gate_ns = dt_ps/1000
    // (gate_div = dt*5>>13) pro pole rámce abs 28; new_meas (valid_a) jde po něm.
    // ----------------------------------------------------------
    reg [31:0] periods_lat  = 32'd0;           // CH_A (latch při res_valid_a)
    reg [47:0] dt_a_lat     = 48'd0;
    reg [31:0] periods_b_hold = 32'd0;         // CH_B
    reg [47:0] dt_b_hold    = 48'd0;
    reg        dt_ovf_lat   = 1'b0;
    reg        dt_alias_lat = 1'b0;
    reg        gd_go        = 1'b0;
    always @(posedge clk_ref_10m) begin
        gd_go <= 1'b0;
        if (res_valid_a) begin
            periods_lat  <= {6'd0, r_periods_a};
            dt_a_lat     <= r_dt_a;
            dt_ovf_lat   <= r_dt_a[47];        // Δt >= 2^47 jednotek (~86 s)
            dt_alias_lat <= alias_a;
            gd_go        <= 1'b1;
        end
        if (res_valid_b) begin
            periods_b_hold <= {6'd0, r_periods_b};
            dt_b_hold      <= r_dt_b;
        end
    end

    wire [63:0] gate_ns_a;
    wire        valid_a, gd_busy;
    gate_div u_gd (.clk(clk_ref_10m), .start(gd_go), .dt(dt_a_lat),
                   .gate_ns(gate_ns_a), .valid(valid_a), .busy(gd_busy));

    wire [63:0] freq_a_x100000 = 64'd0;                 // host počítá (viz spi_app.v)
    wire        div_err_a      = (dt_a_lat == 48'd0);
    wire [63:0] freq_b_hold    = 64'd0;
    wire        err_b_hold     = (dt_b_hold == 48'd0);

    // watchdog ztráty signálu (CH_A): ~2,5 s bez měření -- beze změny
    reg [24:0] wdog        = 25'd0;
    reg        signal_lost = 1'b0;
    always @(posedge clk_ref_10m) begin
        if (valid_a) begin
            wdog        <= 25'd0;
            signal_lost <= 1'b0;
        end else if (wdog == 25'd25000000) begin
            signal_lost <= 1'b1;
        end else begin
            wdog <= wdog + 25'd1;
        end
    end

    // error_flags: bit0=Δt==0 (CH_A), bit2=Δt >= 2^47 ps, bit3=Δt alias,
    // bit4=TDC CH_A zatím nezkalibrován / probíhá kalibrace;
    // bit1=signal_lost přidává živě spi_app.
    wire [31:0] meas_err_flags = {31'd0, div_err_a} | {29'd0, dt_ovf_lat, 2'd0}
                               | {28'd0, dt_alias_lat, 3'd0}
                               | {27'd0, ~cva_s[1] | cbs_s[1], 4'd0};
    wire [7:0]  meas_status2   = {7'd0, err_b_hold};   // CH_B chyba v "status2" slotu
    wire [7:0]  meas_phase_zero = 8'd0;                // phase_status: mrtvé pole

    // ----------------------------------------------------------
    // SPI PHY (10 MHz) + aplikace -- protokol v2, rámec 128 B, NEZMĚNĚNO.
    // CH_A = primární slot (freq_x100000/periods/gate), CH_B = "/16" slot
    // (jen freq16_x100000) -- viz hlavička souboru, proč je to bezpečné
    // provizorium (sdílené hradlo, v3 migrace je samostatný krok).
    // ----------------------------------------------------------
    wire [1023:0] tx_frame_flat;
    wire [1023:0] rx_frame_flat;
    wire          frame_end_tgl;
    wire          tx_frame_valid;
    wire [7:0]    spi_status;
    wire [10:0]   rx_bit_count;

    // 🔴 CDC 2026-10-01: PHY prešla z clk_ref_10m na clk_p0_100m (10x vyssi
    // oversampling SCK -> spolehlivy strop ~20 MHz misto ~2 MHz, viz hlavicka
    // spi_slave_phy.v "Pozn. rychlost"). spi_app.v ZUSTAVA na clk_ref_10m
    // (FSM se nesaha) -- vznika tim hranice dvou domen presne tam, kde to
    // spi_slave_phy.v uz davno predpovidala ("pak je nutne osetrit CDC
    // ramcu vuci aplikaci").
    //
    // RX smer (PHY 100MHz -> app 10MHz) nepotreboval ZADNOU zmenu: fe_s/
    // rx_valid_pulse nize je UZ od zacatku 3-stupnovy synchronizer toggle
    // bitu frame_end_tgl - fungoval spravne i driv (kdy byl synchronizace
    // v ramci STEJNE domeny, tedy no-op), a je to presne spravny vzor i
    // pro SKUTECNOU cross-domain hranici. rx_frame_flat (=PHY rx_shadow)
    // je stabilni od CS^ do dalsiho CS v (>=25us mezera mezi ramci = 250
    // taktu clk_ref_10m), tedy s obrovskou rezervou nad potrebu synchronizace.
    //
    // TX smer (app 10MHz -> PHY 100MHz) synchronizaci DRIV nepotreboval
    // (stejna domena) a ted ano: tx_frame_valid (=spi_app `frame_ok`) jde
    // pres novy 3-stupnovy LEVEL synchronizer (txv_s, stejny vzor jako
    // fe_s nize i PHY vlastni sck_s/cs_s/mosi_s) do PHY domeny.
    // tx_frame_flat SAMOTNY (1024b) se NESYNCHRONIZUJE (kombinacni vodic
    // beze zmeny) -- bezpecne jen diky tomu, ze spi_app.v ma novy mezistav
    // S_TX_ARM: frame_ok padne CELY JEDEN takt clk_ref_10m (100 ns = 10
    // taktu @100MHz) PRED tím, nez se tx_b[] zacne prepisovat. To dava
    // 3-stupnovemu synchronizeru (max 3 takty @100MHz = 30 ns zpozdeni
    // do ustaleni) ~3,3x rezervu PRED tím, nez by data mohla byt torn.
    // Novy 1024b zachytavaci registr by se NEVESEL (registry FPGA jsou
    // na 79 %, 5245/6693) -- proto tenhle levnejsi navrh bez duplikace ramce.
    reg [2:0] txv_s = 3'b000;
    always @(posedge clk_p0_100m) txv_s <= {txv_s[1:0], tx_frame_valid};
    wire tx_valid_sync = txv_s[2];

    spi_slave_phy u_phy (
        .clk(clk_p0_100m),
        .sck_pin(spi_sck),
        .cs_pin(spi_cs_n),
        .mosi_pin(spi_mosi),
        .miso(spi_miso),
        .tx_frame_flat(tx_frame_flat),
        .tx_valid(tx_valid_sync),
        .rx_frame_flat(rx_frame_flat),
        .frame_end_tgl(frame_end_tgl),
        .rx_bit_count(rx_bit_count)
    );

    // DIAGNOSTIKA vstupu SPI (2026-10-03): kolik hran MOSI a nabeznych hran SCK FPGA
    // videla v poslednim ramci (pocitano v CS=0, latch pri CS rise). Rozlisi
    // "pin nic nevidi" (hrany MOSI = 0) od chyby zachyceni bitu v PHY.
    reg [2:0] dm_mosi = 3'b000, dm_sck = 3'b000, dm_cs = 3'b111;
    reg [15:0] dm_mc = 16'd0, dm_sc = 16'd0, dm_mlat = 16'd0, dm_slat = 16'd0;
    always @(posedge clk_p0_100m) begin
        dm_mosi <= {dm_mosi[1:0], spi_mosi};
        dm_sck  <= {dm_sck[1:0],  spi_sck};
        dm_cs   <= {dm_cs[1:0],   spi_cs_n};
        if (dm_cs[2:1] == 2'b10) begin dm_mc <= 16'd0; dm_sc <= 16'd0; end
        else if (!dm_cs[2]) begin
            if (dm_mosi[2] ^ dm_mosi[1])        dm_mc <= dm_mc + 16'd1;
            if (dm_sck[2:1] == 2'b01)           dm_sc <= dm_sc + 16'd1;
        end
        if (dm_cs[2:1] == 2'b01) begin dm_mlat <= dm_mc; dm_slat <= dm_sc; end
    end

    reg [2:0] fe_s = 3'b000;
    always @(posedge clk_ref_10m)
        fe_s <= {fe_s[1:0], frame_end_tgl};
    wire rx_valid_pulse = (fe_s[2] ^ fe_s[1]);

    spi_app u_app (
        .clk(clk_ref_10m),
        .meas_freq_x100000(freq_a_x100000),
        .meas_periods(periods_lat),
        .meas_gate_ns(gate_ns_a),
        .meas_timestamp(timestamp_10m),
        .meas_error_flags(meas_err_flags),
        .meas_channel(8'd0),
        .new_meas(valid_a),
        .signal_lost(signal_lost),
        .meas_freq16_x100000(freq_b_hold),
        .meas_phase_status(meas_phase_zero),
        .meas_status2(meas_status2),
        .meas_dt_a_ps(dt_a_lat),
        .meas_dt_b_ps(dt_b_hold),
        .meas_periods_b(periods_b_hold),
        .meas_tdc_status(tdc_status),
        .meas_cal_diag(cal_diag),
        .dbg_mosi_cnt(dm_mlat),
        .dbg_sck_cnt(dm_slat),
        .cal_mode(cal_mode),
        .base_win(base_win),
        .rx_frame_flat(rx_frame_flat),
        .rx_valid(rx_valid_pulse),
        .tx_frame_flat(tx_frame_flat),
        .tx_valid(tx_frame_valid),
        .dbg_status(spi_status)
    );

    // ----------------------------------------------------------
    // LED heartbeat: toggle na každé dokončené měření CH_A (~2-4 Hz blik).
    // ----------------------------------------------------------
    initial led_tx = 1'b1;
    always @(posedge clk_ref_10m)
        if (valid_a) led_tx <= ~led_tx;

endmodule
