# F-0291: model dosedani smycky nad REALNYM sumem faze (noc_state.csv, LOCK, 10 s).
# Smycka je linearni -> faze = zmereny ustaleny zaznam + deterministicka odezva na pocatecni odchylku y0:
#   x(t) = y0/wd * exp(-z*w*t) * sin(wd*t),  w = 1/tau, wd = w*sqrt(1-z^2);  y_err(t) = dx/dt
# Porovnava udaj STARY (ADEV / nahrada 0,01) a NOVY (ADEV + prebytek sklonu nad K*ADEV; bez ADEV vstupni hodnota)
# se skutecnou |y_err|. Kopie logiky gp_ref_core (okno ADEV m = 100/50/20/10, historie max 720 bodu).
import csv, io, math, sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
NOC = r'C:\GitHub\Frequency_Counter_FPGA\noc_state.csv'
TAU, Z, T0, HIST = 1000.0, 0.7, 10.0, 720
K = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
Y0 = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5       # ppb
ENTRY = math.hypot(Y0, 1.4142 * 2.9 / 128.0)

xr = []
with io.open(NOC, encoding='utf-8') as f:
    for r in csv.DictReader(f):
        if r['gpsdo_state'] == 'LOCK' and r['gpsdo_xr_ns'] != '':
            xr.append(float(r['gpsdo_xr_ns']))
w = 1.0 / TAU
wd = w * math.sqrt(1.0 - Z * Z)


def xdet(t):
    return Y0 / wd * math.exp(-Z * w * t) * math.sin(wd * t)


def ydet(t):
    return Y0 * math.exp(-Z * w * t) * (math.cos(wd * t) - Z * w / wd * math.sin(wd * t))


def core(x, tau):
    n = len(x)
    u = 0.0
    for m in (100, 50, 20, 10):
        if n - 2 * m < 10:
            continue
        s = 0.0
        c = 0
        for i in range(n - 2 * m):
            d = x[i + 2 * m] - 2 * x[i + m] + x[i]
            s += d * d
            c += 1
        u = math.sqrt(0.5 * s / c) / (m * T0)
        break
    yoff = -1.0
    if n >= 30:
        wn = int(tau / T0 + 0.5)
        wn = max(30, min(wn, n))
        seg = x[n - wn:]
        tc = 0.5 * (wn - 1)
        xm = sum(seg) / wn
        sxy = sum((i - tc) * (seg[i] - xm) for i in range(wn))
        sxx = sum((i - tc) ** 2 for i in range(wn))
        yoff = abs(sxy / sxx / T0)
    return u, yoff


def new_val(u, yoff):
    if u <= 0.0:
        return ENTRY
    v = u
    if yoff >= 0.0 and yoff > K * u:
        v = math.sqrt(u * u + yoff * yoff - K * K * u * u)
    return v


def old_val(u):
    return u if u > 0.0 else 0.01


print('K = %.1f, y0 = %.2f ppb, tau = %.0f s; skutecna |y| = deterministicka odezva (sum OCXO v ni neni)' % (K, Y0, TAU))
print('  t[min]  skut|y|   STARY   pomer |  NOVY    pomer   (u_adev  sklon)')
x = [xr[i] + xdet(i * T0) for i in range(len(xr))]
worst_old = worst_new = 0.0
for i in range(6, min(len(x), 1300)):
    t = i * T0
    seg = x[max(0, i + 1 - HIST):i + 1]
    if i % 6 and i not in (6, 12, 18, 24, 29, 30, 49, 50, 109, 110, 209, 210):
        continue
    u, yo = core(seg, TAU)
    yt = abs(ydet(t))
    o, nw = old_val(u), new_val(u, yo)
    if t >= 60:
        worst_old = max(worst_old, yt / o)
        worst_new = max(worst_new, yt / nw)
    if i in (6, 12, 18, 24, 29, 30, 49, 50, 60, 90, 109, 110, 150, 209, 210, 240, 300, 360, 480, 600, 720, 900, 1080, 1260):
        print('  %6.1f  %7.4f  %7.4f  %5.1fx | %7.4f  %5.2fx   (%.4f  %s)' % (t / 60.0, yt, o, yt / o, nw, yt / nw, u, ('%.4f' % yo) if yo >= 0 else '--'))
print('nejhorsi podhodnoceni (skutecna / udavana) od 1 min: STARY %.1fx, NOVY %.2fx' % (worst_old, worst_new))

# ustaleny stav: jak casto a o kolik NOVY zvedne udaj proti STAREMU (jen realny sum, bez dosedani)
hi = []
for i in range(HIST, len(xr), 6):
    u, yo = core(xr[i + 1 - HIST:i + 1], TAU)
    hi.append(new_val(u, yo) / u)
hi.sort()
print('ustaleny stav (%d vyhodnoceni): NOVY/STARY median %.3f  p95 %.3f  p99 %.3f  max %.3f; zvednuto v %.1f %% pripadu'
      % (len(hi), hi[len(hi) // 2], hi[int(0.95 * len(hi))], hi[int(0.99 * len(hi))], hi[-1], 100.0 * sum(1 for v in hi if v > 1.0001) / len(hi)))
