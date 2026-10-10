# Audit: DSI bridge, sdílený SCPI backend, USB CDC konzole (modul 21)  (2026-09-19)

- **Commit:** `597c69c` (větev `audit/2026-09-09-hodiny-pwr`), pracovní strom bez změn v kódu
- **Jádro / doména:** CM7. `ipc_scpi.c` se linkuje **i do obrazu CM4** (běží tam pod TCP/HTTP SCPI).
- **Soubory (426 ř. vč. hlaviček):** `tc358762.c` (137) + `.h` (28),
  `ipc_scpi.c` (147, hlavičku **nemá** — deklarace jsou v `ipc_shared.h:588` a `scpi.h:213-214`),
  `usb_console.c` (82) + `.h` (32).
- **Projité sekce checklistu:** C (umístění bufferů, DMA vs. cache), D (souběh, ISR,
  PRIMASK vs. RTOS), E (návratové hodnoty, tiché zahození), G (DSI, USB).
- **Neprojité (a proč):** A (modul hodiny nekonfiguruje), F (nesahá na Flash),
  H (errata — bez revize silikonu).

## Proč tenhle modul vůbec existuje

Není to plánovaný modul — je to **mezera, kterou našla křížová kontrola** při uzavírání
modulu 20 (2026-09-18). Seznam „co zbývá auditovat“ byl neúplný: porovnání **všech**
`.c` v `CM7/Core/Src` + `CM7/app` proti souborovým seznamům 20 modulů ukázalo, že
`tc358762.c` nebyl nikdy ani zmíněn a `ipc_scpi.c` / `usb_console.c` byly jen citované
v dokumentech jiných modulů. To je druhý výskyt **L-0014** (ruční souhrn se rozešel se
zdrojem pravdy) — a stálo to za to: leží tu **jediný S2 nález za poslední tři moduly**.

## Souhrn

`tc358762.c` je pečlivý port referenčního Linux driveru s doloženou aritmetikou
modeline a s dokumentovaným workaroundem na over-read v HAL. `ipc_scpi.c` je čistý
převod snapshotu na `scpi_src_t` a správně nechává nepodporované operace `NULL`,
aby je parser odmítl místo vymýšlení hodnot.

**Verdikt: podmíněně funkční.** `usb_console.c` uvolňuje slot v kruhovém bufferu ve
chvíli, kdy USB stack přenos **přijal**, ne kdy ho **dokončil** — a protože je CDC
cesta prokazatelně **zero-copy** a FIFO plní až USB ISR, producent může přepsat data,
která se právě vysílají (**F-0127, S2**). Ke tomu tichá ztráta v obou směrech konzole
bez jediného počítadla (**F-0128**) a druhá instance otevřeného F-0014 (**F-0129**).

---

### F-0127 [S2] `usb_console_tx_pump()` uvolní slot ringu při „přijato k odeslání“, ne při „odesláno“ — CDC je zero-copy, takže producent přepisuje vysílaná data

- **Místo:** `CM7/Core/Src/usb_console.c:41-42` (`pump`), `:48-66` (`usb_console_tx`),
  buffer `:21` (`s_tx[1024]`)
- **Popis:** `pump()` posune `s_tail` (= „tyhle bajty jsou volné“) hned jak
  `CDC_Transmit_FS` vrátí `USBD_OK`. Jenže `USBD_OK` znamená jen *„endpoint je
  naprogramovaný“* — data se z `s_tx` kopírují do USB FIFO **později, z USB ISR**.
  Producent (`printf`, `screenshot`) tak smí do těch slotů psát, dokud se ještě vysílají.
- **Důkaz** (celá cesta dohledaná ve vendorovaném middleware, ne odvozená):
  1. `usb_console.c:41-42`
     ```c
     if (CDC_Transmit_FS(&s_tx[t], chunk) == USBD_OK)
       s_tail = (uint16_t)(s_tail + chunk);
     ```
  2. `Middlewares/.../Class/CDC/Src/usbd_cdc.c:777-778` — `USBD_CDC_SetTxBuffer`:
     `hcdc->TxBuffer = pbuff; hcdc->TxLength = length;` → **ukazatel, žádná kopie**.
  3. `usbd_cdc.c:833-846` — `USBD_CDC_TransmitPacket`: nastaví `TxState = 1`, zavolá
     `USBD_LL_Transmit(..., hcdc->TxBuffer, hcdc->TxLength)` a **vrátí `USBD_OK`**;
     mezi tím se nepřenese ani bajt.
  4. `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1892-1893` —
     `HAL_PCD_EP_Transmit`: `ep->xfer_buff = pBuf; ep->xfer_len = len;` → opět jen ukazatel.
  5. `CM7/USB_DEVICE/Target/usbd_conf.c:349` — `hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;`
     → přenos jde **FIFO cestou**, tedy `PCD_WriteEmptyTxFifo()` **z obsluhy přerušení USB**,
     která čte přímo `s_tx`.
  6. Kruhový buffer má **1024 B** (`:19`) a `usb_console_tx` smí dojet až na
     `tail + 1023` (`:51`), takže po uvolnění slotů může producent **přetočit přes
     právě vysílanou oblast**.
  7. 🔑 Cesta je **živá**, ne mrtvý stub: `usb_console.h:19-20` má
     `#define USE_USB_CDC_CONSOLE 1`.
- **Dopad:** Poškozený výstup konzole a **poškozený BMP** u `screenshot` přes USB —
  nedeterministicky, podle toho, jak dlouho USB FIFO plní ISR a kolik producent mezitím
  napíše. Nejhorší je to tam, kde je producent nejrychlejší: `screenshot_emit_bmp`
  posílá **256 B na blok** ve smyčce bez prodlevy (2 400 B na řádek) a `status`/`stats`
  sypou desítky řádků. Při FS bulk (64 B/paket) trvá 1 kB přenos stovky µs až jednotky ms,
  takže okno je velké.
  ⚠️ **A je pravděpodobné, že to už jednou někoho svedlo:** `CLAUDE.md` popisuje
  USB screenshot jako *„best-effort tok“* s *„pruhy ze dvou framů“* a připisuje to
  tearingu (snímání živého FB). Tearing je reálný, ale **tohle je druhá, nezávislá
  příčina téhož projevu** — a na rozdíl od tearingu nezmizí ani u statické obrazovky.
- **Reprodukce:** `HYPOTÉZA — ověřit:` `screenshot` přes USB na **statické** obrazovce
  (STOP, bez animací) a zkontrolovat BMP: tearing je vyloučený, takže jakékoli poškození
  je tohle. Levnější rozlišovací test: přidat do `pump()` počítadlo případů, kdy
  `s_head` přeteče do rozsahu právě vysílaného bloku — nenulové = potvrzeno bez oka.
  Mechanismus sám je doložený ze zdrojáku middleware výše, bez desky.
- **Návrh opravy** (dvě varianty, obě drží `usb_console.c` samostatný):
  - **(a) minimální, celá uvnitř modulu:** neuvolňovat slot hned, ale **až u příštího
    úspěšného pumpu**. Držet `s_pending` = velikost letícího bloku; když další
    `CDC_Transmit_FS` vrátí `USBD_OK`, znamená to, že `TxState` bylo 0, tedy že
    **předchozí přenos dokončil** → teprve tehdy `s_tail += s_pending` a
    `s_pending = chunk`. Žádný nový callback, žádný zásah do generovaného kódu.
    ⚠️ Cena: poslední blok zůstane „rezervovaný“, dokud nepřijde další zápis do konzole
    (ring se tím nezaplní — jen se o ten blok zmenší volné místo).
  - **(b) správná:** uvolňovat v **TxComplete** callbacku (`CDC_TransmitCplt_FS`,
    generovaný soubor **má** `USER CODE` blok). Hlavička si to sama nabízí
    (`usb_console.h:26-27`), jen to používá k *doplňkovému drainu*, ne k uvolnění bufferu.
  🔑 Doporučuji **(a)** — opravuje příčinu, nesahá na generovaný kód a jde ověřit
  bez USB hosta. (b) je čistší, ale vyžaduje regen-safe zásah a testování s hostem.
- **Riziko opravy:** (a) nízké — mění se jen okamžik posunu `s_tail`, chování při plném
  ringu zůstává. ⚠️ Pozor, aby `s_pending` vstupovalo i do výpočtu `used`, jinak by
  „plný ring“ přestal být plný.
- **Vztah k lekcím:** **`L-0025`** („`close` není `free`“ — objekt předaný cizí knihovně
  přestaň vlastnit až ve chvíli, kdy ti přestane volat zpátky; tady je to buffer předaný
  USB stacku), **`L-0028`** (návratová hodnota se čte jako silnější tvrzení, než jaké dává).
- **Stav:** **opraveno 2026-09-19** v `f4f4ebf` **variantou (a)** (rozhodl uživatel),
  ⬜ **neověřeno na HW**. Přidán `s_pending` = bajty letící v CDC; uvolní se teprve až
  další `CDC_Transmit_FS` vrátí `USBD_OK`, což dokazuje, že `TxState` bylo 0, tedy že
  předchozí přenos **dokončil**.
  🔑 **Proč ne (b) přes `CDC_TransmitCplt_FS`, i když je „přesnější“:** callback po
  odpojení hosta uprostřed přenosu **nepřijde**, takže uvolnění navázané na něj by
  nechalo `s_pending` držený navždy a konzole by se jevila trvale plná — **horší než
  původní vada**. Varianta (a) se hojí sama. (Ověřeno, že (b) je technicky proveditelná:
  `CDC_TransmitCplt_FS` je registrovaná v `USBD_Interface_fops_FS:144` a má
  `USER CODE BEGIN 13` — argument „sahá na generovaný soubor“, kterým jsem ji nejdřív
  odmítal, **byl špatný**; rozhodlo až to samohojení.)
  ⚠️ **Vynutilo si to změnu politiky zahazování → viz F-0128**, která proto nebyla volitelná.
  ⚠️ `.text` se **zmenšil** o 24 B (ubyla PRIMASK sekce „drop nejstarší“), takže důkaz
  přítomnosti je symbolový: `s_pending` @`0x2401e3b4`, `.bss` +8 B, nový řetězec v `.elf`.

---

### F-0128 [S3] Konzole zahazuje data v OBOU směrech, a ani jedno nemá počítadlo

- **Místo:** `CM7/Core/Src/usb_console.c:51-58` (TX — zahodí nejstarší bajt),
  `:70-73` (RX — zahodí návrat `osMessageQueuePut`)
- **Popis:** Při plném TX ringu se mlčky **zahodí nejstarší bajt** (`s_tail++`), takže
  výpis přijde o *začátek*. Při plné `UartRxQueue` se mlčky zahodí **přijatý znak
  příkazu**. Ani jedno se nikde nepočítá, takže se o ztrátě nedozví ani uživatel, ani
  diagnostika.
- **Důkaz:**
  - `:56` `s_tail++;` s komentářem *„drop nejstarsi (console)“* — žádný čítač.
  - `:72` `osMessageQueuePut(UartRxQueueHandle, &b, 0u, 0u);` — návratová hodnota
    se **nikam neukládá**; při plné frontě `osMessageQueuePut` vrátí `osErrorResource`
    a bajt je ztracený.
  - 🔑 **Projekt má pro tuhle třídu precedens i hotový vzor:** `gpsraw` hlásí
    `RAW:37558 SENT:551 **OVF:0**` (přetečení GPS cesty, modul 13) a `status` hlásí
    `KONZOLE: N prikazu odmitnuto` (utnutý příkaz, F-0073, modul 14). Tady je stejná
    cesta bez čítače — tedy nekonzistence s vlastním standardem, ne jen chybějící drobnost.
- **Dopad:** **TX:** dlouhý výpis (`status`, `stats`, `sensors`, `membench`) přijde
  o začátek, když host nestíhá odebírat — uživatel dostane výpis, který *vypadá celý*,
  ale chybí mu hlavička, a nic to nehlásí. **RX:** ztracený znak změní příkaz na
  neplatný; to je většinou vidět (`ERR unknown command`), takže dopad je menší, ale
  u ztráty `\n` se dva příkazy slijí do jednoho.
  ⚠️ Kombinace s **F-0127** je nepříjemná: až se F-0127 opraví variantou (a), volného
  místa v ringu **ubude** o velikost letícího bloku, takže zahazování bude *častější* —
  a bez čítače se to nedozvíme.
- **Reprodukce:** `HYPOTÉZA — ověřit:` pustit `membench` (dlouhý výpis) s terminálem,
  který neodebírá, a porovnat začátek výstupu. Ze zdrojáku je zahození doložené.
- **Návrh opravy:** dvě `uint32_t` počítadla (`s_tx_dropped`, `s_rx_dropped`) + řádek
  v `status` (`KONZOLE: ... zahozeno TX n / RX m`). Přesně vzor **L-0017**, který
  projekt už třikrát použil (`FONTY:`, `g_flightrec_lost`, `OVF:`).
  ⚠️ Inkrement na TX straně patří **do PRIMASK sekce**, kde se `s_tail++` děje.
- **Riziko opravy:** nízké; čistě aditivní, nemění chování při zahození.
- **Vztah k lekcím:** **`L-0017`** (tichý přeskok je přípustný jen s počítadlem),
  **`L-0003`** / **`L-0028`** (zahozená návratová hodnota u RX).
- **Stav:** **opraveno 2026-09-19** v `f4f4ebf` (spolu s F-0127), ✅ **OVĚŘENO
  NA HW 2026-09-25** v rozsahu „počítadla existují a jsou dosažitelná bez
  sondy": `status` → **`KONZOLE: zahozeno TX 192 B / RX 0 B`**. Hodnota TX je
  nenulová po každém bootu (výpis bring-upu, než si host otevře port) a
  **během celé session už nevyrostla** ani při dlouhých výpisech
  (`membench`, `selftest`, `errlog`) — tedy ring za provozu nepřetéká.
  ⚠️ **Co ověřené NENÍ:** RX počítadlo je trvale 0, takže zahazovací cesta
  ve směru RX se nikdy neprovedla a její správnost tím doložená není.
  🔑 **Nebylo to volitelné.** Oprava F-0127 si vynutila změnu politiky: „zahoď nejstarší“
  (`s_tail++`) by zahazovala **právě letící** blok, protože in-flight oblast začíná
  přesně na `s_tail`. Zahazuje se tedy **příchozí** bajt — a protože tím zahazování
  zhoustne, počítadlo přestalo být kosmetika. Přidána `s_tx_dropped` i `s_rx_dropped`
  (RX: vyhodnocuje se návrat `osMessageQueuePut`) a řádek **`KONZOLE: zahozeno TX n B /
  RX m B`** ve `status`. ⚠️ Nenulové TX u `screenshot` přes USB = poškozený BMP.
  ⚠️ `continue` v zahazovací cestě bylo zkontrolováno proti **L-0033**: závěrečný
  `usb_console_tx_pump()` je **až za** smyčkou, takže se nepřeskočí.

---

### F-0129 [S3] `ipc_scpi_set_cfg` je napojený i na CM7 — druhá instance otevřeného F-0014 (dva producenti SPSC ringu)

- **Místo:** `CM7/Core/Src/ipc_scpi.c:141` (`ipc_cmd_push`), `:145`
  (`while (ipc_resp_pop(&r))`), napojení `CM7/Core/Src/freertos_task_uart.c:1447`
- **Popis:** `ipc_scpi_set_cfg()` je psaná jako **zápisová půlka CM4** (SCPI SET → `cmd`
  ring → `ipc_service` na CM7 to vyřídí). UART příkaz `scpi ipc <cmd>` na CM7 si ji ale
  přiřadí taky — a tím se **CM7 stane druhým producentem `cmd` ringu** (jehož
  producentem je CM4) **a druhým konzumentem `resp` ringu** (jehož konzumentem je CM4).
- **Důkaz:**
  - `freertos_task_uart.c:1447` `src_ipc.set_cfg = ipc_scpi_set_cfg;` — tedy na CM7,
    v **UartTasku**.
  - `ipc_scpi.c:141` `if (!ipc_cmd_push(&c)) return 0;` → zápis do `cmd` ringu.
  - `ipc_scpi.c:144-145` `ipc_resp_t r; while (ipc_resp_pop(&r)) { }` → vyčerpání
    `resp` ringu.
  - Legitimní vlastníci na CM7: `ipc_service()` je **konzument `cmd`** a **producent
    `resp`** (defaultTask, ~100 Hz). Takže `scpi ipc SET` z UartTasku běží proti
    defaultTasku na obou ringech.
  - 🔴 **Je to tatáž vada jako otevřený `F-0014`** („`ipccmd` je druhý producent SPSC
    ringu“, modul 3, **odložen** s odůvodněním *„IPC dnes prokazatelně funguje a oprava
    sahá do živé mezijádrové cesty“*). Odůvodnění odložení ale **zmiňuje jen `ipccmd`** —
    tuhle druhou cestu ne.
- **Dopad:** Souběh `scpi ipc <SET>` z konzole s SET z webu/TCP může na `cmd` ringu
  roztrhnout `head` → **ztracený nebo zdvojený příkaz**. Vyžaduje ruční diagnostický
  příkaz v tomtéž okamžiku jako zápis z webu, takže je to nepravděpodobné.
  ⚠️ Vyčerpání `resp` ringu je naopak **neškodné**: odpovědi se podle kontraktu
  (`ipc_scpi.c:110`, *„na vysledek necekame“*) nečtou, takže CM4 o nic nepřijde.
- **Reprodukce:** `HYPOTÉZA — ověřit:` `scpi ipc SENS:FREQ:GATE 10` z konzole současně
  s `POST /api/scpi` SET z prohlížeče. Statisticky se to chytá špatně; ze zdrojáku
  je dvojí producent doložený.
- **Návrh opravy:** 🔴 **Neopravovat samostatně.** Patří k **F-0014** jako druhá
  instance (`L-0012`) — jedno rozhodnutí, jeden zásah. Varianty, které F-0014 nabízí
  (zámek nad ringem × jediný producent přes požadavkový příznak), platí pro obě cesty.
  Minimální krok do té doby: **doplnit tuhle cestu do F-0014**, aby se při rozhodování
  nezapomněla — což tento nález dělá.
- **Riziko opravy:** středně vysoké (živá mezijádrová cesta) — proto odložení, ne oprava.
- **Vztah k lekcím:** **`L-0012`** (druhá instance téže vady se musí doložit ve stejném
  rozhodnutí), **`L-0054`**.
- **Stav:** **opraveno 2026-09-19** v `7cd8613`, ⬜ **neověřeno na HW**.
  Provedeno **jinak (lépe), než návrh F-0014 předpokládal:** soubor se kompiluje
  **dvakrát**, jednou per jádro, takže stačila **jádrová podmínka** `#if defined(CORE_CM4)`
  kolem operací s ringem. Tím zůstala **jedna** funkce a validace (brána/kanál/math) se
  **neduplikovala** — návrh „volat `ipc_cfg_apply()` přímo“ by vyrobil druhou kopii
  validace, tedy přesně **L-0018**.
  🔑 **Důkaz v obrazu, který zároveň ukazuje, že produkční cesta je nedotčená:**
  `ipc_scpi_set_cfg` má na **CM7 128 B**, na **CM4 548 B** (tam zůstal inlinovaný
  `ipc_cmd_push` + drain smyčka), a **CM4 `.text` je bajt za bajtem shodný** (242 504 B
  před i po).
  🔴 **F-0014 (instance `ipccmd`) ZŮSTÁVÁ OTEVŘENÝ — a jeho vlastní návrh opravy je pro
  něj špatný.** `ipccmd` má v kódu napsaný účel *„pošli příkaz PŘESNĚ tou cestou, kterou
  použije CM4 … umožňuje ověřit ovládací cestu CM4→CM7 **bez sítě, bez SCPI a bez webu**“*
  (kritérium W1). Návrh „na CM7 volat `ipc_cfg_apply()` přímo“ by ten příkaz **zrušil** —
  přestal by testovat to, kvůli čemu existuje. Řeší se samostatně, viz otázka
  v `AUDIT_STATUS.md`.

---

### F-0130 [S4] `0x040F` do `SYSCTRL` je jediná nerozebraná konstanta v souboru, kde je doložené všechno ostatní

- **Místo:** `CM7/Core/Src/tc358762.c:125`
- **Popis:** `tc_write(hdsi, TC_SYSCTRL, 0x040F)` s komentářem jen *„SYSCTRL - LCDC
  enable“*. V tomtéž souboru je přitom `LCDCTRL` rozebraný bit po bitu (včetně toho,
  **proč** se od hodnoty z Linuxu odečítá bit RGB888) a LCD timing doložený aritmetikou
  modeline (`800+1+2+47 = 850`, `480+7+2+21 = 510`).
- **Důkaz:**
  - `:33-40` — pro `LCDCTRL` je osm pojmenovaných bitových maker (`TC_LCDCTRL_VTGEN`,
    `_UNK6`, `_RGB888`, `_HSPOL`, …), `:116-122` vysvětluje odvození `0x00100050`.
  - `:125` — pro `SYSCTRL` **žádné makro bitů** a žádné odvození; `TC_SYSCTRL` je jen
    adresa (`:28`).
  - Hodnota odpovídá referenčnímu RPi driveru (`tc358762_write(ctx, SYSCTRL, 0x040f)`),
    ale **ta souvislost není v souboru napsaná**, přestože u ostatních registrů je.
- **Dopad:** Žádný na běh. Je to past pro ladění: kdo bude řešit, proč bridge nepouští
  obraz, nemá u tohoto registru za co zatáhnout — na rozdíl od všech ostatních zápisů
  v tom souboru.
- **Reprodukce:** přečíst `tc358762.c:116-126` — kontrast mezi dvěma sousedními zápisy.
- **Návrh opravy:** `docs:` — dopsat, že hodnota je **přejatá z referenčního driveru**
  (s odkazem, který už je v hlavičce souboru), a rozepsat aspoň to, co je z datasheetu
  známé (LCDC enable + clock select). Pokud rozklad není spolehlivě dohledatelný,
  napsat **to** — „nevíme, přejato z RPi“ je lepší údaj než mlčení.
- **Riziko opravy:** žádné (komentář).
- **Vztah k lekcím:** **`L-0006`** (u konstanty uveď, odkud je), checklist E (magické konstanty).
- **Stav:** **opraveno 2026-09-19** (`docs:`). U `TC_SYSCTRL, 0x040F` je nove
  napsane, ze hodnota je **prejata z referencniho driveru** (odkaz uz je v hlavicce
  souboru: raspberrypi/linux rpi-6.6.y `tc358762.c`), ze z datasheetu je znamy jen vyznam
  nizkych bitu (LCDC enable + vyber hodin) a ze **rozklad zbytku dohledatelny nebyl**.
  🔑 Zamerne se tam pise i to, co NEVIME: „nevime, prejato z RPi" je lepsi udaj nez mlceni,
  protoze pristi ctenar tak vi, ze tady neni co odvozovat, a nebude hodnotu menit podle
  vlastni domnenky. V temze souboru je pritom `LCDCTRL` rozebrany bit po bitu a LCD timing
  dolozeny aritmetikou modeline — tohle byla jedina nerozebrana konstanta.

---

## Co bylo zkontrolováno a je v pořádku

**C — umístění bufferů a DMA** (krok 3, z `.map`, ne ze zdrojáku):
`s_tx` @ `0x2401e3b0`, **0x400 = 1024 B**, `s_head`/`s_tail` @ `0x2401e3ac`–`0x2401e3af`
— vše v **AXI SRAM (RAM_D1)**, ne v DTCM. 🔑 **A cache maintenance tu opravdu není
potřeba:** `usbd_conf.c:349` má `dma_enable = DISABLE`, takže `s_tx` nečte žádný DMA
master — FIFO plní CPU z obsluhy USB. **`L-0001`/`L-0002` jsou tím N/A** (a kdyby se
USB DMA někdy zapnulo, `s_tx` by `clean` potřeboval — to je jediná věc, kterou by ta
změna vyžadovala navíc).

**D — souběh v `usb_console.c`** (mimo F-0127/F-0128):
- Kritická sekce přes **PRIMASK**, ne FreeRTOS, je **správně zvolená a zdůvodněná**
  (`:27-32`): `pump()` volají dva kontexty (defaultTask bez zámku a `_write` pod
  `uartTxMutex`) a musí fungovat i **před spuštěním scheduleru** (early `printf`
  z `main` USER CODE 2). Zámek FreeRTOS by tam nešel. ✅
- Délka maskování je ohraničená: `CDC_Transmit_FS` nikde nečeká (`TxState` busy →
  okamžitý `USBD_BUSY`), takže jde o jednotky µs. ✅
- `s_tail++` (zahození nejstaršího) je **taky pod PRIMASK** (`:54-57`), takže se
  nemůže potkat s `pump()`. ✅
- Explicitní `volatile` store bajtu **před** `s_head++` (`:60-63`) s vysvětlením, proč
  to překladač nesmí přehodit. ✅ `s_head`/`s_tail` jsou volně běžící `uint16_t`
  a `used = head - tail` přetečení zvládá samo. ✅
- `usb_console_on_rx` běží z USB ISR a používá `osMessageQueuePut` s timeoutem 0,
  tedy ISR-safe variantu. ✅ (Ignorovaný návrat = F-0128.)

**E — návratové hodnoty:** `tc358762_init` se vyhodnocuje na **obou** místech volání —
`main.c:528` (`goto display_skip` + `bootled_blink_once` + `g_display_init_step`) a
`freertos_task_uart.c:1877` (příkaz `panel`, hlásí `SELHAL` + krok). ✅ Uvnitř
`tc358762_init` se kontroluje **každý** `tc_write` (17 volání, všechna v `if (!…) return false`). ✅

**`tc358762.c` — doložená správnost:**
- 🔑 **Workaround na over-read v HAL je reálný a správně popsaný** (`:55-58`):
  `HAL_DSI_LongWrite` skládá payload po 32bitových slovech, takže u 6bajtového payloadu
  sáhne 2 B za konec pole; `uint8_t data[8]` to drží v mezích a **word count zůstává 6**.
  Kdyby se předalo `sizeof(data)`, bridge by dostal 8bajtový payload. ✅
- LCD timing přepočítán: `HSW 2 + HBP 47 + HDISP 800 + HFP 1 = 850` a
  `VSW 2 + VBP 21 + VDISP 480 + VFP 7 = 510` — **souhlasí** s uvedenou modeline
  (25,979 MHz, total 850×510). Packing `[high16 | low16]` je u každého zápisu
  zkontrolovatelný proti definici registru. ✅
- Zápisy do `D1S` registrů při vypnutém lane D1 jsou neškodné a **shodné s referenčním
  driverem** (ne opomenutí). ✅
- ⚠️ **Poznámka, ne nález:** DSI generic write je **jednosměrný** — `HAL_OK` znamená
  „FIFO to přijalo“, ne „bridge to dostal a nastavil“. `tc358762_init() == true` tedy
  nedokazuje nakonfigurovaný bridge. Projekt to ví a řeší jinde (`g_display_init_step`,
  řádek `DISPLEJ:` ve `status`, příkaz `panel`), takže to tu nezakládá nález.

**`ipc_scpi.c` — co je v pořádku:**
- **Kontrola kontraktu na vstupu:** `sn->magic != IPC_MAGIC || sn->version != IPC_VERSION`
  → `return 0` (`:30`). Snapshot z nesouhlasné banky se tedy **nepřečte**, místo aby se
  interpretoval jako data. ✅
- `memset(s, 0, sizeof *s)` **před** plněním (`:29`) → `set_cfg`/`read_log` zůstanou
  `NULL` a parser nepodporovanou operaci **odmítne** (`-230`) místo vymyšlení hodnoty;
  je to i napsané (`:92-94`). ✅
- Souřadnice se předávají `e7 → e7` **bez mezikroku přes float** (`:55-56`) — aplikovaná
  lekce **L-0031** / F-0070. ✅
- Převod `rtc_unix % 86400` na h/m/s je korektní a komentář správně upozorňuje, že
  datum se z těch polí nečte. ✅
- Readback `set_chan`/`set_gate_idx` se bere z **`ui_cfg`** (nastavení), ne z
  `channel_id` (co ohlásil rámec) — to je oprava „slepého readbacku“ z IPC v11 a
  komentář `:71-77` ji poctivě popisuje včetně toho, jak vada vypadala. ✅
- `ipc_scpi_set_cfg` **validuje lokálně před odesláním** (`:119-139`) a zdůvodňuje,
  proč nečeká na odpověď (jinak by modul přestal být jádrově neutrální). ✅
- ⚠️ **Drobnost, ne nález:** komentář `:32` *„pozice bitu jsou shodne, viz asserty
  vyse“* odkazuje na `_Static_assert`y, které v tomhle souboru **nejsou** — jsou
  v `ipc.c`, který je CM7-only. Ochrana projektu jako celku funguje (build CM7 by spadl),
  ale v translation unitu, který se linkuje do CM4, ji nic nevynucuje.

**Formátování:** v celém modulu **žádné `%f`/`%e`/`%g`** ani `fmt_fixed(..., >=4)`. ✅

## Nezkontrolováno / omezení tohoto běhu

- **Nic z tohoto modulu neběželo na HW v rámci auditu.** F-0127 je doložený z kódu
  middleware a HAL (zero-copy cesta), ale **skutečné poškození dat změřené není** —
  proto je reprodukce vedená jako `HYPOTÉZA` s konkrétním testem (screenshot na
  statické obrazovce).
- **`tc358762` SYSCTRL `0x040F`** se nedařilo rozložit proti datasheetu (TC358762 má
  veřejnou dokumentaci jen omezeně); hodnota je přejatá z referenčního driveru. To je
  přesně obsah F-0130.
- **Generovaný USB kód** (`usbd_conf.c`, `usbd_cdc_if.c`) auditovaný **není** — leží
  mimo rozsah (CubeMX mimo `USER CODE`). Pročetl jsem z něj jen to, co bylo potřeba
  k důkazu F-0127, a **jednu věc, která stojí za zmínku jako v pořádku**:
  `CDC_Transmit_FS` má v `USER CODE` guard `dev_state != USBD_STATE_CONFIGURED`,
  který brání dereferenci `pClassData` před enumerací — bez něj to podle komentáře
  padalo do HardFaultu při připojení kabelu. ✅
- **Chování při odpojení/reenumeraci USB za běhu** (co se stane s letícím blokem, když
  host zmizí) se staticky rozhodnout nedá; `TxState` by zůstalo 1 a `pump()` by vracel
  `USBD_BUSY`, dokud stack stav nevyčistí. Souvisí s F-0127 (varianta (a) by v tom
  stavu držela `s_pending` neuvolněný) — **při opravě to ověřit odpojením kabelu.**
