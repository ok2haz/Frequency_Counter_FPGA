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
//  Rozšíření v rezervě (caps bit4): 100 {fine_last[3:2],fine_first[1:0]}
//  101..107 S1=Σts_rel(u56) 108..117 S2=ΣS1(u80) 118..125 rezerva=0
//  [Λ/Ω regrese: body i=0..N, N=edges okna, t_0=0: Σt=S1, Σi·t=N·S1−S2]
//
// CAL payload 0xA0 (abs offset):
//  12..23 hist0..3 (4x u24, hrany dle fine kódu za poslední okno)
//  24 fine kódy | 25 flags{bit0=cal_mode} | 26..32 S1 | 33..42 S2
//  43..46 edges(u32) | 47..125 rezerva=0
//
// Flat mapování (shodné s PHY): bit[1023]=byte0[7], MSB-first, byte0 první.
// ============================================================

module spi_app (
    input  wire        clk,                  // clk_ref_10m

    input  wire [63:0] meas_freq_x100000,    // hotový kmitočet * 100000 (z recip_calc)
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
    input  wire [3:0]  meas_fine_fl,         // {fine_last, fine_first} okna
    input  wire [95:0] meas_hist,            // 4x24b histogram fine kódů (za okno)
    input  wire [55:0] meas_s1,              // Λ: Σ ts_rel přes hrany okna
    input  wire [79:0] meas_s2,              // Λ: Σ S1 (dvojitá akumulace)

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
    localparam [15:0] FW_VERSION      = 16'h0201;  // bump při KAŽDÉ změně bitstreamu
    // caps: bit0=window stream, bit1=SET_CONFIG, bit4=Λ/fine rozšíření v rezervě
    localparam [15:0] CAPS            = 16'h0013;

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
    reg [3:0]  h_fine_fl = 4'd0;             // {fine_last, fine_first}
    // Λ sumy latchované PŘI new_meas (audit V1): s1_lat/s2_lat v top se
    // přepisují už při res_valid4 (~8,7 µs před valid4) -> rámec postavený
    // v tom okně by míchal S1/S2 okna N+1 s edge_count/gate okna N.
    reg [55:0] h_s1   = 56'd0;
    reg [79:0] h_s2   = 80'd0;

    // window stream (2 poslední uzavřená okna; gap-free z win_recip)
    reg [31:0] win_seq  = 32'd1;
    reg [31:0] w0_seq   = 32'd0, w1_seq   = 32'd0;
    reg [31:0] w0_edges = 32'd0, w1_edges = 32'd0;
    reg [63:0] w0_dt    = 64'd0, w1_dt    = 64'd0;
    reg [1:0]  win_cnt  = 2'd0;

    // konfigurace (SET_CONFIG)
    reg        cal_mode_r = 1'b0;
    reg [1:0]  base_win_r = 2'd1;            // default 250 ms
    assign cal_mode = cal_mode_r;
    assign base_win = base_win_r;

    // žádost o CAL report (one-shot: příští TX rámec = 0xA0)
    reg        cal_req = 1'b0;

    // FSM
    localparam [2:0] S_IDLE    = 3'd0;
    localparam [2:0] S_TX_CRC  = 3'd1;
    localparam [2:0] S_TX_FIN  = 3'd2;
    localparam [2:0] S_RX_CRC  = 3'd3;
    localparam [2:0] S_RX_PROC = 3'd4;

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
            h_fine_fl  <= meas_fine_fl;
            h_s1       <= meas_s1;
            h_s2       <= meas_s2;
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
                        for (k = 0; k < 12; k = k + 1)
                            tx_b[12 + k] <= meas_hist[8*k +: 8];
                        tx_b[24] <= {4'd0, h_fine_fl};
                        tx_b[25] <= {7'd0, cal_mode_r};
                        for (k = 0; k < 7; k = k + 1)
                            tx_b[26 + k] <= h_s1[8*k +: 8];
                        for (k = 0; k < 10; k = k + 1)
                            tx_b[33 + k] <= h_s2[8*k +: 8];
                        for (k = 0; k < 4; k = k + 1)
                            tx_b[43 + k] <= w0_edges[8*k +: 8];
                        for (k = 47; k < 126; k = k + 1)
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
                        tx_b[66] <= 8'd0;
                        tx_b[67] <= 8'd0;
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
                        // rezerva (caps bit4): Λ/Ω + fine kódy posledního okna
                        tx_b[100] <= {4'd0, h_fine_fl};
                        for (k = 0; k < 7; k = k + 1)
                            tx_b[101 + k] <= h_s1[8*k +: 8];
                        for (k = 0; k < 10; k = k + 1)
                            tx_b[108 + k] <= h_s2[8*k +: 8];
                        for (k = 118; k < 126; k = k + 1)
                            tx_b[k] <= 8'd0;
                    end

                    crc_acc    <= 16'hFFFF;
                    crc_idx    <= 7'd0;
                    meas_dirty <= 1'b0;
                    tx_dirty   <= 1'b0;
                    frame_ok   <= 1'b0;   // rámec neúplný až do S_TX_FIN
                    state      <= S_TX_CRC;
                end
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
// divu_seq: generická bezznaménková sekvenční dělička (restoring).
//   quotient = dividend / divisor. 1 iterace/takt -> NW taktů.
//   start (1 takt) zachytí operandy; done pulzne s platným quotient.
// ------------------------------------------------------------
module divu_seq #(
    parameter NW = 72,          // šířka dividendu i kvocientu
    parameter DW = 34           // šířka dělitele
) (
    input  wire          clk,
    input  wire          start,
    input  wire [NW-1:0] dividend,
    input  wire [DW-1:0] divisor,
    output reg  [NW-1:0] quotient,
    output reg           busy,
    output reg           done
);
    reg  [DW:0]   rem;          // částečný zbytek (DW+1 bit)
    reg  [NW-1:0] quo;          // posuvný registr: nahoře zbytek, dole kvocient
    reg  [7:0]    cnt;          // čítač iterací (NW <= 255)

    wire [DW:0] rem_sh = {rem[DW-1:0], quo[NW-1]};
    wire        ge     = (rem_sh >= {1'b0, divisor});
    wire [DW:0] rem_nx = ge ? (rem_sh - {1'b0, divisor}) : rem_sh;

    initial begin
        quotient = {NW{1'b0}};
        busy     = 1'b0;
        done     = 1'b0;
        rem      = {(DW+1){1'b0}};
        quo      = {NW{1'b0}};
        cnt      = 8'd0;
    end

    always @(posedge clk) begin
        done <= 1'b0;

        if (start && !busy) begin
            rem  <= {(DW+1){1'b0}};
            quo  <= dividend;
            cnt  <= NW[7:0];
            busy <= 1'b1;
        end else if (busy) begin
            rem <= rem_nx;
            quo <= {quo[NW-2:0], ge};
            cnt <= cnt - 8'd1;
            if (cnt == 8'd1) begin
                busy     <= 1'b0;
                done     <= 1'b1;
                quotient <= {quo[NW-2:0], ge};
            end
        end
    end
endmodule


// ------------------------------------------------------------
// recip_calc: reciproký výpočet kmitočtu (clk_ref_10m).
//   freq_x100000 = round(periods * CONST / dt); gate_ns = dt*2,5 ns.
//   CONST = PRESCALE * 1e5 / 2.5ns:  /4 -> 1.6e14,  /16 -> 6.4e14.
//   NW=80: num = periods*CONST až ~2^74 (u /16 při vysokém f) > 72 bit.
// ------------------------------------------------------------
module recip_calc #(
    parameter [63:0] CONST = 64'd160000000000000   // /4 (1.6e14); /16 = 6.4e14
) (
    input  wire        clk,            // clk_ref_10m
    input  wire        start,          // 1-taktový puls: dt/periods platné
    input  wire [31:0] periods,        // počet period v okně
    input  wire [33:0] dt,             // Δt mezi referenčními hranami [tick 2,5 ns]

    output reg  [63:0] freq_x100000,
    output reg  [63:0] gate_ns,        // skutečné okno = dt * 2,5 ns
    output reg         valid,          // 1-taktový puls: freq_x100000 platné
    output reg         err             // dt==0 (nelze dělit) -> výsledek 0
);
    reg  [79:0] num;
    reg  [33:0] divr;
    reg         run_div;
    reg         pending;

    wire        div_done;
    wire        div_busy;
    wire [79:0] div_q;

    divu_seq #(.NW(80), .DW(34)) u_div (
        .clk(clk),
        .start(run_div),
        .dividend(num),
        .divisor(divr),
        .quotient(div_q),
        .busy(div_busy),
        .done(div_done)
    );

    // MCP kotva: vstupy z P0 domény (r_periods/r_dt) nejdřív zachyť v clk
    // (10M) doméně obyčejnými DFF. Bez toho si DSP packing přetáhl registr
    // do své pipeline s hodinami P0 (MULT18X18 .CLK(clk_p0)!) a cesta
    // count+1 -> násobička padala na 10ns rozpočtu. Hodnoty jsou při start
    // stabilní (toggle handshake jde o >=3 takty později), +1 takt nevadí.
    reg [31:0] periods_q = 32'd0;
    reg [33:0] dt_q      = 34'd0;
    reg        start_q   = 1'b0;

    initial begin
        freq_x100000 = 64'd0;
        gate_ns      = 64'd0;
        valid        = 1'b0;
        err          = 1'b0;
        num          = 80'd0;
        divr         = 34'd0;
        run_div      = 1'b0;
        pending      = 1'b0;
    end

    always @(posedge clk) begin
        periods_q <= periods;
        dt_q      <= dt;
        start_q   <= start;

        valid   <= 1'b0;
        run_div <= 1'b0;

        if (start_q && !pending) begin           // ignoruj start, dokud běží dělení
            gate_ns <= ({30'd0, dt_q} * 64'd5) >> 1;   // dt * 2,5 ns
            if (dt_q == 34'd0) begin
                err          <= 1'b1;
                freq_x100000 <= 64'd0;
                valid        <= 1'b1;                 // publikuj nulu
                pending      <= 1'b0;
            end else begin
                err     <= 1'b0;
                num     <= ({48'd0, periods_q} * CONST) + {47'd0, dt_q[33:1]}; // +dt/2
                divr    <= dt_q;
                run_div <= 1'b1;                      // spusť děličku příští takt
                pending <= 1'b1;
            end
        end

        if (div_done && pending) begin
            freq_x100000 <= div_q[63:0];
            valid        <= 1'b1;
            pending      <= 1'b0;
        end
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
    input  wire         sig_rise,
    input  wire [33:0]  ev_ts,         // {tick_p0, fine}, LSB 2,5 ns
    input  wire         gate_tick,     // ~okno puls (sdílený)
    output reg  [25:0]  r_periods,
    output reg  [33:0]  r_dt,
    output reg  [1:0]   r_fine_first,  // fine kód první hrany okna
    output reg  [1:0]   r_fine_last,   // fine kód poslední hrany okna
    output reg          r_dt_alias,    // okno bez hran > 42,9 s -> r_dt alias (neplatné)
    output reg  [55:0]  r_s1,          // Λ: Σ ts_rel (vůči první hraně)
    output reg  [79:0]  r_s2,          // Λ: Σ S1 (dvojitá akumulace)
    output reg          res_tgl
);
    // Λ/Ω regrese: pro body i=0..N (t_0=0, t_N=Δt) platí
    //   Σ t_i = r_s1,  Σ i·t_i = N·r_s1 − r_s2   (N = r_periods)
    // STM z toho spočte LSQ slope -> kvantizace/nelinearita binů se
    // průměruje ~sqrt(N). Šířky: ts_rel <= ~2^27 (okno), N <= dt/10
    // (debounce ~40 MHz) => S1 < 2^51, S2 < 2^75 - rezerva je.
    //
    // PIPELINE (timing): sub 34b (st0) a akumulace 56/80b (st1) jsou
    // v oddělených taktech - jednotaktová verze měla slack -2,5 ns.
    // Bezpečné: oversampler garantuje >= 2 takty mezi hranami (debounce
    // vyžaduje celo-LOW okno), takže st1 nikdy nekoliduje s dalším st0.
    reg [25:0] count  = 26'd0;
    reg [33:0] ref_ts = 34'd0;
    reg        armed  = 1'b0;
    reg        primed = 1'b0;
    reg [55:0] s1     = 56'd0;
    reg [79:0] s2     = 80'd0;
    // Stáří otevřeného okna: dt je mod 2^34 ticků (42,9 s) - bez tohoto
    // čítače by první měření po >42,9s výpadku signálu tiše aliasovalo
    // (dt_ovf bit33 kryje jen 21,5-42,9 s). Kritická analýza 2026-07-10.
    reg [31:0] win_age = 32'd0;
    reg        age_ovf = 1'b0;

    reg        acc_d    = 1'b0;    // st1: akumuluj ts_rel_d
    reg        close_d  = 1'b0;    // st1: uzávěrka okna (latch r_*)
    reg [33:0] ts_rel_d = 34'd0;
    // st2: horní polovina S2 (80b sčítačka rozdělena 40+40, carry v registru;
    // jednotaktová 80b verze měla slack ~-1,6 ns)
    reg        acc_d2   = 1'b0;
    reg        close_d2 = 1'b0;
    reg        s2_cy    = 1'b0;
    reg [15:0] s1_hi_d  = 16'd0;

    wire [33:0] ts_rel = ev_ts - ref_ts;   // mod 2^34 (okno << rozsah)

    initial begin
        r_periods = 26'd0;  r_dt = 34'd0;  res_tgl = 1'b0;
        r_fine_first = 2'd0; r_fine_last = 2'd0; r_dt_alias = 1'b0;
        r_s1 = 56'd0; r_s2 = 80'd0;
    end

    always @(posedge clk) begin
        if (gate_tick) armed <= 1'b1;

        // stáří okna: saturace na 2^32-1 taktů -> sticky flag (reset při otevření)
        if (&win_age) age_ovf <= 1'b1;
        else          win_age <= win_age + 32'd1;

        // ---- stage 0: událost (jen 34b subtract + řízení okna) ----
        acc_d   <= 1'b0;
        close_d <= 1'b0;
        if (sig_rise) begin
            ts_rel_d <= ts_rel;
            acc_d    <= primed;            // první hrana vůbec se neakumuluje
            if (armed) begin
                close_d      <= primed;
                r_periods    <= count + 26'd1;
                r_fine_first <= ref_ts[1:0];
                r_fine_last  <= ev_ts[1:0];
                r_dt_alias   <= age_ovf;
                // uzavírací hrana = první hrana dalšího okna (gap-free)
                ref_ts  <= ev_ts;
                count   <= 26'd0;
                armed   <= 1'b0;
                primed  <= 1'b1;
                win_age <= 32'd0;      // nové okno začíná teď (přebije inkrement)
                age_ovf <= 1'b0;
            end else begin
                count <= count + 26'd1;
            end
        end

        // ---- stage 1: s1 (56b) + dolních 40b S2 (carry do registru) ----
        acc_d2   <= acc_d;
        close_d2 <= close_d;
        if (acc_d) begin
            {s2_cy, s2[39:0]} <= {1'b0, s2[39:0]} + {1'b0, s1[39:0]};
            s1_hi_d           <= s1[55:40];          // staré S1 (bod i-1)
            if (close_d) begin
                r_dt <= ts_rel_d;
                r_s1 <= s1 + {22'd0, ts_rel_d};      // bod i=N včetně
                s1   <= 56'd0;                       // nové okno
            end else begin
                s1 <= s1 + {22'd0, ts_rel_d};
            end
        end

        // ---- stage 2: horních 40b S2; při uzávěrce latch r_s2 + reset ----
        if (acc_d2) begin
            if (close_d2) begin
                r_s2    <= {s2[79:40] + {24'd0, s1_hi_d} + {39'd0, s2_cy},
                            s2[39:0]};
                res_tgl <= ~res_tgl;
                s2      <= 80'd0;
            end else begin
                s2[79:40] <= s2[79:40] + {24'd0, s1_hi_d} + {39'd0, s2_cy};
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
