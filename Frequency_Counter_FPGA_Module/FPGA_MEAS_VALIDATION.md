# Validace měřicích metod FPGA čítače — chyby, drift, kompenzace, stabilita, rozmístění logiky

Podklad pro návrh programu FPGA modulu (Gowin GW1NR-9C, Tang Nano 9K na carrier v2.0).
Validováno proti RTL v repu (FW 0x0201: `src/top.v`, `src/spi_app.v`, `src/spi_slave_phy.v`,
`src/timing.sdc`, `src/pins.cst`) a proti `PHASE_CAL_DESIGN.md` + `BOARD_V20_CHECKLIST.md`.
Datum revize: 2026-07-19.

---

## 1. Architektura měření — co se validuje

```
RF vstup ─► MAX9601 (tvarovač) ─► MC100EP016 ÷4/÷16 ─► MC100EPT23 ─► pin 28 (f/4), pin 27 (f/16)
                                                                        │
OCXO 10 MHz (GPSDO) ─► Si5356A ─► 4×100 MHz, 0/90/180/270° ─► piny 35/34/40/33
                     └─► LMK1C1104 ─► pin 63 (clk_ref_10m: gate, SPI, výpočet)
```

Metoda: **4fázové oversamplované reciproké čítání** s rozlišením značky 2,5 ns
(`phase_oversampler` → `win_recip` → `recip_calc`), gap-free okna 0,1/0,25/1 s,
navíc **Λ/Ω regresní akumulátory** (S1=Σt, S2=ΣS1) pro LSQ výpočet směrnice ve
STM32 a **trvalý histogram fine kódů** (background tracking šířek binů).

Klíčový princip, který validace potvrzuje jako správný: *měřítko času je výhradně
100MHz doména odvozená ze Si5356/OCXO*. 10MHz doména jen otvírá/zavírá okno a
počítá — u reciprokého čítače na přesnosti délky okna **nezáleží** (Δt se měří
časovými značkami, ne gate časovačem). Návrh je v tomto konzistentní. ✅

---

## 2. Rozpočet chyb — kvantifikace

Vztažné hodnoty: bin q = 2,5 ns, okno τ = 0,25 s (default), hrany na pinu 28
max 40 MHz (debounce strop), tedy N ≤ 10⁷ hran/okno.

| # | Zdroj chyby | Velikost | Škálování | Kompenzace / poznámka |
|---|---|---|---|---|
| 1 | **Kvantizace Δt** (2 krajní hrany) | worst ±q/τ = ±1×10⁻⁸; rms q/(√6·τ) ≈ 4×10⁻⁹ | ∝ 1/τ | Λ regrese: σ ≈ q/(√N·τ) → **1×10⁻¹¹ @ N=10⁶** (platí jen pro nesoudělné vstupy, viz §4) |
| 2 | **Nelinearita binů** (skew fází 34/40/33 vs. GCLK 35) | očekávání 100–500 ps staticky | systematická, PVT drift | histogram fine kódů (živě, každé okno) → LUT korekce c[fine] ve STM; reziduum σ_b ≈ 2 ps @ N=10⁶ |
| 3 | **Jitter Si5356** (aditivní, ×10 z 10 MHz) | ~1 ps rms | bílý → průměruje se | pod binem o 3 řády; relevantní až pro carry-chain TDC (stupeň 3), i tam OK |
| 4 | **Trigger jitter front-endu** (MAX9601 + EP016 + EPT23) | jednotky ps rms (ECL) | bílý → 1/√N | pod rozlišením; závisí na strmosti vstupu — systémová věc, ne FPGA |
| 5 | **Teplotní drift binů** (insertion delay fabric) | ~desítky ps / 10 °C | pomalý | background tracking histogramem = kompenzováno průběžně; uvnitř jednoho okna zanedbatelné (ps/min) |
| 6 | **Metastabilita vzorkovačů** | 2FF synchronizér/fáze; MTBF @ 100 MHz ≫ roky | — | chybný fine kód max ±1 bin ojediněle; debounce navíc zahodí zákmity |
| 7 | **Reference (GPSDO)** | ADEV OCXO ~1e-11..1e-12 @ 1 s | — | **limit celého měření**; FPGA příspěvek (ř. 1–6 po kompenzaci) je pod ním |
| 8 | Přesnost délky okna (10MHz gate) | nulový vliv na f | — | reciproká metoda; okno jen volí τ |
| 9 | Zaokrouhlení `recip_calc` | ±½ LSB = ±5 µHz | — | `num += dt/2` = korektní round-half ✅ (spi_app.v:515) |

**Závěr rozpočtu:** po LUT korekci + Λ regresi je elektronika (FPGA + front-end)
na úrovni ~1×10⁻¹¹ @ 0,25 s a limitem je reference. Čísla v `PHASE_CAL_DESIGN.md`
(1,4×10⁻⁸ surové okno; ~1×10⁻¹¹ s regresí) **sedí** — 1,4×10⁻⁸ je konzervativní
worst-case (±bin na každé hraně), rms je ~3× lepší. ✅

### Propagace do výstupních polí protokolu

- `freq_x100000` (u64, LSB 10 µHz): dost rozlišení i pro regresní zpřesnění — výpočet
  směrnice ale dělá STM z S1/S2/edges/dt, FPGA hodnota je „hrubý" reciproký výsledek.
- Konstanty `CONST` 1,6e14 (/4) a 6,4e14 (/16) ověřeny: f×10⁵ = periods·PRESC·10⁵/(dt·2,5 ns). ✅
  ⚠️ Platí **jen** pokud osazené tapy EP016 skutečně dávají ÷4/÷16 (BOARD_V20 §5 — ověřit při bring-upu).

---

## 3. Validace ochranných mechanismů v RTL (co už je ošetřeno)

| Mechanismus | Kde | Verdikt |
|---|---|---|
| Debounce 1 hrana/perioda (plně LOW mezihrání) | `phase_oversampler.state` | ✅ strop ~40 MHz na pinu dokumentován; odolné vůči ringingu |
| Saturace histogramu (1s okno × >16,7 M hran) | top.v:158–165 | ✅ |
| Δt overflow bit33 (21,5–42,9 s) → error_flags bit2 | top.v:225 | ✅ |
| **Δt alias >42,9 s** (win_age sticky) → bit3, STM zahodí | win_recip:622–626 | ✅ (kritický nález 2026-07-10, správně opraven) |
| Dělení nulou (dt==0) → err, publikuje 0 | recip_calc:508 | ✅ |
| Kolize start při běžícím dělení → měření zahozeno | recip_calc:506 `pending` | ✅ (8 µs ≪ okno, prakticky nenastane) |
| Watchdog ztráty signálu ~2,5 s → signal_lost | top.v:241–253 | ✅ |
| CDC: toggle-handshake res_tgl/ph_tgl, 2FF/3FF sync, MCP kotva před DSP | top.v, recip_calc:478 | ✅ data stabilní celé okno; SDC clock groups párově |
| Šířky akumulátorů: S1 56 b, S2 80 b | win_recip | ✅ worst case (1s okno, 40 MHz): S1 ≈ 2^54, S2 ≈ 2^77 — rezerva ≥ 4× |
| Gap-free okna (uzavírací hrana = první hrana dalšího okna) | win_recip:665 | ✅ STM může Σedges/Σdt bez mrtvé doby |
| `div_res` pin 53 trvale LOW (MR preskaleru) | top.v:42 | ✅ kritické pro v2.0 (jinak čítač mrtvý) |
| SPI: CRC16, tx_valid drží poslední kompletní rámec, ARMED bez závislosti na cs_fall | spi_slave_phy | ✅ audity V3/V4 zapracované |

**Nalezený rozpor k vyřešení (jediný tvrdý nález):**
`STATUS.md` (sdílený kontrakt) uvádí „SCK cíl ≤6 MHz (max ~10 MHz)", ale PHY
oversampluje 10 MHz hodinami → spolehlivé je **SCK ≤ ~2 MHz** (spi_slave_phy.v:20).
Dokud se PHY nepřepne na `clk_p0_100m` (plán v PHASE_CAL §6.5 / checklist §6 pro
flash-bridge), musí STM držet SCK ≤ 2 MHz. Rámec 128 B @ 2 MHz = ~0,5 ms — pro
4 Hz polling bohatě stačí, jde jen o sladění dokumentace/konfigurace STM. 🔴

---

## 4. Limitace Λ/Ω regrese: koherentní (soudělný) vstup — doplnit do návrhu

Tvrzení „kvantizace se průměruje ~1/√N" předpokládá, že se hrany vstupu vůči
100MHz mřížce **rovnoměrně posouvají** (nesoudělné kmitočty). To je splněno pro
libovolný „cizí" signál, ale **typický use-case čítače je měření 10MHz zdrojů
téměř synchronních s referencí** — pak hrany na pinu 28 (2,5 MHz) padají stále
do téhož místa binu a putují jen rychlostí rozladění δf:

- Kvantizační chyba se stává **deterministickou pilou** s amplitudou až ±q/τ
  (±1×10⁻⁸ @ 0,25 s; ±2,5×10⁻⁹ @ 1 s) a periodou beatu T_b ≈ q/(τ·δf/f)…
  tj. u δf/f = 1×10⁻¹⁰ řádově stovky sekund.
- **Regrese ani histogram tady nepomáhají** (všechny body sdílejí týž offset;
  konstantní offset se v směrnici vyruší, ale pomalá schodovitá změna ne).
- Vůči Allanově deviaci se to projeví jako artefakt ~1e-8 @ krátká τ, klesající
  s průměrováním přes celé beat periody.

**Doporučení do FW/STM (nová položka návrhu):**
1. STM při vyhodnocení stability průměruje přes ≥1 beat periodu (detekovatelné:
   `fine_first/fine_last` se dlouhodobě nemění → koherentní režim; lze indikovat).
2. Okno 1 s pro srovnávání 10MHz normálů (snižuje pilu 4×).
3. Definitivní řešení = **stupeň 3 (carry-chain TDC)**: q klesne na ~50–100 ps →
   pila pod 4×10⁻¹⁰ i v koherentním případě. (Levný mezikrok: DDR na 45° fázích
   §7.4 pilu půlí.)
4. (Volitelně prozkoumat: záměrný malý frakční offset fází Si5356 měněný mezi
   okny = dither mřížky; zatím jen poznámka, nezasahovat do hodin bez měření.)

Tato limitace není chyba implementace — je vlastní každému čítači s pevnou
mřížkou — ale musí být v návrhu programu explicitně, jinak si ji bring-up splete
s vadou.

---

## 5. Drift a kompenzace — matice mechanismů

| Drift | Časová konstanta | Kompenzuje | Stav |
|---|---|---|---|
| Skew fázových stromů (PVT) | minuty–hodiny (teplota), měsíce (stárnutí) | background histogram → LUT ve STM; re-cal trigger ΔT>5 °C z TMP117 | RTL hotové (hist + CAL 0xA0); STM část = TODO #3 |
| Šířky binů při ztrátě signálu | — | ring oscilátor (SET_CONFIG 0x02) místo pin28 | ✅ implementováno; RO nesoudělnost = správný princip |
| OCXO vs. GPS | s–dny | GPSDO smyčka (STM32, mimo FPGA) | běží |
| Teplota uvnitř okna | ps/min | zanedbatelné vůči q; Λ regrese navíc průměruje | ✅ neřešit |
| IODELAY tapy | PVT ±20 % | — (proto **deskew zrušen**, PHASE_CAL §0b bod 3) | ✅ správné rozhodnutí, nechat zrušené |

Zásada, kterou validace potvrzuje: **neopravovat hodiny, opravovat čísla.**
Všechny kompenzace jsou numerické (LUT, regrese) na základě živě měřené pravdy
(histogram), žádný zásah do hodinových cest za běhu. To je teplotně nejrobustnější
možná architektura na tomto křemíku.

---

## 6. Očekávaná stabilita (pro verifikaci při bring-upu)

Predikce ADEV příspěvku elektroniky (bez reference), nesoudělný vstup:

| τ | surový reciproký | s Λ regresí (N=f_pin·τ, f_pin=2,5 MHz) |
|---|---|---|
| 0,25 s | ~4×10⁻⁹ | ~1,3×10⁻¹¹ |
| 1 s | ~1×10⁻⁹ | ~3×10⁻¹² |
| 10 s (Σ oken) | ~1×10⁻¹⁰ | <1×10⁻¹² |

Měřítko úspěchu bring-upu: proti společné referenci (Si5356 výstup zpět do
vstupu přes dělič) musí ADEV klesat ~1/τ a sedět s tabulkou; koherentní artefakt
(§4) se pozná podle pily s periodou beatu. Nad to už měří GPSDO, ne FPGA.

---

## 7. Rozmístění logiky v GW1NR-9C — pravidla pro optimální časy

### 7.1 Hodinové zdroje (fyzická fakta, ověřeno pin reportem)

| Signál | Pin | IOB pozice | Síť v čipu |
|---|---|---|---|
| clk_p0_100m (0°, **master**) | 35 | IOB29[A] = **GCLKT_4** | globální strom (GCLK) — nízký skew |
| clk_p2p5_100m (90°) | 34 | IOB23[B] | **fabric routing** do clock pinů FF |
| clk_p5_100m (180°) | 40 | IOB33[B] | fabric routing |
| clk_p7p5_100m (270°) | 33 | IOB23[A] | fabric routing |
| clk_ref_10m | 63 | IOR5[A] (RPLL_T_in) | GCLK/fabric — nekritické (10 MHz) |
| sig_in4 / sig_in16 | 28 / 27 | IOB11[B] / IOB11[A] | data |

Vše kritické je na **spodní hraně čipu** (IOB11…IOB33) — to je velká výhoda:
celý časově kritický ostrov může ležet v pásu spodních řádků.

Důsledky:
- Jen p0 má garantovaný nízkoskewový strom. Fáze 90/180/270 ponesou insertion
  delay fabric routingu → **statický skew stovky ps až ~1 ns je očekávaný a
  přípustný** — nekazí monotónnost, jen posouvá hranice binů, které histogram
  změří. Do STA je kryjí waveform posuny v SDC. Neopravovat, měřit.
- **Nepoužívat rPLL** pro generování fází (jitter ~100+ ps, a hlavně by zničil
  vazbu na Si5356/OCXO). rPLL nechat jako rezervu pro případný reclock SPI PHY.

### 7.2 Placement pravidla po blocích

| Blok | Doména | Pravidlo rozmístění | Proč |
|---|---|---|---|
| `u_os4` 1. stupeň (s0a..s3a) | 4 fáze | **1 CLS / těsný cluster u IOB11** (pin 28); krátká, vyvážená data cesta z pinu | rozdíl data zpoždění mezi 4 FF jde PŘÍMO do hranic binů; společné zpoždění se ruší |
| `u_os4` 2. stupeň (s0..s3) + `os_r1` | 4 fáze → p0 | hned vedle 1. stupně; **hlídat slack cesty s3→os_r1 (rozpočet 2,5 ns)** — nejtěsnější cesta designu | cross-fázový přenos; při violaci INS_LOC sousedství |
| `win_recip` | p0 | nechat PnR, pipeline už je nařezaná (34b sub → 56b add → 40+40b add s carry FF) | jednotaktové verze měly slack −2,5/−1,6 ns → **nerušit pipeline** při úpravách |
| histogram + phase_check | p0 | volně u oversampleru | nekritické (čítače) |
| `ring_osc` | vlastní | LUT primitivy s keep ✅; **umístit dál od IOB11/23/29 pásma** (GRP na opačný konec spodní banky / do středu) | omezit vazbu spínacího hluku RO do vzorkovačů; RO běží jen v cal módu, ale hygiena zdarma |
| `recip_calc` + `divu_seq` + `spi_app` + PHY | 10 MHz | kdekoli, bez omezení | 100 ns perioda; jen zachovat MCP kotvu periods_q/dt_q (recip_calc:483 — komentář v kódu vysvětluje proč, DSP packing!) |
| budoucí carry-chain TDC (stupeň 3) | p0 | **jeden souvislý ALU/CLS sloupec** (Gowin řetězí vertikálně), LOC jen hlavy řetězu, vstup z IOB11 nejkratší cestou; zachytávací FF banka v sousedních sloupcích | monotónnost tapů; potvrzuje PHASE_CAL §5 |

### 7.3 Konkrétní nástroje (Gowin CST/SDC)

```tcl
# CST — až bude potřeba (dnes design timing plní; použít při violaci s3→os_r1
# nebo pro stabilitu binů mezi buildy). Přesné buňky vzít z floorplanneru /
# PnR reportu, jména post-syntézních FF mají suffix _s0:
# INS_LOC "u_os4/s3a_s0" R28C30[0][A];   // příklad — cluster u IOB11
# GROUP/GRP_LOC pro ring_osc mimo pásmo vzorkovačů.
```

- **Reprodukovatelnost binů mezi buildy:** každá změna PnR přemístí vzorkovače →
  jiné hranice binů → **po každém flashi nového bitstreamu re-kalibrovat LUT**
  (automatické: background histogram to srovná za 1 okno). Pokud se ukáže
  rozptyl mezi buildy nepřijatelný pro srovnávací měření, teprve pak zafixovat
  INS_LOC cluster oversampleru — dřív ne (zbytečná údržba).
- **STA disciplína:** po každém buildu zkontrolovat v PnR reportu (a) 0 violations,
  (b) že cesty `u_os*/s1..s3 → os_r1` jsou timované na 7,5/5/2,5 ns (waveform
  skupiny fungují — SDC je řeší párovými `set_clock_groups`, protože vícecestná
  skupina nebyla Gowin STA respektována, timing.sdc:23), (c) clk_p0 skutečně na
  GCLKT_4 (pin report).
- **Syntéza:** nezapínat retiming přes synchronizéry; `(* keep *)` na RO už je
  přes primitivy. Při refaktoru nedovolit sloučení s0a..s3a (různé hodiny — Gowin
  je nesloučí, ale dávat pozor při případném přepisu na generate).

### 7.4 Cesta na 1,25 ns: DDR vzorkování na 45° fázích („stupeň 1,5") — analýza 2026-07-19

Otázka: dá se zdvojnásobit rozlišení na 1,25 ns vzorkováním obou hran (DDR)?

**Na dnešních 90° fázích NE.** Sestupné hrany 0/90/180/270° (při duty 50 %) padají
na 5/7,5/10/12,5 ns = přesně na náběžné hrany protilehlých fází → 8 vzorkovacích
okamžiků, ale po dvojicích totožných. Reálné duty ~48–52 % z nich udělá dvojice
vzdálené jen ±0,2 ns → biny 0,2/2,3 ns, efektivní kvantizace zůstane ~2,3 ns.
„Zadarmo obě hrany" tady nefunguje.

**Funguje to s přerozestupením fází na 45°** (Si5356 offsety 0/1,25/2,5/3,75 ns =
0/352/704/1056 LSB — jen jiná čísla v CBPro exportu, **žádná změna desky, tras ani
pinů**). Náběžné hrany dají 0/1,25/2,5/3,75, sestupné 5/6,25/7,5/8,75 → uniformní
mřížka 8×1,25 ns. Chyba duty posouvá sestupnou čtveřici jako blok o (duty−50 %)·10 ns:
mění šířku jen 2 hraničních binů, monotónnost drží do |δ|<1,25 ns (Si5356 dává
±0,2 ns) — a celé to spadá do stávající kompenzace histogram→LUT, duty je jen další
pomalý parametr, který background tracking stopuje.

Překvapivý bonus — **STA se UVOLNÍ**: dnes je nejtěsnější cesta s3(7,5 ns)→os_r1(p0)
s rozpočtem 2,5 ns. Fáze 45° leží jen 0–3,75 ns za p0, takže náběžná skupina má do
zachycení v p0 rozpočty 6,25–10 ns; sestupné vzorky se nejdřív přeregistrují na
náběžné hraně vlastní fáze (dekodér si vede offset −1 rámec) → rozpočty ≥6,25 ns.
Žádný MCP trik, žádná těsná cesta.

Náklady (jen RTL + Si konfigurace + protokol):
- 8 synchronizačních řetězů (negedge FF ve fabric — Gowin umí), dekodér 8→3 b,
  `fine` 3 b, `ev_ts` 35 b, histogram 8×24 b (FF je dost, 54 % volných);
- `win_recip`: šířky +1 bit (S1 2^55 < 2^56, S2 ~2^78,5 < 2^80 — rezerva ~2×, ověřeno);
- `recip_calc`: CONST ×2 (3,2e14 / 1,28e15), divisor 35 b, gate_ns = dt·1,25 ns;
- protokol: fine kódy 3+3 b, CAL report 8 histogramů (vejde se do rezervy) → bump
  FW/caps; STM: 8-položková LUT + nové Si offsety.
- Debounce strop zůstává ~40 MHz na pinu (plně LOW rámec je pořád ~10 ns).

Zisk: q 2,5→1,25 ns = surové okno 2× (worst ±5×10⁻⁹ @ 0,25 s), regrese
σ ≈ 6×10⁻¹² @ N=6×10⁵, **koherentní pila (§4) klesne na polovinu** (±5×10⁻⁹ @ 0,25 s).

Proč NE varianty s IDES: Si5356 má strop výstupu **200 MHz** → IDES8 s FCLK 400 MHz
z reference nejde; IDES4 @ 200 MHz DDR dává zase jen 2,5 ns (přínos je jediný strom,
ne rozlišení, viz §7.5). FCLK 400 MHz z rPLL by šel, ale jitter rPLL ~100 ps vstupuje
do každé značky — částečně se průměruje (a mimochodem by fungoval jako přirozený
dither koherentní pily), je to ale spekulativní větev: neřešit před změřením.

**Doporučení:** zařadit jako volitelný stupeň 1,5 po oživení v2.0 a zvládnutém
stupni 0/1 — je to nejlevnější zdvojnásobení přesnosti (žádný HW zásah, STA se
zlepší, kompenzační infrastruktura beze změny, jen víc binů). Nesmí ale odsunout
stupeň 3 (carry-chain, zisk 25–50×) — pokud se půjde rovnou do něj, stupeň 1,5
přeskočit.

### 7.5 Výhled IDES4/IDES8 (FW 0x03xx) — potvrzeno jako správný směr

Vzorkování jedním 200MHz DDR clockem v IOB (4 vzorky/10 ns z jediné hodinové
cesty) odstraní celý problém skew čtyř stromů: teplota se stane common-mode,
uvolní piny 34/40/33 a placement 1. stupně přestane existovat (je v IOB).
Podmínka: Si5356 pak musí dávat 200 MHz na GCLKT_4. Držet jako plán po oživení
v2.0 — infrastruktura (histogram, RO, CAL rámec, LUT) se použije beze změny.

---

## 8. Checklist verifikace pro návrh programu

1. **Sim:** rozšířit `tb_phase_oversampler` o stimulus se známým rozložením hran →
   kontrola histogramu; testbench na Λ akumulátory (S1/S2 vs. zlaté LSQ v SV).
2. **Bring-up hodin:** scope 4×100 MHz s rozestupy 2,5 ns (35/34/40/33), 10 MHz (63);
   `phase_status` present=0xF, fine_seen=0xF.
3. **Histogram:** RO mód → 4 biny ~25 % ±(očekávaný skew); zapsat naměřené šířky
   binů do dokumentace (baseline pro trend).
4. **ADEV:** self-test smyčkou (referenční 100 MHz/2^k do vstupu) proti tabulce §6;
   identifikovat koherentní pilu §4 a ověřit, že průměrování přes beat ji maže.
5. **Teplotní test:** fén/chladič na Tang Nano, sledovat pohyb hranic binů
   (CAL 0xA0) vs. TMP117 — potvrdit, že LUT korekce drží reziduum < 10 ps.
6. **SPI:** do sladění kontraktu držet SCK ≤ 2 MHz (nález §3). Po přepnutí PHY na
   100 MHz re-test na 6–10 MHz.
7. Po **každém** novém bitstreamu: pin report (GCLKT_4), timing report (0 viol.,
   cross-fázové cesty), FW_VERSION bump, re-check histogramu (nové PnR = nové biny).

## 9. Souhrn verdiktu

- **Metoda je zdravá a správně vrstvená**: reciproké čítání (nezávislé na přesnosti
  gate) + fine interpolace 2,5 ns + numerická kompenzace z živého histogramu +
  Λ regrese. Chybové mechanismy jsou identifikované a až na referenci všechny
  potlačené pod 1×10⁻¹¹ @ 0,25 s.
- **Ochrany v RTL jsou kompletní** (alias, saturace, overflow, CDC, debounce) —
  žádná další záplata není potřeba.
- **K doplnění do návrhu programu:** (a) koherentní limitace §4 + její obsluha ve
  STM, (b) sladit SCK limit 2 MHz (jediný tvrdý rozpor), (c) placement zásady §7.2
  jako komentáře/CST šablona pro budoucí úpravy, (d) re-kalibrace po každém buildu.
- **Nedělat:** fabric delay línky, IODELAY deskew, rPLL fáze, vernier TDC —
  všechna čtyři rozhodnutí o zrušení jsou validací potvrzena jako správná.
