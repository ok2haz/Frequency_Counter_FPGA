"""Hodinovy prubeh fáze 2: ADEV A, R (xr) a D, pocet skoku, teploty. python tools/dlouhe_hodiny.py <predpona>
Slouzi k odhaleni hodin, kdy se zhorsil zdroj (A i D), nebo nase GPS (R), nebo OCXO."""
import csv
import sys

import numpy as np

pre = sys.argv[1]
T = 0.25
rows = list(csv.DictReader(open(pre + "_faze2_gps.csv", encoding="ascii", errors="replace")))
need = ("seq_meas", "freq_hz", "gp_xr_ns", "t_ocxo", "gps_sats", "gps_hdop", "wall")
seq = np.array([float(r["seq_meas"]) if r["seq_meas"] not in ("", "NA") else np.nan for r in rows])
f = np.array([float(r["freq_hz"]) if r["freq_hz"] not in ("", "NA") else np.nan for r in rows])
xr = np.array([float(r["gp_xr_ns"]) if r["gp_xr_ns"] not in ("", "NA") else np.nan for r in rows])
to = np.array([float(r["t_ocxo"]) if r["t_ocxo"] not in ("", "NA") else np.nan for r in rows])
ns = np.array([float(r["gps_sats"]) if r["gps_sats"] not in ("", "NA") else np.nan for r in rows])
hd = np.array([float(r["gps_hdop"]) if r["gps_hdop"] not in ("", "NA") else np.nan for r in rows])
wall = [r["wall"][11:16] for r in rows]
ok = np.isfinite(seq) & np.isfinite(f)
_, first = np.unique(seq[ok], return_index=True)
ix = np.flatnonzero(ok)[np.sort(first)]
seq = seq[ix].astype(np.int64); f = f[ix]; xr = xr[ix]; to = to[ix]; ns = ns[ix]; hd = hd[ix]; wall = [wall[i] for i in ix]
g = np.arange(seq[0], seq[-1] + 1)
y = np.interp(g, seq, (f - 1e7) / 1e7)


def adev_y(v, m):
    c = np.cumsum(np.concatenate(([0.0], v)))
    yb = (c[m:] - c[:-m]) / m
    dd = yb[m:] - yb[:-m]
    return np.sqrt(0.5 * np.mean(dd ** 2)) if len(dd) > 2 else np.nan


def adev_x(x, m):
    if len(x) < 2 * m + 2:
        return np.nan
    d = x[2 * m:] - 2 * x[m:-m] + x[:-2 * m]
    return np.sqrt(np.mean(d ** 2) / (2.0 * m ** 2))


# xr na 1 s siti (preruseni odstranit)
t_row = (seq - seq[0]) * T
m = np.isfinite(xr)
dx = np.diff(xr[m]); dx[np.abs(dx) > 200] = 0
xc = np.concatenate(([0.0], np.cumsum(dx))) * 1e-9
tg = np.arange(0, t_row[m][-1], 1.0)
Pr = np.interp(tg, t_row[m], xc)
xA = np.concatenate(([0.0], np.cumsum(y[:-1] * T)))
PA = np.interp(tg, (g - g[0]) * T, xA)
D = PA + Pr
core = 1.4826 * np.median(np.abs(np.diff(y) - np.median(np.diff(y))))
big = np.abs(np.diff(y)) > 6 * core
print("hodina   cas     ADEV A(1s)  A(10s)   R(1s)    R(10s)   D(10s)  skoku%%  sat  hdop  T_ocxo   (jadro %.2e)" % core)
nh = int(len(y) * T // 3600)
for h in range(nh):
    a = int(h * 3600 / T); b = int((h + 1) * 3600 / T)
    s1 = int(h * 3600); s2 = int((h + 1) * 3600)
    yy = y[a:b]
    print("%3d    %s   %.2e  %.2e  %.2e  %.2e  %.2e  %5.2f   %4.1f  %4.2f  %.2f" %
          (h, wall[min(len(wall) - 1, int(np.searchsorted(seq, seq[0] + a)))] if len(wall) else "", adev_y(yy, 4), adev_y(yy, 40),
           adev_x(Pr[s1:s2], 1), adev_x(Pr[s1:s2], 10), adev_x(D[s1:s2], 10), 100 * np.mean(big[a:b - 1]), np.nanmean(ns[(seq >= seq[0] + a) & (seq < seq[0] + b)]),
           np.nanmean(hd[(seq >= seq[0] + a) & (seq < seq[0] + b)]), np.nanmean(to[(seq >= seq[0] + a) & (seq < seq[0] + b)])))
