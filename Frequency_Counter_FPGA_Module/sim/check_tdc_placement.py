"""Kontrola FYZICKEHO rozmisteni retezu TDC po P&R (doplnek check_tdc_netlist.py, ktery vidi jen netlist).

Vstup: textovy timing report Gowin (impl/pnr/Counter_FPGA.tr; build.tcl: -gen_text_timing_rpt 1)
s radky `report_timing -from [get_regs {u_tdc?/u_chain/q_r*}]` z timing.sdc. Kazdy vzorkovaci FF q_r[k]
sedi ve slotu sveho ALU (vystup SUM -> D uvnitr CLS), takze jeho poloha = poloha tapu k.

Selze (navratovy kod 1), kdyz:
  (a) nektery kanal nema vsech NTAP tapu v reportu,
  (b) tapy nejsou v JEDNOM radku a souvisle (kazdy dalsi tap = dalsi slot, pres hranici dlazdice C -> C+1),
  (c) tap 0 nelezi v ocekavanem slotu (C2 slot 1; slot 0 je hlava retezu),
  (d) kanaly nemaji shodne sloupce / nelezi v ocekavanych radcich.
Slot [cls][A|B] -> poradi ALU v dlazdici = cls*2 + (B ? 1 : 0).

Pouziti: python sim/check_tdc_placement.py impl/pnr/Counter_FPGA.tr [--ntap 268] [--rows u_tdca=26,u_tdcb=27]
"""
import argparse
import re
import sys

ap = argparse.ArgumentParser()
ap.add_argument('tr')
ap.add_argument('--ntap', type=int, default=268)
ap.add_argument('--rows', default='u_tdca=26,u_tdcb=27')
ap.add_argument('--first-col', type=int, default=2)
a = ap.parse_args()

want_rows = dict((k, int(v)) for k, v in (x.split('=') for x in a.rows.split(',')))
txt = open(a.tr, encoding='utf-8', errors='replace').read()
loc = {}
for m in re.finditer(r'R(\d+)C(\d+)\[(\d)\]\[([AB])\]\s+(u_tdc\w)/u_chain/q_r_(\d+)_s\d+/CLK', txt):
    r, c, cls, ab, ch, k = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5), int(m.group(6))
    loc[(ch, k)] = (r, c, cls * 2 + (1 if ab == 'B' else 0))

fail = []
cols = {}
for ch, row in sorted(want_rows.items()):
    ks = [k for (c, k) in loc if c == ch and k < a.ntap]
    if len(ks) != a.ntap:
        fail.append('%s: v reportu %d z %d tapu (chybi report_timing v timing.sdc nebo -gen_text_timing_rpt?)'
                    % (ch, len(ks), a.ntap))
        continue
    start = a.first_col * 6 + 1                     # tap 0 = slot 1 prvni dlazdice (slot 0 = hlava)
    bad = 0
    for k in range(a.ntap):
        r, c, s = loc[(ch, k)]
        if r != row or c * 6 + s != start + k:
            if bad < 5:
                fail.append('%s: tap %d v R%dC%d slot %d, ocekavano R%dC%d slot %d'
                            % (ch, k, r, c, s, row, (start + k) // 6, (start + k) % 6))
            bad += 1
    if bad:
        fail.append('%s: %d tapu mimo souvisly radek' % (ch, bad))
    r0, c0, s0 = loc[(ch, 0)]
    r1, c1, s1 = loc[(ch, a.ntap - 1)]
    cols[ch] = (c0, s0, c1, s1)
    print('%s: tapy 0..%d  R%d C%d[%d] .. C%d[%d]%s' % (ch, a.ntap - 1, r0, c0, s0, c1, s1, '' if not bad else '  <== VADA'))

if len(set(cols.values())) > 1:
    fail.append('kanaly nemaji shodne sloupce: %s' % cols)

if fail:
    print('FAIL: rozmisteni TDC')
    for f in fail:
        print('  ' + f)
    sys.exit(1)
print('PASS: kazdy retez TDC v jednom radku, souvisle, shodne sloupce')
