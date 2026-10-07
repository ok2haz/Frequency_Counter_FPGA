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
