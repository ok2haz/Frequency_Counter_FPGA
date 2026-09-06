# H757_LED — projektová poznámka

## 📍 MAPA DOKUMENTŮ — co je autorita a co ne (audit 2026-08-30)

🔴 **Autoritativní je JEN tento soubor + `docs/HW_REFERENCE.md` + `../STATUS.md`.**
Všechno ostatní je buď *plán*, *checklist*, nebo *historie* — **nikdy z toho neodvozuj,
jak kód vypadá dnes.** Ověřuj proti zdrojáku nebo proti běžící desce přes UART.

| dokument | role | pozor |
|---|---|---|
| **CLAUDE.md** (tento) | **jak to funguje teď** | jediný, který se drží aktuální |
| **docs/HW_REFERENCE.md** | stabilní tabulky (hodiny, DSI, protokol, s_view…) | mění se zřídka |
| **../STATUS.md** | stav obou stran + TODO | cross-project rozcestník |
| **../SKILL.md** | **metoda** — čím se ověřuje, jaká třída chyb se opakuje | ⚠️ **žádná fakta o kódu** (ta patří sem), jen postup |
| 🟢 **`citac_zadani_UI.md`** | **zadání UI** (encoder, struktura menu, co musí být vidět a proč) | struktura a chování, **NE vzhled** |
| 🟢 **`UI_ENCODER_NAVRH.md`** | překlad zadání UI do geometrie 800×480 + 4 rozhodnutí | **návrh, nic z toho není v kódu** |
| 🟢 **`../citac_zadani_predavaci.md`** | **předávací zadání NOVÉ desky** (2026-08-30): vstupní modul + firmware FPGA | **smluvní pro to, co se teprve postaví — NEPOPISUJE dnešní kód** |
| `UI_SIZES.md` | rozměry UI prvků | aktualizovat po každé změně layoutu |
| `CUBEMX_CHECKLIST.md`, `*_BRINGUP_*.md`, `HW_OVERENI_PRUCHOD.md` | **postupy**, ne popis stavu | |
| `WEB_UI_PLAN.md`, `*_NAVRH*.md`, `USB_CDC_PLAN.md`, `ENCODER_J7_NAVRH.md` | **plány/návrhy** | část nikdy nebyla realizována |
| `docs/CLAUDE_ARCHIV.md` | **historie** vyřazená z CLAUDE.md | jen k dohledání „proč" |
| `TODO_ZITRA.md` | snímek k datu v nadpisu | **rychle stárne** (sám to o sobě píše) |
| 🔴 **`.claude/zadani_*.md`** | **PŮVODNÍ zadání z 2026-06-19, NEUDRŽOVANÉ** | **NENÍ specifikace dneška** |

⚠️ **Konkrétní důkaz, proč `.claude/zadani_*.md` nebrat jako zadání:** uvádějí 13 fontů,
z nichž **5 už neexistuje** (`mono_20/21/27`, `sans_10/17/20`) a **5 dnešních tam chybí**
(`mono_16/22/52`, `sans_18/32`). Skutečný seznam = `ls CM7/libui/src/fonts/`, generátor
`CM7/libui/tools/font_gen/gen_fonts.js`.

⚠️ **Duplicitní fakta = riziko rozjetí.** Tyhle hodnoty jsou zapsané na víc místech;
při změně je nutné projít všechny: I2C timing `0x70303AEE` (3 dok.), `HSE_VALUE 25000000`
(3), MAGIC `0xA5` (5), SRAM4 `0x38000000` (4). Ověřuj vždy proti kódu, ne proti dokumentu.

## Přehled
STM32H757 dual-core (CM4 + CM7) projekt generovaný v STM32CubeIDE.
- **CM7** = hlavní jádro, veškerá logika (FreeRTOS, displej, SDRAM, I2C, UART).
- **CM4** = konektivita (výhled: ETH/SCPI/web); dnes GPIO+timer (beep+LED) + **IPC konzument** + vlastní CPU% přes DWT. ✅ **CM4 BĚŽÍ (ověřeno na HW 2026-08-14)** — `CM4: alive (IPC heartbeat)`, header „4:xx%". ⚠️ **ALE JEN BEZ AKTIVNÍ SONDY** — viz „Dvoujádro / IPC".
- Veškerý vývoj displeje je v `CM7/Core/Src/`.
- **Dvoujádro CM7↔CM4** viz sekce „Dvoujádro / IPC" níže + `NAVRH_ARCHITEKTURA_CM7_CM4.md` §11 + `DUALCORE_BRINGUP_CHECKLIST.md`.

Hardware: STM32H757 → DSI (1 lane) → **TC358762** DSI-to-DPI bridge → Waveshare **4,3"** 800×480 IPS panel. ATTINY MCU @ I2C 0x45 (power/backlight/reset), TMP117 @ 0x48 (teplota), FT5x06 (touch).

> **Reference:** stabilní tabulky (hodiny, DSI/LTDC/TC358762 timing, framebuffer/MPU, SDRAM mapa,
> FreeRTOS tasky, 64B protokol, W25Q region mapa, seznam `s_view`) jsou v **`docs/HW_REFERENCE.md`**.
> Co bylo z této poznámky vyřazeno při redukci 2026-08-29 (historie, datované „hotovo", staré
> varianty) je v **`docs/CLAUDE_ARCHIV.md`** + plná záloha `docs/.CLAUDE.md.pre-trim.bak`.

## ⚡ ZLATÁ PRAVIDLA (poruš = rozbiješ desku / boot / měření)

**HW konfigurace a regen (CubeMX „Generate Code"):**
- **I2C4 i I2C1 Timing = `0x70303AEE`** (~100 kHz) — ručně NEPŘEPOČÍTÁVAT. Špatná 400 kHz hodnota → tmavý displej + zaseklá ATTINY na sběrnici (jen power-cycle desky i panelu pomůže).
- **DSI:** `VidCfg.Mode==DSI_VID_MODE_BURST` + `ColorCoding==DSI_RGB565`. Jinak per-řádek shear / rotace barev. Při regresi displeje sem koukej první.
- **PB12 (FPGA CS) default Output Level = High** v `gpio.c` — jinak STM drží CS LOW během config loadu FPGA z flash → GW1NR-9 nenaběhne (`RX0:FF`).
- **`HSE_VALUE` = 25000000** v obou `hal_conf.h`. FW **nenaběhne** na desce s 10 MHz HSE. `fpga_freq_init` z toho počítá SPI prescaler.
- **QUADSPI `FlashSize=25`**, **SDMMC1 `HardwareFlowControl=ENABLE`** (jinak SD datová cesta vůbec nejede), **`configCHECK_FOR_STACK_OVERFLOW=2`** (ve `FreeRTOSConfig.h` USER CODE, NE v GUI).
- **`LWIP_RAM_HEAP_POINTER` NEDEFINOVAT** — pevná adresa by dala haldu lwIP do SRAM1 (= CM7) a natrvalo shodila CM4.
- **NEPOVOLOVAT v IOC:** I2C1 (celý init je ruční), I2C4 interrupt, RTC NVIC (Alarm/WakeUp), TIM7 (beeper).
- Po každém regenu, co **přidal soubory**: `Close Project → Open Project → ověřit → teprve pak Clean → Build`. Clean před Close/Open je aktivně škodlivý. Ověř `grep -c FatFs CM7/Debug/sources.mk` > 0.

**Vlákna / časování:**
- Kreslí **VÝHRADNĚ UiTask** (libprim/libui nejsou thread-safe). RTC registry **VÝHRADNĚ defaultTask**. `g_sensors` zápis **VÝHRADNĚ SensorsTask**.
- **Žádný spin > ~10 ms** v defaultTask / UiTask / FpgaTask (mají watchdog heartbeat) → IWDG reset. Blokující práci (SD, membench, sd_export, QSPI erase) dělej **jen z UartTasku** (nemonitorovaný).
- Cross-task komunikace = sdílené globály + request/pend, ne přímé volání HAL z cizího tasku.

**Rendering:**
- **Každý partial redraw MUSÍ začít clear** (fill/blit REPLACE) — jinak dirty-rect copy-forward přes 3 buffery problikává.
- 🔴 **Guard „obsah je stejný, nekresli" smí přeskočit až po `prim_stm32_fb_count()` vykresleních.**
  Takový guard je JEDEN stav, ale framebuffery jsou TŘI — po jednom nakreslení má obsah jen ten
  buffer, do kterého se zrovna kreslilo, a jakmile se cyklus dostane na ostatní, ukážou starší
  obsah = **problikávání**. ⚠️ Copy-forward to nezachrání: kopíruje sjednocení dirty z posledních
  **dvou** snímků, takže při přeskakování (a tedy bez flipu) dirty rect toho jediného vykreslení
  z historie vypadne. Vzor: počítadlo `reps`, reset při změně obsahu, přeskakovat od
  `reps >= prim_stm32_fb_count()`. **Konstantu 3 neduplikuj** — je vystavená funkcí.
  (Takhle jsem 2026-08-30 rozbil trend; viz STATUS #88.)
  ✅ **Od 2026-09-04 je na to sdílená funkce `gate_same(&reps, same)`** (`screen_main.c`, krytá
  `screen_main_selftest`). **Nový guard „obsah je stejný → nekresli" ji MUSÍ použít**, ne si vzor
  opsat. Nasazená na trend, statistiky (3 karty = 3 počítadla), sys pilulku a CPU blok.
  🔴 **Guard NESMÍ být schovaný za další podmínkou, která po usazení přestane platit**
  (typicky „animace se hýbe" / „mix < 1.0"). Přesně tím se to rozbilo podruhé:
  `tick_stats_anim` měl guard pod `if (m0)`, takže po dojetí easingu se už nikdy nekreslilo
  a dva ze tří bufferů držely starou hodnotu **natrvalo**; `tick_sys_xfade` vracel 0 hned na
  `s_sys_mix >= 1.0f`, takže usazená pilulka se nakreslila jedinkrát.
  **Volej `gate_same` na KAŽDÝ tik.** Plný render nastaví `reps = 1` (je to taky jedno vykreslení).
  ⚠️ **Na INKREMENTÁLNÍ překreslení to nestačí:** `screen_main_redraw_signal` kreslí jen
  *změněné* segmenty, tedy předpokládá, že předchozí stav v cílovém bufferu už je — to je jiná
  a hlubší třída (STATUS #137). Totéž platí pro `dchg()` (#97) a change-key skipy v ALLAN/HISTOGRAM
  — zatím to tam vychází, ale je to tatáž latentní chyba.
- Po přepnutí tématu: `screen_main_invalidate()` + `screen_main_init()` (bg_cache je v starých barvách).
- `UI_COLOR_*` nelze ve file-scope `static const` (derefují runtime ukazatel `g_ui_theme`).

**Data / protokol:**
- Změna 64B rámce / offsetů / škálování → **OBĚ strany** + zápis do `STATUS.md`. Změna `IPC_VERSION` → **přeflashnout OBĚ banky**.
- `freq*_x100000` už zahrnuje /4 i /16 — **STM NEnásobí**.
- **Headline + statistiky (Allan, drift, offset, spektrogram, kužel) = SIMULACE**, dokud neběží SPI link (STATUS #2). Web servíruje reálná FPGA data → bez desky správně `null`.
- Nikdy neservíruj `sigma_tau`/`offset`/`drift` ze snapshotu jako měření (CM7 je záměrně neplní).
- Emulovaná data (`fpgasim`) musí být označená (`SIM` marker, `IPC_F_SIM`, `DATALOG_F_SIM`, `DIAG:SIM?`).
- 🔴 **Na I2C1 NIKDY neposílej `General Call 0x06`** (reset adresa 0x00, druhý bajt 0x06). Na nové
  desce by naráz resetoval `MCP4728` (práh a hystereze komparátoru!), `TMP117` i `ADS1115`.
  Platí i dnes preventivně — žádný náš kód na adresu 0x00 sahat nemá.

**Text na displeji — TŘI tiché pasti (všechny reálně kously):**
- 🔴 **`%f` v `printf`/`snprintf` NEVYTISKNE NIC.** Projekt linkuje **nano.specs bez float formátování** —
  ověřeno v mapfile: `_printf_float` ani `_dtoa_r` **nejsou slinkované**. Kontrola:
  `grep -c "_printf_float\|_dtoa_r" CM7/Debug/H757_LED_CM7.map` → musí být `0`, a tedy **žádné `%f`/`%e`/`%g`**.
  Používej `fmt_fixed` (float, **JEN 1–3 desetiny**, viz past níže), **`fmt_sdec`** (double, ±, 0–5 desetin),
  **`fmt_dec_u`** (= `fmt_sdec` bez vynuceného `+`) nebo `fmt_hz`.
- 🔴 **`fmt_fixed(…, dec >= 4)` vytiskne POUZE CELOU ČÁST** (nalezeno 2026-09-04, STATUS #132).
  Má `switch` jen pro `case 1/2/3`; cokoli vyššího spadne do `default:` = `"%ld"` — **bez varování,
  bez ořezu, prostě bez desetin**. Reálně to znamenalo, že v okně MĚŘENÍ hlásilo `σ (n-1)` **vždy
  „0 Hz"** (σ dobrého OCXO je hluboko pod 1 Hz) a v okně ANALÝZA se rozšířená nejistota `U (k=2)`
  zobrazovala jako **„+-0 Hz"** — tedy právě to číslo, kvůli kterému to okno existuje.
  **Pravidlo: `fmt_fixed` VÝHRADNĚ pro 1–3 desetiny, jinak `fmt_dec_u`/`fmt_sdec`.**
  Kontrola: `grep -rn "fmt_fixed([^;]*, *[4-9])" CM7` musí být **prázdný**.
  ⚠️ `default:` mlčí dál, takže past je pořád živá pro nové volající.
- 🔴 **Většina velkých fontů je SUBSETOVANÁ a chybějící glyf se TIŠE PŘESKOČÍ** (`prim_draw_text`:
  `if (g == NULL) continue;` — žádný fallback, žádná šířka). Text prostě zmizí. Aktuální subsety:
  `mono_75`/`mono_52` = jen číslice, `mono_30` = `0123456789,.+-`, `sans_32` = `Hzsmunp`,
  `mono_25` = číslice + VELKÁ + malá + ` :.,-/()=?Δ`. **Plný charset mají jen `mono_14/16/18/20/22`
  a `sans_14/16/18`.** Než napíšeš nový literál velkým fontem, ověř, že na něj má glyfy —
  charset se definuje v `CM7/libui/tools/font_gen/gen_fonts.js`, regenerace
  `cd CM7/libui/tools/font_gen && ../../../../tools/node-v20.18.1-win-x64/node.exe gen_fonts.js`.
  (Audit 2026-08-29 našel **15 takto neviditelných řetězců** — mj. celý splash nápis „GPSDO",
  text modalu „Opravdu restartovat?" a lowercase půlku 11 nadpisů oken.)

**Nastroje a jejich pasti:**
- 🔴 **`.ps1` PIS CISTE ASCII.** PowerShell 5.1 cte skript BEZ BOM jako **ANSI (cp1252)**,
  takze vicebajtovy UTF-8 znak (`—`, `⚠️`, ceska diakritika) rozbije parser a chyba se
  hlasi na uplne jinem radku (`The '<' operator is reserved`). Audit 2026-09-06 nasel
  **121 ne-ASCII znaku ve 12 skriptech** v `tools/`. Kontrola:
  `[Parser]::ParseFile('tools/x.ps1',[ref]$null,[ref]$e)`.
- ⚠️ **Vystup `STM32_Programmer_CLI` obsahuje v banneru hexa retezce**, takze pri
  parsovani `-r32` ber **POSLEDNI** shodu regexu, ne prvni (`gpio_drift.ps1`).
- 🔴 **Novy `.c` soubor se do buildu NEDOSTANE bez `Close → Open Project`** — prelozi se,
  ale linker hlasi `undefined reference`, protoze IDE ho nema v `Release/*/subdir.mk`.
  Bez IDE to obejdes tim, ze implementaci das do souboru, ktery uz v buildu je
  (USER CODE blok). **Hlavicky makefile nepotrebuji, `.c` ano.**
- **`tools/gpio_drift.ps1`** — porovna konfiguraci VSECH GPIO proti drivejsimu snimku;
  odhali tridu vady „pin tise ztratil konfiguraci" bez tabulky ocekavanych AF.
  ⚠️ Cte sondou, takze **haltuje jadro a rozbiji I2C4** — levnejsi bezna cesta je
  `status` -> radek `GPIO HLIDAC` (pocitadlo oprav primo z firmwaru).

**Build / diagnostika:**
- Stavěj **Release (`-Os`)**, ne Debug (`-O0`) — −22 % velikosti, jinak stejné defines. Po přepnutí ověř časování (DWT `delay_us`, bit-bang pípání, I2C recovery).
  - 🔴 **PROČ Release léta nešel přeložit (vyřešeno 2026-08-29).** Když se do projektu přidávaly
    `app/`, `libui/src/` a `libprim/src/`, přidaly se **jen do konfigurace Debug**. V `.cproject`
    proto Release konfiguraci chyběly **obě** věci naráz:
    1. **`<sourceEntries>`** neměly `app`, `libui/src`, `libprim/src` → IDE pro Release vůbec
       negenerovalo `Release/app/subdir.mk` & spol., takže se celé UI nepřekládalo (a linker
       pak hlásil `undefined reference` na všechno z app/libui/libprim);
    2. **C-compiler `includepaths`** neměly 4 cesty (`CM7/app`, `libui/include`,
       `libprim/include`, `libprim/src`) → `fatal error: screens/screen_main.h: No such file`.
    Opraveno přímo v **`CM7/.cproject`** (obě konfigurace jsou teď identické; záloha
    `CM7/.cproject.bak-2026-08-29`). ⚠️ **Po každé změně `.cproject` musí IDE model znovu načíst:
    `Close Project → Open Project`** — teprve pak přegeneruje `Release/*/subdir.mk`.
    ⚠️ Kontrola, že je model kompletní: `find CM7/Release -name subdir.mk | wc -l` musí dát
    **stejné číslo jako Debug (18)**, ne 11. Drobnost pro paritu: Release nemá volbu
    `converthex` (negeneruje `.hex`) — flashuje se `.elf`, takže to nevadí.
    ⚠️ `scripts/build.sh Release CM7` funguje až **po** prvním překladu z IDE (makefily negeneruje).
- **CPU zátěž měř JEN přes UART `stats`** — ladicí sonda cíl haltuje → `g_rtos_cpu_pct` nafouklý.
- **CM4 testuj jen po čistém power-cyklu bez ladicí sondy** (sonda rozbíjí boot handshake + dělá falešné HardFaulty).
- Verze: každé zvýšení = commit + `git tag vX.Y.Z` na tomtéž commitu.

## 🔴 DISPLEJ ZLOBÍ? ZMĚŘ NEJDŘÍV PAMĚŤ, NE KRESLICÍ KÓD

🔴🔴 **GPIOG PÍŠOU OBĚ JÁDRA — KONFIGURACE PINŮ SE ZTRÁCÍ.** CM7 na něj sahá kvůli
FMC (PG0,1,2,4,5,8,15), QUADSPI NCS (PG6) a LED_1; CM4 kvůli ETH (PG11,13), LED_2
(PG7), `ETH_RES` (PG14), `ETH_INT` (PG12). `HAL_GPIO_Init` dělá nad `MODER`/`AFR`
**neatomické read-modify-write**, takže ztracený zápis jednoho jádra tiše vrátí cizí
pin. Změřeno dvakrát: **PG8** (`FMC_SDCLK`) přišel o `MODER` → černý displej,
**PG11** (`ETH_TX_EN`) přišel o `AFR` → deska nedostala IP (TX deskriptor se přitom
dokončil bez chyby!). Vždy se ztratí JEN JEDNA položka, soused z téhož volání přežije.
⚠️ Brání tomu `gpio_guard_tick()` (1 Hz z defaultTask) — kontroluje, opravuje a
**počítá** (`status` → `GPIO HLIDAC`). Je to obrana, ne oprava příčiny; správně
by se konfigurace sdílených GPIO měla serializovat (HSEM). **Nenulové počítadlo
= závod opravdu probíhá.** Nový pin na GPIOG → přidej ho do hlídače.

🔴🔴 **A ÚPLNĚ NEJDŘÍV: DOSTÁVÁ SDRAM HODINY?** `PG8` = `FMC_SDCLK` musí být
v **AF12**. Ověření sondou: `GPIOG->MODER` (0x58021800), bity [17:16] pro PG8
musí být `10`, **ne `11` (analog)**. Bez hodin čip nepřijme jediný příkaz,
neobnovuje se a **čte samé nuly** — což vypadá jako „rozpad obsahu / vadný
refresh / aliasing adres“, ale je to prostě mrtvý takt (STATUS #196).
⚠️ `MX_FMC_Init` proto PG8 znovu potvrzuje **před** inicializační sekvencí;
původce přepisu se nenašel (#197), takže tu obranu **neodstraňuj**.
⚠️ Rychlá indicie bez sondy: `membench` hlásí u vzoru `0x00` **nula chyb** a
u všech ostatních vzorů selhání — to je podpis „všechno čte nuly“, ne vady buněk.



**Pořadí, které stálo tři kola ladění, než se zavedlo (audit 2026-09-05):**

1. **`membench`** → řádek **„retence po 1 s" musí být 0** a celkem 0 chybných bitů.
   Framebuffery i `bg_cache` leží v SDRAM; když se obsah rozpadá, vypadá to jako
   chyba vykreslování — **černý displej** (obsah vyhasl k nule = černá v RGB565)
   nebo **problikávání** (vyhasne jen část mezi překreslením).
2. **`status`** → `DISPLEJ:` (selhal bring-up a v kterém kroku?) a `LTDC: podteceni FIFO`.
3. **`panel`** → zopakuje bring-up za běhu s výpisem každého kroku.
4. **`status` -> `LTDC: podteceni FIFO`.** Nenulove = KAZDY flip da poskozeny
   snimek, tedy „problikne pri kazdem prekresleni, staticky obraz je OK".
   Pricina: `copy-forward` bezi na DMA2D SOUBEZNE se skenovanim panelu z teze
   SDRAM. Lek je **mrtvy cas DMA2D** (`d2ddt`, `DMA2D_AMTCR.DT`); zmereny zlom
   na teto desce je ~208, vychozi hodnota je **240** (STATUS #200).
   ⚠️ Meritko: `podteceni/flip` SMI prerust 1 — cistac se inkrementuje jednou za
   `prim_stm32_present`, takze pri hodnote presne 1,000 je SATUROVANY a nic
   nedokazuje (tim byl #194 rok neprukazny).
5. **Teprve pak** kreslici kod (guardy, copy-forward, dirty rect).

⚠️ **Verdikt o překryvu adres z `membench` je platný AŽ nad pamětí s čistou retencí** —
vyhaslá kontrolní buňka vypadá jako „cizí zápis" a vyrobí falešné podezření na HW
(přesně tak vzniklo #72, viz níže).

🔑 **Proč se to hledalo tak dlouho** (SKILL.md §6d): vada byla v kódu **od prvního
commitu**, ale byla **maskovaná** tím, že se framebuffery přepisují každý snímek a
LTDC je nepřetržitě čte — obojí řádky implicitně obnovuje. Projevila se až
2026-08-30, kdy přibyl guard „obsah je stejný → nekresli" a **ubylo přepisování**.
Symptom se objevil hned po té změně, takže se tři kola hledalo ve vykreslování.
**Optimalizace, které něco dělají méně často, samy nic nerozbijí — jen odkryjí,
co bylo pod nimi.**

**Neuzavřené HW podezření:**
- ✅ **SDRAM `REFRESH_COUNT` byl 4,7× mimo spec — OPRAVENO 2026-09-04 (1835 → 371).**
  🔴 **Bylo to tam od PRVNÍHO commitu** (`git log -S` na `fmc.c`), ne regrese.
  1835 je z ST příkladu pro jinou desku a obnovilo celou matici za **304 ms** místo 64 ms.
  Správně pro tuhle desku (SDCLK 50 MHz, **8192 řádků**): `64e-3·50e6/8192 − 20 = 371`.
  Osazený čip je dle schématu **`MT48LC16M16A2TG`** (4 banky × 4M × 16 = 32 MB, 8192 řádků,
  512 sloupců) → `RowBitsNumber=13` i `ColumnBitsNumber=9` v `fmc.c` jsou **správné**.
  ⚠️ **Při změně SDCLK se REFRESH_COUNT MUSÍ přepočítat** (při 100 MHz vychází **761**).
  🔴 **Proč se to nechalo tak dlouho:** hodnota byla v komentáři označená jako podezřelá,
  se **spočítanou správnou hodnotou i návodem, čím to změřit** — a nechala se jen proto,
  že **jedno** měření retence vrátilo 0. Po dvanácti dnech dal tentýž test 1 048 646
  chybných bitů. **Jedno čisté měření neruší výpočet** (SKILL.md §6e).
  Framebuffery vadu maskovaly, rozpadala se
  **`bg_cache`** — zapsaná jednou v `screen_main_init` a pak už jen čtená → partial redraw
  blitoval poškozené pozadí → problikávání celé plochy.
  ⚠️ **Ověřuj retenci, ne jen „displej jede".** `membench` řádek **„retence po 1 s"** musí být **0**.
  Hodnota se kdysi nechala právě proto, že jedno měření vrátilo 0 — a bylo to falešné uklidnění;
  po power-cyklu dal tentýž test **1 048 646 chybných bitů**. **Jedno čisté měření nestačí.**
- 🔴 **SDRAM adresy se můžou opakovat po 2 MB** (podezření pájka `FMC_A9`=`PF15`). Než saháš na zobrazovací řetězec, přečti `membench` řádek `fb_alias`. Viz „Benchmark pamětí".
  ⚠️ **Verdikt o překryvu je NEDŮVĚRYHODNÝ, dokud retence není 0** — rozpadlé buňky vypadají
  jako „cizí zápis do kontrolní buňky" (přesně to `membench` 2026-09-04 hlásil). Nejdřív refresh, pak alias.

> **📐 Fyzická velikost panelu je pro UI KLÍČOVÁ: 4,3" @ 800×480 → 8,54 px/mm (217 DPI), aktivní plocha ≈ 93,7 × 56,2 mm.**
> Při návrhu layoutu počítej v **milimetrech, ne pixelech**: dotykový cíl potřebuje **≥60 px (7 mm)**, pohodlně 77 px (9 mm);
> text má cap height ≈ 0,72 × `ascent` fontu, takže `mono_16` = 1,35 mm ≈ 9 úhlových minut na 50 cm (doporučeno 16–24′)
> → přístroj je čitelný z ~30–35 cm. Layout byl původně laděn pro 7" panel, kde je všechno o 39 % větší — proto se v UI
> pořád najdou prvky pod hranicí (viz TODO #11 ve `STATUS.md`). Přepočet: **1 mm = 8,54 px**.
> **Přehled ideálních vs. aktuálních velikostí všech prvků = `UI_SIZES.md` — po každé změně layoutu ho aktualizuj.**

**Footer RUN/STOP (2026-07-20): label = AKCE, ne stav.** Běží-li měření, tlačítko nabízí **červené „STOP"**
(`UI_BUTTON_STOP` v libui + `btn_stop_*` v obou paletách); při zastaveném **zelené „RUN"**.
Stav navíc nese **podbarvení velkého kmitočtu**:
při STOP se zóna čísla překryje `UI_COLOR_FREQ_STOP_BG` (poloprůhledná červená, ~15 %) → na první pohled
je vidět, že měření stojí. ⚠️ Alfa < 0xFF vyřazuje DMA2D fast-path (CPU fill), ale **při STOP neběží 20Hz
`tick_freq`**, takže se kreslí jen při plném renderu a při stisku → CPU dopad ~0. Přepnutí RUN/STOP proto
MUSÍ volat `screen_main_redraw_freq_area()` (per-segment dirty cesta by podklad nepřekreslila).

**Hlavní obrazovka má DVĚ rozložení, přepínatelná v okně DISPLEJ** (`render_body_grid` → `_hybrid` / `_classic`, persist v syscfg). ⚠️ **Přepínač je v okně DISPLEJ, NE ve footeru** (ve footeru by přebil PERIOD/FREQ toggle; rozložení je vlastnost displeje, patří vedle Vzhledu). ⚠️ **KLASICKÉ je zamrzlá větev** — nemá easing statistik ani trendu (tiky `screen_main_tick_stats_anim`/`_trend_anim` v něm hned vracejí 0) a záměrně se v něm už nedělají změny; každá další úprava hlavní obrazovky míří do hybridního. **KLASICKÉ**: Allan 53 % šířky, pravý sloupec stohovaný offset (v.54, mono_16) / trend / signál (v.43), všechny mezery `SCR_MAIN_CARD_SECTION_GAP`.
**HYBRIDNÍ (výchozí)**: **vlevo Allan graf 364×242** (přes CELOU výšku mřížky, plné osy — sdílený `allan_plot` má od #11(2b) popisky mono_16 v kartě i okně), **vpravo statistiky** Offset/σ@1s/Drift (3× 125×64, hodnoty mono_18 — proto `SCR_MAIN_GRID_LEFT_RATIO` snížen 53→47 %, jinak by se „+9,9×10⁻¹⁰" nevešlo), **pod nimi mini trend** (398×100) **a úplně dole RF signál bargraf** (398×54) — jen v pravé části. Tap na Allan → okno ALLAN (s_view=23), tap na trend → fullscreen trend (s_view=9). Horní hrana mřížky MUSÍ zůstat na `SCR_MAIN_GRID_Y` (166) — clear oblast velkého čísla (`redraw_freq`) končí přesně na ní. **Pilulky v headeru**: `UI_DIM_PILL_H=46` (5,4 mm, strop = 4 px před spodní linkou hlavičky) + hit-slop přes výšku headeru (`pt_in_pill`, efektivně ~52 px). ⚠️ **Řadu pilulek limituje `HDR_PILL_LIMIT` (590, sníženo z 640 kvůli dvouřádkovému CPU bloku CM7/CM4 mezi pilulkami a hodinami, x 592..642, `screen_main_redraw_cpu`) + fit-check `hdr_pill_fit`** — rozpočtem je levý okraj CPU bloku resp. clear zón sekundového redrawu času/data (x=644, `screen_main_redraw_time`); pilulka, která se nevejde, se vynechá (pořadí = důležitost: GNSS, SYS, SAT, HDOP, HOLD, **CAL**). **CAL = kompaktní „ribbon" chip** (LED + „CAL", bez hodnoty; `has_led`, `UI_PILL_NORMAL`) — placeholder kalibračního stavu, ~67 px místo dřívější „CAL 4 min" pilulky (~90 px), takže se za HOLD před CPU blok v typickém stavu vejde; je **poslední v pořadí**, takže při přetlaku vypadne jako první (HOLD = živý holdover stav zůstane).

## ⚠️ Funkční konfigurace displeje (nepřepisovat naslepo)
Displej funguje s **DSI BURST mode + RGB565**. Hard-won, jde to PROTI Linux rpi-6.6.y referenci:
- **RGB565 (ne RGB888)** odstranilo statický per-řádek shear (3bajtový RGB888 pixel se v DSI streamu rozjížděl; 2bajtový RGB565 sedí).
- **BURST (ne NB_PULSES/sync-pulse)** dal správné barvy (NB_PULSES způsoboval rotaci R→G→B).
- Linux driver předpokládá pre-init bridge přes Pi GPU firmware — na bare STM32 hostu neplatí.

Pokud displej regreduje (shear / špatné barvy), zkontroluj NEJDŘÍV `dsihost.c`: `VidCfg.Mode==DSI_VID_MODE_BURST` a `ColorCoding==DSI_RGB565`.

**⚠️ I2C4 timing NEMĚNIT ručně.** Panel power + backlight jdou přes ATTINY @ 0x45 na I2C4. Ručně spočítaná 400 kHz hodnota (`0x10903163`) na HW nefungovala → probe panelu selhal → **úplně tmavý displej**, a navíc zasekla ATTINY na sběrnici (drží SDA) → nepomohl reflash, jen **úplný power-cycle desky i panelu**. Funkční hodnota = `0x70303AEE` (~100 kHz). Vyšší rychlost jen přes CubeMX Fast Mode + ověřit SCL osciloskopem.

## Klíčové proměnné

**Tabulky (hodiny/PLL, DSI VidCfg, LTDC, TC358762, framebuffer/MPU, SDRAM mapa) → `docs/HW_REFERENCE.md`.**
Load-bearing minimum: `HSE_VALUE=25000000` (viz ZLATÁ PRAVIDLA), DSI BURST+RGB565 (viz „Funkční
konfigurace displeje" výše), FB0/FB1/FB2 na `0xC0000000`/`0xC0100000`/`0xC0200000`, MPU region 0
= 4 MB Write-Through, SDRAM scratch `0xC0400000`, `.sdram` sekce `0xC0800000`.
- **⚠️ Boot vyčistí FB na černo** (`memset(0xC0000000, 0, 3 MB)` v USER CODE 2, za MX_FMC_Init): SDRAM přežije soft reset → jinak by LTDC krátce zobrazil poslední snímek před restartem.

### Triple buffering / tearing-free (prim_stm32_hal.c) — AKTIVNÍ
- **3 framebuffery** (FB0 `0xC0000000` / FB1 `0xC0100000` / FB2 `0xC0200000`) v MPU region 0 (4 MB WT). Render cílí VŽDY skrytý **back**; `prim_stm32_present()` flipne LTDC na back **při vblanku** (`LTDC->SRCR=LTDC_SRCR_VBR`, NE `HAL_LTDC_SetAddress`=immediate → tearing). **Non-blocking:** čeká na PŘEDCHOZÍ flip, ne na aktuální → při nízké kadenci žádný ~17 ms spin (3. buffer garantuje, že copy-forward nepíše do scanovaného bufferu).
- **Dirty-rect copy-forward:** po flipu se do nového back zkopírují **jen změněné oblasti** (ne 768 KB) — levné. Sledování v DMA2D backendu: každý fill/blit zaznamená svůj obdélník (`mark_dirty`). ⚠️ **Každý partial redraw MUSÍ začít fill/blit (clear)**, jinak se ta oblast nezkopíruje dopředu (problikávání). Triple → nový back je 2 snímky starý → kopíruje se sjednocení dirty z posledních 2 snímků. **Dedup (`copy_forward_dedup`, 2026-08-06):** prev+cur se často shodují/překrývají (freq rect a stat karty jsou v obou seznamech) → před kopírováním se vyhodí **shodné a plně obsažené** obdélníky (`rect_covers`) → žádný redundantní DMA2D blit. ⚠️ Kopírovaná množina zůstává **přesně sjednocením** (keep drží jen původní obdélníky, nikdy větší) → žádné riziko problikávání; **žádný bbox-merge** (ten by kopíroval mimo dirty).
  - **⚠️ `mark_dirty` se volá JEN z `d2d_fill`/`d2d_blit_ex`** (DMA2D cesta) — cokoli kreslené přes `prim_internal_blend_px` (per-pixel alpha blend: `aa_corner` u zaoblených rohů `prim_fill_rect_rounded`, `prim_draw_arc`, `glow`, AA hrany tvarů) **zapisuje přímo do framebufferu a `mark_dirty` OBCHÁZÍ**. Funguje to jen proto, že takový obsah je vždy uvnitř dřívějšího REPLACE clear/blit (jeho dirty rect ho „poveze s sebou" — stejný princip jako u textu, viz DMA2D glyph blend výše). **Past:** tlačítko/pilulka, které mění VARIANTU (a tedy barvu) při partial redrawu **bez** předchozího `blit_bg_region`/`fill_rect(..., REPLACE)`, nechá v rozích (mimo poloměr zaoblení) 2 snímky staré „duchy" — objevené 2026-07-19 u `cas_upd_mode` (okno Čas, tlačítko AUTO CET/CEST, jediné místo v appce měnící variantu bez clearu; opraveno přidáním `prim_fill_rect(rect, BG_CARD, REPLACE)` před `ui_button_render`). Ostatní partial-redraw tlačítka (MUTE/AUTODIM/LANG) mají stejnou mezeru, ale je neviditelná, protože drží stále stejnou variantu → identická barva „ducha" a aktuálního stavu.
- **`present` jen při změně:** `draw_diag_values`/`screen_main_redraw_time` vracejí, zda kreslily; volající flipne jen pak (jinak zbytečný flip).
- Volá `app_gpsdo` po každém vykreslení; UiTask LTDC adresu neřídí.
- **⚠️ Cache koherence:** DMA2D obchází CPU D-cache → po každém fill/blit se zneplatní cílová oblast (`SCB_InvalidateDCache_by_Addr`); WT → bez dirty řádků. Bez toho AA hrany textu čtou stará data („px šum").
- **⚠️ `testRED`/`test` UART příkazy** píšou natvrdo do FB0 — při page-flipu nemusí být vidět (bring-up reziduum).
- ⚠️ **Freeze NEBYL bufferem** — byl to `printf` v SensorsTasku (malý stack); buffer má CPU dopad ~0 % (dirty-rect). Off-screen canvas API odstraněno (nepoužité).

### Akcelerace / linker
- Grafiku dělá `libprim`/`libui` (viz `CM7/GPSDO_UI_README.md`); DMA2D backend je volitelný v libprim.
- **DMA2D glyph blend (velký text bez CPU rasterizace):** `prim_draw_text` umí velké glyfy (`h≥24 px`, neprůhledná barva, neořezané) blendovat přes DMA2D místo CPU per-pixel smyčky. Glyf se JEDNOU expanduje do **A8 dlaždice v `.sdram` atlasu** (256 KB, Device paměť — DMA2D čte přímo jako `bg_cache`), pak už jen HW blend (FG=A8 alfa, barva z `FGCOLR`). Cache klíč = ukazatel na coverage data. **Default VYPNUTO**; zapíná se cíleně přes `prim_set_glyph_accel(1/0)` — teď jen kolem **měřeného kmitočtu** (`screen_main.c`), zbytek textu jede CPU. Srazilo UiTask 85→58 %.
  - ⚠️ **Cache platí jen pro statické `const` fonty** (klíč = adresa coverage dat). Runtime-generované/škálované glyfy by ji rozbily (různé glyfy stejná adresa) → nutná invalidace. Cache nemá eviction (bump alokátor); po naplnění (96 položek / 256 KB) nové glyfy padají na CPU.
  - ⚠️ **Zrychlený glyf NEdělá `mark_dirty`** (jako CPU text) → spoléhá na dirty rect předchozího clearu (fill/blit) kvůli copy-forwardu přes 3 buffery. Každý partial redraw velkého textu MUSÍ začít clear, jinak bliká.
- ITCM/DTCM linker sekce (`.itcm_text`/`.dtcm_data`) + kopírovací smyčka v `main.c` USER CODE 1 byly odstraněny (držel je jen smazaný gfx hot-path). V git historii, kdyby bylo potřeba ITCM zrychlení vrátit.
- Největší CPU výhra zůstává **-O2/Release**.

### FreeRTOS tasky (freertos.c)

**Tabulka tasků / prioritys / stacky → `docs/HW_REFERENCE.md`.** Priority: defaultTask/UartTask/
FpgaTask = Normal, UiTask = BelowNormal, I2C4Task = Low. Heap `configTOTAL_HEAP_SIZE` = 32768 B
(drženo i v `.ioc`). ⚠️ **Tabulku v HW_REFERENCE drž synchronní se skutečnými `.stack_size`** —
audit 2026-08-17 odhalil rozjetí. PRIO_BITS=4, `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`=5.

**Rozdělení souborů (CubeMX-regen-safe).** Tasky jsou vyčleněné z `freertos.c` do
`freertos_task_*.c`, sdílený stav v `freertos_shared.h`:
- `freertos.c` — jen `MX_FREERTOS_Init`, `StartDefaultTask`, definice globálů.
- `freertos_task_uart.c` (`UartTask_run`), `freertos_task_sensors.c` (`SensorsTask_run`),
  `freertos_task_ui.c` (`StartUiTask`), `freertos_task_fpga.c` (`StartFpgaTask`),
  `freertos_hooks.c` (RunTimeStats + stack/malloc hooky).
- ⚠️ **Regen-safe pattern:** `StartUartTask`/`StartI2C4` (= tasky v `.ioc`) zůstávají
  ve `freertos.c` jako **tenké stuby**, jejichž USER CODE tělo jen volá
  `UartTask_run`/`SensorsTask_run` ve split souborech → CubeMX regen NEzpůsobí
  duplicitní symbol. `UiTask`/`FpgaTask` (NEjsou v `.ioc`) mají handle + attributes
  v **USER CODE Variables** a `osThreadNew` v **USER CODE RTOS_THREADS** → regen je nesmaže.
  **Při přidání tasku v CubeMX:** drž se tohoto patternu (stub → `*_run`).

### Periferie
- USART1: 115200 8N1, TX=PB14, RX=PA10, NVIC prio 5. printf přes `_write` (blokující TX, timeout 10 ms ⇒ limit ~115 B/řádek).
- I2C4: TMP117 @ 0x48 (0.0078125 °C/LSB, 1×/s), panel ATTINY @ 0x45 (backlight 200), FT5x06 touch.

## UI vrstva (libprim / libui / app, UiTask)
Grafika je **třívrstvý in-place renderer** (detaily v `CM7/GPSDO_UI_README.md`;
⚠️ `.claude/zadani_*.md` je PŮVODNÍ zadání z 2026-06-19, **neudržované — není to
specifikace dneška**, viz Mapa dokumentů nahoře):
- **`libprim`** — generická 2D primitiva: fill, shapes, path, gradient, glow, text (RGB565). Nezná `ui/*`.
- **`libui`** — vizuální slovník: theme, dimensions, komponenty (card, button, chart, bargraph, big_number, pill, sparkline, digit_group), ikony, fonty. Nezná `app/*`.
- **`app/`** — GPSDO hlavní obrazovka (`screens/screen_main.c`) + HAL most (`hal/stm32/prim_stm32_hal.c`).

Veřejné API: `app_gpsdo_render_main()` / `app_gpsdo_render_diag()` / `app_gpsdo_clear()` / `app_gpsdo_handle_touch()` / `app_gpsdo_tick()`.
- **Kreslí VÝHRADNĚ `UiTask`** (libprim/libui nejsou thread-safe). UART nastaví `g_screen_req` (3 = hlavní obrazovka, 4 = clear), UiTask ho obslouží. Obrazovka startuje automaticky po bootu.
- **Barevný model = RGB565** všude (FB i pipeline). `prim_color_t` je ARGB8888 jen jako pracovní barva v paměti (alfa matematika), na hranici FB se balí do RGB565.
- **Dotek tlačítek:** UiTask polluje FT5x06 (@ I2C4, ~15 Hz, pod `i2c4MutexHandle`), hranové spouštění, volá `app_gpsdo_handle_touch(799-x, 479-y)`. **Panel je zrcadlený v X i Y** (H+V flip). **⚠️ Pokus přesunout touch poll do SensorsTasku (uvolnit UiTask) SELHAL** — při saturaci CPU touch čtení v Low-prio tasku timeoutovalo → `vTaskDelayUntil` free-run → I2C4 33 % + touch nefunkční. Vráceno do UiTask (proven ~4,5 %).
- **FT5x06 MUSÍ číst celý touch frame** (`ft5x06_read_touch`, 31 B z TD_STATUS 0x02). Částečné čtení (jen 1. bod) → controller drží I2C při multitouchi → další transakce zatuhne → zamrznutí při 2. prstu. Parsuje se jen 1. bod, ale čte se celý rámec.
- **⚠️ Boot-priming touche** (`s_touch_primed` v UiTask): FT5x06 NENÍ resetem nulován → při Menu→Restart může uživatel **držet prst na „Ano" přes reset** a první poll po bootu vidí „down" jako hranu → spustí akci na souřadnici „Ano", která se na hlavní obrazovce zrcadlí na **kartu Trend** (= „bootuje do trendu"!). Dokud po bootu nevidíme „prst nahoře" (`t.valid==0`), doteky se **ignorují** (jen se sleduje `was_down`) → reziduální/držený dotek se absorbuje.
- **I2C4 mutex** (`i2c4MutexHandle`): TMP117 task + touch + backlight sdílí sběrnici. **Nepovolovat I2C4 interrupt v IOC.**

### Metrologická vrstva (2026-08-18) — co dělá z čítače přístroj

**Overlapping ADEV + MDEV + HDEV.** Původní `adev_stage` byl **non-overlapping**:
při τ = m·τ0 rozdělil ring na M/m disjunktních bloků a zahodil většinu dostupných
dvojic. Overlapping varianta (Riley, NIST SP1065) vytěží M−2m+1 překrývajících se
členů z **týchž dat** — na dlouhých τ, kde je vzorků nejméně, je to rozdíl mezi
„pár párů" a „řádově víc". Konfidenční pás (`ns`) to zohledňuje a je proto užší.
**MDEV** rozliší bílý a blikavý fázový šum (v ADEV mají stejný sklon), **HDEV**
je díky druhým diferencím imunní vůči lineárnímu driftu. ⚠️ **TDEV je nově
EXAKTNÍ** — definice je τ·MDEV/√3; dosud se počítala z ADEV, což platí jen když
MDEV ≈ ADEV, tedy právě ne u fázového šumu. ⚠️ **MTIE zůstává odhad** a popisek to
přiznává: přesný MTIE potřebuje uloženou fázi (⬅ #36). Přepínač v okně ALLAN má
5 segmentů = 60 px = **přesně projektové minimum** dotykového cíle, šestý se
už nevejde.

**Okno ANALÝZA (s_view=41).** Kam se to vešlo: nikam — footer MĚŘENÍ je plný
a karta má 9 řádků. Řešení: **třetí sourozenec v existující rotaci**
ČÍTAČ → MĚŘENÍ → ANALÝZA → ČÍTAČ; stávající tlačítko jen mění popisek, takže to
nestojí ani jeden nový pixel. Obsah: **rozpočet nejistoty** (rozlišení hradla
√2·tdc/gate + σy@τ + ppb reference, sčítané kvadraticky; zobrazuje se **rozklad**,
ne jen součet), **počet platných číslic** = log10(1/u), **drift Vc** a **tempco
Vc↔T**. ⚠️ U nejistoty je vždy uvedeno **k=2** — u vs U se liší dvojnásobně.
⚠️ U prokladů se vždy ukazuje **korelace r** a při |r| < 0,5 se výsledek označí
za neprůkazný; bez toho by uživatel četl proklad šumu jako měření.
⚠️ **Tempco funguje už dnes, bez FPGA** — teplota OCXO i Vc se logují od začátku.
Datalog se čte **jedním průchodem pro obě osy** + decimace na ~200 bodů (0,2 s
místo 9 s) a **jen při vstupu do okna**. Poslední řádek okna = **ℒ(f) fázový šum**
(#45, viz níže).

**Fázový šum ℒ(f) (#45, `phase_noise.c/h`).** Pure-logic modul (Core): radix-2 FFT
(64 bodů) + Hannovo okno nad ringem frakčních fluktuací `s_y[]` (1/s, tentýž, co plní
Allan) → jednostranné PSD `Sy(f)` → `Sφ(f)=(f0/f)²·Sy` → **`ℒ(f)=10·log10(Sφ/2)` [dBc/Hz]**.
`screen_main_phase_noise(target_hz,…)` vrací ℒ na binu nejbližším offsetu (okno ANALÝZA
ukazuje ~0,1 Hz). ⚠️ **fs≈1 Hz → Nyquist 0,5 Hz → jen NÍZKO-offsetové ℒ(f)** (f≈0,016..0,5 Hz);
vyšší offsety (kHz–MHz) až s gap-free timestampingem z FPGA (#62) — tohle je základ, na kterém
to pak pojede. ⚠️ Dokud headline žene simulace (#2), počítá se ℒ(f) ze simulovaného šumu (stejná
výhrada jako ADEV/drift — mechanika správná, čísla až s reálnými daty). Kryje `pn_selftest`
(15. selftest): FFT korektnost (argmax na binu čistého tónu), PSD normalizace, `(f0/f)²` převod
(f0×2 → +6,02 dB).

**Filtr měření (#5).** PRŮMĚR 8 sníží šum o √N, ale jediný výstřelek rozprostře
do N hodnot; MEDIÁN 9 (liché okno = ostrý) výstřelek zahodí, ale šum nesníží —
proto obojí. ⚠️ **Filtruje se POUZE zobrazovaná hodnota**; do Allan/histogramu/
datalogu jdou dál syrová měření, protože filtrovaná data by σy(τ) uměle vylepšila.

## Vývoj bez FPGA desky — co ho odblokovalo (2026-08-18)

**Emulátor FPGA rámců (`fpga_sim_*`, UART `fpgasim`).** Headline byl do teď náhodná
procházka v `screen_main.c`, která celou cestu z FPGA **obcházela** — netestovaná tak
zůstávala celá naše polovina kontraktu. `fpga_freq_poll()` má přitom ideální injekční bod:
`xfer()` je jediné místo, kde vzniká obsah `rx[]`. Emulátor složí syntetický 64B DATA rámec
**včetně správného CRC** a všechno za tím (MAGIC, CRC, `parse_data`, latch, VALID/FRESH/SEQ,
výběr odbočky) běží beze změny. Odemyká #1 a dovoluje vyrobit, co na stole nezařídíš:
SIGNAL_LOST, skok přes práh 380 MHz, poškozené CRC, díru v `phase_status`.
⚠️ **Nevaliduje** drátovou vrstvu, časování ani logiku FPGA. ⚠️ **Pojistky:** výchozí vypnuto,
nepersistuje se, `DATALOG_F_SIM` (bit 6) zůstane v logu navždy, info řádek začíná `SIM `,
`status` to hlásí. Tempo: nová SEQ jen ~4×/s (reálná FPGA má gate 0,25 s), i když FpgaTask
polluje 20×/s — jinak by emulace vyráběla 20 měření/s a zkreslila vše, co se opírá o tempo.

**Disciplinace LSE podle GPS (`rtc_lse_*`, UART `rtc cal`).** `rtc_try_sync()` každých 10 min
přepsal čas a odchylku **zahodil** — přitom právě ona je měření driftu vlastního krystalu.
Fáze se snímá **na hraně GPS sekundy** (GPS dává jen celé sekundy) přes sub-sekundový registr
RTC (1/256 s = 3,9 ms); drift = Δfáze/Δčas. ⚠️ Latence NMEA je přibližně konstantní, takže se
v **rozdílu** dvou fází vyruší — proto se měří rozdíl, ne absolutní fáze. ⚠️ Vzorkovač běží
**před** 1Hz throttlem `rtc_app_tick` (jinak by se s 1 Hz GPS aliasovalo). Okno 8 min → ~12 ppm
na okno; běžící průměr ~2 ppm po 6 h, ~1 ppm po dni. Korekce jde do **`RTC_CALR`** (smooth
calibration, krok 0,954 ppm) až od 12 oken a nejvýš 1×/h; skládá se (CALR už nějakou drží).
`RTC_CALR` žije v backup doméně → přežije reset bez zvláštní persistence.

**Flight recorder (`flightrec.c`, UART `flightrec`).** Crash black-box říká *co* se stalo,
ne *co se dělo předtím* — proto TODO #18 visel. Kruhový buffer 60 s (CPU, heap, **nejmenší
volný stack**, teploty, I2C chybovost) žije v RAM; do W25Q se sype až při poruše (detekovaný
stall, hook přetečení stacku/malloc, `flightrec test`). ⚠️ **Cílový sektor je předem smazaný** —
při poruše zbývá do IWDG ~1,5 s a erase trvá 50–400 ms, takže by se často nestihl.
⚠️ **Nezapisuje se z HardFault handleru** (`w25q wait_ready` ustupuje scheduleru → v exception
kontextu by zatuhlo).

**Rekonstrukce dlouhých τ ADEV z datalogu.** Restart dosud vynuloval pyramidu. ⚠️ Vzorek z logu
se vkládá od **stage 1**, ne 0: stage 1 má τ = 10 s = přesně kadenci logu, takže převod je
exaktní. Sypat log do stage 0 (τ0 = 1 s) by dalo σy(τ) špatně **o celý řád** a přitom věrohodně.
⚠️ **Trend pyramida se záměrně nerekonstruuje** (decimuje po 4, 10 s na žádnou stage nesedne).
Běží po dávkách 20 záznamů/tik (~2 min na pozadí); přeskakuje `freq == 0` a `DATALOG_F_SIM`.

**SCPI nad IPC snapshotem (`ipc_scpi_src_from_snap`, UART `scpi ipc <cmd>`).** Největší riziko
TCP poloviny #25 není socket, ale jestli snapshot nese vše, co SCPI potřebuje. `scpi ipc X`
pustí tentýž parser nad snapshotem a porovná s `scpi X` → **SHODA / ROZDIL**, ověřitelné bez ETH
i bez flashe bank 2. ⚠️ Zbývá přeložit `scpi.c` do CM4 obrazu (linked resource v `CM4/.project`)
+ řetězcový kanál ve sdílené struktuře.

## Encoder + model fokusu (`encoder.c/h`, `menu_list_t` v app_gpsdo.c) — Fáze A, 2026-08-31

**Rotační encoder je od 2026-08-31 plnohodnotná ovládací cesta**, ne jen diagnostika.
Návrh a zbytek fází → `UI_ENCODER_NAVRH.md`, zadání → `citac_zadani_UI.md`.

🔴 **DVĚ ÚPLNÉ CESTY.** Uživatelský požadavek: *„musí jít ovládání i jen dotykem, pro případ
že encoder nejde."* Platí **obě implikace** — každá funkce dosažitelná encoderem samotným
**i** dotykem samotným. Je to silnější než zadání UI §1 (to žádá jen encoder + dotyk jako
zrychlení). ⚠️ Nikdy nezaveď akci dostupnou jen jednou cestou.

- **`encoder.c/h`** — TIM1 encoder mode (CH1=PA8, CH2=PA9) + tlačítko PC13. Piny si modul
  konfiguruje **sám a idempotentně**, v `.ioc` není nic z toho (regen-safe, stejný vzor jako
  CS pin ve `fpga_freq_init`). `encoder_poll()` volá **VÝHRADNĚ UiTask** (~100 Hz smyčka).
  - ⚠️ **`ENC_COUNTS_PER_DETENT` = 4** (mode 3 počítá obě hrany obou kanálů). Kdyby jedna
    západka dala jiný počet kroků, změň to **TAM a nikde jinde** — zbytek UI pracuje jen se
    západkami. Ověření: UART **`enc`** vypisuje západky, jedna západka musí dát `kroku=1`.
  - ⚠️ **`short_press` chodí SPOLU s `double_click`.** Zdržet každý krátký stisk o 400 ms
    kvůli detekci dvojkliku by udělalo ovládání líné, takže se short hlásí hned. Důsledek:
    dvojklik na hlavní obrazovce nejdřív posune cyklus aktivního parametru a teprve pak
    otevře menu — cyklus je nedestruktivní, takže to nevadí.
- **Model fokusu** (`menu_list_t`, `list_draw`/`list_move`/`list_hit`): rozcestníky MENU (12),
  MĚŘENÍ (44) a NÁSTROJE (48) jsou **seznam zalomený do sloupců**, ne mřížka tlačítek.
  🔴 **Pořadí v poli = pořadí encoderu = vizuální pořadí**, sází se **PO SLOUPCÍCH** (dolů
  prvním, pak dolů druhým) → encoder má jednorozměrné pořadí.
  - **Fokus = výplň `BG_1` + accent rámeček 3 px.** ⚠️ Záměrně odlišné od `UI_BUTTON_ACTIVE`
    (mění jen výplň a znamená *stav*) i od `tap_flash` (2px accent obrys **bez** změny výplně,
    ~150 ms). Kdyby se to sjednotilo, uživatel nepozná zaměření od stavu.
  - ⚠️ **Fokus se nekreslí, dokud `encoder_seen()` nevrátí 1** — při ovládání jen dotykem by
    rámeček mátl. První otočení ho zobrazí, i když se index nezmění.
  - ⚠️ **Tap nastavuje i `s_focus`**, aby encoder pokračoval tam, kde uživatel skončil.
- Výška řádku je parametr layoutu, ne konstanta (MENU 160 px, MĚŘENÍ 76, NÁSTROJE 96).
  **Žádný layout nesmí jít pod 60 px** (projektové minimum dotykového cíle, 7 mm).

- 🔑 **JEDEN SPOJENÝ PROSTOR FOKUSU = položky okna + REGISTR TLAČÍTEK** (sjednoceno 2026-09-01, STATUS #127). Předtím se vylučovaly: okno se seznamem mělo `n = L->n` a registr tlačítek se v něm **vůbec nepoužil**, takže v MENU nešlo zaměřit `RESTART`, `? NAPOVEDA` ani `ZPET`. Pořadí je teď **nejdřív položky okna, pak tlačítka** (`enc_items_n()` / `enc_paint()`). ⚠️ **Registr plní `ui_button_render` I `ui_segmented_render`** — segmentové přepínače do něj do 2026-09-01 nespadly, takže v okně ALLAN byly encoderem nedosažitelné obě záložky i přepínač metriky. **Registruje se každý SEGMENT zvlášť**, protože aktivace jde přes dotyk na střed obdélníku a `ui_segmented_hit` mapuje x na segment. 🔴 **Každý nový typ interaktivního prvku v libui musí dostat pozorovatele registru**, jinak bude dostupný jen dotykem — a to porušuje pravidlo dvou úplných ovládacích cest. `ui_button_render` je
  jediné hrdlo, kterým procházejí **všechna** tlačítka (88 volání v `app_gpsdo.c`, 2 v
  `screen_main.c`), takže libui dostalo pozorovatele (`ui_button_set_observer`) a app si
  z něj staví seznam zaměřitelných obdélníků. **Žádné z ~45 oken tedy svoje tlačítka
  nevyjmenovává** a seznam se nemůže rozejít s tím, co je opravdu na obrazovce.
  Registr se nuluje v `window_prep()`; duplicity z partial redrawu se zahazují podle rectu.
  - ⚠️ **Aktivace jde přes `app_gpsdo_handle_touch()` na střed tlačítka** — obě ovládací
    cesty tím sdílejí tutéž logiku a nemohou se rozejít v chování.
  - ⚠️ **Značka fokusu leží VEDLE tlačítka** (prstenec na rectu zvětšeném o 4 px), ne na něm:
    z registru mám jen obdélník, ne variantu ani popisek, takže tlačítko neumím překreslit.
    Mazání = blit pozadí (`screen_main_bg()`) do čtyř pruhů kolem — kdyby prstenec ležel
    na tlačítku, blit pozadí by ho vygumoval.
- **Paměť fokusu per okno** (`s_focus_of[S_VIEW_MAX]`) — ZPĚT vrátí kurzor na položku,
  ze které se odcházelo (zadání UI §7 „menu si pamatuje poslední volbu").
- **Na hlavní obrazovce encoder zaměřuje tlačítka patky** (RUN/STOP · GATE · CHAN ·
  PERIOD · MENU). Až přijde vstupní modul, nahradí se to cyklem parametrů dle zadání §4.
- ⚠️ **`ENC_DIV_DEFAULT` = 4 je odhad, proto je dělič RUNTIME nastavitelný**: UART
  **`enc div 1|2|4`**, persist v syscfg (magic `"SCFF"` → **`"SCG0"`**, tedy jeden boot
  s výchozím nastavením). Ověření na desce: `enc` a otočit o jednu západku — musí vypsat
  `kroku=1`. Bez přeflashování.

⚠️ **Co Fáze A ZÁMĚRNĚ nedělá:** na hlavní obrazovce encoder zatím nic neladí (dlouhý stisk
= AUTO-TRIGGER je no-op) — cyklus práh A/B, hystereze A/B, hradlo potřebuje vstupní modul
(STATUS #78) a hradlo se navíc do FPGA vůbec nedostane (audit #83).

## UART příkazy (StartUartTask)
`led on/off`, `ram write/read`, `sdram write/read`, `temperature`, `sensors`, `adcraw`, `scanner`, `testDSI`,
`testRED`, `test` (RGB565 sanity), `touch`, `touchloop`, `scan1`, `si5356`, `freq`, `gps`, `gpsraw`, `rtc`, `fpgaraw`,
`fpgaloop`, `stats`, `status`, `ui`, `qspiid`/`qspitest`/`qspispeed`/`storetest` (W25Q),
**`screenshot [sd]`** (export obrazovky do BMP; `screenshot.c`): **`screenshot sd`** = doporučená cesta — snímek se nejdřív zkopíruje do SDRAM scratche (anti-tearing, UiTask jinak během zápisu flipne) a FatFs zapíše celý `SHOTnnn.BMP` na kartu. Holé **`screenshot`** posílá 1,15 MB přes USB CDC (~sekundy, ⚠️ best-effort tok + snímá živý FB → u animované obrazovky pruhy ze dvou framů),
**`autocal`** (self-check referencí/napájení VREF/12V/5V/VBAT → PASS/WARN/FAIL; staged kroky ADC/timebase/RF; `autocal.c`, ROZPRACOVÁNO),
**`membench`** (benchmark všech pamětí — rychlost zápisu/čtení + hledání chybných bitů; ⚠️ destruktivní **jen** pro vyhrazený SDRAM a W25Q scratch, interní FLASH se jen čte — viz sekce „Benchmark pamětí"),
**`sdramlog [dump N|reset]`** (datová cache měření v SDRAM — stav / N nejnovějších vzorků / vynulování; viz sekce „Datová cache měření"),
**`datalog [on|off|erase|dump]`** (záznam stability, viz „Datalog"), **`stacktest yes`** (⚠️ záměrně
přeteče stack UartTasku → IWDG reset; ověření řetězce detekce, viz TODO #10),
**`beep`/`beep test`** (testovací pípnutí, mute platí i pro test — odpověď na to upozorní),
**`beep on`/`beep off`** (globální mute, persist BKP_DR2), **`selftest`** (čistě-logické unit testy
za běhu: CRC16 vektor, hystereze /4↔/16 přes `fpga_freq_select_core` (bezstavové jádro), GPS parser
helpery (`gps_selftest`), fmt_frac+hist_h vektory (`screen_main_selftest`), Maidenhead
(`app_gpsdo_selftest`), kalendář+DST (`rtc_selftest`), datalog záznam+CRC+čas (`datalog_selftest`)
— žádný HW, žádný sdílený
stav; destruktivní testy zvlášť: `qspitest`/`storetest`), **`enc`** (encoder #29: TIM1 v encoder mode na PA8/PA9 + tlačítko PC13, 10 s živého výpisu
kroků a stisků; piny si nastavuje sám, **nic v `.ioc`**. ⚠️ Jen HW vrstva — model fokusu v UI
neexistuje a je to návrhové rozhodnutí, ne kód), **`eth`/`eth clk`** (ETH bring-up F0: bit-bang SMI z CM7 — reset PHY, sken adres 0–31, ID/BMCR/BMSR LAN8742A; `clk` změří REF_CLK na PA1 přes TIM2. Piny si nastavuje sám, **nic v `.ioc`** — viz `ETH_BRINGUP_CHECKLIST.md` §2. Bring-up reziduum jako `fpgaraw`), **`panel`** (zopakuje cely bring-up displeje za behu: ATTINY probe s retry -> power-on -> `HAL_DSI_Start` -> `tc358762_init` -> `HAL_LTDC_Reload` + jas, s vypisem vysledku KAZDEHO kroku). 🔴 **Proc existuje:** selhani bring-upu **neni fatalni** — `main.c` udela `goto display_skip` a pristroj bezi dal s **cernym displejem**, zatimco dotyk, UART i mereni funguji; `[ERR]` hlasky z bring-upu se pritom **nikam nedostanou**, protoze konzole jede po USB CDC, ktere v te chvili jeste neni vyctene. Stav bring-upu je nove i v `status` (radek **`DISPLEJ:`**, globál `g_display_init_step`). ⚠️ Bezi z UartTasku (nehlidany watchdogem) — kroky blokuji stovky ms. **`ping`/`screen main`/`clear`/`version`/`help`**.
`rtc` = RTC čas (`g_rtc_text`) + zda je synchronizovaný z GPS (viz „RTC").
**`status`** = od 2026-07-20 plná diagnostika (dřív jen „RUNNING"): verze + uptime, **příčina resetu
+ crash black-box** (`stall:UiTask`, `stack:UartTask`, …), RSR, heap free/min, CPU %, **volný stack
všech 5 tasků**, **stav CM4** (`CM4: ABSENT/alive/SILENT, stall x<n>` — viz sekce Dvoujádro), **FPGA link
+ počet CRC chyb + jak dávno byla poslední** (`fpga_freq_crc_count/last_age_s`)
a stav datalogu. První místo, kam sáhnout při náhodném restartu. (Reset sám nese „uptime od poslední" =
aktuální uptime — čas od posledního resetu; počet resetů se napříč power-cyklem nedrží, persistence
záměrně nezvolena.)
`temperature` = TMP117 0x48 + příznak `(STALE)` při chybě čtení. `sensors` = dump všech 10 senzorů
(`last/min/max/avg`, stav OK/ERR, `err_total/streak/`**`last`**`/samples`; **`last=<s>`** = uptime od poslední
chyby čtení daného senzoru, `err_last_ms` v `sensor_stat_t`, „-" bez chyby) — viz `g_sensors[]`/`sensor_stat.h`.
Neznámý příkaz → `ERR unknown command`. `ui` i `screen main` znovu vykreslí hlavní obrazovku
(`g_screen_req=3`), `clear` ji smaže (`g_screen_req=4`). Odpovědi protokolu CRLF (`ping`→`pong`,
`version`→`FW_VERSION_FULL`).

## Verze firmware (version.h) — JEDNA definice pro UART i displej
`CM7/Core/Inc/version.h`: `FW_VERSION_FULL` = `"gpsdo-ui vX.Y.Z"` (SemVer). Používá ho **UART `version`**
i **displej** (okno „O přístroji" + boot splash) → dřív se lišily (UART `v0.2-diag` vs displej `v0.1`),
teď jsou identické. ⚠️ **Verzuj numericky KONZISTENTNĚ s Git:** každé zvýšení = commit + `git tag vX.Y.Z`
na tomtéž commitu (verze na displeji přesně = git tag → dohledatelnost buildu). PATCH=oprava, MINOR=feature, MAJOR=zlom.

## FPGA čítač kmitočtu (fpga_freq.c/h, FpgaTask, SPI2)
STM32 = SPI master, FPGA = slave. **SPI2**: master, mode 0, MSB, 8-bit, MOSI=PB15, SCK=PI1,
MISO=PI2, **CS=PB12 (manuál GPIO, active-low)**. Bring-up **1 MHz** (`fpga_freq_init` zvolí
prescaler dle `HAL_RCCEx_GetPeriphCLKFreq(SPI123)`). **SCK strop dle kontraktu FPGA (GW1NR-9 oversampling): cíl ≤6 MHz, absolutní max ~10 MHz** — `FPGA_SCK_TARGET_HZ` (hlídá `#error` na `FPGA_SCK_MAX_HZ`).
- **Timing (kontrakt):** CS setup/hold ≥1 µs (dáváme 2), **mezi rámci ≥20 µs** (FPGA potřebuje ~124 cyklů @10 MHz na složení rámce; dáváme 25). Prodlevy přes **DWT cyklový čítač** (`delay_us`, ne NOP-loop — DWT už zapnut pro runtime staty). Po bootu **`osDelay(250)` v StartFpgaTask** než se začne clockovat (FPGA piny 54–57 jsou config piny, musí dokončit load z flash).
- **CRC self-test (akceptační krok 1):** `fpga_freq_crc_selftest()` ověří `crc16("123456789")==0x29B1`; při selhání se `g_init_ok=0` a SPI komunikace se **nezahájí** (poll/restart hned vrací false).
- **Stav SPI/komunikace na displeji:** `fpga_freq_format_status()` skládá řádek `SPI <x.xx>MHZ LINK:OK/-- SEQ:<n> CRC:<n>` (rychlost SCK, živost linky, posl. SEQ, počet CRC chyb). FpgaTask ho po každém pollu uloží do `g_spi_text`/`g_spi_ok` (překreslí jen při změně), UiTask vykreslí stav SPI při překreslení hlavní obrazovky — **zeleně** když link žije, **červeně** když ne.
- **Pevný 64B full-duplex rámec**: MAGIC 0xA5, VERSION, TYPE, FLAGS/STATUS, SEQUENCE(LE32),
  PAYLOAD_LEN, RESERVED, 50B payload, CRC16(LE) na konci. CRC = **CRC-16/CCITT-FALSE** (0x1021/0xFFFF), pokrývá byte 0..61.
- Model: `FpgaTask` polluje **~20 Hz** (`osDelay(50)`), posílá **ACK** (TYPE 0x06, SEQUENCE=poslední) → FPGA full-duplex vrací aktuální **DATA** (TYPE 0x80). Platné = CRC ok + DATA_VALID + DATA_FRESH + nová SEQUENCE. Polling je úmyslně rychlejší než tempo měření (FPGA gate **0,25 s reciproké → ~4 nová měření/s**) kvůli nízké latenci; protokol je pull/ACK, takže rychlejší polling nezpůsobí ztrátu měření (FPGA shodí DATA_FRESH až po ACK té SEQ). Každé čerstvé měření FpgaTask naformátuje do `g_freq_text`/`g_freq_info`. **`xfer()` je pod SPI mutexem** (`s_spi_mtx` v driveru) — UART `fpgaraw`/`fpgaloop` jinak kolidoval s pollingem FpgaTasku (dva tasky na jednom SPI+CS).
- **Dvě odbočky JEDNOHO děliče (4-fázové reciproké měření):** **pin28 = /4** (primár, víc hran/nižší latence), **pin27 = /16** (rozšíření rozsahu), čtou se současně. ⚠️ **`/4` i `/16` jsou dva Q výstupy TÉHOŽ binárního čítače `MC100EP016A`** (Q1=÷4, Q3=÷16; front-end viz `../Frequency_Counter_FPGA_Module/FPGA_module_schematic.pdf` list 2) — tvarovač = `MAX9601` komparátor, strop řetězce **~1,4 GHz**. `fpga_freq_select()` volí zobrazovaný zdroj: **/4 dokud je bez chyby a < ~380 MHz, jinak /16** — s **hysterezí** (nahoru 380 MHz, zpět na /4 až pod 360 MHz; sticky stav → žádné přeblikávání zdrojů u prahu; volat jen z FpgaTasku). Rozsah: na pinu do ~100 MHz → reálně ~400 MHz (/4) / až ~1,4 GHz (/16, limit tvarovače). Headline ukazuje zvolený zdroj, info řádek `<src> PH:<present>/<fine> GATE:<ns>NS SEQ:<n>[ chyba]`.
  - ⚠️ **`/16` NENÍ nezávislý cross-check** (dřívější zavádějící popis): sdílí tvarovač i společný čítač s `/4`, takže společné chyby (výpadek tvarovače u 1,4 GHz, miscount čítače) se projeví v OBOU shodně → porovnání `freq_x100000` vs `freq16_x100000` je jen **downstream sanity** (chytne rozbité čítání/timestamp na jednom pinu ve FPGA, ne front-end). Nesoudělný poměr je s binárními odbočkami nedosažitelný; skutečně nezávislá kontrola by chtěla druhý samostatný dělič (HW). Dvě odbočky slouží hlavně **rozsahu + hladkému handoveru**, ne validaci.
- **Detekce ztráty signálu (SIGNAL_LOST):** FPGA má **autoritativní watchdog** — ~2,5 s bez dokončeného měření → `error_flags` bit1 SIGNAL_LOST + DATA_VALID=0. FpgaTask čte `fpga_freq_signal_lost()` (latch posledního DATA rámce, funguje i při VALID=0) a při ztrátě **nebo mrtvém linku** nastaví `g_freq_stale=1` → UiTask ztlumí kmitočet na **šedou** (čte `g_freq_text`/`g_freq_stale` při překreslení). **Dřívější SEQ-staleness heuristika ODSTRANĚNA** (falešně hlásila stale u nízkých kmitočtů, kde se reciproké okno legitimně protáhne) — teď se věří FPGA flagu.
- **Auto-re-START:** když ~3 s nepřijde žádný platný rámec (`fpga_freq_link_ok()`==0, tj. mrtvý link, ne jen „bez nového měření"), FpgaTask znovu pošle START (20 Hz polling → práh `fails>=60`) — pokrývá FPGA který nabootuje/resetuje až po STM32.
- DATA payload (`fpga_meas_t`, parse v `parse_data()`): `frequency_x100000`(abs12, /4), `edge_count`(20), `gate_time_ns`(28), `timestamp`(36), `channel`(44), `measurement_status`(45), `error_flags`(46), **`phase_status`(50)**, **`status2`(51)**, **`freq16_x100000`(52, /16)**. **`freq*_x100000` = reálný kmitočet × 1e5 (děličku /4 i /16 už zahrnuje FPGA → STM NEnásobí 4 ani 16); `gate_ns` ≈ 250e6 a kolísá.**
  - **`error_flags`:** bit0=`FPGA_ERR_MEAS` (/4 Δt==0), bit1=`FPGA_ERR_SIGNAL_LOST`, bit2=`FPGA_ERR_OVERFLOW` (okno >~21,5 s). **`status2`** bit0=`FPGA_ST2_DIV16_ERR` (/16 Δt==0).
  - **`phase_status`:** bity3:0=present[3:0] (živost 4 fází), bity7:4=fine_seen[3:0] (viděné jemné 2,5 ns kódy). **Zdravé = `PH:F/F`** (obě nibble plné). Mezera v fine_seen → chybějící/špatně posunutá fáze (kontrola 90° rozestupu). ⚠️ SEQUENCE roste pomaleji u nízkých f (okno čeká na hrany) — to NENÍ chyba, skutečnou ztrátu hlásí SIGNAL_LOST.
- **⚠️ V IOC/main.h je PB12 pod starým názvem `SPI2_RCK`** (pozůstatek po 74HC595; PB4 = `SPI2_RES`, nepoužitý). CS pin si `fpga_freq_init` konfiguruje **sám** (push-pull, idle high) — regen-safe, nezávisí na gpio.c.
- **⚠️ CS boot level MUSÍ být HIGH:** `gpio.c` ručně upraven — `MX_GPIO_Init` budí PB12 na **`GPIO_PIN_SET`** (bylo RESET). Jinak STM drží CS asertovaný (LOW) od bootu až do `fpga_freq_init` (po scheduleru), tj. **během konfigurace FPGA z flash** → GW1NR-9 nemusí naběhnout, MISO mlčí (`RX0:FF`). **Při regeneraci z IOC nastav PB12 default Output Level = High.**
- **⚠️ SCK idle LOW (AFCNTR):** `fpga_freq_init` nastavuje `hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE` (CFG2.AFCNTR=1). Bez toho STM mezi přenosy uvolní SCK/MOSI piny → SCK plave, pull-up na FPGA ho táhne HIGH → FPGA vidí při CS↓ falešnou hranu, rozhodí počítání bitů → `RX0:FF`. (Projev na LA: „initial state of CLK does not match settings".) Regen-safe (v driveru, ne v `spi.c`).
- **Bring-up diagnostika:** status řádek při chybějícím linku ukáže `SPI <x.xx>MHZ NOLINK HAL:<OK|ERR> RX0:<hex> CRC:<n>` (HAL=stav přenosu, RX0=první bajt MISO). UART `fpgaraw` vypíše HAL stav + všech 64 přijatých bajtů. `RX0:FF`/samé FF = FPGA nebudí MISO (CS/SCK/MISO zapojení, zem, nebo FPGA neběží).
- Formát: `123.456.789,01234Hz` (tečky tisíce, čárka des., 5 míst, bez mezery před Hz). UART příkaz `freq` vypíše poslední hodnotu; diagnostika ukazuje `g_spi_text` + `g_freq_info`.
- **⚠️ HEADLINE = REÁLNÁ DATA (#1, 2026-08-25, KÓD HOTOVÝ, NEOVĚŘENO NA HW).** Velké číslo + **všechny statistiky** (Allan, drift, offset, Math, spektrogram, trend) čtou jediný hinge `s_freq_n` v `screen_main.c`, který nově žene **reálný/emulovaný kmitočet z FpgaTasku** (`g_freq_x100000`/`g_freq_seq`/`g_freq_valid`, plní je FpgaTask vedle `g_freq_text`). `freq_advance()` volí zdroj: platné měření → REAL; bez měření (mrtvý link, `fpgasim` off) → **SIM fallback** `freq_step()` s viditelným **„SIM" markerem** u čísla. **Emulovaná data (`fpgasim`) jdou reálnou cestou driveru** → berou se jako REAL (bez markeru) — proto se celý řetězec dá testovat bez FPGA desky. **Dynamický formát Hz–GHz**: `num_build_for()`/`num_layout()` staví segmenty/separátory/geometrii podle magnitudy měření (rebuild jen při změně počtu celých číslic). Statická předloha `SCR_MAIN_DIGITS`/`_SEPS` **odstraněna**.
  - **⚠️ FREQ ↔ PERIOD toggle (footer slot 0, 2026-08-29):** `s_freq_n` = **VŽDY frekvence** (v LSB=10^-`s_freq_frac` Hz) — čte ho statistika, SIM walk (`freq_step`), `screen_main_freq_hz()`, Math. Zobrazované číslo je **`s_disp_n`** (`freq_fill_segments` čte jeho): v FREQ režimu `= s_freq_n`, v PERIOD `= 1/f` přepočtená do **s / ms / us / ns / ps** (`disp_update()`, jednotka `s_disp_unit` + převodní `s_disp_unit_s` dle magnitudy, `s_num.unit`). `num_build_for` je dvouprůchodový: (1) frekvenční stav, (2) `num_layout` pro FREQ nebo PERIOD digits. Toggle nastaví `s_disp_recalc=1` → `freq_advance` vynutí rebuild z poslední známé frekvence + `s_freq_fmt_changed` (plný redraw). **Perioda je JEN displej** — nemá hi-res dopočet (1/f, ~7 platných cifer), statistika/Allan zůstávají frekvenční. **⚠️ Jednotka se kreslí přes `s_num.unit_font` = `ui_font_sans_32`, což je SUBSETOVANÝ font** — dřív obsahoval jen `"Hz"`, takže „ns"/„us"/… se vykreslily jako NIC. Charset rozšířen na `Hzsmunp` (`CM7/libui/tools/font_gen/gen_fonts.js` ř. 47) + `ui_font_sans_32.c` přegenerován; **při další regeneraci fontů ten charset nesmí spadnout zpět na `'Hz'`** (kontrola: `grep glyph_count ui_font_sans_32.c` musí být 7, ne 2).
  - 🔴 **Počet číslic: headline se NEPOČÍTÁ ze zaokrouhleného `frequency_x100000`** (to nese jen 5 desetin), ale **z reciproké dvojice `edge_count`/`gate_time_ns`** (`freq_frame_to_lsb`, „hi-res"): reciproký čítač měří `f = N/Δt`, takže se podíl dá spočítat na **7 desetin** a ty číslice navíc jsou **SKUTEČNÉ** — ověřeno: posun hradla o 123 ns změní `10000000,0000000` → `9999995,0800024`. (Leží pod šumem — rozlišení TDC 2,5 ns / 0,25 s okno ≈ 0,1 Hz — a **právě proto se poslední místa kreslí ztlumeně**.) Při 10 MHz to dělá **15 číslic** (8+7), stejně jako původní statická předloha.
    - ⚠️ **`edge_count` má rámec JEN pro pin28 (/4)** — u větve /16 (nad ~380 MHz) hi-res dopočet **nejde** a formát se poctivě srazí na 5 desetin z `x1e5` (`g_freq_hires` = 0). Přepnutí /4↔/16 proto **přestavuje formát** (jinak by se dokreslovaly nuly, které měření nenese).
    - ⚠️ **Počítá se DLOUHÝM DĚLENÍM** (celá část + číslice po jedné), NE `num × 10^frac / den` — to by při 7 desetinách přeteklo uint64 už kolem 10 MHz (2,5e22 ≫ 1,8e19). `num = edges × 4 × 1e9` je bezpečné (nejhůř ~8,6e18 při 21,5 s okně), přesto hlídané stropem.
    - **Kolik desetin podle zdroje** (`max_frac` v `num_build_for`): **hi-res /4 = 7**, **`x1e5` /16 = 5**, **SIM základ = 6** (`FREQ_FRAC_SIM` — o jedno velké desetinné místo víc než x1e5, takže před dvěma malými nejistými jsou **čtyři velké**; fabrikace to není, SIM hodnotu generuje `freq_step()` a číslo nese marker „SIM"). Základ 10 MHz v SIM: `10.000.000,000 0̲sf` = 14 číslic, 715 px.
    - Šířkový guard `FREQ_MAX_W` = **780 px** (zóna leží svisle mezi hlavičkou a mřížkou → vodorovně je volná celá obrazovka; rozpočet 45 px/číslici mono_75). Změřeno: 10 MHz = 773 px (8+7), 400 MHz = 773 px (9+6) → **15 číslic**; 1,4 GHz (/16) = 743 px (10+4). ⚠️ Dřívějších 720 px zbytečně ubíralo desetinu už kolem 400 MHz.
  - **Typografie velkého čísla** (zadání 2026-08-26): celá část = trojice oddělené **tečkou**; za desetinnou čárkou taky **trojice, ale oddělené MEZEROU** (SI styl — po čárce se už žádná tečka nekreslí, aby nebylo pochyb, co je desetinný oddělovač; `mono_25` má prázdný glyf mezery, advance 15, žádný ink). **Poslední důvěryhodná číslice = modré podtržení** (`UI_COLOR_ACC` = `#38BDF8`, samostatný segment). **Poslední 2 číslice menším fontem** (`fade_font` = mono_52) **a v odstínech šedi** (`ui_level_color`: `INK` → `INK_4` SIGMA → `INK_5` FLOOR). Výsledek při 10 MHz: `10.000.000,000 00̲s f` = **15 číslic**, 775 px.
  - ⚠️ **`UI_BIGNUM_SEP_NONE` (`'~'`) = „žádný separátor"** (`big_number.c` `sep_at`). Nutné, protože `separators` je C řetězec — `'\0'` uprostřed by ho utnul a všechny další mezery by zůstaly bez separátoru. Díky sentinelu jde střídat: trojice oddělené mezerou, ale **uvnitř** trojice číslice slepené i tam, kde se dělí segment kvůli změně fontu/podtržení.
  - ⚠️ **Kapacita segmentů `NUM_SEG_MAX` = 12** (dřív 8): dynamický formát dělí jemněji (až 4 skupiny celé části + až 5 zlomku — trojice + osamocená podtržená číslice + 2 nejisté = 9 v nejhorším případě).
  - ⚠️ **Převod `freq_x100000_to_lsb()` DĚLÍ, nenásobí**: `x100000 × 10^frac / 1e5` při ~4 GHz **přeteče uint64** (4e19 > 1,8e19). Dělení `10^(5−frac)` je exaktní a bez přetečení.
  - ⚠️ **Amplituda SIM fallbacku se počítá z `s_freq_frac`** (~0,05 Hz), ne pevně v LSB — jinak by při 5 desetinách (dřív 7) kmitala ±5 Hz a rozhýbala i celou část.

  **Kadence statistiky = seq-driven** (`app_gpsdo_tick_stats_sample` vzorkuje jen na nové `g_freq_seq`; jinak by 1Hz tik nafoukl σy — jako web); **přechod REAL↔SIM i změna magnitudy resetují Allan/trend pyramidu** (nemíchat nekompatibilní vzorky). ⚠️ Plný redraw zóny (změna formátu / REAL↔SIM) **musí nejdřív naplnit číslice i shadow** — jinak se o snímek déle drží stará hodnota. 🔴 **`screen_main_redraw_freq_area()` čistí SJEDNOCENÍ s předchozí zónou** — číslo je vycentrované, takže při změně formátu se mění i jeho levý okraj; bez toho by po stranách zůstali „duchové" starých číslic (typicky i stará jednotka `Hz`), protože partial redraw už do té oblasti nikdy nesáhne. **Jednotka `Hz` se kreslí vždy** (`ui_big_number_render_tail` ji přidává na konec každého partial redrawu). ⚠️ **`s_freq_center`/`s_freq_nominal_hz` už NEjsou fixně 10 MHz** — `screen_main_freq_hz()` = `s_freq_n / 10^frac` (nezávislé na centru); pod 1 Hz je centrum rovno naměřené hodnotě (nulové by SIM stahovalo k nule). ⚠️ **Kolik číslic je nejistých už NENÍ natvrdo 2 (#51, 2026-08-28):** `freq_uncertain_frac()` odvozuje počet ztlumených desetin z **rozlišení hradla reciprokého čítače** (√2·tdc/gate, `FREQ_TDC_PS`=2500 — deterministické, NE simulace) → delší hradlo = víc důvěryhodných cifer. **SIM fallback (`gate_ns`==0) dává 2** (nezměněný vzhled), REAL/emulátor počítá z `gate_ns` rámce; sanitace na [1, frac−1] (aspoň 1 s modrým podtržením + aspoň 1 nejistá). Fade fontem se kreslí víc/míň desetin podle skutečné rozlišovací meze. ⚠️ **τ0 pyramidy pořád předpokládá ~1 s** (plně správný τ0=skutečný rozestup = MathTask #27). Test: `fpgasim on <hz>` → headline; `fpgasim on 32768`/`1400000000` → přeformátování; `fpgasim fault lost` → šedá; `fpgasim off` → SIM marker.
- **Signal bargraf = REÁLNÝ** (už ne simulace): RF vstupní výkon z **AD8307** log-detektoru přes ADS1115 **AIN1** (SensorsTask fast-path ~10 Hz). `app_gpsdo_tick_signal` převádí mV→dBm (`dBm = mV/AD8307_SLOPE_MV_DB + AD8307_INTERCEPT_DBM`, typ. 25 mV/dB, intercept −84 dBm), bargraf mapuje pásmo `RF_DBM_MIN..MAX` (−80..+10 dBm), text „−45.5 dBm". ⚠️ slope/intercept jsou datasheet-typické → přesná **kalibrace do CALIB store** (viz [[w25q-flash]]).

## 🟢 NOVÁ REVIZE DESKY (zadání 2026-08-30) — co se změní a co tím padá

> **Zdroj:** `../citac_zadani_predavaci.md` (předávací zadání: vstupní modul + firmware FPGA).
> **Nic z toho zatím neběží** — deska FPGA je hotová a schéma zafixované, vstupní modul se
> teprve navrhuje, firmware FPGA se teprve píše. Tahle sekce říká, **co z dnešního kódu
> tím přestane platit**, aby se to nedědilo dál. Smluvní tabulky (J3, I²C mapa, pinout
> Tang Nano) → `docs/HW_REFERENCE.md`. Úkoly → `../STATUS.md`.

**Cíl přístroje:** dvoukanálový reciproční čítač, priorita **rozlišení a absolutní přesnost**,
ne maximální kmitočet. Přímo **DC–200 MHz** (`SY100ELT23L`), přes ÷10 (`MC12080`) do 1,1 GHz
jen frekvence. Single-shot TI ~22 ps (carry chain, bin ~50 ps). Trigger level ±1,024 V
s krokem 0,50 mV (`MCP4728`), hystereze 1–60 mV.

### Co z dnešního kódu tím PADÁ

🔴 **POTVRZENO uživatelem 2026-08-30: „deska nemá /4 a /16, jsou dva symetrické vstupy."**
Celá dnešní logika výběru zdroje tedy míří na hardware, který **neexistuje**. Není to živá
chyba — SPI link nikdy nenaběhl (STATUS #2, `RX0:FF`), takže ten kód se nikdy neprovedl proti
reálnému rámci; je to **neprověřená mrtvá větev**, ne regrese.

| Dnes | Proč padá |
|---|---|
| **`fpga_freq_select()` + hystereze /4↔/16** (380/360 MHz), **`fpga_freq_select_core()`** | dvě odbočky JEDNOHO čítače `MC100EP016A` **na desce nejsou**; jsou **dva symetrické kanály** `CH_A`/`CH_B` |
| **`fpga_freq_select_selftest()` = selftest #1** | testuje tu hysterezi → při odstranění klesne `SELFTEST_N` (dnes 16) a musí se srovnat `NAMES[]` i `_Static_assert` v okně Selftest |
| **`freq16_x100000`, `FPGA_ST2_DIV16_ERR`** | není co přepínat |
| **`g_freq_hires` pravidlo „`edge_count` má rámec JEN pro /4"** | symetrické kanály → hi-res dopočet má smysl pro **oba** |
| **`fpga_freq_hires_mul()` kandidáti `{1, 4, 16}`** | nově `{1, 10}` (přímá cesta / `MC12080` ÷10). ⚠️ Funkce násobitel **ověřuje**, ne předpokládá, takže na nové desce nespočítá nesmysl — jen vrátí 0 a **tiše degraduje na 5 desetin**. Seznam kandidátů je proto potřeba doplnit, jinak přijdeme o hi-res rozlišení, aniž by to něco ohlásilo. |
| **`phase_status` (`PH:F/F`, present/fine_seen)** | 4fázový vernier ze `Si5356` (0/90/180/270°) nahrazuje **carry-chain TDC**; jeden `REF_100MHz` |
| **Pravidlo „fáze MUSÍ být 90°, ne 45°"** | tamtéž — Si5356 zůstává jako reference + `CAL_SRC`, ne jako TDC vernier |
| **Rozsah „~400 MHz (/4) / 1,4 GHz (/16)"** | nově 200 MHz přímo / 1,1 GHz přes ÷10 |
| **Poznámka „0x4A NENÍ osazený, NACK je očekávaný"** | 0x4A = `TMP117` **ve vstupním modulu**, objeví se s modulem |
| **20 Hz polling ve `FpgaTask`** | nově `Data_RDY` — viz níže |

✅ **Okno „Dvojkanál" (s_view=46) se tím stává správné.** Dnes ukazuje `CH A` = ÷4
a `CH B` = ÷16 pod těmi popisky (přejmenováno 2026-08-30); s novou deskou to budou
skutečně dva **nezávislé symetrické kanály**, což ty popisky konečně odpovídají realitě.
⚠️ RF bargraf v něm ale dnes plní **jedna** hodnota do obou karet (jeden AD8307 na desce FPGA)
— modul má `RF_Level_A` **i** `RF_Level_B` na `ADS1115` @0x4B, takže každá karta dostane vlastní.

⚠️ **Dokud nová deska neběží, dnešní kód platí a nesahat na něj.** Tabulka je seznam toho,
co při přechodu odstranit, ne co je rozbité.

### ✅ SPI zůstává na 4 vodičích — `Data_RDY` se NEPŘIDÁVÁ (rozhodnuto 2026-08-30)

`MISO`/`MOSI`/`SCK`/`CS` stačí; pátý vodič se záměrně nepřidává. Zadání §6 sice polling
zakazuje, ale jeho argument je o **rušení**, ne o propustnosti (*„každý burst vstřikuje
rušení do časovacích vstupů asynchronně k měření"*), a ten se dá vyřešit bez drátu:

🔑 **Ptej se úrovní `MISO` jako GPIO mezi přenosy.** `IDR` ukazuje skutečnou úroveň pinu
i v režimu alternativní funkce → `HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_2)` funguje bez zásahu
do SPI2. FPGA drží při deasertovaném CS na MISO příznak „mám data", při CS↓ pin přepne
na posuvný registr. **Dotaz je elektricky tichý** (žádné hodiny, žádná změna CS), takže
rušení vzniká jen při skutečném čtení dat — což je nevyhnutelné tak jako tak.
⚠️ Vyžaduje, aby FPGA budila MISO i při CS vysoko (proti zvyku SPI slave = Hi-Z). Tady
to nevadí — sběrnice je dvoubodová (§6: „Jen Tang Nano na SPI1, žádná další zařízení").

Proč `Data_RDY` pro **maximum vzorků** stejně nic nepřinese: trik „externí signál → DMA"
má smysl na mnoho malých přenosů s nízkou latencí. Při vyčítání PSRAM se spustí **jeden
dlouhý DMA** a CPU je volné celou dobu tak jako tak. Handshake je navíc **in-band**
(STATUS/FLAGS + `SEQUENCE`) — protokol byl takhle navržen právě proto, aby extra GPIO
nepotřeboval.

✅ **Důsledek: CS zůstává ruční GPIO na PB12**, hardwarový NSS není potřeba → zlaté pravidlo
„PB12 HIGH během config loadu GW1NR-9" platí triviálně dál, beze změny kódu.

⚠️ **Co tím ztrácíme:** 32bitové razítko každého bloku z `TIM2->CCR1` (diagnostika: kadence
FPGA, vlastní latence, detekce zameškaného bloku). Kdyby se rušení někdy ukázalo jako
**měřitelný** problém, hotová analýza záložní varianty (proč `Data_RDY` patří právě na
`TIM2_CH1`/`PA15` a proč to EXTI neumí) je v `docs/HW_REFERENCE.md`.

### Nová zařízení na I2C1, která firmware neobsluhuje

🔴 **POTVRZENO 2026-08-30: I²C masterem je STM32.** Všechny pasti níže tedy padají
**na naši stranu**, ne na firmware FPGA (zadání je má pod „§6 firmware FPGA", ale sběrnice
vede z I2C1 přes desku FPGA na J4 do modulu a masterem je STM32 — jako dnes pro Si5356 a TMP117).

`MCP23017` ×2 (0x20/0x21, **19 relé**), `MCP4728` (0x60, práh + hystereze),
`ADS1115` (0x4B, `RF_Level_A/B` + monitor ±5 V), `AD5693R` (0x4C, **ladicí napětí OCXO**).
Pasti ze zadání §6:
- **`MCP4728` kanál C zapiš PRVNÍ** — bez `V_MID` = 0,512 V vyjde práh −1,024 V místo nuly.
  (Tovární EEPROM = nuly → práh 0 V, hystereze max = bezpečná strana ✓)
- **Nespínej relé naráz** — cívka `G6K-2F-Y` bere 40 mA, osm relé = 320 mA a na FFC 20 cm
  to je ~320 mV úbytku. Rozděl je do skupin po pár ms.
- **Po přepnutí relé počkej ≥50 ms** — vazební 10n s 1 MΩ dá 10 ms, relé samo 3–5 ms.
  To je desetkrát víc než všechno ostatní.
- **`RF_Level` se měří v modulu** (přes konektor neteče žádný analog) → dnešní `SENS_ADS1`
  (AD8307 na AIN1 desky FPGA) dostane sourozence na 0x4B.
- **Trigger level znamená na každé cestě něco jiného:** 1 MΩ + lineární DC OZ = absolutní
  volty; 50 Ω přes ERA (AC vázané) = relativně ke střední hodnotě; **přes ÷10 = bez významu
  → nastav na střed a NEZOBRAZUJ.** To je přímý požadavek na UI.
- **Kalibrační tabulka je MATICE**, ne jedno číslo — pro každou kombinaci cesty, útlumu
  a amplitudy vlastní koeficient (dispersion `MAX9601` je až 40 ps a mění se s overdrive).
  ⚠️ Dnešní CALIB blob má 20 B a strop 4080 B/blob (1 sektor) → **matice se tam nemusí vejít**,
  viz „W25Q region mapa".
- **Kalibraci ukládej s hashem bitstreamu** — po rekompilaci FPGA může být offset jiný
  a firmware to musí poznat.

### 🔴 NOVÉ: smyčku GPSDO nově drží STM32 (potvrzeno 2026-08-30)

**`AD5693R` @0x4C = řídicí DAC ladicího napětí OCXO a regulační smyčku drží STM32.**
To je **nový podsystém**, ne úprava stávajícího — dnes přístroj GPSDO jenom *zobrazuje*,
nic nedisciplinuje (kromě krystalu LSE, viz `rtc_lse_*`).

Co k tomu bude potřeba a co už máme:
- **Zdroj fázové odchylky:** FPGA porovnává OCXO proti `GPS_1PPS` (PIN33, carry chain C)
  → do protokolu přidat `time_error_ns` (STATUS #36). **Bez toho smyčka nemá vstup.**
- **Výstup:** 16bitový DAC po I2C1. ⚠️ Zápis patří k **jednomu vlastníkovi sběrnice** —
  I2C1 dnes obsluhuje SensorsTask pod `i2c1MutexHandle`; regulátor běží ≤1 Hz, takže
  se nabízí buď SensorsTask, nebo defaultTask s mutexem. **Ne z UiTasku.**
- **Zpětná vazba:** OCXO Vc už měříme na `ADS AIN0` (`SENS_ADS0`) → čtení zpět toho,
  co jsme zapsali (kontrola, ne regulace).
- 🔴 **Poslední dobrá hodnota Vc MUSÍ přežít power-cycle** a nastavit se **brzy po bootu**.
  Jinak každý start rozjede disciplinaci od nuly (u OCXO hodiny ustalování).
  ⚠️ Ověř v datasheetu `AD5693R`, jestli je power-on reset na **zero-scale, nebo midscale** —
  podle toho, jak moc OCXO při startu skočí, než regulátor stihne zasáhnout.
  Kam to uložit: syscfg blob ve W25Q (nový magic) — BKP power-cycle nepřežije.
- **Už hotové stavební kameny:** `warmup_ready()` (uptime ≥300 s **a** |dT/dt| < 0,08 °C/min),
  okno Holdover (s_view=16), disciplinace LSE z GPS (`rtc_lse_*`) jako vzor struktury
  (fáze na hraně GPS sekundy, běžící průměr, korekce nejvýš 1×/h). ⚠️ Jsou to **dvě
  nezávislé smyčky** s různým cílem — nemíchat.

### Otevřené otázky (NEROZHODNUTO — nezakládat na tom kód)

1. **Protokol: v2, nebo rozšíření dnešního 64B rámce?** Nové jsou dva kanály, akumulátory
   (N, Σx, Σx², Σy, Σxy), bulk vyčítání PSRAM a `time_error_ns` pro smyčku GPSDO.
   Návrh existuje: `FPGA_PROTOCOL_V2_NAVRH.md`.
2. **Zisk lineárního DC zesilovače** (§5 zadání sám přiznává, že blok chybí) — je svázaný
   s rozsahem prahu a pracovním bodem JFET sledovače, navrhuje se jako trojice najednou.
   Blokuje návrh modulu, ne náš firmware.

## FPGA strana protokolu (specifikace)

**Kompletní specifikace (64B rámec, TYPE, STATUS/FLAGS bity, DATA payload tabulka s offsety,
škálování, model ACK/FRESH, formát kmitočtu) → `docs/HW_REFERENCE.md`.** Handoff/bring-up →
`FPGA_SPI_HANDOFF.md`, `FPGA_INSTANCE_BRIEF.md`; protokol v2 → `FPGA_PROTOCOL_V2_NAVRH.md`.

Nejčastěji potřebné: MAGIC 0xA5, CRC-16/CCITT-FALSE přes byte 0..61, DATA=TYPE 0x80, měření platné
= CRC ∧ DATA_VALID ∧ DATA_FRESH ∧ nová SEQUENCE. `freq_x100000` / `freq16_x100000` = kmitočet × 1e5
s **už zahrnutou děličkou** (STM NEnásobí). SPI mode 0, MSB, 8-bit, ≤6 MHz cíl / ~10 MHz max,
mezi rámci ≥20 µs. FPGA piny PIN54=MOSI, PIN57=MISO, PIN55=SCK, PIN56=CS.

## RTC (rtc.c/h, LSE 32.768 kHz na PC14/PC15) — disciplinovaný z GPS
RTC běží z **LSE krystalu 32.768 kHz** (PC14=OSC32_IN, PC15=OSC32_OUT), prescalery **127/255 → 1 Hz**, clock source **LSE** (ne LSI). Zapnuto **v CubeMX/.ioc** (RTC na CM7 kontextu) → `MX_RTC_Init` + HAL RTC driver vygenerované; LSE přidané v `SystemClock_Config`. Viz `CUBEMX_CHECKLIST.md` „RTC".
- **App vrstva je regen-safe v `rtc.c`/`rtc.h` USER CODE blocích** (jako `MX_I2C1_Init` v i2c.c — žádný nový soubor). `rtc_app_tick()` (telo v `USER CODE 1`) se volá z **defaultTask** (vedle GPS drainu), throttle **1 Hz** uvnitř.
- **Sync z GPS:** při platném a „sane" GPS fixu (`gps_get`, rok 2024–2099 atd.) srovná RTC z UTC — **první fix hned, pak re-sync každých 10 min** (`RTC_RESYNC_MS`, drift LSE ~ppm). Přesnost = přesnost GPS UTC, mezi syncy drží LSE.
- **Perzistence přes reset:** po syncu zapíše `RTC_SYNC_MAGIC` (0x32F2) do **BKP_DR0**; guard v `MX_RTC_Init` (`USER CODE Check_RTC_BKUP`) při tom magicu **přeskočí** defaultní `SetTime/SetDate 0:00` → RTC drží správný čas přes warm reset (dokud žije backup domain). ⚠️ Bez VBAT baterie přežije jen reset (NRST/SW/WDG), ne plný power-cycle.
- **BKP registry:** **DR0** = RTC sync magic; **DR1** = UI config (mode/chan/gate/run, magic `RTC_UICFG_MAGIC`, save `rtc_save_uicfg_if_dirty`); **DR2** = systémové nastavení (bity7:0 jas, bit8 mute, bit9 auto-dim en, bity10:15 auto-dim prodleva /15 s; magic `RTC_SYSCFG_MAGIC`); **DR3–DR5** = crash black-box (`RTC_CRASH_MAGIC`, hook → kind+jméno tasku, po přečtení smazáno); **DR6** = nastavení 2 (**bit0** = barevné schéma nejnižší bit + **bity9:10** = vyšší dva bity → `g_theme_idx` 0..4 = TMAVÉ/SVĚTLÉ/STŘEDNÍ/OBRYS/KONTRAST; staré záznamy mají bity9:10=0 → 0/1 = tmavé/světlé, zpětně kompat.; bit1 english, **bity2:6 časová zóna** kódovaná tz+13 → 1..27 = −12..+14 h, 0 = legacy záznam → UTC; bit7 AUTO CET/CEST, bit8 animace; `RTC_SYSCFG2_MAGIC`). Save `rtc_save_syscfg_if_dirty` (DR2+DR6). Načtení všech v `MX_RTC_Init` (před schedulerem), zápis výhradně defaultTask (kromě crash hooků).
- **Vlákno:** VEŠKERÝ přístup k RTC registrům je **výhradně z defaultTask** (`rtc_app_tick`). UART/UI čtou jen sdílené `g_rtc_text` ("YYYY-MM-DD HH:MM:SS") / `g_rtc_synced` (1=sync z GPS) — žádná cross-task HAL_RTC kolize. `g_rtc_text`/`g_rtc_synced` definované ve `freertos.c`, extern ve `freertos_shared.h`.
- **Zobrazení:** UART příkaz **`rtc`** (čas + sync stav). **Hlavní obrazovka** (header, `screen_main_redraw_time` + `render_header` date) čte RTC přes helper `rtc_time_date()` — **hodiny tikají plynule 1×/s i při ztrátě fixu** (dřív GPS-direct → mezi RMC stály a bez fixu zamrzly). Před prvním GPS syncem `--:--:--` / `no GPS`; v GPS okně nesynchronizovaný čas **ztlumený** (`UI_COLOR_INK_3`). GNSS/SAT pilulky zůstávají z GPS (odráží fix). **Diagnostika** (karta „System / RTOS / RTC") ukazuje RTC čas `HH:MM:SS` (ztlumený `no GPS` dokud nesrovnán).
- **Časová zóna (okno „Cas", s_view=22, dlaždice v Menu):** RTC běží VŽDY v UTC; zóna je jen zobrazovací posun. Dva režimy: **AUTO CET/CEST** (`g_tz_auto`, EU pravidlo letního času — CEST od poslední neděle března 01:00 UTC do poslední neděle října 01:00 UTC; `rtc_cest_active` = čistá funkce v rtc.c, Sakamoto den v týdnu) nebo **ruční posun** `g_tz_offset_h` −12..+14 h. `rtc_app_tick` z UTC odvozuje **`g_rtc_text_local`** (vč. přehoupnutí data, `rtc_apply_tz` + `rtc_month_days`) a **`g_tz_label`** („UTC"/„UTC+2"/„CET"/„CEST"). Okno Cas: živý UTC + lokální čas (~2×/s), tlačítko AUTO↔RUČNÍ, −/+ (v AUTO režimu první stisk −/+ přepne na ruční naseto z právě platného CET/CEST). **Lokální čas zobrazuje: hlavní obrazovka** (`rtc_time_date` čte local; label zóny na řádku data — change-key složený datum+label, clear fixní šířkou 150 px kvůli zkrácení labelu) **a screensaver**. **UTC zůstává: GPS okno, diagnostika, UART `rtc`.** Persist DR6: bity2:6 ruční posun, bit7 AUTO (viz výše). Kalendářní matematika + DST hranice kryté selftestem **`rtc_selftest`** (6. test).
- ⚠️ **defaultTask stack 256→384 words** kvůli `snprintf` v `rtc_app_tick` (historie: formátování v malém tasku už jednou přeteklo stack).
- ⚠️ **Při čtení RTC vždy `HAL_RTC_GetTime` PŘED `HAL_RTC_GetDate`** (čtení TR odemkne shadow registry, jinak se DR zasekne). **NEpovoluj RTC NVIC** (Alarm/WakeUp) v IOC — jen kalendář.

## Beeper (beeper.c/h) — PH9, 800 Hz + alarm vrstva (alarm.c/h)
Pasivní beeper na **PH9** (pin95). Tón **800 Hz** generuje **TIM7** přerušením @1600 Hz
(`HAL_TIM_PeriodElapsedCallback` v main.c → `beeper_isr_toggle()` přepíná PH9). `beeper_init`
(GPIO+TIM7+NVIC) voláno v main.c USER CODE 2. TIM7_IRQHandler v stm32h7xx_it.c USER CODE.
**Nepoužívej TIM7 jinde / nepovoluj v IOC.**
- **`beeper_tone(freq_hz)`** = libovolný tón (přepíše TIM7 ARR = 1 MHz/(2·freq)); `beeper_set(true)` resetuje ARR na 800 Hz (alarm). **`beeper_boot_melody()`** = vzestupný C-dur arpeggio G5→C6→E6→G6 („power-on" jingle ~0,5 s, blokující `osDelay`); volá UiTask jednou při startu, **jen když není mute** (respektuje `g_sound_muted` z BKP). Watchdog grace (8 s) blokující melodii kryje.
- **`alarm.c/h` = jediný volající `beeper_set()`.** `alarm_tick()` (defaultTask ~100 Hz) hlídá
  **hrany** tří stavů: FPGA `g_freq_stale` (SIGNAL_LOST/mrtvý link), GPS lock (`gps_get`:
  valid ∨ fix_mode≥2) a **limit pass/fail** (`g_meas_verdict`, #44 — jen když limity i alarm
  zapnuté). Ztráta signálu = 3 pípnutí, ztráta GPS locku = 2, **limit FAIL = 4**, obnovení = 1;
  počítadlo `g_alarm_limit_fail`. **Start tichý** (guardy `s_*_ever` — první link-up/první fix
  nepípne, bench bez antény taky ne; limit se armuje jen skutečným PASS, takže zapnutí limitu na
  už špatné hodnotě nepípne).
  Pattern neblokující (HAL_GetTick fáze), vyhodnocení stavů jen 5×/s (gps_get kopíruje ~200 B
  v kritické sekci), časování pípnutí 100×/s. **Mute** = `g_sound_muted` (okno Nastavení /
  UART `beep off`) umlčí okamžitě i test; prev-stavy se při mute dál aktualizují (po odmutení
  žádné pípnutí na starou hranu). UART: `beep`/`beep test`, `beep on`, `beep off`.
  **Od 2026-08-17 navíc `mon_eval()` = prahový monitor** (VBAT / OCXO pásmo / σy@1s — viz sekce
  níže): pattern **3× 200 ms** (pomalejší a delší než FPGA 80 ms a GPS 120 ms — „něco se pomalu
  kazí", ne „právě se ztratil signál"), počítadla `g_alarm_vbat/ocxo/adev`.

## Boot POST diagnostika (bootled.c/h) — LED_1 (PG3) + pípání (PH9)
Každý sledovaný init při startu si zapíše pořadové číslo (`bootled_step`); při zaseknutí
v `Error_Handler()` (nebo při selhání bring-upu panelu) **LED_1 blikne N× a SOUČASNĚ N× pípne**
(800 Hz, 150 ms svit+tón / 150 ms tma+ticho). Happy path = jen zápis do proměnné, žádné zdržení.
- **⚠️ Pípání NEPOUŽÍVÁ `beeper.c`.** Ten generuje tón přes **TIM7 IRQ**, jenže `bootled_fail()` běží
  z `Error_Handler()`, kde jsou **přerušení vypnutá** (přesně proto tenhle modul používá i **DWT**
  místo `HAL_Delay`) → TIM7 by nikdy netikl. Tón se proto **bit-banguje přímo na PH9** stejným DWT
  časováním (`BOOT_BEEP_HALF_US` 625 µs = 800 Hz, shodně s `beeper.c`). GPIO init je idempotentní,
  nezávislý na tom, jestli `beeper_init`/`MX_GPIO_Init` už proběhly.
- **Mute (`g_sound_muted`) se ZÁMĚRNĚ neuplatňuje** — není to UX zvuk, ale hlášení poruchy; při raném
  `Error_Handler` navíc nemusí být BKP s nastavením ještě načtená.
- Důvod: když selže panel, LED je jediný výstup — a v krabičce nemusí být vidět. Pípání projde i zavřeným přístrojem.
- ⚠️ Při selhání *před* `SystemClock_Config` je `SystemCoreClock` ještě default → délky (a tedy i výška
  tónu) budou mimo; vzor blikání/pípání zůstává čitelný. Platí to i pro původní blikání.

## IWDG watchdog (watchdog.c/h) — ~4 s, heartbeat UiTask+FpgaTask
**IWDG1** (LSI ~32 kHz, /64, reload 2000 → ~4 s), **registrová implementace** (KR/PR/RLR) —
`HAL_IWDG_MODULE_ENABLED` je v hal_conf VYPNUTÝ, modul je nezávislý a regen-safe.
- `watchdog_init()` v main.c USER CODE 2 (těsně před schedulerem). ⚠️ **Sekvence dle RM0399:
  nejdřív START (0xCCCC)** — ten HW zapne LSI; teprve pak UNLOCK+PR+RLR+wait SR+RELOAD
  (bez běžícího LSI se PR/RLR update nikdy nepotvrdí — stejné pořadí jako HAL_IWDG_Init).
- **Heartbeat model:** `watchdog_kick_ui()`/`watchdog_kick_fpga()` na začátku smyček UiTask/
  FpgaTask; `watchdog_supervise()` (defaultTask ~100 Hz) obnoví IWDG **jen když oba heartbeaty
  < 2,5 s staré** → zatuhnutí jednoho tasku (ne jen celého scheduleru) = HW reset. Startup
  grace 8 s. **UartTask se nemonitoruje** (legitimně blokuje: `scanner` ~2,5 s, `fpgaloop` ~3 s).
- V DEBUG buildu `__HAL_DBGMCU_FREEZE_IWDG1()` (breakpoint neresetuje). Release bez freeze.
- **⚠️ Diagnostika „kdo se zasekl" (2026-07-20):** `watchdog_supervise` při odmítnutí refreshe zapíše
  do crash black-boxu (BKP_DR3..5, **kind 3**) jméno tasku se starým heartbeatem → po restartu
  `g_crash_text` = **`stall:UiTask` / `stall:FpgaTask` / `stall:BOTH`**. Bez toho byl prostý IWDG reset
  němý (RSR řekl jen „watchdog", ne který task). Zapisuje se **jen jednou za běh** (`s_stall_logged`).
- **⚠️ Blokující operace v taskech s heartbeatem ukrajují z 2,5 s limitu.** Nejhorší třída jsou
  **QSPI erase/write**: `w25q.c wait_ready()` čekal až 1000 ms (sector erase 50–400 ms) v **čistém
  spinu**. Volá ho defaultTask (syscfg auto-save, datalog) i UiTask (`calib_save`) — a protože
  defaultTask je Normal a UiTask BelowNormal, erase v defaultTasku UiTask **úplně vyhladověl**
  (heartbeat stárl) a defaultTask se sám nedostal na `watchdog_supervise`. Od 2026-07-20 `wait_ready`
  **ustupuje scheduleru** (`osDelay(1)` mezi dotazy, jen když scheduler běží). **Pravidlo: žádný spin
  delší než ~10 ms v defaultTask/UiTask/FpgaTask** — buď `osDelay`, nebo to přesuň do UartTasku
  (ten se nemonitoruje).
- **⚠️ IWDG2 = nezávislý watchdog CM4** (`CM4/Core/Src/iwdg2.c`, zrcadlo IWDG1): stejné parametry
  (~4 s, registrová sekvence, `HAL_IWDG` vypnutý). CM4 je bare-metal → **žádný heartbeat, prostý
  `iwdg2_kick()`** v každé iteraci hlavní smyčky; `iwdg2_init()` **až po beep melodii** (blokuje
  ~1,2 s), aby startup nespotřeboval timeout. Zaseknutá CM4 smyčka → IWDG2 reset CM4. DEBUG freeze =
  `__HAL_DBGMCU_FREEZE2_IWDG2()` (APB4FZ2). **🔴 ZMĚŘENO 2026-08-13: reset scope IWDG2 je SYSTEM-WIDE,
  ne per-core → IWDG2 je VYPNUTÝ.** Ověřeno: dočasná CM4, která přestala krmit watchdog, shodila
  **celý přístroj** (uptime CM7 cykloval po ~24 s). Zapnutý IWDG2 by tedy nechal zaseknutou CM4 (dnes
  dělá téměř nic) shodit i displej a měření — horší než CM4 samotná. **Zaseknutí se neztratí:** CM7 ho
  vidí přes heartbeat, loguje `stall:CM4` + `g_cm4_stall_count` (UART `status`). Až na CM4 poběží
  ETH/SCPI, dá se přidat cílený restart CM4 z CM7 (to už není nezávislý watchdog).

## Dvoujádro / IPC (CM7 ↔ CM4) — `ipc_shared.h`, `ipc.c`, `CM4/…/ipc_cm4.c`
✅ **CM4 BĚŽÍ A IPC ROUND-TRIP FUNGUJE — ověřeno HW 2026-08-14** (`status` → `CM4: alive (IPC heartbeat)`).
Option bytes správně z výroby (`BCM4=1`, `BOOT_CM4_ADD0=0x810`). Návrh `NAVRH_ARCHITEKTURA_CM7_CM4.md` §11,
bring-up `DUALCORE_BRINGUP_CHECKLIST.md`. **Plné původní znění této sekce → `docs/CLAUDE_ARCHIV.md` §8.**

- 🔴🔴 **Testuj CM4 VÝHRADNĚ po čistém power-cyklu bez ladicí sondy.** Debugger rozbíjí boot handshake
  (HSEM/`D2CKRDY`) → `g_cm4_absent=1` → „4:off" (vypadá jako „nebootuje", ale CM4 je OK) + **falešné
  HardFaulty** (`HF@24000000`, `HFSR=0x80000000` DEBUGEVT). Po flashi: Terminate debug → Remove All
  Breakpoints → **úplný power-cycle** (ne NRST).
- **Sdílená paměť = SRAM4 / D3 `0x38000000`, 64 KB**, shodná adresa pro obě jádra, magic „IPC1"+verze+size.
  ⚠️ **MPU region 2 na CM7 = NON-CACHEABLE + SHAREABLE** (`main.c MPU_Config`). CM4 nemá D-cache ani MPU
  pro SRAM4 → ordering visí **jen na `__DMB()`**.
- **Snapshot CM7→CM4** (seqlock, `seq` liché = zápis): `ipc_publish` **event-driven** (nové `seq_meas`
  NEBO ≥2 Hz heartbeat), **JEN reálná data** (`fpga_freq_get_last`/`gps_get`/`g_sensors`/`g_calib`/health).
  ⚠️ **sigma/offset/drift ZÁMĚRNĚ neplněné** dokud headline = simulace (#2) — na CM4/webu se nesmí
  servírovat jako měření. Nese: teploty OCXO/deska/MCU/FPGA, napětí 12V/5V/VREF/VBAT/Vc, RF mV + AD8307
  kalibrace, Si5356, kanál, `sens_valid` maska, Math/limit cfg mirror, `ui_cfg` (brána/kanál/RUN),
  ETH stav, alarmy/prahy/selftest, `gps_sats[24]`, datalog transfer kanál `ipc_datalog_xfer_t`.
- **`IPC_VERSION` = 13.** ⚠️⚠️ **Změna → přeflashnout OBĚ banky.**
  - **⚠️ Nesoulad bank je NEVIDITELNÝ — `4:--` to NENÍ.** CM4 při neshodě přestane přijímat snapshot
    (`s_ready=0`), ale heartbeat volá dál → header svítí `4:xx%` jako by bylo vše OK (jediný příznak:
    LED_2 nereaguje na GPS fix). **`cm4_ipc_version`** (razítkuje se v heartbeatu, přežije reset CM7
    + `memset` v `ipc_init`) to zviditelní: System Health `CM4:IPCv<x>!=<y>` + `status` → `⚠ IPC NESOULAD`.
    ⚠️ Detekce funguje **jen dokud se nemění layout PŘED `cm4` blokem** (`snap`/`cmd`/`resp`).
  - **Předletová pojistka:** `scripts/build.sh` varuje, když je obraz starší než `ipc_shared.h`
    (= „přeložil jsem jen jedno jádro").
  - 🔴🔴 **`ipc_init()` na CM7 dělá `memset` CELÉ sdílené struktury — VČETNĚ bloku `cm4`, který
    vlastní CM4. To NENÍ benigní** (komentář v `ipc_cm4.c` to tak dřív tvrdil). Volá se až ze
    `StartDefaultTask`, tedy po pomalé inicializaci displeje (~sekundy), zatímco CM4 (bare-metal)
    publikuje už ~1,3 s po bootu (hned po pípací melodii). Kdo vyhraje, závisí na náběhu →
    **jednorázový zápis do bloku `cm4` se může TIŠE ZTRATIT a už se nikdy nevrátí.**
    ⚠️ Přesně to se stalo při HW průchodu 2026-08-30 (studený start): `memset` dopadl **mezi**
    publikaci httpd a eth, takže `status` hlásil `SCPI(CM4): jeste nedobehl` +
    `HTTP(CM4): jeste nedobehl`, ale `ETH(CM4): init OK` — v pořadí, v jakém to CM4 zapisuje.
    Vypadá to jako „selftest se nespustil", přitom proběhl a prošel.
    **Pravidlo: každá hodnota v bloku `cm4` musí být buď publikovaná OPAKOVANĚ ze smyčky
    (jako `ipc_cm4_set_eth`/`_set_net`), nebo držená lokálně a razítkovaná znovu v
    `ipc_cm4_heartbeat` (jako `cm4_ipc_version`, `s_scpi_ok`, `s_httpd_ok`).**
    Jednorázový zápis do `g_ipc.cm4.*` je vždy chyba.
- **`sens_valid`:** maska platnosti ve snapshotu, **bitové pozice = `SCPI_V_*`** → CM4 backend
  `src->valid = snap.sens_valid`, bit za bit jako CM7 na USB. Hlídá **14 `_Static_assert`** v `ipc.c`
  (+ 8 pro `SCPI_CFG_*` vs `IPC_CFG_*`). ⚠️ Neplatná hodnota se **publikuje vždy** (poslední dobrá pro
  trendy), ale **bez bitu se nesmí servírovat jako měření** (jinak `MEAS:VOLT?` = `9.91E37` na USB vs
  nula na TCP = dvě pravdy o jednom přístroji). Seqlock/ring helpery jsou parametrizované ukazatelem
  → `ipc_selftest` běží nad lokální instancí (žádný race s publisherem).
- **CM4 konzument** (`CM4/Core/Src/ipc_cm4.c`): `ipc_cm4_read` (seqlock, **bounded retry ≤8**,
  **per-read kontrola magicu** kvůli neseqlocknutému memsetu v `ipc_init`), **`ipc_cm4_cm7_alive(now_ms)`**
  (sleduje růst `seq` → **CM4 nedůvěřuje starým datům při zamrzlém CM7**), `ipc_cm4_heartbeat` (živost → CM7).
  LED_2 svítí při GPS fixu ze snapshotu (jen když CM7 žije) = důkaz round-tripu.
  ⚠️ `ipc_shared.h` sdílen **RELATIVNÍM include** `"../../../CM7/Core/Inc/ipc_shared.h"` (regen-safe).
- **CM7 čte CM4 heartbeat:** `ipc_cm4_alive()` → `g_cm4_alive`, `ipc_cm4_cpu_pct()` → **CPU blok headeru**:
  **`4:xx%`** = živý + reálná zátěž CM4 (měří si ji sám přes DWT, publikuje v heartbeatu; dnes ~0 %) /
  `4:--` (D2 ready, IPC ticho) / `4:off` (nenabootoval). Hrana alive→dead → log `stall:CM4` +
  `g_cm4_stall_count`. ⚠️ **CM7 se kvůli mrtvému CM4 NERESETUJE a NESAHÁ na crash black-box**; CM4 se
  zotaví vlastním IWDG2.
- **Boot gate** (`main.c` Boot_Mode_Sequence_1/2, HSEM + `RCC_FLAG_D2CKRDY`): timeout → `g_cm4_absent=1`,
  **NEspadne do Error_Handler** (degradovaný běh). VTOR z auto-remapu → boot řídí **option bytes**.
- **D2 SRAM split** (linkery, regen-safe): **SRAM1 128K → CM7** (`RAM_D2 @0x30000000`), **SRAM2 128K → CM4**
  (`RAM @0x10020000`), **SRAM3 32K → ETH DMA** (`ETH_D2 @0x30040000`, sekce `.eth_dma`). Do ETH byly disjunktní.
  - ⚠️ **CM4 `RAM` = 128K, nesmí sahat přes SRAM3** — jinak by tam linker dal `.bss`/`.data` a **tiše přepsal
    ETH deskriptory** (stejná fyzická paměť, jiná adresa). CM4 zabírá ~15 KB flash / ~4 KB RAM (bez ETH).
  - ⚠️ **`ETH_D2` je SYSTÉMOVÁ adresa `0x30040000`, ne CM4 alias `0x10040000`** (ETH DMA = AHB master, vidí
    D2 na `0x30xxxxxx`). Kontrola: `nm H757_LED_CM4.elf | grep DscrTab` → musí být **`30040000 B`** (jinak
    orphan v `.data`). CM4 nemá D-cache → žádná cache maintenance kolem deskriptorů.
- **Ethernet + lwIP na CM4 (F5 — KÓD HOTOVÝ, NEOVĚŘENO NA HW).** `NO_SYS=1` (bare-metal raw API) + DHCP.
  lwIP zaveden **ručně** z `STM32Cube_FW_H7_V1.13.0` (v `.ioc` LWIP není): `Middlewares/Third_Party/LwIP`
  + `Drivers/BSP/Components/lan8742`; glue `CM4/LWIP/Target/ethernetif.c` + `CM4/LWIP/App/lwip_app.c`
  (jméno ne `lwip.c` kvůli budoucí CubeMX regeneraci). Detaily → `ETH_BRINGUP_CHECKLIST.md`.
  - 🔴🔴 **`LWIP_RAM_HEAP_POINTER` NEDEFINOVAT** (viz ZLATÁ PRAVIDLA). Kontrola: `nm ... | grep ram_heap`
    → musí být `1002xxxx`, ne `3000xxxx`. Cena: `.bss` CM4 +14 kB.
  - ⚠️ **`ethernetif.c` NESMÍ duplikovat `eth.c`** (deskriptory/`EthHandle`/`HAL_ETH_MspInit` z ST příkladu
    odstraněné) → `eth.c` zůstane netknutý regenerací. MAC z **`heth.Init.MACAddr`**, ne `ETH_MAC_ADDR*`.
    **`ETH_RX_BUFFER_SIZE` = `heth.Init.RxBuffLen` (1536)** (ST příklad má 1000 → DMA za konec). Žádná
    cache maintenance (CM4 nemá D-cache — proto ETH patří na CM4).
  - ⚠️ **Smyčka CM4 = rychlá + pomalá část:** `lwip_app_process()` + `iwdg2_kick()` každou iteraci (~1 ms),
    IPC snapshot + heartbeat + ETH stav na 5 Hz. DHCP start/stop přes **link callback**, ne natvrdo.
  - **Paměť:** RAM 30 KB/128 KB (SRAM2), `.eth_dma` 12,7 KB/32 KB (SRAM3), CM4 obraz ~84,6 KB.
  - ⚠️ **Statická IP zatím NEJDE** — okno SÍŤ ji ukládá do syscfg, ale **IPC snapshot ta pole nenese**. Vždy DHCP.
- **cmd ring (CM4→CM7) + config sync:** `IPC_CMD_CFG_SET` (key `IPC_CFG_*` + `arg`/`double argd`) → CM7
  `ipc_service` aplikuje Math/limity na `g_meas_cfg` (`ipc_cfg_apply`, commit jen při reálné změně).
  Čtení zpět = **cfg mirror ve snapshotu** → CM4 obslouží `CALC:` readbacky bez `g_meas_cfg`.
- **SCPI je DATA-SOURCE nezávislé** (`scpi.c/h`): parser čte z `scpi_src_t` (instrument-state + validity
  `SCPI_V_*` + akce `set_cfg`/`read_log`), NE z globálů. **CM7 backend** `scpi_src_load_cm7` (`#if CORE_CM7`)
  plní z `g_sensors`/`gps_get`/`fpga_freq`/`g_calib`/`g_meas_cfg`/datalog; `scpi_process` = wrapper (USB beze změny).
  - **SCPI/web na CM4** (plán W0–W5, `WEB_UI_PLAN.md` + git): `scpi.c` + `meas_math.c` se **linkují i do
    CM4 obrazu** (explicitní `subdir.mk`, `-I CM7/Core/Inc`) a **běží tam skutečně** — CM4 pustí
    `scpi_selftest()`/`httpd_min_selftest()` za bootu, výsledek přes IPC do UART `status`.
    ⚠️ **Na CM4 vrací `DISPlay:*` a `SYST:DATE/TIME` SCPI-99 `-241 "Hardware missing"`** (`#else` mimo
    `#if CORE_CM7` — displej/RTC jsou jen na CM7). Bez té větve build CM4 spadl.
    - **Sdílený `ipc_scpi.c` (OBĚ jádra):** `ipc_scpi_src_from_snap()` + `ipc_scpi_set_cfg()`. ⚠️ **Žádné
      `osDelay`/`HAL_Delay`** (jádrově neutrální).
    - **TCP 5025 (`scpi_tcp.c`)** raw lwIP, pool 4 spojení. ⚠️ `scpi_src_t` se plní **živě pro každý příkaz**.
    - **HTTP port 80 (`httpd_min.c`)** vlastní HTTP/1.1 (ne vendorovaný `httpd`/`fs.c`). ⚠️ **`GET /api/state`
      staví JSON přes `scpi_src_t`, NE přímo ze snapshotu** → validita (`SCPI_V_*`→`null`) je táž logika
      jako SCPI. Čísla bez `%f` přes `fmt_scpi_hz_d`.
    - **`web_ctrl_en` (W0):** `src.set_cfg = web_ctrl_en ? ipc_scpi_set_cfg : NULL` → zakázané ovládání
      spadne do existující NULL-guard ochrany parseru (`-230`). **Basic Auth** (`web_user`/`web_pass`):
      `POST /api/scpi` **přijme každý** dotaz, ale **ZÁPIS** vyžaduje `web_ctrl_en` **A** platné jméno/heslo (prázdné heslo nikdy) — `src.set_cfg` se jinak nechá `NULL` a řízení spadne do NULL-guardu parseru. 🔴 **Ověřeno na HW 2026-09-02** (STATUS #129): `*IDN?` i `SENS:FREQ:GATE?` projdou bez autorizace, `SENS:FREQ:GATE 10` je odmítnut a hodnota se nezmění. ⚠️ Dřív tu stálo, že endpoint jako celek vyžaduje autorizaci — nevyžaduje, čtení je otevřené. ⚠️ **TCP 5025
      Auth nemá** (VISA raw socket nezná HTTP hlavičky) → spoléhá jen na `web_ctrl_en`.
    - 🔴 **SPA (`SPA_HTML[]` v `.rodata`) — pravidla pro editaci** (musí zůstat C řetězcový literál bez build
      kroku): **žádná `"` ani `\` uvnitř** → v JS **žádné regexy** (oddělovač tisíců/`trim` ručně); atributy
      z `innerHTML` **bez uvozovek** → víchodnotový stav přes `data-` atributy + CSS selektor; interaktivita
      `addEventListener`+`data-`, ne `onclick=`. ⚠️ **Barvy křivek = CSS proměnné `--c0..--c3` TŘÍDOU**
      (`class='ln s0'`), ne `stroke=` (jinak se při změně palety nepřebarví). **`spec(kind)` = jeden zdroj
      pravdy** grafu. 🔴 **tělo JSON/SCPI vždy v connection-owned `c->bodybuf`**, NE ve sdíleném scratchi.
    - 🔴🔴 **POVINNÝ OVĚŘOVACÍ ŘETĚZEC PO KAŽDÉ EDITACI SPA — `python tools/spa/check.py --build`.**
      SPA nemá build krok, takže *nic* z toho nechytne překladač sám. Jeden spouštěč existuje proto,
      aby se řetězec nedal provést jen napůl. **Pořadí je věcné, ne libovolné:**
      0. 🔑 **`extract_test.py` — funguje vůbec měřítko?** Extraktor je to, čím se posuzuje všechno
         ostatní; když lže on, „ověřeno" nic neznamená. **Přesně tím SPA třikrát prošla jako v pořádku,
         když v pořádku nebyla.** 🔴 **Nález 2026-09-05: původní extraktor (regex `"(...)"`) NEODSTRAŇOVAL
         C komentáře** — uvozovka v `/* … */` *mimo* literál je pro překladač nic (komentář zmizí dřív,
         než se parsují literály), ale regex ji vzal jako začátek řetězce a od té chvíle posunul páry.
         Nahrazen **stavovým skenerem** (řetězec / blokový / řádkový komentář / escapy), který dělá
         totéž co preprocesor. Ověřeno na 10 syntetických případech vč. `/*` a `//` *uvnitř* literálu
         (CSS komentář, URL), které se komentáře splést nesmí.
      1. `find_odd.py` — řádek s lichým počtem `"` = rozbitý literál.
      2. `ascii_clean.py` — uklidí ne-ASCII **i escapované `\"`** uvnitř literálu.
      3. `spa_size.py <c> dump <html>` — extrahuje HTML; **zapamatuj si počet bajtů**.
      4. `node --check` nad `<script>` částí (`tools/node-v20.18.1-win-x64/node.exe`).
      5. **`dom_check.py`** — JS sahající na **neexistující `id`**, duplicitní `id`, rozbité kotvy,
         nevyvážené `<div>`. ⚠️ `$('neexistuje')` je v prohlížeči **TypeError a od té chvíle se
         přestane kreslit VŠECHNO za tím** — `node --check` ani překlad C to neodhalí.
      6. JS testy: `spa_test` `hov_test` `unc_test` `pwr_test` `mdev_test` `pn_test` `alarm_test`
         `pref_test` `axis_test` (běží nad **vyextrahovaným** JS, ne nad kopií — vytahují si funkce
         ze zdroje přes `grab()`, takže se nemůžou rozejít s tím, co se doopravdy servíruje).
         🔴 **Když test spadne, podezřívej NEJDŘÍV test** — v této session bylo **pět** selhání a
         **ani jedno nebyla chyba v kódu** (STATUS #156/#165). Dvě opakující se příčiny:
         (a) harness nemá plný scope, a protože testovaná funkce je v `try/catch`, selže **tiše**;
         (b) test měří jinou vlastnost, než si myslí (sklon místo rozptylu, mantisa přes `round`).
      7. `./scripts/build.sh Release CM4`.
      8. 🔑 **`arm-none-eabi-nm --print-size --radix=d CM4/Release/H757_LED_CM4.elf` u `SPA_HTML`
         musí dát přesně číslo z kroku 3 + 1** (NUL). **Tohle je jediná kontrola, která utnutý literál
         chytí definitivně**, protože měří, co se doopravdy slinkovalo — ne mezistav. (Utnutí se stalo
         **třikrát**: STATUS #151/#153.) Referenční hodnota 2026-09-05: 114 770 → `nm` 114 771 ✓.
      ⚠️ **Skripty piš do souborů, ne do heredocu tohohle shellu** — `\n` a UTF-8 se v heredocu rozpadnou
      (rozbité literály 2×, `SyntaxError` na U+2014 1×).
    - ⚠️ **SVG:** `viewBox='0 0 100 100'` + `preserveAspectRatio='none'` (souřadnice = %), popisky os jako
      HTML overlay (ne `<text>`). Roztažení kompenzuje `vector-effect:non-scaling-stroke` na `.g`/`.gv`/`.ln`
      (tloušťka i čárkování v souřadnicích obrazovky); **cokoli s tvarem** (`<circle>`, značka bodu) by
      se ale zdeformovalo na elipsu → koncový bod křivky je proto HTML overlay.
    - 🔴 **Graf má RÁM: `.cw` = rámeček s okraji, `.pa` = kreslicí plocha.** Všechna procenta
      (křivky, mřížka, koncový bod, křížek odečtu) jsou vztažená k **`.pa`**, popisky os leží
      v okrajích **vedle ní** (osa Y `right:100%` vlevo, osa X `top:100%` dole). ⚠️ **`.pa` NESMÍ
      mít `overflow:hidden`** — popisky z ní vystupují doleva; ořez dělá `.cw` a samo SVG.
      ⚠️ **Odečet pod kurzorem musí měřit `.pa`, ne `.cw`** (`w.hpa`), jinak se posune o šířku
      levého sloupce. Plná levá + spodní osní linka (`--line2`) je záměrně silnější než čárkovaná
      mřížka. Nový graf tenhle rám dostane tím, že jeho obsah obalíš do `<div class='pa'>`.
    - 🔴 **Osy se NEKRESLÍ ručně — jsou generované** (`niceStep`/`niceAxis`/`niceAxisLog` + `axY`/`axX`).
      Meze zaokrouhluje **`span()`**, tedy tam, odkud je berou i `poly`, `lastXY` a odečet pod kurzorem
      → jeden zdroj pravdy. Nový graf: dej mu `<g id='gyXxx'>` (mřížka), `<div class='yl' id='ylXxx'>`
      a `<div class='xl' id='xlXxx'>` a zavolej `axY`/`axX`; **nepiš statické `<line class='g'>`**.
      Když má graf jinou geometrii než 0..100 %, předej `ax.map`.
      Kontrolu, že se id os nerozešla s markupem, dělá `tools/spa/dom_check.py`.
      🔴 **Jednotka patří k ose JEDNOU** (`axY(..., unit)` → `<b class=yu>` nad sloupcem),
      **nikdy ke každému dílku** — a není to jen estetika: levý sloupec má ~8 znaků
      (56 px − 7 px odsazení, monospace 10 px ≈ 6 px/znak) a `.cw` má `overflow:hidden`,
      takže širší popisek se **tiše ořízne zleva**. Číslo formátuje `axNum` podle **kroku**
      osy (`axDec`), s přechodem na vědecký zápis pod 1e-6 a nad 1e6. Hlídá to `axis_test.js`
      (nejširší popisek každého grafu proti limitu 8 znaků). **Nový graf s delším popiskem
      → zvětši `--pl`.**
    - 🔴 **`rf_dbm`/ALLAN/DRIFT počítá klient z REÁLNÝCH měření; `sigma_tau`/`offset`/`drift` ze snapshotu
      se ZÁMĚRNĚ nepoužívají** (CM7 je neplní). Vzorky **podle `seq_meas`** (jinak σy nesmyslně nízká),
      buffer se **zahodí při změně brány**. ⚠️ **Rozdíl proti displeji:** displejový headline je simulace
      (#2), web servíruje reálná FPGA data → bez desky displej ukazuje kmitočet a **web správně `null`**
      (SPA vypíše důvod: STOP / SPI DOWN / ztráta signálu).
  - **Web rozšíření v12/v13 + revize (2026-08-24..26) — KÓD HOTOVÝ, NEOVĚŘENO NA HW.**
    Plný blow-by-blow → `docs/CLAUDE_ARCHIV.md` §7 + `WEB_UI_PLAN.md` + git. Co si pamatovat:
    - **v12 obsah:** #1 ovládání MATH/LIMITY/NULL/CAS ze SPA (týž `POST /api/scpi`), #2 mDNS
      `gpsdo.local` (ručně psaný responder v `lwip_app.c`, `LWIP_IGMP=1`, **best-effort**),
      #3 SSE push `GET /api/stream` (**SPA má fallback na 1 Hz poll**), #4 alarmy/prahy/selftest
      v `/api/state`, #5 GPS sky plot `GET /api/sats` (`ipc_sat_t` hlídá 7× `_Static_assert`),
      #6 dlouhá historie z datalogu přes IPC kanál `ipc_datalog_xfer_t`.
    - 🔴 **Datalog transfer: jen JEDEN souběžně (503 jinak)**, `ipc_datalog_service` je stavový
      automat, dávkově `IPC_LOG_SCAN_BUDGET` (128) záznamů/tik. **Nad `IPC_LOG_SCAN_MAX` (20 000)
      se bucket VZORKUJE** a odpověď to přizná (`resp_full_env` → SPA „PODVZOREK"). SPA sešívá až
      `DLCHUNKS`=4 dávky (posun `from`) → ~192 bodů; HTTP timeout **8 s**.
    - **v13:** `ipc_log_rec_t` nese `freq_min/max_x100000` → SPA kreslí min/max **pásmo** (`envPoints`).
    - **Paměťový rozpočet:** `HTTPD_BODYBUF_MAX`=4096, `HTTPD_MAX_CONN`=5, SPA ~62 kB `.rodata`,
      CM4 obraz ~195 kB / 1 MB, CM4 `.bss` ~83 kB / 128 kB (`s_hconn` ~25 kB). Při dalším růstu hlídat.
    - ⚠️ **ETag SPA = čas překladu `httpd_min.c`** (`__DATE__ __TIME__`), ne verze FW. `Cache-Control: no-cache`.
    - ⚠️ **localStorage historie se obnoví jen když mezera < 30 s** (Allan chce rovnoměrné τ0).
    - **Headline na webu zrcadlí displej** (`fmtFreqHtml`); koncový bod křivky = **HTML overlay, ne SVG**
      (roztažený `viewBox` → SVG kruh by byl elipsa).
    - ⚠️ Detail karet (klik) rozpadá řady po senzorech (`seriesTable`); hlavička = `data-hd`, ne třída
      (atributy z `innerHTML` jsou bez uvozovek — totéž pravidlo jako `data-st`).
- **SCPI dotazy (jen nad poli, která `scpi_src_t` UŽ má → bez bumpu `IPC_VERSION`):** `SYST:CAP?`,
  **`SYST:ERR:ALL?`** (⚠️ po výpisu chyb **nepřipojuje** `0,"No error"`), **`STAT:PRES`** (no-op, ale MUSÍ
  se přijmout — jinak inicializační `*RST;*CLS;STAT:PRES` z VISA/IVI zaplní frontu), `SENS:FUNC?`→`"FREQ"`,
  `SENS:ROSC:SOUR?`→`INT`, **`SENS:ROSC:LOCK?`** (⚠️ jen LOS_CLKIN bit3 + PLL_LOL bit4 — bit2 LOS_XTAL je
  trvale 1), **`MEAS:PER?`**, `MEAS:FREQ:STAL?`, **`SYST:TEMP:ALL?`** + **`MEAS:VOLT:ALL?`** (agregáty =
  1 round-trip). ⚠️ **Perioda má 15 des. míst (femtosekundy):** zlomek se tiskne **po dvou 32b půlkách**
  (`%07lu%08lu`) — 15 cifer se do `unsigned long` nevejde a newlib-nano neumí `%llu`.
- **`INPut[n]:` (vstupní cesta) — příkazy hotové, HW ne (2026-09-06):** `COUPling|IMPedance|ATTenuation|LEVel|HYSTeresis`, každý platný končí `-241 "Hardware missing"` (vstupní modul se teprve staví). Meze jsou ale **závazné už teď**: práh ±1,024 V (`MCP4728`), hystereze 1–60 mV, impedance jen 50 Ω / 1 MΩ, dělič jen 1 / 10. ⚠️ **Nic se neukládá** — uložená a neuplatněná hodnota by byla stejná past jako okno SÍŤ. ⚠️ Pořadí chyb: chyba příkazu **před** chybou provedení (`INP:LEV 99` → `-222`, ne `-241`). 🔴 **`INPut1:`/`INPut2:` musí fungovat** — `kw_match` číslici neumí, proto `inp_match()`; neexistující kanál = `-114`, ne `-113`.
- **`SENSe:FREQuency:APERture` = alias `GATE`** (VISA/IVI hledají `APERture`). Drženo v JEDNÉ podmínce `||`, aby se obě cesty nemohly rozejít.
- **SET příkazy** (SCPI už není read-only): `SENS:FREQ:GATE <s>` (presety 0,1/1/10/100 s), `SENS:FREQ:CHAN`,
  **`INIT`/`INIT:IMM`** (RUN), **`ABOR`** (STOP), **`READ?`**, `INIT:CONT?`. ⚠️ **Vláknový most:** stav
  měření vlastní UiTask, SCPI běží v UartTasku → SET zapíše jen **požadavek** (`g_ui_cfg_req` +
  `g_ui_cfg_req_pend`) a UiTask ho aplikuje v `app_gpsdo_tick_clock` přes `screen_main_apply_cfg_req()`
  (**před** `s_view` guardem, aby příkaz nezmizel v jiném okně). ⚠️ `GATE?`/`CHAN?` vracejí **nastavenou**
  hodnotu; skutečně změřené okno je na **`SENS:FREQ:GATE:ACTual?`**.
- **`SYST:DATE`/`SYST:TIME`:** SET přes **request-most** (`g_rtc_set_*` + `g_rtc_set_pend`) — RTC registry
  vlastní defaultTask, aplikuje **před** `rtc_try_sync()` (GPS má poslední slovo). ⚠️ Ručně zadaný čas
  **nenastavuje** `s_synced` ani BKP magic → UI dál hlásí „no GPS" (smysl jen bez antény).
- **W1 (ovládání z CM4/webu, ✅ ověřeno HW):** `ipc_ui_cfg_apply()` napojuje `IPC_CFG_GATE/CHAN/RUN` na
  **tentýž most** `g_ui_cfg_req`+`g_ui_cfg_req_pend` co SCPI přes USB. `IPC_CMD_LOG` → `datalog_set_enabled`.
- 🔴 **PAST — SLEPÝ READBACK (nalezeno HW testem přes web).** Zápis fungoval, ale `GATE?`/`CHAN?` přes
  TCP/HTTP vracely pořád `0.1`/`0`. Příčina: snapshot nesl jen `channel_id`/`gate_ns` = **co ohlásil FPGA
  rámec** (nula při mrtvém linku), ne **NASTAVENÍ** (`g_ui_cfg`). Opraveno v **IPC v11** (snapshot má
  `ui_cfg`, bývalý `_pad_s` → velikost beze změny). **Poučení: readback musí číst NASTAVENÍ, ne poslední
  naměřenou hodnotu** — jinak se chyba projeví teprve když měření neběží a vypadá jako porucha zápisu.
- ⚠️ **`scpi_selftest` hlásí ŘÁDEK prvního neúspěšného assertu** (`scpi_selftest_fail_line()`) — je to 101
  kontrol v jedné návratové hodnotě a na hostu se spustit **nedá** (jen arm-none-eabi, žádný nativní C).
  **Totéž má od 2026-08-30 i `httpd_min_selftest`** (`httpd_min_selftest_fail_line()`, makro `HT_OK`).
  🔴 **Jak ten řádek na CÍLI přečíst, když IPC nese jen PASS/FAIL:** obě proměnné (`s_st_fail_line`,
  `s_ht_fail_line`) žijí v `.bss` obrazu CM4, tj. v **SRAM2**. Sonda visí na CM7, takže se čtou na
  **systémové adrese** — CM4 alias `0x1002xxxx` → `0x3002xxxx` (**+0x2000_0000**):
  `nm CM4/Release/H757_LED_CM4.elf | grep s_st_fail_line` → `STM32_Programmer_CLI -r32 <adresa+0x20000000> 1`.
- 🔴 **HW průchod CM4 2026-08-30 (první skutečný běh — bank2 se dlouho neflashovala) našel DVĚ věci:**
  1. **`SCPI(CM4): selftest FAIL` NEBYLA chyba SCPI, ale chyba TESTU.** `scpi_selftest` assertoval
     u `DISP:BRIG` a `SYST:DATE/TIME` chování CM7 (`-222`), jenže na CM4 ty handlery **správně**
     vracejí `-241 "Hardware missing"` (displej ani RTC tam nejsou). Test teď má stejný
     `#if defined(CORE_CM7)` guard jako handler a na CM4 ověřuje **právě tu `#else` větev**.
     ⚠️ **Pravidlo: každý assert nad HW-vázaným příkazem musí být guardovaný stejně jako handler** —
     jinak dvoujádrový selftest padá na správném chování.
  2. **Skutečná chyba v `httpd_min.c`: Basic Auth s víc mezerami = 401.** Po předponě `"Basic "` se
     už nepřeskakovaly další mezery (RFC 7235 povoluje `1*SP`), takže v `auth_b64` zůstala vedoucí
     mezera → délka přestala být násobkem 4 → `b64_decode` vrátil −1 → odmítnuto. Opraveno.
  ✅ Po opravách obě `fail_line` = 0 (ověřeno sondou), tj. **SCPI i HTTP selftest na CM4 PASS**.
  ⚠️ **`meas_math_capture_null()` rovnou zapne relativní režim** (`null_en=1`) → po `CALC:NULL:ACQuire`
  Y klesne na nulu.

## W25Q512JV — externí QSPI flash 64 MB (w25q.c/h, QUADSPI)
Winbond **W25Q512JVFIQ** (512 Mbit = **64 MB**) na **QUADSPI Bank1**. Osazená na STM desce
(schéma `STM32H747BIT.pdf`, sheet `USB_SD_FLASH`). **Bring-up HOTOVÝ** — `qspiid`→`EF4020`,
`qspitest` erase/write/read/verify OK.
- **Piny (v .ioc, CubeMX-managed → regen-safe):** CLK=**PF10**(AF9), NCS=**PG6**(AF10),
  IO0=**PD11**(AF9), IO1=**PD12**(AF9), IO2=**PF7**(AF9), IO3=**PD13**(AF9), /RESET=**PH1**(GPIO out high).
  ⚠️ `SPI2_RCK_Pin`(PB12/FPGA CS) i `QSPI_BK1_IO1_Pin`(PD12) mají stejnou masku `GPIO_PIN_12`, ale
  různé porty (B vs D) → není konflikt.
- **CubeMX QUADSPI:** `FlashSize=25` (2²⁶ = 64 MB — KRITICKÉ), `SampleShifting=HALFCYCLE`, `ClockMode=0`,
  single flash. `MX_QUADSPI_Init` generovaný v `quadspi.c` (`hqspi`). CubeMX prescaler=23 (10 MHz) je jen
  default — **driver `w25q_init` ho přebíjí na `W25Q_SCK_PRESCALER=3` → SCLK 60 MHz** (regen-safe, jako
  FPGA SPI baud). **Read = Quad Fast Read 0x6C** (4-line, 8 dummy) — ⚠️ nad 50 MHz nutné dummy (plain
  Read 0x13 je stropován 50 MHz). Ověřeno `qspispeed` verify=OK @ 60 MHz.
  - **⚠️ Propustnost pollovaného čtení ~4,6 MB/s** (strop `HAL_QSPI_Receive` = CPU čte FIFO bajt po bajtu,
    ~215 ns/B), NE limit SCK. Raw 60 MHz quad = ~30 MB/s → odemkne až **DMA (`HAL_QSPI_Receive_DMA`/MDMA)
    nebo memory-mapped mód**. Pro malá data (config/kalib) je 4,6 MB/s hluboko nad potřebou; DMA přidat
    až u bulk read (fonty XIP / čtení logů). Viz [[revize-2026-07-03]] TODO.
- **⚠️ 4-byte adresování:** 64 MB > 16 MB → 3bajtová adresa nestačí. Driver dělá `EN4B` (0xB7) v initu
  + nativní 4-byte příkazy (READ `0x13` / PP `0x12` / SE `0x21`, `QSPI_ADDRESS_32_BITS`).
- **Driver `w25q.c`:** `w25q_read_jedec` (bez init), `w25q_init` (SW reset 66h/99h → JEDEC check →
  EN4B → quad-enable SR2), `w25q_read` / `w25q_write` (handluje 256B stránky; ⚠️ cíl musí být předem
  smazán) / `w25q_erase_sector` (4 KB), WIP polling (`wait_ready`). **Read = Quad Fast Read 0x6C** (4-line,
  `s_quad` z quad-enable; fallback Fast Read 0x0C 1-line); zápis/erase/registry 1-line.
- **UART:** `qspiid` (JEDEC ID, čeká EF4020), `qspitest` (init + destruktivní self-test sektoru 0),
  `qspispeed` (64 KB timed read + verify → KB/s), `storetest` (blob store self-test na CALIB regionu).

### Region mapa + storage vrstva (w25q_map.h, w25q_store.c/h)

**Region tabulka + QUADSPI piny → `docs/HW_REFERENCE.md`.** Ve zkratce: CONFIG `0x000000`,
CALIB `0x010000`, SETUP `0x020000`, DATA `0x030000` (~63,8 MB, base posunut kvůli SETUP → datalog
se jednou založí znovu), vše zarovnané na 64 KB, deska je generická.

- **`w25q_store`** = generický **kruhový wear-leveled blob store** nad regionem: 1 blob (max **4080 B** = 1 sektor)
  na region, každý zápis jde do dalšího sektoru (round-robin → N× endurance), nejnovější platný `seq` vyhrává.
  **Power-safe:** payload se zapíše první, hlavička (magic+seq+CRC16) NAPOSLED → výpadek uprostřed zápisu =
  magic chybí = záznam neplatný, starý zůstává. API `w25q_store_init/read/write`. Nezná app obsah (jen bajty).
  Ověřeno `storetest` (write/read/CRC + rotace sektorů OK).
- **⚠️ Cap 4080 B/blob** (1 sektor). Stačí na kalib params; velký LUT → multi-sektor rozšíření nebo DATA region (viz [[revize-2026-07-03]]).
- **Nastavení = HYBRID BKP + W25Q flash (`syscfg.c/h`, CONFIG store) — persist i přes power-cycle.**
  BKP (DR1/DR2/DR6) přežije **jen warm reset** (bez VBAT ne power-cycle) → přidána druhá vrstva do
  **W25Q CONFIG store** (blob `{magic, brightness, mute, autodim_en/sec, theme, lang, tz_offset, tz_auto, ui_cfg,
  datalog_en, anim_en, fx_en, + Math/limity: meas_math/null/limit/alarm_en, m/b/null_ref/lo/hi doubles}`,
  survey_valid/n/lat/lon/alt/spread (výsledek self-survey), síť, prahový monitor, okno MĚŘENÍ,
  vzdálený přístup (`web_ctrl_en`/`web_user`/`web_pass`) a **rozložení hlavní obrazovky**
  (`layout_classic`, přidáno 2026-08-23) — **aktuální magic „SCFE"** (dřív SCF8/SCFD; každá změna
  layoutu blobu = nový magic → první boot po ní načte výchozí a při první změně uloží nově;
  bez BKP zálohy jsou fx/anim/Math/survey/rozložení aplikované vždy, ne jen při studeném startu).
  **`syscfg_load()`** (app_gpsdo_init, PŘED `ui_theme_select`+`screen_main_init` kvůli
  tématu/jasu při 1. renderu): při **studeném startu** (BKP smazána → `g_syscfg_bkp_valid=0`) je **flash autoritativní**;
  při **warm resetu** (`g_syscfg_bkp_valid=1`, nastaví MX_RTC_Init dle DR2 magicu) má přednost BKP a flash se jen
  inicializuje. **`syscfg_flash_tick()`** (defaultTask ~100 Hz): **debounced shadow-diff** — po ~1,5 s klidu od
  poslední změny jeden flash zápis (rychlé +/- tapy se sloučí, wear + blokování kritických tasků minimalizované).
  BKP zápisy (`rtc_save_syscfg_if_dirty`) zůstávají = instant cache pro warm reset. ⚠️ **Okno ztráty ≤1,5 s**:
  změna + power-cycle do 1,5 s (flash ještě nezapsán) se ztratí (BKP by ji měl, ale power-cycle ho smaže). ⚠️ **Dva
  QSPI writeři** teď za běhu (defaultTask syscfg auto-save + UiTask `calib_save` na ULOZIT) — prakticky se nepřekrývají
  (různá okna, debounce), mutex je TODO při přidání bulk/log QSPI.
- **AD8307 + ADS 12V/5V kalibrace → CALIB store HOTOVO** (`calib.c/h`, `g_calib`, okno Kalibrace):
  blob `{magic, ad8307_slope, ad8307_intercept, gain_12v, gain_5v}` (20 B, verzováno magicem
  `0x43414C31` „CAL1"), `calib_load()` volá `app_gpsdo_init()` (UiTask, jednou při startu),
  `calib_save()` jen na tlačítko ULOZIT (blokující erase+write, needěje se to periodicky).
  Prázdný/nevalidní záznam → vychozí (datasheet) hodnoty. ⚠️ Krátké okno při bootu (SensorsTask
  běží dřív než `app_gpsdo_init()`) čte 12V/5V default gain, ne uloženou kalibraci — kosmetické.
### Datalog (datalog.c/h, datalog_sd.c) — DATA region, HOTOVO 2026-07-20 (TODO #6)
**Append-only kruhový log stability** do W25Q **DATA** regionu. Záznam **32 B / 10 s** → ~270 kB/den
(8640 zázn./den). ⚠️ **Datalog ZÁMĚRNĚ využívá jen ~1/3 DATA regionu** (`W25Q_DATALOG_SIZE` ~22,3 MB
= 696 960 záznamů → **~80 dní** kruhu; pak se přepisuje nejstarší — log nikdy „nedojde"). Zbylé 2/3
DATA regionu zůstávají volné pro další bulk použití (fonty XIP, rekonstrukce Allan pyramidy). (Plný
region = ~242 dní; dřív tu chybně stálo „~600".)
- **Záznam (LE, ruční serializace — NE memcpy struktury, aby byl formát nezávislý na kompilátoru):**
  `seq(0)`, `t_unix(4)`, `freq_x100000(8)`, `t_ocxo_c100(16)`, `t_board_c100(18)`, `ocxo_vc_mv(20)`,
  `rf_mv(22)`, `flags(24)`, `sats(25)`, `hdop10(26)`, **`vbat(27)`**, **CRC16(28)** (CCITT-FALSE přes byte 0..27).
  - ⚠️ **`rf_mv(22)` jsou SYROVÉ mV, ne dBm** (přejmenováno z `rf_dbm10` 2026-08-18). Uložení
    v mV je záměr — kalibrace `g_calib.ad8307_*` se může změnit, syrová hodnota ne. Jenže staré
    jméno pole neodpovídalo obsahu a **oba konzumenti to vzali doslova**: CSV export i SCPI
    `MMEM:DATA?` dělily deseti a servírovaly výsledek jako dBm → 571 mV vyšlo jako **„57,1 dBm"**
    místo −61,2 dBm (nesmysl: 57 dBm = 500 W na vstupu čítače). Odhaleno testem přes UART
    2026-08-18. Opraveno: SCPI převádí přes kalibraci stejným vzorcem jako `MEAS:POW?`,
    CSV sloupec se jmenuje `rf_mV` a nese syrovou hodnotu.
  - **`vbat(27)` — záložní CR2032, přidáno 2026-08-17.** Prahový monitor (alarm.c) umí křiknout,
    až je baterie vybitá, ale degradace CR2032 trvá měsíce a bez záznamu nejde odhadnout, **kdy**
    ji vyměnit. Vešlo se do **jediného volného bajtu 27** (dřív `spare`, uvnitř CRC), takže formát
    zůstal 32 B a **žádná migrace nebyla potřeba**: kód `(mV−2000)/8` → 1..255 = 2008..4040 mV
    s krokem 8 mV (na baterii bohatě — sleduje se trend přes měsíce, ne mV; chyba ≤4 mV).
    ⚠️ **Kód 0 je vyhrazen jako „nezaznamenáno"** — díky tomu se starší záznamy (kde byl bajt 27
    vždy 0) nepřečtou jako mrtvá baterie 2,00 V, ale jako `DATALOG_INVALID16`. Kryje to selftest
    (round-trip vč. kvantizace, starý záznam s přepočteným CRC, neplatné čtení senzoru).
    Konzumenti: CSV export (sloupec `vbat_mV`, prázdná buňka u starých záznamů), `MMEM:DATA?`
    (SCPI NaN `9.91E37`), UART `datalog dump` (`Vbat=`) a **okno GRAFY** — dolní graf se **tapem
    přepíná OCXO Vc ↔ VBAT** (`TAP: Vc/VBAT`). Záměrně přepínač, ne dvě série v jedné ose: Vc je
    kolem 1,9 V a VBAT kolem 2,9 V, takže společný autoscale by obě křivky zmáčkl do ~30 % výšky.
  `DATALOG_F_*` = GPS_VALID/FIX_3D/FPGA_LINK/SIGNAL_LOST/DIV16/HOLDOVER.
  ⚠️ **RF se ukládá SYROVĚ v mV**, ne v dBm — kalibrace (`g_calib`) se může změnit, syrová hodnota ne.
- **Pozice zápisu se po bootu ODVODÍ ze `seq`** (`find_head`: blok s nejvyšším seq v 1. záznamu → v něm
  první volný slot; smazaná flash = `0xFFFFFFFF` = volno). **Žádná hlavička/metadata** → log přežije
  reset i výpadek napájení. Na hranici erase bloku se blok nejdřív smaže (= zahodí 128 nejstarších).
- **Vlákno:** `datalog_tick()` volá **výhradně defaultTask** (vedle `syscfg_flash_tick`), throttle 10 s
  uvnitř. QSPI přes `qspiMutexHandle` s **krátkým timeoutem (10 ms)** — při obsazené flash vzorek zahodí
  (`write_errors++`) a jede dál; defaultTask krmí watchdog a nesmí čekat.
- **⚠️ Úložiště je za abstrakcí `datalog_backend_t`** (probe/read/write/erase + `erase_size`/`capacity`).
  `datalog_init` zkusí **SD** a spadne na **W25Q** → až bude SD osazená, log se přepne bez zásahu volajících.
  Rozdíl NOR vs SD = jen `erase_size` (SD = 0 → `erase` smí být NULL). **512B RMW layer HOTOVÝ 2026-08-08**
  (generický `blk_io_t` + `blk_read`/`blk_write` = překlad byte-offsetu na 512B bloky vč. spanningu a
  read-modify-write částečných bloků). RMW je kryté `datalog_sd_selftest` (RAM fake blok, bez HW).

### SD karta (`datalog_sd.c`, SDMMC1) — SW HOTOVÝ 2026-08-11 (#28), ale **záměrně VYPNUTÝ**
**SDMMC1 je v `.ioc`** (4-bit, CM7): PC8–11 = D0–D3, PC12 = CK, PD2 = CMD (AF12), `ClockDiv=2`
→ **SDMMC_CK 16 MHz**. Detaily + past při regeneraci = `CUBEMX_CHECKLIST.md` sekce SDMMC1.
- 🔴🔴 **`HardwareFlowControl` MUSÍ být ENABLE** (v `.ioc` chyběl). Bez něj má `CLKCR` bit17
  `HWFC_EN=0` a na H7 SDMMC **datová cesta vůbec nejede**: příkazy (init/CID/CSD → `TRANSFER`) projdou,
  ale **blokový přenos nedostane ani bajt** (`DPSMACT` visí, `STA=0x1000`, `HAL_SD_ReadBlocks` SW
  timeout) — vypadá to jako HW vada DAT0, ale HW je v pořádku (ref. projekt `H757_SDcard_01/` HWFC má).
  **Fix regen-safe v `sd_export.c`:** `sd_apply_init_config()` nastaví `hsd1.Init.HardwareFlowControl`
  + `SDMMC_Init(SDMMC1, hsd1.Init)` po `HAL_SD_InitCard` **ručně zapíše CLKCR** — nutné, protože init
  skládáme sami a vynecháváme `HAL_SD_ConfigWideBusOperation` (ta má 49denní SCR smyčku → IWDG), kde by
  HAL transfer takt+HWFC jinak aplikoval. `HAL_SD_InitCard` sám nechá CLKCR na init hodinách (400 kHz).
  ⚠️ Doplnit HWFC i do `.ioc` přes CubeMX (viz CUBEMX_CHECKLIST). **`sd init` má krokovou diagnostiku
  `[a]..[e]`** (probe datové cesty přes CMDTRANS i DPSM_ENABLE) — první místo pro budoucí SD bring-up.
- **4-bit sběrnice (2026-08-14):** po identifikaci `sd_try_4bit()` = **CMD55 + ACMD6** (bezdatové příkazy)
  řekne kartě 4-bit, pak `SDMMC_Init` nastaví host `WIDBUS=4B`. ⚠️ **NE `HAL_SD_ConfigWideBusOperation`**
  (ta čte SCR neohraničenou datovou smyčkou → dřív IWDG). Fallback při chybě = zůstat 1-bit. **Rychlost
  zápisu** dál táhne hlavně program-time karty; pomáhá **`f_expand` předalokace** (`_USE_EXPAND=1` v ffconf
  — MUSÍ zůstat) a **32 KB bloky** (doporučená velikost, `SD_SPEED_BUF`). Vyšší takt/CK odpor/bulk kondík
  = HW TODO ve STATUS #69.
- **`sd_hal_rd`/`sd_hal_wr` hotové**, za `#ifdef HAL_SD_MODULE_ENABLED` (aktivují se samy se SDMMC1).
- **Card-detect = `PE3`** (`datalog_sd_card_present()`, debounced). Socket J13 má mechanický
  spínač DET_A(GND)–DET_B + 47k pull-up → **karta vložena = LOW**. Pin si `datalog_sd.c`
  konfiguruje **sám** (idempotentně, jako CS ve `fpga_freq_init`) → funguje i bez `.ioc`.
  Je **mimo** `#ifdef HAL_SD_MODULE_ENABLED` (čisté GPIO), takže UI hlásí přítomnost karty
  i při vypnutém SD backendu. **Bez karty se `HAL_SD_Init` ani nezkouší** — to dělá „běh
  bez karty" zadarmo. **Hot-removal:** `sd_still_there()` kontroluje pin před každým blokem,
  při vytažení shodí `s_sd_ok` + `HAL_SD_DeInit` → další zápisy selžou hned místo ~200 ms
  timeoutu (defaultTask nesmí čekat). Vytržení uprostřed zápisu je bezpečné — každý 32B
  záznam má CRC16, takže se poškozený při čtení přeskočí.

### Benchmark pamětí (`membench.c/h`, okno PAMETI s_view=43, UART `membench`)

Rychlost zápisu/čtení **a hlavně hledání chybných bitů** napříč paměťmi. **Plné původní znění
(všechny důsledky + zdůvodnění) → `docs/CLAUDE_ARCHIV.md` §9.** Load-bearing:

- **6 cílů:** DTCM `0x20000000` (64 kB), AXI SRAM (vlastní 32 kB `.bss` buffer), SRAM1 D2
  `0x30001000` (64 kB), SDRAM `0xC0400000` (**512 kB z 32 MB**, scratch v MPU region 1), interní
  FLASH bank1 `0x08000000` (**jen čtení**, 2× s invalidovanou cache — hledá nestabilní čtení),
  W25Q `W25Q_BENCH_BASE` (32 kB, za flight recorderem). Sloupec „testováno" = `blok / kapacita čipu`.
- **SRAM4 se netestuje** (žije v ní IPC — pravidlo „jen paměť, kterou nikdo nepoužívá").
- 🔴🔴 **„Linker sem nic neumisťuje" NENÍ důkaz volné paměti** — pevná adresa v middlewaru se
  v mapfile neprojeví (`LWIP_RAM_HEAP_POINTER` `0x30004000` v SRAM1 shodil CM4 natrvalo). Při přidání
  cíle grepni i **absolutní adresy ve zdrojích obou jader**. Týž problém má UART `ram write`.
  ⚠️ Viníka pádu CM4 určuj čtením **syrového `g_ipc.cm4.heartbeat`** (5×/s) a označ **jen prvního**
  postiženého cíle (`g_cm4_alive` má ~3 s okno → mylně obviní poslední/nejdelší).
- 🔴 **Cache maintenance = SPRÁVNOST, ne rychlost.** Bez `clean` po zápisu data nedojdou do RAM;
  bez `invalidate` před čtením verify čte cache → **vada by se NIKDY neprojevila**. Jen cacheable
  cíle (AXI/SRAM1/SDRAM).
- **Vzory** (`pat_word`, verify hodnotu dopočítá znovu): `0x00`/`0xFF` (stuck-at), `55/AA` (zkrat
  datových linek), **adresa v adrese** (adresní linky), PRNG (crosstalk). **`err_bitmask`** ukáže
  na konkrétní datovou linku. Rozlišovací diag: `pat_err[]`, `first_err_got`/`want`, `alias_off`
  (adresní linky), `retain_err` (retenční — SDRAM, počkej 1 s před verify; clean PŘED, invalidate PO).
- 🔴🔴 **NEUZAVŘENO: adresy SDRAM se můžou opakovat po 2 MB** (intermitentní, podezření pájka
  `FMC_A9`=`PF15` ↔ `HADDR[21]`). **Prozvonit `PF15`** až bude deska otevřená.
  - 🔴 `membench` to PŘÍMO TESTUJE: `FB2` vs `FB0` se liší **právě jen v `HADDR[21]`** → kdyby
    sdílely paměť, triple buffering by byl double. **`fb_alias` řádek si přečti, než saháš na
    zobrazovací řetězec.**
- 🔴 **`sdram_safety_check`:** před každým během reverzibilní sonda ověří, že testovaný blok
  nesdílí buňku s FB0/FB1/FB2/canvas/`.sdram`; kolize → test se přeskočí. (Proto, že překryv existuje.)
- ⚠️ **SDRAM test 4 MB → 512 kB** — při 4 MB se rozbíjelo zobrazení (propustnost FMC, LTDC potřebuje
  ~46 MB/s), ne přepisem FB. Ustupuje scheduleru po 32 kB.
- ⚠️ **Běží JEN v UartTasku** (nehlídaný watchdogem). UI tlačítko → `g_membench_req`, `membench_service()`.
- ⚠️ **`membench_run` volá `__HAL_RCC_D2SRAM1_CLK_ENABLE()`** (`__HAL_RCC_C1_...` varianta pro CM7) —
  bez toho by D2/D3 region četl samé nuly.
- ⚠️ NOR flash umí jen 1→0 → **před každým vzorem erase** (jen 2 vzory u W25Q, erase je drahý).
- 🔴 **Karta si kreslí vlastní hlavičku na baseline `rect.y + 25`** → první vlastní řádek obsahu
  musí začínat aspoň ~30 px pod `rect.y`. Svislý rozpočet v komentáři u `MEMB_ROW0`.
- ⚠️ Řetězce ze snapshotu čti `%.Ns`, ne `%s` (`phase`/`msg` přepisuje UartTask za běhu kreslení).
- ⚠️ Rychlost se měří zvlášť od ověřování (součet do `volatile`); RAM přes DWT CYCCNT, QSPI přes
  `HAL_GetTick`. Sanity pořadí: zápis/čtení DTCM > AXI > SRAM1; W25Q čtení ~4,46 MB/s = strop QSPI.

### Datová cache měření (`sdram_log.c/h`, UART `sdramlog`) — 16 MB SDRAM, 2026-08-30

**Proč:** dosavadní statistika drží jen **decimační pyramidu** v RAM (`s_tr[]`, ADEV stages) — je
báječně levná (O(1) na vzorek), ale je to **ztrátová komprese**. Na Allanovu odchylku při dlouhých
τ, spektrogram a proklad je potřeba **surová řada**. 16 MB v SDRAM = **1 048 576 vzorků po 16 B**;
při dnešní kadenci FPGA (~4 měření/s) to je **~3 dny** souvislé historie.

- **Region `.measlog` @`0xC1000000`, 16 MB** = MPU region 3, **Normal WBWA cacheable**.
  ⚠️ Cacheable ZÁMĚRNĚ: bez MPU regionu by adresa spadla do default mapy, kde je
  `0xA0000000`–`0xDFFFFFFF` **Device paměť** → žádný cache-line prefetch a sekvenční čtení
  (= přesně to, co analýza dělá) by bylo řádově pomalejší.
  ⚠️ **Až bude log plnit SPI přes DMA** (protokol v2 / STATUS #62), musí konzument volat
  `sdram_log_invalidate()` — DMA obchází D-cache stejně jako DMA2D u framebufferu. Dokud plní
  CPU, je koherence samozřejmá.
- 🔴 **Sekce se MUSÍ jmenovat `.measlog`, ne `.sdram_log`** — sekce `.sdram` v linkeru má hladový
  wildcard `*(.sdram*)` a spolkla by ji dřív (první shoda vyhrává). Stalo se; link to naštěstí
  odhalil (`region SDRAM overflowed by 10186752 bytes`). Kvůli místu se `SDRAM` zmenšila 16→8 MB
  (reálně obsazeno 1,79 MB).
- **Záznam 16 B = `{seq, t_ms, f_uhz}`.** ⚠️ Velikost musí zůstat mocnina 2 — index se pak maskuje,
  ne dělí (`put` běží ve FpgaTasku).
- 🔴🔴 **Ukládá se kmitočet v µHz, NE surová dvojice `edges`/`gate_ns`.** `edge_count` může být
  počet period dělené větve (/4) **nebo** neděleného signálu (emulátor) — sám o sobě je
  **dvojznačný**. Pevný předpoklad „×4" už jednou způsobil, že `fpgasim on 10000000` hlásil
  **40 MHz** (opraveno commitem `a6c0128`) — a **při psaní téhle cache se to zopakovalo**
  (dump ukazoval 39 999 989 Hz). Násobitel proto ověřuje **`fpga_freq_hires_mul()` = jediný zdroj
  pravdy** (porovná podíl s autoritativním `frequency_x100000` do 0,1 %); `screen_main` i FpgaTask
  volají tutéž funkci. **Kdo si násobitel odvodí sám, tu chybu si zopakuje potřetí.**
  1 µHz je hluboko pod rozlišením TDC (~0,1 Hz), takže uložení nic neztratí.
- **EPOCHA místo příznaků v záznamu:** `gate_ns` (= τ₀) a **SIM** jsou vlastnosti *konfigurace*,
  ne vzorku → drží je `sdram_log_stat_t`, ne každý záznam (ušetří 8 B/záznam = **dvojnásobná
  historie**). Změna brány nebo přechod REAL↔SIM ring **vynuluje** → log nikdy nemíchá
  nesouměřitelné vzorky. Tatáž politika jako u Allan/trend pyramidy a bufferu ve web SPA.
  ⚠️ `gate_time_ns` z rámce kolísá o ppm, takže se porovnává s **prahem 10 %** (jinak by se ring
  mazal při každém vzorku).
- **Lock-free SPSC ring:** jeden producent (FpgaTask), libovolně čtenářů. `head` se zvedne **až po**
  zápisu záznamu (`__DMB()` mezi tím) → čtenář nikdy neuvidí rozepsaný záznam. Přepsání nejstaršího
  pod rukama hlásí `sdram_log_get` návratovou hodnotou 0.
- **`sdram_log_init()` region SÁM OVĚŘÍ** (běží ve FpgaTasku před smyčkou). Při vadné paměti se log
  **NEZAPNE** — tiše přepisovaný log je HORŠÍ než žádný. Testuje: (a) reverzibilní sondu proti
  **FB0/FB1/FB2** (kdyby měl čip menší kapacitu, `0xC1000000` by mohla být `0xC0000000`), (b) adresní
  aliasing přes celý region, (c) datové linky vzory na 4 místech.
  ⚠️ **`ALIAS_PROBES` musí dosáhnout na konec regionu** — původní hodnota 12 pokrývala jen 64 kB,
  takže test hlásil „bez aliasu", aniž by se podíval do pásma, kde se alias **podezírá** (2 MB).
  Dokonalé falešné uklidnění; hlídá to `_Static_assert`.
  ✅ **Na HW prošlo** (`sdramlog: OK`) → v horních 16 MB se adresy neopakují.
- **UART:** `sdramlog` (stav, `SIM ` prefix u emulovaných dat), `sdramlog dump N` (N nejnovějších,
  ⚠️ bez `%f` — celá část a desetiny se tisknou zvlášť), `sdramlog reset`.
- ⚠️ **Strop dnes NENÍ cache, ale FPGA:** protokol je pull/ACK a brána 0,25 s → ~4 měření/s.
  Souvislý proud (stovky kB/s) potřebuje **protokol v2 + SPI DMA** (STATUS #62). Zápis 16 B
  do SDRAM je proti tomu zdarma.
- ⚠️ **Konzument zatím není napojený** — Allan/spektrogram pořád čtou decimační pyramidu.
  `sdram_log_read_back()` je pro něj připravená, ale **neodzkoušená** (bez volajícího).

### SD export (`sd_export.c/h`) — mount/unmount + CSV

**Plné původní znění (všechny 🔴🔴 detaily + postup Františka) → `docs/CLAUDE_ARCHIV.md` §10.**
✅ **HW ověřen** (SDHC 14,5 GB, `CLKCR=0x4002` 4-bit 16 MHz — STATUS #69 tím padá). W25Q je
autoritativní úložiště, **SD je JEN EXPORT** (`GPSDO.CSV`, oddělovač `;`, unix sekundy, chronologicky).
Card-detect PE3, LOW = vloženo. Load-bearing:

- **Dělba:** `sd_export_tick()` levný → **defaultTask** ~2 Hz. `sd_export_mount()`/`sd_export_run()`
  **BLOKUJÍ** → **VÝHRADNĚ UartTask**. Auto-**UN**mount ano (instantní), auto-mount NE.
- ⚠️ **`FIL`/`DIR`/`FILINFO` NIKDY na stack** (`_FS_TINY=0` → `FIL` nese 512B buffer, ~560 B) →
  **`static FIL`** (funkce nejsou vnořené, běží jen z UartTasku).
- 🔴🔴 **`sd_diskio.c`: `ENABLE_SD_DMA_CACHE_MAINTENANCE` = 0 a `ENABLE_SCRATCH_BUFFER` VYPNUTÝ**
  (USER CODE). Blokující `HAL_SD_*Blocks` na H7 **nepoužívají IDMA** (data přes FIFO CPU) a
  `BSP_SD_*Blocks_DMA` jsou přepsané na tuhle variantu → přes IDMA neteče nic. Cache maintenance
  je pak u čtení **PŘÍMO ŠKODLIVÁ** (`Invalidate` zahodí CPU-zapsaná data → nuly → „karta není
  naformátovaná"). Scratch má navíc 2 chyby ST v zápisové větvi (`Invalidate` před `memcpy`,
  chybí `Clean`, čeká na špatnou zprávu). ⚠️ **Cache maintenance patří VÝHRADNĚ k `_DMA`/`_IT`.**
- 🔴🔴 **`SDMMC1_IRQHandler` MUSÍ existovat + NVIC prio 5** (`stm32h7xx_it.c` USER CODE 1 +
  `BSP_SD_Init`). `sd_diskio.c` čeká na zprávu z `SDQueueID`, kterou posílá jen ta obsluha —
  bez ní každý `f_mount`/`f_read` čeká `SD_TIMEOUT` = **30 s** → vyhladoví UiTask → **IWDG reset**.
- **`BSP_SD_GetCardState()`** přepsaná na polite polling (`osDelay(1)`) — `sd_diskio.c` na ni čeká
  v těsných smyčkách bez yieldu. `SD_TIMEOUT`=30 s je mimo USER CODE (regen vrátí), funkce `__weak`.
- **Postup Františka** (`C:\Claude_obecne\SD_franta.md`): **`BSP_SD_Init()` přepsaný** — identifikace
  **vždy 1-bit**, na 4-bit až potom s fallbackem (`HAL_SD_Init` volá `ConfigWideBusOperation`
  uvnitř identifikace, selhání by shodilo celý init). ⚠️ V `.ioc` **`SD_4_bits_Wide_bus` ZŮSTAT**
  (konfiguruje piny D1–D3; liší se jen runtime `Init.BusWide`). **`hsd1.ErrorCode` je „sticky"**
  → před každým přepnutím vynulovat. **`disk.is_initialized[0] = 0` při unmountu** (jinak mount bez
  karty = `FR_DISK_ERR` místo `FR_NOT_READY`). Deskový erratum: pull-up je omylem na CLK místo CMD
  → CMD (PD2) **musí mít interní pull-up** (`.ioc` `GPIO_PULLUP` ✅). ⚠️ Nepoužívat `-fsyntax-only`.
- **⚠️⚠️ `DATALOG_SD_RAW_OK` = 0, NEZAPÍNAT** (RAW zápis od offsetu 0 = LBA 0 = MBR karty).
- **🔴 Boot bez karty:** `MX_SDMMC1_SD_Init()` má na selhání `Error_Handler()` → v USER CODE
  **vyplnit handle a hned `return`**. ⚠️⚠️ **Holý `return;` NESTAČÍ** — `HAL_SD_MspInit()` s
  `Instance == NULL` nezapne hodiny SDMMC1 ani nenakonfiguruje piny (nutno vyplnit handle před returnem).
- **Okno SD KARTA (`s_view=37`):** PŘIPOJIT↔ODPOJIT (label = AKCE), TEST (verify 8 kB + propustnost
  512 kB), EXPORT CSV, **FORMAT = DVOJÍ potvrzení** (`s_sd_fmt_stage` 0→1→2, auto-zrušení po 6 s) →
  `f_mkfs`. ⚠️ **`_USE_MKFS=1` musí zůstat.** Tlačítka jen nastaví `g_sd_req`, práci dělá UartTask.
- **`get_fattime()`** čte `g_rtc_text_local`, ⚠️ nevolá `HAL_RTC` (běží z UartTasku).
- ⚠️ **`s_busy` příznak** (auto-unmount skip po dobu blokující operace) se nastavuje **VÝHRADNĚ ve
  wrapperu** (`s_busy=true; r=body(); s_busy=false;`), tělo je vyčleněné. **Nový `return` patří do těla.**
- **⚠️ IDMA + D-cache:** `sd_hal_*` používá **statický bounce buffer v `.bss` (RAM_D1) zarovnaný na
  32 B** + clean/invalidate, ne buffer volajícího (`blk[512]` na nezarovnaném stacku → cache
  maintenance by poškodila okolní data).
- **Zap/vyp** = `datalog_set_enabled` (okno Datalog tlačítko ZAPNOUT/VYPNOUT, UART `datalog on|off`),
  **persist v syscfg blobu** (W25Q CONFIG). ⚠️ Kvůli novému poli se **magic zvedl `"SCFG"` → `"SCF2"`**
  → první boot po této změně nastavení nenačte (neznámý magic) a vrátí výchozí; při první změně se uloží nově.
- **Okno Datalog (s_view=17) je ŽIVÉ** (v `app_gpsdo_tick`): stav/backend, záznamů/kapacita, seq, chyby zápisu.
  Používá `kv_row_live` (na rozdíl od `kv_row` si **čistí box hodnoty** — jinak by kratší hodnota nechala ocas té delší).
- **UART `datalog [on|off|erase|dump]`** (`erase` = destruktivní smazání celého logu, `dump` = posledních 10 záznamů).
- **Selftest** (`datalog_selftest` = 7. z 13): round-trip pack/unpack, detekce poškozeného bajtu přes CRC,
  prázdný slot, kalendář→unix vektory. (8. = `meas_math_selftest`; 9. = `setup_selftest` = sanitizace
  slotu sestavy `slot_sanitize`; 10. = `autocal_selftest` = verdikt pásem `ac_verdict`.)
- **Plánované dál:** rekonstrukce Allanovy pyramidy z logu po bootu; export přes USB CDC; memory-mapped XIP.

## I2C1 — senzory na FPGA desce (i2c.c MX_I2C1_Init, ads1115.c/h)
Druhá I2C sběrnice **I2C1**: SCL=**PB8**, SDA=**PB9** (AF4, ~100 kHz, Timing 0x70303AEE jako I2C4).
`MX_I2C1_Init` je **self-contained v i2c.c USER CODE 1** (GPIO+clock tam, regen-safe) — voláno v main.c USER CODE 2 před schedulerem. Mutex `i2c1MutexHandle`.
- **TMP117** @ 0x49, 0x4A (čteno v SensorsTask 2×/s). **⚠️ 0x4A zatím není na sběrnici** → vrací NACK (rychlá chyba `sensor_fail`, červený `!` na diagu) — to je očekávané, NEodstraňovat. 0x49 osazený.
  - ✅ **Vyjasněno 2026-08-30:** 0x4A **je `TMP117` ve vstupním modulu** (`ADD0 → SDA`, smluvní I²C mapa nové desky) — objeví se, jakmile bude modul připojen. Dřívější „NENÍ osazený, ať se připojí až bude" byl správný odhad; teď je to zapsané. Labelovat ho jako **teplota modulu**, ne „FPGA board" (dnešní popisek v diagnostice).
- **ADS1115** @ 0x48 (4 single-ended kanály AIN0–3, 128 SPS, single-shot). Driver `ads1115_start(hi2c,ch,pga)`/`ads1115_read_raw`/`ads1115_raw_to_mv(raw,pga)` — **PGA je per kanál** (tabulka `k_ads_pga[4]` v `freertos_task_sensors.c`; zatím vše ±4.096 V pod `ADS1115_HW_DIVIDERS_REV2=0`, cílové ±2.048 V po úpravě děličů — viz `BOARD_V20_CHECKLIST.md §7`). **AIN0=OCXO_VC_Sense, AIN1=RF_Level (AD8307), AIN2 = 12V/VBUS větev přes dělič (×13417/2814), AIN3 = 5V větev (×4978/2526)** — přepočet v SensorsTask ukládá skutečné napětí větve.
- **⚠️ Stav senzorů = `g_sensors[SENS_COUNT]` (`sensor_stat.h`), NE staré skalární `g_temp*`/`g_ads_mv`.** Jednotná struktura pro 3× TMP117 + 4× ADS + **3× ADC3 (MCU jádro teplota, VDDA, VBAT)** = 10 senzorů: `last` (poslední hodnota, °C nebo mV), `min`/`max`/`mean` (statistika z platných vzorků, running mean), `samples`, `valid` (1=poslední čtení OK), `err_total`/`err_streak` (čítače chyb). Index = `sensor_id_t` (`SENS_T48/T49/T4A/ADS0..3/CORE_T/VDDA/VBAT`). **Zápis VÝHRADNĚ SensorsTask** přes `sensor_update(id,val)` (platný vzorek) / `sensor_fail(id)` (chyba I2C → `valid=0`, `last` drží poslední dobrou → matematika/log ignorují podle `valid`). **Bez zámku** (mění se ~2×/s, roztržené čtení tolerováno; přesná matematika patří dovnitř SensorsTask). `sensor_stat.h` je čistý C header (smí ho includovat i app/ vrstva). SensorsTask čte senzory **2×/s** (`vTaskDelayUntil` 500 ms); touch NENÍ v tomto tasku (viz UI vrstva).
- **⚠️ ADC3 = interní MCU senzory (teplota jádra / VREF / VBAT), `SENS_CORE_T/VDDA/VBAT`.** Čte SensorsTask (`adc3_read_chan`), zobrazení v diag „MCU" kartě + okně SENZORY + UART `sensors`/`adcraw`. **Rozchození bylo HW-tricky — 5 vrstev problémů (interní kanály railovaly na `0xFFFF`):**
  0. **🔑🔑 VREF+ není spojen s VDDA (HW desky) → reference budí vnitřní `VREFBUF`.** Pin 43 má jen `C15` 100n + dodaný **1 µF** (nutný pro stabilitu VREFBUF), s VDDA NEspojen (záměr — kvůli SMPS šumu). Bez reference VREF+ visel (~0,5 V) → VREFINT (1,22 V) i teplota saturovaly na `0xFFFF`. **Zapnutí VREFBUF v `main.c` USER CODE 2** (`HAL_SYSCFG_VREFBUF_VoltageScalingConfig(SCALE0 ≈ 2,5 V)` + `HighImpedanceConfig(DISABLE)` + `HAL_SYSCFG_EnableVREFBUF`). **⚠️🔑 KRITICKÉ: VREFBUF má VLASTNÍ clock `RCC_APB4ENR.VREFEN` (NE přes SYSCFG!) — bez `__HAL_RCC_VREF_CLK_ENABLE()` je celý `VREFBUF->CSR` MRTVÝ** (zápisy ignorovány, čtení vrací 0, ENVR „nedrží"). Tohle byla nejskrytější příčina — VREFBUF vypadal jako HW vada reference, ale chyběl jen ten clock enable. Ověření: `adcraw` → `VREFBUF CSR=0x09` (ENVR+VRR), `CCR≠0` (trim), `vref≈32000`. **Reference je ~2,5 V, NE VDDA 3,3 V** → label senzoru „VREF".
  1. **`PCSEL=0`** — na H7 musí mít každý kanál bit v `ADC3->PCSEL`, jinak je analogový vstup **odpojený** → railuje. `HAL_ADC_ConfigChannel` ho na této verzi NEnastavil → zapínáme ručně (`ADC3->PCSEL |= (1<<kanál)`, kanál z `SQR1` rank1; ADEN musí být 0).
  2. **`ClockPrescaler` CubeMX vynechal** z `MX_ADC3_Init` (i když `.ioc` má DIV8) → ADC běžel na 25 MHz + špatný BOOST. SensorsTask init nastaví `ADC_CLOCK_ASYNC_DIV8` (→3,125 MHz) + `HAL_ADC_Init`.
  3. **Kalibrace jsou 16-bit** (ne 12-bit jak tvrdí HAL komentář; VREFINT_CAL=24291). LL makra `__LL_ADC_CALC_*` u VREFINT dělí 12-bit → špatně. Počítáme ručně 16-bit: **`vref(+) = VREFINT_CAL×3300/vref_data`** (reference-agnostické → vrací skutečné VREF+, tj. ~2500 z VREFBUF), `Temp = (ts×vref/3300 − TS_CAL1)×80/(TS_CAL2−TS_CAL1)+30` (**korekce `×vref/3300`** protože TS_CAL je @ 3,3 V ale VREF+ je 2,5 V), `VBAT = vbat×vref/65535×4` (vnitřní dělič /4). **„VDDA" senzor teď měří VREF+ (~2,5 V), label = „VREF".**
  4. **Single-channel režim** (ScanConvMode=DISABLE, NbrOf=1, čteno po jednom) — scan polling 3 kanálů byl nespolehlivý. VBAT = pin 8 = záložní CR2032 (BT1) → VBAT senzor monitoruje tu baterii.
  🔴 **`SENS_CORE_T` je JEDINÝ senzor, který se FILTRUJE** (IIR α=1/8 @1 Hz, τ≈8 s, v SensorsTasku **před** `sensor_update` → filtruje i min/max/avg). Změřeno 2026-09-01: dva odečty pár sekund po sobě **52,17 a 48,9 °C**; křemíkové čidlo má nekalibrovanou přesnost v jednotkách °C a jde přes VREFINT (sčítá se šum obou převodů). Zobrazuje se na **jednu desetinu** (`temp_deci`), ostatní teploty na dvě (TMP117 má krok 0,0078 °C = dvě desetiny jsou skutečně nesená informace). ⚠️ **Filtrovat se smí JEN tady a jen proto, že na té hodnotě nic nevisí** — žádný alarm, práh ani warm-up kritérium ji nečte (warm-up jede z OCXO 0x49), je to indikátor „jak je horký křemík", ne měřicí vstup. **U analogových větví (ADS0–3, VREF, VBAT) by filtr byl NEPŘÍPUSTNÝ** — ty do měření mluví a zakryl by skutečný výpadek napájení. Nekopírovat ten vzor na jiné senzory.
  Vše regen-safe v `freertos_task_sensors.c` + `main.c` USER CODE 2 (ne v generovaném `adc.c`). Diagnostika: UART `adcraw` (raw + spočítané). Detail viz `CUBEMX_CHECKLIST.md`.
- **Ošetření chyb senzorů:** při selhání I2C čtení (HAL chyba / mutex nezískán) → `sensor_fail`: `valid=0`, hodnota se NEpřepisuje (žádný sentinel, neotráví průměry). **Log** přes UART jen na PŘECHODU stavu (první chyba po OK / obnovení) — žádný 1 Hz spam. **Displej** (diagnostika): neplatná hodnota se kreslí ztlumeně (`UI_COLOR_INK_3`) + malý červený `!` vlevo. **I2C4 MÁ recovery** (`i2c4_recover` v `freertos_task_ui.c`, od 2026-07-12, commit 16df3f8): při ≥8 po sobě jdoucích HAL selháních čtení touch (FT5x06) udělá 9 SCL pulzů (PH11) + **re-init BEZ `MX_I2C4_Init`** (jen `HAL_I2C_Init` inline) — stejný bezpečný vzor jako `i2c1_recover` níže, takže se **NEvolá** ta nebezpečná cesta popsaná v incidentu výše (ta byla o špatné 400 kHz timing hodnotě v `MX_I2C4_Init`, ne o téhle recovery technice). Rate-limit max 1×/5 s, pod `i2c4MutexHandle`. Log `touch: I2C4 nereaguje (N chyb) -> bus recovery` = normální/očekávané chování při zaseknuté sběrnici (ATTINY drží SDA po nešťastné transakci), ne chyba.
  - 🔴🔴 **`i2c4_recover()` sběrnici ZABÍJELA místo aby ji zachránila (nalezeno + opraveno 2026-08-30).**
    **`HAL_GPIO_Init` NESAHÁ NA `ODR`.** `ODR11` (SCL) bylo **0**, takže v okamžiku přepnutí PH11 do
    `GPIO_MODE_OUTPUT_OD` pin **okamžitě stáhl SCL k zemi**. A protože se pulzuje jen když SDA drží
    slave (`for (… && ReadPin(PH12) == RESET; …)`), stačilo aby byla **SDA vysoko** — tělo smyčky se
    vůbec neprovedlo, `WritePin(PH11, SET)` se nikdy nezavolal a **SCL zůstala držená dole natrvalo**.
    Opakovalo se to při každém dalším pokusu (rate-limit to jen zpomalil).
    **Fix:** `HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_SET)` **PŘED** `HAL_GPIO_Init` do
    OUTPUT_OD + bezpodmínečné uvolnění SCL za smyčkou.
    ⚠️ **Pravidlo: než přepneš pin z AF do OUTPUT_OD, VŽDY nejdřív nastav `ODR` na klidovou úroveň.**
    Totéž hlídej v `i2c1_recover` (PB8) při každé úpravě.
    - **Projev:** I2C4 umřela ~7 s po bootu a už nenaběhla → mrtvý dotyk, ale **displej kreslil dál**
      (UiTask žije), takže to vypadalo jako „**displej zamrzne po spořiči**" — spořič je jen první
      místo, kde dotyk potřebuješ. Diagnóza sondou: `GPIOH IDR` SCL=0/SDA=1, `GPIOH ODR`=0x0,
      `I2C4 ISR`=0x8001 (BUSY), TMP117 0x48 = 14 platných čtení a 8351 chyb **v řadě**, zatímco
      TMP117 0x49 na I2C1 měl 5928 platných a sérii 0 (⇒ vadná je konkrétní sběrnice, ne HW obecně).
    - ⚠️ **Slepá ulička, do které jsem šel první:** podezření padlo na zápis jasu do ATTINY při
      auto-dimu. **Vyvráceno** — `s_bl_settle == 0` dokazuje, že zápis podsvícení se nikdy neprovedl;
      sběrnice byla mrtvá dřív, než spořič začal stmívat.
    - **Diagnostika bez sondy:** `status` má řádek `I2C4: SCL=x SDA=x idle|BUSY | TMP117 0x48 err N
      (v rade M)` (`i2c4_line_state()` čte přímo IDR/ISR) + značku `<== SBERNICE MRTVA`. Zdravé =
      `SCL=1 SDA=1 idle`, err v řadě 0.
    - 🔴🔴 **KOŘENOVÁ PŘÍČINA JE HW A FIRMWARE NA NI NEDOSÁHNE (uzavřeno měřením 2026-08-30).**
      Po ~34 min běhu sběrnice zemřela znovu — ale **jinak**: `scanner` nenašel **ŽÁDNÉ**
      zařízení (mlčí 0x38 touch, 0x45 ATTINY i 0x48 TMP117 naráz), přitom `SCL=1 SDA=1`,
      master `idle`, bez chybových příznaků. **To není zaseknutá sběrnice, ale odmlčené slave
      čipy** — linky drží vysoko, takže pull-upy (a tedy napájení panelu) žijí.
      **Firmware to nemá jak resetovat:** napájení LCD, podsvícení, reset dotyku i reset bridge
      jdou VÝHRADNĚ přes ATTINY po I2C4 (`WS_REG_PORTC`), a **přímý GPIO reset ze STM32 na panel
      ani na ATTINY v zapojení NENÍ** (ověřeno v `gpio.c` i `main.h`). Jediná ovládací cesta je
      právě ta mrtvá sběrnice → **pomůže jen odpojení napájení desky i panelu.**
      **Trvalá náprava potřebuje HW zásah** — vyvést reset ATTINY na volný GPIO; teprve pak by
      šla obnova ze SW.
    - **Co firmware dělá místo toho (3 opatření, žádné neřeší příčinu):**
      1. **Nezhoršuje to** — oprava `ODR` výše (recovery už sběrnici nezabíjí).
      2. **Nepálí CPU** — back-off pollu dotyku na 2 Hz při ≥8 chybách v řadě.
         ⚠️ **Změřeno: mrtvá I2C4 dělala z UiTasku 91 % CPU (IDLE 3 %)**, protože neúspěšné
         `HAL_I2C_Mem_Read` je spin s timeoutem 100 ms a běželo 15–30×/s. Po back-offu **21 %**.
         (I2C1 tenhle idiom měla od začátku — `i2c1_backoff_ms`; I2C4 ne.)
      3. **Řekne to uživateli** — `app_gpsdo_touch_dead()` vykreslí přes patku banner
         „DOTYK NEDOSTUPNY (I2C4 neodpovida)". Kritérium = **30 s bez úspěšného čtení**
         (přechodné výpadky se zotaví do ~1 s, měřeno). Banner se opakuje 1×/2 s, protože
         okna se pod ním dál renderují. Bez něj to působí, že „displej zamrzl" — přitom
         kreslí dál a UiTask normálně běží (ověřeno sondou: uptime rostl).
    - **Diagnostické nástroje** (`tools/`): `uartq.ps1` (víc příkazů, čeká na dokončení výpisu),
      `uartlog.ps1` (pasivní log s časovými razítky), **`i2c4watch.ps1`** (hlídá `status` a
      v okamžiku poruchy sám vyvolá `flightrec test` + dump + `scanner`).
      ⚠️ Kritérium poruchy je **souvislá série** chyb (`v rade > 20`), ne jedno selhané čtení —
      to reaguje i na běžný přechodný výpadek (naměřeno: err 2 / v řadě 0, `scanner` vzápětí
      našel všechna 3 zařízení). **I2C1 MÁ recovery** (`i2c1_recover` v SensorsTask): při „wedge" chybě (BERR/ARLO/TIMEOUT — slave drží SDA) udělá 9 SCL pulzů (PB8) + **re-init BEZ `MX_I2C1_Init`** → jeden vadný/zaseknutý čip neshodí zbytek (ADS). Spouští se max 2×/cyklus, JEN na wedge, NE na NACK (absentní 0x4A re-init nezpůsobí). I2C1 nemá ATTINY → reset bezpečný. **⚠️ Recovery NESMÍ volat `MX_I2C1_Init`** — ta má `Error_Handler()` (nekonečná smyčka) při selhání `HAL_I2C_Init`; při ODPOJENÉM/plovoucím busu (pull-upy jsou na FPGA desce!) re-init selže → **zamrzl by CELÝ program** (zjištěno 2026-06-25). Recovery proto dělá GPIO AF + `HAL_I2C_Init` inline a selhání ignoruje (zkusí příští cyklus). **Recovery běží pod `i2c1MutexHandle`** (nekoliduje s UART `si5356`/`scan1`). **Back-off (`i2c1_backoff_ms`):** při trvalém selhání celé I2C1 (mrtvý bus) se polling zpomaluje **3×@500 ms → 3×@1 s → 2×@2 s → @10 s** (šetří CPU); jakékoli HAL_OK → reset na normál (NACK absentního 0x4A se nepočítá → při připojeném kabelu back-off nevznikne). TMP117 0x48 (I2C4) čte dál 2×/s, gate je jen na I2C1.
- **Si5356A** @ 0x70 (clock generator) → `si5356.c/h`, `si5356_init(&hi2c1)` v main.c USER CODE 2 (po `MX_I2C1_Init`, před schedulerem → bez mutexu). Aplikuje **ClockBuilder Pro register map** (`REGMAP[]`, oficiální CBPro „C Code Header" export) přes paging (reg 255 page0/page1) + SiLabs apply proceduru (OEB_ALL off → E2 pulse → SOFT_RESET 0xF6 → OEB_ALL on). Status reg 218 (0xDA), **bitová mapa OVĚŘENA z AN565** (dřívější „bit2=LOS_CLKIN" byla chyba): bit0 SYS_CAL, **bit2 LOS_XTAL** (krystal XA/XB — na desce NEOSAZEN, piny uzemněné → bit TRVALE 1, benigní, ignoruje se), **bit3 LOS_CLKIN** (skutečná ztráta 10 MHz na pinu 4), bit4 PLL_LOL. ⚠️ **PLL_LOL se při fyzické ztrátě vstupu NEasertuje** (AN565: LOL = rozdíl >5000 ppm na PFD, ne odpojený vstup) → ztrátu reference hlásí právě bit3. UART `si5356` = re-init + status, **`si5356 clr`** = vynulovat sticky. 🔴 **STICKY registr 247 (od 2026-09-02, STATUS #103):** reg 218 je ŽIVÝ, takže krátký výpadek reference mezi dvěma čteními (2×/s) byl **neviditelný** — u kmitočtového normálu přitom znamená, že měření z té doby neplatí. 247 má **shodné bitové pozice** a podrží i mikrosekundový glitch (AN565: maže se **zápisem nuly**). SensorsTask ho čte vedle 218 a latchuje do `g_si5356_sticky` (bez `LOS_XTAL` — bez krystalu trvalá 1); zobrazení v `status` (řádek `REFERENCE:`), v okně Reference + tlačítko VYNULOVAT. ⚠️ **Zápis na I2C1 dělá VÝHRADNĚ SensorsTask** — UI/UART jen nastaví `g_si5356_clr_req`. ⚠️ **Jednorázové ARMOVÁNÍ `SI5356_STICKY_ARM_MS` (5 s):** změřeno, že hned po bootu jsou sticky bity `LOS_CLKIN+PLL_LOL+SYS_CAL` nastavené — je to historie z náběhu PLL po soft resetu v `si5356_init`, ne výpadek. Bez armování by přístroj křičel po každém zapnutí. ⚠️ **Bitové masky `SI5356_*` jsou nově JEN v `si5356.h`** — byly duplicitně i v `app_gpsdo.c` a `screen_main.c`.
- **⚠️ KONFIGURACE: 4× 100 MHz, fáze 0/90/180/270° (= 0/2,5/5/7,5 ns), Vstup 10 MHz → VCO 2,2 GHz (N=220) → /22.** Ty 4 fázově posunuté hodiny jsou **reference pro 4-fázový vernier TDC ve FPGA** (reciproký čítač, jemný krok 2,5 ns). **MUSÍ být 90°, NE 45°** — při 45° jemné kódy nesednou na 2,5 ns mřížku TDC → systematická chyba kmitočtu. Fáze v reg (LSB=Tvco/128=3,551 ps): CLK1 r111/112=704=2,5 ns, CLK2 r115/116=1408=5,0 ns, CLK3 r119/120=2112=7,5 ns. **Přesnost čítače = přesnost těch 100 MHz (= ppm 10 MHz vstupu Si5356).**
- **Při změně konfigurace: v CBPro nastav fáze (90° krok!), exportuj „C Code Header" a nahraď `REGMAP[]`** (formát {addr,val,mask} — adresy DECIMÁLNĚ pro 1:1 diff s exportem). **Vynech řádky s `mask==0x00`** (CBPro „do not write"); `mask<0xFF` → read-modify-write, `mask 0xFF` → přímý zápis (`wr_masked` to respektuje, `mask 0` přeskočí).
- **UART `scan1`** = I2C scan na I2C1 (s popisky zařízení). `scanner` zůstává pro I2C4.
- **Diagnostická obrazovka** (`app_gpsdo_render_diag`, tlačítko MENU → ZPĚT): **dvousloupcový layout** (`DG_*` makra v `app_gpsdo.c`). **Levý sloupec:** Teploty — **labely dle umístění, ne adres** (pořadí: „STM board" = TMP117 0x48, „MCU jadro" = ADC3 CORE_T, „OCXO" = 0x49, „FPGA board" = 0x4A), všechny `last` + `min/max`; Napětí — „OCXO_VC" (AIN0, ladicí napětí), „RF_Level" (AIN1), AIN2 (12V), AIN3 (5V), VREF + VBAT. **Pravý sloupec:** *Komunikace + měření FPGA* (`g_spi_text` zeleně/červeně + `g_freq_info` = gate/PH/SEQ), *Reference Si5356* (lock z `g_si5356_status`: LOCK OK / LOS CLKIN! / PLL UNLOCK! / CALIB…), *System / RTOS / RTC* (heap free/min, CPU %, uptime, **RTC čas HH:MM:SS** z `g_rtc_text` — ztlumený + `no GPS` dokud nesrovnán z GPS). Neplatný senzor = ztlumeně + červený `!`. Refresh ~2×/s z `app_gpsdo_tick` (UiTask), tearing-free přes `prim_stm32_present`.
  - ⚠️ **Šířka `dval`/`dtext` boxu MUSÍ nechat rezervu za sousedním textem** (min. ~20 px) — box se před vykreslením čistí (`fill_rect` na pozadí karty), takže když box zasahuje do oblasti sousedního labelu, KAŽDÝ živý redraw kus labelu smaže (viz historie 2026-07-18: min/max box teplot začínal na `DG_LLBL+96`, což bylo uvnitř labelů „STM board"/„FPGA board" — text se při každé aktualizaci ořezával). Boxy v teplotním řádku teď: label do ~140 px, min/max `DG_LLBL+140` šířka 100, hodnota `DG_LVAL` šířka 100 — žádný box nezasahuje do souseda.
  - **Datové zdroje:** Si5356 status čte SensorsTask (`si5356_read_status`, reg 218) do `g_si5356_status`/`g_si5356_ok`; RTOS zdraví počítá UiTask (`xPortGetFreeHeapSize`, idle-delta CPU %) do `g_rtos_*`/`g_uptime_s`.
  - **Diagnostika = technický hub.** Footer: **DIAGRAM** (blokové schéma, s_view=21) | **PAMET** (s_view=5) | **SELFTEST** (s_view=20) | ZPĚT. Do Diagnostiky se dá i z **System Health** (tlačítko DIAGNOSTIKA) a z Menu dlaždice.
  - **Tlačítko DIAGRAM** → **Komunikace: blokové schéma** (`app_gpsdo_render_commdiag`, s_view=21). Uzly: GPS NEO-7M, SENZORY, STM32H757, Si5356A, FPGA GW1NR-9. **Grafika (přepracováno 2026-07-18):** uzel = zaoblený rámeček, výplň BG_0, **obrys 2 px v barvě stavu + stavová „LED" tečka** vpravo nahoře (stav čitelný bez čtení popisků); STM32 má akcentní barvu (= „my", ne stavový). **Čárkovaný skupinový rámeček „FPGA deska"** (`cd_group`, fieldset styl — popisek přerušuje rámeček) kolem Si5356+FPGA. Popisky spojů na **„pilulce"** (`cd_label_chip`) — **šířka se přizpůsobí textu** (`prim_text_width`, žádný prázdný blok), výplň BG_0 překryje čáru pod textem → popisek sedí přímo NA spoji a zůstává čitelný. Spoje = pravoúhlé trasy (`cd_path`) barvené stavem: GPS→STM32 (UART/1PPS), SENZORY↔STM32 (I2C1/I2C4, `i2c_health()`), STM32→FPGA (SPI2, `g_spi_ok`), OCXO→Si5356 (CLKIN, `SI5356_LOS_CLKIN`), Si5356→FPGA (4×100 MHz), RF vstup→FPGA (`g_freq_stale`). Celý diagram se překresluje najednou při změně `cd_state_key()`.
    ⚠️ **Šířka uzlů = text (mono_16, 10 px/znak) + ~18 px padding/stranu** (historie: uzly měly 90-130 px prázdné rezervy). **Postranní popisky OCXO/RF** mají šířku ověřenou proti reálné šířce textu — „OCXO 10MHz" @ sans_16 potřebuje ~104 px, dřívější box 90 px ho **ořezával**.
- **Seznam všech oken `s_view` 0..48 → `docs/HW_REFERENCE.md`.**

### Prahový monitor reálných veličin (alarm.c, okno PRAHY s_view=39) — 2026-08-17
Do 2026-08-17 hlídal `alarm.c` jen **události** (FPGA link, GPS lock, limit pass/fail) — z deseti
senzorů, které měří 24/7 reálná data, **ani jeden**. Doplněny tři prahové podmínky nad skutečně
měřenými veličinami: **VBAT pod mezí** (záložní CR2032 pro RTC/BKP — degradace, o které se jinak
nedozvíš, dokud tiše nepřijdeš o čas a nastavení při odpojení napájení), **OCXO mimo teplotní pásmo**
(rozladěná pec) a **σy@1s nad prahem**.
- **Konfigurace `mon_cfg_t`** (`g_mon_cfg` v alarm.h) + persist v syscfg blobu (magic **„SCFA"→„SCFB"**,
  tedy jeden boot s výchozím nastavením). Výchozí: **VBAT 2,80 V** ZAP (CR2032 s **3,3 V nominálem** na
  této desce; syscfg magic „SCFE"→„SCFF" 2026-08-24, ✅ ověřeno HW 2026-08-25), OCXO 45–55 °C ZAP.
  Displejové nominály VBAT (GRAFY vert. bar + PŘEHLED KANÁLŮ + web) = 3,3 V.
- ⚠️ **`adev_en` je výchozí VYPNUTÝ** — σy@1s se dnes počítá ze **simulace** headline (~1e-8), takže
  jakýkoli realistický práh by pípal na šum. Mechanika je hotová a správná; zapnout až po #2.
- **Hystereze je povinná** (`band_eval`): VBAT ±30 mV, OCXO ±0,5 °C, ADEV ±10 %. Bez ní by veličina
  sedící přesně na prahu překlápěla stav při každém vyhodnocení (5×/s) a pípala donekonečna.
- **Start tichý** (`s_*_ever`): alarm se ozve až po jednom dobrém čtení, takže trvale vybitá baterie
  nepípá při každém bootu — na displeji ji vidíš (SYS pilulka + okno PRAHY) tak jako tak.
- **Vyhodnocení běží i při mute** (`mon_eval` je před mute větví): `g_mon_*_bad` čte SYS pilulka a okna,
  takže musí odrážet skutečnost; mute umlčuje **jen pípnutí**.
- **σy@1s se předává přes globál `g_adev_1s`** (plní `app_gpsdo_tick_stats_sample` ze
  `screen_main_adev_1s()`) — Core vrstva nesmí sahat do `app/screens/`, stejný most jako `g_meas_verdict`.
- **SYS pilulka**: všechny tři jsou **AMBER** (degradace). RED zůstává vyhrazená ztrátě reference
  a selftest FAILu, tedy stavům, kdy přístroj buď neměří, nebo měří špatně a neví o tom.

### Okno KVALITA GPS (s_view=38) — historie příjmu z datalogu
Vstup tlačítkem **KVALITA** v okně GPS. Odpovídá na otázku, kterou živý pohled na družice nezodpoví:
*„je moje anténa dobře umístěná?"* — graf **počtu družic** a **HDOP** v čase + souhrn (% času s 3D fixem,
průměr/min družic, průměr/max HDOP). Zdroj = datalog (`sats`, `hdop10`, `flags`), tedy **reálná data**
(kmitočet v logu je zatím 0 ⬅ #2, ale GPS pole jsou plná od začátku → okno je užitečné hned).
Presety 1 h / 6 h / 1 den / 7 dní. ⚠️ **Renderuje se jen při vstupu a při změně presetu** — jeden render
dělá stovky blokujících `datalog_read_back`; periodické obnovování by v UiTasku porušilo pravidlo
„žádný spin > 10 ms" (stejný vzor jako okno GRAFY). `hdop10 == 255` je sentinel „neplatné" a nesmí
se dostat do statistiky jako HDOP 25,5.

### Průvodce kalibrací napětí (s_view=40)
Vstup tlačítkem **PRŮVODCE** v okně Kalibrace. Řeší to, co ruční krokování gainu neřeší: *„přístroj
ukazuje 4,827 V — je to skutečné napětí, nebo je vedle dělič?"* Uživatel vybere větev (12V/5V), navolí
hodnotu změřenou multimetrem a firmware dopočítá gain. **POUŽÍT** ho nastaví živě (řádek „Přístroj
měření" se hned srovná), **ULOŽIT** persistuje do W25Q CALIB.
- Matematika: `sensor_update` ukládá už **přenásobenou** hodnotu, takže syrovou hodnotu ADS znát
  nepotřebujeme: **`nový_gain = starý_gain × (skutečné / zobrazené)`**. Převod je tím nezávislý na
  aktuálně nastaveném gainu.
- Guardy: nic se neměří (< 100 mV) → nedělíme; výsledný gain mimo `KALIB_ROWS` rozsah → nenabídne se.

### ⚠️ Oprava navigace (2026-08-17)
`goto_view` neznal `case 2` / `15` / `18`, přestože `nav_push(2)` se používal už dřív (GPS → SURVEY).
Spadlo to do `default` → **ZPĚT ze Self-survey vedlo na hlavní obrazovku místo zpátky do GPS okna**.
Doplněno; nová okna 38/39/40 by tu chybu jinak zdědila.
- **Okno GRAFY (s_view=29, STATUS.md #31)** — časový průběh teplot + napájení. Vstup: tlačítko **GRAFY** ve **footeru System Health** (footer proto přeskládán na 4 tlačítka: SENZORY/DIAGNOSTIKA/NASTAVENI/GRAFY nestejných šířek + BACK). Layout: vlevo dva grafy (nahoře **Teploty** — 4 série STM/MCU/OCXO/FPGA se *sdílenou* osou + legenda s aktuální hodnotou; dole **OCXO Vc** — jedna série, autoscale + overlay rozsah/okno), vpravo 5 **vertikálních bargrafů** aktuálních napájecích větví (12V/5V/REF/BAT/Vc) s **nominálním markerem** (#32). **Kombinovaný zdroj:** krátká okna (≤1 h) z **RAM decimační pyramidy** (`sensor_hist.c/h` — per-senzor, idiom `trend_feed`, plněná ze SensorsTasku 2Hz plného sweepu přes `sensor_hist_feed`, base 2 s, 4 stage ×4 → ~3,4 h, ~15 kB RAM_D1), dlouhá okna (6 h/1 den/7 dní) z **datalogu** (W25Q — jen veličiny, které datalog ukládá: OCXO+deska teplota, OCXO Vc; ostatní série se u dlouhých oken v grafu vynechají, legenda je ztlumí). Footer presety −/+ (3 min…7 dní). ⚠️ **Datalog okna se renderují JEN při vstupu/změně presetu** (klíč stabilní per preset), ne periodicky — jeden render dělá stovky blokujících `datalog_read_back` a periodické obnovování by v UiTasku porušilo pravidlo „žádný spin >10 ms" (dlouhá historie je stejně minuty/hodiny stará). RAM okna se hýbou 1×/s (`g_uptime_s`). ⚠️ Headline/statistiky jsou pořád simulace, ale tohle okno kreslí **reálné senzory** (teploty/napájení jsou skutečná HW data).
- **Okno PREHLED KANALU (s_view=30, STATUS.md #47)** — **sesterské** okno ke GRAFY (přepínač ve footeru obou, tlačítka `PREHLED >` / `< GRAFY`, přepíná **bez `nav_push`** → BACK z obou vede tam, odkud byla dvojice otevřena = System Health). Zatímco GRAFY ukazuje **časový průběh**, tohle je **aktuální stav všech kanálů jako horizontální bargrafy** s nominálním markerem + číselnou hodnotou vpravo → odchylka od očekávané hodnoty na první pohled. Dvě karty: **Teploty** (STM/MCU/OCXO/FPGA board, rozsah 0–70/90 °C, bez nominálu) + **Napájení + RF** (12V/5V/REF/VBAT/OCXO Vc s nominálem a ok/warn barvením dle ±15 % pásma, + **RF v dBm** přes `g_calib` AD8307 slope/intercept). Data **reálná** (`g_sensors[]`), refresh z `app_gpsdo_tick` s **per-řádek change-detect** (pct + **min/max** + text; flip jen při změně řádku). Bary kreslí lokální `hbar_row_draw` (ne `ui_bargraph`) — **segmentovaný** track (`HB_SEGS=45` × ~10 px, `hb_seg`/`hb_marker_idx`) s **barevnými markery** (2026-08-06): **AKT** = vyplněný úsek (zelená/amber dle ±15 % pásma) = aktuální hodnota, **REF** = accent segment na nominálu, **MIN** = violet segment na `s->min`, **MAX** = červený segment na `s->max` (peak-hold, z `g_sensors[].min/max`). Markery se kreslí PŘES výplň (viditelné i uvnitř zelené). Legenda barev `hbar_legend()` vpravo od nadpisu (kreslí se jednou). Přepočet syrové hodnoty na jednotku sdílí `hbar_disp`/`hbar_pct_disp` (RF přes `g_calib`). Popis řádků v `HBAR[]`. ⚠️ Jako u GRAFY: senzory jsou skutečná HW data, headline zůstává simulace; geometrie počítaná (tabulky fontů) — vzhled ověřit na displeji.
- **Okno MATH / LIMITY (s_view=31, STATUS.md #43/#44)** — dlaždice **„Math/Limity"** v Menu (dřív Placeholder 2). **#43 Math Mx+B + NULL/relativní:** transformace `Y = M·X + B`, pak (při NULL) `Y −= null_ref` (relativní režim, „null then measure"). **#44 limit pass/fail:** meze `lo/hi` nad výslednou Y → verdikt PASS / FAIL LO / FAIL HI. Pure-logic jádro `meas_math.c/h` (Core, `g_meas_cfg` + `g_meas_verdict`, selftest 8/8). UI: karta A (X měřeno, Y výsledek, tlačítka MATH/M-cyklus/B±/NULL), karta B (verdikt badge, Lo/Hi, PÁSMO ±, LIMITY/ALARM přepínače). **Limity se nastavují jako pásmo kolem aktuální Y** (`math_recenter_limits`: lo/hi = Y ± preset pásmo; recenter při změně math/null/pásma). **⚠️ Průběžné vyhodnocení běží i mimo okno** (`app_gpsdo_tick_stats_sample` → `g_meas_verdict`), takže **alarm hlídá limity na pozadí**: `alarm.c` beepne na hranu PASS→FAIL (4 pípnutí) / FAIL→PASS (1), armuje se jen skutečným PASS (zapnutí limitu na už špatné hodnotě nepípne), respektuje mute; počítadlo `g_alarm_limit_fail`. ⚠️ **Aplikuje se na `screen_main_freq_hz()` = DNES SIMULACE headline** → plný smysl po reálném SPI linku (#2). **Persist v syscfg flash blobu** (`g_meas_cfg` = math/null/limit/alarm flagy + m/b/null_ref/lo/hi jako doubles; magic `"SCF6"`→`"SCF7"`) — NE v BKP → `syscfg_load` aplikuje vždy (jako fx/anim), UI preset indexy (M, pásmo) se dopočtou z cfg při otevření okna (`math_sync_idx`). Formátování Hz bez `%f` (`fmt_hz`, integer extrakce, nano.specs).
- **Okno SELF-SURVEY (s_view=32, STATUS.md #53)** — tlačítko **SURVEY** v GPS okně (footer pravý sloupec). Firmwarové **Welford průměrování** lat/lon/alt z platných fixů → **horizontální rozptyl [m]** = konvergence (klesá s N); START/STOP. `survey_accumulate` běží na pozadí v `app_gpsdo_tick` (i mimo okno). START pošle i **UBX-CFG-TMODE2** survey-in (`gps_survey_in_cmd`) — ⚠️ best-effort, účinné jen na timing-grade RX (NEO-7M nejspíš NAKne = neškodné); firmwarové jádro je HW-nezávislé. **Persist:** STOP uloží výsledek (mean lat/lon/alt + rozptyl + N) do `g_survey_*` → syscfg flash (magic „SCF8") → přežije power-cycle; okno při otevření ukáže poslední uloženou polohu (dokud se nespustí nový survey).
- **Okno SESTAVY (s_view=33, STATUS.md #54)** — tlačítko **SESTAVY >** ve footeru Nastavení. **8 číslovaných slotů** (uložit/načíst profil nastavení: jas/téma/jazyk/zvuk/auto-dim/zóna/UI cfg/efekty/animace + Math+limity) do W25Q **SETUP regionu** (`setup.c/h`). Všech 8 slotů = **JEDEN blob** přes `w25q_store` (wear-leveled, CRC, power-safe). Slot −/+, ULOŽIT/NAČÍST/SMAZAT; NAČÍST aplikuje i téma/jas (`ui_theme_select`+`screen_main_invalidate`+`init`). `setup_init()` v `app_gpsdo_init` (po `calib_load`).
- **Warm-up / stabilizace OCXO (STATUS.md #52)** — `warmup_ready()` (v okně Holdover): „hotovo" = uptime ≥ 300 s **A** tepelný sklon `|dT/dt| < 0,08 °C/min` (z RAM historie 0x49 přes `sensor_hist`). Nahradilo naivní `uptime<180` ve stavu **WARMUP**; OCXO řádek ukazuje `45.2 C +0.03/m` (fialově dokud náběh).
- **Autokalibrace / self-check (`autocal.c/h`, STATUS.md #68, ROZPRACOVÁNO)** — UART `autocal` + tlačítko **AUTO-CAL** v okně Kalibrace (výsledek do status řádku). Dnes **verifikace (guard-band)** z `g_sensors`: VREF ~2,5 V, 12V/5V ±5 %, VBAT → PASS/WARN/FAIL (nejhorší = celkový verdikt). **Staged** (jasně označené, zatím se NEprovádějí — nemění koeficienty): ADC3 HW self-cal (⬅ ADC3 vlastní SensorsTask → nutný koordinovaný reinit), timebase vs GPS (⬅ #2), RF slope/intercept (⬅ externí RF reference = manuální řádky Kalibrace). Bezpečné — **nic nezapisuje do HW**.
- **Bargraf — vertikální mód (`ui_bargraph.c/h`, #32):** `ui_bargraph_t` má nově `vertical` (0=horizontální default, 1=vertikální — segmenty zdola nahoru, jednotná barva `color`, text nekreslí — umísťuje volající), `segs` (0=default 20; jinak N = jemnější granularita) a `nominal_pct` (≤0=žádný; jinak segment na té hodnotě zvýrazněn accentem → odchylka od očekávané hodnoty vidět na první pohled). **Horizontální fast-path `ui_bargraph_update` (main screen RF bar, anim demo) beze změny** (fixních 20 segmentů).
- **Grafické efekty — sada 2026-07-24, řízená bitmaskou `g_fx_enabled` (`fx_flags.h`, bezzávislostní header sdílený firmware+app).** 6 vizuálních efektů (dřív 7 — `FX_HEAD_GLOW` odstraněn 2026-07-26 na přání uživatele, `FX_ALL`=0x3F; syscfg maskuje `& FX_ALL` při load i save → zbytkový bit ze starého blobu se sám ztratí, bez magic bumpu), každý samostatně ZAP/VYP přes okno **EFEKTY** (`s_view=27`, footer tlačítko „EFEKTY >" v okně Animace vedle „PŘÍKLADY"): tlačítka `UI_BUTTON_RUN`(zelená=ZAP)/`UI_BUTTON_STOP`(červená=VYP), tap přepne bit. **Persist JEN v syscfg flash blobu** (magic `"SCF4"`→`"SCF5"`) — NE v BKP (DR6 nemá pod magicem dost volných bitů), proto `syscfg_load` čte flash i při warm resetu a `g_fx_enabled` aplikuje vždy. Default vše ZAP. Bity: **`FX_WATERFALL`** (spektrogram, s_view=26), **`FX_ALLAN_CONF`** (konfidenční pás ADEV ~1/√(2·párů), `allan_band_fill`), **`FX_HOLD_CONE`** (holdover drift kužel, `holdover_cone_draw` — šířka/barva dle stavu LOCK/HOLDOVER/WARMUP/NO-LOCK), **`FX_OCXO_GAUGE`** (analogový budík Vc=AIN0 v Holdoveru, `ocxo_gauge_draw` půlkruh + barevné zóny), **`FX_SPARK_FILL`** (area-chart výplň pod trend sparkline, `spark_fill_below` v libui/sparkline.c), **`FX_SYS_XFADE`** (plynulé prolínání barvy SYS pilulky, `screen_main_tick_sys_xfade` @20 Hz — barvy přes `ui_pill_variant_colors` + `override_style` v ui_pill_t). ⚠️ Efekty přes `prim_internal_blend_px` (glow/arc/per-column fill) obcházejí `mark_dirty` → každý MUSÍ začít clear (fill/blit REPLACE) kvůli copy-forwardu. **Nová libui komponenta `ui_segmented_t`** (`segmented.h/.c`, přidána do `ui/ui.h`): N-segmentový přepínač, vybraný = accent pilulka, text `UI_COLOR_BG_0` (kontrast v obou tématech) — použit pro **TDEV/MTIE přepínač v okně ALLAN** (`ALLAN_METRIC_RECT`, `screen_main_set_allan_metric` 0=ADEV/1=TDEV/2=MTIE; TDEV=τ·ADEV/√3, MTIE≈√3·τ·ADEV = ODHAD bez uložené fáze; metrika mění křivku + Y osu s dynamickými popisky `allan_ylabel`, σy(τ) tabulka zůstává ADEV referencí). **Spektrogram Δf** (s_view=26): heat strip X=čas, barva=odchylka `screen_main_freq_dev_unit()`, wrap styl (1 sloupec/vzorek @2 Hz). **Od 2026-07-25 ZÁLOŽKA** (sdílený segmented `VIEW_TABS` ve footeru vlevo) vedle **ALLAN** (23) a **HISTOGRAM** (6) — sesterská trojice přepínaná BEZ nav_push; Menu dlaždice zrušena (zpět Placeholder 2), metrika ADEV/TDEV/MTIE + LOGY přesunuty do středu footeru. **Status ribbon demo** (s_view=28, footer tlačítko `STATUS RIBBON >` v okně EFEKTY — 2026-08-29, dřív dlaždice Menu): LED chipy GPS/FPGA/REF/SENS. **⚠️ Headline + statistiky jsou pořád SIMULACE** → spektrogram/kužel/konfidenční pás kreslí šum, ne realitu, dokud se nenapojí reálná FPGA data.
- **⚠️ Navigace = zásobník** (`s_nav_stack`/`nav_push`/`nav_back` v app_gpsdo.c). Každý forward přechod pushne aktuální s_view; **BACK (`nav_back`) se vrací k tomu, ODKUD bylo okno otevřeno** — takže okno otevřené z Menu → BACK → zpět do Menu; podpora vnoření (Menu→Nastavení→O přístroji→BACK→Nastavení→BACK→Menu, Diagnostika→DIAGRAM→BACK→Diagnostika). `app_gpsdo_render_main` resetuje zásobník (kořen). `goto_view` renderuje návratový cíl — `case` má pro každé okno, které spawnuje podokno (1/2/3/7/12/15/18/24/**27**/35/36/37/42/43). ⚠️ Nové okno, které volá `nav_push(X); render_Y()`, MUSÍ dostat `case X` v `goto_view`, jinak BACK spadne na hlavní obrazovku.
- **⚠️ REORGANIZACE — 3. iterace 2026-08-29: MENU = nástroje/měření/monitoring, NASTAVENÍ = jen konfigurace.**
  - **NASTAVENÍ (`s_view=7`) = mřížka 3×3 = 9 dlaždic** (w=246, x=18/278/538, y=68/154/240):
    DISPLEJ · Jazyk · ALARMY · CAS · SIT · KALIBRACE · ANIMACE · SESTAVY · O PRISTROJI.
    Jazyk je **přepínač** (label nese stav), zbytek naviguje. Přímé ovládače (jas/auto-dim/vzhled/
    rozložení) žijí v podokně **DISPLEJ** (`s_view=36`), mute v **ALARMY**.
  - **Z Nastavení PRYČ do Menu** (jsou to nástroje, ne nastavení): **Reference Si5356**, **Benchmark
    pamětí**, **SD karta**.
  - `⚠️ Eased jas bar tiká v `s_view==36`, ne v 7. Okno DISPLEJ nese i přepínač rozložení
    (HYBRIDNÍ ↔ KLASICKÉ, `LAYOUT_RECT`).
  - *(Předchozí „1. iterace" (2×5 mřížka vpravo) a „2. iterace" (3×4) jsou v `docs/CLAUDE_ARCHIV.md`.)*

- **Okno SÍŤ (`s_view=35`)** — DHCP ZAP/VYP + statická IP/maska/brána (výběr pole a oktetu,
  `−/+` po jednotkách s wrapem 0..255, bez přetečení do sousedního oktetu). Persist v syscfg
  blobu (magic **„SCF8" → „SCF9"**).
  - **Karta „Stav linky" je od F5 (2026-08-22) ŽIVÁ** (`s_view==35` v `app_gpsdo_tick`,
    per-řádek change-detect): **Link** (UP + rychlost/duplex / DOWN / „ETH init selhal
    (REF_CLK?)" / „CM4 nenabehla"), **IP adresa** přidělená DHCP serverem (accent barvou;
    „ceka na DHCP..." dokud nepřijde) a **PHY** (`LAN8742A`). Hodnoty pochází z lwIP na CM4
    přes IPC (`ipc_cm4_net`) — CM7 na síti sám nedělá nic. Totéž podrobněji v UART `status`.
    ⚠️ Bez linky se **neukazuje stará IP** (jen `--`), aby okno nelhalo.
  - ⚠️ **Konfigurace (DHCP ZAP/VYP + statická adresa) se pořád JEN UKLÁDÁ** — reálně jede
    vždy DHCP. Chybí přenos: `g_net_dhcp`/statická adresa žijí v syscfg na CM7, ale
    **IPC snapshot ta pole nenese**, takže se k nim CM4 nedostane. Doplnit spolu
    s rozšířením snapshotu; pak `g_net_dhcp` → `dhcp_start()`, jinak `netif_set_addr()`.
    **DHCP klient se psát nebude — lwIP ho má** (`LWIP_DHCP`).

**MENU** (footer tlačítko na hlavní obrazovce, `b==4`) → **Menu rozcestník** (`app_gpsdo_render_menu`, s_view=12). **REORGANIZACE 4. iterace 2026-08-29 — 3 úrovně, top Menu jen 4 velké dlaždice:**
  - **TOP MENU** = 4 dlaždice 346×160 (x=44/410, y=68/240): **Nastavení >** · **System Health** · **Diagnostika >** · **Měření >**. `MENU_ITEMS[MENU_N=4]` = `{rect,label,void(*fn)(void)}` (bez enumu/switche). Restart = footer tlačítko vedle ZPĚT.
  - **MĚŘENÍ (`s_view=44`, `app_gpsdo_render_meas_menu`)** — podrozcestník 3×4 = 12 dlaždic (`MEAS_ITEMS`, geometrie jako dřívější top Menu: 14/276/538, 248×76, y=68/154/240/326): **Čítač** (→ hl. obrazovka s_view=0 = klasické velké číslo, `app_gpsdo_render_main`) · Prezentace (34, #67) · Analýza (41) · **Čítač detail** (19, FPGA reciproké — dřív dlaždice „Čítač") · **Math/Limity** (31, na místě dřívějšího „Allan detail") · Histogram (6) · Holdover (16) · Datalog (17) · Kvalita GPS (38) · **TI 1PPS (45)** · **Dvojkanál (46, kanály /4 a /16 NAD SEBOU, plná šířka)** · **Odchylka ×N (47)**. ⚠️ **„Allan detail" už NENÍ dlaždice** — okno ALLAN (s_view=23) se otevírá **rozklikem Allan náhledu na hlavní obrazovce** (`screen_main_hit_allan`).
  - **NÁSTROJE (`s_view=48`, `app_gpsdo_render_tools`)** — z **footeru Diagnostiky** (`NASTROJE > · ZPĚT`, `DIAG_TOOLS_BTN_RECT`; DIAGRAM/PAMET/SELFTEST tlačítka zrušena). Mřížka 3×2 = 6 (`TOOLS_ITEMS`, 248×96, y=92/214): Blokové schéma (21) · Paměť (5) · Selftest (20) · Benchmark (43) · SD karta (37) · Reference Si5356 (14).
  - **Nové měřicí funkce** (v `app_gpsdo.c`, dlaždice v MĚŘENÍ):
    - **TI 1PPS (s_view=45)** — placeholder. Time-interval OCXO vs GPS 1PPS potřebuje HW/FPGA (1PPS do FPGA + `time_error_ns` v protokolu v2, STATUS #36). Okno jen popisuje závislost.
    - **Dvojkanál (s_view=46, `render_dualch`)** — **`CH A` (= odbočka /4, `frequency_x100000`) a `CH B` (= /16, `freq16_x100000`) NAD SEBOU**, dvě karty na plnou šířku (764×178 na y=52 a y=236). V každé: kmitočet `mono_30` + jednotka „Hz" `sans_18` zvlášť (⚠️ mono_30 nemá 'H'/'z'), **RF bargraf s číselnou hodnotou** („−61.2 dBm", 40 segmentů) a **vlastní stavový řádek** (CH A čte `FPGA_ERR_MEAS`, CH B `FPGA_ST2_DIV16_ERR`; SIGNAL_LOST/overflow/link jsou společné). RF je jedna hodnota do obou karet — HW má **jeden** AD8307 (ADS AIN1). Živé (2 Hz tik), šedý kmitočet při `g_freq_stale`. ⚠️ `dualch_bar` **musí clearovat před renderem** — `ui_bargraph_render` value text jen kreslí, nečistí. Svislý rozpočet karty je v komentáři u `DUALCH_CA_Y`.
    - **Odchylka ×N (s_view=47, `render_devmult`)** — měřič kmitočtové odchylky se **zesílením chyby jako ADRET 4110**: `df = f − f0`, velké číslo = `df × N` (N cyklus ×1..×1e6, `DM_MUL_RECT`), vedle `df` v Hz + ppb + ppm. **NUL** (`DM_NUL_RECT`) nastaví `f0 = screen_main_freq_hz()` (nulování jako Adret). Čistě SW nad headline; `f0` se při vstupu bere z `screen_main_freq_nominal()`. Nepersistuje.
  - ⚠️ **NEJSOU dlaždice:** Senzory = footer System Health (`SENZORY · GRAFY · ZPĚT`); Kalibrace/Cas/Sit/Alarmy/Animace/O přístroji = dlaždice Nastavení; **Status ribbon** demo = footer EFEKTY. Restart → confirm okno (s_view=13).
  - ⚠️ **Nové okno, které volá `nav_push(X); render_Y()` MUSÍ dostat `case X` v `render_view`** (konec `app_gpsdo.c`; sdílí ho nav_back). Screensaver-exit má **vlastní konzervativní switch** (jen levná okna) — `render_view(s_prev_view)` tam zamrzl touch.
- **Animace — sada 2026-07-21, řízená globálním přepínačem + `anim.h`.** `anim_t{cur,target}` + `anim_reset/anim_set/anim_step(a,k,snap)` (ease-out: `cur += (target−cur)·k`, `snap` = práh pro dorovnání a zastavení překreslování) je od tétorevize **sdílený header `CM7/app/anim.h`** (`static inline`, ne `static` v jednom `.c`) — používá ho jak `app_gpsdo.c`, tak `screens/screen_main.c` (dvě různé translation units). **Globální přepínač `g_anim_enabled`** (perzist `BKP_DR6` bit8 + syscfg blob pole `anim_en`, magic bump `"SCF2"`→`"SCF3"`) žije v **okně Animace** (Nastavení → dlaždice „ANIMACE", `ANIM_TOGGLE_RECT`) — `anim_step` sám o sobě při VYP okamžitě skočí na cíl, takže VŠECHNY dále popsané animace se bez zásahu volajícího chovají jako dřív (žádná per-volání kontrola flagu). Plynulost všude táhne **vlastní rychlý tik `app_gpsdo_tick_anim` @ ~20 Hz** (vedle `tick_freq` ve `freertos_task_ui.c`), dispatchovaný podle `s_view` — NE 2 Hz `app_gpsdo_tick`. Konkrétní animace:
  - **Okno Animace/demo** (s_view=24, dlaždice „ANIMACE" v Nastavení): demonstrace `anim_t` na **RF bargrafu** (`ui_bargraph_update` = jen změněné segmenty + value text v clearnutém boxu). Cíl skáče (reálný RF z `g_sensors[SENS_ADS1]`, nebo bez signálu demo sekvence úrovní ~2 s/krok), aktuální ho plynule dojíždí. **Footer tlačítko „PRIKLADY ANIMACI >" (`ANIM_DEMO_RECT`) → subokno níže.**
  - **Subokno „PRIKLADY ANIMACI"** (s_view=25, `app_gpsdo_render_animdemo`/`tick_animdemo`, `nav_push(24)` → BACK zpět do Animace, proto `goto_view` má `case 24`): **6 dlaždic, každá jeden typ animace, vše běží NEPŘETRŽITĚ ve smyčce** z `app_gpsdo_tick_anim` (~20 Hz, vlastní frame counter). Dlaždice: 1 ease-out bar, 2 pulzující LED (radius+barva OK/WARN/BAD), 3 flash tlačítka (accent obrys periodicky), 4 eased číslo, 5 zvýraznění poslední číslice, 6 fade text. ⚠️ **Záměrně NEZÁVISÍ na `g_anim_enabled`** — je to ukázka, hýbe se i při vypnutých animacích (vlastní raw ease `ad_ease`, ne `anim_step`). Účel: ověřit každou animaci v izolaci (flash na reálném tlačítku trvá jen ~150 ms, těžko se trefit). Každý tik plný clear+redraw vnitřku každé dlaždice (BG_CARD REPLACE → mark_dirty → copy-forward OK).
  - **Flash tlačítek/pilulek při stisku = DVA mechanismy.** (a) **In-place footer RUN/GATE/CHAN** na hlavní obrazovce: `screen_main_button_flash_start/tick` (neblokující, obrys odezní přes 3 tiky). (b) **In-place přepínače v oknech** (NE navigační): `tap_flash(rect)` / `tap_flash_pill(rect)` v `app_gpsdo.c` (tenké obaly nad `tap_flash_r(rect, rad)`) — **2px accent OBRYS** přes prvek (`rad` = tvar dle prvku: `UI_DIM_BUTTON_RADIUS` vs `UI_DIM_PILL_RADIUS`). Obrys se kreslí PŘES už vykreslený prvek → **text zůstává** (nepřekresluje se), netřeba label ani re-render obsahu. **Volá se JEN u 4 in-place přepínačů, které po stisku ZŮSTANOU na obrazovce** (Animace `ANIM_TOGGLE`/`ANIM_DIGIT`, Čas `TZ_AUTO`, Trend −/+) — tam se dotčené tlačítko hned překreslí (`anim_toggle_redraw`/`cas_upd_mode`/`render_trend_scale_btns`), takže obrys (jde přes `prim_internal_blend_px`, mimo dirty-rect copy-forward) je pokrytý. **⚠️ U NAVIGAČNÍCH tlačítek + pilulek se flash ZÁMĚRNĚ NEvolá (2026-07-22):** problik PŘED navigací stojí vždy ≥1 panel-frame (~17 ms), protože musí být vidět dřív, než se flipne cílová obrazovka → jen zdržoval přepnutí. Odezva navigace = samotná okamžitá změna obrazovky. In-place RUN/GATE/CHAN na hlavní obrazovce mají vlastní `screen_main_button_flash` (kreslen ve stejném snímku jako akce → zdarma). Gate `g_anim_enabled` (u ANIM/DIGIT toggle při VYP→ZAP problik nesvítí — flag je při čtení ještě 0).
  - **Eased jas v Nastavení** (`settings_tick_jas`, s_view=7): HW backlight (`g_brightness`) se mění OKAMŽITĚ (žádný lag stmívání), jen vizuální bar na obrazovce plynule dojíždí k nové hodnotě (`settings_draw_jas_bar`, boxbased partial redraw).
  - **Eased Offset/σ@1s/Drift** (`screen_main_tick_stats_anim`): tři `anim_t` nad *raw* hodnotami (před `fmt_frac`), value-only partial redraw (`draw_stat_card_value`). `stats_anim_resync()` (voláno z `render_body_grid` PŘED plným renderem) zajistí, že návrat na hlavní obrazovku z podnabídky ukáže číslo okamžitě (ne zastaralé nedojeté).
  - **Eased trend sparkline** (`screen_main_tick_trend_anim`, jen v2): `s_spark_prev[]` → `s_spark[]` (cíl) interpolace přes `TREND_ANIM_STEPS=20` kroků, `trend_plot_draw()` sdílí kreslení sigma-band+polyline+regrese mezi plným 1Hz renderem a 20Hz tikem. `trend_anim_resync()` (stejný vzor jako stats) + reset při změně počtu bodů `n` (jinak by interpolace mezi různě dlouhými poli byla nesmysl).
  - **Micro-flash tlačítek na hlavní obrazovce** (`screen_main_button_flash_start/tick`, s_view=0): 2px accent obrys tlačítka na `BTN_FLASH_FRAMES=3` tiků po stisku (leží celý uvnitř dirty rectu, který už zavolal `screen_main_redraw_button`).
  - Zvýraznění změněné číslice v headline bylo **ODSTRANĚNO 2026-07-25** (na přání uživatele) — celý freq-flash mechanismus + přepínač `g_digit_anim_enabled`. Číslice se překreslují bez podbarvení. Detaily v git historii.
  - **Pulsující stavová LED** (`cd_pulse_tick`, blokové schéma komunikace s_view=21): uzly ve stavu BAD/WARN "dýchají" (trojúhelníková vlna poloměru 4..6..4 px, perioda ~1,2 s); OK/ACC uzly zůstávají statické. Vyžaduje skutečný clear před každým překreslením (kruh mění poloměr, ne jen barvu → netriviální jako digit-highlight). ⚠️ **Clear box MUSÍ pokrýt MAX poloměr + AA okraj symetricky** (16×16 = ±8 od středu): AA ramp `prim_fill_circle` sahá na radius+1, takže menší box nechá on-axis/AA pixely většího poloměru jako „duchové čárky" vpravo/dole od LED (opraveno 2026-07-23, dřív 12×12). U širokých uzlů (FPGA GW1NR-9) zasahuje popisek do LED rohu → po clearu se `CD_LABEL[i]` obnoví (clip na box, stejné pořadí jako `cd_node`: kruh, pak text).
  - **Spinner u ULOŽIT v Kalibraci** — ⚠️ **jen statická ikona**, ne skutečný víceframový spin: `calib_save()` je jedno blokující volání (`w25q_store_write` = erase+payload+hlavička bez yield bodu) a UiTask je po tu dobu jednovláknově uvnitř něj, takže opravdová animace během zápisu by vyžadovala zásah do sdíleného (power-safety kriticky seřazeného) `w25q_store.c` — záměrně se do toho nešlo bez explicitního zadání.
  - **Boot splash fade-in** (`splash_draw_content`, `app_gpsdo_boot_splash_tick`): využívá existující jednorázovou smyčku 10×`osDelay(100)` v `StartUiTask` (PŘED hlavní smyčkou, ne za běhu) — barvy lineárně interpolované černá→cíl (`fade_color`) přes `SPLASH_FADE_TICKS=8`; na rozdíl od ostatních položek dělá **plný** redraw každý tik (bezpečné, protože je to jen 8× při bootu, ne steady-state).
- **SYS pilulka barevně** (`compute_sys_level` v screen_main.c): agregace VŠECH chyb → zelená „SYS OK" / amber „SYS !" / červená „SYS ERR". **AMBER** (degradace, funguje) = FPGA SIGNAL_LOST/no-link, Si5356 nečteno/kalibrace, sensor err_streak, watchdog/crash reset (zotaveno), CM4 absent. **RED** (kritické) = **Si5356 LOS_CLKIN (bit3!) nebo PLL_LOL** (ztráta 10 MHz reference — LOL se při fyzické ztrátě vstupu neasertuje, proto je bit3 nutný), selftest FAIL. **Bit2 = LOS_XTAL se záměrně NEhodnotí** (bez krystalu trvale 1 — status `0x04` je normální stav této desky). `screen_main_sys_poll` (v tick_clock) překreslí header při změně úrovně.
- **Trend fullscreen — časové okno až 60 DNÍ přes decimační pyramidu** (tlačítka `−`/`+` krokují presety **1 min / 10 min / 1 h / 6 h / 1 den / 7 dní / 30 dní / 60 dní**). ⚠️ Plochý ring `s_y[]` pokrývá jen 120 s — dlouhá okna kreslí **vlastní decimační pyramida `s_tr[]`** (`trend_feed`, stejný princip jako ADEV pyramida, ale **decimace ×4** místo ×10 a delší ringy → hladší křivka): 9 stagí, stage s má krok 4^s s a rozsah 128·4^s (s=0: 1 s/128 s … s=8: 65536 s/97 dní), paměť ~4,7 kB v RAM_D1. `screen_main_render_trend_big` vybere **nejjemnější stage, který okno pokryje** (`tr_pick`); pokud vyšší stage ještě nemá data (stage 8 se plní až po ~18 h běhu), **spadne na nejvyšší stage s ≥2 vzorky** — vždy je vidět něco místo „Waiting". Overlay ukazuje okno · skutečně pokrytý čas · krok decimace. ⚠️ `TREND_PRESETS` musí být **`int32_t`** (60 dní = 5 184 000 s přeteče int16).
- **⚠️ Radiální gradient pozadí — rychlý isqrt** (`gradient.c`): per-pixel vzdálenost byla dříve lineární hledání `while ((d+1)²≤d2) d++` = až ~540 iterací/pixel × 384k px → **stovky ms** (citelné hlavně při přepnutí schématu = rebuild `bg_cache`). Nahrazeno Newtonovým `isqrt32` (~O(log), pixel-identické) → ~40× rychleji. Rect pilulek se zachytává v `render_header` (`s_gnss_pill_rect`/`s_sys_pill_rect`), `screen_main_hit_gnss/sys` testuje zásah; `screen_main_hit_allan/hit_trend` = tap na Allan/trend kartu → fullscreen okno („↗" náznak v hlavičce karet):
  - **GNSS pill → GPS/GNSS okno** (`app_gpsdo_render_gps`, s_view=2): **ŽIVÉ** (refresh ~2×/s v `app_gpsdo_tick`, first/values split jako diag). **NEsymetrické sloupce** (`GPS_LX/LW/RX/RW` makra): levý široký (502 px) = FIX + Družice, pravý úzký (250 px, ~⅔) = Čas/Poloha/Lokator/Přijímač. **Řádek FIX**: „FIX 3D" (mono_25) vlevo + **TP 100 kHz/10 Hz** vpravo (timepulse přesunut z vlastní karty; s fixem 100 kHz = GPSDO PLL ref disciplinovaná na GNSS, bez fixu 10 Hz = holdover indikátor). **Karta Lokator** (bývalá TIMEPULSE): „Locator JN89NS85KN" = 10-znakový Maidenhead grid (`fmt_locator` z lat/lon, mono_18 accent). Čas+datum z **RTC** (UTC), poloha lat/lon/alt (mono_16), HDOP/PDOP, **Přijímač** = „NEO-7M" v headeru + živé `Vet:`/`Fix:` (z `g.sentences`/`g.fixes`). **Karta Družice = JEDNO zobrazení na plnou šířku, přepínatelné DOTYKEM** (`s_gps_polar`, tap na kartu `GPS_SAT_RECT`): **bargraf C/N0** (default, až 14 nejsilnějších, C/N0 nad + PRN pod sloupcem) ↔ **polární sky plot** (kruh r=86, 3 elevační kružnice + N/S/E/W kříž, tečka=družice: azimut 0=sever po směru hodin, poloměr ∝ 90−elevace = zenit ve středu, PRN vedle tečky). Barvy zelená/žlutá/červená dle C/N0. Data z GSV: `parse_gsv` plní `g.sats[]` PRN+elev+**azim**+C/N0+**`constel`**; `gps_sat_t`/`GPS_MAX_SATS`(=24) v `gps.h`. **Změnový klíč = mód + hash az/el/snr VŠECH družic** (tečka/bar se pohne i u slabé). **GSV je multi-souhvězdí (per-talker akumulace):** GPGSV/GLGSV/GAGSV/GBGSV se skládají do samostatných `done[GPS_CONSTEL_N]` a merge je spojí do `sats[]` (GPS first) — dřív jeden akumulátor, takže GLGSV (msgnum=1) vynuloval právě nasbíraný GPGSV. Jádro `gsv_feed`/`gsv_merge`/`gsv_constel` je bezstavové (kryté `gps_selftest`). Pole `constel` = `gps_constel_t` (GPS/GLONASS/Galileo/BeiDou) → **PRN v bargrafu i sky plotu nese RINEX prefix** (`G05`/`R68`/`E12`/`C07`, tabulka `k_constel_ltr[]="GREC"`); barva tečky/sloupce zůstává dle C/N0 (prefix je jen textový, sílu signálu nepřebíjí). Změnový klíč zahrnuje `constel`. **GLONASS se zapíná na HW přes UART `gps glonass`** (`gps_config_gnss`, UBX-CFG-GNSS GPS+SBAS+QZSS+GLONASS) — best-effort, NEvolá se z `gps_init` (nejde ověřit bez HW, špatný blok by vypnul GPS); NEO-7M může NAKnout, parser GLGSV zvládá tak jako tak. Viz [[gps-todo]].
  - **SYS pill → System Health okno** (`app_gpsdo_render_health`, s_view=3): **živé** (refresh 2×/s v `app_gpsdo_tick`, stejný first/values split jako diag). RTOS (heap/CPU), **volný stack tasků** (`osThreadGetStackSpace`, byty; <64 B → červený `!`), I2C chybovost (agregace `g_sensors[].err_total/streak`, **0x4A vyřazen** = neosazen), linky (FPGA/Si5356/senzory n/10), karta **System**: „Power supplies: OK/Unkn/FAIL" (verdikt z 12V/5V ±10 %, konkrétní napětí jen v Diag/SENZORY), Uptime, „Reset: <příčina>" (červeně při IWDG/crash), „Selftest: PASS/FAIL". **SENZORY > podmenu** (`app_gpsdo_render_sensors`, s_view=4) = přehled **aktuálních hodnot** všech 10 senzorů, dvousloupcově (vlevo Teploty, vpravo Napětí), `dlabel`+`dval` jako diag. Min/max/avg/err jen přes UART `sensors`. `osThreadGetStackSpace` jen při otevřeném okně (scan stacku nezatěžuje běžný provoz). **PAMET podokno** (`app_gpsdo_render_mem`, s_view=5, **tlačítko ve footeru Diagnostiky** — přesunuto 2026-07-18 z Health/Menu) = využití interní FLASH/RAM (linker symboly), RTOS heap (used/total), SDRAM 32 MB, W25Q 64 MB (JEDEC). **NASTAVENI podokno** (`app_gpsdo_render_settings`, s_view=7, tlačítko v Health footeru, **dvousloupcové**): vlevo mute zvuku (ikona `ui_icon_speaker/_muted`), jas −/+ (bargraf + %, clamp 25..255), auto-dim zap/vyp + prodleva −/+ (presety 15..600 s); vpravo **Vzhled** (cyklus 5 schémat TMAVÉ/SVĚTLÉ/STŘEDNÍ/OBRYS/KONTRAST — runtime přepnutí palety; dnes žije v okně DISPLEJ s_view=36), **Jazyk** (ČESKY/ENGLISH — zatím jen infrastruktura `g_lang_en`), **REFERENCE Si5356 >** (`REF_RECT`, s_view=14 — přesunuto 2026-07-18 z Menu dlaždice) a **O PRISTROJI >**; časová zóna má VLASTNÍ okno **Cas** (dlaždice v Menu, s_view=22). Persist DR2+DR6 přes `g_sys_cfg_dirty`. Statické okno (není v ticku, překreslí se při tapu).
  - **Barevná schémata (libui/src/theme.c):** `UI_COLOR_*` makra derefují ukazatel `g_ui_theme` (runtime tabulka) → přepnutí `ui_theme_select(idx)` NEVYŽADUJE změnu volajících. ⚠️ `UI_COLOR_*` proto NELZE použít ve file-scope `static const` inicializátorech. Po přepnutí NUTNÉ `screen_main_invalidate()` + `screen_main_init()` (bg_cache je předrenderovaná ve starých barvách) — dělá to THEME handler v `app_gpsdo_handle_touch`. Uložené schéma aplikuje `app_gpsdo_init` před prvním renderem. **5 schémat (2026-08-19, index `UI_THEME_*` 0..4, persist `g_theme_idx`):** **0 TMAVÉ** · **1 SVĚTLÉ** · **2 STŘEDNÍ** · **3 OBRYS** · **4 KONTRAST**. Schémata 2/3 jsou **varianty tmavého**, lišící se JEN vzhledem **NORMAL tlačítek** (GATE/CHAN/MENU/PERIOD + navigační): STŘEDNÍ = světlejší VÝPLŇ (tlačítka „vystoupí" z pozadí), OBRYS = jen světlejší RÁMEČEK (výplň zůstává tmavá). **KONTRAST = samostatná plná paleta** (černé pozadí, bílý text, syté jasné akcenty, tlačítka = černá výplň + jasný barevný obrys) pro max. čitelnost. V TMAVÉM schématu jsou NORMAL tlačítka nově **laděná do (světle) modra** (`btn_norm` = navy výplň + jasný sky rámeček); **ACTIVE zjasněno** (sytější modrý podklad + accent rámeček), aby vybraný stav vystoupil nad zesvětlalá NORMAL. ⚠️ NORMAL tlačítko má proto **vyhrazené role `btn_norm_top/border`** (`UI_COLOR_BTN_NORM_*`) — dřív bralo přímo `BG_1`/`LINE` (splývalo s pozadím). ⚠️ **Tlačítka = JEN plná výplň `_top` + rámeček `_border`** (dřívější `_bot` role všech variant byly mrtvý kód — `ui_button_render` čte jen výplň; gradient se záměrně nepoužívá, bleedoval by přes zaoblené rohy; odstraněno 2026-08-19). Varianty 2/3 se **staví za běhu** kopií `THEME_DARK` + override `btn_norm_*` (`build_variants` v theme.c); KONTRAST je statická `const` paleta (`THEME_HICONTRAST`). Tlačítko **Vzhled** v okně DISPLEJ (s_view=36) je **cyklické** (tap → další z 5, `THEME_LABELS[]` = jen názvy bez prefixu „VZHLED:", jinak by „KONTRAST" 16 zn. přetekl 200px tlačítko při mono_22 13px/zn.). **Persist = 3 bity:** BKP_DR6 **bit0** (nejnižší, = původní „světlé") **+ bity9:10** (vyšší dva, `g_theme_idx>>1`); staré záznamy měly bity9:10=0 → 0/1 = tmavé/světlé jako dřív (zpětně kompatibilní, žádný magic bump). Zrcadlí i syscfg blob (`theme_idx`, clamp `&0x07`) a setup profil.
  - **ALLAN okno** (`app_gpsdo_render_allan`, s_view=23, **tap na Allan náhled** na hlavní obrazovce — ztlumené „↗" za titulkem karty značí klikatelnost, `screen_main_hit_allan`): **velký log-log graf σy(τ)** (`screen_main_render_allan_big`: Y dekády 10⁻⁶..10⁻¹⁰ s popisky mono_16, X dynamické dekády τ 1 s..100k+ s, křivka z ADEV pyramidy + markery) + vpravo **σy(τ) tabulka** (sdílená `screen_main_render_stats_table`). **Karta na hlavní obrazovce (364×242, přes celou výšku mřížky) má plné osy v mono_14** + živou σy@1s v headeru; tohle okno je totéž ve větším (mono_16). Karta i okno sdílí `allan_plot(area, big)` → `adev_points()` + `allan_plot_curve()` (`ALLAN_Y_MIN/ALLAN_Y_DEC`).
  - **Histogram okno** (`app_gpsdo_render_histogram`, s_view=6, **tlačítko HISTOGRAM v okně ALLAN** — sesterská okna: v histogramu je zpět tlačítko ALLAN; **přepínání je BEZ `nav_push`**, takže ZPĚT z obou vede tam, odkud byla dvojice otevřena, ne k sobě navzájem): vlevo **histogram rozdělení y=(f−f₀)/f₀** (24 binů, auto-range, mean=zelená + medián=amber čára, **Gaussova referenční křivka** z (mean,σ), overlay N/x̄/s/med), vpravo **σy(τ) Allan tabulka** (τ=1/10/100/1k/10k s z ADEV pyramidy, `--` bez dat). Tlačítko **Y: LIN/LOG** (levý footer slot) přepíná osu (`screen_main_hist_logy/toggle`). **Change-key skip:** tick 2×/s překreslí JEN při změně `screen_main_stats_version()` (čítač vzorků, ~1×/s při RUN) nebo lin/log osy — jinak žádný sort/Gauss/ADEV/flip naprázdno (ALLAN okno má stejný mechanismus). **Vzorkování statistiky běží nezávisle na zobrazeném okně** (`app_gpsdo_tick_stats_sample` gatuje jen RUN/STOP; mimo main krokuje simulaci `screen_main_freq_sim_step` bez kreslení) → Allan/histogram rostou 24/7 i při screensaveru — dřívější vazba na main obrazovku zastavovala pyramidu na krátkých τ (bug „Allan nejde přes ~250 s"). Plot i tabulka si čistí svůj rect (`PRIM_BLEND_REPLACE`) → text MUSÍ mít baseline uvnitř rectu (ascent!), jinak AA hrany mimo clear oblast při refreshi tuhnou. Obě okna sdílí geometrii `HIST_PLOT_RECT`/`HIST_TABLE_RECT`.
- **Auto-dim** (UiTask): po `g_autodim_sec` bez doteku ztlumí backlight na 20/255 (`AUTODIM_LEVEL`, nikdy tma); **první dotek jen probudí** (nespustí akci tlačítka). 🔴 **Stejně probouzí i ENCODER** (2026-09-02, STATUS #128): jakákoli událost (západka, krátký/dlouhý stisk, dvojklik) nuluje časovač nečinnosti a při ztlumení jen probudí — **první událost se zahazuje** stejně jako první dotek (v tmě by uživatel naslepo přestavil hodnotu, kterou nevidí). Předtím se nečinnost měřila VÝHRADNĚ od posledního doteku, takže otáčení knoflíkem nebránilo usnutí a ze spořiče encoder nepřebudil vůbec. ⚠️ Proto `app_gpsdo_handle_encoder()` událost **nepolluje**, dostává ji parametrem — `encoder_poll()` je jednokonzumentové API a UiTask ji musí vidět dřív. Aplikace jasu = výhradně UiTask (`ws_panel_set_backlight` @ I2C4 pod mutexem, jen při změně cíle); app vrstva mění jen `g_brightness`.
  - **⚠️ Zápis jasu do ATTINY = zdroj „mrtvého touche" (2026-08-29).** ATTINY je bit-bang I2C slave + PWM podsvícení; runtime zápis jasu **těsně následovaný STARTem touch čtení** mu rozhodí slave automat → drží SDA → celá I2C4 (touch + TMP117 0x48) je mrtvá **až do power-cyklu** (recovery po sběrnici to neopraví). Screensaver ztlumení proto zapisuje jas **s trojí pojistkou**: (1) `HAL_I2C_IsDeviceReady` probe — když ATTINY neACKne, zkusit až za 200 ms (nebušit každých 10 ms); (2) `osDelay(3)` klidová mezera před i po transakci; (3) **`s_bl_settle`** — po zápisu jasu touch poll **~150 ms nečte FT5x06**, aby ATTINY po zápisu neschytal hned START od jiného mastera na témž busu. Vrátit „bez ztlumení podsvícení" = `bl_target = g_brightness` (bez `s_dimmed ? AUTODIM_LEVEL`).
- **Header hlavní obrazovky** (`screen_main_redraw_time`): čas HH:MM:SS + „UTC" popisek (RTC běží v UTC) + datum; vlevo **stavové mikro-ikony** — přeškrtnutý reproduktor (`ui_icon_speaker_muted`) při `g_sound_muted`, amber „H" při holdoveru (`!g.valid && g.fixes>0` = fix ztracen po tom, co už jednou byl). Změnový klíč zahrnuje i stav ikon → překreslí se i mimo tik sekundy.
- **Trend fullscreen** (s_view=9, tap trend karty): celá historie ringu (až `STAT_N`=120 s vs. 60 s na kartě), auto-scale + min/max frac popisky, drift overlay; change-key jako histogram. **O přístroji** (s_view=10, tlačítko v Nastavení): FW `gpsdo-ui v0.1` + `__DATE__ __TIME__` build, autoři OK2HAZ & OK2JNJ, MCU, uptime, selftest verdikt, sériové č. (zatím nepřiděleno → CALIB store). **Boot splash** (s_view=11): logo „GPSDO" (mono_75) + build + živý řádek Selftest (PASS/FAIL z `g_selftest_res`); UiTask ho drží ~1,4 s před hlavní obrazovkou.
- **NEPOVOLOVAT I2C1 v IOC** (init je ručně v USER CODE).

## UART TX
`_write` (main.c) chráněn `uartTxMutexHandle` (serializace printf z více tasků, jen za běhu
scheduleru) + timeout 100 ms (~1150 B/řádek, bez utínání). Pořád blokující HAL_UART_Transmit.

## UART RX (usart.c)
RX přes IT + fronta `UartRxQueue`. `RxCpltCallback` zařadí bajt a znovu nahodí `Receive_IT`.
**⚠️ `HAL_UART_ErrorCallback` (ORE/FE/NE/PE — typicky šum při hot-plugu kabelu) MUSÍ před
re-armem zavolat `HAL_UART_AbortReceive` + vynulovat `ErrorCode`.** Bez toho po chybě `RxState`
zůstane `BUSY` → `HAL_UART_Receive_IT` vrátí `HAL_BUSY` → **RX se už nikdy nenahodí (mrtvá
konzole, TX/výpisy jedou dál).** AbortReceive v IT režimu neblokuje (ISR-safe).

## Build / flash
STM32CubeIDE: vyber projekt **H757_LED_CM7** → Build (Ctrl+B) → Run (Ctrl+F11, config CM7).

**⚠️ `Project → Clean`: kdy ano a kdy je to ŠKODLIVÉ.** Clean je operace nad **výstupy**, ne nad
konfigurací — zahodí `.o` a **přegeneruje makefily z modelu, který má IDE v paměti**. Pomůže tedy
jen na zastaralé *výstupy*, nikdy na zastaralý *model*.

| změna | co udělat | proč |
|---|---|---|
| jen kód (`.c`/`.h`), bump verze | **nic — obyčejný Build** | `make` závislosti řeší sám; Clean je jen ztráta minut |
| linker script, `ipc_shared.h` | Build, ale **obě jádra** | závislost `make` zná; nesoulad bank ne |
| změna flagů/optimalizace uvnitř konfigurace | **Clean** té konfigurace | `.o` nesou staré flagy a `make` to nepozná |
| Debug ↔ Release | Build | každá konfigurace má vlastní adresář |
| **CubeMX „Generate Code", který přidal SOUBORY** | 🔴 **Close Project → Open Project → ověřit → teprve pak Clean → Build** | `.project` se parsuje **jen při otevření projektu** |

🔴 **Po regenu je Clean PŘED Close/Open aktivně škodlivý:** přegeneruje makefily ze stále
zastaralého modelu, takže z buildu zase vypadne middleware — a navíc přepíše případnou ruční
záplatu v `Debug/`. `F5 (Refresh)` nepomůže taky: odsvěží soubory, o kterých Eclipse **už ví**,
ale deklarace linkovaných zdrojů znovu nečte. Jediná operace, která model znovu načte z disku, je
**Close → Open** (poslední záchrana = odebrat projekt z workspace *bez* mazání obsahu a reimportovat).
Ověření **před** buildem (ať nestavíš minuty na rozbitém modelu):
`grep -c FatFs CM7/Debug/sources.mk` a `grep -c hal_eth CM4/Debug/Drivers/STM32H7xx_HAL_Driver/subdir.mk`
→ obojí musí být **> 0**. A po každém regenu `git diff` — ukáže, co CubeMX přepsal (včetně ručních úprav).

**Build bez IDE: `./scripts/build.sh [Debug|Release] [BOTH|CM7|CM4]`** (přidáno 2026-08-22).
Kromě stavění dělá **dvě kontroly, které dřív chyběly**: (a) porovná zdrojáky deklarované
v `.project` proti generovaným `subdir.mk` → **zastaralý model IDE nahlásí jménem souboru
ještě PŘED buildem** (místo záhadného `undefined reference` po minutách překladu);
(b) varuje, když je některý obraz **starší než `ipc_shared.h`** (= „přeložil jsem jen jedno jádro"
→ nesoulad bank). Obě kontroly jsou otestované vyvoláním té poruchy.
Staví přes ST `make` + jejich `arm-none-eabi-gcc` (cesty hledá globem, přežije upgrade IDE) nad
už vygenerovanými makefily; na konci vypíše velikosti obou obrazů a **`IPC_VERSION` s připomínkou
flashnout obě banky**. Vzniklo proto, že po CubeMX regeneraci **IDE přestává přegenerovávat
`Debug/*/subdir.mk`** a z buildu tiše vypadne middleware (2026-08-12 FatFs, 2026-08-22 FatFs na CM7
**i** HAL ETH na CM4) — projeví se to až jako `undefined reference` při linkování, ačkoli
`.project`/`.cproject` jsou v pořádku. Léčba je `Close Project → Open Project` (viz
`CUBEMX_CHECKLIST.md`); skript je nezávislá cesta, jak mezitím vyrobit firmware.
⚠️ Skript makefily **negeneruje** — poprvé je musí vyrobit IDE.
**⚠️ Velikost FW: stavěj konfiguraci `Release`, ne `Debug`.** Debug jede na **`-O0`**;
`Release` je v `.cproject` už nachystaná s **`-Os`** a od Debugu se **neliší ničím jiným**
(stejné defines včetně `DEBUG`, takže `__HAL_DBGMCU_FREEZE_IWDG1` a spol. se chovají stejně).
V IDE: *Project → Build Configurations → Set Active → Release*.
✅ **AKTIVNÍ OD 2026-08-30 — obě jádra běží Release, změřeno a naflashováno:**
| jádro | Debug `-O0` | Release `-Os` | úspora |
|---|---|---|---|
| CM7 (bank1) | 740 980 B | **559 616 B** | −181 364 B (−24,5 %) |
| CM4 (bank2) | 216 016 B | **161 252 B** | −54 764 B (−25,4 %) |
⚠️ **CM4/Release NEdefinuje `-DDEBUG`** (CM7/Release ano). Jediné, co na tom viselo, je
`__HAL_DBGMCU_FREEZE2_IWDG2()` uvnitř `iwdg2_init()` — a ta se **nevolá** (v `CM4/main.c`
zakomentovaná, IWDG2 je záměrně vypnutý), takže je to inertní. Před flashem CM4/Release
prošly obě povinné kontroly: `DscrTab` = `30040000 B`, `ram_heap` = `1002xxxx`.
⚠️ **Míchat konfigurace jde** (CM7-Release + CM4-Debug apod.) — IPC kontrakt drží layout
struktur v `ipc_shared.h` (ABI), ne úroveň optimalizace; `IPC_VERSION` se tím nemění.
🔴 **Po přeflashnutí Release se ZMĚNÍ adresy globálů** (`.bss` layout) — při čtení přes
`STM32_Programmer_CLI -r32` **vždy znovu grepni `CM7/Release/H757_LED_CM7.map`**, ne starou
Debug mapu (jinak čteš cizí proměnnou a vypadá to jako zamrzlý firmware).
⚠️ Optimalizace mění časování — po přepnutí ověř na HW to, co na něm visí: DWT `delay_us`
(SPI2/FPGA rámce), bit-bang pípání v `bootled_fail()`, I2C recovery pulzy.
✅ **Riziko latentního UB prověřeno (2026-08-13):** celý projekt přeložen `-Os` s
`-Wmaybe-uninitialized -Wstrict-aliasing=2` — to jsou varování, která `-O0` vypsat
NEUMÍ, protože potřebují datový tok z optimalizátoru. Mimo vendor kód vzniklo
**jediné** (`-Wformat-truncation` v `allan_ylabel`, mezitím opraveno) a **žádné**
uninitialized/aliasing. Přepnutí na Release tedy neodemkne skrytou UB — zbývá
ověřit už jen to časování výše.

**⚠️ UTF-8 v popiskách:** horní indexy (`⁻¹²`) jsou **3 bajty na znak**, ne jeden.
`allan_ylabel` skládá „10" + znaménko(3) + dvě cifry(6) + NUL = přesně 12 B. Buffery
pro takové řetězce dimenzuj podle bajtů, ne podle počtu znaků.

**Kde je flash doopravdy** (z map souboru, Debug/-O0, celkem 686 KB):
`.rodata` 333 KB — z toho **fonty ~285 KB = 41 % celého programu** (`ui_font_mono_22` sám 46 KB);
**⚠️ Aktualizace 2026-08-29 (změřeno `nm --size-sort -S`):** fonty = **256 KB = 35 %** obrazu po
zrušení `ui_font_mono_20`. **Plný charset (432 glyfů) mají 4 mono + 3 sans fonty**, každý z nich
nese samotnou tabulku `_glyphs` **6912 B** (432 × 16 B) → jen metadata = **48 KB**. Cena za font
(bitmapa + tabulka): mono_22 46,5 KB · sans_18 35,3 KB · mono_18 34,4 KB · sans_16 30,1 KB ·
mono_16 28,2 KB · sans_14 25,2 KB · mono_14 23,8 KB. Zbytek (subsety) je drobné:
mono_75 14,7 KB · mono_25 8,9 KB · mono_52 5,0 KB · mono_30 2,1 KB · sans_32 1,3 KB.
**Slučování velikostí je proto nejúčinnější páka po Release** — `mono_20` (39,5 KB, jen 9 volání)
sloučen do `mono_22` 2026-08-29 = **−39,5 KB**; stejným krokem dřív padly `sans_17`→`sans_16` a
`sans_20`→`sans_18`. Další kandidáti (nedělat bez zadání): `mono_14`↔`mono_16`, `sans_14`↔`sans_16`.
`.text` 353 KB. **Mazání nepoužitých FUNKCÍ velikost NEZMĚNÍ** — `-ffunction-sections`
+ `--gc-sections` je zahodí už dnes (ověřeno: po odstranění 24 mrtvých funkcí byl `.elf`
bajt za bajt stejný). ⚠️ **Neplatí to pro nedosažitelné VĚTVE uvnitř živé funkce** — ty
linker odstranit neumí a stojí flash doopravdy. Případ z 2026-08-13: po odstranění
`prim_path_quad_to()`/`prim_path_arc()` zůstaly jejich `case` větve ve `flatten()` (path.c)
a držely při životě celý `bezier.c`; jejich dobrání ušetřilo **888 B** a zmenšilo
`prim_path_op_t` z 20 na 8 B (cesty se alokují na haldě → i úspora RAM).
**Poučení: když odstraníš producenta, dober i konzumenta** — půlka odstraněné
funkcionality je horší než obě čisté varianty.
Pořadí páky podle výnosu: **Release −151 KB → fonty (~285 KB, ale kvalita) → zbytek**.
Toolchain (arm-none-eabi) není v PATH tohoto prostředí, ale **je na disku**:
`C:\ST\STM32CubeIDE_2.1.0\...\gnu-tools-for-stm32.14.3...\tools\bin\arm-none-eabi-gcc.exe`
(GCC 14.3) — použitelný pro **kompilátorový audit** (`-Wall -Wextra -Wshadow` +
`-fanalyzer`) jednotlivých souborů bez IDE. Flash/link jen z IDE.
🔴🔴🔴 **ČTENÍ LADICÍ SONDOU ZA BĚHU ZABÍJÍ I2C4 (změřeno 2026-08-30, reprodukovatelné na 1. pokus).**
`STM32_Programmer_CLI -c mode=HOTPLUG -r32 …` cíl **haltuje**. Když halt padne doprostřed
transakce na I2C4, **ATTINY (bit-bang slave, který musí sledovat KAŽDOU transakci na sběrnici
včetně cizích) uvidí přenos, co nikdy neskončí, ztratí synchronizaci a celá I2C4 umře natrvalo** —
touch i TMP117 0x48 i podsvícení, až do power-cyklu.

🔴🔴 **REVIDOVÁNO 2026-09-06: TOHLE NENÍ PROKÁZANÉ A NEJSPÍŠ TO NENÍ PRAVDA.**
Uživatel upozornil, že „dřív to šlo i se sondou" — a historie mu dává za pravdu
(měsíce rutinního čtení globálů sondou, viz `debug-bez-konzole`). Prošel jsem
původní měření a **je bezcenné**:

| naměřeno 2026-08-30 | `err` celkem | v řadě |
|---|---|---|
| před testem | 0 | 0 |
| po 1. čtení sondou | 6 | 6 |
| po 2. čtení | 13 | 13 |
| po 3. čtení | 22 | 22 |

**Chybí kontrolní větev.** `tools/probe_test.ps1` nikdy nezměřil, co udělá
**zdravá sběrnice ponechaná stejnou dobu o samotě, bez sondy**. Celý test trvá
~15 s pozorování — a **týž den** je v téhle poznámce zapsáno, že sběrnice umírala
sama od sebe po **7 s, 136 s a ~2016 s**. Sběrnice, která hyne sama během minut,
vyrobí přesně tenhle průběh čísel i kdyby se sondou nesáhlo. **Souběh je úplný,
takže z toho měření nejde vyvodit vůbec nic.**

🔑 **Mnohem lépe podložený podezřelý: zápis jasu do ATTINY při auto-dimu.**
Ten je v této poznámce zapsán jako **změřený** spouštěč přesně téhle poruchy
(2026-08-29) a hlavně **sedí na časování**: auto-dim se spustí po `g_autodim_sec`
**bez doteku displeje** (default **300 s**) — tedy právě během ladění sondou nebo
přes UART, kdy se panelu nikdo nedotýká. To vysvětluje korelaci „umřelo to,
když jsem sondoval", aniž by sonda byla příčinou. Úmrtí v 7 s odpovídá
**bootovnímu** nastavení jasu, taktéž zápisu do ATTINY.

**Rozhodne levný test BEZ sondy** (dokud neproběhne, je příčina otevřená):
1. auto-dim **VYPNOUT** → nechat běžet hodiny → přežije sběrnice?
2. auto-dim na **15 s** → umře opakovaně a rychle?
Když ano, příčina je firmwarová a tedy **dosažitelná** — na rozdíl od
„slave čipy se odmlčely", což je zapsané jako neřešitelné bez HW resetu ATTINY.

**Co z původního bloku platí dál:**
- **UART je stejně první volba** — `status` (řádek `I2C4:`), `sensors`, `stats`,
  `scanner`, `flightrec` pokryjí skoro vše a cíl nezastavují. Sonda je dražší
  nástroj, ne zakázaný.
- **Halt cíle je pro I2C4 pořád teoreticky nebezpečný** (ATTINY je bit-bang slave
  a musí sledovat i cizí transakce) — jen to **není naměřené**. Ber to jako
  riziko, ne jako zákon.
- ⚠️ **Nesonduj v hustých dávkách.** `tools/gpio_drift.ps1` dělal 33 připojení za
  sebou; už proto, že každé je halt, se čte **jedním** připojením.
- Nástroj `tools/probe_test.ps1` je **použitelný až s kontrolní větví** — bez ní
  měří jen to, že čas plyne.


⚠️⚠️ **CPU zátěž NEMĚŘ přes ladicí sondu.** `STM32_Programmer_CLI -c mode=HOTPLUG -r32 …`
cíl na dobu čtení **zastaví**. Běhový čítač FreeRTOS (DWT CYCCNT) přitom běží dál, ale
IDLE task se nevykonává → `g_rtos_cpu_pct` v následujícím okně vyjde **nafouklý**, a čím
častěji se čte, tím víc. Změřeno 2026-08-13: jedno izolované čtení dalo 71 %, po přechodu
na opakované čtení 95–98 %, zatímco **`stats` (bez sondy) ukázal 60 % a IDLE 40 %** —
a to sedí s dlouhodobou zkušeností, že zátěž nešla přes 80 % ani po dnech běhu.
**Jediné důvěryhodné měření CPU je UART `stats`** (per-task, DWT okno 1 s, bez debuggeru).
⚠️ **Tenhle odstavec o CPU platí** (změřeno 2026-08-13, opakovatelné).
🔴 **Nepleť si ho s tvrzením o I2C4 výše, které jsem 2026-09-06 musel odvolat.**
Mezitím tu stálo „sonda se za běhu nepoužívá na NIC" — to bylo přehnané a odvozené
z nekontrolovaného měření. Platí umírněná verze: **UART je první volba** (`status`
vč. řádku `I2C4:`, `stats`, `sensors`, `scanner`, `flightrec`, `selftest` pokryjí
skoro vše a cíl nezastavují), **sonda je legitimní, když UART nestačí** — cíleně,
jedním připojením, ne jako monitor.

**Statický audit (opakovatelný recept, plný průchod 2026-08-30).** Toolchain není v PATH, ale je
na disku — audit jde pustit bez IDE. Flagy se berou **z generovaného makefile**, aby seděly
s reálným buildem: `grep -m1 -oE '\-mcpu=cortex-m7.*-o "\$@"' CM7/Release/app/subdir.mk`
(⚠️ `tr -d '"'` — quotované `-I` cesty jinak rozbije expanze shellu).
🔴🔴 **Flagy ber ze `subdir.mk` TOHO adresáře, ale kompilátor spouštěj z `CM?/Release`.**
Jeden společný flag set nestačí (`app/subdir.mk` nemá `-I../Core/Inc`, takže se Core/Src
tiše nepřeloží) a spuštění z podadresáře taky ne (`-I../Core/Inc` je relativní
k `CM?/Release`, odkud běží `make`). Obojí selže **tiše** a audit pak hlásí „0 varování“,
aniž by cokoli zkontroloval — přesně se to stalo 2026-09-01 (57 z 81 souborů, STATUS #118).
⚠️ **Vždy kontroluj počet ÚSPĚŠNÝCH překladů, ne jen počet varování.**
✅ Hotový skript, který to dělá správně: **`python tools/audit.py`** (92 souborů obou jader,
vypíše **použitou verzi gcc**, pak `prelozeno OK / SELHALO / souboru s varovanim` + log).
Baseline k 2026-09-04 (**GCC 14.3**) = **92 OK, 0 selhání, 2 soubory s varováním** — a ta
3 varování jsou v čistě generovaném CubeMX kódu (`fmc.c` 2× `sdramHandle`, `main.c`
`assert_failed(file)`). Cokoli navíc je regrese.
- 🔴🔴 **Audit MUSÍ běžet tímtéž kompilátorem jako build.** V IDE jsou nainstalované **dvě**
  verze (`gnu-tools-for-stm32` **13.3** a **14.3**) a původní `glob(...)[0]` bral podle
  abecedy tu **starší 13.3** — audit tak tiše kontroloval něco jiného než reálný překlad.
  Změřeno 2026-09-04 na tomtéž souboru: **13.3 → 0 nálezů `-Wformat-truncation`, 14.3 → 1**
  (`app_gpsdo.c`, změnový klíč okna MĚŘENÍ; chybu našel až IDE build, ne audit — STATUS #135).
  Skript teď vybírá **nejnovější** toolchain (verze se parsuje **číslem**, ne abecedně) a
  **verzi vypisuje**. ⚠️ Pravidlo: **nástroj, který si sám vybírá kompilátor, musí tu volbu
  vypsat** — jinak je „audit čistý" tvrzení o neznámém kompilátoru.
- **Přísná varování** (`-fsyntax-only` stačí): `-Wall -Wextra -Wshadow -Wcast-align
  -Wnull-dereference -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wformat=2
  -Wstrict-aliasing=2 -Wmaybe-uninitialized`.
- **Dataflow analyzátor**: `-fanalyzer`. 🔴🔴 **`-fanalyzer` je s `-fsyntax-only` TIŠE VYPNUTÝ**
  — musí se skutečně překládat (`-c -o …`). Bez toho to vypadá jako „0 nálezů", ale analyzátor
  vůbec neběžel (stalo se mi to při tomto auditu). **Vždy dělej pozitivní kontrolu**
  (`int g(void){int*q=0;return *q;}` musí dát `-Wanalyzer-null-dereference`).
- **Mrtvý kód**: symboly v `.o`, které nejsou ve slinkovaném `.elf` (`nm --defined-only`).
  Stojí 0 B (`--gc-sections` je zahodí), takže je to signál o *zdrojové* redundanci, ne o flash.
- ⚠️ **`dchg(cache, sizeof cache, src)` má invariant: `sizeof(src) >= sizeof(cache)`** —
  dělá `strncpy(cache, src, n-1)`, tedy smí z `src` číst až `n-1` B. Píše se to jako
  `char src[sizeof cache];`, aby se invariant držel sám. Porušení najde `-fanalyzer`
  (`-Wanalyzer-out-of-bounds`); naivní grep to NEZVLÁDNE (lokální `char b[]` se v každé
  funkci jmenuje stejně a má jinou velikost → nutná analýza po funkcích).
- **Výsledek průchodu:** CM7 79 souborů + CM4 14 souborů → **0 varování v našem kódu**,
  1 benigní vendor (`ethernetif.c` `-Wcast-align`, ST `container_of` nad RX poolem),
  analyzátor **1 reálný nález** (opraven).

**Testy:** UART `selftest` = neblokující pure-logic unit testy na targetu (idiom
projektu — testy běží na zařízení, ne na hostu; `run_selftests` ve freertos.c, i při bootu):
CRC16, hystereze /4↔/16 (`fpga_freq_select_core`), GPS parser (`gps_selftest`), fmt_frac+hist_h
(`screen_main_selftest`), Maidenhead lokátor (`app_gpsdo_selftest`), kalendář+DST (`rtc_selftest`),
datalog záznam+CRC+čas (`datalog_selftest`), Math Mx+B/NULL/limit pass-fail (`meas_math_selftest`)
→ **„SELFTEST: 16/16 PASS"** (`SELFTEST_N` = 16, indexy 0..15; dál setup sanitizace, autocal
verdikt, prezentace měření, **SCPI parser**, **IPC seqlock+ring**, **vzory benchmarku pamětí**,
**fázový šum FFT** (`pn_selftest` #45 — FFT korektnost + PSD normalizace + `(f0/f)²` převod)
a **indexování datové cache** (`sdram_log_selftest` — ring před i po přetočení)).
⚠️ **Okno Selftest má `ST_SPLIT = (ST_N+1)/2`** a `_Static_assert` na max. 8 řádků ve sloupci —
9. řádek by padl na `y=320`, kde je souhrn „Celkem"/„běh #".
⚠️ **Při FAILu se vypíšou i indexy** neúspěšných testů a u SCPI přímo `scpi.c:<řádek>`.
`qspitest`/`storetest` = destruktivní HW testy.
⚠️ **`run_selftests()` NENÍ reentrantní a má vlastní zámek.** Několik testů drží velké buffery
jako `static` (`gps_selftest`, `scpi_selftest`, `ipc_selftest`, **`pn_selftest`**) — jinak by
přetekly stack malých tasků. `run_selftests` běží v `StartDefaultTask` (stack 2560 B) hned po
`gps_init()`. **🔴 Přesně to shodilo desku 2026-08-29:** `pn_selftest` (#45) měl `pts/pts2/few/y`
na stacku = frame 1576 B + volá `pn_compute` (1144 B) + `pn_fft` (160 B) = ~2880 B → přetečení →
HardFault → `NVIC_SystemReset` → **boot loop** (žádný displej, žádné pípání, jen LED1). Fix =
buffery `static` (frame 40 B). **Nový selftest s polem > ~200 B na stacku = `static`, bez výjimky.**
Tím ale vzniká sdílený stav
mezi **třemi** volajícími (defaultTask při bootu, UartTask `selftest`, UiTask SPUSTIT); souběh
se nečeká, vrátí se poslední známý výsledek. ⚠️ **Volat až za běžícím schedulerem** — před
`vTaskStartScheduler` má port `uxCriticalNesting = 0xaaaaaaaa`, takže by `taskEXIT_CRITICAL()`
přerušení už nikdy nepovolil (SysTick/`HAL_Delay` by zamrzly).
