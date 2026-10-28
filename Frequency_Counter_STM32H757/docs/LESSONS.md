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
| L-0012 | Když opravuješ jednu ze dvou symetrických instancí (I2C1/I2C4, CM7/CM4, FB0/FB1), v témže commitu dolož, že druhá je opravená nebo se jí to netýká. ⚠️ Sourozenec může být i **v témže souboru** (F-0091: `flightrec_init` mazala sektor 0, zatímco `errlog_init` o 300 řádků dál brala správně `(best_i+1) % N`) nebo **na druhém jádře** (F-0137: oprava F-0104 se nepřenesla z `watchdog.c` do `iwdg2.c`). | při opravě grep na sesterskou funkci; poznámka u obou kopií |
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
- 🔁 **První konkrétní incident 2026-09-18 (F-0113, modul 18).** `si5356_init`
  sledoval úspěch jen u hromadného zápisu register mapy, ale **apply proceduru**
  (OEB off → E2 pulse → SOFT_RESET → OEB on) volal bez kontroly návratu. Zvlášť
  poslední zápis „OEB_ALL = 0" zapíná 4× 100 MHz výstupy — jeho NACK by nechal
  hodiny vypnuté, FPGA bez časové základny, a init by přesto vrátil `OK`, protože
  status reg 218 (LOS_CLKIN/PLL_LOL) hlásí stav **vstupu a PLL**, ne výstupních
  bufferů. Opraveno akumulací návratu každého kroku do `ok` (`ok &= wr_masked(...)`).
  🔑 Vzor: **když návratová hodnota končí v `zapisy=OK`, musí ji plnit VŠECHNY
  kritické kroky, ne jen ten první čitelný.**
- **Commit:** `f128b59`, viz `docs/audit/2026-09-18_senzory-drivery.md`
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
- 🔁 **Opakovalo se 2026-09-29 v jiné podobě (F-0198, modul „optimalizace"):**
  `CLAUDE.md:711` tvrdilo „Největší CPU výhra zůstává -O2/Release", ale
  `.cproject` má Release na obou jádrech prokazatelně `-Os` — a zbytek téhož
  dokumentu to jinde (sekce „Build / flash") popisuje správně, s naměřenými
  čísly. Tentokrát to není komentář vs. zbytek funkce, ale **dokumentace vs.
  konfigurační soubor**, což je širší instance téhož vzoru: text tvrdí něco,
  co jde ověřit strojově (grep `.cproject`), a nikdo tu kontrolu neudělal.
  🔑 **Rozšíření pravidla: „ověř to, co jde ověřit" platí i pro tvrzení o
  BUILD KONFIGURACI, ne jen o chování kódu** — `.cproject`/`.ioc`/linker
  skript jsou zdroj pravdy stejně jako generovaný zbytek funkce.
- **Commit:** F-0198 viz `docs/audit/2026-09-29_optimalizace.md`
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
- 🔁 **Opakovalo se 2026-09-18 (F-0115 + F-0117, modul 19) — a v nové podobě.**
  Dvě části firmwaru se mají navzájem vyhýbat v SDRAM, a **každá měla vlastní seznam
  toho, co je cizí**: `membench` (`SDRAM_PROTECTED[]`) nevěděl o `.measlog`, který
  přibyl 2026-08-30, a `sdram_log` (`aliases_framebuffer`) nevěděl o `.sdram`
  (`bg_cache`). Ani jeden seznam nebyl „kopie" toho druhého — proto to nevypadalo
  jako duplicita a grep na sesterskou funkci by nepomohl.
  🔑 **Nová věta k pravidlu: u VZÁJEMNÉHO vyloučení jsou strany DVĚ a bývají
  v různých souborech.** Když A nesmí sáhnout na B, zeptej se rovnou, jestli B nesmí
  sáhnout na A — a jestli to ví. Symetrie tu není v kódu, ale v *požadavku*.
  🔑 **A druhá polovina: seznam „co je cizí" je duplikát mapy paměti, takže driftuje.**
  Kde linker exportuje symboly, ber adresu z nich (`_smeaslog`), ne natvrdo — pak se
  seznam nemůže rozejít potřetí. (`.sdram` symboly nemá, tam zůstala konstanta;
  doplnit je by znamenalo sáhnout na `.ld`, tedy pravidlo 6.)
- **Commit:** `caa08b4`, viz `docs/audit/2026-09-18_diagnostika-pameti.md`
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
- 🔁 **Doplněno 2026-09-19 (F-0132 + F-0138, modul 22) — a je to podstatné zpřísnění:**
  🔴 **Počítadlo, které nikdo nečte, tichý přeskok NEVYŘEŠÍ.** Opravoval jsem F-0132
  (zahozený TX paket bez záznamu) doplněním `g_eth_tx_err++` — a teprve při tom zjistil,
  že **`g_eth_tx_ok`/`g_eth_tx_err` nečte nikdo**, přestože komentář u nich tvrdil
  *„cte je CM7 pres IPC (`status`)"*. Ve snapshotu pro ně není pole a v `CM7/` na ně
  nikdo nesahá. Inkrement do neviditelného počítadla je tedy **poloviční oprava**, která
  navíc *vypadá* hotově.
  🔑 **Pravidlo se tím rozšiřuje na dvě části, obě povinné:**
  (1) tichý přeskok se **započítá**, a (2) to počítadlo je **dosažitelné bez ladicí
  sondy** — na tomhle projektu tedy přes `status`, IPC nebo web. Sonda se nepočítá:
  jeden halt zabije I2C4 do power-cyklu, takže „lze to přečíst sondou" znamená
  v praxi „nepřečte to nikdo".
  ⚠️ Druhá polovina se ověřuje stejně testovatelně jako L-0028: **najdi řádek, kde se
  to počítadlo ČTE.** Když neexistuje, není to diagnostika, ale mrtvá proměnná.
  (U F-0131 to naopak vyšlo dobře **bez** nového počítadla — selhání se projeví jako
  `NET: DOWN` místo falešného `NET: UP`, a `net_link` se publikuje už dnes. Nejlepší
  počítadlo je to, které není potřeba.)
  Commity `356fe02` (F-0132), `dd8ef75` (komentář F-0138).
  ✅ **Druhá část pravidla uzavřena 2026-09-19 (F-0138):** počítadla `g_eth_tx_ok`/
  `g_eth_tx_err` se publikují do IPC (`eth_tx_ok`/`eth_tx_err` v bloku `cm4`,
  recyklovaná vycpávka, `IPC_VERSION` 18) a UART `status` je tiskne na řádku
  `TX(CM4):`. Do té chvíle to byl **doložený případ přesně toho selhání, které
  tahle lekce popisuje**: inkrement existoval, čtenář ne, takže hodnota byla
  dosažitelná jen ladicí sondou — a ta za běhu zabíjí I2C4 do power-cyklu, tedy
  fakticky nedosažitelná. Navazuje **L-0059** (nula toho počítadla nese diagnózu,
  proto musí saturovat, ne přetékat).
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
- 🔁 **Opakovalo se 2026-09-18 (F-0052 + F-0096, moduly 11/16).** Zapis do
  `g_meas_cfg` byl na CTYRECH mistech: scpi.c a ipc.c ho commitovaly atomicky
  (lokalni kopie + kriticka sekce), okno MATH a `setup_load` psaly pole po poli
  bez ochrany -> roztrzena dvojice lo/hi = trvaly FAIL + alarm. Opraveno tak, ze
  vsechny UI cesty pouzivaji **identicky inline vzor** jako scpi/ipc.
  🔑 **Nuance k pravidlu „sluc do jedne funkce":** tady to NESLO — `g_meas_cfg`
  bydli v `meas_math.c`, ktery se linkuje i do CM4 obrazu, a CM4 je bare-metal
  **bez FreeRTOS**, takze sdileny helper s `taskENTER_CRITICAL` tam nemuze
  existovat. Kdyz hranice jazyku/jader brani jedne funkci, disciplina se meni na:
  **identicky inline vzor VSUDE + poznamka, proc se nesjednotilo.** Ne ctyri ruzne
  varianty, ale jeden vzor rozepsany, protoze ho nelze zabalit. Commit `2253ac4`,
  viz `docs/audit/2026-09-11_aplikacni-okna.md` (F-0052) a
  `docs/audit/2026-09-16_perzistence-zaznamniky.md` (F-0096).
- 🔁 **Opakovalo se POTRETI 2026-09-28 (F-0197, druhe kolo modulu 24).**
  `syscfg_load()` (`syscfg.c`) je **pate** misto zapisujici `g_meas_cfg` —
  strukturalne stejna funkce jako `setup_load()` (nacte perzistovany
  math/limit blok a aplikuje ho na `g_meas_cfg`), volana ze stejneho tasku,
  a presto do sweepu 2026-09-18 nespadla. Pravidlo „po pridani lekce zkontroluj
  zbytek projektu na stejny vzor" (`docs/templates/LESSON.md` bod 5) se tehdy
  aplikovalo jen na MISTA ZNAMA V TU CHVILI (ctyri), ne na VSECHNA mista se
  stejnym UCELEM — `syscfg_load` a `setup_load` delaji totez (persist->g_meas_cfg
  po power-cyklu/nacteni profilu) ale zily ve dvou ruznych souborech, takze
  textove hledani „stejny vzor" na ne nemuselo narazit soucasne. Opraveno
  `docs/audit/2026-09-28_matematika-druhe-kolo.md`, overeno na HW
  (`SELFTEST: 16/16 PASS` po opravenem `syscfg_load`).
  🔑 **Dodatek k pravidlu:** kontrola „stejny vzor" po pridani lekce nestaci
  hledat DUPLICITNI KOD — musi hledat i FUNKCE SE STEJNYM UCELEM v jinych
  souborech (zde: „kdo vsechno perzistuje/nacita g_meas_cfg", ne jen „kdo ma
  stejny zdrojovy radek").
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
- 🔁 **Opakovalo se 2026-09-19 (F-0127, modul 21) — s jinou cizi knihovnou.**
  `usb_console_tx_pump()` uvolnil slot kruhoveho bufferu ve chvili, kdy
  `CDC_Transmit_FS` vratil `USBD_OK`. Jenze cela cesta CDC je **zero-copy**:
  `USBD_CDC_SetTxBuffer` si ulozi jen ukazatel, `USBD_CDC_TransmitPacket` vrati
  `USBD_OK` **pred prenosem** a pri `dma_enable = DISABLE` plni FIFO az obsluha
  preruseni USB — primo z naseho bufferu. „Prijato k odeslani" tedy neznamena
  „uz to nepotrebuju"; producent prepisoval vysilana data.
  🔑 **Zobecneni teto lekce: navratova hodnota „OK" od cizi knihovny rika, ze
  ZADANI bylo prijato, ne ze PRACE skoncila.** Vlastnictvi bufferu se vraci az
  dokoncenim — a to je jiny okamzik.
  🔴 **A druha polovina, ktera je proti intuici:** uvolneni navazane na
  **dokoncovaci callback** (`CDC_TransmitCplt_FS`) je „presnejsi", ale samo o
  sobe **horsi** — kdyz se host odpoji uprostred prenosu, callback nikdy
  neprijde, drzeny blok se neuvolni a konzole se jevi trvale plna, tedy vada
  **zavaznejsi nez ta puvodni**. Zvolena varianta uvolnuje az pri PRISTIM
  uspesnem zadani (`USBD_OK` dokazuje, ze predchozi dojelo) a tim **se hoji
  sama**. **Kdyz stavis uvolneni na cizi notifikaci, zeptej se, co kdyz
  neprijde** — a preferuj konstrukci, ktera se zotavi bez ni.
  ⚠️ Vedlejsi dusledek, se kterym je nutne pocitat: politika „pri plnem bufferu
  zahod nejstarsi" **prestane byt pouzitelna**, protoze nejstarsi je prave to
  letici. Zahazuje se prichozi — a tim zahazovani zhoustne, takze pocitadlo
  (L-0017) uz neni kosmetika. Commit `f4f4ebf`, viz
  `docs/audit/2026-09-19_bridge-ipcscpi-usbcdc.md`, F-0127 + F-0128.
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
- 🔁 **Opakovalo se 2026-09-16 (F-0097, modul 16).** Blob store nad W25Q ma strop
  `W25Q_STORE_MAX_BLOB` = 4080 B a **ani jeden ze tri blobu** (`syscfg_blob_t`,
  `setup_book_t`, `calib_blob_t`) ho nemel hlidany prekladem — mez se kontroluje
  az za behu a `w25q_store_write` vrati `false` **tise**. `syscfg_save` by pak
  vracel `false` navzdy, `syscfg_flash_tick` by to zkousel 100x/s a nastaveni by
  se prestalo ukladat, aniz by `status` rekl cokoli; projevilo by se to jako
  „nastaveni neprezije power-cyklus", tedy symptom hledany uplne jinde.
  Syscfg blob pritom vyrostl uz nejmene dvanactkrat (historie magicu
  `"SCFG"` -> `"SCG1"`). Doplneny tri `_Static_assert` (commit `e583432`),
  kazdy overeny pozitivni kontrolou (docasne nafouknuti struktury o 4096 B ->
  preklad skutecne spadne).
  🔑 **Poucen z toho je i dosah lekce:** puvodne byla o JEDNE odpovedi v `httpd_min.c`;
  pravidlo ale plati pro **kazdy pevny strop, ktery se kontroluje az za behu** —
  a ten se hleda tak, ze se u kazdeho `MAX`/`CAP` makra zepta, kdo ho vynucuje.
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
- 🔁 **Opakovalo se 2026-09-18 (F-0114, modul 18).** `ws_panel_set_backlight`
  i `ws_panel_set_portc` měly v komentáři slib „ŽÁDNÝ printf zde — volá to hlídaný
  UiTask", ale oba volaly sdílený `ws_write_reg`, který na chybové cestě `printf`
  dělá. Slib byl tedy nepravdivý přesně jako v původním nálezu — jen naopak
  (komentář tvrdil, že se něco NEDĚJE, a ono se to dělo). Při selhání zápisu jasu
  na umírající I2C4 by z UiTasku běžel printf, a na místě volání navíc pod
  `vTaskSuspendAll` (systémový stall na USART cestě). Opraveno sjednocením do
  `ws_write_reg_ex(…, log)` — jeden zdroj pravdy pro přenos, settery volají
  s `log = false` (L-0018). Komentáře uvedeny na pravdu.
  🔑 Pravidlo platí **oběma směry**: slib „hlídá to X" i slib „X se tu neděje" je
  testovatelný — najdi řádek (ne)dělající X. Sdílený helper může slib tiše porušit.
  Commit `662e049`, viz `docs/audit/2026-09-18_senzory-drivery.md`, F-0114.
- 🔁 **2026-10-03 (F-0227):** `gps glonass` vypisoval „UBX-CFG-GNSS odeslano" bez ohledu na
  výsledek. `gps_config_gnss()` byla `void` a `ubx_send` po `12ec7ac` umí selhat. Opraveno
  `7219e0a` (návratová hodnota až do hlášky). Hláška o úspěchu je taky slib, který musí
  mít řádek, kde se úspěch čte.

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

### L-0057 — Diagnostika, která porušuje invariant produkčního kódu, se nemusí rušit — stačí ji ODMÍTNOUT tam, kde by kolidovala

- **Datum:** 2026-09-19
- **Oblast:** mezijádrové IPC / diagnostické příkazy konzole
- **Symptom:** `cmd` a `resp` jsou bezzámkové **SPSC** ringy — jejich správnost stojí
  na tom, že `cmd` má jediného producenta (CM4) a `resp` jediného konzumenta (CM4).
  Dva diagnostické příkazy na CM7 to porušovaly: `ipccmd` do `cmd` pushoval a z `resp`
  popoval (F-0014), a totéž dělala `ipc_scpi_set_cfg` napojená na `scpi ipc` (F-0129).
  Při souběhu se zápisem z webu by oba producenti přečetli tentýž `head`, zapsali do
  **téhož slotu** a oba ho zvedli → jeden příkaz se **tiše ztratí**, druhý přenese
  poškozený. Ringy ztrátu nedetekují.
- **Příčina:** Nález F-0014 navrhoval opravu *„na CM7 má příkaz volat rovnou
  `ipc_cfg_apply()`, protože je na tomtéž jádře a ring nepotřebuje"*. U `scpi ipc` to
  platilo, ale **u `ipccmd` by to příkaz zrušilo**: jeho účel je v kódu napsaný —
  *„pošli příkaz PŘESNĚ tou cestou, kterou použije CM4 … ověřit ovládací cestu
  CM4→CM7 bez sítě, bez SCPI a bez webu"* (kritérium W1). Oprava podle návrhu by
  odstranila právě to, kvůli čemu nástroj existuje.
- **Oprava — tři různé zásahy pro tři různé situace, ne jeden vzor na všechno:**
  1. **`scpi ipc` (F-0129):** ring **není potřeba** — nástroj srovnává *odpovědi*.
     Soubor `ipc_scpi.c` se kompiluje **dvakrát** (jednou per jádro), takže stačila
     jádrová podmínka `#if defined(CORE_CM4)`. Zůstala **jedna** funkce, validace se
     neduplikovala (`L-0018`). Doloženo velikostí: `ipc_scpi_set_cfg` má na CM7 128 B,
     na CM4 548 B, a **CM4 `.text` je bajt za bajtem shodný**.
  2. **`ipccmd` (F-0014):** ring **je potřeba** (to je celý účel) → příkaz se
     **ODMÍTNE**, když může existovat druhý producent: `ipc_cm4_alive() && g_web_ctrl_en`.
     Únikový východ `ipccmd force <…>`.
  3. Predikát je **záměrně úzký**: bez `g_web_ctrl_en` dostane SCPI na CM4
     `set_cfg = NULL`, takže druhý producent vůbec nevznikne a guard nezasahuje —
     tedy přesně ve stavu, pro který `ipccmd` vznikl (bez sítě, bez webu).
- **Pravidlo:** **Když diagnostický nástroj porušuje invariant, na kterém stojí
  produkční kód, nejdřív se zeptej, JESTLI ten invariant potřebuje porušovat.**
  - Nepotřebuje → nech ho pracovat nad lokální kopií a produkční cestu nechte být.
  - Potřebuje (porušení JE ten test) → **neruš nástroj, odmítni jeho spuštění ve
    stavu, kdy by kolidoval** — a nech únikový východ (`force`) pro toho, kdo ví,
    že kolize nehrozí.
  🔑 **Odmítnutí je plnohodnotná oprava.** „Nástroj to musí umět, takže tu vadu
  musíme snést" je falešné dilema: většina diagnostik se používá v řízeném stavu,
  ve kterém kolize nehrozí — stačí ten stav vynutit.
  ⚠️ A ověř účel nástroje **z kódu, ne z názvu**: návrh opravy v F-0014 byl napsaný,
  aniž by se citoval komentář, který u `ipccmd` ten účel vysvětluje.
- **Detekce:** Projekt už tenhle vzor jednou použil — příkaz `eth` odmítne bit-bang
  SMI, když ETH obsluhuje CM4 (*„dva masteři na MDIO"*). Hledej to takhle: u každého
  diagnostického příkazu, který sahá na zdroj vlastněný druhým jádrem nebo jinou
  úlohou, musí být buď odmítnutí, nebo napsané, proč kolize nehrozí.
- **Commit:** `7cd8613` (F-0129) a tento (F-0014), viz
  `docs/audit/2026-09-19_bridge-ipcscpi-usbcdc.md` a `docs/audit/2026-09-09_ipc-cm7-cm4.md`
- **Stav:** aktivní

### L-0056 — Diagnostiku poruchy zapisuj podle NÁSLEDKU, ne podle detekce

- **Datum:** 2026-09-17
- **Oblast:** watchdog / crash black-box
- **Symptom:** `watchdog_supervise()` psal do crash black-boxu `stall:UiTask`
  ve chvíli, kdy **odmítl obnovit IWDG** — tedy při *detekci*. Když se úloha
  do ~1,5 s vzpamatovala, reset **nepřišel**, ale záznam v BKP zůstal ležet
  a smazal ho až `MX_RTC_Init` při příštím bootu. `status` ho pak připsal
  resetu, který s ním neměl nic společného — klidně `power-on` za dva dny,
  protože doména je zálohovaná z CR2032.
- **Příčina:** Zápis se pověsil na **rozhodnutí** („nekrmím watchdog"), ne na
  **následek** („watchdog mě skutečně resetoval"). Mezi tím je okno ~1,5 s,
  ve kterém se stav může vrátit do normálu. Navíc `s_stall_logged` se nikdy
  nenulovalo, takže druhý — a možná skutečně fatální — stall **jiné** úlohy se
  už nezapsal a po resetu se hlásil ten první.
- 🔑 **Proč to nejde „prostě psát až při resetu":** v okamžiku resetu už kód
  neběží. Záznam **musí** vzniknout dopředu. Řešení tedy není přesunout zápis,
  ale **umět ho vzít zpět**, když následek nenastal.
- **Oprava:** při návratu obou heartbeatů se záznam **zneplatní** (`RTC->BKP3R = 0`)
  a modul se znovu armuje. ⚠️ Zneplatňuje se **jen když je pořád náš**
  (magic + `kind == 3`) — mezitím ho mohl přepsat HardFault nebo `configASSERT`
  a ten patří někomu jinému (táž disciplína jako **L-0025**). Ztracený údaj se
  nezahazuje tiše: `watchdog_stall_recovered()` ho počítá a hlásí `status`.
- **Pravidlo:** **Když musíš diagnostiku zapsat dřív, než víš, jestli k následku
  dojde, doplň k ní cestu, jak ji vzít zpět — a ověř, že rušíš vlastní záznam,
  ne cizí.** Jinak se z „co se stalo" stane „co se skoro stalo", přiřazené
  k náhodné pozdější události. A stav, který jsi zrušil, musí zůstat měřitelný.
- **Detekce:** u každého zápisu do trvalého úložiště, který předchází očekávané
  poruše, se ptej: *co když k té poruše nedojde?* `status` →
  `WATCHDOG: … ` a řádek `zotavenych stallu`.
- **Commit:** `76bf1f6`, viz `docs/audit/2026-09-17_cas-alarmy-watchdog.md`, nález F-0105
- **Stav:** aktivní

### L-0055 — Hodnotu, kterou HW nemusí přijmout, po zápisu PŘEČTI zpátky a zveřejni

- **Datum:** 2026-09-17
- **Oblast:** watchdog / inicializace periferií
- **Symptom:** `watchdog_init()` čekal na propagaci `PR`/`RLR` **ohraničenou**
  smyčkou (což je správně), ale její výsledek zahodil a nastavil `s_ready = 1`
  bezpodmínečně. Kdyby se `SR` nevyprázdnil, zůstaly by v platnosti reset
  defaulty `PR = 0` (/4) a `RLR = 0xFFF` → timeout **~0,51 s místo 4,0 s**,
  tedy osmkrát kratší — a tiše.
- **Proč to není teoretické:** defaultTask smí v jedné iteraci dělat
  `syscfg_save()` → `w25q_store_write()` → **erase sektoru 50–400 ms**.
  Projev by byl „náhodný reset při ukládání nastavení", tedy symptom, který se
  hledá kdekoli jinde než v inicializaci watchdogu.
- **Oprava:** `PR`/`RLR` se po propagaci **odečtou z registrů** a `status` je
  vypisuje i s odvozeným timeoutem a značkou `<== NESEDI`. Drží se **naměřený**
  stav, ne zamýšlený.
  ⚠️ **Vědomě se NEOPRAVUJE retry ani `Error_Handler`em:** IWDG už běží (START
  je neodvolatelný), takže spadnout kvůli tomu do `Error_Handler` by
  z nepohodlí udělalo nefunkčnost. Zveřejnit stav stačí.
- **Pravidlo:** **Ohraničená čekací smyčka je jen polovina práce — druhá je
  přečíst, co v registru doopravdy zůstalo, a vystavit to.** Platí všude, kde
  HW zápis potvrzuje vlastním příznakem (`PVU`/`RVU`, `VOSRDY`, `RECALPF`,
  `DBP`): mez chrání před zatuhnutím, ale sama o sobě nezaručuje, že se hodnota
  uplatnila.
- **Detekce:** ke každé smyčce `while/for (… && REG & FLAG)` dopiš, co se stane
  při vypršení — a když je odpovědí „degraduje se tiše", patří dosažený stav
  do `status`. Rozšíření **L-0009** z generovaného kódu i na vlastní.
- **Commit:** `76bf1f6`, viz `docs/audit/2026-09-17_cas-alarmy-watchdog.md`, nález F-0104
- **Stav:** aktivní

### L-0054 — „Jeden vlastník" je tvrzení o VŠECH volajících, ne o tom hlavním

- **Datum:** 2026-09-17
- **Oblast:** souběh mezi úlohami / zvuková cesta
- **Symptom:** `alarm.c` mělo v komentáři napsaný návrh *„jeden vlastník pattern
  stavu = defaultTask → žádný cross-task zápis do `s_phase`"*. `alarm_click()`
  (UiTask) ho poslušně dodržoval přes flag — a přitom **dva jiní zapisovatelé
  ho porušovali**: `alarm_test()` volal `pattern_start()` přímo z UartTasku
  a `beeper_boot_melody()` psala `s_on`, `TIM7->ARR` i `CNT` z UiTasku.
  Žádná z proměnných stavu nebyla `volatile`.
- **Příčina:** Pravidlo se zapsalo **u té cesty, která ho dodržuje**, místo aby
  se ověřilo u všech. Komentář tak popisoval *záměr*, ne skutečnost — a působil
  jako důkaz, že je věc vyřešená. Kdo četl `alarm_click`, viděl vzorovou
  implementaci a neměl důvod hledat dál.
- 🔑 **Proč je zrovna u „stavové proměnné periferie" následek nepříjemný:**
  `beeper_set()` začíná `if (on == s_on) return;`. Ztracený zápis do `s_on` ho
  rozejde se skutečným stavem TIM7, a ta horší polovina je tichá: TIM7 běží,
  ale `s_on == false` → `beeper_set(false)` se vrátí na první řádce a **pípák
  troubí souvisle**.
- **Oprava:** `alarm_test()` nastavuje jen požadavek (stejný vzor jako
  `alarm_click`); boot melodie je s `alarm_tick` **vzájemně vyloučená**
  (`beeper_melody_busy()`). Mimo to okno je defaultTask jediný zapisovatel.
  ⚠️ **Není to zámek** a je to u toho napsané: když je defaultTask už uvnitř
  `pattern_service`, jeden tón se může uříznout. Přesun melodie do defaultTasku
  by okno uzavřel úplně, ale sahal by na časování startu (CLAUDE.md 4c).
- **Pravidlo:** **Větu „stav vlastní úloha X" ověř VÝČTEM volajících, ne
  u jedné cesty.** `grep` na každou funkci, která ten stav mění, a u každého
  volajícího urči úlohu. Dokud ten výčet není v hlavičce, je „jeden vlastník"
  jen přání.
- **Detekce:** u každého `static` stavu s komentářem o vlastnictví musí být
  seznam volajících a jejich úloh (rozšíření **L-0022** a **L-0023**).
  Konkrétně: `grep -n "pattern_start\|beeper_set\|beeper_tone" CM7 -r` a ověřit,
  že mimo `alarm_tick`/`beeper_boot_melody` nikdo jiný nevolá.
- **Commit:** `68ae8c8`, viz `docs/audit/2026-09-17_cas-alarmy-watchdog.md`, nález F-0103
- **Stav:** aktivní

### L-0053 — Záznam o provedené akci vydávej až podle jejího VÝSLEDKU, ne na jejím začátku

- **Datum:** 2026-09-17
- **Oblast:** trvalý záznamník chyb / perzistence
- **Symptom:** Dvě nezávislá místa tvrdila, že se něco povedlo, aniž by to věděla:
  (a) `calib_save()` mělo `errlog_put(… ERRLOG_CFG_CALIB …)` jako **první příkaz
  funkce** — tedy před kontrolou `s_store.ready`, před získáním mutexu i před
  zápisem. Na všech třech chybových cestách zůstala v trvalé historii věta
  *„uložena kalibrace napětí"* o změně, která se neprovedla.
  (b) `errlog_erase()` zahazovala výsledek všech 64 `w25q_erase_sector` přes
  `(void)`, bezpodmínečně nastavila `s_el_ready = 1` a oba volající hlásili
  *„smazáno"*.
- **Proč na tom záleží:** (a) podle záznamu o změně kalibrace se později vysvětluje
  skok v naměřených datech — falešný záznam pošle analýzu hledat příčinu jinam.
  (b) po nedokončeném mazání se hlava vrátí na začátek regionu, zatímco ve zbytku
  zůstanou **starší záznamy s vyšším `seq`**, které příští `errlog_init()` najde
  jako „nejnovější" a ustaví hlavu na špatném místě.
- 🔑 **Obojí mělo v témže projektu hotovou předlohu, jen o pár souborů dál.**
  `datalog_erase_all()` návratové hodnoty kontroluje a stav posouvá jen při `ok`;
  UART `flightrec test` má v komentáři přímo *„Hlas vysledek, ne zamer"*. Je to
  `L-0012` v čisté podobě: dvě symetrické instance, z nichž jedna zaostala.
- **Oprava:** `errlog_put` v `calib_save` až za zápis a podmíněné úspěchem;
  `errlog_erase()` vrací `bool`, při chybě přeruší smyčku a stav **neposouvá**,
  oba volající hlásí výsledek (`"smazáno"` / `"SELHALO (log zůstává)"`).
  ⚠️ Neúspěšný zápis patří pod `ERRLOG_K_STORAGE`, ne pod „nastavení se změnilo".
- **Pravidlo:** **Záznam o akci patří ZA akci a pod její návratovou hodnotu.**
  Když se hlásí na začátku, není to záznam o tom, co se stalo, ale o tom, co se
  zamýšlelo — a to je horší než mlčet, protože to zní stejně důvěryhodně.
  Totéž platí pro hlášku volajícímu: hlas výsledek, ne záměr.
- **Detekce:** `grep -n "errlog_put\|(void)w25q_\|(void)f_" CM7/Core/Src/*.c` — u
  každého výskytu ověř, že je ZA operací, o které mluví, a že je na ní podmíněný.
  Obecně: první příkaz funkce, který něco zaznamenává, je vždy podezřelý.
- **Commit:** `15aa8d3` (F-0094), `a9af9de` (F-0099), viz
  `docs/audit/2026-09-16_perzistence-zaznamniky.md`
- **Stav:** aktivní

### L-0052 — Obnova uloženého stavu není změna stavu: setter s vedlejším účinkem potřebuje tichou variantu

- **Datum:** 2026-09-17
- **Oblast:** perzistence / trvalý záznamník chyb
- **Symptom:** Po **každém studeném startu** se do trvalé historie chyb zapsala věta
  `NASTAV  interval logu 10s -> 60s`, ačkoli uživatel nic nezměnil. Navíc se tím
  zkreslilo počítadlo `repeat` u příští **skutečné** změny (druhé volání padlo do
  rate-limitu, který je na DRUH, ne na podtyp, a jen inkrementovalo `s_el_pending`).
- **Příčina:** `datalog_set_period_s()` / `datalog_set_store()` logovaly **každou**
  změnu hodnoty. `syscfg_load()` je při bootu volá, aby obnovil uložené nastavení —
  a protože statiky startují na výchozích hodnotách (10 s / AUTO), setter to
  vyhodnotil jako změnu. Účel `ERRLOG_K_CFG` je přitom v `errlog.h` výslovně
  **odlišit zásah uživatele od HW události**, aby skok ve statistice nevypadal
  jako porucha — falešný záznam dělá přesně opak.
- 🔑 **Proč to není jen kosmetika:** je to trvalá historie, podle které se později
  rozhoduje, čím byl způsobený skok v měření. Záznam, který lže o tom, že zásah
  proběhl, pošle analýzu stejně špatným směrem jako chybějící záznam — jen
  sebejistěji (tatáž třída jako `L-0026`: „tichý ořez si vždy najde někoho
  nevinného, koho obvinit").
- **Oprava:** `datalog_cfg_quiet(bool)` potlačí záznam po dobu obnovy; uživatelské
  cesty (UART `datalog`, okno Datalog přes `qspi_req_service`) zůstávají hlasité.
- **Pravidlo:** **Když setter kromě nastavení hodnoty něco HLÁSÍ (log, alarm,
  notifikace), ptej se, kdo ho ještě volá — obnova uloženého stavu při bootu
  není zásah uživatele a hlásit se nesmí.** Buď dej setteru tichou variantu, nebo
  obnovu veď mimo něj; „nastav" a „nastav a oznam" jsou dvě různé operace.
- **Detekce:** u každého setteru, který volá `errlog_put`/`alarm_*`/`printf`,
  vyjmenuj v komentáři volající a u každého uveď, jestli má být hlasitý.
  Křížem: `grep -n "errlog_put" CM7/Core/Src/*.c` a u každého výskytu ověřit,
  že se na tu cestu nedá dostat z `syscfg_load()`.
- **Commit:** `6d1b6e5` (spolu s L-0050), viz `docs/audit/2026-09-16_perzistence-zaznamniky.md`, nález F-0093
- **Stav:** aktivní

### L-0051 — Když platnost ukazatele hlídá samostatný příznak, pořadí zápisu je invariant

- **Datum:** 2026-09-17
- **Oblast:** souběh mezi úlohami / perzistence
- **Symptom:** `datalog_init()` (re-init při přepnutí úložiště, běží v UartTasku)
  nuloval `s_be = NULL; s_ready = false;` **v tomto pořadí**. Mezi těmi dvěma
  příkazy platilo `s_ready == 1 && s_be == NULL`, takže souběžný čtenář
  (`datalog_get_status` z UiTasku, 2×/s) dereferencoval NULL → HardFault.
- 🔴 **Komentář nad tím tvrdil, že je čtenáři chrání mutex** — jenže **žádný ze
  čtyř čtenářů QSPI mutex nebere** (a záměrně: drží ho i minuty trvající
  `datalog_erase_all`). Popsaná ochrana neexistovala, a skutečný následek nebyl
  „hlásil NEDOSTUPNE", jak komentář předpokládal, ale pád. Už potřetí v projektu
  komentář popisoval obranu, kterou kód nedělá (`L-0028`).
- 🔑 **Reachability byla těsnější, než jak to vypadá:** tlačítko na přepnutí
  úložiště **je v okně Datalog** a totéž okno volá `datalog_get_status()` v každém
  tiku — ty dvě úlohy tedy běží současně právě kvůli tomu jednomu stisku.
  „Okno je jen dvě instrukce" není argument, když ho obě strany otevírají naráz.
- **Oprava:** (1) pořadí obráceno a obě hranice oddělené `__DMB()` (díky `"memory"`
  clobberu je to bariéra i pro překladač); na konci initu opačné pořadí —
  `s_be` a hlava hotové **dřív**, než `s_ready` pustí čtenáře dovnitř.
  (2) Čtenáři si ukazatel čtou **jednou do lokálu** a testují ho vedle příznaku;
  zastaralý ukazatel je bezpečný (backendy mají statickou dobu života), NULL nebyl.
- **Pravidlo:** **Dvojice „ukazatel + příznak platnosti" se zapisuje v opačném
  pořadí při zapnutí a při vypnutí — příznak se shazuje PRVNÍ a zvedá POSLEDNÍ —
  a mezi ně patří bariéra.** Čtenář, který ukazatel dereferencuje, si ho musí
  přečíst jednou do lokálu; opakovaný dotaz uprostřed funkce už může vidět jiný stav.
- **Detekce:** najdi každou dvojici `X = NULL` / `X_ready = false` v jednom bloku
  a ověř pořadí. Obecně: u každého „guard flag + pointer" se ptej, co vidí čtenář
  MEZI těmi dvěma zápisy.
- **Commit:** `a80caed`, viz `docs/audit/2026-09-16_perzistence-zaznamniky.md`, nález F-0090
- **Stav:** aktivní

### L-0050 — Nové pole v perzistované struktuře patří do TÉ poloviny obnovy, kde jeho hodnota ještě žije

- **Datum:** 2026-09-17
- **Oblast:** perzistence nastavení / chování po resetu
- **Symptom:** Nastavení datalogu (zap/vyp, úložiště, perioda) **nepřežilo teplý
  reset**. Po reflashi, po Menu → Restart, po watchdogu i po NRST se vrátilo na
  výchozí `ON` / `AUTO` / `10 s`. Uživatel, který záznam vypnul, ho měl po
  restartu zase zapnutý a psalo se do flash.
  🔴 **Přes power-cyklus to fungovalo**, takže to vypadalo jako náhoda — a právě
  proto to zůstalo deset dní neviditelné.
- **Příčina:** `syscfg_load()` je rozdělená na dvě poloviny. Nad `return` patří
  pole, která **nejsou v BKP** a flash je jejich jediný zdroj; pod `return` pole,
  která BKP drží a při teplém resetu z ní přijdou novější. Tři pole datalogu,
  přidaná 2026-09-07 (magic `"SCG0"` → `"SCG1"`), skončila **pod** `return` —
  přestože v BKP nejsou. Zařadila se prostě tam, kam se dopsala nejsnáz.
- 🔑 **Soubor to pravidlo měl napsané a stejně se porušilo.** Komentář nad tou
  polovinou říká *„Čteme VŽDY (i warm reset): `g_fx_enabled` NENÍ v BKP, flash je
  jeho jediný zdroj"* — jenže je to pravidlo o **kategorii**, ne o konkrétním poli,
  takže ho při přidávání nikdo nemusel spojit s tím, co zrovna píše.
- **Oprava:** tři řádky přesunuty nad `return`, k ostatním polím, která BKP nedrží
  (fx, meas, survey, monitor, layout, enc_div). Pořadí perioda → úložiště zachováno.
  ⚠️ **Muselo se opravovat spolu s `L-0052`** — bez toho by se falešné záznamy
  o „změně nastavení" rozšířily ze studeného startu na každý reset.
- **Pravidlo:** **Když je obnova stavu rozdělená podle toho, kde ta hodnota ještě
  žije (záložní doména / flash / nikde), zařaď nové pole VÝČTEM toho druhého
  zdroje, ne dojmem.** U perzistence vždy ověř obě cesty resetu zvlášť: studený
  start a teplý reset jsou dva různé stavy a feature může fungovat jen v jednom.
- **Detekce:** `scripts/check_lessons.sh` — sekce „pod `if (g_syscfg_bkp_valid)
  return;`". Kontrola je **rozdílová**: seznam „co drží BKP" se nevypisuje ručně,
  odvozuje se z toho, co `rtc.c` z BKP doopravdy obnovuje. Hlásí jak volání funkce
  pod returnem, tak globál, který `rtc.c` nezná. Ověřeno pozitivní kontrolou na
  obou podobách vady (`L-0039`).
- **Commit:** `6d1b6e5` + `165023c` (kontrola), viz
  `docs/audit/2026-09-16_perzistence-zaznamniky.md`, nálezy F-0089 a F-0093
- **Stav:** aktivní

### L-0049 — Rozlušti binární `a`/`b` do věty JEDNOU, na zdroji, ne u každého konzumenta

- **Datum:** 2026-09-13
- **Oblast:** `errlog.h`/`flightrec.c` (trvalý zaznamník chyb), IPC protokol, web SPA
- **Symptom:** uživatel nahlásil okno CHYBY jako "neprehledne". Skutečná vada
  nebyla v layoutu — byla v OBSAHU: okno tisklo `a`/`b` (dvě `uint32_t` z
  `errlog_put`) jako holá čísla `"12345/6789"` bez popisku. Význam těch dvou
  čísel se přitom **liší podle `kind`** (u `ERRLOG_K_UART` je to ORE/(FE|NE<<8|
  PE<<16), u `ERRLOG_K_CFG` je to nová/stará hodnota nastavení, u `ERRLOG_K_REF`
  sticky bity Si5356…) — čitelný byl jen tomu, kdo šel číst zdrojový kód
  volajícího `errlog_put`.
- **Příčina/nález:** dekódovací znalost ("co `a`/`b` u tohoto `kind` znamenají")
  neměla ŽÁDNÉ centrální místo — byla rozptýlená po jedenácti call-sitech
  `errlog_put` v šesti různých souborech. Když přišel druhý konzument (web,
  na žádost "muzes ho pridat i do webu?"), měl dvě možnosti: duplikovat tu
  znalost podruhé v JS (a časem se s displejem rozejít — přesně to varuje
  `sens_valid`/`IPC_CFG_*` sekce v CLAUDE.md), nebo poslat surová čísla a nechat
  prohlížeč hádat totéž, co uhodnout nešlo ani na displeji.
- **Řešení:** `errlog_fmt_detail(const errlog_rec_t *r, char *buf, size_t n)`
  (`flightrec.c`, deklarace `errlog.h`) je JEDINÉ místo, které zná mapování
  `kind`→význam `a`/`b`/`sub`. Displej (`app_gpsdo_render_errlog`) i nový IPC
  kanál `ipc_errlog_xfer_t` (`ipc_errlog_service`, v17) volají tutéž funkci;
  web (`build_errlog_json`, SPA) dostane už HOTOVOU větu (`"sbernice I2C4, chyb=
  12, resetu touche=3"`) a nemusí znát nic o `kind` kromě toho, jakou barvu si
  k němu domyslet (CRASH=červená, BOOT/CFG=ztlumené, jinak amber — tahle
  trojice je jediná věc, která se v SPA zopakovala, protože je to jen barva,
  ne sémantika).
- **Proč to nebylo vidět dřív:** okno CHYBY existovalo přes rok bez stížnosti —
  vývojář, který ho psal, zdrojový kód `errlog_put` zná zpaměti, takže mu
  "12345/6789" dávalo smysl. Teprve pohled uživatele, který zdrojový kód nečte,
  odhalil, že displej mluvil jazykem implementace, ne jazykem události.
- **Pravidlo:** Když se strukturovaná binární data (kód důvodu, bitová maska,
  pár čísel s významem závislým na typu záznamu) zobrazují na VÍCE než jednom
  místě (displej, log, web, UART výpis…), dekódovací funkce patří K DATŮM
  (headeru/implementaci, co je vytváří), ne ke každému zobrazovači zvlášť.
  Nový konzument tím dostane čitelnost zdarma a nemůže se se starým rozejít.
- **Detekce:** žádná automatická — nález vznikl při plnění uživatelského
  požadavku ("log se vypisuje neprehledne"), ne z auditu.
- **Commit:** (viz git log — commit bezprostředně po tomto zápisu)
- **Stav:** aktivní

### L-0048 — Malé `N` odhalilo reálnou "mrtvou zónu" na 150–200 kHz (ne jen artefakt testu)

- **Datum:** 2026-09-13
- **Oblast:** periferie (I2C4), metodika měření
- **Symptom:** kompletní sweep I2C4 (25–500 kHz, `N=25`, všechna tři
  zařízení, bez resetu desky) ukázal, že 150 a 200 kHz dávají **100 %
  chyb na VŠECH třech zařízeních najednou** s `SDA` drženou dole
  (skutečně zavěšená sběrnice) — ale 250 kHz a výš (250/300/400/500)
  TMP117 dává znovu čistou nulu a FT5x06/ATtiny čistý NACK (`SDA=1`,
  žádné zavěšení).
- **Příčina/nález:** předchozí dvě měření (velké `N`) měla nad ~125 kHz
  jen kontaminovaná data, protože velké `N` samo způsobovalo reset desky
  (TODO #244) — takže se nedalo rozlišit „je to vlastnost sběrnice" od
  „je to artefakt vlastního testu". S `N=25` (žádný reset) se ukázalo,
  že **mrtvá zóna na 150–200 kHz je reálná, opakovatelná vlastnost**,
  ne testovací artefakt — a je úzká: hned o 50 kHz výš (250 kHz) TMP117
  běží čistě. Mechanismus neznámý (možná rezonance/odraz na vedení
  přesně v tom pásmu kombinace SCLH/SCLL, možná interakce s kapacitou
  sběrnice) — nezkoumáno dál, protože provozní rychlosti (50/75/400 kHz)
  tu zónu neprotínají.
- **Proč to nebylo vidět dřív:** dvě předchozí měření odvodila
  „nad 125 kHz vše kontaminováno" ze SPRÁVNÉHO pozorování (velké `N`
  resetovalo desku), ale tenhle správný závěr **zakryl jiný, skutečný
  jev** v datech, která byla technicky nedůvěryhodná ze zcela jiného
  důvodu. Oprava metodiky (malé `N`) neprokázala jen "400 kHz je OK" —
  odhalila i něco, co se předtím nedalo vidět vůbec.
- **Pravidlo:** **Když je měření kontaminované JEDNÍM konkrétním
  mechanismem (tady: reset desky od velkého `N`), neuzavírej celé
  frekvenční/parametrické pásmo jako „nedůvěryhodné" navždy — oprav
  ten jeden mechanismus a změř to pásmo znovu.** „Kontaminováno" a
  „nemá to zajímavé vlastnosti" jsou dvě různá tvrzení; první nedokazuje
  druhé. Prázdné místo v datech je díra k zalátání, ne důvod ho navěky
  ignorovat.
- **Detekce:** žádná automatická — čistě HW nález z opakovaného měření
  s opravenou metodikou.
- **Commit:** (viz git log — commit bezprostředně po tomto zápisu)
- **Stav:** aktivní

### L-0047 — Přepnutí I2C rychlosti není "levná" operace, když je na sběrnici bit-bang slave

- **Datum:** 2026-09-13
- **Oblast:** periferie (I2C4), regrese vlastní úpravou, HW nález za běhu
- **Symptom:** po naflashování a power-cyklu s `i2c4_speed_select()`
  (L-0045/L-0046) dotyk vůbec nefungoval, boot splash se neukázal, displej
  byl ~7 s černý a hlavní obrazovka se ukázala pozdě, screensaver se
  aktivoval ~11,5 s po zapnutí — reálný HW test, ne odhad.
- **Příčina (✅ POTVRZENO druhým HW testem — oprava fungovala):**
  `i2c4_speed_select()` do té doby dělal `HAL_I2C_DeInit()` +
  `HAL_I2C_Init()` (stejný vzor jako starší `i2c4_recover()`/
  `i2csp_set_timing`). DeInit/Init ale přes `MspDeInit`/`MspInit`
  **překonfiguruje GPIO SCL/SDA** (AF → jiný mode → AF) — a to je přesně
  ta třída hranového přechodu na sběrnici, před kterou kód u
  `s_bl_settle` (`freertos_task_ui.c`) roky varuje: „zápis jasu tesně
  následovaný START-em touch čtení mu rozhodí slave automat → drží SDA →
  mrtvá I2C4 až do power-cyklu". Boot bring-up (`main.c`) dělal přesně
  tuhle sekvenci **bez jakékoli klidové mezery**: zápis jasu do ATtiny
  (50 kHz) → `i2c4_speed_select` (dřívější DeInit/Init) → `ft5x06_probe` —
  runtime cesta má aspoň 150 ms `s_bl_settle`, boot cesta žádnou.
- **Oprava:** `i2c4_speed_select()` přepsán na **minimální RM0399 postup**:
  `PE=0` (jen bit v `CR1`, nesahá na GPIO) → přímý zápis `TIMINGR` → `PE=1`.
  Žádný `HAL_I2C_DeInit`/`Init`, žádné `MspInit`, žádná GPIO hrana. Navíc
  doplněna 150ms klidová mezera do boot sekvence (`main.c`, mezi zápisem
  jasu a přepnutím rychlosti) jako levná pojistka navíc, i když už by
  nemusela být nutná.
- **Pravidlo:** **Když na sběrnici sedí bit-bang slave (firmware, ne
  hardwarový blok, dělá časování), NEPOUŽÍVEJ k přeladění rychlosti nic
  těžšího, než co periferie/registr doopravdy vyžaduje.** `HAL_I2C_DeInit`/
  `Init` je "bezpečný vzor" jen ve smyslu "nespustí `Error_Handler()`" — o
  GPIO vedlejších účincích nic neříká. Než se sáhne po hotovém vzoru
  z jiného místa v kódu (`i2c4_recover`), ověř, že ten vzor běžel v STEJNÉM
  kontextu (frekvence volání, blízkost k zápisu do citlivého slave) — vzor
  bezpečný v diagnostickém nástroji (`i2cspeed`, izolované přepnutí jednou
  za krok, mimo produkční provoz) nemusí být bezpečný v produkční cestě
  (přepnutí na KAŽDÉM touch pollu, těsně po zápisu jasu).
- **Detekce:** žádná automatická — build i audit prošly čistě, je to
  funkční/HW vada. Odhaleno jen reálným power-cyklem na desce.
- **Commit:** `7a1cecb`
- **Stav:** ✅ aktivní, potvrzeno na HW — dotyk, boot splash i čas do
  hlavní obrazovky po power-cyklu s opravou v pořádku (uživatel 2026-09-13:
  „ok funguje").

### L-0046 — Per-target rychlost sběrnice: kdo ji nenastaví, zdědí cizí

- **Datum:** 2026-09-13
- **Oblast:** periferie (I2C4), regrese vlastní úpravou
- **Symptom:** po zavedení `i2c4_speed_select()` (L-0045 — 50 kHz ATtiny,
  200 kHz FT5x06/TMP117) přestaly tři diagnostické UART příkazy (`panel`,
  `scanner`, `i2c4`) spolehlivě fungovat s ATtiny — všechny tři osloví 0x45
  přímo nebo v rámci širšího skenu, ale žádný z nich `i2c4_speed_select`
  nevolal. V provozu konverguje "zbytková" rychlost sběrnice k 200 kHz
  (touch poll ~15 Hz + TMP117 poll 2 Hz běží mnohem častěji než zápis do
  ATtiny), takže tyhle příkazy by na 200 kHz spuštěné za běhu tiše falešně
  hlásily "ATtiny neodpovídá", i když žije.
- **Příčina:** zavedením per-target přepínání rychlosti se rychlost sběrnice
  změnila z **globální konstanty** (jedna hodnota, platí vždy) na **sdílený
  proměnlivý stav** (platí, dokud ji někdo jiný nezmění) — a při hledání
  všech míst, která na I2C4 sahají, se prohledaly jen "produkční" spotřebitele
  (`main.c`, oba FreeRTOS tasky), ne diagnostické UART příkazy ve stejném
  souboru o pár set řádků dál.
- **Oprava:** `i2c4_speed_select(I2C4_TIMING_ATTINY_50KHZ)` doplněno do
  všech tří příkazů. 🔑 **U `scanner`/`i2c4` (přes sdílenou `uart_i2c4_probe`)
  to musí být UVNITŘ per-adresa smyčky, ne jednou před ní** — mutex se mezi
  adresami pouští (aby touch/TMP117 „dýchaly"), takže by jiný task mohl
  rychlost mezitím vrátit na 200 kHz. `panel` mutex mezi probe/power-on
  nepouští, takže mu stačí nastavit jednou na začátku KAŽDÉHO mutex-bloku
  (má dva, oddělené DSI kroky mezi nimi).
- **Pravidlo:** **Když sdílený stav periferie (rychlost, konfigurace, adresa…)
  přestane být konstantní a stane se závislým na tom, kdo tam sáhl
  posledně, promysli VŠECHNY konzumenty — ne jen ty, které jsi měnil.**
  Konkrétně: (1) vyhledej *všechny* volající té periferie v celém projektu,
  ne jen v souborech, které jsi právě upravoval; (2) u každého, co drží
  mutex/zámek PŘES víc transakcí najednou, stačí nastavit stav jednou; (3) u
  každého, co zámek MEZI transakcemi pouští, musí být nastavení stavu
  UVNITŘ každé transakce (jinak ho někdo jiný mezitím změní).
- **Detekce:** žádná automatická (build i audit prošly čistě — jde o
  logickou/funkční vadu, ne syntaktickou). Odhaleno až při psaní přehledové
  tabulky konzumentů pro dokumentaci — **sepsání kompletního přehledu "kdo
  sahá na X" je samo o sobě metoda pro odhalení chybějících míst.**
- **Commit:** `65773b7`
- **Stav:** aktivní

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

### L-0058 — Nedosažitelný kód, který by při dosažení byl vadný, není mrtvý kód, ale past

- **Datum:** 2026-09-19
- **Oblast:** CM4 / lwIP glue, vlastnictví objektů předaných knihovně
- **Symptom:** Žádný — a právě to je na tom to nebezpečné. `HAL_ETH_TxFreeCallback`
  v `CM4/LWIP/Target/ethernetif.c` dělala `pbuf_free((struct pbuf *)buff)`, tedy
  uvolňovala pbuf, který si v zero-copy návrhu ST `low_level_output` předtím
  přivlastnil přes `pbuf_ref()`. My `pbuf_ref()` nevoláme vůbec, takže ten účet
  nebyl vyrovnaný. Nespadlo to jen proto, že **callback nikdo nevolá**:
  `HAL_ETH_Transmit` (blokující varianta, kterou používáme) ho nevolá,
  `HAL_ETH_ReleaseTxPacket` nevolá v celém projektu nikdo a ETH přerušení se
  neobsluhuje.
- **Příčina:** Callback se převzal z příkladu ST, ale bez druhé půlky páru.
  Zrádné je, že **vypadá jako součást funkčního páru** — čtenář nemá důvod
  hledat chybějící `pbuf_ref`. Odjistila by ho první přirozená další změna:
  přechod na `HAL_ETH_Transmit_IT` kvůli propustnosti, nebo zavolání
  `HAL_ETH_ReleaseTxPacket`, což návod ST doporučuje. Následek by bylo dvojí
  uvolnění v lwIP haldě na CM4 — tedy poškození haldy, které se projeví
  **náhodně a daleko od příčiny** (zatuhlé spojení, podivný obsah odpovědí,
  pád CM4).
- **Oprava:** Callback odstraněn (HAL má `__weak` variantu, takže se nic
  nerozbilo — `--gc-sections` symbol z obrazu zahodil úplně). Na jeho místě
  zůstal komentář, který příští úpravě říká, že při přechodu na `Transmit_IT`
  se musí udělat **obojí naráz**: vrátit callback **a** přidat `pbuf_ref(p)`.
  Jedno bez druhého je buď dvojí uvolnění, nebo únik paměti.
- **Pravidlo:** **Kód, který dnes nikdo nevolá, není neškodný.** Když by po
  dosažení byl vadný, je to nabitá zbraň — buď ho odstraň, nebo dopáruj; a když
  ho odstraníš, napiš do komentáře, co musí příští úprava udělat SPOLEČNĚ, aby
  ho směla vrátit. „Nevolá se, tak to nevadí" je popis dnešního stavu, ne
  vlastnost kódu.
- **Detekce:** U každého callbacku nebo funkce, která **uvolňuje či zavírá cizí
  objekt**, najdi řádek, kde se ten objekt přivlastňuje (`*_ref`, `*_alloc`,
  `*_take`). Když takový řádek neexistuje, je to jedno z dvojice: past (uvolňuje
  se nepřivlastněné), nebo únik (přivlastňuje se bez uvolnění). Obojí je nález.
  Souvisí s **L-0025** (`close` není `free`), tam ale obráceně: tam se
  neuvolňovalo, tady se uvolňuje něco, co nám nepatří.
- **Commit:** viz git log — `fix(cm4-eth)` s F-0134
- **Stav:** aktivní

---

### L-0059 — Čítač, jehož NULA nese diagnózu, nesmí přetékat — saturuj ho

- **Datum:** 2026-09-19
- **Oblast:** IPC / diagnostika napříč jádry
- **Symptom:** `g_eth_tx_ok` na CM4 je volně běžící `uint32_t`. Při publikaci do
  sdílené struktury (kde bylo místo jen na `uint16_t` v recyklované vycpávce) by
  se prostým přetypováním po 65 536 paketech vrátil na **0**.
- **Příčina:** U tohohle čítače nula **není počáteční stav, ale diagnóza**:
  „CM4 neodeslala ani jeden paket". Přesně to byla nejcennější odpověď při
  nejdelším ladění v projektu (TX adresa, 2026-09-08), kdy `NET: UP 100 Mbit
  full` i `ETH(CM4): init OK` tvrdily, že je vše v pořádku, a na drát přitom
  nešel ani bajt. Přetečení by tu diagnózu **nerozlišitelně zfalšovalo** —
  a to v okamžiku, kdy vysílání funguje nejlépe.
- **Oprava:** Saturace při **publikaci**, ne v inkrementu (`ipc_cm4_set_eth_tx`):
  `ok` saturuje na 65535, `err` na 255. Semantika je dokumentovaná jako „aspoň
  tolik" a v `status` se saturovaná hodnota tiskne s `+`. Nula tak zůstává
  vyhrazená výhradně pro „nikdy" a výpis ji rovnou označí
  (`<== NEODESLALA ANI JEDEN PAKET`).
- **Pravidlo:** **Než zúžíš nebo publikuješ čítač, zeptej se, co znamená jeho
  nula.** Když nese význam („nikdy", „nespustilo se", „chybí"), musí čítač
  saturovat. Volně běžící čítač smí přetékat jen tam, kde se sleduje POUZE růst
  (`heartbeat`, `SEQUENCE`) — tam je rozdíl dvou hodnot, ne hodnota sama.
- **Detekce:** U každého čítače, který se někam publikuje nebo zužuje, projdi
  obě otázky: (1) co znamená 0, (2) co se stane při přetečení. Když odpověď na
  (1) je diagnóza a na (2) „vrátí 0", je to nález. Doplňkově: saturující pole
  musí být poznatelné i ve výpisu (značka `+`), jinak čtenář nepozná „přesně
  tolik" od „aspoň tolik".
- **Commit:** viz git log — `fix(ipc)` s F-0138
- **Stav:** aktivní

---

### L-0060 — Nula podmíněně měřené hodnoty znamená dvě věci; bez třetího stavu z ní diagnostika udělá tvrzení

- **Datum:** 2026-09-19
- **Oblast:** diagnostika (`membench`), interpretace výsledků
- **Symptom:** `membench` umí v jednom výpisu napsat *„ADRESY SE OPAKUJI po 4 kB —
  dve ruzne adresy = tataz bunka!"* a **hned pod tím** *„framebuffery se ale
  navzajem NEprekryvaji — zobrazeni tim netrpi"*. To druhé je přitom tvrzení
  o měření, které **vůbec neproběhlo**.
- **Příčina:** `fb_alias` se měřil jen pod `if (span)`, tedy když překryv našla
  sonda `sdram_alias_span()` (skenuje od 64 kB). `alias_off` se ale plní i
  **druhou cestou** — z `addr_lines_test()`, která jde už od 4 B. Perioda
  překryvu **pod 64 kB** (vadný nízký adresní bit, např. `HADDR[10]` = 4 kB) je
  proto pro první sondu neviditelná a pro druhou viditelná. V té kombinaci
  zůstal `fb_alias` na nule z `memset` — a výpis nulu přečetl jako „neexistuje",
  ačkoli znamenala „nezměřeno". U nejdražší otázky modulu (kdyby FB0 a FB2
  sdílely paměť, triple buffering je fakticky double) to je **aktivní falešné
  uklidnění**, a vada nízkého adresního bitu se navíc projeví *uvnitř* každého
  framebufferu, takže ta věta nebyla jen nedoložená, ale nejspíš i nepravdivá.
- **Oprava:** Dvě věci naráz, protože každá řeší jinou polovinu: (1) `fb_alias`
  se měří **bezpodmínečně** a jako první věc, ještě před kontrolou chráněných
  oblastí — takže odpověď existuje i když se celý test přeskočí; (2) přibyl
  **třetí stav `fb_alias_checked`** a uklidňující věta se tiskne výhradně při
  něm, jinak se vypíše „překryv MEZI framebuffery se NEMĚŘIL".
- **Pravidlo:** **Když je hodnota měřená podmíněně, její nula znamená dvě různé
  věci — „neexistuje" a „neměřilo se" — a diagnostika mezi nimi MUSÍ umět
  rozlišit.** Buď měř bezpodmínečně, nebo zaveď příznak „změřeno"; tvrzení
  o nepřítomnosti vady se nikdy neopírá o nulu, u které se nedá dokázat, že
  vznikla měřením. (Souvisí s **L-0011**: hláška diagnostiky je pozorování, ne
  diagnóza — tady dokonce ani to pozorování neexistovalo.)
- **Detekce:** U každé uklidňující věty ve výpisu („…NEpřekrývají", „…je v
  pořádku", „bez chyb") najdi řádek, který tu hodnotu **zapsal**, a ověř, že se
  provede na každé cestě, po které se ta věta může vytisknout. Když je zápis pod
  `if`, musí být pod stejným `if` i ta věta — nebo musí existovat příznak.
  Doplňkově: pozor na zřetězení `if (A) … else if (B)`, kde `else` neúmyslně visí
  u posledního testu (přesně to tu bylo a fungovalo jen shodou okolností).
- **Commit:** viz git log — `fix(membench,sdramlog)` s F-0116
- **Stav:** aktivní

---

### L-0061 — Převod akce na „požadavek pro cizí úlohu" je hotový až s odpovědí na to, co když ta úloha nepřijde

- **Datum:** 2026-09-19
- **Oblast:** mezivláknové mosty (`*_req` příznaky), vlastnictví stavu
- **Symptom:** `sdram_log_reset()` nulovala `s_head` přímo z UartTasku, přestože
  hlavička deklaruje **jediného producenta** (FpgaTask) a celá bezzámkovost ringu
  na tom stojí. Producent dělá `h = s_head; … s_head = h + 1u;`, takže reset
  padnoucí mezi ty dva kroky se **tiše ztratil** — uživatel viděl, že
  `sdramlog reset` „nic neudělal".
- **Příčina:** Zjevná oprava je vzor, který projekt už používá desetkrát
  (`g_membench_req`, `g_screen_req`, `g_ui_cfg_req`, `g_sd_req`,
  `g_si5356_clr_req`): cizí úloha nastaví příznak, **vlastník** ho zkonzumuje.
  Jenže tím se akce stane **závislou na tom, že vlastník běží** — a tady vlastník
  běží jen dokud přicházejí vzorky. Při mrtvém SPI linku by `sdramlog reset`
  přestal fungovat úplně. Čistý invariant by tedy rozbil funkci.
- **Oprava:** Požadavek **plus** ohraničené čekání u volajícího (300 ms = ~6
  příležitostí při 20 Hz pollu) **plus** `_force()` jako poslední instance, která
  invariant vědomě poruší — a výpis to **přizná jinou větou** („vynulovano PRIMO
  — producent se za 300 ms neozval"). Čekání s `osDelay` je u volajícího, aby
  modul zůstal bez závislosti na scheduleru.
- **Pravidlo:** **Když měníš přímou akci na požadavek konzumovaný jinou úlohou,
  napiš v témže commitu, co se stane, když ta úloha nepřijde.** Odpověď smí být
  i „nic se nestane, je to v pořádku" — ale musí být vyslovená a ověřená, protože
  „vlastník to zkonzumuje" je předpoklad o běhu, ne vlastnost kódu. Když
  existuje fallback, který invariant poruší, MUSÍ se navenek hlásit **jinak** než
  normální cesta; jinak jedno slovo znamená dvě různé věci.
- **Detekce:** U každého `*_req` příznaku najdi úlohu, která ho konzumuje, a zeptej
  se: (1) za jakých okolností ta úloha neběží nebo nepolluje, (2) co v takovém
  případě uvidí uživatel. Když odpověď na (2) je „nic, a bude si myslet, že to
  proběhlo", je to nález. Souvisí s **L-0054** („jeden vlastník" je tvrzení
  o VŠECH volajících) — tahle lekce je jeho druhá polovina: o tom, co ta
  jednovlastnická disciplína stojí.
- **Commit:** viz git log — `fix(membench,sdramlog)` s F-0118
- **Stav:** aktivní

---

### L-0062 — Než odstraníš duplikát, zjisti, jestli jedna z kopií nedělá něco navíc

- **Datum:** 2026-09-19
- **Oblast:** `.ioc` vs. ruční inicializace, sdílená GPIO mezi jádry
- **Symptom:** TIM1 (encoder) měl **dvě konfigurace**: jednu z `.ioc` přes
  `MX_TIM1_Init()` a jednu jako surové zápisy do registrů v `encoder_init()`.
  Protože `encoder_init()` běží z UiTasku, tedy po `main()`, ruční verze vždy
  vyhrála a ta z `.ioc` **nikdy nenabyla účinku**. Kdo změnil filtr v CubeMX,
  nezměnil nic — a existuje commit, který ty hodnoty „opravoval" v domnění, že
  na nich záleží.
- **Příčina:** Zjevná náprava je „zruš duplikát a nech jednu pravdu". Jenže ty dvě
  kopie **nebyly rovnocenné**: modul kromě timeru konfiguroval i piny PA8/PA9,
  a to **pod `gpio_cfg_lock()`** (HSEM). Generovaný `HAL_TIM_Encoder_MspInit()`
  nastaví tytéž piny stejnými hodnotami, ale **bez toho zámku** — a `GPIOA` sdílí
  CM4 (ETH: PA1 REF_CLK, PA2 MDIO, PA7 CRS_DV). `HAL_GPIO_Init` dělá nad
  `MODER`/`AFR` neatomický read-modify-write, takže ztracený zápis jednoho jádra
  tiše vrátí cizí pin. Naivní „smaž duplikát" by tedy odstranilo **jediný zápis
  chráněný proti závodu** a vyrobilo přesně tu třídu vady, která v tomhle projektu
  shodila displej (PG8) i síť (PG11).
- **Oprava:** Rozdělit vlastnictví podle toho, co která kopie umí: **parametry
  timeru** = `.ioc`/`MX_TIM1_Init` (jediná pravda, surové zápisy zrušeny),
  **konfigurace pinů** = modul, pod zámkem, idempotentně, s komentářem „nemazat
  jako duplikaci — duplikace je jen v hodnotách, jediný vlastník zápisu pod zámkem
  je tenhle modul". Navíc guard `htim1.Instance != TIM1` (**L-0009**), aby regen,
  který by TIM1 z `.ioc` vyhodil, encoder rovnou vypnul místo startu
  nenakonfigurovaného timeru.
- **Pravidlo:** **U každé duplikované inicializace porovnej kopie řádek po řádku,
  ne jen výsledné hodnoty — a ptej se, co navíc dělá kontext** (zámek, bariéra,
  pořadí, cache operace, kontrola návratu). Když jedna kopie má něco, co druhá
  nemá, není to duplikát ke smazání, ale dvě různé odpovědnosti ke **rozdělení**;
  a to „něco navíc" patří do komentáře, jinak to smaže příští čtenář.
- **Detekce:** Před odstraněním duplikátu: `git grep` na obě místa a diff jejich
  *okolí*, ne jen těla. Konkrétně u GPIO: každý `HAL_GPIO_Init` nad **sdíleným
  portem** (GPIOA, GPIOG na této desce) musí být obklopen `gpio_cfg_lock()` /
  `_unlock()`; generovaný kód to nikdy nemá, takže „spolehnu se na MspInit" je
  u sdíleného portu vždy zhoršení. Souvisí s **L-0018** (dvě místa konfigurující
  touž věc) — tahle lekce říká, jak to rozpletnout, aniž se přitom něco ztratí.
- **Commit:** viz git log — `fix(encoder)` s F-0122
- **Stav:** aktivní

---

### L-0063 — „Odmítnout start" je legitimní politika, ale musí jít poznat od vypnuté desky

- **Datum:** 2026-09-19
- **Oblast:** hodiny / politika při poruše, boot diagnostika
- **Symptom:** Když nenaběhne HSE (25 MHz) nebo LSE (32,768 kHz), `SystemClock_Config()`
  selže a spadne do `Error_Handler()`. Přístroj z pohledu uživatele **neudělal
  vůbec nic**: žádný displej, žádná konzole, **a ani bliknutí nebo pípnutí**.
  Od nefunkčního napájení to nebylo k rozeznání.
- **Příčina:** Dvě věci se sešly. (1) U kmitočtového normálu je odmítnutí startu
  **správná** politika — běh proti špatné časové základně je horší než neběh — jen
  nebyla nikde vyslovená. (2) Diagnostika ji nedokázala ohlásit: `bootled_step`
  startuje na **0**, `SystemClock_Config()` je generovaný kód **bez `USER CODE`
  bloku**, takže si tam krok zapsat nelze, a `blink_pattern(0)` proběhne
  **prázdnou smyčkou**. Nula jako „nezačalo se" tedy dala vzor „nic". Navíc HSE
  a LSE se konfigurují v **jednom** `HAL_RCC_OscConfig()`, takže ani návratová
  adresa v crash black-boxu je neodliší.
- **Oprava:** Politika vyslovena a zapsána (CLAUDE.md), a rozlišení se odvodí
  **z registrů v `USER CODE`** (`RCC_CR.HSERDY`, `RCC_BDCR.LSERDY`) — ne úpravou
  generovaného kódu. `Error_Handler` tím nastaví nenulový krok (15 = HSE,
  16 = LSE, 17 = jinak před prvním initem), takže vzor je vidět i slyšet, a stav
  oscilátorů uloží do black-boxu → `status` hlásí `hal_err@HSE` / `hal_err@LSE`.
- **Pravidlo:** **Když se firmware rozhodne nenaběhnout, musí to říct jiným kanálem
  než tím, který zároveň nefunguje.** A u každé diagnostiky, jejíž intenzita je
  číslo (počet bliknutí, délka, počet pípnutí), se zeptej, **co dělá nula** — když
  nula znamená „nic se nestane", je to tichá cesta právě pro ten nejranější a
  nejhorší případ. Výchozí hodnota takového čítače nesmí být neodlišitelná od
  „všechno v pořádku".
- **Detekce:** U každého vzoru řízeného počtem projdi cestu s hodnotou 0 a 1.
  Konkrétně: `bootled_step` = 0 nastane pro **každé** selhání před prvním
  `bootled_step()` — tedy `HAL_Init`, `SystemClock_Config`, MPU, `MX_GPIO_Init`.
  Obecněji: u fáze bootu, která nemá `USER CODE` hook, se stav ověřuje **až za ní**
  z registrů (**L-0009**), a diagnostika pro ni musí mít vlastní kód, ne výchozí nulu.
  ⚠️ Přiznaná mez téhle konkrétní opravy: 15 vs 16 bliknutí se počítá nespolehlivě,
  takže rozlišitelné je „hodiny vs periferie", ne „HSE vs LSE" — to řekne až
  black-box. Když má být čitelný i ten rozdíl, musí se změnit **tvar** vzoru, ne
  jeho počet.
- **Commit:** viz git log — `fix(hodiny)` s F-0007 + F-0108
- **Stav:** aktivní

---

### L-0064 — Než vynutíš mez, zjisti, kdo ji dnes legitimně používá — a jestli není druhá mez na hodnotě

- **Datum:** 2026-09-19
- **Oblast:** formátování bez `%f` (nano.specs), vynucení mezí na rozhraní
- **Symptom:** `fmt_fixed()` má `case 1/2/3` a `default:`, které vytiskne jen celou
  část. Komentář u funkce i nález tvrdily „podporuje 1–3 desetiny", takže se
  nabízelo `default:` změnit na „ořízni na 3".
- **Příčina:** Obojí bylo **nepravda ve prospěch horší opravy**. `default:` je
  zároveň **legitimní implementace nuly** (`fixed_split(v,0,…)` dá scale 1 a
  frac 0, takže `"%ld"` je správný výstup) a spoléhají na ni **čtyři skuteční
  volající** — min/max v seznamu senzorů a teplotní pásmo OCXO. Navržená oprava
  by z „45" udělala „45.000". Skutečný rozsah je **0–3**.
  A byla tam **druhá, ostřejší mez, o které nemluvil nikdo**: `fixed_split`
  počítá `t = (int32_t)(v · 10^decimals + 0.5)`, takže platí i
  `|v| · 10^decimals < 2,15e9` — při 3 desetinách `|v| < ~2,15e6`. Nad tím int32
  přeteče. Tuhle cestu spouští **hodnota**, ne argument, takže ji žádný grep
  nenajde a nikdo by ji nehledal.
- **Oprava:** Clamp na 0..3, pak `d` snižovat, dokud `|v| · 10^d` nevleze do
  int32, a každé omezení počítat do `status` (`FORMAT: omezenych desetin N`).
  Bez `configASSERT` — pád uprostřed kreslení by shodil desku kvůli formátování.
- **Pravidlo:** **Než vynutíš mez, vypiš si všechny volající a zjisti, které
  hodnoty dnes používají** — `default:`/`else` bývá živá větev, ne jen chybová.
  A u každé meze se ptej, jestli není **druhá mez na hodnotě**, ne na argumentu:
  kdekoli se vstup násobí nebo škáluje do celého čísla, existuje rozsah, ve kterém
  argument projde a přesto to přeteče.
- **Detekce:** `grep` na volání funkce a rozdělit je podle **literál vs. proměnná**.
  Literály zkontroluješ očima; proměnné (`HBAR[].deci`, `KALIB_ROWS[].decimals`,
  spočítané výrazy) jsou skutečná riziková plocha a ty potřebují runtime clamp
  s počítadlem. U škálování do `intN` dopočítej mez na vstupní hodnotu a napiš ji
  do komentáře v jednotkách, ve kterých volající myslí.
- **Commit:** viz git log — `fix(format,ipc)` s F-0053
- **Stav:** aktivní

---

### L-0065 — Sdílenou paměť nulujte po vlastnictví, ne po adresním rozsahu

- **Datum:** 2026-09-19
- **Oblast:** IPC CM7 ↔ CM4, inicializace sdílené paměti
- **Symptom:** `ipc_init()` na CM7 dělal `memset` přes **celou** sdílenou strukturu
  včetně bloku `cm4`, do kterého zapisuje výhradně druhé jádro. Protože se volá
  ze `StartDefaultTask` (~sekundy po bootu, za bring-upem displeje), zatímco CM4
  je bare-metal a publikuje už ~1,3 s po bootu, mohl memset dopadnout **doprostřed
  publikování**. Doloženo na HW 2026-08-30: dopadl mezi publikaci httpd a eth,
  takže `status` hlásil „SCPI(CM4): jeste nedobehl", přestože selftest prošel.
- **Příčina:** `memset(p, 0, sizeof *p)` je operace nad **adresním rozsahem**, ale
  sdílená struktura je rozdělená podle **vlastnictví**. Jedno `sizeof` je pohodlné
  a vypadá jako „uveď do známého stavu" — jenže tím CM7 přepisuje data, která mu
  nepatří. Obrana pak visela jen na disciplíně („každou hodnotu publikuj
  opakovaně"), tedy na pravidlu v komentáři, ne na kódu.
- **Oprava:** Každé jádro nuluje svůj blok: CM7 `snap`/`cmd`/`resp`/`log`/`errlog`,
  CM4 svůj `cm4` ve vlastním initu (tam je jediným zapisovatelem a ještě
  nepublikoval → bez závodu). CM7 na `cm4` sáhne jen když je **doloženo**, že CM4
  nenaběhla (vypršelý boot gate) — tím se uzavře i studený start s náhodným
  obsahem SRAM. Layout se nemění, `IPC_VERSION` se nezvedá.
- **Pravidlo:** **Sdílenou paměť inicializuj po blocích podle vlastníka, nikdy
  jedním `memset` přes `sizeof` celé struktury.** Když potřebuješ vyčistit i cizí
  blok, smí to být jen ve stavu, kdy je **dokázáno**, že vlastník nezapisuje —
  a ten důkaz napiš do kódu jako podmínku, ne do komentáře jako předpoklad.
- **Detekce:** U každého `memset`/`memcpy` nad sdílenou strukturou vypiš, které
  její členy píše které jádro (nebo úloha), a porovnej s rozsahem operace.
  Doplňkově se ptej, jestli je ztráta v daném bloku **vratná**: ring nebo handshake
  se sám zhojí (prázdný ring = pošli znovu), jednorázově zapsaný stav ne — a právě
  ten blok se nulovat nesmí. Souvisí s **L-0054** („jeden vlastník" je tvrzení
  o všech volajících) a **L-0061** (požadavek konzumovaný cizí úlohou).
- **Commit:** viz git log — `fix(format,ipc)` s F-0017
- **Stav:** aktivní

---

### L-0066 — Prázdná sekce nic nerezervuje; ochrana paměti musí umět selhat při linkování

- **Datum:** 2026-09-19
- **Oblast:** linker skripty, sdílená paměť mezi jádry
- **Symptom:** CM7 linker měl sekci `.ipc_shared (NOLOAD) … >RAM_D3` s komentářem
  „IPC sdílená paměť CM7↔CM4, 64 KB" a vypadalo to jako ochrana. Ve skutečnosti
  měla sekce **délku 0** (`_sipc_shared == _eipc_shared == 0x38000000`), protože
  `g_ipc` je **makro nad pevnou adresou**, ne objekt — do té sekce tedy nikdy nic
  nespadlo. CM4 linker `RAM_D3` neznal vůbec.
- **Příčina:** Sekce s `KEEP(*(.ipc_shared))` rezervuje jen to, co do ní někdo
  umístí. Když se k paměti sahá přes přetypovanou konstantu (což je u sdílené
  paměti mezi jádry běžné, protože obě strany musí vidět **tutéž** adresu), je
  vstupní seznam prázdný a sekce je nulová. Ochrana tedy existovala jen proto, že
  do RAM_D3 zatím nikdo nic nedal — ne proto, že by tomu něco bránilo. První další
  sekce s `>RAM_D3` by začala přesně na `0x38000000`, tedy na hlavičce IPC, a
  projevilo by se to jako „IPC občas nenaběhne" nebo poškozený snapshot.
- **Oprava:** Rezervovat **explicitně** (`. = _sipc_shared + 64K;` — absolutně, ne
  `. = . + 64K`, to by se přičítalo za případné umístěné objekty), a to v linkeru
  **obou** jader; plus `ASSERT(_sipc_shared == <IPC_BASE>)`, protože adresa je
  zdvojená mezi hlavičkou a linkerem a tenhle assert je jediné místo, kde se ty dvě
  pravdy potkají.
- **Pravidlo:** **Region, na který se sahá přes pevnou adresu, musí být v linkeru
  rezervovaný explicitní velikostí, ne jen značkami.** A ochrana paměti má hodnotu
  jen tehdy, když umí **selhat při linkování** — komentář, značka ani prázdná sekce
  nejsou ochrana. Platí to pro **každý** obraz, který tu paměť vidí, ne jen pro ten,
  kde je definovaná.
- **Detekce:** `nm <elf> | grep _s<sekce>` a porovnej se značkou konce — stejná
  adresa = sekce nic nerezervuje. **A udělej negativní test:** dočasně přidej do
  toho regionu sekci o pár bajtů a ověř, že link **selže** (`region … overflowed`).
  Bez toho testu nevíš, jestli rezervace funguje — přesně to je **L-0039**
  (pozitivní kontrola musí obsahovat vadu, kvůli které kontrola vznikla).
  ⚠️ `NOLOAD` rezervace nic nepřidá do `.text`, ale `size` ji vykáže v `bss` —
  nenech se tím zmást při porovnávání velikostí před/po.
- **Commit:** viz git log — `fix(linker)` s F-0016
- **Stav:** aktivní

---

### L-0067 — Zachraňující retry smí obnovit PŘÍSTUP, ne data — jinak přepíše to, co mezitím vzniklo

- **Datum:** 2026-09-19
- **Oblast:** perzistence (W25Q blob store), zotavení z neúspěšné inicializace
- **Symptom:** Pět inicializací úložiště (`syscfg_load`, `setup_init`, `calib_load`,
  `flightrec_init`, `errlog_init`) mělo tvar *„nedostal jsem mutex → `return`"*.
  Příznak připravenosti pak zůstal `false` **po celý zbytek běhu**: nastavení se
  nikdy neuložilo, kalibrace zůstala na datasheetových výchozích a tlačítka v okně
  SESTAVY tiše nedělala nic. Uživatel to poznal teprve tím, že se mu po restartu
  ztratilo nastavení — a bez jakékoli stopy proč.
- **Příčina:** Dvě věci. Za prvé selhání nemělo **žádný** výstup (na rozdíl od
  `datalog_init`, který ho vypisuje — takže bylo vidět, že to jde). Za druhé, když
  se dopisoval **retry**, nabízelo se prostě zavolat `syscfg_load()` znovu. To by
  ale bylo špatně: `syscfg_load()` **čte blob z flash do RAM**, a v RAM už může být
  novější nastavení, které uživatel mezitím změnil. Zachrana by tedy přepsala živý
  stav starou verzí z disku — vada *horší* než ta, kterou léčí, a projevila by se
  jako „nastavení se samo vrátilo".
- **Oprava:** Retry volá **jen `w25q_init()` + `w25q_store_init()`** — to naskenuje
  sektory a nastaví `ready`/`seq`, ale payload nikam nekopíruje. Obnoví se tedy
  **přístup k úložišti**, ne jeho obsah. Retry je navíc **ohraničený** (5 pokusů,
  ≥10 s od sebe), protože `errlog_init()` může skončit `w25q_erase_sector`, tj.
  50–400 ms v úloze, která krmí watchdog. A stav je vidět třemi cestami: `status`,
  okno PAMĚŤ a **amber SYS pilulka**.
- **Pravidlo:** **Když dopisuješ zotavení z neúspěšné inicializace, rozděl ji na
  „obnov přístup" a „načti data" a zopakuj JEN tu první část.** Opakované volání
  celého `*_load()` přepíše stav, který mezitím vznikl v RAM. A každý retry, který
  může sáhnout na pomalou periferii, musí mít **strop počtu i minimální rozestup** —
  nekonečné opakování mrtvého hardwaru je horší než přiznaná porucha.
- **Detekce:** U každého `if (…) return;` v inicializaci se ptej: *„co zůstane
  rozbité do konce běhu a jak se to pozná?"* Když odpověď na druhou část je „nijak",
  je to nález i bez retry. U navrhovaného retry pak projdi, co všechno ta funkce
  **zapisuje do RAM** — a jestli je bezpečné to přepsat v okamžiku, kdy zařízení už
  nějakou dobu běží. Souvisí s **L-0017** (tichý přeskok jen s počítadlem) a
  **L-0016** (obrana, kterou nikdo nečte, není obrana).
- **Commit:** viz git log — `fix(uloziste)` s F-0098
- **Stav:** aktivní

---

### L-0068 — Pevně dlouhé textové pole není místo na prefix; strukturu ukládej strukturovaně

- **Datum:** 2026-09-19
- **Oblast:** trvalý záznamník chyb (`errlog`), návrh formátu záznamu
- **Symptom:** Crash black-box dekóduje příčinu do `g_crash_text` jako
  `"stall:UiTask"` / `"stack:UartTask"`. Do trvalé historie se z toho kopírovalo
  prvních `ERRLOG_TAG_LEN` = **6 znaků**, tedy přesně `"stall:"` a `"stack:"` —
  jméno tasku zmizelo. Zmizelo přitom právě to, kvůli čemu se black-box kdysi
  rozšiřoval („prostý IWDG reset byl němý — RSR řekl jen watchdog, ne který task").
- **Příčina:** Text nesl **dvě informace najednou**: druh pádu (prefix) a co spadlo
  (za dvojtečkou). Pole má pevnou délku, takže prefix — informace, která má jen šest
  možných hodnot a už existuje pro ni číselné pole `sub` — spolykal celou kapacitu.
  Zrádné je, že `errlog.h` `sub` jako „kind" **dokumentoval**, ale jediný zapisovatel
  do něj posílal nulu; formát byl navržený správně a nepoužíval se.
- **Oprava:** Rozdělit: druh pádu → `sub` (jedna hodnota), rozlišující část za
  oddělovačem → `tag`. Formát záznamu se nezměnil, jen se přestal plýtvat. Výpis pak
  prefix **rekonstruuje** z `sub` přes tabulku jmen, která leží hned u dekódování.
- **Pravidlo:** **Když do pevně dlouhého textového pole ukládáš řetězec, který má
  strukturu, ulož strukturu do strukturovaných polí a do textu jen to, co se jinam
  nevejde.** A než takové pole zkrátíš, napiš si skutečné hodnoty, které do něj
  poletí, a ořež je na papíře — u šesti znaků je rozdíl mezi `"stall:"` a `"UiTask"`
  rozdíl mezi žádnou a celou informací.
- **Detekce:** U každého `for (i = 0; i < LEN && src[i]; i++)` kopírování do pevného
  pole dohledej **všechny** formáty, které do `src` mohou přijít (tady `rtc.c`), a ořež
  je. Když po ořezu vznikne u dvou různých příčin **tentýž** výsledek, je to nález.
  Doplňkově: každé dokumentované pole, které nikdo neplní, je taky nález — grep na
  zapisovatele (souvisí s **L-0028**: věta v komentáři je testovatelná).
- **Commit:** viz git log — `fix(errlog,flightrec)` s F-0092
- **Stav:** aktivní

---

### L-0069 — Periodický plán `next += period` musí mít ošetřené velké zpoždění, jinak dohání

- **Datum:** 2026-09-19
- **Oblast:** periodické vzorkování (`datalog_tick`), plánování v tikové úloze
- **Symptom:** `datalog_tick` posouvá plán `s_next_ms += perioda`. Když tik dlouho
  neběžel (blokující `membench`, `sd_export` nebo erase QSPI z UartTasku), zůstal
  `s_next_ms` daleko v minulosti a následující tiky by zapsaly **několik záznamů
  hned za sebou** — a to se stejným obsahem i `t_unix`, protože `sample()` čte živé
  globály, ne historii.
- **Příčina:** `next += period` je správný vzor: drží kadenci **bez driftu**, protože
  nezávisí na tom, kdy se tik zrovna probudil. Má ale tichý předpoklad, že zpoždění
  je menší než perioda. Když není, změní se z „udržuj kadenci" na „doháněj", a u
  vzorkování to vyrobí **duplikáty předstírající měření v čase, kdy se neměřilo**.
  Allan rekonstruovaný z logu je pak vezme jako plnohodnotné vzorky s τ₀ = 10 s.
- **Oprava:** `next += period` zůstává, ale když je plán i po přičtení stále
  v minulosti, posune se **od teď** a zvedne se počítadlo zmeškaných period, které
  se vypisuje (`skip:N` ve `status`). Díra v logu je správná odpověď — tiše dohnané
  duplikáty jsou horší než přiznaná mezera.
- **Pravidlo:** **U každého plánu `next += period` odpověz, co se stane při zpoždění
  větším než perioda.** Buď se má dohánět (vzácné — typicky u počítání událostí), nebo
  se plán resetuje od teď a mezera se **spočítá a ohlásí**. „Dohánět" nikdy nevol
  mlčky u dat, která nesou časovou značku odvozenou od okamžiku zápisu.
- **Detekce:** Grep na `+= ` u plánovacích proměnných (`*_next_ms`, `*_next_tick`)
  a u každé se ptej: (1) kdo v téže úloze může blokovat déle než periodu, (2) co
  zapíše několik iterací hned po sobě. U vzorkovacích tiků platí i obráceně: když
  `sample()` čte živý stav, **nesmí** se volat víckrát pro jeden časový bod.
- **Commit:** viz git log — `fix(datalog)` s F-0102
- **Stav:** aktivní

---

### L-0070 — Kontrola, která hlásí nálezy i ve zdravém stromě, přestává být kontrolou

- **Datum:** 2026-09-19
- **Oblast:** předepsané kontroly v dokumentaci, detekce pastí
- **Symptom:** `CLAUDE.md` u pasti `fmt_fixed` předepisovalo: *„Kontrola:
  `grep -rn "fmt_fixed([^;]*, *[4-9])" CM7` musí být prázdný."* Ten grep vracel
  **6 shod a žádná z nich nebyla vada**: tři komentáře, které před tou pastí varují,
  a kopie v `Debug/`/`Release/`.
- **Příčina:** Kontrola se psala „odshora" jako regulární výraz nad zdrojem, ale
  neprošla si vlastním výstupem na zdravém stromě. Šum ji tím znehodnotil dvakrát:
  kdo ji spustí, **musí ručně probírat výsledky**, a jakmile to udělá dvakrát, přestane
  ji spouštět. Navíc ta konkrétní past má i druhou podobu (přetečení `int32` podle
  **hodnoty**, ne argumentu), kterou grep nad zdrojem najít vůbec nemůže.
- **Oprava:** Kontrola přesunuta do **běžícího přístroje**: funkce si mez vynucuje
  sama a každé omezení **počítá** (`status` → `FORMAT: omezenych desetin 0`). Číslo je
  buď nula, nebo je něco špatně — žádné probírání výsledků.
- **Pravidlo:** **Každou předepsanou kontrolu spusť na zdravém stromě a ověř, že je
  zelená.** Když není, není to kontrola, ale seznam ke čtení. A kdykoli jde místo grepu
  nad zdrojem použít **počítadlo v běžícím zařízení**, je to lepší: pokrývá i případy,
  které ze zdroje nejsou vidět (**L-0017**).
- **Detekce:** Projdi kontroly předepsané v `CLAUDE.md`/`docs/` a spusť je. Každá,
  která na čistém stromě vrátí nenulový výstup, je nález. U grepů nad zdrojem navíc
  vždy vyluč `Debug/` a `Release/` — jinak se každý nález počítá třikrát. Souvisí
  s **L-0039** (pozitivní kontrola musí obsahovat vadu, kvůli které vznikla)
  a **L-0016** (obrana, kterou nikdo nečte, není obrana).
- **Commit:** viz git log — `docs:` s F-0054
- **Stav:** aktivní

---

### L-0071 — Z kontextu výjimky se nezapisuje; ukládej přes paměť, která přežije reset

- **Datum:** 2026-09-20
- **Oblast:** hooky FreeRTOS, letový zapisovač, co přežije reset
- **Symptom:** `flightrec_dump()` volaný z `vApplicationStackOverflowHook`
  **deterministicky nezapsal nic**. Hook běží v kontextu výjimky PendSV
  (`xPortPendSVHandler` → `vTaskSwitchContext` → `taskCHECK_FOR_STACK_OVERFLOW`),
  kde `osMutexAcquire` vždy vrátí `osErrorISR` — funkce se na téže řádce vrátila.
  Letový zapisovač byl tedy slepý přesně pro ten scénář, kvůli kterému vznikl.
- **Příčina:** Sáhnout na flash z výjimky **nejde ani bez mutexu**: `w25q wait_ready()`
  uvnitř volá `osDelay(1)`, který v PendSV taky neprojde, takže by z toho byl spin
  až do IWDG resetu. Zapisovací cesta prostě **není z výjimky dosažitelná**, a žádné
  obcházení zámků to nezmění.
- **Oprava:** Dvoufázově — hook složí data do paměti (jen bajtové zápisy, žádný zámek,
  žádný `osDelay`) a do flash je vylije **po restartu** ta úloha, která na to má
  prostředí. 🔴 Klíčový detail: **`.bss` maže `Reset_Handler`**, takže staging v obyčejné
  RAM by se při resetu ztratil. Musí to být paměť, které se startup nedotkne — tady
  sekce `.sdram` (NOLOAD), jejíž obsah reset přežije (tentýž důvod, proč boot musí
  framebuffer memsetovat na černo).
- **Pravidlo:** **Kód volaný z hooku nebo ISR nesmí obsahovat nic, co ustupuje
  scheduleru nebo bere zámek** — a když má něco uložit, rozděl to na „slož do paměti"
  a „zapiš na médium", přičemž druhá část patří do úlohy. Paměť pro to předání musí být
  doložitelně **mimo `.bss`**, jinak ji smaže startup.
- **Detekce:** U každé funkce volané z hooku/ISR projdi její **celý** řetěz volání
  a hledej `osMutexAcquire`, `osDelay`, `osMessageQueue*`, `HAL_Delay` — stačí jeden
  a cesta je mrtvá. U staging paměti ověř v linkeru, do které sekce patří, a jestli ji
  startup nuluje. Doplňkově: výsledek předání musí být **vidět** (počítadlo „zachráněno
  po restartu"), jinak se nepozná rozdíl mezi „nestalo se nic" a „ztratilo se to"
  (**L-0017**). Souvisí s **L-0013** (hook běží v kontextu výjimky, RTOS API tam mlčky
  selže).
- **Commit:** viz git log — `fix(flightrec)` s F-0018
- **Stav:** aktivní

---

### L-0072 — Hromadnou úpravu dokumentů nedělej multiline regexem a vždy zkontroluj diffstat

- **Datum:** 2026-09-20
- **Oblast:** vlastní nástroje, dávkové úpravy `docs/`
- **Symptom:** Skript na hromadnou změnu řádků `- **Stav:**` v nálezových dokumentech
  použil `re.search` se vzorem `(### ID.*?)\n- \*\*Stav:\*\*[^\n]*(?:\n(?!###|---).*)*`.
  U prvního nálezu prošel, u druhého **smazal 631 řádků** souboru — kvantifikátor
  `(?:…)*` se rozjel přes celý zbytek dokumentu.
- **Příčina:** Multiline regex nad strukturovaným textem je **nekontrolovatelný**:
  „dokud nenarazíš na další nadpis" se v `re` píše snadno a chová se jinak, než člověk
  čte. Nic to nehlásí — regex „uspěje" a zahodí, co nemá.
  ⚠️ Druhá polovina problému: skript **nekontroloval, co udělal**. Kdybych po zápisu
  porovnal počet nadpisů nebo diffstat, poznal bych to okamžitě místo o krok později.
- **Oprava:** Přepsáno **po řádcích**: najdi index nadpisu, dopředu hledej první
  `- **Stav:**` a **zastav se na dalším `### `**, pak spotřebuj jen odsazené
  pokračovací řádky. Soubor se obnovil z gitu (byl committnutý, nic se neztratilo).
- **Pravidlo:** **Dávkovou úpravu strukturovaného textu piš po řádcích s explicitní
  hranicí sekce, ne multiline regexem** — a po každém zápisu ověř invariant, který
  změna zachovat má (počet sekcí, `git diff --stat`, počet řádků). Když skript umí
  smazat víc, než měl, musí to sám poznat.
- **Detekce:** Grep na `re.search`/`re.sub` s `re.S`/`(?s)` nebo `.*` přes řádky
  v nástrojích nad `docs/`. Před spuštěním takového skriptu nad víc soubory ho pusť
  na **jeden** a podívej se na `git diff --stat`; u dávkových úprav si nech vypsat
  počet změněných řádků na soubor.
- **Commit:** viz git log — `fix(flightrec)` s F-0018 (skript vznikl při té dávce)
- **Stav:** aktivní

---

### L-0073 — Fokus na kraji seznamu se zastavoval, místo aby se zacyklil

- **Datum:** 2026-09-20
- **Oblast:** UI, encoder (`app_gpsdo_handle_encoder`)
- **Symptom:** Uživatel nahlásil „v hlavní nabídce jde blbě encoder". Nejdřív jsem
  podezíral HW vrstvu (zákmity, chybný dělič) — opakované měření `enc` (přesně
  1 západka/s) ale ukázalo čistou schodovitou řadu +1/krok, žádné zákmity,
  `delic=4` správně. Chyba tedy nebyla v počítání kroků.
- **Příčina:** `app_gpsdo.c:8529-8531` počítal nový index fokusu `s_focus + ev.steps`
  a **ořezával** ho na `[0, n-1]` (`if (nf < 0) nf = 0; if (nf >= n) nf = n - 1;`).
  Na krátkém seznamu (MENU = 4 dlaždice + 3 tlačítka patky = 7 prvků) to znamená, že
  otočení za poslední/první prvek se prostě zastaví — uživatel musí otočit zpátky
  celou cestu, což se subjektivně jeví jako „mrtvý směr" nebo vadný encoder.
- **Oprava:** Ořez nahrazen modulem: `nf = (s_focus + ev.steps) % n; if (nf < 0) nf += n;`
  (`app_gpsdo.c:8528-8535`). `n > 0` je už zaručeno guardem o pár řádků výš.
- **Pravidlo:** **Fokus v cyklickém seznamu (menu, karusel) se má ZACYKLIT, ne
  zarazit na kraji** — ořezávací clamp na indexu, který reprezentuje pozici
  v uzavřeném seznamu položek, je skoro vždy špatné chování; správně patří modulo.
  Než se HW podezírá ze zákmitů/chybného děliče, ověř kontrolovaným měřením
  (SKILL §0), že vada skutečně leží tam — v tomto případě neležela.
- **Detekce:** Grep `if (nf < 0)` / `if (.*< 0.*=.*0.*>=.*n.*=.*n *- *1` v souborech
  s `s_focus`/`menu_list_t` — kandidát na clamp místo wrapu v cyklickém seznamu.
- **Commit:** (nekomitováno v době zápisu — viz git log následující commit)
- **Stav:** aktivní

---

### L-0074 — Volání, které jen MĚNÍ STAV, ale nikdy neKRESLÍ, vypadá jako mrtvý vstup

- **Datum:** 2026-09-20
- **Oblast:** UI, encoder (`app_gpsdo_handle_encoder`), obecně partial-redraw architektura
- **Symptom:** Zkoušel jsem opravit hlášení „zaseknul se na channel a nehýbe se pomocí
  encoderu" přidáním speciální větve, kde otáčení na GATE/CHAN volalo
  `screen_main_button_action(bi)` opakovaně. Uživatel hned nato hlásil: „jde tlačítko,
  ale otáčení nereaguje" — tedy hodnota v `st.gate`/`st.chan` se měnila (ověřitelné
  jinudy), ale na displeji se nic neukázalo.
- **Příčina:** `screen_main_button_action()` je **čistá state-mutace** — nastaví
  `st.gate`/`st.chan`/`g_ui_cfg_dirty` a nic víc. Dotyková cesta
  (`app_gpsdo_handle_touch`) po ní VŽDY volá i `prim_set_target`+`prim_reset_clip`+
  `screen_main_redraw_button`+`screen_main_button_flash_start`+`screen_main_redraw_title`+
  `present_now()` — tenhle redraw balík je oddělený od samotné akce a nikde
  vynucený signaturou ani komentářem. Nová volající vrstva (encoder) zavolala jen
  akci, ne redraw, a nic ji na to neupozornilo — ani build, ani audit (žádná z těch
  funkcí nevrací chybu, když se nezavolá).
- **Oprava:** Zrcadlit CELOU dotykovou redraw sekvenci (ne jen samotnou akci) na
  nové volací cestě. V tomto konkrétním případě šla vlastnost `otáčení mění hodnotu
  GATE/CHAN přímo` nakonec **celá pryč** (uživatel po vyzkoušení na HW rozhodl, že
  rotace na hlavní obrazovce má vždy jen listovat fokusem, ne měnit hodnotu —
  zmena hodnoty otáčením patří jen do vyhrazených číselných polí typu IP oktet).
  Missing-redraw bug tím zmizel spolu s celou větví, ale vzorek zůstává platný
  pro příští podobné volání.
- **Pravidlo:** **Když se v kódu objeví `<akce>()` bez doprovodného volání redraw
  funkce, kterou VŠECHNY OSTATNÍ cesty k téže akci volají, je to podezřelé — najdi
  všechny volající téže state-mutující funkce a porovnej, co dělají navíc.**
  Funkce, která jen mění stav a nikdy nekreslí, by měla mít v komentáři u definice
  jasně napsáno „NEKRESLÍ, volající musí redraw udělat sám" (`screen_main_button_action`
  ho nemělo).
- **Detekce:** Grep na `screen_main_button_action(` (nebo obdobné `*_action`/`*_apply`
  state-mutátory) — u KAŽDÉHO volání zkontroluj, že v okolí je i redraw. Obecněji:
  když nová volací cesta k existující funkci vznikne, diffni ji proti VŠEM
  ostatním volajícím téže funkce, ne jen proti nejbližšímu příkladu.
- **Commit:** (nekomitováno v době zápisu — viz git log následující commit)
- **Stav:** aktivní

---

### L-0075 — Ověřování požadavku PO ODESLÁNÍ musí číst návrat, ne stav vlastníka hned potom

- **Datum:** 2026-09-20
- **Oblast:** cross-task request/apply vzor (`encoder_set_div`), obecně F5 fix-phase
  verifikace na HW
- **Symptom:** Při verifikaci F-0125 (fix z 2026-09-19, přechod `encoder_set_div()`
  na request-pattern) na reálné desce: `enc div 2` odpověděl `ENC: neplatny delic
  (povoleno 1, 2, 4); zustava 4` — přestože 2 je platná hodnota. Následný `enc div 4`
  odpověděl `... zustava 2` (napovědělo, že se `2` mezitím přece jen uplatnilo).
  `enc` pak ukázal `delic=4` — obě volání ve skutečnosti PROŠLA, jen o tom UART
  lhal pokaždé, když se hodnota doopravdy měnila.
- **Příčina:** `freertos_task_uart.c` po `encoder_set_div(d)` okamžitě porovnával
  `d == encoder_div()`. `encoder_set_div()` ale od F-0125 jen ZAFRONTUJE `s_div_req`;
  skutečné `s_div` (co čte `encoder_div()`) mění až `encoder_poll()` v UiTasku o poll
  později. Srovnání tedy vždy vidělo STAROU hodnotu — funkční je jen náhodou, když
  se nová hodnota rovná staré (typicky nikdy, protože proč by uživatel nastavoval
  to, co už je nastavené).
- **Oprava:** `encoder_set_div()` teď VRACÍ, jestli `d` bylo platné (`int`, 1/0),
  místo aby volající hádal ze zpožděné asynchronní hodnoty. Volající (`enc div`)
  čte návrat, ne `encoder_div()`. Druhý volající (`syscfg_load()`) návrat ignoruje
  (nepotřebuje ho — spoléhá na tichý no-op interní validace, jak už dřív dělal).
- **Pravidlo:** **Když se synchronní mutace převede na frontový požadavek pro
  cizí úlohu (L-0061), KAŽDÝ volající, který si po volání ověřoval výsledek čtením
  stavu vlastníka, se MUSÍ přepnout na návratovou hodnotu funkce (nebo jiný
  synchronní signál) — čtení stavu vlastníka hned po zafrontování čte starou
  hodnotu skoro jistě.** Tahle třída chyby je neviditelná v recenzi kódu (obě
  volání vypadají rozumně samostatně) a build/audit ji nechytí (typy sedí) —
  odhalí ji jen skutečné vyvolání na HW se sledováním výsledku.
- **Detekce:** Po každém převodu funkce na request-pattern (F5.3: „vzor
  `g_membench_req`") vypsat VŠECHNY volající PŮVODNÍ (synchronní) verze a u
  každého zkontrolovat, jestli si po volání čte stav zpět OKAMŽITĚ. Grep:
  volání `X_set_*` následované do pár řádků čtením `X_get_*`/gettru stejného pole.
- **Commit:** (nekomitováno v době zápisu — viz git log následující commit)
- **Stav:** aktivní

---

### L-0077 — U sdílené SDRAM rozhoduje VZOR přístupu, ne objem dat (strided kopie hladoví LTDC)

- **Datum:** 2026-09-22
- **Oblast:** DMA2D/LTDC, SDRAM, rendering
- **Symptom:** Hlavní obrazovka **pokaždé** problikla poškozeným snímkem při
  stisku RUN/STOP. Plný render celé obrazovky (`ui`) byl přitom vždy **čistý**
  (0 podtečení), i když přenáší 768 kB — tedy podstatně víc dat než to malé
  překreslení, které problikávalo. Ladění `d2ddt` (mrtvý čas DMA2D) na strop
  255 ani zúžení překreslované zóny to neodstranilo.
- **Příčina:** `copy_forward_dedup()` kopíroval jednotlivé dirty obdélníky, tedy
  `d2d_blit_ex` se šířkou < šířky framebufferu → `FGOR`/`OOR` ≠ 0 =
  **strided přístup**: každý řádek kopie začíná v jiné SDRAM řadě. Přepínání řad
  sebere propustnost LTDC, které čte snímek na panel sekvenčně, jeho FIFO
  podteče a na panel jde poškozený snímek. Plná kopie má `FGOR`=`OOR`=0, jede
  **lineárně** a je proto levnější navzdory většímu objemu.
- **Oprava:** copy-forward kopíruje **plnošířkové pásy slité po ose Y**
  (`prim_stm32_hal.c`, `copy_forward_dedup`). Naměřeno **38 → 0** podtečení na
  20 stisků RUN/STOP.
- **Pravidlo:** **U sdíleného paměťového rozhraní posuzuj VZOR přístupu, ne jen
  objem přenesených dat.** Víc bajtů lineárně bývá levnější než míň bajtů
  skákavě — a proto „zmenši, co překresluješ" nemusí pomoct vůbec, kdežto
  „kopíruj to samé lineárně" pomůže úplně. Platí i obráceně jako diagnostický
  test: **když VĚTŠÍ operace neproblikává a MENŠÍ ano, přestaň hledat v objemu
  a podívej se na stride.**
- **Detekce:** `fbdiff` → řádek `podteceni po fazich` musí mít u
  `COPY-FORWARD` nulu. Reprodukce bez prstu: `tap 1` (injektor doteku).
  ⚠️ Nové místo, které volá `d2d_blit_ex` s šířkou < `FB_W` v horké cestě
  souběžně se scan-outem, si tuhle vadu přinese znovu.
- **Commit:** (nekomitováno v době zápisu)
- **Stav:** aktivní

---

### L-0076 — Neměř zátěžovým burstem to, co uživatel dělá jednotlivě; a měř FÁZE, ne jen součet

- ⚠️ **REVIDOVÁNO 2026-09-22.** Původní znění (níže) vzniklo, když jsem si
  myslel, že jde o fyzický strop propustnosti sběrnice, protože žádná ze
  softwarových pák nedávala nulu. **Byl to špatný závěr** — příčina byla
  strided vzor přístupu v copy-forwardu (viz **L-0077**) a nula šla dosáhnout.
  Platná část lekce je metodická, ne technická:
  🔑 **Součtové počítadlo („kolik podtečení celkem") neřekne, KDE vada vzniká.**
  Dokud se podtečení četlo jen jednou za flip, každá teorie (velikost burstu,
  glyph akcelerace, mrtvý čas, obnova SDRAM, fáze vůči vblanku) se dala
  obhájit i vyvrátit. Rozhodl až **rozklad na fáze** (`kresleni | cekani |
  flip | copy-forward`), který ukázal `0 | 0 | 0 | 38` a všechny předchozí
  hypotézy naráz smetl.
  🔑 **A druhá: co nejde reprodukovat bez uživatele, se ladí strašně draho.**
  Vada šla jen fyzickým dotekem (vzdálené SCPI ji nedělá), takže každé měření
  stálo jedno kolo konverzace — dokud nevznikl injektor `tap <idx>`, který
  volá tutéž cestu ze stejného místa smyčky. Ten měl vzniknout **hned**, ne
  po hodinách.

**Původní (mylné) znění:** Ladicí konstanta i „šíř clear region" mají STROP daný sdílenou SDRAM sběrnicí, ne kódem

- **Datum:** 2026-09-21
- **Oblast:** DMA2D/LTDC, rendering, ladicí konstanty
- **Symptom:** Uživatel hlásil problikávání hlavní obrazovky při RUN/STOP.
  Naměřeno (F-0140): burst RUN/STOP toggle korelovaně zvedal
  `status` → `LTDC: podtečení FIFO` (52/1000 při d2ddt=240). Zúžení DMA2D
  přenosu (přesná `freq_area()` místo "maximální možné" `freq_clear_area()`
  + vypnutí HW glyph akcelerace pro tenhle konkrétní redraw) snížilo poměr na
  27/1000 (d2ddt=240) resp. 17/1000 (d2ddt=255, strop registru) — **reálné,
  změřené zlepšení, ale ne nula**. Izolovaný JEDEN toggle (skutečné použití,
  ne umělý burst) byl **čistý i PŘED touto opravou** (0–2 podtečení na
  tisíce flipů).
- **Příčina:** Podtečení FIFO LTDC je **sdílené pásmo SDRAM sběrnice**
  (DMA2D burst soutěží s nepřetržitým čtením scanline LTDC) — je to fyzikální
  strop desky, ne chyba v kódu. Zúžení přenosu (méně bajtů) i mrtvý čas
  DMA2D (`d2ddt`, max 255 = strop 8bitového registru `AMTCR.DT`) ho jen
  **posouvají**, nikdy neodstraní: i kombinace obou u tohohle konkrétního
  redrawu zůstala na ~17/1000 pod umělým burstem.
- **Oprava:** `screen_main_redraw_freq_tint()` — nová, užší varianta pro
  RUN/STOP toggle (geometrie čísla se nemění) vedle `screen_main_redraw_freq_area()`
  (`CM7/app/screens/screen_main.c:2694-2790`, volající `app_gpsdo.c:7834,8642`).
  Zlepšení je **skutečné a bezrizikové** (menší přenos, žádná logická změna
  chování), ale **není to "úplná" oprava** — zbytkové riziko pod extrémním
  burstem zůstává, protože strop je fyzikální. Reálné jednotlivé použití
  (fyzický dotek) bylo čisté už PŘED opravou u SCPI ekvivalentu — otevřená
  otázka, jestli LTDC podtečení vůbec vysvětluje to, co uživatel vidí na
  fyzickém doteku (nebylo možné ověřit bez skutečného doteku displeje).
- **Pravidlo:** **Když je nález korelovaný s fyzikálním sdíleným prostředkem
  (sběrnice, hodiny, napájení), žádná kombinace SW pák nemusí dát nulu —
  změř VŽDY nejlevnější/nejrealističtější scénář (jeden dotek, ne umělý
  burst) PŘED tím, než umělý stresový test prohlásíš za reprezentativní pro
  hlášený symptom.** Umělý burst je nástroj na ODHALENÍ jevu, ne na potvrzení,
  že vysvětluje TOTO konkrétní hlášení uživatele.
- **Detekce:** Žádná automatická — vyžaduje reálné měření na HW (`status` →
  `LTDC: podtečení FIFO`) v obou scénářích (jeden dotek vs. burst) při každém
  budoucím podezření na "problikávání" korelované s DMA2D/LTDC.
- **Commit:** (nekomitováno v době zápisu)
- **Stav:** aktivní

---

### L-0078 — `sd_blocking_begin()` snižuje PRIORITU volajícího tasku; obalovat jen JEDEN zápis, ne smyčku s cross-task čekáním

- **Datum:** 2026-09-23
- **Oblast:** RTOS, SD karta, cross-task komunikace
- **Symptom:** Nový UART příkaz `screenshot all` (export všech ~50 oken UI na SD,
  každé jako vlastní BMP) na desce **zaseklo přístroj na 6+ minut** — neodpovídal
  ani na `ping` (triviální, bez SD/kreslení). Zotavilo se to až po **reflashi +
  softwarovém resetu**; halt sondou k diagnostice nebyl použit (viz pravidlo
  o mrtvé I2C4 — zbytečně riskantní pro tenhle případ).
- **Příčina:** `sd_blocking_begin()` (`sd_export.c`) není jen příznak — **sníží
  prioritu VOLAJÍCÍHO tasku na `osPriorityLow`** po dobu držení (obrana proti
  zaseknutému SD HAL, aby zaseknutí nezabralo displej/dotyk/watchdog). Existující
  volání (`screenshot sd` aj.) ho drží jen kolem JEDNOHO blokujícího zápisu
  (~1-3 s) — bezpečné. Nový kód ho ale držel kolem **CELÉ smyčky 49 oken**,
  včetně fáze, kdy UartTask čeká (`osDelay(5)` polling) na to, až UiTask
  vykreslí požadované okno přes `g_shot_view_req`/`g_shot_view_done` handshake.
  UartTask tak strávil několik minut na sníženou prioritu, zatímco na něm
  ZÁROVEŇ záviselo dokončení cross-task predávání — přesně ten typ kombinace,
  co RTOS scheduler nemá důvod řešit rychle.
- **Oprava:** `sd_blocking_begin()/end()` obaluje **jen samotné volání
  `screenshot_save_sd_named()`** (per soubor), ne čekání na UiTask ani celou
  smyčku — stejný rozsah jako u všech ostatních volajících v projektu.
  Ověřeno na desce: 3 okna OK, 40 oken OK (vč. SD KARTA/DATALOG/SESTAVY/
  BENCHMARK — podezřelá kvůli vlastnímu sahání na SD/QSPI), **49/49 OK**,
  žádné zaseknutí.
- **Pravidlo:** **Funkce, která mění vlastnosti VOLAJÍCÍHO tasku (prioritu,
  masku přerušení…) kvůli JEDNÉ krátké operaci, se nesmí obalit kolem širší
  smyčky — a obzvlášť ne kolem smyčky, která sama čeká na JINÝ task.** Než
  se `_begin()/_end()` pár použije v novém kontextu, ověřit, co přesně dělá
  (grep tělo, ne jen jméno) — jméno `sd_blocking_begin` naznačuje "necham
  bezet SD blokujici operaci", ne "snizim si prioritu na minuty".
- **Detekce:** Žádná automatická — realisticky jen **testovat přírůstkově**
  (malý vzorek → střední → plný rozsah), přesně jak se to nakonec udělalo
  (a mělo se to udělat rovnou, ne až po jednom zaseknutí).
- **Commit:** (nekomitováno v době zápisu)
- **Stav:** aktivní

---

### L-0079 — Baseline pro "první pozorování není hrana" musí čekat, až se ustálí DEBOUNCE, ne jen na první tik volajícího

- **Datum:** 2026-09-23
- **Oblast:** SD karta (card-detect), zvukový alarm, boot
- **Symptom:** Nový dvouton při vložení/vyjmutí SD karty (`alarm_sd_card`) pípal
  "vložení" i po **power resetu s kartou už zasunutou** — přesně to, co měl
  guard "první pozorování po bootu není hrana" zabránit, a podle kódu (i podle
  prvního testu přes SW reset debuggeru) vypadal jako funkční.
- **Příčina:** Guard bral baseline (`s_snd_prev = present`) na **prvním volání
  `sd_export_tick()`** — jenže `present` (`datalog_sd_card_present()`) je sám
  o sobě **debouncovaný** (`SD_DET_STABLE_N`=3 tiky) a po bootu vždy začíná na
  "nepřítomna" (`s_det_stable`=0 v BSS), bez ohledu na to, jestli je karta
  fyzicky uvnitř. Debounce se na skutečnou "přítomna" dorovná až za 3 tiky
  tohoto volání (~1,5 s). Baseline vzatá na 1. tiku tedy VŽDY zachytí
  "nepřítomna", a jakmile debounce o pár tiků později dožene realitu, guard to
  vidí jako hranu nepřítomna→přítomna a pípne — při KAŽDÉM bootu s vloženou
  kartou, nezávisle na typu resetu.
  ⚠️ Testování přes SW reset (`STM32_Programmer_CLI -rst`) tuhle chybu
  neprokázalo ani nevyvrátilo — bez sluchu na desce jsem si "ticho" jen
  domyslel z toho, že `status`/`datalog mirror` pár sekund po resetu vypadaly
  v pořádku. Až skutečný power-cyklus (uživatel) odhalil, že pípnutí je pořád
  tam.
- **Oprava:** Guard teď má vlastní **zpožďovací okno `SD_DET_STABLE_N+1` tiků**
  (`s_snd_grace`), po které se hrana vůbec nevyhodnocuje — jen se `s_snd_prev`
  každý tik přepisuje aktuální hodnotou. Tím je po uplynutí okna debounce jistě
  ustálený a `s_snd_prev` drží SPRÁVNOU tichou baseline (ať je karta přítomná,
  nebo ne). `SD_DET_STABLE_N` přitom bylo doteď `#define`ováno jen uvnitř
  `datalog_sd.c` — přesunuto do `datalog.h` jako jeden sdílený zdroj, aby
  `sd_export.c` nezavedl druhou nezávislou "3" (viz i L-0070 duch téhož problému
  jinde).
- **Pravidlo:** **"Nehodnoť hranu na prvním pozorování" nestačí, když je
  sledovaná hodnota SAMA odvozená z debounce/filtru s vlastním zpožděním.**
  Baseline se smí vzít až PO tom, co uplyne alespoň tolik tiků, kolik ten
  filtr potřebuje k ustálení — jinak baseline zachytí přechodný stav filtru,
  ne skutečnost. Platí obecně, ne jen pro tenhle guard.
- **Detekce:** Žádná automatická — filtr/debounce vypadá zdravě i v jednotkovém
  testu nad ustáleným vstupem; potřeba je test PŘES BOOT se vstupem, který je
  od začátku "true" (karta vložená před zapnutím), a to nejlépe skutečným
  power-cyklem, ne SW resetem (viz L-0078 sekce o SW vs. power reset jinde
  v projektu).
- **Commit:** (nekomitováno v době zápisu)
- **Stav:** aktivní

---

### L-0080 — Vypnutí funkce, která drží otevřený soubor, musí soubor i ZAVŘÍT, ne jen nastavit příznak

- **Datum:** 2026-09-24
- **Oblast:** SD karta / FatFs, datalog zrcadlo
- **Symptom:** Při opravě F-0142 (čtení posledního řádku souboru při znovuotevření
  na téže kartě) se přidal `FA_READ` k dosavadnímu `FA_WRITE` v `f_open()`. Hned
  při prvním ověřovacím testu na HW (`datalog mirror off` → `on`) začalo
  opětovné otevření selhávat (`"otevreni souboru selhalo"`), přestože stejná
  sekvence předtím v této session opakovaně fungovala.
- **Příčina:** `datalog_mirror_set_enabled(false)` (`datalog_sd.c`) od svého
  vzniku jen nastavovala `s_mirror_open = false`, **nikdy nevolala `f_close()`**
  na `s_mirror_fil`. FatFs tak dál vedla soubor jako otevřený týmž objektem;
  dokud se znovu otevíral jen s `FA_WRITE`, kolize se neprojevila. Přidání
  `FA_READ` (kombinovaný režim čtení+zápis) narazilo na zámek, který ten
  zapomenutý, nikdy nezavřený objekt pořád držel — a `f_open()` na stejné
  jméno selhal.
- **Oprava:** `datalog_mirror_set_enabled(false)` teď při `s_mirror_open`
  zavolá `f_close(&s_mirror_fil)` (obaleno `sd_blocking_begin/end` +
  `sd_export_busy_begin/end` — volající je UartTask, blokování je v pořádku).
  `datalog_sd.c:477-495` (F-0146).
- **Pravidlo:** **Přepínač "vypnuto/zapnuto" nad zdrojem, který drží HANDLE
  (soubor, socket, periferie), musí handle při vypnutí uvolnit — samotné
  shození příznaku nestačí, i když se to chvíli nemusí projevit.** Chyba může
  zůstat skrytá, dokud se nezmění NĚCO JINÉHO (tady: přidaný `FA_READ`), co
  na existenci uniklého handle najednou narazí — testuj vypnutí/zapnutí
  vždy PO každé změně otvíracích příznaků, ne jen při prvním napsání kódu.
- **Detekce:** Žádná automatická — projeví se jen funkčním testem sekvence
  zapnuto→vypnuto→zapnuto na reálném FatFs svazku.
- **Commit:** (nekomitováno v době zápisu)
- **Stav:** aktivní

---

### L-0081 — Oprava napsaná hned vedle právě opraveného anti-vzoru ho dokázala zopakovat o pár řádků níž

- **Datum:** 2026-09-24 (nalezeno), 2026-09-25 (opraveno)
- **Oblast:** SD karta / FatFs, datalog zrcadlo — přímé pokračování L-0080
- **Symptom:** V TÉŽE session, v TOMTÉŽ commitu (`caa5f70`), který opravil
  F-0146 (`datalog_mirror_set_enabled(false)` nezavíralo `s_mirror_fil`),
  vznikla NOVÁ funkce (`datalog_mirror_service()`, blok F-0143 — detekce
  smazání W25Q logu), která **stejný anti-vzor zopakovala** — nastavila
  `s_mirror_open = false` bez `f_close()`, o pár desítek řádků výš ve
  stejném souboru, než kde byl ten samý vzor právě opraven.
- **Příčina:** Oprava F-0146 se soustředila na MÍSTO, kde se bug projevil
  (disable větev), ne na VZOR samotný. Nová funkce (F-0143) vznikla
  paralelně, ve stejném pracovním kroku, ale nebyla proti tomu vzoru
  zkontrolována — přesně to, před čím varuje `/audit-modul` §F5.3
  ("zkontroluj vlastní zásah na tutéž třídu vady, kterou opravuješ"),
  jenže tady nešlo o zásah do STEJNÉ funkce, ale o SOUBĚŽNĚ psanou jinou
  funkci v tomtéž souboru a commitu — širší dosah, než na jaký F5.3 svým
  doslovným zněním míří.
  Odhaleno až následným, nezávislým průchodem `/audit-modul` nad HOTOVÝMI
  opravami (ne nad původní vadou) — teprve čtení kódu s otázkou "kde jinde
  se ten samý vzor mohl zopakovat" ho našlo; funkční UART test (off/on
  toggle) na to nedosáhl, protože triggerem je jiná akce (`datalog erase`).
- **Oprava:** Stejný vzor jako F-0146 — `f_close(&s_mirror_fil)` (uvnitř
  `if (s_mirror_open)`) před resetem příznaků, `sd_blocking_begin/end`
  scope rozšířen tak, aby ho zahrnul. `datalog_sd.c:717-747`.
- **Pravidlo:** **Když opravíš anti-vzor na jednom místě, PROHLEDEJ CELÝ
  SOUBOR (ne jen okolí opravy) na stejný vzor — zvlášť pokud v TÉŽE úpravě
  vzniká NOVÝ kód nad STEJNÝM sdíleným stavem** (tady: `s_mirror_open`/
  `s_mirror_fil`). `grep` jménem proměnné, co se do ní zapisuje `false`
  bez sousedního `f_close`, je levná, mechanická kontrola, která by tohle
  chytla přímo při psaní — ne až při druhém, samostatném auditu o den
  později.
- **Detekce:** `grep -n "s_mirror_open = false"` a u KAŽDÉHO výskytu ověřit,
  že mu bezprostředně předchází `f_close()` NEBO je vedle komentář
  zdůvodňující, proč je to bezpečné bez něj (přesně tenhle kontrast —
  3 ze 4 míst mělo jedno nebo druhé, jedno nemělo nic — odhalil nález).
- **Commit:** (nekomitováno v době zápisu)
- **Stav:** aktivní

---

### L-0082 — Zámek, který bere jen jedna strana, nevylučuje nic — a premisa, která vyloučila podezřelého, se musí ověřit proti pořadí bootu

- **Datum:** 2026-09-25 (nalezeno i opraveno, audit modulu 23, F-0149)
- **Oblast:** sdílená GPIO mezi CM7 a CM4, `fmc.c` obranné potvrzení PG8
- **Symptom:** Nevysvětlený přepis `PG8` (`FMC_SDCLK`) do režimu ANALOG →
  SDRAM bez hodin → černý displej po power-cyklu a `membench` 10 551 639
  chybných bitů. Obrana proti tomu existovala (ruční potvrzení pinu ve
  `MX_FMC_Init`), ale příčina se roky nenašla.
- **Příčina — dvě vrstvy, obě v téže dvacítce řádků:**
  1. **Jednostranný zámek.** CM4 kolem všech svých `MX_*_Init` (a ty
     konfigurují `GPIOG`: PG7, PG11, PG13, PG14) poctivě drží HSEM 1
     (`CM4/main.c:175-193`). CM7 ve FMC cestě ho nebral vůbec. `HAL_GPIO_Init`
     přitom dělá nad `MODER`/`AFR` **neatomický read-modify-write**, takže
     vzájemné vyloučení, které drží jen jedna strana, je **žádné vzájemné
     vyloučení**. Obrana proti ztrátě PG8 tu ztrátu sama umožňovala — a v
     opačném směru mohla sebrat `AFR` pinu PG11 (`ETH_TX_EN`, deska bez IP).
  2. **Vyloučení podezřelého na premise, kterou kód popírá.** Komentář
     u té obrany vyloučil CM4 větou *„Stav byl analog uz ~200 ms po resetu,
     tedy PRED bootem CM4"*. Jenže CM4 se budí uvolněním HSEM 0 v
     `Boot_Mode_Sequence_2` (`main.c:333-335`), zatímco `MX_FMC_Init()` běží
     až z `main.c:382` — **CM4 startuje dřív, ne později.** Nejpravděpodobnější
     kandidát byl tím vyškrtnut a pátrání se zastavilo u obrany místo příčiny.
- **Oprava:** `gpio_cfg_lock()` / `gpio_cfg_unlock()` kolem bloku
  (`fmc.c`, `USER CODE FMC_Init 2`) + premisa v komentáři uvedena na pravdu.
  🔑 **Stojící výmluva u `gpio_cfg_lock` neplatila:** komentář tam říká, že
  generované `MX_*_Init` nelze regen-safe obalit — to platí pro
  `HAL_FMC_MspInit`, **ne** pro ruční blok, který leží v `USER CODE`.
- **Pravidlo:** **U každého zámku nad sdíleným prostředkem vyjmenuj VŠECHNY,
  kdo ho musí brát, a ověř, že ho berou — zámek držený jednou stranou je
  horší než žádný, protože vypadá jako ochrana.** A **každou větu, která
  vylučuje podezřelého z časových důvodů („to bylo dřív/později než X"),
  ověř proti skutečnému pořadí volání, ne proti dojmu** — u dvoujádrového
  bootu je pořadí neintuitivní a dá se přečíst ze zdrojáku za minutu.
- **Detekce:** pro každý port, na který sahají obě jádra, `grep` na
  `HAL_GPIO_Init(GPIO<X>` napříč **oběma** projekty a u každého výskytu
  ověřit `gpio_cfg_lock()` v okolí (nebo zdůvodnění, proč tam být nemůže).
  Nenulový `GPIO HLIDAC` ve `status` = závod opravdu probíhá.
- **Commit:** viz `fix(F-0149)`
- **Stav:** aktivní

---

### L-0083 — Když sjednotíš zdroj pravdy, kontrola, která hlídala rozchod, se stane tautologií — a nikdo si toho nevšimne

- **Datum:** 2026-09-25 (nalezeno i opraveno, audit modulu 23, F-0151)
- **Oblast:** `fmc.c` / `fmc.h`, `REFRESH_COUNT`
- **Symptom:** `_Static_assert(REFRESH_COUNT == REFRESH_COUNT_EXPECTED,
  "fmc.h se rozeslo s fmc.c")` vypadal jako pojistka na nejnebezpečnější
  konstantě v projektu. Po preprocesoru z něj ale bylo `371 == 371`:
  **nemohl selhat nikdy.**
- **Příčina:** Assert vznikl správně — v době, kdy byla hodnota **ručně
  zdvojená** ve `fmc.c` i `fmc.h`, a tehdy rozchod skutečně hlídal. Pak přišla
  správná oprava (sjednocení na jediný zdroj, `#define REFRESH_COUNT
  REFRESH_COUNT_EXPECTED`) — a tím **zmizela vada, kterou assert hlídal**,
  zatímco assert zůstal i s hláškou, která tvrdí, že ji hlídá dál.
  Je to zrcadlo L-0070: tam kontrola hlásila nález i ve zdravém stromě,
  tady nehlásí nikdy nic. Obojí přestává být kontrolou.
- **Oprava:** Assert nahrazen mezí, která **selhat může** a hlídá vadu, jež
  reálně hrozí — rozchod hodnoty s **taktem SDCLK** (`REFRESH_COUNT_SPEC_MAX`
  ve `fmc.h`). Mez je jednostranná záměrně: obnovovat se smí častěji, nikdy
  řidčeji. Pozitivní kontrola (L-0039) proběhla na **obou** vadách, kvůli
  kterým vznikla: historická hodnota `1835` → build spadl, SDCLK 50 → 25 MHz
  → build spadl, nedotčený strom → prošel.
- **Pravidlo:** **Když odstraníš duplicitu, projdi kontroly, které ten rozchod
  hlídaly — buď je smaž, nebo je přesměruj na vadu, která po té změně ještě
  existuje.** Kontrola, jejíž obě strany se staly týmž symbolem, je mrtvá.
  🔑 A obecněji: **u každého `_Static_assert` si polož otázku „jaká konkrétní
  změna ho shodí?" — když na ni neumíš odpovědět příkladem, není to pojistka,
  ale dekorace.** Odpověď patří do komentáře u něj.
- **Detekce:** u `_Static_assert(A == B)` ověřit, že `A` a `B` mají **různé
  zdroje**; pokud je jedno `#define`ované druhým, je to tautologie. Obecně:
  ke každé nové pojistce spustit pozitivní kontrolu a její výsledek zapsat.
- **Commit:** viz `fix(F-0151)`
- **Stav:** aktivní

---

### L-0084 — Anonymní zdržení v bootu drželo při životě ÚPLNĚ JINÝ subsystém, než u kterého stálo; modulový audit ho ověřit nemohl

- **Datum:** 2026-09-25 (nalezeno, opraveno, vráceno — audit modulu 23, F-0152)
- **Oblast:** `fmc.c` `USER CODE FMC_Init 2`, boot časování, USB CDC konzole
- **Symptom:** Po odstranění bezpodmínečného `HAL_Delay(200)` (plus blikání
  LED_1) z `MX_FMC_Init` **přestala po STUDENÉM STARTU odpovídat USB CDC
  konzole**. Zařízení se vyenumerovalo se správným `VID/PID 0483:5740`, ale
  data netekla při žádné kombinaci DTR/RTS. Po SW resetu se konzole **vždy**
  vrátila → rozdíl byl výlučně ve studeném startu.
- **Proč to audit ani ověření nechytily — tohle je jádro lekce:** návrh smazat
  to byl **dobře odůvodněný**. Zdržení nemělo v kódu žádné vysvětlení, vypadalo
  jako pozůstatek z bring-upu a `CLAUDE.md` pravidlo 4c blokující zdržení v boot
  cestě před bring-upem displeje zakazuje jmenovitě. Nález navíc **předem
  označil riziko** („nelze vyloučit, že je omylem nosné") a předepsal ověření
  studeným startem. **A to ověření prošlo** — všechno, co modulový audit
  kontroluje, bylo v pořádku:
  `g_fmc_init_fail=0`, `g_display_init_step=0`, `g_cm4_absent=0`,
  `membench` 0 chybných bitů a retence 0, `bgcheck` BEZE ZMĚNY,
  `GPIO HLIDAC` 0, `uptime` normálně rostl.
  **Rozbil se subsystém, který s auditovaným modulem nemá nic společného** —
  a proto se na něj v kontrolním seznamu nikdo nedíval.
  🔑 A rozbil se **zrovna ten, kterým se všechno ostatní měří.** Kdyby mě
  nenapadlo sáhnout po sondě, vypadalo by to jako „deska po power-cyklu
  nenaběhla" — přitom běžela úplně normálně.
- **Příčina:** neznámá; komentář u konstanty to **přiznává jako HYPOTÉZU**
  (rozběh napájení? enumerační okno vůči hostu?). Jistá je jen ta závislost,
  doložená kontrolovaným pokusem s jedinou změněnou proměnnou:
  bez zdržení + power-on → konzole mlčí (opakovaně); se zdržením + power-on →
  `Reset: power-on`, `uptime 22s`, `ping` → `pong`.
  Druhý kandidát (bouře chybových ISR z F-0153) **vyloučen měřením, ne úvahou**:
  `g_uart1_rearm_fail` i `g_gps_rx_drop` byly 0 a nerostly.
- **Oprava:** zdržení vráceno jako pojmenovaná konstanta
  `FMC_POST_INIT_SETTLE_MS` se zdůvodněním a s výsledky měření. Blikání LED_1
  vráceno **nebylo** — na časování nemá vliv a LED_1 je výstup `bootled` pro
  hlášení poruch, takže se pletlo se vzorem poruchy.
- **Pravidlo:** **Absence zdůvodnění u zdržení NENÍ důkaz, že je zbytečné.**
  Anonymní `HAL_Delay` v bootu je podezřelý, ale podezření se uzavírá
  **měřením, ne úsudkem o čistotě kódu**.
  🔴 A hlavně: **když měníš časování bootu, ověření se NESMÍ omezit na
  auditovaný modul — musí pokrýt CELÝ přístroj, a jako první ten kanál,
  kterým diagnostikuješ** (tady USB CDC konzole). Kontrolní seznam
  „SDRAM + displej + CM4" je pro změnu boot časování **nedostatečný**;
  patří do něj `ping` po studeném startu.
  ⚠️ Platí i obráceně: dokud konzole odpovídá, „deska nenaběhla" je tvrzení,
  které si musíš ověřit sondou (`g_uptime_s`), ne odvodit z ticha na portu.
- **Detekce:** po KAŽDÉ změně, která sahá na boot časování (`HAL_Delay`,
  `printf`, blokující volání v `main()` nebo v `MX_*_Init`), povinně:
  power-cyklus → `ping` → `status` s `Reset: power-on` → `selftest` →
  `membench`/`bgcheck`. Samotný SW reset **tuhle třídu vady neodhalí vůbec**.
- **Commit:** `fix(F-0152)` (odstranění), `fix(F-0152)` (vrácení, `830ea2c`)
- **Stav:** aktivní

---

### L-0085 — Nová varianta pod existujícím druhem zdědí CIZÍ prodlevu a CIZÍ dekodér; a událost se dala potlačit sama sebou v jednom průchodu ISR

- **Datum:** 2026-09-25 (nalezeno i opraveno, přezkum vlastních oprav, F-0155 + F-0156)
- **Oblast:** `usart.c` (F-0153), záznamník chyb `errlog`
- **Symptom:** Oprava F-0153 měla udělat tichou poruchu (selhání re-armu GPS
  příjmu = trvalá smrt RX) **viditelnou**. Zalogovala ji pod existující druh
  `ERRLOG_K_UART` s novým `sub = 0xFE`. Výsledek byl horší než nic:
  1. **Dekodér ten `sub` neznal.** `errlog_fmt_detail` rozlišoval jen
     `sub == 0xFF`; všechno ostatní padalo do větve
     `"ORE=%lu FE=%lu NE=%lu PE=%lu"`. Trvalá smrt příjmu se tedy vypsala jako
     **falešné `ORE=<počet>`** — záznamník tvrdil chybu, která se nestala.
  2. **Prodleva `errlog_put` je per DRUH, ne per `sub`** (`ERRLOG_COOLDOWN_MS`
     = 60 s). `HAL_UART_ErrorCallback` loguje **dvakrát v jednom průchodu**:
     nejdřív chybu linky, pak — o pár instrukcí dál — selhání re-armu. Druhé
     volání narazilo na prodlevu, **kterou si první právě nastavilo**, takže
     se v hlavní cestě záznam neemitoval **nikdy**. A protože je porucha
     terminální (další callback nepřijde), nevyvezl ho ani žádný příští záznam.
- **Příčina:** „Přidám to pod existující druh, je to přece taky UART" vypadá
  jako úspora. Jenže druh v tomhle záznamníku nese **dvě implicitní politiky** —
  jak se událost **rozluští** a jak často se **smí emitovat** — a nový `sub`
  zdědí obě, aniž by o něm kterákoli z nich věděla.
- **Oprava:** vlastní druh `ERRLOG_K_UARTFATAL` (na KONEC výčtu — čísla jsou
  v zapsaných datech), vlastní větev ve `errlog_fmt_detail`, vlastní jméno
  v `errlog_kind_name`, a posunutý `ERRLOG_KIND_MAX`.
  ✅ **Ověřeno ve slinkovaném obrazu, ne předpokladem:** `s_el_cool_next` má
  nově **52 B = 13 × uint32** a `s_el_pending` **26 B = 13 × uint16**, tedy
  nový druh má prokazatelně **vlastní kbelík prodlevy**.
- **Pravidlo:** **Než přidáš novou variantu pod existující druh/kategorii,
  vyjmenuj, co ta kategorie implicitně určuje** — typicky rate-limit, dekodér,
  jméno, filtr v UI — **a ověř, že to nová varianta smí zdědit.** Když se liší
  v ŽIVOTNOSTI (přechodná × trvalá) nebo ve VÝZNAMU, patří jí vlastní druh.
  🔴 A zvlášť: **dvě diagnostická hlášení v jednom průchodu ISR si můžou
  navzájem vyčerpat sdílený rate-limit.** To se nepozná čtením ani jednoho
  z nich zvlášť — jen přečtením obou v pořadí, v jakém běží.
- **🔑 Meta-poučení (tohle je na té lekci nejcennější):** F-0153 prošlo
  **buildem (0 varování), `audit.py` v baseline, kontrolou symbolu v `.elf`
  i HW testem na desce** — a přesto nedělalo, co mělo. Žádná z těch kontrol na
  tuhle třídu nedosáhne, protože **všechny ověřují, že se kód přeložil
  a vykonal, ne že jeho VÝSTUP dává smysl.** Odhalilo to až přečtení
  **konzumenta** (`errlog_fmt_detail`) a **mechanismu** (`errlog_put`) — tedy
  kódu, který se vůbec neměnil. Ke každé opravě, která něco *hlásí*, patří
  otázka: **kdo to čte a co z toho udělá?**
- **Detekce:** u každého `errlog_put`/loggeru ověř, že (a) dekodér zná použitý
  `sub`/`kind` — jinak vypíše cizí větev, (b) v témže průchodu se neloguje
  víckrát pod stejný druh. Mechanicky: vypsat všechna volání jako
  `kind + sub` a porovnat s větvemi ve formátovači.
- **Commit:** `fix(F-0155/F-0156)`
- **Stav:** aktivní

---

### L-0086 — Běžící průměr regulační smyčky přežil vlastní akční zásah; a stav z backup domény se po resetu předpokládal nulový

- **Datum:** 2026-09-26 (audit modulu 24, F-0158)
- **Oblast:** `rtc.c` — disciplinace LSE podle GPS (`rtc_lse_*`, `RTC_CALR`)
- **Symptom:** žádný viditelný — smyčka „běžela“, korekce se zapisovaly. Teprve
  simulace přesně podle aritmetiky kódu ukázala, že při driftu +10 ppm zbytek
  osciluje −10..+6 ppm a **neustálí se ani po 4 dnech**.
- **Příčina:** (1) běžící průměr driftu se po zápisu korekce **nenuloval**, takže
  míchal okna naměřená pod starou kalibrací s okny pod novou, a vzorec
  `avg − cal` odečetl jen tu novou. (2) Sourozenec: `s_lse_cal_ppm` startoval po
  resetu na 0 s komentářem „co je zapsané v `RTC_CALR`“ — jenže `RTC_CALR` žije
  v backup doméně a reset přežije. První korekce po každém resetu tak u správně
  zkalibrovaného krystalu **správnou kalibraci odstranila**.
- **Oprava:** po úspěšném `HAL_RTCEx_SetSmoothCalib` nulovat průměr, počet
  i fázovou referenci; `rtc_lse_cal_from_reg()` načte `RTC_CALR` při prvním
  vzorku (`rtc.c`, `USER CODE BEGIN 1`).
- **Pravidlo:** **Estimátor, jehož vstup mění vlastní korekce, začíná po každé
  korekci novou epochu.** A **stav, který v HW přežije reset (backup doména,
  option bytes, externí čip), se po resetu čte zpět z HW** — nikdy se
  nepředpokládá výchozí hodnota proměnné v RAM.
- **Detekce:** u každé smyčky s akumulátorem se ptej: *nuluje ho akční zásah?*
  U každé proměnné, jejíž komentář odkazuje na registr, najdi řádek, kde se ten
  registr **čte** (L-0028). Chování smyčky ověř simulací její aritmetiky, ne
  čtením — zde by čtení vadu nenašlo.
- **Commit:** `c387409`
- **Stav:** aktivní

---

### L-0087 — Porovnání s mezí je pro NaN vždy nepravdivé, takže mez NaN propustí; ošetření existovalo, jen u dvojčete

- **Datum:** 2026-09-26 (audit modulu 24, F-0160 / F-0163 / F-0164)
- **Oblast:** `meas_math.c` (limitní tester), formátovače `fmt_hz`/`fmt_sdec`/
  `fmt_fixed` (`app_gpsdo.c`), `fmt_frac` (`screen_main.c`)
- **Symptom:** limitní tester vyhodnotil NaN jako **PASS** — a NaN je dosažitelné
  třemi SCPI příkazy (`CALC:MATH:M 1E308` → `+Inf`, `CALC:NULL:ACQ` → `Inf − Inf`).
  Formátovače tiskly pro NaN věrohodnou nulu (`(uint32_t)NaN` je UB, M7 `VCVT`
  dá 0) a `fmt_frac` se pro `+Inf` zasekl v normalizační smyčce → IWDG.
- **Příčina:** `if (y < lo) FAIL; if (y > hi) FAIL; PASS` — pro NaN jsou obě
  porovnání nepravdivá, takže tok spadne do „dobré“ větve. Tatáž mez u formátovače
  (`if (a >= 4.2e9) …`) NaN nechytí a přetypování za ní je UB. SCPI dvojče
  `fmt_scpi_hz_d` to přitom mělo vzorově ošetřené (L-0012).
- **Oprava:** negovaná porovnání `if (!(y >= lo)) …` (pro NaN pravdivá) a meze
  formátovačů ve tvaru `if (!(a < MEZ)) → "--"`; horní normalizační smyčka dostala
  mez iterací (L-0030).
- **Pravidlo:** **Každá mez, za kterou následuje verdikt nebo přetypování, se píše
  tak, aby NaN padlo do BEZPEČNÉ větve** — tedy `!(x >= lo)` / `!(x < MEZ)`, ne
  `x < lo` / `x >= MEZ`. „Chybí-li hodnota, projde“ je u testeru i formátovače
  nejhorší možný výsledek. ⚠️ Platí jen bez `-ffast-math` (ověřeno: projekt ho
  nemá); s ním smí překladač NaN porovnání vyoptimalizovat.
- **Detekce:** selftest, který NaN vyrobí **cestou zvenku** (ne dosazením), a pro
  každou mez před `(int)`/`(uint32_t)` nad `float`/`double` otázka „co udělá NaN?“.
- **Commit:** `479d7d1`
- **Stav:** aktivní

---

### L-0088 — Estimátor se ověřuje nezávislou referencí, ne komentářem u téže smyčky; a „vylepšení“ se přijímá až po pozitivní kontrole

- **Datum:** 2026-09-26 (audit modulu 24, F-0161 / F-0166 / F-0167)
- **Oblast:** `screen_main.c` (overlapping ADEV/HDEV/MDEV, konfidenční pás),
  `phase_noise.c` (ℒ(f))
- **Symptom:** všechny tři overlapping estimátory končily smyčku o člen dřív, než
  povoluje vzorec NIST SP1065 **uvedený v komentáři přímo nad smyčkou**; odhady
  byly nezkreslené, jen na dlouhých τ bez 7–10 % členů. Konfidenční pás počítal
  počet členů **druhým vzorcem** vedle estimátoru (u HDEV už nesouhlasil o +1)
  a overlapping členy bral jako nezávislé → pás ~1,5× užší. ℒ(f) měřil kmitočtový
  offset místo šumu (symetrický Hann bez odečtu střední hodnoty).
- **Příčina:** vzorec v komentáři a smyčka pod ním vypadají jako shoda, dokud se
  nedosadí konkrétní indexy — a nikdo je nedosadil. A oprava ℒ(f) podle návrhu
  (odečíst i lineární trend) by **vyrobila vlastní artefakt**: tón s celým počtem
  period má nenulovou projekci na rampu, takže proklad z něj ukousne (bin 1 jen
  −14,9 dB pod špičkou). Chytila to až pozitivní kontrola na hostu.
- **Oprava:** meze smyček podle vzorců; pás z EDF (bílý FM, SP1065) místo počtu
  členů; ℒ(f) jen odečet střední hodnoty + periodický Hann; detrend zamítnut.
- **Pravidlo:** **Statistický estimátor ověř proti NEZÁVISLÉ implementaci** (jiný
  algoritmus — tady SP1065 z fázových dat — na hostu, shoda na počet členů i na
  číslice), ne proti komentáři u téže smyčky. **Každé „vylepšení“ numerické metody
  prožeň pozitivní kontrolou dřív, než ho zapíšeš** — i když ho navrhl nález:
  návrh je hypotéza, ne specifikace.
- **Detekce:** u každého estimátoru dosaď do meze smyčky konkrétní `M`, `m`
  a spočítej členy ručně; nový selftest musí na STARÉM kódu selhat
  (přepis 1:1 na hostu včetně zaokrouhlení na `float`).
- **Další výskyt (2026-09-26, bod 4 — EDF podle typu šumu):** vzorec EDF pro
  blikavý FM při m = 1 jsem zapsal z paměti jako `2(N−2)/(2,3N−4,9)`; Monte Carlo
  (`docs/audit/sim/2026-09-26_edf_typ_sumu.js`) dalo empirickou EDF ~19 proti 0,9
  ze vzorce — správně je `2(N−2)²/…`. **Vzorec z literatury/paměti se do měřidla
  nepouští bez simulace, která ho změří** (ostatní čtyři typy seděly do ~12 %).
- **Commit:** `35d3453`, `32efe36` (+ `06cbfd8` pro další výskyt)
- **Stav:** aktivní

---

### L-0089 — Popisek a barva tvrdily vlastnost, kterou veličina nemá

- **Datum:** 2026-09-26 (audit modulu 24, F-0159 / F-0162)
- **Oblast:** `app_gpsdo.c` — okno ANALÝZA (rozpočet nejistoty), okno SELF-SURVEY
- **Symptom:** okno ANALÝZA ukazovalo „Nejistotu U“ a „Platných cifer“ spočítané
  z hradla, které si `mp_budget` při neznámém hradle **dosadil (1 s)** — živě, na
  dnešní desce bez FPGA; řádek rozlišení vedle přitom poctivě ukazoval „--“.
  SELF-SURVEY barvil „Rozptyl H“ zeleně pod 2 m s popisem „klesá s N“ — jenže
  šlo o směrodatnou odchylku jednotlivých fixů, která s N **neklesá**. Barva
  závisela na místě, ne na délce průzkumu.
- **Příčina:** výpočet byl správný, **nesprávné bylo to, co o něm říkalo UI**.
  Dosazená hodnota nenesla žádný příznak, takže volající nemohl poznat, že čte
  vymyšlený vstup; a popisek „konvergence“ převzal význam, který by měla chyba
  průměru `σ/√N`, ne rozptyl fixů.
- **Oprava:** příznak `mp_budget_t.valid` cestuje **s výsledkem** a řídí všechny
  tři řádky; survey ukazuje „Rozptyl fixu“ neutrální barvou s vysvětlivkou.
- **Pravidlo:** **Než hodnotu obarvíš jako OK nebo ji nazveš přesností
  či konvergencí, ověř, že má tu vlastnost, kterou popisek tvrdí** (klesá s N?
  platí pro skutečné hradlo?). **Náhradní hodnotu dosazenou za neznámý vstup
  nesmí výsledek nést bez příznaku** — příznak patří do struktury výsledku, ne do
  podmínky u každého volajícího (tu jeden zapomene, L-0012).
- **Detekce:** u každé barvy/verdiktu v UI najdi, z čeho se počítá, a zeptej se
  „co by ta hodnota udělala, kdybych měřil 10× déle?“. U funkcí, které dosazují
  výchozí vstup, grep na volající, kteří nečtou příznak platnosti.
- **Commit:** `8c79ced`, `038288c`
- **Stav:** aktivní

---

### L-0090 — Nezměněná velikost `.text` není důkaz, že změna chybí; a ověřovací řetězec patří za KAŽDOU opravu, ne za dávku

- **Datum:** 2026-09-26 (audit modulu 24, F-0160 na CM4, F-0169)
- **Oblast:** ověřovací metoda (CLAUDE.md mechanická pravidla 3 a 4)
- **Symptom:** (1) Po opravě `meas_limit_eval` zůstala `.text` CM4 **bajt za bajt
  stejná** (242 692 B), ačkoli pravidlo 4 říká „když obraz neroste, změna vypadla“.
  (2) Oprava F-0169 prošla buildem s 0 varováními, ale zavedla `-Wshadow`
  v selftestu; `tools/audit.py` ho chytil až **o tři commity později**, protože
  běžel jen na konci dávky.
- **Příčina:** (1) CM4 má FPU jen single precision, `double` porovnání dělá
  `__aeabi_dcmp*`, a záměna `dcmplt`/`dcmpgt` za `dcmpge`/`dcmple` má stejný
  počet instrukcí — změna v obrazu **je**, jen je stejně velká. (2) Build
  (`-Wall`) a audit (`-Wshadow` a spol.) mají **různé sady varování**; build
  sám baseline auditu nehlídá.
- **Oprava:** (1) změna doložena disassembly (`meas_limit_eval` volá
  `__aeabi_dcmpge`/`__aeabi_dcmple`); (2) přejmenování v `7f29799`.
- **Pravidlo:** **Pravidlo 4 je jednosměrné: RŮST `.text` dokazuje, že změna je
  v obrazu; NERŮST nedokazuje opak** — u záměny operátoru, konstanty nebo
  podmínky ověř změnu v disassembly dotčené funkce. A **`tools/audit.py` pouštěj
  po každé opravě, ne po dávce** — build ho nenahrazuje.
- **Detekce:** když `.text` po opravě nezměnila velikost, `objdump -d` nad
  funkcí a hledat nový opkód/konstantu; `audit.py` v každém `fix:` commitu
  (baseline 92 / 0 / 2).
- **Commit:** `479d7d1`, `7f29799`
- **Stav:** aktivní

---

### L-0091 — „NaN-safe“ v komentáři znamenalo „bez UB“, ne „správný verdikt“; vadu v originálu odhalilo až psaní jeho dvojčete

- **Datum:** 2026-09-26 (oprava F-0170)
- **Oblast:** `meas_present.c` (`mp_fit_significant`, `mp_fit_solve`) ↔ SPA `fitSig`
- **Symptom:** žádný viditelný. Při přenosu t-testu z firmwaru do webu (F-0170)
  jsem pro JS nejdřív sepsal chování **na okrajích** jako testovací tabulku
  (n < 3, r = ±1, NaN). Firmwarová funkce, kterou jsem týž den sám napsal
  (F-0169) a opatřil poznámkou *„dokonalá přímka (i NaN-safe)“*, vracela pro
  `r = NaN` **1 = průkazné**. Tentýž den jsem přitom zapsal L-0087 („NaN do
  bezpečné větve“) — a vlastní nový kód ji porušoval.
- **Příčina:** `if (!(r2 < 1.0)) return 1;` je negovaná forma, takže **nezpůsobí
  UB** a to jsem nazval „NaN-safe“. Jenže negace posílá NaN do větve, kterou
  zrovna ta podmínka označuje — a tady to byla větev **„dokonalá přímka“**.
  Negovaná forma není bezpečná sama o sobě; bezpečná je jen tehdy, když
  větev, do které NaN padne, je ta opatrná. `mp_fit_solve` měla totéž z druhé
  strany: `dx <= 0 || dx < 1e-30` NaN v X propustila.
- **Oprava:** explicitní `if (f->r != f->r) return 0;`, v `mp_fit_solve`
  `!(dx >= 1e-30)` a kontrola NaN směrnice; tři nové případy v `mp_selftest`
  a tentýž případ v `tools/spa/stat_test.js` pro web (`a8089d2`, `e99ccbb`).
- **Pravidlo:** **U každé meze rozhodni, KAM má padnout NaN, a napiš to do
  komentáře slovy („NaN → neprůkazné“), ne nálepkou „NaN-safe“.** Negovaná
  forma `!(x < mez)` pošle NaN do větve *za* podmínkou — ověř, že je to ta
  opatrná. A **když přenášíš funkci do dvojčete, sepiš nejdřív tabulku jejího
  chování na okrajích a pusť ji proti ORIGINÁLU** — dvojče napsané podle
  úmyslu odhalí, kde se od úmyslu odchýlil originál.
- **Detekce:** grep na komentáře „NaN-safe“/„NaN safe“ a u každého ověřit,
  který verdikt NaN dostane; u dvojčat (C ↔ JS, CM7 ↔ CM4) sdílená tabulka
  okrajových případů v obou testech (`mp_selftest` + `stat_test.js`).
- **Commit:** `a8089d2`, `e99ccbb`
- **Stav:** aktivní

---

### L-0092 — Rozestup vzorků sedí, okno průměrování ne: okamžitý vzorek není průměr za τ0

- **Datum:** 2026-09-26 (druhý průchod modulu 24, F-0171 / F-0172)
- **Oblast:** statistika stability (`app_gpsdo_tick_stats_sample`, `stats_sample`),
  datalog (`datalog.c sample`), rekonstrukce Allanovy pyramidy
- **Symptom:** žádný viditelný — Allanova křivka vypadala věrohodně. Simulace
  ukázala, že je 2× (bílý FM) až 23× (bílý PM) vysoko a že šum přístroje dostal
  sklon bílého FM. První průchod auditu stejný kód prohlásil za „τ0 = 1 s
  správně".
- **Příčina:** FPGA dává ~4 měření/s po 0,25 s. Statistika brala 1×/s **poslední**
  měření, datalog 1× za 10 s taky. Rozestup vzorků (1 s, 10 s) byl správně, ale
  vzorek pokrýval jen 0,25 s — mrtvá doba 75 % a 97 %. ADEV/MDEV i rekonstrukce
  předpokládají, že vzorek je **průměr za celé τ0**.
- **Oprava:** FpgaTask sčítá cykly a hradla všech měření do akumulátoru každého
  konzumenta; ten si vezme reciproký průměr `Σcykly/Σhradla` (`ace2939`).
- **Pravidlo:** **U každého estimátoru s „τ0" ověř DVĚ věci: rozestup vzorků
  a okno, přes které vzorek průměruje.** Vzorkování „vezmi poslední hodnotu"
  z rychlejšího zdroje není průměr; mezi dvěma konzumenty téhož proudu dat
  (1 s a 10 s) má každý dostat vlastní průměr, ne sdílený snímek.
- **Detekce:** u každé statistiky se ptej: *kolik měření za τ0 dává zdroj a kolik
  jich konzument použije?* Když méně než všechna, je tam mrtvá doba. Simulace
  `docs/audit/sim/2026-09-26_mrtva_doba.js` jako vzor.
- **Další výskyt (týž den, v MÉ opravě):** i po F-0171 se vzorek skládal podle
  1s tiku `HAL_GetTick() - last >= 1000; last = now` — ten se opožďuje o latenci
  smyčky (~1,01 s), takže občas pobral měření navíc, a při nízkém kmitočtu se délky
  vzorků střídaly (±20 % při hradle 0,4 s). **Vzorek z proudu událostí skládej podle
  POČTU událostí, ne podle vlastních hodin konzumenta** (`d0a02e5`).
- **Commit:** `ace2939`, `d0a02e5`
- **Stav:** aktivní

---

### L-0093 — Statistický test ověřený na nezávislých datech selže na datech, která doopravdy dostane

- **Datum:** 2026-09-26 (druhý průchod modulu 24, F-0173)
- **Oblast:** `mp_fit_significant` (t-test průkaznosti prokladu) + web `fitSig`
- **Symptom:** oprava F-0169 (t-test místo pevného |r| ≥ 0,5) prošla pozitivní
  kontrolou — na **bílém šumu** dávala správných 5 % falešných poplachů. Na
  datech, která okno ANALÝZA skutečně prokládá (Vc a teplota z datalogu, pomalu
  putující), ale hlásila falešný „průkazný drift" v 63–91 % případů, tedy hůř
  než pravidlo, které nahradila.
- **Příčina:** t-test předpokládá **nezávislá rezidua**. Autokorelovaná data nesou
  méně nezávislé informace, než kolik mají bodů, takže df = n − 2 je přemrštěné.
  Pozitivní kontrola byla postavená na datech, pro která test platí, ne na
  datech, která dostane.
- **Oprava:** df z efektivního počtu bodů n(1−ρ)/(1+ρ) podle lag-1 autokorelace
  reziduí (`472ece6`); simulace: 7 % (AR) a 28 % (náhodná procházka) falešných,
  skutečný drift dál detekovaný.
- **Pravidlo:** **Pozitivní kontrolu statistické metody postav i na DATECH, KTERÁ
  METODA V PŘÍSTROJI SKUTEČNĚ DOSTANE** (autokorelace, drift, mrtvá doba,
  kvantizace), ne jen na ideálním bílém šumu. Předpoklady testu (nezávislost,
  normalita, stacionarita) vypiš a u každého ověř, jestli ho vstup splňuje.
- **Detekce:** simulace falešných poplachů pro bílý šum, AR(1) a náhodnou
  procházku (`docs/audit/sim/2026-09-26_ttest_autokorelace.js`) — metoda smí
  u šumu bez driftu hlásit „průkazné" jen blízko své hladiny.
- **Commit:** `472ece6`
- **Stav:** aktivní

---

### L-0094 — Přesnost typu se posuzuje vůči VARIACI, ne vůči hodnotě: „Float OK" platilo pro 10 MHz a nikde jinde

- **Datum:** 2026-09-26 (modul 24, F-0179)
- **Oblast:** `screen_main.c` — ring a pyramidy statistiky stability
- **Symptom:** žádný viditelný; při 10 MHz výsledky sedí. Simulace pro nízké
  kmitočty: ADEV 3–4× vysoko (1 MHz, 100 kHz) a při 10 kHz / 1 kHz **nula** —
  body grafu tiše zmizely.
- **Příčina:** `y` nese offset až 1/f (nominál je celé Hz) a `float` má relativní
  krok 6·10⁻⁸ vůči HODNOTĚ, ne vůči variaci. Pro statistiku stability rozhoduje
  poměr variace/hodnota — a ten je u stabilního zdroje s offsetem 10⁻⁴ jen 10⁻⁸.
  Komentář „Float OK (cold path)" posoudil rychlost, ne přesnost. Je to tatáž
  třída jako **L-0031** (typ jako nepoznaná mez), jen na jiném místě.
- **Oprava:** úložiště a mezisoučty v `double`; `float` jen při kreslení, vždy
  relativně k referenci (`5c019b1`).
- **Pravidlo:** **Když ukládáš veličinu, ze které se počítají ROZDÍLY nebo
  rozptyl, porovnej krok typu s nejmenší VARIACÍ, kterou chceš rozlišit — při
  NEJVĚTŠÍM možném offsetu.** Nevyhovuje-li, ukládej v širším typu nebo
  odchylky od reference. Komentář „typ X stačí" musí říct, pro jaký rozsah.
- **Detekce:** u každého `float` akumulátoru statistiky spočítej `ulp(max|hodnota|) /
  min(variace)`; simulace `docs/audit/sim/2026-09-26_float_podlaha.js` jako vzor.
- **Commit:** `5c019b1`
- **Stav:** aktivní

---

### L-0095 — Jedna podmínka pro dvě různé věci: „formát už nesedí" není „měří se jiný signál"

- **Datum:** 2026-09-27 (modul 24, F-0183 + F-0184)
- **Oblast:** `screen_main.c` — `freq_advance()`, přestavba formátu vs. nulování statistiky
- **Symptom:** žádný viditelný dnes (SIM ani `fpgasim` s malým šumem hranici
  nepřekračují). Simulace: stabilní 10 MHz → **1931 nulování statistiky na
  4000 měření**; naopak přechod 10 → 12 MHz statistiku **nevynuloval** vůbec.
- **Příčina:** `need = (počet celých číslic se změnil)` řídilo dvě věci naráz:
  přestavbu formátu (správně) a „jiný signál → vynulovat statistiku, nový
  nominál" (špatně). Počet číslic je vlastnost ZOBRAZENÍ, ne signálu — signál
  na hranici dekády ho mění při každém šumovém překmitu, jiný signál v téže
  dekádě ho nemění vůbec.
- **Oprava:** dvě proměnné, `fmt_need` (jen formát) a `sig_change` (relativní
  změna > 10⁻⁴ proti přesnému kmitočtu při posledním nulování) (`6047fec`).
  F-0184 se našel až **pozitivní kontrolou** opravy F-0183: simulace měla scénář
  „stabilní signál se nesmí nulovat" a ten selhal už na STARÉ logice.
- **Pravidlo:** **Když jedna podmínka spouští víc důsledků, u každého se zeptej,
  jestli je podmínka jeho SKUTEČNÁ příčina, nebo jen korelát.** Vlastnost
  zobrazení (počet číslic, jednotka, rozsah osy) nesmí rozhodovat o datech.
  A pozitivní kontrola opravy má mít i scénář „tohle se NESMÍ stát" — ten najde
  vadu, kterou oprava nezavedla, ale zdědila.
- **Detekce:** přepis rozhodovací logiky do simulace se šumem **přesně na
  hranici** (10 MHz, 1 MHz, …), počítat nulování i skoky reference.
- **Commit:** `6047fec`
- **Stav:** aktivní

---

### L-0096 — Zrychlení přes prefixové součty KUMULATIVNÍ veličiny ruší číslice: nejdřív diferencovat, pak sčítat

- **Datum:** 2026-09-27 (modul 24, F-0182)
- **Oblast:** webová `mdev()` (SPA v `httpd_min.c`)
- **Symptom:** návrh opravy z vlastního auditu (prefixové součty fáze) dával na
  bílém šumu shodné výsledky a byl 18× rychlejší. Se signálem, který má offset
  10⁻⁶ (necentrovaná data), ale relativní chybu **3,6·10⁻⁵**.
- **Příčina:** fáze je součet kmitočtů — při offsetu/driftu roste lineárně až
  kvadraticky, její prefixové součty kvadraticky až kubicky. MDEV potřebuje
  rozdíl dvou takových obřích součtů, tedy malé číslo z velkých → zbytek double
  se vyruší. Test „shoda s referencí" na centrovaném bílém šumu to neviděl.
- **Oprava:** klouzavé okno nad druhými diferencemi d[k] = x[k+2m]−2x[k+m]+x[k]
  — lineární fáze (offset) v nich už zmizela, sčítají se malá čísla (`724e3f7`).
  Chyba < 3·10⁻¹⁵, rychlost stejná (~20×).
- **Pravidlo:** **Zrychlení, které nahrazuje sčítání rozdílem kumulativních
  součtů, ověř na datech s OFFSETEM a DRIFTEM, ne jen na centrovaném šumu.**
  Kde to jde, nejdřív odstraň to, co roste (diferencuj), a teprve pak akumuluj.
  Tatáž třída jako L-0094 (přesnost vůči variaci, ne vůči hodnotě).
- **Detekce:** `docs/audit/sim/2026-09-27_mdev_presnost.js` (varianty proti
  naivní referenci, scénáře drift/offset); `stat_test.js` hlídá drift i offset.
- **Commit:** `724e3f7`
- **Stav:** aktivní

---

### L-0097 — Pevný počet desetin je absolutní krok; přesnost výstupu musí být RELATIVNÍ jako přesnost měření

- **Datum:** 2026-09-27 (modul 24, F-0180)
- **Oblast:** SCPI/JSON formát kmitočtu, datalog, IPC snapshot
- **Symptom:** web, SCPI i datalog dostávaly kmitočet po 10 µHz, displej v plné
  přesnosti → pro tatáž data různá σy (při 1 kHz 6×, při nižších nula).
  Vlastní návrh opravy (9 desetin, datalog v nHz) by vadu jen posunul níž:
  při 10 Hz je 1 nHz relativně 10⁻¹⁰, což podlaha nové desky přesáhne.
- **Příčina:** podlaha čítače je RELATIVNÍ (tdc/τ, na kmitočtu nezávislá),
  kdežto pevný počet desetin je ABSOLUTNÍ krok — relativně roste s klesajícím
  kmitočtem. Každý pevný počet desetin tedy někde pod nějakým kmitočtem selže.
- **Oprava:** výstup s **15 platnými číslicemi** (DBL_DIG, `fmt_scpi_hz_sig`),
  datalog jako IEEE double — relativně ~10⁻¹⁵ na libovolném kmitočtu
  (`e5d7521`). Snapshot dostal double do dvou nikdy neplněných floatů, takže
  layout zůstal (`_Static_assert` na offset i velikost).
- **Pravidlo:** **U veličiny s relativní přesností (kmitočet, poměr) formátuj
  a ukládej v PLATNÝCH číslicích (nebo plovoucí čárce), ne v pevných
  desetinách.** Než zvolíš krok, spočítej ho relativně u NEJNIŽŠÍ hodnoty
  rozsahu. A když výstup nemá přesná data, nepředstírej je — tiskni jen tolik
  číslic, kolik zdroj nese.
- **Detekce:** pro každý formátovač/úložiště spočítej `krok / hodnota` na dolním
  konci rozsahu a porovnej s podlahou měření (`sim/2026-09-26_kvantizace_10uHz.js`,
  `sim/2026-09-27_fmt_hz_sig.js`).
- **Commit:** `e5d7521`
- **Stav:** aktivní

---

### L-0098 — Nulování struktury PO jejím naplnění: překladač zápisy tiše zahodil, dvojče měřítko mělo

- **Datum:** 2026-09-27 (F-0185, nalezeno při úpravě téže funkce pro F-0180)
- **Oblast:** `scpi.c` — `scpi_src_load_cm7_ex` (USB SCPI)
- **Symptom:** přes USB vracely `SENS:FREQ:GATE?`, `CHAN?` a `INIT:CONT?` vždy
  výchozí hodnotu; přes TCP/HTTP správnou. Od `6e43eb2` (přidání readbacku), tedy
  týdny, a CLAUDE.md přitom tvrdil, že „USB cesta byla správně".
- **Příčina:** blok plnící `set_*` byl přidán NAD existující `memset(src, 0, …)`.
  Překladač zápisy před memsetem odstranil jako mrtvé (`.text` po opravě +32 B)
  — bez varování, GCC na mrtvé zápisy neupozorňuje. Selftest SCPI jde přes vlastní
  `scpi_src_t`, ne přes loader, takže to neviděl.
- **Oprava:** blok za `memset` (`f81e25a`).
- **Pravidlo:** **Ve funkci, která strukturu nuluje, patří nulování na ÚPLNÝ
  začátek a každé plnění až za něj — a při přidávání pole do takové funkce se
  podívej, kde memset leží.** Když existuje dvojče (tady USB loader a IPC loader
  téže `scpi_src_t`), rozdíl mezi nimi je nejlevnější test: `scpi X` proti
  `scpi ipc X` hlásí SHODA/ROZDIL.
- **Detekce:** grep `memset(` ve funkcích typu `*_load*`/`*_fill*` a kontrola, že
  nad ním nejsou přiřazení do téže struktury; na HW porovnání `scpi` vs `scpi ipc`
  pro každý readback.
- **Commit:** `f81e25a`
- **Stav:** aktivní

---

### L-0099 — Emulátor, který nevyrábí data jako skutečný zdroj, vadu nemůže ukázat: kvantizaci zdroje modeluj bit za bit

- **Datum:** 2026-09-27 (modul 24, F-0186)
- **Oblast:** `fpga_freq.c` — hi-res kmitočet, `fpgasim`
- **Symptom:** hi-res kmitočet (7 desetin na displeji, 15 platných číslic ven) nesl
  u asynchronního signálu systematickou chybu 0 až +2·10⁻⁹ (10 MHz: až 20 mHz) —
  horší než obyčejné `x100000`. Žádný test ani ověřovací běh ji neviděl.
- **Příčina:** FPGA posílá `gate_time_ns` jako **floor** z Δt v ticích 2,5 ns
  (`spi_app.v:507`), STM ho bralo jako přesné Δt. Pravidlo rekonstrukce ticků bylo
  sepsané v návrhu protokolu v2, jen neimplementované. Emulátor `fpgasim` vyráběl
  hradlo jako libovolné celé ns a hrany `floor(hz·gate)` — tedy **jinak než FPGA**,
  takže vadu nemohl ukázat a navíc přidal vlastní šum až 4·10⁻⁷, který by ji zakryl.
- **Oprava:** `fpga_freq_dt_ticks()` = round(gate_ns/2,5 ns), dělí se přesnými ticky
  ve všech pěti místech; emulátor skládá rámec jako FPGA (celé periody, Δt v ticích,
  `gate_time_ns` floor) (`98c4394`).
- **Pravidlo:** **Emulátor nebo testovací zdroj musí vyrábět data TÍMŽ algoritmem
  a TOUŽ kvantizací jako skutečný zdroj — čti jeho zdroj (RTL), ne dokumentaci.**
  Pole, které zdroj zaokrouhluje, se bere jako zaokrouhlené; když vedle něj
  existuje přesnější veličina (tady ticky), rekonstruuj ji.
- **Detekce:** u každého pole z FPGA dohledat v `spi_app.v`, jak vzniká (floor /
  round / přesné); selftest s vektorem, kde se floor a přesná hodnota liší
  (`fpga_freq_select_selftest`, 9 999 999,900000001 Hz);
  `sim/2026-09-27_gate_floor.js`, `sim/2026-09-27_f0186_emulator.js`.
- **Commit:** `98c4394`
- **Stav:** aktivní

---

### L-0100 — Estimátor nad podvzorkovanými daty není týž estimátor: MDEV z pyramidy průměrů kmitočtu

- **Datum:** 2026-09-27 (modul 24, F-0187)
- **Oblast:** `screen_main.c` — decimační pyramida ADEV/MDEV/HDEV
- **Symptom:** MDEV/TDEV nad 10 s u bílého PM 3,2× (stage 1) až 10× (stage 2)
  vysoko, bílý PM se klasifikoval jako blikavý; web (plná data) ukazoval jiné MDEV
  než displej.
- **Příčina:** stage s drží průměry kmitočtu po 10ˢ s = fázi podvzorkovanou po 10ˢ s.
  ADEV a HDEV potřebují jen průměry kmitočtu přes τ, takže jim to nevadí; MDEV
  průměruje FÁZI přes n = τ/τ0 bodů a z podvzorkované fáze průměroval jen m bodů.
  Komentář to věděl (podlaha „m, ne τ/1 s"), ale nikdo z toho neodvodil, že výsledek
  není MDEV.
- **Oprava:** fázová pyramida — každá položka nese průměr fáze bloku relativně
  k jeho začátku, decimace ho skládá přesně, MDEV nad stage ≥ 1 je pak standardní
  (`35ca1ff`).
- **Pravidlo:** **Než spočítáš estimátor nad komprimovanými daty (decimace,
  průměry, histogram), napiš, co přesně komprese zachovává, a ověř, že estimátor
  nepotřebuje nic dalšího.** ADEV z průměrů kmitočtu ano, MDEV/TDEV ne (potřebují
  průměr fáze), MTIE ne (potřebuje extrémy fáze).
- **Detekce:** porovnání pyramidy s přímým výpočtem z plných dat pro každý typ
  šumu a každou stage (`sim/2026-09-27_mdev_pyramida.js`); selftest proti přímé
  definici nad průměry fáze bloků (`screen_main_selftest`).
- **Commit:** `35ca1ff`
- **Stav:** aktivní

---

### L-0101 — Nulování stavu musí vyprázdnit i všechno, co je k němu „na cestě"

- **Datum:** 2026-09-27 (modul 24, F-0188)
- **Oblast:** `screen_main.c` / `fpga_freq.c` — statistika při změně signálu
- **Symptom:** po přepnutí měřeného zdroje prošly do vynulované pyramidy v 99,6 %
  případů 1–2 vzorky starého nebo smíšeného signálu (|y| až 0,17) a decimací
  dožívaly na dlouhých τ hodiny až dny.
- **Příčina:** `screen_main_stats_reset()` vynulovala pyramidy, ale ne frontu hotových
  vzorků (FpgaTask → UiTask) ani rozpracovaný akumulátor. Nulování a producent běží
  v různých taskách, takže v okamžiku nulování vždy něco „letí".
- **Oprava:** `fpga_stat_flush()` při nulování + filtr `screen_main_signal_match()`
  při odběru (`b619ea8`).
- **Pravidlo:** **Když nuluješ stav, který plní jiný task přes frontu nebo
  akumulátor, vyprázdni i je — a konzument ať navíc ověří, že vzorek k novému stavu
  patří (stejnou podmínkou, jakou se změna detekovala).** Samotné vyprázdnění
  nestačí, když může vzniknout vzorek složený přes hranici.
- **Detekce:** u každé funkce `*_reset`/`*_clear` vypsat, kdo daný stav plní
  a přes jaké mezičlánky; simulace přepnutí v náhodné fázi
  (`sim/2026-09-27_reset_fronta.js`).
- **Commit:** `b619ea8`
- **Stav:** aktivní

---

### L-0102 — Historie se smí navázat na živá data jen přes SOUVISLÝ úsek téhož zdroje

- **Datum:** 2026-09-27 (modul 24, F-0189)
- **Oblast:** `app_gpsdo.c` — rekonstrukce Allanovy pyramidy z datalogu
- **Symptom:** dlouhé τ (10³–10⁵ s) po restartu nesly náběhy OCXO a skoky předchozích
  sezení, mezery dnů i data jiného zdroje — věrohodně vypadající „drift".
- **Příčina:** rekonstrukce sypala záznamy za sebe a kontrolovala jen jejich vlastnosti
  (průměr, SIM), ne **vztah k sousedům** (čas, stejný signál). Živá cesta přitom při
  výpadku pyramidu nuluje — obnova z logu se chovala jinak než měření, které nahrazuje.
- **Oprava:** jen poslední souvislý úsek téhož signálu (řez při mezeře > perioda
  + 120 s nebo nepoužitelném záznamu), start až po prvním reálném měření, po zapnutí
  napájení vůbec (`4eee405`).
- **Pravidlo:** **Obnova stavu z uložené historie musí dodržet TÁŽ pravidla, podle
  kterých živý běh stav nuluje — co by živě vyvolalo reset, musí v historii vyvolat
  řez.** Kontroluj vztah záznamu k předchozímu (čas, zdroj), ne jen záznam sám.
- **Detekce:** syntetický log s mezerou, jiným signálem a výpadkem
  (`sim/2026-09-27_f0189_rekonstrukce.js`); na HW `status` po warm resetu → hláška
  „… vzorku posledniho souvisleho useku (… rezu)".
- **Commit:** `4eee405`
- **Stav:** aktivní

---

### L-0103 — „Nové" není „navazující": u počítadla kontroluj souvislost, ne jen změnu

- **Datum:** 2026-09-27 (modul 24, F-0190)
- **Oblast:** SPA (`httpd_min.c`) — buffer měření pro Allan/drift/ℒ(f) na webu
- **Symptom:** v záložním 1 Hz pollu (bez SSE) počítal web Allanovu odchylku z každého
  ~4. měření: mrtvá doba 75 %, ADEV 2× až 23× vysoko se sklonem bílého FM —
  tatáž vada, kterou displej měl do F-0171 a opravil jen u sebe.
- **Příčina:** vzorek se přidal při jakékoli ZMĚNĚ `seq_meas`; že mezi dvěma vzorky
  chybí měření, SPA nepoznala, přestože počítadlo tu informaci nese.
- **Oprava:** `ingestM` počítá chybějící měření (`seq − last − 1`, uint32), toleruje
  je do 1 % řady a τ0 je zahrnuje; víc = řada začne znovu a `aWarn` řekne proč
  (`d448b57`).
- **Pravidlo:** **Když zdroj čísluje data, kontroluj `q == last + 1`, ne `q != last` —
  a rozhodni, kolik děr je přípustné, podle toho, jak moc zkreslí výsledek, ne
  nulovou tolerancí, která zruší i legitimní případy (reload, ojedinělá ztráta).**
- **Detekce:** grep `!==lastSeq` / `!= s_last_seq` v kódu, který z dat počítá
  statistiku; `tools/spa/stat_test.js` oddíl F-0190 (poll = každé 4. měření).
- **Commit:** `d448b57`
- **Stav:** aktivní

---

### L-0104 — Pořadové číslo dokazuje souvislost jen toho, co zdroj čísluje: díru v čase, kdy zdroj mlčí, musí hlásit jiný mechanismus

- **Datum:** 2026-09-27 (modul 24, F-0193)
- **Oblast:** `fpga_freq.c` / `freertos_task_fpga.c` — vzorky statistiky z měření FPGA
- **Symptom:** vzorek statistiky mohl mít uvnitř mrtvou dobu (součet hradel 1 s, v čase
  víc), aniž by to cokoli ohlásilo — dvěma cestami: FpgaTask nestihl rámec (měření
  přepsané FPGA) a ztráta signálu uprostřed vzorku.
- **Příčina:** (1) nové měření se poznávalo jen podle ZMĚNY `SEQUENCE`, ne souvislosti
  (L-0103); (2) i s kontrolou souvislosti by zůstala druhá díra: při ztrátě signálu FPGA
  **neměří**, `SEQUENCE` stojí a po návratu **navazuje** — díra je v čase, ne v číslování.
  Emulátor přitom díru nevyrobil vůbec (plánoval měření od okamžiku pollu), takže žádný
  test nemohl ani jednu cestu ukázat (L-0099).
- **Oprava:** `fpga_seq_gap` + čítače v `status` (`SEQ FPGA`), `fpga_stat_break` při díře
  i při přechodu na ztrátu signálu/linku, emulátor na pevné mřížce + injektor
  `fpgasim fault gap` (`7f65f4a`).
- **Pravidlo:** **Kontrola pořadového čísla chytí jen ztrátu toho, co zdroj očísloval.
  Vyjmenuj stavy, kdy zdroj NEČÍSLUJE (ztráta signálu, pauza, restart), a pro každý
  zařiď přerušení řady zvlášť.**
- **Detekce:** u každé souvislé řady (akumulátor, pyramida, buffer) projít seznam
  stavů zdroje a ke každému najít místo, kde řadu přeruší; injektor díry
  (`fpgasim fault gap`) a simulace `sim/2026-09-27_f0193_diry_seq.js`.
- **Commit:** `7f65f4a`
- **Stav:** aktivní

---

### L-0105 — Když změníš VÝZNAM stavu, projdi všechny, kdo ho čtou

- **Datum:** 2026-09-27 (regrese vlastní opravy F-0189, nalezeno při HW testu)
- **Oblast:** `app_gpsdo.c` — rekonstrukce ADEV z datalogu, UART `status`
- **Symptom:** bez FPGA desky by `status` trvale hlásil `ADEV rekonstrukce: BEZI …
  <== zive vzorkovani zatim stoji`, přestože živé vzorkování běželo.
- **Příčina:** F-0189 změnil stav 3 z krátké sondy („vyplatí se rekonstrukce?") na
  **neomezené čekání** na první reálné měření. Automat jsem upravil a ověřil simulací,
  ale `app_gpsdo_stats_seed_progress` — jediný čtenář stavu mimo automat — zůstal u
  starého významu „3 = běží". Simulace testovala automat, ne jeho diagnostiku.
- **Oprava:** progress vrací `SEED_PROG_IDLE/RUN/WAIT/SKIP_POR`, `status` je rozliší
  (`a83bf1d`).
- **Pravidlo:** **Když stav dostane nový význam (trvání, co se během něj děje), grepni
  VŠECHNY čtenáře té proměnné — diagnostika, UI, IPC, testy — a u každého ověř, že
  jeho výklad pořád platí.**
- **Detekce:** `grep -n "s_xxx_state"` přes celý strom při každé změně automatu;
  u stavu, který může trvat libovolně dlouho, zkontrolovat, co o něm říká `status`.
- 🔁 **2026-10-03 (F-0226):** `12ec7ac` převedl UBX na TX v přerušení, ale komentář u
  `gps_survey_in_cmd` (`gps.h:93`) dál tvrdil „Blokující TX". Čtenářem změněného chování je
  i komentář u každé deklarace, která ho popisuje. Opraveno `docs:` commitem.
- **Commit:** `a83bf1d`
- **Stav:** aktivní

---

### L-0106 — `scpi_src_t` má dva loadery a chybějící pole v jednom z nich je neviditelné, dokud se ten transport nezeptá

- **Datum:** 2026-09-27 (F-0194, nalezeno při HW testu modulu 24)
- **Oblast:** `ipc_scpi.c` — `ipc_scpi_src_from_snap` (TCP 5025 + `POST /api/scpi`, oba CM4)
- **Symptom:** `*TST?` přes síť hlásilo FAIL (`1`) i po prošlém selftestu, přestože
  `/api/state` ze stejného snapshotu ukazovalo `"selftest":1` a USB `*TST?` hlásilo 0.
- **Příčina:** `ipc_scpi_src_from_snap` (druhý přehrávač téhož `scpi_src_t` vedle USB
  loaderu `scpi_src_load_cm7_ex`) pole `selftest_pass` prostě nikdy nepřiřadila —
  ani mrtvý zápis, ani špatné pořadí, jen chybějící řádek. Snapshot přitom hodnotu
  (`selftest_res`) nesl od začátku; nikdo si jen nevšiml, že se nikam nekopíruje.
- **Oprava:** `s->selftest_pass = (sn->selftest_res == 1);` + test v `ipc_selftest`
  (nastaví `g_selftest_res` na 1 i 2, ověří `ipc_scpi_src_from_snap` obojí).
- **Pravidlo:** **Když má jedna instrument-state struktura víc než jeden loader,
  NEOPRAVUJ nalezené pole jednotlivě — vypiš si VŠECHNA pole struktury a projdi
  je proti KAŽDÉMU loaderu najednou.** Tohle je třetí nález stejné třídy u
  `scpi_src_t` (IPC v11 slepý readback nastavení, F-0185 USB `memset` nad
  plněním, teď F-0194) — se třetím výskytem přestává být náhoda a je čas na
  systematickou kontrolu všech polí, ne další jednotlivou opravu.
- **Detekce:** pro každé pole `scpi_src_t` (`scpi.h`) porovnej, jestli ho nastavují
  VŠECHNY loadery (`grep -n "src->\|s->" scpi.c ipc_scpi.c` a diff seznamů polí);
  na HW `scpi X` vs `scpi ipc X` pro každý readback (stejný test jako L-0098).
- **Commit:** `72bf839`
- **Stav:** aktivní

---

### L-0107 — Když stav plní VÍC konzumentů, nulovací funkce musí projít VŠECHNY, ne jen toho, kvůli kterému vznikla

- **Datum:** 2026-09-27 (F-0195, nalezeno přezkumem vlastních oprav modulu 24)
- **Oblast:** `fpga_freq.c` — `fpga_stat_break()`/`fpga_stat_flush()` nad `s_acc[FPGA_ACC_N]`
- **Symptom:** žádný pozorovaný na HW (nález ze statického přezkumu) — HYPOTÉZA
  s mechanismem: při změně měřeného signálu nebo díře v `SEQUENCE` uprostřed
  10s datalogové periody by `s_acc[FPGA_ACC_DATALOG]` dál tiše sčítal cykly
  a hradla z obou stran přechodu, a výsledek by se zapsal do trvalého datalogu
  s příznakem `freq_avg=1` (= „čistý průměr"), ačkoli je to směs dvou různých
  kmitočtů.
- **Příčina:** `fpga_stat_break()` vznikla u F-0193 (SEQUENCE gap) a `fpga_stat_flush()`
  u F-0188 (přechod REAL↔SIM / změna signálu) — obě řešily tehdy jediného
  známého konzumenta `s_acc[FPGA_ACC_STATS]` (živá statistika). O pár řádků níž
  ale existuje **druhý** akumulátor téhož pole, `s_acc[FPGA_ACC_DATALOG]`
  (`datalog.c` ho čte přes `fpga_acc_take` jednou za periodu) — L-0101 už
  přesně tohle pravidlo formuloval („nuluj i to, co je na cestě"), jenže se
  aplikovalo jen na akumulátor, kvůli kterému se psalo, ne na `FPGA_ACC_N`
  jako celek.
- **Oprava:** obě funkce teď iterují `for (i = 0; i < FPGA_ACC_N; i++)` místo
  pevného indexu `FPGA_ACC_STATS` (`fpga_freq.c`).
- **Pravidlo:** **Když pole/struktura má víc slotů pro víc nezávislých
  konzumentů (`s_acc[FPGA_ACC_N]`, podobně by to platilo pro cokoli
  parametrizované `enum`em konzumenta), nulovací/reset funkce MUSÍ iterovat
  přes VŠECHNY sloty, ne jen přes ten, kvůli kterému vznikla.** Při psaní
  takové funkce vypiš si všechny čtenáře pole (grep na jeho jméno), ne jen
  toho, co motivoval opravu — přesně to L-0101 už žádal a tady se to
  nedodrželo doslova.
- **Detekce:** grep `s_acc\[` v `fpga_freq.c` — každá funkce, která na pole
  sahá s pevným indexem místo smyčkou přes `FPGA_ACC_N`, je kandidát na
  stejnou mezeru.
- **Commit:** (F-0195 fix, viz git log)
- ✅ **OVĚŘENO NA HW 2026-09-28** (power-cyklus, bez FPGA/GPS): reprodukce dle
  postupu výše (`fpgasim on 10000000`, po detekci hranice periody `fpgasim on
  10500000` uprostřed okna) — postižený datalogový záznam `#165819` nese čistou
  hodnotu `10.500.000,00000Hz`, ne směs (predikce bez opravy by dala hodnotu
  mezi 10,0 a 10,5 MHz podle toho, jak daleko v periodě ke změně došlo).
- **Stav:** aktivní

---

### L-0108 — Test, který mění globál a spoléhá, že ho pomocná funkce přečte, si tu funkci musí přečíst — ne věřit komentáři vedle volání

- **Datum:** 2026-09-28 (F-0196, nalezeno při HW ověřování opravy F-0194)
- **Oblast:** `ipc.c` — `ipc_selftest()`, nový test pro `selftest_pass` přidaný
  spolu s F-0194
- **Symptom:** `SELFTEST: 15/16 FAIL #12` po každém bootu (ověřeno na HW) —
  konzistentně, ne přerušovaně, protože příčina byla deterministická logická
  chyba, ne závod.
- **Příčina:** Nový test (`ipc.c:850-857`, commit `72bf839`) nastavil
  `g_selftest_res = 1`, zavolal `ipc_stamp(&t)` a čekal, že
  `t.snap.selftest_res` teď bude `1` — komentář u volání to tvrdil doslovně
  („ktere ho plni ipc_stamp nize"). `ipc_stamp()` ale dělá jen `memset` snapu
  na nulu + zápis magic/version/size (řádek 80); `g_selftest_res` nikdy nečte.
  Skutečné plnění `snap.selftest_res = g_selftest_res` dělá až `ipc_publish()`,
  a to nad **globálním** `g_ipc`, ne nad lokální testovací instancí `t`. Test
  tedy porovnával `t.snap.selftest_res` (vždy 0 po `ipc_stamp`) s očekávanou
  `1`, a assert `== 1` spolehlivě spadl.
- **Oprava:** za každé `ipc_stamp(&t)` v testu doplněno explicitní
  `t.snap.selftest_res = g_selftest_res;` (`ipc.c:852,854`) — test si teď plní
  pole sám, místo aby spoléhal na funkci, která ho neplní.
- **Pravidlo:** **Když test volá pomocnou funkci a předpokládá, co udělá s
  konkrétním polem, ověř si to v TĚLE té funkce, ne v komentáři vedle volání —
  a zvlášť když je pomocná funkce sdílená (`ipc_stamp` slouží i `ipc_init`,
  kde žádné doplňkové pole plnit nemá).** Komentář, který popisuje chování
  cizí funkce, je tvrzení, ne důkaz; ironicky přesně tenhle vzorec (věřit
  textu místo kódu) cituje i commit zprávy oprava samotná — L-0018 tuhle třídu
  už jednou pojmenovala a tady se zopakovala o úroveň hlouběji: v testu, který
  měl být pojistkou proti přesně takové chybě.
- **Detekce:** `selftest` → `SELFTEST: N/16 PASS` musí být vždy N=16 na čistém
  stromu (baseline, CLAUDE.md bod 3); jakýkoli nový sub-test, který manipuluje
  globál a hned volá pomocnou funkci, si zaslouží druhé čtení té funkce
  (ne jen jejího komentáře) předtím, než se commitne.
- **Commit:** (F-0196 fix, viz git log)
- ✅ **OVĚŘENO NA HW 2026-09-28** (power-cyklus): před opravou `SELFTEST:
  15/16 FAIL #12`, po opravě + reflash CM7 + power-cyklus `SELFTEST: 16/16
  PASS`.
- **Stav:** aktivní

### L-0109 — Citovat vzor v komentáři nestačí; oprava musí ten vzor DODRŽET CELÝ, ne jen jeho poslední krok

- **Datum:** 2026-09-29 (F-0199, nalezeno přezkumem vlastní opravy F-0197)
- **Oblast:** `syscfg.c` — `syscfg_load()`, atomický commit `g_meas_cfg`
- **Symptom:** Žádný pozorovatelný — latentní vada nalezená statickým
  srovnáním, ne selháním na desce.
- **Příčina:** Oprava F-0197 (2026-09-28) přidala komentář „Stejný vzor jako
  `scpi.c`/`ipc.c`/okno MATH/`setup_load()`" a skutečně zkopírovala **poslední
  krok** toho vzoru (`taskENTER_CRITICAL(); g_meas_cfg = c; taskEXIT_CRITICAL();`),
  ale vynechala **první krok**, který mají všechny tři citované sesterské
  implementace: `taskENTER_CRITICAL(); c = g_meas_cfg; taskEXIT_CRITICAL();`
  hned po deklaraci lokální kopie. Bez něj zůstala `meas_cfg_t c;`
  neinicializovaná a výplňové bajty struktury (zarovnání `double` polí na 8 B)
  se do globálu zapsaly jako obsah zásobníku, ne jako to, co tam bylo předtím.
  Kompilátor (`-fanalyzer -Wmaybe-uninitialized`, stejné flagy jako produkční
  build) to **nezachytí** — vidí jen pojmenovaná pole, ne padding bajty
  struktury, takže žádný z nástrojů, kterými se projekt rutinně kontroluje,
  tuhle třídu vady nenajde.
- **Oprava:** Doplněn chybějící první krok (`c = g_meas_cfg;` pod kritickou
  sekcí) — `syscfg.c:257-258`, přesně podle citovaného vzoru.
- **Pravidlo:** **Když komentář/commit zprávy tvrdí „stejný vzor jako X",
  ověř to porovnáním KAŽDÉHO kroku s X, ne jen toho, který zrovna píšeš.**
  Citace vzoru bez úplného dodržení je horší než žádná citace — vytváří dojem
  prověřené shody tam, kde je jen částečná. Platí obzvlášť pro „atomický
  commit lokální kopie" idiom (read-modify-write pod kritickou sekcí): chybí-li
  **read** krok, `write` kopíruje nedourčená data tam, kde předtím byla
  struktura netknutá.
- **Detekce:** U každého nového „lokální kopie → commit" bloku zkontroluj, že
  lokální proměnná je **buď** (a) inicializovaná čtením z global **před**
  prvním přepisem pole, **nebo** (b) skutečně KAŽDÉ pole struktury má
  explicitní přiřazení A struktura nemá zarovnávací mezery (ověř `sizeof`
  vs. součet velikostí polí). `-fanalyzer`/`-Wmaybe-uninitialized` na tohle
  nestačí — nutná ruční kontrola proti sesterským implementacím.
- **Vztah k lekcím:** rozšiřuje `L-0018` (dvě místa počítající totéž se
  rozejdou) o jemnější variantu: tady se nerozešly DVĚ NEZÁVISLÉ
  implementace, ale JEDNA nová implementace se rozešla s vlastním prohlášeným
  vzorem. Souvisí i s `L-0012` (oprava symetrické instance se nepřenesla
  celá) — tady šlo o první KROK vzoru, ne o celou druhou instanci.
- **Commit:** F-0199 fix, viz git log (`fix(F-0199): ...`)
- ✅ **OVĚŘENO NA HW 2026-09-29** (reflash + power-cyklus): `Reset: power-on`,
  `SELFTEST: 16/16 PASS`, `ULOZISTE: syscfg OK`, SCPI nad `g_meas_cfg`
  odpovídá zdravě. Žádná regrese — jak se čekalo (vada je ve výplňových
  bajtech, funkčně neviditelná, ověření dokazuje „bez regrese", ne přímo
  „byla tam garbage").
- **Stav:** aktivní

---

### L-0110 — Napěťové kanály ADS1115 pojmenované podle NÁVRHU, ne podle NETLISTU

- **Datum:** 2026-10-02
- **Oblast:** periferie / kalibrace / dokumentace
- **Symptom:** Headline displeje byl přeškrtnutý (`warn_rail_bad()` hlásil napájecí
  větev mimo rozsah), přestože uživatel fyzicky ověřil, že napájení je v pořádku.
  Hlubší prozkoumání ukázalo, že firmware porovnával AIN2/AIN3 proti nominálům
  12 V / 5 V a gainům odvozeným z komentáře "v2.0 rev2" — spekulativní tabulky
  napsané předem, nikdy neověřené proti skutečně osazeným rezistorům.
- **Příčina:** Komentáře i `g_calib` výchozí hodnoty vycházely z PŘEDPOKLÁDANÉHO
  zapojení desky (`ADS1115_HW_DIVIDERS_REV2` plán), ne ze skutečného schématu.
  Export netlistu (`kicad-cli sch export netlist`) z `FPGA_Module_2_1.kicad_sch`
  ukázal realitu: AIN0=OCXO_VC (R51=15k/R52=10k, gain 2,5 — firmware ho vůbec
  needelal), AIN1=**VBUS** (R53=100k/R54=4k99, gain ~21,04 — NE RF_Level/AD8307,
  ten na desce **fyzicky není**, v žádném z 5 listů schématu), AIN2=+3V3
  (R55=10k/R56=10k, gain 2,0 — firmware počítal s "12V" a gainem 4,768),
  AIN3=+5V (R57=10k/R58=22k, gain ~1,4545 — firmware počítal s gainem 1,971).
- **Oprava:** `freertos_task_sensors.c` aplikuje správné gainy na všech 4
  kanálech (`AIN0_GAIN_OCXO_VC`, `AIN1_GAIN_VBUS`, `g_calib.gain_12v/gain_5v`
  přepočtené v `calib.c`). AD8307/RF_Level HW nepřítomnost je jeden centrální
  flag `RF_LEVEL_HW_PRESENT` (`calib.h`) — zamezuje publikaci `SCPI_V_RF`/
  `IPC_V_RF` (`scpi.c`, `ipc.c`), takže `MEAS:POW?`, web JSON `rf_dbm` i UI
  karty (hbar, dualch, warn, main-screen bargraf) korektně hlásí "nevím"
  místo dBm spočítaného z napětí VBUS.
- **Pravidlo:** **Komentář popisující zapojení HW je HYPOTÉZA, dokud není
  ověřený proti reálnému schématu/netlistu — "v2.0 rev2 plán" není totéž co
  "takhle to je osazené".** Exportuj netlist (`kicad-cli sch export netlist
  --format kicadxml`) a dohledej skutečné rezistory/nety, než se podle
  komentáře píše gain nebo PGA rozsah.
- **Detekce:** Žádná regex nechytí "komentář neodpovídá schématu" — jediná
  obrana je opakovat netlist-export kontrolu při jakékoli pochybnosti o
  analogovém kanálu, ne věřit existujícímu komentáři jen proto, že tam je.
- **Vztah k lekcím:** stejná třída jako `L-0012` (dvě místa nesoucí stejný
  fakt se rozejdou) — tady šlo o DESET míst (štítky, gainy, SCPI, IPC, web,
  datalog, autocal) odvozených ze stejného špatného předpokladu.
- **Commit:** fix voda do git log (hash viz `git log --oneline -1`)
- ⬜ **NEOVĚŘENO NA HW** — oprava je zatím jen přeložená (0 varování,
  92/0/2 audit baseline), žádný power-cyklus s multimetrem na AIN0-3 zatím
  neproběhl.
- **Stav:** aktivní

---

### L-0111 — Široké srovnání jako enable širokého registru (FPGA fanout)

- **Datum:** 2026-10-02
- **Oblast:** FPGA / timing
- **Symptom:** Po opravě kritické cesty ve `win_recip` (S1 rozdělena 28+28b)
  synteza hlásila HORŠÍ agregát (TNS −1,25 → −13,52 ns, 11 → 56 endpointů).
  Nejhorší cesty: `u_phy/bit_in_6/7 → rx_shadow_*/CE`.
- **Příčina:** `spi_slave_phy.v` gatoval 1024b shift registr `rx_shadow` živým
  11bitovým srovnáním `bit_in != 1024` a load/nulování 2048 FF (`tx_shadow`,
  `rx_shadow`) srovnáním `bit_in == 0`. Výsledek srovnání musel v jednom taktu
  doběhnout do tisíců CE/D vstupů; syntezér rozprostřel komparátor k cílům.
  Vada existovala už předtím (slack −0,002 až −0,246 ns, A/B syntézou doloženo),
  přidání registrů jinde jen zhoršilo umístění.
- **Oprava:** Registrované příznaky spočtené takt předem: `rx_done`
  (nastaví se při shiftu s `bit_in == 1023`) a `rx_armed` (≡ `bit_in == 0`,
  mění se na týchž dvou místech jako `bit_in`) — `spi_slave_phy.v`.
  Výsledek: 0 porušení, Fmax 114,5 MHz.
- **Pravidlo:** **Enable/mux širokého registru (stovky FF) musí řídit výstup
  JEDNOHO flip-flopu, nikdy živá kombinační funkce více bitů — počítej
  podmínku takt předem a ověř ekvivalenci výčtem všech míst, kde se mění
  zdrojový čítač.**
- **Detekce:** Timing report — nejhorší cesty typu `<čítač>_N/Q → <široký
  registr>_M/CE`. Při opravě timingu VŽDY dělej A/B syntézu (revert jen
  své změny), jinak nepoznáš, jestli nová porušení způsobila oprava, nebo
  jen odkryla/zhoršila starou vadu.
- **Commit:** zatím necommitnuto (viz `git log` po commitu)
- ⬜ **NEOVĚŘENO NA HW.**
- **Stav:** aktivní

---

### L-0112 — `HAL_I2C_IsDeviceReady` na chybějícím čipu hlásí TIMEOUT, ne NACK

- **Datum:** 2026-10-02
- **Oblast:** periferie / I2C
- **Symptom:** (zachyceno před nasazením) Periodická sonda neosazeného AD5693R
  přes `HAL_I2C_IsDeviceReady` by každých 10 s spustila obnovu I2C1
  (9 pulzů SCL + re-init) — sběrnice přitom zdravá, jen čip chybí.
- **Příčina:** HAL H7 (`stm32h7xx_hal_i2c.c`, konec `HAL_I2C_IsDeviceReady`)
  po vyčerpání pokusů nastaví `ErrorCode |= HAL_I2C_ERROR_TIMEOUT` i tehdy,
  když každý pokus skončil čistým NACKem. `i2c1_recover_if_wedged()` bere
  TIMEOUT jako „slave drží sběrnici".
- **Oprava:** Sonda NOP zápisem (`HAL_I2C_Master_Transmit`, CMD 0) — NACK
  pak končí jako `HAL_I2C_ERROR_AF`, který obnovu nespouští
  (`freertos_task_sensors.c`, `ad5693_probe_nop`).
- **Pravidlo:** **Kde po I2C transakci rozhoduje chybový kód (obnova,
  back-off, počítadla), nesonduj přítomnost zařízení `HAL_I2C_IsDeviceReady`
  — použij skutečnou neškodnou transakci, jejíž NACK skončí jako AF.**
- **Detekce:** `grep -n IsDeviceReady` v kódu, který běží PŘED
  `i2c1_recover_if_wedged()`/podobnou kontrolou `HAL_I2C_GetError`.
  ⚠️ Zbytková latentní varianta (předchozí stav, neopravováno): UART
  `scan1` IsDeviceReady používá a nechá v `hi2c1` TIMEOUT; kdyby SensorsTask
  hned potom nezískal mutex, kontrola za blokem by ho viděla.
- **Commit:** zatím necommitnuto
- **Stav:** aktivní

---

### L-0113 — Konfigurace externího čipu přežila výměnu desky: firmware dál krmil PLL, která už neexistuje

- **Datum:** 2026-10-03 (dotaz uživatele „proč nejde 1PPS signál z GPS", F-0218)
- **Oblast:** `gps.c` — UBX-CFG-TP5 (TIMEPULSE modulu NEO-7M), deska FPGA 2.0 → 2.1
- **Symptom:** na vstupu FPGA PIN33 (`GPS_1PPS`) nikdy nebyl 1PPS. S fixem tam šlo 100 kHz,
  bez fixu 10 Hz. UI k tomu hlásilo „Time Pulse 100 kHz" a blokové schéma kreslilo
  „UART/1PPS" na spoji GPS→STM32, kudy 1PPS vůbec nevede.
- **Příčina:** `GPS_TP_FREQ_HZ = 100000` byla správná hodnota pro desku 2.0, kde TIMEPULSE
  napájel hardwarovou PLL na listu GPSDO. Deska 2.1 PLL nemá a net `GPS_CLK_Buff` končí na
  FPGA PIN33 jako 1PPS. Při přechodu desek se prošly piny FPGA, napětí i SPI, ale **hodnota,
  kterou firmware posílá do čipu mimo MCU**, se proti novému spotřebiteli signálu nikdy
  nekontrolovala. Komentář u konstanty to přitom říkal přímo („GPSDO PLL reference … JP2").
  Odkazoval na HW, který na nové desce není.
- **Oprava:** `GPS_TP_FREQ_HZ` 100000 → 1 (`gps.c:362`), bez fixu dál 10 Hz (zadání uživatele).
  Srovnány texty v UI (`app_gpsdo.c:961`, `:4399`, `:7176`). Commit `3cbbdb9`.
- **Pravidlo:** **Při výměně revize desky projdi každou hodnotu, kterou firmware ZAPISUJE do
  čipu mimo MCU (UBX/I²C/SPI konfigurace, DAC, PLL registry), a u každé najdi v NOVÉM netlistu,
  kdo signál spotřebovává.** Komentář, který jmenuje součástku nebo propojku (PLL, JP2…),
  je tvrzení o desce. Ověř, že ta součástka na nové desce existuje.
- **Detekce:** při změně revize grep na konfigurační zápisy do externích čipů (`ubx_send`,
  `HAL_I2C_Mem_Write`, `wr_masked`, `REGMAP[]`) a ke každému dohledat spotřebitele v netlistu.
  Ruční krok, patří do checklistu přechodu desky (sourozenec L-0110: tam šlo o čtení
  kanálů ADS1115 podle návrhu místo podle netlistu, tady o zápis).
- **Commit:** `3cbbdb9`
- 🔁 **2026-10-03, F-0225:** táž třída hned v opravě samé. F-0218 změnila frekvenci na 1 Hz, ale
  **střídu 50 % převzala ze staré konfigurace 100 kHz** → 1PPS měl pulz 500 ms. Opraveno `b8019dd`
  (100 ms, `isLength`). Pravidlo platí pro **každé pole** zapisované konfigurace, ne jen pro to,
  které je zjevně špatně.
- **Stav:** aktivní (⬜ oprava neověřena na HW — osciloskop na R50/PIN33 po power-cyklu)

---

### L-0114 — Komentář varoval před pastí knihovny, která v použité verzi není, a tím blokoval opravu

- **Datum:** 2026-10-03 (F-0219, F-0221, F-0222)
- **Oblast:** `gps.c` — UBX konfigurace přes USART1 (`ubx_send`, `gps_init`)
- **Symptom:** konfigurace TIMEPULSE (1PPS pro FPGA) se posílala jen jednou při startu, do RAM
  modulu a bez kontroly ACK. Po samostatném resetu GPS modulu ji STM32 nikdy neobnovil.
- **Příčina:** komentář v `gps_init` tvrdil, že blokující `HAL_UART_Transmit` drží `huart->Lock`
  a souběžný re-arm RX v callbacku by dostal `HAL_BUSY`, takže RX umře navždy. Opakované
  vysílání za běhu tím vypadalo nebezpečně. V HAL 1.11.6 to **neplatí**: TX (`gState`) a RX
  (`RxState`) jsou nezávislé automaty bez `__HAL_LOCK`. Tvrzení „za běhu žádný TX neběží"
  navíc vyvracel kód o kus dál (SURVEY z UiTasku, `gps glonass` z UartTasku).
- **Oprava:** `ubx_send` přes `HAL_UART_Transmit_IT` se statickým bufferem (start v kritické
  sekci, čekání na předchozí rámec přes `vTaskDelay`), `gps_tick()` z defaultTasku posílá TP5
  znovu 1×/min, počítadla `UBX:odeslano/neodeslano` v `gpsraw`. Commit `12ec7ac`.
- **Pravidlo:** **Komentář, který popisuje chování cizí knihovny („HAL drží zámek", „tahle funkce
  blokuje"), ověř proti zdroji té knihovny v projektu a uveď její verzi.** Neověřená past se
  stává důvodem nedělat správnou věc. A **konfiguraci, která žije jen v RAM externího čipu
  a nedá se ověřit čtením, posílej opakovaně** (idempotentní zápis), ne jednou při startu
  hostitele. Externí čip se může resetovat sám.
- **Detekce:** grep komentářů na jména interních symbolů knihovny (`Lock`, `__HAL_LOCK`,
  `gState`) a jejich porovnání s `Drivers/`. U každého zápisu konfigurace do externího čipu
  se zeptej: kdo ji obnoví, když se čip resetuje a hostitel ne?
- **Commit:** `12ec7ac`
- **Stav:** aktivní (⬜ neověřeno na HW: osciloskopem ověřit, že opakované TP5 neruší 1PPS)

---

### L-0115 — Textový parser na sdílené lince s binárním protokolem se musí synchronizovat na začátku rámce, ne jen na konci

- **Datum:** 2026-10-03 (F-0223)
- **Oblast:** `gps.c` — `gps_feed_char` (NMEA vedle UBX na USART1)
- **Symptom:** po každém UBX příkazu se ztratila jedna NMEA věta. Tiše, žádné počítadlo.
- **Příčina:** řádek se ukončoval jen na `\r\n`. Binární odpověď UBX-ACK `\r\n` nemá, takže
  se přilepila před další NMEA větu, řádek nezačínal `$` a parser zahodil obojí.
- **Oprava:** `$` vždy začne nový řádek, useknutý začátek se počítá (`RSY:` v `gpsraw`).
  Commit `01c7e25`.
- **Pravidlo:** **Když linkou tečou dva protokoly (text + binární), parser jednoho z nich se
  musí resynchronizovat na svém počátečním znaku**, jinak cizí rámec zničí i ten následující.
  Zahozená data se počítají (L-0017).
- **Detekce:** u každého řádkového parseru se zeptej: co udělá s bajty, které přijdou mezi
  koncem jedné věty a začátkem další?
- **Commit:** `01c7e25`
- **Stav:** aktivní

---

### L-0116 — O tom, co je ve FPGA, rozhoduje NETLIST: ani zdroják, ani timing report to neřeknou

- **Datum:** 2026-10-03 (F-0201, carry-chain TDC)
- **Oblast:** FPGA, `tdc.v`, syntéza
- **Symptom:** carry-chain TDC byl „implementovaný", komentáře tvrdily rozlišení 625 ps, build
  prošel a timing report byl v pořádku. Přitom v bitstreamu žádný TDC nebyl a měření bylo hrubé 10 ns.
- **Příčina:** výraz `{15{sig}}+1` je pro syntézu triviální (všechny operandy jsou tentýž bit),
  zredukovala ho na invertor + 1 FF. Atribut `(* keep *)` zachovává JMÉNA sítí, ne aritmetiku.
  Timing report „v pořádku" nic nedokazoval — cesty, které neexistují, se nemají čím porušit.
- **Oprava:** řetěz se staví z PRIMITIV `ALU` přímo (`tdc_chain`), `Frequency_Counter_FPGA_Module/
  sim/check_tdc_netlist.py` po každé syntéze ověří v `.vg` počet `ALU`, `I0=VCC/I1=GND`, hlavu
  řetězu a použití všech `SUM`. Commit `f968517`.
- **Pravidlo:** **Funkci, která závisí na tom, že syntéza nic nezjednoduší (řetězy, zpoždění,
  kruhové oscilátory), ověřuj v syntetizovaném NETLISTU — a to po každé syntéze, ne jednou.**
- **Detekce:** `python Frequency_Counter_FPGA_Module/sim/check_tdc_netlist.py` (musí vypsat 512 `ALU`,
  512 s `I0=VCC/I1=GND`, 2 hlavy s `CIN = sig_eff`).
- **Commit:** `f968517`
- **Stav:** aktivní

---

### L-0117 — Rozpočet logiky na 100 MHz v GW1NR-9C je ~3 úrovně LUT: nejdřív změř, pak navrhuj

- **Datum:** 2026-10-03 (TDC, dekodér)
- **Oblast:** FPGA, časování
- **Symptom:** dekodér 256 tapů jedním taktem měl cestu ~20 ns (slack −10 ns), po opravách řídicích
  cest dál −2 až −3 ns u zdánlivě triviální logiky (24bitový čítač, dekódování stavu).
- **Příčina:** v tomhle fabricu stojí každý přechod mezi LUT 2–3 ns routingu (vzdálenost, fanout),
  takže na 10 ns vyjdou ~3 úrovně. Řetěz je navíc dlouhý sloupec, takže se jeho vzorky routují daleko.
- **Oprava:** (1) přesný čas jen pro hranu na hranici okna (hrany uvnitř okna se jen počítají) →
  vzorky se zmrazí clock enable na 5 taktů a kombinační dekodér má SDC multicycle; (2) one-hot stavy
  a předpočítané registrované příznaky v řídicích cestách; (3) jednotka času T_clk/16384, aby
  kalibrace byla posun místo násobení; (4) částečné součty čítače s registrovaným přenosem.
- **Pravidlo:** **Před návrhem logiky na 100 MHz v tomto fabricu změř cesty (P&R timing report,
  `timing_paths`), ne odhaduj z počtu LUT; a když funkci stačí jednou za okno, dej jí víc taktů
  (zmrazení + multicycle) místo pipeline na každý takt.**
- **Detekce:** `gw_sh build.tcl` → `Max Frequency Summary` a `Total Negative Slack` (musí být 0).
- **Commit:** `f968517`
- **Stav:** aktivní

---

### L-0118 — Kalibrační zdroj, který není statisticky nezávislý na hodinách, vyrobí v histogramu jen pár kódů

- **Datum:** 2026-10-03 (kalibrace TDC, simulace)
- **Oblast:** FPGA, kalibrace code density, simulace
- **Symptom:** simulovaná kalibrace dala jen 25 neprázdných kódů místo ~176 a σ chyby 93 ps.
- **Příčina:** model ring oscilátoru měl přesně racionální periodu (83,2 ns), takže události padaly
  jen do 25 pevných fází vůči 100 MHz hodinám. Skutečný kruh má jitter, ale spoléhat na to je riziko
  (injection locking, stabilní teplota).
- **Oprava:** v hardware dělič kruhu s pseudonáhodným modulem (16/17 oběhů podle LFSR), v simulaci
  jitter zpoždění LUT. Po opravě 178 kódů, σ 26 ps. Přitom se ukázalo, že LUT s transportním
  zpožděním dovolí kruhu oscilovat na vyšší harmonické (4× rychleji) → model s inertním zpožděním.
- **Pravidlo:** **Zdroj pro code-density kalibraci musí mít prokazatelně nerovnoměrnou fázi vůči
  vzorkovacím hodinám; ověř histogram simulací s pozitivní kontrolou (kolik kódů je neprázdných).**
- **Detekce:** `tb_tdc.sv` vypisuje `nz` (počet neprázdných kódů) a `ovf`; na desce `tdc` (`nz`≈170–180).
- **Commit:** `f968517`
- **Stav:** aktivní

---

### L-0119 — Využití registrů nad ~87 % shodí placer/router i při splněném limitu zdrojů

- **Datum:** 2026-10-03 (FPGA, TDC)
- **Oblast:** FPGA, zdroje
- **Symptom:** `PR0003 Failed to place with 165 REG(s) unPlaced` a později `PR0004 78 unrouted nets`
  při 84–88 % registrů (limit 100 %), a časování se zhoršilo i v nedotčeném PHY (setup −3,3 ns).
- **Příčina:** dlouhé carry řetězy se musí umístit do sloupců a PHY má fanout 1024; při zaplnění
  nad ~85 % heuristika nenajde rozumné umístění.
- **Oprava:** odstraněn `recip_calc` (FPGA už nepočítá kmitočet, host ho počítá přesněji z celých
  čísel `edge_count`/`dt`), zrušena Λ/Ω regrese, `gate_div` = `dt·5>>13`, zúženy čítače → 74 % FF /
  69 % logiky. TX posun v PHY předpočítán a rozdělen na 8 segmentů (ekvivalence ověřena).
- **Pravidlo:** **Držet registry pod ~80 %; co umí host (dělení, přesná aritmetika), nepočítej ve FPGA.**
- **Detekce:** `Counter_FPGA.rpt.txt` → `Register ... %` (≤ 80 %).
- **Commit:** `f968517`
- **Stav:** aktivní

---

### L-0120 — Přijatý rámec, který aplikace čte „živě", se nesmí mazat dřív, než ho dočte

- **Datum:** 2026-10-03
- **Oblast:** FPGA, `spi_slave_phy.v`, SPI STM → FPGA
- **Symptom:** FPGA ignorovala VŠECHNY povely STM (ACK, START, SET_CONFIG, žádost o CAL
  report). `tdc` hlásilo „CAL report nedorazil", ve `fpgaraw` byl trvale bit `rx_crc_error`.
  Směr FPGA → STM přitom fungoval, takže link vypadal zdravě.
- **Příčina:** PHY nulovala `rx_shadow` hned po CS↑ (stav `rx_armed`), ale `spi_app`
  (10 MHz) čte `rx_frame_flat` = `rx_shadow` ŽIVĚ ještě ~13 µs (S_RX_CRC, 126 taktů).
  Viděla samé nuly → MAGIC/CRC selhaly vždy. Komentář o řádek výš přitom tvrdil
  „rx_shadow je stabilní od CS↑ do dalšího CS↓". Vada od prvního commitu (`38d7e2b`);
  zůstala skrytá, protože SPI link do té doby nikdy neběžel.
- **Oprava:** nulování odstraněno (1024 bitů posuvu přepíše celý registr). Commit `5a00b7f`.
  End-to-end test `sim/tb_link.sv` (PHY + `spi_app`) v `run.ps1`: se starou PHY dá
  přesně symptom z desky (`flags=23`, odpověď `type=80`), s opravou `flags=43` a `type=a0`.
- **Pravidlo:** **Když modul A předává data modulu B jako „stabilní vodič", ověř, kdo
  a kdy ten registr přepisuje — komentář to nedokazuje. Testuj spojení obou modulů,
  ne každý zvlášť** (ekvivalenční test PHY proti staré PHY vadu zdědil).
- **Detekce:** `fpgaraw` → flags (bajt 3) bit 5 = `rx_crc_error` musí být 0, bit 6 `ack_ok` = 1.
- **Commit:** `5a00b7f`, `deeda88`
- **Stav:** aktivní

---

### L-0121 — Diagnostiku vstupu měř hranami na pinu, ne obsahem dat — a teprve pak viň konfiguraci

- **Datum:** 2026-10-03
- **Oblast:** FPGA, piny, Tang Nano 9K
- **Symptom:** i po L-0120 FPGA přijímala od STM samé `FF`. Osciloskop i sniffer na pinu 54
  (MOSI) ukazovaly čistý signál 0,1 / 3,2 V. Tři kola hypotéz o konfiguračních pinech
  (`use_cpu/mode/ready/done/reconfign/i2c_as_gpio`) nepomohla.
- **Příčina:** vadný vstup pinu 54 na konkrétním kusu Tang Nano. Po výměně modulu
  MOSI funguje (přijato `A5 02 06`, CRC sedí, `ack_ok=1`).
- **Co rozhodlo:** počítadla hran přímo za vstupním bufferem v rámci (`[116,117]` hrany
  MOSI, `[124,125]` hrany SCK): SCK 1024, MOSI **0** → pad do logiky nic nepouští. Pak
  pull-down na pinu (FPGA dál čte 1) → pin neplave, ani ho nedrží konfigurace (UG290:
  `CLKHOLD_N` patří do SSPI, ta byla uvolněná od začátku).
- **Pravidlo:** **U „vstup čte konstantu" nejdřív postav počítadlo hran hned za IBUF
  a porovnej se sousedním pinem téže banky. Než viníš konfiguraci, ověř v dokumentaci,
  do které skupiny pin patří. Vadný kus modulu je legitimní hypotéza, když SW i konfigurace
  prokazatelně sedí — a ověří se výměnou kusu.** (Doplňuje tabulku „HW obviněn — a byl
  nevinný": tady byl HW skutečně vadný, ale až po vyloučení SW měřením.)
- **Detekce:** `fpgaraw` bajty `[66][67][107]` = přijaté `rx[0..2]`, `[112..115]` = CRC
  spočtené/přijaté, `[116,117]` hrany MOSI, `[124,125]` hrany SCK (FW ≥ 0x0404).
- **Commit:** `9ae86de`, `fbd1630`, `deeda88`
- **Stav:** aktivní

---

### L-0122 — Gowin si volby `set_option` pamatuje v projektu: vrácení = výslovně nastavit 0

- **Datum:** 2026-10-03
- **Oblast:** FPGA build (`build.tcl`, `gw_sh`)
- **Symptom:** po odstranění řádků `set_option -use_*_as_gpio 1` z `build.tcl` hlásil nový
  `impl/pnr/device.cfg` pořád `READY/DONE/I2C/RECONFIG_N regular_io = true`.
- **Příčina:** `gw_sh` ukládá volby do projektu; smazaný řádek je nezmění.
- **Oprava:** volby výslovně `set_option ... 0`; ověřeno v `device.cfg`. Commit `deeda88`.
- **Pravidlo:** **Výsledek konfigurace FPGA ověřuj v `impl/pnr/device.cfg`, ne ve skriptu.
  Vrácení volby = zapsat výchozí hodnotu, ne smazat řádek.**
- **Detekce:** `head impl/pnr/device.cfg`
- **Commit:** `deeda88`
- **Stav:** aktivní

---

### L-0123 — USB hub mezi PC a deskou umí „umlčet" CDC konzoli, zatímco zbytek běží

- **Datum:** 2026-10-03
- **Oblast:** USB CDC konzole (COM10), diagnostika
- **Symptom:** COM10 ve Windows `OK`, zápis prochází, ale na `ping` žádná odpověď —
  opakovaně, i po power-cyklu. Displej, ETH (`/api/state`, SCPI 5025) i měření jely.
  Sonda ukázala, že konzole na straně STM je v pořádku (ring prázdný, nic nezahozeno).
- **Příčina:** USB hub, na kterém byly STM, FPGA a programátor. Po připojení přímo do PC
  (bez hubu) konzole odpovídá.
- **Pravidlo:** **Když konzole mlčí a ETH/displej jedou, nejdřív odstraň USB hub, než
  začneš hledat ve firmwaru.** Diagnostika mezitím jde přes ETH (`/api/state`) nebo
  ČTENÍM sondou (`STM32_Programmer_CLI mode=HOTPLUG -r32`; ⚠️ zastaví jádro → I2C4 do
  power-cyklu).
- **Detekce:** `curl http://<IP>/api/state` odpovídá, `ping` na COM10 ne.
- **Commit:** —
- **Stav:** aktivní

---

### L-0124 — Změna parametru musí projít VŠEMI odvozenými konstantami (jediný zdroj)

- **Datum:** 2026-10-03
- **Oblast:** FPGA, `top.v` / `tdc.v`, kalibrace TDC
- **Symptom:** první CAL report z desky: 16,8 % (A) a 18,5 % (B) událostí za koncem řetězu,
  ale `status` hlásil `retez kratky A:0 B:0`.
- **Příčina:** moje změna `CAL_LOG2` 22 → 20 (`97da8c3`) se nepropsala do `OVF_LIM` v `top.v`,
  kde zůstalo natvrdo `1 << (22 − 4)` = 262 144 > 175 705. Mez tedy byla 4× vyšší, než měla být.
- **Oprava:** `localparam CAL_LOG2` v `top.v` jako jediný zdroj, předává se do obou `tdc_chan`
  i do `OVF_LIM`. Commit `7ada866`.
- **Pravidlo:** **Při změně parametru grepni jeho STARÉ ČÍSLO v celém návrhu (ne jen jméno) —
  natvrdo opsané hodnoty jméno nenese.** A detektor, který má něco hlásit, ověř pozitivní
  kontrolou: dokud nevidíš, že naskočí na skutečně vadném stavu, nic nedokazuje.
- **Detekce:** `status` → `retez kratky A:1` při `za koncem retezu` > 1/16 událostí (`tdc`).
- **Commit:** `7ada866`
- **Stav:** aktivní

---

### L-0125 — Zpoždění carry řetězu na křemíku NENÍ STA model: dimenzuj délku podle měření

- **Datum:** 2026-10-03
- **Oblast:** FPGA, `tdc.v` (`tdc_chain`), GW1NR-9C
- **Symptom:** 256 stupňů mělo podle STA (57 ps) pokrýt 14,6 ns; na desce pokryly jen ~8,3 ns
  (~32 ps/stupeň) a ~17 % hran dostalo kód 255 (chyba až ±0,9 ns). Obsazených kódů jen ~133/256.
- **Oprava (varianta B, FW 0x040A):** řetěz 2× delší (512 ALU/kanál), vzorkuje se každý druhý
  stupeň (`STRIDE = 2`) → registry, dekodér i tabulky beze změny, pokrytí ~16,6 ns.
  Průchozí stupně bez použitého `SUM` drží `syn_keep`; `check_tdc_netlist.py` teď skončí chybou,
  když je stupňů méně než 2 × 511. Simulační model ALU přestaven na 32 ± 12 ps (křemík, ne STA).
  Simulace: kontrola STRIDE 1 (model 32 ps) reprodukuje desku: 19 % za koncem, σ 351 ps; STRIDE 2: 0 za koncem, σ 28/32 ps, A−B 0,0 ps, 6/6 PASS
- **Pravidlo:** **Délku TDC řetězu navrhuj s rezervou ≥ 1,5× proti STA a ověř ji kalibrací
  na desce (`za koncem retezu` ≈ 0). Simulační model ladit podle změřeného křemíku.**
- **Detekce:** `tdc` → `za koncem retezu` musí být ≈ 0, `kodu` řádově 150–256.
- **Commit:** `6e4fcd7`
- **Stav:** ⚠️ ČÁSTEČNĚ: na desce (FW 0x040A) pokrytí OK (0 za koncem), ale σ okna ~1,9 ns (horší než 0,7 ns se STRIDE 1) — dominantní kódy 7 % / 13 % událostí, jen ~100 obsazených kódů. Příčina neznámá (HYPOTÉZA: bubliny/nepravidelnost skutečného carry řetězu, model je nezná). Další krok: výpis celého histogramu (`tdc hist`), pak dekodér počtu jedniček nebo STRIDE 1 s 384 tapy.

---

### L-0126 — Než optimalizuješ zdroje FPGA, rozlož je po signálech; největší žrout bývá infrastruktura

- **Datum:** 2026-10-04
- **Oblast:** FPGA, `spi_slave_phy.v`, `spi_app.v` (#264)
- **Symptom:** registry 77–79 %, CLS 87–88 %; pro TDC 1PPS (#262) a akumulátory (#263) nebylo místo.
  Odhad „kde to je" mířil na TDC (řetězy, kalibrace).
- **Příčina:** rozpad netlistu (`sim/res_breakdown.py`) ukázal, že **přes polovinu registrů držely
  rámce SPI** — 128bajtový rámec existoval 3× v registrech (PHY TX posuvný registr, PHY RX, aplikace `tx_b`).
  TDC vzorky byly jen 512 FF.
- **Oprava:** rámce do blokové RAM (1 blok), PHY po bajtech a taktovaná přímo SCK (vedlejší zisk: strop SPI
  ~8–10 → ~40 MHz), RX zpracovaný za letu. FF 77 % → 38 %. Commit `ebba0b0`.
- **Pravidlo:** **Před optimalizací spusť `sim/res_breakdown.py` a seřaď podle velikosti. Data, která se
  přenášejí po bajtech, nepatří do registrů, ale do blokové RAM.** Při přechodu na sestavování po bajtech
  hlídej konzistenci snímku (vícebajtová pole se během skládání nesmí měnit) — a otestuj to zátěží s pozitivní
  kontrolou (`tb_link`: vypnutá ochrana → 61 chyb).
- **Detekce:** `python sim/res_breakdown.py 30`; `impl/pnr/Counter_FPGA.rpt.txt` (Register, CLS).
- **Commit:** `ebba0b0`
- **Stav:** aktivní (⬜ neověřeno na HW)

### L-0127 — Asynchronní vstup se nesmí počítat z jediného klopného obvodu; detekce hrany patří za synchronizátor

- **Datum:** 2026-10-06
- **Oblast:** FPGA, `tdc.v` (`tdc_chan`), `spi_app.v` (`win_recip`)
- **Symptom:** při měření vlastní reference 10 MHz ~20× za 30 min 10 000 004 Hz místo 10 000 000 Hz,
  pak zase správně. +4 Hz při hradle 0,25 s = přesně jedna hrana navíc, Δt v pořádku.
- **Příčina:** `t0` vzorkuje asynchronní vstup jedním FF a `rise_c = t0 & ~t0p` se počítalo přímo. Když `t0`
  zachytí hranu metastabilně, `rise_c` ji vidí jako 1, ale `t0p` si uloží ještě 0 → v dalším taktu
  `rise_c` = 1 podruhé. Synchronní signál (vlastní reference) drží fázi hrany u hran hodin, takže se trefuje opakovaně.
- **Oprava:** počítání ze synchronizované `rise_s` (`t0s`/`t0sp`), uzavírací hranu páruje `win_recip` s
  `trig_ack` v okně 0..2 takty (`pend`). Spouštění přesného času beze změny. FW 0x040F, commit `38011fc`.
  STM pojistka `fpga_freq_miscount` (okno o celý násobek kroku hrany se nezobrazí ani nezapočítá), `8bd4045`.
- **Pravidlo:** **Každý signál z asynchronní domény, který se POČÍTÁ nebo řídí stavový automat, musí projít
  dvěma FF, než se z něj dělá hrana; rychlou nesynchronizovanou cestu smí mít jen časová značka, a pak se
  musí explicitně spárovat s počítací cestou.**
- **Detekce:** `tb_tdc` vstřikuje důsledky metastability (`force` na `t0p`/`t0s`) a dt-kontrola chytí okno
  s hranou navíc; negativní kontrola proti starému RTL musí selhat. Na desce `status` → `CITANI HRAN`.
- **Commit:** `38011fc`, `8bd4045`
- **Stav:** aktivní (⬜ neověřeno na křemíku)

### L-0128 — Naměřený rozptyl smí nejistotu jen zvětšit, nikdy ji nesnížit pod rozlišení přístroje

- **Datum:** 2026-10-06
- **Oblast:** `screen_main.c` `freq_uncertain_frac`, web `fmtFreqHtml`
- **Symptom:** vlastní reference → displej `10 000 000,000 0000 Hz`, všechny číslice „důvěryhodné"; uživatel
  správně neuvěřil.
- **Příčina:** počet důvěryhodných číslic se bral z naměřené σy@1s. U signálu synchronního s hodinami TDC leží
  obě krajní hrany okna stále na STEJNÉM kódu — kvantizační chyba je konstantní, ne náhodná, v rozptylu se
  neprojeví a σy vyjde ~0.
- **Oprava:** `u = max(σy, √2·tdc/gate)` na displeji i webu, commit `d63df87`.
- **Pravidlo:** **Rozptyl dat je dolní odhad nejistoty jen pro NÁHODNOU chybu; systematickou (kvantizace
  koherentního signálu, offset) nevidí. Zobrazená přesnost musí mít podlahu z rozlišení přístroje.**
- **Detekce:** měření vlastní reference musí ukázat podtržení nejvýš na ~0,01 Hz (10 MHz, 0,25 s).
- **Commit:** `d63df87`
- **Stav:** aktivní

### L-0129 — Pevná osa grafu tiše ořízne data; rozsah ber z dat (s hysterezí)

- **Datum:** 2026-10-06
- **Oblast:** `screen_main.c` `allan_metric_yrange`
- **Symptom:** Allanův graf na displeji „nekreslil nic pod 10⁻¹⁰".
- **Příčina:** ADEV měl pevný rozsah 10⁻¹⁰..10⁻⁶ (z doby 4fázového vernieru 2,5 ns); `allan_y` bod pod osou
  přilepí ke spodní hraně. Auto-range existoval, ale jen pro TDEV/MTIE.
- **Oprava:** auto-range pro všechny metriky (křivka + podlaha), hystereze proti poskakování, `d63df87`.
- **Pravidlo:** **Když se změní rozlišení přístroje, projdi všechny pevné rozsahy os a prahy, které z
  původního rozlišení vznikly; graf, který bod ořízne, ho nesmí tiše přilepit ke hraně.**
- **Detekce:** `grep -n "Y_MIN\|Y_MAX" CM7/app` — pevná mez osy musí mít zdůvodnění.
- **Commit:** `d63df87`
- **Stav:** aktivní

### L-0130 — Dva nezávislé výpočty téže statistiky se rozejdou; servíruj výsledek autority

- **Datum:** 2026-10-06
- **Oblast:** IPC v20 (`ipc_stab_t`), `httpd_min.c` `/api/stab`, SPA `drawStab`
- **Symptom:** Allan/MDEV na webu jiné než na displeji.
- **Příčina:** SPA počítala ADEV z vlastní řady surových oken (včetně oken, která firmware vyřazuje, jiná
  agregace, mezery z SSE) — druhý estimátor nad jinými daty.
- **Oprava:** firmware publikuje body z vlastní pyramidy (UiTask, seqlock), web je kreslí; vlastní výpočet
  jen záloha s varováním. Commit `bcaf00c`.
- **Pravidlo:** **Statistiku počítej na JEDNOM místě (u dat, která vidí filtry a vyřazení) a ostatní
  rozhraní ji jen zobrazují; druhý výpočet smí existovat jen jako označená záloha.**
- **Detekce:** web karta ALLAN musí ukazovat `PRISTROJ tau0 …`; `WEB …` = záloha.
- **Commit:** `bcaf00c`
- **Stav:** aktivní (⬜ neověřeno v prohlížeči)

### L-0131 — Model musí nejdřív reprodukovat naměřenou vadu; oprava ověřená jen v modelu není ověřená

- **Datum:** 2026-10-07
- **Oblast:** FPGA TDC, simulace (`sim/gowin_models.v`, `tb_tdc_inl.sv`)
- **Symptom:** obří bin (~134 kódů, 19,5 % hran) a skoky ±9,28 ns; několik iterací oprav, které v simulaci „prošly".
- **Příčina:** simulační model křemíku (ALU, průměr kroku, zlom řetězu) napsaný podle domněnky; skoky ±9,28 ns v něm nevznikly vůbec,
  takže jejich „odstranění" nelze v modelu ověřit. Obří bin model reprodukoval až po úpravě parametrů, tedy test = model ladil sám sebe.
- **Oprava:** architektura 0x0410 (bez `t0`, volné vzorky) + test `tb_tdc_inl` s očekávaným σ a maximálním skokem; zároveň zapsáno, co by
  hypotézu na křemíku vyvrátilo (`docs/audit/2026-10-07_kriticky-audit.md` A-01).
- **Pravidlo:** **Než model použiješ k ověření opravy, ukaž, že reprodukuje původní měření; a výsledek z modelu označ `[SIM]`, ne jako ověřený.**
- **Detekce:** každý nález s opravou v simulaci musí uvést, který naměřený jev model reprodukuje, a ten, který nereprodukuje.
- **Commit:** `4667d7c`
- **Stav:** aktivní

### L-0132 — SDC odkaz na neexistující objekt = chyba; staré výstupy po selhání mlčky zůstanou

- **Datum:** 2026-10-07
- **Oblast:** build FPGA (`timing.sdc`, P&R matice)
- **Symptom:** část běhů P&R v matici skončila, ale tabulka výsledků ukazovala čísla z předchozí konfigurace.
- **Příčina:** `timing.sdc` odkazoval na registr (`src_l`, `qd_f`), který po úpravě architektury neexistuje → Gowin TA2003; běh skončil bez
  nových výstupů a skript kopíroval staré soubory.
- **Oprava:** registry s prefixem `qd_` na úrovni modulu, SDC bez neexistujících objektů; skripty před během mažou staré výstupy.
- **Pravidlo:** **Skript, který sbírá výstupy dlouhého běhu, musí nejdřív smazat staré a po běhu ověřit jejich čerstvost; po přejmenování/odstranění registru projdi `timing.sdc`.**
- **Detekce:** `pnr.log` bez `ERROR`/TA2003 a časové razítko `Counter_FPGA.rpt.txt` novější než start běhu.
- **Commit:** `bbf5865`
- **Stav:** aktivní

### L-0133 — Simulace nevidí časování: široká aritmetika se v Gowin rozpadne na řetězy LUT

- **Datum:** 2026-10-07
- **Oblast:** FPGA časování (`regr_acc` v `spi_app.v`)
- **Symptom:** regresní blok prošel všemi simulacemi, ale P&R dal Fmax 53 MHz (TNS −2460), po pipeline 69 MHz, po rozdělení čítače 94 MHz.
- **Příčina:** 24bitové `n <= n + 1` a 64bitový `sy <= sy + dt` syntetizátor nemapoval na ALU přenos, ale na řetěz 11 LUT; navíc první verze
  řetězila odčítání + sčítání + porovnání v jednom taktu.
- **Oprava:** čítač 3×8 b a akumulátor 2×32 b s registrovaným přenosem, dvoufázové sčítání (`bbf5865`); výsledek 100,165 MHz, TNS 0, ale slack jen +0,017 ns.
- **Pravidlo:** **Po každé změně aritmetiky v `clk_p0_100m` spusť P&R a čti `Counter_FPGA_tr_content.html` (Fmax, TNS, kritická cesta); aritmetiku širší než ~16 bitů rovnou rozděl s registrovaným přenosem; správnost přenosu hlídej vazbou na čekání (`S_W`).**
- **Detekce:** `python` rozbor `tr_content.html`: `clk_p0_100m Setup … 0` a Fmax ≥ 100 MHz před každým flashem.
- **Commit:** `bbf5865`
- **Stav:** aktivní

### L-0134 — Přidaná funkce spolkla rezervu určenou jinému úkolu; změř cenu před přidáním

- **Datum:** 2026-10-07
- **Oblast:** rozpočet FPGA (CLS, časová rezerva)
- **Symptom:** po regresním bloku CLS 79 % (z 68 %) a slack +0,017 ns; přitom #262 (kanál C pro 1PPS, vstup smyčky GPSDO) potřebuje volnou plochu.
- **Příčina:** regresní blok se přidal jako „zisk přesnosti", aniž se předem vyčíslil přínos pro hlavní scénář (GPSDO 10 MHz = násobek 100 MHz = žádný zisk)
  a cena v ploše/časování proti plánovaným úkolům.
- **Oprava:** nic zatím; navrženo `REGR` jako parametr (výchozí 0) nebo odstranění (`docs/audit/2026-10-07_kriticky-audit.md` A-04).
- **Pravidlo:** **Před přidáním bloku do FPGA vyčísli přínos pro hlavní použití a cenu v CLS i v časové rezervě; funkci, jejíž přínos neplatí pro hlavní použití, dej za parametr s výchozí hodnotou 0.**
- **Detekce:** P&R report (`Counter_FPGA.rpt.txt`: CLS < ~78 %) a `Setup` slack > 0,2 ns před commitem.
- **Commit:** `bbf5865`
- **Stav:** aktivní (ke zvážení)

### L-0135 — "Simulace + P&R prošly" neříká nic o tom, že blok na křemíku počítá správně

- **Datum:** 2026-10-07
- **Oblast:** FPGA regresní blok (0x0411), ověřování
- **Symptom:** po nahrání 0x0411 `edge_count` o ~0,9 % vyšší, `regr` zamítá 100 % oken, `ym_B` je ve všech oknech identické.
- **Příčina:** neznámá (viz audit D-01/D-02). Jisté je, že 12 simulací, P&R i netlist kontrola prošly a přesto na desce nic z toho neplatí;
  `tb_regr_e2e` nekontroluje absolutní počet hran (jedna hrana = tisíce ppm) ani to, že se `ym` mezi okny mění.
- **Oprava:** blok vypnut parametrem (FW 0x0412) do doby, než bude reprodukován a pochopen; do `status` přidán stav `REGR:`.
- **Pravidlo:** **Nový blok, který mění číslo na displeji, se před flashem opatří kontrolou, kterou jde ověřit z `status`/`fpgaraw` bez dalšího nástroje
  (shoda `edge_count` s očekávaným, proměnlivost výstupů mezi okny), a v testbenchi se testuje i absolutní počet hran, ne jen relativní chyba.**
- **Detekce:** po každém flashi: `fpgaraw` → `edge_count` proti `f_ref·gate`, a `regr` → `pouzito > 0`.
- **Commit:** `0aa7d34`
- **Stav:** aktivní

### L-0136 — Ovládací prvek, který se nedostane do HW, nelze použít k diagnostice

- **Datum:** 2026-10-07
- **Oblast:** STM ↔ FPGA, `fpga_freq_set_window`
- **Symptom:** přepnutí hradla na 1 s / 0,1 s / 0,25 s (displej, web, SCPI) nezměnilo `gate_ns` (vždy ~250 ms).
- **Příčina:** `fpga_freq_set_window()` (SET_CONFIG 0x01) nemá volajícího; GATE mění jen SW průměrování v STM (CLAUDE.md zmiňuje jako audit #83).
- **Oprava:** žádná (jen zjištění); test hradla nelze použít k rozlišení příčiny chyby počtu hran.
- **Pravidlo:** **Než použiješ nastavení jako experimentální proměnnou, ověř na rámci z FPGA (`gate_ns`/`dt_a`), že se opravdu změnilo.**
- **Detekce:** po změně nastavení porovnat `dt_a` ve `fpgaraw` před/po.
- **Commit:** `06b01d0`
- **Stav:** aktivní

### L-0137 — Vstup pod prahem logiky: chyba, která „skáče" s každým překladem FPGA, je analogová

- **Datum:** 2026-10-08
- **Oblast:** vstup CH_A (pin 25 Tang Nano, LVCMOS33), měření HW
- **Symptom:** počet hran v okně o +0 až +257 000 (až 8,8 %) jinak podle každého P&R a podle toho, který klopný obvod vstup vzorkuje;
  s hysterezí vstupu naopak −289 až −549 hran v každém okně. Pět diagnostických bitstreamů (0x0413–0x0417) a řada hypotéz o logice.
- **Příčina:** 74HC04 (5 V) budil koax zakončený 49R9 u pinu; HC04 do 50 Ω nestačí → na pinu **0 až 1,55 V**, tedy pod VIH 2,0 V.
  Vstupní buffer se překlápěl jen šumem na vrcholu signálu → zákmity (bez hystereze) nebo vypadlé pulzy (s hysterezí).
  Který klopný obvod zákmit zachytí, záleží na zpoždění jeho vedení = na P&R.
- **Oprava:** 49R9 nahrazen 120 Ω (HC04 ~45 Ω výstup funguje jako sériové zakončení u zdroje) → na pinu obdélník 0–2,86 V.
  Výsledek: `CITANI HRAN` 0/0, mezery v SEQ 0, okno σ ≈ 196 ps (FW 0x0412, bez změny logiky).
- **Pravidlo:** **Když se chyba měření mění s každým překladem FPGA (stejná logika, jiné rozmístění), nejdřív změř sondou úrovně a hranu přímo na
  vstupním pinu proti prahům IO standardu; do té doby neměň logiku.**
- **Detekce:** sonda 10:1 na pinu: 0 V a ≥ 2,4 V (LVCMOS33), jediný průchod pásmem 0,8–2,0 V.
- **Commit:** (tento docs commit)
- **Stav:** aktivní

### L-0138 — STA bez nejistoty hodin: „splněné" časování s rezervou pod ~0,3 ns na křemíku selhává podle rozmístění

- **Datum:** 2026-10-08
- **Oblast:** FPGA časování (`timing.sdc`), regresní blok `regr_acc`
- **Symptom:** regrese na desce dávala nesmysly, které se měnily s každým buildem (segment B mrtvý; Σ(t−t0) o 1,3 % nižší;
  zlomek x̄ 0,07 místo 0/0,5), přestože simulace celého `top` i jednotkové testy v plné velikosti procházely a STA hlásila TNS 0.
- **Příčina:** `timing.sdc` neměl `set_clock_uncertainty`; STA počítala s ideálními 100 MHz. Vadné buildy měly nejtěsnější cestu
  0,017 / 0,15 / −0,19 ns, funkční ≥ 0,58 ns. Na křemíku tedy chybí ~0,3 ns (jitter Si5356 + vstupní buffer). Ladicí buildy
  (počítadla přenosů, souvislost značek, surové součty proti spočteným hodnotám) dokázaly, že logika je správná.
- **Oprava:** `set_clock_uncertainty -setup ... 0.5` pro `clk_p0_100m`; `regr_acc` přepsána na krátké cesty (registrované řízení,
  součty s registrovaným přenosem, dělič s řídicími signály ve 4 kopiích). Výsledek: regrese na desce 13× přesnější než dvoubodový
  odhad (σ okna 5,85·10⁻¹¹ proti 7,6·10⁻¹⁰), 2628/2628 oken použito.
- **Pravidlo:** **Každá hodina v SDC musí mít nejistotu odpovídající reálnému jitteru; výsledek, který se mění s rozmístěním
  při splněném časování, je časový problém, ne logický — ověř to ladicím buildem s počítadly, než začneš měnit logiku.**
- **Detekce:** `Counter_FPGA_tr_content.html` → u cesty musí být řádek `tUnc -0.500`; slack po odečtení ≥ 0.
- **Commit:** (commit FW 0x041A)
- **Stav:** aktivní

### L-0139 — Konstanta z modelu a se dvěma významy: rozlišení přístroje musí být změřené a mít jeden význam

- **Datum:** 2026-10-08
- **Oblast:** `meas_present.h` `MP_TDC_PS`, `screen_main.c` `adev_floor_base`, web `floorOf`
- **Symptom:** počet „důvěryhodných" číslic a podlaha Allanova grafu vycházely z 57 ps, přitom změřený šum značky byl ~105 ps
  (dvoubodově) a s regresí ~10 ps ekv. Navíc rozpočet nejistoty bral konstantu jako σ, podlaha jako krok kvantizace (√12×).
- **Příčina:** hodnota převzatá ze STA modelu („HYPOTÉZA do měření") nebyla po měření nikdy nahrazena; dva vzorce ji
  interpretovaly různě a nikdo to nepoznal, protože obě čísla vypadala věrohodně.
- **Oprava:** `MP_TDC_PS` = 105 ps = změřená σ; podlaha převádí na krok q = σ·√12 na displeji i na webu (commit fix(stm,web)).
- **Pravidlo:** **Každá konstanta, která popisuje přesnost přístroje, musí mít v komentáři jednotku, VÝZNAM (σ / krok / pásmo) a zdroj
  (měření s datem, nebo „hypotéza"); hypotéza se po prvním měření nahrazuje.**
- **Detekce:** `grep -rn "HYPOT" CM7/Core/Inc` — každá hypotéza má mít odkaz na měření, které ji nahradí.
- **Commit:** fix(stm,web) MP_TDC_PS
- **Stav:** aktivní

### L-0140 — Logicky souvislý řetěz není fyzicky souvislý: carry chain v FPGA je jen tak dlouhý jako řádek

- **Datum:** 2026-10-09
- **Oblast:** FPGA `tdc.v` (`tdc_chain`), `top.v` `TDC_NTAP`, `pins.cst`
- **Symptom:** žádný viditelný — TDC měřil. Teprve rozbalení bitstreamu (Apicula) a timing report z každého vzorkovacího FF
  ukázaly, že 320tapový řetěz má tapy 0–267 v jednom řádku a 268–319 v jiném (A: R26 → R20, B: R27 → R5) za skokem obecným
  vedením; a že při jiném sestavení placer dal kanál B do řádku R9, 19 řádků od vstupního pinu.
- **Příčina:** carry v GW1NR-9 vede jen vodorovně v řádku (45 CFU × 6 ALU = 270, z toho hlava a konec); delší řetěz nástroj tiše
  rozdělí a spojí vedením. Kontrola `check_tdc_netlist.py` viděla jen netlist, kde je řetěz souvislý.
- **Oprava:** `TDC_NTAP = 268` (přesně řádek), dekodér 288 s doplněním „prošla", řádky vynucené `GROUP`/`GRP_LOC` jen nad ALU,
  kontrola `sim/check_tdc_placement.py` nad textovým timing reportem (`top.v`, `tdc.v` `tdc_chain`, `pins.cst`).
- **Pravidlo:** **U struktury, jejíž funkce závisí na fyzické poloze (delay line, vzorkovače), ověřuj rozmístění po P&R
  strojově, ne netlist; a do skupiny umístění dávej jen buňky, které tvoří tu strukturu — přidané FF placer rozházel mimo slot.**
- **Detekce:** `python sim/check_tdc_placement.py impl/pnr/Counter_FPGA.tr` musí dát PASS (jinak build neodevzdávat).
- **Commit:** feat(fpga) FW 0x041D
- **Stav:** aktivní

### L-0141 — Splněné časování s nejistotou 0,5 ns nestačí: stejná logika měřila špatně, dokud se rezerva nezvedla na ~1 ns

- **Datum:** 2026-10-09
- **Oblast:** FPGA `timing.sdc` (`set_clock_uncertainty`), navazuje na L-0138
- **Symptom:** FW 0x041D (TDC v jednom řádku) při P&R splněném s nejistotou 0,5 ns (skutečná rezerva 0,545 ns) měřil na desce
  1 578 628 Hz místo 10 000 008 Hz (~1/6 hran). Stejný zdroj, stejný vstup; FW 0x041A (rezerva 0,64 ns) a sestavení
  s nejistotou 0,9 ns měřily správně.
- **Příčina:** NEUZAVŘENO. Hypotéza: stejně jako v L-0138 chybí v modelu skutečný jitter/skew hodin, a nejtěsnější cesty
  (počítání hran `rise_q -> cy_q/snap`, `regr_acc`) pak počítají chybně. Výsledek byl v 6 z 7 nahrání špatný, jednou správný
  (NE deterministický) a zmizel až s rezervou >= 0,98 ns; tedy mez není ostrá.
- **Oprava:** `set_clock_uncertainty -setup ... 0.9` v `timing.sdc`. Ověřeno 7/7 nahrání správně, ADEV(0,25 s) 7,2·10⁻¹¹.
- **Pravidlo:** **Po každé změně rozmístění/zdrojů v FPGA ověř na desce výsledek VÍCE nahráními (aspoň 4×) a neber jediné správné měření
  za důkaz; rezervu hlídej proti ~1 ns, ne proti nule.**
- **Detekce:** `Counter_FPGA_tr_content.html` → u nejhorší cesty `tUnc -0.900` a slack >= 0; na desce 4× nahrát a 4× `freq_hz`.
- **Commit:** (commit FW 0x041D)
- **Stav:** aktivní, příčina otevřená (O-bod: změřit skutečný jitter Si5356 / přivést 100 MHz z jiného zdroje)

### L-0142 — Echo konfigurace, které se obnovuje jen s měřením, nesmí být jediný zdroj pravdy o tom, co jsme poslali

- **Datum:** 2026-10-09
- **Oblast:** `fpga_freq_cfg_sync` (výběr kanálu / hradla FPGA, FW 0x041E)
- **Symptom:** test na desce: tlačítko CHAN A → B (na B není signál) → zpět A. Měření se po návratu na A nikdy neobnovilo
  (`SIGNAL_LOST`, SEQUENCE stojí), FPGA zůstala na kanálu B.
- **Příčina:** STM posílalo SET_CONFIG jen když se echo v rámci (`channel_id`, `phase_status`) lišilo od požadavku. Echo ale
  FPGA zamyká při DOKONČENÍ MĚŘENÍ; bez signálu na vybraném kanálu zůstalo staré „A“, takže po přepnutí zpět na A se echo
  a požadavek shodovaly a povel se nikdy neposlal.
- **Oprava:** STM si pamatuje, co naposledy POSLALO (`s_cfg_sent_chan/win`), posílá při změně požadavku hned a opakuje podle
  echa až po 1,5 s (to chytí reset FPGA); `fpga_freq_restart` pamatovanou hodnotu zapomene (`fpga_freq.c`).
- **Pravidlo:** **Stav, který posíláš jinému zařízení, porovnávej s tím, co jsi POSLAL, ne jen s tím, co ti zařízení hlásí
  zpět — hlášení může být zamčené/zastaralé přesně v situaci, kterou opravuješ. Test přepínače vždy dělej i v cestě „vybraný
  zdroj mlčí“ a zpět.**
- **Detekce:** na desce: `tap 3` (CHAN) dvakrát při odpojeném kanálu B → měření na A se musí obnovit do ~3 s.
- **Commit:** (commit FW 0x041E)
- **Stav:** aktivní, opraveno ⬜ neověřeno na desce (čeká na flash CM7)

### L-0143 — Bitstream bez identity: dva různé soubory hlásily stejnou verzi, takže z desky nešlo poznat, co běží

- **Datum:** 2026-10-10
- **Oblast:** FPGA build / protokol (`build.tcl`, `top.v`, `spi_app.v` bajty 12..19)
- **Symptom:** „FPGA se někdy nahraje špatně" (STATUS #282). `ab_test/FW_0x041D_radek.fs` a `FW_0x041D_unc09.fs` jsou
  RŮZNÉ bitstreamy (jiné SDC), oba hlásily `FW:0x041D`. Výsledky měření se tedy nedaly přiřadit souboru.
- **Příčina:** `FW_VERSION` je ruční konstanta. Bitstream se mění i bez její změny (SDC, volby P&R, sestavení v IDE
  místo `build.tcl`, L-0122), a nic v rámci neidentifikovalo konkrétní sestavení.
- **Oprava:** `build.tcl` při každém sestavení zapíše `src/build_id.vh` (unix čas + git hash + příznak neuložených
  změn), rámec ho nese v bajtech 12..19 (CAPS bit10, FW 0x041F), `status` → `FPGA BUILD:`. `tools/fpga_release.py`
  vydá bitstream do `ab_test/` jen se jménem z identity a řádkem v `ab_test/MANIFEST.md` (md5, rezervy, CLS).
- **Pravidlo:** **Každý artefakt, který se nahrává do HW, musí nést identitu generovanou při sestavení (čas + verze
  zdrojů) a zařízení ji musí umět ohlásit; ručně udržované číslo verze identitou není.**
- **Detekce:** `status` → `FPGA BUILD:` se shoduje s řádkem v `ab_test/MANIFEST.md`; `python tools/fpga_release.py`
  kontrola „identita odpovídá .fs".
- **Commit:** (commit FW 0x041F)
- **Stav:** aktivní, ⬜ neověřeno na desce

### L-0144 — Měřicí přístroj bez samokontroly: okno se špatným počtem hran vypadalo zdravě

- **Datum:** 2026-10-10
- **Oblast:** FPGA `win_recip` (`spi_app.v`), STM `fpga_freq_poll` (`fpga_freq.c`)
- **Symptom:** L-0141: 1 578 628 Hz místo 10 000 008 Hz v 6 ze 7 nahrání; L-0137: počet hran o desítky tisíc jinak.
  Rámce měly platné CRC, VALID, navazující SEQUENCE — špatné číslo šlo na displej, do statistiky i datalogu.
- **Příčina:** měřená cesta (vzorek řetězu → detekce hrany → čítač s registrovaným přenosem) neměla žádnou nezávislou
  kontrolu a STM neověřovalo, jestli délka okna odpovídá hradlu. Vada kdekoli v té cestě byla neviditelná.
- **Oprava:** (1) FPGA: kontrolní počítání hran vlastním synchronizátorem téhož pinu, rozdíl proti `snap` v rámci
  (bajty 97/98); (2) STM: okno se zamítne, když |rozdíl| > 1, když délka okna nesedí s hradlem (`fpga_freq_window_check`)
  nebo když hlídač 100 MHz hlásí výpadek; `status` → `FPGA SAMOKONTROLA:`. Zamítnuté okno se nezobrazí ani nezapočte.
- **Pravidlo:** **Každou hodnotu, kterou přístroj vydává jako měření, musí potvrdit aspoň jedna nezávislá kontrola
  (druhá cesta, fyzikální mez, konzistence s nastavením) a výsledek kontroly musí být vidět; bez ní je vadné měření
  k nerozeznání od správného.**
- **Detekce:** `sim/run.ps1 topselfchk` (5 ukradených hran → rozdíl ≥ 4); selftest #1 (`fpga_freq_window_check`:
  okno 6,3× delší neprojde); na desce `status` → `FPGA SAMOKONTROLA: zamitnuto oken: citani 0, ...`.
- **Commit:** (commit FW 0x041F)
- **Stav:** aktivní, ⬜ neověřeno na desce

### L-0145 — Zamítnuté měření prosáklo dalším rámcem se stejnou SEQUENCE

- **Datum:** 2026-10-10
- **Oblast:** STM `fpga_freq_poll` (`fpga_freq.c`, latch `s_last`)
- **Symptom:** nalezeno čtením kódu, na desce nepozorováno: okno odmítnuté jako miscount se do `s_last` nezapsalo jen
  v prvním rámci; o ~25 ms později přišel tentýž výsledek znovu (FPGA ho posílá až do dalšího měření, jen bez FRESH),
  `is_new` byl 0, takže `mc` = 0 a okno se latchlo. Viděl ho web (IPC), SCPI i okna UI.
- **Příčina:** podmínka latchu závisela na výsledku kontroly PRÁVĚ TOHOTO pollu, ne na tom, ke kterému měření rámec
  patří. Rámec bez FRESH nese stále totéž měření.
- **Oprava:** pamatuje se SEQUENCE odmítnutého měření (`s_rej_seq`) a rámce se stejnou SEQUENCE se nelatchují;
  diagnostika rámce (verze, identita, hodiny) se drží zvlášť v `s_frame` (`fpga_freq_get_frame`).
- **Pravidlo:** **Rozhodnutí o platnosti dat váž na identitu dat (pořadové číslo), ne na okamžik, kdy přišla —
  zdroj, který stejná data opakuje, jinak obejde každou kontrolu, která běží jen „při novém".**
- **Detekce:** grep `fpga_freq_get_last` u konzumentů měření; review: každý latch po kontrole musí mít podmínku
  na SEQUENCE.
- **Commit:** (commit fix STM samokontrola)
- **Stav:** aktivní, ⬜ neověřeno na desce

### L-0146 — FPGA bez resetu nepřežije výpadek hodin: stav i kalibrace z rozbitých hodin zůstanou až do nového nahrání

- **Datum:** 2026-10-10
- **Oblast:** FPGA `top.v` (hlídač 100 MHz, `boot_done`), `tdc.v` (`ring_osc` LFSR)
- **Symptom:** HYPOTÉZA pro část případů „někdy se nahraje špatně": 100 MHz dělá Si5356, který STM po startu
  přenastaví (OEB off → soft reset → on). Při resetu STM za běhu FPGA nebo při zapnutí (FPGA naběhne dřív než STM
  nastaví Si5356) dostala logika TDC a oken rozbité / chybějící hodiny a kalibrace po 168 ms mohla proběhnout na jiném
  kmitočtu. FPGA nemá reset, takže výsledek přetrval do dalšího nahrání.
- **Příčina:** žádné hlídání referenční hodiny; boot kalibrace odpočítávala takty té hodiny bez ohledu na její stav;
  LFSR děliče ring oscilátoru (taktovaný kruhem, runt pulzy při start/stop) neměl únik ze stavu 0.
- **Oprava:** hlídač v doméně 10 MHz (OCXO, nezávislá na STM) počítá 100 MHz v oknech 102 µs; mimo pásmo → měření
  stojí (`hold`), odpočet boot kalibrace se nuluje a po 168 ms stabilních hodin proběhne nová kalibrace; počet
  výpadků v rámci (bajty 64/65); LFSR se ze stavu 0 vrátí na seed.
- **Pravidlo:** **Logika bez resetu, která běží z hodin od jiného zařízení, musí ty hodiny hlídat hodinami, na
  kterých nezávisí, a po výpadku se sama vrátit do čistého stavu včetně kalibrací.**
- **Detekce:** `sim/run.ps1 topselfchk` (zastavení 100 MHz na 20 µs → výpadek započten, měření drženo, rekalibrace,
  pak správně); na desce: reset STM (NRST) za běhu → `status` `FPGA HODINY 100M: OK, vypadku 1` a měření se obnoví.
- **Commit:** (commit FW 0x041F)
- **Stav:** aktivní, ⬜ neověřeno na desce

### L-0147 — Přepínač zdroje hodin nesmí ležet za hodinami, které přepíná

- **Datum:** 2026-10-10
- **Oblast:** FPGA `ref_guard` (`spi_app.v`), pin `Ref_Ctrl`, přepínač reference `U19`
- **Symptom:** návrh (ještě bez kódu): kdyby FPGA ovládala přepnutí OCXO → externí reference z logiky v doméně 10/100 MHz,
  vybraná externí reference, která zmizí (vytažený kabel), by zastavila hodiny celého FPGA (Si5356 CLKIN) a tím i logiku,
  která měla přepnout zpět — zařízení by zůstalo viset do power-cyklu.
- **Příčina:** obvod, který rozhoduje o zdroji hodin, běžel na těch hodinách.
- **Oprava:** `ref_guard` běží z vnitřního oscilátoru FPGA (OSC), nezávisle na vnějších hodinách: externí pustí jen při nepřetržité
  přítomnosti hran, při jejím zmizení se sám vrátí na OCXO; po zapnutí je výchozí OCXO (hardwarový pulldown + `sel = 0`).
- **Pravidlo:** **Kdo vybírá zdroj hodin, musí mít vlastní nezávislý zdroj hodin a bezpečný výchozí stav; ovládání, které by po výpadku
  vybraného zdroje už nemělo čím běžet, je past.**
- **Detekce:** `sim/run.ps1 refguard` (vypadnutí vybrané reference → zpět na OCXO do ~15 µs); `status` → `FPGA REFERENCE:`.
- **Commit:** (commit FW 0x0420)
- **Stav:** aktivní, ⬜ neověřeno na desce

### L-0148 — Změna jednoho výstupu Si5356 nesmí jít přes soft reset

- **Datum:** 2026-10-10
- **Oblast:** `si5356.c` (generátor CLK2/3, CLK4/5, CLK6/7), reference FPGA na CLK0/1
- **Symptom:** (návrh, před kódem) nejkratší cesta k „volné frekvenci“ je zapsat nové MultiSynth registry a udělat
  soft reset jako při inicializaci. Soft reset (reg 246) ale **vypne všechny výstupy**, tedy i CLK0/1 = 125 MHz,
  časovou základnu FPGA — jedno přeladění generátoru by FPGA shodilo (a spustilo rekalibraci TDC).
- **Příčina:** výstupy jednoho čipu sdílejí reset; „nezávislé“ jsou jen děliče.
- **Oprava:** `si5356_gen_apply` mění jednu dvojici bez resetu: `OEB_x` (reg 230) → R + MS (10 registrů) → uvolnit `OEB_x`.
  Zapisuje jen SensorsTask (vlastník I2C1) na žádost `si5356_gen_request`. Výpočet P1/P2/P3 je přesný racionální
  (P2/P3 = zbytek/fms zkrácený), pro 125/50/150/200 MHz vychází bit za bitem export z CBPro.
- **Pravidlo:** **Co sdílí zdroj hodin s kritickým odběratelem, se nikdy nemění operací, která zasahuje celý čip (reset, power-down);
  mění se jen vlastní větev a ověří se, že referenční výstup po zásahu běží.**
- **Detekce:** UART `gen` (zapsáno vs. přečteno z čipu → `SHODA`/`ROZDIL`) a `status` → `FPGA HODINY`: výpadek 100/125 MHz
  se po přeladění generátoru nesmí zvýšit.
- **Commit:** (doplnit)
- **Stav:** aktivní, ⬜ neověřeno na desce

### L-0149 — Jednotku času a takt hodin nese ramec, ne konstanta v hostu

- **Datum:** 2026-10-10
- **Oblast:** `fpga_freq.c` (`dt_units_to_ps`), `spi_app.v` (abs 99), `timing.sdc`
- **Symptom:** (předem, při změně hodin FPGA 100 → 125 MHz) rámec nese Δt v jednotkách T_clk/16384 a STM je přepočítával konstantou `625/1024` (= 100 MHz). Kdyby se bitstream a firmware
  rozešly (jedno nahráno, druhé ne), čítač by tiše ukazoval kmitočet o 25 % vedle — a ten rozdíl není z ničeho vidět, `status` je zdravý.
- **Oprava:** FPGA nese kmitočet hodin v rámci (abs 99, CAPS bit12); host z něj počítá `ps = jednotky·15625/(256·clk_mhz)`; chybějící bit = 100 MHz (starý bitstream), bajt 0 se bere jako 100.
- **Pravidlo:** **Veličina, kterou si dvě strany vykládají stejně jen shodou konstant, patří do protokolu — ať ji strana, která ji zná, hlásí, místo aby ji druhá předpokládala.**
- **Detekce:** `selftest` (blok „FW 0x0421“: 0,25 s při 125 MHz = 2,5·10¹¹ ps; bajt 0 → 100 MHz); `status` → `FPGA HODINY <n>M`.
- **Commit:** (doplnit)
- **Stav:** aktivní, ⬜ neověřeno na desce

### L-0150 — Za výstupem BRAM nesmí být logika; „keep" neudrží kopii registru

- **Datum:** 2026-10-10
- **Oblast:** `tdc.v` (tabulka kalibrace, histogram), `spi_app.v` (kopie řídicích registrů)
- **Symptom:** při 125 MHz padaly cesty `BRAM DO → inkrement/negace` o 0,8–3,9 ns (tCO bloku 3,5 ns + aritmetika) a cesty z registrů s velkým fanoutem; syntéza navíc `lut_q`+`lut_q2` vtáhla do BRAM
  (adresní + výstupní registr), takže „FF za BRAM" nebyl FF. Kopie `rise_k` označená `(* keep *)` byla slita s `rise_q` (identický vstup).
- **Oprava:** hned za BRAM jen samostatný FF (`lut_q3`, `hist_rd2`), aritmetika předpočítaná při kalibraci (tabulka nese `{amt, neg}`); kopie registrů musí být **funkčně odlišné** (`rise_k <= rise_a & ~hold`).
- **Pravidlo:** **Za BRAM patří jen FF; kopie registru, kterou syntéza nemá důvod nechat, zmizí i s `keep` — udělej ji logicky jinou. A výsledek P&R jednoho sestavení nic nedokazuje: sestavení závisí na `BLD_TIME`, posuzuj víc sestavení.**
- **Detekce:** `python tools/fpga_release.py --build` (setup/hold/Fmax/CLS/rozmístění) opakovaně; `sim/run.ps1` (celý soubor).
- **Commit:** (doplnit)
- **Stav:** aktivní

### L-0151 — Vystup okna a nove reference nesmi menit stejnou hranou hodin; STA to nepozna

- **Datum:** 2026-10-10
- **Oblast:** `spi_app.v` (`win_recip`: `r_dt`, `r_cs`, `ref_ts`, `code_cur`), diagnostika `/api/fpga` (IPC v22)
- **Symptom:** po nahrani FPGA (125 MHz, FW 0x0421) nemerel kanal A: `error_flags` bit0 (dt == 0), `gate_ps = 0`, pocet hran spravne,
  `code_end == code_st` v kazdem okne; kanal B ve stejnem bitstreamu v poradku. Cast sestaveni (stejne zdroje, jiny BLD_TIME) fungovala, cast ne.
  Statická analyza casovani byla u vsech cista (rezerva setup +0,03 az +0,06 ns, hold +0,45 az +0,54 ns).
- **Pricina (nejpravdepodobnejsi, ⬜ neprokazana primym merenim):** `r_dt <= ts_ps - ref_ts` a `ref_ts <= ts_ps` (a stejne `r_cs <= code_cur` / `code_cur <= code_i`) se
  provadely na JEDNE hrane hodin (`tsc_q`). Na krzemiku vystup videl uz nove hodnoty (dt = 0, code_end == code_st). Zavisi na rozmisteni, proto jedno sestaveni ano a druhe ne.
- **Oprava:** reference se aktualizuji az o takt pozdeji (`tsc_q2`), vystup se zachyti drive. Simulace link/regracc/regrfull/txequiv PASS.
- **Pravidlo:** **Vystupni registr a registr, ze ktereho cte, nemenit na stejne hrane (ani kdyz STA hold projde). A vysledek sestaveni, ktere projde STA, jeste nic nedokazuje:
  po kazde zmene FPGA nahraj bitstream NEKOLIKRAT a over OBA kanaly (`/api/fpga`: `error_flags`, `gate_ps`, `tdc_status` = 0x3).**
- **Detekce:** `GET /api/fpga` (pole `w[]` dekoduje `tools/fpga_diag.py`): `error_flags`, `gate_ps`, `code_end/st`, `tdc_status`; `status` -> `FPGA SAMOKONTROLA`.
- **Stav:** aktivni, ⬜ ověřeno jen na jednom kusu desky; regresni blok (`rg_ok`) ve vydanem sestaveni neověřen

### L-0152 — Regresni blok se do ramce cte ZIVE, ne zamcený při uzavreni okna

- **Datum:** 2026-10-10
- **Oblast:** `spi_app.v` (TX mux bajty 68..96 cte `rg_n_*`, `rg_ym_*`, `rg_ok_*` primo z `regr_acc`)
- **Symptom:** `rg_ok` v ramci je podle sestaveni 3 / 2 / 0 a `xm` nesedi k `ym` (offset desítky ms); dt, periody a kody jsou naproti tomu zamcene pri uzavreni okna.
- **Pricina:** hodnoty regrese se berou v okamziku skladani ramce (zivé registry), kdyz uz bezi dalsi okno (`beg_a` maze `ok_a`).
- **Pravidlo:** vsechno, co patri k jednomu oknu, se zamyka ve stejnem okamziku (pri `res_valid`), ne pri skladani ramce.
- **Stav:** ⬜ OTEVRENE (nespraveno); STM kvuli `rg_ok != 3` regresi nepouzije, mereni je jako dvoubodove.

**Dodatek 2026-10-11 (vyvraceni hypotezy "zive cteni je pricina"):** zamek pri uzavreni okna (FW 0x0423) nesel sestavit (CLS 85-86 %, setup -1,6 az -3,6 ns,
8 pokusu) a hlavne **neresi pricinu**. Merenim na desce (FW 0x0422, G = 0,25 s): `ym` ma jen tri hodnoty 17,176 / 31,250 / 45,324 ms, tj. chyba je presne
**+-2^53 v souctu sum(t - t0)** (14,074 ms * 312 500 / 0,488 ps = 9,007e15 = 2^53); sum je pri plnem hradle prave kolem 2^53 (bit 21 `sy_hi`). `xm` se opakuje z male mnoziny.
RTL aritmetika je **spravna i v plnem meritku**: `sim/tb_regr_big.sv` (312 500 znacek/segment, 125 MHz) = PASS (puvodni sim jedel jen 25 000 hran a mez 2^53 nikdy nepotkal).
Zbyva casovani/zapojeni na krzemiku (multicycle vyjimky pridane pro 125 MHz v timing.sdc, nebo vstupni znacky), ne logika. Neresit zamkem; dalsi krok = bitstream
s ladicim citacem nad `sy_hi` (ne rozsirovat rozmisteni). Pozn.: A-B (stejny signal na A i B) ma stred 0,011 ns, sigma jadra 0,17 ns, ale ~17 % oken ma
chybu -6..+5 ns (plynule, pilovite) = zona chyby TDC pri urcite fazi hrany (INL/kalibracni tabulka).

**Dodatek 2026-10-11 vecer (UART analyza, FW 0x0422 @ 125 MHz, 10 MHz z externiho GPSDO na A i B):** `sdramlog dump` 200 souvislych oken:
**ADEV(0,25 s) = 1,4e-8** (na 100 MHz dvoubodove 7,6e-10, s regresi 5,4e-11). Jen 105/200 oken je v +-0,5 ns; chyby delky okna jsou KVANTOVANE kolem
**+4 ns (= T/2 pri 125 MHz)** a **+8..9,5 / -7,5 ns (= T)**, vzor s rozestupy 4-4-2 okna (zazneje signal x hodiny) = deterministicka chyba casove znacky
podle faze hrany, NE sum a NE modulace (jednostranne). Je spolecna kanalum A i B (shodna struktura TDC); kanalove rozdily (19 % oken) rostou
pilovite s fazi. Spike-reject (5 ns) chyti jen chyby ~T, chyby T/2 projdou. Kalibrace TDC je pritom v poradku (`tdc hist`: ~130 kodu, prumer 62 ps,
max bin 257 ps A / 182 ps B, za koncem 0). Aritmetika tabulky {amt, neg} (0x0421) prosla ctenim bez nalezu. **Dalsi krok = bisect 100 MHz:**
bitstream 0x0420 (ab_test/) + Si5356 CLK0/1 zpet na 100 MHz (REGMAP z HEAD). Vedlejsi: UART `tdc` prepocitava bin na ps s periodou 10 ns
(vypise 321 ps misto 257 ps).

### L-0153 — Kalibrace TDC nahodne selhava a mereni pak bezi s nominalni tabulkou BEZ priznaku

- **Datum:** 2026-10-11
- **Oblast:** `tdc.v` (`cal_fail`), `top.v` (retry), `freertos_task_fpga.c`
- **Symptom:** ten samy bitstream mel pri nekterych nahranich `tdc_status` bit2 (`cal_fail`, vyprsel casovac sberu) a okna A/B se lisila o desitky ns
  (`50000061101` ps misto `50000008xxx`), `error_flags` = 0. Nahodne, zhruba kazde 4. nahrani.
- **Pricina:** neurcena (podezreni: kruhovy oscilator se nekdy nerozbehne / udalosti kalibrace nechodi). `cal_fail` nastavi `cal_valid = 1` s nominalni tabulkou,
  takze mereni nestoji a nic neupozorni.
- **Oprava (mitigace):** FPGA po selhani sama spusti kalibraci znovu (za 2^20 taktu, nejvyse 15x; pocet pokusu v `tdc_status[7:6]`); STM pri `FPGA_TDC_CAL_FAIL`
  pozada o novou kalibraci (nejvyse 1x za 15 s).
- **Pravidlo:** selhani kalibrace NESMI zustat tichym degradovanym rezimem. Po kazdem nahrani FPGA over `tdc_status == 0x03` (A, B zkalibrovane, bez FAIL).
- **Stav:** ⬜ pricina otevrena, mitigace na desce 8 nahrani bez selhani (pocet opakovani 0) -- opakovani samo nebylo vyvolano.

### L-0154 — Zmena kmitoctu hodin FPGA prosla STA i simulaci, a pritom rozbila casove znacky (T/2, T podle faze)

- **Datum:** 2026-10-11
- **Oblast:** hodiny FPGA / TDC (`top.v` `CLK_MHZ`, `timing.sdc`, `si5356.c` `REGMAP`)
- **Symptom:** po prechodu 100 -> 125 MHz (FW 0x0421/0x0422) „meri mene stabilne nez driv": ADEV(0,25 s) 1,4e-8 (na 100 MHz 7,6e-10 / s regresi 5,4e-11),
  delka okna chybna o +4 ns (= T/2) a +-8 ns (= T) v ~polovine oken podle faze hrany vuci hodinam (vzor 4-4-2 okna), spolecne kanalum A i B;
  regrese vzdy zamitnuta. Spike-reject (5 ns) chytil jen chyby ~T. Kalibrace TDC (`tdc hist`) pritom vypadala zdrave.
- **Pricina:** neurcena (bisect probiha). Vylouceno: aritmetika regrese (`sim/tb_regr_big.sv` v plnem meritku PASS), kalibracni tabulka
  (max bin 257 ps), skew A-B (0,01 ns). Kandidati: casovani na krzemiku pri 8 ns mimo STA (multicycle predpoklady), prirazeni hrany k taktu.
- **Oprava:** FW 0x0423 vraci hodiny na 100 MHz (Si5356 MS A = 22, `top.v` `CLK_MHZ = 100`, `timing.sdc` 10 ns), RTL beze zmeny = bisect.
- **Pravidlo:** zmenu kmitoctu hodin (nebo cokoli, co meni casovani TDC) prohlas za hotovou az po MERENI ADEV a rozlozeni chyby delky okna
  na desce (`sdramlog dump` + rozdeleni odchylek), ne po STA, simulaci a „A i B meri".
- **Detekce:** `sdramlog dump 200` -> podil oken v +-0,5 ns od medianu (zdrave > 95 %) a ADEV(0,25 s); `regr` -> pouzito > 0.
- **Commit:** (neni, neulozeno)
- **Stav:** aktivni; ⬜ vysledek bisectu na desce

### L-0155 — Stupnice varování poskládaná ze dvou veličin musí být monotónní, a „zbývající položky" nadhodnocují rezervu

- **Datum:** 2026-10-14
- **Oblast:** FPGA/TDC diagnostika (`fpga_tdc_rez_eval`, `fpga_freq.c`)
- **Symptom:** včasné varování rezervy TDC řetězu (OK ≥ 15 tapů, POZOR 8–14, KRITICKÉ < 8) dalo v simulaci při zkracování řetězu nejdřív KRITICKÉ (38 ps/tap, rezerva 7), a o 1 ps později POZOR (37 ps/tap, 3,9 % událostí za koncem).
- **Příčina:** dvě různé veličiny na jedné stupnici. Rezerva `taps − last` se počítá jen dokud nic není za koncem; jakmile hrany za konec dojdou, je `last` kód „za koncem" a rezerva nejde spočítat, takže jsem přešel na podíl událostí s vlastními prahy (KRIT až od 1/16) a ty vyšly mírněji než poslední hodnota první stupnice. Navíc `taps − last` nadhodnocuje rezervu: užitečný řetěz je v modelu ~264 tapů (`maxtap` se ve všech bězích s přetečením zastaví na 264), ne 268.
- **Oprava:** jakákoli událost za koncem = KRITICKÉ (`fpga_freq.c`, `fpga_tdc_rez_eval`); selftest má vektory pro oba případy; hranice 15/8 potvrzeny simulací (rezerva 7 = 3–4 tapy před přetečením).
- **Pravidlo:** když se stupnice varování skládá ze dvou veličin, ověř ji na modelu, který mez překročí, a zkontroluj, že úroveň s horšícím se stavem nikdy neklesne; vlastní mez „zbývá N položek" přepočítej na veličinu, která se opravdu mění (zde rychlost tapu: ≈ 2,7 tapu na 1 %).
- **Detekce:** `fpga_freq_select_selftest` (vektory rezervy), sweep `tb_tdc_inl` podle `docs/audit/2026-10-10_volba-kmitoctu-hodin.md` kap. 10.6.
- **Commit:** `a30b0b4` (původní verze `ef07ca0`)
- **Stav:** aktivni; ⬜ neověřeno na desce

### L-0156 — Čip, který se zamyká na vnější signál, nestačí inicializovat jednou při bootu: při studeném startu ještě signál není

- **Datum:** 2026-10-15
- **Oblast:** periferie / hodiny (`si5356.c`, `freertos_task_sensors.c`), STATUS #295
- **Symptom:** po studeném zapnutí (2 ze 2 pokusů, M3 a M3b) přístroj neměřil vůbec: Si5356 hlásil trvale `LOS_CLKIN + PLL_LOL` (status 0x1C), `seq_meas = 0`, a sám se nezamkl ani za 19 min, i když OCXO už dávalo 10 MHz (FPGA z něj běžela). Teplý start byl v pořádku. Ruční UART `si5356` (nový init) zamkl okamžitě.
- **Příčina:** `si5356_init` běží jednou v `main()` před schedulerem, tedy dřív, než studený OCXO dá platný vstup. Po soft resetu bez vstupu se PLL Si5356 sama nezamkne ani poté, co vstup přijde (pozorováno, mechanismus v čipu neověřen). Firmware stav čipu četl a hlásil (`REFERENCE:`, SYS pilulka), ale nic s ním nedělal. U teplého startu je OCXO platné hned, takže vada se při běžném vývoji (flash + reset) nikdy neukázala.
- **Oprava:** `si5356_lock_watch` (`freertos_task_sensors.c`): trvá-li FAULT 10 s, SensorsTask (vlastník I2C1) init zopakuje, další pokusy s rozestupem 2× delším až do 300 s; zámek rozestup vrátí. Nastavení generátoru CLK2–7 se před initem uloží a znovu vyžádá; před prvním zámkem se odloží armování sticky registru (výpadek do prvního zámku je součást náběhu). Init bez `printf` (`si5356_init_quiet`), protože SensorsTask má 1,5 kB zásobníku. Výsledek: `status` → `REFERENCE: auto re-init Si5356 …`, errlog druh `ERRLOG_K_REFINIT` (vlastní, kvůli prodlevě per druh, L-0085), web `/api/state` → `si_reinit`.
- **Pravidlo:** Konfiguraci čipu, jehož funkce závisí na vnějším signálu (PLL, synchronizace, přijímač), nepovažuj za hotovou po jednom zápisu při bootu: když stav čipu trvale hlásí ztrátu, konfiguraci zopakuj s omezeným tempem, a chování ověř studeným startem, ne jen resetem.
- **Detekce:** studený start (deska vychladlá ≥ 1 h) a `status` → `REFERENCE:` + `/api/state` `si5356_status`/`si_reinit`. Pro jiné čipy: u každého jednorázového zápisu konfigurace (`*_init` v `main.c`/na začátku tasku) se zeptat, co se stane, když v tu chvíli vstup ještě není. Stejná třída je už ošetřená u GPS (TP5 se posílá znovu 1×/min, F-0219); AD5693R zatím neosazen.
- **Commit:** (nekomitováno v době zápisu)
- **Stav:** aktivní; ⬜ neověřeno na desce (studený start)

### L-0157 — Práh „ustáleno" převzatý z jiného účelu: měřil pouzdro, ne pec, a mrtvá pec by hlášku vypnula

- **Datum:** 2026-10-15
- **Oblast:** UI / metrologická důvěryhodnost (`warmup_ready`, `app_gpsdo.c`), STATUS #299
- **Symptom:** hláška „TEPLOTA NEUSTALENA" po studeném startu svítila ~18 min, ač frekvence OCXO byla v ±10 ppb od ~160 s; zároveň by zmizela po 300 s i u pece, která netopí (stabilní teplota ⇒ sklon 0).
- **Příčina:** podmínka `|dT/dt| < 0,08 °C/min` na plášti OCXO (TMP117 0x49) vznikla z prahu kalibračního průvodce vstupního modulu (zadání UI) a měří dohřívání pouzdra a desky, ne regulaci pece. Chyběla kontrola, že teplota vůbec platí a že pec topí (ΔT plášť−deska), takže „klid" mohl znamenat „studená a mrtvá".
- **Oprava:** `warmup_ready()` — práh sklonu 0,3 °C/min (≤ 1 ppb / 10 min při 3× větší teplotní vazbě než datasheet), povinná platná T49 a ΔT ≥ 10 °C (je-li T48 k dispozici), minimum 900 s po zapnutí napájení (datasheet ±0,01 ppm za 15 min; naměřeno 9,8 ppb v 954 s) a 300 s po resetu při zapnutém napájení (POR/BOR z `g_reset_rsr`). Přehrání záznamů: studený start hotovo v 904 s (dřív 1092 s), teplý 304 s beze změny, mrtvá pec nikdy (dřív po 300 s).
- **Pravidlo:** Prahový údaj převzatý ze zadání jiné funkce ověř na naměřených datech té veličiny, na které ti záleží (zde frekvence), a kritérium „ustáleno" vždy doplň o důkaz, že sledovaný děj vůbec probíhá (pec topí, senzor platí) — klid může znamenat i poruchu.
- **Detekce:** přehrání záznamů `docs/audit/data/2026-10-15_warmup_nove.py` (starý × nový práh, mrtvá pec); na desce: `/api/state` `warmup` a hláška na hlavní obrazovce po studeném startu (očekáváno do ~15 min) a po resetu STM při horkém OCXO (~5 min).
- **Commit:** (nekomitováno v době zápisu)
- **Stav:** aktivní; ⬜ neověřeno na desce (studený start, M4)

### L-0158 — Softwarový reset příznaku nezruší hardwarový přenos: USB TX watchdog rozjel IN FIFO a konzole zůstala o jeden přenos pozadu

- **Datum:** 2026-10-15
- **Oblast:** USB CDC konzole (`usb_console.c`, `usbd_cdc_if.c`), STATUS #298
- **Symptom:** konzole „mlčí" nebo vypisuje zpožděně: odpověď na příkaz dorazí až s dalším příkazem (`ing|pong|p` = zbytek minulé odpovědi + první znak nové ozvěny), ocas `status` uvízl > 60 s. Displej, ETH i měření jedou. Dřív konzole fungovala.
- **Příčina:** když host nemá port otevřený, nikdo nesbírá IN koncový bod a přenos se nedokončí. TX watchdog (2026-10-04) po 250 ms zavolal `CDC_ForceTxIdle` — vynuloval jen softwarový příznak `TxState`. Hardwarový přenos zůstal armovaný a jeho data v TX FIFO; další `CDC_Transmit_FS` zapsal nová data ZA ně a jádro vysílá z hlavy FIFO → konstantní zpoždění o jeden přenos, trvalé do resetu. Zjištěno čtením stavu: ring prázdný (`s_head == s_tail`, `s_pending == 0`), `DIEPCTL1.EPENA = 0`, ale `DTXFSTS1` ≠ 0x80; počítadlo watchdogu 1211 za 9 min, 91 za 25 s bez hosta; po resetu s hostem od začátku 0 a konzole správná.
- **Oprava:** `usb_console.c`: vysílá se jen při přítomném hostu (linka CONFIGURED a DTR z `SET_CONTROL_LINE_STATE`, nebo data od hosta mladší 30 s), při zavřeném portu se jen plní ring a při přetečení se zahazuje NEJSTARŠÍ; každý přechod (otevření, zavření, ztráta linky, zlomený zásek) končí `CDC_AbortTx` (`usbd_cdc_if.c`): zákaz doplňování FIFO z IRQ, `HAL_PCD_EP_Abort` + `HAL_PCD_EP_Flush`, vynulování příznaků a `TxState`. Aborty se dělají jen z kontextu úlohy. `status`: řádek `KONZOLE: host …`. Nástroje v `tools/` nastavují `DtrEnable`.
- **Pravidlo:** Přepsání softwarového stavu automatu (příznak „vysílám") bez zrušení stavu v periferii ho jen rozjede s hardwarem; zásek řeš zrušením přenosu a vyprázdněním fronty periferie, a nezkoušej vysílat, dokud druhá strana neposlouchá.
- **Detekce:** `python tools/cdc_lag_test.py COM10 4` (po resetu a po 30 s bez hosta; každá odpověď musí dorazit celá do ~100 ms); SWD: `DIEPCTL1.EPENA`, `DTXFSTS1` = 0x80 při klidu, `status` bez řádku „TX watchdog zlomil zasek".
- **Commit:** (doplnit)
- **Stav:** aktivní; ✅ ověřeno po resetu i po power-cyklu (2026-10-15, uživatelův terminál + `cdc_lag_test.py`: `ping` -> `pong` celý do ~60 ms); ⬜ nepozorován dlouhodobý běh

### L-0159 — „Neplatný" slot flash nebyl „volný": poškozený záznam se bral za hlavu logu a každý boot přepsal minulý

- **Datum:** 2026-10-15
- **Oblast:** trvalý záznamník chyb (`errlog`, W25Q NOR), STATUS #296
- **Symptom:** `/api/errlog?from=0..3` vracel `n:0`, `errlog dump` vypsal jen hlavičku, `total` se nehýbal (4873 → 4871) ani po novém bootu a nejnovější záznamy (vč. nového druhu `ERRLOG_K_TDC`) se nikdy nezobrazily. Pod tím šlo ~90 % logu o jedinou opakující se událost „ztráta linku" každou minutu.
- **Příčina:** `errlog_init` hledal hlavu jako PRVNÍ slot, kde `el_unpack` selže (seq = 0xFFFFFFFF nebo CRC). Jenže selhat může i poškozený, nedopsaný slot (výpadek napájení při zápisu), který NENÍ smazaný. NOR umí jen 1 → 0, takže zápis do něj dá AND se starým obsahem, tedy zase neplatný záznam. Boot N tak zapsal čtyři záznamy na tytéž sloty jako boot N−1 (AND = oba zničeny), `maxseq` z neplatných se nezvětšil, a proto `total` stál. Na desce bylo 45 takových slotů v sektoru hlavy.
- **Oprava:** slot má tři stavy (smazaný 0xFF / platný / poškozený); hlava je první SMAZANÝ slot, poškozený se přeskočí; `errlog_tick` před zápisem ověří, že cíl je smazaný, jinak ho přeskočí (a počítá); `errlog_count` počítá sloty, ne `seq`; `errlog_read_batch` vrací za poškozený slot placeholder `kind = 0` („POSK") místo konce čtení; `errlog dump` vypisuje diagnostiku (slot hlavy, poškozené při startu, přeskočené zápisem). Navíc narůstá prodleva opakující se události (60 s → 30 min, zpět po klidu), aby jedna nevytlačovala vzácné.
- **Pravidlo:** U paměti, kterou lze jen mazat po blocích (NOR), rozlišuj „smazané" (všechny bity 1) od „neplatné"; volné místo se pozná podle obsahu 0xFF, ne podle selhání kontrolního součtu, a do nesmazaného místa se nezapisuje.
- **Detekce:** `errlog dump` — hlavička `hlava: … poškozených slotů …`; po kterémkoli resetu musí `zaznamu` růst a nejnovější `#seq` odpovídat novému bootu.
- **Commit:** (doplnit)
- **Stav:** aktivní; ✅ ověřeno na desce (3 resety po sobě: záznamy přibývají, poškozených při startu stále 45); ⬜ nepozorován zápis `ERRLOG_K_TDC` / `REFRST` (čeká na M4)

### L-0160 — Emulátor, který zpětně ovlivňuje model, nesmí zapisovat do SKUTEČNÉHO akčního členu

- **Datum:** 2026-10-16
- **Oblast:** emulace FPGA (`fpgasim`), smyčka GPSDO, DAC ladění OCXO (STATUS #305)
- **Symptom:** (zachyceno při revizi, než se to dostalo na desku) emulátor 1PPS ladí „OCXO" podle kódu DAC a smyčka GPSDO tentýž kód zapisuje do AD5693R. S `fpgasim on` by smyčka řídila skutečný DAC podle výmyslu modelu a fyzické OCXO by se rozladilo (reference celého přístroje), aniž by se na jeho vstupu cokoli reálně měřilo.
- **Příčina:** smyčka nerozlišovala, odkud pochází její vstup (emulace × FPGA); „akční člen" byl jen jeden — skutečný DAC.
- **Oprava:** `freertos_task_fpga.c`: při emulaci smyčka řídí VIRTUÁLNÍ kód (`s_g_cmd_q16`, čte ho `gpsdo_sim_dac_code`), `g_gpsdo_drive` zůstává 0 a SensorsTask nezapisuje; změna zdroje (emulace ↔ FPGA) smyčku vrací do WAIT.
- **Pravidlo:** Cokoli, co v emulaci uzavírá zpětnou vazbu (výstup → model → vstup), musí mít VIRTUÁLNÍ akční člen oddělený od skutečného; emulovaný vstup nikdy nesmí vést k zápisu do fyzické periferie.
- **Detekce:** `fpgasim on 10000000` + `gpsdo`: `status` → `DAC OCXO` počet zápisů (`writes`) se nesmí zvyšovat a AIN0 se nesmí pohnout, zatímco `gpsdo` ukazuje FLL/LOCK.
- **Commit:** (doplnit)
- **Stav:** aktivní; ⬜ neověřeno na desce

### L-0161 — Pevné zesílení regulátoru z datasheetu rozkmitalo smyčku: strmost se musí MĚŘIT z prvních kroků

- **Datum:** 2026-10-16
- **Oblast:** smyčka GPSDO (`core_step`, `freertos_task_fpga.c`), STATUS #305
- **Symptom:** simulace na hostu (model OCXO se skutečnou strmostí 0,5×–3,5× odhadu): s pevným K = 3,4·10⁻¹¹/LSB FLL buď kmitala, nebo nezamkla do 4000 s (K 2× jiné); se špatným znaménkem K běžela donekonečna proti dorazu.
- **Příčina:** K (relativní změna kmitočtu na LSB) je naměřená JEDNOU na jedné desce při jedné teplotě (4,3–4,6 Hz/V) a strmost EFC se u OCXO liší 2,5–5,5 Hz/V (datasheet). Pevná hodnota nemá žádnou zpětnou vazbu na to, zda krok kódu udělal to, co se čekalo.
- **Oprava:** FLL po každém velkém kroku (> 50 LSB) spočte `K = Δy / Δkód` z naměřených oken: záporné K = smyčka se zastaví (špatné znaménko), K v pásmu 0,25×–4× zadaného se převezme; tři okna za sebou s kódem na dorazu a pořád odchylkou > 10⁻⁸ = konec (rozsah EFC nestačí). Selftest `gpsdo test` ověřuje 3 strmosti, špatné znaménko i nedostatečný rozsah.
- **Pravidlo:** Zesílení smyčky, které závisí na vlastnosti konkrétního kusu hardwaru, odhadni za běhu z odezvy na vlastní zásah a hlídej její znaménko; dynamiku regulátoru ověř simulací proti modelu s jiným zesílením, než pro které je navržen, dřív než ji nasadíš na HW.
- **Detekce:** `gpsdo test` (on-target) a host simulace (`scratchpad/gpsdo_sim.py`, kopie jádra); na desce `gpsdo` → `K` vypsané v řádku stavu (odhad z FLL) vs. zadané.
- **Commit:** (doplnit)
- **Stav:** aktivní; ⬜ neověřeno na desce

### L-0162 — Tlačítko s armováním do třídy s autorepeatem: držení prstu provede potvrzení samo

- **Datum:** 2026-10-16
- **Oblast:** UI (`app_gpsdo.c`, `touch_pm_control`), RESET ALLAN / RESET TREND (STATUS #306)
- **Symptom:** (zachyceno před commitem) při přidání RESET TREND do `touch_pm_control` (seznam ovládání +/−, která se při držení opakují) by držení prstu na tlačítku proběhlo armování i potvrzení za sebou a smazalo by nasbíraná data bez jakékoli volby.
- **Příčina:** dvoustupňové potvrzení předpokládá DVA samostatné stisky; třída ovládání s opakováním generuje stisky sama.
- **Oprava:** RESET ALLAN / RESET TREND se do `touch_pm_control` nepřidávají; armování vyprší za 5 s a druhý stisk je zvlášť událost.
- **Pravidlo:** Destruktivní akce s potvrzením nikdy nezařazuj do kategorie ovládání s automatickým opakováním při držení.
- **Detekce:** při přidání tlačítka s armováním zkontroluj `touch_pm_control` a ostatní třídy s `s_touch_rep`; na desce podržet prst na RESET (musí jen armovat).
- **Commit:** (doplnit)
- **Stav:** aktivní; ⬜ neověřeno na desce

### L-0163 — Náhled „prvních N řádků" ukazuje právě ty, které se přestaly měnit: karta vypadala zamrzle

- **Datum:** 2026-10-17
- **Oblast:** web SPA (`httpd_min.c`, karta ALLAN `drawStab`), STATUS #309
- **Symptom:** karta Allan na webu „neaktualizovala naměřené páry", ale po rozkliknutí (detail) data byla.
- **Příčina:** tabulka karty brala jen prvních 6 bodů mřížky 1-2-5 (τ 1…50 s). Ty mají v ringu stage plný počet členů (n ≤ 60, u τ 1 s 120) a s časem se nemění; nová data přibývají u DLOUHÝCH τ, která karta nikdy neukázala. Detail kreslil všechny body a „PARU (posl.)" bral z nejdelšího τ. ⬜ HYPOTÉZA podle čtení kódu, ověřit v prohlížeči.
- **Oprava:** karta ukazuje 6 řádků rozložených rovnoměrně přes celý rozsah τ včetně nejdelšího; v `aWarn` poznámka, že n je členů v okně stage (max 60).
- **Pravidlo:** Kompaktní náhled časově rostoucí řady nesmí brát „prvních N" – ukaž řádky rovnoměrně přes celý rozsah (a poslední), jinak ukazuje jen nasycenou část.
- **Detekce:** porovnat kartu s detailem po hodině běhu; test `ser_test`/ruční kontrola řádků tabulky.
- **Commit:** (doplnit)
- **Stav:** aktivní; ⬜ neověřeno v prohlížeči

---

### L-0164 — Kontrola „producent mi záznam nepřepsal" musí počítat i se záznamem, který se PRÁVĚ zapisuje

- **Datum:** 2026-10-17
- **Oblast:** `sdram_log.c` (`sdram_log_get`, `sdram_log_read_back`, `sdram_log_read_abs`), audit F-0237
- **Symptom:** žádný pozorovaný; nalezeno přezkumem nové funkce `sdram_log_read_abs`, která převzala kontrolu ze dvou starších.
- **Příčina:** platnost záznamu `a` se ověřovala `s_head - a > CAP`. Slot záznamu `a` ale přepisuje zápis indexu `a + CAP`, a ten běží, když `s_head == a + CAP` (hlava se zvedá až PO zápisu). Rozepsaný nejstarší záznam proto prošel jako platný. Dvě starší funkce navíc neměly bariéru mezi kopií nevolatilních dat a čtením volatilní hlavy.
- **Oprava:** jedna čistá funkce `rec_still_valid()` = `head - a < CAP` pro všechny tři čtečky, `__DMB()` před kontrolou, selftest na hranu `head == a + CAP`.
- **Pravidlo:** U lock-free ringu kontroluj platnost proti hlavě, která už ukazuje na ROZEPSANÝ slot: nejstarší dostupný záznam je zároveň ten, který producent zapíše jako další. Mez je `>=`, ne `>`. Kontrolu napiš jednou jako funkci a otestuj hranici; neopisuj výraz do každé čtečky.
- **Detekce:** selftest `sdram_log_selftest` (hranice `CAP+7` vs. `7`); disassembly `sdram_log_get` → `movcs` (≥).
- **Commit:** `90c647a`
- **Stav:** aktivní; ⬜ neověřeno na HW

---

### L-0165 — Číslo epochy v `.bss` začíná po každém bootu stejně: navazující klient nepozná jinou řadu

- **Datum:** 2026-10-17
- **Oblast:** `sdram_log.c` (`s_epoch`), `ipc.c` (`ipc_raw_service`), `tools/raw_fetch.py`, audit F-0238
- **Symptom:** žádný pozorovaný (v0.20.0 na desce neběžela); nalezeno přezkumem.
- **Příčina:** epocha měla odlišit „jinou řadu" od pokračování, ale počítala se od nuly v `.bss`, takže první epocha KAŽDÉHO běhu měla číslo 1. Klient s kurzorem `(1, N)` by po restartu desky dostal prázdné odpovědi, pak záznamy od `N` nové řady, a obě řady by slepil bez hlášení.
- **Oprava:** první epocha po startu z cyklů CPU v okamžiku prvního měření (FPGA měří nezávisle na bootu STM32 → náhodný okamžik), rozsah 1..2³⁰−1 (CM4 parsuje přes `atol`); pojistka na serveru: kurzor za hlavou ve stejné epoše = nová řada (`newep`); `raw_fetch.py` `newep` hlásí.
- **Pravidlo:** Identifikátor, podle kterého klient pozná pokračování, musí být jedinečný přes RESTART poskytovatele, ne jen během jednoho běhu. Kde to jde, přidej i kontrolu konzistence, která nezávisí na tom identifikátoru (kurzor za hlavou je v téže řadě nemožný).
- **Detekce:** napodobenina `/api/raw` s restartem při stejném čísle epochy → nástroj musí hlásit `NOVA RADA`.
- **Commit:** `695b7ce`
- **Stav:** aktivní; ⬜ neověřeno na HW

---

### L-0166 — Nový sloupec CSV mění formát souboru, do kterého se jen připisuje

- **Datum:** 2026-10-17
- **Oblast:** `datalog_sd.c` (`mirror_open`), `sd_export.c` (hlavička), audit F-0239
- **Symptom:** žádný pozorovaný; zrcadlo `GPSDOLOG.CSV` by po upgradu na v0.19.0 dostalo pod 11sloupcovou hlavičku 12sloupcové řádky.
- **Příčina:** přidání pole `dac_code` změnilo řádek CSV, ale zrcadlo na téže kartě soubor jen otevřelo a připisovalo. Hlavičku psalo jen při založení souboru.
- **Oprava:** při otevření se hlavička existujícího souboru porovná s aktuální; jiný formát → starý soubor se přejmenuje na `GPSDOLnn.CSV` a založí se nový (nepřepisuje se). Prázdný soubor bez hlavičky se založí znovu.
- **Pravidlo:** Každý soubor, do kterého se připisuje, musí při otevření ověřit, že jeho formát je ten, který se bude psát. Změna záznamu = projdi všechny trvalé konzumenty, nejen ty, které ho čtou za běhu.
- **Detekce:** karta se zrcadlem ze starší verze → po upgradu musí vzniknout `GPSDOL01.CSV` a nový `GPSDOLOG.CSV` s aktuální hlavičkou (hláška `F-0239` na konzoli).
- **Commit:** `056168e`
- **Stav:** aktivní; ⬜ neověřeno na HW (zápis na kartu)

---

### L-0167 — Odhad z historie musí začínat až u poslední změny parametru, který ta historie odráží

- **Datum:** 2026-10-17
- **Oblast:** `freertos_task_fpga.c` (`gpsdo_set_tau`, `gpsdo_tau_advise`), audit F-0241
- **Symptom:** žádný pozorovaný; „tau min." by po `gpsdo tau` počítal až 2 h z fáze, kterou tvarovala předchozí smyčka.
- **Příčina:** změna τ nepřerušila úsek LOCK ani historii a odhad bral celý úsek.
- **Oprava:** `gpsdo_set_tau` si pamatuje čas změny, odhad bere jen body po ní (do 30 min ukazuje „málo dat").
- **Pravidlo:** Když statistika z historie závisí na nastavení, které se dá za běhu změnit, změna nastavení musí historii pro tu statistiku uříznout (L-0101: nulovat i to, co je na cestě).
- **Detekce:** `gpsdo tau 500` a hned `gpsdo` → `tau min. -- (malo dat … od zamceni/zmeny tau …)`.
- **Commit:** `95d835b`
- **Stav:** aktivní; ⬜ neověřeno na HW

---

### L-0168 — Rychlý periodický zápis na sdílenou sběrnici: podmínky znovu v mutexu, krátké timeouty, tlumení při chybě

- **Datum:** 2026-10-17
- **Oblast:** `freertos_task_sensors.c` (`DacTask_run`, `ad5693_servo`), `freertos.c`, STATUS #317 (dither DAC 200 Hz)
- **Symptom:** žádný pozorovaný; návrh dithru 200 Hz odhalil tři pasti dřív, než se dostaly na desku.
- **Příčina / pasti:** (1) Dither dřív běžel v 500ms cyklu SensorsTasku, kde byl jediným zapisovačem DAC; vlastní task by zapsal kód i po ručním `dac <kód>`, pokud by podmínky ověřil jen PŘED čekáním na mutex (mezitím proběhne zápis z SensorsTasku). (2) `HAL_I2C_Master_Transmit` je aktivní čekání s timeoutem; u tasku s nejvyšší prioritou by 10ms timeout při zaseknuté sběrnici běžící každých 5 ms vyhladověl UiTask i defaultTask (pravidlo „žádný spin > 10 ms"). (3) `hi2c1.ErrorCode` je jeden pro všechny volající; `i2c1_recover_if_wedged` ho čte po transakcích SensorsTasku, takže druhý zapisovač jeho význam „poslední transakce" jen zachová, pokud selhání přetrvá (zaseknutá sběrnice selže i při dalším zápisu).
- **Oprava:** `ad5693_servo_ready()` se volá levně před mutexem a znovu UVNITŘ mutexu; mutex i zápis mají timeout 2 ms (zápis 3 B @ 400 kHz trvá ~0,1 ms); po 3 selháních za sebou se zkouší jen 1× za 500 ms a obnovu sběrnice dál dělá SensorsTask; zálohou je dither 2 Hz v SensorsTasku, kdyby se task nevytvořil (malá halda).
- **Pravidlo:** Nový rychlý zapisovač na sběrnici s existujícím vlastníkem: (a) kontrolovat podmínky znovu po získání mutexu, (b) timeouty řádově kratší než perioda, (c) back-off po opakovaném selhání, (d) nechat vlastníkovi sběrnice obnovu a ověřit, že vidí chybu, (e) mít zálohu při selhání `osThreadNew`.
- **Detekce:** `dac dither test` (logika), `dac` → `kroky bez mutexu` / `vynechano po chybe`, `stats` → DacTask stackfree a CPU; ruční `dac 24000` za běhu smyčky nesmí být přepsán (smyčka se vypne, kód zůstane).
- **Commit:** `a3aa147`
- **Stav:** aktivní; ⬜ neověřeno na HW

---

### L-0169 — Odhad z rozdílu dvou zašuměných měření: absolutní práh kroku neříká nic, krok se musí srovnat se šumem
 
- **Datum:** 2026-10-17
- **Oblast:** `freertos_task_fpga.c` `core_step` (FLL smyčky GPSDO), STATUS #318
- **Symptom:** `gpsdo test` FAIL pro model s K 1,7·10⁻¹¹; po malém kroku (−73 LSB) se změřilo záporné K a smyčka vyhlásila falešnou divergenci.
- **Příčina:** K se odhadovalo jako Δy/Δkód pro každý krok > 50 LSB (absolutní práh). Očekávaná změna 73 × 1,7·10⁻¹¹ = 1,2·10⁻⁹ je ale menší než šum rozdílu dvou 16s oken (~3,7·10⁻⁹), takže znaménko je náhodné.
- **Oprava:** K se měří jen když nejmenší věrohodná změna (0,25·K0·|Δkód|) přesahuje 5 σ rozdílu oken; σ okna = √2·PM/délka okna (nové `fll_prev_n`). Jinak platí dosavadní odhad.
- **Pravidlo:** Veličina odvozená dělením (Δy/Δx) smí rozhodovat o divergenci/znaménku jen tehdy, když je čitatel významný proti šumu měření; práh na jmenovateli (kroku) to nezaručí.
- **Detekce:** `gpsdo test` (K modelu 1,7·10⁻¹¹ je regresní případ), host simulace `st_fix_sim.py`.
- **Commit:** `19afcb9`
- **Stav:** aktivní; ⬜ neověřeno na HW

---

### L-0170 — Odhad nad celou historií bez brány a zlomu navždy drží přechodové jevy a mezery

- **Datum:** 2026-10-18
- **Oblast:** `screen_main.c` (Allan pyramida, A2), audit `docs/audit/2026-10-17_a2-allan.md` F-0242/F-0243
- **Symptom:** (nález z auditu, bez pozorování na desce) po náběhu OCXO nebo kroku FLL by ADEV na středních τ zůstala nadsazená, dokud uživatel nedá RESET ALLAN.
- **Příčina:** okno 60 položek přechodový jev samo zapomnělo; průběžné součty od resetu (A2) ne. Totéž platí pro mezery (STOP/RUN, ztráta signálu): člen přes mezeru nese změnu kmitočtu za celou pauzu.
- **Oprava:** brána (do pyramidy jen při ustálené referenci), počet položek od posledního zlomu (`since`; člen nesmí přesáhnout zlom), zlom po > 30 s bez vzorku a po rekonstrukci z logu. Krátké díry (spike okna) se NEpočítají jako zlom, jinak by se decimace nikdy nedokončila. Reset na hraně ustálení se nepoužil: smazal by i rekonstrukci z datalogu po warm resetu.
- **Pravidlo:** Kdo změní odhad z „okna" na „historii", musí zároveň rozhodnout, co z historie vyřadit (neustálený stav, mezery) a kdy; okno to dělalo zadarmo.
- **Detekce:** `meas gate` (stav brány), selftest A2 (člen nepřekročí zlom), `allan log` jako nezávislá kontrola.
- **Commit:** `03f5ca5`
- **Stav:** aktivní; ⬜ neověřeno na HW

---

### L-0171 — Monte Carlo: normalizace každé realizace její vlastní směrodatnou odchylkou zkreslí rozptyl odhadu

- **Datum:** 2026-10-18
- **Oblast:** `docs/audit/sim/2026-10-18_edf_velke_M.py` (V8, EDF)
- **Symptom:** empirické EDF blikavých šumů vycházelo ~50 místo stovek až tisíců (poměr k formuli 0,01–0,2).
- **Příčina:** generátor 1/f šumu dělil každou realizaci její vlastní směrodatnou odchylkou. U blikavého šumu ji určují náhodné nejnižší frekvence, takže AVAR dostal umělý rozptyl navíc.
- **Oprava:** pevná amplituda bez normalizace realizace (EDF je relativní veličina). Po opravě sedí empirické EDF s formulí v ±12 % (až na blikavý PM pro velká m).
- **Pravidlo:** Když simulace nesedí s teorií o řády, podezírej nejdřív měřicí nástroj (generátor), ne teorii (SKILL §0, L-0097).
- **Detekce:** poměr empirické/teoretické EDF pro bílé šumy (musí vyjít ≈ 1 dřív, než se věří blikavým).
- **Commit:** `5e1bfdc`
- **Stav:** aktivní

---

### L-0172 — Funkce, která kaskáduje do `pyr[1..n-1]`, dostane délku pole SPOLU s polem
 
- **Datum:** 2026-10-18
- **Oblast:** `screen_main.c` (`hamp_push`, `adev_feed_into`, selftest filtru), audit v0.27.0 F-0254
- **Symptom:** (nález z auditu, před flashem) selftest filtru by přepsal paměť za svými statickými objekty včetně vlastního stavu `th`.
- **Příčina:** `adev_feed_into(pyr, nst, ...)` po každých 10 položkách zapisuje do `pyr[1]`, `pyr[2]` … Wrapper `hamp_push` předával pevně `ADEV_STAGES`, test mu dal ukazatel na jeden objekt. Python model chybu nezachytil, protože jeho seznamy měly vlastní délku.
- **Oprava:** délka pole je parametr (`nst`) a předává ji ten, kdo pole vlastní; test používá pole s kanárkem (0xA5) za koncem, který se po testu kontroluje.
- **Pravidlo:** Ukazatel na pole nikdy bez jeho délky; wrapper nesmí tvrdit konstantu, kterou zná jen produkční volající. Testovací kopie struktur mají mít za sebou kanárek.
- **Detekce:** kanárek v `screen_main_selftest`; grep `adev_feed_into(` – `nst` musí odpovídat velikosti pole (`nm -S` objektu / `sizeof`).
- **Commit:** viz v0.27.1
- **Stav:** aktivní; ⬜ neověřeno na HW

### L-0173 — `…=hodnota/>` v `innerHTML` bez mezery: lomítko je součást nekvótované hodnoty atributu
 
- **Datum:** 2026-10-18
- **Oblast:** `httpd_min.c` SPA (`drawSpec`, sloupce histogramu v `drawHist`)
- **Symptom:** náhled spektrogramu na webu prázdný, detail se stejnými daty funguje; histogram v náhledu skládaný stejným způsobem.
- **Příčina:** pravidlo SPA „atributy v `innerHTML` bez uvozovek“ + samozavírací `/>` těsně za hodnotou: `height=100/>`. HTML parser bere v nekvótované hodnotě `/` jako součást hodnoty (`height="100/"`), nejde o samozavírací tag; SVG délku `100/` odmítne a obdélník má výšku 0. Detail kreslí `setAttribute`, proto fungoval.
- **Oprava:** ` />` s mezerou; kontrola v `tools/spa/check.py` (regexy `=[^\s'"<>]+/>` a `+'/>` nad servírovaným HTML).
- **Pravidlo:** v řetězci pro `innerHTML` nikdy nekvótovaná hodnota těsně před `/>`; piš `hodnota />` (nebo uzavírací tag). Kdo nemůže prohlížeč otevřít, ať se opírá o specifikaci parseru, ne o to, že „tam už je“ podobný kód.
- **Detekce:** `python tools/spa/check.py` (krok „nekvotovany atribut zakonceny `/>`“ musí hlásit 0).
- **Commit:** `c2247e3`
- **Stav:** aktivní; ⬜ příčina odvozena z HTML specifikace, **vizuálně neověřeno** (prohlížeč nebyl připojen)

### L-0174 — Dlouhé měření bez předem spočítané citlivosti: noc dat, která hypotézu nerozliší
 
- **Datum:** 2026-10-18
- **Oblast:** #312 (OCXO, nebo GPS?), `tools/cov312.py`, `tools/analyze_noc_ext.py`
- **Symptom:** 10h běh proti externímu GPSDO nerozhodl nic; dřívější „horní mez OCXO ≤ …“ vypadala jako výsledek.
- **Příčina:** metoda kovariance y_čítač × y_fáze má šum referenčního kanálu (externí GPSDO, ADEV 3·10⁻¹⁰ @ 128 s) 10× nad hledaným signálem (přebytek 3·10⁻¹¹). Za hypotézy „je to OCXO“ je koherence² = var(O)²/(var_čítač·var_fáze) = 0,001–0,004, mez detekce 10h záznamu je 0,02–0,2. Horní mez spočtená ze směrodatné chyby kovariance je mez citlivosti metody, ne naměřený šum.
- **Oprava:** žádná v kódu; v analýze přibyl výpočet citlivosti (sekce 8 `analyze_noc_ext.py`) a doporučení měřit s referencí lepší než zkoumaná veličina (Rb), ne prodlužovat záznam.
- **Pravidlo:** před dlouhým měřením spočítej z dosavadních dat, co by měřicí uspořádání ukázalo, kdyby hypotéza platila (očekávaná koherence / odezva / SNR), a z toho potřebnou dobu. Vyjde-li ve dnech, změň uspořádání (lepší reference), nikdy nespoléhej na „dalších pár nocí“. „Horní mez z SE“ vždy pojmenuj jako mez citlivosti.
- **Detekce:** `python tools/analyze_noc_ext.py <csv> 3600` → sekce 8 „CITLIVOST kovariancni metody“.
- **Commit:** commit s touto lekcí a zprávou `docs/audit/2026-10-18_noc10h-analyza.md`
- **Stav:** aktivní

### L-0175 — Klient a server si „krok“ historie počítaly každý jinak: sešité dávky se překrývaly
 
- **Datum:** 2026-10-18
- **Oblast:** web SPA `fetchLog` ↔ `GET /api/log` (`httpd_min.c`), historie 24 h / 7 d / 30 d (a nově 6 h)
- **Symptom:** (nález při HW testu, nikdo to nehlásil) dávka 1 začínala 4,4 h před koncem dávky 0 — sešitá řada měla překrývající se úseky.
- **Příčina:** server počítá krok jednoho bodu jako `win/(n·10)` záznamů z parametru `win`, takže `win` = celé okno znamená, že **jedna dávka pokryje celé okno**. Klient přitom posouval `from` o `n·krok_klienta`, kde `krok_klienta = win/(DLCHUNKS·n·10)` (čtvrtina), tedy počítal s tím, že dávka pokryje čtvrtinu okna. Dvě definice téhož kroku, žádný společný zdroj.
- **Oprava:** klient posílá serveru `win/DLCHUNKS`; platí `krok_server == krok_klient` a dávky na sebe navazují.
- **Pravidlo:** když si klient a server dělí práci na dávky, **jedna z obou stran vlastní definici kroku a druhá ji jen přebírá** (parametr, ne vlastní výpočet). Kontrolu lze udělat měřením na živém zařízení: dvě po sobě jdoucí dávky nesmí mít překrývající se časové rozsahy.
- **Detekce:** `python tools/hw_smoke_net.py` (blok `/api/log`) nebo ručně `curl '/api/log?win=21600&n=48&from=0'` a `from=528`.
- **Commit:** commit s v0.29.0 (`feat(firmware): v0.29.0 …`)
- **Stav:** opraveno 2026‑10‑18, ⬜ neověřeno v prohlížeči

---

### L-0176 — Nejistota „reference“ jako konstanta lže na obě strany
 
- **Datum:** 2026-10-18
- **Oblast:** `meas_present.h` `MP_REF_PPB`, hlavní číslo (`freq_uncertain_frac`), okno ANALÝZA, web
- **Symptom:** hlavní číslo označovalo číslice jako důvěryhodné až na 1 ppb i s volně běžícím OCXO (−0,84 ppm vedle), a za zamčené smyčky (0,01 ppb) naopak 100× pesimisticky.
- **Příčina:** nejistota reference byla konstanta 1 ppb nezávislá na stavu smyčky; hlavní číslo ji navíc do rozlišení vůbec nezapočítávalo (jen `max(σy, rozlišení hradla)`).
- **Oprava:** `mp_ref_ppb(stav, proč, locked, state_s)` — jedna inline funkce, kterou volá hlavní číslo, okno ANALÝZA i JSON pro web (`ref_ppb_x100`); konstanty podle stavu (LOCK 0,01 ppb z 10h běhu … volný chod 1 ppm). Kryje ji selftest.
- **Pravidlo:** veličinu, která závisí na stavu přístroje, nikdy nenech jako `#define`; a každé zobrazení „kolik číslic platí“ musí brát nejistotu z **jedné** funkce. Hodnoty bez měření (HOLD, FLL) označ v kódu a ve FUNKCE.md jako odhad.
- **Detekce:** `selftest` (blok `mp_ref_ppb`), `status` + okno ANALÝZA řádek `reference:` s názvem stavu.
- **Commit:** commit s v0.29.0
- **Stav:** aktivní. **Doplněno v0.30.0:** konstanty FLL a HOLD nahrazeny MĚŘENÍM ze smyčky (FLL |y| okna, LOCK ADEV fáze, HOLD ADEV + drift kódu DAC); tabulka zůstává jen jako náhrada a výsledek nese příznak `zmereno/odhad` (IPC `ref_meas`). Poučení: když přístroj veličinu sám měří (a smyčka ji měří), neodhaduj ji — dej konstantu jen jako záchranu a označ ji.

### L-0177 — Prázdný graf bez vysvětlení + záložní výpočet se přepíná podle PŘÍTOMNOSTI dat, ne jejich POUŽITELNOSTI
 
- **Datum:** 2026-10-18
- **Oblast:** web SPA `fwPts` / `drawStab` / `drawZoomAdev` (Allan)
- **Symptom (hlášení uživatele po flashi v0.30.0):** detail Allanu prázdný a nešlo přepínat ADEV/TDEV/MTIE a body na dekádu.
- **Příčina:** 12 min po zapnutí pyramida čeká na ustálení reference (OCXO ~15 min) a `/api/stab` vrací **jediný** bod (τ = 1 s). `fwPts` vracel neprázdné pole → `fA` bylo pravdivé → web **nepoužil vlastní výpočet** z 190 měření a `drawZoomAdev` při `A.length < 2` skončil mlčky s prázdným SVG. Přepínače byly jen v kartě, kterou overlay detailu zakrývá.
- **Oprava:** `fwPts` vrací `null` pod 3 body (fallback na výpočet webu + varování PROČ), detail při < 2 bodech řekne proč je prázdný a co čekat, přepínače metriky/hustoty jsou i v overlayi.
- **Pravidlo:** přepnutí na záložní zdroj podle „máme data?“ je past; podmínka musí být „máme data, ze kterých jde nakreslit výsledek?“. A prázdná plocha bez věty, proč je prázdná, je chyba UI. Ovladač, který mění zobrazení overlaye, musí být V overlayi.
- **Detekce:** `node tools/spa/allan_detail_test.js <spa.js>` (scénář 2: 1 bod z přístroje + 300 měření webu; kontrolováno proti původnímu SPA — 6 chyb).
- **Commit:** commit s v0.30.1
- **Stav:** opraveno 2026‑10‑18, ⬜ neověřeno v prohlížeči (Chrome rozšíření nebylo připojeno; ověřeno v Node nad atrapou DOM)

---

### L-0178 — Znak `%` ve velkém subsetovaném fontu (`mono_25`) se tiše přeskočí
 
- **Datum:** 2026-10-18
- **Oblast:** okno NASTAVENÍ GPS (`gpscfg_draw_vals`), `libprim/src/text.c`
- **Symptom:** `status` po otevření okna: `FONTY: přeskočených glyfů 26`, `poslední chybějící znak U+0025 ve fontu s ascent 26`.
- **Příčina:** rychlostní faktor `66 %` vykreslený `mono_25` (subset: číslice, písmena, ` :.,-/()=?Δ`) — `%` v sadě není.
- **Oprava:** hodnota bez `%`, jednotka v popisku řádku (`dlabel`, mono_18 má plnou sadu). Čítač chybějících glyfů nově ukládá **poslední kódový bod a ascent fontu**, takže se příště nehledá slepě.
- **Pravidlo:** před novým řetězcem ve fontu `mono_25/30/52/75`, `sans_32` ověř sadu (CLAUDE.md „TŘI tiché pasti“); po flashi vždy `status` → `FONTY`.
- **Detekce:** `status` → `FONTY:` (musí být 0).
- **Commit:** commit s v0.30.1 (diagnostika kódového bodu přišla ve v0.29.0)
- **Stav:** opraveno 2026‑10‑18, ⬜ neověřeno na HW

### L-0179 — Podlaha čítače v Allanu končila u 10 s: osa se řídila jen křivkou
 
- **Datum:** 2026-10-18
- **Oblast:** `allan_metric_yrange` (displej), `axLim` (web)
- **Symptom (hlášení uživatele):** čerchovaná čára podlahy končí u 10 s.
- **Příčina:** pravidlo z 2026-10-07 „osa jen podle křivky“ (aby podlaha ~1/τ nestáhla graf) způsobilo, že podlaha nad 10 s ležela pod osou. Pravidlo bylo správné proti krajnosti (1e-16), ale přehnané.
- **Oprava:** osa se natáhne k podlaze, nejvýš 7,5 dekady pod nejvyšší bod křivky (displej `flo`, web `axLim`); test `allan_detail_test`.
- **Pravidlo:** omezení typu „X nesmí tahnout osu“ vždy formuluj jako mez (kolik dekád), ne jako zákaz; zákaz schová data, mez jen krajní případ.
- **Stav:** opraveno 2026‑10‑18, ⬜ neověřeno na HW

### L-0180 - Výchozí hodnota se změnila, popisy v pěti souborech zůstaly na staré

- **Datum:** 2026-10-18
- **Oblast:** dokumentace / komentáře (dither DAC OCXO)
- **Symptom:** `status` hlásí „dither 100 Hz“, ale CLAUDE.md, `ad5693.h`, `freertos_shared.h`, `HW_REFERENCE.md` a hláška `dac dither` tvrdí 200 Hz (dotaz uživatele 2026-10-18).
- **Příčina:** ve v0.21.1 se výchozí rychlost snížila z 200 na 100 Hz (`DAC_DITH_DEFAULT_HZ`), opraven byl jen `version.h`, `MERENI_A_KALIBRACE.md` a `FUNKCE.md`. Hodnota byla zapsaná na ~8 místech jako číslo, ne jako odkaz na symbol.
- **Oprava:** texty srovnány; rychlost ověřena měřením (počet zápisů při známém zlomku povelu: ≈ 17,5/s sedí na 100 Hz, ne na 200 Hz, kde by to bylo ≈ 28/s). Hláška v `freertos_task_uart.c:2314` zůstává (chování, samostatný `fix:`).
- **Pravidlo:** při změně výchozí hodnoty nejdřív `grep -rn "<stará hodnota>"` přes `*.md`, `*.h`, `*.c`; v komentářích odkazuj na symbol (`DAC_DITH_DEFAULT_HZ`), ne na číslo.
- **Detekce:** grep výše; `status` (zobrazuje skutečnou hodnotu).
- **Commit:** commit `docs:` s touto lekcí
- **Stav:** aktivní

### L-0181 - Off-by-one v parsovani UART retezce: obe vetve volby poslaly stejnou hodnotu

- **Datum:** 2026-10-19
- **Oblast:** `freertos_task_uart.c` (UART `gps nav5 stationary|auto`)
- **Symptom:** `gps nav5 stationary` i `gps nav5 auto` poslaly UBX-CFG-NAV5 s dynModel Portable; odhaleno zivym testem (`gps ack`) pred spustenim noci mereni, ne az z vysledku.
- **Priina:** `bool stat = (RxBuffer[10] == 's');` - retezec "gps nav5 " ma 9 znaku (indexy 0..8), prvni znak nazvu volby je tedy na indexu 9, ne 10. Index byl odhadnut, ne spocitan.
- **Oprava:** `RxBuffer[9] == 's'`, overeno `gps ack` pro oba smery pred spustenim (`freertos_task_uart.c`).
- **Pravidlo:** index do retezce za pevnym prefixem VZDY spocitat (delku prefixu vcetne mezery), ne odhadnout; u noveho vetveni na podretezec otestovat OBE vetve na HW/konzoli pred spustenim delsiho mereni - test jedne vetve by chybu neodhalil.
- **Detekce:** zivy test obou voleb pres `gps ack` pred kazdym novym UART prepinacem tohoto typu.
- **Commit:** oprava pred buildem v0.30.7 (pred commitem v teto session)
- **Stav:** aktivni

### L-0182 - Zmrazeny vzorek TDC: povoleni nacteni chybelo v POSLEDNIM taktu zmrazeni (N1, FW 0x0425)

- **Datum:** 2026-10-20
- **Oblast:** FPGA `tdc.v` (`tdc_chan`: `ld`, `fz`, `idle_r`)
- **Symptom:** zadny viditelny; pri spousteni hrana za hranou (segmenty regrese) na ~16,7-25 MHz dostala cast hran kod TDC z PREDCHOZI hrany (chyba az jednotky ns na znacce). Nalezeno auditem a modelem `m_tdc_ld.py` (druha instance), ne mericim symptomem.
- **Priina:** `ld = idle_r & ~(ec & arm_r)` je 0 po cele zmrazeni (fz 1..4). Vzorek hrany, ktera se spusti v prvnim taktu po zmrazeni, mel byt do `qd` nahran jeste v taktu fz == 4 (idle_r = 0) -> zustal tam vzorek predchozi hrany.
- **Oprava:** registr `ldok <= ~trig_ok & (idle_r | fz==3 | fz==4)`, `ld = ldok & ~(ec & arm_r)` (`tdc.v`, FW 0x0425).
- **Pravidlo:** u zmrazovane kopie vzorku (pipeline s mrtvou dobou) over OKRAJ mrtve doby: hrana presne N taktu za predchozi musi dostat SVUJ vzorek; testuj periodou rovnou mrtve dobe a o zlomek kolem ni.
- **Detekce:** `sim/tb_n1.sv` (`run.ps1 n1`): qd pri dekodovani == q_r z taktu pred spustenim. 0x0425: 0 chyb; 0x0424: 1,7 / 44,9 / 99,6 / 21,2 % pri 16,7 / 18 / 20 / 22,5 MHz (shoda s modelem 1,2 / 44 / 100 / 20 %).
- **Commit:** commit integrace E0 (FW 0x0425)
- **Stav:** aktivni

### L-0183 - Hrana v taktu nulovani citace okna se ztratila (close+2, FW 0x0425)

- **Datum:** 2026-10-20
- **Oblast:** FPGA `spi_app.v` (`win_recip`: `count_lo`, `cy_q`, `cl_q2`)
- **Symptom:** nad ~33 MHz (hrany >= 2 takty od sebe a casto) okno obcas ztratilo hranu -> kmitocet o 1/N mene; kontrola `|ccd| <= 1` to propustila (model `m_win_count.py`: 6 / 60 / 96 % oken pri 34 / 40 / 49 MHz).
- **Priina:** v taktu `cl_q2` se citac nuloval (`count_lo <= 0`) a hrana, ktera prisla prave v tom taktu, se nezapocetla ani do stareho, ani do noveho okna.
- **Oprava:** v `cl_q2` `count_lo <= {7'd0, inc}` (hrana se zapocte do noveho okna), prenos `cy_q` blokovany `~cl_q2`.
- **Pravidlo:** citac, ktery se nuluje v tomtez taktu, kdy muze prijit udalost, musi udalost toho taktu zapocitat do NOVEHO intervalu (`<= inc`, ne `<= 0`); kontrola tolerujici +-1 takovou chybu nikdy neuvidi.
- **Detekce:** `sim/tb_close2.sv` (`run.ps1 close2`, cely top, 40 MHz): kmitocet kazdeho okna v mezi 2e-6 (ztrata 1 hrany v kratkem SIM_TOP okne = ~1e-5).
- **Commit:** commit integrace E0 (FW 0x0425)
- **Stav:** aktivni

### L-0184 - Test na PRESNEM nasobku hodin zastaraly vzorek neodhali

- **Datum:** 2026-10-20
- **Oblast:** simulace (`sim/tb_n1.sv`)
- **Symptom:** prvni verze `tb_n1` hlasila u stareho RTL pri 20,0 MHz 0 % chyb, model 100 %.
- **Priina:** 20,0 MHz = presne 5 taktu 100 MHz -> faze hrany vuci hodinam se nemeni a vsechny vzorky jsou bit po bitu stejne; zastaraly vzorek je k nerozeznani od spravneho (a v tom pripade ani nezpusobi chybu casu). Model pocital identitu vzorku, test jeho hodnotu.
- **Oprava:** vstup testu `f · (1 + 813 ppm)` (asynchronni k hodinam, faze prochazi) -> starý RTL 99,6 % chyb pri 20 MHz.
- **Pravidlo:** testovaci signal pro TDC/vzorkovani VZDY mirne rozlad proti hodinam (realny zdroj je asynchronni); presny nasobek hodin schova chyby, ktere zavisi na fazi.
- **Detekce:** revize testbenchu: perioda vstupu nesmi byt racionalni nasobek periody hodin bez rozladeni.
- **Commit:** commit integrace E0 (FW 0x0425)
- **Stav:** aktivni

### L-0185 - Zmena formatu v RTL: upravit VSECHNY testbenche s referencnim modelem, ne jen ty, ktere padly prvni

- **Datum:** 2026-10-20
- **Oblast:** simulace (`sim/tb_regr_acc.sv`, `sim/tb_tx_equiv.sv`)
- **Symptom:** po E0 (FW 0x0425) celá sada: `regracc` FAIL (xm z DUT presne 16x model), `txequiv` FAIL (3294 rozdilu).
- **Priina:** zadna vada RTL. Pri prechodu `xm` z 1/256 na 1/4096 hrany jsem upravil model ve trech testbenchich (`tb_regr_full`, `tb_regr_e2e`, `tb_top_regr`), ale ctvrty (`tb_regr_acc`, `<< 8`) jsem prehlidl. `tb_tx_equiv` srovnava s referenci 0x0424 a nevedel, ze E0 zamerne posila nuly misto ladicich bajtu prijmu (`DBG_RX = 0`: abs 66, 67, 107, 112..117, 124, 125). Navic vypis rozdilu byl omezen na 8 radku, takze nebylo videt, ktere indexy se lisi.
- **Oprava:** `tb_regr_acc` `<< 12`; `tb_tx_equiv` u DBG_RX bajtu KONTROLUJE nuly (ne preskakuje) a na konci vypise souhrn vsech indexu s rozdilem.
- **Pravidlo:** pri zmene meritka nebo formatu pole grepni jeho jmeno ve VSECH `sim/*.sv` a uprav kazdy model najednou. Zamerne zmeneny bajt se v ekvivalencnim testu overuje proti nove ocekavane hodnote, nevyrazuje se.
- **Detekce:** `grep -n "xm\|<< 8" sim/*.sv` pred spustenim sady; souhrn rozdilu podle indexu v `tb_tx_equiv`.
- **Doplneno 2026-10-21 (treti pripad, `tb_regr_e2e`):** timeout 600 ms, zadne okno s regresnimi daty. Testbench instancuje `win_recip` PRIMO a nezapojil nove vstupy 0x0425 (`alias_lim`, `bub`, `cs_act`, `mask_cs`). Plovouci `mask_cs & cs_act` = `x` -> `want_seg` = `x` -> vnitrni znacky se nespousti. Icarus na nezapojeny vstup nevaruje. Oprava: zapojit vsechny porty (`mask_cs` 1, `cs_act` 0, `alias_lim` 250, `bub` z `tdc_chan.bub_o`).
- **Pravidlo 2:** pri pridani PORTU do modulu grepni vsechny jeho primé instance v `sim/*.sv` (`grep -n "win_recip\|tdc_chan" sim/*.sv`), nejen `top.v`. Nezapojeny vstup v simulaci = `z`/`x`, ne 0.
- **Commit:** commit integrace E0 (FW 0x0425)
- **Stav:** aktivni

### L-0186 - Selftesty po E0: buffer 1 kB na zasobniku FpgaTasku a test zavisly na naucene adaptivni mezi

- **Datum:** 2026-10-21
- **Oblast:** `fpga_freq.c` (`fpga_freq_crc_selftest`, `fpga_freq_select_selftest`), FpgaTask
- **Symptom:** po flashi v0.31.0 a power-cyklu `status` -> `stack Fpga free 36 B` (z 2560) a `selftest` -> `15/16 FAIL, #1`. Prijem dat i regrese na desce pritom fungovaly.
- **Pricina:** (1) E0 pridalo do CRC selftestu porovnani tabulky s referenci nad `uint8_t buf[1024]` NA ZASOBNIKU; test bezi ze startu FpgaTasku (`fpga_freq_init`) -> ramec 1056 B (`.su`). (2) Mez prijeti regrese je od E0 adaptivni (`fpga_freq_regr_limit`); selftest pocital s pevnou mezi 5e-8 a okno s odchylkou 1e-8 ocekaval jako prijate. Jakmile se mez na desce naucila (~5 ppb), test padal -- vysledek zavisel na tom, kolik oken FPGA uz dodala. Navic test na konci `fpga_freq_regr_reset()` mazal zivou statistiku `regr`.
- **Oprava:** (1) `buf[FR_LEN]` (128 B, delka ramce) -> ramec 160 B; `static` nejde, funkci soubezne vola `run_selftests` z jineho tasku. (2) test ulozi stav adaptace a pocitadla, bezi s vychozi mezi (stav zahrivani) a stav pod zakazanym prerusenim vrati. v0.31.1.
- **Pravidlo:** (a) pred pridanim pole do selftestu zkontroluj, Z KTEREHO TASKU se vola (`.su` + zasobnik tasku), nejen velikost pole. (b) Selftest funkce s adaptivnim nebo jinak zivym stavem musi ten stav pro test nastavit a pak vratit -- jinak test meri historii zarizeni, ne kod.
- **Detekce:** `status` -> `stack Fpga free`; `grep fpga_freq CM7/Release/Core/Src/fpga_freq.su`; `selftest` az po nekolika minutach behu (ne jen pri startu).
- **Commit:** fix v0.31.1
- **Stav:** aktivni, neovereno na desce

### L-0187 - Delicka regrese ve FPGA pocitala na krzemiku spatne dolni bity, prestoze simulace i STA byly v poradku

- **Datum:** 2026-10-21
- **Oblast:** FPGA `regr_acc` (spi_app.v), STM `fpga_freq.c` (parse_data), fazovy zaznam
- **Symptom:** ADEV z fazoveho zaznamu 3,6e-9 @ 1 s proti 7e-11 ze statistiky. `phrec dump`: teziste indexu `xm` nesedelo na casove teziste `ym` -- soucet indexu „o 2^16 vedle" v 89 % oken (10 MHz), o 2^15 jinde; chyba stejna v segmentu A i B, takze se v regresi odecetla a nikdo ji nevidel.
- **Pricina:** delic `regr_acc` (68 kroku, 3 takty/krok, multicycle v SDC) dava na krzemiku chybu v dolnich ~11 bitech podilu x. Dukaz: diagnosticky build (DIAG_SX) poslal surovy sum(x-x0) -- ten byl PRESNE n(n-1)/2 ve vsech oknech, podil ale chybny; pro tataz data vydany 0x0425 +655 jednotek, diagnosticky build +1614 => zavisi na rozmisteni v P&R = casovani, ne logika. Simulace (plna velikost 312 500 znacek, win_recip s modelem TDC) i bitovy model delicky byly spravne a STA hlasila rezervu.
- **Oprava:** FW 0x0426 posila SUROVE SOUCTY (n, x0, sum(x-x0), t0-ref, sum(t-t0)>>3; caps bit3 FPGA_CAP_RAWREG), STM deli celociselne v uint64 (`rg_raw_to_mean`); delic ve FPGA odstranen (CLS 85 % beze zmeny, registry 66 -> 64 %), zrusene i jeho vyjimky v SDC. STM v0.32.0.
- **Pravidlo:** (a) aritmetiku, kterou umi levne STM (deleni, nasobeni siroka cisla), ve FPGA nedelat -- FPGA ma posilat surove soucty; (b) kdyz se dve velici, ktere maji byt navzajem konzistentni (index a cas tehoz teziste), rozchazeji, nejdriv z dat urcit, KTERA z nich je spatne (souvisla rada => index musi mit zlomek 0 nebo 0,5), a pak diagnostickym buildem zmerit mezivysledek (tady surovy soucet) -- jedno meritko rozhodlo, kde chyba vznika; (c) chyba, ktera je stejna ve dvou vetvich, se v jejich rozdilu vyrusi a zustane skryta -- testy maji kontrolovat i absolutni hodnotu kazde vetve.
- **Detekce:** `phrec dump` + skript (index z ym proti xm); `tb_win_regr_full` kontroluje sum(x-x0) = n(n-1)/2.
- **Commit:** feat FPGA 0x0426 + STM v0.32.0
- **Stav:** aktivni, neovereno na desce

### L-0188 - Kalibracni tabulka TDC pocitana ve FPGA: aritmetika, kterou umi STM, stala 6-8 p. b. CLS (S1/S2)

- **Datum:** 2026-10-21
- **Oblast:** FPGA `tdc.v` (kalibrace), `spi_app.v` (RX), `top.v`, STM `fpga_tdccal.c`, `fpga_freq.c`
- **Symptom:** CLS 85 % = strop vydavaci brany; v3 se nevejde. Rozbor netlistu (`docs/audit/2026-10-21_uspora-fpga-a-presun-do-stm.md`): kalibrace TDC (`cum`, `xv`, `hcur`, `lutv`, `scan_k`, `d_*`, komparatory) ~570 FF + ~300 ALU/LUT, a nic z toho nepotrebuje 100 MHz.
- **Reseni (FW 0x0427 + STM v0.33.0):** FPGA po sberu histogramu ceka (`cal_wait`), STM precte histogram (`CAL` ramce), spocita tabulku stejnym vzorcem (`fpga_tdccal_lut`), zapise ji (TYPE 0x0A `LUT_WRITE`, 1 polozka na ramec: adresa 9 b + kanal + `neg` 14 b; `amt` se odvodi z `neg != 0`), overi pocitadlo a kontrolni soucet z DATA ramce (abs 112..115) a potvrdi (`SET_CONFIG 0x06 = 1`). Statistiky retezu `ovf/peak/nz/last` pocita STM z histogramu. Puvodni vypocet zustava pod `AUTOCAL = 1` jen pro starsi testbenche (`-DSIM_AUTOCAL`). Vysledek P&R: **CLS 85 -> 79 %, registry 64 -> 60 %**.
- **Overeni:** `tb_lutequiv` (vzorec == puvodni vypocet FPGA na vsech 256 adresach), zlate vektory z toho testu ve STM selftestu (`fpga_tdccal_selftest`), `tb_stmcal` (koncovy tok pres SPI), `tb_tx_equiv`.
- **Pravidlo:** (a) aritmetika, kterou STM zvladne levne a kterou FPGA nepotrebuje v realnem case (dělení, sumy, tabulky z histogramu), do FPGA nepatri -- kazdy bit ALU stoji ~pul CLS a navic je dalsi mozne misto, kde krzemik selze bez priznaku (L-0187); (b) kdyz se funkce presouva, nejdriv EXAKTNI test shody vzorce (tady 256 adres 1:1) a zlate vektory do cilove implementace, az pak zmena toku; (c) novy kanal STM->FPGA ma mit pocitadlo a kontrolni soucet vracene v DATA ramci (zapis bez zpetne vazby by pri ztracenem ramci tise nechal polovinu tabulky nominalni).
- **Detekce:** `status` -> `TDC KALIBRACE (STM)` (behy, selhani, opakovani, doba); `selftest` 16/16; `tdc` (statistiky z histogramu).
- **Commit:** feat FPGA 0x0427 + STM v0.33.0
- **Stav:** aktivni, neovereno na desce

### L-0189 - Pouziti signalu pred deklaraci: Gowin i starsi Icarus to prelozi, Icarus 13 ne

- **Datum:** 2026-10-22
- **Oblast:** FPGA `tdc.v` (simulace), `sim/run.ps1`
- **Symptom:** na Icarus Verilog 13.0 (macOS, Homebrew) 12 z 27 testu `COMPILE FAIL`: `Unable to bind wire/reg/memory 'newF'` (tdc.v, `always` blok pred `wire newF`) a `'lastk_r'` (radic kalibrace pred `reg lastk_r`). Na Windows (starsi Icarus) a v Gowin synteze vse prochazelo.
- **Pricina:** Verilog vyzaduje deklaraci pred pouzitim; starsi Icarus a Gowin to tolerovaly. `newF` se pouziva pred deklaraci od `4667d7c`, `lastk_r` pribyl ve FW 0x0427 (deklarace presunuta za radic, aby byla mimo `generate`).
- **Oprava:** obe deklarace presunuty pred prvni pouziti (`wire newR/newF` nad `always`, `reg lastk_r` k ostatnim registrum radice). Logika ani netlist se nemeni.
- **Pravidlo:** (a) signal deklarovat pred prvnim pouzitim, i kdyz to nastroj snese - jiny nebo novejsi nastroj to odmitne a simulace pak tise chybi; (b) prekladat simulace aspon obcas i jinou verzi nastroje (tady Icarus 13 odhalil, co verze na Windows nevidela).
- **Detekce:** `iverilog -g2012` 13.x nad testy z `sim/run.ps1` (vsech 27 se musi prelozit).
- **Commit:** fix(fpga) tdc.v deklarace pred pouzitim
- **Stav:** aktivni, overeno prekladem (Icarus 13.0); bitstream neprestaven

### L-0190 - Stav sigma-delta se posouval i pri neuspesnem zapisu do DAC (F-0259)

- **Datum:** 2026-10-22
- **Oblast:** `freertos_task_sensors.c` `ad5693_servo` (dither DAC OCXO)
- **Symptom:** zadny pozorovany; audit v0.21-v0.22: pri selhani zapisu po I2C1 se akumulator zlomku posunul, kod v DAC zustal stary -> stredni hodnota se odchylila o krok (max 1 LSB = 3,4e-11 po dobu poruchy).
- **Pricina:** `dither_next(&s_dith_acc, q)` menil stav PRED zapisem; vysledek zapisu se do stavu nepromital.
- **Oprava:** krok se pocita do kopie akumulatoru a potvrdi se az po uspesnem zapisu (nebo kdyz se kod nemeni); po selhani dalsi krok zkusi tentyz kod. v0.33.1.
- **Pravidlo:** stav stavoveho automatu (akumulator, citac, ukazatel) posouvat az po uspechu akce, kterou reprezentuje; pri selhani nechat stav tak, aby opakovani udelalo totez.
- **Detekce:** `status` -> `DAC OCXO: ... vynechano po chybe`; `dac dither test` (logika ditheru).
- **Commit:** fix(firmware) v0.33.1
- **Stav:** aktivni, prelozeno na macOS (CubeIDE 2.1.0, GCC 14.3, 0 varovani, audit 95/0/2), neovereno na desce (`ad5693_servo` 0xB8 -> 0xC4 B v obrazu)

### L-0191 - Popisky na displeji prezily hardware, ktery popisovaly (F-0261, /4 a /16)

- **Datum:** 2026-10-22
- **Oblast:** `app_gpsdo.c` (okno Citac - chybove priznaky, okno Selftest - nazev testu #1)
- **Symptom:** okno Citac hlasilo chyby kanalu jako `E/4`/`E/16` a selftest #1 se jmenoval „Hystereze /4 <-> /16", prestoze deska od FW 0x0400 delice nema a test overuje ramec a regresi.
- **Pricina:** pri zmene vyznamu poli (`freq16_x100000` = kanal B, selftest #1 rozsiren o ramec 0x0426) se prejmenovaly jen identifikatory a web, ne texty na displeji.
- **Oprava:** `E-A`/`E-B`, „FPGA ramec + regrese". v0.33.1.
- **Pravidlo:** pri zmene vyznamu pole nebo testu grepni i retezce zobrazene uzivateli (displej, web, UART), nejen identifikatory.
- **Detekce:** `grep -n '"[^"]*/16' CM7/app/app_gpsdo.c`.
- **Commit:** fix(firmware) v0.33.1
- **Stav:** aktivni, prelozeno na macOS (CubeIDE 2.1.0, GCC 14.3, 0 varovani, audit 95/0/2), neovereno na desce

### L-0192 - Testbench, ktery porovnava pres `!=`, neodhali nezapsanou pamet (X)

- **Datum:** 2026-10-22
- **Oblast:** FPGA simulace (`sim/tb_phy_v3.sv`), obecne vsechny testbenche, ktere ctou RAM
- **Symptom:** mutace PHY v3 „RX RAM se nestrida" testem PROSLA - test mel overit, ze v druhe polovine RX RAM zustal predchozi ramec.
- **Pricina:** v mutantovi se druha polovina nikdy nezapsala a obsahovala `X`; `if (x != ocekavano)` je pro `X` neurcite (ne pravda), chyba se nezapocitala. Druha past tehoz testu: `force sig = vyraz` Icarus vyhodnoti jen jednou (varovani `procedural continuous assignments are not yet fully supported`).
- **Oprava:** kontroly RAM a MISO pres `!==` (X i Z se pocitaji jako rozdil); cteni druhe poloviny primo z pameti modelu misto `force`.
- **Pravidlo:** (a) v testbenchi porovnavat `!==` / `===`, kdykoli muze hodnota byt neinicializovana; (b) KAZDY novy testbench projet mutacemi (`sim/mut_phy_v3.py` je vzor) - test, ktery mutaci neshodi, nemeri to, co tvrdi.
- **Detekce:** `python sim/mut_phy_v3.py <koren modulu> <vystup>` -> vsechny mutace „ZACHYCENO".
- **Commit:** feat(fpga) PHY v3
- **Stav:** aktivni

### L-0193 - Vrstva oken testovana jen v pasmu, kde mel kazdy segment hranu

- **Datum:** 2026-10-22
- **Oblast:** STM `fpga_v3.c` (`fpga_v3_grp_feed`, protokol v3 segmenty -> okno), host testy obecne
- **Symptom:** v0.34.0 pri periode > L (25 ms, tedy pod ~40 Hz) nevzniklo ZADNE okno - prazdny segment (N = 0) se bral jako ztrata signalu. Host test (100 Hz .. 30 MHz) i 11 mutaci proslo; chyba se nasla az revizi rozsahu pristroje.
- **Pricina:** scenare testu byly odvozene z implementace (co autor napsal), ne z rozsahu pristroje ve specifikaci (kap. 7: ztrata signalu = 3 ocekavane periody). Mutace overuji jen to, co testy pokryvaji - chybejici scenar neodhali. (Pri vyvoji v0.34.0 mutace naopak nasly 6 slabych testu: poskozeny zaznam kanalu B, prazdny blok 2, ACK za hlavu kruhu, znacka bloku, mezera, absolutni tc0.)
- **Oprava:** v0.34.1 - prazdny segment okno prodlouzi, okno konci prvni hranou po K segmentech, ztrata = mezera > 3 periody, nejvys 2,5 s; scenare 33 / 10 / 1 / 0,5 / 0,3 Hz a vypadek 5 MHz / 1 Hz; 4 nove mutace (15/15).
- **Pravidlo:** (a) seznam scenaru testu odvodit ze SPECIFIKACE a rozsahu pristroje, ne z kodu; (b) u kazde vrstvy, ktera deli cas na segmenty/okna, testovat periodu << L, ~ L, > L a nad limitem ztraty, vypadek kratsi i delsi nez limit; (c) mutace doplnit o mutaci kazde nove podminky (zde 3 periody, strop, `k >= K`).
- **Detekce:** `tests/host/t_fpga_v3.c` blok „nizke kmitocty" + „vypadek signalu"; `python tests/host/mut_fpga_v3.py <vystup>` -> vsechny zachyceny.
- **Commit:** fix(firmware) v0.34.1
- **Stav:** aktivni, prelozeno na macOS (GCC 14.3, 0 varovani, audit 96/0/2), modul do driveru nezapojen

### L-0194 - Simulace celeho topu bezely bez jemneho casu TDC (zaporny posun v AUTOCAL)

- **Datum:** 2026-10-22
- **Oblast:** FPGA simulace (`src/tdc.v` cesta AUTOCAL, jen simulace), testbenche celeho topu (`tb_top_regr`, `tb_top_chan`, `tb_top_selfchk`, `tb_close2`, `tb_n1`)
- **Symptom:** prvni simulace v3 merila kanal A s 10 MHz x (1 + 813 ppb) PRESNE 10 MHz (chyba = 813 ppb), znacky mely jemnou cast vzdy 0 (`lut_q3 = 0x4000`).
- **Pricina:** `xsh = xv >> (CAL_LOG2 - 13)`; pri SIM_TOP je CAL_LOG2 = 12 -> posun -1. Verilog bere pravy operand posunu jako cislo BEZ znamenka -> obrovsky posun -> 0 -> tabulka bez jemneho casu. Testy v2 to neodhalily: kontrolovaly regresi / kmitocet na 1e-6, kam se hrube znacky s fazovym posunem vejdou.
- **Oprava:** `xsh = (CAL_LOG2 >= 13) ? (xv >> (CAL_LOG2 - 13)) : (xv << (13 - CAL_LOG2))`. Syntezy se netyka (AUTOCAL = 0, tabulku pocita STM). Regrese v2 po oprave 29/29 PASS, `topregr` ted dvoubodove 0,00 ppm misto -0,016.
- **Pravidlo:** (a) posun o (parametr - konstanta) vzdy chranit podminkou nebo `$clog2`-stylem, nikdy nespolehat na znamenko; (b) test casovani musi umet rozlisit HRUBY cas od jemneho: aspon jedna kontrola s mezi pod 1 takt / delka okna (zde 10 ns / 2,5 ms = 4e-6) nebo kontrola znacky proti skutecnemu casu hrany.
- **Detekce:** `sim/tb_top_v3.sv` kontrola „T0 = cas hrany X0" (mez 300 ps).
- **Commit:** fix(fpga) tdc.v AUTOCAL
- **Stav:** aktivni

### L-0195 - Stridava kalibrace: potvrzeni tabulky musi nest masku kanalu

- **Datum:** 2026-10-22
- **Oblast:** FPGA v3 (`top_v3.v`, `app_v3.v`, SET_CONFIG 0x06), STM `tdccal_run_v3`
- **Symptom:** `tb_top_v3` rezim 2: po potvrzeni tabulky kanalu A se platnym stal i kanal B s NOMINALNI tabulkou (cekal na svou, test pak vyprsel).
- **Pricina:** v2 kalibruje oba kanaly naraz a potvrzeni (`lw_commit`) jde do obou. Ve v3 se sber stridá -- kdyz STM zapisuje A, B uz muze cekat se svym histogramem; spolecne potvrzeni ho aktivuje bez tabulky.
- **Oprava:** SET_CONFIG 0x06 = 1 nese v p2 masku kanalu (bit0 A, bit1 B, 0 = oba jako v2); FPGA potvrzeni maskuje, STM posila jen kanal, ktery zapsal.
- **Pravidlo:** kdyz se sdileny prikaz (dosud „pro vsechny") zacne pouzivat po castech, musi nest adresata; zkontrolovat vsechny prikazy, ktere v puvodnim navrhu predpokladaly soucasny stav vsech kanalu.
- **Detekce:** `tb_top_v3` rezim 2: „potvrzeni A nepotvrdi cekajici B"; mutace „potvrzeni tabulky bez masky".
- **Commit:** feat(fpga) bitstream v3
- **Stav:** aktivni

### L-0196 - LUT_SUM nepozna chybny bit kanalu u cele tabulky

- **Datum:** 2026-10-22
- **Oblast:** kontrola zapisu tabulky TDC (v2 i v3: `lut_sum`, `fpga_tdccal_chk`)
- **Symptom:** testbench posilal polozky kanalu B s bitem kanalu 0 (vlastni chyba sirky vyrazu v SV); FPGA zapisovalo do A (uz neceka -> zahodilo) a LUT_CNT i LUT_SUM presto SEDELY.
- **Pricina:** clen souctu je `{kanal, adresa, 5'b0} ^ neg` -- bit kanalu je bit 14, chybny bit zmeni soucet o 2^14 a 512 polozek x 2^14 = 2^23 = 0 (mod 2^16). Kontrolni soucet tedy chybu kanalu u tabulky s poctem polozek delitelnym 4 principialne nevidi.
- **Oprava:** v3 hlavicka nese LUT_CNT_B (polozky s bitem B, bajty 122..123), STM ho overuje (`h.lut_cnt_b == (ch ? nc : 0)`). v2 zustava (zapisuje oba kanaly, vzorec je soucasti FW 0x0427).
- **Pravidlo:** u kontrolniho souctu overit, ze kazdy bit vstupu meni vysledek i pri opakovani N-krat (soucet mod 2^k: bit b se „vynuluje" po 2^(k-b) opakovanich); pole, ktere je u vsech polozek stejne (kanal), kontrolovat zvlast poctem.
- **Detekce:** `tb_top_v3` rezim 2 „LUT_CNT_B (bit kanalu polozek)".
- **Commit:** feat(fpga) bitstream v3 / feat(firmware) v0.35.0
- **Stav:** aktivni

### L-0197 - Meze testu podle fyziky, ne pevne cislo; posun indexu pozna jen kontrola proti pravde

- **Datum:** 2026-10-22
- **Oblast:** `sim/tb_top_v3.sv` (obecne testy mereni casu)
- **Symptom:** (1) pevna mez 1e-6 hlasila „chybny kmitocet" u segmentu, ktere maskovani pri CS zkratilo na par znacek tesne u sebe (odstup 1,8 us -> 45 ps sum = 2,5e-5) -- chyba nebyla v RTL; (2) dvoubodovy kmitocet ani SX neodhali posun indexu i znacky o stejnou hranu.
- **Pricina:** chyba odhadu kmitoctu je sigma_znacky / odstup (2pt) a sigma / rozpeti znacek (sklon), ne konstanta; konzistentni posun obou souradnic se v rozdilech vyrusi.
- **Oprava:** meze `2e-7 + 300 ps / dT` a `2e-6 + 120 ps / rozpeti`; nova kontrola T0 zaznamu proti skutecnemu casu hrany X0 z generatoru (mez 300 ps). Pred zavrhnutim RTL overeno ladicim porovnanim vsech 25 409 znacek s pravdou (max 122 ps).
- **Pravidlo:** (a) mez testu odvodit z modelu sumu a delky intervalu; (b) u kazde dvojice (index, cas) mit aspon jednu kontrolu absolutni hodnoty proti pravde, ne jen rozdilu; (c) pri podezreni na RTL nejdriv porovnat jednotlive udalosti s pravdou (ladici vypis), az pak menit navrh.
- **Detekce:** mutace „FIFO: index o hranu vedle" (`sim/mut_top_v3.py`).
- **Commit:** feat(fpga) bitstream v3
- **Stav:** aktivni

### L-0198 - Znacka v taktu clear: konec „zmrazeni" a vynulovani souctu ve stejnem taktu

- **Datum:** 2026-10-23
- **Oblast:** FPGA v3 (`src/v3_core.v` `seg_chan3` + `v3_ring`), obecne kazda dvojice „neprictat" / „vynulovat soucty"
- **Symptom:** po pridani registru do cesty znacky (TSPIPE, `ts_valid` T+9 -> T+10) `topv3` hlasil `SX = N(N-1)/2` v prvnich 9 ramcich (60 chyb); T0, N, dvoubodovy kmitocet i sklon v mezich.
- **Pricina:** `v3_ring` nastavi `clear` a shodi `freeze` v TEMZE taktu, takze v taktu `clear` je `freeze = 0`. Znacka, ktera prijde prave tehdy, projde `take`: `v1 = take & ~first` (first jeste stary) pricte `xm - x0`, `tm - t0` se STARYM x0/t0 do souctu, ktere `clear` prave vynuloval, a `clear` vynuluje i N -> SX = N(N-1)/2 + N_predchozi - 1. Chyba je v RTL od FW 0x0500 (s testem nize na RTL z HEAD, T+9: 11 chyb, rozdil presne 4324 = N_predchozi - 1). Test ji nevidel, protoze oba generatory mely na segment CELY pocet period (A 10 MHz + 813 ppb, B 17,3 MHz x 250 us = 4325): faze znacek vuci hranici segmentu za beh skoro stala a pri T+9 do taktu `clear` nepadla. Na desce pri libovolnem kmitoctu postihne ~ (znacek za takt) segmentu, pri 10 MHz ~10 %. Kmitocet se nezkresli (chybny clen lezi na primce t = P x), zaznam je ale vnitrne nekonzistentni.
- **Oprava:** `take = pop & ~hold & ~freeze & ~clear` (`src/v3_core.v`, seg_chan3): znacka v taktu clear se zahodi jako znacky behem freeze. Test: kanal B -DTB_FB=17.300173e6 (necely pocet period, faze se posouva ~0,25 taktu za segment; po TSPIPE 16.100161e6, L-0199) + pocitadlo pokryti „znacka v taktu clear" (chk > 0; B 8x), mutace bez `~clear` v `sim/mut_top_v3.py`. Vedlejsi nalez: B pres vsechny faze TDC prekrocil mez sklonu ST/SX (2 x 60 ps) u segmentu zkraceneho maskovanim (N 164, chyba 1,50e-5 pri mezi 1,47e-5, SX presne N(N-1)/2, stejne s T+9 i T+10) -> mez 2 x 100 ps podle chyby jedne znacky modelu (T0 proti pravde B az 103 ps).
- **Pravidlo:** u kazdeho bloku s fazi „neprictat" a „nulovat" overit takt, kde jedna konci a druha zacina (vcetne rozpracovane roury), a testovat ho stimulem, jehoz faze vuci hranici se za beh posouva, s pocitadlem pokryti toho taktu -- kmitocet s celym poctem period na interval zkousi hranici stale ve stejne fazi.
- **Detekce:** `tb_top_v3` rezim 0 „pokryti: znacka v taktu clear" + „SX = N(N-1)/2"; mutace „znacka v taktu clear se pricte".
- **Commit:** fix(fpga) v3: znacka v taktu clear
- **Stav:** aktivni

### L-0199 - Kriticka cesta s velkym fan-out: dlouhy usek byva LUT -> FF, oprava je registr ZA logikou

- **Datum:** 2026-10-23
- **Oblast:** FPGA rozlozeni (`src/tdc.v` `ts_ps`, obecne kazdy vyber/multiplexer siroke sbernice jednim bitem)
- **Symptom:** FW 0x0427: 36 z 50 nejhorsich cest z jednoho FF `u_tdca/lut_q3[15]` (fan-out 34), rezerva +0,215 ns, 82 % zpozdeni vedeni. Audit 2026-10-22 navrhl (O1) vybirat o takt driv z `lut_q2`.
- **Pricina:** placer dal 34 multiplexeru k jejich zdroji (u BSRAM, R15C8) a registry `ts_ps` ke spotrebitelum (R13C34): dlouhe vedeni (4,2 ns) je az MEZI LUT a FF, prvni usek (2,8 ns) je fan-out. Replikace zdroje zkrati jen prvni usek. A `lut_q2` je vystupni registr BSRAM (synteza ho vtahne do bloku, tCO ~3,5 ns) -- vyber z nej by dal tCO BSRAM + fan-out 34 v jednom taktu, tedy presun problemu.
- **Oprava:** `tdc.v` parametr `TSPIPE` (v3 = 1): `ts_hq/ts_lq` se plni ve stejnem taktu jako drive `ts_ps` (stejne hodnoty posuvneho registru `tr*`), `ts_ps` az o takt pozdeji -> vedeni pres cip dostane vlastni takt bez logiky; +49 FF na kanal, `ts_valid` T+10. v2 zustava 0 (index se tam paruje s posledni spustenou hranou v okamziku `ts_valid`).
- **Vedlejsi nalez:** s T+10 a kanalem B 17,3 MHz (rozestup hran 5/6 taktu) prisel index dalsi hrany VZDY pred znackou predchozi -> mutace „FIFO hloubky 1" parovala se stalym posunem o hranu, ktery se v rozdilech vyrusi (ekvivalentni mutant, NEZACHYCENO). Pri T+9 se posun stridal (rozestup 6 = soucasny push i pop). Test proto prelazen na B 16,100161 MHz (rozestup 6/7 -> FIFO hloubky 1 se rozejde, zachyceno).
- **Pravidlo:** pred navrhem zmeny kriticke cesty precist z timing reportu polohu KAZDEHO uzlu (kde je LUT, kde FF) a fyzicky typ registru (BSRAM/DSP vystup = vysoke tCO); pridany registr patri za posledni logiku pred dlouhym usekem, a po kazde zmene latence pustit znovu VSECHNY mutace -- zmena casovani muze z mutace udelat ekvivalentni (test ztrati silu, ale dal prochazi).
- **Detekce:** `tools/floorplan_svg.py` (mapa + zdroje nejhorsich cest z `.tr` po kazdem P&R); `docs/audit/2026-10-23_rozlozeni-fpga.md` kap. 3.
- **Commit:** perf(fpga) v3: TSPIPE + registrovany konec citacu

### L-0200 - Cas dvou kanalu na jedne ose rozbalovat spolecne, ne po kanalech

- **Datum:** 2026-10-23
- **Oblast:** `fpga_v3_ev_from_rec` (jednorazove deje, protokol v3)
- **Symptom:** selftest krok 11 (interval A->B pres hranici segmentu, B az po preteceni T0 2^48) FAIL: interval o ~2^48 jednotek (171,8 s) vedle.
- **Pricina:** rozbaleni T0 meli oba kanaly zvlast; prvni zaznam kazdeho kanalu zacal od surove hodnoty, takze kanal, jehoz prvni zaznam prisel az po preteceni, mel posun o nasobek 2^48 proti druhemu. A, B i 1PPS jsou ale jedna osa FPGA.
- **Oprava:** jeden stav rozbaleni pro oba kanaly: rozdil proti posledni znacce libovolneho kanalu se znamenkem (48 b; B muze byt drive nez A) a pocet preteceni z uplynuleho casu STM (`fpga_v3.c` `fpga_v3_ev_from_rec`); index X zustal po kanalech.
- **Pravidlo:** hodnoty na spolecne ose (cas A, B, 1PPS) rozbalovat jednim stavem a rozdily brat se znamenkem; vlastni stav po kanalech jen u nezavislych citacu (index hrany).
- **Detekce:** selftest krok 11 (`fpga v3 test`), mutace „deje: rozbaleni bez casu STM" (`tests/host/mut_fpga_v3.py`).
- **Commit:** `b2dd741`
- **Stav:** aktivni

### L-0201 - Rozptyl ze souctu sum(v^2) - n*m^2 pri velke stredni hodnote lze

- **Datum:** 2026-10-23
- **Oblast:** `fpga_v3_stat_*` (statistika intervalu)
- **Symptom:** host test 2 Hz, B = A + 5 ms: sigma 95 ps misto ~45 ps, prestoze jednotlive odchylky 60 intervalu byly v +-113 ps (ladici vypis).
- **Pricina:** intervaly 5e9 ps -> v^2 = 2,5e19, soucet 1,5e21; double ma na tom miste krok ~3e5 ps^2 a rozptyl (~2e3 ps^2) je rozdil dvou cisel o 18 radu vetsich (katastroficke odecitani).
- **Oprava:** Welforduv algoritmus (prubezna stredni hodnota a soucet ctvercu odchylek), `fpga_v3.c` `fpga_v3_stat_add`.
- **Pravidlo:** prubezny rozptyl vzdy Welfordem nebo od posunuteho pocatku (prvni hodnota), nikdy sum(v^2) - n*m^2, kdyz je stredni hodnota o rady vetsi nez sigma. Zkontrolovano: `meas_present.c` uz centruje (F-0169), `app_gpsdo.c` Welford, `screen_main.c` `nz_slope` jen log10 (male hodnoty) -- bez dalsiho nalezu.
- **Detekce:** host test scenar „2 Hz, B = A + 5 ms, 200 s ticho" (sigma < 80 ps).
- **Commit:** `b2dd741`
- **Stav:** aktivni

### L-0202 - Testbench: po zmene toho, co kanal meri, nova reference absolutnich kontrol

- **Datum:** 2026-10-23
- **Oblast:** `sim/tb_top_v3.sv` rezim 3 (zdroj kanalu B)
- **Symptom:** po prepnuti B z pinu A invertovaneho na neinvertovany hlasila kontrola „T0 = cas hrany X0" konstantni odchylku -37 344 ps u kazdeho zaznamu B.
- **Pricina:** kontrola porovnava T0 s referenci zachycenou z prvniho platneho zaznamu kanalu; neplatne zaznamy po prepnuti se preskocily, ale reference zustala ze sestupnych hran -- posun o sirku pulzu 37,3 ns je fyzika, ne chyba RTL.
- **Oprava:** po kazdem prepnuti zdroje B (preskoceni neplatnych zaznamu) nova reference T0 i dvoubodoveho kanalu B (`t0have[1] = 0; have[1] = 0`).
- **Pravidlo:** kdyz test zmeni, co kanal meri, restartovat reference absolutnich kontrol; konstantni odchylku nejdriv porovnat s fyzikalni velicinou scenare (tady sirka pulzu), az pak podezirat RTL (srov. L-0197).
- **Detekce:** `tb_top_v3` rezim 3 (`sim/run.ps1 topv3pw`).
- **Commit:** `9cbc302`
- **Stav:** aktivni

### L-0203 - Mutace prezije, kdyz scenar danou vetev nikdy nespusti

- **Datum:** 2026-10-23
- **Oblast:** `tests/host/t_fpga_v3.c` (parovani start/stop)
- **Symptom:** mutace „bez serazeni v case", „stop i pred startem" a „souhrn B pres start" prezily vsechny scenare nad emulatorem.
- **Pricina:** emulator ma pri periode deleni 25 ms stalou fazi, takze pripady „B pred A v temze segmentu", „B soucasne se startem" a „souhrn B pres start" nenastaly; u zpozdeni 0 test intervaly nekontroloval vubec.
- **Oprava:** kroky se znamymi casy pro `fpga_v3_ab_feed` a `fpga_v3_pr_rec` (razeni, stop jen po startu, souhrn B pres start, CS, prepnuti zdroje B), scenar 7 Hz B = A + 140 ms, kontrola ztracenych startu a intervalu i pri zpozdeni 0; lepidlo ovladace presunuto do `fpga_v3_pr_rec`, aby testovany kod byl ten, ktery bezi.
- **Pravidlo:** kazda vetev stavoveho automatu ma mit aspon jeden deterministicky krok se znamym vstupem; nahodne a emulovane scenare vzacne vetve nezarucuji -- ktere chybi, ukazou mutace. Kod, ktery se testuje, nesmi byt kopie kodu, ktery bezi.
- **Detekce:** `python3 tests/host/mut_fpga_v3.py <vystup>` -> 24/24 zachyceno.
- **Commit:** `b2dd741`
- **Stav:** aktivni

### L-0204 - Nove pole struktury plnene jen jednou vetvi: druha vetev nese odpad

- **Datum:** 2026-10-23
- **Oblast:** STM `fpga_freq.c` (`fpga_meas_t` z v2 `parse_data` i z v3 `s3_hdr_to_meas`), obecne kazda struktura plnena vic parsery
- **Symptom:** (zachyceno pri psani v0.37.0, pred commitem) nova pole `pps_frac/pps_fine/pps_epoch` plnila jen vetev v3; v2 `parse_data` strukturu NENULUJE (volajici maji lokalni `fpga_meas_t` na zasobniku) -> v2 by v `pps_feed` porovnaval nahodne hodnoty a 1PPS by se "trhal" nebo vyhlasoval s nahodnym zlomkem (GPSDO).
- **Pricina:** dva parsery plni tutez strukturu po polich; v3 dela `memset`, v2 ne. Pridane pole tak v jedne vetvi zustane neinicializovane, a prekladac nic nehlasi.
- **Oprava:** `parse_data` nastavuje nova pole vyslovne na 0 (v2 = jen hruby cas) (`fpga_freq.c`, za `m->pps_ts`).
- **Pravidlo:** pri pridani pole do struktury, kterou plni vic funkci, projit VSECHNY plnice (grep nazvu struktury) a pole nastavit v kazde; lepe nulovat strukturu na zacatku parseru.
- **Detekce:** checklist pri zmene `fpga_meas_t` / `fpga_pps_t`: grep `fpga_meas_t` a `parse_data(` -- kazdy plnic musi nove pole nastavit.
- **Commit:** feat(firmware) v0.37.0
- **Stav:** aktivni

### L-0205 - Pravda testbenche musi pocitat se zaokrouhlenim zpozdeni simulatoru

- **Datum:** 2026-10-23
- **Oblast:** `sim/tb_top_v3.sv` (generatory kanalu A/B), obecne kazdy testbench s `#(real)` a kontrolou proti vypoctene pravde
- **Symptom:** rezim 4 (B 143,3 MHz s predelickou /8) hlasil `T0 = cas hrany X0` -351, -362, -314 ps v po sobe jdoucich zaznamech (rostouci drift); RTL v poradku.
- **Pricina:** generator dela `#(HB)` s realnou pulperiodou, Icarus kazde zpozdeni zaokrouhli na presnost modulu (1 fs). Pravda testu (`PB = 2 * HB`) byla nezaokrouhlena -> rozdil az 0,7 fs na periodu, za stovky tisic period stovky ps. Komentar u vypoctu periody to dokonce predpokladal ("2 x zaokrouhlena pulperioda"), kod ne.
- **Oprava:** `HA/HB = round(0.5e12 / f * 1000) / 1000` (ps zaokrouhlene na fs) -- generator i pravda pouzivaji totez. `topv3` T0 A max 181 -> 102 ps.
- **Pravidlo:** hodnotu, kterou simulator dostane jako zpozdeni, nejdriv zaokrouhlit na jeho presnost a pravdu pocitat z te zaokrouhlene; komentar, ktery neco tvrdi o kodu, overit proti kodu.
- **Detekce:** `topv3presc` (rezim 4, vysoky kmitocet = hodne period) -- drift se ukaze v T0 do 300 ps.
- **Commit:** feat(fpga) FW 0x0503
- **Stav:** aktivni

### L-0206 - Slozene pole (concatenation) kratsi nez cil: Verilog doplni nuly bez varovani

- **Datum:** 2026-10-23
- **Oblast:** FPGA `src/v3_core.v` `psram_hb3` (`mk_ca`), obecne kazde `{...}` prirazene do sirsiho registru / funkce
- **Symptom:** (zachyceno samostatnym testem radice PSRAM pred zapojenim) kalibrace latence vsech 8 pokusu selhala, model PSRAM nedostal zadne cteni.
- **Pricina:** prikaz/adresa HyperBus `{rd, AS, burst, 8'd0, a[21:4], 13'd0, a[3:1]}` mel 45 bitu misto 48 (vyplni horni adresy 11 b, ne 8); funkce vraci 48 b, takze se horni 3 bity doplnily nulou -> bit 47 (R/W#) byl vzdy 0 a vsechno se provedlo jako zapis. Icarus ani Gowin na kratsi spojeni nevaruji.
- **Oprava:** `{rd, 1'b0, 1'b1, 11'd0, a[21:4], 13'd0, a[3:1]}` s komentarem souctu sirek (1+1+1+11+18+13+3 = 48).
- **Pravidlo:** u kazdeho spojeni pro pevny format (prikazy, hlavicky, slova zaznamu) napsat do komentare soucet sirek a overit ho; novy radic periferie nejdriv samostatnym testem proti modelu, az pak do celeho topu.
- **Detekce:** samostatny test radice (scratch `tb_psram_unit.sv`; v repu rezim 5 `topv3deep`), mutace "PSRAM: pevna latence zapisu".
- **Commit:** feat(fpga) FW 0x0505
- **Stav:** aktivni

---

### L-0207 - Model periferie musi modelovat i "klidove" stavy signalu, ne jen datovou fazi

- **Datum:** 2026-10-23
- **Oblast:** FPGA `psram_hb3` + `sim/tb_top_v3.sv` `hb_psram_model` (audit F-0262, S1)
- **Symptom:** (audit) cteni PSRAM by na kremiku bylo posunute o bajt; v simulaci vse PASS.
- **Pricina:** HyperBus ridi RWDS uz od CS# = 0 jako priznak latence (pevna dvojita latence = 1) a pred daty ho stahne do 0. Radic bral bajt pri kazde zmene RWDS od konce CA, takze sestupna hrana priznaku byla "prvni bajt". Model mel RWDS behem CA v Z -> zadna zmena, chyba neviditelna.
- **Oprava:** bajty az po prvnim vzorku RWDS = 0 v datove fazi (`rarm`); model ridi RWDS jako PSRAM (RW_CA pres CA a latenci, 0 bajt pred daty).
- **Pravidlo:** u modelu rozhrani projit kazdy signal v KAZDE fazi transakce (CA, latence, data, konec) a nastavit ho na nejhorsi pripustnou hodnotu podle specifikace, ne na Z / 0. Opraveny model pustit i proti puvodnimu RTL -- musi selhat.
- **Detekce:** `topv3deep` s novym modelem na puvodnim RTL FAIL (kalibrace W0+3); mutace "PSRAM: cteni bez zbrojeni RWDS". Pozor: po oprave byla mutace "cteni bez RWDS" s modelem ekvivalentni (model dava bajt za takt) -- model ma pauzu RWDS uprostred davky (`PSTALL`, `ecfcb6c`); po kazde zmene modelu pustit vsechny mutace dane periferie znovu.
- **Commit:** fix(fpga) `cb14596`
- **Stav:** aktivni

---

### L-0208 - Rozsah samokalibrace = cely pripustny rozsah x granularita parametru

- **Datum:** 2026-10-23
- **Oblast:** FPGA `psram_hb3` kalibrace latence zapisu (audit F-0263)
- **Symptom:** (audit) 8 zkousenych latenci 16..23 bajtu, z nich sedela jen kazda druha.
- **Pricina:** latence HyperBus je v celych taktech CK = 2 bajty; 8 pokusu pokrylo 4 skutecne latence a vychozi pevna dvojita latence (~22 / 26 B) lezela na hranici nebo mimo.
- **Oprava:** W0 = 4, 32 pokusu (4..35 B = latence 3..7 taktu jednoducha i dvojita).
- **Pravidlo:** pred volbou rozsahu kalibrace spocitat, v jakych krocich se parametr skutecne meni (zde 2 B) a jaky je jeho rozsah pro vsechny pripustne konfigurace cipu; pocet pokusu = rozsah / krok, ne "8 je dost".
- **Detekce:** samostatny test radice s latenci 26 -> bad; `topv3deep` (W0 + 16).
- **Commit:** fix(fpga) `74456e9`
- **Stav:** aktivni

---

### L-0209 - Zdrojove synchronni rozhrani bez PLL: fazi vzorkovani kalibrovat, model s oknem neplatnosti

- **Datum:** 2026-10-23
- **Oblast:** FPGA `psram_hb3` cteni (audit F-0264)
- **Symptom:** (audit) vzorek na nabezne hrane clk lezi 5 ns po hrane CK; pri tCKD ~5 ns padne do zmeny dat.
- **Pricina:** pevna faze vzorkovani; model menil vystup skokem (bez okna neplatnosti), takze simulace nemohla zadnou fazi vyloucit.
- **Oprava:** vzorky i na sestupne hrane clk, kalibrace zkousi obe faze (hlavicka 129 bit5); model s TCKD_MIN < TCKD dava X, test `topv3deep2` vyzaduje fazi 1.
- **Pravidlo:** u kazdeho vstupu z vnejsiho obvodu, jehoz zpozdeni zavisi na cipu / teplote, bud kalibrovana faze, nebo doloziteny casovy rozpocet; model vystupu s oknem X (min..max zpozdeni), jinak test nic nedokazuje.
- **Detekce:** `topv3deep2`; mutace "PSRAM: jen faze cteni 0".
- **Commit:** fix(fpga) `02ae245`
- **Stav:** aktivni

---

### L-0210 - CDC: prepinaci bit nesmi menit stav v tomtez taktu jako jeho parametry

- **Datum:** 2026-10-23
- **Oblast:** FPGA `app_v3.v` T_RAW a SET_CONFIG 0x02 -> 100 MHz (audit F-0265)
- **Symptom:** (audit, kontrola kodu) parametry pres 2 FF, prepinac pres 3 FF, oboji zmenene v jednom taktu 10 MHz.
- **Pricina:** staci, aby metastabilita posunula jeden bit parametru o takt proti prepinaci, a cil prevezme starou hodnotu. LUT_WRITE uz zpozdeni mel (`lw_dly`), novejsi prikazy ho neprevzaly.
- **Oprava:** `raw_dly` / `cal_dly`: prepinac 3 takty po parametrech.
- **Pravidlo:** kazdy novy prikaz s prepinacim bitem napsat podle vzoru `lw_dly` (parametry nejdriv, prepinac o >= 2 takty zdrojove domeny pozdeji); pri review hledat `_tgl <= ~` ve stejnem bloku jako prirazeni parametru.
- **Detekce:** kontrola kodu (simulace metastabilitu nemodeluje).
- **Commit:** fix(fpga) `99e7404`
- **Stav:** aktivni

---

### L-0211 - Prikaz bez potvrzeni prijeti: volajici nepozna odmitnuti a vezme stara data

- **Datum:** 2026-10-23
- **Oblast:** FPGA `raw_cap3` (F-0266) + STM `fpga_freq_v3_raw_deep` / `fpga_freq_v3_raw` (F-0272)
- **Symptom:** (audit) odmitnuty start hlubokeho zaznamu (bezi jiny) -> STM precetlo DEEP_N a okna predchoziho zaznamu, klidne jineho kanalu; rezim 0 behem zaznamu prepnul kanal a psal do okna.
- **Pricina:** rezimy sdileneho prostredku (BSRAM, `ch`) se nevylucovaly a FPGA odmitnuti nijak nehlasila; STM cekalo jen na stavove bity, ktere splni i stary stav (status v ramci je navic o transakci starsi).
- **Oprava:** FPGA: vyluka rezimu, STOP, bajt 127 = pocet prijatych prikazu (Gray) + kanal; STM: `s3_raw_cmd` (pred a po), odmitnuto = -5, STOP predchoziho, kontrola kanalu.
- **Pravidlo:** kazdy asynchronni prikaz, po kterem volajici cte vysledek, musi mit potvrzeni prijeti, ktere se odmitnutim NEZMENI (pocitadlo, sekvence); "ceka na bit hotovo" nestaci. Sdileny prostredek: vyluka v HW, ne spolehani na poradi prikazu.
- **Detekce:** `topv3deep` (rezim 0 a 2 behem zaznamu odmitnuty, STOP, bajt 127); host test (13); mutace "RAW: rezim 0 bez vyluky", "bez STOP", "bajt 127 se necte".
- **Commit:** fix(fpga) `a6f78fd`, fix(firmware) `5a2c727`
- **Stav:** aktivni

---

### L-0212 - Odvozena BSRAM: jeden zapisovy port, ne dve prirazeni s ruznou adresou

- **Datum:** 2026-10-23
- **Oblast:** FPGA `raw_cap3` `mem` (audit F-0267)
- **Symptom:** (audit) dve mista zapisu do `mem` (znacka rezimu 0, bajty okna).
- **Pricina:** syntetizator odvozuje BSRAM ze sablony (jedno prirazeni pod podminkou); dve prirazeni s ruznou adresou nemusi poznat a 24 kbit by skoncilo v logice.
- **Oprava:** jeden `always` se zapisem `mem[m_w2 ? widx : n]`, vyluku zarucuje F-0266.
- **Pravidlo:** pole, ktere ma byt v BSRAM, zapisovat z jednoho mista (mux adresy a dat zvlast); po P&R zkontrolovat zpravu o zdrojich (pocet BSRAM).
- **Detekce:** `topv3`, `topv3deep` beze zmeny; P&R.
- **Commit:** fix(fpga) `c67da4b`
- **Stav:** aktivni

---

### L-0213 - Konec stavoveho automatu nesmi cekat na data, ktera nemusi prijit

- **Datum:** 2026-10-23
- **Oblast:** FPGA `raw_cap3` nacitani okna (audit F-0268)
- **Symptom:** (audit) okno koncilo az pri 512 znackach; pri timeoutu cteni PSRAM zustal `wl` = 1 navzdy a vsechny dalsi prikazy se odmitaly.
- **Pricina:** konec vazany na pocet prijatych dat misto na dokonceni vlastnich pozadavku; timeout v nizsi vrstve chybu nepredal vys.
- **Oprava:** konec po dokonceni 24. davky, chybne okno = DEEP_ST bit7 a 0 znacek.
- **Pravidlo:** u kazdeho cekani na data z vnejsiho obvodu: konec podle vlastnich pozadavku (nebo timeout) + priznak chyby; test s "mrtvym" modelem (`dead`).
- **Detekce:** `topv3deep` (model dead = 1); mutace "PSRAM: okno ceka na vsechny bajty".
- **Commit:** fix(fpga) `ba6ae59`
- **Stav:** aktivni

---

### L-0214 - Metadata zaznamu musi sledovat skutecnou cestu signalu (obchvat predelicky)

- **Datum:** 2026-10-23
- **Oblast:** FPGA `top_v3.v` kanal B z pinu A (audit F-0269)
- **Symptom:** (audit) B z pinu A jde mimo `pin_cnt3`, ale zaznam B nesl log2 P_B.
- **Pricina:** pri pridani obchvatu (SET_CONFIG 0x09) se znacka P nepripojila na tutez volbu; STM chybu maskovalo az se zpozdenim (stahne P_B po ozvene).
- **Oprava:** `pr_b_eff = bs2[0] ? 0 : pr_b` do `seg_chan3` B.
- **Pravidlo:** kazda volba cesty signalu (mux zdroje, obchvat) musi ridit i vsechna metadata, ktera popisuji, CO se meri (P, kanal, polarita); test kombinace "obchvat + nenulove P".
- **Detekce:** `topv3pw` s P_B = 4 (puvodni RTL FAIL); mutace "B z pinu A: znacka P_B".
- **Commit:** fix(fpga) `abd3856`
- **Stav:** aktivni

---

### L-0215 - Ticha ztrata dat: kazde zahozeni musi nechat priznak

- **Datum:** 2026-10-23
- **Oblast:** FPGA `raw_cap3` (F-0270) + STM UART (F-0272)
- **Symptom:** (audit) RAW pri hold a pri masce CS (vychozi zapnuta) obsahoval rozdil 2P bez stopy.
- **Pricina:** maska a hold zastavuji TDC (`want` = 0), takze znacka vubec nevznikne; zaznam o tom nevedel.
- **Oprava:** DEEP_ST bit6 (behem zaznamu kanal aspon chvili neznackoval), STM hlasi "mezery ... `fpga diag mask off`"; dotazy behem zaznamu po 100 ms.
- **Pravidlo:** kdyz HW cast dat vedome nezaznamena (maska, hold, preteceni), musi to byt v datech nebo stavu videt; u souvislych zaznamu explicitne "souvisly / s mezerami".
- **Detekce:** `topv3deep` (maska -> bit6, bez masky 0); mutace "RAW: bez priznaku mezer".
- **Commit:** fix(fpga) `019a45a`, fix(firmware) `5a2c727`
- **Stav:** aktivni

---

### L-0216 - SDC: neodkazovat na registry, ktere syntaza muze vstrcit do tvrdeho bloku

- **Datum:** 2026-10-23
- **Oblast:** FPGA `timing_v3.sdc` (audit F-0271)
- **Symptom:** (audit) `report_timing -to [get_regs {u_sa/g_dsp.sx_d* ...}]` u akumulatoru se `syn_dspstyle = "dsp"`.
- **Pricina:** v ALU54D je akumulator vcetne registru; registr v logice nemusi existovat a odkaz na neexistujici objekt je v Gowin chyba TA2003 (beh skonci, viz `calm_s`, `src_l`, `qd_f`).
- **Oprava:** radek odstranen, komentar proc.
- **Pravidlo:** v SDC odkazovat jen na registry, ktere v logice existuji za vsech voleb syntezy (ne DSP / BSRAM / IOB registry); po zmene SDC zkontrolovat `pnr.log` na TA2003.
- **Detekce:** kontrola (P&R na Windows).
- **Commit:** fix(fpga) `1f1e44c`
- **Stav:** aktivni

---

### L-0217 - Nulova hodnota senzoru neni "bez odhadu"

- **Datum:** 2026-10-23
- **Oblast:** STM `fpga_v3_range_probe` / `s3_range_ctl` (audit F-0273)
- **Symptom:** (audit) pod 16 kHz zustala predelicka po vysokem kmitoctu na /16 (1-2 hrany na segment).
- **Pricina:** sonda s rozlisenim 16 kHz vraci 0 pro cokoli pod tim; funkce brala f <= 0 jako "neznamo, nemenit".
- **Oprava:** `fpga_v3_range_probe`: probe 0 -> P = 1.
- **Pravidlo:** u kazdeho cisla ze senzoru rozlisit "nameril 0 / pod rozlisenim" od "nemeril"; kdyz to rozhrani nerozlisi, rozhodnout, co je bezpecnejsi, a napsat kompromis do komentare.
- **Detekce:** host test (11) rozsah ze sondy; mutace "rozsah: sonda 0 beze zmeny".
- **Commit:** fix(firmware) `c76f304`
- **Stav:** aktivni

---

### L-0218 - Watchdog kopat jen z hlidaneho tasku

- **Datum:** 2026-10-23
- **Oblast:** STM `fpga_freq_v3_raw_deep` (UartTask) -> `watchdog_kick_fpga` (audit F-0274)
- **Symptom:** (audit) UartTask kopal slot FpgaTasku po kazdem okne hlubokeho zaznamu.
- **Pricina:** kopnuti "pro jistotu" z jineho tasku; FpgaTask behem cteni dal bezi a kope sam.
- **Oprava:** volani odstraneno.
- **Pravidlo:** `watchdog_kick_<task>` volat jen z tela daneho tasku; kdyz dlouha operace blokuje hlidany task, patri do nej (ne do jineho tasku).
- **Detekce:** kontrola kodu.
- **Commit:** fix(firmware) `c891969`
- **Stav:** aktivni

---

### L-0219 - Opakovani L-0204: nove pole fpga_meas_t plnene jen cestou v3

- **Datum:** 2026-10-23
- **Oblast:** STM `fpga_freq.c` `parse_data` (v2) -- `presc_log2` (audit F-0275)
- **Symptom:** (audit) v0.38.0 pridalo `presc_log2`, v2 parser ho nenuloval (struktura se nenuluje).
- **Pricina:** stejna past jako L-0204 (pole 1PPS): pri pridani pole do `fpga_meas_t` se zapomene na druhou vetev.
- **Oprava:** `m->presc_log2 = 0u` v v2.
- **Pravidlo:** pri kazdem novem poli `fpga_meas_t` grep na vsechna mista, kde se struktura plni (`parse_data`, v3 okna, emulator), a pole nastavit vsude; zvazit memset na zacatku `parse_data`.
- **Detekce:** kontrola kodu.
- **Commit:** fix(firmware) `21a6758`
- **Stav:** aktivni

---

### L-0220 - printf s %f / %e / %g ve firmwaru s nano.specs vytiskne prazdne pole

- **Datum:** 2026-10-23
- **Oblast:** STM `freertos_task_uart.c` `fpga v3` (audit F-0276), cely firmware
- **Symptom:** (audit) "sonda %.3f MHz" (v0.38.0).
- **Pricina:** link `--specs=nano.specs` bez `-u _printf_float`; newlib-nano float konverzi preskoci.
- **Oprava:** sonda v kHz celym cislem; pravidlo v `scripts/zakazane_vzory.txt`.
- **Pravidlo:** ve firmwaru zadne %f/%e/%g: formatovat celymi cisly nebo `uart_ps_fmt` / `uart_regr_fmt` / `fmt_fixed` (L-0064).
- **Detekce:** `scripts/check_lessons.sh` (po oprave 0 vyskytu v CM7/CM4).
- **Commit:** fix(firmware) `12f559f`
- **Stav:** aktivni

### L-0221 - CLS se nechová lineárně v registrech: plošné úspory se ztratí v hustějším balení

- **Datum:** 2026-10-23
- **Oblast:** FPGA / odhad plochy (Gowin GW1NR-9C, CLS)
- **Symptom:** v3 (0x0505) 92 % CLS, 77 nezapojených sítí; odebrání 100–270 registrů (jemný čas 1PPS −275 reg, DSP −108 reg) snížilo CLS jen o 15–40.
- **Příčina:** od ~4 450 reg / 5 400 logiky nahoru placer balí hustěji, takže plocha drží ~3 800–3 970 CLS; jen odebrání celé funkce (PSRAM −499 reg → −139 CLS, hlavička −850 reg → −600 CLS) se projeví.
- **Oprava:** studie `docs/audit/2026-10-23_studie-cls-v3.md`; mezi variantami se rozhoduje celým P&R (`tools/abl_cls.py`), ne součtem registrů.
- **Pravidlo:** úsporu plochy ověř celým P&R na kopii zdrojů; odhad z počtu registrů či LUT neber jako výsledek a cíl 85 % počítej v celých funkcích.
- **Detekce:** `python tools/abl_cls.py run <var> ...` a `table`; rozpad po modulech `tools/syn_hier.py`.
- **Commit:** feat(fpga) FW 0x0506
- **Stav:** aktivní

### L-0222 - Rezerva časování 100 MHz kolísá o ±1,5 ns mezi sestaveními téhož zdroje

- **Datum:** 2026-10-23
- **Oblast:** FPGA / P&R (identita bitstreamu `BLD_TIME` je vstup placeru)
- **Symptom:** pět sestavení v3 stejného zdroje: −1,38, −0,35, −0,85, −1,50 a +0,012 ns; 79 % zpoždění je vedení.
- **Příčina:** jiný vstup P&R = jiné rozmístění; nejdelší cesty tvoří vysoký fan-out (`clear` 280, výběr slova 251, `hold_a` 13) a CE s víc podmínkami.
- **Oprava:** snížení fan-outu (kopie `clear_a/b`, `hold_a_s`, jednotkový výběr slova), registrované podmínky (`fok`/`wok`, `dd_r`, `pcf`), dlouhé čítače rozdělené; pro vydání opakovat sestavení branou (do prvního PASS).
- **Pravidlo:** nepovažuj jedno sestavení za reprezentativní; po změně RTL spusť branu nejméně 5×, a cesty s fan-outem > 30 a CE ze 3+ signálů předem rozděl.
- **Detekce:** `tools/fpga_release.py --v3 --build` (opakovaně), `tools/floorplan_svg.py` (zdroje nejhorších cest).
- **Commit:** feat(fpga) FW 0x0506
- **Stav:** aktivní

### L-0223 - SDC: get_regs / get_clocks na IDES8 a odvozené hodiny PLL Gowin odmítá

- **Datum:** 2026-10-23
- **Oblast:** FPGA / timing_v3.sdc
- **Symptom:** `set_false_path ... -to [get_regs {u_ipps/u_ides*}]` → TA2003, `get_clocks {u_ipps/u_pll/CLKOUT.default_gen_clk}` → TA2004, `get_cells` → TA2003; běh končí chybou.
- **Příčina:** IDES8 není registr a odvozené hodiny se při načtení SDC ještě nevytvoří.
- **Oprava:** `set_false_path -from [get_regs {u_ipps/rdy*}]` (zdroj je registr; `rdy` je kvazistatický) – `src/timing_v3.sdc`.
- **Pravidlo:** výjimku pro cestu do primitiva zapiš na zdroj (`-from` registr), ne na cíl.
- **Detekce:** první `gw_sh` běh po úpravě SDC (chyba TA2003/TA2004 v `build_release.log`).
- **Commit:** feat(fpga) FW 0x0506
- **Stav:** aktivní

### L-0224 - Mutační vzory se po refaktoru tiše rozejdou s kódem

- **Datum:** 2026-10-23
- **Oblast:** FPGA / `sim/mut_top_v3.py`
- **Symptom:** po úpravách RTL (jednotkový výběr slova, `f_buf` pryč, ST 54 b, lite RAW) 6 ze 35 vzorů mutací v kódu nebylo nebo bylo dvakrát; mutace by se spustily na nezměněném zdroji a „zachytit" by neměla co.
- **Příčina:** vzory jsou doslovné textové výskyty; refaktor je mění.
- **Oprava:** vzory aktualizovány; ověření „každý vzor v kódu právě jednou" skriptem před během.
- **Pravidlo:** po každé změně RTL zkontroluj, že každý vzor mutace existuje v kódu právě jednou, než mutace pustíš.
- **Detekce:** kontrola z `docs/audit/2026-10-23_studie-cls-v3.md` kap. 7 (vzor ve zdroji ×1).
- **Commit:** feat(fpga) FW 0x0506
- **Stav:** aktivní

### L-0225 - Host test se nespustil (chybí překladač) a změna chování ho tiše rozbila

- **Datum:** 2026-10-24
- **Oblast:** build / testy (`tests/host/t_fpga_v3.c`)
- **Symptom:** host test `t_fpga_v3` hlásil 3 chyby („1PPS předchozí hrubý", „bez CAPS bit17", „bez PCLK") už na commitnutém stavu; nikdo si toho nevšiml.
- **Příčina:** `fpga_v3.c` změnil zdroj hrubého času 1PPS na `PTS >> 14` (FW 0x0506), test dál čekal bajty 100..107. Na Windows stroji nebyl hostitelský překladač C, takže se host test po změně vůbec nepustil; build firmwaru i `audit.py` ho nekontrolují.
- **Oprava:** test podle FW 0x0506 (`456dcd2`); překladač `zig cc` (pip `ziglang` ve venv, bez zásahu do systému); `tests/host/mut_fpga_v3.py` bere překladač z `CC` a na Windows `.exe`.
- **Pravidlo:** změna čistě logického modulu s host testem není hotová, dokud host test a jeho mutace neproběhnou; chybějící nástroj se doplní, ne přeskočí.
- **Detekce:** `CC="zig cc" python tests/host/mut_fpga_v3.py <výstup>` (pustí i nemutovaný běh) — zatím ručně, kandidát do `scripts/build.sh`.
- **Commit:** `456dcd2`
- **Stav:** aktivní

### L-0226 - Řetězec verze ručně zdvojený s čísly se rozjel

- **Datum:** 2026-10-24
- **Oblast:** build (`CM7/Core/Inc/version.h`)
- **Symptom:** firmware v0.39.1 (tag `v0.39.1`) hlásil na UART, displeji i `*IDN?` „v0.39.0".
- **Příčina:** `FW_VERSION_PATCH` se zvýšilo, `FW_VERSION_STR` byl samostatný literál a zůstal.
- **Oprava:** `FW_VERSION_STR` se skládá z `FW_VERSION_MAJOR/MINOR/PATCH` makrem (v0.40.0).
- **Pravidlo:** hodnota, která se dá odvodit, se nepíše podruhé ručně — odvodí se (stejná třída jako duplicitní fakta v CLAUDE.md).
- **Detekce:** `arm-none-eabi-strings CM7/Release/H757_LED_CM7.elf | grep "gpsdo-ui v"` = git tag.
- **Commit:** `bf71399`
- **Stav:** aktivní

### L-0227 - Záložní mechanismus maskuje nefunkční hlavní cestu v testu

- **Datum:** 2026-10-24
- **Oblast:** testy (`fpga_v3_selftest` krok 12, protokol v3)
- **Symptom:** mutace „emulátor bere ACK celých 32 b" (ACK s jinými horními bity se ignoruje) prošla testem příjmu: všechny záznamy přišly, bez mezer.
- **Příčina:** když ACK nefunguje, kruh přeteče a FPGA (emulátor) posouvá REC_FIRST sama; okno rámce (24 záznamů) je větší než přírůstek mezi rámci, takže příjem zůstane souvislý. Test hlídal výsledek (souvislost), ne cestu (ACK).
- **Oprava:** test navíc vyžaduje, aby příznak přetečení kruhu (FLAGS bit1) nenastal; ACK se posílá se smetím v horních 16 b (FPGA je ignoruje).
- **Pravidlo:** má-li systém záložní cestu (přetečení, timeout, resynchronizace), test hlavní cesty musí hlídat, že záloha nezasáhla.
- **Detekce:** mutace „uzka: emulator ACK 32 b" v `tests/host/mut_fpga_v3.py`.
- **Commit:** `bf71399`
- **Stav:** aktivní

### L-0228 - Registr v cestě změnil časování a stimulus testu přestal vyvolávat kontrolovaný stav

- **Datum:** 2026-10-24
- **Oblast:** FPGA / testy (`sim/tb_top_v3.sv`, `sim/mut_top_v3.py`)
- **Symptom:** mutace „FIFO hloubky 1" (FIFO indexů v `seg_chan3`) prošla `tb_top_v3` (PASS) — u nás i u externí instance, i nad stavem před optimalizací.
- **Příčina:** FW 0x0506 přidal registr `tvd` (`ts_valid` o takt později). Kanál B testu (16,100161 MHz, rozestup hran 6/7 taktů) pak už neplní druhou položku FIFO, takže hloubka 1 nemá pozorovatelný účinek. Test hlídal výsledek, ale stav, který měl vyvolat, už nenastával.
- **Oprava:** B = 13,300161 MHz (rozestup 7/8 taktů, 3325,04 period na segment) v `run.ps1` topv3/topv3full a v režimu 0 mutací; mutace `ts_valid` o 2 takty dřív v režimu 8 (B 16,1 MHz); varianta o 1 takt je od `tvd` ekvivalentní (zdůvodnění v `EKV`).
- **Pravidlo:** po změně latence v cestě dat ověř, že stimulus testu pořád vyvolává stavy, které test kontroluje (mutace, která je „vypne", musí test shodit) — PASS sám o sobě pokrytí nedokazuje.
- **Detekce:** úplný běh `python sim/mut_top_v3.py <modul> <výstup>` po každé změně latence; nezachycená mutace = buď ekvivalentní (zdůvodnit v `EKV`), nebo mezera testu.
- **Commit:** `83f1451`
- **Stav:** aktivní

### L-0229 - Název z volného textu použitý jako jméno adresáře shodil čtyřhodinový běh

- **Datum:** 2026-10-24
- **Oblast:** nástroje (`sim/mut_top_v3.py`)
- **Symptom:** mutační běh `tb_top_v3` (38 mutací, ~4,5 h) skončil výjimkou `WinError 123`; výsledky tří posledních mutací se ztratily a musely se pustit znovu.
- **Příčina:** jméno adresáře mutace se skládalo z jejího popisu odstraněním vybraných znaků (blacklist: mezera, `:`, `,`); nová mutace „1PPS: seq > 1 vzdy" (z macOS, kde `>` v názvu nevadí) obsahovala `>`.
- **Oprava:** jméno = popis s mezerami na `_` a vším mimo `[A-Za-z0-9_.-]` odstraněným (whitelist).
- **Pravidlo:** jméno souboru z volného textu se tvoří whitelistem povolených znaků, ne vyjmenováním zakázaných — a nástroj, který běží hodiny, má selhat při kontrole předem, ne uprostřed.
- **Detekce:** `python sim/mut_top_v3.py <modul> --selftest` (kontrola kotev před během); jména jsou po opravě bezpečná pro libovolný popis.
- **Commit:** `83f1451`
- **Stav:** aktivní

### L-0230 - Výstup deserializéru po uvolnění resetu nese neznámý stav pinu

- **Datum:** 2026-10-24
- **Oblast:** FPGA (`pps_ides3` v `src/v3_core.v`, jemný čas 1PPS)
- **Symptom:** hrana 1PPS 1..22 taktů před uvolněním PCLK po znovuzamčení PLL (pin drží 1) dostala jemný čas prvního slova IDES8 (falešná poloha 0) — nález N1 testu `tb_pps_v3`.
- **Příčina:** detekce hrany na rozhraní slov porovnává s posledním bitem předchozího slova (`lastw`), který je po uvolnění resetu 0 bez ohledu na pin; IDES8 má navíc po uvolnění latenci, kterou simulace neověří.
- **Oprava:** 2b čítač `wn` drží `lastw = 1` po první 3 slova (hrana v poloze 0 se nehlásí), `u_ipps/wn*` ve `false_path` z `rdy` (`timing_v3.sdc`).
- **Pravidlo:** data z bloku s vnitřní latencí (deserializér, synchronizátor, filtr) jsou po uvolnění resetu neplatná, dokud neuplyne jeho latence s rezervou — maskuj je, nedoplňuj výchozí hodnotou.
- **Detekce:** `tb_pps_v3` (N1 výchozí kontrola), mutace „N1: lastw bez masky prvních slov" v `sim/mut_pps_v3.py`.
- **Commit:** `d37ed5c`
- **Stav:** aktivní

### L-0231 - Jemný čas starší než hrubá hrana k ní nepatří

- **Datum:** 2026-10-24
- **Oblast:** FPGA (`pps_cap3` v `src/v3_core.v`)
- **Symptom:** zákmit kratší než takt (hrubý detektor ho nevidí) do ~38 taktů před hranou 1PPS předal hraně svůj jemný čas — nález N2.
- **Příčina:** příznak platného jemného času `fa` se nastavil zákmitem a hrubá hrana ho převzala, protože se při ní nenuloval.
- **Oprava:** `fa <= 1'b0` při hrubé hraně (platný jemný čas hrany přichází až ~13 taktů po ní); okno zákmitu 5..8 taktů před hranou zůstává otevřené.
- **Pravidlo:** když se dvě události párují přes čas, párovací okno musí začínat událostí, ke které se páruje — co přišlo před ní, se zahodí.
- **Detekce:** `tb_pps_v3` (kontrola N2 pro ≥ 10 taktů), mutace „N2: jemny cas pred hrubou hranou se nezahodi".
- **Commit:** `a3f2853`
- **Stav:** aktivní

### L-0232 - Zástupce programu v PATH prošel `command -v` a kontrola se tiše přeskakovala

- **Datum:** 2026-10-24
- **Oblast:** build (`scripts/build.sh`)
- **Symptom:** kontrola modelu IDE (`.project` vs `subdir.mk`) nikdy nehlásila nic, ani když měla; nikdo si nevšiml, že neběží.
- **Příčina:** `PY="$(command -v python3 || command -v python)"` vzal `python3` ze `WindowsApps`, což je zástupce Microsoft Store (existuje, ale nic nespustí); volání bylo navíc s `|| true`.
- **Oprava:** `PY` je první kandidát (`python3`, `python`, `py`), který se opravdu spustí (`-c "import sys"`); bez Pythonu se vypíše varování, že kontrola je PŘESKOČENA (`scripts/build.sh`).
- **Pravidlo:** nástroj z PATH se vybírá tím, že se spustí, ne tím, že existuje; kontrola, která se může přeskočit, to musí říct nahlas.
- **Detekce:** `python3 --version` na Windows stroji; po opravě se výběr vypisuje varováním jen při selhání.
- **Commit:** viz `git log scripts/build.sh`
- **Stav:** aktivní

### L-0233 - Časování FPGA se posuzuje statistikou sestavení, ne jedním sestavením

- **Datum:** 2026-10-24
- **Oblast:** FPGA / P&R (Gowin, `tools/pnr_seeds.py`)
- **Symptom:** externí optimalizace slibovala lepší časování, první sestavení dalo −0,87 ns, další −1,14 / −0,23 ns; ablace z předchozí analýzy (jedno sestavení na variantu) se nedaly porovnat – rozdíly ~0,5 ns byly v šumu.
- **Příčina:** `BLD_TIME` v `build_id.vh` je vstupem P&R (L-0222), takže každé sestavení je jiné rozmístění; jedna hodnota rezervy je vzorek z rozdělení s rozptylem ~1,5 ns. Při stejném seedu a netlistu je P&R deterministický (varianta, kterou Gowin zjednoduší na stejný netlist, dá stejná čísla).
- **Oprava:** `tools/pnr_seeds.py` – N sestavení se syntetickým `BLD_TIME`, souhrn (projde / medián / nejhorší) a skupiny cílů porušených cest přes sestavení. Systematické cesty (≥ 3 z 8) se opravují v RTL přesně (kopie, předpočet, registr místo LUT v povolení), náhodné ne. 0x0507: 2/8 → 6/8, medián −0,23 → +0,07 ns.
- **Pravidlo:** změna kvůli časování FPGA se hodnotí rozdělením rezervy přes ≥ 8 sestavení a seznamem cest, které selhávají opakovaně; jedno sestavení (i v ablační tabulce) není měření.
- **Detekce:** `python tools/pnr_seeds.py run <var> 8 -j 4` + `report <var>`; brána `fpga_release.py --tries K`.
- **Commit:** `7fadfec` (nástroj), opravy `4ceec06`, `c586f03`
- **Stav:** aktivní

### L-0234 - Diagnostika děje při startu předpokládala, že pozorovatel už běží

- **Datum:** 2026-10-24
- **Oblast:** firmware / diagnostika (`gpsdo_tick`, STATUS #337b)
- **Symptom:** v0.40.1 měla změřit interval PSEQ 1 -> 2 po startu FPGA; po několika power-cyklech hlásila „zatím nezměřeno“.
- **Příčina:** (⚠️ opraveno měřením v0.40.4: STM čte FPGA DŘÍV, než přijde první hrana – vidí PSEQ 0; první hrana chodí ~1,145 s po startu FPGA) Domněnka: STM začne číst FPGA až po bootu a bring-upu displeje (sekundy); hlavička nese jen poslední hranu 1PPS (předchozí od úzké hlavičky ani ne), takže PSEQ 1 a 2 jsou dávno přepsané. Kritérium „1 s ± 10 ms“ navíc nepočítalo s 10 Hz timepulse bez fixu.
- **Oprava:** v0.40.2 zaznamená první hranu, kterou STM uvidí (PSEQ, čas od startu FPGA, odstup k další), a interval 1 -> 2 hodnotí proti mřížce 100 ms (`freertos_task_fpga.c`).
- **Pravidlo:** diagnostika jednorázového děje při startu musí nejdřív ověřit časovou osu – kdy nastane děj a kdy začne pozorovatel číst; když pozorovatel nastupuje později, měř první viděnou událost, ne předpokládanou.
- **Detekce:** `status` → `GPSDO: 1PPS prvni videna hrana: PSEQ …, … ms od startu FPGA`.
- **Commit:** `ad093ea`
- **Stav:** aktivní

### L-0235 - Nastavení existovalo jen v RAM a výchozí init ho po startu tiše přepsal

- **Datum:** 2026-10-24
- **Oblast:** firmware / perzistence (`syscfg.c`, generátor Si5356)
- **Symptom:** uživatel změnil kmitočet v okně GENERÁTOR, po restartu byl zpět na výchozím.
- **Příčina:** `g_si_gen` se plnilo jen za běhu; `si5356_init` v `main()` zapíše mapu REGMAP a `si5356_gen_defaults()`. Do syscfg blobu pole nikdy nepřibylo – a blob už neměl volnou výplň.
- **Oprava:** pole generátoru ZA KONEC blobu (192 → 208 B, magic beze změny); úložiště vrací délku uloženého blobu, takže starý blob (192 B) se čte dál a uživatel nepřijde o ostatní nastavení (`syscfg.c`, v0.40.3).
- **Pravidlo:** každá hodnota, kterou uživatel nastaví v UI, se buď persistuje, nebo okno výslovně říká „neukládá se“; nové pole do plného blobu se přidává za konec s rozlišením podle délky, ne novým magicem.
- **Detekce:** změnit generátor, power-cyklus, `gen` musí ukázat nastavenou hodnotu.
- **Commit:** `c4b26f1`
- **Stav:** aktivní

### L-0236 - Nejistota pro jedno měření vzatá z jiného τ, než má měření

- **Datum:** 2026-10-24
- **Oblast:** firmware / prezentace měření (`screen_main.c` `meas_u_rel`)
- **Symptom:** počet platných číslic hlavního čísla se řídil σy@1s i při hradle 0,25 s (pod 1 s Allanova pyramida data nemá).
- **Příčina:** nejistota jednoho zobrazeného měření je σy(τ = hradlo); σy@1s platí jen pro hradlo 1 s – u bílého PM je při 0,25 s až 4× větší, u zdroje s driftem naopak menší.
- **Oprava:** FpgaTask počítá Allanovu odchylku navazujících oken (`win_adev_feed`), `meas_u_rel` ji bere místo σy@1s (v0.40.4).
- **Pravidlo:** nejistota zobrazeného čísla se bere při tomtéž τ, jako má zobrazené měření; statistika z jiného τ jen jako záloha a se škálováním.
- **Detekce:** `status full` → `STATISTIKA: sigma_y@hradlo`.
- **Commit:** `bd01aec`
- **Stav:** aktivní

### L-0237 - Slot rámce vydávaný za absolutní kanál

- **Datum:** 2026-10-24
- **Oblast:** web (`httpd_min.c` SPA, karta DVOJKANÁL)
- **Symptom:** při CHAN = B web ukazoval A a B prohozené (displej správně).
- **Příčina:** `freq_hz` je primární slot (vybraný kanál), `freq16_hz` druhý; SPA je brala jako pevné A/B. Displej to řešil přes `fpga_meas_freq_ch`, web ne – dvě cesty, jedna zapomenutá.
- **Oprava:** `chAB(s)` mapuje sloty podle `s.channel` (`9ade6f3`).
- **Pravidlo:** význam slotu (primární/sekundární) se překládá na absolutní kanál na JEDNOM místě; každý konzument (displej, web, SCPI) používá ten překlad, ne vlastní předpoklad.
- **Detekce:** přepnout CHAN na B a porovnat kartu DVOJKANÁL na displeji a webu.
- **Commit:** `9ade6f3`
- **Stav:** aktivní

### L-0238 - Specifikace říká 25 MHz, firmware jel na polovině a nikdo si toho nevšiml

- **Datum:** 2026-10-25
- **Oblast:** `fpga_freq.c` (`FPGA_SCK_TARGET_HZ`), SPI2 → FPGA
- **Symptom:** dotaz „jaká je rychlost SPI?" ukázal, že po startu běží 12,5 MHz, zatímco `PROTOKOL_V3.md` kap. 2, testy i komentář u nastavení pinů počítají s 25 MHz.
- **Příčina:** konstanta byla nastavena 2026-10-18 na 12,5 MHz (zadání „10 MHz", první stupeň ≥ 10) a při přechodu na protokol v3 se k ní nikdo nevrátil; `spi <MHz>` za běhu se nepersistuje, takže se rozdíl neprojevil nikde jinde než ve `status`.
- **Oprava:** `FPGA_SCK_TARGET_HZ` 25 MHz (kernel 200 MHz / 8), strop 40 MHz beze změny (`85462d0`, v0.40.5).
- **Pravidlo:** číslo ze specifikace a konstanta ve firmwaru se při změně protokolu porovnají výslovně; hodnotu, kterou lze přepsat za běhu bez persistence, ověřuj po restartu ze `status`, ne z paměti.
- **Detekce:** `status` → `SPI 25.00MHZ` po power-cyklu, `RX chyb CRC` a `delka` zůstávají 0.
- **Commit:** `85462d0`
- **Stav:** 🟡 ověřeno na desce po power-cyklu 2026-10-25 (v0.40.6): SCK 25 000 000 Hz, CRC err 5,7/h (reference 12,5 MHz 5,6/h), SEQ díry bez přírůstku, selftest 16/16, `fpga v3 test` PASS; ⬜ nezměřeno osciloskopem (strmost hran, `FPGA_SPI_PIN_SPEED`) ani σy@1s

### L-0239 - Kontrolní počítání hran četlo čítač s registrovaným přenosem v taktu, kdy byl nekonzistentní

- **Datum:** 2026-10-25
- **Oblast:** FPGA / `seg_chan3` (`Frequency_Counter_FPGA_Module/src/v3_core.v`)
- **Symptom:** STM za 7,5 h zamítlo 57 oken kvůli kontrolnímu počítání (`status` → `FPGA SAMOKONTROLA: zamitnuto oken: citani 57`, `FPGA_CHK_COUNT`), přestože žádná hrana nechyběla. Vždy dva po sobě jdoucí záznamy s FLAGS bit4.
- **Příčina:** čítač hran X = `{xhi, xlo}` má registrovaný přenos. V taktu `xcy = 1` je `xlo` už 0 a `xhi` ještě starý, takže `xcnt[9:0]` je o 256 menší. Padne-li `snap` do tohoto taktu, vyjde `cdd = ±256` a falešný `ccd_bad`; `xprev` se uloží také chybný, takže chybný je i následující záznam. Pravděpodobnost f / (256 × 100 MHz) na segment, u 10 MHz asi 1× za 64 s na kanál. Nalezla externí instance při optimalizaci (`exchange/ukol_2026-10-25_fpga_opt2`), potvrzeno testem a měřením na desce.
- **Oprava:** `xc10 = {xhi[1:0] + xcy, xlo}` (dolních 10 b skutečného počtu i v taktu přenosu), `v3_core.v` v `seg_chan3` (`859bd40`).
- **Pravidlo:** hodnotu čítače s registrovaným přenosem čti jen složenou s přenosem nebo v taktu, kdy je konzistentní; test konzumenta musí vynutit okamžik čtení právě v taktu přenosu, ne jen náhodně.
- **Detekce:** `sim/tb_ccd_carry.sv` (`run.ps1`: `ccdcarry`, bez opravy FAIL, s opravou PASS); na desce `status` → `zamitnuto oken: citani` (základ před opravou 57 za 26 960 s = 7,6/h).
- **Commit:** `859bd40`
- **Stav:** 🟡 opraveno a ověřeno simulací; ⬜ neověřeno na desce (bitstream `FW_0x0507_v3_20261025-1613_28c71c9.fs`)

### L-0240 - Otisk stopy z cizího prostředí není důkaz shody chování

- **Datum:** 2026-10-25
- **Oblast:** FPGA / ověřování cizí práce (`sim/trace_v3.py`)
- **Symptom:** externí instance uvedla v tabulce shody sha256 stop základu a řady; u mě stejné testy dávaly jiné otisky a u `topv3deep` i jiný počet řádků (313 010 proti 312 869), přestože testy prošly.
- **Příčina:** autor měl jiný simulátor (Icarus 13 proti našemu 12 devel) a jiný stroj; otisk závisí na prostředí, ne jen na RTL.
- **Oprava:** shodu jsem dokázal vlastním během základu i řady na tomtéž stroji a stejným simulátorem (`trace_v3.py run` + `cmp`): 8 z 8 testů shodných (`docs/audit/2026-10-25_optimalizace-fpga-v3-kolo2.md`).
- **Pravidlo:** shodu chování z cizí práce dokazuj vlastním během základu i změny ve stejném prostředí; převzatý otisk, tabulka PASS nebo počet řádků nejsou důkaz.
- **Detekce:** `python sim/trace_v3.py cmp <základ> <změna>` → „stopy shodne (N testu)"; pozitivní kontrola měřítka (stopa se rozejde při změně chování) je součástí zprávy externí instance, kap. 4.2.
- **Commit:** (dokumentace)
- **Stav:** aktivní

### L-0241 - Počítadlo `CRC err` nevidělo rámce se špatným MAGIC nebo VER

- **Datum:** 2026-10-25
- **Oblast:** firmware / SPI v3 (`fpga_freq.c` `poll_v3`, `fpga_v3.c` `fpga_v3_parse`)
- **Symptom:** při hledání příčiny CRC chyb (5,7/h) se ukázalo, že počítadlo nezachytí poškozený rámec, jehož první bajt (MAGIC) nebo druhý (VER) je špatně. Takový rámec jen shodí link pro ten rámec, nikde se nespočítá.
- **Příčina:** `fpga_v3_parse` kontroluje MAGIC a VER dřív než CRC a vrací `FPGA_V3_E_MAGIC` / `E_VER`; `poll_v3` zvyšuje `g_rx_crc` jen u `E_CRC0` a u vadných bloků 1–3. Posunutý nebo zcela poškozený rámec (např. posun o bit, samé 0xFF) tedy skončí v neviditelné větvi.
- **Oprava:** v0.40.7 zapisuje každou chybu rámce do kruhu s druhem (1 CRC bloku 0, 2 CRC bloku 1–3, 3 MAGIC/VER, 4 LEN) a uloží surový vadný blok; `CRC err` zůstává beze změny (druh 1 + 2). `fpga_freq.c` (`s3x_note`), UART `spi err`, `spi err dump`.
- **Pravidlo:** počítadlo chyb musí pokrývat všechny větve, kterými rámec může být zahozen, ne jen tu, podle které se jmenuje; při přidání nové kontroly před CRC přidej i její počítadlo.
- **Detekce:** `status` → řádek `SPI ERR` (rozpis podle druhu), `spi err` (kind 3 = MAGIC/VER); zkouška na desce `spi err inject <blok> <n>`.
- **Commit:** `4f51a54`
- **Stav:** 🟡 v kódu (v0.40.7), ⬜ neověřeno na desce

### L-0242 - Vysílání FPGA se uprostřed SPI transakce restartuje od bajtu 0, počítadlo ukazovalo jen číslo

- **Datum:** 2026-10-26
- **Oblast:** FPGA / SPI PHY v3 (`spi_slave_phy_v3.v`), diagnostika
- **Symptom:** `CRC err` na STM kolem 6/h bez vysvětlení; po nasazení v0.40.7 a nočním testu (15,6 h, 1 694 472 transakcí) 155 chybných rámců = 91,5 na milion rámců, stejně při 25 i 6,25 MHz, s DMA i bez, při 5× víc rámcích.
- **Příčina (zjištěná část):** ve všech 109 vadných blocích 0 se uprostřed bloku znovu objeví začátek rámce (všech 144 konstantních bitů hlavičky), na pozici q = 2…116 bitů (stejně při 25 i 6,25 MHz); FPGA `RX_LEN_ERR` roste o 2,08 na každou chybu. V RTL `clr <= cs_fall` je asynchronní reset čítačů TX i RX, takže každý pulz na CS delší než 10 ns rámec rozdělí: konec první části (`RX_LEN_ERR` +1), restart od MAGIC, konec druhé části (+1). **Původ pulzu na CS není zjištěn (HYPOTÉZA: přeslech nebo zakmitnutí na vedení, vstup bez hystereze a bez filtru).**
- **Oprava:** FW 0x0508: filtr CS v PHY (16 taktů 100 MHz = 160 ns, pulz kratší se zahodí a spočítá), čítače zahozených pulzů v hlavičce 132..137 (CAPS bit23), `HYSTERESIS=HIGH` na CS, SCK, MOSI (`pins.cst`); STM v0.40.8 čte čítače (`spi err`) a umí měnit rychlost hran SCK/MOSI/CS za běhu (`spi pins`). Bitstream `ab_test/FW_0x0508_v3_20261026-1944_bd20e77.fs`.
- **Pravidlo:** (1) asynchronní reset čítačů v taktové doméně rozhraní nesmí vycházet z neošetřeného signálu pinu; odvoď ho z filtrovaného CS (minimální šířka pulzu) a přidej čítač zahozených pulzů. (2) Diagnostika chyb rozhraní musí ukládat surová data vadného rámce a druh chyby, ne jen počítadlo: z 109 uložených bloků byl mechanismus vidět v rozboru jediného běhu, z holého počítadla nešel poznat.
- **Detekce:** `spi err`, `spi err dump`, `tools/spi_night.py` + `tools/spi_night_report.py`; na FPGA `RX chyb … délka` ve `fpga v3` (přírůstek 2 na chybu).
- **Commit:** `9cdbbbe` (filtr a čítače), `ab65703` (hystereze), `bd20e77` (zápis verze pro bránu vydání), `6b91119` (STM v0.40.8)
- **Stav:** ✅ ověřeno na desce (noc 2026-10-26/27, 7,2 h, ≈ 1,44 mil. rámců, 0 chyb proti očekávaným ≈ 130; `docs/audit/2026-10-26_spi-noc-vyhodnoceni.md` kap. 8). Příčina mezi filtrem CS a hysterezí NEURČENA: čítače zahozených pulzů zůstaly 0, takže filtr žádný pulz neviděl a čítače nebyly nikdy ověřeny na skutečném pulzu. Původ pulzu dál neznámý (HYPOTÉZA: přeslech/zakmitnutí; ověření osciloskopem na CS)

### L-0243 - Maximum čekání DMA2D ve `status` zahrnuje preempci UiTasku a hláška „v pořádku" ho nehlídala

- **Datum:** 2026-10-27
- **Oblast:** `prim_stm32_hal.c` `d2d_wait`, UART `status` (řádek `DMA2D:`)
- **Symptom:** `DMA2D: chyb 0, timeout 0 | flip timeout 0 | max cekani 616.592 ms (mez 500)  (v poradku)`: maximum je nad vypsanou mezí, přesto bez timeoutu a s verdiktem „v pořádku".
- **Příčina:** `g_d2d_wait_max_cyc` měří od vstupu do `d2d_wait` po návrat včetně `osDelay(1)` v pomalé větvi. UiTask je BelowNormal, takže ho při dlouhých UART/SPI úlohách vytlačí ostatní tasky a `osDelay(1)` se vrátí o stovky ms později. Mez 500 ms se kontroluje až po návratu z `osDelay`; když mezitím přenos doběhl, smyčka končí bez timeoutu. Hodnota nad mezí tedy neznamená, že přenos trval dýl než mez, jen že UiTask dlouho nedostal CPU. Verdikt „v pořádku" závisel jen na počítadlech chyb a timeoutů, ne na maximu.
- **Oprava:** `status` při `max > 500 ms` bez chyb vypíše vysvětlení „cekani zahrnuje preempci UiTasku, ne dobu prenosu" místo holého „v pořádku" (chování měření se nemění).
- **Pravidlo:** metrika, která měří čas přes yield (`osDelay`), je horní odhad včetně preempce, ne doba hardwarové operace; pokud se porovnává s mezí, musí výpis říct, co v čísle je. Pro skutečnou dobu přenosu měř jen spin před prvním yieldem.
- **Detekce:** `status` → `DMA2D:`; nezávisle pomocí `fbdiff` (rozklad podtečení LTDC po fázích) a `stats` (CPU po taskách).
- **Commit:** viz commit v0.40.9
- **Stav:** ⬜ neověřeno na HW (změna jen textu výpisu, přeloženo bez varování)

---

### L-0244 - `stacktest` nepřetékal zásobník, protože UartTask mezitím dostal 8 kB

- **Datum:** 2026-10-28
- **Oblast:** `freertos_task_uart.c` `stacktest_overflow`, UART `stacktest yes`
- **Symptom:** `STACKTEST: hook se NEOZVAL (0) - detekce NEFUNGUJE!`, deska se nerestartovala, uptime běžel dál.
- **Příčina:** pole `waste[3600]` bylo dimenzované na zásobník UartTasku 4096 B. Zásobník má dnes `2048 * 4` = 8192 B (`freertos.c`), takže test nepřetekl a hlídač přetečení se ani nespustil. Selhání testu neříká nic o hlásiči.
- **Oprava:** `waste[8000]` (v0.42.1). Komentáře u funkce a u příkazu ještě zmiňují 4096 B a 3600 B.
- **Pravidlo:** test, který záměrně něco překračuje, musí mít mez odvozenou od aktuální konfigurace (nejlépe z `uxTaskGetStackHighWaterMark` / velikosti z atributů), ne pevné číslo; po změně velikosti zásobníku test znovu pustit.
- **Detekce:** `stacktest yes` musí vyvolat IWDG reset a po bootu `Reset: ... stack:UartTask`.
- **Commit:** v0.42.1
- **Stav:** ✅ ověřeno na desce 2026-10-28 (v0.42.1): `stacktest yes` → IWDG reset, `stack:UartTask`, webhook `cm7_stalled` + `cm7_recovered` (po SW resetu, ne po power-cyklu)

---

### L-0245 - pomalé hrany SCK (`spi pins sck low`) zabíjejí SPI spojení s FPGA

- **Datum:** 2026-10-28
- **Oblast:** UART `spi pins`, `tools/spi_night.py` větev `M`
- **Symptom:** 3h test SPI: větev M (SCK/MOSI/CS všechny LOW) 46 minut `ramcu 0`, GPSDO `HOLD`; větve B, W, K dohromady 796 688 rámců, 0 chyb.
- **Příčina:** izolovaně zkoušeno po jednom pinu: po `spi pins sck low` stojí čítač transakcí (43) a spojení nejede; MOSI LOW a CS LOW samy to nedělají. Po `sck medium` spojení hned naběhne (254 transakcí za 10 s, 0 chyb). Proč přesně (hrana nestíhá při 25 MHz, nebo FPGA SCK nevyhodnotí) nezjištěno.
- **Oprava:** žádná, jde o nastavení za běhu (nepersistuje se, po resetu MEDIUM). Plán `spi_night.py` větev M obsahuje nepoužitelnou kombinaci.
- **Pravidlo:** `spi pins sck` nikdy pod MEDIUM; větev „vše LOW" nevypovídá o chybovosti, jen o výpadku. Výsledek bez rámců (`ramcu 0`) není „0 chyb".
- **Detekce:** `spi err` → `transakci 0`; `status` → GPSDO `HOLD`.
- **Commit:** viz docs commit 2026-10-28
- **Stav:** ✅ ověřeno na desce (izolovaný pokus po jednom pinu)

---

## Archiv (neplatné lekce)

*(prázdné)*
