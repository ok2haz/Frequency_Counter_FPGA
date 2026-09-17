# AUDIT_STATUS.md — stav auditu

> Aktualizuj **na začátku a na konci každého sezení**. Tenhle soubor je jediný
> zdroj pravdy o tom, co je hotové — kontext CLI sezení se nepřenáší.

**Poslední aktualizace:** 2026-09-17 (16. sezení — **modul 17 = čas, alarmy,
watchdog**; týž den modul 16 = perzistence
a záznamníky**: `datalog`, `flightrec` + nový `errlog`, `syscfg`, `setup`, `calib`;
týž den fáze oprav, skupina A)
**Fáze:** modul 1 prošel F5 a je ✅ ověřený na HW; moduly 2–6 a 8 prošly F3 **i F5**, ale
⬜ **neověřeně na HW po power-cyklu** (nic z těch oprav studený start neviděl).
**Modul 7 má jen zapsané nálezy** — F5 zatím neproběhla (F-0025…F-0031, z toho 2× S2).
**Modul 8 nemá otevřený nález.**
**F1 je hotová** — `docs/ARCHITECTURE.md` doplněn 2026-09-10 z auditů 1–5 (dluh uzavřen).
**Branch:** `audit/2026-09-09-hodiny-pwr` (vychází z `feat/web-dashboard-v12`, commit `8521130`)
**Pokračovat zde:** 🔴 **`IPC_VERSION` je nově 15 → FLASHNOUT OBĚ BANKY** (bank1 CM7
+ bank2 CM4). Do té doby si jádra nebudou rozumět, pokud se přeflashne jen jedno —
a projeví se to tím, že CM4 přestane přijímat snapshot, **aniž by header přestal
svítit `4:xx%`** (viz „Nesoulad bank je NEVIDITELNÝ" v `CLAUDE.md`).
🔑 **Modul 15 je hotový celý** (11 z 11), včetně nové kontroly hranice JSON
(`tools/spa/json_kontrakt.py`, krok **5b** řetězce `check.py`).
🔴 **NOVÉ 2026-09-16 — modul 16 (perzistence a záznamníky), 14 nálezů.**
✅ **Skupina A opravena 2026-09-17 (6 nálezů, 5 commitů + 1 pojistka), ⬜ neověřeno na HW.**
Nejzávažnější byl **F-0089 [S1]**: tři nastavení datalogu (zap/vyp, úložiště,
perioda) se z flash obnovovala **jen při studeném startu**, přestože nejsou v BKP —
po každém teplém resetu (reflash, Menu→Restart, watchdog) se tiše vrátila na výchozí
hodnoty. Opraveno spolu s **F-0093** (falešné `ERRLOG_K_CFG` při obnově), dál
**F-0090** [S2] (okno pro dereferenci NULL při re-initu), **F-0094**, **F-0097**,
**F-0099**. Lekce **L-0050**…**L-0053** + rozšíření **L-0026**.
🔑 **Co ověřit na desce jako první:** `datalog off` → počkat >2 s → Menu→Restart →
`status` musí pořád hlásit `DATALOG W25Q OFF` (před opravou se vrátilo na `ON`).
Zbytek kontrolního seznamu je na konci sekce „Fáze oprav" v nálezovém dokumentu.
⚠️ **Otevřeno zůstává 8 nálezů modulu 16** — skupina B (F-0091 mazání nejnovějšího
dumpu, F-0092 crash bez jména tasku, F-0095 čtení po záznamech, F-0098 tichý init)
čeká na **rozhodnutí o variantě**; **F-0096 patří k otevřenému F-0052** (třetí cesta,
která píše `g_meas_cfg` bez kritické sekce) — opravovat jedním zásahem, ne zvlášť;
F-0100…F-0102 jsou kosmetika pro F6.
🔴 **NOVÉ 2026-09-17 — modul 17 (čas, alarmy, watchdog), 10 nálezů.**
✅ **Skupiny A+B opraveny 2026-09-17 (9 z 10, v 6 commitech), ⬜ neověřeno na HW.**
Verdikt **podmíněně funkční**. Nejzávažnější **F-0103 [S2]**: stav zvukového
patternu a `beeper` píšou TŘI úlohy (defaultTask, UartTask přes `alarm_test`,
UiTask přes `beeper_boot_melody`), ačkoli `alarm.c:133` deklaruje jediného
vlastníka — ztracený zápis do `s_on` může nechat pípák trvale troubit.
Dál **F-0104** (selhání propagace `PR`/`RLR` zkrátí watchdog 4 s → 0,5 s a nikdo
se to nedozví), **F-0105** (zotavený stall se ohlásí u příštího nesouvisejícího
resetu), **F-0106** (HardFault jako jediný píše magic první → může ohlásit cizí PC),
**F-0107** (`rtc_crash_assert` bez `DBP`), **F-0108** (nenaběhlý LSE zabije celý
přístroj — **čeká na rozhodnutí o politice**, souvisí s otevřeným F-0007).
🔑 **Premisa opravy F-0089 tím OVĚŘENA:** výčet v `MX_RTC_Init` obsahuje právě
těch deset globálů, takže kontrola v `check_lessons.sh` odvozuje správnou množinu.
⚠️ **Otevřen zůstává jediný nález modulu 17 — F-0108** (nenaběhlý LSE zabije
celý přístroj): leží v generovaném `SystemClock_Config()` **bez `USER CODE`**
a patří k otevřenému **F-0007** jako JEDNA politika „co dělat při výpadku
oscilátoru". Neotevírat samostatně.
🔑 **Co ověřit na desce jako první:** `status` → nový řádek
`WATCHDOG: PR=4 RLR=2000 -> 4000 ms | pipak ok`; cokoli jiného u `PR`/`RLR`
(zvlášť `<== NESEDI`) je nález sám o sobě. Pak `beep test` během běžícího
alarmu — pípák nesmí zůstat troubit.
Dál: **skupina B modulu 16**, **F5 pro modul 11** (F-0052 je S2) a
**rozhodnout F-0039** (blokuje ověření F-0037 na desce).
⚠️ Opravy modulu 15 míří do **blobu `SPA_HTML`**, takže po nich MUSÍ projít
`python tools/spa/check.py --build` celý (8 kroků) — zvlášť krok 8 (`nm` nad
`SPA_HTML` = počet bajtů + 1), který jediný definitivně chytí utnutý literál.
Pak ⬜ **naflashovat a ověřit po POWER-CYKLU** — čeká na to modul 14
(`b1aa262`), oprava okna CHYBY (F-0063) a `21ac04e` (buffery do `.bss`, dotýká se
obou jader). `IPC_VERSION` se nemění, takže banky jdou flashovat nezávisle.
🔑 **F-0055 + F-0074 jsou rozhodnuté a odložené do `../STATUS.md` TODO #243:**
zásobník UartTasku se nejdřív konzistentně zvětší v `.ioc` (uživatel), teprve pak
se sáhne na kód — přesun bufferů do `.bss` je konkurenční oprava téhož.
⚠️ **Do té doby `selftest` z konzole nespouštět** (F-0055 desku deterministicky resetuje).
🔒 Rezervu hlídá `scripts/check_lessons.sh` (rámec `UartTask_run` ≤ 1024 B, dnes 700 B).
🔑 **Co u modulu 14 ověřit na desce:** příkaz delší než 95 znaků musí skončit
`ERR prikaz delsi nez 95 znaku - NEPROVEDEN` (a `status` → `KONZOLE: N prikazu odmitnuto`),
`scpi SENSe:FREQuency:APERture 10` musí nastavit hradlo **10 s**, ne 1 s,
`fpgasim on 99999999999999999999` nesmí dát nesmyslný kmitočet (strop 4 GHz)
a `fpgaraw` má vypsat 64 bajtů beze změny.
🔑 **Co u modulu 13 ověřit:** `gps` a `gpsraw` přes UART (fix se musí chytit i s nově
povinným checksumem, `OVF:` má zůstat 0), `scpi SYST:GPS:POS?` → 7 desetin,
`scpi SYST:DATE?` bez antény → `9.91E37`, a **SURVEY nechat běžet ≥1 h** — rozptyl
by nově měl klesnout pod dřívější mez ~0,4 m (F-0070).
Pak **F5 pro modul 11** (**F-0052 je S2** — dotyková cesta píše `g_meas_cfg` bez
kritické sekce, zatímco SCPI i IPC ji mají).
Pak **rozhodnout F-0055** (`selftest` z UART shodí desku — tři varianty) a **F5 pro
modul 11** (**F-0052 je S2** — dotyková cesta píše `g_meas_cfg` bez kritické sekce,
zatímco SCPI i IPC ji mají).
⚠️ **Opravy modulu 12 míří do BANKY 2 (CM4)** — jiná cesta než všechno dosavadní.
`IPC_VERSION` se žádným z nich nemění, takže přeflashování obou bank nutné není.
⚠️ **F-0063 [S2] přišel z PROVOZU, ne z auditu** (2026-09-11): okno CHYBY se po tapu
na dlaždici nikdy nezobrazilo, protože `render_errlog` neflipovalo. Opraveno (`1b21c82`),
⬜ neověřeno na HW. Patří k modulu 11 a je to **jediný nález, který našel uživatel dřív
než audit** — modul 11 fázi oprav ještě neprošel, takže tam takové vady ještě mohou být.
🔑 **Co na desce ověřit** (z UART `status`, bez sondy — CM4 nemá konzoli):
`HTTP(CM4)`/`SCPI(CM4)` selftest PASS a `NET:` s IP; pak z prohlížeče **SPA + dlouhá
historie** (F-0056), **opakované requesty v řadě** (F-0057) a `/api/state` bez
`expected_len` (F-0060). SSE timeout (F-0059) se ověří jen odpojením klienta od sítě.
⚠️ **`selftest` z konzole zatím NESPOUŠTĚT** — deterministicky resetuje desku (F-0055).
🔴 **Všechny moduly 1–16 mají zapsané nálezy; bez dokončené fáze oprav jsou 11 a 16.**
🔴 **OPRAVA TVRZENÍ (2026-09-16):** do té doby tu stálo *„tím je auditovaný veškerý
vlastní kód projektu"* — **neplatilo to.** Mimo moduly 1–15 leželo ~4 500 ř.
vlastního kódu a 2026-09-13 k nim přibyl celý nový podsystém **`errlog`** (FW v0.9.0,
IPC v17). Modul 16 z toho pokryl 2 456 ř. (perzistence a záznamníky).
**Pořád neauditované zůstává** (~1 100 ř., po modulu 17): `membench.c` (722),
`meas_present.c` (404), `sdram_log.c` (307), `screenshot.c` (175), `si5356.c`,
`encoder.c`, `autocal.c`, `phase_noise.c`, `sensor_hist.c`, `meas_math.c`,
`ads1115.c`, `ws_panel.c`, `ft5x06.c`
— plus **vendor kód** (HAL, FatFs, lwIP, CMSIS), generovaný CubeMX kód mimo
`USER CODE` bloky a **fonty** (generovaná data).
Zbytek čeká na **flash + POWER-CYKLUS** — nic z 2026-09-11 neběželo na desce.
⚠️ **F-0039 čeká na rozhodnutí uživatele** (tři varianty) a blokuje ověření F-0037 na desce.
⚠️ Modul 6 byl v tabulce původně zapsaný jako „SPI2/FPGA, QSPI, SDMMC“ — přes 3000 řádků na
jedno sezení. **SDMMC proto dostalo vlastní řádek (modul 7)**, aby se neauditovalo povrchně.
⚠️ **Týmž způsobem se 2026-09-10 rozdělil modul 8** („aplikační logika UI“ = `app_gpsdo.c` +
`screens/*` + `libui`, dohromady **14 375 řádků** bez fontů — 4× víc než modul 7). Nově:
**8 = vykreslovací řetězec** (2 247 ř.), **9 = hlavní obrazovka** (3 272 ř.),
**10 = aplikační okna, navigace, model fokusu** (9 188 ř.).
⚠️ **A modul 10 se 2026-09-11 rozdělil potřetí, přesně jak si ten řádek žádal.** `app_gpsdo.c`
má **9 031 řádků a 310 funkcí** — 2,8× modul 9. Dělící čára vede podle **třídy rizika**, ne
podle velikosti: **10 = strojovna** (navigace, model fokusu, registr tlačítek, vstup dotyk/
encoder, tiky, screensaver, varovný pruh; ≈ 2 800 ř. — sdílený stav a cross-task API, tedy
S1/S2) a **11 = jednotlivá okna** (~45 funkcí `render_*` + kreslicí helpery; ≈ 6 200 ř. —
převážně kreslení, tedy očekávané S3/S4). Oba moduly jsou v témže souboru; nálezový dokument
modulu 10 v sekci „Nezkontrolováno" přesně vymezuje, co zbývá na 11.

**Otevřené po F5** (5× S3 + 1 částečně opravený S1 z modulů 1–6; modul 7 přidal
2× S2, 4× S3 a 1× S4, které F5 zatím neprošly). Čísla v tabulce výše se **odvozují
z nálezových dokumentů** — ověř je `python tools/audit_stav.py --kontrola`:
- **F-0003** [S3] `VOSRDY` bez timeoutu — *odloženo*: leží v generovaném `SystemClock_Config()`
  bez `USER CODE` (regen by opravu smazal) a špatná mez by pustila 480 MHz dřív, než se
  ustálí regulátor. Vrátit se, až se objeví „deska občas nenaběhne“.
- **F-0007** [S3] ztráta HSE = mrtvý přístroj — *čeká na rozhodnutí o politice*: zůstat mrtvý
  ale rozlišitelně (levné), nebo nabootovat na HSI a měření označit za neplatné (drahé, mění
  časování všech sběrnic).
- **F-0014** [S3] `ipccmd` je druhý producent SPSC ringu — *odloženo*: IPC dnes prokazatelně
  funguje (CM4 alive, ETH/web běží) a oprava sahá do živé mezijádrové cesty.
- **F-0016** [S3] `.ipc_shared` je prázdná rezervace — *odloženo*: oprava znamená zásah do
  **linker skriptů obou jader** (pravidlo 6 → jen s výslovným souhlasem).
- **F-0017** [S3] `ipc_stamp()` maže i blok CM4 — *odloženo* ze stejného důvodu jako F-0014.
- **F-0018** [S1] je opravený jen **částečně** (ztráta už není tichá); dvoufázový zápis zbývá.

Otevřené otázky na HW: crash black-box pro
`Error_Handler()` volaný **před** `MX_RTC_Init()` (modul 1) a retenční test `membench`
nad rozsahem `bg_cache` (modul 2, F-0013 — nástroj `bgcheck` už existuje).

## ✅ HW OVĚŘENO 2026-09-12 (moduly 12 + 13, `Reset: power-on`)

Uživatel naflashoval obě banky a poslal výpisy. **`Reset: power-on`**, takže
požadavek `L-0010` (ověření až po power-cyklu) je splněný.

| nález | co se ověřilo | důkaz z desky |
|---|---|---|
| **F-0065 + F-0066** | povinný checksum nic nerozbil a rámce se nezahazují | `gpsraw` → `RAW:37558 SENT:551 **OVF:0**`, `gps` → `FIX:1 SAT:10` — fix se chytil i s nově povinným checksumem, a nové počítadlo přetečení je na nule |
| **F-0070** | souřadnice mají **7 desetin** na obou cestách | `gps` → `50.2284400N 14.4838175E`; `scpi SYST:GPS:POS?` → `50.2284440,14.4838141`. 🔑 Sedm desetin je zároveň důkaz, že běží **nový** `fmt_scpi_deg7` — starý `fmt_scpi_deg6` tiskl `%06ld`, tedy šest |
| **F-0068** | `SYST:DATE?` vrací platné datum, když čas znám | `scpi SYST:DATE?` → `2026,9,12`. ⚠️ Záporná větev (bez antény → `9.91E37`) ověřená **není** |
| **modul 12 (CM4)** | opravy sítě běží a selftesty prošly | `SCPI(CM4): selftest PASS`, `HTTP(CM4): selftest PASS`, `NET: UP 100 Mbit full, IP 10.0.0.106`, `CM4: alive … stall x0` |
| ostatní | nic se nerozbilo | `DISPLEJ: bring-up OK`, `LTDC podtečení 0/326`, `DMA2D chyb 0`, `GPIO HLIDAC: 0 oprav`, `I2C4 … err 0` |

🔴 **A jedno zhoršení, které to měření odhalilo:** `stack Uart free` **168 B**
(bylo 976 B). Změřeno, že z toho **48 B způsobily opravy modulu 13** a zbytek je
tím, že se poprvé spustil `scpi` z konzole — ta cesta má ~1,6 kB rámců a F-0055
ji neměřil. `21ac04e` to vrátil s úrokem (−140 B proti stavu před modulem 13),
ale **F-0055 je tím naléhavější** — viz nový důkaz v jeho nálezu.

⚠️ **Neověřeno zůstává:** F-0064 (chce `nc` na port 5025), F-0067 (chce vadnou
NMEA větu), F-0069 (složená zpráva přes `;`), F-0063 (dlaždice „Chyby (log)"),
a hlavně **F-0070 v tom, kvůli čemu vznikl** — SURVEY musí běžet ≥1 h, aby se dalo
říct, jestli rozptyl klesl pod dřívější mez ~0,4 m.

## ✅ HW OVĚŘENO 2026-09-11 (po flashi a POWER-ON resetu)

První běh oprav z 2026-09-11 na desce. `status` hlásil `Reset: power-on`, takže
požadavek `L-0010` (ověření až po power-cyklu) je splněný.

| nález | co se ověřilo | důkaz z desky |
|---|---|---|
| **F-0037** | σy@1s už není nulová a má správný řád | `STATISTIKA: sigma_y@1s = 6224849 e-15` = **6,2e-9** (dřív `0 e-15`); mezi dvěma čteními se hýbe → živé vzorkování běží |
| **F-0039** | rekonstrukce ADEV už neblokuje | `ADEV rekonstrukce: hotova, vlozeno 0 z 133763 zaznamu` už při **uptime 33 s** — sonda našla log bez platného měření a přeskočila ho (dřív 1 h 47 min) |
| **F-0047** | diagnostika okna hlásí pravdu | `s_view=8` (spořič) → po `ui` → `s_view=0`, `zmen` 3→4. **Obě ta okna byla mezi 17, která se dřív do diagnostiky nikdy nezapsala.** |
| **F-0048** | počítadlo hloubky navigace | nový řádek `UI: navigace (ZPET) max 0/6`, bez značky přetečení |
| **F-0028** | 🔑 **pojistka na taktu se uplatnila** | `sbernice: 4-bit, SDMMC_CK 16.000 MHz, Default Speed (limit 25 MHz)` — HS přepnutí na téhle kartě **neprošlo**, takže takt zůstal v mezích a karta funguje (`f_mount: 0 OK`, 30 GB). `sd diag` se vrátil okamžitě → zbytkové riziko zatuhnutí se neprojevilo. ⚠️ **Tohle měření platilo pro 16MHz variantu; na žádost uživatele je provozní takt od té doby 32 MHz — viz níže.** |
| **F-0031** | odvozený výpis | `sd diag` nově uvádí **režim i platný limit**, ne jen takt |
| **F-0027** | nic se nerozbilo | `GPIO HLIDAC: 0 oprav` |
| modul 8 | meze z F-0033/F-0036 drží | `DMA2D: chyb 0, timeout 0 | max cekani 62.371 ms (mez 500)`, `LTDC podteceni 0/247`, `FONTY: 0` |

### Druhé kolo HW ověření 2026-09-11 (po návratu SD na `.ioc`, `Reset: power-on`)

| nález | co se ověřilo | důkaz z desky |
|---|---|---|
| **F-0028** | takt dle `.ioc` **a stav není tichý** | `sbernice: 4-bit, SDMMC_CK 32.000 MHz, Default Speed (limit 25 MHz)  <-- NAD LIMITEM (vedome, viz SD_CLKDIV)` |
| **F-0028** | 🔑 **na 32 MHz opravdu JDOU DATA**, ne jen mount | `sd test` → *„8 KB zapsano a precteno zpet bit po bitu shodne"*; **zápis 6,17 MB/s, čtení 9,25 MB/s** (1 MB soubor). Tím je doložená empirická opora pro rozhodnutí jet nad limitem DS. |
| **F-0029** | kontroly `f_write`/`f_close` nedělají falešné selhání | `sd export 2000` → `SD: export OK, 2000 zaznamu` |
| **F-0031** | jméno souboru už nelže | `SD: exportuji cast logu do GPSDOnnn.CSV` + `SD: zapisuji GPSDO004.CSV` |
| **F-0025** | mechanismus remountu (částečně) | `sd unmount` → `sd mount` → `OK (namountovano)` a `sd diag` zase 32 MHz 4-bit. Protáhlo `sd_export_unmount()` (reset `is_initialized`) + celý `BSP_SD_Init`. ⚠️ **Cesta přes tik** (fyzické vytažení karty) tím ověřená NENÍ. |
| F-0026, F-0027, F-0030 | nepřímo | mount, export i remount projdou; `GPIO HLIDAC: 0 oprav` |
| F-0037, F-0039 | drží i na novém obrazu | `sigma_y@1s = 6681405 e-15`, `ADEV rekonstrukce: hotova` při uptime 32 s |

### ✅ Ruční doověření uživatelem 2026-09-11 (to, co z UARTu nešlo)

- **F-0025 — HW test OK.** Fyzické vytažení karty za běhu a opětovné vložení: mount
  projde. Tím je ověřená **skutečná podstata nálezu** (cesta přes `sd_export_tick`),
  ne jen mechanismus `sd_export_unmount()`. Dřív to skončilo 30s čekáním a trvalým
  stavem ERROR až do dalšího vytažení.
- **F-0046 + F-0051 — OK.** Fokus po tapu v okně se seznamem sedí na stisknutém
  prvku a paměť fokusu per okno drží i při návratu prstem. Tím je doložené obojí:
  spojený indexový prostor (`ln + i`) i to, že `focus_load()` se volá při vstupu do
  okna, ne až při první události encoderu.

🔑 **Tím jsou VŠECHNY prakticky ověřitelné opravy z 2026-09-11 ověřené na HW.**
Neověřené zůstávají už jen tři, a u každé je důvod strukturální, ne opomenutí:
- **F-0038** (dvojí čtení RTC) — okno je mikrosekundové, pozorovat se nedá.
- **F-0049** (`case 49/50/13` v `render_view`) — projeví se až při úklidu banneru po
  mrtvé I2C4; vyvolat to jde jen haltem sondy, což zabije I2C4 do power-cyklu.
  Nestojí to za to u nálezu téhle závažnosti.
- **F-0026** (opt-in `s_busy` u třetího zapisovatele) — vyžaduje vytažení karty
  **uprostřed** `screenshot sd` nebo `f_getfree`; nepřímo kryté tím, že mount,
  export i remount procházejí.

🔴 **HW test odhalil nový nález — viz F-0055 níže.**

## Přehled modulů

Stav: `nezačato` → `probíhá` → `nálezy zapsány` → `opraveno` → `komentáře hotové`

| # | Modul | Soubory | Jádro | Stav | Datum | S1 | S2 | S3 | S4 | Nálezy |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | konfigurace hodin/PWR | `main.c`, `system_*.c` | CM7 | opraveno (2 otevřené) | 2026-09-09 | 0 | 2 | 5 | 0 | [7](audit/2026-09-09_hodiny-pwr.md) |
| 2 | MPU / cache / linker | `main.c MPU_Config`, `*.ld` | oba | nálezy zapsány | 2026-09-09 | 0 | 0 | 4 | 2 | [6](audit/2026-09-09_mpu-cache-linker.md) |
| 3 | IPC CM7↔CM4 (HSEM) | `ipc.c`, `ipc_shared.h`, `ipc_cm4.c` | oba | nálezy zapsány | 2026-09-09 | 0 | 0 | 4 | 0 | [4](audit/2026-09-09_ipc-cm7-cm4.md) |
| 4 | přerušení a RTOS | `stm32h7xx_it.c`, `freertos*.c` | oba | nálezy zapsány (+1 z HW) | 2026-09-11 | 2 | 1 | 0 | 1 | [3](audit/2026-09-10_preruseni-rtos.md) |
| 5 | drivery: I2C1 + I2C4 | `i2c.c`, `*_sensors.c`, `*_ui.c`, `ft5x06.c`, `ws_panel.c` | CM7 | nálezy zapsány | 2026-09-10 | 0 | 0 | 2 | 0 | [2](audit/2026-09-10_i2c.md) |
| 6 | drivery: SPI2/FPGA + QSPI/W25Q | `fpga_freq.c`, `w25q.c`, `w25q_store.c` | CM7 | opraveno (⬜ neověřeno na HW) | 2026-09-10 | 0 | 0 | 1 | 1 | [2](audit/2026-09-10_spi-qspi.md) |
| 7 | drivery: SDMMC + FatFs | `sd_export.c`, `datalog_sd.c`, `sd_diskio.c`, `sdmmc.c`, `fatfs.c`, `bsp_driver_sd.c` | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-11 | 0 | 2 | 4 | 1 | [7](audit/2026-09-10_sdmmc-fatfs.md) |
| 8 | vykreslovací řetězec | `prim_stm32_hal.c`, `libprim/*`, `libui/*` (bez fontů) | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-10 | 0 | 1 | 4 | 0 | [5](audit/2026-09-10_vykreslovaci-retezec.md) |
| 9 | hlavní obrazovka | `screens/screen_main.c`, `screen_main_data.c` | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-11 | 1 | 1 | 0 | 1 | [3](audit/2026-09-11_hlavni-obrazovka.md) |
| 10 | navigace, model fokusu, vstup, tiky | `app_gpsdo.c` (strojovna, ≈2 800 ř.) | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-11 | 0 | 1 | 3 | 2 | [6](audit/2026-09-11_navigace-fokus-vstup.md) |
| 11 | aplikační okna (`render_*`) | `app_gpsdo.c` (≈6 200 ř.) | CM7 | nálezy zapsány (+1 z provozu, opraven) | 2026-09-11 | 0 | 2 | 1 | 1 | [4](audit/2026-09-11_aplikacni-okna.md) |
| 12 | síťová vrstva: HTTP + SCPI/TCP + mDNS | `httpd_min.c` (bez SPA blobu), `scpi_tcp.c`, `lwip_app.c` (≈1 460 ř.) | **CM4** | **opraveno 6 ze 7** (1 odložený, ⬜ neověřeno na HW) | 2026-09-11 | 0 | 2 | 3 | 2 | [7](audit/2026-09-11_sit-cm4.md) |
| 13 | parsery nedůvěryhodného vstupu: SCPI + NMEA | `scpi.c` (1 472 ř.), `gps.c` (511 ř.) | **oba** (`scpi.o` je i v obrazu CM4) | **opraveno vše** (✅ část ověřena na HW) | 2026-09-12 | 0 | 1 | 6 | 2 | [9](audit/2026-09-12_parsery-scpi-gps.md) |
| 14 | UART konzole (parser příkazů) | `freertos_task_uart.c` (2 365 ř., z toho `UartTask_run` 1 966 ř.) | CM7 | **opraveny 3, 1 částečně, F-0074 → TODO #243** (⬜ neověřeno na HW) | 2026-09-12 | 0 | 0 | 3 | 2 | [5](audit/2026-09-12_uart-konzole.md) |
| 15 | webová SPA (klientský dashboard) | `httpd_min.c` = blob `SPA_HTML` (~3 060 ř., 125 funkcí JS) | **prohlížeč** (obraz CM4) | **opraveno vše** (11 z 11, ⬜ neověřeno na HW) | 2026-09-12 | 2 | 1 | 3 | 5 | [11](audit/2026-09-12_spa-web.md) |
| 16 | perzistence a záznamníky | `datalog.c`, `flightrec.c` (+ **errlog**), `syscfg.c`, `setup.c`, `calib.c` (2 456 ř. vč. hlaviček) | CM7 (kanál `errlog` i na CM4) | **skupina A opravena** (6 ze 14, ⬜ neověřeno na HW) | 2026-09-17 | 1 | 1 | 8 | 4 | [14](audit/2026-09-16_perzistence-zaznamniky.md) |
| 17 | čas, alarmy, watchdog | `rtc.c`, `alarm.c`, `watchdog.c`, `beeper.c`, `bootled.c` (~1 290 ř. + hlavičky) | CM7 | **A+B opraveno** (9 z 10, 1 odložen, ⬜ neověřeno na HW) | 2026-09-17 | 0 | 1 | 5 | 4 | [10](audit/2026-09-17_cas-alarmy-watchdog.md) |

🔑 **Modul 15 uzavřel poslední velkou neauditovanou oblast projektu.** Je jediný,
jehož kód neběží na přístroji — a právě proto se na něj nevztahuje nic z toho, čím
se hlídá firmware (překladač, `tools/audit.py`, `check_lessons.sh`). Hlídá ho vlastní
řetězec `tools/spa/check.py`, který dělá svou práci dobře (literál celý, žádná
uvozovka, DOM konzistentní) — ale **nekontroluje hranici JSON**, a přesně tam leží
oba nálezy S1: `drawTfom` čte `gps.valid` a `drawGq` čte `gps.nsat`, což jsou pole,
která `/api/state` neposílá. Obě karty jsou proto trvale nefunkční, aniž by cokoli
zakřičelo. **Návrh je udělat z toho kontrolu** (prototyp z auditu obě vady najde).
🔑 **Modulem 14 je auditovaný VEŠKERÝ kód, který zpracovává vstup zvenčí**, a všechny
tři moduly (12 síť, 13 parsery, 14 konzole) našly **tutéž třídu vady**: pevný buffer,
který se při přetečení tiše ořízne a obsah se PŘESTO zpracuje. Celkem čtyři výskyty
(F-0056, F-0058, F-0069, F-0073) — proto z toho vznikla **L-0026** a hledá se dál
jako vzor, ne jako jednotlivost.
⚠️ **Modul 13 navazuje na 12 stejnou osou** (kód zpracovávající vstup zvenčí), ale
posouvá ji dovnitř: `scpi.c` je **jeden parser pro tři transporty** (USB CDC, TCP 5025,
HTTP `/api/scpi`) a běží na **obou jádrech**, takže jedna vada v něm je vada na všech
vstupech naráz — což je přesně případ F-0064. `gps.c` přidává čtvrtý vstup: anténu.
⚠️ **Modul 12 byl první auditovaný modul na CM4** a jediná část projektu, která
zpracovává **nedůvěryhodný vstup zvenčí**. Proto nešel v pořadí podle vrstev —
byl vybrán podle rizika. ⚠️ **Blob `SPA_HTML` (2 937 ř. HTML/CSS/JS z 3 906 ř.
souboru) auditovaný NENÍ** — je to klientský kód s vlastním ověřovacím řetězcem
(`tools/spa/check.py`) a zaslouží si samostatný modul 13.

**Doporučené pořadí:** hodiny/PWR → mapa paměti/MPU/cache → IPC mezi jádry →
přerušení a RTOS → jednotlivé drivery periferií → aplikační logika.
Důvod: chyba ve spodních vrstvách se v horních projeví jako „náhodná“ nestabilita
a bez opravy základu se horní vrstvy auditují zbytečně.

## Souhrn nálezů

| Severity | Otevřené | Opravené | Zamítnuté (wontfix + důvod) |
|---|---|---|---|
| S1 | 2 | 4 | 0 |
| S2 | 1 | 15 | 0 |
| S3 | 14 | 42 | 0 |
| S4 | 6 | 22 | 0 |

⚠️ **Čísla nepiš ručně** — `python tools/audit_stav.py --kontrola` je odvodí z nálezových
dokumentů a při rozporu skončí nenulovým kódem (lekce **L-0014**). Sloupec „Otevřené“
zahrnuje i **částečně** opravené (dnes **F-0018** [S1] letový zapisovač z hooku,
**F-0028** [S3] SDMMC takt nad limitem, **F-0077** [S4] rámec `UartTask_run`).

**Modul 16 — nálezy zapsány 2026-09-16 (F3; fáze oprav NEproběhla):**
Verdikt **podmíněně funkční**. 14 nálezů (1×S1, 1×S2, 8×S3, 4×S4), dokument
[audit/2026-09-16_perzistence-zaznamniky.md](audit/2026-09-16_perzistence-zaznamniky.md).
- **F-0089** [S1] `syscfg_load()` obnovuje `datalog_en`/`datalog_store`/
  `datalog_period_s` **jen při studeném startu** — ty tři řádky skončily pod
  `if (g_syscfg_bkp_valid) return;` (`syscfg.c:266`), přestože v BKP nejsou
  (doloženo výčtem DR1/DR2/DR6 z `rtc.c:89-120`). Po každém teplém resetu se
  vrátí výchozí `ON`/`AUTO`/`10 s`. Reprodukce z konzole: `datalog off` →
  2 s → Menu→Restart → `status` ukáže zase `ON`.
- **F-0090** [S2] `datalog_init()` nuluje `s_be` **před** `s_ready`
  (`datalog.c:374`), zatímco čtyři čtenáři (`get_status`, `read_back`,
  `read_bulk`, `format_status`) dereferencují `s_be` guardované jen `s_ready`
  a QSPI mutex **neberou vůbec** — komentář `:371-372` přitom tvrdí, že je
  mutex chrání. Reachability je těsná: tlačítko na přepnutí úložiště je
  v okně Datalog a totéž okno volá `datalog_get_status()` v každém tiku.
- **F-0091** [S3] `flightrec_init()` při plném regionu maže natvrdo sektor 0
  místo sektoru za nejnovějším → **od 66. dumpu si log ničí vlastní nejnovější
  záznam při každém bootu**; `status` pak hlásí „je uložený záznam" a
  `flightrec` říká „žádný". Sesterský `errlog_init()` v témže souboru
  (`:473-477`) to dělá správně.
- **F-0092** [S3] `ERRLOG_TAG_LEN` = 6 utne `g_crash_text` přesně na dvojtečce
  (`"stall:"`, `"stack:"`) a `errlog_fmt_detail` u `K_CRASH` tag netiskne vůbec →
  okno CHYBY i web ukážou u přetečení zásobníku jen `CFSR=0x00000000
  BFAR=0x00000000`. **Ve spojení s otevřeným F-0018** jsou pro scénář #18 oba
  trvalé záznamy slepé.
- **F-0093** [S3] obnova uloženého nastavení při bootu zapíše do `errlog`
  falešnou „změnu nastavení uživatelem" (`datalog_set_period_s/_store` logují
  bezpodmínečně). 🔴 **Opravit spolu s F-0089** — jinak se to po přesunu řádků
  rozšíří ze studeného startu na každý reset.
- **F-0094** [S3] `calib_save()` zapíše errlog záznam jako **první příkaz**
  funkce, tedy před kontrolou `s_store.ready` i před zápisem.
- **F-0095** [S3] `errlog_read_batch()` čte záznam po záznamu (jeden QSPI příkaz
  na 32 B) — vzor, který `datalog.h:211-213` popisuje jako 25× režii a kvůli
  kterému vznikl `datalog_read_bulk` (F-0039/L-0021). Mez 64 v
  `ipc_errlog_service` dává ~11 ms spinu v defaultTasku pod mutexem.
- **F-0096** [S3] `setup_load()` je **třetí** cesta, která píše `g_meas_cfg` bez
  kritické sekce, a `slot_sanitize()` neověřuje `lo <= hi`. ⚠️ **Opravit jedním
  zásahem s otevřeným F-0052**, ne zvlášť.
- **F-0097** [S3] strop 4080 B na blob (`W25Q_STORE_MAX_BLOB`) není nikde
  vynucený `_Static_assert`em — přetečení by znamenalo, že se nastavení přestane
  ukládat a `syscfg_flash_tick` to bude 100×/s zkoušet bez jakéhokoli hlášení.
  Přímá **L-0026**; vzor už v projektu je (`httpd_min.c:84-85`).
- **F-0098** [S3] neúspěšná inicializace úložiště je trvalá a tichá (pět míst).
- **F-0099**…**F-0102** [S4] ignorované návraty v `errlog_erase()`; UART
  `errlog dump` obchází `errlog_fmt_detail` (**L-0049** nedodržena třetím
  konzumentem); `errlog_count()` po přetočení nadhodnocuje; `datalog_tick`
  dohání zameškané vzorky.

**Modul 9 — opraveno 2026-09-11 (⬜ neověřeno na HW):**
- **F-0037** [S1] frakční odchylka `y` počítaná pevným měřítkem `1e-14` → nový **jediný
  zdroj pravdy** `screen_main_frac_dev(double hz)`, používají ho `stats_sample()`
  i `stats_seed_tick()` v `app_gpsdo.c`. Lekce **L-0018**.
  ⚠️ **Účinek na desce zatím ověřit NELZE** — blokuje to F-0039 (viz níže):
  σy@1s zůstává 0, dokud běží rekonstrukce z datalogu.
- **F-0038** [S4] `rtc_time_date()` četl RTC bez ochrany proti přehoupnutí půlnoci →
  dvojí čtení se shodou (3 pokusy), stejný vzor jako `get_fattime()` ve `fatfs.c`.
- Navíc přidán řádek `STATISTIKA: sigma_y@1s` do `status` — bez něj nešla oprava F-0037
  na desce ověřit (σy byla čitelná jen z displeje).

**Modul 9 — nově otevřené (nalezeno AŽ ve fázi oprav, jako F-0036 u modulu 8):**
- **F-0039** [S2] ✅ **opraveno 2026-09-11** (⬜ neověřeno na HW), lekce **L-0021**.
  Rekonstrukce ADEV blokovala živé vzorkování ~1 h 47 min po každém bootu, protože
  rozpočet dávky byl spočítaný proti tiku 20 Hz, zatímco volající běží 1 Hz.
  🔑 Při opravě se ukázalo, že `datalog.h:203` u `datalog_read_back` **přímo varuje**
  „na průchod více záznamy použij `datalog_read_bulk`" — **včetně naměřených čísel**
  (~173 µs režie/záznam vs ~7 µs na data) — a bulk cesta už existovala a byla
  prověřená (používá ji web přes IPC). Žádná ze tří variant v nálezu ji neobsahovala,
  protože jsem hlavičku citované funkce nepřečetl.
  Zvolena **varianta „bulk + brzký konec"**: 8 dávek po 64 záznamech za tik ≈ 5 ms/tik
  → 512 zázn./s → **~4 min** místo 1 h 47 min, **kadence zůstává 1 Hz** (žádná nová
  expozice watchdogu). Navíc sonda: když nejnovější dávka nemá ani jedno použitelné
  měření, rekonstrukce se **vůbec nespustí** — což je dnešní stav (SPI link nenaběhl),
  takže dnes je blokování **nulové**. A řádek `ADEV rekonstrukce:` v `status`, bez
  kterého byla doba běhu neviditelná.
  ⚠️ **Tím se odblokovalo ověření F-0037 na desce** — σy@1s už nebude po bootu držená
  na nule.

**Modul 4 — nový nález z HW 2026-09-11:**
- **F-0055** [S1] **`selftest` z UART přeteče zásobník UartTasku a IWDG shodí desku.**
  Reprodukováno 2×; po restartu `Reset: WATCHDOG!  stack:UartTask`. UartTask má 4096 B
  a jen **976 B** volna (high-water), do kterých se musí vejít celý řetěz
  `run_selftests()` — přitom `gps_selftest` má rámec 468 B, `mp_selftest` 428 B,
  `scpi_selftest` 264 B → `scpi_exec_one` 260 B.
  🔴 Audit zásobníků ve STATUS #146 měřil UartTask na 33 % volna a uzavřel „ostatní
  tasky mají dost" — jenže **měřil běžný provoz**, ne stav se selftestem.
  ⚠️ `CLAUDE.md` přitom `selftest` vede jako nástroj č. 4 „nejdřív měř" s cenou
  **zdarma**, takže metodika doporučuje příkaz, který shodí desku.
  ✅ **Není to regrese z dnešních oprav** — obě verze `freertos_task_uart.c` přeloženy
  týmiž flagy dávají **shodný rámec 700 B**, a všechny těžké rámce leží v souborech,
  kterých se opravy nedotkly. **Potřebuje rozhodnutí** (3 varianty v nálezu).

**Modul 7 — skupina A opravena 2026-09-11 (⬜ neověřeno na HW):**
- **F-0025** [S2] tik volá `sd_export_unmount()` místo holého `f_mount(NULL,…)` → po
  vytažení a vložení karty se SD zase namountuje (dřív 30 s čekání a trvalý stav ERROR).
- **F-0026** [S2] `sd_export_busy_begin/end()` vystaveno v hlavičce; používá ho
  `screenshot_save_sd()` (osm chybových návratů → **obalka nad vyčleněným tělem**)
  i `ui_refresh_capacity()`. **Minimální varianta**, ne přesun vlastnictví unmountu —
  nález sám tu druhou označuje za střední riziko. Lekce **L-0022**.
- **F-0027** [S3] `gpio_cfg_lock()` kolem obou `HAL_GPIO_Init(GPIOC,…)` v SD cestě
  (v `sdmmc.c` přes USER CODE bloky → regen-safe).
  ⚠️ **PC8–PC12 se do `GG_PINS` ZÁMĚRNĚ nedoplnily** — hlídač by SD piny opravoval
  i v době, kdy je karta odmountovaná a mají být jinak. Vědomé rozhodnutí.
- **F-0029** [S3] `export_body()` kontroluje `f_close()` **i hlavičkový `f_write()`**.
- **F-0030** [S3] debounce detekce karty: dotaz oddělen od aktualizace
  (`datalog_sd_det_tick()` volá **jediná** úloha). Lekce **L-0023**.
- **Ověření:** build 0 varování, `audit.py` 92 OK/0/2, `.text` 599 176 → **599 272 B**
  (+96); v obrazu `datalog_sd_det_tick` (68 B), `sd_export_busy_begin/end`,
  `gpio_cfg_lock` nově volán z `HAL_SD_MspInit` i `sd_dat_pullup_enable`,
  `screenshot_save_sd` obepíná tělo dvojicí busy.
🔶 **F-0028 je ČÁSTEČNĚ opravený — takt je `SD_CLKDIV = 1` → 32 MHz, přesně jako `.ioc`
(`SDMMC1.ClockDiv=1`), a přepínání do High Speed je ODSTRANĚNÉ** (2026-09-11, po HW testu
a dvou zpřesněních od uživatele: *„karta dříve běžela spolehlivě na 32 MHz"*).
HW test téhož dne ukázal, že **CMD6 na této kartě neprojde**, takže celý HS aparát by za
cenu vendor volání s ~49denními smyčkami nepřinesl nic — odstraněním vypadl z obrazu
i ten vendor kód (`nm` už `HAL_SD_ConfigSpeedBusOperation` ani `SD_SwitchSpeed` nenajde,
`.text` −592 B), takže **zbytkové riziko zatuhnutí je pryč úplně**.
Sběrnice jede **~28 % nad limitem Default Speed**; opora je empirická (dlouhodobě
spolehlivý provoz po HW úpravě), ne odvozená ze specifikace.
🔑 **Co z opravy zůstává a je to to podstatné: stav přestal být tichý.** `sd diag` hlásí
takt, režim, platný limit **a značku `<-- NAD LIMITEM`**. Nález F-0028 vznikl právě
proto, že se o provozu mimo specifikaci nikde nic nedozvíš — a to opravené je.
⚠️ Až karta začne hlásit `DATA_CRC_FAIL` nebo přerušovaně poškozený export, **začni
řádkem `sbernice`**, ne datovou cestou. Lekce **L-0024** přepsána, aby netvrdila, že
kód padá na bezpečnou hodnotu (netvrdí — tvrdí, že ten stav je vidět).

✅ **F-0028 a F-0031 dobrány 2026-09-11** (rozhodnutí uživatele: doplnit High Speed):
- **F-0028** [S3] identifikace i CMD6 běží na **16 MHz** (`SD_CLKDIV_DS`, v mezích DS)
  a na **32 MHz** (`SD_CLKDIV_HS`) se jde **až po úspěšném** `HAL_SD_ConfigSpeedBusOperation`.
  🔴 Při opravě se zjistilo, že ta vendor funkce obsahuje **tutéž ~49denní smyčku**
  (`SDMMC_SWDATATIMEOUT`), kvůli které se obchází `HAL_SD_Init` — commit `ec64939`,
  a komentář v `BSP_SD_Init` říká „**Přesně to se stalo**" (IWDG shodil desku).
  Ošetřeno čtyřmi věcmi: pojistka na taktu (ověřeno v disassembly — `cmp/bne`
  přeskočí zápis `ClockDiv=1`, takže selhání = 16 MHz), **ohraničené čekání na
  TRANSFER před** vendor voláním (sdílené s `BSP_SD_Init`), HW DTIMER na datové fázi,
  a `sd_blocking_begin()` → priorita pod UiTaskem (heartbeat běží → žádný IWDG).
  ⚠️ **Zbytkové riziko zůstává**: karta, která na CMD6 odpoví a pak nedojde do
  TRANSFER, nechá vendor smyčku točit — zvenčí se to ohraničit nedá. Nejhorší
  následek je zatuhlá konzole, ne restart. Lekce **L-0024**.
- **F-0031** [S4] všech pět zastaralých míst; u taktu, `[a2]` výpisu i `CLAUDE.md` se
  hodnota nově **odvozuje**, ne opisuje. `sd diag` hlásí takt **i režim i limit**.
- **Ověření A+B:** build 0 varování, `audit.py` 92 OK/0/2, `.text` 599 272 → **599 968 B**
  (+696; přibyl `HAL_SD_ConfigSpeedBusOperation` 192 B + `SD_SwitchSpeed` 248 B).

**Modul 11 — nálezy zapsány 2026-09-11 (F3; fáze oprav NEproběhla):**
Verdikt **podmíněně funkční**. Okna se chovají jako kreslicí kód — přesně jak modul 10
předpovídal. ⚠️ **Nebyl to přezkum řádek po řádku** (≈6 200 ř. na jedno sezení nejde);
proběhl **rizikově cílený průchod** podle tříd chyb, které projekt už prokazatelně vyrobil.
Rozsah i to, co se nehledalo, vymezuje sekce „Rozsah a metoda" v nálezovém dokumentu.
- **F-0052** [S2] okno MATH přepisuje `g_meas_cfg` (pět polí typu `double`) **bez kritické
  sekce**, zatímco `scpi.c:953-955` i `ipc.c:539-543` kolem téhož globálu kritickou sekci
  mají — a `ipc.c:526` dokonce v komentáři **výslovně počítá se souběžnými zápisy z UI**.
  🔴 Rozhodující je, že **tatáž funkce `meas_math_capture_null()` má tři volající a dva
  z nich pracují nad lokální kopií**; jen `app_gpsdo.c:8639` píše přímo do globálu.
  V obrazu jsou `lo`/`hi` dvě samostatné instrukce `vstr`. Nejmenší pásmo je 0,001 Hz,
  takže inverze `lo > hi` (→ falešný FAIL → 4 pípnutí) je dosažitelná. `L-0012`.
- **F-0053** [S3] `fmt_fixed()` zná mez 1–3 desetiny, ale `default:` tiše zahodí desetinnou
  část. Dnes na to nikdo nešlape (ověřeno), je to past pro příští volání — a **už jednou
  kousla** (σ hlásila vždy „0 Hz", STATUS #132). `L-0015` + `L-0017`.
- **F-0054** [S4] kontrola, kterou `CLAUDE.md` na tu past předepisuje, **nemůže být nikdy
  zelená**: grep matchuje i komentáře varující před pastí a čte i `CM7/Debug/*.list`.
  6 shod ve zdravém stromě → skutečné volání se v nich utopí. `L-0011` + `L-0020`.

**Modul 10 — opraveno vše 2026-09-11 (⬜ neověřeno na HW):**
Verdikt **podmíněně funkční**. Nic tu neshodí přístroj: modul nemá jediné volání `HAL_*`,
žádný DMA buffer (vše v AXI SRAM, ověřeno `nm`) ani čekací smyčku bez meze. Vada je
soustředěná do **účetnictví fokusu a do diagnostiky** a má jednoho jmenovatele — `s_view` je
rozvětvené **pěti** nezávislými tabulkami (46 `case` + 32 větví + 10 + 10 + 57 testů).
- **F-0046** [S2] `btnreg_sync_focus()` ukládá do `s_focus` **surový index do registru
  tlačítek**, zatímco zbytek modelu čte spojený prostor `ln + index` (`enc_paint` dělá
  `idx -= ln`). V pěti oknech se seznamem (MENU, MĚŘENÍ, NÁSTROJE, FUNKCE, NÁPOVĚDA) tedy
  ukazuje na jiný prvek — a přes `focus_store()` se ta špatná hodnota **trvale uloží**.
  🔴 Komentář nad tou funkcí přitom říká, že existuje proto, aby se obě ovládací cesty
  nerozešly (`L-0008`).
- **F-0047** [S3] `g_ui_view`/`g_ui_view_changes` (diagnostika „otevřelo se okno?") nepokrývá
  **17 ze ~45 oken** — včetně **hlavní obrazovky, MENU, MĚŘENÍ, NÁSTROJŮ, FUNKCÍ a NÁPOVĚDY** —
  protože ta se kreslí přes `window_prep()`, ne `window_first()`. A protože si drží předchozí
  hodnotu, **aktivně lže**: uživatel otevře MENU, `status` ukáže staré okno → vypadá to jako
  nepřijatý dotyk, tedy přesně ten mylný závěr, kterému měla zabránit (`L-0011`).
- **F-0048** [S3] `nav_push()` při plném zásobníku (6 míst, nejhlubší dnešní cesta 5) položku
  **tiše zahodí** a `nav_back()` pak vede jinam. Registr tlačítek v témže souboru to dělá
  správně (`s_btnreg_ovf` → `status`); tady chybí (`L-0017`).
- **F-0049** [S3] `render_view()` nezná okna 49 (FUNKCE), 50 (NÁPOVĚDA) a 13 (modal) → po
  obnově I2C4 (`app_gpsdo_touch_dead`) vyhodí uživatele na hlavní obrazovku. Přímý důsledek
  F-0050. ✅ Ověřeno, že `nav_back()` je v pořádku: všech 13 `nav_push` cílů `case` má.
- **F-0050** [S4] pět dispatch tabulek nad `s_view`. **Kandidát na odložení** — plošný refaktor
  je dražší než vada a `render_view` vs. screensaver se liší z doloženého důvodu; levnější je
  kontrola (odvozená tabulka + `_Static_assert`, nebo skript do `check_lessons.sh`).
- **F-0051** [S4] paměť fokusu per okno (zadání UI §7) se přeskočí, když se uživatel do okna
  vrátí **bez použití encoderu** — `focus_load()` má jediné volání, schované za
  `s_shown_view != s_view`, a `s_shown_view` nuluje jen obsluha encoderu.
⚠️ **F-0046, F-0047 a F-0051 sahají na totéž účetnictví** („kde jsem" + „kde je fokus") a mají
společnou opravu (jednotné `view_set()`); opravovat je odděleně by se pletlo.

✅ **Skupina B opravena 2026-09-11 jedním zásahem** (F-0046 + F-0047 + F-0051), lekce **L-0019**:
- `view_set(uint8_t)` = **jediné místo, kde se mění `s_view`**; nese diagnostiku okna i paměť
  fokusu. Všech **53** přiřazení `s_view = N;` jde tudy, `window_first()` diagnostiku už neplní.
- `cur_list()` = **jediný zdroj** mapování `s_view → seznam`; díky němu má `btnreg_sync_focus()`
  konečně `ln` a počítá ve spojeném prostoru (`ln + i`).
- `s_focus_shown` (dřív funkční statik `s_shown_view`) je file-scope a nuluje ho `view_set()`,
  takže paměť fokusu funguje i při navigaci prstem.
- ⚠️ **Sentinely `s_view = 0xFF` / `-1` zůstaly záměrně mimo** — nejsou to přechody na jiné okno.
- Ověřeno: build 0 varování, `audit.py` 92 OK/0/2 (gcc 14.3.1), `.text` 598 248 → **598 448 B**
  (+200), v obrazu `view_set` 76 B s **53 volajícími** a v `app_gpsdo_handle_touch`
  inlinovaný `bl enc_items_n` → **`add r0, r2`** (= `ln + i`). Detekce v `zakazane_vzory.txt`.
✅ **Skupina A opravena 2026-09-11:**
- **F-0048** — `s_nav_peak` + `s_nav_ovf` + řádek `UI: navigace (ZPET) max N/6` v `status`
  (tentýž vzor, jaký v témže souboru už měl registr tlačítek); `NAV_DEPTH` ze `sizeof`.
  ⚠️ **Pole zůstává na 6 záměrně** — nález sám říká, že zvětšení není oprava; teď je
  rezerva měřitelná, takže bump na 8 je následný krok, až `max` doleze na 6.
- **F-0049** — doplněn `case 49` a `case 50`. 🔑 U okna **13 (modal restartu) padlo
  rozhodnutí: neobnovovat** a vést do MENU — tlačítko NE už tam vede taky, takže obě cesty
  zrušení dialogu končí stejně, a potvrzení destruktivní akce se nemá samo vynořit.
  Okna 8 a 11 zůstávají mimo dispatch záměrně a je to u `default:` napsané.

✅ **Skupina C (F-0050) vyřešena KONTROLOU, ne sjednocením** — lekce **L-0020**:
`scripts/check_lessons.sh` nově hlásí okno s `view_set(N)` bez `case N:` v `render_view()`
i okno živě tikané, které `render_view` nezná. **Pět tabulek v kódu zůstává** (plošný
refaktor sahá na každé okno v kódu, který funguje); odstraněna je tichost, ne duplicita.
🔑 Provedena **pozitivní kontrola**: nad kopií bez `case 49`/`case 26` obě větve zazněly.
⚠️ První verze kontroly se kotvila na dopřednou deklaraci `render_view` a „nenašla" nic —
přesně proto se pozitivní kontrola dělá (táž past jako `-fanalyzer` + `-fsyntax-only`).

**Ověření skupin A+C:** build 0 varování, `audit.py` 92 OK/0/2, `.text` 598 448 → **598 656 B**
(+208), v obrazu `app_gpsdo_nav_stats` (32 B), oba nové řetězce a skoky `render_view` →
`app_gpsdo_render_func` / `_help` / `_menu`.
⬜ **Modul 10 celý čeká na flash + POWER-CYKLUS.**

**Modul 6 — opraveno 2026-09-10 (⬜ neověřeno na HW):**
- **F-0023** [S3] `w25q.c` nekontroloval adresu proti kapacitě čipu → `range_ok()` na začátku
  `w25q_read` / `w25q_write` / `w25q_erase_sector`; bez součtu `addr + len` (přetečení).
  Commit `1f69ca9`, lekce **L-0015**.
- **F-0024** [S4] ignorované návraty SW resetu v `w25q_init` → **zdůvodněno v kódu**, ne
  vyhodnoceno: `cmd_only` selhává jen na straně hosta a výsledek resetu už hlídá
  následující kontrola JEDEC ID. Commit `d7dbd69` (`docs:`, chování se nemění).

**Modul 1 — opraveno:** F-0001 (`pwrclk_check()` v USER CODE ověřuje dosažený stav napájení
a hodin, výstup do `status`), F-0002 (NMI zapisuje výpadek HSE do black-boxu, kind 7),
F-0004 (jen dokumentace: I2C je ~50 kHz, ne ~100), F-0005 (pojistka v `check_lessons.sh`).

**Modul 1 — otevřené:**
- **F-0003** [S3] čekání na `VOSRDY` bez timeoutu — **vědomě odloženo.** Leží v generovaném
  `SystemClock_Config()` (regen by opravu smazal) a špatně zvolená mez by pustila 480 MHz
  dřív, než se regulátor ustálí, tedy riziko rozbít fungující desku. Vrátit se k tomu,
  pokud se objeví „deska občas nenaběhne“.
- ~~**F-0006** [S3] `WRHIGHFREQ`~~ — ✅ **UZAVŘENO 2026-09-10**: `status` hlásí `WRHIGHFREQ=3`,
  tedy nenulovou (ne reset default). Flash má zpoždění naprogramované, nález padá.
- **F-0007** [S3] ztráta HSE = mrtvý přístroj bez rozlišitelné diagnózy — čeká na rozhodnutí
  o politice (zůstat mrtvý, ale rozlišitelně / nabootovat na HSI a měření označit za neplatné).
- ~~S4 (z opravy F-0002): řádek `HSE: CSS…` ve `status`~~ → převzato modulem 4 jako **F-0019**.

**Stav ověření: ✅ OVĚŘENO NA HW po power-cyklu (2026-09-10).**
`status` po studeném startu hlásí `NAPAJENI/HODINY: OK SYSCLK 480 MHz HCLK 240 MHz WRHIGHFREQ=3`
→ **F-0001 i F-0006 uzavřeny** (kontrola napájení/hodin funguje; `WRHIGHFREQ=3` je nenulová,
tedy ne reset default — Flash má naprogramované zpoždění a nález F-0006 tím padá).
`SDRAM refresh: SDRTR=371, ve zdrojaku 371` a `SDRAM cteni: rpipe=1 HCLK | I/O kompenzace READY (CSI ok)`.

✅ **Problikávání displeje (#237/#238/#72) VYŘEŠENO 2026-09-10 — příčina byla ČTECÍ CESTA FMC.**
`ReadPipeDelay = 0` + nikdy nezapnutá I/O kompenzační cela. Naměřeno po opravě:
`membench` **0 chybných bitů** (bylo 3 338 207), retence **0** (bylo 496 068), překryv adres
zmizel, `LTDC podtečení` **0/1000** (bylo 1217/1000), displej po power-cyklu v pořádku.
⚠️ Paměť ani `FMC_A9`/`PF15` vinné nebyly — nová položka v tabulce „HW obviněn a byl nevinný“.
⚠️ Poučení z cesty k tomu je **L-0011** (převzal jsem hypotézu, kterou nabídl nástroj,
místo abych přečetl jeho čísla) — stálo to jeden flash cyklus a jednu vrácenou změnu.

## Log sezení

| Datum | Modul | Co se udělalo | Nové lekce |
|---|---|---|---|
| RRRR-MM-DD | — | inicializace kitu | — |
| 2026-09-09 | hodiny/PWR | F3 přezkum, 7 nálezů (2×S2, 5×S3). Přepočítán celý hodinový strom vč. odvozených frekvencí konzumentů (FMC/SDCLK, LTDC, ADC, SPI123, SDMMC, timery) — sedí až na I2C. Kód neměněn. | zatím žádná (lekce se zapisují až po opravě, F5) |
| 2026-09-09 | hodiny/PWR | F5 opravy: 4 nálezy uzavřeny v 5 commitech (2× `docs:`, 2× `fix:`, 1× `docs:` na komentář). ⚠️ Obě opravy firmwaru jsou zatím jen **přeložené** (build 0 varování, `audit.py` v baseline, `.text` 594 504 → 595 368) — **na HW po power-cyklu NEOVĚŘENO**, viz L-0010. F-0003 vědomě odloženo. | L-0006 … L-0009 |
| 2026-09-09 | hodiny/PWR | Reakce na hlášení „po power-resetu se rozbije displej“: doloženo, že opravy do hodin **nezapisují**, a symptom dohledán jako otevřené #141/#237/#238. `pwrclk_check()` přesto přesunuta až za bring-up displeje. Doplněn power-cyklus do ověřovacího řetězce. | L-0010 |
| 2026-09-09 | MPU/cache/linker | F3 přezkum, 6 nálezů (4×S3, 2×S4), verdikt **funkční**. Mapa 32 MB SDRAM, 4 MPU oblasti a umístění objektů ověřeny proti obrazu (`nm`), ne proti zdrojáku. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-09 | IPC CM7↔CM4 | F3 přezkum, 4 nálezy (4×S3), verdikt **funkční**. Seqlock, SPSC ringy i čtenář na CM4 přečteny řádek po řádku — v jádru protokolu chyba není; nálezy jsou invarianty držené jen komentářem. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | hodiny/PWR + SDRAM | ✅ **HW ověření po power-cyklu.** F-0001 a F-0006 uzavřeny ze `status`. Problikávání displeje vyřešeno: příčina byla čtecí cesta FMC (`rpipe=0` + vypnutá I/O kompenzace), ne obnova a ne vadná paměť — `membench` 0 chybných bitů, LTDC podtečení 0/1000. Uzavřeno STATUS #237/#238/#72. | L-0011 |
| 2026-09-10 | přerušení a RTOS | F3 přezkum, 3 nálezy (1×S1, 1×S2, 1×S4), verdikt **podmíněně funkční**. Priority ISR, grouping, timebase i hooky v pořádku; stacky změřeny z běžícího přístroje (`stats`). Obě funkční vady jsou diagnostika, která selže právě při poruše. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | drivery I2C | F3 přezkum, 2 nálezy (2×S3), verdikt **funkční**. Modul s nejdelší historií incidentů je dnes dobře ošetřený; oba nálezy jsou o tom, že se dodržené pravidlo neuplatnilo všude. **Navíc doplněn `docs/ARCHITECTURE.md`** z auditů 1–5 → F1 uzavřena. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | F5 opravy (moduly 2–5) | Opraveno 10 nálezů ve 3 commitech: F-0008, F-0011, F-0015 (pojistky), F-0018 částečně, F-0019, F-0020, F-0021, F-0022 (tiché vady zviditelněny), F-0009, F-0010, F-0012 (dokumentace). Build 0 varování, audit.py baseline, `.text` 597 232 → 597 488. **⬜ neověřeno na HW.** Rozšířen `audit-modul` o fázi F5. | L-0012, L-0013 |
| 2026-09-10 | drivery SPI2/FPGA + QSPI | F3 přezkum, 2 nálezy (1×S3, 1×S4), verdikt **funkční**. Oba drivery jsou blokující (grep na DMA/IT prázdný → sekce C odpadá). Klíčové zjištění: **`NOLINK` není přičitatelný ovladači na CM7** — CS boot level, AFCNTR, časování i CRC gate jsou v pořádku. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-11 | hlavní obrazovka | F3 přezkum, 2 nálezy (1×S1, 1×S4), verdikt **podmíněně funkční**. Vykreslovací část je v dobrém stavu — `gate_same` je nasazený podle vlastního pravidla (na každý tik, vlastní `reps` na kartu) a každý partial redraw začíná neprůhledným clearem. Vada je v **metrologii**: **F-0037 [S1]** — frakční odchylka `y` se počítá pevným měřítkem `1e-14`, které platí jen pro `frac=7` a `f0=10 MHz`; obojí je dynamické. Táž veličina se přitom v `app_gpsdo.c:7899` počítá SPRÁVNĚ a sype se do TÉŽE ADEV pyramidy. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | F5 opravy (modul 8, 2. dávka) | **F-0036** opraven variantou „mez v ms + zrušení přenosu + čítač nejdelšího čekání“ (commit `d3a099e`). 🔑 Ten čítač hned vyvrátil můj vlastní odhad: nejdelší legitimní čekání je **~61 ms**, ne ~12 ms — byl jsem 5× vedle a první verze s mezí 100 ms by byla HORŠÍ než původní stav (nově se přenos při vypršení ruší). Po změření mez 500 ms, rezerva ~8×. Na desce `chyb 0, timeout 0, max cekani 56,3 ms`. **F-0032** uzavřen dokumentací (`CLAUDE.md` + hlavička `prim_stm32_hal.c`) — oprava kódem by zhoršila F-0036, viz odůvodnění v nálezu. Modul 8 tím nemá otevřený nález. | rozšíření L-0016 |
| 2026-09-10 | F5 opravy (modul 8) | Skupina A: **F-0033** (chyby DMA2D se přestaly mazat naslepo + 3 počítadla a řádek `DMA2D:` ve `status`), **F-0034** (počítadlo přeskočených glyfů + řádek `FONTY:`), **F-0035** (`__DSB()` před startem DMA2D, ověřeno v disassembly). Build 0 varování, `audit.py` 92/0/2, `.text` 597 520 → 597 832. 🔑 **Oprava F-0033 hned odhalila nový nález F-0036** [S2]: hlídací mez v `d2d_wait()` vyprší i na legitimním celoobrazovkovém přenosu (14 vypršení za 25 s, roste s kreslením) a DMA2D se pak přeprogramuje za běhu. **F-0032 a F-0036 zůstávají otevřené.** ⬜ neověřeno na HW po power-cyklu. | L-0016, L-0017 |
| 2026-09-10 | vykreslovací řetězec | F3 přezkum, 4 nálezy (4×S3), verdikt **funkční**. **Modul 8 nejdřív rozdělen** (14 375 ř. → 8/9/10, viz poznámka nahoře). Nic dnes nekreslí špatně; všechny nálezy jsou latentní pasti a chybějící diagnostika. Nejzávažnější F-0032: pravidlo „partial redraw musí začít clear“ platí jen pro neprůhledné barvy, `sw_fill` obchází `mark_dirty`. Ověřeno rozborem indexů, že copy-forward nikdy nepíše do scanovaného bufferu, a že `keep[96]` v dedupu sedí přesně na mez. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | drivery SDMMC + FatFs | F3 přezkum, 7 nálezů (2×S2, 4×S3, 1×S4), verdikt **podmíněně funkční**. Blokující CPU/FIFO cesta místo IDMA je doložitelně správné rozhodnutí; ručně skládaný init obchází dvě vendor smyčky s timeoutem ~49 dní. Slabiny jsou v životním cyklu okolo mountu: **výměna karty za běhu je rozbitá deterministicky** (F-0025) a auto-unmount z defaultTasku umí smazat FatFs semafor drženy jiným taskem (F-0026). Umístění všech bufferů ověřeno `nm` nad `.elf`. Dvě falešné stopy prověřeny a zavrženy (BusFault přes `disk_status`, L-0007 přes `HAL_RCCEx_PeriphCLKConfig`). Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | F5 opravy (modul 6) | F-0023 mez proti kapacitě W25Q (`fix:` `1f69ca9`), F-0024 zdůvodnění ignorovaných návratů (`docs:` `d7dbd69`). Před zásahem ověřeno, že žádný volající na hranici neleží. Build 0 varování, `audit.py` 92/0/2, `.text` 597 488 → 597 520 a mez `cmp.w r0, #67108864` dohledána v disassembly. **Přeložen i CM4/Release** — obraz byl starší než `ipc_shared.h` (assert z F-0017), takže `build.sh` varoval na možný nesoulad bank. **⬜ neověřeno na HW.** | L-0015 |
| 2026-09-11 | **sit CM4 (HTTP + SCPI/TCP + mDNS)** | F3 prezkum, 7 nalezu (2xS2, 3xS3, 2xS4), verdikt **podminene funkcni**. **Prvni auditovany modul na CM4** a jedina cast projektu zpracovavajici neduveryhodny vstup zvenci. Parsery samotne jsou v poradku — zadne preteceni vstupniho bufferu jsem nenasel (`httpd_parse_request`, `b64_decode`, `mdns_match_name` i `scpi_tcp` kontroluji meze pred kazdym zapisem). Vady lezi ve **vystupni** ceste a ve **sprave zivotniho cyklu spojeni**: **F-0056** (odpoved `/api/log` se od 100 MHz nevejde do `bodybuf` a tise se orizne — strop 48 bodu je z v12 a v13 pak pridalo dve dalsi cisla na bod, aniz by se prepocital; projevi se to hlaskou obvinujici datalog a IPC, ktere jsou nevinne) a **F-0057** (`pump_send` zavre pcb, ale neodregistruje `tcp_arg`/`tcp_err`/`tcp_poll`, takze cizi callback sahne na mezitim znovupouzity slot — doloženo ctenim vendorovaneho lwIP `tcp.c:484` a `tcp_priv.h:223`). Umisteni vsech bufferu overeno `nm` nad obrazem CM4. 🔑 Hypoteza „zasobnik jako F-0055, jen na CM4" **merenim vyvracena**: dostupnych 54 168 B proti nejhlubsimu retezu ~2–3 kB (rezerva ~18x). Kod nemenen. | zatim zadna (F5 nebyla) |
| 2026-09-11 | F5 opravy (modul 12) | **6 ze 7 nalezu uzavreno ve 3 commitech.** `d508139` zivotni cyklus spojeni (**F-0057** odregistrace callbacku pred uvolnenim slotu + test `c->pcb == pcb`; **F-0059** SSE dostalo timeout 120 s **a** vyhodnoceni `tcp_write`, ktere komentar uz tri mesice sliboval; **F-0062** mrtva vetev odpovida misto mlceni). `d2038cc` vystupni buffery (**F-0056** rozpocet `_Static_assert`em + `bodybuf` 4096->6144 B + konec ticheho orezu; **F-0058** `hdr_set()`). `3df104c` **F-0060** — `expected_len` pryc z `/api/state` i ze SPA. 🔑 **F-0056 se opravil JINAK, nez nalez navrhoval:** snizeni stropu na 40 bodu by uskodilo, protoze SPA sesiva dalsi davku jen kdyz dostane presne tolik bodu, kolik si vyzadala — rozpocet se musel ZVEDNOUT, ne oriznout. Build 0 varovani, `audit.py` 92/0/2, `.text` 76336 -> 76768 B, `.bss` +10240 B (vedome), SPA retezec `check.py --build` cely zeleny (krok 8: `nm` 139290 = extrakce +1). **F-0061** (mDNS konformita) vedome odlozen. ⬜ **neovereno na HW.** | L-0025, L-0026, L-0027, L-0028 |
| 2026-09-11 | oprava z provozu (modul 11) | **F-0063 [S2]** — uzivatel nahlasil, ze dlazdice „Chyby (log)" nic nedela, „jen slysim klik". Ten klik byl **dukaz**: UiTask ho prehraje (`alarm_click()`) prave kdyz `handle_touch` vrati true, takze vstupni cesta byla vyloucena hned a slo jit rovnou na vykresleni. `app_gpsdo_render_errlog()` neflipovalo — okno se kreslilo do zadniho bufferu a nikdy se neukazalo, prestoze `s_view` uz bylo 51. 🔑 Neresilo se jen hlasene misto: vyctem OBOU dlazdicovych tabulek doloženo, ze ze **22 oken** bylo `render_errlog` JEDINE bez vlastniho flipu. Opraveno `1b21c82` (+8 B `.text`), doplnena trvala kontrola do `check_lessons.sh` a **overena pozitivni kontrolou na tri funkcich** (pricemz se znovu potvrdila L-0020: prvni verze testu se ukotvila na forward deklaraci a „nic nenasla"). Doloženo, ze to NENI regrese z oprav site — vada prisla s `0cc2d90`. ⬜ **neovereno na HW.** | L-0029 |
| 2026-09-12 | parsery SCPI + NMEA | F3 prezkum, 9 nalezu (1xS2, 6xS3, 2xS4), verdikt **podminene funkcni**. Oba parsery jsou psane opatrne a **zadne preteceni bufferu jsem nenasel** — `tokenize`, `gsv_feed`, `nmea_coord`, `memcpy` ve slozene zprave i chybova fronta maji meze dopoctene a v poradku. Nalezy jsou o **chybejici validaci rozsahu**. Nejzavaznejsi **F-0064 [S2]**: `scpi_num` aplikuje exponent iterativne bez meze, takze `1E2147483647` da 2,1e9 iteraci — a protoze CM4 ma `-mfpu=fpv4-sp-d16` (double = softfloat), je to ~450 s zablokovane CM4. Dosazitelne **bez autorizace**, protoze argument se parsuje na `:869`, kdezto opravneni se testuje az na `:871`/`:876`. Dale: NMEA checksum je nepovinny (F-0065), po preteceni radku chybi zahazovani do konce radku — **tataz vada, jakou `scpi_tcp.c` opravil 2026-09-06** (F-0066, L-0012), `SYST:DATE?` pred prvnim fixem vraci `-3333,-33,-33` jako platne datum (F-0068) a souradnice ve `float` drzi self-survey na ~0,42 m at bezi jak chce dlouho (F-0070, spocitano z ULP). 🔑 Jeden kandidat na nalez **cilene provern a ZAMITNUT**: `fmt_scpi_f6` nema rozsahovou pojistku, ale jediny volajici mu dava tabulku `{0.1,1,10,100}` → UB neni dosazitelne. Kod nemenen. | zatim zadna (F5 nebyla) |
| 2026-09-12 | F5 opravy (modul 13) | **Vsech 9 nalezu uzavreno ve 4 commitech** (A+B+C na pokyn uzivatele). `b483158` validace NMEA (**F-0065** checksum povinny, **F-0066** zahazovani do konce radku + pocitadlo `OVF:`, **F-0071** HDOP jen z GGA). `aaacaf5` SCPI (**F-0064** mez exponentu 308 + zabraneni preteceni `int`, **F-0068** `SYST:DATE?/TIME?` pres `g_rtc_synced` a pod kritickou sekci, **F-0069** utnuta jednotka se neprovede). `868ed6e` **F-0070 + F-0067** souradnice celociselne v 1e-7 stupne (7 souboru). `docs:` **F-0072** komentar u `d2`. 🔑 **Dve veci se pri oprave ukazaly jinak, nez nalez odhadoval:** (1) F-0070 melo podle nalezu 'vysoke riziko, mezijaderny kontrakt' — `ipc_shared.h:135` ale ma `gps_lat_e7` jako `int32_t` uz dnes, takze `IPC_VERSION` se nemeni a preflashovat obe banky neni nutne; (2) F-0067 se opravil **lepe nez navrh** — prechodem na e7 zmizel cely problematicky cast z floatu, misto aby se jen doplnila validace. ⚠️ Zisk F-0070 limituje format NMEA (`ddmm.mmmm` = 1,85 m, `ddmm.mmmmm` = 18,5 cm) — zapsano u pole, ne domysleno. Build BOTH 0 varovani, `audit.py` 92/0/2, CM7 `.text` 599392 -> 599760 B; **CM4 klesl 231944 -> 231880 B**, takze zmena dolozena SYMBOLEM (`%07lu` v obou obrazech, mez 308 v disassembly CM4). ⬜ **neovereno na HW.** | L-0030, L-0031, L-0032 |
| 2026-09-12 | UART konzole | F3 prezkum `freertos_task_uart.c` (2365 r.), 5 nalezu (3xS3, 2xS4), verdikt **podminene funkcni**. 🔑 Soubor je psany nadprumerne opatrne — `stacktest_overflow` je vyclenena funkce S VYSVETLENIM proc, `bgcheck`/`stats`/`scpi ipc` maji buffery v `.bss`, skeny I2C ustupuji scheduleru (F-0020), SD blok je obaleny jako celek, `eth` ma pojistku proti kolizi s CM4. Nalezy jsou o dvou vecech: **F-0073** hlavni vstupni buffer ma 32 B a prikaz delsi nez 31 znaku se TISE utne a PROVEDE (`scpi SENSe:FREQuency:APERture 10` = 32 znaku -> nastavi hradlo 1 s misto 10 s) — **ctvrty vyskyt teze tridy** po F-0056/F-0058/F-0069; a **F-0074** pouceni o zasobniku se neuplatnilo vsude: `scpi ipc` ma vsechno staticke a komentar u nej to zduvodnuje, `scpi` o 70 radku niz ma `resp[128]` na zasobniku a vola `scpi_process` s ramcem 492 B. To je **podlozene merenim z desky** (`stack Uart free 168 B`) a spojuje se s otevrenym F-0055. Dale F-0075 (`fpgasim on` bez horni meze -> `(uint64_t)(hz*1e5)` je UB), F-0076 a F-0077. Jeden kandidat cilene provern a ZAMITNUT: `datalog interval 0` vypadalo na deleni nulou, ale `datalog_set_period_s` clampuje na 1..3600. Kod nemenen. | Kod nemenen. | `b1aa262` (F-0073/F-0075/F-0076) + kontrola ramce v `check_lessons.sh` (F-0077b); **F-0074 a F-0077a odlozeny do `../STATUS.md` TODO #243** na zadost uzivatele (nejdriv konzistentne zvetsit zasobnik v `.ioc`). Lekce **L-0033** (`continue` v dlouhe smycce vypina obsluhy na jejim konci — moje vlastni chyba, zachycena pred commitem), **L-0034** (mez PRED pouzitim: konverze mimo rozsah i odecet v `size_t`), **L-0035** (ramec je vlastnost cele funkce; meri se nad `.elf`). 🔴 Kontrola ramce napoprve nefungovala (vracela 0 B, awk cetl `m[2]` misto `m[3]`) — odhalila to az pozitivni kontrola podle **L-0020**. |
| 2026-09-12 | webova SPA (`SPA_HTML`) | F3 prezkum blobu 2 934 r. (CSS 354 / markup 421 / JS 2 003 r., 125 funkci), 11 nalezu (2xS1, 1xS2, 3xS3, 5xS4), verdikt **podminene funkcni**. 🔑 Overovaci retezec `tools/spa/check.py` je dobry v tom, co meri (literal cely: `nm` 139 290 = 139 289 + NUL; 0 uvozovek; 0 ne-ASCII; DOM bez visicich id), ale **nekontroluje hranici JSON** — a prave tam jsou oba S1: **F-0078** `drawTfom` cte `gps.valid`, ktere `/api/state` neemituje, takze vetve `2D FIX` i `LOCK` jsou nedosazitelne a karta hlasi `NO LOCK` i pri 3D fixu (vedle hlavicky, ktera z tehoz JSON pise `GPS 3D`); **F-0079** `push('ns')` cte `gps.nsat`, jenze `nsat` je v JSON o uroven vys a v bloku `gps` je `num_sat` -> karta KVALITA GPS je trvale prazdna. **F-0080** [S2]: casova osa zivych grafu predpoklada 1 vzorek = 1 s, ale vychozi cesta je SSE, kde server tlaci pri KAZDEM novem mereni (~4/s) — okno '1 h' tak ukaze ~15 minut popsanych jako hodina. Autor tuhle past zna a u Allanovy odchylky se ji brani (buffer `M` plnen jen na zmenu `seq_meas`), na historii grafu `H[]` se uvaha nepromitla. Dale F-0081 (vyjimka v `render()` se spolkne nebo se ohlasi jako chyba site), F-0082 (heslo v `localStorage` otevrene), F-0083 (stavove barvy jako identita rady -> OCXO ma trvale cerveny bar), F-0084..F-0088. Overeno a v poradku: zadna cesta pro vlozeni HTML (vsechny hodnoty ze serveru jsou cisla/booly, volny text jde pres `textContent`), `mathY`/`limitVerdict` sedi na `meas_math.c`, `resetInfo` ma totez poradi priorit jako `main.c`, TDEV se pocita z MDEV. | zatim zadna (F5 nebyla) |
| 2026-09-12 | F5 modul 15, skupina B | Tri nalezy, ktere vyzadovaly rozhodnuti uzivatele. **F-0082** heslo z `localStorage` do `sessionStorage` — a opraveno o jednu vec vic, nez nalez navrhoval: presun sam by minul ty, kdo web uz pouzivali (heslo by jim v `localStorage` zustalo navzdy), takze pribyl jednorazovy uklid + hlaska => **L-0037**. **F-0080** historie grafu throttlovana na 1 Hz (prah 0,95 s kvuli jitteru pollu); autor tu past znal a u bufferu `M` se ji branil, do `H[]` se uvaha nepromitla => **L-0036**. **F-0088** warm-up nove ze snapshotu (`g_warmup`, most app->Core jako `g_adev_1s`), **IPC v14 -> v15**; nova lekce z toho NENI, je to dalsi vyskyt L-0018. 🔑 Velikost sdilene struktury ZMERENA sondou `sizeof` nad starou i novou hlavickou: 480 B / 5 568 B pred i po -> v15 recykluje `_pad_h`; PRESTO se flashuji obe banky (bump je kvuli detekci nesouladu). Overeni: build BOTH 0 varovani, audit.py 92/0/2, `tools/spa/check.py --build` vsech 8 kroku OK (nm 141 769 + NUL = 141 770), .text CM7 600104->600136, CM4 231888->234392. | `e0e542e` |
| 2026-09-12 | F5 modul 15, skupina A | Osm nalezu. Oba **S1** byly tataz vada: klient cetl pole, ktere server neemituje — `gps.valid` (neexistuje) a `gps.nsat` (lezi o uroven vys, v bloku `gps` je `num_sat`). Karta HOLDOVER proto hlasila NO LOCK i pri 3D fixu a karta KVALITA GPS zustala navzdy prazdna; v JS je chybejici pole `undefined`, ne chyba, takze nic nezakricelo => **L-0038**. Dale F-0081 (`lastOk` az po praci + pocitadlo `renderErr` misto prazdneho `catch`), F-0083 (stavove barvy uz nejsou identita rady — OCXO se ridi `mon.ocxo`), F-0084 (jeden zdroj pravdy pro vzhled, `setTheme`/`THO`/klic `gt` zrusene), F-0085 (`okNums` validuje obnovena mereni), F-0086 (zastaraly rozpocet 4096 B -> 6144 B + `_Static_assert`), F-0087 (karta DVOJKANAL popisuje osazenou desku). 🔴 K obema S1 pribyla **kontrola** `tools/spa/json_kontrakt.py` jako krok **5b** retezce. Pozitivni kontrola odhalila, ze prvni verze nastroje prehlizela prave F-0078 (`drawTfom` bere stav pres `var s=LAST`) => **L-0039**: pozitivnich pripadu musi byt tolik, kolik nalezu kontrolu vyvolalo. Overeni: `check.py --build` vsech 9 kroku OK (nm 146 454 + NUL = 146 455), .text CM4 234392 -> 239080. | `4a6e4ba`, `001f3c3`, `2d5f35f` |
| 2026-09-16 | perzistence a záznamníky | F3 průchod řádek po řádku přes `datalog.c` + `flightrec.c` (vč. celého nového `errlog`) + `syscfg.c` + `setup.c` + `calib.c` (**2 456 ř.**), 14 nálezů (1×S1, 1×S2, 8×S3, 4×S4), verdikt **podmíněně funkční**. 🔴 **Modul vznikl z opravy nepravdivého tvrzení v tomto souboru** — „auditovaný veškerý vlastní kód" neplatilo a `errlog` (2026-09-13, FW v0.9.0) audit nikdy neviděl. Jádro modulu je psané dobře (ruční serializace, CRC u každého záznamu, power-safe pořadí zápisu, dvoustupňový zápis errlogu ISR→ring→flash). Nálezy mají dva jmenovatele: **(1) co má přežít reset, ho nepřežije** — **F-0089 [S1]** tři nastavení datalogu se obnovují jen při studeném startu, ačkoli v BKP nejsou (doloženo výčtem DR1/DR2/DR6 z `rtc.c:89-120`); **F-0091** `flightrec` si po 64 dumpech maže vlastní nejnovější záznam při každém bootu; **F-0092** záznam o pádu ztratí jméno tasku, protože 6B `tag` utne `g_crash_text` přesně na dvojtečce. **(2) komentář popisuje ochranu, kterou kód nedělá** (potřetí v projektu, L-0028) — **F-0090 [S2]** `datalog_init` nuluje `s_be` před `s_ready` a čtenáři mutex nikdy neberou → okno pro dereferenci NULL; **F-0094** `calib_save` zapíše „uložena kalibrace" dřív, než zjistí, jestli se uložila. Mapa regionů W25Q přepočtena numericky (žádný překryv, vše zarovnané), umístění **všech** bufferů ověřeno `nm` nad `.elf` (všechny v AXI SRAM), rámce všech funkcí změřeny `objdump`em (L-0035). Šest kandidátů cíleně prověřeno a **zamítnuto** (mj. neescapovaný `%s` v JSON, kopie neinicializovaného ocasu do IPC, dělení nulou v `read_bulk`). `tools/audit.py` 92 OK / 0 selhání / 2 s varováním (gcc 14.3.1), **žádné varování v tomto modulu**. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-17 | F5 opravy (modul 16, skupina A) | **6 nálezů uzavřeno v 5 commitech + 1 pojistka.** `6d1b6e5` **F-0089 [S1] + F-0093** (musely spolu): obnova datalogu přesunuta nad `if (g_syscfg_bkp_valid) return;` — tři pole, která nejsou v BKP, se do té chvíle po KAŽDÉM teplém resetu tiše vracela na výchozí ON/AUTO/10 s; nový `datalog_cfg_quiet(bool)` potlačí falešné `ERRLOG_K_CFG` po dobu obnovy. `a80caed` **F-0090 [S2]**: `s_ready` se shazuje první a zvedá poslední (obě hranice `__DMB()`), čtyři čtenáři čtou `s_be` jednou do lokálu. `15aa8d3` **F-0094**, `e583432` **F-0097** (3× `_Static_assert`), `a9af9de` **F-0099**. `165023c` = rozdílová kontrola v `check_lessons.sh`, která F-0089 příště zachytí. 🔑 **Pět pozitivních kontrol** (L-0020/L-0039): každý ze tří assertů ověřen nafouknutím struktury o 4096 B, nová sekce check_lessons ověřena na OBOU podobách vady. 🔑 **Dvě věci dopadly jinak, než nález navrhoval:** F-0090 dostal OBĚ varianty (pořadí i lokální kopie ukazatele), ne jen minimální; F-0099 hlásí výsledek i v `qspi_req_service`, který dosud mlčel úplně — požadavek z okna CHYBY konzoli nevidí, takže to byla jediná chybějící stopa. Build Release BOTH 0 varování, audit.py 92/0/2, CM7 `.text` 604560 → **604752 B** (+192), CM4 beze změny. Dokázáno v obrazu (ne jen přeloženo): pořadí volání v `syscfg_load` před testem `g_syscfg_bkp_valid`, oba `dmb sy` v `datalog_init`, podmíněný `bl errlog_put` v `calib_save`. `IPC_VERSION` se nemění. ⬜ **neověřeno na HW.** | L-0050, L-0051, L-0052, L-0053 + rozšíření L-0026 |
| 2026-09-17 | čas, alarmy, watchdog | F3 průchod řádek po řádku přes `rtc.c` + `alarm.c` + `watchdog.c` + `beeper.c` + `bootled.c` (~1 290 ř.), 10 nálezů (1×S2, 5×S3, 4×S4), verdikt **podmíněně funkční**. 🔑 **Modul zvolen i proto, že oprava F-0089 na něm stojí** — a premisa se potvrdila: výčet v `MX_RTC_Init` obsahuje právě těch deset globálů, které BKP drží, takže nová kontrola v `check_lessons.sh` odvozuje správnou množinu; kódování všech tří bitových polí ověřeno round-tripem zápis↔čtení. Jádro modulu je kvalitní (kanonická sekvence IWDG dle RM0399, `rtc_lse_apply_calib` se **předem** vyhýbá tísňové smyčce s timeoutem 1 s uvnitř HAL, `mon_edge` guardy podložené naměřeným startovním transientem VBAT). Nálezy mají dva jmenovatele: **(1) diagnostika může přiřadit událost ke špatnému resetu** — F-0105 (stall se píše při DETEKCI, ne při resetu, a `s_stall_logged` se nikdy nenuluje; BKP je zálohovaná CR2032, takže záznam přežije dny), F-0106 (HardFault je jediný z devíti zapisovatelů, který píše magic PRVNÍ → při pádu z rozbitého zásobníku může ohlásit cizí PC), F-0104 (výsledek čekání na `PVU`/`RVU` se zahodí → watchdog tiše 0,5 s místo 4 s, přepočteno z LSI); **(2) jediný vlastník, který není jediný** — **F-0103 [S2]**, `alarm.c:133` deklaruje jediného vlastníka stavu patternu a `alarm_test()` (UartTask) i `beeper_boot_melody()` (UiTask) to porušují. Dále F-0107 (`rtc_crash_assert` bez `DBP`, 5 z 9 zapisovatelů ho má), F-0108 (nenaběhlý LSE → `Error_Handler` → mrtvý přístroj, ačkoli měření jede z HSE — politika, souvisí s F-0007). 🔑 **Tři kandidáti cíleně prověřeni a ZAMÍTNUTI**, mj. moje vlastní hypotéza, že halt sondou vyrobí falešný `stall` — `uwTick` jede z TIM6 ISR, která se během haltu nevykoná, takže heartbeaty nezestárnou (L-0011). Umístění všech statik ověřeno `nm` (vše v AXI SRAM, modul nemá DMA), rámce změřeny `objdump`em, časování TIM7/IWDG/LSE přepočteno z hodinového stromu. `audit.py` 92 OK / 0 / 2, žádné varování v modulu. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-17 | F5 opravy (modul 17, A+B) | **9 z 10 nálezů uzavřeno v 6 commitech; F-0108 vědomě odložen.** `3ccddb4` **F-0106** (HardFault píše data první, magic naposled — byl jediný z devíti, kdo to měl obráceně), `887d9f4` **F-0107** (poslední dva zapisovatelé do BKP si odemykají `DBP`), `389690c` **F-0110 + F-0111** (clamp kmitočtu před výpočtem; `beeper_init()` vrací `bool`), `76bf1f6` **F-0104 + F-0105** (odečtené `PR`/`RLR` + řádek `WATCHDOG:` ve `status`; zotavený stall zneplatní záznam, aby se nepřipsal cizímu resetu), `68ae8c8` **F-0103 [S2]** (`alarm_test()` přes flag, boot melodie vzájemně vyloučena — stav pípáku má zase jednoho vlastníka), `0b0e5b6` **F-0109 + F-0112** (`docs:`). 🔑 **Rozhodnutí u skupiny B a jejich cena:** melodie se NEPŘESOUVÁ do defaultTasku (sahalo by to na časování startu, CLAUDE.md 4c) — zbytková vada je pojmenovaná v kódu; u F-0104 se vědomě NEDĚLÁ retry ani `Error_Handler` (IWDG už běží, START je neodvolatelný), jen se zveřejní dosažený stav (L-0009); u F-0105 se zvolilo zneplatnění záznamu místo zápisu do `errlog`, protože ten závisí na neschváleném F-0092. ⚠️ **Upřesnění proti nálezu:** `HAL_GPIO_Init` je v HAL `void`, takže u F-0111 jsou vyhodnotitelná čtyři volání z pěti. Build Release BOTH 0 varování, audit.py 92/0/2, CM7 `.text` 604752 → **605344 B** (+592), `.bss` +16 B, CM4 beze změny. Dokázáno v obrazu: nové pořadí zápisů do BKP u HardFaultu, `orr #256` (DBP) v `rtc_crash_assert`, `alarm_test` zkrácený na čtyři instrukce bez `bl pattern_start`, `alarm_tick` začínající `bl beeper_melody_busy`. `IPC_VERSION` se nemění → stačí flashnout bank1. ⬜ **neověřeno na HW.** | L-0054, L-0055, L-0056 |
