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
    reg        f_err = 1'b0;                   // znacka bez indexu / preteceni FIFO (nesmi nastat; diagnostika)
    wire       push = tq3;
    wire       pop  = ts_valid & (fn != 2'd0);
    always @(posedge clk) begin
        tq1 <= trig_ack; tq2 <= tq1; tq3 <= tq2;
        if (hold) begin
            fn <= 2'd0;
        end else begin
            case ({push, pop})
                2'b10: begin
                    if (fn == 2'd0) begin fh <= xcnt; fn <= 2'd1; end
                    else if (fn == 2'd1) begin fs <= xcnt; fn <= 2'd2; end
                    else f_err <= 1'b1;
                end
                2'b01: begin fh <= fs; fn <= fn - 2'd1; end
                2'b11: begin
                    if (fn == 2'd1) fh <= xcnt;
                    else begin fh <= fs; fs <= xcnt; end
                end
                default: ;
            endcase
            if (ts_valid && fn == 2'd0) f_err <= 1'b1;
        end
        if (clear) f_err <= 1'b0;                  // chyba plati pro segment (zaznam s ni je neplatny)
    end

    // ---- soucty segmentu ----
    reg        first = 1'b1;                   // segment zatim bez znacky
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
    wire       take = pop & ~hold & ~freeze & ~clear;
    always @(posedge clk) begin
        ncy <= 1'b0;
        v1  <= take & ~first;
        v1b <= v1;
        v2  <= v1b;
        v3  <= v2;
        if (take) begin
            xm <= fh; tm <= ts_ps[39:0];
            if (first) begin x0 <= fh; t0 <= ts_ps; first <= 1'b0; end
            nlo <= nlo + 12'd1; ncy <= (nlo == 12'hFFF);
            if (bub    && o_bub != 8'hFF) o_bub <= o_bub + 8'd1;
            if (cs_act && o_ovl != 4'hF)  o_ovl <= o_ovl + 4'd1;
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
            first <= 1'b1; nlo <= 12'd0; nhi <= 12'd0;
            sx_lo <= 24'd0; sx_hi <= 24'd0; st_lo <= 32'd0; st_hi <= 24'd0;
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
    always @(posedge clk) begin
        cp1 <= sig_pin; cp2 <= cp1; cp3 <= cp2;
        ce  <= cp2 & ~cp3 & cnt_en;
        sn1 <= snap;
        if (snap) begin
            cdd   <= ccnt - (xcnt[9:0] - xprev);
            xprev <= xcnt[9:0];
            ccnt  <= {9'd0, ce};
        end else if (ce) ccnt <= ccnt + 10'd1;
        if (sn1) ccd_bad <= ~((cdd == 10'd0) | (cdd == 10'd1) | (cdd == 10'h3FF));
    end

    assign o_n     = {nhi, nlo};
    assign o_x0    = first ? 40'd0 : x0;
    assign o_t0    = first ? 48'd0 : t0;
    // FW 0x0504: soucty v DSP (SUM_DSP = 1) -- pricitani ve V+2 z rozdilu dx_r/dt_r jako u varianty v logice
    generate if (SUM_DSP != 0) begin : g_dsp
        reg [47:0] sx_d = 48'd0 /* synthesis syn_dspstyle = "dsp" */;
        reg [55:0] st_d = 56'd0 /* synthesis syn_dspstyle = "dsp" */;
        always @(posedge clk) begin
            if (clear)   begin sx_d <= 48'd0; st_d <= 56'd0; end
            else if (v2) begin sx_d <= sx_d + {22'd0, dx_r}; st_d <= st_d + {16'd0, dt_r}; end
        end
        assign o_sx = sx_d;
        assign o_st = st_d;
    end else begin : g_fab
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
//   Hranice segmentu: seg_tick (1 takt). Pak freeze (znacky se nepricitaji) -> DRAIN taktu dobeh souctu -> 16 taktu
//   zapis zaznamu A (8 slov) a B (8 slov) -> clear -> konec freeze. Celkem ~22 taktu = 220 ns z 25 ms: vypadne nejvys
//   ~4 znacek kanalu (zanedbatelne; pocitani hran X bezi dal, takze se nic nerozbije).
//   Zaznam v RAM = 32 B (LE slova): bajty 0..30 presne jako v ramci (kap. 3.2), bajt 31 = 0.
//   Aplikace (10 MHz) se dozvi o zapsane dvojici zaznamu prepnutim rec_tgl (az PO zapisu obou).
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
    input  wire [RAW+2:0] raddr,     // {zaznam, slovo}
    output reg  [31:0] rdata
);
    initial begin seg10_act = 1'b0; seg_tick = 1'b0; freeze = 1'b0; snap = 1'b0; clear = 1'b0; rec_tgl = 1'b0; seg_idx = 32'd0; rdata = 32'd0; end
    localparam DRAIN = 6;                            // dobeh souctu seg_chan3 (V+4) + rezerva

    // ---- casovac (odpocet, registrovana detekce konce) ----
    reg  [1:0]  s10 = 2'b00;
    reg  [21:0] cnt = SEG25 - 1;
    reg         z1  = 1'b0;
    always @(posedge clk) begin
        s10      <= {s10[0], seg10_req};
        seg_tick <= 1'b0;
        z1       <= (cnt == 22'd1);
        if (z1) begin                                 // cnt je ted 0: konec segmentu
            seg_tick  <= 1'b1;
            seg10_act <= s10[1];
            cnt       <= (s10[1] ? SEG10 : SEG25) - 1;
        end else cnt <= cnt - 22'd1;
    end

    // ---- zapisovac ----
    reg [31:0] mem [0:(1<<(RAW+3))-1];
    reg [RAW-1:0] wrec = {RAW{1'b0}};                // poradi zaznamu A teto dvojice (sude)
    reg [4:0]  k   = 5'd0;                           // 0..15 slovo (bit 3 = kanal B), 16 = konec
    reg [2:0]  dr  = 3'd0;
    reg        wr  = 1'b0, we = 1'b0;
    reg [RAW+2:0] wa = {(RAW+3){1'b0}};
    reg [31:0] wd = 32'd0;
    reg [31:0] wa_a = 32'd0, wa_b = 32'd0;           // slovo k obou kanalu (1. stupen), vyber kanalu ve 2. stupni
    reg [4:0]  k1 = 5'd0;
    // slovo k zaznamu (LE): 0 {FLAGS, CH, SEG_IDX}, 1 {X0[7:0], N}, 2 X0[39:8], 3 T0[31:0], 4 {SX[15:0], T0[47:32]},
    //                       5 SX[47:16], 6 ST[31:0], 7 {0, ST[55:32]}
    function [31:0] wsel;
        input [2:0] w; input [7:0] chb; input [15:0] si;
        input [23:0] n; input [39:0] x0; input [47:0] t0; input [47:0] sx; input [55:0] st; input [7:0] fl;
        case (w)
            3'd0: wsel = {fl, chb, si};
            3'd1: wsel = {x0[7:0], n};
            3'd2: wsel = x0[39:8];
            3'd3: wsel = t0[31:0];
            3'd4: wsel = {sx[15:0], t0[47:32]};
            3'd5: wsel = sx[47:16];
            3'd6: wsel = st[31:0];
            default: wsel = {8'd0, st[55:32]};
        endcase
    endfunction
    reg        v_s1 = 1'b0;                          // 1. stupen nese slovo k1
    always @(posedge clk) begin
        snap  <= 1'b0;
        clear <= 1'b0;
        v_s1  <= 1'b0;
        // 2. stupen: vyber kanalu, 3. stupen: zapis do RAM
        we <= v_s1;
        if (v_s1) begin
            wa <= {wrec + {{(RAW-1){1'b0}}, k1[3]}, k1[2:0]};
            wd <= k1[3] ? wa_b : wa_a;
        end
        if (we) mem[wa] <= wd;
        if (seg_tick) begin
            freeze <= 1'b1; snap <= 1'b1; dr <= 3'd0; wr <= 1'b0; k <= 5'd0;
        end else if (freeze && !wr) begin
            dr <= dr + 3'd1;
            if (dr == DRAIN - 1) wr <= 1'b1;
        end else if (wr) begin
            if (k != 5'd16) begin
                // 1. stupen: slovo k[2:0] obou kanalu (8:1)
                wa_a <= wsel(k[2:0], {1'b0, a_p, 4'd0}, seg_idx[15:0], a_n, a_x0, a_t0, a_sx, a_st, a_fl);
                wa_b <= wsel(k[2:0], {1'b0, b_p, 4'd1}, seg_idx[15:0], b_n, b_x0, b_t0, b_sx, b_st, b_fl);
                k1   <= k;
                v_s1 <= 1'b1;
                k    <= k + 5'd1;
            end else if (!v_s1 && !we) begin                 // posledni slovo je v RAM
                wr      <= 1'b0;
                clear   <= 1'b1;
                freeze  <= 1'b0;
                wrec    <= wrec + {{(RAW-2){1'b0}}, 2'd2};
                seg_idx <= seg_idx + 32'd1;
            end
        end
        if (clear) rec_tgl <= ~rec_tgl;                  // aplikace smi cist dvojici az po zapisu obou
    end
    always @(posedge rclk) rdata <= mem[raddr];
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
    output reg  [31:0] seq,
    output reg  [33:0] last,         // hruby cas (takty) -- kontrola, bajty 100..107
    output reg  [33:0] prev,
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
    reg [47:0] f_buf = 48'd0;
    wire [33:0] dd  = f_buf[47:14] - c_tick;
    wire        dhi = ~dd[33] & (dd[32:7] != 26'd0);        // > 127
    wire        dlo =  dd[33] & (dd[32:7] != {26{1'b1}});   // < -128
    initial begin seq = 32'd0; last = 34'd0; prev = 34'd0; pts_l = 48'd0; pts_p = 48'd0; pfl_l = 8'd0; pfl_p = 8'd0;
                  d_l = 8'd0; d_p = 8'd0; nofine = 1'b0; end
    always @(posedge clk) begin
        s  <= {s[1:0], pin};
        fs <= {fs[1:0], f_tgl};
        if (win != 5'd31)  win  <= win + 5'd1;
        if (fage != 5'd31) fage <= fage + 5'd1;
        if (fs[2] ^ fs[1]) begin f_buf <= f_ts; fa <= f_lock; fage <= 5'd0; end
        if (s[1] & ~s[2]) begin c_tick <= tick; pend <= 1'b1; win <= 5'd0; end
        else if (pend && ((fa && fage != 5'd31) || win == 5'd31)) begin
            pend  <= 1'b0; fa <= 1'b0;
            seq   <= seq + 32'd1;
            prev  <= last;  last  <= c_tick;
            pts_p <= pts_l; pfl_p <= pfl_l; d_p <= d_l;
            if (fa && fage != 5'd31) begin
                pts_l <= f_buf; pfl_l <= 8'd3; nofine <= 1'b0;
                d_l   <= dhi ? 8'h7F : dlo ? 8'h80 : dd[7:0];
            end else begin
                pts_l <= {c_tick, 14'd0}; pfl_l <= 8'd1; d_l <= 8'd0; nofine <= f_lock;
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
module pin_cnt3 #(
    parameter PROBE_W = 100000                   // okno sondy [takty clk] = 1 ms pri 100 MHz
)(
    input  wire        pin,
    input  wire        clk,
    input  wire [2:0]  sel,
    output wire        sig_out,
    output reg  [15:0] probe                     // hrany /16 za posledni okno (saturuje)
);
    reg [3:0] c = 4'd0;
    always @(posedge pin) c <= c + 4'd1;
    assign sig_out = (sel == 3'd0) ? pin : (sel == 3'd1) ? c[0] : (sel == 3'd2) ? c[1] : (sel == 3'd3) ? c[2] : c[3];
    reg [2:0]  s  = 3'b000;
    reg [16:0] wc = 17'd0;
    reg [15:0] pc = 16'd0;
    initial probe = 16'd0;
    always @(posedge clk) begin
        s <= {s[1:0], c[3]};
        if (wc == PROBE_W - 1) begin
            wc <= 17'd0; probe <= pc; pc <= {15'd0, s[1] & ~s[2]};
        end else begin
            wc <= wc + 17'd1;
            if (s[1] & ~s[2] && pc != 16'hFFFF) pc <= pc + 16'd1;
        end
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
    reg        rdy   = 1'b0;                     // RESETN CLKDIV, reset IDES8 a logiky PCLK
    reg [33:0] t_rel = 34'd0;                    // tick v taktu pred uvolnenim
    initial begin f_tgl = 1'b0; f_ts = 48'd0; epoch = 4'd0; end
    always @(posedge clk) begin
        lk_s <= {lk_s[0], lock};
        rdy  <= lk_s[1];
        if (lk_s[1] & ~rdy) begin t_rel <= tick; epoch <= epoch + 4'd1; end
    end
    assign f_lock = rdy;
    CLKDIV #(.DIV_MODE("4"), .GSREN("false")) u_div (.HCLKIN(fclk), .RESETN(rdy), .CALIB(1'b0), .CLKOUT(pclk));
    wire [7:0] q;
    IDES8 #(.GSREN("false"), .LSREN("true")) u_ides (
        .D(pin), .FCLK(fclk), .PCLK(pclk), .CALIB(1'b0), .RESET(~rdy),
        .Q0(q[0]), .Q1(q[1]), .Q2(q[2]), .Q3(q[3]), .Q4(q[4]), .Q5(q[5]), .Q6(q[6]), .Q7(q[7]));
    wire [7:0] w = (Q0_FIRST != 0) ? q : {q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]};   // w[0] = nejdrivejsi
    // domena PCLK: prvni nabezna hrana (i na rozhrani slov), poloha 0..7 v taktu
    reg  [33:0] pcnt  = 34'd0;
    reg         lastw = 1'b0;
    reg  [36:0] p_ts  = 37'd0;                   // {pcnt, poloha}; cas = {p_ts, 11'b0}
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
            pcnt <= 34'd0; lastw <= 1'b0; p_tgl <= 1'b0;
        end else begin
            pcnt  <= pcnt + 34'd1;
            lastw <= w[7];
            if (hit) begin p_ts <= {pcnt, pos}; p_tgl <= ~p_tgl; end
        end
    end
    // do domeny clk: p_ts se drzi do dalsi hrany (~1 s) -> po 3 FF prepinaciho bitu je stabilni
    reg [2:0] t_s = 3'b000;
    always @(posedge clk) begin
        t_s <= {t_s[1:0], p_tgl};
        if (!rdy) t_s <= 3'b000;
        else if (t_s[2] ^ t_s[1]) begin
            f_ts  <= {p_ts[36:3] + t_rel, p_ts[2:0], 11'd0};
            f_tgl <= ~f_tgl;
        end
    end
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
//   zmeny. Rozlozeni: FIFO a okno jsou BSRAM, radic PSRAM ~150 bunek u vyhrazenych vyvodu PSRAM.
module raw_cap3 #(
    parameter PS_INIT = 15000                    // cekani po resetu PSRAM [takty] (simulace kratsi)
)(
    input  wire        clk,
    input  wire        req_tgl,      // z 10 MHz (2FF + hrana)
    input  wire        req_ch,       // kvazistaticke
    input  wire [1:0]  req_mode,     // FW 0x0505: 0 = 512 do BSRAM, 1 = hluboky do PSRAM, 2 = nacist okno z PSRAM
    input  wire [23:0] req_arg,      // rezim 1: pocet znacek, rezim 2: cislo okna (kvazistaticke)
    input  wire        tv_a, tv_b,   // znacka kanalu (ts_valid & platny kanal)
    input  wire [47:0] ts_a, ts_b,
    output reg  [9:0]  n,            // pocet ulozenych znacek (0..512; kvazistaticke pro 10 MHz)
    output reg  [23:0] dn,           // hluboky zaznam: znacek v PSRAM
    output wire [7:0]  dst,          // bit0 PSRAM pripravena, bit1 kalibrace zapisu selhala, bit2 zaznam bezi, bit3 FIFO
                                     // preteklo, bit4 nacita se okno, bit5 okno nacteno
    output wire [5:0]  dwsel,        // bity 4:0 zvolena latence zapisu PSRAM (psram_hb3 W0 + n), bit5 faze cteni
    output reg  [15:0] dwin,         // posledni (nacitane) okno
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
    initial begin n = 10'd0; rq = 48'd0; dn = 24'd0; dwin = 16'd0; end
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
    reg        wl = 1'b0, wdone = 1'b0;
    reg [4:0]  wb = 5'd0;                        // odeslanych davek (24 = 3072 B = 512 znacek)
    reg [21:0] wra = 22'd0;
    reg [39:0] asm_ = 40'd0;
    reg [2:0]  abi = 3'd0;
    reg [9:0]  widx = 10'd0, wlim = 10'd0;
    wire [31:0] wbase = {16'd0, ar2[15:0]} << 9;                                    // prvni znacka okna
    wire [31:0] wrem  = ({8'd0, dn} > wbase) ? ({8'd0, dn} - wbase) : 32'd0;
    assign dst = {2'b00, wdone, wl, dovf, drun, p_bad, p_rdy};
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
            if (md2 == 2'd0) begin run <= 1'b1; ch <= chs[1]; n <= 10'd0; end
            else if (md2 == 2'd1 && p_rdy && !drun && !wl) begin
                drun <= 1'b1; dfin <= (ar2 == 24'd0); dovf <= 1'b0; ch <= chs[1]; dn <= 24'd0;
                dreq <= (ar2 > DMAX) ? DMAX : ar2; dwa <= 22'd0;
                fwp <= 9'd0; frp <= 9'd0; fcnt <= 10'd0; fq_v <= 1'b0; nw_v <= 1'b0; sw_v <= 1'b0; sbi <= 3'd0;
            end else if (md2 == 2'd2 && p_rdy && !drun && !wl) begin
                wl <= 1'b1; wdone <= 1'b0; dwin <= ar2[15:0]; wb <= 5'd0; wra <= ar2[10:0] * 22'd3072;
                widx <= 10'd0; abi <= 3'd0; n <= 10'd0;
                wlim <= (wrem >= 32'd512) ? 10'd512 : wrem[9:0];
            end
        end else if (run && tv) begin
            mem[n[8:0]] <= ts;
            n <= n + 10'd1;
            if (n == 10'd511) run <= 1'b0;
        end
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
                    if (widx < wlim) mem[widx[8:0]] <= {asm_, p_rbyte};
                    widx <= widx + 10'd1;
                end else abi <= abi + 3'd1;
            end
            if (wb == 5'd24 && p_free && widx == 10'd512) begin wl <= 1'b0; wdone <= 1'b1; n <= wlim; end
        end
    end
    always @(posedge rclk) rq <= mem[rk];
endmodule
