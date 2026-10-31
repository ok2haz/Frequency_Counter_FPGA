// ============================================================
// File: spi_slave_phy.v
// SPI SLAVE PHY (Mode 0: CPOL=0, CPHA=0), MSB-first, 128 B/transakce
// (protokol v2 dle FPGA_PROTOCOL_V2_NAVRH.md).
//
// 🔴 2026-10-04 (TODO #264): PHY TAKTOVANA PRIMO SCK + RAMCE V BLOKOVE RAM.
//   Driv: SCK/CS/MOSI oversamplovane 100 MHz, TX ramec v 1024bit posuvnem
//   registru, RX v 1024bit rx_shadow (+ aplikace 685 FF tx_b) = ~2700 FF a
//   strop SCK ~8-10 MHz (MISO se menilo az ~30-40 ns po sestupne hrane SCK,
//   protoze hranu bylo treba nejdriv zachytit synchronizatorem).
//   Ted:
//     * RX: MOSI se vzorkuje NABEZNOU hranou SCK a CRC i dulezite bajty
//       (MAGIC, VER, TYPE, SEQUENCE, payload 0..2, CRC) se zpracuji ZA LETU,
//       jak bajty prichazeji -> aplikace ma vysledek hned po CS nahoru (driv
//       ~13 us cteni + CRC v 10 MHz). RX RAM proto neni potreba. Hodnoty jsou
//       po CS nahoru staticke; aplikace si je prevezme do ~1 us (L-0120 --
//       mazani pred doctenim -- uz nemuze nastat, nic se nemaze).
//     * TX: MISO se meni SESTUPNOU hranou SCK z bajtoveho registru; dalsi bajt
//       se predcita z TX RAM (cteci port taktovany SCK). Bajt 0 je VZDY MAGIC
//       0xA5 -> je v registru natvrdo uz pri CS dole, pred prvni hranou SCK se
//       z RAM nic cist nemusi. Aplikace sklada do DRUHE poloviny TX RAM a
//       prepina `tx_half` az po dokonceni ramce vcetne CRC; PHY polovinu
//       zamyka po dobu transakce (`tx_base`) -> na drat nikdy nejde
//       rozpracovany ramec.
//     * Prechod mezi domenami (SCK <-> 10 MHz aplikace) delaji dvouportove
//       blokove RAM; ridici bity (tx_base, rx_half) jsou po dobu transakce
//       staticke. Konec ramce (CS nahoru) a stav "busy" se hlidaji v domene
//       100 MHz (`clk`), jako dosud.
//   Citace bitu SCK domeny nuluje asynchronne puls `clr` (domena clk, ~30 ns po
//   CS dole, tedy pred prvni hranou SCK); po CS nahoru drzi hodnotu, takze je
//   diagnostika muze precist.
//
// Adresa RAM: {polovina, index bajtu[6:0]} = 8 bitu (256 x 8 = 1 blok BSRAM).
// ============================================================

module spi_slave_phy (
    input  wire        clk,          // clk_p0_100m: CS/stav/konec ramce
    input  wire        clk_app,      // clk_ref_10m: aplikacni porty RAM

    input  wire        sck_pin,
    input  wire        cs_pin,       // active LOW
    input  wire        mosi_pin,
    output wire        miso,

    // TX: aplikace pise ramec do TX RAM (domena clk_app)
    input  wire        tx_half,      // polovina, kterou ma PHY vysilat (z aplikace)
    input  wire        tx_we,
    input  wire [7:0]  tx_waddr,
    input  wire [7:0]  tx_wdata,
    output reg         tx_base,      // polovina, kterou PHY vysila / vysle (domena clk)
    output wire        busy,         // CS dole (domena clk)

    // RX: vysledek posledniho ramce (SCK domena, staticke po CS nahoru)
    output reg  [7:0]  rx_b0, rx_b1, rx_b2,   // MAGIC, VER, TYPE
    output reg  [31:0] rx_seq,                // bajty 4..7 (LE)
    output reg  [7:0]  rx_p0, rx_p1, rx_p2,   // payload 12..14
    output reg  [15:0] rx_crc_calc,           // CRC-16/CCITT-FALSE bajtu 0..125
    output reg  [15:0] rx_crc_recv,           // bajty 126..127 (LE)
    output reg         frame_end_tgl,// toggle pri CS nahoru (konec ramce)

    // diagnostika posledniho ramce (latch pri CS nahoru, domena clk)
    output reg  [15:0] dbg_mosi_cnt, // pocet zmen MOSI mezi sousednimi bity
    output reg  [15:0] dbg_sck_cnt   // pocet nabeznych hran SCK (spravne 1024)
);

    localparam [7:0] MAGIC = 8'hA5;

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

    // ---------------- SCK domena: RX ----------------
    reg [10:0] rcnt = 11'd0;         // pocet nabeznych hran v ramci
    reg [6:0]  rsh  = 7'd0;          // predchozich 7 bitu bajtu
    reg        mprev = 1'b0;
    reg [15:0] mcnt = 16'd0;
    always @(posedge sck_pin or posedge clr) begin
        if (clr) begin
            rcnt <= 11'd0;
            mcnt <= 16'd0;
        end else begin
            if (rcnt != 11'd1024) rcnt <= rcnt + 11'd1;
            if (rcnt != 11'd0 && mosi_pin != mprev) mcnt <= mcnt + 16'd1;
        end
    end
    always @(posedge sck_pin) begin
        rsh   <= {rsh[5:0], mosi_pin};
        mprev <= mosi_pin;
    end

    // zpracovani bajtu za letu (na 8. nabezne hrane bajtu)
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
    wire       rbyte_done = (rcnt[2:0] == 3'd7) && (rcnt[10] == 1'b0);
    wire [7:0] rbyte      = {rsh, mosi_pin};
    wire [6:0] rk         = rcnt[9:3];
    always @(posedge sck_pin or posedge clr) begin
        if (clr) rx_crc_calc <= 16'hFFFF;
        else if (rbyte_done && rk < 7'd126) rx_crc_calc <= crc16_step(rx_crc_calc, rbyte);
    end
    always @(posedge sck_pin) begin
        if (rbyte_done) case (rk)
            7'd0:   rx_b0             <= rbyte;
            7'd1:   rx_b1             <= rbyte;
            7'd2:   rx_b2             <= rbyte;
            7'd4:   rx_seq[7:0]       <= rbyte;
            7'd5:   rx_seq[15:8]      <= rbyte;
            7'd6:   rx_seq[23:16]     <= rbyte;
            7'd7:   rx_seq[31:24]     <= rbyte;
            7'd12:  rx_p0             <= rbyte;
            7'd13:  rx_p1             <= rbyte;
            7'd14:  rx_p2             <= rbyte;
            7'd126: rx_crc_recv[7:0]  <= rbyte;
            7'd127: rx_crc_recv[15:8] <= rbyte;
            default: ;
        endcase
    end

    // ---------------- SCK domena: TX ----------------
    reg [10:0] tcnt = 11'd0;         // pocet sestupnych hran v ramci
    reg [7:0]  cur  = MAGIC;         // vysilany bajt (MSB = MISO); bajt 0 = MAGIC natvrdo
    reg [7:0]  tx_rdata;
    always @(negedge sck_pin or posedge clr) begin
        if (clr) begin
            tcnt <= 11'd0;
            cur  <= MAGIC;
        end else begin
            tcnt <= tcnt + 11'd1;
            if (tcnt[2:0] == 3'd7) cur <= tx_rdata;          // hranice bajtu: dalsi bajt
            else                   cur <= {cur[6:0], 1'b0};
        end
    end
    assign miso = cur[7];

    // TX RAM: zapis clk_app, cteni SCK (nabezna hrana; adresa = NASLEDUJICI bajt,
    // takze na sestupne hrane hranice bajtu je v tx_rdata uz pripraven)
    reg [7:0] txram [0:255];
    always @(posedge clk_app)
        if (tx_we) txram[tx_waddr] <= tx_wdata;
    always @(posedge sck_pin)
        tx_rdata <= txram[{tx_base, tcnt[9:3] + 7'd1}];

    // ---------------- konec ramce, polovina TX (domena clk) ----------------
    initial begin
        tx_base = 1'b0; frame_end_tgl = 1'b0;
        dbg_mosi_cnt = 16'd0; dbg_sck_cnt = 16'd0; tx_rdata = 8'd0;
        rx_b0 = 8'd0; rx_b1 = 8'd0; rx_b2 = 8'd0; rx_seq = 32'd0;
        rx_p0 = 8'd0; rx_p1 = 8'd0; rx_p2 = 8'd0; rx_crc_calc = 16'hFFFF; rx_crc_recv = 16'd0;
    end
    always @(posedge clk) begin
        // polovina TX sleduje aplikaci jen v klidu; po dobu transakce drzena
        if (!cs_active) tx_base <= half_s[2];
        if (cs_rise) begin
            frame_end_tgl <= ~frame_end_tgl;
            // citace SCK domeny jsou po CS nahoru staticke (do dalsi transakce)
            dbg_mosi_cnt  <= mcnt;
            dbg_sck_cnt   <= {5'd0, rcnt};
        end
    end

endmodule
