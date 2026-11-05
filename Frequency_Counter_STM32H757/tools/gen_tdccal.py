"""gen_tdccal.py -- vygeneruje CM7/Core/Src/fpga_tdccal.c ze sablony fpga_tdccal.c.in.

Zlate vektory (histogram + tabulka jednoho kanalu TDC, 2^15 udalosti) bere z vystupu simulace FPGA
`Frequency_Counter_FPGA_Module/sim/tb_lutequiv.sv` (radky "HIST;k;h" a "LUT;k;v"), tedy z puvodniho vypoctu ve FPGA (AUTOCAL = 1).
Pred zapisem se vzorec nezavisle zkontroluje python zrcadlem (shoda s FPGA pro 2^15 i pro histogram vynasobeny 2^5 = meritko 2^20).

Pouziti (po zmene vzorce nebo vstupnich dat):
    cd Frequency_Counter_FPGA_Module/sim && iverilog -g2012 -I . -I ../src -s tb_lutequiv -o ../sim_out/lutequiv.vvp \
        ../src/tdc.v tb_lutequiv.sv gowin_models.v && vvp -n ../sim_out/lutequiv.vvp > ../sim_out/lutequiv.log
    python tools/gen_tdccal.py
"""
import io, os, re, sys

root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
log = os.path.join(root, 'Frequency_Counter_FPGA_Module', 'sim_out', 'lutequiv.log')
tpl = os.path.join(root, 'Frequency_Counter_STM32H757', 'CM7', 'Core', 'Src', 'fpga_tdccal.c.in')
out = os.path.join(root, 'Frequency_Counter_STM32H757', 'CM7', 'Core', 'Src', 'fpga_tdccal.c')

hist, lut = {}, {}
for ln in io.open(log, encoding='utf-8', errors='replace'):
    m = re.match(r'(HIST|LUT);(\d+);(\d+)', ln.strip())
    if m:
        (hist if m.group(1) == 'HIST' else lut)[int(m.group(2))] = int(m.group(3))
nc = len(hist)
assert nc == 256 and len(lut) == 256, 'vystup tb_lutequiv.sv neobsahuje 256 adres'
H = [hist[i] for i in range(nc)]
L = [lut[i] for i in range(nc)]
assert max(H) < 65536


def mirror(hh, log2n):
    cum, res = 0, []
    for h in hh:
        xv = 2 * cum + h
        cum += h
        xsh = xv >> (log2n - 13)
        x14 = 16383 if xsh > 16383 else xsh
        neg = (-x14) & 0x3FFF
        amt = 1 + (1 if x14 else 0)
        res.append((amt << 14) | neg)
    return res


assert mirror(H, 15) == L, 'python zrcadlo != vypocet FPGA'
assert mirror([h * 32 for h in H], 20) == L, 'meritko 2^20 != 2^15'
nz = sum(1 for h in H if h)
last = max(i for i, h in enumerate(H) if h)


def arr(name, v):
    s = 'static const uint16_t %s[%d] = {\n' % (name, len(v))
    for i in range(0, len(v), 16):
        s += '    ' + ', '.join(('0x%04X' % x) if name.endswith('lut') else str(x) for x in v[i:i + 16]) + ',\n'
    return s + '};\n'


src = io.open(tpl, encoding='utf-8').read()
for key, val in (('@GOLD_HIST@', arr('k_gold_hist', H)), ('@GOLD_LUT@', arr('k_gold_lut', L)), ('@NZ@', nz), ('@LAST@', last),
                 ('@PEAK@', max(H)), ('@OVF@', H[-1]), ('@SUM@', sum(H))):
    src = src.replace(key, str(val))
io.open(out, 'w', encoding='utf-8', newline='\n').write(src)
print('zapsano', out, '| nz', nz, 'last', last, 'peak', max(H), 'ovf', H[-1], 'sum', sum(H))
