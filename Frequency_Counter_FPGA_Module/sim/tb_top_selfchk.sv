// ============================================================
// tb_top_selfchk.sv -- CELY top.v (SIM_TOP): SAMOKONTROLA FW 0x041F (STATUS #283).
//   Faze 1: tri okna v klidu -> kontrolni rozdil pocitani |ccd| <= 1, hlidac 100 MHz OK, zadny vypadek.
//   Faze 2: v jednom okne se merne ceste "ukradne" 5 hran (force rise_s = 0) -> ccd tohoto okna >= 4.
//           Bez kontrolniho pocitani by okno proslo s kmitoctem o 5 hran nizsim a bez jakehokoli priznaku.
//   Faze 3: hodiny 100 MHz se na 20 us zastavi (jako init Si5356 pri resetu STM za behu FPGA) ->
//           hlidac musi napocitat vypadek (clk_loss = 1, clk_flt = 1), drzet mereni (boot_done = 0),
//           znovu kalibrovat TDC a pak merit spravne (3 okna, kmitocet 1e-5, |ccd| <= 1).
// CH_A = 10 MHz * (1 + 813 ppm), CH_B = 5 MHz * (1 - 300 ppm).
// ============================================================
`timescale 1ps/1fs
module tb_top_selfchk;
    reg clk10 = 0, clk100 = 0, clk100_en = 1;
    always #50000 clk10 = ~clk10;
    initial begin #2300; forever begin #5000; clk100 = clk100_en ? ~clk100 : 1'b0; end end
    real TA = 100000.0 / (1.0 + 813e-6);          // CH_A: 10 MHz
    real TB = 200000.0 / (1.0 - 300e-6);          // CH_B: 5 MHz
    reg ch_a = 0, ch_b = 0;
    initial begin #12345; forever begin ch_a = 1; #(TA/2.0); ch_a = 0; #(TA/2.0); end end
    initial begin #23456; forever begin ch_b = 1; #(TB/2.0); ch_b = 0; #(TB/2.0); end end
    wire miso, led;
    top dut (.clk_ref_10m(clk10), .clk_p0_100m(clk100), .ch_a(ch_a), .ch_b(ch_b), .led_tx(led),
             .spi_sck(1'b0), .spi_cs_n(1'b1), .spi_mosi(1'b0), .spi_miso(miso));

    real fa, fp;
    integer win = 0, errs = 0, phase = 1, n = 0, steal_done = 0, recal_seen = 0, hold_seen = 0;
    integer ccd;
    initial fa = 1.0e12 / TA;

    function real hz(input [31:0] per, input [47:0] dt);
        hz = (dt == 0) ? 0.0 : real'(per) / (real'(dt) * 0.6103515625) * 1.0e12;
    endfunction

    // faze 3: zotaveni musi projit drzenim mereni a novou kalibraci
    always @(posedge clk10) if (phase == 3) begin
        if (!dut.boot_done) hold_seen = 1;
        if (dut.cal_busy_a && hold_seen) recal_seen = 1;
    end

    always @(posedge clk10) begin
        if (dut.valid_a && dut.cal_valid_a && !dut.cal_busy_a && dut.cal_valid_b && !dut.cal_busy_b) begin
            win = win + 1;
            n = n + 1;
            fp  = hz(dut.periods_p, dut.dt_p);
            ccd = $signed(dut.ccd_p);
            $display("okno %0d faze %0d: %.1f Hz | ccd=%0d | clk_status=%b loss=%0d",
                     win, phase, fp, ccd, dut.clk_status[2:0], dut.clk_loss);
            if (phase == 1) begin
                if (!(fp / fa > 1.0 - 1e-5 && fp / fa < 1.0 + 1e-5)) begin errs = errs + 1; $display("  CHYBA: kmitocet"); end
                if (ccd > 1 || ccd < -1) begin errs = errs + 1; $display("  CHYBA: ccd v klidu"); end
                if (dut.clk_status[1:0] != 2'b01 || dut.clk_loss != 0) begin errs = errs + 1; $display("  CHYBA: hlidac v klidu"); end
                if (n == 3) begin phase = 2; n = 0; end
            end else if (phase == 2) begin
                // okno, ve kterem se kradlo: prvni dokoncene po kradezi
                if (steal_done == 1) begin
                    if (ccd < 4) begin errs = errs + 1; $display("  CHYBA: ukradene hrany nevidi (ccd=%0d)", ccd); end
                    steal_done = 2;
                end else if (steal_done == 2) begin
                    if (ccd > 1 || ccd < -1) begin errs = errs + 1; $display("  CHYBA: ccd po kradezi"); end
                    phase = 3; n = 0;
                    // zastavit 100 MHz na 20 us (mezi okny)
                    fork begin
                        #(64'd300_000_000);   // 300 us po konci okna
                        clk100_en = 0;
                        #(64'd20_000_000);
                        clk100_en = 1;
                    end join_none
                end
            end else begin
                if (n == 1) begin
                    if (dut.clk_loss != 8'd1 || !dut.clk_status[1]) begin errs = errs + 1; $display("  CHYBA: vypadek nenapocitan"); end
                    if (!hold_seen || !recal_seen) begin errs = errs + 1; $display("  CHYBA: chybi drzeni (%0d) / rekalibrace (%0d)", hold_seen, recal_seen); end
                end
                if (!(fp / fa > 1.0 - 1e-5 && fp / fa < 1.0 + 1e-5)) begin errs = errs + 1; $display("  CHYBA: kmitocet po zotaveni"); end
                if (ccd > 1 || ccd < -1) begin errs = errs + 1; $display("  CHYBA: ccd po zotaveni"); end
                if (n == 3) begin
                    if (errs == 0) $display("PASS: tb_top_selfchk"); else $display("FAIL: tb_top_selfchk (%0d chyb)", errs);
                    $finish;
                end
            end
        end
    end

    // faze 2: 1 ms po zacatku okna (mimo segmenty regrese i uzavirani) ukrast 5 hran merne ceste CH_A
    integer k;
    always @(posedge clk10) if (phase == 2 && steal_done == 0 && dut.valid_a) begin
        steal_done = -1;
        fork begin
            #(64'd1_000_000_000);
            for (k = 0; k < 5; k = k + 1) begin
                @(posedge dut.u_wra.rise_s);
                force dut.u_wra.rise_s = 1'b0;
                @(posedge clk100); @(posedge clk100);
                release dut.u_wra.rise_s;
                #(64'd1_000_000);
            end
            steal_done = 1;
        end join_none
    end

    initial begin #(64'd200_000_000_000); $display("FAIL: timeout (win=%0d phase=%0d)", win, phase); $finish; end
endmodule
