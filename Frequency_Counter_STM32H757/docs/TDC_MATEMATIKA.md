# Matematika měření a TDC — přesný popis (stav k FW 0x041A, STM v0.14.0, 2026-10-08)

Zdroj pravdy je kód: `Frequency_Counter_FPGA_Module/src/{tdc.v, spi_app.v, top.v}` a
`CM7/Core/Src/fpga_freq.c`. Tento dokument nic nevymýšlí navíc; u každého čísla je uvedeno, zda je
**[kód]** (plyne z RTL/C), **[HW]** (změřeno na desce) nebo **[SIM]** (jen simulace / odhad).
Co nebylo na křemíku ověřeno, je tak označeno.

---

## 1. Co přístroj měří (jedna věta)

Reciproční čítač: spočítá **celé periody N** vstupního signálu mezi dvěma okamžiky a změří
**časový interval Δt** mezi nimi pomocí TDC s rozlišením desítek ps. Kmitočet je

```
f = N / Δt
```

Hradlo (cca 0,25 s) určuje jen *přibližně*, kdy se okno uzavře; skutečná délka okna se **měří**.

---

## 2. Časová základna a jednotky

| veličina | hodnota | zdroj |
|---|---|---|
| referenční hodiny TDC `clk_p0_100m` | 100 MHz (Si5356 z 10 MHz OCXO) | [kód] |
| perioda vzorkování T | 10 000 ps | [kód] |
| jednotka času `u` | **T / 16384 = 0,6103515625 ps** | [kód] `tdc.v` |
| převod | `ps = u · 625 / 1024` (FPGA i STM) | [kód] `dt_units_to_ps` |
| volnoběžný čas `tick_ps` | +16384 jednotek za takt, 48 bitů (přeteče po 172 s) | [kód] `top.v` |
| stavy | časy se vždy používají jen **jako rozdíly** (konstantní offsety pipeline se krátí) | [kód] |

Pozn.: spodních 14 bitů `tick_ps` je trvale 0 (čítá se jen horních 34 bitů, `tick_hi`).

---

## 3. TDC — princip

### 3.1 Řetěz zpoždění
Asynchronní vstup jde do `CIN` prvního bloku **ALU** (hradlo Gowin GW1NR-9C zapojené jako průchod
carry: `I0=1, I1=0` ⇒ `COUT = CIN`, `SUM = ~CIN`). Hrana se šíří řetězem rychlostí ≈ **39 ps na ALU**
[HW; z `10 ns / 252 tapů`]. Každý ALU má vlastní výstup `SUM`, který vzorkuje klopný obvod `q_r`
na náběžné hraně `clk_p0_100m` (`STRIDE = 1` = vzorkuje se **každý** ALU).

Po vzorkování platí: `thermo[i] = 1` ⇔ hrana už tap `i` prošla (`q = ~thermo`).

### 3.2 Kód
```
k = počet úvodních jedniček v thermo = pozice PRVNÍ nuly      (0 … 319; 511 = vše plné)
```
Větší `k` = hrana přišla dřív před vzorkovací hranou (delší zpoždění τ).

Dekodér `tdc_dec`: 10 bloků po 32 tapech, `full[b] = &thermo[32b +: 32]`, vybere se nejnižší
neúplný blok (priorita, 4 bity) a v něm nejnižší nula (priorita, 5 bitů) ⇒ `code = {blok, pozice}` (9 bitů).
Je kombinační; vstup je ZMRAZENÝ (kopie `qd_r`, clock-enable) nejméně 5 taktů, takže cesta má v SDC
multicycle 5/4.

### 3.3 Kalibrace code density (ve FPGA)
Zpoždění tapů **nejsou stejná** a mění se s teplotou/napětím, proto se kalibruje:

1. ring oscilátor (13 LUT inverzí + dělič pseudonáhodným modulem, aby události neležely na mřížce) krmí
   řetěz místo vstupu (`sig_eff = s_col ? ro : sig_raw`);
2. nasbírá se **N_cal = 2^20 = 1 048 576** událostí, `hist[k]++`;
3. tabulka (BRAM, 512 × 15 b):
   ```
   lut[k] = 16384 · ( cum(k) + hist[k]/2 ) / N_cal   (+ 16384 pro sadu R)
   cum(k) = Σ_{j<k} hist[j]
   ```
   Je to distribuční funkce středu binu: pro fázi rovnoměrnou v periodě T je
   `τ(k) = T · (cum + h/2) / N_cal`. Šířka binu `w_k = T · hist[k] / N_cal`.
   V RTL: `xv = 2·cum + hist; lut = xv >> (CAL_LOG2 − 13)`, saturace 16383.
4. kalibrace běží sama ~168 ms po zapnutí a na příkaz `tdc cal` (SET_CONFIG 0x02); během ní se neměří.

### 3.4 Časová značka hrany
```
ts = tick_ps − lut[k]            (lut zahrnuje konstantu +T, tj. 16384 jednotek)
```
`ts` je platná ~8 taktů po spuštění (`ts_valid`). Absolutní offset (latence pipeline) je společný všem
značkám a v rozdílech mizí.

### 3.5 Kvantizační chyba (teorie z histogramu)
Hrana leží rovnoměrně v binu šířky `w`, tabulka vrací jeho střed:
```
σ_q² = Σ_k (w_k / T) · w_k² / 12        σ_Δt = √2 · σ_q   (dvě nezávislé značky)
```
Z histogramu naměřeného na desce [HW, 2026-10-08, FW 0x0412-kompatibilní TDC, rekalibrace při 36,9 °C]:

| kanál | obsazených kódů z 512 | nejvyšší kód | největší bin | medián obsazeného binu | σ_q | σ okna (2 hrany) |
|---|---|---|---|---|---|---|
| A | 154 | 252 | 198 ps (1,9 % T) | 68 ps | **30 ps** | 42 ps |
| B | 165 | 254 | 157 ps (1,5 % T) | 63 ps | **27 ps** | 38 ps |

Průměr na kód je 40 ps, ale 97 (A) / 88 (B) kódů uvnitř rozsahu je prázdných: zpoždění ALU jsou
nerovnoměrná (shluky), takže použitelných stupňů je méně než 320.

⚠️ **Dvě kalibrace za sebou daly různý největší bin** (A: 311 ps dříve, 198 ps nyní). Tabulka není stabilní
mezi kalibracemi; příčina není vyšetřena.

---

## 4. Spouštění, počítání hran, okno

### 4.1 Detekce hrany (bez samostatného spouštěče)
```
q_r[KD] → dR → dRp          (KD = 1; dva klopné obvody = dvoustupňová synchronizace)
ec      = ~dR & dRp          (nová nabíhající hrana vstupu)
rise_s  = ec zpožděný o 1 takt
```
Počítá se **každá** nabíhající hrana (`rise_s`); přesný čas dostane jen ta, která je „žádaná" (`want`).
Při žádané hraně se vzorky zmrazí (`qd_r` přestane načítat) na 5 taktů a dekodér čte zmrazený stav.

Historie vady (FW ≤ 0x040F): samostatný spouštěč `t0` viděl hranu o 1,87 ns (A) / ≈5,3 ns (B) později
než vzorky tapů; hrany z této „slepé" zóny skončily o takt později za koncem řetězu = **obří bin**
(kód ~134, 19 % hran u A, 48 % u B). [HW + SIM] 0x0410 spouštěč odstranilo; na desce obří bin zmizel
(největší bin 198 / 157 ps).

### 4.2 Okno „gap-free"
Hradlo `gate_tick` (0,25 s z čítače na 10 MHz reference) pouze **nahodí `armed`**. Okno uzavře
**první nabíhající hrana po `gate_tick`**; ta dostane přesný čas `ts_close` a je zároveň první hranou
dalšího okna (žádná mezera):
```
N   = count + 1            (count = počet hran uvnitř okna bez uzavírací; +1 = uzavírací)
Δt  = ts_close,k − ts_close,k−1          [jednotky u]
```
Pokud po uzavírací hraně dorazí další hrany během dekódování, patří do **nového** okna (`count` se nuluje při `trig_ack`).
`primed`: první uzavírací hrana okno neuzavírá (nemá začátek).

Čítač hran je rozdělen na dolních 8 b + registrovaný přenos do horních 18 b (časování).

### 4.3 Rámec FPGA → STM (128 B, little-endian)
| pole | bajty (abs) | obsah |
|---|---|---|
| `edge_count` | 20..27 | **N** (počet period v okně) |
| `dt_a` | 118..125 | **Δt** v jednotkách u (CH_A) |
| `gate_ns` | 28..35 | informativně `floor(dt·5/8192)` |
| `frequency_x100000` | 12..19 | **vždy 0** (FPGA kmitočet nepočítá) |
| `edges_b`, `dt_b` | 108..111, 101..106 | totéž pro CH_B |
| `fw_version`, `caps` | 60..63 | 0x0412 / 0x0062 (bit1 SET_CONFIG, bit5 dt, bit6 CAL) |
| `tdc_status` | 100 | bit0/1 cal A/B platná, bit2 fail, bit3 busy, bit4/5 řetěz krátký |

---

## 5. Výpočet kmitočtu na STM (`fpga_freq.c`)

```
gate_ps       = (dt_a · 625 + 512) >> 10                       // u → ps, zaokrouhleno
f_x1e5        = round( N · 1e17 / gate_ps )                    // kmitočet · 1e5 [double, < 2^53]
f_hires [µHz] = dlouhé dělení: (N · mul) · 1e12 / gate_ps      // fpga_freq_scaled, 6 desetin
f_hires [Hz]  = (double)(N · mul) · 1e12 / gate_ps             // přesnost ~2e-16 relativně
```
- `mul` = násobitel hran; ověřuje `fpga_freq_hires_mul` (kandidáti {1,4,16}), tj. porovná výsledek
  s `f_x1e5` na 0,1 %. Na této desce **mul = 1**. Nevyhovuje-li, hi-res se nepoužije (zbývá 5 desetin).
- Výpočet dělí **dlouhým dělením po číslicích**, ne `N·10^k/gate` (přeteklo by `uint64` už kolem 10 MHz).
- `miscount` (`fpga_freq_miscount`): rozdíl proti referenčnímu oknu se porovná s krokem jedné hrany
  `step = mul · 1e18 / gate_ps` [µHz]; je-li rozdíl `k·step` (|k| ≤ 2, tolerance 1/8 kroku), okno se
  vyřadí. **Hlídá jen ±1…2 hrany** — větší chyba počtu hran projde.

### 5.1 Nejistoty (analytické)
Dvě nezávislé značky ⇒ `σ_Δt = √2 · σ_ts`, a protože N je celé číslo:
```
σ_f / f  =  √2 · σ_ts / Δt           (jedno okno 0,25 s)
chyba počtu hran o 1:   Δf / f = 1 / N   (10 MHz, 0,25 s: 4 Hz = 4·10⁻⁷)
```
| σ_ts | σ_f/f (0,25 s) | z toho 10 MHz |
|---|---|---|
| 30 ps (jen kvantizace, z histogramu) | 1,7·10⁻¹⁰ | 1,7 mHz |
| 215 ps (změřeno u FW 0x040F, kap. 5 audit „vlastní reference") | 1,2·10⁻⁹ | 12 mHz |
| 57 ps (konstanta `MP_TDC_PS` pro displej) | 3,2·10⁻¹⁰ | 3,2 mHz |

⚠️ **Rozpor v kódu:** `meas_present.h:143` `MP_TDC_PS = 57.0` je označené jako *HYPOTÉZA z STA modelu*
a řídí (a) počet „důvěryhodných" číslic velkého čísla (`freq_uncertain_frac`), (b) čerchovanou podlahu
Allanova grafu (`adev_floor_base`: ADEV = tdc/(2τ), MDEV = tdc/(2τ√m), HDEV = 0,527·tdc/τ).
Změřená značka měla **215 ps** (FW 0x040F; s obřím binem a INL), histogram dává jen **30 ps** — žádná
z těch tří hodnot spolu nesouhlasí a nová nebyla změřena na FW 0x0410+. Dokud nebude změřen skutečný
rozptyl značky, jsou číslice i podlaha grafu **odhadem**. (Doporučení: nahradit konstantu
za změřenou hodnotu z `tools/tdc_beat.py`.)

### 5.2 „Příliš přesné" číslice při měření vlastní reference
Pokud je vstup synchronní s hodinami TDC (čítač měří vlastní referenci), leží obě krajní hrany okna
na **stejném kódu**: kvantizační chyba je konstantní a v rozdílu `ts_B − ts_A` **zmizí**. Rozptyl pak
vyjde téměř 0 a přístroj ukazuje `10 000 000,000 000 0 Hz`. To není přesnost, ale nepřítomnost nezávislé
chyby. Firmware proto bere `σ = max(naměřené σy, √2·tdc/gate)`.

---

## 6. Regresní blok (FW 0x041A: ZAPNUTÝ a na desce ověřený — podrobnosti kap. 11)

Kromě dvou krajních hran okna se měří i **vnitřní** hrany ve dvou segmentech
(A = [G/16, 3G/16), B = [13G/16, 15G/16), G = délka hradla 0,25 s) a kmitočet je sklon přímky mezi
jejich středy:
```
xm = x0 + Σ(x − x0)/n          // průměrný index hrany v okně (posílá se ×256)
ym = (t0 − ref) + Σ(t − t0)/n  // průměrný čas hrany od začátku okna (posílá se ×16 v jednotkách u)
f  = (xm_B − xm_A) / (ym_B − ym_A)
```
Proč to pomáhá: chyba jedné značky je **bílý šum** ~105 ps (kap. 10); průměr z n značek ho potlačí
o √n (při 10 MHz má segment 312 500 značek). Nepotlačí se deterministická část (kvantizace ~30 ps), která
se u signálu blízkého násobku 100 MHz během segmentu nemění (fáze se posune jen o ~25 ps).
STM regresi použije, jen když souhlasí s dvoubodovým odhadem do `FPGA_REGR_MAX_REL` (5·10⁻⁸) a obě
poloviny mají ≥ 8 značek; jinak zůstane dvoubodový výsledek. Efektivní délka okna = N / f_regr.

---

## 7. Kolik prvků TDC má

### 7.1 Jeden kanál (A i B jsou stejné; `TDC_TAPS = 320`, `STRIDE = 1`, `KD = 1`, `DUAL = 0`)
| prvek | počet | poznámka |
|---|---|---|
| **ALU ve zpožďovacím řetězu** | **320** | [HW] použitelných ~252 (fyzický konec řetězu; maxtap 248) |
| **vzorkovací klopné obvody `q_r`** | **320** | volně běží každý takt, `keep` |
| **zmrazitelná kopie `qd_r`** | **320** | clock-enable `ld`, čte dekodér |
| detekce hrany `dR`, `dRp`, `ec_r` | 3 FF | synchronizace + počítání |
| řídicí `idle_r`, `arm_r`, `fz[2:0]` | 5 FF | zmrazení vzorků 5 taktů |
| dekodér `tdc_dec` | 10 × AND32, prioritní výběr bloku, 32:1 mux, prioritní kodér | stovky LUT (netlist: skupiny `ld_c`+`pg_c`+`code` ≈ 830 LUT na oba kanály) |
| kód `code_r` (9 b), `hicode_r` | 18 FF | `hicode_r` = diagnostika nejvyššího tapu |
| paměť **histogramu** | 512 × 21 b | BRAM, adresa = kód |
| paměť **kalibrační tabulky** | 512 × 15 b | BRAM, adresa = kód |
| řízení kalibrace | `cnt` 21 b, `cum` 22 b, `xv`, `hcur`, `lutv` … | one-hot stavy `s_clr/s_col/s_drn/s_cmp` |
| časová značka `ts_ps` | 48 b | odčítačka `tick_ps − lut` |

### 7.2 Celý návrh (2 kanály + sdílené) — výsledek P&R FW 0x0412 [P&R]
| zdroj | využito | z | % |
|---|---|---|---|
| Logic (LUT/ALU) | 3 579 | 8 640 | 42 % |
| Register (FF) | 3 144 | 6 693 | 47 % |
| **CLS** (hlavní omezení) | 2 790 | 4 320 | **65 %** |
| BSRAM | 5 | 26 | 20 % |
| DSP | 5 | 10 | 50 % |
| ALU v netlistu celkem | 1 295 | — | z toho **640 = řetězy TDC** |
| FF v netlistu: `q_r` 640 + `qd_r` 640 | 1 280 | | největší jednotlivé skupiny |

Časování: `clk_p0_100m` Fmax **103,1 MHz**, setup slack **+0,299 ns**, TNS 0, hold 0.
Sdílené: jeden ring oscilátor, `tick_hi` 34 b, `gtmr` 24 b, SPI rámce v BRAM.

---

## 8. Omezení a neověřené věci (poctivě)

1. **V řetězu smí být jedna hrana** ⇒ vstup do ~30 MHz; nad tím se musí dělit předděličkou (HW na nové desce).
2. Tabulka kalibrace **platí pro teplotu a napětí kalibrace**; dvě kalibrace za sebou daly různý největší bin (311 vs 198 ps).
3. **Počet hran:** na FW 0x0411 byl na desce o ~0,9 % vyšší (`edge_count` 2 520 559 místo 2 500 002 při správném `dt_a`);
   příčina nevyšetřena (regrese / zdroj hran 0x0410 / zapojení). `miscount` to nechytí (hlídá ±1…2 hrany). Viz `docs/audit/2026-10-07_kriticky-audit.md` kap. 6.
4. **INL/DNL na křemíku nezměřena** (potřebuje posun fáze se zlomkem 0,05–0,95, tj. rozladění reference).
5. Rozlišení značky **na FW 0x0410+ nezměřeno** (215 ps platí pro 0x040F s obřím binem).
6. Konstanta `MP_TDC_PS = 57 ps` je odhad (kap. 5.1).
7. Hradlo FPGA je **vždy 0,25 s**; `fpga_freq_set_window()` nikdo nevolá, tlačítko GATE mění jen softwarové průměrování v STM.

## 9. Měření po opravě vstupu (HW 2026-10-08, FW 0x0412, vstup 0–2,86 V)

| veličina | hodnota |
|---|---|
| počet hran | `CITANI HRAN` 0 navíc / 0 chybějících, SEQ bez mezer (300 s, 1201 oken) |
| σ okna (rozdíl dvou značek), proti klouzavému mediánu 21 oken | **196 ps** (99,8 % oken pod 1,5 ns, žádné skoky ±10 ns) |
| ⇒ σ jedné značky (nezávislé) | **≈ 139 ps** (kvantizace z histogramu jen 30 ps → zbytek = INL, šum vstupu, jitter hodin) |
| posun fáze na okno | 3,4 ns (zlomek 0,34) → fáze pokrývá celou periodu, INL jde mapovat |
| INL (nástroj `tdc_beat.py`, orientačně) | hladký průběh přibližně ±300 ps přes periodu 10 ns |
| `spike-reject` (práh 3·10⁻⁹) | vyřazuje 8,3 % oken = běžný šum, ne artefakt → k uvolnění |

⚠️ Čísla šumu z doby před opravou vstupu (215 ps, skoky ±9,28 ns, miscount 2 %) **nejsou důvěryhodná** (L-0137).

## 10. INL a šum značky — měření s kódy TDC (HW 2026-10-08, FW 0x0418, 2048 po sobě jdoucích oken)

FPGA od 0x0418 posílá kód TDC uzavírací i zahajovací hrany okna (bajty 68..75, caps bit8); STM je zapisuje do záznamníku (`inl`),
`tools/tdc_inl_fit.py` odhaduje chybu tabulky `e[kód]` metodou nejmenších čtverců společně s pomalým driftem kmitočtu
(ověřeno na syntetických datech: korelace 0,98 se skutečnou chybou).

| otázka | odpověď z dat |
|---|---|
| Je chyba stabilní funkcí kódu (INL)? | **Ne.** Fit na 1. polovině, test na nevidené 2.: σ okna **141 → 141 ps** (MAD), 156 → 158 ps (std). Korekce podle kódu nic nezlepší. |
| Povaha šumu | **bílý**: autokorelace oken lag 1 = **−0,47** (≈ −0,5 = nezávislé značky), lag 2 = +0,11 |
| σ okna / σ značky | 141–156 ps / **≈ 100–110 ps** (σ_ts = σ_okna/√2) |
| Rozdělení | skoro Gaussovo (std/MAD = 1,1), |r| > 600 ps jen 0,2 % → **ne** chybně dekódované hrany |
| Závislost na poloze hrany | σ okna podle kódu konce okna: kód 0–120: 170–200 ps; kód 120–250: 124–129 ps (tj. značka ≈ 88 ps vs 135 ps) |
| Stabilita v čase | σ po blocích 256 oken: 148–166 ps, bez trendu |

**Závěr:** zbývajících ~100 ps na značku není vada tabulky ani dekodéru (kvantizace jen 30 ps), ale **náhodný šum**. Nejpravděpodobnější zdroj je hrana
na vstupním pinu (74HC04 → 120 Ω, strmost kolem 1 V/ns: 50 mV šumu na 5V napájení HC04 = 50–70 ps) nebo napájení/jitter hodin.
Nástroje korekce proto nemají co opravit; změní se to jen zlepšením hrany, zprůměrováním více značek nebo rozlišením šumu zdroje od TDC
(viz navržený experiment: stejný signál na CH_A i CH_B, rozdíl dt_a − dt_b = čistý šum TDC).

⚠️ Regresní blok (FW 0x0411) při čistém vstupu: `n_A` = 312 499 a `xm_A` = 312 500 jsou přesné, ale `ym_A` je nesmyslné (≈ 5,2 s) a segment B se neaktualizuje
(`n_B` = 1024, `xm_B`/`ym_B` konstantní) → regrese je stále nefunkční; příčina v návrhu (nejen ve vstupu) je **neuzavřená**.

## 11. Regrese na křemíku — proč selhávala a výsledek (HW 2026-10-08, FW 0x041A)

**Příčina selhání nebyla v logice, ale v časové rezervě.** Postup (všechno měřeno, ne odhadnuto):

| build | nejtěsnější cesta regrese | výsledek na desce |
|---|---|---|
| 0x0411 | +0,017 ns | segment B se nikdy neaktualizoval, `ym_A` nesmysl |
| 0x0419 (ladicí) | −0,19 ns | Σ(t − t0) o 1,3 % nižší |
| 0x041A bez nejistoty hodin | +0,15 ns | zlomek x̄ 0,07–0,55 místo přesně 0/0,5 (chyby v řádu ns) |
| 0x041B/0x041C (ladicí, tatáž logika) | ≥ 0,37 / 0,58 ns | značky souvislé, Σ(x−x0) = n(n−1)/2 na bit, podíl přesný |
| **0x041A + `set_clock_uncertainty 0,5 ns`** | +0,135 ns **po** odečtení 0,5 ns | **všechno přesně, viz níže** |

Simulace celého `top.v` (`sim/tb_top_regr.sv`, zkrácené hradlo) i jednotkový test v plné velikosti
(`sim/tb_regr_full.sv`, 60 000 až 312 500 značek) procházely i pro vadné buildy → vada byla fyzická.
`timing.sdc` neměl nejistotu hodin, takže STA počítala s ideálními 100 MHz; na křemíku chybělo ~0,3 ns
(jitter Si5356, vstupní buffer hodin). Nyní `set_clock_uncertainty -setup 0.5` nutí P&R tuto rezervu dodržet.
Současně byla `regr_acc` přepsána tak, aby žádná cesta neměla víc než ~2 úrovně LUT a žádný řídicí signál
nerozváděl do desítek klopných obvodů (registrované začátky/konce segmentů, součty s registrovaným
přenosem, dělič s předpočítanými řídicími signály ve 4 kopiích).

**Výsledek na desce** (GPSDO 10 MHz → CH_A, vnitřní OCXO, δ = +813 ppb, 120 s / 2628 oken):

| | dvoubodový odhad (2 značky) | **regrese** |
|---|---|---|
| σ(df/f) sousedních oken / √2 (okno 0,25 s) | 7,6·10⁻¹⁰ (≈ 191 ps ekv.) | **5,85·10⁻¹¹ (≈ 15 ps ekv.)** |
| použitá okna | — | 2628 / 2628 (zamítnuto 0) |
| střední rozdíl f_regr − f_2pt | — | 0,000 ppb (σ 0,46 ppb = šum dvoubodového odhadu) |
| počty značek A/B | — | 312 499 / 312 500 (každá hrana segmentu) |
| zlomek x̄ | — | přesně 0 nebo 0,5 (souvislé značky) |

⇒ **13× přesnější kmitočet z jednoho okna 0,25 s**. Chyby počítání: 0 (CRC 0, díry v SEQ 0, miscount 0).

**Zdroje FPGA (FW 0x041A, P&R):** Logic 4 359 / 8 640 (51 %), Register 3 841 / 6 693 (58 %),
**CLS 3 327 / 4 320 (78 %)**, BSRAM 5/26, DSP 6/10; Fmax 101,4 MHz s nejistotou 0,5 ns (reálná rezerva ≥ 0,64 ns).

⚠️ Konstanta `MP_TDC_PS` (57 ps) pro počet důvěryhodných číslic a podlahu Allanova grafu neodpovídá
ani dvoubodovému šumu (~105 ps), ani regresi (ekv. ~10 ps na značku) — viz STATUS, k řešení.
⚠️ Regrese pokrývá jen CH_A (CH_B by potřeboval druhý blok, +~11 % CLS).
