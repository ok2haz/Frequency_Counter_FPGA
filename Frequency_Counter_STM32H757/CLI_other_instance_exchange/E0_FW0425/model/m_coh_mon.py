# Model hlidace koherence coh_mon (FW 0x0425). Takt 100 MHz = 1 jednotka casu. tgl10 se preklapi kazdych 10 taktu v case
# phi + 10k (+ jitter). Cesta P vzorkuje v celych casech n, cesta N v n - 0.5 (sestupna hrana), metastabilita: |t_prechod - t_vzorek|
# < eps -> nahodny vysledek. Vadne pulzy: chybejici pulz 100 MHz = od daneho okamziku jsou vsechny dalsi prechody o 1 takt "drive"
# v citani taktu (posun -1), navic pulz = +1.
import random

def simulate(phi, eps, jit, glitches, ncyc=2_000_000, seed=0):
    rnd = random.Random(seed)
    # casy prechodu v jednotkach taktu, s posunem od vadnych pulzu
    trans = []
    shift = 0.0; gi = 0; gl = sorted(glitches)
    t = phi
    while t < ncyc:
        while gi < len(gl) and gl[gi][0] <= t:
            shift += gl[gi][1]; gi += 1
        trans.append(t + shift + rnd.gauss(0, jit))
        t += 10.0
    def level_at(ts, tt):        # uroven tgl10 v case ts podle prechodu (s metastabilitou)
        # najit pocet prechodu <= ts
        import bisect
        i = bisect.bisect_right(tt, ts)
        lv = i & 1
        # blizky prechod -> nahodny
        for j in (i - 1, i):
            if 0 <= j < len(tt) and abs(tt[j] - ts) < eps:
                return rnd.random() < 0.5
        return lv
    # synchronizovane urovne cest P a N v kazdem taktu
    def path(off):
        return [level_at(n - off, trans) for n in range(ncyc)]
    LP = path(0.0); LN = path(0.5)
    # coh_mon logika (zjednodusene: p2/p3 a n2/n3 jen zpozdeni o konstantu -> vynechano)
    ip = inn = 0; pp = pn = 0; dp = dn = 0; ep = 0; err = 0; epochs = 0
    for n in range(1, ncyc):
        tr_p = LP[n] != LP[n-1]; tr_n = LN[n] != LN[n-1]
        dev_p = tr_p and pp and ip != 10
        dev_n = tr_n and pn and inn != 10
        dpx = dp + (1 if dev_p else 0); dnx = dn + (1 if dev_n else 0)
        if tr_p: ip = 1; pp = 1
        else: ip = min(ip + 1, 15)
        if tr_n: inn = 1; pn = 1
        else: inn = min(inn + 1, 15)
        if tr_p:
            ep += 1
            if ep == 1024:
                ep = 0; epochs += 1
                if dpx and dnx: err += 1
                dp = dn = 0; continue
        dp, dn = dpx, dnx
    return err, epochs

print("faze prechodu   eps[takt] jitter  vadne pulzy  -> chybnych epoch / epoch")
for phi, eps, jit in [(0.0, 0.03, 0.002), (0.5, 0.03, 0.002), (0.25, 0.03, 0.002), (0.02, 0.05, 0.01)]:
    e, n = simulate(phi, eps, jit, [], seed=1)
    print(f"  {phi:5.2f}         {eps:5.2f}   {jit:5.3f}      0          {e}/{n}   (falesne poplachy)")
gl = [(300_000, -1), (900_000, +1), (1_500_000, -1)]
for phi, eps, jit in [(0.0, 0.03, 0.002), (0.5, 0.03, 0.002), (0.25, 0.03, 0.002)]:
    e, n = simulate(phi, eps, jit, gl, seed=2)
    print(f"  {phi:5.2f}         {eps:5.2f}   {jit:5.3f}      3          {e}/{n}   (ocekavano 3)")
