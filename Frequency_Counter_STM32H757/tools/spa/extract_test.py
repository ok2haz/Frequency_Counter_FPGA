# -*- coding: utf-8 -*-
"""Testy extraktoru SPA na SYNTETICKYCH pripadech.

Duvod: extraktor je merítko, kterým se posuzuje vsechno ostatní — kdyz lze
on sam, „overeni" nic neznamena. Presne to se stalo 3x (STATUS #151/#153).
Kazdy pripad rika, KTERA kontrola v retezci ho ma chytit.
"""
import io
import os
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from spa_size import extract              # noqa: E402

Q = chr(34)
fail = []


def mk(body):
    fd, p = tempfile.mkstemp(suffix='.c')
    os.close(fd)
    io.open(p, 'w', encoding='utf-8', newline='').write(
        'static const char SPA_HTML[] =\n' + body + ';\n')
    return p


def case(lbl, body, want_html, want_lit=None):
    p = mk(body)
    try:
        _, _, n_lit, html = extract(p)
        ok = (html == want_html) and (want_lit is None or n_lit == want_lit)
        print('  %s %s' % ('OK  ' if ok else 'CHYBA', lbl))
        if not ok:
            print('      dostal : %r (literalu %d)' % (html, n_lit))
            print('      cekal  : %r (literalu %s)' % (want_html, want_lit))
            fail.append(lbl)
    except SystemExit as e:
        print('  CHYBA %s -> vyjimka: %s' % (lbl, e))
        fail.append(lbl)
    finally:
        os.unlink(p)


def case_raises(lbl, body):
    p = mk(body)
    try:
        extract(p)
        print('  CHYBA %s -> melo vyhodit chybu, ale proslo' % lbl)
        fail.append(lbl)
    except SystemExit:
        print('  OK   %s (spravne odmitnuto)' % lbl)
    finally:
        os.unlink(p)


print('--- zaklad ---')
case('jeden literal',
     Q + 'ahoj' + Q, 'ahoj', 1)
case('dva literaly se spoji',
     Q + 'a' + Q + '\n' + Q + 'b' + Q, 'ab', 2)
case('escapy \\n \\t \\\\ \\"',
     Q + 'a\\nb\\tc\\\\d\\' + Q + 'e' + Q, 'a\nb\tc\\d' + Q + 'e', 1)

print('\n--- PRIPAD A: uvozovka v C komentari MIMO literal ---')
print('    (prekladac komentar zahodi -> extrakce ho MUSI taky ignorovat)')
case('blokovy komentar s uvozovkami',
     Q + 'a' + Q + '\n/* pozor na ' + Q + 'tohle' + Q + ' */\n' + Q + 'b' + Q,
     'ab', 2)
case('radkovy komentar s uvozovkou',
     Q + 'a' + Q + '\n// komentar s ' + Q + '\n' + Q + 'b' + Q,
     'ab', 2)
case('komentar s lichym poctem uvozovek',
     Q + 'a' + Q + '\n/* jedna ' + Q + ' uvozovka */\n' + Q + 'b' + Q,
     'ab', 2)

print('\n--- co se NESMI splest s komentarem ---')
case('/* uvnitr literalu je obsah, ne komentar',
     Q + '/* css komentar */' + Q, '/* css komentar */', 1)
case('// uvnitr literalu (napr. URL) je obsah',
     Q + 'http://x' + Q, 'http://x', 1)
case('hvezdicka a lomitko na hranici literalu',
     Q + 'a*' + Q + Q + '/b' + Q, 'a*/b', 2)

print('\n--- PRIPAD B: rozbity literal -> MUSI se odmitnout ---')
case_raises('neuzavrena uvozovka', Q + 'a' + Q + '\n' + Q + 'b')

print('\n--- PRIPAD C: konec deklarace je `";`, ne pouhe `;` ---')
print('    (rozbity radek koncici strednikem si jinak SAM urizne rozsah)')
# Radek uvnitr literalu konci strednikem (bezny JS!) - nesmi byt povazovan
# za konec deklarace, jinak by se zbytek SPA vubec nezkontroloval.
case('strednik uvnitr literalu neukonci deklaraci',
     Q + 'var a=1;' + Q + '\n' + Q + 'var b=2;' + Q, 'var a=1;var b=2;', 2)

print('\n--- soulad s realnym souborem ---')
real = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    '..', '..', 'CM4', 'LWIP', 'App', 'httpd_min.c')
if os.path.exists(real):
    _, _, n_lit, html = extract(real)
    print('  literalu=%d  bajtu=%d' % (n_lit, len(html)))
    nonascii = sorted({c for c in html if ord(c) > 126})
    print('  ne-ASCII: %d %s' % (len(nonascii),
                            ['U+%04X' % ord(c) for c in nonascii[:6]]))
    print('  uvozovek v HTML: %d' % html.count(Q))
    # !! Cistota ASCII/uvozovek se tu ZAMERNE NEHODNOTI jako chyba: tenhle test
    # bezi jako KROK 0 retezce, tedy PRED `ascii_clean.py` (krok 2), takze by
    # hlasil selhani nad souborem, ktery se o dva kroky pozdeji stejne uklidi.
    # Verdikt o cistote patri kroku 3 v `check.py`, kde uz je po uklidu.
    # Ukolem tohohle testu je overit EXTRAKTOR, ne obsah souboru.
else:
    print('  (httpd_min.c nenalezen — preskoceno)')

print('\n' + ('SELHALO: ' + ', '.join(fail) if fail else 'vse OK'))
sys.exit(1 if fail else 0)
