"""(2026-10-31, nocni beh 10 000 000,041 Hz s APP_OSC) Stalost tvaru E(faze mod 100 ns) v case.
Faze je presne znama z cisla okna (generator a takt FPGA z tehoz VCO), takze se bloky nezarovnavaji. Beh se rozdeli na
bloky (vychozi 4), v kazdem se nafituje model P = 100 ns s K harmonickymi a pouzije se na VSECHNY bloky: zbytek pri
krizovem pouziti rika, zda by jedna tabulka korekci vydrzela cely beh. Vypise i stredni chybu okna po blocich
(STATUS #348) a rms slozky 10 ns / 100 ns po blocich (rozklad jako tdc_gensplit.py).
K drz pod slepou harmonickou (tdc_gensweep.py ji hlasi; pri 102,5 ps/segment je to 140) -> vychozi K 130.
python -I tdc_genstab.py <ab.csv> P1 P2 P3 R [K] [bloku]"""
import sys, os, math
import numpy as np
argv = sys.argv[1:]
KK = int(argv[5]) if len(argv) > 5 else 130
NB = int(argv[6]) if len(argv) > 6 else 4
sys.argv = [sys.argv[0], argv[0], '--a'] + argv[1:5] + ['--P', '100000']
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tdc_gensweep.py'), encoding='utf-8').read()
src = src[:src.index("print('oken %d")]
exec(src)
P = 100000.0
g = np.linspace(0, P, 2000, endpoint=False)

def comps(coef):
    s10 = sum(coef[2*k-1]*np.cos(2*math.pi*k/P*g) + coef[2*k]*np.sin(2*math.pi*k/P*g) for k in range(1, KK+1) if k % 10 == 0)
    s100 = sum(coef[2*k-1]*np.cos(2*math.pi*k/P*g) + coef[2*k]*np.sin(2*math.pi*k/P*g) for k in range(1, KK+1) if k % 10 != 0)
    return s10, s100

for lab, c in (('A', A), ('B', B)):
    n = len(c['e'])
    edges = np.linspace(0, n, NB + 1).astype(int)
    blk = [np.arange(edges[i], edges[i + 1]) for i in range(NB)]
    coefs = []
    print('%s: blok  okna   stredni chyba okna [ps] (relativne)   slozka 10 ns / 100 ns rms [ps]   zbytek vlastniho fitu [ps]' % lab)
    for i, idx in enumerate(blk):
        X = design(c['phi'][idx], KK)
        coef, *_ = np.linalg.lstsq(X, c['e'][idx], rcond=None)
        coefs.append(coef)
        r = c['e'][idx] - X @ coef
        s10, s100 = comps(coef)
        print('     %4d %6d   %+7.3f (%+.2e)                 %6.1f / %5.1f                    %5.1f' % (
            i, len(idx), coef[0], coef[0] / c['gate_ps'], s10.std(), s100.std(), mad(r)))
    print('   krizove pouziti: model z bloku (radek) na data bloku (sloupec), MAD zbytku [ps]')
    print('        ' + ''.join('%8d' % j for j in range(NB)))
    for i in range(NB):
        row = '   %4d ' % i
        for j, idx in enumerate(blk):
            X = design(c['phi'][idx], KK)
            r = c['e'][idx] - X @ coefs[i]
            r = r - np.median(r)
            row += '%8.1f' % mad(r)
        print(row)
    a10, a100 = comps(coefs[0])
    b10, b100 = comps(coefs[-1])
    print('   tvar prvni vs posledni blok: korelace slozky 10 ns %+.3f, slozky 100 ns %+.3f; rms rozdilu %.1f / %.1f ps\n' % (
        np.corrcoef(a10, b10)[0, 1], np.corrcoef(a100, b100)[0, 1], (a10 - b10).std(), (a100 - b100).std()))
