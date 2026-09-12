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

# ── Dispatch podle `s_view` je rozvětvený do pěti tabulek (audit F-0049 / F-0050) ─
# Okno, které existuje (`view_set(N)`), ale nemá `case N:` v `render_view()`, se při
# obnově obrazovky (nav_back, úklid po mrtvé I2C4) vykreslí jako HLAVNÍ OBRAZOVKA —
# a udělá to TIŠE. Přesně tak se ztratila okna 49 (FUNKCE) a 50 (NÁPOVĚDA).
# Sjednotit všech pět tabulek je dražší než vada (a `render_view` vs. screensaver se
# liší z doloženého důvodu), takže místo sjednocení hlídá rozdíl tenhle test.
# Detaily: docs/audit/2026-09-11_navigace-fokus-vstup.md
APPG="CM7/app/app_gpsdo.c"
if [ -f "$APPG" ]; then
    # `case N:` uvnitř těla render_view()
    rv_cases() {
        awk '/^static void render_view\(uint8_t v\)$/{f=1} f&&/^\}/{exit} f' "$APPG" \
            | grep -oE 'case [0-9]+:' | grep -oE '[0-9]+'
    }
    # Okna, která nepatří do render_view a je to ZÁMĚR:
    #   0  = default (hlavní obrazovka)
    #   8  = screensaver — vlastní cesta obnovy (app_gpsdo_touch_dead / exit_screensaver)
    #   11 = boot splash — běží jen při startu, obnovovat ho nedává smysl
    rv_vyjimky() { printf '0\n8\n11\n'; }

    CHYBI_RV="$( grep -oE 'view_set\([0-9]+\)' "$APPG" | grep -oE '[0-9]+' | sort -u \
        | grep -vxF -f <( { rv_cases; rv_vyjimky; } | sort -u ) | tr '\n' ' ' )"
    if [ -n "${CHYBI_RV// /}" ]; then
        echo "[!] ZAKAZANO: okno má view_set(), ale chybí mu 'case' v render_view() — ZPĚT/obnova"
        echo "    ho vykreslí jako hlavní obrazovku, a tiše (F-0049). Chybí: ${CHYBI_RV}"
        echo
        NALEZY=$((NALEZY + 1))
    fi

    # Živě překreslovaná okna (app_gpsdo_tick) musí jít i obnovit.
    CHYBI_TICK="$( awk '/^void app_gpsdo_tick\(void\)$/{f=1} f&&/^\}/{exit} f' "$APPG" \
        | grep -oE 's_view == [0-9]+' | grep -oE '[0-9]+' | sort -u \
        | grep -vxF -f <( { rv_cases; rv_vyjimky; } | sort -u ) | tr '\n' ' ' )"
    if [ -n "${CHYBI_TICK// /}" ]; then
        echo "[!] REVIZE: okno se živě překresluje v app_gpsdo_tick(), ale render_view() ho nezná"
        echo "    (po obnově se nevrátí). Chybí: ${CHYBI_TICK}"
        echo
        NALEZY=$((NALEZY + 1))
    fi
fi

# ── Okno volané z dlaždicové tabulky musí flipnout SAMO (audit 2026-09-11) ────
# `MENU_ITEMS`/`MEAS_ITEMS`/`TOOLS_ITEMS` se volají přes ukazatel
# (`TOOLS_ITEMS[i].fn()`) a volající za ně flip nedodělá: obsluha tapu jen vrátí
# `true` a UiTask na to reaguje POUZE zvukovou odezvou `alarm_click()`. Okno bez
# `present_now()` se proto nakreslí do zadního bufferu a NIKDY se neukáže —
# navenek „tlačítko nic nedělá, jen klikne". Přesně tak se choval `render_errlog`
# (okno CHYBY) od chvíle, kdy vzniklo. Viz L-0029.
if [ -f "$APPG" ]; then
    TAB_FN="$( awk '/(MENU|MEAS|TOOLS)_ITEMS\[[A-Z_]*\] = \{/{f=1;next} f&&/^\};/{f=0} f' "$APPG" \
        | grep -oE ',[[:space:]]*[a-z_][a-z_0-9]*[[:space:]]*\}' \
        | grep -oE '[a-z_][a-z_0-9]{3,}' | sort -u )"
    CHYBI_FLIP=""
    for FN in $TAB_FN; do
        BODY="$( awk -v fn="$FN" '$0 ~ "^(static )?void " fn "\\(void\\)[[:space:]]*$" {f=1} f{print} f&&/^\}/{exit}' "$APPG" )"
        [ -z "$BODY" ] && continue          # deklarace jinde / jiná signatura
        printf '%s\n' "$BODY" | grep -qE 'present_now|s_dirty = 1' \
            || CHYBI_FLIP="$CHYBI_FLIP $FN"
    done
    if [ -n "${CHYBI_FLIP// /}" ]; then
        echo "[!] ZAKÁZÁNO: okno z dlaždicové tabulky neflipne samo — nakreslí se do zadního"
        echo "    bufferu a NIKDY se neukáže (uživatel slyší jen klik). Chybí:${CHYBI_FLIP}"
        echo
        NALEZY=$((NALEZY + 1))
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
