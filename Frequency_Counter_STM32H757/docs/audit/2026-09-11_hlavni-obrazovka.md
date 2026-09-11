# Audit: hlavní obrazovka (`screens/*`)  (2026-09-11)

- **Commit:** `71cd134` (větev `audit/2026-09-09-hodiny-pwr`), pracovní strom bez změn v kódu
- **Jádro / doména:** CM7, UiTask (kreslí výhradně on); data z FpgaTasku přes `g_freq_*`
- **Projité sekce checklistu:** C (umístění bufferů), D (souběh UiTask × FpgaTask × defaultTask),
  E (ošetření chyb, meze polí), G (jen v rozsahu „co modul čte z periferií")
- **Neprojité (a proč):** A, B, F, H — modul nekonfiguruje hodiny, nemluví s CM4,
  nesahá na Flash ani na errata-citlivé periferie

## Přečtené soubory

`CM7/app/screens/screen_main.c` (3 059), `screen_main.h` (172), `screen_main_data.c` (41).
Cíleně dohledáno: `app_gpsdo.c:7899` (druhý výpočet frakční odchylky — kvůli F-0037),
`libprim/src/text.c` (`fmt_frac` konzumenti), `docs/ARCHITECTURE.md` §2–§3.
Umístění bufferů ověřeno `nm` nad `.elf`, ne odhadem ze zdrojáku.

## Souhrn

Hlavní obrazovka je **vykreslovacím a stavovým jádrem přístroje**: staví velké číslo
kmitočtu s dynamickým formátem, drží statistiku (plochý ring + dvě decimační
pyramidy) a obsluhuje ~10 částečných překreslení v různých kadencích (1 Hz, 20 Hz).

**Vykreslovací část je v dobrém stavu a je to doložitelné.** Guard `gate_same`
(STATUS #88) je nasazený konzistentně a na správném místě — volá se na **každý** tik,
ne pod podmínkou, která po usazení přestane platit; každá karta má **vlastní** počítadlo
`reps`; každý partial redraw začíná neprůhledným clearem (`REPLACE` fill nebo
`blit_bg_region`). Dvě dřívější regrese (#88 u `tick_stats_anim`, #137 u formátu periody)
jsou v kódu opravené **i s vysvětlením, proč se staly**.

**Verdikt: podmíněně funkční.** Vada není v kreslení, ale v **metrologii**: frakční
odchylka `y`, ze které žije celá statistika (Allan, drift, offset, trend, histogram,
ℒ(f), prahový monitor), se počítá pevným měřítkem `1e-14`, které platí jen pro
jedinou kombinaci formátu a kmitočtu (**F-0037**). Táž veličina se přitom o pár
souborů dál počítá správně a sype se do **téže** pyramidy.

---

### F-0037 [S1] Frakční odchylka `y` se počítá pevným měřítkem, které platí jen pro 10 MHz a 7 desetin

- **Místo:** `CM7/app/screens/screen_main.c:1125-1136` (`stats_sample()`), řádek `:1129`
- **Popis:** `y` má být `(f − f0) / f0`. Kód místo toho počítá
  `y = off_n * 1e-14f`, kde `off_n` je odchylka v **LSB**. Převod LSB→Hz je ale
  `10^-s_freq_frac`, a `s_freq_frac` i `f0` jsou **obojí dynamické**. Pevná konstanta
  je tedy správná jen v jediném bodě.
- **Důkaz:**
  - `:1127-1129`
    ```c
    /* off_n = odchylka v LSB (LSB=1e-7 Hz), f0=1e7 Hz -> y = off_n*1e-14 */
    int64_t off_n = (int64_t)s_freq_n - (int64_t)s_freq_center;
    float y = (float)off_n * 1e-14f;
    ```
    Komentář sám ty dva předpoklady vyjmenovává — `LSB = 1e-7` (tj. `frac == 7`)
    a `f0 = 1e7`.
  - **Ani jeden z nich není konstantní.** `s_freq_frac` nabývá `7` (hi-res),
    `5` (`x1e5`, větev /16) nebo `6` (SIM) — viz `FREQ_FRAC_HIRES/X1E5/SIM`
    (`:675-677`) a `num_build_for` (`:902`, `:917`). `f0` je `s_freq_nominal_hz`
    = celá část **naměřeného** kmitočtu (`:904`), tedy 32 768 Hz i 1,4 GHz.
  - Správně je `y = off_n · 10^-frac / f0`. Rovnost s `1e-14` nastane jen když
    `f0 = 10^(14-frac)`:

    | `frac` | zdroj | `f0`, při kterém by to sedělo | skutečné `f0` (10 MHz) | chyba |
    |---|---|---|---|---|
    | 7 | hi-res /4 | 10 MHz | 10 MHz | ✅ sedí |
    | 6 | **SIM (výchozí stav)** | 100 MHz | 10 MHz | **10× menší** |
    | 5 | `x1e5` (/16) | 1 GHz | 10 MHz | **100× menší** |

  - 🔴 **Rozhodující důkaz — táž veličina se počítá i správně, o kus dál:**
    `CM7/app/app_gpsdo.c:7899`
    ```c
    screen_main_adev_seed_10s((float)((hz - f0) / f0));
    ```
    To je rekonstrukce dlouhých τ z datalogu a je **matematicky správně**. Obě
    cesty přitom končí ve **stejné** pyramidě: `screen_main_adev_seed_10s`
    → `adev_feed_from(stg, y)`, `stats_sample` → `adev_feed(y)` → `adev_feed_from(0, y)`.
    V pyramidě se tak **míchají vzorky ve dvou různých měřítkách**.
  - Žádná kompenzace dál v řetězci **neexistuje**: `fmt_frac` bere hodnotu přímo
    (`:1912-1914`, `:1919-1921`, `:1809`) a formátuje ji jako frakční odchylku
    (`1,2×10⁻⁸`). `grep "1e-14\|1e14"` nad `screen_main.c` a `app_gpsdo.c` vrací
    jen ten jediný řádek `:1129`.
- **Dopad:** Postihuje **všechno, co ze statistiky žije**: Offset (`stats_mean`),
  σy@1s (`stats_adev`), Drift (`stats_drift`), trend sparkline, histogram, σy(τ)
  tabulku, Allanův graf, ℒ(f) fázový šum (`pn_compute` nad `s_y[]`) a přes
  `screen_main_adev_1s()` → `g_adev_1s` i **prahový monitor** v `alarm.c`.
  V **dnešním** výchozím stavu (SIM, `frac = 6`, 10 MHz) jsou všechna ta čísla
  **10× menší, než odpovídá simulovanému signálu** — tedy přístroj hlásí
  desetkrát lepší stabilitu, než jakou má i ta simulace.
  ⚠️ **Co to dnes NEzpůsobuje, a je poctivé to říct:** headline je podle
  `CLAUDE.md` (#2) do zprovoznění SPI linku simulace, takže se dnes nezkresluje
  *měření*, ale simulovaná čísla. Prahový alarm nad σy je navíc výchozím
  nastavením **vypnutý** (`adev_en`, `alarm.c`) právě proto, že σy je simulované —
  takže ani falešně nepípá.
  🔴 Vada se ale projeví **v plné síle přesně ve chvíli, kdy naběhne FPGA link**
  nebo se bude měřit něco jiného než 10 MHz — a tehdy už půjde o metrologii, ne
  o kosmetiku. Přístroj je čítač; „σy(τ) vypadá věrohodně, ale je o řád vedle"
  je nejhorší možná třída vady pro měřicí přístroj.
- **Reprodukce:** Bez HW: dosaditelné z uvedených řádků. Na desce:
  `fpgasim on 10000000` (REAL cesta, `frac` se přepne) a porovnat σy@1s před
  a po přechodu SIM→REAL — hodnota skočí o dekádu, aniž by se signál změnil.
  Druhý pohled: `fpgasim on 32768` → `y` bude vedle ~305×.
- **Návrh opravy:** Počítat `y` ze stejných veličin jako správná cesta, ne
  z pevné konstanty:
  ```c
  double f  = (double)s_freq_n / (double)pow10_u64(s_freq_frac);
  float  y  = (s_freq_nominal_hz > 0.0)
            ? (float)((f - s_freq_nominal_hz) / s_freq_nominal_hz) : 0.0f;
  ```
  ⚠️ **Lepší je ale vyrobit JEDEN zdroj pravdy** a použít ho na obou místech
  (`stats_sample` i volající `screen_main_adev_seed_10s`) — jinak se ty dvě
  kopie rozejdou znovu, což je přesně to, co se stalo teď (lekce `L-0012`).
  ⚠️ Oprava **změní hodnoty** na displeji o dekádu. To není regrese, ale je
  potřeba to čekat a nezaměnit za novou vadu.
- **Riziko opravy:** nízké technicky (pár řádků, cold path 1×/s, float je tu
  povolený), **střední komunikačně** — čísla se viditelně změní.
  ⚠️ `s_freq_nominal_hz` může být 0 před prvním měřením → dělení nulou musí mít
  guard (výše je).
- **Vztah k lekcím:** **`L-0006`** (konstanta odvozená z jiného provozního
  režimu, než v jakém kód běží — přesně jako `TIMINGR` u I2C) a **`L-0012`**
  (dvě instance téhož výpočtu, opravená jen jedna).
- **Stav:** opraveno 2026-09-11, ⬜ **neověřeno na HW ve smyslu účinku** (blokuje
  to F-0039, viz níže). Opraveno **jinak, než nález navrhoval**: nález nabízel
  opravit vzorec na místě, místo toho vznikl **jeden zdroj pravdy**
  `screen_main_frac_dev(double hz)` a používají ho **obě** cesty —
  `stats_sample()` i `stats_seed_tick()` v `app_gpsdo.c`. Důvod: oprava na místě
  by nechala dvě kopie vzorce, a právě jejich rozejití ten nález vyrobilo.
  ⚠️ Helper pracuje s absolutním kmitočtem, ne s odchylkou v LSB — při 1,4 GHz
  a 7 desetinách je `s_freq_n` ~1,4e16, tedy nad přesným rozsahem double, ale
  zaokrouhlení ~2e-7 Hz je proti nejmenší zajímavé odchylce (~0,014 Hz) o pět
  řádů menší. Jeden vzorec za to stojí.
  Navíc přidán řádek `STATISTIKA: sigma_y@1s` do `status` — σy do teď šlo přečíst
  **jen z displeje**, takže oprava nešla na desce ověřit, jen jí věřit.

---

### F-0038 [S4] `rtc_time_date()` čte sdílený řetězec bez ochrany proti roztržení, kterou tentýž řetězec jinde má

- **Místo:** `CM7/app/screens/screen_main.c:67-78` (`rtc_time_date()`)
- **Popis:** Funkce čte `g_rtc_text_local`, který **zapisuje defaultTask**
  (`rtc_app_tick`, 1 Hz, `strncpy`, bez zámku). Čtení je prosté `strncpy` +
  `strlen >= 19`, takže může zastihnout půlku staré a půlku nové hodnoty.
- **Důkaz:**
  - `:69-70` `char rt[24]; strncpy(rt, (const char *)g_rtc_text_local, sizeof rt - 1);`
    — jediné čtení, žádné porovnání.
  - Protipól **na tomtéž řetězci**: `CM7/FATFS/App/fatfs.c` `get_fattime()` čte
    `g_rtc_text_local` **dvakrát a porovnává** (až 3 pokusy), s komentářem
    „cteni z UartTasku muze zastihnout pulku stare a pulku nove hodnoty…
    Staci precist dvakrat a shodnout se". Tatáž třída rizika je tam ošetřená,
    tady ne.
  - `CLAUDE.md` (RTC / Vlákno) potvrzuje vlastnictví: zápis výhradně defaultTask,
    čtenáři berou `g_rtc_text*` bez zámku.
- **Dopad:** Nejhůř **jeden snímek s pomíchaným časem nebo datem** v hlavičce
  (přes půlnoc teoreticky i špatné datum). Hodiny se překreslují 1×/s, takže se
  to samo opraví v dalším tiku. Je to kosmetika — proto S4, ne výš.
  ⚠️ Pravděpodobnost je navíc nízká: písař tiká 1×/s a čtení trvá desítky
  instrukcí.
- **Reprodukce:** `HYPOTÉZA — ověřit:` staticky doložitelné z uvedených řádků;
  na desce prakticky nepozorovatelné (okno je mikrosekundové).
- **Návrh opravy:** Buď převzít vzor z `get_fattime()` (dvojí čtení se shodou),
  nebo — a to je lepší — **vystavit jedno sdílené čtení** a použít ho na obou
  místech, ať se ty dvě kopie nerozejdou. Třetí, nejlevnější varianta: nechat
  být a **poznamenat u obou**, že jsou párové.
- **Riziko opravy:** nízké; ⚠️ `rtc_time_date` běží v UiTasku na 1Hz cestě,
  takže trojí čtení 20 B je zanedbatelné.
- **Vztah k lekcím:** `L-0012` (ošetřená jen jedna ze dvou instancí).
- **Stav:** opraveno 2026-09-11, ⬜ neověřeno na HW (okno je mikrosekundové,
  pozorovatelné to není). Opraveno podle návrhu — převzat **tentýž vzor** jako
  `get_fattime()` (dvojí čtení se shodou, až 3 pokusy), s komentářem, který na
  párové místo výslovně odkazuje, aby se ty dvě kopie daly porovnat.
  ⚠️ Sdílenou funkci pro obě místa použít **nelze**: `fatfs.c` je generovaný
  a nemá `USER CODE` blok pro include (proto si tam `extern` deklaruje ručně).

---

### F-0039 [S2] Rekonstrukce ADEV z datalogu blokuje ŽIVÉ vzorkování ~1 h 47 min po každém bootu

- **Místo:** `CM7/app/app_gpsdo.c:7858-7868` (rozpočet dávky), `:7889`
  (`stats_seed_tick`), `:7923` (`if (stats_seed_tick()) return;`);
  kadence volajícího `CM7/Core/Src/freertos_task_ui.c:540-543`
- **Popis:** Rekonstrukce dlouhých τ z datalogu běží po dávkách a **dokud běží,
  zablokuje veškeré živé vzorkování statistiky**. Její rozpočet je ale spočítaný
  proti tiku, který v kódu neexistuje — reálně trvá skoro dvě hodiny místo
  deklarovaných dvou minut.
- **Důkaz (změřeno na desce, ne odvozeno):**
  - `:7864` komentář: „nacita se proto ADEV_SEED_CHUNK zaznamu za tik
    (**~20 Hz**), tedy **~400/s** — **48k** zaznamu zabere **~2 min**".
  - `:7868` `#define ADEV_SEED_CHUNK 20u`.
  - Skutečný volající `freertos_task_ui.c:540`:
    `if (HAL_GetTick() - last_stat_s >= 1000) { … app_gpsdo_tick_stats_sample(); }`
    → **1 Hz**, ne 20 Hz. Tedy **20 záznamů/s**, ne 400/s.
  - Skutečný počet záznamů (`status`): **128 719**, ne 48 000; strop
    (`:7879` `cap = 24u * 10000u`) dovoluje až 240 000.
  - → 128 719 / 20 = **6 436 s ≈ 1 h 47 min**.
  - `:7923` `if (stats_seed_tick()) return;` — po celou tu dobu se
    `screen_main_stats_sample()` **nezavolá ani jednou**.
  - **Pozorováno:** `status` po 200 s běhu hlásí
    `STATISTIKA: sigma_y@1s = 0 e-15  (jeste malo vzorku)`, přestože v SIM režimu
    by po 200 vzorcích musela být řádu 1e-9.
- **Dopad:** Po **každém** bootu je celá statistika mrtvá skoro dvě hodiny:
  Offset, σy@1s, Drift, trend sparkline, histogram, σy(τ) tabulka, Allanův graf,
  ℒ(f) i `g_adev_1s` pro prahový monitor. Uživatel to vidí jako „statistiky se
  po zapnutí dlouho neukážou".
  🔴 **A v dnešním stavu je ta práce navíc ZBYTEČNÁ:** `:7894-7895` přeskakuje
  záznamy s `freq_x100000 == 0` a s příznakem SIM — a protože SPI link nikdy
  nenaběhl (#2), takové jsou prakticky **všechny**. Přístroj tedy dvě hodiny
  čte 128 tisíc záznamů z QSPI, aby do pyramidy vložil **skoro nic**, a přitom
  blokuje to, co by data mělo dodávat.
  ⚠️ Není to regrese z tohoto auditu — je to v kódu od zavedení rekonstrukce.
  Odhalilo to až počítadlo přidané opravou F-0037.
- **Reprodukce:** Restart a `status` → `STATISTIKA: sigma_y@1s = 0 e-15` déle
  než pár minut, zatímco `DATALOG … rec` ukazuje >100 000 záznamů.
- **Návrh opravy:** Rozpočet a kadence se musí potkat — jsou tři cesty a **liší
  se cenou i rizikem**, takže to patří uživateli, ne mně:
  1. **Volat `stats_seed_tick()` z rychlejšího tiku** (20 Hz, jak komentář
     předpokládal). Nejblíž původnímu záměru. ⚠️ 20 blokujících QSPI čtení na tik
     při 20 Hz znamená 400 čtení/s v UiTasku — je potřeba ověřit, že to neohrozí
     heartbeat (komentář `:7861` sám varuje, že dávka najednou = IWDG).
  2. **Nechat 1 Hz a zvýšit `ADEV_SEED_CHUNK`.** Jednodušší, ale posouvá to
     blokující práci do jednoho tiku — táž past, před kterou komentář varuje.
  3. **Přeskakovat prázdné záznamy levně**, bez plného `datalog_read_back`
     každého z nich (číst jen `freq_x100000`), případně rekonstrukci úplně
     vynechat, když log neobsahuje ani jedno platné měření. To řeší dnešní
     zbytečnost, ale ne rozpočet pro den, kdy log platná data mít bude.
  ⚠️ Ať se zvolí cokoli, **komentář `:7861-7866` se musí opravit** — dnes uvádí
  tři čísla (20 Hz, 400/s, 2 min), z nichž ani jedno neplatí.
- **Riziko opravy:** střední — dotýká se kadence blokujícího QSPI čtení
  v UiTasku, tedy přesně toho, co hlídá watchdog.
- **Vztah k lekcím:** **`L-0006`** (konstanta odvozená z předpokladu, který
  neplatí — tady kadence volajícího) a **`L-0016`** (rozpočet i měřidlo jeho
  rezervy se navrhují společně; kdyby se doba rekonstrukce od začátku někde
  vypisovala, nikdo by dvě hodiny nehledal).
- **Stav:** opraveno 2026-09-11, ⬜ **neověřeno na HW**. Uživatel zvolil
  **variantu „bulk + brzký konec"**, tedy kombinaci, kterou původní tři návrhy
  neobsahovaly — vznikla až po přečtení `datalog.h`.
  🔑 **Klíčové zjištění při opravě:** bulk cesta už existuje a je prověřená
  (`datalog_read_bulk`, 64 záznamů jedním QSPI příkazem, používá ji web přes IPC),
  a `datalog.h:203` u `datalog_read_back` **přímo varuje**: *„Na PRŮCHOD VÍCE
  ZÁZNAMY použij `datalog_read_bulk` — tohle platí na každý záznam vlastní mutex
  i vlastní QSPI příkaz (změřeno ~173 µs/záznam, zatímco 32 B dat je jen ~7 µs;
  režie je 25× větší než přenos)."* Rekonstrukce dělala přesně to, před čím ta
  hlavička varuje. Žádná z původních tří variant to nezohledňovala, protože jsem
  při psaní nálezu nepřečetl hlavičku funkce, kterou nález cituje.
  **Co se změnilo:**
  1. `datalog_read_bulk`, `ADEV_SEED_BATCHES = 8` dávek za tik →
     8 × (173 µs + 64 × 7 µs) ≈ **5 ms/tik** při **512 záznamech/s**.
     128 719 záznamů → **~4 min** místo 1 h 47 min. **Kadence zůstává 1 Hz**,
     takže žádná nová expozice watchdogu ani 20Hz kreslicí cesty.
  2. **Sonda `seed_worth_it()`** — jedna dávka nejnovějších záznamů; když v ní není
     ani jedno použitelné měření, rekonstrukce se **vůbec nespustí**. To je přesně
     dnešní stav (SPI link nenaběhl → vše `freq == 0` / SIM), takže dnes je doba
     blokování **nulová**. ⚠️ Je to heuristika, ne důkaz, a je to u ní napsané.
  3. `app_gpsdo_stats_seed_progress()` → řádek v `status`
     (`ADEV rekonstrukce: BEZI n/N zaznamu ... <== zive vzorkovani zatim stoji`).
     Bez něj byla doba běhu neviditelná — a právě proto si 1 h 47 min nikdo
     nevšiml. Nová lekce **`L-0021`**.
  ⚠️ Komentář `:7861-7866` přepsán (tři nesprávná čísla nahrazena naměřenými).
  ⚠️ Ošetřeno i zacyklení: `consumed == 0` (chyba čtení) by tik zacyklilo navždy,
  a protože rekonstrukce blokuje živé vzorkování, bylo by to trvalé.
  Ověřeno v obrazu: `app_gpsdo_stats_seed_progress` (56 B), `s_seed_buf` (2 048 B
  v `.bss`, ne na stacku UiTasku), tři nové řetězce, 3× `bl datalog_read_bulk`
  (dřív volal jen IPC).

---

## Souvislosti s dřívějšími nálezy (nezakládám je znovu)

- **τ0 pyramidy předpokládá 1 s, ale vzorkuje se podle `g_freq_seq`** (~4/s
  u reálné FPGA). `:1108-1112` to samo přiznává a odkazuje na MathTask (#27).
  ⚠️ **Interaguje s F-0037**: po opravě měřítka bude σy(τ=1 s) pořád ve
  skutečnosti σy(τ≈0,25 s), dokud se nevyřeší #27. Opravit jedno bez druhého
  statistiku nezachrání — jen ji posune z „o dekádu vedle" na „o dekádu jinde".
- **Headline i statistiky jsou do #2 simulace** — dokumentováno v `CLAUDE.md`.
- **Klasické rozložení je zamrzlá větev** (`:158-166`): `tick_stats_anim`
  i `tick_trend_anim` v něm hned vracejí 0, tedy **bez easingu a bez
  `gate_same`**. Kreslí se jen 1Hz plným redrawem, takže obsah dostanou všechny
  buffery a problikávání nehrozí. Není to nález, ale je dobré vědět, že guardy
  v téhle větvi nejsou.

## Co bylo zkontrolováno a je v pořádku

**Guard „obsah je stejný → nekresli" (STATUS #88)** — `gate_same` (`:379-385`)
je implementovaný správně (`reps = 1` při změně, skip až od `prim_stm32_fb_count()`)
a **nasazený podle vlastního pravidla**: volá se na každý tik, ne pod podmínkou,
která po usazení přestane platit (`:1963-1970` to explicitně opravuje dřívější
regresi), a každá ze tří stat karet má **vlastní** `reps` (`:1885-1887`), takže
dokreslování jedné neumlčí ostatní. ✓

**Clear před partial redrawem** — `draw_stat_card_value` (`:1943`) začíná
`prim_fill_rect(..., PRIM_BLEND_REPLACE)`, `blit_bg_region` jde přes `prim_blit`.
Obojí jde přes DMA2D → `mark_dirty`. ✓ (Souvisí s F-0032 z modulu 8: kdyby
některý z těch clearů byl poloprůhledný, dirty by se neoznačil — tady není.)

**Meze polí** — `num_layout` (`:730-823`): `NUM_SEG_MAX = 12`, nejhorší případ
4 skupiny celé části + 5 zlomku; `s_seps[gn-1] = '\0'` při `gn = 12` sedí přesně
na poslední index (11 separátorů + NUL = 12 B). Ověřeno dosazením, žádné přetečení. ✓
`freq_fill_segments` (`:1048-1060`) má `memset(d,'0',…)` jako hardening pro případ,
že by `Σ s_seg_len` přesáhlo `s_disp_total`. ✓

**Přetečení `uint64` v hi-res dopočtu** — `freq_frame_to_lsb` (`:696-723`) počítá
**dlouhým dělením** (celá část, pak číslice po jedné) místo `num × 10^frac / den`,
s komentářem, že to druhé přeteče už kolem 10 MHz. Fallback `x1e5` naopak **dělí**
(`10^(5-frac)`), protože násobení by při ~4 GHz přeteklo. Obojí dosazeno a sedí. ✓

**Násobitel reciproké dvojice se neodhaduje, ale ověřuje** (`:707`
`fpga_freq_hires_mul`) — jediný zdroj pravdy, sdílený s `sdram_log.c`. Když
nesedí žádný, hi-res se **nepoužije** (radši 5 poctivých desetin než 15 špatných). ✓

**Formát periody se přestavuje i uvnitř jedné frekvenční dekády** (`:1019-1027`) —
oprava dřívější vady, kdy `9,99 MHz → 1,00 MHz` posunulo periodu o dekádu,
formát se nepřestavěl a `freq_fill_segments` **tiše zahodila vedoucí číslici**. ✓

**Seqlock čtení měření z FpgaTasku** (`:990-993`) — `do { … } while (seq != g_freq_seq)`
nad `g_freq_*`. FpgaTask (Normal) může preemptnout UiTask (BelowNormal), takže
re-check je na místě a je správně (čte se seq první, kontroluje poslední). ✓

**Vlastnictví stavu** — `st` (mode/chan/gate/running) mění **výhradně UiTask**;
SCPI z UartTasku zapisuje jen požadavek (`g_ui_cfg_req` + `_pend`) a
`screen_main_apply_cfg_req()` ho aplikuje v UiTasku (`:309-329`). ✓

**Umístění bufferů (`nm` nad `.elf`)** — `bg_cache` `0xC0840000` (768 000 B),
`g_mask_b` `0xC08FB800`, `g_mask_a` `0xC0908000`, vedle `s_glyph_atlas`
`0xC0800000` (256 kB); `.sdram` končí na `0xC0914800`, tedy **1,08 MB ze 4 MB**
regionu. Statistika je v AXI SRAM: `s_tr` 4 716 B, `s_hist` 15 840 B, `s_adev` 648 B.
Nic z toho není v DTCM (`L-0001` splněno konstrukcí). ✓
⚠️ `bg_cache` leží v **Device paměti** (`.sdram` je mimo MPU, audit F-0012), ale
čte ji jen DMA2D přes `prim_blit` a zapisuje se přes `prim_fb_t` cíl (halfword
přístupy) — nezarovnaný 32bitový přístup, který by na Device paměti byl
UsageFault, tu nevzniká. ✓

**Sanitace vstupů z GPS** — HDOP se před `snprintf` ořízne na [0, 999] (`:501`)
s komentářem proč. ✓

## Nezkontrolováno / omezení tohoto běhu

- **Nečetl jsem řádek po řádku celých 3 059 řádků** — kompletně jsem prošel
  hlavičku, stavovou a statistickou část (`:1-1230`) a jádro částečných
  překreslení (`:1880-2120`); zbytek (Allan/histogram/trend renderery,
  `render_body_grid`, footer) jsem procházel cíleně přes strukturu a grep na
  rizikové vzory (`gate_same`, clear před redrawem, meze polí). Je to vědomý
  kompromis hloubka × šířka; rizikové cesty jsou pokryté, kosmetika renderů ne.
- **Správnost Allanovy matematiky** (overlapping ADEV, MDEV/HDEV, TDEV, MTIE)
  jsem **neověřoval proti referenci** (NIST SP1065) — to je samostatná práce
  a bez správného `y` (F-0037) by stejně neměla smysl.
- **Nic neběželo na HW jako důsledek tohoto auditu** — kód nebyl měněn
  (`git status` na `*.c`/`*.h` prázdný).
- **Vizuální stránka** (rozměry, čitelnost) neposuzována — to je `UI_SIZES.md`.
