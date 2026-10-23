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
// seg_chan3: jeden kanal. Casovani (tdc_chan): trig_ack v taktu T (= ec), rise_s v T+1, ts_valid v T+9; dalsi
//   spusteni nejdriv v T+5. Index hrany se zachyti v T+3 (citac X ma registrovany prenos, hodnota je od T+3
//   konzistentni a dalsi hrana ji zmeni nejdriv v T+4) a ceka ve FIFO (hloubka 2: nejvys dve rozpracovane znacky).
//   🔴 v2 (win_recip/regr_acc) bral index posledni SPUSTENE hrany -- pri spousteni hrana za hranou (> ~11 MHz) patril
//   k znacce predchozi hrany index nasledujici (konstantni posun -> sklon spravne, ale x0 o krok vedle). Tady paruje FIFO.
// Soucty (znacky >= 5 taktu od sebe, kazdy stupen nejvys ~2 urovne LUT jako v2 -- zadna multicycle vyjimka v SDC):
//   V+1 dolnich 20/13 b rozdilu t - t0, x - x0 (registrovana vypujcka), V+2 hornich 20/13 b, V+3 dolni casti souctu,
//   V+4 horni casti souctu (registrovany prenos). Zapisovac proto ceka DRAIN = 6 taktu po hranici segmentu. Rozsahy pri L = 25 ms: N <= 5e5 (24 b), x - x0 < 2^21 (26 b), t - t0 < 2^36 (40 b), SX < 2^40 (48 b),
//   ST < 2^55 (56 b).
// ------------------------------------------------------------
module seg_chan3 (
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
    output wire [23:0] o_n,
    output wire [39:0] o_x0,
    output wire [47:0] o_t0,
    output wire [47:0] o_sx,
    output wire [55:0] o_st,
    output wire [7:0]  o_flags,     // R_*: bit0 neplatny, bit2 bublina, bit3 znacky pri CS, bit4 kontrolni pocitani
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
    assign o_sx    = {sx_hi, sx_lo};
    assign o_st    = {st_hi, st_lo};
    assign o_flags = {3'd0, ccd_bad & ~inv, (o_ovl != 4'd0), (o_bub != 8'd0), 1'b0, inv | f_err};
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
        input [2:0] w; input ch; input [15:0] si;
        input [23:0] n; input [39:0] x0; input [47:0] t0; input [47:0] sx; input [55:0] st; input [7:0] fl;
        case (w)
            3'd0: wsel = {fl, 7'd0, ch, si};
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
                wa_a <= wsel(k[2:0], 1'b0, seg_idx[15:0], a_n, a_x0, a_t0, a_sx, a_st, a_fl);
                wa_b <= wsel(k[2:0], 1'b1, seg_idx[15:0], b_n, b_x0, b_t0, b_sx, b_st, b_fl);
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
    output reg  [31:0] seq,
    output reg  [33:0] last,
    output reg  [33:0] prev
);
    reg [2:0] s = 3'b000;
    initial begin seq = 32'd0; last = 34'd0; prev = 34'd0; end
    always @(posedge clk) begin
        s <= {s[1:0], pin};
        if (s[1] & ~s[2]) begin
            prev <= last;
            last <= tick;
            seq  <= seq + 32'd1;
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
module raw_cap3 (
    input  wire        clk,
    input  wire        req_tgl,      // z 10 MHz (2FF + hrana)
    input  wire        req_ch,       // kvazistaticke
    input  wire        tv_a, tv_b,   // znacka kanalu (ts_valid & platny kanal)
    input  wire [47:0] ts_a, ts_b,
    output reg  [9:0]  n,            // pocet ulozenych znacek (0..512; kvazistaticke pro 10 MHz)
    input  wire        rclk,
    input  wire [8:0]  rk,
    output reg  [47:0] rq
);
    reg [2:0]  rs = 3'b000;
    reg [1:0]  chs = 2'b00;
    reg        run = 1'b0, ch = 1'b0;
    reg [47:0] mem [0:511];
    initial begin n = 10'd0; rq = 48'd0; end
    always @(posedge clk) begin
        rs  <= {rs[1:0], req_tgl};
        chs <= {chs[0], req_ch};
        if (rs[2] ^ rs[1]) begin run <= 1'b1; ch <= chs[1]; n <= 10'd0; end
        else if (run && (ch ? tv_b : tv_a)) begin
            mem[n[8:0]] <= ch ? ts_b : ts_a;
            n <= n + 10'd1;
            if (n == 10'd511) run <= 1'b0;
        end
    end
    always @(posedge rclk) rq <= mem[rk];
endmodule
