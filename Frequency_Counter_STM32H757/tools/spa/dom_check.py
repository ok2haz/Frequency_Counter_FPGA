"""Strukturalni kontrola vyextrahovaneho SPA HTML.

Kryje presne tu tridu chyb, kterou jsem u SPA delal opakovane a ktera se
NEPROJEVI ani pri `node --check`, ani v C prekladu: JS sahne na $('neco'),
co v markupu neexistuje -> az v prohlizeci TypeError a od te chvile se
prestane kreslit VSECHNO za tim. Bez prohlizece to jinak neodhalim.
"""
import io
import re
import sys

html = io.open(sys.argv[1], encoding='utf-8').read()
js = re.search(r'<script>(.*)</script>', html, re.S).group(1)
markup = html[:html.index('<script>')]

# ── ID v markupu (atributy jsou zamerne bez uvozovek — viz pravidla SPA) ──────
ids = re.findall(r"\bid=['\"]?([A-Za-z0-9_-]+)", markup)
dup = sorted({i for i in ids if ids.count(i) > 1})
idset = set(ids)

# ── ID, na ktera sahne JS ────────────────────────────────────────────────────
used = set(re.findall(r"\$\(\s*'([A-Za-z0-9_-]+)'\s*\)", js))
# dynamicka volani $(var) nedokazu overit — nahlas je, at se na ne kouknu okem
dyn = sorted(set(re.findall(r"\$\(\s*([a-zA-Z_][A-Za-z0-9_]*)\s*[,)]", js)))

missing = sorted(used - idset)
# ID vyrobena za behu (innerHTML) — vyhledej je i v JS retezcich
made = set(re.findall(r"id=([A-Za-z0-9_-]+)", js))
missing = [m for m in missing if m not in made]

# ── id predavana PROMENNOU (osy: `YT={freq:['ylFreq','gyFreq']}`) ────────────
# `$(var)` se overit neda, ale samotne retezce ano — a prave tyhle se pri
# prejmenovani grafu tise rozejdou s markupem.
axis_ids = set(re.findall(r"'((?:gy|gx|yl|xl)[A-Z]\w*)'", js))
axis_missing = sorted(a for a in axis_ids if a not in idset)

# ── kotvy navigace ───────────────────────────────────────────────────────────
anchors = re.findall(r"href='#([A-Za-z0-9_-]+)'", markup)
broken = sorted(a for a in anchors if a not in idset)

# ── parove tagy (hruba kontrola vyvazenosti div) ─────────────────────────────
opens = len(re.findall(r'<div\b', markup))
closes = len(re.findall(r'</div>', markup))

print('ID v markupu      : %d  (unikatnich %d)' % (len(ids), len(idset)))
print('ID pouzitych v JS : %d' % len(used))
print('kotev v navigaci  : %d' % len(anchors))
print('id os v JS        : %d' % len(axis_ids))
print('<div> / </div>    : %d / %d  %s'
      % (opens, closes, 'OK' if opens == closes else '<<< NEVYVAZENE'))

bad = 0
if dup:
    print('\nDUPLICITNI ID (prohlizec vrati prvni, druhy prvek je mrtvy):')
    for d in dup:
        print('   ' + d)
    bad += len(dup)
if missing:
    print('\nJS SAHA NA NEEXISTUJICI ID (TypeError -> prestane kreslit zbytek):')
    for m in missing:
        print('   ' + m)
    bad += len(missing)
if axis_missing:
    print('\nID OSY V JS BEZ PROTEJSKU V MARKUPU (osa se tise nevykresli):')
    for a in axis_missing:
        print('   ' + a)
    bad += len(axis_missing)
if broken:
    print('\nROZBITE KOTVY (odkaz nikam nevede):')
    for b in broken:
        print('   #' + b)
    bad += len(broken)
if opens != closes:
    bad += 1
if dyn:
    print('\nneoverene dynamicke $(promenna) - zkontrolovat okem: '
          + ', '.join(dyn))

print('\n' + ('NALEZENO PROBLEMU: %d' % bad if bad else 'struktura OK'))
sys.exit(1 if bad else 0)
