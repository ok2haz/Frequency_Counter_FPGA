# FUNKCE.md — co přístroj umí a co je implementované

> **Účel:** jeden přehled schopností hardwaru a firmwaru a jejich *stavu*. Není to návod ani popis kódu — hloubku drží
> `CLAUDE.md` (jak to funguje), `docs/HW_REFERENCE.md` (tabulky), `../STATUS.md` (úkoly), `docs/MERENI_A_KALIBRACE.md` (měření).
> **Udržovat:** při každé nové funkci nebo změně stavu upravit řádek zde (spolu s commitem, který ji mění).
> Stav k **2026-10-18, firmware v0.28.0, IPC v28, FPGA FW 0x0424**.

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
| Okno **FUNKCE MĚŘENÍ**: Frekvence A/B, Perioda A/B | 🟡 | od v0.28.0 (B = FREQ/PERIOD + CHAN B); zbylé funkce šedé s důvodem, limity v kap. 12 |
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
| Offset, σ(n−1), drift, histogram (posledních 120 s), trend až 60 dní | 🟡 | hlavní obrazovka + okna |
| Spektrogram Δf — barevný pás odchylky v čase (displej: ring 720 s, plní se i za zavřeného okna; web: karta + detail) | 🟡 | v0.28.0; není to FFT spektrogram (kap. 12) |
| **Allan ADEV / MDEV / HDEV / TDEV** (overlapping), konfidenční pás z EDF, typ šumu | 🟡 | okno ALLAN, web |
| MTIE | ➖ jen odhad | potřebuje uloženou fázi |
| Podlaha čítače (kvantizace TDC, model σ znacky 105 ps) v grafu — přes celou časovou osu, kde opouští osu dole, značka | 🟡 | v0.28.0 (do v0.27.2 končila kolem 18 s, mez 1e-11) |
| **Akumulátory členů od resetu** (7 stage, τ do 9·10⁶ s, 63 bodů) | 🟡 | v0.23.0–v0.24.0 (`adev_acc_update`) |
| Brána podle ustálení reference, zlom na mezeře | 🟡 | `meas gate on|off` |
| Pohledy **VSE / OKNO (~600 členů) / RAW** | 🟡 | tlačítko v okně ALLAN |
| **Filtr izolovaných odlehlých vzorků** (Hampel 31, 7 MAD) + surová pyramida, počet vyřazených | 🟡 | v0.27.0, `meas outlier on|off`, `status full` |
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
| Holdover s fyzikálním odhadem | ⬜ | zatím jen amber „H" a ilustrativní kužel (displej i web od v0.28.0, šířka podle stavu, ne změřená) |

## 5. GPS a čas

| Funkce | Stav |
|---|---|
| NMEA parser, multi‑konstelace (GPS/GLONASS/Galileo/BeiDou) v GSV, sky plot a bargraf C/N0 | ✅ |
| RTC z LSE 32,768 kHz, synchronizace z GPS (první fix + 10 min), disciplinace LSE podle GPS | ✅ |
| Časová zóna: ruční posun nebo AUTO CET/CEST | ✅ |
| Maidenhead lokátor, self‑survey (průměr polohy), okno KVALITA GPS (displej z datalogu; web z živého okna relace, karta + detail od v0.28.0) | 🟡 |
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

## 12. Měřicí funkce a grafy — limity, vylepšení, optimalizace (2026-10-18, v0.28.0)

> Všechno níže vychází ze čtení zdrojů, ne z měření na desce (deska se při tomto průchodu nepoužívala). Co je číslo z kódu,
> má odkaz; co je odhad, je označeno **HYPOTÉZA**. Úkoly jsou ve `../STATUS.md` #323.

### 12.1 Okno FUNKCE MĚŘENÍ — co je odemčené a proč zbytek ne

| Funkce | Stav | Limit / co chybí | Vylepšení |
|---|---|---|---|
| Frekvence A, Perioda A | ✅ | hodnota na velkém čísle je vždy jeden kanál; perioda je 1/f (kap. 12.2) | přesnější perioda (12.2) |
| **Frekvence B, Perioda B** | 🟡 od v0.28.0 | je to totéž co FREQ/PERIOD + tlačítko CHAN B: FPGA vybraný kanál dá do primárního slotu, **jen A má regresní blok** (`rg_*`; u 10 MHz z GPSDO regrese stejně nic nepřidá, TDC_MATEMATIKA); přepnutí kanálu **nenuluje Allan** (kanály mají jinou podlahu, po přepnutí RESET ALLAN) | po přepnutí kanálu zlom pyramidy (jako při změně signálu), nebo oddělená pyramida B |
| Poměr A/B | ⬜ data JSOU (oba kanály v rámci současně: `edge_count/gate_ps` a `edges_b/dt_b_ps`) | chybí **režim zobrazení**: poměr dvou 10 MHz je 1 ± 10⁻¹⁰, tedy 12+ desetin, formátovač velkého čísla umí 7 (`FREQ_FRAC_HIRES`); a třetí funkce nevejde do 1 bitu `st.mode` v `g_ui_cfg` | `ratio = f_A/f_B` v `double` → odchylka od 1 v ppt/ppb jako „Odchylka ×N"; persistence funkce na 2 bity |
| Časový interval A–B, Fáze A–B | ⬜ | rámec nese délky oken (`dt_a`, `dt_b`) a kódy TDC, ale **ne společné razítko hran A a B** vůči stejnému taktu 100 MHz; konstantní offset kanálů (STA 6,23 vs 6,14 ns) se kalibruje relé vstupního modulu | protokol v3 (razítko A a B), kalibrace offsetu A↔B |
| Totalize, šířka pulzu, střída | ⬜ | FPGA to neměří (gap‑free okna, měření šířky pulzu) | RTL |

### 12.2 Perioda — jak se počítá a proč by šla přesněji

- **Dnes:** `disp_update()` (screen_main.c) vezme hi-res kmitočet `s_freq_n` (LSB = 10⁻⁷ Hz, 7 desetin), udělá z něj `double` a `T = 1/f`, zaokrouhlí na **nejvýš 7 desetin zvolené jednotky** (s … ps) a poslední **2 číslice vždy vyšedí** (pevně, `num_layout(…, 2)`; u kmitočtu se jejich počet odvozuje z rozlišení hradla).
- **Limit 1 — nízké kmitočty.** Rozlišení je dané LSB kmitočtu: relativně `10⁻⁷ Hz / f`. Při 1 Hz je to 10⁻⁷, zatímco měření samo (σ značky 105 ps, okno ≥ 1 s) dává `√2·105 ps / 1 s ≈ 1,5·10⁻¹⁰` — perioda ztrácí **zhruba tři řády** (7 místo ~10 platných číslic). Od ~100 Hz výš se to schová do šumu (u 10 MHz a hradla 0,25 s je rozlišení 5,9·10⁻¹⁰ a perioda 100,0000000 ns má 10 číslic).
- **Limit 2 — pevný strop 7 desetin** jednotky: u 1 s je to 100 ns, ač měření nese ~150 ps.
- **Vylepšení:** počítat `T = gate_ps / edges` přímo (celočíselné dělení ve fixním bodu, stejně jako `fpga_freq_scaled`), počet desetin odvodit z `u_res` (jako `freq_uncertain_frac`) a ne z pevných 7; počet nejistých číslic taky z `u_res`. **Riziko:** zasahuje formátovač velkého čísla (`num_build_for`, `num_layout`, `NUM_SEG_MAX`); bez desky ověřit nejde → nedělat naslepo.

### 12.3 Histogram — z kolika vzorků a je to dobře?

- **Dnes:** z plochého kruhu `s_y[]` = **posledních 120 vzorků po 1 s (2 minuty)**, kdykoli `STAT_N` = 120 (sdílí ho trend 60 s, offset, drift, σy@1 s). 24 binů v rozsahu min…max, tedy ~5 vzorků na bin; **střední hodnota, medián i směrodatná odchylka jsou spočtené správně** (od v0.28.0 s n−1; do v0.27.2 s n).
- **Co je slabé:** 2 minuty je krátké na ocasy (nad 2σ čekáš ~5 vzorků, nad 3σ pod jedním) a rozsah min…max ho táhne jediný odlehlý vzorek (zbytek se slije do pár binů); Gaussova křivka se proto dá „dobře“ nakreslit i na data, která Gaussova nejsou. Histogram také **nezná filtr odlehlých** ani **bránu reference** (plní se z `stats_push` stejně jako vše).
- **Vylepšení:** samostatný ring 1 800 – 3 600 vzorků (30–60 min) jako `float` relativně k referenci (7–14 kB, DTCM ještě zbývá ~26 kB); počet binů ≈ √n nebo Freedman–Diaconis; rozsah ±4σ s počítadly „pod/nad“; odečíst lineární trend okna (drift OCXO jinak rozšíří rozdělení); ukázat N a šířku binu. Web má vlastní okno (`win`) a vlastní histogram nad `H.f`.

### 12.4 Podlaha čítače v Allanu

- **Proč končila na ~10–18 s:** v `allan_plot_curve` (displej) i v `drawStab` (web) byla mez „podlaha nejde pod 1e‑11“ (commit `16e7a5e`, zavedeno proto, aby se osa neroztáhla dolů k podlaze). Model `ADEV = √12·σ_značky /(2τ) ≈ 1,8·10⁻¹⁰/τ` překročí 10⁻¹¹ při τ ≈ 18 s. Mez byla moje volba, ne fyzika.
- **Dnes (v0.28.0):** čára se kreslí **přes celou osu τ, dokud je v rozsahu osy**; osu dál řídí jen křivka (požadavek z 2026‑10‑07). Kde podlaha osu opustí dole, je na dolní hraně **trojúhelníková značka** (displej) / zpráva „od τ … leží pod osou“ (web). Víc ukázat nejde, aniž by se osa stáhla k podlaze: u τ = 10⁵ s je podlaha ~10⁻¹⁵ a křivka ~10⁻¹².
- **Limity modelu:** σ značky **105 ps** je jedna konstanta (`MP_TDC_PS`) pro oba kanály a pro signál asynchronní k referenci; u koherentního signálu (GPSDO 10 MHz vs. hodiny 100 MHz z téhož OCXO řetězce) kvantizace není náhodná a křivka může ležet i pod čarou. **HYPOTÉZA:** kanál B bez regrese má šum o něco vyšší.
- **Vylepšení — měřit místo modelovat:** oba kanály dostávají tentýž signál z externího GPSDO, každý přes vlastní budič 7404 → ADEV rozdílu `(f_A − f_B)/f` je přímo **podlaha dvou kanálů vč. budičů** (společný šum zdroje se odečte). Data jsou v rámci už dnes. Nová křivka „A−B“ do Allanu by podlahu ověřila a vyměnila odhad `105 ps` za změřenou hodnotu.

### 12.5 Spektrogram

- **Web:** karta měla v náhledu **prázdný pás**: `drawSpec` skládal `<rect … height=100/>` s nekvótovaným atributem, a `/` je v HTML parseru součást hodnoty (`height="100/"`) → obdélník nulové výšky (detail používal `setAttribute`, proto fungoval). Stejná chyba byla u sloupců histogramu. Opraveno ve v0.28.0 (`/>` s mezerou). **Jak poznat podobné chyby:** v `innerHTML` nikdy `…=hodnota/>` bez mezery. Vizuálně neověřeno (Chrome rozšíření nebylo připojeno).
- **Displej:** kreslil jen za otevřeného okna (sloupec 2×/s, pevná škála ±0,4 Hz — u GPSDO plochá modrá). Od v0.28.0 historii drží `screen_main` (720 vzorků po 1 s = 12 min, DTCM, plní se vždy) a okno ji při otevření **překreslí**; škála je ±3σ okna a ukázaná v popisku.
- **Limity:** je to **barevný pás odchylky v čase**, ne spektrogram (kmitočet × čas); 12 min je málo na teplotní cykly. **Vylepšení:** druhý ring po 10 s (2 h) nebo decimace; skutečný spektrogram = krátkodobé FFT (`phase_noise.c` už FFT umí) → osa y = offset kmitočet, což ukáže periodicity (klimatizace) zřetelněji.
- **Data máme?** Ano: každé 1 s (`stats_push`) je vzorek průměru 4 měření; displej i web mají vlastní řadu (web ~4/s z SSE).

### 12.6 Kvalita GPS a Holdover

- **Kvalita GPS na webu nešla zvětšit** — karta neměla `data-z`. Od v0.28.0 detail (družice vlevo, HDOP vpravo, každý se svou osou). **Limit:** web kreslí jen **živé okno relace**; `/api/log` pole `sats`/`hdop10` neposílá (bod = `[t, f, ocxo, deska, vc, vbat, flags, lo, hi]`). **Vylepšení:** přidat `sats`/`hdop` do bodu (`ipc_log_rec_t`, `HTTPD_LOG_PT_MAX` — `_Static_assert` rozpočtu to zastaví), pak by web měl totéž, co displej (dny).
- **Holdover na webu neměl kužel.** Od v0.28.0 ho má (zrcadlo `holdover_cone_draw`: šířka podle stavu, v holdoveru roste s dobou). **Je ilustrativní:** šířka není změřená. **Vylepšení (STATUS #303/#311):** kvantitativní kužel z `x(t) = x₀ + y₀·t + ½·D·t² ± σy(t)·t`, kde `D` je stárnutí z kódu DAC v čase a `σy` z pyramidy; údaj „±X ns za 1 h“.

### 12.7 Optimalizace

- `wf_repaint()` (spektrogram) kreslí až 720 sloupců samostatně; při změně škály o > 30 % to stojí řádově milisekundy v UiTasku (limit 10 ms) — **HYPOTÉZA**, neměřeno. Úprava: kreslit po pásech, nebo držet barevný bitmap (720×1 px řádek) a jen blitovat.
- Pyramidy a nové kruhy jsou v DTCM (`s_wf` 2,9 kB; celkem ~38 kB z 64 kB).
- Všechny nové věci jsou **neověřené na HW**.

Podrobný seznam úkolů a jejich stavu: `../STATUS.md`. Stav ověření po verzích: `docs/AUDIT_STATUS.md`.
