"""Chyba casove znacky jednoho kanalu jako funkce polohy hrany v ramci 100 ns, signal z NEZNAMEHO zdroje (externi OCXO).

Na rozdil od tdc_gensweep.py pravda neni znama: kmitocet zdroje = vyhlazeny namereny kmitocet (klouzavy prumer pres
--smooth oken), faze hrany vuci taktu = integral (f_vyhl - f_nom) / f_nom. Chyba okna e = (f - f_vyhl) / f_vyhl * gate.
Vyhlazeni E(faze) prumeruje jen kdyz se faze za --smooth oken protoci pres mnoho period (posun za okno ~ desitky ns).
Fit E(faze mod P) Fourierem po blocich (--block oken), protoze integrovana faze pomalu blodi; tvary bloku se zarovnaji
kruhovym posunem na spolecny vzor (jako tdc_dtvar.py). Kontrola: totez se zamichanou fazi.
Segment okna pokryje sweep/10 faze -> harmonicke se v modelu prumeruji pres segment (sinc).

python -I tdc_extsweep.py <ab.csv> [--chan A|B] [--P 100000] [--K 30] [--block 1200] [--smooth 401]
       [--ref <npy z tdc_gensweep.py --P 100000 --out>] [--out <npy>]
--ref: porovna slozku vazanou na 100 ns (harmonicke k, ktere nejsou nasobek 10) s tvarem z behu s generatorem.
"""
import argparse, csv, math
import numpy as np

NSEG = 10
ci = 10.0 * (np.arange(NSEG) - 4.5) / 82.5

ap = argparse.ArgumentParser()
ap.add_argument('csv')
ap.add_argument('--chan', default='A')
ap.add_argument('--P', type=float, default=100000.0)
ap.add_argument('--K', type=int, default=30)
ap.add_argument('--block', type=int, default=1200)
ap.add_argument('--smooth', type=int, default=401)
ap.add_argument('--ref')
ap.add_argument('--out')
a = ap.parse_args()
P = a.P
col = 'f_primary' if a.chan == 'A' else 'f_secondary'

seq, f, g = [], [], []
for r in csv.DictReader(open(a.csv)):
    if r['signal_lost'] != 'False':
        continue
    try:
        x = float(r[col])
    except ValueError:
        continue
    if x > 0:
        seq.append(int(r['seq'])); f.append(x); g.append(float(r['gate_ns']))
seq = np.array(seq); f = np.array(f)
gate_ps = float(np.median(g)) * 1e3
fnom = 1e8 / round(1e8 / np.median(f))

def mad(v):
    return 1.4826 * np.median(np.abs(v - np.median(v)))

# vyhlazeny kmitocet na mrizce cisla okna (diry interpolovat)
n = seq[-1] - seq[0] + 1
fg = np.interp(np.arange(n), seq - seq[0], f)
w = np.ones(a.smooth) / a.smooth
fs_g = np.convolve(fg, w, mode='same')
h = a.smooth // 2
fs_g[:h] = fs_g[h]; fs_g[-h:] = fs_g[-h - 1]                 # okraje: drzet (vyrazeny nize)
rel_g = (fs_g - fnom) / fnom
sweep_g = rel_g * gate_ps                                     # posun faze za okno [ps]
phs_g = np.concatenate([[0.0], np.cumsum(sweep_g[:-1])])     # faze zacatku okna
idx = seq - seq[0]
keep = (idx >= h) & (idx < n - h)
fs = fs_g[idx][keep]; e = ((f[keep] - fs) / fs * gate_ps); sw = sweep_g[idx][keep]; ph = phs_g[idx][keep]
print('kanal %s: oken %d (po orezani okraju %d), f_nom %.0f Hz, posun f %.3e, posun faze %.1f ns/okno (%.2f periody P)' % (
    a.chan, len(seq), len(e), fnom, np.median(rel_g), np.median(sw) / 1000, np.median(sw) / P))
print('  sigma chyby okna (MAD) %.1f ps; vyhlazeni %d oken, blok %d oken, K %d, P %.0f ps' % (mad(e), a.smooth, a.block, a.K, P))

frac = (np.arange(NSEG) + 0.5) / NSEG

def design(ph, sw, K):
    phi = ph[:, None] + frac[None, :] * sw[:, None]
    cols = [np.ones(len(ph))]
    for k in range(1, K + 1):
        om = 2 * math.pi * k / P
        att = np.sinc(om * (sw / NSEG) / (2 * math.pi))      # prumer harmonicke pres segment (np.sinc = sin(pi x)/(pi x))
        cols.append((np.cos(om * phi) @ ci) * att)
        cols.append((np.sin(om * phi) @ ci) * att)
    return np.column_stack(cols)

G = 2000
grid = np.linspace(0, P, G, endpoint=False)
def shape(coef, K, skip10=False):
    return sum(coef[2*k-1] * np.cos(2*math.pi*k/P*grid) + coef[2*k] * np.sin(2*math.pi*k/P*grid)
               for k in range(1, K + 1) if not (skip10 and k % 10 == 0))

def blocks(phv):
    out = []; r2 = []; s0 = []; s1 = []
    for b0 in range(0, len(e) - a.block // 2, a.block):
        j = np.arange(b0, min(b0 + a.block, len(e)))
        X = design(phv[j], sw[j], a.K)
        tr = np.random.default_rng(b0).random(len(j)) < 0.5; te = ~tr
        coef, *_ = np.linalg.lstsq(X[tr], e[j][tr], rcond=None)
        res = e[j][te] - X[te] @ coef; base = e[j][te] - e[j][tr].mean()
        r2.append(1 - np.var(res) / np.var(base)); s0.append(mad(base)); s1.append(mad(res))
        coef_all, *_ = np.linalg.lstsq(X, e[j], rcond=None)
        out.append(coef_all)
    return np.array(out), np.median(r2), np.median(s0), np.median(s1)

def align(S):
    ref = S[0].copy()
    for _ in range(4):
        al = []; cs = []
        for D in S:
            c = np.fft.ifft(np.fft.fft(D) * np.conj(np.fft.fft(ref))).real   # kruhova korelace
            s = int(np.argmax(c)); Dr = np.roll(D, -s)
            al.append(Dr); cs.append(np.corrcoef(Dr, ref)[0, 1])
        ref = np.mean(al, axis=0)
    return ref, np.array(cs)

C, r2, s0, s1 = blocks(ph)
Cn, r2n, s0n, s1n = blocks(np.random.default_rng(3).permutation(ph))
print('\nfit po blocich (median pres bloky, test na polovine oken bloku):')
print('  skutecna faze:   R^2 %.2f, sigma %.1f -> %.1f ps' % (r2, s0, s1))
print('  zamichana faze:  R^2 %.2f, sigma %.1f -> %.1f ps   (kontrola)' % (r2n, s0n, s1n))

full = np.array([shape(c, a.K) for c in C]); full -= full.mean(axis=1, keepdims=True)
ref, cs = align(full)
print('  tvar: korelace bloku se vzorem median %.2f (kvartily %.2f / %.2f); p-p %.0f ps, rms %.1f ps' % (
    np.median(cs), *np.percentile(cs, [25, 75]), ref.max() - ref.min(), ref.std()))
amp = np.abs(np.fft.rfft(ref))[1:a.K + 1] * 2 / G
print('  amplitudy harmonickych periody %.0f ns [ps]: ' % (P / 1000) + ' '.join('%d:%.0f' % (k + 1, v) for k, v in enumerate(amp[:12])))
print('  (beh s generatorem 2026-10-30 18:03, kanal A: 1:63 2:76 3:30 4:28 ... 10:26; B: 1:62 2:73 3:27 4:22 ... 10:109)')

if a.ref:
    R = np.load(a.ref)
    # slozka 100 ns: harmonicke 1..K krome nasobku 10, z obou tvaru, na stejne mrizce
    def comp(v):
        F = np.fft.rfft(v); m = np.zeros_like(F)
        for k in range(1, a.K + 1):
            if k % 10 != 0 and k < len(F):
                m[k] = F[k]
        return np.fft.irfft(m, n=G)
    Rg = np.interp(grid, np.linspace(0, P, len(R), endpoint=False), R, period=P)
    x, y = comp(ref), comp(Rg)
    cc = [np.corrcoef(np.roll(x, s), y)[0, 1] for s in range(0, G, 5)]
    s = int(np.argmax(cc)) * 5
    xr = np.roll(x, s); k = np.dot(xr, y) / np.dot(xr, xr)
    print('\nporovnani slozky 100 ns s behem s generatorem: korelace %.2f (posun %.1f ns), pomer amplitud gen/ext %.2f, '
          'rms ext %.1f / gen %.1f ps' % (max(cc), s * P / G / 1000, k, x.std(), y.std()))
if a.out:
    np.save(a.out, ref)
