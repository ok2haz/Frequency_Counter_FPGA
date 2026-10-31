"""dsp_sta.py -- casovani pinu DSP souctu seg_chan3 v HOTOVEM sestaveni (2026-10-31, STATUS #347).

Zkopiruje abl/<jmeno> (zdroje, projekt, build.tcl) do abl/<jmeno>rt, do kopie timing_v3.sdc pripise report_timing na piny
ALU54D obou kanalu (citac N = n2734_s0, soucty g_dsp.u_sx / g_dsp.u_st), z registru clear_a / clear_b a z vystupu DOUT,
zapne SDF (zpozdeni vedeni po sinku), spusti gw_sh a overi, ze datove radky bitstreamu (.fs bez komentaru //) jsou SHODNE
s puvodnim sestavenim (report ani SDF nesmi zmenit P&R). Pak vypise souhrn: pro kanal, instanci a skupinu pinu (CE,
RESET, A, B, DOUT->cil) nejhorsi rezervu setup a hold, zpozdeni dat a zdroj nejhorsi cesty.
Vydany bitstream se da pripravit do abl/ nastrojem tools/rel_repro.py (zdroje z gitu + identita sestaveni).

Pouziti (z adresare Frequency_Counter_FPGA_Module, Windows, Gowin v C:/Gowin):
    python tools/dsp_sta.py <jmeno sestaveni v abl, napr. appOsc@2> [--jen-rozbor]
"""
import os, re, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import abl_cls

sys.stdout.reconfigure(encoding='utf-8')
DSP = ['n2734_s0', 'g_dsp.u_sx', 'g_dsp.u_st']
REP = []
for ch in ('u_sa', 'u_sb'):
    for inst in DSP:
        for kind in ('-setup', '-hold'):
            REP.append('report_timing %s -max_paths 80 -max_common_paths 1 -to [get_pins {%s/%s/*}]' % (kind, ch, inst))
for reg in ('clear_a', 'clear_b'):
    for kind in ('-setup', '-hold'):
        REP.append('report_timing %s -max_paths 120 -max_common_paths 1 -from [get_regs {u_ring/%s_s0}]' % (kind, reg))
# kam vedou cesty z vystupu DOUT (citac N ma zpetnou vazbu DOUT -> A; v SDF je uvnitr DSP, 0 ns, STA ji nevykazuje)
for ch in ('u_sa', 'u_sb'):
    for inst in ('n2734_s0', 'g_dsp.u_sx'):
        for kind in ('-setup', '-hold'):
            REP.append('report_timing %s -max_paths 120 -max_common_paths 1 -from [get_pins {%s/%s/DOUT*}]' % (kind, ch, inst))


def data(p):
    return [l for l in open(p, encoding='ascii', errors='replace').read().splitlines() if not l.startswith('//')]


def build(name):
    src = os.path.join(abl_cls.ABL, name)
    dst = os.path.join(abl_cls.ABL, name + 'rt')
    if os.path.isdir(dst):
        shutil.rmtree(dst, onerror=lambda f, p, e: (os.chmod(p, 0o700), f(p)))
    os.makedirs(os.path.join(dst, 'v3'))
    shutil.copytree(os.path.join(src, 'src'), os.path.join(dst, 'src'))
    shutil.copy(os.path.join(src, 'v3', 'Counter_FPGA_v3.gprj'), os.path.join(dst, 'v3', 'Counter_FPGA_v3.gprj'))
    fs, fd = src.replace('\\', '/'), dst.replace('\\', '/')
    tcl = open(os.path.join(src, 'build.tcl')).read()
    assert tcl.count(fs) == 1, 'build.tcl neobsahuje cestu sestaveni'
    tcl = tcl.replace(fs, fd)
    assert tcl.count('\nrun all') == 1
    # SDF a simulacni netlist jsou jen vystupni volby (rozmisteni nemeni -- overi se shodou bitstreamu)
    tcl = tcl.replace('\nrun all', '\nset_option -gen_sdf 1\nset_option -gen_verilog_sim_netlist 1\nrun all')
    open(os.path.join(dst, 'build.tcl'), 'w').write(tcl)
    with open(os.path.join(dst, 'src', 'timing_v3.sdc'), 'a', newline='') as f:
        f.write('\n// dsp_sta.py: casovani pinu DSP souctu seg_chan3\n' + '\n'.join(REP) + '\n')
    with open(os.path.join(dst, 'run.log'), 'w') as log:
        rc = subprocess.call([abl_cls.GW_SH, os.path.join(dst, 'build.tcl')], cwd=dst, stdout=log, stderr=subprocess.STDOUT)
    print('gw_sh rc', rc)


def report(name):
    src = os.path.join(abl_cls.ABL, name)
    dst = os.path.join(abl_cls.ABL, name + 'rt')
    pnr_s = os.path.join(src, 'v3', 'impl', 'pnr')
    pnr_d = os.path.join(dst, 'v3', 'impl', 'pnr')
    if not os.path.exists(os.path.join(pnr_d, 'Counter_FPGA_v3.fs')):
        sys.exit('bitstream kopie chybi (viz %s)' % os.path.join(dst, 'run.log'))
    same = data(os.path.join(pnr_s, 'Counter_FPGA_v3.fs')) == data(os.path.join(pnr_d, 'Counter_FPGA_v3.fs'))
    print('bitstream %s: datove radky %s' % (name, 'SHODNE' if same else 'ODLISNE -> rozbor neplati pro puvodni sestaveni'))
    tr = open(os.path.join(pnr_d, 'Counter_FPGA_v3.tr'), encoding='utf-8', errors='replace').read()
    # radky tabulek "  n  slack  od  do  clk  clk  relace  skew  data"
    row = re.compile(r'^\s+\d+\s+(-?[\d.]+)\s+(\S+)\s+(\S+)\s+\S+\s+\S+\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s*$')
    kind, best = None, {}
    for line in tr.splitlines():
        l = line.lower()
        if re.match(r'^\d+(\.\d+)*\s', line):            # nadpis oddilu: tabulka cest, nebo jiny oddil (konec tabulky)
            kind = None
            if 'paths table' in l:
                kind = 'setup' if 'setup' in l else 'hold' if 'hold' in l else 'recovery' if 'recovery' in l else 'removal'
            continue
        m = row.match(line)
        if not (m and kind):
            continue
        slack, fr, to, skew, dd = float(m.group(1)), m.group(2), m.group(3), float(m.group(5)), float(m.group(6))
        mm = re.match(r'(u_s[ab])/(n2734_s0|g_dsp\.u_s[xt])/([A-Z]+)', to)
        mf = re.match(r'(u_s[ab])/(n2734_s0|g_dsp\.u_s[xt])/DOUT', fr)
        if mf:
            key = (mf.group(1), mf.group(2), 'DOUT->' + re.sub(r'\[\d+\]', '', re.sub(r'_\d+_s\d*', '', to)), kind)
        elif mm:
            key = (mm.group(1), mm.group(2), mm.group(3), kind)
        elif fr.startswith('u_ring/clear_'):
            key = (fr.split('/')[1].replace('_s0', ''), 'z clear', re.sub(r'\[\d+\]', '', to.split('/')[-1]), kind)
        else:
            continue
        if key not in best or slack < best[key][0]:
            best[key] = (slack, fr, to, dd, skew)
    print('%-7s %-11s %-26s %-6s %8s %8s %7s  %s' % ('kanal', 'instance', 'pin', 'druh', 'rezerva', 'data', 'skew', 'nejhorsi cesta'))
    for key in sorted(best):
        s, fr, to, dd, sk = best[key]
        print('%-7s %-11s %-26s %-6s %8.3f %8.3f %7.3f  %s -> %s' % (key[0], key[1], key[2], key[3], s, dd, sk, fr, to))
    # poloha bloku DSP (radky tSu / tHld podrobnych cest)
    loc = sorted(set(re.findall(r'(DSP_R\d+\[\d\]\[[AB]\])\s+(u_s[ab]/(?:n2734_s0|g_dsp\.u_s[xt]))\s*$', tr, re.M)))
    print('poloha DSP: ' + '; '.join('%s %s' % (i, d) for d, i in loc))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if '--jen-rozbor' not in sys.argv:
        build(sys.argv[1])
    report(sys.argv[1])
