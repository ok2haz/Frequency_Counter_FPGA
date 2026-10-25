"""yosys_abc4.py -- STABILNI nahradni metrika bitstreamu v3 z yosysu (2026-10-25, kolo 2 optimalizace plochy).

Proc vedle yosys_stat.py: synth_gowin mapuje LUT pres abc9 globalne nad zplostelym navrhem a drobna zmena kdekoli
posune vysledek i v nezmenenych modulech (zmereno: dekodery TDC beze zmeny RTL +-250 LUT, app_v3 +-490 LUT i v rezimu
hier). Tady se synth_gowin pusti do map_luts, LUT mapuje klasicke "abc -lut 4" (bez abc9 a sirokych LUT/MUX2) a zbytek
synth_gowin dobehne -> nezmenene moduly davaji mezi variantami stejna cisla, rozdil lezi tam, kde se menil RTL.
Porad to NENI CLS ani casovani Gowinu: yosys neslucuje registry (syn_preserve ignoruje), nezna syn_dspstyle (citace a
soucty s atributem pocita v logice) a primitiva ALU54D / SDPB bere jako cerne skrinky.

Rozpad po modulech RTL: FF / BSRAM / DSP podle radku zdroje (atribut src -> modul), kombinacni bunky (LUT, ALU) podle
modulu koncovych bodu, ktere budi (zpetny kuzel; sdilena bunka se deli rovnym dilem).

Pouziti (z adresare Frequency_Counter_FPGA_Module; yosys a python3 v PATH):
    python tools/yosys_abc4.py stat <koren> [--deep] [--json <soubor>]   # souhrn, ridici sady, hloubka, moduly RTL
    python tools/yosys_abc4.py cmp <koren A> <koren B> [--deep]          # A / B / rozdil + moduly, jen zmenene radky
    python tools/yosys_abc4.py lines <koren> <soubor.v> [N] [--deep]     # jemny rozpad podle radku koncovych FF
<koren> = adresar se src/ (build_id.vh se doplni pevnou identitou, stejnou pro vsechny varianty, jako pnr_seeds.py BASE_T).
"""
import collections, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import yosys_stat                                       # analyze(), summary() -- stejne metriky jako drive

SRC = ['top_v3.v', 'v3_core.v', 'app_v3.v', 'spi_slave_phy_v3.v', 'spi_app.v', 'tdc.v']
BID = "localparam [31:0] BLD_TIME = 32'h6ADC0000;\nlocalparam [31:0] BLD_GIT  = 32'h00000000;\n"
OUTP = {'Q', 'F', 'O', 'SUM', 'COUT', 'DO', 'DOA', 'DOB', 'DOUT', 'Q0', 'Q1', 'Q2', 'Q3', 'Q4', 'Q5', 'Q6', 'Q7',
        'CLKOUT', 'LOCK', 'OSCOUT', 'CASO'}
BRAM = {'SDPB', 'DPB', 'SDPX9B', 'DPX9B', 'SP', 'SPX9'}


def synth(root, deep, js):
    """synth_gowin + abc -lut 4 nad kopii src/ korene; netlist JSON do js."""
    with tempfile.TemporaryDirectory() as td:
        s = os.path.join(td, 'src')
        shutil.copytree(os.path.join(root, 'src'), s, copy_function=shutil.copyfile)
        os.chmod(s, 0o755)
        with open(os.path.join(s, 'build_id.vh'), 'w') as f:
            f.write(BID)
        ys = ('read_verilog -I src ' + ' '.join('src/' + x for x in SRC) + '\n'
              'synth_gowin -top top_v3 -family gw1n -noiopads -run begin:map_luts\n'
              'abc -lut 4\nclean\n'
              'synth_gowin -top top_v3 -family gw1n -noiopads -run map_cells:\n')
        with open(os.path.join(td, 'v.ys'), 'w') as f:
            f.write(ys)
        p = ('verilog_defines -DDEEP_RAW; ' if deep else '') + 'script v.ys; write_json n.json'
        r = subprocess.run(['yosys', '-q', '-p', p], cwd=td, capture_output=True, text=True)
        if r.returncode:
            sys.exit('yosys selhal:\n' + r.stdout[-3000:] + r.stderr[-3000:])
        shutil.copy(os.path.join(td, 'n.json'), js)


def _isout(c, p):
    return c.get('port_directions', {}).get(p, 'output' if p in OUTP else 'input') == 'output'


def _comb(t):
    return t.startswith('LUT') or t.startswith('MUX2') or t == 'ALU'


def _kind(t):
    return ('LUT' if t.startswith('LUT') else 'MUX' if t.startswith('MUX2') else 'ALU' if t == 'ALU' else
            'FF' if t.startswith('DFF') else 'BRAM' if t in BRAM else 'DSP' if t == 'ALU54D' else None)


def _ranges(srcdir):
    """soubor -> [(radek od, radek do, modul)]"""
    rg = {}
    for fn in os.listdir(srcdir):
        if not fn.endswith('.v'):
            continue
        cur = None
        with open(os.path.join(srcdir, fn), encoding='utf-8', errors='replace') as f:
            for i, l in enumerate(f, 1):
                m = re.match(r'\s*module\s+(\w+)', l)
                if m:
                    cur = (m.group(1), i)
                if re.match(r'\s*endmodule', l) and cur:
                    rg.setdefault(fn, []).append((cur[1], i, cur[0]))
                    cur = None
    return rg


def breakdown(js, srcdir, key='module'):
    """key = 'module' (modul RTL) nebo 'line' (soubor:radek + jmeno koncoveho FF)."""
    m = json.load(open(js))['modules']['top_v3']
    cells = m['cells']
    rg = _ranges(srcdir)
    alias = collections.defaultdict(list)
    for n, v in m['netnames'].items():
        for b in v['bits']:
            if isinstance(b, int):
                alias[b].append(n)
    driver = {}
    for cn, c in cells.items():
        for p, bits in c['connections'].items():
            if _isout(c, p):
                for b in bits:
                    if isinstance(b, int):
                        driver[b] = cn

    def tag(c):
        s = c.get('attributes', {}).get('src', '')
        mm = re.search(r'([\w.]+\.v):(\d+)', s)
        if not mm:
            return '(neznamy)'
        if key == 'line':
            q = [b for p, bits in c['connections'].items() if _isout(c, p) for b in bits if isinstance(b, int)]
            nm = re.sub(r'\[\d+\]', '', min(alias.get(q[0], ['?']), key=len)) if q else '?'
            return '%s:%s %s' % (mm.group(1), mm.group(2), nm)
        for a, b, name in rg.get(mm.group(1), []):
            if a <= int(mm.group(2)) <= b:
                return name
        return '(neznamy)'
    seq = {cn: tag(c) for cn, c in cells.items() if not _comb(c['type'])}
    share = collections.defaultdict(set)
    for cn, c in cells.items():
        if _comb(c['type']):
            continue
        stack = [b for p, bits in c['connections'].items() if not _isout(c, p) for b in bits if isinstance(b, int)]
        seen = set()
        while stack:
            b = stack.pop()
            d = driver.get(b)
            if d is None or d in seen or not _comb(cells[d]['type']):
                continue
            seen.add(d)
            share[d].add(seq[cn])
            cd = cells[d]
            stack.extend(bb for p, bits in cd['connections'].items() if not _isout(cd, p) for bb in bits if isinstance(bb, int))
    res = collections.defaultdict(collections.Counter)
    for cn, c in cells.items():
        k = _kind(c['type'])
        if k is None:
            continue
        if _comb(c['type']):
            s = share.get(cn) or {'(nepouzito)'}
            for i in s:
                res[i][k] += 1.0 / len(s)
        else:
            res[seq[cn]][k] += 1
    return res


def _fmt_mod(res):
    out = ['%-30s %8s %7s %6s %5s %4s' % ('modul RTL', 'LUT', 'ALU', 'FF', 'BSRAM', 'DSP')]
    tot = collections.Counter()
    for k, c in sorted(res.items(), key=lambda kv: -sum(kv[1].values())):
        tot.update(c)
        out.append('%-30s %8.1f %7.1f %6d %5d %4d' % (k[:30], c['LUT'], c['ALU'], c['FF'], c['BRAM'], c['DSP']))
    out.append('%-30s %8.1f %7.1f %6d %5d %4d' % ('CELKEM', tot['LUT'], tot['ALU'], tot['FF'], tot['BRAM'], tot['DSP']))
    return out


def main():
    a = sys.argv[1:]
    deep = '--deep' in a
    a = [x for x in a if x != '--deep']
    js_out = None
    if '--json' in a:
        i = a.index('--json'); js_out = a[i + 1]; del a[i:i + 2]
    if not a or a[0] not in ('stat', 'cmp', 'lines'):
        sys.exit(__doc__)
    with tempfile.TemporaryDirectory() as td:
        if a[0] == 'stat':
            js = js_out or os.path.join(td, 'n.json')
            synth(os.path.abspath(a[1]), deep, js)
            an = yosys_stat.analyze(js, 20)
            yosys_stat.show(an, 20)
            print()
            print('\n'.join(_fmt_mod(breakdown(js, os.path.join(a[1], 'src')))))
        elif a[0] == 'lines':
            js = os.path.join(td, 'n.json')
            synth(os.path.abspath(a[1]), deep, js)
            n = int(a[3]) if len(a) > 3 else 40
            res = breakdown(js, os.path.join(a[1], 'src'), 'line')
            rows = sorted(((k, v) for k, v in res.items() if k.startswith(a[2] + ':')), key=lambda kv: -sum(kv[1].values()))
            print('%-52s %7s %6s %5s' % ('radek koncoveho FF', 'LUT', 'ALU', 'FF'))
            for k, v in rows[:n]:
                print('%-52s %7.1f %6.1f %5d' % (k[:52], v['LUT'], v['ALU'], v['FF']))
        else:
            ra, rb = os.path.abspath(a[1]), os.path.abspath(a[2])
            ja, jb = os.path.join(td, 'a.json'), os.path.join(td, 'b.json')
            synth(ra, deep, ja); synth(rb, deep, jb)
            sa, sb = yosys_stat.summary(yosys_stat.analyze(ja, 20)), yosys_stat.summary(yosys_stat.analyze(jb, 20))
            print('%-28s %8s %8s %8s' % ('(abc -lut 4%s)' % (', -DDEEP_RAW' if deep else ''), 'A', 'B', 'B-A'))
            for k in list(sa) + [k for k in sb if k not in sa]:
                va, vb = sa.get(k, 0), sb.get(k, 0)
                print('%-28s %8s %8s %+8d' % (k, va, vb, vb - va))
            ma, mb = breakdown(ja, os.path.join(ra, 'src')), breakdown(jb, os.path.join(rb, 'src'))
            print('\nmoduly RTL (LUT/ALU/FF/BSRAM), jen zmenene:')
            for k in sorted(set(ma) | set(mb)):
                x, y = ma.get(k, collections.Counter()), mb.get(k, collections.Counter())
                if any(abs(x[t] - y[t]) > 1e-6 for t in ('LUT', 'ALU', 'FF', 'BRAM')):
                    print('  %-22s %7.1f/%5.0f/%5d/%3d -> %7.1f/%5.0f/%5d/%3d  (%+.1f/%+.0f/%+d/%+d)' % (
                        k[:22], x['LUT'], x['ALU'], x['FF'], x['BRAM'], y['LUT'], y['ALU'], y['FF'], y['BRAM'],
                        y['LUT'] - x['LUT'], y['ALU'] - x['ALU'], y['FF'] - x['FF'], y['BRAM'] - x['BRAM']))


if __name__ == '__main__':
    main()
