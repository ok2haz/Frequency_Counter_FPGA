# Audit 4: (1) coh_mon pri NEkoherentnich hodinach (externi reference: 100 MHz z externi, REF_10MHz z OCXO) -> chyby => musi se vypnout
#          (2) Grayuv citac chybnych epoch: kazdy krok meni 1 bit, prevod zpet XOR prefixem
#          (3) prirazeni bublin/CS k oknu: uzaviraci znacka (ts_valid az po uzavreni) patri do SVEHO okna
import random, bisect
def coh_errors(dfrac, ncyc=1_500_000, eps=0.03, seed=3):
    rnd = random.Random(seed); P10 = 10.0 * (1.0 + dfrac)
    trans = [0.3 + P10 * k for k in range(int(ncyc / P10) + 2)]
    def lev(ts):
        i = bisect.bisect_right(trans, ts)
        for j in (i - 1, i):
            if 0 <= j < len(trans) and abs(trans[j] - ts) < eps: return rnd.random() < 0.5
        return i & 1
    LP = [lev(n) for n in range(ncyc)]; LN = [lev(n - 0.5) for n in range(ncyc)]
    ip = inn = 0; pp = pn = 0; dp = dn = 0; ep = 0; err = 0
    for n in range(1, ncyc):
        tp = LP[n] != LP[n-1]; tn = LN[n] != LN[n-1]
        dpx = min(dp + (tp and pp and ip != 10), 7); dnx = min(dn + (tn and pn and inn != 10), 7)
        if tp: ip = 1; pp = 1
        else: ip = min(ip + 1, 15)
        if tn: inn = 1; pn = 1
        else: inn = min(inn + 1, 15)
        if tp:
            ep += 1
            if ep == 1024: ep = 0; err += (dpx > 0 and dnx > 0); dp = dn = 0; continue
        dp, dn = dpx, dnx
    return err
e = coh_errors(1e-5)
print(f"(1) nekoherentni hodiny (rozdil 1e-5, 15 ms): chybnych epoch {e} -> pomaly rozdil kmitoctu poplach NEdava (skluzy vidi kazda cesta jindy); vypnuti v ext. rezimu je semanticke + prechod Si5356")
print(f"    koherentni: {coh_errors(0.0)}")
# (2)
ok = True; prev = 0
for b in range(1, 256):
    g = b ^ (b >> 1); gp = (b - 1) ^ ((b - 1) >> 1)
    if bin(g ^ gp).count('1') != 1: ok = False
    back = 0
    for i in range(8): back |= (bin(g >> i).count('1') & 1) << i
    if back != b: ok = False
print(f"(2) Gray: 1 bit na krok a zpetny prevod 0..255: {'OK' if ok else 'CHYBA'}")
# (3) udalostni model win_recip: segmentove znacky (ts_valid) + uzaviraci (ts_valid ~9 taktu po close, tsc_q o takt pozdeji)
random.seed(9); ok = True
for trial in range(2000):
    events = []                      # (cyklus, typ, bub, cs)
    t = 0; windows = []
    for w in range(5):
        segs = [(t + random.randint(100, 900), random.random() < 0.1, random.random() < 0.05) for _ in range(random.randint(0, 6))]
        segs.sort()
        close = t + 1000
        cl = (close + 9, random.random() < 0.1, random.random() < 0.05)
        events += [(c, 'seg', b, s) for c, b, s in segs] + [(cl[0], 'cls', cl[1], cl[2])]
        windows.append((sum(b for _, b, _ in segs) + cl[1], sum(s for _, _, s in segs) + cl[2]))
        t = close
    events.sort()
    bubc = ovlc = 0; got = []
    for c, typ, b, s in events:          # ts_valid: pricist; uzaviraci -> tsc_q o takt pozdeji: zachytit a vynulovat
        bubc += b; ovlc += s
        if typ == 'cls': got.append((bubc, ovlc)); bubc = ovlc = 0
    if got != windows: ok = False; break
print(f"(3) prirazeni bublin a znacek pri CS k oknu (2000 nahodnych behu): {'OK' if ok else 'CHYBA'}")
