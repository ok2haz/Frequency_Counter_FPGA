# -*- coding: utf-8 -*-
"""Povinny overovaci retezec pro SPA (`SPA_HTML[]` v CM4/LWIP/App/httpd_min.c).

SPA nema build krok — je to jeden obri C retezcovy literal — takze **prekladac
sam neodhali skoro nic** z toho, co se na ni da pokazit. Tenhle skript spoji
vsechny kontroly do jednoho behu, aby se retezec nedal provest jen napul.

    python tools/spa/check.py            # kontroly nad zdrojem (rychle)
    python tools/spa/check.py --build    # + build CM4 + kontrola `nm` (definitivni)

⚠️ Krok `nm` je JEDINA kontrola, ktera utnuty literal chytne definitivne
(merí, co se doopravdy slinkovalo, ne mezistav). Utnuti se stalo 3x — STATUS #151/#153.
"""
import glob
import io
import os
import re
import subprocess
import sys

try:                       # konzole je casto cp1252 -> nesmi shodit skript
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SRC = os.path.join(ROOT, 'CM4', 'LWIP', 'App', 'httpd_min.c')
NODE = os.path.join(ROOT, 'tools', 'node-v20.18.1-win-x64', 'node.exe')
OUT = os.path.join(HERE, '_out')
HTML = os.path.join(OUT, 'spa.html')
JS = os.path.join(OUT, 'spa.js')
TESTS = ['spa_test', 'hov_test', 'unc_test', 'pwr_test',
         'mdev_test', 'pn_test', 'alarm_test', 'pref_test', 'axis_test', 'sys_test']

fail = []


def step(n, name):
    print('\n== %d) %s %s' % (n, name, '=' * max(0, 58 - len(name))))


def run(cmd, must_pass=True, label=None):
    r = subprocess.run(cmd, capture_output=True, text=True,
                       encoding='utf-8', errors='replace', cwd=ROOT)
    out = (r.stdout or '') + (r.stderr or '')
    print(out.rstrip())
    if must_pass and r.returncode != 0:
        fail.append(label or ' '.join(map(str, cmd[:2])))
    return r.returncode, out


def main():
    do_build = '--build' in sys.argv
    if not os.path.isdir(OUT):
        os.makedirs(OUT)

    # Nejdriv se overi MERITKO. Kdyz lze extraktor, nic dalsiho nic neznamena —
    # presne tim se SPA 3x „overila" jako v poradku, kdyz v poradku nebyla.
    step(0, 'extract_test.py (funguje vubec extraktor?)')
    run([sys.executable, os.path.join(HERE, 'extract_test.py')], label='extract_test')

    step(1, 'find_odd.py (liche uvozovky = rozbity literal)')
    _, o = run([sys.executable, os.path.join(HERE, 'find_odd.py'), SRC])
    m = re.search(r'lichym poctem uvozovek:\s*(\d+)', o)
    if not m or int(m.group(1)) != 0:
        fail.append('find_odd: liche uvozovky')

    step(2, 'ascii_clean.py (ne-ASCII a escapovane uvozovky v literalu)')
    run([sys.executable, os.path.join(HERE, 'ascii_clean.py'), SRC])

    step(3, 'spa_size.py (extrakce HTML)')
    _, o = run([sys.executable, os.path.join(HERE, 'spa_size.py'), SRC, 'dump', HTML])
    m = re.search(r'bajtu HTML=(\d+)', o)
    if not m:
        fail.append('spa_size: nelze precist velikost')
        return done()
    n_html = int(m.group(1))

    html = io.open(HTML, encoding='utf-8').read()
    bad = sorted({c for c in html if ord(c) > 126})
    print('ne-ASCII v servirovanem HTML: %d %s' % (len(bad), bad[:8]))
    if bad:
        fail.append('ne-ASCII v HTML')
    if '"' in html:
        # uvozovka v HTML je povolena jen kdyz ji tam nekdo chtel; SPA ji nema mit
        print('!! uvozovka v servirovanem HTML (SPA ma byt bez nich)')
        fail.append('uvozovka v HTML')

    ms = re.search(r'<script>(.*)</script>', html, re.S)
    if not ms:
        fail.append('nenalezen <script> v HTML')
        return done()
    io.open(JS, 'w', encoding='utf-8').write(ms.group(1))
    print('JS bajtu: %d' % len(ms.group(1)))

    step(4, 'node --check (syntaxe JS)')
    if not os.path.exists(NODE):
        print('!! node nenalezen: %s  -> KROK PRESKOCEN' % NODE)
        fail.append('node chybi (kroky 4 a 6 neprobehly)')
    else:
        rc, _ = run([NODE, '--check', JS], label='node --check')
        if rc == 0:
            print('JS syntaxe OK')

    step(5, 'dom_check.py (chybejici/duplicitni id, kotvy, <div>)')
    run([sys.executable, os.path.join(HERE, 'dom_check.py'), HTML], label='dom_check')

    step(6, 'JS testy')
    if os.path.exists(NODE):
        for t in TESTS:
            path = os.path.join(HERE, t + '.js')
            if not os.path.exists(path):
                print('  -- %s: CHYBI SOUBOR' % t)
                fail.append(t + ' chybi')
                continue
            r = subprocess.run([NODE, path, JS], capture_output=True, text=True,
                               encoding='utf-8', errors='replace')
            out = (r.stdout or '') + (r.stderr or '')
            n_bad = out.count('CHYBA')
            print('  %-11s %s' % (t, 'OK' if r.returncode == 0 else
                                  'SELHAL (%d kontrol)' % n_bad))
            if r.returncode != 0:
                print(out.rstrip())
                fail.append(t)

    if not do_build:
        print('\n(krok 7-8 preskocen — spust s --build pro definitivni kontrolu `nm`)')
        return done()

    step(7, 'build CM4 Release')
    sh = os.path.join(ROOT, 'scripts', 'build.sh')
    rc, _ = run(['bash', sh, 'Release', 'CM4'], label='build CM4')
    if rc != 0:
        return done()

    step(8, 'nm: velikost SPA_HTML v ELF musi sedet s extrakci')
    nms = glob.glob(r'C:\ST\STM32CubeIDE*\STM32CubeIDE\plugins'
                    r'\*gnu-tools*\tools\bin\arm-none-eabi-nm.exe')
    elf = os.path.join(ROOT, 'CM4', 'Release', 'H757_LED_CM4.elf')
    if not nms or not os.path.exists(elf):
        print('!! nm nebo ELF nenalezen -> KROK PRESKOCEN')
        fail.append('nm neprobehl')
    else:
        r = subprocess.run([sorted(nms)[-1], '--print-size', '--radix=d', elf],
                           capture_output=True, text=True, errors='replace')
        ln = [l for l in r.stdout.splitlines() if l.endswith(' SPA_HTML')]
        if not ln:
            print('!! SPA_HTML v ELF nenalezen')
            fail.append('SPA_HTML chybi v ELF')
        else:
            n_elf = int(ln[0].split()[1])
            ok = (n_elf == n_html + 1)
            print('  extrakce %d B + NUL = %d   ELF %d   -> %s'
                  % (n_html, n_html + 1, n_elf, 'OK' if ok else 'NESEDI!'))
            if not ok:
                print('  !! literal je v ELF JINE DELKY nez v extrakci — typicky'
                      ' utnuty uvozovkou (STATUS #151/#153)')
                fail.append('nm: velikost SPA_HTML nesedi')
    return done()


def done():
    print('\n' + '=' * 66)
    if fail:
        print('SELHALO: ' + ', '.join(fail))
        return 1
    print('SPA: vsechny kontroly OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
