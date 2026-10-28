"""abl_cls.py -- studie CLS bitstreamu v3: ablace (varianty zdrojaku v kopii adresare) + synteza + P&R v Gowin.

Pouziti (z adresare Frequency_Counter_FPGA_Module, Windows, Gowin v C:/Gowin):
    python tools/abl_cls.py list                    # varianty
    python tools/abl_cls.py run <var> [<var> ...]   # kazda varianta paralelne ve vlastnim adresari abl/<var>/
    python tools/abl_cls.py table                   # tabulka vysledku (CLS, logika, registry, nezapojene site)
Varianta = seznam uprav kopie src/ (puvodni zdrojaky se nemeni). Uprava = (soubor, vzor, nahrada):
    vzor '@@RE@@<regex>'  : regularni vyraz (DOTALL), nahradi PRVNI shodu (musi existovat)
    vzor '@@DEL@@a|b'     : smaze radky obsahujici a nebo b
    jinak                 : presny text, musi byt v souboru prave jednou
Synteza po modulech: python tools/syn_hier.py abl/<var>/v3/impl/gwsynthesis/Counter_FPGA_v3_syn_rsc.xml
"""
import os, re, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GW_SH = r'C:\Gowin\Gowin_V1.9.12_x64\IDE\bin\gw_sh.exe'
ABL = os.path.join(ROOT, 'abl')

RAW_INST = r'@@RE@@    raw_cap3 #\(\.PS_INIT\(PS_INIT\)\) u_raw \(.*?\.ps_rwds\(IO_psram_rwds\[0\]\)\);'
RAW_OFF = ("    assign raw_n = 10'd0; assign raw_dn = 24'd0; assign raw_dst = 8'd0; assign raw_wsel = 6'd0; assign raw_win = 16'd0;\n"
           "    assign raw_dack = 8'd0; assign raw_q = 48'd0;\n"
           "    assign O_psram_ck[0] = 1'b0; assign O_psram_ck_n[0] = 1'b1; assign O_psram_cs_n[0] = 1'b1; assign O_psram_reset_n[0] = 1'b0;\n"
           "    assign IO_psram_dq[7:0] = 8'hzz; assign IO_psram_rwds[0] = 1'bz;")
PPS_SDC = '@@DEL@@u_ipps'
PRE_SDC = '@@DEL@@u_pca|u_pcb'

# varianty: jmeno -> [(soubor, vzor, nahrada), ...]
APP_HB = r"@@RE@@    always @\* begin\n        hb = 8'd0;\n        case \(bb\).*?\n        endcase\n    end"
APP_IT = r"@@RE@@    always @\* begin\n        case \(a_kind\[1:0\]\).*?it_src = 24'd0;\n    end"

# volby Gowin (bez zmeny RTL): jmeno -> radky tcl za zakladnimi volbami
TCLX = {
    'p0': 'set_option -place_option 0', 'p1': 'set_option -place_option 1', 'p3': 'set_option -place_option 3',
    'p4': 'set_option -place_option 4', 'td0': 'set_option -timing_driven 0', 'gf100': 'set_option -global_freq 100',
    'rep': 'set_option -replicate_resources 1', 'rs': 'set_option -resource_sharing 1', 'rt': 'set_option -retiming 1',
    'mf16': 'set_option -maxfan 16', 'mf32': 'set_option -maxfan 32', 'mf64': 'set_option -maxfan 64', 'hold': 'set_option -correct_hold_violation 1', 'r2': 'set_option -route_option 2', 'r0': 'set_option -route_option 0',
}

V = {
    'base': [],
    'tim2': [],
    'tim': [],                                            # jen jine jmeno (druha rada sestaveni stejneho src/)
    'aHB': [('app_v3.v', APP_HB, "    always @* hb = {bb[7:2], 2'b0} ^ {6'd0, bb[1:0]} ^ MAGIC ^ flags_b ^ st_clk;"),
            ('timing_v3.sdc', '@@DEL@@u_ipps|u_pca|u_pcb|rx_s_|u_pps|u_cseq', '')],
    'aIT': [('app_v3.v', APP_IT, "    always @* it_src = {g_cur[7:0], 16'd0} ^ hist_a ^ hist_b ^ raw_q[23:0];")],
    # 2026-10-27: hluboky RAW (PSRAM, raw_cap3) jako varianta: `define na zacatku top_v3.v (jako -DDEEP_RAW v simulaci)
    'deep': [('top_v3.v', "module top_v3 (", "`define DEEP_RAW\nmodule top_v3 (")],
    # 2026-10-27: cil prescaleru na vstupnich pinech 300 MHz (3,33 ns) misto 400 MHz (2,5 ns): strop vstupniho bufferu LVCMOS33 je 300 MHz
    'pin33': [('timing_v3.sdc', "create_clock -name pin_a -period 2.5 -waveform {0 1.25}", "create_clock -name pin_a -period 3.33 -waveform {0 1.665}"),
              ('timing_v3.sdc', "create_clock -name pin_b -period 2.5 -waveform {0 1.25}", "create_clock -name pin_b -period 3.33 -waveform {0 1.665}")],
    'noRAW': [('top_v3.v', RAW_INST, RAW_OFF)],            # RAW_CAPTURE i hluboky zaznam (PSRAM) pryc
    'noDSP': [('top_v3.v', "localparam SUM_DSP    = 1;", "localparam SUM_DSP    = 0;")],
    # 2026-10-28 (L-0247): soucty SX/ST/N jen kanalu B v logice (A dal v DSP) -- vada hornich bitu SX kanalu B
    'noDSPb': [('top_v3.v', "seg_chan3 #(.SUM_DSP(SUM_DSP)) u_sb (", "seg_chan3 #(.SUM_DSP(0)) u_sb (")],
    'noTSP': [('top_v3.v', "localparam TDC_TSPIPE = 1;", "localparam TDC_TSPIPE = 0;")],
    'noPPS': [('top_v3.v', "    pps_ides3 u_ipps (.clk(clk_p0_100m), .pin(gps_1pps), .tick(tick_hi), .f_tgl(ppsf_tgl), .f_ts(ppsf_ts), .f_lock(ppsf_lock),\n                      .epoch(pps_epoch));",
               "    assign ppsf_tgl = 1'b0; assign ppsf_ts = 48'd0; assign ppsf_lock = 1'b0; assign pps_epoch = 4'd0;"),
              ('timing_v3.sdc', PPS_SDC, '')],
    'noPRE': [('top_v3.v', "    pin_cnt3 u_pca (.pin(ch_a), .clk(clk_p0_100m), .wend(pwend), .sel(pr_a), .sig_out(div_a), .probe(prb_a));\n    pin_cnt3 u_pcb (.pin(ch_b), .clk(clk_p0_100m), .wend(pwend), .sel(pr_b), .sig_out(div_b), .probe(prb_b));",
               "    assign div_a = ch_a; assign div_b = ch_b; assign prb_a = 16'd0; assign prb_b = 16'd0;"),
              ('timing_v3.sdc', PRE_SDC, '')],
    # ---- 2026-10-24: druha analyza (presun do STM, nevyuzite zdroje FPGA) ----
    # hlavicka: predchozi 1PPS (84..95) -- STM si historii drzi sam
    'hPPSp': [('app_v3.v', "@@DEL@@8'd84: hb|8'd88: hb|8'd91: hb|8'd94: hb", '')],
    # hlavicka: a_ts / ts10 (28..31) -- STM pole ts10m nepouziva
    'hTS': [('app_v3.v', "@@DEL@@8'd28: hb", '')],
    # hlavicka: rx_last (56..59) -- STM nepouziva
    'hRXL': [('app_v3.v', "@@DEL@@8'd56: hb", '')],
    # hlavicka: fill16 (42..43) a head_m1 -> rec_head primo (STM odecte sam)
    'hFill': [('app_v3.v', "@@DEL@@8'd42: hb", ''),
              ('app_v3.v', "8'd32: hb = head_m1[7:0];  8'd33: hb = head_m1[15:8];  8'd34: hb = head_m1[23:16];  8'd35: hb = head_m1[31:24];",
               "8'd32: hb = rec_head[7:0];  8'd33: hb = rec_head[15:8];  8'd34: hb = rec_head[23:16];  8'd35: hb = rec_head[31:24];")],
    # drobnost: 32b porovnani pps_seq v top_v3 -> pps_nz
    'pnz': [('top_v3.v', "(pps_seq != 32'd0), ppsf_lock}", "pps_nz, ppsf_lock}")],
    # citace do DSP (ALU54D, 6 volnych): tick_hi, horni cast citace hran X obou kanalu, seg_idx
    'dTick': [('top_v3.v', "    reg  [33:0] tick_hi = 34'd0;", "    reg  [33:0] tick_hi = 34'd0 /* synthesis syn_dspstyle = \"dsp\" */;")],
    'dX': [('v3_core.v', "    reg  [31:0] xhi = 32'd0;", "    reg  [31:0] xhi = 32'd0 /* synthesis syn_dspstyle = \"dsp\" */;")],
    # sdilene tr1/tr2 (tick_hi - 1/-2) pro oba kanaly TDC (v2: SHARE_TR, -68 FF)
    'shTR': [('top_v3.v', "    wire [47:0] tick_ps = {tick_hi, 14'd0};",
              "    wire [47:0] tick_ps = {tick_hi, 14'd0};\n    reg  [33:0] tr1_r = 34'd0, tr2_r = 34'd0;\n"
              "    always @(posedge clk_p0_100m) begin tr1_r <= tick_hi; tr2_r <= tr1_r; end"),
             ('top_v3.v', "    tdc_chan #(.EXT_TR(0), .CAL_LOG2(CAL_LOG2), .TAPS(TDC_TAPS), .STRIDE(TDC_STRIDE), .KD(TDC_KD), .DUAL(TDC_DUAL), .NTAP(TDC_NTAP), .BANKS(1), .TSPIPE(TDC_TSPIPE)) u_tdca (",
              "    tdc_chan #(.EXT_TR(1), .CAL_LOG2(CAL_LOG2), .TAPS(TDC_TAPS), .STRIDE(TDC_STRIDE), .KD(TDC_KD), .DUAL(TDC_DUAL), .NTAP(TDC_NTAP), .BANKS(1), .TSPIPE(TDC_TSPIPE)) u_tdca ("),
             ('top_v3.v', "    tdc_chan #(.EXT_TR(0), .CAL_LOG2(CAL_LOG2), .TAPS(TDC_TAPS), .STRIDE(TDC_STRIDE), .KD(TDC_KD), .DUAL(TDC_DUAL), .NTAP(TDC_NTAP), .BANKS(1), .TSPIPE(TDC_TSPIPE)) u_tdcb (",
              "    tdc_chan #(.EXT_TR(1), .CAL_LOG2(CAL_LOG2), .TAPS(TDC_TAPS), .STRIDE(TDC_STRIDE), .KD(TDC_KD), .DUAL(TDC_DUAL), .NTAP(TDC_NTAP), .BANKS(1), .TSPIPE(TDC_TSPIPE)) u_tdcb ("),
             ('top_v3.v', ".dump_k(hist_k), .dump_q(hq_a), .code_o(code_a), .bub_o(bub_a), .tr1_ext(34'd0), .tr2_ext(34'd0),",
              ".dump_k(hist_k), .dump_q(hq_a), .code_o(code_a), .bub_o(bub_a), .tr1_ext(tr1_r), .tr2_ext(tr2_r),"),
             ('top_v3.v', ".dump_k(hist_k), .dump_q(hq_b), .code_o(code_b), .bub_o(bub_b), .tr1_ext(34'd0), .tr2_ext(34'd0),",
              ".dump_k(hist_k), .dump_q(hq_b), .code_o(code_b), .bub_o(bub_b), .tr1_ext(tr1_r), .tr2_ext(tr2_r),")],
    # pocet bublin / znacek pri CS segmentu v hlavicce (112..115) pryc, v seg_chan3 jen priznak (STM pocita zaznamy s FLAGS)
    'hBub': [('app_v3.v', "@@DEL@@8'd112: hb", ''),
             ('v3_core.v', "            if (bub    && o_bub != 8'hFF) o_bub <= o_bub + 8'd1;\n            if (cs_act && o_ovl != 4'hF)  o_ovl <= o_ovl + 4'd1;",
              "            if (bub)    o_bub <= 8'd1;\n            if (cs_act) o_ovl <= 4'd1;")],
    # 1PPS d (jemny - hruby) jen z 12 b (|d| < 2048 taktu; saturace na +-127 zustava)
    'dd12': [('v3_core.v', "    always @(posedge clk) dd_r <= f_ts[47:14] - c_tick;",
              "    wire [11:0] df12 = f_ts[25:14] - c_tick[11:0];\n    always @(posedge clk) dd_r <= {{22{df12[11]}}, df12};")],
    # PSEQ jen 16 b (STM rozbali), SEQ ramce jen 16 b
    'pseq16': [('v3_core.v', "            seq   <= seq + 32'd1;", "            seq[15:0] <= seq[15:0] + 16'd1;")],
    'seq16': [('app_v3.v', "                    seq_tx  <= seq_tx + 32'd1;", "                    seq_tx[15:0] <= seq_tx[15:0] + 16'd1;")],
    # ukazatele kruhu REC_HEAD / REC_ACK jen 16 b (STM rozbali; v hlavicce horni bajty 0)
    'p16': [('app_v3.v', "    reg [31:0] rec_head = 32'd0, rec_ack = 32'd0;",
             "    reg [15:0] rh16 = 16'd0, ra16 = 16'd0;\n    wire [31:0] rec_head = {16'd0, rh16}, rec_ack = {16'd0, ra16};"),
            ('app_v3.v', r"@@RE@@else rec_head <= rec_head \+ 32'd2;", "else rh16 <= rh16 + 16'd2;"),
            ('app_v3.v', r"@@RE@@begin rec_head <= rec_head \+ 32'd2; hpend", "begin rh16 <= rh16 + 16'd2; hpend"),
            ('app_v3.v', "if (state != S_ASM && (rec_head - rec_ack) > RING_USE) begin rec_ack <= rec_head - RING_USE; ovf <= 1'b1; end",
             "if (state != S_ASM && fill16 > RING_USE[15:0]) begin ra16 <= rh16 - RING_USE[15:0]; ovf <= 1'b1; end"),
            ('app_v3.v', "if (({rx_p3, rx_p2, rx_p1, rx_p0} - rec_ack) <= (rec_head - rec_ack)) begin\n                                rec_ack <= {rx_p3, rx_p2, rx_p1, rx_p0};",
             "if (({rx_p1, rx_p0} - ra16) <= fill16) begin\n                                ra16 <= {rx_p1, rx_p0};"),
            ('app_v3.v', "a_nrec <= ((rec_head - rec_ack) > 32'd24) ? 5'd24", "a_nrec <= (fill16 > 16'd24) ? 5'd24")],
    # dlouhe casovace (boot 24 b, cal_tmo 29 b, rt_dly 20 b, coh_hold 24 b) -> spolecna casova zakladna 1 ms (pwend)
    'tmr': [('top_v3.v', "    reg [16:0] pwc = 17'd0;\n    reg        pwend = 1'b0;\n    always @(posedge clk_p0_100m) begin\n        pwend <= (pwc == 17'd99998);\n        pwc   <= pwend ? 17'd0 : pwc + 17'd1;\n    end\n", ""),
            ('top_v3.v', "    wire [47:0] tick_ps = {tick_hi, 14'd0};",
             "    wire [47:0] tick_ps = {tick_hi, 14'd0};\n    reg [16:0] pwc = 17'd0;\n    reg        pwend = 1'b0;\n    always @(posedge clk_p0_100m) begin\n        pwend <= (pwc == 17'd99998);\n        pwc   <= pwend ? 17'd0 : pwc + 17'd1;\n    end"),
            ('top_v3.v', "    reg [23:0] boot_cnt  = 24'd0;", "    reg [7:0]  boot_cnt  = 8'd0;"),
            ('top_v3.v', "            boot_cnt  <= 24'd0;", "            boot_cnt  <= 8'd0;"),
            ('top_v3.v', "            boot_cnt <= boot_cnt + 24'd1;", "            if (pwend) boot_cnt <= boot_cnt + 8'd1;"),
            ('top_v3.v', "            boot_tc  <= (boot_cnt == 24'hFFFFFE);", "            boot_tc  <= pwend & (boot_cnt == 8'd166);"),
            ('top_v3.v', "    reg [28:0] cal_tmo   = 29'd0;", "    reg [12:0] cal_tmo   = 13'd0;"),
            ('top_v3.v', "        tmo_h <= &cal_tmo[28:15];\n        tmo_l <= &cal_tmo[14:0];", "        tmo_h <= pwend;\n        tmo_l <= (cal_tmo == 13'd5367);"),
            ('top_v3.v', "            cal_tmo <= cal_tmo + 29'd1;", "            if (pwend) cal_tmo <= cal_tmo + 13'd1;"),
            ('top_v3.v', "            cal_tmo <= 29'd0;", "            cal_tmo <= 13'd0;"),
            ('top_v3.v', "    reg [19:0] rt_dly  = 20'd0;", "    reg [3:0]  rt_dly  = 4'd0;"),
            ('top_v3.v', "        rt_tc   <= rt_run & (rt_dly == 20'hFFFFE);", "        rt_tc   <= rt_run & pwend & (rt_dly == 4'd9);"),
            ('top_v3.v', "            rt_dly <= rt_dly + 20'd1;", "            if (pwend) rt_dly <= rt_dly + 4'd1;"),
            ('top_v3.v', "    reg  [23:0] coh_hold = 24'hFFFFFF;", "    reg  [7:0]  coh_hold = 8'hFF;"),
            ('top_v3.v', "        if (rx_s[1] ^ rx_s[0])     coh_hold <= 24'hFFFFFF;\n        else if (coh_hold != 24'd0) coh_hold <= coh_hold - 24'd1;",
             "        if (rx_s[1] ^ rx_s[0])     coh_hold <= 8'hA8;\n        else if (pwend && coh_hold != 8'd0) coh_hold <= coh_hold - 8'd1;"),
            ('top_v3.v', "        coh_hz <= (coh_hold == 24'd0);", "        coh_hz <= (coh_hold == 8'd0);")],
}


def apply_edits(d, name, edits):
    for fn, pat, new in edits:
        p = os.path.join(d, 'src', fn)
        t = open(p, encoding='utf-8', newline='').read()
        crlf = '\r\n' in t
        if crlf:
            t = t.replace('\r\n', '\n')
        if pat.startswith('@@DEL@@'):
            pats = pat[7:].split('|')
            t = '\n'.join(l for l in t.split('\n') if not any(x in l for x in pats))
        elif pat.startswith('@@RE@@'):
            m = re.search(pat[6:], t, re.S)
            assert m, (name, fn, pat[:60])
            t = t[:m.start()] + new + t[m.end():]
        else:
            assert t.count(pat) == 1, (name, fn, t.count(pat), pat[:70])
            t = t.replace(pat, new)
        if crlf:
            t = t.replace('\n', '\r\n')
        open(p, 'w', encoding='utf-8', newline='').write(t)


def prepare(name):
    d = os.path.join(ABL, name)
    if os.path.isdir(d):
        shutil.rmtree(d, onerror=lambda f, path, e: (os.chmod(path, 0o700), f(path)))
    os.makedirs(d)
    shutil.copytree(os.path.join(ROOT, 'src'), os.path.join(d, 'src'))
    os.makedirs(os.path.join(d, 'v3'))
    shutil.copy(os.path.join(ROOT, 'v3', 'Counter_FPGA_v3.gprj'), os.path.join(d, 'v3', 'Counter_FPGA_v3.gprj'))
    parts = name.split('+')
    for pt in parts:
        apply_edits(d, name, V.get(pt, []))
    extra = chr(10).join(TCLX.get(pt, '') for pt in parts)
    fwd = d.replace('\\', '/')
    tcl = f'''open_project "{fwd}/v3/Counter_FPGA_v3.gprj"
set_option -top_module top_v3
set_option -use_sspi_as_gpio 1
set_option -use_mspi_as_gpio 1
set_option -use_cpu_as_gpio 0
set_option -use_mode_as_gpio 0
set_option -use_ready_as_gpio 0
set_option -use_done_as_gpio 0
set_option -use_reconfign_as_gpio 0
set_option -use_i2c_as_gpio 0
set_option -place_option 2
set_option -route_option 1
set_option -gen_text_timing_rpt 1
{extra}
{'run syn' if os.environ.get('SYN_ONLY') else 'run all'}
'''
    open(os.path.join(d, 'build.tcl'), 'w').write(tcl)
    return d


def run(names):
    for n in names:                       # nejdriv vsechny pripravit (chyba ve variante nesmi nechat bezet jine)
        if any(x not in V and x not in TCLX for x in n.split('+')):
            sys.exit('neznama varianta ' + n)
    dirs = [(n, prepare(n)) for n in names]
    procs = []
    for n, d in dirs:
        log = open(os.path.join(d, 'run.log'), 'w')
        procs.append((n, time.time(), subprocess.Popen([GW_SH, os.path.join(d, 'build.tcl')], cwd=d, stdout=log, stderr=subprocess.STDOUT)))
    for n, t0, p in procs:
        p.wait()
        print('%-8s rc=%d %4.0f s' % (n, p.returncode, time.time() - t0), flush=True)


def parse(name):
    d = os.path.join(ABL, name)
    rpt = os.path.join(d, 'v3', 'impl', 'pnr', 'Counter_FPGA_v3.rpt.txt')
    if not os.path.exists(rpt):
        return None
    t = open(rpt, encoding='utf-8', errors='replace').read()
    def g(pat):
        m = re.search(pat, t)
        return m.group(1) if m else '?'
    r = dict(cls=g(r'CLS\s*\|\s*(\d+)/4320'), logic=g(r'Logic\s*\|\s*(\d+)/8640'), alu=g(r'(\d+) ALU'), lut=g(r'\((\d+) LUT'),
             reg=g(r'Register\s*\|\s*(\d+)/'), bsram=g(r'BSRAM\s*\|\s*(\d+)/'), dsp=g(r'ALU54D\s*\|\s*(\d+)'))
    lg = open(os.path.join(d, 'run.log'), encoding='utf-8', errors='replace').read()
    m = re.search(r'There are total (\d+) unrouted', lg)
    r['unr'] = m.group(1) if m else '0'
    trp = os.path.join(d, 'v3', 'impl', 'pnr', 'Counter_FPGA_v3.tr')
    r['setup'] = r['hold'] = r['fmax'] = '-'
    if os.path.exists(trp):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import fpga_release
        tr = fpga_release.parse_tr(open(trp, encoding='utf-8', errors='replace').read())
        r['setup'] = '%s' % tr.get('setup_ws'); r['hold'] = '%s' % tr.get('hold_ws'); r['fmax'] = '%s' % tr.get('fmax')
        r['viol'] = '%s/%s' % (tr.get('setup_viol'), tr.get('hold_viol'))
    return r


def table():
    if not os.path.isdir(ABL):
        return
    print('%-10s %5s %5s %5s %5s %5s %5s %4s %5s %7s %7s %7s' % ('varianta', 'CLS', 'logik', 'LUT', 'ALU', 'reg', 'BSRAM', 'DSP', 'unrt', 'setup', 'hold', 'Fmax'))
    for n in sorted(os.listdir(ABL)):
        r = parse(n)
        if r:
            print('%-10s %5s %5s %5s %5s %5s %5s %4s %5s %7s %7s %7s' % (n, r['cls'], r['logic'], r['lut'], r['alu'], r['reg'], r['bsram'], r['dsp'], r['unr'], r['setup'], r['hold'], r['fmax']))


if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] == 'list':
        print(' '.join(V))
    elif sys.argv[1] == 'run':
        run(sys.argv[2:])
    elif sys.argv[1] == 'table':
        table()
