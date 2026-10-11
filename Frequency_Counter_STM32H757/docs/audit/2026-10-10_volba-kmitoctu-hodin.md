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
  Maximální CMOS výstup Si5356 a rozsah VCO **[?]** (z paměti 200 MHz CMOS / VCO 2,2–2,84 GHz; ověřit v ClockBuilder Pro, je nainstalován).
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
