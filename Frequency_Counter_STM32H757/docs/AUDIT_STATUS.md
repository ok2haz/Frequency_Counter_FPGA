# AUDIT_STATUS.md — stav auditu

> Aktualizuj **na začátku a na konci každého sezení**. Tenhle soubor je jediný
> zdroj pravdy o tom, co je hotové — kontext CLI sezení se nepřenáší.

**Poslední aktualizace:** 2026-09-10 (4. sezení — F5 opravy)
**Fáze:** modul 1 prošel F5 a je ✅ ověřený na HW; moduly 2–5 prošly F3 (jen nálezy).
**F1 je hotová** — `docs/ARCHITECTURE.md` doplněn 2026-09-10 z auditů 1–5 (dluh uzavřen).
F1 je přeskočená a je to rostoucí dluh (viz níže).
**Branch:** `audit/2026-09-09-hodiny-pwr` (vychází z `feat/web-dashboard-v12`, commit `8521130`)
**Pokračovat zde:** ⬜ **naflashovat a ověřit po POWER-CYKLU** (viz níže), teprve pak modul 6
„drivery: SPI2/FPGA, QSPI, SDMMC“.

**Otevřené po F5** (5× S3 + 1 částečně opravený S1). Čísla v tabulce výše se **odvozují
z nálezových dokumentů** — ověř je `python tools/audit_stav.py --kontrola`:
- **F-0003** [S3] `VOSRDY` bez timeoutu — *odloženo*: leží v generovaném `SystemClock_Config()`
  bez `USER CODE` (regen by opravu smazal) a špatná mez by pustila 480 MHz dřív, než se
  ustálí regulátor. Vrátit se, až se objeví „deska občas nenaběhne“.
- **F-0007** [S3] ztráta HSE = mrtvý přístroj — *čeká na rozhodnutí o politice*: zůstat mrtvý
  ale rozlišitelně (levné), nebo nabootovat na HSI a měření označit za neplatné (drahé, mění
  časování všech sběrnic).
- **F-0014** [S3] `ipccmd` je druhý producent SPSC ringu — *odloženo*: IPC dnes prokazatelně
  funguje (CM4 alive, ETH/web běží) a oprava sahá do živé mezijádrové cesty.
- **F-0016** [S3] `.ipc_shared` je prázdná rezervace — *odloženo*: oprava znamená zásah do
  **linker skriptů obou jader** (pravidlo 6 → jen s výslovným souhlasem).
- **F-0017** [S3] `ipc_stamp()` maže i blok CM4 — *odloženo* ze stejného důvodu jako F-0014.
- **F-0018** [S1] je opravený jen **částečně** (ztráta už není tichá); dvoufázový zápis zbývá.
Dluh, který roste: `docs/ARCHITECTURE.md` je pořád prázdná šablona. Doložená čísla pro §4
„Hodinový strom“ a §1 „Rozdělení jader“ jsou hotová v `audit/2026-09-09_hodiny-pwr.md`,
pro §2 „Mapa paměti“ a §3 „Konfigurace MPU“ v `audit/2026-09-09_mpu-cache-linker.md` —
u obou stačí přepsat, ne dohledávat znovu. Otevřené otázky na HW: crash black-box pro
`Error_Handler()` volaný **před** `MX_RTC_Init()` (modul 1) a retenční test `membench`
nad rozsahem `bg_cache` (modul 2, F-0013 — nástroj `bgcheck` už existuje).

## Přehled modulů

Stav: `nezačato` → `probíhá` → `nálezy zapsány` → `opraveno` → `komentáře hotové`

| # | Modul | Soubory | Jádro | Stav | Datum | S1 | S2 | S3 | S4 | Nálezy |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | konfigurace hodin/PWR | `main.c`, `system_*.c` | CM7 | opraveno (2 otevřené) | 2026-09-09 | 0 | 2 | 5 | 0 | [7](audit/2026-09-09_hodiny-pwr.md) |
| 2 | MPU / cache / linker | `main.c MPU_Config`, `*.ld` | oba | nálezy zapsány | 2026-09-09 | 0 | 0 | 4 | 2 | [6](audit/2026-09-09_mpu-cache-linker.md) |
| 3 | IPC CM7↔CM4 (HSEM) | `ipc.c`, `ipc_shared.h`, `ipc_cm4.c` | oba | nálezy zapsány | 2026-09-09 | 0 | 0 | 4 | 0 | [4](audit/2026-09-09_ipc-cm7-cm4.md) |
| 4 | přerušení a RTOS | `stm32h7xx_it.c`, `freertos*.c` | oba | nálezy zapsány | 2026-09-10 | 1 | 1 | 0 | 1 | [3](audit/2026-09-10_preruseni-rtos.md) |
| 5 | drivery: I2C1 + I2C4 | `i2c.c`, `*_sensors.c`, `*_ui.c`, `ft5x06.c`, `ws_panel.c` | CM7 | nálezy zapsány | 2026-09-10 | 0 | 0 | 2 | 0 | [2](audit/2026-09-10_i2c.md) |
| 6 | drivery: SPI2/FPGA, QSPI, SDMMC | `fpga_freq.c`, `w25q*.c`, `sd_export.c` | CM7 | nezačato | — | – | – | – | – | — |
| 7 | aplikační logika UI | `app_gpsdo.c`, `screens/*`, `libui` | CM7 | nezačato | — | – | – | – | – | — |

**Doporučené pořadí:** hodiny/PWR → mapa paměti/MPU/cache → IPC mezi jádry →
přerušení a RTOS → jednotlivé drivery periferií → aplikační logika.
Důvod: chyba ve spodních vrstvách se v horních projeví jako „náhodná“ nestabilita
a bez opravy základu se horní vrstvy auditují zbytečně.

## Souhrn nálezů

| Severity | Otevřené | Opravené | Zamítnuté (wontfix + důvod) |
|---|---|---|---|
| S1 | 1 | 0 | 0 |
| S2 | 0 | 3 | 0 |
| S3 | 5 | 10 | 0 |
| S4 | 0 | 3 | 0 |

**Modul 1 — opraveno:** F-0001 (`pwrclk_check()` v USER CODE ověřuje dosažený stav napájení
a hodin, výstup do `status`), F-0002 (NMI zapisuje výpadek HSE do black-boxu, kind 7),
F-0004 (jen dokumentace: I2C je ~50 kHz, ne ~100), F-0005 (pojistka v `check_lessons.sh`).

**Modul 1 — otevřené:**
- **F-0003** [S3] čekání na `VOSRDY` bez timeoutu — **vědomě odloženo.** Leží v generovaném
  `SystemClock_Config()` (regen by opravu smazal) a špatně zvolená mez by pustila 480 MHz
  dřív, než se regulátor ustálí, tedy riziko rozbít fungující desku. Vrátit se k tomu,
  pokud se objeví „deska občas nenaběhne“.
- ~~**F-0006** [S3] `WRHIGHFREQ`~~ — ✅ **UZAVŘENO 2026-09-10**: `status` hlásí `WRHIGHFREQ=3`,
  tedy nenulovou (ne reset default). Flash má zpoždění naprogramované, nález padá.
- **F-0007** [S3] ztráta HSE = mrtvý přístroj bez rozlišitelné diagnózy — čeká na rozhodnutí
  o politice (zůstat mrtvý, ale rozlišitelně / nabootovat na HSI a měření označit za neplatné).
- ~~S4 (z opravy F-0002): řádek `HSE: CSS…` ve `status`~~ → převzato modulem 4 jako **F-0019**.

**Stav ověření: ✅ OVĚŘENO NA HW po power-cyklu (2026-09-10).**
`status` po studeném startu hlásí `NAPAJENI/HODINY: OK SYSCLK 480 MHz HCLK 240 MHz WRHIGHFREQ=3`
→ **F-0001 i F-0006 uzavřeny** (kontrola napájení/hodin funguje; `WRHIGHFREQ=3` je nenulová,
tedy ne reset default — Flash má naprogramované zpoždění a nález F-0006 tím padá).
`SDRAM refresh: SDRTR=371, ve zdrojaku 371` a `SDRAM cteni: rpipe=1 HCLK | I/O kompenzace READY (CSI ok)`.

✅ **Problikávání displeje (#237/#238/#72) VYŘEŠENO 2026-09-10 — příčina byla ČTECÍ CESTA FMC.**
`ReadPipeDelay = 0` + nikdy nezapnutá I/O kompenzační cela. Naměřeno po opravě:
`membench` **0 chybných bitů** (bylo 3 338 207), retence **0** (bylo 496 068), překryv adres
zmizel, `LTDC podtečení` **0/1000** (bylo 1217/1000), displej po power-cyklu v pořádku.
⚠️ Paměť ani `FMC_A9`/`PF15` vinné nebyly — nová položka v tabulce „HW obviněn a byl nevinný“.
⚠️ Poučení z cesty k tomu je **L-0011** (převzal jsem hypotézu, kterou nabídl nástroj,
místo abych přečetl jeho čísla) — stálo to jeden flash cyklus a jednu vrácenou změnu.

## Log sezení

| Datum | Modul | Co se udělalo | Nové lekce |
|---|---|---|---|
| RRRR-MM-DD | — | inicializace kitu | — |
| 2026-09-09 | hodiny/PWR | F3 přezkum, 7 nálezů (2×S2, 5×S3). Přepočítán celý hodinový strom vč. odvozených frekvencí konzumentů (FMC/SDCLK, LTDC, ADC, SPI123, SDMMC, timery) — sedí až na I2C. Kód neměněn. | zatím žádná (lekce se zapisují až po opravě, F5) |
| 2026-09-09 | hodiny/PWR | F5 opravy: 4 nálezy uzavřeny v 5 commitech (2× `docs:`, 2× `fix:`, 1× `docs:` na komentář). ⚠️ Obě opravy firmwaru jsou zatím jen **přeložené** (build 0 varování, `audit.py` v baseline, `.text` 594 504 → 595 368) — **na HW po power-cyklu NEOVĚŘENO**, viz L-0010. F-0003 vědomě odloženo. | L-0006 … L-0009 |
| 2026-09-09 | hodiny/PWR | Reakce na hlášení „po power-resetu se rozbije displej“: doloženo, že opravy do hodin **nezapisují**, a symptom dohledán jako otevřené #141/#237/#238. `pwrclk_check()` přesto přesunuta až za bring-up displeje. Doplněn power-cyklus do ověřovacího řetězce. | L-0010 |
| 2026-09-09 | MPU/cache/linker | F3 přezkum, 6 nálezů (4×S3, 2×S4), verdikt **funkční**. Mapa 32 MB SDRAM, 4 MPU oblasti a umístění objektů ověřeny proti obrazu (`nm`), ne proti zdrojáku. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-09 | IPC CM7↔CM4 | F3 přezkum, 4 nálezy (4×S3), verdikt **funkční**. Seqlock, SPSC ringy i čtenář na CM4 přečteny řádek po řádku — v jádru protokolu chyba není; nálezy jsou invarianty držené jen komentářem. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | hodiny/PWR + SDRAM | ✅ **HW ověření po power-cyklu.** F-0001 a F-0006 uzavřeny ze `status`. Problikávání displeje vyřešeno: příčina byla čtecí cesta FMC (`rpipe=0` + vypnutá I/O kompenzace), ne obnova a ne vadná paměť — `membench` 0 chybných bitů, LTDC podtečení 0/1000. Uzavřeno STATUS #237/#238/#72. | L-0011 |
| 2026-09-10 | přerušení a RTOS | F3 přezkum, 3 nálezy (1×S1, 1×S2, 1×S4), verdikt **podmíněně funkční**. Priority ISR, grouping, timebase i hooky v pořádku; stacky změřeny z běžícího přístroje (`stats`). Obě funkční vady jsou diagnostika, která selže právě při poruše. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | drivery I2C | F3 přezkum, 2 nálezy (2×S3), verdikt **funkční**. Modul s nejdelší historií incidentů je dnes dobře ošetřený; oba nálezy jsou o tom, že se dodržené pravidlo neuplatnilo všude. **Navíc doplněn `docs/ARCHITECTURE.md`** z auditů 1–5 → F1 uzavřena. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | F5 opravy (moduly 2–5) | Opraveno 10 nálezů ve 3 commitech: F-0008, F-0011, F-0015 (pojistky), F-0018 částečně, F-0019, F-0020, F-0021, F-0022 (tiché vady zviditelněny), F-0009, F-0010, F-0012 (dokumentace). Build 0 varování, audit.py baseline, `.text` 597 232 → 597 488. **⬜ neověřeno na HW.** Rozšířen `audit-modul` o fázi F5. | L-0012, L-0013 |
