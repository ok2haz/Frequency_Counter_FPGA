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
| L-0020 | Duplicitu, kterou je dražší odstranit než snést, převeď na KONTROLU rozdílu — a tu kontrolu vždy ověř pozitivní kontrolou, jinak jsi jen přidal zelené světlo. | `scripts/check_lessons.sh` sekce „dispatch podle `s_view`" |
| L-0021 | Práce, která blokuje jinou práci, musí hlásit, jak dlouho ještě poběží — jinak je její doba trvání neviditelná a nikdo ji neodhalí. A než začneš optimalizovat, přečti hlavičku funkce, kterou voláš. | `status` → `ADEV rekonstrukce:` |
| L-0022 | Obrana, která je opt-in, je neúplná, dokud není u NÍ vyjmenované, kdo ji musí zavolat — a proč nestačí ta druhá, která vypadá podobně. | u každého `s_busy`/`lock` v hlavičce seznam volajících + čím se liší od sousední obrany |
| L-0023 | `static` uvnitř dotazovací funkce přestane být privátní ve chvíli, kdy přibude druhý volající. Dotaz odděl od aktualizace: číst smí kdokoli, posouvat stav jen jedna úloha. | funkce se `static` stavem a víc než jedním volajícím = nález |
| L-0024 | Když limit závisí na REŽIMU, hodnota se smí nastavit nad limit jen po ověření režimu — a když se to vědomě poruší, musí to být VIDĚT. Tiché „nastav a doufej" je to, co se zakazuje; hlášený provoz nad limitem je rozhodnutí. | `sd diag` → řádek `sbernice` uvádí takt, režim, limit a značku `<-- NAD LIMITEM` |
| L-0025 | Objekt předaný cizí knihovně přestaň vlastnit až ve chvíli, kdy ti přestane volat zpátky — `close` není `free`. Odregistruj VŠECHNY callbacky dřív, než uvolníš slot. | `tcp_close`/`*_close` bez předchozího `tcp_arg(pcb, NULL)` = nález; obsluha musí ověřit, že jí ten objekt pořád patří |
| L-0026 | Když do záznamu přibude pole, přepočítej strop bufferu, do kterého se ten záznam skládá — a strop připoj k bufferu `_Static_assert`em, ne komentářem. | `_Static_assert(POCET * MAX_NA_KUS + HLAVICKA < BUFFER)` u každé pevné odpovědi |
| L-0027 | Na neautentizovaném endpointu smí diagnostika vydat jen to, co odesílatel sám poslal. Délka odvozená z tajemství je taky únik. | u každé položky veřejné diagnostiky musí být napsané, PROČ je neškodná; „jen délky a booly" není zdůvodnění |
| L-0028 | Věta v komentáři tvaru „hlídá to X" je TESTOVATELNÁ — najdi řádek, kde se X čte. Zahozená návratová hodnota je nejčastější podoba obrany, která neexistuje. | grep na `tcp_write(`/`f_write(`/`HAL_*` bez uložení návratu, křížem proti komentářům se slovy „hlída", „brani", „osetruje" |
| L-0029 | Funkce volaná přes ukazatel z tabulky musí být SOBĚSTAČNÁ — volající za ni nedodělá krok, který ostatní položky tabulky dělají samy. Přidáváš-li do tabulky položku, projdi, co dělají ostatní. | `scripts/check_lessons.sh` sekce „okno z dlaždicové tabulky neflipne samo" |
| L-0030 | Počet iterací nikdy nesmí záviset na vstupu zvenčí bez meze — a mez odvoď z rozsahu cílového typu, ne odhadem. Ochrana patří PŘED drahou operaci, ne za ni. | grep na `while (n-- > 0)` / `for` s hranicí z parsovaného vstupu; u SCPI vektor `1E999` → `*ok == 0` |
| L-0031 | Datový typ je taky mez. Než začneš zlepšovat algoritmus, spočítej ULP typu, ve kterém hodnota přichází — a porovnej ho s přesností, kterou slibuješ. | u každé metriky konvergence/rozptylu uveď, jaké je rozlišení VSTUPU, ne jen akumulátoru |
| L-0032 | Kontrola integrity podmíněná přítomností toho, co kontroluje, není kontrola. Chybí-li kontrolní součet, je to důvod data zahodit, ne je pustit dál. | grep na `if (checksum_je_pritomen) { kontroluj }` bez `else return` |
| L-0033 | Odmítnutí vstupu patří do VĚTVENÍ, ne do řízení smyčky. `continue` v dlouhé smyčce přeskočí i všechno, co je za ním — u smyčky s obsluhami na konci to není odmítnutí příkazu, ale vypnutí funkcí. | u každého `continue`/`break`/`return` ve smyčce přečti tělo AŽ NA KONEC a vyjmenuj, co se přeskočí |
| L-0034 | Mez ověř PŘED použitím hodnoty, ne po něm — konverze `double`→celé číslo mimo rozsah je UB (ne oříznutí) a odečet v `size_t` podteče na obrovské číslo (ne na zápor). Obojí selže tiše a překladač mlčí. | grep na `(uint64_t)`/`(uint32_t)` nad hodnotou z parseru a na `sizeof(x) - i` s neověřeným `i` |
| L-0035 | Rámec funkce je vlastnost CELÉ funkce, ne větve — GCC rezervuje lokály všech cest už při vstupu, takže velký lokál v jednom příkazu ubere zásobník i cestám, které ho nepoužijí. Měř rámec nad `.elf`, ne odhadem ze zdrojáku. | `scripts/check_lessons.sh` → rámec `UartTask_run` ≤ 1024 B; ručně `objdump -d` a `sub sp, #N` |
| L-0036 | Kadence dat je vlastnost PŘENOSU, ne konstanta konzumenta. Když se transport změní (poll → push), přehodnoť každý výpočet, který si tempo odvozoval — „počet vzorků = sekundy“ přestane platit tiše a graf začne lhát o čase, ne o hodnotách. | u každé historie se ptej: kdo rozhoduje, KDY přibude vzorek? grep na `length` použitou jako čas |
| L-0037 | Přesun tajemství do bezpečnějšího úložiště není hotový, dokud se nesmaže z toho starého — jinak oprava mine právě ty, kdo produkt už používali. | po změně úložiště přidej jednorázový úklid a ověř ho na profilu, kde stará hodnota leží |
| L-0038 | Kontrakt mezi dvěma jazyky uvnitř JEDNOHO obrazu nehlídá nikdo — překladač vidí jen svou půlku. Producent a konzument dat se rozejdou stejně snadno jako dva projekty, jen tišeji: v JS je chybějící pole `undefined`, ne chyba. | `tools/spa/json_kontrakt.py` (krok 5b); obecně: u každé hranice jazyků se ptej, co ten rozpor ohlásí |
| L-0039 | Pozitivní kontrola musí obsahovat KAŽDOU vadu, kvůli které kontrola vznikla — ne jednu zástupnou. Jinak projde a ta druhá zůstane neviditelná. | ke každé nové kontrole napiš tolik pozitivních případů, kolik nálezů ji vyvolalo, a spusť je všechny |
| L-0040 | Ustupuj scheduleru podle ČASU, ne podle počtu iterací. Když jedna iterace může trvat 1 ms i 10 ms (timeout!), počet iterací neomezuje nic — a úloha s vyšší prioritou vyhladoví tu nižší i při „pravidelném“ yieldu. | u každé smyčky s I/O timeoutem: kolik trvá NEJHORŠÍ iterace × kolik jich je mezi yieldy? |

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

### L-0020 — Duplicitu, kterou je drazsi odstranit nez snest, prevedi na KONTROLU rozdilu

- **Datum:** 2026-09-11
- **Oblast:** vedeni oprav / struktura UI dispatch
- **Symptom:** `s_view` je v `app_gpsdo.c` rozvetvene do **peti** nezavislych tabulek
  (46 `case` + 32 vetvi + 10 + 10 + 57 testu). Okna 49 (FUNKCE) a 50 (NAPOVEDA) chybela
  v `render_view()`, takze obnova obrazovky je vykreslila jako hlavni obrazovku — TISE
  (F-0049). Projekt uz jednou tuhle tridu zazil: `goto_view` vs. `render_view` se rozesly
  a symptom byl uplne stejny.
- **Pricina:** Duplicita sama. Jenze sjednotit ji **nebylo spravne**: plosny refaktor sahá
  na kazde okno v kodu, ktery funguje, a dve z tech tabulek se lisi ZAMERNE a s dolozenym
  duvodem (`exit_screensaver` je zuzena kopie `render_view`, protoze plosna varianta
  zamrzla dotykovou vrstvu).
- **Oprava:** Misto sjednoceni **kontrola rozdilu** v `scripts/check_lessons.sh`: okno,
  ktere ma `view_set(N)` ale nema `case N:` v `render_view()`, se nahlasi. Vyjimky
  (default / screensaver / splash) jsou ve skriptu vyjmenovane **i se zduvodnenim**.
- **Pravidlo:** **Kdyz je odstraneni duplicity drazsi nez vada, kterou pusobi, nenechavej
  ji tichou — preved ji na kontrolu rozdilu.** Zaznamenej pritom i to, co se lisit MA,
  a proc; jinak z vyjimek vznikne druhy tichy seznam.
  🔴 **A kazdou takovou kontrolu overi POZITIVNI KONTROLA** — spust ji nad zdrojakem, do
  ktereho jsi vadu schvalne vratil, a prekontroluj, ze zazni. Bez toho jsi nepridal
  kontrolu, ale zelene svetlo. (Tady to bylo nutne: prvni verze se kotvila na
  `/^static void render_view\(/`, coz chytilo DOPREDNOU DEKLARACI o 7 000 radku vys,
  awk skoncil na prvni `}` a test „nenasel" nic — pritom hlasil vsech 52 oken jako
  chybejici. Stejnou past uz projekt zna z `-fanalyzer` + `-fsyntax-only`.)
- **Detekce:** `scripts/check_lessons.sh` — sekce „dispatch podle `s_view`". Musi byt
  cista; kdyz zazni, pridalo se okno a nekdo zapomnel na `render_view`.
- **Commit:** viz `docs/audit/2026-09-11_navigace-fokus-vstup.md`, nalezy F-0049 a F-0050
- **Stav:** aktivni

### L-0021 — Prace, ktera blokuje jinou praci, musi hlasit, jak dlouho jeste potrva

- **Datum:** 2026-09-11
- **Oblast:** statistika / datalog / diagnostika
- **Symptom:** Rekonstrukce ADEV pyramidy z datalogu po bootu blokovala ZIVE
  vzorkovani statistiky **1 h 47 min po kazdem zapnuti** (`sigma_y@1s` zustala 0).
  Nikdo si toho mesice nevsiml a odhalilo to az pocitadlo pridane kvuli jine oprave
  (F-0037). Komentar u kodu pritom sliboval „~2 min".
- **Pricina:** Dve chyby, ktere se nascitaly:
  1. **Rozpocet davky byl spocitany proti kadenci, ktera v kodu neexistuje** —
     komentar predpokladal tik 20 Hz, skutecny volajici bezel 1 Hz (20 zaznamu/s
     misto 400). Instance `L-0006`.
  2. **Doba behu nebyla nikde videt.** Zadny citac, zadny radek v `status` —
     takze „statistika po zapnuti dlouho nic neukazuje" vypadalo jako vlastnost,
     ne jako vada s konkretnim koncem.
- **Oprava:** `datalog_read_bulk` misto `datalog_read_back` (rezie QSPI prikazu se
  rozlozi na 64 zaznamu misto na jeden), sonda „ma to vubec smysl?" pred startem,
  a **radek postupu v `status`**.
- **Pravidlo:** **Kdyz jedna prace blokuje druhou, jeji doba trvani je funkcni
  parametr, ne detail — a musi byt MERITELNA za behu.** Bez toho se z docasneho
  stavu stane neviditelny trvaly stav.
  🔴 **A druha polovina teto lekce: NEZ zacnes neco optimalizovat, precti hlavicku
  funkce, kterou volas.** `datalog.h` mel u `datalog_read_back` napsane „na pruchod
  vice zaznamy pouzij `datalog_read_bulk`" **vcetne zmerenych cisel** (~173 us
  rezie na zaznam vs ~7 us na data). Bulk cesta uz existovala a byla proverena
  (pouziva ji web pres IPC). Puvodni tri navrhy oprav v nalezu ji NEOBSAHOVALY,
  protoze jsem hlavicku funkce, kterou nalez cituje, necetl.
- **Detekce:** `status` -> radek `ADEV rekonstrukce:`. Pri bootu musi bud zmizet
  hned („preskocena"), nebo dojet do „hotova" v jednotkach minut.
  Obecne: u kazde davkove operace, ktera neco blokuje, se zeptej, kde se da
  precist, kolik jeste zbyva.
- **Commit:** viz `docs/audit/2026-09-11_hlavni-obrazovka.md`, nalez F-0039
- **Stav:** aktivni

### L-0022 — Obrana, ktera je opt-in, dojde jen na ty volajici, kteri o ni vedi

- **Datum:** 2026-09-11
- **Oblast:** FatFs / SD / sdilene zdroje mezi ulohami
- **Symptom:** Projekt mel proti odmountovani svazku pod rukama jine ulohy obranu —
  priznak `s_busy`, ktery na dobu dlouhe operace vypne auto-unmount. Byl ale nasazeny
  jen na DVA ze CTYR dlouhych zapisovatelu. `screenshot sd` (1,15 MB) a `f_getfree()`
  ho nenastavovaly, takze vytazeni karty behem nich znamenalo
  `osSemaphoreDelete` -> `vPortFree` nad semaforem, ktery druha uloha PRAVE DRZI —
  tedy zapis do uvolnene haldy FreeRTOS (audit F-0026).
- **Pricina:** Obrana byla **opt-in a nikde nebylo napsane, kdo ji ma zapnout**.
  Navic vedle ni zila druha, podobne vypadajici obrana (`sd_blocking_begin/end`,
  ktera resi PRIORITU, ne svazek) — a `screenshot sd` volal prave tu. Vypadalo to
  tedy jako osetreny pripad, i kdyz byl osetreny jen z poloviny.
- **Oprava:** `sd_export_busy_begin/end()` vystaveno v hlavicce a u nich napsano
  (a) **ze se volaji SPOLU** se `sd_blocking_*`, (b) **cim se lisi**, (c) ze se
  nastavuji vyhradne obalkou nad vyclenenym telem.
- **Pravidlo:** **Obrana, ktera je opt-in, je neuplna, dokud neni U NI vyjmenovane,
  kdo ji musi zavolat — a proc nestaci ta druha, ktera vypada podobne.** Kdyz vedle
  sebe zijou dve podobne pojmenovane obrany, je to samo o sobe duvod to napsat:
  pristi volajici si vybere jednu a bude si myslet, ze ma hotovo.
- **Detekce:** U kazdeho priznaku typu `s_busy`/`lock`, ktery neco vypina, si vypis
  VSECHNY dlouhe operace nad tymz zdrojem a over, ze ho maji vsechny. Tady:
  `grep -n "s_busy = " sd_export.c` proti seznamu volajicich `f_write`/`f_getfree`
  nad svazkem.
- **Commit:** viz `docs/audit/2026-09-10_sdmmc-fatfs.md`, nalez F-0026
- **Stav:** aktivni

### L-0023 — `static` v dotazovaci funkci prestane byt privatni s druhym volajicim

- **Datum:** 2026-09-11
- **Oblast:** detekce SD karty / sdileny stav mezi ulohami
- **Symptom:** `datalog_sd_card_present()` drzela `static uint8_t stable, cnt;` a
  menila je pri KAZDEM dotazu. Volaly ji ale **tri ulohy** (defaultTask, UiTask,
  UartTask) ruznou kadenci. Dusledky dva: neatomicky read-modify-write nad sdilenym
  stavem, a hlavne — casova konstanta debounce byla **nedefinovana**, protoze tri
  nezavisle kadence se scitaly (audit F-0030).
- **Pricina:** Funkce byla napsana jako dotaz a chovala se jako tik. Komentar u ni
  rikal „casovou konstantu urcuje kadence volajiciho" — coz byla pravda, dokud byl
  volajici jeden. Nikdo tu vetu nezkontroloval, kdyz pribyli dalsi dva.
- **Oprava:** Dotaz oddelen od aktualizace: `datalog_sd_card_present()` uz jen cte,
  novy `datalog_sd_det_tick()` posouva stav a vola ho **jedina** uloha.
- **Pravidlo:** **`static` uvnitr dotazovaci funkce prestane byt privatni ve chvili,
  kdy pribude druhy volajici.** Kdyz funkce vypada jako dotaz (`*_present()`,
  `*_get()`, `*_is_*()`), ale meni stav, oddel to: cist smi kdokoli, posouvat stav
  jen jedna uloha.
  ⚠️ A kdyz komentar mluvi o „kadenci volajiciho", je to signal, ze funkce ma
  **jednoho** volajiciho — over, jestli to jeste plati.
- **Detekce:** Funkce se `static` promennou, ktera se v ni **zapisuje**, a s vic nez
  jednim volajicim napric ulohami = nalez. Hledat pres `grep -n "static.*;" ` uvnitr
  funkci + spocitat volajici.
- **Commit:** viz `docs/audit/2026-09-10_sdmmc-fatfs.md`, nalez F-0030
- **Stav:** aktivni

### L-0024 — Limit zavisi na REZIMU: nastav hodnotu az po overeni rezimu, pri selhani spadni na bezpecnou

- **Datum:** 2026-09-11
- **Oblast:** SDMMC / konstanty odvozene z provozniho rezimu
- **Symptom:** `SDMMC_CK` bylo natvrdo 32 MHz a komentar to obhajoval limitem
  **50 MHz**. Jenze 50 MHz plati az pro **High Speed**, do ktereho se karta NIKDY
  neprepinala (nikde v projektu nebyl CMD6). V Default Speed je strop **25 MHz**,
  takze sbernice jela ~28 % nad specifikaci — tise, roky, a na jedne konkretni
  karte to fungovalo (audit F-0028).
- **Pricina:** Hodnota byla nastavena podle limitu rezimu, ktery se nikdy nezapnul.
  Nikdo neoveril, ze ten rezim opravdu naskocil, protoze **nebylo kde** — `sd diag`
  vypisoval takt, ale ne rezim, takze z nej neslo poznat, ktery limit vlastne plati.
  ⚠️ Vlastni pojistka HAL to nechytne: porovnava `ClockDiv` proti
  `sdmmc_clk / (2 x SD_NORMAL_SPEED_FREQ)`, coz je celociselne `64e6/50e6 = 1` —
  takze `ClockDiv = 1` (32 MHz) projde, prestoze je nad 25 MHz.
- **Oprava:** ⚠️ **Takt ZUSTAL 32 MHz, tedy nad limitem Default Speed (25 MHz) —
  vedome rozhodnuti uzivatele (2026-09-11), oprene o dlouhodobe spolehlivy provoz na
  teto desce po HW uprave.** Prepinani do High Speed bylo zkouseno a **odstraneno**
  (na teto karte CMD6 neprosel, takze vendor volani s ~49dennimi smyckami nic
  neprinaselo).
  Opravena je tedy **viditelnost, ne hodnota**: komentar u konstanty uz necituje
  limit rezimu, ve kterem pristroj nebezi, a `sd diag` hlasi takt, rezim, platny
  limit **a znacku `<-- NAD LIMITEM`**.
  🔑 Lekce proto NEtvrdi, ze kod pada na bezpecnou hodnotu. Tvrdi, ze **kdyz se
  limit vedome prekroci, musi to byt videt** — a ze duvod patri k tomu mistu.
- **Pravidlo:** **Kdyz limit zavisi na REZIMU, smi se hodnota nastavit nad limit az po
  overeni rezimu — a kdyz se to vedome porusi, MUSI to byt videt.**
  Zakazane je tiche „nastav a doufej": vypadek prepnuti pak udela **nepoznatelny**
  provoz mimo specifikaci. Hlaseny provoz nad limitem je naopak legitimni rozhodnuti —
  nekdo ho udelal, vi o nem a diagnostika ho pripomene, az zacne karta zlobit.
  🔑 A druha polovina, ktera je tim DULEZITEJSI: **do diagnostiky patri i REZIM, ne
  jen hodnota.** Samotny takt neni overitelny udaj, kdyz strop zavisi na necem, co
  vypis neukazuje. Kdyz se rozhodne jet nad limitem, je ten radek jedina obrana.
- **Detekce:** `sd diag` -> radek `sbernice` musi uvadet takt, rezim i limit.
  Znacka `<-- NAD LIMITEM` neni sama o sobe nalez (dnes je to vedomy stav), ale je to
  **prvni misto, kam se podivat**, kdyz karta zacne hlasit `DATA_CRC_FAIL` nebo
  preruvane poskozeny export. Nalez by byl, kdyby ten radek rezim NEUVADEL.
  Obecne: u kazde konstanty, jejiz komentar cituje nejaky „limit", over, ze rezim,
  pro ktery ten limit plati, je opravdu zapnuty.
- **Commit:** viz `docs/audit/2026-09-10_sdmmc-fatfs.md`, nalez F-0028
- **Stav:** aktivni

### L-0025 — `tcp_close` neni `free`: cizi knihovna volala zpatky na uvolneny slot

- **Kde:** `CM4/LWIP/App/httpd_min.c` (`pump_send`), `CM4/LWIP/App/scpi_tcp.c`
- **Co se stalo:** `pump_send()` po odeslani odpovedi zavolalo `tcp_recv(pcb, NULL)`,
  `tcp_close(pcb)` a `c->pcb = NULL`. Jenze `tcp_close` pcb **NEZRUSI** — necha ho
  v `tcp_active_pcbs` ve stavu FIN_WAIT_1/2 (az `TCP_FIN_WAIT_TIMEOUT` = 20 s, plus
  retransmise `TCP_MAXRTX` = 12) **i s `callback_arg`**, ktery porad ukazuje na ten
  slot. `c->pcb = NULL` ale uz znamena „slot volny", takze `conn_alloc` ho okamzite
  pridelil dalsimu klientovi — a pozdni `on_err` (RST po zavreni posila prohlizec
  bezne) nebo `on_poll` pak sahly na **cizi, zive spojeni** a zabily ho.
- **Jak se to naslo:** cteni vendorovaneho lwIP, ne symptomu. `tcp.c:484` + doc
  komentar `tcp.c:462-470` („put in a closing state … automatically freed in
  `tcp_slowtmr()`") a `tcp_priv.h:223-228`, kde `TCP_EVENT_POLL` predava
  `(pcb)->callback_arg`.
  🔑 **Rozhodujici indicie byla ASYMETRIE ve vlastnim kodu:** `on_poll` v temze
  souboru `tcp_arg(pcb, NULL)` pred `tcp_abort` delalo, `scpi_tcp.c` taky —
  jen `pump_send` ne. Kdyz jedno misto dela navic krok, ktery ostatni nedelaji,
  je to bud zbytecne, nebo tam jinde chybi; tretí moznost neni.
- **Oprava:** spolecne `conn_detach/conn_close/conn_abort`, ktere odregistruji
  `tcp_arg`/`tcp_recv`/`tcp_sent`/`tcp_err`/`tcp_poll` **pred** uvolnenim slotu,
  a druha vrstva: kazda obsluha overi `c->pcb == pcb` a cizi callback zahodi.
- **Pravidlo:** **Objekt predany cizi knihovne prestan vlastnit az ve chvili, kdy ti
  prestane volat zpatky — `close` neni `free`.** Kdyz API rika „po zavreni uz pcb
  nepouzivej", neznamena to „po zavreni uz te nikdo nezavola". Odregistruj VSECHNY
  callbacky, ne jen ten, ktery te zrovna trapi.
- **Detekce:** `tcp_close(`/`*_close(` bez predchoziho `tcp_arg(pcb, NULL)` v temze
  bloku = nalez. Obsluha, ktera dostava `arg` i handle, musi overit, ze k sobe patri.
- **Commit:** `d508139`, viz `docs/audit/2026-09-11_sit-cm4.md`, nalez F-0057
- **Stav:** aktivni

---

### L-0026 — nove pole v zaznamu prebilo strop bufferu, o kterem nikdo nevedel

- **Kde:** `CM4/LWIP/App/httpd_min.c` (`build_log_json`, `HTTPD_BODYBUF_MAX`)
- **Co se stalo:** strop „48 bodu na odpoved `/api/log`" vznikl ve v12 (`c802108`)
  a pocital s bodem o sedmi polozkach. Ve v13 (`2bd7574`) pribyla **min/max obalka
  kmitoctu**, tedy dve dalsi cisla na bod — a strop se **neprepocital**. Pri 10 MHz
  to jeste vyslo (3997 B ze 4096, rezerva 2,4 %), od 100 MHz uz ne: `fmt_scpi_hz_d`
  je o znak delsi a jsou tri na bod. Odpoved pretekla, zapisovac ji **tise oriznul**
  a klient dostal JSON useknuty uprostred tokenu.
- **Proc to bylo horsi nez „graf se nenacte":** SPA hlasila
  `historie se nenacetla … bezi datalog? (CM7 odpovida pres IPC)` — tedy poslala
  uzivatele ladit datalog a mezijaderny kanal, ktere byly v poradku.
  **Tichy orez si vzdy najde nekoho nevinneho, koho obvinit.**
- **Oprava:** rozpocet je VYPOCET hlidany `_Static_assert`
  (`HTTPD_LOG_MAX_PTS * HTTPD_LOG_PT_MAX + HTTPD_LOG_HDR_MAX < HTTPD_BODYBUF_MAX`),
  buffer 4096 -> 6144 B, a orez prestal byt tichy: `jbuf_t.ovf` -> `build_*_json`
  vrati 0 -> volajici posle 503 misto neplatneho JSON.
  ⚠️ Samotne snizeni stropu by nestacilo a jeste by uskodilo: SPA pokracuje
  v sesivani davek jen kdyz dostane **presne** pozadovany pocet bodu, takze mensi
  strop by zkratil historii. Rozpocet se musel zvednout, ne oriznout.
- **Pravidlo:** **Kdyz do zaznamu pribude pole, prepocitej strop bufferu, do ktereho
  se ten zaznam sklada — a strop pripoj k bufferu `_Static_assert`em, ne komentarem.**
  Komentar „strop kvuli velikosti bufferu" nikoho pri pridavani pole nezastavi;
  `_Static_assert` ano.
- **Detekce:** kazda pevna odpoved skladana do pevneho bufferu musi mit
  `_Static_assert(POCET * MAX_NA_KUS + HLAVICKA < BUFFER)`. Zapisovac do pevneho
  bufferu musi umet ohlasit, ze se neco nevesolo (viz **L-0017**).
- **Commit:** `d2038cc`, viz `docs/audit/2026-09-11_sit-cm4.md`, nalez F-0056
- **Stav:** aktivni

---

### L-0027 — „jen delky a booly" prozradilo delku hesla

- **Kde:** `CM4/LWIP/App/httpd_min.c` (`s_auth_dbg`, `GET /api/state`)
- **Co se stalo:** diagnostika posledniho pokusu o prihlaseni nesla
  `expected_len = strlen(web_user) + 1 + strlen(web_pass)` a servirovala se
  v `/api/state`, ktere je **zamerne otevrene** (cteni nevyzaduje autorizaci).
  Komentar u struktury pritom vyslovne tvrdil, ze se „jen delky a bool vysledky"
  exportuji proto, aby *„se pres `/api/state` neda vytahat platne heslo"*.
  Heslo se opravdu vytahnout nedalo — **jeho delka ano**, cimz se zuzuje hruba sila,
  a `expected_len == 1` navic znamena „obe pole prazdna". Dosazitelne jednim parem
  pozadavku bez znalosti hesla: `POST /api/scpi` s libovolnou `Authorization`
  hlavickou pole naplnilo **jeste pred** porovnanim, pak stacilo `GET /api/state`.
- **Oprava:** pole odstraneno ze struktury i z JSON, SPA hlaska ukazuje uz jen delku
  toho, co poslal klient sam. Komentare uvedeny na pravou miru.
- **Pravidlo:** **Na neautentizovanem endpointu smi diagnostika vydat jen to, co
  odesilatel sam poslal.** Delka odvozena z tajemstvi je taky unik — a „neni to cele
  tajemstvi" neni argument. Kdyz u diagnostiky pises „nic tajneho neexportuje",
  **vyjmenuj kazdou polozku a u kazde napis, proc je neskodna**; souhrnne tvrzeni
  o cele strukture se pri pristim pridanem poli stane nepravdivym a nikdo si toho
  nevsimne.
- **Detekce:** u kazde polozky verejne diagnostiky se zeptej „vznikla z dat, ktera
  mi poslal ten, kdo to cte?". Kdyz ne, ven nepatri.
- **Commit:** `3df104c`, viz `docs/audit/2026-09-11_sit-cm4.md`, nalez F-0060
- **Stav:** aktivni

---

### L-0028 — komentar popisoval obranu, kterou kod nedelal (uz podruhe v temze souboru)

- **Kde:** `CM4/LWIP/App/httpd_min.c` (`sse_push`, timeout drzenych spojeni)
- **Co se stalo:** SSE spojeni bylo z timeoutu **vyjmute uplne**, s oduvodnenim
  primo v komentari: *„misto toho ho hlida `tcp_write` chyba a `on_err`"*. Jenze
  `sse_push` zahazoval navratove hodnoty **vsech tri** `tcp_write`. Ta obrana tedy
  neexistovala a klient, ktery zmizel bez FIN/RST (uspany notebook, vypnuta Wi-Fi,
  NAT zahodil stav), drzel jeden z peti slotu minuty.
- 🔴 **Tatáz trida vady je v temze souboru zdokumentovana uz od 2026-09-06**
  (`scpi_tcp.c:135-140`): *„Komentar pritom uz tehdy tvrdil, ze se tomu brani — kod
  delal opak."* Tam slo o zahazovani prilis dlouheho radku, tady o navratovou
  hodnotu — ale mechanismus je stejny a **za tri mesice se to nezmenilo**, protoze
  z toho prvniho nalezu nevznikla lekce, jen komentar na miste.
- **Oprava:** navratove hodnoty se kontroluji (`ERR_MEM` nemuze uprostred nastat —
  misto ve `sndbuf` je overene pro vsechny tri kusy naraz), pri chybe se spojeni
  zrusi, a SSE dostalo vlastni timeout `HTTPD_SSE_IDLE_MS` 120 s **mereny od
  POTVRZENEHO odeslani** (`on_sent`), ne od posledniho pokusu o zapis. To je
  podstatne: do `sndbuf` se mrtvemu klientovi jeste par udalosti zapise, ale ACK
  uz neprijde.
- **Pravidlo:** **Veta v komentari tvaru „hlida to X" je TESTOVATELNA — najdi radek,
  kde se X cte.** Kdyz takovy radek neexistuje, neni to nepresny komentar, ale
  **chybejici obrana**, a komentar ji navic maskuje: pristi ctenar uz hledat nebude.
  Zahozena navratova hodnota je nejcastejsi podoba teto vady (srov. **L-0003**).
- **Detekce:** grep na volani, jejichz navrat se nikam neuklada
  (`tcp_write(`, `f_write(`, `HAL_*`), a krizem proti komentarum se slovy
  „hlida", „brani", „osetruje", „pozna". Kazdy takovy par je kandidat na nalez.
- **Commit:** `d508139`, viz `docs/audit/2026-09-11_sit-cm4.md`, nalez F-0059
- **Stav:** aktivni

---

### L-0029 — okno se kreslilo do zadniho bufferu a nikdy se neukazalo

- **Kde:** `CM7/app/app_gpsdo.c` (`app_gpsdo_render_errlog`, okno CHYBY / s_view=51)
- **Co se stalo:** dlazdice „Chyby (log)" v NASTROJICH pusobila mrtve — po tapu se
  nestalo nic, jen bylo slyset klik. `app_gpsdo_render_errlog()` totiz na konci
  **neflipovalo**. Okna z tabulek `MENU_ITEMS`/`MEAS_ITEMS`/`TOOLS_ITEMS` se volaji
  pres ukazatel (`TOOLS_ITEMS[i].fn()`) a **volajici za ne flip nedodela**: obsluha
  tapu jen vrati `true` a UiTask na to reaguje POUZE zvukovou odezvou. Okno se tedy
  vykreslilo do zadniho bufferu a nikdy se neukazalo — `s_view` uz pritom bylo 51,
  takze pristroj v tom okne „byl", jen ho nebylo videt.
- 🔑 **Ten klik byl DIAGNOSTIKA, ne zvuk navic.** `freertos_task_ui.c:382`:
  `if (app_gpsdo_handle_touch(tx, ty)) { alarm_click(); … }` — klik zazni **prave
  kdyz byl dotyk obslouzeny**. „Nic to nedela, ale klikne" tedy od zacatku rikalo
  *„vstup je v poradku, problem je za nim"*. Kdyby neklikalo, hledalo by se
  v souradnicich a v `list_hit`; takhle slo jit rovnou na vykresleni.
- **Jak se to naslo:** ne hledanim v hlasenem miste, ale **vyctem vsech polozek
  obou tabulek** — ze 22 oken bylo `render_errlog` JEDINE bez vlastniho flipu.
  Kdyz jedna polozka homogenni tabulky dela neco jinak nez ostatnich 21, je to
  bud zamer s oduvodnenim, nebo vada; treti moznost neni.
- **Oprava:** `present_now()` na konec funkce + kontrola do `check_lessons.sh`.
- **Pravidlo:** **Funkce volana pres ukazatel z tabulky musi byt SOBESTACNA.**
  Volajici, ktery ji spousti pres `fn()`, za ni nemuze doplnit krok, ktery ostatni
  polozky delaji samy — nevi, ktera to je. Kdyz do takove tabulky pridavas polozku,
  **projdi, co dela sousedni**, a ne jen to, co potrebuje ta tvoje.
- **Detekce:** `scripts/check_lessons.sh` — sekce „okno z dlazdicove tabulky
  neflipne samo": vytahne jmena funkci z `MENU/MEAS/TOOLS_ITEMS`, najde jejich
  telo a overi `present_now`/`s_dirty = 1`.
  🔴 **Pozitivni kontrola je soucast teto lekce, ne volitelny doplnek** (L-0020).
  Overeno odebranim flipu ze **tri ruznych** funkci (`render_errlog`, `render_mem`,
  `render_sd`) — kontrola pokazde ohlasila spravne jmeno a po obnove zase mlcela.
  ⚠️ A jeste jednou se pritom potvrdila L-0020: **prvni verze toho testu se
  ukotvila na FORWARD DEKLARACI** (`static void f(void);`) misto na definici,
  takze „nic nenasla" a vypadala jako dukaz, ze kontrola nefunguje. Kotvit se musi
  na hlavicku nasledovanou `{`.
- **Commit:** `1b21c82`, viz `docs/audit/2026-09-11_aplikacni-okna.md`, nalez F-0063
- **Stav:** aktivni

---

### L-0030 — exponent bez meze: dve miliardy iteraci z jednoho retezce

- **Kde:** `CM7/Core/Src/scpi.c` (`scpi_num`), dopad na **CM4**
- **Co se stalo:** `scpi_num` cetl exponent do `int` bez omezeni a aplikoval ho
  **iterativne** (`while (e-- > 0) v *= 10.0`). Pocet iteraci byl tim plne
  v rukou odesilatele: `1E2147483647` = 2,1 miliardy nasobeni.
  Nejhorsi to bylo na CM4, kde `-mfpu=fpv4-sp-d16` znamena **single precision**,
  takze kazde `v * 10.0` (double) jde pres softwarovy `__aeabi_dmul` (~50 cyklu)
  → **~450 s zablokovaneho jadra**, ktere zaroven publikuje IPC heartbeat
  (CM7 by hlasil `stall:CM4`) a jehoz IWDG2 je zamerne vypnuty.
- 🔴 **A bylo to dosazitelne BEZ autorizace**, protoze argument se parsuje pri
  rozpoznavani hlavicky (`:869`), kdezto opravneni se testuje az za tim
  (`:871`/`:876`). Ochrana tedy byla **az za** drahou operaci.
- **Oprava:** mez `e > 308` (rozsah `double`) → `*ok = 0` → uz existujici `-224`;
  `if (e < 10000)` navic brani preteceni `int` (signed overflow = UB).
  Poradi parsovani vs. opravneni se ZAMERNE nemenilo — SCPI-99 chce chybu
  prikazu hlasit pred chybou provedeni.
- **Pravidlo:** **Pocet iteraci nikdy nesmi zaviset na vstupu zvenci bez meze —
  a mez odvod z ROZSAHU CILOVEHO TYPU, ne odhadem.** `double` ma 308 dekad, takze
  vyssi exponent neni "velke cislo", ale neplatny vstup.
  🔑 Druha polovina: **ochrana patri PRED drahou operaci.** Kontrola opravneni,
  ktera se provede az po zpracovani argumentu, chrani stav, ale ne cas.
- **Detekce:** grep na `while (n-- > 0)` / cyklus, jehoz hranice pochazi
  z parsovaneho vstupu. U SCPI konkretne vektor `1E999` → `*ok == 0`.
- **Commit:** `aaacaf5`, viz `docs/audit/2026-09-12_parsery-scpi-gps.md`, F-0064
- **Stav:** aktivni

---

### L-0031 — datovy typ byl mez, o ktere nikdo nevedel (self-survey stal na 0,42 m)

- **Kde:** `CM7/Core/Inc/gps.h` (`lat_deg`/`lon_deg` byly `float`),
  konzument `CM7/app/app_gpsdo.c` (`survey_accumulate`)
- **Co se stalo:** self-survey (#53) pocita Welfordem horizontalni rozptyl polohy
  a ten rozptyl je **meritko konvergence** — ma klesat s poctem vzorku. Neklesal
  pod ~0,4 m a vypadalo to jako vlastnost anteny nebo prijimace.
  Pricina byla v **datovem typu**: `float` ma pro hodnotu ~50 stupnu
  ULP 2⁻¹⁸ = 3,81·10⁻⁶ stupne, coz je **0,42 m**. Akumulator pritom `double` byl —
  jenze kvantizace byla uz ve VSTUPU a lepsim akumulatorem se nevrati.
- 🔑 **Jak se to naslo:** ne merenim, ale **vypoctem ULP** pri cteni parseru.
  Predpoved byla ciselna (0,42 m v sirce, 0,27 m v delce na 50°), takze se da
  na HW potvrdit i vyvratit — to je rozdil proti "mozna je to presnosti".
- **Oprava:** souradnice cele celociselne v 1e-7 stupne (`int32_t lat_e7`),
  parsovani bez floatu. ⚠️ **Zisk limituje format zaznamu**: `ddmm.mmmm` (4
  desetiny minut) = 1,85 m, `ddmm.mmmmm` (5) = 18,5 cm. Typ uz uzkym hrdlem neni,
  rozliseni NMEA ano — a to je ted zapsane u pole, ne domyslene.
- **Pravidlo:** **Datovy typ je taky mez.** Nez zacnes zlepsovat algoritmus nebo
  hledat vadu v hardwaru, spocitej **ULP typu, ve kterem hodnota prichazi**, a
  porovnej ho s presnosti, kterou slibujes. U metriky konvergence (rozptyl,
  smerodatna odchylka, residuum) uved, jake je rozliseni VSTUPU — ne jen
  akumulatoru.
- **Detekce:** u kazde veliciny, ktera se ma "zlepsovat s poctem vzorku", musi byt
  napsana spodni mez daná typem a formatem zdroje.
- **Commit:** `868ed6e`, viz `docs/audit/2026-09-12_parsery-scpi-gps.md`, F-0070
- **Stav:** aktivni

---

### L-0032 — kontrola integrity byla podminena tim, ze integrita dorazila

- **Kde:** `CM7/Core/Src/gps.c` (`parse_line`, NMEA checksum)
- **Co se stalo:** cela kontrola checksumu byla uvnitr `if (star)`:
  ```c
  char *star = strchr(l, '*');
  if (star) { … if (cs != given) return; }
  /* else: nic — pokracuje se na parsovani */
  ```
  Veta **bez** `*HH` tedy prosla, jako by byla overena. Pritom prave to je pripad,
  kdy se ma zahodit: NMEA 0183 checksum u `$`-vet vyzaduje a u-blox ho vzdy posila,
  takze jeho absence znamena poskozeny nebo cizi ramec.
- 🔴 **Ve dvojici s chybejicim „zahazuj do konce radku" (F-0066) to byla uplna
  injekcni cesta**: vstup delsi nez buffer, jehoz ocas zacina `$GPRMC,…` bez
  checksumu, se prijal jako platna veta. Zadna z tech dvou vad nebyla sama o sobe
  vic nez „tolerance k sumu".
- **Oprava:** `if (star == NULL) return;`
- **Pravidlo:** **Kontrola integrity podminena pritomnosti toho, co kontroluje,
  neni kontrola.** Chybi-li kontrolni soucet (CRC, checksum, podpis, delka), je to
  duvod data ZAHODIT, ne je pustit dal s tim, ze „nemame cim overit".
  🔑 Obecneji: `if (mame_cim_overit) { over }` je vzdy podezrele — spravne je
  `if (!mame_cim_overit) return;`.
- **Detekce:** grep na `if (<kontrolni_udaj_existuje>) { kontroluj }` bez
  `else return`. U parseru vstupu se na to ptej u KAZDE volitelne casti ramce.
- **Commit:** `b483158`, viz `docs/audit/2026-09-12_parsery-scpi-gps.md`, F-0065
- **Stav:** aktivni

---

### L-0033 — `continue` v dlouhe smycce neodmita prikaz, ale vypina funkce

- **Kde:** `CM7/Core/Src/freertos_task_uart.c` (`UartTask_run`), oprava F-0073.
  🔴 **Tohle je moje vlastni chyba, zachycena pred commitem** — zapisuje se proto,
  ze byla naprosto neviditelna v diffu a vypadala jako spravne reseni.
- **Co se stalo:** utnuty prikaz se mel NEPROVEST. Napsal jsem to takhle:
  ```c
  if (s_rx_trunc) { s_rx_trunc = 0; printf("ERR …"); continue; }
  ```
  Cetl jsem to jako „preskoc zpracovani prikazu". Jenze `UartTask_run` je jedna
  `for (;;)` smycka o ~2000 radcich a **za** zpracovanim prikazu, uplne dole, jeste
  bezi `sd_export_service()`, `datalog_erase_service()`, `membench_service()`,
  `qspi_req_service()` a zaverecny `osDelay(1)`. `continue` tedy neodmitl prikaz —
  **vypnul na tu iteraci export na SD, mazani datalogu, benchmark pameti i QSPI
  pozadavky** a odebral smycce jedine misto, kde ustupuje scheduleru.
- 🔑 **Proc to slo prehlednout:** odmitnuti prikazu a obsluhy pozadavku z UI jsou
  dve nesouvisejici veci, ktere jen bydli v tomtez tele smycky. Diff mel tri radky
  a zadny z nich se tech obsluh netykal — souvislost je **1800 radku daleko**.
- **Oprava:** odmitnuti je prvni clen uz existujiciho `else if` retezu:
  ```c
  if (s_rx_trunc) { s_rx_trunc = 0; printf("ERR …"); }
  else if (RxBuffer[0] == '\0') { /* nic */ }
  else if (strcmp(RxBuffer, "led on") == 0) { … }
  ```
  Vetveni vyjadruje presne to, co jsem chtel (tenhle prikaz se neprovede), a nesaha
  na beh smycky.
- **Pravidlo:** **Odmitnuti vstupu patri do VETVENI, ne do rizeni smycky.**
  U kazdeho `continue`/`break`/`return` uvnitr smycky precti telo **az na konec** a
  vyjmenuj, co se preskoci. V dlouhe smycce s obsluhami na konci je `continue`
  skoro vzdy chyba.
  ⚠️ Plati i pro `return` v inicializacni funkci, za kterou jeste neco bezi.
- **Detekce:** grep na `continue;` ve funkci delsi nez obrazovka; pak se zeptej,
  co je mezi nim a `}` smycky. Nova veta v `docs/STYLE_CZ.md` to nezachyti — je to
  otazka na telo funkce, ne na formu.
- **Commit:** `b1aa262` (oprava uz v poradi; chybny mezistav se necommitoval),
  viz `docs/audit/2026-09-12_uart-konzole.md`, F-0073
- **Stav:** aktivni

---

### L-0034 — mez PRED pouzitim: konverze mimo rozsah a odecet v `size_t`

- **Kde:** `CM7/Core/Src/freertos_task_uart.c` — `fpgasim on <Hz>` (F-0075)
  a `fpgaraw` (F-0076)
- **Co se stalo:** dve ruzne podoby teze chyby, obe v jednom souboru.
  1. **`fpgasim on 99999999999999999999`** — parser cetl cislice do `double` bez
     horni meze (kontroloval jen `hz < 1.0`), takze vysledek byl ~1e20. Nasledne
     `(uint64_t)(hz * 100000.0)` je **nedefinovane chovani**, ne zabaleni: ARM to
     provede jako `VCVT` se saturaci, ale spolehat se na to nelze a hodnota je
     stejne nesmyslna.
  2. **`fpgaraw`** — `p += snprintf(line + p, sizeof(line) - p, …)` bez kontroly
     `p`. `sizeof` je `size_t`, takze pri `p > sizeof(line)` **podtece** na ~1,8e19
     a `snprintf` dostane kapacitu, kterou nema. Dnes je to nedosazitelne
     (16 bajtu x 3 znaky = 48 ze 64), ale rezerva je **16 B** — staci zmenit format.
- 🔑 **Spolecny jmenovatel:** v obou pripadech se hodnota **nejdriv pouzila** a
  teprve pak (nebo vubec) omezila. Prekladac na obojim mlci a `-fanalyzer` taky,
  protoze mez zavisi na vstupu za behu.
- **Oprava:** mez uvnitr akumulacni smycky (`if (hz < 1.0e12) hz = hz*10 + …`) plus
  strop `4.0e9` shodny s `fmt_scpi_hz_d`; u `snprintf` podminka
  `if (p >= 0 && (size_t)p < sizeof line)` pred pouzitim.
  ⚠️ Mez **uvnitr** smycky je zamerne: cislice se dal ctou (parser zustane
  synchronizovany), jen se uz nepricitaji.
- **Pravidlo:** **Mez over PRED pouzitim hodnoty.** Konverze `double`→celociselny
  typ mimo rozsah je UB, ne orez; odecet v bezznamenkovem typu podtece na obrovske
  cislo, ne na zapor. Ani jedno neni „nepravdepodobne cislo", obojim jde projit.
- **Detekce:** grep na `(uint64_t)`/`(uint32_t)` nad hodnotou z parseru a na
  `sizeof(x) - i`, kde `i` neni tesne predtim overene.
- **Commit:** `b1aa262`, viz `docs/audit/2026-09-12_uart-konzole.md`, F-0075/F-0076
- **Stav:** aktivni

---

### L-0035 — ramec je vlastnost cele funkce, ne vetve (a meri se nad `.elf`)

- **Kde:** `CM7/Core/Src/freertos_task_uart.c` (`UartTask_run`, 1 966 radku),
  nalezy F-0077 a F-0074
- **Co se stalo:** cela konzole je **jedna funkce**, ve ktere ma kazdy prikaz svou
  vetev a sve lokaly. GCC ale rezervuje ramec **vsech** lokalu uz pri vstupu do
  funkce, takze velke pole v obsluze jednoho prikazu ubere zasobnik i vsem ostatnim
  cestam — vcetne tech, ktere se toho prikazu nikdy nedotknou.
  Projekt uz tim jednou pretekl: docasny `waste[3600]` udelal ramec 4904 B proti
  4096 B zasobniku → HardFault pri prvnim znaku z USB (STATUS #34).
  Dnes je ramec **700 B** jen proto, ze optimalizator sloty disjunktnich vetvi
  sdili — coz je vlastnost prekladu, ne zaruka.
- 🔑 **Odhad ze zdrojaku tu nefunguje.** Soucet deklarovanych lokalu je o rad vetsi
  nez skutecny ramec (sdilene sloty) a naopak volana funkce si pridava svuj
  (`scpi_process` 492 B, `scpi_exec_one` 268 B). Jedine pouzitelne cislo je
  `sub sp, sp, #N` v disassembly **slinkovaneho obrazu**.
- **Opatreni:** do `scripts/check_lessons.sh` pribyla kontrola, ktera ramec
  `UartTask_run` zmeri v `CM7/Release/H757_LED_CM7.elf` (objdump) a **kricí nad
  1024 B**. Mez je zamerne nizko: UartTask ma 4096 B a na desce mu zbyva 168 B.
  🔴 **Pozitivni kontrola je soucast tohohle opatreni, ne volitelny doplnek
  (L-0020):** poprve kontrola vracela **0 B** — awk mel `match()` se tremi
  skupinami a cetl `m[2]` misto `m[3]`, takze by mlcela nad jakkoli velkym ramcem.
  Overeno snizenim meze na 256 B (musi zakricet: 700 > 256) a zamenou symbolu za
  neexistujici (musi mlcet a nespadnout).
- **Pravidlo:** **Ramec funkce je vlastnost cele funkce, ne vetve.** Velky lokal
  patri do `static` (kdyz je funkce jednovlaknova) nebo do vlastni funkce —
  a jestli to platit zustalo, se overuje **merenim nad obrazem**, ne cetbou.
- **Detekce:** `scripts/check_lessons.sh`; rucne
  `objdump -d … | awk '/<fn>:/{f=1} f&&/sub.*sp, #/{print}'`.
- **Souvislost:** velikost samotneho zasobniku UartTasku resi **TODO #243**
  (`.ioc`, rozhodnuti uzivatele) — tahle lekce je o tom, aby ramec nerostl znovu.
- **Commit:** kontrola v `scripts/check_lessons.sh`, viz
  `docs/audit/2026-09-12_uart-konzole.md`, F-0077
- **Stav:** aktivni

---

### L-0036 — kadence dat je vlastnost prenosu, ne domnenka konzumenta

- **Kde:** `CM4/LWIP/App/httpd_min.c`, SPA (`render`, `xlab`, odecet pod kurzorem),
  nalez F-0080
- **Co se stalo:** graf ukladal jeden bod na kazdou prijatou zpravu a osa X pocitala
  **body jako sekundy**. To platilo, dokud se data tahala pollem 1 Hz. Pak pribyl
  SSE push, ktery server posila pri KAZDEM novem mereni (~4/s pri brane 0,25 s) —
  a osa zacala tvrdit az 4x delsi cas, nez data pokryvala. Okno „1 h" (3600 bodu)
  drzelo ctvrthodinu.
- 🔑 **Nejzajimavejsi na tom je, ze autor tu past znal.** Buffer mereni `M` se plni
  **jen na zmenu `seq_meas`** a komentar nad nim presne vysvetluje proc: *„poll bezi
  1 Hz, ale mereni chodi jinym tempem … opakovane hodnoty vypadaji jako dokonala
  stabilita -> sigma_y by vysla nesmyslne NIZKA"*. Tataz uvaha se ale nepromitla do
  historie grafu `H[]`, ktera je o dvacet radku vedle. **Obrana byla spravna a uplna
  — jen se neaplikovala na druhy buffer v temze souboru.**
- **Oprava:** throttle `H[]` na 1 Hz (praha 0,95 s kvuli jitteru pollu). Druha
  varianta (ukladat ke vzorku cas) byla zvazena a zamitnuta: je vetsi a nevyresila
  by, ze 3600 bodu pri 4/s pokryje jen ctvrthodinu.
- **Pravidlo:** **Kadence dat je vlastnost PRENOSU, ne konstanta konzumenta.**
  Kdyz se zmeni transport (poll -> push, 1 Hz -> event-driven), projdi VSECHNY
  vypocty, ktere si tempo odvozovaly. Chyba se neprojevi chybnymi hodnotami, ale
  chybnym **casem** — a to se pri pohledu na graf pozna nejhur.
- **Detekce:** u kazde historie se zeptej „kdo rozhoduje, KDY do ni pribude vzorek?"
  a grep na `length` pouzitou jako cas (`n-1` sekund, `idx` jako stari).
- **Commit:** `e0e542e`, viz `docs/audit/2026-09-12_spa-web.md`, F-0080
- **Stav:** aktivni

---

### L-0037 — presun tajemstvi neuklidi to stare misto

- **Kde:** `CM4/LWIP/App/httpd_min.c`, SPA (`auth`/`login`/init), nalez F-0082
- **Co se stalo:** heslo se ukladalo do `localStorage` v otevrene podobe a zustavalo
  tam navzdy. Oprava ho presunula do `sessionStorage` (plati do zavreni zalozky).
  🔴 **Samotny presun by ale minul prave ty, kdo web uz pouzivali:** jejich heslo
  lezi v `localStorage` dal a nova verze uz se tam nedivá, takze by ho nikdo nikdy
  nesmazal. Uzivatel by navic mel dojem, ze je problem vyresen.
- **Oprava:** pri prvnim nacteni `localStorage.removeItem('gp')` + hlaska, ze se
  heslo nove uklada jen do zavreni zalozky.
- **Pravidlo:** **Zmena ulozisteho tajemstvi neni hotova, dokud se tajemstvi
  nesmaze z toho stareho.** Plati stejne pro klic v souboru, heslo v BKP registru
  i token v konfiguraci — nove misto je jen pulka prace.
  ⚠️ Tyz vzor plati i pro ZMENSENI rozsahu (kratsi platnost, uzsi opravneni):
  stare zaznamy si drzi stara pravidla, dokud je nekdo aktivne nezrusi.
- **Detekce:** po kazde zmene ulozeni tajemstvi si polozit otazku „co je na starem
  miste TED, na profilu, ktery uz produkt pouzival?" a napsat na to uklid.
- **Commit:** `e0e542e`, viz `docs/audit/2026-09-12_spa-web.md`, F-0082
- **Stav:** aktivni

---

⚠️ **F-0088 nova lekce NENI** — je to dalsi vyskyt **L-0018** (dve mista pocitaji touz
velicinu: warm-up si web odvozoval z `uptime_s`, pristroj z `warmup_ready()`).
Opraveno tak, jak L-0018 zada: kriterium zustalo na jednom miste a prenasi se
**vysledek**, ne vstupy.

---

### L-0038 — kontrakt mezi dvema jazyky uvnitr jednoho obrazu nehlida nikdo

- **Kde:** `CM4/LWIP/App/httpd_min.c` — `build_state_json()` (C) a `SPA_HTML` (JS)
  v temze souboru; nalezy F-0078 a F-0079
- **Co se stalo:** klient cetl `gps.valid` a `gps.nsat`. Ani jedno pole v odpovedi
  neni: `valid` se neemituje vubec a `nsat` lezi o uroven vys (v bloku `gps` je
  `num_sat`). Dusledek byl trvaly a tichy — karta HOLDOVER hlasila `NO LOCK`
  i pri 3D fixu a karta KVALITA GPS zustala navzdy prazdna.
- 🔑 **Nejsilnejsi na tom je, ze obe poloviny jsou v JEDNOM souboru, jednom commitu
  a jednom obrazu.** Neni to rozjeta verze ani zapomenuty deploy: producent
  i konzument se preloz(il)i spolu — jen kazdeho kontroluje neco jineho. C prekladac
  vidi `jputf("\"num_sat\":%u")` jako obycejny retezec, JS zadny prekladac nema
  a cteni neexistujiciho pole je v nem `undefined`, tedy platna hodnota.
- **Oprava:** `valid` odvozen z `fix_mode >= 2` (tentyz zdroj, jaky pouziva pilulka
  v hlavicce), `nsat` -> `num_sat`; a hlavne **kontrola** `tools/spa/json_kontrakt.py`
  zapojena jako krok 5b overovaciho retezce.
- **Pravidlo:** **Hranice mezi jazyky uvnitr jednoho obrazu je stejne krehka jako
  hranice mezi dvema projekty — jen tissi.** U kazde takove hranice se zeptej,
  CO ohlasi rozpor. Kdyz odpoved zni „nic", patri tam kontrola, ne opatrnost.
  ⚠️ Tyz vzor plati pro C ↔ Python nastroje (jmena symbolu v `check_lessons.sh`),
  C ↔ linker skript (jmena sekci) a firmware ↔ `.ioc`.
- **Detekce:** `python tools/spa/json_kontrakt.py` (soucast `check.py`).
- **Commit:** `4a6e4ba` (oprava), `2d5f35f` (kontrola), viz
  `docs/audit/2026-09-12_spa-web.md`, F-0078 a F-0079
- **Stav:** aktivni

---

### L-0039 — pozitivni kontrola musi obsahovat KAZDOU vadu, kvuli ktere vznikla

- **Kde:** `tools/spa/json_kontrakt.py`, pri opravach F-0078 a F-0079
- **Co se stalo:** nova kontrola hranice JSON mela pokryt oba nalezy. Pozitivni
  kontrolu jsem udelal na obou — a vyplatilo se to: pripad (b) (`nsat` existuje,
  ale jinde) **zakricel spravne**, zatimco pripad (a) (`gps.valid` neexistuje
  vubec) **prosel TISE**. Kontrola tedy nenasla prave ten nalez, kvuli kteremu
  primarne vznikla.
- **Proc:** `drawTfom` dostava stav pres `var s=LAST;`, ne jako parametr; nastroj
  umel jen parametr a `LAST.` primo, takze cele telo te funkce ignoroval.
  🔴 Kdybych pozitivni kontrolu udelal jen na jednom (lehcim) pripadu, mel bych
  zelenou kontrolu, ktera **prehlizi polovinu tridy vad** — a duveroval bych ji.
- **Oprava:** doplnena lokalni kopie korene (`var s=LAST`) + komentar primo v kodu
  kontroly, proc tam ten radek je.
- **Pravidlo:** **Pozitivnich pripadu musi byt tolik, kolik nalezu kontrolu
  vyvolalo** — jeden zastupny nestaci, protoze tridu vad obvykle tvori vic cest
  a nastroj muze umet jen nektere. Doplnek k **L-0020**.
  ⚠️ Stejne plati po kazdem rozsireni kontroly: novy pripad = novy pozitivni test.
- **Detekce:** u kazde kontroly si vypsat nalezy, ktere ji vyvolaly, a overit, ze
  na kazdem z nich skonci nenulovym kodem.
- **Commit:** `2d5f35f`, viz `docs/audit/2026-09-12_spa-web.md`
- **Stav:** aktivni

---

### L-0040 — ustupuj podle casu, ne podle poctu iteraci

- **Kde:** `CM7/Core/Src/freertos_task_uart.c` (`i2cspeed_run`), nalez z mereni
  2026-09-12; TODO #244
- **Co se stalo:** mereni chybovosti I2C4 pousti ostatni ulohy ke slovu **po 64
  transakcich**. Pri 25-100 kHz je to v poradku (transakce trva ~1 ms, tedy
  ~64 ms mezi ustupy). Pri 150 kHz ale kazda transakce skonci **plnym 10ms
  timeoutem**, takze tentyz kod drzi CPU **~640 ms v kuse** — a UiTask
  (BelowNormal) se nespusti vubec. Jeho heartbeat zestarne pres 2,5 s,
  `watchdog_supervise` prestane krmit IWDG a deska se resetuje.
  🔑 **Zmereno:** `N=25` krok dokoncil, `N=500` i `N=1000` restart. Rozhoduje
  DOBA, ne frekvence.
- 🔴 **Past je v tom, ze yield tam BYL a vypadal pravidelne.** Kdo cte kod, vidi
  „kazdych 64 transakci se ustupuje" a ma pocit, ze je to osetrene. Jenze
  „64 transakci" neni jednotka casu — a prave v poruchovem rezimu, kde na tom
  zalezi, se hodnota te jednotky zmeni o rad.
- **Pravidlo:** **Ustupuj podle CASU.** `if (HAL_GetTick() - last >= 20u) { osDelay(1); last = ...; }`
  omezi drzeni CPU bez ohledu na to, jak dlouho trva jedna iterace. Pocet iteraci
  se smi pouzit jen tam, kde je iterace prokazatelne kratka a NEMA timeout.
  ⚠️ Druha polovina: u smycky, ktera bouchá do nefunkcniho HW, patri i **mez
  neuspechu** — tisic marnych pokusu neprinese vic informace nez padesat.
- **Detekce:** u kazde smycky s I/O timeoutem si spocitej `nejhorsi_iterace ×
  pocet_mezi_yieldy`. Kdyz to prekroci ~100 ms, je to vada (projektove pravidlo
  „zadny spin > ~10 ms" plati pro hlidane tasky; tady slo o nehlidany UartTask,
  ktery ale muze vyhladovet hlidane).
- **Stav:** aktivni — **oprava `i2cspeed` zatim NEPROVEDENA** (TODO #244),
  uzivatel mereni uzavrel driv. Do te doby plati provozni opatreni: nad 100 kHz
  jen male `N`.

---

<!-- Nové záznamy přidávej sem, ID pokračuje L-0025, L-0026, … -->

### L-0045 — Kmitočtový limit bit-bang I2C slave = jeho vlastní CPU takt

- **Datum:** 2026-09-13
- **Oblast:** periferie (I2C4), metodika měření
- **Symptom:** dva samostatné sweepy chybovosti (2026-09-10, 2026-09-12 se
  třikrát tvrdšími pull-upy) shodně naměřily koleno mezi 75 a 100 kHz, kde
  začíná NACKovat jen ATtiny na 0x45 — dotyk (FT5x06, 0x38) a teploměr
  (TMP117, 0x48) na téže sběrnici zůstávaly čisté až do 100 kHz. Oba
  dokumenty explicitně nechaly otevřenou otázku **proč** koleno leží zrovna
  tam — jestli je to firmware slave, nebo náběžná hrana (pull-up × kapacita).
- **Příčina:** ATtiny na desce má **CPU CLK 1 MHz**. Je to bit-bang I2C
  slave — SCL/SDA časování dělá firmware na ATtiny polling smyčkou/přerušením,
  ne hardwarový I2C blok — a při 1 MHz jádrovém taktu nestíhá obsloužit hrany
  nad ~75 kHz. FT5x06 a TMP117 mají oba hardwarový I2C blok, takže tenhle
  limit na ně neplatí a mohly by běžet rychleji, aniž by to ATtiny ohrozilo.
- **Oprava:** I2C4 dostala dvě provozní rychlosti (`i2c4_speed_select()` v
  `i2c.c`) — 50 kHz pro ATtiny (beze změny, bezpečná rezerva do ~75 kHz),
  200 kHz pro FT5x06+TMP117. Přepíná se podle cílového zařízení PŘED každou
  transakcí, pod `i2c4MutexHandle`.
- **Pravidlo:** **Když bit-bang slave omezuje rychlost celé I2C sběrnice,
  zjisti jeho CPU takt DŘÍV, než začneš měřit sweep chybovosti napříč
  všemi zařízeními na sběrnici najednou.** Pevná (fyzikální) mez daná
  hodinovým kmitočtem firmwaru slave je jiná třída limitu než integrita
  signálu (pull-up/kapacita/náběžná hrana) — první se nedá obejít NIČÍM na
  master straně ani na desce (silnější pull-up nepomůže, protože slave stejně
  nestihne zpracovat data), druhá ano. Škrtit VŠECHNA zařízení na sběrnici na
  rychlost nejpomalejšího slave je zbytečné, pokud ten slave je jediný
  bit-bang mezi hardwarovými I2C periferiemi — per-target `TIMINGR` přepínání
  (bezpečný vzor: `DeInit` → změna `Init.Timing` → `Init`, nikdy
  `MX_I2C4_Init` s jeho `Error_Handler()` trapem) škáluje sběrnici na
  rychlost KAŽDÉHO zařízení zvlášť.
- **Detekce:** žádná automatická — šlo o doménovou znalost HW (datasheet
  ATtiny), kterou žádný sweep ani analyzátor nemůže odvodit ze samotné
  chybovosti. ⚠️ **200 kHz pro FT5x06/TMP117 samotné (bez souběžné zátěže
  ATtiny) zůstává ⬜ neověřeno na HW** — obě existující měření mají čistá data
  jen do 100 kHz, vše nad 125 kHz je v obou dokumentech označeno jako
  kontaminované zavěšenou sběrnicí (viz oprava bodu 3 v
  `docs/audit/2026-09-10_i2c.md` — i to tvrzení dřív citovalo nedůvěryhodná
  data). Ověřit `i2cspeed` s malým `N`, jen `0x38`/`0x48`.
- **Commit:** (viz git log — commit bezprostředně po tomto zápisu)
- **Stav:** aktivní

### L-0044 — Ne každou fragilní `-I` cestu se má odstranit stejným trikem

- **Datum:** 2026-09-13
- **Oblast:** build (CubeMX regen, `CM7/.cproject`, libui/libprim architektura)
- **Symptom:** `CM7/.cproject` má čtyři `-I` cesty (`app`, `libui/include`,
  `libprim/include`, `libprim/src`), které byly jednou (2026-08-29) fragilní
  a vyžádaly manuální opravu. Po opravě L-0042/L-0043 (odstranění závislosti
  na `-I` u `scpi.h` v CM4) padla otázka, jestli udělat totéž i tady.
- **Příčina zjištěná při zkoumání:** rozšířený scan (`find_fragile_includes.py`
  varianta pro CM7) zprvu ukázal jen 12 fragilních řádků — ale to bylo
  neúplné, protože skript hledal jen **uvozovkové** `#include "x.h"`.
  `libui/src` a `libprim/src` (73 souborů, 169 řádků) uvnitř sebe používají
  **úhlové** `#include <ui/button.h>`, `#include <prim/fb.h>` — záměrný
  návrhový vzor (obě knihovny se includují, jako by byly externí/instalované,
  přesně jak to popisuje `CLAUDE.md`: „libprim nezná ui/*", „libui nezná
  app/*"). Úhlový include **nemá fallback** na „hledej nejdřív ve složce
  including souboru" (na rozdíl od uvozovkového) — je to čistě `-I`
  závislost, a je to 169 míst, ne 12.
- **Rozhodnutí (ne oprava):** relativní `#include` trik z L-0042 se sem
  **nepřenesl**. Přepsat 169 úhlových includů na relativní uvozovkové by
  bořilo záměrnou architekturu (dvě knihovny jako samostatné moduly) za
  problém, který se od jednorázového incidentu 2026-08-29 (šlo o mezeru
  v Release configu při prvním zavedení složek, ne o „regen to maže
  pořád") **neopakoval přes čtyři další regeny** (2026-09-01/06/12/13).
  Riziko refaktoru > riziko problému.
- **Oprava, která místo toho proběhla:** `check_regen()` v `scripts/build.sh`
  rozšířena o kontrolu přítomnosti všech čtyř `-I` cest v `CM7/.cproject`
  (`libprim/include`, `libui/include`, `${ProjName}/app`) — **detekce**, ne
  odstranění závislosti. Když se to příště přece jen ztratí, `build.sh` to
  nahlásí PŘED buildem, ne až jako záhadný `fatal error: ui/button.h`.
- **Pravidlo:** **Cena odstranění `-I` závislosti (relativní `#include`)
  škáluje s počtem míst, která na ní závisí, a úhlové includy tuhle cenu
  zvyšují — nemají fallback, takže je nejde postupně/částečně opravit.**
  Než se rozhodne mezi „odstranit závislost" (L-0042 vzor) a „jen ji
  hlídat" (`check_regen()`), spočítej **reálný** počet závislých míst
  (včetně `<...>` includů, ne jen `"..."`) A frekvenci opakování problému.
  Malý počet + opakující se incident → odstranit. Velký počet + jeden
  historický incident → hlídat, nepřepisovat záměrnou architekturu.
- **Detekce:** `check_regen()` (3 nové kontroly, ověřené kontrolovaným
  pokusem — zdravý strom mlčí, každá ze tří cest jednotlivě odstraněná
  z `.cproject` hlásí).
- **Commit:** (viz git log — commit bezprostředně po tomto zápisu)
- **Stav:** aktivní

### L-0043 — "Generate IRQ handler" vypnuto NESTAČÍ, když je handler mimo USER CODE

- **Datum:** 2026-09-13
- **Oblast:** build / dvoujádro (CubeMX regen, CM4 crash black-box)
- **Symptom:** V `.ioc` se u `NVIC2.HardFault_IRQn` (CM4) odškrtlo "Generate IRQ
  handler" (6. pole `false`) — stejná hodnota, jakou má CM7 už od 2026-08-16 a
  jejíž `naked` handler přežil minimálně tři dřívější regeny. Po reálném
  "Generate Code" v IDE ale CubeMX **celou funkci `HardFault_Handler` na CM4
  smazal** (i doxygen komentář nad ní) — přesně ta vada, které měl flag
  zabránit.
- **Příčina:** vypnutý "Generate IRQ handler" řekne CubeMX jen "nepiš přes
  tohle svůj stub" — NEŘÍKÁ "nesahej na obsah". Regen dál skenuje soubor,
  pozná svůj vlastní generovaný blok (doxygen `@brief This function handles
  Hard fault interrupt.` + jméno funkce) a když handler přestane být
  "potřebný" (flag vypnutý), blok **aktivně odstraní** jako už nepoužívaný —
  bez ohledu na to, že v něm mezitím byl náš `naked` kód. CM7ho handler tuhle
  osudu unikl ze zcela jiného důvodu: leží **uvnitř** `/* USER CODE BEGIN 1 */
  ... END 1 */` a nemá CubeMX doxygen komentář — regen do USER CODE obsahu
  nikdy nesahá, takže ho ani nerozpoznal jako "svůj" blok k úklidu.
- **Oprava:** `HardFault_Handler` na CM4 přesunut do stejného slotu jako na
  CM7 — definice do `USER CODE BEGIN 1`/`END 1` v `stm32h7xx_it.c`, prototyp
  do `USER CODE BEGIN EFP`/`END EFP` v `stm32h7xx_it.h` (regen smazal i
  prototyp, ne jen tělo). Ověřeno reálným "Generate Code" v IDE (ne
  syntetickým testem) + `./scripts/build.sh Release BOTH` (0 varování) +
  `nm` (`HardFault_Handler` je `T`, definovaný silný symbol, ne weak default).
- **Pravidlo:** **Regen-safe vlastní handler potřebuje OBĚ pojistky najednou:**
  (1) v `.ioc` vypnuté "Generate IRQ handler" (jinak regen přepíše obsah
  svým stubem) **a** (2) umístění celého kódu (tělo i prototyp) uvnitř
  `USER CODE` bloků (jinak regen i s vypnutým flagem svůj rozpoznaný blok
  odstraní). Jedna bez druhé nestačí — a chybu neodhalí nic než skutečný
  regen, protože obě chybové cesty (přepsání stubem / smazání) vypadají
  na první pohled jinak, ale obě mají stejný symptom: zmizelý kód.
- **Detekce:** `check_regen()` v `scripts/build.sh` (`grep -c naked` v
  `stm32h7xx_it.c`) — hlásí zmizení bez ohledu na to, KTERÝ z obou
  mechanismů selhal. Nově navíc kryté i tím, že kód leží v `USER CODE`
  (self-evidentní ochrana, ne jen kontrola po škodě).
- **Commit:** (viz git log — commit bezprostředně po tomto zápisu)
- **Stav:** aktivní

### L-0041 — `grep -c … || echo 0` v shellu dá "0\n0", ne "0"

- **Datum:** 2026-09-12
- **Oblast:** build / styl (shell skriptování)
- **Symptom:** `check_regen()` v `scripts/build.sh` měla dvě pozitivní kontroly
  (chybějící include cesty, chybějící `naked` handler) a OBĚ mlčely i na
  uměle rozbitém stromu — vypadalo to jako „kontrola je hotová a funguje",
  přitom nikdy nic nehlásila.
- **Příčina:** `grep -c PATTERN file` při **nulové shodě** vypíše `0` **a
  současně** skončí s návratovým kódem 1 (grep signalizuje "nic nenalezeno"
  přes exit code, ne přes prázdný výstup). `n="$(grep -c … || echo 0)"` proto
  při nulové shodě spustí OBĚ větve `||` — `grep` vypíše `0` na stdout, exit
  kód 1 spustí `echo 0`, a `$(...)` posbírá výstup obojího → `n` je řetězec
  `"0\n0"`. `[ "$n" -lt 2 ]` na takovém vstupu spadne na
  `integer expected`, `set -e` (respektive nezachycená chyba testu) kontrolu
  potichu přeskočí.
- **Oprava:** sdílená funkce `cnt()` (`scripts/build.sh`) — `grep -c … || true`
  (ne `|| echo 0`, aby se druhá větev nikdy nevypsala) + `case "$n" in
  ''|*[!0-9]*) n=0 ;; esac`, která cokoli, co není čistě číslo, převede na `0`.
- **Pravidlo:** **Nikdy `$(prikaz || echo NAHRADA)` u příkazu, který sám umí
  vypsat výstup i při neúspěchu** (`grep -c`, `wc -l` na neexistující soubor
  přes pipe apod.) — `||` nahradí až prázdný/chybějící výstup, ne výstup,
  který přišel spolu s nenulovým exit kódem. Bezpečný vzor: zachytit syrový
  výstup (`|| true`, aby `$(...)` neskončilo na chybě), pak ho **validovat**
  (regex na číslo), ne slepě věřit, že je to jedna hodnota.
- **Detekce:** kontrolovaný pokus s třemi větvemi (zdravý strom → ticho;
  odstraněný `naked` → hlásí; odstraněné include cesty → hlásí) —
  `scripts/build.sh` sám o sobě nemá jednotkové testy, takže jde o manuální
  ověření při každé úpravě `check_regen()`/`cnt()`. Obecný test na tuto třídu
  chyby: `n="$(grep -c nic /dev/null || echo 0)"; [ "$n" = "0" ]` musí projít
  (dřívější kód by na `/dev/null` dal `"0\n0"` a test by spadl).
- **Commit:** `1e9e211`
- **Stav:** aktivní

### L-0042 — Fragilní `-I` v `.cproject` šlo obejít, ne jen hlídat

- **Datum:** 2026-09-13
- **Oblast:** build / dvoujádro (CM7↔CM4 sdílené soubory)
- **Symptom:** `CM4/.cproject` má dvě `-I../../CM7/Core/Inc` položky (Debug+
  Release × C/C++ compiler), které CubeMX regenerace maže při KAŽDÉM běhu
  (je to XML mimo `USER CODE`, nezměnitelné). `check_regen()` (viz L-0041)
  to jen hlásila — oprava byla „`git checkout -- CM4/.cproject`" po každém
  regenu, navěky.
- **Příčina:** šest `#include "X.h"` řádků (`scpi.c`, `meas_math.c`,
  `ipc_scpi.c` v `CM7/Core/Src` — fyzicky sdílené soubory, linkované i do
  CM4 přes `CM4/.project`; a `main.c`, `httpd_min.c`, `scpi_tcp.c` v CM4
  samotném) psalo bare jméno hlavičky (`scpi.h`, `version.h`, `meas_math.h`,
  `ipc_shared.h`, `meas_present.h`) místo cesty — tím se řešení jména
  headeru odevzdalo kompilátorovému `-I` seznamu, tedy `.cproject`.
  Zbytek headerů, které tyto soubory potřebují (`meas_math.h`/`datalog.h`
  ze `scpi.h`), leží ve STEJNÉ složce jako `scpi.h` (`CM7/Core/Inc`) — ty se
  řeší samy, protože GCC quote-include vždy nejdřív zkusí složku
  *includujícího souboru*, a to bylo od začátku regen-safe.
- **Oprava:** těch šest `#include` se přepsalo na cestu relativní k
  **fyzickému umístění souboru na disku** (`#include "../Inc/scpi.h"` v
  `CM7/Core/Src/scpi.c`, `#include "../../../CM7/Core/Inc/scpi.h"` v
  `CM4/Core/Src/main.c` a `CM4/LWIP/App/*.c`) — přesně vzor, který `ipc_cm4.h`
  používal pro `ipc_shared.h` už dřív. GCC řeší quote-include vůči adresáři
  souboru, který `#include` napsal, ne vůči CWD ani `-I` — a ten adresář je
  pevný bez ohledu na to, které jádro soubor zrovna kompiluje (fyzická cesta
  na disku je jen jedna). `.cproject` se **nemusel měnit** — jeho `-I` cesta
  zůstala (harmless), ale žádný `#include` na ní už nezávisí.
- **Pravidlo:** **Sdílený/cross-adresářový header nikdy neincluduj bare
  jménem, jen relativní cestou k jeho fyzickému umístění** — tím se
  rozpoznávání souboru přestane opírat o build-systémový `-I` seznam, který
  generátor (CubeMX, ale stejně tak CMake/IDE reimport) může kdykoli
  přepsat. Bare `#include "x.h"` je bezpečné jen pro header ve STEJNÉ složce
  jako soubor, který ho includuje.
- **Detekce:** `tools/find_fragile_includes.py`-styl skript (scratchpad této
  session) — projde všechny soubory jednoho jádra, pro každý bare
  `#include "X.h"` zkontroluje, jestli `X.h` leží ve stejné složce jako
  including soubor; pokud ne a cesta není `../`-relativní, je to fragilní
  závislost na `-I`. Ověřeno i přímým kompilátorem: `arm-none-eabi-gcc
  -fsyntax-only` se **záměrně vyříznutou** `-I../../CM7/Core/Inc` (přímo v
  `CM4/Release/*/subdir.mk`, tedy artefaktu, který regen skutečně přepisuje)
  — po opravě prošlo všech šest souborů, `./scripts/build.sh Release BOTH`
  dal byte-přesně stejný `.elf` jako s tou `-I` cestou.
- **Commit:** (viz git log — commit bezprostředně po L-0041)
- **Stav:** aktivní

---

## Archiv (neplatné lekce)

*(prázdné)*
