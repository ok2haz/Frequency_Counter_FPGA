"""Tempco OCXO z ranniho teplotniho prechodu (overnight_lock.csv, 06:40-08:30).
Smycka kmitocet dorovnava se zpozdenim (tau 1000 s), proto se odchylka VOLNEHO OCXO sklada z obou casti:
    y_volny(t) = dTIE/dt - K * (kod(t) - kod_ref)        [dTIE/dt = zbytkova odchylka, kterou smycka jeste nedorovnala]
a ta se proklada teplotou plaste OCXO (TMP117 0x49), volitelne se zpozdenim teploty (tepelna setrvacnost krystalu).
TIE je v zaznamu jen u zapornych hodnot (vada regexu loggeru) -> pouzity jen radky, kde TIE je; dTIE/dt z primky pres 5 radku.
"""
import csv, io, math, sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
P = r'C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\noc_logs\20261113_lock\overnight_lock.csv'
K = 3.4e-11
rows = []
with io.open(P, encoding='utf-8') as f:
    for r in csv.DictReader(f, delimiter=';'):
        if r['iso'][:10] == '2026-11-14' and '06:40' <= r['iso'][11:16] <= '08:30':
            rows.append(dict(iso=r['iso'][11:16], t=float(r['unix']), code=float(r['code']), toc=float(r['t_ocxo_c']),
                             tb=float(r['t_board_c']), tie=(float(r['tie_ns']) if r['tie_ns'] != '' else None)))
print('radku %d, z toho s TIE %d' % (len(rows), sum(1 for r in rows if r['tie'] is not None)))
print('  cas    TIE   kod      T_ocxo  T_deska')
for r in rows[::6]:
    print('  %s  %5s  %8.2f  %5.2f  %5.2f' % (r['iso'], '--' if r['tie'] is None else '%.0f' % r['tie'], r['code'], r['toc'], r['tb']))

# dTIE/dt: primka pres okno +-2 radky (jen kdyz vsech 5 ma TIE)
pts = []
code_ref = sum(r['code'] for r in rows[:15]) / 15.0
for i in range(2, len(rows) - 2):
    w = rows[i - 2:i + 3]
    if any(x['tie'] is None for x in w):
        continue
    tm = sum(x['t'] for x in w) / 5; xm = sum(x['tie'] for x in w) / 5
    sl = sum((x['t'] - tm) * (x['tie'] - xm) for x in w) / sum((x['t'] - tm) ** 2 for x in w)   # ns/s = 1e-9
    yres = sl * 1e-9
    yfree = yres - K * (rows[i]['code'] - code_ref)
    pts.append((i, rows[i]['t'], yres, yfree))
print('bodu s dTIE/dt: %d; nejvetsi |zbytkova odchylka| %.3f ppb; rozsah y_volny %.3f .. %.3f ppb'
      % (len(pts), max(abs(p[2]) for p in pts) * 1e9, min(p[3] for p in pts) * 1e9, max(p[3] for p in pts) * 1e9))


def fit(lag_rows):
    xs, ys = [], []
    for i, t, yres, yfree in pts:
        j = i - lag_rows
        if j < 0:
            continue
        xs.append(rows[j]['toc']); ys.append(yfree)
    n = len(xs); xm = sum(xs) / n; ym = sum(ys) / n
    sxx = sum((x - xm) ** 2 for x in xs); sxy = sum((xs[k] - xm) * (ys[k] - ym) for k in range(n))
    b = sxy / sxx
    res = [ys[k] - ym - b * (xs[k] - xm) for k in range(n)]
    rms = math.sqrt(sum(v * v for v in res) / (n - 2))
    r = sxy / math.sqrt(sxx * sum((y - ym) ** 2 for y in ys))
    return b, rms, r, n, max(xs) - min(xs)


print('zpozdeni teploty [min] | tempco [1e-10/K] | zbytky rms [1e-12] | r | bodu | rozsah T [K]')
best = None
for lag in range(0, 13):
    b, rms, r, n, dT = fit(lag)
    print('   %2d                  %+6.2f              %5.1f          %+.3f  %d   %.2f' % (lag, b * 1e10, rms * 1e12, r, n, dT))
    if best is None or rms < best[1]:
        best = (lag, rms, b)
print('nejmensi zbytky: zpozdeni %d min, tempco %+.2e /K (= %+.2f LSB/K kodu)' % (best[0], best[2], -best[2] / K))
