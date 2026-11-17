# Je teplotni koeficient kodu DAC stabilni mezi nocemi a useky? (#311)
# Pouziti: python -I noc_tempco_robust.py <csv_1116> <csv_1113>
import sys
from datetime import datetime
import numpy as np
sys.stdout.reconfigure(encoding='utf-8', errors='replace')


def load(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        hdr = f.readline().strip().split(';')
        for ln in f:
            v = dict(zip(hdr, ln.rstrip('\n').split(';')))
            try:
                rows.append((float(v['unix']), float(v['code']), float(v['t_board_c']), float(v['t_ocxo_c'])))
            except (KeyError, ValueError):
                pass
    a = np.array(rows)
    return a[:, 0], a[:, 1], a[:, 2], a[:, 3]


def fit1(x, y):
    p, cov = np.polyfit(x, y, 1, cov=True)
    r = np.corrcoef(x, y)[0, 1]
    return p[0], np.sqrt(cov[0, 0]), r


def fit2(th, tb, y):
    A = np.column_stack([th, tb, np.ones_like(th)])
    c, *_ = np.linalg.lstsq(A, y, rcond=None)
    res = y - A @ c
    s2 = res @ res / (len(y) - 3)
    cov = s2 * np.linalg.inv(A.T @ A)
    return c[0], np.sqrt(cov[0, 0]), c[1], np.sqrt(cov[1, 1]), res.std()


def hhmm(u):
    return datetime.fromtimestamp(u).strftime('%m-%d %H:%M')


def useky(name, t, code, tb, to, cuts):
    print('=' * 100)
    print(name)
    for (a, b, lbl) in cuts:
        m = (t >= a) & (t < b)
        if m.sum() < 20:
            continue
        th = (t[m] - t[m][0]) / 3600.0
        s_t, e_t, r_t = fit1(th, code[m])
        s_b, e_b, r_b = fit1(tb[m], code[m])
        s_o, e_o, r_o = fit1(to[m], code[m])
        c_t, ce_t, c_b, ce_b, rs = fit2(th, tb[m], code[m])
        print('  %-34s %s..%s n %3d | dT_desky p-p %.2f C | kod p-p %.2f LSB' % (
            lbl, hhmm(t[m][0]), hhmm(t[m][-1]), m.sum(), np.ptp(tb[m]), np.ptp(code[m])))
        print('      kod~cas %+.3f+-%.3f LSB/h (r %+.2f) | kod~T_desky %+.2f+-%.2f LSB/C (r %+.2f) | '
              'kod~T_ocxo %+.2f+-%.2f LSB/C (r %+.2f)' % (s_t, e_t, r_t, s_b, e_b, r_b, s_o, e_o, r_o))
        print('      kod~cas+T_desky: cas %+.3f+-%.3f LSB/h, T_desky %+.2f+-%.2f LSB/C, rezidua %.2f LSB' % (
            c_t, ce_t, c_b, ce_b, rs))


def lag_scan(name, t, code, tb, tmax=None, cidlo='T_desky'):
    print('  zpozdeni kod(t) ~ %s(t - L), %s (L < 0 = kod predbiha teplotu):' % (cidlo, name))
    m0 = np.ones_like(t, dtype=bool) if tmax is None else (t < tmax)
    for lag_min in (-60, -30, -20, -10, 0, 10, 20, 30, 45, 60, 90, 120):
        tbl = np.interp(t - lag_min * 60.0, t, tb, left=np.nan, right=np.nan)
        m = m0 & ~np.isnan(tbl)
        s, e, r = fit1(tbl[m], code[m])
        res = code[m] - np.polyval(np.polyfit(tbl[m], code[m], 1), tbl[m])
        print('      L %3d min: %+.2f LSB/C  r %+.3f  rezidua %.2f LSB' % (lag_min, s, r, res.std()))


t1, c1, b1, o1 = load(sys.argv[1])
t2, c2, b2, o2 = load(sys.argv[2])


def ts(s):
    return datetime.strptime(s, '%Y-%m-%d %H:%M').timestamp()


useky('NOC 16/17 (K 3,85e-11)', t1, c1, b1, o1, [
    (t1[0], t1[-1] + 1, 'cela noc'),
    (t1[0], ts('2026-11-17 05:00'), 'chladnuti 22:23-05:00'),
    (ts('2026-11-17 05:00'), t1[-1] + 1, 'ustalena teplota 05:00-08:00'),
])
useky('NOC 13/14 (K 3,4e-11)', t2, c2, b2, o2, [
    (t2[0], t2[-1] + 1, 'cela noc'),
    (t2[0], ts('2026-11-14 07:00'), 'bez udalosti v 07:00'),
    (t2[0], ts('2026-11-14 03:00'), 'chladnuti 21:11-03:00'),
    (ts('2026-11-14 03:00'), ts('2026-11-14 07:00'), 'ustalena teplota 03:00-07:00'),
    (ts('2026-11-14 07:00'), t2[-1] + 1, 'udalost 07:00-08:30'),
])
print('=' * 100)
lag_scan('noc 16/17', t1, c1, b1)
lag_scan('noc 13/14 do 07:00', t2, c2, b2, ts('2026-11-14 07:00'))
lag_scan('noc 16/17', t1, c1, o1, cidlo='T_ocxo')
lag_scan('noc 13/14 do 07:00', t2, c2, o2, ts('2026-11-14 07:00'), cidlo='T_ocxo')
lag_scan('noc 13/14 cela (i udalost)', t2, c2, o2, cidlo='T_ocxo')
# udalost 13/14 07:00-08:30: proklad kodu teplotou pouzdra OCXO, deska jde opacne
m = (t2 >= ts('2026-11-14 07:00')) & (t2 < ts('2026-11-14 08:30'))
s, e, r = fit1(o2[m], c2[m])
print('  udalost 13/14 07:00-08:30: kod~T_ocxo %+.2f+-%.2f LSB/C r %+.2f | dT_ocxo p-p %.2f C, dT_desky p-p %.2f C' % (
    s, e, r, np.ptp(o2[m]), np.ptp(b2[m])))
# udalost 13/14: co delaly teploty
m = (t2 >= ts('2026-11-14 06:40')) & (t2 < ts('2026-11-14 08:30'))
print('=' * 100)
print('udalost 13/14 po 10 min: cas | kod | T_desky | T_ocxo')
for i in np.where(m)[0][::10]:
    print('   %s | %.2f | %.2f | %.2f' % (hhmm(t2[i]), c2[i], b2[i], o2[i]))
