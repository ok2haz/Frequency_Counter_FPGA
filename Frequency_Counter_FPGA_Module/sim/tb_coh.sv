// ============================================================
// tb_coh.sv -- prisny hlidac koherence 100 MHz <-> 10 MHz `coh_mon` (FW 0x0425, E0 audit 2026-10-20, kap. 2.2; model m_coh_mon.py).
//   Hodiny jsou koherentni (100 MHz na mrizce 10 ns, tgl10 se preklapi kazdych 100 ns s nastavitelnou fazi PH proti mrizce).
//   1) klid, faze 3,1 ns                                  -> 0 chybnych epoch, valid = 1, ep_ok = 1
//   2) faze na NABEZNE hrane 100 MHz + jitter +-150 ps     -> cesta P "kmita" 9/11, cesta N je cista -> 0 chybnych epoch
//   3) faze na SESTUPNE hrane + jitter                     -> naopak -> 0 chybnych epoch
//   4) 3x vynechany pulz 100 MHz (kazdy v jine epose)      -> presne +3
//   5) 2x pulz 100 MHz navic                               -> presne +2
//   6) en = 0                                              -> vynechany pulz se NEzapocte
//   Na konci: Gray -> binarni = err_bin, err_seen = 1.
//   Nove v 0x0425 (modul ve 0x0424 neni), negativni kontrolou jsou scenare 4/5 (bez detekce by vysly 0).
// ============================================================
`timescale 1ps/1fs
module tb_coh;
    reg clk = 0, tgl10 = 0, en = 0;
    integer drop_req = 0, extra_req = 0;
    // 100 MHz na pevne mrizce k*10 ns; vynechani/vlozeni pulzu nemeni mrizku (jen jeden interval 9/11 taktu)
    initial begin
        forever begin
            if (drop_req > 0) begin drop_req = drop_req - 1; #10000; end
            else begin
                clk = 1; #5000; clk = 0;
                if (extra_req > 0) begin extra_req = extra_req - 1; #1500; clk = 1; #2000; clk = 0; #1500; end
                else #5000;
            end
        end
    end
    real PH = 3100.0, JIT = 0.0;            // faze preklopeni tgl10 proti mrizce [ps], jitter (rovnomerne +-JIT)
    real tnext, jj;
    integer m;
    // tgl10 na ABSOLUTNI mrizce: preklopeni v case n*100 ns + PH (+ jitter), takze zmena PH se projevi hned (pri en = 0)
    initial begin
        forever begin
            m  = $urandom % 2001;
            jj = (JIT > 0.0) ? (JIT * (real'(m) - 1000.0) / 1000.0) : 0.0;
            tnext = ($floor(($realtime - PH) / 100000.0) + 1.0) * 100000.0 + PH + jj;
            if (tnext <= $realtime + 1000.0) tnext = tnext + 100000.0;
            #(tnext - $realtime);
            tgl10 = ~tgl10;
        end
    end

    wire [7:0] err_gray; wire ep_ok, err_seen, path_n, valid;
    coh_mon dut (.clk(clk), .tgl10(tgl10), .en(en), .err_gray(err_gray), .ep_ok(ep_ok), .err_seen(err_seen), .path_n(path_n), .valid(valid));

    function [7:0] g2b(input [7:0] g); integer i; begin g2b[7] = g[7]; for (i = 6; i >= 0; i = i - 1) g2b[i] = g2b[i+1] ^ g[i]; end endfunction

    localparam real EPOCH = 102400000.0;    // 1024 x 100 ns [ps]
    integer errs = 0, base;
    task wait_epochs(input integer n); begin #(EPOCH * n); end endtask
    task expect_cnt(input integer want, input [8*40-1:0] what);
        begin
            if (dut.err_bin !== want[7:0] || g2b(err_gray) !== want[7:0]) begin
                errs = errs + 1; $display("  CHYBA %0s: chybnych epoch %0d (gray->%0d), ocekavano %0d", what, dut.err_bin, g2b(err_gray), want);
            end else $display("  OK %0s: chybnych epoch %0d", what, dut.err_bin);
        end
    endtask

    integer k;
    initial begin
        #1000000; en = 1;
        wait_epochs(20);
        if (!valid || !ep_ok) begin errs = errs + 1; $display("  CHYBA: valid=%0d ep_ok=%0d v klidu", valid, ep_ok); end
        expect_cnt(0, "klid faze 3,1 ns");
        // faze na nabezne hrane s jitterem: vypnout, posunout, zapnout (prvni interval po povoleni se nehodnoti)
        en = 0; #200000; PH = 0.0; JIT = 150.0; #50000; en = 1;
        wait_epochs(20); expect_cnt(0, "faze na nabezne hrane +-150 ps");
        // test musi skutecne rozkmitat cestu P (jinak by "0 chyb" nic nedokazovalo): mene odchylek ma cesta N
        if (path_n !== 1'b1) begin errs = errs + 1; $display("  CHYBA: cesta P se nerozkmitala (path_n=%0d) -- test nic neoveril", path_n); end
        else $display("  OK: cesta P kmitala, rozhodla cesta N (path_n=1)");
        en = 0; #200000; PH = 5000.0; JIT = 150.0; #50000; en = 1;
        wait_epochs(20); expect_cnt(0, "faze na sestupne hrane +-150 ps");
        if (path_n !== 1'b0) begin errs = errs + 1; $display("  CHYBA: cesta N se nerozkmitala (path_n=%0d)", path_n); end
        else $display("  OK: cesta N kmitala, rozhodla cesta P (path_n=0)");
        JIT = 0.0; PH = 3100.0; en = 0; #200000; en = 1; wait_epochs(3);
        base = dut.err_bin;
        for (k = 0; k < 3; k = k + 1) begin wait_epochs(2); drop_req = 1; end
        wait_epochs(3); expect_cnt(base + 3, "3x vynechany pulz");
        for (k = 0; k < 2; k = k + 1) begin wait_epochs(2); extra_req = 1; end
        wait_epochs(3); expect_cnt(base + 5, "2x pulz navic");
        en = 0; wait_epochs(1); drop_req = 1; wait_epochs(2); en = 1; wait_epochs(3);
        expect_cnt(base + 5, "vynechany pulz pri en = 0");
        if (!err_seen) begin errs = errs + 1; $display("  CHYBA: err_seen = 0"); end
        if (errs == 0) $display("PASS: tb_coh"); else $display("FAIL: tb_coh (%0d chyb)", errs);
        $finish;
    end
endmodule
