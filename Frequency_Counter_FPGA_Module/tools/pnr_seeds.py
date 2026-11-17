"""pnr_seeds.py -- statistika casovani pres vic sestaveni (L-0222: BLD_TIME je vstup P&R = seed rozmisteni).

Kazde sestaveni bezi v kopii abl/<var>@<k>/ (tools/abl_cls.py: varianty zdrojaku V a volby Gowin TCLX), src/build_id.vh
dostane jiny BLD_TIME (pevny posun od zakladu, at je beh opakovatelny). Vysledek: rezerva kazdeho sestaveni + ktere
skupiny registru (cil cesty bez indexu bitu) selhavaji opakovane = systematicke cesty, ne nahoda rozmisteni.

Pouziti (z adresare Frequency_Counter_FPGA_Module, Windows, Gowin v C:/Gowin):
    python tools/pnr_seeds.py run <var> <N> [-j J]     # N sestaveni varianty (napr. base, noTSP, base+p3), J paralelne
    python tools/pnr_seeds.py report <var> [<var> ...] # souhrn: rezerva min/median/max, # PASS, skupiny cest
"""
import os, re, shutil, statistics, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import abl_cls, fpga_release

BASE_T = 0x6ADC0000           # zaklad BLD_TIME (2026-10), sestaveni k ma BLD_TIME = BASE_T + 7919*k


def run(var, n, jobs):
    # priprava kopii serialne (abl_cls.prepare pouziva spolecny adresar abl/<var>/), sestaveni paralelne
    dirs = []
    for k in range(n):
        name = '%s@%d' % (var, k)
        d = os.path.join(abl_cls.ABL, name)
        if os.path.isdir(d):
            shutil.rmtree(d, onerror=lambda f, path, e: (os.chmod(path, 0o700), f(path)))
        tmp = abl_cls.prepare(var)
        shutil.move(tmp, d)
        tcl = open(os.path.join(d, 'build.tcl')).read().replace(tmp.replace('\\', '/'), d.replace('\\', '/'))
        open(os.path.join(d, 'build.tcl'), 'w').write(tcl)
        bt = BASE_T + 7919 * k
        open(os.path.join(d, 'src', 'build_id.vh'), 'w').write(
            "// pnr_seeds.py: syntheticky BLD_TIME (seed rozmisteni)\n"
            "localparam [31:0] BLD_TIME = 32'h%08X;\nlocalparam [31:0] BLD_GIT  = 32'h00000000;\n" % bt)
        dirs.append((name, d))

    def go(nd):
        name, d = nd
        t0 = time.time()
        with open(os.path.join(d, 'run.log'), 'w') as log:
            rc = subprocess.call([abl_cls.GW_SH, os.path.join(d, 'build.tcl')], cwd=d, stdout=log, stderr=subprocess.STDOUT)
        return name, rc, time.time() - t0

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for name, rc, dt in ex.map(go, dirs):
            r = one(name)
            print('%-16s rc=%d %4.0f s  CLS %s  setup %s  viol %s' % (name, rc, dt, r.get('cls'), r.get('setup_ws'),
                                                                     r.get('setup_viol')), flush=True)


def group(reg):
    """u_ring/wa_b_22_s0 -> u_ring/wa_b ; hold_b_s_s0 -> hold_b_s ; u_sa/o_bub_0_s1 -> u_sa/o_bub"""
    reg = re.sub(r'/(D|CE|Q|SET|RESET|CLEAR|PRESET|DOUT\[\d+\])$', '', reg)
    reg = re.sub(r'_s\d*$', '', reg)
    reg = re.sub(r'(_\d+)+$', '', reg)
    return reg


def one(name):
    d = os.path.join(abl_cls.ABL, name, 'v3', 'impl', 'pnr')
    r = {}
    try:
        tr = open(os.path.join(d, 'Counter_FPGA_v3.tr'), encoding='utf-8', errors='replace').read()
        rpt = open(os.path.join(d, 'Counter_FPGA_v3.rpt.txt'), encoding='utf-8', errors='replace').read()
    except OSError:
        return r
    r.update(fpga_release.parse_tr(tr))
    m = re.search(r'CLS\s*\|\s*(\d+)/4320', rpt)
    r['cls'] = int(m.group(1)) if m else None
    # cesty z prvni tabulky "Setup Paths Table[1]" (report_timing -max_paths 50 -max_common_paths 1)
    paths = []
    # druhy vyskyt = vlastni tabulka (prvni je obsah); vezmi vsechny radky "n  slack  od  do" za nim do prazdneho bloku
    for mi in re.finditer(re.escape('Setup Paths Table[1]'), tr):
        rows = []
        for line in tr[mi.end():].splitlines()[1:80]:
            mm = re.match(r'\s+(\d+)\s+(-?[\d.]+)\s+(\S+)\s+(\S+)', line)
            if mm:
                rows.append((float(mm.group(2)), mm.group(3), mm.group(4)))
            elif rows and not line.strip():
                break
        if rows:
            paths = rows
            break
    r['paths'] = paths
    return r


def report(vars_):
    for var in vars_:
        names = sorted((n for n in os.listdir(abl_cls.ABL) if n.startswith(var + '@')), key=lambda s: int(s.split('@')[1]))
        res = [(n, one(n)) for n in names]
        res = [(n, r) for n, r in res if r.get('setup_ws') is not None]
        if not res:
            print(var, ': zadne vysledky'); continue
        ws = [r['setup_ws'] for _, r in res]
        cls = [r['cls'] for _, r in res]
        print('=== %s: %d sestaveni, CLS %s..%s, setup min %.3f / median %.3f / max %.3f ns, PASS (>= 0) %d' % (
            var, len(res), min(cls), max(cls), min(ws), statistics.median(ws), max(ws), sum(w >= 0 for w in ws)))
        for n, r in res:
            print('   %-16s CLS %4s  setup %7.3f  viol %4s  %s' % (n, r['cls'], r['setup_ws'], r['setup_viol'], r['setup_path']))
        # skupiny cilu a zdroju porusenych cest: v kolika sestavenich, nejhorsi rezerva
        cnt, worst, src = {}, {}, {}
        for n, r in res:
            seen = set()
            for s, a, b in r['paths']:
                if s >= 0:
                    continue
                g = group(b)
                if g not in seen:
                    cnt[g] = cnt.get(g, 0) + 1; seen.add(g)
                worst[g] = min(worst.get(g, 0.0), s)
                src.setdefault(g, {}).setdefault(group(a), 0)
                src[g][group(a)] += 1
        print('   cil (porusene cesty z top 50)       sestaveni  nejhorsi  hlavni zdroje')
        for g in sorted(cnt, key=lambda g: (-cnt[g], worst[g])):
            top = sorted(src[g].items(), key=lambda kv: -kv[1])[:3]
            print('   %-36s %3d/%-3d  %7.3f   %s' % (g, cnt[g], len(res), worst[g], ', '.join('%s(%d)' % kv for kv in top)))


if __name__ == '__main__':
    a = sys.argv[1:]
    jobs = 4
    if '-j' in a:
        i = a.index('-j'); jobs = int(a[i + 1]); del a[i:i + 2]
    if not a:
        print(__doc__)
    elif a[0] == 'run':
        run(a[1], int(a[2]), jobs)
    elif a[0] == 'report':
        report(a[1:])
