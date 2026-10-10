#!/usr/bin/env python3
"""Srovna konfiguraci Release v CM7/.cproject s Debug (idempotentni).

PROC: kdyz se do projektu pridavaly slozky `app/`, `libui/src/` a `libprim/src/`,
pridaly se JEN do konfigurace Debug. Release konfiguraci proto chybely:
  1) <sourceEntries> zaznamy `app`, `libui/src`, `libprim/src`
     -> IDE pro Release vubec negenerovalo Release/app/subdir.mk & spol.,
        takze se cele UI neprekladalo (linker: undefined reference na vsechno);
  2) 4 include cesty v C-compiler `includepaths`
     -> fatal error: screens/screen_main.h: No such file or directory.

POUZITI:  python scripts/fix_release_cproject.py [--check]
  --check  jen zkontroluje a vrati 1, kdyz je Release rozjety (pro CI/pred buildem)

⚠️ Po zapisu musi IDE model znovu nacist: Close Project -> Open Project.
   Makefily se generuji az pri BUILDU konfigurace, ne pri otevreni projektu.
   Kontrola uplnosti:  find CM7/Release -name subdir.mk | wc -l   ==  totez co Debug (18)

⚠️ Eclipse muze .cproject prepsat z pameti pri zavirani projektu/IDE. Kdyz se to
   stane, spust tenhle skript znovu (je idempotentni) a teprve pak IDE otevri.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPROJ = os.path.join(ROOT, "CM7", ".cproject")

REL_MARK = '<cconfiguration id="com.st.stm32cube.ide.mcu.gnu.managedbuild.config.exe.release'
SRC_DIRS = ("libprim/src", "app", "libui/src")
INC_PATHS = (
    "${workspace_loc:/H757_LED/CM7/libprim/include}",
    "${workspace_loc:/H757_LED/CM7/libui/include}",
    "${workspace_loc:/${ProjName}/libprim/src}",
    "${workspace_loc:/${ProjName}/app}",
)


def split_release(s):
    head, _, tail = s.partition(REL_MARK)
    if not tail:
        sys.exit("CHYBA: konfigurace Release v .cproject nenalezena")
    return head, REL_MARK + tail


def missing(rel):
    """Co konfiguraci Release chybi (prazdny seznam = OK)."""
    gaps = []
    m = re.search(r"<sourceEntries>(.*?)</sourceEntries>", rel, re.S)
    have = set(re.findall(r'name="([^"]*)"', m.group(1))) if m else set()
    gaps += [f"sourceEntries: {d}" for d in SRC_DIRS if d not in have]
    m = re.search(r'<option[^>]*c\.compiler\.option\.includepaths"[^>]*>(.*?)</option>',
                  rel, re.S)
    inc = m.group(1) if m else ""
    gaps += [f"includepath: {p}" for p in INC_PATHS if p.replace("&", "&amp;") not in inc
             and p not in inc]
    return gaps


def main():
    s = open(CPROJ, encoding="utf-8", newline="").read()
    head, rel = split_release(s)
    gaps = missing(rel)

    if "--check" in sys.argv:
        if gaps:
            print("Release konfigurace je ROZJETA, chybi:")
            for g in gaps:
                print("   -", g)
            return 1
        print("Release konfigurace OK (shodna s Debug)")
        return 0

    if not gaps:
        print("Release konfigurace uz je OK - nic nemenim.")
        return 0

    if not os.path.exists(CPROJ + ".bak-release-fix"):
        open(CPROJ + ".bak-release-fix", "w", encoding="utf-8", newline="").write(s)

    # 1) include cesty do C-compiler includepaths
    m = re.search(r'(<option[^>]*superClass="com\.st\.stm32cube\.ide\.mcu\.gnu\.managedbuild'
                  r'\.tool\.c\.compiler\.option\.includepaths"[^>]*>)(.*?)(\t*</option>)',
                  rel, re.S)
    if m:
        add = "".join(f'{chr(9) * 9}<listOptionValue builtIn="false" '
                      f'value="&quot;{p}&quot;"/>\n'
                      for p in INC_PATHS if p not in m.group(2))
        if add:
            rel = rel[:m.start()] + m.group(1) + m.group(2) + add + m.group(3) + rel[m.end():]

    # 2) zdrojove slozky do sourceEntries
    anchor = (chr(9) * 6 + '<entry flags="VALUE_WORKSPACE_PATH|RESOLVED" '
              'kind="sourcePath" name="Common"/>\n')
    if rel.count(anchor) == 1:
        add = "".join(f'{chr(9) * 6}<entry flags="VALUE_WORKSPACE_PATH" '
                      f'kind="sourcePath" name="{d}"/>\n'
                      for d in SRC_DIRS if f'name="{d}"' not in rel)
        rel = rel.replace(anchor, anchor + add, 1)

    open(CPROJ, "w", encoding="utf-8", newline="").write(head + rel)

    rest = missing(rel)
    if rest:
        print("CHYBA: po zapisu porad chybi:", rest)
        return 1
    print("Release konfigurace srovnana s Debug.")
    print("Dalsi krok v IDE: Close Project -> Open Project -> Build (konfigurace Release).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
