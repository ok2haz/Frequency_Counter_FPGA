"""F-0293: vyhodnoceni lokalni strmosti K z f0293_k_kroky.ps1 (spoustet: python -I tools/f0293_vyhodnot.py k_kroky.csv).

Sklon surove faze TIE [ns] v case [s] = relativni odchylka kmitoctu y (1 ns/s = 1e-9). Pro kazdou fazi se
proklada primka poslednimi (faze - ZAHOZENO) minutami.

1) Sousedni zaklady: K kroku = (sklon kroku - prumer sklonu sousednich zakladnich fazi) / d. Odecita pomaly drift OCXO,
   ale kazdy krok nese sum dvou zakladu.
2) Globalni model (hlavni vysledek, 2026-11-16): sklon_j = polynom st. 2 v case (trend zakladu pres celou noc) + K * d_j
   pres vsech 17 faz; K, chyba z rozptylu rezidui (fazi), varianty K+/K-, K podle velikosti kroku, kvadraticky clen
   a odchylky jednotlivych kodu od primky (INL v LSB).
Sum jedne faze (~17 ppt pri 30 min) je putovani OCXO proti GPS na tau ~ 30 min, ne bily sum 1PPS (1-3 ppt).

K_FW = vychozi K firmwaru (GPSDO_KDAC_DEFAULT): od v0.45.21 3,85e-11 (z tohoto mereni), do v0.45.20 3,4e-11.
"""
import csv
import math
import sys
from datetime import datetime

import numpy as np

K_FW = 3.85e-11
K_OLD = 3.4e-11
ZAHOZENO_S = 600  # prvnich 10 min faze se nepouziva (prechod se nenasel, viz MERENI_A_KALIBRACE 2026-11-16; rezerva)
ST_TRENDU = 2


def sklon(body):
    n = len(body)
    if n < 5:
        return None
    mx = sum(t for t, _ in body) / n
    my = sum(v for _, v in body) / n
    sxx = sum((t - mx) ** 2 for t, _ in body)
    if sxx == 0:
        return None
    return sum((t - mx) * (v - my) for t, v in body) / sxx  # ns/s


def model(sk, cols):
    """sklon [ppt] = polynom st. ST_TRENDU (cas stredu faze) + sloupce kroku; vrati koeficienty, kovarianci, sigma, X"""
    ns = sorted(sk)
    t = np.array([sk[n][4] for n in ns])
    tc = (t - t.mean()) / (t.max() - t.min()) * 2.0
    y = np.array([sk[n][2] * 1000.0 for n in ns])
    X = np.vstack([tc ** k for k in range(ST_TRENDU + 1)] + cols).T
    c, *_ = np.linalg.lstsq(X, y, rcond=None)
    r = y - X @ c
    dof = len(y) - X.shape[1]
    s2 = float(r @ r) / dof
    return c, s2 * np.linalg.inv(X.T @ X), math.sqrt(s2), X, r


def main(path):
    faze = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f, delimiter=";"):
            if r["TIE_ns"] == "":
                continue
            t = datetime.strptime(r["cas"], "%Y-%m-%d %H:%M:%S").timestamp()
            n = int(r["faze_n"])
            faze.setdefault(n, {"typ": r["typ"], "d": int(r["d"]), "kod": int(r["kod_cil"]), "b": []})["b"].append((t, float(r["TIE_ns"])))
    sk = {}
    for n, v in faze.items():
        t0 = v["b"][0][0]
        body = [(t, x) for t, x in v["b"] if t - t0 >= ZAHOZENO_S]
        s = sklon(body)
        tm = sum(t for t, _ in body) / len(body) if body else t0
        sk[n] = (v["typ"], v["d"], s, len(body), tm)
    print("faze typ   d   sklon[ppt]  bodu")
    for n in sorted(sk):
        typ, d, s, nb, _ = sk[n]
        print("%3d  %-5s %+3d  %s  %d" % (n, typ, d, "      -" if s is None else "%+10.1f" % (s * 1000), nb))
    print()
    print("1) Sousedni zaklady: krok  d    K_lokalni[e-11/LSB]  (pomer k K firmwaru %.2f)" % (K_FW * 1e11))
    for n in sorted(sk):
        typ, d, s, nb, _ = sk[n]
        if typ != "krok" or s is None:
            continue
        sus = [sk[m][2] for m in (n - 1, n + 1) if m in sk and sk[m][2] is not None]
        if not sus:
            continue
        dy = (s - sum(sus) / len(sus)) * 1e-9
        print("%3d  %+3d   %+8.3f   (pomer %.2f)" % (n, d, dy / d * 1e11, dy / d / K_FW))

    if any(v[2] is None for v in sk.values()) or len(sk) < ST_TRENDU + 4:
        print("\nGlobalni model: malo faz nebo faze bez sklonu, preskoceno.")
        return
    ns = sorted(sk)
    d = np.array([sk[n][1] for n in ns], dtype=float)
    c, cov, sg, X, r = model(sk, [d])
    K, sK = c[-1], math.sqrt(cov[-1, -1])
    print("\n2) Globalni model (trend zakladu polynom st. %d + K*d, %d faz):" % (ST_TRENDU, len(ns)))
    print("   K = %.2f +- %.2f e-11/LSB (1 sigma), pomer k K firmwaru %.2f, k drivejsimu 3,4e-11 %.2f; sum faze %.1f ppt"
          % (K / 10, sK / 10, K / (K_FW * 1e12), K / (K_OLD * 1e12), sg))
    cpm, covpm, sgpm, *_ = model(sk, [np.where(d > 0, d, 0.0), np.where(d < 0, d, 0.0)])
    sdk = math.sqrt(covpm[-2, -2] + covpm[-1, -1] - 2 * covpm[-2, -1])
    print("   K+ = %.2f +- %.2f, K- = %.2f +- %.2f (rozdil %.2f +- %.2f) e-11/LSB"
          % (cpm[-2] / 10, math.sqrt(covpm[-2, -2]) / 10, cpm[-1] / 10, math.sqrt(covpm[-1, -1]) / 10,
             (cpm[-2] - cpm[-1]) / 10, sdk / 10))
    vel = sorted({abs(int(v)) for v in d if v != 0})
    cd, covd, *_ = model(sk, [np.where(np.abs(d) == a, d, 0.0) for a in vel])
    print("   K podle velikosti kroku: " + ", ".join("|%d| %.2f +- %.2f" % (a, cd[ST_TRENDU + 1 + i] / 10,
                                                                         math.sqrt(covd[ST_TRENDU + 1 + i, ST_TRENDU + 1 + i]) / 10)
                                                  for i, a in enumerate(vel)) + " e-11/LSB")
    cq, covq, *_ = model(sk, [d, d * d])
    print("   kvadraticky clen %.3f +- %.3f e-11/LSB^2 (nelinearita EFC/DAC v rozsahu kroku)" % (cq[-1] / 10, math.sqrt(covq[-1, -1]) / 10))
    trend = X[:, :ST_TRENDU + 1] @ c[:ST_TRENDU + 1]
    print("   odchylka kodu od primky (INL), 1 sigma na kod %.2f LSB:" % (sg / K))
    chi2 = 0.0
    for i, n in enumerate(ns):
        if sk[n][0] != "krok":
            continue
        dev = (sk[n][2] * 1000.0 - trend[i] - K * d[i]) / K
        chi2 += (dev / (sg / K)) ** 2
        print("     kod %5d (d %+d): %+5.2f LSB" % (faze[n]["kod"], int(d[i]), dev))
    print("   chi2 odchylek %.1f pro %d kodu (ocekavano ~ pocet kodu, kdyz INL pod rozlisenim)" % (chi2, int((d != 0).sum())))


if __name__ == "__main__":
    main(sys.argv[1])
