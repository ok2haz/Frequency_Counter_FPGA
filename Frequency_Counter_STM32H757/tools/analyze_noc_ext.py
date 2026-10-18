# -*- coding: utf-8 -*-
"""Rozsirena analyza dlouheho behu loggeru (gpsdo_log.ps1) - 10 h, smycka GPSDO v LOCK.

Pouziti: python tools/analyze_noc_ext.py <csv> [orez_s] [porovnani.csv]

Nadstavba nad analyze_gpsdo_log.py a cov312.py (ty zustavaji). Pridava:
  1. integrita dat (diry v seq, host/zarizeni), pocty odlehlych vzorku citace,
  2. stabilita FAZE OCXO-GPS s OPRAVOU NA KVANTIZACI 10 ns (odhad sigma_x z kratkych tau, MDEV, prebytek),
  3. spektralni analyza citace (Welch PSD y, hledani tonu),
  4. KOHERENCE a PRUMER krizoveho spektra y_citac x y_faze (znamenko cov podle frekvence) - k #312,
  5. DAC kod: sklon, teplotni koeficient (castecna regrese), cas vs teplota,
  6. TIE/MTIE faze proti GPS 1PPS.
Vse je odhad z jednoho behu; u kazde veliciny je napsano, co NEdokazuje.
"""
import csv
import gzip
import json
import math
import sys
import numpy as np

P = sys.argv[1]
T0 = float(sys.argv[2]) if len(sys.argv) > 2 else 3600.0
PCMP = sys.argv[3] if len(sys.argv) > 3 else None
PB = P[:-3] if P.endswith(".gz") else P            # pridruzene soubory (.stab.jsonl) lezi vedle nekomprimovaneho nazvu
rng = np.random.default_rng(7)


def load(path):
    op = gzip.open if path.endswith('.gz') else open
    rows = list(csv.DictReader(op(path, 'rt', encoding='utf-8')))

    def col(n):
        out = []
        for r in rows:
            try:
                out.append(float(r[n]))
            except Exception:
                out.append(np.nan)
        return np.array(out)

    d = {k: col(k) for k in ("uptime_s", "seq_meas", "freq_hz", "gp_y_ppt", "gp_xr_ns", "gp_code", "gp_t_s",
                             "t_ocxo", "t_board", "t_fpga", "gps_sats", "gps_hdop", "gate_s")}
    d["state"] = np.array([r["gp_state"] for r in rows])
    wall = np.array([np.datetime64(r["wall"]) for r in rows])
    d["t"] = (wall - wall[0]).astype("timedelta64[ms]").astype(float) / 1000.0
    d["wall0"] = str(wall[0])
    d["n"] = len(rows)
    return d


def grid_y(d):
    """citac na pevne mrizce 'cislo mereni' (FPGA meri nepretrzite, F-0193); chybejici = interpolace"""
    seq = d["seq_meas"]
    ok = np.isfinite(seq) & np.isfinite(d["freq_hz"])
    s = seq[ok].astype(np.int64)
    s, iu = np.unique(s, return_index=True)
    f = d["freq_hz"][ok][iu]
    n = s[-1] - s[0] + 1
    g = np.arange(s[0], s[-1] + 1)
    gate = float(np.nanmedian(d["gate_s"]))
    y = np.interp(g, s, (f - 1e7) / 1e7)
    miss = np.ones(n, bool)
    miss[s - s[0]] = False
    # cas mereni: stred okna; zacatek mrizky navazat na wall cas prvniho radku
    t0 = d["t"][ok][iu][0]
    tg = t0 + (g - g[0]) * gate
    return tg, y, miss, gate


def phase_1hz(d):
    """fazovy zaznam xr na MRIZCE 1 s podle gp_t_s (cas smycky, +1 za 1PPS)"""
    ok = np.isfinite(d["gp_t_s"]) & np.isfinite(d["gp_xr_ns"]) & (d["state"] == "LOCK")
    ts = d["gp_t_s"][ok].astype(np.int64)
    xr = d["gp_xr_ns"][ok]
    tt = d["t"][ok]
    last = {}
    for a, b, c in zip(ts, xr, tt):
        last[a] = (b, c)
    k = np.array(sorted(last))
    x = np.array([last[i][0] for i in k])
    t = np.array([last[i][1] for i in k])
    full = np.arange(k[0], k[-1] + 1)
    miss = ~np.isin(full, k)
    xg = np.interp(full, k, x)
    tg = np.interp(full, k, t)
    return tg, xg, miss


def phase_dev(x_ns, tau0, ms):
    """overlapping ADEV, MDEV, TDEV, HDEV z fazoveho zaznamu [ns]"""
    xs = np.asarray(x_ns) * 1e-9
    N = len(xs)
    out = []
    for m in ms:
        if N - 3 * m < 8:
            continue
        d2 = xs[2 * m:] - 2 * xs[m:N - m] + xs[:N - 2 * m]
        adev = np.sqrt(0.5 * np.mean(d2 ** 2)) / (m * tau0)
        c = np.cumsum(np.concatenate(([0.0], d2)))
        sj = c[m:] - c[:-m]
        mdev = np.sqrt(0.5 * np.mean(sj ** 2)) / (m * m * tau0)
        d3 = xs[3 * m:] - 3 * xs[2 * m:N - m] + 3 * xs[m:N - 2 * m] - xs[:N - 3 * m]
        hdev = np.sqrt(np.mean(d3 ** 2) / 6.0) / (m * tau0)
        tdev = m * tau0 * mdev / np.sqrt(3.0) * 1e9      # [ns]
        out.append((m * tau0, adev, mdev, hdev, tdev, len(d2)))
    return out


def welch(x, fs, nseg, win_hann=True, overlap=0.5):
    """jednostranne PSD [jednotka^2/Hz]; vraci f, S"""
    step = int(nseg * (1 - overlap))
    w = np.hanning(nseg) if win_hann else np.ones(nseg)
    U = (w ** 2).sum()
    acc = None
    cnt = 0
    for s in range(0, len(x) - nseg + 1, step):
        seg = x[s:s + nseg]
        seg = seg - seg.mean()
        F = np.fft.rfft(seg * w)
        p = (np.abs(F) ** 2) * 2.0 / (fs * U)
        acc = p if acc is None else acc + p
        cnt += 1
    return np.fft.rfftfreq(nseg, 1.0 / fs), acc / cnt, cnt


def csd(x, y, fs, nseg, overlap=0.5):
    step = int(nseg * (1 - overlap))
    w = np.hanning(nseg)
    U = (w ** 2).sum()
    sxx = syy = sxy = 0
    cnt = 0
    for s in range(0, len(x) - nseg + 1, step):
        a = x[s:s + nseg] - x[s:s + nseg].mean()
        b = y[s:s + nseg] - y[s:s + nseg].mean()
        A = np.fft.rfft(a * w)
        B = np.fft.rfft(b * w)
        sxx = sxx + np.abs(A) ** 2
        syy = syy + np.abs(B) ** 2
        sxy = sxy + A * np.conj(B)
        cnt += 1
    k = 2.0 / (fs * U)
    return np.fft.rfftfreq(nseg, 1.0 / fs), sxx * k / cnt, syy * k / cnt, sxy * k / cnt, cnt


def hr(s):
    print("\n" + "=" * 78 + "\n" + s + "\n" + "=" * 78)


d = load(P)
print("soubor:", P, " radku", d["n"], " start", d["wall0"], " orez %.0f s" % T0)

# ------------------------------------------------------------------ 1. integrita
hr("1. INTEGRITA DAT A ODLEHLE VZORKY CITACE")
tg, y, miss, gate = grid_y(d)
print("mrizka mereni: %d (hradlo %.3f s, %.2f h); chybi %d (%.2f %%)" % (len(y), gate, len(y) * gate / 3600, miss.sum(), 100 * miss.mean()))
dseq = np.diff(d["seq_meas"])
ev = np.where(dseq >= 2)[0]
print("udalosti vypadku zapisu: %d, nejdelsi vypadek %d mereni (%.1f s); 2-3 s vypadky = zastaveni cteni logem, ne mereni FPGA" % (
    len(ev), int(dseq[ev].max()) - 1, (dseq[ev].max() - 1) * gate))
yc = y[~miss] * 1e9
print("citac 0,25 s: median %.3f ppb, sd %.3f ppb (siroke chvosty = pomala slozka externiho GPSDO, ne vypadky)" % (np.median(yc), yc.std()))
print("  min %.2f max %.2f ppb" % (yc.min(), yc.max()))
# izolovane odlehle vzorky PODLE FIRMWARE (v0.27.0): vzorky 1 s (prumer 4 mereni), okno 31, |x-med| > 7 MAD, soused v norme (< 2,7 MAD)
from numpy.lib.stride_tricks import sliding_window_view
n4 = (len(y) // 4) * 4
y1 = y[:n4].reshape(-1, 4).mean(axis=1) * 1e9
W = 31
sw = sliding_window_view(y1, W)
med_w = np.median(sw, axis=1)
mad_w = 1.4826 * np.median(np.abs(sw - med_w[:, None]), axis=1)
c = y1[W // 2: W // 2 + len(med_w)]
dev = np.abs(c - med_w) / np.maximum(mad_w, 1e-12)
cand = np.where(dev > 7)[0]
iso = [i for i in cand if 0 < i < len(dev) - 1 and dev[i - 1] < 2.7 and dev[i + 1] < 2.7]
print("izolovane odlehle vzorky 1 s (pravidlo firmwaru): %d z %d (%.3f %%), kandidatu >7 MAD celkem %d (z toho shluky %d)" % (
    len(iso), len(dev), 100 * len(iso) / len(dev), len(cand), len(cand) - len(iso)))
try:
    L = [json.loads(l.split(" ", 1)[1]) for l in open(PB + ".stab.jsonl").read().strip().splitlines()]
    print("firmware (posledni zaznam stab.jsonl): vyrazeno ro=%s z posouzenych rn=%s, shluky/skoky rm=%s" % (L[-1].get("ro"), L[-1].get("rn"), L[-1].get("rm")))
except Exception as e:
    print("stab.jsonl nelze cist:", e)
if iso:
    amp = np.abs(c[iso] - med_w[iso])
    print("  velikost izolovanych odlehlych: median %.2f ppb, max %.2f ppb" % (np.median(amp), amp.max()))

# ------------------------------------------------------------------ 2. faze
hr("2. FAZE OCXO-GPS (xr) NA MRIZCE 1 s: KVANTIZACE vs PREBYTEK")
tp, xp, mp = phase_1hz(d)
cut = tp >= T0
print("vzorku 1 Hz: %d, doplneno interpolaci %d (%.2f %%); po orezu %d" % (len(xp), mp.sum(), 100 * mp.mean(), cut.sum()))
xcut = xp[cut]
ms = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]
R = phase_dev(xcut, 1.0, ms)
# sigma_x kvantizace: z ADEV na 2-8 s (bily PM): ADEV = sqrt(3)*sx/tau  (pri velkem fm)
sq = []
for tau, a, md, hd, td, n in R:
    if tau in (2.0, 4.0, 8.0):
        sq.append(a * tau / np.sqrt(3.0) * 1e9)
sx = float(np.mean(sq))
print("odhad sigma_x bileho PM (kvantizace 10 ns + zbytek sawtooth) z tau 2,4,8 s: %.2f ns (teorie 10/sqrt12 = 2,89 ns)" % sx)
print()
print("   tau[s]   ADEV      MDEV      HDEV      TDEV[ns]  ADEV_kvant  PREBYTEK(ADEV)  PREBYTEK(MDEV)  n")
for tau, a, md, hd, td, n in R:
    aq = np.sqrt(3.0) * sx * 1e-9 / tau
    mq = np.sqrt(3.0) * sx * 1e-9 / tau ** 1.5          # MDEV bileho PM s tau0 = 1 s
    ex_a = np.sqrt(max(a * a - aq * aq, 0.0))
    ex_m = np.sqrt(max(md * md - mq * mq, 0.0))
    print("  %7.0f  %.2e  %.2e  %.2e  %7.3f   %.2e    %.2e       %.2e    %d" % (tau, a, md, hd, td, aq, ex_a, ex_m, n))
print("(prebytek = sqrt(max(sigma^2 - sigma_kvantizace^2, 0)); u MDEV je kvantizace potlacena silneji, takze je prebytek presnejsi)")

# hodinove okno: ADEV fáze na 100 a 500 s po hodinach (konzistence)
print()
print("hodinova okna: ADEV a MDEV faze na 100 s a 500 s (jednotky 1e-11), prebytek podle MDEV")
print(" hod   ADEV100  MDEV100  pres100   ADEV500  MDEV500  pres500   sats  HDOP  xr_sd[ns]")
for h in range(1, 10):
    m = (tp >= h * 3600) & (tp < (h + 1) * 3600)
    if m.sum() < 3000:
        continue
    Rh = {round(r[0]): r for r in phase_dev(xp[m], 1.0, [100, 500])}
    s_h = d["gps_sats"][(d["t"] >= h * 3600) & (d["t"] < (h + 1) * 3600)]
    hd_ = d["gps_hdop"][(d["t"] >= h * 3600) & (d["t"] < (h + 1) * 3600)]
    out = []
    for tau in (100, 500):
        r = Rh.get(tau)
        if r is None:
            out += [np.nan] * 3
            continue
        mq = np.sqrt(3.0) * sx * 1e-9 / tau ** 1.5
        out += [r[1] * 1e11, r[2] * 1e11, np.sqrt(max(r[2] ** 2 - mq ** 2, 0)) * 1e11]
    print("%4d  %7.1f  %7.1f  %7.1f   %7.1f  %7.1f  %7.1f   %4.1f  %4.2f  %6.2f" % (h, *out, np.nanmean(s_h), np.nanmean(hd_), xp[m].std()))

if PCMP:
    d2 = load(PCMP)
    tp2, xp2, mp2 = phase_1hz(d2)
    R2 = phase_dev(xp2[tp2 >= T0], 1.0, ms)
    print()
    print("srovnani s %s:" % PCMP.replace("\\", "/").split("/")[-1])
    print("   tau[s]  MDEV nyni  MDEV drive   ADEV nyni  ADEV drive")
    for a, b in zip(R, R2):
        if abs(a[0] - b[0]) < 1e-9:
            print("  %7.0f   %.2e   %.2e    %.2e   %.2e" % (a[0], a[2], b[2], a[1], b[1]))

# ------------------------------------------------------------------ 3. TIE / MTIE
hr("3. CHYBA CASU (TIE) A MTIE FAZE PROTI GPS 1PPS (po orezu, smycka LOCK)")
xr = xcut - np.median(xcut)
print("TIE: median posun %.1f ns, sd %.2f ns, min %.0f, max %.0f (pp %.0f ns)" % (np.median(xcut), xr.std(), xcut.min(), xcut.max(), xcut.max() - xcut.min()))
for lim in (10, 20, 30, 50):
    print("  |TIE - median| < %2d ns: %.2f %% casu" % (lim, 100 * (np.abs(xr) < lim).mean()))


def mtie(x, m):
    # okno delky m+1 vzorku: max-min; rychle pres posuvne max/min (blokova metoda)
    n = len(x) - m
    if n < 1:
        return np.nan
    from numpy.lib.stride_tricks import sliding_window_view
    # pro velke m pouzij krok kvuli pameti
    step = max(1, m // 16)
    sw = sliding_window_view(x, m + 1)[::step]
    return float((sw.max(axis=1) - sw.min(axis=1)).max())


print("MTIE [ns]: ", end="")
for m in (1, 10, 30, 100, 300, 1000, 3000, 10000, 30000):
    if m < len(xcut) // 3:
        print("%ds=%.0f  " % (m, mtie(xcut, m)), end="")
print()

# ------------------------------------------------------------------ 4. PSD citace
hr("4. SPEKTRUM CITACE (PSD y, 0,25 s mrizka): HRBOL A TONY")
fs = 1.0 / gate
yy = y.copy()
cutg = tg >= T0
f, S, nseg = welch(yy[cutg], fs, 8192)
print("Welch: segment 8192 vzorku (%.0f s), %d segmentu, rozliseni %.2f mHz" % (8192 * gate, nseg, 1e3 * f[1]))
edges = np.array([0.001, 0.002, 0.004, 0.008, 0.016, 0.03, 0.06, 0.12, 0.25, 0.5, 1.0, 2.0])
print("  pasmo [Hz]        S_y [1e-20/Hz]   (bily FM = vodorovne; kvantizace = f^2)")
for a, b in zip(edges[:-1], edges[1:]):
    m = (f >= a) & (f < b)
    print("  %.3f - %.3f   %9.1f       sigma_pasma %.2e" % (a, b, np.mean(S[m]) * 1e20, np.sqrt(np.sum(S[m]) * (f[1] - f[0]))))
# tony: pomer k lokalnimu medianu
Sm = S.copy()
pk = []
w = 25
for i in range(w, len(S) - w):
    loc = np.median(S[i - w:i + w + 1])
    if S[i] > 12 * loc and S[i] == S[i - 2:i + 3].max():
        pk.append((f[i], S[i] / loc))
print("  tony (S > 12x lokalni median, jen max): ", ", ".join("%.4f Hz (T=%.1f s, %.0fx)" % (a, 1 / a, b) for a, b in pk[:12]) if pk else "zadne")

# ------------------------------------------------------------------ 5. koherence k #312
hr("5. KRIZOVE SPEKTRUM y_citac x y_faze (k #312): ZNAMENKO A KOHERENCE")
# y_faze = derivace xr [ns/s = ppb]; y_citac = ppb; spolecna mrizka 1 s -> citac zprumerovat do 1 s
n1 = len(xp)
ypha = np.diff(xp)                       # ppb; interval (t, t+1)
tt1 = tp[:-1]
# citac -> prumer po okne 1 s (4 vzorky)
idxg = np.floor((tg - tg[0]) / 1.0).astype(int)
nb = idxg.max() + 1
yc1 = np.bincount(idxg, weights=y * 1e9, minlength=nb) / np.maximum(np.bincount(idxg, minlength=nb), 1)
t1c = tg[0] + np.arange(nb) + 0.5
# sjednotit na casovou osu fazi
yc_on = np.interp(tt1 + 0.5, t1c, yc1)
sel = tt1 >= T0
a_ = yc_on[sel]
b_ = ypha[sel]
for nseg_s, lab in ((4096, "segment 4096 s"), (1024, "segment 1024 s")):
    f2, sxx, syy, sxy, c2 = csd(a_, b_, 1.0, nseg_s)
    coh = np.abs(sxy) ** 2 / (sxx * syy)
    print("\n%s (%d segmentu)  koherence^2 a Re(Sxy)/sqrt(Sxx*Syy) po pasmech" % (lab, c2))
    print("  pasmo [mHz]       coh^2   Re/sqrt   prahy 95 %% coh^2 ~ %.2f" % (1 - 0.05 ** (1.0 / (c2 - 1))))
    bands = [(0.2, 0.5), (0.5, 1), (1, 2), (2, 4), (4, 8), (8, 16), (16, 32), (32, 64), (64, 128), (128, 250)]
    for lo, hi in bands:
        m = (f2 * 1e3 >= lo) & (f2 * 1e3 < hi)
        if m.sum() == 0:
            continue
        Sxy = sxy[m].sum()
        cc = np.abs(Sxy) ** 2 / (sxx[m].sum() * syy[m].sum())
        rr = Sxy.real / np.sqrt(sxx[m].sum() * syy[m].sum())
        print("  %5.1f - %5.1f   %6.3f  %+7.3f   (bin %d)" % (lo, hi, cc, rr, m.sum()))
print()
print("Cteni: Re(Sxy) < 0 = spolecny OCXO sum (y_c = E-O, y_x = O-G -> cov = -var(O)); Re > 0 = spolecna slozka O a G (uzavrena smycka)")
print("       nebo spolecny sum GPS v E a O. Znamenko kladne v pasmu = predpoklad 'O nezavisle na G' neplati a 'OCXO <= -cov' nema smysl.")

# ------------------------------------------------------------------ 6. DAC / teplota
hr("6. DAC KOD: DRIFT, TEPLOTA, STARNUTI")
cod = d["gp_code"]
mk = (d["t"] >= T0) & np.isfinite(cod) & np.isfinite(d["t_ocxo"])
tt = d["t"][mk] / 3600.0
c_ = cod[mk]
to_ = d["t_ocxo"][mk]
tb_ = d["t_board"][mk]
# po 30 min blocich
B = 1800.0
kb = np.floor((d["t"][mk] - d["t"][mk][0]) / B).astype(int)
nbk = kb.max() + 1
cm = np.array([c_[kb == i].mean() for i in range(nbk)])
tom = np.array([to_[kb == i].mean() for i in range(nbk)])
tbm = np.array([tb_[kb == i].mean() for i in range(nbk)])
th = (np.arange(nbk) + 0.5) * B / 3600.0
print("30min bloky: kod %.2f -> %.2f LSB; t_ocxo %.3f -> %.3f C; t_board %.2f -> %.2f C" % (cm[0], cm[-1], tom[0], tom[-1], tbm[0], tbm[-1]))
K = 3.4e-11


def fit(cols, names):
    A = np.column_stack(cols + [np.ones(len(cm))])
    sol, res, rk, sv = np.linalg.lstsq(A, cm, rcond=None)
    pred = A @ sol
    sd = (cm - pred).std(ddof=A.shape[1])
    cov = np.linalg.inv(A.T @ A) * sd ** 2
    out = []
    for i, nm in enumerate(names):
        out.append("%s = %+.3f +- %.3f" % (nm, sol[i], np.sqrt(cov[i, i])))
    return sol, sd, out, np.linalg.cond(A)


for names, cols in ((["cas[LSB/h]"], [th]), (["T_ocxo[LSB/C]"], [tom]), (["T_ocxo[LSB/C]", "cas[LSB/h]"], [tom, th]),
                    (["T_board[LSB/C]", "cas[LSB/h]"], [tbm, th]), (["T_ocxo[LSB/C]", "T_board[LSB/C]", "cas[LSB/h]"], [tom, tbm, th])):
    sol, sd, out, cn = fit(list(cols), names)
    print("  model kod ~ %-42s rezidua sd %.2f LSB, cond %.0f: %s" % (" + ".join(names), sd, cn, "; ".join(out)))
sol, sd, out, cn = fit([th], ["cas"])
print("  => samotny trend v case: %.2f LSB/h = %.1f ppt/h = %.2f ppb/den (VSE vc. teploty; hornimez starnuti)" % (sol[0], sol[0] * K * 1e12, sol[0] * 24 * K * 1e9))
vif = 1.0 / (1 - np.corrcoef(tom, th)[0, 1] ** 2)
print("  korelace T_ocxo~cas r=%.3f (VIF %.1f), T_board~cas r=%.3f -> teplotu a starnuti z JEDNOHO behu nejde spolehlive oddelit" % (
    np.corrcoef(tom, th)[0, 1], vif, np.corrcoef(tbm, th)[0, 1]))
# rychle sklony (10 min diff) pro teplotu
dc_ = np.diff(cm)
dto = np.diff(tom)
dtb = np.diff(tbm)
print("  korelace 30min prirustku: dkod~dT_ocxo %.2f, dkod~dT_board %.2f (n=%d)" % (np.corrcoef(dc_, dto)[0, 1], np.corrcoef(dc_, dtb)[0, 1], len(dc_)))
print("  dopad na holdover (jen starnuti, bez teploty): chyba kmitoctu po 1 h bez vedeni = %.1f ppt/h * 1 h; casova chyba 1 h = %.1f ns, 6 h = %.0f ns (0,5*a*t^2, a = %.2e /s)" % (
    sol[0] * K * 1e12, 0.5 * (sol[0] * K / 3600) * 3600 ** 2 * 1e9, 0.5 * (sol[0] * K / 3600) * (6 * 3600) ** 2 * 1e9, sol[0] * K / 3600))

# vlastnosti smycky: sd y smycky a kod
print()
yl = d["gp_y_ppt"][d["t"] >= T0]
print("odhad y smycky: sd %.1f ppt (jde o 1PPS-kvantizaci na 1 s, ne o stabilitu OCXO); kod DAC sd (po odecteni trendu) %.2f LSB" % (
    np.nanstd(yl), np.std(c_ - np.polyval(np.polyfit(tt, c_, 1), tt))))

# ------------------------------------------------------------------ 7. offset vs externi GPSDO
hr("7. OFFSET PROTI EXTERNIMU GPSDO PO HODINACH")
print(" hod   mean_ppb  sd(prumeru 1 min)")
for h in range(0, 10):
    m = (tg >= h * 3600) & (tg < (h + 1) * 3600)
    if m.sum() < 100:
        continue
    yy_ = y[m] * 1e9
    k = np.floor((tg[m] - tg[m][0]) / 60).astype(int)
    pm = np.array([yy_[k == i].mean() for i in range(k.max() + 1)])
    print("%3d  %8.4f   %6.3f" % (h, yy_.mean(), pm.std()))
hm = []
for h in range(1, 10):
    m = (tg >= h * 3600) & (tg < (h + 1) * 3600)
    if m.sum() > 100:
        hm.append((y[m] * 1e9).mean())
hm = np.array(hm)
print("po orezu: prumer hodinovych prumeru %.4f ppb (%.1e), sd hodinovych prumeru %.4f ppb -> SE %.4f ppb (%.1e)" % (
    hm.mean(), hm.mean() * 1e-9, hm.std(ddof=1), hm.std(ddof=1) / np.sqrt(len(hm)), hm.std(ddof=1) / np.sqrt(len(hm)) * 1e-9))


# ------------------------------------------------------------------ 8. ADEV citace offline vs firmware, citlivost #312
hr("8. ADEV CITACE (1 s prumery, overlapping) vs FIRMWARE; CITLIVOST METODY #312")
yb = y[:n4].reshape(-1, 4).mean(axis=1)               # 1 s prumery (bezmrtva doba)
tb1 = tg[:n4].reshape(-1, 4).mean(axis=1)


def adev_y(yv, m):
    c = np.cumsum(np.concatenate(([0.0], yv)))
    ybar = (c[m:] - c[:-m]) / m
    dd = ybar[m:] - ybar[:-m]
    return np.sqrt(0.5 * np.mean(dd ** 2)), len(dd)


try:
    last = open(PB + ".stab.jsonl").read().strip().splitlines()[-1]
    fw = {int(p[0]): p for p in json.loads(last.split(" ", 1)[1])["p"] if p[0] in (1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000)}
except Exception:
    fw = {}
print("  tau[s]   offline(cely beh)  offline(po orezu)  firmware(kumul.)   fw/offline(cely)")
for tau in (1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000):
    a_all, n_all = adev_y(yb, tau)
    a_cut, n_cut = adev_y(yb[tb1 >= T0], tau) if (tb1 >= T0).sum() > 4 * tau else (np.nan, 0)
    f_ = fw.get(tau)
    print("  %6d     %.3e         %.3e         %s       %s" % (tau, a_all, a_cut,
          ("%.3e" % f_[1]) if f_ else "   -   ", ("%.2f" % (f_[1] / a_all)) if f_ else "-"))
print("(firmware pocita z cele relace od posledniho resetu statistik; filtr izolovanych odlehlych je v obou)")

print()
print("CITLIVOST kovariancni metody (cov(y_citac, y_faze) -> horni mez sumu OCXO):")
print("  predpoklad H_O: CELY prebytek faze je od OCXO. Pak cov = -var(O) a koherence^2 = var(O)^2 / (var_c * var_x).")
print("  tau[s]  ADEV_citac  ADEV_faze  prebytek(MDEV)  koh^2 pri H_O   N pro 95 % detekci (~3/koh^2)   doba N*tau")
for tau in (64, 128, 256, 512):
    ac, _ = adev_y(yb[tb1 >= T0], tau)
    rr = [r for r in R if abs(r[0] - tau) < 1e-9][0]
    mq = np.sqrt(3.0) * sx * 1e-9 / tau ** 1.5
    ex = np.sqrt(max(rr[2] ** 2 - mq ** 2, 0.0))
    coh2 = ex ** 4 / (ac ** 2 * rr[1] ** 2)
    N = 3.0 / coh2
    print("  %5d   %.2e   %.2e   %.2e      %.4f            %9.0f                     %.1f dne" % (tau, ac, rr[1], ex, coh2, N, N * tau / 86400.0))
print("  (soucasny beh ma na tau 128 s jen ~250 nezavislych bloku; koherenci pod ~0,02 nelze od nuly odlisit)")

# ------------------------------------------------------------------ 9. spektrum faze
hr("9. SPEKTRUM FAZE xr: PREBYTEK NAD KVANTIZACI (typ sumu)")
fp, Sx, ns_ = welch(xcut, 1.0, 2048)
Sq = 2.0 * (sx ** 2)              # jednostranne PSD bileho PM: 2 sigma^2 / fs   [ns^2/Hz]
print("Welch: segment 2048 s, %d segmentu; bile PM (kvantizace) = %.1f ns^2/Hz (sigma_x = %.2f ns)" % (ns_, Sq, sx))
print("  pasmo [Hz]       S_x [ns^2/Hz]   prebytek nad kvantizaci   (sklon f^-n z dvojic sousednich pasem)")
bands = [(0.0005, 0.001), (0.001, 0.002), (0.002, 0.004), (0.004, 0.008), (0.008, 0.016), (0.016, 0.032), (0.032, 0.064), (0.064, 0.128), (0.128, 0.25), (0.25, 0.5)]
ex_list = []
for lo, hi in bands:
    m = (fp >= lo) & (fp < hi)
    sm = float(np.mean(Sx[m]))
    ex_ = max(sm - Sq, 0.0)
    ex_list.append(((lo * hi) ** 0.5, ex_))
    print("  %.4f - %.4f   %11.1f      %11.1f" % (lo, hi, sm, ex_))
print("  sklony prebytku (log-log, sousedni pasma):", ", ".join("%.1f" % (math.log10(b[1] / a[1]) / math.log10(b[0] / a[0])) if a[1] > 0 and b[1] > 0 else "-" for a, b in zip(ex_list[:-1], ex_list[1:])))
print("  (bile FM: -2, flicker FM: -3, flicker PM: -1, bile PM: 0, nahodna chuze FM: -4)")
