"""(2026-10-30, beh 10 000 000,041 Hz) Rozklad E(faze mod 100 ns) kazdeho kanalu na slozku periodickou v 10 ns (k nasobek 10) a slozku vazanou na 100 ns
(ostatni k). Korelace slozek A a B: je 100ns slozka spolecna? Vypise i tvary po 5 ns.
python -I gs_split.py <ab.csv> P1 P2 P3 R [K]"""
import sys, os, math
import numpy as np
argv = sys.argv[1:]
KK = int(argv[5]) if len(argv) > 5 else 100
sys.argv = [sys.argv[0], argv[0], '--a'] + argv[1:5]
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tdc_gensweep.py'), encoding='utf-8').read()
src = src[:src.index("print('oken %d")]
exec(src)
P = 100000.0
g = np.linspace(0, P, 2000, endpoint=False)
parts = {}
for lab, c in (('A', A), ('B', B)):
    X = design(c['phi'], KK)
    coef, *_ = np.linalg.lstsq(X, c['e'], rcond=None)
    s10 = sum(coef[2*k-1]*np.cos(2*math.pi*k/P*g) + coef[2*k]*np.sin(2*math.pi*k/P*g) for k in range(1, KK+1) if k % 10 == 0)
    s100 = sum(coef[2*k-1]*np.cos(2*math.pi*k/P*g) + coef[2*k]*np.sin(2*math.pi*k/P*g) for k in range(1, KK+1) if k % 10 != 0)
    parts[lab] = (s10, s100)
    print('%s: slozka 10 ns p-p %.0f / rms %.1f ps, slozka 100 ns p-p %.0f / rms %.1f ps' % (
        lab, s10.max() - s10.min(), s10.std(), s100.max() - s100.min(), s100.std()))
print('korelace A/B: slozka 10 ns %+.2f, slozka 100 ns %+.2f' % (
    np.corrcoef(parts['A'][0], parts['B'][0])[0, 1], np.corrcoef(parts['A'][1], parts['B'][1])[0, 1]))
d = parts['A'][1] - parts['B'][1]
print('100ns slozka A-B: rms %.1f ps (A %.1f, B %.1f) -> v A-B se z ni zbude %.0f %%' % (
    d.std(), parts['A'][1].std(), parts['B'][1].std(), 100 * d.std() / np.sqrt((parts['A'][1].std()**2 + parts['B'][1].std()**2) / 2)))
print('\nslozka 100 ns po 5 ns (faze od zacatku behu):')
for i in range(0, 2000, 100):
    print('  %6.1f ns  A %+6.1f  B %+6.1f' % (g[i] / 1000, parts['A'][1][i], parts['B'][1][i]))
