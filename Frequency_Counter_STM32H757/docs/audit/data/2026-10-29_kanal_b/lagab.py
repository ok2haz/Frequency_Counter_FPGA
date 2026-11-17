"""Zarovnani oken A a B v ab.csv: korelace fluktuaci f_B(n) s f_A(n+j), j = -2..2 (fluktuace = odchylka od medianu
9 sousedu, aby pomaly drift korelaci nedelal). Pri spravnem parovani je maximum v j = 0. Zlomkovy posun (B posunute
o cast okna) da maximum rozdelene mezi j = 0 a +-1. Navic regrese (f_A - f_B) na (f_A(n) - f_A(n-1))."""
import csv, statistics, sys, math

for p in sys.argv[1:]:
    fa, fb, seq = [], [], []
    for r in csv.DictReader(open(p)):
        if r['signal_lost'] != 'False':
            continue
        a = float(r['f_primary']); b = float(r['f_secondary'])
        if a > 0 and b > 0:
            fa.append(a); fb.append(b); seq.append(int(r['seq']))
    def fl(v):
        return [v[i] - statistics.median(v[i - 4:i + 5]) for i in range(4, len(v) - 4)]
    A, B = fl(fa), fl(fb)
    sq = seq[4:-4]
    def corr(x, y):
        mx, my = sum(x) / len(x), sum(y) / len(y)
        sxy = sum((p - mx) * (q - my) for p, q in zip(x, y))
        return sxy / math.sqrt(sum((p - mx) ** 2 for p in x) * sum((q - my) ** 2 for q in y))
    out = []
    for j in (-2, -1, 0, 1, 2):
        xs, ys = [], []
        for i in range(2, len(A) - 2):
            if sq[i + j] - sq[i] == j:                       # souvisle cislovana okna
                xs.append(A[i + j]); ys.append(B[i])
        out.append('j=%+d r=%.3f' % (j, corr(xs, ys)))
    # podil uniku: f_A - f_B ~ k * (f_A(n) - f_A(n-1))
    xs, ys = [], []
    for i in range(1, len(fa)):
        if seq[i] - seq[i - 1] == 1:
            xs.append(fa[i] - fa[i - 1]); ys.append(fa[i] - fb[i])
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    k = sum((p - mx) * (q - my) for p, q in zip(xs, ys)) / sum((p - mx) ** 2 for p in xs)
    print('%s  oken %d | corr f_B(n) s f_A(n+j): %s | f_A-f_B = %.3f * (f_A(n)-f_A(n-1)), r = %.2f'
          % (p.replace('\\', '/').split('/')[-2], len(fa), ', '.join(out), k, corr(xs, ys)))
