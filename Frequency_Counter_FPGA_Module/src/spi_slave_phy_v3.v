// ============================================================
// File: spi_slave_phy_v3.v
// SPI SLAVE PHY pro protokol v3 (docs/PROTOKOL_V3.md v projektu STM): Mode 0 (CPOL=0, CPHA=0), MSB-first,
// transakce PRESNE 1024 B = 4 bloky x 256 B, CRC-16/CCITT-FALSE na blok (bajty 0..253 bloku, LE na 254..255).
// 2026-10-22, NEZAPOJENO do top.v (dnesni bitstream pouziva spi_slave_phy.v, protokol v2 128 B).
//
// Vychazi z spi_slave_phy.v (#264: PHY taktovana primo SCK, ramce v blokove RAM). Rozdily:
//   * 1024 B: citace hran 14 b, index bajtu 10 b (blok = index[9:8], bajt v bloku = index[7:0]).
//   * TX: CRC kazdeho bloku pocita PHY ZA LETU z vysilanych bajtu a na pozice 254/255 bloku ho vlozi samo
//     (obsah TX RAM na techto pozicich se nevysila). Skladac ramce CRC neresi a na drat nemuze jit blok
//     s CRC, ktere nesedi k vyslanym datum.
//   * RX: CRC kazdeho bloku za letu, vysledek `rx_blk_ok[3:0]` (staticky po CS nahoru); hlavicka bloku 0
//     (MAGIC, VER, TYPE, SEQ, p0..p3) jako ve v2; CELY ramec do RX RAM (2 poloviny x 1024 B, stridave),
//     aplikace cte polovinu `rx_rd_half` (bloky 1-3 nesou LUT_WRITE).
//   * Delka: `rx_len_ok` = transakce mela presne 8192 nabeznych hran SCK. Kratsi i delsi transakci ma
//     aplikace zahodit (PROTOKOL_V3 kap. 2, RX_LEN_ERR).
//   * Bajt 0 je MAGIC natvrdo (jako v2) -> CRC bloku 0 zacina konstantou crc16(FFFF, A5).
//
// Pametova mapa: TX RAM 2048 x 8 {polovina, index[9:0]} (zapis clk_app, cteni SCK) = 1 BSRAM;
//                RX RAM 2048 x 8 {polovina, index[9:0]} (zapis SCK, cteni clk_app) = 1 BSRAM.
// Casovani: TX vystup MISO se meni SESTUPNOU hranou, CRC TX se pocita z tx_rdata (RAM, nabezna hrana) do
// sestupne hrany = pulperioda SCK (20 ns pri 25 MHz); overit STA pri zapojeni (create_clock spi_sck >= 30 MHz).
// ============================================================

module spi_slave_phy_v3 (
    input  wire        clk,          // clk_p0_100m: CS, stav, konec ramce
    input  wire        clk_app,      // clk_ref_10m: aplikacni porty RAM

    input  wire        sck_pin,
    input  wire        cs_pin,       // active LOW
    input  wire        mosi_pin,
    output wire        miso,

    // TX: aplikace pise ramec do TX RAM (domena clk_app); bajty 254/255 kazdeho bloku a bajt 0 PHY prepise
    input  wire        tx_half,      // polovina, kterou ma PHY vysilat
    input  wire        tx_we,
    input  wire [10:0] tx_waddr,     // {polovina, index bajtu 0..1023}
    input  wire [7:0]  tx_wdata,
    output reg         tx_base,      // polovina, kterou PHY vysila (domena clk; po dobu transakce drzena)
    output wire        busy,         // CS dole (domena clk)

    // RX: hlavicka bloku 0 + stav bloku (SCK domena, staticke po CS nahoru)
    output reg  [7:0]  rx_b0, rx_b1, rx_b2,   // MAGIC, VER, TYPE
    output reg  [31:0] rx_seq,                // bajty 4..7 (LE)
    output reg  [7:0]  rx_p0, rx_p1, rx_p2, rx_p3,   // bajty 12..15 (p0..p2; ACK: REC_ACK = {p3,p2,p1,p0})
    output reg  [3:0]  rx_blk_ok,             // CRC bloku k sedi
    output reg         rx_len_ok,             // presne 1024 B (domena clk, latch pri CS nahoru)
    output reg         frame_end_tgl,         // toggle pri CS nahoru (domena clk)

    // RX RAM: cteni aplikaci (domena clk_app), polovina posledniho dokonceneho ramce
    output reg         rx_rd_half,
    input  wire [9:0]  rx_raddr,
    output reg  [7:0]  rx_rdata,

    output reg  [15:0] dbg_sck_cnt            // pocet nabeznych hran SCK posledni transakce (spravne 8192)
);

    localparam [7:0]  MAGIC     = 8'hA5;
    localparam [13:0] NBITS     = 14'd8192;

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
    localparam [15:0] CRC_MAGIC = 16'h04BF;   // crc16_step(16'hFFFF, 8'hA5) (spocitano, CRC-16/CCITT-FALSE), overuje tb_phy_v3

    // ---------------- stav v domene clk (100 MHz) ----------------
    reg [2:0] cs_s   = 3'b111;
    reg [2:0] half_s = 3'b000;
    always @(posedge clk) begin
        cs_s   <= {cs_s[1:0],   cs_pin};
        half_s <= {half_s[1:0], tx_half};
    end
    wire cs_active = ~cs_s[2];
    wire cs_rise   = (cs_s[2:1] == 2'b01);
    wire cs_fall   = (cs_s[2:1] == 2'b10);
    assign busy    = cs_active;
    reg  clr = 1'b1;                 // puls: novy ramec -> vynulovat citace SCK domeny
    always @(posedge clk) clr <= cs_fall;
    reg  rx_wr_half = 1'b0;          // polovina RX RAM, do ktere se pise (meni se jen pri CS nahoru)

    // ---------------- SCK domena: RX ----------------
    reg [13:0] rcnt = 14'd0;         // pocet nabeznych hran (saturuje na NBITS + 1 = „delsi")
    reg [6:0]  rsh  = 7'd0;
    always @(posedge sck_pin or posedge clr) begin
        if (clr) rcnt <= 14'd0;
        else if (rcnt != NBITS + 14'd1) rcnt <= rcnt + 14'd1;
    end
    always @(posedge sck_pin) rsh <= {rsh[5:0], mosi_pin};

    wire       rbyte_done = (rcnt[2:0] == 3'd7) && (rcnt[13] == 1'b0);   // jen bajty 0..1023
    wire [7:0] rbyte      = {rsh, mosi_pin};
    wire [9:0] rk         = rcnt[12:3];
    wire [7:0] rb_in_blk  = rk[7:0];
    reg  [15:0] rcrc   = 16'hFFFF;
    reg  [7:0]  rcrc_lo = 8'd0;
    always @(posedge sck_pin or posedge clr) begin
        if (clr) begin
            rcrc      <= 16'hFFFF;
            rx_blk_ok <= 4'd0;
        end else if (rbyte_done) begin
            if (rb_in_blk == 8'd0)        rcrc <= crc16_step(16'hFFFF, rbyte);       // zacatek bloku
            else if (rb_in_blk < 8'd254)  rcrc <= crc16_step(rcrc, rbyte);
            if (rb_in_blk == 8'd255)      rx_blk_ok[rk[9:8]] <= ({rbyte, rcrc_lo} == rcrc);
        end
    end
    always @(posedge sck_pin) begin
        if (rbyte_done && rb_in_blk == 8'd254) rcrc_lo <= rbyte;
        if (rbyte_done) case (rk)
            10'd0:  rx_b0         <= rbyte;
            10'd1:  rx_b1         <= rbyte;
            10'd2:  rx_b2         <= rbyte;
            10'd4:  rx_seq[7:0]   <= rbyte;
            10'd5:  rx_seq[15:8]  <= rbyte;
            10'd6:  rx_seq[23:16] <= rbyte;
            10'd7:  rx_seq[31:24] <= rbyte;
            10'd12: rx_p0         <= rbyte;
            10'd13: rx_p1         <= rbyte;
            10'd14: rx_p2         <= rbyte;
            10'd15: rx_p3         <= rbyte;
            default: ;
        endcase
    end

    // RX RAM: zapis SCK (cely ramec), cteni clk_app
    reg [7:0] rxram [0:2047];
    always @(posedge sck_pin)
        if (rbyte_done) rxram[{rx_wr_half, rk}] <= rbyte;
    always @(posedge clk_app)
        rx_rdata <= rxram[{rx_rd_half, rx_raddr}];

    // ---------------- SCK domena: TX ----------------
    reg [13:0] tcnt = 14'd0;         // pocet sestupnych hran v ramci
    reg [7:0]  cur  = MAGIC;         // vysilany bajt (MSB = MISO); bajt 0 = MAGIC natvrdo
    reg [7:0]  tx_rdata;
    reg [15:0] tcrc = CRC_MAGIC;     // CRC vysilaneho bloku (bajty 0..253)
    wire [9:0] tk_next = tcnt[12:3] + 10'd1;          // index bajtu, ktery se nacita na hranici
    wire [7:0] tb_next = tk_next[7:0];
    always @(negedge sck_pin or posedge clr) begin
        if (clr) begin
            tcnt <= 14'd0;
            cur  <= MAGIC;
            tcrc <= CRC_MAGIC;
        end else begin
            tcnt <= tcnt + 14'd1;
            if (tcnt[2:0] == 3'd7) begin                   // hranice bajtu: dalsi bajt
                if (tb_next == 8'd254)      cur <= tcrc[7:0];       // CRC bloku, LE
                else if (tb_next == 8'd255) cur <= tcrc[15:8];
                else begin
                    cur  <= tx_rdata;
                    tcrc <= (tb_next == 8'd0) ? crc16_step(16'hFFFF, tx_rdata) : crc16_step(tcrc, tx_rdata);
                end
            end else cur <= {cur[6:0], 1'b0};
        end
    end
    assign miso = cur[7];

    // TX RAM: zapis clk_app, cteni SCK (nabezna hrana; adresa = NASLEDUJICI bajt)
    reg [7:0] txram [0:2047];
    always @(posedge clk_app)
        if (tx_we) txram[tx_waddr] <= tx_wdata;
    always @(posedge sck_pin)
        tx_rdata <= txram[{tx_base, tk_next}];

    // ---------------- konec ramce, poloviny (domena clk) ----------------
    initial begin
        tx_base = 1'b0; frame_end_tgl = 1'b0; rx_len_ok = 1'b0; rx_rd_half = 1'b1; dbg_sck_cnt = 16'd0;
        tx_rdata = 8'd0; rx_rdata = 8'd0; rx_blk_ok = 4'd0;
        rx_b0 = 8'd0; rx_b1 = 8'd0; rx_b2 = 8'd0; rx_seq = 32'd0;
        rx_p0 = 8'd0; rx_p1 = 8'd0; rx_p2 = 8'd0; rx_p3 = 8'd0;
    end
    always @(posedge clk) begin
        if (!cs_active) tx_base <= half_s[2];
        if (cs_rise) begin
            frame_end_tgl <= ~frame_end_tgl;
            dbg_sck_cnt   <= {2'd0, rcnt};
            rx_len_ok     <= (rcnt == NBITS);
            rx_rd_half    <= rx_wr_half;     // aplikace cte prave dokonceny ramec
            rx_wr_half    <= ~rx_wr_half;    // dalsi ramec do druhe poloviny
        end
    end

endmodule
