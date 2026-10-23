// ============================================================
// File: app_v3.v -- aplikacni vrstva protokolu v3 (domena clk_ref_10m), 2026-10-22. Specifikace: docs/PROTOKOL_V3.md
// v projektu STM (kap. 3 MISO, 4 MOSI, 5 stranky, 6 chovani). Pouziva top_v3.v; bitstream v2 (top.v) ho nepouziva.
//
//  * RX: transakce musi mit presne 1024 B (rx_len_ok), MAGIC, VER = 0x03 a platne CRC bloku 0; LUT_WRITE navic CRC
//    vsech bloku (vadny pouzity blok = cely prikaz zahodit). Jinak RX_LEN_ERR / RX_CRC_ERR a FLAGS bit4.
//  * TX: skladac pise ramec po bajtech do NEAKTIVNI poloviny TX RAM PHY (jako v2): blok 0 = hlavicka (multiplexer
//    nad poli), bloky 1-3 = {k, SEQ[7:0]} + 8 zaznamu po 31 B z kruhu (BSRAM, slova 32 b) nebo stranka (84 polozek
//    po 3 B). CRC bloku (254/255) vklada PHY za letu. Skladani 1024 taktu = 102 us (< mezera 150 us, kap. 2).
//  * Kruh: aplikace zna REC_HEAD (pocet zapsanych zaznamu; prepinaci bit od zapisovace po kazde dvojici A/B)
//    a REC_ACK (od STM). Ramec DATA nese zaznamy od REC_ACK; pri preteceni (nepotvrzenych > 2^RAW - 4) se REC_ACK
//    posune a FLAGS bit1 hlasi preteceni az do dalsiho prijateho ACK.
//  * Zapis tabulky TDC: polozky jdou do 100 MHz domeny stejne jako v2 (lut_wr_sync: data + prepinaci bit 3 takty po nich).
// ============================================================

module app_v3 #(
    parameter [31:0] BUILD_TIME = 32'd0,
    parameter [31:0] BUILD_GIT  = 32'd0,
    parameter        RAW        = 9,          // log2 zaznamu v kruhu (v3_ring)
    parameter [7:0]  CAL_LOG2   = 8'd20       // pocet udalosti kalibrace (souhrnna stranka)
)(
    input  wire        clk,                   // clk_ref_10m
    // ---- PHY v3: RX (staticke po CS nahoru), rx_valid = synchronizovany pulz konce transakce ----
    input  wire        rx_valid,
    input  wire [7:0]  rx_b0, rx_b1, rx_b2,
    input  wire [31:0] rx_seq,
    input  wire [7:0]  rx_p0, rx_p1, rx_p2, rx_p3,
    input  wire [3:0]  rx_blk_ok,
    input  wire        rx_len_ok,
    output reg  [9:0]  rx_raddr,
    input  wire [7:0]  rx_rdata,
    // ---- PHY v3: TX ----
    output wire        tx_half,
    output reg         tx_we,
    output reg  [10:0] tx_waddr,
    output reg  [7:0]  tx_wdata,
    input  wire        phy_busy,              // domena 100 MHz (synchronizuje se zde)
    input  wire        phy_base,
    // ---- kruh zaznamu ----
    input  wire        rec_tgl,               // domena 100 MHz: prepne po zapisu dvojice zaznamu
    output reg  [RAW+2:0] ring_raddr,
    input  wire [31:0] ring_rdata,            // 1 takt po adrese (registrovany vystup RAM)
    // ---- stav (synchronizovany / kvazistaticky) ----
    input  wire [7:0]  st_clk, st_clk_loss, st_coh, st_coh_cnt,
    input  wire [7:0]  st_tdc_a, st_tdc_b,
    input  wire [1:0]  st_cal_wait,
    input  wire        st_cal_run,
    input  wire        st_seg10,              // platna delka segmentu (1 = 10 ms)
    input  wire [31:0] pps_seq,               // zive z 100 MHz (STM bere shodu dvou ramcu)
    input  wire [33:0] pps_last, pps_prev,
    input  wire [7:0]  bub_a, bub_b,
    input  wire [3:0]  ovl_a, ovl_b,
    input  wire [15:0] tdc_cfg, maxtap_a, maxtap_b,
    input  wire [1:0]  cal_retry,
    output reg  [9:0]  hist_k,                // vypis histogramu (oba kanaly)
    input  wire [23:0] hist_a, hist_b,
    output reg  [8:0]  raw_k,
    input  wire [47:0] raw_q,
    input  wire [9:0]  raw_n,
    // ---- konfigurace ----
    output reg         cal_tgl,               // SET_CONFIG 0x02 start (prepinaci bit) ...
    output reg  [1:0]  cal_mask,              // ... maska kanalu (0 = oba)
    output reg         ref_sel,
    output reg         mask_cs_en,
    output reg         led_hb_en,
    output reg         seg10,
    output reg         raw_tgl,
    output reg         raw_ch,
    output reg         lw_tgl,
    output reg  [9:0]  lw_a,
    output reg  [13:0] lw_neg,
    output reg         lw_ch,
    output reg         lw_commit_tgl,
    output reg  [1:0]  lw_commit_mask,        // kanaly, ktere potvrzeni aktivuje (kvazistaticke pred prepnutim lw_commit_tgl)
    output reg  [1:0]  bsrc                   // FW 0x0501, SET_CONFIG 0x09: bit0 = kanal B z pinu A, bit1 = B invertovany
);
    localparam [7:0]  MAGIC = 8'hA5, VER = 8'h03;
    localparam [7:0]  T_SETCFG = 8'h01, T_ACK = 8'h06, T_START = 8'h08, T_STOP = 8'h09, T_LUTW = 8'h0A, T_RAW = 8'h0B;
    localparam [7:0]  T_DATA = 8'h80, T_PAGE = 8'hA0;
    // FW 0x0500 = prvni bitstream protokolu v3 (top_v3.v). Rada 0x04xx je v2 (top.v), cisla se nesdili.
    // FW 0x0501: SET_CONFIG 0x09 zdroj kanalu B (jednorazove deje: sirka pulzu, kalibrace zpozdeni A/B), CAPS bit20.
    localparam [15:0] FW_VERSION = 16'h0501;
    // CAPS: bity v2 tam, kde funkce trva (1 SET_CONFIG, 4 tabulku zapisuje STM, 5 cas v T/16384, 10 samokontrola, 11 volba
    // reference, 12 kmitocet hodin v ramci, 13 1PPS, 14 diagnostika E0) + bit16 zaznamy v3, bit19 banky tabulky TDC.
    // bit17 (1PPS pres IDES8) a bit18 (predelicka pin_cnt) = 0: v teto verzi nejsou (kap. 8 krok 8, volitelne).
    // bit20 (FW 0x0501) = SET_CONFIG 0x09 zdroj kanalu B.
    localparam [31:0] CAPS = 32'h0019_7C32;
    localparam [31:0] RING_USE = (32'd1 << RAW) - 32'd4;   // rezerva 2 segmentu: zapisovac neprepise zaznam, ktery se kopiruje

    initial begin
        rx_raddr = 10'd0; tx_we = 1'b0; tx_waddr = 11'd0; tx_wdata = 8'd0; ring_raddr = {(RAW+3){1'b0}};
        hist_k = 10'd0; raw_k = 9'd0;
        cal_tgl = 1'b0; cal_mask = 2'd0; ref_sel = 1'b0; mask_cs_en = 1'b1; led_hb_en = 1'b0; seg10 = 1'b0;
        raw_tgl = 1'b0; raw_ch = 1'b0;
        lw_tgl = 1'b0; lw_a = 10'd0; lw_neg = 14'd0; lw_ch = 1'b0; lw_commit_tgl = 1'b0; lw_commit_mask = 2'b11; bsrc = 2'b00;
    end

    // ---- casovani ramce, kruh ----
    reg [31:0] ts10 = 32'd0;
    always @(posedge clk) ts10 <= ts10 + 32'd1;
    reg [2:0]  rt_s = 3'b000;
    reg [31:0] rec_head = 32'd0, rec_ack = 32'd0;
    reg        ovf = 1'b0;

    // ---- stav prijmu ----
    reg [15:0] rx_crc_err = 16'd0, rx_len_err = 16'd0;
    reg [31:0] rx_last = 32'd0;
    reg        rx_err = 1'b0, cmd_done = 1'b0;
    reg [13:0] lut_cnt = 14'd0;
    reg [13:0] lut_cnt_b = 14'd0;            // polozky s bitem kanalu B (LUT_SUM chybu kanalu u cele tabulky nevidi)
    reg [15:0] lut_sum = 16'd0;
    reg [2:0]  lw_dly = 3'b000, lc_dly = 3'b000;
    reg        page_req = 1'b0;
    reg [7:0]  page_kind = 8'd0;
    reg [15:0] page_no = 16'd0;

    // ---- TX RAM PHY ----
    reg        tx_half_r = 1'b0;
    assign tx_half = tx_half_r;
    reg [2:0]  pb_s = 3'b000, pbase_s = 3'b000;
    reg [2:0]  cool = 3'd0;
    always @(posedge clk) begin pb_s <= {pb_s[1:0], phy_busy}; pbase_s <= {pbase_s[1:0], phy_base}; end
    wire       tx_can = (cool == 3'd0) && !(pb_s[2] && (pbase_s[2] == ~tx_half_r));

    // ---- FSM ----
    localparam [2:0] S_IDLE = 3'd0, S_RX = 3'd1, S_LUT = 3'd2, S_ASM = 3'd3;
    reg [2:0]  state = S_IDLE;
    reg        rx_pend = 1'b0, dirty = 1'b1;
    reg [31:0] seq_tx = 32'd0;

    // snimek pro skladany ramec
    reg [31:0] a_first = 32'd0, a_head = 32'd0, a_ts = 32'd0;
    reg [4:0]  a_nrec = 5'd0;
    reg        a_page = 1'b0;
    reg [7:0]  a_kind = 8'd0;
    reg [15:0] a_pno = 16'd0;
    reg [15:0] a_pbase = 16'd0;              // pno * 252
    reg [15:0] a_pcnt = 16'd0;               // polozek na strance
    reg [15:0] a_ptot = 16'd0;               // polozek celkem (druh)

    // ---- skladac: index, citace zaznamu / polozek, dvoustupnova roura ----
    reg [10:0] ai = 11'd0;                   // index bajtu 0..1023 (bit 10 = konec)
    reg [2:0]  m  = 3'd0;                    // zaznam v bloku 0..7
    reg [4:0]  rb = 5'd0;                    // bajt zaznamu 0..30
    reg [6:0]  q  = 7'd0;                    // polozka stranky v bloku 0..83
    reg [1:0]  bi = 2'd0;                    // bajt polozky 0..2
    reg [15:0] gbase = 16'd0;                // globalni index polozky 0 bloku
    reg [23:0] item_r = 24'd0;
    reg [7:0]  d1 = 8'd0, d2 = 8'd0;
    reg        r1 = 1'b0, r2 = 1'b0;         // bajt je z kruhu (vybere se v 2. stupni)
    reg [1:0]  l1 = 2'd0, l2 = 2'd0;
    reg        w1 = 1'b0, w2 = 1'b0;
    reg [9:0]  i1 = 10'd0, i2 = 10'd0;

    wire [1:0] blk = ai[9:8];
    wire [7:0] bb  = ai[7:0];
    wire [15:0] gb_blk = a_pbase + ((blk == 2'd1) ? 16'd0 : (blk == 2'd2) ? 16'd84 : 16'd168);   // polozka 0 bloku

    // ---- hlavicka (blok 0), multiplexer nad bajty ----
    wire [7:0]  flags_b = {2'b00, cmd_done, rx_err, 1'b0, st_cal_run, ovf, (a_nrec != 5'd0) & ~a_page};
    wire [31:0] head_m1 = (a_head != 32'd0) ? (a_head - 32'd1) : 32'd0;
    wire [31:0] fill    = a_head - a_first;
    wire [15:0] fill16  = (fill > 32'h0000FFFF) ? 16'hFFFF : fill[15:0];
    wire [47:0] pts_l   = {pps_last, 14'd0};
    wire [47:0] pts_p   = {pps_prev, 14'd0};
    wire [31:0] pseq_p  = (pps_seq != 32'd0) ? (pps_seq - 32'd1) : 32'd0;
    wire [7:0]  pfl_l   = {7'd0, (pps_seq != 32'd0)};
    wire [7:0]  pfl_p   = {7'd0, (pps_seq > 32'd1)};
    reg  [7:0]  hb;
    always @* begin
        hb = 8'd0;
        case (bb)
            8'd0:  hb = MAGIC;
            8'd1:  hb = VER;
            8'd2:  hb = a_page ? T_PAGE : T_DATA;
            8'd3:  hb = flags_b;
            8'd4:  hb = seq_tx[7:0];   8'd5:  hb = seq_tx[15:8];  8'd6:  hb = seq_tx[23:16]; 8'd7:  hb = seq_tx[31:24];
            8'd8:  hb = 8'h00;         8'd9:  hb = 8'h04;         8'd10: hb = 8'd4;
            8'd12: hb = BUILD_TIME[7:0];  8'd13: hb = BUILD_TIME[15:8]; 8'd14: hb = BUILD_TIME[23:16]; 8'd15: hb = BUILD_TIME[31:24];
            8'd16: hb = BUILD_GIT[7:0];   8'd17: hb = BUILD_GIT[15:8];  8'd18: hb = BUILD_GIT[23:16];  8'd19: hb = BUILD_GIT[31:24];
            8'd20: hb = FW_VERSION[7:0];  8'd21: hb = FW_VERSION[15:8];
            8'd22: hb = 8'd100;
            8'd23: hb = st_seg10 ? 8'd10 : 8'd25;
            8'd24: hb = CAPS[7:0];  8'd25: hb = CAPS[15:8];  8'd26: hb = CAPS[23:16];  8'd27: hb = CAPS[31:24];
            8'd28: hb = a_ts[7:0];  8'd29: hb = a_ts[15:8];  8'd30: hb = a_ts[23:16];  8'd31: hb = a_ts[31:24];
            8'd32: hb = head_m1[7:0];  8'd33: hb = head_m1[15:8];  8'd34: hb = head_m1[23:16];  8'd35: hb = head_m1[31:24];
            8'd36: hb = a_first[7:0];  8'd37: hb = a_first[15:8];  8'd38: hb = a_first[23:16];  8'd39: hb = a_first[31:24];
            8'd40: hb = a_page ? 8'd0 : {3'd0, a_nrec};
            8'd42: hb = fill16[7:0];   8'd43: hb = fill16[15:8];
            8'd44: hb = st_clk;        8'd45: hb = st_clk_loss;   8'd46: hb = st_coh;        8'd47: hb = st_coh_cnt;
            8'd48: hb = st_tdc_a;      8'd49: hb = st_tdc_b;
            8'd50: hb = {5'd0, 1'b0, (pps_seq != 32'd0), 1'b0};
            8'd51: hb = {6'd0, st_cal_wait};
            8'd52: hb = rx_crc_err[7:0];  8'd53: hb = rx_crc_err[15:8];
            8'd54: hb = rx_len_err[7:0];  8'd55: hb = rx_len_err[15:8];
            8'd56: hb = rx_last[7:0];  8'd57: hb = rx_last[15:8];  8'd58: hb = rx_last[23:16];  8'd59: hb = rx_last[31:24];
            8'd60: hb = lut_cnt[7:0];  8'd61: hb = {2'd0, lut_cnt[13:8]};
            8'd62: hb = lut_sum[7:0];  8'd63: hb = lut_sum[15:8];
            8'd66: hb = {3'd0, bsrc[1], bsrc[0], ref_sel, led_hb_en, mask_cs_en};
            8'd68: hb = pps_seq[7:0];  8'd69: hb = pps_seq[15:8];  8'd70: hb = pps_seq[23:16];  8'd71: hb = pps_seq[31:24];
            8'd72: hb = pts_l[7:0];    8'd73: hb = pts_l[15:8];    8'd74: hb = pts_l[23:16];
            8'd75: hb = pts_l[31:24];  8'd76: hb = pts_l[39:32];   8'd77: hb = pts_l[47:40];
            8'd78: hb = pfl_l;
            8'd84: hb = pseq_p[7:0];   8'd85: hb = pseq_p[15:8];   8'd86: hb = pseq_p[23:16];  8'd87: hb = pseq_p[31:24];
            8'd88: hb = pts_p[7:0];    8'd89: hb = pts_p[15:8];    8'd90: hb = pts_p[23:16];
            8'd91: hb = pts_p[31:24];  8'd92: hb = pts_p[39:32];   8'd93: hb = pts_p[47:40];
            8'd94: hb = pfl_p;
            8'd100: hb = pps_last[7:0];  8'd101: hb = pps_last[15:8];  8'd102: hb = pps_last[23:16];  8'd103: hb = pps_last[31:24];
            8'd104: hb = pps_prev[7:0];  8'd105: hb = pps_prev[15:8];  8'd106: hb = pps_prev[23:16];  8'd107: hb = pps_prev[31:24];
            8'd112: hb = bub_a;  8'd113: hb = bub_b;  8'd114: hb = {4'd0, ovl_a};  8'd115: hb = {4'd0, ovl_b};
            8'd116: hb = a_page ? a_kind : 8'd0;
            8'd117: hb = a_page ? a_pno[7:0] : 8'd0;   8'd118: hb = a_page ? a_pno[15:8] : 8'd0;
            8'd119: hb = a_page ? a_pcnt[7:0] : 8'd0;  8'd120: hb = a_page ? a_pcnt[15:8] : 8'd0;
            8'd122: hb = lut_cnt_b[7:0];  8'd123: hb = {2'd0, lut_cnt_b[13:8]};
            default: hb = 8'd0;
        endcase
    end

    // ---- polozka stranky (index g, adresu nastavil skladac >= 2 takty predem) ----
    wire [15:0] g_cur = gbase + {9'd0, q};
    reg  [23:0] it_src;
    always @* begin
        case (a_kind[1:0])
            2'd0: case (g_cur[2:0])      // souhrn kalibrace
                      3'd0: it_src = {8'd0, st_tdc_b, st_tdc_a};
                      3'd1: it_src = {8'd0, maxtap_a};
                      3'd2: it_src = {8'd0, maxtap_b};
                      3'd3: it_src = {8'd0, tdc_cfg};
                      3'd4: it_src = {22'd0, cal_retry};
                      3'd5: it_src = {14'd0, raw_n};
                      3'd6: it_src = {16'd0, CAL_LOG2};
                      default: it_src = {22'd0, st_cal_wait};
                  endcase
            2'd1: it_src = hist_a;
            2'd2: it_src = hist_b;
            default: it_src = g_cur[0] ? raw_q[47:24] : raw_q[23:0];
        endcase
        if (g_cur >= a_ptot || q >= 7'd84) it_src = 24'd0;
    end

    // ---- zaznam j bloku: index v kruhu ----
    wire [4:0]     jrec = {blk - 2'd1, m};                                 // (blk-1)*8 + m
    wire [RAW+4:0] rsum = {5'd0, a_first[RAW-1:0]} + {{RAW{1'b0}}, jrec};   // (sirka obecne, i RAW < 5 v simulaci)
    wire [RAW-1:0] rlo  = rsum[RAW-1:0];

    // ---- LUT_WRITE (davka): cteni RX RAM ----
    reg [1:0]  lk = 2'd1;                    // blok 1..3
    reg [6:0]  ln = 7'd0, li = 7'd0;         // pocet polozek bloku, index polozky
    reg [3:0]  lp = 4'd0;                    // faze (cteni s latenci 2 takty)
    reg [7:0]  lb0 = 8'd0, lb1 = 8'd0;

    always @(posedge clk) begin
        tx_we <= 1'b0;
        if (cool != 3'd0) cool <= cool - 3'd1;
        lw_dly <= {lw_dly[1:0], 1'b0};
        lc_dly <= {lc_dly[1:0], 1'b0};
        if (lw_dly[2]) lw_tgl        <= ~lw_tgl;
        if (lc_dly[2]) lw_commit_tgl <= ~lw_commit_tgl;

        // kruh: novy par zaznamu; preteceni posune REC_ACK (ne behem skladani -- snimek a_first uz plati)
        rt_s <= {rt_s[1:0], rec_tgl};
        if (rt_s[2] ^ rt_s[1]) begin rec_head <= rec_head + 32'd2; dirty <= 1'b1; end
        if (state != S_ASM && (rec_head - rec_ack) > RING_USE) begin rec_ack <= rec_head - RING_USE; ovf <= 1'b1; end

        if (rx_valid) rx_pend <= 1'b1;

        case (state)
            S_IDLE: begin
                if (rx_pend) begin
                    rx_pend <= 1'b0;
                    state   <= S_RX;
                end else if (dirty && tx_can) begin
                    dirty   <= 1'b0;
                    seq_tx  <= seq_tx + 32'd1;
                    a_first <= rec_ack;
                    a_head  <= rec_head;
                    a_nrec  <= ((rec_head - rec_ack) > 32'd24) ? 5'd24 : (rec_head[4:0] - rec_ack[4:0]);
                    a_ts    <= ts10;
                    a_page  <= page_req;
                    a_kind  <= page_kind;
                    a_pno   <= page_no;
                    a_pbase <= {page_no[7:0], 8'd0} - {page_no[13:0], 2'd0};          // * 252
                    a_ptot  <= (page_kind[1:0] == 2'd0) ? 16'd8 : (page_kind[1:0] == 2'd3) ? {5'd0, raw_n, 1'b0} : 16'd512;
                    ai      <= 11'd0;
                    w1      <= 1'b0; w2 <= 1'b0;
                    state   <= S_ASM;
                end
            end

            S_RX: begin
                state <= S_IDLE;
                dirty <= 1'b1;
                if (!rx_len_ok) begin
                    if (rx_len_err != 16'hFFFF) rx_len_err <= rx_len_err + 16'd1;
                    rx_err <= 1'b1;
                end else if (rx_b0 != MAGIC || rx_b1 != VER || !rx_blk_ok[0]) begin
                    if (!rx_blk_ok[0] && rx_crc_err != 16'hFFFF) rx_crc_err <= rx_crc_err + 16'd1;
                    rx_err <= 1'b1;
                end else begin
                    rx_err   <= 1'b0;
                    cmd_done <= 1'b1;
                    rx_last  <= {rx_seq[23:0], rx_b2};
                    case (rx_b2)
                        T_ACK: begin
                            if (({rx_p3, rx_p2, rx_p1, rx_p0} - rec_ack) <= (rec_head - rec_ack)) begin
                                rec_ack <= {rx_p3, rx_p2, rx_p1, rx_p0};
                                ovf     <= 1'b0;
                            end
                        end
                        T_SETCFG: case (rx_p0)
                            8'h02: if (rx_p1[0]) begin cal_mask <= rx_p1[2:1]; cal_tgl <= ~cal_tgl; end
                            8'h04: ref_sel <= rx_p1[0];
                            8'h05: begin mask_cs_en <= rx_p1[0]; led_hb_en <= rx_p1[1]; end
                            8'h06: begin
                                if (rx_p1[0]) begin                                        // potvrzeni = prepnuti banky
                                    lc_dly <= 3'b001;                                      // (maska 3 takty pred prepinacim bitem)
                                    lw_commit_mask <= (rx_p2[1:0] == 2'b00) ? 2'b11 : rx_p2[1:0];   // p2: bit0 A, bit1 B, 0 = oba
                                end
                                else begin lut_cnt <= 14'd0; lut_cnt_b <= 14'd0; lut_sum <= 16'd0; end   // zacatek zapisu
                            end
                            8'h08: begin
                                if (rx_p1 == 8'd10) seg10 <= 1'b1;
                                else if (rx_p1 == 8'd25) seg10 <= 1'b0;
                            end
                            8'h09: bsrc <= rx_p1[1:0];                                  // FW 0x0501: zdroj kanalu B
                            default: ;                                                  // 0x01, 0x03 (v3 bez vyznamu), 0x07
                        endcase
                        T_LUTW: begin
                            if (rx_blk_ok == 4'b1111) begin lk <= 2'd1; lp <= 4'd0; li <= 7'd0; state <= S_LUT; end
                            else begin
                                cmd_done <= 1'b0; rx_err <= 1'b1;
                                if (rx_crc_err != 16'hFFFF) rx_crc_err <= rx_crc_err + 16'd1;
                            end
                        end
                        T_PAGE: begin page_req <= 1'b1; page_kind <= rx_p0; page_no <= {rx_p2, rx_p1}; end
                        T_RAW:  begin raw_ch <= rx_p0[0]; raw_tgl <= ~raw_tgl; end
                        T_START, T_STOP: ;
                        default: cmd_done <= 1'b0;                                      // neznamy TYPE = neprovedeno
                    endcase
                end
            end

            // ---- LUT_WRITE: bloky 1..3, v kazdem [0] = pocet (0..84), od [1] polozky po 3 B ----
            S_LUT: begin
                lp <= lp + 4'd1;
                case (lp)
                    4'd0: rx_raddr <= {lk, 8'd0};
                    4'd3: begin ln <= (rx_rdata > 8'd84) ? 7'd84 : rx_rdata[6:0]; li <= 7'd0; end
                    4'd4: begin
                        if (li == ln) begin                                         // blok hotov
                            if (lk == 2'd3) begin state <= S_IDLE; dirty <= 1'b1; end
                            else begin lk <= lk + 2'd1; lp <= 4'd0; end
                        end else rx_raddr <= {lk, 8'd1} + {2'd0, li, 1'b0} + {3'd0, li};   // 1 + 3*li
                    end
                    4'd5: rx_raddr <= rx_raddr + 10'd1;
                    4'd6: begin rx_raddr <= rx_raddr + 10'd1; lb0 <= rx_rdata; end
                    4'd7: lb1 <= rx_rdata;
                    4'd8: begin                                                     // treti bajt = rx_rdata
                        lw_a   <= {1'b0, lb1[6], lb0};
                        lw_ch  <= lb1[7];
                        lw_neg <= {lb1[5:0], rx_rdata};
                        lw_dly <= 3'b001;
                        if (lut_cnt != 14'h3FFF) lut_cnt <= lut_cnt + 14'd1;
                        if (lb1[7] && lut_cnt_b != 14'h3FFF) lut_cnt_b <= lut_cnt_b + 14'd1;
                        lut_sum <= lut_sum + ({1'b0, lb1[7], lb1[6], lb0, 5'd0} ^ {2'd0, lb1[5:0], rx_rdata});
                        li <= li + 7'd1;
                        lp <= 4'd4;
                    end
                    default: ;
                endcase
            end

            // ---- skladani ramce ----
            S_ASM: begin
                // stupen 0: bajt ai
                if (!ai[10]) begin
                    w1 <= 1'b1; i1 <= ai[9:0]; r1 <= 1'b0; d1 <= 8'd0;
                    if (blk == 2'd0) d1 <= hb;
                    else if (bb == 8'd0) begin
                        d1 <= {6'd0, blk};
                        m <= 3'd0; rb <= 5'd0; q <= 7'd0; bi <= 2'd0;
                        // prvni polozka bloku: adresa 2 takty (200 ns) predem -- it_src se bere v bb == 2
                        gbase  <= gb_blk;
                        hist_k <= gb_blk[9:0];
                        raw_k  <= gb_blk[9:1];
                    end else if (bb == 8'd1) begin
                        d1 <= seq_tx[7:0];
                    end else if (!a_page) begin
                        if (bb < 8'd250) begin
                            if ({blk - 2'd1, m} < a_nrec) begin
                                ring_raddr <= {rlo, rb[4:2]};
                                r1 <= 1'b1; l1 <= rb[1:0];
                            end
                            if (rb == 5'd30) begin rb <= 5'd0; m <= m + 3'd1; end
                            else rb <= rb + 5'd1;
                        end
                    end else begin
                        if (bb < 8'd254) begin
                            if (bi == 2'd0) begin
                                d1 <= it_src[7:0]; item_r <= it_src;
                                hist_k <= g_cur[9:0] + 10'd1;                       // dalsi polozka (za 3 takty)
                                raw_k  <= (g_cur[9:0] + 10'd1) >> 1;
                            end else d1 <= (bi == 2'd1) ? item_r[15:8] : item_r[23:16];
                            if (bi == 2'd2) begin bi <= 2'd0; q <= q + 7'd1; end
                            else bi <= bi + 2'd1;
                        end
                    end
                    ai <= ai + 11'd1;
                end else w1 <= 1'b0;
                // stupen 1 -> 2 (data z kruhu prijdou v tomto taktu)
                w2 <= w1; i2 <= i1; d2 <= d1; r2 <= r1; l2 <= l1;
                // stupen 2: zapis do TX RAM
                if (w2) begin
                    tx_we    <= 1'b1;
                    tx_waddr <= {~tx_half_r, i2};
                    tx_wdata <= r2 ? ring_rdata[{l2, 3'b000} +: 8] : d2;
                    if (i2 == 10'd1023) begin
                        tx_half_r <= ~tx_half_r;
                        cool      <= 3'd5;
                        if (a_page) page_req <= 1'b0;
                        state     <= S_IDLE;
                    end
                end
            end

            default: state <= S_IDLE;
        endcase
    end

    // pocet polozek na strance (kvazistaticke behem skladani)
    always @(posedge clk) a_pcnt <= (a_ptot > a_pbase) ? (((a_ptot - a_pbase) > 16'd252) ? 16'd252 : (a_ptot - a_pbase)) : 16'd0;
endmodule
