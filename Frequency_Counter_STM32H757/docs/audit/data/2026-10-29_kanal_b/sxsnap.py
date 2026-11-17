"""Snimky segmentu z uart.log (fpga v3 seg): normalni segmenty N = 249997 bez CS -> SX a ST proti referenci.
Rozlozi odchylku SX na horni (nad 2^32) a spodni cast; totez ST."""
import re, sys, collections

SX_REF = 0x74697CE8E
pat = re.compile(r'seg (\d+) ch 0x(\w+) fl 0x(\w+) N (\d+) X0 (\w+) T0 (\w+) SX (\w+) ST (\w+)')
seen = {}
for line in open(sys.argv[1], errors='replace'):
    m = pat.search(line)
    if m:
        seg = int(m.group(1)); seen[(seg, m.group(5))] = m.groups()
print('unikatnich snimku', len(seen))
hi = collections.Counter(); lo_ok = 0; lo_bad = 0; st_hi = collections.Counter(); cs = collections.Counter()
for g in seen.values():
    seg, ch, fl, n, x0, t0, sx, st = g
    n = int(n); sx = int(sx, 16); st = int(st, 16); fl = int(fl, 16)
    if fl & 0x08:
        cs[sx >> 32] += 1
        continue
    if n != 249997:
        continue
    d = sx - SX_REF
    if d % (1 << 32) == 0:
        lo_ok += 1; hi[d >> 32] += 1
    else:
        lo_bad += 1
    st_hi[st >> 32] += 1
print('normalni N=249997: spodnich 32 b SX presne %d, jinak %d' % (lo_ok, lo_bad))
print('  odchylka SX v nasobcich 2^32:', sorted(hi.items()))
print('  ST >> 32:', sorted((hex(k), v) for k, v in st_hi.items()))
print('CS segmenty SX >> 32:', sorted(cs.items()))
