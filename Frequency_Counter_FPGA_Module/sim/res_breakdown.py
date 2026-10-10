# res_breakdown.py -- rozpad registru/LUT/ALU podle jmena v syntetizovanem netlistu.
# Gowin synteza rozpad po modulech nedava (hierarchie je zplostela), takze se
# zdroje seskupi podle zakladu jmena instance (bez indexu a sufixu _s/_sN).
#   python sim/res_breakdown.py [Counter_FPGA.vg] [pocet radku]
import re, sys, os, collections

d = os.path.dirname(os.path.abspath(__file__))
p = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].endswith('.vg') else \
    os.path.join(d, '..', 'impl', 'gwsynthesis', 'Counter_FPGA.vg')
n = int(sys.argv[-1]) if sys.argv[-1].isdigit() else 40
t = open(p, encoding='utf-8', errors='ignore').read()

def key(name):
    name = name.lstrip(chr(92))
    name = re.sub(r'_s\d*$', '', name)
    name = re.sub(r'\[\d+\]', '', name)
    name = re.sub(r'_\d+$', '', name)
    name = re.sub(r'_\d+$', '', name)
    return name

ff = collections.Counter(); lut = collections.Counter(); alu = collections.Counter()
for m in re.finditer(r'^\s*(DFF\w*|LUT\d|ALU|MUX2\w*)\s+(\S+)\s*\(', t, re.M):
    typ, name = m.group(1), key(m.group(2))
    if typ.startswith('DFF'):
        ff[name] += 1
    elif typ == 'ALU':
        alu[name] += 1
    else:
        lut[name] += 1
print('FF celkem', sum(ff.values()), '| LUT+MUX', sum(lut.values()), '| ALU', sum(alu.values()))
print('--- registry (FF) ---')
for k, v in ff.most_common(n):
    print('%6d  %s' % (v, k))
print('--- LUT/MUX ---')
for k, v in lut.most_common(15):
    print('%6d  %s' % (v, k))
print('--- ALU ---')
for k, v in alu.most_common(10):
    print('%6d  %s' % (v, k))
