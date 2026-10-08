// ============================================================
// File: spi_app.v
// SPI aplikační vrstva (doména clk_ref_10m). Protokol v2 dle
// FPGA_PROTOCOL_V2_NAVRH.md (STM32H757 repo): rámec 128 B, VER=0x02.
//  - skládá 128B TX rámec (TYPE=0x80 DATA / 0xA0 CAL report)
//  - CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) přes byte 0..125
//  - validuje RX rámec (MAGIC, CRC), zpracuje TYPE
//  - SEQUENCE (++ na každé nové měření/okno) a FLAGS (VALID/FRESH/...)
//  - (window stream ODSTRANEN ve FW 0x0411; nahradil ho regresni blok, bajty 68..96)
//
// RX TYPEs: 0x01 SET_CONFIG | 0x06 ACK | 0x08 START | 0x09 STOP
//           0xA0 žádost o CAL report (příští TX rámec = 0xA0)
//           ostatní = ignorovat (rezervováno)
//
// SET_CONFIG payload: [12]=config_id, [13]=hodnota
//   0x01 base window: 0=100 ms, 1=250 ms (default), 2=1 s
//   0x02 cal_mode:    0=normal, 1=ring oscilátor místo pin28 (kalibrace)
//
// Rámec (128 B, little-endian vícebajtová pole):
//  0 MAGIC=0xA5 | 1 VER=0x02 | 2 TYPE | 3 FLAGS | 4..7 SEQUENCE
//  8..9 PAYLOAD_LEN=114 | 10..11 RESERVED | 12..125 PAYLOAD | 126..127 CRC16(LE)
//
// DATA payload 0x80 (abs offset; 12..59 je 1:1 s protokolem v1):
//  12 freq_x100000(u64) 20 edge_count(u64) 28 gate_ns(u64) 36 ts(u64)
//  44 channel 45 meas_status 46 error_flags(u32) 50 phase_status
//  51 status2 52..59 freq16_x100000(u64)
//  60..61 fw_version(u16) 62..63 caps(u16) 64 clk_status 65 rez
//  66..67 diag (RX) | 68..96 REGR (FW >= 0x0411, caps bit7): regresni blok CH_A (window stream ODSTRANEN):
//    68..70 n_A(u24)  71..75 xm_A(u40, prumerny index hrany v okne * 256)  76..81 ym_A(u48, prumerny cas * 16,
//    jednotka T/16384, vztazeny k ZACATKU okna)  82..84 n_B  85..89 xm_B  90..95 ym_B  96 {ok_B, ok_A}
//    A = prvni cast okna (G/16..3G/16), B = posledni (13G/16..15G/16). Sklon primky mezi jejich stredy =
//    kmitocet; zisk proti dvoum krajnim bodum je jen u signalu, jehoz faze vuci hodinam TDC "prohazuje".
//  100 tdc_status: bit0=cal_valid A, bit1=cal_valid B, bit2=cal_fail (A|B),
//      bit3=cal_busy (A|B), bit4=retez A kratky (udalosti za koncem), bit5=totez B
//  101..106 dt_b (u48, tez T_clk/16384) | 107 rez | 108..111 edges_b (u32) | 112..117 rez=0
//  118..125 dt_a (u64) = presne okno CH_A v jednotkach T_clk/16384 (= 0,6104 ps,
//           1,6384e12 jednotek/s; caps bit5). ps = jednotky * 625 / 1024.
//  (gate_ns v abs 28 = floor(dt * 5 / 8192), informativni; frequency_x100000 = 0)
//  [Λ/Ω regrese (S1/S2, caps bit4) ODSTRANENA 2026-10-03: s casem v ps by
//   S1 potreboval ~65 b a S2 ~90 b; STM je stejne nikdy nectl. Viz tdc.v.]
//
// CAL payload 0xA0 (abs offset): diagnostika kalibrace TDC
//  12..23 kanal A: ovf(u32 = udalosti za koncem retezu) peak(u32) nz(u16) last(u16)
//  24..35 kanal B: totez | 36 tdc_status | 37 {7'd0,cal_mode} | 38..125 rezerva=0
//
// 🔴 2026-10-03: SKUTECNY carry-chain TDC (`tdc.v`), cas v pikosekundach.
//  (historie 0x0400: CAPS 0x0023 = window stream, SET_CONFIG, dt_ps; aktualne viz FW_VERSION/CAPS nize)
//  SET_CONFIG 0x02 = 1 spusti kalibraci TDC (ring oscilator, ~0,3 s), 0 = nic;
//  kalibrace probehne i sama po zapnuti. Behem ni jsou mereni zablokovana.
//
// 🔴 2026-10-04 (#264): ramce uz nejsou 1024bit vodice, ale bloková RAM v PHY
//  (po bajtech, TX ve dvou polovinach). Viz spi_slave_phy.v.
// ============================================================

module spi_app #(
    parameter REGR_EN = 0           // 1 = regresni blok (bajty 68..96) je ve ramci platny a CAPS bit7 = 1
)(
    input  wire        clk,                  // clk_ref_10m

    input  wire [63:0] meas_freq_x100000,    // 2026-10-03: vždy 0 (host počítá z edges/dt_ps)
    input  wire [31:0] meas_periods,         // počet period v okně -> edge_count
    input  wire [63:0] meas_gate_ns,         // skutečné okno Δt [ns]
    input  wire [63:0] meas_timestamp,       // živý 10MHz tick counter
    input  wire [31:0] meas_error_flags,
    input  wire [7:0]  meas_channel,
    input  wire        new_meas,             // pulz: nové měření k dispozici
    input  wire        signal_lost,          // živý: watchdog (žádné měření ~2,5 s)
    input  wire [63:0] meas_freq16_x100000,  // pin27 /16 kmitočet *100000
    input  wire [7:0]  meas_phase_status,    // {fine_seen[3:0], present[3:0]}
    input  wire [7:0]  meas_status2,         // pin27 status/flags
    input  wire [47:0] meas_dt_a_ps,         // CH_A: okno Δt [T_clk/16384]
    input  wire [47:0] meas_dt_b_ps,         // CH_B: okno Δt [T_clk/16384]
    input  wire [31:0] meas_periods_b,       // CH_B: počet period v okně
    input  wire [7:0]  meas_tdc_status,      // viz hlavička (abs 100)
    input  wire [191:0] meas_cal_diag,       // diagnostika kalibrace A[95:0] B[191:96]
    input  wire [15:0] meas_maxtap_a,        // B1a: nejvyssi KDY set tap A (bubliny: >> d_last)
    input  wire [15:0] meas_maxtap_b,        // B1a: nejvyssi KDY set tap B
    input  wire [15:0] meas_tdc_cfg,         // konfigurace TDC: [4:0] log2(adres tabulky), [5] DUAL, [6] STRIDE==1, [15:7] pocet tapu
    // FW 0x0411: stredni hodnoty casovych znacek vnitrnich hran ve dvou segmentech okna CH_A (regr_acc)
    input  wire [23:0] rg_n_a, rg_n_b,       // pocet znacek v segmentu
    input  wire [39:0] rg_xm_a, rg_xm_b,     // prumerny index hrany v okne * 256
    input  wire [47:0] rg_ym_a, rg_ym_b,     // prumerny cas hrany * 16 [T/16384], vztazeny k zacatku okna
    input  wire        rg_ok_a, rg_ok_b,
    // FW 0x0418: kody TDC uzaviraci/zahajovaci hrany okna (jen kdyz REGR_EN = 0; bajty 68..75, caps bit8)
    input  wire [8:0]  cd_a_end, cd_a_st, cd_b_end, cd_b_st,
    output wire [9:0]  hist_k,               // vypis histogramu: adresa {sada, kod} (CAL pozadavek)
    input  wire [23:0] meas_hist_a,          // hist_A[hist_k] (kvazistaticke)
    input  wire [23:0] meas_hist_b,          // hist_B[hist_k]
    input  wire [15:0] dbg_mosi_cnt,         // diag: hrany MOSI v poslednim ramci (tx_b[116,117])
    input  wire [15:0] dbg_sck_cnt,          // diag: nabezne hrany SCK v poslednim ramci (tx_b[124,125])

    // RX: vysledek prijateho ramce zpracovany za letu v PHY (staticky po CS nahoru)
    input  wire          rx_valid,           // pulz po CS↑ (synchronizováno)
    input  wire [7:0]    rx_b0, rx_b1, rx_b2,// MAGIC, VER, TYPE
    input  wire [31:0]   rx_seq,
    input  wire [7:0]    rx_p0, rx_p1, rx_p2,
    input  wire [15:0]   rx_crc_calc, rx_crc_recv,

    // TX: ramec se sklada po bajtech do NEAKTIVNI poloviny TX RAM PHY
    output wire          tx_half,            // aktivni polovina (PHY ji vysila)
    output wire          tx_we,
    output wire [7:0]    tx_waddr,
    output wire [7:0]    tx_wdata,
    input  wire          phy_busy,           // CS dole (domena 100 MHz, synchronizuje se zde)
    input  wire          phy_base,           // polovina, kterou PHY prave vysila

    output wire [7:0]    dbg_status,
    output wire          cal_mode,           // 1 = RO mux místo pin28
    output wire [1:0]    base_win            // 0=100ms 1=250ms 2=1s
);

    // ---- konstanty rámce ----
    localparam [7:0]  MAGIC           = 8'hA5;
    localparam [7:0]  VERSION         = 8'h02;
    localparam [7:0]  TYPE_DATA       = 8'h80;
    localparam [7:0]  TYPE_CAL        = 8'hA0;
    localparam [7:0]  TYPE_ACK        = 8'h06;
    localparam [7:0]  TYPE_START      = 8'h08;
    localparam [7:0]  TYPE_STOP       = 8'h09;
    localparam [7:0]  TYPE_SET_CONFIG = 8'h01;
    localparam [15:0] PAYLOAD_LEN     = 16'd114;
    localparam [15:0] FW_VERSION      = 16'h041A;  // bump při KAŽDÉ změně bitstreamu
    // 🔴 0x0410 -> 0x0411 (2026-10-07): REGR -- regresni blok (stredni hodnoty znacek vnitrnich hran ve dvou
    // segmentech okna CH_A, abs 68..96) misto window streamu (STM ho nikdy nectl). Viz regr_acc / win_recip.
    // 🔴 0x040F -> 0x0410 (2026-10-07): nova architektura spousteni TDC (volne vzorky q + kompaktni
    // kopie qd, detekce hrany z 2. stupne, zadny samostatny `t0`; viz tdc.v): odstranuje obri bin
    // (pricina: slepota spoustece 1,87/5,3 ns). Dekoder a multiplexer TX bajtu ~3x mensi.
    // CAL report: hist_k 10 bitu, maxtap s rozlisenim 8 tapu, bajty 50..53 = konfigurace TDC.
    // 🔴 0x040C -> 0x040D (2026-10-04, B1a): CAL report nese navic d_maxtap (nejvyssi
    // KDY navzorkovany tap, CAL bajty 46..49) k diagnoze obriho binu -- maxtap>>d_last
    // = bubliny nad prvni nulou, maxtap~d_last = retez tam fyzicky konci. Jen diag,
    // zadna zmena mereni/DATA ramce.
    // 🔴 0x0201 -> 0x0300 (2026-09-30): top.v prešel na novou desku (dva
    // symetricke kanaly CH_A/CH_B, hrube citani na jedne 100MHz referenci
    // misto 4fazoveho vernieru). spi_app.v samo je netknute, ale semantika
    // bitstreamu se zmenila zasadne (freq16_x100000 uz neznamena /16, je to
    // CH_B; phase_status je mrtve pole, vzdy 0 -- zadny TDC jeste neni).
    // Bez bumpu by FW_VERSION lhalo -- stejne cislo jako stara jednokanalova
    // deska, prestoze je to jiny bitstream. Viz pravidlo v radku vyse.
    // caps: bit0=window stream, bit1=SET_CONFIG, bit5=dt_ps (skutečný TDC)
    localparam [15:0] CAPS            = (REGR_EN != 0) ? 16'h00E2 : 16'h0162;   // bit8 = kody TDC v bajtech 68..75   // bit1 SET_CONFIG, bit5 dt_ps, bit6 CAL: hist_k 10 b + konfigurace TDC, bit7 REGR (68..96); bit0 (window stream) ODSTRANEN

    // ---- CRC-16/CCITT-FALSE: zpracuj jeden bajt (8 iterací, MSB-first) ----
    function [15:0] crc16_step;
        input [15:0] crc_in;
        input [7:0]  data;
        integer b;
        reg [15:0] c;
        begin
            c = crc_in ^ {data, 8'd0};
            for (b = 0; b < 8; b = b + 1)
                c = c[15] ? ((c << 1) ^ 16'h1021) : (c << 1);
            crc16_step = c;
        end
    endfunction

    // ---- stav / příznaky ----
    reg [31:0] seq           = 32'd1;
    reg [31:0] last_sent_seq = 32'd0;
    reg        data_valid    = 1'b1;
    reg        data_fresh    = 1'b1;
    reg        ack_ok        = 1'b0;
    reg        rx_crc_error  = 1'b0;

    // držené (latchované) měření = SNIMEK pro skladani ramce. 🔴 Ramec se od
    // 2026-10-04 sklada PO BAJTECH (128 taktu), takze se tyto registry behem
    // skladani NESMI zmenit (jinak by vicebajtove pole bylo roztrzene) -> nove
    // mereni se behem S_TX_WR jen poznamena (meas_pend) a prevezme po nem.
    // Vstupy meas_* drzi top.v az do dalsiho mereni (>= ~100 ms), odklad o
    // max. ~13 us je bezpecny.
    reg [63:0] h_freq  = 64'd12345678901234; // power-on dummy pro bring-up
    reg [63:0] h_edge  = 64'd0;
    reg [63:0] h_gate  = 64'd250000000;      // skutečné okno [ns] (default 0,25 s)
    reg [63:0] h_freq16 = 64'd0;             // pin27 /16 kmitočet *100000
    reg [7:0]  h_phase   = 8'd0;             // {fine_seen[3:0], present[3:0]}
    reg [7:0]  h_status2 = 8'd0;
    reg [63:0] h_ts    = 64'd0;
    reg [31:0] h_err   = 32'd0;
    reg [7:0]  h_ch    = 8'd0;
    reg [47:0] h_dt_a = 48'd0;               // okno CH_A [T_clk/16384]
    reg [47:0] h_dt_b = 48'd0;               // okno CH_B [T_clk/16384]
    reg [31:0] h_edge_b = 32'd0;
    reg [1:0]  meas_pend = 2'd0;         // pocet mereni odlozenych behem S_TX_WR (saturace 3)


    // konfigurace (SET_CONFIG)
    reg        cal_mode_r = 1'b0;
    // CAL pozadavek s payloadem [12]=1 -> rezim vypisu histogramu, [14] = kod
    reg        hist_mode = 1'b0;
    reg [9:0]  hist_k_r  = 10'd0;
    assign hist_k = hist_k_r;
    // DIAGNOSTIKA RX (2026-10-03): co FPGA skutecne prijala od STM v poslednim ramci
    // -> DATA [66,67,107,112..115] = rx[0], rx[1], rx[2], CRC spoctene (lo,hi), prijate (lo,hi)
    reg [7:0]  d_rx0 = 8'd0, d_rx1 = 8'd0, d_rx2 = 8'd0;
    reg [7:0]  d_ccl = 8'd0, d_cch = 8'd0, d_rcl = 8'd0, d_rch = 8'd0;
    reg [1:0]  base_win_r = 2'd1;            // default 250 ms
    assign cal_mode = cal_mode_r;
    assign base_win = base_win_r;

    // žádost o CAL report (one-shot: příští TX rámec = 0xA0)
    reg        cal_req = 1'b0;

    // FSM
    localparam [2:0] S_IDLE    = 3'd0;
    localparam [2:0] S_TX_WR   = 3'd1;   // 128 bajtu do TX RAM + CRC
    localparam [2:0] S_RX_PROC = 3'd5;   // vyhodnoceni ramce (CRC spocitala PHY za letu)

    reg [2:0]  state    = S_IDLE;
    reg [7:0]  idx      = 8'd0;          // index skladaneho TX bajtu 0..127
    reg [15:0] crc_acc  = 16'hFFFF;
    reg        tx_dirty = 1'b1;          // postav první rámec po resetu
    reg        meas_dirty = 1'b0;
    reg        rx_pend  = 1'b0;          // rx_valid zachyceny i mimo S_IDLE
    reg        is_cal   = 1'b0;          // skladany ramec = CAL report
    reg [2:0]  cool     = 3'd0;          // odstup po prepnuti poloviny (CDC, viz nize)

    // TX RAM: aplikace pise do NEAKTIVNI poloviny (~tx_half_r), PHY vysila aktivni
    reg        tx_half_r = 1'b0;
    assign tx_half = tx_half_r;
    reg        tx_we_r   = 1'b0;
    reg [7:0]  tx_waddr_r = 8'd0, tx_wdata_r = 8'd0;
    assign tx_we = tx_we_r; assign tx_waddr = tx_waddr_r; assign tx_wdata = tx_wdata_r;

    // stav PHY do teto domeny (busy = CS dole, base = polovina, kterou PHY vysila)
    reg [2:0]  pb_s = 3'b000, pbase_s = 3'b000;
    always @(posedge clk) begin pb_s <= {pb_s[1:0], phy_busy}; pbase_s <= {pbase_s[1:0], phy_base}; end
    // Zapis do ~tx_half_r je bezpecny, kdyz PHY nevysila PRAVE tuto polovinu.
    // `cool` (>= 4 takty po prepnuti) pokryje zpozdeni synchronizatoru: PHY mohla
    // pri CS dole zamknout jeste starou polovinu, tedy tu, do ktere by se psalo.
    wire       tx_can = (cool == 3'd0) && !(pb_s[2] && (pbase_s[2] == ~tx_half_r));


    // error_flags do rámce: latchované chyby měření + ŽIVÝ signal_lost (bit1),
    // ten musí jít do rámce i bez new_meas. bit0=měření, bit1=signal_lost, bit2=Δt ovf.
    wire [31:0] err_word = h_err | {30'd0, signal_lost, 1'b0};

    wire [7:0] flags_byte = {(|err_word), ack_ok, rx_crc_error,
                             1'b0 /*busy*/, 1'b0 /*fifo_ovf*/,
                             ~data_valid /*fifo_empty*/, data_fresh, data_valid};

    assign dbg_status = flags_byte;

    // ---- obsah TX bajtu podle indexu: ploche `case` nad bajty (2026-10-07; generovano ze seznamu poli) ----
    // Drive retez porovnani rozsahu s odecitanim indexu (`idx >= a && idx <= b` + `x[8*(idx-a) +: 8]`)
    // dával ~800 LUT (8 bitu x ~100). Ploche `case` je cisty 128:1 mux nad konstantnimi vyrezy.
    reg [7:0] tb;
    always @* begin
        tb = 8'd0;
        case (idx[6:0])
            7'd0:  tb = MAGIC;
            7'd1:  tb = VERSION;
            7'd2:  tb = is_cal ? TYPE_CAL : TYPE_DATA;
            7'd3:  tb = flags_byte;
            // SEQUENCE je soucast snimku (behem S_TX_WR se nemeni) -> patri ke stejnemu mereni jako data
            7'd4:  tb = seq[7:0];
            7'd5:  tb = seq[15:8];
            7'd6:  tb = seq[23:16];
            7'd7:  tb = seq[31:24];
            7'd8:  tb = PAYLOAD_LEN[7:0];
            7'd9:  tb = PAYLOAD_LEN[15:8];
            7'd12: tb = is_cal ? (meas_cal_diag[7:0]) : (h_freq[7:0]);
            7'd13: tb = is_cal ? (meas_cal_diag[15:8]) : (h_freq[15:8]);
            7'd14: tb = is_cal ? (meas_cal_diag[23:16]) : (h_freq[23:16]);
            7'd15: tb = is_cal ? (meas_cal_diag[31:24]) : (h_freq[31:24]);
            7'd16: tb = is_cal ? (meas_cal_diag[39:32]) : (h_freq[39:32]);
            7'd17: tb = is_cal ? (meas_cal_diag[47:40]) : (h_freq[47:40]);
            7'd18: tb = is_cal ? (meas_cal_diag[55:48]) : (h_freq[55:48]);
            7'd19: tb = is_cal ? (meas_cal_diag[63:56]) : (h_freq[63:56]);
            7'd20: tb = is_cal ? (meas_cal_diag[71:64]) : (h_edge[7:0]);
            7'd21: tb = is_cal ? (meas_cal_diag[79:72]) : (h_edge[15:8]);
            7'd22: tb = is_cal ? (meas_cal_diag[87:80]) : (h_edge[23:16]);
            7'd23: tb = is_cal ? (meas_cal_diag[95:88]) : (h_edge[31:24]);
            7'd24: tb = is_cal ? (meas_cal_diag[103:96]) : (h_edge[39:32]);
            7'd25: tb = is_cal ? (meas_cal_diag[111:104]) : (h_edge[47:40]);
            7'd26: tb = is_cal ? (meas_cal_diag[119:112]) : (h_edge[55:48]);
            7'd27: tb = is_cal ? (meas_cal_diag[127:120]) : (h_edge[63:56]);
            7'd28: tb = is_cal ? (meas_cal_diag[135:128]) : (h_gate[7:0]);
            7'd29: tb = is_cal ? (meas_cal_diag[143:136]) : (h_gate[15:8]);
            7'd30: tb = is_cal ? (meas_cal_diag[151:144]) : (h_gate[23:16]);
            7'd31: tb = is_cal ? (meas_cal_diag[159:152]) : (h_gate[31:24]);
            7'd32: tb = is_cal ? (meas_cal_diag[167:160]) : (h_gate[39:32]);
            7'd33: tb = is_cal ? (meas_cal_diag[175:168]) : (h_gate[47:40]);
            7'd34: tb = is_cal ? (meas_cal_diag[183:176]) : (h_gate[55:48]);
            7'd35: tb = is_cal ? (meas_cal_diag[191:184]) : (h_gate[63:56]);
            7'd36: tb = is_cal ? (meas_tdc_status) : (h_ts[7:0]);
            7'd37: tb = is_cal ? ({7'd0, cal_mode_r}) : (h_ts[15:8]);
            7'd38: tb = is_cal ? ({5'd0, hist_mode ? hist_k_r[9:8] : 2'd0, hist_mode}) : (h_ts[23:16]);
            7'd39: tb = is_cal ? (hist_mode ? hist_k_r[7:0] : 8'd0) : (h_ts[31:24]);
            7'd40: tb = is_cal ? (hist_mode ? meas_hist_a[7:0] : 8'd0) : (h_ts[39:32]);
            7'd41: tb = is_cal ? (hist_mode ? meas_hist_a[15:8] : 8'd0) : (h_ts[47:40]);
            7'd42: tb = is_cal ? (hist_mode ? meas_hist_a[23:16] : 8'd0) : (h_ts[55:48]);
            7'd43: tb = is_cal ? (hist_mode ? meas_hist_b[7:0] : 8'd0) : (h_ts[63:56]);
            7'd44: tb = is_cal ? (hist_mode ? meas_hist_b[15:8] : 8'd0) : (h_ch);
            7'd45: tb = is_cal ? (hist_mode ? meas_hist_b[23:16] : 8'd0) : ({6'd0, data_fresh, data_valid});
            7'd46: tb = is_cal ? (meas_maxtap_a[7:0]) : (err_word[7:0]);
            7'd47: tb = is_cal ? (meas_maxtap_a[15:8]) : (err_word[15:8]);
            7'd48: tb = is_cal ? (meas_maxtap_b[7:0]) : (err_word[23:16]);
            7'd49: tb = is_cal ? (meas_maxtap_b[15:8]) : (err_word[31:24]);
            7'd50: tb = is_cal ? (meas_tdc_cfg[7:0]) : (h_phase);
            7'd51: tb = is_cal ? (meas_tdc_cfg[15:8]) : (h_status2);
            7'd52: tb = is_cal ? 8'd0 : (h_freq16[7:0]);
            7'd53: tb = is_cal ? 8'd0 : (h_freq16[15:8]);
            7'd54: tb = is_cal ? 8'd0 : (h_freq16[23:16]);
            7'd55: tb = is_cal ? 8'd0 : (h_freq16[31:24]);
            7'd56: tb = is_cal ? 8'd0 : (h_freq16[39:32]);
            7'd57: tb = is_cal ? 8'd0 : (h_freq16[47:40]);
            7'd58: tb = is_cal ? 8'd0 : (h_freq16[55:48]);
            7'd59: tb = is_cal ? 8'd0 : (h_freq16[63:56]);
            7'd60: tb = is_cal ? 8'd0 : (FW_VERSION[7:0]);
            7'd61: tb = is_cal ? 8'd0 : (FW_VERSION[15:8]);
            7'd62: tb = is_cal ? 8'd0 : (CAPS[7:0]);
            7'd63: tb = is_cal ? 8'd0 : (CAPS[15:8]);
            7'd64: tb = is_cal ? 8'd0 : (8'h01);
            7'd66: tb = is_cal ? 8'd0 : (d_rx0);
            7'd67: tb = is_cal ? 8'd0 : (d_rx1);
            7'd68: tb = is_cal ? 8'd0 : ((REGR_EN != 0) ? rg_n_a[7:0]    : cd_a_end[7:0]);
            7'd69: tb = is_cal ? 8'd0 : ((REGR_EN != 0) ? rg_n_a[15:8]   : {7'd0, cd_a_end[8]});
            7'd70: tb = is_cal ? 8'd0 : ((REGR_EN != 0) ? rg_n_a[23:16]  : cd_a_st[7:0]);
            7'd71: tb = is_cal ? 8'd0 : ((REGR_EN != 0) ? rg_xm_a[7:0]   : {7'd0, cd_a_st[8]});
            7'd72: tb = is_cal ? 8'd0 : ((REGR_EN != 0) ? rg_xm_a[15:8]  : cd_b_end[7:0]);
            7'd73: tb = is_cal ? 8'd0 : ((REGR_EN != 0) ? rg_xm_a[23:16] : {7'd0, cd_b_end[8]});
            7'd74: tb = is_cal ? 8'd0 : ((REGR_EN != 0) ? rg_xm_a[31:24] : cd_b_st[7:0]);
            7'd75: tb = is_cal ? 8'd0 : ((REGR_EN != 0) ? rg_xm_a[39:32] : {7'd0, cd_b_st[8]});
            7'd76: tb = is_cal ? 8'd0 : (rg_ym_a[7:0]);
            7'd77: tb = is_cal ? 8'd0 : (rg_ym_a[15:8]);
            7'd78: tb = is_cal ? 8'd0 : (rg_ym_a[23:16]);
            7'd79: tb = is_cal ? 8'd0 : (rg_ym_a[31:24]);
            7'd80: tb = is_cal ? 8'd0 : (rg_ym_a[39:32]);
            7'd81: tb = is_cal ? 8'd0 : (rg_ym_a[47:40]);
            7'd82: tb = is_cal ? 8'd0 : (rg_n_b[7:0]);
            7'd83: tb = is_cal ? 8'd0 : (rg_n_b[15:8]);
            7'd84: tb = is_cal ? 8'd0 : (rg_n_b[23:16]);
            7'd85: tb = is_cal ? 8'd0 : (rg_xm_b[7:0]);
            7'd86: tb = is_cal ? 8'd0 : (rg_xm_b[15:8]);
            7'd87: tb = is_cal ? 8'd0 : (rg_xm_b[23:16]);
            7'd88: tb = is_cal ? 8'd0 : (rg_xm_b[31:24]);
            7'd89: tb = is_cal ? 8'd0 : (rg_xm_b[39:32]);
            7'd90: tb = is_cal ? 8'd0 : (rg_ym_b[7:0]);
            7'd91: tb = is_cal ? 8'd0 : (rg_ym_b[15:8]);
            7'd92: tb = is_cal ? 8'd0 : (rg_ym_b[23:16]);
            7'd93: tb = is_cal ? 8'd0 : (rg_ym_b[31:24]);
            7'd94: tb = is_cal ? 8'd0 : (rg_ym_b[39:32]);
            7'd95: tb = is_cal ? 8'd0 : (rg_ym_b[47:40]);
            7'd96: tb = is_cal ? 8'd0 : ({6'd0, rg_ok_b, rg_ok_a});
            7'd100: tb = is_cal ? 8'd0 : (meas_tdc_status);
            7'd101: tb = is_cal ? 8'd0 : (h_dt_b[7:0]);
            7'd102: tb = is_cal ? 8'd0 : (h_dt_b[15:8]);
            7'd103: tb = is_cal ? 8'd0 : (h_dt_b[23:16]);
            7'd104: tb = is_cal ? 8'd0 : (h_dt_b[31:24]);
            7'd105: tb = is_cal ? 8'd0 : (h_dt_b[39:32]);
            7'd106: tb = is_cal ? 8'd0 : (h_dt_b[47:40]);
            7'd107: tb = is_cal ? 8'd0 : (d_rx2);
            7'd108: tb = is_cal ? 8'd0 : (h_edge_b[7:0]);
            7'd109: tb = is_cal ? 8'd0 : (h_edge_b[15:8]);
            7'd110: tb = is_cal ? 8'd0 : (h_edge_b[23:16]);
            7'd111: tb = is_cal ? 8'd0 : (h_edge_b[31:24]);
            7'd112: tb = is_cal ? 8'd0 : (d_ccl);
            7'd113: tb = is_cal ? 8'd0 : (d_cch);
            7'd114: tb = is_cal ? 8'd0 : (d_rcl);
            7'd115: tb = is_cal ? 8'd0 : (d_rch);
            7'd116: tb = is_cal ? 8'd0 : (dbg_mosi_cnt[7:0]);
            7'd117: tb = is_cal ? 8'd0 : (dbg_mosi_cnt[15:8]);
            7'd118: tb = is_cal ? 8'd0 : (h_dt_a[7:0]);
            7'd119: tb = is_cal ? 8'd0 : (h_dt_a[15:8]);
            7'd120: tb = is_cal ? 8'd0 : (h_dt_a[23:16]);
            7'd121: tb = is_cal ? 8'd0 : (h_dt_a[31:24]);
            7'd122: tb = is_cal ? 8'd0 : (h_dt_a[39:32]);
            7'd123: tb = is_cal ? 8'd0 : (h_dt_a[47:40]);
            7'd124: tb = is_cal ? 8'd0 : (dbg_sck_cnt[7:0]);
            7'd125: tb = is_cal ? 8'd0 : (dbg_sck_cnt[15:8]);
            default: tb = 8'd0;
        endcase
    end

    always @(posedge clk) begin
        tx_we_r <= 1'b0;
        if (cool != 3'd0) cool <= cool - 3'd1;

        // ---- nové měření: SEQUENCE++, FRESH/VALID, latch dat ----
        // (behem skladani ramce jen poznamenat, prevzit az po nem -- viz h_*)
        // Vic mereni behem jednoho skladani se slije do jednoho snimku (vstupy nesou
        // jen posledni), ale SEQUENCE poroste o JEJICH POCET -> STM sloucene
        // mereni pozna jako diru v SEQUENCE a statistika ho poctive zapocita.
        if (new_meas && state == S_TX_WR && meas_pend != 2'd3) meas_pend <= meas_pend + 2'd1;
        if ((new_meas || meas_pend != 2'd0) && state != S_TX_WR) begin
            meas_pend  <= 2'd0;
            seq        <= seq + {30'd0, meas_pend} + {31'd0, new_meas};
            data_valid <= 1'b1;
            data_fresh <= 1'b1;
            h_freq     <= meas_freq_x100000;
            h_edge     <= {32'd0, meas_periods}; // edge_count = počet period v okně
            h_gate     <= meas_gate_ns;          // skutečné Δt okna [ns]
            h_freq16   <= meas_freq16_x100000;
            h_phase    <= meas_phase_status;
            h_status2  <= meas_status2;
            h_ts       <= meas_timestamp;
            h_err      <= meas_error_flags;
            h_ch       <= meas_channel;
            h_dt_a     <= meas_dt_a_ps;
            h_dt_b     <= meas_dt_b_ps;
            h_edge_b   <= meas_periods_b;
            meas_dirty <= 1'b1;
        end else if (signal_lost && !new_meas && meas_pend == 2'd0) begin
            data_valid <= 1'b0;          // ztráta signálu -> data nejsou platná
        end

        // sběr rx_valid i mimo S_IDLE (konzumace níže pulz zároveň smaže)
        if (rx_valid) rx_pend <= 1'b1;

        case (state)
            S_IDLE: begin
                if (rx_pend || rx_valid) begin
                    // zpracuj příchozí rámec (priorita)
                    rx_pend    <= 1'b0;
                    state      <= S_RX_PROC;
                end else if ((meas_dirty || tx_dirty) && tx_can) begin
                    // zacni skladat do NEAKTIVNI poloviny; snimek h_* drzi (meas_pend)
                    is_cal     <= cal_req;
                    crc_acc    <= 16'hFFFF;
                    idx        <= 8'd0;
                    meas_dirty <= 1'b0;
                    tx_dirty   <= 1'b0;
                    state      <= S_TX_WR;
                end
            end

            S_TX_WR: begin
                // bajt idx: 0..125 obsah (+CRC), 126/127 CRC (LE)
                tx_we_r    <= 1'b1;
                tx_waddr_r <= {~tx_half_r, idx[6:0]};
                if (idx < 8'd126) begin
                    tx_wdata_r <= tb;
                    crc_acc    <= crc16_step(crc_acc, tb);
                    idx        <= idx + 8'd1;
                end else if (idx == 8'd126) begin
                    tx_wdata_r <= crc_acc[7:0];
                    idx        <= idx + 8'd1;
                end else begin
                    tx_wdata_r    <= crc_acc[15:8];
                    // ramec kompletni -> prepnout polovinu (zapis bajtu 127 probehne
                    // TIMTO taktem; PHY prepnuti uvidi az pres 3stup. synchronizator)
                    tx_half_r     <= ~tx_half_r;
                    cool          <= 3'd5;
                    last_sent_seq <= seq;
                    if (is_cal) cal_req <= 1'b0;
                    state         <= S_IDLE;
                end
            end

            S_RX_PROC: begin
                d_rx0 <= rx_b0;  d_rx1 <= rx_b1;  d_rx2 <= rx_b2;
                d_ccl <= rx_crc_calc[7:0]; d_cch <= rx_crc_calc[15:8];
                d_rcl <= rx_crc_recv[7:0]; d_rch <= rx_crc_recv[15:8];
                if (rx_b0 == MAGIC && rx_crc_calc == rx_crc_recv) begin
                    rx_crc_error <= 1'b0;
                    case (rx_b2)
                        TYPE_ACK: begin
                            ack_ok <= 1'b1;
                            // SEQUENCE v ACK (LE) == naposledy odeslaná -> shoď FRESH
                            if (rx_seq == last_sent_seq) data_fresh <= 1'b0;
                        end
                        TYPE_START: ack_ok <= 1'b0; // continuous měření běží i tak
                        TYPE_STOP:  ack_ok <= 1'b0;
                        TYPE_SET_CONFIG: begin
                            ack_ok <= 1'b0;
                            case (rx_p0)
                                8'h01: base_win_r <= (rx_p1 <= 8'd2) ? rx_p1[1:0] : 2'd1;
                                8'h02: cal_mode_r <= rx_p1[0];
                                default: ;      // neznámé config_id = ignorovat
                            endcase
                        end
                        TYPE_CAL: begin          // žádost o CAL report
                            ack_ok    <= 1'b0;
                            cal_req   <= 1'b1;
                            // [12] bit0 = 1: vypis hist[k] (k = {[12][2:1], [14]}, 10 bitu; starsi STM posila
                            // [12] == 1 a k < 256 -> stejny vyznam); jinak souhrn
                            hist_mode <= (rx_p0[0] && rx_p0[7:3] == 5'd0);
                            if (rx_p0[0] && rx_p0[7:3] == 5'd0) hist_k_r <= {rx_p0[2:1], rx_p2};
                        end
                        default:    ack_ok <= 1'b0;  // rezervované TYPE = ignorovat
                    endcase
                end else begin
                    rx_crc_error <= 1'b1;
                    ack_ok       <= 1'b0;
                end
                tx_dirty <= 1'b1;   // připrav nový TX rámec pro příští transakci
                state    <= S_IDLE;
            end

            default: state <= S_IDLE;
        endcase
    end

endmodule


// ============================================================
// Pomocné moduly měření kmitočtu (drženy zde, ne v samostatných souborech,
// aby je překlad viděl i bez aktualizace seznamu zdrojů v IDE/.prj).
// ============================================================

// ------------------------------------------------------------
// gate_div: gate_ns = floor(dt * 5 / 8192) = floor(dt[T/16384] * 0,6104 ps / 1000).
//   🔴 2026-10-03: nahrazuje `recip_calc` (reciproký výpočet kmitočtu, ~450
//   registrů) i dělení 1000. FPGA NEPOČÍTÁ frequency_x100000 (v rámci = 0):
//   přesný poměr edge_count / dt počítá host. Jednotka času je T_clk/16384,
//   takže převod na ns je jen dt*5 >> 13 (bez děliček).
// ------------------------------------------------------------
module gate_div (
    input  wire        clk,            // clk_ref_10m
    input  wire        start,          // 1-taktový puls: dt platné
    input  wire [47:0] dt,             // Δt [T_clk/16384]
    output reg  [63:0] gate_ns,
    output reg         valid,          // 1-taktový puls: gate_ns platné
    output wire        busy
);
    assign busy = 1'b0;
    wire [49:0] x5 = {2'b00, dt} + {dt, 2'b00};        // dt * 5
    initial begin gate_ns = 64'd0; valid = 1'b0; end
    always @(posedge clk) begin
        valid <= start;
        if (start) gate_ns <= {27'd0, x5[49:13]};      // max ~1,6e11 (38 b)
    end
endmodule


// ============================================================
// Lean 4fázové měření kmitočtu (drženo zde, ne v samostatném souboru,
// aby je překlad viděl i bez aktualizace seznamu zdrojů v IDE/.prj).
//   phase_oversampler : 4fázový oversampling -> hrana + poloha 2,5 ns
//   win_recip         : windowed reciproký čítač (f = periods/Δt)
//                       + Λ/Ω akumulátory + fine kódy krajních hran
//   phase_check       : přítomnost (živost) 4 fázových hodin
// 4 fáze: clk_p0/p2p5/p5/p7p5 = 0/2,5/5/7,5 ns (90°).
// ============================================================

module phase_oversampler (
    input  wire        clk_p0,
    input  wire        clk_p2p5,
    input  wire        clk_p5,
    input  wire        clk_p7p5,
    input  wire        sig_in,        // asynchronní vstup
    output reg         sig_rise,
    output reg  [1:0]  fine
);
    // 2-stupňový synchronizér async sig_in v každé fázi (metastabilita)
    reg s0a = 1'b0, s1a = 1'b0, s2a = 1'b0, s3a = 1'b0;
    reg s0  = 1'b0, s1  = 1'b0, s2  = 1'b0, s3  = 1'b0;
    always @(posedge clk_p0)   begin s0a <= sig_in; s0 <= s0a; end
    always @(posedge clk_p2p5) begin s1a <= sig_in; s1 <= s1a; end
    always @(posedge clk_p5)   begin s2a <= sig_in; s2 <= s2a; end
    always @(posedge clk_p7p5) begin s3a <= sig_in; s3 <= s3a; end

    reg [3:0] os_r1 = 4'b0;     // {s3,s2,s1,s0}
    reg [3:0] os_r2 = 4'b0;
    reg       prev_last = 1'b0; // s3 minulého okna

    wire       any_high   = |os_r2;
    wire       all_low    = (os_r2 == 4'b0000) && (prev_last == 1'b0);
    wire [1:0] first_high = os_r2[0] ? 2'd0 : os_r2[1] ? 2'd1 : os_r2[2] ? 2'd2 : 2'd3;

    // debounce: jedna hrana za potvrzenou periodu (LOW okno mezi náběžnými
    // hranami) -> odolné vůči zákmitům/ringingu i metastabilitě. Strop ~40 MHz
    // na pinu (nutné aspoň 1 plně LOW okno mezi hranami).
    reg state = 1'b0;          // 0 = potvrzeno LOW, 1 = HIGH (čeká na návrat LOW)

    initial begin sig_rise = 1'b0; fine = 2'd0; end

    always @(posedge clk_p0) begin
        os_r1     <= {s3, s2, s1, s0};
        os_r2     <= os_r1;
        prev_last <= os_r2[3];

        sig_rise <= 1'b0;
        if (!state) begin
            if (any_high) begin            // potvrzená LOW->HIGH = 1 hrana/perioda
                sig_rise <= 1'b1;
                fine     <= first_high;
                state    <= 1'b1;
            end
        end else if (all_low) begin        // čekej na plně LOW okno (debounce)
            state <= 1'b0;
        end
    end
endmodule


// ------------------------------------------------------------
// regr_acc: akumulace casovych znacek VNITRNICH hran ve dvou segmentech okna -> stredni hodnoty (n, x, y).
//
// PROC: dva krajni body okna davaji sigma_f/f = sqrt2 * sigma_znacky / T_okna. Prumer z n znacek potlaci
// NAHODNOU slozku chyby znacky o sqrt(n) (zmereno 2026-10-08: chyba znacky je bily sum ~105 ps, viz
// docs/TDC_MATEMATIKA.md kap. 10). Kmitocet = sklon primky mezi stredy segmentu:
//     f = (x_B - x_A) / (y_B - y_A),   x = index hrany v okne, y = cas hrany
// Deterministicka slozka (kvantizace ~30 ps) se pri pomalu se menici fazi (vstup ~ nasobek 100 MHz) nezprumeruje.
//
// Vystup: n, x_prumer*256, y_prumer*16 (y relativne k `ref_ts`):
//   xm = x0*256 + floor(sum(x-x0)*256 / n)          ym = (t0-ref)*16 + floor(sum(t-t0)*16 / n)
//
// CASOVANI (FW 0x041A, 2026-10-08): na tomto cipu stoji jeden skok vedenim 2-3 ns, takze kazda cesta ma mit
// nejvys ~2 urovne LUT a zadny ridici signal nesmi rozvadet do desitek FF. Predchozi verze mely rezervu
// 0,01-0,2 ns a na krzemiku podle rozmisteni selhavaly (0x0411: segment B se neaktualizoval; jiny build:
// sum(t-t0) o 1,3 % nizsi), prestoze logika byla spravna (overeno na desce citaci prenosu a sim/tb_regr_full.sv).
// Proto:
//   * zacatek/konec segmentu jsou REGISTROVANE (znacky prichazeji >= 9 taktu po nabehu segmentu),
//   * rozdily x-x0, t-t0 se meni JEN pri znacce (enable),
//   * sx i sy jsou rozdelene s registrovanym prenosem, n je 3 x 8 b,
//   * delic: rozdil `dsub` se pocita KAZDY takt (bez enable), vyber a posun v kazdem 2. taktu; ridici signaly
//     posunu/nacteni jsou predpocitane registry ve 4 KOPIICH (kazda ridi ctvrtinu bitu `nq`).
// ------------------------------------------------------------
module regr_acc (
    input  wire        clk,             // clk_p0_100m
    input  wire        seg_a,           // segment A aktivni (quasi-staticke)
    input  wire        seg_b,           // segment B aktivni
    input  wire [47:0] ref_ts,          // zacatek okna = ts predchozi uzaviraci hrany [T/16384]
    input  wire        samp_v,          // 1 takt: znacka vnitrni hrany je platna
    input  wire        samp_b,          // znacka patri do segmentu B (zachyceno pri spusteni)
    input  wire [25:0] samp_x,          // poradi hrany v okne
    input  wire [47:0] samp_ts,         // absolutni cas hrany [T/16384]
    output reg  [23:0] n_a, n_b,
    output reg  [39:0] xm_a, xm_b,      // x_prumer * 256
    output reg  [47:0] ym_a, ym_b,      // y_prumer * 16
    output reg         ok_a, ok_b
);
    initial begin n_a = 0; n_b = 0; xm_a = 0; xm_b = 0; ym_a = 0; ym_b = 0; ok_a = 0; ok_b = 0; end

    // ---- hrany segmentu (registrovane) ----
    reg sa_p = 1'b0, sb_p = 1'b0;
    reg beg_a = 1'b0, beg_b = 1'b0, beg = 1'b0, end_a = 1'b0, end_b = 1'b0;
    always @(posedge clk) begin
        sa_p  <= seg_a;               sb_p  <= seg_b;
        beg_a <= ~sa_p & seg_a;       beg_b <= ~sb_p & seg_b;
        beg   <= (~sa_p & seg_a) | (~sb_p & seg_b);
        end_a <= sa_p & ~seg_a;       end_b <= sb_p & ~seg_b;
    end

    // ---- akumulator ----
    reg        first = 1'b1;
    reg [7:0]  n_lo = 8'd0, n_md = 8'd0, n_hi = 8'd0;
    reg        c1 = 1'b0, c2 = 1'b0;
    wire [23:0] n = {n_hi, n_md, n_lo};
    reg [25:0] x0 = 26'd0;
    reg [47:0] t0 = 48'd0;
    reg [25:0] sx_lo = 26'd0;  reg [21:0] sx_hi = 22'd0;  reg sx_c = 1'b0;   // sum(x - x0) = {sx_hi, sx_lo}
    reg [31:0] sy_lo = 32'd0;  reg [31:0] sy_hi = 32'd0;  reg sy_c = 1'b0;   // sum(t - t0) = {sy_hi, sy_lo}
    wire [47:0] sx = {sx_hi, sx_lo};
    wire [63:0] sy = {sy_hi, sy_lo};
    reg        v1 = 1'b0, f1 = 1'b0, v2 = 1'b0;
    reg [25:0] dx_r = 26'd0;
    reg [39:0] dt_r = 40'd0;                // rozdil < 2^40 (segment < 0,67 s)
    reg [7:0]  dth_d = 8'd0;
    wire       act = (samp_b ? seg_b : seg_a) & (seg_a | seg_b);

    always @(posedge clk) begin
        // stupen 1: rozdily jen pri znacce (pak jsou stabilni az do dalsi, >= 8 taktu)
        v1 <= samp_v & act;
        f1 <= first;
        if (samp_v) begin
            dx_r <= samp_x - x0;
            dt_r <= samp_ts[39:0] - t0[39:0];
        end
        if (samp_v & act & first) begin first <= 1'b0; x0 <= samp_x; t0 <= samp_ts; end
        // stupen 2: dolni casti souctu + citac
        c1 <= 1'b0; c2 <= 1'b0;
        v2 <= v1 & ~f1;
        if (v1) begin
            n_lo  <= n_lo + 8'd1; c1 <= (n_lo == 8'hFF);
            dth_d <= dt_r[39:32];
            if (!f1) begin
                {sy_c, sy_lo} <= {1'b0, sy_lo} + {1'b0, dt_r[31:0]};
                {sx_c, sx_lo} <= {1'b0, sx_lo} + {1'b0, dx_r};
            end
        end
        if (c1) begin n_md <= n_md + 8'd1; c2 <= (n_md == 8'hFF); end
        if (c2) n_hi <= n_hi + 8'd1;
        // stupen 3: horni casti
        if (v2) begin
            sy_hi <= sy_hi + {24'd0, dth_d} + {31'd0, sy_c};
            sx_hi <= sx_hi + {21'd0, sx_c};
        end
        // zacatek segmentu (registrovany; nikdy soucasne se znackou)
        if (beg) begin
            first <= 1'b1;
            n_lo <= 8'd0; n_md <= 8'd0; n_hi <= 8'd0;
            sx_lo <= 26'd0; sx_hi <= 22'd0; sy_lo <= 32'd0; sy_hi <= 32'd0;
        end
    end

    // ---- delic: dva prubehy (x, pak y), 68 iteraci po 2 taktech ----
    reg        for_b = 1'b0, phase_y = 1'b0;
    reg        waitf = 1'b0;  reg [2:0] wc = 3'd0;
    reg        busy = 1'b0, fsel = 1'b0, lastr = 1'b0;
    (* keep = "true" *) reg [3:0] ldx_q = 4'd0;      // nacti delenec x (4 kopie)
    (* keep = "true" *) reg [3:0] ldy_q = 4'd0;      // nacti delenec y
    (* keep = "true" *) reg [3:0] sh_q  = 4'd0;      // vyber + posun (takt s fsel = 1)
    reg [6:0]  cnt = 7'd0;
    reg [24:0] rem = 25'd0;
    reg [67:0] nq  = 68'd0;
    reg [25:0] dsub = 26'd0;
    reg [39:0] xq  = 40'd0;
    reg [47:0] y0r = 48'd0;
    wire [24:0] rem_n = {rem[23:0], nq[67]};
    wire        qb    = ~dsub[25];
    wire [67:0] dvx   = {12'd0, sx, 8'd0};
    wire [67:0] dvy   = {sy, 4'd0};

    // dalsi stav ridicich signalu (z registru -> kratke cesty); kopie se registruji ze stejneho vyrazu
    reg  n_ge2 = 1'b0;                                   // n >= 2 (registrovano; n je pri rozhodnuti 5 taktu stabilni)
    wire go_x     = waitf && (wc == 3'd4) && n_ge2;
    wire done_sel = busy & fsel & lastr;                 // posledni vyber prubehu
    wire ld_now   = ldx_q[0] | ldy_q[0];
    wire busy_nx  = ld_now | (busy & ~done_sel);
    wire fsel_nx  = ld_now ? 1'b0 : (busy & ~done_sel & ~fsel);
    wire sh_nx    = busy_nx & fsel_nx;

    integer i;
    always @(posedge clk) begin
        // start po konci segmentu: pockat 5 taktu na dobeh pipeline akumulatoru, pak posoudit n
        if ((end_a | end_b) && !busy && !waitf && !ld_now) begin
            for_b <= end_b; waitf <= 1'b1; wc <= 3'd0;
        end
        if (waitf) begin
            wc <= wc + 3'd1;
            if (wc == 3'd4) begin
                waitf <= 1'b0;
                if (!n_ge2) begin if (for_b) ok_b <= 1'b0; else ok_a <= 1'b0; end
            end
        end
        ldx_q <= {4{go_x}};
        ldy_q <= {4{done_sel & ~phase_y}};
        busy  <= busy_nx;
        fsel  <= fsel_nx;
        sh_q  <= {4{sh_nx}};
        dsub  <= {1'b0, rem_n} - {2'b00, n};            // kazdy takt; pouzije se jen v taktu vyberu
        lastr <= (cnt == 7'd67);
        n_ge2 <= (n_hi != 8'd0) | (n_md != 8'd0) | (n_lo[7:1] != 7'd0);

        if (ldx_q[0]) begin
            y0r <= ((t0 - ref_ts) << 4);
            rem <= 25'd0; cnt <= 7'd0; phase_y <= 1'b0;
        end else if (ldy_q[0]) begin
            rem <= 25'd0; cnt <= 7'd0; phase_y <= 1'b1;
        end else if (sh_q[0]) begin
            rem <= dsub[25] ? rem_n : dsub[24:0];
            cnt <= cnt + 7'd1;
        end
        // nq po ctvrtinach, kazda ridena vlastni kopii ridicich signalu
        for (i = 0; i < 68; i = i + 1) begin
            if (ldx_q[i / 17])      nq[i] <= dvx[i];
            else if (ldy_q[i / 17]) nq[i] <= dvy[i];
            else if (sh_q[i / 17])  nq[i] <= (i == 0) ? qb : nq[(i == 0) ? 0 : i - 1];
        end
        if (done_sel & ~phase_y) xq <= {nq[38:0], qb};                 // podil sum(x-x0)*256/n
        if (done_sel &  phase_y) begin
            if (for_b) begin
                n_b <= n; xm_b <= {x0, 8'd0} + xq; ym_b <= y0r + {nq[46:0], qb}; ok_b <= 1'b1;
            end else begin
                n_a <= n; xm_a <= {x0, 8'd0} + xq; ym_a <= y0r + {nq[46:0], qb}; ok_a <= 1'b1;
            end
        end
        // nove okno: stare vysledky neplati
        if (beg_a) ok_a <= 1'b0;
        if (beg_b) ok_b <= 1'b0;
    end
endmodule

module win_recip #(
    parameter REGR = 0           // 1 = FW 0x0411: i vnitrni hrany ve dvou segmentech okna dostanou presny cas (regr_acc)
)(
    input  wire         clk,           // clk_p0_100m
    input  wire         rise_s,        // kazda nabezna hrana, SYNCHRONIZOVANA (tdc_chan, pro pocitani)
    input  wire         trig_ack,      // tato hrana spustila presny cas
    input  wire         ts_valid,      // ~8 taktu po trig_ack: ts_ps platny
    input  wire [47:0]  ts_ps,         // presny cas hrany [T_clk/16384], mod 2^48 (172 s)
    input  wire         gate_tick,     // ~okno puls (sdileny)
    input  wire [8:0]   code_i,        // FW 0x0418: kod TDC prave casovane hrany (tdc_chan.code_o)
    input  wire         hold,          // 1 = kalibrace/neplatny TDC: zahod rozpracovane okno
    input  wire         seg_a,         // REGR: segment A okna aktivni (staticke; mimo okamzik prechodu)
    input  wire         seg_b,         // REGR: segment B okna aktivni
    output wire         want,          // okno ceka na presny cas pristi hrany
    output wire         want_seg,      // REGR: i vnitrni hrany maji dostat presny cas (do tdc_chan.want_seg)
    output reg  [25:0]  r_periods,
    output reg  [47:0]  r_dt,          // Δt okna [T_clk/16384]
    output reg          r_dt_alias,    // okno bez hran > ~25 s -> r_dt neplatne
    output reg          res_tgl,
    output reg  [8:0]   r_ce, r_cs,     // FW 0x0418: kod uzaviraci a ZAHAJOVACI hrany okna (INL diagnostika/korekce)
    output wire [23:0]  rg_n_a, rg_n_b,
    output wire [39:0]  rg_xm_a, rg_xm_b,
    output wire [47:0]  rg_ym_a, rg_ym_b,
    output wire         rg_ok_a, rg_ok_b
);
    // 🔴 2026-10-03: okno uzavira PRVNI hrana po gate_tick; ta dostane presny
    // cas (tdc_chan ji zmrazi a dekoduje), hrany uvnitr okna se POCITAJI
    // (rise_c, bez omezeni rychlosti). Uzavírací hrana je zároveň první hranou
    // dalšího okna (gap-free). Hrany, které přijdou za uzavírací hranou během
    // dekódování, patří do NOVÉHO okna (count se nuluje při trig_ack).
    reg        armed  = 1'b0;
    reg        primed = 1'b0;
    reg [25:0] snap   = 26'd0;      // pocet period uzavirane okna
    reg [47:0] ref_ts = 48'd0;
    reg [7:0]  age    = 8'd0;       // gate_tick od posledni uzaviraci hrany
    reg        alias_s = 1'b0;
    // vsechny pulzy se zpozdi o 1 takt (kratke cesty); konzistentne pro count
    // i uzavirani, takze poradi udalosti se nemeni
    reg        rise_q = 1'b0, trig_q = 1'b0, gate_q = 1'b0, hold_q = 1'b0;
    // hrany se pocitaji ze SYNCHRONIZOVANE `rise_s` (tdc_chan: ec z 2. stupne vzorku, +1 takt). `rise_s`
    // uzaviraci hrany prijde 0..2 takty PO jejim `trig_ack`. `pend` ceka na ni: prvni rise_s po trig =
    // uzaviraci hrana (dalsi hrana je >= 3 takty dal, takze zamena nehrozi). Pojistka: po 3 taktech bez
    // rise_s se okno uzavre i tak (nesmi nastat; jinak by okno viselo).
    reg        pend   = 1'b0;
    reg [1:0]  pend_n = 2'd0;
    // citac okna: dolnich 8 b + registrovany prenos do hornich 18 b (kratka cesta);
    // hrany jsou od sebe >= 3 takty, takze preneseny hi je hotovy dřív než se count cte
    reg        cy_q   = 1'b0;
    reg [17:0] count_hi = 18'd0;
    reg [7:0]  count_lo = 8'd0;
    wire [25:0] count = {count_hi, count_lo};
    assign want = armed;

    initial begin
        r_periods = 26'd0; r_dt = 48'd0; r_dt_alias = 1'b0; res_tgl = 1'b0; r_ce = 9'd0; r_cs = 9'd0;
    end

    // ---- REGR: rozlisi UZAVIRACI spusteni (armed) od spusteni vnitrni hrany (segment) ----
    wire trig_c = (REGR != 0) ? (trig_q & armed)  : trig_q;      // uzaviraci hrana okna
    wire trig_s = (REGR != 0) & trig_q & ~armed;                  // vnitrni hrana ve segmentu
    reg        sg_pend = 1'b0, sg_b = 1'b0, ts_seg = 1'b0;
    reg [8:0]  code_cur = 9'd0;     // kod posledni uzaviraci hrany (= zacatek dalsiho okna)
    reg [25:0] sg_x    = 26'd0;

    wire close = (trig_c | pend) & (rise_q | (pend & (pend_n == 2'd3)));  // uzaviraci hrana
    wire inc   = rise_q & ~close;          // bezna hrana uvnitr okna
    wire ts_close = ts_valid & ~((REGR != 0) & ts_seg);           // presny cas UZAVIRACI hrany
    always @(posedge clk) begin
        rise_q <= rise_s;
        trig_q <= trig_ack;
        gate_q <= gate_tick;
        hold_q <= hold;
        cy_q   <= inc & (count_lo == 8'hFF) & ~hold_q;
        if (cy_q) count_hi <= count_hi + 18'd1;
        if (hold_q) begin
            armed    <= 1'b0;
            primed   <= 1'b0;
            pend     <= 1'b0;
            pend_n   <= 2'd0;
            count_lo <= 8'd0;
            count_hi <= 18'd0;
            age      <= 8'd0;
            sg_pend  <= 1'b0;
            ts_seg   <= 1'b0;
        end else begin
            if (gate_q) begin
                armed <= 1'b1;
                if (age != 8'hFF) age <= age + 8'd1;
            end
            if (inc) count_lo <= count_lo + 8'd1;
            if (trig_c & ~rise_q) begin pend <= 1'b1; pend_n <= 2'd0; end
            else if (pend & ~rise_q) pend_n <= pend_n + 2'd1;
            if (trig_c) armed <= 1'b0;          // dalsi trig az po dalsim gate_tick
            if (close) begin
                pend     <= 1'b0;
                pend_n   <= 2'd0;
                snap     <= count;                 // + 1 (uzaviraci hrana) pri vystupu
                count_lo <= 8'd0;
                count_hi <= 18'd0;
                armed    <= 1'b0;
                alias_s  <= (age >= 8'd250);       // >= 250 oken bez hrany
                age      <= 8'd0;
            end
            // vnitrni hrana ve segmentu: poradi hrany se zachyti, az ji napocita rise_q (0..1 takt po trig)
            if (trig_s) begin sg_pend <= 1'b1; sg_b <= seg_b; ts_seg <= 1'b1; end
            if (trig_c) ts_seg <= 1'b0;
            if (sg_pend & rise_q) begin sg_x <= count + 26'd1; sg_pend <= 1'b0; end   // count tuto hranu jeste neobsahuje
            if (ts_close) begin
                if (primed) begin                  // prvni hrana vubec okno neuzavira
                    r_periods  <= snap + 26'd1;
                    r_dt       <= ts_ps - ref_ts;
                    r_dt_alias <= alias_s;
                    res_tgl    <= ~res_tgl;
                    r_ce       <= code_i;
                    r_cs       <= code_cur;
                end
                code_cur <= code_i;
                ref_ts <= ts_ps;
                primed <= 1'b1;
            end
        end
    end

    generate
        if (REGR != 0) begin : rg
            assign want_seg = seg_a | seg_b;
            regr_acc u_rg (.clk(clk), .seg_a(seg_a), .seg_b(seg_b), .ref_ts(ref_ts),
                           .samp_v(ts_valid & ts_seg), .samp_b(sg_b), .samp_x(sg_x), .samp_ts(ts_ps),
                           .n_a(rg_n_a), .n_b(rg_n_b), .xm_a(rg_xm_a), .xm_b(rg_xm_b),
                           .ym_a(rg_ym_a), .ym_b(rg_ym_b), .ok_a(rg_ok_a), .ok_b(rg_ok_b));
        end else begin : rg
            assign want_seg = 1'b0;
            assign rg_n_a = 24'd0;  assign rg_n_b = 24'd0;
            assign rg_xm_a = 40'd0; assign rg_xm_b = 40'd0;
            assign rg_ym_a = 48'd0; assign rg_ym_b = 48'd0;
            assign rg_ok_a = 1'b0;  assign rg_ok_b = 1'b0;
        end
    endgenerate
endmodule


module phase_check (
    input  wire       clk_p0,
    input  wire       clk_p2p5,
    input  wire       clk_p5,
    input  wire       clk_p7p5,
    input  wire       gate_tick,
    output reg  [3:0] present
);
    reg t0 = 1'b0, t1 = 1'b0, t2 = 1'b0, t3 = 1'b0;
    always @(posedge clk_p0)   t0 <= ~t0;
    always @(posedge clk_p2p5) t1 <= ~t1;
    always @(posedge clk_p5)   t2 <= ~t2;
    always @(posedge clk_p7p5) t3 <= ~t3;

    reg [1:0] q0 = 2'b0, q1 = 2'b0, q2 = 2'b0, q3 = 2'b0;
    reg [3:0] seen = 4'b0;

    initial present = 4'b0;

    always @(posedge clk_p0) begin
        q0 <= {q0[0], t0};
        q1 <= {q1[0], t1};
        q2 <= {q2[0], t2};
        q3 <= {q3[0], t3};

        if (gate_tick) begin
            present <= seen;
            seen    <= 4'b0;
        end else begin
            if (q0[1] ^ q0[0]) seen[0] <= 1'b1;
            if (q1[1] ^ q1[0]) seen[1] <= 1'b1;
            if (q2[1] ^ q2[0]) seen[2] <= 1'b1;
            if (q3[1] ^ q3[0]) seen[3] <= 1'b1;
        end
    end
endmodule
