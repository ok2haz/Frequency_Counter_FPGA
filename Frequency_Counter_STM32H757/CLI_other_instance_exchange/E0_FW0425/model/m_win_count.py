# Cyklovy model pocitani hran ve win_recip (count_lo/count_hi s registrovanym prenosem, close -> cl_q -> cl_q2).
# Overeni opravy close+2 (FW 0x0425). inc = rise_q (1 takt na hranu); close = takt inc uzaviraci hrany.
# Spravne: snap okna = pocet hran od predchozi uzaviraci (vyhradne) po tuto (vcetne).
import random

def run(P, phase, ncyc, gate_every, fixed):
    edge_cyc = set()
    k = 0
    while True:
        t = phase + k * P
        if t >= ncyc: break
        edge_cyc.add(int(t) + 1)          # hrana mezi vzorky -> inc v nasledujicim taktu (pevny posun, na nem nezalezi)
        k += 1
    cnt_lo, cnt_hi, cy_q = 0, 0, 0
    cl_q = cl_q2 = 0
    armed = 0
    true_cnt = 0          # skutecny pocet hran od posledniho uzavreni (vyhradne) -> referencni model
    snaps = []; refs = []
    for c in range(ncyc):
        inc = 1 if c in edge_cyc else 0
        if c % gate_every == 0: armed = 1
        close = inc and armed
        # referencni pocitani
        if inc: true_cnt += 1
        if close:
            refs.append(true_cnt); true_cnt = 0
        # RTL (neblokujici)
        count = (cnt_hi << 8) | cnt_lo
        if fixed: n_cy = 1 if (inc and cnt_lo == 0xFF and not cl_q2) else 0
        else:     n_cy = 1 if (inc and cnt_lo == 0xFF) else 0
        n_hi = cnt_hi + 1 if cy_q else cnt_hi
        n_lo = (cnt_lo + 1) & 0xFF if inc else cnt_lo
        if cl_q2:
            snaps.append(count)
            n_lo = (1 if inc else 0) if fixed else 0
            n_hi = 0
        n_clq = 1 if close else 0
        if close: armed = 0
        cnt_lo, cnt_hi, cy_q, cl_q2, cl_q = n_lo, n_hi, n_cy, cl_q, n_clq
    # prvni okno je neuplne (start) -> porovnat od druheho
    m = min(len(snaps), len(refs))
    bad = sum(1 for i in range(1, m) if snaps[i] != refs[i])
    return bad, m - 1

random.seed(2)
print("f_in [MHz]  odstup [takty]   stary RTL: spatna okna   oprava")
for f in [10, 25, 30, 33.3, 34, 36, 40, 45, 49]:
    P = 100.0 / f
    ob = of = tot = 0
    for t in range(10):
        ph = random.random() * P
        g_every = random.choice([997, 1013, 2503])
        b, n = run(P, ph, 300000, g_every, False); ob += b; tot += n
        b, n = run(P, ph, 300000, g_every, True);  of += b
    print(f"{f:8.1f}     {P:6.2f}            {ob:5d}/{tot:<6d}           {of}/{tot}")
