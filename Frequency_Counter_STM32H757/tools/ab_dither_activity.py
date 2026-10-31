"""Souvisi skoky v kanalu A s aktivitou ditheru DAC?
python tools/ab_dither_activity.py <csv z gpsdo_log.ps1> [orez_s]
Dither 1. radu zapisuje kod jen pri zmene; podil zapisu je ~ 2*min(zlomek, 1-zlomek) (pri zlomku 0 se nezapisuje nic,
pri 0,5 se meni kazdy krok). Ze zlomku kodu (sloupec gp_code) a z toho, jestli je okno 'skok', spocte cetnost skoku pro bin aktivity.
Smysluplne jen pro behy se smyckou v LOCK (kod se meni) a EXTERNIM zdrojem na kanalu A (na vlastni reference se Vc neprojevi)."""
import csv
import sys

import numpy as np

fn = sys.argv[1]
cut = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
rows = list(csv.DictReader(open(fn, encoding="ascii", errors="replace")))


def col(k):
    a = []
    for r in rows:
        try:
            a.append(float(r[k]))
        except (ValueError, TypeError):
            a.append(np.nan)
    return np.array(a)


seq, f, code, st = col("seq_meas"), col("freq_hz"), col("gp_code"), col("gp_locked")
ok = np.isfinite(seq) & np.isfinite(f) & np.isfinite(code)
seq, f, code, st = seq[ok], f[ok], code[ok], st[ok]
_, first = np.unique(seq, return_index=True)
first = np.sort(first)
seq, f, code, st = seq[first].astype(np.int64), f[first], code[first], st[first]
T = 0.25
y = (f - 1e7) / 1e7
d = np.diff(y)
valid = np.diff(seq) == 1
core = 1.4826 * np.median(np.abs(d[valid] - np.median(d[valid])))
big = np.abs(d) > 6 * core
frac = code[1:] - np.floor(code[1:])
act = 2.0 * np.minimum(frac, 1.0 - frac)
m = valid & (seq[1:] - seq[0] >= cut / T) & (st[1:] > 0.5)
print("oken %d (souvisle, LOCK), jadro %.2e, skoku %.2f %%" % (m.sum(), core, 100 * big[m].mean()))
edges = [0, 0.05, 0.1, 0.2, 0.4, 0.7, 1.0001]
print("aktivita ditheru (podil kroku se zapisem)   oken   skoku   podil[%]   RMS rozdilu")
for lo, hi in zip(edges[:-1], edges[1:]):
    s = m & (act >= lo) & (act < hi)
    if s.sum() < 50:
        continue
    print("  %.2f - %.2f   %7d  %6d   %6.2f    %.2e" % (lo, hi, s.sum(), (big & s).sum(), 100 * big[s].mean(), np.sqrt(np.mean(d[s] ** 2))))
lo_s = m & (act < 0.1)
hi_s = m & (act >= 0.4)
if lo_s.sum() > 100 and hi_s.sum() > 100:
    p1, p2 = big[lo_s].mean(), big[hi_s].mean()
    n1, n2 = lo_s.sum(), hi_s.sum()
    p = (big[lo_s].sum() + big[hi_s].sum()) / (n1 + n2)
    z = (p2 - p1) / np.sqrt(p * (1 - p) * (1 / n1 + 1 / n2))
    print("\nmala aktivita (<0,1): %.2f %% (%d oken); velka (>=0,4): %.2f %% (%d oken); z = %.1f" % (100 * p1, n1, 100 * p2, n2, z))
