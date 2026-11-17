`timescale 1ps/1ps
// REFERENCE: spi_app pred prepisem multiplexeru TX bajtu (2026-10-07), jen pro tb_tx_equiv
module spi_app_old (
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
    output wire [7:0]  hist_k,               // vypis histogramu: adresa kodu (CAL pozadavek)
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
    localparam [15:0] FW_VERSION      = 16'h040F;  // bump při KAŽDÉ změně bitstreamu
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
    localparam [15:0] CAPS            = 16'h0023;

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

    // window stream (2 poslední uzavřená okna; gap-free z win_recip)
    reg [31:0] win_seq  = 32'd1;
    reg [31:0] w0_seq   = 32'd0, w1_seq   = 32'd0;
    reg [31:0] w0_edges = 32'd0, w1_edges = 32'd0;
    reg [63:0] w0_dt    = 64'd0, w1_dt    = 64'd0;
    reg [1:0]  win_cnt  = 2'd0;

    // konfigurace (SET_CONFIG)
    reg        cal_mode_r = 1'b0;
    // CAL pozadavek s payloadem [12]=1 -> rezim vypisu histogramu, [14] = kod
    reg        hist_mode = 1'b0;
    reg [7:0]  hist_k_r  = 8'd0;
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

    // ---- obsah TX bajtu podle indexu (driv 685 FF `tx_b`, ted kombinacni mux) ----
    reg [7:0] tb;
    always @* begin
        tb = 8'd0;
        case (idx[6:0])
            7'd0:  tb = MAGIC;
            7'd1:  tb = VERSION;
            7'd2:  tb = is_cal ? TYPE_CAL : TYPE_DATA;
            7'd3:  tb = flags_byte;
            // SEQUENCE je soucast snimku (behem S_TX_WR se nemeni) -> patri ke
            // stejnemu mereni jako data; zvlastni kopie (driv b_seq) zachycena
            // v taktu startu se mohla rozejit s h_*, kdyz nove mereni prislo
            // prave v tom taktu (nalezeno tb_link 2026-10-04).
            7'd4:  tb = seq[7:0];
            7'd5:  tb = seq[15:8];
            7'd6:  tb = seq[23:16];
            7'd7:  tb = seq[31:24];
            7'd8:  tb = PAYLOAD_LEN[7:0];
            7'd9:  tb = PAYLOAD_LEN[15:8];
            default: begin
                if (is_cal) begin
                    // ---- CAL report 0xA0 ----
                    if (idx[6:0] >= 7'd12 && idx[6:0] <= 7'd35) tb = meas_cal_diag[8*(idx[6:0]-7'd12) +: 8];
                    else case (idx[6:0])
                        7'd36: tb = meas_tdc_status;
                        7'd37: tb = {7'd0, cal_mode_r};
                        // vypis histogramu: [38]=1, [39]=kod, [40..42]=A, [43..45]=B
                        7'd38: tb = hist_mode ? 8'd1 : 8'd0;
                        7'd39: tb = hist_mode ? hist_k_r : 8'd0;
                        7'd40: tb = hist_mode ? meas_hist_a[7:0]   : 8'd0;
                        7'd41: tb = hist_mode ? meas_hist_a[15:8]  : 8'd0;
                        7'd42: tb = hist_mode ? meas_hist_a[23:16] : 8'd0;
                        7'd43: tb = hist_mode ? meas_hist_b[7:0]   : 8'd0;
                        7'd44: tb = hist_mode ? meas_hist_b[15:8]  : 8'd0;
                        7'd45: tb = hist_mode ? meas_hist_b[23:16] : 8'd0;
                        // B1a: nejvyssi set tap (stabilni diag, vzdy ve CAL reportu)
                        7'd46: tb = meas_maxtap_a[7:0];
                        7'd47: tb = meas_maxtap_a[15:8];
                        7'd48: tb = meas_maxtap_b[7:0];
                        7'd49: tb = meas_maxtap_b[15:8];
                        default: tb = 8'd0;
                    endcase
                end else begin
                    // ---- DATA 0x80 (offsety 12..59 = 1:1 s v1) ----
                    if      (idx[6:0] >= 7'd12  && idx[6:0] <= 7'd19)  tb = h_freq  [8*(idx[6:0]-7'd12)  +: 8];
                    else if (idx[6:0] >= 7'd20  && idx[6:0] <= 7'd27)  tb = h_edge  [8*(idx[6:0]-7'd20)  +: 8];
                    else if (idx[6:0] >= 7'd28  && idx[6:0] <= 7'd35)  tb = h_gate  [8*(idx[6:0]-7'd28)  +: 8];
                    else if (idx[6:0] >= 7'd36  && idx[6:0] <= 7'd43)  tb = h_ts    [8*(idx[6:0]-7'd36)  +: 8];
                    else if (idx[6:0] >= 7'd46  && idx[6:0] <= 7'd49)  tb = err_word[8*(idx[6:0]-7'd46)  +: 8];
                    else if (idx[6:0] >= 7'd52  && idx[6:0] <= 7'd59)  tb = h_freq16[8*(idx[6:0]-7'd52)  +: 8];
                    else if (idx[6:0] >= 7'd68  && idx[6:0] <= 7'd71)  tb = w0_seq  [8*(idx[6:0]-7'd68)  +: 8];
                    else if (idx[6:0] >= 7'd72  && idx[6:0] <= 7'd75)  tb = w0_edges[8*(idx[6:0]-7'd72)  +: 8];
                    else if (idx[6:0] >= 7'd76  && idx[6:0] <= 7'd83)  tb = w0_dt   [8*(idx[6:0]-7'd76)  +: 8];
                    else if (idx[6:0] >= 7'd84  && idx[6:0] <= 7'd87)  tb = w1_seq  [8*(idx[6:0]-7'd84)  +: 8];
                    else if (idx[6:0] >= 7'd88  && idx[6:0] <= 7'd91)  tb = w1_edges[8*(idx[6:0]-7'd88)  +: 8];
                    else if (idx[6:0] >= 7'd92  && idx[6:0] <= 7'd99)  tb = w1_dt   [8*(idx[6:0]-7'd92)  +: 8];
                    else if (idx[6:0] >= 7'd101 && idx[6:0] <= 7'd106) tb = h_dt_b  [8*(idx[6:0]-7'd101) +: 8];
                    else if (idx[6:0] >= 7'd108 && idx[6:0] <= 7'd111) tb = h_edge_b[8*(idx[6:0]-7'd108) +: 8];
                    else if (idx[6:0] >= 7'd118 && idx[6:0] <= 7'd123) tb = h_dt_a  [8*(idx[6:0]-7'd118) +: 8];
                    else case (idx[6:0])
                        7'd44:  tb = h_ch;
                        7'd45:  tb = {6'd0, data_fresh, data_valid};
                        7'd50:  tb = h_phase;     // {fine_seen[3:0], present[3:0]}
                        7'd51:  tb = h_status2;   // pin27 status
                        // ---- v2 rozšíření ----
                        7'd60:  tb = FW_VERSION[7:0];
                        7'd61:  tb = FW_VERSION[15:8];
                        7'd62:  tb = CAPS[7:0];
                        7'd63:  tb = CAPS[15:8];
                        7'd64:  tb = 8'h01;              // clk_status: bit0=10MHz OK
                        7'd65:  tb = {6'd0, win_cnt};
                        7'd66:  tb = d_rx0;
                        7'd67:  tb = d_rx1;
                        // TDC (caps bit5): stav ZIVE (bez signalu by jinak nebylo videt kalibraci)
                        7'd100: tb = meas_tdc_status;
                        7'd107: tb = d_rx2;
                        7'd112: tb = d_ccl;
                        7'd113: tb = d_cch;
                        7'd114: tb = d_rcl;
                        7'd115: tb = d_rch;
                        7'd116: tb = dbg_mosi_cnt[7:0];
                        7'd117: tb = dbg_mosi_cnt[15:8];
                        7'd124: tb = dbg_sck_cnt[7:0];
                        7'd125: tb = dbg_sck_cnt[15:8];
                        default: tb = 8'd0;
                    endcase
                end
            end
        endcase
    end

    always @(posedge clk) begin
        tx_we_r <= 1'b0;
        if (cool != 3'd0) cool <= cool - 3'd1;

        // ---- nové měření: SEQUENCE++, FRESH/VALID, latch dat, window stream ----
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
            // window stream: posuň historii (okna na sebe navazují hranou)
            w1_seq   <= w0_seq;   w1_edges <= w0_edges;  w1_dt <= w0_dt;
            w0_seq   <= win_seq;  w0_edges <= meas_periods;
            w0_dt    <= meas_gate_ns;
            win_seq  <= win_seq + 32'd1;
            if (win_cnt != 2'd2) win_cnt <= win_cnt + 2'd1;
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
                            // [12]==1: vypis hist[k] (k = [14]); jinak souhrn
                            hist_mode <= (rx_p0 == 8'd1);
                            if (rx_p0 == 8'd1) hist_k_r <= rx_p2;
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
