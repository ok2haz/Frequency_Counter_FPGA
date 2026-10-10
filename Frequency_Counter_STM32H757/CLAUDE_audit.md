# CLAUDE.md — kontext projektu (audit firmware STM32H757BIT6)

> Tento soubor se načítá automaticky do každého sezení. **Drž ho krátký.**
> Detaily patří do `docs/` a načítají se až podle potřeby.

## 1. Projekt

- **Název / účel:** `<<DOPLNIT>>`
- **MCU:** STM32H757BIT6 — dual-core Cortex-M7 (do 480 MHz) + Cortex-M4 (do 240 MHz),
  2 MB Flash (2 banky), 1 MB SRAM v doménách D1/D2/D3, LQFP208/TFBGA
- **Revize silikonu:** `<<DOPLNIT: rev Y / rev V — určuje max. f_CPU a seznam errat>>`
- **Napájení:** `<<DOPLNIT: SMPS / LDO / direct SMPS>>` (chybná konfigurace = zařízení nenaběhne po resetu)
- **RTOS:** `<<DOPLNIT: FreeRTOS / bare-metal / ThreadX>>`
- **Generátor kódu:** `<<DOPLNIT: CubeMX + .ioc / ruční>>`
- **Toolchain:** `<<DOPLNIT: arm-none-eabi-gcc verze, CMake / Makefile / CubeIDE>>`

## 2. Příkazy

```bash
# build
<<DOPLNIT>>
# statická analýza
<<DOPLNIT: cppcheck / clang-tidy s compile_commands.json>>
# testy (pokud jsou)
<<DOPLNIT>>
```

## 3. Závazná pravidla (platí bez výjimky)

1. **Nejdřív čti `docs/LESSONS.md`.** Před každou opravou. Chyba, která už tam je,
   se nesmí zavést znovu ani v jiné podobě.
2. **Po každé opravě přidej záznam** do `docs/LESSONS.md` (šablona `docs/templates/LESSON.md`).
3. **Logika a komentáře nikdy v jednom commitu.** Buď `fix:` (mění chování),
   nebo `docs:` (mění jen komentáře). Nikdy oboje.
4. **Fáze auditu ≠ fáze opravy.** Ve fázi auditu se kód *nemění*, jen se píší nálezy
   do `docs/audit/`. Opravy až po odsouhlasení uživatelem.
5. **Žádné tvrzení bez důkazu.** Každý nález musí mít odkaz `soubor.c:řádek`.
   Co nelze dokázat ze kódu, se označí jako `HYPOTÉZA` a uvede se, jak ji ověřit na HW.
6. **Nesahej na** `.ioc`, generované bloky mimo `/* USER CODE BEGIN … END */`,
   linker skripty a startup soubory bez explicitního souhlasu.
7. **Neodstraňuj** `volatile`, `__DMB()/__DSB()/__ISB()`, MPU a cache operace
   (`SCB_CleanDCache_by_Addr`, …), HSEM zámky — i když se zdají zbytečné.
   Pokud si myslíš, že jsou zbytečné, napiš nález, neupravuj.
8. **Komentáře česky**, UTF-8, bez emoji, styl podle `docs/STYLE_CZ.md`.
9. **Nikdy `git commit --amend`, `git push --force`, `git checkout .`** bez vyžádání.
10. Rozsah jednoho sezení = **jeden modul**. Na konci aktualizuj `docs/AUDIT_STATUS.md`.

## 4. Mapa dokumentace

| Soubor | Kdy ho načíst |
|---|---|
| `docs/AUDIT_PLAYBOOK.md` | Vždy na začátku auditu — proces, fáze, severity, formát nálezu |
| `docs/CHECKLIST_STM32H7.md` | Při auditu kteréhokoli modulu — HW-specifické pasti |
| `docs/STYLE_CZ.md` | Před psaním/úpravou komentářů a kódu |
| `docs/LESSONS.md` | Před opravou a po opravě (povinné) |
| `docs/ARCHITECTURE.md` | Při orientaci v projektu; průběžně doplňuj doloženými fakty |
| `docs/AUDIT_STATUS.md` | Na začátku a konci sezení — co je hotové, kde pokračovat |
| `docs/audit/*.md` | Nálezy z jednotlivých běhů (výstup, ne vstup) |

## 5. Jak pracovat (zkráceně)

1. Načti `AUDIT_STATUS.md` → vyber první modul se stavem `nezačato`.
2. Načti `AUDIT_PLAYBOOK.md` + `CHECKLIST_STM32H7.md` + `LESSONS.md`.
3. Přečti modul celý (i hlavičky, i konfiguraci hodin/DMA/MPU, které se ho týkají).
4. Zapiš nálezy do `docs/audit/RRRR-MM-DD_<modul>.md`. **Bez editace kódu.**
5. Aktualizuj `AUDIT_STATUS.md` a shrň uživateli 5 nejzávažnějších nálezů.
