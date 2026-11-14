"""Rozbor nocniho zaznamu overnight_lock.csv (UART `gpsdo` + `sensors` po 60 s, kod DAC na 0,01 LSB).
 - trajektorie kodu: sklon -> starnuti D (K = 3,4e-11 / LSB, f0 = 1e7 Hz)
 - regrese kod ~ cas + T_ocxo (+ T_deska): tempco, zbytky
 - rozdeleni na klidny usek a ranni teplotni prechod (argument = cas rezu HH:MM, vychozi 07:00)
Pozor: sloupce y_ppb / phase_ns / tie_ns jsou u KLADNYCH hodnot prazdne (regex loggeru necekal '+') -> nepouzivat ke statistice.
"""
import csv, io, math, sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
P = r'C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\noc_logs\20261113_lock\overnight_lock.csv'
CUT = sys.argv[1] if len(sys.argv) > 1 else '07:00'
K = 3.4e-11

rows = []
bad = 0
with io.open(P, encoding='utf-8') as f:
    for r in csv.DictReader(f, delimiter=';'):
        try:
            if not r['state'].startswith('LOCK'):
                bad += 1; continue
            rows.append(dict(iso=r['iso'], t=float(r['unix']), code=float(r['code']), toc=float(r['t_ocxo_c']),
                             tb=float(r['t_board_c']), tm=float(r['t_mod_c']), vc=float(r['vc_mv']),
                             rej=int(r['pps_rej']), gap=int(r['pps_gap']), torn=int(r['pps_torn'])))
        except (ValueError, TypeError, KeyError):
            bad += 1


def solve(A, b):
    n = len(b)
    M = [A[i][:] + [b[i]] for i in range(n)]
    for c in range(n):
        p = max(range(c, n), key=lambda i: abs(M[i][c]))
        M[c], M[p] = M[p], M[c]
        for i in range(c + 1, n):
            f = M[i][c] / M[c][c]
            for j in range(c, n + 1):
                M[i][j] -= f * M[c][j]
    x = [0.0] * n
    for i in range(n - 1, -1, -1):
        x[i] = (M[i][n] - sum(M[i][j] * x[j] for j in range(i + 1, n))) / M[i][i]
    return x


def fit(y, cols):
    """LSQ y = b0 + sum b_k * (col_k - mean); vraci (koeficienty bez b0, rms zbytku, SE koeficientu, zbytky)."""
    n = len(y)
    X = [[1.0] + [c[i] for c in cols] for i in range(n)]
    k = len(X[0])
    A = [[sum(X[i][a] * X[i][b] for i in range(n)) for b in range(k)] for a in range(k)]
    bv = [sum(X[i][a] * y[i] for i in range(n)) for a in range(k)]
    beta = solve(A, bv)
    res = [y[i] - sum(beta[a] * X[i][a] for a in range(k)) for i in range(n)]
    s2 = sum(v * v for v in res) / max(1, n - k)
    # kovariance = s2 * inv(A): sloupce inverze resenim
    se = []
    for a in range(k):
        e = [1.0 if j == a else 0.0 for j in range(k)]
        se.append(math.sqrt(max(0.0, s2 * solve(A, e)[a])))
    return beta, math.sqrt(s2), se, res


def lag1(res):
    m = sum(res) / len(res)
    num = sum((res[i] - m) * (res[i - 1] - m) for i in range(1, len(res)))
    den = sum((v - m) ** 2 for v in res)
    return num / den if den > 0 else 0.0


def block(name, rr):
    if len(rr) < 20:
        print('%s: malo radku (%d)' % (name, len(rr))); return
    t0 = rr[0]['t']
    th = [(r['t'] - t0) / 3600.0 for r in rr]
    code = [r['code'] for r in rr]
    toc = [r['toc'] for r in rr]; tb = [r['tb'] for r in rr]
    tcm = sum(toc) / len(toc); tbm = sum(tb) / len(tb); thm = sum(th) / len(th)
    print('== %s: %s .. %s, %d radku, %.2f h' % (name, rr[0]['iso'], rr[-1]['iso'], len(rr), th[-1]))
    print('   kod %.2f -> %.2f (zmena %+.2f LSB, rozsah %.2f..%.2f) | T_ocxo %.2f -> %.2f (rozsah %.2f..%.2f) | T_deska %.2f -> %.2f | Vc %.2f -> %.2f mV'
          % (code[0], code[-1], code[-1] - code[0], min(code), max(code), toc[0], toc[-1], min(toc), max(toc), tb[0], tb[-1], rr[0]['vc'], rr[-1]['vc']))
    for label, cols, names in (
            ('kod ~ cas', [[v - thm for v in th]], ['cas [LSB/h]']),
            ('kod ~ cas + T_ocxo', [[v - thm for v in th], [v - tcm for v in toc]], ['cas [LSB/h]', 'T_ocxo [LSB/K]']),
            ('kod ~ cas + T_ocxo + T_deska', [[v - thm for v in th], [v - tcm for v in toc], [v - tbm for v in tb]],
             ['cas [LSB/h]', 'T_ocxo [LSB/K]', 'T_deska [LSB/K]'])):
        beta, rms, se, res = fit(code, cols)
        rho = lag1(res)
        infl = math.sqrt((1 + rho) / (1 - rho)) if abs(rho) < 0.999 else float('inf')   # korelovane zbytky: SE x tento faktor
        s = '   %-30s zbytky rms %.3f LSB, lag-1 %.2f (SE x %.1f)' % (label, rms, rho, infl)
        print(s)
        for i, nm in enumerate(names):
            b, e = beta[i + 1], se[i + 1] * infl
            extra = ''
            if nm.startswith('cas'):
                d = b * 24.0 * K
                extra = ' = %+.2f LSB/den = %+.2e /den = %+.2f mHz/den (+- %.2f)' % (b * 24, d, d * 1e7 * 1e3, e * 24 * K * 1e10)
            else:
                extra = ' = %+.2e /K' % (-b * K)      # kod kompenzuje: kmitocet volneho OCXO se meni opacne
            print('      %-18s %+.4f +- %.4f%s' % (nm, b, e, extra))
    # korelace T_ocxo s casem (kolinearita)
    n = len(th)
    sxy = sum((th[i] - thm) * (toc[i] - tcm) for i in range(n))
    sxx = sum((v - thm) ** 2 for v in th); syy = sum((v - tcm) ** 2 for v in toc)
    print('   korelace T_ocxo s casem r = %+.2f (blizko +-1 = cas a teplotu nejde oddelit)' % (sxy / math.sqrt(sxx * syy) if sxx > 0 and syy > 0 else 0.0))


print('radku LOCK %d, jinych/vadnych %d | 1PPS odmitnuto %d -> %d, diry %d -> %d, roztrzena %d -> %d'
      % (len(rows), bad, rows[0]['rej'], rows[-1]['rej'], rows[0]['gap'], rows[-1]['gap'], rows[0]['torn'], rows[-1]['torn']))
dts = [rows[i]['t'] - rows[i - 1]['t'] for i in range(1, len(rows))]
print('rozestup radku: median %.0f s, max %.0f s, mezer > 90 s: %d' % (sorted(dts)[len(dts) // 2], max(dts), sum(1 for d in dts if d > 90)))
cut = [i for i, r in enumerate(rows) if r['iso'][11:16] >= CUT and r['iso'][:10] == rows[-1]['iso'][:10]]
ci = cut[0] if cut else len(rows)
block('CELY ZAZNAM', rows)
block('KLIDNY USEK (do %s)' % CUT, rows[:ci])
block('RANNI PRECHOD (od %s)' % CUT, rows[ci:])
