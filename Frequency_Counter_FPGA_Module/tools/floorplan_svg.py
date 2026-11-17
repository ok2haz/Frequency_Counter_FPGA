#!/usr/bin/env python3
"""Mapa rozmisteni GW1NR-9 z textoveho timing reportu Gowin (impl/pnr/Counter_FPGA.tr, build.tcl: -gen_text_timing_rpt 1).

Report nese polohu (R, C) kazde bunky na NAHLASENYCH cestach: 50 nejhorsich cest navrhu a cesty ze vzorkovacich FF obou
retezu TDC (timing.sdc / timing_v3.sdc, report_timing). Neni to tedy uplna mapa obsazeni (.db je binarni), ale ukazuje
presne to, co rozhoduje o rezerve: kde lezi zdroje a cile nejhorsich cest a jak daleko vedou. Audit 2026-10-23
(docs/audit/2026-10-23_rozlozeni-fpga.md).

Vystup: SVG (mrizka R1..R28 x C1..C46, bunky podle modulu, nejhorsi cesty jako lomene cary podle rezervy, zdroj s nejvic
cestami zvyrazneny) a souhrn na stdout (rezerva, podil vedeni, zdroje s nejvic cestami, rozpeti modulu).
Jen standardni knihovna Pythonu; bezi na Windows i macOS.

Pouziti: python tools/floorplan_svg.py impl/pnr/Counter_FPGA.tr mapa.svg [--paths 50] [--title "FW 0x0500"]
"""
import argparse
import re
import sys
from collections import Counter, defaultdict
from xml.sax.saxutils import escape as esc

# Radky bez CFU (GW1NR-9, poloha z reportu 0x0427): BSRAM R10 a R28, DSP R19. Retezy TDC: R26 (A), R27 (B).
ROWS, COLS = 28, 46
SPECIAL = {10: 'BSRAM', 19: 'DSP', 28: 'BSRAM'}
GROUPS = [  # (jmeno, barva, test na hierarchii bunky)
    ('TDC A', '#7F77DD', lambda h: h.startswith('u_tdca')),
    ('TDC B', '#1D9E75', lambda h: h.startswith('u_tdcb')),
    ('mereni (okna/segmenty/kruh)', '#D85A30', lambda h: re.match(r'u_(wr|s[ab]$|ring|rg|win|regr|v3)', h) is not None),
    ('ostatni (aplikace, PHY, top)', '#888780', lambda h: True),
]
ROW_RE = re.compile(r'^\s*([\d.]+)\s+(-?[\d.]+)\s+(t\w+)\s+(\w\w)?\s*(\d+)?\s+(R\d+C\d+\[\d\]\[[AB]\]|IOB\d+\[[AB]\]|R\d+C\d+)?\s+(\S+)\s*$')


def parse(path):
    txt = open(path, encoding='utf-8', errors='replace').read().replace('\r\n', '\n')
    secs = [(m.start(), m.group(1)) for m in re.finditer(r'Report Command:(report_timing[^\n]*)', txt)]
    paths, cells = [], {}
    for b in re.finditer(r'Path Summary:\n(.*?)(?=\n\s*Path\d+\s*\n|\Z)', txt, re.S):
        body = b.group(1)
        sec = [c for p, c in secs if p < b.start()]
        sec = sec[-1] if sec else ''
        if not sec.startswith('report_timing -setup') or 'Data Arrival Path:' not in body:
            continue
        g = lambda k: (re.search(k + r'\s*:\s*(\S+)', body) or [None, None])[1]
        try:
            slack = float(g('Slack'))
        except (TypeError, ValueError):
            continue
        arr = body.split('Data Arrival Path:')[1].split('Data Required Path:')[0]
        steps = []
        for line in arr.split('\n'):
            m = ROW_RE.match(line)
            if m:
                at, dl, ty, rf, fo, lc, node = m.groups()
                steps.append(dict(d=float(dl), t=ty, fo=int(fo) if fo else 0, loc=lc, node=node))
        ck = next((i for i, s in enumerate(steps) if s['t'] == 'tINS' and 'ibuf' in s['node']), -1)
        data = steps[ck + 2:] if ck >= 0 else steps          # za hodinovym bufferem a vstupem CLK
        logic = sum(s['d'] for s in data if s['t'] in ('tC2Q', 'tINS'))
        net = sum(s['d'] for s in data if s['t'] == 'tNET')
        pts = []
        for s in data:
            m = re.match(r'R(\d+)C(\d+)', s['loc'] or '')
            if not m:
                continue
            rc = (int(m.group(1)), int(m.group(2)))
            cells[s['node'].rsplit('/', 1)[0]] = rc
            if not pts or pts[-1] != rc:
                pts.append(rc)
        kind = 'tdc' if 'u_chain/q_r' in sec else 'top'
        fo = max([s['fo'] for s in data if s['t'] == 'tC2Q'] or [0])
        paths.append(dict(kind=kind, slack=slack, frm=g('From'), to=g('To'), logic=logic, net=net, pts=pts, fo=fo))
    # vytizeni vedeni (report_route_congestion: dlazdice s nejvyssim vytizenim) a nety s nejvetsim fan-out
    cong = [(int(m.group(1)), int(m.group(2)), float(m.group(3)))
            for m in re.finditer(r'^\s*R(\d+)C(\d+)\s+([\d.]+)\s*$', txt.split('report_route_congestion', 1)[-1].split('Report Command', 1)[0], re.M)] \
        if 'report_route_congestion' in txt else []
    fan = re.findall(r'^\s*(\d+)\s+(\S+)\s+(-?[\d.]+)\s+([\d.]+)\s*$', txt.split('report_high_fanout_nets', 1)[-1].split('Report Command', 1)[0], re.M) \
        if 'report_high_fanout_nets' in txt else []
    return paths, cells, cong, fan


def group_of(cell):
    h = cell.split('/')[0] if '/' in cell else 'top'
    return next(i for i, (_, _, f) in enumerate(GROUPS) if f(h))


def svg(paths, cells, cong, npaths, title):
    X0, Y0, CW, CH = 58, 46, 13, 12
    W, H = X0 + COLS * CW + 20, Y0 + ROWS * CH + 112
    cx = lambda c: X0 + (c - 1) * CW + CW / 2
    cy = lambda r: Y0 + (r - 1) * CH + CH / 2
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" font-family="sans-serif">' % (W, H, W, H),
         '<rect width="100%" height="100%" fill="#ffffff"/>',
         '<text x="%d" y="20" font-size="14" font-weight="600" fill="#2c2c2a">%s</text>' % (X0, esc(title))]
    o.append('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="#b4b2a9" stroke-width="0.6"/>' % (X0, Y0, COLS * CW, ROWS * CH))
    for r, name in SPECIAL.items():
        o.append('<rect x="%d" y="%d" width="%d" height="%d" fill="#888780" opacity="0.16"/>' % (X0, Y0 + (r - 1) * CH, COLS * CW, CH))
        o.append('<text x="%d" y="%.1f" font-size="9" fill="#5f5e5a" text-anchor="end" dominant-baseline="central">%s</text>'
                 % (X0 + COLS * CW - 3, cy(r), esc(name)))
    for c in (1, 10, 20, 30, 40, 46):
        o.append('<text x="%.1f" y="%d" font-size="10" fill="#5f5e5a" text-anchor="middle">C%d</text>' % (cx(c), Y0 - 6, c))
    for r in (1, 5, 10, 15, 19, 26, 27, 28):
        o.append('<text x="%d" y="%.1f" font-size="10" fill="#5f5e5a" text-anchor="end" dominant-baseline="central">R%d</text>' % (X0 - 6, cy(r), r))
    bins = Counter((rc, group_of(c)) for c, rc in cells.items())
    for ((r, c), gi), n in sorted(bins.items(), key=lambda x: x[0][1], reverse=True):   # "ostatni" vespod
        if not (1 <= r <= ROWS and 1 <= c <= COLS):
            continue
        o.append('<rect x="%.1f" y="%.1f" width="%d" height="%d" rx="1.5" fill="%s" opacity="%.2f"><title>R%dC%d %s: %d bunek</title></rect>'
                 % (X0 + (c - 1) * CW + 0.5, Y0 + (r - 1) * CH + 0.5, CW - 1, CH - 1, GROUPS[gi][1], min(0.95, 0.25 + 0.12 * n), r, c, esc(GROUPS[gi][0]), n))
    for r, c, v in cong:                                    # vytizene vedeni: ramecek dlazdice
        o.append('<rect x="%.1f" y="%.1f" width="%d" height="%d" fill="none" stroke="#2c2c2a" stroke-width="1.2" stroke-dasharray="2 1.5">'
                 '<title>R%dC%d vedeni vytizeno %.0f %%</title></rect>' % (X0 + (c - 1) * CW + 0.5, Y0 + (r - 1) * CH + 0.5, CW - 1, CH - 1, r, c, 100 * v))
    top = sorted([p for p in paths if p['kind'] == 'top'], key=lambda p: p['slack'])[:npaths]
    seen = set()
    uniq = []
    for p in top:
        k = tuple(p['pts'])
        if k not in seen:
            seen.add(k)
            uniq.append(p)
    for p in reversed(uniq):
        s = p['slack']
        col, w, op = ('#E24B4A', 2.0, 0.95) if s < 0.5 else ('#EF9F27', 1.5, 0.95) if s < 1.0 else ('#5f5e5a', 0.8, 0.55)
        pts = ' '.join('%.1f,%.1f' % (cx(c), cy(r)) for r, c in p['pts'])
        o.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="%.1f" opacity="%.2f" stroke-linejoin="round" stroke-linecap="round">'
                 '<title>%+.3f ns: %s -&gt; %s</title></polyline>' % (pts, col, w, op, s, esc(p['frm']), esc(p['to'])))
    src = Counter(p['frm'] for p in top)
    hot = src.most_common(1)[0] if src else None
    if hot:
        rc = next(p['pts'][0] for p in top if p['frm'] == hot[0] and p['pts'])
        o.append('<circle cx="%.1f" cy="%.1f" r="7" fill="none" stroke="#E24B4A" stroke-width="1.6"/>' % (cx(rc[1]), cy(rc[0])))
    y = Y0 + ROWS * CH + 22                                  # legenda: kazda skupina znacek na vlastnim radku
    x = X0
    for name, col, _ in GROUPS:
        o.append('<rect x="%d" y="%d" width="10" height="10" rx="2" fill="%s"/><text x="%d" y="%d" font-size="11" fill="#444441">%s</text>' % (x, y - 9, col, x + 14, y, esc(name)))
        x += 30 + 6.8 * len(name)
    y += 18
    x = X0
    for lab, col, w in (('rezerva < 0,5 ns', '#E24B4A', 2), ('0,5-1 ns', '#EF9F27', 1.5), ('>= 1 ns', '#5f5e5a', 0.8)):
        o.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="%s"/><text x="%d" y="%d" font-size="11" fill="#444441">%s</text>' % (x, y - 4, x + 18, y - 4, col, w, x + 24, y, esc(lab)))
        x += 44 + 6.8 * len(lab)
    if cong:
        o.append('<rect x="%d" y="%d" width="10" height="10" fill="none" stroke="#2c2c2a" stroke-width="1.2" stroke-dasharray="2 1.5"/>'
                 '<text x="%d" y="%d" font-size="11" fill="#444441">vedeni vytizeno (congestion)</text>' % (x, y - 9, x + 14, y))
    y += 18
    if hot:
        o.append('<circle cx="%d" cy="%d" r="5" fill="none" stroke="#E24B4A" stroke-width="1.5"/><text x="%d" y="%d" font-size="11" fill="#444441">%s: zdroj %d z %d nejhorsich cest</text>'
                 % (X0 + 5, y - 4, X0 + 15, y, esc(hot[0]), hot[1], len(top)))
        y += 18
    if top:
        tl = sum(p['logic'] for p in top)
        tn = sum(p['net'] for p in top)
        o.append('<text x="%d" y="%d" font-size="11" fill="#5f5e5a">Nejhorsi rezerva %+.3f ns: %s -&gt; %s</text>' % (X0, y, top[0]['slack'], esc(top[0]['frm']), esc(top[0]['to'])))
        o.append('<text x="%d" y="%d" font-size="11" fill="#5f5e5a">%d nejhorsich cest: vedeni %.0f %% zpozdeni. Kresli se jen bunky na nahlasenych cestach (%d).</text>'
                 % (X0, y + 16, len(top), 100 * tn / max(tl + tn, 1e-9), len(cells)))
    o.append('</svg>')
    return '\n'.join(o), top, hot


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('tr')
    ap.add_argument('svg')
    ap.add_argument('--paths', type=int, default=50, help='kolik nejhorsich cest kreslit (vychozi 50 = report_timing v SDC)')
    ap.add_argument('--title', default='GW1NR-9: rozmisteni bunek na nejhorsich cestach')
    a = ap.parse_args()
    paths, cells, cong, fan = parse(a.tr)
    if not paths:
        sys.exit('v reportu nejsou cesty setup (chybi -gen_text_timing_rpt 1?)')
    out, top, hot = svg(paths, cells, cong, a.paths, a.title)
    open(a.svg, 'w', encoding='utf-8', newline='\n').write(out)
    tl = sum(p['logic'] for p in top)
    tn = sum(p['net'] for p in top)
    print('cest setup %d (z retezu TDC %d), bunek s polohou %d -> %s' % (len(paths), sum(p['kind'] == 'tdc' for p in paths), len(cells), a.svg))
    print('nejhorsi rezerva %+.3f ns: %s -> %s; %d nejhorsich: vedeni %.0f %% zpozdeni' % (top[0]['slack'], top[0]['frm'], top[0]['to'], len(top), 100 * tn / max(tl + tn, 1e-9)))
    print('zdroje nejhorsich cest (pocet, nejhorsi rezerva, fan-out):')
    for frm, n in Counter(p['frm'] for p in top).most_common(6):
        ps = [p for p in top if p['frm'] == frm]
        print('  %3d  %+.3f  %3d  %s' % (n, min(p['slack'] for p in ps), max(p['fo'] for p in ps), frm))
    if cong:
        print('vytizene vedeni: ' + ', '.join('R%dC%d %.0f %%' % (r, c, 100 * v) for r, c, v in cong))
    if fan:
        print('nety s nejvetsim fan-out (fan-out, nejhorsi rezerva): ' + ', '.join('%s %s (%s)' % (n, f, s) for f, n, s, _ in fan[:8]))
    span = defaultdict(list)
    for c, rc in cells.items():
        span[GROUPS[group_of(c)][0]].append(rc)
    print('rozpeti skupin (bunek, R min..max, C min..max):')
    for name, _, _ in GROUPS:
        l = span.get(name)
        if l:
            print('  %-30s %4d  R%d..%d  C%d..%d' % (name, len(l), min(r for r, _ in l), max(r for r, _ in l), min(c for _, c in l), max(c for _, c in l)))


if __name__ == '__main__':
    main()
