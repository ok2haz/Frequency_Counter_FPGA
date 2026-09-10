---
description: Audit jednoho modulu firmware (F3); na vyžádání i triáž a oprava nálezů (F5)
---

Proveď audit modulu: **$ARGUMENTS**

Postup (dodrž pořadí, nepřeskakuj):

1. Načti `docs/AUDIT_PLAYBOOK.md`, `docs/CHECKLIST_STM32H7.md`, `docs/LESSONS.md`
   a relevantní část `docs/ARCHITECTURE.md`.
2. Zjisti, co k modulu patří: zdrojáky, hlavičky, jeho konfigurace (hodiny, GPIO,
   DMA, MPU, NVIC, linker sekce). Vypiš seznam souborů, které budeš čítat, a čti je celé.
3. Dohledej umístění všech bufferů v `build/*.map` (pokud existuje). Neodhaduj ze zdrojáku.
4. Projdi checklist sekci po sekci. U každé relevantní položky rozhodni:
   OK / nález / nelze rozhodnout staticky.
5. Zkontroluj modul proti každému pravidlu v `docs/LESSONS.md`.
6. Zapiš nálezy do `docs/audit/<dnesni-datum>_$ARGUMENTS.md` podle
   `docs/templates/FINDING.md`. Každý nález musí mít lokaci a důkaz.
7. Aktualizuj `docs/AUDIT_STATUS.md` (stav, datum, počty nálezů, kde pokračovat).
8. Do chatu napiš: verdikt (funkční / podmíněně / nefunkční) a 5 nejzávažnějších
   nálezů s ID a jednou větou. **Pokud jsou nálezy, na konci nabídni opravu** —
   rozděl je do tří skupin podle §F5 níže (**opravit hned / rozhodnout / odložit**)
   a zeptej se, jestli má fáze oprav proběhnout. Neopravuj bez vyžádání.

**Zákazy pro fázi auditu:** neupravuj žádný `.c`, `.h`, `.ld`, `.ioc` ani komentáře.
Nespouštěj build s modifikacemi. Nedomýšlej si — co nejde dokázat, označ `HYPOTÉZA`.
⚠️ Tyto zákazy platí **do chvíle, než uživatel opravu schválí**. Pak platí §F5.

---

## §F5 — Fáze oprav: jak nálezy opravit tak, aby to platilo

Spouští se **až na vyžádání** („oprav nálezy"). Postup je destilát toho, co se
2026-09-09/10 při opravách modulů 1–5 osvědčilo — i toho, co se pokazilo.

### F5.0 Triáž PŘED prvním zásahem (nikdy neopravuj podle pořadí čísel)

Rozděl **všechny** otevřené nálezy do tří skupin a skupiny předlož uživateli:

| skupina | co do ní patří | co s ní |
|---|---|---|
| **A — opravit hned** | jednoznačná příčina, malý diff, oprava nemůže změnit chování za normálního provozu (chybějící `Enable`, `_Static_assert`, pořadí dvou řádků, kontrola návratové hodnoty, `osDelay` do hladovějící smyčky) | oprav |
| **B — rozhodnout** | oprava vyžaduje **politiku**, ne kód: co má přístroj dělat při ztrátě reference, jestli zůstat mrtvý nebo běžet dál a lhát | **nesahej na to** — polož otázku, nabídni varianty s cenou |
| **C — odložit** | leží v **generovaném kódu bez `USER CODE`** (regen opravu smaže), nebo je oprava větší než vada a zařízení funguje | zapiš důvod odložení do nálezu, ať se to příště neotvírá znovu |

⚠️ **Zařízení, které funguje, je hodnota.** Když je vada latentní a oprava riziková,
je odložení s odůvodněním lepší výsledek než odvážný zásah.

### F5.1 Rozsah a členění commitů

- **Jeden nález = jeden `fix:` commit**, dokud to jde. Když víc nálezů sdílí tentýž
  soubor (typicky `freertos_task_uart.c` nebo sdílené globály), **seskup je podle
  tématu** a v hlavičce commitu je **vyjmenuj i s ID a lokací**. Nepředstírej dělení,
  které git neumí bez ručního stagování hunků.
- **Logika a komentáře nikdy v jednom commitu** (pravidlo 3). Komentář, který popisuje
  právě měněný řádek, je součástí `fix:`; oprava *cizího* zastaralého komentáře je `docs:`.
- Ke každé opravě **zápis do `docs/LESSONS.md`** a **změna `Stav:` v nálezu** na
  `opraveno <datum>` — včetně věty, **jestli se opravilo jinak, než nález navrhoval**.

### F5.2 Povinný ověřovací řetězec (bez něj oprava neexistuje)

1. `./scripts/build.sh Release CM7` → **0 varování** (a `Release CM4`, když se ho to týká)
2. `python tools/audit.py` → **92 OK / 0 selhání / 2 s varováním** (baseline)
3. **`.text` před a po** — musí povyrůst; když ne, `--gc-sections` změnu zahodil
4. **Nový řetězec/symbol dohledej přímo v `.elf`** (`grep -ac`, `nm`) — to je jediný
   důkaz, že se to slinkovalo
5. `AUDIT_STATUS.md` → **`⬜ neověřeno na HW`**, dokud to neběželo **po power-cyklu**

### F5.3 Pasti, do kterých jsem v tomhle projektu spadl (nespadni znovu)

- 🔴 **Skripty piš do SOUBORŮ, ne do heredocu.** `\n` v řetězci se rozpadne na skutečný
  konec řádku a rozbije C literál. Stalo se i **při opravě nálezů**, přestože je to
  mechanické pravidlo č. 1 v `CLAUDE.md`.
- 🔴 **Zkontroluj vlastní zásah na tutéž třídu vady, kterou opravuješ.** Při opravě
  F-0019 jsem zapsal do `RTC->BKPxR` **před** povolením `DBP` — tedy přesně ta chyba
  „pořadí operací", jakou jsem o dva nálezy dřív popisoval.
- 🔴 **Když opravuješ jednu ze dvou symetrických instancí** (I2C1/I2C4, CM7/CM4, FB0/FB1),
  v témže commitu **doilož, že druhá je buď opravená, nebo se jí to netýká.** F-0021
  vznikl přesně tím, že se oprava z I2C4 na I2C1 nepřenesla.
- ⚠️ **Když je plná oprava velká, udělej malou, která vadu zviditelní.** U F-0018
  (letový zapisovač z hooku nikdy nezapíše) byla správná oprava dvoufázový zápis;
  místo toho přibyl čítač do `status`, aby ztráta přestala být tichá — a v commitu
  je napsané, že to **není celá oprava**.
- ⚠️ **Nesahej na časování bootu** — viz body níže.

### F5.4 Co platilo už dřív (a pořád platí)

Zapsáno 2026-09-09 poté, co se opravy modulu hodiny/PWR prohlásily za ověřené, přestože
je nikdo nepustil na hardwaru.

1. 🔴 **„Přeloženo" NENÍ „ověřeno".** `build.sh` bez varování, `tools/audit.py`
   v baseline a povyrostlý `.text` dokazují jen to, že se změna dostala do obrazu.
   O chování na desce neříkají nic. Dokud změna neběžela na HW, piš do
   `AUDIT_STATUS.md` **`⬜ neověřeno na HW`**, ne „hotovo".
2. 🔴 **Ověření po flashi není ověření — musí přijít POWER-CYKLUS.** Studený start
   je jiný stav než reset po flashi: ATTINY nabíhá vlastním tempem (probe má proto
   10 pokusů po 100 ms), SDRAM startuje s náhodným obsahem a **degradovanou retencí**
   (STATUS #238), obě jádra závodí o sdílená GPIO (#219/#208) a FPGA teprve načítá
   konfiguraci z flash. Spousta vad je vidět **jen** takhle.
3. ⚠️ **Nesahej bezdůvodně na časování bootu.** Jakýkoli `printf`, `HAL_Delay` nebo
   blokující volání vložené do `main()` **před** bring-up displeje posouvá zmíněné
   závody. Když nová diagnostika nemusí běžet brzy, dej ji **až za `display_skip:`**.
4. 🔑 **Když uživatel po opravě hlásí vadu, NEJDŘÍV zjisti, jestli je nová.**
   Pořadí: (a) prohledej `STATUS.md` a `git log` na tentýž symptom — na tomhle
   projektu má většina projevů displeje už zapsanou historii i vyloučené příčiny;
   (b) `git diff <základ>..HEAD -- '*.c'` a v něm najdi **skutečné zápisy** do
   registrů, ne jen dotčené soubory; (c) teprve pak měř podle pořadí v CLAUDE.md
   („DISPLEJ ZLOBÍ? ZMĚŘ NEJDŘÍV PAMĚŤ" → `membench`, řádek *retence po 1 s*).
   ⚠️ Bisect je podle SKILL §6g **první** krok, ne poslední.
5. **Nikdy neprohlašuj vadu za způsobenou svou změnou ani za cizí bez důkazu.**
   Doklad „moje změna to není" = diff bez zápisů do dotčené periferie **plus**
   dřívější záznam téhož symptomu, ne pocit.
