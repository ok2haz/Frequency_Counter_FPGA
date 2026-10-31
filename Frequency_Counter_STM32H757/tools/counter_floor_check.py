"""Skutecny sum JEDNOHO okna citace z dlouheho logu (gpsdo_log.ps1): ADEV(0,25 s .. 100 s) proti modelu 105 ps a struktura chvostu.

Pouziti: python tools/counter_floor_check.py [csv|csv.gz]. Odpoved na otazku „da se posunout posledni platna cislice?“ (2026-10-18):
horni mez sumu citace vcetne zdroje; zdroj (externi GPSDO) ji prekryva.
"""
import csv
import gzip
import numpy as np

P = r"C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\docs\audit\data\2026-10-18_noc10h.csv.gz"
rows = list(csv.DictReader(gzip.open(P, "rt", encoding="utf-8")))
seq = np.array([float(r["seq_meas"]) if r["seq_meas"] != "NA" else np.nan for r in rows])
f = np.array([float(r["freq_hz"]) if r["freq_hz"] != "NA" else np.nan for r in rows])
t = np.array([np.datetime64(r["wall"]) for r in rows])
ok = np.isfinite(seq) & np.isfinite(f)
seq, f = seq[ok].astype(np.int64), f[ok]
t0 = 3600 * 4  # preskoc rozbeh
sel = seq >= seq[0] + int(3600 / 0.25)
seq, f = seq[sel], f[sel]
g = np.arange(seq[0], seq[-1] + 1)
y = np.interp(g, seq, (f - 1e7) / 1e7)
miss = np.ones(len(g), bool)
miss[seq - seq[0]] = False
print("oken", len(y), "chybi", miss.sum())
T = 0.25
# ADEV(tau = m*T) z prumeru m oken (overlapping), jen kde nechybi
def adev(m):
    c = np.cumsum(np.concatenate(([0.0], y)))
    yb = (c[m:] - c[:-m]) / m
    d = yb[m:] - yb[:-m]
    return np.sqrt(0.5 * np.mean(d ** 2)), len(d)
print("tau[s]   ADEV        model(105 ps)  pomer   implikovane sigma_znacky [ps] (horni mez: veskery sum = citac)")
for m in (1, 2, 4, 8, 16, 40, 80, 400):
    a, n = adev(m)
    tau = m * T
    mod = np.sqrt(3.0) * 105e-12 / tau
    print("%7.2f  %.3e  %.3e  %5.2f   %6.1f" % (tau, a, mod, a / mod, a * tau / np.sqrt(3.0) * 1e12))
# rozdily sousednich oken (vysoke frekvence): ADEV(0.25) primo
d1 = np.diff(y)
print("ADEV(0,25 s) z rozdilu sousednich oken: %.3e" % (np.sqrt(0.5 * np.mean(d1 ** 2))))
# robustni (MAD) odhad stejneho: odolny proti pomale slozce i odlehlym
mad = 1.4826 * np.median(np.abs(d1 - np.median(d1)))
print("totez robustne (MAD rozdilu): %.3e  => sigma_znacky <= %.1f ps" % (mad / np.sqrt(2.0), mad / np.sqrt(2.0) * T / np.sqrt(3.0) * 1e12))
# korelace sousednich oken (sdilene znacky => -0,5)
yy = y - np.mean(y)
r1 = np.mean(yy[1:] * yy[:-1]) / np.mean(yy * yy)
dd = d1 - np.mean(d1)
print("autokorelace rozdilu lag1: %.3f (bily sum nezavisly na oknech => -0,5; sdilene znacky => 0)" % (np.mean(dd[1:] * dd[:-1]) / np.mean(dd * dd)))

# ---- struktura chvostu
core = mad / np.sqrt(2.0) * np.sqrt(2.0)        # sigma rozdilu (jadro)
big = np.abs(d1) > 6 * core
print("\n|rozdil| > 6x jadro: %.2f %% oken; RMS rozdilu %.2e vs jadro %.2e" % (100 * big.mean(), np.sqrt(np.mean(d1 ** 2)), core))
ph = (g[1:] % 4)
print("fáze (seq mod 4) velkych rozdilu:", [int(((ph == k) & big).sum()) for k in range(4)], " vsech oken v fazi:", [int((ph == k).sum()) for k in range(4)])
# prispevek chvostu k rozptylu
tailvar = np.sum(d1[big] ** 2) / np.sum(d1 ** 2)
print("podil chvostu (>6x jadro) na celkovem rozptylu rozdilu: %.1f %%" % (100 * tailvar))
# ADEV bez chvostu (clip na 6x jadro)
dc = np.clip(d1, -6 * core, 6 * core)
print("ADEV(0,25 s) po orezu chvostu: %.3e (jadro %.3e)" % (np.sqrt(0.5 * np.mean(dc ** 2)), core / np.sqrt(2.0)))
# izolovane (jednovzorkove) vs sled
idx = np.where(big)[0]
iso = np.sum(np.diff(idx) > 2) if len(idx) > 1 else 0
print("velkych rozdilu: %d, z toho izolovanych (dalsi nejdrive za >2 okna): %d" % (len(idx), iso))
# kolik oken ma presne stejnou hodnotu jako predchozi (kvantizace/koherence)
print("okna s IDENTICKOU hodnotou jako predchozi: %.1f %%" % (100 * np.mean(d1 == 0)))
vals = np.unique(np.round(np.diff(y) * 1e10))
print("rozlisitelnych hodnot rozdilu na 1e-10 (hrube):", len(vals))
