# Checklist: FPGA modul — schéma v2.0 (`FPGA_Module_2_0/`)

Zdroj pravdy: netlist z `Frequency_Counter_FPGA_Module/FPGA_Module_2_0/FPGA_module.kicad_sch`
(sch 2026-06-21, PDF export 2026-07-03). Stav: **rozpracováno** — U3 (Tang Nano) je odpojené,
U7 (druhý Si5356) odstraněn, PCB soubor je stále layout v1.3 (2025-06-05).

## 1. Stav v2.0 (ověřeno netlistem)

- **U3 Tang Nano: všechny I/O piny nezapojené** — sítě končí na volných koncích sériových rezistorů.
  Čeká na zapojení podle plánu níže.
- **U7 (Si5356 @0x71) odstraněn** vč. PLL_CLK5–8 → zůstává jediný U4 @0x70 (CLK_0/2/4/6 → PLL_CLK1–4).
- **Gen_Out J14/J15 osiřelé**: U4 CLK_5/CLK_7 nezapojené, R107/R110 visí ve vzduchu →
  buď dopojit U4-13→R107 a U4-9→R110 (pozor: sdílí MultiSynth s fázemi → jen 100 MHz/2^k),
  nebo J14/J15 + R71/R72/C4/C5 odstranit.
- GPSDO beze změny (÷10/÷10, CLR na GND ✓, mux ✓); U4 CLK_IN ← R42 ← 10MHz_GPSDO_CLK3 ✓;
  napájení VBUS (J7-19/20, jmenovitě 12 V) → F1/F3 → bucky ✓; U2 VBB nově ošetřen ✓.
- SPI sběrnice na J6/J7 beze změny (FPGA_CS2 = J7-7, Input_Mod_CS1 = J7-6/J6-6).

## 2. Zapojit U3 v KiCadu (hlavní TODO — implementuje tabulku hodin z revize)

| Síť (volný konec)            | U3 pozice | FPGA pin | Role / RTL port                  |
|------------------------------|-----------|----------|----------------------------------|
| PLL_CLK1 (za R35)            | 14        | **35** GCLKT_4 | **0° master → `clk_p0_100m`** |
| PLL_CLK2 (za R36)            | 12        | 34       | 90° → `clk_p2p5_100m`            |
| PLL_CLK3 (za R52)            | 13        | 40       | 180° → `clk_p5_100m`             |
| PLL_CLK4 (za R53)            | 11        | 33       | 270° → `clk_p7p5_100m`           |
| 10MHz_GPSDO_CLK4 (za R84)    | 48        | **63** RPLL_T_in | `clk_ref_10m` (+ rezerva pro rPLL) |
| DIV_Bit1/2 (za R29)          | 8         | 28       | `sig_in4` (÷4) — cst beze změny  |
| DIV_Bit3/4 (za R30)          | 7         | 27       | `sig_in16` (÷16) — cst beze změny|
| DIV_Res (U2 MR)              | 18        | 53       | **`div_res` — NOVÝ výstup RTL!** |
| DIV_Out (U2 ~TC)             | 17        | 51       | roadmap: carry preskaleru        |
| CH_B (za R33)                | 5         | 25       | roadmap: kanál B                 |
| GPS_Time_Stamp (volný pin R113) | 30     | 48       | roadmap: HW timestamp            |
| MOSI                         | 19        | 54       | `spi_mosi` ✓                     |
| SCK                          | 20        | 55       | `spi_sck` ✓                      |
| FPGA_CS2                     | 21        | 56       | `spi_cs_n` ✓                     |
| MISO                         | 22        | 57       | `spi_miso` ✓                     |
| Reset                        | 39        | 77       | roadmap: reset                   |
| PLL_INT                      | 38        | 76       | volitelné (jde i do STM J7-11)   |

Nechat nezapojené: pozice 1–4 (TF_* — TF karta se dle rozhodnutí 2026-07-06 NEpoužije; jsou to 4 volné
GPIO rezervy, PIN36 = GCLKC_4 clock-capable; při případném použití z carrieru nesmí být vložena TF karta —
sdílené spoje na modulu), 27–29, 40–47 (1V8 banka!), HDMI, pozice 25 (+3V3 modulu; napájení jde +5V přes FB1 ✓).

- [ ] Po zapojení: ERC, re-export netlistu, **Update PCB from Schematic** (pcb je v1.3!) a přeroutovat
      změněné spoje (33/35/63 hodiny, 41/42 se uvolní). Pokud je deska v1.3 už vyrobená → řezání drah, ověřit!
- [ ] Vyexportovat nové PDF do repa (stávající `FPGA_module_schematic.pdf` = rev 1.0, smazat/nahradit).

## 3. FPGA projekt (Counter_FPGA.gprj)

- [x] **`src/pins.cst`** — HOTOVO (2026-07-07) — cílový stav (změna jen u hodin + nový div_res):
  ```
  IO_LOC "clk_ref_10m"      63;   // 10MHz_GPSDO_CLK4 (bylo 41)
  IO_LOC "clk_p0_100m"      35;   // PLL_CLK1, GCLKT_4 - master 0° (bylo 33)
  IO_LOC "clk_p2p5_100m"    34;   // PLL_CLK2 - 90° (beze změny)
  IO_LOC "clk_p5_100m"      40;   // PLL_CLK3 - 180° (beze změny)
  IO_LOC "clk_p7p5_100m"    33;   // PLL_CLK4 - 270° (bylo 35)
  IO_LOC "sig_in4"          28;   // DIV_Bit1/2 (beze změny)
  IO_LOC "sig_in16"         27;   // DIV_Bit3/4 (beze změny)
  IO_LOC "div_res"          53;   // NOVÉ: MR preskaleru - držet LOW
  IO_LOC "led_tx"           16;   // on-module LED (beze změny)
  IO_LOC "spi_mosi"         54;   // SPI beze změny
  IO_LOC "spi_miso"         57;
  IO_LOC "spi_sck"          55;
  IO_LOC "spi_cs_n"         56;
  ```
  (+ IO_PORT: `div_res` LVCMOS33 DRIVE=8; ostatní IO_PORT řádky beze změny)
- [x] 🔴 **RTL: přidat výstup `div_res`** — HOTOVO (konstantní LOW) (trvale `1'b0`, příp. řízený přes SPI povel).
      Bez něj Gowin default „unused pin = pull-up" drží MC100EP016 MR HIGH = **preskaler v trvalém
      resetu → čítač nikdy nic nenaměří**. Pozn.: MR je ECL vstup buzený LVCMOS33 — funguje,
      ale mimo katalogové úrovně (ověřit při bring-up).
- [x] **`src/timing.sdc`** — HOTOVO (+ párové clock groups, false-path calm_s): fáze jako jeden hodinový systém (dnešní `-asynchronous` skrývá reálné
      2,5/5/7,5ns cesty s1/s2/s3→os_r1 v oversampleru):
  ```
  create_clock -name clk_ref_10m   -period 100.0 -waveform {0 50.0}   [get_ports {clk_ref_10m}]
  create_clock -name clk_p0_100m   -period 10.0  -waveform {0 5.0}    [get_ports {clk_p0_100m}]
  create_clock -name clk_p2p5_100m -period 10.0  -waveform {2.5 7.5}  [get_ports {clk_p2p5_100m}]
  create_clock -name clk_p5_100m   -period 10.0  -waveform {5.0 10.0} [get_ports {clk_p5_100m}]
  create_clock -name clk_p7p5_100m -period 10.0  -waveform {7.5 12.5} [get_ports {clk_p7p5_100m}]
  set_clock_groups -asynchronous -group [get_clocks {clk_ref_10m}] \
      -group [get_clocks {clk_p0_100m clk_p2p5_100m clk_p5_100m clk_p7p5_100m}]
  # phase_check toggly jsou CDC-safe (2FF) - nevynucovat na nich 2,5 ns
  set_false_path -from [get_regs {u_pc/t1 u_pc/t2 u_pc/t3}]
  ```
- [x] **SSPI/MSPI pro GUI buildy** — HOTOVO (impl/Counter_FPGA_process_config.json má SSPI/MSPI true): IDE Project → Configuration → Dual-Purpose Pin → zaškrtnout
      „Use SSPI as regular IO" (+ MSPI). Persistuje se v `impl/Counter_FPGA_process_config.json`
      (`"SSPI": true`); staré impl/ bylo smazáno → GUI vzalo default false → chyby PR2017/PR2028.
      build.tcl to řeší jen pro CLI běh.
- [x] Po buildu v pin reportu ověřeno: `clk_p0_100m` na 35 = GCLKT_4, timing 0 violations, bitstream OK (FW 0x0201).
- [ ] Regression: `sim/run.ps1` (iverilog není v PATH — doinstalovat Icarus).
- [ ] Roadmap RTL: GPS_Time_Stamp(48) HW timestamp, CH_B(25), Reset(77), PLL_INT(76), DIV_Out(51);
      kalibrace binů TDC histogramem (code-density) — kvantifikuje skew fabricových fází 34/40/33.

## 4. STM32H757 (Si5356 vždy konfiguruje STM)

- [ ] 🔴 **CBPro re-export pro U4 @0x70** — REGMAP v `si5356.c` má fáze na CLK1/2/3 (bench),
      deska používá **CLK_0/2/4/6**. Nové přiřazení (fVCO 2,2 GHz, LSB = Tvco/128 = 3,551 ps):
      | Si výstup | Síť → FPGA pin | Fáze | Offset LSB |
      |-----------|----------------|------|------------|
      | CLK_0     | PLL_CLK1 → 35  | 0°   | 0          |
      | CLK_2     | PLL_CLK2 → 34  | 90°  | 704 (0x2C0)|
      | CLK_4     | PLL_CLK3 → 40  | 180° | 1408 (0x580)|
      | CLK_6     | PLL_CLK4 → 33  | 270° | 2112 (0x840)|
      CLK_1/3/5/7 zakázat (CLK_5/7 povolit jen pokud se dopojí Gen_Out).
      Ruční edit REGMAP nedoporučen — offsety CLK4/CLK6 (regs 123/124, 131/132) se dnes nezapisují vůbec.
- [ ] GPS timepulse: 100 kHz / 1 MHz dle JP2, **duty 50 %** (XOR fázový detektor to vyžaduje).

## 5. HW opravy (v2.0 stále platné!) a bring-up

- [ ] 🔴 **U12 TLV9001IDCK — vadný symbol potvrzen i v v2.0**: pady 1=IN+, 3=IN−, 4=OUT
      (pady 3+4 spojené = follower). Skutečné DCK pouzdro: 1=OUT, 3=IN+, 4=IN− → na PCB zkrat vstupů.
      Vlastní nota „Pinout check" ve schématu — stále nevyřešená.
- [ ] 🔴 **U11 MIC920 — stejný nestandardní symbol** (1=IN+, 4=OUT). OPA365 U13/U21 jsou správně →
      chyba knihovny. Ověřit datasheet, opravit symbol/footprint.
- [ ] TS5A3159 (U10): ověřit smysl přepínání (IN=H → COM↔NO = PLL větev).
- [ ] −5 V (B0505S-1W neregulovaný): změřit pod zátěží; naprázdno ujíždí k −6 V
      (MAX9601 VCC−VEE blízko abs-max) → případně bleed/post-LDO.
- [ ] VBUS 12 V přes J7-19/20: proudová rezerva kontaktů, chybí reverzní ochrana + TVS;
      koordinace F1 (1 A) vs. 5V větev (3 A). Název „VBUS" pro 12 V je matoucí — zvážit přejmenování.
- [ ] R63/C60 smyčkového filtru: schéma 100k/100µ vs. noty „10–47k / 10–47µF" — finalizovat BOM.
- [ ] DIV_Bit1/2 a DIV_Bit3/4: osazení NU rezistorů (R26/R59/R99/R100) — které tapy Q0/Q1/Q3/Q4
      skutečně dávají ÷4 a ÷16 (RTL konstanty recip_calc 1.6e14/6.4e14 s tím počítají!).
- [ ] J5 GPS: pin2 = GPS_CLK_OUT (PLL reference), pin4 = GPS_Time_Stamp — NEO-7M má jen jeden
      TIMEPULSE → ujasnit zdroj druhého signálu.
- [ ] Bring-up: osciloskop 4×100 MHz s rozestupy 2,5 ns na pinech 35/34/40/33; 10 MHz na 63;
      `div_res` LOW → f/4 na pinu 28; SPI phase_status (present=0xF, fine_seen=0xF); histogram binů.

## 6. Plnění FPGA bitstreamem přes SPI (bez TF karty) — rozhodnutí varianty

**Přímá SSPI slave konfigurace z STM32 přes header NENÍ možná** (ověřeno z package dat
`GW1NR-9C/QFN88P.json` + PnR pin reportu):

- QFN88P nemá dedikovaný SSPI SCLK — konfigurační hodiny ve slave módech jdou přes **MCLK = pin 59**,
  který (spolu s MCS_N/MO/MI 60–62) je na modulu zapojen do vlastní konfigurační flash W25Q32
  a **není vyveden na header**.
- **MODE0/MODE1 = piny 88/87** (banka 3, 1.8V) — strapované na modulu pro boot z flash,
  na header nevyvedené → mód SSPI nelze zvolit bez zásahu do modulu.
- Na headeru z SSPI sady je jen SSPI_CS_N(55), DIN(54), SO(56), DOUT(53) — bez hodin a bez MODE je k ničemu.

**Doporučená varianta: flash-bridge přes fabric** (field-update bez USB a bez TF):

1. FPGA bootuje z modulové flash jako dosud (MSPI master).
2. Aplikační SPI protokol (piny 54–57 dle plánu výše, beze změny) se rozšíří o „flash update" povely
   (nový TYPE v rámci; bulk přenos ~350–700 kB bitstreamu po blocích s CRC).
3. RTL most: po konfiguraci jsou MSPI piny 59–62 uživatelské I/O (volba `-use_mspi_as_gpio` už je
   zapnutá!) → fabric SPI-master přepíše bitstream v modulové W25Q32.
4. Zdroj obrazu: STM32 blob store na vlastní W25Q512 (už existuje) — STM streamuje, FPGA zapisuje.
5. Aktivace nového obrazu: RECONFIG_N (pin 9, 1.8V) není na headeru → **restart napájení zařízení**
   (přijatelné pro field-update). Zvážit Gowin MultiBoot (golden + update image s fallbackem;
   `Multi_Boot: true` už je v process configu).
6. Hygiena bootu: pin 55 = SSPI_CS_N a 54 = DIN jsou během konfigurace živé config vstupy →
   **STM drží SPI piny v high-Z, dokud FPGA nenaběhne** (heartbeat / pevné ~100 ms zpoždění).

Závěr: zapojení SPI na 54/55/56/57 dle sekce 2 je pro tento cíl správné a optimální — nic se nemění,
jen se doplní RTL most + protokol + STM podpora.

## 7. Analogový front-end — revize přesnosti (2026-07-26, re-check 2026-07-27)

Z validace schématu + netlistu. **ADS1115 per-kanál PGA je už hotový ve FW**
(`ads1115_start(hi2c, ch, pga)` + tabulka `k_ads_pga[4]` v `freertos_task_sensors.c`,
zatím pod `ADS1115_HW_DIVIDERS_REV2 = 0`). Níže je HW + navazující FW, co zbývá.

**Stav po úpravách schématu 2026-07-27 (netlist + Si5356/OCXO/MIC920 datasheety ověřeny):**
- ✅ HOTOVO: U4 TLV9001 **přepinován** (buffer VBB funguje) · MAX9601 prahy R5/R7/R8/**R10=22k** +
  R8 na +5V (±0,93 V) · OCXO_VC R51=15k/R52=10k · **VBUS R55=100k/R56=4k99** · **+5V R57=15k/R58=10k** ·
  **10MHz_GPSDO_CLK3 dopojen** · **MIC920 komparátory bias 12k/12k → 1,65 V** (R104/R105 na U16 OCXO,
  R106/R107 na U18 Ext ref; na uzlu C60/C70 → zachová amplitudu). Všechny 4 ADC děliče → ±2,048 V.
- ✅ OVĚŘENO datasheetem/podklady (NENÍ chyba):
  · **Si5356 SSC_DIS** 1,048 V ∈ [0,85; 1,3] = SSC vypnutý. OEB=GND (výstupy on). XA/XB=GND.
  · **Pin 63** = RPLL_T_in = dedikovaný PLL vstup, ne boot-strap → 10 MHz OK.
  · **OCXO NVG47A1282 = sinus** (+3 dBm) → komparátor nutný; **MIC920 = op-amp** → bias nutný (přidán).
  · **Thévenin Q→EPT23 130/82** (VTT=1,28 V = VCC−2) = standardní PECL, **neměnit** (VTT musí být < VOL).
- ❌ ZBÝVÁ (jediná věc):
  1. **R54 = 10k STÁLE OSAZEN** — zatěžuje výstup AD8307 (srazí 25 mV/dB). **R54 → DNP.**
- FW po osazení desky: přepnout REV2=1; přeměřit `g_calib.gain_12v` (100k/4k99 = ×21,0) a
  `gain_5v` (15k/10k = ×2,5, dřív 8k2/10k); přidat AIN0 ×2,5 (OCXO_VC).
- Rev2 PGA tabulka ve FW: AIN0/AIN2/**AIN3**=±2,048, AIN1=±4,096.

### 7.1 ADC děliče (U8 ADS1115, VDD = 3,3 V) — namapovat na PGA hranici pod VDD
Cíl „co nejpřesněji": plný rozsah čidla → ~2,0 V, PGA ±2,048 V (plné 15bit, rezerva k VDD).
Všechny odpory **0,1 % / ≤25 ppm/°C** (u VBUS je chyba poměru ~2× tolerance).

| AIN | Signál | Změna odporů | Výsledek | PGA |
|----|--------|--------------|----------|-----|
| 0 | OCXO_VC_Sense (0–5 V) | **R51 → 15k, R52 → 10k** | 5→2,00 V | ±2,048 V |
| 1 | RF_Level (AD8307) | **R53 → 1k (sériově), R54 → DNP** | přímo | ±4,096 V |
| 2 | VBUS (0–42 V) | **R55 → 100k, R56 → 4k99** | 40→1,90 V | ±2,048 V |
| 3 | +5 V (0–5 V) | **R57 → 15k, R58 → 10k** | 5→2,00 V | ±2,048 V |

- **⚠️ AIN1 (AD8307) — kritické: NEzatěžovat výstup na GND!** Detektor má výstup 2 µA/dB
  do interního 12,5 kΩ = 25 mV/dB, bez bufferu. Stávající R53=5k1 / R54=10k (15 kΩ na G)
  sráží strmost na ~13,6 mV/dB → měření RF je zkreslené. Řešení: R54 vyjmout, R53 jen malý
  sériový (1k, ESD + anti-alias s C38), PGA ±4,096 V pokryje 0,25–2,6 V pod VDD.
- **AIN2 (VBUS) — přeběh:** dnešní R55=56k/R56=9k1 dá při 40 V **5,59 V** > abs. max vstupu
  ADS1115 (VDD+0,3 = 3,6 V) → poškození. Nové 100k/4k99: FS 42 V, při 40 V 1,90 V, Zs 4,75 k.
- C37–C40 (10n) nechat (RC pól ~2–3 kHz, pro DC monitoring OK).

### 7.2 Navazující FW (až bude HW hotový)
- V `freertos_task_sensors.c` přepnout **`ADS1115_HW_DIVIDERS_REV2` → 1** (aktivuje PGA tabulku).
- Přepočítat kalibraci `g_calib.gain_12v` (VBUS, nový poměr 105k/4,99k = ×21,04) a
  `g_calib.gain_5v` (+5 V, nový poměr 25k/10k = ×2,5).
- **AIN0 OCXO_VC_Sense**: přidat škálování ×2,5 (25k/10k) → reportovat skutečné VC napětí
  (dnes se ukládá jen mV na pinu, bez přepočtu).
- **AIN1 RF_Level**: aplikovat 2bodovou kalibraci AD8307 (slope 25 mV/dB, intercept) →
  dBm/dBW; ADC LSB (0,005 dB) není omezení, detektor ±0,5 dB je.
- Kalibrace absolutní přesnosti: změřit regulovaných +5 V a známý OCXO_VC (DAC) → dopočítat
  gain/offset ADC (maže i tempco interní reference ADS1115).
- (Volitelně) použít pin **ALERT/RDY = `ADC_RDY`** (R63 pull-up → STM J5.12) jako
  conversion-ready IRQ místo `osDelay(9)` — nastavit Hi_thresh MSB=1, Lo_thresh MSB=0,
  COMP_QUE ≠ 11.
- Layout: **Kelvin / hvězdové zemnění** dolních odporů děličů přímo ke GND pinu U8.

### 7.3 Prahy komparátorů MAX9601 (U1) — zúžit a symetrizovat
- Dnes: kanál A (R6) rozsah jezdce **±4,17 V**, kanál B (R9) **+2,61…−4,31 V** (R8 na +3V3).
  Nepoškodí (v mezích Vee…Vcc), ale rozsah je ~10× širší, než je použitelné (signál u 0 V),
  u krajů opouští common-mode → komparátor „zamrzne"; navíc kanály nejsou symetrické.
- **R5 = R7 = R8 = R10 → 22k** a **R8 přepojit z +3V3 na +5V** → oba kanály ±0,93 V.
  (Pro jemnější ±0,5 V dej 47k.) Ověřit abs. max differential input MAX9601 v datasheetu.

### 7.4 OZ / komparátory — pinout (uzavřeno)
- **U16, U18 (MIC920) OK** — symbol `OPA365xxDBV_1` (pad1=IN+, pad3=IN−, pad4=OUT) odpovídá
  reálnému nestandardnímu pinoutu MIC920 dle datasheetu. Beze změny.
- **U4 (TLV9001) — ✅ OPRAVENO 2026-07-27.** Přepinováno na pad1=OUT / pad3=IN+ / pad4=IN−
  (netlist: VBB→R33→pad3, výstup pad1 svázán s pad4=IN− → unity buffer VBB). Odpovídá reálnému
  TLV9001 DCK, buffer VBB pro reference `~D0/~D1` U5 teď funguje. (Alternativa „U4 vynechat +
  VBB přímo" už netřeba.)

### 7.5 Napájení — kontrola rozsahu VBUS
- List 5 pozn.: VBUS až 36–40 V. **LMR33630 (U23/U24)** doporučené Vin max = 36 V (abs 42 V)
  → při 40 V bez rezervy. Ověřit i napěťový derating vstupních C (C96/C97 100u, C98/C99) na ≥ 50 V.
