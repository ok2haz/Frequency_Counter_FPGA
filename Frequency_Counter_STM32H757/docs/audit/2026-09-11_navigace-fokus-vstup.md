# Audit: navigace, model fokusu, vstup a tiky (`app_gpsdo.c` — strojovna)  (2026-09-11)

- **Commit:** `193c333` (větev `audit/2026-09-09-hodiny-pwr`), pracovní strom bez změn v kódu
- **Jádro / doména:** CM7, UiTask (kreslí výhradně on); část API čte UartTask a defaultTask
- **Projité sekce checklistu:** C (umístění dat), D (souběh, RTOS, priority), E (ošetření chyb,
  meze polí, čekací smyčky)
- **Neprojité (a proč):** A, B, F, G, H — modul nekonfiguruje hodiny ani napájení, nemluví
  přímo s CM4, nesahá na Flash ani na periferní registry a nemá žádné volání `HAL_*`

## Rozdělení modulu 10 (rozhodnuto na začátku tohoto sezení)

`AUDIT_STATUS.md` u modulu 10 ukládal zvážit další dělení. `app_gpsdo.c` má **9 031 řádků
a 310 funkcí** — to je 2,8× modul 9 a na jedno sezení se to poctivě přečíst nedá. Dělím ho
podle **třídy rizika**, ne podle velikosti:

| modul | obsah | rozsah | proč zvlášť |
|---|---|---|---|
| **10** (tento) | navigace (`nav_push`/`nav_back`/`render_view`), model fokusu + registr tlačítek, vstup (dotyk, encoder), tiky, screensaver, splash, varovný pruh, `app_gpsdo_init`/`flush` | ≈ 2 800 ř. | sdílený stav, cross-task API, dispatch — sem patří S1/S2 |
| **11** (příště) | ~45 funkcí `app_gpsdo_render_*` + jejich kreslicí helpery | ≈ 6 200 ř. | převážně kreslení a formátování — očekávané S3/S4 |

## Přečtené soubory

`CM7/app/app_gpsdo.c` — celá strojovna: 1–380, 555–580, 2630–2860, 2874–3080, 4056–4080,
4160–4220, 5528–5590, 7085–7190, 7406–7600, 7800–8300, 8640–8710, 9020–9031.
Cíleně dohledáno: `CM7/Core/Src/freertos_task_ui.c` (kadence volajícího),
`CM7/Core/Src/syscfg.c:150-175` (cross-task čtení `app_gpsdo_meas_ui_get`),
`CM7/Core/Src/sensor_hist.c:82-110` (reentrance `sensor_hist_series`),
`CM7/Core/Src/datalog.c:286-330` (`find_head` — cena blokujícího QSPI),
`CM7/app/screens/screen_main.c:2420-2425` (`screen_main_strike_reading`).
Umístění dat ověřeno `nm`/`.map` nad `CM7/Release/H757_LED_CM7.elf`, ne odhadem ze zdrojáku.

## Souhrn

Strojovna dělá tři věci: **dispatch podle `s_view`** (které okno se kreslí, tiká a jak reaguje
na dotyk), **model fokusu** (jednorozměrný prostor „položky okna + registr tlačítek", po kterém
se pohybuje encoder) a **koalescenci flipů** (`s_dirty` + `app_gpsdo_flush`).

**Datová vrstva je v pořádku a je to doložené.** Žádné DMA buffery, žádné volání `HAL_*`, žádná
čekací smyčka bez meze — všechny tři smyčky v modulu mají tvrdý strop (`e < 9`, `e > -20`,
`j >= 0`). Všechny statické objekty leží v AXI SRAM `0x2402xxxx` (ověřeno `nm`), takže `L-0001`
je splněno konstrukcí. Publikace výsledku self-survey do `double` globálů je správně obalená
`taskENTER_CRITICAL()` a je u ní napsané proč (`:4203-4215`) — to je přesně ten druh místa,
kde se jinak chyby dělají.

**Verdikt: podmíněně funkční.** Nic tu neshodí přístroj. Vada je soustředěná do **účetnictví
fokusu a do diagnostiky**, a má společného jmenovatele: *kód, který má držet dvě věci v souladu,
je sám napsaný dvakrát.* Konkrétně je `s_view` rozvětvené **pěti** nezávislými tabulkami
(46 `case` + 32 větví + 10 `case` + 10 `case` + 57 testů), takže se rozejít musí — a už se
rozešly. Nejzávažnější je **F-0046**: funkce, která má srovnat fokus po doteku, počítá v jiném
indexovém prostoru než zbytek modelu a ukládá do paměti okna trvale špatnou hodnotu.
Druhá v pořadí je **F-0047**: diagnostika „otevřelo se okno?" nepokrývá 17 ze ~45 oken včetně
hlavní obrazovky a MENU, a protože si drží předchozí hodnotu, **aktivně lže**.

---

### F-0046 [S2] `btnreg_sync_focus()` píše do `s_focus` index v jiném prostoru, než ve kterém ho zbytek modelu čte

- **Místo:** `CM7/app/app_gpsdo.c:8229-8239` (`btnreg_sync_focus()`), volána z `:8285`
  (`app_gpsdo_handle_touch()`, první řádek)
- **Popis:** Fokus má **jeden spojený prostor**: indexy `0 … ln-1` jsou položky okna,
  `ln … ln+s_btnreg_n-1` jsou tlačítka z registru (`ln` = počet položek seznamu).
  `btnreg_sync_focus` ale uloží **surový index do registru**, tedy hodnotu bez posunu o `ln`.
  V každém okně se seznamem ukazuje výsledek na úplně jiný prvek.
- **Důkaz:**
  - Definice prostoru — `:8119-8133` (`enc_paint()`):
    ```c
    int ln = enc_items_n(L);
    if (idx < ln) { ...položka seznamu...; return; }
    idx -= ln;                      /* ⬅ tlačítka začínají AŽ na ln */
    if (idx >= (int)s_btnreg_n) return;
    ```
  - Totéž při aktivaci — `:8200-8205`:
    ```c
    if (s_focus >= ln) {
        prim_rect_t r = s_btnreg[s_focus - ln];
    ```
  - A rozporný zápis — `:8231-8236`:
    ```c
    for (int i = 0; i < s_btnreg_n; i++) {
        prim_rect_t r = s_btnreg[i];
        if (x >= r.x && ...) {
            s_focus = (int8_t)i;         /* ⬅ chybí `+ ln` */
            focus_store((uint8_t)s_view);
            return; } }
    ```
  - `ln` je nenulové v pěti oknech (`:8111-8116` + `:8146-8148`): **MENU** `ln = MENU_N = 4`,
    **MĚŘENÍ** `ln = MEAS_N = 12`, **NÁSTROJE** `ln = TOOLS_N = 7`, **FUNKCE** `ln = FUNC_N = 12`,
    **NÁPOVĚDA** `ln = HELP_N`. Jen okna bez seznamu (`ln == 0`) vycházejí náhodou správně.
  - 🔴 Ironie, která z toho dělá nález a ne překlep: komentář nad tou funkcí (`:8226-8228`)
    říká *„Bez tohohle by se obě ovládací cesty rozešly — po tapu by encoder pokračoval tam,
    kde byl před ním, ne tam, co uživatel právě zmáčkl."* Funkce dělá **přesně to**, čemu má
    bránit. To je učebnicový `L-0008` (komentář slibuje chování, které tělo ruší).
- **Dopad:** Deterministické a **trvalé** — špatný index se přes `focus_store()` uloží do
  `s_focus_of[s_view]` a přežije odchod z okna. Příklad, který jde projít prstem:
  v MENU (4 dlaždice + tlačítka ZPĚT / RESTART / ? NÁPOVĚDA) klepne uživatel na **RESTART**;
  uloží se `s_focus = <index RESTARTu v registru>` ∈ {0,1,2}, což jsou v spojeném prostoru
  **dlaždice** Nastavení / Diagnostika / System Health. Při příštím vstupu do MENU a prvním
  otočení knoflíku se rámeček objeví na dlaždici, ne na tlačítku, které uživatel naposled
  použil. Neshodí to přístroj a po pár západkách se to vizuálně srovná, ale porušuje to
  projektové pravidlo **„dvě úplné ovládací cesty se nesmí rozejít"** — a rozchází je právě ta
  funkce, která je má držet spolu.
- **Reprodukce:** MENU → prstem RESTART → NE → odejít ZPĚT na hlavní obrazovku → znovu MENU →
  otočit encoderem o jednu západku. Fokus je na dlaždici, ne na RESTARTu.
  (Bez HW dokazatelné přímo z uvedených řádků — indexové prostory se liší o `ln`.)
- **Návrh opravy:** jednořádková — `s_focus = (int8_t)(enc_items_n(L) + i);`. ⚠️ Háček:
  `btnreg_sync_focus` nemá `L` (výběr seznamu je lokální proměnná v `app_gpsdo_handle_encoder`,
  `:8146-8148`). **Správná oprava proto začíná vytažením výběru seznamu do sdílené funkce**
  (např. `static const menu_list_t *cur_list(void)`), kterou pak použijí obě místa — jinak
  vznikne třetí kopie téhož mapování `s_view → seznam` a nález se zopakuje jinde
  (`L-0018`: dvě místa počítající touž věc nejsou duplicita kódu, ale dvě pravdy).
- **Riziko opravy:** nízké — čistě UI stav, nic v měřicí ani datové cestě. Ověřit lze
  encoderem na desce (rámeček musí po tapu sedět na stisknutém prvku).
- **Vztah k lekcím:** **`L-0008`** (komentář popisuje chování, které tělo ruší),
  **`L-0018`** (mapování `s_view → seznam` má mít jeden zdroj pravdy).
- **Stav:** opraveno 2026-09-11, ⬜ **neověřeno na HW**. Opraveno **podle návrhu, ale
  i s tím háčkem, který nález předvídal**: samotné `+ ln` nešlo napsat, protože
  `btnreg_sync_focus` neměla `L`. Vzniklo proto `cur_list()` jako **jediný zdroj**
  mapování `s_view → seznam` (dosud lokální výraz v obsluze encoderu) a používají ho
  obě místa. Ověřeno v obrazu: `app_gpsdo_handle_touch` má inlinovaný výběr seznamu
  (`cmp #12 / #44 / #48`) následovaný `bl enc_items_n` → **`add r0, r2`** (= `ln + i`)
  → `strb` do `s_focus`. Řešeno **v jednom commitu s F-0047 a F-0051**, protože všechny
  tři sahají na totéž účetnictví.

---

### F-0047 [S3] Diagnostika „otevřelo se okno?" nepokrývá 17 ze ~45 oken — a protože si drží předchozí hodnotu, aktivně lže

- **Místo:** `CM7/app/app_gpsdo.c:172-181` (`g_ui_view`, `g_ui_view_changes`, `window_first()`);
  výpis `CM7/Core/Src/freertos_task_uart.c:2085-2086`
- **Popis:** Počítadlo se aktualizuje **jen** ve `window_first()`. Sedmnáct render funkcí ale
  volá rovnou `window_prep()` a `s_view` si nastaví samo, takže do diagnostiky nikdy nezapíšou.
  Hodnota přitom není nulována — zůstane v ní **předchozí** okno.
- **Důkaz:**
  - Jediné místo zápisu, `:175-181`:
    ```c
    static int window_first(uint8_t view_id)
    {
        window_prep();
        int f = (s_view != (int)view_id);
        if (f) { g_ui_view = view_id; g_ui_view_changes++; }
        return f;
    }
    ```
  - `grep -c` nad souborem: **39×** `window_first(`, **21×** `window_prep()`. Po odečtení
    jednoho volání uvnitř `window_first` samotného (`:177`) zbývá **17 render funkcí**, které
    `s_view` nastavují bez zápisu do diagnostiky (řádek `window_prep()` → následující `s_view =`):
    `:350` → **0 (HLAVNÍ OBRAZOVKA)**, `:2621` → 7 Nastavení, `:2795` → 11 splash,
    `:3064` → **12 MENU**, `:3072` → **44 MĚŘENÍ**, `:3176` → **48 NÁSTROJE**, `:3299` → 45 TI,
    `:3687` → 13 potvrzení restartu, `:3860` → 15 Kalibrace, `:5386` → 25 Příklady animací,
    `:5900` → 20 Selftest, `:6121` → 36 Displej, `:6952` → 26 Spektrogram, `:7017` → 27 Efekty,
    `:7183` → 0 (`app_gpsdo_clear`), `:7265` → **49 FUNKCE**, `:7366` → **50 NÁPOVĚDA**.
  - Účel té proměnné je zapsaný přímo nad ní (`:169-171`): *„bez ní nešlo z UART poznat, jestli
    se okno vůbec OTEVŘELO — »po stisku dlaždice se nic nestane« mohlo znamenat jak nepřijatý
    dotyk, tak okno, které se otevře a hned zavře."*
- **Dopad:** Nástroj neodpovídá na otázku, kvůli které vznikl, a **odpovídá špatně místo aby
  mlčel**. Nejhorší dvojice: uživatel je na **hlavní obrazovce** (nejčastější stav přístroje) —
  `status` hlásí poslední navštívené okno. A když klepne na **MENU** a to se otevře,
  `g_ui_view` se nezmění → z UART to vypadá jako nepřijatý dotyk, tedy **přesně ten mylný
  závěr, kterému měla diagnostika zabránit**. V projektu, kde se metoda opírá o to, že `status`
  se dá věřit (`L-0011`, a měření MMC čítače u ETH na špatné adrese), je to podstatné.
- **Reprodukce:** Na desce: z hlavní obrazovky `status` → řádek `UI: okno s_view=…` ukazuje
  jiné číslo než 0. Pak MENU → `status` → číslo i `zmen` beze změny.
- **Návrh opravy:** Zápis přesunout tam, kde `s_view` **skutečně** vzniká, ne na jednu
  z cest k němu. Nejlevnější varianta bez refaktoru: `static void view_set(uint8_t v)`
  (nastaví `s_view` + diagnostiku) a nahradit jí všech ~45 přiřazení `s_view = N`;
  `window_first()` pak zůstane jen na „je to první vstup?".
  ⚠️ Nestačí jen doplnit `window_first` do těch 17 funkcí — některé z nich (splash,
  `app_gpsdo_clear`, modal) nemají sémantiku „živě překreslované okno".
- **Riziko opravy:** nízké technicky (jen zápis do dvou diagnostických proměnných), ale
  **širokodotyková** — mění se ~45 míst. Vhodné dělat mechanicky a ověřit `git diff --stat`.
- **Vztah k lekcím:** **`L-0011`** (nástroj, kterému se věří, se musí nejdřív sám ověřit —
  tady měřidlo nepokrývá většinu měřeného rozsahu) a **`L-0017`** (co se rozhodnu nezaznamenat,
  musí jít poznat; zde se to tváří jako platný údaj).
- **Stav:** opraveno 2026-09-11, ⬜ **neověřeno na HW**. Opraveno **doporučenou (širší)
  variantou**: vzniklo `view_set(uint8_t)` a **všech 53 přiřazení `s_view = N;`** jím prošlo;
  `window_first()` diagnostiku už neplní (jinak by se `g_ui_view_changes` počítalo dvakrát).
  ⚠️ **Sentinely `s_view = 0xFF` / `-1`** (vynucení plného renderu po změně tématu/presetu)
  zůstaly **nedotčené a je to záměr** — nejsou to přechody na jiné okno, jen zneplatnění,
  po kterém stejně přijde skutečný `view_set()` z render funkce. Kdyby šly přes `view_set`,
  vyrobily by falešný přechod v diagnostice.
  Ověřeno v obrazu: `view_set` je skutečná funkce (76 B) s **53 volajícími** a její tělo
  dělá přesně to, co má — `focus_store` → `str s_view` → `strb g_ui_view` → `++
  g_ui_view_changes` → inlinovaný `focus_load` → `s_focus_shown = 0xFF`.

---

### F-0048 [S3] `nav_push()` při plném zásobníku tiše zahodí položku — a `nav_back()` pak vede jinam

- **Místo:** `CM7/app/app_gpsdo.c:306-308` (`s_nav_stack[6]`, `nav_push()`), `:320-324` (`nav_back()`)
- **Popis:** Zásobník má 6 míst a přetečení se **mlčky ignoruje**. Po zahození se `nav_back()`
  vrátí o úroveň jinam, než odkud se okno otevřelo, a nic to nehlásí.
- **Důkaz:**
  - `:306-308`:
    ```c
    static uint8_t s_nav_stack[6];
    static int     s_nav_sp = 0;
    static void nav_push(uint8_t from) { if (s_nav_sp < 6) s_nav_stack[s_nav_sp++] = from; }
    ```
    Podmínka je správná (pole se nepřepíše), chybí ale **jakýkoli záznam**, že se událost stala.
  - Nejhlubší dnes dosažitelná cesta má **5 úrovní**:
    hlavní → MENU (`nav_push(0)`) → Nastavení (`nav_push(12)`) → Animace (`nav_push(7)`) →
    Efekty (`nav_push(24)`) → Status ribbon (`nav_push(27)`). Rezerva je tedy **jediná úroveň**.
  - Že rezerva je těsná, dokládá i rozložení volání: `nav_push(7)` je v souboru **8×**
    (`grep -oE "nav_push\([0-9]+\)" | sort | uniq -c`), tedy Nastavení je nejrozvětvenější
    uzel a každé nové podokno pod ním tu rezervu spotřebuje.
  - Srovnání ve stejném souboru: **registr tlačítek to dělá správně** — `:2907` nastaví
    `s_btnreg_ovf = 1` a `status` to hlásí (`app_gpsdo_btnreg_stats`). Stejný vzor zde chybí.
- **Dopad:** Dnes pravděpodobně latentní (5 < 6). Projeví se až přidáním jedné úrovně zanoření,
  a to jako **„ZPĚT vede na špatné okno"** — tedy symptom, který se hledá v navigační logice,
  ne v kapacitě pole. Bez počítadla není jak ho odlišit od chyby v `render_view`.
  ⚠️ Není to hypotetické: `goto_view`/`render_view` už jednou způsobily přesně tenhle symptom
  (viz komentář `:316-319`), takže by se hledalo znovu na špatném místě.
- **Reprodukce:** `HYPOTÉZA — ověřit:` přidat šestou úroveň zanoření a projít ZPĚT.
  Dnes na desce nereprodukovatelné, protože tak hluboká cesta neexistuje.
- **Návrh opravy:** Minimální a v duchu toho, co už soubor umí: `s_nav_ovf` + zveřejnit ho
  vedle `app_gpsdo_btnreg_stats()` do `status`. Teprve pak (volitelně) zvětšit pole na 8.
  **Samotné zvětšení pole není oprava** — jen posune tichou hranici dál.
- **Riziko opravy:** nízké — přidání příznaku nemění chování.
- **Vztah k lekcím:** **`L-0017`** (tichý přeskok je přípustný jen s počítadlem) a
  **`L-0015`** (modul, který zná svou mez, ji musí na rozhraní vynutit, ne jen odvozovat);
  **`L-0016`** (mez a měřidlo její rezervy se navrhují společně — tady je mez bez měřidla).
- **Stav:** otevřeno

---

### F-0049 [S3] `render_view()` nezná okna 49 (FUNKCE), 50 (NÁPOVĚDA) a 13 (potvrzení restartu)

- **Místo:** `CM7/app/app_gpsdo.c:7085-7136` (`render_view()`), volající `:7177`
  (`app_gpsdo_touch_dead()`)
- **Popis:** Dispatch má 46 `case`, ale tři existující okna v něm chybí a spadnou do
  `default: app_gpsdo_render_main()`.
- **Důkaz:**
  - `:7134` — `default: app_gpsdo_render_main(); break;`
  - Okna 49 a 50 přitom existují a uživatel v nich může setrvat: `:7265` (`s_view = 49`,
    „FUNKCE MERENI", dlaždice v rozcestníku MĚŘENÍ, `MEAS_ITEMS[0]` `:3005`) a `:7366`
    (`s_view = 50`, „? NAPOVEDA" z patky MENU, `:8656-8658`).
  - Jediná cesta, kudy se to projeví, je `:7169-7178`:
    ```c
    if (!s_banner) return;
    s_banner = 0;                      /* sbernice ozila -> uklid po banneru */
    if (s_view == 8) { ...screensaver... }
    else { render_view(s_view); }      /* ⬅ pro 49/50/13 vykreslí hlavní obrazovku */
    ```
  - ✅ Ostatní volající jsou v pořádku a ověřil jsem to: `nav_back()` (`:323`) může popnout
    jen hodnoty, které někdo pushnul, a to je množina {0,1,2,3,7,12,15,18,24,27,35,44,48}
    (`grep -oE "nav_push\([0-9]+\)"`) — **všechny mají `case`**. `app_gpsdo_tick_warn()`
    (`:7534`, `:7542`) je nad `render_view` volá jen při `s_view == 0` (guard `:7524`).
- **Dopad:** Úzký, ale reálný: když po výpadku ožije I2C4 (banner „DOTYK NEDOSTUPNY" se uklízí),
  uživatel sedící v okně FUNKCE nebo NÁPOVĚDA je **vyhozen na hlavní obrazovku**. Modal 13
  se zavře, což je nejspíš žádoucí, ale je to náhoda, ne rozhodnutí.
- **Reprodukce:** `HYPOTÉZA — ověřit:` otevřít NÁPOVĚDU, vyvolat výpadek I2C4 (dle
  `CLAUDE.md` stačí jeden halt ladicí sondou) a počkat na obnovu → okno se změní na hlavní
  obrazovku. ⚠️ Tenhle pokus **zabije I2C4 až do power-cyklu**, takže se nevyplatí
  kvůli nálezu téhle závažnosti; ze zdrojáku je to dokazatelné i tak.
- **Návrh opravy:** doplnit `case 49` a `case 50`. U `case 13` se **rozhodnout vědomě**:
  buď doplnit, nebo do `default` napsat, že modal se po obnově záměrně zavírá.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **`L-0012`** (symetrické instance) — je to přímý důsledek **F-0050**.
- **Stav:** otevřeno

---

### F-0050 [S4] `s_view` je rozvětvené pěti nezávislými tabulkami, které se musí držet v souladu ručně

- **Místo:** `CM7/app/app_gpsdo.c` — `render_view()` `:7085-7136`, `app_gpsdo_tick()`
  `:7546-7584`, `app_gpsdo_exit_screensaver()` `:2726-2738`, `touch_pm_control()` `:8247-8267`,
  `app_gpsdo_handle_touch()` `:8283-9031`
- **Popis:** Pět míst nezávisle rozhoduje podle `s_view`. Přidání okna vyžaduje ruční zásah
  v každém z nich a nic to nekontroluje.
- **Důkaz (změřeno, ne odhadnuto):**

  | tabulka | rozsah | velikost |
  |---|---|---|
  | `render_view()` | `:7085-7136` | **46** `case` |
  | `app_gpsdo_tick()` (živý redraw) | `:7546-7584` | **32** větví `s_view ==` |
  | `app_gpsdo_exit_screensaver()` | `:2726-2738` | **10** `case` |
  | `touch_pm_control()` (auto-repeat ±) | `:8247-8267` | **10** `case` |
  | `app_gpsdo_handle_touch()` | `:8283-9031` | **57** testů `s_view ==` |

  - Že to není teoretické: komentář `:316-319` popisuje, že přesně tahle duplicita už jednou
    způsobila vadu — *„Dříve tu byl `goto_view` = vlastní switch s_view→render fn. Zrušen
    2026-08-29 ve prospěch sdíleného `render_view` … dva rozjeté switche zapomínaly na nová
    okna a ZPĚT/probuzení z nich vedlo na hlavní obrazovku."*
  - A přesto **druhá tabulka existuje dál**: `app_gpsdo_exit_screensaver()` (`:2726-2738`) je
    zúžená kopie `render_view`. Tady je to **vědomé a odůvodněné** (`:2720-2725`: plošné
    `render_view(s_prev_view)` zamrzlo dotykovou vrstvu) — takže to není chyba, ale je to
    nezaplacený dluh: důvod je popsaný, ale příčina („které okno a proč zamrzlo") nikoli.
  - **F-0049 je přímý výsledek téhle struktury** — dvě okna chybí v jedné z pěti tabulek.
- **Dopad:** Žádný okamžitý. Je to **generátor budoucích nálezů**: každé nové okno je pět
  příležitostí něco zapomenout a čtyři z těch pěti selžou tiše (okno se prostě chová jinak,
  nic nehlásí).
- **Reprodukce:** neaplikovatelné (strukturální nález, doložený počty výše).
- **Návrh opravy:** ⚠️ **Nedoporučuji plošný refaktor** — zařízení funguje a `render_view`
  vs. screensaver má doložený důvod se lišit. Levnější a bezpečnější je **kontrola místo
  sjednocení**: tabulka `s_view → {render fn, tick fn, ±ovladač}` jako pole `static const`,
  z níž se odvodí `render_view` i `app_gpsdo_tick`, plus `_Static_assert` na její délku
  vůči `S_VIEW_MAX`. `handle_touch` a screensaver ať zůstanou, jak jsou.
  Alternativa za cenu jednoho buildu: skript do `scripts/check_lessons.sh`, který vypíše
  `s_view` hodnoty přítomné v jedné tabulce a chybějící v druhé.
- **Riziko opravy:** **střední až vysoké** u plošné varianty (dotýká se každého okna),
  nízké u kontrolní varianty.
- **Vztah k lekcím:** **`L-0018`** (dvě místa počítající touž věc jsou dvě pravdy čekající,
  až se rozejdou) a **`L-0012`** (symetrické instance se musí opravovat společně).
- **Stav:** otevřeno — **kandidát na skupinu C (odložit)**, viz triáž

---

### F-0051 [S4] Paměť fokusu per okno se přeskočí, když se uživatel do okna vrátí bez použití encoderu

- **Místo:** `CM7/app/app_gpsdo.c:8154-8167` (`s_shown_view` v `app_gpsdo_handle_encoder()`)
- **Popis:** `focus_load()` má **jediné** volání — uvnitř podmínky `s_shown_view != s_view`.
  `s_shown_view` se ale nuluje jen na čtyřech místech **uvnitř obsluhy encoderu**; navigace
  dotykem ho nechá být. Když se uživatel vrátí do okna, kde `s_shown_view` už tu hodnotu má,
  uložený fokus se nenačte.
- **Důkaz:**
  - `:8155-8157` — jediné volání `focus_load()` v celém souboru
    (`grep -n "focus_load"` → `:2919` definice, `:8157` volání):
    ```c
    static uint8_t s_shown_view = 0xFF;
    if (encoder_seen() && s_shown_view != (uint8_t)s_view) {
        s_shown_view = (uint8_t)s_view;
        focus_load((uint8_t)s_view);
    ```
  - Reset `s_shown_view = 0xFF` je na `:8175` (dlouhý stisk = ZPĚT), `:8180` (dvojklik),
    `:8206`, `:8212`, `:8217` — **všech pět je v obsluze encoderu**. `nav_back()`,
    `render_view()` ani žádná `app_gpsdo_render_*` na něj nesahají.
  - Posloupnost, která to vyrobí: encoder v MENU (`s_shown_view = 12`) → **prstem** dlaždice
    (`:8665` nastaví `s_focus = i`, `s_view = 44`) → **prstem** ZPĚT (`:9028` `nav_back()` →
    `render_view(12)`), přičemž `btnreg_sync_focus` (`:8285`) mezitím přepsal `s_focus`
    indexem tlačítka ZPĚT v okně 44 → zpět v MENU je `s_shown_view == s_view == 12`, blok se
    přeskočí a `focus_load(12)` se **nezavolá**.
- **Dopad:** Zadání UI §7 („menu si pamatuje poslední volbu") neplatí, když se do okna vrátíte
  bez otočení knoflíkem — a to je při ovládání prstem běžná cesta. Fokus začne tam, kde skončil
  v *jiném* okně. Sčítá se to s **F-0046** (uložená hodnota je navíc z jiného indexového
  prostoru), takže obojí je rozumné opravit najednou.
  ⚠️ Ne­shodí to nic; po první západce se fokus posune do platného rozsahu (`:8158-8159`
  ořízne `s_focus` na `n-1`).
- **Reprodukce:** viz posloupnost v důkazu — projde se prstem a jedním otočením encoderu.
- **Návrh opravy:** `s_shown_view` přestat používat jako „kde jsem naposled kreslil fokus"
  a načítat fokus při **vstupu do okna**, ne při první události encoderu — tedy `focus_load()`
  volat z místa, kde se `s_view` mění (viz jednotné `view_set()` navržené u **F-0047**;
  obě opravy tím sdílí jeden zásah).
- **Riziko opravy:** nízké, ale **nedělat odděleně od F-0046/F-0047** — všechny tři sahají
  na totéž účetnictví a oddělené opravy by se pletly.
- **Vztah k lekcím:** **`L-0018`** (jeden zdroj pravdy pro „kde jsem").
- **Stav:** opraveno 2026-09-11, ⬜ **neověřeno na HW**. Opraveno **jinak, než nález
  navrhoval**: nález chtěl volat `focus_load()` při vstupu do okna — to se stalo, ale
  `s_shown_view` se **nezrušilo**, jen dostalo užší a poctivější roli. Je z něj
  file-scope `s_focus_shown` = „okno, ve kterém je značka fokusu už vykreslená", které
  nuluje `view_set()` (tedy i při navigaci prstem). Obsluha encoderu tak přestala
  `focus_load()` volat úplně a řeší jen vykreslení. Důvod, proč proměnnou nezrušit:
  pořád plní původní účel — *první* otočení knoflíkem musí značku zobrazit, i když se
  index nezmění.

---

## Co bylo zkontrolováno a je v pořádku

Tenhle seznam je pro reprodukovatelnost auditu stejně důležitý jako nálezy.

**C — umístění dat (ověřeno `nm`/`.map`, ne ze zdrojáku):**
- Všechny statické objekty modulu leží v **AXI SRAM D1** (`0x2402xxxx`): `s_fb` `0x24028db4`,
  `s_btnreg` `0x24028ac8` (**192 B = 24 × 8 B**, sedí s `BTNREG_MAX 24` a `prim_rect_t`),
  `s_nav_stack` `0x24028d98` (6 B), `s_focus_of` `0x24028a8e` (0x34 = 52 B = `S_VIEW_MAX`),
  `s_survey` `0x24028a38` (64 B). **Nic v DTCM** → `L-0001` splněno konstrukcí.
- Modul nemá žádný DMA buffer ani volání `_DMA(` → `L-0002` se ho netýká.

**D — souběh a RTOS:**
- Publikace výsledku self-survey (`:4202-4216`) je v `taskENTER_CRITICAL()` **správně**:
  `g_survey_lat/lon` jsou `double` (2 zápisy) a čte je defaultTask (syscfg). Zdůvodnění je
  u kódu včetně toho, proč čtenáře chránit netřeba (nižší priorita UiTasku).
- `app_gpsdo_warn_active()` volá **UartTask** (`freertos_task_uart.c:2078`), zatímco `warn_eval()`
  jinak běží v UiTasku. Ověřil jsem reentranci: `warn_eval` má jen lokální `warn_t w[12]`
  a `warmup_ready()` → `sensor_hist_series()` (`sensor_hist.c:82-110`) zapisuje **výhradně do
  bufferu volajícího**, žádný sdílený scratch. ✅ Bez nálezu.
- `app_gpsdo_btnreg_stats()`, `app_gpsdo_encoder_draws()`, `app_gpsdo_ui_counters()` — čte je
  UartTask, píše UiTask; jsou to monotónní diagnostické čítače, roztržené čtení je neškodné.
- `g_ui_view` / `g_ui_view_changes` jsou `volatile` (`:172-173`, `freertos_shared.h:273-274`). ✅
- Kreslení volá **výhradně UiTask**; UART mění jen `g_screen_req` (ověřeno grepem `app_gpsdo_`
  napříč `CM7/Core/Src/` — mimo UiTask jsou jen čtecí/diagnostické funkce a `app_gpsdo_selftest`,
  což je čistá logika).
- Kadence volajícího (`freertos_task_ui.c`) odpovídá komentářům: `tick` 500 ms, `tick_clock`
  100 ms, `tick_signal` 100 ms, `tick_freq` 50 ms, `tick_anim` 50 ms, statistika 1 s. ✅

**E — ošetření chyb a meze:**
- **Žádné volání `HAL_*`** v modulu → `L-0003` se ho netýká.
- **Žádná čekací smyčka bez meze** → `L-0004` splněno; všechny tři `while` mají tvrdý strop
  (`:903` `j >= 0`, `:3588` `e < 9`, `:5098` `e > -20`).
- `warn_eval()` (`:7446-7467`) má kapacitu `WARN_MAX 12` proti 8 podmínkám **a u toho napsáno,
  proč rezerva** (dřív bylo `w[8]` přesně na doraz). To je správně udělaný limit.
- `btnreg_observer()` (`:2903-2912`) přetečení **hlásí** (`s_btnreg_ovf` → `status`) a
  deduplikuje podle rectu. Vzorová implementace `L-0017` — a měřítko pro F-0048.
- `focus_load/store` (`:2919-2927`) mají bounds check proti `S_VIEW_MAX`; `s_focus` je `int8_t`
  a největší spojený prostor je `FUNC_N (12) + BTNREG_MAX (24) = 36` → bez přetečení. ✅
- Modal potvrzení restartu (`:8667-8674`) **polyká všechny ostatní tapy** a je u toho napsáno
  proč (jinak by propadly na neviditelný `BACK_RECT`). ✅

**Vykreslovací pravidla (návaznost na modul 8):**
- `focus_ring_draw/clear` (`:8081-8098`) kreslí **vyplněnými pruhy** (`prim_fill_rect` /
  `prim_blit`), tedy DMA2D cestou, která dělá `mark_dirty` — ne zaobleným tahem přes
  `prim_internal_blend_px`. U kódu je zapsané, že opačná varianta to 2026-08-31 rozbila. ✅
- `screen_main_strike_reading()` (`screen_main.c:2420-2425`) je `PRIM_BLEND_REPLACE` fill →
  `mark_dirty` proběhne, přeškrtnutí neproblikává. ✅ (Kontrolováno kvůli F-0032 z modulu 8.)
- `list_item_draw()` (`:2949-2966`) začíná `REPLACE` fillem → pravidlo „každý partial redraw
  musí začít clear" splněno. ✅
- `warn_draw()` (`:7480-7502`) záměrně **nečistí** pozadím (ležel by přes Allanovu kartu) a je
  volaný jako poslední před flipem z `app_gpsdo_flush()` (`:7996-8017`); výplň je `REPLACE`,
  takže dirty rect vzniká. ✅

**Watchdog (blokující práce v hlídaném tasku):**
- Okna s blokujícím `datalog_read_back` se renderují **jen při vstupu / změně presetu**, ne
  z 500ms tiku — ověřeno v kódu: GRAFY `:1690-1697` (change-key stabilní per preset),
  ANALÝZA `:4779` (`ana_recompute()` pouze pod `if (first)`). KVALITA GPS (38) v tiku vůbec
  není. ✅ To je přesně to, co `CLAUDE.md` vyžaduje.
- `calib_save()` (`:8953`, `:9006`) a `setup_save/erase/load` (`:8563-8565`) blokují QSPI
  z UiTasku, ale jen na stisk tlačítka a `w25q wait_ready()` od 2026-07-20 ustupuje
  scheduleru — vědomě přijaté, zapsané v `CLAUDE.md`.
- `run_selftests()` z UiTasku (`:8724`) — čistá logika, `CLAUDE.md` tenhle třetí volající
  eviduje včetně toho, že souběh se nečeká.
- `datalog_set_store()` (to jediné, co spustí blokující `find_head`) se z tohoto modulu
  **nevolá** — ověřeno grepem.

## Nezkontrolováno / omezení tohoto běhu

- **~45 funkcí `app_gpsdo_render_*` a jejich kreslicí helpery (≈ 6 200 ř.) = modul 11.**
  Tento běh je nečetl; dotkl se jich jen tam, kde to potřeboval jako důkaz (`window_prep`
  vs. `window_first`, blokující QSPI v tiku, seznam `s_view` přiřazení).
- **Dlouhý řetěz `app_gpsdo_handle_touch()` (`:8283-9031`, 57 testů `s_view`)** je pročtený
  jen v místech, která se týkají navigace a fokusu (hlavní obrazovka, MENU, modal, záložky,
  konečný `BACK_RECT`). Per-okno obsluha tapů patří k modulu 11.
- **Nic z tohoto modulu neběželo na HW v rámci tohoto auditu** — nálezy jsou statické.
  F-0046, F-0047 a F-0051 jsou přesto dokazatelné ze zdrojáku (indexové prostory, počty
  volání), F-0048 a F-0049 jsou dnes latentní.
- **`HELP_N` nebylo dohledáno číselně** (je v části souboru patřící modulu 11); pro F-0046
  stačí, že je nenulové, což plyne z `enc_items_n()` `:8114`.
