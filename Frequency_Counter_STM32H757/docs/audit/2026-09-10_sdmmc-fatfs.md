# Audit: drivery SDMMC + FatFs  (2026-09-10)

- **Commit:** `ef89f45` (větev `audit/2026-09-09-hodiny-pwr`)
- **Jádro / doména:** CM7 / D1 (SDMMC1 je v D1, buffery v AXI SRAM)
- **HAL / middleware:** CMSIS device 1.10.7, FatFs **R0.12c** (`_FFCONF 68300`)
- **Projité sekce checklistu:** A (jen hodiny SDMMC), B (sdílené GPIO mezi jádry),
  C (DMA/cache/umístění bufferů), D (přerušení, souběh, RTOS), E (ošetření chyb),
  G (SDMMC, konkrétní pasti)
- **Neprojité (a proč):** F (Flash/option byty — modul se jich netýká),
  H (errata — SDMMC-specifické erratum pro rev V nebylo v tomto běhu dohledáno,
  viz „Nezkontrolováno“)

## Přečtené soubory (celé)

`CM7/Core/Src/sd_export.c` (1250), `CM7/Core/Src/datalog_sd.c` (403),
`CM7/Core/Src/sdmmc.c` (181), `CM7/Core/Inc/sd_export.h` (144),
`CM7/FATFS/App/fatfs.c` (107), `CM7/FATFS/Target/sd_diskio.c` (720),
`CM7/FATFS/Target/bsp_driver_sd.c` (307), `CM7/FATFS/Target/ffconf.h` (270).
Cíleně: `Middlewares/Third_Party/FatFs/src/{ff.c f_mount/find_volume, diskio.c,
option/syscall.c}`, `Drivers/.../stm32h7xx_hal_sd.c`, `.../stm32h7xx_hal_rcc_ex.c`,
`CM7/Core/Src/screenshot.c` (SD větev), `freertos_task_uart.c` (blok `sd`),
`H757_LED.ioc` (SDMMC1 + PE3), `CM7/Release/H757_LED_CM7.elf` (`nm` — umístění bufferů).

## Souhrn

Modul obsluhuje SD kartu jako **export médium** (CSV z datalogu, BMP screenshoty);
autoritativní úložiště zůstává W25Q. Cesta je záměrně postavená na **blokujícím
CPU/FIFO přenosu** místo IDMA (`BSP_SD_*Blocks_DMA` jsou přepsané), což odstraňuje
celou třídu problémů s cache a zarovnáním — a je to doložitelně správné rozhodnutí.
Init karty je poskládaný ručně, protože dvě místa ve vendor HAL mají smyčky
s timeoutem ~49 dní; obcházejí se ohraničenými variantami. Tahle část je promyšlená.

**Verdikt: podmíněně funkční.** Základní scénář (karta vložená při startu, export,
test) je ověřený na HW a drží. Slabiny jsou v **životním cyklu okolo mountu**:
výměna karty za běhu je rozbitá deterministicky (**F-0025**) a odmountování
z defaultTasku umí smazat FatFs semafor, který zrovna drží jiný task
(**F-0026**) — to je jediný nález v tomto modulu, který může poškodit paměť.
Zbytek jsou robustnostní a dokumentační nálezy.

---

### F-0025 [S2] Po vytažení a opětovném vložení karty se SD už nenamountuje

- **Místo:** `CM7/Core/Src/sd_export.c:106-111` (`sd_export_tick()`)
- **Popis:** Automatický unmount při vytažení karty volá holý `f_mount(NULL, "", 0)`,
  ale **neresetuje `disk.is_initialized[0]`**. Ruční `sd_export_unmount()` (`:176-182`)
  to dělá — obě cesty se tedy rozcházejí právě v tom kroku, kvůli kterému tam ten
  řádek je.
- **Důkaz:**
  - `sd_export.c:109` → `f_mount(NULL, "", 0);` a nic dalšího; oproti tomu
    `sd_export.c:181-182` → `extern Disk_drvTypeDef disk; disk.is_initialized[0] = 0;`
    s komentářem, proč je to nutné.
  - `Middlewares/Third_Party/FatFs/src/diskio.c` `disk_initialize()`:
    `if (disk.is_initialized[pdrv] == 0) { stat = …disk_initialize(…); if (stat == RES_OK) disk.is_initialized[pdrv] = 1; }`
    → při hodnotě 1 se **`SD_initialize()` (a tím `BSP_SD_Init()`) vůbec nezavolá**
    a vrátí se `RES_OK`.
  - `grep is_initialized` přes celý strom: jediný reset je `sd_export.c:182`.
- **Dopad:** Deterministický, ne náhodný. Posloupnost: úspěšný mount
  (`is_initialized[0] = 1`) → vytažení karty (tik zahodí svazek, příznak zůstane 1)
  → vložení karty → `sd_export_tick` požádá o mount → `f_mount(…, 1)` →
  `find_volume` → `disk_initialize` vrátí `RES_OK` **bez identifikace karty** →
  `disk_read` → `SD_CheckStatusWithTimeout(SD_TIMEOUT)`. Karta byla mezitím bez
  napájení, je ve stavu IDLE a na CMD13 se starým RCA neodpovídá, takže se
  **30 sekund** čeká a pak přijde `FR_DISK_ERR`. Stav se nastaví na `SD_EXP_ERROR`,
  který se drží do dalšího vytažení → **další pokusy dopadnou stejně**. Únik z toho
  je jen `sd unmount` (projde přes `sd_export_unmount`) nebo restart.
  Desku to neshodí — blok `sd` v UartTasku je obalený `sd_blocking_begin()`, takže
  po dobu čekání jde task pod UiTask a watchdog i dotyk běží dál.
- **Reprodukce:** `sd mount` (OK) → vytáhnout kartu → počkat ~2 s → vložit →
  sledovat konzoli: mount po ~30 s selže. Kontrolní větev: totéž, ale místo
  vytažení dát `sd unmount` → po vložení se namountuje normálně.
- **Návrh opravy:** V `sd_export_tick()` volat `sd_export_unmount()` místo holého
  `f_mount(NULL, "", 0)`. Je to levné (`f_mount(NULL,…)` na médium nesahá,
  `disk.is_initialized` je zápis do RAM) a odstraní to rozdíl mezi dvěma cestami
  úplně, místo aby se druhá kopie řádku udržovala ručně.
  ⚠️ `sd_export_unmount()` na konci volá `datalog_sd_card_present()` — v tiku
  je to volání navíc do debouncovaného čítače, viz **F-0030**.
- **Riziko opravy:** nízké. `sd_export_unmount()` je proti dnešnímu kódu navíc jen
  o reset příznaku a přepočet `s_state`, obojí je v defaultTasku levné.
- **Vztah k lekcím:** **`L-0012`** (oprava se neaplikovala na dvojče) — přesně ten
  vzor: správná cesta existuje, ale druhá kopie ji nepřevzala.
- **Stav:** opraveno 2026-09-11, ✅ **OVĚŘENO NA HW (fyzické vytažení karty za běhu → vložení → mount projde; dřív 30 s a trvalý ERROR)**. Opraveno **podle návrhu**: tik volá
  `sd_export_unmount()` místo holého `f_mount(NULL,…)`, takže druhá kopie zmizela
  úplně místo aby se udržovala. ⚠️ Upozornění z nálezu („přidá to do tiku další
  volání debouncovaného čítače") **padlo tím, že se F-0030 opravilo ve stejném
  commitu** — `datalog_sd_card_present()` už stav neposouvá, jen ho čte.
  ⚠️ `sd_export_unmount()` leží pod hlavičkou „VÝHRADNĚ z UartTasku", ale platí to
  o *ostatních* funkcích v té sekci: tahle na médium nesahá (`f_mount(NULL,…)`
  jen zahodí ukazatel, reset `is_initialized` je zápis do RAM), takže ji defaultTask
  volat smí. Je to u volání napsané.
  Ověřeno v obrazu: `sd_export_tick` nově skáče na `sd_export_unmount`.

---

### F-0026 [S2] Odmountování z defaultTasku smaže FatFs semafor, který drží jiný task

- **Místo:** `CM7/Core/Src/sd_export.c:106-111` (`sd_export_tick()`) ×
  `CM7/Core/Src/screenshot.c:110-158` (`screenshot_save_sd()`) a
  `CM7/Core/Src/sd_export.c:279-295` (`ui_refresh_capacity()`)
- **Popis:** Projekt má proti tomuhle obranu — příznak `s_busy`, který na dobu
  dlouhé operace vypne auto-unmount. Nasazený je ale jen na `sd_export_run()`
  a `sd_export_selftest()`. **Třetí dlouhý zapisovatel (`screenshot sd`, 1,15 MB)
  a `f_getfree()` v `ui_refresh_capacity()` ho nenastavují.**
- **Důkaz:**
  - Účel příznaku je popsaný v `sd_export.c:45-52`: „`_FS_REENTRANT=1` chrání
    operace nad svazkem mutexem, ale **deregistraci svazku ne**.“
  - `grep s_busy CM7/Core/Src/screenshot.c` → **prázdné**. Příkaz `screenshot sd`
    (`freertos_task_uart.c:1213-1215`) volá jen `sd_blocking_begin()`, což řeší
    prioritu, ne odmountování.
  - `ff.c` `f_mount()`: **nebere zámek svazku** — rovnou dělá
    `clear_lock(cfs)` a `if (!ff_del_syncobj(cfs->sobj)) return FR_INT_ERR;`.
  - `option/syscall.c`: `ff_del_syncobj()` → `osSemaphoreDelete(sobj)`;
    `ff_rel_grant()` → `osSemaphoreRelease(sobj)`.
  - `ffconf.h:242,246`: `_FS_REENTRANT 1`, `_SYNC_t osSemaphoreId_t`.
  - `sd_export.c:108`: podmínka je `if (s_mounted && !s_busy)` — během screenshotu
    je `s_mounted` true a `s_busy` false, takže se unmount **provede**.
- **Dopad:** Když uživatel vytáhne kartu během `screenshot sd` (sekundy) nebo během
  `f_getfree` (komentář na `:277-278` sám přiznává, že u FAT16 / neplatného FSINFO
  projde celou FAT), defaultTask smaže semafor, který UartTask právě **drží**.
  `osSemaphoreDelete` → `vQueueDelete` → `vPortFree`, takže následné
  `unlock_fs` → `osSemaphoreRelease` píše **do uvolněné haldy**. To není chybějící
  soubor, to je poškození haldy FreeRTOS — projeví se později a jinde
  (typicky HardFault v nesouvisejícím tasku), tedy nejhůř dohledatelná třída.
  ⚠️ Bez vytažení karty nenastane; není to vada, která by se projevila sama.
- **Reprodukce:** `HYPOTÉZA — ověřit na HW:` spustit `screenshot sd` a během zápisu
  kartu vytáhnout; sledovat `status` (heap free/min) a crash black-box po restartu.
  Staticky je řetězec doložený výše kompletní.
- **Návrh opravy:** Minimální varianta: dát `screenshot_save_sd()` a
  `ui_refresh_capacity()` tentýž wrapper, jaký mají `sd_export_run`/`_selftest`
  (`s_busy = true; … s_busy = false;` kolem těla, s vyčleněným tělem, aby se
  příznak nedal zapomenout na chybové cestě). Protože `s_busy` je privátní pro
  `sd_export.c`, znamená to vystavit dvojici `sd_export_busy_begin/end()` v
  `sd_export.h` vedle `sd_blocking_begin/end()` — a v hlavičce napsat, že se
  **obojí** volá společně (jedno chrání prioritu, druhé svazek).
  Systémovější varianta (větší): auto-unmount v tiku nedělat vůbec a jen nastavit
  požadavek pro UartTask, který je jediným vlastníkem FatFs operací. Tím by
  deregistrace svazku a práce nad svazkem byly v jednom tasku a příznak by nebyl
  potřeba. Je to čistší, ale mění vlastnictví, takže to není „minimální“.
- **Riziko opravy:** nízké u první varianty (přidání příznaku nic neodebírá),
  střední u druhé (mění se, kdo odmountovává — dotkne se hot-removal chování).
- **Vztah k lekcím:** `L-0012` (guard nasazený jen na část symetrických cest);
  po opravě **nová lekce** — „obrana, která je opt-in, musí být vyjmenovaná
  u všech volajících, jinak ji třetí volající nedostane“.
- **Stav:** opraveno 2026-09-11, ⬜ **neověřeno na HW** — **minimální variantou**, ne systémovou.
  Vzniklo `sd_export_busy_begin/end()` v `sd_export.h` a používá ho
  `screenshot_save_sd()` i `ui_refresh_capacity()`. **Obojí jako OBALKA nad
  vyčleněným tělem** (`begin(); r = body(); end();`), protože `screenshot_save_sd`
  má **osm** chybových návratů a na žádném se příznak nesmí ztratit — tentýž vzor,
  jaký už měly `sd_export_run()`/`_selftest()`.
  **Proč ne systémová varianta** (přesunout auto-unmount do UartTasku): nález sám
  ji označuje za „není minimální" a za střední riziko, protože mění vlastnictví
  a dotkne se chování při hot-removal. Zařízení funguje; §F5.0 v takovém případě
  volí menší zásah. Zůstává jako možnost, kdyby se ukázalo, že opt-in příznak
  nestačí.
  🔑 V hlavičce je nově napsané, že `sd_export_busy_*` a `sd_blocking_*` se volají
  **obojí** — každé řeší něco jiného (priorita × deregistrace svazku). Právě to,
  že to nikde nestálo, způsobilo, že třetí volající dostal jen polovinu obrany.
  Nová lekce **`L-0022`**. Ověřeno v obrazu: `screenshot_save_sd` obepíná tělo
  dvojicí `sd_export_busy_begin`/`_end`.

---

### F-0027 [S3] SD konfiguruje GPIOC za běhu bez `gpio_cfg_lock()`, ačkoli je to sdílený port

- **Místo:** `CM7/Core/Src/sdmmc.c:127-133` (`HAL_SD_MspInit`) a
  `CM7/Core/Src/sd_export.c:429-439` (`sd_dat_pullup_enable()`)
- **Popis:** Obě funkce volají `HAL_GPIO_Init(GPIOC, …)` **za běhu** (spouští je
  `BSP_SD_Init()` při mountu, ne boot), a ani jedna nebere `gpio_cfg_lock()`.
  Pravidlo projektu přitom GPIOC jmenovitě uvádí.
- **Důkaz:**
  - `CM7/Core/Inc/gpio_guard.h:19-20`: „Použij ho **všude**, kde se `HAL_GPIO_Init`
    volá **ZA BĚHU** na portu, kam sahá i druhé jádro (**GPIOA/B/C/G**). Viz #208.“
  - GPIOC je opravdu sdílený: `CM4/Core/Src/eth.c:129` →
    `HAL_GPIO_Init(GPIOC, …)` pro **PC1 (MDC), PC4 (RXD0), PC5 (RXD1)**.
  - Tabulka hlídače `GG_PINS` ve `freertos.c` ty tři piny obsahuje
    (`{GPIOC, 1u, 11u, "MDC"}`, `{GPIOC, 4u, …}`, `{GPIOC, 5u, …}`) — tedy projekt
    už dřív změřil, že se konfigurace na GPIOC ztrácí.
  - `grep gpio_cfg_lock` → používají ho `encoder.c` (PA8/PA9/PC13) a
    `fpga_freq.c` (PB12). **Ani jeden ze dvou SD zápisů do GPIOC v seznamu není.**
  - `HAL_GPIO_Init` dělá nad `MODER`/`AFR`/`PUPDR` neatomický read-modify-write
    (týž mechanismus, který popsal #219/#208).
- **Dopad:** Souběh mountu SD s inicializací ETH na CM4 může tiše přepsat jednu
  položku konfigurace. Ve směru **SD → ETH** to hlídač do 1 s opraví a započítá
  (`status` → `GPIO HLIDAC`). Ve směru **ETH → SD** ale **PC8–PC12 v `GG_PINS`
  nejsou**, takže ztracená AF na datové nebo hodinové lince SD se neopraví a
  projeví se jako „karta přestala jet“ bez jakéhokoli záznamu.
  Okno je úzké: CM4 konfiguruje ETH jednorázově ~1,3 s po resetu, takže reálně
  hrozí hlavně **studený start s vloženou kartou** (auto-mount z defaultTasku).
- **Reprodukce:** `HYPOTÉZA — ověřit na HW:` opakovaný studený start s vloženou
  kartou, po náběhu číst `status` → `GPIO HLIDAC` (nenulové počítadlo u MDC/RXD0/RXD1
  = závod proběhl) a `sd diag` (řádek `sbernice`). Staticky je nález doložený výše.
- **Návrh opravy:** Dvě místa, obě regen-safe:
  1. `sd_dat_pullup_enable()` — obalit `gpio_cfg_lock()/unlock()` (vlastní soubor).
  2. `HAL_SD_MspInit` — `gpio_cfg_lock()` do `USER CODE BEGIN SDMMC1_MspInit 0`
     a `gpio_cfg_unlock()` do `USER CODE BEGIN SDMMC1_MspInit 1`; oba bloky
     existují a obepínají právě generovaný `HAL_GPIO_Init`.
  Samostatně zvážit doplnění PC8–PC12 do `GG_PINS`, aby byl i opačný směr vidět.
- **Riziko opravy:** nízké. `gpio_cfg_lock()` je podle hlavičky navržený tak, že
  při nezískání zámku pokračuje (raději závod než deadlock při bootu).
  ⚠️ Doplnění `GG_PINS` je samostatná změna — hlídač by pak SD piny **opravoval**,
  a to se nesmí stát v době, kdy je karta odmountovaná a piny mají být jinak.
- **Vztah k lekcím:** `L-0012`; pravidlo samo je v `gpio_guard.h`, chybí jen
  jeho uplatnění.
- **Stav:** opraveno 2026-09-11, ⬜ **neověřeno na HW**. Obě místa obalena `gpio_cfg_lock()/unlock()`:
  `sd_dat_pullup_enable()` (vlastní soubor) a `HAL_SD_MspInit` přes **USER CODE**
  bloky `SDMMC1_MspInit 0` / `1`, takže zámek drží přes **oba** generované
  `HAL_GPIO_Init` (GPIOC i GPIOD) a regenerace z CubeMX to nesmaže.
  ⚠️ **Doplnění PC8–PC12 do `GG_PINS` ZÁMĚRNĚ NEPROBĚHLO** — nález to sám označuje
  za samostatnou změnu s pastí: hlídač by pak SD piny **opravoval**, a to se nesmí
  stát v době, kdy je karta odmountovaná a piny mají být jinak. Zůstává otevřené
  jako vědomé rozhodnutí, ne opomenutí.
  Ověřeno v obrazu: `gpio_cfg_lock` je nově volán z `HAL_SD_MspInit`
  a `sd_dat_pullup_enable` (vedle dosavadních `encoder_init` a `fpga_freq_init`).

---

### F-0028 [S3] SDMMC_CK = 32 MHz, ale karta nikdy nepřejde do High Speed (spec limit 25 MHz)

- **Místo:** `CM7/Core/Src/sd_export.c:450-460` (`sd_apply_init_config()`),
  `CM7/Core/Src/sdmmc.c:66-72`, `H757_LED.ioc:935`
- **Popis:** Hodinový dělič je `ClockDiv = 1`, tedy **SDMMC_CK = 32 MHz**.
  Komentář to obhajuje limitem 50 MHz — jenže **50 MHz platí až pro High Speed
  režim**, do kterého se karta nikdy nepřepne. V Default Speed je strop **25 MHz**.
- **Důkaz:**
  - `H757_LED.ioc:907` `RCC.SDMMCFreq_Value=64000000`, `:935` `SDMMC1.ClockDiv=1`
    → `SDMMC_CK = 64 / (2 × 1) = 32 MHz`. Totéž nastavuje runtime
    `sd_apply_init_config()` (`sd_export.c:458`).
  - `Drivers/…/stm32h7xx_hal_sd.c:273-274`:
    `SD_NORMAL_SPEED_FREQ 25000000U`, `SD_HIGH_SPEED_FREQ 50000000U`.
  - Přepnutí do High Speed dělá `HAL_SD_ConfigSpeedBusOperation()`
    (deklarace `stm32h7xx_hal_sd.h:685`). `grep -rniE
    "ConfigSpeedBusOperation|SWITCH_FUNC|CmdSwitch|HIGH_SPEED|SDMMC_SPEED_MODE"`
    přes `CM7/Core/Src`, `CM7/FATFS`, `CM7/app` → **žádný výskyt**. `BSP_SD_Init()`
    ji záměrně nevolá (`sd_export.c:494-507` vysvětluje, proč se vynechává celý
    `HAL_SD_Init`), takže karta zůstává v Default Speed.
  - Komentáře, které mez uvádějí: `sd_export.c:457` („0 NEPOUZIVAT — bypass delicky
    by dal 64 MHz, **nad SD HS limitem 50 MHz**“) a `sdmmc.c:64-65` — obojí cituje
    limit režimu, ve kterém přístroj neběží.
  - ⚠️ Vlastní pojistka HAL to nechytne: `stm32h7xx_hal_sd.c:2467` porovnává
    `hsd->Init.ClockDiv >= (sdmmc_clk / (2U × SD_NORMAL_SPEED_FREQ))`, což je
    celočíselně `64e6 / 50e6 = 1`, takže `ClockDiv = 1` projde — přestože
    32 MHz > 25 MHz.
- **Dopad:** Sběrnice jede ~28 % nad specifikací Default Speed. Dnes to na této
  kartě měřitelně funguje (`sd test` prošel po HW úpravě: odstraněn R60 = pull-up
  na CK, bulk kondenzátor na SD VDD 10 µF), ale je to **rezerva vybraná do nuly**:
  jiná karta, delší vodič nebo vyšší teplota se projeví jako `DATA_CRC_FAIL` nebo
  přerušovaně poškozený export — a hledalo by se to v datové cestě, ne v taktu.
  Nejde o nefunkčnost, jde o robustnost.
- **Reprodukce:** `sd diag` → řádek `sbernice` vypíše skutečný takt z `CLKCR`.
  Ověření meze: `sd test` (verify 8 kB + 1 MB rychlostní test) při `ClockDiv=1`
  a `ClockDiv=2` a porovnat chybovost a KB/s.
- **Návrh opravy:** Rozhodnutí patří uživateli, ale mechanicky jsou tři cesty:
  1. **`ClockDiv = 2`** (16 MHz) — v mezích Default Speed, ověřená hodnota z HW
     průchodu (`CLKCR = 0x4002`). Cena: poloviční propustnost.
  2. Ponechat 32 MHz a **zapsat u konstanty pravdu** — že je to vědomě nad
     specifikací Default Speed a čím to bylo změřeno. Nejlevnější varianta,
     nemění chování.
  3. Doplnit přepnutí do High Speed (CMD6). Tím se 32 MHz dostane do specifikace,
     ale znamená to sáhnout do ručně skládaného initu, kde se vendor cestám
     vyhýbáme kvůli neohraničeným smyčkám — tedy nejdražší a nejrizikovější.
- **Riziko opravy:** varianta 2 nulové, varianta 1 nízké (jen jiná hodnota, ale
  mění propustnost exportu), varianta 3 střední až vysoké.
- **Vztah k lekcím:** **`L-0006`** — konstanta odvozená z hodin, u níž komentář
  uvádí mez z jiného provozního režimu, než v jakém zařízení běží. Po opravě
  **nová lekce `L-0024`**.
- **Stav:** **ČÁSTEČNĚ opraveno 2026-09-11** — a je to vědomé rozhodnutí, ne nedodělek.
  🔴 **Konečná podoba (2026-09-11, po HW testu a dvou zpřesněních od uživatele):
  takt je `SD_CLKDIV = 1` → 32 MHz, tedy PŘESNĚ hodnota z `.ioc`
  (`SDMMC1.ClockDiv=1`), a přepínání do High Speed je ODSTRANĚNÉ.**
  **Proč takhle** (uživatel, 2026-09-11): *„karta dříve běžela spolehlivě na 32 MHz"* —
  a HW test toho dne ukázal, že **CMD6 na této kartě stejně neprojde**, takže celý
  HS aparát by za cenu vendor volání s ~49denními smyčkami (`SD_SwitchSpeed`)
  nepřinesl nic. Odstraněním vypadl z obrazu i ten vendor kód (ověřeno: `nm` už
  `HAL_SD_ConfigSpeedBusOperation` ani `SD_SwitchSpeed` nenajde, `.text` −592 B),
  takže **zbytkové riziko zatuhnutí popsané výše je pryč úplně**.
  **Co z nálezu opravené ZŮSTÁVÁ — a je to jeho jádro:**
  - komentář u konstanty už **necituje limit režimu, ve kterém přístroj neběží**.
    Nově říká pravdu: je to Default Speed, limit 25 MHz, a 32 MHz je nad ním vědomě,
    včetně důvodů a ceny;
  - 🔑 **stav přestal být tichý:** `sd diag` hlásí takt, režim, platný limit **a
    značku `<-- NAD LIMITEM`**. Přesně kvůli téhle neviditelnosti nález vznikl.
  **Co zůstává vědomě NEOPRAVENÉ:** sběrnice jede **~28 % nad limitem Default Speed**.
  Opora pro to rozhodnutí je empirická (dlouhodobě spolehlivý provoz po HW úpravě:
  odstraněn R60 = pull-up na CK, bulk kondenzátor na SD VDD 10 µF), ne odvozená ze
  specifikace — a je to tak zapsané, aby se to příště nepletlo.
  ⚠️ Cena: rezerva vybraná do nuly. Jiná karta, delší vodič nebo vyšší teplota se
  může projevit jako `DATA_CRC_FAIL` nebo přerušovaně poškozený export. **Až se to
  stane, začni řádkem `sbernice` v `sd diag`** — ta značka je tam přesně proto.
  ✅ **Ověřeno na HW 2026-09-11** (po flashi, `Reset: power-on`):
  `sbernice: 4-bit, SDMMC_CK 32.000 MHz, Default Speed (limit 25 MHz)  <-- NAD LIMITEM`,
  a hlavně **datová cesta na 32 MHz prokazatelně jede**: `sd test` = *„8 KB zapsano a
  precteno zpet bit po bitu shodne"*, **zápis 6,17 MB/s, čtení 9,25 MB/s** (1 MB soubor).
  🔑 Tím je opora pro rozhodnutí jet nad limitem DS **změřená, ne jen tvrzená** — a to je
  přesně ten rozdíl, který u „vědomého porušení limitu" dělá rozhodnutí z přehlédnutí.

  *(Původní provedení, platné jen mezi commity `79f6ea7` a dneškem: uživatel zvolil
  **variantu 3**
  (doplnit High Speed) — nález ji označuje za nejdražší a nejrizikovější, a po přečtení
  HAL to platilo ještě víc, než nález tušil. **Implementováno proto s pojistkou, která
  variantu 3 dělá bezpečnější než variantu 2.**
  🔴 **Co se při opravě zjistilo (a nález to nevěděl):**
  `HAL_SD_ConfigSpeedBusOperation` → `SD_SwitchSpeed` obsahuje **dvě** smyčky
  s `SDMMC_SWDATATIMEOUT` = `0xFFFFFFFF` ms (~49 dní) — tedy **tutéž konstrukci**,
  kvůli které se v tomto projektu obchází `HAL_SD_Init` i
  `HAL_SD_ConfigWideBusOperation`. Doklad je přímo v git historii: commit `ec64939`
  *„HAL_SD_Init umi tocit ~49 dni v tesne smycce -> obejit + srazit prioritu (#28)"*
  a komentář v `BSP_SD_Init`: *„Když karta to ACMD13 neodbaví, `sd fs` zamrzne a IWDG
  shodí desku. **Přesně to se stalo.**"*
  ⚠️ Druhá věc: HAL si High Speed **neověřuje dotazem na kartu** — ve větvi
  `SDMMC_SPEED_MODE_HIGH` mu stačí `CardType == CARD_SDHC_SDXC`, protože `CardSpeed`
  tenhle projekt **nezjišťuje** (`HAL_SD_GetCardStatus` se záměrně přeskakuje, viz
  tentýž komentář). Předpokládá tedy „SDHC ⇒ umí HS".
  **Jak je to ošetřené:**
  1. **Pojistka na takt** — `SD_CLKDIV_DS` (2 = 16 MHz) je výchozí; `SD_CLKDIV_HS`
     (1 = 32 MHz) se nastaví **až po** návratu `HAL_OK`. Ověřeno v disassembly:
     `cmp r0,#0 / bne` přeskočí zápis `ClockDiv=1` i `SDMMC_Init`, takže při
     jakémkoli selhání zůstává 16 MHz. **Přístroj tedy není mimo specifikaci
     v žádném výsledku** — což je víc, než uměla varianta 2.
  2. **Ohraničené čekání na TRANSFER před** vendor voláním (`sd_wait_transfer(1000)`,
     sdílené s `BSP_SD_Init`, aby nevznikla druhá kopie té smyčky). Druhá vendor
     smyčka je **těsná příkazová bez yieldu**, takže tohle ji v normálním případě
     ukončí na první iteraci.
  3. Datovou fázi CMD6 ohraničuje **hardware** (DTIMER), tedy minuty, ne 49 dní.
  4. Volající běží pod `sd_blocking_begin()` → `osPriorityLow`, tedy **pod UiTaskem**:
     i při zaseknutí běží heartbeat a IWDG desku neshodí. Nejhorší následek je
     zatuhlá konzole, ne restart — na rozdíl od incidentu z 2026-08-13.
  ⚠️ **Zbytkové riziko zůstává** a nejde ho zvenčí ohraničit: karta, která na CMD6
  odpoví a **pak** se nedostane do TRANSFER, nechá vendor smyčku točit.
  `sd diag` nově hlásí režim i platný limit (`… , High Speed (limit 50 MHz)`).
  Tahle analýza vendor cesty platí beze změny i po návratu na 32 MHz — mění se
  jen to, jakou hodnotu takt dostane po neúspěchu.)*

---

### F-0029 [S3] `export_body()` nekontroluje `f_close()`, ačkoli tentýž soubor vysvětluje, proč se to musí

- **Místo:** `CM7/Core/Src/sd_export.c:1232` (`f_close`), `:1210` (hlavičkový `f_write`)
- **Popis:** Export CSV zahodí návratovou hodnotu `f_close()` a nekontroluje ani
  zápis hlavičky. O 130 řádků výš je u téhož volání komentář, proč se kontrolovat má.
- **Důkaz:**
  - `sd_export.c:1232` → `f_close(&f);` bez přiřazení; funkce hned vrací `written`.
  - `sd_export.c:1210` → `f_write(&f, line, (UINT)n, &bw);` bez kontroly, přestože
    **všechny** zápisy řádků o pár řádků níž (`:1224`) kontrolované jsou.
  - Protipól ve stejném souboru — `selftest_body()`, `sd_export.c:1102-1111`:
    „⚠️ Návratovou hodnotu `f_close` **KONTROLOVAT**. Právě tady se zapisuje
    adresářová položka — když to selže, `f_write` už hlásilo OK a chyba se projeví
    až o kus dál jako zavádějící `FR_NO_FILE`.“
  - `screenshot.c:151-154` má tutéž kontrolu (`f_sync` i `f_close` vyhodnocené).
    Ze tří míst, která zavírají soubor na kartě, je nekontrolované **jen jedno**.
- **Dopad:** `sd export` vypíše „exportovano N zaznamu“ i tehdy, když se
  adresářová položka nezapsala — na PC pak soubor chybí nebo má nulovou délku.
  Uživatel má potvrzení, že data má, a přitom je nemá; to je horší než čisté
  selhání. Typický spouštěč: karta vytažená na konci exportu nebo zaplněná karta.
- **Reprodukce:** Spustit `sd export` s velkým logem a vytáhnout kartu v poslední
  vteřině; hlášení bude „export OK, N zaznamu“.
- **Návrh opravy:** `if (f_close(&f) != FR_OK) { s_state = SD_EXP_ERROR; return -1; }`
  a hlavičkový `f_write` kontrolovat stejně jako řádky.
  ⚠️ Návratová hodnota `-1` už znamená „export selhal“, takže volající
  (`sd_export_service`, UART blok, UI) se nemění.
- **Riziko opravy:** nízké — mění se jen chování při už existující chybě.
- **Vztah k lekcím:** **`L-0003`** (ignorovaná návratová hodnota) + `L-0012`
  (pravidlo aplikované na jednu ze tří kopií).
- **Stav:** opraveno 2026-09-11, ✅ **OVĚŘENO NA HW** (`sd export 2000` → `export OK, 2000 zaznamu` — kontroly nedělají falešné selhání). Opraveno **podle návrhu a ještě o kus dál**:
  kontroluje se `f_close()` **i hlavičkový `f_write()`** (nález ho zmiňoval jako
  druhou polovinu). Obojí nastaví `s_state = SD_EXP_ERROR` a vrátí `-1`, což už
  znamená „export selhal" — volající se tedy nemění.

---

### F-0030 [S3] Debouncer detekce karty má sdílený stav a volají ho tři tasky

- **Místo:** `CM7/Core/Src/datalog_sd.c:148-156` (`datalog_sd_card_present()`)
- **Popis:** Funkce drží `static uint8_t stable, cnt;` a mění je při každém volání.
  Volají ji **tři různé tasky** různou kadencí, bez jakékoli ochrany.
- **Důkaz:**
  - Stav: `datalog_sd.c:150` `static uint8_t stable, cnt;`, zápis na `:153-154`.
  - defaultTask: `freertos.c:708` → `sd_export_tick()` → `sd_export.c:104`.
  - UiTask: `app_gpsdo.c:6213` a `:8547` → `sd_export_ui_info()` → `sd_export.c:270`.
  - UartTask: `freertos_task_uart.c:1285` (`sd det`), plus `sd_export_mount()`
    (`sd_export.c:159`), `sd_export_unmount()` (`:183`), `export_body()` (`:1227`),
    `sd_export_format()` (`:305`).
  - Komentář na `datalog_sd.c:147` říká: „Časovou konstantu určuje **kadence
    volajícího**“ — což platilo, dokud byl volající jeden.
- **Dopad:** Dvojí. (a) Časová konstanta debounce je nedefinovaná — tři nezávislé
  kadence se sčítají, takže `SD_DET_STABLE_N = 3` neodpovídá žádnému skutečnému
  času. (b) Souběh nad `cnt`/`stable` je neatomický read-modify-write; roztržení
  mezi dvěma tasky může počítadlo posunout nebo stav překlopit dřív, než by
  odpovídalo skutečnosti. Praktický následek je nanejvýš o půl sekundy dřívější
  nebo pozdější reakce na vložení karty — proto S3, ne S2.
  ⚠️ Poznámka pro F-0025: navržená oprava tam přidá do tiku **další** volání
  téhle funkce, takže se to má opravit společně.
- **Reprodukce:** Staticky doložitelné z uvedených volajících.
- **Návrh opravy:** Oddělit dotaz od aktualizace: nechat `datalog_sd_card_present()`
  jen **číst** `stable` a přidat `datalog_sd_det_tick()`, který stav aktualizuje a
  volá se **výhradně z defaultTasku** (tam, kde už tik je). Tím zmizí souběh
  i nedefinovaná časová konstanta a kadence bude jedna a známá (2 Hz → překlopení
  do 1,5 s).
- **Riziko opravy:** nízké, ale **pozor na pořadí při bootu**: dnes první volání
  s vloženou kartou vrátí `false` a `true` až po třech voláních; po změně bude
  stejné chování vázané na tik. Ověřit, že auto-mount po startu pořád nastane.
- **Vztah k lekcím:** nová lekce po opravě — „stav v `static` uvnitř dotazovací
  funkce se stane sdíleným, jakmile přibude druhý volající“ → **`L-0023`**.
- **Stav:** opraveno 2026-09-11, ⬜ **neověřeno na HW**. Opraveno **podle návrhu**: dotaz a aktualizace
  oddělené — `datalog_sd_card_present()` už jen **čte** `s_det_stable` (smí se tedy
  ptát kdokoli odkudkoli) a stav posouvá nový `datalog_sd_det_tick()`, volaný
  **jediným místem**: `sd_export_tick()` v defaultTasku. Tím zmizel souběh i
  nedefinovaná časová konstanta.
  ⚠️ **Pořadí při bootu prověřeno**, jak nález žádal: `datalog_sd_det_tick()` je
  **první** řádek tiku, hned před dotazem, takže se překlopení do „karta je tam"
  stane po `SD_DET_STABLE_N` (3) tikách stejně jako dřív a auto-mount po startu
  nastane. Jediná změna je, že ostatní úlohy už čítač neposouvají — tedy méně
  překlopení, ne víc.
  ⚠️ `s_det_force` (override `sd force on`) nastaví `s_det_stable` rovnou, aby se
  na override nečekaly tři tiky.

---

### F-0031 [S4] Komentáře a hlášky popisují stav, který už neplatí

- **Místo:** `CM7/Core/Src/sd_export.c:443-449`, `:556-563`, `:756`;
  `CM7/Core/Src/freertos_task_uart.c:1251`; `CLAUDE.md` (sekce SD karta)
- **Popis:** Čtyři místa tvrdí něco, co se od jejich napsání změnilo. Žádné z nich
  nemění chování, ale všechna posílají příště špatným směrem.
- **Důkaz:**
  1. `sd_export.c:443-444`: „Nejdůležitější je `HardwareFlowControl = ENABLE` —
     naše `.ioc` ho **NEMÁ** (`SDMMC1.IPParameters=ClockDiv` bez HWFC)“.
     Skutečnost: `H757_LED.ioc:936-937` →
     `SDMMC1.HardwareFlowControl=SDMMC_HARDWARE_FLOW_CONTROL_ENABLE`,
     `SDMMC1.IPParameters=ClockDiv,HardwareFlowControl`. **Doplněno je.**
     Totéž nese `CLAUDE.md` („⚠️ Doplnit HWFC i do `.ioc` přes CubeMX“) —
     ten úkol je hotový.
  2. `sd_export.c:563`: „Tím se zapne HWFC, **transfer takt 16 MHz** i WIDBUS=1B.“
     Skutečnost: `ClockDiv = 1` → 32 MHz (viz F-0028).
  3. `sd_export.c:756`: `printf("  [a2] Init: HWFC=ENABLE, **ClockDiv=2**, 1-bit …")`
     — vytiskne 2, zatímco `sd_apply_init_config()` o řádek výš nastavila 1.
     Diagnostický výpis, který lže o hodnotě, kterou právě nastavil.
  4. `freertos_task_uart.c:1251`: „exportuji … do **GPSDO.CSV**“. Skutečnost:
     `export_next_name()` (`sd_export.c:1173-1181`) tvoří `GPSDOnnn.CSV`;
     pevné jméno bylo opuštěné 2026-08-13, protože přepisovalo předchozí export.
  5. `CLAUDE.md` sekce SD: „`ClockDiv=2` → **SDMMC_CK 16 MHz**“ a
     „✅ HW ověřen (… `CLKCR=0x4002` 4-bit 16 MHz)“ — popisuje předchozí nastavení.
- **Dopad:** Žádný na běh. Riziko je metodické: bod 1 by příště poslal někoho
  „opravovat“ `.ioc`, který je v pořádku, a bod 3 znehodnocuje výpis, jehož jediným
  smyslem je říct, co se doopravdy nastavilo.
- **Reprodukce:** Porovnání uvedených řádků, viz důkaz.
- **Návrh opravy:** Samostatný `docs:` commit; u bodů 2, 3 a 5 odvodit hodnotu
  z `ClockDiv` místo jejího opisování (u výpisu `[a2]` vytisknout
  `hsd1.Init.ClockDiv` a dopočtený takt, ať se to nemůže rozejít znovu).
- **Riziko opravy:** nulové u komentářů; u výpisu `[a2]` nízké (mění se jen text).
- **Vztah k lekcím:** `L-0006` (duplikovaná hodnota odvozená z hodin se rozejde),
  `L-0014` (druhá kopie údaje se má odvozovat, ne udržovat ručně).
- **Stav:** opraveno 2026-09-11 (`docs:` + dva odvozené výpisy), ⬜ neověřeno na HW.
  Všech pět bodů:
  1. Komentář o chybějícím HWFC v `.ioc` → opraveno, `.ioc` ho **má**; u kódu je
     napsané, proč ho init přesto nastavuje (skládá se ručně a nesmí záviset na tom,
     co zrovna vygeneroval CubeMX).
  2. „transfer takt 16 MHz" → nahrazeno odkazem na `SD_CLKDIV_DS` + větou
     **„hodnotu sem NEOPISUJ"**.
  3. `[a2]` výpis **tiskne `hsd1.Init.ClockDiv`** a dopočtený takt místo literálu —
     byl to diagnostický výpis, který lhal o hodnotě, kterou právě nastavil.
  4. „do GPSDO.CSV" → `GPSDOnnn.CSV` + poznámka, že skutečné jméno vypíše
     `export_body()`.
  5. `CLAUDE.md` přepsán na nové chování (DS 16 MHz → HS 32 MHz po CMD6).
  🔑 U bodů 2, 3 a 5 se hodnota **odvozuje**, ne opisuje — přesně jak nález žádal;
  jinak by se rozešly potřetí.

---

## Co bylo zkontrolováno a je v pořádku

**Umístění bufferů (sekce C, ověřeno `nm` nad `.elf`, ne ze zdrojáku):**
všechny SD buffery leží v AXI SRAM (`RAM_D1`, `0x2400xxxx`), tedy mimo DTCM, a ty,
u kterých na tom záleží, jsou zarovnané na 32 B:
`s_bounce` `0x24001580` (512 B), `s_fsbuf` `0x24017a00` (512 B),
`s_probe.5` `0x24017600` (512 B), `s_fs` `0x24017c58` (564 B),
`sbuf.2` `0x2400f1cc` (32 kB), `s_row` `0x2400e40c` (2400 B), `hsd1` `0x24017e90`.
`SDFatFS`/`SDFile` z generovaného `fatfs.c` v obrazu **nejsou** — `--gc-sections`
je zahodil, protože `sd_export.c` má vlastní `s_fs`. **L-0001 splněno.**

**Cache a DMA (sekce C):** rozhodnutí nepoužívat IDMA je doložené a konzistentní.
`BSP_SD_ReadBlocks_DMA`/`WriteBlocks_DMA` jsou přepsané na blokující
`HAL_SD_ReadBlocks`/`WriteBlocks`, které na H7 tahají data CPU přes FIFO, takže
cache maintenance je vypnutá **správně** (`ENABLE_SD_DMA_CACHE_MAINTENANCE 0`,
`ENABLE_SCRATCH_BUFFER` nedefinováno) a v `sd_export_fs()` je u čtení LBA 0
výslovně vysvětleno, proč tam invalidace **nesmí** být. **L-0002 splněno**
(dokumentovaným řešením, ne mlčky).

**Čekací smyčky (sekce E, L-0004):** projito všech osm. Ohraničené jsou
`BSP_SD_Init` (1 s na TRANSFER, `sd_export.c:545-554`), `sd_xfer_fail` (200 ms),
`sd_wait_ready` (parametr), `sd_probe_read_dpsm` (SW timeout nad HW DTIMER),
`SD_CheckStatusWithTimeout` a obě smyčky v `SD_read`/`SD_write` (30 s).
Dvě vendor smyčky s timeoutem ~49 dní (`SD_SendSDStatus`, závěrečné čekání
v `HAL_SD_Init`) jsou **obejité tím, že se ty funkce nevolají** — poskládaný init
je popsaný na `sd_export.c:494-507`. Tísně 30 s v `sd_diskio.c` neubližují, protože
`BSP_SD_GetCardState()` je přepsaná na polite polling s `osDelay(1)`.

**Priority a hladovění (sekce D):** `sd_blocking_begin/end()` sráží volající task
pod UiTask po dobu SD práce; obaluje **celý** blok `sd` v UartTasku
(`freertos_task_uart.c:1242…1307`, jeden vstup / jeden výstup) i `screenshot sd`
(`:1213-1215`). `SDMMC1_IRQHandler` existuje v `USER CODE` (`stm32h7xx_it.c:311`)
a NVIC prioritu 5 nastavuje `BSP_SD_Init()` — tedy `configLIBRARY_MAX_SYSCALL_
INTERRUPT_PRIORITY`, což je pro obsluhu volající `osMessageQueuePut` správně.

**Boot bez karty:** `MX_SDMMC1_SD_Init()` má early-return v `USER CODE`
(`sdmmc.c:35-72`) **a vyplňuje handle** — bez vyplnění by `HAL_SD_MspInit()`
nezapnul hodiny SDMMC1 (test `if (sdHandle->Instance == SDMMC1)`). Ověřeno, že
`hsd1.Init` v `sdmmc.c` (HWFC ENABLE, ClockDiv 1) souhlasí s `.ioc` i s runtime
`sd_apply_init_config()` — tři kopie, ale dnes shodné.

**Falešná stopa, kterou jsem prověřil a zavrhl:** `f_mount` s `opt=1` volá
`disk_status()` **jen** když je svazek už namountovaný (`ff.c` `find_volume`,
`if (fs->fs_type)`), jinak jde rovnou na `disk_initialize()`. Čtení registrů
SDMMC1 před zapnutím hodin (dokumentovaný BusFault, `sd_export.c:835-840`) tedy
touto cestou **nehrozí**. Stejně tak `disk_initialize()` v tomto vydání FatFs
nastavuje `is_initialized` až **po úspěchu** (`diskio.c`), takže neúspěšný mount
příznak nezablokuje — problém je jen v cestě popsané v F-0025.

**Druhá falešná stopa:** `HAL_SD_MspInit` volá za běhu
`HAL_RCCEx_PeriphCLKConfig(RCC_PERIPHCLK_SDMMC)`, což vypadá jako porušení
**L-0007** (rekonfigurace PLL za běhu). Není: pro `RCC_SDMMCCLKSOURCE_PLL` dělá
HAL jen `__HAL_RCC_PLLCLKOUT_ENABLE(RCC_PLL1_DIVQ)` a přepnutí multiplexeru
(`stm32h7xx_hal_rcc_ex.c`), **žádné `__HAL_RCC_PLLx_DISABLE()`**. Návratová
hodnota je na této větvi deterministicky `HAL_OK`, takže ani `Error_Handler()`
o dva řádky níž není dosažitelný.

**`get_fattime()`** (`fatfs.c:45-103`): nesahá na `HAL_RTC` (RTC registry vlastní
defaultTask), čte `g_rtc_text_local` a proti roztržení řetězce používá dvojí čtení
se shodou. Čte 20 B z pole deklarovaného jako 24 B — záměrně méně, než je
deklarováno, protože `extern` je ve čtyřech souborech a nesoulad velikosti by
linker neodhalil. Bez GPS syncu vrací 2020-01-01, ne 1980.

**Datalog na SD** (`datalog_sd.c`) je celý za `DATALOG_SD_RAW_OK = 0`, takže
`sd_probe()` vrací `false` a datalog jede na W25Q. RAW zápis by šel od LBA 0 = MBR
karty; obrana je na místě a dobře zdůvodněná. RMW vrstva je krytá
`datalog_sd_selftest()` proti RAM fake bloku včetně případu „mimo rozsah“.

## Nezkontrolováno / omezení tohoto běhu

- **Errata pro SDMMC na rev V** nebyla dohledána (sekce H). Revize silikonu je
  v projektu stejně jen `HYPOTÉZA` (`CLAUDE.md`), takže se to nedá uzavřít bez
  přečtení `HAL_GetREVID()` na desce.
- **Skutečné časování a integrita signálu na 32 MHz** (F-0028) se staticky
  rozhodnout nedá — potřebuje `sd test` na víc kartách, ideálně i po zahřátí.
- **Vendorovaný `ff.c`** nebyl čten celý; četl jsem cíleně `f_mount`,
  `find_volume`, `f_mkfs` a mechanismus zámků (`_FS_REENTRANT`). Předpokládám ho
  nemodifikovaný — neověřeno proti originálu R0.12c.
- **Souběh popsaný v F-0026** je doložený staticky (řetězec volání až po
  `vPortFree`), ale nebyl vyvolán na HW.
