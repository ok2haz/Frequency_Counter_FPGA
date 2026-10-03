// ============================================================
// File: spi_app.v
// SPI aplikační vrstva (doména clk_ref_10m). Protokol v2 dle
// FPGA_PROTOCOL_V2_NAVRH.md (STM32H757 repo): rámec 128 B, VER=0x02.
//  - skládá 128B TX rámec (TYPE=0x80 DATA / 0xA0 CAL report)
//  - CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) přes byte 0..125
//  - validuje RX rámec (MAGIC, CRC), zpracuje TYPE
//  - SEQUENCE (++ na každé nové měření/okno) a FLAGS (VALID/FRESH/...)
//  - window stream: 2 poslední uzavřená okna (gap-free reciproké čítání,
//    okno N+1 začíná hranou, kterou skončilo okno N -> STM může Σedges/Σdt)
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
//  60..61 fw_version(u16) 62..63 caps(u16) 64 clk_status 65 win_count
//  66..67 pad 68..83 window[0] 84..99 window[1]
//  window rec: +0 win_seq(u32) +4 edges(u32) +8 dt_ns(u64)
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
//  FW_VERSION 0x0400, CAPS 0x0023 (window stream, SET_CONFIG, dt_ps).
//  SET_CONFIG 0x02 = 1 spusti kalibraci TDC (ring oscilator, ~0,3 s), 0 = nic;
//  kalibrace probehne i sama po zapnuti. Behem ni jsou mereni zablokovana.
//
// Flat mapování (shodné s PHY): bit[1023]=byte0[7], MSB-first, byte0 první.
// ============================================================

module spi_app (
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
    input  wire [15:0] dbg_mosi_cnt,         // diag: hrany MOSI v poslednim ramci (tx_b[116,117])
    input  wire [15:0] dbg_sck_cnt,          // diag: nabezne hrany SCK v poslednim ramci (tx_b[124,125])

    input  wire [1023:0] rx_frame_flat,
    input  wire          rx_valid,           // pulz po CS↑ (synchronizováno)

    output wire [1023:0] tx_frame_flat,
    output wire          tx_valid,           // 1 = rámec kompletní (payload i CRC);
                                             // PHY při 0 drží poslední celý rámec
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
    localparam [15:0] FW_VERSION      = 16'h0409;  // bump při KAŽDÉ změně bitstreamu
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

    // ---- TX bajty + RX rozbalení ----
    reg  [7:0] tx_b [0:127];
    wire [7:0] rx_b [0:127];

    genvar gi;
    generate
        for (gi = 0; gi < 128; gi = gi + 1) begin : g_flat
            assign tx_frame_flat[1023 - 8*gi -: 8] = tx_b[gi];
            assign rx_b[gi] = rx_frame_flat[1023 - 8*gi -: 8];
        end
    endgenerate

    // ---- stav / příznaky ----
    reg [31:0] seq           = 32'd1;
    reg [31:0] last_sent_seq = 32'd0;
    reg [31:0] b_seq         = 32'd1;   // SEQUENCE použitá v právě skládaném rámci
    reg        data_valid    = 1'b1;
    reg        data_fresh    = 1'b1;
    reg        ack_ok        = 1'b0;
    reg        rx_crc_error  = 1'b0;

    // držené (latchované) měření
    reg [63:0] h_freq  = 64'd12345678901234; // power-on dummy pro bring-up
    reg [63:0] h_edge  = 64'd0;
    reg [63:0] h_gate  = 64'd250000000;      // skutečné okno [ns] (default 0,25 s)
    reg [63:0] h_freq16 = 64'd0;             // pin27 /16 kmitočet *100000
    reg [7:0]  h_phase   = 8'd0;             // {fine_seen[3:0], present[3:0]}
    reg [7:0]  h_status2 = 8'd0;
    reg [63:0] h_ts    = 64'd0;
    reg [31:0] h_err   = 32'd0;
    reg [7:0]  h_ch    = 8'd0;
    reg [47:0] h_dt_a = 48'd0;               // okno CH_A [ps]
    reg [47:0] h_dt_b = 48'd0;               // okno CH_B [ps]
    reg [31:0] h_edge_b = 32'd0;
    reg [7:0]  h_tdc  = 8'd0;

    // window stream (2 poslední uzavřená okna; gap-free z win_recip)
    reg [31:0] win_seq  = 32'd1;
    reg [31:0] w0_seq   = 32'd0, w1_seq   = 32'd0;
    reg [31:0] w0_edges = 32'd0, w1_edges = 32'd0;
    reg [63:0] w0_dt    = 64'd0, w1_dt    = 64'd0;
    reg [1:0]  win_cnt  = 2'd0;

    // konfigurace (SET_CONFIG)
    reg        cal_mode_r = 1'b0;
    // DIAGNOSTIKA RX (2026-10-03): co FPGA skutecne prijala od STM v poslednim ramci
    // (zachyceno v S_RX_PROC) -> tx_b[66,67,107,112..115] = rx[0], rx[1], rx[2],
    // CRC spoctene FPGA (lo,hi), CRC prijate (lo,hi). Rozhodne, jestli jsou spatne
    // bity MOSI (posun/ruseni) nebo CRC.
    reg [7:0]  d_rx0 = 8'd0, d_rx1 = 8'd0, d_rx2 = 8'd0;
    reg [7:0]  d_ccl = 8'd0, d_cch = 8'd0, d_rcl = 8'd0, d_rch = 8'd0;
    reg [1:0]  base_win_r = 2'd1;            // default 250 ms
    assign cal_mode = cal_mode_r;
    assign base_win = base_win_r;

    // žádost o CAL report (one-shot: příští TX rámec = 0xA0)
    reg        cal_req = 1'b0;

    // FSM
    localparam [2:0] S_IDLE    = 3'd0;
    localparam [2:0] S_TX_ARM  = 3'd1;   // NOVY: viz komentar u S_IDLE/S_TX_ARM nize
    localparam [2:0] S_TX_CRC  = 3'd2;
    localparam [2:0] S_TX_FIN  = 3'd3;
    localparam [2:0] S_RX_CRC  = 3'd4;
    localparam [2:0] S_RX_PROC = 3'd5;

    reg [2:0]  state    = S_IDLE;
    reg [6:0]  crc_idx  = 7'd0;
    reg [15:0] crc_acc  = 16'hFFFF;
    reg        tx_dirty = 1'b1;   // postav první rámec po resetu
    reg        meas_dirty = 1'b0;
    // audit V2: rx_valid je 1-taktový a FSM může být v S_TX_*/S_RX_* (build
    // + CRC ~130 taktů po new_meas) -> pulz by se ztratil (zahozený povel).
    reg        rx_pend  = 1'b0;
    // audit V3: platnost TX rámce - PHY při frame_ok=0 (probíhá přestavba)
    // drží poslední KOMPLETNÍ rámec, jinak by CS↓ uprostřed přestavby
    // odvysílal nový payload se starým CRC.
    reg        frame_ok = 1'b0;
    assign tx_valid = frame_ok;

    // error_flags do rámce: latchované chyby měření + ŽIVÝ signal_lost (bit1),
    // ten musí jít do rámce i bez new_meas. bit0=měření, bit1=signal_lost, bit2=Δt ovf.
    wire [31:0] err_word = h_err | {30'd0, signal_lost, 1'b0};

    wire [7:0] flags_byte = {(|err_word), ack_ok, rx_crc_error,
                             1'b0 /*busy*/, 1'b0 /*fifo_ovf*/,
                             ~data_valid /*fifo_empty*/, data_fresh, data_valid};

    assign dbg_status = flags_byte;

    integer k;

    always @(posedge clk) begin
        // ---- nové měření: SEQUENCE++, FRESH/VALID, latch dat, window stream ----
        if (new_meas) begin
            seq        <= seq + 32'd1;
            data_valid <= 1'b1;
            data_fresh <= 1'b1;
            h_freq     <= meas_freq_x100000;     // hotový výpočet z recip_calc
            h_edge     <= {32'd0, meas_periods}; // edge_count = počet period v okně
            h_gate     <= meas_gate_ns;          // skutečné Δt okna [ns]
            h_freq16   <= meas_freq16_x100000;   // pin27 /16
            h_phase    <= meas_phase_status;
            h_status2  <= meas_status2;
            h_ts       <= meas_timestamp;
            h_err      <= meas_error_flags;
            h_ch       <= meas_channel;
            h_dt_a     <= meas_dt_a_ps;
            h_dt_b     <= meas_dt_b_ps;
            h_edge_b   <= meas_periods_b;
            h_tdc      <= meas_tdc_status;
            // window stream: posuň historii (okna na sebe navazují hranou)
            w1_seq   <= w0_seq;   w1_edges <= w0_edges;  w1_dt <= w0_dt;
            w0_seq   <= win_seq;  w0_edges <= meas_periods;
            w0_dt    <= meas_gate_ns;
            win_seq  <= win_seq + 32'd1;
            if (win_cnt != 2'd2) win_cnt <= win_cnt + 2'd1;
            meas_dirty <= 1'b1;
        end else if (signal_lost) begin
            data_valid <= 1'b0;          // ztráta signálu -> data nejsou platná
        end

        // sběr rx_valid i mimo S_IDLE (konzumace níže pulz zároveň smaže)
        if (rx_valid) rx_pend <= 1'b1;

        case (state)
            S_IDLE: begin
                if (rx_pend || rx_valid) begin
                    // zpracuj příchozí rámec (priorita)
                    rx_pend <= 1'b0;
                    crc_acc <= 16'hFFFF;
                    crc_idx <= 7'd0;
                    state   <= S_RX_CRC;
                end else if (meas_dirty || tx_dirty) begin
                    // 🔴 CDC (PHY ted bezi na clk_p0_100m, viz top.v): frame_ok
                    // padne TADY, O CELY JEDEN TAKT DRIV nez se tx_b[] zacne
                    // prepisovat (zapis az v S_TX_ARM nize). Dava to synchronizeru
                    // tx_valid v top.v (clk_p0_100m domena) celych 100 ns (10 taktu
                    // @100MHz) rezervy na ustaleni PRED tím, než se data skutečně
                    // zmeni -- bez tohohle mezikroku by mohl PHY behem ~20-30ns
                    // okna (zpozdeni 2-3-stupnoveho synchronizeru) zachytit torn
                    // ramec (nova data + stara CRC), protoze tx_frame_flat je
                    // kombinacni vodic (nesynchronizovany -- novy 1024b registr by
                    // se nevesel, registry FPGA jsou na 79 %). Viz spi_slave_phy.v.
                    frame_ok <= 1'b0;
                    state    <= S_TX_ARM;
                end
            end

            S_TX_ARM: begin
                    // sestav hlavičku + payload
                    tx_b[0] <= MAGIC;
                    tx_b[1] <= VERSION;
                    tx_b[2] <= cal_req ? TYPE_CAL : TYPE_DATA;
                    tx_b[3] <= flags_byte;

                    tx_b[4] <= seq[7:0];
                    tx_b[5] <= seq[15:8];
                    tx_b[6] <= seq[23:16];
                    tx_b[7] <= seq[31:24];
                    b_seq   <= seq;

                    tx_b[8]  <= PAYLOAD_LEN[7:0];
                    tx_b[9]  <= PAYLOAD_LEN[15:8];
                    tx_b[10] <= 8'd0;
                    tx_b[11] <= 8'd0;

                    if (cal_req) begin
                        // ---- CAL report 0xA0 ----
                        // hist záměrně ŽIVÝ (per-gate statistika, atomický
                        // 1-taktový update v top) - koherenci s oknem nepotřebuje
                        for (k = 0; k < 24; k = k + 1)
                            tx_b[12 + k] <= meas_cal_diag[8*k +: 8];
                        tx_b[36] <= meas_tdc_status;
                        tx_b[37] <= {7'd0, cal_mode_r};
                        for (k = 38; k < 126; k = k + 1)
                            tx_b[k] <= 8'd0;
                        cal_req <= 1'b0;
                    end else begin
                        // ---- DATA 0x80 (offsety 12..59 = 1:1 s v1) ----
                        for (k = 0; k < 8; k = k + 1) begin
                            tx_b[12 + k] <= h_freq[8*k +: 8];
                            tx_b[20 + k] <= h_edge[8*k +: 8];
                            tx_b[28 + k] <= h_gate[8*k +: 8];
                            tx_b[36 + k] <= h_ts  [8*k +: 8];
                        end
                        tx_b[44] <= h_ch;
                        tx_b[45] <= {6'd0, data_fresh, data_valid};
                        for (k = 0; k < 4; k = k + 1)
                            tx_b[46 + k] <= err_word[8*k +: 8];
                        tx_b[50] <= h_phase;     // {fine_seen[3:0], present[3:0]}
                        tx_b[51] <= h_status2;   // pin27 status
                        for (k = 0; k < 8; k = k + 1)
                            tx_b[52 + k] <= h_freq16[8*k +: 8];
                        // ---- v2 rozšíření ----
                        tx_b[60] <= FW_VERSION[7:0];
                        tx_b[61] <= FW_VERSION[15:8];
                        tx_b[62] <= CAPS[7:0];
                        tx_b[63] <= CAPS[15:8];
                        tx_b[64] <= 8'h01;              // clk_status: bit0=10MHz OK
                        tx_b[65] <= {6'd0, win_cnt};
                        tx_b[66] <= d_rx0;
                        tx_b[67] <= d_rx1;
                        for (k = 0; k < 4; k = k + 1) begin
                            tx_b[68 + k] <= w0_seq  [8*k +: 8];
                            tx_b[72 + k] <= w0_edges[8*k +: 8];
                            tx_b[84 + k] <= w1_seq  [8*k +: 8];
                            tx_b[88 + k] <= w1_edges[8*k +: 8];
                        end
                        for (k = 0; k < 8; k = k + 1) begin
                            tx_b[76 + k] <= w0_dt[8*k +: 8];
                            tx_b[92 + k] <= w1_dt[8*k +: 8];
                        end
                        // TDC (caps bit5): stav, okno CH_B, hrany CH_B, okno CH_A [ps]
                        tx_b[100] <= meas_tdc_status;   // ZIVE (ne latch z new_meas): bez signalu by jinak nebylo videt, jak dopadla kalibrace
                        for (k = 0; k < 6; k = k + 1)
                            tx_b[101 + k] <= h_dt_b[8*k +: 8];
                        tx_b[107] <= d_rx2;
                        for (k = 0; k < 4; k = k + 1)
                            tx_b[108 + k] <= h_edge_b[8*k +: 8];
                        tx_b[112] <= d_ccl;  tx_b[113] <= d_cch;
                        tx_b[114] <= d_rcl;  tx_b[115] <= d_rch;
                        tx_b[116] <= dbg_mosi_cnt[7:0]; tx_b[117] <= dbg_mosi_cnt[15:8];
                        for (k = 0; k < 6; k = k + 1)
                            tx_b[118 + k] <= h_dt_a[8*k +: 8];
                        tx_b[124] <= dbg_sck_cnt[7:0];
                        tx_b[125] <= dbg_sck_cnt[15:8];
                    end

                    crc_acc    <= 16'hFFFF;
                    crc_idx    <= 7'd0;
                    meas_dirty <= 1'b0;
                    tx_dirty   <= 1'b0;
                    // frame_ok uz je 0 (nastaveno v S_IDLE o takt drive -- CDC
                    // rezerva, viz komentar tam); tady se neopakuje.
                    state      <= S_TX_CRC;
            end

            S_TX_CRC: begin
                crc_acc <= crc16_step(crc_acc, tx_b[crc_idx]);
                if (crc_idx == 7'd125) state <= S_TX_FIN;
                else                   crc_idx <= crc_idx + 7'd1;
            end

            S_TX_FIN: begin
                tx_b[126]     <= crc_acc[7:0];
                tx_b[127]     <= crc_acc[15:8];
                last_sent_seq <= b_seq;
                frame_ok      <= 1'b1;    // payload + CRC konzistentní
                state         <= S_IDLE;
            end

            S_RX_CRC: begin
                crc_acc <= crc16_step(crc_acc, rx_b[crc_idx]);
                if (crc_idx == 7'd125) state <= S_RX_PROC;
                else                   crc_idx <= crc_idx + 7'd1;
            end

            S_RX_PROC: begin
                d_rx0 <= rx_b[0];  d_rx1 <= rx_b[1];  d_rx2 <= rx_b[2];
                d_ccl <= crc_acc[7:0]; d_cch <= crc_acc[15:8];
                d_rcl <= rx_b[126];    d_rch <= rx_b[127];
                if (rx_b[0] == MAGIC && crc_acc == {rx_b[127], rx_b[126]}) begin
                    rx_crc_error <= 1'b0;
                    case (rx_b[2])
                        TYPE_ACK: begin
                            ack_ok <= 1'b1;
                            // SEQUENCE v ACK (LE) == naposledy odeslaná -> shoď FRESH
                            if ({rx_b[7], rx_b[6], rx_b[5], rx_b[4]} == last_sent_seq)
                                data_fresh <= 1'b0;
                        end
                        TYPE_START: ack_ok <= 1'b0; // continuous měření běží i tak
                        TYPE_STOP:  ack_ok <= 1'b0;
                        TYPE_SET_CONFIG: begin
                            ack_ok <= 1'b0;
                            case (rx_b[12])
                                8'h01: base_win_r <= (rx_b[13] <= 8'd2)
                                                     ? rx_b[13][1:0] : 2'd1;
                                8'h02: cal_mode_r <= rx_b[13][0];
                                default: ;      // neznámé config_id = ignorovat
                            endcase
                        end
                        TYPE_CAL: begin          // žádost o CAL report
                            ack_ok  <= 1'b0;
                            cal_req <= 1'b1;
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


module win_recip (
    input  wire         clk,           // clk_p0_100m
    input  wire         rise_c,        // kazda nabezna hrana (hrube, tdc_chan)
    input  wire         trig_ack,      // tato hrana spustila presny cas
    input  wire         ts_valid,      // 7 taktu po trig_ack: ts_ps platny
    input  wire [47:0]  ts_ps,         // presny cas hrany [T_clk/16384], mod 2^48 (172 s)
    input  wire         gate_tick,     // ~okno puls (sdileny)
    input  wire         hold,          // 1 = kalibrace/neplatny TDC: zahod rozpracovane okno
    output wire         want,          // okno ceka na presny cas pristi hrany
    output reg  [25:0]  r_periods,
    output reg  [47:0]  r_dt,          // Δt okna [T_clk/16384]
    output reg          r_dt_alias,    // okno bez hran > ~25 s -> r_dt neplatne
    output reg          res_tgl
);
    // 🔴 2026-10-03: okno uzavira PRVNI hrana po gate_tick; ta dostane presny
    // cas (tdc_chan ji zmrazi a dekoduje), hrany uvnitr okna se POCITAJI
    // (rise_c, bez omezeni rychlosti). Uzavírací hrana je zároveň první hranou
    // dalšího okna (gap-free). Hrany, které přijdou za uzavírací hranou během
    // 7 taktů dekódování, patří do NOVÉHO okna (count se nuluje při trig_ack).
    reg        armed  = 1'b0;
    reg        primed = 1'b0;
    reg [25:0] snap   = 26'd0;      // pocet period uzavirane okna
    reg [47:0] ref_ts = 48'd0;
    reg [7:0]  age    = 8'd0;       // gate_tick od posledni uzaviraci hrany
    reg        alias_s = 1'b0;
    // vsechny pulzy se zpozdi o 1 takt (kratke cesty); konzistentne pro count
    // i uzavirani, takze poradi udalosti se nemeni
    reg        rise_q = 1'b0, trig_q = 1'b0, gate_q = 1'b0, hold_q = 1'b0;
    // citac okna: dolnich 8 b + registrovany prenos do hornich 18 b (kratka cesta);
    // hrany jsou od sebe >= 3 takty, takze preneseny hi je hotovy dřív než se count cte
    reg        cy_q   = 1'b0;
    reg [17:0] count_hi = 18'd0;
    reg [7:0]  count_lo = 8'd0;
    wire [25:0] count = {count_hi, count_lo};
    assign want = armed;

    initial begin
        r_periods = 26'd0; r_dt = 48'd0; r_dt_alias = 1'b0; res_tgl = 1'b0;
    end

    wire inc   = rise_q & ~trig_q;         // bezna hrana uvnitr okna
    always @(posedge clk) begin
        rise_q <= rise_c;
        trig_q <= trig_ack;
        gate_q <= gate_tick;
        hold_q <= hold;
        cy_q   <= inc & (count_lo == 8'hFF) & ~hold_q;
        if (cy_q) count_hi <= count_hi + 18'd1;
        if (hold_q) begin
            armed    <= 1'b0;
            primed   <= 1'b0;
            count_lo <= 8'd0;
            count_hi <= 18'd0;
            age      <= 8'd0;
        end else begin
            if (gate_q) begin
                armed <= 1'b1;
                if (age != 8'hFF) age <= age + 8'd1;
            end
            if (inc) count_lo <= count_lo + 8'd1;
            if (rise_q & trig_q) begin
                snap     <= count;                 // + 1 (uzaviraci hrana) pri vystupu
                count_lo <= 8'd0;
                count_hi <= 18'd0;
                armed    <= 1'b0;
                alias_s  <= (age >= 8'd250);       // >= 250 oken bez hrany
                age      <= 8'd0;
            end
            if (ts_valid) begin
                if (primed) begin                  // prvni hrana vubec okno neuzavira
                    r_periods  <= snap + 26'd1;
                    r_dt       <= ts_ps - ref_ts;
                    r_dt_alias <= alias_s;
                    res_tgl    <= ~res_tgl;
                end
                ref_ts <= ts_ps;
                primed <= 1'b1;
            end
        end
    end
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
