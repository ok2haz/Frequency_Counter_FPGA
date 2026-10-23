#!/usr/bin/env python3
"""Negativni kontroly sim/tb_top_v3.sv (protokol v3, top_v3): kazda mutace RTL (docasna kopie src/ ve vystupnim adresari,
repo se nemeni) musi test SHODIT. Bezi paralelne (kazda mutace = jedna simulace 3..15 min).
Pouziti: python3 sim/mut_top_v3.py <koren FPGA modulu> <vystupni adresar> [jmeno ...] [-j N]"""
import os, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

root = os.path.abspath(sys.argv[1])
out = os.path.abspath(sys.argv[2])
args = sys.argv[3:]
jobs = 8
if '-j' in args:
    i = args.index('-j'); jobs = int(args[i + 1]); del args[i:i + 2]
only = set(args)
sim, src = os.path.join(root, 'sim'), os.path.join(root, 'src')

TDC = ['-DSIM_LUT_PS=800', '-DSIM_IDX', '-DSIM_TAP_MEAN=39', '-DSIM_TAP_SPREAD=15', '-DSIM_BREAK_IDX=268', '-DSIM_TOP']
MODE = {0: ['-DSIM_AUTOCAL', '-DTB_NFR=8', '-DTB_FB=16.100161e6'], 1: ['-DSIM_AUTOCAL', '-DTB_MODE=1', '-DSIM_RAW=4'], 2: ['-DTB_MODE=2'],
        3: ['-DSIM_AUTOCAL', '-DTB_MODE=3', '-DTB_FA=5.0e6', '-DTB_WA=37300.0']}
# (jmeno, soubor v src/, puvodni text, mutace, rezim testbenche)
M = [
    # (konstantni posun indexu, napr. push = tq1, je EKVIVALENTNI mutant: vyrusi se ve vsech rozdilech -- netestuje se)
    ('FIFO hloubky 1', 'v3_core.v', "                    else if (fn == 2'd1) begin fs <= xcnt; fn <= 2'd2; end",
     "                    else if (fn == 2'd1) begin fh <= xcnt; end", 0),
    ('rozdil t: bez vypujcky', 'v3_core.v', "dtu <= tm[39:20] - t0[39:20] - {19'd0, btl};", "dtu <= tm[39:20] - t0[39:20];", 0),
    ('zaznam: slovo T0 = SX', 'v3_core.v', "            3'd3: wsel = t0[31:0];", "            3'd3: wsel = sx[31:0];", 0),
    ('znacka v taktu clear se pricte', 'v3_core.v', "    wire       take = pop & ~hold & ~freeze & ~clear;",
     "    wire       take = pop & ~hold & ~freeze;", 0),
    ('freeze se nepouzije', 'v3_core.v', "    wire       take = pop & ~hold & ~freeze & ~clear;", "    wire       take = pop & ~hold & ~clear;", 0),
    ('kontrolni pocitani bez hran', 'v3_core.v', "        ce  <= cp2 & ~cp3 & cnt_en;", "        ce  <= 1'b0;", 0),
    ('zaznam B do slotu A', 'v3_core.v', "            wa <= {wrec + {{(RAW-1){1'b0}}, k1[3]}, k1[2:0]};", "            wa <= {wrec, k1[2:0]};", 0),
    ('ACK se ignoruje', 'app_v3.v', "                                rec_ack <= {rx_p3, rx_p2, rx_p1, rx_p0};", "                                rec_ack <= rec_ack;", 0),
    ('CRC bloku 0 se nekontroluje', 'app_v3.v', "rx_b1 != VER || !rx_blk_ok[0]) begin", "rx_b1 != VER) begin", 0),
    ('delka transakce se nekontroluje', 'app_v3.v', "                if (!rx_len_ok) begin", "                if (1'b0) begin", 0),
    ('stranka bez posunu bloku', 'app_v3.v', "a_pbase + ((blk == 2'd1) ? 16'd0 : (blk == 2'd2) ? 16'd84 : 16'd168)", "a_pbase", 0),
    ('preteceni: REC_ACK se neposune', 'app_v3.v',
     "if (state != S_ASM && (rec_head - rec_ack) > RING_USE) begin rec_ack <= rec_head - RING_USE; ovf <= 1'b1; end", "", 1),
    ('potvrzeni tabulky bez masky', 'top_v3.v', ".lw_commit(lw_cm_p & cmk2[1])", ".lw_commit(lw_cm_p)", 2),
    ('tabulka do aktivni banky', 'tdc.v', "    wire          bwr     = (BANKS != 0) ? ~bank_r : 1'b0;", "    wire          bwr     = (BANKS != 0) ? bank_r : 1'b0;", 2),
    # TSPIPE (rozlozeni 2026-10-23): platnost o takt driv nez ts_ps -> znacka predchozi spustene hrany s indexem teto.
    # (horni cast z tr* o takt pozdeji = konstantni posun vsech znacek a dolni cast z lut_q3 o takt pozdeji = tataz polozka
    # tabulky, kod se drzi >= 5 taktu -> ekvivalentni mutanty, netestuji se)
    ('znacka: ts_valid o takt driv', 'tdc.v', "        ts_valid <= (TSPIPE != 0) ? tv_w : tv_z;", "        ts_valid <= tv_z;", 0),
    ('B zdroj ignorovan', 'top_v3.v', "    wire        sig_b = (bs2[0] ? ch_a : ch_b) ^ bs2[1];", "    wire        sig_b = ch_b;", 3),
    ('RAW: prohozene poloviny', 'app_v3.v', "it_src = g_cur[0] ? raw_q[47:24] : raw_q[23:0];", "it_src = g_cur[0] ? raw_q[23:0] : raw_q[47:24];", 0),
]
RTL = ['top_v3.v', 'v3_core.v', 'app_v3.v', 'spi_slave_phy_v3.v', 'spi_app.v', 'tdc.v']


def run(m):
    name, fn, a, b, mode = m
    d = os.path.join(out, name.replace(' ', '_').replace(':', '').replace(',', ''))
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for f in RTL:
        shutil.copy(os.path.join(src, f), d)
    p = os.path.join(d, fn)
    s = open(p, encoding='utf-8').read()
    if s.count(a) != 1:
        return name, 'KOTVA NENALEZENA (%dx)' % s.count(a), '', 0
    open(p, 'w', encoding='utf-8').write(s.replace(a, b))
    vvp = os.path.join(d, 't.vvp')
    cmd = ['iverilog', '-g2012', '-I', sim] + TDC + MODE[mode] + ['-s', 'tb_top_v3', '-o', vvp,
           os.path.join(sim, 'gowin_models.v')] + [os.path.join(d, f) for f in RTL] + [os.path.join(sim, 'tb_top_v3.sv')]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return name, 'PREKLAD SELHAL', (r.stdout + r.stderr)[:200], 0
    t0 = time.time()
    r = subprocess.run(['vvp', '-n', vvp], capture_output=True, text=True, cwd=d, timeout=3600)
    caught = 'PASS' not in r.stdout
    first = next((l.strip() for l in r.stdout.splitlines() if 'CHYBA' in l or 'FAIL' in l), '')
    return name, 'ZACHYCENO' if caught else '!!! NEZACHYCENO', first[:80], time.time() - t0


sel = [m for m in M if not only or m[0] in only]
bad = 0
with ThreadPoolExecutor(max_workers=jobs) as ex:
    for name, res, first, dt in ex.map(run, sel):
        print('%-34s %-16s %4.0f s  %s' % (name, res, dt, first), flush=True)
        bad += 0 if res == 'ZACHYCENO' else 1
print('\nvsechny mutace zachyceny' if bad == 0 else '\n%d mutaci NEZACHYCENO' % bad)
sys.exit(1 if bad else 0)
