#!/usr/bin/env python3
"""Dukaz shody chovani top_v3 (2026-10-24): STOPA testu topv3* a jeji porovnani.
Stopa = kazdy bajt kazde transakce SPI (cas, poradi transakce, index bajtu, MOSI, MISO) + zmeny vystupu top_v3
(led_tx, fpga_ok, ref_ctrl, PSRAM O_psram_*, IO_psram_*) s casem; zapisuje ji tb_top_v3.sv pod -DTRACE. Misto
gowin_models.v se prekladaji sim/gowin_models_trace.v (nahoda per instance ze jmena -> sled nezavisi na zbytku navrhu).
Testy, soubory a defs se berou z tabulky $tests v sim/run.ps1 (jako run_sim.py).

  python3 sim/trace_v3.py run <koren RTL> <vystup> [test ...] [-j N] [--sim <adresar sim>] [-D<def> ...]
      <koren RTL>/src = zdroje RTL; testbench a modely z <adresar sim> (vychozi <koren RTL>/sim). Bez jmen = vsechny topv3*.
      Do <vystup>/<test>/ zapise log.txt a trace.n (normalizovana stopa), na konec radek PASS/FAIL a otisk stopy.
  python3 sim/trace_v3.py cmp <vystup A> <vystup B> [test ...]
      Porovna normalizovane stopy; pri neshode vypise prvni rozdil (test, cas, signal, obe hodnoty). Exit 1 pri neshode.
Normalizace: radky O se slouci, kdyz se stav vystupu nezmenil (glitch v jednom casovem kroku ukaze $fstrobe jen jednou)."""
import hashlib, os, re, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

O_NAMES = ['led_tx', 'fpga_ok', 'ref_ctrl', 'O_psram_ck', 'O_psram_ck_n', 'O_psram_cs_n', 'O_psram_reset_n',
           'IO_psram_rwds', 'IO_psram_dq']


def tests_from_ps1(sim):
    ps = open(os.path.join(sim, 'run.ps1'), encoding='utf-8').read()
    blk = ps[ps.index('$tests = @('):]
    blk = blk[:re.search(r'\n\)\s*\n', blk).start()]
    blk = '\n'.join(l.split('#', 1)[0] if not l.strip().startswith('#') else '' for l in blk.splitlines())

    def arr(s, key):
        m = re.search(key + r'\s*=\s*@\((.*?)\)', s, re.S)
        return [a or b for a, b in re.findall(r'"([^"]*)"|\'([^\']*)\'', m.group(1))] if m else []
    out = {}
    for e in re.findall(r'@\{(.*?)\}', blk, re.S):
        g = lambda k: (re.search(k + r'\s*=\s*"([^"]*)"', e) or [None, None])[1]
        if g('name'):
            out[g('name')] = dict(top=g('top'), tb=g('tb'), rtl=arr(e, 'rtl'), extra=arr(e, 'extra'), defs=arr(e, 'defs'),
                                  stmcal=bool(re.search(r'stmcal\s*=\s*\$true', e)))
    return out


def normalize(raw, dst):
    """raw trace.txt -> trace.n (bez opakovani stejneho stavu O); vrati (pocet radku, sha256)."""
    h = hashlib.sha256(); n = 0; last_o = None
    with open(raw, encoding='ascii', errors='replace') as f, open(dst, 'w', newline='\n') as g:
        for line in f:
            p = line.split(' ', 2)
            if len(p) < 3:
                continue
            if p[1] == 'O':
                st = p[2]
                if st == last_o:
                    continue
                last_o = st
            g.write(line); h.update(line.encode()); n += 1
    return n, h.hexdigest()


def run_one(t, name, root, sim, out, extra_defs):
    d = os.path.join(out, name); os.makedirs(d, exist_ok=True)
    src = os.path.join(root, 'src')
    extra = [os.path.join(sim, 'gowin_models_trace.v' if x == 'gowin_models.v' else x) for x in t['extra']]
    files = extra + [os.path.join(src, x) for x in t['rtl']] + [os.path.join(sim, t['tb'])]
    ac = [] if t['stmcal'] else ['-DSIM_AUTOCAL']
    vvp = os.path.join(d, 't.vvp')
    cmd = ['iverilog', '-g2012', '-I', sim, '-s', t['top']] + ac + t['defs'] + ['-DTRACE'] + extra_defs + ['-o', vvp] + files
    t0 = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        open(os.path.join(d, 'log.txt'), 'w').write(' '.join(cmd) + '\n' + r.stdout + r.stderr)
        return name, 'PREKLAD SELHAL', 0, '', 0.0
    raw = os.path.join(d, 'trace.txt')
    if os.path.exists(raw):
        os.remove(raw)
    with open(os.path.join(d, 'log.txt'), 'w') as f:
        f.write(' '.join(cmd) + '\n')
        f.flush()
        r = subprocess.run(['vvp', '-n', vvp], stdout=f, stderr=subprocess.STDOUT, cwd=d)
    log = open(os.path.join(d, 'log.txt'), encoding='utf-8', errors='replace').read()
    res = 'PASS' if re.search(r'^PASS', log, re.M) else 'FAIL'
    n, hx = normalize(raw, os.path.join(d, 'trace.n')) if os.path.exists(raw) else (0, '')
    if os.path.exists(raw):
        os.remove(raw)
    os.remove(vvp)
    dt = time.time() - t0
    with open(os.path.join(d, 'trace.sum'), 'w') as f:
        f.write('%s %s %d %s %.0f\n' % (name, res, n, hx, dt))
    return name, res, n, hx, dt


def describe(la, lb):
    pa, pb = la.split(), lb.split()
    if len(pa) > 1 and len(pb) > 1 and pa[1] == 'O' and pb[1] == 'O' and pa[0] == pb[0]:
        diffs = ['%s %s/%s' % (O_NAMES[i], pa[2 + i], pb[2 + i]) for i in range(len(O_NAMES))
                 if 2 + i < len(pa) and 2 + i < len(pb) and pa[2 + i] != pb[2 + i]]
        return 'cas %s ps, vystup: %s' % (pa[0], ', '.join(diffs))
    if len(pa) > 5 and len(pb) > 5 and pa[1] == 'X' and pb[1] == 'X' and pa[2:4] == pb[2:4]:
        return 'cas %s/%s ps, transakce %s bajt %s: MOSI %s/%s MISO %s/%s' % (pa[0], pb[0], pa[2], pa[3], pa[4], pb[4], pa[5], pb[5])
    return 'A: %s | B: %s' % (la.strip(), lb.strip())


def cmp_one(a, b, name):
    fa, fb = os.path.join(a, name, 'trace.n'), os.path.join(b, name, 'trace.n')
    if not (os.path.exists(fa) and os.path.exists(fb)):
        return name, False, 'chybi stopa (%s)' % ('A' if not os.path.exists(fa) else 'B'), 0
    n = 0
    with open(fa) as f, open(fb) as g:
        while True:
            la, lb = f.readline(), g.readline()
            if not la and not lb:
                return name, True, 'shodne', n
            n += 1
            if la != lb:
                if not la or not lb:
                    return name, False, 'radek %d: stopa %s skoncila driv (%s)' % (n, 'A' if not la else 'B', (la or lb).strip()), n
                return name, False, 'radek %d: %s' % (n, describe(la, lb)), n


def main():
    if len(sys.argv) < 4 or sys.argv[1] not in ('run', 'cmp'):
        sys.exit(__doc__)
    mode, args = sys.argv[1], sys.argv[2:]
    jobs = max(1, (os.cpu_count() or 4) - 2)
    if '-j' in args:
        i = args.index('-j'); jobs = int(args[i + 1]); del args[i:i + 2]
    if mode == 'cmp':
        a, b = os.path.abspath(args[0]), os.path.abspath(args[1])
        names = args[2:] or sorted(x for x in os.listdir(a) if os.path.exists(os.path.join(a, x, 'trace.n')))
        bad = 0
        for name, ok, msg, n in map(lambda x: cmp_one(a, b, x), names):
            print('%-12s %-8s %s' % (name, 'SHODA' if ok else 'ROZDIL', msg), flush=True)
            bad += 0 if ok else 1
        print('stopy shodne (%d testu)' % len(names) if bad == 0 else '%d z %d testu se lisi' % (bad, len(names)))
        sys.exit(1 if bad else 0)
    root = os.path.abspath(args[0]); out = os.path.abspath(args[1]); rest = args[2:]
    sim = os.path.join(root, 'sim')
    if '--sim' in rest:
        i = rest.index('--sim'); sim = os.path.abspath(rest[i + 1]); del rest[i:i + 2]
    extra_defs = [x for x in rest if x.startswith('-D')]
    names = [x for x in rest if not x.startswith('-D')]
    tab = tests_from_ps1(sim)
    names = names or [k for k in tab if k.startswith('topv3')]
    os.makedirs(out, exist_ok=True)
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for name, res, n, hx, dt in ex.map(lambda x: run_one(tab[x], x, root, sim, out, extra_defs), names):
            print('%-12s %-14s %8d radku  %s  %5.1f min' % (name, res, n, hx[:16], dt / 60), flush=True)


if __name__ == '__main__':
    main()
