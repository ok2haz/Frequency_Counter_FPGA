#!/usr/bin/env python3
"""Nahradni metriky bitstreamu v3 z yosysu (2026-10-24): jen RELATIVNI ukazatel mezi base/ a upravenou verzi -- NENI
to CLS ani casovani Gowinu (GowinSynthesis slucuje registry, mapuje ALU54D podle syn_dspstyle; yosys ne).

  python3 tools/yosys_stat.py <koren modulu> [--deep] [--json <soubor>] [--top N]
  python3 tools/yosys_stat.py cmp <koren A> <koren B> [--deep]        (tabulka A / B / rozdil)
  python3 tools/yosys_stat.py hier <koren A> [<koren B>] [--deep]      (po modulech bez zplosteni; jen zmenene radky)

Vypise: LUT1..LUT4, ALU, MUX2_*, FF podle typu, BSRAM, DSP, ridici sady (unikatni hodiny / povoleni / reset-set) a FF
v nich, logickou hloubku (nejdelsi kombinacni cesta v urovnich LUT, MUX2_* = 1 uroven; ALU v ceste zvlast) celkem
a pro koncove body z tabulky kap. 1 zadani (registr -> registr), a N siti s nejvetsim vetvenim."""
import collections, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LUTS = {'LUT1', 'LUT2', 'LUT3', 'LUT4'}
MUXES = {'MUX2_LUT5', 'MUX2_LUT6', 'MUX2_LUT7', 'MUX2_LUT8', 'MUX2'}
COMB = LUTS | MUXES | {'ALU'}
FF_PREFIX = 'DFF'
BRAM = {'DPB', 'DPX9B', 'SDPB', 'SDPX9B', 'SP', 'SPX9', 'pROM', 'pROMX9', 'RAM16SDP4', 'RAM16SDP2', 'RAM16SDP1', 'RAM16S4'}
DSP = {'ALU54D', 'MULT9X9', 'MULT18X18', 'MULT36X36', 'MULTALU18X18', 'MULTALU36X18', 'MULTADDALU18X18', 'PADD9', 'PADD18'}
OUT_PINS = {'Q', 'F', 'O', 'SUM', 'COUT', 'DO', 'DOA', 'DOB', 'DOUT', 'Q0', 'Q1', 'Q2', 'Q3', 'Q4', 'Q5', 'Q6', 'Q7',
            'CLKOUT', 'LOCK', 'CLKOUTP', 'CLKOUTD', 'CLKOUTD3', 'OSCOUT', 'CASO', 'SO'}
# koncove body z tabulky kap. 1 zadani: (popis, zdroj regex | None = libovolny registr, cil regex, piny cile)
ENDPOINTS = [
    ('u_pps/win -> u_pps/nofine', r'^u_pps\.win$', r'^(u_pps\.nofine|pps_nofine)$', None),
    ('u_pps/* -> u_pps/nofine', None, r'^(u_pps\.nofine|pps_nofine)$', None),
    ('u_ring/ohA -> u_ring/wa_a', r'^u_ring\.ohA$', r'^u_ring\.wa_a$', None),
    ('* -> u_ring/wa_a', None, r'^u_ring\.wa_a$', None),
    ('u_pps/dd_r -> u_pps/d_l', r'^u_pps\.(dd_r|dd)$', r'^(u_pps\.d_l|pps_d_l)$', None),
    ('u_sa/(soucty) -> u_ring/wa_a', r'^u_sa\.', r'^u_ring\.wa_a$', None),
    ('u_lws/lw_n_q -> u_tdca/lut (DI)', r'^(u_lws\.)?lw_n_qa?$', r'^u_tdca\.lut', None),
    ('u_sa/tvd -> u_sa/t0 (CE)', r'^u_sa\.tvd$', r'^u_sa\.t0$', {'CE'}),
    ('* -> u_sa/t0', None, r'^u_sa\.t0$', None),
    ('u_tdca/cal_valid,busy_ra -> hold_a_s', r'^u_tdca\.(cal_valid|busy_ra)$', r'^hold_a_s$', None),
    ('* -> u_pps/pts_l', None, r'^(u_pps\.pts_l|pps_pts_l)$', None),
    ('* -> u_app/tx_wdata', None, r'^(u_app\.tx_wdata|tx_wdata)$', None),
]


def synth(root, deep, js):
    pre = 'verilog_defines -DDEEP_RAW; ' if deep else ''
    with tempfile.TemporaryDirectory() as td:          # cesty bez mezer (yosys -p je deli)
        shutil.copy(os.path.join(HERE, 'yosys_v3.ys'), os.path.join(td, 'v3.ys'))
        r = subprocess.run(['yosys', '-q', '-p', pre + 'script %s; write_json %s' % (os.path.join(td, 'v3.ys'), os.path.join(td, 'n.json'))],
                           cwd=root, capture_output=True, text=True)
        if r.returncode:
            sys.exit('yosys selhal:\n' + r.stdout[-3000:] + r.stderr[-3000:])
        shutil.copy(os.path.join(td, 'n.json'), js)


def hier(root, deep):
    """LUT/MUX/ALU/FF po modulech (synth_gowin -noflatten): presnejsi lokalizace zmeny nez celkove cislo (abc9 mapuje
    zplosteny navrh globalne a drobna zmena posune i nesouvisejici bunky)."""
    pre = 'verilog_defines -DDEEP_RAW; ' if deep else ''
    with tempfile.TemporaryDirectory() as td:
        ys = open(os.path.join(HERE, 'yosys_v3.ys')).read().replace('-noiopads', '-noiopads -noflatten')
        open(os.path.join(td, 'v3h.ys'), 'w').write(ys)
        r = subprocess.run(['yosys', '-q', '-p', pre + 'script %s; tee -q -o %s stat' % (os.path.join(td, 'v3h.ys'), os.path.join(td, 's.txt'))],
                           cwd=root, capture_output=True, text=True)
        if r.returncode:
            sys.exit('yosys selhal:\n' + r.stderr[-3000:])
        txt = open(os.path.join(td, 's.txt')).read()
    res = collections.OrderedDict()
    for blk in re.split(r'\n=== ', txt)[1:]:
        name = blk.split(' ===')[0].strip()
        name = re.sub(r'^\$paramod\$[0-9a-f]+\\?', 'paramod:', name)
        name = re.sub(r'^\$paramod\\', '', name).split('\\')[0]
        c = {m.group(2): int(m.group(1)) for m in re.finditer(r'^\s+(\d+)\s+(\S+)$', blk, re.M)}
        if name.startswith('design hierarchy'):
            name = 'CELKEM (hierarchie)'
        v = (sum(c.get(k, 0) for k in LUTS), sum(v for k, v in c.items() if k.startswith('MUX2')), c.get('ALU', 0),
             sum(v for k, v in c.items() if k.startswith('DFF')))
        if sum(v):
            res[name] = tuple(a + b for a, b in zip(res.get(name, (0, 0, 0, 0)), v)) if name in res else v
    return res


def analyze(js, topn=20):
    m = json.load(open(js))['modules']['top_v3']
    cells = m['cells']
    name_of = {}
    alias = collections.defaultdict(set)
    for n, v in sorted(m['netnames'].items(), key=lambda kv: (kv[1].get('hide_name', 0), len(kv[0]))):
        for i, b in enumerate(v['bits']):
            if isinstance(b, int):
                alias[b].add(n)
                if b not in name_of:
                    name_of[b] = (n, i, len(v['bits']))
    cnt = collections.Counter(c['type'] for c in cells.values())
    driver = {}
    sinks = collections.Counter()
    for cn, c in cells.items():
        pd = c.get('port_directions', {})
        for p, bits in c['connections'].items():
            out = pd.get(p, 'output' if p in OUT_PINS else 'input') == 'output'
            for b in bits:
                if not isinstance(b, int):
                    continue
                if out:
                    driver[b] = (cn, p)
                else:
                    sinks[b] += 1
    # ridici sady
    cs = collections.Counter()
    def cb(c, p):
        b = c['connections'].get(p)
        return None if b is None else tuple(b)
    for c in cells.values():
        t = c['type']
        if not t.startswith(FF_PREFIX):
            continue
        sr = next((p for p in ('RESET', 'SET', 'CLEAR', 'PRESET') if p in c['connections']), None)
        cs[(t.startswith('DFFN'), cb(c, 'CLK'), cb(c, 'CE'), sr, cb(c, sr) if sr else None)] += 1
    # hloubka: DP po kombinacnich bunkach (LUT/MUX = 1 uroven, ALU zvlast); zdroje = vystupy FF/BSRAM/DSP/primitiv/vstupy.
    # Smycka kruhoveho oscilatoru (LUT1/LUT2) = zpetna hrana, bere se jako zdroj. Mezipamet zvlast pro kazdy filtr zdroje.
    def ins_of(c):
        pd = c.get('port_directions', {})
        return [bb for pp, bits in c['connections'].items() if pd.get(pp, 'output' if pp in OUT_PINS else 'input') == 'input'
                for bb in bits if isinstance(bb, int)]
    caches = {}
    def depth(b, src_ok, key):
        res = caches.setdefault(key, {})
        if b in res:
            return res[b]
        work = [b]; onstack = set()
        while work:
            x = work[-1]
            if x in res:
                work.pop(); continue
            d = driver.get(x)
            if d is None or cells[d[0]]['type'] not in COMB:
                if src_ok is None:
                    res[x] = (0, 0)
                else:
                    res[x] = (0, 0) if any(src_ok(n) for n in alias.get(x, ())) else None
                work.pop(); continue
            c = cells[d[0]]; ins = ins_of(c)
            if x not in onstack:
                onstack.add(x)
                pend = [i for i in ins if i not in res and i not in onstack]
                if pend:
                    work.extend(pend); continue
            best = None
            for i in ins:
                r = res.get(i)
                if r is not None and (best is None or r > best):
                    best = r
            t = c['type']
            res[x] = None if best is None else (best[0] + (1 if (t in LUTS or t in MUXES) else 0), best[1] + (1 if t == 'ALU' else 0))
            work.pop(); onstack.discard(x)
        return res[b]
    # celkova nejdelsi cesta do FF/BSRAM (ltp -noff obdoba)
    worst = (0, 0); worst_ep = ''
    ep_pins = []
    for cn, c in cells.items():
        t = c['type']
        if not (t.startswith(FF_PREFIX) or t in BRAM or t in DSP):
            continue
        q = c['connections'].get('Q') or c['connections'].get('DO') or c['connections'].get('DOA') or []
        qn = tuple(sorted(alias.get(q[0], ()))) if q and isinstance(q[0], int) else ()
        qn = qn + (cn,)
        for p, bits in c['connections'].items():
            if c.get('port_directions', {}).get(p, 'output' if p in OUT_PINS else 'input') != 'input' or p in ('CLK', 'CLKA', 'CLKB'):
                continue
            for b in bits:
                if isinstance(b, int):
                    ep_pins.append((cn, qn, p, b))
    for cn, qn, p, b in ep_pins:
        d = depth(b, None, '*')
        if d and d > worst:
            worst = d; worst_ep = '%s.%s' % (qn[0], p)
    eps = []
    for lab, sre, tre, pins in ENDPOINTS:
        tr = re.compile(tre)
        sel = [(cn, qn, p, b) for cn, qn, p, b in ep_pins if any(tr.search(n) for n in qn) and (pins is None or p in pins)]
        best = None
        if sel:
            sr = re.compile(sre) if sre else None
            ok = (lambda n: bool(sr.search(n))) if sr else None
            for cn, qn, p, b in sel:
                d = depth(b, ok, lab)
                if d is not None and (best is None or d > best[0]):
                    best = (d, p)
        eps.append((lab, len(sel), best))
    fan = sorted(((n, b) for b, n in sinks.items()), reverse=True)[:topn]
    fan = [(n, '%s[%d]' % (name_of[b][0], name_of[b][1]) if b in name_of and name_of[b][2] > 1 else name_of.get(b, ('?',))[0]) for n, b in fan]
    ffs = {t: n for t, n in cnt.items() if t.startswith(FF_PREFIX)}
    return dict(cnt=cnt, ffs=ffs, cs=cs, worst=worst, worst_ep=worst_ep, eps=eps, fan=fan)


def summary(a):
    c = a['cnt']
    lut = sum(c[t] for t in LUTS); mux = sum(c[t] for t in MUXES); ff = sum(a['ffs'].values())
    big = sorted(a['cs'].values(), reverse=True)
    return collections.OrderedDict([
        ('LUT1', c['LUT1']), ('LUT2', c['LUT2']), ('LUT3', c['LUT3']), ('LUT4', c['LUT4']), ('LUT celkem', lut),
        ('MUX2_*', mux), ('ALU', c['ALU']), ('FF celkem', ff)] +
        [(t, n) for t, n in sorted(a['ffs'].items())] +
        [('BSRAM', sum(c[t] for t in BRAM)), ('DSP', sum(c[t] for t in DSP)), ('ridici sady', len(a['cs'])),
         ('ridici sady s 1 FF', sum(1 for v in big if v == 1)), ('hloubka max [LUT]', a['worst'][0]),
         ('ALU v nejhlubsi ceste', a['worst'][1])])


def show(a, topn):
    for k, v in summary(a).items():
        print('%-28s %7s' % (k, v))
    print('nejhlubsi koncovy bod: %s' % a['worst_ep'])
    print('\nkoncove body (LUT urovni / ALU v ceste, pin; pocet bitu cile):')
    for lab, n, best in a['eps']:
        print('  %-40s %s' % (lab, ('%2d / %3d  %-6s (%d)' % (best[0][0], best[0][1], best[1], n)) if best else '-- (%d)' % n))
    print('\nridici sady s nejvice FF:')
    for k, v in a['cs'].most_common(8):
        print('  %4d FF  CE=%s SR=%s' % (v, 'ano' if k[2] else 'ne', k[3] or '-'))
    print('\n%d siti s nejvetsim vetvenim:' % topn)
    for n, nm in a['fan']:
        print('  %5d  %s' % (n, nm))


def main():
    args = sys.argv[1:]
    deep = '--deep' in args
    args = [x for x in args if x != '--deep']
    topn = 20
    if '--top' in args:
        i = args.index('--top'); topn = int(args[i + 1]); del args[i:i + 2]
    js = None
    if '--json' in args:
        i = args.index('--json'); js = args[i + 1]; del args[i:i + 2]
    if args and args[0] == 'hier':
        ra, rb = args[1], args[2] if len(args) > 2 else None
        ha = hier(os.path.abspath(ra), deep); hb = hier(os.path.abspath(rb), deep) if rb else ha
        print('%-36s %22s %22s %24s' % ('modul (bez zplosteni)', 'A: LUT/MUX/ALU/FF', 'B: LUT/MUX/ALU/FF', 'B-A'))
        for k in list(ha) + [k for k in hb if k not in ha]:
            a = ha.get(k, (0, 0, 0, 0)); b = hb.get(k, (0, 0, 0, 0))
            if a != b or not rb:
                print('%-36s %22s %22s %24s' % (k[:36], '%d/%d/%d/%d' % a, '%d/%d/%d/%d' % b, '%+d/%+d/%+d/%+d' % tuple(y - x for x, y in zip(a, b))))
        return
    if args and args[0] == 'cmp':
        ra, rb = args[1], args[2]
        res = []
        for r in (ra, rb):
            with tempfile.TemporaryDirectory() as td:
                j = os.path.join(td, 'n.json'); synth(os.path.abspath(r), deep, j); res.append(analyze(j, topn))
        sa, sb = summary(res[0]), summary(res[1])
        print('%-28s %8s %8s %8s' % ('', 'A', 'B', 'B-A'))
        for k in list(sa) + [k for k in sb if k not in sa]:
            va, vb = sa.get(k, 0), sb.get(k, 0)
            print('%-28s %8s %8s %+8d' % (k, va, vb, vb - va))
        print('\nkoncove body (LUT / ALU)   A | B:')
        for (lab, na, ba), (_, nb, bb) in zip(res[0]['eps'], res[1]['eps']):
            f = lambda b: '%2d/%3d' % b[0] if b else '  --  '
            print('  %-40s %s | %s' % (lab, f(ba), f(bb)))
        return
    root = os.path.abspath(args[0])
    if js is None:
        td = tempfile.mkdtemp(); js = os.path.join(td, 'n.json')
    synth(root, deep, js)
    show(analyze(js, topn), topn)


if __name__ == '__main__':
    main()
