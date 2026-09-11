# LESSONS.md — registr opravených chyb („neopakovat“)

**Povinné čtení před každou opravou. Povinný zápis po každé opravě.**

Účel: chyba, která už byla jednou vyřešená, se nesmí vrátit — ani ve stejném
místě, ani jinde v projektu. Každý záznam je proto uzavřený tím, že z něj vznikne
**pravidlo** a pokud možno i **automatická detekce** (`scripts/zakazane_vzory.txt`).

## Jak zapisovat

- Nový záznam **na konec sekce „Aktivní lekce“**, ID `L-NNNN` vzestupně, nikdy se
  nepřečísluje ani nemaže (historie musí být stabilní).
- Šablona: `docs/templates/LESSON.md`.
- Pole `Detekce` je nejdůležitější: buď regulární výraz do
  `scripts/zakazane_vzory.txt`, nebo test, nebo bod do `CHECKLIST_STM32H7.md`.
- `Pravidlo` musí být jedna imperativní věta, kterou lze aplikovat i mimo původní
  místo chyby.
- Když se lekce stane nadbytečnou (kód odstraněn), přesuň ji do „Archiv“
  s důvodem — nemazat.

## Rychlý přehled pravidel (čti aspoň tohle)

| ID | Pravidlo | Detekce |
|---|---|---|
| L-0001 | Buffer pro DMA1/DMA2 nikdy neumísťuj do DTCM; ověř sekci v `.map`, ne jen atribut. | `scripts/check_lessons.sh` + kontrola `.map` |
| L-0002 | Ke každému DMA přenosu patří cache operace: clean před TX, invalidate po RX, na 32 B zarovnaný rozsah. | grep `_DMA(` bez `DCache` v modulu |
| L-0003 | Návratovou hodnotu `HAL_*` vždy vyhodnoť, nebo ignorování zdůvodni komentářem. | `-Wunused-result`, clang-tidy |
| L-0004 | Žádná čekací smyčka bez timeoutu. | grep `while *(!*(.*&` |
| L-0005 | Komentáře a logika nikdy v jednom commitu. | kontrola diffu před commitem |
| L-0006 | U konstanty odvozené z hodin uveď zdroj hodin a jeho frekvenci; ověřuj přepočtem z `HAL_RCCEx_GetPeriphCLKFreq()`, ne z komentáře. | checklist G + přepočet při auditu modulu |
| L-0007 | Sdílené PLL a systémové hodiny konfiguruje výhradně CM7; CM4 nesmí volat `SystemClock_Config()` ani `PeriphCommonClock_Config()`. | `scripts/build.sh` — `nm` nad obrazem CM4 (tvrdé selhání) + `scripts/check_lessons.sh` |
| L-0008 | Komentář o chování při poruše piš až po přečtení celého těla funkce včetně generovaného zbytku, ne jen svého `USER CODE` bloku. | `tools/audit.py` (velikosti fault handlerů) + checklist E |
| L-0009 | Kritické volání v generovaném kódu bez `USER CODE` bloku hlídej ověřením dosaženého stavu v `USER CODE`, ne návratovou hodnotou na místě. | UART `status` řádek `NAPAJENI/HODINY:` musí být `OK` |
| L-0010 | Změna firmwaru je hotová až po běhu na desce a po POWER-CYKLU; do té doby `⬜ neověřeno na HW`. Diagnostiku nedávej do bootu před bring-up displeje. | `AUDIT_STATUS.md` (stav ověření u každé opravy) + CLAUDE.md bod 4b/4c |
| L-0011 | Hlášku diagnostiky ber jako pozorování, ne diagnózu — ověř ji proti ostatním číslům z téhož výpisu, než sáhneš do kódu. U paměti: chyby u vzoru `0x00` vylučují vyhasnutí, selhání zápisu s okamžitým ověřením vylučuje retenci. | rozlišovací tabulka v CLAUDE.md („DISPLEJ ZLOBÍ?“) |
| L-0012 | Když opravuješ jednu ze dvou symetrických instancí (I2C1/I2C4, CM7/CM4, FB0/FB1), v témže commitu dolož, že druhá je opravená nebo se jí to netýká. | při opravě grep na sesterskou funkci; poznámka u obou kopií |
| L-0013 | Hook FreeRTOS (`vApplicationStackOverflowHook`) běží v kontextu výjimky, ne úlohy — RTOS API tam mlčky selže. | `osMutexAcquire`/`osDelay` v hooku = nález; použij ISR-safe cestu (RAM ring) |
| L-0014 | Souhrnná čísla neudržuj ručně — odvoď je z místa, kde fakt žije. | `python tools/audit_stav.py --kontrola` |
| L-0015 | Když modul zná svou mez, musí ji na rozhraní vynutit, ne jen odvozovat — u paměti, která adresu mlčky zabalí, je ovladač jediná obrana. | u ovladače paměti se ptej, co udělá s adresou o 1 za koncem; kontrolu piš bez součtu `addr + len` |
| L-0016 | Příznak chyby nikdy nemaž, aniž bys ho přečetl; hlídací mez, kterou lze splést s normálním provozem, není ochrana, ale generátor tichých chyb — a mez i měřidlo její rezervy se navrhují SPOLEČNĚ (můj odhad byl 5× vedle). | `status` → `DMA2D: chyb 0, timeout 0, max cekani << mez`; každý zápis do `*_IFCR` musí mít nad sebou čtení `*_ISR` |
| L-0017 | Tichý přeskok je přípustný jen s počítadlem — co se rozhodneš nevykreslit, musí jít změřit. | `status` → `FONTY: preskocenych glyfu 0` |
| L-0018 | Dvě místa, která počítají touž veličinu, nejsou duplicita kódu — jsou to dvě pravdy čekající, až se rozejdou. Slučuj, neopravuj obě. | při opravě grep na druhou instanci; `status` → `STATISTIKA: sigma_y@1s` |
| L-0019 | Účetnictví, které se veze se stavem (diagnostika, paměť, invalidace), připoj ke ZMĚNĚ toho stavu, ne k některé z cest, které k ní vedou. | `grep -nE "^\s*s_view = [0-9]+;" CM7/app/app_gpsdo.c` musí být prázdný |

*(Řádky výše jsou „startovací“ pravidla vycházející z typických chyb na H7.
Nech je, i když v projektu ještě nenastaly — jsou levné a chrání dopředu.)*

---

## Aktivní lekce

### L-0001 — DMA buffer v DTCM

- **Datum:** (startovací, bez incidentu)
- **Oblast:** DMA / mapa paměti
- **Symptom:** DMA přenos se nespustí nebo skončí `TE` (transfer error);
  data zůstanou nulová. Zdánlivě „náhodně“ podle překladu.
- **Příčina:** DMA1/DMA2 na H7 nemají přístup do DTCM (`0x2000_0000`).
  Proměnná bez explicitní sekce spadne do `.bss` v DTCM.
- **Oprava:** buffer do AXI SRAM (D1) nebo D2 SRAM, explicitní sekce v linkeru
  + `__attribute__((section(".dma_buf"), aligned(32)))`.
- **Pravidlo:** Buffer pro DMA1/DMA2 nikdy neumísťuj do DTCM; umístění ověř v `.map`.
- **Detekce:** `scripts/check_lessons.sh` (heuristika) + při auditu modulu vždy
  dohledat symbol v `build/*.map`.
- **Commit:** —
- **Stav:** aktivní

### L-0002 — Chybějící cache maintenance u DMA

- **Datum:** (startovací, bez incidentu)
- **Oblast:** cache / DMA
- **Symptom:** Data občas stará o jeden rámec; chyba se objeví až při vyšší zátěži
  nebo po zapnutí optimalizací.
- **Příčina:** Zapnutá D-cache, DMA zapisuje/čte přímo z RAM, CPU vidí cache.
- **Oprava:** `SCB_CleanDCache_by_Addr` před TX, `SCB_InvalidateDCache_by_Addr`
  po dokončení RX; adresa i délka zarovnané na 32 B.
- **Pravidlo:** Ke každému DMA přenosu patří odpovídající cache operace na
  32 B zarovnaném rozsahu — nebo buffer v nekešované MPU oblasti.
- **Detekce:** v modulu s `_DMA(` musí být `DCache` nebo dokumentované MPU řešení.
- **Commit:** —
- **Stav:** aktivní

### L-0003 — Ignorovaná návratová hodnota HAL

- **Datum:** (startovací, bez incidentu)
- **Oblast:** ošetření chyb
- **Symptom:** Periferie tiše nefunguje, kód pokračuje, jako by vše proběhlo.
- **Příčina:** `HAL_UART_Init(&huart3);` bez kontroly `!= HAL_OK`.
- **Oprava:** kontrola + definované chování (retry / reset / error log).
- **Pravidlo:** Návratovou hodnotu `HAL_*` vždy vyhodnoť, nebo ignorování
  zdůvodni jednořádkovým komentářem.
- **Detekce:** clang-tidy `bugprone-unused-return-value`, případně grep.
- **Commit:** —
- **Stav:** aktivní

### L-0006 — Konstanta časování spočítaná pro jiný zdroj hodin

- **Datum:** 2026-09-09
- **Oblast:** hodiny / I2C
- **Symptom:** Dokumentace i komentáře na pěti místech tvrdily, že I2C4 i I2C1 jedou ~100 kHz.
  Sběrnice ve skutečnosti jede ~50 kHz, takže každá transakce trvá dvakrát dýl, než s čím
  počítaly odhady zátěže UiTasku a pravidlo „žádný spin > ~10 ms“.
- **Příčina:** `TIMINGR = 0x70303AEE` odpovídá kernelu ~240 MHz, což je **HCLK**. I2C ale běží
  z PCLK (`D3PCLK1` resp. `D2PCLK1`) = **120 MHz**, protože obě APB děličky jsou `/2`.
  Do kalkulátoru časování šla nejspíš frekvence HCLK místo frekvence kernelu periferie.
- **Oprava:** Hodnota registru **ponechána** — je funkční a ověřená provozem, a ruční přepočet
  téhož registru už jednou desku položil (400 kHz → tmavý displej + zaseklá ATTINY).
  Opravena dokumentace: `CLAUDE.md` (3 místa), `CUBEMX_CHECKLIST.md` (2 místa), komentář
  v `CM7/Core/Src/i2c.c`.
- **Pravidlo:** U každé konstanty odvozené z hodin uveď vedle ní **zdroj hodin a jeho frekvenci**;
  při auditu ji přepočítej z `HAL_RCCEx_GetPeriphCLKFreq()`, nikdy ne z komentáře.
- **Detekce:** `CHECKLIST_STM32H7.md` sekce G („I2C: TIMINGR odpovídá reálné frekvenci hodinového
  zdroje periferie“) — při auditu modulu se musí doložit přepočtem, ne odkazem na dokumentaci.
  Volitelné zpřísnění: runtime selftest porovnávající `TIMINGR` proti
  `HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C4)`.
- **Commit:** viz `docs/audit/2026-09-09_hodiny-pwr.md`, nález F-0004
- **Stav:** aktivní

### L-0007 — Sdílené PLL smí konfigurovat jen jedno jádro

- **Datum:** 2026-09-09
- **Oblast:** hodiny / dvě jádra
- **Symptom:** (zatím nenastalo — pojistka proti latentní pasti.) Projevilo by se jako náhlý
  rozpad obsahu SDRAM nebo černý displej za běhu, tedy třída „SDRAM čte samé nuly“, kterou
  by nikdo nehledal v hodinách druhého jádra.
- **Příčina:** CubeMX generuje `PeriphCommonClock_Config()` do `main.c` **obou** jader a volání
  vkládá do `main()` hned za `SystemClock_Config()`. Na CM4 je ta funkce dnes jen definovaná,
  nevolaná. Kdyby se zavolala, `HAL_RCCEx_PeriphCLKConfig()` by PLL před přeprogramováním vypnul
  (`stm32h7xx_hal_rcc_ex.c:3717` `__HAL_RCC_PLL2_DISABLE()`, `:3821` `__HAL_RCC_PLL3_DISABLE()`),
  a CM7 přitom z týchž PLL bere FMC/SDRAM (PLL2R), LTDC a ADC (PLL3R) a SPI2 (PLL2P).
- **Oprava:** Kód firmwaru **beze změny** (mrtvá funkce se nemaže, regen by ji stejně vrátil).
  Přidána kontrola do `scripts/check_lessons.sh` a bod do `CUBEMX_CHECKLIST.md`.
- **Pravidlo:** Sdílené PLL a systémové hodiny konfiguruje **výhradně CM7**; CM4 nesmí volat
  `SystemClock_Config()` ani `PeriphCommonClock_Config()`. Po každé regeneraci to ověř.
- **Detekce (dvě vrstvy, primární je ta druhá):**
  1. `scripts/check_lessons.sh` — cílená kontrola zdrojáku `CM4/Core/Src/main.c`
     (hledá volání `PeriphCommonClock_Config();`, definici `(void)` ani prototyp nechytá).
     ⚠️ **Slabá vrstva:** skript nikdo nespouští automaticky (není v `build.sh`, `audit.py`
     ani v git hooku), takže sama o sobě je to detekce jen pro toho, kdo si ji vyžádá.
  2. **`scripts/build.sh` — `check_cm4_clock_owner()`, tvrdé selhání buildu.** Měří
     **slinkovaný obraz** (`arm-none-eabi-nm` nad `CM4/<cfg>/H757_LED_CM4.elf`), ne text
     zdrojáku: dokud funkci nikdo nevolá, linker ji přes `--gc-sections` zahodí a v obrazu
     není; jakmile volání vznikne, symbol se objeví. Nedá se obejít přeformátováním volání.
     Součástí je **pozitivní kontrola měřítka** — v obrazu CM7 ten symbol být musí, jinak
     test hlásí, že už nic neměří. Obě poruchové větve ověřeny podstrčeným obrazem
     (exit kód 1), běžný build prochází s 0.
- **Commit:** viz `docs/audit/2026-09-09_hodiny-pwr.md`, nález F-0005
- **Stav:** aktivní

### L-0008 — Komentář sliboval chování, které za ním generovaný zbytek funkce ruší

- **Datum:** 2026-09-09
- **Oblast:** obsluha přerušení / chybové cesty
- **Symptom:** `NMI_Handler` měl v `USER CODE 0` korektní ošetření CSS (potvrzení příznaku,
  počítadlo `g_css_fail` pro `status`) a komentář tvrdící „NERESETOVAT, přístroj běží dál
  a nahlas to hlásí“. O osm řádků níž ale zůstala generovaná `while (1) { }`, takže se
  z obsluhy nikdy nevyšlo. Týž nepravdivý popis byl i u UART příkazu `css on`
  („nejhorší případ je hlášení“). Skutečnost: zamrznutí a po ~4 s reset od IWDG.
- **Příčina:** Změna se psala do bloku `USER CODE BEGIN … 0`, ale zbytek těla funkce
  (druhý blok `USER CODE … 1` s `while (1)`) se už nečetl. Komentář tak popisoval záměr,
  ne kód.
- **Oprava:** Chování ponecháno (návrat z NMI na této desce nedává smysl — CSS vypne HSE,
  SYSCLK spadne na HSI 64 MHz, PLL1/2/3 přijdou o referenci, takže USART1 má rozjetý
  baudrate a FMC/SDRAM i LTDC zůstanou bez hodin). Místo toho se výpadek **zaznamená**
  do crash black-boxu (kind 7, `DR4 = RCC->CR`) a komentáře opraveny na pravdu.
- **Pravidlo:** Když komentář popisuje chování při poruše, přečti tu cestu **až po
  uzavírací závorku funkce**, ne jen blok `USER CODE`, do kterého píšeš. Generovaný zbytek
  funkce je součástí chování.
- **Detekce:** `tools/audit.py` už měří velikosti fault handlerů (handler o velikosti
  ~2 B = `b .` = tiché zamrznutí). Doplňkově: při auditu modulu je povinné číst celé tělo
  handleru, viz `CHECKLIST_STM32H7.md` sekce E.
- **Commit:** viz `docs/audit/2026-09-09_hodiny-pwr.md`, nález F-0002
- **Stav:** aktivní

### L-0009 — Kritické volání v generovaném kódu se hlídá ověřením stavu, ne návratové hodnoty

- **Datum:** 2026-09-09
- **Oblast:** hodiny / napájení / regen-safe vzory
- **Symptom:** `HAL_PWREx_ConfigSupply()` v `SystemClock_Config()` se volá bez kontroly
  výsledku (`main.c:482`), přestože umí po ~1 s vrátit `HAL_ERROR` (timeout `ACTVOSRDY`
  nebo `SMPSEXTRDY`). Při vadné napájecí větvi kód tiše pokračuje a nastaví VOS0 + 480 MHz
  nad napájením, které na to nemusí být připravené — projev je „občas nenaběhne“
  nebo náhodný HardFault, tedy nejhůř dohledatelná třída poruch.
- **Příčina (proč se to neopravilo přímočaře):** `SystemClock_Config()` **nemá uvnitř žádný
  blok `USER CODE`**. Přidání `if (... != HAL_OK)` přímo do ní by porušilo pravidlo 6
  a první „Generate Code“ by tu kontrolu smazalo.
- **Oprava:** Volání ponecháno beze změny. V `USER CODE` (`main.c`, funkce `pwrclk_check()`,
  volaná z `USER CODE 2`) se místo toho ověřuje **dosažený stav registrů**: `PWR->CR3`
  (konfigurace napájení + `SMPSEXTRDY`), `PWR->CSR1.ACTVOSRDY`, VOS0 = `PWR->D3CR` scale 1
  **plus** `SYSCFG->PWRCR.ODEN` **plus** `VOSRDY`, `HAL_RCC_GetSysClockFreq()`,
  `HAL_RCC_GetHCLKFreq()` a `FLASH_ACR.LATENCY`. Výsledek jde do boot logu a do UART `status`.
- **Pravidlo:** Když kritické volání leží v generovaném kódu bez `USER CODE` bloku, nehlídej
  ho návratovou hodnotou na místě — **ověř dosažený stav v `USER CODE`**. Je to regen-safe
  a navíc to odhalí i případ, kdy HAL vrátí `HAL_OK` a stav přesto nesedí.
- **Detekce:** UART `status` řádek `NAPAJENI/HODINY:` — musí být `OK`. Vypisuje se vždy,
  i když je vše v pořádku (číslo, které je vidět jen při poruše, si nikdo neověří předem).
- **Commit:** viz `docs/audit/2026-09-09_hodiny-pwr.md`, nález F-0001
- **Stav:** aktivní

### L-0010 — „Přeloženo a v obrazu“ se vydávalo za „ověřeno“; ověření po flashi není ověření

- **Datum:** 2026-09-09
- **Oblast:** metoda ověřování / náběh
- **Symptom:** Opravy z auditu modulu hodiny/PWR (F-0001, F-0002) byly prohlášené za ověřené
  na základě řetězce „build 0 varování + `tools/audit.py` v baseline + povyrostlý `.text`“.
  Hned po naflashování přišlo hlášení, že **po power-cyklu se rozbije zobrazení displeje**,
  a nebylo čím rychle rozhodnout, jestli to je regrese.
- **Příčina:** Ten řetězec dokazuje jen to, že se změna přeložila a dostala do obrazu.
  O chování na desce neříká nic — a **studený start je jiný stav než reset po flashi**:
  ATTINY nabíhá vlastním tempem, SDRAM startuje s náhodným obsahem a degradovanou retencí
  (STATUS #238), obě jádra závodí o sdílená GPIO (#219/#208), FPGA teprve načítá config.
  Druhá polovina příčiny: nová diagnostika (`pwrclk_check()` se dvěma `printf`) byla vložená
  do `main()` **před** bring-up displeje, tedy přesně tam, kde se to časování nemá měnit.
- **Oprava:** Volání přesunuto **za `display_skip:`**, takže bootovní cesta až včetně
  bring-upu displeje je zase bajt za bajtem shodná se stavem před auditem (`git diff` proti
  základu ukazuje v `main.c` **0 smazaných řádků** a žádný přidaný před bring-upem).
  Do `.claude/commands/audit-modul.md` a `CLAUDE.md` doplněna povinnost ověřit power-cyklem.
  ⚠️ Samotná vada displeje se tím **neopravuje** — je to zdokumentované, otevřené
  #141 / #237 / #238 (retence SDRAM po studeném startu), ne regrese.
- **Pravidlo:** Změna firmwaru je hotová až po běhu na desce **a po power-cyklu**; do té doby
  se do `AUDIT_STATUS.md` píše `⬜ neověřeno na HW`. A diagnostiku, která nemusí běžet brzy,
  nedávej do bootovní cesty před bring-up displeje.
- **Detekce:** `AUDIT_STATUS.md` — každá oprava firmwaru musí mít explicitní stav ověření.
  Bod 4b/4c v mechanických pravidlech `CLAUDE.md` + oddíl „fáze F5“ v `audit-modul.md`.
- **Commit:** viz `docs/audit/2026-09-09_hodiny-pwr.md`, oddíl „Incident při ověřování“
- **Stav:** aktivní

### L-0011 — Převzal jsem hypotézu, kterou mi nabídl diagnostický nástroj, místo abych přečetl jeho čísla

- **Datum:** 2026-09-10
- **Oblast:** metoda diagnostiky / FMC-SDRAM
- **Symptom:** Displej problikával po power-cyklu. `membench` k tomu vypsal
  `retence po 1 s: 496 068 chybnych bitu  <- OBSAH SE ROZPADA (refresh?)`. Vzal jsem ten
  závěr a zdvojnásobil obnovu (`REFRESH_COUNT` 371 → 175). **Nepomohlo to** — stálo to jeden
  flash cyklus a jednu změnu, kterou jsem pak musel vrátit.
- **Příčina (moje, ne kódu):** V tomtéž výpisu už byl důkaz, že o retenci nejde, a já ho
  přečetl až napodruhé:
  - vzor **`0x00` dal 103 982 chyb** — zapsat nulu a přečíst nenulu nejde vysvětlit vyhasnutím
    buňky, ta padá **k** nule;
  - selhával už **zápis s okamžitým ověřením** (3 338 207 bitů), ne teprve výdrž;
  - první chyba `@0xC0400040: čekáno 0x00000000, přečteno 0x00000157` — nesmysl při čtení,
    ne rozpadlá data;
  - **DTCM, AXI SRAM i SRAM1 byly 100 % OK** → vada je nutně na externí sběrnici.
  Skutečná příčina byla **čtecí cesta FMC**: `ReadPipeDelay = 0` a nikdy nezapnutá I/O
  kompenzační cela. Po opravě `membench` **0 chybných bitů**, retence **0**, překryv adres
  zmizel, `LTDC podtečení` **0/1000**, displej po power-cyklu OK.
- **Oprava:** `fmc.c` `ReadPipeDelay` → `RPIPE_DELAY_1` (+ runtime `rpipe`), I/O kompenzační
  cela v `main.c` před `MX_FMC_Init` (+ CSI), `REFRESH_COUNT` zpět na 371.
- **Pravidlo:** **Hlášku nástroje ber jako pozorování, ne jako diagnózu.** Když výpis nabízí
  příčinu (`(refresh?)`), ověř ji proti **ostatním číslům z téhož výpisu**, dřív než na ni
  sáhneš do kódu. U paměti to rozhodne jeden řádek: **chyby u vzoru `0x00` vylučují vyhasnutí**
  (buňka padá k nule) a **selhání zápisu s okamžitým ověřením vylučuje retenci** (ta se pozná
  až z výdrže).
- **Druhá polovina lekce:** neprohlašuj dva projevy za nezávislé bez důkazu. Tvrdil jsem, že
  podtečení LTDC je „druhá, nezávislá věc“ — bylo to **totéž**; opravou čtecí cesty zmizelo samo.
- **Detekce:** rozlišovací tabulka je nově přímo v `CLAUDE.md` na začátku oddílu
  „DISPLEJ ZLOBÍ? ZMĚŘ NEJDŘÍV PAMĚŤ“ (vzor `0x00` / mrtvý takt / skutečná retence), takže
  se příště rozhodne z prvního výpisu, ne až z druhého flashe.
- **Commit:** viz `docs/audit/2026-09-09_mpu-cache-linker.md` (F-0013) a STATUS #237/#238/#72
- **Stav:** aktivní

### L-0012 — Oprava se neaplikovala na dvojče

- **Datum:** 2026-09-10
- **Oblast:** I2C / obecně symetrické instance
- **Symptom:** `i2c1_recover()` přepínal PB8 do `OUTPUT_OD` dřív, než nastavil `ODR`, takže na
  okamžik stáhl SCL k zemi. Je to **týž anti-vzor**, který na I2C4 kdysi nechal SCL dole
  natrvalo a kvůli kterému se psala oprava v `i2c4_recover()`.
- **Příčina:** Oprava se udělala jen v jedné ze dvou symetrických instancí. `CLAUDE.md` k ní
  dokonce dostala větu „Totéž hlídej v `i2c1_recover` (PB8)“ — tedy poznámku *pro příště*
  místo změny *teď*. Poznámka se pak nikdy neproměnila v kód.
- **Oprava:** `HAL_GPIO_WritePin(..., GPIO_PIN_SET)` přesunut před `HAL_GPIO_Init`, s odkazem
  na sesterskou funkci přímo v komentáři, aby se ty dvě kopie daly porovnat.
- **Pravidlo:** Když opravuješ jednu ze dvou symetrických instancí (I2C1/I2C4, CM7/CM4,
  FB0/FB1/FB2, snap/cmd/resp), **v témže commitu dolož, že druhá je buď opravená, nebo se jí
  to netýká.** Věta „hlídej to i tam“ není splnění, je to odklad.
- **Detekce:** při opravě grepni jméno sesterské funkce; u obou kopií nech poznámku, že jsou
  párové. V auditu: sekce „Co bylo zkontrolováno“ musí u symetrických driverů uvádět **obě** strany.
- **Commit:** viz `docs/audit/2026-09-10_i2c.md`, nález F-0021
- **Stav:** aktivní

### L-0013 — Hook FreeRTOS není kontext úlohy

- **Datum:** 2026-09-10
- **Oblast:** RTOS / diagnostika
- **Symptom:** `flightrec_dump("stack")` volaný z `vApplicationStackOverflowHook` neudělal
  **nikdy nic** — a neohlásil to. Letový zapisovač tak pro přetečení zásobníku neuložil
  ani jednou to, kvůli čemu existuje.
- **Příčina:** Hook běží uvnitř **PendSV** (`xPortPendSVHandler` → `vTaskSwitchContext` →
  `taskCHECK_FOR_STACK_OVERFLOW`), tedy v kontextu výjimky. `osMutexAcquire()` tam vrací
  `osErrorISR` a funkce se na první řádce vrátí. Omezení bylo přitom známé — u letového
  zapisovače stálo „nezapisuje se z HardFault handleru“ — jen se nedotáhlo na druhý
  exception kontext.
- **Oprava:** zatím **částečná**: neúspěch zvyšuje `g_flightrec_lost` a hlásí ho `status`,
  aby ztráta přestala být tichá. Správně je dvoufázový zápis jako má `errlog`
  (RAM ring + vylití z úlohy).
- **Pravidlo:** **Hook FreeRTOS není kontext úlohy.** Ve `vApplicationStackOverflowHook`,
  `vApplicationMallocFailedHook` ani v `configASSERT` nevolej nic, co potřebuje scheduler
  (`osMutexAcquire`, `osDelay`, fronty) — mlčky to selže. Použij ISR-safe cestu:
  zápis do RAM ringu nebo přímo do registru (BKP).
- **Detekce:** v `freertos_hooks.c` nesmí být `osMutex*`, `osDelay`, `osMessageQueue*`;
  co se z hooku volá dál, musí být v hlavičce označené **ISR-SAFE** (vzor: `errlog.h`).
- **Commit:** viz `docs/audit/2026-09-10_preruseni-rtos.md`, nález F-0018
- **Stav:** aktivní

### L-0014 — Souhrn se rozešel se zdrojem pravdy během jednoho sezení

- **Datum:** 2026-09-10
- **Oblast:** vedení auditu
- **Symptom:** Na otázku „jsou ostatní nálezy opravené?“ se ukázalo, že souhrnná tabulka
  v `docs/AUDIT_STATUS.md` **nesouhlasí s nálezovými dokumenty**: F-0015 byl opravený v kódu
  (commit `8525a10`), ale jeho `Stav:` zůstal „otevřeno“, a součty podle severity byly u S1,
  S3 i S4 špatně. Rozešlo se to **v rámci jednoho dne**, ne za měsíce.
- **Příčina:** Skript, kterým jsem hromadně přepisoval stavy po opravách, dostal seznam tří
  souborů a `2026-09-09_ipc-cm7-cm4.md` v něm chybělo. Součty jsem pak dopočítal ručně
  z paměti místo z dokumentů. Je to **táž třída jako L-0006** (duplikované číslo se rozejde),
  jen o patro výš: souhrn je druhá kopie údaje, který žije v nálezech.
- **Oprava:** Stavy srovnány (F-0015 → opraveno, F-0006 → uzavřeno měřením). Přidán
  **`tools/audit_stav.py`**, který stavy i součty čte přímo z `docs/audit/*.md`;
  s `--kontrola` porovná svůj výsledek se souhrnem a při rozporu skončí nenulovým kódem.
- **Pravidlo:** **Souhrnná čísla neudržuj ručně — odvoď je z místa, kde fakt žije.**
  Když už druhá kopie musí existovat (protože se čte jinde), musí ji hlídat nástroj.
- **Detekce:** `python tools/audit_stav.py --kontrola` — pustit na konci každého sezení
  a po každé dávce oprav.
- **Commit:** viz `docs/audit/2026-09-09_ipc-cm7-cm4.md`, nález F-0015
- **Stav:** aktivní

### L-0015 — Ovladač znal svůj limit, ale nevynucoval ho

- **Datum:** 2026-09-10
- **Oblast:** ovladače externích pamětí (QSPI/SPI/I2C EEPROM)
- **Symptom:** Zatím žádný — je to **latentní** past, nalezená auditem (F-0023). `w25q.c`
  má kapacitu čipu zapsanou v `W25Q_SIZE_BYTES` a používá ji k odvození celé region mapy,
  ale `w25q_read` / `w25q_write` / `w25q_erase_sector` proti ní adresu **nekontrolovaly**.
- **Příčina:** Sériové flash paměti adresu mimo rozsah **neohlásí** — čip vyšší adresní
  bity prostě ignoruje, takže se přístup zabalí zpátky do kapacity a sáhne na jiné místo.
  Chybí tedy jakákoli zpětná vazba: volající dostane „úspěch“, data jsou jinde.
- **Oprava:** `range_ok(addr, len)` na začátku všech tří vstupních bodů, zapsané jako
  `addr < SIZE && len <= SIZE - addr` — **záměrně bez součtu `addr + len`**, který by
  přetekl právě tam, kde má kontrola chytat.
- **Pravidlo:** **Když modul zná svou mez, musí ji na svém rozhraní vynutit, ne jen
  odvozovat.** Platí dvojnásob tam, kde hardware chybu nehlásí — u paměti, která mlčky
  zabaluje adresu, je jediná obrana v ovladači.
  🔑 Při posuzování dopadu se dívej na **nejdestruktivnější** operaci, ne na nejčastější:
  u čtení je následek špatná hodnota, u `erase` **tiše smazaná cizí oblast** — a když je
  rozvržení husté (`w25q_map.h`: CONFIG, CALIB, SETUP, DATA), nesmaže se „nic“, ale
  kalibrace. Projeví se to až po restartu jako „přístroj zapomněl konfiguraci“, tedy
  hodně daleko od příčiny.
  ⚠️ Než takovou mez přidáš, ověř, že na ní **žádný legitimní volající neleží** — jinak
  z latentní pasti uděláš živou regresi.
- **Detekce:** U každého ovladače paměti se zeptej, co udělá s adresou o jedničku za
  koncem. Když odpověď zní „zabalí se“, chybí kontrola.
- **Commit:** `1f69ca9` (viz `docs/audit/2026-09-10_spi-qspi.md`, nález F-0023)
- **Stav:** aktivní

### L-0016 — Příznak chyby se mazal, aniž ho kdo přečetl — a schovával druhou vadu

- **Datum:** 2026-09-10
- **Oblast:** DMA2D / diagnostika periferií
- **Symptom:** Žádný — a právě to byl problém. `d2d_wait()` po každém přenosu mazala
  `DMA2D` příznak chyby přenosu (`TEIF`) bez jediného čtení, a vypršení hlídací
  smyčky se nikam nezapisovalo. Poškozený obdélník tak nezanechal **žádnou stopu**
  a při vyšetřování vypadal jako vada paměti nebo panelu — tedy směr, kterým už
  tenhle projekt několikrát chybně šel (tabulka „HW OBVINĚN — A BYL NEVINNÝ").
- **Příčina:** Mazání příznaků je nutné, aby se nehromadily; jenže „vymazat" se
  napsalo místo „přečíst, započítat, vymazat". Bez čtení je to tichý filtr chyb.
- **Oprava:** `d2d_wait()` teď `ISR` přečte (jednou — je to horká cesta) a při
  `TEIF`/`CEIF` zvedne `g_d2d_errors`; vypršení obou hlídacích smyček zvedne
  `g_d2d_timeouts`, resp. `g_ltdc_flip_timeouts`. Nový řádek `DMA2D:` ve `status`.
  Do masky mazání doplněn `CCEIF`, který se dřív nemazal vůbec.
- 🔑 **Co to okamžitě našlo:** do 25 s běhu **14 vypršení** hlídací meze, rostoucích
  s kreslením (14 → 23 → 25 přes vynucené plné redrawy). Mez 2 000 000 iterací je
  při 480 MHz ~12 ms, tedy **řádově tolik, co celoobrazovkový přenos** s mrtvým
  časem DMA2D 240 — vyprší tedy i za normálního provozu a volající pak DMA2D
  přeprogramuje uprostřed běžícího přenosu (nález F-0036).
- **Pravidlo:** **Příznak chyby nikdy nemaž, aniž bys ho přečetl.** Když se maže
  proto, aby se nehromadil, musí mezi čtením a mazáním být inkrement počítadla,
  které je vidět v `status`. Totéž platí pro vypršení hlídací smyčky: timeout bez
  záznamu je tichá chyba, ne ochrana.
  🔑 A druhá polovina: **hlídací mez, kterou lze splést s normálním provozem,
  není ochrana, ale generátor tichých chyb.** Mez se volí proti nejdelšímu
  LEGITIMNÍMU případu, ne odhadem.
  🔴 **Třetí polovina, kterou jsem se naučil až při opravě:** i ten „nejdelší
  legitimní případ" je potřeba ZMĚŘIT, ne odhadnout. V nálezu F-0036 jsem
  spočítal, že celoobrazovkový přenos trvá ~12 ms, a podle toho zvolil mez
  100 ms. Čítač `g_d2d_wait_max_cyc`, který jsem přidal jako součást téže
  opravy, pak na desce ukázal **~61 ms** — byl jsem **5× vedle** a rezerva
  by byla jen 1,6×. A protože nová verze při vypršení přenos **ruší**, byla
  by ta oprava **horší než původní stav**. Po změření zvýšeno na 500 ms.
  **Když do opravy vkládáš konstantu, přidej zároveň měřidlo, které ukáže
  rezervu** — jinak se odhad nikdy nekonfrontuje s realitou. Mez a měřidlo
  se navrhují společně, ne měřidlo až potom.
- **Detekce:** `status` → řádek `DMA2D:` musí být `chyb 0, timeout 0 | flip timeout 0`.
  Při auditu periferie: každý zápis do `*_IFCR`/`*_ICR` musí mít nad sebou čtení
  odpovídajícího `*_ISR`.
- **Commit:** viz `docs/audit/2026-09-10_vykreslovaci-retezec.md`, nálezy F-0033 a F-0036
- **Stav:** aktivní

### L-0017 — Tichý přeskok místo chyby: chybějící glyf nešlo zjistit jinak než pohledem

- **Datum:** 2026-09-10
- **Oblast:** vykreslování textu / diagnostika
- **Symptom:** `prim_draw_text` chybějící glyf **tiše přeskočí** (`if (g == NULL)
  continue;`) — text na displeji prostě zmizí a nic to neohlásí. Není to teorie:
  audit 2026-08-29 našel **15 takto neviditelných řetězců** (mj. splash „GPSDO"
  a text modalu „Opravdu restartovat?"), protože většina velkých fontů je
  subsetovaná (`mono_75`/`mono_52` jen číslice, `sans_32` jen `Hzsmunp`).
- **Příčina:** Přeskok je sám o sobě správný (fallback glyf by kreslil nesmysl a
  `prim_text_width` skáče stejně, takže se kresba a měření nerozejdou). Chybělo
  ale **jakékoli hlášení**, takže vada prošla překladačem, `audit.py` i selftestem
  a odhalil ji jen člověk, který si všiml prázdného místa.
- **Oprava:** `s_missing_glyphs` v `libprim/src/text.c` + `prim_text_missing_glyphs()`
  a řádek `FONTY:` ve `status`. ⚠️ Počítá se **jen** ve `prim_draw_text`, ne ve
  `prim_text_width` — ta se při zarovnání CENTER/RIGHT volá na týž řetězec navíc
  a chyby by se zdvojily.
- **Pravidlo:** **Tichý přeskok je přípustný jen s počítadlem.** Když se kód
  rozhodne něco nevykreslit / nezpracovat, musí to jít změřit — jinak se z toho
  stane vada, kterou najde až uživatel. V tomhle projektu je to levné: nový čítač
  do `status` stojí jeden build a zůstane užitečný.
- **Detekce:** `status` → `FONTY: preskocenych glyfu 0`. Nahrazuje ruční
  `grep glyph_count` po regeneraci fontů, o kterém `L-0007` říká, že takové
  vrstvy nikdo nespouští.
- **Commit:** viz `docs/audit/2026-09-10_vykreslovaci-retezec.md`, nález F-0034
- **Stav:** aktivní

### L-0018 — Dva vypocty teze veliciny: oprav obe, nebo vyrob jeden zdroj pravdy

- **Datum:** 2026-09-11
- **Oblast:** metrologie / hlavni obrazovka
- **Symptom:** Frakcni odchylka `y = (f-f0)/f0` se pocitala **dvakrat**. V
  `screen_main.c stats_sample()` pevnym meritkem `y = off_n * 1e-14f`, ktere plati
  jen pro `frac == 7` a `f0 == 10 MHz`; v `app_gpsdo.c` (rekonstrukce z datalogu)
  spravne jako `(hz - f0) / f0`. Obe cesty pritom sypou vzorky do **teze** ADEV
  pyramidy, takze se v ni michala dve ruzna meritka. Pri dnesnim vychozim stavu
  (SIM, `frac == 6`) byla vsechna zobrazena cisla **10x mensi**, nez odpovida signalu.
- **Pricina:** Konstanta `1e-14` vznikla zkracenim `10^-frac / f0` za predpokladu,
  ktery **prestal platit** ve chvili, kdy se `s_freq_frac` i `s_freq_nominal_hz`
  staly dynamickymi (dynamicky format headline). Komentar u toho radku ty dva
  predpoklady poctive vyjmenovaval — a presto se prehlizely, protoze se cetly
  jako popis, ne jako podminka platnosti.
- **Oprava:** `screen_main_frac_dev(double hz)` = **jediny** vypocet, ktery volaji
  obe cesty. Oprava „na miste" (prepsat vzorec v `stats_sample`) by nechala dve
  kopie vzorce, tedy presne stav, ktery tu vadu vyrobil.
- **Pravidlo:** **Dve mista, ktera pocitaji touz velicinu, nejsou duplicita kodu —
  jsou to dve ruzne pravdy cekajici, az se rozejdou.** Kdyz pri oprave najdes druhou
  instanci, neopravuj ji zvlast: sluc je do jedne funkce a uved ji v nalezu.
  (Zesileni `L-0012`, ktere zatim rikalo jen „opravit obe".)
- **Detekce:** `status` -> radek `STATISTIKA: sigma_y@1s` (pridan touz opravou,
  protoze sigma_y sla do te doby precist **jen z displeje** — opravu tedy neslo
  na desce overit, jen ji verit). Hodnota musi odpovidat radu signalu; skok o
  dekadu pri prechodu SIM<->REAL znamena, ze se meritka opet rozesla.
- **Commit:** viz `docs/audit/2026-09-11_hlavni-obrazovka.md`, nalez F-0037
- **Stav:** aktivni

### L-0019 — Ucetnictvi bylo pripojene k CESTAM ke zmene stavu, ne ke zmene samotne

- **Datum:** 2026-09-11
- **Oblast:** navigace UI / diagnostika / model fokusu
- **Symptom:** Dve nezavisle vady, ktere vypadaly nesouvisle, dokud se nenapsaly vedle sebe:
  (a) `status` u **hlavni obrazovky a MENU** hlasil cizi okno, takze diagnostika „otevrelo
  se okno?" **aktivne lhala** a potvrzovala zaver „dotyk se neprijal" (F-0047);
  (b) pamet fokusu per okno (zadani UI §7) neplatila, kdyz se uzivatel do okna vratil bez
  otoceni knoflikem (F-0051).
- **Pricina:** Stav `s_view` se menil na **53 mistech**, ale dve navazna ucetnictvi byla
  pripojena jinam — diagnostika do `window_first()` a nacteni fokusu az do obsluhy encoderu.
  Obe ta mista jsou jen **jedna z cest** ke zmene stavu, ne ta zmena. `window_first()`
  nevola 17 ze ~45 oken; obsluha encoderu nebezi, kdyz uzivatel navigoval prstem. Kazda
  cesta, ktera to obesla, ucetnictvi tise vynechala.
- **Oprava:** `view_set(uint8_t)` = **jedine misto, kde se `s_view` meni**, a nese s sebou
  diagnostiku i fokus. Vsech 53 prirazeni jde tudy; `window_first()` diagnostiku uz neplni.
  ⚠️ Sentinel `s_view = 0xFF` (vynuceni plneho renderu) zustal MIMO — neni to prechod na
  jine okno a pres `view_set` by vyrobil falesny zaznam v diagnostice.
- **Pravidlo:** **Ucetnictvi, ktere se veze se stavem — diagnostika, pamet, invalidace,
  notifikace — pripoj ke ZMENE toho stavu, ne k nektere z cest, ktere k ni vedou.**
  Kdyz se stav meni na N mistech, je to N prilezitosti zapomenout; kdyz na jednom, je to
  nula. A nejhorsi varianta neni „nezaznamena se nic", ale **„zaznamena se predchozi
  hodnota"** — to uz neni chybejici udaj, ale nespravny (viz `L-0011`).
- **Detekce:** `grep -nE "^\s*s_view = [0-9]+;" CM7/app/app_gpsdo.c` musi byt **prazdny**
  (radek je i v `scripts/zakazane_vzory.txt`). Obecne: kdyz najdes stav, ktery se meni na
  vic nez par mistech a neco se k nemu „pripocitava", zeptej se, jestli to pripocitavani
  vidi VSECHNY zmeny.
- **Commit:** viz `docs/audit/2026-09-11_navigace-fokus-vstup.md`, nalezy F-0047 a F-0051
- **Stav:** aktivni

<!-- Nové záznamy přidávej sem, ID pokračuje L-0020, L-0021, … -->

---

## Archiv (neplatné lekce)

*(prázdné)*
