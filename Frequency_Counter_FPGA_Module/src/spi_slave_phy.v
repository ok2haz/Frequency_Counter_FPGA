// ============================================================
// File: spi_slave_phy.v
// SPI SLAVE PHY (Mode 0: CPOL=0, CPHA=0), MSB-first, 8N, 128 B/transakce
// (protokol v2 dle FPGA_PROTOCOL_V2_NAVRH.md).
//
// FPGA negeneruje hodiny. SCK/CS/MOSI jsou asynchronní vstupy
// oversamplované systémovými hodinami (clk = clk_ref_10m, 10 MHz),
// hrany SCK/CS se detekují -> robustní vůči metastabilitě a delším drátům.
//
// Mode 0:
//   - MOSI vzorkuj na NÁBĚŽNÉ hraně SCK
//   - MISO měň na SESTUPNÉ hraně SCK (první bit platný už při CS↓)
//
// TX rámec (tx_frame_flat) je stabilní mimo transakci; latchuje se
// do tx_shadow při CS↓ (data zamrzlá aplikací). RX rámec se předá
// při CS↑ přes rx_frame_flat + toggle frame_end_tgl.
//
// Mapování flat: bit[1023] = byte0[7] (MSB), MSB-first, byte0 první.
//
// Pozn. rychlost: oversampling 10 MHz / 3FF sync je spolehlivý do ~2 MHz
//   SCK (rámec 128 B = 1024 bitů -> ~512 µs). Pro SCK 4+ MHz dle v2 návrhu
//   přepoj clk na 100 MHz (clk_p0_100m) - PHY je na hodinách generický,
//   ale pak je nutné ošetřit CDC rámců vůči aplikaci (10 MHz doména).
// ============================================================

module spi_slave_phy (
    input  wire         clk,          // 10 MHz system clock (clk_ref_10m)

    input  wire         sck_pin,
    input  wire         cs_pin,       // active LOW
    input  wire         mosi_pin,
    output reg          miso,

    input  wire [1023:0] tx_frame_flat, // stabilní mimo transakci
    input  wire          tx_valid,      // 0 = rámec se přestavuje: drž poslední celý
    output wire [1023:0] rx_frame_flat, // = rx_shadow, platné po frame_end_tgl
    output reg           frame_end_tgl, // toggle při CS↑ (konec rámce)
    output reg  [10:0]   rx_bit_count   // diagnostika: počet přijatých bitů
);

    // 3-stupňové synchronizéry asynchronních vstupů
    reg [2:0] sck_s  = 3'b000;
    reg [2:0] cs_s   = 3'b111;   // CS idle = HIGH
    reg [2:0] mosi_s = 3'b000;

    always @(posedge clk) begin
        sck_s  <= {sck_s[1:0],  sck_pin};
        cs_s   <= {cs_s[1:0],   cs_pin};
        mosi_s <= {mosi_s[1:0], mosi_pin};
    end

    wire sck_rise = (sck_s[2:1] == 2'b01);
    wire sck_fall = (sck_s[2:1] == 2'b10);

    wire cs_active = ~cs_s[2];
    wire cs_fall   = (cs_s[2:1] == 2'b10); // 1 -> 0 : start rámce
    wire cs_rise   = (cs_s[2:1] == 2'b01); // 0 -> 1 : konec rámce

    reg [1023:0] tx_shadow = 1024'd0;
    reg [1023:0] rx_shadow = 1024'd0;
    reg [10:0]   bit_in    = 11'd0;

    // rx_shadow je stabilní od CS↑ do dalšího CS↓ -> čteme ho přímo (úspora 1024 FF)
    assign rx_frame_flat = rx_shadow;

    initial begin
        miso          = 1'b1;
        frame_end_tgl = 1'b0;
        rx_bit_count  = 11'd0;
    end

    always @(posedge clk) begin
        // ARMED: dokud nezačal shift (bit_in==0), drž TX rámec naložený a
        // první bit (MSB byte0) na MISO. NEZÁVISÍ na zachycení sestupné hrany
        // CS -> funguje i když CS bylo dole už při startu FPGA nebo je drženo
        // staticky (chybějící cs_fall byl původní příčina "FPGA mlčí" / 0xFF).
        // Při tx_valid=0 (aplikace přestavuje payload/CRC) se NEpřelatchovává
        // -> transakce zahájená během přestavby odvysílá poslední KOMPLETNÍ
        // rámec, nikdy mix nového payloadu se starým CRC (audit V3).
        if (bit_in == 11'd0) begin
            if (tx_valid) begin
                tx_shadow <= tx_frame_flat;
                miso      <= tx_frame_flat[1023];
            end else begin
                miso      <= tx_shadow[1023];
            end
            rx_shadow <= 1024'd0;
        end

        if (cs_active) begin
            if (sck_rise && bit_in != 11'd1024) begin
                // vzorkuj MOSI (MSB-first -> shift doleva, nový bit do LSB);
                // hrany nad 1024 ignoruj (jinak by přeshift zkazil rámec)
                rx_shadow <= {rx_shadow[1022:0], mosi_s[2]};
                bit_in    <= bit_in + 11'd1;
            end
            if (sck_fall) begin
                // posuň TX, nová MSB na MISO
                tx_shadow <= {tx_shadow[1022:0], 1'b0};
                miso      <= tx_shadow[1022];
            end
        end

        // konec rámce: zachyť počet bitů, toggle, a reset bit_in -> re-arm
        // pro další rámec (preload se připraví hned, nezávisle na nové hraně).
        if (cs_rise) begin
            rx_bit_count  <= bit_in;
            frame_end_tgl <= ~frame_end_tgl;
            bit_in        <= 11'd0;
        end
    end

endmodule
