#!/usr/bin/env python3
"""audit_stav.py — spocita stav nalezu PRIMO z docs/audit/*.md.

PROC EXISTUJE: souhrnna tabulka v `docs/AUDIT_STATUS.md` je DRUHA KOPIE udaje,
ktery zije v nalezovych dokumentech — a druha kopie se drive nebo pozdeji rozejde.
Rozesla se uz 2026-09-10, v ramci JEDNOHO sezeni: F-0015 byl opraveny v kodu, ale
jeho `Stav:` zustal "otevreno", a soucty podle severity nesedely o dva nalezy.
Tenhle skript je tedy tataz obrana jako `REFRESH_COUNT_EXPECTED` u SDRAM: jedno
misto pravdy, zbytek se z nej odvozuje.

Pouziti:
    python tools/audit_stav.py            # tabulka + soucty
    python tools/audit_stav.py --kontrola # navic porovna se souctem v AUDIT_STATUS.md
                                          # (nenulovy exit pri rozporu)
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AUDIT_DIR = os.path.join(ROOT, 'docs', 'audit')
STATUS_MD = os.path.join(ROOT, 'docs', 'AUDIT_STATUS.md')

# Poradi je zamerne: "castecne" se NEPOCITA jako opraveno.
OPRAVENO = ('opraveno', 'uzavreno', 'uzavřeno', 'vyreseno', 'vyřešeno')
CASTECNE = ('castecne', 'částečně')


def ascii_out(s):
    """Windows konzole je cp1252 a na diakritice/emoji spadne (tataz past ma
    zdokumentovanou `tools/audit.py`). Vypis proto vzdy prozenem pres ASCII."""
    return s.encode('ascii', 'replace').decode('ascii')


def klasifikuj(stav):
    s = stav.lower()
    if any(k in s for k in CASTECNE):
        return 'castecne'
    if any(k in s for k in OPRAVENO):
        return 'opraveno'
    return 'otevreno'


def nacti():
    nalezy = []
    for path in sorted(glob.glob(os.path.join(AUDIT_DIR, '*.md'))):
        if os.path.basename(path).lower() == 'readme.md':
            continue
        text = open(path, encoding='utf-8').read()
        # Nadpis nalezu: "### F-0001 [S2] popis"
        for m in re.finditer(r'^### (F-\d+)\s+\[(S\d)\]', text, re.M):
            fid, sev = m.group(1), m.group(2)
            zbytek = text[m.end():]
            ms = re.search(r'^- \*\*Stav:\*\*\s*(.*)$', zbytek, re.M)
            stav = ms.group(1).strip() if ms else '(chybi pole Stav)'
            nalezy.append((fid, sev, klasifikuj(stav), os.path.basename(path), stav))
    return nalezy


def main():
    nalezy = nacti()
    if not nalezy:
        print('CHYBA: v docs/audit/ nejsou zadne nalezy', file=sys.stderr)
        return 2

    sirka = max(len(n[3]) for n in nalezy)
    print('%-8s %-4s %-10s %-*s  %s' % ('ID', 'SEV', 'STAV', sirka, 'SOUBOR', 'POZNAMKA'))
    for fid, sev, kl, soubor, stav in sorted(nalezy):
        print(ascii_out('%-8s %-4s %-10s %-*s  %.60s' % (fid, sev, kl, sirka, soubor, stav)))

    soucty = {}
    for _, sev, kl, _, _ in nalezy:
        soucty.setdefault(sev, {'otevreno': 0, 'castecne': 0, 'opraveno': 0})[kl] += 1

    print('\n%-4s %9s %9s %9s' % ('SEV', 'otevrene', 'castecne', 'opravene'))
    celkem = {'otevreno': 0, 'castecne': 0, 'opraveno': 0}
    for sev in sorted(soucty):
        d = soucty[sev]
        for k in celkem:
            celkem[k] += d[k]
        print('%-4s %9d %9d %9d' % (sev, d['otevreno'], d['castecne'], d['opraveno']))
    print('%-4s %9d %9d %9d   (celkem %d nalezu)'
          % ('SUM', celkem['otevreno'], celkem['castecne'], celkem['opraveno'], len(nalezy)))

    chybejici = [n[0] for n in nalezy if 'chybi pole Stav' in n[4]]
    if chybejici:
        print('\nCHYBA: nalezy bez pole `Stav:`: %s' % ', '.join(chybejici))
        return 1

    if '--kontrola' in sys.argv:
        # Porovnej s tabulkou v AUDIT_STATUS.md: "| S3 | 5 | 10 | 0 |"
        txt = open(STATUS_MD, encoding='utf-8').read()
        rozpor = 0
        for sev in sorted(soucty):
            m = re.search(r'^\|\s*%s\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|' % sev, txt, re.M)
            if not m:
                print('\nCHYBA: %s neni v souhrnu AUDIT_STATUS.md' % sev)
                rozpor = 1
                continue
            # "otevrene" v souhrnu = otevrene + castecne (castecne neni hotove)
            oce = soucty[sev]['otevreno'] + soucty[sev]['castecne']
            opr = soucty[sev]['opraveno']
            if (int(m.group(1)), int(m.group(2))) != (oce, opr):
                print('\nROZPOR %s: AUDIT_STATUS ma %s/%s, dokumenty maji %d/%d'
                      % (sev, m.group(1), m.group(2), oce, opr))
                rozpor = 1
        if rozpor:
            print('\n=> Souhrn v AUDIT_STATUS.md se rozesel s nalezovymi dokumenty.')
            return 1
        print('\nOK: souhrn v AUDIT_STATUS.md souhlasi s nalezovymi dokumenty.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
