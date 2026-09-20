# Ověření oprav z auditu na HW — jeden průchod (2026-09-19, doplněno 2026-09-20)

> **Účel:** odbavit ověřovací dluh auditu. Nasčítalo se **přes 40 `fix:` commitů**,
> které **nikdy neběžely na desce**, a podle `L-0010` žádný z nich není hotový.
> Pořadí je zvolené tak, aby se přístroj **restartoval co nejméně** a aby se odečty,
> které platí jen po studeném startu, nepřepsaly něčím, co se spustí později.
>
> ⚠️ **Dokument vznikl 2026-09-19 a pak se k němu přidaly další tři dávky oprav.**
> Fáze **3e** (skupina B a A+A′) a **9** (dnešní UI/diagnostika) jsou doplněné
> 2026-09-20 — bez nich by se průchod musel dělat třikrát.
>
> 🔴🔴 **`IPC_VERSION` se ZMĚNILA 17 → 18** (F-0138). Dokud nejsou naflashované **obě
> banky**, CM4 snapshot ignoruje a **polovinu tohohle checklistu nejde vyhodnotit** —
> a zrádné je, že to navenek vypadá dobře: header dál svítí `4:xx%`. Jediný příznak
> je řádek `⚠ IPC NESOULAD` ve `status`.
>
> Legenda: ✅ funguje · ⚠️ funguje s výhradou · ❌ nefunguje · ⬜ nezkoušeno
>
> 🔴🔴 **CELÝ PRŮCHOD BEZ AKTIVNÍ LADICÍ SONDY.** Po flashi *Terminate* debug session,
> *Run → Remove All Breakpoints*, pak **úplný power-cycle**. Sonda rozbíjí boot handshake
> CM7↔CM4 (CM4 hlásí „off") a **jeden halt stačí na mrtvou I2C4** až do power-cyklu
> (změřeno kontrolovaným pokusem 2026-09-07). Diagnostika jde celá přes UART.

---

## 0. Flash — než cokoli odečteš

🔴 **Flashni OBĚ banky.** Mezi neověřenými jsou opravy CM4 (`2f2aa53`, `b6cc88e`) a dnešní
`ipc_scpi.c` se linkuje do obou obrazů.
🔴 **`IPC_VERSION` se ZMĚNILA 17 → 18** (F-0138 přidal TX počítadla do bloku `cm4`),
takže flash obou bank **není volitelný**. Nesoulad bank
je **neviditelný** — header dál svítí `4:xx%`.

🔴🔴 **PAST: „Set Active → Release" NESTAČÍ.** Mění jen tlačítko **Build**. Tlačítko
**Run/Debug** flashuje podle **vlastní** Run konfigurace, která si pamatuje konkrétní
cestu k `.elf` (typicky `Debug\…`). Zkontroluj *Run → Run Configurations… → Main →
C/C++ Application* = `Release\H757_LED_CM7.elf`.

- [ ] Bank1 = `CM7/Release/H757_LED_CM7.elf`, bank2 = `CM4/Release/H757_LED_CM4.elf`
- [ ] **POWER-CYCLE** (odpojit napájení), ne NRST — studený start je jiný stav
- [ ] **Kontrola, že jde opravdu o Release:** okno **PAMĚŤ** (`s_view=5`) → FLASH (CM7)
      ≈ **589 KB**. Když ukáže ~792 KB, naflashoval se Debug a **celý průchod je neplatný**.

---

## 1. Odečty, které platí JEN hned po bootu

⚠️ Tohle udělej **první** a nic mezitím nespouštěj — `selftest`, `membench` i restart
z fáze 6 tyhle hodnoty přepíšou nebo vynulují.

### 1a. `status` — jeden příkaz, který pokrývá osm modulů

- [ ] `Reset: power-on` ← **bez tohohle neplatí `L-0010`** a celý průchod se musí opakovat
- [ ] crash black-box **prázdný** (žádné `stall:` / `stack:` / `HF@`)
- [ ] **`WATCHDOG: PR=4 RLR=2000 -> 4000 ms | pipak ok`** (F-0104 + F-0111)
      🔴 Cokoli jiného u `PR`/`RLR`, zvlášť značka `<== NESEDI`, **je nález sám o sobě** —
      znamenalo by to watchdog ~0,5 s místo 4 s. `pipak NENABEHL` = přístroj je trvale němý
      včetně alarmu na ztrátu reference.
- [ ] **`KONZOLE: zahozeno TX … / RX …`** — 🔑 **nový řádek (F-0128).** Po bootu by měl
      **chybět** (obě nuly = netiskne se). Když se objeví hned, konzole nestíhá odtékat.
- [ ] `DISPLEJ: bring-up OK`
- [ ] `I2C4: SCL=1 SDA=1 idle | TMP117 0x48 err 0 (v rade 0)`
- [ ] `GPIO HLIDAC: 0 oprav`
- [ ] `LTDC: podteceni FIFO 0/…` a `DMA2D: chyb 0, timeout 0` (moduly 8, 2)
- [ ] `FONTY: preskocenych glyfu 0`
- [ ] `CM4: alive (IPC heartbeat)`, **`SCPI(CM4): selftest PASS`**, **`HTTP(CM4): selftest PASS`**
      — a **žádné `⚠ IPC NESOULAD`** (to by znamenalo, že se naflashovala jen jedna banka)
- [ ] 🔴 **`NET: UP …, IP …`** (modul 12) — a je to zároveň **nejdůležitější regresní
      kontrola modulu 22**: `F-0131` nově vyhodnocuje návrat `HAL_ETH_Start`, takže kdyby
      se v té podmínce spletl, linka **nenaskočí vůbec**. `NET: DOWN` po povedeném bootu
      s připojeným kabelem = regrese, ne nález. Viz fáze 3d.
- [ ] `ADEV rekonstrukce: …` — dojede do „hotova" v jednotkách minut, ne hodin (F-0039)
- [ ] `STATISTIKA: sigma_y@1s` — nenulová a v řádu signálu (F-0037)
- [ ] `DATALOG …` — zapiš si stav, budeš ho potřebovat ve fázi 6
- [ ] `REFERENCE:` — sticky bity Si5356 (po bootu je armování 5 s, takže čisté)
- [ ] 🔑 **`FORMAT: omezenych desetin 0`** — *nový řádek (F-0053)*. Nenulové znamená, že
      se někde na displeji ukazuje **zaokrouhlená** hodnota místo požadované; dřív se to
      dělo tiše (σ hlásila „0 Hz", STATUS #132). Hlídá i druhou mez — přetečení `int32`
      podle **hodnoty**, kterou grep nad zdrojem najít neumí.
- [ ] 🔑 **`ULOZISTE: syscfg OK | calib OK | sestavy OK | flightrec OK | errlog OK`** —
      *nový řádek (F-0098)*. Všech pět musí být `OK`. Cokoli s `--` znamená, že se ta část
      W25Q nepřipravila a **do konce běhu nepřipraví** (nastavení se neuloží, kalibrace
      zůstane na datasheetových výchozích). Pod ním se při nenulových pokusech objeví
      `pokusy o zachranu: syscfg N, errlog M (strop 5)` — nenulové = jednorázová smůla
      při bootu, kterou retry řešil.
- [ ] `Reset:` **nesmí** hlásit `hal_err@HSE` ani `hal_err@LSE` (F-0007 + F-0108).
      Kdyby ano, nenaběhl oscilátor — ale to bys nejspíš nečetl, protože přístroj by
      v tom případě vůbec nenastartoval. **Nový je vzor blikání:** ≥ 15 bliknutí LED_1
      + pípnutí = hodiny. Do 2026-09-19 byla ta smrt **úplně tichá a temná**.

### 1b. `sdramlog` — musí naběhnout (modul 19, F-0117)

- [ ] `sdramlog` → **`ready`**
      🔑 **Tohle je vlastně test dnešní opravy:** kontrola aliasu v `sdram_log_init`
      dostala dvě nové položky (`canvas`, `.sdram`). Kdyby některá **falešně** ohlásila
      alias, log by se **nezapnul** a `fail[]` by řekl, která. Zapiš si `count`/`total`.

### 1c. `si5356` (modul 18, F-0113)

- [ ] `si5356` → `zapisy=OK` a stav bez `LOS_CLKIN!` / `PLL_LOL!`
      (`LOS_XTAL(bez krystalu, ok)` je normální stav téhle desky)

---

## 2. F-0055 [S1] — jediný test, který může resetovat desku

🔑 **Proč zrovna teď:** `selftest` z konzole desku dosud **deterministicky resetoval**
(zásobník UartTasku). Blokátor padl — commit `4b935c9` zvedl v `.ioc` UartTask
**1024 → 2048 slov**. Premisa nálezu se tím změnila a rozhodne se **měřením**, ne debatou.
Dělej to **po fázi 1** (aby se neztratily odečty) a **před** vším ostatním.

🔴 **POZOR PŘI VYHODNOCENÍ: jsou v obrazu DVĚ konkurenční opravy téhož, ne jedna.**
Kromě zvětšení zásobníku přibyl 2026-09-20 i přesun velkých lokálů do `.bss`
(`resp[128]` u `scpi`, `buf[512]` u `qspispeed` — F-0074 + F-0077a). TODO #243 zadávalo
dělat je **odděleně**, aby se poznalo, která pomohla; uživatel se rozhodl udělat obě.
**Když to teď projde, znamená to „už to nepadá", NE „víme čím."** Zapiš tedy i `stats`
→ volný stack UartTasku, ať je aspoň číslo, ze kterého se to dá příště dopočítat.

- [ ] `selftest` → **`SELFTEST: 16/16 PASS`** a **deska se neresetuje**
      - ✅ prošlo → **F-0055 jde zavřít** a provozní omezení „`selftest` z konzole
        nespouštět" padá
      - ❌ reset → F-0055 **platí dál**; zapiš `status` → `Reset:` a crash black-box
        (`stack:UartTask`?) a omezení zůstává

---

## 2b. F-0018 [S1] — `stacktest yes` (⚠️ ZÁMĚRNĚ SHODÍ DESKU)

🔑 **Co se ověřuje:** letový zapisovač volaný z hooku přetečení zásobníku
**deterministicky nezapsal nic** — hook běží v PendSV, kde `osMutexAcquire` vždy vrátí
`osErrorISR`. Od 2026-09-20 se dump nejdřív složí do `.sdram` a do flash ho vylije až
`flightrec_init()` **po restartu**. Tohle je jediný způsob, jak to ověřit.

⚠️ **Dělej to až po fázích 1 a 2** — příkaz přetečení vyvolá schválně, takže následuje
IWDG reset a všechny odečty „jen po bootu" jsou pryč. Po restartu bude crash black-box
hlásit `stack:UartTask`, což je **očekávané**, ne nález.

- [ ] `stacktest yes` → deska se restartuje (to je záměr)
- [ ] po restartu `status`:
      - [ ] `Reset:` hlásí `WATCHDOG` + crash black-box **`stack:UartTask`**
      - [ ] 🔑 **`FLIGHTREC: N dumpu zachranenych po restartu (SDRAM staging)`** —
            *nový řádek*. **Tohle je ten důkaz.** Když chybí, dvoufázový zápis nefunguje
            a F-0018 platí dál.
      - [ ] `FLIGHTREC: … zahozeno` **nesmí** přibýt (to je jiná cesta — nedostaný mutex
            mimo kontext výjimky)
- [ ] `flightrec` → vypíše **60 s historie před pádem** (CPU, heap, nejmenší stack,
      teploty, I2C). Prázdný výpis při nenulovém „zachráněno" = vylilo se, ale obsah
      je vadný → samostatný nález.
- [ ] `errlog dump` → nejnovější řádek **`CRASH  stack UartTa`** (F-0092)
      🔑 Do 2026-09-20 tam stálo jen `CRASH  CFSR=0x00000000 BFAR=0x00000000`, tedy
      *že* se pád stal, ale ne *co* spadlo. Jméno tasku je useknuté na 6 znaků
      (`UartTa`) — to je správně, tag má pevnou délku.

---

## 3. Dnešní opravy — modul 21 (`0924328` a dřív)

### 3a. F-0127 + F-0128 — kruhový buffer konzole

🔑 **Rozlišovací test:** přepni na **STOP** a nech obrazovku **statickou** (bez animací).
Tím se vyloučí tearing, takže jakékoli poškození snímku je ta vada v ringu.

- [ ] `screenshot` (přes USB, bez `sd`) → BMP se otevře a **není poškozený**
- [ ] hned poté `status` → `KONZOLE: zahozeno TX …`
      - **0 / řádek chybí** → ✅ ring drží
      - **nenulové TX** → BMP je poškozený a víme proč (host neodebíral); není to regrese
        opravy, ale její **měřidlo**
- [ ] `screenshot sd` → `SHOTnnn.BMP` na kartě, taky nepoškozený (jiná cesta, sdílí `s_row`)

### 3b. F-0014 — `ipccmd` odmítne běh, když může zapisovat i CM4

- [ ] vzdálené ovládání **POVOLENÉ** (okno SÍŤ) + CM4 živá → `ipccmd run 1` musí **odmítnout**:
      `ipccmd: CM4 zije a vzdalene ovladani je POVOLENE — push z CM7 by kolidoval`
- [ ] `ipccmd force run 1` → **projde** (`OK (uplatni UiTask do ~0,2 s)`) a RUN/STOP se
      na displeji přepne
- [ ] vzdálené ovládání **VYPNUTÉ** → `ipccmd run 0` projde **bez** `force`
      🔑 Tím je ověřené, že predikát je úzký (bez `web_ctrl_en` guard nezasahuje) —
      tedy že příkaz dál plní kritérium W1 „bez sítě a bez webu".

### 3c. F-0129 — `scpi ipc`

- [ ] `scpi ipc SENS:FREQ:GATE?` → `=> SHODA` (čtení funguje dál)
- [ ] `scpi SENS:FREQ:GATE?` si zapiš, pak `scpi ipc SENS:FREQ:GATE 10`, pak znovu
      `scpi SENS:FREQ:GATE?` → **hodnota se NESMÍ změnit**
      🔑 To je ta záměrná změna chování: `scpi ipc <SET>` už do ringu nesahá, takže
      přístroj nepřestaví. ⚠️ Zároveň nesmí hlásit `ROZDIL` (aplikuje se lokální zrcadlo).

---

### 3d. Modul 22 — CM4: ETH/lwIP (`356fe02`)

⚠️ **Doplněno po prvním sepsání checklistu** — modul 22 vznikl až po něm.
🔑 Tyhle opravy jsou celé v **CM4**, takže je uvidíš jen s naflashovanou **bankou 2**.

- [ ] **F-0131 (nejdůležitější):** `status` → `NET: UP <rychlost> <duplex>, IP <adresa>`
      s připojeným kabelem. Nově se vyhodnocuje návrat `HAL_ETH_GetMACConfig` /
      `SetMACConfig` / `Start`, takže **chyba v té podmínce by linku zablokovala úplně**.
      `NET: DOWN` při zapojeném kabelu = **regrese této dávky**.
- [ ] vytáhnout a zapojit kabel → linka musí spadnout na DOWN a **znovu naskočit**
      (ověří, že se při chybě nezacyklí a že se `linkchanged` pokaždé odvodí znovu)
- [ ] **F-0133:** `status` → `ETH(CM4): init OK` a `NET:` s IP. Kdyby nová runtime
      kontrola `heth.Init.RxBuffLen != ETH_RX_BUFFER_SIZE` falešně zahlásila rozpor,
      rozhraní by se **vůbec nezapnulo** (`netif` down) — takže funkční síť tu kontrolu
      zároveň ověřuje. Dnes je 1536 = 1536.
- [ ] **F-0132 (TX cesta):** z prohlížeče stáhnout **celou SPA** (~139 kB) a projít
      dashboard; pak `/api/state` a `POST /api/scpi`. Ověří dlouhé TX přenosy, tedy právě
      cestu, kde se skládají zřetězené pbufy.
- [ ] **F-0138 (nově měřitelné):** `status` → řádek **`TX(CM4): odeslano N, zahozeno M`**.
      Dokud byl F-0138 otevřený, tohle číslo z UARTu přečíst nešlo a kritériem bylo jen
      „SPA se načte celá". Teď platí obojí:
      - `odeslano` musí **růst** při načítání SPA (saturuje na 65535, pak se tiskne `65535+`),
      - `zahozeno` má být **0** — nenulové znamená zřetězený pbuf delší než `ETH_TX_DESC_CNT`,
      - 🔴 **`odeslano 0` s `NET: UP` = přesně ta vada z 2026-09-08** (linka hlásí
        100 Mbit full, na drát nejde nic). Výpis to sám označí `<== NEODESLALA ANI JEDEN PAKET`.
      ⚠️ Vyžaduje **obě banky s `IPC_VERSION` 18** — při nesouladu `status` napřed vypíše
      `⚠ IPC NESOULAD`, a ten řádek má přednost před jakoukoli interpretací TX počítadel.
- [ ] **F-0135:** `GPIO HLIDAC: 0 oprav` ve `status` (jako dřív). Příznak
      `g_hsem_gpio_unlocked` se **záměrně nepublikuje** — je to stav jednoho okamžiku
      při bootu, ne počítadlo, takže z UARTu ho nevyčteš; tady jde jen o to, že se
      zámek chová jako dřív.
      ⚠️ Tahle položka **nemá** společnou příčinu s F-0138, i když to dřív v checklistu
      takhle stálo: F-0138 byl chybějící *čtenář* existujících počítadel (opraveno),
      tady chybí *publikace* jednorázového příznaku — což je vědomé rozhodnutí.
- [ ] `SCPI(CM4): selftest PASS`, `HTTP(CM4): selftest PASS`, `CM4: alive … stall x0`
      — nic z modulu 22 se jich netýká, jsou to kontrolní hodnoty proti regresi.

## 3e. Dávky z 2026-09-19 (skupina B) a 2026-09-20 (skupina A + A′)

### Váže se na studený start z fáze 1

- [ ] **F-0017** — ✅ **už se ověřilo ve fázi 1a**, nedělej kvůli tomu další restart:
      řádky `SCPI(CM4): selftest PASS` a `HTTP(CM4): selftest PASS` odečtené hned po
      power-cyklu z fáze 0 JSOU ten důkaz. Sem to patří jen proto, aby se vědělo,
      že tahle položka není „nezkontrolovaná".
      🔑 **Proč zrovna studený start:** `ipc_init()` na CM7 dělal `memset` přes celou
      sdílenou strukturu včetně bloku `cm4` a běží až ze `StartDefaultTask`, tedy
      sekundy po bootu — zatímco CM4 publikuje už ~1,3 s po bootu. Při HW průchodu
      2026-08-30 memset dopadl **mezi** publikaci httpd a eth a `status` hlásil
      „jeste nedobehl", přestože selftest prošel. Nově blok `cm4` nuluje CM4 sama.
      **„Jeste nedobehl" po studeném startu = oprava nefunguje.**
- [ ] **F-0016** — jen regresně: IPC funguje jako dřív (`CM4: alive`, snapshot se čte).
      Oprava je čistě linkerová rezervace 64 kB v RAM_D3 na obou jádrech; kdyby byla
      špatně, **neslinkovalo by se to** (ověřeno negativním testem při opravě).

### Encoder (F-0122, F-0125) — 🔴 tady je jediná skutečná změna chování

- [ ] **`enc`** → otočit o **jednu západku** → musí vypsat **`kroku=1`**
      🔴 **Tohle je nejdůležitější položka celé dávky.** TIM1 se nově konfiguruje
      z `.ioc` přes `MX_TIM1_Init()` a `encoder.c` ho jen startuje
      (`HAL_TIM_Encoder_Start`). Registry vycházejí bit za bitem stejně **až na
      `CC1E/CC2E`**, které byly dřív nulové — počítání to měnit nemá (slave-mode
      controller bere `TI1FP1/TI2FP2`), ale je to **jediný neověřený rozdíl**.
- [ ] **`enc div 2`** → `enc` → jedna západka dá `kroku=1` při poloviční citlivosti,
      a po `enc div 4` se chování vrátí (F-0125: změnu dělá až vlastník stavu v UiTasku)
- [ ] stisk → `short_press`, držení 1 s → `long_press`

### Trvalá historie a letový zapisovač (F-0091, F-0095, F-0100, F-0101)

- [ ] `errlog dump 20` → řádky mají **čitelný DETAIL**, ne holá čísla `a=…  b=…`
      (F-0100: třetí konzument teď používá tentýž `errlog_fmt_detail` jako displej a web)
- [ ] `errlog dump 200` u naplněného logu → **nekončí dřív, než slíbil** (F-0101)
- [ ] `flightrec test` **2×** s restartem mezi tím → druhý dump **nepřepíše** ten první
      (F-0091: maže se sektor **za nejnovějším**, ne natvrdo sektor 0)
      ⚠️ Plný projev původní vady nastával až po ~64 dumpech; tenhle test ověří aspoň
      to, že se cíl posouvá.

### Datalog (F-0102)

- [ ] `datalog` → řádek obsahuje **`skip:0`** za normálního provozu
- [ ] po `membench` (blokuje sekundy) smí `skip` narůst o 1 — ale `seq` **nesmí**
      poskočit o víc než o jedna a v logu nesmí být dva záznamy se stejným `t_unix`
      (F-0102: zmeškané vzorky se **nedohánějí**, díra je správná odpověď)

### CM4 / mDNS (F-0061)

- [ ] `gpsdo.local` se z prohlížeče pořád resolvuje (přidané čtení QCLASS a omezení
      tempa na 1 odpověď / 250 ms nesmí resolvování rozbít)

---

## 4. Modul 19 — `membench` (destruktivní jen pro scratch)

- [ ] `membench` doběhne; řádek **SDRAM**: `OK`, **retence po 1 s: 0 chybnych bitu**,
      `celkem 0 chybnych bitu`
      🔑 **Co ověřujeme na dnešní opravě (F-0115):** do chráněného seznamu přibyl
      `.measlog`. Nové položky smí test nejvýš **PŘESKOČIT** (`msg` = „kolize s 0x…!"),
      **nikdy nesmí hlásit chybu**. Přeskočení by znamenalo skutečný alias a bylo by to
      samostatné zjištění.
- [ ] `sdramlog` **hned po** `membench` → `count`/`total` **bez díry** proti fázi 1b
- [ ] 🔑 **F-0116** — ve výpisu se **NESMÍ objevit ANI JEDNA** z těchto dvou vět:
      `(framebuffery se ale navzajem NEprekryvaji …)` ani
      `(prekryv MEZI framebuffery se NEMERIL …)`.
      Obě se tisknou jen při nenulovém `alias_off`, takže na zdravé desce nemá být ani
      jedna. **Kdyby se objevila ta druhá**, znamená to, že se `fb_alias` neměřil —
      a to je po opravě chyba sama o sobě (nově se měří bezpodmínečně).
- [ ] 🔑 **F-0120** — řádek **interní FLASH** hlásí `cteni stabilni` a souhrn
      `MEMBENCH: OK (celkem 0 chybnych bitu)` **bez přípony**.
      Kdyby se objevilo `+ NESTABILNI CTENI FLASH`, je to skutečný nález (dvě po sobě
      jdoucí čtení téhož bloku se lišila) — a nově se to **nepřičítá k počtu bitů**,
      protože u paměti jen pro čtení „chybný bit" nedává smysl.
      🔑 Tohle je vlastní pointa F-0115: kdyby benchmark do měřicího logu sáhl, projeví
      se to tady.
- [ ] `sd diag` → řádek `sbernice: 4-bit, SDMMC_CK 32.000 MHz, Default Speed (limit 25 MHz)
      <-- NAD LIMITEM (vedome, viz SD_CLKDIV)` (F-0028 — vědomý stav, ne nález)

---

## 5. Modul 14 — konzole, meze vstupu

- [ ] příkaz **delší než 95 znaků** → `ERR prikaz delsi nez 95 znaku - NEPROVEDEN`
      a `status` → `KONZOLE: N prikazu odmitnuto`
- [ ] `scpi SENSe:FREQuency:APERture 10` → hradlo **10 s** (ne 1 s) — alias `GATE`
- [ ] `fpgasim on 99999999999999999999` → **nesmí** dát nesmyslný kmitočet (strop 4 GHz)
- [ ] `fpgaraw` → vypíše **64 bajtů** beze změny
- [ ] `fpgasim off` (uklidit po sobě)

---

## 6. Perzistence a čas — TEPRVE TEĎ (resetuje uptime)

### 6a. F-0089 — nastavení datalogu přežije TEPLÝ reset

- [ ] `datalog off` → **počkat > 2 s** (debounce flash zápisu je ~1,5 s)
- [ ] Menu → **Restart**
- [ ] `status` → musí pořád hlásit **`DATALOG … OFF`**
      🔴 Před opravou se vrátilo na `ON` — to byl ten S1 nález.
- [ ] `datalog on` (vrátit do provozního stavu)

### 6b. F-0103 — stav pípáku má jednoho vlastníka

- [ ] vyvolat alarm (např. `fpgasim fault lost` → ztráta signálu = 3 pípnutí),
      **během** pípání poslat `beep test`
- [ ] pípák **nesmí zůstat troubit** souvisle
- [ ] `fpgasim off`, `status` → `g_alarm_*` počítadla dávají smysl

---

## 7. F-0052 + F-0096 — atomický zápis `g_meas_cfg`

- [ ] okno **MATH/LIMITY**, zapnout LIMITY
- [ ] **držet prst** na tlačítku pásma (auto-repeat) a současně z konzole opakovaně
      `scpi CALC:LIM:LOW?` a `scpi CALC:LIM:UPP?`
- [ ] **nikdy nesmí vyjít `lo > hi`** a displej se nesmí rozejít s tím, co hlásí SCPI
      🔴 Před opravou mohla preempce mezi dvěma `vstr` vydat `(nové lo, staré hi)`;
      nejmenší pásmo je 0,001 Hz, takže k inverzi stačilo 0,002 Hz pohybu.
- [ ] okno **SESTAVY** → uložit profil s limity → **NAČÍST** → nesmí spustit falešný
      alarm (F-0096: `slot_sanitize` teď `lo > hi` prohodí)

---

## 8. Regrese UI (moduly 8–11) — rychlý průlet

- [ ] Diagnostika → `NASTROJE >` → dlaždice **Chyby (log)** → okno se **zobrazí**
      (F-0063 — dřív jen cvaklo)
- [ ] `status` → `UI: navigace (ZPET) max n/6` **bez** značky přetečení (F-0048)
- [ ] pár tapů v MENU/MĚŘENÍ a `status` → `s_view` odpovídá tomu, co je na displeji (F-0047)
- [ ] fokus encoderem v okně se seznamem sedí na stisknutém prvku (F-0046/F-0051)
- [ ] okno **PAMĚŤ** → řádky CM7 **i CM4** (ne `--`) — IPC v16

---

## 9. Dnešní UI a diagnostika (2026-09-20) — přívětivost

⚠️ Tohle nejsou opravy nálezů, ale změny chování, které se taky nedají ověřit jinak
než na desce.

- [ ] **`stats`** → na konci blok **`--- UiTask po fazich (posledni 1s okno, N pruchodu)`**
      🔑 **Kvůli tomuhle to celé vzniklo:** doteď šlo změřit, že UiTask bere ~58 % CPU,
      ale ne **čím**. Zapiš si ta čísla — je to vstup pro rozhodnutí, jestli rozšiřovat
      DMA2D glyph accel, a bez nich by se to dělalo naslepo.
      - [ ] `N pruchodu` má být ~100 (smyčka je 100 Hz) — výrazně méně znamená, že
            něco ve smyčce blokuje
      - [ ] `CELKEM` musí být **menší** než 1 000 000 µs; rozdíl proti součtu fází je
            `osDelay` + režie smyčky, ne chyba měření
- [ ] **`status`** → kratší než dřív; **`status full`** → vypíše i řádky `UI kresleni:`,
      `UI: okno`, `GLOW:`, `SDRAM refresh:`, `SDRAM cteni:`, `STATISTIKA:`,
      `ATTINY zapisu jasu:`
      ⚠️ Krátká verze **musí** pořád ukázat cokoli, co není v pořádku — když se něco
      pokazí a `status` o tom mlčí, zatímco `status full` to hlásí, je to **nález**.
- [ ] **Encoder na hlavní obrazovce** (bez dotyku):
      - [ ] otáčením zaměř **GATE** → další otočení **přepíná preset hradla**
            (ne přejíždí fokus dál)
      - [ ] totéž pro **CHAN**
      - [ ] u **RUN/STOP**, **PERIOD/FREQ** a **MENU** otáčení dál **přejíždí fokus**
            (záměrně — RUN je destruktivní, MENU naviguje)
      - [ ] tytéž změny jdou pořád i **dotykem** (obě cesty musí zůstat úplné)
- [ ] **Dlouhý stisk encoderu na hlavní obrazovce** → amber pruh přes patku
      „AUTO-TRIGGER zatim nejde / prah a hystereze vyzaduji vstupni modul", který
      **sám zmizí do ~2,5 s** a patka se vrátí.
      ⚠️ Když nezmizí, úklid v `app_gpsdo_tick_clock` nefunguje a patka zůstane
      překrytá — to je nález.

---

## Co z UART ověřit NELZE (ať se to neprohlásí za ověřené)

| nález | proč ne |
|---|---|
| **F-0114** (printf z UiTasku při selhání jasu) | vyžaduje **selhávající** I2C4; vyvolat se dá jen haltem sondy, což zabije I2C4 do power-cyklu |
| **F-0038** (dvojí čtení RTC) | okno je mikrosekundové, pozorovat se nedá |
| **F-0049** (`case 49/50/13`) | projeví se až při úklidu banneru po mrtvé I2C4 |
| **F-0026** (opt-in `s_busy` u třetího zapisovatele) | chce vytažení karty **uprostřed** `screenshot sd` |
| **F-0059** (SSE timeout 120 s) | chce odpojit klienta od sítě a čekat |
| **F-0070** v plném rozsahu | **SURVEY ≥ 1 h** — rozptyl má klesnout pod dřívější mez ~0,4 m |
| **F-0003** | **neopravený** — čekání na `VOSRDY` leží v generovaném kódu bez `USER CODE`; oprava je připravená v `CUBEMX_CHECKLIST.md` na příští regeneraci |
| **F-0137** (`iwdg2_config_ok`) | IWDG2 je **záměrně vypnutý**, takže `iwdg2_init()` se nevolá a `--gc-sections` ji z obrazu zahodí — ověřit to nejde, dokud se IWDG2 nezapne |
| **F-0007 + F-0108** v plném rozsahu | chtělo by to **odpojit HSE, resp. LSE krystal**. Ověřitelné je jen to, že normální boot funguje (fáze 1a) |
| **F-0061** v plném rozsahu | konflikt jména a probing dle RFC 6762 se **záměrně nedělá**; ověřuje se jen to, že `gpsdo.local` pořád funguje |
| **F-0091** v plném rozsahu | plný projev nastával až po **~64 dumpech**; fáze 3e ověří jen posun cíle |

---

## Po průchodu

1. Do `docs/AUDIT_STATUS.md` u každé ověřené opravy nahradit ⬜ za ✅ **s důkazem
   z desky** (konkrétní řádek výpisu, ne „funguje").
2. Co selže → **nový nález**, ne oprava naslepo. A nejdřív zjistit, jestli je vada
   **nová**: `STATUS.md` + `git log` na tentýž symptom, pak `git diff` na **skutečné
   zápisy** do dotčené periferie (§F5.4 bod 4).
3. Když deska po flashi nenaběhne: **bisect je PRVNÍ krok** (SKILL §6g), ne poslední —
   a známý dobrý bod je `b483158` (poslední HW-ověřený stav, moduly 12+13).
