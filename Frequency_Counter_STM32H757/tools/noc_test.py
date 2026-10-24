#!/usr/bin/env python3
"""noc_test.py -- dlouhy beh prístroje se záznamem diagnostiky pres UART (bez haltu cile, jen status/fpga v3/gpsdo).

Pouziti (z adresare Frequency_Counter_STM32H757):
    python tools/noc_test.py --port COM10 --hours 10 --period 600 --out <soubor.log>
Kazdych <period> s zapise do <out> blok s casovym razitkem: kmitocet, kanal, status (vybrane radky), fpga v3, gpsdo,
tdc. Na konci (nebo po Ctrl+C) vypise souhrn: min/max uptime skoky (restart = vypadek), vyvoj poctu mezer / RX chyb /
odmitnutych 1PPS, sigma_y, stav smycky, extremy teplot. Zadny zasah do prístroje (jen cteni); CHAN se nemeni.
UART prikazy jdou pres tools/uartq.ps1 (exkluzivni pristup k portu -- zadny jiny terminal nesmi byt otevreny).
"""
import argparse, datetime, os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UARTQ = os.path.join(ROOT, 'tools', 'uartq.ps1')


def uart(port, cmds, idle=2000, mx=25000, timeout=90):
    cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', UARTQ, '-Port', port,
           '-Idle', str(idle), '-Max', str(mx), '-Cmds', ';'.join(cmds)]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, encoding='utf-8', errors='replace')
        return r.stdout
    except subprocess.TimeoutExpired:
        return '(TIMEOUT uartq)\n'


KEEP = re.compile(r'RUNNING|Reset:|Heap:|CM4:|NET:|GPSDO:|FPGA: link|FPGA FW|SEQ FPGA|STATISTIKA: (sigma|ADEV|tau0|filtr)|'
                  r'TDC REZERVA|TDC: teplot|CITANI HRAN|FPGA SAMOKONTROLA|FPGA HODINY|FPGA DIAG|REGR MEZ|REFERENCE:|'
                  r'DAC OCXO: (dither|zapisu)|LTDC|DMA2D: chyb|I2C4:|WATCHDOG|GPIO HLIDAC|ULOZISTE|VAROVANI|DATALOG|'
                  r'TEPLOTA|ocxo|OCXO')


def sample(port):
    out = {}
    out['status'] = uart(port, ['status full'])
    out['fpga'] = uart(port, ['fpga v3'])
    out['freq'] = uart(port, ['freq', 'scpi SENS:FREQ:CHAN?', 'scpi MEAS:FREQ?'])
    out['tdc'] = uart(port, ['tdc'])
    return out


def keep(txt):
    return '\n'.join(l.rstrip() for l in txt.splitlines() if KEEP.search(l))


def num(rx, txt, cast=float):
    m = re.search(rx, txt)
    return cast(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM10')
    ap.add_argument('--hours', type=float, default=10.0)
    ap.add_argument('--period', type=int, default=600)
    ap.add_argument('--out', default=os.path.join(ROOT, 'tools', 'noc_test.log'))
    a = ap.parse_args()
    t_end = time.time() + a.hours * 3600.0
    rows = []
    n = 0
    with open(a.out, 'a', encoding='utf-8') as f:
        f.write('==== noc_test start %s, %.1f h po %d s ====\n' % (datetime.datetime.now().isoformat(timespec='seconds'), a.hours, a.period))
        f.flush()
        try:
            while time.time() < t_end:
                t0 = time.time()
                s = sample(a.port)
                n += 1
                stamp = datetime.datetime.now().isoformat(timespec='seconds')
                f.write('\n---- vzorek %d  %s ----\n' % (n, stamp))
                f.write(keep(s['status']) + '\n')
                f.write(keep(s['fpga']) + '\n')
                for l in s['fpga'].splitlines():
                    if re.search(r'ramcu|REC_HEAD|hlavicka|1PPS:|predelicka', l):
                        f.write(l.rstrip() + '\n')
                f.write(s['freq'].strip() + '\n')
                for l in s['tdc'].splitlines():
                    if re.search(r'^TDC [AB]:|rezerva', l):
                        f.write(l.rstrip() + '\n')
                f.flush()
                st = s['status'] + s['fpga'] + s['freq']
                rows.append(dict(
                    t=stamp,
                    up=num(r'uptime\s+(\d+)s', st, int),
                    mezer=num(r'mezer (\d+) ', st, int),
                    resync=num(r'resync (\d+),', st, int),
                    crc=num(r'RX chyb CRC (\d+)', st, int),
                    dlen=num(r'delka (\d+)\b', st, int),
                    rej=num(r'odmitnuto (\d+)', st, int),
                    sig1=num(r'sigma_y@1s = (-?\d+) e-15', st, int),
                    sigh=num(r'sigma_y@hradlo = (-?\d+) e-15', st, int),
                    state=(re.search(r'GPSDO: ([A-Z]+)', st) or [None, '?'])[1],
                    ch=num(r'CHAN\?\s*\n\s*(\d)', st, int),
                    spike=num(r'spike-reject (\d+)', st, int),
                    link=('link OK' in st),
                ))
                dt = a.period - (time.time() - t0)
                if dt > 0:
                    time.sleep(dt)
        except KeyboardInterrupt:
            pass
        # souhrn
        f.write('\n==== SOUHRN (%d vzorku) ====\n' % len(rows))
        restarts = 0
        for p, c in zip(rows, rows[1:]):
            if p['up'] is not None and c['up'] is not None and c['up'] < p['up']:
                restarts += 1
                f.write('RESTART mezi %s a %s (uptime %s -> %s)\n' % (p['t'], c['t'], p['up'], c['up']))
        f.write('restartu: %d\n' % restarts)
        for k in ('mezer', 'resync', 'crc', 'dlen', 'rej', 'spike'):
            v = [r[k] for r in rows if r[k] is not None]
            if v:
                f.write('%-7s prvni %d posledni %d (max %d)\n' % (k, v[0], v[-1], max(v)))
        for k in ('sig1', 'sigh'):
            v = [r[k] for r in rows if r[k] is not None and r[k] > 0]
            if v:
                f.write('%-7s min %d max %d [e-15]\n' % (k, min(v), max(v)))
        f.write('GPSDO stavy: %s\n' % ', '.join(sorted({r['state'] for r in rows})))
        f.write('link ztracen ve vzorcich: %d\n' % sum(1 for r in rows if not r['link']))
        f.write('kanaly: %s\n' % sorted({r['ch'] for r in rows if r['ch'] is not None}))
        f.flush()
    print('hotovo, log:', a.out)


if __name__ == '__main__':
    main()
