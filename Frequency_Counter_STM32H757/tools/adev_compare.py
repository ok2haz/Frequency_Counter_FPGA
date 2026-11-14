"""adev_compare.py - porovna Allanovu odchylku (overlapping, tau0 = 1 s) ze surovych mereni (`tools/raw_fetch.py`, CSV abs,epoch,seq,t_ms,f_hz,flags).
Pouziti: python tools/adev_compare.py soubor.csv od1 do1 [od2 do2 ...]   (od/do = cisla RADKU dat, pro srovnani stejne faze po zapnuti vs. pozdeji).
Vyhazuje mereni bez priznaku platnosti (bit0) a se STALE/SPIKE (bity 2, 3); nominal 10 MHz. Slouzi k objektivnimu srovnani dvou behu
(napr. stary a novy bitstream) misto dojmu z displeje. Zdroj stability je vzdy dvojice zdroj + reference: srovnavat jen stejne faze zahrati OCXO."""
import csv, sys, math

def load(path, t0=None, t1=None):
    ts, fs = [], []
    with open(path) as fh:
        r = csv.reader(fh)
        next(r)
        for row in r:
            try:
                tms = int(row[3]); f = float(row[4]); fl = int(row[5])
            except Exception:
                continue
            if not (fl & 1) or (fl & 12):
                continue
            ts.append(tms / 1000.0); fs.append(f)
    return ts, fs

def adev_overlap(y, m):
    """overlapping Allan z relativnich odchylek y (tau0 = 1 s), tau = m*tau0"""
    n = len(y)
    if n < 2 * m + 1:
        return None, 0
    # integrovane (fazove) x
    x = [0.0]
    for v in y:
        x.append(x[-1] + v)
    s = 0.0; c = 0
    for i in range(0, len(x) - 2 * m):
        d = x[i + 2 * m] - 2 * x[i + m] + x[i]
        s += d * d; c += 1
    return math.sqrt(s / (2.0 * c)) / m, c

def report(name, ts, fs, i0, i1, nom=1e7):
    seg_t, seg_f = ts[i0:i1], fs[i0:i1]
    # jen souvisly usek 1 s (diry v rade by zkreslily x)
    y = [(f - nom) / nom for f in seg_f]
    out = []
    for m in (1, 2, 4, 8, 16, 32, 64, 128):
        a, c = adev_overlap(y, m)
        out.append('%d:%s' % (m, ('%.2e' % a) if a else '--'))
    dur = (seg_t[-1] - seg_t[0]) if len(seg_t) > 1 else 0
    # rozptyl 1s mereni a pocet mezer
    gaps = sum(1 for a, b in zip(seg_t, seg_t[1:]) if b - a > 1.5)
    print('%-28s n=%6d dur=%6.0fs mezer>1.5s:%d  ADEV(tau=m s)  %s' % (name, len(seg_f), dur, gaps, '  '.join(out)))

if __name__ == '__main__':
    path = sys.argv[1]
    ts, fs = load(path)
    print(path, len(fs), 'platnych mereni')
    segs = [(a, b) for a, b in zip(sys.argv[2::2], sys.argv[3::2])]
    for a, b in segs:
        report('%s..%s' % (a, b), ts, fs, int(a), int(b))
