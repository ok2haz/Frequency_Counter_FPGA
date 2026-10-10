"""Sesije literaly rozbite tim, ze se `\\n` zapsalo jako skutecny konec radku.
Rozbity tvar:   "  neco;            <- radek nekonci uvozovkou
                "                   <- samostatna uvozovka
Spravny tvar:   "  neco;\\n"
"""
import io
import sys

p = sys.argv[1]
lines = io.open(p, encoding='utf-8', newline='').read().split('\n')
Q = chr(34)
BS = chr(92)

out = []
i = 0
fixed = 0
while i < len(lines):
    l = lines[i]
    nxt = lines[i + 1] if i + 1 < len(lines) else None
    # radek zacina uvozovkou, NEkonci uvozovkou, a dalsi radek je jen uvozovka
    if (l.lstrip().startswith(Q) and not l.rstrip().endswith(Q)
            and nxt is not None and nxt.strip() == Q):
        out.append(l.rstrip() + BS + 'n' + Q)
        fixed += 1
        i += 2
        continue
    out.append(l)
    i += 1

io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(out))
print('sesito rozbitych literalu:', fixed)
