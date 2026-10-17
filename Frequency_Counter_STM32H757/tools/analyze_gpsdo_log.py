import csv, json, sys
import numpy as np

P = sys.argv[1] if len(sys.argv) > 1 else "docs/audit/data/2026-10-17_noc.csv"   # pouziti: python tools/analyze_gpsdo_log.py <csv>
rows = list(csv.DictReader(open(P)))
def col(n):
    out = []
    for r in rows:
        try: out.append(float(r[n]))
        except: out.append(np.nan)
    return np.array(out)
wall = np.array([np.datetime64(r["wall"]) for r in rows])
t = (wall - wall[0]).astype("timedelta64[ms]").astype(float) / 1000.0
f = col("freq_hz"); y = col("gp_y_ppt"); xr = col("gp_xr_ns"); x = col("gp_x_ns")
code = col("gp_code"); to = col("t_ocxo"); tb = col("t_board"); seq = col("seq_meas")
state = [r["gp_state"] for r in rows]; lk = col("gp_locked")
print("radku", len(rows), "delka h", round(t[-1] / 3600, 2))
dt = np.diff(t); print("mezery >3 s:", int((dt > 3).sum()), "max dt", dt.max())
print("stav ne-LOCK:", sum(1 for s in state if s != "LOCK"), " locked==0:", int((lk == 0).sum()))
ds = np.diff(seq); print("seq skoky !=1 nebo 2:", int(((ds != 2) & (ds != 1)).sum()), " pomer krok2:", round(float((ds == 2).mean()), 3))

# --- hodinova tabulka
print()
print("hod  n    f_ext[ppb] sd   y[ppt] sd   xr_mean xr_sd pp   code   t_ocxo t_board")
for h0 in range(0, 10):
    m = (t >= h0 * 3600) & (t < (h0 + 1) * 3600)
    if m.sum() < 10: continue
    fe = (f[m] - 1e7) / 1e7 * 1e9
    print("%2d %5d  %8.3f %6.3f  %7.1f %6.1f  %7.1f %5.1f %4.0f  %8.2f %6.2f %6.2f" % (
        h0, m.sum(), np.nanmean(fe), np.nanstd(fe), np.nanmean(y[m]), np.nanstd(y[m]),
        np.nanmean(xr[m]), np.nanstd(xr[m]), np.nanmax(xr[m]) - np.nanmin(xr[m]),
        np.nanmean(code[m]), np.nanmean(to[m]), np.nanmean(tb[m])))

# --- orez rozbehu: od 1 h (smycka v LOCK od ~22:15 ustalena)
T0 = 3600.0
m = t >= T0
print()
print("== po orezu prvni hodiny: n =", int(m.sum()))
fe = (f[m] - 1e7) / 1e7 * 1e9
print("freq vs ext GPSDO [ppb]: mean %.3f  sd %.3f  min %.3f max %.3f" % (fe.mean(), fe.std(), fe.min(), fe.max()))
print("y [ppt]: mean %.1f sd %.1f ; xr [ns]: mean %.1f sd %.2f min %.0f max %.0f (pp %.0f)" % (
    np.nanmean(y[m]), np.nanstd(y[m]), np.nanmean(xr[m]), np.nanstd(xr[m]), np.nanmin(xr[m]), np.nanmax(xr[m]),
    np.nanmax(xr[m]) - np.nanmin(xr[m])))
# trend fe: linearni regrese
A = np.vstack([t[m] / 3600, np.ones(m.sum())]).T
sl, ic = np.linalg.lstsq(A, fe, rcond=None)[0]
print("trend freq vs ext: %.4f ppb/h  (zacatek %.3f, konec %.3f ppb)" % (sl, ic + sl * t[m][0] / 3600, ic + sl * t[m][-1] / 3600))
for k in (0, 1, 2, 3, 4, 5, 6, 7, 8):
    mm = (t >= (1 + k) * 3600) & (t < (2 + k) * 3600)
    if mm.sum() > 50: print("  hodina %d: ext-offset %.3f ppb" % (1 + k, np.nanmean((f[mm] - 1e7) / 1e7 * 1e9)))

# --- DAC kod: drift, teplota
print()
mc = (t >= T0) & np.isfinite(code)
Ac = np.vstack([t[mc] / 86400, np.ones(mc.sum())]).T
sc = np.linalg.lstsq(Ac, code[mc], rcond=None)[0][0]
print("kod DAC: zacatek(1h) %.2f, konec %.2f ; sklon %.1f LSB/den -> %.2f ppb/den (K=3.4e-11/LSB)" % (
    code[mc][:300].mean(), code[mc][-300:].mean(), sc, sc * 3.4e-2))
for a, b in (("t_ocxo", to), ("t_board", tb)):
    r = np.corrcoef(b[mc], code[mc])[0, 1]
    print("korelace kod vs %s: r=%.3f ; %s: %.2f -> %.2f C" % (a, r, a, b[mc][:300].mean(), b[mc][-300:].mean()))
# regrese kod ~ a + b*t_ocxo + c*cas
Ar = np.vstack([to[mc], t[mc] / 3600, np.ones(mc.sum())]).T
co = np.linalg.lstsq(Ar, code[mc], rcond=None)[0]
print("kod = %.1f*T_ocxo + %.2f*h + c  -> tempco %.2f ppb/C ; cas %.3f LSB/h = %.2f ppb/den (POZOR kolinearita)" % (
    co[0], co[1], co[0] * 3.4e-2, co[1], co[1] * 24 * 3.4e-2))

# --- fazova stabilita z x (2 s grid), TDEV/ADEV z fazy
def adev_tdev(xns, tau0, ms):
    xs = xns * 1e-9; N = len(xs); out = []
    for m_ in ms:
        if N - 2 * m_ < 8: continue
        d = xs[2 * m_:] - 2 * xs[m_:N - m_] + xs[:N - 2 * m_]
        a = np.sqrt(0.5 * np.mean(d ** 2)) / (m_ * tau0)           # ADEV (overlapping)
        s = np.cumsum(np.concatenate(([0.0], d)))
        sj = s[m_:] - s[:-m_]
        mv = np.sqrt(0.5 * np.mean(sj ** 2)) / (m_ * m_ * tau0)    # MDEV
        out.append((m_ * tau0, a, mv, m_ * tau0 * mv / np.sqrt(3) * 1e9, len(d)))
    return out
mx = (t >= T0) & np.isfinite(xr)
tt = t[mx]; xx = xr[mx]
# rovnomerna mrizka 2 s
grid = np.arange(tt[0], tt[-1], 2.0); xg = np.interp(grid, tt, xx)
print()
print("faze xr (vs GPS 1PPS, smycka ZAPNUTA) - tau[s]  ADEV(x)  MDEV  TDEV[ns]  n")
for r in adev_tdev(xg, 2.0, [1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 1500, 2500]):
    print("  %7.0f  %.3e  %.3e  %7.3f  %d" % r)
# ADEV z externi freq (1 s hradlo, kazdy 2. vzorek)
ye = (f - 1e7) / 1e7
mm = (t >= T0)
tg = t[mm]; yv = ye[mm]; okv = np.isfinite(yv); tg = tg[okv]; yv = yv[okv]
bi = ((tg - tg[0]) // 2.0).astype(int); nb = bi.max() + 1                      # kose po 2 s, vzorky v kosi se PRUMERUJI (rychle logovani)
cnt = np.bincount(bi, minlength=nb); yg = np.bincount(bi, weights=yv, minlength=nb) / np.maximum(cnt, 1)
yg = np.where(cnt > 0, yg, np.interp(np.arange(nb), np.nonzero(cnt)[0], yg[cnt > 0]))
print("vzorku v kosi 2 s: prumer %.1f" % cnt[cnt > 0].mean())
print()
print("ADEV(freq vs externi GPSDO, tau0=2 s, mrtva doba 50 %) tau  sigma")
for m_ in (1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2000):
    N = len(yg)
    if N < 4 * m_: continue
    c = np.cumsum(np.concatenate(([0.0], yg)))
    yb = (c[m_:] - c[:-m_]) / m_
    d = yb[m_:] - yb[:-m_]
    print("  %6.0f  %.3e" % (2.0 * m_, np.sqrt(0.5 * np.mean(d ** 2))))

# --- stab.jsonl: posledni zaznam
print()
last = open(P + ".stab.jsonl").read().strip().split("\n")[-1]
js = json.loads(last.split(" ", 1)[1])
print("firmware ADEV (posledni zaznam %s) n=%d tau0=%s drift=%s off=%s" % (last.split(" ")[0], js["n"], js["tau0"], js["drift"], js["off"]))
sel = [p for p in js["p"] if p[0] in (1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000)]
for p in sel: print("  tau %6s  sigma %.3e  mdev %.3e  hdev %.3e  n=%s" % (p[0], p[1], p[2], p[3], p[4]))
