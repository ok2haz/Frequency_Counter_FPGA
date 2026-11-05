"""Vyhodnoceni testu NAV5 Stationary vs Portable (xr_ns = faze OCXO proti nasi GPS). python tools/dlouhe_nav5.py <predpona>"""
import csv
import sys

import numpy as np

pre = sys.argv[1]
T = 0.25
rows = list(csv.DictReader(open(pre + "_faze2_gps.csv", encoding="ascii", errors="replace")))
B = list(csv.DictReader(open(pre + ".blocks.csv")))


def col(k):
    out = []
    for r in rows:
        try:
            out.append(float(r[k]))
        except (ValueError, TypeError):
            out.append(np.nan)
    return np.array(out)


seq = col("seq_meas"); xr = col("gp_xr_ns"); wall = [r["wall"] for r in rows]
ok = np.isfinite(seq) & np.isfinite(xr)
seq, xr = seq[ok].astype(np.int64), xr[ok]
wall = [w for w, o in zip(wall, ok) if o]
_, first = np.unique(seq, return_index=True)
ix = np.flatnonzero(np.ones(len(seq), bool))  # already unique per row mostly; dedupe by seq below
order = np.argsort(seq, kind="stable")
seq = seq[order]; xr = xr[order]; wall = [wall[i] for i in order]
_, fi = np.unique(seq, return_index=True)
fi = np.sort(fi)
seq, xr = seq[fi], xr[fi]; wall = [wall[i] for i in fi]
t_row = (seq - seq[0]) * T


def adev_x(x, m):
    if len(x) < 2 * m + 2:
        return np.nan
    d = x[2 * m:] - 2 * x[m:-m] + x[:-2 * m]
    return np.sqrt(np.mean(d ** 2) / (2.0 * m ** 2))


dxr = np.diff(xr); dxr[np.abs(dxr) > 300] = 0.0
xc = np.concatenate(([0.0], np.cumsum(dxr))) * 1e-9
tg = np.arange(0, t_row[-1], 1.0)
P = np.interp(tg, t_row, xc)

taus = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000]
print("=== CELEK (vsechny bloky, informativne) ===")
print("tau[s]   " + "  ".join("%9g" % t for t in taus))
print("ADEV R   " + "  ".join("%.3e" % adev_x(P, t) for t in taus))

print("\n=== po blocich (NS = Stationary, NA = Portable), skip 15 min ===")
print("blok  start               hodin  ADEV R: " + "  ".join("%9gs" % t for t in taus))
res = {}
for b in B:
    t0 = (np.datetime64(b["wall"]) - np.datetime64(wall[0])).astype("timedelta64[s]").astype(float) + 60 * float(b["skip_min"])
    t1 = (np.datetime64(b["wall"]) - np.datetime64(wall[0])).astype("timedelta64[s]").astype(float) + 60 * float(b["delka_min"]) - 60
    i0, i1 = int(t0), min(int(t1), len(tg) - 1)
    if i1 - i0 < 2000:
        continue
    seg = P[i0:i1] - P[i0]
    ad = [adev_x(seg, t) for t in taus]
    res.setdefault(b["blok"], []).append(ad)
    print("%-4s  %s   %4.1f   " % (b["blok"], b["wall"], (i1 - i0) / 3600.0) + "  ".join("%.3e" % a for a in ad))

print("\n=== souhrn: geometricky prumer pres bloky stejneho typu ===")
print("typ   n   " + "  ".join("%9gs" % t for t in taus))
for nm in ("NS", "NA"):
    if nm not in res:
        continue
    arr = np.array(res[nm])
    gm = np.exp(np.nanmean(np.log(arr), axis=0))
    print("%-4s  %d   " % (nm, len(res[nm])) + "  ".join("%.3e" % v for v in gm))

if "NS" in res and "NA" in res:
    ns = np.exp(np.nanmean(np.log(np.array(res["NS"])), axis=0))
    na = np.exp(np.nanmean(np.log(np.array(res["NA"])), axis=0))
    print("\npomer NS/NA (< 1 = stationary lepsi):")
    print("  " + "  ".join("%.2f" % r for r in ns / na))
