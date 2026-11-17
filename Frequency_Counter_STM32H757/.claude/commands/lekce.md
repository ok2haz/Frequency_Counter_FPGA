---
description: Zapiš opravenou chybu do registru lekcí a zkontroluj výskyt jinde
---

Zaznamenej lekci z opravy: **$ARGUMENTS**

1. Načti `docs/LESSONS.md` a `docs/templates/LESSON.md`.
2. Zjisti hash posledního commitu s opravou (`git log -1 --oneline`).
3. Napiš nový záznam `L-NNNN` na konec sekce „Aktivní lekce“:
   - **Příčina musí být technická**, ne popis symptomu.
   - **Pravidlo = jedna imperativní věta**, obecnější než konkrétní místo chyby.
4. Přidej řádek do tabulky „Rychlý přehled pravidel“.
5. Navrhni **automatickou detekci**:
   - jde-li to regexem, přidej řádek do `scripts/zakazane_vzory.txt`
     ve tvaru `regex<TAB>zpráva (L-NNNN)` a spusť `scripts/check_lessons.sh`,
   - jinak přidej položku do `docs/CHECKLIST_STM32H7.md` do správné sekce.
6. **Prohledej celý projekt na stejný vzor** — chyba je typicky zkopírovaná
   v dalších driverech. Nalezené výskyty zapiš jako nálezy do `docs/audit/`,
   neopravuj je bez souhlasu.
7. Do chatu napiš: ID lekce, pravidlo, způsob detekce, počet dalších výskytů.
