#!/usr/bin/env python3
"""Vyhodnoceni sweepu voleb P&R (tools/pnr_sweep.tcl): tabulka behu podle rezervy, kontrola rozmisteni TDC a mapa
nejlepsiho behu (tools/floorplan_svg.py). Audit rozlozeni 2026-10-23 (docs/audit/2026-10-23_rozlozeni-fpga.md).

Beh se pocita jako pouzitelny jen kdyz: setup i hold bez poruseni, rezerva >= 0 (po nejistote 0,9 ns z SDC), CLS <= 85 %,
rozmisteni retezu TDC PASS (sim/check_tdc_placement.py). Poradi: pouzitelne pred ostatnimi, pak podle setup rezervy.
Volby nejlepsiho behu se do build.tcl / build_v3.tcl prenaseji RUCNE a bitstream se pak sestavi a vyda pres
tools/fpga_release.py (sweep sam bitstream nevydava). Rezerva z jednoho behu je i nahoda rozmisteni (FW 0x0421: stejna
volba 3 z 8 behu bez poruseni) -> rozhodovat podle vic behu (pnr_sweep.tcl s jinym BLD_TIME = jiny vstup P&R).

Pouziti: python tools/pnr_sweep_report.py v3/impl/sweep [--ntap 268] [--svg mapa_nejlepsi.svg] [--md]
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from fpga_release import MAX_CLS_PCT, parse_rpt, parse_tr, read   # noqa: E402  (tytez parsery jako brana vydani)


def run_one(d, ntap):
    tr, rpt = os.path.join(d, 'Counter_FPGA.tr'), os.path.join(d, 'Counter_FPGA.rpt.txt')
    r = dict(tag=os.path.basename(d), ok=False, note='')
    m = re.search(r'pnr_s (\d+) rc (\d+)', read(os.path.join(d, 'sweep.txt'))) if os.path.exists(os.path.join(d, 'sweep.txt')) else None
    r['pnr_s'] = int(m.group(1)) if m else None
    if not (os.path.exists(tr) and os.path.exists(rpt)):
        r['note'] = 'chybi .tr/.rpt.txt (P&R selhal?)'
        return r
    r.update(parse_tr(read(tr)))
    r.update(parse_rpt(read(rpt)))
    pl = subprocess.run([sys.executable, os.path.join(ROOT, 'sim', 'check_tdc_placement.py'), tr, '--ntap', str(ntap)],
                        capture_output=True, text=True)
    last = (pl.stdout.strip().splitlines() or ['?'])[-1]
    r['tdc'] = pl.returncode == 0 and last.startswith('PASS')
    bad = []
    if r['setup_viol'] != 0: bad.append('setup %s' % r['setup_viol'])
    if r['hold_viol'] != 0: bad.append('hold %s' % r['hold_viol'])
    if r['setup_ws'] is None or r['setup_ws'] < 0: bad.append('rezerva')
    if r['cls'] is None or r['cls'] > MAX_CLS_PCT: bad.append('CLS')
    if not r['tdc']: bad.append('TDC: ' + last[:60])
    r['ok'] = not bad
    r['note'] = ', '.join(bad)
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('sweep')
    ap.add_argument('--ntap', type=int, default=268)
    ap.add_argument('--svg', help='mapa nejlepsiho behu (vychozi <sweep>/nejlepsi.svg)')
    ap.add_argument('--md', action='store_true', help='tabulka v Markdownu (do auditu)')
    a = ap.parse_args()
    dirs = sorted(os.path.join(a.sweep, d) for d in os.listdir(a.sweep) if os.path.isdir(os.path.join(a.sweep, d)))
    if not dirs:
        sys.exit('%s: zadne behy (spustil se pnr_sweep.tcl?)' % a.sweep)
    rows = [run_one(d, a.ntap) for d in dirs]
    rows.sort(key=lambda r: (not r['ok'], -(r.get('setup_ws') if r.get('setup_ws') is not None else -99)))
    f = lambda v, fmt: '-' if v is None else fmt % v
    if a.md:
        print('| beh | setup rezerva [ns] | hold [ns] | Fmax [MHz] | CLS | reg | TDC | P&R [s] | pouzitelny | nejhorsi cesta |')
        print('|---|---|---|---|---|---|---|---|---|---|')
        for r in rows:
            print('| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (
                r['tag'], f(r.get('setup_ws'), '%+.3f'), f(r.get('hold_ws'), '%+.3f'), f(r.get('fmax'), '%.2f'), f(r.get('cls'), '%d %%'),
                f(r.get('reg'), '%d %%'), 'PASS' if r.get('tdc') else 'FAIL', f(r['pnr_s'], '%d'), 'ano' if r['ok'] else 'ne: ' + r['note'],
                (r.get('setup_path') or '-').replace('|', '/')))
    else:
        print('%-10s %8s %8s %7s %4s %4s %4s %5s  %s' % ('beh', 'setup', 'hold', 'Fmax', 'CLS', 'reg', 'TDC', 'P&R', 'nejhorsi cesta / proc nepouzitelny'))
        for r in rows:
            print('%-10s %8s %8s %7s %4s %4s %4s %5s  %s' % (
                r['tag'], f(r.get('setup_ws'), '%+.3f'), f(r.get('hold_ws'), '%+.3f'), f(r.get('fmax'), '%.2f'), f(r.get('cls'), '%d'),
                f(r.get('reg'), '%d'), 'ok' if r.get('tdc') else 'FAIL', f(r['pnr_s'], '%d'),
                (r.get('setup_path') or '') if r['ok'] else 'NEPOUZITELNY: ' + r['note']))
    best = next((r for r in rows if r['ok']), None)
    if best is None:
        print('\nzadny beh nesplnil podminky')
        return 1
    svg = a.svg or os.path.join(a.sweep, 'nejlepsi.svg')
    fp = subprocess.run([sys.executable, os.path.join(HERE, 'floorplan_svg.py'), os.path.join(a.sweep, best['tag'], 'Counter_FPGA.tr'), svg,
                         '--title', 'GW1NR-9, sweep P&R, nejlepsi beh %s (%+.3f ns, CLS %s %%)' % (best['tag'], best['setup_ws'], best['cls'])],
                        capture_output=True, text=True)
    if not a.md:
        print('\nnejlepsi: %s, rezerva %+.3f ns, CLS %s %% -> volby do build*.tcl rucne, pak tools/fpga_release.py --build' %
              (best['tag'], best['setup_ws'], best['cls']))
        print(fp.stdout.rstrip() or fp.stderr.rstrip())
    return 0


if __name__ == '__main__':
    sys.exit(main())
