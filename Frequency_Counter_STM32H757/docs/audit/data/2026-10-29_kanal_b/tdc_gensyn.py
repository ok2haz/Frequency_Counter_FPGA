"""Synteticky ab.csv pro overeni tdc_gensweep.py: znamy tvar E_A, E_B, sum 3 ps na okno, jedna dira.
python -I tdc_gensyn.py <out.csv> <mHz>   -> vypise P1 P2 P3 R pro tdc_gensweep.py, ulozi <out>_trueA/B.npy
Overeni: tdc_gensweep.py <out.csv> --a <P1 P2 P3 R> --out X, pak tdc_dcmp.py X_A.npy <out>_trueA.npy -> korelace 1,000.
(Registry tady z Fraction.limit_denominator, firmware pouziva sblizeny zlomek -- pro syntetiku je podstatna jen
shoda mezi generatorem a rozborem.)"""
import csv, math, sys
from fractions import Fraction
import numpy as np
out = sys.argv[1]; mhz = int(sys.argv[2])
P = 10000.0; NSEG = 10; ci = 10.0 * (np.arange(NSEG) - 4.5) / 82.5
x = Fraction(2200000000 * 128 * 1000, 10000000 * 1000 + mhz)        # 128 * MS
q = x.numerator // x.denominator
fr = (x - q).limit_denominator(2**30 - 1)
P1, P2, P3, R = q - 512, fr.numerator, fr.denominator, 1
FT = Fraction(2200000000 * 128) * P3 / ((P1 + 512) * P3 + P2) / R
ft = float(FT); rel = float((FT - Fraction(10**7)) / 10**7)
EA = lambda p: 60 * np.cos(2 * math.pi * p / P) + 25 * np.sin(4 * math.pi * p / P) + 10 * np.cos(6 * math.pi * p / P + 1)
EB = lambda p: 40 * np.sin(2 * math.pi * p / P + 0.5) - 15 * np.cos(8 * math.pi * p / P)
rng = np.random.default_rng(5)
gp = 0.25e12
with open(out, 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['t_unix', 'seq', 'f_primary', 'f_secondary', 'gate_ns', 'channel', 'signal_lost'])
    for s in range(14400):
        if 5000 <= s < 5010:
            continue                                   # dira
        phs = s * rel * gp + 1234.0
        phi = phs + (np.arange(NSEG) + 0.5) / NSEG * rel * gp
        ea = EA(phi) @ ci + rng.normal(0, 3); eb = EB(phi) @ ci + rng.normal(0, 3)
        gn = 250000000 - (1 if rng.random() < 0.5 else 0)
        w.writerow([0, s, '%.9f' % (ft * (1 + ea / gp)), '%.9f' % (ft * (1 + eb / gp)), gn, 0, False])
g = np.linspace(0, P, 400, endpoint=False) + 1234.0
np.save(out.replace('.csv', '_trueA.npy'), EA(g) - EA(g).mean())
np.save(out.replace('.csv', '_trueB.npy'), EB(g) - EB(g).mean())
print(P1, P2, P3, R)
