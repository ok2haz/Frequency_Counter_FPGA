#!/usr/bin/env python3
"""Negativni kontroly tb_phy_v3: kazda mutace PHY v3 (docasna kopie, repo se nemeni) musi test shodit.
Pouziti: python sim/mut_phy_v3.py <koren FPGA modulu> <vystupni adresar>   (vyzaduje iverilog + vvp v PATH)
Zaveden 2026-10-22: bez mutaci by test neodhalil nestridani RX RAM (porovnani != s X, L-0192)."""
import os, subprocess, sys

fpga, out = sys.argv[1], sys.argv[2]
src = open(os.path.join(fpga, 'src', 'spi_slave_phy_v3.v'), encoding='utf-8').read()
M = [
    ('TX CRC se nevklada (254/255 z RAM)',
     "if (tb_next == 8'd254)      cur <= tcrc[7:0];", "if (1'b0)      cur <= tcrc[7:0];"),
    ('spatna konstanta CRC po MAGIC', "CRC_MAGIC = 16'h04BF", "CRC_MAGIC = 16'hFFFF"),
    ('delka se nekontroluje', 'rx_len_ok     <= (rcnt == NBITS);', "rx_len_ok     <= 1'b1;"),
    ('polovina TX se meni i behem transakce', 'if (!cs_active) tx_base <= half_s[2];', 'tx_base <= half_s[2];'),
    ('RX CRC se na zacatku bloku nenuluje',
     "if (rb_in_blk == 8'd0)        rcrc <= crc16_step(16'hFFFF, rbyte);",
     "if (rb_in_blk == 8'd0)        rcrc <= crc16_step(rcrc, rbyte);"),
    ('RX RAM se nestrida', 'rx_wr_half    <= ~rx_wr_half;', 'rx_wr_half    <= rx_wr_half;'),
]
bad = 0
for name, a, b in M:
    assert src.count(a) == 1, ('kotva', name)
    p = os.path.join(out, 'phy_v3_mut.v')
    open(p, 'w', encoding='utf-8').write(src.replace(a, b))
    vvp = os.path.join(out, 'phy_v3_mut.vvp')
    r = subprocess.run(['iverilog', '-g2012', '-s', 'tb_phy_v3', '-o', vvp, p, os.path.join(fpga, 'sim', 'tb_phy_v3.sv')],
                       capture_output=True, text=True)
    if r.returncode:
        print('%-42s PREKLAD SELHAL' % name); bad += 1; continue
    o = subprocess.run(['vvp', '-n', vvp], capture_output=True, text=True).stdout
    caught = 'FAIL' in o and 'PASS' not in o
    first = next((l.strip() for l in o.splitlines() if 'CHYBA' in l), '')
    print('%-42s %s  %s' % (name, 'ZACHYCENO' if caught else '!!! NEZACHYCENO', first[:60]))
    bad += 0 if caught else 1
print('\nvsechny mutace zachyceny' if bad == 0 else '\n%d mutaci NEZACHYCENO' % bad)
sys.exit(1 if bad else 0)
