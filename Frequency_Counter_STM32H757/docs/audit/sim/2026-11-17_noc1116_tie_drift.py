# Noc LOCK 2026-11-16/17 (v0.45.21, K 3,85e-11): je pomale putovani TIE ustalena chyba PLL 2. radu pri driftu kmitoctu?
# PLL x' = y_zbytek - Kp*x - I, I' = Ki*x, Ki = 1/tau^2 -> pri rampe kmitoctu y_zbytek = d*t je x_ss = d*tau^2.
# Rychlost driftu d se odhadne z derivace kodu DAC (smycka kodem kompenzuje drift OCXO): d = -dkod/dt * K,
# takze ocekavane TIE ~ -(dkod/dt * K * tau^2). Merne: korelace vyhlazeneho TIE s mistni derivaci kodu (sklon teorie -1).
# Pouziti: python -I 2026-11-17_noc1116_tie_drift.py noc_logs/20261116_lock/overnight_lock.csv
import sys
import numpy as np
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
K = 3.85e-11          # strmost DAC [1/LSB] (F-0293)
TAU = 1000.0          # casova konstanta smycky [s]
rows = []
with open(sys.argv[1], encoding='utf-8') as f:
    hdr = f.readline().strip().split(';')
    for ln in f:
        v = dict(zip(hdr, ln.rstrip('\n').split(';')))
        if v.get('code') and v.get('tie_ns'):
            rows.append(v)
t = np.array([float(r['unix']) for r in rows])
code = np.array([float(r['code']) for r in rows])
tie = np.array([float(r['tie_ns']) for r in rows])
tb = np.array([float(r['t_board_c']) for r in rows])
n = len(t)
for half in (15, 30, 45):          # pul okna derivace ve vzorcich (60 s)
    dcd = np.full(n, np.nan)       # dkod/dt [LSB/s] z primky pres +-half vzorku
    for i in range(half, n - half):
        a = slice(i - half, i + half + 1)
        dcd[i] = np.polyfit(t[a] - t[i], code[a], 1)[0]
    pred = dcd * K * TAU * TAU * 1e9          # dkod/dt * K * tau^2 [ns]; teorie: TIE = -pred
    m = ~np.isnan(dcd)
    tsm = np.array([tie[max(0, i - half):i + half + 1].mean() for i in range(n)])   # TIE vyhlazene stejnym oknem
    r = np.corrcoef(pred[m], tsm[m])[0, 1]
    sl, b = np.polyfit(pred[m], tsm[m], 1)
    res = tsm[m] - (sl * pred[m] + b)
    print('okno derivace +-%2d min: TIE_vyhlazene ~ dkod/dt*K*tau^2  r %+.3f | sklon %+.2f (teorie -1) | posun %+.1f ns'
          ' | sd TIE_vyhl %.1f ns -> zbytek %.1f ns' % (half, r, sl, b, tsm[m].std(), res.std()))
    print('    predpoved z mistniho driftu (prumer): %+.1f ns, prumer TIE %+.1f ns' % (np.nanmean(pred), tie.mean()))
p = np.polyfit(tb, code, 1)
print('kod ~ T_desky: %+.2f LSB/C; rezidua %.2f LSB (kod sd %.2f)' % (p[0], (code - np.polyval(p, tb)).std(), code.std()))
