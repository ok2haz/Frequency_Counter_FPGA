# Audit: konfigurace hodin / PWR  (2026-09-09)

- **Commit:** `8521130` (branch `feat/web-dashboard-v12`)
- **Jádro / doména:** CM7 (D1/D2/D3), okrajově CM4
- **HAL / CMSIS:** CMSIS Device **V1.10.7** (`Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h7xx.h:102`),
  odpovídá balíku STM32Cube_FW_H7 V1.12/1.13. Verzovací makro v `stm32h7xx_hal.h` v tomto
  stromu **chybí** (nedohledatelné) — viz Omezení.
- **Soubory čtené celé:** `CM7/Core/Src/main.c`, `Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c`
  (relevantní části), `CM7/Core/Src/stm32h7xx_it.c` (NMI/fault vektory), `CM4/Core/Src/main.c`
  (boot + clock část). **Konfigurace čtená cíleně:** `CM7/Core/Src/i2c.c`, `fmc.c`, `ltdc.c`,
  `rtc.c`, `beeper.c`, `stm32h7xx_hal_msp.c`, `stm32h7xx_hal_timebase_tim.c`,
  `CM7/USB_DEVICE/Target/usbd_conf.c`, `CM7/Core/Inc/stm32h7xx_hal_conf.h`,
  `CM4/Core/Inc/stm32h7xx_hal_conf.h`, `CM7/STM32H757BITX_FLASH.ld`.
- **Projité sekce checklistu:** A (celá), B (body o hodinách a per-core RCC), D (timebase,
  priorita NMI, volatile u sdílených proměnných modulu), E (návratové hodnoty, čekací smyčky,
  reset flagy), G (I2C/FMC/ADC/LTDC/USB — jen z pohledu kernel clocku), H.
- **Neprojité (a proč):** C (DMA/cache/umístění bufferů) — patří modulu 2 „MPU / cache / linker“;
  F (Flash/bootloader) mimo `FLASH_ACR`, protože modul do interní Flash nezapisuje.

## Souhrn

Modul staví SYSCLK 480 MHz z HSE 25 MHz v režimu bypass (PLL1 M=5, N=192, P=2 -> VCO 960 MHz),
HCLK/AXI 240 MHz, všechny APB 120 MHz, VOS0 přes SMPS 1V8 -> LDO. **Hodinový strom je vnitřně
konzistentní a všechny odvozené frekvence, které jsem přepočítal, sedí s tím, co konzumenti
předpokládají** — s jedinou výjimkou I2C (F-0004). Ověřeno mimo jiné: FMC 100 MHz -> SDCLK 50 MHz
(souhlasí s `REFRESH_COUNT` 371), LTDC 25 MHz -> 57,7 Hz obnovování panelu 800x480, ADC 25 MHz/8 =
3,125 MHz, timery 240 MHz (beeper i HAL timebase to počítají správně), SDMMC 64 MHz, USB z HSI48.

Největší riziko modulu není ve výpočtu hodin, ale v **ošetření selhání náběhu**: tři místa
(F-0001, F-0003, F-0007) reagují na chybu napájení/hodin tichým zaseknutím nebo pokračováním
„jako by se nic nestalo“, a to v okně, kdy ještě neběží IWDG ani konzole. F-0002 je navíc
prokazatelný rozpor mezi kódem a dvěma komentáři, které popisují chování, jaké kód nemá.

**Verdikt: podmíněně funkční.** Za normálních podmínek konfigurace hodin funguje a je správně
spočítaná; nefunkční je chování v poruchových stavech (ztráta HSE, nenaběhlé napájení) a jedna
dokumentovaná frekvence neodpovídá skutečnosti.

---

### F-0001 [S2] Návratová hodnota `HAL_PWREx_ConfigSupply()` se ignoruje

- **Místo:** `CM7/Core/Src/main.c:482` (funkce `SystemClock_Config()`)
- **Popis:** Konfigurace napájecího zdroje Vcore je první operace celého náběhu a jediná, na
  které stojí platnost VOS0 a tím i 480 MHz. Volá se bez kontroly výsledku, takže selhání
  napájecí větve kód nezastaví ani neohlásí.
- **Důkaz:** `HAL_PWREx_ConfigSupply()` vrací `HAL_ERROR` ve třech případech
  (`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c:335`, `:357`, `:376`):
  (a) konfigurace napájení je už zamčená a požadovaná hodnota se liší od platné,
  (b) timeout `PWR_FLAG_ACTVOSRDY` (`:353-358`),
  (c) timeout `PWR_FLAG_SMPSEXTRDY` — tato větev se pro `PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO`
  **skutečně provádí** (`:363-379`). Obě čekání mají limit `PWR_FLAG_SETTING_DELAY`.
  Na `main.c:482` není návratová hodnota přiřazena ani testována; následující řádky 486-488
  bezpodmínečně nastaví VOS0 a čekají na `VOSRDY`.
- **Dopad:** Když se externí SMPS větev nerozběhne, kód po ~1 s tiše pokračuje a nastaví
  VOS0 + 480 MHz nad napájením, které pro to není připravené. Projev je přesně ta třída poruchy,
  před kterou varuje checklist A: „naběhne jen někdy / jen s debuggerem“, náhodné HardFaulty,
  nereprodukovatelné pády. Zasahuje celý systém, ne jen modul.
- **Reprodukce:** HYPOTÉZA — staticky je doložené jen ignorování návratové hodnoty. Ověřit:
  přidat dočasně `HAL_StatusTypeDef st = HAL_PWREx_ConfigSupply(...)` a vypsat `st` přes
  `bootled` (konzole v tu chvíli neběží), nebo číst `PWR->CR3` a `PWR->CSR1` sondou po resetu.
- **Návrh opravy:** Návratovou hodnotu vyhodnotit. Reakce **není** `Error_Handler()` — ten
  v tomto okamžiku resetuje (viz F-0007) a při trvalé vadě napájení by vyrobil smyčku. Správně:
  zaznamenat příznak (nový `bootled` krok + globál pro `status`) a pokračovat, protože přístroj
  s degradovaným napájením je pořád lepší než přístroj bez jakéhokoli výstupu.
- **Riziko opravy:** nízké — přidání testu nemění pořadí ani obsah zápisů do PWR. Riziko je jen
  v tom, jaká reakce se zvolí; reset zvolit nesmí.
- **Vztah k lekcím:** `L-0003`, `L-0009`.
- **Stav:** opraveno 2026-09-09, ale **jinak, než navrhoval tento nález**. Kontrola návratové
  hodnoty přímo na `main.c:482` by musela do `SystemClock_Config()`, která **nemá žádný blok
  `USER CODE`** — porušila by pravidlo 6 a první regenerace z CubeMX by ji smazala.
  Místo toho přibyla v `USER CODE` funkce `pwrclk_check()` (`main.c`, volaná z `USER CODE 2`),
  která ověřuje **dosažený stav registrů**: `PWR->CR3` (konfigurace napájení + `SMPSEXTRDY`),
  `PWR->CSR1.ACTVOSRDY`, VOS0 jako `PWR->D3CR` scale 1 **plus** `SYSCFG->PWRCR.ODEN` **plus**
  `VOSRDY`, `HAL_RCC_GetSysClockFreq()`, `HAL_RCC_GetHCLKFreq()` a `FLASH_ACR.LATENCY`.
  Je to přísnější než původní návrh — odhalí i případ, kdy HAL vrátí `HAL_OK` a stav přesto
  nesedí. Výstup: řádek v boot logu a v UART `status` (`NAPAJENI/HODINY:`), vypisuje se vždy.
  Volání `HAL_PWREx_ConfigSupply()` zůstalo beze změny, nic se neresetuje ani nezastavuje.
  Ověřeno: `.text` 594 536 → 595 384 (+848 B), oba nové řetězce dohledány přímo v `.elf`,
  build 0 varování, `tools/audit.py` 92 OK / 0 selhání / 2 s varováním.

---

### F-0002 [S2] `NMI_Handler` končí nekonečnou smyčkou, čímž ruší vlastní ošetření výpadku HSE

- **Místo:** `CM7/Core/Src/stm32h7xx_it.c:107-116` (funkce `NMI_Handler()`)
- **Popis:** Blok `USER CODE 0` správně potvrdí příznak CSS a zvýší počítadlo `g_css_fail`, aby
  ztrátu časové základny bylo možné nahlásit. Hned za ním ale zůstala generovaná
  `while (1) { }`, takže se z obsluhy nikdy nevyjde. Dva komentáře přitom explicitně tvrdí opak.
- **Důkaz:**
  - `stm32h7xx_it.c:107-110` — ošetření CSS (`RCC->CICR = RCC_CICR_HSECSSC; g_css_fail++`).
  - `stm32h7xx_it.c:114-116` — `while (1) { }` uvnitř bloku `USER CODE NonMaskableInt_IRQn 1`.
  - `stm32h7xx_it.c:101-106` — komentář: „NERESETOVAT... HW uz se sam prepnul na HSI, takze
    pristroj BEZI DAL... zaznamenat to a hlasit nahlas. Priznak cte `status` i SYS pilulka.“
  - `CM7/Core/Src/freertos_task_uart.c:488-496` — příkaz `css on` volá `HAL_RCC_EnableCSS()`
    a jeho komentář tvrdí: „Od 2026-09-08 uz NMI neresetuje, takze nejhorsi pripad je hlaseni
    ‚CASOVA ZAKLADNA NEPLATI‘.“ Cesta je tedy za běhu dosažitelná, ne teoretická.
  - `g_css_fail` je korektně `volatile` (`CM7/Core/Inc/freertos_shared.h:244`) — čtenář by
    hodnotu viděl, kdyby se k němu kód kdy dostal.
- **Dopad:** Po `css on` a skutečném výpadku HSE zůstane CPU v NMI (priorita -2, nad vším
  ostatním) -> scheduler, displej, UART i měření stojí. IWDG1 (~4 s, nezávislý čítač) pak celý
  přístroj resetuje. Výsledek je zamrznutí + watchdog reset místo slíbeného „běž dál a hlas to“,
  a `g_css_fail` se nikdy nezobrazí. Je to tedy horší varianta téže poruchy, kterou měl zápis
  z 2026-09-08 odstranit.
- **Reprodukce:** Deterministické, ověřitelné na HW: UART `css on`, pak odpojit/zastavit
  externích 25 MHz na HSE. Očekávané dnes: displej zamrzne, po ~4 s reset, `status` hlásí
  příčinu resetu WATCHDOG. Bez HW dokázáno staticky z těla funkce.
- **Návrh opravy:** Odstranit `while (1) { }` z `USER CODE NonMaskableInt_IRQn 1` — samotný
  návrat z NMI je korektní, příznak už je potvrzený na `:108`, takže se NMI neopakuje.
  Ostatní tři vektory (MemManage/BusFault/UsageFault) nechat beze změny: ty jsou skutečně
  nedosažitelné (`SCB->SHCSR` se nikde nezapisuje) a jejich `while (1)` nic neruší.
- **Riziko opravy:** střední. Návrat z NMI je bezpečný jen proto, že příznak CSS je potvrzený;
  kdyby do NMI někdy vedla i jiná (nepotvrzená) příčina, vznikla by smyčka NMI. Proto opravu
  spárovat s tím, že se návrat provede **jen** ve větvi `if (RCC->CIFR & RCC_CIFR_HSECSSF)`,
  a v ostatních případech chování ponechat.
- **Vztah k lekcím:** `L-0008`.
- **Stav:** opraveno 2026-09-09, ale **jinak, než navrhoval tento nález**. Smazat `while (1)`
  by nestačilo: CSS při výpadku HSE ten oscilátor vypne a přepne SYSCLK na HSI, takže
  SYSCLK spadne 480 → 64 MHz (USART1 pak vysílá ~15 360 Bd místo 115 200 = nečitelná konzole)
  a PLL1/2/3 přijdou o referenci, tedy FMC/SDRAM i LTDC zůstanou bez hodin. „Běžet dál a hlásit
  nahlas“ na této desce **není proveditelné**. Zamrznutí proto zůstává (resetuje IWDG ~4 s),
  ale výpadek se **zaznamená** do crash black-boxu: kind 7, `DR4 = RCC->CR` → po restartu
  `status` ukáže `NMI@<RCC_CR>` (z bitů HSEON/HSERDY/HSION je vidět, jestli HSE opravdu zmizel).
  Dekodér kind 7 už existoval (`rtc.c:163-168`), takže stačil zápis. Oba nepravdivé komentáře
  opraveny. Ověřeno: `.text` 594 504 → 594 536 (+32 B), `NMI_Handler` 84 B (dřív ~52),
  build 0 varování, `tools/audit.py` 92 OK / 0 selhání / 2 s varováním.
- **Zjištění, které z opravy vypadlo (nové, neopravené):** řádek `HSE: CSS hlasil vypadek Nx`
  ve `status` (`freertos_task_uart.c:2016-2018`) se **nemůže nikdy vypsat**. Čte `g_css_fail`,
  což je obyčejná proměnná v `.bss` (`freertos.c:272`), a jediná cesta, jak se zvýší, končí
  zamrznutím a resetem od IWDG — reset RAM vynuluje. Trvalý záznam po výpadku HSE nese jen
  crash black-box (`NMI@<RCC_CR>`). Levná náprava, pokud se ta živá indikace má zachovat:
  přesunout počítadlo do volného BKP registru. **Nechávám otevřené jako S4** — samo o sobě
  to nic nerozbíjí a patří to k modulu „přerušení a RTOS“.

---

### F-0003 [S3] Čekání na `VOSRDY` bez timeoutu, v okně bez watchdogu i konzole

- **Místo:** `CM7/Core/Src/main.c:488`
- **Popis:** `while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}` nemá žádný limit ani únikovou větev.
- **Důkaz:** `main.c:488` — prázdné tělo smyčky, žádné `HAL_GetTick()`. Pro srovnání: všechna
  odpovídající čekání uvnitř HAL mají timeout `PWR_FLAG_SETTING_DELAY`
  (`stm32h7xx_hal_pwr_ex.c:353-358`, `:372-378`). `watchdog_init()` se volá až na `main.c:447`,
  tedy o 40 řádků a několik inicializací **později**, a konzole (`printf`) v tomto okamžiku ještě
  nemá inicializovaný UART/USB.
- **Dopad:** Pokud regulátor nikdy neohlásí připravenost (typicky v důsledku téhož problému
  napájení jako F-0001), přístroj se zastaví natrvalo: černý displej, žádný výstup, žádné
  blikání, a IWDG ještě neběží, takže ani reset. Z pohledu uživatele „deska je mrtvá“.
- **Reprodukce:** HYPOTÉZA — vyžaduje vadu napájení. Ověřit lze nepřímo: dočasně zkrátit
  čekání na počítadlo a při vypršení rozsvítit `bootled` vzor; za normálního HW musí projít
  na první čtení.
- **Návrh opravy:** Ohraničit počítadlem (ne `HAL_GetTick()` — v tomto místě je timebase ještě
  na HSI a měnit se bude vzápětí) a při vypršení nahlásit přes `bootled_fail_n()`, tedy jediný
  výstup, který v této fázi funguje.
- **Riziko opravy:** nízké, pokud se ponechá stejná horní mez chování (čekání zůstane, jen
  přestane být nekonečné). Nesmí se zkrátit natolik, aby propadlo dřív, než regulátor stihne
  ustálit — tady je počítadlo bezpečnější než čas.
- **Vztah k lekcím:** `L-0004`.
- **Stav:** otevřeno

---

### F-0004 [S3] `Timing = 0x70303AEE` odpovídá ~50 kHz, ne dokumentovaným ~100 kHz

- **Místo:** `CM7/Core/Src/i2c.c:41` (I2C4) a `CM7/Core/Src/i2c.c:166` (I2C1)
- **Popis:** Obě sběrnice používají tutéž hodnotu `TIMINGR` a projektová dokumentace ji na třech
  místech označuje jako „~100 kHz“. Přepočet z reálného zdroje hodin dává zhruba polovinu.
- **Důkaz:**
  - Zdroj hodin: `i2c.c:86-87` `I2c4ClockSelection = RCC_I2C4CLKSOURCE_D3PCLK1` a
    `i2c.c:151-152` `I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1`.
  - `main.c:525-529`: `AHBCLKDivider = RCC_HCLK_DIV2` (HCLK 240 MHz),
    `APB1CLKDivider = RCC_APB1_DIV2`, `APB4CLKDivider = RCC_APB4_DIV2`
    -> **PCLK1 = PCLK4 = 120 MHz**; oba typy hodin jsou v `ClockType` uvedené (`main.c:520-522`),
    takže se děličky skutečně aplikují.
  - Rozklad `0x70303AEE`: PRESC = 7, SCLDEL = 3, SDADEL = 0, SCLH = 0x3A = 58, SCLL = 0xEE = 238.
  - t(PRESC) = (7+1)/120 MHz = 66,67 ns; t(SCLL) = 239 x 66,67 ns = 15,93 us;
    t(SCLH) = 59 x 66,67 ns = 3,93 us; perioda SCL = 19,87 us -> **f(SCL) = 50,3 kHz**.
  - Kontrola opačným směrem: pro 100 kHz by tato hodnota potřebovala kernel clock ~238 MHz.
    To je nedosažitelné pro PCLK (strop 120 MHz), ale rovná se HCLK -> hodnota byla nejspíš
    spočítaná se zadaným „I2C clock = 240 MHz“ (inference, ne důkaz).
- **Dopad:** Sběrnice funguje, jen je dvakrát pomalejší, než s čím počítá dokumentace i rozpočty
  časování. Konkrétně: povinné čtení celého 31bajtového rámce FT5x06 trvá cca 6 ms místo cca 3 ms,
  což jde proti pravidlu „žádný spin > ~10 ms“ v hlídaných taskech a mění odhad zátěže UiTasku.
  **Nejde o poruchu** — jde o rozpor mezi zapsaným předpokladem a skutečností.
- **Reprodukce:** Výpočet výše je deterministický. Skutečnou frekvenci na sběrnici ověřit
  osciloskopem na SCL (I2C4 = PH11) — přesně ten postup, který si projekt sám předepisuje.
- **Návrh opravy:** **Hodnotu registru neměnit.** Je označená jako „ručně NEPŘEPOČÍTÁVAT“ a je
  za tím doložený incident (ručně spočítaná 400 kHz hodnota = tmavý displej a zaseknutá ATTINY).
  Opravit se má **dokumentace** (všechna tři místa) na „~50 kHz“ a přepočítat časové rozpočty,
  které z ní vycházejí. Případné zrychlení je samostatné rozhodnutí s měřením, ne součást
  této opravy.
- **Riziko opravy:** nulové, pokud se mění jen dokumentace. Jakákoli změna `TIMINGR` je riziko
  vysoké (viz incident výše).
- **Vztah k lekcím:** `L-0006`.
- **Stav:** opraveno 2026-09-09 (jen dokumentace — `CLAUDE.md` 3 místa, `CUBEMX_CHECKLIST.md`
  2 místa, komentář v `i2c.c`; hodnota `TIMINGR` záměrně beze změny)

---

### F-0005 [S3] CM4 obsahuje nevolanou `PeriphCommonClock_Config()`, která by za běhu zbořila PLL2/PLL3

- **Místo:** `CM4/Core/Src/main.c:338-370`
- **Popis:** CM4 má vlastní kopii konfigurace PLL2/PLL3 včetně výběru zdrojů pro FMC, SPI123 a
  ADC. Dnes se **nevolá**, takže to není živá chyba — je to ale nabitá past, protože právě tohle
  volání CubeMX generuje do `main()` hned za `SystemClock_Config()`.
- **Důkaz:**
  - `grep PeriphCommonClock_Config CM4/Core/Src/main.c` vrací **jediný** výskyt: definici na
    řádku 338. Žádné volání v `main()`.
  - Obsah je bajt za bajtem tentýž jako na CM7 (`CM7/Core/Src/main.c:541-572`), tedy včetně
    `RCC_PERIPHCLK_FMC|RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_SPI2|RCC_PERIPHCLK_LTDC`.
  - `HAL_RCCEx_PeriphCLKConfig()` PLL před přeprogramováním **vypíná**:
    `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c:3717` `__HAL_RCC_PLL2_DISABLE()`
    a `:3821` `__HAL_RCC_PLL3_DISABLE()`.
  - CM7 na těchto PLL závisí za plného běhu: FMC (SDRAM, a tedy framebuffery) z PLL2R,
    LTDC a ADC z PLL3R, SPI2 z PLL2P (`CM7/Core/Src/main.c:565-567`).
  - CM4 dnes správně nekonfiguruje ani systémové hodiny — potvrzeno komentářem
    `CM4/Core/Src/main.c:241` a absencí definice `SystemClock_Config` v tom souboru.
- **Dopad:** Latentní. Kdyby volání kdykoli vzniklo (regenerace z CubeMX, kopie vzoru z CM7,
  budoucí „CM4 si nastaví svoje hodiny“), CM4 by uprostřed běhu na několik mikrosekund odstavil
  hodiny SDRAM, zatímco LTDC z téže SDRAM skenuje obraz a refresh matice se zastaví. Projev by
  byl přesně ta třída, kterou projekt už jednou marně honil: „SDRAM čte samé nuly“ / rozpad
  obsahu / černý displej — a hledalo by se to v kreslicím kódu, ne v hodinách druhého jádra.
- **Reprodukce:** Dnes nedosažitelné (mrtvý kód). HYPOTÉZA pro popsaný dopad — ověřit lze jen
  záměrným zavoláním, což se dělat nemá.
- **Návrh opravy:** Funkci na CM4 **neodstraňovat bez rozmyslu** (regen ji vrátí). Levnější a
  trvanlivější je pojistka: na začátek těla vložit tvrzení, které jádro smí konfigurovat sdílené
  PLL (na CM4 okamžitý návrat), plus poznámka do `CUBEMX_CHECKLIST.md` pro kontrolu po každé
  regeneraci. Alternativa je kontrola v `scripts/check_lessons.sh`, že v `CM4/Core/Src/main.c`
  není volání `PeriphCommonClock_Config()`.
- **Riziko opravy:** nízké — mění se nevolaný kód. Pozor jen na to, aby úprava nespadla mimo
  bloky `USER CODE` (jinak ji regen smaže a pojistka zmizí právě ve chvíli, kdy je potřeba).
- **Vztah k lekcím:** `L-0007`.
- **Stav:** opraveno 2026-09-09 pojistkou (firmware beze změny), ve **dvou vrstvách**:
  1. `scripts/check_lessons.sh` — grep zdrojáku `CM4/Core/Src/main.c`. Vzor ověřen pozitivní
     i negativní kontrolou (volání chytí, definici `(void)` ani prototyp ne).
     ⚠️ Ukázalo se, že tahle vrstva je slabá: skript **nikdo nespouští automaticky** (není
     v `build.sh`, `audit.py` ani v git hooku), takže by past sklapla tiše.
  2. **`scripts/build.sh` → `check_cm4_clock_owner()` — tvrdé selhání buildu (exit 1).**
     Měří **slinkovaný obraz** (`arm-none-eabi-nm` nad `CM4/<cfg>/H757_LED_CM4.elf`), ne text
     zdrojáku: dokud funkci nikdo nevolá, linker ji přes `--gc-sections` zahodí a v obrazu
     není. Nedá se obejít přeformátováním volání. Obsahuje **pozitivní kontrolu měřítka** —
     v obrazu CM7 ten symbol být musí (dnes `0800cb68 T`), jinak test hlásí, že už nic neměří.
     Ověřeno podstrčeným obrazem: obě poruchové větve vrací exit 1, běžný build 0.
  + bod v `CUBEMX_CHECKLIST.md` sekce „RCC / Clock Configuration“.

---

### F-0006 [S3] `FLASH_ACR.WRHIGHFREQ` se nikde neprogramuje

- **Místo:** chybějící zápis; relevantní kód `CM7/Core/Src/main.c:531`
  (`HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4)`) a
  `Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c:196-216`.
- **Popis:** RM0399 předepisuje pro danou kombinaci VOS a frekvence AXI **dvojici** hodnot:
  počet čekacích stavů (`LATENCY`) a zpoždění signálů Flash (`WRHIGHFREQ`). Projekt nastavuje
  jen první z nich.
- **Důkaz:**
  - Grep `WRHIGHFREQ` přes `CM7`, `CM4` a `Common` nenajde **žádný** zdrojový výskyt
    (shody jsou jen v přeložených `.o`, kde symbol pochází z hlavičky CMSIS).
  - HAL programuje výhradně `LATENCY`: `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c:946`
    `__HAL_FLASH_SET_LATENCY(FLatency)`; `SystemInit` v
    `Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c:199` a `:215` rovněž jen `FLASH_ACR_LATENCY`.
  - Cílový stav: VOS0, AXI/HCLK 240 MHz (`main.c:486`, `:524-525`), `FLASH_LATENCY_4`.
- **Dopad:** Nízký v dnešním použití — grep potvrdil, že za běhu se do **interní** Flash nikde
  nezapisuje (žádné `HAL_FLASH_Program` / `HAL_FLASHEx_Erase` / `HAL_FLASH_Unlock` mimo HAL;
  konfigurace i data jdou do W25Q a BKP). Jde tedy o odchylku od předpisu, ne o pozorovanou
  poruchu. Význam vzroste, jakmile přibude IAP/bootloader nebo zápis do interní Flash.
- **Reprodukce:** HYPOTÉZA — skutečnou hodnotu nelze určit staticky (závisí na hodnotě po resetu,
  kterou nikdo nepřepisuje). Ověřit: přečíst `FLASH->ACR` bity [5:4] za běhu (nejlevněji řádkem
  ve `status`, viz pravidlo „nový čítač do `status` je levnější než jedna špatná oprava“),
  a porovnat s tabulkou RM0399 pro VOS0 / 240 MHz.
- **Návrh opravy:** Nejprve **změřit** (přidat výpis), teprve pak případně doplnit zápis
  `WRHIGHFREQ` do `SystemClock_Config()` za `HAL_RCC_ClockConfig()`. Bez měření neměnit —
  slepý zápis do `FLASH_ACR` na běžícím zařízení je horší než dnešní stav.
- **Riziko opravy:** samotné čtení nulové; zápis střední (ovlivňuje časování přístupů k Flash,
  ze které se běží).
- **Vztah k lekcím:** —
- **Stav:** **částečně vyřešeno 2026-09-09 — zavedeno měření, zápis zůstává otevřený.**
  Nález sám předepisoval „nejprve změřit, teprve pak případně zapisovat“. Měření je hotové:
  `pwrclk_check()` odečítá `FLASH_ACR.WRHIGHFREQ` do `g_pwrclk_wrhighfreq` a UART `status`
  ho vypisuje na řádku `NAPAJENI/HODINY:` (`WRHIGHFREQ=<n>`). **Zbývá:** po nejbližším
  naflashování odečíst hodnotu z běžícího přístroje a porovnat ji s tabulkou RM0399 pro
  VOS0 / AXI 240 MHz. Teprve pokud nesedí, řešit zápis — praktický dopad zůstává nízký,
  protože do interní Flash se za běhu nezapisuje.

---

### F-0007 [S3] Ztráta HSE při náběhu končí nečitelnou smrtí přístroje, bez definované politiky

- **Místo:** `CM7/Core/Src/main.c:500` (`HSEState = RCC_HSE_BYPASS`), `:504` (PLL zdroj HSE),
  `:513-516` (`Error_Handler()` při selhání), `:655-693` (`Error_Handler()`)
- **Popis:** Celá časová základna přístroje visí na externích 25 MHz. Když nepřijdou,
  `HAL_RCC_OscConfig()` po timeoutu selže a kód spadne do `Error_Handler()`, který jednou
  resetuje a podruhé už jen bliká donekonečna. Žádná definovaná degradovaná cesta neexistuje.
- **Důkaz:**
  - `main.c:500` `RCC_HSE_BYPASS` (vnější hodiny, ne krystal) — potvrzeno i nezávisle
    komentářem `main.c:303-305`, kde je `HSEBYP=1` doložený čtením `RCC_CR`.
  - `main.c:504` `PLL.PLLSource = RCC_PLLSOURCE_HSE`, `main.c:523` `SYSCLKSource = PLLCLK`.
  - `main.c:513-516`: selhání `HAL_RCC_OscConfig()` -> `Error_Handler()`.
  - `main.c:688-693`: `Error_Handler()` resetuje **nejvýš jednou za pokus o start**, pak
    `bootled_fail()` = nekonečné blikání.
  - CSS, který by výpadek uměl ohlásit, je záměrně vypnutý (`main.c:298-309`) a jeho ošetření
    je navíc rozbité — viz F-0002.
- **Dopad:** Při výpadku reference (mrtvý Si5356/TCXO, odpojený rozvod) je přístroj z pohledu
  uživatele mrtvý: bez displeje, bez konzole, jen blikající LED_1 v krabičce. Diagnóza „chybí
  reference“ se nikam nedostane. Zároveň platí opačný argument: u kmitočtového normálu je běh
  proti špatné základně horší než neběh, takže odmítnout start **může být správná politika** —
  jen dnes není nikde vyslovená ani odlišená od jiných příčin selhání.
- **Reprodukce:** Deterministické na HW: odpojit 25 MHz vstup HSE a resetovat.
- **Návrh opravy:** Rozhodnout politiku (patří do triáže, ne do opravy): buď (a) zůstat mrtvý,
  ale rozlišitelně — vlastní `bootled` krok „HSE chybí“ místo obecného `Error_Handler`, aby
  vzor blikání příčinu nesl; nebo (b) nabootovat na HSI, veškeré měření označit za neplatné a
  hlásit to nahlas (SYS pilulka, `status`). Varianta (b) je pro servis přívětivější, ale je
  podstatně větší zásah a nesmí dovolit, aby se cokoli naměřeného tvářilo jako platné.
- **Riziko opravy:** (a) nízké; (b) vysoké — mění se frekvence všech sběrnic a s nimi časování
  I2C, SPI, DSI/LTDC i DWT prodlev.
- **Vztah k lekcím:** nová lekce po opravě.
- **Stav:** otevřeno

---

## Co bylo zkontrolováno a je v pořádku

Tento seznam je pro reprodukovatelnost stejně důležitý jako nálezy — příště se tyhle body
nemusí počítat znovu, pokud se nezmění PLL.

**A. Napájení, hodiny, náběh**
- `HAL_PWREx_ConfigSupply(PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO)` (`main.c:482`) je **slučitelné
  s VOS0**: HAL výslovně požaduje, aby Vcore napájel LDO
  (`Drivers/.../Inc/stm32h7xx_hal_pwr.h:230-234`), a tato volba SMPS 1,8 V vede na LDO. Kdyby
  bylo zvolené přímé napájení Vcore ze SMPS, VOS0 (a tedy 480 MHz) by bylo neplatné.
- **Podmínka HAL na zapnuté hodiny SYSCFG před VOS0 je splněna**: `__HAL_PWR_VOLTAGESCALING_CONFIG`
  pro tuto rodinu zapisuje `SYSCFG->PWRCR` (`stm32h7xx_hal_pwr.h:258-286`), a
  `__HAL_RCC_SYSCFG_CLK_ENABLE()` proběhne v `HAL_MspInit()`
  (`CM7/Core/Src/stm32h7xx_hal_msp.c:69`), kterou volá `HAL_Init()` na `main.c:240` — tedy
  **před** `SystemClock_Config()` na `:247`. Toto je nejtišší možná chyba v celém modulu
  (zápis do neclockované SYSCFG by se ztratil a čip by běžel 480 MHz na VOS1); pořadí je správné.
- PLL1: ref = 25/5 = 5 MHz, což odpovídá `PLLRGE = RCC_PLL1VCIRANGE_2` (4-8 MHz);
  VCO = 5 x 192 = 960 MHz, `PLLVCOSEL = RCC_PLL1VCOWIDE`; P = 2 -> SYSCLK 480 MHz.
  VCO 960 MHz je na horní hranici, kterou umí až revize V — což je nezávislé potvrzení hypotézy
  o revizi silikonu a je konzistentní s tím, že boot aplikuje workaround pro rev Y podmíněně
  (`Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c:258-263`).
- Sběrnice: D1CPRE = 1 (CPU 480 MHz), HCLK/AXI = 240 MHz, APB1/2/3/4 = 120 MHz. Všechny čtyři
  APB typy jsou v `ClockType` (`main.c:520-522`), takže žádná dělička nezůstala nenastavená.
  Meze pro VOS0 (HCLK 240, APB 120) jsou dodrženy.
- `FLASH_LATENCY_4` odpovídá VOS0 při AXI 240 MHz.
- `HSE_VALUE = 25000000` je shodné v **obou** `hal_conf.h` (`CM7/Core/Inc:109`, `CM4/Core/Inc:109`),
  takže `SystemCoreClockUpdate()` a všechny prescalery odvozené za běhu vyjdou na obou jádrech
  stejně. (Komentář u obou definic mluví o 60 MHz — je zavádějící, ale hodnota je správná;
  patří do fáze komentářů, ne mezi nálezy.)
- `HAL_PWR_EnableBkUpAccess()` (`main.c:492`) je **před** konfigurací LSE (`:493`, `:501`),
  tedy ve správném pořadí.

**B. Dvě jádra**
- CM4 **nekonfiguruje systémové hodiny** — `SystemClock_Config` v `CM4/Core/Src/main.c` vůbec
  není definovaná; komentář na `:241` to potvrzuje. (Nevolaná `PeriphCommonClock_Config` je
  F-0005.)
- Boot handshake: obě čekací smyčky na `RCC_FLAG_D2CKRDY` (`main.c:230-235` a `:263-268`)
  **mají timeout** a při jeho vypršení nespadnou do `Error_Handler()`, ale nastaví
  `g_cm4_absent` a pokračují degradovaně. To je správná volba — opačně by nenaběhlá bank2
  znamenala černý displej. `g_cm4_absent` je `volatile` (`freertos_shared.h:217`).
- CM4 si zvlášť povoluje hodiny ve své doméně (`RCC_C2_*`), protože plain makra `__HAL_RCC_*`
  píší vždy do registrů CM7 — ošetřeno a zdokumentováno v `CM4/Core/Src/main.c:113-135`.

**C/D. Timebase a odvozené frekvence konzumentů** (přepočítáno ručně, ne převzato z komentářů)
- HAL timebase je na **TIM6**, ne na SysTick (`CM7/Core/Src/stm32h7xx_hal_timebase_tim.c:41-93`,
  `main.c:639-642`), což je pro FreeRTOS správně. Výpočet navíc korektně zohledňuje násobič
  timerů: při APB1 děličce != 1 bere `2 x PCLK1` (`stm32h7xx_hal_timebase_tim.c:71-77`)
  -> 240 MHz, prescaler 239 -> 1 MHz, perioda 999 -> 1 kHz.
- Beeper: TIM7 na témže 240MHz timer clocku, PSC = 239 -> 1 MHz, ARR = 624 -> 1600 Hz update
  -> 800 Hz na PH9 (`CM7/Core/Src/beeper.c:36-38`). Souhlasí.
- FMC/SDRAM: `FmcClockSelection = RCC_FMCCLKSOURCE_PLL2` používá **PLL2R**; PLL2 VCO = 5 x 40 =
  200 MHz, R = 2 -> FMC kernel 100 MHz, `FMC_SDRAM_CLOCK_PERIOD_2` (`CM7/Core/Src/fmc.c:153`)
  -> **SDCLK 50 MHz**. To je přesně základ, ze kterého je spočítaný `REFRESH_COUNT` 371
  (`fmc.c:121`, komentář `fmc.c:31`). Konzistentní — po opravě z 2026-09-04 tu není rozpor.
- LTDC: kernel je vždy **PLL3R** (potvrzeno v HAL, `stm32h7xx_hal_rcc_ex.c:1451-1453`,
  `DIVIDER_R_UPDATE`); PLL3 VCO = 5 x 35 = 175 MHz, R = 7 -> **25 MHz**. Proti timingu panelu
  (`CM7/Core/Src/ltdc.c:47-54`): celkem 850 x 510 pixelových hodin = 433 500 -> **57,7 Hz**
  obnovování, aktivní plocha 847-47 = 800 a 502-22 = 480. Sedí na 800x480.
- ADC: `AdcClockSelection = RCC_ADCCLKSOURCE_PLL3` rovněž z PLL3R = 25 MHz; s prescalerem
  `ADC_CLOCK_ASYNC_DIV8`, který si SensorsTask nastavuje sám, vychází 3,125 MHz — přesně
  hodnota uvedená v projektové dokumentaci. **Poznámka pro budoucí změny:** LTDC a ADC sdílejí
  jeden dělič PLL3R, takže změna pixel clocku displeje automaticky mění hodiny ADC a naopak.
- SPI123 (FPGA link): `Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2` = **PLL2P**; P = 1 ->
  200 MHz. Hodnota P = 1 je pro PLL2 platná (`IS_RCC_PLL2P_VALUE` připouští 1-128,
  `stm32h7xx_hal_rcc_ex.h:4291`). Driver si prescaler odvozuje za běhu přes
  `HAL_RCCEx_GetPeriphCLKFreq()`, takže na této frekvenci nezávisí staticky.
- SDMMC: PLL1Q = 960/15 = 64 MHz; s `ClockDiv = 2` vychází SDMMC_CK 16 MHz, což odpovídá
  dokumentovanému stavu.
- USB: `RCC_PERIPHCLK_USB` / `UsbClockSelection = RCC_USBCLKSOURCE_HSI48` je nastaveno
  v `CM7/USB_DEVICE/Target/usbd_conf.c:78-79` a `HSI48State = RCC_HSI48_ON` v
  `main.c:502`. Kernel clock USB tedy **není** zapomenutý (což by jinak byla tichá vada —
  po resetu je `USBSEL` = „disabled“).
- MPU je konfigurována **před** `SCB_EnableDCache()` (`main.c:211` vs. `:220`) — správné pořadí.
  Obsah regionů patří modulu 2.

**E. Reset a diagnostika**
- `RCC->RSR` se čte a maže (`main.c:314-315`) a příčina resetu se převádí na text i příznak
  (`:317-330`) — checklist E splněn.

## Nezkontrolováno / omezení tohoto běhu

- **F1 (inventura) není hotová** — `docs/ARCHITECTURE.md` je pořád prázdná šablona. Audit F3
  tedy běžel bez ověřené mapy vlastnictví periferií; fakta jsem místo toho dokládal přímo ze
  zdrojáků. Tabulky „Hodinový strom“ a „Rozdělení jader“ v `ARCHITECTURE.md` jdou z tohoto
  auditu vyplnit doloženými čísly (viz sekce výše) — je to levné a mělo by se udělat dřív než
  audit dalšího modulu.
- **Umístění bufferů z `.map` (krok 3 zadání) nebylo relevantní** — modul hodin/PWR žádné
  buffery nemá. Dostupné mapy: `CM7/Release/H757_LED_CM7.map` (2026-09-08),
  `CM7/Debug/H757_LED_CM7.map`. Pro modul 2 je použít.
- **Verze HAL nedohledatelná**: v `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal.h` chybí
  makra `__STM32H7xx_HAL_VERSION_*`. Verzi jsem odvodil z CMSIS Device V1.10.7. Kontrola
  proti errata sheetu podle přesné verze HAL tím pádem proběhla jen částečně (checklist H).
- **`RCC_APB4ENR.RTCAPBEN` se v celém projektu ani v HAL nikde nenastavuje** (ověřeno grepem
  přes `CM7`, `CM4`, `Common` i `Drivers/STM32H7xx_HAL_Driver/Src`, s kontrolním pozitivním
  grepem, že hledání skutečně proběhlo). `CM7/Core/Src/rtc.c:235` volá jen `__HAL_RCC_RTC_ENABLE()`,
  což je `RCC_BDCR.RTCEN` (jádrové hodiny RTC), nikoli APB rozhraní. **Nález z toho nedělám**:
  zápisy a čtení `RTC->BKPxR` prokazatelně fungují (persistence tématu, crash black-box
  `stall:` i `hal_err@krok N`), takže buď RTCAPBEN pro přístup k zálohovaným registrům na této
  součástce nutné není, nebo ho zapíná něco, co jsem nenašel. **Otevřená otázka pro HW ověření:**
  platí to i pro `Error_Handler()` volaný **před** `MX_RTC_Init()` (tj. selhání v
  `SystemClock_Config`, `main.c:515` a `:533`)? Ověřit: vyvolat časnou chybu a po restartu se
  podívat, jestli `status` hlásí `hal_err@krok N`, nebo mlčí. Na tom závisí, jestli je
  diagnostika nejhoršího typu selhání skutečná, nebo jen zamýšlená.
- **Vše, co závisí na skutečném HW, je označené `HYPOTÉZA`** u příslušných nálezů: F-0001
  (chování při vadném napájení), F-0003 (timeout regulátoru), F-0004 (frekvence SCL na
  osciloskopu), F-0006 (skutečná hodnota `WRHIGHFREQ`).
- **Nezkoumáno záměrně:** obsah a atributy MPU regionů, linker skripty, priority NVIC mimo NMI,
  a `PeriphCommonClock_Config` z pohledu ostatních periferií (I2C4 kernel jsem ověřil, ostatní
  D3 periferie ne). Patří modulům 2 a „přerušení a RTOS“.
