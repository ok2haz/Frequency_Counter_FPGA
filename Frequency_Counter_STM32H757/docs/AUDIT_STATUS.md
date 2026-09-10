# AUDIT_STATUS.md — stav auditu

> Aktualizuj **na začátku a na konci každého sezení**. Tenhle soubor je jediný
> zdroj pravdy o tom, co je hotové — kontext CLI sezení se nepřenáší.

**Poslední aktualizace:** 2026-09-10 (7. sezení — modul 7, jen F3)
**Fáze:** modul 1 prošel F5 a je ✅ ověřený na HW; moduly 2–6 prošly F3 **i F5**, ale
⬜ **neověřeně na HW** (nic z těch oprav neběželo po power-cyklu).
**Modul 7 má jen zapsané nálezy** — F5 zatím neproběhla.
**F1 je hotová** — `docs/ARCHITECTURE.md` doplněn 2026-09-10 z auditů 1–5 (dluh uzavřen).
**Branch:** `audit/2026-09-09-hodiny-pwr` (vychází z `feat/web-dashboard-v12`, commit `8521130`)
**Pokračovat zde:** ⬜ **naflashovat a ověřit po POWER-CYKLU** (viz níže); pak buď F5 pro
modul 7 (nálezy F-0025…F-0031), nebo modul 8 „aplikační logika UI“.
⚠️ Modul 6 byl v tabulce původně zapsaný jako „SPI2/FPGA, QSPI, SDMMC“ — přes 3000 řádků na
jedno sezení. **SDMMC proto dostalo vlastní řádek (modul 7)**, aby se neauditovalo povrchně.
⚠️ **Týmž způsobem se 2026-09-10 rozdělil modul 8** („aplikační logika UI“ = `app_gpsdo.c` +
`screens/*` + `libui`, dohromady **14 375 řádků** bez fontů — 4× víc než modul 7). Nově:
**8 = vykreslovací řetězec** (2 247 ř.), **9 = hlavní obrazovka** (3 272 ř.),
**10 = aplikační okna, navigace, model fokusu** (9 188 ř.). Modul 10 je pořád velký a
při jeho zahájení se má zvážit další dělení podle témat (navigace / model fokusu / okna).

**Otevřené po F5** (5× S3 + 1 částečně opravený S1 z modulů 1–6; modul 7 přidal
2× S2, 4× S3 a 1× S4, které F5 zatím neprošly). Čísla v tabulce výše se **odvozují
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

Otevřené otázky na HW: crash black-box pro
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
| 6 | drivery: SPI2/FPGA + QSPI/W25Q | `fpga_freq.c`, `w25q.c`, `w25q_store.c` | CM7 | opraveno (⬜ neověřeno na HW) | 2026-09-10 | 0 | 0 | 1 | 1 | [2](audit/2026-09-10_spi-qspi.md) |
| 7 | drivery: SDMMC + FatFs | `sd_export.c`, `datalog_sd.c`, `sd_diskio.c`, `sdmmc.c`, `fatfs.c`, `bsp_driver_sd.c` | CM7 | nálezy zapsány | 2026-09-10 | 0 | 2 | 4 | 1 | [7](audit/2026-09-10_sdmmc-fatfs.md) |
| 8 | vykreslovací řetězec | `prim_stm32_hal.c`, `libprim/*`, `libui/*` (bez fontů) | CM7 | opraveno (2 otevřené, ⬜ neověřeno na HW) | 2026-09-10 | 0 | 1 | 4 | 0 | [5](audit/2026-09-10_vykreslovaci-retezec.md) |
| 9 | hlavní obrazovka | `screens/screen_main.c`, `screen_main_data.c` | CM7 | nezačato | — | – | – | – | – | — |
| 10 | aplikační okna, navigace, model fokusu | `app_gpsdo.c` | CM7 | nezačato | — | – | – | – | – | — |

**Doporučené pořadí:** hodiny/PWR → mapa paměti/MPU/cache → IPC mezi jádry →
přerušení a RTOS → jednotlivé drivery periferií → aplikační logika.
Důvod: chyba ve spodních vrstvách se v horních projeví jako „náhodná“ nestabilita
a bez opravy základu se horní vrstvy auditují zbytečně.

## Souhrn nálezů

| Severity | Otevřené | Opravené | Zamítnuté (wontfix + důvod) |
|---|---|---|---|
| S1 | 1 | 0 | 0 |
| S2 | 3 | 3 | 0 |
| S3 | 10 | 14 | 0 |
| S4 | 1 | 4 | 0 |

⚠️ **Čísla nepiš ručně** — `python tools/audit_stav.py --kontrola` je odvodí z nálezových
dokumentů a při rozporu skončí nenulovým kódem (lekce **L-0014**). Sloupec „Otevřené“
zahrnuje i **částečně** opravené (dnes F-0018).

**Modul 6 — opraveno 2026-09-10 (⬜ neověřeno na HW):**
- **F-0023** [S3] `w25q.c` nekontroloval adresu proti kapacitě čipu → `range_ok()` na začátku
  `w25q_read` / `w25q_write` / `w25q_erase_sector`; bez součtu `addr + len` (přetečení).
  Commit `1f69ca9`, lekce **L-0015**.
- **F-0024** [S4] ignorované návraty SW resetu v `w25q_init` → **zdůvodněno v kódu**, ne
  vyhodnoceno: `cmd_only` selhává jen na straně hosta a výsledek resetu už hlídá
  následující kontrola JEDEC ID. Commit `d7dbd69` (`docs:`, chování se nemění).

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
| 2026-09-10 | drivery SPI2/FPGA + QSPI | F3 přezkum, 2 nálezy (1×S3, 1×S4), verdikt **funkční**. Oba drivery jsou blokující (grep na DMA/IT prázdný → sekce C odpadá). Klíčové zjištění: **`NOLINK` není přičitatelný ovladači na CM7** — CS boot level, AFCNTR, časování i CRC gate jsou v pořádku. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | F5 opravy (modul 8) | Skupina A: **F-0033** (chyby DMA2D se přestaly mazat naslepo + 3 počítadla a řádek `DMA2D:` ve `status`), **F-0034** (počítadlo přeskočených glyfů + řádek `FONTY:`), **F-0035** (`__DSB()` před startem DMA2D, ověřeno v disassembly). Build 0 varování, `audit.py` 92/0/2, `.text` 597 520 → 597 832. 🔑 **Oprava F-0033 hned odhalila nový nález F-0036** [S2]: hlídací mez v `d2d_wait()` vyprší i na legitimním celoobrazovkovém přenosu (14 vypršení za 25 s, roste s kreslením) a DMA2D se pak přeprogramuje za běhu. **F-0032 a F-0036 zůstávají otevřené.** ⬜ neověřeno na HW po power-cyklu. | L-0016, L-0017 |
| 2026-09-10 | vykreslovací řetězec | F3 přezkum, 4 nálezy (4×S3), verdikt **funkční**. **Modul 8 nejdřív rozdělen** (14 375 ř. → 8/9/10, viz poznámka nahoře). Nic dnes nekreslí špatně; všechny nálezy jsou latentní pasti a chybějící diagnostika. Nejzávažnější F-0032: pravidlo „partial redraw musí začít clear“ platí jen pro neprůhledné barvy, `sw_fill` obchází `mark_dirty`. Ověřeno rozborem indexů, že copy-forward nikdy nepíše do scanovaného bufferu, a že `keep[96]` v dedupu sedí přesně na mez. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | drivery SDMMC + FatFs | F3 přezkum, 7 nálezů (2×S2, 4×S3, 1×S4), verdikt **podmíněně funkční**. Blokující CPU/FIFO cesta místo IDMA je doložitelně správné rozhodnutí; ručně skládaný init obchází dvě vendor smyčky s timeoutem ~49 dní. Slabiny jsou v životním cyklu okolo mountu: **výměna karty za běhu je rozbitá deterministicky** (F-0025) a auto-unmount z defaultTasku umí smazat FatFs semafor drženy jiným taskem (F-0026). Umístění všech bufferů ověřeno `nm` nad `.elf`. Dvě falešné stopy prověřeny a zavrženy (BusFault přes `disk_status`, L-0007 přes `HAL_RCCEx_PeriphCLKConfig`). Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | F5 opravy (modul 6) | F-0023 mez proti kapacitě W25Q (`fix:` `1f69ca9`), F-0024 zdůvodnění ignorovaných návratů (`docs:` `d7dbd69`). Před zásahem ověřeno, že žádný volající na hranici neleží. Build 0 varování, `audit.py` 92/0/2, `.text` 597 488 → 597 520 a mez `cmp.w r0, #67108864` dohledána v disassembly. **Přeložen i CM4/Release** — obraz byl starší než `ipc_shared.h` (assert z F-0017), takže `build.sh` varoval na možný nesoulad bank. **⬜ neověřeno na HW.** | L-0015 |
