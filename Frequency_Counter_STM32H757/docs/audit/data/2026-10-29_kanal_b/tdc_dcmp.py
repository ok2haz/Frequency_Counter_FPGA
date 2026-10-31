"""Porovnani tvaru D ze dvou behu (stejny bitstream): max korelace pres kruhovy posun, i zrcadlene."""
import sys
import numpy as np
a = np.load(sys.argv[1]); b = np.load(sys.argv[2]); G = len(a)
def best(x, y):
    c = [np.corrcoef(np.roll(x, s), y)[0, 1] for s in range(G)]
    s = int(np.argmax(c)); return c[s], s
c1, s1 = best(a, b)
c2, s2 = best(a[::-1], b)
print('korelace tvaru D: %.3f (posun %d/%d periody), zrcadlene %.3f' % (c1, s1, G, c2))
x = np.roll(a, s1)
k = np.dot(x, b) / np.dot(x, x)
print('pomer amplitud (2. beh / 1. beh) %.2f, rms rozdilu po srovnani %.1f ps z rms %.1f / %.1f ps' % (k, np.std(b - k * x), a.std(), b.std()))
