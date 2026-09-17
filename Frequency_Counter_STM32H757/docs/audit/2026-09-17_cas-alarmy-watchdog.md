# Audit: čas, alarmy, watchdog (2026-09-17)

- **Commit:** `064edff`
- **Jádro / doména:** CM7 / D3 (RTC + BKP, LSE), D2 (TIM7), + IWDG1.
  Vlastník všech periferií modulu je **defaultTask**, s výjimkami rozebranými
  v F-0103.
- **Soubory (čteny celé):** `CM7/Core/Src/rtc.c` (616 ř.) + `Inc/rtc.h`,
  `CM7/Core/Src/alarm.c` (332 ř.) + `Inc/alarm.h`,
  `CM7/Core/Src/watchdog.c` (106 ř.), `CM7/Core/Src/beeper.c` (96 ř.),
  `CM7/Core/Src/bootled.c` (143 ř.). **Celkem ~1 290 ř. + hlavičky.**
  Dohledána konfigurace a všichni volající: `main.c`
  (`SystemClock_Config` — LSE a DBP, pořadí initu, `Error_Handler`),
  `freertos.c` (defaultTask, `RTC->BKP10R`), `freertos_hooks.c` (crash hooky),
  `stm32h7xx_it.c` (`hard_fault_capture`, NMI/CSS, TIM7_IRQHandler),
  `FreeRTOSConfig.h` (`configASSERT` → `rtc_crash_assert`), `gps.c` (`gps_get`),
  `syscfg.c` (druhá strana kontraktu BKP), `freertos_task_ui.c`,
  `freertos_task_uart.c`.
- **Projité sekce checklistu:** **A** (LSE, hodiny TIM7, IWDG/LSI),
  **D** (souběh mezi úlohami a ISR, `volatile`, priority IRQ, zásobníky, watchdog),
  **E** (návratové hodnoty, čekací smyčky, `Error_Handler`, fault handlery,
  reset flagy), částečně **G** (TIM).
- **Neprojité (a proč):** **B** — modul nemá mezijádrovou část; **C** — žádné DMA,
  žádný sdílený buffer (ověřeno `nm`: všechny statiky v `.bss`/`.data` v AXI SRAM);
  **F** — modul nesahá na interní Flash; **H** — nezávisí na revizi silikonu.

## Rozsah a metoda

Modul byl vybrán jako další v pořadí navrženém u modulu 16, s jedním konkrétním
důvodem navíc: **oprava F-0089 i nová kontrola v `scripts/check_lessons.sh` stojí
na tom, co `rtc.c` obnovuje z BKP** — audit `rtc.c` ten základ ověřuje.
✅ **Ověřeno:** výčet v `MX_RTC_Init` (`rtc.c:89-120`) obsahuje právě
`g_ui_cfg`, `g_brightness`, `g_sound_muted`, `g_autodim_en`, `g_autodim_sec`,
`g_theme_idx`, `g_lang_en`, `g_tz_offset_h`, `g_tz_auto`, `g_anim_enabled` —
a nic víc. Kontrola z F-0089 tedy odvozuje správnou množinu a premisa nálezu
platí. Kódování všech tří bitových polí prověřeno **round-tripem zápis↔čtení**
(schéma na bitech 0/9/10, zóna `tz+13`, auto-dim `/15`) — sedí.

Průchod byl řádek po řádku. Nad rámec čtení bylo provedeno:

- **umístění všech statik dohledáno v obrazu** (`nm --print-size` nad
  `CM7/Release/H757_LED_CM7.elf`), ne odhadnuto ze zdrojáku;
- **rámce funkcí změřeny** nad `.elf` podle **L-0035**;
- **přepočtena všechna časování modulu z hodinového stromu** (TIM7, IWDG, LSE
  timeout), ne opsána z komentářů (**L-0006**);
- ověřeno, kdo doopravdy volá každou funkci modulu (grep přes obě jádra);
- `python tools/audit.py` (GCC **14.3.1**): **92 OK / 0 selhání / 2 s varováním**
  = baseline, **žádné varování v tomto modulu**.

## Souhrn

Pět malých souborů, které dohromady tvoří **diagnostický a bezpečnostní skelet
přístroje**: kalendář a jeho disciplinace z GPS, crash black-box, IWDG heartbeat,
zvukový alarm a POST bez běžících přerušení. Skoro nic z toho se za normálního
provozu neprojeví — a právě proto se vady projeví až ve chvíli, kdy na nich
záleží.

Kvalita jádra je vysoká a na řadě míst je vidět, že se z minulých chyb poučilo:
`watchdog_init` má **kanonickou** sekvenci IWDG dle RM0399 i s odůvodněním, proč
START musí být první; `bootled.c` používá DWT místo `HAL_Delay` a bit-banguje tón
mimo `beeper.c`, protože v `Error_Handler` neběží přerušení; `rtc_lse_apply_calib`
**předem testuje `RECALPF`**, aby se vyhnul tísňové smyčce s timeoutem 1 s uvnitř
HAL (přesně to, co pravidlo „žádný spin > ~10 ms" žádá); `mon_edge` má `_ever`
guard podložený **naměřeným** startovním transientem VBAT; `rtc_try_sync` po
skokovém srovnání času zahodí referenci driftu.

Nálezy mají **dva jmenovatele**:

1. **Diagnostika, která může přiřadit událost ke špatnému resetu.** `F-0105`:
   stall se zapíše v okamžiku *detekce*, ne resetu — a protože `s_stall_logged`
   se nikdy nenuluje a BKP je zálohovaná baterií, zotavený stall zůstane ležet
   a ohlásí se u příštího, zcela nesouvisejícího resetu. `F-0106`: jediný
   dosažitelný fault vektor je zároveň jediný, který píše **magic jako první** —
   HardFault způsobený rozbitým zásobníkem tak může ohlásit cizí PC. `F-0104`:
   selhání propagace `PR`/`RLR` zkrátí watchdog z ~4 s na ~0,5 s a nikdo se to
   nedozví.
2. **Jediný vlastník, který ve skutečnosti není jediný.** `F-0103` [S2]:
   `alarm.c` má v komentáři napsané *„jeden vlastník pattern stavu =
   defaultTask → žádný cross-task zápis do `s_phase`"* — a `alarm_test()`
   z UartTasku i `beeper_boot_melody()` z UiTasku to porušují.

**Verdikt: podmíněně funkční.** Za běžného provozu modul dělá, co má; žádný nález
není deterministická nefunkčnost. Vady se projeví při poruše, při souběhu dvou
zvukových cest a při startu bez LSE.

---

## Fáze oprav (2026-09-17) — skupiny A + B, 9 z 10 nálezů v 6 commitech

Uživatel schválil **A i B**, včetně rozhodnutí u skupiny B. Ta rozhodnutí
a jejich důvody:

| nález | rozhodnutí | proč tahle varianta |
|---|---|---|
| **F-0103** | `alarm_test()` přes flag **+ vzájemné vyloučení** boot melodie | Přesun melodie do defaultTasku by okno uzavřel úplně, ale sahá na časování startu (CLAUDE.md 4c) — větší riziko než zbytková vada. Zbytek je pojmenovaný v kódu. |
| **F-0104** | jen **ověřit dosažený stav a vypsat ho** | IWDG už běží (START je neodvolatelný); `Error_Handler` by z nepohodlí udělal nefunkčnost. Vzor **L-0009**. |
| **F-0105** | zneplatnit záznam při zotavení + počítadlo + znovu armovat | Zápis do `errlog` (varianta b) závisí na **F-0092** (modul 16, neschválený) — dvě opravy najednou se nedělají. |
| **F-0111** | `beeper_init()` vrací `bool` → `status` | Němý pípák nesmí shodit měřicí přístroj, ale musí jít poznat. |
| **F-0108** | **NEOPRAVOVAT** (skupina C) | Leží v generovaném `SystemClock_Config()` **bez `USER CODE`** (regen opravu smaže) a oprava sahá do inicializace hodin. Patří k otevřenému **F-0007** jako jedna politika. |

| commit | nálezy | co se změnilo |
|---|---|---|
| `3ccddb4` | **F-0106** | `hard_fault_capture()` píše data první, magic naposled |
| `887d9f4` | **F-0107** | `rtc_crash_assert()` a clear příznaku restartu si odemykají `DBP` |
| `389690c` | **F-0110** + **F-0111** | clamp kmitočtu před výpočtem; `beeper_init()` vrací `bool` |
| `76bf1f6` | **F-0104** + **F-0105** | odečtené `PR`/`RLR` + řádek `WATCHDOG:` ve `status`; zneplatnění záznamu při zotavení |
| `68ae8c8` | **F-0103** | `alarm_test()` přes flag; `beeper_melody_busy()` vyloučí melodii |
| `0b0e5b6` | **F-0109** + **F-0112** | `docs:` — freeze IWDG platí i v Release; skutečný důvod pro 5Hz vyhodnocení |

**Ověřovací řetězec (§F5.2):** `./scripts/build.sh Release BOTH` **0 varování**,
`python tools/audit.py` **92 OK / 0 selhání / 2 s varováním** (baseline,
gcc 14.3.1), CM7 `.text` 604 752 → **605 344 B** (+592), `.bss` +16 B,
**CM4 beze změny** (242 504 B — nic z toho v obrazu CM4 není).

**Důkazy v obrazu** (ne jen „přeloženo"):
- `hard_fault_capture` (RTC base `0x58004000`, `BKP0R` na offsetu `0x50`):
  `0x60`(PC) → `0x64`(CFSR) → `0x6c`(BFAR) → `0x70`(LR) → `0x74`(HFSR) →
  **`0x5c`(MAGIC) až teď**;
- `rtc_crash_assert` nově začíná `orr.w r3, r3, #256` (= `PWR_CR1_DBP`, bit 8)
  nad PWR base `0x58024800`, teprve pak píše BKP;
- **`alarm_test` je nově čtyři instrukce** (`ldr`/`movs`/`strb`/`bx`) — žádné
  `bl pattern_start`;
- `alarm_tick` začíná `bl beeper_melody_busy` + `cmp`/`bne` na epilog;
- nové symboly: `watchdog_timeout_ms` (52 B), `watchdog_cfg_pr/rlr/ok`,
  `watchdog_stall_recovered`, `beeper_ready`, `beeper_melody_busy`,
  v `.bss` `s_cfg_pr`/`s_cfg_rlr`/`s_stall_recovered`/`s_melody_busy`/`s_test_req`;
- všechny čtyři nové řetězce (`WATCHDOG: PR=`, `NENABEHL`,
  `zotavenych stallu`, `NESEDI (ceka se 4000 ms)`).

⚠️ **Upřesnění proti nálezu (F-0111):** `HAL_GPIO_Init` je v HAL `void`, takže
z pěti vyjmenovaných volání jsou vyhodnotitelná **čtyři**, ne pět. Návraty
`HAL_TIM_Base_Start_IT`/`Stop_IT` se ignorují **vědomě a se zdůvodněním**
(L-0003 to připouští) — selhat mohou jen při nekonzistentním `htim7.State`,
který už hlídá init, a volající na to nemá jak reagovat.

**Lekce:** **L-0054** (F-0103), **L-0055** (F-0104), **L-0056** (F-0105);
F-0106 a F-0107 jsou další výskyty **L-0012**, F-0110 výskyt **L-0015**/**L-0030**,
F-0109 a F-0112 výskyty **L-0028** — nové lekce z nich nevznikly záměrně.

⬜ **NEOVĚŘENO NA HW** — čeká na flash a **POWER-CYKLUS**. `IPC_VERSION` se
nemění, CM4 obraz se nezměnil vůbec → stačí flashnout bank1.

🔑 **Co ověřit na desce** (z konzole, bez sondy):
1. **F-0104 + F-0111:** `status` → nový řádek
   `WATCHDOG: PR=4 RLR=2000 -> 4000 ms | pipak ok`. Cokoli jiného u `PR`/`RLR`
   (zvlášť značka `<== NESEDI`) je nález sám o sobě.
2. **F-0103:** `beep test` musí pípnout **dvakrát** jako dřív; pak totéž během
   běžícího alarmu (odpojená anténa / `fpgasim fault lost`) — pípák nesmí
   zůstat troubit. Boot melodie musí znít celá.
3. **F-0110:** boot melodie beze změny (clamp se jí nedotýká, 784–1568 Hz).
4. **F-0105:** řádek `zotavenych stallu` se **nesmí objevit** za normálního
   provozu; `status` → `Reset: power-on` bez přilepeného `stall:*`.
5. Nic se nesmí rozbít: `DISPLEJ: bring-up OK`, `GPIO HLIDAC: 0 oprav`,
   `rtc` vrací čas, `selftest` (⚠️ pozor na otevřený F-0055).

---

### F-0103 [S2] Stav zvukového patternu a `beeper` píšou tři úlohy, ačkoli modul deklaruje jediného vlastníka

- **Místo:** `CM7/Core/Src/alarm.c:329-332` (`alarm_test()`), `:90-105`
  (`pattern_start`/`pattern_stop`), `:85-88` (stavové proměnné);
  `CM7/Core/Src/beeper.c:19` (`s_on`), `:46-71`
- **Popis:** Stav přehrávače (`s_pulses_left`, `s_on_ms`, `s_off_ms`, `s_phase`,
  `s_phase_until`) i `s_on` v `beeper.c` jsou **obyčejné `static`** bez `volatile`
  a bez zámku. Píše je defaultTask (`alarm_tick` ~100 Hz), **UartTask**
  (`alarm_test`) a **UiTask** (`beeper_boot_melody`).
- **Důkaz:**
  - Deklarovaný návrh, `alarm.c:133-134`: *„Touch click: UiTask jen nastavi flag,
    prehraje ho alarm_tick (jeden vlastnik pattern stavu = defaultTask -> zadny
    cross-task zapis do s_phase)."* `alarm_click()` (`:136`) to dodržuje —
    nastaví jen `s_click_req`, jedinou `volatile` proměnnou v souboru.
  - `alarm_test()` (`:329-332`) volá `pattern_start(2, 100, 100)` **přímo**;
    volající je `freertos_task_uart.c:1826` (UART `beep test`), tedy **UartTask**.
  - `beeper_boot_melody()` (`beeper.c:75-86`) volá `beeper_tone`/`beeper_set`
    a přepisuje `TIM7->ARR` i `TIM7->CNT`; volající je
    `freertos_task_ui.c:194`, tedy **UiTask**.
  - `beeper_set()` (`beeper.c:46-48`) začíná `if (on == s_on) return;` — stav
    periferie se tedy odvozuje z proměnné, kterou píšou tři úlohy.
- **Dopad:** Ztracený zápis do `s_on` rozejde proměnnou se skutečným stavem TIM7.
  Nejhorší varianta: TIM7 běží, ale `s_on == false` → **`beeper_set(false)` se
  na první řádce vrátí a pípák troubí souvisle**, dokud něco jiného stav
  nepřeklopí. Mírnější a pravděpodobnější varianta je rozsypaný pattern
  (`beep test` uprostřed alarmu) nebo uříznutá boot melodie.
  ⚠️ Podtečení `s_pulses_left` prověřeno a **nehrozí**: `pattern_start` nastavuje
  `s_pulses_left` **před** `s_phase`, takže stav `s_phase == 1 && s_pulses_left == 0`
  není dosažitelný.
- **Reprodukce:** `HYPOTÉZA — ověřit:` odpojit anténu (nebo `fpgasim fault lost`)
  tak, aby běžel alarm pattern, a během něj z konzole spustit `beep test`
  opakovaně. Sledovat, jestli pípák někdy zůstane troubit; `beep off`/`beep on`
  ho vrátí do definovaného stavu.
- **Návrh opravy:** dotáhnout vzor, který modul už má a používá pro dotyk:
  `alarm_test()` ať jen nastaví `s_test_req` (jako `s_click_req`) a pattern
  spustí `alarm_tick`. U boot melodie je to složitější — je záměrně blokující
  a běží v UiTasku; minimální varianta je **nechat ji doběhnout bez souběhu**
  (příznak `s_melody_active`, který `alarm_tick` respektuje), plná varianta je
  přesunout ji do defaultTasku jako pattern. Ve všech případech `s_on`
  a stav patternu označit `volatile`.
- **Riziko opravy:** nízké u `alarm_test` (stejný vzor jako `alarm_click`,
  pár řádků), **střední** u boot melodie — sahá na časování startu (CLAUDE.md 4c).
- **Vztah k lekcím:** **L-0028** (komentář popisuje disciplínu, kterou kód
  porušuje), **L-0023** (`static` přestane být privátní s druhým volajícím),
  **L-0012** (`alarm_click` opravený vzor je, `alarm_test` ne).
- **Stav:** opraveno 2026-09-17 v `68ae8c8` (lekce **L-0054**). Provedeny OBĚ části návrhu: `alarm_test()` přes flag **a** vzájemné vyloučení boot melodie. ⚠️ Melodie se **záměrně nepřesouvá** do defaultTasku, ačkoli by okno uzavřela úplně — sahalo by to na časování startu (CLAUDE.md 4c). Zbytková vada (uříznutý tón, když je defaultTask už uvnitř `pattern_service`) je v kódu pojmenovaná.

---

### F-0104 [S3] `watchdog_init()` zahodí výsledek čekání na `PVU`/`RVU`; neúspěch zkrátí watchdog z ~4 s na ~0,5 s a nikdo se to nedozví

- **Místo:** `CM7/Core/Src/watchdog.c:63-69` (`watchdog_init()`)
- **Popis:** Po zápisu `PR`/`RLR` se čeká na propagaci **ohraničenou** smyčkou
  (to je správně, **L-0004** splněna), ale její výsledek se **nevyhodnocuje** —
  `s_ready = 1` se nastaví bezpodmínečně. Když se `SR` nevyprázdní, zůstanou
  v platnosti **reset defaulty** `PR = 0` a `RLR = 0xFFF`.
- **Důkaz:**
  ```c
  67  for (uint32_t i = 0; i < 100000u && IWDG1->SR != 0u; i++) { }   /* PVU/RVU (bounded) */
  68  IWDG1->KR  = IWDG_KEY_RELOAD;
  69  s_ready = 1;
  ```
  Přepočet obou stavů (LSI ~32 kHz):
  - zamýšleno: `PR = 4` (/64) → 500 Hz, `RLR = 2000` → **4,0 s**
    (`watchdog.c:20-21`, souhlasí s komentářem);
  - reset default: `PR = 0` (/4) → 8 kHz, `RLR = 0xFFF = 4095` → **0,51 s**,
    tedy **8× kratší**.
- **Dopad:** Watchdog by tiše hlídal 0,5 s místo 4 s. Že to není teoretické,
  plyne z toho, co defaultTask v jedné iteraci smí dělat: `syscfg_flash_tick()`
  → `syscfg_save()` → `w25q_store_write()` → **erase sektoru 50–400 ms**
  (`w25q.c:230`), k tomu `datalog_tick` (mutex 10 ms), `ipc_errlog_service`
  (~5–11 ms, viz F-0095) a `gpio_guard_tick`. Součet se do 500 ms vejde jen
  těsně. Projev by byl **náhodný reset při ukládání nastavení** — symptom, který
  se hledá kdekoli jinde než v inicializaci watchdogu.
  ⚠️ Během `WDG_GRACE_MS` (8 s) se obnovuje bezpodmínečně, takže vada se
  neprojeví při startu, ale až za provozu.
- **Reprodukce:** `HYPOTÉZA — ověřit:` stav není odnikud čitelný — právě to je
  součást nálezu. Po opravě se ověří tím, že `status` vypíše skutečné
  `IWDG1->PR`/`RLR` a odvozený timeout.
- **Návrh opravy:** vyhodnotit smyčku a stav zveřejnit, ne ho opravovat naslepo
  (vzor **L-0009**: kritické volání se hlídá ověřením **dosaženého stavu**).
  Minimální varianta: uložit `IWDG1->PR`/`RLR` po propagaci do dvou globálů a
  přidat do `status` řádek `WATCHDOG: PR=4 RLR=2000 -> 4,0 s`. Když se hodnoty
  neshodují se zamýšlenými, označit to.
  ⚠️ **Nepokoušet se o retry ani o `Error_Handler`** — IWDG už běží (START je
  neodvolatelný) a spadnout kvůli tomu do `Error_Handler` by z nepohodlí udělalo
  nefunkčnost.
- **Riziko opravy:** nízké (dva globály + řádek výpisu, žádná změna chování).
- **Vztah k lekcím:** **L-0003** (návratová hodnota / výsledek čekání),
  **L-0017** (co se rozhodneš přejít, musí jít změřit), **L-0009**.
- **Stav:** opraveno 2026-09-17 v `76bf1f6` (lekce **L-0055**). Zvolena minimální varianta z návrhu — `PR`/`RLR` se odečtou z registrů a `status` je vypisuje i s odvozeným timeoutem a značkou `<== NESEDI`. Retry ani `Error_Handler` **vědomě ne** (IWDG už běží, START je neodvolatelný).

---

### F-0105 [S3] Záznam o stallu se píše při DETEKCI, ne při resetu — zotavený stall se ohlásí u příštího, nesouvisejícího resetu

- **Místo:** `CM7/Core/Src/watchdog.c:96-105` (`watchdog_supervise()`),
  `s_stall_logged` na `:30`
- **Popis:** Když oba heartbeaty zestárnou, funkce zapíše crash black-box
  (`stall_blackbox`) a spustí `flightrec_dump("stall")` — a **teprve pak** se
  čeká, jestli IWDG opravdu vyprší. Když se zaseknutá úloha do ~1,5 s vzpamatuje,
  reset **nepřijde**, ale záznam v BKP zůstane. Smaže ho až `MX_RTC_Init` při
  příštím bootu — tedy se přiřadí k resetu, který s ním nemá nic společného.
- **Důkaz:**
  - `:88-91`: jakmile jsou oba heartbeaty zase čerstvé, IWDG se normálně obnovuje
    → přístroj běží dál, ačkoli `stall_blackbox` už proběhl.
  - `:96-97`: `s_stall_logged` se nastaví na 1 a **nikde se nenuluje** (grep
    přes celý strom). Druhý, skutečně fatální stall jiné úlohy se proto
    **nezaznamená** a po resetu se ohlásí ten **první**.
  - BKP přežije i power-cyklus: doména je zálohovaná z **CR2032 (BT1)** —
    `alarm.h:47-49` ji popisuje jako *„VBAT = zalozni CR2032 pro RTC/BKP domenu"*
    a `alarm.c:50-51` nad ní drží práh 2800 mV. Záznam tedy může přežít dny.
  - Časové okno zotavení: heartbeat je prohlášený za mrtvý na 2500 ms
    (`WDG_STALL_MS`), IWDG má 4000 ms → **stall v rozmezí ~2,5–6,5 s se
    zaznamená, ale nezresetuje**.
- **Dopad:** `status` po příštím resetu (třeba `power-on` za dva dny) ohlásí
  `stall:UiTask`, ačkoli se tehdy nic takového nestalo. Je to přesně ta horší
  varianta z **L-0011**: *„nejhorší varianta není nezaznamená se nic, ale
  zaznamená se předchozí hodnota"*. Navíc `flightrec_dump` nastaví
  `s_dumped = 1` (`flightrec.c:265`), takže **skutečný dump později v témž běhu
  se už neprovede**.
  ⚠️ **Prověřeno a ZAMÍTNUTO jako cesta k tomuto stavu: halt ladicí sondou.**
  Napadlo mě, že halt > 2,5 s udělá přesně tohle. Nedělá: `HAL_GetTick()` jede
  z TIM6 ISR, a ta se během haltu nevykoná (čekající přerušení se navíc slučují),
  takže `uwTick` halt **nepřeskočí** a heartbeaty se zastaralé netváří.
- **Reprodukce:** `HYPOTÉZA — ověřit:` dočasně zkrátit `WDG_STALL_MS` na ~300 ms
  a vyvolat legitimní krátkou pauzu UiTasku (např. ULOŽIT v okně Kalibrace →
  erase 400 ms). Přístroj poběží dál, ale `status` po příštím `Menu → Restart`
  ohlásí `stall:UiTask`.
- **Návrh opravy:** dvě možnosti, druhá je lepší:
  (a) po návratu obou heartbeatů do normálu **záznam zneplatnit** (vynulovat
  `RTC->BKP3R`) a `s_stall_logged` vrátit na 0 — jednoduché, ale zahodí informaci
  o tom, že k zaseknutí vůbec došlo;
  (b) **rozlišit „detekováno" od „způsobilo reset"**: zotavený stall zapsat do
  `errlog` (trvalá historie, existuje od 2026-09-13, `ERRLOG_K_*`) a do BKP psát
  jen to, co se skutečně nedožilo obnovy. Zároveň přidat počítadlo zotavených
  stallů do `status` — dnes je takový stav **úplně neviditelný**.
- **Riziko opravy:** střední — sahá se na cestu, která běží těsně před resetem;
  varianta (a) je nízkoriziková, (b) potřebuje rozvahu, kam přesně zapsat.
- **Vztah k lekcím:** **L-0011**, **L-0017**, **L-0019** (účetnictví připoj ke
  ZMĚNĚ stavu — tady je připojené k *detekci*, ne k *následku*).
- **Stav:** opraveno 2026-09-17 v `76bf1f6` (lekce **L-0056**). Zvolena varianta (a) z návrhu — zneplatnění záznamu při zotavení — **rozšířená** o dvě věci, které návrh neměl: zneplatní se jen když je záznam pořád náš (magic + `kind == 3`, disciplína L-0025), a `s_stall_logged` se znovu armuje, takže se zapíše i druhý, jiný stall. Varianta (b) (zápis do `errlog`) **odložena**: závisí na F-0092 (modul 16, neschválený) a dvě opravy najednou se nedělají.

---

### F-0106 [S3] `hard_fault_capture()` zapisuje magic PRVNÍ, na rozdíl od všech ostatních zapisovatelů black-boxu

- **Místo:** `CM7/Core/Src/stm32h7xx_it.c:62-73` (`hard_fault_capture()`),
  konkrétně pořadí `:66` vs `:67-71`
- **Popis:** Projekt má explicitní invariant „data první, magic naposled", aby
  reset uprostřed zápisu nenechal platný magic nad **starými** daty. Pět ze šesti
  zapisovatelů ho dodržuje a tři to mají i v komentáři. HardFault — jediný
  dosažitelný fault vektor — ho porušuje.
- **Důkaz:**
  ```c
  66  RTC->BKP3R = 0xC7A50000u | 4u;    /* magic PRVNI */
  67  RTC->BKP4R = frame[6];            /* teprve ted data */
  68  RTC->BKP5R = cfsr;
  69  RTC->BKP7R = ...; 70  RTC->BKP8R = frame[5]; 71  RTC->BKP9R = SCB->HFSR;
  ```
  Ostatní zapisovatelé (všichni s daty první):
  `watchdog.c:46-48` — *„magic naposled"*;
  `freertos_hooks.c:51-53` — *„magic az naposled"*;
  `main.c:796-798` — *„Poradi: data prvni, magic naposled"*;
  `stm32h7xx_it.c:128-131` (NMI) — *„Poradi data-pak-magic (jako watchdog.c
  i Error_Handler), aby reset uprostred zapisu nenechal platny magic nad
  starymi daty"*;
  `stm32h7xx_it.c:156-158`, `:182-184`, `:208-210` (Mem/Bus/Usage) — taktéž.
- **Dopad:** Okno je jen šest instrukcí před `NVIC_SystemReset()`, takže náhodný
  souběžný reset je nepravděpodobný. **Reálná cesta ale existuje a je to zrovna
  ta typická:** `frame[6]` a `frame[5]` se čtou **z exception rámce**, tedy
  z paměti, na kterou ukazuje zásobník v okamžiku pádu. Když HardFault způsobil
  **rozbitý nebo přetečený ukazatel zásobníku** — což je jedna z hlavních příčin
  HardFaultů —, může čtení `frame[6]` faultovat znovu → lockup → reset IWDG,
  a v BKP zůstane **platný magic „HardFault" nad daty předchozí události**.
  `status` pak ohlásí `HF@<cizí PC>` s cizím CFSR a `addr2line` pošle hledání
  na nesouvisející řádek.
- **Reprodukce:** `HYPOTÉZA — ověřit:` `stacktest yes` (úmyslné přetečení
  zásobníku) opakovaně a porovnat hlášené PC s očekávaným. ⚠️ Nepřímé — vada se
  projeví jen když druhý fault padne do toho šestiinstrukčního okna.
- **Návrh opravy:** přehodit pořadí — `BKP4R`…`BKP9R` první, `BKP3R` (magic)
  naposled. Tři řádky, žádná změna sémantiky. Zároveň doplnit tutéž větu
  do komentáře, jakou mají ostatní.
- **Riziko opravy:** nízké. ⚠️ `hard_fault_capture` je v `USER CODE BEGIN 0`,
  takže regenerace ji nesmaže (`stm32h7xx_it.c:56-58`).
- **Vztah k lekcím:** **L-0012** (šest symetrických instancí, jedna zaostala),
  **L-0011**.
- **Stav:** opraveno 2026-09-17 v `3ccddb4` (lekce: rozšíření **L-0012**). Přesně podle návrhu — přehozeno pořadí, doloženo v disassembly.

---

### F-0107 [S3] `rtc_crash_assert()` zapisuje do BKP bez povolení `DBP`; z devíti přímých zapisovatelů ho pět povoluje a čtyři ne

- **Místo:** `CM7/Core/Src/rtc.c:265-270` (`rtc_crash_assert()`);
  srovnávací instance `watchdog.c:40`, `freertos_hooks.c:45`, `main.c:795`,
  `stm32h7xx_it.c:65`, `:122`
- **Popis:** Zápis do `RTC->BKPxR` vyžaduje odemčenou zálohovanou doménu
  (`PWR->CR1.DBP`). Pět zapisovatelů si ji preventivně odemkne, `rtc_crash_assert`
  ne — a je to ten, který běží z `configASSERT`, tedy odkudkoli včetně ISR.
- **Důkaz:**
  - `rtc.c:265-270` obsahuje jen tři zápisy do `RTC->BKPxR`, žádné `PWR->CR1`.
  - Wiring je potvrzený: `FreeRTOSConfig.h:166`
    `#define configASSERT( x ) if ((x) == 0) {rtc_crash_assert(__LINE__); taskDISABLE_INTERRUPTS(); for( ;; );}`
    — funkce je tedy **skutečně dosažitelná**, ne mrtvý kód.
  - Ostatní čtyři bez `DBP` (`stm32h7xx_it.c:156`, `:182`, `:208`) leží
    v dokumentovaně **nedosažitelných** vektorech (`:94-99`), takže u nich
    na tom nesejde.
  - Projekt tuhle přesnou chybu už jednou udělal: `stm32h7xx_it.c:126-127`
    — *„⚠️ MUSI byt AZ ZA povolenim `DBP` o radek vyse, jinak by zapis do
    zalohovane domeny propadl. (Pri prvnim pokusu jsem to mel obracene.)"*
    Totéž je zapsané v `audit-modul` §F5.3 jako past, do které se spadlo.
- **Dopad:** **Dnes to funguje** a nález to netvrdí jinak: `SystemClock_Config()`
  volá `HAL_PWR_EnableBkUpAccess()` (`main.c:618`) a nic `DBP` zpět nezamyká,
  takže od `main.c:323` je doména trvale odemčená. Vada je v tom, že
  `rtc_crash_assert` **tiše závisí na vzdáleném řádku v generovaném kódu**,
  který nemá `USER CODE` blok a regenerace ho může přesunout. Kdyby se to stalo,
  selže právě záznam o selhaném `configASSERT` — tedy diagnostika chyby.
  ⚠️ Táž závislost platí pro `freertos.c:684` `RTC->BKP10R = 0u;`, na kterém
  stojí logika „Error_Handler resetuje nejvýš jednou za pokus o start"
  (`main.c:811-818`).
- **Reprodukce:** staticky rozhodnutelné; vyvolat by to šlo jen dočasným
  zakomentováním `main.c:618`.
- **Návrh opravy:** doplnit `PWR->CR1 |= PWR_CR1_DBP;` na začátek
  `rtc_crash_assert()` a `freertos.c:684` — dva řádky, stejný idiom jako
  u ostatních pěti. Alternativa („spolehnout se na globální odemčení a napsat
  to do komentáře") je horší: komentář neplatí přes regeneraci.
  ⚠️ Ostatní čtyři (nedosažitelné vektory) **nechat být** a v nálezu je
  vyjmenovat, ať se to příště neotevírá znovu.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0012**, §F5.3 (*„zkontroluj vlastní zásah na tutéž
  třídu vady"*).
- **Stav:** opraveno 2026-09-17 v `887d9f4`. Přesně podle návrhu; zbylé tři zapisovatele v nedosažitelných vektorech **záměrně ponechány** a důvod je zapsaný v kódu i zde.

---

### F-0108 [S3] Nenaběhlý LSE zabije celý přístroj, ačkoli měření na něm nezávisí

- **Místo:** `CM7/Core/Src/main.c:624-642` (`SystemClock_Config()`),
  timeout `CM7/Core/Inc/stm32h7xx_hal_conf.h:141-143`
- **Popis:** LSE (32,768 kHz hodinkový krystal pro RTC) se konfiguruje **v jednom
  `HAL_RCC_OscConfig` volání spolu s HSE a PLL**. Když nenaběhne, celé volání
  vrátí chybu a skočí se do `Error_Handler()` — tedy blikání a reset, žádné
  měření, žádný displej, žádná konzole.
- **Důkaz:**
  ```c
  624  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE
  625                                   |RCC_OSCILLATORTYPE_LSE;
  627  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  639  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }
  ```
  `stm32h7xx_hal_conf.h:142`: `#define LSE_STARTUP_TIMEOUT (5000UL)` a
  `stm32h7xx_hal_rcc.h:8019`: `RCC_LSE_TIMEOUT_VALUE LSE_STARTUP_TIMEOUT`
  → před selháním se **5 sekund čeká**.
  Že měření LSE nepotřebuje, plyne z hodinového stromu (`docs/ARCHITECTURE.md` §4):
  SYSCLK i všechny kernel clocky měřicí cesty (FMC, SPI123/FPGA, LTDC, ADC) jedou
  z **HSE 25 MHz** přes PLL1/2/3. LSE napájí **výhradně RTC**.
- **Dopad:** Vadný nebo neosazený hodinkový krystal (případně vybitá CR2032
  v kombinaci s problémem náběhu) udělá z čítače cihlu, přestože by uměl měřit
  a jen by neznal datum. Je to stejná třída jako otevřený **F-0007** (ztráta HSE
  = mrtvý přístroj), ale s **výrazně slabším ospravedlněním**: u HSE je smrt
  měření nevyhnutelná, u LSE není.
- **Reprodukce:** `HYPOTÉZA — ověřit:` nelze bez zásahu do HW (odpájení Y2 /
  zkratování LSE). Nepřímo: `status` by po ~5 s ticha ohlásil `hal_err@<krok>`
  s krokem odpovídajícím `SystemClock_Config`.
- **Návrh opravy:** 🔴 **Vyžaduje ROZHODNUTÍ O POLITICE, ne kód** — a navíc leží
  v generovaném `SystemClock_Config()` **bez `USER CODE` bloku**, takže regenerace
  jakoukoli úpravu smaže (táž překážka jako u **F-0003**). Varianty:
  (a) **nechat být** a jen zapsat důvod — LSE na této desce prokazatelně běží,
  riziko je hypotetické (nejlevnější, nic nerozbije);
  (b) vyjmout LSE z hlavního `OscInitStruct` a zkonfigurovat ho **samostatně
  v `USER CODE`** za `SystemClock_Config()`, s tolerancí selhání a hlášením
  do `status` (RTC by pak jel z LSI nebo vůbec) — regen-safe, ale mění pořadí
  inicializace hodin, tedy přesně to, před čím varuje CLAUDE.md 4c;
  (c) řešit společně s **F-0007** jako jednu politiku „co dělat při výpadku
  oscilátoru".
- **Riziko opravy:** (a) žádné; (b) **vysoké** — zásah do hodinové inicializace;
  (c) střední.
- **Vztah k lekcím:** **L-0009** (ověřit dosažený stav v `USER CODE`, ne sahat
  do generovaného kódu), vztah k **F-0003** a **F-0007**.
- **Stav:** otevřeno

---

### F-0109 [S4] `CM7/Release` definuje `-DDEBUG`, takže IWDG freeze na haltu je aktivní i v ostrém buildu; dokumentace tvrdí opak

- **Místo:** `CM7/Core/Src/watchdog.c:53-55`;
  `CM7/Release/Core/Src/subdir.mk` (překladové příznaky); `CLAUDE.md`, sekce
  „IWDG watchdog"
- **Popis:** `__HAL_DBGMCU_FREEZE_IWDG1()` je pod `#ifdef DEBUG`. Dokumentace
  z toho vyvozuje, že v Release je freeze vypnutý. Release ale `DEBUG` definuje.
- **Důkaz:**
  - `watchdog.c:53-55`:
    ```c
    #ifdef DEBUG
        __HAL_DBGMCU_FREEZE_IWDG1();       /* na breakpointu neresetuj */
    #endif
    ```
  - Skutečné příznaky `CM7/Release`, vytažené z generovaného makefilu:
    `-mcpu=cortex-m7 -std=gnu11 **-DDEBUG** -DCORE_CM7 -DUSE_HAL_DRIVER -DSTM32H757xx …`
  - `CLAUDE.md`: *„V DEBUG buildu `__HAL_DBGMCU_FREEZE_IWDG1()` (breakpoint
    neresetuje). **Release bez freeze.**"* — nepravda.
  - Projekt o tom ví z druhé strany: *„CM4/Release NEdefinuje `-DDEBUG`
    (CM7/Release ano)"* je v CLAUDE.md o dva odstavce dál. Obě věty stojí
    v témže souboru a odporují si.
- **Dopad:** Žádný funkční — freeze na haltu je spíš žádoucí. Vada je, že
  dokumentace popisuje jiné chování, než jaké má naflashovaný firmware, a je to
  chování **watchdogu**, tedy věci, u které se předpoklady nehádají.
  Kdo by ladil „proč mi sonda nezresetovala desku", hledal by to jinde.
- **Reprodukce:** doložitelné z příznaků překladu (viz důkaz).
- **Návrh opravy:** `docs:` commit — opravit větu v `CLAUDE.md` a u `#ifdef DEBUG`
  ve `watchdog.c` poznamenat, že **CM7/Release `DEBUG` definuje**, takže je
  větev aktivní i tam. ⚠️ Kód neměnit: odstranění freeze by ladění zhoršilo.
- **Riziko opravy:** žádné (jen dokumentace).
- **Vztah k lekcím:** **L-0028** (testovatelná věta v komentáři/dokumentaci —
  tahle se testuje jedním grepem do `subdir.mk`).
- **Stav:** opraveno 2026-09-17 v `0b0e5b6` (`docs:`). Opraveno v `CLAUDE.md` i poznámkou přímo u `#ifdef DEBUG`; kód se záměrně nemění.

---

### F-0110 [S4] `beeper_tone()` nevynucuje mez kmitočtu; pod ~8 Hz 16bitový `ARR` tiše přeteče

- **Místo:** `CM7/Core/Src/beeper.c:60-71`
- **Popis:** `arr = 1000000u / (2u * freq_hz)` se zapisuje do `TIM7->ARR`, který
  je na TIM7 **16bitový**. Pro `freq_hz < 8` vyjde `arr > 65535` a zápis se
  ořízne — tón bude úplně jiný, než volající požadoval, a nic to neohlásí.
- **Důkaz:**
  - `:63-65`: `uint32_t arr = 1000000u / (2u * (uint32_t)freq_hz); if (arr) arr--;
    __HAL_TIM_SET_AUTORELOAD(&htim7, arr);`
  - TIM7 je basic timer s 16bitovým čítačem (RM0399); `arr` pro `freq_hz = 7`
    vychází **71 427**, tedy `& 0xFFFF` = 5 891 → místo 7 Hz zazní ~85 Hz.
  - `freq_hz == 0` je ošetřeno (`:62`), záporné hodnoty typ nedovolí.
- **Dopad:** **Dnes nedosažitelné** — jediný volající je `beeper_boot_melody()`
  (`:75-86`) s tóny 784–1568 Hz (ověřeno grepem: mimo `beeper.c` `beeper_tone`
  nikdo nevolá). Je to past pro příštího volajícího, a `beeper.h` funkci
  vystavuje. Přesně třída **F-0053** (`fmt_fixed` s `dec >= 4`), která
  v tomhle projektu **už jednou kousla**.
- **Reprodukce:** staticky rozhodnutelné.
- **Návrh opravy:** clamp na začátku (`if (freq_hz < 16) freq_hz = 16;`) nebo
  ořez `arr` na 65535 — jeden řádek. Mez patří do funkce, ne do komentáře.
- **Riziko opravy:** žádné (mění chování jen pro vstupy, které dnes nikdo nedává).
- **Vztah k lekcím:** **L-0015** (*„když modul zná svou mez, musí ji na rozhraní
  vynutit"*), **L-0030**.
- **Stav:** opraveno 2026-09-17 v `389690c` (lekce: další výskyt **L-0015**/**L-0030**). Přesně podle návrhu — clamp před výpočtem, mez v hlavičce.

---

### F-0111 [S4] `beeper.c` nevyhodnocuje ani jednu návratovou hodnotu HAL; selhání TIM7 znamená trvale němý přístroj bez hlášení

- **Místo:** `CM7/Core/Src/beeper.c:30` (`HAL_GPIO_Init`), `:40`
  (`HAL_TIM_Base_Init`), `:52`, `:69` (`HAL_TIM_Base_Start_IT`), `:54`
  (`HAL_TIM_Base_Stop_IT`)
- **Popis:** Pět volání HAL, žádné vyhodnocené a žádné zdůvodnění, proč se to
  ignoruje. `beeper_init()` je `void` a volajícímu nic nevrací.
- **Důkaz:** `beeper.c:40` `HAL_TIM_Base_Init(&htim7);` — návrat zahozen;
  `:52` `HAL_TIM_Base_Start_IT(&htim7);` — dtto. Pro srovnání: `MX_RTC_Init`
  (`rtc.c:79-82`, `:196-199`, `:205-208`) i `HAL_RTC_MspInit` (`:229-232`)
  návratové hodnoty kontrolují.
- **Dopad:** Když TIM7 nenaběhne, přístroj **mlčí navždy** — a mlčí i alarm na
  ztrátu 10MHz reference a na vyběhnutí OCXO z pásma, tedy ty dvě věci, kvůli
  kterým zvuk existuje. Uživatel to nemá jak poznat: `beep test` také nic
  neudělá a odpoví, jako by pípl.
  ⚠️ Pravděpodobnost selhání `HAL_TIM_Base_Init` je nízká (nekontroluje HW),
  proto S4 — ale cena za kontrolu je jeden `if`.
- **Reprodukce:** staticky rozhodnutelné.
- **Návrh opravy:** `beeper_init()` ať vrací `bool` a výsledek se uloží do
  globálu, který ukáže `status` (řádek `BEEPER: ok` / `CHYBA`). Alternativa,
  která je v tomhle projektu přijatelná a levnější: nechat `void`, ale
  **zdůvodnit komentářem**, proč se návraty ignorují (to **L-0003** výslovně
  připouští). ⚠️ `Error_Handler()` tu volat **nechci** — němý pípák není důvod
  shodit měřicí přístroj.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0003**, **L-0017**.
- **Stav:** opraveno 2026-09-17 v `389690c` + `76bf1f6` (řádek `status`). ⚠️ **Upřesnění proti nálezu:** `HAL_GPIO_Init` je v HAL `void`, takže z pěti volání jsou vyhodnotitelná čtyři. Zvolena varianta „vracet `bool` a hlásit"; návraty `Start_IT`/`Stop_IT` se ignorují **vědomě a zdůvodněně** (L-0003 to připouští).

---

### F-0112 [S4] Dvě protichůdná zdůvodnění ceny `gps_get()`; to přísnější je měřením vyvrácené

- **Místo:** `CM7/Core/Src/alarm.c:261` vs. `CM7/Core/Src/rtc.c:503-506`;
  měřená skutečnost `CM7/Core/Src/gps.c:468-473`
- **Popis:** Dva soubory téhož modulu odvozují opačný závěr ze stejného faktu.
  `alarm.c` **škrtí vyhodnocení na 5×/s** s odůvodněním, že `gps_get` je drahý;
  `rtc.c` volá tutéž funkci **~100×/s** a odůvodňuje, proč to tak být musí.
  Obě zdůvodnění nemohou platit zároveň.
- **Důkaz:**
  - `alarm.c:261`: *„Vyhodnoceni stavu (hrany) jen 5x/s — gps_get kopiruje
    ~200B v kriticke sekci."*
  - `rtc.c:503-506`: *„⚠️ ZAMERNE PRED throttlem: vzorkovac driftu musi bezet
    tempem defaultTasku (~100 Hz)…"* → `rtc_lse_sample()` (`:431-434`) volá
    `gps_get()` jako první příkaz.
  - Skutečná cena, `gps.c:468-473`:
    ```c
    void gps_get(gps_data_t *out) { taskENTER_CRITICAL(); *out = s_gps; taskEXIT_CRITICAL(); }
    ```
    tedy kopie ~200 B. Při 480 MHz je to řádově **0,2 µs**; 100×/s ≈ **20 µs/s**,
    což je 0,002 % CPU a 0,002 % doby s maskovanými přerušeními.
    **Zdůvodnění v `alarm.c` tedy neplatí** — cena je zanedbatelná.
- **Dopad:** Žádný funkční (5×/s je pro hrany alarmu dost). Vada je v tom, že
  v kódu stojí **nepravdivý důvod pro škrcení** — příští úprava se podle něj
  zařídí (např. nebude chtít přidat podmínku, která `gps_get` potřebuje),
  nebo se naopak zbytečně přidá další throttle.
  ⚠️ Pravý důvod pro 5×/s existuje a je jiný: hrany alarmu nemá smysl
  vyhodnocovat rychleji, než se stav mění (GPS 1 Hz, senzory 2 Hz).
- **Reprodukce:** doložitelné výpočtem výše; přímé měření by šlo přes DWT kolem
  `gps_get` (nestojí to za jeden build).
- **Návrh opravy:** `docs:` commit — přepsat komentář v `alarm.c` na skutečný
  důvod (kadence vstupních dat) a v `rtc.c` doplnit, že cena `gps_get` je
  změřeně zanedbatelná, takže 100 Hz vzorkování nic nestojí.
- **Riziko opravy:** žádné (komentáře).
- **Vztah k lekcím:** **L-0028** (věta v komentáři je testovatelná — tahle
  neobstála), **L-0018** (dvě místa, dvě pravdy).
- **Stav:** opraveno 2026-09-17 v `0b0e5b6` (`docs:`). Opraveno v obou souborech; spočítáno, že to přísnější zdůvodnění je vyvrácené (~0,002 % CPU).

---

## Co bylo zkontrolováno a je v pořádku

**Kontrakt BKP ↔ `syscfg` (premisa opravy F-0089)** — výčet v `MX_RTC_Init`
(`rtc.c:89-120`) obsahuje právě deset globálů a nic víc; kontrola přidaná
2026-09-17 do `check_lessons.sh` z něj tedy odvozuje správnou množinu. Kódování
ověřeno round-tripem: schéma (bit0 + bity 9:10, `rtc.c:111` vs `:607`/`:612`),
zóna (`tz+13` na bitech 2:6, `:117` vs `:609`), auto-dim (`/15` na bitech 10:15,
`:100` vs `:604`) — všechno se čte tak, jak se zapsalo, a `g_anim_enabled`
na bitu 8 se maskou `0x06` správně vynechá.

**Kanonická sekvence IWDG** (`watchdog.c:59-68`) odpovídá RM0399 i HAL:
START **před** UNLOCK/PR/RLR, protože teprve START rozběhne LSI a bez běžících
hodin by se update `PR`/`RLR` nikdy nepotvrdil. Čekání na `SR` je **ohraničené**
(`L-0004` splněna). Nekonečná smyčka v modulu není žádná.

**`rtc_lse_apply_calib` se vyhýbá tísňové smyčce uvnitř HAL** — `HAL_RTCEx_SetSmoothCalib`
čeká na `RECALPF` **těsnou smyčkou bez yieldu s timeoutem 1000 ms**, což by
v defaultTasku porušilo pravidlo „žádný spin > ~10 ms". Kód proto `RECALPF`
testuje **předem** (`rtc.c:412`) a kolo raději přeskočí. Ověřeno, že je ta obrana
těsná: `RECALPF` nastavuje **jen** zápis do `CALR`, a ten dělá jediné místo
nejvýš 1×/hodinu — mezi testem a voláním ho tedy nikdo nastavit nemůže.

**Matematika smooth kalibrace** (`rtc.c:403-427`) přepočítána nezávisle:
korekce v ppm = `(CALP·512 − CALM) · 2⁻²⁰ · 10⁶` = `(CALP·512 − CALM) · 0,95367`.
Volba `CALP`/`CALM`, znaménko i zpětný dopočet `s_lse_cal_ppm` sedí; `CALM`
je 9bitové a kód ho clampuje na 0..511 (`:419-420`). Saturace při extrémním
požadavku je bezpečná. `LSE_PPM_SANE = 250` navíc drží vstup hluboko pod mezí.

**Sub-sekundová fáze** (`rtc.c:381-398`): `HAL_RTC_GetTime` je volané **před**
`GetDate` (odemčení shadow registrů) a `SubSeconds`/`SecondFraction` se berou
z téhož volání; `SubSeconds` odpočítává dolů, takže `(sf − SubSeconds)/(sf+1)`
je správný uplynulý zlomek. Zabalení rozdílu do ±30 s řeší přechod přes minutu.
Skokové srovnání času zahodí referenci driftu (`:495-498`) — bez toho by další
okno naměřilo skok jako obří „drift".

**`HAL_TIM_PeriodElapsedCallback` je sdílený a správně rozlišuje instanci**
(`main.c:760-774`): TIM6 → `HAL_IncTick()`, TIM7 → `beeper_isr_toggle()`.
Kdyby chyběl `else if`, tikal by pípák z HAL timebase na 1 kHz. **Není to tak.**

**Priority přerušení** (checklist D): TIM7 má prioritu **6**
(`beeper.c:42`), tedy číselně nad `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5`
→ je maskovatelný kritickou sekcí a **nevolá žádné RTOS API**
(`TIM7_IRQHandler` → `HAL_TIM_IRQHandler` → callback → `HAL_GPIO_TogglePin`).
`HAL_GPIO_TogglePin` píše přes `BSRR`, což je atomické per pin — souběh
s jinými zápisy do GPIOH (I2C4 na PH11/PH12) tedy nehrozí.

**GPIOH nemá závod o konfiguraci.** `HAL_GPIO_Init` dělá neatomický RMW nad
`MODER`/`OTYPER`/`PUPDR`, ale obě místa, která ho na GPIOH volají, jsou mimo
souběh: `beeper_init()` běží v `main()` **před schedulerem** (`main.c:476`)
a `bootled` `beep_gpio_init()` buď rovněž před schedulerem
(`bootled_blink_once`, `main.c:503/512/520/529`), nebo až po `__disable_irq()`
v `Error_Handler` (`main.c:799`). Proto GPIOH **nepotřebuje** být v `GG_PINS`.

**`dwt_enable()` nuluje `DWT->CYCCNT`, ale nikdy za běhu RTOS.** Prověřeno,
protože ten čítač používají i FreeRTOS RunTimeStats, `delay_us` ve `fpga_freq.c`
a `membench`: všichni volající (`bootled_blink_once`, `bootled_fail`,
`bootled_fail_n`) jsou buď před schedulerem, nebo v `Error_Handler` po
`__disable_irq()`. **Žádná cesta z běžící aplikace neexistuje.**

**Hranové guardy alarmu** jsou konzistentní a podložené: `s_*_ever` u FPGA/GPS/
limitu i `mon_edge` (`alarm.c:167-188`) mají v komentáři **naměřený** důvod
(startovní transient VBAT 2516 mV proti ustáleným 2932 mV). Armování ΔT
kritéria (`:226`) správně řeší studený start. `band_eval` s `MON_INF = 1e30f`
prověřeno na aritmetiku `float`: `hi − hyst` i `lo + hyst` zůstávají `±1e30`
(ULP ~1e23), takže jednostranné meze fungují.

**Mute cesta** (`alarm.c:251-253`, `:282-291`): vyhodnocení `mon_eval()` běží
**před** mute větví, takže `g_mon_*_bad` odráží skutečnost i při ztlumeném zvuku
— a `prev` stavy se při mute dál aktualizují, takže po odmutění nepípne stará
hrana. Odchod z `alarm_tick` při throttlu (`:264`) je až **za** `pattern_service`
a klikem, takže neshazuje nic, co je za ním (**L-0033** splněna).

**Umístění statik** ověřeno `nm` nad `.elf`: `g_mon_cfg` (28 B), `hrtc` (36 B),
`htim7` (76 B), všech 10 `s_lse_*`, `s_ui_ms`/`s_fpga_ms`, `s_phase*` — **všechno
v `.bss`/`.data` v AXI SRAM (`0x2400xxxx`)**, nic v DTCM, nic na zásobníku
(**L-0001** splněna konstrukcí). Modul **nepoužívá DMA** — sekce C checklistu
odpadá.

**Zásobníky (L-0035, měřeno nad `.elf`):** `rtc_app_tick` **288 B**,
`alarm_tick` **212 B**, `MX_RTC_Init` 48 B, `beeper_init` 32 B. Statické funkce
(`rtc_lse_sample`, `rtc_try_sync`, `mon_eval`, `watchdog_supervise`) se
inlinovaly do volajících, takže jejich lokály (`gps_data_t` ~200 B) jsou už
v těch číslech započtené. Proti změřeným **1 728 B** volným v defaultTasku je
to i s `snprintf` pohodlné.

**Prověřeno a ZAMÍTNUTO jako nález:**
- *Halt ladicí sondou vyrobí falešný `stall`* — **vyvráceno**: `HAL_GetTick()`
  jede z TIM6 ISR, která se během haltu nevykoná (čekající přerušení se slučují),
  takže `uwTick` halt nepřeskočí a heartbeaty nezestárnou. Byla to moje hypotéza
  k F-0105 a čísla ji nepotvrdila (**L-0011**).
- *Podtečení `s_pulses_left` na 255 pípnutí* — nedosažitelné: `pattern_start`
  nastavuje `s_pulses_left` **před** `s_phase`, takže stav
  `s_phase == 1 && s_pulses_left == 0` nevznikne.
- *`beeper_set(false)` nechá pin nahoře kvůli čekajícímu TIM7 přerušení* —
  nehrozí: `HAL_TIM_Base_Stop_IT` zdroj přerušení zakáže a `HAL_TIM_IRQHandler`
  testuje `ITSource`, takže případné doběhnuté vektorování pin nepřepne;
  `WritePin(RESET)` je navíc až za `Stop_IT`.
- *Přetečení v `bootled delay_ms`* — `ms · (SystemCoreClock/1000)` přeteče až
  od ~8 948 ms; používané hodnoty jsou 150 a 800.
- *`MX_RTC_Init` předčasný `return` rozbije regeneraci* — `return` leží uvnitř
  `USER CODE Check_RTC_BKUP` (`rtc.c:184-187`), takže je regen-safe.
- *Kvantizace `g_autodim_sec` na 15 s při ukládání do BKP* — bezztrátová,
  všechny presety v UI jsou násobky 15 a rozsah 15..600 se do 6 bitů vejde.

## Nezkontrolováno / omezení tohoto běhu

- **Nic z tohoto modulu nebylo spuštěno na HW.** Tři nálezy (`F-0103`, `F-0104`,
  `F-0105`) mají reprodukci označenou jako **HYPOTÉZA** včetně návodu, co změřit;
  `F-0106` je dosažitelný jen nepřímo a `F-0108` vyžaduje zásah do osazení.
- **Chování LSE při studeném startu není změřené.** Tvrzení „LSE na této desce
  prokazatelně běží" stojí na tom, že RTC drží čas — ne na měření doby náběhu.
  Kdyby se krystal rozbíhal blízko 5 s, projevilo by se to jako prodloužený boot,
  což nikdo nesledoval.
- **Crash black-box byl posuzován jen ze strany zapisovatelů.** Čtecí stranu
  (`MX_RTC_Init:125-178`) jsem přečetl, ale kombinace „jaký `kind` co znamená"
  napříč všemi devíti zapisovateli by si zasloužila vlastní tabulku; dnes žije
  roztroušeně v komentářích. Souvisí s otevřeným **F-0092** (modul 16).
- **`freertos_hooks.c` je modul 4** a nebyl zde znovu čten; F-0103 ani F-0107 se
  ho netýkají, ale otevřený **F-0018** (dump z hooku se nikdy neprovede) sousedí
  s F-0105 — obojí je o tom, že se diagnostika poruchy nedostane tam, kam má.
- **Drift LSE nebyl ověřen proti skutečnosti.** Matematiku jsem přepočítal, ale
  jestli `rtc_lse_ppm()` konverguje k reálnému driftu krystalu, se pozná až
  z několikahodinového běhu s anténou (`rtc cal`, ≥12 oken ≈ 2 h).
