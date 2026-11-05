"""cs_model.py -- model CLS z ridicich sad registru: sum_sad ceil(FF_sady / k) proti skutecnemu CLS z P&R (studie CLS 2026-10-23).
Pouziti: python tools/cs_model.py abl/<var> [...]   (cte v3/impl/gwsynthesis/*.vg a pnr/*.rpt.txt)"""
import collections, math, os, re, sys

for d in sys.argv[1:]:
    vg = os.path.join(d, 'v3', 'impl', 'gwsynthesis', 'Counter_FPGA_v3.vg')
    rpt = os.path.join(d, 'v3', 'impl', 'pnr', 'Counter_FPGA_v3.rpt.txt')
    if not (os.path.exists(vg) and os.path.exists(rpt)):
        continue
    t = open(vg, encoding='utf-8', errors='ignore').read()
    sets = collections.Counter()
    nff = 0
    for m in re.finditer(r'^\s*(DFF\w*)\s+(\S+)\s*\((.*?)\);', t, re.M | re.S):
        typ, body = m.group(1), m.group(3)
        port = dict(re.findall(r'\.(\w+)\(([^)]*)\)', body))
        key = (port.get('CLK'), port.get('CE', '1'), port.get('RESET') or port.get('SET') or port.get('CLEAR') or port.get('PRESET') or '0', typ[3:] if len(typ) > 3 else '')
        # typ DFF, DFFE, DFFS, DFFR, DFFSE ... : sada = (clk, ce, sr-signal, sr-druh)
        sets[(port.get('CLK'), port.get('CE', '1'), port.get('RESET') or port.get('SET') or port.get('CLEAR') or port.get('PRESET') or '0')] += 1
        nff += 1
    r = open(rpt, encoding='utf-8', errors='replace').read()
    cls = int(re.search(r'CLS\s*\|\s*(\d+)/4320', r).group(1))
    out = ['%-16s FF %5d sad %4d CLS %5d' % (os.path.basename(d), nff, len(sets), cls)]
    for k in (2, 3, 4, 6, 8):
        out.append('k%d=%d' % (k, sum(math.ceil(n / k) for n in sets.values())))
    print('  '.join(out))
