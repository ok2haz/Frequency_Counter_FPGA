"""x0_velke.csv: je chybne X0 = spravne X0 s VYNULOVANOU 2bitovou skupinou? Pro odchylku -k*2^j (j = 6 / 32)
porovna precteny X0[j+1:j] (ma byt 00) a spravny (X0 + k*2^j)[j+1:j] (ma byt k). Plus dolni bity u -64k."""
import csv, sys, collections
c = collections.Counter()
for r in csv.DictReader(open(sys.argv[1])):
    dx = int(r['dx'], 16); x0 = int(r['x0'], 16)
    d = dx - 250000
    if d < 0 and -d % 64 == 0 and -d <= 192:
        j, k = 6, -d // 64
    elif (dx >> 32) in (0xFF, 0xFE, 0xFD) and (dx & 0xFFFFFFFF) == 250000:
        j, k = 32, 0x100 - (dx >> 32)
    else:
        c['jine'] += 1
        continue
    true = x0 + k * (1 << j)
    rd = (x0 >> j) & 3; tr = (true >> j) & 3
    c[(j, 'cteno %d%d' % (rd >> 1, rd & 1), 'spravne %d%d' % (tr >> 1, tr & 1), 'k=%d' % k)] += 1
    if j == 6:
        c[(j, 'dolnich 6 b X0 = %02x' % (x0 & 0x3F) if False else 'dolni6', x0 & 0x3F == (true & 0x3F))] += 1
for k, v in sorted(c.items(), key=lambda x: str(x[0])):
    print(v, k)
