// ============================================================
// tb_seg_dsp.sv -- 2026-10-25: cileny test souctu seg_chan3 v ALU54D (krok "soucty primo z xm/tm") a poctu N v DSP (krok
// "N v DSP": SUM_DSP = 1 pocita N v n_d, SUM_DSP = 0 v nlo/nhi s registrovanym prenosem): dve instance seg_chan3
// se stejnymi vstupy -- SUM_DSP = 1 (ALU54D: DOUT <= DOUT + {preteceni, xm/tm} - x0/t0) a SUM_DSP = 0 (puvodni algoritmus:
// rozdil ve dvou stupnich s vypujckou, soucty v logice) -- a v kazdem segmentu se porovnaji vsechny vystupy v taktu, kdy je
// cte zapisovac kruhu (E+7). Stimulus jako tdc_chan: trig_ack v T, rise_s v T+1, ts_valid v T+10, dalsi spusteni nejdriv
// v T+5; netrigrovane hrany jen rise_s; ridici signaly segmentu jako v3_ring (freeze E+1..E+25, snap E+1, clear E+26,
// nfc = ~freeze & ~clear). Cas zacina 20 000 taktu pred pretecenim 2^40 i 2^48 jednotek; citac hran X se v kazdem 8. segmentu
// po clear (bez hran a rozpracovanych znacek v E+10..E+40) nastavi v obou instancich na 2^40 - 40 -> preteceni padne doprostred
// dalsiho segmentu. Tak nastane preteceni mezi x0/t0 a xm/tm (bit 40 operandu A), ktere tb_top_v3 nevyvola (tam cas zacina
// od 0 a preteceni 40b casu je az za 0,67 s, index za 2^40 hran). Pokryti: pocet pricteni s wt = 1 a wx = 1 musi byt > 0.
// Spusteni: run_ps1_doplnek.txt (segdsp), ~10 s; mutace rezim 9 v sim/mut_top_v3.py.
// ============================================================
`timescale 1ps/1fs
module tb_seg_dsp;
    reg clk = 0;
    always #5000 clk = ~clk;
    reg        rise_s = 0, trig_ack = 0, ts_valid = 0, bub = 0, cs_act = 0, hold = 0, sig_pin = 0;
    reg        freeze = 0, snap = 0, clear = 0, nfc = 1;
    reg [47:0] ts_ps = 48'd0;
    reg [2:0]  presc = 3'd0;
    wire [23:0] n1, n0;  wire [39:0] x01, x00;  wire [47:0] t01, t00, sx1, sx0;  wire [55:0] st1, st0;
    wire [7:0]  fl1, fl0, bu1, bu0;  wire [3:0] ov1, ov0;  wire [2:0] p1, p0;
    seg_chan3 #(.SUM_DSP(1)) u1 (.clk(clk), .rise_s(rise_s), .cnt_en(1'b1), .trig_ack(trig_ack), .ts_valid(ts_valid), .ts_ps(ts_ps),
        .bub(bub), .cs_act(cs_act), .hold(hold), .sig_pin(sig_pin), .freeze(freeze), .snap(snap), .clear(clear), .nfc(nfc), .presc(presc),
        .o_n(n1), .o_x0(x01), .o_t0(t01), .o_sx(sx1), .o_st(st1), .o_flags(fl1), .o_presc(p1), .o_bub(bu1), .o_ovl(ov1));
    seg_chan3 #(.SUM_DSP(0)) u0 (.clk(clk), .rise_s(rise_s), .cnt_en(1'b1), .trig_ack(trig_ack), .ts_valid(ts_valid), .ts_ps(ts_ps),
        .bub(bub), .cs_act(cs_act), .hold(hold), .sig_pin(sig_pin), .freeze(freeze), .snap(snap), .clear(clear), .nfc(nfc), .presc(presc),
        .o_n(n0), .o_x0(x00), .o_t0(t00), .o_sx(sx0), .o_st(st0), .o_flags(fl0), .o_presc(p0), .o_bub(bu0), .o_ovl(ov0));

    integer errors = 0, nchk = 0, nseg = 0, nwt = 0, nwx = 0, nmarks = 0, nsum = 0;
    task chk(input cond, input [8*64-1:0] what);
        begin nchk = nchk + 1; if (!cond) begin errors = errors + 1; if (errors < 20) $display("  CHYBA: %0s (t = %0t)", what, $time); end end endtask

    // ---- ridici signaly segmentu (jako v3_ring) ----
    localparam integer SEGL = 431;                    // takty segmentu (neceleny pocet period stimulus -> faze se posouva)
    integer cyc = 0, sc = 0, e = -1;
    // ---- stimulus znacek ----
    integer seed = 12345;
    integer last_edge = -10, last_trig = -10, k;
    reg [63:0] tbase;
    reg [47:0] tq [0:15];                             // cas spustene hrany (ceka na ts_valid v T+10)
    integer    tqt [0:15];
    integer    qh = 0, qt = 0;
    reg        rpend = 0;                             // rise_s v pristim taktu
    initial begin
        tbase = 64'h4_0000_0000 - 64'd20000;          // tick 2^34 - 20 000: cas [T/16384] pretece 2^40 i 2^48 za 20 000 taktu
    end
    reg quiet = 1'b0;                                 // segment s nastavenim citace X: bez hran v E+10..E+40
    always @(negedge clk) begin
        // segment
        snap = 1'b0; clear = 1'b0;
        if (sc == SEGL - 1) begin sc = 0; e = 0; quiet = ((cyc / SEGL) % 8) == 3; end else begin sc = sc + 1; if (e >= 0) e = e + 1; end
        if (quiet && e == 35) begin                   // FIFO je prazdne (posledni spusteni pred E+10, ts_valid do E+20)
            u1.xhi = 32'hFFFF_FFFF; u0.xhi = 32'hFFFF_FFFF; u1.xlo = 8'd216; u0.xlo = 8'd216;
        end
        if (e == 1) begin freeze = 1'b1; snap = 1'b1; end
        if (e == 26) begin clear = 1'b1; freeze = 1'b0; end
        nfc = ~freeze & ~clear;
        // hold obcas (inv, nulovani FIFO), CS obcas (o_ovl), bublina obcas (o_bub)
        hold   = (cyc % 9000) > 8800;
        cs_act = ($random(seed) % 20) == 0;
        bub    = ($random(seed) % 30) == 0;
        // hrany: kazdy takt s pravdepodobnosti ~1/3, nejdriv 2 takty po predchozi; spusteni nejdriv 5 taktu po predchozim
        trig_ack = 1'b0; ts_valid = 1'b0;
        rise_s = rpend; rpend = 1'b0;
        if (cyc - last_edge >= 2 && ($random(seed) % 3) == 0 && !(quiet && e >= 10 && e <= 40)) begin
            last_edge = cyc; rpend = 1'b1; sig_pin = ~sig_pin;
            if (cyc - last_trig >= 5 && !hold) begin
                last_trig = cyc; trig_ack = 1'b1;
                tq[qt % 16] = ((tbase + cyc) << 14) - 48'd16384 - ($random(seed) & 16'h3FFF);
                tqt[qt % 16] = cyc + 10; qt = qt + 1;
            end
        end
        if (qh < qt && tqt[qh % 16] == cyc) begin ts_valid = 1'b1; ts_ps = tq[qh % 16]; qh = qh + 1; nmarks = nmarks + 1; end
        cyc = cyc + 1;
    end
    // ---- porovnani v taktu cteni zapisovace (E+7) a pokryti preteceni ----
    always @(posedge clk) begin
        if (u1.v1 && u1.g_dsp.wt) nwt = nwt + 1;
        if (u1.v1 && u1.g_dsp.wx) nwx = nwx + 1;
        if (u1.v1) nsum = nsum + 1;
        if (e == 7) begin
            nseg = nseg + 1;
            chk(n1 === n0 && x01 === x00 && t01 === t00, "N, X0, T0 shodne");
            chk(sx1 === sx0, "SX: ALU54D = puvodni soucet");
            chk(st1 === st0, "ST: ALU54D = puvodni soucet");
            chk(fl1 === fl0 && p1 === p0 && bu1 === bu0 && ov1 === ov0, "FLAGS, P, BUB, OVL shodne");
            if (sx1 !== sx0 || st1 !== st0)
                $display("  seg %0d: N %0d SX %h / %h ST %h / %h", nseg, n1, sx1, sx0, st1, st0);
        end
    end
    initial begin
        #(64'd600_000_000);                             // 60 000 taktu
        chk(nwt > 0, "pokryti: pricteni s pretecenim casu (wt = 1)");
        chk(nwx > 0, "pokryti: pricteni s pretecenim indexu (wx = 1)");
        chk(nseg > 100, "pokryti: aspon 100 segmentu");
        $display("segmentu %0d, znacek %0d, pricteni %0d, s pretecenim casu %0d, indexu %0d, kontrol %0d", nseg, nmarks, nsum, nwt, nwx, nchk);
        if (errors == 0) $display("PASS: tb_seg_dsp"); else $display("FAIL: tb_seg_dsp (%0d chyb)", errors);
        $finish;
    end
endmodule
