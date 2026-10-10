# Audit: encoder fokus — cílený přezkum dnešní úpravy (2026-09-20)

- **Commit:** `c411a14` (HEAD) — ⚠️ auditovaný stav je **pracovní strom nad tímto
  commitem, nekomitováno** (`git status` ukazuje `app_gpsdo.c` jako modifikovaný).
  Tento dokument audituje obsah souboru **tak, jak leží na disku** k 2026-09-20,
  ne poslední commit.
- **Jádro / doména:** CM7, aplikační logika (UiTask)
- **Rozsah:** ⚠️ **NENÍ celý modul 20** (metrologie/encoder/export). Na žádost
  uživatele jde o užší, cílený přezkum jen té části kódu, která se v tomto
  sezení skutečně měnila: `app_gpsdo_handle_encoder()` a jeho bezprostřední
  závislosti (`enc_paint`, `enc_items_n`, `cur_list`, `focus_load`/`focus_store`,
  `view_set`, `btnreg_sync_focus`) v `CM7/app/app_gpsdo.c`. Zbytek modulu 20
  (`meas_present.c`, `screenshot.c`, `phase_noise.c`, `autocal.c`, `meas_math.c`,
  HW vrstva `encoder.c/h`) **není** tímto během dotčen.
- **Projité sekce checklistu:** D (souběh/RTOS — vlastnictví, reentrance),
  E (robustnost — meze, návratové hodnoty)
- **Neprojité (a proč):** A/B/C/F/G/H — modul se dnes nedotkl hodin, DMA,
  dvoujádrového bootu, flash ani žádné periferie; jde o čistou aplikační logiku
  běžící výhradně v UiTasku.

## Souhrn

Dnešní sezení prošlo `app_gpsdo_handle_encoder()` třemi iteracemi: (1) náhrada
ořezávacího clampu modulem pro cyklický fokus (**L-0073**), (2) krátce přidaná
a hned nato odstraněná speciální větev pro GATE/CHAN, kde se objevil a opravil
skutečný chybějící-redraw bug, než uživatel rozhodl, že celá ta funkce (otáčení
mění hodnotu GATE/CHAN přímo) nemá v produktu být (**L-0074**), (3) finální stav
= tahle větev je pryč, rotace na hlavní obrazovce vždy jen listuje fokusem mezi
tlačítky patky se zacyklením na kraji. Všechny tři iterace prošly
build+audit+HW ověřením; finální stav uživatel potvrdil přímo na desce.

Cílený přezkum finálního stavu (meze indexů, vlastnictví vlákna, úplnost revertu,
životní cyklus bezpečnostního sentinelu `s_focus_shown`) **nenašel žádný nový
nález**. Jedna hypotéza, kterou jsem si při čtení okolního mechanismu vytvořil
(možná kolize `s_focus_shown` napříč okny), se po dočtení `view_set()`
(`app_gpsdo.c:3061-3070`) ukázala jako **vyvrácená** — zapsáno níže jako
zkontrolovaný, uzavřený bod, ne jako nález.

**Verdikt: funkční. 0 nových nálezů.**

## Co bylo zkontrolováno a je v pořádku

- **Modulo wraparound** (`app_gpsdo.c:8511-8526`): `n` je v tomto bodě vždy `> 0`
  (guard `if (n <= 0) {...return...}` na `8500`). `%` s kladným dělitelem +
  `if (nf < 0) nf += n;` dá vždy výsledek v `[0, n-1]` bez ohledu na velikost
  `ev.steps` (`int16_t`, ±32767) — žádné přetečení meziv­ýsledku `int`.
- **Rozsah `s_focus`** (`int8_t`, `app_gpsdo.c:3029`) vs. max pozorované `n`:
  nejvyšší `ln` je `MEAS_N=12` (`app_gpsdo.c:3142`), `s_btnreg_n` je shora
  omezené `BTNREG_MAX=24` (`app_gpsdo.c:2985`) → max `n = 36`, hluboko pod
  rozsahem `int8_t` (0..127). Žádné riziko přetečení indexu.
- **Úplnost revertu GATE/CHAN větve:** po odstranění má `screen_main_hit_button`
  jediné volací místo v celém souboru — `app_gpsdo.c:8631`, uvnitř
  `app_gpsdo_handle_touch()` (legitimní, nezměněné použití). Uvnitř
  `app_gpsdo_handle_encoder()` už není citováno vůbec (ověřeno grepem).
  Grep na klíčová slova odstraněné vlastnosti (`bi == 2 || bi == 3`,
  „otáčení mění hodnotu GATE/CHAN") v celém `CM7/` nenašel žádný osiřelý
  komentář ani jinde v kódu, který by ji dál popisoval jako živou — jediný
  zbylý výskyt je nový komentář `app_gpsdo.c:8506`, který ji explicitně
  popisuje jako **odstraněnou historii**, ne aktuální chování.
- **Životní cyklus `s_focus_shown` / kolize napříč okny (vyvrácená hypotéza):**
  Původně jsem se domníval, že `s_focus_shown` (globální `uint8_t` sentinel,
  `app_gpsdo.c:3035`) — porovnávaný jen s AKTUÁLNÍM `s_view` — může při návratu
  DOTYKEM na okno, které encoder už dřív navštívil (`s_focus_shown == s_view`
  shodou čísel), přeskočit ořezávací blok (`8463-8474`) a nechat `s_focus`
  z `focus_load()` (`3037-3041`, samo o sobě bez ořezu proti aktuálnímu `n`)
  neověřený. **Vyvráceno čtením `view_set()`** (`app_gpsdo.c:3061-3070`) —
  jediné místo, kde se `s_view` mění (`3063: if ((int)v == s_view) return;`),
  **vždy** (řádek `3069`) nastaví `s_focus_shown = 0xFF` při KAŽDÉM přechodu,
  ať už ho vyvolal dotyk nebo encoder. `0xFF` nikdy neodpovídá platnému číslu
  okna (`S_VIEW_MAX = 52`), takže ořezávací blok se na první encoderový vstup
  po JAKÉMKOLI přechodu vždy spustí. Komentář u `3030-3034` navíc cituje, že
  tahle přesná třída chyby byla už řešena — **F-0051** (přenesení `focus_load`
  z encoderové obsluhy do `view_set`, aby fungovalo i pro navigaci prstem).
  Mechanismus je tedy robustní; hypotéza byla postavená na neúplném čtení
  (znal jsem jen `8463-8474`, ne `3061-3070`) a je uzavřená bez nálezu.
- **Build + audit:** `./scripts/build.sh Release CM7` → 0 varování. `.text`
  finální stav **609 472 B**, tj. **−112 B proti stavu PŘED dnešním sezením**
  (609 584 B) — čistý úbytek kódu (celá GATE/CHAN větev zmizela), ne jen
  náhrada stejně velkého bloku. `python tools/audit.py` → **92 OK / 0 SELHALO /
  2 s varováním** (nezměněná baseline).
- **Vlastnictví / vlákna:** `app_gpsdo_handle_encoder()` má jediné volací místo
  v celém projektu (`CM7/Core/Src/freertos_task_ui.c`, UiTask, ~100 Hz smyčka,
  ihned po `encoder_poll(&eev)`). `s_focus`, `s_focus_shown`, `s_btnreg[]`
  zapisuje výhradně UiTask — žádné riziko reentrance ani cross-task race.
- **Hledání dvojčete L-0073 jinde v souboru:** grep na vzor `if (X < 0) X = 0;`
  napříč `app_gpsdo.c` našel 10 výskytů; kromě opraveného (dnes `3040`, `8466`
  — obojí legitimní ořez STAVU po `focus_load`/vstupu do okna, ne reakce na
  směrový vstup) jsou zbylé (`446`, `1689`, `1890`, `2087`, `3435`, `3550`,
  `4947`, `9224`) hodnotové/segmentové clampy (procenta, index segmentu
  bargrafu) — nejde o cyklickou navigaci, tedy L-0073 se tam NEuplatňuje.
  Žádné další dvojče nalezeno.
- **HW:** uživatel po flashi + softwarovém resetu (přes sondu, ne fyzický
  power-cyklus — viz „Nezkontrolováno" níže) potvrdil finální chování přímo
  na desce („Ano — přesně takhle to má být"). `status` po každém ze tří flashů
  čistý kromě zdokumentovaného benigního `HF@24000000`/`HFSR=0x80000000`
  artefaktu přítomné sondy (CLAUDE.md, sekce „Dvoujádro / IPC").

## Nezkontrolováno / omezení tohoto běhu

- **Skutečný POWER-CYCLE neproveden** — všechna dnešní HW ověření běžela po
  `STM32_Programmer_CLI -rst` (softwarový reset), ne po odpojení napájení
  (pravidlo 4b / L-0010). Encoderová logika ale neběží v boot-race okně (běží
  až v UiTasku dlouho po bring-upu displeje/ATTINY/SDRAM), takže riziko
  vyplývající konkrétně z tohoto rozdílu je nízké, ne nulové — zapsáno pro
  úplnost, ne jako důvod nedůvěřovat výsledku.
- **Interakce `ev.steps` + `ev.short_press` v JEDNOM pollu** (`8511` běží před
  `8528` — nejdřív posun fokusu, pak aktivace NOVĚ zaostřené položky, ne
  původní): předchází dnešnímu sezení, nezměněno, mimo rozsah tohoto běhu —
  patří modulu 10/11.
- **Zbytek modulu 20** (`meas_present.c`, `screenshot.c`, `phase_noise.c`,
  `autocal.c`, `meas_math.c`, HW vrstva `encoder.c/h`) zůstává ve stavu
  „nálezy zapsány" dle `AUDIT_STATUS.md` — tímto během nedotčen.
- **`docs/LESSONS.md` L-0073 a L-0074 mají lehce posunuté číslo řádku** (psané
  v mezistavu, kód se od té doby dál upravoval) — obsahově platné, přesná
  čísla řádků v nich odpovídají stavu v okamžiku zápisu, ne finálnímu.
