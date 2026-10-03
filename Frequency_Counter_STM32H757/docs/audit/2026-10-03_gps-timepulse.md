# Audit: GPS TIMEPULSE / UBX konfigurační cesta (2026-10-03)

- **Commit:** `3cbbdb9` (audituje se stav PO opravě F-0218; oprava je součástí tohoto běhu)
- **Jádro / doména:** CM7 / D2 (USART1), signál TIMEPULSE vede na desku FPGA (2.1)
- **HAL:** STM32H7xx HAL **1.11.6** (`stm32h7xx_hal.c:53-54`)
- **Spouštěč:** dotaz uživatele „proč nejde 1PPS signál z GPS", pak zadání „oprav kód a udělej audit"
- **Soubory:** `CM7/Core/Src/gps.c` (celý, 593 ř.), `CM7/Core/Inc/gps.h` (celý),
  `CM7/Core/Src/usart.c` (`USER CODE 1`, RX callbacky), volající UBX cest
  (`freertos.c:691`, `app_gpsdo.c:4481/4486`, `freertos_task_uart.c:1933`),
  zobrazení stavu TIMEPULSE (`app_gpsdo.c` ~944-962, ~4399, ~7176),
  HAL `stm32h7xx_hal_uart.c` (`HAL_UART_Transmit`, `HAL_UART_Receive_IT`,
  `UART_Start_Receive_IT`), schéma desky FPGA 2.1 (`FPGA_Core.kicad_sch`, `GPSDO.kicad_sch`),
  `Frequency_Counter_FPGA_Module/src/pins.cst`
- **Projité sekce checklistu:** D (souběh, ISR, reentrance UART), E (návratové hodnoty, vypadlá periferie),
  G (UART)
- **Neprojité (a proč):** A, B, C, F, H. Modul nekonfiguruje hodiny ani DMA. USART1 jede v IT
  režimu bez DMA, do Flash nesahá. Parser NMEA jako takový prošel auditem v modulu 13
  (`2026-09-12_parsery-scpi-gps.md`, F-0064..F-0072) a znovu se neprochází. Ten audit výslovně
  zapsal, že **„UBX odesílací cesta je prověřená jen staticky"**, a právě na ni se tento běh zaměřuje.

## Souhrn

GPS modul NEO-7M dostává od STM32 při startu UBX-CFG-TP5. Výstup TIMEPULSE vede
na desce 2.1 cestou J3 pin2 (`GPS_CLK_Out`) → U7 74LVC1G17 → `GPS_CLK_Buff` → R50 33R →
**FPGA PIN33_IOB23A** (`GPS_1PPS`). **Do STM32 1PPS nevede.** Firmware ale dál posílal
konfiguraci staré desky 2.0: **100 kHz s fixem** (reference pro HW PLL na listu GPSDO, která na
desce 2.1 už není). Proto na PIN33 žádný 1PPS nikdy nebyl (F-0218, **opraveno**).
Zbylé nálezy se týkají robustnosti: konfigurace se posílá jednou, naslepo a jen do RAM
modulu (F-0219). Fáze B bude muset rozlišit 10 Hz bez fixu od 1PPS na témže pinu (F-0220).
Dál zastaralé komentáře a drobnosti UART cesty.

**Verdikt: podmíněně funkční.** (F5 2026-10-03: F-0218, F-0219, F-0221, F-0222, F-0223, F-0224
opraveny; otevřený zůstává jen F-0220, požadavek na Fázi B.) Po opravě je konfigurace správná podle zadání. Že ji modul opravdu
přijal, ale firmware ověřit neumí, a na HW to zatím neběželo.

---

### F-0218 [S1] TIMEPULSE s fixem 100 kHz místo 1PPS: FPGA na PIN33 nikdy nedostal 1PPS

- **Místo:** `CM7/Core/Src/gps.c:358-377` (před opravou, `GPS_TP_FREQ_HZ`, `gps_config_timepulse()`)
- **Popis:** `freqPeriodLock` = 100 000 Hz. S fixem tedy z modulu šel 100 kHz signál, ne 1PPS.
  Hodnota pochází ze staré desky 2.0, kde TIMEPULSE napájel hardwarovou PLL
  (`../Frequency_Counter_FPGA_Module/BOARD_V20_CHECKLIST.md:123`: „GPS_CLK_OUT (PLL reference)").
- **Důkaz:** schéma 2.1 nemá na listu GPSDO žádný fázový detektor ani PLL (osazení listu: OCXO
  NVG47A1282, 2× OPA365, 2× LMK1C1104, 2× 74LVC1G17, TS5A3159). Net `GPS_CLK_Buff` končí na
  FPGA PIN33 (netlist, viz `AUDIT_STATUS.md` záznam 2026-10-02 „GPS 1PPS → FPGA"). `pins.cst:53`
  pin rezervuje jako `gps_1pps`. Zadání uživatele 2026-10-03: s fixem 1PPS, bez fixu 10 Hz.
- **Dopad:** deterministický. Na PIN33 byl s fixem 100 kHz, bez fixu 10 Hz, 1 Hz nikdy.
  Fáze B (time error OCXO vs 1PPS) by neměla co měřit. UI tvrdilo „Time Pulse 100 kHz",
  „100 kHz (disc.)" a blokové schéma „UART/1PPS" na spoji GPS→STM32, kudy 1PPS nevede.
- **Reprodukce:** osciloskop na R50/PIN33 s fixem: před opravou 100 kHz, po ní 1 Hz.
- **Oprava (provedena):** `GPS_TP_FREQ_HZ` 100000 → **1**. `GPS_TP_FREQ_NOFIX_HZ` zůstává **10**
  (zadání). Flags `0x6F` beze změny (`alignToTow` + `polarity` = náběžná hrana na začátku UTC
  sekundy), střída 50 %. Texty UI srovnány: GPS okno „Time Pulse 1PPS", Holdover
  „1PPS (FPGA)" / „10 Hz bez fixu", blokové schéma „UART". Upraven i `gps.h`.
- **Ověření:** build Release CM7 0 varování; `tools/audit.py` 92 OK / 0 / 2. `.text` 631 032 →
  631 024 B (−8 B: kratší řetězce; podle L-0090 to není důkaz, proto ještě dvě kontroly).
  `grep -ac` nad `.elf` najde nové řetězce 1×, staré 0×. Disassembly `gps_config_timepulse`:
  `strb #10 → [sp,#8]` (freqPeriod), `strb #1 → [sp,#12]` (freqPeriodLock), `0x80` na 19/23,
  `0x6F` na 28.
- **Riziko opravy:** nízké. Mění se jedno číslo v rámci, který modul už dnes přijímá. Spotřebitel
  signálu (FPGA PIN33) zatím pin nepoužívá (`pins.cst:53` zakomentováno), takže se nic nerozbije.
- **Vztah k lekcím:** nová **L-0113**; táž třída jako **L-0110** (hodnota podle staré nebo
  zamýšlené desky, ne podle netlistu).
- **Stav:** **opraveno v `3cbbdb9`**, ⬜ **neověřeno na HW**

---

### F-0219 [S3] Konfigurace TIMEPULSE je „vystřel a zapomeň": jednou, naslepo, jen do RAM modulu

- **Místo:** `gps.c:351` (`HAL_UART_Transmit` bez vyhodnocení), `gps.c:443` (jediné volání
  `gps_config_timepulse`), `freertos.c:691` (`gps_init` jednou ze `StartDefaultTask`),
  `gps.c:303` (parser zahodí vše, co nezačíná `$`, tedy i UBX-ACK)
- **Popis:** TP5 se pošle jednou při startu STM32. Firmware nijak nezjistí, jestli ho modul
  přijal: návratová hodnota se zahodí (L-0003) a odpověď UBX-ACK-ACK/NAK se nečte. Konfigurace
  žije jen v RAM modulu (komentář `gps.c:438` to přiznává).
- **Důkaz:** výše uvedené řádky. Nikde jinde se `gps_config_timepulse` nevolá (grep přes `CM7/`).
- **Dopad:** ve třech situacích platí místo zadání výchozí TIMEPULSE modulu a STM32 to nepozná:
  (a) modul se resetuje nebo ztratí napájení samostatně (je napájen z desky FPGA přes J3,
  ne z STM32); (b) při studeném startu modul ještě nepřijímá v okamžiku, kdy `gps_init` vysílá;
  (c) TX selže (timeout 100 ms).
  `HYPOTÉZA` (nedoloženo datasheetem v repozitáři): výchozí TP NEO-7M je 1 Hz s fixem a bez fixu
  žádný pulz. Pak by po ztrátě konfigurace 1PPS dál chodil a ztratil by se jen indikátor 10 Hz
  a nulový `antCableDelay`.
- **Reprodukce:** `HYPOTÉZA — ověřit:` power-cyklus celé sestavy, bez fixu (anténa odpojená)
  osciloskop na R50/PIN33. **10 Hz = konfigurace došla.** Žádný pulz = nedošla (nebo modul
  bez fixu pulz nedává). Totéž po power-cyklu jen desky FPGA.
- **Návrh opravy:** (1, levná) posílat TP5 periodicky, např. 1×/min a při přechodu bez fixu → fix.
  Rámec je idempotentní. Jen ho neposílat z hlídaného tasku blokujícím TX, viz F-0222.
  (2, poctivá) poll UBX-CFG-TP5 a porovnat odpověď. To vyžaduje UBX parser v `gps_feed_char`.
- **Riziko opravy:** (1) nízké, (2) střední (nový parser binárního protokolu).
- **Vztah k lekcím:** L-0003, L-0028, L-0086 (stav v externím čipu se po resetu čte zpět,
  nepředpokládá se)
- **Stav:** **opraveno v `12ec7ac`** (2026-10-03), varianta (1) podle rozhodnutí uživatele:
  `gps_tick()` z defaultTasku posílá TP5 znovu **1×/min** (`GPS_TP_RESEND_MS`). Aby to v hlídaném
  tasku nebyl spin, přešel zároveň `ubx_send` na IT TX (F-0222). Návratovou hodnotu teď vrací
  `ubx_send` a počítá ji `gpsraw` jako `UBX:odeslano/neodeslano`. **Doručení modulu to pořád
  nedokazuje** (ACK se nečte, varianta (2) neprovedena). ⚠️ `HYPOTÉZA` k ověření osciloskopem:
  opakované (i shodné) TP5 nesmí způsobit vynechaný nebo zkrácený pulz 1PPS. Kdyby ho
  způsobovalo, je to pro Fázi B horší než původní vada a periodu je třeba přehodnotit.
  ⬜ neověřeno na HW

---

### F-0220 [S3] Bez fixu jde na týž pin 10 Hz: Fáze B musí 1PPS validovat periodou, ne jen hranou

- **Místo:** `gps.c:363` (`GPS_TP_FREQ_NOFIX_HZ 10u`), `pins.cst:53` (`gps_1pps` na PIN33),
  `app_gpsdo.c:961` a `:4399` (UI odvozuje stav pulzu z NMEA `fix_quality`)
- **Popis:** podle zadání nese PIN33 s fixem 1PPS zarovnaný na UTC a bez fixu 10 Hz, který na UTC
  zarovnaný není. Kdyby časový čítač ve FPGA (Fáze B) bral každou náběžnou hranu jako časovou
  značku, bez fixu by měl 10 neukotvených značek za sekundu a smyčka GPSDO by regulovala podle
  nesmyslu. Přepnutí 10 Hz ↔ 1 Hz navíc řídí modul **podle vlastního kritéria „locked"**
  (`lockGnssFreq`/`lockedOtherSet`). To se nemusí krýt s NMEA fixem, ze kterého kreslí stav UI.
- **Důkaz:** flags `0x6F` (`gps.c:381`) obsahují `lockedOtherSet`, tedy přepínání sad řídí
  přijímač. UI čte `g.fix_quality` z GGA (`gps.c:174`).
- **Dopad:** dnes žádný (Fáze B neexistuje, PIN33 nezapojený v bitstreamu). Je to **požadavek na
  návrh** Fáze B: 1PPS přijmout jen při rozestupu hran 1 s ± tolerance (a/nebo při fixu hlášeném
  ze STM), první hranu po přepnutí zahodit. Do té doby UI tvrdí stav pulzu odvozený, ne změřený.
  Komentář u `app_gpsdo.c:944` to od opravy F-0218 přiznává.
- **Reprodukce:** nelze, Fáze B neexistuje.
- **Návrh opravy:** zapsat do `FPGA_PROTOCOL_V3_NAVRH.md` a do plánu Fáze B: validace periody
  a příznak platnosti `time_error` v rámci. Alternativa, kterou uživatel zatím **nezvolil**:
  bez fixu žádný pulz.
- **Riziko opravy:** —
- **Vztah k lekcím:** L-0089 (popisek tvrdí vlastnost, kterou veličina nemá)
- **Stav:** otevřeno, skupina **C** (až s Fází B)

---

### F-0221 [S4] Komentář v `gps_init` tvrdí neexistující zámek HAL a že po startu žádný TX neběží

- **Místo:** `gps.c:439-442`. Totéž tvrzení nese i projektová paměť (gps-todo, „HAL past 2026-06-28").
- **Popis:** komentář říká, že `HAL_UART_Transmit` drží `huart->Lock` a souběžný re-arm RX
  v callbacku by dostal `HAL_BUSY`. Dále tvrdí: „Po TX už na USART1 žádný další TX neběží".
  **Neplatí ani jedno.**
- **Důkaz:** HAL 1.11.6: `HAL_UART_Transmit` (`stm32h7xx_hal_uart.c:1119`) pracuje jen
  s `gState`, `HAL_UART_Receive_IT` (`:1377`) a `UART_Start_Receive_IT` (`:3494`) jen s `RxState`.
  `__HAL_LOCK` v žádné z nich není (grep těl funkcí), TX a RX jsou nezávislé stavové automaty.
  Za běhu přitom TX existuje: `gps_survey_in_cmd`/`gps_survey_disable_cmd` z UiTasku
  (`app_gpsdo.c:4481`, `:4486`) a `gps_config_gnss` z UartTasku (`freertos_task_uart.c:1933`).
- **Dopad:** funkčně žádný, běh je bezpečný. Komentář ale odrazuje od periodického opakování TP5
  (F-0219), protože popisuje past, která v této verzi HAL není.
  Vedlejší pozorování: dva souběžné TX (UiTask + UartTask) by vrátily `HAL_BUSY` druhému volajícímu
  a ten by se tiše ztratil. Obě cesty spouští ručně uživatel, takže je to teoretické.
- **Reprodukce:** statická (citace HAL).
- **Návrh opravy:** `docs:` přepsat komentář na skutečný stav (u HAL 1.11.6 souběh TX/RX bezpečný;
  pořadí TX → arm RX v `gps_init` je neškodné, ne nutné) a opravit záznam v paměti.
  ⚠️ Při upgradu HAL tvrzení ověřit znovu.
- **Riziko opravy:** nulové (komentář).
- **Vztah k lekcím:** L-0008, L-0028
- **Stav:** **opraveno v `12ec7ac`** (2026-10-03). Komentář se přepsal v `fix:` commitu, ne `docs:`,
  protože popisuje chování, které se tímtéž commitem měnilo (IT TX). Paměť projektu (gps-todo)
  opravena také.

---

### F-0222 [S4] Blokující TX na 9600 Bd v UiTasku trvá ~37 ms (pravidlo „žádný spin > ~10 ms")

- **Místo:** `gps.c:351` (polled `HAL_UART_Transmit`), volané z UiTasku přes
  `app_gpsdo.c:4481/4486` (tlačítko START/STOP v okně SURVEY)
- **Popis:** TMODE2 má 28 B payload + 8 B obálka = 36 B × 10 bitů / 9600 Bd = **37,5 ms** aktivního
  čekání v UiTasku. TP5 (40 B, 41,7 ms) a GNSS (44 B, 45,8 ms) běží v defaultTasku
  (jednou při bootu) a v UartTasku (nehlídaný).
- **Důkaz:** přepočet z délek rámců v `gps.c:387-426` a baudu `gps.c:433`.
- **Dopad:** jedno zaškobrtnutí UI na stisk tlačítka. Watchdog (2,5 s) nehrozí. Stává se to
  relevantní, kdyby se TP5 začal opakovat z hlídaného tasku (F-0219).
- **Návrh opravy:** `HAL_UART_Transmit_IT` se statickým bufferem, nebo odeslání delegovat
  do UartTasku přes požadavek (vzor `g_*_req`).
- **Riziko opravy:** nízké až střední (IT TX na USART1 vedle IT RX).
- **Vztah k lekcím:** L-0040
- **Stav:** **opraveno v `12ec7ac`** (2026-10-03), mimo původní triáž (byla C): IT TX se statickým
  bufferem, test `gState` + kopie + start v kritické sekci, čekání na předchozí rámec přes
  `vTaskDelay` max 200 ms. Bylo to nutné kvůli F-0219 (opakování z hlídaného defaultTasku).
  ⬜ neověřeno na HW

---

### F-0223 [S4] Každá odpověď UBX (ACK/NAK) zničí následující NMEA větu

- **Místo:** `gps.c:449-467` (`gps_feed_char`), `gps.c:303` (`if (l[0] != '$') return;`)
- **Popis:** binární bajty UBX-ACK (`B5 62 05 0x …`, 10 B) nekončí `\r\n`, takže se přilepí na
  začátek `s_line` a za ně přijde další NMEA věta. Hotový řádek pak začíná `0xB5`, ne `$`,
  a parser ho zahodí celý, včetně té NMEA věty.
- **Důkaz:** výše uvedené řádky. Resynchronizace na `$` v `gps_feed_char` neexistuje.
- **Dopad:** po každém UBX příkazu se ztratí jedna NMEA věta (typicky RMC nebo GGA, tedy jeden
  1Hz vzorek). Dnes 1× při bootu a na vyžádání. Při periodickém TP5 (F-0219) by to bylo
  1 věta/min. Zanedbatelné, ale tiché.
- **Reprodukce:** `gpsraw` těsně po `gps glonass`: `last=[...]` může ukázat řádek s binárním
  prefixem.
- **Návrh opravy:** v `gps_feed_char` při `c == '$'` začít nový řádek (`s_len = 0; s_drop = 0;`).
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0017 (tichý přeskok bez počitadla)
- **Stav:** **opraveno v `01c7e25`** (2026-10-03): `$` vždy začne nový řádek (a zruší zahazování
  po přetečení), useknutý začátek počítá `s_resyncs` → `gpsraw` `RSY:n`. ⚠️ Od F-0219 roste `RSY`
  o ~1/min (ACK na periodické TP5), to je normální. ⬜ neověřeno na HW

---

### F-0224 [S4] Dokumentace popisovala 100 kHz a 1PPS do STM32

- **Místo:** `CLAUDE.md:2194` (GPS okno „TP 100 kHz/10 Hz … GPSDO PLL ref"),
  `HW_OVERENI_PRUCHOD.md:119` („TP 100 kHz"), `../STATUS.md:21` (`GPS ──1PPS/UTC──► STM32H757`),
  `gps.h:3-6` (hlavička: „GpsTask je vybírá", „parsuje $xxRMC + $xxGGA". Drain je v defaultTasku,
  parsují se i GSA/GSV).
- **Popis / důkaz:** viz F-0218. Schéma STM desky nemá žádný net 1PPS (jen `ETH_PPS_OUT`),
  uživatel potvrdil 2026-10-03.
  ⚠️ Diagram v `../STATUS.md:20-27` celý popisuje desku 2.0 (4fázový vernier, MC100EP016A ÷4/÷16).
  Opravuje se jen šipka GPS, zbytek je mimo rozsah.
- **Stav:** první tři místa **opravena `docs:` commitem tohoto běhu**; hlavička `gps.h` opravena `docs:` commitem 2026-10-03
  (drain v defaultTasku, GSA/GSV, souřadnice v e7, UBX příkazy)

---

## Co bylo zkontrolováno a je v pořádku

- **Rámec TP5 bajt po bajtu** proti rozložení UBX-CFG-TP5 v0 (32 B): `tpIdx`@0,
  `freqPeriod`@8, `freqPeriodLock`@12, `pulseLenRatio`@16 (`0x80000000` = 2³¹ = 50 % v jednotkách
  2⁻³²), `pulseLenRatioLock`@20, `flags`@28 = `0x6F` = active | lockGnssFreq | lockedOtherSet |
  isFreq | alignToTow | polarity. `isFreq=1` → periody v Hz. `alignToTow` vyžaduje periodu,
  která dělí 1 s: 1 Hz splňuje. Ověřeno i v disassembly (viz F-0218).
- **Fletcher checksum UBX** (`gps.c:348-349`) počítá přes `cls..payload`, tedy od indexu 2. ✅
- **`ubx_send` meze:** `n ≤ 64`, `f[80]`, max 72 B. ✅ (shodně s modulem 13)
- **Souběh TX/RX USART1** (HAL 1.11.6, viz F-0221): TX z UiTasku/UartTasku vedle RX IT je
  bezpečný. Re-arm RX v callbacku (`usart.c:210-229`) vyhodnocuje návrat a selhání loguje (F-0153). ✅
- **Popisky UI se vejdou do změnových bufferů:** `c_tp[20]` (GPS okno) pro „Time Pulse 1PPS"
  (15 zn.). `c_tp[16]` (Holdover) pro „10 Hz bez fixu" (14 zn.) a „1PPS (FPGA)" (11 zn.).
  Původní varianta „10 Hz (bez fixu)" (16 zn.) by `c_tp[16]` utnula, proto se zkrátila
  před commitem. Font `mono_18` / `kv_row` má plnou sadu znaků.
- **Cesta signálu ve schématu 2.1:** J3 pin2 → U7 → `GPS_CLK_Buff` → R50 → PIN33. LED D1
  „GPS_CLK" na téže síti bude po opravě s fixem blikat 1× za sekundu (dřív při 100 kHz trvale svítila).

## Nezkontrolováno / omezení tohoto běhu

- **Nic neběželo na HW.** Rozhodující měření: osciloskop na R50/PIN33 po **power-cyklu**:
  bez fixu 10 Hz, s fixem 1 Hz, náběžná hrana zarovnaná na UTC sekundu (porovnat s jinou
  1PPS referencí, je-li k dispozici).
- **Datasheet / Receiver Description u-blox 7 v repozitáři není.** Rozložení TP5 a výchozí
  hodnoty jsou ověřené proti známé specifikaci, ne proti dokumentu v projektu. Proto je výchozí
  stav modulu v F-0219 `HYPOTÉZA`.
- **`antCableDelay` = 0** (rámec ho nuluje, stejně jako před opravou). U-blox má výchozí hodnotu
  (`HYPOTÉZA`: 50 ns). Pro disciplinaci kmitočtu je konstantní posun lhostejný, pro absolutní
  čas (TIE vůči UTC) ne. Rozhodnutí patří ke kalibraci Fáze B, ne sem.
- **FPGA strana (Fáze B) neexistuje.** PIN33 je v `pins.cst:53` zakomentovaný, takže spotřebitel
  1PPS se zatím ověřit nedá.
