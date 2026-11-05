"""syn_hier.py -- rozpad zdroju syntezy po modulech z *_syn_rsc.xml (Gowin: registry, ALU, LUT, BSRAM, DSP vcetne podmoduli).
Pouziti: python tools/syn_hier.py <Counter_FPGA_v3_syn_rsc.xml> [--cls]
Odhad CLS modulu: max(ceil((LUT+ALU)/2), ceil(reg/2)) -- dolni mez (kazda CLS = 2 LUT/ALU + 2 registry); skutecne CLS je vetsi."""
import re, sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()


def num(s):
    return int(s.split('(')[0]) if s else 0


def walk(m, depth, rows):
    own = dict(reg=num(m.get('Register')), alu=num(m.get('Alu')), lut=num(m.get('Lut')), bsram=num(m.get('Bsram')), dsp=num(m.get('Alu54d') or m.get('ALU54D')))
    tot = dict(reg=num(m.get('T_Register')), alu=num(m.get('T_Alu')), lut=num(m.get('T_Lut')), bsram=num(m.get('T_Bsram')), dsp=num(m.get('T_ALU54D')))
    rows.append((depth, m.get('name'), own, tot))
    for s in m.findall('SubModule'):
        walk(s, depth + 1, rows)


rows = []
walk(root, 0, rows)
print('%-28s %6s %6s %6s %6s %5s | %6s %6s %6s %6s  ~CLS' % ('modul', 'reg', 'alu', 'lut', 'bsram', 'dsp', 'T.reg', 'T.alu', 'T.lut', 'T.bsr'))
for d, n, o, t in rows:
    cls = max((t['lut'] + t['alu'] + 1) // 2, (t['reg'] + 1) // 2)
    print('%-28s %6d %6d %6d %6d %5d | %6d %6d %6d %6d %5d' % ('  ' * d + n, o['reg'], o['alu'], o['lut'], o['bsram'], o['dsp'], t['reg'], t['alu'], t['lut'], t['bsram'], cls))
