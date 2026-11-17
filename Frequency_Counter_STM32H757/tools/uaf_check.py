# -*- coding: utf-8 -*-
"""Hleda pouziti ukazatele PO jeho uvolneni v lwIP kodu na CM4.

Duvod: `-fanalyzer` tuhle tridu NENAJDE, protoze nezna lwIP alokator —
`pbuf_free()` pro nej neni uvolneni pameti. Nalezeno 2026-09-06 v `on_recv`
(`p->tot_len` cteno az ZA `pbuf_free(p)`), viz STATUS #170.

    python tools/uaf_check.py

⚠️ Je to HEURISTIKA, ne dukaz. Nevidi do vetveni, takze cokoli nahlasi si
preber okem — typicky falesne pozitivni je uvolneni ve vetvi, ktera hned
skonci `return`em na dalsim radku.
"""
import glob
import io
import os
import re
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
# !! Argument musi byt HOLY identifikator: `tcp_close(c->pcb)` uvolnuje `c->pcb`,
# ne `c`, takze nasledne `c->pcb = NULL` je spravne, ne chyba.
FREE = re.compile(r'\b(pbuf_free|tcp_close|tcp_abort|mem_free|vPortFree)'
                  r'\s*\(\s*(\w+)\s*\)')


def strip_comments(txt):
    """Odstrani C komentare, ale ZACHOVA pocet radku (cisla radku musi sedet).

    !! Bez tohohle nastroj hlasil sam sebe: komentar, ktery VYSVETLUJE opravenou
    chybu, obsahuje `pbuf_free(p)` i `p->tot_len` — a heuristika si na nem
    vyrobila nalez. Presne tatáz vada jako u extraktoru SPA (STATUS #161):
    kontrola musi merit KOD, ne to, co je o nem napsano.
    """
    out, i, n, state = [], 0, len(txt), 'code'
    while i < n:
        c = txt[i]
        if state == 'code':
            if c == '/' and i + 1 < n and txt[i + 1] == '*':
                state = 'block'; out.append('  '); i += 2; continue
            if c == '/' and i + 1 < n and txt[i + 1] == '/':
                state = 'line'; out.append('  '); i += 2; continue
            if c == '"':
                state = 'str'
            out.append(c); i += 1; continue
        if state == 'block':
            if c == '*' and i + 1 < n and txt[i + 1] == '/':
                state = 'code'; out.append('  '); i += 2; continue
            out.append('\n' if c == '\n' else ' '); i += 1; continue
        if state == 'line':
            if c == '\n':
                state = 'code'; out.append('\n'); i += 1; continue
            out.append(' '); i += 1; continue
        # state == 'str'
        if c == '\\' and i + 1 < n:
            out.append(txt[i]); out.append(txt[i + 1]); i += 2; continue
        if c == '"':
            state = 'code'
        out.append(c); i += 1
    return ''.join(out)


hits = 0
files = sorted(glob.glob(os.path.join(ROOT, 'CM4', 'LWIP', '**', '*.c'), recursive=True))
for f in files:
    lines = strip_comments(io.open(f, encoding='utf-8', errors='replace').read()).split('\n')
    for i, l in enumerate(lines):
        m = FREE.search(l)
        if not m:
            continue
        # uvolneni a `return` na TEMZE radku -> nasledujici radky jsou nedosazitelne
        if re.search(r'\breturn\b', l[m.end():]):
            continue
        var = m.group(2)
        for j in range(i + 1, min(i + 8, len(lines))):
            nxt = lines[j]
            if re.search(r'\b' + re.escape(var) + r'\s*->', nxt):
                print('%s:%d  %s(%s) -> dereference na radku %d:'
                      % (os.path.relpath(f, ROOT), i + 1, m.group(1), var, j + 1))
                print('    %s' % nxt.strip()[:100])
                hits += 1
                break
            if re.search(r'\breturn\b', nxt) or nxt.strip() == '}':
                break

print('podezreni: %d' % hits)
sys.exit(1 if hits else 0)
