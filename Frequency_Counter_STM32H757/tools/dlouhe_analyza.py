"""Vyhodnoceni behu tools/dlouhe_mereni.ps1.
python tools/dlouhe_analyza.py <predpona> [faze]        napr. docs/audit/data/2026-10-18_2135_dlouhe   (faze 1 | 2 | obe, vychozi obe)

FAZE 1 (smycka / DAC / dither, bloky OFF, D200, D100, D2): po blocich ADEV A pri tau 0,25-5 s, podil skoku a RMS rozdilu sousednich oken,
  zamitani regrese z snap.csv, souhrn podle typu bloku a podle cyklu (aby se oddelil vliv denni doby).
FAZE 2 (volne bezici OCXO, smycka OFF): ADEV A (ext. GPSDO proti OCXO), ADEV fazi xr (OCXO proti nasi GPS na strese), ADEV rozdilu D = A + xr
  (ext. GPSDO proti nasi GPS; OCXO se vykrati) a z nich tri-rohy klobouk (three-cornered hat): ADEV OCXO, ext. GPSDO, nasi GPS.
Jen numpy."""
import csv
import datetime as dt
import sys

import numpy as np

pre = sys.argv[1]
which = sys.argv[2] if len(sys.argv) > 2 else "obe"
T = 0.25


def load(fn):
    rows = list(csv.DictReader(open(fn, encoding="ascii", errors="replace")))
    cols = {}
    for k in rows[0]:
        a = []
        for r in rows:
            try:
                a.append(float(r[k]))
            except (ValueError, TypeError):
                a.append(np.nan)
        cols[k] = np.array(a)
    cols["_wall"] = np.array([r["wall"] for r in rows])
    return cols


def tosec(w):
    return np.array([dt.datetime.strptime(x[:19], "%Y-%m-%dT%H:%M:%S").timestamp() for x in w])


def uniq(D):
    ok = np.isfinite(D["seq_meas"]) & np.isfinite(D["freq_hz"])
    _, first = np.unique(D["seq_meas"][ok], return_index=True)
    idx = np.flatnonzero(ok)[np.sort(first)]
    return {k: v[idx] for k, v in D.items()}


def mad(x):
    return 1.4826 * np.median(np.abs(x - np.median(x)))


def adev_y(y, m):
    c = np.cumsum(np.concatenate(([0.0], y)))
    yb = (c[m:] - c[:-m]) / m
    dd = yb[m:] - yb[:-m]
    return np.sqrt(0.5 * np.mean(dd ** 2)) if len(dd) > 2 else np.nan


def adev_x(x, m, tau0):
    """ADEV z fazi x [s] (vzorek po tau0) pro tau = m*tau0, prekryvajici se."""
    if len(x) < 2 * m + 2:
        return np.nan
    d = x[2 * m:] - 2 * x[m:-m] + x[:-2 * m]
    return np.sqrt(np.mean(d ** 2) / (2.0 * (m * tau0) ** 2))


def grid(D):
    seq = D["seq_meas"].astype(np.int64)
    g = np.arange(seq[0], seq[-1] + 1)
    have = np.zeros(len(g), bool)
    have[seq - seq[0]] = True
    y = np.interp(g, seq, (D["freq_hz"] - 1e7) / 1e7)
    return g, have, y


# ======================================== FAZE 1 ==============================================
def faze1():
    D = uniq(load(pre + "_faze1_smycka.csv"))
    B = list(csv.DictReader(open(pre + ".blocks.csv")))
    S = list(csv.DictReader(open(pre + ".snap.csv")))
    wall = tosec(D["_wall"])
    seq = D["seq_meas"].astype(np.int64)
    snapt = np.array([dt.datetime.strptime(r["wall"], "%Y-%m-%dT%H:%M:%S").timestamp() for r in S])

    def sn(r, k):
        try:
            return float(r[k])
        except (ValueError, TypeError):
            return np.nan

    ph1 = [b for b in B if b["faze"] == "1"]
    # globalni prah skoku: MAD rozdilu sousednich oken pres celou fazi 1
    g, have, y = grid(D)
    d_all = np.diff(y)
    core = mad(d_all[have[1:] & have[:-1]])
    thr = 6 * core
    print("=== FAZE 1: radku %d, jadro rozdilu (MAD) %.2e, prah skoku %.2e ===" % (len(seq), core, thr))
    print("blok  start             vzorku  chybi%%  ADEV0,25   ADEV1      ADEV2      ADEV5      skoky%%  RMSd/jadro  zamitnuti%% t_ocxo")
    res = []
    for i, b in enumerate(ph1):
        t0 = dt.datetime.strptime(b["wall"], "%Y-%m-%dT%H:%M:%S").timestamp() + 60 * float(b["skip_min"])
        t1 = dt.datetime.strptime(b["wall"], "%Y-%m-%dT%H:%M:%S").timestamp() + 60 * float(b["delka_min"]) - 2
        m = (wall >= t0) & (wall < t1)
        if m.sum() < 500:
            continue
        s = seq[m]
        gg = np.arange(s[0], s[-1] + 1)
        hv = np.zeros(len(gg), bool); hv[s - s[0]] = True
        yy = np.interp(gg, s, (D["freq_hz"][m] - 1e7) / 1e7)
        dd = np.diff(yy)
        v = hv[1:] & hv[:-1]
        ad = [adev_y(yy, k) for k in (1, 2, 4, 20)]
        jump = 100.0 * np.mean(np.abs(dd[v]) > thr)
        rms = np.sqrt(np.mean(dd[v] ** 2)) / core
        # zamitani regrese z prirustku snimku mezi t0 a t1
        sm = np.flatnonzero((snapt >= t0 - 150) & (snapt <= t1 + 150))
        rej = np.nan
        if len(sm) >= 2:
            a, c = sm[0], sm[-1]
            du = sn(S[c], "regr_used") - sn(S[a], "regr_used"); dr = sn(S[c], "regr_rej") - sn(S[a], "regr_rej")
            if du + dr > 0:
                rej = 100.0 * dr / (du + dr)
        res.append(dict(name=b["blok"], cyc=i // 4, ad=ad, jump=jump, rms=rms, rej=rej, t=np.nanmean(D["t_ocxo"][m]) if "t_ocxo" in D else np.nan, start=b["wall"], n=int(m.sum()), miss=100 * (1 - hv.mean())))
        r = res[-1]
        print("%-4s %s %6d  %5.1f   %.2e  %.2e  %.2e  %.2e  %5.2f   %5.2f      %5.2f     %.2f" %
              (r["name"], b["wall"][5:16], r["n"], r["miss"], ad[0], ad[1], ad[2], ad[3], jump, rms, r["rej"], r["t"]))
    print("\n--- souhrn podle typu bloku (prumer a rozptyl pres cykly) ---")
    print("typ    n  ADEV0,25 (prum+-sd)       ADEV1 (prum+-sd)         skoky%% (prum+-sd)     zamitnuti%% (prum+-sd)")
    for nm in ("OFF", "D200", "D100", "D2"):
        rr = [r for r in res if r["name"] == nm]
        if not rr:
            continue
        a0 = np.array([r["ad"][0] for r in rr]); a1 = np.array([r["ad"][1] for r in rr])
        jj = np.array([r["jump"] for r in rr]); rj = np.array([r["rej"] for r in rr])
        print("%-5s %2d  %.2e +- %.1e   %.2e +- %.1e   %5.2f +- %4.2f        %5.2f +- %4.2f" %
              (nm, len(rr), a0.mean(), a0.std(ddof=1) if len(rr) > 1 else 0, a1.mean(), a1.std(ddof=1) if len(rr) > 1 else 0,
               jj.mean(), jj.std(ddof=1) if len(rr) > 1 else 0, np.nanmean(rj), np.nanstd(rj, ddof=1) if len(rr) > 1 else 0))
    # parove porovnani v ramci cyklu: kazdy ON blok proti OFF stejneho cyklu (omezi denni dobu)
    print("\n--- ADEV(0,25 s) pomer blok / OFF stejneho cyklu (cim blize 1, tim mensi vliv) ---")
    for c in sorted(set(r["cyc"] for r in res)):
        off = [r for r in res if r["cyc"] == c and r["name"] == "OFF"]
        if not off:
            continue
        o = off[0]["ad"][0]
        print("cyklus %d: " % (c + 1) + "  ".join("%s %.2f" % (r["name"], r["ad"][0] / o) for r in res if r["cyc"] == c))


# ======================================== FAZE 2 ==============================================
def faze2():
    D = uniq(load(pre + "_faze2_gps.csv"))
    wall = tosec(D["_wall"])
    seq = D["seq_meas"].astype(np.int64)
    g, have, y = grid(D)
    print("\n=== FAZE 2: radku %d, trvani %.2f h, chybi %.2f %% oken ===" % (len(seq), (seq[-1] - seq[0]) * T / 3600.0, 100 * (1 - have.mean())))
    for c in ("gp_state", "t_ocxo", "t_board", "gps_sats", "gp_code", "dac_code"):
        if c in D and np.isfinite(D[c]).any():
            v = D[c][np.isfinite(D[c])]
            print("  %-9s min %.3f  stred %.3f  max %.3f" % (c, v.min(), v.mean(), v.max()))
    if "gp_state" in D:
        st = D["gp_state"]
    # ---- ADEV A (ext. GPSDO proti OCXO), po 2 h
    cut = int(600 / T)
    print("\n--- ADEV A (ext. GPSDO - OCXO), celek po orezu 10 min ---")
    taus = [0.25, 1, 10, 100, 1000, 5000]
    print("tau[s]   " + "  ".join("%9g" % t for t in taus))
    print("ADEV A   " + "  ".join("%.3e" % adev_y(y[cut:], max(1, int(round(t / T)))) for t in taus))
    print("\n--- stacionarita: ADEV A po 2 h blocich (tau 1, 10, 100 s) ---")
    nb = int(2 * 3600 / T)
    for k in range(0, len(y) - nb + 1, nb):
        yy = y[k:k + nb]
        print("  %5.1f-%5.1f h:  %.2e  %.2e  %.2e" % (k * T / 3600, (k + nb) * T / 3600, adev_y(yy, 4), adev_y(yy, 40), adev_y(yy, 400)))
    # ---- faze xr (OCXO proti nasi GPS), 1 s
    xr = D["gp_xr_ns"]
    ok = np.isfinite(xr)
    t_row = (seq - seq[0]) * T
    tr = t_row[ok]; xv = xr[ok]
    dx = np.diff(xv)
    brk = np.abs(dx) > 200.0
    dx[brk] = 0.0
    print("\nxr_ns: %d vzorku, preruseni (skok > 200 ns): %d, rozsah %.0f .. %.0f ns, posun za beh %.0f ns" % (ok.sum(), brk.sum(), xv.min(), xv.max(), xv[-1] - xv[0]))
    xc = np.concatenate(([0.0], np.cumsum(dx)))
    tgrid = np.arange(0, tr[-1], 1.0)
    Pr = np.interp(tgrid, tr, xc) * 1e-9                   # [s]
    # faze A: ext - OCXO: cumsum(y)*T
    xA = np.concatenate(([0.0], np.cumsum(y[:-1] * T)))   # [s] na siti g (T)
    tA = (g - g[0]) * T
    PA = np.interp(tgrid, tA + (g[0] - seq[0]) * T, xA)
    # test znamenka: ktera kombinace faze se vykrati OCXO (mensi sum na dlouhem tau)
    Dp, Dm = PA + Pr, PA - Pr
    m100 = 100
    ap, am = adev_x(Dp[600:], m100, 1.0), adev_x(Dm[600:], m100, 1.0)
    sgn = 1.0 if ap < am else -1.0
    print("kontrola znamenka: ADEV(100 s) z A+xr = %.2e, z A-xr = %.2e -> pouzito %s" % (ap, am, "A+xr" if sgn > 0 else "A-xr"))
    Dd = PA + sgn * Pr
    P0 = PA[600:]; R0 = Pr[600:]; D0 = Dd[600:]
    print("\n--- Tri-rohy klobouk (3CH), tau v s: A = ext.GPSDO-OCXO, R = OCXO-nase GPS(strecha, vc. kvantizace 1PPS), D = ext.GPSDO-nase GPS ---")
    print("tau      ADEV(A)    ADEV(R)    ADEV(D)  |  OCXO       ext.GPSDO  nase GPS(R+kvant.)")
    out = []
    for m in (1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000):
        a2 = adev_x(P0, m, 1.0) ** 2; r2 = adev_x(R0, m, 1.0) ** 2; d2 = adev_x(D0, m, 1.0) ** 2
        if not np.isfinite(a2 + r2 + d2):
            continue
        o2 = 0.5 * (a2 + r2 - d2) if sgn > 0 else None
        # pro sgn>0: D=A+R => OCXO se vykrati v D; sigma_D^2 = s_E^2 + s_G^2; s_A^2 = s_E^2+s_O^2; s_R^2 = s_O^2+s_G^2
        # (pro sgn<0 jde o stejny model s opacnym znamenkem faze; variance se skladaji stejne)
        o2 = 0.5 * (a2 + r2 - d2); e2 = 0.5 * (a2 + d2 - r2); gg2 = 0.5 * (r2 + d2 - a2)
        f = lambda v: ("%.2e" % np.sqrt(v)) if v > 0 else "  <0   "
        print("%5g  %.3e  %.3e  %.3e  |  %s  %s  %s" % (m, np.sqrt(a2), np.sqrt(r2), np.sqrt(d2), f(o2), f(e2), f(gg2)))
        out.append((m, np.sqrt(a2), np.sqrt(r2), np.sqrt(d2), o2, e2, gg2))
    # drift a stari OCXO: linearni fit y_A v case (ext. GPSDO drzi 0, OCXO stari)
    tt = (np.arange(len(y)) * T)[cut:] / 3600.0
    yy = y[cut:]
    p = np.polyfit(tt, yy, 1)
    print("\ndrift (A: ext.GPSDO-OCXO): %.2e / h  (%.2e / den), stredni offset %.2e" % (p[0], p[0] * 24, np.mean(yy)))
    if "t_ocxo" in D:
        tq = np.interp(np.arange(len(y)) * T, t_row, D["t_ocxo"])[cut:]
        if np.std(tq) > 0:
            print("korelace A (po 100 s prumerech) s teplotou OCXO: r = %.2f" % np.corrcoef(np.convolve(yy, np.ones(400) / 400, "valid")[::400], np.convolve(tq, np.ones(400) / 400, "valid")[::400])[0, 1])


if which in ("1", "obe"):
    faze1()
if which in ("2", "obe"):
    faze2()
