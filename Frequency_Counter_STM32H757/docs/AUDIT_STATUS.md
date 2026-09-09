# AUDIT_STATUS.md — stav auditu

> Aktualizuj **na začátku a na konci každého sezení**. Tenhle soubor je jediný
> zdroj pravdy o tom, co je hotové — kontext CLI sezení se nepřenáší.

**Poslední aktualizace:** 2026-09-09
**Fáze:** F5 (opravy) — modul 1 hotový; F1 je přeskočená a je to dluh (viz níže)
**Branch:** `audit/2026-09-09-hodiny-pwr` (vychází z `feat/web-dashboard-v12`, commit `8521130`)
**Pokračovat zde:** modul 2 „MPU / cache / linker“ (`main.c MPU_Config`, `*.ld`, `.map`).
Před ním doplnit `docs/ARCHITECTURE.md` §4 „Hodinový strom“ a §1 „Rozdělení jader“ —
doložená čísla jsou hotová v `docs/audit/2026-09-09_hodiny-pwr.md` (sekce „Co bylo
zkontrolováno a je v pořádku“), stačí je přepsat. Otevřená otázka na HW z modulu 1:
funguje crash black-box i pro `Error_Handler()` volaný **před** `MX_RTC_Init()`?

## Přehled modulů

Stav: `nezačato` → `probíhá` → `nálezy zapsány` → `opraveno` → `komentáře hotové`

| # | Modul | Soubory | Jádro | Stav | Datum | S1 | S2 | S3 | S4 | Nálezy |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | konfigurace hodin/PWR | `main.c`, `system_*.c` | CM7 | opraveno (2 otevřené) | 2026-09-09 | 0 | 2 | 5 | 0 | [7](audit/2026-09-09_hodiny-pwr.md) |
| 2 | MPU / cache / linker | `main.c MPU_Config`, `*.ld` | oba | nezačato | — | – | – | – | – | — |
| 3 | IPC CM7↔CM4 (HSEM) | `ipc.c`, `ipc_shared.h`, `ipc_cm4.c` | oba | nezačato | — | – | – | – | – | — |
| 4 | přerušení a RTOS | `stm32h7xx_it.c`, `freertos*.c` | oba | nezačato | — | – | – | – | – | — |

**Doporučené pořadí:** hodiny/PWR → mapa paměti/MPU/cache → IPC mezi jádry →
přerušení a RTOS → jednotlivé drivery periferií → aplikační logika.
Důvod: chyba ve spodních vrstvách se v horních projeví jako „náhodná“ nestabilita
a bez opravy základu se horní vrstvy auditují zbytečně.

## Souhrn nálezů

| Severity | Otevřené | Opravené | Zamítnuté (wontfix + důvod) |
|---|---|---|---|
| S1 | 0 | 0 | 0 |
| S2 | 0 | 2 | 0 |
| S3 | 2 | 3 | 0 |
| S4 | 1 | 0 | 0 |

**Modul 1 — opraveno:** F-0001 (`pwrclk_check()` v USER CODE ověřuje dosažený stav napájení
a hodin, výstup do `status`), F-0002 (NMI zapisuje výpadek HSE do black-boxu, kind 7),
F-0004 (jen dokumentace: I2C je ~50 kHz, ne ~100), F-0005 (pojistka v `check_lessons.sh`).

**Modul 1 — otevřené:**
- **F-0003** [S3] čekání na `VOSRDY` bez timeoutu — **vědomě odloženo.** Leží v generovaném
  `SystemClock_Config()` (regen by opravu smazal) a špatně zvolená mez by pustila 480 MHz
  dřív, než se regulátor ustálí, tedy riziko rozbít fungující desku. Vrátit se k tomu,
  pokud se objeví „deska občas nenaběhne“.
- **F-0006** [S3] `WRHIGHFREQ` — **měření zavedeno**, zbývá odečíst hodnotu z běžícího
  přístroje (`status` → `WRHIGHFREQ=`) a porovnat s tabulkou RM0399 pro VOS0 / 240 MHz.
- **F-0007** [S3] ztráta HSE = mrtvý přístroj bez rozlišitelné diagnózy — čeká na rozhodnutí
  o politice (zůstat mrtvý, ale rozlišitelně / nabootovat na HSI a měření označit za neplatné).
- **S4 (nové, z opravy F-0002):** řádek `HSE: CSS hlasil vypadek Nx` ve `status` se nemůže
  nikdy vypsat — `g_css_fail` je v `.bss` a reset ji vynuluje. Patří modulu „přerušení a RTOS“.

**Stav ověření obou oprav firmwaru: ⬜ NEOVĚŘENO NA HW po power-cyklu.**
Po naflashování zkontrolovat `status` → řádek
`NAPAJENI/HODINY: OK SYSCLK 480 MHz HCLK 240 MHz WRHIGHFREQ=<n>` (to zároveň uzavře F-0006),
a to **po power-cyklu**, ne jen po flashi (viz L-0010).

⚠️ **Hlášení „po power-resetu se rozbije displej“ (2026-09-09) NENÍ nález tohoto modulu.**
Je to otevřené **#141 / #237 / #238** — retence SDRAM po studeném startu, doložená měřením
(1 048 646 chybných bitů) a už jednou vyloučená bisectem z 2026-09-04. Opravy z tohoto auditu
do RCC/PWR/FLASH/SYSCFG **jen čtou**; jediné zápisy v celém diffu jsou v nedosažitelné CSS
větvi `NMI_Handler`. Rozhodne `membench` → řádek „retence po 1 s“ (musí být 0).
Podrobně v `audit/2026-09-09_hodiny-pwr.md`, oddíl „Incident při ověřování“.

## Log sezení

| Datum | Modul | Co se udělalo | Nové lekce |
|---|---|---|---|
| RRRR-MM-DD | — | inicializace kitu | — |
| 2026-09-09 | hodiny/PWR | F3 přezkum, 7 nálezů (2×S2, 5×S3). Přepočítán celý hodinový strom vč. odvozených frekvencí konzumentů (FMC/SDCLK, LTDC, ADC, SPI123, SDMMC, timery) — sedí až na I2C. Kód neměněn. | zatím žádná (lekce se zapisují až po opravě, F5) |
| 2026-09-09 | hodiny/PWR | F5 opravy: 4 nálezy uzavřeny v 5 commitech (2× `docs:`, 2× `fix:`, 1× `docs:` na komentář). Obě opravy firmwaru ověřeny: `.text` 594 504 → 595 384, build 0 varování, `tools/audit.py` 92 OK / 0 selhání / 2 s varováním. F-0003 vědomě odloženo. | L-0006, L-0007, L-0008, L-0009 |
