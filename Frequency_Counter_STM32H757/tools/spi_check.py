#!/usr/bin/env python3
"""spi_check.py -- overeni SPI2 25 MHz (STATUS #342, v0.40.5+) na desce: takt, CRC chyby, diry v SEQ, sigma_y.

Kazdych --period s precte pres UART `spi` + `status` (jen cteni) a z webu /api/state sigma_y@1s (sy1e15). Na konci souhrn
a verdikt: takt musi byt 25 000 000 Hz, prirustek CRC err a SEQ der za beh se porovna s referenci 12,5 MHz
(2026-10-25: 66 CRC chyb za 11,8 h = 5,6/h, SEQ diry 14 za 11,8 h; casto zpusobene vlastnimi UART dotazy -> proto dlouhy --period).

Pouziti (z adresare Frequency_Counter_STM32H757; zadny jiny terminal nesmi drzet port):
    python tools/spi_check.py --port COM10 --hours 1 --period 120 --out spi25.csv [--ip 10.0.0.106]
Po flashi v0.40.5 nebo novejsi a power-cyklu. Pro porovnani sigma_y mit na vstupu A totez co pri referenci (30 MHz generator:
sigma_y@1s 5,9e-11; ext. GPSDO 1,8e-10). Ctrl+C = predcasny konec se souhrnem.
"""
import argparse, csv, datetime, json, os, re, subprocess, time, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UARTQ = os.path.join(ROOT, 'tools', 'uartq.ps1')
COLS = ['t_iso', 'fw', 'uptime_s', 'sck_hz', 'crc_err', 'seq_diry', 'seq_missed', 'resync', 'sy1e15']
REF_CRC_PER_H = 5.6


def uart(port, cmds, idle=2000, mx=30000, timeout=90):
    cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', UARTQ, '-Port', port,
           '-Idle', str(idle), '-Max', str(mx), '-Cmds', ';'.join(cmds)]
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, encoding='utf-8', errors='replace').stdout
    except subprocess.TimeoutExpired:
        return ''


def num(rx, txt, cast=int):
    m = re.search(rx, txt)
    return cast(m.group(1)) if m else None


def sy(ip):
    try:
        with urllib.request.urlopen('http://%s/api/state' % ip, timeout=5) as r:
            return json.loads(r.read().decode('utf-8', 'replace')).get('sy1e15')
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM10')
    ap.add_argument('--ip', default='10.0.0.106')
    ap.add_argument('--hours', type=float, default=1.0)
    ap.add_argument('--period', type=float, default=120.0)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    t_end = time.time() + a.hours * 3600.0
    rows = []
    with open(a.out, 'a', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=COLS)
        if f.tell() == 0:
            w.writeheader()
        try:
            while time.time() < t_end:
                t0 = time.time()
                txt = uart(a.port, ['spi', 'status'])
                m = re.search(r'RUNNING gpsdo-ui (v[\d.]+)\s+uptime\s+(\d+)s', txt)
                if m:
                    r = dict(t_iso=datetime.datetime.now().isoformat(timespec='seconds'), fw=m.group(1), uptime_s=int(m.group(2)),
                             sck_hz=num(r'SCK (\d+) Hz', txt), crc_err=num(r'CRC err (\d+)', txt),
                             seq_diry=num(r'SEQ FPGA: diry (\d+)', txt), seq_missed=num(r'zmeskano (\d+) mereni', txt),
                             resync=num(r'resync (\d+)', txt), sy1e15=sy(a.ip))
                    rows.append(r)
                    w.writerow(r)
                    f.flush()
                    print('%s up %ds SCK %s CRC %s diry %s sy1s %s' % (r['t_iso'], r['uptime_s'], r['sck_hz'], r['crc_err'], r['seq_diry'], r['sy1e15']))
                dt = a.period - (time.time() - t0)
                if dt > 0:
                    time.sleep(dt)
        except KeyboardInterrupt:
            pass
    if len(rows) < 2:
        print('malo vzorku')
        return
    h = (rows[-1]['uptime_s'] - rows[0]['uptime_s']) / 3600.0
    ok = all(r['sck_hz'] == 25000000 for r in rows)
    dcrc = (rows[-1]['crc_err'] or 0) - (rows[0]['crc_err'] or 0)
    ddir = (rows[-1]['seq_diry'] or 0) - (rows[0]['seq_diry'] or 0)
    sys_ = [r['sy1e15'] for r in rows if r['sy1e15']]
    print('\n==== SOUHRN %.2f h, %d vzorku ====' % (h, len(rows)))
    print('SCK 25 MHz po celou dobu: %s' % ('ANO' if ok else 'NE  <== FAIL'))
    print('prirustek CRC err: %d (%.1f/h, reference 12,5 MHz %.1f/h)%s' % (dcrc, dcrc / h if h else 0, REF_CRC_PER_H,
          '  <== HORSI' if h and dcrc / h > REF_CRC_PER_H * 2 + 1 else ''))
    print('prirustek SEQ der: %d' % ddir)
    if sys_:
        print('sigma_y@1s [e-15]: min %d, posledni %d' % (min(sys_), sys_[-1]))


if __name__ == '__main__':
    main()
