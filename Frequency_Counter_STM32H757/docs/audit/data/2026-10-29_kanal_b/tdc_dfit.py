"""Test H1: A-B okna = LSQ kombinace D(faze) pres 10 segmentu, D = rozdil nelinearit TDC A a B,
periodicka v 10 ns (10 MHz je soudelne se 100 MHz -> vsechny hrany segmentu maji tutez fazi).

Faze se rekonstruuje kumulaci posunu fm za okno (fm = (fa+fb)/2). D se modeluje Fourierovou
radou o K harmonickych s periodou 10 ns; fit po blocich, trenink na sudych oknech, test na lichych.
"""
import csv, sys, math
import numpy as np

P = 10000.0          # perioda taktu [ps]
NSEG = 10
ci = 10.0 * (np.arange(NSEG) - 4.5) / 82.5        # vahy LSQ: chyba intervalu = sum ci * e_i

def load(p):
    seq, fa, fb, g, t = [], [], [], [], []
    for r in csv.DictReader(open(p)):
        if r['signal_lost'] != 'False':
            continue
        a = float(r['f_primary']); b = float(r['f_secondary'])
        if a > 0 and b > 0:
            seq.append(int(r['seq'])); fa.append(a); fb.append(b); g.append(float(r['gate_ns'])); t.append(float(r['t_unix']))
    return np.array(seq), np.array(fa), np.array(fb), np.array(g), np.array(t)

def mad(v):
    return 1.4826 * np.median(np.abs(v - np.median(v)))

seq, fa, fb, g, t = load(sys.argv[1])
ab = (fa - fb) / fa * g * 1e3
fm = (fa + fb) / 2
sweep = (fm - 1e7) / 1e7 * g * 1e3
# faze na zacatku okna; restart na dire v seq
phs = np.zeros_like(sweep)
for i in range(1, len(seq)):
    phs[i] = phs[i - 1] + sweep[i - 1] if seq[i] - seq[i - 1] == 1 else 0.0
frac = (np.arange(NSEG) + 0.5) / NSEG
phi = phs[:, None] + frac[None, :] * sweep[:, None]          # faze stredu segmentu

def design(idx, K):
    cols = [np.ones(len(idx))]
    for k in range(1, K + 1):
        a = 2 * math.pi * k / P * phi[idx]
        cols.append(np.cos(a) @ ci)
        cols.append(np.sin(a) @ ci)
    return np.column_stack(cols)

def run(block, K, verbose=False):
    n = len(ab)
    res_all = np.full(n, np.nan)
    r2s = []
    for b0 in range(0, n, block):
        idx = np.arange(b0, min(b0 + block, n))
        if len(idx) < block // 2:
            continue
        X = design(idx, K); y = ab[idx]
        tr = idx % 2 == 0; te = ~tr
        coef, *_ = np.linalg.lstsq(X[tr], y[tr], rcond=None)
        pred = X[te] @ coef
        base = y[te] - np.median(y[tr])
        r = y[te] - pred
        res_all[idx[te]] = r
        r2s.append(1 - np.var(r) / np.var(base))
    ok = ~np.isnan(res_all)
    return np.median(r2s), mad(ab[ok]), mad(res_all[ok]), res_all, ok

print('oken %d, sigma(A-B) %.1f ps' % (len(ab), mad(ab)))
print('\nfit D(faze) po blocich (test na lichych oknech, ktera fit nevidel):')
print('  blok[oken]  K   median R^2   sigma pred -> po [ps]')
for block in (600, 2400, 14400):
    for K in (3, 10, 20):
        r2, s0, s1, _, _ = run(block, K)
        print('  %6d    %3d   %6.2f      %5.1f -> %5.1f' % (block, K, r2, s0, s1))

# kontrola: zamichana faze (stejna rozdeleni sweep, nesouvisi s A-B)
rng = np.random.default_rng(1)
saved = phi.copy()
perm = rng.permutation(len(ab))
phi = saved[perm]
r2, s0, s1, _, _ = run(2400, 10)
print('  kontrola - faze zamichana mezi okny, blok 2400, K 10: R^2 %.2f, sigma %.1f -> %.1f' % (r2, s0, s1))
phi = saved

r2, s0, s1, res, ok = run(2400, 10)
print('\nsigma podle |sweep| pred a po odecteni D (blok 2400, K 10):')
edges = [0, 25, 50, 100, 200, 400, 800, 1600, 1e9]
for lo, hi in zip(edges, edges[1:]):
    m = ok & (np.abs(sweep) >= lo) & (np.abs(sweep) < hi)
    if m.sum() > 200:
        print('   |sweep| %5.0f-%-5.0f ps: oken %6d  %6.1f -> %5.1f ps' % (lo, min(hi, 99999), m.sum(), mad(ab[m]), mad(res[m])))
for th in (100, 200, 500):
    print('   |A-B| > %d ps: %d -> %d (z %d testovacich oken)' % (th, (np.abs(ab[ok] - np.median(ab)) > th).sum(), (np.abs(res[ok]) > th).sum(), ok.sum()))

# tvar D: fit z hodinovych bloku, zarovnane na spolecnou fazi neni mozne (neznamy pocatek), vypis amplitudy
print('\namplituda D (spicka-spicka) z fitu blok 2400 K 10, prvnich 8 bloku:')
grid = np.linspace(0, P, 200, endpoint=False)
for b0 in range(0, 8 * 2400, 2400):
    idx = np.arange(b0, b0 + 2400)
    X = design(idx, 10)
    coef, *_ = np.linalg.lstsq(X, ab[idx], rcond=None)
    D = sum(coef[2 * k - 1] * np.cos(2 * math.pi * k / P * grid) + coef[2 * k] * np.sin(2 * math.pi * k / P * grid) for k in range(1, 11))
    sp = np.abs(sweep[idx])
    print('   blok %3d: D p-p %6.1f ps, rms %5.1f ps, median |sweep| %5.0f ps, faze ujela %7.0f ps' % (
        b0 // 2400, D.max() - D.min(), D.std(), np.median(sp), phs[idx[-1]] - phs[idx[0]]))
