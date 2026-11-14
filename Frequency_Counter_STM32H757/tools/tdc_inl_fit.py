# Odhad INL (chyby kalibracni tabulky) TDC z oken zaznamenanych firmwarem (UART `inl dump`, FW >= 0x0418).
#
# Model: chyba okna i (mezi uzaviraci hranou i-1 a i):
#     gate_ps_i - N_i/f0 = -(N_i/f0) * eps_i  -  e[code_end_i]  +  e[code_st_i]  +  sum
# eps_i = pomala relativni zmena kmitoctu (po useku linearni mezi uzly), e[c] = chyba casu tabulky pro kod c [ps].
# Pouziti:
#   python tools/tdc_inl_fit.py inl.txt [--knot 40] [--ridge 1.0] [--out e.csv]
import sys, re
import numpy as np

def arg(n, d):
    return sys.argv[sys.argv.index(n) + 1] if n in sys.argv else d

txt = open(sys.argv[1], errors='ignore').read()
rows = [tuple(int(x) for x in m.groups()) for m in re.finditer(r'^(\d+);(\d+);(\d+);(\d+);(\d+);(\d+)\s*$', txt, re.M)]
if len(rows) < 50:
    sys.exit('malo radku: %d' % len(rows))
seq = np.array([r[0] for r in rows]); N = np.array([r[1] for r in rows], float)
G = np.array([r[2] for r in rows], float); cs = np.array([r[3] for r in rows]); ce = np.array([r[4] for r in rows])
gap = np.array([r[5] for r in rows])
# jen okna bez diry v SEQ (gap == 0); ztracene radky vypisu jsou na vysledku nezavisle (kazdy radek nese oba kody)
ok = (gap == 0)
f0 = np.median(N / G * 1e12)
y = G - N / f0 * 1e12
print('oken %d, pouzito %d, f0 = %.6f Hz, hrub. sigma y = %.0f ps' % (len(rows), ok.sum(), f0, np.std(y[ok])))
used = sorted(set(cs[ok]) | set(ce[ok]))
idx = {c: k for k, c in enumerate(used)}
nc = len(used); n = len(rows)
knot = int(arg('--knot', '40')); lam = float(arg('--ridge', '1.0'))
tpos = (seq - seq[0]) / float(knot)
nk = int(tpos.max()) + 3
def build(sel):
    m = int(sel.sum())
    A = np.zeros((m, nc + nk)); b = y[sel].copy(); ii = np.where(sel)[0]
    for r, i in enumerate(ii):
        A[r, idx[ce[i]]] -= 1.0
        A[r, idx[cs[i]]] += 1.0
        t = tpos[i]; k0 = int(t); w = t - k0
        sc = -N[i] / f0 * 1e12 * 1e-9          # 1 jednotka = 1 ppb
        A[r, nc + k0] += sc * (1 - w); A[r, nc + k0 + 1] += sc * w
    return A, b
def fit(sel):
    A, b = build(sel)
    R = 1e-3 * np.eye(nc + nk); R[:nc, :nc] = lam * np.eye(nc)
    x = np.linalg.solve(A.T @ A + R, A.T @ b)
    return x, A, b
half = n // 2
sel1 = ok.copy(); sel1[half:] = False
sel2 = ok.copy(); sel2[:half] = False
x1, A1, b1 = fit(sel1)
# vyhodnoceni na DRUHE polovine: kmitocet (eps) se tam nezna -> dofituj jen eps na 2. polovine s pevnym e z 1. pulky
A2, b2 = build(sel2)
e_fixed = x1[:nc]
b2c = b2 - A2[:, :nc] @ e_fixed
Aeps = A2[:, nc:]
def eps_fit(Ae, bb):
    sol = np.linalg.lstsq(Ae.T @ Ae + 1e-6 * np.eye(Ae.shape[1]), Ae.T @ bb, rcond=None)[0]
    return bb - Ae @ sol
r_with = eps_fit(Aeps, b2c); r_without = eps_fit(Aeps, b2)
sd = lambda v: 1.4826 * np.median(np.abs(v - np.median(v)))
print('2. pulka (nevidena pri fitu):  sigma okna  bez korekce %.0f ps  |  s korekci e[kod] %.0f ps   (robustne MAD)' % (sd(r_without), sd(r_with)))
print('                                              std     bez korekce %.0f ps  |  s korekci %.0f ps' % (np.std(r_without), np.std(r_with)))
xa, Aa, ba = fit(ok)
e = xa[:nc]; e -= np.average(e)
print('odhad e[kod]: sigma %.0f ps, min %.0f max %.0f ps (%d kodu)' % (np.std(e), e.min(), e.max(), nc))
out = arg('--out', '')
if out:
    open(out, 'w').write('code;e_ps\n' + ''.join('%d;%.1f\n' % (c, e[idx[c]]) for c in used))
    print('ulozeno', out)
