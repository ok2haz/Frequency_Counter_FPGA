"""ab_report.py - vyhodnoceni A/B testu dvou behu (napr. dva bitstreamy) ze surovych mereni (`tools/raw_fetch.py`).

Pouziti:
    python tools/ab_report.py novy=raw_novy.csv stary=raw_stary.csv [--skip 2700] [--seg 1800]
Kazdy beh se rozdeli na useky po --seg radcich (1 mereni = 1 s) PO preskoceni prvnich --skip radku (zahrati OCXO, vychozi 45 min).
Pro kazdy usek se vypise overlapping Allanova odchylka pri tau = 1, 16 a 64 s, pro kazdy beh median a rozptyl useku, a pomer medianu
prvniho behu ku druhemu. Prvni soubor v poradi = "A" (testovany), druhy = "B" (srovnavaci).

Jak cist vysledek: pomer < 1,3 je v rozptylu mezi useky jednoho behu (kolisani zdroje, denni doba); trvale > 1,5 ve vsech tau a v obou
behich (A, B, A, B) je skutecny rozdil. Jeden par behu nestaci - je-li zdroj (externi GPSDO) jiny v noci a ve dne, porovnava se stejna
denni doba nebo se strida A-B-A-B.
"""
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adev_compare as ac


def segments(path, skip, seg):
    ts, fs = ac.load(path)
    y = [(f - 1e7) / 1e7 for f in fs]
    out = []
    i = skip
    while i + seg <= len(y):
        s = y[i:i + seg]
        r = []
        for m in (1, 16, 64):
            a, _ = ac.adev_overlap(s, m)
            r.append(a)
        out.append((i, ts[i] / 3600.0, r))
        i += seg
    return len(y), out


def main():
    args = sys.argv[1:]
    skip, seg = 2700, 1800
    pairs = []
    i = 0
    while i < len(args):
        if args[i] == '--skip':
            skip = int(args[i + 1]); i += 2
        elif args[i] == '--seg':
            seg = int(args[i + 1]); i += 2
        else:
            k, _, v = args[i].partition('=')
            pairs.append((k, v)); i += 1
    if len(pairs) < 2:
        print(__doc__); sys.exit(1)
    meds = {}
    nseg = {}
    for name, path in pairs:
        n, segs = segments(path, skip, seg)
        print('=== %s (%s): %d radku, useku %d (po %d s, bez prvnich %d) ===' % (name, path, n, len(segs), seg, skip))
        for i0, h, r in segs:
            print('   radky %6d (uptime ~%4.1f h)  ADEV1=%.2e  ADEV16=%.2e  ADEV64=%.2e' % (i0, h, r[0], r[1], r[2]))
        nseg[name] = len(segs)
        if segs:
            meds[name] = [statistics.median(s[2][k] for s in segs) for k in range(3)]
            lo = [min(s[2][k] for s in segs) for k in range(3)]
            hi = [max(s[2][k] for s in segs) for k in range(3)]
            print('   median          ADEV1=%.2e  ADEV16=%.2e  ADEV64=%.2e   rozsah useku: %.1e-%.1e / %.1e-%.1e / %.1e-%.1e' %
                  (meds[name][0], meds[name][1], meds[name][2], lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
        else:
            print('   (zadny usek - behu chybi data po preskoceni zahrati)')
    names = [p[0] for p in pairs]
    if names[0] in meds and names[1] in meds:
        a, b = meds[names[0]], meds[names[1]]
        print('\nPOMER medianu %s / %s: ADEV1 %.2f  ADEV16 %.2f  ADEV64 %.2f' % (names[0], names[1], a[0] / b[0], a[1] / b[1], a[2] / b[2]))
        if min(nseg[names[0]], nseg[names[1]]) < 3:
            print('POZOR: aspon jeden beh ma mene nez 3 useky (%d a %d) - median je nahodny; pockej a stahni dalsi data.' % (nseg[names[0]], nseg[names[1]]))
        worst = max(a[k] / b[k] for k in range(3))
        best = min(a[k] / b[k] for k in range(3))
        if worst < 1.3:
            print('Vyhodnoceni: v rozptylu useku, rozdil %s vs %s neprokazan.' % (names[0], names[1]))
        elif best > 1.5:
            print('Vyhodnoceni: %s je trvale horsi nez %s ve vsech tau (>= 1,5x). Opakuj A-B-A-B, abys vyloucil denni dobu zdroje.' % (names[0], names[1]))
        else:
            print('Vyhodnoceni: rozdil je hranicni; pridej dalsi dvojici behu (A-B-A-B).')


if __name__ == '__main__':
    main()
