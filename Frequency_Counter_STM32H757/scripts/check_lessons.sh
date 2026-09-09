#!/usr/bin/env bash
# check_lessons.sh — spustitelná část registru lekcí (docs/LESSONS.md).
#
# Prochází zdrojové soubory a hlásí výskyty vzorů, které už jednou způsobily
# chybu. Vzory jsou v scripts/zakazane_vzory.txt (regex<TAB>zpráva).
#
# Použití:
#   scripts/check_lessons.sh [cesta ...]        # default: Core Src src app
#   PRISNY=1 scripts/check_lessons.sh           # nenulový exit při nálezu (pro CI/hook)
#
# Pozor: jsou to heuristiky. Nález = "ověř to ručně", ne "je to chyba".

set -u

SKRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VZORY="${SKRIPT_DIR}/zakazane_vzory.txt"
PRISNY="${PRISNY:-0}"

if [ ! -f "$VZORY" ]; then
    echo "CHYBA: chybí soubor se vzory: $VZORY" >&2
    exit 2
fi

# Cesty ke zdrojákům (uprav podle projektu)
if [ "$#" -gt 0 ]; then
    CESTY=("$@")
else
    CESTY=()
    for d in Core CM7 CM4 Src src app Application Drivers/Custom; do
        [ -d "$d" ] && CESTY+=("$d")
    done
    [ "${#CESTY[@]}" -eq 0 ] && CESTY=(".")
fi

# Vylučované cesty (generovaný kód, knihovny)
VYLOUCIT='(Drivers/STM32H7xx_HAL_Driver|Drivers/CMSIS|Middlewares|ThirdParty|build|\.git)'

MAPA_SOUBORU="$(find "${CESTY[@]}" -type f \( -name '*.c' -o -name '*.h' \) \
    | grep -Ev "$VYLOUCIT" | sort)"

if [ -z "$MAPA_SOUBORU" ]; then
    echo "Nenalezeny žádné zdrojové soubory v: ${CESTY[*]}" >&2
    exit 2
fi

POCET_SOUBORU="$(echo "$MAPA_SOUBORU" | wc -l | tr -d ' ')"
NALEZY=0

echo "== check_lessons.sh: $POCET_SOUBORU souborů =="
echo

while IFS=$'\t' read -r REGEX ZPRAVA; do
    # přeskoč komentáře a prázdné řádky
    case "$REGEX" in
        ''|'#'*) continue ;;
    esac

    VYSTUP="$(echo "$MAPA_SOUBORU" | xargs grep -nHE -- "$REGEX" 2>/dev/null)"

    if [ -n "$VYSTUP" ]; then
        POCET="$(echo "$VYSTUP" | wc -l | tr -d ' ')"
        echo "[!] ${ZPRAVA}"
        echo "    vzor: ${REGEX}   (výskytů: ${POCET})"
        echo "$VYSTUP" | sed 's/^/    /' | head -n 20
        [ "$POCET" -gt 20 ] && echo "    ... (zkráceno)"
        echo
        NALEZY=$((NALEZY + POCET))
    fi
done < "$VZORY"

# ── Kontroly vázané na konkrétní soubor ──────────────────────────────────────
# Sem patří to, co se nedá vyjádřit jedním regexem přes celý strom, protože
# tentýž zápis je někde správně a jinde chyba.

# L-0007: sdílené PLL smí konfigurovat jen jedno jádro.
# Volání PeriphCommonClock_Config() je v CM7 správně (main.c), v CM4 by za běhu
# přes __HAL_RCC_PLL2_DISABLE()/PLL3_DISABLE() odstavilo hodiny SDRAM (FMC z PLL2R)
# a LTDC (PLL3R) pod rukama běžícímu CM7. Funkce je v CM4 vygenerovaná, ale
# NESMÍ se volat. Definice `(void)` se sem nechytí, hledá se jen volání `();`.
CM4_MAIN="CM4/Core/Src/main.c"
if [ -f "$CM4_MAIN" ]; then
    VYSTUP="$(grep -nHE 'PeriphCommonClock_Config *\( *\) *;' "$CM4_MAIN" 2>/dev/null)"
    if [ -n "$VYSTUP" ]; then
        POCET="$(echo "$VYSTUP" | wc -l | tr -d ' ')"
        echo "[!] ZAKAZANO: CM4 volá PeriphCommonClock_Config() — přenastavilo by PLL2/PLL3 pod CM7 (L-0007)"
        echo "$VYSTUP" | sed 's/^/    /'
        echo
        NALEZY=$((NALEZY + POCET))
    fi
fi

if [ "$NALEZY" -eq 0 ]; then
    echo "OK: žádný zakázaný vzor nenalezen."
    exit 0
fi

echo "== celkem podezřelých míst: ${NALEZY} =="
echo "Každé místo ověř ručně a buď oprav, nebo doplň zdůvodnění komentářem."

[ "$PRISNY" = "1" ] && exit 1
exit 0
