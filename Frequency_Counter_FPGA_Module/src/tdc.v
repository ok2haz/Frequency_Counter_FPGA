// ============================================================
// File: tdc.v  --  skutecny carry-chain TDC (2026-10-03, F-0201/F-0203)
//
// Nahrazuje `carry_tdc` z top.v, ktery syntéza zredukovala na invertor
// (v netlistu nebyla jediná ALU bunka). Tady je retez z PRIMITIV `ALU`
// (instancovanych primo), takze ho syntéza nemuze zjednodusit.
//
// PRINCIP
//   * Asynchronni vstup jde do CIN prvniho ALU. Kazdy ALU je zapojen jako
//     pruchod carry: I0=1, I1=0 => COUT = CIN, SUM = ~CIN. Hrana se tak
//     sirí retezem rychlosti ~57 ps/tap (STA model GW1NR-9C, zmereno na
//     zkusebnim projektu: 256 tapu = 14,6 ns; na krzemiku bude nerovnomerne
//     a rychleji/pomaleji -> proto kalibrace).
//   * Kazdy tap vzorkuje FF na hrane clk_p0 (100 MHz). Vzorek je teplomerovy
//     kod: thermo[i] = 1, pokud hrana uz tap i prosla. Kod = pocet jednicek
//     od indexu 0 (0..255; 255 = nasyceni, hrana starsi nez retez).
//   * Cas hrany = tick(vzorku) - cal[kod]; cal[] je KALIBRACNI TABULKA
//     (BRAM 256 x 14 b; hodnoty 0..16383 = zpozdeni hrana -> takt).
//   * 🔴 JEDNOTKA CASU = T_clk/16384 = 0,6103515625 ps (10 ns / 2^14), ne ps:
//     tick pricita 16384 za takt a kalibrace je pouhy posun (zadne nasobeni;
//     nasobeni 5000 v 100MHz fabricu nesplnovalo casovani). 1 s = 1,6384e12
//     jednotek, pretekani 48 b po 172 s. Prevod: ps = jednotky * 625 / 1024.
//
// 🔴 PRECIZNI CAS JEN PRO HRANU NA HRANICI OKNA
//   V tomto fabricu je budget ~3 urovne LUT na takt 100 MHz (routing 2-3 ns
//   na prechod), dekoder 256 tapu jednim taktem nejde. Reciproky citac ale
//   potrebuje presny cas JEN dvou hran okna (prvni hrana po gate_tick uzavre
//   okno a zaroven otevre dalsi); hrany uvnitr okna se jen POCITAJI.
//   Proto:
//     * hrube pocitani: t0 (vzorek tapu 0) -> rise_c = nabezna hrana, kazdy
//       takt, bez omezeni;
//     * presny cas: pri `want & rise_c` se vzorky q ZMRAZI (clock enable) na
//       5 taktu, dekoder (kombinacni, mnoho urovni) ma 5 taktu na vyhodnoceni
//       (SDC multicycle q -> code_r), pak cteni kalibracni tabulky.
//   Dusledek: mrtva doba presneho casu 5 taktu (50 ns) -- nevadi, presny cas
//   se bere jednou za okno. Λ/Ω regrese (cas kazde hrany) tim zanika; stejne
//   ji STM nikdy necetl.
//
// KALIBRACE (code density, cela ve FPGA, bez ucasti STM)
//   Ring oscilator (asynchronni k clk_p0) krmi retez; kazda jeho hrana se
//   dekoduje stejnou cestou (zmrazeni + dekoder) a zapocte do histogramu.
//   Po 2^CAL_LOG2 udalostech:
//     cal[k] = 16384 * (soucet hist[j<k] + hist[k]/2) / N      [T_clk/16384]
//   Startuje samo po zapnuti (top.v) a na pozadavek STM (SET_CONFIG 0x02).
//   Behem kalibrace jsou mereni blokovana (cal_busy).
//
// SYMETRIE KANALU
//   Oba kanaly jsou instance TOHOTO modulu: stejna struktura, stejna latence
//   (7 taktu trig -> ts), stejny posun casu => v rozdilech (okno) se kruti.
//   Kazdy kanal ma VLASTNI kalibracni tabulku (zpozdeni tapu se na krzemiku
//   lisi tak, jak P&R retez umisti). Rozdil cest pin -> retez a pristupu
//   hodin se timto NEOVERI ani neodecte (patri k TI A-B, mimo tento krok).
//
// OMEZENI (poctive)
//   * Okno retezu 256 x ~57 ps = ~14,6 ns: v nem smi byt nejvyse JEDNA hrana,
//     tedy perioda vstupu >= ~2 x 14,6 ns => horni mez ~34 MHz (nominalne;
//     na krzemiku dle skutecneho zpozdeni). Nad tim je nutna predelicka.
//   * Pokud je retez na krzemiku RYCHLEJSI nez STA model (~57 ps), nemusi
//     pokryt 10 ns. Kalibrace to ukaze: d_ovf (udalosti za koncem retezu)
//     musi byt ~0 (STM: CAL report; bit4/5 tdc_status = retez kratky).
//   * Rare outlier: hrana presne na vzorkovaci hrane se muze detekovat o takt
//     pozdeji (kod ~175); cas vychazi shodne, chyba kalibrace horniho konce.
//   * Teplotni drift zpozdeni tap: tabulka plati pro teplotu kalibrace;
//     STM ma kalibraci opakovat (SET_CONFIG 0x02 = 1).
//   * NEOVERENO NA KRZEMIKU. Overeno: Icarus (sim/tb_tdc.sv) s behavioralnim
//     modelem ALU s nahodnym zpozdenim na tap, syntéza + P&R + timing report.
// ============================================================

// ------------------------------------------------------------
// tdc_chain: ALU retez + vzorkovaci FF.
//   thermo_o[i] = 1 <=> hrana tap i prosla (ZMRAZENO, kdyz cap_en = 0)
//   t0          = vzorek tapu 0 BEZ zmrazeni (hrube pocitani hran)
// ------------------------------------------------------------
module tdc_chain #(
    parameter TAPS = 256
)(
    input  wire             clk,       // clk_p0_100m
    input  wire             sig,       // asynchronni vstup (po vyberu zdroje)
    input  wire             cap_en,    // 0 = zmrazit vzorky q
    output wire [TAPS-1:0]  thermo_o,
    output reg              t0
);
    wire [TAPS-1:0] s;

    // Kazdy stupen ma VLASTNI skalarni vodice (g[i].co) misto jednoho vektoru:
    // v simulaci (Icarus) jinak kazda zmena jednoho bitu preslo vsech 256 portu.
    genvar i;
    generate
        for (i = 0; i < TAPS; i = i + 1) begin : g
            wire co;
            wire su;
            if (i == 0) begin : h
                // I0=1, I1=0: COUT = CIN (pruchod), SUM = ~CIN
                ALU #(.ALU_MODE(0)) u (.I0(1'b1), .I1(1'b0), .I3(1'b0),
                                       .CIN(sig), .COUT(co), .SUM(su));
            end else begin : h
                ALU #(.ALU_MODE(0)) u (.I0(1'b1), .I1(1'b0), .I3(1'b0),
                                       .CIN(g[i-1].co), .COUT(co), .SUM(su));
            end
            assign s[i] = su;
        end
    endgenerate

    (* keep = "true" *) reg [TAPS-1:0] q = {TAPS{1'b1}};   // q = ~thermo
    always @(posedge clk) if (cap_en) q <= s;
    initial t0 = 1'b0;
    always @(posedge clk) t0 <= ~s[0];                      // = sig vzorkovany
    assign thermo_o = ~q;
endmodule


// ------------------------------------------------------------
// ring_osc: LUT oscilator (asynchronni zdroj pro kalibraci). 1 NAND + 12
// invertoru = 13 inverzi (licho). Vystup = kruh / (2^DIV_LOG2 az +1 obeh,
// LFSR) -- aby v retezu byla vzdy jen jedna hrana a udalosti neleze na mrizce.
// ------------------------------------------------------------
module ring_osc #(
    parameter DIV_LOG2 = 4   // vystup = kruh / 2^DIV_LOG2 (default /16; sim zrychluje)
)(
    input  wire en,
    output wire out
);
    (* keep = "true" *) wire [12:0] n;
    // n[0] = NAND(en, n[12])
    LUT2 #(.INIT(4'b0111)) u0 (.I0(en), .I1(n[12]), .F(n[0]));
    genvar i;
    generate
        for (i = 1; i < 13; i = i + 1) begin : r
            LUT1 #(.INIT(2'b01)) u (.I0(n[i-1]), .F(n[i]));
        end
    endgenerate

    // Delic s PSEUDONAHODNYM modulem: pulperioda vystupu = 2^(DIV_LOG2-1) nebo
    // +1 obeh kruhu (LFSR). Bez toho by udalosti ve vzorkovanem okne lezely na
    // mrizce (cely pocet obehu x perioda kruhu mod 10 ns) a histogram by mel
    // jen nekolik kodu. LFSR x^16+x^15+x^13+x^4+1 (perioda 65535).
    reg [DIV_LOG2-1:0] cnt  = {DIV_LOG2{1'b0}};
    reg [15:0]         lfsr = 16'hACE1;
    reg                o    = 1'b0;
    wire [DIV_LOG2-1:0] lim = ({{(DIV_LOG2-1){1'b0}}, 1'b1} << (DIV_LOG2-1))
                              - {{(DIV_LOG2-1){1'b0}}, 1'b1} + {{(DIV_LOG2-1){1'b0}}, lfsr[0]};
    always @(posedge n[12]) begin
        if (cnt >= lim) begin
            cnt  <= {DIV_LOG2{1'b0}};
            o    <= ~o;
            lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[14] ^ lfsr[12] ^ lfsr[3]};
        end else begin
            cnt <= cnt + {{(DIV_LOG2-1){1'b0}}, 1'b1};
        end
    end
    assign out = o;
endmodule


// ------------------------------------------------------------
// tdc_chan: kanal = retez + zmrazeni + dekoder + kalibrace + presny cas
//   Rozhrani k oknu (win_recip):
//     rise_c   : kazda nabezna hrana (hrube, 1 takt)
//     want     : okno ceka na presny cas pristi hrany
//     trig_ack : tato hrana (rise_c) spustila presny cas (jen kdyz want)
//     ts_valid : 7 taktu po trig_ack: ts_ps = cas teto hrany [ps]
//   ts_ps = tick_ps(cyklus m+6) - cal[kod]; posun je stejny pro VSECHNY
//   presne casy obou kanalu, v rozdilech (okno) se kruti.
// ------------------------------------------------------------
module tdc_chan #(
    parameter CAL_LOG2 = 20         // kalibrace z 2^CAL_LOG2 udalosti (1M: ~1/4 casu, chyba bunky ~1 ps)
)(
    input  wire        clk,         // clk_p0_100m
    input  wire        sig_raw,     // asynchronni vstup kanalu
    input  wire        ro,          // asynchronni kalibracni zdroj (z ring_osc)
    input  wire [47:0] tick_ps,     // volnobezny cas [T_clk/16384], +16384 za takt
    input  wire        want,        // okno: pristi hrana ma dostat presny cas
    input  wire        cal_req,     // 1-taktovy pulz: spust kalibraci
    input  wire        cal_abort,   // 1-taktovy pulz: timeout kalibrace (top.v)

    output wire        rise_c,
    output wire        trig_ack,
    output reg         ts_valid,
    output reg  [47:0] ts_ps,           // [T_clk/16384]
    output wire        use_ro,      // 1 = retez krmi ring oscilator
    output wire        cal_busy,
    output reg         cal_valid,   // tabulka platna
    output reg         cal_fail,    // posledni kalibrace selhala (timeout)
    // diagnostika (platna po dokonceni kalibrace)
    output reg [31:0]  d_ovf,       // hist[255]: udalosti za koncem retezu
    output reg [31:0]  d_peak,      // nejvetsi hist[k]
    output reg [15:0]  d_nz,        // pocet neprazdnych kodu (<= 256)
    output reg [15:0]  d_last       // nejvyssi neprazdny kod
);
    localparam TAPS = 256;
    localparam HW   = CAL_LOG2 + 1;           // sirka citace kodu (0..2^CAL_LOG2)

    initial begin
        ts_valid = 1'b0; ts_ps = 48'd0; cal_valid = 1'b0; cal_fail = 1'b0;
        d_ovf = 32'd0; d_peak = 32'd0; d_nz = 16'd0; d_last = 16'd0;
    end

    // ---------------- stav kalibrace (ONE-HOT, kazda cesta <= 1-2 LUT) -----
    //   pri 100 MHz je v tomto fabricu budget ~3 urovne LUT (routing 2-3 ns/hop),
    //   proto zadne dekodovani binarniho stavu v ridicich cestach.
    reg s_clr = 1'b0, s_col = 1'b0, s_drn = 1'b0, s_cmp = 1'b0;
    reg [9:0] ph = 10'd1;                     // one-hot faze (DRN/CMP)
    assign use_ro   = s_col;
    assign cal_busy = s_clr | s_col | s_drn | s_cmp;
    wire sig_eff = s_col ? ro : sig_raw;

    // ---------------- zmrazeni vzorku ----------------
    reg        idle_r = 1'b1;                 // 1 = vzorky se nezmrazuji
    reg  [2:0] fz = 3'd0;                     // 1..4 = zmrazeno
    reg        arm_r = 1'b0;                  // pri kalibraci nebo want (lag 1 takt)
    wire [TAPS-1:0] th;
    wire       t0;
    reg        t0p = 1'b0;
    always @(posedge clk) begin
        t0p   <= t0;
        arm_r <= s_col | want;
    end
    assign rise_c = t0 & ~t0p;

    wire trig_ok  = t0 & ~t0p & arm_r & idle_r;           // 1 LUT
    assign trig_ack = trig_ok & ~s_col;
    wire cap_en   = idle_r & ~(t0 & ~t0p & arm_r);        // 1 LUT, fanout 256 CE

    always @(posedge clk) begin
        if (trig_ok) begin
            fz     <= 3'd1;
            idle_r <= 1'b0;
        end else if (!idle_r) begin
            if (fz == 3'd4) begin fz <= 3'd0; idle_r <= 1'b1; end
            else            fz <= fz + 3'd1;
        end
    end

    tdc_chain #(.TAPS(TAPS)) u_chain (.clk(clk), .sig(sig_eff), .cap_en(cap_en),
                                      .thermo_o(th), .t0(t0));

    // ---------------- dekoder (kombinacni, 5 taktu: SDC multicycle) -------
    // kod = pocet uvodnich jednicek teplomeru (bere se prvni nula); 8 bloku
    // po 32 tapech: hruby index z prvniho neuplneho bloku + jemny z nej.
    function [5:0] lead32;                    // pocet uvodnich jednicek (0..32)
        input [31:0] v;
        integer j;
        begin
            lead32 = 6'd32;
            for (j = 31; j >= 0; j = j - 1) if (!v[j]) lead32 = j[5:0];
        end
    endfunction

    reg [7:0] pk;
    reg [5:0] ld;
    integer   pg;
    always @* begin
        pk = 8'd255;                          // vsechny bloky prosly -> nasyceni
        ld = 6'd0;
        for (pg = 7; pg >= 0; pg = pg - 1) begin
            ld = lead32(th[32*pg +: 32]);
            if (ld != 6'd32) pk = {pg[2:0], ld[4:0]};
        end
    end

    reg [7:0] code_r = 8'd255;
    reg       dv1 = 1'b0, dv2 = 1'b0;
    wire      fz4 = (fz == 3'd4);
    always @(posedge clk) begin
        dv1 <= fz4;                           // dekoder dokoncen na konci fz==4
        dv2 <= dv1;
        if (fz4) code_r <= pk;                // q zmrazene >= 5 taktu (MCP 5)
    end

    // ---------------- kalibracni tabulka + histogram (BRAM) --------------
    reg [13:0]    lut  [0:255];
    reg [HW-1:0]  hist [0:255];
    integer li;
    initial begin
        // nominalni 57 ps/tap (= 93,4 jednotek T/16384), dokud neprobehne
        // kalibrace (jen orientacne)
        for (li = 0; li < 256; li = li + 1)
            lut[li] = (li * 93 + 46 > 16383) ? 14'd16383 : (li * 93 + 46);
    end

    reg  [13:0]   lut_q   = 14'd0;
    reg  [HW-1:0] hist_rd = {HW{1'b0}};
    reg  [HW-1:0] hist_wd_r = {HW{1'b0}};
    reg           hist_we_r = 1'b0;
    reg  [7:0]    scan_k  = 8'd0;
    reg  [8:0]    clr_k   = 9'd0;
    reg  [HW-1:0] cnt     = {HW{1'b0}};
    reg           cnt_hit = 1'b0;             // cnt == CNT_LAST (registrovane)
    reg  [13:0]    lutv   = 14'd0;             // hodnota tabulky (saturovana)

    wire [7:0]    hist_ra = s_cmp ? scan_k : code_r;
    wire          hist_we = s_clr | hist_we_r;
    wire [7:0]    hist_wa = s_clr ? clr_k[7:0] : code_r;
    wire [HW-1:0] hist_wd = s_clr ? {HW{1'b0}} : hist_wd_r;
    wire          lut_we  = s_cmp & ph[4];
    wire [7:0]    lut_wa  = scan_k;
    wire [13:0]   lut_wd  = lutv;

    always @(posedge clk) begin
        lut_q     <= lut[code_r];
        hist_rd   <= hist[hist_ra];
        hist_wd_r <= hist_rd + {{(HW-1){1'b0}}, 1'b1};
        hist_we_r <= dv2 & s_col;
        if (lut_we)  lut[lut_wa]   <= lut_wd;
        if (hist_we) hist[hist_wa] <= hist_wd;

        // presny cas hrany (7 taktu od trig_ack)
        ts_ps    <= tick_ps - {34'd0, lut_q};
        ts_valid <= dv2 & cal_valid & ~cal_busy;
    end

    // faze: jedno-horka, mimo DRN/CMP drzena v 1; po ph[9] se sama otoci na ph[0]
    // (nezavisla na cal_req -> kratka cesta)
    always @(posedge clk) ph <= (s_drn | s_cmp) ? {ph[8:0], ph[9]} : 10'd1;

    // ---------------- radic kalibrace (jen ridici registry) ---------------
    localparam [HW-1:0] CNT_LAST = {1'b1, {(HW-1){1'b0}}} - {{(HW-1){1'b0}}, 1'b1};
    always @(posedge clk) begin
        cnt_hit <= (cnt == CNT_LAST);         // cnt se meni jen pri udalosti (>= 8 taktu)
        if (cal_req) begin
            s_clr <= 1'b1; s_col <= 1'b0; s_drn <= 1'b0; s_cmp <= 1'b0;
            clr_k     <= 9'd0;
            cnt       <= {HW{1'b0}};
            cal_valid <= 1'b0;
            cal_fail  <= 1'b0;
        end else begin
            if (s_clr) begin                      // vynuluj histogram (256 taktu)
                clr_k <= clr_k + 9'd1;
                if (clr_k == 9'd255) begin s_clr <= 1'b0; s_col <= 1'b1; end
            end
            if (s_col) begin                      // sbirej udalosti
                if (dv2) begin
                    cnt <= cnt + {{(HW-1){1'b0}}, 1'b1};
                    if (cnt_hit) begin            // 2^CAL_LOG2. udalost
                        s_col <= 1'b0; s_drn <= 1'b1;
                    end
                end
                if (cal_abort) begin              // RO nejde -> selhani (timeout z top.v)
                    cal_fail  <= 1'b1;
                    cal_valid <= 1'b1;            // degradovany rezim: nominalni LUT (57 ps/tap), mereni nestoji
                    s_col     <= 1'b0;
                end
            end
            if (s_drn) begin                      // dobehne zapis posledni udalosti
                if (ph[9]) begin
                    s_drn <= 1'b0; s_cmp <= 1'b1; scan_k <= 8'd0;
                end
            end
            if (s_cmp) begin                      // 256 kodu x 10 taktu
                if (ph[9]) begin
                    if (scan_k == 8'd255) begin
                        s_cmp     <= 1'b0;
                        cal_valid <= 1'b1;
                    end else begin
                        scan_k <= scan_k + 8'd1;
                    end
                end
            end
        end
    end

    // ---------------- vypocet tabulky (datova cesta, bez cal_req) ---------
    //   cal[k] = 16384 * (cum_pred + hist[k]/2) / N  =  (2*cum_pred + hist[k]) >> (LOG2-13)
    //   (N = 2^CAL_LOG2; jednotka T_clk/16384 -> ZADNE nasobeni, jen posun)
    //   10 taktu na kod, kazdy takt jedna kratka operace:
    //   ph1 hcur<=hist_rd | ph2 xv,cum | ph3 hnz,hgt,lutv | ph4 lut zapis | ph5 diag
    reg [HW:0]    cum  = {(HW+1){1'b0}};
    reg [HW+1:0]  xv   = {(HW+2){1'b0}};
    reg [HW-1:0]  hcur = {HW{1'b0}};
    reg           hnz  = 1'b0, hgt = 1'b0;
    reg  [8:0]    nzc  = 9'd0;                // pocet neprazdnych kodu (<= 256)
    wire [HW+1:0] xsh  = xv >> (CAL_LOG2 - 13);        // 0..16384
    always @(posedge clk) begin
        if (s_clr) begin
            d_ovf <= 32'd0; d_peak <= 32'd0; d_nz <= 16'd0; d_last <= 16'd0; nzc <= 9'd0;
        end
        if (s_drn) cum <= {(HW+1){1'b0}};
        if (s_cmp & ph[1]) hcur <= hist_rd;               // hist_rd = hist[scan_k]
        if (s_cmp & ph[2]) begin
            xv  <= {cum, 1'b0} + {2'b00, hcur};
            cum <= cum + {1'b0, hcur};
        end
        if (s_cmp & ph[3]) begin
            hnz  <= (hcur != {HW{1'b0}});
            hgt  <= ({{(32-HW){1'b0}}, hcur} > d_peak);
            lutv <= (xsh > 16383) ? 14'd16383 : xsh[13:0];   // saturace (x = 2N)
        end
        if (s_cmp & ph[5]) begin                          // diagnostika
            if (hnz) begin
                d_nz   <= {7'd0, nzc + 9'd1};
                nzc    <= nzc + 9'd1;
                d_last <= {8'd0, scan_k};
                if (hgt) d_peak <= {{(32-HW){1'b0}}, hcur};
            end
            if (scan_k == 8'd255) d_ovf <= {{(32-HW){1'b0}}, hcur};
        end
    end
endmodule
