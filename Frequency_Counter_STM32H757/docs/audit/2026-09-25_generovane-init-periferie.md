# Audit modulu 23 — generované init soubory periferií (`MX_*_Init` + `USER CODE`)

**Datum:** 2026-09-25
**Fáze:** F3 (přezkum kódu) — **bez editace kódu**
**Jádra:** CM7 (+ CM4 `eth.c`)
**Branch:** `audit/2026-09-09-hodiny-pwr`

## Proč tenhle modul vznikl

Mezera v pokrytí nalezená **křížovou kontrolou souborů**, ne dalším auditem:
seznam všech `.c` v projektu se porovnal se souborovými seznamy 22 modulů
v `AUDIT_STATUS.md` a ukázalo se, že CubeMX-generované `MX_*_Init` soubory
nebyly nikdy předmětem žádného modulu. Zástupné položky v tabulce
(`libprim/*`, `libui/*`, `freertos*.c`) je nepokrývají. Zapsáno commitem
`9558a5d`.

🔑 **Proč to nebylo bezpečné vynechat:** právě ve `fmc.c` žily `ReadPipeDelay = 0`
a `REFRESH_COUNT = 1835` — dohromady 3 338 207 chybných bitů, černý displej
a měsíce podezírání pájky `FMC_A9` a vadného SDRAM čipu. Obě byly v kódu
**od prvního commitu** a našlo je **měření, ne audit**.

## Rozsah — co bylo čteno

| soubor | celkem | ruční `USER CODE` | čteno |
|---|---|---|---|
| `CM7/Core/Src/fmc.c` | 477 ř. | **180 ř.** | celé |
| `CM7/Core/Src/usart.c` | 229 ř. | **88 ř.** | celé |
| `CM7/Core/Inc/fmc.h` | — | — | celé |
| `adc.c`, `dsihost.c`, `ltdc.c`, `quadspi.c`, `spi.c`, `tim.c`, CM4 `eth.c` | 122–199 ř. | 17–19 ř. | `USER CODE` bloky |

**Kontext přečtený kvůli rozhodnutí nálezů:** `CM7/Core/Src/main.c` (pořadí
bootu, Boot_Mode_Sequence_1/2), `CM7/Core/Src/freertos.c` (`gpio_cfg_lock`,
`gpio_guard_tick`), `CM4/Core/Src/main.c` (rozsah HSEM 1), `CM4/Core/Src/gpio.c`,
`CM4/Core/Src/eth.c` (zápisy do GPIOG), `freertos_task_uart.c` (`sdraminit`).

**Buffery v `.map`:** tento modul žádné nealokuje. Umístění SDRAM oblastí
(`FB0/1/2`, `.sdram`, `.measlog`) je předmět modulu 2, kde je doložené.

---

### F-0149 [S2] Obranný zápis PG8 ve `MX_FMC_Init` běží BEZ `gpio_cfg_lock()`, zatímco CM4 souběžně drží HSEM 1 — jednostranný zámek nevylučuje nic

- **Místo:** `CM7/Core/Src/fmc.c:229-238` (ruční potvrzení PG8 v `USER CODE FMC_Init 2`),
  souvisí `CM7/Core/Src/fmc.c:346` (`HAL_GPIO_Init(GPIOG, …)` v generovaném
  `HAL_FMC_MspInit`).
- **Popis:** Blok na `fmc.c:229-238` volá `HAL_GPIO_Init(GPIOG, &sdclk)` pro PG8,
  tedy **neatomický read-modify-write** nad `GPIOG->MODER`/`AFR`/`OTYPER`.
  Nedělá to pod `gpio_cfg_lock()` (HSEM 1), přestože CM4 kolem **všech** svých
  `MX_*_Init` — a ty konfigurují GPIOG — HSEM 1 poctivě drží. **Zámek, který
  bere jen jedna strana, neposkytuje vzájemné vyloučení žádné.**
- **Důkaz:**
  1. **Souběh je doložen pořadím bootu, staticky:** CM4 se probouzí z STOP
     uvolněním HSEM 0 na `main.c:333-335` (`HAL_HSEM_FastTake(HSEM_ID_0)` +
     `HAL_HSEM_Release(HSEM_ID_0,0)`, blok `Boot_Mode_Sequence_2`, ř. 327-345).
     `MX_GPIO_Init()` je až na `main.c:381` a **`MX_FMC_Init()` až na `main.c:382`**.
     CM4 tedy běží **dřív**, než CM7 vůbec sáhne na FMC.
  2. **CM4 na GPIOG skutečně sahá:** `CM4/Core/Src/gpio.c:56` `HAL_GPIO_Init(GPIOG,…)`
     (LED_2 PG7, ETH_RES PG14), `CM4/Core/Src/eth.c:150` `HAL_GPIO_Init(GPIOG,…)`
     (PG11/PG13).
  3. **CM4 je pod zámkem:** `CM4/Core/Src/main.c:175` `HAL_HSEM_FastTake(1u)`
     před `MX_GPIO_Init/MX_TIM12_Init/MX_ETH_Init`, uvolnění na `:193`.
  4. **CM7 ve FMC cestě pod zámkem není:** `grep gpio_cfg_lock CM7/` vrací
     `encoder.c:68,74`, `fpga_freq.c:212`, `sdmmc.c:108`, `sd_export.c:497`,
     `freertos.c:455` — **`fmc.c` v seznamu chybí**.
- **🔴 Proč se na to nepřišlo dřív — chybná premisa v komentáři:** komentář na
  `fmc.c:222-227` vylučuje CM4 jako původce přepisu PG8 větou *„Stav byl analog
  uz ~200 ms po resetu, tedy PRED bootem CM4."* Pořadí bootu (bod 1) tu premisu
  **popírá** — CM4 startuje před `MX_FMC_Init`, ne po něm. Nejpravděpodobnější
  kandidát na nevysvětlený přepis PG8 → ANALOG (černý displej, 10 551 639
  chybných bitů) byl tedy vyloučen na základě tvrzení, které boot sekvence
  nepodporuje.
  ⚠️ **HYPOTÉZA:** že k přepisu došlo *právě tímto* závodem, není z kódu
  dokazatelné — jen to, že důvod pro vyloučení CM4 neplatí. **Ověření na HW:**
  `status` → `GPIO HLIDAC` (nenulové počítadlo oprav PG8 = závod opravdu probíhá)
  a `errlog` (`ERRLOG_K_GPIO`, drží historii přes resety, takže se pozná, jestli
  četnost roste); souběžně `CM4: alive` pro potvrzení, že CM4 v tom bootu běžel.
- **Dopad:** Ztracený zápis v obou směrech. (a) CM7 může o svůj PG8 zápis přijít →
  `FMC_SDCLK` zůstane mimo AF12 → SDRAM nedostane hodiny → **černý displej**
  a `membench` hlásí chyby u všech vzorů kromě `0x00`. (b) CM7 může přepsat
  právě zapisovanou konfiguraci CM4 → **PG11 `ETH_TX_EN` ztratí `AFR`** → deska
  nedostane IP. Obojí je přesně třída #208/#219, kterou projekt už třikrát
  změřil. Obrana `gpio_guard_tick()` vadu **opravuje až 1×/s z defaultTasku**,
  tedy dávno po bring-upu displeje.
- **🔑 Ironie, která tomu dává váhu:** je to kód, jehož **jediným účelem** je
  bránit ztrátě konfigurace PG8 — a sám je napsaný tím způsobem, který tu ztrátu
  vyrábí.
- **Reprodukce:** staticky doložitelné (viz Důkaz). Časový překryv na HW → viz
  HYPOTÉZA výše.
- **Návrh opravy:** Obalit blok `fmc.c:229-238` do `gpio_cfg_lock()` /
  `gpio_cfg_unlock()` (+ `#include "gpio_guard.h"`). **Je to regen-safe** —
  blok leží uvnitř `USER CODE FMC_Init 2`.
  🔴 **Tím padá stojící výmluva:** komentář u `gpio_cfg_lock` (`freertos.c:437-438`)
  říká *„Nepokryva to, co konfiguruji GENEROVANE `MX_*_Init` na CM7 (jsou mimo
  USER CODE, takze je nelze regen-safe obalit)"*. To platí pro `HAL_FMC_MspInit`
  (`fmc.c:346`), **ne** pro tenhle ruční blok — ten v `USER CODE` je.
  ⚠️ Generovaný `HAL_FMC_MspInit` zůstává nechráněný; to je samostatná, větší
  otázka (skupina B/C), protože obalit ho regen-safe nejde.
- **Riziko opravy:** nízké. `gpio_cfg_lock()` je best-effort s omezeným čekáním
  (`50000` pokusů, ~jednotky ms) a při neúspěchu pokračuje — nemůže tedy zavést
  deadlock při bootu. Zúží okno, negarantuje.
- **Vztah k lekcím:** **L-0012** (symetrické instance — jedna strana opravená,
  druhá ne; tady doslova: CM4 zamyká, CM7 ne), **L-0022** (obrana, která je
  opt-in, dojde jen na ty volající, kteří o ní vědí), **L-0011** (závěr
  „to nebyl CM4" převzatý bez opory v datech). **Nová lekce L-0082.**
- **Stav:** **opraveno 2026-09-25**, ⬜ **neověřeno na HW**. Opraveno **podle
  návrhu**: blok obalen `gpio_cfg_lock()`/`gpio_cfg_unlock()`, přidán
  `#include "gpio_guard.h"` (obojí uvnitř `USER CODE`, tedy regen-safe).
  **Navíc oproti návrhu** uvedena na pravdu chybná premisa v komentáři
  (`fmc.c:222-227`), která vylučovala CM4 — je to komentář popisující právě
  měněné řádky, takže patří do `fix:` (F5.1).
  **Ověřeno ve slinkovaném obrazu:** `objdump -d` nad `MX_FMC_Init` vypisuje
  `bl <gpio_cfg_lock>` i `bl <gpio_cfg_unlock>`.
  🔑 **Co ověřit na desce:** `status` → `GPIO HLIDAC` (počítadlo oprav PG8) by
  po power-cyklech mělo být **nižší nebo nulové**; `errlog` (`ERRLOG_K_GPIO`)
  drží historii přes resety, takže jde porovnat četnost před/po. Displej musí
  po **studeném startu** naběhnout (ne jen po resetu — viz F-0152 níže, obojí
  je ve stejné funkci).

---

### F-0150 [S4] Komentář ve `fmc.c` tvrdí `REFRESH_COUNT = 175`; skutečná hodnota je 371 a experiment se 175 byl změřen a zamítnut

- **Místo:** `CM7/Core/Src/fmc.c:53-79` (blok „🔴 2026-09-09: 371 NESTACILO").
- **Popis:** Komentář na 25 řádcích argumentuje, že 371 nestačí, a uzavírá:
  *„Nova hodnota: tREF 32 ms -> 32e-3 * 50e6 / 8192 - 20 = 175."* a
  *„Zvyseno na 32 ms (REFRESH_COUNT_EXPECTED=175)"*. Skutečná hodnota je
  **371** a důvod pro 175 (nedostatečná rezerva obnovy) byl **měřením vyvrácen**.
- **Důkaz:**
  - `CM7/Core/Inc/fmc.h:69`: `#define REFRESH_COUNT_EXPECTED 371`.
  - `fmc.h:65-68` popisuje, proč se vrátilo na 371: *„Kdyby slo o rezervu obnovy,
    dvojnasobek by to vyrazne zlepsil. Nezlepsil -> obnova to NENI a hleda se ve
    CTECI CESTE (viz `ReadPipeDelay` ve fmc.c). Vraceno na spec hodnotu."*
  - `CLAUDE.md:487`: *„⚠️ **`REFRESH_COUNT` je a zůstává 371** — jediný zdroj
    `REFRESH_COUNT_EXPECTED` ve `fmc.h`."*
  - `fmc.c:80`: `#define REFRESH_COUNT REFRESH_COUNT_EXPECTED` → efektivně 371.
- **Dopad:** Čistě dokumentační, ale na **nejnebezpečnější konstantě v projektu**.
  Čtenář `fmc.c` (a `fmc.c` je první soubor, kam se u SDRAM sáhne) odejde
  s přesvědčením, že hodnota je 175 a že 371 je prokázaně málo — tedy s pravým
  opakem toho, co měření ukázalo. Riziko je konkrétní: „oprava" zpátky na 175,
  která znovu ubere ~1 procentní bod pásma SDRAM právě LTDC, jehož podtečení se
  celý F-0140 snažil dostat na nulu.
- **Reprodukce:** statické porovnání `fmc.c:54` vs `fmc.h:69`.
- **Návrh opravy:** `docs:` — blok přepsat tak, aby popisoval **dnešní** stav:
  hodnota 371, historie 1835 → 371, pokus 371 → 175 **a jeho zamítnutí měřením**,
  se závěrem, že příčinou byla čtecí cesta (`ReadPipeDelay`, I/O kompenzační cela).
  Historii nemazat — je poučná; jen ji označit jako uzavřenou, ne platnou.
- **Riziko opravy:** nulové (komentář).
- **Vztah k lekcím:** **L-0008** (komentář slibuje něco, co kód nedělá),
  **L-0018** (dvě místa o téže veličině se rozejdou — tady `fmc.c` vs `fmc.h`).
- **Stav:** **opraveno 2026-09-25** (`docs:`), ✅ **binárně ověřeno**. Opraveno
  **podle návrhu**: blok přepsán tak, aby popisoval dnešní stav — hodnota
  **371**, pokus o 175 označen jako **uzavřená slepá ulička zamítnutá měřením**
  (při 175 pořád 496 068 chybných bitů retence, tedy dvojnásobná rezerva
  nepomohla ⇒ obnova to není), a doplněna **skutečná příčina** nalezená
  2026-09-10 ve čtecí cestě FMC (`ReadPipeDelay` + I/O kompenzační cela).
  Historie ponechána — je poučná —, ale je z ní teď poznat, že je uzavřená.
  **Ověřeno:** `.text` **615112 B beze změny** před i po, build 0 varování ⇒
  změna je prokazatelně jen komentář.

---

### F-0151 [S4] `_Static_assert` u `REFRESH_COUNT` je tautologie — nemůže selhat, ale tváří se jako pojistka

- **Místo:** `CM7/Core/Src/fmc.c:110-111`.
- **Popis:**
  ```c
  #define REFRESH_COUNT REFRESH_COUNT_EXPECTED          /* fmc.c:80 */
  _Static_assert(REFRESH_COUNT == REFRESH_COUNT_EXPECTED,
                 "fmc.h REFRESH_COUNT_EXPECTED se rozeslo s REFRESH_COUNT v fmc.c");
  ```
  Po preprocesoru je to `_Static_assert(371 == 371, …)`. **Podmínka je splněná
  identicky** a nemůže být nikdy nesplněná, protože obě strany jsou tentýž
  symbol. Kontrola tedy nehlídá nic.
- **Důkaz:** `fmc.c:80` definuje `REFRESH_COUNT` **jako** `REFRESH_COUNT_EXPECTED`;
  `fmc.c:110` porovnává ta dvě jména. Jediný zdroj hodnoty je `fmc.h:69`.
- **Dopad:** Falešná jistota. Assert vznikl v době, kdy hodnota byla **ručně
  zdvojená** ve `fmc.c` a `fmc.h`, a tehdy rozchod skutečně hlídal. Po sjednocení
  na jediný zdroj (což je správná oprava) ztratil smysl, ale zůstal — včetně
  hlášky, která tvrdí, že rozchod hlídá. Kdo ho v budoucnu uvidí, uzavře, že
  konstanta je zajištěná, a nebude hledat skutečnou pojistku.
  ⚠️ Vada, před kterou dnes nikdo nechrání, je jiná a reálná: **rozchod hodnoty
  s taktem SDCLK** (`REFRESH_COUNT` je odvozený z 50 MHz; při změně SDCLK na
  100 MHz musí být 761 — viz `CLAUDE.md`). Tenhle vztah nehlídá nic.
- **Reprodukce:** statické; ověřitelné i tím, že změna `fmc.h:69` na libovolnou
  hodnotu překlad **neshodí**.
- **Návrh opravy:** Dvě varianty, obě malé:
  (a) **smazat** assert i s hláškou (poctivé: nic nehlídá);
  (b) **nahradit ho pojistkou, která hlídat může** — svázat hodnotu s taktem,
  např. `_Static_assert` nad přepočtem z konstanty SDCLK, takže změna děličky
  FMC nebo `SDClockPeriod` překlad zastaví. To je ta kontrola, o které si dnes
  komentář myslí, že existuje.
  🔑 Varianta (b) potřebuje rozhodnutí (odkud brát SDCLK staticky) → skupina B.
- **Riziko opravy:** (a) nulové; (b) nízké, ale vyžaduje ověřit, že zdroj taktu
  je v době překladu známý.
- **Vztah k lekcím:** **L-0070** („kontrola, která hlásí nálezy i ve zdravém
  stromě, přestává být kontrolou" — tady zrcadlově: kontrola, která **nikdy**
  nic nenahlásí, taky), **L-0020** (duplicitu převeď na kontrolu rozdílu — tady
  duplicita zmizela a kontrola po ní zůstala prázdná), **L-0039** (pozitivní
  kontrola musí obsahovat každou vadu, kvůli které vznikla). **Nová lekce L-0083.**
- **Stav:** **opraveno 2026-09-25**, ✅ **ověřeno pozitivní kontrolou** (na HW
  se ověřit nedá — je to kontrola překladu). Zvolena **varianta (b)**, tedy ta
  dražší: tautologie nahrazena mezí, která selhat **může**.
  `fmc.h` nově definuje `FMC_SDCLK_MHZ` / `SDRAM_ROWS` / `SDRAM_TREF_US` a z nich
  `REFRESH_COUNT_SPEC_MAX`; assert hlídá `REFRESH_COUNT_EXPECTED <= MAX + 1`.
  **Mez je jednostranná záměrně** — obnovovat se smí častěji, nikdy řidčeji;
  `+1` je zaokrouhlení (přesný vzorec dá 370,625, hodnota 371 je jeho
  zaokrouhlení nahoru), bez té tolerance by kontrola padala na správné hodnotě.
  🔑 **Pozitivní kontrola (`scratchpad/poz_kontrola_f0151.sh`) proběhla na OBOU
  vadách, kvůli kterým assert vznikl** — a to je to podstatné, protože nahradit
  tautologii další tautologií je přesně vzor L-0081:
  | případ | výsledek |
  |---|---|
  | nedotčený strom | **prošel** ✅ |
  | historická hodnota `1835` | **build spadl** ✅ |
  | SDCLK 50 → 25 MHz při hodnotě 371 | **build spadl** ✅ |
  | po obnovení | **prošel** ✅ |
  ⚠️ Assert by tedy **původní vadu z prvního commitu zachytil při překladu**.

---

### F-0152 [S3] `MX_FMC_Init` obsahuje 200 ms blokující `HAL_Delay` + blikání LED_1 — v boot cestě před bring-upem displeje

- **Místo:** `CM7/Core/Src/fmc.c:245-249` (`USER CODE FMC_Init 2`).
- **Popis:**
  ```c
      //Deactivate speculative/cache access to first FMC Bank to save FMC bandwidth
  //   FMC_Bank1->BTCR[0] = 0x000030D2;
      HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_RESET);
      HAL_Delay(200);
      HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_SET);
  ```
  Bezpodmínečné **200 ms blokující zdržení** plus zhasnutí a rozsvícení LED_1.
  Nic z toho nemá v kódu zdůvodnění a vypadá to jako pozůstatek z bring-upu.
  Nad tím dva řádky zakomentovaného mrtvého kódu.
- **Důkaz:** `MX_FMC_Init()` se volá z `main.c:382`; bring-up displeje (ATTINY
  probe → power-on → `HAL_DSI_Start` → `tc358762_init`) začíná až kolem
  `main.c:489` a končí `display_skip:` na `main.c:560`. Zdržení tedy leží
  **před** celým bring-upem a posouvá vše za ním o 200 ms.
- **Dopad:** `CLAUDE.md` pravidlo **4c** to zakazuje jmenovitě: *„Nesahej
  bezdůvodně na časování bootu. `printf`, `HAL_Delay` nebo blokující volání
  vložené do `main()` **před** bring-up displeje posouvá výše zmíněné závody."*
  Závody, o které jde, jsou dokumentované a měřené: ATTINY nabíhá vlastním
  tempem (probe má 10 pokusů po 100 ms), obě jádra závodí o GPIOG (#219/#208 —
  a právě sem patří i F-0149 výše), FPGA načítá konfiguraci z flash.
  Druhotně: LED_1 je výstup **`bootled`** pro hlášení poruch; její zhasnutí
  a rozsvícení uprostřed initu se dá splést se vzorem blikání poruchy.
- **Reprodukce:** statické. Dopad na boot je měřitelný: doba od resetu do
  prvního snímku by se měla zkrátit o ~200 ms.
- **Návrh opravy:** Odstranit obě `HAL_GPIO_WritePin` i `HAL_Delay(200)`
  a zakomentovaný `FMC_Bank1->BTCR[0]`.
  🔴 **ALE:** nelze vyloučit, že je zdržení **omylem nosné** — leží hned za
  inicializační sekvencí SDRAM a při studeném startu (rozbíhající se napájení
  SDRAM) může nechtěně krýt čas, který čip potřebuje. Proto to není skupina A:
  odstranění musí projít **studeným startem** s `membench` (retence 0, 0 chybných
  bitů) a `bgcheck` („BEZE ZMĚNY"), ne jen resetem po flashi.
  ⚠️ Alternativa, pokud se ukáže jako nosné: nahradit dokumentovaným, jmenovaným
  zdržením se zdůvodněním, ne anonymním pozůstatkem s blikáním LED.
- **Riziko opravy:** **střední** — ne kvůli složitosti (smazání tří řádků), ale
  kvůli tomu, že mění časování bootu, tedy přesně tu proměnnou, na kterou je
  tenhle přístroj citlivý.
- **Vztah k lekcím:** **L-0010** (ověření až po power-cyklu, ne po flashi),
  `CLAUDE.md` pravidlo 4c.
- **Stav:** 🔴 **ZAMÍTNUTO MĚŘENÍM NA HW 2026-09-25 — návrh byl ŠPATNÝ,
  zdržení je NOSNÉ.** Odstranění proběhlo (`fix`), studený start ho vyvrátil
  a zdržení bylo **vráceno** jako pojmenovaná konstanta
  `FMC_POST_INIT_SETTLE_MS` (commit `830ea2c`). Vznikla **lekce L-0084**.

  **Co se naměřilo po odstranění a STUDENÉM STARTU:** jádro přístroje bylo
  v naprostém pořádku — prošlo *všechno*, co předepisoval tenhle nález
  i kontrolní seznam modulu:

  | kontrola | výsledek |
  |---|---|
  | `g_fmc_init_fail` | **0** (sekvence SDRAM prošla všech 6 kroků) |
  | `g_display_init_step` | **0** (displej naběhl) |
  | `g_cm4_absent` | **0** (CM4 naběhl, zámek z F-0149 boot nezadrhl) |
  | `membench` | **0 chybných bitů**, retence **0** |
  | `bgcheck` | **BEZE ZMĚNY** |
  | `GPIO HLIDAC` | **0 oprav**, `uptime` normálně rostl |

  🔴 **A přesto přestala odpovídat USB CDC KONZOLE.** Zařízení se vyenumerovalo
  se správným `VID/PID 0483:5740`, ale data netekla při žádné kombinaci
  DTR/RTS. Po SW resetu se konzole **vždy** vrátila → rozdíl je výlučně
  ve studeném startu, ne v obrazu.
  🔑 **Rozbil se subsystém, který s auditovaným modulem nemá nic společného —
  a zrovna ten, kterým se všechno ostatní měří.** Bez sondy (`g_uptime_s`
  rostlo) by to vypadalo jako „deska po power-cyklu nenaběhla", přitom běžela
  normálně. Tohle je obsah L-0084 a důvod, proč byl ověřovací seznam v tomhle
  nálezu **nedostatečný**, i když byl splněn do puntíku.

  **Druhý kandidát vyloučen měřením, ne úvahou:** F-0153 přidává `errlog_put`
  do chybové ISR cesty USART1, což by při bouři chyb mohlo vyhladovět USB.
  `g_uart1_rearm_fail` i `g_gps_rx_drop` byly **0** a nerostly ⇒ žádná bouře.

  **Kontrolovaný pokus (jediná změněná proměnná = to zdržení):**
  | varianta | studený start |
  |---|---|
  | **bez** zdržení | konzole **mlčí** (opakovaně, ~5 min pokusů, obojí DTR/RTS) |
  | **se** zdržením | `Reset: power-on`, `uptime 22s`, `ping` → **`pong`** ✅ |

  **Po vrácení ověřeno po studeném startu:** `selftest` **16/16 PASS**,
  `bgcheck` BEZE ZMĚNY, `membench` 0 chybných bitů, displej OK,
  `LTDC` 0/285, `GPIO HLIDAC` 0, I2C4 `SCL=1 SDA=1 idle`, CM4 alive.

  ⚠️ **Mechanismus NENÍ znám** a komentár u konstanty to přiznává jako
  HYPOTÉZU (rozběh napájení? enumerační okno vůči hostu?). Jistá je jen ta
  závislost. ⚠️ Blikání LED_1 vráceno **nebylo** — na časování nemá vliv a
  LED_1 je výstup `bootled`, takže se pletlo se vzorem poruchy.
  ⚠️ **Poctivá výhrada:** je to n=1 proti n=1, byť s jedinou změněnou
  proměnnou. Kdo by na to zdržení sahal znovu, musí opakovat kontrolovaný
  pokus, ne se spolehnout na tenhle záznam.

---

### F-0153 [S3] Návrat `HAL_UART_Receive_IT()` se zahazuje v obou callbacích — jediné selhání, které GPS příjem zabije natrvalo, je jako jediné nepočítané

- **Místo:** `CM7/Core/Src/usart.c:197` (`HAL_UART_RxCpltCallback`)
  a `CM7/Core/Src/usart.c:224` (`HAL_UART_ErrorCallback`).
- **Popis:** Obě obsluhy končí `HAL_UART_Receive_IT(&huart1, &RxByte, 1);`
  bez uložení a vyhodnocení návratové hodnoty. Když se re-arm nepovede
  (`HAL_BUSY`, `HAL_ERROR`), **RX se už nikdy nenahodí** a GPS přestane
  dodávat data — tiše a bez jakéhokoli počítadla.
- **Důkaz:** `usart.c:197` a `:224` — volání jako samostatný příkaz, bez `if`
  a bez `(void)`. Kontrast se **zbytkem téhož souboru**, který je v tomhle
  ohledu vzorný: `usart.c:193-196` vyhodnocuje `osMessageQueuePut` a vede
  `g_gps_rx_drop` + `errlog_put`; `usart.c:209-215` rozlišuje `ORE/FE/NE/PE`
  do čtyř počítadel a loguje je.
- **Dopad:** `USART1` **není konzole, je to GPS NMEA vstup** (`usart.c:30-34`),
  takže mrtvý RX znamená ztrátu fixu, času i družic — a tedy i ztrátu
  disciplinace RTC (`rtc_try_sync`) a zdroje pro okno KVALITA GPS. Projeví se
  to jako „GPS přestala fungovat" bez jediné stopy v `status`, protože všechna
  existující počítadla měří jen chyby, které callback **zpracoval**.
  ⚠️ Komentář na `usart.c:220-221` tvrdí, že `HAL_UART_AbortReceive` zaručuje,
  že `Receive_IT` nevrátí `HAL_BUSY`. To je věta typu „hlídá to X" — a je
  testovatelná: dnes nic nečte, jestli to opravdu vyšlo.
- **Reprodukce:** HYPOTÉZA — z kódu nelze doložit, že re-arm reálně selhává.
  **Ověření na HW:** doplnit počítadlo (viz Návrh) a nechat běžet s GPS;
  vyvolat ORE opakovaným hot-plugem UART kabelu, což je přesně scénář, kvůli
  kterému `HAL_UART_ErrorCallback` vznikl.
- **Návrh opravy:** Uložit návrat a při ≠ `HAL_OK` zvýšit nové počítadlo
  (`g_uart1_rearm_fail`) + `errlog_put` (kind `ERRLOG_K_UART`, ISR-safe cesta
  už je v souboru použitá). Vypsat v `status` vedle stávajících `ORE/FE/NE/PE`.
  🔑 Počítadlo samotné situaci nespraví (RX zůstane mrtvý), ale **převede
  tichou trvalou poruchu na viditelnou** — a to je přesně to, co L-0017 žádá.
  Skutečné zotavení (re-init USART1) je větší zásah a patří do samostatného
  rozhodnutí.
- **Riziko opravy:** nízké — přidání počítadla nemění chování v úspěšné cestě.
  ⚠️ Zápis do `status` je v jiném souboru → dva soubory v jednom `fix:` commitu.
- **Vztah k lekcím:** **L-0003** (návratovou hodnotu vždy vyhodnoť),
  **L-0028** (věta „hlídá to X" je testovatelná; zahozená návratová hodnota je
  nejčastější podoba obrany, která neexistuje), **L-0017** (tichý přeskok je
  přípustný jen s počítadlem), **L-0016** (mez/porucha, kterou nelze odlišit
  od normálního provozu, je generátor tichých chyb).
- **Stav:** **opraveno 2026-09-25**, ⬜ **neověřeno na HW**. Opraveno **jinak,
  než nález navrhoval, a v menším rozsahu — po ověření, které návrh neudělal.**
  Návrh chtěl nové počítadlo **plus řádek ve `status`**, protože předpokládal,
  že stávající `g_uart1_*` nikdo nečte. **Ověření to vyvrátilo:** `grep` sice
  ukázal, že mimo `usart.c` se na ně nikdo neodkazuje, ale **`errlog_put()` je
  publikuje do trvalého záznamníku chyb při každé události** — a ten je čitelný
  přes UART `errlog` i přes web (`GET /api/errlog`). Počítadla tedy dosažitelná
  jsou a podmínka L-0017 (2) je splněná.
  Oprava proto jde **stejnou cestou jako její sousedé**: `g_uart1_rearm_fail`
  + `errlog_put(ERRLOG_K_UART, 0xFE, …, "REARM")`. Žádný zásah do `status`,
  žádný druhý soubor — **jeden soubor, jeden vzor**.
  🔑 Re-arm je vyčleněn do `uart1_rearm_rx()` (jediný zdroj pravdy pro obě
  obsluhy; dvě kopie téhož by se rozešly — L-0012/L-0018).
  ⚠️ **Počítadlo poruchu nespraví** — RX zůstane mrtvý. Převádí ji z tiché na
  viditelnou. Skutečné zotavení (re-init USART1) je samostatné rozhodnutí.
  **Ověřeno ve slinkovaném obrazu:** řetězec `REARM` je v `.elf` přítomen
  (`grep -ac` = 1), takže `--gc-sections` novou cestu nezahodil.
  🔑 **Co ověřit na desce:** `errlog` po opakovaném hot-plugu UART kabelu —
  musí přibývat záznamy `ERRLOG_K_UART` a GPS se musí vrátit k fixu.

---

### F-0154 [S4] `SDRAM_TIMEOUT` = 65 535 ms: chybová cesta inicializační sekvence trvá přes minutu, a to v době, kdy neběží watchdog

- **Místo:** `CM7/Core/Src/fmc.c:82` (`#define SDRAM_TIMEOUT ((uint32_t)0xFFFF)`),
  použito na `fmc.c:131,138,143,153`.
- **Popis:** `HAL_SDRAM_SendCommand()` bere timeout v **milisekundách**, takže
  `0xFFFF` = **65,5 s** na jeden příkaz. Sekvence má čtyři takové příkazy →
  teoreticky až ~4,4 minuty, než se vrátí číslo selhaného kroku.
- **Důkaz:** `HAL_SDRAM_SendCommand(SDRAM_HandleTypeDef*, FMC_SDRAM_CommandTypeDef*,
  uint32_t Timeout)` — poslední parametr jde do `HAL_GetTick()` porovnání
  v `FMC_SDRAM_SendCommand`. `fmc.c:131` atd. předávají `SDRAM_TIMEOUT`.
- **Dopad:** Malý a nepravděpodobný — FMC příkazová fronta se vyprazdňuje
  v mikrosekundách, takže se timeout v praxi nevyčerpá. Pokud by se ale řadič
  zasekl (a to je přesně ten stav, kvůli kterému se návratové hodnoty
  2026-09-07 začaly vyhodnocovat), přístroj stráví minuty v `MX_FMC_Init`
  s černým displejem a bez konzole. `watchdog_init()` běží až těsně před
  schedulerem, takže IWDG to neukončí.
  ⚠️ Hodnota pochází z ST příkladu — stejný původ jako `REFRESH_COUNT 1835`.
- **Reprodukce:** staticky; vyvolat reálně nelze bez vadného řadiče.
- **Návrh opravy:** Snížit na řádovou desítku až stovku ms (JEDEC sekvence
  nepotřebuje víc) a v komentáři uvést, že jednotka jsou **ms**, ne takty —
  právě ta záměna dělá z `0xFFFF` nevinně vypadající hodnotu.
- **Riziko opravy:** nízké, ale nenulové: zkrácení timeoutu je změna chování
  v chybové cestě, kterou nelze na HW vyvolat. Doporučeno až společně s jiným
  zásahem do `fmc.c`, ne samostatně.
- **Vztah k lekcím:** **L-0006** (konstanta převzatá pro jiný kontext — tady ne
  jiný takt, ale jiná jednotka/očekávání), **L-0004** (čekací smyčka s mezí,
  která je fakticky nekonečná).
- **Stav:** otevřeno.

---

## Co bylo zkontrolováno a je v pořádku

**`fmc.c` — parametry SDRAM (tohle byla hlavní otázka modulu):**
- `ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1` (`fmc.c:193`) — odpovídá opravě
  z 2026-09-10, která odstranila 3 338 207 chybných bitů. ✅
- `RowBitsNumber = 13` (8192 řádků) a `ColumnBitsNumber = 9` (`fmc.c:185-186`) —
  odpovídá osazenému `MT48LC16M16A2TG` (4 banky × 4M × 16). ✅
- **CAS latency je konzistentní na obou místech:** `Init.CASLatency =
  FMC_SDRAM_CAS_LATENCY_3` (`fmc.c:189`) i mode registr
  `SDRAM_MODEREG_CAS_LATENCY_3` (`fmc.c:147`). Rozchod těchto dvou je klasická
  tichá vada (řadič vzorkuje v jiném taktu, než čip vysílá) — tady není. ✅
- `SDClockPeriod_2` + FMC kernel 100 MHz → SDCLK 50 MHz, z čehož je odvozen
  `REFRESH_COUNT` 371. Přepočet `64e-3 · 50e6 / 8192 − 20 = 370,7` sedí. ✅
- **Návratové hodnoty celé sekvence se vyhodnocují** (`fmc.c:131,138,143,153,155`)
  a selhaný krok jde do `g_fmc_init_fail`, který hlásí `status`. Navíc `fmc.c:159`
  **čte zpátky `SDRTR` z HW** a ověřuje, že se refresh opravdu zapsal — to je
  přesně **L-0055** („hodnotu, kterou HW nemusí přijmout, po zápisu přečti
  zpátky"). Nadprůměrně dobré. ✅
- `HAL_SDRAM_Init` selhání → `Error_Handler()` a `bootled_step(BOOTLED_STEP_FMC)`
  je nastaven **před** ním (`fmc.c:171`), takže porucha bliká a pípá svým číslem
  (ne tiše, jako v F-0007). ✅
- `HAL_Delay(1)` po `CLK_ENABLE` (`fmc.c:133`) splňuje JEDEC ≥ 100 µs. ✅

**`fmc.c` — re-init za běhu (`sdraminit`):**
- Vyčlenění sekvence do `fmc_sdram_init_sequence()` je správné a příkaz běží
  z **UartTasku** (`freertos_task_uart.c:915-931`), tedy z jediného tasku, který
  watchdog nehlídá — což je nutné, protože sekvence obsahuje `HAL_Delay`. ✅
- Příkaz **nepřepisuje** `g_fmc_init_fail` (ukládá do lokální `r`), takže
  diagnostický běh nepřebije bootovací výsledek v `status`. Vyhodnoceno jako
  **záměr, ne vada**. ✅
- PALL / AUTOREFRESH / LOAD_MODE obsah SDRAM neruší (mode registr se přepisuje
  toutéž hodnotou), takže framebuffery přežijí; možné krátké probliknutí je
  v komentáři přiznané. ✅

**`usart.c`:**
- **Příznaky chyb se čtou PŘED mazáním** (`usart.c:208` uloží `huart->ErrorCode`,
  teprve `:216-219` mažou) — to je přímo **L-0016**, a je splněná. ✅
- `HAL_UART_AbortReceive` před re-armem (`usart.c:222`) řeší past „po chybě
  zůstane `RxState` = BUSY → RX se už nikdy nenahodí". ✅
- `errlog_put` z ISR jen do RAM ringu, vylití do flash dělá defaultTask —
  odpovídá **L-0071** (z kontextu výjimky se nezapisuje). ✅
- Rozlišení `ORE`/`FE`/`NE`/`PE` do čtyř počítadel + drop fronty zvlášť. Splňuje
  **L-0017** pro vše kromě re-armu (viz F-0153). ✅
- `Usart16ClockSelection = D2PCLK2` (`usart.c:102`) — zdroj hodin je explicitní,
  ne implicitní default. ✅

**Nízkoprioritní soubory** (`adc.c`, `dsihost.c`, `ltdc.c`, `quadspi.c`, `spi.c`,
`tim.c`, CM4 `eth.c`): `USER CODE` bloky obsahují **výhradně** `#include "bootled.h"`
a jedno `bootled_step(...)`. Žádná logika, žádný nález. Ověřeno extrakcí obsahu
všech `USER CODE` bloků. ✅
⚠️ `dsihost.c` je nositelem load-bearing konfigurace (`DSI_VID_MODE_BURST` +
`DSI_RGB565`) — ta je ale **v generované části**, ne v `USER CODE`, takže sem
nespadá. Patří k modulu 8 / `CLAUDE.md` „Funkční konfigurace displeje".

## Checklist — relevantní položky

| sekce | položka | verdikt |
|---|---|---|
| A | hodiny periferie povoleny před zápisem | OK (`__HAL_RCC_FMC_CLK_ENABLE` v MspInit, `fmc.c:266`) |
| A | konstanta odvozená z hodin (`REFRESH_COUNT`) | hodnota **OK (371)**, ale dokumentace lže → F-0150; vazba na SDCLK nehlídaná → F-0151 |
| A | I/O kompenzace (CCCSR) pro rychlé FMC | OK — zapíná se v `main.c` `USER CODE SysInit` před `MX_FMC_Init` (modul 1) |
| B | sdílená GPIO mezi jádry | **NÁLEZ F-0149** |
| D | `volatile` u sdílených proměnných | OK (`g_fmc_init_fail`, `g_fmc_init_runs`, `g_uart1_*`) |
| E | návratové hodnoty HAL | OK ve `fmc.c`; **NÁLEZ F-0153** v `usart.c` |
| E | čekací smyčka s timeoutem | formálně OK, ale mez je 65 s → **F-0154** |
| E | rozpoznání vypadlé periferie (UART ORE/FE/NE) | OK, nadprůměrné |
| G | FMC/SDRAM: refresh z reálné HCLK | OK (přepočet sedí) |
| G | UART: `ORE` ošetřen | OK |
| H | warningy nejsou vypnuté | OK — `fmc.c` je jeden ze **2** souborů baseline `audit.py` 92/0/2 (2× `sdramHandle` nepoužitý parametr v generovaných Msp funkcích, benigní) |

## Shrnutí

**Verdikt: podmíněně funkční.**

Modul obsahuje **jeden S2** (F-0149 — jednostranný GPIO zámek v obranném kódu
PG8, souběh s CM4 doložen pořadím bootu), **dva S3** (F-0152 zdržení bootu,
F-0153 nepočítané selhání re-armu GPS RX) a **tři S4**.

🔑 **Nejcennější zjištění není žádná z jednotlivých vad, ale to, že komentář
u PG8 vyloučil CM4 jako původce na základě premisy, kterou boot sekvence
popírá.** Tím se uzavřelo pátrání po příčině přepisu PG8 → ANALOG, která
způsobila černý displej a 10,5 milionu chybných bitů, a zůstala jen obrana
(`gpio_guard_tick`), která vadu opravuje 1×/s — tedy dávno po bring-upu displeje.

Zároveň platí, že `fmc.c` je v podstatných věcech (návratové hodnoty, zpětné
čtení `SDRTR`, konzistence CAS latency) napsaný **lépe než průměr projektu** —
riziko tu nebylo v tom, že by šlo o generovaný kód, ale v tom, že se na něj
kvůli zařazení „generovaný" nikdo systematicky nepodíval.
