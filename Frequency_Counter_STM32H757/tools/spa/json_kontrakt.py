# -*- coding: utf-8 -*-
"""Hranice JSON: cte SPA pole, ktera server opravdu emituje?

Proc to existuje (audit modulu 15, nalezy F-0078 a F-0079): SPA a `build_*_json`
jsou v TEMZE souboru a v temze obrazu, presto se rozesly. `drawTfom` cetl
`gps.valid`, ktere /api/state nikdy neposilalo, takze karta HOLDOVER hlasila
`NO LOCK` i pri 3D fixu; `push('ns')` cetl `gps.nsat`, jenze `nsat` lezi
o uroven vys a v bloku `gps` je `num_sat` — karta KVALITA GPS zustala navzdy
prazdna. Ani jedno nic nenahlasilo: v JS je cteni neexistujiciho pole
`undefined`, ne chyba. Zadny jiny krok `check.py` na to nedosahne (meri literal,
DOM a syntaxi, ne VYZNAM dat).

    python tools/spa/json_kontrakt.py [cesta/k/httpd_min.c]

⚠️ ROZSAH KONTROLY (radeji uzky a tichy nez siroky a zasumeny):
  - `LAST.*` kdekoli a `s.*` JEN ve funkcich, kde `s` je parametr se stavem
    (v `spec()` je `s` lokalni objekt popisu grafu — ten se preskoci);
  - vnorene objekty pres pomocnou promennou (`var g=s.gps`) JEN v ramci te
    funkce, kde vznikla — jinak by se `g` pletlo s `var g=$('skg')` v drawSky.
Co kontrola NEHLIDA: dynamicky skladane klice a pole ctena mimo tyhle cesty.

Konci nenulovym kodem, kdyz klient cte pole, ktere server neemituje.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
DEFAULT = os.path.join(ROOT, 'CM4', 'LWIP', 'App', 'httpd_min.c')

# Pomocniky, ktere emituji `"jmeno":hodnota`. Kdyz pribude dalsi, patri sem —
# jinak kontrola jeho klice nezna a zacne hlasit falesne nalezy.
EMIT = r'(?:jnum_u|jnum_i|jnum_d|jnum_hz|jnum_c100|jnum_e7|jbool|jstr)\s*\(\s*&j\s*,\s*"([A-Za-z_0-9]+)"'
# ⚠️ MUSI vzit VSECHNY sousedni literaly jednoho volani, ne jen prvni: `jputf` se
# bezne sklada z dvou radku (`"\"math\":{...,"  "\"m\":%s,...},"`) a kdyz se cte
# jen prvni, zavirajici `}` se nikdy neuvidi -> vsechny dalsi klice se pak tvari
# jako vnorene do `math`. (Prvni verze teto kontroly presne timhle hlasila 17
# falesnych nalezu.)
JPUTF = r'jputf\s*\(\s*&j\s*,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)'

# Vlastnosti JS/DOM — nejsou to pole odpovedi.
SKIP = set('''length push shift slice sort indexOf join concat charAt substring
toFixed toExponential toString textContent innerHTML className style value
setAttribute removeAttribute getAttribute appendChild removeChild firstChild
insertBefore lastChild childNodes href download disabled tagName readyState
close onopen onmessage onerror display left top width height background
map filter forEach hasOwnProperty'''.split())


def split_spa(lines):
    lo = next(i for i, l in enumerate(lines, 1) if 'static const char SPA_HTML[]' in l)
    hi = next(i for i, l in enumerate(lines, 1) if i > lo and l.rstrip().endswith('";'))
    return '\n'.join(lines[:lo - 1] + lines[hi:]), '\n'.join(lines[lo:hi - 1])


def server_keys(server):
    """Cesty klicu VCETNE vnoreni (`gps.num_sat`). Ctou se v poradi emise."""
    paths, objs = set(), set()
    for fn in re.finditer(r'static size_t build_\w+_json\([^)]*\)\s*\{(.*?)\n\}', server, re.S):
        scope = []
        for m in re.finditer(EMIT + '|' + JPUTF, fn.group(1)):
            if m.group(1):                                  # pomocnik s klicem
                paths.add('.'.join(scope + [m.group(1)]))
                continue
            fmt = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)))
            for t in re.finditer(r'\\"([A-Za-z_0-9]+)\\":\s*\{|\\"([A-Za-z_0-9]+)\\":|\}',
                                 fmt):                      # format v poradi znaku
                if t.group(1):                              # otevreni objektu
                    p = '.'.join(scope + [t.group(1)])
                    paths.add(p); objs.add(p); scope.append(t.group(1))
                elif t.group(2):
                    paths.add('.'.join(scope + [t.group(2)]))
                elif scope:
                    scope.pop()
    return paths, objs


def js_functions(js):
    """(jmeno, parametry, telo) pro kazdou `function jmeno(...){...}`."""
    out = []
    for m in re.finditer(r'function\s+(\w+)\s*\(([^)]*)\)\s*\{', js):
        i, depth = m.end(), 1
        while i < len(js) and depth:
            if js[i] == '{': depth += 1
            elif js[i] == '}': depth -= 1
            i += 1
        out.append((m.group(1), [a.strip() for a in m.group(2).split(',') if a.strip()], js[m.end():i]))
    return out


def client_reads(spa_c, objs):
    js = spa_c.replace('\\"', '"').replace('\\n', '\n')
    js = re.sub(r'^\s*"', '', js, flags=re.M)
    js = re.sub(r'"\s*$', '', js, flags=re.M)
    js = re.sub(r'/\*.*?\*/', ' ', js, flags=re.S)

    top_objs = {o for o in objs if '.' not in o}
    reads = set()

    def scan(body, roots):
        """`roots` = jmeno promenne -> cesta v JSON ('' = koren odpovedi)."""
        alias = dict(roots)
        for rx in (r'var\s+(\w+)\s*=\s*(\w+)\.([A-Za-z_0-9]+)\s*(?:\|\||;|,|\))',
                   r'var\s+(\w+)\s*=\s*\((\w+)&&\2\.([A-Za-z_0-9]+)\)\s*\?'):
            for m in re.finditer(rx, body):
                if m.group(2) in roots and m.group(3) in top_objs:
                    alias[m.group(1)] = m.group(3)
        for var, base in alias.items():
            for m in re.finditer(r'\b' + var + r'\.([A-Za-z_0-9]+)(?:\.([A-Za-z_0-9]+))?', body):
                a, b = m.group(1), m.group(2)
                if a in SKIP: continue
                path = (base + '.' + a) if base else a
                if b and not (b in SKIP):
                    if path in top_objs: path += '.' + b
                reads.add((path, m.group(0)))

    fns = js_functions(js)
    # Kdo dostava STAV? `s` je v SPA pretizene — nekde je to odpoved serveru,
    # jinde popis grafu ze `spec()` (`fLvl(s,..)`, `seriesTable(s)`). Rozhodne se
    # to podle VOLANI, ne podle jmena parametru: koren je `render`, a co je z nej
    # (tranzitivne) volane s `s`, dostava stav taky.
    consumers, zmena = {'render'}, True
    while zmena:
        zmena = False
        for name, params, body in fns:
            if name not in consumers:
                continue
            for m in re.finditer(r'\b(\w+)\s*\(\s*(s|LAST)\s*[,)]', body):
                if m.group(1) not in consumers:
                    consumers.add(m.group(1)); zmena = True

    for name, params, body in fns:
        roots = {'LAST': ''}
        if name in consumers and 's' in params and not re.search(r'\bvar\s+s\s*=', body):
            roots['s'] = ''
        # ⚠️ Lokalni kopie korene: `var s=LAST;` (drawTfom, drawDev, drawUnc).
        # Bez tohohle radku kontrola PREHLEDLA presne ten nalez, kvuli kteremu
        # vznikla (F-0078 zije v `drawTfom`) — odhalila to az pozitivni kontrola.
        for m in re.finditer(r'var\s+(\w+)\s*=\s*LAST\s*[;,]', body):
            roots[m.group(1)] = ''
        scan(body, roots)
    return reads


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    lines = io.open(path, encoding='utf-8', errors='replace').read().split('\n')
    server, spa = split_spa(lines)
    keys, objs = server_keys(server)
    print('klicu emituje server: %d (objektu: %d)' % (len(keys), len(objs)))

    chybi, jinde = [], []
    for cesta, jak in sorted(client_reads(spa, objs)):
        if cesta in keys:
            continue
        last = cesta.rsplit('.', 1)[-1]
        kde = sorted(k for k in keys if k.rsplit('.', 1)[-1] == last)
        (jinde if kde else chybi).append((cesta, jak, kde))

    for cesta, jak, kde in jinde:
        print('  !! `%s` (v kodu `%s`) NEEXISTUJE — server ho emituje jako: %s'
              % (cesta, jak, ', '.join(kde)))
    for cesta, jak, _ in chybi:
        print('  !! `%s` (v kodu `%s`) server NEEMITUJE vubec' % (cesta, jak))

    if jinde or chybi:
        print('\nSELHALO: klient cte %d poli, ktera v odpovedi nejsou.'
              % (len(jinde) + len(chybi)))
        print('V JS je to `undefined`, ne chyba — projevi se to jako prazdna nebo'
              ' trvale spatna karta (viz F-0078, F-0079).')
        return 1
    print('hranice JSON OK: vsechna ctena pole server emituje')
    return 0


if __name__ == '__main__':
    sys.exit(main())
