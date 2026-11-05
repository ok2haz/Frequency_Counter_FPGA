"""(2026-10-31) Z ceho je slozka vazana na 100 ns? E(faze mod 100 ns) se rozdeli na 10 taktu po 10 ns (E_n) a kazdy
takt se popise jako tvar spolecny vsem taktum posunuty o s_n a zvednuty o o_n:  E_n(x) ~ E_ref(x + s_n) + o_n.
- Vysvetli-li slozku 100 ns hlavne o_n (schod po celem taktu), jde o posun casu vazany na takt vuci 10 MHz
  (napr. zpozdeni hrany hodin v tom taktu). Tak vypada beh s F0a: o_n az -180 ps, zbytek polovina.
- Zbude-li uzka spicka uvnitr taktu, je to porucha s vlastnim tvarem (APP_OSC: dvojice +-100 ps, 50 ns od sebe).
E_ref se zpresnuje zarovnanim taktu (4 iterace) a uklada se do tdcref_A/B.npy vedle <ab.csv> (porovnani behu:
korelace po kruhovem posunu). K drz pod slepou harmonickou (tdc_gensweep.py ji hlasi), vychozi K 130.
python -I tdc_gencyc.py <ab.csv> P1 P2 P3 R [K]"""
import sys, os, math
import numpy as np
argv = sys.argv[1:]
KK = int(argv[5]) if len(argv) > 5 else 130
sys.argv = [sys.argv[0], argv[0], '--a'] + argv[1:5] + ['--P', '100000']
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tdc_gensweep.py'), encoding='utf-8').read()
src = src[:src.index("print('oken %d")]
exec(src)
P = 100000.0
NG = 8000                                  # 12,5 ps na bod
g = np.linspace(0, P, NG, endpoint=False)
M = NG // 10                               # bodu na takt

def shape(coef):
    return sum(coef[2*k-1]*np.cos(2*math.pi*k/P*g) + coef[2*k]*np.sin(2*math.pi*k/P*g) for k in range(1, KK+1))

def fit_shift(e, ref):
    """nejlepsi kruhovy posun (+-40 bodu = +-500 ps) a offset; vrati (posun_ps, offset, rms zbytku)"""
    best = None
    for s in range(-40, 41):
        r = np.roll(ref, -s)
        o = np.mean(e - r)
        rr = np.sqrt(np.mean((e - r - o) ** 2))
        if best is None or rr < best[2]:
            best = (s * P / NG, o, rr)
    return best

for lab, c in (('A', A), ('B', B)):
    X = design(c['phi'], KK)
    coef, *_ = np.linalg.lstsq(X, c['e'], rcond=None)
    E = shape(coef)
    cyc = E.reshape(10, M)
    avg = cyc.mean(axis=0)
    c100 = cyc - avg
    print('%s: rms E %.1f ps = slozka 10 ns %.1f (+) slozka 100 ns %.1f' % (
        lab, np.sqrt(np.mean((E - E.mean()) ** 2)), avg.std(), c100.std()))
    print('   takt  offset  -> zbytek | posun+offset -> zbytek   [ps]')
    r_off, r_sh = [], []
    for n in range(10):
        e = cyc[n]
        o = np.mean(e - avg)
        ro = np.sqrt(np.mean((e - avg - o) ** 2))
        s, o2, rs = fit_shift(e, avg)
        r_off.append(ro); r_sh.append(rs)
        print('   %2d   %+6.1f -> %5.1f   | %+6.0f %+6.1f -> %5.1f' % (n, o, ro, s, o2, rs))
    print('   rms zbytku slozky 100 ns: jen offset %.1f, posun+offset %.1f ps (puvodne %.1f)' % (
        np.sqrt(np.mean(np.square(r_off))), np.sqrt(np.mean(np.square(r_sh))), c100.std()))
    ref = avg.copy()
    for it in range(4):
        fits = [fit_shift(cyc[n], ref) for n in range(10)]
        ref = np.mean([np.roll(cyc[n], int(round(fits[n][0] * NG / P))) - fits[n][1] for n in range(10)], axis=0)
        ref -= ref.mean()
    fits = [fit_shift(cyc[n], ref) for n in range(10)]
    print('   po zarovnani taktu: tvar TDC rms %.1f ps (prumer bez zarovnani %.1f), posuny taktu [ps]: %s' % (
        ref.std(), avg.std(), ' '.join('%+.0f' % f[0] for f in fits)))
    print('   offsety taktu [ps]: %s; zbytek %.1f ps\n' % (
        ' '.join('%+.0f' % f[1] for f in fits), np.sqrt(np.mean([f[2] ** 2 for f in fits]))))
    np.save(os.path.join(os.path.dirname(os.path.abspath(argv[0])), 'tdcref_%s.npy' % lab), ref)
