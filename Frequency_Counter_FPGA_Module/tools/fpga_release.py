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
    python tools/fpga_release.py --selftest       # overi parsovani reportu na syntetickych datech

Navratovy kod: 0 = vse PASS (a vydano), 1 = aspon jedna kontrola FAIL (nic se nevydava).
"""
import argparse, datetime, hashlib, os, re, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNR = os.path.join(ROOT, 'impl', 'pnr')
GW_SH = r'C:\Gowin\Gowin_V1.9.12_x64\IDE\bin\gw_sh.exe'

# Meze (zdroj v komentari). Rezerva casovani je UZ PO odecteni nejistoty hodin z timing.sdc.
MIN_SETUP_NS = 0.0        # zaporna rezerva = cesta nesplnena
WARN_SETUP_NS = 0.2       # L-0134: pod 0,2 ns jen varovani
MIN_HOLD_NS = 0.0
MIN_UNC_NS = 0.9          # L-0141: s nejistotou 0,5 ns meril build na desce spatne
MAX_CLS_PCT = 82          # nad tim uz P&R casovani nezvladne (P&R 2026-10-10: 83 % = TNS -91 ns)
WARN_CLS_PCT = 78         # L-0134
MAX_REG_PCT = 80          # L-0119
FS_ID_MAX_AGE_S = 3600    # identita v build_id.vh nesmi byt o hodinu starsi nez bitstream (IDE build)


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


def check(t_start=None):
    R = Res()
    fs = os.path.join(PNR, 'Counter_FPGA.fs')
    tr = os.path.join(PNR, 'Counter_FPGA.tr')
    rpt = os.path.join(PNR, 'Counter_FPGA.rpt.txt')
    cfg = os.path.join(PNR, 'device.cfg')
    info = {}
    for p in (fs, tr, rpt, cfg):
        if not os.path.exists(p):
            R.add(False, 'vystup existuje', '%s CHYBI (sestaveni selhalo?)' % os.path.relpath(p, ROOT))
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
    R.add(t['fmax'] is not None and t['fmax'] >= 100.0, 'Fmax clk_p0_100m', '%s MHz (s nejistotou)' % t['fmax'])

    unc = parse_unc(read(os.path.join(ROOT, 'src', 'timing.sdc'))); info['unc'] = unc
    R.add(unc is not None and unc >= MIN_UNC_NS, 'nejistota hodin (L-0141)', '%s ns, min %.1f' % (unc, MIN_UNC_NS))

    p = parse_rpt(read(rpt)); info.update(p)
    R.add(p['cls'] is not None and p['cls'] <= MAX_CLS_PCT, 'CLS', '%s %%' % p['cls'],
          warn=(p['cls'] is not None and p['cls'] > WARN_CLS_PCT))
    R.add(p['reg'] is not None and p['reg'] <= MAX_REG_PCT, 'registry (L-0119)', '%s %%' % p['reg'])

    c = parse_cfg(read(cfg))
    ok = c.get('SSPI') == 'true' and c.get('MSPI') == 'true' and c.get('JTAG') == 'false'
    R.add(ok, 'device.cfg piny (L-0122)', 'SSPI=%s MSPI=%s JTAG=%s' % (c.get('SSPI'), c.get('MSPI'), c.get('JTAG')))

    syn = os.path.join(ROOT, 'impl', 'gwsynthesis', 'Counter_FPGA.log')
    top_ok = os.path.exists(syn) and 'Current top module is "top"' in read(syn)
    R.add(top_ok, 'top modul = top', 'ok' if top_ok else 'syntéza vybrala jiny top (build.tcl -top_module?)')

    pl = subprocess.run([sys.executable, os.path.join(ROOT, 'sim', 'check_tdc_placement.py'), tr],
                        capture_output=True, text=True)
    last = (pl.stdout.strip().splitlines() or ['?'])[-1]
    R.add(pl.returncode == 0 and last.startswith('PASS'), 'rozmisteni TDC (L-0140)', last)

    bt, bg = parse_build_id(read(os.path.join(ROOT, 'src', 'build_id.vh'))) \
        if os.path.exists(os.path.join(ROOT, 'src', 'build_id.vh')) else (None, None)
    info['build_time'], info['build_git'] = bt, bg
    age = None if bt is None else os.path.getmtime(fs) - bt
    R.add(bt is not None and age is not None and -60 <= age <= FS_ID_MAX_AGE_S, 'identita odpovida .fs',
          'build_id %s, .fs o %s s pozdeji' % (None if bt is None else datetime.datetime.fromtimestamp(bt, datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC'),
                                                None if age is None else int(age)))
    if bg is not None:
        R.add(True, 'git zdroju', '%07x%s' % (bg & 0x0FFFFFFF, ' + NEULOZENE ZMENY' if bg >> 31 else ''),
              warn=bool(bg >> 31))
    info['fw'] = parse_fw(read(os.path.join(ROOT, 'src', 'spi_app.v')))
    info['md5'] = hashlib.md5(open(fs, 'rb').read()).hexdigest()
    return R, info


def release(info):
    bt, bg, fw = info['build_time'], info['build_git'], info['fw']
    stamp = datetime.datetime.fromtimestamp(bt, datetime.timezone.utc).strftime('%Y%m%d-%H%M')
    name = 'FW_0x%04X_%s_%07x%s.fs' % (fw, stamp, bg & 0x0FFFFFFF, 'd' if bg >> 31 else '')
    dst_dir = os.path.join(ROOT, 'ab_test'); os.makedirs(dst_dir, exist_ok=True)
    dst = os.path.join(dst_dir, name)
    shutil.copyfile(os.path.join(PNR, 'Counter_FPGA.fs'), dst)
    man = os.path.join(dst_dir, 'MANIFEST.md')
    new = not os.path.exists(man)
    with open(man, 'a', encoding='utf-8') as f:
        if new:
            f.write('# Vydane bitstreamy (zapisuje tools/fpga_release.py)\n\n'
                    '| soubor | md5 | FW | sestaveno (UTC) | git | CLS | reg | setup ws [ns] | hold ws [ns] | Fmax | nejistota |\n'
                    '|---|---|---|---|---|---|---|---|---|---|---|\n')
        f.write('| %s | %s | 0x%04X | %s | %07x%s | %s %% | %s %% | %.3f | %.3f | %.2f | %.1f |\n' % (
            name, info['md5'], fw, datetime.datetime.fromtimestamp(bt, datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
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
    print('selftest fpga_release: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', action='store_true', help='nejdriv spustit gw_sh build.tcl')
    ap.add_argument('--no-copy', action='store_true', help='jen kontroly, nic nevydat do ab_test/')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest: return selftest()
    t0 = None
    if a.build:
        t0 = time.time()
        print('gw_sh build.tcl ...')
        r = subprocess.run([GW_SH, 'build.tcl'], cwd=ROOT, capture_output=True, text=True, errors='replace')
        log = os.path.join(ROOT, 'impl', 'build_release.log')
        os.makedirs(os.path.dirname(log), exist_ok=True)
        open(log, 'w', encoding='utf-8').write(r.stdout + r.stderr)
        if r.returncode != 0 or 'ERROR' in r.stdout:
            print('  FAIL  sestaveni: gw_sh rc=%d, viz %s' % (r.returncode, os.path.relpath(log, ROOT)))
            return 1
    R, info = check(t0)
    print('Kontrola bitstreamu %s' % os.path.relpath(os.path.join(PNR, 'Counter_FPGA.fs'), ROOT))
    R.show()
    if R.fail:
        print('VYSLEDEK: FAIL (%d) -- bitstream se NEVYDAVA' % R.fail)
        return 1
    if a.no_copy:
        print('VYSLEDEK: PASS (bez vydani)')
        return 0
    dst = release(info)
    print('VYSLEDEK: PASS -> %s (md5 %s), zapsano do ab_test/MANIFEST.md' % (os.path.relpath(dst, ROOT), info['md5']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
