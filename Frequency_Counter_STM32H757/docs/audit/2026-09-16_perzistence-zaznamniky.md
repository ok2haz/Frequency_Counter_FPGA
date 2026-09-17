# Audit: perzistence a záznamníky (2026-09-16)

- **Commit:** `8c87146`
- **Jádro / doména:** CM7 / D1 (QSPI W25Q), zápisy z **defaultTask**, čtení z
  **UiTask**, **UartTask** a **defaultTask**; jeden kanál přechází přes IPC na **CM4**
- **Soubory (čteny celé):** `CM7/Core/Src/datalog.c` (696 ř.),
  `CM7/Core/Src/flightrec.c` (741 ř. — obsahuje **i celý `errlog`**),
  `CM7/Core/Src/syscfg.c` (318 ř.), `CM7/Core/Src/setup.c` (162 ř.),
  `CM7/Core/Src/calib.c` (83 ř.), hlavičky `datalog.h`, `errlog.h`, `flightrec.h`,
  `w25q_map.h`. **Celkem 2 456 ř.**
  Dohledáni všichni producenti a konzumenti: `w25q_store.c` (kontrakt blobu),
  `w25q.c` (blokující/nevýtěžná cesta QSPI), `freertos.c` (pořadí initu, smyčka
  defaultTasku, `gpio_guard_tick`), `freertos_task_uart.c` (`qspi_req_service`,
  příkazy `errlog`/`flightrec`/`datalog`), `freertos_task_ui.c`,
  `freertos_task_sensors.c`, `freertos_task_fpga.c`, `usart.c` (ISR producent),
  `screen_main.c`, `app_gpsdo.c` (okno CHYBY, okno Datalog), `rtc.c` (co
  doopravdy drží BKP), `ipc.c` + `ipc_shared.h` (kanál v17),
  `CM4/LWIP/App/httpd_min.c` (`build_errlog_json`).
- **Projité sekce checklistu:** **D** (souběh mezi úlohami a ISR, `volatile`,
  zásobníky), **E** (návratové hodnoty, tiché selhání, chování po resetu),
  **F** (flash: granularita, pořadí erase/write, power-safe zápis, mapa regionů),
  částečně **B** (kanál `errlog` přes SRAM4) a **C** (umístění bufferů).
- **Neprojité (a proč):** **A** — modul nekonfiguruje ani nečte žádné hodiny;
  **G** — QSPI ovladač je modul 6; **H** — modul nezávisí na revizi silikonu.

## Rozsah a metoda

Modul vznikl proto, že tvrzení v `AUDIT_STATUS.md:59` („tím je auditovaný veškerý
vlastní kód projektu") **neplatí** — mimo moduly 1–15 leželo ~4 500 ř. vlastního
kódu a 2026-09-13 k nim přibyl celý nový podsystém **`errlog`** (FW v0.9.0,
IPC v17), který audit nikdy neviděl.

Průchod byl **řádek po řádku** přes všech pět `.c` (na 2 456 ř. to jde).
Nad rámec čtení bylo provedeno:

- **mapa regionů W25Q přepočtena numericky** (ne z komentářů) — viz „Zkontrolováno
  a je v pořádku";
- **umístění všech bufferů dohledáno v obrazu** (`nm --print-size` nad
  `CM7/Release/H757_LED_CM7.elf` z 2026-09-13 19:02), ne odhadnuto ze zdrojáku;
- **rámce všech funkcí modulu změřeny** nad `.elf` (`objdump -d`, `sub sp, #N`)
  podle **L-0035**;
- `python tools/audit.py` (GCC **14.3.1**): **92 OK / 0 selhání / 2 soubory
  s varováním** = baseline, **žádné varování v tomto modulu**.

## Souhrn

Modul je **jediná cesta, kterou se cokoli v přístroji dostane přes power-cyklus**:
nastavení (`syscfg`), kalibrace (`calib`), profily (`setup`), dlouhodobý záznam
měření (`datalog`), kontext před pádem (`flightrec`) a nový trvalý zaznamník chyb
(`errlog`). Jádro je psané dobře: ruční serializace nezávislá na překladači, CRC
u každého záznamu, **power-safe pořadí zápisu** (payload první, hlavička poslední),
odvození pozice zápisu ze `seq` místo metadat, krátké timeouty QSPI mutexu v úlohách
s watchdogem a **dvoustupňový zápis errlogu** (ISR → RAM ring → defaultTask → flash),
který je přesně tou opravou, jakou u `flightrec` pořád chybí (otevřený **F-0018**).

Nálezy mají **dva jmenovatele**:

1. **Věci, které mají přežít reset, ho nepřežijí nebo se ztratí.** `F-0089` [S1]:
   tři nastavení datalogu (zap/vyp, úložiště, perioda) se z flash obnovují
   **jen při studeném startu**, přestože nejsou v BKP — po každém *teplém* resetu
   (reflash, Menu→Restart, watchdog) se tiše vrátí na výchozí hodnoty.
   `F-0091` [S3]: `flightrec` si po 64 dumpech začne **mazat vlastní nejnovější
   záznam při každém bootu**. `F-0092` [S3]: záznam o pádu v `errlog` ztratí
   jméno tasku, tedy přesně to, kvůli čemu crash black-box vznikl.

2. **Komentář popisuje ochranu, kterou kód nedělá** (podruhé v projektu, viz
   **L-0028**). `F-0090` [S2]: komentář u `datalog_init` říká, že mutex chrání
   souběžné čtenáře — čtenáři ho ale **nikdy neberou** a `s_be` se nuluje *před*
   `s_ready`, takže mezi těmi dvěma příkazy je okno pro dereferenci NULL.
   `F-0091`: komentář slibuje „smaž nejstarší", kód maže sektor 0.
   `F-0094`: `calib_save()` zapíše do trvalé historie „uložena kalibrace"
   **dřív**, než zjistí, jestli se uložila.

**Verdikt: podmíněně funkční.** Za normálního provozu se všechno zapisuje
a čte správně; vady se projeví po resetu, po naplnění regionu a v souběhu.
Nic z toho dnes neshodí přístroj deterministicky — jediná cesta k pádu
(`F-0090`) je úzké okno dvou instrukcí.

---

## Fáze oprav (2026-09-17) — skupina A, 6 nálezů v 5 commitech

Triáž §F5.0 byla předložena uživateli; schválena **skupina A** (jednoznačná
příčina, malý diff, žádná změna chování za normálního provozu).

| commit | nálezy | co se změnilo |
|---|---|---|
| `6d1b6e5` | **F-0089** [S1] + **F-0093** [S3] | obnova datalogu přesunuta nad `if (g_syscfg_bkp_valid) return;`; nový `datalog_cfg_quiet(bool)` potlačí falešné `ERRLOG_K_CFG` po dobu obnovy |
| `a80caed` | **F-0090** [S2] | `s_ready` se shazuje první a zvedá poslední, obě hranice s `__DMB()`; čtyři čtenáři čtou `s_be` jednou do lokálu |
| `15aa8d3` | **F-0094** [S3] | `errlog_put` v `calib_save()` až za zápis a podmíněn úspěchem |
| `e583432` | **F-0097** [S3] | tři `_Static_assert(sizeof(blob) <= W25Q_STORE_MAX_BLOB)` |
| `a9af9de` | **F-0099** [S4] | `errlog_erase()` vrací `bool`, při chybě stav neposouvá; oba volající hlásí výsledek |
| `165023c` | (pojistka k F-0089) | `scripts/check_lessons.sh` — rozdílová kontrola toho, co smí být pod early returnem |

**Ověřovací řetězec (§F5.2):** `./scripts/build.sh Release BOTH` **0 varování**,
`python tools/audit.py` **92 OK / 0 selhání / 2 s varováním** (baseline,
gcc 14.3.1), CM7 `.text` 604 560 → **604 752 B** (+192), CM4 beze změny
(nic z toho v obrazu CM4 není), `.bss` +1 B.

**Důkazy v obrazu** (ne jen „přeloženo"):
- `syscfg_load` volá `datalog_cfg_quiet → set_enabled → set_period_s → set_store
  → datalog_cfg_quiet` a **teprve pak** `ldrb g_syscfg_bkp_valid` + `bne` na
  epilog (`8018126`…`8018130`) — tím je F-0089 doložená, ne jen tvrzená;
- `datalog_init` má oba `dmb sy` přesně mezi `strb` příznaku a následujícím
  zápisem (`800203e`, `8002112`);
- `calib_save`: `cbnz s_store.ready` → `bl w25q_store_write` → `cmp/beq` →
  `bl errlog_put` s `r0=11`/`r1=5`, tedy zápis se při neúspěchu přeskočí;
- `errlog_erase` má nově tři podmíněné skoky (dřív prostá smyčka), oba nové
  řetězce jsou v `.elf`.

🔑 **Pozitivní kontroly** (`L-0020`, `L-0039`) — celkem pět, protože kontrol
i asertů bylo pět:
- každý ze tří `_Static_assert` ověřen dočasným nafouknutím struktury o 4096 B →
  jednosouborový překlad skutečně skončil hláškou „nevejde do jednoho sektoru";
- nová sekce `check_lessons.sh` ověřena na **obou** podobách vady (vložené
  `datalog_set_enabled(...)` pod return → hlásí `datalog_set_enabled()`; vložené
  `g_fx_enabled = b.fx_en;` → hlásí `g_fx_enabled`), a nad zdravým stromem mlčí.

**Lekce:** **L-0050** (F-0089), **L-0051** (F-0090), **L-0052** (F-0093),
**L-0053** (F-0094 + F-0099); F-0097 je **rozšíření L-0026**, ne nová lekce —
je to druhý výskyt téže vady.

⬜ **NEOVĚŘENO NA HW** — čeká na flash a **POWER-CYKLUS**. `IPC_VERSION` se
nemění (zůstává 17), takže banky jdou flashovat nezávisle; CM4 obraz se
nezměnil vůbec.

🔑 **Co ověřit na desce** (z konzole, bez sondy):
1. **F-0089:** `datalog off` → počkat >2 s → **Menu → Restart** → `status` musí
   pořád hlásit `DATALOG W25Q OFF`. Totéž `datalog interval 60` → po restartu
   musí zůstat 60 s. (Před opravou se obojí vrátilo na `ON` / 10 s.)
2. **F-0093:** po **power-cyklu** `errlog dump 5` — hned za záznamem `BOOT`
   už nesmí být `NASTAV interval logu …`.
3. **F-0094:** `errlog dump` po neúspěšném ULOŽIT v okně Kalibrace (těžko
   vyvolatelné) — nepovinné.
4. **F-0099:** `errlog erase` musí vypsat `errlog: smazano`; z okna CHYBY
   tlačítko SMAZAT musí na konzoli vypsat `errlog: mazani (z UI) OK`.
5. Nic se nesmí rozbít: `status` → `DATALOG`, `GPIO HLIDAC: 0 oprav`,
   `Reset: power-on`.

---

### F-0089 [S1] Nastavení datalogu se po teplém resetu tiše vrátí na výchozí hodnoty

- **Místo:** `CM7/Core/Src/syscfg.c:266` (`if (g_syscfg_bkp_valid) return;`)
  vs. `:278`, `:281`, `:282` (funkce `syscfg_load()`)
- **Popis:** `syscfg_load()` je rozdělená na dvě části. Nad `return` patří pole,
  která **nejsou v BKP** a flash je jejich jediný zdroj (a proto se aplikují vždy);
  pod `return` pole, která BKP drží a při teplém resetu z ní přijdou novější.
  Tři pole datalogu (`datalog_en`, `datalog_period_s`, `datalog_store`, přidaná
  2026-09-07 s magicem `"SCG0"` → `"SCG1"`) skončila **pod** `return`, přestože
  v BKP nejsou.
- **Důkaz:**
  - `syscfg.c:196-197` sám formuluje pravidlo: *„Cteme VZDY (i warm reset):
    `g_fx_enabled` NENI v BKP, flash je jeho jediny zdroj. Ostatni pole se z flash
    aplikuji jen pri studenem startu (nize)."*
  - Co BKP doopravdy drží, je vyčteno z `rtc.c:89-120`: **DR1** = `g_ui_cfg`;
    **DR2** = `g_brightness`, `g_sound_muted`, `g_autodim_en`, `g_autodim_sec`;
    **DR6** = `g_theme_idx`, `g_lang_en`, `g_tz_offset_h`, `g_tz_auto`,
    `g_anim_enabled`. **Žádné pole datalogu tam není**, a `rtc.c` slovo „datalog"
    vůbec neobsahuje (ověřeno grepem).
  - Řádky pod `return` odpovídají přesně obsahu BKP (`syscfg.c:269-277`, `:283`)
    — **až na ty tři** na `:278`, `:281`, `:282`.
  - `g_syscfg_bkp_valid` se nastavuje na 1 pokaždé, když v DR2 sedí magic
    (`rtc.c:96-102`), tedy po **každém** resetu, který nepřeruší napájení
    backup domény.
- **Dopad:** Po teplém resetu — tedy po **každém reflashi**, po **Menu → Restart**
  (`NVIC_SystemReset`, `freertos.c:753`), po **watchdog resetu** i po NRST — jede
  datalog s hodnotami přeloženými do obrazu: `s_enabled = true` (`datalog.c:40`),
  `s_store_pref = DATALOG_STORE_AUTO` (`:48`), `s_period_s = 10 s` (`:47`).
  Uživatel, který záznam **vypnul**, ho má po restartu zase zapnutý a píše se do
  flash; kdo si vynutil `FLASH`, dostane `AUTO`; kdo nastavil periodu 60 s, dostane
  10 s (6× víc zápisů a jiný τ₀ pro rekonstrukci Allanovy pyramidy,
  viz `datalog_adev_stage()`). Přes power-cyklus přitom nastavení funguje, takže
  vada vypadá jako náhodná.
- **Reprodukce (bez power-cyklu, z konzole):** `datalog off` → počkat >2 s
  (debounce 1,5 s + zápis, `syscfg.c:43`) → `status` ukáže `DATALOG W25Q OFF` →
  Menu → Restart (nebo jakýkoli SW reset) → `status` ukáže **`DATALOG W25Q ON`**.
  Totéž s `datalog interval 60` → po restartu zpět 10 s.
- **Návrh opravy:** přesunout tři řádky (`:278`, `:281`, `:282`) **nad** `return`
  na `:266`, do skupiny „není v BKP → aplikuj vždy", vedle `fx`/`meas`/`survey`/
  `mon`/`layout`/`enc_div`. Pořadí *perioda před úložištěm* musí zůstat (komentář
  `:279-280`). 🔴 **Opravit ZÁROVEŇ s F-0093** — po přesunu by se dnešní falešné
  záznamy `ERRLOG_K_CFG` generovaly nejen při studeném startu, ale při **každém**
  resetu.
- **Riziko opravy:** nízké (přesun tří řádků), ale **nikdy bez F-0093**.
- **Vztah k lekcím:** nová lekce po opravě — „když se do struktury přidá pole,
  urči, do KTERÉ poloviny podmíněného bloku patří, a to podle toho, kde ta hodnota
  ještě žije (BKP/flash), ne podle toho, kam se dopsalo nejsnáz."
- **Stav:** opraveno 2026-09-17 v `6d1b6e5` (spolu s F-0093; lekce **L-0050**). Opraveno přesně podle návrhu — tři řádky přesunuty nad `return`. Navíc přibyla regresní kontrola v `scripts/check_lessons.sh` (`165023c`), kterou nález nenavrhoval.

---

### F-0090 [S2] `datalog_init()` nuluje `s_be` před `s_ready`; čtenáři `s_be` dereferencují bez zámku

- **Místo:** `CM7/Core/Src/datalog.c:371-374` (`datalog_init()`), čtenáři
  `:512` (`datalog_get_status`), `:527` (`datalog_read_back`), `:563`
  (`datalog_read_bulk`), `:596-598` (`datalog_format_status`)
- **Popis:** Re-init nastaví `s_be = NULL;` a **až potom** `s_ready = false;`.
  Všichni čtenáři testují `s_ready` a pak dereferencují `s_be` — a **žádný z nich
  QSPI mutex nebere**, takže je re-init před nimi nechrání. Mezi těmi dvěma
  příkazy platí `s_ready == true && s_be == NULL`.
- **Důkaz:**
  - Zápis: `datalog.c:374` → `s_be = NULL; s_ready = false;` (v tomto pořadí,
    na jednom řádku, obojí NEvolatile, bez bariéry).
  - Čtení bez mutexu, guardované jen `s_ready`:
    `:512` `out->backend = s_ready ? s_be->name : "--";`
    `:524-527` `if (!s_ready || …) return false;` … `s_be->capacity` **před**
    `osMutexAcquire` na `:530`;
    `:556` `if (!s_ready || …) return 0;` … `:563` `const uint32_t cap = s_be->capacity;`
    **před** `osMutexAcquire` na `:570`;
    `:595-598` `if (!s_ready) {…}` … `s_be->name`, `s_be->capacity`.
  - Komentář `datalog.c:371-372` tvrdí: *„⚠️ Nulovat AZ pod mutexem — jinak by
    soubezny ctenar (UI/UART) videl `s_be == NULL` uprostred re-initu a hlasil
    „NEDOSTUPNE"."* Mutex ale chrání jen zapisovatele proti zapisovateli;
    **čtenář ho nikdy nebere**, takže popsaná ochrana neexistuje — a skutečný
    následek není „hlásil NEDOSTUPNE", ale **dereference NULL**.
  - Kdo je současně v běhu: re-init pouští **UartTask** přes
    `qspi_req_service()` → `datalog_set_store()` → `datalog_init()`
    (`freertos_task_uart.c:134-137`, `datalog.c:91`). Čtenáři běží
    v **UiTask** (`app_gpsdo.c:1480`, `:4524`, `:4823`, `:5041`, `:8030`),
    v **UartTask** (`freertos_task_uart.c:812`, `:1634`, `sd_export.c:1280`,
    `scpi.c:1088`) a v **defaultTask** (`ipc.c:285`).
  - Reachability je těsná: tlačítko na přepnutí úložiště **je v okně Datalog**
    (`app_gpsdo.c:9082` nastaví `g_datalog_store_req`) a totéž okno je živé
    a v každém tiku volá `datalog_get_status()`. Ty dvě úlohy tedy běží
    současně právě kvůli tomu jednomu stisku.
- **Dopad:** Dereference NULL ukazatele v UiTasku/defaultTasku → **HardFault**
  (`0x00000000` je na H7 mimo namapovanou paměť pro čtení `s_be->name`).
  Projeví se jako náhodný pád po přepnutí úložiště logu. Okno je široké 1–2
  instrukce, takže je to vzácné — ale determinovaně existující.
- **Reprodukce:** `HYPOTÉZA — ověřit:` v okně Datalog opakovaně přepínat
  úložiště (AUTO→FLASH→SD→…) a sledovat `status` → `Reset:`/crash black-box na
  `HF@…`. Vynutit okno lze i uměle: dočasně vložit `osDelay(1)` mezi
  `s_be = NULL;` a `s_ready = false;` a přepnout úložiště s otevřeným oknem
  Datalog — pak je pád deterministický. (Ověřovací zásah, ne oprava.)
- **Návrh opravy:** minimální a bezpečná varianta — **prohodit pořadí**:
  `s_ready = false; __DMB(); s_be = NULL;`, a totéž na konci (`s_be` nastavit
  dřív než `s_ready = true`, `datalog.c:390`). Doplňkově (levné, čitelné) přidat
  do čtenářů `|| s_be == NULL` ke stávajícímu testu `!s_ready` — obrana proti
  příštímu přeuspořádání. Brát mutex ve čtenářích typu `datalog_get_status`
  **nedoporučuji**: volá se z UiTasku 2×/s a mutex drží i minuty trvající
  `datalog_erase_all()`.
- **Riziko opravy:** nízké (dva řádky, žádná změna chování za normálního běhu).
- **Vztah k lekcím:** **L-0028** (komentář popisuje obranu, kterou kód nedělá —
  **potřetí** v projektu), **L-0022** (obrana je neúplná, dokud u ní není
  vyjmenované, kdo ji musí zavolat).
- **Stav:** opraveno 2026-09-17 v `a80caed` (lekce **L-0051**). Provedeny OBĚ varianty z návrhu, ne jen minimální: prohození pořadí s `__DMB()` **a** čtení `s_be` jednou do lokálu ve všech čtyřech čtenářích. Mutex se ve čtenářích záměrně nebere (návrh to nedoporučoval).

---

### F-0091 [S3] `flightrec_init()` maže sektor 0 místo sektoru za nejnovějším — po 64 dumpech si log ničí vlastní nejnovější záznam

- **Místo:** `CM7/Core/Src/flightrec.c:176-204` (`flightrec_init()`), rozhodující
  je `:197-204`
- **Popis:** Init hledá **první volný (smazaný)** sektor. Když žádný nenajde
  (region plný), smaže **natvrdo `W25Q_FLIGHTREC_BASE`, tedy sektor 0** — bez
  ohledu na to, kde leží nejstarší a kde nejnovější dump. Po jednom oběhu regionu
  sektor 0 drží ten **nejnovější** záznam a init ho zahodí.
- **Důkaz:**
  - `:198-204`:
    ```c
    if (free_idx >= 0) { s_write_off = …free_idx…; s_ready = 1; }
    else { s_write_off = W25Q_FLIGHTREC_BASE; if (w25q_erase_sector(s_write_off)) s_ready = 1; }
    ```
  - Komentář `:197-198` říká: *„Kdyz zadny smazany neni, smaz nejstarsi
    (nejnovejsi index za poslednim) — deje se to jen jednou za 64 dumpu."*
    Kód **nejstarší nehledá**; závorka dokonce naznačuje správné pravidlo
    (index za posledním), které se neprovádí.
  - Průběh: boot 1–64 → `free_idx` postupně 0…63, dumpy jdou po řadě.
    Boot 65 → všech 64 sektorů má platnou hlavičku → `free_idx == -1` → smaže se
    sektor 0 (**tady je to správně, je nejstarší**) a dump do něj padne.
    Boot 66 → sektor 0 má **nejvyšší** `seq` (`:183-184` do něj nastaví
    `s_read_off`), ostatní jsou plné → `free_idx == -1` → **smaže se sektor 0**,
    tedy ten právě přečtený. A stejně tak každý další boot: od té chvíle se
    používá výhradně sektor 0 a každý dump se maže při následujícím startu.
  - `s_have_dump`/`s_have_read` se přitom nastaví na 1 (`:192-193`) **před**
    erase, takže je stav navenek „záznam existuje".
  - **Sesterská implementace v témže souboru to dělá správně:** `errlog_init()`
    na `:473-477` bere `ni = (best_i + 1) % W25Q_ERRLOG_SECTORS`, tedy sektor
    **za nejnovějším**.
- **Dopad:** Po ~64 dumpech (dump = detekovaný stall, přetečení zásobníku,
  selhání `malloc` nebo `flightrec test`) se flight recorder **trvale znehodnotí**:
  `status` hlásí *„flight recorder: je ulozeny zaznam -> `flightrec`"*
  (`freertos_task_uart.c:2664-2665`), zatímco `flightrec` vypíše *„FLIGHTREC:
  zadny ulozeny zaznam"* (`:2055-2056`), protože `hdr_unpack` nad smazaným
  sektorem selže (`flightrec.c:151`). Dva výpisy si odporují právě v okamžiku,
  kdy se hledá příčina poruchy. Zaniká i rotace kvůli životnosti flash (píše se
  pořád do jednoho sektoru) — to je ale u ~64 zápisů/životnost desky bezvýznamné.
- **Reprodukce:** `HYPOTÉZA — ověřit:` 65× `flightrec test` + reset (jeden dump
  na běh, `s_dumped` na `:265`). Levněji se to dokáže zmenšením
  `W25Q_FLIGHTREC_SECTORS` na 2 v ladicím buildu — pak stačí tři cykly
  `flightrec test` + restart.
- **Návrh opravy:** převzít pravidlo z `errlog_init()`: zapamatovat si index
  nejnovějšího sektoru (`best_i`, dnes se drží jen `s_read_off`) a při plném
  regionu mazat `(best_i + 1) % W25Q_FLIGHTREC_SECTORS`. „První volný" zůstává
  jako rychlá cesta pro nezaplněný region.
- **Riziko opravy:** nízké — mění se jen volba cílového sektoru, formát dumpu ani
  čtecí cesta se nedotýká.
- **Vztah k lekcím:** **L-0028** (komentář slibuje jiné chování než kód),
  **L-0012** (dvě symetrické instance v jednom souboru; `errlog` opravený,
  `flightrec` ne).
- **Stav:** otevřeno

---

### F-0092 [S3] Záznam o pádu v `errlog` neobsahuje, co spadlo — tag se utne přesně na dvojtečce

- **Místo:** `CM7/Core/Src/flightrec.c:628-635` (`errlog_boot_record()`),
  `:669-671` (`errlog_fmt_detail`, větev `ERRLOG_K_CRASH`),
  kontrakt `CM7/Core/Inc/errlog.h:39`, `:64`
- **Popis:** Crash black-box dekóduje příčinu do `g_crash_text` jako
  `"stall:UiTask"` / `"stack:UartTask"` / `"assert:L1234"`. Do trvalé historie se
  z něj kopíruje **prvních `ERRLOG_TAG_LEN` = 6 znaků**, tedy přesně prefix
  s dvojtečkou — rozlišující část se zahodí. `errlog_fmt_detail()` navíc pro
  `K_CRASH` tag **vůbec netiskne**, jen `CFSR`/`BFAR`, které jsou u těchto druhů
  pádu nulové.
- **Důkaz:**
  - `flightrec.c:630-631`:
    `for (; k < ERRLOG_TAG_LEN && g_crash_text[k]; k++) ct[k] = …;` — `ERRLOG_TAG_LEN`
    je 6 (`errlog.h:64`).
  - Skutečné hodnoty `g_crash_text` (`rtc.c:133-176`): `"stack:%s"`, `"stall:%s"`,
    `"HF@%08lX%c"`, `"hal_err@%lu"`, `"assert:L%lu"`, `"malloc fail"`.
    Po ořezu na 6 znaků: **`"stack:"`, `"stall:"**, `"HF@240"`, `"hal_er"`,
    `"assert"`, `"malloc"`. U prvních dvou tedy zmizí **jméno tasku** — přesně to,
    kvůli čemu se black-box rozšiřoval (`CLAUDE.md`: *„Bez toho byl prostý IWDG
    reset němý (RSR řekl jen „watchdog", ne který task)"*).
  - `flightrec.c:669-671` — jediný výstup pro `K_CRASH`:
    `snprintf(buf, n, "CFSR=0x%08lX BFAR=0x%08lX", r->a, r->b);` — tag se
    nepoužije. A `a`/`b` plní `errlog_boot_record()` z `g_crash_cfsr`/`g_crash_bfar`
    (`:634`), které `rtc.c` nastavuje **jen pro `kind == 4` (HardFault)**
    (`rtc.c:145-146`); pro stack/stall/assert/hal_err zůstávají nulové.
  - Okno CHYBY i web tedy u přetečení zásobníku ukážou doslova
    `CRASH   CFSR=0x00000000 BFAR=0x00000000`.
  - `errlog.h:39` dokumentuje `sub` jako *„kind (1 stack/2 malloc/3 stall/4 HF/
    5 hal/6 assert)"*, ale **jediný zapisovatel předává `sub = 0`**
    (`flightrec.c:634`, ověřeno grepem — jiný producent `ERRLOG_K_CRASH` v projektu
    není). Dokumentované pole se nikdy neplní.
- **Dopad:** Otázka, kvůli které `errlog` vznikl (`errlog.h:8-9`: *„stává se to
  častěji?", „co se dělo před týdnem"*), je pro **nejčastější druhy pádu**
  nezodpověditelná: z historie se pozná jen *že* se pád stal, ne *co* spadlo.
  🔑 **Ve spojení s otevřeným F-0018** (dump ze `stack` hooku se nikdy neprovede,
  protože `osMutexAcquire` v kontextu výjimky vždy vrátí `osErrorISR`) to znamená,
  že pro scénář STATUS #18 jsou **oba** trvalé záznamy slepé.
- **Reprodukce:** `stacktest yes` (úmyslné přetečení zásobníku UartTasku) →
  po restartu okno CHYBY / `errlog dump` → řádek `CRASH` bez jména tasku.
  ⚠️ `selftest` z konzole zatím nespouštět (F-0055).
- **Návrh opravy:** dvě nezávislé, obě malé:
  (a) `errlog_boot_record()` naplní `sub` skutečným `kind` z BKP — `rtc.c` ho už
  dekóduje, stačí ho vystavit vedle `g_crash_text` (dnes se zahazuje);
  (b) `errlog_fmt_detail()` u `K_CRASH` vytiskne **tag** a `CFSR`/`BFAR` jen když
  jsou nenulové. Rozšíření `ERRLOG_TAG_LEN` **nedoporučuji** — měnilo by formát
  32 B záznamu a znehodnotilo by existující logy; jméno tasku se do 6 znaků stejně
  nevejde, kdežto `sub` + smysluplná věta ano.
- **Riziko opravy:** nízké (b) / střední (a — dotýká se dekódování black-boxu
  v `rtc.c`, tedy cesty, která běží před schedulerem).
- **Vztah k lekcím:** **L-0049** (rozlušti `a`/`b`/`sub` do věty **na zdroji** —
  věta tu vzniká, ale vynechá jediné pole, které něco nese), **L-0017**
  (co se rozhodneš nezobrazit, musí jít změřit), **L-0028**.
- **Stav:** otevřeno

---

### F-0093 [S3] Obnova uloženého nastavení při bootu zapíše do `errlog` falešnou „změnu nastavení uživatelem"

- **Místo:** `CM7/Core/Src/syscfg.c:281-282` → `CM7/Core/Src/datalog.c:57-60`
  (`datalog_set_period_s`) a `:75-78` (`datalog_set_store`)
- **Popis:** Oba settery vydají `errlog_put(ERRLOG_K_CFG, …)` při **jakékoli**
  změně hodnoty. `syscfg_load()` je při studeném startu volá, aby obnovil uložené
  nastavení — a protože statiky mají v tu chvíli výchozí hodnoty, setter to
  vyhodnotí jako změnu a zapíše ji do trvalé historie.
- **Důkaz:**
  - `datalog.c:57-60`: `if (sec == s_period_s) return;` … `errlog_put(ERRLOG_K_CFG,
    ERRLOG_CFG_LOGPER, sec, old_p, "logT");`
  - Výchozí hodnoty statiků: `s_period_s = DATALOG_PERIOD_S` = 10 (`datalog.c:47`),
    `s_store_pref = DATALOG_STORE_AUTO` = 0 (`:48`).
  - `syscfg.c:281-282` je volá s hodnotami z flash. Uložená perioda 60 s tedy dá
    záznam, který `errlog_fmt_detail` vypíše jako
    **`NASTAV  interval logu 10s -> 60s`** (`flightrec.c:722-724`), i když uživatel
    nic nezměnil.
  - Druhé volání (`datalog_set_store`) padne do rate-limitu — `ERRLOG_COOLDOWN_MS`
    je 60 s **na druh**, ne na podtyp (`flightrec.c:361`, `:495-496`) — takže se
    neuloží, ale **inkrementuje `s_el_pending[ERRLOG_K_CFG]`** (`:514-515`). Tento
    čítač se přilepí jako `repeat` k **příštímu skutečnému** záznamu `K_CFG`, který
    se pak vykreslí jako `x2` (`app_gpsdo.c:3262-3266`).
  - Účel pole je přitom v `errlog.h:48-53` výslovně: odlišit **zásah uživatele**
    od HW události, aby skok ve statistice nevypadal jako porucha. Falešný záznam
    dělá přesně opak.
- **Dopad:** Po každém studeném startu jeden nepravdivý řádek v okně CHYBY i na
  webu a zkreslené počítadlo opakování u další skutečné změny. Nic se nerozbije,
  ale trvalá historie lže — a je to historie, podle které se má rozhodovat, jestli
  je skok v měření HW událost, nebo zásah.
- **Reprodukce:** `datalog interval 60` → počkat na zápis do flash → **power-cyklus**
  → `errlog dump 5` → hned za záznamem `BOOT` je `NASTAV interval logu 10s -> 60s`
  s `uptime` pár sekund.
- **Návrh opravy:** oddělit „nastav" od „nastav a zaznamenej". Nejmenší varianta:
  přidat do obou setterů vnitřní příznak (`static uint8_t s_restoring;`) nebo
  parametr `bool log_it`, který `syscfg_load()` vypne; UART/UI cesty
  (`qspi_req_service`, `freertos_task_uart.c:130-137`) zůstanou beze změny.
  🔴 **Nutná podmínka opravy F-0089** — po přesunu těch řádků nad `return` by se
  falešné záznamy jinak dělaly při **každém** resetu, ne jen při studeném startu.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0019** (účetnictví připoj ke ZMĚNĚ stavu, ne k cestě —
  tady je připojené k cestě, a proto ho spustí i obnova).
- **Stav:** opraveno 2026-09-17 v `6d1b6e5` (spolu s F-0089; lekce **L-0052**). Zvolena varianta „vnitřní příznak“: `datalog_cfg_quiet(bool)` v `datalog.h`/`datalog.c`; parametr `bool log_it` by znamenal sáhnout na všechny volající.

---

### F-0094 [S3] `calib_save()` zapíše „uložena kalibrace" do trvalé historie dřív, než se uloží (a i když se neuloží vůbec)

- **Místo:** `CM7/Core/Src/calib.c:66-82` (`calib_save()`), konkrétně `:70` vs. `:71`
  a `:79-82`
- **Popis:** `errlog_put(ERRLOG_K_CFG, ERRLOG_CFG_CALIB, …)` je **prvním**
  příkazem funkce — před kontrolou `s_store.ready`, před získáním mutexu a před
  `w25q_store_write()`. Na všech třech chybových cestách proto zůstane
  v trvalé historii záznam o změně, která se neprovedla.
- **Důkaz:**
  ```c
  66  bool calib_save(void) {
  70      (void)errlog_put(ERRLOG_K_CFG, ERRLOG_CFG_CALIB, 0u, 0u, "kalib");
  71      if (!s_store.ready) return false;
  79      if (osMutexAcquire(qspiMutexHandle, CALIB_LOCK_MS) != osOK) return false;
  80      bool ok = w25q_store_write(&s_store, &b, sizeof b);
  82      return ok;
  ```
  Výstup `errlog_fmt_detail` pro tento podtyp je bezpodmínečné
  *„ulozena kalibrace napeti"* (`flightrec.c:729-731`) — bez „pokus" či „selhalo".
  Zdůvodnění na `:68-69` („bez záznamu by skok v logu vypadal jako změna měřeného
  signálu") je správné, ale platí pro **provedenou** změnu.
- **Dopad:** Kalibrace se neuloží (nedostupná flash, `calib_load` neproběhl, obsazený
  mutex), uživatel v okně Kalibrace uvidí chybu — ale trvalá historie, podle které
  se později vysvětluje skok v datech, bude tvrdit, že se kalibrace změnila.
  Analýza pak bude hledat příčinu skoku na nesprávném místě.
- **Reprodukce:** `HYPOTÉZA — ověřit:` vyvolat ULOŽIT v okně Kalibrace během
  `datalog erase` (celý region, mutex držen minuty) — `calib_save` selže na
  `osMutexAcquire` (timeout 1000 ms), `errlog dump` přesto ukáže záznam.
- **Návrh opravy:** přesunout `errlog_put` **za** `w25q_store_write` a vydat ho
  jen při `ok == true`. Alternativa (zaznamenat i neúspěch) je horší — druh
  `K_CFG` znamená „nastavení se změnilo"; neúspěšný zápis patří pod
  `ERRLOG_K_STORAGE`, kam už `datalog.c:500-503` píše.
- **Riziko opravy:** nízké (přesun jednoho řádku).
- **Vztah k lekcím:** **L-0028** (obrana/záznam, který popisuje něco jiného, než
  co se stalo), **L-0003** (návratová hodnota se nevyhodnocuje tam, kde má).
- **Stav:** opraveno 2026-09-17 v `15aa8d3` (lekce **L-0053**, společná s F-0099). Opraveno podle návrhu — `errlog_put` přesunut za zápis a podmíněn `ok`.

---

### F-0095 [S3] `errlog_read_batch()` čte záznam po záznamu — dávka 64 je ~11 ms nepřerušeného pollingu v defaultTasku pod QSPI mutexem

- **Místo:** `CM7/Core/Src/flightrec.c:581-601` (`errlog_read_batch`),
  volající `CM7/Core/Src/ipc.c:393-407` (`ipc_errlog_service`)
- **Popis:** Funkce se jmenuje „batch" a drží **jeden** mutex, ale uvnitř vydá
  **jeden QSPI příkaz na každý 32B záznam**. Je to přesně vzor, který
  `datalog.h:211-213` popisuje jako 25násobnou režii a kvůli kterému vznikl
  `datalog_read_bulk()` (oprava F-0039, lekce **L-0021**).
- **Důkaz:**
  - `flightrec.c:590-598`: `for (k = 0; k < count; k++) { … w25q_read(BASE + rel,
    b, sizeof b); … }` — `sizeof b` je `ERRLOG_REC_SIZE` = 32 B.
  - Cena jednoho takového čtení je v projektu **změřená**: `datalog.h:212-213`
    — *„zmereno ~173 us/zaznam, zatimco 32 B dat je jen ~7 us"*. Stejný ovladač,
    stejná délka.
  - `w25q_read` je čistý polling bez ustoupení scheduleru:
    `w25q.c:59-60` = `HAL_QSPI_Command` + `HAL_QSPI_Receive` (obojí blokující,
    `osDelay` je jen ve `wait_ready`, `w25q.c:89`, který se u čtení nevolá).
  - `ipc.c:405-407` ořezává požadavek na `IPC_ERRLOG_CHUNK` = **64**
    (`ipc_shared.h:301`). 64 × 173 µs ≈ **11,1 ms** souvislého spinu
    v defaultTasku **s drženým `qspiMutexHandle`** — nad projektovým pravidlem
    *„žádný spin > ~10 ms v defaultTask/UiTask/FpgaTask"* (`CLAUDE.md`,
    „Vlákna / časování").
  - 🔑 **Záznamy leží v kruhu souvisle** (`rel` klesá o 32 B na krok, `:592-593`),
    tedy přesně situace, kterou `datalog_read_bulk` řeší jedním (nejvýš dvěma)
    čtením — `datalog.c:536-548` to i vysvětluje.
  - Vedlejší nesoulad: jediný klient posílá nejvýš **32** záznamů
    (`HTTPD_ERRLOG_MAX_PTS`, `httpd_min.c:83`, ořez na `:4123`), takže polovina
    pole `rec[64]` ve sdílené SRAM4 (**~3 kB**) se nikdy nenaplní.
- **Dopad:** Dnes se reálně žádá ≤32 záznamů (~5,5 ms), takže se to neprojeví.
  CM7 ale mez 64 připouští a hodnota přichází ze **sdílené paměti zapisované
  druhým jádrem** (`g_ipc.errlog.req_count`) — chyba na CM4 tedy může defaultTask
  poslat přes limit. Po tu dobu neprojde `datalog_tick` (timeout 10 ms),
  `syscfg_flash_tick`, `errlog_tick` ani `flightrec_dump` (50 ms).
- **Reprodukce:** `HYPOTÉZA — ověřit:` `GET /api/errlog?n=32` a změřit dobu, po
  kterou defaultTask drží mutex (přidat `HAL_GetTick()` kolem `errlog_read_batch`
  a vypsat maximum do `status` — stejný vzor jako `max cekani` u DMA2D, F-0036).
- **Návrh opravy:** přepsat `errlog_read_batch` na jedno čtení souvislého bloku
  `count * 32 B` (se dvěma kusy při přetečení kruhu) a rozbalit ho pozpátku —
  tělo `datalog_read_bulk` (`datalog.c:552-590`) je hotová předloha. Buffer
  `IPC_ERRLOG_CHUNK * 32` = 2 kB patří do `.bss`, ne na zásobník. Zároveň srovnat
  `IPC_ERRLOG_CHUNK` s `HTTPD_ERRLOG_MAX_PTS` nebo mez v `ipc_errlog_service`
  snížit na 32.
- **Riziko opravy:** nízké–střední (mění se indexace kruhu; kryje ji `errlog dump`
  proti oknu CHYBY — obojí musí dát tytéž záznamy ve stejném pořadí).
- **Vztah k lekcím:** **L-0021** (přečti hlavičku funkce, kterou voláš — varování
  i s naměřenými čísly je v `datalog.h:211-213`), **L-0040** (ustupuj podle času,
  ne podle počtu iterací).
- **Stav:** otevřeno

---

### F-0096 [S3] `setup_load()` je druhá cesta, která přepisuje `g_meas_cfg` bez kritické sekce — a navíc neověřuje `lo <= hi`

- **Místo:** `CM7/Core/Src/setup.c:108-136` (`setup_load()`), zápisy `:125-133`;
  sanitizace `:40-47` (`slot_sanitize`)
- **Popis:** Načtení profilu sestavy zapíše devět polí `g_meas_cfg`, z toho **pět
  typu `double`**, bez kritické sekce. Je to přesně situace otevřeného **F-0052**
  [S2] (okno MATH), jen z druhé strany. `slot_sanitize()` navíc kontroluje jas,
  auto-dim, zónu a `meas_m`, ale **ne vzájemné pořadí `meas_lo`/`meas_hi`**.
- **Důkaz:**
  - `setup.c:125-133`: `g_meas_cfg.math_en = …; … g_meas_cfg.lo = s.meas_lo;
    g_meas_cfg.hi = s.meas_hi;` — žádné `taskENTER_CRITICAL()`.
  - Protějšky, které kritickou sekci **mají**: `scpi.c:953-955` a `ipc.c:539-543`
    (doloženo v nálezu F-0052); `ipc.c:526` v komentáři výslovně počítá se
    souběžnými zápisy z UI.
  - `slot_sanitize()` (`setup.c:40-47`) řeší `brightness`, `autodim_sec`,
    `tz_offset_h` a `meas_m == 0.0`; `lo`/`hi` v ní nefigurují.
  - `setup_load()` běží v **UiTask** (tlačítko NAČÍST v okně SESTAVY,
    `s_view=33`), zatímco `g_meas_cfg` čte SCPI v **UartTasku** a
    `syscfg_flash_tick()`/`pack()` v **defaultTasku** (`syscfg.c:143-151`,
    100 Hz).
  - Důsledek inverze je v F-0052 doložený: `lo > hi` → trvalý `FAIL` →
    `alarm.c` pípne 4×.
- **Dopad:** (a) roztržené čtení dvojice `lo`/`hi` v jiné úloze při načtení
  profilu; (b) profil uložený **starším** firmwarem (magic `"STP1"` se od
  zavedení nezměnil) nebo poškozený může nainstalovat `lo > hi` a spustit falešný
  alarm; (c) `syscfg_flash_tick` může roztrženou hodnotu zapsat do flash a udělat
  z ní stav, se kterým se nabootuje.
- **Reprodukce:** `HYPOTÉZA — ověřit:` uložit profil s `lo`/`hi` a pak v okně
  SESTAVY načíst profil opakovaně, zatímco přes SCPI běží `CALC:LIM:LOW?`/`HIGH?`.
  Inverzi lze vyrobit přímo: uložit profil, přes SCPI nastavit `hi < lo` (F-0052),
  uložit, načíst.
- **Návrh opravy:** opravit **spolu s F-0052** jedním zásahem — jedna funkce
  `meas_cfg_apply(const meas_cfg_t *)` s kritickou sekcí, kterou použijí všechny
  tři cesty (MATH okno, SCPI, IPC) i `setup_load()`. Do `slot_sanitize()` doplnit
  `if (s->meas_lo > s->meas_hi) { prohodit; }` — stejnou kontrolu potřebuje
  i `syscfg_load()` (`syscfg.c:216-217`, kde dnes taky chybí).
- **Riziko opravy:** nízké, pokud se dělá jako součást F-0052; samostatně by
  vznikly dvě konkurenční opravy téhož (§F5.0).
- **Vztah k lekcím:** **L-0012** (opravuješ-li jednu ze dvou symetrických
  instancí, dolož druhou — tady je instance **třetí**), **L-0018**.
- **Stav:** otevřeno

---

### F-0097 [S3] Strop 4080 B na blob není nikde vynucený `_Static_assert`em

- **Místo:** `CM7/Core/Src/syscfg.c:50-116` (`syscfg_blob_t`),
  `CM7/Core/Src/setup.c:30-33` (`setup_book_t`),
  `CM7/Core/Src/calib.c:34-40` (`calib_blob_t`);
  mez `CM7/Core/Inc/w25q_store.h:21`, vynucení `w25q_store.c:86`
- **Popis:** Blob store umí uložit nejvýš `W25Q_STORE_MAX_BLOB` = 4080 B
  (jeden sektor bez 16B hlavičky). Překročení se pozná **až za běhu** —
  `w25q_store_write()` vrátí `false`. Ani jeden ze tří blobů tu mez nekontroluje
  při překladu; grep na `_Static_assert` ve všech pěti souborech modulu a
  v `w25q_store.c/h`, `w25q_map.h`, `errlog.h` vrací **prázdno**.
- **Důkaz:**
  - `w25q_store.h:21`: `#define W25Q_STORE_MAX_BLOB (4096u - W25Q_STORE_HDR)`.
  - `w25q_store.c:86`: `if (!s->ready || len > W25Q_STORE_MAX_BLOB || …) return false;`
    — mez existuje, ale kontroluje se runtime a **bez jakéhokoli hlášení**.
  - Skutečné velikosti změřené v obrazu (`nm --print-size --radix=d` nad
    `CM7/Release/H757_LED_CM7.elf`): `snap` (= `syscfg_blob_t`) **184 B**,
    `s_book` (= `setup_book_t`) **520 B**; `calib_blob_t` je 20 B.
    Rezerva je dnes velká — nález je o tom, že se o ní nikdo nedozví, až dojde.
  - Riziko není teoretické: `syscfg_blob_t` **rostla nejméně dvanáctkrát**
    (historie magiců `"SCFG"` → `"SCG1"` v komentáři `syscfg.c:24-41`), pokaždé
    přidáním pole na konec.
- **Dopad:** Až blob překročí 4080 B, `syscfg_save()` začne vracet `false`
  a `syscfg_flash_tick()` ho bude zkoušet **při každém tiku (100 Hz)** donekonečna
  (`syscfg.c:315-316`: `if (syscfg_save()) pending = 0;`) — pokaždé s pokusem
  o získání QSPI mutexu (10 ms timeout). Nastavení se přestane ukládat,
  `status` o tom nic neřekne a projev bude *„nastavení nepřežije power-cyklus"* —
  tedy symptom, který se hledá na úplně jiném místě.
- **Reprodukce:** dokazatelné při překladu — dočasně přidat do `syscfg_blob_t`
  pole `uint8_t pad[4000];` a ověřit, že se nic nestane (dnes) vs. že překlad
  spadne (po opravě).
- **Návrh opravy:** tři řádky, jeden u každého blobu:
  ```c
  _Static_assert(sizeof(syscfg_blob_t) <= W25Q_STORE_MAX_BLOB,
                 "syscfg blob se nevejde do jednoho sektoru W25Q");
  ```
  Vzor už v projektu existuje — `httpd_min.c:84-85` dělá přesně tohle pro
  `/api/errlog` (a `:3648` pro `/api/log`).
- **Riziko opravy:** žádné (překladová kontrola, nemění kód).
- **Vztah k lekcím:** **L-0026** (*„strop připoj k bufferu `_Static_assert`em,
  ne komentářem"*) — lekce vznikla z F-0056, kde přesně tohle přeteklo.
- **Stav:** opraveno 2026-09-17 v `e583432` (lekce: **rozšíření L-0026** — je to druhý výskyt téže vady, ne nová třída). Tři `_Static_assert` přesně podle návrhu; každý ověřen pozitivní kontrolou (dočasné nafouknutí struktury o 4096 B → překlad skutečně spadne).

---

### F-0098 [S3] Neúspěšná inicializace úložiště je trvalá a tichá

- **Místo:** `CM7/Core/Src/syscfg.c:190` (`syscfg_load`),
  `CM7/Core/Src/setup.c:53` (`setup_init`),
  `CM7/Core/Src/calib.c:48` (`calib_load`),
  `CM7/Core/Src/flightrec.c:174` (`flightrec_init`), `:445` (`errlog_init`)
- **Popis:** Všech pět inicializací má tvar *„nedostal jsem mutex (nebo flash
  neodpověděla) → `return`"*. Příslušný příznak (`s_store.ready`, `s_ready`,
  `s_el_ready`) pak zůstane `false` **po celý zbytek běhu** — žádný retry, žádné
  počítadlo, žádný řádek v `status`.
- **Důkaz:**
  - `syscfg.c:190`: `if (osMutexAcquire(qspiMutexHandle, SYSCFG_LOCK_LOAD_MS) != osOK) return;`
    → `s_store.ready` zůstane 0 → `syscfg_save()` na `:288` vrací `false` navždy
    a `syscfg_flash_tick()` na `:301` hned odchází. **Nastavení se nikdy neuloží.**
  - `setup.c:53`: totéž → `book_flush()` (`:72`) vrací `false` → tlačítka
    ULOŽIT/SMAZAT v okně SESTAVY tiše nic nedělají.
  - `calib.c:48`: `g_calib` zůstane na výchozích hodnotách z datasheetu
    (`:24-27`), takže **RF v dBm a větve 12 V/5 V budou nekalibrované** — a nic
    to neohlásí.
  - `flightrec.c:174` a `:445`: `s_ready`/`s_el_ready` = 0. U `errlog` to má aspoň
    následek, který je vidět: záznamy se hromadí v RAM ringu (16 položek,
    `:360`) a po jeho naplnění se počítají do `errlog_dropped()` (`:499`), což
    okno CHYBY i `errlog` vypisují (`app_gpsdo.c:3208-3209`).
  - Pro porovnání: `datalog_init()` výsledek **vypisuje** (`datalog.c:395-397`,
    `„datalog: -- NEDOSTUPNE"`) a `datalog_format_status()` ho nese do `status`.
    Ostatní čtyři nic takového nemají.
- **Dopad:** Jednorázová smůla při bootu (obsazená flash, neúspěšný `w25q_init`)
  znehodnotí perzistenci na celý běh. Uživatel to pozná až tím, že se nastavení
  nebo kalibrace po restartu ztratí — tedy nejdřív při dalším resetu a bez
  jakékoli stopy, proč.
- **Reprodukce:** `HYPOTÉZA — ověřit:` dočasně snížit `SYSCFG_LOCK_LOAD_MS`
  na 0 a ověřit, že `datalog off` → power-cyklus → log je zase zapnutý, aniž by
  cokoli v `status` bylo jinak.
- **Návrh opravy:** minimální varianta (v duchu F-0022/F-0033 — zviditelnit,
  ne přepisovat): jeden společný řádek do `status`, například
  `ULOZISTE: syscfg OK | calib OK | sestavy OK | errlog OK | flightrec OK`,
  odvozený z těch pěti příznaků. Plnější varianta (retry při dalším tiku) je
  dražší a u `calib_load`/`setup_init` by znamenala volat je znovu z tiku, tedy
  novou souběžnou cestu — **tu nedoporučuji bez zadání**.
- **Riziko opravy:** nízké u výpisu; střední u retry (nová cesta souběhu).
- **Vztah k lekcím:** **L-0017** (tichý přeskok je přípustný jen s počítadlem),
  **L-0016** (obrana, kterou nikdo nečte, není obrana).
- **Stav:** otevřeno

---

### F-0099 [S4] `errlog_erase()` zahazuje všech 64 návratových hodnot a bezpodmínečně hlásí připravenost

- **Místo:** `CM7/Core/Src/flightrec.c:603-613`
- **Popis:** Mazání regionu ignoruje výsledek každého `w25q_erase_sector()`
  a poté nastaví `s_el_ready = 1` a `s_el_write_off = W25Q_ERRLOG_BASE`, jako by
  bylo smazáno. Volající (`freertos_task_uart.c:847`) vytiskne *„errlog: smazano"*.
- **Důkaz:**
  ```c
  606  for (uint32_t i = 0; i < W25Q_ERRLOG_SECTORS; i++) {
  607      (void)w25q_erase_sector(W25Q_ERRLOG_BASE + i * W25Q_SECTOR_SIZE);
  608  }
  609  s_el_seq_next = 1u;
  611  s_el_ready = 1;
  ```
  Srovnání se sesterskou funkcí: `datalog_erase_all()` (`datalog.c:607-612`)
  výsledek **kontroluje** (`if (!s_be->erase(off)) { ok = false; break; }`)
  a stav resetuje jen při `ok`.
- **Dopad:** Po nedokončeném mazání se `s_el_write_off` vrátí na začátek regionu,
  kde ale pořád leží staré záznamy. `errlog_tick` sektor před prvním zápisem smaže
  (`:537-539`), takže se to samo srovná; zbytek regionu ale zůstane se starými
  záznamy, které mají **vyšší `seq`** než nově psané — příští `errlog_init()`
  je najde jako „nejnovější" a hlava se ustaví na špatném místě.
- **Reprodukce:** `HYPOTÉZA — ověřit:` vyvolat `errlog erase` při odpojené/vadné
  flash. Prakticky nedosažitelné bez zásahu do HW.
- **Návrh opravy:** stejný tvar jako `datalog_erase_all()` — počítat neúspěchy,
  při chybě nenastavovat `s_el_ready` a vrátit/vypsat výsledek.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0003**, **L-0012** (dvojče `datalog_erase_all` to dělá
  správně).
- **Stav:** opraveno 2026-09-17 v `a9af9de` (lekce **L-0053**, společná s F-0094). Opraveno šířeji než návrh: kromě kontroly návratů hlásí výsledek **oba** volající — i `qspi_req_service`, který dosud mlčel úplně (požadavek z okna CHYBY konzoli nevidí).

---

### F-0100 [S4] UART `errlog dump` obchází `errlog_fmt_detail()` a tiskne holá čísla — třetí konzument nedodržuje L-0049

- **Místo:** `CM7/Core/Src/freertos_task_uart.c:866-877`
- **Popis:** Konzole vypisuje `a`/`b` v hexu (`a=%08lX b=%08lX`) a `sub` jako
  číslo. Význam těch polí se ale **liší podle druhu události** — a právě proto
  vznikla `errlog_fmt_detail()` jako jediný zdroj pravdy, kterou používá displej
  i web. Navíc se čte `errlog_read_back()` v cyklu, tedy jeden mutex a jeden QSPI
  příkaz na záznam, ačkoli `errlog_read_batch()` existuje právě proti tomu.
- **Důkaz:**
  - `freertos_task_uart.c:871-875`: `printf("  #%lu up=%lus %s/%u a=%08lX b=%08lX
    x%u %s\n", …)`.
  - `errlog.h:107-112` u `errlog_fmt_detail` říká: *„JEDEN zdroj pravdy: pouziva
    ho displej (okno CHYBY) i IPC kanal pro web, aby obe strany rikaly totez
    o tomtez zaznamu."* Konzole v tom výčtu chybí.
  - `errlog.h:96-99` u `errlog_read_batch` zdůvodňuje dávkování tím, že osm
    samostatných `errlog_read_back` je osm cyklů acquire/release. `errlog dump`
    jich dělá až **200** (`freertos_task_uart.c:855`, `:866-868`).
  - **L-0049** (zapsaná 2026-09-13, tedy s tímto podsystémem) zní: *„Rozlušti
    binární `a`/`b` do věty JEDNOU, na zdroji, ne u každého konzumenta."*
  - Konzole je přitom podle `CLAUDE.md` („NEZNÁŠ PŘÍČINU? NEJDŘÍV MĚŘ")
    **nástroj první volby** — displej i web jsou dražší cesta.
- **Dopad:** Kdo diagnostikuje přes UART (tedy doporučený postup), musí význam
  `a`/`b` dohledávat ve zdrojáku; displej a web mu přitom řeknou větu. Mírný
  rozpor v tom, co o témže záznamu říkají tři výstupy.
- **Reprodukce:** `errlog dump 5` vedle okna CHYBY na displeji.
- **Návrh opravy:** v cyklu zavolat `errlog_fmt_detail(&r, det, sizeof det)`
  (72 B na zásobníku UartTasku — ⚠️ s ohledem na otevřený **F-0055**/**F-0074**
  raději `static char det[ERRLOG_DETAIL_LEN];`, funkce běží jen v UartTasku)
  a vypsat větu; hex `a`/`b` ponechat jako doplněk na konci řádku. Čtení převést
  na `errlog_read_batch` po dávkách (spolu s F-0095).
- **Riziko opravy:** nízké; **pozor na zásobník UartTasku** (F-0055).
- **Vztah k lekcím:** **L-0049**, **L-0021**.
- **Stav:** otevřeno

---

### F-0101 [S4] `errlog_count()` po přetočení kruhu nadhodnocuje počet čitelných záznamů

- **Místo:** `CM7/Core/Src/flightrec.c:552-556` (`errlog_count`),
  `:560-579` (`errlog_read_back`), `:581-601` (`errlog_read_batch`)
- **Popis:** `errlog_count()` vrací `min(seq_next-1, ERRLOG_CAPACITY)`, tedy po
  přetočení vždy 8192. Skutečně čitelných je ale méně ze dvou důvodů:
  (a) sektor, do kterého se právě zapisuje, byl při vstupu **celý smazán**, takže
  až 128 slotů za hlavou je prázdných; (b) index `idx = count-1` vychází na
  `back % span == 0`, což ukazuje přímo na pozici zápisu, tedy na prázdný slot.
- **Důkaz:**
  - `:554-555`: `written = s_el_seq_next - 1; return (written < CAPACITY) ? written : CAPACITY;`
    — `ERRLOG_CAPACITY` = `64 * (4096/32)` = **8192** (`:362-363`).
  - `:564`: `back = ((idx_from_newest + 1u) * ERRLOG_REC_SIZE) % span;`
    Pro `idx = 8191` je `back = 8192*32 % 262144 = 0`, takže
    `rel = (write_off - BASE)` = pozice příštího zápisu.
  - `:537-539` (`errlog_tick`) maže celý sektor při vstupu do něj, takže sloty
    od `write_off` do konce sektoru jsou `0xFF` → `el_unpack` je odmítne (`:408`).
  - `errlog_read_batch` na neplatný záznam **přeruší celou dávku**
    (`:596`: `if (!el_unpack(b, &out[got])) break;`), takže nevrátí ani záznamy
    za ním.
  - Drobnost téže třídy: `s_el_write_off` se čte **před** získáním mutexu
    (`:564-565`, `:591-593` vs. `:574`, `:589`), zatímco ho pod mutexem posouvá
    `errlog_tick` v defaultTasku — čtenář může spočítat pozici ze zastaralé hodnoty.
- **Dopad:** Web dostane `total` = 8192, ale poslední stránka stránkování přijde
  prázdná; `errlog dump 200` u naplněného logu skončí dřív. Žádná ztráta dat,
  jen nekonzistentní hlášení. Projeví se až po 8192 záznamech — při rate-limitu
  1 záznam/druh/minutu jsou to měsíce provozu.
- **Reprodukce:** `HYPOTÉZA — ověřit:` zmenšit `W25Q_ERRLOG_SECTORS` na 2
  v ladicím buildu, vyvolat >256 záznamů a porovnat `errlog_count()` s počtem
  řádků, které `errlog dump 300` doopravdy vypíše.
- **Návrh opravy:** odvodit počet ze **stavu flash**, ne ze `seq` — stejně jako to
  po opravě 2026-08-15 dělá `datalog find_head()` (`datalog.c:347-366`, komentář
  tam přesně tuhle past popisuje): `count = CAPACITY - (zbytek smazaných slotů
  v aktuálním sektoru)`. Levnější mezikrok: `min(written, CAPACITY - 1)` a `rel`
  počítat pod mutexem.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0011** (čísla z diagnostiky musí sedět s tím, co jde
  doopravdy přečíst), **L-0031** (datový typ / kapacita je taky mez).
- **Stav:** otevřeno

---

### F-0102 [S4] `datalog_tick()` dohání zameškané vzorky; `syscfg.c` má duplicitní include se zastaralým komentářem

- **Místo:** `CM7/Core/Src/datalog.c:485`; `CM7/Core/Src/syscfg.c:10-11`
- **Popis:** Dvě drobnosti bez dopadu na dnešní provoz:
  (a) `datalog_tick` posouvá plán `s_next_ms += period*1000`, zatímco
  `flightrec_tick` (`flightrec.c:212`) i `errlog` používají `now + interval`.
  Kdyby defaultTask někdy vynechal víc než jednu periodu, datalog by zameškané
  vzorky **doháněl** — zapsal by několik záznamů hned za sebou se **stejným
  obsahem i `t_unix`**, protože `sample()` čte živé globály, ne historii.
  (b) `syscfg.c:10-11` includuje `datalog.h` dvakrát, přičemž druhý řádek nese
  dva komentáře, z nichž jeden patří k prvnímu includu.
- **Důkaz:**
  - `datalog.c:484-485`:
    `if ((int32_t)(HAL_GetTick() - s_next_ms) < 0) return; s_next_ms += … * 1000u;`
    proti `flightrec.c:211-212`:
    `if ((int32_t)(now - s_next_ms) < 0) return; s_next_ms = now + 1000u;`
  - `s_next_ms` se posouvá **před** pokusem o mutex (`datalog.c:493`), takže
    neúspěšné zápisy backlog nedělají — dohánění tedy může vzniknout jen tím, že
    se `datalog_tick()` vůbec nezavolá po dobu > periody.
  - Prakticky nedosažitelné: nejdelší blokující operace (`errlog_erase` ~3–26 s,
    `datalog_erase_all` minuty) běží v UartTasku a `w25q.c wait_ready()` ustupuje
    scheduleru (`w25q.c:89`), takže defaultTask dál běží. Proto S4, ne S3.
  - `syscfg.c:10-11`:
    ```c
    #include "datalog.h"
    #include "datalog.h"   /* datalog_sd_det_force/forced … */   /* datalog_enabled/set_enabled … */
    ```
- **Dopad:** (a) latentní — v logu by mohla vzniknout skupina duplicitních vzorků,
  které by Allanova statistika započítala jako reálná měření (τ₀ by přestalo
  platit). (b) žádný, jen čitelnost.
- **Reprodukce:** (a) `HYPOTÉZA — ověřit:` umělé zablokování defaultTasku na
  > 10 s. (b) viditelné v kódu.
- **Návrh opravy:** (a) `s_next_ms = HAL_GetTick() + period*1000u;` — sjednotí to
  chování se zbytkem projektu; (b) odstranit duplicitní řádek a sloučit komentáře
  (patří do `docs:` commitu, ne do `fix:`).
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0018** (dvě místa, která dělají totéž jinak).
- **Stav:** otevřeno

---

## Co bylo zkontrolováno a je v pořádku

**Mapa regionů W25Q** — přepočtena numericky z `w25q_map.h` (ne z komentářů):
`DATALOG 0x00030000..0x01575000` (22 302 720 B = 696 960 záznamů, 5 445 bloků),
`FLIGHTREC 0x01575000..0x015B5000`, `BENCH 0x015B5000..0x015BD000`,
`ERRLOG 0x015BD000..0x015FD000` (8 192 záznamů), za ním **42 MB volných**.
Všechny hranice jsou zarovnané na 4 KB sektor, **žádná se nepřekrývá**
a `W25Q_DATALOG_SIZE` končí přesně na `W25Q_FLIGHTREC_BASE`.
`ERRLOG_REC_SIZE` (32 B) dělí sektor beze zbytku → test `(write_off % 4096) == 0`
v `errlog_tick` skutečně trefí začátek sektoru.

**Power-safe pořadí zápisu** — `w25q_store_write()` zapisuje payload první
a hlavičku s magicem naposled (`w25q_store.c:99-112`), takže výpadek uprostřed
nechá platný starý záznam. `flightrec_dump()` naopak zapisuje hlavičku první
(`flightrec.c:280-281`) **a je to správně** — u dumpu při poruše je důležitější
zachytit důvod než vzorky, a `flightrec_report()` s useknutou řadou počítá
(`:325`, test na `0xFFFFFFFF`).

**Serializace** — všechny tři formáty (datalog 32 B, flightrec 16 B, errlog 32 B)
se skládají ručně po bajtech (`put_u16`/`put_u32`/`el_put32`), ne `memcpy`
struktury, takže nezávisí na paddingu překladače. Každý má CRC-16/CCITT-FALSE
a **jeden** společný polynom: `errlog` volá `datalog_crc16()`
(`datalog.h:262-264`, `flightrec.c:401`) místo čtvrté kopie.

**Umístění všech bufferů dohledáno v obrazu** (`nm` nad `CM7/Release/*.elf`),
ne odhadnuto: `s_bulk` 2048 B, `s_ring` 960 B, `s_el_ring` 512 B, `s_book` 520 B,
`s_tmp.3` 2048 B, `s_el_rows.18` 256 B, `snap.2` 184 B — **všechny v `.bss`
v AXI SRAM (`0x2400xxxx`)**, žádný v DTCM a žádný na zásobníku (**L-0001** splněna).

**DMA a cache** — modul **nepoužívá DMA ani přerušení**: grep na `_DMA(`/`_IT(`/
`DCache` ve všech pěti `.c` vrací 0. QSPI cesta je čistě pollovaná
(`w25q.c:59-60`), takže sekce C checklistu odpadá (**L-0002** nemá co porušit).

**Zásobníky (L-0035, měřeno nad `.elf`):** `syscfg_flash_tick` 188 B →
`syscfg_save` 188 B (nejhlubší řetěz modulu v defaultTasku, ~376 B proti
změřeným 1 728 B volným), `datalog_tick` 308 B, `flightrec_report` 116 B,
`ipc_errlog_service` 76 B, `errlog_tick` 84 B, `errlog_read_batch` 36 B,
`setup_load` 20 B, `calib_save` 36 B. Žádný velký lokál; velké buffery jsou
správně `static` a je to u nich zdůvodněné (`ipc.c:398-402`, `datalog.c:547-549`).

**ISR-safe cesta errlogu** — `errlog_put()` (`flightrec.c:486-520`) opravdu
nesahá na QSPI ani na RTOS API, jen `HAL_GetTick()` a krátká sekce
`__disable_irq()`/`__set_PRIMASK()`. Volá se z ISR (`usart.c:195`, `:214`)
i z úloh a je to bezpečné. Slot na `s_el_tail_i` nemůže producent přepsat
(plný ring se pozná testem `nxt == s_el_tail_i`, `:498`), takže kopie v
`errlog_tick` (`:532`) je konzistentní i bez zámku. `HAL_GetTick()` přetečení
po 49,7 dne je ošetřené znaménkovým rozdílem (`(int32_t)(now - cool_next)`).

**Pořadí initu** — `errlog_init()` až po `w25q_init()` a `errlog_boot_record()`
až po `MX_RTC_Init` (které black-box z BKP dekóduje **a smaže**), viz
`freertos.c:674-679`. Invariant je v kódu i zdůvodněný.

**`datalog_set_store()` vs. souběh initu** — guard `if (s_inited) datalog_init();`
(`datalog.c:91`) skutečně brání tomu, aby `syscfg_load()` z UiTasku pustil init
souběžně s defaultTaskem; je to opravená chyba z 2026-09-07 a komentář
`:80-90` ji popisuje přesně.

**`find_head()`** — ověřuje **celý 32B záznam včetně CRC**, ne jen `seq`
(`datalog.c:304-305`, `:339-340`), a počet záznamů odvozuje ze stavu flash, ne
z absolutní hodnoty `seq` (`:347-366`). Obojí jsou opravy dřívějších reálných
chyb a jsou v kódu doložené. Sken ustupuje scheduleru po 512 blocích (`:315`),
takže ~1 s trvající průchod neshodí watchdog.

**IPC kanál v17** — `_Static_assert` hlídá shodu `IPC_ERRLOG_TAG_LEN`/
`IPC_ERRLOG_DETAIL_LEN` s `errlog.h` (`ipc.c:46-47`) i velikost celé struktury
proti SRAM4 (`ipc_shared.h:422`). `ipc_errlog_service()` ořezává `req_count`
na `IPC_ERRLOG_CHUNK` **před** použitím (`ipc.c:405-406`), takže hodnota od CM4
nemůže přetéct pole `rec[]`; `resp_gen` se nastavuje až po `IPC_DMB()` (`:425-426`).
Kopie do `o->text` je přesně `IPC_ERRLOG_DETAIL_LEN` bajtů z pole téže velikosti
(`:417-419`) — bez přetečení. Na straně CM4 `build_errlog_json()` kopíruje
neukončený `tag[6]` do `tag[7]` s vynuceným NUL (`httpd_min.c:643-645`) a rozpočet
odpovědi hlídá `_Static_assert` (`:84-85`).

**Prověřeno a ZAMÍTNUTO jako nález:**
- *Nezarovnaný přístup do `.sdram`* — modul do `.sdram` vůbec nesahá, všechny
  buffery jsou v AXI SRAM (ověřeno `nm`).
- *`errlog_fmt_detail` a `g_sensor_desc[r->sub]`* — index je ohraničený
  (`r->sub < SENS_COUNT`, `flightrec.c:685`) a `sub` je `uint8_t`, takže
  podtečení nehrozí.
- *Kopie 72 B `det` do IPC včetně neinicializovaného ocasu za NUL* — obsah za
  terminátorem se na CM4 nikdy nečte (`%s` v `jputf`, `httpd_min.c:646-648`),
  takže do sítě neodchází; není to únik, jen zbytečná kopie.
- *Neescapovaný `%s` v JSON* (`httpd_min.c:646`) — všechny zdroje textu jsou
  literály firmwaru, popisky senzorů a čísla; `tag` z flash je krytý CRC, takže
  nese jen to, co zapsal firmware. Injekce uvozovky není dosažitelná.
- *Dělení nulou v `datalog_read_back`/`_bulk`* — `datalog_init()` odmítne backend
  s kapacitou < 32 B (`datalog.c:389`), takže `% s_be->capacity` je bezpečné.
- *`vbat_encode`/`vbat_decode`* — kód 0 je vyhrazený sentinel, `code < 1`
  se clampuje na 1 (`datalog.c:156`), takže starý záznam se nepřečte jako
  vybitá baterie; kryje to i selftest (`:663-677`).

## Nezkontrolováno / omezení tohoto běhu

- **Nic z tohoto modulu nebylo spuštěno na HW.** Všechny nálezy jsou statické;
  u čtyř z nich (`F-0090`, `F-0094`, `F-0099`, `F-0102a`) je reprodukce označená
  jako **HYPOTÉZA** včetně návodu, co změřit. `F-0089` a `F-0093` jsou naopak
  reprodukovatelné **z konzole během minuty**.
- **Dohánění času (`F-0102a`) je posouzené jen z kódu.** Tvrzení „defaultTask
  se nevyhladoví" stojí na tom, že `w25q wait_ready()` ustupuje scheduleru; při
  jiné skladbě blokujících operací (např. budoucí SD backend) to platit nemusí.
- **SD backend datalogu (`datalog_sd.c`) je modul 7** a nebyl zde znovu čten;
  `F-0090` se ho ale týká také (`datalog_backend_sd` je nekonstantní).
- **`w25q.c` / `w25q_store.c` byly čteny jen do hloubky potřebné pro kontrakt**
  (mez blobu, pořadí zápisu, blokující charakter čtení) — audit ovladače je
  hotový jako modul 6.
- **Odhad ~173 µs/záznam v `F-0095`** je převzatý z měření zapsaného
  v `datalog.h:212-213`; pro `errlog` nebyl změřen znovu (stejný ovladač, stejná
  délka čtení, takže převod je přímý — ale je to převzaté číslo, ne nové měření).
- **Kolize `F-0089` × `F-0093`** je záměrně popsaná u obou nálezů: opravit jeden
  bez druhého by chování zhoršilo.
