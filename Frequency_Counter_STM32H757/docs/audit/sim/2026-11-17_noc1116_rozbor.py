# Rozbor nocniho behu LOCK 2026-11-16/17 (v0.45.21, K 3,85e-11) + srovnani s noci 2026-11-13 (K 3,4e-11).
# Pouziti: python -I noc1116_rozbor.py <csv_1116> <status_log_1116> <csv_1113>
import sys, re, math, statistics as st
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
import numpy as np

KPHYS = 38.5  # ppt na LSB (zmereno F-0293, 3,85e-11) - fyzikalni strmost, plati pro obe noci


def load(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        hdr = f.readline().strip().split(';')
        for ln in f:
            v = ln.rstrip('\n').split(';')
            if len(v) < len(hdr):
                v += [''] * (len(hdr) - len(v))
            rows.append(dict(zip(hdr, v)))
    return hdr, rows


def fl(x):
    try:
        return float(x)
    except Exception:
        return None


def col(rows, k):
    return [fl(r.get(k, '')) for r in rows]


def stats(name, xs, unit='', scale=1.0):
    v = [x * scale for x in xs if x is not None]
    if not v:
        print('  %-22s --' % name)
        return
    a = np.array(v)
    print('  %-22s n %4d | prum %+9.3f | sd %8.3f | min %+9.3f | max %+9.3f | p-p %8.3f %s'
          % (name, len(a), a.mean(), a.std(ddof=1) if len(a) > 1 else 0, a.min(), a.max(), a.max() - a.min(), unit))


def linfit(x, y):
    x = np.asarray(x, float); y = np.asarray(y, float)
    A = np.vstack([x, np.ones_like(x)]).T
    c, res, *_ = np.linalg.lstsq(A, y, rcond=None)
    r = y - A @ c
    s2 = (r @ r) / (len(y) - 2)
    cov = s2 * np.linalg.inv(A.T @ A)
    rr = np.corrcoef(x, y)[0, 1]
    return c[0], math.sqrt(cov[0, 0]), c[1], math.sqrt(s2), rr


def multifit(cols, y, names):
    X = np.vstack([np.asarray(c, float) for c in cols] + [np.ones(len(y))]).T
    y = np.asarray(y, float)
    c, *_ = np.linalg.lstsq(X, y, rcond=None)
    r = y - X @ c
    dof = len(y) - X.shape[1]
    s2 = (r @ r) / dof
    cov = s2 * np.linalg.inv(X.T @ X)
    out = []
    for i, n in enumerate(names):
        out.append('%s %+.4f +- %.4f' % (n, c[i], math.sqrt(cov[i, i])))
    return out, math.sqrt(s2)


def oadev_phase(x, tau0, ms):
    x = np.asarray(x, float)
    out = []
    for m in ms:
        n = len(x) - 2 * m
        if n < 3:
            continue
        d = x[2 * m:] - 2 * x[m:-m] + x[:-2 * m]
        out.append((m * tau0, math.sqrt((d @ d) / (2.0 * n * (m * tau0) ** 2)), n))
    return out


def rozbor_noci(label, path):
    hdr, rows = load(path)
    print('=' * 100)
    print('%s  (%s)' % (label, path))
    ok = [r for r in rows if r.get('code')]
    bad = [r for r in rows if not r.get('code') or r.get('err')]
    t = np.array([fl(r['unix']) for r in ok])
    print('  radku %d, platnych %d, s chybou %d | %s .. %s | %.2f h'
          % (len(rows), len(ok), len(bad), ok[0]['iso'], ok[-1]['iso'], (t[-1] - t[0]) / 3600))
    for r in bad[:5]:
        print('    chyba:', r['iso'], r.get('err'))
    dt = np.diff(t)
    print('  rozestup vzorku: median %.1f s, max %.1f s, mezer > 90 s: %d' % (np.median(dt), dt.max(), int((dt > 90).sum())))
    states = {}
    for r in ok:
        states[r['state']] = states.get(r['state'], 0) + 1
    print('  stavy:', states)
    us = np.array([fl(r['uptime_s']) for r in ok])
    jumps = np.where(np.abs(np.diff(us) - dt) > 5)[0]
    print('  cas ve stavu: %d .. %d s, skoku proti hodinam PC: %d' % (us[0], us[-1], len(jumps)))
    for j in jumps[:5]:
        print('    skok', ok[j]['iso'], us[j], '->', ok[j + 1]['iso'], us[j + 1])
    ys = col(ok, 'y_ppb'); ph = col(ok, 'phase_ns'); tie = col(ok, 'tie_ns'); code = col(ok, 'code')
    stats('y [ppt]', ys, 'ppt', 1000.0)
    stats('faze smycky [ns]', ph, 'ns')
    stats('TIE [ns]', tie, 'ns')
    stats('kod [LSB]', code, 'LSB')
    ny = sum(1 for y in ys if y is not None and abs(y) > 0.1)
    print('  |y| > 0,1 ppb: %d z %d' % (ny, sum(1 for y in ys if y is not None)))
    th = (t - t[0]) / 3600.0
    sl, ssl, b, sres, rr = linfit(th, code)
    print('  kod ~ cas: %+.3f +- %.3f LSB/h = %+.1f +- %.1f ppt/h (%.2f ppb/den) | r %.3f | rezidua sd %.3f LSB'
          % (sl, ssl, sl * KPHYS, ssl * KPHYS, sl * KPHYS * 24 / 1000, rr, sres))
    print('  kod: zacatek %.2f, konec %.2f, zmena %+.2f LSB = %+.0f ppt' % (code[0], code[-1], code[-1] - code[0], (code[-1] - code[0]) * KPHYS))
    temps = {}
    for k in ('t_ocxo_c', 't_board_c', 't_mod_c', 'vc_mv'):
        if k in hdr:
            temps[k] = col(ok, k)
            stats(k, temps[k])
    for k in ('t_ocxo_c', 't_board_c', 't_mod_c'):
        if k in temps and all(v is not None for v in temps[k]):
            s1, ss1, _, sr1, r1 = linfit(temps[k], code)
            print('  kod ~ %-9s: %+.3f +- %.3f LSB/C = %+.1f ppt/C | r %.3f | rezidua sd %.3f LSB' % (k, s1, ss1, s1 * KPHYS, r1, sr1))
            o, sr = multifit([th, temps[k]], code, ['cas[LSB/h]', k + '[LSB/C]'])
            print('     kod ~ cas + %-9s: %s | rezidua sd %.3f LSB' % (k, ', '.join(o), sr))
    # hodinova tabulka
    print('  hodina  | n  | kod prum | dKod/h LSB | ppt   | T_ocxo | T_desky | T_modulu | TIE sd | y sd ppt | ref ppb (zdroj)')
    h0 = int(t[0] // 3600)
    prev = None
    for h in range(h0, int(t[-1] // 3600) + 1):
        idx = [i for i in range(len(ok)) if int(t[i] // 3600) == h]
        if not idx:
            continue
        def m(k):
            v = [fl(ok[i].get(k, '')) for i in idx]
            v = [x for x in v if x is not None]
            return (sum(v) / len(v)) if v else float('nan')
        def sd(k, sc=1.0):
            v = [fl(ok[i].get(k, '')) for i in idx]
            v = [x * sc for x in v if x is not None]
            return st.stdev(v) if len(v) > 1 else float('nan')
        c = m('code')
        dc = (c - prev) if prev is not None else float('nan')
        prev = c
        srcs = sorted(set(ok[i].get('ref_src', '') for i in idx))
        import datetime
        hh = datetime.datetime.fromtimestamp(h * 3600).strftime('%H:00')
        print('  %s   | %2d | %8.2f | %+10.2f | %+5.0f | %6.2f | %7.2f | %8.2f | %6.2f | %8.1f | %s %s'
              % (hh, len(idx), c, dc, dc * KPHYS, m('t_ocxo_c'), m('t_board_c'), m('t_mod_c'), sd('tie_ns'), sd('y_ppb', 1000.0),
                 ('%.3f' % m('ref_ppb')) if 'ref_ppb' in hdr else '--', '/'.join(srcs)))
    return hdr, ok, t


hdr16, ok16, t16 = rozbor_noci('NOC 2026-11-16/17, v0.45.21, K 3,85e-11', sys.argv[1])
# 1PPS, GNSS, nejistota reference
def ic(r, k):
    return int(r[k]) if r.get(k, '') != '' else None
f, l = ok16[0], ok16[-1]
el = fl(l['unix']) - fl(f['unix'])
print('  1PPS: prijato %+d za %.0f s (ocekavano ~%.0f), odmitnuto %d->%d, diry %d->%d, roztrzena %d->%d'
      % (ic(l, 'pps_rx') - ic(f, 'pps_rx'), el, el, ic(f, 'pps_rej'), ic(l, 'pps_rej'), ic(f, 'pps_gap'), ic(l, 'pps_gap'),
         ic(f, 'pps_torn'), ic(l, 'pps_torn')))
g = {}
for r in ok16:
    g[(r['gnss'], r['fix'])] = g.get((r['gnss'], r['fix']), 0) + 1
sats = [int(r['sats']) for r in ok16 if r['sats']]
hd = [float(r['hdop']) for r in ok16 if r['hdop']]
print('  GNSS:', g, '| druzic min %d med %d max %d | HDOP min %.1f max %.1f' % (min(sats), st.median(sats), max(sats), min(hd), max(hd)))
src = [(r['iso'], r['ref_src'], r['ref_ppb']) for r in ok16]
for i in range(1, len(src)):
    if src[i][1] != src[i - 1][1]:
        print('  zdroj nejistoty reference: %s -> %s v %s (%s ppb)' % (src[i - 1][1], src[i][1], src[i][0], src[i][2]))
zm = [float(r['ref_ppb']) for r in ok16 if r['ref_src'] == 'zmereno' and r['ref_ppb']]
if zm:
    a = np.array(zm)
    print('  nejistota reference (zmereno): n %d | median %.3f | p95 %.3f | max %.3f | min %.3f ppb' % (len(a), np.median(a), np.percentile(a, 95), a.max(), a.min()))
# ADEV z TIE (OCXO proti GPS 1PPS, smycka zavrena)
tie = np.array([fl(r['tie_ns']) for r in ok16])
tau0 = float(np.median(np.diff(t16)))
print('  ADEV z TIE (tau0 %.0f s, x v ns -> y bez jednotky):' % tau0)
for tau, ad, n in oadev_phase(tie * 1e-9, tau0, [1, 2, 5, 10, 20, 50, 100]):
    print('    tau %6.0f s: %.2e  (n %d)' % (tau, ad, n))
ph = np.array([fl(r['phase_ns']) for r in ok16])
print('  ADEV z faze smycky:')
for tau, ad, n in oadev_phase(ph * 1e-9, tau0, [1, 2, 5, 10, 20, 50, 100]):
    print('    tau %6.0f s: %.2e  (n %d)' % (tau, ad, n))
print('  prumerny kmitocet z TIE za celou noc: (TIE_konec - TIE_zacatek)/T = %+.3f ppt' % ((tie[-1] - tie[0]) * 1e3 / (t16[-1] - t16[0])))
s_, ss_, _, _, rr_ = linfit((t16 - t16[0]), tie)
print('  sklon TIE (LS): %+.4f +- %.4f ns/h = %+.3f ppt | r %.3f' % (s_ * 3600, ss_ * 3600, s_ * 1e3, rr_))

# status.log
print('=' * 100)
print('STATUS.LOG')
txt = open(sys.argv[2], encoding='utf-8', errors='replace').read()
dumps = re.split(r'\n==== (\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) ====\n', txt)
pairs = list(zip(dumps[1::2], dumps[2::2]))
print('  vypisu', len(pairs))
pat = {
    'uptime': r'uptime (\d+)s', 'reset': r'Reset: ([^\n]+)', 'varovani': r'VAROVANI: ([^\n]+)', 'cpu': r'CPU (\d+)%',
    'heapmin': r'min-ever (\d+) B', 'dacerr': r'chyb I2C (\d+)', 'rbmis': r'readback nesedi (\d+)', 'ltdc': r'podteceni FIFO (\d+) /',
    'i2c4': r'TMP117 0x48 err (\d+)', 'fpgacrc': r'CRC err (\d+)', 'seqgap': r'SEQ FPGA: diry (\d+)', 'gnssdrop': r'poklesu (\d+)',
    'rekal': r'teplot. rekal (\d+)x', 'sticky': r'sticky 247 = (\w+)', 'glyf': r'preskocenych glyfu (\d+)',
    'stkdef': r'stack default free (\d+)', 'stki2c': r'stack I2C4\s+free (\d+)', 'stkfpga': r'stack Fpga\s+free (\d+)',
}
first, last = {}, {}
changes = {k: [] for k in pat}
prevv = {}
for ts, body in pairs:
    for k, p in pat.items():
        m = re.search(p, body)
        v = m.group(1).strip() if m else None
        if k not in first:
            first[k] = v
        if k in prevv and v != prevv[k] and k not in ('uptime', 'cpu', 'heapmin', 'stkdef', 'stki2c', 'stkfpga'):
            changes[k].append((ts, prevv[k], v))
        prevv[k] = v
        last[k] = v
for k in pat:
    print('  %-9s prvni %-28s posledni %-28s zmen %d %s' % (k, first[k], last[k], len(changes[k]), changes[k][:3] if changes[k] else ''))
cpus = [int(m) for m in re.findall(r'CPU (\d+)%', txt)]
print('  CPU %%: min %d max %d' % (min(cpus), max(cpus)))
ups = [int(m) for m in re.findall(r'uptime (\d+)s', txt)]
print('  uptime monotonni:', all(b > a for a, b in zip(ups, ups[1:])))
print('  nejistota reference po vypisech: cas | ref | ADEV faze | sklon | drift kodu | prochazka | pred vstupem | tau min')
for ts, body in pairs:
    m = re.search(r'nejistota reference ([\d.]+) ppb \((\w+)\) \| ADEV faze (\S+)(?: ppt)? \| sklon faze (\S+)(?: ppt)? \| drift kodu (\S+)(?: ppt/h)? \| prochazka kodu (\S+)(?: ppt/sqrt\(h\))? \| pred vstupem do stavu (\S+)', body)
    mt = re.search(r'tau min\. (\d+) s', body)
    mh = re.search(r'HDEV faze e-12: ([^\n]+)', body)
    if m:
        print('    %s | %s %-7s | %4s | %4s | %4s | %4s | %4s | %s | %s' % (ts[11:16], m.group(1), m.group(2), m.group(3), m.group(4), m.group(5),
                                                               m.group(6), m.group(7), mt.group(1) if mt else '--', mh.group(1).strip() if mh else ''))

if len(sys.argv) > 3:
    rozbor_noci('NOC 2026-11-13/14, K 3,4e-11 (logger bez kladnych cisel -> y/faze/TIE neuplne)', sys.argv[3])
