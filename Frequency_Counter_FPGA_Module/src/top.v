// ============================================================
// File: top.v  (DRAFT — nová deska, dva symetrické kanály, HRUBÉ čítání)
//
// 🔴 TOTO JE PŘECHODNÝ BRING-UP KROK, NE FINÁLNÍ v3 IMPLEMENTACE.
// Nahrazuje 4fázový oversampling vernier (potřeboval 4 fázově posunuté
// 100 MHz hodiny ze Si5356) JEDNODUCHÝM jednohodinovým čítáním hran na
// clk_p0_100m (10 ns/LSB, žádné jemné sub-period rozlišení) — protože
// nová deska dává jen JEDEN REF_100MHz (PIN35). Carry-chain TDC (jemné
// rozlišení ~50-100 ps) je "stupeň 3" v PHASE_CAL_DESIGN.md, zatím jen
// koncepční popis, NENÍ implementovaný. Než se TDC udělá, měří se jen
// s rozlišením 1 hodinového taktu (10 ns) — dost pro ELEKTRICKÝ bring-up
// (SPI komunikace, kontinuita zapojení CH_A/CH_B), ne pro finální přesnost.
//
// SPI protokol: v2 beze změny (128B rámec, spi_app.v netknutý). CH_A jede
// v PRIMÁRNÍM slotu payloadu (freq_x100000/edge_count/gate_ns — stejně
// jako dřív pin28/÷4), CH_B v SEKUNDÁRNÍM slotu (freq16_x100000 — stejně
// jako dřív pin27/÷16). Obě sdílejí JEDNO hradlo (gate_tick) — to je
// podmínka, kterou FPGA_PROTOCOL_V3_NAVRH.md §10 bod 6 označuje jako
// nutnou pro párování v datové cache, takže se tím nic nepředjímá špatně.
// Migrace na skutečný v3 payload (presc pole, nezávislé edge_count pro
// oba kanály, time_error_ps) je samostatný krok (viz FPGA_PROTOCOL_V3_NAVRH.md
// §11 checklist) — TADY se spi_app.v vůbec nemění.
//
// ODSTRANĚNO proti staré desce (FPGA_Module_2_0):
//   - sig_in4/sig_in16 (odbočky ÷4/÷16 MC100EP016) -> CH_A/CH_B (PIN25/27)
//   - div_res (MR děliče, PIN53) -- nová deska tenhle pin nepoužívá
//   - clk_p2p5/p5/p7p5_100m (4fázový vernier) -- jen JEDEN REF_100MHz
//   - phase_oversampler, phase_check, cal_mode/ring_osc, histogram fine
//     kódů -- všechno záviselo na existenci 4 fází nebo na fine bitech,
//     které teď nemáme (fine je vždy 0). meas_hist/meas_phase_status
//     jdou do spi_app jako konstantní 0 (CAL report bude nesmyslný, dokud
//     nepřibude skutečný TDC -- to je OČEKÁVANÉ, ne přehlédnutí).
//
// 🔴 PROTOKOLOVÁ POLE S1/S2/fine/hist (spi_app.v, caps bit4 rezerva) jsou
// navržena pro LSB=2,5 ns 4fázového vernieru, ne pro carry-chain TDC.
// Až přibude skutečný TDC (PHASE_CAL_DESIGN.md "stupeň 3"), nestačí jen
// propojit signály -- LSB i rozsah akumulátorů se musí přepočítat na nové
// jemné rozlišení (~22 ps single-shot / bin ~50 ps). Detail viz komentář
// u mapy payloadu ve spi_app.v.
//
// Piny: nová deska, viz src/pins.cst + ../../citac_zadani_predavaci.md §4.
// ✅ SYNTÉZA + P&R OVĚŘENY (gw_sh.exe build.tcl, 2026-09-30): 0 chyb,
// 1 benigní varování (PR1014 -- clk_ref_10m na obecném routingu, 10 MHz
// = 100 ns perioda, zanedbatelné), bitstream vygenerován
// (impl/pnr/Counter_FPGA.fs). Resource usage: Logic 55 %, Register 79 %,
// DSP 70 % -- v pohodě se vejde do GW1NR-9.
// ⬜ NEOVĚŘENO REÁLNÝM SIGNÁLEM -- syntéza/P&R dokazuje jen že bitstream
// vznikl a časování je uzavřené, ne že měření na desce funguje správně.
// Desku teprve pájíte; po zapojení ověřit nejdřív SPI (STM32 `fpgaraw`),
// pak CH_A/CH_B na známém kmitočtu proti referenčnímu čítači.
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
    // P0 doména: volnoběžná časová značka (10 ns/LSB)
    // ----------------------------------------------------------
    reg [31:0] tick_p0 = 32'd0;
    always @(posedge clk_p0_100m) tick_p0 <= tick_p0 + 32'd1;

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
    // Hrubé čítání hran: CH_A, CH_B (jednohodinový debounce edge detect,
    // fine vždy 0 -- žádné sub-period rozlišení, viz hlavička souboru).
    // ----------------------------------------------------------
    wire rise_a, rise_b;
    wire [1:0] fine_a, fine_b;   // vždy 2'b00

    coarse_edge_detect u_ceda (.clk_p0(clk_p0_100m), .sig_in(ch_a),
                               .sig_rise(rise_a), .fine(fine_a));
    coarse_edge_detect u_cedb (.clk_p0(clk_p0_100m), .sig_in(ch_b),
                               .sig_rise(rise_b), .fine(fine_b));

    wire [33:0] ev_ts_a = {tick_p0, fine_a};
    wire [33:0] ev_ts_b = {tick_p0, fine_b};

    // ----------------------------------------------------------
    // 2x windowed reciproký čítač (win_recip z spi_app.v -- NEZMĚNĚNÝ,
    // už časově uzavřený blok; bezpečnější znovupoužít než přepisovat).
    // CH_A navíc nese Λ akumulátory (S1/S2) a krajní fine kódy pro
    // budoucí LUT korekci -- dnes fine==0, takže S1/S2 jsou i tak platné
    // (jen bez jemné korekce), degenerují na prostý součet časových značek.
    // ----------------------------------------------------------
    wire [25:0] r_periods_a,  r_periods_b;
    wire [33:0] r_dt_a,       r_dt_b;
    wire        res_tgl_a,    res_tgl_b;
    wire [1:0]  ff_a, fl_a,   ff_b_nc, fl_b_nc;
    wire        alias_a,      alias_b_nc;
    wire [55:0] s1_a,         s1_b_nc;
    wire [79:0] s2_a,         s2_b_nc;

    win_recip u_wra (
        .clk(clk_p0_100m), .sig_rise(rise_a), .ev_ts(ev_ts_a), .gate_tick(gate_tick),
        .r_periods(r_periods_a), .r_dt(r_dt_a),
        .r_fine_first(ff_a), .r_fine_last(fl_a), .r_dt_alias(alias_a),
        .r_s1(s1_a), .r_s2(s2_a),
        .res_tgl(res_tgl_a)
    );
    win_recip u_wrb (
        .clk(clk_p0_100m), .sig_rise(rise_b), .ev_ts(ev_ts_b), .gate_tick(gate_tick),
        .r_periods(r_periods_b), .r_dt(r_dt_b),
        .r_fine_first(ff_b_nc), .r_fine_last(fl_b_nc), .r_dt_alias(alias_b_nc),
        .r_s1(s1_b_nc), .r_s2(s2_b_nc),
        .res_tgl(res_tgl_b)
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

    // ----------------------------------------------------------
    // Reciproký výpočet kmitočtu (recip_calc z spi_app.v -- NEZMĚNĚNÝ).
    // CONST pro PŘÍMOU cestu (presc=1, žádná předdělička): 1e5/2,5ns = 4e13.
    // ⚠️ Až přijde ÷10 přes MC12080 (relé), CONST pro postižený kanál musí
    // jít na 4e14 -- runtime přepínání je mimo rozsah tohoto bring-up kroku
    // (patří do v3 SET_CONFIG/presc, FPGA_PROTOCOL_V3_NAVRH.md §4/§7).
    // ----------------------------------------------------------
    wire [63:0] freq_a_x100000, gate_ns_a;
    wire        valid_a, div_err_a;
    recip_calc #(.CONST(64'd40000000000000)) u_calc_a (   // 4e13 (presc=1)
        .clk(clk_ref_10m), .start(res_valid_a),
        .periods({6'd0, r_periods_a}), .dt(r_dt_a),
        .freq_x100000(freq_a_x100000), .gate_ns(gate_ns_a),
        .valid(valid_a), .err(div_err_a)
    );

    wire [63:0] freq_b_x100000, gate_ns_b_unused;
    wire        valid_b, div_err_b;
    recip_calc #(.CONST(64'd40000000000000)) u_calc_b (   // 4e13 (presc=1)
        .clk(clk_ref_10m), .start(res_valid_b),
        .periods({6'd0, r_periods_b}), .dt(r_dt_b),
        .freq_x100000(freq_b_x100000), .gate_ns(gate_ns_b_unused),
        .valid(valid_b), .err(div_err_b)
    );

    // latch hodnot k okamžiku platnosti (sedí v rámci) -- CH_A nese i S1/S2
    reg [31:0] periods_lat = 32'd0;
    reg        dt_ovf_lat  = 1'b0;
    reg [3:0]  fine_fl_lat = 4'd0;     // {fine_last, fine_first} -- vždy 0, viz výše
    reg        dt_alias_lat = 1'b0;
    reg [55:0] s1_lat      = 56'd0;
    reg [79:0] s2_lat      = 80'd0;
    always @(posedge clk_ref_10m) begin
        if (res_valid_a) begin
            periods_lat <= {6'd0, r_periods_a};
            dt_ovf_lat  <= r_dt_a[33];          // Δt >= 2^33 ticků (~21,5 s)
            dt_alias_lat <= alias_a;
            fine_fl_lat <= {fl_a, ff_a};
            s1_lat      <= s1_a;
            s2_lat      <= s2_a;
        end
    end
    reg [63:0] freq_b_hold = 64'd0;
    reg        err_b_hold  = 1'b0;
    always @(posedge clk_ref_10m) begin
        if (valid_b) begin
            freq_b_hold <= freq_b_x100000;
            err_b_hold  <= div_err_b;
        end
    end
    // ⚠️ ZDEDENA VLASTNOST (ne regrese dnesni prace -- stejny vzor mel puvodni
    // navrh pro freq16_hold/valid16): CH_A a CH_B jsou NEZAVISLE signaly,
    // jejich okna se uzaviraji na VLASTNI prvni hrane po gate_tick, ne
    // synchronne. Pokud by `valid_a` (spousti new_meas v spi_app) a `valid_b`
    // vystrelily na STEJNEM taktu clk_ref_10m, spi_app zachyti `freq_b_hold`
    // PRED touto aktualizaci (neblokujici prirazeni -> stara hodnota) -- CH_B
    // v tom jednom ramci bude o jedno okno stary. Nahodna shoda, nizka
    // pravdepodobnost, samoopravi se pristi okno. Skutecne reseni = nezavisly
    // edge_count/win_seq pro kazdy kanal (FPGA_PROTOCOL_V3_NAVRH.md, plna
    // migrace), ne zaplata nad timhle prechodnym bring-up kodem.

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

    // error_flags: bit0=Δt==0 (CH_A), bit2=Δt>=2^33 ticků, bit3=Δt alias;
    // bit1=signal_lost přidává živě spi_app.
    wire [31:0] meas_err_flags = {31'd0, div_err_a} | {29'd0, dt_ovf_lat, 2'd0}
                               | {28'd0, dt_alias_lat, 3'd0};
    wire [7:0]  meas_status2   = {7'd0, err_b_hold};   // CH_B chyba v "status2" slotu

    // meas_hist/meas_phase_status: bez skutečného TDC nemají smysl (žádné
    // fine bity ke sběru) -- konstantní 0, CAL report (TYPE 0xA0) bude
    // prázdný, dokud nepřibude carry-chain TDC. Očekávané, ne přehlédnuté.
    wire [95:0] meas_hist_zero  = 96'd0;
    wire [7:0]  meas_phase_zero = 8'd0;
    wire        cal_mode_nc;    // spi_app output, dnes nic nepřepíná (žádný RO mux)

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
        .meas_fine_fl(fine_fl_lat),
        .meas_hist(meas_hist_zero),
        .meas_s1(s1_lat),
        .meas_s2(s2_lat),
        .cal_mode(cal_mode_nc),
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


// ------------------------------------------------------------
// coarse_edge_detect: jednohodinový (clk_p0, 100 MHz) debounce edge
// detektor. Nahrazuje phase_oversampler, když je k dispozici jen JEDNA
// referenční hodina (nová deska, viz hlavička souboru) -- fine se vždy
// vrací 2'b00 (žádné sub-period rozlišení, rozlišení je 1 takt = 10 ns).
//
// Debounce vzor je STEJNÝ jako u phase_oversampler (spi_app.v) -- jedna
// hrana za potvrzenou periodu (LOW okno mezi náběžnými hranami), jen
// s JEDNOU fází místo čtyř. Strop spolehlivého čítání je zhruba
// clk_p0/3..clk_p0/2 na vstupu (potřeba aspoň celý takt LOW mezi hranami
// pro spolehlivé rozlišení bez oversamplingu) -- při 100 MHz tedy řádově
// desítky MHz na vstupu, s dostatečnou rezervou pro bring-up na 5-10 MHz.
// ------------------------------------------------------------
module coarse_edge_detect (
    input  wire       clk_p0,
    input  wire       sig_in,     // asynchronní vstup
    output reg        sig_rise,   // 1-taktový pulz na potvrzenou hranu
    output wire [1:0] fine        // vždy 0 -- žádné sub-period rozlišení
);
    assign fine = 2'b00;

    // 2-stupňový synchronizér (metastabilita)
    reg s0 = 1'b0, s1 = 1'b0;
    always @(posedge clk_p0) begin s0 <= sig_in; s1 <= s0; end

    reg state = 1'b0;   // 0 = potvrzeno LOW, 1 = HIGH (čeká na návrat LOW)

    initial sig_rise = 1'b0;

    always @(posedge clk_p0) begin
        sig_rise <= 1'b0;
        if (!state) begin
            if (s1) begin           // potvrzená LOW->HIGH = 1 hrana/perioda
                sig_rise <= 1'b1;
                state    <= 1'b1;
            end
        end else if (!s1) begin     // návrat na LOW = konec debounce okna
            state <= 1'b0;
        end
    end
endmodule
