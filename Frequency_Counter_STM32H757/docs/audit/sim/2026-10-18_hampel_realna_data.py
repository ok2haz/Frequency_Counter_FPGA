"""Audit v0.27.0: filtr izolovanych odlehlych vzorku (Hampel 31 / 5 MAD) na SKUTECNYCH datech z desky.
Vzorek 1 s = prumer 4 po sobe jdoucich mereni 0,25 s (seq plynule). Port hamp_is_outlier/hamp_push z screen_main.c.
Pouziti: python 2026-10-18_hampel_realna_data.py <csv> [orez_s]"""
import csv, sys
import numpy as np

HN, HMIN, HK = 31, 15, 7.0   # od v0.27.1 (audit F-0255); puvodne 5.0
rows = list(csv.DictReader(open(sys.argv[1])))
T0 = float(sys.argv[2]) if len(sys.argv) > 2 else 1200.0
def num(v):
    try: return float(v)
    except Exception: return np.nan
wall = np.array([np.datetime64(r["wall"]) for r in rows])
t = (wall - wall[0]).astype("timedelta64[ms]").astype(float) / 1000.0
f = np.array([num(r["freq_hz"]) for r in rows])
def num(v):
    try: return float(v)
    except Exception: return np.nan
seq = np.array([num(r["seq_meas"]) for r in rows])
ok = (t >= T0) & np.isfinite(f) & np.isfinite(seq)
f, seq = f[ok], seq[ok]
# bloky po 4 souvislych seq
y = []
i = 0
while i + 3 < len(f):
    if np.all(np.diff(seq[i:i + 4]) == 1):
        y.append((f[i:i + 4].mean() - 1e7) / 1e7); i += 4
    else:
        i += 1
y = np.array(y)
print("%s: %d vzorku 1 s (%.2f h)" % (sys.argv[1].split("/")[-1], len(y), len(y) / 3600))

def med(a): return float(np.median(a))
def is_out(w, prev, P, nx):
    if len(w) < HMIN: return False
    m = med(w); mad = 1.4826 * med([abs(x - m) for x in w])
    if not mad > 0: return False
    T = HK * mad
    return abs(P - m) > T and abs(prev - m) < 0.5 * T and abs(nx - m) < 0.5 * T

w = []; kept = []; out = []; 
for k in range(len(y) - 1):
    P, nx = y[k], y[k + 1]
    prev = w[-1] if w else P
    if is_out(w, prev, P, nx): out.append(k)
    else: kept.append(P)
    w.append(P)
    if len(w) > HN: w.pop(0)
print("posouzeno %d, vyrazeno %d (%.4f %%)" % (len(y) - 1, len(out), 100.0 * len(out) / max(1, len(y) - 1)))

def adev(x, m):
    S = np.concatenate(([0.0], np.cumsum(x))); a = (S[m:] - S[:-m]) / m; d = a[m:] - a[:-m]
    return float(np.sqrt(0.5 * np.mean(d * d)))
print("  tau[s]   ADEV_surova   ADEV_filtr    pomer")
for m in (1, 2, 5, 10, 20, 50, 100, 200):
    if len(kept) > 4 * m:
        a, b = adev(y, m), adev(np.array(kept), m)
        print("  %5d   %.3e    %.3e    %.3f" % (m, a, b, b / a))
if out:
    print("  vyrazene vzorky: |y - med| v ppb:", [round((y[k] - np.median(y[max(0, k - 31):k] if k else y[:1])) * 1e9, 2) for k in out[:12]])
