#!/usr/bin/env python3
"""start_collect.py -- sber startu pristroje pro TODO #337b (falesna 1. hrana 1PPS po startu FPGA) pres UART `status`.

Po kazdem restartu (power-cyklu) pocka, az uptime dosahne --min-up s, jednou precte `status` a zapise radek do CSV:
verze, uptime, prvni videna hrana 1PPS (PSEQ, cas od startu FPGA, dalsi hrana), interval PSEQ 1->2 proti mrizce 100 ms
(zmereno / falesna / odchylka us), SEQ diry, CRC err. Jen cteni; mezi radky jen `status` jednou za --poll s (SPI mutex).

Pouziti (z adresare Frequency_Counter_STM32H757; zadny jiny terminal nesmi drzet port):
    python tools/start_collect.py --port COM10 --out starty.csv
Pak pristroj vypinat/zapinat; Ctrl+C = konec a souhrn. Radek event=FALESNA znamena odchylku nad 1000 us od mrizky 100 ms.
"""
import argparse, csv, datetime, os, re, subprocess, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UARTQ = os.path.join(ROOT, 'tools', 'uartq.ps1')
COLS = ['t_iso', 'event', 'fw', 'uptime_s', 'pseq_first', 'first_ms', 'next_ms', 'pair_n', 'pair_bad', 'dev_us', 'seq_diry', 'crc_err']


def uart(port, cmds, idle=2000, mx=25000, timeout=60):
    cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', UARTQ, '-Port', port,
           '-Idle', str(idle), '-Max', str(mx), '-Cmds', ';'.join(cmds)]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, encoding='utf-8', errors='replace')
        return r.stdout
    except subprocess.TimeoutExpired:
        return ''


def parse(txt):
    d = {}
    m = re.search(r'RUNNING gpsdo-ui (v[\d.]+)\s+uptime\s+(\d+)s', txt)
    if not m:
        return None
    d['fw'], d['uptime_s'] = m.group(1), int(m.group(2))
    m = re.search(r'PSEQ (\d+), ([\d.]+) ms od startu FPGA, dalsi za ([\d.]+) ms', txt)
    if m:
        d['pseq_first'], d['first_ms'], d['next_ms'] = m.group(1), m.group(2), m.group(3)
    m = re.search(r'zmereno (\d+), falesna 1\. hrana (\d+) \| odchylka od mrizky 100 ms ([+-]?\d+) us', txt)
    if m:
        d['pair_n'], d['pair_bad'], d['dev_us'] = m.group(1), m.group(2), m.group(3)
    m = re.search(r'SEQ FPGA: diry (\d+)', txt)
    if m:
        d['seq_diry'] = m.group(1)
    m = re.search(r'CRC err (\d+)', txt)
    if m:
        d['crc_err'] = m.group(1)
    d['_false'] = 'FALESNA HRANA' in txt
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM10')
    ap.add_argument('--out', required=True)
    ap.add_argument('--min-up', type=int, default=60, help='kolik s po startu cekat nez se status precte')
    ap.add_argument('--poll', type=float, default=10.0)
    a = ap.parse_args()
    new = not os.path.exists(a.out) or os.path.getsize(a.out) == 0
    prev, captured, n, nfalse = None, False, 0, 0
    with open(a.out, 'a', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=COLS)
        if new:
            w.writeheader()
        try:
            while True:
                t0 = time.time()
                d = parse(uart(a.port, ['status']))
                if d is not None:
                    up = d['uptime_s']
                    if prev is None or up < prev:
                        captured = False       # novy start
                        if prev is not None:
                            print('restart detekovan (uptime %d -> %d)' % (prev, up))
                    prev = up
                    if not captured and up >= a.min_up:
                        row = {k: d.get(k, '') for k in COLS}
                        row['t_iso'] = datetime.datetime.now().isoformat(timespec='seconds')
                        row['event'] = 'FALESNA' if d['_false'] else ''
                        w.writerow(row)
                        f.flush()
                        captured = True
                        n += 1
                        nfalse += 1 if d['_false'] else 0
                        print('start %d: uptime %d s, PSEQ %s, 1. hrana %s ms, interval 1->2: n=%s bad=%s dev=%s us%s'
                              % (n, up, d.get('pseq_first', '?'), d.get('first_ms', '?'), d.get('pair_n', '-'), d.get('pair_bad', '-'),
                                 d.get('dev_us', '-'), '  <== FALESNA' if d['_false'] else ''))
                dt = a.poll - (time.time() - t0)
                if dt > 0:
                    time.sleep(dt)
        except KeyboardInterrupt:
            pass
    print('hotovo: %d startu, falesnych %d, soubor %s' % (n, nfalse, a.out))


if __name__ == '__main__':
    main()
