# Přesný frekvenční čítač — zadání pro UI

Dokument pro instanci, která **sjednocuje existující grafiku a staví
kompletní uživatelské rozhraní**.

Grafické prvky už existují. Tento dokument neurčuje vzhled — určuje
**strukturu, chování a co musí být kde vidět a proč**. Kde uvádím
maketu obrazovky, jde o rozvržení informací, ne o grafický návrh.

---

# 1. Kontext přístroje

Dvoukanálový reciproční frekvenční čítač s GPSDO. Rozlišení časového
intervalu ~22 ps, rozsah do 200 MHz přímo a 1,1 GHz přes předděličku.

Uživatel je **technik nebo metrolog**, ne laik. Očekává hustou informaci
a krátké cesty, ne průvodce a ikony.

## Hardware ovládání

| Prvek | Dostupnost |
|---|---|
| Encoder s tlačítkem | 1× |
| Displej | DSI, existující grafika |
| Stavové LED | 7 z 10 pinů `LED_Con` |
| Klávesnice | **není** |
| Dotyk | **nepředpokládej** — návrh musí fungovat jen encoderem |

Pokud je displej dotykový, ber dotyk jako **zrychlení**, ne jako
podmínku. Každá funkce musí být dosažitelná encoderem.

---

# 2. Tři zásady, ze kterých vychází celý návrh

## Zásada 1 — hustota informace před čistotou

Metrolog potřebuje na hlavní obrazovce vidět **odečet i podmínky, za
kterých vznikl**. Prázdné místo je horší než údaj, který se hodí jednou
za deset měření.

Konkrétně: nikdy neschovávej prahy, impedanci, vazbu, útlum, cestu,
zdroj základny a stav GPSDO do menu.

## Zásada 2 — každý údaj má svou nejistotu vedle sebe

Odečet bez σ a počtu vzorků je v tomto přístroji **nepoužitelný**,
protože rozlišení 12 číslic svádí k nezasloužené důvěře.

Platí i pro analytické obrazovky: ADEV bez podlahy přístroje, Δf bez
nejistoty a offset bez rozptylu jsou neúplné.

## Zásada 3 — hloubka podle četnosti, ne podle logiky

Seznamy řaď podle toho, jak často se používají. `Frekvence A` je první,
i když by logicky patřila mezi ostatní funkce.

Cíl: nic z běžné práce hlubší než **tři kliky**, kalibrace a systém
čtyři — a to úmyslně, ať se tam uživatel nedostane omylem.

---

# 3. Nejdůležitější specifikum tohoto přístroje

**Trigger level znamená na každé cestě něco jiného.** To je jediná věc,
kterou když UI zanedbá, přístroj bude dávat nesmyslné výsledky a uživatel
nepozná proč.

| Cesta a vazba | Význam prahu | Kde to zobrazit |
|---|---|---|
| 1 MΩ + DC vazba | **absolutní mV na vstupu** | hlavní obrazovka + editor |
| 50 Ω nebo AC vazba | relativně ke střední hodnotě | dtto |
| přes ÷10 | **bez významu** | dtto + **zamknout editaci** |

Požadavky na UI:

- Aktuální **cesta (PŘÍMO / ÷10) musí být na hlavní obrazovce**
- Editor prahu zobrazuje řádek s významem aktuální hodnoty
- V režimu ÷10 je editace prahu **zamčená**, ne jen varovaná
- Varovný pruh `PRÁH BEZ VÝZNAMU` při vstupu do režimu ÷10

---

# 4. Hlavní obrazovka

Není součástí menu. Zobrazuje se vždy, když uživatel není v menu, a
**sama je ovládacím prvkem**.

```
┌──────────────────────────────────────────────────┐
│  FREKVENCE A                          ⏱ 1,000 s │
│                                                  │
│      10 000 000,000 12  Hz                       │
│                                                  │
│  σ 0,000 08 Hz    N=1000    ▓▓▓▓▓▓▓░░░  72 %     │
├──────────────────────────────────────────────────┤
│ A: 50Ω AC  0dB  PŘÍMO  ↑ +12,5 mV  hyst 10 mV    │
│ B: 1MΩ DC −6dB PŘÍMO  ↑   0,0 mV  hyst 25 mV     │
│ INT · GPSDO LOCK · 25,3 °C                       │
└──────────────────────────────────────────────────┘
```

## Povinné údaje

| Údaj | Proč |
|---|---|
| Odečet a jednotka | |
| Funkce a doba hradla | |
| **Průběh hradla** | uživatel ví, kdy přijde další číslo |
| **σ a počet vzorků** | bez toho nelze číslu věřit |
| Práh A a B číselně | |
| **Cesta PŘÍMO / ÷10** | mění význam prahu |
| Impedance, vazba, útlum, sklon | kontrola, že měříš tím, čím myslíš |
| Zdroj základny INT / EXT | |
| Stav GPSDO | |
| Teplota | kalibrace binů na ní závisí |

## Zvýraznění aktivního parametru

Encoder na hlavní obrazovce ladí **jeden aktivní parametr**. Ten musí být
vizuálně odlišený. Krátký stisk ho cykluje:

```
práh A → práh B → hystereze A → hystereze B → hradlo → zpět
```

---

# 5. Ovládání encoderem — smluvní

| Akce | Funkce | Platí kde |
|---|---|---|
| Otáčení | ladí aktivní parametr | hlavní obrazovka |
| Otáčení | pohyb v seznamu | menu |
| Otáčení | kurzor / číslice | editor, grafy |
| Krátký stisk | cyklus aktivního parametru | hlavní obrazovka |
| Krátký stisk | vstup / potvrzení | menu, editor |
| **Dlouhý stisk (1 s)** | **AUTO-TRIGGER** | hlavní obrazovka |
| Dlouhý stisk | **zpět o úroveň** | všude jinde |
| Dvojklik | vstup do menu | hlavní obrazovka |

**Dlouhý stisk je vždy „zpět"** kromě hlavní obrazovky, kde je to
auto-trigger. Ta výjimka je záměrná — auto-trigger je nejužitečnější
jednotlivá akce a v menu by byl tři kliky hluboko.

## Adaptivní krok ladění

| Rychlost otáčení | Krok prahu |
|---|---|
| < 2 kroky/s | **0,5 mV** (nejmenší) |
| 2–15 kroků/s | 5 mV |
| > 15 kroků/s | 50 mV |

Rozsah ±1024 mV projede rychlým otáčením za dvě sekundy, ale doladí na
nejmenší krok. **Žádné přepínání režimu.**

## Stavové LED

```
GPS FIX        svítí = platná pozice
GPSDO LOCK     bliká = zavírá smyčku, svítí = zavřeno
EXT REF        svítí = externí reference platná a použitá
SIG A          svítí = signál nad prahem citlivosti
SIG B          dtto
GATE           bliká s hradlem
ERROR          jakékoli varování
```

---

# 6. Stavy rozhraní

```
MĚŘENÍ ⇄ MENU → { EDITACE, VÝSLEDEK, PRŮVODCE }
PRŮVODCE → DIALOG → MENU
VAROVÁNÍ = překryv, kdykoli, nad vším
```

| Stav | Vstup | Výstup |
|---|---|---|
| MĚŘENÍ | výchozí | dvojklik → MENU |
| MENU | dvojklik z měření | dlouhý stisk → MĚŘENÍ |
| EDITACE | stisk na hodnotě | stisk potvrdí, dlouhý zruší |
| VÝSLEDEK | spuštění analýzy | dlouhý stisk → MENU |
| PRŮVODCE | spuštění kalibrace | dokončení → DIALOG |
| DIALOG | konec průvodce | volba → MENU |
| VAROVÁNÍ | podmínka | zmizí, když podmínka skončí |

---

# 7. Struktura menu

Šest skupin. První tři používá uživatel při každém měření, druhé tři
nastaví zřídka.

```
FUNKCE              KANÁL A / B         HRADLO · STATISTIKA
ČASOVÁ ZÁKLADNA     KALIBRACE           SYSTÉM
```

## FUNKCE — plochý seznam řazený podle četnosti

```
Frekvence A          ← výchozí
Frekvence B
Perioda A
Perioda B
Časový interval A→B
Fáze A↔B
Poměr A/B
Totalize A ▸         start / stop / reset, hradlování ručně / časem / signálem B
Šířka pulzu A
Šířka pulzu B
Střída A
Střída B
ANALÝZA ▸            Jitter/histogram, ADEV, Δf normálů, Fázový šum
KŘÍŽOVÁ KONTROLA ▸   diagnostika tří cest
```

Menu si pamatuje poslední volbu. **Analýza je v podmenu**, aby
neprodlužovala cestu k základním funkcím.

## KANÁL A / KANÁL B — dvě samostatné položky

Ne jedna položka s volbou strany. Šetří klik a odstraňuje chybu, kdy
uživatel nastaví práh na špatném kanálu.

```
Vazba:        AC / DC
Impedance:    50 Ω / 1 MΩ
Útlum:        0 / −6 / −15 / −27 dB / AUTO
Cesta:        AUTO / PŘÍMO / PŘES ÷10
Sklon:        ↑ / ↓ / oba
Práh ▸        režim AUTO / MANUÁL / 50 % ampl., hodnota, jednotka, auto-trigger
Hystereze ▸   režim AUTO / MANUÁL, hodnota, doporučení
Úroveň signálu: [jen čtení, dBm z detektoru]
```

### Chování automatik — musí být zobrazeno v nápovědě

| Automatika | Chování |
|---|---|
| Útlum AUTO | podle úrovně signálu, hystereze 3 dB, po přepnutí **50 ms** pauza |
| Cesta AUTO | pod 180 MHz přímo, nad 220 MHz ÷10, hystereze 10 % |
| Práh AUTO | projede rozsah, najde špičky, nastaví střed |
| Práh 50 % ampl. | drží polovinu rozkmitu i při změně signálu — pro střídu |
| Hystereze AUTO | z rozptylu period; **pro TI drž ručně na minimu** |

**Po přepnutí útlumu nebo cesty zobraz odečet šedě po dobu ustálení
(50 ms).** Vazební kondenzátory s 1 MΩ mají časovou konstantu 10 ms.

## HRADLO · STATISTIKA

```
Doba hradla:   1 ms / 10 ms / 100 ms / 1 s / 10 s / 100 s / ruční
Režim:         jednorázově / opakovaně / na spouštěč
Regrese ▸      zapnuto, počet vzorků, co zobrazit
Statistika ▸   vyp / průměr / průměr+σ / min-max / vše, počet, reset
Trend ▸        časová osa, rozsah, export
Limity ▸       zapnuto, dolní/horní, akce, počítadlo
Zobrazení ▸    počet číslic, jednotka, relativně k
```

**`Regrese` a `Statistika` se pletou** a nápověda to musí rozlišit:

*Regrese* počítá jednu hodnotu z mnoha časových známek metodou nejmenších
kvadrátů. Přidá 3 a víc číslic rozlišení. Běží ve FPGA, gap-free.

*Statistika* počítá průměr a rozptyl z mnoha **hotových** výsledků. Řekne,
jak se přístroj a signál rozptylují.

## ČASOVÁ ZÁKLADNA

```
Zdroj:            INTERNÍ OCXO / EXTERNÍ
GPSDO ▸           zapnuto, časová konstanta, stav, ladicí napětí, satelity, historie
Reference TDC:    100 MHz (Si5356) / 10 MHz (OCXO surová)
Výstupy ▸         10 MHz TTL (4×), CLK_OUT_1/2/3
Externí reference ▸  očekávaná f, stav, odchylka od OCXO
Teplota OCXO:     [jen čtení]
```

Dvě specifika:

**Při volbě EXTERNÍ musí firmware nejdřív ověřit frekvenci**, ne jen
přítomnost hran — tvarovač externí reference se bez signálu sám rozkmitá.
UI zobrazí `ověřuji…` a při neúspěchu `EXT REF NEPLATNÁ, zůstávám na OCXO`.

**Reference TDC — vždy jen jedna aktivní.** Obě jsou koherentní a jejich
současný provoz by vyrobil chybu, která se neprůměruje. UI nesmí dovolit
zapnout obě.

## KALIBRACE

```
Offset A↔B ▸           stav, spustit (průvodce), počet amplitud, historie
Biny TDC ▸             stav, režim, teplotní tabulka, vynulovat
Práh a hystereze ▸     reference DAC, kalibrovat prahy (průvodce), zpět na výchozí
Útlum a citlivost ▸    tabulka útlumů, kalibrace dBm
Křížová kontrola ▸     spustit, výsledek
Uložit do flash / Načíst / Vše na výchozí
```

## SYSTÉM

```
Diagnostika ▸    3 strany živých hodnot + test hardwaru
Rozhraní ▸       Ethernet, SCPI port, USB, vzdálený přístup
Ukládání ▸       8 slotů, nastavení při zapnutí, export
Displej ▸        jasnost, spořič, téma, jazyk
Zvuk / Datum a čas / O přístroji ▸
```

---

# 8. Editace hodnot — tři způsoby

## Adaptivní krok — pro spojité hodnoty

Práh, hystereze, limity.

```
┌──────────────────────────────────┐
│  KANÁL A · PRÁH                  │
│        +  12,5  mV               │
│  ├────────●──────────────┤       │
│  −1024              +1024 mV     │
│                                  │
│  absolutní · mV na vstupu        │
│  ─────────────────────────       │
│  Aktuální úroveň: −18,2 dBm      │
│  Doporučeno: +8 mV               │
└──────────────────────────────────┘
```

**Pruh polohy v rozsahu** je povinný — bez něj uživatel neví, jak blízko
je limitu.

**Řádek s významem** se mění podle cesty a impedance. V režimu ÷10 tam je
`bez významu` a editace je zamčená.

## Po číslicích — pro přesné zadání

Nominální frekvence, IP adresa, limity. Kurzor pod aktivní číslicí,
stisk posouvá, na poslední pozici se vrací na první.

## Výběr ze seznamu — s okamžitým dopadem

Vazba, impedance, útlum. **Hodnota se aplikuje okamžitě**, uživatel vidí
dopad na odečet bez potvrzování. Zobraz i předchozí hodnotu pro srovnání.

---

# 9. Obrazovky výsledků

Rozvržení informací; grafiku navrhni podle existující sady.

## Histogram

```
HISTOGRAM · PERIODA A            N=98 432

     ▁▂▄▆█▇▅▃▁
  ▁▂▄███████████▄▂▁
────┴─────────┴─────────┴──────
  99,9998    100,0000   100,0002 ns
                   ▲ kurzor

střed   100,000 02 ns
σ         22,4 ps
p-p       141 ps      (6,3 σ)
šikmost   +0,04       rozdělení: normální
kurzor    100,000 05 ns  ·  2,1 % vzorků
```

**σ a p-p současně.** Poměr p-p/σ u normálního rozdělení je asi 6 — když
je výrazně větší, něco občas ruší.

**Šikmost a klasifikace rozdělení** je diagnostická informace, ne
kosmetika. Nesymetrický histogram znamená prahování na šumné hraně nebo
koherentní přeslech.

Ovládání: otáčení = kurzor, stisk = cyklus kurzor → zoom → posun.

## ADEV — nejhodnotnější analytická obrazovka

```
ALLANOVA ODCHYLKA · KANÁL A

1e-10┤●
     │ ●
1e-11┤   ●
     │      ●●
1e-12┤          ●●●●●
     │                 ●●
1e-13┤                    ●
     └──┴────┴────┴────┴────┴──
       0,1   1    10   100  1000 s

τ=1 s      2,2e-11
τ=10 s     2,4e-12   sklon −0,48
τ=100 s    2,2e-13   sklon −0,51

Dominantní šum: BÍLÝ FM  (sklon −½)
Podlaha přístroje: 2,2e-11 @ τ=1 s
```

**Sklon křivky identifikuje typ šumu** a tabulka patří přímo na obrazovku:

| Sklon | Typ šumu | Význam |
|---|---|---|
| −1 | bílý fázový | šum měřicího systému, TDC |
| −½ | bílý frekvenční | šum oscilátoru, normální |
| 0 | flicker frekvenční | podlaha oscilátoru |
| +½ | random walk | teplotní vlivy |
| +1 | drift | stárnutí, nestabilita napájení |

**Podlaha přístroje musí být zobrazena.** Firmware ji změří jednou (stejný
signál v obou kanálech přes kalibrační relé) a uloží. Bez ní uživatel neví,
jestli vidí oscilátor nebo čítač.

## Trend

```
TREND · FREKVENCE A          10 min

+2e-11┤        ╭──╮
      │   ╭────╯  ╰─╮      ╭───
    0 ┤───╯          ╰─────╯
−2e-11┤
      └──┴────┴────┴────┴────┴──
       −10  −8   −6   −4   −2   0 min

aktuální  10 000 000,000 12 Hz
min/max   ...,000 08 / ...,000 15
sklon     +1,2e-13 / hodinu
vztaženo k 10 000 000,000 00 Hz
```

**Sklon v jednotkách za hodinu** je hlavní číslo — z něj poznáš drift.

**Relativní zobrazení** proti nominálu; absolutní dvanáctimístné hodnoty
v grafu nikdo nepřečte.

## Δf normálů (Adret režim)

```
Δf NORMÁLŮ · A vs INTERNÍ    τ=100 s

fáze
+200ps┤              ╭────
      │        ╭─────╯
    0 ┤────────╯
      └──┴────┴────┴────┴──
       0   100  200  300  400 s

Δf/f       +1,84e-12
nejistota  ±2,2e-13   (τ=100 s)
fázový posuv  +214 ps za 400 s
projekce   +4,6e-12 za 24 h

A: 10 000 000,000 018 4 Hz
```

**Fázový graf, ne frekvenční.** Fáze je to, co se měří; frekvence je její
derivace a v grafu by byla samý šum.

**Projekce na 24 h** řekne, o kolik se dva normály za den rozejdou.

---

# 10. Průvodci

## Kalibrace offsetu A↔B — pět kroků

Jediná operace, kterou uživatel může zkazit. Průvodce ho tím provede
a zkontroluje podmínky.

```
KROK 1/5 — kontrola podmínek
  ✓ Teplota ustálená      25,1 °C
  ✓ Rail −5 V             −5,04 V
  ✓ V_S komparátoru       10,09 V
  ✓ Kalibrace binů TDC    platná
  ✗ Vstupy odpojeny       ← ODPOJ KABELY

  Kalibrační relé přepne oba vstupy na společný
  zdroj. Signál z BNC by výsledek zkreslil.

KROK 2/5 — volba amplitud
  1 / 3 (doporučeno) / 5 amplitud
  Dispersion komparátoru je až 40 ps a mění se
  s amplitudou. Kalibrace jednou amplitudou platí jen pro ni.

KROK 3/5 — měření
  amplituda 2/3 · −20 dBm
  ▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░  62 %
  vzorků 12 400 / 20 000
  průběžně +23,1 ps  σ 22,4 ps

KROK 4/5 — výsledek
  amplituda   offset     σ
  −40 dBm     +31,2 ps   24,1 ps
  −20 dBm     +23,4 ps   22,4 ps
    0 dBm     +19,8 ps   21,9 ps
  Rozptyl podle amplitudy: 11,4 ps
  Předchozí: +23,9 ps (2 dny, 24,8 °C) · rozdíl −0,5 ps ✓

KROK 5/5 — uložení
  [Uložit a použít] [Uložit jako záložní] [Zahodit]
```

**Zámek:** položka `Spustit` je nedostupná, dokud teploměr nehlásí změnu
pod 0,1 °C/min. Zobraz šedě s poznámkou `čekám na ustálení, 3 min`.
Kalibrace za studena dá offset, který po zahřátí neplatí — a uživatel
by to nepoznal.

**Porovnání s předchozí kalibrací** je zásadní: skoková změna znamená,
že se něco stalo.

## Kalibrace prahu — čtyři kroky, vyžaduje voltmetr

```
KROK 1/4  Připoj voltmetr mezi TP_TRIG_A a GND, rozlišení ≥ 0,1 mV
KROK 2/4  Nastavuji kód 2048. Zadej naměřenou hodnotu: [ +0,3 ] mV
KROK 3/4  Nastavuji kód 0.    Zadej naměřenou hodnotu: [ +1021,4 ] mV
KROK 4/4  Offset +0,3 mV · Zisk 0,9974 (−0,26 %)
          Reference DAC 2,0427 V (nominál 2,048) — v toleranci ±2 % ✓
```

---

# 11. Diagnostika

Tři strany živých hodnot, obnovované každou sekundu.

```
STRANA 1 — NAPÁJENÍ A TEPLOTY
  VBUS      23,84 V   ✓
  +5 V       5,012 V  ✓  (5,00 ±0,25)
  +3V3       3,301 V  ✓
  −5 V      −5,04 V   ✓  (−4,5…−6,0)
  V_S       10,05 V   ✓  (9,5…11,5)
  OCXO      48,2 °C   Δ 0,02 °C/min ✓
  modul     31,4 °C
  RF_Level A  −18,2 dBm   ·   B  −41,7 dBm

STRANA 2 — FPGA
  bitstream hash, bin TDC, σ binů,
  obsazenost FIFO, počet zahozených známek

STRANA 3 — SBĚRNICE A GPS
  I²C sken, stav relé (čteno z expandéru),
  GPS: satelity, SNR, fix
```

Tři položky s vysvětlením, proč tam jsou:

**`V_S` s mezemi** — komparátor vyžaduje 9,5 až 11,5 V a mimo to nedává
garantovaný rozkmit. Bez tohoto měření by uživatel nepoznal, proč měření
selhává.

**`Δ°C/min`** — zamyká kalibraci offsetu. Uživatel vidí, proč je zamčená
a jak dlouho ještě.

**Stav relé čtený z expandéru**, ne to, co firmware poslal. Jediný způsob,
jak odhalit rozpojený spoj v modulu.

## Test hardwaru

Automatická sekvence spustitelná z diagnostiky. Devět položek, výsledek
✓ / ✗ s odkazem na řešení.

```
✓ I²C sken · ✓ FPGA odpovídá · ✓ PLL zavřená · ✓ Napájecí větve
✓ Reference · ✓ Relé (19 přepnuto a přečteno) · ✓ DAC prahů
✓ Carry chain A (128 binů, σ 1,8 ps) · ✗ Křížová kontrola (84 ps > 50 ps)

8 z 9 v pořádku. Viz Kalibrace ▸ Křížová kontrola.
```

---

# 12. Varovné překryvy

Zobrazují se **nad jakoukoli obrazovkou** a nezmizí, dokud podmínka trvá.

| Priorita | Varování | Chování |
|---|---|---|
| 1 | `RAIL −5 V MIMO ROZSAH` | **zastavit měření**, výsledky neplatné |
| 2 | `KALIBRACE NEPLATÍ` | **zastavit měření**, hash bitstreamu se změnil |
| 3 | `EXT REF CHYBÍ` | automaticky zpět na OCXO |
| 4 | `PŘETÍŽENÍ A/B` | informace, doporuč zvýšit útlum |
| 5 | `BEZ SIGNÁLU` | informace, odečet je šum |
| 6 | `FIFO PŘETÉKÁ` | informace, měření není gap-free |
| 7 | `TEPLOTA NEUSTÁLENÁ` | informace, kalibrace zamčená |
| 8 | `PRÁH BEZ VÝZNAMU` | informace, režim ÷10 |

Priority 1 a 2 zastaví měření a odečet zobraz přeškrtnutý nebo šedě.

---

# 13. Kontextová nápověda

Dlouhý stisk na položce menu (mimo editaci) zobrazí jednu obrazovku
vysvětlení. U tohoto přístroje je to důležitější než u běžného měřáku,
protože několik nastavení má neintuitivní dopad.

| Položka | Nápověda |
|---|---|
| `Cesta ÷10` | Nad 200 MHz nutná. TI a fáze v tomto režimu nefungují — vidíš jen každou desátou hranu. Práh ztrácí význam. |
| `Hystereze` | Potlačuje šum na hraně. Pro TI drž na minimu — posouvá trigger point o polovinu své hodnoty. |
| `Regrese` | Počítá jednu hodnotu z mnoha časových známek. Přidá 3 a víc číslic rozlišení. Není totéž jako statistika. |
| `Reference TDC` | 100 MHz má o 20 dB nižší násobení fázového šumu než 10 MHz. Vždy jen jedna aktivní — jsou koherentní. |
| `Kalibrace binů` | Zpoždění hradel roste s teplotou o 20–30 %. Kalibrace musí být průběžná, ne jednorázová. |
| `Offset A↔B` | Rozdíl zpoždění kanálů. Kalibruje se přes vnitřní relé. Za studena naměřený offset po zahřátí neplatí. |
| `Podlaha přístroje` | Hranice, pod kterou měříš vlastní čítač, ne oscilátor. Změřená kalibračním relé. |

---

# 14. Hloubka na horké cestě — kontrolní seznam

Po dokončení návrhu ověř, že platí:

| Úkon | Max. kliků |
|---|---|
| Čtení frekvence po zapnutí | **0** |
| Změna prahu | stisk + otočení |
| Změna hradla | 5× stisk + otočení |
| **Auto-trigger** | **dlouhý stisk** |
| Změna funkce | 2 |
| Změna impedance nebo vazby | 3 |
| Zapnutí statistiky | 3 |
| Spuštění ADEV | 4 |
| Kalibrace offsetu | 4 |
| Nastavení Ethernetu | 4 |

---

# 15. Priority implementace

## Verze 1 — funkční přístroj

```
hlavní obrazovka s inline editací a zvýrazněním aktivního parametru
menu: FUNKCE, KANÁL A/B, HRADLO, ZÁKLADNA
editace: adaptivní krok, výběr ze seznamu
diagnostika (3 strany živých hodnot)
průvodce kalibrací offsetu
varovné překryvy priority 1–5
stavové LED
```

## Verze 2

```
histogram, ADEV, trend, Δf normálů
křížová kontrola, test hardwaru
kontextová nápověda
editace po číslicích
průvodce kalibrací prahu
```

## Verze 3

```
SCPI, Ethernet, vzdálený přístup
limity pass/fail
export na USB
fázový šum
vícejazyčnost
```

**Nezačínej analytickými obrazovkami.** Potřebují buffer v PSRAM a jeho
vyčítání — samostatný kus firmwaru. Verze 1 už je použitelný přístroj.

---

# 16. Co potřebuješ od ostatních instancí

| Co | Od koho |
|---|---|
| Formát dat z FPGA (akumulátory, známky, histogram) | firmware FPGA |
| Rozsahy a jednotky `RF_Level` → dBm | vstupní modul |
| Skutečné meze automatik (útlum, cesta) | vstupní modul |
| Kdy je kalibrace binů platná | firmware FPGA |
| Seznam varovných podmínek a jejich zdrojů | firmware STM32 |

Tyto hodnoty v dokumentu uvádím jako příklady. **Před dokončením UI si
je vyžádej** — hlavně meze automatik a formát dat.
