# FUNKCE.md — co přístroj umí a co je implementované

> **Účel:** jeden přehled schopností hardwaru a firmwaru a jejich *stavu*. Není to návod ani popis kódu — hloubku drží
> `CLAUDE.md` (jak to funguje), `docs/HW_REFERENCE.md` (tabulky), `../STATUS.md` (úkoly), `docs/MERENI_A_KALIBRACE.md` (měření).
> **Udržovat:** při každé nové funkci nebo změně stavu upravit řádek zde (spolu s commitem, který ji mění).
> Stav k **2026-10-18, firmware v0.27.0, IPC v28, FPGA FW 0x0424**.

## Legenda stavu

| Značka | Význam |
|---|---|
| ✅ | implementováno a **ověřeno na desce** (po power-cyklu, bez ladicí sondy) |
| 🟡 | kód hotový a přeložený, **neověřeno na HW** (nebo ověřeno jen krátce) |
| 🧪 | ověřeno jen simulací / na hostu |
| ⬜ | plán, nic neimplementováno |
| ➖ | záměrně nezapnuto, odstraněno nebo mimo rozsah |

---

## 1. Hardware

| Díl | Co dělá | Stav |
|---|---|---|
| **STM32H757BIT6** (CM7 480 MHz + CM4 240 MHz) | CM7: logika, měření, UI, úložiště; CM4: Ethernet, lwIP, SCPI, web | ✅ |
| **FPGA GW1NR-9** (Tang Nano, deska FPGA modulu 2.1) | reciproké měření, carry-chain TDC, záchyt 1PPS, SPI slave k STM32 | ✅ (bitstream 0x0424) |
| **Si5356A** | 4× 100 MHz z 10 MHz reference (hodiny FPGA); CLK2–7 volně nastavitelné (okno GENERÁTOR) | ✅ |
| **OCXO Bliley 6891 / NVG47A1282** (10 MHz, EFC 0–4 V) | interní reference, řízená DAC | ✅ |
| **DAC AD5693R** (0x4C) | ladění OCXO (76,3 µV/LSB, K ≈ 3,4·10⁻¹¹/LSB), RC 220 ms + buffer OPA365 | ✅ |
| **ADS1115** (0x48) | Vc OCXO, 12 V/VBUS, +5 V větev (AD8307 RF na desce není osazen) | ✅ |
| **TMP117** ×3 (0x48 deska STM, 0x49 OCXO, 0x4A FPGA/modul) | teploty; 0x4A čeká na vstupní modul | ✅ (0x4A ➖ neosazen) |
| **ADC3 (MCU)** | teplota jádra, VREF+ (VREFBUF 2,5 V), VBAT | ✅ |
| **GPS u-blox NEO-7M** | NMEA + UBX-TIM-TP (sawtooth), 1PPS do FPGA PIN33 | ✅ |
| **Displej 4,3" 800×480** (DSI → TC358762) + dotyk FT5x06 + ATTINY (napájení/podsvícení) | hlavní UI | ✅ |
| **Rotační encoder + tlačítko** (TIM1, PC13) | plnohodnotné ovládání vedle dotyku | ✅ |
| **Beeper** (PH9) | alarmy, boot melodie, POST diagnostika | ✅ |
| **SDRAM 32 MB**, **W25Q512JV 64 MB** (QSPI), **SD karta** (SDMMC1, 4 bit) | framebuffery a cache / trvalé nastavení a datalog / export | ✅ |
| **Ethernet LAN8742A** (CM4) | web, SCPI TCP 5025, DHCP, mDNS | ✅ |
| **USB CDC** | konzole (příkazy, SCPI), screenshot | ✅ |
| **Vstupní modul** (relé, MCP4728 práh, ÷10, 200 MHz/1,1 GHz) | dvoukanálový vstup DC–200 MHz | ⬜ (schéma/zadání, nic neběží) |

## 2. Měření kmitočtu a času (FPGA + STM32)

| Funkce | Stav | Poznámka / kde |
|---|---|---|
| Reciproké čítání, dva kanály A/B | ✅ | FPGA `tdc.v`, parser v `fpga_freq.c` |
| Carry-chain TDC (jednotka T/16384 ≈ 0,61 ps) + regrese | ✅ | `docs/TDC_MATEMATIKA.md` |
| Hradlo 0,05 / 0,1 / **0,25** / 0,5 / 1 s | ✅ | footer GATE, `SENS:FREQ:GATE` |
| Hi-res kmitočet z dvojice hrany/hradlo (až 15 číslic) | ✅ | velké číslo, SCPI `MEAS:FREQ?` |
| FREQ ↔ PERIOD přepínač (s … ps) | ✅ | footer |
| Detekce ztráty signálu, auto‑restart, kontrola oken (samokontrola FPGA) | ✅ | `status` → FPGA SAMOKONTROLA |
| Kalibrace TDC (code density) po startu, po teplotě, po výpadku hodin | ✅ | UART `tdc`, `tdc cal` |
| Rozsah vstupu | ✅ | dnes do ~30 MHz bez děliče; 200 MHz/1,1 GHz ⬜ (vstupní modul) |
| Math Mx+B, NULL (relativní měření), limity PASS/FAIL + alarm | 🟡 | okno MATH/LIMITY |
| Odchylka ×N (jako ADRET 4110) | 🟡 | okno „Odchylka ×N" |
| Dvojkanál (A a B nad sebou) | 🟡 | okno „Dvojkanál" |
| Emulátor FPGA (`fpgasim`) vč. injektorů poruch | ✅ | UART `fpgasim` |

## 3. Statistika stability

| Funkce | Stav | Poznámka |
|---|---|---|
| Offset, σ(n−1), drift, histogram, trend až 60 dní, spektrogram Δf | 🟡 | hlavní obrazovka + okna |
| **Allan ADEV / MDEV / HDEV / TDEV** (overlapping), konfidenční pás z EDF, typ šumu | 🟡 | okno ALLAN, web |
| MTIE | ➖ jen odhad | potřebuje uloženou fázi |
| Podlaha čítače (kvantizace TDC) v grafu | 🟡 | |
| **Akumulátory členů od resetu** (7 stage, τ do 9·10⁶ s, 63 bodů) | 🟡 | v0.23.0–v0.24.0 (`adev_acc_update`) |
| Brána podle ustálení reference, zlom na mezeře | 🟡 | `meas gate on|off` |
| Pohledy **VSE / OKNO (~600 členů) / RAW** | 🟡 | tlačítko v okně ALLAN |
| **Filtr izolovaných odlehlých vzorků** (Hampel 31, 5 MAD) + surová pyramida, počet vyřazených | 🟡 | v0.27.0, `meas outlier on|off`, `status full` |
| Rekonstrukce dlouhých τ z datalogu po resetu | 🟡 | poslední souvislý úsek, jen po warm resetu |
| `allan log`: overlapping s krokem 1 s nad `.measlog` (τ do 1000 s) | 🟡 | UART, ~0,3 s |
| Fázový šum ℒ(f) (Welch FFT, 0,016–0,5 Hz) | 🟡 | okno ANALÝZA |
| Rozpočet nejistoty (k = 2), počet platných číslic, tempco Vc↔T | 🟡 | okno ANALÝZA |
| Filtr zobrazené hodnoty (průměr 8 / medián 9) | 🟡 | jen zobrazení, ne statistika |

## 4. Smyčka GPSDO (disciplinace OCXO)

| Funkce | Stav | Poznámka |
|---|---|---|
| FPGA měří čas hrany 1PPS v taktech 100 MHz | ✅ | FW 0x0424, `pps_ts` |
| FLL (měří K z kroků nad šumem oken) → PLL 2. řádu (τ 1000 s) | ✅ | LOCK po ~290 s, `gpsdo` |
| Stavy OFF / WAIT / FLL / LOCK / HOLD s důvodem | ✅ | `status`, okno GPSDO TIE |
| Oprava sawtooth (UBX‑TIM‑TP, automatická volba znaménka) | ✅ | |
| Dither DAC 1. řádu, výchozí **100 Hz** (`DacTask`), 2–200 Hz | 🟡 | `dac dither`, krátce ověřeno v0.22.0 |
| Uložení kódu DAC a jeho obnova po startu | ✅ | |
| Doporučení τ smyčky (`tau min.` — dolní mez) | 🟡 | `gpsdo_tau_advise` |
| Pasivní fázový monitor ve stavu OFF | 🟡 | v0.22.0, STATUS #319 (zkontrolovat, zda ponechat) |
| Selftest smyčky (`gpsdo test`, 3 strmosti, špatné znaménko) | ✅ | PASS po v0.21.1 |
| Holdover s fyzikálním odhadem | ⬜ | zatím jen amber „H" a ilustrativní kužel |

## 5. GPS a čas

| Funkce | Stav |
|---|---|
| NMEA parser, multi‑konstelace (GPS/GLONASS/Galileo/BeiDou) v GSV, sky plot a bargraf C/N0 | ✅ |
| RTC z LSE 32,768 kHz, synchronizace z GPS (první fix + 10 min), disciplinace LSE podle GPS | ✅ |
| Časová zóna: ruční posun nebo AUTO CET/CEST | ✅ |
| Maidenhead lokátor, self‑survey (průměr polohy), okno KVALITA GPS (z datalogu) | 🟡 |
| UBX‑CFG‑GNSS / TMODE2 (GLONASS, survey‑in) | 🟡 best‑effort, NEO‑7M může odmítnout |

## 6. Záznam dat a export

| Funkce | Stav | Poznámka |
|---|---|---|
| **Datalog** ve W25Q (32 B / 10 s, ~80 dní, přežije reset) vč. kódu DAC | ✅ | okno Datalog, `datalog` |
| **SDRAM log** `.measlog` (surová měření, ~18 h, epochy) | ✅ | `sdramlog`, web `/api/raw` |
| Export CSV na SD (`GPSDO.CSV`, zrcadlo `GPSDOLOG.CSV` s rotací při změně formátu) | 🟡 | okno SD KARTA |
| PC logger `tools/gpsdo_log.ps1` (CSV + stab.jsonl + události), `raw_fetch.py` | ✅ | `analyze_gpsdo_log.py`, `cov312.py` |
| Screenshot (BMP na SD / přes CDC) | ✅ | `screenshot sd` |
| Trvalé nastavení (CONFIG store + BKP, sestavy 8 slotů) | ✅ | |

## 7. Rozhraní

| Rozhraní | Co umí | Stav |
|---|---|---|
| **UART / USB CDC** | desítky příkazů (`status`, `stats`, `selftest`, `gpsdo`, `dac`, `tdc`, `membench`, `meas …`, `allan log` …), `help` | ✅ |
| **SCPI** (USB + TCP 5025) | `*IDN?`, `MEAS:`, `SENS:FREQ:GATE/CHAN`, `INIT/ABOR/READ?`, `CALC:` (Math, limity, `CALC:AVER:CLE`), `SYST:`, `STAT:` | ✅ (`INPut[n]:` jen `-241 Hardware missing`) |
| **Web** (HTTP 80, SPA, SSE push) | živé hodnoty, grafy (teploty, Vc/DAC, GPSDO TIE), Allan z firmwaru, GPS sky plot, historie z datalogu, ovládání (Basic Auth + `web_ctrl_en`) | ✅ |
| Web API | `/api/state`, `/api/stream`, `/api/stab`, `/api/log`, `/api/errlog`, `/api/sats`, `/api/fpga`, `/api/raw`, `POST /api/scpi` | ✅ |
| mDNS `gpsdo.local`, DHCP | ✅ (statická IP se ukládá, ale 🟡 neuplatňuje — vždy DHCP) |

## 8. Uživatelské rozhraní (displej)

| Funkce | Stav |
|---|---|
| Hlavní obrazovka (hybridní/klasické rozložení): velké číslo (až 15 číslic), Allan náhled, statistiky, mini trend, RF bargraf | ✅ |
| ~50 oken: MENU, Měření, Nástroje, Nastavení, GPS, System Health, Senzory, Grafy, Přehled kanálů, Allan/Histogram/Spektr, Analýza, GPSDO TIE, Datalog, Kvalita GPS, SD karta, Paměť, Selftest, Chyby, Čas, Síť, Alarmy, Sestavy … (seznam `s_view` v `docs/HW_REFERENCE.md`) | ✅/🟡 |
| Dvě úplné ovládací cesty: **dotyk i encoder** (fokus, registr tlačítek) | ✅ |
| 5 barevných schémat, jas + auto‑dim, animace a grafické efekty (přepínatelné) | ✅ |
| Jazyk ČESKY/ENGLISH | ➖ jen infrastruktura (texty zatím česky) |
| Boot splash + selftest, banner „DOTYK NEDOSTUPNY" při mrtvé I2C4 | ✅ |

## 9. Spolehlivost a diagnostika

| Funkce | Stav |
|---|---|
| IWDG s heartbeaty UiTask/FpgaTask, příčina resetu + crash black‑box (`stall:UiTask` …) | ✅ |
| Flight recorder (60 s před poruchou → W25Q), trvalý errlog (W25Q) | ✅ |
| `selftest` (16 čistě logických testů vč. SCPI, IPC, FFT, Allan, smyčky), běží i při bootu | ✅ |
| `status` / `stats` / `sensors` / `scanner` / `membench` / `fbdiff` / `bgcheck` | ✅ |
| Prahový monitor (VBAT, teplota OCXO, σy@1 s) s hysterezí + alarmy (ztráta signálu/GPS, limit FAIL) | ✅ (σy@1 s 🟡 výchozí vypnuto) |
| Obnova I2C1 (sběrnice senzorů), ochrana GPIO sdílených mezi jádry (`GPIO HLIDAC`) | ✅ |
| Detekce nesouladu IPC bank (`cm4_ipc_version`) | ✅ |
| Boot POST diagnostika (LED + pípání, ≥ 15 bliknutí = hodiny) | ✅ |
| Autokalibrace/self‑check napájení (verifikace, nic nezapisuje) | 🟡 |

## 10. Nástroje a build

| Nástroj | Účel |
|---|---|
| `./scripts/build.sh Release CM7|CM4|BOTH` | překlad bez IDE (kontroly modelu IDE a nesouladu IPC) |
| `python tools/audit.py` | statická analýza 92 souborů (GCC 14.3), baseline 92 OK / 0 / 2 |
| `python tools/spa/check.py --build` | povinný řetězec po editaci webu (extrakce, DOM, JSON kontrakt, JS testy, `nm`) |
| `tools/uartq.ps1`, `uartlog.ps1`, `i2c4watch.ps1`, `cold_start_log.ps1` | UART dotazy a logování |
| `tools/gpsdo_log.ps1`, `analyze_gpsdo_log.py`, `cov312.py`, `raw_fetch.py` | dlouhé běhy a analýza |
| FPGA modul: `fpga_release.py`, `fpga_load_test.py` | vydávání a ověření bitstreamu |

## 11. Co zatím NEUMÍ / otevřené

- **Vstupní modul** (relé, nastavitelný práh a hystereze, ÷10, rozsah DC–200 MHz / 1,1 GHz) — hardware se teprve staví, firmware vrací `-241`.
- **Fyzikální holdover** a odhad stárnutí OCXO (potřebuje dny provozu; STATUS #303, #311).
- **Absolutní fáze vůči UTC** — fáze smyčky je relativní, nezná zpoždění kabelu.
- **Statická IP** (ukládá se, neuplatňuje), **jazyk ENGLISH**, **protokol v2 + SPI DMA** (souvislý proud měření).
- **Allan pro τ > 1000 s s krokem 1 s** (jen pyramida), **MTIE přesně**.
- Ověření na HW čeká: dither 100 Hz dlouhodobě, A2/V1–V9 + filtr odlehlých (v0.23.0–v0.27.0), DTCM, fázový monitor v OFF.

Podrobný seznam úkolů a jejich stavu: `../STATUS.md`. Stav ověření po verzích: `docs/AUDIT_STATUS.md`.
