"""#312: horni mez sumu OCXO z kovariance cov(y_citac, y_faze).

Model (y = relativni odchylka kmitoctu):
  citac meri externi GPSDO (E) vuci OCXO (O):  y_c = y_E - y_O
  smycka meri OCXO vuci nasi GPS (G1):         y_x = y_O - y_G1
  cov(y_c, y_x) = -var(O) - cov(E, G1)
cov(E, G1) >= 0 (spolecne chyby GPS: drahy, hodiny druzic, ionosfera), proto  var(O) <= -cov(y_c, y_x).
Pouziva se Allanova varianta: prvni diference po sobe jdoucich bloku delky tau (zbavi offsetu a driftu).
Pouziti: python cov312.py <csv> <orez_s> [tau,tau,...]
"""
import csv
import sys
import numpy as np

P = sys.argv[1]
T0 = float(sys.argv[2]) if len(sys.argv) > 2 else 3600.0
TAUS = [float(v) for v in sys.argv[3].split(",")] if len(sys.argv) > 3 else [10, 20, 50, 100, 200, 500]

rows = list(csv.DictReader(open(P)))


def col(n):
    out = []
    for r in rows:
        try:
            out.append(float(r[n]))
        except Exception:
            out.append(np.nan)
    return np.array(out)


up = col("uptime_s")
f = col("freq_hz")
xr = col("gp_xr_ns")
sats = col("gps_sats")
hdop = col("gps_hdop")
to = col("t_ocxo")
state = np.array([r["gp_state"] for r in rows])
wall = np.array([np.datetime64(r["wall"]) for r in rows])
t = (wall - wall[0]).astype("timedelta64[ms]").astype(float) / 1000.0
yc = (f - 1e7) / 1e7 * 1e9          # ppb

import os
W0 = float(os.environ.get("W0", "0")) * 3600.0
W1 = float(os.environ.get("W1", "1e9")) * 3600.0
ok = (state == "LOCK") & np.isfinite(f) & np.isfinite(xr) & (t >= T0) & (t >= W0) & (t < W1)
print("%s: radku %d, pouzito %d, delka %.2f h" % (P.split("/")[-1], len(rows), ok.sum(), (t[ok][-1] - t[ok][0]) / 3600 if ok.any() else 0))


def blocks(tau):
    """vrati pole (yc_blok, yx_blok, t_start, sats, hdop, to) pro souvisle bloky delky tau"""
    tt, c, x = t[ok], yc[ok], xr[ok]
    s, h, o = sats[ok], hdop[ok], to[ok]
    k = np.floor((tt - tt[0]) / tau).astype(int)
    out = []
    for b in np.unique(k):
        m = k == b
        if m.sum() < max(2, 0.4 * tau / np.median(np.diff(tt))):   # moc dira v bloku
            out.append(None)
            continue
        out.append((c[m].mean(), x[m][-1], x[m][0], tt[m][-1], tt[m][0], np.nanmean(s[m]), np.nanmean(h[m]), np.nanmean(o[m]), tt[0] + b * tau))
    return out


def analyse(tau, boot=400, rng=np.random.default_rng(1)):
    bl = blocks(tau)
    # y_x z faze: (x_konec_bloku - x_konec_predchoziho) / delka; potrebujeme dva sousedni platne bloky
    yc_b, yx_b, tb, sb, hb, ob = [], [], [], [], [], []
    for i in range(1, len(bl)):
        if bl[i] is None or bl[i - 1] is None:
            yc_b.append(np.nan); yx_b.append(np.nan); tb.append(np.nan); sb.append(np.nan); hb.append(np.nan); ob.append(np.nan)
            continue
        dt = bl[i][3] - bl[i - 1][3]
        yx_b.append((bl[i][1] - bl[i - 1][1]) / dt)       # ns/s = ppb
        yc_b.append(bl[i][0])
        tb.append(bl[i][8]); sb.append(bl[i][5]); hb.append(bl[i][6]); ob.append(bl[i][7])
    yc_b, yx_b = np.array(yc_b), np.array(yx_b)
    dc = np.diff(yc_b)
    dx = np.diff(yx_b)
    good = np.isfinite(dc) & np.isfinite(dx)
    dc, dx = dc[good], dx[good]
    tb_, sb_, hb_, ob_ = (np.array(a)[1:][good] for a in (tb, sb, hb, ob))
    n = len(dc)
    if n < 20:
        return None
    Vc, Vx, C = (dc * dc).mean() / 2, (dx * dx).mean() / 2, (dc * dx).mean() / 2
    # blokovy bootstrap (bloky ~30 min), kvuli korelaci reziduí
    L = max(3, int(1800 / tau))
    nb = max(1, n // L)
    est = []
    for _ in range(boot):
        idx = np.concatenate([np.arange(s0, min(s0 + L, n)) for s0 in rng.integers(0, max(1, n - L + 1), nb)])
        est.append((dc[idx] * dx[idx]).mean() / 2)
    se = np.std(est)
    return dict(tau=tau, n=n, Vc=Vc, Vx=Vx, C=C, se=se, corr=C / np.sqrt(Vc * Vx), t=tb_, s=sb_, h=hb_, o=ob_, dx=dx, dc=dc)


print()
print(" tau   n   ADEV_citac  ADEV_faze   corr    cov[ppb^2]  +-SE      OCXO <= (95 %%)   [jednotky 1e-11]")
res = {}
for tau in TAUS:
    r = analyse(tau)
    if r is None:
        print("%5.0f  malo dat" % tau)
        continue
    res[tau] = r
    up95 = -r["C"] + 1.96 * r["se"]
    print("%5.0f %5d  %8.1f   %8.1f   %6.3f  %9.4f  %7.4f   %8.1f     (-cov %.4f)" % (
        tau, r["n"], np.sqrt(r["Vc"]) * 100, np.sqrt(r["Vx"]) * 100, r["corr"], r["C"], r["se"],
        np.sqrt(max(up95, 0.0)) * 100, -r["C"]))
print("(ADEV v 1e-11: ppb * 100;  zaporna cov = OCXO sum + kladna cov(E,G1) -> SIGN: cov<0 ocekavano)")

# --- podminky GPS: ADEV faze po hodinach vs sats / HDOP / denni doba / teplota
for tau in (50, 100, 200):
    if tau not in res:
        continue
    r = res[tau]
    hr = np.floor(r["t"] / 3600.0).astype(int)
    print()
    print("tau %d s: ADEV faze (y_x) po hodinach vs podminky GPS" % tau)
    print(" hod  n   ADEV_faze[1e-11]  ADEV_citac[1e-11]  sats   HDOP  t_ocxo")
    rowsH = []
    for hh in np.unique(hr):
        m = hr == hh
        if m.sum() < 15:
            continue
        a = np.sqrt((r["dx"][m] ** 2).mean() / 2) * 100
        b = np.sqrt((r["dc"][m] ** 2).mean() / 2) * 100
        rowsH.append((a, b, np.nanmean(r["s"][m]), np.nanmean(r["h"][m]), np.nanmean(r["o"][m])))
        print("%4d %4d  %10.1f %16.1f   %5.1f  %5.2f  %6.2f" % (hh, m.sum(), a, b, *rowsH[-1][2:]))
    if len(rowsH) >= 4:
        A = np.array(rowsH)
        for j, nm in ((2, "sats"), (3, "HDOP"), (4, "t_ocxo")):
            print("   korelace ADEV_faze ~ %s: r = %.2f (n=%d hodin)" % (nm, np.corrcoef(A[:, 0], A[:, j])[0, 1], len(A)))
        print("   korelace ADEV_faze ~ ADEV_citac: r = %.2f" % np.corrcoef(A[:, 0], A[:, 1])[0, 1])
