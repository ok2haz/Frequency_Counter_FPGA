// ============================================================
// tb_top_regr.sv -- simulace CELEHO top.v (SIM_TOP: hradlo 2,5 ms, kalibrace 2^12, rychly nabeh)
// s regresnim blokem (SIM_TOP_REGR). Reprodukce stavu z desky: CH_A = 10 MHz * (1 + 813 ppb),
// clk_p0_100m = 10 * clk_ref_10m (oba z OCXO, pevna faze). Po kazdem uzavrenem okne vypise
// r_periods, r_dt a vystupy regr_acc a porovna je s ocekavanim:
//   n_A, n_B ~ G/8 * f, xm ~ index stredu segmentu, ym ~ cas stredu segmentu od zacatku okna,
//   f_regr = (xm_B - xm_A)/(ym_B - ym_A) ~ f_in na 1e-7.
// ============================================================
`timescale 1ps/1fs
module tb_top_regr;
    reg clk10 = 0, clk100 = 0;
    always #50000 clk10 = ~clk10;
    initial begin #2300; forever #5000 clk100 = ~clk100; end    // pevny fazovy posun proti 10 MHz
    real TSIG = 100000.0 / (1.0 + 813e-9);
    reg ch_a = 0;
    initial begin #12345; forever begin ch_a = 1; #(TSIG/2.0); ch_a = 0; #(TSIG/2.0); end end
    wire miso, led;
    top dut (.clk_ref_10m(clk10), .clk_p0_100m(clk100), .ch_a(ch_a), .ch_b(1'b0), .led_tx(led),
             .spi_sck(1'b0), .spi_cs_n(1'b1), .spi_mosi(1'b0), .spi_miso(miso));

    real U, f_in, fr, fdt;
    integer win = 0, errs = 0;
    reg tg_p = 0;
    initial begin
        U = 0.6103515625 / 16.0;    // ym jednotka [ps]
        f_in = 1.0e12 / TSIG;
    end
    always @(posedge clk100) begin
        tg_p <= dut.res_tgl_a;
        if (dut.res_tgl_a !== tg_p && dut.cal_valid_a && !dut.cal_busy_a) begin
            win = win + 1;
            fdt = real'(dut.r_periods_a) / (real'(dut.r_dt_a) * 0.6103515625) * 1.0e12;
            $display("okno %0d: N=%0d dt=%0d u  f_2pt=%.4f Hz (chyba %.2f ppm) | A: ok=%0d n=%0d xm=%.1f ym=%.3f us | B: ok=%0d n=%0d xm=%.1f ym=%.3f us",
                win, dut.r_periods_a, dut.r_dt_a, fdt, (fdt/f_in-1.0)*1e6,
                dut.rg_ok_a, dut.rg_n_a, real'(dut.rg_xm_a)/256.0, real'(dut.rg_ym_a)*U*1e-6,
                dut.rg_ok_b, dut.rg_n_b, real'(dut.rg_xm_b)/256.0, real'(dut.rg_ym_b)*U*1e-6);
            if (dut.rg_ok_a && dut.rg_ok_b && dut.rg_ym_b > dut.rg_ym_a) begin
                fr = (real'(dut.rg_xm_b) - real'(dut.rg_xm_a)) / 256.0 / (((real'(dut.rg_ym_b) - real'(dut.rg_ym_a)) * U) * 1e-12);
                $display("        f_regr = %.4f Hz (chyba %.3f ppm)", fr, (fr/f_in-1.0)*1e6);
                if (win >= 3 && ((fr/f_in-1.0) > 1e-6 || (fr/f_in-1.0) < -1e-6)) errs = errs + 1;
            end else if (win >= 3) errs = errs + 1;
            if (win == 6) begin
                if (errs == 0) $display("PASS: tb_top_regr"); else $display("FAIL: tb_top_regr (%0d oken bez platne regrese)", errs);
                $finish;
            end
        end
    end
    initial begin #(64'd60_000_000_000); $display("FAIL: timeout (cal_valid=%0d busy=%0d)", dut.cal_valid_a, dut.cal_busy_a); $finish; end
endmodule
