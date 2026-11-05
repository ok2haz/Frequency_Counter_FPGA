"""V8 (audit A2, F-0246): ověření aproximace EDF overlapping ADEV pro VELKÉ M (stovky až desítky tisíc vzorků).

Firmware (`screen_main.c` adev_edf_alpha) používá vzorce SP1065 / Howe-Allan-Barnes; v komentáři je Monte Carlo ověření
jen pro M = 24 a 120. S akumulátory A2 (v0.23.0) dostává EDF M v řádu 10^3..10^5.

Postup: pro každý typ šumu alfa (2 bílý PM, 1 blikavý PM, 0 bílý FM, -1 blikavý FM, -2 RW FM) se vygeneruje R realizací
M vzorků y, spočte se overlapping AVAR(m) a z rozptylu AVAR empiricky EDF_emp = 2·E[AVAR]^2 / Var[AVAR]
(platí Var[AVAR] = 2·AVAR^2/EDF). Porovnání s formulí z firmware (port níže).

Použití: python 2026-10-18_edf_velke_M.py [R] — výstup tabulka alfa, M, m, EDF_emp, EDF_formule, poměr.
"""
import sys
import math
import numpy as np

rng = np.random.default_rng(20261018)


def flicker(n, rng):
    """1/f šum (výkon ~ 1/f) délky n přes FFT syntézu (pevná amplituda, bez normalizace realizace)."""
    nn = 1
    while nn < 2 * n:
        nn *= 2
    w = rng.standard_normal(nn)
    W = np.fft.rfft(w)
    f = np.arange(len(W), dtype=float)
    f[0] = 1.0
    W = W / np.sqrt(f)
    W[0] = 0.0
    # POZOR: bez normalizace realizace jeji vlastni smerodatnou odchylkou. Takove deleni je dominovano nahodnymi nejnizsimi
    # frekvencemi a do AVAR vnasi umely rozptyl (prvni verze skriptu proto dala EDF blikaveho sumu ~50 misto stovek).
    # Pevna amplituda: uvazuje se jen o relativnim rozptylu AVAR (EDF), absolutni uroven nevadi.
    return np.fft.irfft(W, nn)[:n] * math.sqrt(nn)


def gen_y(alpha, M, rng):
    """M vzorků relativního kmitočtu y pro daný typ šumu (alfa: 2 WPM, 1 FPM, 0 WFM, -1 FFM, -2 RWFM)."""
    if alpha == 0:
        return rng.standard_normal(M)
    if alpha == -2:
        return np.cumsum(rng.standard_normal(M))
    if alpha == -1:
        return flicker(M, rng)
    if alpha == 2:
        return np.diff(rng.standard_normal(M + 1))
    if alpha == 1:
        return np.diff(flicker(M + 1, rng))
    raise ValueError(alpha)


def avar_overlap(y, m):
    """overlapping AVAR z průměrů kmitočtu přes m vzorků (SP1065)."""
    M = len(y)
    S = np.concatenate(([0.0], np.cumsum(y)))
    av = (S[m:] - S[:-m]) / m              # průměry přes m, M-m+1 hodnot
    d = av[m:] - av[:-m]                   # rozdíly sousedních průměrů, M-2m+1 hodnot
    return 0.5 * np.mean(d * d)


def edf_formula(alpha, M, m):
    """Port `adev_edf_alpha` z screen_main.c (N = M + 1)."""
    N = M + 1.0
    mm = float(m)
    if alpha == 2:
        e = (N + 1.0) * (N - 2.0 * mm) / (2.0 * (N - mm))
    elif alpha == 1:
        e = math.exp(math.sqrt(math.log((N - 1.0) / (2.0 * mm)) * math.log((2.0 * mm + 1.0) * (N - 1.0) / 4.0)))
    elif alpha == -1:
        e = (2.0 * (N - 2.0) ** 2 / (2.3 * N - 4.9)) if m == 1 else 5.0 * N * N / (4.0 * mm * (N + 3.0 * mm))
    elif alpha == -2:
        e = (N - 2.0) / mm * ((N - 1.0) ** 2 - 3.0 * mm * (N - 1.0) + 4.0 * mm * mm) / ((N - 3.0) ** 2)
    else:
        e = (3.0 * (N - 1.0) / (2.0 * mm) - 2.0 * (N - 2.0) / N) * 4.0 * mm * mm / (4.0 * mm * mm + 5.0)
    return e if e >= 1.0 else 1.0


def main():
    R = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    cases = [(1000, (1, 2, 5, 10, 50)), (10000, (1, 5, 10, 100)), (36000, (1, 10, 100, 1000))]
    names = {2: "bily PM", 1: "blikavy PM", 0: "bily FM", -1: "blikavy FM", -2: "RW FM"}
    print("R = %d realizaci; chyba EDF_emp ~ sqrt(2/(R-1)) = %.0f %%" % (R, 100 * math.sqrt(2.0 / (R - 1))))
    print("%-11s %6s %5s %10s %10s %7s" % ("sum", "M", "m", "EDF_emp", "EDF_form", "pomer"))
    bad = 0
    for M, ms in cases:
        Ruse = R if M <= 10000 else max(60, R // 3)
        for alpha in (2, 1, 0, -1, -2):
            vals = {m: [] for m in ms}
            for _ in range(Ruse):
                y = gen_y(alpha, M, rng)
                for m in ms:
                    vals[m].append(avar_overlap(y, m))
            for m in ms:
                v = np.array(vals[m])
                emp = 2.0 * v.mean() ** 2 / v.var(ddof=1)
                fo = edf_formula(alpha, M, m)
                r = emp / fo
                flag = "" if 0.75 <= r <= 1.33 else "  <== MIMO"
                if flag:
                    bad += 1
                print("%-11s %6d %5d %10.1f %10.1f %7.2f%s" % (names[alpha], M, m, emp, fo, r, flag))
    print("mimo +-25 %% (ocekavana chyba odhadu + aproximace): %d" % bad)


if __name__ == "__main__":
    main()
