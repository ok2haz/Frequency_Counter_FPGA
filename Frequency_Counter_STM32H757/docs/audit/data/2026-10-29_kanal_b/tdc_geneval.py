"""(2026-10-31) Vyhodnoceni testu TDC s ujizdejici fazi JEDNIM prikazem (postup v docs/MERENI_A_KALIBRACE.md).

Adresar behu (tools/ab_night.py) se NEMENI: zkopiruje se do <out> (vychozi noc_logs/_rozbor/<jmeno behu>) a vse bezi nad kopii.
Registry generatoru (P1 P2 P3 R kanalu A = CLK2/3 a B = CLK4/5) se vezmou z posledniho vypisu `gen` v uart.log (ab_night.py
ho od 2026-10-31 loguje v kazdem cyklu), jinak z --a / --b. Pak:
  ab_night.py --report, tdc_gensweep.py (perioda 10 ns), tdc_gensplit.py / tdc_genstab.py / tdc_gencyc.py (K 130),
  souhrnna tabulka a srovnani tvaru chyby TDC v ramci taktu (tdcref_A/B.npy) s referencnimi behy (--ref, vychozi
  noc_logs/_rozbor/gen1803 = F0a a noc_logs/_rozbor/noc0610 = APP_OSC, maji-li tdcref).
python -I tdc_geneval.py <adresar behu> [--out <adresar>] [--a P1 P2 P3 R] [--b P1 P2 P3 R] [--ref <adresar> ...] [--K 130]"""
import argparse, os, re, shutil, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
STM = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..'))
ROZ = os.path.join(STM, 'noc_logs', '_rozbor')
RE_GEN = re.compile(r'GEN CLK(\d)/\d: .*?\n\s+MS: R=(\d+) P1=(\d+) P2=(\d+) P3=(\d+)')


def gen_regs(log):
    """posledni registry dvojic CLK2/3 a CLK4/5 z vypisu `gen` -> {2: (p1, p2, p3, r), 4: (...)}"""
    out = {}
    if os.path.exists(log):
        for m in RE_GEN.finditer(open(log, encoding='utf-8', errors='replace').read()):
            out[int(m.group(1))] = (int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(2)))
    return out


def run(args, title):
    print('\n==== %s' % title, flush=True)
    r = subprocess.run([sys.executable, '-I'] + args, capture_output=True, text=True, encoding='utf-8', errors='replace')
    out = r.stdout + (r.stderr if r.returncode else '')
    print(out.rstrip(), flush=True)
    return out


def best_corr(x, y):
    n = len(x)
    c, s = max(((np.corrcoef(x, np.roll(y, s))[0, 1], s) for s in range(n)))
    return c, s * 10000.0 / n, (x - np.roll(y, s)).std()


ap = argparse.ArgumentParser()
ap.add_argument('run')
ap.add_argument('--out')
ap.add_argument('--a', nargs=4, type=int, metavar=('P1', 'P2', 'P3', 'R'))
ap.add_argument('--b', nargs=4, type=int, metavar=('P1', 'P2', 'P3', 'R'))
ap.add_argument('--ref', nargs='*', default=[os.path.join(ROZ, 'gen1803'), os.path.join(ROZ, 'noc0610')])
ap.add_argument('--K', type=int, default=130)
ap.add_argument('--jen-registry', action='store_true', help='jen vypsat registry generatoru z <beh>/uart.log a skoncit')
a = ap.parse_args()

src = os.path.abspath(a.run)
if a.jen_registry:
    print(gen_regs(os.path.join(src, 'uart.log')))
    sys.exit(0)
out = os.path.abspath(a.out) if a.out else os.path.join(ROZ, os.path.basename(src.rstrip('/\\')))
if out == src:
    sys.exit('vystupni adresar nesmi byt adresar behu')
os.makedirs(out, exist_ok=True)
for f in ('ab.csv', 'uart.log', 'cykly.csv', 'udalosti.csv', 'x0_velke.csv'):
    if os.path.exists(os.path.join(src, f)):
        shutil.copy2(os.path.join(src, f), os.path.join(out, f))
csv = os.path.join(out, 'ab.csv')
regs = gen_regs(os.path.join(out, 'uart.log'))
ra = tuple(a.a) if a.a else regs.get(2)
rb = tuple(a.b) if a.b else regs.get(4)
if ra is None:
    sys.exit('registry generatoru A nejsou v uart.log (`gen`), zadej --a P1 P2 P3 R')
if rb is None:
    rb = ra
print('beh %s -> kopie %s\nregistry A (CLK2/3): P1 %d P2 %d P3 %d R %d | B (CLK4/5): P1 %d P2 %d P3 %d R %d' % ((src, out) + ra + rb))
ab = ['--a'] + [str(x) for x in ra] + (['--b'] + [str(x) for x in rb] if rb != ra else [])
pos = [str(x) for x in ra]                     # tdc_gensplit / genstab / gencyc berou jen registry A (B stejne)
if rb != ra:
    print('POZOR: B ma jine registry nez A -- gensplit/genstab/gencyc pocitaji s registry A pro oba kanaly')

rep = run([os.path.join(STM, 'tools', 'ab_night.py'), '--report', out], 'report (ab_night.py)')
sw = run([os.path.join(HERE, 'tdc_gensweep.py'), csv] + ab, 'chyba okna, perioda faze 10 ns (tdc_gensweep.py)')
sp = run([os.path.join(HERE, 'tdc_gensplit.py'), csv] + pos + [str(a.K)], 'rozklad 10 ns / 100 ns, K %d (tdc_gensplit.py)' % a.K)
st = run([os.path.join(HERE, 'tdc_genstab.py'), csv] + pos + [str(a.K), '4'], 'stalost po blocich, K %d (tdc_genstab.py)' % a.K)
cy = run([os.path.join(HERE, 'tdc_gencyc.py'), csv] + pos + [str(a.K)], 'takty po 10 ns, K %d (tdc_gencyc.py)' % a.K)

# ---- souhrn ----
print('\n==== SOUHRN')
m = re.search(r'oken (\d+) za ([\d.]+) h, median A-B ([-\d.]+) ps, sigma\(MAD\) ([\d.]+) ps', rep)
if m:
    print('oken %s za %s h, sigma(A-B) MAD %s ps' % (m.group(1), m.group(2), m.group(4)))
for ch in ('A', 'B'):
    s = re.search(r'%s: slozka 10 ns p-p (\d+) / rms ([\d.]+) ps, slozka 100 ns p-p (\d+) / rms ([\d.]+) ps' % ch, sp)
    z = re.findall(r'^  %s\s+%d\s+([-\d.]+)\s+([\d.]+) -> +([\d.]+)\s+([-+\d.]+) \(([-+\de.]+)\)' % (ch, 10), sw, re.M)
    t = re.search(r'%s: blok.*?tvar prvni vs posledni blok: korelace slozky 10 ns ([-+\d.]+), slozky 100 ns ([-+\d.]+)' % ch, st, re.S)
    print('%s: slozka 10 ns rms %s ps, 100 ns rms %s ps | chyba okna (P 10 ns) %s -> %s ps, stredni %s | stalost 10 ns %s, 100 ns %s' % (
        ch, s.group(2) if s else '?', s.group(4) if s else '?', z[0][1] if z else '?', z[0][2] if z else '?',
        z[0][4] if z else '?', t.group(1) if t else '?', t.group(2) if t else '?'))
for ch in ('A', 'B'):
    p = os.path.join(out, 'tdcref_%s.npy' % ch)
    if not os.path.exists(p):
        continue
    x = np.load(p)
    for r in a.ref:
        q = os.path.join(r, 'tdcref_%s.npy' % ch)
        if os.path.abspath(r) == out or not os.path.exists(q):
            continue
        y = np.load(q)
        if len(y) != len(x):
            continue
        c, sh, d = best_corr(x, y)
        print('%s: tvar TDC v ramci taktu rms %.1f ps proti %s (rms %.1f): korelace %+.2f (posun %.2f ns), rms rozdilu %.1f ps' % (
            ch, x.std(), os.path.basename(r.rstrip('/\\')), y.std(), c, sh / 1000.0, d))
