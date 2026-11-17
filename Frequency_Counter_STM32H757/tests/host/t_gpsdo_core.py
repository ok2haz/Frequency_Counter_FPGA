"""Host test regulatoru GPSDO (FLL + PLL 2. radu nad modelem OCXO): jadro (gp_core_t, core_start_fll, core_step) a cast
gpsdo_selftest se st_run (3 skutecne strmosti K ruzne od vychoziho odhadu, spatne znamenko K, nedostatecny rozsah EFC)
se VYJMOU ze zdroje (CM7/Core/Src/freertos_task_fpga.c) a prelozi na PC. Testuje se tedy text, ktery jde do obrazu.

Vznikl s F-0293 (vychozi K 3,4e-11 -> 3,85e-11): do te doby slo `gpsdo test` overit jen na desce po flashi.

Pouziti:  python tests/host/t_gpsdo_core.py <vystupni adresar> [K_vychozi]
  K_vychozi (volitelne) prepise GPSDO_KDAC_DEFAULT -- kontrola, ze test umi selhat (napr. 1.6e-10: odhad 5x mimo
  skutecnou strmost, FLL ho uz nesmi prijmout a do 4000 s se nezamkne).
Prekladac: promenna prostredi CC (napr. "python -m ziglang cc"), vychozi "cc". Cesty se berou od umisteni skriptu.
Navratovy kod 0 = PASS.
"""
import io
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
if len(sys.argv) < 2:
    sys.exit('pouziti: python tests/host/t_gpsdo_core.py <vystupni adresar> [K_vychozi]')
OUT = os.path.abspath(sys.argv[1])
KOVR = sys.argv[2] if len(sys.argv) > 2 else None
os.makedirs(OUT, exist_ok=True)
CC = shlex.split(os.environ.get('CC', 'cc'), posix=(os.name != 'nt'))

src = io.open(os.path.join(ROOT, 'CM7', 'Core', 'Src', 'freertos_task_fpga.c'), encoding='utf-8').read()


def cut(start, end_after):
    """text od kotvy `start` po konec funkce (prvni '\\n}\\n' za kotvou `end_after`)"""
    a = src.index(start)
    return src[a:src.index('\n}\n', src.index(end_after, a)) + 3]


defs = src[src.index('#define GPSDO_KDAC_DEFAULT'):src.index('/* ── Audit 2026-10-20 (slabina 3): odhad sumu odectu')]
if KOVR is not None:
    defs = re.sub(r'#define GPSDO_KDAC_DEFAULT\s+\S+', '#define GPSDO_KDAC_DEFAULT  (' + KOVR + ')', defs)
kdef = re.search(r'#define GPSDO_KDAC_DEFAULT\s+(\S+)', defs).group(1)
core = cut('/* ── Jadro regulatoru (cista logika', 'static int core_step')
model = cut('/* ── Selftest: regulator nad modelem OCXO', 'static int st_run')
a = src.index('    static const double KT[3]')
block = src[a:src.index('    ok = ok && st_tau_advise();', a)]
kt = re.search(r'static const double KT\[3\] = \{([^}]*)\}', block).group(1)

c = '\n'.join([
    '#include <stdio.h>', '#include <string.h>', '#include <math.h>', '#include <stdint.h>', '#include "gpsdo.h"',
    defs, core, model,
    'int main(void)', '{', block,
    '    {   /* vypis: cas zamceni, zbytkova odchylka a faze pro kazdou strmost (stejna semena jako blok selftestu) */',
    '        static const double KD[3] = {' + kt + '};',
    '        for (int i = 0; i < 3; i++) {',
    '            uint32_t l2; double y2, x2; int d2;',
    '            s_st_rnd = 0x9E3779B9u + (uint32_t)i;',
    '            int k2 = st_run(GPSDO_KDAC_DEFAULT, KD[i], 12000u, &l2, &y2, &x2, &d2);',
    '            printf("K skutecne %.2e (odhad %.2e, pomer %.2f): LOCK %d za %u s, y %.2e, faze %.0f ns, divergence %d\\n",',
    '                   KD[i], GPSDO_KDAC_DEFAULT, KD[i] / GPSDO_KDAC_DEFAULT, k2, (unsigned)l2, y2, x2, d2);',
    '        }',
    '    }',
    '    printf("gpsdo_selftest (regulator, K vychozi %s): %s\\n", "' + kdef + '", ok ? "PASS" : "FAIL");',
    '    return ok ? 0 : 1;', '}', ''])
p = os.path.join(OUT, 't_gpsdo_core.c')
io.open(p, 'w', encoding='utf-8', newline='\n').write(c)
exe = os.path.join(OUT, 't_gpsdo_core.exe')
if os.path.exists(exe):
    os.remove(exe)                       # L-0286: nikdy nespoustet starou binarku
r = subprocess.run(CC + ['-std=c11', '-O2', '-Wall', '-Wextra', '-I' + os.path.join(ROOT, 'CM7', 'Core', 'Inc'), p, '-lm', '-o', exe],
                   capture_output=True, text=True)
if r.returncode or r.stderr.strip():
    print(r.stderr[:3000])
if r.returncode:
    sys.exit('PREKLAD SELHAL')
t = subprocess.run([exe], capture_output=True, text=True, timeout=300)
print(t.stdout.strip())
sys.exit(t.returncode)
