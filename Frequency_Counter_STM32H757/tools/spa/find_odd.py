"""Najde v oblasti SPA_HTML radky s lichym poctem NEescapovanych uvozovek —
tim se rozbiji parovani literalu pri extrakci SPA."""
import io
import sys

p = sys.argv[1]
lines = io.open(p, encoding='utf-8', errors='replace').read().split('\n')
start = next(i for i, l in enumerate(lines) if 'SPA_HTML[]' in l)
print('SPA zacina na radku', start + 1)

bad = 0
for i in range(start, len(lines)):
    l = lines[i]
    # !! Konec deklarace je radek konciciho literalu, tedy `";` — NE pouhe `;`.
    # Rozbity radek (napr. `... ||w;` bez uzaviraci uvozovky) jinak podminku splni
    # a UREZE rozsah kontroly, takze zbytek souboru se vubec neproveri. Presne to
    # se stalo 2026-09-06: find_odd hlasil nula lichych radku, zatimco literal byl
    # rozbity o 1600 radku niz. Kontrola se nesmi dat umlcet tou vadou, kterou hleda.
    if i > start + 5 and l.rstrip().endswith('";'):
        print('konec deklarace na radku', i + 1)
        break
    # spocitej uvozovky, ktere NEjsou escapovane zpetnym lomitkem
    n = 0
    j = 0
    while j < len(l):
        if l[j] == '\\':
            j += 2
            continue
        if l[j] == '"':
            n += 1
        j += 1
    if n % 2:
        bad += 1
        print('  LICHY radek %d: %s' % (i + 1, l[:120]))
        if bad > 8:
            break
print('radku s lichym poctem uvozovek:', bad)
