"""Model holdoveru OCXO (Bliley NVG47A1282, spec 6891) - cas do dosazeni cile presnosti.

Spusteni: python -I docs/audit/data/2026-10-16_holdover_model.py
Vystup je v docs/audit/2026-10-16_holdover-analyza.md (kap. 3). ASCII only.

Model (cisty vypocet z datasheetu, ZADNA zmerena data):
  relativni odchylka kmitoctu   eps(t) = eps0 + eT + D * t
  casova chyba (od posledni faze) x(t)  = x0 + (eps0 + eT) * t + D * t^2 / 2
    eps0 = zbytkova chyba kmitoctu v okamziku ztraty GPS (kvalita doladeni)
    eT   = pricitana teplotni slozka (pasmo teploty * koeficient)
    D    = stari [1/den] -> [1/s]
    x0   = chyba faze v okamziku ztraty (1PPS vs OCXO, ~30 ns pro NEO-7M)
"""
import math

DAY = 86400.0
T_COEF_PPB_PER_C = 0.18      # horni mez z datasheetu: +-5 ppb v -5..+50 C = 10 ppb / 55 C
X0 = 30e-9                   # s, fazova chyba pri ztrate (NEO-7M 1PPS ~30 ns RMS)

FREQ_TARGETS = [1e-8, 5e-9, 1e-9, 5e-10, 1e-10, 5e-11]
TIME_TARGETS = [(1e-6, "1 us"), (10e-6, "10 us"), (100e-6, "100 us"), (1e-3, "1 ms")]

# (nazev, eps0, aging ppb/den, dT [C])
SCEN = [
    ("A optimisticka  (eps0 3e-11, stari 0,3 ppb/den, dT 0,5 C)", 3e-11, 0.3, 0.5),
    ("B specifikace   (eps0 1e-10, stari 1,0 ppb/den, dT 1 C)  ", 1e-10, 1.0, 1.0),
    ("C pesimisticka  (eps0 1e-9,  stari 3,0 ppb/den, dT 2 C)  ", 1e-9, 3.0, 2.0),
]


def fmt_t(s):
    if s <= 0:
        return "0 (nelze)"
    if s < 120:
        return "%.0f s" % s
    if s < 7200:
        return "%.0f min" % (s / 60)
    if s < 2 * DAY:
        return "%.1f h" % (s / 3600)
    return "%.1f dne" % (s / DAY)


def t_freq(F, eps0, aging_ppb_day, dT):
    e0 = eps0 + dT * T_COEF_PPB_PER_C * 1e-9
    D = aging_ppb_day * 1e-9 / DAY            # 1/s
    if F <= e0:
        return 0.0
    return (F - e0) / D


def t_time(X, eps0, aging_ppb_day, dT):
    e0 = eps0 + dT * T_COEF_PPB_PER_C * 1e-9
    D = aging_ppb_day * 1e-9 / DAY
    a, b, c = D / 2.0, e0, X0 - X
    disc = b * b - 4 * a * c
    return (-b + math.sqrt(disc)) / (2 * a)


print("== Presnost KMITOCTU (|eps| <= cil): cas od ztraty GPS ==")
print("%-62s" % "scenar" + "".join("%11s" % ("%.0e" % F) for F in FREQ_TARGETS))
for name, e0, ag, dT in SCEN:
    print("%-62s" % name + "".join("%11s" % fmt_t(t_freq(F, e0, ag, dT)) for F in FREQ_TARGETS))

print("\n== Presnost CASU (|x| <= cil, x0 = 30 ns): cas od ztraty GPS ==")
print("%-62s" % "scenar" + "".join("%11s" % n for _, n in TIME_TARGETS))
for name, e0, ag, dT in SCEN:
    print("%-62s" % name + "".join("%11s" % fmt_t(t_time(X, e0, ag, dT)) for X, _ in TIME_TARGETS))

print("\n== Dnesni stav BEZ doladeni (Vc = 0 V, eps0 = 8,4e-7 z mereni 2026-10-16) ==")
print("  eps = 8.4e-7 -> 1 us cas dosazen za %s, kmitocet 1e-6 prekrocen nikdy (stari 1 ppb/den je zanedbatelne)" % fmt_t(1e-6 / 8.4e-7))

print("\n== Slozky (B): za 1 den ==")
D = 1.0e-9 / DAY
print("  stari       eps +1,0e-9 (1 ppb)")
print("  teplota     +%.1e (dT 1 C x 0,18 ppb/C)" % (T_COEF_PPB_PER_C * 1e-9))
print("  fazova chyba za 1 den: stari %.2f us, eps0 %.2f us" % (D * DAY ** 2 / 2 * 1e6, 1e-10 * DAY * 1e6))
