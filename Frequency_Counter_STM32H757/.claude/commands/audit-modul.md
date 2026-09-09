---
description: Audit jednoho modulu firmware (fáze F3) bez úpravy kódu
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
8. Do chatu napiš jen: verdikt (funkční / podmíněně / nefunkční) a 5 nejzávažnějších
   nálezů s ID a jednou větou.

**Zákazy pro tento příkaz:** neupravuj žádný `.c`, `.h`, `.ld`, `.ioc` ani komentáře.
Nespouštěj build s modifikacemi. Nedomýšlej si — co nejde dokázat, označ `HYPOTÉZA`.

---

## Když se z auditu přejde na opravy (fáze F5)

Tohle nepatří do auditu samotného, ale do commitu, který z něj vzejde. Zapsáno
2026-09-09 poté, co se opravy modulu hodiny/PWR prohlásily za ověřené, přestože
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
