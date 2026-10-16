# -*- coding: utf-8 -*-
"""hw_neovereno.py - seznam oprav, ktere cekaji na overeni na HW.

Proc existuje: souhrnne dokumenty (AUDIT_STATUS.md, HW_OVERENI_*.md) se po
kolech overovani needituji a ZASTARAVAJI - 2026-09-25 pokryval
HW_OVERENI_AUDIT_2026-09-19.md jen 11 ze 46 skutecne neoverenych oprav.
Jediny spolehlivy zdroj jsou `Stav:` radky v docs/audit/*.md (L-0014).

Kriterium: nalez, jehoz blok `Stav:` obsahuje "opraveno" A "neověřeno na HW".
Blok se cte cely (vicerradkovy), ne jen prvni radek - proto dava vic nez
prosty grep. Nic nezapisuje.

Pouziti:
    python tools/hw_neovereno.py            # souhrn + seznam, serazeno S2 -> S4
    python tools/hw_neovereno.py --sev S2   # jen jedna zavaznost
"""
import glob
import io
import os
import re
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except AttributeError:
    pass

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "audit")
HDR = re.compile(r"^###\s+(F-\d{4})\s*\[(S\d)\]\s*(.+?)\s*$")
STAV = re.compile(r"^-?\s*\*\*Stav:")


def sber():
    rows = []
    for path in sorted(glob.glob(os.path.join(ROOT, "*.md"))):
        with io.open(path, encoding="utf-8") as fh:
            lines = fh.read().splitlines()
        cur = None
        for i, ln in enumerate(lines):
            m = HDR.match(ln)
            if m:
                cur = (m.group(1), m.group(2), m.group(3))
                continue
            if cur and STAV.match(ln):
                blok = ln
                for j in range(i + 1, min(i + 14, len(lines))):
                    if lines[j].startswith("### ") or lines[j].startswith("---"):
                        break
                    blok += " " + lines[j].strip()
                if "opraveno" in blok and "neověřeno na HW" in blok:
                    rows.append((cur[0], cur[1], cur[2], os.path.basename(path), "NEOVERENO"))
                elif "ČÁSTEČNĚ OVĚŘENO" in blok:
                    # Bez teto vetve by castecne overena polozka ze seznamu
                    # TISE ZMIZELA, i kdyz jeji jadro overene neni (F-0026).
                    rows.append((cur[0], cur[1], cur[2], os.path.basename(path), "CASTECNE"))
                cur = None
    return rows


def main():
    sev_filtr = None
    if "--sev" in sys.argv:
        sev_filtr = sys.argv[sys.argv.index("--sev") + 1].upper()
    rows = sber()
    rows.sort(key=lambda r: (r[4] != "CASTECNE", r[1], r[0]))
    pocty = {}
    for r in rows:
        pocty[r[1]] = pocty.get(r[1], 0) + 1
    castecne = sum(1 for r in rows if r[4] == "CASTECNE")
    print("CEKA NA HW OVERENI: %d nalezu (%s), z toho castecne overenych: %d" % (
        len(rows), ", ".join("%s=%d" % (k, pocty[k]) for k in sorted(pocty)), castecne))
    print()
    for r in rows:
        if sev_filtr and r[1] != sev_filtr:
            continue
        znacka = "~" if r[4] == "CASTECNE" else " "
        print("%s%s [%s] %-62.62s  %s" % (znacka, r[0], r[1], r[2], r[3]))
    if castecne:
        print()
        print("~ = castecne overeno: regrese vyloucena, JADRO nalezu (typicky zavod) overene neni")


if __name__ == "__main__":
    main()
