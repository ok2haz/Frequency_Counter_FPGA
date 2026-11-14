"""t_hist_json.py - host test odpovedi /api/log z pyramidy CM4 (TODO #251): vytahne SKUTECNE funkce build_log_json_hp / jputf / jinit
z CM4/LWIP/App/httpd_min.c a fmt_scpi_hz_d z CM7/Core/Src/scpi.c, prelozi je na PC a overi, ze vystup je platny JSON ve tvaru datalogu
(9 poli na bod, min <= f <= max). Spusteni z korene projektu: python tests/host/t_hist_json.py [prikaz kompilatoru]
(vychozi: python -m ziglang cc; viz CLAUDE.md, Windows bez gcc)."""
import re, subprocess, json, sys, os, tempfile

def func(src, header_regex, end='\n}\n'):
    m = re.search(header_regex, src, re.M)
    assert m, header_regex
    i = m.start()
    j = src.index(end, i) + len(end)
    return src[i:j]

h = open('CM4/LWIP/App/httpd_min.c', encoding='utf-8').read()
s = open('CM7/Core/Src/scpi.c', encoding='utf-8').read()

parts = []
parts.append(func(h, r'^typedef struct \{ char \*p; size_t cap, used; uint8_t ovf; \} jbuf_t;', '\n'))
parts.append(func(h, r'^static void jinit\(', '\n'))
parts.append(func(h, r'^static void jputf\('))
parts.append(func(s, r'^void fmt_scpi_hz_d\('))
parts.append(func(h, r'^static size_t build_log_json_hp\('))

body = '\n'.join(parts)
test = r'''
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include "hist_pyr.h"
''' + body + r'''
int main(void)
{
    static hp_t h;
    hp_reset(&h);
    for (uint32_t s = 0; s < 3000u; s++)
        hp_feed(&h, s, 1700000000u + s, 1e7 + 0.001 * (double)(s % 50u), (s < 1500u) ? 4512 : HP_EMPTY16, -150, 1920, (s % 2u) ? 3090u : 0u);
    hp_pt_t out[48];
    int n = hp_query(&h, 0u, 60u, 30u, out);
    static char buf[6144];
    size_t len = build_log_json_hp(buf, sizeof buf, out, n, hp_span_s(&h));
    fwrite(buf, 1, len, stdout);
    return len ? 0 : 1;
}
'''
tmp = tempfile.mkdtemp()
csrc, exe = os.path.join(tmp, 't.c'), os.path.join(tmp, 't.exe')
open(csrc, 'w', encoding='utf-8').write(test)
r = subprocess.run((sys.argv[1:] or [sys.executable, '-m', 'ziglang', 'cc']) + ['-std=gnu11', '-O1', '-ICM7/Core/Inc', csrc, '-lm', '-o', exe], capture_output=True, text=True)
print(r.stderr[:1500])
assert r.returncode == 0
o = subprocess.run([exe], capture_output=True)
print('exit', o.returncode, 'bytes', len(o.stdout))
j = json.loads(o.stdout.decode())
print({k: j[k] for k in j if k != 'p'}, 'body', len(o.stdout), '<6144')
print(j['p'][0]); print(j['p'][-1])
assert len(j['p']) == j['n'] == 30
for p in j['p']:
    assert len(p) == 9, p
    assert p[0] > 1700000000
    assert p[7] <= p[1] <= p[8], p
print('JSON OK')
