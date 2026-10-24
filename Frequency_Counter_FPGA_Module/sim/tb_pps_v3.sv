// ============================================================
// tb_pps_v3.sv -- samostatny test retezu GPS 1PPS v3 (FW 0x0506): pps_ides3 (rPLL 100 -> 400 MHz, CLKDIV /4, IDES8,
// skladani f_ts ze ziveho ticku s vypujckou bw v krocich fix 1..3) + pps_cap3 (hruba hrana, zverejneni najednou),
// zapojene jako v top_v3.v (tick = 34b citac 100 MHz, pin = gps_1pps, Q0_FIRST vychozi). Doplnek k tb_top_v3.sv: tam
// tick zacina od 0 a za beh neprekroci zadnou hranici prenosu v nevhodne fazi (audit 2026-10-23).
//   * TB budi tick SAM a mezi pulzy ho posouva skokem HORNI casti tick[33:8]; dolnich 8 b bezi dal (PCLK pocita takty
//     s pevnym posunem dolniho bajtu proti ticku = t_rel, skok o nasobek 256 = jiny pocatecni stav citace; na desce
//     tick neskace, stejny stav nastane za 2^8 .. 2^34 taktu). Hrana se tak dostane na kazdou hranici prenosu hned.
//   * Referencni model: hrana v case t_e [ps] -> u = tick(t_e) x 16384 + zlomek taktu x 16384 (osa ticku v jednotkach
//     T/16384 = 0,6103515625 ps). PTS = u + K; K (latence uvolneni CLKDIV + faze PLL SIM_PLL_PS) se urci z prvni hrany
//     epochy (hrana uprostred mezi vzorky IDES8); kazda dalsi hrana tez epochy |PTS - u - K| <= 2048 (1 krok IDES8 =
//     1,25 ns), zvlast rozdil po sobe jdoucich hran. K v -2..+3 taktu a d = jemny - hruby v +-8 taktu, v epose v okne
//     1 taktu (jako tb_top_v3): chyti konstantni chyby osy (napr. chybejici t_rel), ktere by se v K schovaly.
//   * Hranice prenosu: a) tick[7:0] FF->00, b) tick[20:0] same jednicky -> prenos do bitu 21 (f_ts[35]), c) tick[33:0]
//     same jednicky -> preteceni 34 b (mod 2^48), d) hrana o 1..8 taktu driv (bw = 1 i 0). Kazdy scenar: hrana v taktu
//     -9..+1 vuci prechodu x 11 fazi (8 poloh IDES8 uprostred mezi vzorky + 1 ps pred / presne na / 1 ps za vzorkem Q7
//     = hranice slova PCLK, lastw). Prijem (f_ts <= tick) je v modelu vzdy 3 takty za hl -> bw = 1 pro takt -3..-1.
//   * pps_cap3: seq, seq_nz, seq_gt1, pts_p/pfl_p/d_p, PFLAGS, saturace d_l (force dd_r), nofine (blokovany f_tgl).
//   * rozhrani pps_ides3: f_ts se po preklopeni f_tgl nezmeni az do dalsi hrany (na tom stoji pps_cap3 i vyjimky SDC).
//   * ztrata zamku (force u_pll.RESET): hruby cas {c_tick, 14'b0}, epocha +1, nova kotva; ztrata uprostred mereni;
//     znovuzamceni pri pinu 1PPS = 1. Pruzkum (bez PASS): hrana tesne pred znovuzamcenim, kratky pulz 3 ns, zakmit.
// Preklad (z korene modulu): iverilog -g2012 -I sim -s tb_pps_v3 -o t.vvp sim/gowin_models.v src/v3_core.v sim/tb_pps_v3.sv
// Nalez N1 (hrana 1..22 taktu pred uvolnenim PCLK, pin drzi 1 -> jemny cas prvniho slova IDES8, falesna poloha 0) je
//   od opravy (maska wn v pps_ides3) VYCHOZI kontrola; -DTB_LAX_RELOCK ji vrati na pruzkum (pro RTL bez opravy).
// Nalez N2 (zakmit kratsi nez takt, hruby ho nevidi): vychozi kontrola jen pro zakmit >= 10 taktu pred hranou (to opravuje
//   fa <= 0 pri hrube hrane v pps_cap3); okno 5..8 taktu zustava otevrene (viz docs/audit/2026-10-24_pps_tb.md kap. 6.2).
// Volby: -DTB_LAX_RELOCK     N1 jen pruzkum (bez kontroly)
//        -DTB_STRICT_GLITCH  N2 jako kontrola pro vsechny vzdalenosti zakmitu -- SELZE i s opravou (okno 5..8 taktu)
// ============================================================
`timescale 1ps/1ps
`ifndef TB_TICK0
`define TB_TICK0 34'h2_5A3C_E0B7          // pocatecni tick: t_rel prvni epochy daleko od 0 (bez t_rel -> |d| velke)
`endif
`ifndef TB_SEED
`define TB_SEED 20261024
`endif
`ifndef TB_PW
`define TB_PW 800000                      // sirka pulzu 1PPS [ps] (na desce ~100 ms; RTL na ni nezavisi)
`endif
module tb_pps_v3;
    localparam integer T  = 10000;        // perioda clk [ps]
    localparam integer KW = 2048;         // 1 krok IDES8 (1,25 ns) v jednotkach T/16384 = tolerance
    localparam integer U1 = 16384;        // 1 takt v jednotkach T/16384
    localparam [63:0]  PW = `TB_PW;

    // ---- hodiny 100 MHz (= CLKIN rPLL) a tick (34b citac jako tick_hi v top_v3, navic skok horni casti) ----
    reg clk = 1'b0;
    initial begin #2300; forever #(T / 2) clk = ~clk; end
    reg  [33:0] tick = `TB_TICK0;
    reg         tj = 1'b0;                // v pristim taktu tick <= {tj_hi, tick[7:0]} + 1
    reg  [25:0] tj_hi = 26'd0;
    always @(posedge clk) tick <= tj ? ({tj_hi, tick[7:0]} + 34'd1) : (tick + 34'd1);

    // ---- DUT: zapojeni jako top_v3.v r. 72..77 ----
    reg pin = 1'b0;
    wire        f_tgl, f_lock;
    wire [47:0] f_ts;
    wire [3:0]  epoch;
    pps_ides3 dut_ides (.clk(clk), .pin(pin), .tick(tick), .f_tgl(f_tgl), .f_ts(f_ts), .f_lock(f_lock), .epoch(epoch));
    wire [31:0] seq;
    wire        seq_nz, seq_gt1, nofine;
    wire [33:0] last, prev;
    wire [47:0] pts_l, pts_p;
    wire [7:0]  pfl_l, pfl_p, d_l, d_p;
    pps_cap3 dut_cap (.clk(clk), .pin(pin), .tick(tick), .f_tgl(f_tgl), .f_ts(f_ts), .f_lock(f_lock),
                      .seq(seq), .seq_nz(seq_nz), .seq_gt1(seq_gt1), .last(last), .prev(prev), .pts_l(pts_l), .pts_p(pts_p),
                      .pfl_l(pfl_l), .pfl_p(pfl_p), .d_l(d_l), .d_p(d_p), .nofine(nofine));

    // ---- promenne (vse deklarovano pred prvnim pouzitim) ----
    integer    errors = 0, nchk = 0;
    reg [8*48-1:0] ctx;                   // kde jsme (do chybovych hlasek)
    reg [63:0] tclk = 0;                  // cas posledni nabezne hrany clk [ps]
    reg [63:0] tref_t = 0;                // kotva osy: v taktu po hrane tref_t plati tick = tref_v
    reg [33:0] tref_v = 0;
    reg [63:0] tfe = 0, tq7 = 0;          // posledni vzorek IDES8 (obe hrany FCLK); vzorek Q7 posledniho slova PCLK
    integer    q7ph = 700;                // faze vzorku Q7 (hranice slova) za nabeznou hranou clk [ps] -- meri se
    reg [63:0] tx = 0;                    // hrana clk, po ktere tick[7:0] = 0x00 (prechod scenare)
    reg [63:0] e_t = 0;                   // posledni hrana: cas, tick, u; latence zverejneni [takty], zverejneno, f_lock
    reg [33:0] e_tk = 0;
    reg [47:0] e_u = 0;
    integer    e_lat = 0;
    reg        e_pub = 1'b0, e_flk = 1'b0;
    reg [31:0] q_seq = 0;                 // zverejnene hodnoty minule hrany (pro pts_p / pfl_p / d_p)
    reg [47:0] q_pts = 0;
    reg [7:0]  q_pfl = 0, q_d = 0;
    reg        k_have = 1'b0, kp_ok = 1'b0, k_last_ok = 1'b0;
    reg [47:0] k0 = 0, kp_u = 0, kp_p = 0, k_last = 0;
    integer    dmin_e = 127, dmax_e = -128, max_dk = 0, max_dd = 0;
    integer    k0_min = 0, k0_max = 0, k0_n = 0;
    integer    n_edge = 0, n_fine = 0, n_coarse = 0, n_sc = 0, n_epoch = 0;
    integer    cov_a = 0, cov_b = 0, cov_c = 0, cov_d0 = 0, cov_d1 = 0;
    integer    lat_f0 = 999, lat_f1 = 0, lat_c0 = 999, lat_c1 = 0;
    reg [7:0]  pcov = 8'd0;
    reg        m_bw = 1'b0;               // vypujcka a horni casti f_ts videne v kroku fix == 2 (pokryti)
    reg [12:0] m_mid = 0, m_top = 0;
    reg [3:0]  ep_q = 4'd0;
    integer    ep_chg = 0, ep_lock = 0, lat_rel = 19;
    reg        ft_q = 1'b0, fh_v = 1'b0, lk_q2 = 1'b0, flk_q = 1'b0;
    reg        fh_en = 1'b1;              // kontrola rozhrani f_ts/f_tgl zapnuta (v pruzkumu zakmitu jen pocita)
    reg        need_lk = 1'b1;            // jemny cas jen pri zamcene PLL (ne pri ztrate zamku po dokonceni mereni)
    reg [47:0] fh = 0;
    integer    n_tgl = 0, fh_viol = 0;
    integer    seed = `TB_SEED;
    integer    sc, m, ph, i, n, nd, r, nt0, ll_f = 0, ll_c = 0, sp_pub = 0, sp_fin = 0, rl_bad = 0, gl_bad = 0;
    reg [31:0] s0;
    reg [25:0] h;
    reg [63:0] te, tg, t0;
    reg [33:0] dv34;
    reg [3:0]  e0;
    reg signed [63:0] dk;
    reg [8*32-1:0] ll_map, sp_map, rl_map;

    // ---- pomocne funkce ----
    function signed [63:0] s48(input [47:0] v); s48 = {{16{v[47]}}, v}; endfunction
    function signed [63:0] s34(input [33:0] v); s34 = {{30{v[33]}}, v}; endfunction
    function [7:0] sat8(input [33:0] v);                // int8 se saturaci (nezavisle na RTL)
        begin if (s34(v) > 127) sat8 = 8'h7F; else if (s34(v) < -128) sat8 = 8'h80; else sat8 = v[7:0]; end endfunction
    function [33:0] tick_at(input [63:0] t);             // tick v okamziku t (t nesmi byt presne na hrane clk)
        tick_at = tref_v + (t - tref_t) / T; endfunction
    function [47:0] u_at(input [63:0] t);                // skutecny cas t na ose ticku [T/16384]
        reg [33:0] tk; reg [63:0] dt;
        begin dt = t - tref_t; tk = tref_v + dt / T; u_at = {tk, 14'd0} + ((dt % T) * U1) / T; end endfunction
    function on_clk(input [63:0] t);                     // t presne na hrane clk: hruby synchronizator zavodi
        on_clk = ((t - tref_t) % T) == 0; endfunction
    function [63:0] at(input [63:0] c, input integer cyc, input integer ps);   // c + cyc taktu + ps (se znamenkem)
        reg signed [63:0] o;
        begin o = cyc * T + ps; at = c + o; end endfunction
    function integer slot(input integer ph);             // faze hrany vuci hrane clk C, slovo s Q7 v C + q7ph
        slot = (ph < 8) ? (q7ph - 1250 * (7 - ph) - 625)   // 0..7: uprostred pred vzorkem Q<ph>
                        : (q7ph + ph - 9);                 // 8: 1 ps pred Q7, 9: presne na Q7, 10: 1 ps za (Q0 dalsiho)
    endfunction

    task chk(input cond, input [8*72-1:0] what);
        begin nchk = nchk + 1;
              if (!cond) begin errors = errors + 1; if (errors <= 25) $display("  CHYBA: %0s [%0s, t = %0t ps]", what, ctx, $time); end end
    endtask
    task chkv(input cond, input [8*72-1:0] what, input signed [63:0] got, input signed [63:0] exp);
        begin nchk = nchk + 1;
              if (!cond) begin errors = errors + 1;
                  if (errors <= 25) $display("  CHYBA: %0s: je %0d, ma byt %0d [%0s, t = %0t ps]", what, got, exp, ctx, $time); end end
    endtask

    // ---- sledovani: hrana clk, mrizka IDES8, epocha, rozhrani pps_ides3 ----
    always @(posedge clk) tclk = $time;
    always @(dut_ides.fclk) tfe = $time;
    always @(posedge dut_ides.pclk) tq7 = tfe;          // PCLK = 1 ps za nabeznou FCLK -> vzorek Q7 = posledni pred ni
    always @(posedge clk) if (dut_ides.fix == 3'd2) begin m_bw <= dut_ides.bw; m_mid <= dut_ides.f_ts[34:22]; m_top <= dut_ides.f_ts[47:35]; end
    always @(posedge clk) begin if (epoch != ep_q) ep_chg = ep_chg + 1; ep_q <= epoch; end
    always @(posedge clk) flk_q <= f_lock;              // f_lock pred hranou (nofine se pocita z nej)
    // f_ts se po preklopeni f_tgl drzi do dalsi hrany 1PPS (pps_cap3 cte f_ts az ~4 takty po f_tgl, SDC false_path)
    always @(posedge clk) begin
        ft_q <= f_tgl; lk_q2 <= f_lock;
        if (f_tgl != ft_q) begin n_tgl = n_tgl + 1; fh_v <= 1'b1; fh <= f_ts; end
        else if (fh_v && f_ts != fh) begin
            fh_viol = fh_viol + 1; fh_v <= 1'b0;
            if (fh_en) chk(1'b0, "rozhrani: f_ts zmeneno po preklopeni f_tgl (pred dalsi hranou)");
        end
        if (f_lock != lk_q2) fh_v <= 1'b0;
    end
    always @(posedge pin) fh_v <= 1'b0;

    // ---- ulohy ----
    task grid;                                           // faze vzorku Q7 vuci clk (meri se po kazdem zamceni)
        begin repeat (4) @(posedge clk); #1; q7ph = ((tq7 + 10 * T) - tclk) % T; end endtask
    task new_epoch;
        begin
            if (k_have) begin k_last = k0; k_last_ok = 1'b1; end
            k_have = 1'b0; kp_ok = 1'b0; dmin_e = 127; dmax_e = -128; n_epoch = n_epoch + 1;
            grid;
        end endtask
    // skok: po hrane tx plati tick = {h, 8'h00} (prechod dolniho bajtu FF -> 00 s prenosem do h)
    task jump_to(input [25:0] hh);
        begin
            @(posedge clk); #1;
            while (tick[7:0] >= 8'hE0) begin @(posedge clk); #1; end
            tj_hi = hh - 26'd1; tj = 1'b1;
            @(posedge clk); #1; tj = 1'b0;
            tref_t = tclk; tref_v = tick;
            tx = tclk + (256 - tick[7:0]) * T;
            chk(tick[33:8] == hh - 26'd1, "TB: skok ticku");
        end endtask
    // hrana v te, sirka pw (0 = pin zustane 1, < 20 taktu = kratky pulz); ceka na zverejneni (nejvys 80 taktu)
    task pulse(input [63:0] tt, input [63:0] pw);
        begin
            if (tt <= $time + 1) begin chk(1'b0, "TB: hrana naplanovana do minulosti"); tt = $time + 2; end
            #(tt - $time) pin = 1'b1;
            e_t = tt; e_tk = tick_at(tt); e_u = u_at(tt);
            n_edge = n_edge + 1;
            if (pw != 0 && pw < 20 * T) begin #(pw); pin = 1'b0; end
            s0 = seq; e_lat = 0;
            while (seq == s0 && e_lat < 80) begin @(posedge clk); #1; e_lat = e_lat + 1; end
            e_pub = (seq != s0); e_flk = flk_q;
            if (pw >= 20 * T) begin if (tt + pw > $time) #(tt + pw - $time); pin = 1'b0; end
        end endtask
    task chk_common;                                     // pole pps_cap3 po zverejneni
        begin
            chkv(seq == q_seq + 32'd1, "seq +1 na hranu", seq, q_seq + 1);
            chk(seq_nz == (seq != 32'd0), "seq_nz = (seq != 0)");
            chkv(seq_gt1 == (seq > 32'd1), "seq_gt1 = (seq > 1)", seq_gt1, seq > 32'd1);
            chk(pts_p == q_pts, "pts_p = pts_l minule hrany");
            chk(pfl_p == q_pfl, "pfl_p = pfl_l minule hrany");
            chk(d_p == q_d, "d_p = d_l minule hrany");
            chk(last == 34'd0 && prev == 34'd0, "last/prev = 0 (FW 0x0506)");
            q_seq = seq; q_pts = pts_l; q_pfl = pfl_l; q_d = d_l;
        end endtask
    task chk_fine(input dov, input [7:0] dval);          // jemny cas: PFLAGS 3, PTS proti referenci, d_l (dov: vynuceno)
        reg [47:0] kk; reg [33:0] d34; reg signed [63:0] dd, dv;
        begin
            n_fine = n_fine + 1;
            if (e_lat < lat_f0) lat_f0 = e_lat; if (e_lat > lat_f1) lat_f1 = e_lat;
            chk(e_pub, "hrana zverejnena (do 80 taktu)");
            chkv(pfl_l == 8'd3, "PFLAGS (ceka se jemny cas = 3)", pfl_l, 3);
            chk(nofine == 1'b0, "nofine = 0 s jemnym casem");
            chk(f_lock == 1'b1 || !need_lk, "f_lock = 1");
            chk(pts_l[10:0] == 11'd0, "PTS[10:0] = 0");
            kk = pts_l - e_u;
            if (!k_have) begin
                k_have = 1'b1; k0 = kk;
                chkv(s48(kk) >= -2 * U1 && s48(kk) <= 3 * U1, "posun osy K mimo -2..+3 taktu [T/16384]", s48(kk), 0);
                if (k0_n == 0 || s48(kk) < k0_min) k0_min = s48(kk);
                if (k0_n == 0 || s48(kk) > k0_max) k0_max = s48(kk);
                k0_n = k0_n + 1;
                if (n_epoch <= 3)
                    $display("  epocha %0d: t_rel = 0x%02h, vzorek Q7 v clk + %0d ps, K = %0d (%.4f taktu)",
                             epoch, dut_ides.t_rel, q7ph, s48(kk), s48(kk) / 16384.0);
            end
            dk = s48(kk - k0);
            if (dk > max_dk) max_dk = dk;
            if (-dk > max_dk) max_dk = -dk;
            chkv(dk >= -KW && dk <= KW, "PTS - skutecna hrana - K [T/16384]", dk, 0);
            if (kp_ok) begin
                dd = s48((pts_l - kp_p) - (e_u - kp_u));
                if (dd > max_dd) max_dd = dd;
                if (-dd > max_dd) max_dd = -dd;
                chkv(dd >= -KW && dd <= KW, "rozdil PTS po sobe jdoucich hran - skutecny rozdil", dd, 0);
            end
            kp_ok = 1'b1; kp_p = pts_l; kp_u = e_u;
            d34 = pts_l[47:14] - (e_tk + 34'd2);         // hruby = tick hrany + 2 takty (2 FF synchronizace)
            if (!dov && on_clk(e_t) && d_l == sat8(d34 + 34'd1)) d34 = d34 + 34'd1;   // hrana na hrane clk: c_tick o 1 driv
            chkv(d_l == (dov ? dval : sat8(d34)), "d_l (jemny - hruby, saturovane)", $signed(d_l), dov ? $signed(dval) : $signed(sat8(d34)));
            if (!dov) begin
                dv = s34(d34);
                if (dv < dmin_e) dmin_e = dv;
                if (dv > dmax_e) dmax_e = dv;
                chkv(dv >= -8 && dv <= 8, "d = jemny - hruby mimo +-8 taktu", dv, 0);
                chkv(dmax_e - dmin_e <= 1, "d v epose mimo okno 1 taktu (max - min)", dmax_e - dmin_e, 1);
            end
            pcov[pts_l[13:11]] = 1'b1;
        end endtask
    task chk_coarse(input nf);                           // bez jemneho casu: PTS = {c_tick, 14'b0}, PFLAGS 1, d_l 0
        begin
            n_coarse = n_coarse + 1;
            if (e_lat < lat_c0) lat_c0 = e_lat; if (e_lat > lat_c1) lat_c1 = e_lat;
            chk(e_pub, "hrana zverejnena (do 80 taktu)");
            chkv(pfl_l == 8'd1, "PFLAGS (ceka se hruby cas = 1)", pfl_l, 1);
            chkv((pts_l[47:14] == e_tk + 34'd2 || (on_clk(e_t) && pts_l[47:14] == e_tk + 34'd1)) && pts_l[13:0] == 14'd0,
                 "PTS = {tick hrany + 2, 14'b0} (hruby)", pts_l[47:14], e_tk + 34'd2);
            chkv(d_l == 8'd0, "d_l = 0 bez jemneho casu", d_l, 0);
            chkv(nofine == nf, "nofine", nofine, nf);
        end endtask
    task cover_sc(input integer mm);                     // pokryti scenaru podle vypujcky videne v kroku fix == 2
        begin
            n_sc = n_sc + 1;
            if (m_bw) begin
                if ({m_top, m_mid} == 26'd0) cov_c = cov_c + 1; else if (m_mid == 13'd0) cov_b = cov_b + 1; else cov_a = cov_a + 1;
            end
            if (mm <= -2) begin if (m_bw) cov_d1 = cov_d1 + 1; else cov_d0 = cov_d0 + 1; end
        end endtask
    task cal_edge;                                       // hrana uprostred mezi vzorky, bez skoku (prvni v epose = kotva K)
        begin @(posedge clk); #1; pulse(at(tclk, 30, slot(3)), PW); chk_common; chk_fine(1'b0, 8'd0); end endtask
    task unlock;
        begin
            chkv(ep_chg == ep_lock, "epocha se behem zamceni nezmenila", ep_chg - ep_lock, 0);
            force dut_ides.u_pll.RESET = 1'b1;
            n = 0; while (f_lock && n < 20) begin @(posedge clk); #1; n = n + 1; end
            chk(!f_lock, "f_lock = 0 po RESET PLL (do 20 taktu)");
        end endtask
    task wait_relock(input [3:0] ee, input integer cc);
        begin
            n = 0; while (!f_lock && n < 150) begin @(posedge clk); #1; n = n + 1; end
            chk(f_lock, "f_lock = 1 po uvolneni RESET PLL");
            repeat (3) @(posedge clk); #1;
            chkv(epoch == ee + 4'd1, "epocha +1 po znovuzamceni", epoch, ee + 4'd1);
            chkv(ep_chg - cc == 1, "epocha se zmenila prave jednou", ep_chg - cc, 1);
            ep_lock = ep_chg;
            new_epoch;
        end endtask
    task relock;
        reg [3:0] ee; integer cc;
        begin
            ee = epoch; cc = ep_chg;
            @(posedge clk); #1; t0 = tclk; release dut_ides.u_pll.RESET;
            n = 0; while (!f_lock && n < 150) begin @(posedge clk); #1; n = n + 1; end
            lat_rel = (tclk - t0) / T;
            wait_relock(ee, cc);
        end endtask

    // ============================================================
    initial begin
        ctx = "start"; ll_map = ""; sp_map = ""; rl_map = "";
        @(posedge clk); #1; tref_t = tclk; tref_v = tick;
        chk(seq == 0 && !seq_nz && !seq_gt1 && pfl_l == 0 && pts_l == 0 && !nofine && epoch == 0, "vychozi stav pps_cap3 / pps_ides3");
        // ---- 1) prvni zamceni PLL, zakladni hrany (8 poloh IDES8, prvni = kotva K) ----
        n = 0; while (!f_lock && n < 200) begin @(posedge clk); #1; n = n + 1; end
        repeat (3) @(posedge clk); #1;
        chk(f_lock, "PLL zamcena po startu");
        chkv(epoch == 4'd1 && ep_chg == 1, "epocha 1 po prvnim zamceni", epoch, 1);
        ep_lock = ep_chg; new_epoch;
        ctx = "zakladni hrany";
        cal_edge;
        chk(seq == 1 && seq_nz && !seq_gt1, "prvni hrana: seq 1, seq_nz 1, seq_gt1 0");
        for (ph = 0; ph < 8; ph = ph + 1) begin
            @(posedge clk); #1; pulse(at(tclk, 30 + ph, slot(ph)), PW); chk_common; chk_fine(1'b0, 8'd0);
        end
        // ---- 2) hranice prenosu a) b) c), d) = hrana o 1..8 taktu driv ----
        for (sc = 0; sc < 3; sc = sc + 1)
            for (m = -9; m <= 1; m = m + 1)
                for (ph = 0; ph < 11; ph = ph + 1) begin
                    r = $random(seed);
                    case (sc)
                        0: begin h = r[25:0]; if (h[12:0] == 13'd0) h[0] = 1'b1; end   // a) jen tick[7:0]
                        1: h = {r[12:0] | 13'd1, 13'd0};                                 // b) tick[20:0] same 1
                        default: h = 26'd0;                                              // c) tick[33:0] same 1
                    endcase
                    $sformat(ctx, "scenar %c, takt %0d, faze %0d", 8'd97 + sc, m, ph);
                    jump_to(h);
                    pulse(at(tx, m, slot(ph)), PW);
                    chk_common; chk_fine(1'b0, 8'd0); cover_sc(m);
                end
        // ---- 3) pps_cap3: saturace d_l (dd_r vynuceno od 5. taktu po hrane do zverejneni), nofine ----
        for (i = 0; i < 10; i = i + 1) begin
            case (i)
                0: dv34 = 34'd127;          1: dv34 = 34'd128;          2: dv34 = 34'd300;
                3: dv34 = -34'sd128;        4: dv34 = -34'sd129;        5: dv34 = -34'sd300;
                6: dv34 = 34'h1_FFFF_FFFF;  7: dv34 = 34'h2_0000_0000;  8: dv34 = 34'd5;
                default: dv34 = -34'sd5;
            endcase
            $sformat(ctx, "saturace d_l, dd = %0d", s34(dv34));
            @(posedge clk); #1; te = at(tclk, 30, slot(i % 8));
            fork
                pulse(te, PW);
                begin #(te + 5 * T - $time); force dut_cap.dd_r = dv34; end
            join
            release dut_cap.dd_r;
            chk_common; chk_fine(1'b1, sat8(dv34));
        end
        for (i = 0; i < 2; i = i + 1) begin
            ctx = "nofine (f_tgl blokovan)";
            @(posedge clk); #1; te = at(tclk, 30, slot(2 + i));
            if (f_tgl) force dut_cap.f_tgl = 1'b1; else force dut_cap.f_tgl = 1'b0;
            pulse(te, PW);
            chk_common; chk_coarse(1'b1);
            chkv(e_lat >= 34, "bez jemneho casu zverejneni az po okne 31 taktu", e_lat, 34);
            release dut_cap.f_tgl;
            repeat (60) @(posedge clk); #1;
        end
        ctx = "po nofine"; cal_edge; cal_edge;
        // ---- 4) ztrata zamku: hruby cas, epocha +1, nova kotva K ----
        ctx = "ztrata zamku";
        unlock;
        nt0 = n_tgl;
        for (i = 0; i < 4; i = i + 1) begin
            r = $random(seed);
            h = (i == 1) ? 26'd0 : (i == 2) ? {r[12:0] | 13'd1, 13'd0} : r[25:0];
            $sformat(ctx, "bez zamku, hrana %0d", i);
            jump_to(h);
            pulse(at(tx, -2 + (i % 2), 1234 + 2000 * i), PW);    // c_tick (tick + 2) pres prechod dolniho bajtu
            chk_common; chk_coarse(1'b0);
        end
        chkv(n_tgl == nt0, "f_tgl se bez zamku nepreklopi", n_tgl - nt0, 0);
        relock;
        ctx = "nova epocha"; cal_edge;
        for (sc = 1; sc < 3; sc = sc + 1)
            for (m = -4; m <= 0; m = m + 1)
                for (ph = 0; ph < 8; ph = ph + 1) begin
                    r = $random(seed);
                    h = (sc == 1) ? {r[12:0] | 13'd1, 13'd0} : 26'd0;
                    $sformat(ctx, "epocha 2, scenar %c, takt %0d, faze %0d", 8'd97 + sc, m, ph);
                    jump_to(h);
                    pulse(at(tx, m, slot(ph)), PW);
                    chk_common; chk_fine(1'b0, 8'd0); cover_sc(m);
                end
        // ---- 5) ztrata zamku uprostred mereni: jemny cas spravny, nebo hruby (nikdy spatny jemny) ----
        for (i = 0; i < 16; i = i + 1) begin
            $sformat(ctx, "ztrata zamku %0d taktu po hrane", i);
            @(posedge clk); #1; te = at(tclk, 30, slot(i % 8));
            fork
                pulse(te, PW);
                begin #(te + i * T + 3000 - $time); force dut_ides.u_pll.RESET = 1'b1; end
            join
            chk_common;
            if (pfl_l == 8'd3) begin                   // mereni dokonceno pred ztratou zamku (fa = f_lock pri prijmu)
                ll_f = ll_f + 1; ll_map = {ll_map[8*31-1:0], "j"}; need_lk = 1'b0; chk_fine(1'b0, 8'd0); need_lk = 1'b1;
            end
            else begin ll_c = ll_c + 1; ll_map = {ll_map[8*31-1:0], "h"}; chk_coarse(e_flk); end
            n = 0; while (f_lock && n < 20) begin @(posedge clk); #1; n = n + 1; end
            chk(!f_lock, "f_lock = 0 po RESET PLL");
            relock; cal_edge;
        end
        // ---- 6a) znovuzamceni pri pinu = 1 (hrana > 31 taktu pred uvolnenim): zadne zverejneni navic ----
        ctx = "znovuzamceni pri pin = 1";
        unlock;
        @(posedge clk); #1; pulse(at(tclk, 20, 4321), 0);
        chk_common; chk_coarse(1'b0);
        repeat (40) @(posedge clk); #1;
        nt0 = n_tgl; s0 = seq;
        relock;
        repeat (100) @(posedge clk); #1;
        chkv(seq == s0, "znovuzamceni pri pin = 1: zadne zverejneni navic", seq - s0, 0);
        $display("  znovuzamceni pri pin = 1: IDES8 hlasi hranu v 1. slove po uvolneni: %0s",
                 (n_tgl != nt0) ? "ano (falesna, bez hrube hrany se nezverejni)" : "ne");
        pin = 1'b0; repeat (10) @(posedge clk);
        cal_edge; cal_edge; cal_edge;
        // ---- 6b) hrana nd taktu pred uvolnenim PCLK (pin drzi 1): kontrola N1 (s -DTB_LAX_RELOCK jen pruzkum) ----
        for (nd = 0; nd <= 28; nd = nd + 1) begin
            $sformat(ctx, "hrana %0d taktu pred uvolnenim PCLK", nd);
            unlock;
            e0 = epoch; i = ep_chg;
            @(posedge clk); #1; t0 = tclk + 40 * T;        // uvolneni RESET 1 ps po hrane t0, PCLK od t0 + lat_rel taktu
            te = at(t0, lat_rel - nd, slot(3));
            fork
                begin #(t0 + 1 - $time); release dut_ides.u_pll.RESET; end
                pulse(te, 0);
            join
            chk_common;
            if (pfl_l == 8'd3) begin
                dk = s48(pts_l - e_u - k0);                // K minule epochy (v modelu je K pri kazdem zamceni stejne)
                if (dk < -KW || dk > KW) begin
                    rl_bad = rl_bad + 1; rl_map = {rl_map[8*31-1:0], "X"};
                    $display("  NALEZ N1: hrana %0d taktu pred uvolnenim PCLK: PFLAGS 3, PTS - hrana - K = %0d (%.0f ps), d_l = %0d",
                             nd, dk, dk * 0.6103515625, $signed(d_l));
                end else rl_map = {rl_map[8*31-1:0], "j"};
`ifndef TB_LAX_RELOCK
                chkv(dk >= -KW && dk <= KW, "N1: jemny cas hrany pred znovuzamcenim", dk, 0);
`endif
            end else begin rl_map = {rl_map[8*31-1:0], "h"}; chk_coarse(e_flk); end
            wait_relock(e0, i);
            pin = 1'b0; repeat (10) @(posedge clk);
            ctx = "po N1"; cal_edge;
        end
        // ---- 7a) kratky pulz 3 ns (kratsi nez takt): pruzkum ----
        for (i = 0; i < 20; i = i + 1) begin
            $sformat(ctx, "kratky pulz 3 ns, faze %0d ps", 250 + 500 * i);
            @(posedge clk); #1; nt0 = n_tgl;
            pulse(at(tclk, 30, 250 + 500 * i), 3000);
            if (n_tgl != nt0) sp_fin = sp_fin + 1;
            if (e_pub) begin
                sp_pub = sp_pub + 1; chk_common;
                if (pfl_l == 8'd3) begin sp_map = {sp_map[8*31-1:0], "j"}; chk_fine(1'b0, 8'd0); end
                else begin sp_map = {sp_map[8*31-1:0], "h"}; chk_coarse(e_flk); end
            end else sp_map = {sp_map[8*31-1:0], "-"};
            ctx = "po kratkem pulzu"; cal_edge;
        end
        // ---- 7b) zakmit 3 ns (hruby ho nevidi) nd taktu pred skutecnou hranou: kontrola pro nd >= 10 (fa <= 0 pri hrube
        //      hrane), s -DTB_STRICT_GLITCH pro vsechna nd ----
        for (i = 0; i < 12; i = i + 1) begin
            nd = (i == 0) ? 2 : (i == 1) ? 4 : (i == 2) ? 5 : (i == 3) ? 6 : (i == 4) ? 7 : (i == 5) ? 8 : (i == 6) ? 10 :
                 (i == 7) ? 20 : (i == 8) ? 30 : (i == 9) ? 36 : (i == 10) ? 40 : 50;
            $sformat(ctx, "zakmit %0d taktu pred hranou", nd);
            @(posedge clk); #1; tg = at(tclk, 30, 3000);   // [C + 3 ns, C + 6 ns]: zadna hrana clk -> hruby nevidi
            te = at(tclk, 30 + nd, slot(3));
            nt0 = fh_viol;
`ifndef TB_STRICT_GLITCH
            fh_en = 1'b0;
`endif
            fork
                begin #(tg - $time); pin = 1'b1; #3000; pin = 1'b0; end
                pulse(te, PW);
            join
            fh_en = 1'b1;
            chk_common;
            if (pfl_l == 8'd3) begin
                dk = s48(pts_l - e_u - k0);
                if (dk < -KW || dk > KW || fh_viol != nt0) gl_bad = gl_bad + 1;
                $display("  zakmit %0d taktu pred hranou: PFLAGS 3, PTS - hrana - K = %0d (%.0f ps), d_l = %0d%0s%0s",
                         nd, dk, dk * 0.6103515625, $signed(d_l), (dk < -KW || dk > KW) ? " -> NALEZ N2" : "",
                         (fh_viol != nt0) ? ", f_ts se menilo po f_tgl (cteno rozpracovane)" : "");
`ifdef TB_STRICT_GLITCH
                chkv(dk >= -KW && dk <= KW, "N2: jemny cas hrany po zakmitu", dk, 0);
`else
                if (nd >= 10) chkv(dk >= -KW && dk <= KW, "N2: jemny cas hrany po zakmitu >= 10 taktu pred ni", dk, 0);
`endif
            end else begin
                $display("  zakmit %0d taktu pred hranou: PFLAGS %0d (hruby cas), nofine %0d", nd, pfl_l, nofine);
                chk_coarse(e_flk);
            end
            repeat (60) @(posedge clk);
            ctx = "po zakmitu"; cal_edge;
        end
        // ---- souhrn ----
        ctx = "souhrn";
        chkv(pcov == 8'hFF, "pokryti: vsech 8 poloh IDES8", pcov, 8'hFF);
        chk(cov_a > 0 && cov_b > 0 && cov_c > 0, "pokryti: scenare a, b, c s vypujckou");
        chk(cov_d0 > 0 && cov_d1 > 0, "pokryti: scenar d s bw = 0 i 1");
        $display("1PPS v3: hran %0d (jemny cas %0d, hruby %0d), polohy IDES8 %b, epoch %0d (prijem PCLK %0d taktu po uvolneni RESET PLL)",
                 n_edge, n_fine, n_coarse, pcov, n_epoch, lat_rel);
        $display("hranice prenosu (hrany %0d): a) %0d, b) %0d, c) %0d s vypujckou; d) hrana 1..8 taktu driv: bw = 1 %0d, bw = 0 %0d",
                 n_sc, cov_a, cov_b, cov_c, cov_d1, cov_d0);
        $display("max |PTS - u - K| = %0d (%.0f ps), max chyba rozdilu = %0d (%.0f ps), K ve %0d epochach %0d..%0d, latence jemny %0d..%0d / hruby %0d..%0d taktu",
                 max_dk, max_dk * 0.6103515625, max_dd, max_dd * 0.6103515625, k0_n, k0_min, k0_max, lat_f0, lat_f1, lat_c0, lat_c1);
        $display("ztrata zamku 0..15 taktu po hrane (j = jemny spravne, h = hruby): %0s", ll_map);
        $display("hrana 0..28 taktu pred uvolnenim PCLK (j/h, X = spatny jemny cas = nalez N1): %0s, spatne %0d", rl_map, rl_bad);
        $display("kratky pulz 3 ns, 20 fazi po 0,5 ns (j/h zverejneno, - ztraceno): %0s; IDES8 videl %0d/20, zverejneno %0d/20", sp_map, sp_fin, sp_pub);
        $display("zakmit 3 ns pred hranou (2..50 taktu): spatny nebo rozpracovany jemny cas %0d/12 (nalez N2)", gl_bad);
        $display("kontrol %0d, chyb %0d, simulovany cas %0t ps", nchk, errors, $time);
        if (errors == 0) $display("PASS: tb_pps_v3"); else $display("FAIL: tb_pps_v3 (%0d chyb)", errors);
        $finish;
    end
    initial begin #(64'd200_000_000_000); $display("FAIL: tb_pps_v3 timeout (%0s)", ctx); $finish; end
endmodule
