# -*- coding: utf-8 -*-
"""Uklidi ne-ASCII a escapovane uvozovky UVNITR literalu SPA_HTML.

SPA je zamerne ciste ASCII a bez uvozovek (viz pravidla v CLAUDE.md).

!! Puvodni verze mela PEVNOU TABULKU nahrad — a nova znacka (emoji) ji proste
proklouzla. Stejna trida chyby jako u extraktoru: kontrola postavena na vyctu
toho, co znam, misto na vlastnosti, kterou chci. Tabulka proto slouzi uz jen
k HEZKEMU prepisu bezneho textu; cokoli mimo ni se odstrani tak jako tak
a nahlasi se, aby si toho clovek vsiml.
"""
import io
import sys
import unicodedata

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

p = sys.argv[1]
lines = io.open(p, encoding='utf-8', newline='').read().split('\r\n')
crlf = True
if len(lines) == 1:
    lines = lines[0].split('\n')
    crlf = False

start = next(i for i, l in enumerate(lines) if 'SPA_HTML[]' in l)
# !! Konec deklarace je radek konciciho literalu, tedy `";` — NE pouhe `;`.
# Rozbity radek (napr. `... ||w;` bez uzaviraci uvozovky) jinak podminku splni
# a UREZE rozsah kontroly, takze zbytek souboru se vubec neproveri. Presne to
# se stalo 2026-09-06: find_odd hlasil nula lichych radku, zatimco literal byl
# rozbity o 1600 radku niz. Kontrola se nesmi dat umlcet tou vadou, kterou hleda.
end = next((i for i in range(start + 5, len(lines))
            if lines[i].rstrip().endswith('";')), None)
if end is None:
    raise SystemExit('konec deklarace (radek koncici na ";) NENALEZEN'
                     ' - literal je rozbity; oprav ho drive nez uklizis znaky')
Q = chr(34)
BS = chr(92)

REP = {
    '⚠️': '!! ', '⚠': '!! ',
    '·': '|', '—': '-', '–': '-', '→': '->',
    '„': '', '“': '', '”': '', '‘': '', '’': '',
    'í': 'i', 'é': 'e', 'á': 'a', 'č': 'c',
    'ř': 'r', 'ž': 'z', 'š': 's', 'ě': 'e',
    'ú': 'u', 'ů': 'u', 'ý': 'y', 'ť': 't', 'ď': 'd',
    'ó': 'o', 'ň': 'n',
    BS + Q: '',                      # escapovana uvozovka -> pryc (do HTML nepatri)
}

n = 0
dropped = {}
for i in range(start, end + 1):
    l = lines[i]
    if Q not in l:
        continue                     # C komentar mimo literal necháme
    o = l
    for a, b in REP.items():
        l = l.replace(a, b)
    # cokoli, co v tabulce neni: zkus rozlozit diakritiku, jinak zahod
    if any(ord(c) > 126 for c in l):
        out = []
        for c in l:
            if ord(c) <= 126:
                out.append(c)
                continue
            d = unicodedata.normalize('NFKD', c)
            d = ''.join(x for x in d if ord(x) <= 126)
            dropped[c] = dropped.get(c, 0) + 1
            out.append(d)
        l = ''.join(out)
    if l != o:
        n += 1
        lines[i] = l

io.open(p, 'w', encoding='utf-8', newline='').write(
    ('\r\n' if crlf else '\n').join(lines))
print('uklizeno radku:', n)
if dropped:
    print('!! znaky MIMO tabulku (odstraneny genericky) - zkontroluj text:')
    for c, k in sorted(dropped.items(), key=lambda x: -x[1]):
        print('   U+%04X %s x%d' % (ord(c), unicodedata.name(c, '?'), k))
