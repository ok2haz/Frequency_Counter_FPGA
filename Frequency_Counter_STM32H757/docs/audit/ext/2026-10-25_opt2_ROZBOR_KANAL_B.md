# Rozbor: co může být s kanálem B (FPGA v3, FW 0x0507)

Zpracováno 2026-10-25 na žádost uživatele během optimalizace. **Konkrétní příznak chyby kanálu B neznám.** Repo ani měření
z desky nemám, jen snímek `base/` a v něm report P&R a cesty časování vydaného bitstreamu. Proto níže nejdřív shrnuji, co
z dat plyne, pak uvádím tabulku „příznak → příčina → jak ověřit“ a nakonec připravené opravy.

Rozlišuji tři stupně jistoty:
- **ZJIŠTĚNO** = doloženo simulací nebo čísly z reportu;
- **NÁLEZ** = vlastnost návrhu, která se od kanálu A liší;
- **HYPOTÉZA** = fyzikálně možné, z dat neověřitelné.

## 1. Shrnutí

| # | co | týká se | stupeň | oprava / ověření |
|---|---|---|---|---|
| 1 | **Falešný příznak kontrolního počítání (FLAGS bit4)**: `snap` v taktu registrovaného přenosu čítače X dá `cdd = ±256` | A i B; četnost roste s kmitočtem | **ZJIŠTĚNO** simulací (`tb_ccd_carry.sv`) | `navrh_opravy.patch` |
| 2 | Pin B je v P&R na **LW** (sekundární síť), pin A na **PRIMARY**; návrh chtěl obě lokálně | jen B (asymetrie) | **NÁLEZ** (report P&R) | `CLOCK_LOC … LOCAL_CLOCK` (K2, mění `pins.cst`) |
| 3 | Minimální šířka pulzu hodin předděličky horší u B (MPW −2,27 ns proti −1,6 ns u A); čítač `u_pcb/c` je mezi cestami, které v 1 z 8 sestavení neprojdou | B víc než A | **NÁLEZ** (studie CLS, `pnr_seeds`) | K2; měřit PROBE_A/B se stejným signálem |
| 4 | Řetěz B leží v řádku **R27** hned u řady bloků **BSRAM R28** (za ní spodní I/O banka 2 s hodinovými piny 100 a 10 MHz); řetěz A v R26 sousedí s logikou | jen B | **HYPOTÉZA** (rušení napájení / přeslech od činnosti BSRAM, např. tabulka TDC každou značku, RAM PHY při SPI, kruh) | pokus: prohodit `GRP_LOC` A/B; zjistit, které BSRAM leží v R28 |
| 5 | Druhé vzorkovací registry B (`qd_r`) jsou dál od řetězu: vedení q_r → qd_r medián 2,74 ns (A 1,91 ns), nejhorší 5,01 ns (A 4,34 ns) | jen B | **NÁLEZ** (cesty časování); dopad malý | – (rezerva na metastabilitu zůstává > 3 ns) |
| 6 | LUT výběru zdroje B má **pin A na vstupu i v normálním režimu** → hrany A mohou o jednotky ps modulovat zpoždění hran B | jen B | **HYPOTÉZA** (přeslech uvnitř LUT a vedení) | K1 (`git_patche_kanalB/K1_…`) |
| 7 | Cesta pin B → řetěz má o LUT víc (výběr zdroje + XOR) a vede přes síť LW; rozdíl náběžné/sestupné hrany v XOR kalibrace 0x09 nezachytí | jen B | **NÁLEZ** (už zmíněn v PROTOKOL_V3 kap. 10.5) | změřit generátorem se známou šířkou |

Simulace A a B nerozlišuje. T0 proti skutečné hraně vychází v `tb_top_v3` na max. 181 ps u A a 77 ps u B a všechny
funkční kontroly B (kalibrace B, LUT_CNT_B, potvrzení s maskou, zdroj B 0x09, předdělička B) procházejí i v mutacích.
**Funkční chybu RTL specifickou pro B jsem nenašel**, kromě bodu 1, který se týká obou kanálů.

## 2. Bod 1: falešný FLAGS bit4 (ZJIŠTĚNO, chyba RTL v `base/`)

Čítač hran X (`seg_chan3`) je 40b s registrovaným přenosem: `xlo` (8 b) a `xhi` (32 b) s přenosem `xcy` o takt později.
V taktu po přetečení `xlo` (FF → 00) je `xlo = 0`, ale `xhi` je ještě starý. `xcnt[9:0]` je tedy o 256 menší než skutečný
počet. FIFO indexů bere `xcnt` až v T+3, kdy je konzistentní (komentář v kódu). **Kontrolní počítání ale bere `xcnt[9:0]`
ve `snap`, který může padnout právě do tohoto taktu:**

    cdd <= ccnt - (xcnt[9:0] - xprev);   // v taktu xcy = 1 je výsledek +256 → ccd_bad = 1
    xprev <= xcnt[9:0];                  // a další segment −256 → ccd_bad = 1 znovu

Simulace (`sim/tb_ccd_carry.sv`, v `navrh_opravy.patch`): vynucený `snap` v taktu `xcy = 1` dal ve všech 8 případech
`cdd = 256` a falešný `ccd_bad`, i když žádná hrana nechyběla. Na desce padne `snap` do takového taktu náhodně
s pravděpodobností `f / (256 × 100 MHz)` na segment. Při L = 25 ms (40 segmentů/s) to dává jednu událost za:

| kmitočet vstupu (hrany do X za s) | jedna událost za | počet poškozených záznamů |
|---|---|---|
| 1 MHz | ~11 min | 2 po sobě (záznam + další) |
| 10 MHz | ~64 s | 2 |
| 40 MHz (P = 1, téměř max.) | ~16 s | 2 |
| 143 MHz s P = 8 (18 MHz hran) | ~36 s | 2 |

**Proč to může vypadat jako „chyba kanálu B“:** chyba je v obou kanálech stejná, ale četnost je úměrná kmitočtu hran.
Pokud na B měříte vyšší kmitočet než na A (A třeba 10 MHz reference, B měřený signál), uvidíte příznaky častěji na B. Totéž
platí, pokud STM zahazuje záznamy s FLAGS bit4 nebo jimi přerušuje okna.

Oprava (`navrh_opravy.patch`): `xc10 = {xhi[1:0] + xcy, xlo}`, tedy dolních 10 b skutečného počtu i v taktu přenosu.
`tb_ccd_carry.sv` bez opravy selže (cdd 256), s opravou projde. Negativní kontrola (3 chybějící hrany se dál hlásí)
projde v obou případech. Patch jde aplikovat na `base/` i na konec optimalizační řady.

## 3. Bod 2 a 3: pin B na síti LW, předdělička (NÁLEZ)

Report vydaného sestavení (`docs/data/pnr_report_0x0507.txt`, kap. 6 Global Clock Signals):

    ch_a_d         | PRIMARY        |  TL
    ch_b_d         | LW             |  -
    PRIMARY 7/8 (88 %), LW 8/8 (100 %)

Vstupní piny kanálů jsou hodinami čítače předděličky (`pin_cnt3`: `always @(posedge pin)`). Gowin je proto povýšil na
hodinové sítě, A na primární a B na „long wire“ (LW). **Návrh to výslovně nechtěl.** Komentář `pin_cnt3` v `v3_core.v` říká:
„c[3:0] musí ležet u IOB pinu … krátké lokální vedení, hodiny pinu NE na globální síť (PRIMARY je ve v3 skoro plná) —
OVĚŘIT po P&R“. Ověření dopadlo jinak a nesymetricky. Důsledky:

- **Hodiny předděličky B mají horší tvar pulzu.** Studie CLS (kap. 7) uvádí MPW −2,27 ns u `pin_b` proti −1,6 ns u
  `pin_a`. Čítač `u_pcb/c` je v seznamu zbylých cest časování (1 z 8 sestavení). Ve vydaném sestavení má rezervu
  +0,121 ns, kdežto `u_pca/c` +0,190 ns. **Při vysokém kmitočtu se předdělička B pokazí dřív než A.**
  - S P > 1 to znamená chybějící dělené hrany, tedy skoky fáze a chybný kmitočet.
  - I s P = 1 to znamená chybnou sondu PROBE_B, a tedy špatnou volbu rozsahu ve STM.
- **Datová cesta pin → LUT předděličky → řetěz TDC vede u obou kanálů přes hodinovou síť, ale přes různou** (PRIMARY a LW).
  Zpoždění, jeho teplotní drift a tvar hrany se tak u A a B liší. To vadí hlavně u intervalu A→B (start/stop, šířka pulzu).
  Kmitočet jednoho kanálu konstantní zpoždění neovlivní, drift ano (d(zpoždění)/dt).

Gowin umí přiřazení sítě řídit v CST (SUG935-1.6.1E, příl. A.8): `CLOCK_LOC "net" LOCAL_CLOCK;` povede síť lokálně,
`CLOCK_LOC "net" BUFG = CLK;` povede po primární síti jen hodinové zátěže. Připravil jsem pokus K2
(`git_patche_kanalB/K2_…`, **mění `pins.cst`, proto mimo optimalizační řadu**):

    CLOCK_LOC "ch_a_d" LOCAL_CLOCK;
    CLOCK_LOC "ch_b_d" LOCAL_CLOCK;

Oba piny pak povedou čítač předděličky i řetěz TDC krátkým lokálním vedením (jak návrh zamýšlel) a uvolní se jedna síť
PRIMARY a jedna LW. Ověřit:
1. report P&R (kap. 6), že `ch_a_d`/`ch_b_d` už nejsou v tabulce globálních hodin;
2. MPW a časování `u_pca/u_pcb` (`pnr_seeds`);
3. `check_tdc_placement.py`;
4. na desce šum a rozptyl A a B se stejným signálem.

Pokud Gowin `LOCAL_CLOCK` u hodin s 4 FF odmítne, je druhá varianta `BUFG = CLK` (stejná síť pro oba, data lokálně).

## 4. Bod 4–6: fyzické rozdíly řetězu B (NÁLEZ / HYPOTÉZA)

- **Umístění řetězu** (`pins.cst`: `GRP_LOC g_tdca R26C[2:46]`, `g_tdcb R27C[2:46]`). Mřížka GW1NR-9 podle
  `tools/floorplan_svg.py`: R1..R28 × C1..C46, řady bez CFU jsou BSRAM R10 a **R28** a DSP R19. Řetěz B tedy leží hned nad
  řadou bloků BSRAM R28 a za ní je spodní I/O banka 2 (hodiny 100 MHz = pin 35 / IOB29A, 10 MHz = pin 42 / IOB41B, LED
  `led_tx` IOB13A 8 mA). Řetěz A má na obou stranách běžnou logiku. Zpoždění článků řetězu závisí na místním napájení.
  - Činnost bloků BSRAM v R28 může přidat šum nebo přeslech do řetězu B (HYPOTÉZA). Které bloky tam P&R dal, ukáže
    floorplan nebo `.rpt`. Kandidáti:
    - tabulka TDC kanálu B, čtená po každé značce B (vlastní, ale časově korelovaná činnost);
    - RAM PHY v doméně SCK (25 MHz během transakce; návrh už má maskování značek při CS kvůli rušení SPI, O4);
    - kruh záznamů (čtení během skládání rámce, 10 MHz).
  - Rušení s pevnou fází vůči 100 MHz (např. 10 MHz) by záviselo na tom, do kterého z 10 taktů hrana padne. Kalibrace
    hustotou kódů ho neodstraní, protože průměruje přes všechny fáze.
  - Po kroku 1 optimalizační řady je bloků BSRAM 24 místo 16 a některý z nových může přibýt do R28. Zápis je jednou za
    segment, čtení jen při skládání rámce, tedy stejná činnost jako dřív, jen v jiných blocích. Stojí za to ověřit, kam je
    P&R umístí.
- **Druhý stupeň vzorkování** (`qd_r`, kompaktní kopie). U A může ležet v R25 hned nad řetězem. U B ne pod řetězem (R28 je
  BSRAM bez CFU) ani v R26 (řetěz A), takže leží nad řetězem A a 268 sítí B vede přes řádek s řetězem A. Z cest časování:

  | | q_r → qd_r medián | nejhorší vedení | nejmenší rezerva |
  |---|---|---|---|
  | A | 1,91 ns | 4,34 ns | 3,90 ns |
  | B | 2,74 ns | 5,01 ns | 3,23 ns |

  Na metastabilitu 1. stupně zbývá u B pořád > 3 ns, takže dopad na chybovost odhaduji jako zanedbatelný. Sítě B ale
  vedou přes řetěz A, což je potenciální přeslech B→A (HYPOTÉZA).
- **LUT výběru zdroje B**: `sig_b = (bs2[0] ? ch_a : div_b) ^ bs2[1]`. I při `bsrc = 0` má LUT před řetězem B pin A na vstupu
  a vedení pinu A k ní je dlouhé (z IOB8 až k hlavě řetězu B). Hrany A pak mohou podle vzájemné fáze hran A a B posunout
  hranu B o jednotky ps. Šlo by o periodickou chybu B s rozdílovým kmitočtem f_A − f_B, kterou A nemá (HYPOTÉZA).
  Oprava K1 (`git_patche_kanalB/K1_…`): pin A projde přes samostatné hradlo `cha_b = ch_a & bs2[0]` (`keep`). V normálním
  režimu je na vstupu LUT výběru statická 0. Logicky je shodné (stopa `topv3pw` beze změny), mění jen zapojení.

## 5. Tabulka: příznak → nejpravděpodobnější příčina → pokus

| pozorovaný příznak na B | nejpravděpodobnější příčina | rozhodující pokus |
|---|---|---|
| občasné záznamy s FLAGS bit4 (kontrolní počítání), po dvou, častěji při vyšším kmitočtu; okna přerušená / zahozená | bod 1 (snap v taktu přenosu) | spočítat bit4 za hodinu při 10 MHz na A i B: čekání ~56/h na kanál; s `navrh_opravy.patch` 0 |
| chyba jen s předděličkou (P > 1) nebo chybná PROBE_B nad ~100–150 MHz | bod 3 (hodiny `u_pcb` na LW, MPW) a strop vstupu LVCMOS33 | stejný signál na A i B: PROBE_A = PROBE_B? Pak K2 (`LOCAL_CLOCK`) |
| větší rozptyl / šum B než A při stejném signálu, P = 1 | bod 4, 2 nebo 6 | (a) prohodit `GRP_LOC` řádků (jde chyba s řádkem R27?); (b) vypnout signál na A (zmizí-li, přeslech → K1); (c) maskování při CS zapnout/vypnout a porovnat (SPI → RAM PHY v R28?); (d) K2 |
| periodická chyba B závislá na rozdílu kmitočtů A a B | bod 6 (přeslech A→B v LUT výběru) | rozmítat f_A při pevném f_B; s K1 musí zmizet |
| konstantní posun A−B, chyba šířky pulzu | bod 7 (LUT navíc, XOR, náběžná/sestupná hrana, jiná síť) | kalibrace `0x09 = 1` (existuje), šířka generátorem se známou šířkou |
| B se nekalibruje (CAL_FAIL_B, LUT_CNT_B ≠ počet) | funkční cesty kalibrace B jsou v simulaci a mutacích pokryté; spíš ring oscilátor → `u_tdcb/sig_eff` (P&R) | histogram B (stránka druh 2): plný rozsah kódů jako A? `MAXTAP_B` |

## 6. Co jsem připravil

- `navrh_opravy.patch`: oprava bodu 1 + `sim/tb_ccd_carry.sv` (ukazuje chybu na `base/`). Do `run.ps1` doplnit řádek
  z popisu patche.
- `git_patche_kanalB/K1_…` (K1): oddělení pinu A od LUT výběru zdroje B (logicky shodné, stopa `topv3pw` shodná).
- `git_patche_kanalB/K2_…` (K2): `CLOCK_LOC … LOCAL_CLOCK` pro oba piny kanálů (**mění `pins.cst`**, pokus pro hlavní
  instanci).
- Doporučený postup:
  1. aplikovat opravu bodu 1 (jistá chyba);
  2. sestavit s K2 a porovnat report kap. 6 a MPW;
  3. na desce stejný signál na A i B (rozptyl, PROBE, bit4);
  4. podle výsledku K1 nebo pokus s prohozením řádků.
