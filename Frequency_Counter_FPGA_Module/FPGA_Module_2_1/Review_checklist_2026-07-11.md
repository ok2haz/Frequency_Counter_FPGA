# Review checklist — FPGA_module, revize 2026-07-11

Zdroj: netlist z `FPGA_module.kicad_sch` (export 22:52) + render GPSDO listu. Označení: GW = fyzický pin GW1NR-9, pad = header modulu.

---

## ✅ Správně vyřešeno (potvrzuji tvé změny)

- [x] DAC AD5693R (U12): **VDD = +5 V, VLOGIC = +3V3** → plný rozsah 0–5 V EFC + I²C úroveň. (Vyřešen dřívější 3,3V strop.)
- [x] Analogová XOR smyčka, dělič 74HC390, JP1 a detektor přítomnosti GPS **odstraněny** → čistě digitální cesta.
- [x] Reference MUX U18 (TS5A3159) **na logické úrovni** — obě větve tvarované Schmitty (OCXO přes U17, ext. přes U19), muxuje čisté hodiny. Čistá varianta.
- [x] `Ref_Ctrl` (ref_sel) na **GW51** → U18.6 (IN). ✓
- [x] `Reset` přesunut z GW77 (1,8V!) na **GW49** + pull-up R50. ✓
- [x] `div2` (DIV_Bit3/4) přesunut na **GW41** — pár IOB11 s `div1` rozdělen. ✓
- [x] Fázové hodiny 35/33/40/29 **mezi sebou nesdílí diff pár**. ✓

---

## 🔴 Kritické

- [x] **1. Kontence na uzlu OCXO_VC — DAC výstup zkratovaný s výstupem op-ampu.**
      Síť `OCXO_VC` obsahuje současně `U12.VOUT` (výstup DAC) **i `U16.1` (výstup OPA365)** —
      dva nízkoimpedanční výstupy proti sobě. Navíc monitor (J9, R51→ADS) visí přímo na uzlu DAC.
      **Fix:** překreslit: DAC `VOUT → R80/filtr → U14(+in)`, U14 jako buffer `→ R75/R74 → VC`
      (drive). **U16 jako oddělený sense buffer:** `+in ← VC pin`, `out → vlastní síť → J9 + R51`
      (monitor). Výstup U16 ani J9/R51 nesmí být na uzlu DAC.
- [x] **2. GW29 (PLL_CLK4, 100 MHz) a GW30 (GPS timepulse) = pár IOB13.**
      100MHz agresor indukuje jitter do pomalé GPS hrany → degraduje disciplinaci.
      **Fix:** přesunout `gps_timepulse` (R46, dnes na GW30) na **GW31** (IOB15A; pár GW32 volný).
      GW29 nech CLK4, pár GW30 pak tichý.

## 🟠 Externí reference — signálová integrita

- [x] **3. Chybí 50Ω terminace na J10 (Ext. Ref.).** Externí 10MHz normál (Rb/Cs/lab) je 50Ω
      zdroj se sinusem ~+7…+13 dBm. Bez terminace → odrazy, špatná amplituda, zvonění.
      **Fix:** 50R (49R9) z uzlu J10 na GND.
- [x] **4. Chybí bias na práh Schmittu U19.** Vstup je jen `J10 → C73 → svorky D2/D3 → U19`.
      AC-couplovaný sinus má DC úroveň danou jen vodivostí svorek, ne středem hystereze
      (~1,5 V). → nesymetrické křížení prahu = duty distorze/jitter; u malé amplitudy netogluje.
      **Fix:** bias dělič (2×10k z +3V3 a GND do uzlu vstupu), příp. self-bias 1M přes hysterezi.
      (OCXO větev přes U15 MIC920 má nastavitelný práh R77 — externí větev by měla mít ekvivalent.)
- [x] **5. C73 = 100p — po přidání 50Ω terminace je moc malý** (159Ω @10 MHz → útlum a fázový
      posun). **Fix:** zvětšit na ~1–10 nF (Xc << 50Ω).
- [x] **6. D2/D3 = BAR64 jako svorky.** BAR64 je PIN dioda (RF switch/atenuátor) — vysoká
      kapacita, pomalé zotavení. **Fix:** BAV99 / BAT54S (rychlé signálové/Schottky svorky).

## 🟡 Piny a hodnoty

- [ ] **7. Fázové hodiny: GW35 je JEDINÝ použitelný GCLK na headeru** — potvrzeno z UG803 (QN88).
      Header vyvádí jen 3 GCLK-schopné piny: 35 (IOB29A/GCLKT_4 = pravý, single-ended OK),
      36 (GCLKC_4) a 51 (GCLKC_3) — ale GCLKC piny jsou globální hodinou JEN v diferenciálním
      páru, single-ended jsou obyčejné IO (pozn. [2] datasheetu). Ostatní GCLK páry (0,1,2,5,6,7)
      nejsou na headeru; pin 52 (GCLKT_3) drží onboard 27MHz XO. **Všechny 4 fáze na GCLK tedy
      NEJDE** — HW limit modulu. Fáze 1–3 přes fabric + DNL je jediná varianta.
      **phase0/coarse MUSÍ zůstat na GW35.** Ověř, že HDL mapuje coarse sem.
      (Alternativa „4 fáze z interního rPLL" = všechny na globálním stromu, ale horší jitter/fázová
      přesnost než Si5356 → nedoporučeno, ztratíš DNL-kalibrovatelnost.)
- [x] **8. C86/C88/C91/C93 = 3p3** na výstupech LMK — tvoje vlastní poznámka „ne 33p ale 4p7".
      Zvážit 4p7 (dorovnání slew 10MHz hran na SMA/Si5356 vstupech).
- [ ] **9. Reset pull-up R50 = 12k** — ověř, že jde na +3V3 (v netlistu sdílí uzel s R59/R61/R63);
      10k je standardnější. Aktivní úroveň sladit s HDL (reset_n → pull-up).
- [ ] **10. Série fázových hodin R66/R67/R68/R70 = 33R** a měřených R31/R47/R48 = 47R — OK jako
      sériová terminace; ověř jen konzistenci s délkou tras (impedance ~50Ω).

## 🟢 Ověřit / drobné

- [ ] **11. Tiché protějšky párů** (GW26, 27, 32, 34, 36, 39, 42 + GW30 po přesunu GPS) →
      nastavit „Unused Pin = Input, Pull-Down" v Gowin projektu (proti přeslechu).
- [ ] **12. `gps_extint` (GW48) pár s `reset` (GW49) = IOR24** — oba pomalé/kvazistatické, OK.
- [ ] **13. Napájení časových obvodů z buck měničů** (OCXO, MIC920, Si5356, LMK) bez LDO —
      pro EFC šum a fázový šum zvážit LDO aspoň pro OCXO a DAC referenci (přetrvává z předchozí revize).

---

## Celková funkční analýza

- **Datová cesta čítače** (měřené vstupy → fáze → Si5356 → 100 MHz → timestamp) je konzistentní;
  fázové hodiny jsou v jedné bance (dobrá lokalita/skew), jediná pinová vada je pár CLK4/GPS (bod 2).
- **Digitální disciplinace** je architektonicky správně: DAC (5V, plný rozsah) + ctrl_fsm na osc_free,
  Ref_Ctrl → MUX. **Ale reálná EFC cesta je rozbitá kontencí (bod 1)** — dokud se uzel OCXO_VC
  nerozdělí, DAC nebude ovládat OCXO deterministicky a hrozí poškození výstupů.
- **Přepínání reference** OCXO/ext je logicky správné (mux na logické úrovni, select z FPGA), ale
  **externí vstup není použitelný pro 50Ω sinusový normál** bez terminace a biasu (body 3–5). Pro
  logický (TTL) externí zdroj by fungoval, ale AC vazba (C73) napovídá záměr sinus → dořeš front-end.
- **Strop přesnosti** zůstává dán GPS (NEO-M8T ~10⁻¹² s 1PPS + sawtooth) a šumem EFC — proto je
  čistota uzlu VC (bod 1) a napájení (bod 13) přímo v rozpočtu nejistoty.

*Pořadí oprav: 1 (deska jinak neřídí OCXO) → 2 (přesun GPS) → 3–6 (ext. ref front-end) → zbytek.*
