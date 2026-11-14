# Cyklovy model rizeni tdc_chan (spousteni, zmrazeni qd, fz, ld) -- overeni opravy N1 (FW 0x0425).
# Semantika: hodnoty registru "v taktu c" jsou ty po hrane c; neblokujici prirazeni.
# level(k) = uroven signalu v okamziku vzorku k (tap KD). ec v taktu c = level(c-1) & ~level(c-2) (dR = q_r[KD] o takt zpozdeny).
# Spoustejici vzorek hrany, ktera da ec v taktu c, je vzorek c-1; spravne qd pro tuto hranu = vzorek c-1.
import random, math

def run(P, phase, ncyc, fixed, arm_all=True):
    level = lambda k: (((k - phase) / P) % 1.0) < 0.5
    idle, fz, ldok = 1, 0, 1
    qd = -1                     # index vzorku v qd
    code_r = None
    pend = None                 # spoustejici vzorek prave zpracovavane udalosti
    bad = good = 0
    for c in range(2, ncyc):
        ec = level(c-1) and not level(c-2)
        arm = 1 if arm_all else 0
        trig_ok = ec and arm and idle
        if fixed:
            ld = ldok and not (ec and arm)
        else:
            ld = idle and not (ec and arm)
        fz4 = (fz == 4)
        # --- vypocet novych hodnot (hrana c+1) ---
        n_qd = (c) if ld else qd          # q_r v taktu c = vzorek c
        if fz4 and c > 20:      # prvnich ~20 taktu = nabeh modelu (qd jeste nic nenacetlo)
            # code_r <= dekodovane qd (aktualni obsah)
            if qd == pend: good += 1
            else: bad += 1
        if trig_ok:
            n_fz, n_idle = 1, 0
            n_pend = c - 1
        elif not idle:
            if fz == 4: n_fz, n_idle = 0, 1
            else: n_fz, n_idle = fz + 1, idle
            n_pend = pend
        else:
            n_fz, n_idle, n_pend = fz, idle, pend
        n_ldok = (not trig_ok) and (idle or fz == 3 or fz == 4)
        qd, fz, idle, ldok, pend = n_qd, n_fz, n_idle, n_ldok, n_pend
    return good, bad

random.seed(1)
print("f_in [MHz]   stary RTL: spatne/vsech    oprava: spatne/vsech")
for f in [10.0, 15.0, 16.7, 18.0, 19.0, 20.0, 21.0, 22.5, 24.0, 25.0, 28.0, 33.0]:
    P = 100.0 / f
    gb = [0, 0]; gf = [0, 0]
    for t in range(20):
        ph = random.random() * P
        g, b = run(P, ph, 20000, fixed=False); gb[0] += g; gb[1] += b
        g, b = run(P, ph, 20000, fixed=True);  gf[0] += g; gf[1] += b
    print(f"{f:8.1f}      {gb[1]:7d}/{gb[0]+gb[1]:<8d}           {gf[1]:5d}/{gf[0]+gf[1]}")
