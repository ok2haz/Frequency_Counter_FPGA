// ============================================================
// tb_top_chan.sv -- CELY top.v (SIM_TOP): vyber kanalu (SET_CONFIG 0x03) a echo hradla (FW 0x041E).
// CH_A = 10 MHz * (1 + 813 ppm), CH_B = 5 MHz * (1 - 300 ppm) -- ruzne kmitocty, takze je poznat, ktery
// kanal drzi PRIMARNI slot ramce (spi_app: meas_periods / meas_dt_a_ps). Faze 1: chan_sel = 0 (tri okna),
// pak se chan_sel prepne (deposit do spi_app; dekodovani SET_CONFIG pokryva tb_link) a faze 2: tri okna.
// Kontroluje se v kazdem dokoncenem okne (valid_a = new_meas):
//   - primarni kmitocet N/dt odpovida vybranemu kanalu (1e-5),
//   - sekundarni slot (dt_s, periods_s) je druhy kanal,
//   - meas_channel (bajt 44) = chan_sel, echo hradla v phase_status = base_win,
//   - pri CH_B primarnim je regrese pro primarni slot VYPNUTA (rg_ok na vstupu spi_app = 0).
// ============================================================
`timescale 1ps/1fs
module tb_top_chan;
    reg clk10 = 0, clk100 = 0;
    always #50000 clk10 = ~clk10;
    initial begin #2300; forever #5000 clk100 = ~clk100; end
    real TA = 100000.0 / (1.0 + 813e-6);          // CH_A: 10 MHz
    real TB = 200000.0 / (1.0 - 300e-6);          // CH_B: 5 MHz
    reg ch_a = 0, ch_b = 0;
    initial begin #12345; forever begin ch_a = 1; #(TA/2.0); ch_a = 0; #(TA/2.0); end end
    initial begin #23456; forever begin ch_b = 1; #(TB/2.0); ch_b = 0; #(TB/2.0); end end
    wire miso, led;
    top dut (.clk_ref_10m(clk10), .clk_p0_100m(clk100), .ch_a(ch_a), .ch_b(ch_b), .led_tx(led),
             .spi_sck(1'b0), .spi_cs_n(1'b1), .spi_mosi(1'b0), .spi_miso(miso));

    real fa, fb, fp, fs;
    integer win = 0, errs = 0, phase = 1, n1 = 0, n2 = 0;
    initial begin fa = 1.0e12 / TA; fb = 1.0e12 / TB; end

    function real hz(input [31:0] per, input [47:0] dt);
        hz = (dt == 0) ? 0.0 : real'(per) / (real'(dt) * 0.6103515625) * 1.0e12;
    endfunction

    always @(posedge clk10) begin
        if (dut.valid_a && dut.cal_valid_a && !dut.cal_busy_a && dut.cal_valid_b && !dut.cal_busy_b) begin
            win = win + 1;
            fp = hz(dut.periods_p, dut.dt_p);
            fs = hz(dut.periods_s, dut.dt_s);
            $display("okno %0d faze %0d chan_sel=%0d: primarni %.1f Hz sekundarni %.1f Hz | base_win=%0d echo=%0d | rg_ok(spi_app)=%0d/%0d",
                     win, phase, dut.chan_sel, fp, fs, dut.base_win, dut.meas_phase_zero[2:0], dut.u_app.rg_ok_a, dut.u_app.rg_ok_b);
            if (phase == 1) begin
                n1 = n1 + 1;
                if (!(fp / fa > 1.0 - 1e-5 && fp / fa < 1.0 + 1e-5)) errs = errs + 1;
                if (!(fs / fb > 1.0 - 1e-5 && fs / fb < 1.0 + 1e-5)) errs = errs + 1;
            end else begin
                n2 = n2 + 1;
                if (!(fp / fb > 1.0 - 1e-5 && fp / fb < 1.0 + 1e-5)) errs = errs + 1;
                if (!(fs / fa > 1.0 - 1e-5 && fs / fa < 1.0 + 1e-5)) errs = errs + 1;
                if (dut.u_app.rg_ok_a || dut.u_app.rg_ok_b) errs = errs + 1;   // regrese neplati pro CH_B
            end
            if (dut.meas_phase_zero[2:0] != dut.base_win) errs = errs + 1;
            if (n1 == 3 && phase == 1) begin
                phase = 2;
                dut.u_app.chan_sel_r = 1'b1;          // jako SET_CONFIG 0x03 = 1
                n2 = -1;                              // okno, ktere uz bylo rozpracovane, se nepocita
            end
            if (phase == 2 && n2 == 3) begin
                if (errs == 0) $display("PASS: tb_top_chan"); else $display("FAIL: tb_top_chan (%0d chyb)", errs);
                $finish;
            end
        end
    end
    // meas_channel v rámci: po prepnuti musí h_ch == 1 (kontrola po dobehu faze 2)
    always @(posedge clk10) if (phase == 2 && n2 >= 1 && dut.u_app.h_ch !== 8'd1) begin errs = errs + 1; $display("h_ch=%0d", dut.u_app.h_ch); end
    initial begin #(64'd120_000_000_000); $display("FAIL: timeout (win=%0d phase=%0d)", win, phase); $finish; end
endmodule
