"""Zavisi vznik skoku v kanalu A na fazi vstupni hrany vuci hodinam 100 MHz?
python tools/ab_phase_jumps.py <csv z gpsdo_log.ps1> [orez_s]
Fazi hrany vuci hodinam odvodime ze zmerene relativni odchylky y: x(t) = suma y*T (casovy posun vstupu proti OCXO), modulo 10 ns.
Je-li pomer skoku na faze rovnomerny, faze s chybou nesouvisi. Pri vlastni referenci (y = 0) faze stoji a test nema smysl."""
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


seq, f = col("seq_meas"), col("freq_hz")
ok = np.isfinite(seq) & np.isfinite(f)
seq, f = seq[ok], f[ok]
_, first = np.unique(seq, return_index=True)
first = np.sort(first)
seq, f = seq[first].astype(np.int64), f[first]
T = 0.25
g = np.arange(seq[0], seq[-1] + 1)
y = np.interp(g, seq, (f - 1e7) / 1e7)
have = np.zeros(len(g), bool)
have[seq - seq[0]] = True
d = np.diff(y)
core = 1.4826 * np.median(np.abs(d - np.median(d)))
big = np.abs(d) > 6 * core
valid = have[1:] & have[:-1]          # skok jen mezi dvema SKUTECNYMI okny
i0 = int(cut / T)
big[:i0] = False
valid[:i0] = False

x = np.cumsum(y) * T                  # posun hrany [s] proti hodinam (neznamy pocatek)
PER = 1e-8
ph = (x[1:] % PER) / PER              # 0..1 (jedna perioda 100 MHz)
nb = 20
bins = np.minimum((ph * nb).astype(int), nb - 1)
tot = np.bincount(bins[valid], minlength=nb)
jm = np.bincount(bins[valid & big], minlength=nb)
print("oken pro test %d, skoku %d (%.2f %%), stred y %.3e, posun hrany %.1f ns za zaznam (%.1f period hodin)" %
      (valid.sum(), (valid & big).sum(), 100 * (valid & big).sum() / max(valid.sum(), 1), np.mean(y), x[-1] * 1e9, x[-1] / PER))
print("faze  oken  skoku  podil[%]")
for k in range(nb):
    print("%4.2f %5d %5d  %5.2f" % ((k + 0.5) / nb, tot[k], jm[k], 100.0 * jm[k] / max(tot[k], 1)))
exp = tot * (valid & big).sum() / max(valid.sum(), 1)
chi = np.sum((jm - exp) ** 2 / np.maximum(exp, 1e-9))
print("chi2 vs rovnomerne rozlozeni: %.1f (19 stupnu volnosti; > 30 = nerovnomerne s p < 0,05)" % chi)
