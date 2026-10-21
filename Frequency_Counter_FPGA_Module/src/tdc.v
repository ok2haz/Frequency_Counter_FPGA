`timescale 1ps/1ps   // simulace: zpozdeni (SIM_*) jsou v ps; bez toho by dedila default 1 s podle poradi souboru
// ============================================================
// File: tdc.v  --  carry-chain TDC (2026-10-03, F-0201/F-0203; architektura 2026-10-07, FW 0x0410)
//
// PRINCIP
//   * Asynchronni vstup jde do CIN prvniho ALU. Kazdy ALU je zapojen jako pruchod carry
//     (I0=1, I1=0 => COUT = CIN, SUM = ~CIN), hrana se retezem sirí ~39 ps/ALU (kremik).
//   * Kazdy STRIDE-ty ALU vzorkuje FF `q` na hrane clk_p0 (100 MHz); thermo[i] = 1, pokud hrana
//     uz tap i prosla. Kod = pocet uvodnich jednicek (= pozice PRVNI nuly).
//   * Cas hrany = tick - cal[kod]; cal[] je KALIBRACNI TABULKA z code density (ring oscilator,
//     cela ve FPGA). Jednotka casu = T_clk/16384 = 0,6103515625 ps; ps = jednotky * 625 / 1024.
//
// 🔴 ARCHITEKTURA SPOUSTENI (2026-10-07) -- proc a co se zmenilo
//   Mereni na desce (docs/audit/2026-10-07_tdc-vlastni-reference.md) a simulace kremikoveho modelu
//   (sim/tb_tdc_inl.sv) ukazaly PRICINU obriho binu (kod 134 = 14 % hran u A, 48 % u B):
//     retez fyzicky konci na ~268. ALU (10,45 ns) a samostatny spoustec `t0` (FF za invertorem,
//     jinde na cipu nez vzorky q) videl hranu az o a = 1,87 ns (A) / ~5,3 ns (B) POZDEJI nez vzorky.
//     Hrana s tau < a se proto spustila o takt pozdeji (tau + 10 ns) a spadla za konec retezu.
//     Sirka obriho binu = a + 10 - 10,45 ns: A 1,42 ns (zmereno 1,435), B 4,85 ns (zmereno 4,78).
//   Oprava = ODSTRANIT slepotu spoustece. Neni zadny samostatny `t0`:
//     q   : vzorky V MISTE ALU, volne bezi kazdy takt (zadny CE, kratke cesty, 1. stupen synchronizace)
//     qd  : KOMPAKTNI kopie q (2. stupen), CE = zmrazeni; dekoder cte qd
//     dR  : 2. stupen TEHOZ vzorku q[KD] -> detekce hrany ec = ~dR & dRp, stejny cas jako ostatni tapy
//   Slepota je tak jen poloha tapu KD (~0,16 ns), ne 1,87/5,3 ns. Pocitani hran (rise_s) a spousteni
//   pouzivaji tentyz ec (uz po 2 FF -> metastabilita nejde do citace).
//   Volitelne DUAL=1: druha sada vzorku na SESTUPNOU hranu hodin (q_f, qd_f); tau je pak <= 5 ns a
//   retez musi pokryt jen 5 ns + slepotu (rezerva na teplotu/P&R). Kalibrace jedna, tabulka {sada, kod}.
//
// OMEZENI (poctive)
//   * v retezu smi byt nejvyse JEDNA hrana: vstup do ~30 MHz.
//   * tabulka plati pro teplotu kalibrace (STM opakuje, SET_CONFIG 0x02).
//   * NEOVERENO NA KRZEMIKU. Overeno: Icarus (sim/tb_tdc.sv, tb_tdc_inl.sv se silicon modelem,
//     tb_dec_equiv.sv), syntéza + P&R.
// ============================================================

// ------------------------------------------------------------
// tdc_chain: ALU retez + vzorkovaci FF + detekce hrany.
//   thermo_r/f[i] = 1 <=> hrana tap i prosla; ZMRAZENE (qd), kdyz ld = 0
//   ec            = nova hrana videna v tomto vzorku (z 2. stupne, NEzmrazuje se)
//   ecf           = (DUAL) ta hrana byla poprve videt v sade F (sestupna hrana)
// ------------------------------------------------------------
module tdc_chain #(
    parameter TAPS   = 256,   // pocet VZORKOVANYCH tapu (sirka kodu)
    parameter STRIDE = 2,     // ALU na jeden vzorkovany tap (delka retezu = NTAP*STRIDE)
    parameter KD     = 2,     // tap pro detekci hrany (vzorek q[KD] -> dR)
    parameter DUAL   = 0,     // 1 = druha sada vzorku na sestupnou hranu hodin
    // Fyzicky postavenych tapu (<= TAPS). Tapy NTAP..TAPS-1 nemaji ALU a hlasi se jako "hrana uz prosla":
    // dekoder (nasobek 32) tak najde prvni neproslou pozici uvnitr skutecneho retezu, a kdyz hrana
    // dojde az na konec, je teplomer plny = kod "za koncem". Duvod: do jednoho radku CFU GW1NR-9
    // se vejde 270 ALU = hlava + 268 tapu + konec (docs/audit/2026-10-09_tdc-3kanaly-rozmisteni.md).
    parameter NTAP   = TAPS
)(
    input  wire             clk,       // clk_p0_100m
    input  wire             sig,       // asynchronni vstup (po vyberu zdroje)
    input  wire             ld,        // 1 = qd nacte q; 0 = zmrazit
    input  wire             trig,      // 1 takt: spusteni presneho casu (zapamatuje sadu, ve ktere byla hrana videt)
    output wire [TAPS-1:0]  thermo_r,
    output wire [TAPS-1:0]  thermo_f,
    output wire             ec,
    output wire             qd_src_o   // sada zmrazeneho vzorku: 0 = R, 1 = F (jen DUAL)
);
    localparam NALU = NTAP * STRIDE;
    wire [TAPS-1:0] s;
    genvar p;
    generate
        for (p = NTAP; p < TAPS; p = p + 1) begin : pad
            assign s[p] = 1'b0;                 // q = 0 = hrana prosla (viz NTAP)
        end
    endgenerate

    // Kazdy stupen ma VLASTNI skalarni vodice (g[i].co) misto jednoho vektoru:
    // v simulaci (Icarus) jinak kazda zmena jednoho bitu preslo vsech portu.
    // syn_keep: stupne s NEPOUZITYM SUM jsou jen pruchod carry (COUT = CIN)
    // a syntéza by je jinak smela nahradit vodicem -> retez by se zkratil.
    genvar i;
    generate
        for (i = 0; i < NALU; i = i + 1) begin : g
            wire co;
            wire su;
            if (i == 0) begin : h
                // I0=1, I1=0: COUT = CIN (pruchod), SUM = ~CIN
                (* syn_keep = 1 *)
                ALU #(.ALU_MODE(0)
`ifdef SIM_IDX
                      , .IDX(i)
`endif
                     ) u (.I0(1'b1), .I1(1'b0), .I3(1'b0),
                          .CIN(sig), .COUT(co), .SUM(su));
            end else begin : h
                (* syn_keep = 1 *)
                ALU #(.ALU_MODE(0)
`ifdef SIM_IDX
                      , .IDX(i)
`endif
                     ) u (.I0(1'b1), .I1(1'b0), .I3(1'b0),
                          .CIN(g[i-1].co), .COUT(co), .SUM(su));
            end
            // vzorkuje se jen kazdy STRIDE-ty stupen; PRIMO ze skalaru `su` --
            // mezivektor (s_all[NALU]) Icarus prepocitaval pri KAZDE zmene bitu pro
            // vsechny cteni => simulace ~100x pomalejsi (2026-10-03).
            if ((i % STRIDE) == 0) begin : t
                assign s[i / STRIDE] = su;
            end
        end
    endgenerate

    // ---- sada R (nabezna hrana): q volne, qd zmrazitelna kopie ----
    (* keep = "true" *) reg [TAPS-1:0] q_r  = {TAPS{1'b1}};     // q = ~thermo
    reg [TAPS-1:0] qd_r = {TAPS{1'b1}};
    reg            dR   = 1'b1;                                  // 2. stupen vzorku q_r[KD]
    always @(posedge clk) begin
        q_r <= s;
        dR  <= q_r[KD];
        if (ld) qd_r <= q_r;
    end
    assign thermo_r = ~qd_r;

    // ---- sada F (sestupna hrana): q_f vzorkuje o pul taktu pozdeji; do posedge domeny jde
    //      FF -> FF (pul taktu, zadna logika). Registry jsou na urovni modulu (ne v generate bloku)
    //      a zmrazene se jmenuji `qd_*`, aby jeden vzor v timing.sdc (u_chain/qd_*) nasel neco VZDY
    //      (i pri DUAL=0, kde se qd_f/qd_src odstrani a jmeno v SDC by jinak bylo chyba TA2003). ----
    (* keep = "true" *) reg [TAPS-1:0] q_f  = {TAPS{1'b1}};
    reg [TAPS-1:0] qd_f   = {TAPS{1'b1}};
    reg            dF     = 1'b1, dFp = 1'b1;
    reg            dRp    = 1'b1;
    reg            qd_src = 1'b0;                                // zmrazena sada (nastavi se pri trig)
    always @(negedge clk) if (DUAL != 0) q_f <= s;
    always @(posedge clk) begin
        dRp <= dR;
        dF  <= q_f[KD];
        dFp <= dF;
        if (ld && DUAL != 0) qd_f <= q_f;
        if (trig) qd_src <= newF;
    end
    // poradi vzorku (od nejstarsiho): F(m-1)=dFp, R(m)=dR, F(m)=dF
    wire newR = ~dR & dFp;                // poprve videna v R(m)
    wire newF = ~dF & dR;                 // poprve videna v F(m)
    assign ec       = (DUAL != 0) ? (newR | newF) : (~dR & dRp);   // q = 1 = hrana jeste neprosla
    assign qd_src_o = (DUAL != 0) ? qd_src : 1'b0;
    assign thermo_f = (DUAL != 0) ? ~qd_f : {TAPS{1'b0}};
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
            // FW 0x041F: pojistka proti stavu 0. Tyhle registry taktuje sam kruh, a ten se pri zapnuti a vypnuti
            // (`en`) muze zakmitnout zkracenym pulzem -> registry se mohou nastavit libovolne. Ve stavu 0 by XOR
            // LFSR zustal navzdy (`cal_req` ho nenastavuje), delic by mel pevny modul, udalosti kalibrace by lezely
            // na mrizce a tabulka TDC by byla spatne az do dalsiho nahrani FPGA -- tedy vada "jen nekdy po nahrani".
            lfsr <= (lfsr == 16'd0) ? 16'hACE1 : {lfsr[14:0], lfsr[15] ^ lfsr[14] ^ lfsr[12] ^ lfsr[3]};
        end else begin
            cnt <= cnt + {{(DIV_LOG2-1){1'b0}}, 1'b1};
        end
    end
    assign out = o;
endmodule


// ------------------------------------------------------------
// tdc_dec: dekoder teplomeru "pocet uvodnich jednicek" (= pozice PRVNI nuly), KOMBINACNI ale
// strukturovany: (1) blok plny = AND32, (2) index prvniho neuplneho bloku, (3) vyber toho
// bloku, (4) pozice prvni nuly v nem. Ekvivalentni puvodni funkci vcetne bublin a nasyceni
// (sim/tb_dec_equiv.sv, 40 000 nahodnych vektoru), ale ~3x mene LUT nez puvodni zapis
// (res_breakdown: 730 LUT na kanal -> ~250). Vstup je ZMRAZENY (>= 5 taktu), takze cesta
// qd -> code_r ma v SDC multicycle (viz timing.sdc); registrovane stupne by stouply FF.
// ------------------------------------------------------------
module tdc_dec #(
    parameter TAPS = 256                        // nasobek 32
)(
    input  wire [TAPS-1:0]           th,        // 1 = hrana tap uz prosla
    output wire [$clog2(TAPS)-1:0]   code
);
    localparam NB = TAPS / 32;                 // nejvyse 16 bloku (512 tapu)
    localparam PW = $clog2(NB);
    localparam CW = $clog2(TAPS);

    wire [NB-1:0] full_c;
    genvar gb;
    generate
        for (gb = 0; gb < NB; gb = gb + 1) begin : bf
            assign full_c[gb] = &th[32*gb +: 32];
        end
    endgenerate

    // 🔎 2026-10-07: zkouseno i prepsani prioritnich kodéru na binarni strom puleni (lowzero32/16): LUT NEklesly
    // (Logic 3829 proti 3758) -- dominuje vyber bloku (mux 10:1 x 32 bitu), ne priorita. Proto smycky.
    reg [PW-1:0] pg_c;
    integer j;
    always @* begin
        pg_c = {PW{1'b0}};
        for (j = NB - 1; j >= 0; j = j - 1) if (!full_c[j]) pg_c = j[PW-1:0];   // nejnizsi neuplny blok
    end

    wire [31:0] blk = th[32*pg_c +: 32];
    reg  [4:0]  ld_c;
    integer i;
    always @* begin
        ld_c = 5'd31;
        for (i = 31; i >= 0; i = i - 1) if (!blk[i]) ld_c = i[4:0];              // nejnizsi nula
    end

    assign code = (&full_c) ? {CW{1'b1}} : {pg_c, ld_c};
endmodule

// ------------------------------------------------------------
// tdc_chan: kanal = retez + zmrazeni + dekoder + kalibrace + presny cas
//   Rozhrani k oknu (win_recip):
//     rise_c   : kazda nabezna hrana (hrube, 1 takt)
//     rise_s   : tataz hrana o takt pozdeji (pro POCITANI; uz po 2 FF, tedy bez metastability)
//     want     : okno ceka na presny cas pristi hrany
//     trig_ack : tato hrana (rise_c) spustila presny cas (jen kdyz want)
//     ts_valid : ~8 taktu po trig_ack: ts_ps = cas teto hrany [T_clk/16384]
//   ts_ps = tick(cyklus spusteni + const) - V[kod]; posun je stejny pro VSECHNY presne casy obou
//   kanalu, v rozdilech (okno) se kruti. U DUAL=1 je V pro sadu R = T + tau, pro sadu F = w_lo + tau
//   (cumulativni poradi R pred F), viz tabulka nize -- tedy casy z obou sad jsou na jedne ose.
// ------------------------------------------------------------
module tdc_chan #(
    parameter EXT_TR = 0,             // optimalizace 5 (2026-10-20): 1 = tr1/tr2 z top (sdilene obema kanaly, -68 FF)
    parameter CAL_LOG2 = 20,        // kalibrace z 2^CAL_LOG2 udalosti (1M: ~1/4 casu, chyba bunky ~1 ps)
    parameter TAPS     = 256,       // vzorkovanych tapu (nasobek 32)
    parameter STRIDE   = 2,         // ALU na vzorek
    parameter KD       = 2,         // tap detekce hrany
    parameter DUAL     = 0,         // 1 = i sestupna hrana hodin
    parameter NTAP     = TAPS       // fyzicky postavenych tapu (<= TAPS), viz tdc_chain
)(
    input  wire        clk,         // clk_p0_100m
    input  wire        sig_raw,     // asynchronni vstup kanalu
    input  wire        ro,          // asynchronni kalibracni zdroj (z ring_osc)
    input  wire [47:0] tick_ps,     // volnobezny cas [T_clk/16384], +16384 za takt
    input  wire [33:0] tr1_ext,     // optimalizace 5: tick_hi - 1 / - 2 sdilene v top (EXT_TR = 1); jinak nezapojeno (0)
    input  wire [33:0] tr2_ext,
    input  wire        want,        // okno: pristi hrana ma dostat presny cas
    input  wire        want_seg,    // FW 0x0411: i VNITRNI hrany (segmenty okna) maji dostat presny cas
    input  wire        cal_req,     // 1-taktovy pulz: spust kalibraci
    input  wire        cal_abort,   // 1-taktovy pulz: timeout kalibrace (top.v)

    output wire        rise_c,
    output wire        rise_s,      // tataz hrana pro POCITANI (o takt pozdeji, bez metastability)
    output wire        trig_ack,
    output reg         ts_valid,
    output reg  [47:0] ts_ps,           // [T_clk/16384]
    output wire        use_ro,      // 1 = retez krmi ring oscilator
    output wire        cal_busy,
    output reg         cal_valid,   // tabulka platna
    output reg         cal_fail,    // posledni kalibrace selhala (timeout)
    // diagnostika (platna po dokonceni kalibrace)
    output reg [31:0]  d_ovf,       // hist[posledni adresa]: udalosti za koncem retezu (u DUAL: posledni kod sady F)
    output reg [31:0]  d_peak,      // nejvetsi hist[k]
    output reg [15:0]  d_nz,        // pocet neprazdnych kodu
    output reg [15:0]  d_last,      // nejvyssi neprazdny kod (= nejvyssi pozice PRVNI nuly)
    // B1a (2026-10-04): nejvyssi KDY navzorkovany tap za celou kalibraci (od 2026-10-07 s rozlisenim 8 tapu):
    //   d_maxtap ~ d_last  -> teplomer cisty, retez tam fyzicky konci (oprava = delsi/jiny P&R retezu)
    //   d_maxtap >> d_last -> bubliny nad prvni nulou (oprava = vzorkovani/metastabilita)
    output reg [15:0]  d_maxtap,
    // vypis histogramu (diagnostika DNL): mimo kalibraci cte BRAM hist[dump_k]; dump_k je kvazistaticky
    // (nastavi ho STM pozadavkem), adresa = {sada, kod} (u DUAL=0 jen kod)
    input  wire [9:0]  dump_k,
    output wire [23:0] dump_q,
    output wire [$clog2(TAPS)-1:0] code_o,  // FW 0x0418: kod posledni presne casovane hrany (platny v ts_valid)
    output reg         bub_o        // FW 0x0425: bublina = nejvyssi prosly tap (po 8) lezi > 8 tapu ZA kodem (platne v ts_valid)
);
    localparam CW = $clog2(TAPS);             // sirka kodu
    localparam AW = CW + DUAL;                // sirka adresy tabulek ({sada, kod})
    localparam NC = 32'd1 << AW;                  // pocet adres
    localparam HW = CAL_LOG2 + 1;             // sirka citace kodu (0..2^CAL_LOG2)

    initial begin
        ts_valid = 1'b0; ts_ps = 48'd0; cal_valid = 1'b0; cal_fail = 1'b0;
        d_ovf = 32'd0; d_peak = 32'd0; d_nz = 16'd0; d_last = 16'd0; d_maxtap = 16'd0;
        bub_o = 1'b0;
    end

    // ---------------- stav kalibrace (ONE-HOT, kazda cesta <= 1-2 LUT) -----
    //   pri 100 MHz je v tomto fabricu budget ~2-3 urovne LUT (routing 2-3 ns/hop),
    //   proto zadne dekodovani binarniho stavu v ridicich cestach.
    reg s_clr = 1'b0, s_col = 1'b0, s_drn = 1'b0, s_cmp = 1'b0;
    reg [9:0] ph = 10'd1;                     // one-hot faze (DRN/CMP)
    assign use_ro   = s_col;
    // FW 0x0421: cal_busy z REGISTRU (busy_ra = OR ctyr stavu o takt pozdeji): fanout do oken a hold nestihal; o takt pozdejsi
    // nastup/konec hlaseni je bezpecny (cal_valid se meni soucasne se stavem a ts_valid se pri cal_busy neemituje).
    reg  busy_ra = 1'b0;
    assign cal_busy = busy_ra;
    wire sig_eff = s_col ? ro : sig_raw;

    // ---------------- spousteni a zmrazeni vzorku ----------------
    reg        idle_r = 1'b1;                 // 1 = qd se nezmrazuje
    reg  [2:0] fz = 3'd0;                     // 1..4 = zmrazeno
    reg        arm_r = 1'b0;                  // pri kalibraci nebo want (lag 1 takt)
    reg        ec_r  = 1'b0;
    wire [TAPS-1:0] th_r, th_f;
    wire       ec, src_l;
    always @(posedge clk) begin
        arm_r <= s_col | want | want_seg;
        ec_r  <= ec;
    end
    assign rise_c = ec;
    // ec uz je z 2. stupne vzorku (dR), tedy bez metastability: pocitani hran staci o takt zpozdit
    // (pend/trig parovani ve win_recip pocita s rise_s 0..2 takty po trig_ack)
    assign rise_s = ec_r;

    wire trig_ok  = ec & arm_r & idle_r;                  // 1 LUT
    assign trig_ack = trig_ok & ~s_col;
    // FW 0x0425 (oprava N1, audit 2026-10-20): `qd` se smi nacist i v POSLEDNIM zmrazenem taktu (fz == 4). Driv bylo
    // ld = idle_r & ~(ec & arm_r), tedy 0 po cele zmrazeni (fz 1..4). Prvni takt, kdy smi prijit nove spusteni (idle_r = 1),
    // patri ke vzorku z PREDCHOZIHO taktu, ktery se do qd mel nahrat jeste pri idle_r = 0 -> hrana se vzorkem presne 5 taktu za
    // vzorkem predchozi spustene hrany se spustila, ale dekodovala ZMRAZENY vzorek predchozi hrany (chyba az jednotky ns; jen
    // pri spousteni hrana za hranou, tj. segmenty regrese pri ~17..25 MHz). Nacteni qd v taktu fz == 4 je bezpecne: code_r si
    // vysledek zachyti na TEZE hrane (neblokujici prirazeni), multicycle 5 qd -> code_r zustava (qd se meni nejdriv po 5 taktech).
    // ldok je REGISTR, takze `ld` ma dal 4 vstupy (ldok, dR, dRp, arm_r) = 1 LUT jako dosud.
    reg  ldok = 1'b1;                                     // 1 = v tomto taktu smi qd nacitat (klid nebo fz == 4)
    always @(posedge clk) ldok <= ~trig_ok & (idle_r | (fz == 3'd3) | (fz == 3'd4));
    wire ld       = ldok & ~(ec & arm_r);                 // 1 LUT; CE kompaktni kopie qd

    always @(posedge clk) begin
        if (trig_ok) begin
            fz     <= 3'd1;
            idle_r <= 1'b0;
        end else if (!idle_r) begin
            if (fz == 3'd4) begin fz <= 3'd0; idle_r <= 1'b1; end
            else            fz <= fz + 3'd1;
        end
    end

    // sada, ve ktere byla hrana poprve videt (jen DUAL), drzi `qd_src` v retezu (SDC: u_chain/qd_*)
    tdc_chain #(.TAPS(TAPS), .STRIDE(STRIDE), .KD(KD), .DUAL(DUAL), .NTAP(NTAP)) u_chain (
        .clk(clk), .sig(sig_eff), .ld(ld), .trig(trig_ok),
        .thermo_r(th_r), .thermo_f(th_f), .ec(ec), .qd_src_o(src_l));

    wire [TAPS-1:0] th = (DUAL != 0 && src_l) ? th_f : th_r;

    // ---------------- dekoder (tdc_dec) -------------
    // 🔴 2026-10-07 (optimalizace zdroju): puvodni kombinacni dekoder (8 bloku po 32 s prioritou,
    // ~730 LUT na kanal) a diagnostika B1a (~690 LUT) byly dohromady ~70 % vsech LUT celeho navrhu
    // (sim/res_breakdown.py). `tdc_dec` dela totez strukturovane (~250 LUT); kombinacni cesta ma
    // 5 taktu (multicycle, qd je zmrazena), fz == 4 ho nacita.
    wire [CW-1:0] pk;
    tdc_dec #(.TAPS(TAPS)) u_dec (.th(th), .code(pk));

    // B1a: nejvyssi SET tap s rozlisenim 8 tapu (kazdy 8. vzorek): staci na verdikt "retez fyzicky
    // konci" (maxtap 133 vs last 134, 2026-10-07) a stoji ~40 LUT misto ~690.
    reg [CW-4:0] hk8;
    integer      hg;
    always @* begin
        hk8 = {(CW-3){1'b0}};
        // jen skutecne tapy: doplnek NTAP..TAPS-1 je vzdy "prosla" a ukazoval by vzdy konec
        for (hg = 0; hg < (NTAP + 7) / 8; hg = hg + 1) if (th[8*hg]) hk8 = hg[CW-4:0];
    end
    wire [CW-1:0] hk = {hk8, 3'b000};

    reg [CW-1:0] code_r   = {CW{1'b1}};
    reg [CW-1:0] hicode_r = {CW{1'b0}};       // B1a: nejvyssi set tap tohoto eventu
    reg          src_c    = 1'b0;
    assign code_o = code_r;
    reg          dv1 = 1'b0, dv2 = 1'b0;
    reg [CW:0]   c8_r = {(CW+1){1'b0}};       // FW 0x0425: code_r + 8 (bublina, 2 takty)
    wire         fz4 = (fz == 3'd4);
    always @(posedge clk) begin
        dv1 <= fz4;                           // dekoder dokoncen na konci fz==4
        dv2 <= dv1;
        if (fz4) begin code_r <= pk; hicode_r <= hk; src_c <= src_l; end   // qd zmrazene >= 5 taktu (MCP 5)
        // FW 0x0425: bublina (vic hran v retezu / zakmit vstupu): nejvyssi prosly tap (zaokrouhleny dolu na 8) je o vic nez 8 tapu
        // za prvni nulou. Normalne hk <= code - 1. Prepocte se v taktu dv1 (code_r/hicode_r se drzi >= 5 taktu, tedy i v ts_valid).
        // P&R 2026-10-20: soucet a porovnani ve DVOU taktech (v jednom -0,76 ns); bub_o je platne v dv2+1, ts_valid az dv2+3
        if (dv1) c8_r <= {1'b0, code_r} + 10'd8;
        if (dv2) bub_o <= ({1'b0, hicode_r} > c8_r);
    end
    wire [AW-1:0] caddr;
    generate
        if (DUAL != 0) begin : ca
            assign caddr = {src_c, code_r};
        end else begin : ca
            assign caddr = code_r;
        end
    endgenerate

    // ---------------- kalibracni tabulka + histogram (BRAM) --------------
    // Adresa = {sada, kod}; kumulativni poradi pri vypoctu tabulky: nejdriv VSECHNY kody sady R, pak F.
    // Proto je V[F, k] = w_lo + tau_F[k] (w_lo = podil udalosti sady R x T) a V[R, k] = T + tau_R[k]
    // (+T se pridava jednim bitem 14 v `lutv`, viz nize) -- obe sady na jedne case ose bez dalsiho scitani.
    // FW 0x0421: polozka tabulky je HOTOVA pro vypocet casove znacky: {amt[1:0], neg[13:0]}. Cas = tick*16384 - V, V = 16384*b14 + x14
    // (b14 = bit +T), tedy dolnich 14 bitu = -x14 (mod 2^14) = `neg` a horni cast = tick_hi - amt, amt = b14 + (x14 != 0).
    // Pocita se pri kalibraci (ne za behu), takze za BRAM neni zadna aritmetika.
    reg [15:0]    lut  [0:NC-1];
    reg [HW-1:0]  hist [0:NC-1];
    integer li, nx, ng;
    initial begin
        // nominalni hodnota, dokud neprobehne kalibrace (jen orientacne; cal_valid = 0 => ts se nevydavaji); V = 16384 + x14
        for (li = 0; li < NC; li = li + 1) begin
            nx = (li * 93 + 46 > 16383) ? 16383 : (li * 93 + 46);
            ng = 16384 - nx;
            lut[li] = (nx == 0) ? 16'h4000 : {2'd2, ng[13:0]};
        end
    end

    reg  [15:0]   lut_q   = 16'd0;
    reg  [15:0]   lut_q2  = 16'd0;            // FW 0x0421: vystup BRAM (synteza lut_q + lut_q2 vtahne do BRAM: adresa + vystupni registr)
    reg  [15:0]   lut_q3  = 16'd0;            // samostatny FF za BRAM (tCO 3,5 ns): az od nej se vybira
    reg  [33:0]   tr0     = 34'd0, tr1 = 34'd0, tr2 = 34'd0;   // tick_hi - 0/1/2 (viz nize; tr0 jen pro DUAL)
    reg           tv_y    = 1'b0, tv_z = 1'b0;
    reg  [HW-1:0] hist_rd = {HW{1'b0}};
    reg  [HW-1:0] hist_rd2  = {HW{1'b0}};     // FW 0x0421: samostatny registr za vystupem BRAM (tCO 3,5 ns + inkrement nestihal)
    reg  [HW-1:0] hist_wd_r = {HW{1'b0}};
    reg           hist_we_r = 1'b0;
    reg           evc1      = 1'b0;           // zapocitana udalost kalibrace (dv2 & s_col), o takt zpozdena
    reg  [AW-1:0] scan_k  = {AW{1'b0}};
    reg  [AW-1:0] clr_k   = {AW{1'b0}};
    reg  [HW-1:0] cnt     = {HW{1'b0}};
    reg           cnt_hit = 1'b0;             // cnt == CNT_LAST (registrovane)
    reg  [15:0]   lutv    = 16'd0;            // polozka tabulky {amt, neg}
    reg  [13:0]   xs14    = 14'd0;            // saturovana hodnota x14 (viz vypocet tabulky)

    // FW 0x041E: volba adresy cteni BRAM pres REGISTROVANY priznak `busy_ra` (= cal_busy o takt pozdeji) misto
    // kombinacniho OR ctyr stavu: cesta s_col -> cal_busy -> mux -> ADB BRAM mela 3 urovne LUT a 6 ns vedeni
    // (nejhorsi cesta navrhu pri nejistote hodin 0,9 ns). Posun o takt nevadi: priklad vyberu se meni jen na
    // zacatku/konci kalibrace, kdy se cte jen kvuli diagnostice (vypis dump_k) a pocitani (s_col) zacina az po
    // desitkach taktu (fz/dv1/dv2). Prioritu `s_cmp` (scan_k) drzime primo.
    reg           cmp_ra  = 1'b0;             // FW 0x0421: s_cmp o takt pozdeji (vyber adresy z registru; faze kalibrace o 1 dale)
    always @(posedge clk) begin busy_ra <= s_clr | s_col | s_drn | s_cmp; cmp_ra <= s_cmp; end
    wire [AW-1:0] hist_ra = cmp_ra ? scan_k : (busy_ra ? caddr : dump_k[AW-1:0]);
    wire          hist_we = s_clr | hist_we_r;
    wire [AW-1:0] hist_wa = s_clr ? clr_k : caddr;
    wire [HW-1:0] hist_wd = s_clr ? {HW{1'b0}} : hist_wd_r;
    wire          lut_we  = s_cmp & ph[8];
    wire [AW-1:0] lut_wa  = scan_k;
    wire [15:0]   lut_wd  = lutv;

    always @(posedge clk) begin
        lut_q     <= lut[caddr];
        lut_q2    <= lut_q;
        lut_q3    <= lut_q2;
        hist_rd   <= hist[hist_ra];
        hist_rd2  <= hist_rd;
        hist_wd_r <= hist_rd2 + {{(HW-1){1'b0}}, 1'b1};
        evc1      <= dv2 & s_col;             // zapisuje se JEN udalost, kterou citac `cnt` zapocetl (i po s_col -> s_drn)
        hist_we_r <= evc1;
        if (lut_we)  lut[lut_wa]   <= lut_wd;
        if (hist_we) hist[hist_wa] <= hist_wd;

        // presny cas hrany (~8 taktu od trig_ack) = tick*16384 - V. FW 0x0421 (casovani pro periodu 8 ns): BEZ ARITMETIKY za BRAM. Polozka tabulky nese
        // dolnich 14 bitu hotovych (neg) a `amt` = o kolik se horni cast lisi od tick_hi. tick_hi se pricita PRAVE o 1 za takt
        // (mod 2^34), takze tick_hi - 1 a tick_hi - 2 jsou jeho dve predchozi hodnoty: misto 34bitoveho odecitani se vybira
        // z posuvneho registru. DUAL = 0: V ma vzdy bit +T, tedy amt = 1 nebo 2; DUAL = 1: amt = 0, 1 nebo 2 (tr0).
        tr0   <= tick_ps[47:14];
        tr1   <= (DUAL != 0) ? tr0 : tick_ps[47:14];
        tr2   <= tr1;
        // ts_ps se DRZI do dalsi platne znacky (>= 5 taktu): okno/regrese z nej odecitaji ve 2 taktech (multicycle v SDC)
        if (tv_z) ts_ps <= {(DUAL != 0) ? ((lut_q3[15:14] == 2'd0) ? tr0 : ((lut_q3[15:14] == 2'd1) ? tr1 : tr2))
                                       : (lut_q3[15] ? ((EXT_TR != 0) ? tr2_ext : tr2) : ((EXT_TR != 0) ? tr1_ext : tr1)), lut_q3[13:0]};
        tv_y     <= dv2 & cal_valid & ~cal_busy;
        tv_z     <= tv_y;
        ts_valid <= tv_z;
    end

    assign dump_q = {{(24-HW){1'b0}}, hist_rd2};

    // faze: jedno-horka, mimo DRN/CMP drzena v 1; po ph[9] se sama otoci na ph[0]
    // (nezavisla na cal_req -> kratka cesta)
    always @(posedge clk) ph <= (s_drn | s_cmp) ? {ph[8:0], ph[9]} : 10'd1;

    // ---------------- radic kalibrace (jen ridici registry) ---------------
    localparam [HW-1:0] CNT_LAST = {1'b1, {(HW-1){1'b0}}} - {{(HW-1){1'b0}}, 1'b1};
    localparam [AW-1:0] ADDR_LAST = {AW{1'b1}};
    always @(posedge clk) begin
        cnt_hit <= (cnt == CNT_LAST);         // cnt se meni jen pri udalosti (>= 8 taktu)
        if (cal_req) begin
            s_clr <= 1'b1; s_col <= 1'b0; s_drn <= 1'b0; s_cmp <= 1'b0;
            clr_k     <= {AW{1'b0}};
            cnt       <= {HW{1'b0}};
            cal_valid <= 1'b0;
            cal_fail  <= 1'b0;
            d_maxtap  <= 16'd0;               // B1a: reset pred novou kalibraci
        end else begin
            if (s_clr) begin                      // vynuluj histogram (NC taktu)
                clr_k <= clr_k + {{(AW-1){1'b0}}, 1'b1};
                if (clr_k == ADDR_LAST) begin s_clr <= 1'b0; s_col <= 1'b1; end
            end
            if (s_col) begin                      // sbirej udalosti
                if (dv2) begin
                    cnt <= cnt + {{(HW-1){1'b0}}, 1'b1};
                    if ({{(16-CW){1'b0}}, hicode_r} > d_maxtap) d_maxtap <= {{(16-CW){1'b0}}, hicode_r};  // B1a
                    if (cnt_hit) begin            // 2^CAL_LOG2. udalost
                        s_col <= 1'b0; s_drn <= 1'b1;
                    end
                end
                if (cal_abort) begin              // RO nejde -> selhani (timeout z top.v)
                    cal_fail  <= 1'b1;
                    cal_valid <= 1'b1;            // degradovany rezim: nominalni LUT, mereni nestoji
                    s_col     <= 1'b0;
                end
            end
            if (s_drn) begin                      // dobehne zapis posledni udalosti
                if (ph[9]) begin
                    s_drn <= 1'b0; s_cmp <= 1'b1; scan_k <= {AW{1'b0}};
                end
            end
            if (s_cmp) begin                      // NC adres x 10 taktu
                if (ph[9]) begin
                    if (lastk_r) begin
                        s_cmp     <= 1'b0;
                        cal_valid <= 1'b1;
                    end else begin
                        scan_k <= scan_k + {{(AW-1){1'b0}}, 1'b1};
                    end
                end
            end
        end
    end

    // ---------------- vypocet tabulky (datova cesta, bez cal_req) ---------
    //   V[a] = 16384 * (cum_pred + hist[a]/2) / N  =  (2*cum_pred + hist[a]) >> (LOG2-13)
    //   (N = 2^CAL_LOG2 = pocet VSECH udalosti obou sad; jednotka T_clk/16384 -> jen posun)
    //   10 taktu na adresu, kazdy takt jedna kratka operace:
    //   ph1 hcur<=hist_rd | ph2 xv,cum | ph3 hnz,hgt,lutv | ph4 lut zapis | ph5 diag
    reg [HW:0]    cum  = {(HW+1){1'b0}};
    reg [HW+1:0]  xv   = {(HW+2){1'b0}};
    reg [HW-1:0]  hcur = {HW{1'b0}};
    reg           hnz  = 1'b0, hgt = 1'b0;
    reg           dg_nz = 1'b0, dg_pk = 1'b0, dg_ov = 1'b0;
    reg           lastk_r = 1'b0, clr_d = 1'b0;
    wire [HW+1:0] xsh  = xv >> (CAL_LOG2 - 13);        // 0..16384
    // sada R (adresy bez horniho bitu u DUAL, vsechny u DUAL=0) dostava +T = bit 14
    wire          r_entry = (DUAL != 0) ? ~scan_k[AW-1] : 1'b1;
    always @(posedge clk) begin
        clr_d <= s_clr;                                   // vynulovani diagnostiky z registru (s_clr ridi i BRAM, velky fanout)
        if (clr_d) begin
            d_ovf <= 32'd0; d_peak <= 32'd0; d_nz <= 16'd0; d_last <= 16'd0;
        end
        if (s_drn) cum <= {(HW+1){1'b0}};
        if (s_cmp & ph[3]) hcur <= hist_rd2;              // hist_rd2 = hist[scan_k] o 2 takty (FW 0x0421; BRAM DO ma jediny odbernik hist_rd2)
        // FW 0x0421 (casovani pro periodu 8 ns): hcur je stabilni od ph[1], soucty se delaji az v ph[3] (multicycle 2 v timing.sdc)
        // a vsechny dalsi kroky se o fazi posunuly (ph[4]: hnz/hgt/lutv, ph[5]: zapis tabulky, ph[6]: diagnostika).
        if (s_cmp & ph[5]) begin
            xv  <= {cum, 1'b0} + {2'b00, hcur};
            cum <= cum + {1'b0, hcur};
        end
        if (s_cmp & ph[6]) begin
            hnz  <= (hcur != {HW{1'b0}});
            hgt  <= ({{(32-HW){1'b0}}, hcur} > d_peak);
            xs14 <= (xsh > 16383) ? 14'd16383 : xsh[13:0];                // saturace (x = 2N)
        end
        if (s_cmp & ph[7])                                                 // polozka tabulky {amt, neg}: V = 16384*r_entry + x14
            lutv <= {{1'b0, r_entry} + {1'b0, (xs14 != 14'd0)}, 14'd0 - xs14};
        // povoleni zapisu diagnostiky jsou REGISTRY (CE jednim FF, ne z ph/hnz/hgt/scan_k): v ph[7] se pripravi, v ph[8] plati
        dg_nz <= s_cmp & ph[8] & hnz;
        dg_pk <= s_cmp & ph[8] & hnz & hgt;
        lastk_r <= (scan_k == ADDR_LAST);                 // scan_k se meni jednou za 10 taktu
        dg_ov <= s_cmp & ph[8] & lastk_r;
        if (dg_nz) begin                                  // diagnostika
            d_nz   <= d_nz + 16'd1;                       // pocet neprazdnych kodu (multicycle 3 v timing.sdc)
            d_last <= {{(16-AW){1'b0}}, scan_k};
        end
        if (dg_pk) d_peak <= {{(32-HW){1'b0}}, hcur};
        if (dg_ov) d_ovf  <= {{(32-HW){1'b0}}, hcur};
    end
endmodule
