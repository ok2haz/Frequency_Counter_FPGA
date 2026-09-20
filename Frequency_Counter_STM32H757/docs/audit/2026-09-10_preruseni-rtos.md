# Audit: přerušení a RTOS  (2026-09-10)

- **Commit:** `f9a97c4` (branch `audit/2026-09-09-hodiny-pwr`)
- **Jádro / doména:** CM7 (FreeRTOS), okrajově CM4 (bare-metal)
- **Projité sekce checklistu:** D (celá), E (chybové cesty hooků a `configASSERT`),
  B (priority a NVIC per jádro), A (timebase — dokončeno z modulu 1)
- **Neprojité (a proč):** C — patří modulu 2; F, G, H bez vazby na tento modul.
- **Soubory čtené celé:** `CM7/Core/Src/freertos_hooks.c` (76 ř.), `CM7/Core/Inc/FreeRTOSConfig.h`
  (220 ř.), `CM7/Core/Src/stm32h7xx_it.c` (334 ř.). **Cíleně:** `CM7/Core/Src/freertos.c`
  (definice tasků, HSEM guard), `freertos_task_uart.c` (dlouhé příkazy), `usart.c`
  (RX/Error callback), `watchdog.c`, `flightrec.c`, `CM4/Core/Src/stm32h7xx_it.c`,
  `Middlewares/.../tasks.c`, `.../portable/GCC/ARM_CM4F/port.c`, `.../CMSIS_RTOS_V2/cmsis_os2.c`.
- **Stacky: dohledány z běžícího přístroje** (UART `stats` po power-cyklu), ne odhadem:
  IDLE 420 B, Uart 1024 B, I2C4 932 B, Ui 5268 B, Fpga 988 B, default 1728 B, Tmr Svc 852 B volných.

## Souhrn

Základ je postavený správně a několik věcí je nadprůměrných: `configASSERT` nejen zastaví, ale
**zapíše důvod i řádek do crash black-boxu**, hooky přetečení stacku a `malloc` zapisují jméno
tasku do BKP **před** tím, než se zacyklí, a IWDG z toho udělá auto-recovery. Priority ISR jsou
všechny ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`, priority grouping je 4, HAL timebase je na TIM6
a v obsluhách nejsou `printf`, `HAL_Delay`, `malloc` ani `osDelay`.

Nálezy jsou dva funkční a jeden kosmetický, a **oba funkční mají společné téma: diagnostika,
která v okamžiku poruchy nefunguje.** F-0018 je cesta, která se tváří, že zaznamená kontext před
pádem, a přitom deterministicky neudělá nic. F-0020 je příkaz, který má rozhodnout o mrtvé I2C4,
ale právě když je I2C4 mrtvá, shodí přístroj watchdogem — a obviní přitom nesprávný task.

**Verdikt: podmíněně funkční.** Provoz to neohrožuje, ale dvě diagnostické cesty selhávají
přesně v situaci, pro kterou byly napsané.

---

### F-0018 [S1] `flightrec_dump()` volaný z hooku přetečení stacku deterministicky nezapíše nic

- **Místo:** `CM7/Core/Src/freertos_hooks.c:63` (`flightrec_dump("stack")`)
  vs. `CM7/Core/Src/flightrec.c:270`
- **Popis:** Hook přetečení stacku běží v **kontextu výjimky PendSV**, ale `flightrec_dump()`
  hned na začátku sahá na mutex. CMSIS-RTOS2 v kontextu přerušení mutex nedá — vrátí chybu —
  a funkce se na téže řádce **vrátí bez jakékoli akce**. Letový zapisovač tedy pro přetečení
  stacku nezaznamená nikdy nic, a nijak to neohlásí.
- **Důkaz (řetěz je uzavřený, nic se nedomýšlí):**
  1. `Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c:484` —
     `xPortPendSVHandler` obsahuje `bl vTaskSwitchContext`.
  2. `Middlewares/Third_Party/FreeRTOS/Source/tasks.c:3112` — `vTaskSwitchContext()` volá
     `taskCHECK_FOR_STACK_OVERFLOW()`, které při `configCHECK_FOR_STACK_OVERFLOW = 2`
     (`FreeRTOSConfig.h:190`) volá `vApplicationStackOverflowHook()`.
     → **hook běží uvnitř PendSV.**
  3. `Middlewares/.../CMSIS_RTOS_V2/cmsis_os2.c:1763-1765` — `osMutexAcquire()` začíná
     `if (IRQ_Context() != 0U) { stat = osErrorISR; }`.
  4. `flightrec.c:270` — `if (osMutexAcquire(qspiMutexHandle, FR_LOCK_MS) != osOK) return;`
  → v PendSV je návratová hodnota vždy `osErrorISR`, tedy vždy `return`.
- **Dopad:** Ztrácí se přesně to, kvůli čemu letový zapisovač existuje — 60 s kontextu
  (CPU, heap, nejmenší volný stack, teploty, chybovost I2C) před pádem. **Přístroj to
  neohrozí** a *která* úloha přetekla se pozná: crash black-box (BKP) se zapisuje **před**
  tím (`freertos_hooks.c:62`) a ten funguje, protože jde o přímé zápisy do registrů.
  Ztráta je čistě diagnostická — ale tichá, což je horší než hlášená.
  ⚠️ `vApplicationMallocFailedHook` (`:73`) volá totéž, ale ten běží v **kontextu úlohy**
  (`pvPortMalloc`), takže tam mutex projde a dump se provede. Vada je specifická pro stack hook.
  ⚠️ Projekt zná i správný vzor: `errlog_put()` je v `errlog.h:85` explicitně označený
  **ISR-SAFE** (jen RAM ring, do flash to vyleje `errlog_tick`) a `HAL_UART_ErrorCallback`
  ho z ISR správně používá. Na `flightrec` se ten vzor jen nepřenesl.
  ⚠️ V `CLAUDE.md` je u letového zapisovače napsané „Nezapisuje se z HardFault handleru
  (v exception kontextu by zatuhlo)“ — omezení bylo tedy známé, jen se nedotáhlo na tenhle
  druhý exception kontext.
- **Reprodukce:** Deterministické. Na HW: `stacktest yes` (záměrně přeteče stack UartTasku) →
  po restartu `status` ukáže `stack:UartTask` z black-boxu, ale `flightrec` **žádný nový
  záznam nemá**.
- **Návrh opravy:** Rozdělit zápis na dvě fáze, stejně jako to má `errlog`: z hooku jen
  **označit v RAM**, že se má dump provést (a nechat data v ringu), a vlastní zápis do flash
  udělat po restartu nebo z úlohy. Alternativa (horší): v hooku detekovat kontext výjimky
  (`__get_IPSR() != 0`) a jít na flash **bez** mutexu — je to bezpečné jen proto, že hned
  potom stejně následuje `__disable_irq()` a spin, ale `w25q wait_ready()` uvnitř volá
  `osDelay(1)`, který v PendSV rovněž neprojde, takže by se z toho stal spin až do IWDG.
  **Minimum, které stojí za to udělat hned:** ať `flightrec_dump()` při odmítnutí mutexu
  zvýší počítadlo, aby ta ztráta přestala být tichá.
- **Riziko opravy:** nízké u počítadla; střední u přestavby na dvoufázový zápis.
- **Vztah k lekcím:** `L-0011` (nástroj hlásí, že něco dělá, a nedělá) — a nová lekce po opravě:
  „hook FreeRTOS není kontext úlohy“.
- **Stav:** **opraveno 2026-09-20** — dvoufazovy zapis pres SDRAM, tedy
  presne to, co nalez doporucoval (a co uz dela `errlog`).
  - `flightrec_dump()` nove **VZDY nejdriv** slozi dump do sekce `.sdram`
    (`fr_stage_pending`) — jen bajtove zapisy, zadny mutex, zadny `osDelay`, takze je
    to bezpecne i z PendSV.
  - Z kontextu vyjimky (`__get_IPSR() != 0`) se na mutex uz **vubec nesaha** a funkce
    se vraci; dump ceka v SDRAM.
  - Po restartu ho vylije `flightrec_init()` (`fr_flush_pending`), kde uz mutex drzime
    a flash je pripravena. Az tim ma zapisovac pro scenar STATUS #18 vubec smysl.
  - ⚠️ **Proc SDRAM a ne RAM:** `.bss` maze `Reset_Handler`, takze staging v RAM by se
    pri resetu ztratil. `.sdram` je NOLOAD, startup na ni nesaha a obsah SDRAM prezije
    reset — tentyz duvod, proc boot musi framebuffer memsetovat na cerno.
  - ⚠️ **Device pamet:** buffer je v `.sdram`, kde je nezarovnany 32bitovy pristup
    UsageFault (past F-0012). Bezpecne to je proto, ze `hdr_pack`/`rec_pack` plni
    buffer VYHRADNE pres `put16`/`put32`, a ty zapisuji **po bajtech** (overeno
    v `flightrec.c:55-57`). Buffer je navic `aligned(32)`.
  - ⚠️ Po **power-cyklu** je SDRAM nahodna -> platnost se overuje magicem `"FRP1"`
    a delkou v rozsahu. Pro pretečeni zasobniku to staci: po nem nasleduje IWDG reset,
    ne odpojeni napajeni.
  - Viditelnost: nove pocitadlo `g_flightrec_staged` a radek v `status`
    („N dumpu zachranenych po restartu"). `g_flightrec_lost` zustava pro skutecnou
    ztratu (nedostal mutex mimo kontext vyjimky).
  - Z opravy vznikla **`L-0071`**. ⬜ **neovereno na HW** (kriterium: `stacktest yes`
    -> po restartu `status` hlasi `zachranenych po restartu 1` a `flightrec` vypise
    60 s pred padem).

---

### F-0020 [S2] `scanner` může vyhladovět UiTask přes práh watchdogu — a nejhůř právě když je I2C4 mrtvá

- **Místo:** `CM7/Core/Src/freertos_task_uart.c:699-701` (příkaz `scanner`),
  `CM7/Core/Src/watchdog.c:23` (`WDG_STALL_MS`), `CM7/Core/Src/freertos.c:520,526`
  (priority tasků)
- **Popis:** `scanner` projde 127 adres blokujícím `HAL_I2C_IsDeviceReady(&hi2c4, addr, 3, 10)`
  **bez jediného ústupu scheduleru**. Běží v UartTasku s prioritou **Normal**, zatímco UiTask
  má **BelowNormal** — po celou dobu skenu tedy UiTask nedostane procesor a jeho heartbeat
  stárne. Watchdog obnovuje IWDG jen když jsou **oba** heartbeaty čerstvé.
- **Důkaz:**
  - `freertos_task_uart.c:699-701`: `for (uint16_t i = 1; i < 128; i++) { … HAL_I2C_IsDeviceReady(&hi2c4, (uint16_t)(i << 1), 3, 10); }`
    — v tom bloku není žádné `osDelay` (na rozdíl od `fpgaloop`, který na
    `freertos_task_uart.c:1602` `osDelay(1)` **má**, takže ten problém nemá).
  - `freertos.c:520` UiTask `osPriorityBelowNormal`, `:526` UartTask `osPriorityNormal`.
  - `watchdog.c:23` `#define WDG_STALL_MS 2500u`; `watchdog_supervise()` obnoví IWDG
    jen v `if (!ui_stale && !fpga_stale)`, jinak nechá watchdog vypršet.
  - `CLAUDE.md` sama uvádí dobu běhu příkazu: „`scanner` ~2,5 s“ — což se rovná prahu
    **na milisekundu**, tedy nulová rezerva.
  - Horní odhad při nereagující sběrnici: 127 adres × 3 pokusy × 10 ms timeout = **3,8 s**,
    tedy jistě přes práh. Když slave ACKne nebo NACKne rychle, sken je mnohem kratší —
    proto to za normálního stavu prochází.
- **Dopad:** Za zdravé sběrnice nejspíš projde (proto se to dosud neprojevilo). **Jakmile
  ale I2C4 přestane odpovídat** — přesně stav, který je v projektu zdokumentovaný a kvůli
  kterému `scanner` člověk spouští — každá adresa spotřebuje plný timeout, sken přeleze
  2,5 s a IWDG přístroj resetuje.
  🔑 A druhá polovina dopadu je horší než reset: crash black-box zapíše
  **`stall:UiTask`** (`watchdog.c` volá `stall_blackbox("UiTask")`), protože UiTask má
  starý heartbeat. UiTask přitom není zaseknutý — je **vyhladovělý**. Diagnostika tedy
  ukáže na nesprávnou úlohu, což je podle projektových pravidel horší než mlčet.
- **Reprodukce:** `HYPOTÉZA — ověřit:` na HW vyvolat stav „I2C4 neodpovídá“ (podle
  zdokumentovaného postupu) a spustit `scanner`; očekává se reset a po něm
  `status` → `Reset: WATCHDOG!` + `stall:UiTask`. Bez HW se dokládá jen výpočtem výše.
- **Návrh opravy:** Do smyčky skenu vložit `osDelay(1)` po každé adrese (127 ms navíc,
  zanedbatelné) — týž vzor, jaký už `fpgaloop` používá. Tím UiTask dostane procesor a
  heartbeat nezestárne. Doplňkově zvážit snížení timeoutu z 10 ms na 2–3 ms; NACK přijde
  řádově dřív a u nereagující sběrnice to zkrátí sken čtyřnásobně.
- **Riziko opravy:** nízké — `scanner` běží v nehlídaném UartTasku a o pár set ms delší
  sken nikomu nevadí.
- **Vztah k lekcím:** `L-0004` je příbuzná (čekání bez rozumné meze); nová lekce po opravě:
  „úloha s vyšší prioritou, která blokuje déle než práh watchdogu, resetuje přístroj
  jménem cizí úlohy“.
- **Stav:** opraveno 2026-09-10 — `osDelay(1)` do smyčky skenu; cena 127 ms na sken.

---

### F-0019 [S4] Řádek `HSE: CSS hlasil vypadek Nx` ve `status` se nemůže nikdy vypsat

- **Místo:** `CM7/Core/Src/freertos_task_uart.c` (blok `status`, výpis `HSE:`),
  `CM7/Core/Src/freertos.c:272` (`volatile uint16_t g_css_fail;`),
  `CM7/Core/Src/stm32h7xx_it.c` (`NMI_Handler`)
- **Popis:** Počítadlo výpadků reference se zvyšuje v `NMI_Handler`, jenže ten hned poté
  zamrzne a přístroj resetuje IWDG. `g_css_fail` je obyčejná proměnná v `.bss`, kterou
  reset vynuluje — takže hodnota, kterou má `status` vypsat, do `status` nikdy nedojde.
- **Důkaz:** `freertos.c:272` — `g_css_fail` je v `.bss` (žádná sekce, žádný BKP registr).
  `NMI_Handler` po zvýšení počítadla pokračuje do `while (1) { }` (viz audit modulu 1,
  nález F-0002), takže se `status` mezitím spustit nemůže; po IWDG resetu je `.bss`
  vynulovaná. Podmínka výpisu `if (g_css_fail)` je tedy vždy nepravdivá.
- **Dopad:** Kosmetický — trvalý záznam o výpadku HSE **existuje** a funguje: crash
  black-box (kind 7) po restartu ukáže `NMI@<RCC_CR>`. Ztrácí se jen ta živá indikace,
  která by stejně neměla kdy zafungovat.
- **Reprodukce:** Deterministické z kódu; na HW by šlo `css on` + odpojení HSE.
- **Návrh opravy:** Buď počítadlo přesunout do volného BKP registru (přežije reset a řádek
  začne dávat smysl), nebo řádek ze `status` odstranit a spolehnout se na black-box.
  První je o kousek užitečnější, druhé levnější.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** `L-0008` (kód slibuje výstup, který nemá jak vzniknout).
- **Stav:** opraveno 2026-09-10 — čítač se zapisuje i do `RTC->BKP11R` (za povolením `DBP`) a `status` ho čte odtud, takže řádek přestal být nedosažitelný.

---

### F-0055 [S1] `selftest` z UART přeteče zásobník UartTasku a shodí desku

- **Místo:** `CM7/Core/Src/freertos_task_uart.c` (příkaz `selftest` → `run_selftests()`),
  velikost zásobníku `CM7/Core/Src/freertos.c:564` (`UartTask_attributes.stack_size = 1024*4`)
- **Popis:** UART příkaz `selftest` **deterministicky přeteče zásobník UartTasku**.
  Hook to zachytí, zapíše do crash black-boxu a IWDG desku resetuje.
- **Důkaz — změřeno na desce 2026-09-11, reprodukováno 2× za sebou:**
  - Výstup se utne vždy na stejném místě (po pěti vypsaných testech, uprostřed
    závěrečného řádku `SELFTE…`) a USB CDC konzole odpadne.
  - Po restartu `status`:
    ```
    RUNNING gpsdo-ui v0.8.1  uptime 27s
    Reset: WATCHDOG!  stack:UartTask
      RSR=0x04460000  <-- WATCHDOG/CRASH
    ```
    Tedy přesně ten řetězec, kterým TODO #10 ověřovalo detekci přetečení
    (`stacktest yes`) — jen ho tentokrát vyvolal běžný diagnostický příkaz.
  - **Rozpočet:** `UartTask` má **4096 B**, změřený high-water `free` je **976 B**
    (`status` → `stack Uart free 976 B`), tedy běžný vrchol ~3120 B. Do těch 976 B se
    musí vejít celý řetěz `run_selftests()`. Změřené rámce (`objdump`, `sub sp,#N`):
    `gps_selftest` **468 B**, `mp_selftest` **428 B**, `scpi_selftest` **264 B** →
    `scpi_exec_one` **260 B**, `ipc_selftest` 228 B, `datalog_selftest` 196 B.
    Už dvojice `scpi_selftest → scpi_exec_one` s uloženými registry přesahuje 600 B
    a `scpi_process_ctx` mezi nimi se ještě přičítá.
    ⚠️ Přesný nejhlubší řetěz **není určen** — `objdump` ukáže rámec funkce, ne
    nejhlubší cestu; na to je potřeba `-fstack-usage` (metoda ze STATUS #146).
  - 🔴 **Proč to dosavadní audit zásobníků nechytil:** STATUS #146 (2026-09-05) měřil
    `-fstack-usage` proti `osThreadGetStackSpace`, vyšlo mu **UartTask 33 % volno** a
    uzavřel to větou *„Ostatní tasky mají dost — neměnit naslepo."* Měřil ale **běžný
    provoz**, ne stav, kdy z UartTasku poběží `run_selftests()`. Na běžný provoz je
    33 % dost, na selftest ne.
  - `CLAUDE.md` přitom `selftest` vede jako **nástroj č. 4 v pořadí „nejdřív měř"**
    s cenou **„zdarma"**, a u `run_selftests` má zdokumentované **tři** volající včetně
    „UartTask `selftest`". Metodika projektu tedy doporučuje příkaz, který shodí desku.
- **Dopad:** Deterministický a **destruktivní**: reset přístroje (ztráta uptime,
  přerušené měření, zahozená statistika). Navíc **falešná stopa při diagnostice** —
  po resetu `status` hlásí `WATCHDOG! stack:UartTask`, což vypadá jako samovolný pád,
  zatímco to způsobil právě ten příkaz, kterým se porucha měla vyšetřovat.
  ⚠️ Boot-time selftest (z `defaultTask`, stack 3584 B, volno 1712 B) je v pořádku —
  vada je **jen** na cestě z UartTasku.
- **Reprodukce:** `selftest` na konzoli → výpis se utne → po ~4 s reset →
  `status` ukáže `Reset: WATCHDOG!  stack:UartTask`. **Ověřeno 2×.**
- 🔑 **NENÍ to regrese z oprav 2026-09-11** — doloženo dvěma nezávislými způsoby:
  1. Obě verze `freertos_task_uart.c` (stav před `d3a099e` a po dnešních commitech)
     přeloženy **týmiž** flagy z `CM7/Release/Core/Src/subdir.mk` → rámec
     `UartTask_run` je **700 B v obou**.
  2. Každý měřitelný rámec na cestě selftestu leží v souborech, kterých se dnešní
     opravy nedotkly (`scpi.c`, `gps.c`, `meas_present.c`, `ipc.c`). Z měněných
     souborů přispívá `screen_main_selftest` **36 B** a `app_gpsdo_selftest` **0 B**.
- **Návrh opravy:** Tři cesty, liší se cenou — **rozhodnutí patří uživateli**:
  1. **Zvětšit zásobník UartTasku** (4096 → 5120 B). Nejlevnější, ale ubírá ze společné
     haldy (32768 B) a jen posouvá hranici. ⚠️ Změnit i v `.ioc`, jinak to regen vrátí
     (stejně jako #146 u defaultTasku).
  2. **Nespouštět `run_selftests()` z UartTasku** — příkaz jen nastaví požadavek a
     vykoná ho úloha, která má rezervu (vzor `g_membench_req`/`g_sd_req`, v projektu
     zavedený). Nejčistší, ale mění, kdo test vlastní.
  3. **Zmenšit nejtěžší rámce** (`gps_selftest` 468 B, `mp_selftest` 428 B → statické
     buffery, jako se to udělalo u `pn_selftest` v #45). Dlouhodobě nejlepší, protože
     pomůže i ostatním volajícím, ale je to nejvíc práce.
  ⚠️ **Než se sáhne na kterýkoli rámec, změř to `-fstack-usage`**, ne `objdump`.
- **Riziko opravy:** varianta 1 nízké, varianta 2 střední (mění vlastnictví testu),
  varianta 3 nízké na kus, ale dotkne se víc souborů.
- **Vztah k lekcím:** **`L-0016`** (mez i měřidlo její rezervy se navrhují společně —
  rezerva se tu měřila, ale ne ve stavu, ve kterém dochází) a **nová lekce po opravě**:
  „rezervu zásobníku měř v NEJHORŠÍM dosažitelném stavu úlohy, ne v běžném".
- 🔴 **NOVÝ DŮKAZ Z DESKY 2026-09-12 — rezerva je horší, než tenhle nález měřil.**
  Při běžném `scpi …` z konzole (tedy **bez** `selftest`) hlásil `status`
  **`stack Uart free 168 B`** ze 4096 B, zatímco tenhle nález pracoval s 976 B.
  Řetěz je `UartTask_run (700 B) + scpi_process (492 B) + scpi_process_ctx +
  scpi_exec_one (268 B)` ≈ **1,6 kB jen v rámcích**; 976 B bylo změřeno v běhu,
  kde se SCPI nepoužilo. **Vrchol tedy není 3120 B, ale 3928 B.**
  Dílčí úleva: `21ac04e` přesunul `sub`/`rb` ve `scpi_process_ctx` do `.bss`
  (−140 B proti stavu před modulem 13), ale **podstata nálezu tím nemizí** —
  `scpi_process` drží `scpi_src_t src` na zásobníku (492 B) a `run_selftests`
  přidává dalších ~1,2 kB.
  🔑 Pro rozhodnutí mezi třemi variantami to znamená: **varianta 1 (zvětšit
  zásobník) už nestačí na „posunutí hranice" — 4096 B je pod potřebou i bez
  selftestu.** Varianta 2 (spouštět testy z úlohy, která má rezervu) řeší
  selftest, ale ne SCPI cestu; tu by řešilo jedině zvětšení zásobníku
  **nebo** přesun `scpi_src_t` mimo stack.
- **Stav:** **vse pripravene, ceka VYHRADNE na HW overeni (2026-09-20).**
  Oba kroky, ktere nalez pozadoval, jsou hotove:
  1. ✅ **zasobnik UartTasku zvetsen** — `.ioc` ma `UartTask, 24, 2048` (slov) a
     `freertos.c` `stack_size = 2048 * 4` = **8192 B** (bylo 4096 B). Udelal uzivatel.
  2. ✅ **velke lokaly prikazu do `.bss`** — `resp[128]` a `buf[512]`, viz **F-0074**
     a **F-0077**. Dolozeno v obrazu (`resp.14` 128 B, `buf` 512 B v `.bss`).
  🔴 **PORAD JE TO OTEVRENE, protoze zadny z tech kroku nebezel na desce** — a presne
  na tohle upozornuje pravidlo „prelozeno neni provereno". Puvodni vada byla
  DETERMINISTICKA a zmerena 2x (`Reset: WATCHDOG! stack:UartTask`), takze dukaz o oprave
  musi byt taky z desky.
  ⚠️ **Dve konkurencni opravy naraz — merіtelnost je obetovana vedome.** TODO #243
  zadavalo delat je ODDELENE prave proto, aby se poznalo, ktera pomohla; uzivatel se
  2026-09-20 rozhodl udelat obe. Kdyz `selftest` projde, bude to znamenat „uz to
  nepadá", ne „vime cim".
  **Kriterium:** `selftest` z konzole dobehne **„SELFTEST: 16/16 PASS" bez resetu**,
  `status` -> `Reset:` nehlasi `stack:UartTask`, a `stats` ukaze volny stack UartTasku
  (ocekavany rad: ~4 kB vic nez drive).
  ⬜ **neovereno na HW.**

---

## Co bylo zkontrolováno a je v pořádku

**D. Priority a souběh**
- **Všechny NVIC priority jsou ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY` (5):** USART1 = 5,
  SDMMC1 = 5, OTG_FS = 5, TIM7 = 6, TIM6 (HAL tick) = `TICK_INT_PRIORITY` = **15**
  (`stm32h7xx_hal_conf.h:168`), PendSV = 15. Žádná obsluha, která volá RTOS API, není nad
  prahem. Nejtěsnější je USART1 s hodnotou přesně 5, což je povolené.
- **Priority grouping je `NVIC_PRIORITYGROUP_4`** — nastavuje ho `HAL_Init()`
  (`Drivers/.../stm32h7xx_hal.c:147`), takže všechny 4 bity jsou preempční, jak port vyžaduje.
- **`configASSERT` je definovaný a užitečný** (`FreeRTOSConfig.h:166`): zapíše `rtc_crash_assert(__LINE__)`
  do black-boxu, teprve pak zakáže přerušení a zacyklí — IWDG z toho udělá auto-recovery
  a po restartu je vidět **řádek**, na kterém to prasklo. Tím je zároveň aktivní
  `vPortValidateInterruptPriority`, tedy runtime kontrola priorit ISR.
- **RTOS API se z ISR volá jen přes CMSIS wrapper** (`usart.c:191` `osMessageQueuePut` v RX
  callbacku, SDMMC1 obsluha), který ISR kontext sám pozná a použije `…FromISR` variantu.
  V našem kódu není žádné přímé `…FromISR`.
- **V žádné obsluze přerušení není `printf`, `HAL_Delay`, `malloc` ani `osDelay`** (ověřeno
  grepem přes `stm32h7xx_it.c` obou jader).
- **HAL timebase je na TIM6, ne na SysTick** (dokončeno v modulu 1), takže si ho FreeRTOS
  nebere.

**E. Chybové cesty**
- **Hooky zapisují black-box PŘED tím, než se zacyklí** (`freertos_hooks.c:62`, `:72`) a
  ukládají jméno přetečeného tasku — po IWDG resetu je tedy vidět `stack:<task>` /
  `malloc fail`. Zápis jde přímo do `RTC->BKPxR` (bez HAL), což je v tomto kontextu správně.
- **`HAL_UART_ErrorCallback`** (`usart.c:202-224`) je vzorový: spočítá ORE/FE/NE/PE, zaloguje
  ISR-safe cestou, vyčistí příznaky, zavolá **`HAL_UART_AbortReceive`**, vynuluje `ErrorCode`
  a teprve pak znovu nahodí `Receive_IT`. Přesně to, co dřív chybělo a dělalo mrtvou konzoli.
- **`errlog_put()` je explicitně ISR-SAFE** (`errlog.h:85`) — jen RAM ring, do flash to vyleje
  `errlog_tick` z úlohy. Správný vzor, který F-0018 postrádá.

**Stacky (změřeno na běžícím přístroji, ne odhadnuto)**
- `stats` po power-cyklu: IDLE **420 B** volných z 512 B (`configMINIMAL_STACK_SIZE` = 128 slov),
  Uart 1024 B, I2C4 932 B, Ui 5268 B, Fpga 988 B, default 1728 B, Tmr Svc 852 B.
  Všechny mají kladnou rezervu; nejtěsnější je I2C4Task s 932 B, což je stále přes 20 %.
- `configCHECK_FOR_STACK_OVERFLOW = 2` (`FreeRTOSConfig.h:190`) — kontroluje se vzorek i
  ukazatel, ne jen ukazatel.
- Heap: `configTOTAL_HEAP_SIZE` = 32768 B, `stats` hlásí 11 064 B volných a **min-ever shodné
  s aktuálním** → po rozjezdu se už nealokuje, takže vyčerpání haldy nehrozí.

**B. Dvě jádra**
- **CM4 nenastavuje žádnou NVIC prioritu** — a je to v pořádku: běží bare-metal bez FreeRTOS
  (lwIP se pollingem obsluhuje ve smyčce), takže omezení `configMAX_SYSCALL…` se ho netýká.
- **HSEM 1 serializuje konfiguraci sdílených GPIO** (`freertos.c:391-416`), HSEM 0 je bootovací
  gate — ID se nekříží.

**Kontrola proti `LESSONS.md`:** L-0001 ✓, L-0002 ✓ (netýká se), L-0003 ✓, L-0004 ⚠️ příbuzné
s F-0020 (spin bez ústupu, byť s timeoutem), L-0005 ✓, L-0006 ✓, L-0007 ✓, L-0008 ⚠️ viz F-0019,
L-0009 ✓, L-0010 ✓ (bez změny kódu), L-0011 ⚠️ viz F-0018 (nástroj se tváří, že zaznamenává).

## Nezkontrolováno / omezení tohoto běhu

- **Bez HW.** F-0020 je označený `HYPOTÉZA` v části reprodukce — výpočet doby skenu je
  jednoznačný, ale skutečná doba při nereagující sběrnici se musí změřit.
- **`__DSB()` před návratem z obsluhy** (checklist D, past s bufferováním zápisů na M7):
  v žádné obsluze není. **Nevedu to jako nález** — příznaky se čistí přes `__HAL_*_CLEAR_*`
  a HAL po nich registry ještě čte, takže případné opožděné zapsání by způsobilo nanejvýš
  jeden planý vstup do obsluhy, která by nenašla žádný příznak a nic neudělala. Dopad je
  zanedbatelný, ale **staticky to vyloučit nejde**.
- **Obsahy jednotlivých úloh nebyly auditovány** (`freertos_task_uart.c` má 2281 řádků) —
  procházel jsem je jen z hlediska přerušení, priorit a blokování. Vlastní logika příkazů
  patří k auditu příslušných modulů.
- **`configUSE_TICKLESS_IDLE` a chování FPU kontextu v ISR** neposuzováno do hloubky:
  port ARM_CM4F ukládá FPU kontext líně a žádná obsluha s plovoucí čárkou nepracuje
  (ověřeno čtením obsluh), takže se to nemá o co opřít.
