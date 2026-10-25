// ============================================================
// tb_top_v3.sv -- CELY top_v3.v (protokol v3, FW 0x0500) se skutecnym TDC (gowin_models), SIM_TOP: segment 250 us,
// kalibrace 2^12, rychly nabeh. "STM" = BFM po SPI (1024 B, SCK 25 MHz), jako fpga_freq.c v3:
//   * kazda transakce posle ACK s REC_ACK = dalsi ocekavany zaznam a prijme ramec; CRC vsech 4 bloku, hlavicka
//     (MAGIC, VER, FRAME_LEN, NBLK, FW, CAPS), znacky bloku {k, SEQ}, REC_FIRST <= ocekavany (prekryv je normalni:
//     ramec se sklada pred zpracovanim ACK z teze transakce), zadna mezera.
//   * zaznamy: SEG_IDX = poradi/2, CH = poradi & 1; platne zaznamy (FLAGS bit0 = 0): kmitocet dvoubodove ze sousednich
//     segmentu (X0, T0) proti zadanemu, N ~ f*L, u kmitoctu, kde se casuje kazda hrana, SX = N(N-1)/2 PRESNE,
//     sklon ST/SX = perioda; kontrolni pocitani (bit4) = 0.
//   * 1PPS (PSEQ, PTS po 1 ms), stranka histogramu A (= dut.u_tdca.hist), chybna delka / CRC (RX_LEN_ERR, RX_CRC_ERR),
//     SET_CONFIG 0x08 (SEG_LEN 10 ms).
// Kanal B -DTB_FB=13.300161e6 (run.ps1 topv3, od 2026-10-24; drive 16,100161 MHz): perioda 7,52 taktu, rozestup hran
// stridave 7 a 8 taktu -> casuje se kazda hrana a ve FIFO indexu jsou DVE rozpracovane znacky (pop az s tvd = ts_valid
// o takt pozdeji, FW 0x0506) -> FIFO hloubky 1 test shodi. Pri 16,1 MHz (rozestup 6/7) druha polozka FIFO po registru
// tvd nenastavala a mutace FIFO hloubky 1 prosla (L-0228; drive L-0199). Na segment NENI cely pocet period
// (13,3 MHz x 250 us = 3325,04): faze znacek vuci hranici segmentu se posouva ~0,3 taktu za segment, takze test
// projde i znacku prave v taktu clear (L-0198); kmitocet s celym poctem period by hranici zkousel stale ve stejne fazi.
// FW 0x0501: v rezimu 0 navic RAW_CAPTURE (512 znacek kanalu A, stranky druhu 3, rozdily = perioda).
// Rezimy (-DTB_MODE=): 0 = vse vyse (vychozi), 1 = preteceni kruhu (-DSIM_RAW=4: 16 zaznamu, STM chvili necte),
//   2 = VYROBNI tok kalibrace (bez -DSIM_AUTOCAL): STM cte histogram strankami (druh 1/2, 3 stranky), spocte tabulku,
//       zapise ji DAVKOU (LUT_WRITE, 252 polozek na ramec), overi LUT_CNT/LUT_SUM, potvrdi -> banka se prepne na hranici
//       segmentu; pak rekalibrace JEN kanalu A (SET_CONFIG 0x02 maska A): B meri porad, A behem sberu neplatny, po sberu
//       meri dal se starou bankou, nova tabulka do druhe banky,
//   3 = sirka pulzu (FW 0x0501, SET_CONFIG 0x09): kanal A = pulz sirky TB_WA, B = pin A invertovany -> (T0_B - T0_A)
//       mod P = sirka; B = pin A neinvertovany -> zpozdeni mezi kanaly ~0; zpet na pin B -> B meri zase svuj signal.
// ============================================================
`timescale 1ps/1fs
`include "tb_clk.vh"
`ifndef TB_MODE
`define TB_MODE 0
`endif
`ifndef TB_FA
`define TB_FA (10.0e6 * (1.0 + 813.0e-9))
`endif
`ifndef TB_FB
`define TB_FB (3.3e6 * (1.0 - 2.1e-6))
`endif
`ifndef TB_WA
`define TB_WA 0.0
`endif
`ifndef TB_PPS_DPS
`define TB_PPS_DPS 900                    // 1PPS: perioda 1 ms + 900 ps -> poloha hrany v taktu 100 MHz se meni (IDES8)
`endif
`ifndef TB_NFR
`define TB_NFR 14
`endif
module tb_top_v3;
    reg clk10 = 0, clk100 = 0;
    always #50000 clk10 = ~clk10;
    initial begin #2300; forever #(`TB_THALF_PS) clk100 = ~clk100; end
    real FA, FB, HA, HB, PA, PB, WA;
    reg ch_a = 0, ch_b = 0, pps = 0;
    initial begin
        // Icarus zaokrouhli kazde #(real) na presnost 1 fs -> pravda testu musi pocitat se ZAOKROUHLENOU pulperiodou
        // (bez toho drift az ~0,7 fs na periodu: pri 143 MHz stovky ps za tisice segmentu; L-0205)
        HA = $floor(0.5e12 / `TB_FA * 1000.0 + 0.5) / 1000.0; HB = $floor(0.5e12 / `TB_FB * 1000.0 + 0.5) / 1000.0;
        PA = 2.0 * HA; PB = 2.0 * HB;                 // [ps]; skutecna perioda = 2 x zaokrouhlena pulperioda (fs)
        WA = (`TB_WA > 0.0) ? `TB_WA : HA;           // sirka pulzu kanalu A [ps] (vychozi strida 50 %)
    end
    initial begin #12345; forever begin ch_a = 1; #(WA); ch_a = 0; #(PA - WA); end end
    initial begin #23456; forever begin ch_b = 1; #(HB); ch_b = 0; #(HB); end end
    initial begin #(64'd700_000_000); forever begin pps = 1; #(64'd100_000_000); pps = 0; #(64'd900_000_000 + `TB_PPS_DPS); end end   // 1PPS po 1 ms + DPS

    reg sck = 0, cs = 1, mosi = 0; wire miso, led, fok, refc;
    // FW 0x0505: vestavena PSRAM (cip 0 = model, cip 1 nevyuzit)
    wire [1:0] ps_ck, ps_ckn, ps_csn, ps_rstn; wire [1:0] ps_rwds; wire [15:0] ps_dq;
`ifdef TB_PS_LATE
    // audit 2026-10-23 F-0264: vystup PSRAM neplatny 4..6 ns po hrane CK -> vzorek na nabezne hrane clk (5 ns po hrane
    // CK) padne do zmeny, kalibrace musi zvolit fazi 1 (sestupna hrana)
    hb_psram_model #(.TCKD_MIN(4000), .TCKD(6000)) u_ps0 (.ck(ps_ck[0]),
`else
    hb_psram_model u_ps0 (.ck(ps_ck[0]),
`endif .ck_n(ps_ckn[0]), .cs_n(ps_csn[0]), .rst_n(ps_rstn[0]), .dq(ps_dq[7:0]), .rwds(ps_rwds[0]));
    integer ndeep = 0, nwin, w; real dmaxd;
    top_v3 dut (.clk_ref_10m(clk10), .clk_p0_100m(clk100), .ch_a(ch_a), .ch_b(ch_b), .led_tx(led), .fpga_ok(fok),
                .ref_ctrl(refc), .ext_ref_sens(1'b0), .gps_1pps(pps), .spi_sck(sck), .spi_cs_n(cs), .spi_mosi(mosi), .spi_miso(miso),
                .O_psram_ck(ps_ck), .O_psram_ck_n(ps_ckn), .O_psram_cs_n(ps_csn), .O_psram_reset_n(ps_rstn),
                .IO_psram_rwds(ps_rwds), .IO_psram_dq(ps_dq));

    // ---- BFM ----
    function [15:0] crc(input [15:0] c0, input [7:0] d);
        integer b; reg [15:0] c; begin c = c0 ^ {d, 8'd0}; for (b = 0; b < 8; b = b + 1) c = c[15] ? ((c << 1) ^ 16'h1021) : (c << 1); crc = c; end endfunction
    reg [7:0] mo [0:1099];
    reg [7:0] mi [0:1099];
    integer i, j, b, errors = 0, nchk = 0;
    reg [15:0] cc;
    reg [31:0] mseq = 0;
    task seal(input integer blk);
        begin cc = 16'hFFFF; for (i = 0; i < 254; i = i + 1) cc = crc(cc, mo[blk * 256 + i]);
              mo[blk * 256 + 254] = cc[7:0]; mo[blk * 256 + 255] = cc[15:8]; end endtask
    task build(input [7:0] ty, input [7:0] p0, input [7:0] p1, input [7:0] p2, input [7:0] p3);
        begin for (i = 0; i < 1024; i = i + 1) mo[i] = 0;
              mseq = mseq + 1;
              mo[0] = 8'hA5; mo[1] = 8'h03; mo[2] = ty; mo[4] = mseq[7:0]; mo[5] = mseq[15:8]; mo[6] = mseq[23:16]; mo[7] = mseq[31:24];
              mo[8] = 8'h00; mo[9] = 8'h04; mo[10] = 8'd4; mo[12] = p0; mo[13] = p1; mo[14] = p2; mo[15] = p3;
              for (b = 0; b < 4; b = b + 1) seal(b); end endtask
    task build_ack(input [31:0] a); build(8'h06, a[7:0], a[15:8], a[23:16], a[31:24]); endtask
`ifdef TRACE
    // 2026-10-24: STOPA pro dukaz shody chovani (sim/trace_v3.py): kazdy bajt transakce (cas, poradi transakce, index,
    // MOSI, MISO) a zmeny vystupu top_v3 ($fstrobe = hodnoty na konci casoveho kroku; stejne radky slucuje trace_v3.py)
    integer trf, ntr = 0;
    initial trf = $fopen("trace.txt");
    always @(led or fok or refc or ps_ck or ps_ckn or ps_csn or ps_rstn or ps_rwds or ps_dq)
        $fstrobe(trf, "%0t O %b %b %b %b %b %b %b %b %h", $time, led, fok, refc, ps_ck, ps_ckn, ps_csn, ps_rstn, ps_rwds, ps_dq);
`endif
    task xfer(input integer nbytes);
        begin cs = 0; #(64'd1_000_000);
`ifdef TRACE
              ntr = ntr + 1;
`endif
              for (i = 0; i < nbytes; i = i + 1) begin mi[i] = 0;
                  for (j = 7; j >= 0; j = j - 1) begin mosi = mo[i][j]; #(64'd20_000); sck = 1; mi[i][j] = miso; #(64'd20_000); sck = 0; end
`ifdef TRACE
                  $fdisplay(trf, "%0t X %0d %0d %h %h", $time, ntr, i, mo[i], mi[i]);
`endif
              end
              #(64'd1_000_000); cs = 1; #(64'd200_000_000); end endtask   // mezera 200 us

    function [3:0] gr(input [3:0] b); gr = b ^ {1'b0, b[3:1]}; endfunction   // Gray (bajt 127, F-0266)
    // FW 0x0507 (uzka hlavicka): 16b pole rozbalena k nejblizsi hodnote kolem reference (jako STM fpga_v3_unwrap)
    function [31:0] unw(input [15:0] v, input [31:0] r);
        reg [15:0] dlt;
        begin dlt = v - r[15:0]; unw = r + {{16{dlt[15]}}, dlt}; end
    endfunction
    function [63:0] rd(input integer off, input integer n);
        integer q; reg [63:0] v; begin v = 0; for (q = n - 1; q >= 0; q = q - 1) v = (v << 8) | mi[off + q]; rd = v; end endfunction

    // ---- "STM": prijem ramce ----
    reg [31:0] nxt = 0, hseq = 0, pseq = 0;
    reg [7:0]  nz;
    integer nmask = 0, ninv_a = 0, novf = 0;
    // FW 0x0503: platne zaznamy podle kanalu a log2 P (FLAGS 7:5), neplatne B, ocekavana sonda
    integer nvp [0:1][0:7]; integer ninv_b = 0, ki, kj; real pr_e; reg [2:0] r_pl;
    initial for (ki = 0; ki < 2; ki = ki + 1) for (kj = 0; kj < 8; kj = kj + 1) nvp[ki][kj] = 0;
    // pokryti: znacka prave v taktu clear (konec freeze = zacatek novych souctu, L-0198). Kanal B (run.ps1 topv3,
    // -DTB_FB=13.300161e6) nema na segment cely pocet period -> faze jeho znacek vuci hranici segmentu se posouva
    // ~0,3 taktu za segment.
    integer ncl_a = 0, ncl_b = 0;
    always @(posedge clk100) begin
        if (dut.u_sa.clear && dut.u_sa.pop) ncl_a = ncl_a + 1;
        if (dut.u_sb.clear && dut.u_sb.pop) ncl_b = ncl_b + 1;
    end
    reg [23:0] rawi [0:1023]; reg [47:0] rm0, rm1; integer kk, nraw1; real rawe;
    real t0ref [0:1], t0res, t0max [0:1]; reg t0have [0:1];
    localparam real T0LIM = (`TB_MODE == 2) ? 500.0 : 300.0;   // rezim 2: tabulka STM z 2^12 udalosti (vetsi INL)
    initial begin t0have[0] = 0; t0have[1] = 0; t0max[0] = 0; t0max[1] = 0; end
    integer nfr = 0, nrec_tot = 0, dup = 0, nvalid_a = 0, nvalid_b = 0, nflag_ccd = 0;
    reg [7:0] r_fl; reg [15:0] r_seg; reg [7:0] r_ch; reg [23:0] r_n; reg [39:0] r_x0; reg [47:0] r_t0, r_sx; reg [55:0] r_st;
    reg        have [0:1];
    reg [39:0] px0 [0:1]; reg [47:0] pt0 [0:1]; reg [31:0] pord [0:1];
    real f2, ferr, maxe [0:1], sl, slerr, maxsl [0:1], U, P, Fc;
    // FW 0x0501: zdroj kanalu B (bfa = pin A, binv = invertovany) a parovani A/B v jednom segmentu
    reg bfa = 0, binv = 0, haveA = 0; reg [47:0] tA0, dd; reg [31:0] ordA;
    real dps, dmod, werr, wmax = 0.0; integer nw = 0;
    reg [31:0] pps_last_seq = 0; reg [47:0] pps_last_ts = 0; integer npps = 0;
    // FW 0x0502: jemny cas 1PPS (IDES8). Skutecna hrana k (PSEQ = k + 1) v 700 us + k * (1 ms + DPS).
    real    pf_ref = 0.0, pf_e, pf_max = 0.0; reg pf_have = 0; integer npf = 0, dmin = 127, dmax = -128;
    reg [7:0] pf_pos = 8'd0; reg [3:0] pf_ep = 4'd0; reg [7:0] pf_d;
    initial begin U = `TB_PS_PER_UNIT; have[0] = 0; have[1] = 0; maxe[0] = 0; maxe[1] = 0; maxsl[0] = 0; maxsl[1] = 0; end
    task chk(input cond, input [8*64-1:0] what);
        begin nchk = nchk + 1; if (!cond) begin errors = errors + 1; if (errors < 20) $display("  CHYBA: %0s", what); end end endtask
`ifndef NO_OPT_INV
    // 2026-10-24: INVARIANTY optimalizaci casovani -- kopie registru a predpocitane registry musi byt v KAZDEM taktu shodne
    // s puvodnim vyrazem (i ve stavech, ktere se do vystupu nepromitnou). -DNO_OPT_INV vypne (preklad proti base/ pro stopu).
    reg inv_ok = 1'b1;
    always @(posedge clk100) begin
        inv_ok = (dut.u_pps.wnz === (dut.u_pps.win != 5'd0)) && (dut.u_pps.wnz_b === dut.u_pps.wnz)
              && (dut.u_pps.pend_b === dut.u_pps.pend) && (dut.u_pps.fok_b === dut.u_pps.fok) && (dut.u_pps.wok_b === dut.u_pps.wok)
              && (dut.u_lws.lw_a_qa === dut.u_lws.lw_a_qb) && (dut.u_lws.lw_n_qa === dut.u_lws.lw_n_qb)
              && (dut.u_sa.fnz === (dut.u_sa.fn != 2'd0)) && (dut.u_sb.fnz === (dut.u_sb.fn != 2'd0))
              && (dut.u_sa.first_b === dut.u_sa.first) && (dut.u_sb.first_b === dut.u_sb.first)
              && (dut.u_ring.nfc_a === (~dut.u_ring.freeze & ~dut.u_ring.clear_a))
              && (dut.u_ring.nfc_b === (~dut.u_ring.freeze & ~dut.u_ring.clear_b))
              && (dut.bs2_s === dut.bs2) && (dut.pr_a_s === dut.pr_a) && (dut.pr_b_s === dut.pr_b)
              && (dut.hx_a === (~dut.boot_done | (dut.pchg_a != 2'd0)))
              && (dut.hx_b === (~dut.boot_done | (dut.bchg != 2'd0) | (dut.pchg_b != 2'd0)))
              && (dut.hold_a_s === dut.hold_a) && (dut.hold_b_s === dut.hold_b);
        if (!inv_ok) chk(1'b0, "invariant optimalizace (kopie / predpocitany registr)");
    end
    // 2026-10-25: kopie ohA* zapisovace kruhu zanikly (16 SDPB) -> invariant zapisovace: takt a data kazdeho slova
`include "tb_ring_inv.vh"
`endif
    task check_frame(input integer want_data);
        integer k, m, ord, off, nrec;
        reg [31:0] first, caps;
        begin
            // CRC bloku
            for (k = 0; k < 4; k = k + 1) begin
                cc = 16'hFFFF; for (i = 0; i < 254; i = i + 1) cc = crc(cc, mi[k * 256 + i]);
                chk({mi[k * 256 + 255], mi[k * 256 + 254]} === cc, "CRC bloku MISO");
            end
            chk(mi[0] === 8'hA5 && mi[1] === 8'h03, "MAGIC/VER");
            chk(rd(8, 2) == 1024 && mi[10] == 4, "FRAME_LEN/NBLK");
            caps = rd(24, 4);
            chk(rd(20, 2) == 16'h0507, "FW_VERSION 0x0507");
            // uzka hlavicka (CAPS bit22): zrusena / zuzena pole jsou 0
            chk(caps[22], "CAPS bit22 (uzka hlavicka)");
            nz = 0;
            for (k = 6; k < 8; k = k + 1)     nz = nz | mi[k];
            for (k = 28; k < 32; k = k + 1)   nz = nz | mi[k];
            for (k = 34; k < 36; k = k + 1)   nz = nz | mi[k];
            for (k = 38; k < 40; k = k + 1)   nz = nz | mi[k];
            for (k = 42; k < 44; k = k + 1)   nz = nz | mi[k];
            for (k = 56; k < 60; k = k + 1)   nz = nz | mi[k];
            for (k = 70; k < 72; k = k + 1)   nz = nz | mi[k];
            for (k = 84; k < 96; k = k + 1)   nz = nz | mi[k];
            for (k = 100; k < 108; k = k + 1) nz = nz | mi[k];
            for (k = 112; k < 116; k = k + 1) nz = nz | mi[k];
            chk(nz == 8'd0, "uzka hlavicka: zrusena pole = 0");
`ifdef DEEP_RAW
            chk(caps[21], "CAPS bit21 (hluboky RAW do PSRAM)");
`else
            chk(!caps[21], "CAPS bit21 = 0 (bez PSRAM, od FW 0x0506)");
`endif
            chk(caps[18], "CAPS bit18 (predelicka pin_cnt3)");
            chk(caps[17], "CAPS bit17 (1PPS pres IDES8)");
            chk(caps[16] && caps[19] && caps[13] && caps[20], "CAPS v3/banky/1PPS/zdroj B");
            chk(mi[22] == 100, "CLK_MHZ");
            hseq = unw(rd(4, 2), hseq);
            if (want_data && mi[2] == 8'h80) begin
                first = unw(rd(36, 2), nxt); nrec = mi[40];
                chk(nrec <= 24, "NREC <= 24");
                if (first > nxt && mi[3][1]) begin                     // preteceni kruhu: mezera s priznakem
                    novf = novf + 1; nxt = first; have[0] = 0; have[1] = 0;
                end
                chk(first <= nxt, "REC_FIRST > ocekavany (mezera bez priznaku preteceni)");
                for (k = 1; k < 4; k = k + 1) chk(mi[k * 256] == k && mi[k * 256 + 1] == hseq[7:0], "znacka bloku {k, SEQ}");
                for (m = 0; m < nrec; m = m + 1) begin
                    ord = first + m;
                    off = (1 + m / 8) * 256 + 2 + 31 * (m % 8);
                    if (ord < nxt) begin dup = dup + 1; end
                    else if (ord == nxt) begin
                        nxt = nxt + 1; nrec_tot = nrec_tot + 1;
                        r_seg = rd(off, 2); r_ch = mi[off + 2] & 8'h0F; r_pl = mi[off + 2][6:4]; r_fl = mi[off + 3]; r_n = rd(off + 4, 3);
                        r_x0 = rd(off + 7, 5); r_t0 = rd(off + 12, 6); r_sx = rd(off + 18, 6); r_st = rd(off + 24, 7);
                        chk(r_seg == ord[16:1], "SEG_IDX = poradi/2");
                        chk(r_ch == ord[0], "CH = poradi & 1");
                        if (!r_fl[0] && r_n > 0) begin
`ifdef TB_DBG2
                            if (r_ch) $display("DBG2 rec %0d B n %0d x0 %0d t0res %.0f ps fl %02h", ord, r_n, r_x0,
                                               real'(r_t0) * U - (23456.0 + real'(r_x0 - 1) * PB), r_fl);
`endif
`ifdef TB_DBG
                            $display("DBG rec %0d ch %0d n %0d x0 %0d t0 %0d sx %0d st %0d fl %02h", ord, r_ch, r_n, r_x0, r_t0, r_sx, r_st, r_fl);
`endif
                            // FW 0x0503: X pocita delene hrany -> perioda zaznamu = perioda pinu x 2^p (CH bity 6:4)
                            P  = ((r_ch && !bfa) ? PB : PA) * real'(1 << r_pl); Fc = 1.0e12 / P;
                            nvp[r_ch & 1][r_pl] = nvp[r_ch & 1][r_pl] + 1;
                            chk(!r_fl[1], "platny zaznam bez FLAGS bit1 (zmena P)");
                            // sirka pulzu / zpozdeni: prvni hrana B proti prvni hrane A teze hranice segmentu (mod perioda)
                            if (r_ch == 0) begin haveA = 1; ordA = ord; tA0 = r_t0; end
                            else if (bfa && haveA && ordA == ord - 1) begin
                                dd = r_t0 - tA0;
                                dps = dd[47] ? (real'(dd) - 281474976710656.0) * U : real'(dd) * U;
                                dmod = dps - $floor(dps / PA) * PA;
                                werr = binv ? (dmod - WA) : ((dmod > PA / 2.0) ? dmod - PA : dmod);
                                if (werr < 0) werr = -werr;
                                if (werr > wmax) wmax = werr;
                                nw = nw + 1;
                                chk(werr < 300.0, binv ? "sirka pulzu (T0_B - T0_A mod P)" : "zpozdeni A/B (stejna hrana)");
                            end
                            if (r_ch) nvalid_b = nvalid_b + 1; else nvalid_a = nvalid_a + 1;
                            // T0 = cas hrany X0 (generator kanalu A zacina v 12345 ps, B v 23456 ps)
                            t0res = real'(r_t0) * U - ((r_ch ? 23456.0 : 12345.0) + real'(r_x0 - 1) * P);
                            if (!t0have[r_ch]) begin t0have[r_ch] = 1; t0ref[r_ch] = t0res; end
                            if ((t0res - t0ref[r_ch]) > t0max[r_ch]) t0max[r_ch] = t0res - t0ref[r_ch];
                            if ((t0ref[r_ch] - t0res) > t0max[r_ch]) t0max[r_ch] = t0ref[r_ch] - t0res;
                            chk((t0res - t0ref[r_ch]) < T0LIM && (t0ref[r_ch] - t0res) < T0LIM, "T0 = cas hrany X0 (parovani indexu a znacky)");
                            if ((t0res - t0ref[r_ch]) >= T0LIM || (t0ref[r_ch] - t0res) >= T0LIM)
                                $display("  T0: ord %0d ch %0d n %0d odchylka %.0f ps", ord, r_ch, r_n, t0res - t0ref[r_ch]);
                            chk(r_fl[4] == 0, "kontrolni pocitani (FLAGS bit4)");
                            if (r_fl[4]) nflag_ccd = nflag_ccd + 1;
                            // N ~ f * L (kazda hrana casovana, nejvys ~3 znacky vypadnou na hranici); pri maskovani CS mene
                            if (r_n * P < real'(dut.SEG25) * `TB_TCLK_R - 8.0 * P) nmask = nmask + 1;
                            if (P > 5.5 * `TB_TCLK_R && !dut.mask_cs_en)
                                chk((r_n * P <= real'(dut.SEG25) * `TB_TCLK_R + P) && (r_n * P >= real'(dut.SEG25) * `TB_TCLK_R - 8.0 * P), "N ~ f*L");
                            // kazda hrana casovana -> indexy po sobe -> SX = N(N-1)/2 presne
                            if (P > 5.5 * `TB_TCLK_R) chk(r_sx == (r_n * (r_n - 1)) / 2, "SX = N(N-1)/2");
                            // sklon v segmentu: ST/SX = perioda [jednotky]
                            if (r_sx > 0) begin
                                sl = real'(r_st) / real'(r_sx) * U; slerr = (sl / P - 1.0); if (slerr < 0) slerr = -slerr;
                                if (slerr > maxsl[r_ch]) maxsl[r_ch] = slerr;
                                // mez: ST/SX je pomerovy odhad, chyba 2 (e_prumer - e0) / rozpeti -> 2 x 100 ps (chyba jedne
                                // znacky modelu TDC vc. INL tabulky az ~100 ps; T0 proti pravde = rozdil dvou znacek az 181 ps A,
                                // 159 ps B) na rozpeti znacek segmentu. Puvodnich 2 x 60 ps nestacilo, kdyz B prosel vsechny
                                // faze TDC (L-0198).
                                chk(slerr < 2e-6 + 200.0 / (real'(r_n - 1) * P), "ST/SX = perioda");
                            end
                            // dvoubodove ze sousednich segmentu
                            if (have[r_ch] && ord == pord[r_ch] + 2) begin
                                f2 = real'((r_x0 - px0[r_ch]) & 40'hFFFFFFFFFF) /
                                     (real'((r_t0 - pt0[r_ch]) & 48'hFFFFFFFFFFFF) * U) * 1.0e12;
                                ferr = f2 / Fc - 1.0; if (ferr < 0) ferr = -ferr;
                                if (ferr > maxe[r_ch]) maxe[r_ch] = ferr;
                                // mez: 300 ps (2 znacky, 5 sigma) na odstup prvnich znacek; segment 250 us -> 1,2e-6
                                chk(ferr < 2e-7 + 300.0 / (real'((r_t0 - pt0[r_ch]) & 48'hFFFFFFFFFFFF) * U), "kmitocet dvoubodove");
                                if (ferr >= 2e-7 + 300.0 / (real'((r_t0 - pt0[r_ch]) & 48'hFFFFFFFFFFFF) * U)) $display("  2pt: ord %0d ch %0d n %0d x0 %0d (prev %0d, ord %0d) t0 %0d (prev %0d) dX %0d dT %.3f us e %.2e fl %02h",
                                    ord, r_ch, r_n, r_x0, px0[r_ch], pord[r_ch], r_t0, pt0[r_ch], r_x0 - px0[r_ch],
                                    real'(r_t0 - pt0[r_ch]) * U * 1e-6, ferr, r_fl);
                            end
                            have[r_ch] = 1; px0[r_ch] = r_x0; pt0[r_ch] = r_t0; pord[r_ch] = ord;
                        end else begin
                            // neplatny / prazdny zaznam: souvislost X neni zarucena (sber histogramu krmi retez ring
                            // oscilatorem -> hrany signalu se nepocitaji) -> nova reference T0 i dvoubodoveho
                            have[r_ch] = 0; t0have[r_ch] = 0;
                            if (r_ch == 0) haveA = 0;
                            if (r_ch == 0 && r_fl[0]) ninv_a = ninv_a + 1;
                            if (r_ch == 1 && r_fl[0]) ninv_b = ninv_b + 1;
                        end
                    end
                end
                nfr = nfr + 1;
            end
            // 1PPS: PSEQ / PTS posledni hrany; po sobe jdouci hrany 1 ms = 1e5 taktu
            pseq = unw(rd(68, 2), pps_last_seq);
            if (pseq != 0 && pseq != pps_last_seq) begin
                if (pps_last_seq != 0 && pseq == pps_last_seq + 1) begin
                    chk(((rd(72, 6) - pps_last_ts) >> 14) >= 99999 && ((rd(72, 6) - pps_last_ts) >> 14) <= 100001, "1PPS po 1 ms");
                    npps = npps + 1;
                end
                // jemny cas: PFLAGS bit1, PPS_STATUS bit0 (PCLK bezi), epocha stala; PTS proti skutecne hrane
                if (mi[78][1]) begin
                    pf_e = real'(rd(72, 6)) * U - (700.0e6 + real'(pseq - 1) * (1.0e9 + real'(`TB_PPS_DPS)));
                    if (!pf_have) begin pf_have = 1; pf_ref = pf_e; pf_ep = mi[50][7:4]; end
                    if ((pf_e - pf_ref) > pf_max) pf_max = pf_e - pf_ref;
                    if ((pf_ref - pf_e) > pf_max) pf_max = pf_ref - pf_e;
                    chk((pf_e - pf_ref) < 1300.0 && (pf_ref - pf_e) < 1300.0, "1PPS jemny cas = skutecna hrana (+-1,25 ns)");
                    chk(mi[50][0] && mi[50][7:4] == pf_ep && !mi[50][2], "PPS_STATUS: PCLK bezi, epocha stala");
                    pf_d = mi[79];
                    if ($signed(pf_d) < dmin) dmin = $signed(pf_d);
                    if ($signed(pf_d) > dmax) dmax = $signed(pf_d);
                    pf_pos[rd(72, 2) >> 11 & 7] = 1'b1;
                    npf = npf + 1;
                end
                pps_last_seq = pseq; pps_last_ts = rd(72, 6);
            end
        end endtask

`ifdef TB_DBG
    integer dn = 0; real tprev = 0;
    integer dnb = 0;
    always @(posedge clk100) if (dut.tsv_b && dut.cal_valid_b && dnb < 12) begin
        dnb = dnb + 1;
        $display("DBG B ts=%0d frac %0d code %0d lut_q3=%h", dut.ts_b, dut.ts_b & 16383, dut.u_tdcb.code_r, dut.u_tdcb.lut_q3);
    end
    initial begin
        wait (dut.cal_valid_a && dut.cal_valid_b);
        for (dn = 0; dn < 300; dn = dn + 1) if (dut.u_tdca.hist[dn] != 0) $display("DBG histA[%0d]=%0d lutA1=%h lutA0=%h", dn, dut.u_tdca.hist[dn], dut.u_tdca.lut[512+dn], dut.u_tdca.lut[dn]);
        for (dn = 0; dn < 300; dn = dn + 1) if (dut.u_tdcb.hist[dn] != 0 && dn < 40) $display("DBG histB[%0d]=%0d lutB1=%h", dn, dut.u_tdcb.hist[dn], dut.u_tdcb.lut[512+dn]);
        dn = 0;
    end
    always @(posedge clk100) if (dut.tsv_a && dut.cal_valid_a && dn < 30) begin
        dn = dn + 1;
        $display("DBG ts_a=%0d (tick %0d frac %0d code %0d) d=%.1f u, lut_q3=%h bank=%0d x=%0d", dut.ts_a, dut.ts_a >> 14, dut.ts_a & 16383, dut.u_tdca.code_r,
                 real'(dut.ts_a) - tprev, dut.u_tdca.lut_q3, dut.bank_a, dut.u_sa.fh);
        tprev = real'(dut.ts_a);
    end
`endif
`ifdef TB_DBG2
    // kazda znacka B proti skutecnemu casu hrany: hrana x (index od 1 = prvni nabezna) je v case 23456 + (x-1)*PB [ps]
    real res, res0 = 0.0, rmax = 0.0; integer nres = 0, nout = 0; reg [39:0] xb; reg prev_cs = 1; integer since_cs = 0;
    always @(posedge clk100) begin
        if (dut.tsv_b && dut.cal_valid_b && !dut.hold_b && dut.u_sb.fn != 0) begin
            xb = dut.u_sb.fh;
            res = real'(dut.ts_b) * `TB_PS_PER_UNIT - (23456.0 + real'(xb - 1) * PB);
            if (nres == 0) begin res0 = res; $display("DBG2 res0 %.0f ps", res0); end
            nres = nres + 1;
            if ((res - res0) > 400.0 || (res - res0) < -400.0) begin
                nout = nout + 1;
                if (nout < 25) $display("DBG2 B x=%0d res=%.0f ps (od prvni %.0f) code=%0d cs=%0d since_cs=%0d t=%0t", xb, res, res - res0, dut.u_tdcb.code_r, ~cs, since_cs, $time);
            end
            if ((res - res0) > rmax) rmax = res - res0;
            if ((res0 - res) > rmax) rmax = res0 - res;
        end
    end
    always @(posedge clk100) begin if (!cs) since_cs = 0; else since_cs = since_cs + 1; end
    final $display("DBG2 B znacek %0d, odlehlych (>400 ps) %0d, max|res| %.0f ps", nres, nout, rmax);
`endif
    // ---- "STM": tabulka TDC z histogramu (stranky), zapis davkou ----
    localparam NC = 512;
    reg [23:0] hh [0:NC-1];
    reg [15:0] lt [0:NC-1];
    reg [15:0] lsum; integer lcnt;
    task read_hist(input integer ch);
        integer t2, k2, j2;
        begin for (t2 = 0; t2 < 3; t2 = t2 + 1) begin
                build(8'hA0, ch + 1, t2[7:0], 8'd0, 0); xfer(1024);
                build_ack(nxt); xfer(1024);
                chk(mi[2] == 8'hA0 && mi[116] == ch + 1 && rd(117, 2) == t2, "kalibrace: stranka histogramu");
                for (k2 = 0; k2 < 252; k2 = k2 + 1) if (t2 * 252 + k2 < NC) begin
                    j2 = (1 + k2 / 84) * 256 + 2 + 3 * (k2 % 84);
                    hh[t2 * 252 + k2] = rd(j2, 3);
                end
            end end endtask
    task calc_lut;
        integer a; longint cum, xv, xsh, x14; reg [13:0] neg; reg [1:0] amt;
        begin cum = 0;
            for (a = 0; a < NC; a = a + 1) begin
                xv = 2 * cum + hh[a]; cum = cum + hh[a];
                xsh = (dut.CAL_LOG2 >= 13) ? (xv >> (dut.CAL_LOG2 - 13)) : (xv << (13 - dut.CAL_LOG2));
                x14 = (xsh > 16383) ? 16383 : xsh;
                neg = 14'd0 - x14[13:0]; amt = 2'd1 + ((x14 != 0) ? 2'd1 : 2'd0);
                lt[a] = {amt, neg};
            end
            chk(cum == (1 << dut.CAL_LOG2), "kalibrace: soucet histogramu = 2^CAL_LOG2");
        end endtask
    task write_lut(input integer ch);
        integer a, n, k3, blk, e; reg [8:0] ad9;
        begin build(8'h01, 8'h06, 8'h00, 0, 0); xfer(1024);          // zacatek zapisu: LUT_CNT = LUT_SUM = 0
            lsum = 0; lcnt = 0;
            for (a = 0; a < NC; a = a + 252) begin
                build(8'h0A, 0, 0, 0, 0);
                for (k3 = 0; k3 < 252 && a + k3 < NC; k3 = k3 + 1) begin
                    blk = 1 + k3 / 84; e = blk * 256 + 1 + 3 * (k3 % 84);
                    ad9 = a + k3;                                     // (SV: bez explicitni sirky se vyraz rozsiri na 32 b)
                    mo[e]     = ad9[7:0];
                    mo[e + 1] = {ch[0], ad9[8], lt[a + k3][13:8]};
                    mo[e + 2] = lt[a + k3][7:0];
                    mo[blk * 256] = mo[blk * 256] + 1;
                    lsum = lsum + ({1'b0, ch[0], 9'(a + k3), 5'd0} ^ {2'd0, lt[a + k3][13:0]});
                    lcnt = lcnt + 1;
                end
                for (blk = 1; blk < 4; blk = blk + 1) seal(blk);
                xfer(1024);
            end
            build_ack(nxt); xfer(1024); build_ack(nxt); xfer(1024);
            $display("kalibrace %0d: LUT_CNT %0d (ocek %0d), LUT_SUM %04x (ocek %04x)", ch, rd(60, 2), lcnt, rd(62, 2), lsum);
            chk(rd(60, 2) == lcnt && rd(62, 2) == lsum, "kalibrace: LUT_CNT/LUT_SUM");
            chk(rd(122, 2) == (ch ? lcnt : 0), "kalibrace: LUT_CNT_B (bit kanalu polozek)");
        end endtask
    task check_lut(input integer ch, input integer bank);
        integer a, bad;
        begin bad = 0;
            for (a = 0; a < NC; a = a + 1)
                if ((ch ? dut.u_tdcb.lut[bank * NC + a] : dut.u_tdca.lut[bank * NC + a]) !== lt[a]) bad = bad + 1;
            chk(bad == 0, "kalibrace: tabulka v aktivni bance = vypocet");
            if (bad) $display("  kanal %0d banka %0d: %0d spatnych polozek", ch, bank, bad);
        end endtask
    integer nv0, nv1;

    integer f, k, t;
    reg [31:0] e0;
    initial begin
        if (`TB_MODE == 2) begin
            // ---- vyrobni tok: A sebere histogram a ceka; STM ho precte, zapise, potvrdi ----
            for (k = 0; k < 2; k = k + 1) begin
                wait (k ? dut.cal_wait_b : dut.cal_wait_a);
                build_ack(nxt); xfer(1024); build_ack(nxt); xfer(1024);
                chk(mi[51][k] == 1'b1, "kalibrace: CAL_WAIT v hlavicce");
                chk((k ? dut.cal_valid_b : dut.cal_valid_a) == 0, "kalibrace: pred zapisem neplatna");
                read_hist(k); calc_lut; write_lut(k);
                build(8'h01, 8'h06, 8'h01, 8'd1 << k, 0); xfer(1024);  // potvrzeni JEN kanalu k -> prepnuti banky na hranici segmentu
                if (k == 0) chk(dut.cal_valid_b == 0, "kalibrace: potvrzeni A nepotvrdi cekajici B");
                #(64'd300_000_000);
                chk((k ? dut.cal_valid_b : dut.cal_valid_a) == 1 && (k ? dut.bank_b : dut.bank_a) == 1, "kalibrace: platna, banka 1");
                check_lut(k, 1);
                t0have[k] = 0;                                         // nova tabulka = nova osa TDC
            end
            $display("vyrobni kalibrace obou kanalu hotova v %0t ps", $time);
            build(8'h01, 8'h05, 8'h00, 0, 0); xfer(1024);              // bez maskovani pri CS (segment 250 us < transakce)
            #(64'd600_000_000);
            build_ack(nxt); xfer(1024); nxt = unw(rd(32, 2), nxt);
            for (f = 0; f < 4; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            chk(nvalid_a >= 4 && nvalid_b >= 4, "po kalibraci platne zaznamy obou kanalu");
            // ---- rekalibrace JEN A: B meri dal, A po sberu meri se starou bankou ----
            nv0 = nvalid_a; nv1 = nvalid_b; ninv_a = 0;
            build(8'h01, 8'h02, 8'h03, 0, 0); xfer(1024);              // p1: bit0 start, bity 2:1 maska = A
            wait (dut.cal_wait_a);
            chk(dut.cal_valid_a == 1 && dut.bank_a == 1, "rekalibrace: A ceka se starou platnou bankou");
            chk(!dut.cal_busy_b && dut.cal_valid_b, "rekalibrace: B se nekalibruje");
            for (f = 0; f < 4; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            chk(nvalid_b > nv1 + 4, "rekalibrace: B meri porad");
            chk(nvalid_a > nv0 + 2, "rekalibrace: A po sberu meri se starou bankou");
            chk(ninv_a > 0, "rekalibrace: A behem sberu histogramu neplatny");
            read_hist(0); calc_lut; write_lut(0);
            build(8'h01, 8'h06, 8'h01, 8'd1, 0); xfer(1024);
            #(64'd300_000_000);
            chk(dut.bank_a == 0 && dut.cal_valid_a, "rekalibrace: nova tabulka v bance 0");
            check_lut(0, 0);
            t0have[0] = 0;
            for (f = 0; f < 3; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            $display("ramcu %0d, zaznamu %0d (platnych A %0d B %0d), duplicit %0d, kontrol %0d, max|e| 2pt A %.2e B %.2e",
                     nfr, nrec_tot, nvalid_a, nvalid_b, dup, nchk, maxe[0], maxe[1]);
            $display("T0 proti skutecne hrane: max odchylka A %.0f ps, B %.0f ps", t0max[0], t0max[1]);
            if (errors == 0) $display("PASS: tb_top_v3 (rezim 2)"); else $display("FAIL: tb_top_v3 (%0d chyb)", errors);
            $finish;
        end
        // nabeh + kalibrace obou kanalu (strida se: A, pak B) + prepnuti banky na hranici segmentu
        wait (dut.cal_valid_a && dut.cal_valid_b);
        $display("kalibrace hotova v %0t ps (banka A %0d, B %0d)", $time, dut.bank_a, dut.bank_b);
        chk(dut.bank_a == 1 && dut.bank_b == 1, "aktivni banka po prvni kalibraci = 1");
        #(64'd300_000_000);
        if (`TB_MODE == 3) begin
            // ---- sirka pulzu (FW 0x0501): B = pin A invertovany (SET_CONFIG 0x09 = 3) ----
            build(8'h01, 8'h05, 8'h00, 0, 0); xfer(1024);              // bez maskovani pri CS (segment 250 us < transakce)
            // audit 2026-10-23 F-0269: P_B = 4 na pinu B; B z pinu A jde mimo predelicku -> zaznam B musi nest P = 1
            build(8'h01, 8'h07, 8'h01, 8'd2, 0); xfer(1024);
            build(8'h01, 8'h09, 8'h03, 0, 0); xfer(1024); bfa = 1; binv = 1;
            #(64'd600_000_000);                                          // B 2 segmenty neplatny po zmene zdroje
            build_ack(0); xfer(1024); nxt = unw(rd(32, 2), nxt); haveA = 0; t0have[1] = 0; have[1] = 0;   // novy zdroj B = nova reference
            chk(mi[66][4:3] == 2'b11, "CFG echo: B z pinu A, invertovany");
            for (f = 0; f < 6; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            $display("sirka pulzu %.0f ps (perioda %.0f ps): %0d dvojic A/B, max |chyba| %.0f ps", WA, PA, nw, wmax);
            chk(nw >= 6, "sirka pulzu: dvojice A/B");
            // ---- zpozdeni mezi kanaly: B = pin A neinvertovany -> stejna hrana v obou retezech ----
            nw = 0; wmax = 0.0;
            build(8'h01, 8'h09, 8'h01, 0, 0); xfer(1024); binv = 0;
            #(64'd600_000_000);
            build_ack(nxt); xfer(1024); nxt = unw(rd(32, 2), nxt); haveA = 0; t0have[1] = 0; have[1] = 0;   // novy zdroj B = nova reference
            chk(mi[66][4:3] == 2'b01, "CFG echo: B z pinu A");
            for (f = 0; f < 6; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            $display("zpozdeni A/B (stejna hrana): %0d dvojic, max |rozdil| %.0f ps", nw, wmax);
            chk(nw >= 6, "zpozdeni A/B: dvojice");
            // ---- zpet na pin B: B meri zase svuj signal ----
            build(8'h01, 8'h09, 8'h00, 0, 0); xfer(1024); bfa = 0;
            #(64'd600_000_000);
            build_ack(nxt); xfer(1024); nxt = unw(rd(32, 2), nxt); t0have[1] = 0; have[1] = 0;
            e0 = nvalid_b;
            for (f = 0; f < 4; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            chk(nvalid_b > e0 + 4, "zpet na pin B: platne zaznamy B");
            chk(mi[66][4:3] == 2'b00, "CFG echo: B z pinu B");
        end else if (`TB_MODE == 4) begin
            // ---- FW 0x0503 predelicka: B ~143,3 MHz (primo pres TDC nejde), P_B = 8 a pak 16; sonda, PRESC, FLAGS 7:5 ----
            build(8'h01, 8'h05, 8'h00, 0, 0); xfer(1024);              // bez maskovani pri CS
            build(8'h01, 8'h07, 8'h01, 8'd3, 0); xfer(1024);           // B: log2 P = 3
            #(64'd1_000_000_000);                                        // 4 segmenty: prepnuti + 2 neplatne
            build_ack(0); xfer(1024); nxt = unw(rd(32, 2), nxt); have[1] = 0; t0have[1] = 0;
            chk(mi[64] == 8'd0 && mi[65] == 8'd3, "PRESC A 0, B 3 (plati, nic neceka)");
            pr_e = `TB_FB / 16.0e3;
            chk(real'(rd(110, 2)) > pr_e - 2.0 && real'(rd(110, 2)) < pr_e + 2.0, "PROBE B = f/16 za 1 ms");
            pr_e = `TB_FA / 16.0e3;
            chk(real'(rd(108, 2)) > pr_e - 2.0 && real'(rd(108, 2)) < pr_e + 2.0, "PROBE A = f/16 za 1 ms");
            $display("predelicka: PROBE A %0d B %0d (cekano %.1f / %.1f), PRESC %02h %02h", rd(108, 2), rd(110, 2),
                     `TB_FA / 16.0e3, `TB_FB / 16.0e3, mi[64], mi[65]);
            for (f = 0; f < 6; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            chk(nvp[1][3] >= 6, "B P = 8: platne zaznamy");
            build(8'h01, 8'h07, 8'h01, 8'd4, 0); xfer(1024);           // B: log2 P = 4
            e0 = ninv_b;
            for (f = 0; f < 8; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            chk(mi[65] == 8'd4, "PRESC B 4 po hranici segmentu");
            chk(nvp[1][4] >= 4, "B P = 16: platne zaznamy");
            chk(ninv_b >= e0 + 2, "B: neplatne segmenty kolem prepnuti P");
            chk(nvp[0][0] >= 10 && nvp[0][3] == 0 && nvp[0][4] == 0, "A: P = 1 po celou dobu");
            $display("predelicka: zaznamy B P=8 %0d, P=16 %0d, neplatne B %0d, A P=1 %0d", nvp[1][3], nvp[1][4], ninv_b, nvp[0][0]);
        end else if (`TB_MODE == 5) begin
            // ---- FW 0x0505 PSRAM: kalibrace latence zapisu, hluboky zaznam A (1100 znacek), okna 0..2 pres stranky ----
            build(8'h01, 8'h05, 8'h00, 0, 0); xfer(1024);
            build_ack(0); xfer(1024);
            chk(mi[128][0] && !mi[128][1], "PSRAM: pripravena, kalibrace zapisu prosla");
            chk(mi[129][4:0] == 5'd16, "PSRAM: nalezena latence zapisu modelu (W0 + 16 = 20)");
`ifdef TB_PS_LATE
            chk(mi[129][5], "PSRAM: faze cteni 1 (vystup modelu neplatny 4..6 ns po hrane CK)");
`else
            chk(!mi[129][5], "PSRAM: faze cteni 0 (nabezna hrana staci)");
`endif
            // audit 2026-10-23 F-0266: bajt 127 = pocet prijatych prikazu T_RAW (Gray) + kanal; vyluka rezimu, STOP
            chk(mi[127] == 8'h00, "RAW: zatim zadny prikaz (bajt 127)");
            build(8'h01, 8'h05, 8'h01, 0, 0); xfer(1024);              // F-0270: maska pri CS -> v zaznamu mezery
            build(8'h0B, 8'h02, 8'hA0, 8'h86, 8'h01); xfer(1024);   // rezim 1, kanal A, 100000 znacek (10 ms)
            build_ack(0); xfer(1024); build_ack(0); xfer(1024);
            chk(mi[127] == {4'd0, gr(4'd1)} && mi[128][2], "RAW: hluboky start prijat, kanal A, bezi");
            build(8'h0B, 8'h01, 8'd0, 8'd0, 8'd0); xfer(1024);      // rezim 0, kanal B: behem zaznamu odmitnout
            build(8'h0B, 8'h04, 8'd0, 8'd0, 8'd0); xfer(1024);      // rezim 2: behem zaznamu odmitnout
            build_ack(0); xfer(1024); build_ack(0); xfer(1024);
            chk(mi[127] == {4'd0, gr(4'd1)} && mi[128][2] && !mi[128][4], "RAW: rezim 0 a 2 behem hlubokeho zaznamu odmitnuty");
            build(8'h0B, 8'h02, 8'd0, 8'd0, 8'd0); xfer(1024);      // rezim 1 pri bezicim zaznamu = STOP
            build_ack(0); xfer(1024); build_ack(0); xfer(1024);
            $display("PSRAM: STOP po %0d znackach, stav %02h, bajt 127 %02h", rd(124, 3), mi[128], mi[127]);
            chk(mi[127] == {4'd0, gr(4'd2)} && !mi[128][2] && rd(124, 3) > 0 && rd(124, 3) < 100000,
                "RAW: STOP prijat, zaznam skoncil pred N");
            chk(mi[128][6], "RAW: maska pri CS behem zaznamu -> priznak mezer");
            build(8'h01, 8'h05, 8'h00, 0, 0); xfer(1024);
            build(8'h0B, 8'h02, 8'd76, 8'd4, 8'd0); xfer(1024);     // rezim 1, kanal A, 1100 znacek
            #(64'd300_000_000);
            build_ack(0); xfer(1024);
            chk(mi[127] == {4'd0, gr(4'd3)} && !mi[128][6], "RAW: druhy hluboky start prijat, bez masky zadne mezery");
            $display("PSRAM: hluboky zaznam %0d znacek, stav %02h, latence W0+%0d", rd(124, 3), mi[128], mi[129]);
            chk(rd(124, 3) == 1100 && !mi[128][2] && !mi[128][3], "PSRAM: 1100 znacek, zaznam skoncil, bez preteceni");
            ndeep = 0; dmaxd = 0.0;
            for (w = 0; w < 3; w = w + 1) begin
                build(8'h0B, 8'h04, w[7:0], 8'd0, 8'd0); xfer(1024);   // rezim 2: okno w
                // ramec teto transakce se slozil jeste pred nactenim okna (FPGA sklada ramec po predchozi transakci)
                build_ack(0); xfer(1024); build_ack(0); xfer(1024);
                chk(mi[128][5] && !mi[128][4] && rd(130, 2) == w, "PSRAM: okno nacteno");
                chk(mi[127] == {4'd0, gr(4 + w[3:0])}, "RAW: nacteni okna prijato (bajt 127)");
                nwin = (w < 2) ? 512 : 76;
                build(8'hA0, 8'd0, 8'd0, 8'd0, 0); xfer(1024); build_ack(0); xfer(1024);
                chk(rd(256 + 2 + 3 * 5, 3) == nwin, "PSRAM: souhrnna stranka, pocet znacek okna");
                for (t = 0; t * 252 < 2 * nwin; t = t + 1) begin
                    build(8'hA0, 8'd3, t[7:0], 8'd0, 0); xfer(1024); build_ack(0); xfer(1024);
                    for (k = 0; k < 252; k = k + 1) if (t * 252 + k < 1024) rawi[t * 252 + k] = rd((1 + k / 84) * 256 + 2 + 3 * (k % 84), 3);
                end
                for (k = 0; k < nwin; k = k + 1) begin
                    rm1 = {rawi[2 * k + 1], rawi[2 * k]};
                    if (ndeep > 0) begin
                        rawe = real'((rm1 - rm0) & 48'hFFFFFFFFFFFF) * U - PA;
                        if (rawe < 0) rawe = -rawe;
                        if (rawe > dmaxd) dmaxd = rawe;
                        chk(rawe < 300.0, "PSRAM: rozdil po sobe jdoucich znacek = perioda A (i pres hranici okna)");
                    end
                    rm0 = rm1; ndeep = ndeep + 1;
                end
            end
            $display("PSRAM: precteno %0d znacek ve 3 oknech, max |rozdil - P| %.0f ps; model zapsal %0d B, precetl %0d B",
                     ndeep, dmaxd, u_ps0.nwr, u_ps0.nrd);
            chk(ndeep == 1100, "PSRAM: vsech 1100 znacek z oken");
            // audit 2026-10-23 F-0268: PSRAM pri cteni neodpovi -> okno skonci s chybou (bit7), dalsi okno jde nacist
            u_ps0.dead = 1'b1;
            build(8'h0B, 8'h04, 8'd1, 8'd0, 8'd0); xfer(1024); build_ack(0); xfer(1024); build_ack(0); xfer(1024);
            chk(mi[128][5] && mi[128][7] && !mi[128][4] && mi[127] == {4'd0, gr(4'd7)}, "PSRAM: okno bez odpovedi skonci s chybou");
            u_ps0.dead = 1'b0;
            build(8'h0B, 8'h04, 8'd1, 8'd0, 8'd0); xfer(1024); build_ack(0); xfer(1024); build_ack(0); xfer(1024);
            chk(mi[128][5] && !mi[128][7] && !mi[128][4] && rd(130, 2) == 1 && mi[127] == {4'd0, gr(4'd8)},
                "PSRAM: po chybe dalsi okno nacteno");
        end else if (`TB_MODE == 1) begin
            // ---- preteceni kruhu: 16 zaznamu, STM 20 segmentu necte ----
            build_ack(0); xfer(1024); build_ack(0); xfer(1024); nxt = unw(rd(36, 2), 0);   // kruh 16 pretekl uz pri nabehu
            build_ack(nxt); xfer(1024); check_frame(1);
            build_ack(nxt); xfer(1024); check_frame(1);
            e0 = nxt;
            #(64'd5_000_000_000);                                          // 20 segmentu = 40 zaznamu > 16
            build_ack(nxt); xfer(1024);
            chk(mi[3][1] == 1'b1, "FLAGS bit1 preteceni");
            chk(unw(rd(36, 2), e0) > e0, "REC_FIRST skocil za ACK");
            $display("preteceni: ocekavano %0d, REC_FIRST %0d, REC_HEAD %0d, FLAGS %02h", nxt, rd(36, 2), rd(32, 2), mi[3]);
            nxt = unw(rd(36, 2), nxt); have[0] = 0; have[1] = 0;         // STM: prerusi souvisly usek
            check_frame(1);
            build_ack(nxt); xfer(1024); check_frame(1);
            build_ack(nxt); xfer(1024); check_frame(1);
            chk(mi[3][1] == 1'b0, "FLAGS bit1 po ACK zhasne");
            for (f = 0; f < 6; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            chk(nvalid_a >= 4 && nvalid_b >= 4, "po preteceni zase platne zaznamy");
        end else begin
            // ---- mereni: TB_NFR ramcu ----
            // SIM: segment 250 us je kratsi nez transakce (328 us) -> s maskovanim pri CS by kazdy segment prisel o znacky;
            // maskovani se overuje zvlast nize (FLAGS bit3 / mensi N, kmitocet dal spravne)
            build(8'h01, 8'h05, 8'h00, 0, 0); xfer(1024);
            #(64'd600_000_000);                                          // 2 segmenty jeste s maskovanim
            build_ack(0); xfer(1024);
            nxt = unw(rd(32, 2), nxt);                                         // zaznamy do teto chvile se neposuzuji
            for (f = 0; f < `TB_NFR; f = f + 1) begin
                build_ack(nxt); xfer(1024); check_frame(1);
                $display("ramec %0d: SEQ %0d REC_FIRST %0d NREC %0d REC_HEAD %0d FLAGS %02h | prevzato %0d, duplicit %0d",
                         f, rd(4, 2), rd(36, 2), mi[40], rd(32, 2), mi[3], nxt, dup);
            end
            chk(nvalid_a >= 8 && nvalid_b >= 8, "pocet platnych zaznamu");
            chk(nrec_tot >= 3 * `TB_NFR, "prijem postupuje (REC_ACK se posouva)");
            chk(dup > 0, "prekryv ramcu (ocekavany podle kap. 6.1)");
            $display("znacka v taktu clear (pokryti hranice segmentu): A %0d, B %0d", ncl_a, ncl_b);
            chk(ncl_a + ncl_b > 0, "pokryti: znacka v taktu clear");
            // ---- RX_LEN_ERR: kratsi a delsi transakce se zahodi ----
            e0 = rd(54, 2);
            build(8'h01, 8'h05, 8'h03, 0, 0); xfer(1023);                 // SET_CONFIG diag (led) -- nesmi se provest
            build(8'h01, 8'h05, 8'h03, 0, 0); xfer(1025);
            build_ack(nxt); xfer(1024); build_ack(nxt); xfer(1024); check_frame(1);
            chk(rd(54, 2) == e0 + 2, "RX_LEN_ERR +2");
            chk(mi[66][1] == 1'b0, "zkracena/delsi transakce se neprovedla (CFG echo)");
            // ---- RX_CRC_ERR: vadny blok 0 ----
            e0 = rd(52, 2);
            build(8'h01, 8'h05, 8'h03, 0, 0); mo[100] = mo[100] ^ 8'h01; xfer(1024);
            build_ack(nxt); xfer(1024); build_ack(nxt); xfer(1024); check_frame(1);
            chk(rd(52, 2) == e0 + 1 && mi[66][1] == 1'b0, "RX_CRC_ERR +1, prikaz neproveden");
            // ---- SET_CONFIG 0x05 (echo) a 0x08 (SEG_LEN 10 ms) ----
            build(8'h01, 8'h05, 8'h03, 0, 0); xfer(1024);
            build(8'h01, 8'h08, 8'd10, 0, 0); xfer(1024);
            build_ack(nxt); xfer(1024); check_frame(1);
            build_ack(nxt); xfer(1024); check_frame(1);
            chk(mi[66][1:0] == 2'b11, "CFG echo mask_cs + led");
            chk(mi[23] == 8'd10, "SEG_LEN_MS = 10");
            build(8'h01, 8'h08, 8'd25, 0, 0); xfer(1024);
            // ---- stranka histogramu A (druh 1), stranka 1 a 2 ----
            for (t = 0; t < 3; t = t + 1) begin
                build(8'hA0, 8'd1, t[7:0], 8'd0, 0); xfer(1024);
                build_ack(nxt); xfer(1024);
                chk(mi[2] == 8'hA0 && mi[116] == 8'd1 && rd(117, 2) == t, "stranka: TYPE/druh/cislo");
                chk(rd(119, 2) == ((t < 2) ? 252 : 8), "stranka: pocet polozek");
                for (k = 0; k < 252; k = k + 1) begin
                    i = t * 252 + k;
                    j = (1 + k / 84) * 256 + 2 + 3 * (k % 84);
                    if (i < 512) chk(rd(j, 3) == dut.u_tdca.hist[i], "stranka: polozka = hist A");
                    else chk(rd(j, 3) == 0, "stranka: za koncem 0");
                end
                for (k = 1; k < 4; k = k + 1) chk(mi[k * 256] == k && mi[k * 256 + 1] == rd(4, 1), "stranka: znacka bloku");
                build_ack(nxt); xfer(1024);                             // dalsi ramec je zase DATA
                chk(mi[2] == 8'h80, "po strance zase DATA");
            end
            chk(npps >= 3, "1PPS: aspon 3 hrany v ramcich");
            $display("1PPS jemny cas: %0d hran, max |chyba - prvni| %.0f ps, polohy IDES8 %b, d %0d..%0d taktu, epocha %0d",
                     npf, pf_max, pf_pos, dmin, dmax, pf_ep);
            chk(npf >= 3, "1PPS: jemny cas aspon 3x");
            chk((pf_pos[0] + pf_pos[1] + pf_pos[2] + pf_pos[3] + pf_pos[4] + pf_pos[5] + pf_pos[6] + pf_pos[7]) >= 4,
                "1PPS: pokryti aspon 4 poloh IDES8");
            chk(dmax - dmin <= 1, "1PPS: jemny - hruby v okne 1 taktu");
            chk(dmin >= -8 && dmax <= 8, "1PPS: jemny cas na ose tick (|d| <= 8 taktu)");
            // ---- maskovani pri CS (SET_CONFIG 0x05 bit0): segmenty pres transakci maji mene znacek, kmitocet spravne ----
            build(8'h01, 8'h05, 8'h01, 0, 0); xfer(1024);
            #(64'd600_000_000);
            e0 = nvalid_a; nmask = 0;
            for (f = 0; f < 4; f = f + 1) begin build_ack(nxt); xfer(1024); check_frame(1); end
            chk(nmask > 0, "maskovani: segment s mene znackami");
            chk(nvalid_a > e0, "maskovani: platne zaznamy dal");
            // ---- RAW_CAPTURE kanalu A: 512 po sobe jdoucich znacek (zachyceni probehne v mezere 200 us po zadosti,
            //      512 x perioda), souhrnna stranka polozka 5 = pocet, stranky druhu 3 (dve polozky u24 na znacku) ----
            build(8'h0B, 8'd0, 0, 0, 0); xfer(1024);
            build(8'hA0, 8'd0, 8'd0, 8'd0, 0); xfer(1024); build_ack(nxt); xfer(1024);
            chk(mi[2] == 8'hA0 && mi[116] == 8'd0 && rd(256 + 2 + 3 * 5, 3) == 512, "RAW: souhrnna stranka, polozka 5 = 512 znacek");
            for (t = 0; t < 5; t = t + 1) begin
                build(8'hA0, 8'd3, t[7:0], 8'd0, 0); xfer(1024); build_ack(nxt); xfer(1024);
                chk(mi[2] == 8'hA0 && mi[116] == 8'd3 && rd(117, 2) == t, "RAW: stranka druhu 3");
                chk(rd(119, 2) == ((t < 4) ? 252 : 16), "RAW: pocet polozek stranky");
                for (k = 0; k < 252; k = k + 1) if (t * 252 + k < 1024) rawi[t * 252 + k] = rd((1 + k / 84) * 256 + 2 + 3 * (k % 84), 3);
            end
            nraw1 = 0; rawe = 0.0;
            for (k = 0; k < 511; k = k + 1) begin
                rm0 = {rawi[2 * k + 1], rawi[2 * k]}; rm1 = {rawi[2 * k + 3], rawi[2 * k + 2]};
                dps = real'((rm1 - rm0) & 48'hFFFFFFFFFFFF) * U;
                kk = $rtoi(dps / PA + 0.5);
                werr = dps - kk * PA; if (werr < 0) werr = -werr;
                if (werr > rawe) rawe = werr;
                if (kk == 1) nraw1 = nraw1 + 1;
                chk(kk >= 1 && werr < 300.0, "RAW: rozdil znacek = nasobek periody");
            end
            $display("RAW_CAPTURE: 512 znacek, po sobe jdoucich %0d/511, max |rozdil - k*P| %.0f ps", nraw1, rawe);
            chk(nraw1 >= 500, "RAW: znacky po sobe jdoucich hran");
        end
        $display("ramcu %0d, zaznamu %0d (platnych A %0d B %0d), duplicit %0d, kontrol %0d, max|e| 2pt A %.2e B %.2e, sklon A %.2e B %.2e, 1PPS %0d",
                 nfr, nrec_tot, nvalid_a, nvalid_b, dup, nchk, maxe[0], maxe[1], maxsl[0], maxsl[1], npps);
        $display("T0 proti skutecne hrane: max odchylka A %.0f ps, B %.0f ps; preteceni s priznakem %0d", t0max[0], t0max[1], novf);
        if (errors == 0) $display("PASS: tb_top_v3 (rezim %0d)", `TB_MODE); else $display("FAIL: tb_top_v3 (%0d chyb)", errors);
        $finish;
    end
    initial begin #(64'd80_000_000_000); $display("FAIL: timeout (cal_valid %0d/%0d)", dut.cal_valid_a, dut.cal_valid_b); $finish; end
endmodule

// ------------------------------------------------------------
// hb_psram_model: behavioralni model jednoho cipu vestavene PSRAM (HyperBus x8) pro tb_top_v3 (FW 0x0505). Obecna
//   specifikace HyperBus: CA 6 bajtu na hranach CK (MSB prvni; [47] cteni, [44:16] + [2:0] = adresa 16b slova),
//   zapis od hrany 6 + WLAT (RWDS = maska, 0 = zapsat), cteni od hrany 6 + RLAT, RWDS se s kazdym bajtem preklopi
//   (prvni bajt RWDS = 1), vystup TCKD po hrane CK. Pamet 64 kB (adresa se maskuje). PREDPOKLAD, ne datasheet.
//   Audit 2026-10-23 (F-0262): od CS# = 0 ridi model RWDS = RW_CA (priznak latence; pevna dvojita latence = 1) po celou
//   CA; pri zapisu ho po CA pusti, pri cteni ho drzi pres latenci a do 0 ho stahne az bajt pred prvnimi daty.
//   F-0264: TCKD_MIN < TCKD = vystup (DQ i RWDS) je od TCKD_MIN do TCKD po hrane CK neplatny (X), jinak se meni skokem.
//   PSTALL: po 64. bajtu cteni PSRAM PSTALL hran nic nevyda a RWDS se neprepina (nejhorsi pripustne chovani: data plati
//   jen s prepnutim RWDS) -- bez toho by radic, ktery po prvnim bajtu bere kazdy takt, prosel (bajt za takt).
// ------------------------------------------------------------
module hb_psram_model #(parameter integer WLAT = 20, parameter integer RLAT = 20, parameter integer TCKD = 3000,
                        parameter integer TCKD_MIN = 3000, parameter RW_CA = 1'b1, parameter integer PSTALL = 2) (
    input wire ck, input wire ck_n, input wire cs_n, input wire rst_n, inout wire [7:0] dq, inout wire rwds
);
    reg [7:0]  mem [0:65535];
    reg [7:0]  dq_o = 8'd0;
    reg        dq_oe = 1'b0, rw_o = 1'b0, rw_oe = 1'b0;
    assign dq   = dq_oe ? dq_o : 8'hzz;
    assign rwds = rw_oe ? rw_o : 1'bz;
    integer    e = 0, k, kb, kd;
    reg        chg;
    reg [47:0] ca = 48'd0;
    reg        rd = 1'b0;
    reg [21:0] a = 22'd0;
    integer    nwr = 0, nrd = 0;
    reg        dead = 1'b0;                              // 1 = pri cteni neodpovida (F-0268)
    initial for (k = 0; k < 65536; k = k + 1) mem[k] = 8'h00;
    always @(negedge cs_n) begin e = 0; rw_o <= #(TCKD) RW_CA; rw_oe <= #(TCKD) 1'b1; end
    always @(posedge cs_n) begin dq_oe <= #(TCKD) 1'b0; rw_oe <= #(TCKD) 1'b0; end
    always @(ck) if (!cs_n && rst_n && (ck === 1'b0 || ck === 1'b1)) begin
        if (e < 6) begin
            ca = {ca[39:0], dq};
            if (e == 5) begin
                rd = ca[47];
                a  = {ca[37:16], ca[2:0], 1'b0};            // bajtova adresa (slovo x 2), maskuje se na 64 kB
                if (!rd) rw_oe <= #(TCKD) 1'b0;           // zapis: RWDS dal ridi radic (maska)
            end
        end else if (rd) begin
            kb  = e - 6 - RLAT;                               // index hrany dat (< 0 = latence)
            kd  = (kb >= 64 + PSTALL) ? kb - PSTALL : kb;     // index bajtu po pauze
            chg = !dead && (kb == -1 || (kb >= 0 && (kb < 64 || kb >= 64 + PSTALL)));
            if (chg && TCKD_MIN < TCKD) begin dq_o <= #(TCKD_MIN) 8'hxx; rw_o <= #(TCKD_MIN) 1'bx; end
            if (chg && kb == -1) rw_o <= #(TCKD) 1'b0;        // bajt pred daty RWDS = 0 (konec priznaku latence)
            if (chg && kb >= 0) begin
                dq_o <= #(TCKD) mem[a[15:0]]; dq_oe <= #(TCKD) 1'b1; rw_o <= #(TCKD) ((kd % 2) == 0);
                a = a + 22'd1; nrd = nrd + 1;
            end
        end else if (e >= 6 + WLAT) begin
            if (rwds === 1'b0) begin mem[a[15:0]] = dq; nwr = nwr + 1; end
            a = a + 22'd1;
        end
        e = e + 1;
    end
endmodule
