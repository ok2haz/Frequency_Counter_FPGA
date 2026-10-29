"""Korelace po cyklech (600 s): prirustek oken B bez LSQ / s vadnym segmentem / opravenych X0
proti poctu oken s velkym |A-B| v tomtez intervalu (ab.csv)."""
import csv, sys, statistics, datetime, math

d = sys.argv[1]
cyk = list(csv.DictReader(open(d + '/cykly.csv')))
ab = []
for r in csv.DictReader(open(d + '/ab.csv')):
    if r['signal_lost'] != 'False':
        continue
    fa = float(r['f_primary']); fb = float(r['f_secondary']); g = float(r['gate_ns'])
    if fa <= 0 or fb <= 0:
        continue
    ab.append((float(r['t_unix']), int(r['seq']), (fa - fb) / fa * g * 1e3, fa, fb))
med = statistics.median(x[2] for x in ab)

def ts(s):
    return datetime.datetime.fromisoformat(s).timestamp()

print('cyklus        dOkna  dNoLSQ  dVadOk  dX0fix |  n_ab  >200  >500  >1ns  sigMAD')
rows = []
for a, b in zip(cyk, cyk[1:]):
    t0, t1 = ts(a['cas']), ts(b['cas'])
    sel = [x[2] - med for x in ab if t0 <= x[0] < t1]
    if len(sel) < 100:
        continue
    dok = int(b['okna_b']) - int(a['okna_b'])
    dno = dok - (int(b['lsq_b']) - int(a['lsq_b']))
    dvo = int(b['vadseg_okna_b']) - int(a['vadseg_okna_b'])
    dfx = int(b['x0_fix_b']) - int(a['x0_fix_b'])
    n2 = sum(abs(v) > 200 for v in sel); n5 = sum(abs(v) > 500 for v in sel); n1 = sum(abs(v) > 1000 for v in sel)
    m = statistics.median(abs(v) for v in sel) * 1.4826
    rows.append((dno, dvo, dfx, n2, n5, n1))
    print('%s %6d %7d %7d %7d | %5d %5d %5d %5d %7.1f' % (b['cas'][11:16], dok, dno, dvo, dfx, len(sel), n2, n5, n1, m))

def corr(i, j):
    x = [r[i] for r in rows]; y = [r[j] for r in rows]
    mx, my = sum(x) / len(x), sum(y) / len(y)
    sxy = sum((p - mx) * (q - my) for p, q in zip(x, y))
    sx = math.sqrt(sum((p - mx) ** 2 for p in x)); sy = math.sqrt(sum((q - my) ** 2 for q in y))
    return sxy / (sx * sy) if sx and sy else float('nan')

nm = ['NoLSQ', 'VadOkna', 'X0fix', '>200', '>500', '>1ns']
for i in range(3):
    print('r(%s, >200)=%.2f  r(%s, >500)=%.2f  r(%s, >1ns)=%.2f' % (nm[i], corr(i, 3), nm[i], corr(i, 4), nm[i], corr(i, 5)))
print('soucet NoLSQ %d, >200 %d, >500 %d' % (sum(r[0] for r in rows), sum(r[3] for r in rows), sum(r[4] for r in rows)))
