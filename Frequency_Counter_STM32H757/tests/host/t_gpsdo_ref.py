"""Host test jadra nejistoty reference (F-0291) a brany kvality GNSS (F-0290): gp_ref_core, gp_gnss_step a jejich bloky
gpsdo_selftest se VYJMOU ze zdroje
(CM7/Core/Src/freertos_task_fpga.c) a prelozi na PC spolu s mp_ref_ppb (meas_present.h). Testuje se tedy text,
ktery jde do obrazu, ne kopie.

Pouziti:  python tests/host/t_gpsdo_ref.py <vystupni adresar>
Prekladac: promenna prostredi CC (napr. "python -m ziglang cc"), vychozi "cc". Cesty se berou od umisteni skriptu.
"""
import io
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else None
if OUT is None:
    sys.exit('pouziti: python tests/host/t_gpsdo_ref.py <vystupni adresar>')
os.makedirs(OUT, exist_ok=True)
CC = shlex.split(os.environ.get('CC', 'cc'), posix=(os.name != 'nt'))

src = io.open(os.path.join(ROOT, 'CM7', 'Core', 'Src', 'freertos_task_fpga.c'), encoding='utf-8').read()
a = src.index('#define GP_REF_RW_LAG')
core = src[a:src.index('\n}\n', src.index('static int gp_ref_core', a)) + 3]
a2 = src.index('    {   static int32_t  tx[360];')
block = src[a2:src.index('    return ok;\n}', a2)]
lock_y = re.search(r'#define GPSDO_FLL_LOCK_Y\s+\S+', src).group(0)
# F-0290: brana kvality GNSS (gp_gnss_step) a jeji blok selftestu
a3 = src.index('typedef struct { uint8_t ok, run; } gp_gnss_t;')
gnss = src[a3:src.index('\n}\n', src.index('static int gp_gnss_step', a3)) + 3]
a4 = src.index('    {   gp_gnss_t q = { 0u, 0u };')
gblock = src[a4:src.index('\n    }\n', a4) + 7]

c = '\n'.join([
    '#include <stdio.h>', '#include <string.h>', '#include <math.h>', '#include "meas_present.h"',
    lock_y, core, gnss,
    'int main(void)', '{', '    int ok = 1;', block, gblock,
    '    if (!ok) printf("blok selftestu ze zdroje: FAIL\\n");',
    '    {   static int32_t x[40]; static uint32_t cq[40]; double u, d, yo, rw;',
    '        for (int i = 0; i < 40; i++) { x[i] = 1000 * i; cq[i] = 24970u << 16; }',
    '        (void)gp_ref_core(x, cq, 29, 3.4e-11, 1000.0, &u, &d, &yo, &rw); /* pod 30 bodu: nic zmereno */',
    '        ok = ok && u == 0.0 && yo < 0.0;',
    '        (void)gp_ref_core(x, cq, 40, 3.4e-11, 20000.0, &u, &d, &yo, &rw); /* okno delsi nez usek: cely usek, 0,1 ns/s */',
    '        ok = ok && yo > 0.0999 && yo < 0.1001;',
    '        (void)gp_ref_core(x, cq, 40, 3.4e-11, 0.0, &u, &d, &yo, &rw);    /* tau 0: nejkratsi okno, bez deleni nulou */',
    '        ok = ok && yo > 0.0999 && yo < 0.1001;',
    '    }',
    '    {   mp_ref_in_t r; uint8_t m = 0u; double v;                             /* mp_ref_ppb: prebytek sklonu nad 2 x ADEV */',
    '        memset(&r, 0, sizeof r); r.state = GPSDO_LOCK; r.locked = 1u;',
    '        ok = ok && mp_ref_ppb(&r, &m) == MP_REF_PPB_LOCK_ENTRY && m == 0u;',
    '        r.entry_ppb = 0.3; ok = ok && mp_ref_ppb(&r, &m) == 0.3 && m == 0u;',
    '        r.lock_u_ppb = 0.013; r.lock_yoff_ok = 1u; r.lock_yoff_ppb = 0.02;',
    '        ok = ok && mp_ref_ppb(&r, &m) == 0.013 && m == 1u;',
    '        r.lock_yoff_ppb = 0.3; v = mp_ref_ppb(&r, &m); ok = ok && v > 0.2991 && v < 0.2993;',
    '        r.state = GPSDO_HOLD; r.entry_ppb = 0.3; r.entry_meas = 1u; r.hold_drift_ppb_h = 0.4; r.state_s = 3600u;',
    '        v = mp_ref_ppb(&r, &m); ok = ok && m == 0u && v > 0.5048 && v < 0.5050;     /* prochazka nezmerena: nahrada 0,07 */',
    '        r.hold_rw_ppb_sqrth = 0.5; r.state_s = 4u * 3600u;                         /* F-0292: sqrt(0,3^2 + 0,7^2*4 + 1,6^2) */',
    '        v = mp_ref_ppb(&r, &m); ok = ok && m == 1u && v > 2.1470 && v < 2.1472;',
    '        r.entry_meas = 0u; v = mp_ref_ppb(&r, &m); ok = ok && m == 0u;             /* vstupni udaj z nahrady: neni to mereni */',
    '    }',
    '    printf("%s\\n", ok ? "PASS" : "FAIL");', '    return ok ? 0 : 1;', '}', ''])
p = os.path.join(OUT, 't_gpsdo_ref.c')
io.open(p, 'w', encoding='utf-8', newline='\n').write(c)
exe = os.path.join(OUT, 't_gpsdo_ref.exe')
r = subprocess.run(CC + ['-std=c11', '-O2', '-Wall', '-Wextra', '-I' + os.path.join(ROOT, 'CM7', 'Core', 'Inc'), p, '-lm', '-o', exe],
                   capture_output=True, text=True)
if r.returncode or r.stderr.strip():
    print(r.stderr[:3000])
if r.returncode:
    sys.exit('PREKLAD SELHAL')
t = subprocess.run([exe], capture_output=True, text=True, timeout=120)
print(t.stdout.strip())
sys.exit(t.returncode)
