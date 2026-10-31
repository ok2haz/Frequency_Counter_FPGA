"""Po minutach: rozptyl kmitoctu zdroje (sd f_A okna proti 10 MHz, ppt) a sigma(A-B) MAD [ps].
Vypise korelaci a sigma(A-B) v binech podle neklidu zdroje -> lze porovnat dva behy (ruzne bitstreamy)."""
import csv, statistics, sys, math

def load(p):
    rows = []
    for r in csv.DictReader(open(p)):
        if r['signal_lost'] != 'False':
            continue
        fa = float(r['f_primary']); fb = float(r['f_secondary']); g = float(r['gate_ns'])
        if fa > 0 and fb > 0:
            rows.append((float(r['t_unix']), fa, (fa - fb) / fa * g * 1e3))
    return rows

for p in sys.argv[1:]:
    rows = load(p)
    t0 = rows[0][0]; mins = {}
    for t, fa, ab in rows:
        mins.setdefault(int((t - t0) // 60), []).append((fa, ab))
    pts = []
    for k, s in sorted(mins.items()):
        if len(s) < 100:
            continue
        sd = statistics.pstdev(fa for fa, _ in s) / 1e7 * 1e12        # ppt
        m = statistics.median(ab for _, ab in s)
        sg = statistics.median(abs(ab - m) for _, ab in s) * 1.4826
        pts.append((sd, sg))
    x = [math.log10(a) for a, _ in pts]; y = [math.log10(b) for _, b in pts]
    mx, my = sum(x) / len(x), sum(y) / len(y)
    r = sum((a - mx) * (b - my) for a, b in zip(x, y)) / math.sqrt(sum((a - mx) ** 2 for a in x) * sum((b - my) ** 2 for b in y))
    print('%s: minut %d, r(log neklid zdroje, log sigma A-B) = %.2f' % (p.split('/')[-2] if '/' in p else p, len(pts), r))
    for lo, hi in ((0, 200), (200, 400), (400, 800), (800, 1600), (1600, 1e9)):
        sel = [b for a, b in pts if lo <= a < hi]
        if sel:
            print('   sd f_A %5.0f-%-5.0f ppt: minut %4d, median sigma A-B %5.1f ps' % (lo, min(hi, 99999), len(sel), statistics.median(sel)))
