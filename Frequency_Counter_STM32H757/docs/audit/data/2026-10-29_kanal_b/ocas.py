"""Ocas A-B: sousedni okna nejvetsich odchylek (par = sdilena hranicni znacka, osamocene = delka okna)
a kdo ulital (odchylka fA, fB od medianu 5 sousedu, v ps na okno)."""
import csv, sys, statistics

d = sys.argv[1]
ab = {}
for r in csv.DictReader(open(d + '/ab.csv')):
    if r['signal_lost'] != 'False':
        continue
    fa = float(r['f_primary']); fb = float(r['f_secondary']); g = float(r['gate_ns'])
    if fa > 0 and fb > 0:
        ab[int(r['seq'])] = (fa, fb, g)
seqs = sorted(ab)
dab = {s: (ab[s][0] - ab[s][1]) / ab[s][0] * ab[s][2] * 1e3 for s in seqs}
med = statistics.median(dab.values())

def dev(s, i):
    """odchylka kanalu i v okne s od medianu sousedu s-3..s+3 (bez s), ps"""
    nb = [ab[k][i] for k in range(s - 3, s + 4) if k != s and k in ab]
    if len(nb) < 4:
        return float('nan')
    m = statistics.median(nb)
    return (ab[s][i] - m) / m * ab[s][2] * 1e3

big = sorted((s for s in seqs if abs(dab[s] - med) > 500), key=lambda s: -abs(dab[s] - med))
print('oken >500 ps:', len(big))
par = 0; sam = 0; dA = 0; dB = 0
for s in big:
    p = dab.get(s - 1, 0) - med; n = dab.get(s + 1, 0) - med; v = dab[s] - med
    if (abs(p) > 0.3 * abs(v) and p * v < 0) or (abs(n) > 0.3 * abs(v) and n * v < 0):
        par += 1
    else:
        sam += 1
    ea, eb = dev(s, 0), dev(s, 1)
    if abs(eb) > abs(ea):
        dB += 1
    else:
        dA += 1
print('s opacnym sousedem (par) %d, osamocene %d; vic ulita A %d, B %d' % (par, sam, dA, dB))
print('\nnejvetsich 25:  seq   A-B[ps]  soused-1  soused+1   dev_A   dev_B')
for s in big[:25]:
    print('%8d %9.0f %9.0f %9.0f %7.0f %7.0f' % (s, dab[s] - med, dab.get(s - 1, 0) - med, dab.get(s + 1, 0) - med, dev(s, 0), dev(s, 1)))
# shluky: rozestupy mezi okny > 500 ps
gaps = [b - a for a, b in zip(sorted(big), sorted(big)[1:])]
print('\nrozestup oken >500 ps (median %d oken); do 10 oken: %d z %d' % (statistics.median(gaps), sum(g <= 10 for g in gaps), len(gaps)))
