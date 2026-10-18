"""Vyhodnoceni tools/ab_capture.py: ADEV A, B a A-B (spolecny sum zdroje se odecte), korelace skoku. python tools/ab_analyze.py <soubor.npz>"""
import sys
import numpy as np

d = np.load(sys.argv[1])
seq, A, B = d["seq"], d["A"], d["B"]
print("oken %d, rozsah seq %d..%d" % (len(seq), seq[0], seq[-1]))
# souvisla mrizka
g = np.arange(seq[0], seq[-1] + 1)
okm = np.zeros(len(g), bool)
okm[seq - seq[0]] = True
yA = np.interp(g, seq, (A - 1e7) / 1e7)
yB = np.interp(g, seq, (B - 1e7) / 1e7)
print("chybi %d oken (%.1f %%)" % ((~okm).sum(), 100 * (~okm).mean()))
T = 0.25


def adev(y, m):
    c = np.cumsum(np.concatenate(([0.0], y)))
    yb = (c[m:] - c[:-m]) / m
    dd = yb[m:] - yb[:-m]
    return np.sqrt(0.5 * np.mean(dd ** 2))


def mad_sigma(x):
    return 1.4826 * np.median(np.abs(x - np.median(x)))


# kanal B ma rozliseni jen 1e-5 Hz = 1e-12 rel.; ukaz pocet unikatnich hodnot
print("unikatni hodnoty B: %d, A: %d" % (len(np.unique(B)), len(np.unique(A))))
print("tau[s]   ADEV(A)    ADEV(B)    ADEV(A-B)   [A-B: spolecny sum zdroje se odecte]")
for m in (1, 2, 4, 8, 40):
    print("%6.2f  %.3e  %.3e  %.3e" % (m * T, adev(yA, m), adev(yB, m), adev(yA - yB, m)))
dA, dB = np.diff(yA), np.diff(yB)
print("\nrobustni sigma rozdilu sousednich oken: A %.2e  B %.2e  (A-B) %.2e" % (mad_sigma(dA), mad_sigma(dB), mad_sigma(dA - dB)))
print("RMS rozdilu: A %.2e  B %.2e  (A-B) %.2e" % (np.sqrt(np.mean(dA ** 2)), np.sqrt(np.mean(dB ** 2)), np.sqrt(np.mean((dA - dB) ** 2))))
cc = np.corrcoef(dA, dB)[0, 1]
print("korelace rozdilu A vs B (spolecny zdroj => +): %.3f" % cc)
big = np.abs(dA) > 6 * mad_sigma(dA)
bigB = np.abs(dB) > 6 * mad_sigma(dB)
print("velke skoky A: %d, B: %d, soucasne v A i B: %d" % (big.sum(), bigB.sum(), (big & bigB).sum()))
print("rozdil strednich hodnot A-B: %.3e (rel)" % np.mean(yA - yB))
