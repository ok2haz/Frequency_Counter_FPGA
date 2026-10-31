"""Test TDC s pomalu ujizdejici fazi (krok 0 navrhu kalibrace, 2026-10-30): chyba KAZDEHO kanalu zvlast.

Vstup A a B z generatoru Si5356 (`gen 2 10000000.040`, `gen 4 10000000.040`). Generator i takt FPGA 100 MHz
jsou ze tehoz VCO, takze skutecny pomer kmitoctu je PRESNE dany registry MultiSynthu (P1/P2/P3/R, vypis `gen`)
a nezavisi na OCXO. Odchylka namereneho kmitoctu kanalu od teto pravdy je cista chyba pristroje:

  e_w = (f_kanal - f_pravda) / f_pravda * gate   [ps]   = sum ci * E(faze stredu segmentu i)

Faze hran proti taktu se posouva o sweep = (f_pravda - f_nom) / f_nom * gate za okno (f_nom = 100 MHz / n, nejblizsi
soudelny kmitocet) a je PRESNE zname z cisla okna (seq), takze se nerekonstruuje kumulaci jako u GPSDO a nebloudi.
E(faze) se fituje Fourierovou radou s periodou 10 ns pres cely beh, trenink na nahodne polovine oken, test na zbytku.
Doporuceny kmitocet 10 000 000,041 Hz: posun 1025 ps za okno, faze segmentu na mrizce 2,5 ps, perioda projde za 2,4 s.
(Pri ,040 Hz je posun presne P/10 za okno a segmenty sedi jen na 100 polohach -- tvar jde urcit, ale hruberi.)

Pouziti:  python -I tdc_gensweep.py <ab.csv> --a P1 P2 P3 R [--b P1 P2 P3 R] [--K 10] [--out prefix]
  --b chybi = B ma tytez registry jako A. --out ulozi tvary E_A, E_B, E_A-E_B (400 bodu na periodu) do
  <prefix>_A.npy / _B.npy / _AB.npy (porovnani s tvarem D z behu s GPSDO: tdc_dcmp.py).
"""
import argparse, csv, math
from fractions import Fraction
import numpy as np

P = 10000.0                                   # perioda taktu [ps]
NSEG = 10
ci = 10.0 * (np.arange(NSEG) - 4.5) / 82.5    # vahy LSQ: chyba intervalu = sum ci * e_i (jako tdc_dfit.py)
G = 400
VCO = 2200000000

def f_true(p1, p2, p3, r):
    """Kmitocet vystupu v jednotkach taktu FPGA (VCO/22 = 100 MHz presne): 128*VCO / ((P1+512) + P2/P3) / R."""
    return Fraction(VCO * 128) * p3 / ((p1 + 512) * p3 + p2) / r

def mad(v):
    return 1.4826 * np.median(np.abs(v - np.median(v)))

ap = argparse.ArgumentParser()
ap.add_argument('csv')
ap.add_argument('--a', nargs=4, type=int, required=True, metavar=('P1', 'P2', 'P3', 'R'))
ap.add_argument('--b', nargs=4, type=int, metavar=('P1', 'P2', 'P3', 'R'))
ap.add_argument('--K', type=int, default=10)
ap.add_argument('--P', type=float, default=10000.0,
                help='perioda faze [ps]: 10000 = takt; 100000 = takt + poloha vuci 10 MHz referenci (nalez 2026-10-30, '
                     'pak --K 100..200; harmonicke k nasobek 10 jsou slozka periodicka v taktu)')
ap.add_argument('--out')
a = ap.parse_args()
P = a.P

seq, fa, fb, g = [], [], [], []
for r in csv.DictReader(open(a.csv)):
    if r['signal_lost'] != 'False':
        continue
    x, y = float(r['f_primary']), float(r['f_secondary'])
    if x > 0 and y > 0:
        seq.append(int(r['seq'])); fa.append(x); fb.append(y); g.append(float(r['gate_ns']))
seq = np.array(seq); fa = np.array(fa); fb = np.array(fb); g = np.array(g)

FA = f_true(*a.a)
FB = f_true(*(a.b if a.b else a.a))

def channel(name, fm, FT):
    n = round(Fraction(100000000) / FT)
    fnom = Fraction(100000000, n)
    rel = float((FT - fnom) / fnom)                       # relativni posun proti soudelnemu kmitoctu
    ft = float(FT)
    # Delka okna je pevna mrizka (K bunek po 25 ms); `gate_ns` jednotliveho okna je NAMERENA delka primarniho
    # kanalu a nese jeho vlastni chybu -> do faze ji nebrat (falesna korelace s chybou A).
    gate_ps = float(np.median(g)) * 1e3
    e = (fm - ft) / ft * gate_ps                          # chyba okna [ps]
    sweep = rel * gate_ps                                 # posun faze za okno [ps], konstantni
    phs = (seq - seq[0]) * sweep                          # faze zacatku okna, presne z cisla okna
    frac = (np.arange(NSEG) + 0.5) / NSEG
    phi = phs[:, None] + frac[None, :] * sweep
    return dict(name=name, e=e, phi=phi, ft=ft, n=n, rel=rel, sweep=sweep, gate_ps=gate_ps)

def design(phi, K):
    cols = [np.ones(len(phi))]
    for k in range(1, K + 1):
        w = 2 * math.pi * k / P * phi
        cols.append(np.cos(w) @ ci)
        cols.append(np.sin(w) @ ci)
    return np.column_stack(cols)

grid = np.linspace(0, P, G, endpoint=False)

def fit(y, phi, K):
    X = design(phi, K)
    # NAHODNE deleni trenink/test: pri posunu presne P/m za okno se faze opakuje po m oknech a deleni
    # sudy/lichy by dalo testu jine vzory faze, nez videl trenink (m sude).
    tr = np.random.default_rng(7).random(len(y)) < 0.5; te = ~tr
    coef, *_ = np.linalg.lstsq(X[tr], y[tr], rcond=None)
    res = y[te] - X[te] @ coef
    base = y[te] - np.mean(y[tr])
    r2 = 1 - np.var(res) / np.var(base)
    coef_all, *_ = np.linalg.lstsq(X, y, rcond=None)
    E = sum(coef_all[2 * k - 1] * np.cos(2 * math.pi * k / P * grid) + coef_all[2 * k] * np.sin(2 * math.pi * k / P * grid)
            for k in range(1, K + 1))
    return r2, mad(base), mad(res), coef_all[0], E

def blind(sweep, K):
    """Slepe harmonicke (2026-10-31): chyba okna na harmonickou k reaguje s vahou |H(k)| = |sum ci exp(j 2 pi k d i / P)|,
    d = posun faze mezi segmenty. Kde je |H| < 0,3, fit amplitudu neurci a muze tam dat cokoli, aniz by se zhorsila
    predpoved oken (pri 102,5 ps/segment a P 100 ns je H(140) = 0,001 a slabe jsou i k 1..4). Tvar a rms slozek
    s takovou harmonickou neplati -> rozklad delat s K pod ni (noc 2026-10-30: K 150 dal slozku 10 ns 105..163 ps
    podle bloku, K 130 stale 96 ps)."""
    k = np.arange(1, K + 1)
    H = np.abs(np.exp(2j * math.pi * np.outer(k, np.arange(NSEG)) * (sweep / NSEG) / P) @ ci)
    ks = list(k[H < 0.3])
    rng = []
    for x in ks:
        if rng and x == rng[-1][1] + 1:
            rng[-1][1] = x
        else:
            rng.append([x, x])
    return ', '.join(('%d' % r[0]) if r[0] == r[1] else ('%d..%d' % (r[0], r[1])) for r in rng)

A = channel('A', fa, FA)
B = channel('B', fb, FB)
print('oken %d, seq %d..%d (diry %d)' % (len(seq), seq[0], seq[-1], int(np.sum(np.diff(seq) != 1))))
for c in (A, B):
    span = abs(c['rel']) * (seq[-1] - seq[0]) * c['gate_ps'] / P
    print('%s: pravda %.6f Hz (100 MHz / %d %+.3e), posun faze %.0f ps/okno, beh pokryl %.0f period faze (%.0f ps)' % (
        c['name'], c['ft'], c['n'], c['rel'], c['sweep'], span, P))
    seg = abs(c['sweep']) / NSEG
    if seg > 2500.0:
        print('  !! %s: posun za segment %.0f ps > 2,5 ns -> faze se v segmentu rozmaze, tvar E NELZE urcit' % (c['name'], seg))
    elif span < 20:
        print('  !! %s: beh pokryl jen %.1f period -> tvar E nespolehlivy' % (c['name'], span))
    else:
        d = math.gcd(int(round(seg * 1000)), int(P * 1000)) / 1000.0     # krok mezi segmenty = sweep / NSEG
        print('  %s: faze segmentu na mrizce %.3g ps (%d ruznych poloh v periode)' % (c['name'], d, int(round(P / d))))
    # ruznych vzoru oken = kolik oken trva, nez se faze zacatku okna zopakuje; fit potrebuje vic vzoru nez neznamych
    c['kmax'] = (int(round(P / (math.gcd(int(round(abs(c['sweep']) * 1000)), int(P * 1000)) / 1000.0))) - 2) // 2
    if c['kmax'] < a.K:
        print('  !! %s: faze okna se opakuje po %d oknech -> lze nejvyse K %d (zvol kmitocet, kde posun za okno neni P/m)'
              % (c['name'], 2 * c['kmax'] + 2, c['kmax']))
    bl = blind(c['sweep'], a.K)
    if bl:
        print('  !! %s: slepe harmonicke do K %d (|H| < 0,3): %s -> jejich amplitudu fit neurci, tvar a rms slozek ber '
              's K pod nimi (nebo druhy beh s jinym posunem)' % (c['name'], a.K, bl))

out = {}
KE = max(1, min(a.K, A['kmax'], B['kmax']))            # rad fitu pro vypsany tvar (degenerovany kmitocet ho omezi)
print('\nfit E(faze), Fourier s periodou %.0f ps, test na nahodne polovine oken, kterou fit nevidel:' % P)
print('  kanal  K   R^2   sigma pred -> po [ps]   stredni chyba okna [ps] (relativne)')
for c, lab in ((A, 'A'), (B, 'B')):
    for K in sorted({4, KE, 20}):
        if K > c['kmax']:
            continue
        r2, s0, s1, c0, E = fit(c['e'], c['phi'], K)
        print('  %-5s %3d  %5.2f   %6.1f -> %5.1f        %+7.2f (%+.2e)' % (lab, K, r2, s0, s1, c0, c0 / c['gate_ps']))
        if K == KE:
            out[lab] = E - E.mean()
# rozdil A-B primo (overeni proti tvaru D z behu s GPSDO)
if abs(A['rel'] - B['rel']) < 1e-15:
    r2, s0, s1, c0, E = fit(A['e'] - B['e'], A['phi'], KE)
    print('  A-B   %3d  %5.2f   %6.1f -> %5.1f' % (KE, r2, s0, s1))
    out['AB'] = E - E.mean()

print('\ntvar E (K %d), spicka-spicka / rms [ps], max strmost [ps/ps]:' % KE)
for lab, E in out.items():
    print('  %-3s %6.1f / %5.1f   %.3f' % (lab, E.max() - E.min(), E.std(), np.abs(np.gradient(E, grid)).max()))
if 'A' in out and 'B' in out:
    print('  korelace E_A a E_B (tataz faze): %+.2f' % np.corrcoef(out['A'], out['B'])[0, 1])
print('\nE po %.0f ps (faze od zacatku behu):' % (20 * P / G))
print('   faze      ' + '  '.join('%7s' % k for k in out))
for i in range(0, G, 20):
    print('  %6.0f ps  ' % grid[i] + '  '.join('%+7.1f' % out[k][i] for k in out))
if a.out:
    for k, E in out.items():
        np.save('%s_%s.npy' % (a.out, k), E)
    print('ulozeno: %s_{%s}.npy' % (a.out, ','.join(out)))
