# AUDIT_PLAYBOOK.md — proces auditu firmware

Cíl: **doložitelně** určit, co v kódu funguje, co je nestabilní a co nefunguje.
Ne "kód vypadá dobře", ale "řádek X za podmínky Y selže, protože Z".

## 0. Fáze auditu

| Fáze | Co se dělá | Co se **nesmí** | Výstup |
|---|---|---|---|
| **F0 Příprava** | build musí projít, vygenerovat `compile_commands.json`, čistá git branch `audit/<datum>` | nic měnit ve zdrojích | funkční build, log warningů |
| **F1 Inventura** | mapa modulů, závislostí, vlastnictví periferií mezi CM7/CM4, mapa paměti | domýšlet si | `ARCHITECTURE.md` doplněný odkazy `soubor:řádek` |
| **F2 Statická analýza** | kompilátor `-Wall -Wextra`, cppcheck, clang-tidy, `--warn-common`, mapa stacku | ignorovat warningy jako "šum" | seznam surových nálezů |
| **F3 Přezkum kódu** | modul po modulu podle checklistu | opravovat | `docs/audit/RRRR-MM-DD_<modul>.md` |
| **F4 Triáž** | uživatel rozhodne, co se opravuje a v jakém pořadí | opravovat na vlastní pěst | seřazený seznam k opravě |
| **F5 Opravy** | jedna oprava = jeden commit `fix:` + záznam v `LESSONS.md` | míchat s komentáři/refaktorem | commity + lekce |
| **F6 Komentáře** | průchod komentáři podle `STYLE_CZ.md`, commity `docs:` | měnit jakoukoli logiku | čitelný kód, binárně ověřený |
| **F7 Regresní pojistka** | z lekcí udělat spustitelná pravidla (`scripts/check_lessons.sh`) | — | zelený běh skriptu |

Fáze se **nepřeskakují a neslučují**. F3 bez F1 vede k nálezům mimo kontext.

## 1. Severity (jednotná klasifikace)

- **S1 — nefunkčnost:** kód prokazatelně nedělá, co má (špatný registr, špatná
  podmínka, nedosažitelná větev, buffer v paměti nedostupné pro DMA, chybějící
  povolení hodin). Deterministicky reprodukovatelné.
- **S2 — nestabilita:** funguje "většinou". Race condition, chybějící `volatile`,
  chybějící cache maintenance, nedefinované chování, závislost na časování,
  ignorovaná návratová hodnota HAL, přetečení stacku, priority IRQ vs RTOS.
- **S3 — robustnost:** chybí timeout, nezpracovaná chyba, magické konstanty,
  neošetřený vstup, watchdog nekopnut v chybové cestě.
- **S4 — čitelnost/styl:** komentáře, pojmenování, mrtvý kód, duplikace.

Pravidlo pro triáž: **S1 a S2 se řeší vždy**, S3 podle rizika, S4 hromadně v F6.

## 2. Formát nálezu (povinný)

Nálezy jdou do `docs/audit/RRRR-MM-DD_<modul>.md`, jeden blok na nález,
podle `docs/templates/FINDING.md`. Minimum:

```
### F-0012 [S2] Chybí invalidace D-cache po příjmu přes DMA
Místo:    Core/Src/uart_rx.c:143-158
Důkaz:    buffer `rx_buf` je v `.axi_sram` (linker.ld:88), D-cache je zapnutá
          (main.c:112 SCB_EnableDCache), po `HAL_UART_Receive_DMA` se nikde
          nevolá SCB_InvalidateDCache_by_Addr.
Dopad:    CPU může číst zastaralá data; projeví se náhodně, hlavně po zvýšení
          zátěže sběrnice.
Reprodukce: <<postup nebo "HYPOTÉZA — ověřit měřením">>
Návrh:    invalidace 32B-zarovnaného rozsahu v callbacku, nebo buffer do
          nekešované MPU oblasti.
Riziko opravy: nízké / středně / vysoké + proč
```

**Zakázáno:** nález bez lokace, nález typu "mohlo by být lepší", nález, který
je jen opsaný komentář z kódu.

## 3. Jak hledat nefunkčnost (ne jen "code smells")

Pořadí je záměrné — nejvíc chyb ve firmware je v prvních třech bodech.

1. **Inicializační pořadí:** hodiny periferie povolené *po* zápisu do jejích
   registrů; GPIO alternate function vs. periferie; napájecí domény; `HAL_Init`
   vs. konfigurace PWR/VOS; deinit při chybě.
2. **Datové cesty:** kde leží buffer × kdo k němu přistupuje (CPU / DMA1-2 /
   BDMA / MDMA / Ethernet / periferie druhého jádra) × cache × zarovnání.
3. **Souběh:** ISR ↔ hlavní smyčka ↔ druhé jádro ↔ RTOS tasky. U každé
   proměnné sdílené přes tyto hranice ptej se: `volatile`? atomická? zámek?
   barrier?
4. **Návratové hodnoty:** každé `HAL_*` a `osXxx` volání — je výsledek použit?
5. **Nekonečné čekání:** `while (!(REG & FLAG));` bez timeoutu = potenciální
   zatuhnutí; hledej systematicky.
6. **Nedosažitelný / mrtvý kód:** větve, které nemohou nastat (špatná maska,
   `if (u8var > 300)`), přepsané `#define`, neregistrované callbacky.
7. **Rozpory kód ↔ konfigurace:** kód předpokládá 480 MHz, hodiny nastaveny na
   400; velikost bufferu v `.h` ≠ velikost v linkeru; baudrate vs. skutečný PCLK.

## 4. Co lze a nelze rozhodnout bez HW

- **Staticky rozhodnutelné:** registry, masky, pořadí init, umístění bufferů,
  chybějící `volatile`, priority IRQ, timeouty, velikosti, návratové hodnoty.
- **Vyžaduje HW/měření:** skutečné časování, integrita signálu, teplotní
  závislost, spotřeba, chování v mezních stavech napájení.
Vše z druhé kategorie se v nálezu označí `HYPOTÉZA` + navrhne se konkrétní
způsob ověření (co změřit, kde, jakým nástrojem).

## 5. Řízení kontextu (důležité pro CLI sezení)

- Jeden modul = jedno sezení. Na konci vždy zapiš stav do `AUDIT_STATUS.md`,
  aby se dalo pokračovat po restartu.
- Nikdy nečti celý repozitář "pro kontext". Čti cíleně: modul + jeho hlavičky +
  konfigurace, která se ho týká.
- Dlouhé výpisy (mapa, warningy) nezobrazuj celé — filtruj a shrň, uchovej
  soubor s výstupem na disku.

## 6. Definice hotového modulu

- [ ] Všechny položky `CHECKLIST_STM32H7.md` relevantní pro modul projity.
- [ ] Nálezy zapsány s lokací a důkazem.
- [ ] Zkontrolováno proti `LESSONS.md` (žádná dříve opravená chyba znovu).
- [ ] `AUDIT_STATUS.md` aktualizován (datum, počet nálezů, kdo/co dál).
