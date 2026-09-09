# AUDIT_STATUS.md — stav auditu

> Aktualizuj **na začátku a na konci každého sezení**. Tenhle soubor je jediný
> zdroj pravdy o tom, co je hotové — kontext CLI sezení se nepřenáší.

**Poslední aktualizace:** 2026-09-09
**Fáze:** F3 (přezkum kódu) — běží; F1 je přeskočená a je to dluh (viz níže)
**Branch:** `feat/web-dashboard-v12` (audit zatím bez vlastní branche, commit `8521130`)
**Pokračovat zde:** modul 2 „MPU / cache / linker“ (`main.c MPU_Config`, `*.ld`, `.map`).
Před ním doplnit `docs/ARCHITECTURE.md` §4 „Hodinový strom“ a §1 „Rozdělení jader“ —
doložená čísla jsou hotová v `docs/audit/2026-09-09_hodiny-pwr.md` (sekce „Co bylo
zkontrolováno a je v pořádku“), stačí je přepsat. Otevřená otázka na HW z modulu 1:
funguje crash black-box i pro `Error_Handler()` volaný **před** `MX_RTC_Init()`?

## Přehled modulů

Stav: `nezačato` → `probíhá` → `nálezy zapsány` → `opraveno` → `komentáře hotové`

| # | Modul | Soubory | Jádro | Stav | Datum | S1 | S2 | S3 | S4 | Nálezy |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | konfigurace hodin/PWR | `main.c`, `system_*.c` | CM7 | nálezy zapsány | 2026-09-09 | 0 | 2 | 5 | 0 | [7](audit/2026-09-09_hodiny-pwr.md) |
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
| S2 | 2 | 0 | 0 |
| S3 | 5 | 0 | 0 |
| S4 | 0 | 0 | 0 |

Otevřené nálezy modulu 1: F-0001, F-0002 (S2); F-0003, F-0004, F-0005, F-0006, F-0007 (S3).

## Log sezení

| Datum | Modul | Co se udělalo | Nové lekce |
|---|---|---|---|
| RRRR-MM-DD | — | inicializace kitu | — |
| 2026-09-09 | hodiny/PWR | F3 přezkum, 7 nálezů (2×S2, 5×S3). Přepočítán celý hodinový strom vč. odvozených frekvencí konzumentů (FMC/SDCLK, LTDC, ADC, SPI123, SDMMC, timery) — sedí až na I2C. Kód neměněn. | zatím žádná (lekce se zapisují až po opravě, F5) |
