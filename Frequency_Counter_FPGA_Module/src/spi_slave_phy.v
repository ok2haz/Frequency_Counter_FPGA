// ============================================================
// File: spi_slave_phy.v
// SPI SLAVE PHY (Mode 0: CPOL=0, CPHA=0), MSB-first, 8N, 128 B/transakce
// (protokol v2 dle FPGA_PROTOCOL_V2_NAVRH.md).
//
// FPGA negeneruje hodiny. SCK/CS/MOSI jsou asynchronní vstupy
// oversamplované systémovými hodinami (parametr `clk`), hrany SCK/CS se
// detekují -> robustní vůči metastabilitě a delším drátům.
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
// 🔴 Pozn. rychlost (2026-10-01): `clk` = clk_p0_100m (100 MHz, bylo
//   clk_ref_10m/10 MHz) -- oversampling/3FF sync spolehlivý do ~20 MHz SCK
//   (bylo ~2 MHz), rezerva ~5x. CDC vůči aplikaci (clk_ref_10m, 10 MHz)
//   řeší top.v: RX směr (frame_end_tgl) má 3-stupňový toggle-sync (fe_s),
//   TX směr (tx_valid) má nový 3-stupňový level-sync (txv_s) + mezistav
//   S_TX_ARM ve spi_app.v, který dává 100 ns rezervy před přepisem
//   tx_b[] -- bez něj by torn rámec (nová data + stará CRC) mohl projít
//   ~30ns oknem zpoždění synchronizeru (3 takty @100MHz). Žádný nový 1024b registr
//   (nevešel by se, registry FPGA byly na 79 %) -- tx_frame_flat zůstává
//   kombinační vodič, bezpečný právě díky té 100 ns rezervě.
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
    // 🔴 2026-10-03: posun TX (`cs_active && sck_fall`) je PREDPOCITAN o takt drive
    // do jednoho registru. Duvod: enable 1024 FF `tx_shadow` ctel cs_s[2] a
    // sck_s[2:1] pres LUT s fanoutem 1024 a po pridani TDC (placeni 84 % FF)
    // mel setup -3,3 ns (routing cs_s_2 -> LUT 4,4 ns + LUT -> FF 3,4 ns).
    // Ekvivalence: v dalsim taktu plati cs_s[2] == cs_s[1] a sck_s[2:1] == sck_s[1:0]
    // dnesniho taktu, takze `tx_shift_r` == (cs_active && sck_fall) PRESNE ve stejnem
    // cyklu jako drive (overeno sim/tb_phy_equiv.sv proti puvodnimu modulu).
    reg tx_shift_r = 1'b0;
    always @(posedge clk) tx_shift_r <= (sck_s[1:0] == 2'b10) & ~cs_s[1];
    // ... a pro enable 1024 FF `tx_shadow` je navic REPLIKOVANY po 8 segmentech
    // (kazdy segment 128 FF ma vlastni registr strobe, `keep` brani sloucení):
    // jediny registr s fanoutem 1024 mel i po predpoctu setup -0,5 ns.

    wire cs_active = ~cs_s[2];
    wire cs_fall   = (cs_s[2:1] == 2'b10); // 1 -> 0 : start rámce
    wire cs_rise   = (cs_s[2:1] == 2'b01); // 0 -> 1 : konec rámce

    wire [1023:0] tx_shadow;                   // slozeno ze 8 segmentu (viz nize)
    reg [1023:0] rx_shadow = 1024'd0;
    reg [10:0]   bit_in    = 11'd0;
    // 🔴 2026-10-02: `rx_done` NAHRAZUJE `bit_in != 11'd1024` jako enable pro
    // rx_shadow (1024 CE/D vstupů) — overeno timing reportem Gowin syntezy
    // (A/B test u win_recip fixu, viz AUDIT_STATUS.md 2026-10-02): nejhorsi
    // setup cesty v celem navrhu jsou `bit_in_6/7_s0/Q -> rx_shadow_*_s1/CE`,
    // az -0,636 ns, 56 koncovych bodu. Priciny: 11bitove srovnani `bit_in !=
    // 1024` zavisi na VSECH bitech bit_in a jeho vysledek musi FANOUTOVAT do
    // 1024 flip-flopu v JEDNOM taktu — syntezer misto jednoho bufferovaneho
    // signalu roztahl cast komparatoru az k jednotlivym cilum (proto je
    // `bit_in[6]`/`[7]`, ne "cely komparator", na kriticke ceste).
    // RESENI: `rx_done` je JEDEN registrovany bit, spocitany PREDEM (kdyz
    // `bit_in==1023`, tedy JESTE PRED poslednim povolenym shiftem) — enable
    // 1024 FF uz necte zive 11 bitu, cte vystup jednoho flip-flopu (Q pin),
    // ktery synteza buferuje/replikuje standardnim zpusobem pro siroky
    // fanout. Chovani je IDENTICKE s puvodnim `bit_in != 1024`: `rx_done`
    // naskoci na 1 PRAVE VE STEJNEM TAKTU, kdy by puvodni vyraz poprve vysel
    // false (bit_in dosahne 1024), takze dalsi shift se zastavi na stejnem
    // miste; `cs_rise` nuluje oboje stejne jako drive.
    reg          rx_done   = 1'b0;
    // 🔴 2026-10-02: `rx_armed` NAHRAZUJE `bit_in == 11'd0` ze stejneho duvodu
    // jako `rx_done` vyse — po oprave rx_done se nejhorsi cesta presunula na
    // `bit_in_0_s0/Q -> tx_shadow_*_s1/CE` (srovnani bit_in==0 gatuje load
    // tx_shadow A nulovani rx_shadow = 2048 FF). Ekvivalence: bit_in se meni
    // JEN v shift bloku (+1 -> vzdy nenulove, max 1024) a v cs_rise (-> 0);
    // rx_armed se meni presne na tech dvou mistech (shift -> 0, cs_rise -> 1,
    // cs_rise textove pozdeji = vyhraje pri soubehu, stejne jako u bit_in).
    // Start: bit_in=0 <=> rx_armed=1. Tedy rx_armed == (bit_in == 0) vzdy.
    reg          rx_armed  = 1'b1;

    // rx_shadow je stabilní od CS↑ do dalšího CS↓ -> čteme ho přímo (úspora 1024 FF)
    assign rx_frame_flat = rx_shadow;

    // TX posuvny registr po 8 segmentech (128 FF). Chovani PRESNE jako puvodni
    // jediny registr: nacteni pri (rx_armed && tx_valid), posuv doleva o 1 bit pri
    // tx_shift (posuv ma prednost, jako v puvodnim poradi prirazeni).
    genvar sg;
    generate
        for (sg = 0; sg < 8; sg = sg + 1) begin : txs
            (* keep = "true" *) reg sh_r = 1'b0;
            reg [127:0] seg = 128'd0;
            always @(posedge clk) begin
                sh_r <= (sck_s[1:0] == 2'b10) & ~cs_s[1];
                if (rx_armed & tx_valid) seg <= tx_frame_flat[128*sg +: 128];
                if (sh_r) begin
                    if (sg == 0) seg <= {seg[126:0], 1'b0};
                    else         seg <= {seg[126:0], tx_shadow[(sg > 0) ? (128*sg - 1) : 0]};
                end
            end
            assign tx_shadow[128*sg +: 128] = seg;
        end
    endgenerate

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
        if (rx_armed) begin   // == (bit_in == 0), registrovane — viz deklarace
            if (tx_valid) begin
                miso      <= tx_frame_flat[1023];
            end else begin
                miso      <= tx_shadow[1023];
            end
            // 🔴 2026-10-03: rx_shadow se tady NESMI nulovat. Aplikace (10 MHz) cte
            // rx_frame_flat (= rx_shadow) ZIVE ~13 us po CS↑ (S_RX_CRC, 126 taktu);
            // nulovani hned po CS↑ (rx_armed=1) jí podsunulo samé nuly -> MAGIC/CRC
            // selhaly VZDY -> FPGA ignorovala vsechny povely STM (ACK, START,
            // SET_CONFIG, CAL) a hlasila rx_crc_error=1. Vada od prvniho commitu.
            // Mazat netreba: rámec o 1024 bitech prepise vsech 1024 bitu posuvem.
        end

        if (cs_active) begin
            if (sck_rise && !rx_done) begin
                // vzorkuj MOSI (MSB-first -> shift doleva, nový bit do LSB);
                // hrany nad 1024 ignoruj (jinak by přeshift zkazil rámec).
                // `rx_done` = registrovaná náhrada `bit_in != 1024`, viz deklarace.
                rx_shadow <= {rx_shadow[1022:0], mosi_s[2]};
                bit_in    <= bit_in + 11'd1;
                rx_armed  <= 1'b0;   // bit_in je od ted nenulove
                // Tento shift je 1024. (bit_in 1023 -> 1024) => od příští hrany
                // už neshiftovat. Široké srovnání tím krmí JEN jeden registr,
                // ne 1024 CE vstupů.
                if (bit_in == 11'd1023) rx_done <= 1'b1;
            end
        end

        // posuň TX, nová MSB na MISO (== cs_active && sck_fall, predpocitano)
        if (tx_shift_r) begin
            miso      <= tx_shadow[1022];
        end

        // konec rámce: zachyť počet bitů, toggle, a reset bit_in -> re-arm
        // pro další rámec (preload se připraví hned, nezávisle na nové hraně).
        if (cs_rise) begin
            rx_bit_count  <= bit_in;
            frame_end_tgl <= ~frame_end_tgl;
            bit_in        <= 11'd0;
            rx_done       <= 1'b0;   // textově ZA shift blokem -> při souběhu s sck_rise vyhraje reset (jako bit_in)
            rx_armed      <= 1'b1;   // totéž: bit_in <= 0 => armed
        end
    end

endmodule
