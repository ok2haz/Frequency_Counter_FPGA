#!/usr/bin/env python3
"""state_csv.py - loguje stav pristroje (GET /api/state) do CSV: teploty, napeti, GPSDO, uptime, citace chyb. Jen cteni, bez portu UART.

Pouziti:
  python tools/state_csv.py --out noc_state.csv --every 10            # navaze na existujici CSV, Ctrl+C = konec
  python tools/state_csv.py --ip 10.0.0.106 --out noc_state.csv --every 10 --hours 10

Pro surova mereni kmitoctu (kazde mereni): tools/raw_fetch.py --out noc_raw.csv --follow --every 10
Pri restartu pristroje (uptime_s klesne) se do CSV zapise radek s event=RESTART; vypadek spojeni = event=NO_CONN.
"""
import argparse, csv, datetime, json, os, time, urllib.error, urllib.request

COLS = ['t_iso', 'event', 'uptime_s', 'freq_hz', 'freq16_hz', 'channel', 'seq_meas', 'gate_ns', 'signal_lost', 'spi_ok',
        'sy1e15', 'ref_ppb_x100', 'temp_ocxo_c', 'temp_board_c', 'temp_mcu_c', 'temp_fpga_c', 'vc_mv', 'v12_mv', 'v5_mv', 'vbat_mv',
        'gps_fix', 'nsat', 'gpsdo_state', 'gpsdo_locked', 'gpsdo_y_ppt', 'gpsdo_x_ns', 'gpsdo_xr_ns', 'gpsdo_code', 'gpsdo_t_s',
        'pps_ok', 'pps_rej', 'dac_code', 'sys_level', 'si5356_status', 'tdc_rez_a', 'tdc_rez_b', 'tdc_ovf_a', 'tdc_ovf_b']


def get(ip, to=5.0):
    with urllib.request.urlopen('http://%s/api/state' % ip, timeout=to) as r:
        return json.loads(r.read().decode('utf-8', 'replace'))


def row(d, event=''):
    g = d.get('gpsdo') or {}
    gp = d.get('gps') or {}
    dac = d.get('dac') or {}
    return {
        't_iso': datetime.datetime.now().isoformat(timespec='seconds'), 'event': event,
        'uptime_s': d.get('uptime_s'), 'freq_hz': d.get('freq_hz'), 'freq16_hz': d.get('freq16_hz'),
        'channel': d.get('channel'), 'seq_meas': d.get('seq_meas'), 'gate_ns': d.get('gate_ns'),
        'signal_lost': d.get('signal_lost'), 'spi_ok': d.get('spi_ok'), 'sy1e15': d.get('sy1e15'),
        'ref_ppb_x100': d.get('ref_ppb_x100'), 'temp_ocxo_c': d.get('temp_ocxo_c'), 'temp_board_c': d.get('temp_board_c'),
        'temp_mcu_c': d.get('temp_mcu_c'), 'temp_fpga_c': d.get('temp_fpga_c'), 'vc_mv': d.get('vc_mv'),
        'v12_mv': d.get('v12_mv'), 'v5_mv': d.get('v5_mv'), 'vbat_mv': d.get('vbat_mv'),
        'gps_fix': gp.get('fix_mode') if isinstance(gp, dict) else None, 'nsat': d.get('nsat'),
        'gpsdo_state': g.get('state'), 'gpsdo_locked': g.get('locked'), 'gpsdo_y_ppt': g.get('y_ppt'),
        'gpsdo_x_ns': g.get('x_ns'), 'gpsdo_xr_ns': g.get('xr_ns'), 'gpsdo_code': g.get('code'), 'gpsdo_t_s': g.get('t_s'),
        'pps_ok': g.get('pps_ok'), 'pps_rej': g.get('pps_rej'),
        'dac_code': dac.get('code') if isinstance(dac, dict) else None, 'sys_level': d.get('sys_level'),
        'si5356_status': d.get('si5356_status'), 'tdc_rez_a': d.get('tdc_rez_a'), 'tdc_rez_b': d.get('tdc_rez_b'),
        'tdc_ovf_a': d.get('tdc_ovf_a'), 'tdc_ovf_b': d.get('tdc_ovf_b'),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ip', default='10.0.0.106')
    ap.add_argument('--out', required=True)
    ap.add_argument('--every', type=float, default=10.0)
    ap.add_argument('--hours', type=float, default=0.0, help='0 = bez limitu (Ctrl+C)')
    a = ap.parse_args()
    new = not os.path.exists(a.out) or os.path.getsize(a.out) == 0
    t_end = time.time() + a.hours * 3600.0 if a.hours > 0 else None
    prev_up, bad = None, 0
    with open(a.out, 'a', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=COLS)
        if new:
            w.writeheader()
        try:
            while t_end is None or time.time() < t_end:
                t0 = time.time()
                try:
                    d = get(a.ip)
                    ev = ''
                    if prev_up is not None and d.get('uptime_s') is not None and d['uptime_s'] < prev_up:
                        ev = 'RESTART'
                    if bad:
                        ev = (ev + ' ' if ev else '') + 'ZPET_PO_%d_VYPADCICH' % bad
                    bad = 0
                    prev_up = d.get('uptime_s', prev_up)
                    w.writerow(row(d, ev))
                except (urllib.error.URLError, TimeoutError, ConnectionError, ValueError, OSError):
                    bad += 1
                    w.writerow({'t_iso': datetime.datetime.now().isoformat(timespec='seconds'), 'event': 'NO_CONN'})
                f.flush()
                dt = a.every - (time.time() - t0)
                if dt > 0:
                    time.sleep(dt)
        except KeyboardInterrupt:
            pass
    print('hotovo:', a.out)


if __name__ == '__main__':
    main()
