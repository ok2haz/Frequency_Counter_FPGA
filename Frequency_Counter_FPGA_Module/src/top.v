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
    output reg  fpga_ok,         // FW 0x0422 PIN63 (IOR5A) -> LED D5 "FPGA_OK" (anoda na pinu, 1k k zemi): svit = bitstream bezi A referencni hodiny OK

    // FW 0x0420: vyber reference (prepinac U19: OCXO / externi 10 MHz)
    output wire ref_ctrl,        // PIN41 Ref_Ctrl: 0 = OCXO, 1 = externi
    input  wire ext_ref_sens,    // PIN51 Ext_Ref_Sens: externi reference po Schmittu (jen detekce pritomnosti hran)

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
    // REFERENCNI KMITOCET FPGA = 100 MHz (Si5356 CLK0/1). FW 0x0421/0x0422 bezely na 125 MHz; FW 0x0423 (2026-10-11) se vraci
    // na 100 MHz: na 125 MHz mela casova znacka chybu T/2 a T podle faze hrany (ADEV 1,4e-8) a regrese pocitala spatne (L-0152).
    // Jednotka casu v ramci je T_clk/16384 = 0,6103515625 ps (ps = jednotky * 625 / 1024); kmitocet jde v ramci (abs 99), aby host
    // nepocital s jinym taktem, nez na jakem FPGA skutecne bezi. Vse ostatni (TDC, tabulka kalibrace) je v tehle jednotce.
    localparam CLK_MHZ = 100;
    reg  [33:0] tick_hi = 34'd0;
    always @(posedge clk_p0_100m) tick_hi <= tick_hi + 34'd1;
    wire [47:0] tick_ps = {tick_hi, 14'd0};

    // ----------------------------------------------------------
    // FW 0x041F: identita bitstreamu. src/build_id.vh zapisuje build.tcl pri KAZDEM sestaveni (BLD_TIME = unix
    // cas, BLD_GIT = [27:0] git hash, [31] neulozene zmeny); soubor se neverzuje. 🔴 Sestaveni v Gowin IDE
    // build.tcl nespusti -> ponese identitu POSLEDNIHO sestaveni skriptem (L-0122: stavet jen pres build.tcl).
    // ----------------------------------------------------------
`ifdef SIM_TOP
    localparam [31:0] BLD_TIME = 32'd0;
    localparam [31:0] BLD_GIT  = 32'd0;
`else
`include "build_id.vh"
`endif

    // ----------------------------------------------------------
    // FW 0x041F: HLIDAC 100 MHz (domena 10 MHz, ta jde primo z OCXO a na STM nezavisi).
    // Proc: 100 MHz dela Si5356, ktery konfiguruje STM po svem startu (OEB_ALL off -> soft reset -> on). Kdyz se
    // STM restartuje za behu FPGA, 100 MHz na chvili zmizi nebo se rozkmita -- a FPGA nema zadny reset, takze
    // citace oken, faze kalibrace a tabulka TDC prezivaji v libovolnem stavu az do dalsiho nahrani. Stejne pri
    // zapnuti: FPGA naběhne driv, nez STM Si5356 nastavi, a kalibrace po 168 ms mohla probehnout na jinem kmitoctu.
    // Jak: cdiv[4] (perioda 320 ns) se v 10 MHz pocita v okne 1024 taktu (102,4 us) -> spravne 640 hran (hodiny
    // jsou koherentni, odchylka nejvys 1). Mimo 636..644 = vypadek nebo jiny kmitocet; mezera >~0,6 us se pozna.
    // Rychle zakmity (navic pulzy) tim poznat NEJDE -- to hlida az kontrolni pocitani hran (win_recip).
    // Reakce: dokud hlidac nehlasi OK, drzi se boot_done = 0 -> mereni stoji (hold) a 168 ms po ustaleni se TDC
    // znovu kalibruje (cal_boot). Mereni tak po vypadku vzdy zacne z cisteho stavu s tabulkou z platnych hodin.
    // ----------------------------------------------------------
    reg [4:0]  cdiv = 5'd0;
    always @(posedge clk_p0_100m) cdiv <= cdiv + 5'd1;
    reg [2:0]  cm_s   = 3'b000;
    reg [9:0]  cm_win = 10'd0;
    reg [10:0] cm_cnt = 11'd0;             // 11 b: ani 2,6nasobek kmitoctu nepretece zpet do pasma
    reg        clk_ok = 1'b0;              // posledni okno v poradku
    reg        clk_flt = 1'b0;             // od nahrani aspon jeden prechod OK -> vadne
    reg [7:0]  clk_loss = 8'd0;            // pocet takovych prechodu (saturuje)
    wire       cm_edge = cm_s[2] ^ cm_s[1];
    // Ocekavany pocet prechodu cdiv[4] v okne 1024 taktu 10 MHz (102,4 us): cdiv[4] se prepne kazdych 16 taktu referencni hodiny
    // -> 102,4 us * CLK_MHZ / 16 us = 6,4 * CLK_MHZ (100 MHz: 640); odchylka nejvys 1, okno +-4.
    localparam [10:0] CM_EXP = (CLK_MHZ * 32) / 5;
    wire       cm_good = (cm_cnt >= CM_EXP - 11'd4) && (cm_cnt <= CM_EXP + 11'd4);
    always @(posedge clk_ref_10m) begin
        cm_s   <= {cm_s[1:0], cdiv[4]};
        cm_win <= cm_win + 10'd1;
        if (&cm_win) begin                 // konec okna (hrana v tomto taktu se nepocita: nejvys -1)
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
    reg [1:0]  ok_s = 2'b00;               // clk_ok do domeny 100 MHz
    always @(posedge clk_p0_100m) ok_s <= {ok_s[0], clk_ok};

    // ----------------------------------------------------------
    // Hradlovací okno (SET_CONFIG 0x01): 0 = 100 ms, 1 = 250 ms (default), 2 = 1 s,
    // FW 0x041E: 3 = 50 ms, 4 = 500 ms. Nezávisí na počtu fázových hodin.
    // ----------------------------------------------------------
    wire [2:0]  base_win;
    wire        chan_sel;       // FW 0x041E (SET_CONFIG 0x03): 0 = CH_A primarni, 1 = CH_B primarni
`ifdef SIM_TOP
    // SIMULACE celeho top (sim/tb_top_regr.sv): hradlo 100x kratsi (base_win 1 = 2,5 ms), jinak beze zmeny
    wire [23:0] gate_lim = 24'd24999;
`else
    wire [23:0] gate_lim = (base_win == 3'd0) ? 24'd999999  :   // 100 ms
                           (base_win == 3'd2) ? 24'd9999999 :   // 1 s
                           (base_win == 3'd3) ? 24'd499999  :   // 50 ms
                           (base_win == 3'd4) ? 24'd4999999 :   // 500 ms
                                                24'd2499999;    // 250 ms (a neplatna hodnota)
`endif
    reg  [23:0] gtmr     = 24'd0;
    reg  [2:0]  bw_d     = 3'd1;
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

    // ----------------------------------------------------------
    // FW 0x0411: SEGMENTY okna pro regresni blok CH_A (viz regr_acc). Cas v hradle urcuje citac gtmr (10 MHz):
    //   A = [G/16, 3G/16), B = [13G/16, 15G/16)  (G = delka hradla v taktech 10 MHz)
    // Okno se uzavira prvni hranou PO gate_tick, tedy segment A zacina az po zacatku okna a segment B konci
    // dost PRED jeho koncem (>= G/16 = 6 ms pri 100 ms hradle): znacky vnitrnich hran se nikdy nepotkaji
    // s uzaviraci znackou ani s delenim v regr_acc. Prechody vlajek jsou vzdy daleko od hran signalu
    // (staticke vuci 100 MHz), do 100 MHz domeny jdou pres 2 FF.
    // ----------------------------------------------------------
`ifdef SIM_TOP
    wire [23:0] sa0 = 24'd1562, sa1 = 24'd4687, sb0 = 24'd20312, sb1 = 24'd23437;
`else
    // G = delka hradla v taktech 10 MHz: 100 ms 1 000 000, 250 ms 2 500 000, 1 s 10 000 000, 50 ms 500 000, 500 ms 5 000 000
    wire [23:0] sa0 = (base_win == 3'd0) ? 24'd62500   : (base_win == 3'd2) ? 24'd625000  :
                      (base_win == 3'd3) ? 24'd31250   : (base_win == 3'd4) ? 24'd312500  : 24'd156250;    // G/16
    wire [23:0] sa1 = (base_win == 3'd0) ? 24'd187500  : (base_win == 3'd2) ? 24'd1875000 :
                      (base_win == 3'd3) ? 24'd93750   : (base_win == 3'd4) ? 24'd937500  : 24'd468750;    // 3G/16
    wire [23:0] sb0 = (base_win == 3'd0) ? 24'd812500  : (base_win == 3'd2) ? 24'd8125000 :
                      (base_win == 3'd3) ? 24'd406250  : (base_win == 3'd4) ? 24'd4062500 : 24'd2031250;   // 13G/16
    wire [23:0] sb1 = (base_win == 3'd0) ? 24'd937500  : (base_win == 3'd2) ? 24'd9375000 :
                      (base_win == 3'd3) ? 24'd468750  : (base_win == 3'd4) ? 24'd4687500 : 24'd2343750;   // 15G/16
`endif
    wire seg_a, seg_b;                     // segmenty okna CH_A (do win_recip u_wra)
    reg sa_10 = 1'b0, sb_10 = 1'b0;
    always @(posedge clk_ref_10m) begin
        sa_10 <= (gtmr >= sa0) && (gtmr < sa1);
        sb_10 <= (gtmr >= sb0) && (gtmr < sb1);
    end
    reg [1:0] sa_s = 2'b00, sb_s = 2'b00;
    always @(posedge clk_p0_100m) begin sa_s <= {sa_s[0], sa_10}; sb_s <= {sb_s[0], sb_10}; end
    assign seg_a = sa_s[1];
    assign seg_b = sb_s[1];
    wire gate_tick = (gt_s[2] ^ gt_s[1]);  // ~gate puls v P0

    // ----------------------------------------------------------
    // Kalibrace TDC: jeden ring oscilátor pro oba kanály. Spouští se po zapnutí
    // (~168 ms, ať se ustálí napájení/hodiny) a hranou cal_mode ze STM.
    // FW 0x041F: 168 ms se pocita az od chvile, kdy hlidac hlasi 100 MHz v poradku, a pri kazdem jeho vypadku
    // se odpocet vrati na zacatek (boot_done = 0 zaroven drzi mereni, viz `hold` u win_recip).
    // ----------------------------------------------------------
    reg [23:0] boot_cnt  = 24'd0;
    reg        boot_done = 1'b0;
    reg        cal_boot  = 1'b0;
    always @(posedge clk_p0_100m) begin
        cal_boot <= 1'b0;
        if (!ok_s[1]) begin
            boot_cnt  <= 24'd0;
            boot_done <= 1'b0;
        end else if (!boot_done) begin
            boot_cnt <= boot_cnt + 24'd1;
`ifdef SIM_TOP
            if (boot_cnt == 24'd2000) begin boot_done <= 1'b1; cal_boot <= 1'b1; end
`else
            if (&boot_cnt) begin boot_done <= 1'b1; cal_boot <= 1'b1; end
`endif
        end
    end
    wire cal_mode;                                   // z spi_app (10 MHz doména)
    reg [2:0] calm_s = 3'b000;
    always @(posedge clk_p0_100m) calm_s <= {calm_s[1:0], cal_mode};
    reg  cal_req = 1'b0;                                  // registrovano (kratke cesty)
    reg  rt_go   = 1'b0;                                  // FW 0x0422: automaticke opakovani kalibrace po selhani (viz nize)
    always @(posedge clk_p0_100m) cal_req <= (calm_s[1] & ~calm_s[2]) | cal_boot | rt_go;

    // Pocet udalosti kalibrace = 2^CAL_LOG2. JEDINY zdroj: predava se do obou tdc_chan
    // i do meze OVF_LIM nize (2026-10-03: OVF_LIM mel natvrdo 22, kalibrace 20 ->
    // retez pokryvajici jen ~83 % periody se nenahlasil jako kratky).
`ifdef SIM_TOP
    localparam CAL_LOG2 = 12;
`else
    localparam CAL_LOG2 = 20;
`endif
    // Konfigurace TDC (oba kanaly stejna; viz tdc.v). Volba z P&R matice 2026-10-07 (docs/audit/2026-10-07_tdc-vlastni-reference.md):
    //   STRIDE 1 / DUAL 0 : CLS 65 %, Fmax 111 MHz, TNS 0  <-- VYCHOZI (jemne biny ~39 ps, MENSI nez STRIDE 2: retez
    //                                                         ma jen TAPS ALU misto 512, z nichz >240 stejne nešlo pouzit)
    //   STRIDE 2 / DUAL 0 : CLS 69 %, Fmax 111 MHz, TNS 0
    //   DUAL 1            : CLS 87 %, Fmax 80-94 MHz, TNS < 0   (v simulaci funguje, na tomto cipu se nevejde)
    // STRIDE 2 = vzorek kazdy 2. ALU (dosavadni), 1 = kazdy ALU. DUAL 1 = druha sada vzorku na sestupnou hranu hodin.
    // TDC_NTAP = fyzicky postavenych tapu = 268: PRESNE JEDEN RADEK CFU (45 dlazdic x 6 ALU = 270 = hlava + 268 + konec).
    //   Pri 320 (do FW 0x041A) lezelo 52 tapu kazdeho kanalu ve VZDALENEM radku (A: R26 -> R20, B: R27 -> R5)
    //   za skokem obecnym vedenim; hrana na ne za 10 ns nedojde (maxtap 248-252), takze to byla jen plocha
    //   a potencialni obri bin (audit docs/audit/2026-10-09_tdc-3kanaly-rozmisteni.md, F-0228).
    //   Rezerva 268 proti ~252 potrebnym pri 100 MHz = 6-8 %; pri vyssi referenci (150-200 MHz) roste.
    //   Radek kazdeho retezu vynucuje pins.cst (GROUP ALU -> R26 / R27), kontrola sim/check_tdc_placement.py.
    // TDC_TAPS = sirka dekoderu (nasobek 32) >= NTAP; tapy NTAP..TAPS-1 jsou "prosla" (tdc_chain).
    localparam TDC_STRIDE = 1;
    // 0 = regresni blok vypnuty (vnitrni hrany se nemeri, CAPS bit7 = 0, bajty 68..75 = kody TDC); 1 = regrese.
    // FW 0x041A: zapnuto -- chyba znacky je NAHODNA (~105 ps, docs/TDC_MATEMATIKA.md kap. 10), prumer ji potlaci.
`ifdef SIM_TOP_REGR
    localparam TDC_REGR   = 1;
`else
    localparam TDC_REGR   = 1;
`endif
    localparam TDC_DUAL   = 0;
    localparam TDC_TAPS   = (TDC_STRIDE == 1) ? 288 : 256;
    localparam TDC_NTAP   = (TDC_STRIDE == 1) ? 268 : 256;
    localparam TDC_KD     = (TDC_STRIDE == 1) ? 1 : 2;
    wire use_ro_a, use_ro_b, ro_sig;
    ring_osc u_ro (.en(use_ro_a | use_ro_b), .out(ro_sig));

    // Timeout kalibrace (RO nejde / udalosti nechodi): spolecny pro oba kanaly,
    // ~5,4 s (2^29 taktu) od zacatku sberu; sber 2^20 udalosti trva ~0,1-0,5 s (podle rychlosti kruhu).
    reg [28:0] cal_tmo   = 29'd0;
    reg        cal_abort = 1'b0;
    reg        tmo_h = 1'b0, tmo_l = 1'b0;           // FW 0x0421: 29bitove AND ve dvou registrovanych castech
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

    // ----------------------------------------------------------
    // 2x TDC kanál (tdc.v): hrana -> sig_rise + ev_ts [ps]
    // ----------------------------------------------------------
    wire        rise_a, rise_b, trig_a, trig_b, tsv_a, tsv_b, want_a, want_b;
    wire        rise_sa, rise_sb;     // hrany pro pocitani (ec z 2. stupne vzorku, +1 takt)
    wire        wseg_a;              // FW 0x0411: i vnitrni hrany CH_A maji dostat presny cas (regresni blok)
    wire [23:0] rg_n_a, rg_n_b;  wire [39:0] rg_xm_a, rg_xm_b;  wire [47:0] rg_ym_a, rg_ym_b;  wire rg_ok_a, rg_ok_b;
    wire        wseg_b_nc, rgb_ok_a_nc, rgb_ok_b_nc;
    wire [23:0] rgb_n_a_nc, rgb_n_b_nc;  wire [39:0] rgb_xm_a_nc, rgb_xm_b_nc;  wire [47:0] rgb_ym_a_nc, rgb_ym_b_nc;
    wire [47:0] ts_a, ts_b;
    wire [8:0]  code_a, code_b;               // FW 0x0418: kod TDC prave casovane hrany
    wire [8:0]  r_cea, r_csa, r_ceb, r_csb;   // kod uzaviraci/zahajovaci hrany okna
    wire        cal_busy_a, cal_busy_b, cal_valid_a, cal_valid_b, cal_fail_a, cal_fail_b;
    wire [31:0] da_ovf, da_peak, db_ovf, db_peak;
    wire [15:0] da_nz, da_last, db_nz, db_last;
    wire [15:0] da_maxtap, db_maxtap;          // B1a: nejvyssi set tap (bubliny: >> d_last)
    wire [9:0]  hist_k;                        // z spi_app (kvazistaticky)
    wire [23:0] hq_a, hq_b;                    // hist[hist_k] kanalu A/B

    tdc_chan #(.CAL_LOG2(CAL_LOG2), .TAPS(TDC_TAPS), .STRIDE(TDC_STRIDE), .KD(TDC_KD), .DUAL(TDC_DUAL), .NTAP(TDC_NTAP)) u_tdca (
        .clk(clk_p0_100m), .sig_raw(ch_a), .ro(ro_sig), .tick_ps(tick_ps),
        .want(want_a), .want_seg(wseg_a), .cal_req(cal_req), .cal_abort(cal_abort),
        .rise_c(rise_a), .rise_s(rise_sa), .trig_ack(trig_a), .ts_valid(tsv_a), .ts_ps(ts_a),
        .use_ro(use_ro_a), .cal_busy(cal_busy_a),
        .cal_valid(cal_valid_a), .cal_fail(cal_fail_a),
        .d_ovf(da_ovf), .d_peak(da_peak), .d_nz(da_nz), .d_last(da_last),
        .d_maxtap(da_maxtap),
        .dump_k(hist_k), .dump_q(hq_a), .code_o(code_a)
    );
    tdc_chan #(.CAL_LOG2(CAL_LOG2), .TAPS(TDC_TAPS), .STRIDE(TDC_STRIDE), .KD(TDC_KD), .DUAL(TDC_DUAL), .NTAP(TDC_NTAP)) u_tdcb (
        .clk(clk_p0_100m), .sig_raw(ch_b), .ro(ro_sig), .tick_ps(tick_ps),
        .want(want_b), .want_seg(1'b0), .cal_req(cal_req), .cal_abort(cal_abort),
        .rise_c(rise_b), .rise_s(rise_sb), .trig_ack(trig_b), .ts_valid(tsv_b), .ts_ps(ts_b),
        .use_ro(use_ro_b), .cal_busy(cal_busy_b),
        .cal_valid(cal_valid_b), .cal_fail(cal_fail_b),
        .d_ovf(db_ovf), .d_peak(db_peak), .d_nz(db_nz), .d_last(db_last),
        .d_maxtap(db_maxtap),
        .dump_k(hist_k), .dump_q(hq_b), .code_o(code_b)
    );

    // ----------------------------------------------------------
    // FW 0x0422: AUTOMATICKY OPAKOVANA KALIBRACE po selhani. cal_fail = vyprsel casovac sberu (kruh se nerozbehl / udalosti nechodi).
    // HW 2026-10-11: ten samy bitstream mel pri nekterych nahranich tdc_status bit2 (cal_fail) a pak mereni s nominalni tabulkou
    // (chyba desitek ns) BEZ jakeho koli chyboveho priznaku. Po selhani se za ~8 ms (2^20 taktu) kalibrace spusti znovu, nejvyse 15x
    // za sebou; pocet pokusu (saturuje na 3) nese tdc_status[7:6].
    wire cfail = cal_fail_a | cal_fail_b;
    reg        cfail_q = 1'b0, rt_run = 1'b0;
    reg [19:0] rt_dly  = 20'd0;
    reg [3:0]  rt_n    = 4'd0;
    reg [1:0]  rt_tot  = 2'd0;
    always @(posedge clk_p0_100m) begin
        cfail_q <= cfail;
        rt_go   <= 1'b0;
        if (cfail & ~cfail_q & (rt_n != 4'hF)) rt_run <= 1'b1;               // nova porucha
        if (rt_run) begin
            rt_dly <= rt_dly + 20'd1;
            if (&rt_dly) begin
                rt_run <= 1'b0; rt_go <= 1'b1; rt_n <= rt_n + 4'd1;
                if (rt_tot != 2'd3) rt_tot <= rt_tot + 2'd1;
            end
        end
        if (cal_valid_a & cal_valid_b & ~cfail) rt_n <= 4'd0;                // kalibrace se povedla
    end

    // 2x windowed reciproký čítač (win_recip ze spi_app.v), čas v ps.
    // ----------------------------------------------------------
    wire [25:0] r_periods_a,  r_periods_b;
    wire [47:0] r_dt_a,       r_dt_b;
    wire        res_tgl_a,    res_tgl_b;
    wire        alias_a,      alias_b_nc;
    wire [7:0]  r_ccd_a,      r_ccd_b;      // FW 0x041F: kontrolni pocitani hran (viz win_recip)

    // `hold` do oken z registru (cal_busy je OR ctyr stavu; FW 0x0421: fanout + vzdalenost TDC <-> okno byly pri 8 ns periode cestou -0,4 ns)
    reg hold_a = 1'b1, hold_b = 1'b1;
    always @(posedge clk_p0_100m) begin
        hold_a <= cal_busy_a | ~cal_valid_a | ~boot_done;
        hold_b <= cal_busy_b | ~cal_valid_b | ~boot_done;
    end
    win_recip #(.REGR(TDC_REGR)) u_wra (
        .clk(clk_p0_100m), .sig_pin(ch_a), .rise_s(rise_sa), .trig_ack(trig_a), .ts_valid(tsv_a), .ts_ps(ts_a),
        .gate_tick(gate_tick), .code_i(code_a), .r_ce(r_cea), .r_cs(r_csa), .r_ccd(r_ccd_a), .seg_a(seg_a), .seg_b(seg_b), .want(want_a), .want_seg(wseg_a),
        .hold(hold_a),
        .r_periods(r_periods_a), .r_dt(r_dt_a), .r_dt_alias(alias_a), .res_tgl(res_tgl_a),
        .rg_n_a(rg_n_a), .rg_n_b(rg_n_b), .rg_xm_a(rg_xm_a), .rg_xm_b(rg_xm_b),
        .rg_ym_a(rg_ym_a), .rg_ym_b(rg_ym_b), .rg_ok_a(rg_ok_a), .rg_ok_b(rg_ok_b)
    );
    win_recip #(.REGR(0)) u_wrb (
        .clk(clk_p0_100m), .sig_pin(ch_b), .rise_s(rise_sb), .trig_ack(trig_b), .ts_valid(tsv_b), .ts_ps(ts_b),
        .gate_tick(gate_tick), .code_i(code_b), .r_ce(r_ceb), .r_cs(r_csb), .r_ccd(r_ccd_b), .seg_a(1'b0), .seg_b(1'b0), .want(want_b), .want_seg(wseg_b_nc),
        .hold(hold_b),
        .r_periods(r_periods_b), .r_dt(r_dt_b), .r_dt_alias(alias_b_nc), .res_tgl(res_tgl_b),
        .rg_n_a(rgb_n_a_nc), .rg_n_b(rgb_n_b_nc), .rg_xm_a(rgb_xm_a_nc), .rg_xm_b(rgb_xm_b_nc),
        .rg_ym_a(rgb_ym_a_nc), .rg_ym_b(rgb_ym_b_nc), .rg_ok_a(rgb_ok_a_nc), .rg_ok_b(rgb_ok_b_nc)
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
    reg [1:0] rt1 = 2'b00, rt2 = 2'b00;                                      // pocet automatickych opakovani kalibrace (do 10 MHz)
    always @(posedge clk_ref_10m) begin rt1 <= rt_tot; rt2 <= rt1; end
    wire [7:0] tdc_status = {rt2, short_b, short_a, cbs_s[1], cfl_s[1], cvb_s[1], cva_s[1]};
    wire [191:0] cal_diag = {db_last, db_nz, db_peak, db_ovf, da_last, da_nz, da_peak, da_ovf};
    // konfigurace TDC pro host: [4:0] log2(adres tabulky), [5] DUAL, [6] STRIDE==1, [15:7] pocet tapu
    localparam [8:0] TDC_TAPS9 = TDC_NTAP;                // fyzicke tapy (STM `tdc`: konfigurace N tapu)
    localparam [4:0] TDC_AW5   = $clog2(TDC_TAPS) + TDC_DUAL;
    wire [15:0] tdc_cfg = {TDC_TAPS9, (TDC_STRIDE == 1) ? 1'b1 : 1'b0, (TDC_DUAL != 0) ? 1'b1 : 1'b0, TDC_AW5};

    // ----------------------------------------------------------
    // Výsledky oken do 10 MHz domény. FPGA kmitočet NEPOČÍTÁ: rámec nese přesné
    // celé hodnoty (periods, dt_ps) a poměr dělá host. Jen gate_ns = dt_ps/1000
    // (gate_div = dt*5>>13) pro pole rámce abs 28; new_meas (valid_a) jde po něm.
    // ----------------------------------------------------------
    // FW 0x041E: kazdy kanal ma SVUJ latch (A: *_lat, B: *_hold). Do rámce jde PRIMARNI slot = kanal vybrany
    // SET_CONFIG 0x03 (chan_sel) a SEKUNDARNI slot = druhy kanal; nove mereni (SEQUENCE++) spousti dokonceni
    // okna PRIMARNIHO kanalu. STM podle `meas_channel` (bajt 44) pozna, co je co.
    reg [31:0] periods_lat  = 32'd0;           // CH_A (latch při res_valid_a)
    reg [47:0] dt_a_lat     = 48'd0;
    reg [31:0] periods_b_hold = 32'd0;         // CH_B
    reg [47:0] dt_b_hold    = 48'd0;
    reg        dt_ovf_lat   = 1'b0, dt_ovf_b = 1'b0;
    reg        dt_alias_lat = 1'b0, dt_alias_b = 1'b0;
    reg [8:0]  cea_lat = 9'd0, csa_lat = 9'd0, ceb_hold = 9'd0, csb_hold = 9'd0;   // FW 0x0418
    reg [7:0]  ccda_lat = 8'd0, ccdb_hold = 8'd0;                                   // FW 0x041F
    reg        gd_go        = 1'b0;
    always @(posedge clk_ref_10m) begin
        gd_go <= 1'b0;
        if (res_valid_a) begin
            periods_lat  <= {6'd0, r_periods_a};
            dt_a_lat     <= r_dt_a;
            dt_ovf_lat   <= r_dt_a[47];        // Δt >= 2^47 jednotek (~86 s)
            dt_alias_lat <= alias_a;
            cea_lat      <= r_cea;
            csa_lat      <= r_csa;
            ccda_lat     <= r_ccd_a;
            if (!chan_sel) gd_go <= 1'b1;
        end
        if (res_valid_b) begin
            periods_b_hold <= {6'd0, r_periods_b};
            dt_b_hold      <= r_dt_b;
            dt_ovf_b       <= r_dt_b[47];
            dt_alias_b     <= alias_b_nc;
            ceb_hold       <= r_ceb;
            csb_hold       <= r_csb;
            ccdb_hold      <= r_ccd_b;
            if (chan_sel) gd_go <= 1'b1;
        end
    end

    // primarni / sekundarni slot (kombinacne z chan_sel, jen 10 MHz domena)
    wire [31:0] periods_p  = chan_sel ? periods_b_hold : periods_lat;
    wire [31:0] periods_s  = chan_sel ? periods_lat    : periods_b_hold;
    wire [47:0] dt_p       = chan_sel ? dt_b_hold      : dt_a_lat;
    wire [47:0] dt_s       = chan_sel ? dt_a_lat       : dt_b_hold;
    wire        dt_ovf_p   = chan_sel ? dt_ovf_b       : dt_ovf_lat;
    wire        dt_alias_p = chan_sel ? dt_alias_b     : dt_alias_lat;
    wire [8:0]  cde_p = chan_sel ? ceb_hold : cea_lat,  cds_p = chan_sel ? csb_hold : csa_lat;
    wire [8:0]  cde_s = chan_sel ? cea_lat  : ceb_hold, cds_s = chan_sel ? csa_lat  : csb_hold;
    wire [7:0]  ccd_p = chan_sel ? ccdb_hold : ccda_lat, ccd_s = chan_sel ? ccda_lat : ccdb_hold;

    // stav hlidace 100 MHz do ramce (bajt 64): bit0 OK, bit1 od nahrani aspon jeden vypadek, bit2 zotaveni
    // (boot_done = 0: ceka se na ustaleni + novou kalibraci, mereni stoji)
    reg [1:0]  bd_s = 2'b00;
    always @(posedge clk_ref_10m) bd_s <= {bd_s[0], boot_done};
    // FW 0x0420: stav reference z ref_guard (domena OSC) do 10 MHz, bity 3..5
    wire ref_act, ref_ext_pres, ref_ext_lost;
    reg [1:0] ra_s = 2'b00, rp_s = 2'b00, rl_s = 2'b00;
    always @(posedge clk_ref_10m) begin ra_s <= {ra_s[0], ref_act}; rp_s <= {rp_s[0], ref_ext_pres}; rl_s <= {rl_s[0], ref_ext_lost}; end
    wire [7:0] clk_status = {2'd0, rl_s[1], rp_s[1], ra_s[1], ~bd_s[1], clk_flt, clk_ok};

    wire [63:0] gate_ns_a;
    wire        valid_a, gd_busy;
    gate_div #(.CLK_MHZ(CLK_MHZ)) u_gd (.clk(clk_ref_10m), .start(gd_go), .dt(dt_p),
                   .gate_ns(gate_ns_a), .valid(valid_a), .busy(gd_busy));

    wire [63:0] freq_a_x100000 = 64'd0;                 // host počítá (viz spi_app.v)
    wire        div_err_a      = (dt_p == 48'd0);
    wire [63:0] freq_b_hold    = 64'd0;
    wire        err_b_hold     = (dt_s == 48'd0);

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
    wire [31:0] meas_err_flags = {31'd0, div_err_a} | {29'd0, dt_ovf_p, 2'd0}
                               | {28'd0, dt_alias_p, 3'd0}
                               | {27'd0, ~(chan_sel ? cvb_s[1] : cva_s[1]) | cbs_s[1], 4'd0};
    wire [7:0]  meas_status2   = {7'd0, err_b_hold};   // chyba SEKUNDARNIHO slotu (Δt == 0)
    // phase_status je od FW 0x041E ECHO nastaveneho hradla {5'd0, base_win}: STM podle nej pozna, ze FPGA po
    // resetu/prenacteni konfigurace drzi opravdu to hradlo, ktere chce (CAPS bit9).
    wire [7:0]  meas_phase_zero = {5'd0, base_win};

    // ----------------------------------------------------------
    // SPI PHY (10 MHz) + aplikace -- protokol v2, rámec 128 B, NEZMĚNĚNO.
    // CH_A = primární slot (freq_x100000/periods/gate), CH_B = "/16" slot
    // (jen freq16_x100000) -- viz hlavička souboru, proč je to bezpečné
    // provizorium (sdílené hradlo, v3 migrace je samostatný krok).
    // ----------------------------------------------------------
    // 🔴 2026-10-04 (#264): PHY je taktovana primo SCK, ramce lezi v blokove RAM
    // uvnitr PHY (TX ve dvou polovinach, RX ve dvou polovinach). Prechod mezi
    // domenami SCK <-> clk_ref_10m (aplikace) dela RAM; ridici bity jsou po dobu
    // transakce staticke. Driv: 1024bit vodice + tx_valid synchronizator +
    // 1024bit posuvne registry (~2700 FF) a strop SCK ~8-10 MHz.
    wire          frame_end_tgl;
    wire [7:0]    spi_status;
    wire          tx_half, tx_we, phy_busy, phy_base;
    wire [7:0]    tx_waddr, tx_wdata;
    wire [7:0]    rx_b0, rx_b1, rx_b2, rx_p0, rx_p1, rx_p2;
    wire [31:0]   rx_seq;
    wire [15:0]   rx_crc_calc, rx_crc_recv;
    wire [15:0]   dbg_mosi_cnt, dbg_sck_cnt;

    spi_slave_phy u_phy (
        .clk(clk_p0_100m),
        .clk_app(clk_ref_10m),
        .sck_pin(spi_sck),
        .cs_pin(spi_cs_n),
        .mosi_pin(spi_mosi),
        .miso(spi_miso),
        .tx_half(tx_half),
        .tx_we(tx_we),
        .tx_waddr(tx_waddr),
        .tx_wdata(tx_wdata),
        .tx_base(phy_base),
        .busy(phy_busy),
        .rx_b0(rx_b0), .rx_b1(rx_b1), .rx_b2(rx_b2), .rx_seq(rx_seq),
        .rx_p0(rx_p0), .rx_p1(rx_p1), .rx_p2(rx_p2),
        .rx_crc_calc(rx_crc_calc), .rx_crc_recv(rx_crc_recv),
        .frame_end_tgl(frame_end_tgl),
        .dbg_mosi_cnt(dbg_mosi_cnt),
        .dbg_sck_cnt(dbg_sck_cnt)
    );

    reg [2:0] fe_s = 3'b000;
    always @(posedge clk_ref_10m)
        fe_s <= {fe_s[1:0], frame_end_tgl};
    wire rx_valid_pulse = (fe_s[2] ^ fe_s[1]);

    wire ref_sel;
    // vnitrni oscilator FPGA (250 MHz / 8 = 31,25 MHz): hlidac reference nesmi zaviset na vnejsich hodinach
    wire osc_clk;
    OSC #(.FREQ_DIV(8), .DEVICE("GW1NR-9C")) u_osc (.OSCOUT(osc_clk));
    ref_guard u_refg (.osc_clk(osc_clk), .ext_sens(ext_ref_sens), .sel_req(ref_sel), .ref_ctrl(ref_ctrl),
                      .ext_present(ref_ext_pres), .ext_lost(ref_ext_lost));
    assign ref_act = ref_ctrl;

    spi_app #(.REGR_EN(TDC_REGR), .BUILD_TIME(BLD_TIME), .BUILD_GIT(BLD_GIT)) u_app (
        .clk(clk_ref_10m),
        .meas_freq_x100000(freq_a_x100000),
        .meas_periods(periods_p),
        .meas_gate_ns(gate_ns_a),
        .meas_timestamp(timestamp_10m),
        .meas_error_flags(meas_err_flags),
        .meas_channel({7'd0, chan_sel}),
        .new_meas(valid_a),
        .signal_lost(signal_lost),
        .meas_freq16_x100000(freq_b_hold),
        .meas_phase_status(meas_phase_zero),
        .meas_status2(meas_status2),
        .meas_dt_a_ps(dt_p),
        .meas_dt_b_ps(dt_s),
        .meas_periods_b(periods_s),
        .meas_tdc_status(tdc_status),
        .meas_cal_diag(cal_diag),
        .meas_tdc_cfg(tdc_cfg),
        .rg_n_a(rg_n_a), .rg_n_b(rg_n_b), .rg_xm_a(rg_xm_a), .rg_xm_b(rg_xm_b),
        // regrese je jen na CH_A: pri CH_B primarnim NEPLATI pro primarni slot (STM by jinak prepsal Δt jeho okna)
        .rg_ym_a(rg_ym_a), .rg_ym_b(rg_ym_b), .rg_ok_a(rg_ok_a & ~chan_sel), .rg_ok_b(rg_ok_b & ~chan_sel),
        .cd_a_end(cde_p), .cd_a_st(cds_p), .cd_b_end(cde_s), .cd_b_st(cds_s),
        .meas_maxtap_a(da_maxtap),
        .meas_maxtap_b(db_maxtap),
        .hist_k(hist_k),
        .meas_hist_a(hq_a),
        .meas_hist_b(hq_b),
        .dbg_mosi_cnt(dbg_mosi_cnt),
        .dbg_sck_cnt(dbg_sck_cnt),
        .meas_clk_status(clk_status),
        .meas_clk_loss(clk_loss),
        .meas_clk_mhz(CLK_MHZ[7:0]),
        .meas_ccd_p(ccd_p),
        .meas_ccd_s(ccd_s),
        .cal_mode(cal_mode),
        .base_win(base_win),
        .chan_sel(chan_sel),
        .ref_sel(ref_sel),
        .rx_valid(rx_valid_pulse),
        .rx_b0(rx_b0), .rx_b1(rx_b1), .rx_b2(rx_b2), .rx_seq(rx_seq),
        .rx_p0(rx_p0), .rx_p1(rx_p1), .rx_p2(rx_p2),
        .rx_crc_calc(rx_crc_calc), .rx_crc_recv(rx_crc_recv),
        .tx_half(tx_half),
        .tx_we(tx_we),
        .tx_waddr(tx_waddr),
        .tx_wdata(tx_wdata),
        .phy_busy(phy_busy),
        .phy_base(phy_base),
        .dbg_status(spi_status)
    );

    // ----------------------------------------------------------
    // LED heartbeat: toggle na každé dokončené měření CH_A (~2-4 Hz blik).
    // ----------------------------------------------------------
    initial led_tx = 1'b1;
    always @(posedge clk_ref_10m)
        if (valid_a) led_tx <= ~led_tx;

    // ----------------------------------------------------------
    // FW 0x0422: LED "FPGA_OK" (D5, PIN63). Bezi z VNITRNIHO oscilatoru FPGA (osc_clk), aby se po vypadku vnejsich hodin nezasekla:
    //   * svit     = bitstream bezi A referencni hodiny jsou OK (10 MHz pritomne A hlidac hodin hlasi 100 MHz v toleranci)
    //   * zhasnuto = bitstream nenabehl, nebo chybi/je mimo toleranci nektera referencni hodina (napr. Si5356 bez zamku)
    // Kalibrace TDC do podminky NEPATRI (reference hodin a TDC jsou ruzne veci; stav TDC je v tdc_status).
    // 10 MHz pritomnost: preklapeny bit z domeny clk_ref_10m, v osc domene detekce hran s casovym oknem.
    // ----------------------------------------------------------
    reg tgl10 = 1'b0;
    always @(posedge clk_ref_10m) tgl10 <= ~tgl10;
    wire good10 = clk_ok;                                  // hlidac hodin: 100 MHz v toleranci (meri se proti 10 MHz)
    reg [2:0] t10_s = 3'b000;
    reg [1:0] good_s = 2'b00;
    reg [7:0] t10_tmo = 8'd0;
    initial fpga_ok = 1'b0;
    always @(posedge osc_clk) begin
        t10_s  <= {t10_s[1:0], tgl10};
        good_s <= {good_s[0], good10};
        if (t10_s[2] ^ t10_s[1])      t10_tmo <= 8'd250;      // hrana ~kazdych 200 ns; 250 taktu osc = ~8 us
        else if (t10_tmo != 8'd0)     t10_tmo <= t10_tmo - 8'd1;
        fpga_ok <= (t10_tmo != 8'd0) & good_s[1];
    end

endmodule
