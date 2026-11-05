// ============================================================
// tb_hold_inv.sv -- 2026-10-24: cileny test predpocitanych registru optimalizace casovani v top_v3 (SIM_TOP).
// Zadny test topv3* nevyvola ztratu hodin (ok_s[1] -> 0, novy nabeh boot_done) a zmena predelicky / zdroje B je jen
// v rezimech 3 a 4. Tady se v kratkem behu (~3 ms) vynuti:
//   * ztrata hodin: force dut.clk_ok = 0 (hlidac 100 MHz v domene 10 MHz) -> ok_s[1] = 0 -> boot_done = 0 -> novy nabeh,
//   * zmena predelicky obou kanalu (force u_app.pr_a_req / pr_b_req) -> pchg_a / pchg_b (2 hranice segmentu),
//   * zmena zdroje B (force u_app.bsrc) -> bchg,
// a v KAZDEM taktu se kontroluje, ze kopie hold_a_s / hold_b_s (z predpocitanych hx_a / hx_b) jsou shodne s hold_a /
// hold_b (puvodni vyraz) a ze plati vsechny invarianty kopii (stejny blok jako v tb_top_v3.sv). Pokryti: pocet poklesu
// boot_done, udalosti pchg_a / pchg_b / bchg a hranic segmentu musi byt > 0.
// Spusteni: run.ps1 holdinv (run_ps1_doplnek.txt), ~1 min.
// ============================================================
`timescale 1ps/1fs
`include "tb_clk.vh"
module tb_hold_inv;
    reg clk10 = 0, clk100 = 0;
    always #50000 clk10 = ~clk10;
    initial begin #2300; forever #(`TB_THALF_PS) clk100 = ~clk100; end
    reg ch_a = 0, ch_b = 0, pps = 0;
    initial begin #12345; forever begin ch_a = 1; #(50000); ch_a = 0; #(50000); end end          // 10 MHz
    initial begin #23456; forever begin ch_b = 1; #(151515); ch_b = 0; #(151515); end end        // ~3,3 MHz
    wire miso, led, fok, refc;
    wire [1:0] ps_ck, ps_ckn, ps_csn, ps_rstn; wire [1:0] ps_rwds; wire [15:0] ps_dq;
    top_v3 dut (.clk_ref_10m(clk10), .clk_p0_100m(clk100), .ch_a(ch_a), .ch_b(ch_b), .led_tx(led), .fpga_ok(fok),
                .ref_ctrl(refc), .ext_ref_sens(1'b0), .gps_1pps(pps), .spi_sck(1'b0), .spi_cs_n(1'b1), .spi_mosi(1'b0), .spi_miso(miso),
                .O_psram_ck(ps_ck), .O_psram_ck_n(ps_ckn), .O_psram_cs_n(ps_csn), .O_psram_reset_n(ps_rstn),
                .IO_psram_rwds(ps_rwds), .IO_psram_dq(ps_dq));

    integer errors = 0, nchk = 0;
    task chk(input cond, input [8*64-1:0] what);
        begin nchk = nchk + 1; if (!cond) begin errors = errors + 1; if (errors < 20) $display("  CHYBA: %0s (t = %0t)", what, $time); end end endtask

    // invarianty kopii a predpocitanych registru (jako tb_top_v3.sv) + kopie hold_x_s == hold_x
    reg inv_ok = 1'b1;
    integer ncyc = 0, n_bd_fall = 0, n_pca = 0, n_pcb = 0, n_bch = 0, n_seg = 0, n_hold_tgl = 0;
    reg bd_q = 1'b0, pca_q = 1'b0, pcb_q = 1'b0, bch_q = 1'b0, ha_q = 1'b1;
    // 2026-10-25 (krok kruh v 16 SDPB): invariant zapisovace (takt a data kazdeho slova zaznamu); pokryti: hold B se zmeni
    // v okne E+7..E+15, kdy drivejsi zapisovac uz precetl slovo 0 A, ale jeste ne slovo 0 B (FLAGS B se ctou zive)
`include "tb_ring_inv.vh"
    integer n_hbw = 0;
    reg hb_q = 1'b1;
    task bsrc_win(input [1:0] v);
        begin
            @(posedge dut.u_ring.seg_tick);
            while (!(dut.hold_b_s === 1'b0 && !dut.cal_busy_a && !dut.cal_busy_b)) @(posedge dut.u_ring.seg_tick);
            repeat (5) @(posedge clk100);
            #1 force dut.u_app.bsrc = v;
        end
    endtask
    always @(posedge clk100) begin
        inv_ok = (dut.u_pps.wnz === (dut.u_pps.win != 5'd0)) && (dut.u_pps.wnz_b === dut.u_pps.wnz)
              && (dut.u_pps.pend_b === dut.u_pps.pend) && (dut.u_pps.fok_b === dut.u_pps.fok) && (dut.u_pps.wok_b === dut.u_pps.wok)
              && (dut.u_lws.lw_a_qa === dut.u_lws.lw_a_qb) && (dut.u_lws.lw_n_qa === dut.u_lws.lw_n_qb)
              && (dut.u_pps.dhi_r === (~dut.u_pps.dd_r[33] & (dut.u_pps.dd_r[32:7] != 26'd0)))
              && (dut.u_pps.dlo_r === (dut.u_pps.dd_r[33] & (dut.u_pps.dd_r[32:7] != {26{1'b1}})))
              && (dut.u_sa.fnz === (dut.u_sa.fn != 2'd0)) && (dut.u_sb.fnz === (dut.u_sb.fn != 2'd0))
              && (dut.u_sa.first_b === dut.u_sa.first) && (dut.u_sb.first_b === dut.u_sb.first)
              && (dut.u_ring.nfc_a === (~dut.u_ring.freeze & ~dut.u_ring.clear_a))
              && (dut.u_ring.nfc_b === (~dut.u_ring.freeze & ~dut.u_ring.clear_b))
              && (dut.bs2_s === dut.bs2) && (dut.pr_a_s === dut.pr_a) && (dut.pr_b_s === dut.pr_b)
              && (dut.hx_a === (~dut.boot_done | (dut.pchg_a != 2'd0)))
              && (dut.hx_b === (~dut.boot_done | (dut.bchg != 2'd0) | (dut.pchg_b != 2'd0)))
              && (dut.hold_a_s === dut.hold_a) && (dut.hold_b_s === dut.hold_b);
        if (!inv_ok) chk(1'b0, "invariant optimalizace (kopie / predpocitany registr)");
        if (ri_e >= 7 && ri_e < 15 && hb_q !== dut.hold_b_s) n_hbw = n_hbw + 1;   // hold B se zmenil mezi E+7 a E+15
        hb_q = dut.hold_b_s;
        ncyc = ncyc + 1;
        if (bd_q && !dut.boot_done) n_bd_fall = n_bd_fall + 1;
        if (!pca_q && dut.pchg_a != 2'd0) n_pca = n_pca + 1;
        if (!pcb_q && dut.pchg_b != 2'd0) n_pcb = n_pcb + 1;
        if (!bch_q && dut.bchg != 2'd0) n_bch = n_bch + 1;
        if (dut.seg_tick) n_seg = n_seg + 1;
        if (ha_q != dut.hold_a_s) n_hold_tgl = n_hold_tgl + 1;
        bd_q = dut.boot_done; pca_q = (dut.pchg_a != 2'd0); pcb_q = (dut.pchg_b != 2'd0); bch_q = (dut.bchg != 2'd0);
        ha_q = dut.hold_a_s;
    end

    initial begin
        #(64'd600_000_000);                                       // 600 us: nabeh, kalibrace bezi
        chk(dut.boot_done === 1'b1, "boot_done po nabehu");
        force dut.u_app.pr_a_req = 3'd1;                          // predelicka A: plati od hranice segmentu
        #(64'd300_000_000);
        force dut.u_app.pr_b_req = 3'd2;
        #(64'd300_000_000);
        force dut.u_app.bsrc = 2'b01;                             // zdroj B = pin A
        #(64'd400_000_000);
        force dut.clk_ok = 1'b0;                                  // ztrata hodin (hlidac): ok_s -> 0, boot_done -> 0
        #(64'd5_000_000);
        release dut.clk_ok;                                       // hlidac ho v dalsim okne obnovi
        #(64'd400_000_000);
        force dut.u_app.pr_a_req = 3'd0; force dut.u_app.pr_b_req = 3'd0; force dut.u_app.bsrc = 2'b00;
        // ztrata hodin tesne pred hranici segmentu (boot_done a pchg se meni blizko sebe)
        @(posedge dut.seg_tick); #(64'd200_000_000);
        force dut.clk_ok = 1'b0; #(64'd3_000_000); release dut.clk_ok;
        #(64'd600_000_000);
        // 2026-10-25: zmena zdroje B tak, aby se hold B zmenil v okne zapisu E+7..E+15 (bsrc -> bs1 -> bs2 -> bs3 != bs2 ->
        // bchg -> hold_b_s: 5 taktu); jednou nahoru a jednou dolu
        // (v hranici, kdy B meri platne a nic se nekalibruje -- jinak by hold B uz byl 1 z jineho duvodu)
        bsrc_win(2'b01);
        #(64'd1_000_000_000);
        bsrc_win(2'b00);
        #(64'd600_000_000);
        chk(n_bd_fall >= 2, "pokryti: boot_done kleslo aspon 2x");
        chk(n_pca >= 2 && n_pcb >= 2 && n_bch >= 2, "pokryti: pchg_a, pchg_b, bchg aspon 2x");
        chk(n_seg >= 8, "pokryti: aspon 8 hranic segmentu");
        chk(n_hbw >= 2, "pokryti: hold B se zmenil mezi zapisem slova 0 A a slova 0 B (E+7..E+15)");
        chk(ri_nseg >= 8, "pokryti: invariant zapisovace kruhu aspon 8x");
        $display("taktu %0d, poklesu boot_done %0d, pchg_a %0d, pchg_b %0d, bchg %0d, hranic segmentu %0d, zmen hold_a_s %0d, kontrol %0d",
                 ncyc, n_bd_fall, n_pca, n_pcb, n_bch, n_seg, n_hold_tgl, nchk);
        $display("kruh: zapisu zkontrolovano %0d, zmen hold B v okne E+7..E+15: %0d", ri_nseg, n_hbw);
        if (errors == 0) $display("PASS: tb_hold_inv"); else $display("FAIL: tb_hold_inv (%0d chyb)", errors);
        $finish;
    end
    initial begin #(64'd10_000_000_000); $display("FAIL: tb_hold_inv timeout"); $finish; end
endmodule
