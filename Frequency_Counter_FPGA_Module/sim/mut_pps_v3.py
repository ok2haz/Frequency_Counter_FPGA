#!/usr/bin/env python3
"""Negativni kontroly sim/tb_pps_v3.sv (retez GPS 1PPS v3: pps_ides3 + pps_cap3, FW 0x0506): kazda mutace RTL (docasna
kopie src/v3_core.v ve vystupnim adresari, repo se nemeni) musi test SHODIT. Jedna simulace ~2 s, bezi paralelne.
Nejdriv se overi, ze kazda kotva je v kodu PRAVE JEDNOU (vsechny mutace, pred prvni simulaci), a ze nemutovany RTL
projde; mutace oznacene jako ekvivalentni se pousti taky, ale jejich NEZACHYCENI neni chyba (zduvodneni u polozky).
Pouziti: python3 sim/mut_pps_v3.py <koren FPGA modulu> <vystupni adresar> [jmeno ...] [-j N]"""
import os, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

root = os.path.abspath(sys.argv[1])
out = os.path.abspath(sys.argv[2])
args = sys.argv[3:]
jobs = max(1, (os.cpu_count() or 4) - 2)
if '-j' in args:
    i = args.index('-j'); jobs = int(args[i + 1]); del args[i:i + 2]
only = set(args)
sim, src = os.path.join(root, 'sim'), os.path.join(root, 'src')
TIMEOUT = 600                       # [s] na jednu simulaci (normalne ~2 s)

# (jmeno, [(puvodni text, mutace), ...], ekvivalentni) -- vse v src/v3_core.v
M = [
    # --- pps_ides3: skladani f_ts s vypujckou bw (FW 0x0506) ---
    ('bw: bez vypujcky ve fix 2 (bity 34:22)',
     [("            {bw, f_ts[34:22]} <= {1'b0, f_ts[34:22]} - {13'd0, bw};",
       "            {bw, f_ts[34:22]} <= {1'b0, f_ts[34:22]};")], False),
    ('bw: bez vypujcky ve fix 3 (bity 47:35)',
     [("            f_ts[47:35] <= f_ts[47:35] - {12'd0, bw};", "            f_ts[47:35] <= f_ts[47:35];")], False),
    # EKVIVALENTNI: prijem (f_ts <= tick) je vzdy 3 takty za hl (p_tgl -> 3 FF t_s -> detekce; i pri falesne hrane
    # po uvolneni), tedy f_ts[21:14] - hl = 3 (mod 256) a rovnost, kde se '<=' lisi od '<', nenastane. Na desce muze
    # byt zpozdeni o takt jine (faze PCLK), ale nikdy 0 ani 256 taktu.
    ('bw: fix 1 s <= misto <', [("            bw  <= (f_ts[21:14] < hl);", "            bw  <= (f_ts[21:14] <= hl);")], True),
    ('bw: fix 1 s > misto <', [("            bw  <= (f_ts[21:14] < hl);", "            bw  <= (f_ts[21:14] > hl);")], False),
    ('bw: fix 1 s >= misto <', [("            bw  <= (f_ts[21:14] < hl);", "            bw  <= (f_ts[21:14] >= hl);")], False),
    ('hl bez t_rel', [("        hl  <= p_ts[10:3] + t_rel;", "        hl  <= p_ts[10:3];")], False),
    ('f_ts[21:14] neskopirovano z hl', [("            f_ts[21:14]       <= hl;\n", "")], False),
    ('poloha p_ts[2:0] = 0', [("if (hit) begin p_ts <= {pcnt, pos};", "if (hit) begin p_ts <= {pcnt, 3'd0};")], False),
    ('lastw ignorovano (rozhrani slov)', [("((i == 0) ? ~lastw : ~w[i-1])", "((i == 0) ? 1'b1 : ~w[i-1])")], False),
    ('epoch se nezvysi', [("t_rel <= tick[7:0]; epoch <= epoch + 4'd1;", "t_rel <= tick[7:0]; epoch <= epoch;")], False),
    # na vystupu pps_cap3 by byla ekvivalentni (fs 3 FF + fok cte f_ts nejdriv 4 takty po f_tgl, to uz je fix 3 hotovy);
    # chyti ji kontrola rozhrani pps_ides3 (f_ts se po preklopeni f_tgl nesmi zmenit), jen kdyz se fix 3 opravdu meni
    # f_ts[47:35] = scenare b) a c) s vypujckou
    ('f_tgl uz ve fix 2 (pred dokoncenim)',
     [("            fix <= 3'd3;\n", "            fix <= 3'd3;\n            f_tgl <= ~f_tgl;\n"),
      ("            fix   <= 3'd0;\n            f_tgl <= ~f_tgl;\n", "            fix   <= 3'd0;\n")], False),
    # --- pps_cap3 ---
    ('seq_gt1 <= 1', [("seq_nz <= 1'b1; seq_gt1 <= seq_nz;", "seq_nz <= 1'b1; seq_gt1 <= 1'b1;")], False),
    ('pts_p se neposune', [("            pts_p <= pts_l; pfl_p <= pfl_l; d_p <= d_l;", "            pfl_p <= pfl_l; d_p <= d_l;")], False),
    ('saturace d_l prohozena', [("dhi ? 8'h7F : dlo ? 8'h80 : dd[7:0]", "dhi ? 8'h80 : dlo ? 8'h7F : dd[7:0]")], False),
    # navic (sonda testu): nofine bez f_lock, jemny cas bez kontroly stari, zverejneni bez straze win != 0
    ('nofine bez f_lock', [("pfl_l <= 8'd1; d_l <= 8'd0; nofine <= f_lock;", "pfl_l <= 8'd1; d_l <= 8'd0; nofine <= 1'b1;")], False),
    ('fok bez kontroly stari (fage 31)',
     [("fok <= fa && (fage != 5'd31) && (fage != 5'd0);", "fok <= fa && (fage != 5'd0);")], False),
    ('zverejneni bez straze win != 0',
     [("else if (pend && win != 5'd0 && (fok || wok)) begin", "else if (pend && (fok || wok)) begin")], False),
    # --- tri mutace 1PPS z mut_top_v3.py (stejne kotvy; '1PPS: bez t_rel' = 'hl bez t_rel' vyse) ---
    ('IDES8: obracene poradi bitu', [("    parameter Q0_FIRST = 1 ", "    parameter Q0_FIRST = 0 ")], False),
    ('1PPS: bez t_rel', [("        hl  <= p_ts[10:3] + t_rel;", "        hl  <= p_ts[10:3];")], False),
    ('1PPS: jemny cas se nepouzije', [("pts_l <= f_ts; pfl_l <= 8'd3;", "pts_l <= {c_tick, 14'd0}; pfl_l <= 8'd3;")], False),
]
FN = 'v3_core.v'
TB = 'tb_pps_v3.sv'


def dname(k, name):                 # poradove cislo: jmena se lisi i jen operatorem ('<=', '>', '>=')
    return os.path.join(out, '%02d_' % k + ''.join(c if c.isalnum() else '_' for c in name).strip('_'))


def sim1(k, name, text):
    d = dname(k, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    p = os.path.join(d, FN)
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    vvp = os.path.join(d, 't.vvp')
    cmd = ['iverilog', '-g2012', '-I', sim, '-s', 'tb_pps_v3', '-o', vvp,
           os.path.join(sim, 'gowin_models.v'), p, os.path.join(sim, TB)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return 'PREKLAD SELHAL', (r.stdout + r.stderr).strip().replace('\n', ' ')[:200], 0.0
    t0 = time.time()
    try:
        r = subprocess.run(['vvp', '-n', vvp], capture_output=True, text=True, cwd=d, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return 'CASOVY LIMIT', '', time.time() - t0
    dt = time.time() - t0
    with open(os.path.join(d, 'vystup.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(r.stdout)
    first = next((l.strip() for l in r.stdout.splitlines() if 'CHYBA' in l or 'FAIL' in l), '')
    return ('PASS' if 'PASS' in r.stdout else 'SELHAL'), first, dt


with open(os.path.join(src, FN), encoding='utf-8') as f:
    orig = f.read()
sel = [m for m in M if not only or m[0] in only]
if only - {m[0] for m in M}:
    sys.exit('neznama mutace: %s' % ', '.join(sorted(only - {m[0] for m in M})))
# 1) kotvy: kazda prave jednou, pro VSECHNY vybrane mutace, nez se spusti prvni simulace
bad = []
for name, reps, _ in sel:
    for a, _b in reps:
        if orig.count(a) != 1:
            bad.append('%s: kotva %dx: %r' % (name, orig.count(a), a[:60]))
if bad:
    print('\n'.join(bad))
    sys.exit('KOTVY NESEDI (%d) -- zadna simulace nespustena' % len(bad))
os.makedirs(out, exist_ok=True)
# 2) nemutovany RTL musi projit
res, first, dt = sim1(0, 'bez mutace', orig)
print('%-42s %-16s %5.1f s  %s' % ('(bez mutace)', res, dt, first), flush=True)
if res != 'PASS':
    sys.exit('nemutovany RTL neprojde -- mutace nemaji smysl')


def run(km):
    k, (name, reps, ekv) = km
    s = orig
    for a, b in reps:
        s = s.replace(a, b)
    res, first, dt = sim1(k, name, s)
    if res == 'SELHAL':
        verdict = 'ZACHYCENO' + (' (cekana ekv.!)' if ekv else '')
    elif res == 'PASS':
        verdict = 'EKVIVALENTNI' if ekv else '!!! NEZACHYCENO'
    else:
        verdict = res
    return name, verdict, first, dt


nbad = 0
with ThreadPoolExecutor(max_workers=jobs) as ex:
    for name, verdict, first, dt in ex.map(run, enumerate(sel, 1)):
        print('%-42s %-16s %5.1f s  %s' % (name, verdict, dt, first[:110]), flush=True)
        nbad += 0 if verdict.startswith('ZACHYCENO') or verdict == 'EKVIVALENTNI' else 1
print('\nvsechny mutace zachyceny (ekvivalentni zduvodneny)' if nbad == 0 else '\n%d mutaci NEZACHYCENO' % nbad)
sys.exit(1 if nbad else 0)
