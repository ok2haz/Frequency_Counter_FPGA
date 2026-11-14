// ============================================================
// tb_ccd_carry.sv -- 2026-10-25 (nalez pri optimalizaci, navrh_opravy.patch): kontrolni pocitani hran seg_chan3 (FLAGS bit4)
// pri snap v taktu registrovaneho prenosu citace X. Citac X = {xhi, xlo} ma registrovany prenos: takt po preteceni xlo
// (FF -> 00) plati xcy = 1 a xhi je jeste stary, takze xcnt[9:0] je o 256 mensi nez skutecny pocet hran. Padne-li snap do
// tohoto taktu, cdd = ccnt - (xcnt[9:0] - xprev) vyjde +-256 a zaznam (i nasledujici, xprev je posunuty) dostane falesny
// priznak ccd_bad, prestoze zadna hrana nechybi. Na desce snap pada do takoveho taktu nahodne s pravdepodobnosti
// f / (256 * 100 MHz) na segment (10 MHz: 3,9e-4, pri L = 25 ms asi jednou za minutu na kanal).
// Test: signal 25 MHz (perioda 4 takty, rise_s 3 takty po nabezne hrane pinu jako tdc_chan), snap kazdych 1000 taktu a
// navic v 8 pripadech prave v taktu xcy = 1. Kontrola: ccd_bad = 0 po celou dobu (krome nabehu). Negativni kontrola:
// v jednom segmentu chybi 3 hrany rise_s (pin ano; rozdil +-1 navrh toleruje zamerne) -> ccd_bad = 1 v nasledujicim
// vyhodnoceni.
// Bez opravy SELZE (cdd = 256), s opravou (xc10 = {xhi[1:0] + xcy, xlo}) projde. ~1 s.
// ============================================================
`timescale 1ps/1fs
module tb_ccd_carry;
    reg clk = 0; always #5000 clk = ~clk;
    reg rise_s = 0, sig_pin = 0, snap = 0;
    wire [7:0] fl; wire [23:0] n; wire [39:0] x0; wire [47:0] t0, sx; wire [55:0] st; wire [2:0] p; wire [7:0] bu; wire [3:0] ov;
    seg_chan3 #(.SUM_DSP(0)) u (.clk(clk), .rise_s(rise_s), .cnt_en(1'b1), .trig_ack(1'b0), .ts_valid(1'b0), .ts_ps(48'd0),
        .bub(1'b0), .cs_act(1'b0), .hold(1'b0), .sig_pin(sig_pin), .freeze(1'b0), .snap(snap), .clear(1'b0), .nfc(1'b1), .presc(3'd0),
        .o_n(n), .o_x0(x0), .o_t0(t0), .o_sx(sx), .o_st(st), .o_flags(fl), .o_presc(p), .o_bub(bu), .o_ovl(ov));
    integer cyc = 0, ncar = 0, nfalse = 0, nsteal_seen = 0, errors = 0;
    integer steal_at = 30002;                      // od tohoto taktu chybi 3 hrany rise_s (pin ano)
    always @(negedge clk) begin
        sig_pin = (cyc % 4) < 2;
        rise_s  = (cyc % 4) == 3 && cyc > 8 && !(cyc > steal_at && cyc < steal_at + 12);
        snap = (cyc % 1000 == 0) && cyc > 0;
        if (u.xcy && cyc > 3000 && cyc < 28000 && (cyc / 1024) % 3 == 1) snap = 1;   // snap prave v taktu prenosu
        cyc = cyc + 1;
    end
    always @(posedge clk) begin
        if (snap && u.xcy) ncar = ncar + 1;
        // vyhodnoceni (sn1 o takt pozdeji) -- ccd_bad plati od taktu po sn1
        if (u.ccd_bad && cyc > 2100 && cyc < 30000) begin
            nfalse = nfalse + 1;
            if (nfalse < 5) $display("  CHYBA: falesny ccd_bad (zadna hrana nechybi), takt %0d, cdd = %0d", cyc, u.cdd);
        end
        if (u.ccd_bad && cyc >= 30000) nsteal_seen = nsteal_seen + 1;
    end
    initial begin
        #(64'd340_000_000);                        // 34 000 taktu
        if (ncar < 5) begin errors = errors + 1; $display("  CHYBA: pokryti: snap v taktu prenosu jen %0d x", ncar); end
        if (nfalse > 0) errors = errors + 1;
        if (nsteal_seen == 0) begin errors = errors + 1; $display("  CHYBA: chybejici hrany nebyly nahlaseny (ccd_bad)"); end
        $display("snap v taktu prenosu %0d x, taktu s falesnym ccd_bad %0d, chybejici hrany nahlaseny %0d taktu", ncar, nfalse, nsteal_seen);
        if (errors == 0) $display("PASS: tb_ccd_carry"); else $display("FAIL: tb_ccd_carry (%0d chyb)", errors);
        $finish;
    end
endmodule
