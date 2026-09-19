# Ověření oprav z auditu na HW — jeden průchod (2026-09-19)

> **Účel:** odbavit ověřovací dluh auditu. Modulů 1–21 se dotklo **29 `fix:` commitů**,
> které **nikdy neběžely na desce**, a podle `L-0010` žádný z nich není hotový.
> Pořadí je zvolené tak, aby se přístroj **restartoval jen dvakrát** a aby se odečty,
> které platí jen po studeném startu, nepřepsaly něčím, co se spustí později.
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
`ipc_scpi.c` se linkuje do obou obrazů. `IPC_VERSION` se nemění (17), ale nesoulad bank
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

- [ ] `selftest` → **`SELFTEST: 16/16 PASS`** a **deska se neresetuje**
      - ✅ prošlo → **F-0055 jde zavřít** a provozní omezení „`selftest` z konzole
        nespouštět" padá
      - ❌ reset → F-0055 **platí dál**; zapiš `status` → `Reset:` a crash black-box
        (`stack:UartTask`?) a omezení zůstává

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
      ⚠️ **Počítadlo `g_eth_tx_err` z UARTu NEPŘEČTEŠ** — to je otevřený **F-0138**
      (nikdo je nepublikuje). Kritérium je tedy „SPA se načte celá a dashboard kreslí",
      ne číslo.
- [ ] **F-0135:** `GPIO HLIDAC: 0 oprav` ve `status` (jako dřív). Příznak
      `g_hsem_gpio_unlocked` se **záměrně nepublikuje** (viz F-0138), takže z UARTu ho
      nevyčteš; tady jde jen o to, že se zámek chová jako dřív.
- [ ] `SCPI(CM4): selftest PASS`, `HTTP(CM4): selftest PASS`, `CM4: alive … stall x0`
      — nic z modulu 22 se jich netýká, jsou to kontrolní hodnoty proti regresi.

## 4. Modul 19 — `membench` (destruktivní jen pro scratch)

- [ ] `membench` doběhne; řádek **SDRAM**: `OK`, **retence po 1 s: 0 chybnych bitu**,
      `celkem 0 chybnych bitu`
      🔑 **Co ověřujeme na dnešní opravě (F-0115):** do chráněného seznamu přibyl
      `.measlog`. Nové položky smí test nejvýš **PŘESKOČIT** (`msg` = „kolize s 0x…!"),
      **nikdy nesmí hlásit chybu**. Přeskočení by znamenalo skutečný alias a bylo by to
      samostatné zjištění.
- [ ] `sdramlog` **hned po** `membench` → `count`/`total` **bez díry** proti fázi 1b
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

## Co z UART ověřit NELZE (ať se to neprohlásí za ověřené)

| nález | proč ne |
|---|---|
| **F-0114** (printf z UiTasku při selhání jasu) | vyžaduje **selhávající** I2C4; vyvolat se dá jen haltem sondy, což zabije I2C4 do power-cyklu |
| **F-0038** (dvojí čtení RTC) | okno je mikrosekundové, pozorovat se nedá |
| **F-0049** (`case 49/50/13`) | projeví se až při úklidu banneru po mrtvé I2C4 |
| **F-0026** (opt-in `s_busy` u třetího zapisovatele) | chce vytažení karty **uprostřed** `screenshot sd` |
| **F-0059** (SSE timeout 120 s) | chce odpojit klienta od sítě a čekat |
| **F-0070** v plném rozsahu | **SURVEY ≥ 1 h** — rozptyl má klesnout pod dřívější mez ~0,4 m |
| **F-0116, F-0118, F-0130** | **otevřené** (skupina B / S4), neopravené — není co ověřovat |

---

## Po průchodu

1. Do `docs/AUDIT_STATUS.md` u každé ověřené opravy nahradit ⬜ za ✅ **s důkazem
   z desky** (konkrétní řádek výpisu, ne „funguje").
2. Co selže → **nový nález**, ne oprava naslepo. A nejdřív zjistit, jestli je vada
   **nová**: `STATUS.md` + `git log` na tentýž symptom, pak `git diff` na **skutečné
   zápisy** do dotčené periferie (§F5.4 bod 4).
3. Když deska po flashi nenaběhne: **bisect je PRVNÍ krok** (SKILL §6g), ne poslední —
   a známý dobrý bod je `b483158` (poslední HW-ověřený stav, moduly 12+13).
