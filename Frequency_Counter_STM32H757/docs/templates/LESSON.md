# Šablona lekce (do `docs/LESSONS.md`)

```markdown
### L-NNNN — <krátký název chyby>

- **Datum:** RRRR-MM-DD
- **Oblast:** hodiny / DMA / cache / IPC / RTOS / periferie / build / styl
- **Symptom:** jak se to projevovalo z venku (co viděl uživatel nebo měření).
- **Příčina:** skutečná technická příčina, ne popis symptomu.
- **Oprava:** co se změnilo, jednou větou + odkaz `soubor:řádek`.
- **Pravidlo:** JEDNA imperativní věta, přenositelná i na jiná místa v projektu.
- **Detekce:** regex do `scripts/zakazane_vzory.txt` / test / bod checklistu /
  warning překladače. Ideálně spustitelná věc, ne „dávat pozor“.
- **Commit:** `<hash>` (aby šlo udělat `git show` a vidět původní opravu)
- **Stav:** aktivní / archiv (+ důvod)
```

## Pravidla kvality lekce

1. **Příčina ≠ symptom.** „UART nefungoval“ není příčina. „Buffer v DTCM,
   kam DMA1 nemá přístup“ je příčina.
2. **Pravidlo musí být obecnější než původní chyba** — jinak se stejná chyba
   objeví o dva soubory dál a lekce ji nezachytí.
3. **Když pravidlo nejde detekovat automaticky**, přidej ho jako položku do
   `CHECKLIST_STM32H7.md`, ať se na něj alespoň systematicky ptáme.
4. **Nemazat.** I neplatná lekce má hodnotu (vysvětluje, proč je kód takový).
5. Po přidání lekce vždy zkontroluj **zbytek projektu** na stejný vzor —
   chyba bývá zkopírovaná ve třech dalších driverech.
