# Audit: poslední úpravy — přezkum vlastních oprav d2d_wait + SD zrcadlo (2026-09-24)

- **Commit:** `438c874` (HEAD). Auditovaný rozsah = dva konkrétní commity ze
  stejné session, na žádost uživatele ("Poslední úpravy" — viz
  `AskUserQuestion` v konverzaci): `1d18c28` (d2d_wait yield fix) a
  `caa5f70` (SD zrcadlo F-0142/F-0143/F-0146). Oba už prošly cílenou
  diagnostikou a HW ověřením (`docs/audit/2026-09-24_sezeni-2026-09-23-24.md`,
  `docs/audit/2026-09-10_preruseni-rtos.md`) — tenhle běh je **druhý,
  nezávislý průchod nad HOTOVÝMI opravami**, ne nad původní vadou. Cíl:
  chytit regrese, které vznikly PŘI psaní oprav (přesně třída chyby, na
  kterou upozorňuje F5.3 v `/audit-modul`: "zkontroluj vlastní zásah na
  tutéž třídu vady, kterou opravuješ").
- **Jádro / doména:** CM7. `d2d_wait()` = vykreslovací řetězec (`app/hal/stm32`),
  SD zrcadlo = úložná vrstva (`Core/Src/datalog_sd.c`).
- **Projité sekce checklistu:** D (souběh/RTOS), E (chyby/robustnost).
- **Neprojité (a proč):** A/B/C/F/G/H — rozsah je úzký (2 komity, žádná
  změna hodin, DMA umístění bufferů (mimo ověření polohy nového `.bss`
  bufferu v `.map`), flash geometrie ani periferní inicializace.

## Souhrn

**Verdikt: podmíněně funkční — jeden nový S2 nález.** `d2d_wait()` yield fix
je čistý (jeden drobný S4 — nepřesný komentář o scénáři, který v aktuálním
call-graphu nenastává). SD zrcadlo má ale **novou regresi (F-0148 [S2])**:
oprava F-0143 (detekce smazání W25Q logu) znovu zavádí **přesně tu chybu,
kterou o pár desítek řádků výš ve stejném souboru a stejném commitu opravuje
F-0146** — nastaví `s_mirror_open = false` bez `f_close()`. Protože FatFs
na této desce má `_FS_LOCK = 2` (potvrzeno v `ffconf.h`), stejný mechanismus,
který u F-0146 blokoval `f_open()` po vypnutí/zapnutí zrcadla, tu po
`datalog erase` (za aktivního zrcadla) pravděpodobně natrvalo zablokuje
opětovné vytvoření souboru — a na rozdíl od F-0146 tentokrát **ani
`datalog mirror off`/`on` neopraví stav**, protože `s_mirror_open` je už
`false` a `f_close()` guard v `datalog_mirror_set_enabled()` se přeskočí.

---

### F-0148 [S2] `datalog_mirror_service()` (oprava F-0143) znovu zavádí únik FatFs handle, opravený o 20 řádků výš jako F-0146

- **Místo:** `CM7/Core/Src/datalog_sd.c:726-732` (`datalog_mirror_service()`,
  blok detekce smazání W25Q logu, přidaný commitem `caa5f70`). Pro srovnání
  správně opravená sesterská instance: `datalog_sd.c:490-497`
  (`datalog_mirror_set_enabled(false)`, F-0146, opravená ve **stejném
  commitu**).
- **Popis:** Blok F-0143 nastavuje `s_mirror_open = false` (řádek 731) BEZ
  předchozího `f_close(&s_mirror_fil)` — přesně ten vzor, který F-0146
  (opravený o pár desítek řádků výš v témže souboru, `caa5f70`) označil za
  příčinu úniku FatFs handle. `s_mirror_fil` zůstane v interní FatFs tabulce
  zámků (`_FS_LOCK`) vedený jako otevřený objekt, přestože ho program dál
  nepovažuje za otevřený.
- **Důkaz:**
  - `CM7/FATFS/Target/ffconf.h:231`: `#define _FS_LOCK 2` — souborové zámky
    JSOU zapnuté na této desce (tabulka 2 položek). Bez `_FS_LOCK` by tenhle
    mechanismus vůbec neexistoval a nález by nebyl platný.
  - Grep všech míst, kde se `s_mirror_open` nastavuje na `false`
    (`datalog_sd.c`): tři místa mimo `mirror_open()`'s vlastní úspěšné
    otevření — `:709` (odpojení karty, **má** komentář vysvětlující proč je
    bez `f_close()` v pořádku: `f_mount(NULL,...)` při odmountování sám
    zneplatní všechny handly té sběrnice), `:731` (**tento nález** — žádné
    takové zdůvodnění, žádný `f_close`), `:747` (chybová větev
    `mirror_write_batch()`, **má** `f_close()` bezprostředně před sebou).
    F-0148 je tedy JEDINÉ ze čtyř míst nastavujících `s_mirror_open=false`,
    které nemá ani `f_close()`, ani zdůvodnění, proč je bezpečné bez něj.
  - `datalog_mirror_service()` a `datalog_erase_all()` (volané z UART
    příkazu `datalog erase`, `freertos_task_uart.c:1785`) běží ve STEJNÉM
    tasku (UartTask, `freertos_task_uart.c:2997` volá
    `datalog_mirror_service()` v hlavní smyčce hned po zpracování příkazu),
    takže po `datalog erase` je scénář spolehlivě a rychle reprodukovatelný
    — detekce (`ds0.last_seq < s_mirror_seq`) se spustí hned v **další**
    iteraci smyčky UartTask, řádově do 1 ms.
  - Návazný `mirror_open()` (řádek 738, volané ve STEJNÉM běhu funkce po
    resetu `s_mirror_vsn=0`) jde větví `FA_CREATE_ALWAYS` (`:625`) na
    STEJNÉ jméno souboru (`DL_MIRROR_FILE`), tedy přesně ten typ konfliktu,
    jaký F-0146 demonstroval na `FA_READ|FA_WRITE` — `_FS_LOCK` kontroluje
    duplicitní otevření podle cesty/clusteru, ne podle přístupového módu.
- **Dopad:** Po `datalog erase` se zapnutým zrcadlem se SD zrcadlo
  pravděpodobně **natrvalo zasekne** — každý další tik
  `datalog_mirror_service()` zkusí `mirror_open()`, ten selže na
  `FR_LOCKED`, `datalog_mirror_status`/`status` bude hlásit
  `"vytvoreni souboru selhalo"` donekonečna. 🔴 **Na rozdíl od F-0146 tenhle
  stav NEJDE opravit obvyklou cestou** (`datalog mirror off` → `on`):
  `datalog_mirror_set_enabled(false)` zavolá `f_close()` jen
  `if (s_mirror_open)` (řádek 490) — a `s_mirror_open` je v tomhle stavu
  už `false` (nastavil ho právě tenhle nález), takže se `f_close()`
  přeskočí a STARÝ, opravdu zaseknutý zámek se dál nikdy nezavře. Jediné
  známé zotavení: odpojení/opětovné namountování karty (řádek `:709` cestou
  skutečně invaliduje handly celého svazku přes `f_mount(NULL,...)`) nebo
  reset MCU (zámková tabulka žije jen v RAM).
- **Reprodukce:** HYPOTÉZA (staticky doložitelná z `_FS_LOCK` mechanismu a
  z už jednou **empiricky potvrzeného** identického chování u F-0146 —
  není to spekulace o neznámém chování FatFs, je to stejný mechanismus,
  který dnes na této desce už jednou předvedl přesně tenhle symptom).
  Neověřeno na HW, protože ověření vyžaduje `datalog erase` nad produkčním
  logem (160 000+ záznamů) — destruktivní operace nad živými daty, viz
  `docs/audit/2026-09-24_sezeni-2026-09-23-24.md` "Nezkontrolováno" (tatáž
  výhrada platila už pro F-0143 samotné). Postup k ověření: `datalog
  mirror on` → počkat na `aktivni` → `datalog erase` → sledovat `status`/
  `datalog mirror` přes několik tiků → očekávat trvalé
  `"vytvoreni souboru selhalo"`.
- **Návrh opravy:** Stejný vzor jako F-0146 — než se v bloku F-0143 nastaví
  `s_mirror_open = false`, nejdřív (pokud `s_mirror_open` je `true`) zavolat
  `f_close(&s_mirror_fil)` obalené `sd_blocking_begin/end` +
  `sd_export_busy_begin/end` (tahle dvojice v `datalog_mirror_service()`
  začíná až o pár řádků níž, takže by se musela buď posunout před erase-blok,
  nebo si erase-blok obalit vlastní instancí — riziko viz níže).
- **Riziko opravy:** nízké až střední. Nízké, pokud se `sd_blocking_begin/end`
  scope prostě rozšíří tak, aby zahrnoval i erase-detekci (posune blok pár
  řádků níž za `sd_blocking_begin()`); střední, pokud se přidá DRUHÁ
  instance `sd_blocking_begin/end` jen pro tenhle `f_close` — `L-0078`
  varuje, že `sd_blocking_begin()` snižuje prioritu volajícího tasku, takže
  dvě zanořené/oddělené instance za sebou nejsou nebezpečné (UartTask je
  jediný volající), ale zbytečně zdvojují kód, který už o pár řádků níž
  jednou existuje.
- **Vztah k lekcím:** `L-0080` (vypnutí funkce, která drží handle, musí
  handle zavřít, ne jen shodit příznak) — **toto je DRUHÝ výskyt téže
  lekce v TÉŽE session**, tentokrát ne jako důsledek vedlejšího efektu
  (přidání `FA_READ`), ale jako přímé zopakování anti-vzoru při psaní
  sesterského kódu o pár řádků níž. Také `L-0012` (oprava se neaplikovala
  na dvojče — tady je to obrácené: dvojče vzniklo AŽ PO opravě prvního a
  zdědilo jeho starou chybu).
- **Stav:** **opraveno 2026-09-25**, přesně dle návrhu výše (varianta A).
  `sd_blocking_begin()/sd_export_busy_begin()` přesunuty PŘED F-0143 blok
  (byly pár řádků pod ním); uvnitř `if (ds0.ready && ...)` přidán
  `if (s_mirror_open) f_close(&s_mirror_fil);` před reset příznaků —
  stejný vzor jako F-0146. `datalog_sd.c:717-747`.
  Build 0 varování, `audit.py` 92/0/2. `.text` beze změny (615096 B) —
  ověřeno PŘÍMO v `.elf` (rule F5.2 bod 4, ne jen bod 3): `mirror_open()`
  je `static` s jediným volajícím a `-Os` ho zjevně inlinuje do
  `datalog_mirror_service`, takže počet volání `f_close` uvnitř
  disassemblovaného rozsahu funkce vzrostl **3→4** (`objdump -d
  --start-address=0x08003118 --stop-address=0x08003488 | grep -c "bl.*f_close"`)
  — nová podmíněná volání je v obraze prokazatelně přítomné, i když se
  celkový `.text` shodou okolností nezměnil o jediný bajt.
  ✅ **Naflashováno a regresně ověřeno na HW 2026-09-25** (první pokus
  2026-09-24 selhal — deska byla dočasně bez napájení/odpojená,
  `STM32_Programmer_CLI`: "Voltage: 0.00V"; o pár minut později znovu
  dostupná). Po flashi + SW resetu: `selftest` → `SELFTEST: 16/16 PASS`,
  `status` zdravý (LTDC podtečení 0/532, DMA2D chyb 0, GPIO hlídač
  0 oprav), **4× opakovaný `datalog mirror off/on` cyklus bez chyby**
  (watermark stabilní na seq 160483, žádná regrese normální cesty).
  🔴 **Původní CÍLOVÝ scénář (`datalog erase` nad aktivním zrcadlem)
  zůstává neověřený** — destruktivní operace nad živými daty (160 000+
  záznamů), neprovedeno bez výslovného souhlasu uživatele, stejná
  výhrada jako u F-0142/F-0143 samotných.

---

### F-0147 [S4] Komentář u `d2d_wait()` tvrdí scénář "před schedulerem", který v současném call-graphu nenastává

- **Místo:** `CM7/app/hal/stm32/prim_stm32_hal.c:177-179` (komentář u pomalé
  větve `d2d_wait()`, přidaný `1d18c28`).
- **Popis:** Komentář tvrdí, že `prim_stm32_init()` (jediné volání, které
  čistí 3 framebuffery přes `d2d_fill()` hned při startu) běží "před
  schedulerem", a proto tam `osKernelGetState() != osKernelRunning` a kód
  spadne na čistý spin. To je fakticky nepravda pro **tuhle konkrétní**
  volací cestu — `prim_stm32_init()` se volá výhradně z `app_gpsdo_init()`
  (`app_gpsdo.c:358`), tu volá `window_prep()` (`app_gpsdo.c:168`), tu volá
  `app_gpsdo_render_main()` (`app_gpsdo.c:371`), tu volá **`StartUiTask`**
  (`freertos_task_ui.c:218`) — což je tělo FreeRTOS tasku, tedy kód, který
  se PRINCIPIÁLNĚ nemůže vykonat dřív, než `osKernelStart()` (`main.c:583`)
  spustí scheduler. `osKernelGetState()` je tedy při volání z
  `prim_stm32_init()` VŽDY `osKernelRunning`; větev "not running" v tomhle
  volání nikdy nenastane.
- **Důkaz:** `grep -n "prim_stm32_init(" CM7/` → jediný volající
  `CM7/app/app_gpsdo.c:358`, uvnitř `app_gpsdo_init()`, s guardem
  `if (s_inited) return;` (volá se jen jednou). Řetězec volajících výše
  end v `StartUiTask` (`CM7/Core/Src/freertos_task_ui.c:186-218`), což je
  `osThreadNew` callback — FreeRTOS task tělo běží jen po
  `osKernelStart()`.
- **Dopad:** Žádný funkční — `osKernelGetState()` runtime kontrola je
  bezpečná v obou směrech (funguje správně, ať je scheduler běžící nebo
  ne), takže se nic nerozbije. Dopad je čistě na **důvěryhodnost
  komentáře**: budoucí čtenář (nebo budoucí "já") by mohl na základě téhle
  věty usoudit, že existuje legitimní early-boot volací cesta do
  `d2d_wait()` před schedulerem, a tomu přizpůsobit další úpravy (např.
  přidat kód, který na to spoléhá), zatímco ve skutečnosti dnes žádná
  taková cesta neexistuje — jde čistě o defenzivní kód pro hypotetickou
  budoucí volací cestu (podobnou `sd_wait_ready()`/`w25q.c wait_ready()`,
  které TAKOVOU cestu skutečně mají, viz `main.c` USER CODE 2 inicializace
  před schedulerem).
- **Reprodukce:** staticky doložitelné (viz Důkaz), HW není potřeba.
- **Návrh opravy:** `docs:` úprava komentáře — nahradit tvrzení "při
  bootovacím čištění 3 framebufferů" obecnějším a přesným zdůvodněním, že
  jde o defenzivní pojistku pro budoucí volací cesty (po vzoru
  `sd_wait_ready`), ne o popis existujícího stavu. Žádná změna kódu.
- **Riziko opravy:** nulové (`docs:` komentář).
- **Vztah k lekcím:** žádná existující specificky o komentářích s
  nedoloženými tvrzeními o volací cestě; blízké obecnému duchu `L-0011`
  (nevěřit tvrzení, které samo sebe neověřuje) — nezakládat novou lekci,
  jde o kosmetiku bez dopadu na chování.
- **Stav:** opraveno 2026-09-25 (`docs:` — komentář v `prim_stm32_hal.c:177-180`
  nahrazen přesným zněním: guard `osKernelGetState()` je defenzivní pojistka pro
  hypotetickou budoucí early-boot cestu, ne popis existujícího stavu; větev
  "not running" se v současném call-graphu nikdy nevykoná). Build 0 varování,
  `audit.py` 92/0/2, `.text` beze změny (615096 B). Bez lekce (kosmetika).

---

## Co bylo zkontrolováno a je v pořádku

- **`d2d_wait()` yield umístění volání:** všech 7 volání `d2d_wait()`
  (`prim_stm32_hal.c:226,242,252,263,328,346,471`) běží výhradně z DMA2D
  backendu volaného skrze `prim_fill_rect`/`prim_blit*`, tedy výhradně z
  UiTask (dokumentovaný invariant "kreslí VÝHRADNĚ UiTask"). Yield uvnitř
  `d2d_wait()` proto nemůže vnést cross-task race do `mark_dirty`/dirty-rect
  stavu — žádný jiný task nevolá DMA2D primitiva souběžně.
- **`D2D_WAIT_MS` časová mez zůstala nezměněná** (500 ms) — `osDelay(1)`
  mění JEN způsob čekání (yield místo spinu), ne kdy se přenos zruší;
  potvrzeno i měřením na desce (tempo flipu nezměněné před/po, viz
  `2026-09-24_sezeni-2026-09-23-24.md`).
  Watchdog dopad: i kdyby `d2d_wait()` vyčerpal celých 500 ms, je to
  5× pod 2,5 s prahem `watchdog_supervise` (existující, needitovaná mez).
- **`mirror_recover_seq_from_file()` — formát řádku ověřen proti
  SKUTEČNÉMU zapisovači** (`sd_export_csv_row`/`sd_export_csv_header`,
  `sd_export.c:258-274`): obě funkce ukončují řádek `"\r\n"`, přesně tím,
  co parser hledá (`buf[i-1]=='\r' && buf[i]=='\n'`). Bez tohohle ověření
  by šlo snadno předpokládat neshodu formátu (jiný projekt/writer v historii
  této session psal bez `\r`) — ověřeno přímo v aktuálním zdrojovém páru,
  ne odhadem.
- **`DL_MIRROR_TAIL_BUF` (640 B) rezerva proti skutečné délce řádku:**
  ruční odhad max. délky řádku `sd_export_csv_row` (10 polí + separátory +
  `\r\n`) vychází ~85-90 B v nejhorším případě (uint32 seq/unix na 10 cifer,
  freq na 15 cifer vč. tečky, zbytek krátká čísla/hex) — hluboko pod
  komentářem uváděných "~155 B", tedy 640 B pokryje bezpečně **7+** kompletních
  řádků, ne jen dokumentovaný 4× násobek. Okrajový případ (`n_lines==1` v
  okně, které nezačíná na offsetu 0 souboru) je za těchto podmínek prakticky
  nedosažitelný — nejde o neprozkoumané riziko, je to dokumentovaná a
  odůvodněná rezerva.
- **Nový `.bss` buffer ověřen přímo v `.elf`** (ne odhadem ze zdrojáku):
  `arm-none-eabi-nm --size-sort -S` → `24000da8 00000281 b buf.1` = 641 B
  (640+1), v D1 AXI SRAM, žádný konflikt s jinými sekcemi.
- **`g_d2d_slow_entries` v `.map`:** `.bss.g_d2d_slow_entries` @
  `0x2402c724`, prostý 32bit čítač, žádné DMA/cache náležitosti.
- **Threading model SD zrcadla:** `datalog_mirror_service()` i
  `datalog_mirror_set_enabled()` i `datalog_erase_all()` (UART `erase`)
  běží výhradně v UartTasku (potvrzeno grepem volajících) — žádný
  cross-task race nad `s_mirror_*` stavem; F-0148 je čistě sekvenční
  bug (chybějící `f_close`), ne race.
- **Zbylá tři místa `s_mirror_open = false`** (`:709`, `:747`) zkontrolována
  zvlášť — obě buď mají `f_close()` bezprostředně před sebou, nebo mají
  zdůvodněnou výjimku (`f_mount(NULL,...)` invaliduje handly samo).

## Nezkontrolováno / omezení tohoto běhu

- **F-0148 není ověřeno na HW** — reprodukce vyžaduje `datalog erase` nad
  živým, aktivním zrcadlem s produkčním obsahem logu (160 000+ záznamů);
  destruktivní akce, neprovedena bez výslovného souhlasu uživatele (stejná
  výhrada platila pro F-0143 samotné).
- Sekce checklistu A/B/C/F/G/H **záměrně vynechány** — rozsah je 2 commity,
  žádná změna hodin, boot pořadí, DMA umístění (mimo ověření nového
  bufferu), flash geometrie ani periferní inicializace.
- Neprošel jsem znovu CELÝ `datalog_sd.c` (2026-09-10 modul 7 ho už
  auditoval v plném rozsahu) — jen diff commitu `caa5f70` a jeho bezprostřední
  okolí (funkce, které mění nebo na které volají).
