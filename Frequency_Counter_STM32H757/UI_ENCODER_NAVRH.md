# NÁVRH: přechod UI na encoder-first dle `citac_zadani_UI.md`

Stav: **NÁVRH. Nic z toho zatím není v kódu.** Rozhodnutí padla 2026-08-31.
Vstup: [`citac_zadani_UI.md`](citac_zadani_UI.md). Rozměry dnešních prvků → [`UI_SIZES.md`](UI_SIZES.md).

> Zadání výslovně říká: *„Grafické prvky už existují. Tento dokument neurčuje vzhled —
> určuje strukturu, chování a co musí být kde vidět a proč."* Tenhle dokument je překlad
> té struktury do konkrétní geometrie 800×480 a do dnešní kódové základny.

---

## 0. Čtyři rozhodnutí

| # | Rozhodnutí | Volba |
|---|---|---|
| 1 | Ovládací model | **DVĚ ÚPLNÉ CESTY — encoder i dotyk, ani jedna není podmínkou** |
| 2 | Hlavní obrazovka | **Podmínky A/B + zmenšený Allan** |
| 3 | Menu | **Ploché seznamy, bez rolování** (viz §3) |
| 4 | Varování | **Horní pruh přes šířku + zásah do odečtu** |

🔴 **Doplněno uživatelem 2026-08-31: „musí jít ovládání i jen dotykem, pro případ že
encoder nejde."** To je **silnější požadavek než zadání** — `citac_zadani_UI.md` §1 žádá
jen „každá funkce dosažitelná encoderem, dotyk je zrychlení". Nově musí platit **obě
implikace**: každá funkce je dosažitelná **encoderem samotným** *i* **dotykem samotným**.
Redundance je symetrická, ne jednosměrná. Viz §2.1 — mění to menu i patku.

## 0.1 Co je naopak hotové a nebrání

- ✅ **Fonty.** `codepoints()` v `gen_fonts.js` zahrnuje řecké `0x0391–0x03C9` (Ω σ τ Δ μ)
  i šipky `0x2190–0x2199` (↑ ↓). Plný charset mají `mono_14/16/18/22` a `sans_14/16/18`.
  Hustý řádek `A: 50Ω AC 0dB PŘÍMO ↑ +12,5 mV hyst 10 mV` ≈ 45 znaků × 11 px v `mono_18`
  = **495 px** → vejde se s rezervou. **Žádná práce s fonty**, žádné riziko tiše
  přeskočeného glyfu.
- ✅ **Encoder HW běží** — `enc` (TIM1 encoder mode PA8/PA9 + tlačítko PC13). Chybí jen
  model fokusu, což `CLAUDE.md` sama vede jako otevřené návrhové rozhodnutí.
- ✅ Pásma: hlavička **56**, tělo **360** (56..416), patka **64** (416..480).

---

## 1. Hlavní obrazovka — geometrie

```
y=0    ┌ FREKVENCE A        [GNSS][SYS][SAT]        ⏱ 1,000 s ┐  hlavička 56
y=56   │ (rezerva na varovný pruh, viz §4)                    │
y=56   │        10 000 000,000 12  Hz                         │  odečet 110
y=166  ├──────────────────────────────────────────────────────┤
y=166  │ σ 0,000 08 Hz    N=1000    ▓▓▓▓▓▓▓░░░  72 %          │  34
y=200  ├──────────────────────────────────────────────────────┤
y=202  │ A: 50Ω AC   0dB  PŘÍMO ↑ +12,5 mV  hyst 10 mV        │  32
y=234  │ B: 1MΩ DC  −6dB  PŘÍMO ↑   0,0 mV  hyst 25 mV        │  32
y=266  │ INT · GPSDO LOCK · 25,3 °C                           │  32
y=302  ├──────────────────────────────────────────────────────┤
y=302  │ ADEV σy(τ)  (mini, plná šířka)                       │  114
y=416  └──────────────────────────────────────────────────────┘
y=416    patka: legenda encoderu / dotykové zkratky              64
```

### Co se mění proti dnešku

| Dnes | Nově |
|---|---|
| Mřížka 166..416: Allan 364×242 + statistiky + trend + RF bar | Allan **764×114** (plná šířka, nízký), zbytek do okna ANALÝZA |
| σ@1s / offset / drift jako tři karty | **σ + N přímo pod odečtem** (Zásada 2) |
| Hlavička: pilulky + CPU + čas/datum | Pilulky + **název funkce vlevo** + **hradlo vpravo**; čas/datum ustupuje |
| Nic o vstupu | **Tři řádky podmínek** (A, B, časová základna) |

🔴 **Řádky A/B NESMÍ být jeden `snprintf`.** Encoder na hlavní obrazovce cykluje
*jeden aktivní parametr* (práh A → práh B → hyst A → hyst B → hradlo) a ten musí jít
zvýraznit a překreslit samostatně. Řádek je tedy **pole pozicovaných políček**, každé
s vlastním rectem — jinak by se při každé změně prahu překresloval celý řádek a fokus
by neměl kam sednout.

⚠️ **Allan 764×114 má jiný poměr stran než dnešních 364×242.** Pět dekád v ~90 px
plochy grafu = 18 px/dekádu, což je pod čitelností. **Mini graf proto auto-rozsahem
na 3 dekády** kolem dat; plných 5 dekád zůstane v okně ALLAN (s_view=23).

---

## 2. Model fokusu (encoder-first)

Každá obrazovka dostane **uspořádaný seznam zaměřitelných prvků** a index fokusu.

### Vizuál fokusu — musí se lišit od dvou věcí, které už existují

| Prvek | Dnešní význam | Nesmí se plést s |
|---|---|---|
| `UI_BUTTON_ACTIVE` | *stav* (vybraná volba) | fokusem |
| `tap_flash` 2px accent obrys | *přechodný* problik po stisku | fokusem |

**Návrh: fokus = svislý accent pruh 6 px na levé hraně prvku + podklad `BG_1`.**
Funguje na tlačítku, na řádku seznamu i na políčku uvnitř řádku podmínek, a s ničím
z výše uvedeného nekoliduje. ⚠️ Pruh kreslit **jako fill (REPLACE), ne přes
`prim_internal_blend_px`** — jinak obejde `mark_dirty` a bude problikávat.

### Gesta (zadání §5)

| Akce | Hlavní obrazovka | Jinde |
|---|---|---|
| Otáčení | ladí aktivní parametr | pohyb v seznamu / kurzor |
| Krátký stisk | cyklus aktivního parametru | vstup / potvrzení |
| Dlouhý stisk (1 s) | **AUTO-TRIGGER** | **zpět o úroveň** |
| Dvojklik | vstup do MENU | — |

**Adaptivní krok prahu** (§5): < 2 kroky/s → 0,5 mV · 2–15 → 5 mV · > 15 → 50 mV.
Rozsah ±1024 mV projede rychlým otáčením za ~2 s a doladí na nejmenší krok.
Žádné přepínání režimu.

## 2.1 Dotyková parita — každé gesto musí mít dotykový protějšek

Protože **dotyk sám musí stačit**, nesmí existovat akce dostupná jen encoderem.

| Gesto encoderu | Dotykový protějšek | Stav dnes |
|---|---|---|
| Otáčení = pohyb v seznamu | tap přímo na řádek | ✅ existuje |
| Otáčení = ladění hodnoty | tlačítka `−` / `+` u hodnoty | ✅ existuje |
| Krátký stisk = cyklus aktivního parametru | **tap přímo na políčko** (lepší než cyklus — jde rovnou na cíl) | ⚠️ políčka musí být zaměřitelná, viz §1 |
| Krátký stisk = vstup/potvrzení | tap na položku / `OK` | ✅ existuje |
| Dvojklik = MENU | tlačítko `MENU` v patce | ✅ existuje |
| Dlouhý stisk = zpět | tlačítko `ZPĚT` v patce | ✅ existuje |
| **Dlouhý stisk = AUTO-TRIGGER** | 🔴 **chybí** — viz níže |
| Dlouhý stisk = nápověda | 🔴 **chybí** — položka `? Nápověda` v seznamu |
| **Adaptivní krok podle rychlosti otáčení** | 🔴 **chybí** — `−`/`+` s akcelerací při držení |

### 🔴 Dvě mezery v dotykové vrstvě

Dnešní dotyk je **čistě hranový** (`hranové spouštění` — UiTask pollí FT5x06 ~15 Hz
a reaguje na náběžnou hranu). **Nemá tedy vůbec pojem „držení"**, takže dvě věci nejdou:

1. **Dlouhý dotyk** (protějšek dlouhého stisku encoderu) — nutný pro AUTO-TRIGGER a nápovědu.
2. **Auto-repeat s akcelerací** (protějšek adaptivního kroku) — bez něj by uživatel dotykem
   projížděl rozsah ±1024 mV po 0,5 mV, tedy **4096 klepnutí**.

Obojí = rozšíření touch vrstvy o měření doby držení. ⚠️ Musí respektovat back-off při
chybách I2C4 (dnes 2 Hz při ≥8 chybách v řadě) — při zpomaleném pollu je „držení"
detekovatelné, ale hrana může přijít pozdě; práh dlouhého stisku volit ≥600 ms.

### Důsledek pro patku hlavní obrazovky

Dnes: `RUN/STOP · GATE · CHAN · PERIOD/FREQ · MENU` (5 tlačítek).
Nově **`AUTO` přibývá jako šesté** — jenže GATE a CHAN se stávají zbytečnými, protože
jsou to políčka v řádcích podmínek a tapnou se přímo. Návrh patky:

```
RUN/STOP   AUTO   PERIOD/FREQ   MENU
```

Čtyři tlačítka místo pěti, a `AUTO-TRIGGER` je na jeden tap i na jeden dlouhý stisk —
splňuje §14 („auto-trigger = dlouhý stisk") pro obě cesty.

### 🔴 Rozpor v zadání, který je nutné rozhodnout

**§5 říká „dlouhý stisk = zpět všude kromě hlavní obrazovky".
§13 říká „dlouhý stisk na položce menu zobrazí nápovědu".**
V menu tedy dlouhý stisk znamená obojí. Návrh řešení (k odsouhlasení):
- **zpět** = dlouhý stisk (1 s), jak říká §5 — je častější,
- **nápověda** = velmi dlouhý stisk (2 s), nebo poslední položka každého seznamu `? Nápověda`.

---

## 3. Menu — ploché seznamy

Sedm položek nejvyšší úrovně (⚠️ zadání §7 mluví o „šesti skupinách", ale `KANÁL A`
a `KANÁL B` jsou dle téhož §7 **dvě samostatné položky** → fakticky sedm):

```
FUNKCE ▸
KANÁL A ▸
KANÁL B ▸
HRADLO · STATISTIKA ▸
ČASOVÁ ZÁKLADNA ▸
KALIBRACE ▸
SYSTÉM ▸
```

🔴 **Sedm položek × 60 px (projektové minimum dotykového cíle, 7 mm) = 420 px > 360 px těla.**

**Rolování se ZAMÍTÁ** (původní návrh, přehodnocen po požadavku na samostatný dotyk):
encoder roluje přirozeně, ale **dotyk sám nemá čím** — v projektu není ani drag, ani
fling, ani tažitelný posuvník, a stavět je jen kvůli vrchnímu menu se nevyplatí.

**Řešení: dva sloupce × 4 řádky, výška 84 px, čtení PO SLOUPCÍCH.**

```
┌───────────────────────┬───────────────────────┐
│ FUNKCE              ▸ │ ČASOVÁ ZÁKLADNA     ▸ │
│ KANÁL A             ▸ │ KALIBRACE           ▸ │
│ KANÁL B             ▸ │ SYSTÉM              ▸ │
│ HRADLO · STATISTIKA ▸ │ ? Nápověda            │
└───────────────────────┴───────────────────────┘
```

- **Pořád je to seznam**, ne dlaždice: textové řádky zarovnané vlevo se šipkou `▸`,
  fokus = accent pruh na levé hraně. Jen zalomený do dvou sloupců.
- **84 px na řádek = 9,8 mm** → nad pohodlnou hranicí dotyku (9 mm), ne jen nad minimem.
- **Encoder jde lineárně** dolů levým sloupcem, pak dolů pravým — jednorozměrné pořadí
  zůstává, což je pro encoder to podstatné.
- Osmá pozice zabere `? Nápověda`, čímž se zároveň řeší dotykový protějšek dlouhého
  stisku (§2.1) a rozpor §5 vs §13.

`FUNKCE` je plochý seznam **řazený podle četnosti**, ne podle logiky (Zásada 3):
`Frekvence A` první. Menu si pamatuje poslední volbu.

---

## 4. Varovné překryvy

**Pruh přes celou šířku pod hlavičkou**, barva dle priority, text nejvyššího aktivního
varování + `+N dalších`. Priorita 1–2 navíc **odečet zešedne a přeškrtne se**.
SYS pilulka zůstává jako souhrn.

⚠️ **Geometrie je na doraz.** Zóna odečtu je 110 px, `mono_75` má 75 px výšky glyfu →
po odečtení 34px pruhu zbývá 76 px. Vejde se **s jedním pixelem**. Doporučení: pruh
**30 px** místo 34 (zbude 80 px, 5 px rezerva) a ověřit na HW. Pruh **nesmí způsobit
reflow** — kreslí se přes horní okraj zóny odečtu, geometrie zbytku se nemění.

| Priorita | Varování | Chování |
|---|---|---|
| 1 | `RAIL −5 V MIMO ROZSAH` | zastavit měření, odečet přeškrtnutý |
| 2 | `KALIBRACE NEPLATÍ` | zastavit měření (změnil se hash bitstreamu) |
| 3 | `EXT REF CHYBÍ` | automaticky zpět na OCXO |
| 4–8 | `PŘETÍŽENÍ` · `BEZ SIGNÁLU` · `FIFO PŘETÉKÁ` · `TEPLOTA NEUSTÁLENÁ` · `PRÁH BEZ VÝZNAMU` | informace |

---

## 5. 🔴 Co ze zadání dnes NEMÁ zdroj dat

Zadání předpokládá vstupní modul a protokol v3, které neexistují. Bez nich by se
kreslily **prázdné nebo lživé** údaje:

| Prvek | Blokátor |
|---|---|
| Řádky podmínek A/B (impedance, vazba, útlum, cesta, práh, hystereze) | vstupní modul (STATUS #78) |
| **Průběh hradla** | 🔴 **audit #83 — brána se do FPGA nedostane, okno je vždy 250 ms.** Progress bar by ukazoval postup po bráně, kterou nikdo nenastavil. Vázat na skutečné `gate_ns` z rámce, nebo nekreslit. |
| Kanál B kdekoli | dva kanály = protokol v3 (#77) |
| Cyklus aktivního parametru (práh A/B, hyst A/B, hradlo) | **všech pět dnes nejde nastavit** → encoder by cykloval mrtvé parametry |
| Podlaha přístroje v ADEV | kalibrační relé v modulu |

**Důsledek pro pořadí prací:** encoder-first se vyplatí dělat **hned** (fokus, seznamy,
gesta, varování — to všechno má zdroj dat už dnes), ale **hlavní obrazovka dle §4 má
smysl až se vstupním modulem**. Do té doby by tři čtvrtiny nových řádků byly šedé
placeholdery — což je horší než dnešní obrazovka, ne lepší.

---

## 6. Doporučené pořadí

| Fáze | Obsah | Blokováno čím |
|---|---|---|
| **A** | Model fokusu + gesta encoderu + dvousloupcové seznamy v menu | ničím |
| **A2** | 🔴 **Dotyková parita**: dlouhý dotyk + auto-repeat s akcelerací (dnes je touch čistě hranový) | ničím |
| **B** | Varovné pruhy (priority, které mají zdroj: RF, teploty, GPS, selftest, EXT REF) | ničím |
| **C** | σ + N pod odečet; ADEV mini na plnou šířku | ničím |
| **D** | Řádky podmínek A/B + průběh hradla + cyklus aktivního parametru | vstupní modul, #77, #83 |
| **E** | Editory (adaptivní krok s pruhem polohy, po číslicích), průvodci, nápověda | částečně D |

**Fáze A–C jde udělat na dnešním hardwaru a samy o sobě přiblíží přístroj zadání.**
Fáze D je ta, která čeká na modul.
