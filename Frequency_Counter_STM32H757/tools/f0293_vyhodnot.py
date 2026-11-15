"""F-0293: vyhodnoceni lokalni strmosti K z f0293_k_kroky.ps1 (spoustet: python -I tools/f0293_vyhodnot.py k_kroky.csv).

Sklon surove faze TIE [ns] v case [s] = relativni odchylka kmitoctu y (1 ns/s = 1e-9). Pro kazdou fazi se
proklada primka poslednimi (faze - ZAHOZENO) minutami; K kroku = (sklon kroku - prumer sklonu sousednich
zakladnich fazi) / d. Prumer sousednich zakladnich faze odecita pomaly drift OCXO. Porovnani s K = 3,4e-11/LSB.
"""
import csv
import sys

K_NOM = 3.4e-11
ZAHOZENO_S = 600  # prvnich 10 min faze (dosedani) se nepouziva


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


def main(path):
    from datetime import datetime
    faze = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f, delimiter=";"):
            if r["TIE_ns"] == "":
                continue
            t = datetime.strptime(r["cas"], "%Y-%m-%d %H:%M:%S").timestamp()
            n = int(r["faze_n"])
            faze.setdefault(n, {"typ": r["typ"], "d": int(r["d"]), "b": []})["b"].append((t, float(r["TIE_ns"])))
    sk = {}
    for n, v in faze.items():
        t0 = v["b"][0][0]
        body = [(t, x) for t, x in v["b"] if t - t0 >= ZAHOZENO_S]
        s = sklon(body)
        sk[n] = (v["typ"], v["d"], s, len(body))
    # TIE je znamenkove opacna podle definice smycky? Znamenko se urci z kroku +8/-8 (viz vypis), tady se uvadi surove.
    print("faze typ   d   sklon[ppt]  bodu")
    for n in sorted(sk):
        typ, d, s, nb = sk[n]
        print("%3d  %-5s %+3d  %s  %d" % (n, typ, d, "      -" if s is None else "%+10.1f" % (s * 1000), nb))
    print()
    print("krok  d    K_lokalni[e-11/LSB]  vs nominal %.2f" % (K_NOM * 1e11))
    for n in sorted(sk):
        typ, d, s, nb = sk[n]
        if typ != "krok" or s is None:
            continue
        sus = [sk[m][2] for m in (n - 1, n + 1) if m in sk and sk[m][2] is not None]
        if not sus:
            continue
        dy = (s - sum(sus) / len(sus)) * 1e-9
        print("%3d  %+3d   %+8.3f   (pomer %.2f)" % (n, d, dy / d * 1e11, dy / d / K_NOM))
    print("\nZnamenko: kladny pomer = TIE roste s kodem; pokud vychazi zaporne u vsech, je to jen konvence znamenka TIE.")
    print("Rozptyl pomeru mezi kroky (1, 2, 4, 8 LSB) rika, jak velka je DNL/INL; mezi +d a -d je rozdil asymetrie.")


if __name__ == "__main__":
    main(sys.argv[1])
