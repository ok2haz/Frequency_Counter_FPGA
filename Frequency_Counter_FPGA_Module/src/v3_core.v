// ============================================================
// File: v3_core.v -- protokol v3 (docs/PROTOKOL_V3.md v projektu STM), jadro mereni v domene 100 MHz.
// 2026-10-22. Pouziva top_v3.v (projekt v3/Counter_FPGA_v3.gprj); dnesni bitstream v2 (top.v) ho nepouziva.
//
//   seg_chan3 : jeden kanal -- absolutni citac hran X (40 b), parovani znacky TDC s indexem hrany (FIFO 2),
//               soucty segmentu N, X0, T0, SX = sum(x - x0), ST = sum(t - t0), diagnostika (bubliny, CS,
//               kontrolni pocitani hran), priznak neplatnosti (kalibrace / bez tabulky).
//   v3_ring   : casovac segmentu (L = 25 / 10 ms, mrizka taktu 100 MHz), zapis dvojice zaznamu (A, B) do kruhu
//               v BSRAM (32 B na zaznam, 8 slov), prepinaci bit pro aplikaci (10 MHz).
//   pps_cap3  : GPS 1PPS -- poradi (u32), cas posledni a predchozi hrany v taktech 100 MHz (hruby, jako v2).
//   cal_seq3  : kalibrace TDC kanalu STRIDAVE (A, pak B): jeden kanal meri vzdy (PROTOKOL_V3 kap. 6.3).
//   raw_cap3  : RAW_CAPTURE (TYPE 0x0B) -- 512 po sobe jdoucich znacek jednoho kanalu do BSRAM.
//
// Proc takhle (rozbor docs/audit/2026-10-22_optimalizace-bitstreamu.md, O4): FPGA uz nesklada okna, regresi ani
// zamky vysledku; jen scita znacky po segmentech a zaznamy kopiruje skladac (app_v3.v) z RAM. Hranice segmentu
// je pevna mrizka taktu (presne 4,096e10 jednotek pri 25 ms) -- STM sklada okna libovolne delky.
// ============================================================

// ------------------------------------------------------------
// seg_chan3: jeden kanal. Casovani (tdc_chan): trig_ack v taktu T (= ec), rise_s v T+1, ts_valid v T+9 (top_v3: TSPIPE = 1
//   -> T+10; FIFO snese do T+12: index treti hrany prijde nejdriv v T+13); dalsi
//   spusteni nejdriv v T+5. Index hrany se zachyti v T+3 (citac X ma registrovany prenos, hodnota je od T+3
//   konzistentni a dalsi hrana ji zmeni nejdriv v T+4) a ceka ve FIFO (hloubka 2: nejvys dve rozpracovane znacky).
//   🔴 v2 (win_recip/regr_acc) bral index posledni SPUSTENE hrany -- pri spousteni hrana za hranou (> ~11 MHz) patril
//   k znacce predchozi hrany index nasledujici (konstantni posun -> sklon spravne, ale x0 o krok vedle). Tady paruje FIFO.
// Soucty (znacky >= 5 taktu od sebe, kazdy stupen nejvys ~2 urovne LUT jako v2 -- zadna multicycle vyjimka v SDC):
//   V+1 dolnich 20/13 b rozdilu t - t0, x - x0 (registrovana vypujcka), V+2 hornich 20/13 b, V+3 dolni casti souctu,
//   V+4 horni casti souctu (registrovany prenos). Zapisovac proto ceka DRAIN = 6 taktu po hranici segmentu. Rozsahy pri L = 25 ms: N <= 5e5 (24 b), x - x0 < 2^21 (26 b), t - t0 < 2^36 (40 b), SX < 2^40 (48 b),
//   ST < 2^55 (56 b).
// ------------------------------------------------------------
module seg_chan3 #(
    // FW 0x0504: 1 = soucty SX/ST jednim stupnem s atributem syn_dspstyle = "dsp" (GowinSynthesis -> ALU54D v radku DSP
    // R19; v logice by to byl 48/56b retez ALU). 0 = dvoustupnove soucty s registrovanym prenosem v logice (do FW 0x0503).
    // Hodnoty v obou variantach shodne a hotove nejpozdeji ve V+4 (DRAIN = 6 plati).
    parameter SUM_DSP = 0
)(
    input  wire        clk,         // clk_p0_100m
    input  wire        rise_s,      // kazda nabezna hrana (tdc_chan.rise_s)
    input  wire        cnt_en,      // 0 = retez krmi ring oscilator (sber histogramu): hrany nejsou signal, nepocitat
    input  wire        trig_ack,    // tato hrana spustila presny cas
    input  wire        ts_valid,    // znacka ts_ps platna (drzi se do dalsi)
    input  wire [47:0] ts_ps,       // [T_clk/16384], mod 2^48
    input  wire        bub,         // bublina u znacky (platne v ts_valid)
    input  wire        cs_act,      // CS SPI aktivni (synchronizovane)
    input  wire        hold,        // kanal nemeri platne (kalibrace / bez tabulky / boot)
    input  wire        sig_pin,     // TYZ vstupni pin pro kontrolni pocitani (vlastni synchronizator)
    input  wire        freeze,      // v3_ring: konec segmentu, soucty se ctou -> znacky se nepricitaji
    input  wire        snap,        // v3_ring: 1 takt na zacatku freeze (kontrolni porovnani poctu hran)
    input  wire        clear,       // v3_ring: 1 takt, soucty prectene -> novy segment
    input  wire        nfc,         // 2026-10-24: v3_ring, registr == ~freeze & ~clear (v kazdem taktu)
    input  wire [2:0]  presc,       // FW 0x0503: log2 P predelicky (meni se jen na hranici segmentu)
    output wire [23:0] o_n,
    output wire [39:0] o_x0,
    output wire [47:0] o_t0,
    output wire [47:0] o_sx,
    output wire [55:0] o_st,
    output wire [7:0]  o_flags,     // R_*: bit0 neplatny, bit1 zmena P, bit2 bublina, bit3 znacky pri CS, bit4 kontrolni pocitani
    output wire [2:0]  o_presc,     // FW 0x0503: log2 P segmentu (zaznam: bajt CH bity 6:4)
    output reg  [7:0]  o_bub,       // pocet znacek s bublinou (saturuje)
    output reg  [3:0]  o_ovl        // pocet znacek pri aktivnim CS (saturuje)
);
    initial begin o_bub = 8'd0; o_ovl = 4'd0; end

    // ---- absolutni citac hran (40 b, mod 2^40), registrovany prenos ----
    reg  [7:0]  xlo = 8'd0;
    reg  [31:0] xhi = 32'd0;
    reg         xcy = 1'b0;
    wire        inc = rise_s & cnt_en;
    always @(posedge clk) begin
        xcy <= inc & (xlo == 8'hFF);
        if (inc) xlo <= xlo + 8'd1;
        if (xcy) xhi <= xhi + 32'd1;
    end
    wire [39:0] xcnt = {xhi, xlo};

    // ---- index spustene hrany -> FIFO (2) ----
    reg        tq1 = 1'b0, tq2 = 1'b0, tq3 = 1'b0;
    reg [39:0] fh = 40'd0, fs = 40'd0;         // hlava (nejstarsi), druha
    reg [1:0]  fn = 2'd0;
    reg        fnz = 1'b0;                     // 2026-10-24: == (fn != 0) jako registr (pop / take o uroven mene)
    reg        f_err = 1'b0;                   // znacka bez indexu / preteceni FIFO (nesmi nastat; diagnostika)
    // FW 0x0506: ts_valid o takt zpozdene (registr tvd odlehci fan-out ts_valid z tdc_chan; ts_ps se drzi >= 5 taktu, FIFO
    // snese do T+12 -> pop v T+11)
    reg        tvd = 1'b0;
    always @(posedge clk) tvd <= ts_valid;
    wire       push = tq3;
    wire       pop  = tvd & fnz;
    always @(posedge clk) begin
        tq1 <= trig_ack; tq2 <= tq1; tq3 <= tq2;
        if (hold) begin
            fn <= 2'd0; fnz <= 1'b0;
        end else begin
            case ({push, pop})
                2'b10: begin
                    fnz <= 1'b1;                   // fn je po vlozeni 1 nebo 2 (2 zustava i pri preteceni)
                    if (fn == 2'd0) begin fh <= xcnt; fn <= 2'd1; end
                    else if (fn == 2'd1) begin fs <= xcnt; fn <= 2'd2; end
                    else f_err <= 1'b1;
                end
                2'b01: begin fh <= fs; fn <= fn - 2'd1; fnz <= fn[1]; end   // pop jen pri fn 1 nebo 2
                2'b11: begin
                    if (fn == 2'd1) fh <= xcnt;
                    else begin fh <= fs; fs <= xcnt; end
                end
                default: ;
            endcase
            if (tvd && fn == 2'd0) f_err <= 1'b1;
        end
        if (clear) f_err <= 1'b0;                  // chyba plati pro segment (zaznam s ni je neplatny)
    end

    // ---- soucty segmentu ----
    reg        first = 1'b1;                   // segment zatim bez znacky
    (* syn_preserve = 1 *) reg first_b = 1'b1;   // 2026-10-24: kopie first pro povoleni x0 (t0 a x0 = 88 FF na jednom CE)
    reg [11:0] nlo = 12'd0, nhi = 12'd0;
    reg        ncy = 1'b0;
    reg [39:0] x0 = 40'd0;
    reg [47:0] t0 = 48'd0;
    reg [39:0] xm = 40'd0;                     // index prave znacky
    reg [39:0] tm = 40'd0;                     // cas prave znacky (dolnich 40 b)
    reg        v1 = 1'b0, v1b = 1'b0, v2 = 1'b0, v3 = 1'b0;
    reg [12:0] dxl = 13'd0, dxu = 13'd0;
    reg [19:0] dtl = 20'd0, dtu = 20'd0;
    reg        bxl = 1'b0, btl = 1'b0;
    wire [25:0] dx_r = {dxu, dxl};
    wire [39:0] dt_r = {dtu, dtl};
    reg [23:0] sx_lo = 24'd0, sx_hi = 24'd0;
    reg [31:0] st_lo = 32'd0;
    reg [23:0] st_hi = 24'd0;
    reg        sxc = 1'b0, stc = 1'b0;
    reg [1:0]  dxh = 2'd0;
    reg [7:0]  dth = 8'd0;
    reg        inv = 1'b1;                     // segment zasazeny hold
    // ~clear: v3_ring shodi freeze v tomtez taktu, kdy da clear -> znacka prave v taktu clear by se pricetla se starym
    // x0/t0 do prave vynulovanych souctu (N ji nepocita); zahodi se jako znacky behem freeze (L-0198)
    // 2026-10-24: ~freeze & ~clear jako jeden registr nfc (v3_ring, shodny v kazdem taktu) -> CE t0 = LUT5 ze 5 registru
    wire       take = pop & ~hold & nfc;
    // 2026-10-24 (casovani, pnr_seeds: hold_x_s -> t0/x0 CE v 5 z 8 sestaveni): t0 a x0 jsou tm a xm PRVNI znacky
    // segmentu -> nactou se o takt pozdeji z tm/xm, povoleni je jediny registr tf (t0) / tf_b (x0). ts_ps[47:40] je v tom
    // taktu stale tataz znacka (drzi se >= 5 taktu), dalsi take nejdriv za 5 taktu (v1 cte t0/x0 az pri druhe znacce),
    // zapisovac cte o_t0/o_x0 az DRAIN taktu po hranici (znacky behem freeze se neberou) -> stejne hodnoty.
    reg        tf = 1'b0;
    (* syn_preserve = 1 *) reg tf_b = 1'b0;
    always @(posedge clk) begin
        ncy <= 1'b0;
        v1  <= take & ~first;
        tf   <= take & first;
        tf_b <= take & first_b;
        if (tf)   t0 <= {ts_ps[47:40], tm};
        if (tf_b) x0 <= xm;
        v1b <= v1;
        v2  <= v1b;
        v3  <= v2;
        if (take) begin
            xm <= fh; tm <= ts_ps[39:0];
            if (first)   first <= 1'b0;
            if (first_b) first_b <= 1'b0;
            nlo <= nlo + 12'd1; ncy <= (nlo == 12'hFFF);
            if (bub)    o_bub <= 8'd1;                 // FW 0x0507: jen priznak (pocet v hlavicce zrusen, kap. 3.1b)
            if (cs_act) o_ovl <= 4'd1;
        end
        if (ncy) nhi <= nhi + 12'd1;
        if (v1)  begin {btl, dtl} <= {1'b0, tm[19:0]} - {1'b0, t0[19:0]};  {bxl, dxl} <= {1'b0, xm[12:0]} - {1'b0, x0[12:0]}; end
        if (v1b) begin dtu <= tm[39:20] - t0[39:20] - {19'd0, btl};      dxu <= xm[25:13] - x0[25:13] - {12'd0, bxl}; end
        if (v2) begin
            {sxc, sx_lo} <= {1'b0, sx_lo} + {1'b0, dx_r[23:0]}; dxh <= dx_r[25:24];
            {stc, st_lo} <= {1'b0, st_lo} + {1'b0, dt_r[31:0]}; dth <= dt_r[39:32];
        end
        if (v3) begin
            sx_hi <= sx_hi + {22'd0, dxh} + {23'd0, sxc};
            st_hi <= st_hi + {16'd0, dth} + {23'd0, stc};
        end
        if (hold) inv <= 1'b1;
        if (clear) begin
            first <= 1'b1; first_b <= 1'b1; nlo <= 12'd0; nhi <= 12'd0;
            sx_lo <= 24'd0; sx_hi <= 24'd0; st_lo <= 32'd0; st_hi <= 24'd0;
            x0    <= 40'd0; t0 <= 48'd0;              // 2026-10-25: segment bez znacky ma X0 = T0 = 0 bez masky first (viz o_x0)
            o_bub <= 8'd0; o_ovl <= 4'd0;
            inv   <= hold;
        end
    end

    // ---- kontrolni pocitani hran (samokontrola jako v2 win_recip): vlastni synchronizator pinu ----
    // Rozdil (kontrolni hrany segmentu) - (prirustek X za segment) je u zdraveho segmentu 0 (+-1 na hranici: obe cesty
    // maji od pinu ~4 takty, hrana tesne u snap padne v kazde ceste do jineho segmentu; sousedni segmenty se vyrusi).
    (* keep = "true" *) reg cp1 = 1'b0;
    reg        cp2 = 1'b0, cp3 = 1'b0, ce = 1'b0;
    reg [9:0]  ccnt = 10'd0, xprev = 10'd0, cdd = 10'd0;
    reg        ccd_bad = 1'b0, sn1 = 1'b0;
    // 2026-10-25 (oprava, sim/tb_ccd_carry.sv): citac X ma registrovany prenos -- v taktu xcy = 1 je xlo uz 0 a xhi jeste stary,
    // xcnt[9:0] je tedy o 256 mensi nez skutecny pocet hran. snap v tomto taktu dal cdd = +-256 a falesny ccd_bad ve dvou
    // zaznamech (f / (256 x 100 MHz) na segment). xc10 = dolnich 10 b skutecneho poctu i v taktu prenosu.
    wire [9:0] xc10 = {xhi[1:0] + {1'b0, xcy}, xlo};
    always @(posedge clk) begin
        cp1 <= sig_pin; cp2 <= cp1; cp3 <= cp2;
        ce  <= cp2 & ~cp3 & cnt_en;
        sn1 <= snap;
        if (snap) begin
            cdd   <= ccnt - (xc10 - xprev);
            xprev <= xc10;
            ccnt  <= {9'd0, ce};
        end else if (ce) ccnt <= ccnt + 10'd1;
        if (sn1) ccd_bad <= ~((cdd == 10'd0) | (cdd == 10'd1) | (cdd == 10'h3FF));
    end

    // 2026-10-25 (plocha, kruh v 16 SDPB): x0 / t0 se nuluji v clear (tf / tf_b s clear nikdy nesplyne: take v taktech
    // freeze a clear nenastane), takze v okne zapisu zaznamu (E+7 .. E+15, znacky stoji) je x0 == (first ? 0 : x0) a
    // t0 == (first ? 0 : t0). Drive tuto masku nesl vyber slova zapisovace; s primym zapisem do SDPB by stala 88 LUT na kanal.
    assign o_x0    = x0;
    assign o_t0    = t0;
    // FW 0x0504: soucty v DSP (SUM_DSP = 1); 2026-10-25: ALU54D pricita primo rozdil xm - x0 / tm - t0 ve V+1 (dx_r / dt_r a
    // stupne v1b, v2, v3 pouziva jen varianta SUM_DSP = 0)
    generate if (SUM_DSP != 0) begin : g_dsp
        // FW 0x0506: ST jen 54 b (ALU54D). Hranice: znacky >= 5 taktu od sebe -> ST <= max_N (N*L - a*N^2/2) = 1,0e16 < 2^54 (1,8e16)
        // 2026-10-25 (plocha): ALU54D primo DOUT <= DOUT + A - B (UG287 ALUD_MODE 0 "ACC/0 +/- B +/- A", B_ADD_SUB = 1,
        // C_ADD_SUB = 0, ACCLOAD = 1), A = xm / tm, B = x0 / t0 -> rozdil se nepocita v logice (dxl/dxu, dtl/dtu, btl/bxl:
        // 68 FF a 66 ALU na kanal; varianta SUM_DSP = 0 je zustava). Pricita se v taktu v1 (take + 1: xm / tm uz nesou znacku,
        // x0 / t0 jsou platne od take prvni znacky + 1 a dalsi take je nejdriv za 5 taktu), RESET = clear (v1 s clear nesplyne).
        // Rozdil mod 2^40: pravy rozdil je < 2^21 (x: hrana nejdriv za 2 takty, segment <= 2,5e6 taktu) a < 2^36 (t) ->
        // preteceni 40b citace mezi x0 / t0 a xm / tm je prave x0[39] & ~xm[39] (t0[39] & ~tm[39]); bit 40 operandu A
        // pricte 2^40, takze A - B = (xm - x0) mod 2^40 = drivejsi dx_r (26 b) a (tm - t0) mod 2^40 = dt_r. Soucty jsou
        // hotove ve V+1 misto V+3 (zapisovac cte od E+7). Overeni: sim/tb_seg_dsp.sv (proti SUM_DSP = 0, pres preteceni).
        wire        wx = x0[39] & ~xm[39];
        wire        wt = t0[39] & ~tm[39];
        wire [53:0] sx_q, st_q;
        ALU54D #(.AREG(1'b0), .BREG(1'b0), .ASIGN_REG(1'b0), .BSIGN_REG(1'b0), .ACCLOAD_REG(1'b0), .OUT_REG(1'b1),
                 .B_ADD_SUB(1'b1), .C_ADD_SUB(1'b0), .ALU_RESET_MODE("SYNC")) u_sx (
            .A({13'd0, wx, xm}), .B({14'd0, x0}), .ASIGN(1'b0), .BSIGN(1'b0), .ACCLOAD(1'b1), .CASI(55'd0),
            .CLK(clk), .CE(v1 | clear), .RESET(clear), .DOUT(sx_q), .CASO());
        ALU54D #(.AREG(1'b0), .BREG(1'b0), .ASIGN_REG(1'b0), .BSIGN_REG(1'b0), .ACCLOAD_REG(1'b0), .OUT_REG(1'b1),
                 .B_ADD_SUB(1'b1), .C_ADD_SUB(1'b0), .ALU_RESET_MODE("SYNC")) u_st (
            .A({13'd0, wt, tm}), .B({14'd0, t0[39:0]}), .ASIGN(1'b0), .BSIGN(1'b0), .ACCLOAD(1'b1), .CASI(55'd0),
            .CLK(clk), .CE(v1 | clear), .RESET(clear), .DOUT(st_q), .CASO());
        assign o_sx = sx_q[47:0];                    // SX < 2^40 (komentar u modulu) -> 48 b jako drive
        assign o_st = {2'b00, st_q};
        // 2026-10-25 (plocha): pocet znacek N v DSP (ALU54D, inference jako seq_c v pps_cap3; v simulaci je atribut komentar)
        // misto nlo/nhi s registrovanym prenosem (24 FF + 24 ALU). Pricita se v tk1 (take o takt pozdeji, registr ->
        // povoleni bloku DSP bez cesty pres take); N je hotove v E+2 (posledni take v E), zapisovac cte v E+7. clear v taktu,
        // kdy by tk1 = 1, nenastane (take v taktech freeze a clear neni). N <= 5e5 < 2^24 jako drive.
        reg        tk1 = 1'b0;
        reg [23:0] n_d = 24'd0 /* synthesis syn_dspstyle = "dsp" */;
        always @(posedge clk) begin
            tk1 <= take;
            if (clear)    n_d <= 24'd0;
            else if (tk1) n_d <= n_d + 24'd1;
        end
        assign o_n = n_d;
    end else begin : g_fab
        assign o_n  = {nhi, nlo};
        assign o_sx = {sx_hi, sx_lo};
        assign o_st = {st_hi, st_lo};
    end endgenerate
    // FW 0x0503: log2 P segmentu (prevzato v clear = zacatek segmentu; P se meni v seg_tick pred nim), FLAGS bit1 =
    // P se na zacatku tohoto segmentu zmenilo (segment je stejne neplatny: hold 2 hranice v top_v3)
    reg [2:0] p_seg = 3'd0;
    reg       p_chg = 1'b0;
    always @(posedge clk) if (clear) begin p_chg <= (presc != p_seg); p_seg <= presc; end
    assign o_presc = p_seg;
    assign o_flags = {3'd0, ccd_bad & ~inv, (o_ovl != 4'd0), (o_bub != 8'd0), p_chg, inv | f_err};
endmodule

// ------------------------------------------------------------
// v3_ring: casovac segmentu + zapis zaznamu do kruhu.
//   Hranice segmentu: seg_tick (1 takt). Pak freeze (znacky se nepricitaji) -> DRAIN taktu dobeh souctu -> zapis
//   zaznamu A (8 slov) a B (8 slov) -> clear -> konec freeze. Celkem 25 taktu = 250 ns z 25 ms: vypadne nejvys
//   ~4 znacek kanalu (zanedbatelne; pocitani hran X bezi dal, takze se nic nerozbije).
//   Zaznam v RAM = 32 B (LE slova): bajty 0..30 presne jako v ramci (kap. 3.2), bajt 31 = 0.
//   Aplikace (10 MHz) se dozvi o zapsane dvojici zaznamu prepnutim rec_tgl (az PO zapisu obou).
//   2026-10-25 (plocha): kazde slovo w kanalu c ma vlastni blok SDPB (16 bloku) -> zaznam se zapise najednou bez vyberu
//   slova; aplikace cte po bajtech (adresa {zaznam, bajt}).
// ------------------------------------------------------------
module v3_ring #(
    parameter RAW      = 9,          // log2 poctu zaznamu v kruhu (512 = 6,4 s pri 25 ms)
    parameter SEG25    = 2500000,    // taktu na segment L = 25 ms
    parameter SEG10    = 1000000     // L = 10 ms
)(
    input  wire        clk,
    input  wire        seg10_req,    // 1 = L 10 ms (kvazistaticke z 10 MHz; prevezme se na hranici segmentu)
    output reg         seg10_act,    // prave platna delka (echo do ramce)
    output reg         seg_tick,     // 1 takt: hranice segmentu (i prepnuti banky TDC)
    output reg         freeze,
    output reg         snap,
    output reg         clear,
    (* syn_preserve = 1 *) output reg clear_a,   // FW 0x0506: kopie clear pro seg_chan3 A / B (fan-out clear byl 280)
    (* syn_preserve = 1 *) output reg clear_b,
    // 2026-10-24: nfc_a/nfc_b = ~freeze & ~clear_a / ~clear_b jako registr (shodne v kazdem taktu; seg_chan3 take o uroven mene)
    (* syn_preserve = 1 *) output reg nfc_a,
    (* syn_preserve = 1 *) output reg nfc_b,
    output reg         rec_tgl,
    output reg  [31:0] seg_idx,      // cislo segmentu, ktery se prave zapisuje / bezi
    input  wire [23:0] a_n,  b_n,
    input  wire [39:0] a_x0, b_x0,
    input  wire [47:0] a_t0, b_t0,
    input  wire [47:0] a_sx, b_sx,
    input  wire [55:0] a_st, b_st,
    input  wire [7:0]  a_fl, b_fl,
    input  wire [2:0]  a_p, b_p,         // FW 0x0503: log2 P segmentu -> bajt CH bity 6:4
    // cteni kruhu (domena aplikace)
    input  wire        rclk,
    input  wire [RAW+4:0] raddr,     // 2026-10-25: {zaznam, bajt 0..31} (drive {zaznam, slovo} a bajt vybirala aplikace)
    output wire [7:0]  rdata         // bajt, 1 takt rclk po adrese (jako drive registrovany vystup RAM)
);
    initial begin seg10_act = 1'b0; seg_tick = 1'b0; freeze = 1'b0; snap = 1'b0; clear = 1'b0; clear_a = 1'b0; clear_b = 1'b0; nfc_a = 1'b1; nfc_b = 1'b1; rec_tgl = 1'b0; seg_idx = 32'd0; end
    localparam DRAIN = 6;                            // dobeh souctu seg_chan3 (V+4) + rezerva

    // ---- casovac (odpocet, registrovana detekce konce) ----
    reg  [1:0]  s10 = 2'b00;
    // FW 0x0506: odpocet {chi, clo} = puvodni 22b cnt, rozdeleny na 2 x 11 b (dlouhy retez ALU mel v P&R rezervu -3 az -5 ns);
    // lo_z = (clo == 0) a hi_z = (chi == 0) jsou registrovane o takt driv, perioda zustava presne SEG25 / SEG10 taktu
    // Podminka: (SEG - 1) mod 2048 musi byt >= 2 (jinak by reload dal lo_z / z1 chybne); plati pro 2 499 999 (1439), 999 999 (575),
    // simulace 24 999 (423) a 9 999 (1807)
    reg  [10:0] clo = SEG25 - 1;
    reg  [10:0] chi = (SEG25 - 1) >> 11;
    reg         lo_z = 1'b0, hi_z = 1'b0;
    reg         z1  = 1'b0;
    wire [21:0] ld  = (s10[1] ? SEG10 : SEG25) - 1;
    always @(posedge clk) begin
        s10      <= {s10[0], seg10_req};
        seg_tick <= 1'b0;
        hi_z     <= (chi == 11'd0);
        lo_z     <= (clo == 11'd1);                   // clo bude 0
        z1       <= hi_z & (clo == 11'd1);            // cnt == 1
        if (z1) begin                                 // cnt je ted 0: konec segmentu
            seg_tick  <= 1'b1;
            seg10_act <= s10[1];
            clo       <= ld[10:0];
            chi       <= ld[21:11];
            lo_z      <= 1'b0;
        end else if (lo_z) begin
            clo <= 11'h7FF;
            chi <= chi - 11'd1;
        end else clo <= clo - 11'd1;
    end

    // ---- zapisovac (2026-10-25, plocha: 16 SDPB, blok = kanal x slovo) ----
    // Slovo w zaznamu kanalu c lezi v bloku SDPB (c, w) na adrese dvojice (wrec / 2) -> cely zaznam obou kanalu se zapise
    // bez vyberu slova (drive jednickovy kod oh* ve ctvrtinach, wl_x / wh_x, wd, wa: ~420 LUT a ~220 FF; cesty ohA, k ->
    // wa_x, DSP -> wa_a). Takt vzorku zustava jako ve stupni 1 drivejsiho zapisovace (slovo k v E+7+k, E = takt seg_tick):
    // vystupy seg_chan3 jsou od E+4 do clear stale (N, X0, T0, SX, ST, P, ccd_bad od E+3, seg_idx do E+25), zive se meni
    // jen FLAGS bit0 (inv = hold, f_err) -> slova A a slova 1..7 B se zapisi v E+7 (wsA), slovo 0 B (FLAGS) v E+15 (wsB0,
    // drive k = 8). Konec zapisu (clear) zustava v E+26 (citac kc) -> freeze / clear / nfc / seg_idx / rec_tgl beze zmeny.
    // Cteni: blok (zaznam & 1, bajt / 4) cte 8b portem adresu {dvojice, bajt % 4} (UG285: zapis 32 b a cteni 8 b teze
    // pameti, bajt k slova W je 8b adresa 4W + k) -> vyber 16:1 po bajtu (domena aplikace, 100 ns).
    localparam integer PW = RAW - 1;                 // bitu adresy dvojice (SDPB: 32 b -> 9 bitu adresy, 8 b -> 11)
    reg [RAW-1:0] wrec = {RAW{1'b0}};                // poradi zaznamu A teto dvojice (sude)
    reg [4:0]  kc   = 5'd0;                          // takty freeze (0 = E+1)
    reg        wsA  = 1'b0;                          // E+7: zapis slov A a slov 1..7 B
    reg        wsB0 = 1'b0;                          // E+15: zapis slova 0 B (FLAGS B ve stejnem taktu jako drive)
    reg        kend = 1'b0;                          // E+25: konec zapisu (registr misto porovnani k, cesta k -> nfc_a)
    always @(posedge clk) begin
        snap  <= 1'b0;
        clear <= 1'b0; clear_a <= 1'b0; clear_b <= 1'b0;
        nfc_a <= ~freeze; nfc_b <= ~freeze;              // clear v pristim taktu 0, freeze beze zmeny (vyjimky nize)
        wsA   <= 1'b0; wsB0 <= 1'b0; kend <= 1'b0;
        if (seg_tick) begin
            freeze <= 1'b1; snap <= 1'b1; kc <= 5'd0;
            nfc_a  <= 1'b0; nfc_b <= 1'b0;
        end else if (freeze) begin
            kc   <= kc + 5'd1;
            wsA  <= (kc == DRAIN - 1);
            wsB0 <= (kc == DRAIN + 7);
            kend <= (kc == DRAIN + 17);
            if (kend) begin                              // posledni slovo je v RAM
                clear   <= 1'b1; clear_a <= 1'b1; clear_b <= 1'b1;
                nfc_a   <= 1'b0; nfc_b <= 1'b0;
                freeze  <= 1'b0;
                wrec    <= wrec + {{(RAW-2){1'b0}}, 2'd2};
                seg_idx <= seg_idx + 32'd1;
            end
        end
        if (clear) rec_tgl <= ~rec_tgl;                  // aplikace smi cist dvojici az po zapisu obou
    end
    // slova zaznamu (LE): 0 {FLAGS, CH, SEG_IDX}, 1 {X0[7:0], N}, 2 X0[39:8], 3 T0[31:0], 4 {SX[15:0], T0[47:32]},
    //                     5 SX[47:16], 6 ST[31:0], 7 {0, ST[55:32]}
    wire [255:0] dA = {{8'd0, a_st[55:32]},              // 7
                       a_st[31:0],                       // 6
                       a_sx[47:16],                      // 5
                       {a_sx[15:0], a_t0[47:32]},        // 4
                       a_t0[31:0],                       // 3
                       a_x0[39:8],                       // 2
                       {a_x0[7:0], a_n},                 // 1
                       {a_fl, 1'b0, a_p, 4'd0, seg_idx[15:0]}};    // 0
    wire [255:0] dB = {{8'd0, b_st[55:32]},
                       b_st[31:0],
                       b_sx[47:16],
                       {b_sx[15:0], b_t0[47:32]},
                       b_t0[31:0],
                       b_x0[39:8],
                       {b_x0[7:0], b_n},
                       {b_fl, 1'b0, b_p, 4'd1, seg_idx[15:0]}};
    wire [13:0]  ada = {{(9 - PW){1'b0}}, wrec[RAW-1:1], 1'b0, 4'b1111};                 // [13:5] slovo, [3:0] vsechny bajty
    wire [13:0]  adb = {{(9 - PW){1'b0}}, raddr[RAW+4:6], raddr[1:0], 3'b000};          // [13:3] bajt {dvojice, bajt % 4}
    wire [127:0] rq;                                     // bajt bloku g = {kanal, slovo} na 8*g
    genvar gw;
    generate for (gw = 0; gw < 16; gw = gw + 1) begin : g_m
        wire [31:0] dob;
        SDPB #(.READ_MODE(1'b0), .BIT_WIDTH_0(32), .BIT_WIDTH_1(8), .BLK_SEL_0(3'b000), .BLK_SEL_1(3'b000), .RESET_MODE("SYNC")) u_m (
            .CLKA(clk), .CEA((gw == 8) ? wsB0 : wsA), .RESETA(1'b0), .ADA(ada), .DI((gw < 8) ? dA[32*gw +: 32] : dB[32*(gw-8) +: 32]),
            .CLKB(rclk), .CEB(1'b1), .RESETB(1'b0), .OCE(1'b1), .ADB(adb), .DO(dob), .BLKSELA(3'b000), .BLKSELB(3'b000));
        assign rq[8*gw +: 8] = dob[7:0];
    end endgenerate
    reg [3:0] rs = 4'd0;                                 // blok cteneho bajtu (se stejnou latenci jako vystup SDPB)
    always @(posedge rclk) rs <= raddr[5:2];
    assign rdata = rq[8*rs +: 8];
endmodule

// ------------------------------------------------------------
// pps_cap3: GPS 1PPS (PIN33) -- jako pps_cap (v2), navic poradi u32 a predchozi hrana (hlavicka v3, bajty 68..107).
//   Hruby cas v taktech 100 MHz (tick_hi, 34 b = jednotky T/16384 >> 14). Cte se ZIVE z domeny 10 MHz: roztrzene
//   cteni (hrana prave pri skladani) STM pozna tak, ze vzorek bere az ze dvou ramcu se shodnym {PSEQ, PTS} (jako v2).
// ------------------------------------------------------------
module pps_cap3 (
    input  wire        clk,
    input  wire        pin,
    input  wire [33:0] tick,
    input  wire        f_tgl,        // pps_ides3: novy jemny cas (prepinaci bit, domena clk)
    input  wire [47:0] f_ts,
    input  wire        f_lock,
    output wire [31:0] seq,          // 2026-10-24: z registru seq_c (DSP), hodnota beze zmeny
    output reg         seq_nz,       // FW 0x0506: seq != 0 (bez 32b porovnani v hlavicce)
    output reg         seq_gt1,      // seq > 1
    output wire [33:0] last,         // FW 0x0506: bajty 100..107 = 0 (hruby cas je v PTS: pts >> 14 pri PFLAGS bit1 = 0)
    output wire [33:0] prev,
    output reg  [47:0] pts_l,        // PTS [T/16384]: jemny (IDES8), kdyz prisel, jinak hruby x 16384
    output reg  [47:0] pts_p,
    output reg  [7:0]  pfl_l,        // PFLAGS: bit0 platne, bit1 jemny cas
    output reg  [7:0]  pfl_p,
    output reg  [7:0]  d_l,          // jemny - hruby [takty], int8 saturovane (diagnostika osy a poradi bitu IDES8)
    output reg  [7:0]  d_p,
    output reg         nofine        // posledni hrana pri zamcene PLL bez jemneho casu (PPS_STATUS bit2)
);
    // FW 0x0502: hrubou hranu, jemny cas a PSEQ zverejni NAJEDNOU -- jemny cas prijde ~4 takty po hrubem (IDES8, PCLK,
    // synchronizator), nejvys 31 taktu se ceka. STM tak nikdy nevidi novy PSEQ se starym PTS.
    reg [2:0]  s = 3'b000;
    reg [2:0]  fs = 3'b000;
    reg        pend = 1'b0, fa = 1'b0;
    reg [4:0]  win = 5'd0, fage = 5'd0;
    reg [33:0] c_tick = 34'd0;
    reg        fok = 1'b0, wok = 1'b0;   // FW 0x0506: podminky zverejneni registrovane (o takt zpozdene)
    // 2026-10-24 (casovani): wnz = (win != 0) jako registr -- win se po hrane jen zvysuje (saturuje na 31), takze
    // wnz <= 0 pri hrane, jinak 1; povoleni zverejneni je pak jedna LUT4 ze 4 registru (cesta win -> nofine byla 9,4 ns).
    // Povoleni ridi ~165 FF (fan-out 130 v P&R) -> dve kopie: pub (pts_l, pfl_l, d_l, nofine, pend, fa) a pub_b
    // (seq, seq_nz, seq_gt1, pts_p, pfl_p, d_p) z kopii registru (*_b, stejna logika -> pub_b == pub v kazdem taktu).
    reg        wnz = 1'b0;
    // 2026-10-24 (plocha): poradi hrany v DSP (ALU54D); seq cte jen aplikace (domena 10 MHz, asynchronni skupina v SDC),
    // v domene 100 MHz z nej nic nevede -> vzdalenost radku DSP nevadi. Pro simulaci je atribut komentar.
    reg [31:0] seq_c = 32'd0 /* synthesis syn_dspstyle = "dsp" */;
    assign     seq = seq_c;
    (* syn_preserve = 1 *) reg pend_b = 1'b0;
    (* syn_preserve = 1 *) reg wnz_b = 1'b0;
    (* syn_preserve = 1 *) reg fok_b = 1'b0;
    (* syn_preserve = 1 *) reg wok_b = 1'b0;
    wire       edg   = s[1] & ~s[2];
    wire       pub   = pend & wnz & (fok | wok);
    wire       pub_b = pend_b & wnz_b & (fok_b | wok_b);
    // FW 0x0506: rozdil se registruje (dd_r, 1 takt za f_ts / c_tick); zverejneni ceka na fage >= 1 a win >= 1;
    // f_ts se po preklopeni f_tgl drzi do dalsi hrany 1PPS (~1 s), proto zadny kopirovaci registr
    reg  [33:0] dd_r = 34'd0;
    // FW 0x0507: rozdil z 12 b (jemny cas se bere jen do 31 taktu po hrube hrane a fa se pri ni nuluje -> |d| < 2048)
    wire [11:0] df12 = f_ts[25:14] - c_tick[11:0];
    // 2026-10-25 (casovani, cesta dd_r -> d_l): priznaky saturace z df12 registrovane SPOLU s dd_r. dd_r je df12 se znamenkem
    // rozsirenym na 34 b, takze drivejsi dhi = ~dd[33] & (dd[32:7] != 0) = ~df12[11] & (df12[10:7] != 0) a dlo = dd[33] &
    // (dd[32:7] != vse 1) = df12[11] & (df12[10:7] != 4'hF) -> dhi_r / dlo_r jsou v kazdem taktu shodne s drivejsimi dhi /
    // dlo (invariant v tb_top_v3, tb_hold_inv, tb_pps_v3). d_l je pak jedna LUT3 z registru misto dvou urovni za dd_r.
    reg         dhi_r = 1'b0, dlo_r = 1'b0;
    always @(posedge clk) begin
        dd_r  <= {{22{df12[11]}}, df12};
        dhi_r <= ~df12[11] & (df12[10:7] != 4'd0);          // > 127
        dlo_r <=  df12[11] & (df12[10:7] != 4'hF);          // < -128
    end
    wire [33:0] dd  = dd_r;
    assign last = 34'd0; assign prev = 34'd0;
    initial begin seq_nz = 1'b0; seq_gt1 = 1'b0; pts_l = 48'd0; pts_p = 48'd0; pfl_l = 8'd0; pfl_p = 8'd0;
                  d_l = 8'd0; d_p = 8'd0; nofine = 1'b0; end
    always @(posedge clk) begin
        s  <= {s[1:0], pin};
        fs <= {fs[1:0], f_tgl};
        if (win != 5'd31)  win  <= win + 5'd1;
        if (fage != 5'd31) fage <= fage + 5'd1;
        fok   <= fa && (fage != 5'd31) && (fage != 5'd0);
        fok_b <= fa && (fage != 5'd31) && (fage != 5'd0);
        wok   <= (win == 5'd31);
        wok_b <= (win == 5'd31);
        wnz   <= 1'b1;
        wnz_b <= 1'b1;
        if (fs[2] ^ fs[1]) begin fa <= f_lock; fage <= 5'd0; end
        // jemny cas, ktery prisel PRED hrubou hranou, k ni nepatri (zakmit kratsi nez takt, hruby ho nevidi -- nalez N2):
        // platny jemny cas hrany chodi az ~13 taktu po hrube detekci, takze se tim nic platneho nezahodi
        if (edg) begin c_tick <= tick; pend <= 1'b1; pend_b <= 1'b1; win <= 5'd0; wnz <= 1'b0; wnz_b <= 1'b0;
                       fa <= 1'b0; end
        else begin
            if (pub) begin
                pend  <= 1'b0; fa <= 1'b0;
                if (fok) begin
                    pts_l <= f_ts; pfl_l <= 8'd3; nofine <= 1'b0;
                    d_l   <= dhi_r ? 8'h7F : dlo_r ? 8'h80 : dd[7:0];
                end else begin
                    pts_l <= {c_tick, 14'd0}; pfl_l <= 8'd1; d_l <= 8'd0; nofine <= f_lock;
                end
            end
            if (pub_b) begin
                pend_b <= 1'b0;
                seq_c  <= seq_c + 32'd1;
                seq_nz <= 1'b1; seq_gt1 <= seq_nz;
                pts_p  <= pts_l; pfl_p <= pfl_l; d_p <= d_l;
            end
        end
    end
endmodule

// ------------------------------------------------------------
// pin_cnt3 (FW 0x0503, etapa E5): predelicka vstupu P = 2^sel (sel 0..4) pro kmitocty nad prime pocitani (~40 MHz).
//   4b citac taktovany PRIMO pinem (jediny stupen na plnem kmitoctu = c[0]); do retezu TDC jde pin (sel 0) nebo c[sel-1].
//   Cas znacky se nemeni (pridany jitter jednoho KO ~ jednotky ps, posun konstantni), pocet period = P x pocet delenych
//   hran. sel se meni jen na hranici segmentu (top_v3) a segmenty kolem zmeny jsou neplatne (hold).
//   Sonda: c[3] (/16) se pocita v domene clk VZDY (i pri sel 0) za okno 1 ms -> STM pozna kmitocet az do ~800 MHz
//   (f/16 < 50 MHz synchronizatoru) a P voli sam (fpga_v3_range_select).
// Rozlozeni: c[3:0] musi lezet u IOB pinu (25 = IOB8A, 27 = IOB11A) a hlavy retezu TDC (R26/R27 C2): kratke lokalni
//   vedeni, hodiny pinu NE na globalni sit (PRIMARY je ve v3 skoro plna) -- OVERIT po P&R (create_clock pin_a/pin_b
//   v timing_v3.sdc, mapa floorplan_svg.py). Cesta pin/c -> retez je asynchronni (false_path pres u_tdc?/sig_eff).
// ------------------------------------------------------------
module pin_cnt3 (
    input  wire        pin,
    input  wire        clk,
    input  wire        wend,                     // 1 takt na konci okna sondy (1 ms pri 100 MHz; spolecne pro oba kanaly, top_v3)
    input  wire [2:0]  sel,
    output wire        sig_out,
    output reg  [15:0] probe                     // hrany /16 za posledni okno (saturuje)
);
    reg [3:0] c = 4'd0;
    always @(posedge pin) c <= c + 4'd1;
    assign sig_out = (sel == 3'd0) ? pin : (sel == 3'd1) ? c[0] : (sel == 3'd2) ? c[1] : (sel == 3'd3) ? c[2] : c[3];
    reg [2:0]  s  = 3'b000;
    reg [15:0] pc = 16'd0;
    reg        pcf = 1'b0;                       // pc == 0xFFFF (o takt zpozdene; hrany /16 jsou nejmene 2 takty od sebe)
    initial probe = 16'd0;
    always @(posedge clk) begin
        s <= {s[1:0], c[3]};
        pcf <= (pc == 16'hFFFF);
        if (wend) begin
            probe <= pc; pc <= {15'd0, s[1] & ~s[2]};
        end else if (s[1] & ~s[2] && !pcf) pc <= pc + 16'd1;
    end
endmodule

// ------------------------------------------------------------
// pps_ides3 (FW 0x0502, etapa E4): jemny cas 1PPS. rPLL 100 -> 400 MHz (FCLK, VCO 800 MHz), CLKDIV /4 (PCLK 100 MHz),
//   IDES8 na pinu 1PPS: 8 vzorku za takt (DDR) = krok 1,25 ns = 2048 jednotek T/16384 (sigma kvantizace 0,36 ns proti
//   2,9 ns hrubeho citace). Cas = {pcnt, poloha, 11'b0}: pcnt pocita takty PCLK od uvolneni CLKDIV (v resetu PCLK stoji
//   -> asynchronni reset pcnt se uvolni bez metastability), v domene clk se k nemu pricte tick z taktu uvolneni (t_rel).
//   Vysledek lezi na ose tick_hi az na pevny posun (latence uvolneni + faze PLL), ktery se muze zmenit JEN pri novem
//   zamceni PLL -> epocha (pocet uvolneni mod 16) v PPS_STATUS bity 7:4; STM pri zmene epochy zahodi fazovou kotvu.
// Rozlozeni: IDES8 je v IOB pinu 1PPS (33 = IOB23A, spodni okraj), CLKDIV na HCLK teze strany; logika PCLK (pcnt 34 b,
//   detekce hrany, p_ts) je par desitek bunek, placer ji da k pinu. Do domeny clk jde jen prepinaci bit a 48 b drzenych
//   ~1 s (vyjimky v timing_v3.sdc) -> zadna dlouha casovana cesta pres cip.
// ------------------------------------------------------------
module pps_ides3 #(
    parameter Q0_FIRST = 1                       // 1 = Q0 je nejdrivejsi vzorek (PREDPOKLAD -- overit na desce: d v [-1, 0])
)(
    input  wire        clk,                      // clk_p0_100m (GCLK) = CLKIN rPLL i domena vystupu
    input  wire        pin,                      // gps_1pps
    input  wire [33:0] tick,                     // tick_hi
    output reg         f_tgl,                    // preklopi se s kazdym novym jemnym casem
    output reg  [47:0] f_ts,                     // [T/16384]
    output wire        f_lock,                   // PCLK bezi (PLL zamcena a uvolnena)
    output reg  [3:0]  epoch                     // pocet uvolneni PCLK (mod 16)
);
    wire fclk, lock, pclk;
    rPLL #(
        .FCLKIN("100"), .DEVICE("GW1NR-9C"),
        .IDIV_SEL(0), .FBDIV_SEL(3), .ODIV_SEL(2),            // 100 * 4 / 1 = 400 MHz, VCO = 400 * 2 = 800 MHz
        .DYN_IDIV_SEL("false"), .DYN_FBDIV_SEL("false"), .DYN_ODIV_SEL("false"),
        .PSDA_SEL("0000"), .DUTYDA_SEL("1000"), .DYN_DA_EN("false"),
        .CLKOUT_FT_DIR(1'b1), .CLKOUTP_FT_DIR(1'b1), .CLKOUT_DLY_STEP(0), .CLKOUTP_DLY_STEP(0),
        .CLKFB_SEL("internal"), .CLKOUT_BYPASS("false"), .CLKOUTP_BYPASS("false"), .CLKOUTD_BYPASS("false"),
        .DYN_SDIV_SEL(2), .CLKOUTD_SRC("CLKOUT"), .CLKOUTD3_SRC("CLKOUT")
    ) u_pll (
        .CLKOUT(fclk), .LOCK(lock), .CLKOUTP(), .CLKOUTD(), .CLKOUTD3(),
        .RESET(1'b0), .RESET_P(1'b0), .CLKIN(clk), .CLKFB(1'b0),
        .FBDSEL(6'd0), .IDSEL(6'd0), .ODSEL(6'd0), .PSDA(4'd0), .DUTYDA(4'd0), .FDLY(4'd0)
    );
    reg [1:0]  lk_s  = 2'b00;
    reg        rdy   = 1'b0;                     // reset logiky PCLK a stav do clk
    (* syn_preserve = 1 *) reg rdy_h = 1'b0;     // FW 0x0506: kopie jen pro RESETN CLKDIV a RESET IDES8 (SDC: false_path jen z ni, audit)
    reg [7:0]  t_rel = 8'd0;                     // FW 0x0506: jen dolnich 8 b ticku v taktu pred uvolnenim (zbytek z ticku pri prijmu)
    initial begin f_tgl = 1'b0; f_ts = 48'd0; epoch = 4'd0; end
    always @(posedge clk) begin
        lk_s <= {lk_s[0], lock};
        rdy  <= lk_s[1];
        rdy_h <= lk_s[1];
        if (lk_s[1] & ~rdy) begin t_rel <= tick[7:0]; epoch <= epoch + 4'd1; end
    end
    assign f_lock = rdy;
    CLKDIV #(.DIV_MODE("4"), .GSREN("false")) u_div (.HCLKIN(fclk), .RESETN(rdy_h), .CALIB(1'b0), .CLKOUT(pclk));
    wire [7:0] q;
    IDES8 #(.GSREN("false"), .LSREN("true")) u_ides (
        .D(pin), .FCLK(fclk), .PCLK(pclk), .CALIB(1'b0), .RESET(~rdy_h),
        .Q0(q[0]), .Q1(q[1]), .Q2(q[2]), .Q3(q[3]), .Q4(q[4]), .Q5(q[5]), .Q6(q[6]), .Q7(q[7]));
    wire [7:0] w = (Q0_FIRST != 0) ? q : {q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]};   // w[0] = nejdrivejsi
    // domena PCLK: prvni nabezna hrana (i na rozhrani slov), poloha 0..7 v taktu
    reg  [7:0]  pcnt  = 8'd0;                    // FW 0x0506: 8 b (dolni cast; plny cas se slozi z ticku v clk, viz nize)
    reg         lastw = 1'b0;
    reg  [1:0]  wn    = 2'd0;                    // slova PCLK od uvolneni (saturuje na 3), viz lastw nize
    reg  [10:0] p_ts  = 11'd0;                   // {pcnt[7:0], poloha}
    reg         p_tgl = 1'b0;
    reg  [2:0]  pos;
    reg         hit;
    integer     i;
    always @* begin
        hit = 1'b0; pos = 3'd0;
        for (i = 7; i >= 0; i = i - 1)
            if (w[i] && ((i == 0) ? ~lastw : ~w[i-1])) begin hit = 1'b1; pos = i[2:0]; end
    end
    always @(posedge pclk or negedge rdy) begin
        if (!rdy) begin
            pcnt <= 8'd0; lastw <= 1'b0; p_tgl <= 1'b0; wn <= 2'd0;
        end else begin
            pcnt  <= pcnt + 8'd1;
            // prvni 3 slova po uvolneni: stav pinu pred slovem neznamy (Q v resetu = 0, latence IDES8 na desce neoverena)
            // -> jako 1, hrana na Q0 se nehlasi (pin drzeny 1 pres uvolneni by dal falesnou hranu v poloze 0, nalez N1)
            lastw <= w[7] | ~&wn;
            if (~&wn) wn <= wn + 2'd1;
            if (hit) begin p_ts <= {pcnt, pos}; p_tgl <= ~p_tgl; end
        end
    end
    // do domeny clk: p_ts se drzi do dalsi hrany (~1 s) -> po 3 FF prepinaciho bitu je stabilni
    // FW 0x0506: cas hrany = tick v okamziku prijmu (do ~6 taktu po hrane) minus zpozdeni prijmu; zpozdeni = (tick[7:0] - hl)
    // mod 256, kde hl = dolnich 8 b ticku hrany = pcnt[7:0] + t_rel (PCLK pocita takty jako tick, jen s pevnym posunem).
    // Dva takty: 1. {tick, poloha} do f_ts, 2. oprava horni casti o vypujcku a dolnich 8 b na hl, preklopeni f_tgl.
    reg [2:0] t_s = 3'b000;
    reg [2:0] fix = 3'd0;
    reg       bw  = 1'b0;
    // 2026-10-24 (casovani, pnr_seeds: bw -> f_ts[47:35] ve 4 z 8 sestaveni): f_ts[47:35] - 1 jako registr fm1 (f_ts[47:35]
    // se od nacteni v taktu prijmu do fix 3 nemeni) -> ve fix 3 vypujcka jen vybira, zadny 13b odecet za bw
    reg [12:0] fm1 = 13'd0;
    reg [7:0] hl  = 8'd0;                        // p_ts a t_rel se meni nejpozdeji o 3 takty driv, nez se hl pouzije
    always @(posedge clk) begin
        hl  <= p_ts[10:3] + t_rel;
        fm1 <= f_ts[47:35] - 13'd1;
        t_s <= {t_s[1:0], p_tgl};
        if (!rdy) begin t_s <= 3'b000; fix <= 3'd0; end
        else if (t_s[2] ^ t_s[1]) begin
            f_ts <= {tick, p_ts[2:0], 11'd0};
            fix  <= 3'd1;
        end else if (fix == 3'd1) begin
            bw  <= (f_ts[21:14] < hl);
            fix <= 3'd2;
        end else if (fix == 3'd2) begin             // dolnich 13 b horni casti - vypujcka, prenos do fix 3
            {bw, f_ts[34:22]} <= {1'b0, f_ts[34:22]} - {13'd0, bw};
            f_ts[21:14]       <= hl;
            fix <= 3'd3;
        end else if (fix == 3'd3) begin             // hornich 13 b - prenos
            if (bw) f_ts[47:35] <= fm1;
            fix   <= 3'd0;
            f_tgl <= ~f_tgl;
        end
    end
endmodule

// ------------------------------------------------------------
// lut_wr_sync3 (2026-10-24, casovani): v3 kopie lut_wr_sync (spi_app.v, sdileny s v2 -- beze zmeny) se ZVLASTNIM
//   vystupnim registrem adresy a dat pro kazdy kanal TDC. Puvodni jedna sada lw_a_q / lw_n_q budila tabulky obou
//   kanalu (BSRAM u_tdca a u_tdcb lezi daleko od sebe) -> cesta lw_n_q -> u_tdca/lut DI 9,2 ns, z toho vetsina vedeni.
//   Kopie *_a / *_b maji stejny vstup (lwa_s1 / lwn_s1) -> v kazdem taktu shodne s puvodnim lw_a_q / lw_n_q.
// ------------------------------------------------------------
module lut_wr_sync3 (
    input  wire        clk,              // clk_p0_100m
    input  wire        lw_tgl, lw_commit_tgl, lw_ch,
    input  wire [9:0]  lw_a,
    input  wire [13:0] lw_neg,
    output wire        lw_en_a, lw_en_b, // zapis polozky (1 takt) do kanalu A / B
    output wire        lw_cm_p,          // potvrzeni tabulky (1 takt, oba kanaly)
    (* syn_preserve = 1 *) output reg [9:0]  lw_a_qa,   // adresa pro kanal A (kopie; syn_preserve, jinak Gowin kopie slouci)
    (* syn_preserve = 1 *) output reg [9:0]  lw_a_qb,   // ... pro kanal B
    (* syn_preserve = 1 *) output reg [13:0] lw_n_qa,   // data pro kanal A
    (* syn_preserve = 1 *) output reg [13:0] lw_n_qb    // data pro kanal B
);
    reg [2:0]  lwt_s = 3'b000, lwc_s = 3'b000;
    reg [9:0]  lwa_s1 = 10'd0;
    reg [13:0] lwn_s1 = 14'd0;
    reg [1:0]  lwh_s = 2'b00;
    initial begin lw_a_qa = 10'd0; lw_a_qb = 10'd0; lw_n_qa = 14'd0; lw_n_qb = 14'd0; end
    always @(posedge clk) begin
        lwt_s  <= {lwt_s[1:0], lw_tgl};
        lwc_s  <= {lwc_s[1:0], lw_commit_tgl};
        lwa_s1 <= lw_a;   lw_a_qa <= lwa_s1; lw_a_qb <= lwa_s1;
        lwn_s1 <= lw_neg; lw_n_qa <= lwn_s1; lw_n_qb <= lwn_s1;
        lwh_s  <= {lwh_s[0], lw_ch};
    end
    wire lw_wr_p = lwt_s[2] ^ lwt_s[1];
    assign lw_cm_p = lwc_s[2] ^ lwc_s[1];
    assign lw_en_a = lw_wr_p & ~lwh_s[1];
    assign lw_en_b = lw_wr_p &  lwh_s[1];
endmodule

// ------------------------------------------------------------
// cal_seq3: kalibrace TDC kanalu STRIDAVE. Pozadavky (po zapnuti oba, SET_CONFIG 0x02 podle masky, opakovani po
//   selhani) se frontuji; sber histogramu bezi vzdy jen v jednom kanalu (sdileny ring oscilator, druhy kanal meri).
//   Kanal ve stavu cekani na tabulku (s_wait) uz neni busy -> muze zacit sber druheho kanalu.
// ------------------------------------------------------------
module cal_seq3 (
    input  wire clk,
    input  wire go_a, go_b,          // 1 takt: pozadavek na kalibraci kanalu
    input  wire busy_a, busy_b,      // tdc_chan.cal_busy
    output reg  req_a, req_b         // 1 takt: tdc_chan.cal_req
);
    reg pa = 1'b0, pb = 1'b0;        // ceka ve fronte
    reg act = 1'b0;                  // probiha (od req do konce busy)
    reg [3:0] gd = 4'd0;             // ochrana: busy nabehne az par taktu po req
    initial begin req_a = 1'b0; req_b = 1'b0; end
    always @(posedge clk) begin
        req_a <= 1'b0; req_b <= 1'b0;
        if (go_a) pa <= 1'b1;
        if (go_b) pb <= 1'b1;
        if (gd != 4'd0) gd <= gd - 4'd1;
        if (act) begin
            if (gd == 4'd0 && !busy_a && !busy_b) act <= 1'b0;
        end else if (!busy_a && !busy_b) begin
            if (pa)      begin req_a <= 1'b1; pa <= go_a; act <= 1'b1; gd <= 4'd8; end
            else if (pb) begin req_b <= 1'b1; pb <= go_b; act <= 1'b1; gd <= 4'd8; end
        end
    end
endmodule

// ------------------------------------------------------------
// raw_cap3: RAW_CAPTURE (TYPE 0x0B) -- po pozadavku (prepinaci bit z 10 MHz) ulozi 512 po sobe jdoucich znacek
//   vybraneho kanalu (ts_ps, 48 b) do BSRAM; cte aplikace stranou druh 3 (dve polozky u24 na znacku).
// ------------------------------------------------------------
// ------------------------------------------------------------
// psram_hb3 (FW 0x0505): radic vestavene PSRAM GW1NR-9 (HyperBus x8, jeden ze dvou cipu = 4 MB) bez PLL.
//   Takt clk 100 MHz = jeden DDR takt sbernice (DQ se meni s nabeznou hranou clk), CK = 50 MHz z FF na SESTUPNE hrane
//   clk -> hrana CK je uprostred kazdeho bajtu (zarovnani na stred, 5 ns na obe strany). Prenos ~100 MB/s v datech.
//   Prikaz/adresa (CA) 6 bajtu, pak cekani WAIT_B bajtovych taktu (latence, viz nize), pak data (davka BURST bajtu).
//   CTENI se zarovnava podle RWDS: PSRAM ho pri cteni prepina s kazdym bajtem -> bajt se bere, kdyz se vzorek RWDS
//   zmenil (presna latence pri cteni tedy nevadi). Behem CA a latence drzi PSRAM RWDS jako priznak latence (pevna
//   dvojita latence = 1), pred prvnim bajtem ho stahne do 0 -> bajty se pocitaji az od prvniho vzorku RWDS = 0.
//   ZAPIS musi trefit latenci PSRAM presne: po zapnuti radic zkusi
//   WAIT_B = W0 .. W0+31 (zapis vzoru, cteni, porovnani) a vezme prvni, ktery sedi -> zadna zavislost na tom, jak
//   presne datasheet pocita latenci od CA. Vysledek: rdy, wsel (index latence), bad (zadna nesedi).
//   Audit 2026-10-23 (F-0263): latence je v celych taktech CK (2 bajty), sedi tedy jen kazdy druhy WAIT_B; puvodnich
//   8 pokusu (16..23) pokrylo jen 4 latence a vychozi pevna dvojita latence (~22 / 26 bajtu) mohla vyjit mimo.
//   4..35 pokryje latenci 3..7 taktu jednoduchou i dvojitou.
//   Audit 2026-10-23 (F-0264): faze vzorkovani cteni. PSRAM meni DQ/RWDS tCKD po hrane CK (= sestupna hrana clk); vzorek
//   na nabezne hrane clk je 5 ns po hrane CK -> pri tCKD ~5 ns padne do zmeny. Radic proto vzorkuje i na sestupne hrane
//   (tesne pred dalsi hranou CK) a kalibrace zkusi obe faze: rph = 0 (nabezna) pokusy 0..31, pak rph = 1 (sestupna).
//   CS# nejvys ~1,7 us na davku (tCSM 4 us = obnova PSRAM): BURST = 128 B.
// Rozlozeni: piny PSRAM jsou vyhrazene (cip v pouzdre), radic je ~150 bunek; datova cesta (FIFO zapisu, okno RAW)
//   v BSRAM. Jedina cesta mezi clk a vyvody je registr -> pin; CK z FF na sestupne hrane.
// 🔴 NEOVERENO na krzemiku: jmena vyvodu (O_psram_*, IO_psram_*), casovani tCKD/tDSV PSRAM, inicializace (CR0 se
//   nemeni = vychozi pevna dvojita latence). Model v sim/tb_top_v3.sv (hb_psram_model) je podle obecne specifikace
//   HyperBus; proto samokalibrace latence zapisu a samotest pri startu (PSRAM_ST v hlavicce).
// ------------------------------------------------------------
module psram_hb3 #(
    parameter INIT_CYC = 15000,       // cekani po resetu PSRAM [takty clk] (tVCS 150 us); simulace kratsi
    parameter W0       = 4            // nejkratsi zkousena latence zapisu [bajtove takty od konce CA]
)(
    input  wire        clk,
    // uzivatel (domena clk): davka BURST bajtu na adresu (bajtova, nasobek BURST)
    input  wire        req,           // 1 takt: start davky (jen kdyz rdy & ~busy)
    input  wire        we,            // 1 = zapis, 0 = cteni
    input  wire [21:0] addr,          // bajtova adresa (4 MB)
    output wire        wtake,         // zapis: v tomto taktu se bere wbyte (kombinacni; dalsi bajt o takt pozdeji)
    input  wire [7:0]  wbyte,
    output reg         rvalid,        // cteni: bajt rbyte platny
    output reg  [7:0]  rbyte,
    output reg         busy,
    output reg         rdy,           // inicializace a kalibrace hotova
    output reg         bad,           // zadna latence zapisu nesedi
    output reg  [4:0]  wsel,          // zvolena latence zapisu (W0 + wsel)
    output reg         rph,           // zvolena faze vzorkovani cteni (0 = nabezna hrana clk, 1 = sestupna)
    // PSRAM (cip 0)
    output reg         ps_ck,
    output wire        ps_ck_n,
    output reg         ps_cs_n,
    output reg         ps_rst_n,
    inout  wire [7:0]  ps_dq,
    inout  wire        ps_rwds
);
    localparam BURST = 128;
    reg        dq_oe = 1'b0, rw_oe = 1'b0;
    reg [7:0]  dq_o = 8'd0;
    reg        rw_o = 1'b0;
    assign ps_dq   = dq_oe ? dq_o : 8'hzz;
    assign ps_rwds = rw_oe ? rw_o : 1'bz;
    assign ps_ck_n = ~ps_ck;
    initial begin ps_ck = 1'b0; ps_cs_n = 1'b1; ps_rst_n = 1'b0; rvalid = 1'b0; rbyte = 8'd0;
                  busy = 1'b1; rdy = 1'b0; bad = 1'b0; wsel = 5'd0; rph = 1'b0; end
    // CK: bezi jen behem transakce (ck_en), preklapi na sestupne hrane clk
    reg ck_en = 1'b0;
    always @(negedge clk) ps_ck <= ck_en ? ~ps_ck : 1'b0;

    localparam [2:0] S_INIT = 3'd0, S_IDLE = 3'd1, S_CA = 3'd2, S_WAIT = 3'd3, S_DATA = 3'd4, S_END = 3'd5;
    reg [2:0]  st = S_INIT;
    reg [15:0] cnt = 16'd0;
    reg [47:0] ca = 48'd0;
    reg        cur_we = 1'b0;
    reg [7:0]  nb = 8'd0;             // bajty davky
    reg [1:0]  rs = 2'b00;            // vzorky RWDS (cteni)
    reg        rarm = 1'b0;           // cteni: RWDS uz byl 0 (dalsi zmena = prvni bajt, ne konec priznaku latence)
    reg [7:0]  dqs = 8'd0;
    reg        rwn = 1'b0;            // faze 1: vzorky RWDS/DQ na sestupne hrane clk (do domeny clk o pul taktu pozdeji)
    reg [7:0]  dqn = 8'd0;
    always @(negedge clk) begin rwn <= ps_rwds; dqn <= ps_dq; end
    // kalibrace: 0 = zapis vzoru, 1 = cteni a porovnani
    reg        cal = 1'b1, cal_rd = 1'b0, cal_ok = 1'b1;
    reg [4:0]  ctry = 5'd0;
    wire [7:0] pat = {nb[6:0], 1'b1} ^ {ctry, 3'h5} ^ {rph, 7'd0};   // vzor zavisly na pokusu (stary vzor neprojde)
    wire [7:0] wait_b = W0 + {3'd0, (cal ? ctry : wsel)};
    assign wtake = (st == S_DATA) && cur_we && !cal;
    // CA (HyperBus): [47] R/W# (1 cteni), [46] adresni prostor (0 pamet), [45] linearni davka, [44:16] horni adresa
    // (slova 16 b: A[21:1] -> radek/sloupec), [2:0] dolni sloupec
    function [47:0] mk_ca(input rd, input [21:0] a);
        mk_ca = {rd, 1'b0, 1'b1, 11'd0, a[21:4], 13'd0, a[3:1]};   // 1+1+1+11+18+13+3 = 48 b
    endfunction
    always @(posedge clk) begin
        rvalid <= 1'b0;
        rs     <= {rs[0], rph ? rwn : ps_rwds};
        dqs    <= rph ? dqn : ps_dq;
        case (st)
            S_INIT: begin                                     // reset PSRAM, cekani tVCS
                cnt <= cnt + 16'd1;
                if (cnt == 16'd100) ps_rst_n <= 1'b1;
                if (cnt == INIT_CYC[15:0]) begin st <= S_IDLE; cnt <= 16'd0; end
            end
            S_IDLE: begin
                busy <= cal;
                if (cal || req) begin
                    cur_we <= cal ? ~cal_rd : we;
                    ca     <= mk_ca(cal ? cal_rd : ~we, cal ? {10'd0, ctry, 7'd0} : addr);
                    busy <= 1'b1; st <= S_CA; cnt <= 16'd0; nb <= 8'd0; rarm <= 1'b0;
                    ps_cs_n <= 1'b0; dq_oe <= 1'b1;            // CK az s prvnim bajtem CA (hrana uprostred bajtu)
                end
            end
            S_CA: begin                                       // 6 bajtu CA, MSB prvni
                dq_o <= ca[47:40]; ca <= {ca[39:0], 8'd0}; ck_en <= 1'b1;
                cnt  <= cnt + 16'd1;
                if (cnt == 16'd5) begin st <= S_WAIT; cnt <= 16'd0; end
            end
            S_WAIT: begin
                dq_oe <= cur_we ? 1'b1 : 1'b0; dq_o <= 8'd0;
                cnt <= cnt + 16'd1;
                if (cur_we && cnt == {8'd0, wait_b} - 16'd1) begin rw_oe <= 1'b1; rw_o <= 1'b0; st <= S_DATA; end
                if (!cur_we) st <= S_DATA;                    // cteni: data podle RWDS
            end
            S_DATA: begin
                cnt <= cnt + 16'd1;
                if (cur_we) begin
                    dq_o  <= cal ? pat : wbyte;
                    nb    <= nb + 8'd1;
                    if (nb == BURST - 1) st <= S_END;
                end else begin
                    if (!rarm) begin
                        if (rs[0] == 1'b0) rarm <= 1'b1;      // konec priznaku latence (RWDS = 1 behem CA)
                    end else if (rs[1] ^ rs[0]) begin         // RWDS se zmenil -> novy bajt
                        nb <= nb + 8'd1;
                        if (cal) begin if (dqs != pat) cal_ok <= 1'b0; end
                        else begin rvalid <= 1'b1; rbyte <= dqs; end
                        if (nb == BURST - 1) st <= S_END;
                    end
                    if (cnt == 16'd300) begin cal_ok <= 1'b0; st <= S_END; end   // PSRAM neodpovida (CS# < tCSM 4 us)
                end
            end
            S_END: begin
                ps_cs_n <= 1'b1; ck_en <= 1'b0; dq_oe <= 1'b0; rw_oe <= 1'b0;
                st <= S_IDLE;
                if (cal) begin
                    if (!cal_rd) cal_rd <= 1'b1;              // vzor zapsan -> precist
                    else begin
                        cal_rd <= 1'b0;
                        if (cal_ok) begin cal <= 1'b0; rdy <= 1'b1; wsel <= ctry; busy <= 1'b0; end
                        else if (ctry == 5'd31 && rph) begin cal <= 1'b0; bad <= 1'b1; busy <= 1'b0; end
                        else if (ctry == 5'd31) begin rph <= 1'b1; ctry <= 5'd0; cal_ok <= 1'b1; end
                        else begin ctry <= ctry + 5'd1; cal_ok <= 1'b1; end
                    end
                end else busy <= 1'b0;
            end
            default: st <= S_IDLE;
        endcase
    end
endmodule

// FW 0x0505: rezim 1 = HLUBOKY zaznam az 699 050 znacek (6 B) do vestavene PSRAM (psram_hb3, 4 MB) pres FIFO 512 znacek
//   v BSRAM a davky 128 B (~80 MB/s -> plynule do ~13 Mznacek/s, jinak FIFO pretece: priznak, znacky se zahodi);
//   rezim 2 = nacist okno w (znacky w*512 .. w*512+511) z PSRAM do BSRAM -> stranky druhu 3 jako u rezimu 0. Rezim 0 beze
//   zmeny.
//   Audit 2026-10-23 (F-0266): rezimy se vylucuji -- rezim 0 se behem hlubokeho zaznamu / nacitani okna odmitne (psal by
//   do tehoz BSRAM a prepnul kanal bezicimu zaznamu), rezim 1 a 2 rezim 0 ukonci, rezim 1 pri bezicim zaznamu = STOP
//   (zbytek FIFO se dopise). Kazdy prijaty prikaz zvysi pocitadlo dack (Grayuv kod, cte se v 10 MHz bez synchronizace)
//   -> STM pozna odmitnuty prikaz a nevezme stara data; bit7 = kanal posledniho prijateho zaznamu. Rozlozeni: FIFO a okno jsou BSRAM, radic PSRAM ~150 bunek u vyhrazenych vyvodu PSRAM.
module raw_cap3 #(
    parameter PS_INIT = 15000                    // cekani po resetu PSRAM [takty] (simulace kratsi)
)(
    input  wire        clk,
    input  wire        req_tgl,      // z 10 MHz (2FF + hrana)
    input  wire        req_ch,       // kvazistaticke
    input  wire [1:0]  req_mode,     // FW 0x0505: 0 = 512 do BSRAM, 1 = hluboky do PSRAM, 2 = nacist okno z PSRAM
    input  wire [23:0] req_arg,      // rezim 1: pocet znacek, rezim 2: cislo okna (kvazistaticke)
    input  wire        tv_a, tv_b,   // znacka kanalu (ts_valid & platny kanal)
    input  wire        gap_a, gap_b, // audit F-0270: kanal ted neznackuje (hold, maska pri CS) -> v zaznamu bude mezera
    input  wire [47:0] ts_a, ts_b,
    output reg  [9:0]  n,            // pocet ulozenych znacek (0..512; kvazistaticke pro 10 MHz)
    output reg  [23:0] dn,           // hluboky zaznam: znacek v PSRAM
    output wire [7:0]  dst,          // bit0 PSRAM pripravena, bit1 kalibrace zapisu selhala, bit2 zaznam bezi, bit3 FIFO
                                     // preteklo, bit4 nacita se okno, bit5 okno nacteno, bit6 v zaznamu jsou mezery,
                                     // bit7 okno s chybou cteni
    output wire [5:0]  dwsel,        // bity 4:0 zvolena latence zapisu PSRAM (psram_hb3 W0 + n), bit5 faze cteni
    output reg  [15:0] dwin,         // posledni (nacitane) okno
    output wire [7:0]  dack,         // bit7 kanal zaznamu, bity 3:0 pocet prijatych prikazu T_RAW (Gray, mod 16)
    input  wire        rclk,
    input  wire [8:0]  rk,
    output reg  [47:0] rq,
    output wire        ps_ck, ps_ck_n, ps_cs_n, ps_rst_n,
    inout  wire [7:0]  ps_dq,
    inout  wire        ps_rwds
);
    localparam [23:0] DMAX = 24'd699050;         // 4 MB / 6 B
    reg [2:0]  rs = 3'b000;
    reg [1:0]  chs = 2'b00, md1 = 2'b00, md2 = 2'b00;
    reg [23:0] ar1 = 24'd0, ar2 = 24'd0;
    reg        run = 1'b0, ch = 1'b0;
    reg [47:0] mem [0:511];
    reg [3:0]  dacc = 4'd0;                      // prijate prikazy (binarne)
    initial begin n = 10'd0; rq = 48'd0; dn = 24'd0; dwin = 16'd0; end
    assign dack = {ch, 3'd0, dacc ^ {1'b0, dacc[3:1]}};
    // ---- PSRAM ----
    wire       p_wtake, p_rvalid, p_busy, p_rdy, p_bad;
    wire [7:0] p_rbyte;
    wire [4:0] p_wsel;
    wire       p_rph;
    assign dwsel = {p_rph, p_wsel};
    reg        p_req = 1'b0, p_we = 1'b0, p_wait = 1'b0;   // p_wait: zadost odeslana, radic jeste neni busy
    reg [21:0] p_addr = 22'd0;
    reg [7:0]  p_wbyte;
    psram_hb3 #(.INIT_CYC(PS_INIT)) u_ps (.clk(clk), .req(p_req), .we(p_we), .addr(p_addr), .wtake(p_wtake), .wbyte(p_wbyte),
        .rvalid(p_rvalid), .rbyte(p_rbyte), .busy(p_busy), .rdy(p_rdy), .bad(p_bad), .wsel(p_wsel), .rph(p_rph),
        .ps_ck(ps_ck), .ps_ck_n(ps_ck_n), .ps_cs_n(ps_cs_n), .ps_rst_n(ps_rst_n), .ps_dq(ps_dq), .ps_rwds(ps_rwds));
    wire       p_free = p_rdy & ~p_busy & ~p_req & ~p_wait;
    // ---- hluboky zaznam: FIFO -> serializator po bajtech -> davky 128 B ----
    reg        drun = 1'b0, dfin = 1'b0, dovf = 1'b0;
    // audit 2026-10-23 (F-0270): znacky pri hold (kalibrace, zmena P) a pri masce CS (vychozi zapnuta, kazda transakce
    // SPI) v zaznamu chybi bez stopy -> rozdil 2P misto P. dgap = behem zaznamu (rezim 0 nebo 1) kanal aspon chvili
    // neznackoval.
    reg        dgap = 1'b0;
    reg [23:0] dreq = 24'd0;
    reg [47:0] fifo [0:511];
    reg [8:0]  fwp = 9'd0, frp = 9'd0;
    reg [9:0]  fcnt = 10'd0;                     // slov ve FIFO (jeste nevyzvednutych)
    reg [47:0] fq = 48'd0, nw = 48'd0, sw = 48'd0;
    reg        fq_v = 1'b0, nw_v = 1'b0, sw_v = 1'b0;
    reg [2:0]  sbi = 3'd0;                       // index bajtu v sw (0 = nejvyssi)
    reg [21:0] dwa = 22'd0;
    wire [12:0] avail = {fcnt, 3'd0} - {2'd0, fcnt, 1'b0} + (sw_v ? 13'd6 - {10'd0, sbi} : 13'd0)
                        + (nw_v ? 13'd6 : 13'd0) + (fq_v ? 13'd6 : 13'd0);    // bajtu k zapisu
    always @* case (sbi)
        3'd0: p_wbyte = sw[47:40]; 3'd1: p_wbyte = sw[39:32]; 3'd2: p_wbyte = sw[31:24];
        3'd3: p_wbyte = sw[23:16]; 3'd4: p_wbyte = sw[15:8];  default: p_wbyte = sw[7:0];
    endcase
    // ---- okno z PSRAM ----
    reg        wl = 1'b0, wdone = 1'b0, werr = 1'b0;
    reg [4:0]  wb = 5'd0;                        // odeslanych davek (24 = 3072 B = 512 znacek)
    reg [21:0] wra = 22'd0;
    reg [39:0] asm_ = 40'd0;
    reg [2:0]  abi = 3'd0;
    reg [9:0]  widx = 10'd0, wlim = 10'd0;
    wire [31:0] wbase = {16'd0, ar2[15:0]} << 9;                                    // prvni znacka okna
    wire [31:0] wrem  = ({8'd0, dn} > wbase) ? ({8'd0, dn} - wbase) : 32'd0;
    assign dst = {werr, dgap, wdone, wl, dovf, drun, p_bad, p_rdy};
    wire       tv  = ch ? tv_b : tv_a;
    wire [47:0] ts = ch ? ts_b : ts_a;
    wire       push = drun && !dfin && tv && (fcnt != 10'd512);
    wire       pop  = !fq_v && (fcnt != 10'd0) && (!nw_v || !sw_v);
    always @(posedge clk) begin
        rs  <= {rs[1:0], req_tgl};
        chs <= {chs[0], req_ch};
        md1 <= req_mode; md2 <= md1; ar1 <= req_arg; ar2 <= ar1;
        p_req <= 1'b0;
        if (p_busy) p_wait <= 1'b0;
        if (rs[2] ^ rs[1]) begin
            if (md2 == 2'd0 && !drun && !wl) begin run <= 1'b1; ch <= chs[1]; n <= 10'd0; dacc <= dacc + 4'd1; dgap <= 1'b0; end
            else if (md2 == 2'd1 && drun) begin dfin <= 1'b1; dacc <= dacc + 4'd1; end   // STOP
            else if (md2 == 2'd1 && p_rdy && !wl) begin
                run <= 1'b0; wdone <= 1'b0; dacc <= dacc + 4'd1;
                drun <= 1'b1; dfin <= (ar2 == 24'd0); dovf <= 1'b0; ch <= chs[1]; dn <= 24'd0; dgap <= 1'b0;
                dreq <= (ar2 > DMAX) ? DMAX : ar2; dwa <= 22'd0;
                fwp <= 9'd0; frp <= 9'd0; fcnt <= 10'd0; fq_v <= 1'b0; nw_v <= 1'b0; sw_v <= 1'b0; sbi <= 3'd0;
            end else if (md2 == 2'd2 && p_rdy && !drun && !wl) begin
                run <= 1'b0; dacc <= dacc + 4'd1;
                wl <= 1'b1; wdone <= 1'b0; werr <= 1'b0; dwin <= ar2[15:0]; wb <= 5'd0; wra <= ar2[10:0] * 22'd3072;
                widx <= 10'd0; abi <= 3'd0; n <= 10'd0;
                wlim <= (wrem >= 32'd512) ? 10'd512 : wrem[9:0];
            end
        end else if (run && tv) begin
            n <= n + 10'd1;
            if (n == 10'd511) run <= 1'b0;
        end else if ((run || (drun && !dfin)) && (ch ? gap_b : gap_a)) dgap <= 1'b1;
        // hluboky zaznam: zapis do FIFO
        if (drun && !dfin && tv) begin
            if (fcnt == 10'd512) dovf <= 1'b1;
            else begin
                fifo[fwp] <= ts; fwp <= fwp + 9'd1;
                dn <= dn + 24'd1;
                if (dn + 24'd1 == dreq) dfin <= 1'b1;
            end
        end
        fcnt <= fcnt + {9'd0, push} - {9'd0, pop};
        // vyzvednuti z FIFO (BSRAM: adresa -> data o takt pozdeji) a dva stupne pred serializatorem
        if (pop) begin fq <= fifo[frp]; frp <= frp + 9'd1; fq_v <= 1'b1; end
        if (fq_v && !nw_v) begin nw <= fq; nw_v <= 1'b1; fq_v <= 1'b0; end
        if (!sw_v && nw_v && !(fq_v && !nw_v)) begin sw <= nw; sw_v <= 1'b1; nw_v <= 1'b0; sbi <= 3'd0; end
        if (p_wtake) begin
            if (sbi == 3'd5) begin
                sbi <= 3'd0;
                if (nw_v) begin sw <= nw; nw_v <= 1'b0; end else sw_v <= 1'b0;
            end else if (sw_v) sbi <= sbi + 3'd1;
        end
        if (drun && p_free && (avail >= 13'd128 || (dfin && avail != 13'd0))) begin
            p_req <= 1'b1; p_wait <= 1'b1; p_we <= 1'b1; p_addr <= dwa; dwa <= dwa + 22'd128;
        end
        if (drun && dfin && p_free && avail == 13'd0) drun <= 1'b0;
        // okno: 24 davek cteni, bajty -> znacky do BSRAM
        if (wl) begin
            if (wb != 5'd24 && p_free) begin
                p_req <= 1'b1; p_wait <= 1'b1; p_we <= 1'b0; p_addr <= wra; wra <= wra + 22'd128; wb <= wb + 5'd1;
            end
            if (p_rvalid) begin
                asm_ <= {asm_[31:0], p_rbyte};
                if (abi == 3'd5) begin
                    abi <= 3'd0;
                    widx <= widx + 10'd1;
                end else abi <= abi + 3'd1;
            end
            // audit 2026-10-23 (F-0268): konec po 24. davce i kdyz nedosly vsechny bajty (timeout cteni) -- driv wl zustal
            // 1 navzdy a kazdy dalsi rezim 1 / 2 se odmital az do nove konfigurace FPGA. Chybne okno: bit7, 0 znacek.
            if (wb == 5'd24 && p_free) begin
                wl <= 1'b0; wdone <= 1'b1; werr <= (widx != 10'd512); n <= (widx == 10'd512) ? wlim : 10'd0;
            end
        end
    end
    // audit 2026-10-23 (F-0267): jeden zapisovy port BSRAM (rezim 0 a nacitani okna se vylucuji, F-0266) -- dve prirazeni
    // do mem s ruznou adresou v jednom bloku nemusi GowinSynthesis odvodit jako BSRAM (512 x 48 b v logice se nevejde)
    wire       m_w0 = !(rs[2] ^ rs[1]) && run && tv;
    wire       m_w2 = wl && p_rvalid && (abi == 3'd5) && (widx < wlim);
    always @(posedge clk) if (m_w0 | m_w2) mem[m_w2 ? widx[8:0] : n[8:0]] <= m_w2 ? {asm_, p_rbyte} : ts;
    always @(posedge rclk) rq <= mem[rk];
endmodule

// ------------------------------------------------------------
// raw_cap3l (FW 0x0506, rozbor CLS 2026-10-23): RAW_CAPTURE jen v rezimu 0 (512 znacek jednoho kanalu do BSRAM), bez PSRAM
//   a hlubokeho zaznamu. Stejne porty a chovani jako raw_cap3 pro rezim 0; rezimy 1 a 2 se odmitnou BEZ potvrzeni (dack se
//   nezvysi -> STM pozna odmitnuty prikaz), dn / dwin / dwsel = 0, dst jen bit6 (mezery v zaznamu). Zdroj: PSRAM a FIFO
//   hlubokeho zaznamu stoji ~500 registru a ~140 CLS (docs/audit/2026-10-23_studie-cls-v3.md); pro hluboky zaznam se
//   sestavuje -DDEEP_RAW (raw_cap3).
// ------------------------------------------------------------
module raw_cap3l (
    input  wire        clk,
    input  wire        req_tgl,
    input  wire        req_ch,
    input  wire [1:0]  req_mode,
    input  wire [23:0] req_arg,
    input  wire        tv_a, tv_b,
    input  wire        gap_a, gap_b,
    input  wire [47:0] ts_a, ts_b,
    output reg  [9:0]  n,
    output wire [23:0] dn,
    output wire [7:0]  dst,
    output wire [5:0]  dwsel,
    output wire [15:0] dwin,
    output wire [7:0]  dack,
    input  wire        rclk,
    input  wire [8:0]  rk,
    output reg  [47:0] rq
);
    reg [2:0]  rs = 3'b000;
    reg        chs0 = 1'b0, chs1 = 1'b0;
    reg [1:0]  md1 = 2'b00, md2 = 2'b00;
    reg        run = 1'b0, ch = 1'b0, dgap = 1'b0;
    reg [3:0]  dacc = 4'd0;
    reg [47:0] mem [0:511];
    initial begin n = 10'd0; rq = 48'd0; end
    assign dn = 24'd0;
    assign dwsel = 6'd0;
    assign dwin = 16'd0;
    assign dst = {1'b0, dgap, 6'd0};
    assign dack = {ch, 3'd0, dacc ^ {1'b0, dacc[3:1]}};
    wire        tv = ch ? tv_b : tv_a;
    wire [47:0] ts = ch ? ts_b : ts_a;
    always @(posedge clk) begin
        rs   <= {rs[1:0], req_tgl};
        chs0 <= req_ch; chs1 <= chs0;
        md1  <= req_mode; md2 <= md1;
        if (rs[2] ^ rs[1]) begin
            if (md2 == 2'd0) begin run <= 1'b1; ch <= chs1; n <= 10'd0; dacc <= dacc + 4'd1; dgap <= 1'b0; end
        end else if (run && tv) begin
            n <= n + 10'd1;
            if (n == 10'd511) run <= 1'b0;
        end else if (run && (ch ? gap_b : gap_a)) dgap <= 1'b1;
    end
    wire m_w0 = !(rs[2] ^ rs[1]) && run && tv;
    always @(posedge clk) if (m_w0) mem[n[8:0]] <= ts;
    always @(posedge rclk) rq <= mem[rk];
endmodule

// ------------------------------------------------------------
// Vychozi AUTOCAL pro tdc_chan3 (stejny blok jako na zacatku tdc.v; v3_core.v se preklada pred tdc.v)
// ------------------------------------------------------------
`ifndef TDC_AUTOCAL_DEF
 `ifdef SIM_AUTOCAL
  `define TDC_AUTOCAL_DEF 1
 `else
  `define TDC_AUTOCAL_DEF 0
 `endif
`endif

// ------------------------------------------------------------
// tdc_chan3 (2026-10-25, plocha): v3 KOPIE tdc_chan z tdc.v (tdc.v sdili bitstream v2 a je zamceny). Shodna az na jedno misto:
//   horni cast znacky ts_hq (TSPIPE = 1, DUAL = 0, EXT_TR = 0) bez vyberu tr1 / tr2 -- viz "ts_hq bez vyberu" nize. Retez
//   (tdc_chain), dekoder (tdc_dec), kalibrace, tabulka a histogram jsou beze zmeny (moduly z tdc.v, kod kanalu doslova).
// tdc_chan: kanal = retez + zmrazeni + dekoder + kalibrace + presny cas
//   Rozhrani k oknu (win_recip):
//     rise_c   : kazda nabezna hrana (hrube, 1 takt)
//     rise_s   : tataz hrana o takt pozdeji (pro POCITANI; uz po 2 FF, tedy bez metastability)
//     want     : okno ceka na presny cas pristi hrany
//     trig_ack : tato hrana (rise_c) spustila presny cas (jen kdyz want)
//     ts_valid : ~8 taktu po trig_ack (TSPIPE = 1: o takt pozdeji): ts_ps = cas teto hrany [T_clk/16384]
//   ts_ps = tick(cyklus spusteni + const) - V[kod]; posun je stejny pro VSECHNY presne casy obou
//   kanalu, v rozdilech (okno) se kruti. U DUAL=1 je V pro sadu R = T + tau, pro sadu F = w_lo + tau
//   (cumulativni poradi R pred F), viz tabulka nize -- tedy casy z obou sad jsou na jedne ose.
// ------------------------------------------------------------
module tdc_chan3 #(
    parameter EXT_TR = 0,             // optimalizace 5 (2026-10-20): 1 = tr1/tr2 z top (sdilene obema kanaly, -68 FF)
    parameter CAL_LOG2 = 20,        // kalibrace z 2^CAL_LOG2 udalosti (1M: ~1/4 casu, chyba bunky ~1 ps)
    parameter TAPS     = 256,       // vzorkovanych tapu (nasobek 32)
    parameter STRIDE   = 2,         // ALU na vzorek
    parameter KD       = 2,         // tap detekce hrany
    parameter DUAL     = 0,         // 1 = i sestupna hrana hodin
    parameter NTAP     = TAPS,      // fyzicky postavenych tapu (<= TAPS), viz tdc_chain
    parameter AUTOCAL  = `TDC_AUTOCAL_DEF,  // 1 = (jen simulace) tabulku pocita FPGA; 0 = pocita a zapisuje STM
    // Protokol v3 (docs/PROTOKOL_V3.md kap. 6.3): 1 = DVE BANKY tabulky. STM pise do neaktivni banky, potvrzeni ji aktivuje
    // az na hranici segmentu (bank_sw); prekalibrace s platnou tabulkou nemaze cal_valid -> po sberu histogramu kanal meri
    // dal se starou bankou, dokud STM novou nezapise a nepotvrdi. 0 = jako v2 (jedna tabulka, top.v).
    parameter BANKS    = 0,
    // Rozlozeni (audit 2026-10-23): 1 = horni cast znacky (vyber tr1/tr2 bitem lut_q3[15]) a dolnich 14 b nejdriv do
    // registru, ts_ps az z nej -> ts_valid o takt pozdeji (T+10 misto T+9), +49 FF. V 0x0427 byla cesta lut_q3[15] ->
    // ts_ps nejhorsi v navrhu (+0,215 ns, 82 % vedeni: jeden FF u BSRAM budi 34 cilu rozesetych pres ~40 sloupcu).
    // v3 paruje index hrany se znackou pres FIFO (seg_chan3, snese latenci do T+12); v2 (win_recip) bere index posledni
    // spustene hrany, takze o takt pozdejsi znacka by tam pri rychlem spousteni patrila k jine hrane -> v2 jen 0.
    parameter TSPIPE   = 0
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
    // FW 0x0427: zapis kalibracni tabulky z STM (domena clk; top.v synchronizuje z 10 MHz). Plati jen v cal_wait.
    input  wire        lw_en,       // 1-taktovy pulz: zapis polozky
    input  wire [9:0]  lw_a,        // adresa ({sada, kod}; u DUAL = 0 jen kod)
    input  wire [13:0] lw_neg,      // dolnich 14 bitu polozky (-x14 mod 2^14); amt se odvodi (viz nize)
    input  wire        lw_commit,   // 1-taktovy pulz: tabulka kompletni -> cal_valid
    output wire        cal_wait,    // sber dokoncen, histogram je ke cteni (dump_k), FPGA ceka na tabulku od STM

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
    output reg         bub_o,       // FW 0x0425: bublina = nejvyssi prosly tap (po 8) lezi > 8 tapu ZA kodem (platne v ts_valid)
    // protokol v3 (BANKS = 1); u BANKS = 0 bez vyznamu (bank_sw nezapojeno, bank_o = 0)
    input  wire        bank_sw,     // 1 takt: hranice segmentu -> aktivovat potvrzenou banku
    output wire        bank_o       // aktivni banka tabulky
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
    reg s_clr = 1'b0, s_col = 1'b0, s_drn = 1'b0, s_cmp = 1'b0, s_wait = 1'b0;
    assign cal_wait = s_wait;
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
    localparam    NB = (BANKS != 0) ? 2 : 1;      // pocet bank tabulky (v3: 2)
    reg [15:0]    lut  [0:NC*NB-1];
    reg [HW-1:0]  hist [0:NC-1];
    integer li, nx, ng;
    initial begin
        // nominalni hodnota, dokud neprobehne kalibrace (jen orientacne; cal_valid = 0 => ts se nevydavaji); V = 16384 + x14
        for (li = 0; li < NC * NB; li = li + 1) begin
            nx = ((li % NC) * 93 + 46 > 16383) ? 16383 : ((li % NC) * 93 + 46);
            ng = 16384 - nx;
            lut[li] = (nx == 0) ? 16'h4000 : {2'd2, ng[13:0]};
        end
    end

    reg  [15:0]   lut_q   = 16'd0;
    reg  [15:0]   lut_q2  = 16'd0;            // FW 0x0421: vystup BRAM (synteza lut_q + lut_q2 vtahne do BRAM: adresa + vystupni registr)
    reg  [15:0]   lut_q3  = 16'd0;            // samostatny FF za BRAM (tCO 3,5 ns): az od nej se vybira
    reg  [33:0]   tr0     = 34'd0, tr1 = 34'd0, tr2 = 34'd0;   // tick_hi - 0/1/2 (viz nize; tr0 jen pro DUAL)
    reg           tv_y    = 1'b0, tv_z = 1'b0;
    // horni cast znacky: tick_hi - amt (amt = 1 nebo 2 u DUAL = 0) vybrana z posuvneho registru tr*
    wire [33:0]   ts_sel  = (DUAL != 0) ? ((lut_q3[15:14] == 2'd0) ? tr0 : ((lut_q3[15:14] == 2'd1) ? tr1 : tr2))
                                      : (lut_q3[15] ? ((EXT_TR != 0) ? tr2_ext : tr2) : ((EXT_TR != 0) ? tr1_ext : tr1));
    reg  [33:0]   ts_hq   = 34'd0;          // TSPIPE = 1: vybrana horni cast (FF muze lezet u BSRAM, ts_ps u spotrebitelu)
    reg  [13:0]   ts_lq   = 14'd0;          // TSPIPE = 1: dolnich 14 b ze stejne polozky tabulky
    reg           tv_w    = 1'b0;           // TSPIPE = 1: tv_z o takt pozdeji
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
    reg           lastk_r = 1'b0;             // FW 0x0427: pouziva radic (AUTOCAL = 1), proto je mimo generate; deklarace pred radicem (Icarus 13)
    always @(posedge clk) begin busy_ra <= s_clr | s_col | s_drn | s_cmp; cmp_ra <= s_cmp; end
    wire [AW-1:0] hist_ra = cmp_ra ? scan_k : (busy_ra ? caddr : dump_k[AW-1:0]);
    wire          hist_we = s_clr | hist_we_r;
    wire [AW-1:0] hist_wa = s_clr ? clr_k : caddr;
    wire [HW-1:0] hist_wd = s_clr ? {HW{1'b0}} : hist_wd_r;
    // FW 0x0427: polozku {amt, neg} zapisuje STM (cal_wait). amt = r_entry + (x14 != 0) = r_entry + (neg != 0), tj. se odvodi
    // z neg (neg = 0 <=> x14 = 0). Sada R (u DUAL=0 vzdy) ma +T (bit 14 hodnoty V) -> r_entry = 1.
    wire          lw_r    = (DUAL != 0) ? ~lw_a[AW-1] : 1'b1;
    wire [1:0]    lw_amt  = {1'b0, lw_r} + {1'b0, (lw_neg != 14'd0)};
    wire          lut_we  = (AUTOCAL != 0) ? (s_cmp & ph[8]) : (lw_en & s_wait);
    // v3 (BANKS = 1): aktivni banka bank_r se cte, zapisuje se do druhe; u BANKS = 0 je horni bit adresy konstantni 0
    reg           bank_r  = 1'b0;
    reg           sw_pend = 1'b0;                  // potvrzena tabulka ceka na hranici segmentu
    assign bank_o = bank_r;
    wire          bwr     = (BANKS != 0) ? ~bank_r : 1'b0;
    wire          brd     = (BANKS != 0) ?  bank_r : 1'b0;
    wire [AW:0]   lut_wa  = {bwr, (AUTOCAL != 0) ? scan_k : lw_a[AW-1:0]};
    wire [AW:0]   lut_ra  = {brd, caddr};
    wire [15:0]   lut_wd  = (AUTOCAL != 0) ? lutv : {lw_amt, lw_neg};

    always @(posedge clk) begin
        lut_q     <= lut[lut_ra];
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
        // TSPIPE = 1: tataz hodnota o takt pozdeji (ts_hq/ts_lq se plni ve stejnem taktu jako drive ts_ps, tr* tedy stejne)
        // 2026-10-25 (plocha, ts_hq bez vyberu): pri DUAL = 0 je amt = 1 nebo 2 a drive ts_hq <= lut_q3[15] ? tr2 : tr1 v taktu
        // tv_z. tr2 v taktu tv_z = tr1 v taktu tv_y (tv_z <= tv_y vzdy, tr2 <= tr1 kazdy takt) -> ts_hq se nacte z tr1 uz v tv_y
        // a v tv_z se pri amt = 1 (lut_q3[15] = 0) prepise aktualnim tr1. Hodnota po tv_z je shodna s drivejsim ts_sel; ts_hq
        // se cte jen v tv_w (ts_ps) a dalsi znacka (tv_y) prijde nejdriv za 5 taktu. Odpada tr2 (34 FF, Gowin ho sdili mezi
        // kanaly) a 34 LUT3 vyberu v kazdem kanalu; povoleni ts_hq je LUT3 ze tri registru, data primo z tr1.
        if (DUAL == 0 && TSPIPE != 0 && EXT_TR == 0) begin
            if (tv_y | (tv_z & ~lut_q3[15])) ts_hq <= tr1;
        end else if (tv_z) ts_hq <= ts_sel;
        if (tv_z) ts_lq <= lut_q3[13:0];
        tv_w     <= tv_z;
        if ((TSPIPE != 0) ? tv_w : tv_z) ts_ps <= (TSPIPE != 0) ? {ts_hq, ts_lq} : {ts_sel, lut_q3[13:0]};
        tv_y     <= dv2 & cal_valid & ~cal_busy;
        tv_z     <= tv_y;
        ts_valid <= (TSPIPE != 0) ? tv_w : tv_z;
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
            s_clr <= 1'b1; s_col <= 1'b0; s_drn <= 1'b0; s_cmp <= 1'b0; s_wait <= 1'b0;
            clr_k     <= {AW{1'b0}};
            cnt       <= {HW{1'b0}};
            if (BANKS == 0) cal_valid <= 1'b0;   // v3: platna tabulka v aktivni bance plati dal (sber blokuje cal_busy)
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
                    s_drn <= 1'b0;
                    if (AUTOCAL != 0) begin s_cmp <= 1'b1; scan_k <= {AW{1'b0}}; end
                    else              s_wait <= 1'b1;                 // histogram kompletni, ceka se na tabulku od STM
                end
            end
            if (s_wait) begin
                if (lw_commit) begin
                    s_wait <= 1'b0;
                    if (BANKS != 0) sw_pend   <= 1'b1;      // v3: aktivace az na hranici segmentu
                    else            cal_valid <= 1'b1;
                end
            end
            if (sw_pend && bank_sw) begin              // v3: prepnuti banky (BANKS = 0: sw_pend je vzdy 0)
                bank_r    <= ~bank_r;
                sw_pend   <= 1'b0;
                cal_valid <= 1'b1;
            end
            if (s_cmp) begin                      // NC adres x 10 taktu
                if (ph[9]) begin
                    if (lastk_r) begin
                        s_cmp     <= 1'b0;
                        if (BANKS != 0) sw_pend   <= 1'b1;   // v3 (jen simulace AUTOCAL): jako potvrzeni od STM
                        else            cal_valid <= 1'b1;
                    end else begin
                        scan_k <= scan_k + {{(AW-1){1'b0}}, 1'b1};
                    end
                end
            end
        end
    end

    generate
    if (AUTOCAL != 0) begin : autocal   // ===== puvodni vypocet tabulky ve FPGA: JEN simulace (-DSIM_AUTOCAL) =====
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
    reg           clr_d = 1'b0;
    // 2026-10-22 (L-0194): pri CAL_LOG2 < 13 (simulace celeho topu, SIM_TOP: 12) byl posun ZAPORNY -> Verilog ho bere jako
    // cislo bez znamenka -> xsh = 0 a tabulka bez jemneho casu (znacky jen na takt). Syntezy se netyka (AUTOCAL = 0).
    wire [HW+1:0] xsh  = (CAL_LOG2 >= 13) ? (xv >> (CAL_LOG2 - 13)) : (xv << (13 - CAL_LOG2));   // 0..16384
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
    end else begin : stmcal             // ===== FW 0x0427: tabulku pocita STM; diagnostika d_* z histogramu taky (S2) =====
        always @(posedge clk) begin d_ovf <= 32'd0; d_peak <= 32'd0; d_nz <= 16'd0; d_last <= 16'd0; end
    end
    endgenerate
endmodule
