# chain_netlist_check.py -- porovna obe instance tdc_chain v netlistu (q, qf, t0).
import re, sys, os
d = os.path.dirname(os.path.abspath(__file__))
p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(d, '..', 'impl', 'gwsynthesis', 'Counter_FPGA.vg')
t = open(p, encoding='utf-8', errors='ignore').read()
for name in ('tdc_chain', 'tdc_chain_0'):
    m = re.search(r'^module ' + name + r' \((.*?)^endmodule', t, re.M | re.S)
    if not m:
        print(name, 'nenalezen'); continue
    b = m.group(1)
    q = re.findall(r'^\s*DFF\w*\s+(q_\d+)_s', b, re.M)
    qf = re.findall(r'^\s*DFF\w*\s+(qf_\d+)_s', b, re.M)
    print('== %s: q %d, qf %d' % (name, len(q), len(qf)))
    for r in ('q_0', 'q_1', 'q_2', 'qf_0', 'qf_1', 'qf_2'):
        mm = re.search(r'^\s*(DFF\w*)\s+' + r + r'_s\d*\s*\((.*?)\);', b, re.M | re.S)
        print('   %-5s %s' % (r, (mm.group(1) + ' ' + ' '.join(mm.group(2).split())[:140]) if mm else '-- CHYBI'))
    for mm in re.finditer(r'^\s*(LUT\d|INV)\s+(\S+)\s*\((.*?)\);', b, re.M | re.S):
        if 't0' in mm.group(3):
            print('   t0 logika:', mm.group(1), mm.group(2), ' '.join(mm.group(3).split())[:140])
    tm = re.search(r'assign\s+t0\s*=\s*([^;]+);', b)
    print('   assign t0 =', tm.group(1).strip() if tm else '-')
