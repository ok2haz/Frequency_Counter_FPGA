# Šablona nálezu

Soubor s nálezy: `docs/audit/RRRR-MM-DD_<modul>.md`.
V záhlaví souboru vždy: modul, jádro, commit hash auditované verze, verze HAL,
seznam projitých sekcí checklistu.

---

```markdown
# Audit: <modul>  (RRRR-MM-DD)

- **Commit:** <hash>
- **Jádro / doména:** CM7 / D2
- **Projité sekce checklistu:** A, C, D
- **Neprojité (a proč):** F (modul nepracuje s Flash)

## Souhrn
<3–5 řádků: co modul dělá, co je největší riziko, verdikt funkční / podmíněně / nefunkční>

---

### F-NNNN [S1|S2|S3|S4] <věcný název bez emocí>

- **Místo:** `cesta/soubor.c:120-135` (funkce `foo()`)
- **Popis:** co je špatně, jedna až tři věty.
- **Důkaz:** konkrétní odkazy — registr, řádek linkeru, hodnota z `.map`,
  úsek datasheetu (kapitola), výstup analyzátoru. Bez důkazu nález neexistuje.
- **Dopad:** co se stane a kdy (deterministicky / při zátěži / při teplotě /
  po resetu). Uveď, jestli to shodí jen modul, nebo celý systém.
- **Reprodukce:** postup, nebo `HYPOTÉZA — ověřit: <co změřit a čím>`.
- **Návrh opravy:** minimální varianta; pokud existuje víc cest, uveď kompromis.
- **Riziko opravy:** nízké / střední / vysoké + co může rozbít.
- **Vztah k lekcím:** `L-0002` (nebo `nová lekce po opravě`).
- **Stav:** otevřeno / opravit / wontfix (důvod) / opraveno v `<commit>`

---

## Co bylo zkontrolováno a je v pořádku
<Krátký seznam — pro reprodukovatelnost auditu je stejně důležitý jako nálezy.>

## Nezkontrolováno / omezení tohoto běhu
<Např.: nelze ověřit bez HW, chybí schéma, nedostupný `.map`.>
```
