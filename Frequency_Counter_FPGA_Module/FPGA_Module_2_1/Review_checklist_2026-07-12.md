# Review checklist — FPGA_module, revize 2026-07-12

Zdroj: netlist z `FPGA_module.kicad_sch` (export 10:52) + render GPSDO a FPGA_core listu.
GW = fyzický pin GW1NR-9, pad = header modulu.

---

## ✅ Opraveno od 07-11 (potvrzuji)

- [x] **Kontence OCXO_VC vyřešena** — DAC drive (`OCXO_VC` = U12.VOUT, R80, C56) a monitor
      (`OCXO_VC_Sense` = U16.out, R51→ADS) jsou teď **oddělené sítě**. EFC řetězec čistý:
      `DAC → R80 47k/C68 1u (47 ms) → U14 buffer → R75 39k/C61 10u film (0,39 s) → R74 82R → VC`,
      U16 snímá VC → monitor. ✓
- [x] **gps_timepulse přesunut GW30 → GW31** (IOB15A) — CLK4 (GW29) i GPS (GW31) mají tichý
      protějšek páru. Přeslech 100MHz→GPS vyřešen. ✓
- [x] **Externí reference — kompletní front-end:** J10 → **R100 49R9 (50Ω term)** → **C73 10n** →
      **D2 BAT54S** svorky → **U25 MIC920** komparátor s **biasem R101/R102 12k (→1,65 V)** a
      nastavitelným prahem R104 10k. Kvalitativně na úrovni OCXO větve. Body 3–6 vyřešeny. ✓
- [x] **DAC AD5693R:** VDD=+5V, VLOGIC=+3V3, /RESET=+3V3, A0=GND. Plný rozsah 0–5 V + I²C. ✓
- [x] **Reset** GW49 + R50 12k pull-up na +3V3. ✓
- [x] Fázové hodiny 35/33/40/29 a měřené 25/28/41 — **žádné sdílené diff páry**. ✓
- [x] Si5356 CLK_IN napájen (CLK1 → R62 → CLK_IN); ref_clk FPGA (GW63) ← CLK3. Smyčka uzavřená. ✓
- [x] DIV_Out (÷256 PECL) zrušen. ✓

---

## 🔴 Vysoká závažnost

- [ ] **1. TLV9001 (U4, VBB buffer) — STÁLE špatný pinout.** Symbol má pin1=+in/pin3=−in/pin4=out,
      ale reálný DCK je **pin1=OUT, pin3=IN+, pin4=IN−**. Na skutečném čipu tedy: **výstop op-ampu
      budí VBB** (rve se s referencí MC100EP016A) a **oba vstupy jsou spojené** na R44/R45. →
      **bias translatoru U5 je rozbitý → DIV_Bit1/2 i DIV_Bit3/4 nefungují správně.** Tvoje poznámka
      „Pinout check" to hlásí, ale není opraveno. **Fix:** správný symbol/pinout (OUT=1, IN+=3, IN−=4),
      nebo zvážit vynechání U4 (VBB umí R44/R45 nabudit i přímo přes menší odpor).

## 🟠 Střední závažnost

- [ ] **2. R75 39k v sérii za EFC bufferem k VC.** Zdrojová impedance VC uzlu ~39k. Pokud vstup
      VC OCXO (NVG47A1282) není >>39k (mnoho OCXO má 10–100k), **EFC rozsah se přiškrtí a posune**
      + uzel je náchylný na šum. **Fix:** ověř Zin VC z DS; pokud <~1 MΩ, sniž R75 (buffer má budit
      VC nízkou impedancí — filtr dělej menším R + C61). Jinak riziko ztráty pull-range.
- [ ] **3. Časové obvody bez LDO** (OCXO, MIC920, Si5356, DAC ref stále z buck LMR33630). Spínací
      šum → fázový šum a šum EFC, přímo v rozpočtu nejistoty GPSDO. **Fix:** LDO aspoň pro OCXO a
      DAC/analogovou větev (např. TPS7A LDO za buckem).
- [ ] **4. Prescaler ÷2 tap (DIV_Bit1/2, Q0).** Při vysokém vstupu (MAX9601 do ~500 MHz) je ÷2 až
      ~250 MHz → **LVCMOS33 vstup FPGA přes header to nezvládne.** Použitelné jen pro nízké vstupy.
      **Fix:** volit tap dle rozsahu (÷16 = Q3 pro vysoké f), příp. dokumentovat rozsahy per tap.

## 🟡 Nízká / hodnoty

- [x] **5. DAC VREF cap C55 = 10n.** AD5693R DS doporučuje ~100 nF na VREF (šum interní reference).
      **Fix:** 100n.
- [x] **6. C86/C88/C91/C93 = 3p3** na výstupech LMK — tvoje poznámka „ne 33p ale 4p7". Stále 3p3;
      rozhodni (dorovnání slew 10MHz hran). Kosmetika.
- [x] **7. Reset pull-up R50 = 12k** — funkční (na +3V3), 10k je standardnější. Triviální.
- [x] **8. Prescaler MR (U3.27) = GND** — odpověď na tvou poznámku „KAM MR?": volně běžící dělič,
      reset nepotřebuješ (reciproční měření). **OK, neřeš** — leda bys chtěl synchronní reset děliče.

## Validace hodnot (klíčové, OK)

- Sériové terminace: fáze R66–R70 = 33R, měřené R31/R47/R48 = 47R, LMK R83–R90 = 33R, Si5356
  vstup R62 = 47R — konzistentní se single-ended ~50Ω trasami. ✓
- Ext. ref bias 12k/12k → 1,65 V (střed 3V3), práh R104 10k, coupling C73 10n (Xc≈1,6Ω @10MHz proti
  50Ω term — OK). ✓
- EFC filtry: 47 ms (R80/C68) + 0,39 s (R75/C61 film) — vhodné časové konstanty pro DAC-řízené EFC. ✓
- AD5693R A0=GND → adresa v rozsahu 0x4C–0x4F, nekoliduje (0x48 ADS, 0x49/0x4A TMP117, 0x70 Si5356);
  ověř konkrétní hodnotu z DS. ✓
- I²C pull-upy 4k7 (R64/R65) na jednom místě. ✓

---

## Přesnost a stabilita GPSDO

Limitní faktory tohoto zapojení (od nejkratších τ): (a) vlastní ADEV OCXO NVG47A1282 na krátkých τ;
(b) šum EFC (DAC + buffer + zdrojová Z uzlu VC + absence LDO — body 2,3); (c) kvalita GPS timepulse
na středních/dlouhých τ; (d) digitální smyčka v STM32 (návrhová).

### S NEO-7M (současný modul)
- Consumer modul, **ne timing-grade.** FPGA jemně timestampuje timepulse (100 kHz fix / 10 Hz no-fix)
  proti 100MHz OCXO základně; sawtooth (qErr) u 100 kHz nelze aplikovat per hranu.
- Timepulse jitter ~20–30 ns RMS, koreluje (hanging bridges) → neaverážuje jako 1/√N.
- **Realistický strop: ~1×10⁻¹⁰ na τ ~100 s, ~1×10⁻¹¹ na τ ~1000–10000 s.** GPS-limitováno.
  Krátkodobě (τ<10 s) drží OCXO (dle jeho ADEV, typicky 1e-11…1e-12 @1s).
- 10⁻¹² tímto modulem **nedosáhneš.**

### Upgrade na NEO-M8 — záleží na variantě
- **NEO-M8N/M8Q (standardní „8M"):** novější, o něco lepší timepulse, ale pořád consumer.
  Zlepšení jen mírné → **~1×10⁻¹¹.** Nestojí za to jako hlavní upgrade.
- **NEO-M8T (timing varianta) — ten skutečný upgrade:** hlásí **kvantizační chybu (qErr) per pulz**
  v UBX-TIM-TP a umí **timing/survey-in mód** (fixní pozice). Po sawtooth korekci je 1PPS hrana
  ~1–2 ns RMS. **Realistický strop: ~1–2×10⁻¹² na τ ~100–1000 s, k ~mid-10⁻¹³ na τ ~den.**

**Ale M8T svůj potenciál odemkne jen když:**
1. timepulse přepneš na **1 PPS** (kvůli per-pulz qErr) — pro FPGA timestamp,
2. STM32 čte **qErr z UBX-TIM-TP** a odečítá sawtooth,
3. zapneš **timing/survey-in mód** (fixní pozice),
4. vyčistíš **EFC šum** (body 2,3 — LDO, nižší R75), jinak se stropem stane šum EFC, ne GPS.

Pozn.: NEO-M8T má **jen jeden timepulse pin** — 1PPS pro digitál a 100kHz pro analog současně nejde;
při digitální cestě dej 1PPS. Krátké τ vždy limituje OCXO — ověř ADEV NVG47A1282.

---

## Celková funkční analýza

- **Datová cesta a disciplinace jsou teď architektonicky i elektricky v pořádku** — EFC smyčka je
  čistá (kontence pryč), externí reference má plnohodnotný front-end, piny FPGA jsou bez kolizí.
- **Jediná funkční vada:** TLV9001 pinout (bod 1) — láme DIV kanály. To je must-fix.
- **Pro dosažení 10⁻¹²** (s M8T) je nutné vyčistit EFC šum (LDO + série k VC) — jinak analogová
  část EFC stropí přesnost dřív než GPS.
- Zbytek (VREF cap, tapy prescaleru, LMK caps) je dolaďování.

*Pořadí oprav: 1 (DIV kanály) → 2 (EFC integrita) → 3 (LDO, kvůli 10⁻¹² cíli) → zbytek.*
