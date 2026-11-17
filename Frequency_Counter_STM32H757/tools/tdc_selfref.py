# Rozbor oken z `sdramlog dump N` (UART) pro mereni vlastni reference / posunuteho generatoru.
# Pouziti:  python tools/tdc_selfref.py [--port COM10] [--n 200] [--file dump.txt] [--fnom 1e7] [--w134 1435] [--gate 0.25]
#
# Co to zjisti (a co NE):
#  * rozdeleni hodnot oken: presne nominal / skoky +-X Hz / MISCOUNT (okno vyrazene STM jako chybny pocet hran)
#  * skoky chodi ve DVOJICICH (+ potom -)? -> chyba JEDNE casove znacky (sdilena hrana dvou oken),
#    ne dvou oken; soucet dvojice = 0
#  * velikost skoku v ps vs T_clk - w134/2 (obri bin) -> podpis kodu 134
#  * ⚠️ Pri mereni VLASTNI reference je signal synchronni s hodinami TDC: obe krajni hrany okna lezi na STEJNEM
#    kodu, kvantizace se odecte a presnost/rozliseni to NEOVERI. Pravou systematickou chybu a INL da jen
#    generator zaveseny na stejnou reference s malym posunem (napr. 10 000 000,137 Hz): faze hrany pak
#    projede vsechny kody a odchylka prumeru od nastavene hodnoty je primo chyba citace.
import re, sys, time, collections, statistics as st

def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default

def fetch(port, n):
    import serial
    p = serial.Serial(port, 115200, timeout=0.3)
    p.reset_input_buffer(); p.write(('sdramlog dump %d\r\n' % n).encode())
    d = b''; last = time.time()
    while time.time() - last < 3 and len(d) < 400000:
        c = p.read(8000)
        if c: d += c; last = time.time()
    return d.decode('utf-8', 'replace')

txt = open(arg('--file', ''), encoding='utf-8').read() if '--file' in sys.argv else fetch(arg('--port', 'COM10'), int(arg('--n', '200')))
fnom = float(arg('--fnom', '1e7')); w134 = float(arg('--w134', '1435')); T = 10000.0
gate = float(arg('--gate', 0.25))   # ⚠️ hlavicka dumpu hradlo zaokrouhluje (249 ms), skutecne je 0,25 s
rows = re.findall(r'^\s*(\d+) seq=(\d+)\s+t=(\d+)\s+A=([\d,]+) Hz', txt, re.M)
r = [(int(s), float(f.replace(',', '.'))) for _, s, _, f in rows][::-1]       # chronologicky
n = len(r)
if n < 10: sys.exit('malo oken (%d)' % n)
seqs = [x[0] for x in r]
print('oken %d, seq %d..%d, mezery v seq %d, brana %.3f s' % (n, seqs[0], seqs[-1],
      sum(1 for a, b in zip(seqs, seqs[1:]) if b - a != 1), gate))

good = [f for _, f in r if f != 0]
med = st.median(good)
def cls(f):
    if f == 0: return 'Z'
    d = f - med
    return '0' if abs(d) < 0.02 else ('+' if d > 0 else '-')
s = ''.join(cls(f) for _, f in r)
c = collections.Counter(s)
print('median %.6f Hz (nominal %.0f, rozdil %+.6f Hz = %+.2e)' % (med, fnom, med - fnom, (med - fnom) / fnom))
print('rozdeleni: shodne %d (%.1f %%), skok+ %d, skok- %d, MISCOUNT(Z) %d' %
      (c['0'], 100 * c['0'] / n, c['+'], c['-'], c['Z']))
jumps = sorted(set(round(f - med, 5) for _, f in r if f != 0 and abs(f - med) >= 0.02))
for j in jumps[:6]:
    print('  skok %+.5f Hz  ->  chyba Dt %.1f ps  (T - w134/2 = %.1f ps)' % (j, abs(j) / fnom * gate * 1e12, T - w134 / 2))
pairs = collections.Counter(a + b for a, b in zip(s, s[1:]))
pm = pairs['+-'] + pairs['-+']
print('dvojice +- / -+: %d z %d skoku (%.0f %%)  -> %s' % (pm, c['+'] + c['-'],
      100 * 2 * pm / max(1, c['+'] + c['-']),
      'chyba JEDNE casove znacky (sdilena hrana dvou oken)' if 2 * pm > 0.8 * (c['+'] + c['-'])
      else 'NE jen dvojice - hledej jiny puvod'))
print('prvnich 200:', s[:200])
