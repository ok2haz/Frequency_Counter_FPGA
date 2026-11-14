# Doplnkove modely (optimalizace 3 / kriticka validace):
#  A) N1 oprava pri preklapeni arm (maskovani CS zapina/vypina spousteni) -- zadny zastaraly vzorek
#  B) maskovani CS nevychyli sklon regrese ani pri maskovani ve STALE stejne fazi segmentu (nejhorsi pripad)
#  C) coh_mon: kratky zakmit 10 MHz (navic preklopeni) a fazovy skok 6 ns -> detekce; skok 3 ns -> mimo dosah (ocekavano)
import random, math, bisect
random.seed(11)
# ---- A ----
def run_arm(P, phase, ncyc, mask_period, mask_len):
    level = lambda k: (((k - phase) / P) % 1.0) < 0.5
    idle, fz, ldok, qd, pend = 1, 0, 1, -1, None; bad = good = 0; arm_r = 1
    for c in range(2, ncyc):
        arm_next = 0 if (c % mask_period) < mask_len else 1   # want_seg & ~cs
        ec = level(c-1) and not level(c-2)
        trig = ec and arm_r and idle
        ld = ldok and not (ec and arm_r)
        if fz == 4 and c > 20:
            if qd == pend: good += 1
            else: bad += 1
        n_qd = c if ld else qd
        if trig: n_fz, n_idle, n_pend = 1, 0, c-1
        elif not idle: (n_fz, n_idle) = (0, 1) if fz == 4 else (fz+1, idle); n_pend = pend
        else: n_fz, n_idle, n_pend = fz, idle, pend
        n_ldok = (not trig) and (idle or fz in (3, 4))
        qd, fz, idle, ldok, pend, arm_r = n_qd, n_fz, n_idle, n_ldok, n_pend, arm_next
    return good, bad
tot_g = tot_b = 0
for f in [10, 16.7, 18, 20, 22.5, 25, 33]:
    for t in range(5):
        g, b = run_arm(100/f, random.random()*10, 30000, random.choice([97, 331, 1000]), random.choice([1, 3, 7, 40]))
        tot_g += g; tot_b += b
print(f"A) N1 + preklapeni arm (maskovani): spatne {tot_b}/{tot_g+tot_b}")
# ---- B ----
f = 10e6; P = 1/f; N = 2_500_000; G = N*P; sig = 100e-12
def slope(mask_phase, mask_frac):
    # segmenty A=[G/16,3G/16), B=[13G/16,15G/16); znacka kazde hrany; maskovano okno delky mask_frac*segment v pevne fazi
    xs = {0: [], 1: []}
    for s, (a, b) in enumerate([(N//16, 3*N//16), (13*N//16, 15*N//16)]):
        L = b - a; m0 = a + int(mask_phase*L); m1 = m0 + int(mask_frac*L)
        idx = [i for i in range(a, b, 50) if not (m0 <= i < m1)]    # decimace 50 (rychlost), stejne pro obe metody
        xs[s] = idx
    def mean_xy(idx):
        n = len(idx); xm = sum(idx)/n; ym = sum(i*P + random.gauss(0, sig) for i in idx)/n; return xm, ym
    (xa, ya), (xb, yb) = mean_xy(xs[0]), mean_xy(xs[1])
    return (xb-xa)/(yb-ya)
random.seed(5)
d_ref = [slope(0.0, 0.0)/f-1 for _ in range(20)]
d_msk = [slope(0.37, 0.02)/f-1 for _ in range(20)]
mr = sum(d_ref)/len(d_ref); mm = sum(d_msk)/len(d_msk)
sr = math.sqrt(sum((d-mr)**2 for d in d_ref)/19)
print(f"B) maskovani 2 % segmentu ve stale fazi: stredni odchylka {mm:.2e} proti bez maskovani {mr:.2e} (sigma jednoho {sr:.1e}) -> vychyleni ~0")
# ---- C ----
def coh(trans_mod, ncyc=1_200_000, eps=0.03, seed=1):
    rnd = random.Random(seed); trans = [ (0.3 + 10*k) for k in range(ncyc//10) ]
    trans = trans_mod(trans)
    def lev(ts):
        i = bisect.bisect_right(trans, ts)
        for j in (i-1, i):
            if 0 <= j < len(trans) and abs(trans[j]-ts) < eps: return rnd.random() < 0.5
        return i & 1
    LP = [lev(n) for n in range(ncyc)]; LN = [lev(n-0.5) for n in range(ncyc)]
    ip = inn = 0; pp = pn = 0; dp = dn = 0; ep = 0; err = 0
    for n in range(1, ncyc):
        tp = LP[n] != LP[n-1]; tn = LN[n] != LN[n-1]
        dvp = tp and pp and ip != 10; dvn = tn and pn and inn != 10
        dpx = min(dp + dvp, 7); dnx = min(dn + dvn, 7)
        if tp: ip = 1; pp = 1
        else: ip = min(ip+1, 15)
        if tn: inn = 1; pn = 1
        else: inn = min(inn+1, 15)
        if tp:
            ep += 1
            if ep == 1024: ep = 0; err += (dpx > 0 and dnx > 0); dp = dn = 0; continue
        dp, dn = dpx, dnx
    return err
def glitch(tr):          # navic kratky impulz 2 ns uprostred periody v case ~500 us
    k = 50_000; t = tr[k] + 4.0; return sorted(tr + [t, t + 2.0])
def jump(ns):
    def f(tr): k = 60_000; return tr[:k] + [t + ns/10.0*10.0/10.0 for t in tr[k:]]   # posun vsech dalsich prechodu o ns (1 takt = 10 ns)
    return f
print(f"C) coh_mon: zakmit 10 MHz -> chybnych epoch {coh(glitch)} (ocekavano >= 1)")
print(f"   fazovy skok 6 ns -> {coh(lambda tr: tr[:60000] + [t + 0.6 for t in tr[60000:]])} (ocekavano 1)")
print(f"   fazovy skok 3 ns -> {coh(lambda tr: tr[:60000] + [t + 0.3 for t in tr[60000:]])} (pod rozlisenim: 0 je v poradku)")
print(f"   bez poruchy -> {coh(lambda tr: tr)} (ocekavano 0)")
