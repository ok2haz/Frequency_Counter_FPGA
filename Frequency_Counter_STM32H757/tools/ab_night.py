#!/usr/bin/env python3
"""ab_night.py -- samostatny nocni test A-B a vady kanalu B ve FPGA (L-0247, firmware >= v0.42.10).

Obe vstupy A a B na tomtez zdroji 10 MHz. Test (1) pocka --warmup s na dohrati, (2) vynuluje `fpga v3 seg`,
(3) po celou dobu sbira mereni A a B z webu (SSE /api/stream) do ab.csv, (4) kazdych --period s precte pres UART
`status`, `fpga v3`, `fpga v3 seg`, `fpga v3 x0` a ulozi citace do cykly.csv, nove velke odchylky X0 do x0_velke.csv
a cely vypis do uart.log, (5) na konci (nebo po Ctrl+C) napise report.txt: A-B po hodinach, ocas, kdo ulital.

Vystup (adresar --outdir, vychozi noc_logs/ab_<datum_cas>/):
  ab.csv         t_unix, seq, f_primary (A), f_secondary (B), gate_ns, channel, signal_lost
  cykly.csv      kazdy cyklus: uptime, verze, okna B / z LSQ, okna s vadnym segmentem, segmenty, SX mimo meze, X0 opravy
  x0_velke.csv   odchylky X0 >= 8 hran (seg, d, pd, priznaky, surove dx/dt/X0)
  udalosti.csv   START, WARMUP, RESTART, NO_UART, NO_WEB, KONEC
  uart.log       surove vypisy UART s casem
  report.txt     souhrn

Pouziti (z adresare Frequency_Counter_STM32H757; zadny jiny terminal nesmi drzet COM port):
    python tools/ab_night.py                         # 16 h, COM10, 10.0.0.106
    python tools/ab_night.py --hours 0.25 --warmup 0 # kratka zkouska
    python tools/ab_night.py --report noc_logs/ab_X  # jen report z existujiciho adresare
    python tools/ab_night.py --selftest              # test parsovani a reportu bez desky
"""
import argparse, csv, datetime, json, os, re, subprocess, sys, threading, time, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UARTQ = os.path.join(ROOT, 'tools', 'uartq.ps1')
F0 = 1e7

RE_UP = re.compile(r'RUNNING gpsdo-ui (v[\d.]+)\s+uptime\s+(\d+)s')
RE_WIN = re.compile(r'druhy kanal: mereni (\d+), z toho delka okna z LSQ (\d+) \| oken s vadnym segmentem \(z LSQ vyrazen\): A (\d+) B (\d+)')
RE_X0 = re.compile(r'X0 proti predpovedi z T0: opraveno \(64\*k hran\) A (\d+) B (\d+), posledni (-?\d+) / (-?\d+) \| '
                   r'mimo predpoved bez opravy A (\d+) B (\d+)')
RE_SEG = re.compile(r'kanal ([AB]): segmentu (\d+), vadnych \(> 2 ns nebo SX mimo meze\) (\d+)')
RE_SXB = re.compile(r'^\s+([AB]): SX mimo meze (\d+) \(z toho odchylka <= 2 ns (\d+)\)', re.M)
RE_BIGN = re.compile(r'velke \(\|d\| >= 8 hran\): celkem (\d+)')
RE_EV = re.compile(r'^\s+([AB]) seg\s+(\d+)\s+d (-?\d+) m\s+pd (-?\d+) m\s+(\S{3})\s*\n\s+dx ([0-9A-F]{16}) dt ([0-9A-F]{16}) X0 ([0-9A-F]{16})', re.M)

CYK_COLS = ['cas', 'uptime_s', 'verze', 'okna_b', 'lsq_b', 'vadseg_okna_a', 'vadseg_okna_b', 'seg_a', 'seg_b',
            'vadnych_seg_a', 'vadnych_seg_b', 'sxmimo_a', 'sxmimo_b', 'x0_fix_a', 'x0_fix_b', 'x0_odd_a', 'x0_odd_b', 'x0_velkych']


def iso():
    return datetime.datetime.now().isoformat(timespec='seconds')


def uart(port, cmds, idle=2500, mx=60000, timeout=150):
    cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', UARTQ, '-Port', port,
           '-Idle', str(idle), '-Max', str(mx), '-Cmds', ';'.join(cmds)]
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, encoding='utf-8', errors='replace').stdout
    except subprocess.TimeoutExpired:
        return ''


def parse_cycle(txt):
    """Citace z vypisu `status;fpga v3;fpga v3 seg;fpga v3 x0` -> dict (chybejici pole prazdna)."""
    o = {k: '' for k in CYK_COLS}
    m = RE_UP.search(txt)
    if m: o['verze'], o['uptime_s'] = m.group(1), int(m.group(2))
    m = RE_WIN.search(txt)
    if m: o['okna_b'], o['lsq_b'], o['vadseg_okna_a'], o['vadseg_okna_b'] = (int(x) for x in m.groups())
    m = RE_X0.search(txt)
    if m:
        g = [int(x) for x in m.groups()]
        o['x0_fix_a'], o['x0_fix_b'], o['x0_odd_a'], o['x0_odd_b'] = g[0], g[1], g[4], g[5]
    for m in RE_SEG.finditer(txt):
        c = m.group(1).lower()
        o['seg_' + c], o['vadnych_seg_' + c] = int(m.group(2)), int(m.group(3))
    for m in RE_SXB.finditer(txt):
        o['sxmimo_' + m.group(1).lower()] = int(m.group(2))
    m = RE_BIGN.search(txt)
    if m: o['x0_velkych'] = int(m.group(1))
    return o


def parse_big(txt):
    """Velke odchylky X0 (kruh |d| >= 8 hran, s radkem dx/dt/X0)."""
    out = []
    for m in RE_EV.finditer(txt):
        out.append({'kanal': m.group(1), 'seg': int(m.group(2)), 'd_hran': int(m.group(3)) / 1000.0,
                    'pd_hran': int(m.group(4)) / 1000.0, 'priznaky': m.group(5),
                    'dx': int(m.group(6), 16), 'dt': int(m.group(7), 16), 'x0': int(m.group(8), 16)})
    return out


class Collector(threading.Thread):
    """SSE /api/stream -> ab.csv (jako C:/tmp_opt/ab_collect.py), samo se znovu pripoji."""
    def __init__(self, ip, path, ev):
        super().__init__(daemon=True)
        self.ip, self.path, self.ev, self.stop, self.n, self.reconn = ip, path, ev, False, 0, 0

    def run(self):
        new = not os.path.exists(self.path)
        f = open(self.path, 'a', buffering=1, newline='')
        if new: f.write('t_unix,seq,f_primary,f_secondary,gate_ns,channel,signal_lost\n')
        last = None
        while not self.stop:
            try:
                req = urllib.request.urlopen('http://%s/api/stream' % self.ip, timeout=30)
                while not self.stop:
                    line = req.readline().decode('utf-8', 'replace')
                    if not line: break
                    if not line.startswith('data:'): continue
                    try: d = json.loads(line[5:])
                    except ValueError: continue
                    fa, fb, seq = d.get('freq_hz'), d.get('freq16_hz'), d.get('seq_meas')
                    if fa is None or fb is None or seq == last: continue
                    last = seq
                    f.write('%.2f,%d,%.9f,%.9f,%d,%s,%s\n' % (time.time(), seq, fa, fb, d.get('gate_ns') or 0,
                                                              d.get('channel'), d.get('signal_lost')))
                    self.n += 1
                req.close()
            except Exception as e:
                self.reconn += 1
                self.ev('NO_WEB', str(e)[:120])
                time.sleep(10)
        f.close()


def mad(v):
    s = sorted(v); m = s[len(s) // 2]
    return m, sorted(abs(x - m) for x in v)[len(v) // 2] * 1.4826


def report(outdir):
    """report.txt z ab.csv (+ cykly.csv, x0_velke.csv, udalosti.csv)."""
    p = os.path.join(outdir, 'ab.csv')
    lines = []
    rows = []
    if os.path.exists(p):
        for r in csv.DictReader(open(p, encoding='utf-8')):
            try:
                fa, fb, g = float(r['f_primary']), float(r['f_secondary']), int(r['gate_ns'])
                if fa > 0 and fb > 0 and g > 0: rows.append((float(r['t_unix']), int(r['seq']), fa, fb, g))
            except (ValueError, KeyError):
                continue
    lines.append('ab_night report %s, adresar %s' % (iso(), outdir))
    if len(rows) < 20:
        lines.append('malo oken A-B (%d)' % len(rows))
    else:
        dab = [(fa - fb) / fa * g * 1e3 for _, _, fa, fb, g in rows]             # ps
        med, s = mad(dab)
        t0 = rows[0][0]
        lines.append('oken %d za %.2f h, median A-B %.1f ps, sigma(MAD) %.1f ps' % (len(rows), (rows[-1][0] - t0) / 3600, med, s))
        lines.append('  |A-B - med| > 100 / 200 / 500 ps / 1 ns / 1 us: ' + ' / '.join(
            str(sum(1 for x in dab if abs(x - med) > th)) for th in (100, 200, 500, 1000, 1e6)))
        # kdo ulital: odchylka od medianu sousedu +-3 okna (druha diference by sum zdroje nechala)
        ya = [fa / F0 - 1 for _, _, fa, _, _ in rows]; yb = [fb / F0 - 1 for _, _, _, fb, _ in rows]
        ca = cb = 0
        for i, x in enumerate(dab):
            if abs(x - med) <= 200: continue
            lo, hi = max(0, i - 3), min(len(rows), i + 4)
            na = sorted(ya[j] for j in range(lo, hi) if j != i); nb = sorted(yb[j] for j in range(lo, hi) if j != i)
            if abs(ya[i] - na[len(na) // 2]) > abs(yb[i] - nb[len(nb) // 2]): ca += 1
            else: cb += 1
        lines.append('  okna s |A-B| > 200 ps: vic ulita A %d, B %d' % (ca, cb))
        # pary sousednich oken s opacnou chybou > 1 us (X0 o 64*k hran)
        pairs = [(rows[i][1], dab[i] - med, dab[i + 1] - med) for i in range(len(dab) - 1)
                 if abs(dab[i] - med) > 1e6 and abs(dab[i + 1] - med) > 1e6 and (dab[i] - med) * (dab[i + 1] - med) < 0]
        lines.append('  pary oken s opacnou chybou > 1 us (X0 o 64*k hran): %d' % len(pairs))
        for sq, a, b in pairs[:20]:
            lines.append('    seq %d: %+.3f us / %+.3f us (%+.1f / %+.1f hran po 100 ns)' % (sq, a / 1e6, b / 1e6, a / 1e5, b / 1e5))
        big = sorted(range(len(dab)), key=lambda i: -abs(dab[i] - med))[:10]
        lines.append('  nejvetsi: ' + ', '.join('seq %d %+.0f ps' % (rows[i][1], dab[i] - med) for i in big))
        lines.append('')
        lines.append('po hodinach:  hod   oken  sigma[ps]  >200  >500  >1ns  >1us')
        h = 0
        while True:
            seg = [dab[i] for i in range(len(rows)) if t0 + 3600 * h <= rows[i][0] < t0 + 3600 * (h + 1)]
            if not seg: break
            if len(seg) >= 20:
                _, sh = mad(seg)
                c = [sum(1 for x in seg if abs(x - med) > th) for th in (200, 500, 1000, 1e6)]
                lines.append('             %4d %6d %9.1f %5d %5d %5d %5d' % (h, len(seg), sh, *c))
            h += 1
    pc = os.path.join(outdir, 'cykly.csv')
    if os.path.exists(pc):
        cy = list(csv.DictReader(open(pc, encoding='utf-8')))
        if cy:
            first, last = cy[0], cy[-1]
            lines.append('')
            lines.append('citace z desky (prvni cyklus -> posledni; po restartu se nuluji):')
            for k in CYK_COLS[1:]:
                lines.append('  %-15s %s -> %s' % (k, first.get(k, ''), last.get(k, '')))
    pe = os.path.join(outdir, 'udalosti.csv')
    if os.path.exists(pe):
        ev = list(csv.reader(open(pe, encoding='utf-8')))[1:]
        cnt = {}
        for e in ev: cnt[e[1]] = cnt.get(e[1], 0) + 1
        lines.append('')
        lines.append('udalosti: ' + ', '.join('%s %d' % kv for kv in sorted(cnt.items())))
    px = os.path.join(outdir, 'x0_velke.csv')
    if os.path.exists(px):
        xs = list(csv.DictReader(open(px, encoding='utf-8')))
        lines.append('velkych odchylek X0 zapsano: %d (podrobne x0_velke.csv)' % len(xs))
    txt = '\n'.join(lines) + '\n'
    open(os.path.join(outdir, 'report.txt'), 'w', encoding='utf-8').write(txt)
    return txt


def run(a):
    out = a.outdir or os.path.join(ROOT, 'noc_logs', 'ab_' + datetime.datetime.now().strftime('%Y%m%d_%H%M'))
    os.makedirs(out, exist_ok=True)
    fe = open(os.path.join(out, 'udalosti.csv'), 'a', newline='', buffering=1)
    we = csv.writer(fe)
    if fe.tell() == 0: we.writerow(['cas', 'udalost', 'popis'])
    lock = threading.Lock()

    def ev(kind, txt=''):
        with lock:
            we.writerow([iso(), kind, txt]); fe.flush()
        print(iso(), kind, txt, flush=True)

    flog = open(os.path.join(out, 'uart.log'), 'a', encoding='utf-8', buffering=1)
    fc = open(os.path.join(out, 'cykly.csv'), 'a', newline='', buffering=1)
    wc = csv.DictWriter(fc, CYK_COLS)
    if fc.tell() == 0: wc.writeheader()
    fx = open(os.path.join(out, 'x0_velke.csv'), 'a', newline='', buffering=1)
    xcols = ['cas', 'kanal', 'seg', 'd_hran', 'pd_hran', 'priznaky', 'dx', 'dt', 'x0']
    wx = csv.DictWriter(fx, xcols)
    if fx.tell() == 0: wx.writeheader()

    t_end = time.time() + a.hours * 3600
    ev('START', 'hodin %.2f, perioda %d s, port %s, web %s, vystup %s' % (a.hours, a.period, a.port, a.ip, out))
    txt = uart(a.port, ['version'])
    flog.write('==== %s\n%s\n' % (iso(), txt))
    m = re.search(r'gpsdo-ui v(\d+)\.(\d+)\.(\d+)', txt)
    if not m: ev('NO_UART', 'version neodpovedel (port drzi jiny terminal?)')
    elif tuple(int(x) for x in m.groups()) < (0, 42, 10): ev('VERZE', 'firmware v%s.%s.%s < v0.42.10 (chybi oprava X0)' % m.groups())
    if a.warmup > 0:
        ev('WARMUP', 'cekam %d s' % a.warmup)
        time.sleep(min(a.warmup, max(0, t_end - time.time())))
    flog.write('==== %s\n%s\n' % (iso(), uart(a.port, ['fpga v3 seg reset'])))
    col = Collector(a.ip, os.path.join(out, 'ab.csv'), ev)
    col.start()
    last_up, seen = None, set()
    try:
        while time.time() < t_end:
            t_next = time.time() + a.period
            txt = uart(a.port, ['status', 'fpga v3', 'fpga v3 seg', 'fpga v3 x0'], mx=90000)
            flog.write('==== %s\n%s\n' % (iso(), txt))
            c = parse_cycle(txt)
            if c['uptime_s'] == '':
                ev('NO_UART', 'status bez odpovedi')
            else:
                if last_up is not None and c['uptime_s'] < last_up: ev('RESTART', 'uptime %s -> %s s' % (last_up, c['uptime_s']))
                last_up = c['uptime_s']
                c['cas'] = iso()
                wc.writerow(c)
                for e in parse_big(txt):
                    key = (e['kanal'], e['seg'], e['dx'], e['dt'])
                    if key in seen: continue
                    seen.add(key)
                    e = dict(e, cas=iso(), dx='%016X' % e['dx'], dt='%016X' % e['dt'], x0='%016X' % e['x0'])
                    wx.writerow(e)
                print('%s cyklus: up %s s, oken A-B %d, okna s vadnym seg. B %s, X0 opraveno B %s, bez opravy B %s' %
                      (iso(), c['uptime_s'], col.n, c['vadseg_okna_b'], c['x0_fix_b'], c['x0_odd_b']), flush=True)
            while time.time() < min(t_next, t_end): time.sleep(5)
    except KeyboardInterrupt:
        ev('PRERUSENO', 'Ctrl+C')
    col.stop = True
    time.sleep(2)
    ev('KONEC', 'oken A-B %d, znovupripojeni webu %d' % (col.n, col.reconn))
    for f in (flog, fc, fx): f.close()
    print(report(out))
    fe.close()


def selftest():
    txt = """RUNNING gpsdo-ui v0.42.10  uptime 1234s
  druhy kanal: mereni 3724, z toho delka okna z LSQ 3539 | oken s vadnym segmentem (z LSQ vyrazen): A 0 B 313
  X0 proti predpovedi z T0: opraveno (64*k hran) A 0 B 2, posledni 0 / -64 | mimo predpoved bez opravy A 0 B 22
  kanal A: segmentu 43119, vadnych (> 2 ns nebo SX mimo meze) 0, SX != N(N-1)/2 21560, max 409 ps, rms dobrych 102 ps
  kanal B: segmentu 43119, vadnych (> 2 ns nebo SX mimo meze) 2963, SX != N(N-1)/2 23056, max 2147483647 ps, rms dobrych 109 ps
    A: SX mimo meze 0 (z toho odchylka <= 2 ns 0) | v mezich, odchylka 0,5-1 ns 0, 1-2 ns 0
    B: SX mimo meze 2903 (z toho odchylka <= 2 ns 37) | v mezich, odchylka 0,5-1 ns 0, 1-2 ns 0
  velke (|d| >= 8 hran): celkem 1
    B seg 10971  d -64007 m  pd -20687 m  -CP
      dx 000000000003D050 dt 0000002540BE4000 X0 000000020680AED9
  vsechny (|d| >= 0,5 hrany): celkem 1
    B seg 10971  d -64007 m  pd -20687 m  -CP
"""
    c = parse_cycle(txt)
    ok = (c['verze'] == 'v0.42.10' and c['uptime_s'] == 1234 and c['okna_b'] == 3724 and c['lsq_b'] == 3539 and
          c['vadseg_okna_b'] == 313 and c['x0_fix_b'] == 2 and c['x0_odd_b'] == 22 and c['seg_a'] == 43119 and
          c['vadnych_seg_b'] == 2963 and c['sxmimo_a'] == 0 and c['sxmimo_b'] == 2903 and c['x0_velkych'] == 1)
    b = parse_big(txt)
    ok = ok and len(b) == 1 and b[0]['seg'] == 10971 and b[0]['d_hran'] == -64.007 and b[0]['dx'] == 0x3D050
    # report nad umelym ab.csv: 400 oken, jeden par +-12,8 us v B, jedno okno B +600 ps
    import tempfile
    d = tempfile.mkdtemp(prefix='ab_night_')
    with open(os.path.join(d, 'ab.csv'), 'w', newline='') as f:
        f.write('t_unix,seq,f_primary,f_secondary,gate_ns,channel,signal_lost\n')
        for i in range(400):
            fb = F0 * (1 + ((-5.12e-5 if i == 100 else 5.12e-5 if i == 101 else 2.4e-9 if i == 200 else 0.0)))
            fa = F0 * (1 + 1e-11 * ((i * 7919) % 13 - 6))
            f.write('%.2f,%d,%.9f,%.9f,%d,0,False\n' % (1000.0 + 0.25 * i, i, fa, fb, 250000000))
    r = report(d)
    ok = ok and 'pary oken s opacnou chybou > 1 us (X0 o 64*k hran): 1' in r and 'vic ulita A 0, B 3' in r
    print(r)
    print('SELFTEST', 'PASS' if ok else 'FAIL')
    return ok


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', default='COM10')
    ap.add_argument('--ip', default='10.0.0.106')
    ap.add_argument('--hours', type=float, default=16.0)
    ap.add_argument('--period', type=int, default=600, help='odstup cyklu UART [s]')
    ap.add_argument('--warmup', type=int, default=300, help='cekani pred startem [s]')
    ap.add_argument('--outdir', default='')
    ap.add_argument('--report', default='', help='jen report z adresare')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest: sys.exit(0 if selftest() else 1)
    if a.report: print(report(a.report)); sys.exit(0)
    run(a)
