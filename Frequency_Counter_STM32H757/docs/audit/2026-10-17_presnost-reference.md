# Kde utíká přesnost reference (OCXO + smyčka GPSDO) a co zlepšit — analýza 2026-10-17

> **Fáze analýzy, kód se neměnil.** Podklady: datasheet OCXO Bliley 6891 / **NVG47A1282** (`../datasheets/`), AD5693R
> (`../datasheets/AD5693.pdf`), schéma `FPGA_Module_2_1` (aktualizováno uživatelem 2026-10-17), kód smyčky
> (`freertos_task_fpga.c`, `freertos_task_sensors.c`, FPGA `spi_app.v` modul `pps_cap`), dnešní měření
> (`docs/MERENI_A_KALIBRACE.md`: noc se smyčkou, free-run, hradlo 0,25 s), simulace ditheru
> (scratchpad `dither_sim.py`). Navazuje na STATUS #277, #303, #305, #308, #311, #312.
> „Reference" = 10 MHz z OCXO disciplinovaného GPS (a z ní 100 MHz Si5356 pro FPGA). Měřicí řetěz čítače je zvlášť (kap. 5).

## 1. Shrnutí

Řazeno podle toho, kolik stability dnes pravděpodobně stojí:

| # | zdroj | mechanismus | velikost (odhad) | τ, kde vadí | jistota | oprava | cena |
|---|---|---|---|---|---|---|---|
| 1 | **napájení OCXO** | datasheet: **±1 ppb na 1 % změny** napájení (5 V) = **2·10⁻⁸/V** | 2 mV kolísání = **4·10⁻¹¹** | 10–1000 s | ⬜ HYPOTÉZA (zdroj OCXO na schématu nedohledán, kolísání nezměřeno) | změřit (M1); vlastní nízkošumový LDO u OCXO | HW, střední |
| 2 | **dither DAC 2 Hz** | 1. řád ΣΔ po 0,5 s, RC 220 ms nestačí → schod 1 LSB | **3·10⁻¹¹ p-p**, ADEV ≤ 7·10⁻¹² @ 1 s, ≤ 1·10⁻¹² @ 10 s, tóny **−76…−85 dBc** na 0,02–1 Hz | 0,25–25 s, fázový šum blízko nosné | ✅ výpočet + simulace | **dither 200–400 Hz** (I2C1 už jede 400 kHz → ~3 % sběrnice) | **FW, malá** |
| 3 | **kvantizace 1PPS ve FPGA 10 ns** | `pps_cap` vzorkuje jen náběžnou hranou 100 MHz → σ 2,9 ns | ≈ **80 % rozptylu** zbytku po sawtooth (σ ≈ 3,2 ns) | 1–300 s fáze, volba τ | ✅ z čísel firmwaru (rozptyl 2. diferencí 63 ns²) | DDR vstup (IDDR) → 5 ns, nebo 4 fáze → 2,5 ns | FPGA, malá–střední |
| 4 | **smyčka 2. řádu vs. drift OCXO** | fázové zpoždění = drift·(1/ω_n²) | **−40 ns** při 4,4 LSB/h (změřeno −9,3 ns na 1 LSB/h) | fáze (TIE), přechody driftu | ✅ změřeno | drift jako stav (3. řád / Kalman) | FW, střední |
| 5 | **šum přijímače GPS / putování času** | NEO-7M není timing přijímač, bez pevné polohy | přebytek fáze ~4·10⁻¹¹ @ 100 s (OCXO, nebo GPS, #312) | 50–1000 s | ⬜ otevřené (#312) | timing přijímač (NEO-M8T, pevná poloha) | HW, střední |
| 6 | **teplota OCXO** | ±5 ppb na −5…+50 °C (~0,1 ppb/°C průměr) | noc: r(kód, T) = −0,95, regrese −0,21 ppb/°C (kolineární) | ≥ 1000 s, holdover | ✅ datasheet, ⬜ změřit (#311) | izolace/klid vzduchu, kompenzace ve smyčce | mech. + FW |
| 7 | **analogová ladicí cesta** (reference DAC, dělič R73+R72 proti EFC 10–100 kΩ, OPA365) | teplotní drift a 1/f šum | ref. 2–5 ppm/°C → **1,7–4,3·10⁻¹²/°C**; šum ~1 µV rms → **4,5·10⁻¹³**; dělič ⬜ až ~10⁻¹¹/°C | holdover, ≥ 1000 s | ✅/⬜ | R73 1k → 100 Ω; ratiometrická EFC z Vref OCXO | HW, malá–střední |
| 8 | **stárnutí** | 1 ppb/den (datasheet), naměřeno 1,6–5 ppb/den krátce po zapnutí | — smyčka vyrovná | holdover | ✅ | model driftu (#303), dopředná vazba | FW |

🔑 **Nejlevnější jistý zisk je dither 200 Hz (FW).** **Nejpravděpodobnější velká neznámá je napájení OCXO** a dá se zjistit bez zásahu
do přístroje (M1). **Rozhodující měření celé otázky je Rb normál, který uživatel má** (M2): změří OCXO přímo, bez GPS.

## 2. Ladicí řetěz — čísla, ze kterých se vychází

- **Citlivost EFC:** K = 3,4·10⁻¹¹ / LSB (změřeno), LSB = 76,3 µV → **4,5·10⁻⁷ / V**. Tedy **1 µV na EFC = 4,5·10⁻¹³**, 11 µV ≈ 5·10⁻¹².
- **Osazení (schéma aktualizované 2026-10-17):** U12 AD5693R (B grade, GAIN ×2, 0–5 V, interní reference 2,5 V) → **R78 100k + C68 2,2 µF
  (τ₁ = 220 ms)** → U15 OPA365 sledovač → **R73 1k0 + C61 1 µF (τ₂ = 1 ms)** → **R72 82R + C59 100 nF (8 µs)** → pin 1 OCXO (EFC 0–4 V,
  vstup 10–100 kΩ). C55 10 nF (VREF), C56 10 nF.
  ⚠️ CLAUDE.md a komentář `freertos_task_sensors.c:357` uvádějí C68 1 µF / τ₁ 100 ms → opravit (`docs:`).
- **OCXO NVG47A1282:** napájení **+5,0 V ±5 %**, 2 W ustáleně; **citlivost na napájení ±1 ppb / 1 %**; krátkodobá stabilita
  **≤ 1·10⁻¹⁰ (0,1–1 s)** (garantovaná mez, skutečnost obvykle lepší); fázový šum −120 dBc/Hz @ 10 Hz, −130 @ 100 Hz;
  teplota ±5 ppb (−5…+50 °C); stárnutí ±1 ppb/den; **výstup Vref +4,0 V ±0,2 V (max 1 mA)**.

## 3. Hardware

### 3.1 Napájení OCXO (bod 1) — ⬜ nejdřív změřit
Citlivost 2·10⁻⁸/V znamená, že pro úroveň 1·10⁻¹² musí být napájení OCXO stabilní na **≈ 50 µV** v pásmu τ 1–1000 s.
Kolísání 5V větve o 2 mV (0,04 %) by samo vysvětlilo celý dosud nepřiřazený přebytek ~4·10⁻¹¹ @ 100 s (#312).
- Na listu `GPSDO.kicad_sch` nejsou napájecí symboly +5 V (jen `+3V3`, `PWR_FLAG`); napájení OCXO zřejmě přichází konektorem
  **J8 „OCXO_OPA"** nebo hierarchicky. ⬜ **Doplnit: z jaké větve a přes jaký regulátor je OCXO napájeno, a co dalšího z ní bere proud** (FPGA, relé, LED, podsvícení…).
- **Návrh:** vlastní nízkošumový LDO jen pro OCXO (typově LT3042/LT3045, ADM7150 — šum jednotky µV rms, PSRR > 70 dB na nízkých kmitočtech),
  umístěný u OCXO, napájený z 12 V; zem OCXO hvězdicově. Teplotní koeficient výstupu LDO ~ 10 ppm/°C × 5 V = 50 µV/°C → 1·10⁻¹²/°C.
- Odběr OCXO se mění s teplotou okolí (pec: 5 W při náběhu, 2 W ustáleně), takže úbytek na přívodu se mění s teplotou → **krátký silný přívod
  nebo sense** k pinu OCXO.

### 3.2 Dither DAC (bod 2) — FW, jistý zisk
Simulace (1. řád ΣΔ → RC 220 ms → K): zlomek kódu se mění pomalu se smyčkou, takže v provozu projdou všechny řádky tabulky.

| aktualizace | zlomek | p-p [10⁻¹²] | ADEV 0,25 s | ADEV 1 s | ADEV 10 s | nejsilnější tón |
|---|---|---|---|---|---|---|
| **2 Hz (dnes)** | 0,5 | 27,7 | 7,9·10⁻¹² | ~0 | ~0 | 1 Hz, −84 dBc |
| 2 Hz | 0,1 | 30,5 | 4,0·10⁻¹² | **7,1·10⁻¹²** | ~0 | 0,2 Hz, −76 dBc |
| 2 Hz | 0,03 | 30,5 | 2,2·10⁻¹² | 2,9·10⁻¹² | **1,1·10⁻¹²** | 0,18 Hz, −86 dBc |
| 2 Hz | 0,01 | 30,5 | 1,2·10⁻¹² | 1,3·10⁻¹² | 7,3·10⁻¹³ | 0,02 Hz, −76 dBc |
| **200 Hz** | 0,01–0,5 | **0,4–0,8** | ≤ 2,4·10⁻¹³ | ~0 | ~0 | 2–100 Hz, **≤ −125 dBc** |
| 400 Hz | 0,01–0,5 | 0,2–0,4 | ~0 | ~0 | ~0 | ≤ −136 dBc |

- Dnešní dither tedy přidává **pomalé tóny 0,02–1 Hz o úrovni −76 až −85 dBc** (fázový šum OCXO tam bývá okolo −90 dBc/Hz) a ADEV
  až ~7·10⁻¹² na 1 s. Proti datasheetové mezi 1·10⁻¹⁰ to nevadí, proti skutečnému OCXO této třídy (jednotky 10⁻¹²) ano.
- **Návrh FW:** zápis DAC z vlastního časovače 200–400 Hz (krátký task s `vTaskDelayUntil` nebo TIM s odloženou prací), pod `i2c1MutexHandle`,
  jen 3B `WRITE_DAC` bez readbacku. Zápis 3 B při 400 kHz ≈ 95 µs, s režií ~150 µs → **~3 % I2C1 při 200 Hz**. Pozor na sdílení sběrnice
  s ADS1115 (převod ~8 ms se čeká mimo mutex?) a Si5356 — ⬜ ověřit, že čtení senzorů 2 Hz nebude dither blokovat déle než pár ms
  (jinak tóny na kmitočtu přerušení). **2. řád ΣΔ** tóny dál rozptýlí.
- RC 220 ms je s 200 Hz víc než dost; zvětšovat ho netřeba.

### 3.3 Snímání 1PPS ve FPGA (bod 3)
`pps_cap` (`spi_app.v:609-628`) bere hranu jen na náběžné hraně 100 MHz → kvantizace 10 ns, **σ = 10/√12 = 2,89 ns**. Firmware hlásí
rozptyl druhých diferencí fáze po opravě sawtooth **63 ns²** (CLAUDE.md, „Sawtooth"), což pro bílý šum odpovídá σ = √(63/6) = **3,24 ns**.
**Kvantizace snímání tedy tvoří ~80 % zbylého bílého šumu 1PPS** (zbytek ~1,5 ns je přijímač po opravě). Rozhodnutí z 2026-10-16
(„10 ns je proti 30 ns RMS zanedbatelné") platilo PŘED opravou sawtooth; po ní už ne.
- **Levná varianta:** vstup PIN33 přes **IDDR** (dvojitá rychlost v I/O bloku, ne v CLS) → vzorek na obou hranách → 5 ns, σ 1,44 ns,
  celkem σ ≈ 2,1 ns (−35 %). ⬜ HYPOTÉZA: dostupnost IDDR na PIN33 (GW1NR-9) a že se nezvedne CLS (dnes 85 %).
- **Lepší:** 4 fáze z rPLL (0/90/180/270°) → 2,5 ns, σ 0,72 ns, celkem ≈ 1,66 ns (−49 %). Cena: PLL + pár registrů.
- Dopad: nižší `a` (bílý PM) → menší TIE šum, kratší dolní mez τ, menší šum předaný smyčkou do OCXO kolem τ smyčky.
  Kvantizace je bílá jen díky tomu, že sawtooth NEO-7M (±~10 ns) hrany přes biny rozhazuje — s timing přijímačem (3.5) to přestane platit
  a jemnější snímání bude nutné.

### 3.4 Analogová cesta (bod 7) — hlavně pro holdover
- **Reference AD5693R (B):** 2 ppm/°C typ., 5 max → při 1,9 V **3,8–9,5 µV/°C = 1,7–4,3·10⁻¹²/°C**. Šum výstupu 6 µV p-p (0,1–10 Hz) → ~1 µV rms →
  **4,5·10⁻¹³**; offset drift ±1 µV/°C. Proti teplotě OCXO samotného (~1·10⁻¹⁰/°C) zanedbatelné; ve smyčce se vyrovná.
- **Dělič R73 + R72 (1 082 Ω) proti vstupu EFC 10–100 kΩ:** podíl Rs/(Rs+Rin) = 1–10 %. Kdyby měl vstup EFC teplotní koeficient
  ~300 ppm/°C, dává to při Rin 10 kΩ až **56 µV/°C ≈ 2,5·10⁻¹¹/°C**. ⬜ HYPOTÉZA (koeficient vstupu neznámý; síť může být uvnitř pece).
  **Návrh:** R73 1k → **100 Ω** a C61 1 µF → **10 µF** (τ₂ zůstane 1 ms) → 10× menší citlivost. ⬜ Ověřit stabilitu OPA365 s 10 µF přes 100 Ω (izolační R).
- **Ratiometrická EFC (střednědobě):** OCXO má **Vref 4,0 V**. DAC s externí referencí (AD5693 bez „R", nebo AD5693R s vypnutou interní
  referencí — ⬜ ověřit v datasheetu, že VREF pin pak funguje jako vstup) z Vref OCXO, **GAIN ×1 → 0–4 V = přesně rozsah EFC**, LSB 61 µV
  (o 20 % jemnější). Drift reference DAC pak sleduje vnitřní referenci OCXO a teplotní člen DAC zmizí.
- C56 je ve schématu 10 nF; CLAUDE.md uvádí „C56 neosazen (limit 2 nF na VOUT)". ⬜ Sjednotit (datasheet AD5693R: kapacitní zátěž výstupu).

### 3.5 GPS přijímač a anténa (bod 5)
- Anténa na střeše, 360° (uživatel) ✅. Externí GPSDO má okenní anténu 160° → jeho chyby GPS jsou na našich **nezávislé** (kovariance použitelná).
- **NEO-7M** není timing přijímač: specifikace 1PPS ~30 ns RMS, nemá režim pevné polohy (TMODE2 NAKne, #53). Timing přijímač
  (**NEO-M8T / LEA-M8T**) s pevnou polohou a qErr typicky výrazně sníží putování času na 100–1000 s. ⬜ Zvažovat až podle M2:
  pokud přebytek @ 100 s není OCXO, je to nejúčinnější HW změna pro dlouhá τ.

### 3.6 Teplota (bod 6)
Datasheet ±5 ppb na 55 °C; noc 2026-10-17 r(kód, T) = −0,95 (kolineární s časem). Krabice bez proudění vzduchu sníží pomalé změny;
teplota OCXO (TMP117 0x49) je k dispozici pro kompenzaci (kap. 4.4).

## 4. Firmware a matematika

### 4.1 Dither 200–400 Hz (+ 2. řád) — viz 3.2. **Nejvyšší poměr zisk/cena.**

### 4.2 Smyčka: drift jako stav (bod 4)
Dnešní PLL 2. řádu (PI) sleduje lineární drift kmitočtu s konstantní fázovou chybou x = D/ω_n². Změřeno: −9,3 ns na 1 LSB/h
(1/ω_n² ≈ 9,9·10⁵ s²), při driftu 4,4 LSB/h ≈ **−40 ns**. Kmitočet je v průměru správně, ale fáze (TIE) se posouvá s driftem a při změně
driftu (teplota, stárnutí) má smyčka přechod.
- **Návrh:** přidat odhad driftu (3. řád, nebo Kalmanův filtr se stavy fáze, kmitočet, drift). Kalman navíc umí váhovat podle změřeného
  šumu 1PPS (`a`) a OCXO → optimální τ se nastaví sám.
- ⚠️ Zásah do smyčky = **nejdřív simulace** nad stávajícím modelem `gp_core` (selftest `gpsdo test` ho už má), pak A/B na desce.

### 4.3 Časová konstanta smyčky
Dnes τ = 1000 s; dolní mez z fáze („tau min.") 100–170 s. Optimum leží výš, pokud je přebytek z GPS. Rozhodne M2 (OCXO proti Rb):
z ADEV OCXO a bílého PM `a` se spočítá průsečík. Do té doby τ neměnit.

### 4.4 Teplotní kompenzace (dopředná vazba)
Až bude změřený tempco (#311): `kód += c_T · (T_ocxo − T_ref)` mimo smyčku → smyčka vyrovnává jen zbytek; hlavně lepší **holdover**
(#303). Bez změřeného koeficientu to nedělat (kolinearita z noci by dala špatné číslo).

### 4.5 Sawtooth
Funguje (494 → 63 ns²). Po zjemnění snímání (3.3) přepočítat: opravu q dělí přijímač po 1 ns, snímání po 10 ns — dnes zbytek
dominuje snímání, ne q.

## 5. Měřicí strana čítače (nemění referenci, ale určuje, co uvidíme)
- **Anomálie 1 s vs 0,25 s:** ADEV firmwaru @ 1 s = 1,07·10⁻¹⁰ (hradlo 1 s) vs 3,9·10⁻¹¹ (4 × 0,25 s). Čtyři navazující okna by měla dát
  stejný šum jako jedno 1s okno. ⬜ Vyšetřit (střídat hradla po 10 min ve stejných podmínkách; regresní blok FPGA u delšího okna?).
- **ADEV bez stropu 60 párů** (akumulátory, `2026-10-17_volny-vykon.md` A2) — kratká τ dnes vychází z poslední minuty.
- Podlaha čítače ~4·10⁻¹¹/τ (hradlo 0,25 s) stačí na OCXO od τ ≈ 20–50 s.

## 6. Měření k potvrzení (od nejlevnějšího)
- **M1 — napájení (bez zásahu do přístroje):** do `gpsdo_log.ps1` přidat `v5_mv`, `v12_mv` (už jsou v `/api/state`; ADS1115 LSB ~0,25 mV
  na 5V větvi) a korelovat s fází/kmitočtem. Doplňkově záměrný skok zátěže (zapnout podsvícení naplno / relé) a sledovat `y`.
- **M2 — Rb normál (uživatel má Efratom FRK-LLN):** Rb 10 MHz na vstup CH A → čítač měří **náš OCXO proti Rb**:
  (a) `gpsdo off` (DAC drží kód) ≥ 6 h → ADEV OCXO 20–3000 s, (b) smyčka zapnutá ≥ 12 h → skutečná stabilita reference.
  Rozhodne #312 i optimální τ. Rb: ~1·10⁻¹¹ @ 1 s, ~1·10⁻¹² za den (článek 2018).
- **M3 — dither A/B:** po změně FW (3.2) střídat 2 Hz / 200 Hz proti Rb, hradlo 0,25 s.
- **M4 — teplota (#311):** řízený teplotní skok, krabice.

## 7. Doporučené pořadí (k rozhodnutí)
1. **M1** (logger, 0 rizika) a **M2** (Rb — nejvíc informace za nejmenší cenu).
2. **Dither 200 Hz** (FW, malý zásah, jistý zisk) + M3.
3. Podle M1: **napájení OCXO** (vlastní LDO).
4. **IDDR snímání 1PPS** (FPGA) — po ověření dostupnosti a CLS.
5. **Drift ve smyčce / Kalman** (FW, se simulací).
6. τ podle M2; teplotní kompenzace podle M4.
7. Holdover/drobnosti: R73 100 Ω + C61 10 µF; ratiometrická EFC; timing přijímač podle M2.
