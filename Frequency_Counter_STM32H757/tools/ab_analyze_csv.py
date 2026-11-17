"""Vyhodnoceni CSV z tools/gpsdo_log.ps1 (sloupce freq_hz = kanal A, freq_b_hz = kanal B, gp_*, vc_mv, t_ocxo).
python tools/ab_analyze_csv.py <soubor.csv> [orez_s_na_zacatku]
ADEV A, B, A-B; casovy prubeh skoku (rozhodne, jestli jde o nabeh); vazba na Vc a teplotu OCXO."""
import sys
import numpy as np
import csv

fn = sys.argv[1]
cut = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
def load(fn):
    rows = list(csv.DictReader(open(fn, encoding="ascii", errors="replace")))
    cols = {}
    for k in rows[0]:
        a = []
        for r in rows:
            try:
                a.append(float(r[k]))
            except (ValueError, TypeError):
                a.append(np.nan)
        cols[k] = np.array(a)
    return cols


D = load(fn)
ok = np.isfinite(D["seq_meas"]) & np.isfinite(D["freq_hz"])
_, first = np.unique(D["seq_meas"][ok], return_index=True)
idx = np.flatnonzero(ok)[np.sort(first)]
D = {k: v[idx] for k, v in D.items()}
print("radku %d, seq %d..%d, gate_s %s" % (len(idx), D["seq_meas"][0], D["seq_meas"][-1], np.unique(D["gate_s"][np.isfinite(D["gate_s"])])[:3]))
seq = D["seq_meas"].astype(np.int64)
miss = np.diff(seq) - 1
print("diry v seq: %d mist, chybi %d mereni (%.3f %%)" % ((miss > 0).sum(), miss[miss > 0].sum(), 100.0 * miss[miss > 0].sum() / (seq[-1] - seq[0] + 1)))
T = 0.25
t = (seq - seq[0]) * T
yA = (D["freq_hz"] - 1e7) / 1e7
hasB = "freq_b_hz" in D and np.isfinite(D["freq_b_hz"]).sum() > 100
yB = (D["freq_b_hz"] - 1e7) / 1e7 if hasB else None
print("A: stred %.3e, B: stred %s" % (np.mean(yA), ("%.3e" % np.nanmean(yB)) if hasB else "-"))

# souvisla mrizka (diry se dointerpoluji)
g = np.arange(seq[0], seq[-1] + 1)
tg = (g - seq[0]) * T
yAg = np.interp(g, seq, yA)
yBg = np.interp(g, seq, yB) if hasB else None
i0 = int(cut / T)


def adev(y, m):
    c = np.cumsum(np.concatenate(([0.0], y)))
    yb = (c[m:] - c[:-m]) / m
    dd = yb[m:] - yb[:-m]
    return np.sqrt(0.5 * np.mean(dd ** 2)) if len(dd) > 2 else np.nan


def mad(x):
    return 1.4826 * np.median(np.abs(x - np.median(x)))


print("\n== ADEV (celek po orezu %.0f s) ==" % cut)
print("tau[s]     ADEV(A)     ADEV(B)    ADEV(A-B)")
for m in (1, 2, 4, 8, 20, 40, 80, 200, 400, 800, 1600):
    a = adev(yAg[i0:], m)
    b = adev(yBg[i0:], m) if hasB else np.nan
    ab = adev(yAg[i0:] - yBg[i0:], m) if hasB else np.nan
    print("%7.2f  %.3e  %.3e  %.3e" % (m * T, a, b, ab))

dA = np.diff(yAg[i0:])
sA = mad(dA)
big = np.abs(dA) > 6 * sA
print("\n== Rozdily sousednich oken A ==")
print("jadro (MAD sigma) %.2e, RMS %.2e, RMS/jadro %.1f" % (sA, np.sqrt(np.mean(dA ** 2)), np.sqrt(np.mean(dA ** 2)) / sA))
print("skoku > 6x jadro: %d (%.2f %% oken)" % (big.sum(), 100 * big.mean()))
share = np.sum(dA[big] ** 2) / np.sum(dA ** 2)
print("podil rozptylu v techto skocich: %.1f %%" % (100 * share))
print("velikost skoku (medián |skok|, relativne): %.2e" % np.median(np.abs(dA[big])) if big.any() else "bez skoku")

print("\n== Casovy prubeh skoku A (po 10 min) ==")
tt = tg[i0 + 1:]
for k in range(0, int(tt[-1] // 600) + 1):
    msk = (tt >= k * 600) & (tt < (k + 1) * 600)
    if msk.sum() < 100:
        continue
    print("%4d-%4d min: skoku %4d z %5d (%.2f %%), jadro %.2e, RMS %.2e" %
          (k * 10, (k + 1) * 10, big[msk].sum(), msk.sum(), 100 * big[msk].mean(), mad(dA[msk]), np.sqrt(np.mean(dA[msk] ** 2))))

if hasB:
    dB = np.diff(yBg[i0:])
    bigB = np.abs(dB) > 6 * mad(dB)
    print("\n== B ==")
    print("jadro %.2e, skoku %d (%.2f %%), soucasne s A: %d, korelace dA/dB %.3f" %
          (mad(dB), bigB.sum(), 100 * bigB.mean(), (big & bigB).sum(), np.corrcoef(dA, dB)[0, 1]))

print("\n== Okolnosti (stred za celek) ==")
for c in ("gp_locked", "vc_mv", "dac_code", "t_ocxo", "t_board", "gps_sats"):
    if c in D and np.isfinite(D[c]).any():
        v = D[c][np.isfinite(D[c])]
        print("%-10s min %s  stred %.3f  max %s" % (c, v.min(), v.mean(), v.max()))
