"""F-0292, model nejistoty holdoveru: kod potrebny k drzeni kmitoctu se chova jako nahodna prochazka (eval292.py: okamzity kod
je nejlepsi prediktor). Overeni, ze koeficient prochazky zmereny z POSLEDNICH 2 h historie (tak, jak to umi firmware: 720 bodu
po 10 s) predpovida skutecnou zmenu kodu za H = 0,5 / 1 / 2 / 3 h:
   q2 = prumer (kod(t+D) - kod(t))^2 / D  pres historii, D = 1800 s (prekryvajici se rozdily)
   predpoved sigma(H) = sqrt(q2 * H);  z = skutecna zmena / predpoved;  kalibrace: rms z ~ 1
Dnesni model: sqrt(u0^2 + (drift*t)^2) s driftem ze sklonu kodu za 2 h -> pro srovnani totez z = zmena / (|sklon| * H).
"""
import csv, io, math, sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
K = 3.4e-11
OVERNIGHT = r'C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\noc_logs\20261113_lock\overnight_lock.csv'   # lokalni archiv nocniho zaznamu (noc_logs neni v gitu)
DL = int(sys.argv[1]) if len(sys.argv) > 1 else 1800


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


def run(name, t, c):
    dt = t[1] - t[0]
    nh = int(7200.0 / dt)                 # 2 h historie
    nd = int(DL / dt)
    print('== %s (krok %.0f s, D = %d s)' % (name, dt, DL))
    for H in (1800.0, 3600.0, 7200.0, 10800.0):
        nH = int(H / dt)
        zs, zd, act, prd = [], [], [], []
        for i in range(nh, len(c) - nH, max(1, int(600.0 / dt))):
            h = c[i - nh:i + 1]
            d = [h[j + nd] - h[j] for j in range(len(h) - nd)]
            q2 = sum(v * v for v in d) / len(d) / DL
            pred = math.sqrt(q2 * H)
            n = len(h); tc = 0.5 * (n - 1); hm = sum(h) / n
            slope = sum((j - tc) * (h[j] - hm) for j in range(n)) / sum((j - tc) ** 2 for j in range(n)) / dt   # LSB/s
            a = c[i + nH] - c[i]
            act.append(a); prd.append(pred)
            zs.append(a / pred)
            zd.append(a / max(1e-9, abs(slope) * H))
        rms = lambda v: math.sqrt(sum(x * x for x in v) / len(v))
        print('   H = %4.1f h: skutecna zmena rms %.2f LSB = %5.1f e-12 | predpoved prochazky rms %.2f LSB -> rms z = %.2f, podil |z| > 2: %.0f %% | dnesni model (jen drift): rms z = %.1f'
              % (H / 3600.0, rms(act), rms(act) * K * 1e12, rms(prd), rms(zs), 100.0 * sum(1 for z in zs if abs(z) > 2) / len(zs), rms(zd)))
    d = [c[j + nd] - c[j] for j in range(len(c) - nd)]
    q = math.sqrt(sum(v * v for v in d) / len(d) / DL)
    print('   koeficient prochazky z celeho zaznamu: %.3f LSB/sqrt(h) = %.1f e-12 / sqrt(h)' % (q * 60.0, q * 60.0 * K * 1e12))


t, c = load_noc_state()
run('noc 2026-10-24/25', t, c)
t, c = load_overnight()
run('noc 2026-11-13/14 klidny usek', t, c)
