# Review checklist — Counter_FPGA_Board (FPGA_module, rev 1.0)

Datum review: 2026-07-10. Zdroj: FPGA_module.pdf (netlist z KiCad 10.0.4), `zadani_fpga_citac.md`,
`kicad_pinout_gw1nr9_qn88_v5_4phase_ddr.csv`, firmware `prg/main.c` + `prg/Si5356A_Reg.c`.

---

## 🔴 Kritické — bez opravy deska neměří

- [x] ~~**1. MAX9601 latch enable obráceně (U1).**~~ **STAŽENO 2026-07-10 — nález byl chybný,
      zapojení JE správné.** Dle Table 1 v MAX9601 EV kit dokumentu: LE=0, /LE=1 → **compare mód**;
      LE=1, /LE=0 → latch. Schéma (LE=GND, /LE=+3V3) tedy dává trvalý compare mód, přesně jak má.
      („Latch Enable" je u této rodiny aktivní v high — LE high teprve latchuje.) Žádná akce.
- [ ] **2. U4 TLV9001 (VBB buffer) — prohozený OUT/IN+ vs reálný DCK pinout.** Reálné SC70-5:
      1=OUT, 2=V−, 3=IN+, 4=IN−, 5=V+. Symbol má 1=IN+, 4=OUT. Teď: VBB → pin 1 (=skutečný OUT),
      piny 3+4 spojené → bias ~D0/~D1 pro U5 nefunkční → DIV_Bit1/2 a DIV_Bit3/4 mrtvé.
      **Fix:** VBB → pin 3 (IN+), pin 1 (OUT) → R44/R45, zpětná vazba pin 1 → pin 4 (IN−).
      (Vlastní poznámka „Pinout check" ve schématu tím pádem vyřešena — smazat.)
- [ ] **3. DIV_Out: LVPECL ~TC (U3 pin 12) přímo na FPGA pin 51.** (a) otevřený emitor bez
      pull-down → výstup vůbec korektně nespíná; (b) PECL úrovně 1,5–2,4 V nesplní VIL 0,8 V
      LVCMOS33. **Fix:** protáhnout TC/~TC přes volnou polovinu U2 (D0/~D0 → Q0; bias síť
      R27/R28 už existuje) a do FPGA poslat LVTTL.
- [ ] **4a. Firmware: kolize signatur `si5356_write_register`.** `main.c:61` deklaruje
      `(dev_addr, reg, val)`, definice v `Si5356A_Reg.c:10` je `(reg, value, mask)`.
      Helpery `si5356_soft_reset` / `si5356_powerdown_all_outputs` / `si5356_enable_output`
      reálně zapisují do registru 0xE2 nesmysly. **Fix:** sjednotit na jednu signaturu.
- [ ] **4b. Firmware: I2C adresa Si5356A.** Kód používá `0x71<<1`, deska strapuje I2C_LSB=GND
      → **0x70**. Na této desce každý zápis NACKne. **Fix:** `0x70<<1`.

## 🟠 Vážná rizika a nestability

- [ ] **5. 74HC390 (IC1) na +5 V s 3,3V hodinami z LMK1C1104.** VIH @5 V = 3,5 V → vstup pod
      spec, teplotně nespolehlivé; je to referenční dělič GPSDO. **Fix:** napájet IC1 z +3V3.
- [ ] **6. Backfeed 5 V do Tang Nano.** Deska tlačí +5 V přes FB1 do 5V pinu modulu, který je
      zároveň na USB VBUS (a USB-C se má používat pro UART/JTAG dle zadání). **Fix:**
      Schottky / ideal-diode OR na desce; do té doby zákaz současného připojení.
- [ ] **7. −5 V z B0505S-1W (neregulovaný) přímo na VEE MAX9601.** Při malé zátěži až
      −5,5…−5,9 V (doporučené max −5,5, abs. max −6) + spínací zvlnění moduluje práh
      komparátorů → jitter značek. **Fix:** minimální zátěž + záporné LDO (např. TPS7A30xx)
      nebo důkladný LC filtr.
- [ ] **8. Časovací obvody přímo z buck měničů.** OCXO, MAX9601 VCC, Si5356A, LMK buffery bez
      post-regulace. **Fix:** LDO minimálně pro OCXO a analogový front-end.
- [ ] **9. GPSDO smyčka — strop hluboko pod 10⁻¹² ze zadání.** XOR type-1, RC 100k/100µ
      (τ≈10 s), bez sawtooth korekce, elektrolyt C65 jako integrátor (svod → drift EFC),
      skokové přepnutí lock↔holdover. Realisticky 10⁻¹⁰…10⁻¹¹. **Návrh:** disciplinovat
      softwarově (FPGA měří fázi gps_pps vs ref_clk, STM32 řídí EFC přes DAC/PWM); analogovou
      smyčku nechat jako fallback. Minimálně: C65 vyměnit za nízkosvodový (film).
- [ ] **10. J11 „GPS_CLK" SMA přímo na timepulse GPS modulu.** Externí 50Ω zátěž zhroutí
      referenci PLL uprostřed měření. **Fix:** buffer (volná brána 74LVC) mezi modul a SMA.
- [ ] **11. Ověřit GPS modul se DVĚMA timepulse výstupy.** J3.2 (clock pro PLL) + J3.4
      (GPS_Time_Stamp → FPGA pin 48). Běžné NEO breakouty mají jen jeden PPS (timing varianty
      NEO-M8T apod. mají dva).

## 🟡 Pinout a dokumentace (nutné před psaním HDL)

- [ ] **12. Sjednotit pinout: schéma vs CSV vs zadání §9.** Skutečnost dle netlistu:

  | Signál | CSV/zadání | Schéma (netlist) |
  |---|---|---|
  | phase_clk0–3 | 35, 25, 27, 29 | **35, 34, 40, 33** (PLL_CLK1–4) |
  | ref_clk 10 MHz | 63 | 63 ✓ |
  | ch_b | 41 | **25** |
  | div1, div2 | 42, 31 | **28, 27** (DIV_Bit1/2, DIV_Bit3/4) |
  | spi sck/mosi/miso/cs | 33/32/34/37 | **55/54/57/56** |
  | div_out / div_res | — | **51 / 53** |
  | gps_pps | 40 | **48** |
  | reset_n | 38 | **77** |

      `.cst` generovat ze schématu; aktualizovat CSV i §9 zadání.
- [ ] **13. PLL_CLK4 (pin 33) + PLL_CLK2 (pin 34) na jednom diff. páru IOB23A/B** — přeslech
      mezi fázemi = datově závislý jitter, DNL to nespraví. **Fix:** přemapovat na oddělené páry
      (ideálně zpět na „čisté" piny 25/27/29 dle logiky CSV a měřené vstupy jinam).
- [ ] **14. div1 + div2 na jednom páru IOB11A/B** — korelovaný přeslech mezi souběžně měřenými
      kanály. **Fix:** oddělit.
- [ ] **15. Fázové hodiny na pinech sdílených s LCD FPC konektorem modulu** (RGB_CK/VS/HS/DE
      = pahýly). Zvážit přemapování na header-only piny.
- [ ] **16. Odstranit dangling sheet piny PLL_CLK5–8** (pozůstatek 8fázové verze) a PLL2_CLK1/2
      na GPSDO listu; projet ERC dočista.
- [ ] **17. Vyřešit/smazat TODO poznámky ve schématu:** „Opravit CS", „Pinout check",
      „Kontrola C71/C6/odporu", „100p za 10p", „ne 33p ale 4p7".

## 🟢 Menší nálezy a zlepšení

- [ ] **18. Reset (FPGA pin 77) bez pull-upu** — plave během bootu STM32. Přidat 10k.
- [ ] **19. Volba odboček prescaleru:** osazeno Q0 (÷2) a Q3 (÷16); ÷2 může na LVCMOS33 přes
      header překročit vzorkovatelnost (>150–200 MHz). Zvolit R35/R37 (÷4, ÷32) dle rozsahu.
- [ ] **20. Prescaler natvrdo ÷256** (PE=+3V3, P0–7=GND) — programovatelnost EP016 nevyužita;
      buď využít P piny, nebo zvážit levnější pevný dělič.
- [ ] **21. Ochrana vstupů J1/J2:** DC vazba přímo do MAX9601 bez clampů. Přidat steering
      diody + sériový R/PTC, příp. přepínatelný atenuátor.
- [ ] **22. Ruční EFC (R73) referencovaný na +5 V z bucku** — vzít z přesnější reference
      (OCXO REF pin 2 je nezapojen — pokud referenci dává, použít ji).
- [ ] **23. Využít TMP117 pro teplotně spouštěnou rekalibraci DNL** (STM32 → `recal_dnl` při ΔT).
- [ ] **24. Přejmenovat síť „VBUS"** (nese 12 V z J5.19/20) na např. +12V_IN.
- [ ] **25. Ověřit warm-up proud OCXO na 5V větvi** (dimenzování F3/L4, úbytky).

## ✅ Ověřeno jako OK (bez akce)

- **MAX9601 latch enable: LE=GND, /LE=+3V3 = trvalý compare mód — správně.** Ověřeno vizuálně
  z Table 1 MAX9601 EV kitu (LE=0//LE=1 → compare; LE=1//LE=0 → latch). EV kit strapuje pro
  compare mód /LE na VCCO — stejná topologie jako ve schématu.
- PECL infrastruktura 3,3 V: Thevenin 130/82, 10R série, VBB bias koncept, terminace CLK/~CLK
  u U3, kanál B přes U2, Si5356 výstupy ob jeden (CLK0/2/4/6) s 33R sérií.
- TS5A3159 zapojení správně (1=NO smyčka, 3=NC ruční, 4=COM→OCXO_VC, 6=IN řízení).
- I2C adresy bez kolize (ADS1115 0x48, TMP117 0x49/0x4A, Si5356 0x70), pull-upy 4k7 jen
  na jednom místě, ADC_RDY pull-up.
- ADS1115 monitorovací děliče v rozsahu (OCXO_VC 0–5 V → max 3,31 V).
- Fázový plán 4×45° @100 MHz na Si5356A proveditelný (45° = 3 cykly VCO @2,4 GHz);
  validovaný register dump existuje.
- ref_clk 10 MHz → pin 63 (IOR5A/RPLL_T_in) dle zadání.

---

*Doporučené pořadí: 2→3 (deska neměří) → 4 (firmware) → 5–7 (spolehlivost) → 12–15
(pinout před HDL) → zbytek dle kapacity. (Bod 1 stažen — zapojení latch enable je správné.)*
