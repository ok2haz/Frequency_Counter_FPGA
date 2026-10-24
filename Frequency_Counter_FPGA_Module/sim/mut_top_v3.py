#!/usr/bin/env python3
"""Negativni kontroly sim/tb_top_v3.sv (protokol v3, top_v3): kazda mutace RTL (docasna kopie src/ ve vystupnim adresari,
repo se nemeni) musi test SHODIT. Bezi paralelne (kazda mutace = jedna simulace 3..15 min).
Pouziti: python3 sim/mut_top_v3.py <koren FPGA modulu> <vystupni adresar> [jmeno ...] [-j N]
         python3 sim/mut_top_v3.py <koren FPGA modulu> --selftest   (jen kontrola kotev, bez simulace)
2026-10-24: kotvy VSECH vybranych mutaci se zkontroluji (v kodu prave jednou) PRED prvni simulaci (L-0224); mutace muze mit
vic nahrad (seznam dvojic misto jedne kotvy)."""
import os, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

root = os.path.abspath(sys.argv[1])
selftest = len(sys.argv) > 2 and sys.argv[2] == '--selftest'
out = None if selftest else os.path.abspath(sys.argv[2])
args = [] if selftest else sys.argv[3:]
jobs = 8
if '-j' in args:
    i = args.index('-j'); jobs = int(args[i + 1]); del args[i:i + 2]
only = set(args)
sim, src = os.path.join(root, 'sim'), os.path.join(root, 'src')

TDC = ['-DSIM_LUT_PS=800', '-DSIM_IDX', '-DSIM_TAP_MEAN=39', '-DSIM_TAP_SPREAD=15', '-DSIM_BREAK_IDX=268', '-DSIM_TOP']
MODE = {0: ['-DSIM_AUTOCAL', '-DTB_NFR=8', '-DTB_FB=16.100161e6'], 1: ['-DSIM_AUTOCAL', '-DTB_MODE=1', '-DSIM_RAW=4'], 2: ['-DTB_MODE=2'],
        3: ['-DSIM_AUTOCAL', '-DTB_MODE=3', '-DTB_FA=5.0e6', '-DTB_WA=37300.0'],
        4: ['-DSIM_AUTOCAL', '-DTB_MODE=4', '-DTB_FB=143.300173e6'],
        5: ['-DSIM_AUTOCAL', '-DTB_MODE=5', '-DDEEP_RAW'], 6: ['-DSIM_AUTOCAL', '-DTB_MODE=5', '-DTB_PS_LATE', '-DDEEP_RAW'],
        7: ['-DSIM_AUTOCAL']}
# 2026-10-24: rezim 7 = cileny test tb_hold_inv (ztrata hodin, predelicka, zdroj B; invarianty kopii)
TBS = {7: ('tb_hold_inv', 'tb_hold_inv.sv')}
# (jmeno, soubor v src/, puvodni text, mutace, rezim testbenche)
M = [
    # (konstantni posun indexu, napr. push = tq1, je EKVIVALENTNI mutant: vyrusi se ve vsech rozdilech -- netestuje se)
    ('FIFO hloubky 1', 'v3_core.v', "                    else if (fn == 2'd1) begin fs <= xcnt; fn <= 2'd2; end",
     "                    else if (fn == 2'd1) begin fh <= xcnt; end", 0),
    ('rozdil t: bez vypujcky', 'v3_core.v', "dtu <= tm[39:20] - t0[39:20] - {19'd0, btl};", "dtu <= tm[39:20] - t0[39:20];", 0),
    ('zaznam: slovo T0 = SX', 'v3_core.v', "             | ({32{oh[3]}} & t0[31:0])", "             | ({32{oh[3]}} & sx[31:0])", 0),
    # 2026-10-24: take = pop & ~hold & nfc (nfc = registr ~freeze & ~clear z v3_ring) -> stejna chyba vyjadrena vstupy freeze/clear
    ('znacka v taktu clear se pricte', 'v3_core.v', "    wire       take = pop & ~hold & nfc;",
     "    wire       take = pop & ~hold & ~freeze;", 0),
    ('freeze se nepouzije', 'v3_core.v', "    wire       take = pop & ~hold & nfc;", "    wire       take = pop & ~hold & ~clear;", 0),
    ('kontrolni pocitani bez hran', 'v3_core.v', "        ce  <= cp2 & ~cp3 & cnt_en;", "        ce  <= 1'b0;", 0),
    ('zaznam B do slotu A', 'v3_core.v', "            wa <= {wrec + {{(RAW-1){1'b0}}, k1[3]}, k1[2:0]};", "            wa <= {wrec, k1[2:0]};", 0),
    ('ACK se ignoruje', 'app_v3.v', "                                rec_ack <= {rx_p3, rx_p2, rx_p1, rx_p0};", "                                rec_ack <= rec_ack;", 0),
    ('CRC bloku 0 se nekontroluje', 'app_v3.v', "rx_b1 != VER || !rx_blk_ok[0]) begin", "rx_b1 != VER) begin", 0),
    ('delka transakce se nekontroluje', 'app_v3.v', "                if (!rx_len_ok) begin", "                if (1'b0) begin", 0),
    ('stranka bez posunu bloku', 'app_v3.v', "a_pbase + ((blk == 2'd1) ? 16'd0 : (blk == 2'd2) ? 16'd84 : 16'd168)", "a_pbase", 0),
    ('preteceni: REC_ACK se neposune', 'app_v3.v',
     "if (state != S_ASM && (rec_head - rec_ack) > RING_USE) begin rec_ack <= rec_head - RING_USE; ovf <= 1'b1; end", "", 1),
    ('potvrzeni tabulky bez masky', 'top_v3.v', ".lw_commit(lw_cm_p & cmk2[1])", ".lw_commit(lw_cm_p)", 2),
    ('tabulka do aktivni banky', 'tdc.v', "    wire          bwr     = (BANKS != 0) ? ~bank_r : 1'b0;", "    wire          bwr     = (BANKS != 0) ? bank_r : 1'b0;", 2),
    # TSPIPE (rozlozeni 2026-10-23): platnost o takt driv nez ts_ps -> znacka predchozi spustene hrany s indexem teto.
    # (horni cast z tr* o takt pozdeji = konstantni posun vsech znacek a dolni cast z lut_q3 o takt pozdeji = tataz polozka
    # tabulky, kod se drzi >= 5 taktu -> ekvivalentni mutanty, netestuji se)
    ('znacka: ts_valid o takt driv', 'tdc.v', "        ts_valid <= (TSPIPE != 0) ? tv_w : tv_z;", "        ts_valid <= tv_z;", 0),
    # E4 (FW 0x0502): jemny cas 1PPS -- obracene poradi bitu IDES8, bez t_rel (osa PCLK), jemny cas se nepouzije
    ('IDES8: obracene poradi bitu', 'v3_core.v', "    parameter Q0_FIRST = 1 ", "    parameter Q0_FIRST = 0 ", 0),
    ('1PPS: bez t_rel', 'v3_core.v', "        hl  <= p_ts[10:3] + t_rel;",
     "        hl  <= p_ts[10:3];", 0),
    ('1PPS: jemny cas se nepouzije', 'v3_core.v', "pts_l <= f_ts; pfl_l <= 8'd3;", "pts_l <= {c_tick, 14'd0}; pfl_l <= 8'd3;", 0),
    # E5 (FW 0x0503): predelicka -- zaznam bez log2 P (CH 6:4), sonda /8, SET_CONFIG 0x07 prohozeny kanal
    ('predelicka: zaznam bez P', 'v3_core.v', "    assign o_presc = p_seg;", "    assign o_presc = 3'd0;", 4),
    ('predelicka: sonda /8', 'v3_core.v', "        s <= {s[1:0], c[3]};", "        s <= {s[1:0], c[2]};", 4),
    ('SET_CONFIG 0x07: prohozeny kanal', 'app_v3.v', "8'h07: if (rx_p1[0]) pr_b_req", "8'h07: if (!rx_p1[0]) pr_b_req", 4),
    # FW 0x0504: soucty v DSP -- bez vynulovani na zacatku segmentu, pricitani o takt driv (horni cast rozdilu jeste stara)
    ('DSP: soucty bez clear', 'v3_core.v', "            if (clear)   begin sx_d <= 48'd0; st_d <= 54'd0; end\n            else if (v2)",
     "            if (v2)", 0),
    ('DSP: pricita o takt driv', 'v3_core.v', "            else if (v2) begin sx_d <= sx_d", "            else if (v1b) begin sx_d <= sx_d", 0),
    # FW 0x0505: PSRAM -- cteni bez RWDS (kazdy takt), bez kalibrace latence zapisu, okno bez posunu adresy
    ('PSRAM: cteni bez RWDS', 'v3_core.v', "                    end else if (rs[1] ^ rs[0]) begin         // RWDS",
     "                    end else if (1'b1) begin         // RWDS", 5),
    ('PSRAM: pevna latence zapisu', 'v3_core.v', "    wire [7:0] wait_b = W0 + {3'd0, (cal ? ctry : wsel)};", "    wire [7:0] wait_b = W0 + {3'd0, wsel};", 5),
    ('PSRAM: okno bez posunu adresy', 'v3_core.v', "wra <= ar2[10:0] * 22'd3072;", "wra <= 22'd0;", 5),
    # audit 2026-10-23 F-0262: RWDS = 1 behem CA (priznak latence) -> bez zbrojeni je konec priznaku prvni "bajt"
    # F-0264: kalibrace nezkousi fazi 1 (vystup PSRAM neplatny pri vzorku na nabezne hrane)
    ('PSRAM: jen faze cteni 0', 'v3_core.v', "else if (ctry == 5'd31) begin rph <= 1'b1;", "else if (ctry == 5'd31) begin rph <= 1'b0;", 6),
    # F-0266: rezim 0 bez vyluky (behem hlubokeho zaznamu prepne kanal a pise do BSRAM), STOP se neprovede
    ('RAW: rezim 0 bez vyluky', 'v3_core.v', "if (md2 == 2'd0 && !drun && !wl) begin", "if (md2 == 2'd0) begin", 5),
    ('RAW: bez STOP', 'v3_core.v', "else if (md2 == 2'd1 && drun) begin dfin <= 1'b1; dacc <= dacc + 4'd1; end",
     "else if (md2 == 2'd1 && drun) begin end", 5),
    # F-0268: okno ceka na vsech 512 znacek (bez odpovedi PSRAM se zasekne)
    ('PSRAM: okno ceka na vsechny bajty', 'v3_core.v', "            if (wb == 5'd24 && p_free) begin\n", "            if (wb == 5'd24 && p_free && widx == 10'd512) begin\n", 5),
    ('RAW: bez priznaku mezer', 'v3_core.v', "(drun && !dfin)) && (ch ? gap_b : gap_a)) dgap <= 1'b1;", "(drun && !dfin)) && (ch ? gap_b : gap_a)) dgap <= 1'b0;", 5),
    ('PSRAM: cteni bez zbrojeni RWDS', 'v3_core.v', "                    if (!rarm) begin\n", "                    if (1'b0) begin\n", 5),
    ('B z pinu A: znacka P_B', 'top_v3.v', ".nfc(nfc_b), .presc(pr_b_eff),", ".nfc(nfc_b), .presc(pr_b),", 3),   # 2026-10-24: port nfc
    # audit 2026-10-23 (FW 0x0506): nove cesty
    ('ring: jednickovy vyber bez rotace', 'v3_core.v', "                ohA  <= {ohA[6:0], ohA[7]};", "", 0),
    ('ring: odpocet bez vypujcky hornich 11 b', 'v3_core.v', "            chi <= chi - 11'd1;", "            chi <= chi;", 0),
    ('1PPS: seq > 1 vzdy', 'v3_core.v', "seq_nz <= 1'b1; seq_gt1 <= seq_nz;", "seq_nz <= 1'b1; seq_gt1 <= 1'b1;", 0),
    ('B zdroj ignorovan', 'top_v3.v', "    wire        sig_b = (bs2[0] ? ch_a : div_b) ^ bs2[1];", "    wire        sig_b = div_b;", 3),
    ('RAW: prohozene poloviny', 'app_v3.v', "it_src = g_cur[0] ? raw_q[47:24] : raw_q[23:0];", "it_src = g_cur[0] ? raw_q[23:0] : raw_q[47:24];", 0),
    # 2026-10-24 (optimalizace casovani): nove struktury -- kazda musi test shodit
    ('1PPS: wnz se pri hrane nenuluje', 'v3_core.v', "win <= 5'd0; wnz <= 1'b0; wnz_b <= 1'b0; end", "win <= 5'd0; wnz_b <= 1'b0; end", 0),
    ('1PPS: kopie povoleni bez wnz_b', 'v3_core.v', "    wire       pub_b = pend_b & wnz_b & (fok_b | wok_b);",
     "    wire       pub_b = pend_b & (fok_b | wok_b);", 0),
    ('tabulka B: kopie adresy se neaktualizuje', 'v3_core.v', "lw_a_qa <= lwa_s1; lw_a_qb <= lwa_s1;", "lw_a_qa <= lwa_s1;", 2),
    ('FIFO: fnz po vyberu vzdy 1', 'v3_core.v', "fnz <= fn[1]; end", "fnz <= 1'b1; end", 0),
    ('soucty: kopie first_b se nenuluje', 'v3_core.v', "first <= 1'b1; first_b <= 1'b1;", "first <= 1'b1;", 0),
    ('nfc: v taktu clear 1', 'v3_core.v', "                nfc_a   <= 1'b0; nfc_b <= 1'b0;\n", "                nfc_a   <= 1'b1; nfc_b <= 1'b1;\n", 0),
    ('nfc: pri seg_tick nenulovan', 'v3_core.v', "            nfc_a  <= 1'b0; nfc_b <= 1'b0;\n", "", 0),
    ('ring: horni pulslovo bez rotace', 'v3_core.v', "                ohAh <= {ohAh[6:0], ohAh[7]};\n", "", 0),
    ('hold_s: hx bez boot_tc', 'top_v3.v', "    wire bd_nx  = ok_s[1] & (boot_done | boot_tc);", "    wire bd_nx  = ok_s[1] & boot_done;", 7),
    ('hold_s: hx bez ok_s', 'top_v3.v', "    wire bd_nx  = ok_s[1] & (boot_done | boot_tc);", "    wire bd_nx  = boot_done | boot_tc;", 7),
]
RTL = ['top_v3.v', 'v3_core.v', 'app_v3.v', 'spi_slave_phy_v3.v', 'spi_app.v', 'tdc.v']
# 2026-10-24: EKVIVALENTNI mutanty (test je shodit nemuze; nezachyceni se nepocita jako chyba, vypise se zduvodneni)
EKV = {
    # pfl_p = pps_gt1 ? pps_pfl_p : 0. Pri seq = 1 je pps_pfl_p = pfl_l pred prvni hranou = 0 (pps_cap3 nema reset,
    # pfl_l zacina 0), takze hradlo seq_gt1 nema pozorovatelny ucinek; stopa topv3 s touto mutaci je shodna s base/
    '1PPS: seq > 1 vzdy': 'pfl_p je pri seq = 1 vzdy 0 (pfl_l pred prvni hranou = 0) -> seq_gt1 bez ucinku',
}


def pairs(m):
    """[(kotva, nahrada), ...] -- jedna dvojice, nebo seznam dvojic v miste kotvy (b = None)."""
    name, fn, a, b, mode = m
    return a if isinstance(a, list) else [(a, b)]


def check_anchors(sel):
    bad = []
    for m in sel:
        s = open(os.path.join(src, m[1]), encoding='utf-8').read()
        for a, _ in pairs(m):
            if s.count(a) != 1:
                bad.append('%-40s %s: kotva %dx: %r' % (m[0], m[1], s.count(a), a[:70]))
    return bad


def run(m):
    name, fn, a, b, mode = m
    d = os.path.join(out, name.replace(' ', '_').replace(':', '').replace(',', ''))
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for f in RTL:
        shutil.copy(os.path.join(src, f), d)
    p = os.path.join(d, fn)
    s = open(p, encoding='utf-8').read()
    for aa, bb in pairs(m):
        if s.count(aa) != 1:
            return name, 'KOTVA NENALEZENA (%dx)' % s.count(aa), '', 0
        s = s.replace(aa, bb)
    open(p, 'w', encoding='utf-8').write(s)
    vvp = os.path.join(d, 't.vvp')
    top, tbf = TBS.get(mode, ('tb_top_v3', 'tb_top_v3.sv'))
    cmd = ['iverilog', '-g2012', '-I', sim] + TDC + MODE[mode] + ['-s', top, '-o', vvp,
           os.path.join(sim, 'gowin_models.v')] + [os.path.join(d, f) for f in RTL] + [os.path.join(sim, tbf)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return name, 'PREKLAD SELHAL', (r.stdout + r.stderr)[:200], 0
    t0 = time.time()
    try:
        r = subprocess.run(['vvp', '-n', vvp], capture_output=True, text=True, cwd=d, timeout=4 * 3600)
    except subprocess.TimeoutExpired:
        return name, 'CASOVY LIMIT', '', time.time() - t0
    caught = 'PASS' not in r.stdout
    if not caught and name in EKV:
        return name, 'EKVIVALENTNI', EKV[name][:80], time.time() - t0
    first = next((l.strip() for l in r.stdout.splitlines() if 'CHYBA' in l or 'FAIL' in l), '')
    return name, 'ZACHYCENO' if caught else '!!! NEZACHYCENO', first[:80], time.time() - t0


sel = [m for m in M if not only or m[0] in only]
nb = check_anchors(sel)
if nb or selftest:
    print('\n'.join(nb) if nb else 'kotvy OK: %d mutaci, kazda kotva v kodu prave jednou' % len(sel))
    sys.exit(1 if nb else 0)
bad = 0
with ThreadPoolExecutor(max_workers=jobs) as ex:
    for name, res, first, dt in ex.map(run, sel):
        print('%-34s %-16s %4.0f s  %s' % (name, res, dt, first), flush=True)
        bad += 0 if res in ('ZACHYCENO', 'EKVIVALENTNI') else 1
print('\nvsechny mutace zachyceny' if bad == 0 else '\n%d mutaci NEZACHYCENO' % bad)
sys.exit(1 if bad else 0)
