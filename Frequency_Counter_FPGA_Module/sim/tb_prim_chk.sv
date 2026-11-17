// ============================================================
// tb_prim_chk.sv -- 2026-10-25: kontrola PREDPOKLADU o primitivech Gowin, na kterych stoji kroky "kruh v 16 SDPB" a "soucty
// v ALU54D primo z xm - x0 / tm - t0". sim/gowin_models_ext.v je napsany podle UG285 / UG287 a modelu yosys; tento test ma
// projit i s OFICIALNIMI modely z Gowin IDE (IDE/simlib/gw1n/prim_sim.v) -- hlavni instance ho pusti takto:
//   iverilog -g2012 -DGOWIN_PRIM -s tb_prim_chk -o p.vvp <Gowin>/IDE/simlib/gw1n/prim_sim.v sim/tb_prim_chk.sv && vvp -n p.vvp
//   (-DGOWIN_PRIM vlozi globalni instanci GSR, na kterou modely prim_sim.v odkazuji; kdyby je IDE prekladalo jinak, staci
//    ji podle Gowin dokumentace simulace pridat / odebrat -- na testovanou logiku nema vliv)
// a s modely tohoto balicku:
//   iverilog -g2012 -s tb_prim_chk -o p.vvp sim/gowin_models_ext.v sim/tb_prim_chk.sv && vvp -n p.vvp
// Overuje presne konfigurace z v3_core.v:
//   SDPB  READ_MODE 0, BIT_WIDTH_0 32 (zapis), BIT_WIDTH_1 8 (cteni), ADA = {slovo[8:0], 1'b0, 4'b1111}, ADB = {bajt[10:0], 3'b000}:
//         bajt k 32b slova W se cte na 8b adrese 4W + k jako DI[8k+7:8k]; cteni ma latenci 1 hranu CLKB (bypass); zapis do
//         bloku necte stara data jineho slova.
//   ALU54D AREG = BREG = 0, OUT_REG = 1, B_ADD_SUB = 1, C_ADD_SUB = 0, ALUD_MODE 0, ACCLOAD = 1, ALU_RESET_MODE "SYNC":
//         DOUT <= DOUT + A - B pri CE, RESET (synchronni, i s CE = 1) nuluje, bez CE drzi; 54 b modulo 2^54.
// ============================================================
`timescale 1ps/1ps
module tb_prim_chk;
`ifdef GOWIN_PRIM
    GSR GSR (.GSRI(1'b1));                     // globalni reset modelu Gowin neaktivni
`endif
    reg clka = 0, clkb = 0;
    always #5000  clka = ~clka;                // 100 MHz (zapis, jako clk_p0_100m)
    always #50000 clkb = ~clkb;                // 10 MHz (cteni, jako clk_ref_10m)
    integer errors = 0, nchk = 0, i, k, w;
    task chk(input cond, input [8*64-1:0] what);
        begin nchk = nchk + 1; if (!cond) begin errors = errors + 1; if (errors < 20) $display("  CHYBA: %0s (t = %0t)", what, $time); end end endtask

    // ---------------- SDPB 32 b zapis / 8 b cteni ----------------
    reg         cea = 0;
    reg  [8:0]  wa = 9'd0;
    reg  [31:0] di = 32'd0;
    reg  [10:0] ra = 11'd0;
    wire [31:0] dout;
    SDPB #(.READ_MODE(1'b0), .BIT_WIDTH_0(32), .BIT_WIDTH_1(8), .BLK_SEL_0(3'b000), .BLK_SEL_1(3'b000), .RESET_MODE("SYNC")) u_m (
        .CLKA(clka), .CEA(cea), .RESETA(1'b0), .ADA({wa, 1'b0, 4'b1111}), .DI(di),
        .CLKB(clkb), .CEB(1'b1), .RESETB(1'b0), .OCE(1'b1), .ADB({ra, 3'b000}), .DO(dout), .BLKSELA(3'b000), .BLKSELB(3'b000));
    reg [31:0] ref_m [0:511];
    function [31:0] pat(input integer a); pat = {a[7:0] ^ 8'h5A, a[7:0] + 8'h31, ~a[7:0], a[7:0] ^ 8'hC3} ^ (a * 32'h9E3779B1); endfunction

    // ---------------- ALU54D: DOUT <= DOUT + A - B ----------------
    reg  [53:0] aa = 54'd0, bb = 54'd0;
    reg         ce = 0, rst = 0;
    wire [53:0] dq;
    wire [54:0] caso;
    ALU54D #(.AREG(1'b0), .BREG(1'b0), .ASIGN_REG(1'b0), .BSIGN_REG(1'b0), .ACCLOAD_REG(1'b0), .OUT_REG(1'b1),
             .B_ADD_SUB(1'b1), .C_ADD_SUB(1'b0), .ALU_RESET_MODE("SYNC")) u_alu (
        .A(aa), .B(bb), .ASIGN(1'b0), .BSIGN(1'b0), .ACCLOAD(1'b1), .CASI(55'd0),
        .CLK(clka), .CE(ce), .RESET(rst), .DOUT(dq), .CASO(caso));
    reg [53:0] acc_ref = 54'd0;
    integer seed = 20261025;

    initial begin
        // ---- SDPB: zapis 512 slov (100 MHz), cteni vsech bajtu (10 MHz) ----
        @(negedge clka);
        for (i = 0; i < 512; i = i + 1) begin
            wa = i; di = pat(i); cea = 1; ref_m[i] = pat(i);
            @(negedge clka);
        end
        cea = 0;
        // prepis jednoho slova: ostatni bajty bloku beze zmeny
        wa = 9'd77; di = 32'hDEADBEEF; cea = 1; ref_m[77] = 32'hDEADBEEF; @(negedge clka); cea = 0;
        @(negedge clkb);
        for (w = 0; w < 512; w = w + 1)
            for (k = 0; k < 4; k = k + 1) begin
                ra = {w[8:0], k[1:0]};
                @(posedge clkb); #1;                   // adresa zachycena touto hranou -> data po ni (bypass)
                chk(dout[7:0] === ref_m[w][8*k +: 8], "SDPB: bajt k slova W na 8b adrese 4W + k = DI[8k+7:8k]");
                if (dout[7:0] !== ref_m[w][8*k +: 8] && errors < 20)
                    $display("    slovo %0d bajt %0d: precteno %h, ceka se %h", w, k, dout[7:0], ref_m[w][8*k +: 8]);
                @(negedge clkb);
            end
        // latence: zmena adresy mezi hranami CLKB vystup nezmeni
        ra = 11'd0; @(posedge clkb); #1; ra = 11'd5; #20000;
        chk(dout[7:0] === ref_m[0][7:0], "SDPB: vystup se meni az s hranou CLKB (cteni registrovane)");

        // ---- ALU54D ----
        @(negedge clka); rst = 1; ce = 1; @(negedge clka); rst = 0; ce = 0; acc_ref = 0;
        chk(dq === 54'd0, "ALU54D: synchronni RESET nuluje DOUT");
        for (i = 0; i < 2000; i = i + 1) begin
            aa = {$random(seed), $random(seed)} & {14'd0, {40{1'b1}}};
            aa[40] = $random(seed) & 1;
            bb = {$random(seed), $random(seed)} & {14'd0, {40{1'b1}}};
            ce = ($random(seed) & 3) != 0;
            rst = (($random(seed) & 255) == 0);
            @(negedge clka);
            if (rst) acc_ref = 0; else if (ce) acc_ref = acc_ref + aa - bb;
            chk(dq === acc_ref, "ALU54D: DOUT <= DOUT + A - B pri CE, RESET nuluje, jinak drzi (mod 2^54)");
            if (dq !== acc_ref && errors < 20) $display("    krok %0d: DOUT %h, ceka se %h (A %h B %h CE %b RST %b)", i, dq, acc_ref, aa, bb, ce, rst);
        end
        ce = 0; rst = 0;
        $display("SDPB: 2048 bajtu prectenych, ALU54D: 2000 kroku, kontrol %0d", nchk);
        if (errors == 0) $display("PASS: tb_prim_chk"); else $display("FAIL: tb_prim_chk (%0d chyb)", errors);
        $finish;
    end
endmodule
