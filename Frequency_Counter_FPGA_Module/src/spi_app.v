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
//   0x01 base window: 0=100 ms, 1=250 ms (default), 2=1 s, 3=50 ms, 4=500 ms
//   0x02 cal_mode:    0=normal, 1=ring oscilátor místo pin28 (kalibrace)
//   0x03 channel:     0=CH_A (default), 1=CH_B primarni (FW >= 0x041E)
//   0x04 reference:  0=OCXO (default po zapnuti), 1=externi 10 MHz vstup (FW >= 0x0420, caps bit11); pin Ref_Ctrl ridi ref_guard
//
// Rámec (128 B, little-endian vícebajtová pole):
//  0 MAGIC=0xA5 | 1 VER=0x02 | 2 TYPE | 3 FLAGS | 4..7 SEQUENCE
//  8..9 PAYLOAD_LEN=114 | 10..11 RESERVED | 12..125 PAYLOAD | 126..127 CRC16(LE)
//
// DATA payload 0x80 (abs offset; 12..59 je 1:1 s protokolem v1):
//  12 freq_x100000(u64) 20 edge_count(u64) 28 gate_ns(u64) 36 ts(u64)
//  44 channel 45 meas_status 46 error_flags(u32) 50 phase_status
//  51 status2 52..59 freq16_x100000(u64) -- od FW 0x0424 (caps bit13): 52..55 pps_ts (u32, tick 100 MHz
//    pri posledni hrane GPS 1PPS) 56 pps_seq (u8, pocet hran) 57..59 = 0; viz modul pps_cap
//  60..61 fw_version(u16) 62..63 caps(u16) 64 clk_status 65 rez
//  FW >= 0x041F (caps bit10, SAMOKONTROLA): 12..15 cas sestaveni (u32, unix s), 16..19 git (u32: [27:0] hash,
//    [31] = neulozene zmeny) -- na miste freq_x100000, ktere od FW 0x0400 nese vzdy 0. 64 clk_status: bit0 = 100 MHz
//    v poslednim okne hlidace v poradku, bit1 = od nahrani aspon jeden vypadek, bit2 = probiha zotaveni (cekani +
//    rekalibrace po vypadku). 65 = pocet vypadku 100 MHz (saturuje 255). 97/98 = rozdil kontrolniho a merneho
//    pocitani hran okna (int8, saturuje) pro primarni / sekundarni slot; zdrave okno ma |rozdil| <= 1.
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
    parameter REGR_EN = 0,          // 1 = regresni blok (bajty 68..96) je ve ramci platny a CAPS bit7 = 1
    // FW 0x0425 (optimalizace 3, brana CLS <= 85 %): ladici bajty prijmu (abs 66, 67, 107, 112..117, 124, 125) z bring-upu SPI.
    // 0 = posilaji se nuly a synteza registry i citace PHY odstrani (~100 FF); 1 = jako 0x0424.
    parameter DBG_RX  = 0,
    // Identita bitstreamu (FW 0x041F): generuje build.tcl do src/build_id.vh pri KAZDEM sestaveni. FW_VERSION se meni
    // jen rucne, takze dva ruzne bitstreamy mohou hlasit stejne cislo (0x041D: radek vs. unc09) -- tohle je odlisi.
    parameter [31:0] BUILD_TIME = 32'd0,   // unix cas sestaveni [s]; 0 = neznamy (simulace, sestaveni mimo build.tcl)
    parameter [31:0] BUILD_GIT  = 32'd0    // [27:0] zkraceny git hash, [31] = zdroje mely neulozene zmeny
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
    // FW 0x041F: samokontrola (caps bit10)
    input  wire [7:0]  meas_clk_status,      // stav hlidace 100 MHz (zivy), viz hlavicka bajt 64
    input  wire [7:0]  meas_clk_loss,        // pocet vypadku hodin FPGA (zivy)
    input  wire [7:0]  meas_clk_mhz,         // FW 0x0421: kmitocet referencnich hodin FPGA v MHz (abs 99)
    input  wire [7:0]  meas_ccd_p,           // rozdil kontrolniho a merneho pocitani hran, primarni slot (s merenim)
    input  wire [7:0]  meas_ccd_s,           // totez, sekundarni slot
    // FW 0x0424 (caps bit13): GPS 1PPS z pps_cap (domena 100 MHz, cte se zive -- viz pps_cap)
    input  wire [31:0] meas_pps_ts,          // tick 100 MHz v okamziku posledni hrany 1PPS
    input  wire [7:0]  meas_pps_seq,         // pocet hran 1PPS (mod 256)
    // FW 0x0425 (caps bit14, audit 2026-10-20): diagnostika O4 a hlidac koherence hodin
    input  wire [7:0]  meas_bub_p,           // bubliny v okne, primarni slot (patri k mereni -> snimek)
    input  wire [7:0]  meas_bub_s,           // totez, sekundarni slot
    input  wire [7:0]  meas_ovl,             // {ovl_s[3:0], ovl_p[3:0]}: znacky okna pri aktivnim CS
    input  wire [7:0]  meas_coh_st,          // hlidac koherence: bit0 platne, bit1 posledni epocha OK, bit2 od nahrani chyba, bit3 vybrana cesta
    input  wire [7:0]  meas_coh_cnt,         // pocet epoch (102,4 us) s chybou koherence (saturuje 255)

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
    output wire [2:0]    base_win,           // 0=100ms 1=250ms 2=1s 3=50ms 4=500ms
    output wire          chan_sel,           // FW 0x041E: 0 = CH_A primarni, 1 = CH_B primarni
    output wire          ref_sel,            // FW 0x0420: pozadavek na referenci (0 = OCXO, 1 = externi); skutecny stav ridi ref_guard
    // FW 0x0425: SET_CONFIG 0x05 = omezeni ruseni / diagnostika (bit0 maskovani vnitrnich znacek pri CS, vychozi 1;
    // bit1 LED heartbeat na PIN29, vychozi 0 = LED stoji: PIN29 je v BANCE 2 spolu s CH_A/CH_B/1PPS/hodinami)
    output wire          mask_cs_en,
    output wire          led_hb_en
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
    // 🔴 0x0424 -> 0x0425 (2026-10-20, audit E0): oprava N1 (zastarale qd pri odstupu 5 taktu), oprava close+2 (hrany >= 2 takty),
    // mez aliasu podle hradla, diagnostika O4 (bubliny, znacky pri CS; abs 57..59), hlidac koherence 100/10 MHz (abs 10/11),
    // maskovani vnitrnich znacek pri CS a SET_CONFIG 0x05; caps bit14.
    localparam [15:0] FW_VERSION      = 16'h0425;  // bump při KAŽDÉ změně bitstreamu
    // 🔴 0x0423 -> 0x0424 (2026-10-16, STATUS #305): GPS 1PPS (PIN33) -- cas hrany v taktech 100 MHz (hruby citac,
    // bez TDC) a poradi v bajtech 52..56 (caps bit13), vstup smycky GPSDO na STM. Viz modul pps_cap.
    // 🔴 0x0422 -> 0x0423 (2026-10-11): REFERENCNI HODINY FPGA ZPET 125 -> 100 MHz (Si5356 CLK0/1, top.v CLK_MHZ, timing.sdc 10 ns).
    // Na 125 MHz mela casova znacka chybu T/2 a T podle faze hrany (ADEV(0,25 s) 1,4e-8 misto 7e-10) a regrese pocitala spatne
    // (L-0152). RTL zustava (vcetne zmen 0x0421 pro casovani na 8 ns), meni se jen hodiny: bisect, zda vadu dela kmitocet.
    // Jednotka casu T_clk/16384 = 0,6103515625 ps, hlidac hodin ocekava 640 prechodu, abs 99 = 100.
    // 0x0421 -> 0x0422 (2026-10-11): oprava souběhu v win_recip (reference okna se aktualizují o takt později, L-0151), LED FPGA_OK na PIN63,
    // automaticke opakovani kalibrace TDC po selhani (L-0153).
    // 0x0420 -> 0x0421 (2026-10-10): referencni hodiny FPGA 100 -> 125 MHz (v 0x0423 vraceno). Zustava z ni: kmitocet hodin
    // v ramci (abs 99, CAPS bit12) a casovani pro 8 ns (registrovane cesty, 3taktova iterace deleni regrese, drzena casova
    // znacka -- viz timing.sdc, tabulka kalibrace nese hotove {amt, neg}).
    // 🔴 0x041F -> 0x0420 (2026-10-10, STATUS #285): VYBER REFERENCE (SET_CONFIG 0x04, caps bit11). Pin Ref_Ctrl (PIN41)
    // prepina OCXO / externi 10 MHz vstup (prepinac U19). Hlidac `ref_guard` bezi z vnitrniho oscilatoru FPGA, nezavisle
    // na vnejsich hodinach: externi referenci pusti jen kdyz na PIN51 jsou hrany, a kdyz zmizi, SAM prepne zpet na OCXO.
    // Stav v clk_status (abs 64): bit3 = vybrana externi, bit4 = na externim vstupu jsou hrany, bit5 = externi vypadla
    // (automaticky navrat na OCXO; smaze se pozadavkem 0). Po zapnuti je VZDY OCXO.
    // 🔴 0x041E -> 0x041F (2026-10-10, STATUS #282/#283): SAMOKONTROLA (caps bit10). Identita bitstreamu (cas
    // sestaveni + git) v bajtech 12..19; hlidac 100 MHz (bajty 64/65) -- po vypadku nebo zmene kmitoctu reference
    // drzi mereni a po ustaleni znovu kalibruje TDC (driv vypadek 100 MHz, napr. init Si5356 pri resetu STM za behu
    // FPGA, prosel bez zotaveni); kontrolni pocitani hran nezavislym synchronizatorem (bajty 97/98), ktere odhali
    // ztracene/pridane hrany v merne ceste.
    // Regresni blok (68..96) se cte ZIVE z domeny 100 MHz bez handshake -- zamerne: vysledky regr_acc jsou
    // stabilni od konce segmentu B (15G/16) do zacatku segmentu A dalsiho okna (G/16 po nem, >= 3 ms), takze
    // v okamziku skladani ramce noveho mereni se nemeni. Snimek by stal 226 FF (CLS 77 -> 83 %, P&R 2026-10-10).
    // 🔴 0x041D -> 0x041E (2026-10-09): VYBER KANALU (SET_CONFIG 0x03; primarni slot ramce = vybrany kanal,
    // `meas_channel` bajt 44 to hlasi) a PET HRADEL (0x01: 0=100ms 1=250ms 2=1s 3=50ms 4=500ms), echo hradla
    // v bajtu 50 (dřive mrtvy phase_status). CAPS bit9 = podporuje obojí.
    // 🔴 0x041A -> 0x041D (2026-10-09): retez TDC 268 tapu = presne jeden radek CFU, radky vynucene v pins.cst
    // (A R26, B R27). Driv 320 tapu, z toho 52 ve vzdalenem radku. Konfigurace TDC hlasi 268 tapu, dekoder 288.
    // (0x041B/0x041C byly jen ladici buildy.)
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
    // bit12 (0x1000) = abs 99 nese kmitocet hodin FPGA [MHz] (od FW 0x0421; dnes 100)
    // bit13 (0x2000) = bajty 52..56 nesou GPS 1PPS (FW 0x0424)
    localparam [15:0] CAPS            = (REGR_EN != 0) ? 16'hFEE2 : 16'hFF62;   // FW 0x0425: + bit14 (diagnostika E0), bit15 (xm v 1/4096 hrany)   // bit8 = kody TDC v bajtech 68..75   // bit1 SET_CONFIG, bit5 dt_ps, bit6 CAL: hist_k 10 b + konfigurace TDC, bit7 REGR (68..96); bit0 (window stream) ODSTRANEN; bit10 samokontrola (FW 0x041F)

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
    reg [7:0]  h_ccd_p = 8'd0, h_ccd_s = 8'd0;   // FW 0x041F: rozdil kontrolniho pocitani (patri k mereni)
    reg [7:0]  h_bub_p = 8'd0, h_bub_s = 8'd0, h_ovl = 8'd0;   // FW 0x0425: diagnostika okna (patri k mereni)
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
    reg [2:0]  base_win_r = 3'd1;            // default 250 ms
    reg        chan_sel_r = 1'b0;            // default CH_A
    reg        ref_sel_r  = 1'b0;            // default OCXO
    reg        mask_cs_r  = 1'b1;            // FW 0x0425: default maskovat vnitrni znacky pri CS
    reg        led_hb_r   = 1'b0;            // FW 0x0425: default LED heartbeat vypnuty
    assign cal_mode = cal_mode_r;
    assign base_win = base_win_r;
    assign chan_sel = chan_sel_r;
    assign ref_sel  = ref_sel_r;
    assign mask_cs_en = mask_cs_r;
    assign led_hb_en  = led_hb_r;

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
            7'd10: tb = is_cal ? 8'd0 : meas_coh_st;       // FW 0x0425 (caps bit14): hlidac koherence (zivy)
            7'd11: tb = is_cal ? 8'd0 : meas_coh_cnt;
            7'd12: tb = is_cal ? (meas_cal_diag[7:0]) : (BUILD_TIME[7:0]);
            7'd13: tb = is_cal ? (meas_cal_diag[15:8]) : (BUILD_TIME[15:8]);
            7'd14: tb = is_cal ? (meas_cal_diag[23:16]) : (BUILD_TIME[23:16]);
            7'd15: tb = is_cal ? (meas_cal_diag[31:24]) : (BUILD_TIME[31:24]);
            7'd16: tb = is_cal ? (meas_cal_diag[39:32]) : (BUILD_GIT[7:0]);
            7'd17: tb = is_cal ? (meas_cal_diag[47:40]) : (BUILD_GIT[15:8]);
            7'd18: tb = is_cal ? (meas_cal_diag[55:48]) : (BUILD_GIT[23:16]);
            7'd19: tb = is_cal ? (meas_cal_diag[63:56]) : (BUILD_GIT[31:24]);
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
            // FW 0x0424 (caps bit13): 1PPS z GPS -- cas hrany v taktech 100 MHz + poradi, ZIVE z pps_cap
            // (viz modul pps_cap: zadny snimek, STM bere shodu dvou ramcu). Do FW 0x0423 tu byl
            // freq16_x100000, ktery FPGA od 0x0400 posilala jako 0 (STM ho pocita z edges_b/dt_b).
            7'd52: tb = is_cal ? 8'd0 : (meas_pps_ts[7:0]);
            7'd53: tb = is_cal ? 8'd0 : (meas_pps_ts[15:8]);
            7'd54: tb = is_cal ? 8'd0 : (meas_pps_ts[23:16]);
            7'd55: tb = is_cal ? 8'd0 : (meas_pps_ts[31:24]);
            7'd56: tb = is_cal ? 8'd0 : (meas_pps_seq);
            7'd57: tb = is_cal ? 8'd0 : h_bub_p;           // FW 0x0425 (caps bit14): bubliny okna, primarni slot
            7'd58: tb = is_cal ? 8'd0 : h_bub_s;           //   sekundarni slot
            7'd59: tb = is_cal ? 8'd0 : h_ovl;             //   {sekundarni[3:0], primarni[3:0]} znacek pri aktivnim CS
            7'd60: tb = is_cal ? 8'd0 : (FW_VERSION[7:0]);
            7'd61: tb = is_cal ? 8'd0 : (FW_VERSION[15:8]);
            7'd62: tb = is_cal ? 8'd0 : (CAPS[7:0]);
            7'd63: tb = is_cal ? 8'd0 : (CAPS[15:8]);
            7'd64: tb = is_cal ? 8'd0 : (meas_clk_status);
            7'd65: tb = is_cal ? 8'd0 : (meas_clk_loss);
            7'd66: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? d_rx0 : 8'd0);
            7'd67: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? d_rx1 : 8'd0);
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
            7'd97:  tb = is_cal ? 8'd0 : (h_ccd_p);
            7'd98:  tb = is_cal ? 8'd0 : (h_ccd_s);
            7'd99:  tb = is_cal ? 8'd0 : (meas_clk_mhz);
            7'd100: tb = is_cal ? 8'd0 : (meas_tdc_status);
            7'd101: tb = is_cal ? 8'd0 : (h_dt_b[7:0]);
            7'd102: tb = is_cal ? 8'd0 : (h_dt_b[15:8]);
            7'd103: tb = is_cal ? 8'd0 : (h_dt_b[23:16]);
            7'd104: tb = is_cal ? 8'd0 : (h_dt_b[31:24]);
            7'd105: tb = is_cal ? 8'd0 : (h_dt_b[39:32]);
            7'd106: tb = is_cal ? 8'd0 : (h_dt_b[47:40]);
            7'd107: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? d_rx2 : 8'd0);
            7'd108: tb = is_cal ? 8'd0 : (h_edge_b[7:0]);
            7'd109: tb = is_cal ? 8'd0 : (h_edge_b[15:8]);
            7'd110: tb = is_cal ? 8'd0 : (h_edge_b[23:16]);
            7'd111: tb = is_cal ? 8'd0 : (h_edge_b[31:24]);
            7'd112: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? d_ccl : 8'd0);
            7'd113: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? d_cch : 8'd0);
            7'd114: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? d_rcl : 8'd0);
            7'd115: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? d_rch : 8'd0);
            7'd116: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? dbg_mosi_cnt[7:0] : 8'd0);
            7'd117: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? dbg_mosi_cnt[15:8] : 8'd0);
            7'd118: tb = is_cal ? 8'd0 : (h_dt_a[7:0]);
            7'd119: tb = is_cal ? 8'd0 : (h_dt_a[15:8]);
            7'd120: tb = is_cal ? 8'd0 : (h_dt_a[23:16]);
            7'd121: tb = is_cal ? 8'd0 : (h_dt_a[31:24]);
            7'd122: tb = is_cal ? 8'd0 : (h_dt_a[39:32]);
            7'd123: tb = is_cal ? 8'd0 : (h_dt_a[47:40]);
            7'd124: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? dbg_sck_cnt[7:0] : 8'd0);
            7'd125: tb = is_cal ? 8'd0 : ((DBG_RX != 0) ? dbg_sck_cnt[15:8] : 8'd0);
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
            h_ccd_p    <= meas_ccd_p;
            h_ccd_s    <= meas_ccd_s;
            h_bub_p    <= meas_bub_p;
            h_bub_s    <= meas_bub_s;
            h_ovl      <= meas_ovl;
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
                                8'h01: base_win_r <= (rx_p1 <= 8'd4) ? rx_p1[2:0] : 3'd1;
                                8'h02: cal_mode_r <= rx_p1[0];
                                8'h03: chan_sel_r <= rx_p1[0];          // 0 = CH_A, 1 = CH_B
                                8'h04: ref_sel_r  <= rx_p1[0];          // 0 = OCXO, 1 = externi (FW >= 0x0420)
                                8'h05: begin mask_cs_r <= rx_p1[0]; led_hb_r <= rx_p1[1]; end   // FW >= 0x0425
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
module gate_div #(
    parameter CLK_MHZ = 100          // referencni hodiny FPGA: jednotka dt = T_clk/16384 -> gate_ns = dt * 1000 / (CLK_MHZ * 16384)
)(
    input  wire        clk,            // clk_ref_10m
    input  wire        start,          // 1-taktový puls: dt platné
    input  wire [47:0] dt,             // Δt [T_clk/16384]
    output reg  [63:0] gate_ns,
    output reg         valid,          // 1-taktový puls: gate_ns platné
    output wire        busy
);
    assign busy = 1'b0;
    // 100 MHz: ns = dt * 5 / 8192 (10 ns / 16384 = 5/8192 ns). Plati JEN pro CLK_MHZ = 100 (jiny kmitocet = jiny prevod).
    wire [49:0] x5 = {2'b00, dt} + {dt, 2'b00};        // dt * 5
    wire [47:0] g_ns = {11'd0, x5[49:13]};
    initial begin gate_ns = 64'd0; valid = 1'b0; end
    always @(posedge clk) begin
        valid <= start;
        if (start) gate_ns <= {16'd0, g_ns};           // max ~1,6e11 (38 b)
    end
endmodule

// ------------------------------------------------------------
// pps_cap (FW 0x0424, STATUS #305): cas nabezne hrany GPS 1PPS (PIN33) v taktech referencnich hodin.
//   HRUBY citac, BEZ TDC (rozhodnuti 2026-10-16): kvantizace 10 ns (sigma 2,9 ns) je proti sumu
//   1PPS z GPS (~30 ns RMS) zanedbatelna a stoji jen ~43 FF (CLS je ~84 %). Hodiny 100 MHz jsou
//   z OCXO (Si5356), takze pocet taktu mezi dvema 1PPS = kmitocet OCXO proti GPS.
//   `cap` a `seq` se do ramce ctou ZIVE z domeny 100 MHz, bez handshake (jako regresni blok):
//   meni se jen pri hrane 1PPS (1 Hz, bez fixu GPS 10 Hz). Roztrzene cteni (hrana prave behem
//   ~400 ns skladani bajtu 52..56) odhali STM: vzorek prijme az tehdy, kdyz dva po sobe jdouci
//   ramce nesou shodne {seq, cap}. Zpozdeni synchronizatoru (2-3 takty) je konstantni.
// ------------------------------------------------------------
module pps_cap (
    input  wire        clk,            // clk_p0_100m
    input  wire        pin,            // GPS 1PPS (asynchronni vstup)
    input  wire [31:0] tick,           // volnobezny citac taktu clk (mod 2^32 = 42,9 s pri 100 MHz)
    output reg  [31:0] cap,            // tick v okamziku posledni nabezne hrany
    output reg  [7:0]  seq             // pocet zachycenych hran (mod 256)
);
    reg [2:0] s;
    initial begin cap = 32'd0; seq = 8'd0; s = 3'b000; end
    always @(posedge clk) begin
        s <= {s[1:0], pin};
        if (s[1] & ~s[2]) begin
            cap <= tick;
            seq <= seq + 8'd1;
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
    reg        v0 = 1'b0, f0 = 1'b0, cap_q = 1'b0;     // FW 0x0421: stupen 0 (takt znacky)
    reg        v1 = 1'b0, f1 = 1'b0, v2 = 1'b0;
    reg [25:0] dx_r = 26'd0;
    reg [39:0] dt_r = 40'd0;                // rozdil < 2^40 (segment < 0,67 s)
    reg [7:0]  dth_d = 8'd0;
    // FW 0x0421 (casovani pro periodu 8 ns): `act` je REGISTROVANE. seg_a/seg_b chodi z 10 MHz domeny (sa_s/sb_s) a rozvadely se na ~100 CE
    // (x0, t0): cesta mela 3,7 ns vedeni a byla nejhorsi v navrhu (-0,9 ns). Znacka prichazi >= 8 taktu po spusteni
    // (samp_b je tou dobou stabilni) a okraj segmentu se posune o 1 takt (stejne jako jeho synchronizace z 10 MHz).
    reg        act = 1'b0;
    always @(posedge clk) act <= (samp_b ? seg_b : seg_a) & (seg_a | seg_b);

    always @(posedge clk) begin
        // stupen 0 (takt znacky): zachyt prvni znacky segmentu
        v0 <= samp_v & act;
        f0 <= first;
        cap_q <= samp_v & act & first;
        // zachyceni o takt pozdeji (FW 0x0421): samp_ts (= ts_ps) i samp_x (sg_x) jsou drzene, dalsi znacka az za >= 5 taktu
        if (cap_q) begin first <= 1'b0; x0 <= samp_x; t0 <= samp_ts; end
        // stupen 1 (o takt pozdeji): rozdily jen pri znacce. FW 0x0421 (casovani pro periodu 8 ns): samp_ts (= ts_ps, drzena) i samp_x (sg_x)
        // jsou stabilni >= 5 taktu, takze rozdily maji 2 takty (multicycle v timing.sdc). U PRVNI znacky segmentu je t0
        // uz prepsane (rozdil 0) -- ta se stejne zahazuje (f1).
        v1 <= v0;
        f1 <= f0;
        if (v0) begin
            dx_r <= samp_x - x0;
            dt_r <= samp_ts[39:0] - t0[39:0];
        end
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
    // FW 0x0421 (casovani pro periodu 8 ns): iterace delice trva 3 takty (fc0, fc1 = pocitani dsub, fsel = vyber a posun). 26bitovy rozdil
    // `dsub` (rem, nq[67], n) ma tak 2 takty (multicycle v timing.sdc): jeden takt byl pri periode 8 ns nesplnitelny (7,5 ns).
    reg        fc0 = 1'b0, fc1 = 1'b0;
    (* keep = "true" *) reg [7:0] ldx_q = 8'd0;      // nacti delenec x (8 kopii, FW 0x0421: kazda ridi ~9 bitu nq)
    (* keep = "true" *) reg [7:0] ldy_q = 8'd0;      // nacti delenec y
    (* keep = "true" *) reg [7:0] sh_q  = 8'd0;      // vyber + posun (takt s fsel = 1)
    reg [6:0]  cnt = 7'd0;
    reg [24:0] rem = 25'd0;
    // FW 0x0421: odlozeny posledni krok (viz nize), zvlast pro segment A a B a ve 2 KOPIICH (0 = n, xm; 1 = ym, ok):
    // jediny `fin_v` rizeny CE ~110 registru a jeho cesta (5,8 ns vedeni) byla pri periode 8 ns nesplnena.
    reg        finp_a = 1'b0, finp_b = 1'b0;
    (* keep = "true" *) reg [1:0] fin_va = 2'b00;
    (* keep = "true" *) reg [1:0] fin_vb = 2'b00;
    reg [47:0] fin_q = 48'd0;
    reg [47:0] dtr   = 48'd0;                // t0 - ref_ts (kazdy takt; t0 i ref_ts jsou pri nacteni deliteli davno stabilni)
    reg [67:0] nq  = 68'd0;
    reg [25:0] dsub = 26'd0;
    reg [39:0] xq  = 40'd0;
    reg [47:0] y0r = 48'd0;
    wire [24:0] rem_n = {rem[23:0], nq[67]};
    wire        qb    = ~dsub[25];
    // Audit 4 (N3, test T9): x se dělí s rozlišením 1/4096 hrany (dřív 1/256). Nad ~16 MHz se značkuje nepravidelně a zaokrouhlení
    // 1/256 hrany dávalo ~56 ps rms na těžiště (20 MHz) -> fázový záznam i regrese horší než šum značky. Stejný počet kroků děličky.
    wire [67:0] dvx   = {8'd0, sx, 12'd0};
    wire [67:0] dvy   = {sy, 4'd0};

    // dalsi stav ridicich signalu (z registru -> kratke cesty); kopie se registruji ze stejneho vyrazu
    reg  n_ge2 = 1'b0;                                   // n >= 2 (registrovano; n je pri rozhodnuti 5 taktu stabilni)
    wire go_x     = waitf && (wc == 3'd6) && n_ge2;
    // FW 0x0421: `done_sel` je REGISTR (dsel_q): posledni vyber = takt vyberu, kdyz cnt == 67 (cnt se meni jen v taktu vyberu,
    // takze je v taktu fc1 stabilni a dsel_q je v taktu vyberu totez co driv `busy & fsel & lastr`).
    reg  dsel_q = 1'b0;
    wire done_sel = dsel_q;                              // posledni vyber prubehu
    // FW 0x0421: ld_now je JEDEN registr (driv OR dvou, ridil ~10 cest -> wc, busy, fc0, ...)
    (* keep = "true" *) reg ldn_q = 1'b0;
    wire ld_now   = ldn_q;
    wire busy_nx  = ld_now | (busy & ~done_sel);
    wire fc0_nx   = ld_now | (busy & ~done_sel & fsel);
    wire fc1_nx   = busy & ~done_sel & fc0;
    wire fsel_nx  = busy & ~done_sel & fc1 & ~ld_now;
    wire sh_nx    = busy_nx & fsel_nx;

    integer i;
    always @(posedge clk) begin
        // start po konci segmentu: pockat 5 taktu na dobeh pipeline akumulatoru, pak posoudit n
        if ((end_a | end_b) && !busy && !waitf && !ld_now) begin
            for_b <= end_b; waitf <= 1'b1; wc <= 3'd0;
        end
        if (waitf) begin
            wc <= wc + 3'd1;
            if (wc == 3'd6) begin
                waitf <= 1'b0;
                if (!n_ge2) begin if (for_b) ok_b <= 1'b0; else ok_a <= 1'b0; end
            end
        end
        ldn_q <= go_x | (done_sel & ~phase_y);
        ldx_q <= {8{go_x}};
        ldy_q <= {8{done_sel & ~phase_y}};
        busy  <= busy_nx;
        fc0   <= fc0_nx;
        fc1   <= fc1_nx;
        fsel  <= fsel_nx;
        sh_q  <= {8{sh_nx}};
        dtr   <= t0 - ref_ts;
        dsub  <= {1'b0, rem_n} - {2'b00, n};            // kazdy takt; pouzije se jen v taktu vyberu
        dsel_q <= fsel_nx & (cnt == 7'd67);
        n_ge2 <= (n_hi != 8'd0) | (n_md != 8'd0) | (n_lo[7:1] != 7'd0);

        if (ldx_q[0]) begin
            y0r <= (dtr << 4);              // dtr = t0 - ref_ts, registrovano (FW 0x041E; 48b odecitani v jednom taktu bylo kriticke)
            rem <= 25'd0; cnt <= 7'd0; phase_y <= 1'b0;
        end else if (ldy_q[0]) begin
            rem <= 25'd0; cnt <= 7'd0; phase_y <= 1'b1;
        end else if (sh_q[0]) begin
            rem <= dsub[25] ? rem_n : dsub[24:0];
            cnt <= cnt + 7'd1;
        end
        // nq po ctvrtinach, kazda ridena vlastni kopii ridicich signalu
        for (i = 0; i < 68; i = i + 1) begin
            if (ldx_q[i / 9])       nq[i] <= dvx[i];
            else if (ldy_q[i / 9])  nq[i] <= dvy[i];
            else if (sh_q[i / 9])   nq[i] <= (i == 0) ? qb : nq[(i == 0) ? 0 : i - 1];
        end
        if (done_sel & ~phase_y) xq <= {nq[38:0], qb};                 // podil sum(x-x0)*4096/n (audit 4; <= 2^36)
        // FW 0x041E: posledni krok (48bitovy soucet s y0r) se odlozi o takt: podil {nq,qb} se nejdriv ZAREGISTRUJE
        // (`fin_q`), teprve pak se scita. Driv vedla cesta dsub[25] -> qb -> carry chain 48 b -> ym_a/ym_b
        // (nejhorsi cesta navrhu pri nejistote hodin 0,9 ns). n, x0, xq, y0r jsou v tu chvili stabilni.
        finp_a <= done_sel & phase_y & ~for_b;           // takt po done_sel: fin_q uz platne
        finp_b <= done_sel & phase_y &  for_b;
        fin_va <= {2{finp_a}};                           // o dalsi takt: ym = y0r + fin_q ma 2 takty (multicycle v SDC)
        fin_vb <= {2{finp_b}};
        if (done_sel & phase_y) fin_q <= {nq[46:0], qb};
        if (fin_vb[0]) begin n_b <= n; xm_b <= {2'd0, x0, 12'd0} + xq; end   // audit 4: x0*4096 <= 2^38
        if (fin_vb[1]) begin ym_b <= y0r + fin_q; ok_b <= 1'b1; end
        if (fin_va[0]) begin n_a <= n; xm_a <= {2'd0, x0, 12'd0} + xq; end
        if (fin_va[1]) begin ym_a <= y0r + fin_q; ok_a <= 1'b1; end
        // nove okno: stare vysledky neplati
        if (beg_a) ok_a <= 1'b0;
        if (beg_b) ok_b <= 1'b0;
    end
endmodule

module win_recip #(
    parameter REGR = 0           // 1 = FW 0x0411: i vnitrni hrany ve dvou segmentech okna dostanou presny cas (regr_acc)
)(
    input  wire         clk,           // clk_p0_100m
    input  wire         sig_pin,       // FW 0x041F: TYZ vstupni pin, ale pro KONTROLNI pocitani (vlastni synchronizator)
    input  wire         rise_s,        // kazda nabezna hrana, SYNCHRONIZOVANA (tdc_chan, pro pocitani)
    input  wire         trig_ack,      // tato hrana spustila presny cas
    input  wire         ts_valid,      // ~8 taktu po trig_ack: ts_ps platny
    input  wire [47:0]  ts_ps,         // presny cas hrany [T_clk/16384], mod 2^48 (172 s)
    input  wire         gate_tick,     // ~okno puls (sdileny)
    input  wire [8:0]   code_i,        // FW 0x0418: kod TDC prave casovane hrany (tdc_chan.code_o)
    input  wire         hold,          // 1 = kalibrace/neplatny TDC: zahod rozpracovane okno
    input  wire         seg_a,         // REGR: segment A okna aktivni (staticke; mimo okamzik prechodu)
    input  wire         seg_b,         // REGR: segment B okna aktivni
    // FW 0x0425 (audit 2026-10-20): diagnostika O4 a omezeni ruseni
    input  wire [7:0]   alias_lim,     // mez aliasu [pocet gate_tick bez uzavreni]; podle hradla (1 s: 170 < 171,8 s preteceni r_dt)
    input  wire         bub,           // tdc_chan.bub_o: bublina u prave casovane hrany (platne v ts_valid)
    input  wire         cs_act,        // CS SPI aktivni (synchronizovane do clk, uroven)
    input  wire         mask_cs,       // 1 = vnitrni hrany (segmenty) se pri aktivnim CS NEspousteji (rusení SPI se do regrese nedostane)
    output reg  [7:0]   r_bub,         // pocet casovanych hran okna s bublinou (vcetne uzaviraci; saturuje 255)
    output reg  [3:0]   r_ovl,         // pocet casovanych hran okna pri aktivnim CS (saturuje 15)
    output wire         want,          // okno ceka na presny cas pristi hrany
    output wire         want_seg,      // REGR: i vnitrni hrany maji dostat presny cas (do tdc_chan.want_seg)
    output reg  [25:0]  r_periods,
    output reg  [47:0]  r_dt,          // Δt okna [T_clk/16384]
    output reg          r_dt_alias,    // okno bez hran > ~25 s -> r_dt neplatne
    output reg          res_tgl,
    output reg  [8:0]   r_ce, r_cs,     // FW 0x0418: kod uzaviraci a ZAHAJOVACI hrany okna (INL diagnostika/korekce)
    output reg  [7:0]   r_ccd,          // FW 0x041F: kontrolni pocet hran okna minus r_periods (int8, saturuje)
    output wire [23:0]  rg_n_a, rg_n_b,
    output wire [39:0]  rg_xm_a, rg_xm_b,
    output wire [47:0]  rg_ym_a, rg_ym_b,
    output wire         rg_ok_a, rg_ok_b
);
    // 🔴 2026-10-03: okno uzavira PRVNI hrana po gate_tick; ta dostane presny
    // cas (tdc_chan ji zmrazi a dekoduje), hrany uvnitr okna se POCITAJI
    // (rise_c, bez omezeni rychlosti). Uzavírací hrana je zároveň první hranou
    // dalšího okna (gap-free). Hrany, které přijdou za uzavírací hranou během
    // dekódování, patří do NOVÉHO okna (FW 0x041F: count se nuluje 2 takty po uzavreni, viz cl_q2).
    reg        armed  = 1'b0;
    reg        primed = 1'b0;
    reg [25:0] snap   = 26'd0;      // pocet period uzavirane okna
    reg [47:0] ref_ts = 48'd0;
    reg [7:0]  age    = 8'd0;       // gate_tick od posledni uzaviraci hrany
    reg        alias_s = 1'b0;
    // vsechny pulzy se zpozdi o 1 takt (kratke cesty); konzistentne pro count
    // i uzavirani, takze poradi udalosti se nemeni
    reg        rise_q = 1'b0, trig_q = 1'b0, gate_q = 1'b0, hold_q = 1'b0;
    // FW 0x0421 (casovani pro periodu 8 ns): `rise_q` rizeni okna (close/pend/armed/sg_x) je SAMOSTATNA kopie, ostatni `rise_q` je jen pro citani hran
    // (count_lo, cy_q): jediny registr s fanoutem pres ~60 prvku byl nejhorsi cestou (-0,1 ns).
    // FW 0x0421: rise_a/trig_a = prvni registrovy stupen (jen vedeni od TDC), rise_q/trig_q a jejich kopie pro rizeni okna jsou
    // az za nim (blizko spotrebitelum). Merena i kontrolni cesta jsou o takt delsi, tedy stejny rozdil (viz ce3).
    reg rise_a = 1'b0, trig_a = 1'b0;
    (* keep = "true" *) reg rise_k = 1'b0;
    (* keep = "true" *) reg trig_k = 1'b0;       // totez pro `trig_q` (rizeni okna: trig_c, pend, armed)
    // FW 0x041F: kopie hold_q jen pro vystupni registry okna (r_*, res_tgl, primed, ref_ts). hold_q ma fanout
    // pres sto FF a jeho cesta do res_tgl byla po pridani samokontroly nejtesnejsi v navrhu (+0,03 ns).
    (* keep = "true" *) reg hold_o = 1'b0;
    // hrany se pocitaji ze SYNCHRONIZOVANE `rise_s` (tdc_chan: ec z 2. stupne vzorku, +1 takt). `rise_s`
    // uzaviraci hrany prijde 0..2 takty PO jejim `trig_ack`. `pend` ceka na ni: prvni rise_s po trig =
    // uzaviraci hrana (dalsi hrana je >= 3 takty dal, takze zamena nehrozi). Pojistka: po 3 taktech bez
    // rise_s se okno uzavre i tak (nesmi nastat; jinak by okno viselo).
    reg        pend   = 1'b0;
    reg [1:0]  pend_n = 2'd0;
    // citac okna: dolnich 8 b + registrovany prenos do hornich 18 b (kratka cesta);
    // hrany jsou od sebe >= 3 takty, takze preneseny hi je hotovy dřív než se count cte (snap az v close+2)
    reg        cy_q   = 1'b0;
    reg [17:0] count_hi = 18'd0;
    reg [7:0]  count_lo = 8'd0;
    wire [25:0] count = {count_hi, count_lo};
    assign want = armed;

    initial begin
        r_periods = 26'd0; r_dt = 48'd0; r_dt_alias = 1'b0; res_tgl = 1'b0; r_ce = 9'd0; r_cs = 9'd0; r_ccd = 8'd0;
        r_bub = 8'd0; r_ovl = 4'd0;
    end
    // FW 0x0425: citace diagnostiky okna (bubliny, znacky pri aktivnim SPI). Zachyti se SPOLU s vystupem okna (tsc_q), tedy vcetne
    // uzaviraci znacky (jeji ts_valid prijde ~9 taktu po uzavreni, az po vynulovani count v cl_q2).
    reg [7:0]  bubc = 8'd0;
    reg [3:0]  ovlc = 4'd0;

    // ---- FW 0x041F: KONTROLNI pocitani hran (samokontrola) ----
    // Merena cesta = vzorek carry retezu -> dR/dRp -> ec_r -> rise_q -> count_lo/count_hi (s registrovanym
    // prenosem) -> snap -> r_periods. Kontrolni cesta = vlastni 3stupnovy synchronizator TEHOZ pinu, detekce hrany
    // a 10bitovy citac, nulovany na stejnem uzavreni okna; porovnava se se spodnimi 10 bity snap. Pokryje vstup,
    // synchronizaci, detekci hrany, count_lo i ztracene prenosy do count_hi (zmeni bit 8/9; nepozna se jen ztrata
    // presne 4k prenosu v jednom okne). 26 b by zachytilo vse, ale v Gowin se citac s nulovanim rozpadl na retez
    // LUT (-4 ns) a spolu se snimkem regrese zvedl CLS na 83 % (P&R 2026-10-10). Zpozdeni obou cest od pinu je 4 takty, takze
    // zdrave okno ma rozdil 0 (+-1, kdyz hrana padne tesne k hodinove hrane a cesty ji vzorkuji v sousednich
    // taktech -- jedno okno +1, sousedni -1). Vetsi rozdil = merna cesta hrany ztraci nebo pridava: casova chyba
    // v citaci, metastabilita, nebo vstup na hranici prahu, kdy ho kazdy klopny obvod vidi jinak (L-0137). Driv se
    // takova chyba projevila jen spatnym kmitoctem bez jakehokoli priznaku (STATUS #282).
    (* keep = "true" *) reg cp1 = 1'b0;
    reg        cp2 = 1'b0, cp3 = 1'b0, ce1 = 1'b0, ce2 = 1'b0, ce3 = 1'b0;     // ce3 je soucasne s rise_q stejne hrany (FW 0x0421)
    reg [9:0]  ccnt  = 10'd0;           // kontrolni hrany od posledniho uzavreni (vcetne te uzaviraci), mod 1024
    reg        cl_q  = 1'b0, cl_q2 = 1'b0, cl_q3 = 1'b0;
    reg [9:0]  csnap = 10'd0;           // ccnt uzavreneho okna
    reg [9:0]  cdd   = 10'd0;           // csnap - snap, mod 1024 (= se znamenkem)
    reg [7:0]  cdd8  = 8'd0;
    always @(posedge clk) begin
        cp1 <= sig_pin; cp2 <= cp1; cp3 <= cp2;
        ce1 <= cp2 & ~cp3;
        ce2 <= ce1;
        ce3 <= ce2;
    end

    // ---- REGR: rozlisi UZAVIRACI spusteni (armed) od spusteni vnitrni hrany (segment) ----
    wire trig_c = (REGR != 0) ? (trig_k & armed)  : trig_k;      // uzaviraci hrana okna
    wire trig_s = (REGR != 0) & trig_q & ~armed;                  // vnitrni hrana ve segmentu
    reg        sg_pend = 1'b0, sg_b = 1'b0, ts_seg = 1'b0;
    reg [8:0]  code_cur = 9'd0;     // kod posledni uzaviraci hrany (= zacatek dalsiho okna)
    reg [25:0] sg_x    = 26'd0;

    wire close = (trig_c | pend) & (rise_k | (pend & (pend_n == 2'd3)));  // uzaviraci hrana
    // FW 0x041F: pocita se KAZDA hrana vcetne uzaviraci (driv inc = rise_q & ~close a +1 na vystupu). Uzaviraci
    // hrana tak padne do `count` v taktu close a v taktu cl_q se zachyti se `snap`; `close` uz nelezi na ceste
    // rise_q -> count_lo / cy_q, ktera byla nejtesnejsi v navrhu (rezerva 0,08 ns, L-0141).
    wire inc   = rise_q;
    wire ts_close = ts_valid & ~((REGR != 0) & ts_seg);           // presny cas UZAVIRACI hrany
    // FW 0x0421 (casovani pro periodu 8 ns): vystup okna se zachyti o TAKT PO ts_valid z jednoho registru (CE ~120 registru r_*, ref_ts, code_cur:
    // `ts_valid` do nich vedl 5 ns). ts_ps se drzi do dalsi platne znacky a code_i (kod posledni znacky) tez; po uzaviraci hrane
    // dalsi znacka az s dalsim oknem (>= ms), takze o takt pozdejsi zachyceni nic neposune.
    reg        tsc_q = 1'b0;
    // FW 0x0422: reference dalsiho okna (ref_ts, code_cur, primed) se aktualizuji az o takt PO zachyceni vystupu (tsc_q2). Predtim se
    // vystup (r_dt = ts_ps - ref_ts, r_cs = code_cur) a nove reference menily na STEJNE hrane hodin; na krzemiku (podle rozmisteni) vystup
    // videl uz nove hodnoty: dt = 0 a code_end == code_st v kazdem okne (HW 2026-10-10, kanal A). STA to nepoznala.
    reg        tsc_q2 = 1'b0;
    always @(posedge clk) begin
        rise_a <= rise_s;
        trig_a <= trig_ack;
        rise_q <= rise_a;
        rise_k <= rise_a & ~hold;        // = rise_q mimo hold; jina funkce, aby ji synteza nesloucila s rise_q
        trig_q <= trig_a;
        trig_k <= trig_a & ~hold;
        gate_q <= gate_tick;
        hold_q <= hold;
        hold_o <= hold;
        // FW 0x0425: v taktu nulovani (cl_q2) se prenos stareho okna do noveho nesmi propsat (hrana v close+2 viz nize)
        cy_q   <= inc & (count_lo == 8'hFF) & ~hold_q & ~cl_q2;
        if (cy_q) count_hi <= count_hi + 18'd1;
        // FW 0x041F: uzavreni okna se provadi az DVA TAKTY PO `close` (z registru cl_q2): zachyceni `snap` a nulovani
        // citace. Driv je ridil primo kombinacni `close` (trig_q, pend, rise_q) s fanoutem 26 CE + 26 RESET -- prave
        // tyto cesty (rise_q/trig_q -> snap/count) byly nejtesnejsi v navrhu a L-0141 je podezrela z chyb na
        // krzemiku. Uzaviraci hrana se do `count` zapocte v taktu close (viz `inc`); kdyz pritom pretoci count_lo,
        // prenos (cy_q) se do count_hi propise az na konci taktu close+1 -- proto se zachycuje v close+2, ne driv
        // (v close+1 by se prenos ztratil: chyba 256 hran zhruba v kazdem 256. okne). Dalsi hrana prijde nejdriv
        // v close+3 (vstup do ~30 MHz), takze se nic neztrati ani nezapocte dvakrat.
        cl_q  <= close & ~hold_q;
        cl_q2 <= cl_q;
        cl_q3 <= cl_q2;
        // kontrolni pocitani: `ccnt` v taktu cl_q obsahuje i kontrolni dvojce uzaviraci hrany (kdyz prijde o takt
        // pozde, pripadne dalsimu oknu: -1 / +1 v sousednich oknech, pri stale stejnem zpozdeni se vyrusi)
        // (FW 0x0421: bez vetve hold_q -> 0: kazde uzavreni ccnt stejne nastavi a prvni okno po holdu se jen zahaji, nevystupuje)
        if (cl_q)       ccnt <= {9'd0, ce3};
        else            ccnt <= ccnt + {9'd0, ce3};
        if (cl_q)  csnap <= ccnt;
        if (cl_q2) cdd   <= csnap - count[9:0];          // mod 1024; count v tomto taktu = to, co jde do snap
        if (cl_q3) cdd8  <= ((&cdd[9:7]) | ~(|cdd[9:7])) ? cdd[7:0] : (cdd[9] ? 8'h80 : 8'h7F);
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
            bubc     <= 8'd0;
            ovlc     <= 4'd0;
        end else begin
            if (ts_valid) begin                    // kazda casovana hrana (uzaviraci i vnitrni)
                if (bub    && bubc != 8'hFF) bubc <= bubc + 8'd1;
                if (cs_act && ovlc != 4'hF)  ovlc <= ovlc + 4'd1;
            end
            if (gate_q) begin
                armed <= 1'b1;
                if (age != 8'hFF) age <= age + 8'd1;
            end
            if (inc) count_lo <= count_lo + 8'd1;
            if (trig_c & ~rise_k) begin pend <= 1'b1; pend_n <= 2'd0; end
            else if (pend & ~rise_k) pend_n <= pend_n + 2'd1;
            if (trig_c) armed <= 1'b0;          // dalsi trig az po dalsim gate_tick
            if (close) begin
                pend     <= 1'b0;
                pend_n   <= 2'd0;
                armed    <= 1'b0;
            end
            if (cl_q) begin                        // FW 0x041F: o takt pozdeji (fanout `close`); gate_tick v tomto
                alias_s  <= (age >= alias_lim);    // taktu se nezapocte. FW 0x0425: mez podle hradla (driv pevne 250: pri 1 s
                                                   // 250 s > 171,8 s preteceni 48b r_dt -> okno 171,8..250 s se tise zabalilo)
                age      <= 8'd0;
            end
            if (cl_q2) begin                       // dva takty po close (viz vyse)
                snap     <= count;                 // vcetne uzaviraci hrany (zapoctena v taktu close)
                // FW 0x0425 (oprava close+2): hrana, jejiz `inc` padne PRAVE do taktu nulovani, uz patri NOVEMU oknu. Driv nulovani
                // jeji pricteni prepsalo (posledni prirazeni vyhrava) -> nad 33 MHz chybela v casti oken jedna perioda a kontrolni
                // pocitani (|ccd| <= 1) ji propustilo. Ted plati hrany >= 2 takty od sebe (fyzicka mez ~40-45 MHz dana vzorkovanim).
                count_lo <= {7'd0, inc};
                count_hi <= 18'd0;
            end
            // vnitrni hrana ve segmentu: poradi hrany se zachyti, az ji napocita rise_q (0..1 takt po trig)
            if (trig_s) begin sg_pend <= 1'b1; sg_b <= seg_b; ts_seg <= 1'b1; end
            if (trig_c) ts_seg <= 1'b0;
            if (sg_pend & rise_k) begin sg_x <= count + 26'd1; sg_pend <= 1'b0; end   // count tuto hranu jeste neobsahuje
        end
        // vystup okna: rizeny kopii hold_o (stejna hodnota jako hold_q, viz deklarace)
        tsc_q <= ts_close & ~hold_o;
        tsc_q2 <= tsc_q & ~hold_o;
        if (tsc_q) begin
            if (primed) begin                      // prvni hrana vubec okno neuzavira
                r_periods  <= snap;
                r_dt       <= ts_ps - ref_ts;
                r_dt_alias <= alias_s;
                res_tgl    <= ~res_tgl;
                r_ce       <= code_i;
                r_cs       <= code_cur;
                r_ccd      <= cdd8;               // hotove 4 takty po close (<= T+6), ts_close je T+7 (T = trig_ack)
                r_bub      <= bubc;               // FW 0x0425 (vcetne uzaviraci znacky: jeji ts_valid byl v predchozim taktu)
                r_ovl      <= ovlc;
            end
            bubc <= 8'd0;                          // dalsi okno zacina (tsc_q je mimo hold, ts_valid ve stejnem taktu nenastane)
            ovlc <= 4'd0;
        end
        if (tsc_q2) begin                          // reference pro dalsi okno az o takt po zachyceni vystupu (viz tsc_q2)
            code_cur <= code_i;
            ref_ts <= ts_ps;
            primed <= 1'b1;
        end
    end

    generate
        if (REGR != 0) begin : rg
            // FW 0x0425: pri aktivnim CS (a mask_cs) se vnitrni hrany nespousti: SPI asynchronni k signalu -> vynechani znacek
            // stredni hodnotu segmentu nevychyli, rušení přenosem se do regrese nedostane (ztrata ~0,2 % znacek)
            assign want_seg = (seg_a | seg_b) & ~(mask_cs & cs_act);
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


// ------------------------------------------------------------
// ref_guard (FW 0x0420): bezpecne prepnuti reference OCXO <-> externi 10 MHz (pin Ref_Ctrl -> prepinac U19).
//   Bezi z VNITRNIHO oscilatoru FPGA (OSC), tedy NEZAVISLE na vnejsich hodinach: vybrana externi reference, ktera
//   zmizi, by jinak zastavila hodiny celeho FPGA (Si5356 CLKIN) a nikdo by uz nemohl prepnout zpet.
//   * ext_present: na ext_sens (PIN51, externi vstup po Schmittu) byly hrany v poslednich ~PRES_TMO taktech osc.
//   * Externi se pusti az po NEPRETRZITE pritomnosti ~STABLE_N taktu (zakmity pri pripojovani kabelu).
//   * Zmizi-li externi, kdyz je vybrana: sel = 0 (OCXO) a ext_lost = 1 (sticky) -- dalsi pokus az po sel_req = 0.
//   * Po zapnuti FPGA je sel = 0 (a pin Ref_Ctrl ma navic externi pulldown 10k) => OCXO.
//   Kmitocet externiho signalu se NEkontroluje (jen pritomnost hran): musi to byt 10 MHz.
// ------------------------------------------------------------
module ref_guard #(
    parameter [7:0]  PRES_TMO = 8'd250,   // ~8 us pri 31,25 MHz: bez hrany po tuto dobu = externi chybi
    parameter [9:0]  STABLE_N = 10'd512   // ~16 us nepretrzite pritomna, nez se externi pusti
)(
    input  wire osc_clk,       // vnitrni oscilator (OSC), nezavisly na vnejsich hodinach
    input  wire ext_sens,      // PIN51: externi reference (asynchronni)
    input  wire sel_req,       // pozadavek z aplikace (domena 10 MHz, kvazistaticky)
    output wire ref_ctrl,      // PIN41: 1 = externi, 0 = OCXO
    output reg  ext_present,
    output reg  ext_lost
);
    reg [2:0] es = 3'b000;
    reg [1:0] rq = 2'b00;
    reg [7:0] tmo = 8'd0;
    reg [9:0] stab = 10'd0;
    reg       sel = 1'b0;
    initial begin ext_present = 1'b0; ext_lost = 1'b0; end
    always @(posedge osc_clk) begin
        es <= {es[1:0], ext_sens};
        rq <= {rq[0], sel_req};
        if (es[2] ^ es[1]) tmo <= PRES_TMO;
        else if (tmo != 8'd0) tmo <= tmo - 8'd1;
        ext_present <= (tmo != 8'd0);
        if (!ext_present)            stab <= 10'd0;
        else if (stab != STABLE_N)   stab <= stab + 10'd1;
        if (!rq[1]) begin
            sel      <= 1'b0;
            ext_lost <= 1'b0;
        end else if (!ext_lost) begin
            if (sel && !ext_present)                  begin sel <= 1'b0; ext_lost <= 1'b1; end
            else if (!sel && stab == STABLE_N)        sel <= 1'b1;
        end
    end
    assign ref_ctrl = sel;
endmodule


// ============================================================
// FW 0x0425 (audit 2026-10-20, slabina 4): PRISNY HLIDAC KOHERENCE 100 MHz <-> 10 MHz.
// Obe hodiny jsou z tehoz OCXO (100 MHz = Si5356 z 10 MHz), takze mezi dvema preklopenimi bitu `tgl10` (preklapi se kazdy takt
// 10 MHz = kazdych 100 ns) musi byt PRESNE 10 taktu 100 MHz. Stavajici hlidac (640 +- 4 za 102,4 us) vidi jen hrube chyby;
// tenhle pozna jediny chybejici / navic pulz 100 MHz, zakmit 10 MHz a fazovy skok >= 10 ns (validace 2026-10-20: skok 5-10 ns
// jen kdyz prekroci vzorkovaci okamzik OBOU cest, skok 6 ns v modelu nezachycen; mensi skoky nejsou cilem).
// Metastabilita: pomer fazi obou hodin je PEVNY, takze synchronizator muze sedet trvale na prechodu tgl10 a davat 9/11 misto 10.
// Proto DVE cesty: vzorkovani nabeznou a sestupnou hranou 100 MHz (posun 5 ns); obe zaroven marginalni byt nemohou. V epose
// 1024 preklopeni (102,4 us) se spocitaji odchylky (interval != 10) v kazde ceste; skutecna chyba se projevi v OBOU (deterministicka
// cesta ji vidi vzdy), metastabilita jen v jedne -> chyba epochy = min(odchylky_P, odchylky_N) > 0. Citac chybnych epoch roste
// nejvys o 1 za epochu -> GRAY kod, bezpecny prenos do 10 MHz domeny (top.v).
// Cena: ~35 FF, ~35 LUT (odchylky 3b). Nezachyti: jitter a skoky < 10 ns spolehlive, pomale putovani fazi.
// ============================================================
module coh_mon (
    input  wire       clk,        // clk_p0_100m
    input  wire       tgl10,      // z domeny clk_ref_10m: preklapi se kazdy jeji takt
    input  wire       en,         // 1 = hodiny OK (mimo vypadek / zotaveni)
    output reg  [7:0] err_gray,   // pocet epoch s chybou, GRAY kod (saturuje na binarnich 255)
    output reg        ep_ok,      // posledni ukoncena epocha bez chyby
    output reg        err_seen,   // od nahrani aspon jedna chybna epocha
    output reg        path_n,     // cesta s mene odchylkami v posledni epose (0 = nabezna, 1 = sestupna)
    output reg        valid       // aspon jedna epocha dokoncena od povoleni
);
    // cesta P: nabezna hrana
    reg p1 = 1'b0, p2 = 1'b0, p3 = 1'b0;
    // cesta N: sestupna hrana, pak dva stupne nabeznou (n1 -> n2 ma pul periody = 5 ns, jen vodic)
    reg n1 = 1'b0, n2 = 1'b0, n3 = 1'b0;
    always @(negedge clk) n1 <= tgl10;
    always @(posedge clk) begin p1 <= tgl10; p2 <= p1; p3 <= p2; n2 <= n1; n3 <= n2; end
    wire tr_p = p2 ^ p3;
    wire tr_n = n2 ^ n3;

    reg [3:0]  ip = 4'd0, in = 4'd0;          // takty od posledniho preklopeni (saturuji na 15 = vypadek preklapeni)
    reg        pp = 1'b0, pn = 1'b0;          // prvni interval po povoleni se nehodnoti
    reg [2:0]  dp = 3'd0, dn = 3'd0;          // odchylky v aktualni epose (saturuji na 7; staci >= 1 a porovnani)
    reg [9:0]  ep = 10'd0;                    // preklopeni cesty P v epose
    reg [7:0]  err_bin = 8'd0;
    reg        ef_v = 1'b0, ef_p = 1'b0, ef_n = 1'b0;   // P&R 2026-10-20: zaregistrovany konec epochy a priznaky odchylek
    initial begin err_gray = 8'd0; ep_ok = 1'b0; err_seen = 1'b0; path_n = 1'b0; valid = 1'b0; end
    // odchylky VCETNE preklopeni v tomto taktu (aby se odchylka na konci epochy neztratila)
    wire       dev_p = tr_p & pp & (ip != 4'd10);
    wire       dev_n = tr_n & pn & (in != 4'd10);
    wire [2:0] dp_x  = (dev_p && dp != 3'd7) ? dp + 3'd1 : dp;
    wire [2:0] dn_x  = (dev_n && dn != 3'd7) ? dn + 3'd1 : dn;
    wire       ep_end = tr_p & (&ep);
    wire [7:0] err_inc = err_bin + 8'd1;
    always @(posedge clk) begin
        if (!en) begin
            ip <= 4'd0; in <= 4'd0; pp <= 1'b0; pn <= 1'b0; dp <= 3'd0; dn <= 3'd0; ep <= 10'd0; valid <= 1'b0; ef_v <= 1'b0;
        end else begin
            if (tr_p) begin ip <= 4'd1; pp <= 1'b1; end else if (ip != 4'hF) ip <= ip + 4'd1;
            if (tr_n) begin in <= 4'd1; pn <= 1'b1; end else if (in != 4'hF) in <= in + 4'd1;
            if (tr_p) ep <= ep + 10'd1;
            // P&R 2026-10-20: rozhodnuti konce epochy ve DVOU taktech. V taktu ep_end se jen zaregistruji priznaky (odchylky obou
            // cest vcetne preklopeni v tomto taktu), zapocteni do citace o takt pozdeji. V jednom taktu cesta n2 -> dn_x -> CE
            // err_gray mela rezervu 0,001 ns (podle rozmisteni i zaporna).
            ef_v <= ep_end;
            if (ep_end) begin
                ef_p <= (dp_x != 3'd0); ef_n <= (dn_x != 3'd0);
                path_n <= (dn < dp);           // z registru (bez odchylky tohoto taktu) -- diagnosticky bit
            end
            if (ef_v) begin                       // konec epochy (1024 preklopeni cesty P = 102,4 us), o takt pozdeji
                ep_ok  <= ~ef_p | ~ef_n;
                valid  <= 1'b1;
                if (ef_p && ef_n) begin           // chyba videna OBEMA cestami = skutecna
                    err_seen <= 1'b1;
                    if (err_bin != 8'hFF) begin
                        err_bin  <= err_inc;
                        err_gray <= err_inc ^ (err_inc >> 1);
                    end
                end
            end
            if (ep_end) begin
                dp <= 3'd0; dn <= 3'd0;
            end else begin
                dp <= dp_x; dn <= dn_x;
            end
        end
    end
endmodule
