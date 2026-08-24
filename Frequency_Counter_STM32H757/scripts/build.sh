#!/usr/bin/env bash
# Build obou jader z prikazove radky, bez STM32CubeIDE.
#
# PROC TO EXISTUJE (2026-08-22): po CubeMX regeneraci prestalo IDE pregenerovavat
# `Debug/*/subdir.mk`, takze z buildu TISE vypadl FatFs (CM7) a HAL ETH (CM4) —
# projevilo se to az jako `undefined reference to f_open` / `HAL_ETH_Init`, presto
# ze `.project`/`.cproject` na disku byly cele v poradku. Tenhle skript stavi
# primo pres `make` nad UZ vygenerovanymi makefily, takze naladou IDE neni rusen.
#
#   ./scripts/build.sh              # Debug, obe jadra
#   ./scripts/build.sh Release      # Release (-Os, doporucene pro flash)
#   ./scripts/build.sh Debug CM4    # jen jedno jadro
#   ./scripts/build.sh Debug CM7 clean
#
# ⚠️ Skript makefily NEGENERUJE — jen je pouziva. Kdyz `Debug/` neexistuje,
# musi ho jednou vyrobit IDE (Project -> Build).
set -euo pipefail

CFG="${1:-Debug}"
WHICH="${2:-BOTH}"
TARGET="${3:-all}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDE_PLUGINS="/c/ST/STM32CubeIDE_2.1.0/STM32CubeIDE/plugins"

# Nastroje ST hledame globem, at to prezije upgrade IDE (verze je v nazvu adresare).
find_tool_dir() {
    local pat="$1" hit
    hit="$(ls -d ${IDE_PLUGINS}/${pat}/tools/bin 2>/dev/null | sort | tail -1 || true)"
    [ -n "$hit" ] || { echo "CHYBA: nenalezeno: ${IDE_PLUGINS}/${pat}/tools/bin" >&2; exit 1; }
    echo "$hit"
}
MAKE_BIN="$(find_tool_dir 'com.st.stm32cube.ide.mcu.externaltools.make.win32_*')"
GCC_BIN="$(find_tool_dir 'com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*.win32_*')"
export PATH="${MAKE_BIN}:${GCC_BIN}:${PATH}"

# Porovna zdrojaky deklarovane v `.project` (<link>) proti tomu, co je v generovanych
# `Debug/*/subdir.mk`. Rozdil = zastaraly model IDE (viz CUBEMX_CHECKLIST) — presne to,
# co 2026-08-12 i 2026-08-22 vypadalo jako zahadny `undefined reference` pri linkovani.
# Hlasi se PRED buildem, aby se nestavelo minuty na rozbitem modelu.
check_model() {
    local core="$1"
    local proj="${ROOT}/${core}/.project"
    local bdir="${ROOT}/${core}/${CFG}"
    [ -f "$proj" ] && [ -d "$bdir" ] || return 0
    python - "$proj" "$bdir" "$core" <<'PY' || true
import sys, re, os, glob
proj, bdir, core = sys.argv[1], sys.argv[2], sys.argv[3]
src = open(proj, encoding='utf-8', errors='replace').read()
linked = set()
for m in re.finditer(r'<link>(.*?)</link>', src, re.S):
    n = re.search(r'<name>(.*?)</name>', m.group(1), re.S)
    if n and n.group(1).strip().endswith('.c'):
        linked.add(os.path.basename(n.group(1).strip()))
built = ''.join(open(f, encoding='utf-8', errors='replace').read()
                for f in glob.glob(os.path.join(bdir, '**', 'subdir.mk'), recursive=True))
# ⚠️ Hledej cely nazev souboru, ne podretezec: `diskio.c` se jinak "najde" uvnitr
# `sd_diskio.c` a chybejici FatFs by proklouzl. Pred nazvem musi byt / nebo mezera.
missing = sorted(b for b in linked
                 if not re.search(r'[/\s]' + re.escape(b) + r'(\s|$)', built, re.M))

# ⚠️ Druha kontrola: objekt muze byt v `subdir.mk` (tedy se PRELOZI), ale chybet
# v `objects.list` (tedy se NESLINKUJE) — presne to udela IDE, kdyz pregeneruje
# build soubory a nezna zdroje pridane rucne. Projevi se to jako `undefined
# reference` na neco, co se pritom prelozilo.
ol = os.path.join(bdir, 'objects.list')
if os.path.isfile(ol):
    listed = open(ol, encoding='utf-8', errors='replace').read()
    declared = set()
    for f in glob.glob(os.path.join(bdir, '**', 'subdir.mk'), recursive=True):
        declared.update(re.findall(r'^\./(\S+\.o)', open(f, encoding='utf-8', errors='replace').read(), re.M))
    unlinked = sorted(o for o in declared if o not in listed)
    if unlinked:
        print("*** %s: %d objektu se PRELOZI, ale NENI v objects.list (neslinkuji se):"
              % (core, len(unlinked)))
        print("   " + ", ".join(unlinked[:6]) + (" ..." if len(unlinked) > 6 else ""))
        print("   => IDE pregenerovalo build soubory a zahodilo rucne pridane zdroje.")
if missing:
    # ASCII zamerne: Python na Windows tiskne do konzole v cp1252 a na emoji spadne.
    print("*** %s: %d zdrojaku je v .project, ale NENI v generovanych makefilech:" % (core, len(missing)))
    print("   " + ", ".join(missing[:10]) + (" ..." if len(missing) > 10 else ""))
    print("   => zastaraly model IDE. Close Project -> Open Project (Clean NEPOMUZE!),")
    print("      viz CUBEMX_CHECKLIST.md. Tenhle skript mezitim stavi z toho, co v makefilech je.")
PY
}

build_core() {
    # ⚠️ Dve `local` prikazy zamerne: `local a="$1" b="...$a..."` deklaruje OBE jmena
    # jako lokalni driv, nez expanduje, takze `$a` je pod `set -u` jeste neznama.
    local core="$1"
    local dir="${ROOT}/${core}/${CFG}"
    if [ ! -f "${dir}/makefile" ]; then
        echo "PRESKAKUJI ${core}/${CFG}: chybi makefile (nech ho jednou vygenerovat v IDE)" >&2
        return 0
    fi
    echo "################ ${core} / ${CFG} ################"
    check_model "$core"
    ( cd "$dir" && make "$TARGET" 2>&1 | grep -viE '^arm-none-eabi-gcc "|^Finished building|^ *$' ) || return 1
}

rc=0
case "$WHICH" in
    CM7)  build_core CM7 || rc=1 ;;
    CM4)  build_core CM4 || rc=1 ;;
    BOTH) build_core CM7 || rc=1; build_core CM4 || rc=1 ;;
    *)    echo "CHYBA: druhy argument = CM7 | CM4 | BOTH" >&2; exit 1 ;;
esac

if [ "$TARGET" = "all" ] && [ "$rc" -eq 0 ]; then
    echo ""
    echo "================ vysledek ================"
    for c in CM7 CM4; do
        elf="${ROOT}/${c}/${CFG}/H757_LED_${c}.elf"
        [ -f "$elf" ] && "${GCC_BIN}/arm-none-eabi-size" "$elf"
    done
    # ⚠️ IPC_VERSION musi souhlasit v OBOU obrazech — jinak CM4 IPC vypne a na
    # displeji je "4:--" (CM4 zije, ale snapshotu neveri). Proto se to hlida tady.
    hdr="${ROOT}/CM7/Core/Inc/ipc_shared.h"
    v="$(grep -oE '#define IPC_VERSION +[0-9]+' "$hdr" | grep -oE '[0-9]+$' || true)"
    echo ""
    echo "IPC_VERSION = ${v}  -> pri zmene FLASHNI OBE BANKY (bank1 CM7 + bank2 CM4)."
    # Predletova pojistka: oba obrazy musi byt novejsi nez sdilena hlavicka. Flashnuti
    # jen jedne banky je tichá chyba — CM4 pri neshode verzi prestane cist snapshot,
    # ale heartbeat publikuje dal, takze header dal ukazuje "4:xx%" jako by bylo OK.
    for c in CM7 CM4; do
        elf="${ROOT}/${c}/${CFG}/H757_LED_${c}.elf"
        if [ -f "$elf" ] && [ "$hdr" -nt "$elf" ]; then
            echo "⚠️  ${c}: obraz je STARSI nez ipc_shared.h -> prelozit znovu, jinak nesoulad bank!"
        fi
    done
fi
exit "$rc"
