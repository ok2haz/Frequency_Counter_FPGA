# check_tdc_netlist.py -- KONTROLA, ze carry-chain TDC je v syntetizovanem netlistu.
# 🔴 L-0116: o tom, co je ve FPGA, rozhoduje NETLIST, ne zdrojak ani timing report
# (F-0201: `{15{sig}}+1` syntéza zredukovala na invertor, v netlistu nebyla jedina
# ALU bunka a timing report presto vypadal v poradku). Spoustet po KAZDE syntéze:
#   python sim/check_tdc_netlist.py [cesta/k/Counter_FPGA.vg]
# Ocekavano (FW >= 0x040A, STRIDE 2): 2 x 511 stupnu g[i].h.u (512. stupen se
# nevzorkuje, syntéza ho vypusti), vsechny I0=VCC a I1=GND, 2 hlavy retezu
# s CIN = sig_eff. Mene stupnu = retez zkraceny syntézou -> CHYBA.
import io, re, sys, os
d = os.path.dirname(os.path.abspath(__file__))
p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(d, '..', 'impl', 'gwsynthesis', 'Counter_FPGA.vg')
t = io.open(p, encoding='utf-8', errors='ignore').read()
cells = []
for m in re.finditer(r'^\s*ALU\s+(\S+)\s*\((.*?)\);', t, re.M | re.S):
    cells.append((m.group(1).strip(), ' '.join(m.group(2).split())))
chain = [(n, b) for n, b in cells if n.startswith('\\g[') and '.h.u' in n]
print('ALU celkem v netlistu:', len(cells), '| stupne retezu g[i].h.u:', len(chain))
ok = sum(1 for n, b in chain if '.I0(VCC)' in b and '.I1(GND)' in b)
print('z toho I0=VCC a I1=GND (pruchod carry):', ok)

def port(b, name):
    m = re.search(r'\.' + name + r'\(([^)]*)\)', b)
    return m.group(1).strip() if m else None

couts = {port(b, 'COUT') for n, b in chain}
print('priklad stupne:', chain[1][0], '|', chain[1][1][:160])
print('priklad hlavy :', chain[0][0], '|', chain[0][1][:160])
heads = [n for n, b in chain if port(b, 'CIN') not in couts]
print('stupne, jejichz CIN neni COUT jineho stupne (hlavy retezu):', len(heads))
for n, b in chain:
    if port(b, 'CIN') not in couts:
        print('   ', n, 'CIN =', port(b, 'CIN'))
sums = sum(1 for n, b in chain if port(b, 'SUM'))
print('stupne s pouzitym SUM:', sums)
if len(chain) < 1022 or ok != len(chain) or len(heads) != 2:
    print('CHYBA: retez neodpovida ocekavani (2 x 511 pruchodu carry)'); sys.exit(1)
print('OK: retez kompletni')
