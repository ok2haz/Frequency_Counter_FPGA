# -*- coding: utf-8 -*-
"""Overi, ze kazde okno, ktere se pushne na navigacni zasobnik, ma `case`
v `render_view()`.

Duvod (CLAUDE.md): `nav_back()` renderuje navratovy cil pres `render_view`.
Kdyz pro nej chybi `case`, spadne to do `default` a tlacitko ZPET vede na
HLAVNI OBRAZOVKU misto tam, odkud uzivatel prisel. Uz se to stalo (okna 2/15/18,
STATUS „Oprava navigace 2026-08-17") a je to chyba, kterou prekladac nevidi —
navenek to vypada jako „nejak divna navigace", ne jako vada.

    python tools/nav_check.py
"""
import io
import os
import re
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SRC = os.path.join(ROOT, 'CM7', 'app', 'app_gpsdo.c')


def strip_comments(txt):
    """Odstrani C komentare, zachova cisla radku. (Bez toho by se matchovaly
    priklady v komentarich — tatáz vada jako u extraktoru SPA, STATUS #161.)"""
    out, i, n, st = [], 0, len(txt), 'code'
    BS = chr(92)
    while i < n:
        c = txt[i]
        if st == 'code':
            if c == '/' and i + 1 < n and txt[i + 1] == '*':
                st = 'block'; out.append('  '); i += 2; continue
            if c == '/' and i + 1 < n and txt[i + 1] == '/':
                st = 'line'; out.append('  '); i += 2; continue
            if c == '"':
                st = 'str'
            out.append(c); i += 1; continue
        if st == 'block':
            if c == '*' and i + 1 < n and txt[i + 1] == '/':
                st = 'code'; out.append('  '); i += 2; continue
            out.append('\n' if c == '\n' else ' '); i += 1; continue
        if st == 'line':
            if c == '\n':
                st = 'code'; out.append('\n'); i += 1; continue
            out.append(' '); i += 1; continue
        if c == BS and i + 1 < n:
            out.append(txt[i]); out.append(txt[i + 1]); i += 2; continue
        if c == '"':
            st = 'code'
        out.append(c); i += 1
    return ''.join(out)


code = strip_comments(io.open(SRC, encoding='utf-8', errors='replace').read())

# Okno 0 = hlavni obrazovka. `case 0` NEEXISTUJE zamerne — obsluhuje ji
# `default: app_gpsdo_render_main()`, coz je pro ni spravne. Vyjmout, jinak
# kontrola hlasi nalez tam, kde zadny neni.
MAIN_VIEW = 0
pushed = sorted({int(x) for x in re.findall(r'nav_push\(\s*(\d+)\s*\)', code)}
                - {MAIN_VIEW})

# telo render_view: od jeho definice po prvni '}' v nultem sloupci
m = re.search(r'\n[a-zA-Z_].*?render_view\s*\([^)]*\)\s*\n?\{(.*?)\n\}', code, re.S)
if m is None:
    print('render_view() nenalezen — zkontroluj jmeno funkce')
    sys.exit(2)
cases = sorted({int(x) for x in re.findall(r'case\s+(\d+)\s*:', m.group(1))})

print('nav_push(X) v kodu  : %s' % pushed)
print('case X v render_view: %s' % cases)
missing = [p for p in pushed if p not in cases]
extra = [c for c in cases if c not in pushed]
print()
if extra:
    print('case bez nav_push (neskodne, jen mrtve): %s' % extra)
if missing:
    print('🔴 CHYBI case pro okno: %s' % missing)
    print('   -> ZPET z podokna spadne na hlavni obrazovku misto navratu')
    sys.exit(1)
print('OK: kazde pushnute okno ma svuj case')
