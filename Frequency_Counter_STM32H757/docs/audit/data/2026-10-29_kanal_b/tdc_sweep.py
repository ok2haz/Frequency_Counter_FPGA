"""Hypoteza: sigma(A-B) roste s neklidem zdroje, protoze hrana za okno projde vic kodu TDC
(rozdil nelinearit A a B se neprumeruje).

Po oknech:
  ab    = A-B v ps ((fa-fb)/fa * gate)
  sweep = posun faze hrany proti taktu 100 MHz za okno [ps] = (fm-1e7)/1e7 * gate,
          fm = (fa+fb)/2 (chyba A a B se v prumeru nekoreluje s ab)
  d1    = zmena kmitoctu mezi sousednimi okny (fm(n+1)-fm(n-1))/2, prevedeno na ps za okno
  d2    = zakriveni fm(n) - (fm(n-1)+fm(n+1))/2, ps za okno

H1 (nelinearita TDC): ab ~ D'(faze) * sweep -> sigma roste s |sweep| a podepsany sklon ab na sweep
    se meni s fazi (v kratkych blocich vyznamny, s promennym znamenkem).
H2 (FM uvnitr okna / ruzna odezva odhadu): sigma roste s |d2| (resp. |d1|) pri pevnem |sweep|.
"""
import csv, sys, math, statistics

def mad(v):
    m = statistics.median(v)
    return 1.4826 * statistics.median(abs(x - m) for x in v)

def load(p):
    w = []
    for r in csv.DictReader(open(p)):
        if r['signal_lost'] != 'False':
            continue
        fa = float(r['f_primary']); fb = float(r['f_secondary']); g = float(r['gate_ns'])
        if fa > 0 and fb > 0:
            w.append((int(r['seq']), float(r['t_unix']), fa, fb, g))
    return w

def ols(x, y):
    n = len(x); mx = sum(x) / n; my = sum(y) / n
    sxx = sum((a - mx) ** 2 for a in x)
    if sxx <= 0 or n < 10:
        return None
    b = sum((a - mx) * (c - my) for a, c in zip(x, y)) / sxx
    res = [c - my - b * (a - mx) for a, c in zip(x, y)]
    s2 = sum(e * e for e in res) / (n - 2)
    return b, b / math.sqrt(s2 / sxx)

for p in sys.argv[1:]:
    w = load(p)
    rows = []
    for i in range(1, len(w) - 1):
        s0, s1, s2 = w[i - 1][0], w[i][0], w[i + 1][0]
        if s1 - s0 != 1 or s2 - s1 != 1:
            continue
        _, t, fa, fb, g = w[i]
        fm = lambda k: (w[k][2] + w[k][3]) / 2
        ab = (fa - fb) / fa * g * 1e3
        sweep = (fm(i) - 1e7) / 1e7 * g * 1e3
        d1 = (fm(i + 1) - fm(i - 1)) / 2 / 1e7 * g * 1e3
        d2 = (fm(i) - (fm(i - 1) + fm(i + 1)) / 2) / 1e7 * g * 1e3
        rows.append((t, ab, sweep, d1, d2))
    med = statistics.median(r[1] for r in rows)
    print('%s: oken %d, median A-B %.1f ps, sigma %.1f ps' % (p, len(rows), med, mad([r[1] for r in rows])))

    # 1) sigma podle |sweep|
    edges = [0, 25, 50, 100, 200, 400, 800, 1600, 1e9]
    print('\n1) sigma(A-B) podle |sweep| (posun faze za okno)')
    for lo, hi in zip(edges, edges[1:]):
        sel = [r[1] for r in rows if lo <= abs(r[2]) < hi]
        if len(sel) > 200:
            print('   |sweep| %5.0f-%-5.0f ps: oken %6d  sigma %5.1f ps' % (lo, min(hi, 99999), len(sel), mad(sel)))

    # 2) dvourozmerne: |sweep| x |d2|
    d2e = [0, 25, 100, 400, 1e9]
    print('\n2) sigma(A-B) [ps] (oken): radky |sweep|, sloupce |d2| (zakriveni kmitoctu za okno, ps)')
    print('   %-14s' % '' + ''.join('%-18s' % ('d2 %g-%g' % (a, min(b, 9999))) for a, b in zip(d2e, d2e[1:])))
    for lo, hi in zip(edges, edges[1:]):
        line = '   sw %5.0f-%-5.0f ' % (lo, min(hi, 99999))
        for a, b in zip(d2e, d2e[1:]):
            sel = [r[1] for r in rows if lo <= abs(r[2]) < hi and a <= abs(r[4]) < b]
            line += ('%5.1f (%6d)     ' % (mad(sel), len(sel))) if len(sel) >= 200 else '%-18s' % '   -'
        print(line)

    # 3) podepsane sklony v blocich 60 s
    print('\n3) podepsany sklon A-B na regresor v blocich 60 s (H0: mean t^2 ~ 1, |t|>3 ~ 0,3 %)')
    for name, idx in (('sweep', 2), ('d1', 3), ('d2', 4)):
        blk = {}
        for r in rows:
            blk.setdefault(int(r[0] // 60), []).append(r)
        ts = []; bs = []
        for k, b in blk.items():
            o = ols([r[idx] for r in b], [r[1] for r in b])
            if o:
                bs.append(o[0]); ts.append(o[1])
        pos = sum(b > 0 for b in bs)
        o_all = ols([r[idx] for r in rows], [r[1] for r in rows])
        print('   %-5s: bloku %d, mean t^2 %.2f, |t|>3 %.1f %%, sklon>0 %d %% | celkovy sklon %+.4f (t %+.1f)' % (
            name, len(ts), sum(t * t for t in ts) / len(ts), 100 * sum(abs(t) > 3 for t in ts) / len(ts),
            100 * pos / len(bs), o_all[0], o_all[1]))
    print()
