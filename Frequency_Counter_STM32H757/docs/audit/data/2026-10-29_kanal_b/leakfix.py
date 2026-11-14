"""Odhad: A-B bez uniku spolecneho sumu z posunu oken. r = (f_A - f_B) - k*(f_A(n) - f_A(n-1)), k z regrese.
Vypise sigma(MAD) a ocas [ps na okno] pred a po (jen souvisle cislovana okna)."""
import csv, statistics, sys

for p in sys.argv[1:]:
    rows = []
    for r in csv.DictReader(open(p)):
        if r['signal_lost'] != 'False':
            continue
        a = float(r['f_primary']); b = float(r['f_secondary']); g = float(r['gate_ns'])
        if a > 0 and b > 0:
            rows.append((int(r['seq']), a, b, g))
    xs, ys, gs = [], [], []
    for i in range(1, len(rows)):
        if rows[i][0] - rows[i - 1][0] == 1:
            xs.append(rows[i][1] - rows[i - 1][1]); ys.append(rows[i][1] - rows[i][2]); gs.append(rows[i][3] / rows[i][1] * 1e3)
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    k = sum((u - mx) * (v - my) for u, v in zip(xs, ys)) / sum((u - mx) ** 2 for u in xs)
    def st(v):
        m = statistics.median(v); d = [abs(x - m) for x in v]
        return '%5.1f ps  >100 %6d  >200 %6d  >500 %5d  >1ns %4d' % (statistics.median(d) * 1.4826, sum(x > 100 for x in d),
                                                                  sum(x > 200 for x in d), sum(x > 500 for x in d), sum(x > 1000 for x in d))
    before = [y * g for y, g in zip(ys, gs)]
    after = [(y - k * x) * g for x, y, g in zip(xs, ys, gs)]
    print('%s (oken %d, k = %.3f)' % (p.replace('\\', '/').split('/')[-2], len(before), k))
    print('   dnes     sigma ' + st(before))
    print('   bez uniku sigma ' + st(after))
