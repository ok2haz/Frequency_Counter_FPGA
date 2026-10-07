# TDC a vnitřní matematika: co ověří měření vlastní reference (2026-10-07)

> Vznik: otázka uživatele „měření vlastní reference neověří správnost vnitřní matematiky a TDC?“
> a pokyn „proveď analýzu, měření a test“. Měřeno **naživo na desce** (FW 0x040F, CM7 v0.13.0,
> IPC v20, uptime 1412–1912 s). ⚠️ **Po celou dobu měřil čítač svou vlastní 10MHz referenci** (uživatel
> potvrdil), tedy signál **synchronní** s hodinami TDC. Závěry níže jsou omezené právě tímhle.

## 1. Co měření vlastní reference ověří a co ne

| ověří | neověří |
|---|---|
| hrubé čítání hran, párování start/stop, převod jednotek, `f = N/Δt`, formátování, cestu rámce na displej/web | **TDC**: obě krajní hrany okna padají na STEJNÝ kód, `cal[k]` se v rozdílu odečte (špatná tabulka, INL, znaménko jemného času by daly pořád přesně 10 MHz) |
| odolnost hrubého čítání proti metastabilitě (fáze hrany stojí u hran hodin → trefuje se opakovaně) | přesnost reference (poměr reference k sobě je vždy 1) |
| chování TDC **v jediném fázovém bodě** | rozlišení, systematickou chybu, nelinearitu přes celou periodu |

Statistika (ADEV/MDEV) se šumem ≈ 0 nic netestuje; ta je kryta selftesty a `stat_test` proti SP1065.

## 2. Naměřeno (pozor: všechno ze synchronního signálu)

### 2.1 Verdikt B1a z křemíku (STATUS #267) — první měření na desce

`tdc` → `maxtap 133` vs nejvyšší kód 134 u **obou** kanálů: **teploměr je čistý, řetěz fyzicky končí na ~tapu 134**
(příčina (a), ne bubliny). Bubliny/metastabilita vzorkování jako příčina obřího binu jsou tím vyvrácené;
oprava = delší/jiný carry chain (P&R), ne další vzorkovací FF. `za koncem řetězu 0` a `retez kratky 0/0` to
nezachytí, protože sytí se kód 134, ne 255.

### 2.2 Kalibrační histogram (ring oscilátor = ASYNCHRONNÍ zdroj, tedy NEzávislé na vlastní referenci)

`tools/tdc_hist_analysis.py` nad `tdc hist` (1 048 576 událostí):

| | FW 0x040D (04.10.) A / B | FW 0x040F (07.10.) A / B |
|---|---|---|
| použité kódy | 34..134 (84) / 28..134 (96) | 24..134 (94) / **66..134 (63)** |
| obří bin k=134 | 1947 / 1520 ps | **1435 / 4784 ps** |
| σ kvantizace 1 hrany | 251 / 175 ps | **161 / 956 ps** |
| σ bez obřího binu | 39 / 37 ps | **35 / 28 ps** |

- Mimo obří bin je rozlišení ~28–35 ps rms — **TDC samo o sobě není „extrémně přesné“, ale ani šumné**.
- Obří bin kanálu A je 14 % periody (hrany s τ ∈ ⟨8,57; 10⟩ ns se nasytí), kanálu **B 48 %** (kódy 0..65 nikdy
  nevzniknou, spouštěč vidí hranu o ~5 ns později). Kanál B je na tom **hůř**; ⬜ dosud na něm nebylo měřeno.
- Kalibrace se mezi běhy mění (jiný P&R i teplota): obří bin A 1947 → 1435 ps. Netvrdit „ladí se na ~30 ps“.

### 2.3 Okna při vlastní referenci (`tools/tdc_selfref.py`, 198 oken `sdramlog dump`, a 244 s ze `status`)

| veličina | hodnota |
|---|---|
| okna přesně 10 000 000,000000 Hz | 72,7 % |
| skok **+0,37128 Hz** / **−0,37128 Hz** | 12,6 % / 13,1 % |
| okna vyřazená jako chybný počet hran (miscount) | 1,5 % (198 oken), **2,05 %** (244 s), kumulativně 3,0 % |
| z toho „hrana navíc“ / „hrana chybí“ | 85 % / 15 % |
| `spike-reject` | 27,4 % oken za 244 s (kumulativně 26 %) |
| mezery v `SEQUENCE` | 0 |

**Okna nabývají jen tří hodnot** — žádný „šum“ mezi nimi. To je přesně to, co dává synchronní signál (kvantizace
konstantní), a proto displej tvrdil `10 000 000,000 0000`. Oprava z `d63df87` (podlaha √2·tdc/gate pro počet
důvěryhodných číslic) řeší zobrazení; **přesnost ale vlastní reference nemá jak ukázat**.

### 2.4 Skoky ±0,37128 Hz = chyba JEDNÉ časové značky, ne dvou oken

- Skoky chodí ve **dvojicích `+−`** (98 % skoků, součet dvojice = 0). Okna jsou gap-free a sdílejí uzavírací
  hranu, takže chybná **jedna** časová značka dá `+e` v okně k a `−e` v okně k+1.
- Velikost: `0,37128 Hz / 10 MHz · 0,25 s = 9 282,0 ps`. Odpovídá **`T_clk − w134/2 = 10 000 − 1 435/2 = 9 282,5 ps`**
  (shoda 0,5 ps, tj. 5·10⁻⁵) — **podpis obřího binu**, ne náhodný šum. Podíl takto postižených oken (26–27 %)
  souhlasí s `spike-reject`.
- ⬜ **HYPOTÉZA mechanismu:** hrana na hranici taktu: spouštěč (`t0`) a zmrazený teploměr se vyhodnotí o jeden takt
  odlišně, takže se kód čte jednou jako ~0 a podruhé jako 134 (`cal[134] ≈ 9 282 ps`). Ověření: generátor s posunem
  (viz 4); podíl postižených oken musí záviset na fázi.
- Důsledek: spike-reject vyřadí obě okna dvojice správně (vůči `uhz_prev`); **bez něj** by jedna taková značka
  dala skok σy ≈ 3,7·10⁻⁸ na jediném vzorku.

### 2.5 Chybné počty hran (miscount) FW 0x040F stále přetrvávají

`CITANI HRAN` roste ~2 % oken. Oprava `38011fc` (synchronizátor) tedy **příčinu neodstranila** (nebo to není
jediná příčina). ⚠️ Nemám měření „před“ ani „po“ (počítadlo vzniklo až s v0.13.0), takže **nevím, o kolik se to
zlepšilo**. Pojistka STM funguje (okna se nezobrazí ani nepočítají). ⬜ Příčina otevřená:
- Neizolované (osamocené `Z`), nesouvisí zjevně s dvojicemi `+−` (3 případy, málo dat).
- Převaha „navíc“ (85 %) odpovídá spíš dvojí detekci hrany než ztrátě.
- Navrhovaná instrumentace FPGA: čítač „rise_s bez předchozího trig“ a počet rozdílů `rise_c`/`rise_s` v rámci
  (CAL bajty), aby šlo oddělit chybu počítání od chyby časové značky.

## 3. Co se tímto změnilo v závěrech

1. **Obří bin = fyzický konec řetězu** (2.1). Bubliny jsou vyvrácené → vzorkovací FF nepomůžou.
2. **Kanál B má obří bin 48 %** a startuje na kódu 66 — před použitím B na přesné měření (TI A–B) je nutné opravit.
3. „Extrémně přesné“ číslo při vlastní referenci je **artefakt synchronního signálu**, ne vlastnost TDC.
4. Oprava `38011fc` je **nedostatečná** pro počítání hran (2.5) — nelze ji uvádět jako hotovou.

## 4. Test, který TDC a matematiku opravdu ověří (⬜ čeká na generátor)

Generátor se **vstupem 10 MHz zavěšeným na stejnou referenci**, na CH_A kmitočet **10 000 000,137 Hz**:

1. Správná hodnota je přesně známá (obě strany sdílí časovou základnu) → odchylka průměru = **systematická chyba čítače**.
2. Fáze hrany vůči 100MHz hodinám projede celých 10 ns za ~0,7 s → **všechny kódy TDC** → průběh chyby v čase = INL.
3. Rozptyl oken = skutečné rozlišení (očekávání z 2.2: √2·~30 ps/0,25 s ≈ 1,7·10⁻¹⁰ = 1,7 mHz; **obří bin jej
   zhorší** na ~228 ps → ~9 mHz u A (9·10⁻¹⁰), u B ~1350 ps → ~54 mHz).
4. Podíl skokových oken (2.4) musí záviset na fázi; měřit `tools/tdc_selfref.py --fnom 1e7 ...` a `status`.
5. Totéž na kanálu B (`channel` přepnout) — dosud neměřen.

Bez generátoru: nezávislý nezavěšený oscilátor dává jen rozptyl a σy(1 s) k porovnání s podlahou; absolutní
chybu ne.

## 5. Měření GPSDO proti vnitřnímu OCXO, kanál A (HW 2026-10-07, 60 min)

Zapojení: **vstup CH_A = externí GPSDO 10 MHz (měl fix), časová základna = vnitřní OCXO** (Si5356 → 100 MHz).
Dva nezávislé oscilátory. Data: `docs/audit/data/2026-10-07_beat_A.csv` (14 329 oken, 3 600 s, sběr přes SSE),
rozbor `python tools/tdc_beat.py analyze ...`, FW 0x040F, v0.13.0.

### 5.1 Výsledky

| veličina | hodnota |
|---|---|
| **δ OCXO vs GPSDO** | **+839,97 ppb** (10 000 008,3997 Hz), po 5 min 839,6 až 841,0 ppb |
| stabilita δ za hodinu | ±0,5 ppb, OCXO 47,3 °C |
| šum časové značky (robustně, 20oknový fit) | **215 ps** (bloky po 5 min: 177–348 ps) |
| σ okna ≈ √2 × šum značky | **304 ps = 1,2·10⁻⁹** za 0,25 s |
| značky s chybou ≥ 2 ns | 4,4 % (u vlastní reference 26 %) |
| počet hran v okně | N: 81 %, N+1: 9 % (hradlo končí první hranou po ticku) |

Číslo δ je absolutní chyba časové základny jen tak přesně, jak je GPSDO na 10 MHz (jeho typ a stav jsem nezjišťoval).

**Počítadla ve `status`** (stav před: uptime 3086 s, po: 6741 s, tedy 3 655 s ≈ 14 620 oken):

| | před | po | přírůstek | podíl oken |
|---|---|---|---|---|
| `spike-reject` | 3 402 | 6 780 | +3 378 | **23,1 %** |
| chybný počet hran, „navíc“ | 276 | 288 | +12 | 0,08 % |
| chybný počet hran, „chybí“ | 97 | 97 | 0 | 0 % |

### 5.2 Co z toho plyne

1. **Chybné počty hran závisejí na fázi, ne na kmitočtu.** U vlastní reference (pevná fáze) 2,05 % oken, zde
   **0,08 %**, tedy ~25× méně, a to na stejném FW 0x040F. Hypotéza „metastabilita při pevné fázi hrany vůči taktu“ tím
   získala podporu (⬜ není to důkaz: fáze putovala velmi pomalu, viz 5.3). Synchronizátor `38011fc` tak možná
   funguje a zbytek patří do oblasti hrany, kde se signál zdržel u vlastní reference.
2. **`spike-reject` vyřazuje 23 % oken, ale značek s chybou ≥ 2 ns je jen 4,4 %** (≈ 9 % oken, protože chyba značky
   zasáhne dvě okna). Práh 3·10⁻⁹ = 0,75 ns v Δt je při σ okna 304 ps jen ~2,5σ. Zbylých ~14 % oken jsou běžné
   odlehlé hodnoty z těžkých ocasů rozdělení, ne artefakt obřího binu. Důsledek: **σy je optimistické** (ořezává
   ocasy), a práh ladil na dřívější σ 314 ps (STATUS #267) při jiné fázi. ⬜ Nové TODO: práh vázat na změřený
   šum, nebo rozpoznat artefakt podle ±9,28 ns, ne podle amplitudy.
3. **Rozlišení TDC je skutečně ~215 ps na značku** (σ okna 304 ps), tedy 1,3× horší než předpověď z kalibračního
   histogramu (228 ps pro okno A, 2.2) a **o řád horší než dříve uváděných „~30 ps“**. Tyto dvě čísla nejsou v rozporu:
   30 ps je σ bez obřího binu, 215 ps je σ s ním a s INL.
4. **δ je za hodinu stabilní na 1 ppb**, takže softwarová korekce časové základny by byla smysluplná, i než přijde DAC.
5. Mezery v `SEQUENCE` v SSE: 35 událostí, 72 oken (0,5 %). Z toho 12 jsou chybně napočítaná okna (jsou
   zamlčená záměrně), zbytek je sloučení publikace/SSE a 1 zmeškané okno FpgaTasku. Neovlivňuje výsledky.

### 5.3 Co se NEPODAŘILO určit

**INL/DNL a přesnou polohu skoků.** Hradlo je zarovnané na hodiny, takže fáze hrany se mezi okny posune o
`frac(0,25 s · δ / 10 ns) · 10 ns`. Při δ = 840 ppb je `2,5·10⁷·δ = 20,9992` (po 5 min 20,989–21,025), tedy posun
jen −8…+250 ps na okno, a to se střídavě znaménkem, takže fáze se celou hodinu zdržovala v úzkém pásmu a během
bloku se mění o ~1 ns. Při takto pomalé změně polynom bloku INL vstřebá; mapa by byla falešná pila (nástroj to
pozná a mapu nevypíše; první verze nástroje pilu vyrobila a dala nesmysl σ 1 ns, opraveno `91c453c`).

⚠️ **Tím je i výsledek 5.2 (bod 1) fázově nejistý:** měřená fáze nepokryla celou periodu, tedy 0,08 % platí pro
tu část, kde se fáze zdržela.

### 5.4 Další krok

1. **Změnit rozdíl OCXO × vstup tak, aby zlomek `2,5·10⁷·δ` ležel mezi 0,05 a 0,95** (ideálně ~0,5: posun
   5 ns/okno, celá perioda za 2 okna, bez dwellu). Možnosti: generátor s odstavenou referencí o ±12 ppb
   (tj. 10 000 000,12 Hz), nebo vstup jiným GPSDO výstupem. Změnit OCXO bez DAC (`AD5693R` neosazen) nejde.
2. Až bude pokrytí fáze plné: INL mapa, přesná poloha obřího binu a závislost skoků a chybných počtů na fázi.
3. Zopakovat na **kanálu B** (obří bin 48 %, dosud neměřen).
4. TODO: práh `spike-reject` (viz 5.2 bod 2) a softwarová korekce δ (jen pokud má smysl před DAC).

