"""Brana sestaveni FPGA: sestavi (volitelne), zkontroluje a teprve pak vyda bitstream do ab_test/.

Proc (STATUS #282/#283): "FPGA se nekdy nahraje spatne" mela vic pricin a zadna z nich se neda poznat
z desky, kdyz nevis, CO jsi nahral. Dva ruzne bitstreamy hlasily stejne FW 0x041D; sestaveni v Gowin IDE
nepouziva volby z build.tcl (L-0122); po selhanem behu zustaly stare vystupy (L-0132); a cesty s rezervou
pod ~1 ns meri na krzemiku spatne (L-0138, L-0141). Tento skript to hlida najednou a do ab_test/ pusti jen
bitstream, ktery projde, s jednoznacnym jmenem a radkem v ab_test/MANIFEST.md.

Pouziti (z adresare Frequency_Counter_FPGA_Module):
    python tools/fpga_release.py --build          # gw_sh build.tcl + kontroly + vydani
    python tools/fpga_release.py                  # jen kontroly nad impl/pnr (bez noveho sestaveni)
    python tools/fpga_release.py --no-copy        # kontroly bez vydani
    python tools/fpga_release.py --v3 --build     # totez pro bitstream protokolu v3 (gw_sh v3/build_v3.tcl)
    python tools/fpga_release.py --v3 [--no-copy] # jen kontroly nad v3/impl/pnr (s vydanim / bez)
    python tools/fpga_release.py --selftest       # overi parsovani reportu a profil v3 na syntetickych datech

Protokol v3 (--v3; top_v3.v, FW 0x05xx, docs/PROTOKOL_V3.md): vystupy v3/impl/pnr a v3/impl/gwsynthesis, SDC
src/timing_v3.sdc, top modul top_v3, FW ze src/app_v3.v (navic kontrola 0x0500..0x05FF), sestaveni v3/build_v3.tcl.
Meze a ostatni kontroly jsou stejne jako u v2. Vydava ab_test/FW_0x0500_v3_<cas>_<git>.fs, v MANIFEST.md ma sloupec FW
"0x0500 (v3)" (v2 dal FW_0x0427_<cas>_<git>.fs a "0x0427"). Jmeno vystupu P&R v3 je Counter_FPGA.* (build_v3.tcl,
pnr_sweep.tcl), nebo Counter_FPGA_v3.*, kdyz ho Gowin odvodi od projektu -- pred prvnim P&R neovereno, brana prijme
obe a radek "Kontrola bitstreamu" ukaze, ktere nasla; obe naraz = FAIL (starsi smazat).

Navratovy kod: 0 = vse PASS (a vydano), 1 = aspon jedna kontrola FAIL (nic se nevydava).
"""
import argparse, datetime, hashlib, os, re, shutil, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GW_SH = r'C:\Gowin\Gowin_V1.9.12_x64\IDE\bin\gw_sh.exe'

# Meze (zdroj v komentari). Rezerva casovani je UZ PO odecteni nejistoty hodin z timing.sdc.
MIN_SETUP_NS = 0.0        # zaporna rezerva = cesta nesplnena
WARN_SETUP_NS = 0.2       # L-0134: pod 0,2 ns jen varovani
MIN_HOLD_NS = 0.0
MIN_UNC_NS = 0.9          # L-0141: s nejistotou 0,5 ns meril build na desce spatne
MAX_CLS_PCT = 85          # od FW 0x0421 (RTL pro 8 ns) ma navrh 83-85 %; do 0x0420 platilo 82 (tehdy P&R 83 % = TNS -91 ns pri 100 MHz)
WARN_CLS_PCT = 82         # L-0134
MAX_REG_PCT = 80          # L-0119
FS_ID_MAX_AGE_S = 3600    # identita v build_id.vh nesmi byt o hodinu starsi nez bitstream (IDE build)
V3_FW = (0x0500, 0x05FF)  # protokol v3 = FW 0x05xx (PROTOKOL_V3.md), v2 = 0x04xx


def profile(v3=False, root=ROOT):
    """Cesty a jmena jedne varianty bitstreamu: v2 (top.v, vychozi) nebo v3 (top_v3.v, --v3). root = adresar FPGA modulu."""
    if not v3:
        return dict(ver='v2', root=root, impl=os.path.join(root, 'impl'), tcl='build.tcl', top='top',
                    sdc='timing.sdc', app='spi_app.v', bases=('Counter_FPGA',), tag='', fw=None)
    # Jmeno vystupu P&R dava Gowin podle projektu (Counter_FPGA_v3.gprj) nebo volby output_base_name; build_v3.tcl
    # i pnr_sweep.tcl pocitaji s Counter_FPGA.*. Pred prvnim P&R v3 neovereno -> brana prijme obe jmena, ale ne obe naraz.
    return dict(ver='v3', root=root, impl=os.path.join(root, 'v3', 'impl'), tcl='v3/build_v3.tcl', top='top_v3',
                sdc='timing_v3.sdc', app='app_v3.v', bases=('Counter_FPGA', 'Counter_FPGA_v3'), tag='_v3', fw=V3_FW)


def read(p):
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()


# ---------------------------------------------------------------- parsovani reportu
def parse_tr(txt):
    """Z textoveho timing reportu: pocty poruseni, Fmax clk_p0_100m, nejhorsi setup/hold rezerva."""
    r = {}
    m = re.search(r'<Numbers of Setup Violated Endpoints>:(\d+)', txt)
    r['setup_viol'] = int(m.group(1)) if m else None
    m = re.search(r'<Numbers of Hold Violated Endpoints>:(\d+)', txt)
    r['hold_viol'] = int(m.group(1)) if m else None
    m = re.search(r'clk_p0_100m\s+[\d.]+\(MHz\)\s+([\d.]+)\(MHz\)', txt)
    r['fmax'] = float(m.group(1)) if m else None
    # prvni radek tabulky po "Setup Paths Table[1]" resp. "3.1.2 Hold Paths Table" = nejhorsi cesta
    def first_slack(after):
        # nadpis je v reportu dvakrat (obsah na zacatku + vlastni tabulka): vezmi prvni vyskyt, za kterym
        # do par radku nasleduje radek cesty "1  <rezerva>  <od>  <do>"
        for mi in re.finditer(re.escape(after), txt):
            for line in txt[mi.end():].splitlines()[1:6]:
                mm = re.match(r'\s+1\s+(-?[\d.]+)\s+(\S+)\s+(\S+)', line)
                if mm: return float(mm.group(1)), '%s -> %s' % (mm.group(2), mm.group(3))
        return None, None
    r['setup_ws'], r['setup_path'] = first_slack('Setup Paths Table[1]')
    r['hold_ws'], r['hold_path'] = first_slack('3.1.2 Hold Paths Table')
    return r


def parse_rpt(txt):
    r = {}
    m = re.search(r'CLS\s*\|\s*(\d+)/(\d+)\s*\|\s*(\d+)%', txt)
    r['cls'] = int(m.group(3)) if m else None
    m = re.search(r'(?m)^\s*Register\s*\|\s*(\d+)/(\d+)\s*\|\s*(\d+)%', txt)
    r['reg'] = int(m.group(3)) if m else None
    return r


def parse_cfg(txt):
    return {k: v for k, v in re.findall(r'set (\w+) regular_io = (\w+)', txt)}


def parse_unc(sdc):
    """Nejistota setup pro clk_p0_100m z timing.sdc (radky s // jsou komentar)."""
    for line in sdc.splitlines():
        s = line.strip()
        if s.startswith('//') or 'set_clock_uncertainty' not in s or 'clk_p0_100m' not in s: continue
        m = re.search(r'([\d.]+)\s*$', s)
        if m and '-setup' in s: return float(m.group(1))
    return None


def parse_period(sdc):
    """Perioda [ns] referencnich hodin FPGA (create_clock clk_p0_100m -period X; jmeno je historicke)."""
    for line in sdc.splitlines():
        s = line.strip()
        if s.startswith('//') or 'create_clock' not in s or 'clk_p0_100m' not in s: continue
        m = re.search(r'-period\s+([\d.]+)', s)
        if m: return float(m.group(1))
    return None


def parse_build_id(txt):
    t = re.search(r"BLD_TIME\s*=\s*32'h([0-9A-Fa-f]{8})", txt)
    g = re.search(r"BLD_GIT\s*=\s*32'h([0-9A-Fa-f]{8})", txt)
    return (int(t.group(1), 16) if t else None, int(g.group(1), 16) if g else None)


def parse_fw(txt):
    m = re.search(r"FW_VERSION\s*=\s*16'h([0-9A-Fa-f]{4})", txt)
    return int(m.group(1), 16) if m else None


# ---------------------------------------------------------------- kontroly
class Res:
    def __init__(self): self.rows = []; self.fail = 0
    def add(self, ok, name, detail, warn=False):
        tag = 'PASS' if ok and not warn else ('WARN' if ok else 'FAIL')
        if not ok: self.fail += 1
        self.rows.append((tag, name, detail))
    def show(self):
        for t, n, d in self.rows: print('  %-4s  %-26s %s' % (t, n, d))


def check(t_start=None, P=None, max_cls=None):
    P = P or profile()
    max_cls = MAX_CLS_PCT if max_cls is None else max_cls
    R = Res()
    root, pnr = P['root'], os.path.join(P['impl'], 'pnr')
    hit = [b for b in P['bases'] if os.path.exists(os.path.join(pnr, b + '.fs'))]
    base = hit[0] if hit else P['bases'][0]
    fs = os.path.join(pnr, base + '.fs')
    tr = os.path.join(pnr, base + '.tr')
    rpt = os.path.join(pnr, base + '.rpt.txt')
    cfg = os.path.join(pnr, 'device.cfg')
    info = {'fs': fs}
    if len(hit) > 1:
        R.add(False, 'vystup jednoznacny',
              '%s: %s -- starsi smazat' % (os.path.relpath(pnr, root), ' i '.join(b + '.fs' for b in hit)))
        return R, info
    for p in (fs, tr, rpt, cfg):
        if not os.path.exists(p):
            alt = ''.join('ani %s.fs; ' % b for b in P['bases'][1:]) if p == fs else ''
            R.add(False, 'vystup existuje', '%s CHYBI (%ssestaveni selhalo?)' % (os.path.relpath(p, root), alt))
            return R, info
    if t_start is not None:
        old = [os.path.basename(p) for p in (fs, tr, rpt, cfg) if os.path.getmtime(p) < t_start]
        R.add(not old, 'vystupy cerstve (L-0132)', 'ok' if not old else 'STARE: ' + ', '.join(old))

    t = parse_tr(read(tr)); info.update(t)
    R.add(t['setup_viol'] == 0, 'setup bez poruseni', '%s koncovych bodu' % t['setup_viol'])
    R.add(t['hold_viol'] == 0, 'hold bez poruseni', '%s koncovych bodu' % t['hold_viol'])
    ws = t['setup_ws']
    R.add(ws is not None and ws >= MIN_SETUP_NS, 'nejhorsi setup rezerva',
          '%s ns  (%s)' % (ws, t['setup_path']), warn=(ws is not None and ws < WARN_SETUP_NS))
    R.add(t['hold_ws'] is not None and t['hold_ws'] >= MIN_HOLD_NS, 'nejhorsi hold rezerva', '%s ns' % t['hold_ws'])
    sdc = read(os.path.join(root, 'src', P['sdc']))
    per = parse_period(sdc)
    fmin = None if per is None else 1000.0 / per
    R.add(t['fmax'] is not None and fmin is not None and t['fmax'] >= fmin, 'Fmax clk_p0_100m',
          '%s MHz (s nejistotou), hodiny %s MHz' % (t['fmax'], None if fmin is None else round(fmin, 3)))

    unc = parse_unc(sdc); info['unc'] = unc
    R.add(unc is not None and unc >= MIN_UNC_NS, 'nejistota hodin (L-0141)', '%s ns, min %.1f' % (unc, MIN_UNC_NS))

    p = parse_rpt(read(rpt)); info.update(p)
    R.add(p['cls'] is not None and p['cls'] <= max_cls, 'CLS' if max_cls == MAX_CLS_PCT else 'CLS (TEST: mez %d %%)' % max_cls,
          '%s %%' % p['cls'],
          warn=(p['cls'] is not None and p['cls'] > WARN_CLS_PCT))
    R.add(p['reg'] is not None and p['reg'] <= MAX_REG_PCT, 'registry (L-0119)', '%s %%' % p['reg'])

    c = parse_cfg(read(cfg))
    ok = c.get('SSPI') == 'true' and c.get('MSPI') == 'true' and c.get('JTAG') == 'false'
    R.add(ok, 'device.cfg piny (L-0122)', 'SSPI=%s MSPI=%s JTAG=%s' % (c.get('SSPI'), c.get('MSPI'), c.get('JTAG')))

    syn = os.path.join(P['impl'], 'gwsynthesis', base + '.log')
    top_ok = os.path.exists(syn) and ('Current top module is "%s"' % P['top']) in read(syn)
    R.add(top_ok, 'top modul = %s' % P['top'],
          'ok' if top_ok else 'syntéza vybrala jiny top (%s -top_module?)' % os.path.basename(P['tcl']))

    pl = subprocess.run([sys.executable, os.path.join(ROOT, 'sim', 'check_tdc_placement.py'), tr],
                        capture_output=True, text=True)
    last = (pl.stdout.strip().splitlines() or ['?'])[-1]
    R.add(pl.returncode == 0 and last.startswith('PASS'), 'rozmisteni TDC (L-0140)', last)

    bid = os.path.join(root, 'src', 'build_id.vh')
    bt, bg = parse_build_id(read(bid)) if os.path.exists(bid) else (None, None)
    info['build_time'], info['build_git'] = bt, bg
    age = None if bt is None else os.path.getmtime(fs) - bt
    R.add(bt is not None and age is not None and -60 <= age <= FS_ID_MAX_AGE_S, 'identita odpovida .fs',
          'build_id %s, .fs o %s s pozdeji' % (None if bt is None else datetime.datetime.fromtimestamp(bt, datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC'),
                                                None if age is None else int(age)))
    if bg is not None:
        R.add(True, 'git zdroju', '%07x%s' % (bg & 0x0FFFFFFF, ' + NEULOZENE ZMENY' if bg >> 31 else ''),
              warn=bool(bg >> 31))
    fw = info['fw'] = parse_fw(read(os.path.join(root, 'src', P['app'])))
    if P['fw']:
        R.add(fw is not None and P['fw'][0] <= fw <= P['fw'][1], 'FW protokolu %s' % P['ver'],
              '%s v src/%s, ocekavano 0x%04X..0x%04X'
              % (None if fw is None else '0x%04X' % fw, P['app'], P['fw'][0], P['fw'][1]))
    info['md5'] = hashlib.md5(open(fs, 'rb').read()).hexdigest()
    return R, info


def release(info, P=None, test_cls=None):
    """test_cls = mez CLS zkusebniho bitstreamu (--test-cls): jmeno TEST_..., v MANIFEST.md 'TEST, ne vydani'."""
    P = P or profile()
    bt, bg, fw = info['build_time'], info['build_git'], info['fw']
    stamp = datetime.datetime.fromtimestamp(bt, datetime.timezone.utc).strftime('%Y%m%d-%H%M')
    name = '%sFW_0x%04X%s_%s_%07x%s.fs' % ('TEST_' if test_cls else '', fw, P['tag'], stamp, bg & 0x0FFFFFFF,
                                          'd' if bg >> 31 else '')
    dst_dir = os.path.join(P['root'], 'ab_test'); os.makedirs(dst_dir, exist_ok=True)
    dst = os.path.join(dst_dir, name)
    shutil.copyfile(info['fs'], dst)
    man = os.path.join(dst_dir, 'MANIFEST.md')
    new = not os.path.exists(man)
    with open(man, 'a', encoding='utf-8') as f:
        if new:
            f.write('# Vydane bitstreamy (zapisuje tools/fpga_release.py)\n\n'
                    '| soubor | md5 | FW | sestaveno (UTC) | git | CLS | reg | setup ws [ns] | hold ws [ns] | Fmax | nejistota |\n'
                    '|---|---|---|---|---|---|---|---|---|---|---|\n')
        f.write('| %s | %s | 0x%04X%s | %s | %07x%s | %s %% | %s %% | %.3f | %.3f | %.2f | %.1f |\n' % (
            name, info['md5'], fw, (' (%s)' % P['ver'] if P['tag'] else '') +
            (' TEST (mez CLS %d %%), ne vydani' % test_cls if test_cls else ''),
            datetime.datetime.fromtimestamp(bt, datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
            bg & 0x0FFFFFFF, '+' if bg >> 31 else '', info['cls'], info['reg'], info['setup_ws'],
            info['hold_ws'], info['fmax'], info['unc']))
    return dst


# ---------------------------------------------------------------- selftest nastroje
def selftest():
    tr = ('<Numbers of Setup Violated Endpoints>:0\n<Numbers of Hold Violated Endpoints>:2\n'
          '  2     clk_p0_100m   100.000(MHz)   100.769(MHz)   5       TOP\n'
          '3.1.1.1 Setup Paths Table[1]\n hlavicka\n  1             0.076        a/Q   b/D   x\n'
          '3.1.2 Hold Paths Table\n hlavicka\n  1             0.162        c/Q   d/D   x\n')
    t = parse_tr(tr)
    ok = (t['setup_viol'] == 0 and t['hold_viol'] == 2 and t['fmax'] == 100.769
          and t['setup_ws'] == 0.076 and t['setup_path'] == 'a/Q -> b/D' and t['hold_ws'] == 0.162)
    p = parse_rpt('  Register                    | 3935/6693        |  59%\n  CLS            | 3398/4320    |  79%\n')
    ok &= (p['cls'] == 79 and p['reg'] == 59)
    ok &= parse_cfg('set SSPI regular_io = true\nset JTAG regular_io = false\n') == {'SSPI': 'true', 'JTAG': 'false'}
    sdc = ('// set_clock_uncertainty -setup -from [get_clocks {clk_p0_100m}] 0.5\n'
           'set_clock_uncertainty -setup -from [get_clocks {clk_p0_100m}] -to [get_clocks {clk_p0_100m}] 0.9\n')
    ok &= parse_unc(sdc) == 0.9
    ok &= parse_build_id("localparam [31:0] BLD_TIME = 32'h6ACA21F9;\nlocalparam [31:0] BLD_GIT  = 32'h8D526C00;") \
        == (0x6ACA21F9, 0x8D526C00)
    ok &= parse_fw("localparam [15:0] FW_VERSION      = 16'h041F;") == 0x041F
    ok &= parse_period('// create_clock -name clk_p0_100m -period 8.0\ncreate_clock -name clk_p0_100m   -period 10.0  -waveform {0 5.0}  [get_ports {clk_p0_100m}]') == 10.0
    print('selftest fpga_release: %s' % ('PASS' if ok else 'FAIL'))
    ok3 = selftest_v3()
    return 0 if ok and ok3 else 1


def selftest_v3():
    """Profil v3 na syntetickem stromu v docasnem adresari. Vedle lezi strom v2 s jinymi hodnotami: v3 musi cist jen
    v3/impl, timing_v3.sdc, app_v3.v a top_v3 (v2 naopak jen svoje), vydani a radek MANIFEST nesou v3, chyby v3 padaji."""
    bad = []
    def expect(cond, what):
        if not cond: bad.append(what)
    def put(rel, txt):
        p = os.path.join(tmp, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w', encoding='utf-8') as f: f.write(txt)
    def tree(ver, base, ws, cls, unc, fw, top):
        impl = 'impl' if ver == 'v2' else 'v3/impl'
        put('src/' + ('timing.sdc' if ver == 'v2' else 'timing_v3.sdc'), sdc % unc)
        put('src/' + ('spi_app.v' if ver == 'v2' else 'app_v3.v'), "localparam [15:0] FW_VERSION = 16'h%04X;\n" % fw)
        put('%s/pnr/%s.fs' % (impl, base), 'bitstream %s %s\n' % (ver, base))
        put('%s/pnr/%s.tr' % (impl, base), tr % ws + tdc)
        put('%s/pnr/%s.rpt.txt' % (impl, base), '  Register   | 3935/6693  |  60%%\n  CLS   | 3398/4320  |  %d%%\n' % cls)
        put('%s/pnr/device.cfg' % impl, 'set SSPI regular_io = true\nset MSPI regular_io = true\nset JTAG regular_io = false\n')
        put('%s/gwsynthesis/%s.log' % (impl, base), 'Current top module is "%s"\n' % top)
    def fails(P):
        R, info = check(None, P)
        return [n for t, n, d in R.rows if t == 'FAIL'], info

    sdc = ('// set_clock_uncertainty -setup -from [get_clocks {clk_p0_100m}] -to [get_clocks {clk_p0_100m}] 0.5\n'
           'create_clock -name clk_p0_100m   -period 10.0  -waveform {0 5.0}   [get_ports {clk_p0_100m}]\n'
           'set_clock_uncertainty -setup -from [get_clocks {clk_p0_100m}] -to [get_clocks {clk_p0_100m}] %s\n')
    tr = ('<Numbers of Setup Violated Endpoints>:0\n<Numbers of Hold Violated Endpoints>:0\n'
          '  2     clk_p0_100m   100.000(MHz)   104.000(MHz)   5       TOP\n'
          '3.1.1.1 Setup Paths Table[1]\n hlavicka\n  1             %s        a/Q   b/D   x\n'
          '3.1.2 Hold Paths Table\n hlavicka\n  1             0.300        c/Q   d/D   x\n')
    # vzorkovaci FF obou retezu TDC tak, jak je chce sim/check_tdc_placement.py (radky 26/27, tap k = slot 13 + k)
    tdc = ''.join('  R%dC%d[%d][%s]  %s/u_chain/q_r_%d_s0/CLK\n'
                  % (row, (13 + k) // 6, (13 + k) % 6 // 2, 'AB'[(13 + k) % 2], ch, k)
                  for ch, row in (('u_tdca', 26), ('u_tdcb', 27)) for k in range(268))
    tmp = tempfile.mkdtemp(prefix='fpga_release_v3_')
    try:
        bt = int(time.time()) - 120
        put('src/build_id.vh', "localparam [31:0] BLD_TIME = 32'h%08X;\nlocalparam [31:0] BLD_GIT  = 32'h08F9E685;\n" % bt)
        tree('v2', 'Counter_FPGA', '0.215', 79, '1.0', 0x0427, 'top')
        tree('v3', 'Counter_FPGA_v3', '0.512', 77, '0.9', 0x0500, 'top_v3')
        P2, P3 = profile(False, tmp), profile(True, tmp)
        f3, i3 = fails(P3)
        expect(f3 == [] and i3['unc'] == 0.9 and i3['fw'] == 0x0500 and i3['cls'] == 77 and i3['setup_ws'] == 0.512
               and os.path.basename(i3['fs']) == 'Counter_FPGA_v3.fs', 'v3 PASS ze souboru v3 %s' % f3)
        f2, i2 = fails(P2)
        expect(f2 == [] and i2['unc'] == 1.0 and i2['fw'] == 0x0427 and i2['cls'] == 79 and i2['setup_ws'] == 0.215,
               'v2 PASS ze souboru v2 %s' % f2)
        stamp = datetime.datetime.fromtimestamp(bt, datetime.timezone.utc).strftime('%Y%m%d-%H%M')
        n3, n2 = os.path.basename(release(i3, P3)), os.path.basename(release(i2, P2))
        expect(n3 == 'FW_0x0500_v3_%s_8f9e685.fs' % stamp and n2 == 'FW_0x0427_%s_8f9e685.fs' % stamp, 'jmena %s %s' % (n3, n2))
        man = read(os.path.join(tmp, 'ab_test', 'MANIFEST.md'))
        expect(('| %s | ' % n3) in man and '| 0x0500 (v3) |' in man and ('| %s | ' % n2) in man and '| 0x0427 |' in man,
               'radky MANIFEST')
        expect(read(os.path.join(tmp, 'ab_test', n3)) == 'bitstream v3 Counter_FPGA_v3\n', 'obsah vydaneho v3')

        put('v3/impl/gwsynthesis/Counter_FPGA_v3.log', 'Current top module is "top"\n')
        expect(fails(P3)[0] == ['top modul = top_v3'], 'v3 se syntezou top')
        put('v3/impl/gwsynthesis/Counter_FPGA_v3.log', 'Current top module is "top_v3"\n')
        put('src/app_v3.v', "localparam [15:0] FW_VERSION = 16'h0427;\n")
        expect(fails(P3)[0] == ['FW protokolu v3'], 'v3 s FW 0x0427')
        put('src/app_v3.v', "localparam [15:0] FW_VERSION = 16'h0501;\n")
        put('src/timing_v3.sdc', sdc % '0.5')
        expect(fails(P3)[0] == ['nejistota hodin (L-0141)'], 'v3 s nejistotou 0,5 ns')
        put('src/timing_v3.sdc', sdc % '0.9')
        # druhe mozne jmeno vystupu: samo projde, obe naraz = nejednoznacne, zadne = chybi
        tree('v3', 'Counter_FPGA', '0.333', 76, '0.9', 0x0501, 'top_v3')
        expect(fails(P3)[0] == ['vystup jednoznacny'], 'v3 s obema jmeny')
        os.remove(os.path.join(tmp, 'v3', 'impl', 'pnr', 'Counter_FPGA_v3.fs'))
        f, i = fails(P3)
        expect(f == [] and os.path.basename(i['fs']) == 'Counter_FPGA.fs' and i['setup_ws'] == 0.333 and i['fw'] == 0x0501,
               'v3 jako Counter_FPGA %s' % f)
        os.remove(os.path.join(tmp, 'v3', 'impl', 'pnr', 'Counter_FPGA.fs'))
        expect(fails(P3)[0] == ['vystup existuje'], 'v3 bez .fs')
    except Exception as e:
        bad.append('vyjimka %s: %s' % (type(e).__name__, e))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print('selftest fpga_release v3: %s%s' % ('PASS' if not bad else 'FAIL: ', '; '.join(bad)))
    return not bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', action='store_true', help='nejdriv spustit gw_sh build.tcl (s --v3 v3/build_v3.tcl)')
    ap.add_argument('--no-copy', action='store_true', help='jen kontroly, nic nevydat do ab_test/')
    ap.add_argument('--v3', action='store_true', help='bitstream protokolu v3 (top_v3, vystupy v3/impl/)')
    ap.add_argument('--selftest', action='store_true')
    # 2026-10-24 (STATUS #334, rozhodnuti uzivatele): ZKUSEBNI bitstream pro desku pri CLS nad branou -- jen s jinou mezi CLS,
    # ostatni kontroly beze zmeny; vystup TEST_FW_..., v MANIFEST.md "TEST (mez CLS N %), ne vydani"
    ap.add_argument('--test-cls', type=int, default=None, metavar='N', help='zkusebni bitstream: mez CLS N %% (86..92)')
    # rezerva casovani kolisa +-1,5 ns podle BLD_TIME (L-0222) -> s --build az K sestaveni, dokud kontroly neprojdou
    ap.add_argument('--tries', type=int, default=1, metavar='K', help='s --build: nejvys K sestaveni do prvniho PASS')
    a = ap.parse_args()
    if a.selftest: return selftest()
    if a.test_cls is not None and not (MAX_CLS_PCT < a.test_cls <= 92):
        print('--test-cls mimo %d..92' % (MAX_CLS_PCT + 1)); return 1
    P = profile(a.v3)
    tries = max(1, a.tries) if a.build else 1
    for k in range(tries):
        t0 = None
        if a.build:
            t0 = time.time()
            print('gw_sh %s ... (sestaveni %d/%d)' % (P['tcl'], k + 1, tries), flush=True)
            r = subprocess.run([GW_SH, P['tcl']], cwd=ROOT, capture_output=True, text=True, errors='replace')
            log = os.path.join(P['impl'], 'build_release.log')
            os.makedirs(os.path.dirname(log), exist_ok=True)
            open(log, 'w', encoding='utf-8').write(r.stdout + r.stderr)
            if r.returncode != 0 or 'ERROR' in r.stdout:
                print('  FAIL  sestaveni: gw_sh rc=%d, viz %s' % (r.returncode, os.path.relpath(log, ROOT)))
                return 1
        R, info = check(t0, P, a.test_cls)
        print('Kontrola bitstreamu %s' % os.path.relpath(info['fs'], ROOT))
        R.show()
        if not R.fail:
            break
        print('VYSLEDEK sestaveni %d: FAIL (%d), setup ws %s ns' % (k + 1, R.fail, info.get('setup_ws')), flush=True)
    if R.fail:
        print('VYSLEDEK: FAIL (%d) -- bitstream se NEVYDAVA' % R.fail)
        return 1
    if a.no_copy:
        print('VYSLEDEK: PASS (bez vydani)')
        return 0
    dst = release(info, P, a.test_cls)
    print('VYSLEDEK: PASS -> %s (md5 %s), zapsano do ab_test/MANIFEST.md' % (os.path.relpath(dst, ROOT), info['md5']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
