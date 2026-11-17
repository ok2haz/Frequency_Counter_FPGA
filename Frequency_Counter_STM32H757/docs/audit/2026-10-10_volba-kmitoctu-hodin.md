# Analýza — jaký kmitočet vzorkovacích hodin je pro tento čítač ideální (2026-10-10)

Vstup: dotaz uživatele „zvýšit 100 MHz na 125, případně až 200 MHz a víc; ideální kmitočet pro náš případ“.
Fáze auditu: **kód v repu se nemění**. P&R experimenty běžely na kopiích projektu ve scratchpadu.
Navazuje na STATUS #275 (zvýšení reference), `docs/TDC_MATEMATIKA.md`, `docs/audit/2026-10-09_tdc-3kanaly-rozmisteni.md`.

Legenda: **[M]** změřeno na desce / P&R, **[V]** výpočet z naměřeného, **[D]** dokument (zadání, UG286),
**[?]** neověřeno — datasheet GW1NR / Si5356 tu lokálně není, ověřit před rozhodnutím.

---

## 1. Co kmitočet hodin v tomto návrhu OPRAVDU určuje

Měřicí řetěz: carry-chain TDC (krok ALU) → vzorkování hodinami → hrubý čítač period + přesná časová značka
zavírací hrany okna (+ regrese vnitřních značek). Z toho plyne, **na čem kmitočet závisí a na čem ne**:

| Veličina | Závisí na kmitočtu hodin? | Poznámka |
|---|---|---|
| **Rozlišení jedné značky** (krok TDC ≈ 39,7 ps [M]) | **NE** | dáno křemíkem (zpoždění ALU), ne hodinami |
| **Šum jedné značky** (σ ≈ 105 ps [M], zdroj neznámý, O4) | **NE** (nejspíš) | kvantování jen 39,7/√12 = 11,5 ps; zbytek je jiný zdroj — dokud není znám, nelze tvrdit, že vyšší kmitočet pomůže |
| **Pokrytí řetězu** (hrana musí zůstat uvnitř) | **ANO** | řetěz musí pokrýt periodu: T / δ taktů |
| **Nejvyšší kmitočet vstupu na jedné hraně v řetězu** | **ANO** (úměrně) | v řetězu smí být jedna hrana; dnes ≈ 30 MHz při 100 MHz [D] |
| **Mrtvá doba jedné značky** (≈ 8 taktů) | **ANO** | 80 ns při 100 MHz |
| **Zavřít časování zbytku logiky** | **ANO, silně** | viz kap. 3 |
| **Jitter vzorkovacích hodin** | ANO (zdroj hodin, ne kmitočet) | viz kap. 5 |
| **Absolutní přesnost** (OCXO / reference) | **NE** | viz kap. 7 |

Hrubé rozlišení „bez TDC“ (kdyby TDC nebylo) je T_clk: 10 ns → 8 → 5 ns, tedy relativně 4·10⁻⁸ → 2·10⁻⁸ na 0,25 s okno.
S funkčním TDC (≈ 10⁻¹⁰) je to řádově horší než TDC, takže jde jen o záložní režim.

## 2. Pokrytí řetězu — nejsilnější argument PRO vyšší kmitočet

δ = 10 ns / 252 tapů = **39,7 ps** [M] (`maxtap` 248–252, FPGA ≈ 37 °C). Řetěz má v jednom řádku nejvýš **268 tapů** [M].

| f [MHz] | T [ns] | potřebné tapy | rezerva proti 268 |
|---|---|---|---|
| 100 | 10,0 | 252 | **+6 %** |
| 125 | 8,0 | 201 | +33 % |
| 150 | 6,67 | 168 | +60 % |
| 166,7 | 6,0 | 151 | +77 % |
| 200 | 5,0 | 126 | +113 % |

Dnešní rezerva 6 % je **menší než teplotní rozptyl zpoždění**, který zadání uvádí (20–30 % mezi 25 °C nominál a 85 °C
slow corner [D]). Chladný start (kratší δ) může řetěz zkrátit pod periodu → hrana za koncem → „obří bin“
(STATUS #270). Nepodloženo měřením při nízké teplotě **[?]**, ale riziko je strukturální. Rezerva ≥ 25–30 % je rozumný cíl
→ **f ≥ ~125 MHz**. Nad ~150 MHz se rezerva už nezužitkuje (řetěz lze zkrátit — ušetří plochu).

## 3. Uzavření časování — hlavní argument PROTI

Experiment: současný návrh beze změny, jen perioda `clk_p0_100m` v `timing.sdc` (nejistota 0,9 ns beze změny):

| f [MHz] | nejhorší rezerva [ns] | TNS [ns] | selhávajících bodů | sampler `q_r→qd_r` [ns] | CLS |
|---|---|---|---|---|---|
| 100 (dnes) | **+0,02** | 0 | 0 | OK | 80 % |
| 125 | −1,21 | −65 | 177 | +0,75 | 81 % |
| 150 | −2,55 | −755 | 1 279 | +0,21 | 80 % |
| 166,7 | −2,86 | −1 418 | 1 747 | −0,77 | 80 % |
| 200 | −4,02 | −3 900 | 2 555 | **−1,43** | 80 % |

[M] P&R Gowin 1.9.12, Slow 1,14 V 85 °C. Co selhává:

- **125 MHz:** pět tříd cest — odečet časové značky `tick_hi→add` (48 b), přičtení histogramu při kalibraci, povolení `x0/t0` v regresi,
  `hold_q→ref_ts`, `scan_k→cal_valid`. Všechny jdou řešit registrací / multicyklem. **Reálné.**
- **150 MHz:** přibývají aritmetika oken (`r_periods`, `dt_r`), řízení dělení v regresi, `pend_n`. Vyžaduje systematické zpipelinování
  celé `win_recip` + `regr_acc`. **Možné, ale už ne drobnost.**
- **≥ 167 MHz:** selhává už **vzorkovač TDC** (`q_r→qd_r`, kopie 268 vzorků s povolením `ld`) — samotný srdcový bod; dál to řeší jen
  dvě hodinové domény (rychlá: vzorkování + zmrazení + zachycení kódu; pomalá /2 nebo /4: vše ostatní) s přechodem přes handshake.
  Zadání samo uvádí f_MAX 203,6 MHz pro 25bitový čítač s jednou úrovní LUT při Slow 1,14 V 85 °C [D] → **200 MHz je strop
  pro cokoli, co má víc než jeden LUT mezi registry**; nejistota 0,9 ns by z 5 ns nechala 4,1 ns.

Zbytek návrhu dnes končí na ≈ 100–110 MHz bez ohledu na požadavek (Fmax 108–113 MHz ve všech čtyřech bězích) [M].

## 4. Vstup a mrtvá doba

Úměrně kmitočtu (zadání: jedna hrana v řetězu, vstup do ~30 MHz při 100 MHz [D]):

| f [MHz] | max. vstup na hraně v řetězu [V] | mrtvá doba značky (8 taktů) | max. frekvence značek |
|---|---|---|---|
| 100 | ~30 MHz | 80 ns | 12,5 M/s |
| 125 | ~37 MHz | 64 ns | 15,6 M/s |
| 150 | ~45 MHz | 53 ns | 18,8 M/s |
| 200 | ~60 MHz | 40 ns | 25 M/s |

⚠️ **Pro hlavní použití (10 MHz z GPSDO/OCXO) je to bez významu:** perioda 100 ns je delší než dnešní mrtvá doba 80 ns.
A počet vnitřních značek regrese **není limitován mrtvou dobou**: `regr` hlásí 312 497 značek na 1 223 oken = **≈ 255 značek na segment**
při 31 ms segmentu [M] (1 značka na ≈ 120 µs, ne na 100 ns). Úzké hrdlo je tedy jinde (trigger vnitřní hrany / `sg_pend`, **[?]** ověřit),
a vyšší kmitočet **σ regrese nezlepší**. Zlepšení by dalo jen odstranění toho hrdla.

Přímá cesta DC–200 MHz podle zadání [D] FPGA dnes **nezvládne** ani při 200 MHz vzorkování: čítání hran vzorkováním potřebuje
f_clk > 2·f_in. Pro vstupy nad ~50 MHz je nutný prescaler/IDES (viz kap. 6), takže vzorkovací kmitočet to NErozhoduje.

## 5. Zdroj hodin a jitter

- **Si5356 (dnes):** VCO = 10 MHz × N (celé N), výstup = VCO / celé dělení → nízký jitter. Plány s celými čísly pro všechny kandidáty:
  100 = 2,2G/22 · 125 = 2,5G/20 · 150 = 2,4G/16 · 160 = 2,4G/15 · 166,7 = 2,5G/15 · 200 = 2,4G/12.
  **[D] ověřeno 2026-10-14 v `datasheets/` (Si5356A rev 1.2, AN565 rev 0,5):** výstup CMOS 1–200 MHz, MultiSynth 5–200 MHz, zátěž ≤ 15 pF
  (hrana ≤ 2 ns), střída 45/50/55 %, chyba syntézy 0–1 ppb, CLKIN 5–200 MHz, fpfd 5–40 MHz. **Rozsah VCO v textu není** — vzorce AN565 pro
  zpětnovazební MultiSynth počítají s fVCO = 2,2 GHz (128 × 2200 = 281 600), jiné VCO **[?]** ověřit v ClockBuilder Pro (je nainstalován).
  Jitter Si5356 viz kap. 10.
- **Poměr k 10 MHz:** 100/150/160/200 → celé číslo (10/15/16/20 taktů na periodu 10 MHz). **125 → 12,5** (vzor se opakuje po dvou periodách).
  Nevadí (domény jsou stejně asynchronní + handshake), ale „10 taktů na periodu“ nesmí být nikde předpoklad.
- **Vnitřní PLL FPGA (zadání: 100 → 400 MHz VCO → 200 MHz) [D]:** funguje bez změny desky, ale **přidá jitter** vnitřního PLL oproti
  Si5356; hodnotu jitteru **[?]** (datasheet). U TDC s krokem 40 ps a nevysvětleným šumem 105 ps je to riziko — nasazovat jen po změření.
- 200 MHz LVCMOS33 po 33 Ω a krátké trase na modulu Tang Nano: integrita signálu a max. vstupní kmitočet GCLK pinu **[?]**.

## 6. Architektury pro vysoké vstupní kmitočty (jen přehled)

Hrubé rozlišení a čítání nad ~50 MHz vstupu se nedělá zvýšením hodin, ale:
1. **Prescaler v FPGA taktovaný vstupem** (lokální vedení, bez globální sítě — zadání zakazuje promoci CH na GCLK): ÷N před TDC
   (Adret režim v zadání). Nezávislé na vzorkovacím kmitočtu.
2. **IDES4/IDES8** (v čipu je; UG289 [D]): sériově‑paralelní převod při rychlém hodinovém FCLK z PLL, např. 800 MS/s → hrubé rozlišení 1,25 ns.
   Max. rychlost pro LVCMOS33 **[?]**. Lepší než 5 ns z 200 MHz, ale pořád horší než TDC (40 ps).

## 7. Důležitý kontext: co omezuje přesnost skutečně

Dnes (STATUS #284): kmitočet vůči externímu GPSDO se po restartu posunul o **−5 ppm** a po minutách se vrátil.
Takový posun je **~5 000× víc než rozlišení čítače** (10⁻¹⁰ při 0,25 s). Absolutní přesnost určuje reference (OCXO / externí zdroj),
ne kmitočet vzorkování. Ref_Ctrl (PIN41) a přepínač OCXO/externí reference (`U19`) firmware vůbec neřídí. Pro cíl „absolutní přesnost“
je **to** řádově významnější krok než změna taktu.

## 8. Doporučení

> ⚠️ **Přehodnoceno 2026-10-14 — viz kap. 10.** Pokus se 125 MHz (FW 0x0421/0x0422) selhal na desce (L-0154) a nebyl čistý bisect
> (měnil i dělič). Dnešní doporučení: zůstat na 100 MHz, případně nejdřív zkusit 110 MHz = 2,2 GHz / 20. Text níže je HISTORIE úvahy z 2026-10-10.

1. **Ideální kmitočet pro náš případ: 125 MHz (výchozí), případně 150 MHz.**
   - 125: rezerva řetězu 6 → 33 %, časování vyžaduje jen pět tříd úprav (reálné, bez dvou domén), Si5356 2,5G/20. Nevýhoda: poměr 12,5 k 10 MHz.
   - 150: rezerva 60 %, celé číslo k 10 MHz, řetěz lze zkrátit na ~200 tapů (úspora plochy ~5 p. b. CLS), ale vyžaduje zpipelinovat
     celou `win_recip`/`regr_acc` a časová rezerva bude mít nejistotu stejně těsnou jako dnes (L-0141).
2. **200 MHz a víc nedoporučuji** pro tento účel: vyžaduje dvě hodinové domény a přepsání jádra, nezlepší rozlišení (δ je dáno křemíkem),
   nezlepší regresi (255 značek/segment, hrdlo je jinde), přidává riziko jitteru a integrity signálu. Smysl by mělo jen spolu s architekturou
   pro vysoké vstupy (kap. 6).
3. **Před jakoukoli změnou kmitočtu změřit zdroj šumu 105 ps** (STATUS #272): stejný signál na CH_A i CH_B, rozdíl `dt_a − dt_b` =
   čistý šum TDC; a jitter výstupu Si5356 osciloskopem. Je‑li šum z hodin, kmitočet mění vše; není‑li, změna kmitočtu přesnost nezvýší.
4. **Jednotka v rámci:** při změně kmitočtu nést v rámci jednotku/kmitočet (CAPS), aby STM nevypisovalo o 25 % špatná čísla při nesouladu
   FPGA × firmware (viz kap. 9).
5. **Pořadí práce** (každý krok samostatně ověřený na desce): (a) vyřešit reference (Ref_Ctrl, kap. 7) a měření šumu (bod 3);
   (b) pak 125 MHz na větvi.

## 9. Co všechno se při změně kmitočtu musí přepočítat (kontrolní seznam)

- `timing.sdc` (období, multicyklické cesty počtem taktů — zachovat časy, ne počty), nejistota (zůstává ≥ 0,9 ns, přehodnotit).
- Jednotka času T_clk/16384: `ps = jednotky · T[ps] / 16384` (100: ·625/1024; 125: ·125/256; 150: ·625/1536 …); `gate_ns`; `FPGA_TICKS_PER_S`.
- STM: `fpga_freq.c/h` (`dt_units_to_ps`, `FPGA_TICK_PS`, `FPGA_TICKS_PER_S`), `meas_present.h` (`MP_TDC_PS`), emulátor `fpgasim`,
  web SPA (`tools/spa`, konstanty), Python `tools/*`, `docs/TDC_MATEMATIKA.md`.
- FPGA: přetečení 48bitové značky 2³⁴·T (172 s → 137 s při 125 MHz), `r_dt[47]` (86 → 69 s), časové prahy v taktech (`cal_tmo`, `boot_cnt`, `age`),
  testbenche (11 používá periodu).
- Si5356 `REGMAP` (z ClockBuilder Pro), popisky „4× 100 MHz“ v UI/dokumentaci.
- Kalibrace TDC: počet tapů/dekodér (TAPS násobek 32), `TDC_NTAP`, `OVF_LIM`, tabulky.

---

## 10. Dodatek 2026-10-14 — datasheet Si5356, přehodnocení po pokusu 125 MHz, rezerva řetězu

Fáze auditu: **kód se nemění**. Podklady: `datasheets/Si5356.pdf` (rev 1.2), `Si5356A_AN565.pdf` (rev 0,5), `docs/MERENI_A_KALIBRACE.md`, L-0152..L-0154,
`docs/audit/2026-10-10_si5356-generator.md`, `tdc.v`, `top.v`. Legenda jako výše, **[V]** = výpočet z [D]/[M] hodnot.

### 10.1 Jitter hodin [D] a co z něj plyne [V]

| Parametr (Si5356A, výstupy na stejném kmitočtu, fOUT > 5 MHz) | typ | max |
|---|---|---|
| Period jitter (10 000 taktů) | 50 ps pk-pk | 75 ps pk-pk |
| Cycle-cycle jitter (10 000 taktů) | 40 ps pk | 70 ps pk |
| Phase jitter (12 kHz–20 MHz) | 2 ps rms | — |
| Výstup–výstup skew | — | ±150 ps |
| Šířka pásma PLL | 1,6 MHz | — |

- 75 ps pk-pk za 10 000 taktů odpovídá ≈ 10 ps rms (předpoklad Gaussova rozdělení, pk-pk ≈ 7σ) **[V]**. To je < 1 % rozptylu šumu jedné značky
  (σ ≈ 105 ps, O4). Hypotéza „šum značky je jitter Si5356“ proto **není podložena**; zdroj O4 je jinde.
- Datasheet **nerozlišuje celé a zlomkové dělení** MultiSynthu, hodnoty platí „pro libovolnou kombinaci výstupů“. Pro zlomkové dělení tedy
  záruka platí jen v rámci toho tvrzení; deterministická periodická složka zlomkového děliče by se v pk-pk za 10 000 taktů měla projevit, ale to je **[?]**.
- Střída 45/50/55 % (±0,4 ns při 8 ns) nevadí při vzorkování jen náběžnou hranou (`DUAL = 0`); u `DUAL = 1` (sestupná hrana) by byla další překážka.

### 10.2 Pokus se 125 MHz NEBYL čistý bisect [D]

`2026-10-10_si5356-generator.md` (ř. 54) a STATUS #292 uvádějí: 125 MHz se dělalo na **VCO 2,2 GHz zlomkovým dělením 17 3/5** (VCO je společné
s generátorem CLK2–7; MS A reg 53/54/55/59). Návrat na 100 MHz (FW 0x0423) vrátil **MS A = 22 (celé)**. Přechod 100 → 125 MHz tedy změnil najednou:

| | 100 MHz (funguje) | 125 MHz (T/2, T chyby) | 110 MHz (návrh) |
|---|---|---|---|
| kmitočet | 100 | 125 | 110 |
| MultiSynth A | ÷22 celé | ÷17 3/5 **zlomek** | ÷20 celé |
| VCO | 2,2 GHz | 2,2 GHz | 2,2 GHz |
| takty na periodu 10 MHz | 10 | **12,5** | 11 |

L-0154 vede změnu jako „jen hodiny“ a příčinu jako neurčenou (STA a simulace prošly). Mezi kandidáty je proto stále i **zlomkové dělení** a
**poloviční poměr 12,5**, nejen časování na křemíku. ⚠️ Dřívější doporučení v kap. 8 („125 = Si5356 2,5G/20“) se **neprovedlo**; zbytek kap. 8 nebrat jako plán.

### 10.3 Rezerva řetězu a důsledek přetečení [V]

- Rezerva při 100 MHz: A 252 z 268 tapů (6 %), **B po studeném startu 258 (3,7 %)**, po rekalibraci za tepla B 250. Citlivost na teplotu **neznáme** (A se neměnilo).
- Rezerva = 268 / potřebné tapy − 1, nejhorší případ B za studena (δ_B = 10 ns / 258 = 38,8 ps):

| f [MHz] | potřebné tapy | rezerva |
|---|---|---|
| 100 | 258 | +4 % |
| 110 | 235 | +14 % |
| 120 | 215 | +25 % |
| 125 | 206 | +30 % |

- **Přetečení je pozvolné, ne skokové.** Tabulka dává každému kódu čas středu binu (`tdc.v`: `V[a] = 16384·(cum_pred + hist[a]/2)/N`), takže hrany za koncem
  řetězu (poslední bin, `da_ovf`) dostanou čas středu chybějícího úseku *s*; chyba je omezená na ±*s*/2. Při rovnoměrné fázi hran:

| chybí *s* | podíl hran za koncem | rms přes všechny hrany |
|---|---|---|
| 5 tapů (0,2 ns) | 2 % | 8 ps |
| 13 tapů (0,5 ns) | 5 % | 32 ps (+4 % k 105 ps) |
| 26 tapů (1,0 ns) | 10 % | 91 ps (+32 %) |
| 52 tapů (2,0 ns) | 20 % | 258 ps |

- Příznak `SHORT` (`tdc_status` bit4/5) se nastaví při > 1/16 událostí kalibrace za koncem (`top.v:383`), tj. při ≈ 0,6 ns (rms ≈ 45 ps). Je to příznak po zhoršení;
  včasné varování (zbývající tapy, podíl `ovf`) se nikde nevyhodnocuje. Z kódu STM jsem našel jen zobrazení „SHORT“ (`fpga_freq.c`), ne vyřazení oken.
- Pravděpodobnější cesta k vyčerpání rezervy než chladná místnost je **jiná deska s rychlejším křemíkem** (neměřeno).

### 10.4 Doporučený postup (každý krok samostatně ověřit)

1. **STM, bez zásahu do FPGA ani měřicí cesty:** do `status`/errlogu/`/api/fpga` zobrazit rezervu (tapy, podíl `ovf`) s prahy.
2. **Simulace** (`sim/tb_tdc_inl.sv`, silikonový model): zkrátit zpoždění tapu o 10 a 20 % a ověřit model z 10.3.
3. **Chladový test:** studený start v chladnější místnosti (případně lokální chlazení modulu, pozor na kondenzaci), `tdc cal`, odečet `last`/`ovf`.
4. **Jen když krok 3 ukáže, že rezerva mizí:** 110 MHz = **2,2 GHz / 20** (stejné VCO, celé dělení, poměr 11; Si5356 jen MS A: `reg 54` `0x09 → 0x08`,
   P1 = 128·20 − 512 = 2048 — ověřit v CBPro). FPGA: `CLK_MHZ = 110`, `timing.sdc` 9,09 ns, hlídač hodin 704 přechodů (6,4·`CLK_MHZ`), `gate_div` (od 0x0423 jen 100 MHz),
   kontrolní seznam kap. 9. STM `dt_units_to_ps` bere kmitočet z rámce. Přijetí podle L-0154: podíl oken v ±0,5 ns > 95 %, ADEV(0,25 s) ≈ 1·10⁻¹⁰, `regr` použito > 0.
   Pokud 110 MHz projde, vyloučí to kmitočet i zlomkové dělení jako příčinu z L-0154 (zbude poměr 12,5 a časování).
5. **120 MHz** (2,4 GHz / 20 celé, nebo zlomek 18 1/3) až po ověření rozsahu VCO v CBPro; změna VCO přeplánuje i generátor CLK2–7.
6. **Vyhnout se:** `DUAL = 1` (CLS 87 %, Fmax 80–94 MHz, střída 45–55 %), návrat 320 tapů (F-0228), změna `OVF_LIM` bez nového bitstreamu a pravidelná rekalibrace za chodu
   (díra v řadě oken → zahodí rozpracovaný vzorek statistiky).

### 10.5 Neověřeno

Rozsah VCO Si5356, datasheet GW1NR (max. kmitočet GCLK, jitter vnitřního PLL), teplotní citlivost zpoždění tapu, chování `SHORT` na STM, model 10.3 na desce.

### 10.6 Kroky 1 a 2 provedeny 2026-10-14

**Krok 1 (STM, `ef07ca0` + oprava `a30b0b4`).** CAL report se čte jednou po každé dokončené kalibraci; `status` → `TDC REZERVA`, UART `tdc`, errlog druh `ERRLOG_K_TDC`.
Úrovně: **OK** ≥ 15 tapů, **POZOR** 8–14, **KRITICKÉ** < 8 tapů nebo aspoň jedna událost za koncem (FPGA příznak `SHORT` zůstává až od 1/16). Nemění měření, FPGA ani IPC.
Release CM7 `.text` 653 856 → 655 768 B, 0 varování, `audit.py` 92 OK / 0 selhání / 2 s varováním. ⬜ Neověřeno na desce (řádek `TDC REZERVA` v `status` po flashi).

**Krok 2 (simulace, Icarus, `sim/tb_tdc_inl.sv`, parametry jako `tdcinl268`, jen `SIM_TAP_MEAN`/`SIM_TAP_SPREAD` škálované společně, `NEDGES=2000`, `CAL_LOG2_TB=14`, bez `EXPECT_*`).**
Jeden běh = jeden náhodný průběh zpoždění tapů; kalibrace z 16 384 událostí (na desce 2^20).

| tap [ps] | řetěz [ns] | `last` | `maxtap` | za koncem | největší bin | σ značky [ps] |
|---|---|---|---|---|---|---|
| 39 | 10,45 | 254 (rezerva 14) | 248 | 0 | 63 | **18,5** |
| 38 | 10,18 | 261 (rezerva 7) | 256 | 0 | 67 | **18,3** |
| 37 | 9,92 | 511 | 264 | 3,87 % | 387 | 28,4 |
| 36 | 9,65 | 511 | 264 | 6,53 % | 653 | 51,6 |
| 35 | 9,38 | 511 | 264 | 9,22 % | 922 | 83,1 |
| 34 | 9,11 | 511 | 264 | 11,93 % | 1193 | 119,4 |
| 33 | 8,84 | 511 | 264 | 14,62 % | 1462 | 161,1 |
| 31 | 8,31 | 511 | 264 | 17,80 % | 1780 | 218,3 |
| 29 | 7,77 | 511 | 264 | 22,28 % | 2228 | 302,0 |

(`last` = 511 znamená, že nejvyšší obsazený kód je „za koncem“.)

1. **Model z 10.3 platí.** Předpověď σ² = σ₀² + (p^1,5·T/√12)², kde p je naměřený podíl hran za koncem a σ₀ = 18,5 ps, vychází 28,7 / 51,6 / 82,9 / 120 / 162 / 304 ps proti naměřeným 28,4 / 51,6 / 83,1 / 119,4 / 161,1 / 302,0 ps
   (37 / 36 / 35 / 34 / 33 / 29 ps), tedy shoda do ~1 %. Poslední bin má šířku = chybějící úsek a kalibrace mu přiřadí střed, jak se předpokládalo.
2. **Užitečný řetěz je asi 264 tapů, ne 268.** Ve všech sedmi bězích s přetečením se `maxtap` zastaví na 264. Rezerva `taps − last` proto nadhodnocuje skutečnou rezervu o ~4 tapy.
3. **Přetečení začíná mezi 38 a 37 ps.** Mezi 39 a 38 ps (−2,6 % rychlosti tapu) se `last` posune o 7 tapů (≈ 2,7 tapu na 1 % rychlosti). Z toho **[V, hrubé]**: rezerva 14 tapů ≈ 4–5 % rychlosti, 7 tapů ≈ 1,5–2,5 %.
   Pro desku: **B po studeném startu (rezerva 10) má v rychlosti tapu jen ~2 % do přetečení, A (rezerva 16) ~4 %.** To je méně, než naznačovalo „3,7 % / 6 %“ v tapech.
4. **Cena přetečení je mírná, dokud je malé.** Podíl hran za koncem 6,25 % (práh FPGA `SHORT`) odpovídá přidanému rms ≈ 45 ps; proti šumu značky na desce ~105 ps je to +9 %. Skutečně špatné je až 10–20 % (σ 83–300 ps).
5. **Prahy z kroku 1 jsou v modelu vhodně nastavené.** Rezerva 7 (38 ps, σ ještě 18,3 ps) = KRITICKÉ je 3–4 tapy před přetečením; 14 (39 ps) = POZOR. Původní zařazení 0 < ovf < 1/16 jako POZOR bylo nemonotónní (mírnější než rezerva 7) a opraveno `a30b0b4` (L-0155).

**Omezení modelu:** zpoždění tapů rovnoměrně ±38 % bez struktury INL, jeden náhodný průběh na bod, 2000 hran, kalibrace z 16 384 událostí, žádná teplotní závislost. Nic z toho neříká, co dělá křemík při chladu (krok 3), ani nic o chybě T/2 u 125 MHz (L-0154).
Reprodukce: `sim/run.ps1` definice `tdcinl268` s jinými `SIM_TAP_MEAN`/`SIM_TAP_SPREAD`, `-DNEDGES=2000` a bez `EXPECT_*`; `PATH` musí obsahovat `C:\iverilog\bin`; osm běhů paralelně ~10 min.


### 10.7 P&R test 110 MHz (2026-10-15) [V]

**Otázka (riziko z #297):** uzavře se časování návrhu při 110 MHz (perioda 9,09 ns), když audit uváděl Fmax 108–113 MHz a CLS je na 80 %?
**Postup:** kopie `src/` FPGA modulu mimo repo, změněno **jen** `top.v` `CLK_MHZ = 110` a `timing.sdc` perioda `clk_p0_100m` 9,0909 ns (nejistota hodin 0,9 ns a vše ostatní beze změny), `gw_sh build.tcl` (Gowin 1.9.12, `place_option 2`). Referenční kopie na 100 MHz + 7 sestavení na 110 MHz, každé s jiným `BLD_TIME` (= jiné rozmístění). Nic se nenahrávalo na desku, repozitář se nezměnil. Data: `docs/audit/data/2026-10-15_pnr_110MHz.txt`.

| sestavení | Fmax `clk_p0_100m` | TNS setup | nejhorší cesta (slack, včetně nejistoty 0,9 ns) | CLS |
|---|---|---|---|---|
| 100 MHz (reference) | 115,3 MHz | 0 | +1,327 ns | 84 % |
| 110 MHz, 1. | 105,2 MHz | **−0,415 ns (1 koncový bod)** | −0,415 ns (`u_tdca/arm_r_s1` → `u_wra/trig_a_s0`) | 83 % |
| 110 MHz, s1…s6 | 110,1 / 111,6 / 114,1 / 116,5 / 115,5 / 113,9 MHz | 0 (všech 6) | +0,008 / +0,131 / +0,324 / +0,510 / +0,435 / +0,315 ns | 83–85 % |

**Závěry:**
1. **Časování se na 110 MHz uzavře, ale s malou rezervou a ne vždy:** 6 ze 7 sestavení prošlo, 1 neprošlo (jediná cesta, −0,415 ns). Nejhorší slack prošlých sestavení je +0,008 až +0,51 ns, na 100 MHz +1,33 ns. Rezerva se tedy zmenšuje o ~1 ns, víc než samotná změna periody (0,91 ns), protože P&R má méně prostoru.
2. **Slack je počítán včetně nejistoty 0,9 ns**, takže skutečná rezerva je o 0,9 ns větší. Kritéria z L-0141 (měřilo špatně při skutečné rezervě 0,545 ns a správně při ≥ 0,64 ns) tedy splňují všechna prošlá sestavení; neprošlé sestavení (skutečně 0,485 ns) je pod mezí.
3. **Vydání 110 MHz by vyžadovalo víc sestavení a výběr** (`tools/fpga_release.py` brána + `fpga_load_test.py`), nebo úpravu RTL u cesty `arm_r` → `trig_a` (8,2 ns zpoždění dat při 2 logických úrovních = špatné rozmístění, ne hloubka logiky).
4. **CLS se nezměnilo** (83–85 %, na 100 MHz 84 %). Nové logiky se nepřidává, takže 110 MHz tuto rezervu nezhorší.
5. **Další věci, které by 110 MHz vyžadovalo mimo časování** [D, ze zdrojů]: `gate_div` v `spi_app.v` převádí na ns pevně `dt·5 >> 13` (platí jen pro 100 MHz; ns v rámci je informativní, FW počítá z `dt` a `clk_mhz`); Si5356 reg 54 `0x09 → 0x08` (2,2 GHz / 20); hlídač hodin a `CM_EXP` se z `CLK_MHZ` počítají samy; FW parser už `clk_mhz` z rámce bere (abs 99, CAPS bit12).
6. **Co test NEŘÍKÁ:** nezkoumal chybu časové značky podle fáze hrany (L-0152/L-0154 – to bylo na 125 MHz, 110 MHz je jiný poměr k 10 MHz referenci), jitter při jiném děliči (10.1) ani chování TDC na křemíku. Přínos pro rezervu řetězu je odhad: při stejné rychlosti tapu a periodě 9,09 ns by řetěz potřeboval ~91 % tapů proti dnešku, tj. B po studeném startu (last 265) by měl rezervu řádově 25 tapů místo 3 **[HYPOTÉZA – ověřit sestavením a `tdc` na desce]**.
