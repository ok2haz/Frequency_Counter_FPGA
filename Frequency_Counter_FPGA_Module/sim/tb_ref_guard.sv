// ============================================================
// tb_ref_guard.sv -- ref_guard (FW 0x0420): bezpecne prepnuti reference OCXO <-> externi 10 MHz.
// Osc 31,25 MHz (OSC /8), externi vstup 10 MHz. Scenare:
//  1 externi chybi, pozadavek 1            -> ref_ctrl zustane 0
//  2 externi pritomna, pozadavek 1         -> ref_ctrl = 1 az po stabilizaci (>= ~12 us, <= ~40 us), ne okamzite
//  3 externi zmizi, kdyz je vybrana        -> do 15 us SAMA zpet na OCXO, ext_lost = 1
//  4 externi se vrati, pozadavek porad 1   -> zustane OCXO (sticky ext_lost)
//  5 pozadavek 0, pak 1                    -> ext_lost smazan, znovu se vybere externi
//  6 zakmit (externi jen 5 us)             -> nikdy se nevybere
//  7 pozadavek 0 pri vybrane externi       -> ref_ctrl = 0 do 3 taktu osc
// ============================================================
`timescale 1ps/1ps
module tb_ref_guard;
    reg osc = 0; always #16000 osc = ~osc;                 // 31,25 MHz
    reg ext_en = 0; reg ext = 0;
    always begin #50000; if (ext_en) ext = ~ext; else ext = 0; end   // 10 MHz po zapnuti
    reg req = 0;
    wire ctrl, pres, lost;
    ref_guard dut (.osc_clk(osc), .ext_sens(ext), .sel_req(req), .ref_ctrl(ctrl), .ext_present(pres), .ext_lost(lost));

    integer errs = 0;
    task chk(input cond, input [255:0] what); begin if (!cond) begin errs = errs + 1; $display("CHYBA: %0s (t=%0t ctrl=%b pres=%b lost=%b)", what, $time, ctrl, pres, lost); end end endtask
    integer t_sel;

    initial begin
        // 1
        req = 1; #200_000_000; chk(ctrl == 0 && pres == 0, "1: externi chybi -> OCXO");
        // 2
        ext_en = 1; t_sel = 0;
        #8_000_000;  chk(ctrl == 0, "2: po 8 us jeste ne (stabilizace)");
        #40_000_000; chk(ctrl == 1 && pres == 1, "2: po 48 us vybrana externi");
        // 3
        ext_en = 0; #15_000_000; chk(ctrl == 0 && lost == 1, "3: externi zmizela -> sama zpet na OCXO");
        // 4
        ext_en = 1; #200_000_000; chk(ctrl == 0 && lost == 1, "4: externi zpet, ale sticky -> OCXO");
        // 5
        req = 0; #1_000_000; chk(lost == 0 && ctrl == 0, "5: pozadavek 0 maze ext_lost");
        req = 1; #60_000_000; chk(ctrl == 1, "5: znovu vybrana externi");
        // 7
        req = 0; #200_000; chk(ctrl == 0, "7: pozadavek 0 -> OCXO do 3 taktu osc"); #1_000_000;
        // 6 zakmit
        ext_en = 0; #50_000_000; req = 1; ext_en = 1; #5_000_000; ext_en = 0; #100_000_000;
        chk(ctrl == 0, "6: zakmit 5 us se nevybere");
        if (errs == 0) $display("PASS: tb_ref_guard"); else $display("FAIL: tb_ref_guard (%0d chyb)", errs);
        $finish;
    end
endmodule
