// ============================================================
// File: top.v  (LEAN, 4-phase oversampling reciprocal + kalibrace)
// - Kmitočet měřen lean 4fázovým oversampling čítačem:
//   počítá KAŽDOU hranu (bez mrtvé doby) do ~40 MHz na pinu, 2,5 ns značka.
// - Dva předdělené vstupy: pin 28 = f/4 (primární), pin 27 = f/16 (rezerva).
// - Reciproké okno 0,25 s: f = periods/Δt přímo ve FPGA (recip_calc).
// - Λ/Ω regresní akumulátory (S1=Σt, S2=ΣS1) přes VŠECHNY hrany okna:
//   STM z nich spočte LSQ slope -> kvantizace i nelinearita binů se
//   průměrují ~sqrt(N) => prakticky teplotně nezávislé měření.
// - Histogram fine kódů běží trvale (background tracking šířek binů);
//   vestavěný ring oscilátor = async zdroj hustoty kódů bez signálu.
// - div_res (pin 53): MR preskaleru MC100EP016 - MUSÍ být LOW (v2.0!).
// - SPI SLAVE pro STM32H757 master: protokol v2 (128B rámec, CRC16,
//   DATA 0x80 / CAL report 0xA0, SET_CONFIG okno 0,1/0,25/1 s + cal_mode).
// - LED = heartbeat (toggle na každé dokončené měření pin28).
// Piny: carrier v2.0 (BOARD_V20_CHECKLIST.md); bench jumpery přepojit!
// ============================================================

module top (
    input  wire clk_ref_10m,     // pin 63 (10MHz_GPSDO_CLK4)

    input  wire clk_p0_100m,     // pin 35 = GCLKT_4 (0°, master)
    input  wire clk_p2p5_100m,   // pin 34 (90°)
    input  wire clk_p5_100m,     // pin 40 (180°)
    input  wire clk_p7p5_100m,   // pin 33 (270°)

    input  wire sig_in4,         // pin 28: měřený kmitočet / 4
    input  wire sig_in16,        // pin 27: měřený kmitočet / 16

    output reg  led_tx,          // heartbeat
    output wire div_res,         // pin 53: MC100EP016 MR (aktivní HIGH)

    // SPI slave (STM32 = master, Mode 0, active-low CS)
    input  wire spi_sck,
    input  wire spi_cs_n,
    input  wire spi_mosi,
    output wire spi_miso
);

    // Preskaler drž mimo reset (bez toho nečítá - Gowin default pull-up
    // by MR držel HIGH). TODO: řízený reset preskaleru přes SPI povel.
    assign div_res = 1'b0;

    // ----------------------------------------------------------
    // P0 doména: volnoběžná časová značka (10 ns/LSB)
    // ----------------------------------------------------------
    reg [31:0] tick_p0 = 32'd0;
    always @(posedge clk_p0_100m) tick_p0 <= tick_p0 + 32'd1;

    // ----------------------------------------------------------
    // Hradlovací okno (SET_CONFIG: 100 ms / 250 ms / 1 s, default 250 ms):
    // volnoběžný tick v 10 MHz -> sync do P0. base_win je ze stejné
    // 10MHz domény (spi_app) -> bez CDC; při změně restart čítače.
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
    wire gate_tick = (gt_s[2] ^ gt_s[1]);  // ~0,25 s puls v P0

    // ----------------------------------------------------------
    // Kalibrační mód: RO místo pin28 (povel CAL_START/STOP z STM).
    // cal_mode je kvazistatický (10M doména) -> 2FF sync do P0.
    // ----------------------------------------------------------
    wire cal_mode;
    reg [1:0] calm_s = 2'b00;
    always @(posedge clk_p0_100m) calm_s <= {calm_s[0], cal_mode};

    // Ring oscilátor (~25-40 MHz, /4 => ~6-10 MHz): asynchronní zdroj
    // pro code-density kalibraci binů i bez vstupního signálu.
    wire ro_out;
    ring_osc u_ro (.en(calm_s[1]), .out(ro_out));
    wire sig4_eff = calm_s[1] ? ro_out : sig_in4;

    // ----------------------------------------------------------
    // 2x 4fázový oversampler (pin 28 /4 [nebo RO], pin 27 /16)
    // ----------------------------------------------------------
    wire        rise4,  rise16;
    wire [1:0]  fine4,  fine16;

    phase_oversampler u_os4 (
        .clk_p0(clk_p0_100m), .clk_p2p5(clk_p2p5_100m),
        .clk_p5(clk_p5_100m), .clk_p7p5(clk_p7p5_100m),
        .sig_in(sig4_eff), .sig_rise(rise4), .fine(fine4)
    );
    phase_oversampler u_os16 (
        .clk_p0(clk_p0_100m), .clk_p2p5(clk_p2p5_100m),
        .clk_p5(clk_p5_100m), .clk_p7p5(clk_p7p5_100m),
        .sig_in(sig_in16), .sig_rise(rise16), .fine(fine16)
    );

    wire [33:0] ev_ts4  = {tick_p0, fine4};
    wire [33:0] ev_ts16 = {tick_p0, fine16};

    // ----------------------------------------------------------
    // 2x windowed reciproký čítač (P0); u_wr4 navíc Λ akumulátory
    // a fine kódy první/poslední hrany (pro LUT korekci v STM).
    // ----------------------------------------------------------
    wire [25:0] r_periods4,  r_periods16;
    wire [33:0] r_dt4,       r_dt16;
    wire        res_tgl4,    res_tgl16;
    wire [1:0]  ff4, fl4,    ff16_nc, fl16_nc;
    wire        alias4,      alias16_nc;
    wire [55:0] s1_4,        s1_16_nc;
    wire [79:0] s2_4,        s2_16_nc;

    win_recip u_wr4 (
        .clk(clk_p0_100m), .sig_rise(rise4), .ev_ts(ev_ts4), .gate_tick(gate_tick),
        .r_periods(r_periods4), .r_dt(r_dt4),
        .r_fine_first(ff4), .r_fine_last(fl4), .r_dt_alias(alias4),
        .r_s1(s1_4), .r_s2(s2_4),
        .res_tgl(res_tgl4)
    );
    win_recip u_wr16 (
        .clk(clk_p0_100m), .sig_rise(rise16), .ev_ts(ev_ts16), .gate_tick(gate_tick),
        .r_periods(r_periods16), .r_dt(r_dt16),
        .r_fine_first(ff16_nc), .r_fine_last(fl16_nc), .r_dt_alias(alias16_nc),
        .r_s1(s1_16_nc), .r_s2(s2_16_nc),
        .res_tgl(res_tgl16)
    );

    // ----------------------------------------------------------
    // Self-test fází + HISTOGRAM fine kódů (pin28/RO).
    // Histogram běží trvale => background tracking šířek binů
    // (teplotní drift se stopuje živě, ne jednorázovou kalibrací).
    // ----------------------------------------------------------
    wire [3:0] present;
    phase_check u_pc (
        .clk_p0(clk_p0_100m), .clk_p2p5(clk_p2p5_100m),
        .clk_p5(clk_p5_100m), .clk_p7p5(clk_p7p5_100m),
        .gate_tick(gate_tick), .present(present)
    );

    reg [23:0] hist0 = 24'd0, hist1 = 24'd0, hist2 = 24'd0, hist3 = 24'd0;
    reg [23:0] hist0_lat = 24'd0, hist1_lat = 24'd0,
               hist2_lat = 24'd0, hist3_lat = 24'd0;
    always @(posedge clk_p0_100m) begin
        if (gate_tick) begin
            hist0_lat <= hist0;  hist1_lat <= hist1;
            hist2_lat <= hist2;  hist3_lat <= hist3;
            hist0 <= 24'd0; hist1 <= 24'd0; hist2 <= 24'd0; hist3 <= 24'd0;
        end else if (rise4) begin
            // saturace (audit V4): 1s okno + >16,7M hran by jinak tiše wraplo
            case (fine4)
                2'd0: if (hist0 != 24'hFFFFFF) hist0 <= hist0 + 24'd1;
                2'd1: if (hist1 != 24'hFFFFFF) hist1 <= hist1 + 24'd1;
                2'd2: if (hist2 != 24'hFFFFFF) hist2 <= hist2 + 24'd1;
                2'd3: if (hist3 != 24'hFFFFFF) hist3 <= hist3 + 24'd1;
            endcase
        end
    end
    // fine_seen = |histogram (zachová sémantiku phase_status pro STM UI)
    wire [3:0] fine_seen4_lat  = {|hist3_lat, |hist2_lat, |hist1_lat, |hist0_lat};
    wire [7:0] phase_status_p0 = {fine_seen4_lat, present};
    reg        ph_tgl = 1'b0;   // handshake pro CDC (1 toggle / okno)
    always @(posedge clk_p0_100m) if (gate_tick) ph_tgl <= ~ph_tgl;

    // ----------------------------------------------------------
    // CDC do 10 MHz: hrany res_tgl + výpočet recip_calc
    // ----------------------------------------------------------
    reg [2:0] res4_s = 3'b000, res16_s = 3'b000, ph_s = 3'b000;
    reg [7:0]  phase_status = 8'd0;
    reg [95:0] hist_10m     = 96'd0;   // {hist3,hist2,hist1,hist0} - stabilní celé okno
    always @(posedge clk_ref_10m) begin
        res4_s  <= {res4_s[1:0],  res_tgl4};
        res16_s <= {res16_s[1:0], res_tgl16};
        ph_s    <= {ph_s[1:0],    ph_tgl};
        if (ph_s[2] ^ ph_s[1]) begin           // latch stabilní data (MCP handshake)
            phase_status <= phase_status_p0;
            hist_10m     <= {hist3_lat, hist2_lat, hist1_lat, hist0_lat};
        end
    end
    wire res_valid4  = (res4_s[2]  ^ res4_s[1]);
    wire res_valid16 = (res16_s[2] ^ res16_s[1]);

    reg [63:0] timestamp_10m = 64'd0;
    always @(posedge clk_ref_10m) timestamp_10m <= timestamp_10m + 64'd1;

    // primární /4 (pin 28)
    wire [63:0] freq4_x100000, gate_ns4;
    wire        valid4, div_err4;
    recip_calc #(.CONST(64'd160000000000000)) u_calc4 (   // 1.6e14 (/4)
        .clk(clk_ref_10m), .start(res_valid4),
        .periods({6'd0, r_periods4}), .dt(r_dt4),
        .freq_x100000(freq4_x100000), .gate_ns(gate_ns4),
        .valid(valid4), .err(div_err4)
    );

    // rezervní /16 (pin 27)
    wire [63:0] freq16_x100000, gate_ns16_unused;
    wire        valid16, div_err16;
    recip_calc #(.CONST(64'd640000000000000)) u_calc16 (  // 6.4e14 (/16)
        .clk(clk_ref_10m), .start(res_valid16),
        .periods({6'd0, r_periods16}), .dt(r_dt16),
        .freq_x100000(freq16_x100000), .gate_ns(gate_ns16_unused),
        .valid(valid16), .err(div_err16)
    );

    // latch hodnot k okamžiku platnosti (sedí v rámci)
    reg [31:0] periods_lat = 32'd0;
    reg        dt_ovf_lat  = 1'b0;
    reg [3:0]  fine_fl_lat = 4'd0;     // {fine_last, fine_first}
    reg        dt_alias_lat = 1'b0;    // okno bez hran > 42,9 s -> Δt alias
    reg [55:0] s1_lat      = 56'd0;
    reg [79:0] s2_lat      = 80'd0;
    always @(posedge clk_ref_10m) begin
        if (res_valid4) begin
            periods_lat <= {6'd0, r_periods4};
            dt_ovf_lat  <= r_dt4[33];          // Δt >= 2^33 ticků (~21,5 s)
            dt_alias_lat <= alias4;
            fine_fl_lat <= {fl4, ff4};
            s1_lat      <= s1_4;
            s2_lat      <= s2_4;
        end
    end
    reg [63:0] freq16_hold = 64'd0;
    reg        err16_hold  = 1'b0;
    always @(posedge clk_ref_10m) begin
        if (valid16) begin
            freq16_hold <= freq16_x100000;
            err16_hold  <= div_err16;
        end
    end

    // watchdog ztráty signálu (pin28): ~2,5 s bez měření
    reg [24:0] wdog        = 25'd0;
    reg        signal_lost = 1'b0;
    always @(posedge clk_ref_10m) begin
        if (valid4) begin
            wdog        <= 25'd0;
            signal_lost <= 1'b0;
        end else if (wdog == 25'd25000000) begin
            signal_lost <= 1'b1;
        end else begin
            wdog <= wdog + 25'd1;
        end
    end

    // error_flags: bit0=Δt==0, bit2=Δt>=2^33 ticků (~21,5 s), bit3=Δt alias
    // (okno bez hran > 42,9 s -> hodnota NEPLATNÁ, STM zahodí);
    // bit1=signal_lost přidává živě spi_app.
    wire [31:0] meas_err_flags = {31'd0, div_err4} | {29'd0, dt_ovf_lat, 2'd0}
                               | {28'd0, dt_alias_lat, 3'd0};
    wire [7:0]  meas_status2   = {7'd0, err16_hold};

    // ----------------------------------------------------------
    // SPI PHY (10 MHz) + aplikace - protokol v2, rámec 128 B
    // ----------------------------------------------------------
    wire [1023:0] tx_frame_flat;
    wire [1023:0] rx_frame_flat;
    wire          frame_end_tgl;
    wire          tx_frame_valid;
    wire [7:0]    spi_status;
    wire [10:0]   rx_bit_count;

    spi_slave_phy u_phy (
        .clk(clk_ref_10m),
        .sck_pin(spi_sck),
        .cs_pin(spi_cs_n),
        .mosi_pin(spi_mosi),
        .miso(spi_miso),
        .tx_frame_flat(tx_frame_flat),
        .tx_valid(tx_frame_valid),
        .rx_frame_flat(rx_frame_flat),
        .frame_end_tgl(frame_end_tgl),
        .rx_bit_count(rx_bit_count)
    );

    reg [2:0] fe_s = 3'b000;
    always @(posedge clk_ref_10m)
        fe_s <= {fe_s[1:0], frame_end_tgl};
    wire rx_valid_pulse = (fe_s[2] ^ fe_s[1]);

    spi_app u_app (
        .clk(clk_ref_10m),
        .meas_freq_x100000(freq4_x100000),
        .meas_periods(periods_lat),
        .meas_gate_ns(gate_ns4),
        .meas_timestamp(timestamp_10m),
        .meas_error_flags(meas_err_flags),
        .meas_channel(8'd0),
        .new_meas(valid4),
        .signal_lost(signal_lost),
        .meas_freq16_x100000(freq16_hold),
        .meas_phase_status(phase_status),
        .meas_status2(meas_status2),
        .meas_fine_fl(fine_fl_lat),
        .meas_hist(hist_10m),
        .meas_s1(s1_lat),
        .meas_s2(s2_lat),
        .cal_mode(cal_mode),
        .base_win(base_win),
        .rx_frame_flat(rx_frame_flat),
        .rx_valid(rx_valid_pulse),
        .tx_frame_flat(tx_frame_flat),
        .tx_valid(tx_frame_valid),
        .dbg_status(spi_status)
    );

    // ----------------------------------------------------------
    // LED heartbeat: toggle na každé dokončené měření pin28 (~2 Hz blik).
    // ----------------------------------------------------------
    initial led_tx = 1'b1;
    always @(posedge clk_ref_10m)
        if (valid4) led_tx <= ~led_tx;

endmodule


// ------------------------------------------------------------
// ring_osc: LUT ring oscilátor (13 invertujících stupňů) + /4.
// Asynchronní zdroj pro code-density kalibraci binů TDC.
// Gowin primitivy => syntéza řetěz nezoptimalizuje. Kmitočet
// ~25-40 MHz dle PVT, po /4 bezpečně pod ~40MHz stropem debounce.
// (Ve zdrojích jen pro syntézu - testbenche tento soubor nepoužívají.)
// ------------------------------------------------------------
module ring_osc (
    input  wire en,
    output wire out
);
    wire [12:0] n;

    LUT2 #(.INIT(4'h7)) u_nand (.F(n[0]), .I0(en), .I1(n[12])); // ~(en & n12)
    genvar i;
    generate
        for (i = 1; i < 13; i = i + 1) begin : g_inv
            LUT1 #(.INIT(2'b01)) u_inv (.F(n[i]), .I0(n[i-1]));
        end
    endgenerate

    reg [1:0] div = 2'b00;
    always @(posedge n[12]) div <= div + 2'b01;
    assign out = div[1];
endmodule
