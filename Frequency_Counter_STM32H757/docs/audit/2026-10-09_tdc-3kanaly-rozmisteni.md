# Audit — tři TDC (A, B, GPS 1PPS): fyzické rozmístění, zdroje, dosažitelná přesnost (2026-10-09)

Vstup: `TDC_FPGA_GPSDO_ZADANI_PRO_CLAUDE_REVIZE_2026-10-09(1).md` (RFC k revizi), HEAD `d526c00`,
produkční bitstream FW 0x041A (`Frequency_Counter_FPGA_Module/ab_test/FW_0x041A_regr_unc.fs`, md5 `85790f3a…`).
Fáze auditu: **kód v repu se nemění** (pravidlo 4). Všechny P&R experimenty běžely na kopiích projektu
ve scratchpadu, ne v `src/`.

Nástroje: Gowin V1.9.12 (`gw_sh`), Apicula 0.34 (`gowin_unpack`, rozbalení bitstreamu),
skripty `chains.py`, `tdcmap.py`, `qrmap.py`, `mkvar.py` (scratchpad sezení).

Legenda: **[P&R]** výstup Gowin, **[BIT]** rozbalený bitstream, **[HW]** změřeno na desce,
**[DOKUMENT]** datasheet, **[VÝPOČET]**, **[HYPOTÉZA]**.

---

## 1. Kde fyzicky leží řetězy dnes (FW 0x041A)

Metoda: (a) do kopie `timing.sdc` přidán `report_timing -from [get_regs {u_tdc?/u_chain/q_r*}]`
— každý vzorkovací FF `q_r[k]` sedí ve slotu svého ALU (výstup `SUM` → `D` uvnitř CLS), takže poloha
FF = poloha ALU tapu k; (b) produkční bitstream rozbalen Apiculou a porovnán seznam buněk s přestavbou.

| kanál | tapy 0..267 | tapy 268..319 | hlava řetězu |
|---|---|---|---|
| A (`u_tdca`) | **R26 C2[1] … C46[4]**, souvisle | **R20 C38[0] … C46[3]** | R26 C2 slot 0 |
| B (`u_tdcb`) | **R27 C2[1] … C46[4]**, souvisle | **R5 C38[0] … C46[3]** | R27 C2 slot 0 |

[P&R] + [BIT]: obsazení buněk produkčního bitstreamu a přestavby je **shodné** (diff 0 řádků
seznamu instancí), plné řádky R26 a R27 (270 ALU, 268 FF `DFFSE`) v obou.

**F-0228 — 52 tapů na kanál leží v jiném, vzdáleném řádku a nikdy se nepoužijí.**
Řádek GW1NR-9 má 45 dlaždic CFU (C2..C46) × 6 ALU = **270 ALU**; carry vede jen vodorovně v řádku.
Nástroj si vezme slot 0 první dlaždice jako hlavu a slot 5 poslední jako výstup, takže
**do jednoho řádku se vejde nejvýš 268 vzorkovaných tapů**. Řetěz 320 musel přeskočit
(A: R26 → R20, 6 řádků; B: R27 → R5, **22 řádků**) obecným vedením. Hrana se za 10 ns dostane
na tap 248–252 [HW, `tdc` maxtap 248], takže zlom na tapu 268 se dnes neprojeví — ale
52 ALU + 52 FF `q_r` + 52 FF `qd_r` + část dekodéru na kanál je **plocha bez užitku**
a kdyby se hrana někdy dostala za tap 267 (chladno, jiný kus), vznikne v tom místě obří bin.

**F-0229 — symetrie A/B je dnes náhodná, ne vynucená.** Bez omezení dal placer v jiném běhu
(experiment v1 níže) **B do řádku R9** (19 řádků od vstupního pinu), A do R27. Rozmístění
tedy závisí na zbytku návrhu; constraints na řetězy neexistují.

**F-0230 — naivní `GROUP` s FF řetěz rozbije.** `GROUP {"…/g*" "…/q_r*"}; GRP_LOC R26C[2:46]`
(experiment v2) splnil řádek, ale **FF `q_r` rozházel mimo slot jejich ALU** (q_r[1] v C20,
q_r[2] v C31…). Každý tap by pak šel do FF obecným vedením s jiným zpožděním → nemonotónní
kód. Fungují dvě jiné formy (experimenty v5, v6):
- `GROUP g = {"u_tdca/u_chain/g*"}; GRP_LOC g R26C[2:46];` (jen ALU, FF dorovná placer), nebo
- `INS_LOC "u_tdca/u_chain/q_r_<k>_s1" R26C<c>[<cls>][<A|B>];` pro každý tap (tap k = lineární
  pozice 13+k od C2 slotu 0).
Obě daly A/B/PPS v řádcích R26/R27/R25, **tapy 0..255 souvisle C2[1] … C44[4], ve všech třech
řádcích stejné sloupce**. Doporučení: ALU skupina + kontrolní skript (viz kap. 5), který build
odmítne, když není každý `q_r[k]` ve slotu svého ALU v jednom řádku.

Vstupy: CH_A = IOB8[A] (pin 25), CH_B = IOB11[A] (pin 27), GPS = **IOB23[A] (pin 33, banka 2)** [P&R].
Hlavy řetězů jsou na C2, takže vstupní vedení pin → hlava je pro každý kanál jinak dlouhé
(STA dřív 6,23 / 6,14 ns). Je to **konstantní posun kanálů**, ve kmitočtu se krátí, pro TI A–B
se musí kalibrovat (T06/T07). Posunout hlavu k pinu nejde, řetěz by se do řádku nevešel.

---

## 2. Kolik tapů

| volba | ALU/kanál | rezerva nad 10 ns (při 39,7 ps/ALU, maxtap 248–252) | poznámka |
|---|---|---|---|
| 320 (dnes) | 322 | velká, ale 52 tapů v jiném řádku | F-0228 |
| **268** (celý řádek) | 270 | 16–20 tapů ≈ **6–8 %** (0,6–0,8 ns) | dekodér potřebuje násobek 32 → doplnit na 288 jedničkami (= „už prošla“) |
| 256 | 258 | 4–8 tapů ≈ 1,6–3 % | dekodér beze změny; **v chladu riziko** krátkého řetězu |

Kritérium: hrana musí zůstat v řetězu i při nejrychlejším křemíku (nízká teplota). Teplotní
koeficient zpoždění carry tohoto čipu **není změřený** [HYPOTÉZA: řádově 0,1 %/°C].
Doporučení: **268 (celý řádek)**; 256 jen pokud by plocha rozhodovala a měření `tdc` v chladu
(např. 0 °C) ukázalo maxtap ≤ 248.

---

## 3. Zdroje a časování — experimenty P&R (kopie projektu, `set_clock_uncertainty 0.5`)

| var. | obsah | CLS | Fmax clk_p0 | výsledek |
|---|---|---|---|---|
| 0x041A | A+B 320 tapů, regrese jen A | 78 % | 101,4 MHz | dnešní stav |
| **v7** | A+B **256** tapů v R26/R27, regrese jen A | **74 %** | 100,25 MHz | **projde** (rezerva jen 0,025 ns) |
| v9 | v7 + regrese i pro B | 85 % | 92,3 MHz | **neprojde** (TNS −8,5 ns) |
| v8 | v7 + třetí plný TDC (PPS) v R25 | 86 % | 71,3 MHz | **neprojde** (TNS −56 ns) |
| v1/v5/v6 | v7 + regrese B + třetí TDC | 89–92 % | 62–68 MHz | **neprojde** (TNS −25 až −99 ns) |

V experimentech jsou výstupy regrese B a PPS drženy naživu XOR propadem do `led_tx` (přenos do SPI
rámce by stál podobně nebo víc). Selhávají hlavně cesty **uvnitř regresního bloku** (`dsub → ym_a/ym_b`,
`ref_ts → n2154`, `sa_s → u_wrb/rg/t0`) — tedy dělič a vstup akumulátoru, které měly těsnou rezervu
už v 0x041A.

**F-0231 — tři plné TDC + dva regresní bloky se v GW1NR-9C v dnešní podobě nevejdou (NO-GO
pro „kopírovat 3×“).** Cena: třetí plný TDC ≈ +12 p. b. CLS, regrese B ≈ +11 p. b.; nad ~85 % CLS
přestane procházet časování 100 MHz. Výsledek P&R, ne odhad.

Co by místo uvolnilo (seřazeno podle poměru přínos/riziko; **[HYPOTÉZA] dokud neprojde P&R**):
1. **268/256 tapů v jednom řádku** — změřeno v7: −4 p. b.
2. **Regrese bez děliče ve FPGA**: FPGA pošle za segment jen `n`, `Σ(x−x_ref)`, `Σ(t−t_ref)`
   (≈ 17 B), STM vydělí v `double`. Odpadne 68bitový sekvenční dělič, `nq/xq/rem/dsub`, výstupní
   `xm/ym` (~380 FF + logika na kanál) a hlavně nejhorší časové cesty. Odpovídá i zásadě RFC §7
   („FPGA dodá primární data, STM počítá“). Vyžaduje rámec v3 (> 128 B).
3. **PPS bez plného carry řetězu** (kap. 4.2) — místo +12 p. b. odhadem 1–2 p. b.

---

## 4. Dosažitelná přesnost

### 4.1 Kanály A a B (stejná konstrukce, čísla z 10 MHz na CH_A)

| veličina | hodnota | důkaz |
|---|---|---|
| průměrný krok řetězu (1 ALU) | ≈ 39,7 ps | [HW] 10 ns / 252 tapů |
| obsazených kódů | 154 / 165 (A / B) → průměrná šířka obsazeného binu ≈ 61–65 ps | [HW] |
| nejširší bin | 198 / 157 ps | [HW] (dříve 311 ps — nestabilita kalibrace, O-bod otevřen) |
| kvantizační šum značky σ_q | 30 / 27 ps (dolní mez) | [HW] histogram |
| **celkový šum jedné značky** | **≈ 105 ps** (80–160 podle estimátoru), zdroj neznámý (O4) | [HW] |
| single-shot časový interval A→B | ≈ √2·105 ≈ **150 ps** (při nezávislých kanálech) + konstantní posun ke kalibraci | [VÝPOČET], **neměřeno** |
| kmitočet, 2 body, okno G | σ_y = √2·105 ps / G → **5,9·10⁻¹⁰ (0,25 s)**, 1,5·10⁻¹⁰ (1 s) | [VÝPOČET] |
| ADEV bez regrese (bílý PM) | √3·105 ps/τ → 7,3·10⁻¹⁰ (0,25 s), 1,8·10⁻¹⁰ (1 s), 1,8·10⁻¹² (100 s) | [VÝPOČET] |
| kmitočet, regrese, 0,25 s | **≤ 5,4–6,5·10⁻¹¹ naměřeno** (horní mez, obsahuje šum obou zdrojů); model 1,4·10⁻¹² | [HW] / [VÝPOČET] |

Kdyby se šum značky srazil na kvantizaci (~30 ps; vyžaduje najít zdroj 105 ps, O4), všechny řádky
odvozené z 105 ps se zlepší ≈ 3,5×. **Cíl „rozlišení pod 100 ps“** splňuje krok (40 ps) i kvantizace
(30 ps), **ne** změřený šum jedné značky (105 ps).

### 4.2 GPS 1PPS

| zdroj chyby | hodnota | důkaz |
|---|---|---|
| **PPS přijímače NEO-7M (GPS)** | **30 ns RMS, 60 ns 99 %** (GLONASS: 50 / 100 ns) | [DOKUMENT] NEO-7 datasheet UBX-13003830, tab. výkonu |
| plný carry TDC | ≈ 0,1 ns | [HW] jako A/B |
| DDR/IDES vzorkování (2,5 ns krok) | σ_q ≈ 0,72 ns | [VÝPOČET] |
| jen hrubý tik 10 ns | σ_q ≈ 2,9 ns | [VÝPOČET] |

**F-0232 — plný carry TDC pro PPS z NEO-7M metrologicky nic nepřinese.** Chyba časové značky se
sčítá kvadraticky s 30 ns jitteru PPS: plný TDC → 30,0002 ns, hrubý tik 10 ns → 30,14 ns (+0,5 %).
Rozlišení FPGA tu není limit; limitem je přijímač. Referenční ADEV z PPS ≈ √3·30 ns/τ = 5,2·10⁻⁸/τ
→ 5·10⁻¹¹ při 1000 s, 5·10⁻¹² při 10⁴ s [VÝPOČET]. Servo OCXO proto musí mít časovou konstantu
řádově 10³–10⁴ s a krátkodobou stabilitu dává OCXO, ne GPS.
Korekce `qErr` (UBX-TIM-TP) může pilovitou složku zmenšit — **[HYPOTÉZA]: že ji NEO-7M podává
a jak velký je zbytek, ověřit v u-blox 7 Receiver Description + měřením.** Plný TDC by dával smysl
až s časovacím přijímačem (např. řady M8T/F9T, jednotky ns), a i tam stačí 0,7 ns z IDES.

**F-0233 — RFC §6.1 „kalibrace GPS by trvala 12 dní“ neplatí pro tento návrh.** Code-density
kalibrace nebere události ze signálu, ale z **ring oscilátoru** (`sig_eff = s_col ? ro : sig_raw`,
`tdc.v`); 2²⁰ událostí trvá 0,1–0,5 s. Pro PPS stačí kalibraci spouštět hned po PPS hraně a
značku, která by na kalibraci narazila, označit jako ztracenou (`pps_status`).

### 4.3 Řízení OCXO (AD5693R)

**F-0234 [HYPOTÉZA] — krok DAC může být hrubší než cílová stabilita.** AD5693R má 16 b
(×2: 76,3 µV/LSB na 0–5 V). Relativní krok kmitočtu = (rozsah EFC v ppm) / 65 536: např. ±0,5 ppm
na 5 V → **1,5·10⁻¹¹ na LSB**, tedy víc než stabilita dobrého OCXO (~10⁻¹²). Řešení: zúžit EFC
odporovým děličem, dithering nejnižšího bitu, nebo jiný DAC. Rozhodne až EFC konkrétního OCXO.

---

## 5. Kontrola rozmístění (návrh nástroje, RFC §5.2)

`qrmap.py` (scratchpad) čte textový timing report s `report_timing -from q_r*` a pro každý kanál
vypíše souvislé úseky tapů (řádek, sloupec, slot) a každé přerušení. Pro zařazení do buildu:
přesunout do `Frequency_Counter_FPGA_Module/sim/check_tdc_placement.py`, `report_timing` řádky
přidat do `timing.sdc`, `build.tcl` doplnit o `set_option -gen_text_timing_rpt 1` a skript nechat
**selhat**, když (a) některý kanál není v jednom řádku, (b) `q_r[k]` není ve slotu k+1 od hlavy,
(c) kanály nemají shodné sloupce. Nezávislé ověření z bitstreamu: Apicula (`gowin_unpack`) —
plné řádky ALU + `DFFSE`.

---

## 6. Co z toho plyne pro architekturu (návrh, ke schválení)

1. Řetězy A, B (a případně PPS) v sousedních řádcích **R26, R27 (R25)**, stejné sloupce C2..C46,
   **268 tapů**, constraint na ALU skupinu + kontrolní skript.
2. Regrese symetricky pro A i B, ale **bez děliče** (součty → STM); surové `N` a `Δt` zůstávají
   (B5/O1 RFC — `gate_ps` se nepřepisuje).
3. PPS: podle přijímače — s NEO-7M levný capture (hrubý tik + DDR/IDES), plný třetí řetěz jen
   s časovacím přijímačem a jen pokud po krocích 1–2 zbude plocha (P&R rozhodne).
4. Rámec v3 (> 128 B) s poli z RFC §7.

⬜ Nic z kap. 6 není implementováno ani ověřeno na HW.

---

## 7. Doplnění po rozhodnutí zadavatele (2026-10-09, odpoledne)

**Rozhodnuto:** řetěz A i B v jednom řádku (hotovo jako FW 0x041D); PPS levným vzorkováním (STATUS #276); zvýšení hodin na
150–200 MHz jako TODO (#275); krok DAC RC filtrem + dither (#277). Přijímač GPS zůstává NEO-7M, PPS je na pinu 33 (IOB23[A]).

**Provedeno:** `TDC_NTAP = 268`, dekodér 288, `GROUP`/`GRP_LOC` jen pro ALU (A R26, B R27), `sim/check_tdc_placement.py`,
simulace všech 16 testů PASS (nový `tdcinl268`, `decequiv288`), P&R: CLS 75 %, Fmax 100,86 MHz, TNS 0, rozmístění PASS.
Odmítnuté varianty: `GROUP` i s FF (FF rozhozena mimo slot ALU, F-0230); `INS_LOC` pro každý FF funguje, ale je zbytečně
dlouhý.

**F-0235 — nejistota hodin 0,5 ns nestačí (L-0141).** Stejná logika jako 0x041A, jiné rozmístění, rezerva 0,545 ns:
na desce měřil špatně (1 578 628 Hz) v 6 z 7 nahrání, jednou správně. Sestavení s nejistotou 0,9 ns (rezerva ≥ 0,98 ns)
měřilo správně 7× ze 7 a ADEV(0,25 s) vyšla 7,2·10⁻¹¹. Příčina není uzavřená (hypotéza: skutečný jitter/skew hodin
chybí v modelu). Pozor: tím se rezerva 100 MHz zmenšila — při vyšší referenci (#275) bude potřeba počítat s ~1 ns
nejistoty a tomu přizpůsobit celou logiku, ne jen TDC.

**F-0236 — dither DAC: RC filtr před bufferem je nutná podmínka.** Simulace (1. řád sigma-delta, RC 47 ms + 1 ms; strmost
OCXO 2,5–5,5 Hz/V): bez RC na vstupu bufferu zvlnění = celý LSB (76 µV pp, dither je k ničemu); s RC 47k/1µ a zápisem 100 Hz
7–14 µV pp (1,8–7,7·10⁻¹² pp), 200 Hz 3–7 µV, 400 Hz 1–3 µV. Pro malé zlomky vznikají limitní cykly. Doporučení v STATUS #277.
Skript `dac_dither.py` je ve scratchpadu sezení (není v repu).

Nic z toho není ověřeno na HW s osazeným DAC: čip AD5693R není osazen (⬜).
