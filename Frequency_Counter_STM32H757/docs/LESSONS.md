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
| L-0007 | Sdílené PLL a systémové hodiny konfiguruje výhradně CM7; CM4 nesmí volat `SystemClock_Config()` ani `PeriphCommonClock_Config()`. | `scripts/check_lessons.sh` (cílená kontrola na `CM4/Core/Src/main.c`) |
| L-0008 | Komentář o chování při poruše piš až po přečtení celého těla funkce včetně generovaného zbytku, ne jen svého `USER CODE` bloku. | `tools/audit.py` (velikosti fault handlerů) + checklist E |
| L-0009 | Kritické volání v generovaném kódu bez `USER CODE` bloku hlídej ověřením dosaženého stavu v `USER CODE`, ne návratovou hodnotou na místě. | UART `status` řádek `NAPAJENI/HODINY:` musí být `OK` |

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
- **Detekce:** `scripts/check_lessons.sh` — cílená kontrola na `CM4/Core/Src/main.c`
  (hledá volání `PeriphCommonClock_Config();`, definici `(void)` ani prototyp nechytá;
  ověřeno pozitivní i negativní kontrolou vzoru).
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

<!-- Nové záznamy přidávej sem, ID pokračuje L-0010, L-0011, … -->

---

## Archiv (neplatné lekce)

*(prázdné)*
