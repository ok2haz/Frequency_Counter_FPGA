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
# ⚠️ TRETI kontrola (2026-09-01): predchozi dve chytaji jen zdroje, ktere jsou v
# `.project` jako <link>, resp. uz jsou v subdir.mk. Soubor, ktery CubeMX NOVE
# vytvori PRIMO v uz existujici zdrojove slozce (`Core/Src/tim.c` pri pridani
# TIM1), neni ani jedno — a presto se neslinkuje, dokud IDE nenacte model.
# Projevilo se to jako `undefined reference to MX_TIM1_Init`, ktere tenhle skript
# NEPREDPOVEDEL. Proto se porovnava i to, co je SKUTECNE NA DISKU.
disk = set()
for sub in ('Core/Src', 'app', 'app/screens', 'app/hal/stm32',
            'libui/src', 'libprim/src', 'libprim/src/internal'):
    d = os.path.join(os.path.dirname(bdir), sub)
    for f in glob.glob(os.path.join(d, '*.c')):
        if 'ui_font_' in os.path.basename(f):   # generovane tabulky glyfu
            continue
        disk.add(os.path.basename(f))
notbuilt = sorted(b for b in disk
                  if not re.search(r'[/\s]' + re.escape(b) + r'(\s|$)', built, re.M))
if notbuilt:
    print("*** %s: %d zdrojaku je NA DISKU, ale NENI v generovanych makefilech:"
          % (core, len(notbuilt)))
    print("   " + ", ".join(notbuilt[:10]) + (" ..." if len(notbuilt) > 10 else ""))
    print("   => typicky NOVY soubor od CubeMX. Close Project -> Open Project.")

if missing:
    # ASCII zamerne: Python na Windows tiskne do konzole v cp1252 a na emoji spadne.
    print("*** %s: %d zdrojaku je v .project, ale NENI v generovanych makefilech:" % (core, len(missing)))
    print("   " + ", ".join(missing[:10]) + (" ..." if len(missing) > 10 else ""))
    print("   => zastaraly model IDE. Close Project -> Open Project (Clean NEPOMUZE!),")
    print("      viz CUBEMX_CHECKLIST.md. Tenhle skript mezitim stavi z toho, co v makefilech je.")
PY
}

# ── CTVRTA kontrola (2026-09-12): co regenerace CubeMX TISE sebere ──────────
# Regen 2026-09-12 smazal peti mistech vlastni kod (viz CUBEMX_CHECKLIST, oddil
# „Co Generate Code SEBERE"). Vetsina se opravila presunem do `USER CODE`.
# Include cesty `CM7/Core/Inc` v `CM4/.cproject` (jedna z tech peti veci) uz
# 2026-09-13 NEHLIDAME: regen tu -I cestu dal maze (je to XML mimo USER CODE,
# nezmenitelne), ale zadny #include na ni uz nezavisi — scpi.h/meas_math.h/
# ipc_shared.h/version.h/meas_present.h se includuji RELATIVNI cestou k fyzicke
# poloze souboru (`#include "../Inc/scpi.h"` v CM7/Core/Src, `"../../../CM7/Core/Inc/scpi.h"`
# v CM4 souborech) — stejny vzor, jaky uz driv mel `ipc_cm4.h` pro `ipc_shared.h`.
# GCC quote-include hleda nejdriv ve slozce souboru se #include (podle jeho
# skutecne cesty, ne podle -I ani CWD), takze cesta neplati jen „obvykle", ale
# VZDY. Overeno kompilatorem se zamerne vyriznutou -I../../CM7/Core/Inc primo
# v CM4/Release/*/subdir.mk (presny artefakt, ktery regen prepisuje) — build
# dal 0 varovani a byte-presne stejny .elf.
#
# Zbyva jedina fragilni vec, kterou regen-safe udelat NEJDE:
#   `naked` HardFault na CM4 — regen ho prepise stockovym telem, takze crash
#   black-box CM4 tise prijde o PC/LR. (Na CM7 je to vyresene tim, ze `.ioc`
#   ma u HardFault vypnute „generovat obsluhu"; CM4 to zatim nema.)
# ⚠️ Kontrola jen HLASI, neopravuje: automaticka oprava cizich souboru by byla
# horsi nez hlaska — clovek ma videt, ze regen neco vzal.
# ⚠️ Pocet shod ber VYHRADNE pres `cnt` — `grep -c` pri nula shodach vypise `0`
# a SOUCASNE skonci s kodem 1 (chybejici soubor = kod 2 a prazdny vystup). Naivni
# `$(grep -c … || echo 0)` proto slozi retezec "0\n0", `[ … -lt … ]` spadne na
# „integer expected" a kontrola TISE neprobehne. Presne tak se 2026-09-12 obe
# pozitivni kontroly tvarily, ze je vse v poradku.
cnt() {
    local n
    n="$(grep -c "$1" "$2" 2>/dev/null || true)"
    case "$n" in ''|*[!0-9]*) n=0 ;; esac
    printf '%s' "$n"
}

check_regen() {
    local bad=0
    local n
    n="$(cnt '^__attribute__((naked))' "${ROOT}/CM4/Core/Src/stm32h7xx_it.c")"
    if [ "$n" -lt 1 ]; then
        echo "*** CM4: chybi 'naked' HardFault_Handler -> crash black-box CM4 ztratil PC/LR."
        echo "    => regenerace CubeMX; viz CUBEMX_CHECKLIST.md, oddil 'Co Generate Code SEBERE'."
        bad=1
    fi
    [ "$bad" -eq 0 ] || echo "    (build pokracuje, ale tohle oprav driv, nez budes flashovat)"
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

# Pojistka: sdilena PLL smi konfigurovat jen CM7 (audit F-0005, lekce L-0007).
#
# CubeMX generuje `PeriphCommonClock_Config()` do `main.c` OBOU jader a volani
# vklada do `main()` hned za `SystemClock_Config()`. Na CM7 to tak byt ma; na CM4
# by to za behu pres `__HAL_RCC_PLL2_DISABLE`/`PLL3_DISABLE` odstavilo hodiny SDRAM
# (FMC z PLL2R) a LTDC (PLL3R) pod rukama bezicimu CM7. Projevilo by se to jako
# "SDRAM cte same nuly" / rozpad obrazu a hledalo by se to v kreslicim kodu.
#
# ⚠️ PROC se to meri na OBRAZU a ne grepem zdrojaku: `nm` rekne, co se doopravdy
# slinkovalo. Dokud funkci nikdo nevola, linker ji pres `--gc-sections` zahodi a
# v obrazu NENI; jakmile volani vznikne, symbol se objevi. Grep zdrojaku proti tomu
# zavisi na zapisu volani a da se minout preformatovanim.
# ⚠️ Soucasti je POZITIVNI KONTROLA: v obrazu CM7 ten symbol BYT MUSI. Kdyby tam
# nebyl, test uz nic nemeri a mlcel by — presne ta trida "zelena, ktera nic
# neznamena", kterou projekt uz nekolikrat zaplatil.
check_cm4_clock_owner() {
    local nm="${GCC_BIN}/arm-none-eabi-nm"
    local e4="${ROOT}/CM4/${CFG}/H757_LED_CM4.elf"
    local e7="${ROOT}/CM7/${CFG}/H757_LED_CM7.elf"
    local sym='PeriphCommonClock_Config'
    [ -f "$e4" ] || return 0

    if "$nm" "$e4" 2>/dev/null | grep -q "$sym"; then
        echo ""
        echo "*** CHYBA: obraz CM4 obsahuje ${sym} -> CM4 konfiguruje sdilena PLL!"
        echo "    Odstavilo by to hodiny SDRAM a LTDC pod bezicim CM7 (lekce L-0007)."
        echo "    Najdi volani:  grep -n '${sym} *( *) *;' CM4/Core/Src/main.c"
        echo "    Na CM4 se ta funkce smi jen GENEROVAT, nikdy VOLAT."
        return 1
    fi
    if [ -f "$e7" ] && ! "$nm" "$e7" 2>/dev/null | grep -q "$sym"; then
        echo ""
        echo "*** POZOR: ${sym} chybi i v obrazu CM7 -> tahle kontrola uz nic nemeri."
        echo "    Overit rucne, jestli CM7 hodiny periferii vubec konfiguruje."
        return 1
    fi
    return 0
}

check_regen

rc=0
case "$WHICH" in
    CM7)  build_core CM7 || rc=1 ;;
    CM4)  build_core CM4 || rc=1 ;;
    BOTH) build_core CM7 || rc=1; build_core CM4 || rc=1 ;;
    *)    echo "CHYBA: druhy argument = CM7 | CM4 | BOTH" >&2; exit 1 ;;
esac

if [ "$TARGET" = "all" ]; then
    check_cm4_clock_owner || rc=1
fi

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
