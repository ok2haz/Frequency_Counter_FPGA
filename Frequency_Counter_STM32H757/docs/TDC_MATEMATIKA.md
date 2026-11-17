# Matematika měření, TDC a regresní blok — dokument k revizi

| | |
|---|---|
| **Verze dokumentu** | 2.0 (2026-10-09) — přepsáno z průběžně doplňované verze 1.x, ta zůstává v gitu |
| **Platí pro** | FPGA **FW 0x041A** (commit `2366b16`, bitstream `Frequency_Counter_FPGA_Module/ab_test/FW_0x041A_regr_unc.fs`, složka není v gitu), STM `gpsdo-ui v0.14.0` (+ commit `d269ecc`) |
| **Hardware** | Tang Nano 9K (GW1NR-9C) + STM32H757; vstup CH_A: GPSDO 10 MHz přes 74HC04 → koax 80 cm → pin 25 s 120 Ω proti GND (0 až 2,86 V) |
| **Stav** | ⬜ **čeká na revizi** (kap. 12 obsahuje otevřené body a otázky pro revizora) |

**Štítky důkazu** (u každého čísla a tvrzení): **[kód]** plyne z RTL/C · **[HW]** změřeno na desce ·
**[SIM]** jen simulace · **[ÚVAHA]** odvozeno, neměřeno · **[HYP]** hypotéza, kterou je třeba ověřit.
Nic, co není označeno [HW], se nesmí brát jako ověřené na křemíku.

---

## Obsah
1. Souhrn
2. Signálová cesta
3. Časová základna a jednotky
4. TDC: řetěz, kód, kalibrace, časová značka
5. Okno a počítání hran
6. Rámec FPGA → STM
7. Výpočet kmitočtu na STM
8. Regresní blok
9. Chybový rozpočet
10. Měření (kronika a co z ní plyne)
11. Zdroje, časování a ověření
12. Otevřené body a otázky pro revizora
13. Příloha: vzorce, soubory, příkazy, změny proti verzi 1.x

---

## 1. Souhrn

Přístroj je **reciproční čítač**: spočítá **celé periody N** vstupního signálu mezi dvěma uzavíracími
hranami okna a změří **časový interval Δt** mezi nimi pomocí **TDC** (carry-chain, 320 ALU na kanál).

```
f = N / Δt                      N = celé číslo (počet hran),  Δt = rozdíl dvou časových značek
```

Hradlo FPGA je pevně **0,25 s** (tlačítko GATE, web a SCPI mění jen softwarové průměrování na STM,
`fpga_freq_set_window()` nemá volajícího). Okna na sebe **navazují bez mezery** (gap-free), takže součet
Δt po sobě jdoucích oken je přesně rozdíl dvou značek.

Kromě dvou krajních hran okna FPGA od FW 0x0411 měří i **vnitřní hrany** ve dvou segmentech okna a posílá
jejich průměry (n, x̄, ȳ); STM z nich spočte kmitočet jako sklon přímky mezi středy segmentů (kap. 8).

| veličina | hodnota | důkaz |
|---|---|---|
| jednotka času u | T/16384 = 0,6103515625 ps | [kód] |
| zpoždění jednoho ALU | ≈ 39,7 ps (10 ns / 252 tapů) | [HW] |
| použitelných tapů z 320 | ≈ 252 (nejvyšší kód 252/254), obsazených kódů 154/165 z 512 | [HW] |
| kvantizační šum značky (z histogramu) | σ_q = 30 ps (A), 27 ps (B) | [HW] |
| **celkový šum jedné značky** | **80 – 160 ps podle estimátoru, nejlepší odhad ≈ 105 ps** (bílý, nezávislý) | [HW] |
| stabilní INL podle kódu | **nenalezen** (křížová validace: 141 → 141 ps) | [HW] |
| ADEV(0,25 s) dvoubodový → s regresí | 7,6·10⁻¹⁰ → 5,4 – 6,5·10⁻¹¹ (≈ 12 – 14×; horní mez přístroje, obsahuje šum oscilátorů) | [HW] |
| teoretický limit regrese (bílý šum) | 1,4·10⁻¹² | [ÚVAHA] |
| chyba počtu hran 10 MHz / 0,25 s | 1 hrana = 4·10⁻⁷ (4 Hz) | [kód] |
| FPGA: CLS / Fmax | 78 % / 101,4 MHz (s nejistotou hodin 0,5 ns) | P&R |

**Co je spolehlivé:** logika počítání hran a oken (simulace i HW), regresní blok (HW: 40 654 oken bez jediného zamítnutí),
převod jednotek. **Co spolehlivé není:** zdroj zbylého šumu značky (~105 ps), chování regrese na zamčeném signálu (δ → 0),
vliv nahrazení délky okna na dlouhá τ (kap. 12, O1), stabilita kalibrace mezi běhy.

---

## 2. Signálová cesta

```
GPSDO 10 MHz ──(HC04, 120 Ω)──► pin 25 ──► LUT mux (sig_eff) ──► ALU řetěz 320× ──► q_r (320 FF, volně, každý takt)
                                      ▲                                             │ 2. stupeň
          ring oscilátor (kalibrace) ─┘                                      qd_r (320 FF, zmrazitelná kopie) ──► dekodér ──► kód 9 b
                                                                                │
                                          q_r[KD=1] ─► dR ─► dRp ─► ec ─► rise_s (počítání hran)
                                                                    └──► trig_ok (spuštění přesné časové značky)
                                      kód ──► lut[kód] ──► ts = tick_ps − lut ──► win_recip ──► r_periods, r_dt
                                                                              └─► regr_acc (vnitřní hrany)  ──► rámec 128 B ──► SPI ──► STM
```

Všechny časově kritické části běží na `clk_p0_100m` (100 MHz, z Si5356 z 10 MHz OCXO). Aplikační vrstva a SPI na `clk_ref_10m`
(10 MHz) a `spi_sck`. Přechody mezi doménami: toggle-handshake `res_tgl` a 2–3FF synchronizéry.

Poznámka k signálu: vstup musí mít úrovně LVCMOS33 (VIL ≤ 0,8 V, VIH ≥ 2,0 V) a **jednu** hranu v řetězu
(≤ ~30 MHz). Dokud na pinu bylo jen 0–1,55 V, počet hran byl chybný a všechna měření šumu byla neplatná (L-0137).

---

## 3. Časová základna a jednotky

| | |
|---|---|
| hodiny TDC | `clk_p0_100m` = 100 MHz, perioda T = 10 000 ps [kód] |
| jednotka u | **T / 16384 = 0,6103515625 ps**; převod `ps = u · 625 / 1024` [kód] |
| volnoběžný čas | `tick_ps` = +16 384 u za takt (48 b, přeteče po 171,8 s); spodních 14 b je trvale 0, čítá se jen horních 34 b [kód] |
| proč 16384 | mocnina dvou ⇒ převod kalibrační tabulky je posun, ne dělení |
| absolutní čas | **nepoužívá se**: konstantní zpoždění pipeline je společné všem značkám a v rozdílech mizí |

---

## 4. TDC

### 4.1 Zpožďovací řetěz [kód, HW]
Vstup jde do `CIN` prvního **ALU** (hradlo Gowin zapojené jako průchod přenosu: `I0 = 1, I1 = 0` ⇒ `COUT = CIN`, `SUM = ~CIN`).
Hrana se šíří řetězem po ≈ **39,7 ps/ALU**. Každý ALU má vlastní výstup `SUM`, který vzorkuje klopný obvod `q_r` na hraně
`clk_p0_100m` (`STRIDE = 1`: vzorkuje se **každý** ALU, `TDC_TAPS = 320`). Atribut `syn_keep` je nutný, jinak syntéza průchozí
stupně zkrátí na vodič. Úplnost řetězu kontroluje `sim/check_tdc_netlist.py` (2 × 320 stupňů, CIN každého = COUT předchozího).

Fyzicky se hrana za 10 ns dostane asi na tap **248–252**; zbylých ~68 tapů se nevyužije [HW: `tdc` hlásí „za koncem řetězu 0, maxtap 248“].

### 4.2 Kód a dekodér
Po vzorkování platí `thermo[i] = 1` ⇔ hrana už prošla tap `i` (`thermo = ~qd`). **Kód** = počet úvodních jedniček = index první nuly:

```
k ∈ 0 … 319,   k = 511 (= samé jedničky) značí „celý řetěz plný“
```

Větší k = hrana přišla dřív před vzorkovací hranou (delší τ). Dekodér `tdc_dec`: 10 bloků po 32 tapech; `full[b] = &thermo[32b +: 32]`;
nejnižší neúplný blok (4 b) a v něm nejnižší nula (5 b) ⇒ `code = {blok, pozice}`. Je **kombinační** a čte zmrazenou kopii `qd_r`
(≥ 5 taktů) ⇒ v SDC je `multicycle` 5/4 (qd → code_r, hicode_r).

Zpoždění ALU **nejsou rovnoměrná**: obsazeno je jen 154 (A) / 165 (B) kódů z ~250 možných; 97 / 88 kódů uvnitř rozsahu je prázdných,
z toho většina lichých (A: 75 z 97) — Gowin skládá ALU po dvojicích, přenos uvnitř dvojice je rychlejší než mezi dvojicemi [HW + HYP]. Medián šířky
obsazeného binu 68 / 63 ps, největší 198 / 157 ps. Kalibrace nerovnoměrnost započítá (kap. 4.3).

### 4.3 Kalibrace code density (ve FPGA)
1. Ring oscilátor (13 LUT inverzí s pseudonáhodným dělením LFSR, aby události nelezly na mřížce vůči hodinám) krmí řetěz místo vstupu
   (`sig_eff = s_col ? ro : sig_raw`).
2. Nasbírá se **N_cal = 2²⁰** událostí; `hist[k]++` (BRAM 512 × 21 b).
3. Z histogramu se spočte tabulka (BRAM 512 × 15 b): pro fázi rovnoměrnou v periodě je zpoždění středu binu k

   ```
   τ(k) = T · ( cum(k) + hist[k]/2 ) / N_cal ,          cum(k) = Σ_{j<k} hist[j]
   lut[k] = 16384 + 16384 · ( cum(k) + hist[k]/2 ) / N_cal      // RTL: (2·cum + hist) >> (CAL_LOG2 − 13), saturace 16383, +16384 = bit 14
   ```
   Šířka binu `w_k = T · hist[k] / N_cal`. Konstanta +T (bit 14) je pevný posun, který se v rozdílech krátí.
4. Kalibrace startuje sama 2²⁴ taktů (168 ms) po zapnutí a na příkaz STM (`tdc cal`, SET_CONFIG 0x02). STM ji vyvolá také při driftu teploty FPGA
   ≥ 3 °C (nejvýš 1× za 30 s). **Během kalibrace se neměří** (`hold`: rozpracované okno se zahodí).

Statistická chyba tabulky: max. odchylka distribuční funkce ≈ 0,87/√N_cal ≈ 8·10⁻⁴ ⇒ ≲ **8 ps** [ÚVAHA].
⚠️ Předpoklad rovnoměrnosti fáze ring oscilátoru vůči hodinám **není ověřen**; dvě po sobě jdoucí kalibrace daly největší bin 311 ps a 198 ps [HW] (otevřený bod O3).

### 4.4 Časová značka
```
ts = tick_ps − lut[kód]                      (48 b, jednotky u)          [kód]
```
Platná ≈ 7–8 taktů po spuštění (`ts_valid`). Absolutní offset (pipeline) je u všech značek stejný a v rozdílech mizí.

### 4.5 Detekce hrany, spuštění a mrtvá doba [kód]
```
q_r[KD] → dR → dRp          KD = 1 (tap 1; „slepota“ detekce je jen poloha tapu, ne ~2–5 ns jako ve FW ≤ 0x040F)
ec     = ~dR & dRp          nová nabíhající hrana (q = 1 znamená „hrana ještě neprošla“)
rise_s = ec zpožděné o 1 takt   (počítání hran)
trig_ok = ec & arm_r & idle_r   (spuštění přesné značky), arm_r = kalibrace | want | want_seg
```
Při spuštění se kopie `qd_r` zmrazí (clock enable `ld = idle_r & ~(ec & arm_r)`) na 5 taktů (`fz` = 1…4, pak `idle_r`); na konci (`fz == 4`) se kód
uloží do `code_r`. Z toho: **nová značka nejdřív za 5 taktů (50 ns)**; hrany uvnitř mrtvé doby se **počítají**, ale nedostanou čas.
Při 10 MHz (10 taktů mezi hranami) je každá hrana označitelná [HW: n_A = 312 499 z 312 500 hran].

Historie: FW ≤ 0x040F měl samostatný spouštěč `t0`, který viděl hranu o 1,87 ns (A) / ≈ 5,3 ns (B) později než vzorky tapů, takže hrany
z této „slepé“ zóny skončily o takt později za koncem řetězu = **obří bin** (kód ~134; šířka a + 10 − 10,45 ns). FW 0x0410 `t0` odstranilo
(detekce z 2. stupně téhož vzorku q[KD]); obří bin na desce zmizel (největší bin 157–198 ps) [HW]. Hodnoty „a“ jsou odvozené z měření obřího binu, ne změřené přímo.

### 4.6 Kvantizační šum (teorie)
Hrana leží rovnoměrně v binu šířky w, tabulka vrací střed ⇒ σ_q² = Σ_k (w_k/T) · w_k²/12 = Σ w_k³ /(12 T). Z naměřeného histogramu:
**σ_q = 30 ps (A), 27 ps (B)**, okno (2 značky) 42 / 38 ps [HW, `tools/tdc_hist_analysis.py`]. To je **dolní mez** šumu značky; měřený je ~3,5× vyšší (kap. 9, 10).

---

## 5. Okno a počítání hran

### 5.1 Gap-free okno [kód]
Hradlo `gate_tick` (0,25 s z čítače `gtmr` na 10 MHz) jen **nahodí `armed`**. Okno uzavře **první nabíhající hrana po `gate_tick`**; ta dostane
přesnou značku `ts_close` a je zároveň první hranou dalšího okna:

```
N  = (počet hran uvnitř okna) + 1           (+1 = uzavírací hrana; zahajovací hrana patří předchozímu oknu)
Δt = ts_close,k − ts_close,k−1              [u]
```

Pomocné signály: `pend`/`pend_n` — `rise_s` uzavírací hrany přichází 0–2 takty po `trig_ack`; pojistka po 3 taktech zavře okno i bez ní.
`primed` — první uzavírací hrana okno neuzavírá (nemá začátek). `age`/`alias` — okno bez hran > ~25 s se označí.
Čítač hran: dolních 8 b + registrovaný přenos do horních 18 b (časování). Hrany, které přijdou po uzavírací hraně během dekódování, patří do **nového** okna.

### 5.2 Telescoping (důležité pro dlouhé τ)
Protože sousední okna sdílejí uzavírací značku, platí **přesně**
```
Σ_{k=1..M} Δt_k = ts_M − ts_0          ⇒   f̄_M = Σ N_k / Σ Δt_k   má chybu jen dvou značek,  ne  M značek
```
Tj. šum dvoubodového odhadu je pro dlouhé průměrování **bílý fázový šum** (ADEV ∝ 1/τ), ne bílý frekvenční (ADEV ∝ 1/√τ). Viz kap. 8.5 (O1).

---

## 6. Rámec FPGA → STM (128 B, little-endian, CRC-16/CCITT-FALSE přes 0…125) [kód]

| bajty (abs) | pole | poznámka |
|---|---|---|
| 0–11 | MAGIC `A5`, VER `02`, TYPE `80`, FLAGS, SEQUENCE u32, PAYLOAD_LEN, rez. | |
| 12–19 | `frequency_x100000` | **vždy 0** (FPGA kmitočet nepočítá) |
| 20–27 | **`edge_count` = N** | |
| 28–35 | `gate_ns` = ⌊Δt·5/8192⌋ | informativně |
| 36–43 | `timestamp` (10 MHz čítač) | |
| 44–51 | kanál, status, error_flags, phase_status, status2 | |
| 52–59 | `freq16_x100000` | vždy 0 |
| 60–63 | `fw_version` = 0x041A, `caps` = 0x00E2 | caps: bit1 SET_CONFIG, bit5 dt, bit6 CAL, bit7 REGR (bit8 = kódy TDC jen když je REGR vypnuto) |
| 64 | `clk_status` | |
| **68–96** | **regresní blok** | `n_A` u24 (68), `xm_A` u40 (71), `ym_A` u48 (76), `n_B` (82), `xm_B` (85), `ym_B` (90), `ok` bit0 = A, bit1 = B (96) |
| 100 | `tdc_status` | bit0/1 kalibrace A/B platná, bit2 fail, bit3 busy, bit4/5 řetěz krátký |
| 101–106 | `dt_b` u48 | CH_B |
| 108–111 | `edges_b` u32 | CH_B |
| 112–117 | diagnostika SPI PHY | CRC spočtené/přijaté, hrany MOSI |
| **118–125** | **`dt_a` u64** | **Δt v jednotkách u** (STM maskuje na 48 b; [124,125] = hrany SCK) |

Ve FW 0x0418 (REGR vypnuto, caps 0x0162) obsahují bajty 68–75 kódy TDC uzavírací/zahajovací hrany okna A a B (jen pro INL diagnostiku, UART `inl`).

---

## 7. Výpočet kmitočtu na STM (`CM7/Core/Src/fpga_freq.c`)

### 7.1 Základ [kód]
```
gate_ps  = (dt_a · 625 + 512) >> 10                        // u → ps, zaokrouhleno
f_x1e5   = round( N · 1e17 / gate_ps )                      // kmitočet ·10⁵ (double, < 2⁵³)
f_uHz    = fpga_freq_scaled(N·mul, gate_ps, 6)              // dlouhé dělení po číslicích (N·10⁶·10⁶ by přeteklo uint64)
f_Hz     = (double)(N·mul) · 1e12 / gate_ps                 // přesnost ~2·10⁻¹⁶ relativně
```
`mul` = násobitel hran, ověřuje `fpga_freq_hires_mul` (kandidáti {1, 4, 16}; porovná výsledek s `f_x1e5` na 0,1 %); na této desce **mul = 1**.

**Příklad** (skutečný rámec z desky, FW 0x0412): N = 2 500 002, dt_a = 409 599 983 539 u ⇒ gate_ps = 249 999 989 953 ⇒ **f = 10 000 008,4018803 Hz**
(`f_x1e5` = 1 000 000 840 188). Jedna hrana navíc by posunula výsledek o 4,00000016 Hz.

### 7.2 Kontrola počtu hran (`fpga_freq_miscount`)
Rozdíl vůči referenčnímu oknu se porovná s krokem jedné hrany `step = mul · 1e18 / gate_ps` [µHz]; je-li rozdíl `k·step` (|k| ≤ 2, tolerance 1/8 kroku),
okno se vyřadí. **Hlídá jen ±1…2 hrany** (větší chyba počtu projde) — proto je nutné kontrolovat počet hran i jinak (`CITANI HRAN` ve `status`).

### 7.3 Ochrana proti obřímu binu (`spike-reject`)
Skok kmitočtu oproti poslednímu přijatému oknu > 2·10⁻⁸ (≈ 5 ns v Δt; obří bin = 9,28 ns = 3,7·10⁻⁸) vyřadí okno ze statistiky. Do 2026-10-08 byl práh
3·10⁻⁹ (3 σ) a s čistým vstupem vyřazoval 8,3 % běžných oken.

### 7.4 Akceptace regrese (`parse_data`) [kód]
```
fr = ( (xm_B − xm_A)/256 ) / ( ((ym_B − ym_A)/16) · (625/1024) ps ) · 1e12
akceptuj ⇔  n_A ≥ 8  ∧  n_B ≥ 8  ∧  fr > 0  ∧  | fr / f_2pt − 1 | < 5·10⁻⁸          (FPGA_REGR_MAX_REL, FPGA_REGR_MIN_N)
pak:  gate_ps := N · 1e12 / fr       // „efektivní“ délka okna; všechny další výpočty (hi-res, statistika, datalog, IPC, SCPI) dostanou f_regr
jinak: zůstává dvoubodový výsledek, okno se započítá do `rejected`
```
Mez 5·10⁻⁸ ≈ 40 σ dvoubodového odhadu. Do 2026-10-08 byla 3·10⁻⁷ (250 σ — příliš volná, propustila by chybu 3 Hz na 10 MHz).

---

## 8. Regresní blok

### 8.1 Princip a odvození [ÚVAHA]
V segmentu S (A = [G/16, 3G/16), B = [13G/16, 15G/16), G = 0,25 s ⇒ **každý 31,25 ms, 312 500 hran při 10 MHz**, středy 0,03125 s a 0,21875 s, rozestup **0,1875 s**)
FPGA označí časem téměř každou hranu. Hrana i má index x_i (pořadí v okně) a naměřený čas y_i = t_i + m_i, kde t_i = t₀ + (x_i − x₀)/f je skutečný čas a m_i chyba značky:

```
ȳ_S = t₀ + (x̄_S − x₀)/f + m̄_S
f̂  = (x̄_B − x̄_A) / (ȳ_B − ȳ_A)   ⇒   relativní chyba  ε ≈ −(m̄_B − m̄_A) / Δȳ ,   Δȳ = 0,1875 s
```
Index x̄ je **přesný** (všechny hrany se počítají), takže chyba pochází jen z m̄. Pro nezávislé značky o rozptylu σ²: σ_ε = √(σ_A²/n_A + σ_B²/n_B) / Δȳ.
S σ = 105 ps a n = 312 500: **σ_ε = 1,4·10⁻¹²** (ekvivalent 0,35 ps v okně) oproti dvoubodovému √2·σ/G = 5,9·10⁻¹⁰.

Deterministická složka chyby (kvantizace + případné INL) se průměrem nesnižuje, ale: (a) při rozladění δ = 813 ppb projde fáze segmentem **25,4 ns** = 2,5 periody hodin,
tedy všemi kódy a střední chyba tabulky se vyruší; (b) při zamčeném signálu (δ → 0) je fáze v A i B stejná a deterministická chyba je **společná** a v rozdílu ȳ_B − ȳ_A se vykrátí;
(c) bílý šum ~100 ps ≫ šířka binu 40–70 ps působí jako **dither** a linearizuje kvantizaci [HYP]. Případ δ → 0 nebyl na desce měřen (O10).

### 8.2 Co FPGA počítá (`regr_acc`) [kód]
Pro každou označenou hranu segmentu se jen **sčítá** (žádné násobení):
```
n    = počet označených hran                    (3 × 8 b s registrovaným přenosem, max. 2²⁴)
x₀, t₀ = index a čas první hrany segmentu
Σx   = Σ (x_i − x₀)                             (48 b = 26 b + 22 b s registrovaným přenosem)
Σy   = Σ (t_i − t₀)                             (64 b = 2 × 32 b s registrovaným přenosem; rozdíl t−t₀ < 2⁴⁰ u = 0,67 s)
```
Po konci segmentu se **jednou** vydělí sekvenčním dělicím algoritmem (restoring, 68 iterací po 2 taktech, dva průběhy = ~272 taktů ≈ 2,7 µs):
```
xm = x₀·256 + ⌊ Σx · 256 / n ⌋              (u40, odesílá se ×256)
ym = (t₀ − ref_ts)·16 + ⌊ Σy · 16 / n ⌋     (u48, odesílá se ×16; ref_ts = čas zahajovací hrany okna)
```
Pipeline značky: stupeň 1 (rozdíly, jen při značce), 2 (dolní části součtů + čítač), 3 (horní části). Před posouzením `n ≥ 2` se čeká 5 taktů na doběh pipeline.
**Podmínka návrhu:** konec segmentu B musí předcházet uzavírací hraně o víc než ~300 taktů (G/16 = 1,56 M taktů při 0,25 s ✓).

### 8.3 Časování — proč dřívější verze na desce selhávaly [HW]
Logika byla správná (ověřeno ladicími buildy s počítadly přenosů, souvislostí značek a surovými součty — shodné na bit se spočtenými hodnotami), selhávalo **časování**:

| build | nejtěsnější cesta regrese | výsledek na desce |
|---|---|---|
| 0x0411 | +0,017 ns | segment B se nikdy neaktualizoval, `ym_A` nesmysl |
| 0x0419 (ladicí) | −0,19 ns | Σ(t − t₀) o 1,3 % nižší |
| 0x041A bez nejistoty hodin | +0,15 ns | zlomek x̄ chybný (chyby v řádu ns) |
| 0x041B / 0x041C (ladicí, tatáž logika) | ≥ 0,37 / 0,58 ns | značky souvislé, Σ(x−x₀) = n(n−1)/2 přesně, podíl přesný |
| **0x041A + `set_clock_uncertainty 0,5 ns`** | +0,135 ns **po** odečtení 0,5 ns | vše přesně (kap. 10) |

`timing.sdc` neměl nejistotu hodin a STA počítala s ideálními 100 MHz; reálně chybělo ~0,3 ns (jitter Si5356, vstupní buffer). Nyní je v SDC
`set_clock_uncertainty -setup -from clk_p0_100m -to clk_p0_100m 0.5` a `regr_acc` je přepsána tak, aby žádná cesta neměla víc než ~2 úrovně LUT
(registrované začátky/konce segmentů, rozdíly jen při značce, součty s registrovaným přenosem, dělič s předpočítanými řídicími signály ve 4 kopiích).
⚠️ Hodnota 0,5 ns je **empirická** (neúspěšné buildy ≤ 0,15 ns, úspěšné ≥ 0,58 ns), ne odvozená z rozpočtu jitteru (O2).

### 8.4 Rozsah ověření
Jen **CH_A**, jen **10 MHz**, jen hradlo **0,25 s**, jen δ ≈ +813 ppb. Pro jiné hradlo (100 ms, 1 s) existují konstanty segmentů v `top.v`, ale STM okno nikdy nemění.
Při nízkém kmitočtu (málo hran v segmentu, n < 8) se regrese zamítne a platí dvoubodový odhad [kód].

### 8.5 Vedlejší účinek: ztráta telescopingu (O1) [ÚVAHA]
Nahrazení `gate_ps := N/fr` (kap. 7.4) ruší vlastnost z kap. 5.2: součet náhradních délek **není** rozdíl dvou značek. Chyba okna je pak nezávislá (bílý šum frekvence) a při
dlouhém průměrování roste jako 1/√τ místo 1/τ. Odhad pro σ_f = 5,5·10⁻¹¹ na okno: ADEV(1000 s) ≈ 8,7·10⁻¹³ s regresí, ale ≈ 1,8·10⁻¹³ s plynulými dvoubodovými okny.
Pro **krátká τ** (≲ 10 s) vyhrává regrese výrazně, pro **dlouhá τ** mohou být lepší dvoubodová data. Reálný šum regrese ale může být až ~40× menší než 5,5·10⁻¹¹ (teoretický limit 1,4·10⁻¹²),
pak by regrese vyhrávala i ve 1000 s. **Neměřeno**; vyžaduje záznam obou variant.

---

## 9. Chybový rozpočet

| zdroj | velikost | povaha | důkaz |
|---|---|---|---|
| kvantizace tabulky | σ_q = 30 / 27 ps | deterministická podle fáze, dolní mez | [HW] z histogramu |
| statistika kalibrační tabulky (N_cal = 2²⁰) | ≲ 8 ps | systematická do další kalibrace | [ÚVAHA] |
| **celkový šum jedné značky** | **≈ 105 ps** (rozsah 80 – 160 ps podle estimátoru) | **bílý, nezávislý, skoro Gaussův** | [HW] kap. 10 |
| stabilní INL podle kódu | nenalezen | — | [HW] křížová validace |
| chybný počet hran | 1 hrana = 4·10⁻⁷ | skoková | `CITANI HRAN`: 0 za 300 s |
| nestabilita referenčních oscilátorů (OCXO × GPSDO) | neznámá, obsažena v ADEV 5,5·10⁻¹¹ | skutečný signál | — |
| vstupní hrana (strmost ~1 V/ns, šum napájení HC04) | podezření jako zdroj ~100 ps | náhodná | [HYP] |

**Dvoubodový odhad:** σ_f/f = √2·σ_m/G = 5,9·10⁻¹⁰ (σ_m = 105 ps), ADEV(G) = √3·σ_m/G = 7,3·10⁻¹⁰ (sousední okna sdílejí značku ⇒ Allanova variance 3σ_m²/G²).
**Regrese:** teoretický limit 1,4·10⁻¹², naměřená horní mez 5,4 – 6,5·10⁻¹¹ (kap. 10).

Konstanta `MP_TDC_PS` (`meas_present.h`) = **105 ps = σ značky**; řídí počet „důvěryhodných“ číslic velkého čísla (rozlišení okna √2·σ/G) a podlahu Allanova grafu
(displej `adev_floor_base` i web `floorOf`, ekvivalentní krok q = σ·√12: ADEV = q/(2τ), MDEV = q/(2τ√m), HDEV = 0,527·q/τ). S regresí je skutečný šum okna ≥ 8× menší,
konstanta je tedy **konzervativní**. (Do 2026-10-08 byla 57 ps jako krok tapu ze STA a obě použití ji chápala jinak — L-0139.)

---

## 10. Měření (kronika a co z ní plyne)

### 10.1 Neplatná měření (vstup pod prahem)
Do úpravy vstupu byla na pinu 25 jen úroveň **0 – 1,55 V** (74HC04 do 49R9; VIH = 2,0 V). Vstupní buffer se překlápěl šumem na vrcholu:
počet hran v okně byl o +0 až +257 000 jinak podle každého P&R (0,9 % u FW 0x0411, až 8,8 % u diagnostických buildů) a s hysterezí naopak −289 až −549.
**Všechna čísla z té doby (šum značky 215 ps, skoky ±9,28 ns, miscount 2 %, výsledky bitstreamů 0x0411–0x0417) neplatí** (L-0137). Po výměně 49R9 za 120 Ω:
obdélník 0 – 2,86 V, `CITANI HRAN` 0/0.

### 10.2 Platná měření (GPSDO → CH_A, vnitřní OCXO jako časová základna, δ = +813,5 ppb)
| měření | FW | výsledek |
|---|---|---|
| počet hran, 300 s, 1201 oken | 0x0412 | N přesně, `CITANI HRAN` 0/0, SEQ bez mezer; kmitočet 10 000 008,135 Hz |
| histogram kalibrace | 0x0412 | obsazených kódů 154/165, největší bin 198/157 ps, σ_q 30/27 ps |
| **INL a šum, 2048 oken** (`inl`, `tools/tdc_inl_fit.py`) | 0x0418 | σ okna 141 ps (MAD) / 156 ps (std); **autokorelace lag 1 = −0,47**, lag 2 = +0,11 ⇒ bílý šum značek; std/MAD = 1,1 (skoro Gauss), \|r\| > 600 ps jen 0,2 % |
| křížová validace INL | 0x0418 | fit e[kód] na 1. polovině, test na nevidené 2.: σ okna **141 → 141 ps** ⇒ žádný stabilní INL podle kódu |
| závislost na poloze hrany | 0x0418 | σ okna podle kódu konce: kódy 0–120: 170 – 203 ps, kódy 120–250: 124 – 133 ps (značka ≈ 135 vs 90 ps) |
| ADEV(0,25 s) dvoubodový | 0x0418 | 7,6·10⁻¹⁰ ⇒ σ_m = 110 ps |
| **regrese** (20 – 120 s; později 40 654 oken) | 0x041A | použito 100 %, zamítnuto 0; **ADEV(0,25 s) = 5,4 – 6,5·10⁻¹¹**; střed f_regr − f_2pt = 0,000 ppb, σ = 0,46 – 0,49 ppb |
| souvislost značek segmentu A | 0x041B (ladicí) | mezery 0, duplicity 0, odchylky intervalu 0 (312 499 značek) |

**Odhady σ jedné značky** (liší se metodou, ne chybou): 80 ps (z σ(f_regr − f_2pt) = 4,7·10⁻¹⁰ ⇒ okno 117 ps ⇒ /√2), 105 – 110 ps (okno 141 – 156 ps ⇒ /√2; ADEV 7,6·10⁻¹⁰ ⇒ /√3),
160 ps (krátký kvadratický fit nástroje `tdc_beat.py`, zahrnuje pomalý drift). Pro `MP_TDC_PS` zvoleno 105 ps.

**Srovnání zlepšení** — záleží na statistice: ADEV(0,25 s) 7,6·10⁻¹⁰ → 5,4 – 6,5·10⁻¹¹ je **≈ 12 – 14×**; směrodatná odchylka jednoho okna z dvoubodového odhadu (5,9·10⁻¹⁰, resp. 4,7·10⁻¹⁰ z přímého
rozdílu) proti ADEV regrese je **≈ 8 – 11×**. (Dřívější tvrzení „13×“ porovnávalo ADEV s korelovanými okny a bylo uvedeno bez této poznámky.) Naměřených 5,4 – 6,5·10⁻¹¹ je **horní mez šumu přístroje**:
obsahuje skutečný šum OCXO × GPSDO, který se z jednoho kanálu nedá oddělit (O4).

### 10.3 Fáze v experimentu (kontrola)
δ = 813 ppb ⇒ fáze se posune o **0,0813 ps na hranu**, **25,4 ns během segmentu** (2,5 periody hodin), 152 ns mezi středy segmentů A a B, 203 ns (20,3 periody) za okno.
Úplný cyklus hodin projde za ~123 000 hran. [ÚVAHA, shoduje se s měřeným posunem fáze 3,4 ns/okno z `tdc_beat.py` = zlomek 0,34 periody]

---

## 11. Zdroje, časování a ověření

### 11.1 Prvky TDC
**Jeden kanál (A i B totožné; `TDC_TAPS = 320`, `STRIDE = 1`, `KD = 1`, `DUAL = 0`)**

| prvek | počet | poznámka |
|---|---|---|
| ALU v řetězu | **320** | použitelných ≈ 252 [HW] |
| vzorkovací FF `q_r` | **320** | volně běží každý takt, `keep` |
| zmrazitelná kopie `qd_r` | **320** | clock enable |
| detekce hrany `dR`, `dRp`, `ec_r` | 3 FF | |
| řízení spuštění `idle_r`, `arm_r`, `fz[2:0]` | 5 FF | |
| dekodér | 10 × AND32, výběr bloku, mux 32:1, prioritní kodér | skupiny `ld_c`+`pg_c`+`code` ≈ 830 LUT na oba kanály |
| `code_r`, `hicode_r` | 18 FF | |
| histogram / kalibrační tabulka | 512 × 21 b / 512 × 15 b (BRAM) | |
| řízení kalibrace | `cnt` 21 b, `cum` 22 b, `xv`, `hcur`, `lutv` | one-hot `s_clr/s_col/s_drn/s_cmp` |
| časová značka | 48 FF + odčítačka | |

Sdílené: jeden ring oscilátor (13 LUT + dělič), `tick_hi` 34 b, `gtmr` 24 b, SPI rámce v BRAM. **Regresní blok** (jen CH_A): ≈ 750 FF (odhad z RTL: akumulátory 290, dělič 235, výstupy 226), součty 48 b a 2 × 32 b.

### 11.2 Celý návrh — FW 0x041A, P&R (Gowin V1.9.12, `gw_sh build.tcl`)
| zdroj | využito | z | % |
|---|---|---|---|
| Logic (LUT/ALU) | 4 359 | 8 640 | 51 % |
| Register | 3 841 | 6 693 | 58 % |
| **CLS** | **3 327** | 4 320 | **78 %** |
| BSRAM | 5 | 26 | 20 % |
| DSP | 6 | 10 | 60 % |
| ALU v netlistu | 1 625 | — | z toho 640 = řetězy TDC |
| největší jednotlivé skupiny FF | `q_r` 640, `qd_r` 640 | | |

Časování: `clk_p0_100m` Fmax **101,4 MHz** *po* odečtení nejistoty 0,5 ns ⇒ reálná rezerva ≈ 0,64 ns, TNS 0, hold 0.
Srovnání verzí: 0x0412 (regrese vypnuta) CLS 65 %, 0x0410 68 %, **regrese stojí ~11–13 procentních bodů CLS**, což komplikuje plánovaný třetí TDC kanál pro 1PPS (#262).

### 11.3 Ověření
- **Simulace** (`sim/run.ps1`, Icarus): coarse, gatediv, tdc, tdcinl, tdcinl_dual, regre2e (zisk 14× při zkráceném hradle), regracc, **regrfull** (60 000 značek, plná šířka čísel), **topregr** (celý `top.v`, `-DSIM_TOP`, hradlo 2,5 ms), decequiv256/320, txequiv, phase, link — vše PASS.
  Simulace **nevidí časování** (L-0133, L-0138) ani šum reálného obvodu.
- **Netlist:** `sim/check_tdc_netlist.py` (2 × 320 ALU stupňů s průchodem carry).
- **STM:** `./scripts/build.sh Release BOTH` 0 varování; `tools/audit.py` 92 OK / 0 selhání / 2 soubory s varováním; `tools/spa/check.py --build` OK;
  `selftest` (`fpga_freq_select_selftest`: vektory regrese vč. zamítnutí 1·10⁻⁷, hysteréze, ticky okna, miscount; na desce PASS po nahrání v0.14.0+).
- **HW diagnostika:** UART `status`, `tdc`, `tdc hist`, `tdc cal`, `regr`, `regr reset`, `fpgaraw`; paměť přes SWD (statiky `s_rg_*`, `s_last`).

---

## 12. Otevřené body a otázky pro revizora

### Otevřené body
| č. | bod | stav |
|---|---|---|
| **O1** | `gate_ps := N/f_regr` ruší telescoping (kap. 5.2, 8.5); dlouhá τ mohou být horší než u plynulých dvoubodových oken. Rozhodnout: nechat, nebo posílat obojí. | neměřeno |
| **O2** | Nejistota hodin 0,5 ns je empirická. Odvodit z jitteru Si5356 + vstupního bufferu, případně potvrdit dalšími P&R rozmístěními (seedy). | otevřeno |
| **O3** | Kalibrace není stabilní (největší bin 311 vs 198 ps) — rovnoměrnost fáze ring oscilátoru vůči hodinám neověřena. Může zhoršovat INL a šum. | otevřeno |
| **O4** | Zdroj ~105 ps šumu značky neznámý (vstupní hrana / napájení HC04 / jitter hodin / TDC). Experiment: tentýž signál na CH_A i CH_B (rozdíl `dt_a − dt_b` = čistý šum TDC), osciloskop na pinu 25 (strmost, šum). | čeká na zapojení |
| **O5** | Z 320 tapů se využije ~252; zkrácení (např. 288) by uvolnilo ~10 % plochy TDC, ale teplotní rezerva (zimní provoz = rychlejší řetěz) není vyšetřena. | otevřeno |
| **O6** | Regrese jen pro CH_A; CH_B by stál +~11–13 % CLS. | otevřeno |
| **O7** | Zápis bitstreamu do flash FPGA hlásil „Verify Failed“, po novém nahrání konfigurace z flash ale běží 0x041A. Ověřit power-cyklem. | ověřit |
| **O8** | Web a displej nevědí, že je aktivní regrese (IPC bez příznaku), `MP_TDC_PS` je proto jedna konstanta (105 ps). | otevřeno |
| **O9** | Netestováno: hradlo 100 ms / 1 s, jiné kmitočty než 10 MHz, vstup blízký 30 MHz. | otevřeno |
| **O10** | Zamčený stav (δ → 0): argument o vykrácení společné chyby (kap. 8.1b) nebyl měřen. | čeká na GPSDO disciplinaci OCXO (DAC AD5693R) |
| **O11** | `miscount` hlídá jen ±1…2 hrany; větší chyba počtu projde. | známé omezení |
| **O12** | Konzole USB CDC je po resetu a při dlouhých výpisech nespolehlivá (diagnostika se čte přes SWD). | otevřeno |
| **O13** | Záznamník `inl` (kódy TDC v rámci) funguje jen s REGR vypnutým (caps bit8). | záměr |

### Otázky pro revizora
1. Je odvození kap. 8.1 (včetně vykrácení deterministické chyby a argumentu o ditheru) správné? Co by ho vyvrátilo?
2. Je v pořádku, že akceptační mez regrese (5·10⁻⁸, kap. 7.4) je pevná? Nemá se odvodit z běžícího rozptylu rozdílu f_regr − f_2pt?
3. Má se nahrazovat `gate_ps` (O1), nebo má STM udržovat dvě řady (plynulá dvoubodová pro dlouhá τ, regresní pro krátká)?
4. Je rozpočet z kap. 9 úplný? Co chybí (teplota, napájení FPGA, přeslechy mezi kanály, závislost kalibrace na teplotě)?
5. Je volba KD = 1 (detekce hrany z tapu 1) bezpečná z hlediska metastability a spouštění? Alternativa: KD = 0 nebo samostatný vzorek.
6. Jsou předpoklady o mrtvé době (5 taktů) a o rychlosti značek v kap. 4.5 dostatečné pro vstupy nad 10 MHz?
7. Je `set_clock_uncertainty 0,5 ns` rozumné, nebo maskuje problém, který je třeba řešit jinak (např. omezením fan-outu hodin)?
8. Má smysl použít sestupnou hranu hodin (`DUAL`) pro jemnější biny — za cenu CLS 87 % — pokud je dominantní náhodný šum, ne kvantizace?

---

## 13. Příloha

### 13.1 Vzorce rychle
```
u = 10000/16384 ps = 0,6103515625 ps         ps = u·625/1024
ts = tick − lut[k];   lut[k] = 16384 + 16384·(cum(k) + hist[k]/2)/2²⁰
N = count + 1;   Δt = ts_k − ts_(k−1)         f = N·1e12 / (Δt·625/1024) Hz
σ_q² = Σ w_k³/(12 T)                          σ_f/f (2 body) = √2·σ_m/G        ADEV_2pt(G) = √3·σ_m/G
f_regr = (x̄_B − x̄_A)/(ȳ_B − ȳ_A);   σ_f/f (regrese, bílý šum) = √(2/n)·σ_m/(0,75·G)
Δf při chybě 1 hrany = f/N = 1/G
fáze na hranu = (1/f)·δ;   za segment (G/8) = δ·G/8;  za okno = δ·G
```

### 13.2 Soubory
| soubor | obsah |
|---|---|
| `Frequency_Counter_FPGA_Module/src/tdc.v` | `tdc_chain`, `tdc_dec`, `tdc_chan`, `ring_osc`, `edge_diag` (nepoužito) |
| `…/src/spi_app.v` | rámec (TX mux), `gate_div`, `regr_acc`, `win_recip` |
| `…/src/top.v` | propojení, segmenty (`gtmr`), kalibrace po startu, parametry `TDC_*`, `SIM_TOP` |
| `…/src/timing.sdc` | hodiny, multicycle, nejistota hodin |
| `…/sim/*.sv`, `run.ps1` | testbenche; `tb_tdc_inl.sv` obsahuje model křemíku |
| `CM7/Core/Src/fpga_freq.c`, `fpga_freq.h` | parser, výpočty, regrese, `inl` záznamník |
| `CM7/Core/Src/freertos_task_fpga.c` | FpgaTask, spike-reject, rekalibrace |
| `CM7/Core/Inc/meas_present.h` | `MP_TDC_PS` |
| `tools/tdc_beat.py`, `tdc_inl_fit.py`, `tdc_hist_analysis.py` | analýza měření |

### 13.3 Jak měření zopakovat
```
status                                  # FW 0x041A, CAPS 0x00E2, REGR:pouzita, CITANI HRAN 0/0
tdc ; tdc hist                          # histogram kalibrace  →  python tools/tdc_hist_analysis.py hist.txt
regr ; regr reset                       # počty oken, f_regr − f_2pt (střed, sigma)
python tools/tdc_beat.py collect --minutes 5 --out beat.csv ; analyze beat.csv      # přes SSE /api/stream
python tools/tdc_inl_fit.py inl.txt     # INL; vyžaduje FW s kódy v rámci (REGR vypnuto) a UART `inl dump`
```
SWD: paměť STM se čte `STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -u <adresa> <velikost> <soubor>` (adresy z `CM7/Release/H757_LED_CM7.map`); po čtení STM resetovat (halt rozbíjí I2C4).
FPGA: `programmer_cli -d GW1NR-9C --operation_index 2 --fsFile <soubor>` (SRAM); tentýž příkaz s `--operation_index 6` zapisuje do flash.

### 13.4 Změny proti verzi 1.x tohoto dokumentu
- Přepsáno do jednoho souvislého textu; kap. „Regrese“ původně tvrdila „ve FW 0x0412 vypnutá“ — platí FW 0x041A, regrese je **zapnutá a ověřená**.
- **Opraveno:** fáze v segmentu při δ = 813 ppb je **25,4 ns** (2,5 periody), ne 25 ps; tvrzení „zisk regrese jen pro signál nesoudělný se 100 MHz“ nahrazeno úvahou kap. 8.1 (náhodný šum se průměruje i při zamčeném signálu, deterministická chyba se vykrátí/zprůměruje).
- **Upřesněno:** zlepšení „13×“ je poměr ADEV(0,25 s); pro směrodatnou odchylku jednoho okna je to ≈ 8 – 11×.
- **Doplněno:** telescoping (5.2, 8.5), mrtvá doba (4.5), akceptační mez regrese (7.4), chybový rozpočet (9), otevřené body a otázky (12).
- Odstraněno: čísla šumu z doby před opravou vstupu (215 ps, ±9,28 ns, miscount 2 %) — jsou označena jako neplatná (10.1).
