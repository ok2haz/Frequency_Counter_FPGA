#!/usr/bin/env python3
"""Spoustec simulaci FPGA bez PowerShellu: cte tabulku $tests ze sim/run.ps1 a pousti
iverilog/vvp se stejnymi volbami (vcetne -DSIM_AUTOCAL pro testy bez stmcal = $true).
Pouziti: run_sim.py <koren_FPGA_modulu> [jmeno ...]   (bez jmen = vsechny testy)"""
import os, re, subprocess, sys, time

root = os.path.abspath(sys.argv[1])
only = set(sys.argv[2:])
sim, src = os.path.join(root, 'sim'), os.path.join(root, 'src')
ps = open(os.path.join(sim, 'run.ps1'), encoding='utf-8').read()

# vyrizni blok $tests = @( ... ) az po radek s jedinou ')'
blk = ps[ps.index('$tests = @('):]
blk = blk[:re.search(r'\n\)\s*\n', blk).start()]
# odstran komentare, pak najdi vsechny @{ ... } (mohou byt pres vic radku)
blk = '\n'.join(l.split('#', 1)[0] if not l.strip().startswith('#') else '' for l in blk.splitlines())
entries = re.findall(r'@\{(.*?)\}', blk, re.S)

def arr(s, key):
    m = re.search(key + r'\s*=\s*@\((.*?)\)', s, re.S)
    return re.findall(r'"([^"]*)"|\'([^\']*)\'', m.group(1)) if m else []

tests = []
for e in entries:
    g = lambda k: (re.search(k + r'\s*=\s*"([^"]*)"', e) or [None, None])[1]
    t = dict(name=g('name'), top=g('top'), tb=g('tb'),
             rtl=[a or b for a, b in arr(e, 'rtl')], extra=[a or b for a, b in arr(e, 'extra')],
             defs=[a or b for a, b in arr(e, 'defs')], stmcal=bool(re.search(r'stmcal\s*=\s*\$true', e)))
    if t['name']:
        tests.append(t)

out_dir = os.environ.get('SIM_OUT', '/tmp')
fail = 0
for t in tests:
    if only and t['name'] not in only:
        continue
    vvp = os.path.join(out_dir, t['name'] + '.vvp')
    files = [os.path.join(sim, x) for x in t['extra']] + [os.path.join(src, x) for x in t['rtl']] + [os.path.join(sim, t['tb'])]
    ac = [] if t['stmcal'] else ['-DSIM_AUTOCAL']
    cmd = ['iverilog', '-g2012', '-I', sim, '-s', t['top']] + ac + t['defs'] + ['-o', vvp] + files
    t0 = time.time()
    print('--- Test %s ---' % t['name'], flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print('  COMPILE FAIL\n' + r.stdout + r.stderr, flush=True); fail += 1; continue
    if os.environ.get('COMPILE_ONLY'):
        print('  COMPILE OK', flush=True); continue
    r = subprocess.run(['vvp', '-n', vvp], capture_output=True, text=True, cwd=out_dir)
    for line in r.stdout.splitlines():
        print('  ' + line)
    ok = 'PASS' in r.stdout
    print('  -> %s (%.0f s)' % ('PASS' if ok else 'FAIL', time.time() - t0), flush=True)
    fail += 0 if ok else 1
print('\nVSE PROSLO' if fail == 0 else '\n%d test(u) selhalo' % fail)
sys.exit(1 if fail else 0)
