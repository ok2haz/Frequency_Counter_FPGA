"""Je D(faze) stabilni v case? Fit D v kratkych blocich (600 oken), kazdy blok ma neznamy pocatek faze
-> zarovnani kruhovym posunem na spolecny vzor. Kontrola: totez s fazi zamichanou mezi okny.
Vypise prumerny tvar D (po 250 ps) a rozlozeni korelaci bloku se vzorem."""
import sys, math
import numpy as np
OUT = sys.argv[2] if len(sys.argv) > 2 else None
sys.argv = [sys.argv[0], sys.argv[1]]
import os
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tdc_dfit.py'), encoding='utf-8').read()
src = src[:src.index("print('oken %d")]          # jen nacteni a pomocne funkce z tdc_dfit.py
exec(src)

K = 20; B = 600; G = 400
grid = np.linspace(0, P, G, endpoint=False)
basis = np.column_stack([f(2 * math.pi * k / P * grid) for k in range(1, K + 1) for f in (np.cos, np.sin)])

def shapes(ph):
    global phi
    phi = ph
    out = []; r2 = []
    for b0 in range(0, len(ab) - B, B):
        idx = np.arange(b0, b0 + B)
        X = design(idx, K)
        coef, *_ = np.linalg.lstsq(X, ab[idx], rcond=None)
        D = basis @ coef[1:]
        out.append(D - D.mean())
    return np.array(out)

def align(S):
    ref = S[0].copy()
    for it in range(4):
        al = []; cs = []
        for D in S:
            c = np.array([np.dot(np.roll(D, s), ref) for s in range(G)])
            s = int(np.argmax(c))
            Dr = np.roll(D, s)
            al.append(Dr); cs.append(np.corrcoef(Dr, ref)[0, 1])
        ref = np.mean(al, axis=0)
    return ref, np.array(cs), np.array(al)

saved = phi.copy()
S = shapes(saved)
ref, cs, al = align(S)
rng = np.random.default_rng(2)
Sn = shapes(saved[rng.permutation(len(ab))])
refn, csn, _ = align(Sn)
print('bloku %d po %d oknech, K %d' % (len(S), B, K))
print('korelace bloku se vzorem: median %.2f, kvartily %.2f / %.2f   | kontrola zamichana faze: median %.2f, kvartily %.2f / %.2f' % (
    np.median(cs), *np.percentile(cs, [25, 75]), np.median(csn), *np.percentile(csn, [25, 75])))
print('vzor D: p-p %.1f ps, rms %.1f ps (kontrola p-p %.1f ps)' % (ref.max() - ref.min(), ref.std(), refn.max() - refn.min()))
# zarovnat vzor tak, aby minimum bylo na fazi 0, vypsat po 250 ps
s0 = int(np.argmin(ref)); r = np.roll(ref, -s0)
print('tvar D (faze od minima, po 250 ps):')
for i in range(0, G, 10):
    print('  %5.0f ps %+7.1f %s' % (grid[i], r[i], '#' * int(max(0, (r[i] - r.min()) / 4))))
# derivace: kde je D strme
dD = np.gradient(r, grid)
print('max |dD/dfaze| %.3f ps/ps na fazi %.0f ps; podil fazi s |dD| > 0,1: %.0f %%' % (
    np.abs(dD).max(), grid[np.argmax(np.abs(dD))], 100 * np.mean(np.abs(dD) > 0.1)))

if OUT:
    np.save(OUT, ref)
