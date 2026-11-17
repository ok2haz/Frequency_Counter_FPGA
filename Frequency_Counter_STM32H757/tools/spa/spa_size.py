# -*- coding: utf-8 -*-
"""Vyextrahuje servirovane HTML z `SPA_HTML[]` v httpd_min.c a zmeri ho.

    python spa_size.py <httpd_min.c> [dump <vystup.html>]

!! Skener MUSI odstranit C komentare STEJNE jako preprocesor, jinak se rozejde
s realitou: uvozovka v `/* komentari */` uvnitr deklarace neni pro prekladac
nic (komentar zmizi driv, nez se parsuji literaly), ale naivni regex
`"(...)"` ji vezme jako zacatek retezce a od te chvile pary literalu posune.
Presne tim se puvodni verze utnula 3x (STATUS #151/#153) — a naposledy
naopak PRIDALA 9 B, co v obrazu nejsou. Proto tenhle stavovy skener:
sleduje, jestli je v retezci nebo v komentari, a resi i escapy.
"""
import io
import sys


def extract(path):
    txt = io.open(path, encoding='utf-8', errors='replace').read()
    lines = txt.split('\n')

    start = None
    for i, l in enumerate(lines):
        if 'SPA_HTML[]' in l:
            start = i
            break
    if start is None:
        raise SystemExit('SPA_HTML nenalezen')

    end = None
    for i in range(start, len(lines)):
        if lines[i].rstrip().endswith('";'):
            end = i
            break
    # !! Nenalezeny konec je CHYBA, ne prazdny vysledek. Driv se tu nechavalo
    # `end = start`, takze rozbita deklarace tise vratila nula literalu a
    # vypadala jako v poradku — presne to tiche selhani, ktere ma tenhle
    # nastroj odhalovat u jinych.
    if end is None:
        raise SystemExit('konec deklarace (radek koncici na ";) NENALEZEN'
                         ' - literal je rozbity nebo neuzavreny')

    body = '\n'.join(lines[start:end + 1])

    parts = []
    buf = []
    i, n = 0, len(body)
    state = 'code'          # code | str | block | line
    n_lit = 0
    while i < n:
        c = body[i]
        if state == 'code':
            if c == '/' and i + 1 < n and body[i + 1] == '*':
                state = 'block'; i += 2; continue
            if c == '/' and i + 1 < n and body[i + 1] == '/':
                state = 'line'; i += 1; continue
            if c == '"':
                state = 'str'; buf = []; n_lit += 1; i += 1; continue
            i += 1; continue
        if state == 'block':
            if c == '*' and i + 1 < n and body[i + 1] == '/':
                state = 'code'; i += 2; continue
            i += 1; continue
        if state == 'line':
            if c == '\n':
                state = 'code'
            i += 1; continue
        # state == 'str'
        if c == '\\' and i + 1 < n:
            esc = body[i + 1]
            buf.append({'n': '\n', 't': '\t', 'r': '\r',
                        '"': '"', '\\': '\\', '0': '\0'}.get(esc, esc))
            i += 2; continue
        if c == '"':
            parts.append(''.join(buf)); state = 'code'; i += 1; continue
        buf.append(c); i += 1
    if state == 'str':
        raise SystemExit('NEUZAVRENY retezec — literal je rozbity')

    return start + 1, end + 1, n_lit, ''.join(parts)


if __name__ == '__main__':
    a, b, n_lit, html = extract(sys.argv[1])
    print('radky %d..%d  literalu=%d  bajtu HTML=%d (%.1f kB)'
          % (a, b, n_lit, len(html), len(html) / 1024.0))
    if len(sys.argv) > 3 and sys.argv[2] == 'dump':
        io.open(sys.argv[3], 'w', encoding='utf-8', newline='').write(html)
        print('ulozeno ->', sys.argv[3])
