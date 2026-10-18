---
description: Průchod komentáři jednoho modulu (fáze F6) — bez jakékoli změny logiky
---

Aktualizuj komentáře v modulu: **$ARGUMENTS**

1. Načti `docs/STYLE_CZ.md` a drž se ho doslova.
2. Zkontroluj, že pracovní kopie je čistá (`git status`). Pokud není, zastav se.
3. Přečti modul celý. Než začneš psát, urči u každé funkce: co dělá, proč je
   napsaná takhle, jaké má předpoklady a jednotky. Bez toho komentář nepiš.
4. Uprav:
   - hlavičku souboru (`@file`, `@brief`, `@details`, `@note` s HW závislostmi),
   - Doxygen bloky veřejných funkcí (parametry: jednotky + rozsah, všechny návratové hodnoty),
   - komentáře u magických konstant, workaroundů erraty, cache/DMA operací,
     synchronizace mezi jádry a závazného pořadí operací.
5. Smaž komentáře, které jen převyprávějí kód, zakomentovaný kód a historii úprav.
6. **Pokud komentář nesouhlasí s kódem: neupravuj ani jedno.** Zapiš nález do
   `docs/audit/` (severity podle dopadu) a pokračuj.
7. Ověření na konci — povinné:
   - `git diff` musí obsahovat **výhradně** komentáře a whitespace; vypiš mi počet
     změněných řádků kódu (musí být 0),
   - build musí projít; porovnej `arm-none-eabi-size` před a po,
   - commit ve tvaru `docs(<modul>): české komentáře podle STYLE_CZ`.

**Zákazy:** žádné přejmenování, žádný refaktor, žádné „zjednodušení“ podmínky,
žádná změna pořadí řádků, žádné úpravy generovaných bloků CubeMX mimo USER CODE.
