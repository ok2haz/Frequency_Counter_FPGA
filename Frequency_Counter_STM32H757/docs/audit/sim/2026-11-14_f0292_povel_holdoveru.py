"""F-0292: jaky povel drzet v holdoveru? Vyhodnoceni nad zaznamy kodu DAC v LOCK (bez zasahu do desky).
Cil v case t = prumer kodu v (t, t+H) -- to, co smycka v nasledujici dobe skutecne potrebovala (H = 1 h a 3 h).
Prediktory z minulosti (jen data <= t):
  A  okamzity kod (dnesni chovani)
  B  prumer poslednich 1000 s      C  prumer poslednich 3000 s      D  prumer poslednich 7200 s
  E  primka pres poslednich 3000 s extrapolovana na stred ciloveho useku
  F  primka pres poslednich 7200 s extrapolovana na stred ciloveho useku
Vystup: rms chyby [LSB] a x K = pocatecni odchylka kmitoctu holdoveru [1e-12].
"""
import csv, io, math, sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
K = 3.4e-11
OVERNIGHT = r'C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\noc_logs\20261113_lock\overnight_lock.csv'   # lokalni archiv nocniho zaznamu (noc_logs neni v gitu)


def load_noc_state():
    t, c = [], []
    with io.open(r'C:\GitHub\Frequency_Counter_FPGA\noc_state.csv', encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if r['gpsdo_state'] == 'LOCK' and r['gpsdo_code'] not in ('', None):
                t.append(float(r['uptime_s'])); c.append(float(r['gpsdo_code']))
    return t, c


def load_overnight(cut='07:00'):
    t, c = [], []
    with io.open(OVERNIGHT, encoding='utf-8') as f:
        rows = list(csv.DictReader(f, delimiter=';'))
    day = rows[-1]['iso'][:10]
    for r in rows:
        if r['iso'][:10] == day and r['iso'][11:16] >= cut:
            break
        if r['state'].startswith('LOCK') and r['code']:
            t.append(float(r['unix'])); c.append(float(r['code']))
    return t, c


def mean_in(t, c, lo, hi):
    v = [c[i] for i in range(len(t)) if lo < t[i] <= hi]
    return (sum(v) / len(v)) if v else None


def line_at(t, c, lo, hi, tx):
    idx = [i for i in range(len(t)) if lo < t[i] <= hi]
    if len(idx) < 5:
        return None
    tm = sum(t[i] for i in idx) / len(idx); cm = sum(c[i] for i in idx) / len(idx)
    sxx = sum((t[i] - tm) ** 2 for i in idx)
    sxy = sum((t[i] - tm) * (c[i] - cm) for i in idx)
    return cm + (sxy / sxx) * (tx - tm) if sxx > 0 else cm


def run(name, t, c):
    print('== %s: %d bodu, %.1f h, krok %.0f s, kod %.2f..%.2f' % (name, len(t), (t[-1] - t[0]) / 3600.0, t[1] - t[0], min(c), max(c)))
    for H in (3600.0, 10800.0):
        err = {k: [] for k in 'ABCDEF'}
        step = max(1, int(600.0 / (t[1] - t[0])))                   # vyhodnoceni po 10 min
        for i in range(0, len(t), step):
            if t[i] - t[0] < 7200.0 or t[-1] - t[i] < H:
                continue
            tgt = mean_in(t, c, t[i], t[i] + H)
            p = {'A': c[i], 'B': mean_in(t, c, t[i] - 1000.0, t[i]), 'C': mean_in(t, c, t[i] - 3000.0, t[i]),
                 'D': mean_in(t, c, t[i] - 7200.0, t[i]),
                 'E': line_at(t, c, t[i] - 3000.0, t[i], t[i] + H / 2), 'F': line_at(t, c, t[i] - 7200.0, t[i], t[i] + H / 2)}
            for k, v in p.items():
                if v is not None and tgt is not None:
                    err[k].append(v - tgt)
        print('   cil = prumer kodu v nasledujicich %.0f h (%d vyhodnoceni)' % (H / 3600.0, len(err['A'])))
        for k, nm in (('A', 'okamzity kod (dnes)'), ('B', 'prumer 1000 s'), ('C', 'prumer 3000 s'), ('D', 'prumer 7200 s'),
                      ('E', 'primka 3000 s -> stred'), ('F', 'primka 7200 s -> stred')):
            e = err[k]
            if not e:
                continue
            rms = math.sqrt(sum(v * v for v in e) / len(e))
            print('      %s %-24s rms %.3f LSB = %5.1f e-12 | max |chyba| %.2f LSB' % (k, nm, rms, rms * K * 1e12, max(abs(v) for v in e)))


t, c = load_noc_state()
run('noc 2026-10-24/25 (web, 10 s)', t, c)
t, c = load_overnight()
run('noc 2026-11-13/14 klidny usek (UART, 60 s)', t, c)
