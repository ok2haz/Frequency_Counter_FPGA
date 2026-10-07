# Mereni TDC a casove zakladny pomoci SCHODU (beat) mezi nezavislymi oscilatory.
#
# Zapojeni: casova zakladna citace = vnitrni OCXO (Si5356 -> 100 MHz), VSTUP citace = externi GPSDO 10 MHz.
# Dva nezavisle oscilatory se lisi o delta (typicky 1e-9..1e-8), takze faze hrany signalu vuci hodinam TDC
# pomalu projede VSECHNY kody (10 ns za 10 ns / delta, napr. 1,6 s pri delta = 6e-9). Na rozdil od mereni
# VLASTNI reference (synchronni -> obe krajni hrany okna na stejnem kodu -> chyba se odecte) je tohle
# NEZAVISLE mereni: dava (a) absolutni chybu OCXO vuci GPS, (b) INL/DNL TDC jako funkci faze,
# (c) skutecny rozptyl oken, (d) frakci a polohu skoku u obriho binu.
#
# Pouziti:
#   python tools/tdc_beat.py collect --minutes 30 --out beat.csv [--host 10.0.0.106]
#   python tools/tdc_beat.py analyze beat.csv [--fnom 1e7] [--block 160] [--bins 50]
#   python tools/tdc_beat.py selftest            # overi samotny nastroj na syntetickych datech
import sys, json, time, math, urllib.request, statistics as st

T_PS = 10000.0                     # perioda vzorkovani TDC [ps]
JUMP_PS = 2000.0                   # |chyba znacky| nad tim = skok (obri bin, ~9,3 ns), ne sum

def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default

# ------------------------------------------------------------------ sber (SSE)
def collect(host, minutes, out):
    rows = []; t_end = time.time() + 60 * minutes
    req = urllib.request.urlopen('http://%s/api/stream' % host, timeout=30)
    print('sber %d min z %s ...' % (minutes, host)); last = 0
    while time.time() < t_end:
        line = req.readline().decode('utf-8', 'replace')
        if not line.startswith('data:'): continue
        try: d = json.loads(line[5:])
        except ValueError: continue
        if d.get('freq_hz') is None or d.get('signal_lost'): continue
        rows.append((time.time(), d['seq_meas'], d['freq_hz'], d['gate_ns']))
        if len(rows) - last >= 400:
            last = len(rows); print('  %d oken' % len(rows)); open(out, 'w').write(
                'wall,seq,f_hz,gate_ns\n' + ''.join('%.3f,%d,%.7f,%d\n' % r for r in rows))
    open(out, 'w').write('wall,seq,f_hz,gate_ns\n' + ''.join('%.3f,%d,%.7f,%d\n' % r for r in rows))
    print('ulozeno %d oken do %s' % (len(rows), out))

# ------------------------------------------------------------------ pomocne
def solve(A, b):
    n = len(b); M = [A[i][:] + [b[i]] for i in range(n)]
    for i in range(n):
        p = max(range(i, n), key=lambda r: abs(M[r][i])); M[i], M[p] = M[p], M[i]
        for r in range(i + 1, n):
            f = M[r][i] / M[i][i]
            for c in range(i, n + 1): M[r][c] -= f * M[i][c]
    x = [0.0] * n
    for i in range(n - 1, -1, -1):
        x[i] = (M[i][n] - sum(M[i][c] * x[c] for c in range(i + 1, n))) / M[i][i]
    return x

def polyfit_robust(x, y, deg=2, cut=1500.0):
    """LSQ polynom stupne deg; 3 iterace s vyrazenim |rezidua| > cut (skoky obriho binu)."""
    use = [True] * len(x)
    for _ in range(3):
        xs = [xi for xi, u in zip(x, use) if u]; ys = [yi for yi, u in zip(y, use) if u]
        if len(xs) < deg + 2: break
        A = [[sum(xi ** (i + j) for xi in xs) for j in range(deg + 1)] for i in range(deg + 1)]
        b = [sum(yi * xi ** i for xi, yi in zip(xs, ys)) for i in range(deg + 1)]
        c = solve(A, b)
        res = [yi - sum(c[i] * xi ** i for i in range(deg + 1)) for xi, yi in zip(x, y)]
        use = [abs(r) < cut for r in res]
    return c, res

def load(path):
    rows = []
    for l in open(path).read().splitlines()[1:]:
        w, s, f, g = l.split(','); rows.append((float(w), int(s), float(f), int(g)))
    return rows

def segments(rows):
    seg = [rows[0]]; out = []
    for a, b in zip(rows, rows[1:]):
        if b[1] - a[1] == 1: seg.append(b)
        else: out.append(seg); seg = [b]
    out.append(seg); return [s for s in out if len(s) >= 200]

# ------------------------------------------------------------------ analyza
def analyze(path, fnom, block, nb):
    rows = load(path); gaps = sum(1 for a, b in zip(rows, rows[1:]) if b[1] - a[1] != 1)
    nmiss = sum(b[1] - a[1] - 1 for a, b in zip(rows, rows[1:]) if b[1] - a[1] > 1)
    print('oken %d, mezer v SEQ %d (chybi %d oken = zahozena miscount okna + zmeskana), delka %.0f s' % (
        len(rows), gaps, nmiss, rows[-1][0] - rows[0][0]))
    segs = segments(rows)
    if not segs: sys.exit('zadny souvisly usek >= 200 oken')
    fall = [r[2] for r in rows]
    mf = st.mean(fall); delta = mf / fnom - 1
    print('prumer f = %.7f Hz  ->  delta (OCXO vs vstup) = %+.3e  (%+.3f ppb)' % (mf, delta, delta * 1e9))
    if abs(delta) > 1e-12: print('perioda schodu %.1f s (10 ns faze za tuto dobu)%s' % (
        T_PS * 1e-12 / abs(delta), '  <-- pomale, INL mapa bude hruba' if abs(delta) < 2e-10 else ''))
    bins = [[] for _ in range(nb)]; jumps = [0] * nb; allres = []; nwin = 0
    for seg in segs:
        dts = []; ncum = []; n = 0; ts = 0.0; tsl = []
        for _, _, f, g in seg:
            N = round(f * g * 1e-9); dt = N / f * 1e12     # [ps]
            n += N; ts += dt; ncum.append(n); tsl.append(ts)
        for a in range(0, len(seg) - block // 2, block):
            xs = ncum[a:a + block]; ys = tsl[a:a + block]
            if len(xs) < 40: continue
            xm = st.mean(xs); sc = (xs[-1] - xs[0]) or 1.0
            xc = [(x - xm) / sc for x in xs]
            c, res = polyfit_robust(xc, ys)
            for xi, yi, r in zip(xc, ys, res):
                fit = sum(c[i] * xi ** i for i in range(3))
                ph = fit % T_PS; k = min(nb - 1, int(ph / T_PS * nb))
                nwin += 1
                if abs(r) >= JUMP_PS: jumps[k] += 1
                else: bins[k].append(r); allres.append(r)
    print('analyzovano %d oken v %d useku, rezidua = chyba casove znacky (ps)' % (nwin, len(segs)))
    nj = sum(jumps)
    print('skoky |chyba| >= %.0f ps: %d (%.1f %% oken)' % (JUMP_PS, nj, 100.0 * nj / max(1, nwin)))
    if allres:
        mad = st.median(abs(x - st.median(allres)) for x in allres) * 1.4826
        print('mimo skoky: stred %.1f ps, sigma %.1f ps (robustne %.1f ps)  -> sigma okna ~ sqrt2 x = %.1f ps = %.2e relativne za 0,25 s' % (
            st.mean(allres), st.pstdev(allres), mad, math.sqrt(2) * mad, math.sqrt(2) * mad * 1e-12 / 0.25))
    print('\nINL mapa: faze (ps vuci hranici taktu) | n | stred chyby | sigma | skoky')
    for k in range(nb):
        v = bins[k]
        if not v and not jumps[k]: continue
        mu = st.mean(v) if v else float('nan'); sg = st.pstdev(v) if len(v) > 1 else float('nan')
        bar = '#' * int(min(40, abs(mu) / 5)) if v else ''
        print('%6.0f-%-6.0f %5d %8.1f %7.1f %5d  %s' % (k * T_PS / nb, (k + 1) * T_PS / nb, len(v), mu, sg, jumps[k], bar))
    print('\nPOZOR: faze je urcena az na konstantu (neznamy offset vuci hranici taktu) - polohu obriho binu urci rozlozeni skoku.')

# ------------------------------------------------------------------ selftest nastroje
def selftest():
    import random
    random.seed(7)
    fnom = 1e7; delta = 6e-9; gate = 0.25; ph0 = 3000.0
    # model TDC: INL = 60 ps * sin(2 pi phi / 5000), sum 28 ps, skok +-9282 ps v useku faze 9000..9300 (50 % pravd.)
    t = 0.0; rows = []; seq = 1000; ts_prev = None; ncum = 0
    f_true = fnom * (1 + delta)
    N = 2500000
    for k in range(6000):
        t_edge = (k + 1) * N / f_true * 1e12                         # skutecna cas hrany [ps]
        ph = (t_edge + ph0) % T_PS
        eps = 60 * math.sin(2 * math.pi * ph / 5000) + random.gauss(0, 28)
        if 9000 <= ph < 9300 and random.random() < 0.5: eps += 9282 * random.choice((-1, 1))
        ts = t_edge + eps
        if ts_prev is not None:
            dt = ts - ts_prev; f = N / dt * 1e12
            rows.append((k * 0.25, seq, f, 250000000)); seq += 1
        ts_prev = ts
    open('_selftest_beat.csv', 'w').write('wall,seq,f_hz,gate_ns\n' + ''.join('%.3f,%d,%.7f,%d\n' % r for r in rows))
    import io, contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf): analyze('_selftest_beat.csv', fnom, 160, 20)
    out = buf.getvalue(); print(out)
    ok = ('+6.0' in out.split('delta')[1][:60] or '+5.9' in out.split('delta')[1][:60])
    j = [l for l in out.splitlines() if l.startswith('skoky')][0]
    nj = int(j.split(':')[1].split('(')[0])
    ok2 = 80 < nj < 400           # ~ 3 % faze (300/10000) x 50 % x 6000 = ~90 plus sousedni bod dvojice
    mad = [l for l in out.splitlines() if l.startswith('mimo skoky')][0]
    sg = float(mad.split('sigma')[1].split('ps')[0])
    print('OK delta' if ok else 'FAIL delta', '| OK skoky %d' % nj if ok2 else '| FAIL skoky %d' % nj, '| sigma %.1f ps (vlozeno INL 60/sqrt2 + sum 28 -> ~46)' % sg)
    sys.exit(0 if ok and ok2 and 35 < sg < 60 else 1)

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'collect': collect(arg('--host', '10.0.0.106'), float(arg('--minutes', '30')), arg('--out', 'beat.csv'))
    elif cmd == 'analyze': analyze(sys.argv[2], float(arg('--fnom', '1e7')), int(arg('--block', '160')), int(arg('--bins', '50')))
    elif cmd == 'selftest': selftest()
    else: print(__doc__ if __doc__ else open(__file__).read().split('\n\n')[0])
