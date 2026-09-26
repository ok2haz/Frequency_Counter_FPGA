# AUDIT_STATUS.md — stav auditu

> Aktualizuj **na začátku a na konci každého sezení**. Tenhle soubor je jediný
> zdroj pravdy o tom, co je hotové — kontext CLI sezení se nepřenáší.

**Poslední aktualizace:** 2026-09-26 (konec sezení) — 🟡 **Modul 24 — bod 6: skutečné τ0
vzorku (`a5ddf9d`).** Levná varianta: τ0 = součet oken měření za tik se MĚŘÍ, okno ALLAN
varuje (τ0 mimo ±2 % / kolísání > 5 %), UART `status full` ho vypíše. Osa τ se nepřepočítává
(poctivě = vzorkovat po počtu měření, TODO #27). Při rozboru se ukázalo, že 1Hz tik UiTasku
(~1,01 s) občas pobere 5 měření — nad ~1 kHz zanedbatelné, na desce to změří `status full`.
Build CM7 0 varování, `audit.py` 92/0/2, `.text` 622 584 B. ⬜ Neověřeno na HW.
🔑 **Pokračovat zde:** HW průchod (sonda); z matematiky zbývá bod 5 (MTIE/TIE ze
`sdram_log`) a poctivý přepočet τ0 (#27) — oba až s běžícím FPGA.

**Předchozí, 2026-09-26:** 🟡 **Modul 24 — bod 4: typ šumu
a EDF pásu podle něj (`06cbfd8`).** EDF podle lokálního typu šumu (5 typů, Howe–Allan–
Barnes), vzorce ověřené Monte Carlem (`docs/audit/sim/2026-09-26_edf_typ_sumu.js`, shoda
do ~12 %; simulace chytila můj chybně zapamatovaný vzorec pro blikavý FM → dodatek
k L-0088). Klasifikace shodná s webem (ověřeno na hostu). Okno ALLAN ukazuje typ šumu
a sklon. Build CM7 0 varování, `audit.py` 92/0/2, `.text` 621 912 B. ⬜ Neověřeno na HW.
🔑 **Pokračovat zde:** zbývá bod 5 (přesné MTIE/TIE z `sdram_log`) a 6 (τ0 = skutečný
rozestup) — oba potřebují k ověření běžící FPGA; jinak HW průchod.

**Předchozí, 2026-09-26:** 🟡 **Modul 24 — doplněk
„co dál s matematikou": F-0179 [S3] + podlaha čítače v grafu.** Rozhodnutí
uživatele „2 a 3, tvoje doporučení".
`5c019b1` **F-0179** — statistika stability ve `float`: nominál je celé Hz, |y| < 1/f,
a kvantizace smazala šum (simulace: ADEV 3–4× vysoko při 1 MHz/100 kHz, **0** při
10 kHz/1 kHz). Nově ring, obě pyramidy a součty v `double`, `float` jen relativně
při kreslení (+5,9 kB RAM). Lekce **L-0094** (přesnost typu vůči VARIACI, ne hodnotě).
`422fde5` **podlaha čítače** — čerchovaná čára v grafu stability (displej i web):
kvantizace TDC = bílý PM, pro tentýž estimátor jako křivka (ADEV tdc/(2τ), HDEV
0,527·tdc/τ, MDEV tdc/(2τ√m)); vzorce ověřeny simulací estimátorů do 0,6 %.
Ověření: build CM7+CM4 0 varování, `audit.py` 92/0/2, `check.py --build` vše OK
(SPA_HTML 153 488 B), CM7 `.text` 619 592 B, CM4 246 952 B. ⬜ Neověřeno na HW.
🔑 **Pokračovat zde:** HW průchod (viz níže); další matematika z nabídky: určení
typu šumu + EDF podle něj, přesné MTIE/TIE z `sdram_log`, τ0 = skutečný rozestup.

**Předchozí, 2026-09-26 (konec F5 druhého průchodu):** 🟡 **Modul 24, DRUHÝ
PRŮCHOD — F5 hotová: opraveno 7 z 8 (skupiny A+B), F-0176 do C.** Rozhodnutí
uživatele „a+b, u B tvoje doporučení". Commity: `ace2939` (F-0171/F-0172 —
akumulátor Σcyklů/Σhradel pro statistiku i datalog, příznak `freq_avg` v bitu 63),
`472ece6` (F-0173 — n_eff z autokorelace reziduí, firmware jedním průchodem +
web), `50b3124` (F-0177 — podtržení i v celé části), `2a39f44` (F-0178),
`937b5d6` (F-0174 web gate_ns, F-0175 Welch + periodický Hann na obou stranách).
Každá oprava má pozitivní kontrolu (stará SPA / přepis 1:1 v Node selhal, nová
prošla); simulace a kontroly v `docs/audit/sim/`. Ověření: build CM7+CM4 0
varování, `audit.py` 92/0/2 (chytil 1× `-Wshadow` ještě před commitem),
`check.py --build` vše OK (SPA_HTML 152 101 B), CM7 `.text` 618 856 B, CM4
245 564 B. Lekce **L-0092** (rozestup × okno průměrování), **L-0093** (test
ověřený na nezávislých datech). ⬜ **Neověřeno na HW** — sonda nepřipojená;
F-0171/F-0172 se ukážou až s během FPGA (HYPOTÉZA: navazující okna FPGA).
🔑 **Pokračovat zde:** HW průchod (TODO #254 + modul 24): flash OBOU bank,
`selftest` 16/16, web DRIFT/NEJISTOTA/ℒ(f), `fpgasim on 100000000` → podtržení
na desítkách Hz; F-0168 + F-0176 s reálnými daty z FPGA.

**Předchozí, 2026-09-26 (druhý průchod F3):** 🔴 **Modul 24, DRUHÝ PRŮCHOD (F3) —
8 nových nálezů (1× S2, 3× S3, 4× S4).** Zápis →
[`audit/2026-09-26_matematika-mereni-2.md`](audit/2026-09-26_matematika-mereni-2.md),
simulace v `docs/audit/sim/`. Tentokrát se ověřovaly PŘEDPOKLADY vzorců:
🔴 **F-0171 [S2]** statistika displeje bere 1 měření 0,25 s za sekundu (mrtvá doba
75 %) → σy 2× (bílý FM) až 23× (bílý PM, τ = 50 s) vysoko a šum přístroje dostane
sklon bílého FM; **první průchod to chybně prohlásil za „τ0 = 1 s správně"**.
**F-0172** datalog = okamžitý vzorek, rekonstrukce ho vkládá jako 10s průměr
(σy z historie 3,3× nad živými). **F-0173** t-test z F-0169 předpokládá nezávislá
rezidua — u autokorelovaných dat hlásí falešný drift v 63–91 % (moje doporučení
to zhoršilo; korekce n_eff → 7–28 %). **F-0174** web počítá nejistotu z nastavené
brány (dvojče STATUS #83). S4: web ℒ(f) bez F-0161, GUM u rozlišení, vynucené
podtržení desetin nad 7 MHz, σy(1 s) dvakrát v okně ALLAN.
*(Triáž F5.0 proběhla týž den — viz záznam výše.)*

**Předchozí, 2026-09-26 (konec F5/F6):** 🟡 **Modul 24 — F5
hotová: opraveno 11 z 12 (skupiny A+B), F-0168 odložen do C.** Rozhodnutí
uživatele „a+b, u B tvoje doporučení": F-0162 varianta (b), F-0165 politika
„nevím", F-0167 EDF bílý FM (SP1065), F-0169 t-test.
Commity: `c387409` (F-0158 + sourozenec: `RTC_CALR` se po resetu nečetl),
`35d3453` (F-0161; odečet trendu **zamítnut pozitivní kontrolou**),
`479d7d1` (F-0160/0163/0164), `32efe36` (F-0166/0167), `8c79ced` (F-0159/0169),
`2f9c015` (F-0165, 8 míst na obou jádrech + kontrola v `calib_load`),
`038288c` (F-0162), `7f29799` (`-Wshadow` z vlastní opravy F-0169 — chytil ho
až `audit.py`, L-0090).
Ověření: build CM7+CM4 0 varování, `audit.py` **92 / 0 / 2**, CM7 `.text`
615 184 → 616 272 B, CM4 242 692 → 242 700 B; `audit_stav.py --kontrola` OK.
Lekce **L-0086 až L-0090**.
⬜ **Neověřeno na HW** — ladicí sonda nebyla připojená (`No debug probe
detected`), takže ani flash, ani `selftest` na desce. Až bude: flashnout
**obě banky** (mění se i CM4 — `httpd_min.c`), `selftest` → 16/16 PASS
(nové případy v `pn_selftest`, `meas_math_selftest`, `mp_selftest`), okno
ANALÝZA bez FPGA musí ukazovat „-- (bez mereni)", SELF-SURVEY „Rozptyl fixu:"
neutrálně. Disciplinace LSE se ověřuje **hodiny** (12 oken po 10 min).
F6 (komentáře) hotová v `4ca647d` (`phase_noise.h`, hlavička okna ANALÝZA,
`CLAUDE.md`). Při ní nalezen **F-0170 [S4]**: webové JS dvojče estimátorů
(`httpd_min.c` SPA) se s F-0166/F-0169 nesrovnalo (L-0012 — měl jsem ho chytit
v commitu F-0169).
✅ **F-0170 opraveno týž den** na žádost uživatele: `e99ccbb` (SPA: `mdev()` se všemi
členy, `fitSig` = t-test jako firmware, nový vynucující `tools/spa/stat_test.js`,
`check.py --build` vše OK, `SPA_HTML` 150 353 B) a `a8089d2` (firmware: NaN
v `mp_fit_significant`/`mp_fit_solve` končí jako „nevím“ — vada v mé vlastní opravě
F-0169, L-0091). CM7 `.text` 616 480 B, CM4 243 816 B, `audit.py` 92/0/2.
🔑 **Pokračovat zde:** HW průchod (TODO #254 + tento modul) — flash **obou bank**,
`selftest` 16/16, web: karta DRIFT hned po RUN nesmí kreslit čáru nad šumem.

**Předchozí, 2026-09-26 (F3):** 🔴 **Modul 24 (matematické funkce měření,
průřezový) — F3 zapsána, 12 nálezů (1× S2, 7× S3, 4× S4).** Verdikt **podmíněně funkční**. Zápis →
[`audit/2026-09-26_matematika-mereni.md`](audit/2026-09-26_matematika-mereni.md).
Na žádost uživatele (audit + kontrola + optimalizace + komentáře); podle
pravidel 3/4 nejdřív jen F3, opravy (F5) a komentáře (F6) až po triáži.
🔴 **F-0158 [S2]** — disciplinace LSE **nekonverguje**: běžící průměr driftu se
po zápisu korekce nenuluje (`rtc_lse_reset` volá jen UART), takže míchá okna
z různých kalibrací. **Simulace přesně podle kódu:** zbytek osciluje −10…+6 ppm
a neustálí se ani po 4 dnech; s nulováním průměru hned +0,46 ppm (kvantizační
mez). Dopad v holdoveru; `RTC_CALR` přežije reset.
Dál: **F-0159** ANALÝZA počítá U a počet číslic z hradla, které `mp_budget`
tiše dosadí (1 s) — **živě, dokud neběží FPGA**; **F-0160** limitní tester
propustí NaN jako PASS a NaN jde vyrobit třemi SCPI příkazy (SCPI formátovač
je proti NaN ošetřený, UI dvojče ne — L-0012); **F-0161** ℒ(f) bez odečtu
průměru a se symetrickým Hannem (únik DC do binu 6: −66,7 dB, periodický: 0;
dnes maskováno TDC 2,5 ns, aktivní s novou deskou); **F-0162** survey hlásí
rozptyl fixů jako „konvergenci"; **F-0163** díra v opravě F-0053 (`fmt_fixed`
nehlídá `d = 0`, NaN, Inf); **F-0164** `fmt_frac` smyčka bez meze → +Inf
zasekne UiTask; **F-0165** převod mV→dBm 8×, tři politiky. S4: off-by-one
v overlapping estimátorech, přeceněný konfidenční pás, σy(1 s) vs hradlo
v rozpočtu (HYPOTÉZA), naivní proklad.
✅ Ověřeno a v pořádku: kadence 1 Hz (τ0 i `fs = 1.0` správně), mez přetečení
hi-res dělení, vzorce SP1065, TDEV exaktní, Welford, datalog bez času se do
prokladu nedostane.
*(Triáž F5.0 proběhla týž den — viz záznam výše.)*

**Předchozí, 2026-09-25 (konec sezení):** 🟡 **HW PRŮCHOD OPRAV
PŘERUŠEN na žádost uživatele, pokračování = `../STATUS.md` TODO #254.**
Seznam **negenerovat** z `HW_OVERENI_AUDIT_2026-09-19.md` (zastaralý, 11 ze 46),
ale **`python tools/hw_neovereno.py`** (nový nástroj; čte celé `Stav:` bloky
a zachytí i částečně ověřené `~`). Stav: **37 (S2=4, S3=26, S4=7)**, na
začátku průchodu 46. Uzavřeno na desce 9 nálezů (F-0036, F-0056, F-0057,
F-0064, F-0060, F-0131, F-0132, F-0128, F-0027), F-0026 jen částečně
(závod — vytažení karty během zápisu — čeká na souhlas uživatele).
Deska: CM7 = HEAD (`dc6ff88`+), ethernet nově zapojený (`IP 10.0.0.106`),
SD karta 30 GB ve slotu.
🔑 **Pokračovat zde:** `STATUS.md #254` — rozdělené na „vyžaduje uživatele
u desky", „jde bez uživatele", „důkaz už posbíraný, jen zapsat" a „ověřit
nelze vůbec".

**Předchozí, 2026-09-25 (čtvrtý běh):** ✅ **Tři odložené S4
komentářové nálezy skupiny C opraveny** (uživatel schválil "opravit S4
komentáře"). Čistě `docs:` — žádná změna chování, `.text` beze změny
(615096 B), build 0 varování, `audit.py` 92/0/2:
- **F-0147** — komentář u `d2d_wait()` (`prim_stm32_hal.c:177-180`) tvrdil
  scénář "před schedulerem", který v současném call-graphu nenastává;
  přepsán na "defenzivní pojistka pro hypotetickou budoucí early-boot cestu".
- **F-0144** — komentář u `alarm_sd_card()` (`alarm.c:151`) doplněn o
  oboustrannost (každé `pattern_start()` utne rozehraný SD dvouton).
- **F-0145** — komentář u `s_snd_grace` (`sd_export.c:131`) doplněn o vedlejší
  efekt L-0079 (skutečné vložení v prvních ~2 s po bootu se taky nepípne).

🔑 **Backlog NÁLEZŮ je vyčerpaný** — všech 22 číslovaných modulů auditováno
a opraveno, ad-hoc běhy nad posledními úpravami taky. Ověřeno tvrdě:
**142 `Stav:` řádků v `docs/audit/*.md`, všech 142 opraveno/uzavřeno/odloženo**
(žádný S1/S2/S3/S4 otevřený). Vědomě odložená zůstává jen S3 **F-0003**
(čeká na příští CubeMX regen).

🔴 **NOVÉ 2026-09-25 — ALE POKRYTÍ NENÍ ÚPLNÉ: `MODUL 23` (generované init
soubory periferií) CHYBÍ.** Zjištěno křížovou kontrolou souborového pokrytí
(seznam všech `.c` proti souborovým seznamům 22 modulů), ne dalším auditem.
Tabulka modulů **nikdy nevzala za předmět CubeMX-generované `MX_*_Init`
soubory**. Nejsou v žádném souborovém seznamu; zástupné položky (`libprim/*`,
`libui/*`, `freertos*.c`) je nepokrývají.

| soubor | celkem | ruční `USER CODE` | obsah |
|---|---|---|---|
| **`fmc.c`** | 477 ř. | **180 ř.** | celá `fmc_sdram_init_sequence()` — mode registr, CAS latency, `REFRESH_COUNT`, timing |
| **`usart.c`** | 229 ř. | **88 ř.** | `HAL_UART_RxCpltCallback` + `HAL_UART_ErrorCallback` |
| `adc/dsihost/ltdc/quadspi/spi/tim/eth.c` | 122–199 ř. | 17–19 ř. | boilerplate, nízká priorita |

🔑 **Proč to má prioritu, i když je to „generovaný kód":** `fmc.c` je přesně
ten soubor, kde žily **`ReadPipeDelay = 0`** a **`REFRESH_COUNT = 1835`** —
3 338 207 chybných bitů, černý displej, měsíce falešného podezření na pájku
a na vadný SDRAM čip. Obě byly v kódu **od prvního commitu** a našlo je
**měření, ne audit** (viz `CLAUDE.md`, „HW OBVINĚN — A BYL NEVINNÝ").
V `usart.c` sedí past „bez `AbortReceive` zůstane RX navždy `BUSY`" = mrtvá
konzole. ⚠️ Nuance: `fmc.c` **statickou analýzou prochází** — je to jeden ze
dvou souborů baseline `audit.py` 92/0/**2**. „Neauditováno" tedy znamená
*nebyl předmětem modulového auditu*, ne *nikdo se na něj nepodíval*.
⚠️ Past při opravách v těchto souborech: veškerá logika musí zůstat
v `USER CODE` blocích (pravidlo 6), jinak ji příští regen smaže.

✅ **F3 modulu 23 PROVEDENA týž den — 6 nálezů (1× S2, 2× S3, 3× S4),
fáze oprav NEproběhla.** Verdikt **podmíněně funkční**. Zápis →
[`audit/2026-09-25_generovane-init-periferie.md`](audit/2026-09-25_generovane-init-periferie.md).
🔴 **F-0149 [S2]** — obranné potvrzení PG8 ve `fmc.c:229-238` volá
`HAL_GPIO_Init(GPIOG,…)` **bez `gpio_cfg_lock()`**, zatímco CM4 kolem všech
svých GPIOG initů HSEM 1 drží (`CM4/main.c:175-193`). Zámek, který bere jen
jedna strana, nevylučuje nic. **Souběh doložen pořadím bootu staticky:** CM4 se
budí uvolněním HSEM 0 na `main.c:333-335`, `MX_FMC_Init()` běží až na `main.c:382`.
🔑 **Tím padá stojící výmluva** u `gpio_cfg_lock` (*„generované `MX_*_Init` nelze
regen-safe obalit"*) — tenhle blok **v `USER CODE` je**, obalit ho lze.
🔴 **A hlavně:** komentář `fmc.c:222-227` vyloučil CM4 jako původce přepisu
PG8 → ANALOG větou *„tedy PRED bootem CM4"* — **pořadí bootu tu premisu popírá**.
Uzavřelo to pátrání po příčině černého displeje a 10 551 639 chybných bitů.
Dál **F-0150** (komentář `fmc.c:53-79` tvrdí `REFRESH_COUNT=175`; skutečnost je
**371** a experiment se 175 byl měřením zamítnut), **F-0151**
(`_Static_assert` u `REFRESH_COUNT` je po sjednocení zdroje **tautologie** —
nemůže selhat, ale tváří se jako pojistka), **F-0152 [S3]** (200 ms blokující
`HAL_Delay` + blikání LED_1 v `fmc.c:245-249`, v boot cestě **před** bring-upem
displeje → pravidlo 4c), **F-0153 [S3]** (návrat `HAL_UART_Receive_IT()` se
zahazuje v obou callbacích `usart.c:197,224` — jediné selhání, které GPS příjem
zabije natrvalo, je jako jediné nepočítané), **F-0154 [S4]** (`SDRAM_TIMEOUT`
= 65,5 s, a to v době, kdy neběží watchdog).
✅ **Nízkoprioritních 7 souborů** (`adc/dsihost/ltdc/quadspi/spi/tim/eth.c`) má
v `USER CODE` **výhradně** `bootled_step()` — žádný nález, ověřeno extrakcí.
🔑 **Zároveň poctivě:** `fmc.c` je v podstatných věcech (vyhodnocení návratů,
**zpětné čtení `SDRTR` z HW** = L-0055, konzistence CAS latency na obou místech)
napsaný **lépe než průměr projektu**. Riziko nebylo v tom, že jde o generovaný
kód, ale v tom, že se na něj kvůli nálepce „generovaný" nikdo nepodíval.
✅ **FÁZE OPRAV PROBĚHLA TÝŽ DEN — skupiny A i B schváleny uživatelem („a+b"),
5 z 6 nálezů opraveno ve 3 commitech.** ⬜ **Neověřeno na HW.**

| nález | commit | jak | co ověřit na desce |
|---|---|---|---|
| **F-0149** [S2] | `9eeb0c0` | `gpio_cfg_lock()` kolem PG8 bloku + premisa v komentáři uvedena na pravdu | `status` → `GPIO HLIDAC` nižší/nulový; `errlog` (`ERRLOG_K_GPIO`) na porovnání četnosti |
| **F-0151** [S4] | `9eeb0c0` | varianta **(b)** — tautologie nahrazena mezí na takt SDCLK (`fmc.h`) | nic (kontrola překladu), ale ✅ **pozitivní kontrola hotová** |
| **F-0152** [S3] | `9eeb0c0` → 🔴 **vráceno** `830ea2c` | odstranění **ZAMÍTNUTO MĚŘENÍM** — zdržení je NOSNÉ pro USB CDC konzoli; vráceno jako `FMC_POST_INIT_SETTLE_MS` | ✅ **ověřeno na HW po studeném startu** |
| **F-0153** [S3] | `6296e75` | `g_uart1_rearm_fail` + `errlog_put`, re-arm vyčleněn do `uart1_rearm_rx()` | `errlog` po hot-plugu UART kabelu; GPS se musí vrátit k fixu |
| **F-0150** [S4] | *(docs)* | komentář `REFRESH_COUNT` uveden na pravdu (371, pokus o 175 = uzavřená slepá ulička) | nic — `.text` beze změny |

**Ověření (F5.2):** build Release CM7 **0 varování**, `audit.py` **92 OK /
0 selhání / 2 s varováním** (baseline, GCC 14.3), `.text` 615 096 → **615 112 B**
(+16). Protože se kód zároveň přidával i odebíral, velikost sama nestačí —
změny doloženy **přímo v `.elf`**: `objdump -d` nad `MX_FMC_Init` nově vypisuje
`bl <gpio_cfg_lock>` i `bl <gpio_cfg_unlock>` a **už neobsahuje** `bl <HAL_Delay>`
ani `bl <HAL_GPIO_WritePin>`; řetězec `REARM` je v obrazu přítomen.

🔑 **Nejcennější krok celé opravy byla POZITIVNÍ KONTROLA u F-0151** — nahradit
tautologii další tautologií je přesně vzor **L-0081**, takže nestačilo, že se
nový assert přeložil. Dočasnou úpravou `fmc.h` se ověřilo, že **selhat umí**,
a to na **obou** vadách, kvůli kterým vznikl (L-0039): historická hodnota
`1835` → build spadl, SDCLK 50 → 25 MHz → build spadl, nedotčený strom →
prošel. **Assert by tedy původní vadu z prvního commitu zachytil při překladu.**

**Nové lekce:** **L-0082** (zámek, který bere jen jedna strana, nevylučuje nic
— a premisa vylučující podezřelého se musí ověřit proti pořadí bootu),
**L-0083** (po sjednocení zdroje pravdy se kontrola rozchodu stane tautologií).

🔴🔴 **HW PRŮCHOD 2026-09-25 — F-0152 ZAMÍTNUTO MĚŘENÍM, zbytek ✅ OVĚŘEN.**
Naflashováno (CM7 Release, bank1) a proběhly **dva power-cykly**.

🔴 **Odstranění zdržení ve `fmc.c` bylo ŠPATNĚ a měření to ukázalo.** Po
studeném startu bez něj prošlo **všechno, co tenhle nález i checklist modulu
předepisoval** — `g_fmc_init_fail`=0, `g_display_init_step`=0,
`g_cm4_absent`=0, `membench` 0 chybných bitů, retence 0, `bgcheck` BEZE ZMĚNY,
`GPIO HLIDAC` 0, `uptime` rostl — **ale přestala odpovídat USB CDC konzole**
(zařízení vyenumerované, `VID/PID 0483:5740`, data netekla při žádné kombinaci
DTR/RTS; po SW resetu se vždy vrátila). Rozbil se subsystém, který
s auditovaným modulem nesouvisí — **a zrovna ten, kterým se všechno ostatní
měří.** Druhý kandidát (bouře ISR z F-0153) vyloučen měřením:
`g_uart1_rearm_fail`=0, `g_gps_rx_drop`=0, nerostly.
Zdržení **vráceno** jako `FMC_POST_INIT_SETTLE_MS` se zdůvodněním (`830ea2c`);
blikání LED_1 vráceno nebylo. **Nová lekce L-0084.**

✅ **Stav po vrácení, ověřeno po STUDENÉM STARTU** (`Reset: power-on`):
`ping`→`pong`, `selftest` **16/16 PASS**, `bgcheck` BEZE ZMĚNY, `membench`
**0 chybných bitů** / retence 0, `DISPLEJ: bring-up OK`, `LTDC` 0/285,
`GPIO HLIDAC` 0 oprav, I2C4 `SCL=1 SDA=1 idle` (`scanner` našel 0x38+0x45+0x48,
TMP117 err=0), `CM4: alive` + SCPI/HTTP selftest PASS, `ULOZISTE`/`WATCHDOG`/
`FORMAT`/`FONTY` OK, napájení 12,03 V / 5,01 V.
⇒ **F-0149, F-0150, F-0151, F-0153 jsou ✅ ověřené na HW po power-cyklu** (bez
regrese); **F-0152 uzavřeno jako zamítnuté**.

⚠️ **Co ověřit NELZE a nebylo:** účinek **F-0149** — `GPIO HLIDAC` bylo 0 před
i po, takže se závod na téhle desce právě neprojevuje a oprava je preventivní
bez pozorovatelného účinku (tvrdit „ověřeno účinkem" by bylo nepoctivé; ověřeno
je jen „bez regrese"). **F-0153** by chtěl opakovaný hot-plug GPS kabelu.
⚠️ **Past pro příště:** `status` umí ukázat `I2C4: SCL=0 SDA=1 BUSY`, což je
podpis mrtvé sběrnice — ale je to jen okamžik zachycený uprostřed přenosu.
Rozhodne `scanner` + `err v rade`.

🔑 **Pokračovat zde:** (1) F-0154 [S4] zůstává otevřený (skupina C —
`SDRAM_TIMEOUT` 65,5 s; měnit až spolu s jiným zásahem do `fmc.c`).
(2) Generovaný `HAL_FMC_MspInit` zůstává bez zámku — mimo `USER CODE`,
regen-safe obalit nejde; samostatné rozhodnutí.
(3) 🔑 **Otevřená otázka z L-0084:** proč 200 ms ve `fmc.c` rozhoduje o USB CDC,
se neví. Kdyby to někdo chtěl dořešit, začíná to zúžením zdržení (200 → 100 →
50 ms) s power-cyklem po každém kroku, ne úvahou.

---

🔴 **NOVÉ 2026-09-25 (čtvrtý běh) — PŘEZKUM VLASTNÍCH OPRAV modulu 23:
3 nálezy (2× S3, 1× S4), F3 zapsána, fáze oprav NEproběhla.** Verdikt
**podmíněně funkční**. Zápis →
[`audit/2026-09-25_fixreview-modul23.md`](audit/2026-09-25_fixreview-modul23.md).
`/audit-modul` přišel **bez jména modulu** a žádný modul se stavem `nezačato`
nezbyl, takže cíl vybrán podle precedensu posledních dvou sezení (přezkum
hotových oprav — právě ten našel F-0148 a L-0081).

🔴 **Obě S3 míří na TUTÉŽ opravu (F-0153) a obě znamenají, že nedoručuje,
co slibuje** — tedy že „převádí tichou poruchu na viditelnou":
- **F-0155 [S3]** — `errlog_put(ERRLOG_K_UART, **0xFE**, …)` jde do pole `sub`,
  jenže `errlog_fmt_detail` (`flightrec.c:912-919`) zná jen `sub == 0xFF`.
  Selhání re-armu se proto vypíše jako **falešné `ORE=<počet> FE=0 NE=0 PE=0`**
  — záznamník tvrdí chybu, která se nestala, a zamlčuje tu, která se stala.
- **F-0156 [S3]** — `errlog_put` má prodlevu **per DRUH** (`ERRLOG_COOLDOWN_MS`
  = **60 s**). `HAL_UART_ErrorCallback` ale loguje **dvakrát v jednom průchodu**:
  nejdřív chybu linky, pak (přes `uart1_rearm_rx()`) selhání re-armu. Druhé
  volání narazí na prodlevu, kterou si první právě nastavilo → **v hlavní cestě
  se záznam neemituje NIKDY**. A protože je selhání re-armu terminální (příjem
  je mrtvý, další callback nepřijde), nevyveze ho ani žádný příští záznam.
  Zbude jen `g_uart1_rearm_fail`, čitelné **pouze sondou** — což zpřísněná
  **L-0017** za dostatečné nepovažuje.
  🔑 Doporučená oprava (vlastní `kind` pro terminální poruchu) řeší **obě**
  S3 jedním zásahem.
- **F-0157 [S4]** — oprava F-0151 zavedla `SDRAM_ROWS`/`FMC_SDCLK_MHZ` jako
  druhé vyjádření faktů z `fmc.c:207`/`:212` (generovaný kód mimo `USER CODE`,
  takže vynutit to nejde) — čistá bilance F-0151 zůstává kladná, ale je to
  menší výskyt téže třídy, kterou odstraňovala (L-0018).

✅ **Zkontrolováno a v pořádku:** `uart1_rearm_rx()` jako jediný zdroj pravdy
pro obě obsluhy; **`errlog_put` doložen jako skutečně ISR-safe** (krátká sekce
`__disable_irq()`, zápis jen do RAM ringu, čas se dopočítává až v `tick`) —
tvrzení z mého komentáře tedy platí (L-0028); sentinel `0xFE` nekoliduje
s `ec & 0xFF` (max `0x3F`); F-0149 je celý v `USER CODE` a HSEM hodiny jsou
zapnuté před použitím; F-0144/145/147/150 obsahují **výhradně komentáře**
(doloženo i `.text` beze změny).

🔑 **Poučení, které tenhle běh potvrdil potřetí:** F-0153 prošlo buildem,
`audit.py`, kontrolou symbolu v `.elf` **i HW testem** — a přesto nefunguje,
jak má. Žádná z těch kontrol na tuhle třídu nedosáhne: všechny ověřují, že se
kód **přeložil a vykonal**, ne že jeho **výstup dává smysl**. Odhalilo to až
přečtení **konzumenta** (`errlog_fmt_detail`) a **mechanismu** (`errlog_put`)
— tedy kódu, který se vůbec neměnil.

✅ **FÁZE OPRAV PROBĚHLA TÝŽ DEN — skupina A schválena („oprav"), všechny
3 nálezy uzavřeny ve 2 commitech.** ⬜ **Neověřeno na HW.**

| nález | commit | jak |
|---|---|---|
| **F-0155 + F-0156** [S3] | `dc6ff88` | **jeden zásah na obě**: nový druh `ERRLOG_K_UARTFATAL` = 12 na KONEC výčtu, vlastní větev ve `errlog_fmt_detail`, jméno `GPS!`, posunutý `ERRLOG_KIND_MAX` |
| **F-0157** [S4] | *(docs)* | explicitní odkaz `viz fmc.c:207/:212` u obou maker + věta, že vynutit to nelze |

🔑 **Past nalezená AŽ při opravě:** `ERRLOG_KIND_MAX` byl navázaný na
**konkrétní** položku (`ERRLOG_K_CFG`), ne na poslední — bez jeho posunutí by
`errlog_put` nový druh **tiše odmítl** (`kind > MAX → return false`) a oprava
by nefungovala, aniž by to cokoli ohlásilo. Varování připsáno na obě místa.

✅ **Oddělení kbelíků prodlevy ověřeno ve slinkovaném obrazu, ne úvahou:**
`s_el_cool_next` má **52 B = 13 × uint32**, `s_el_pending` **26 B = 13 × uint16**
(`nm --print-size --radix=d`) ⇒ druh 12 indexuje vlastní slot nezávisle na
druhu 4. Věta hlásí **následek** (L-0056):
`GPS prijem MRTVY az do resetu (re-arm selhal, pokusu=N)`.

**Ověření (F5.2):** build **0 varování**, `audit.py` **92/0/2**, `.text`
615 112 → **615 184 B** (+72), nový řetězec doložen v `.elf`.

🔴 **Na HW ověřit NELZE a je to strop, ne opomenutí:** spouštěcí porucha dosud
nikdy nenastala (`g_uart1_rearm_fail = 0`, změřeno na desce) a **injektor
errlog záznamu v konzoli neexistuje**, takže novou větev dekodéru není čím
vyvolat. Ověření je proto statické + v obrazu.

**Nová lekce L-0085** (nová varianta pod existujícím druhem zdědí cizí
prodlevu i cizí dekodér; dvě hlášení v jednom průchodu ISR si můžou sdílený
rate-limit vyčerpat navzájem).

**Předchozí, 2026-09-25 (třetí `/audit-modul` běh):** ✅
**Přezkum opravy `bb0d7a7` (F-0148) — 0 nových nálezů.** Stejná třída
kontroly, která F-0148 samo odhalila (viz L-0081): rozšíření
`sd_blocking_begin/end` scope neporušuje L-0078 (žádná smyčka/cross-task
čekání, jen o pár řádků delší neblokující úsek), žádný double-close,
žádné další nezdůvodněné `s_mirror_open=false` bez `f_close()`. Zápis →
`docs/audit/2026-09-25_fixreview-f0148.md`.

**Předchozí, 2026-09-25 (druhý běh):** ✅ **F-0148 [S2] opraveno** (schváleno
uživatelem "a" = skupina A z triáže). Přezkum VLASTNÍCH OPRAV `1d18c28`
(d2d_wait yield) a `caa5f70` (SD zrcadlo F-0142/F-0143/F-0146) 2026-09-24
našel novou regresi: oprava F-0143 v `datalog_mirror_service()` znovu
zaváděla přesně tu chybu (`s_mirror_open=false` bez `f_close()`), kterou
F-0146 opravilo o 20 řádků výš **ve stejném commitu** (nová lekce **L-0081**).
Oprava: `f_close()` guard analogický F-0146, `sd_blocking_begin/end` scope
rozšířen tak, aby zahrnul i erase-detekci (`datalog_sd.c:717-747`).
Build 0 varování, `audit.py` 92/0/2. `.text` beze změny (615096 B) — ověřeno
přímo v `.elf`: `f_close` volání uvnitř `datalog_mirror_service` (po
inlinování `mirror_open`, `static` s jediným volajícím) vzrostla 3→4.
✅ **Naflashováno a regresně ověřeno na HW 2026-09-25** (deska byla
2026-09-24 dočasně bez napájení, o pár minut později zase dostupná):
`selftest` → 16/16 PASS, `status` zdravý, 4× `datalog mirror off/on`
cyklus bez chyby. 🔴 **Cílový scénář (`datalog erase` nad aktivním
zrcadlem) zůstává neověřený** — destruktivní nad živými daty, neprovedeno
bez výslovného souhlasu. Druhý nález **F-0147 [S4]** (nepřesný komentář
u `d2d_wait()`) zůstává odložený (skupina C, `docs:` dluh, neschváleno
k opravě).
Zápis → `docs/audit/2026-09-24_fixreview-d2dwait-sdmirror.md`,
`docs/LESSONS.md` L-0081.
🔑 **Pokračovat zde:** F-0147 komentář jen na vyžádání; jinak audit
backlogu momentálně nezbývá nic S1/S2 otevřeného.

**Předchozí, 2026-09-24 (první běh):** 🔴 **Cílený audit posledních úprav
(6 commitů `837d2d6`…`2fdd202` + 1 nekomitovaná oprava), NE číslovaný modul —
na žádost uživatele "projdi poslední úpravy".** Rozsah: SD dvouton (+ oprava
L-0079), automatické CSV zrcadlo datalogu na SD, ikona SD karty v headeru
(+ odebrání HDOP pilulky), F-0140 dokončeno (lineární copy-forward +
`fbdiff`/`tap`/`screenshot all`), sjednocení textů/stylu tlačítek, a živě
nalezený + opravený **výkonnostní nález mimo audit**: `d2d_wait()` busy-spin
v pomalé větvi (F-0140 zvětšil typický přenos, takže do ní začaly padat i
běžné redraw, ne jen celoobrazovkové) zvedal UiTask CPU% z ~65 na 80-93 %
beze změny skutečné práce — opraveno yieldem (`osDelay(1)` mezi kontrolami),
ověřeno na HW (UiTask 26-44 %, `LTDC podtečení` pořád 0).
Nálezy → `docs/audit/2026-09-24_sezeni-2026-09-23-24.md`: **F-0142 [S2]**
(vodotisk SD zrcadla v syscfg může při dlouhém dohánění zálohy zaostat za
obsahem souboru → duplicitní řádky po výpadku napájení) — **✅ opraveno**
(čte poslední řádek souboru při "same_card" otevření, `max()` s persistovaným
vodotiskem), **F-0143 [S3]** (`datalog erase` neresetuje vodotisk zrcadla →
tiše "zamrzne" jako hotové, L-0011 vzor) — **✅ opraveno** (detekce poklesu
`ds.last_seq` → reset vodotisku + nový soubor), a bonusový **F-0146 [S2]**
nalezený PŘI ověřování F-0142 (vypnutí zrcadla nezavíralo FatFs handle →
opětovné zapnutí selhávalo, jakmile F-0142 přidalo `FA_READ`) — **✅
opraveno** (`f_close()` v `datalog_mirror_set_enabled(false)`), viz
`docs/LESSONS.md` **L-0080**. **F-0144/F-0145 [S4]** zůstávají otevřené,
odložené do skupiny C (jen `docs:` dluh, cena opravy > přínos).
**Verdikt: podmíněně funkční → po opravách F-0142/F-0143/F-0146 funkční.**
⬜ **Opravy ověřeny jen SW toggly přes UART, NE skutečným power-cyklem
v přesném okamžiku poruchy** (výpadek při dohánění / erase za běhu) —
pravidlo 4b platí dál, nutné ověřit na HW po power-cyklu.
✅ **Zakomitováno** (3 commity: `1d18c28` d2d_wait yield, `caa5f70` SD
zrcadlo F-0142/F-0143/F-0146, `a1f4722` docs).
🔑 **Pokračovat zde:** (1) flashnout CM7 Release + skutečný power-cyklus,
ověřit F-0142/F-0143/F-0146 + d2d_wait na desce, (2) F-0144/F-0145
zůstávají jako `docs:` TODO (skupina C, cena opravy > přínos).

**Dodatek 2026-09-24 (stejné sezení): srovnání zastaralého stavu v tomto
souboru.** Tabulka „Přehled modulů" a sekce „Otevřené po F5" **lhaly** —
psaly se před koly oprav 2026-09-19/20 a od té doby se needitovaly, i když
nálezové dokumenty samy ukazovaly opravu. Zjištěno cíleným `grep` přes
všechny `Stav:` řádky v `docs/audit/*.md` (jediný spolehlivý zdroj).
Opraveno: moduly 1–5 v tabulce byly `nálezy zapsány`, přitom KAŽDÝ nález
v nich je `opraveno` (moduly 2/3/4/5 → `opraveno vše`; modul 1 → jen
F-0003 zůstává vědomě odložené na příští CubeMX regen). „Otevřené po F5"
tvrdilo, že F-0007/F-0014/F-0016/F-0017/F-0018 čekají na rozhodnutí/opravu
— všech pět je opraveno (F-0007 a F-0014 19.9., F-0016/F-0017 19.9.,
F-0018 v PLNÉM rozsahu 20.9., ne jen částečně jak tu dřív stálo). Navíc
oprava dokumentační chyby v `docs/audit/2026-09-18_diagnostika-pameti.md`
(F-0116 mělo dvě `Stav:` řádky — druhá, duchovská `otevřeno`, zůstala
po dřívější editaci a moje vlastní grep-kontrola na ni naletěla).
**Poučení pro příště:** tenhle typ zastarání se nehlídá sám — `python
tools/audit_stav.py --kontrola` (zmíněný v textu výše) buď neexistuje,
nebo se nespouští pravidelně; bez něj je jediná záruka manuální průchod
`grep -rn "Stav:" docs/audit/*.md` před každým shrnutím „co chybí".

**Předchozí, 2026-09-22:** ✅ **F-0140 VYŘEŠENO: problikávání při
RUN/STOP byl STRIDED copy-forward, ne propustnost sběrnice.** `copy_forward_dedup()`
kopíroval jednotlivé dirty obdélníky (`FGOR`/`OOR` ≠ 0 = strided přístup do SDRAM,
každý řádek jiná řada) → LTDC přišlo o propustnost, FIFO podteklo, na panel šel
poškozený snímek. Opraveno na **plnošířkové pásy slité po ose Y** (lineární
přístup, `FGOR`=`OOR`=0). **Naměřeno: 38 → 0 podtečení na 20 stisků**; 40 stisků
+ 30 plných renderů = 0/902 flipů; buffery se v STOP srovnají (`fbdiff` shoda);
UiTask 25 % CPU. Build 0 varování, `audit.py` 92/0/2, `.text` 609 456 → 610 952 B.
⬜ **Zbývá vizuální potvrzení uživatelem na displeji** (pravidlo 4b).
🔑 **Rozhodl to rozklad podtečení po FÁZÍCH `present()`** (`kresleni 0 | cekani 0 |
flip 0 | COPY-FORWARD 38`) — dokud se četl jen součet za flip, daly se obhájit
i vyvrátit všechny teorie (velikost burstu, glyph accel, `d2ddt`, `sdrtr`, fáze
vůči vblanku) a žádná nebyla správně. Druhý klíč byl paradox „plný render 768 kB
= 0 podtečení, malé překreslení = podteče vždy" → ne objem, ale **vzor přístupu**.
Nově trvale v kódu: UART **`fbdiff`** (porovná 3 framebuffery + rozklad fází) a
**`tap <0-4>`** (injektor doteku — vada šla reprodukovat jen fyzickým dotekem,
SCPI ekvivalent ne, takže do té doby každé měření stálo kolo s uživatelem).
Nová **L-0077**, revidovaná **L-0076** (původní závěr „fyzický strop sběrnice"
byl mylný). Nekomitováno.

**Předchozí, 2026-09-21:** cílený audit + první (částečná) oprava hlášeného
problikávání hlavní obrazovky při RUN/STOP. Uživatel nejdřív nahlásil jev,
proběhlo v konverzaci **pět neúspěšných pokusů o opravu** (buffer-„settling"
teorie nad `screen_main_redraw_freq()`), všechny odloženy do `git stash` beze
změny stromu — teorie byla po přečtení `prim_stm32_present()` vyvrácena. Na
uživatelův pokyn proveden **bisect** (build z 2026-09-06 přes `git checkout
<hash> -- .`): vada je přítomná i tam → **není regrese posledních ~15 dnů**.
Zapsán řádný nález **F-0140 [S2]** do `audit/2026-09-10_vykreslovaci-retezec.md`
(modul 8, dodatek): `screen_main_redraw_freq_area()` (RUN/STOP, format toggle)
je nejtěžší jednorázová DMA2D zátěž redraw řetězce hlavní obrazovky a **koreluje**
s měřeným podtečením LTDC FIFO (`status` → `LTDC: podtečení FIFO`, 52/1000 při
burst SCPI RUN/STOP toggle). Uživatel schválil opravu („oprav to úplně a
ideálně") → **F-0140 opraveno částečně**: nová `screen_main_redraw_freq_tint()`
(`screen_main.c`/`.h`, volající `app_gpsdo.c:7834,8642`) používá pro RUN/STOP
UZŠÍ DMA2D zónu (`freq_area()` místo `freq_clear_area()`) + bez HW glyph akcelerace
— bezpečné, protože RUN/STOP nemění geometrii čísla (na rozdíl od format-change
volání, které si `screen_main_redraw_freq_area()` ponechává beze změny).
Ověřovací řetězec F5.2 hotový (build 0 varování, `audit.py` 92/0/2, `.text`
+64 B, symbol v `.elf`), naflashováno a **změřeno na běžící desce**:
burst 20× toggle 52→**27**/1000 (d2ddt=240) resp. →**17**/1000 (d2ddt=255).
🔑 **Kombinace obou pák u d2ddt=255 dala prakticky totéž jako d2ddt=255 samotné
(17 vs. 15/1000) — ukazuje to na fyzický strop sdílené SDRAM sběrnice, ne na
chybu v SW** (nová **L-0076**). ⚠️ **Zásadní zjištění:** jediné izolované
přepnutí (reálné použití, ne umělý burst) bylo **čisté (0 podtečení) i PŘED
touto opravou** — LTDC podtečení tedy pro single-tap scénář nic nevysvětlovalo,
opravený ani neopravený kód. **Fyzický dotek nebylo možné otestovat** (žádný
UART hook na `app_gpsdo_handle_touch()`) — **zbývá ověřit uživatelem na desce**,
jestli hlášené problikávání po flashi zmizelo; pokud přetrvává i po JEDNOM
klepnutí, LTDC podtečení není vysvětlením a hledání musí pokračovat jinam.
Detaily, přesná čísla a otevřené otázky v `audit/2026-09-10_vykreslovaci-retezec.md`
(F-0140) a `docs/LESSONS.md` (L-0076). Nekomitováno.

**Předchozí:** 2026-09-20 (pokračování) — **F5 modulu 20 dokončena.**
Všech 5 dřívějších nálezů (F-0122…F-0126, zapsané 2026-09-18) bylo při kontrole
zdrojáku potvrzeno jako **skutečně opravené** (žádná z nich nebyla jen popsaná
v dokumentu — všech pět je ověřitelně v `encoder.c/.h`, `screenshot.c`, `autocal.c`).
Při HW verifikaci F-0125 (`enc div N`) se navíc našla a rovnou opravila **nová
regrese F-0139 [S3]**: UART handler po zafrontování požadavku porovnával proti
STARÉ hodnotě (čtenou dřív, než ji UiTask stihl aplikovat) → `enc div 2`/`enc div 4`
hlásily „neplatny delic" pro platné hodnoty a **persistence do syscfg se nikdy
neuplatnila** (ticho selhávající, dokumentovaná funkce). Opraveno (`encoder_set_div()`
teď vrací, jestli hodnotu přijala, místo aby volající hádal ze zpožděné async
hodnoty), **ověřeno přímo na desce** (`enc div 2`→`ulozeno`, `enc div 9`→správně
odmítnuto). Nová **L-0075**. Modul 20 je tím **kompletně opravený** (6/6 nálezů);
F-0122 (encoder HW počítání) a F-0139 (tahle oprava) jsou navíc ✅ **ověřené na HW**,
zbylé 4 (F-0123 screenshot BMP, F-0126 autocal) čekají na HW test, který dnes
neproběhl (mimo rozsah tohoto sezení).
⚠️ **Ověření je po SW resetu přes sondu, ne po plném power-cyklu** — viz L-0010/pravidlo 4b.

**Předchozí, týž den:** cílený audit encoderové logiky (NE celý modul 20), viz
[`audit/2026-09-20_encoder-fokus-session.md`](audit/2026-09-20_encoder-fokus-session.md).
Přehled dnešního sezení: (1) odstraněno dočasné profilovací
vybavení UiTasku (`UIP_*`, viz `freertos_task_ui.c`) po zodpovězení otázky
„čím je UiTask vytížený" — DMA2D glyph accel rozšiřovat nemá smysl (cíl by
pokryl jen ~4 %); (2) **L-0073** — fokus v cyklických seznamech se ZACYKLÍ
(modulo), ne zarazí na kraji; (3) **L-0074** — po dvou špatných odhadech
designu (rotace mění GATE/CHAN hodnotu → chybějící redraw bug → uživatel
rozhodl celou tu vlastnost zrušit) je finální stav: rotace na hlavní obrazovce
VŽDY jen listuje fokus, žádná výjimka pro GATE/CHAN. **Vše tři body ověřeno
na desce** (flash + SW reset přes sondu, ne plný power-cyklus — viz „Nezkontrolováno"
v nálezovém dokumentu). Cílený audit finálního stavu: **0 nových nálezů**,
jedna hypotéza (kolize `s_focus_shown` napříč okny) vyvrácena čtením `view_set()`.
Také ověřeno na HW a zapsáno do `CLAUDE.md`: **revize silikonu = rev V** (dřív
HYPOTÉZA, teď přímý odečet `STM32_Programmer_CLI` `Revision ID: Rev V`).
⚠️ **Nic z dnešního sezení není zatím komitnuté** (`git status` — `app_gpsdo.c`,
`CLAUDE.md`, `docs/LESSONS.md`, `docs/HW_OVERENI_AUDIT_2026-09-19.md` čekají).

**Předchozí:** 2026-09-19 — **dávka oprav ze skupiny B: 6 nálezů uzavřeno**
(uživatel rozhodl politiky, které F5.0 odkládala). ⬜ **Vše neověřeno na HW.**

| nález | rozhodnutí | commit | co je potřeba ověřit na HW |
|---|---|---|---|
| **F-0134** [S3] | smazat past `HAL_ETH_TxFreeCallback` | `ac5a369` | nic — odstraněný kód se nikdy nevykonával; jen že síť dál jede |
| **F-0138** [S3] | recyklovat rezervu, **`IPC_VERSION` 17→18** | `ac5a369` | `status` → `TX(CM4): odeslano N` musí růst; **flashnout OBĚ banky** |
| **F-0116** [S3] | měřit `fb_alias` vždy **+** příznak `checked` | `61a76f1` | `membench` → žádná z obou nových vět (tedy `fb_alias` 0 a změřeno) |
| **F-0118** [S3] | požadavek + fallback po 300 ms | `61a76f1` | `sdramlog reset` → „vynulovano (producentem)" při běžícím měření |
| **F-0122** [S3] | `.ioc` je jediný vlastník TIM1 | `e9c89a4` | 🔴 `enc` → jedna západka = `kroku=1` (kvůli novým `CC1E/CC2E`) |
| **F-0007 + F-0108** [S3] | „zůstat mrtvý, ale **rozlišitelně**" | `6b1dc19` | nejde bez odpojení HSE/LSE; regresně jen že normální boot funguje |

🔴 **Dvě věci z té dávky, které nálezy popisovaly MÍRNĚJI, než jaká byla skutečnost:**
- **F-0007/F-0108:** nešlo o „blikající LED v krabičce", ale o **naprosto tichou a temnou
  desku** — `bootled_step` startuje na 0 a `blink_pattern(0)` proběhne prázdnou smyčkou.
- **F-0122:** riziko nebylo „střední". `MX_TIM1_Init()` vyrábí **bit za bit tentýž** stav
  registrů, takže přechod na HAL cestu není změna chování; `.ioc` se dokonce **nemusel
  měnit vůbec** (jeho hodnoty už odpovídaly), nekonzistentní byl kód a hlavička.

🔴 **Druhá dávka téhož dne — skupina B dorozhodnuta, 4 další nálezy uzavřeny.**
⬜ **Vše neověřeno na HW.**

| nález | rozhodnutí | commit | co ověřit na HW |
|---|---|---|---|
| **F-0053** [S3] | clamp 0..3 **+** hlídání přetečení int32 **+** počítadlo | `689621c` | `status` → `FORMAT: omezenych desetin 0` |
| **F-0017** [S3] | každé jádro nuluje **svůj** blok sdílené paměti | `689621c` | po **studeném** startu `SCPI(CM4)`/`HTTP(CM4): selftest PASS` |
| **F-0016** [S3] | `.ipc_shared` rezervuje 64 K, a to v **obou** linkerech | `eae40a4` | jen regresně: `CM4: alive`, IPC funguje jako dřív |
| **F-0098** [S3] | `ULOZISTE:` v `status` + řádek v okně PAMĚŤ + amber SYS + retry | `b546aa1` | `ULOZISTE` 5× OK, okno PAMĚŤ `5/5 OK`, SYS zelená |

🔑 **Tři věci, kde byl nález sám nepřesný — a oprava se proto od návrhu liší:**
- **F-0053:** rozsah je **0–3, ne 1–3**. `default:` **není** chybová větev, ale
  legitimní implementace nuly a spoléhají na ni **čtyři skuteční volající**;
  navržené „ořízni na 3" by je rozbilo. A byla tam **druhá mez na HODNOTĚ**
  (`|v| · 10^dec < 2,15e9`), o které nemluvil nikdo.
- **F-0007/F-0108** (první dávka): nešlo o „blikající LED", ale o **tichou a temnou
  desku** — `blink_pattern(0)` proběhne prázdnou smyčkou.
- **F-0098:** retry **nesmí znovu načíst blob** — v RAM může být novější nastavení
  a přečtení staré verze by ho přetlačilo. Obnovuje se **přístup**, ne obsah.

🔑 **F-0016 má negativní test** (`L-0039`): do RAM_D3 se dočasně přidala sekce o 4 B
a link **selhal** (`region 'RAM_D3' overflowed by 4 bytes`). Bez toho by „build prošel"
nedokazovalo, že rezervace vůbec funguje.

**Předchozí:** 2026-09-18 (18. sezení — **modul 18 = senzory / drivery
periferií**: `si5356`, `ads1115`, `ws_panel`, `ft5x06`, `sensor_hist`; F3 i F5
skupina A týž den, 2 nálezy S3 opraveny, ⬜ neověřeno na HW).
✅ **Týž den F5 pro modul 11: F-0052 [S2] + F-0096 [S3] opraveny naráz** (`2253ac4`) —
`g_meas_cfg` se ze všech UI cest (okno MATH, `setup_load`, `syscfg_load`) zapisuje
atomicky (lokální kopie + kritická sekce, vzor scpi/ipc) a `lo>hi` se sanitizuje.
**S2 fronta je tím prázdná.** Zbývají v modulu 11 jen F-0053 [S3] a F-0054 [S4].
🔴 **NOVÉ 2026-09-18 — modul 19 (diagnostika paměti = MĚŘIDLO), 7 nálezů
(4× S3, 3× S4), F3 zapsána, fáze oprav NEproběhla.** Verdikt **podmíněně funkční**.
Nejzávažnější jsou dvě vady třídy „nástroj tvrdí, co neměřil" (**L-0011**):
**F-0116** — `membench` umí vypsat *„framebuffery se navzájem NEpřekrývají"*, aniž tu
kontrolu provedl (měří se jen `if (span)`, ale `alias_off` plní i druhá sonda s jiným
rozsahem); **F-0115** — `SDRAM_PROTECTED[]` neobsahuje `.measlog` (8 MB @`0xC1000000`),
přidaný 2026-08-30, takže bezpečnostní sonda na měřicí log nesáhne.
**F-0117** je zrcadlo F-0115 na druhé straně (`sdram_log` nekontroluje alias na
`bg_cache`) — **opravit spolu**. Dál **F-0118** (druhý zapisovatel `s_head`),
**F-0119** (hlavička pořád tvrdí 16 MB / 16 B / 1 048 576 — zbytek po F-0010),
F-0120, F-0121.
🔑 **Všechny cíle `membench` jsem doložil jako skutečně volné** (`.map` + `nm` obou
obrazů + linker + grep na absolutní adresy) — včetně toho, že `_estack` je v RAM_D1,
ne v DTCM, a že `#pragma location = 0x30000100` v CM4 je pod `__ICCARM__`, tedy pro
GCC neaktivní.
✅ **Skupina A opravena 2026-09-18 (5 ze 7, ve 2 commitech), ⬜ neověřeno na HW.**
`caa08b4` **F-0115 + F-0117** (chráněné oblasti SDRAM na **obou** stranách — `.measlog`
se nově bere z linker symbolu `_smeaslog`, `sdram_log` hlídá i `.sdram`/canvas)
+ část F-0120; `339c596` **F-0119 + F-0121** (`docs:` — zastaralá čísla kapacity,
`.text` binárně nezměněn). Lekce: výskyt **L-0012** s novou větou o **vzájemném**
vyloučení (strany jsou dvě a bývají v různých souborech).
⚠️ **Otevřené zůstávají F-0116 a F-0118 — skupina B, čekají na rozhodnutí o variantě:**
F-0116 (měřit `fb_alias` vždy × zavést `fb_alias_checked`), F-0118 (reset přes příznak
**mění pozorovatelné chování** — při mrtvém linku by `sdramlog reset` nezabral).
🔑 **Co ověřit na desce:** `membench` doběhne jako dřív (SDRAM `OK`, retence 0) — nové
položky smí test nejvýš **přeskočit**, ne hlásit chybu; `sdramlog` po bootu musí být
`ready` (falešný alias by log **nezapnul** a napsal důvod do `fail[]`).
🔴 **NOVÉ 2026-09-18 — modul 20 (metrologie, encoder, export), 5 nálezů (1× S3,
4× S4), F3 zapsána, fáze oprav NEproběhla.** Verdikt **funkční** — žádný S1/S2 a kód
je z velké části čistá logika napsaná velmi dobře (Welford, guardy na každé dělení,
`NaN` se korektně chytí, velké buffery `static`). Jediný S3 **F-0122**: `encoder.h`
slibuje, že v `.ioc` není nic z encoderu — TIM1 i PA8/PA9 tam jsou, `MX_TIM1_Init()`
se volá z `main.c:393` a jeho konfigurace se **nikdy neuplatní**, protože
`encoder_init()` tytéž registry přepíše později z UiTasku. **Skupina B** (varianta
(a) opravit dokumentaci × (b) přejít na CubeMX cestu = zásah do `.ioc`).
Dál **F-0123** (`bmp_header` „sdílí obě cesty" — USB ji nevolá), **F-0124** (výčet
uživatelů scratche vynechává `membench`), **F-0125** (`encoder_set_div` píše stav
UiTasku), **F-0126** (`g_autocal` píšou dvě úlohy).
🔴 **NOVÉ 2026-09-19 — modul 21 (DSI bridge, sdílený SCPI backend, USB CDC),
4 nálezy (1× S2, 2× S3, 1× S4), F3 zapsána, fáze oprav NEproběhla.** Verdikt
**podmíněně funkční**. Modul vznikl jako **mezera nalezená křížovou kontrolou**, ne
podle plánu — a vyplatilo se: je v něm **jediný S2 za poslední čtyři moduly**.
🔴 **F-0127 [S2]:** `usb_console_tx_pump()` uvolní slot kruhového bufferu ve chvíli, kdy
USB stack přenos **přijal**, ne kdy ho **dokončil**. Cesta CDC je prokazatelně
**zero-copy** (`USBD_CDC_SetTxBuffer` ukládá ukazatel, `TransmitPacket` vrací `USBD_OK`
před přenosem, `HAL_PCD_EP_Transmit` jen `xfer_buff = pBuf`, `dma_enable = DISABLE`
→ FIFO plní **USB ISR**), takže producent může přepsat data, která se právě vysílají.
Projev: poškozený výpis konzole a poškozený BMP u `screenshot` přes USB — což se dosud
připisovalo tearingu. Oprava (a) se vejde celá do `usb_console.c`.
Dál **F-0128** (konzole zahazuje v obou směrech bez počítadla — TX drop-oldest i
ignorovaný návrat `osMessageQueuePut`; projekt má vzor `OVF:`/`FONTY:`),
**F-0129** (`ipc_scpi_set_cfg` je napojený i na CM7 → **druhá instance F-0014**),
**F-0130** (`SYSCTRL 0x040F` bez rozkladu).
✅ **Skupina B rozhodnuta a opravena 2026-09-19 (3 ze 4), ⬜ neověřeno na HW.**
`f4f4ebf` **F-0127 + F-0128** (`s_pending` — slot se uvolní až po dokončení přenosu;
uživatel zvolil variantu (a), protože se **hojí sama**: uvolnění navázané na
`CDC_TransmitCplt_FS` by po odpojení hosta nechalo blok držený navždy a konzole by
se jevila trvale plná. Vynutilo si to změnu politiky zahazování → počítadla TX/RX
a řádek `KONZOLE: zahozeno TX n B / RX m B`).
`7cd8613` **F-0129** — provedeno **lépe než návrh F-0014**: soubor se kompiluje
dvakrát, takže stačila jádrová podmínka `#if defined(CORE_CM4)` a validace se
**neduplikovala**. Důkaz: `ipc_scpi_set_cfg` má na CM7 **128 B**, na CM4 **548 B**,
a CM4 `.text` je **bajt za bajtem shodný** → produkční cesta nedotčená.
Lekce: výskyt **L-0025** s novou větou („OK od cizí knihovny = zadání přijato, ne
práce hotova“ + „uvolnění navázané na cizí notifikaci musí přežít, že nepřijde“).
🔴 **OTEVŘENÁ OTÁZKA — `ipccmd` (F-0014):** jeho vlastní návrh opravy je pro něj
**špatný**. Kód u něj má napsaný účel *„pošli příkaz PŘESNĚ tou cestou, kterou
použije CM4 … ověřit ovládací cestu CM4→CM7 bez sítě, bez SCPI a bez webu“*
(kritérium W1), takže „volat `ipc_cfg_apply()` přímo“ by ten příkaz zrušil.
✅ **VYŘEŠENO 2026-09-19 rozhodnutím uživatele:** zvolena právě ta třetí cesta —
`ipccmd` **odmítne běžet**, když `ipc_cm4_alive() && g_web_ctrl_en` (vzor příkazu
`eth`: *„dva masteři na MDIO“*), s únikem `ipccmd force <podpříkaz>`. Účel příkazu
(kritérium W1 — ověřit cestu CM4→CM7 **bez sítě a bez webu**) tím zůstal zachovaný,
protože právě v tom stavu guard nezasahuje. **F-0014 je tím uzavřený**, vznikla
**L-0057**.
Předtím 2026-09-17 (16.–17. sezení — **modul 17 = čas, alarmy,
watchdog**; týž den modul 16 = perzistence
a záznamníky**: `datalog`, `flightrec` + nový `errlog`, `syscfg`, `setup`, `calib`;
týž den fáze oprav, skupina A)
🔴 **NOVÉ 2026-09-19 — modul 22 (CM4: ETH/lwIP glue, boot, IWDG2), 7 nálezů
(5× S3, 2× S4), F3 zapsána, fáze oprav NEproběhla.** Verdikt **podmíněně funkční**;
dvoujádrová část je výborná (per-core RCC, HSEM 1 kolem GPIO initů, degradovaný
bring-up, kde selhání periferie nezabije IPC — a to vše regen-safe).
🔑 **Pět S3 míří do jednoho místa, které tenhle projekt zná nejlépe: „link UP, ale nic
neteče".** **F-0131**: `ethernet_link_check_state` prohlásí `netif_set_up` +
`netif_set_link_up`, **aniž zkontroluje návrat `HAL_ETH_Start`** — a `heth.ErrorCode`
se nečte nikde, ETH IRQ není zapojený vůbec. **F-0132**: počítadlo `g_eth_tx_err`
míjí cestu „pbuf chain > 4 segmenty", která **je dosažitelná**
(`LWIP_NETIF_TX_SINGLE_PBUF` v `lwipopts.h` není definované). **F-0133**: vazba
`ETH_RX_BUFFER_SIZE` ↔ `heth.Init.RxBuffLen` je **jen v komentáři**, přitom rozpor =
zápis DMA za konec pbufu (dnes 1536 = 1536 ✅). **F-0134**: `HAL_ETH_TxFreeCallback`
je **nabitá zbraň** — dnes nestřílí (`HAL_ETH_ReleaseTxPacket` nevolá nikdo a
`HAL_ETH_Transmit` ji nevolá) a `pbuf_ref` chybí, takže účet je vyrovnaný; přepnutí TX
na `Transmit_IT` z toho udělá **dvojí `pbuf_free`**. **F-0135**: když se nepodaří vzít
HSEM 1, CM4 konfiguruje sdílená GPIO **bez zámku a bez záznamu** — právě ta podmínka,
za které vzniká třída PG8/PG11.
Dál **F-0136** (`iwdg2_kick()` 1000×/s tvrdí, že obnovuje watchdog, který nikdy
nestartoval) a **F-0137** (`iwdg2_init()` má tutéž vadu, kterou F-0104 opravil na CM7 —
**oprava se nepřenesla na dvojče**, L-0012; dormantní).
🔑 **Umístění bufferů doloženo z `.map`:** `.eth_dma` @`0x30040000` 12 740 B/32 kB,
deskriptory i RX pool na **systémových** adresách, `ram_heap` @`0x1002ac90` v **CM4
aliasu** — což potvrzuje, proč `eth_dma_addr()` existuje a proč RX nikdy netrpěl.
✅ **Skupina A opravena 2026-09-19 (5 z 8, ve 2 commitech), ⬜ neověřeno na HW.**
`356fe02` **F-0131 + F-0132 + F-0133 + F-0135**, `dd8ef75` **F-0136 + komentář F-0138**
(`docs:`). CM4 `.text` 242 504 → **242 552** (+48 B), **CM7 bajt za bajtem shodný** —
žádný soubor CM7 se nedotkl, takže pro CM7 stačí banka, která je už postavená.
🔑 **U F-0131 nebylo potřeba nové počítadlo:** selhání se projeví jako `NET: DOWN`
místo falešného `NET: UP`, a `net_link` se přes IPC publikuje už dnes.
🔴 **NOVÝ NÁLEZ Z FÁZE OPRAV — F-0138 [S3]:** při opravě F-0132 se ukázalo, že
`g_eth_tx_ok`/`g_eth_tx_err` **nečte nikdo**, přestože komentář tvrdil *„cte je CM7
pres IPC (`status`)"*. Inkrement do neviditelného počítadla je poloviční oprava —
z toho vzniklo **zpřísnění L-0017**: počítadlo musí být (1) zapsané a (2) **dosažitelné
bez ladicí sondy**. Komentář uveden na pravdu; **viditelnost je skupina B a NE před
HW průchodem** (znamenala by zásah do sdílené struktury).
⚠️ **Otevřené zůstávají F-0134** (skupina B: smazat `HAL_ETH_TxFreeCallback` × dopárovat
`pbuf_ref`, což dnes = únik paměti), **F-0137** (skupina C: mrtvý kód, navrženo místo
opravy připsat odkaz na F-0104/L-0055) a **F-0138**.
🔑 **Co ověřit na desce:** `status` → `NET: UP …, IP …` musí pořád naskočit — to je
nejdůležitější regresní kontrola této dávky (F-0131 nesmí falešně blokovat UP). Pak
stáhnout SPA z prohlížeče (ověří TX cestu včetně F-0132/F-0133).

🔑 **HW PRŮCHOD JE PŘIPRAVENÝ: [`docs/HW_OVERENI_AUDIT_2026-09-19.md`](HW_OVERENI_AUDIT_2026-09-19.md)**
— konsolidovaný kontrolní seznam pro **jedno sezení** (29 neověřených `fix:` commitů
z modulů 1–21, seřazeno na **dva restarty**). Obsahuje i past „Set Active → Release
nestačí" a test, který může **zavřít otevřený S1 F-0055** (`selftest` z konzole —
blokátor padl s `4b935c9`).
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
Dál: **skupina B modulu 16** (F-0091/F-0092/F-0095/F-0098 — rozhodnout variantu),
**F-0053/F-0054 modulu 11** (S3/S4, drobné) a **politika oscilátoru F-0007+F-0108**.
✅ **F-0052 [S2] modulu 11 hotový** (`2253ac4`, viz nahoře).
⚠️ Opravy modulu 15 míří do **blobu `SPA_HTML`**, takže po nich MUSÍ projít
`python tools/spa/check.py --build` celý (8 kroků) — zvlášť krok 8 (`nm` nad
`SPA_HTML` = počet bajtů + 1), který jediný definitivně chytí utnutý literál.
Pak ⬜ **naflashovat a ověřit po POWER-CYKLU** — čeká na to modul 14
(`b1aa262`), oprava okna CHYBY (F-0063) a `21ac04e` (buffery do `.bss`, dotýká se
obou jader). `IPC_VERSION` se nemění, takže banky jdou flashovat nezávisle.
🔑 **F-0055 + F-0074 jsou rozhodnuté a odložené do `../STATUS.md` TODO #243:**
zásobník UartTasku se nejdřív konzistentně zvětší v `.ioc` (uživatel), teprve pak
se sáhne na kód — přesun bufferů do `.bss` je konkurenční oprava téhož.
✅ **2026-09-24: `selftest` z konzole je zase bezpečný** — F-0055 ověřen na HW
(viz „Souhrn nálezů" výše), `SELFTEST: 16/16 PASS` bez resetu.
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
✅ **F5 pro modul 11 hotová, vše opraveno:** F-0052 (`2253ac4`, 2026-09-18),
F-0053 [S3] `fmt_fixed` default tiše zahodilo desetiny a F-0054 [S4] rozbitá
`grep` kontrola v `CLAUDE.md` (obě 2026-09-19), F-0055 [S1] ověřen na HW
2026-09-24 (viz „Souhrn nálezů").
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
✅ **`selftest` z konzole je od 2026-09-24 bezpečný** — F-0055 ověřen na HW.
🔴 **Všechny moduly 1–16 mají zapsané nálezy; bez dokončené fáze oprav jsou 11 a 16.**
🔴 **OPRAVA TVRZENÍ (2026-09-16):** do té doby tu stálo *„tím je auditovaný veškerý
vlastní kód projektu"* — **neplatilo to.** Mimo moduly 1–15 leželo ~4 500 ř.
vlastního kódu a 2026-09-13 k nim přibyl celý nový podsystém **`errlog`** (FW v0.9.0,
IPC v17). Modul 16 z toho pokryl 2 456 ř. (perzistence a záznamníky).
## ✅ Pokrytí: každý vlastní `.c` je v souborovém seznamu nějakého modulu (2026-09-19)

Modulem 22 se uzavřela poslední mezera. **Doloženo měřením, ne dojmem:** výčet všech
`.c` v `CM7/Core/Src`, `CM7/app` (+`screens`, `hal`), `CM7/libui/src`, `CM7/libprim/src`,
`CM4/Core/Src`, `CM4/LWIP` = **72 souborů**; po odečtení generovaného CubeMX kódu
(`adc`, `dsihost`, `fmc`, `gpio`, `i2c`, `ltdc`, `quadspi`, `rtc`, `sdmmc`, `spi`, `tim`,
`usart`, `eth`, `*_hal_msp`, `*_timebase_tim`, `syscalls`, `sysmem`) je **každý zbylý
soubor v souborovém seznamu některého z 22 modulů** — jmenovitě, nebo zástupným zápisem
(`libui/*`, `libprim/*`, `freertos*.c`, `screens/…`).

🔴 **Ale „je v seznamu" NENÍ „přečteno řádek po řádku"**, a tuhle větu tu nechávám
schválně: právě přehnané tvrzení o kompletnosti se tu ukázalo jako nepravdivé **třikrát**
(2026-09-16, -18, -19). Hloubka se mezi moduly liší a dva to samy přiznávají:
- **modul 11** (aplikační okna, ≈6 200 ř.) — *rizikově cílený* průchod, ne řádek po
  řádku; geometrie a korektnost obsahu jednotlivých oken se nekontrolovaly;
- **modul 8** (`libui/*`, `libprim/*`, 2 247 ř.) — pokrytý zástupným zápisem, tedy
  s nejmenší dohledatelností jednotlivých souborů.

🔑 **Metoda, kterou se to ověřuje** (zopakovat, než kdokoli prohlásí audit za hotový):
porovnat `ls` všech vlastních `.c` proti souborovým seznamům tabulky a brát „soubor je
někde citovaný" **jako nepokrytý**, dokud v seznamu není. Ruční seznam „co zbývá"
selhal třikrát, `ls` ani jednou.

### Historie mezery (proč je ta věta výše tak opatrná)

Modul 21 uzavřel mezeru na CM7, kterou našla křížová kontrola při modulu 20.
**Táž kontrola pro CM4 (2026-09-19) našla další tři soubory** mimo souborové seznamy
tehdejších 21 modulů — a z nich vznikl **modul 22**:

| soubor | ř. | stav |
|---|---|---|
| `CM4/LWIP/Target/ethernetif.c` | 618 | citovaný v dokumentech modulu 12, ale **v jeho souborovém seznamu není**. 🔴 **Nejcitlivější zbylý kus:** je to lwIP glue včetně ETH DMA cesty — tedy soubor, ve kterém žila past „TX buffery musí mít systémovou adresu `0x30xxxxxx`" (nalezená 2026-09-08 **měřením, ne auditem**). |
| `CM4/Core/Src/main.c` | 431 | boot a hlavní smyčka CM4 (rychlá/pomalá část, `iwdg2_kick`, IPC heartbeat). Moduly 1 a 2 auditovaly `main.c` **CM7**, ne tenhle. |
| `CM4/Core/Src/iwdg2.c` | 31 | 🔴 v žádném dokumentu ani zmíněný. ⚠️ Prakticky **mrtvý** — `iwdg2_init()` je v `CM4/main.c` zakomentovaná (IWDG2 má system-wide reset scope, viz CLAUDE.md), takže priorita je nízká. |

Mimo rozsah zůstává záměrně: **vendor kód** (HAL, FatFs, lwIP, CMSIS), generovaný
CubeMX kód mimo `USER CODE` bloky a **fonty** (generovaná data).

🔴 **OPRAVA ČÍSLA (2026-09-18):** do teď tu stálo „~1 100 ř." a pak „~640 ř." —
**obojí bylo špatně** a druhé číslo jsem odvodil odečtem od toho prvního, aniž bych
ho ověřil. Už samotné čtyři vyjmenované soubory dávaly 1 608 ř. Skutečnost před
modulem 19 byla **2 718 ř.** Dvojí výskyt **L-0014** (ruční souhrn se rozešel se
zdrojem pravdy — jednou v počtu řádků, podruhé v tom, co všechno do seznamu patří);
čísla výše jsou spočítaná z `wc -l` a z `ls` nad skutečnými soubory.
🔴 **NOVÉ 2026-09-18 — modul 18 (senzory / drivery), 2 nálezy (2× S3).** Verdikt
**funkční**. ✅ **Oba opraveny 2026-09-18 (skupina A, 2 `fix:` commity), ⬜ neověřeno
na HW.** **F-0113** (`f128b59`): `si5356_init` nekontroloval návraty apply-sekvence —
selhání zápisu „výstupy ON" nechalo 4× 100 MHz vypnuté, FPGA bez časové základny,
ale init hlásil OK; nově se návrat každého kroku sčítá do `ok` (výskyt L-0003).
**F-0114** (`662e049`): sdílený `ws_write_reg` tiskl `printf` i pro dva runtime
settery (backlight/portc), kteří v komentáři slibovali opak (hlídaný UiTask) —
sjednoceno do `ws_write_reg_ex(…, log)`, settery `log=false` (výskyt L-0028, L-0018).
Oba nálezy ležely v chybových cestách; normální provoz byl správný.
🔑 **Co ověřit na desce jako první:** UART `si5356` → řádek stavu (LOCK/hodiny)
při běžícím FPGA linku; a při umělém výpadku jasu (I2C4) nesmí z UiTasku vzniknout
printf. Build Release CM7 0 varování, `audit.py` 92/0/2, `.text` 605344 → **605376 B**
(+32). `IPC_VERSION` beze změny → stačí flashnout bank1.
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

**Otevřené po F5** — 🔴 **přepsáno 2026-09-24, předchozí verze byla zastaralá**
(psána před koly oprav 2026-09-19/20, nikdy neaktualizovaná — F-0007/F-0014/
F-0016/F-0017/F-0018 mezitím dostaly opravu, kterou tenhle seznam pořád
popisoval jako neprovedenou). Zdroj pravdy jsou vždy **`Stav:`** řádky
v jednotlivých `docs/audit/*.md`, ne tenhle souhrn — ověř `python
tools/audit_stav.py --kontrola`, pokud existuje. Skutečně otevřené zbývá
už jen jedno:
- **F-0003** [S3] `VOSRDY` bez timeoutu — *odloženo 2026-09-20, vědomé rozhodnutí*:
  řádek leží v generovaném `SystemClock_Config()` bez `USER CODE` bloku (regen by
  opravu smazal); oprava (čítač, ne `HAL_GetTick()` — timebase v tom místě ještě
  běží na HSI) je **připravená v `CUBEMX_CHECKLIST.md`** a provede se při nejbližší
  regeneraci, kdy se generovaný kód beztak přepisuje a kontroluje. Do té doby vada
  nastane jen při závadě napájení (deska by byla nepoužitelná tak jako tak).
- **F-0007** [S3] ztráta HSE = mrtvý přístroj — **opraveno 2026-09-19**, politika
  „zůstat mrtvý, ale rozlišitelně" (`bootled.c`, ≥15 bliknutí = hodiny, ne periferie;
  zapsáno i v `CLAUDE.md`). ⬜ neověřeno skutečným výpadkem HSE na HW.
- ✅ **F-0014** [S3] `ipccmd` je druhý producent SPSC ringu — **opraveno 2026-09-19**
  (⬜ neověřeno na HW), spolu s druhou instancí **F-0129**. Každá **jinak**, protože
  návrh opravy v nálezu byl pro `ipccmd` špatný: `scpi ipc` ring nepotřebuje (jádrová
  podmínka), `ipccmd` ho potřebuje (to JE ten test), takže se místo zrušení **odmítne
  spustit**, když je `ipc_cm4_alive() && g_web_ctrl_en` (únik: `ipccmd force`).
  Vznikla z toho **`L-0057`** („odmítnutí je plnohodnotná oprava").
- **F-0016** [S3] `.ipc_shared` je prázdná rezervace — **opraveno 2026-09-19**,
  varianta (b) dle rozhodnutí uživatele. ⬜ neověřeno na HW.
- **F-0017** [S3] `ipc_stamp()` maže i blok CM4 — **opraveno 2026-09-19**, varianta
  „každé jádro nuluje svůj blok". ⬜ neověřeno na HW.
- **F-0018** [S1] — **opraveno 2026-09-20 v plném rozsahu** (dvoufázový zápis přes
  SDRAM staging, přesně jak nález navrhoval); dřívější poznámka „jen částečně" už
  neplatí. ⬜ neověřeno na HW (kritérium: `stacktest yes` → po restartu `status`
  hlásí „zachráněných po restartu 1").

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
| 1 | konfigurace hodin/PWR | `main.c`, `system_*.c` | CM7 | **opraveno vše krom F-0003** (odloženo na regen, ⬜ neověřeno na HW) | 2026-09-09 | 0 | 2 | 5 | 0 | [7](audit/2026-09-09_hodiny-pwr.md) |
| 2 | MPU / cache / linker | `main.c MPU_Config`, `*.ld` | oba | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-09 | 0 | 0 | 4 | 2 | [6](audit/2026-09-09_mpu-cache-linker.md) |
| 3 | IPC CM7↔CM4 (HSEM) | `ipc.c`, `ipc_shared.h`, `ipc_cm4.c` | oba | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-09 | 0 | 0 | 4 | 0 | [4](audit/2026-09-09_ipc-cm7-cm4.md) |
| 4 | přerušení a RTOS | `stm32h7xx_it.c`, `freertos*.c` | oba | **opraveno vše** (F-0055 ✅ ověřeno HW 2026-09-24, zbytek ⬜) | 2026-09-11 | 2 | 1 | 0 | 1 | [3](audit/2026-09-10_preruseni-rtos.md) |
| 5 | drivery: I2C1 + I2C4 | `i2c.c`, `*_sensors.c`, `*_ui.c`, `ft5x06.c`, `ws_panel.c` | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-10 | 0 | 0 | 2 | 0 | [2](audit/2026-09-10_i2c.md) |
| 6 | drivery: SPI2/FPGA + QSPI/W25Q | `fpga_freq.c`, `w25q.c`, `w25q_store.c` | CM7 | opraveno (⬜ neověřeno na HW) | 2026-09-10 | 0 | 0 | 1 | 1 | [2](audit/2026-09-10_spi-qspi.md) |
| 7 | drivery: SDMMC + FatFs | `sd_export.c`, `datalog_sd.c`, `sd_diskio.c`, `sdmmc.c`, `fatfs.c`, `bsp_driver_sd.c` | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-11 | 0 | 2 | 4 | 1 | [7](audit/2026-09-10_sdmmc-fatfs.md) |
| 8 | vykreslovací řetězec | `prim_stm32_hal.c`, `libprim/*`, `libui/*` (bez fontů) | CM7 | **opraveno vše** (F-0140 ✅ změřeno 38→0, ⬜ vizuálně nepotvrzeno) | 2026-09-22 | 0 | 2 | 4 | 0 | [6](audit/2026-09-10_vykreslovaci-retezec.md) |
| 9 | hlavní obrazovka | `screens/screen_main.c`, `screen_main_data.c` | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-11 | 1 | 1 | 0 | 1 | [3](audit/2026-09-11_hlavni-obrazovka.md) |
| 10 | navigace, model fokusu, vstup, tiky | `app_gpsdo.c` (strojovna, ≈2 800 ř.) | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-11 | 0 | 1 | 3 | 2 | [6](audit/2026-09-11_navigace-fokus-vstup.md) |
| 11 | aplikační okna (`render_*`) | `app_gpsdo.c` (≈6 200 ř.) | CM7 | **opraveno 3 ze 4** (F-0053/F-0054 otevřené, ⬜ neověřeno na HW) | 2026-09-11 | 0 | 2 | 1 | 1 | [4](audit/2026-09-11_aplikacni-okna.md) |
| 12 | síťová vrstva: HTTP + SCPI/TCP + mDNS | `httpd_min.c` (bez SPA blobu), `scpi_tcp.c`, `lwip_app.c` (≈1 460 ř.) | **CM4** | **opraveno 6 ze 7** (1 odložený, ⬜ neověřeno na HW) | 2026-09-11 | 0 | 2 | 3 | 2 | [7](audit/2026-09-11_sit-cm4.md) |
| 13 | parsery nedůvěryhodného vstupu: SCPI + NMEA | `scpi.c` (1 472 ř.), `gps.c` (511 ř.) | **oba** (`scpi.o` je i v obrazu CM4) | **opraveno vše** (✅ část ověřena na HW) | 2026-09-12 | 0 | 1 | 6 | 2 | [9](audit/2026-09-12_parsery-scpi-gps.md) |
| 14 | UART konzole (parser příkazů) | `freertos_task_uart.c` (2 365 ř., z toho `UartTask_run` 1 966 ř.) | CM7 | **opraveny 3, 1 částečně, F-0074 → TODO #243** (⬜ neověřeno na HW) | 2026-09-12 | 0 | 0 | 3 | 2 | [5](audit/2026-09-12_uart-konzole.md) |
| 15 | webová SPA (klientský dashboard) | `httpd_min.c` = blob `SPA_HTML` (~3 060 ř., 125 funkcí JS) | **prohlížeč** (obraz CM4) | **opraveno vše** (11 z 11, ⬜ neověřeno na HW) | 2026-09-12 | 2 | 1 | 3 | 5 | [11](audit/2026-09-12_spa-web.md) |
| 16 | perzistence a záznamníky | `datalog.c`, `flightrec.c` (+ **errlog**), `syscfg.c`, `setup.c`, `calib.c` (2 456 ř. vč. hlaviček) | CM7 (kanál `errlog` i na CM4) | **opraveno 7 ze 14** (A + F-0096, ⬜ neověřeno na HW) | 2026-09-17 | 1 | 1 | 8 | 4 | [14](audit/2026-09-16_perzistence-zaznamniky.md) |
| 17 | čas, alarmy, watchdog | `rtc.c`, `alarm.c`, `watchdog.c`, `beeper.c`, `bootled.c` (~1 290 ř. + hlavičky) | CM7 | **A+B opraveno** (9 z 10, 1 odložen, ⬜ neověřeno na HW) | 2026-09-17 | 0 | 1 | 5 | 4 | [10](audit/2026-09-17_cas-alarmy-watchdog.md) |
| 18 | senzory / drivery periferií | `si5356.c`, `ads1115.c`, `ws_panel.c`, `ft5x06.c`, `sensor_hist.c` (1 004 ř. vč. hlaviček) | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-18 | 0 | 0 | 2 | 0 | [2](audit/2026-09-18_senzory-drivery.md) |
| 19 | diagnostika paměti (**měřidlo**) | `membench.c`, `sdram_log.c` (1 269 ř. vč. hlaviček) | CM7 | **skupina A opravena** (5 ze 7, 2 čekají na rozhodnutí, ⬜ neověřeno na HW) | 2026-09-18 | 0 | 0 | 4 | 3 | [7](audit/2026-09-18_diagnostika-pameti.md) |
| 20 | metrologie, encoder, export | `meas_present.c`, `encoder.c`, `screenshot.c`, `phase_noise.c`, `autocal.c`, `meas_math.c` (1 449 ř. vč. hlaviček) | CM7 (+`meas_math` i CM4) | **opraveno vše** (6 z 6, F-0122+F-0139 ✅ na HW, 4 zbylé ⬜) | 2026-09-20 | 0 | 0 | 2 | 4 | [6](audit/2026-09-18_metrologie-encoder-export.md) |
| 21 | DSI bridge, sdílený SCPI backend, USB CDC | `tc358762.c`, `ipc_scpi.c`, `usb_console.c` (426 ř. vč. hlaviček) | CM7 (+`ipc_scpi` i CM4) | **opraveno 3 ze 4** (F-0130 [S4] otevřen, ⬜ neověřeno na HW) | 2026-09-19 | 0 | 1 | 2 | 1 | [4](audit/2026-09-19_bridge-ipcscpi-usbcdc.md) |
| 22 | CM4: ETH/lwIP glue, boot a smyčka, IWDG2 | `ethernetif.c`, `CM4/Core/Src/main.c`, `iwdg2.c` (1 218 ř. vč. hlaviček) | **CM4** | **skupina A opravena** (5 ze 8, ⬜ neověřeno na HW) | 2026-09-19 | 0 | 0 | 6 | 2 | [7](audit/2026-09-19_cm4-eth-boot.md) |
| **23** | **generované init soubory periferií** (`MX_*_Init` + `USER CODE`) | **`fmc.c`** (180 ř. ručně), **`usart.c`** (88 ř. ručně), dál `adc.c`, `dsihost.c`, `ltdc.c`, `quadspi.c`, `spi.c`, `tim.c`, `eth.c` (17–19 ř. ručně) | oba | **opraveno 4 z 6, ✅ OVĚŘENO NA HW po studeném startu** (F-0149/150/151/153). 🔴 **F-0152 ZAMÍTNUTO měřením** (zdržení je nosné pro USB CDC → vráceno, L-0084); F-0154 [S4] odložen do C | 2026-09-25 | 0 | 1 | 2 | 3 | [6](audit/2026-09-25_generovane-init-periferie.md) |
| **24** | **matematické funkce měření** (průřezový) | `meas_math.c`, `phase_noise.c`, `meas_present.c` celé; matematické úseky `screen_main.c`, `app_gpsdo.c`, `fpga_freq.c`, `rtc.c` (+ převody dBm v `scpi.c`, `httpd_min.c`) | oba | **1. průchod: opraveno 12 z 13** (F-0168 odložen do C; ⬜ neověřeno na HW). **2. průchod: opraveno 8 z 9** (vč. F-0179 float podlaha; F-0176 [S4] odložen do C s F-0168; ⬜ neověřeno na HW) | 2026-09-26 | 0 | 2 | 11 | 9 | [13](audit/2026-09-26_matematika-mereni.md) + [9](audit/2026-09-26_matematika-mereni-2.md) |

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

🔴 **Srovnáno 2026-09-26** (po F5 modulu 24) — čísla níže jsou výstup
`tools/audit_stav.py` (172 nálezů celkem). Tabulka byla rozjetá už před tímto
sezením: moduly 23 (F-0026 částečně, F-0152 zamítnuto) a F-0144/F-0145/F-0147
(mezitím opraveny `docs:`) se do ní nepropsaly.

| Severity | Otevřené | Opravené | Zamítnuté (wontfix + důvod) |
|---|---|---|---|
| S1 | 0 | 6 | 0 |
| S2 | 1 | 23 | 0 |
| S3 | 2 | 86 | 0 |
| S4 | 4 | 50 | 0 |

⚠️ **Čísla nepiš ručně** — `python tools/audit_stav.py --kontrola` je odvodí z nálezových
dokumentů a při rozporu skončí nenulovým kódem (lekce **L-0014**). Sloupec „Otevřené“
zahrnuje i **částečně** opravené — dnes dvě: **F-0026** [S2] (závod vytažení karty
během zápisu čeká na souhlas uživatele s HW testem, `../STATUS.md` TODO #254) a
**F-0061** [S4] mDNS responder (4 drobné odchylky od RFC 6762, žádná paměťově nebezpečná).
✅ **S1 nově BEZ otevřených (aktualizováno 2026-09-24) — F-0055 ověřen na HW.**
UART `selftest` deterministicky přetékal zásobník UartTasku a shazoval desku
(změřeno 2×, `WATCHDOG! stack:UartTask`). Po flashi aktuálního HEAD + SW resetu
(`STM32_Programmer_CLI -rst`) `selftest` doběhl **„SELFTEST: 16/16 PASS" bez
resetu**, `status`/`stats` ukázaly `stack Uart free 6360 B` (bylo 168 B), ověřeno
2× (uptime 46 s a znovu 88 s). ⬜ Zbývá jen fyzický power-cyklus (formalita —
vada byla vlastností běžícího programu, ne cold-boot závodu).
**S2 „Otevřené" = jen F-0026 (částečně)** — F-0148 opraveno 2026-09-25,
F-0158 (disciplinace LSE) opraveno 2026-09-26, obojí ⬜ neověřeno na HW.
**S3 „Otevřené" = F-0003** (VOSRDY timeout, vědomě odloženo na příští CubeMX regen)
**+ F-0152** (návrh zamítnut měřením na HW — zdržení je nosné, L-0084; v dokumentu
zůstává otevřený jako záznam, ne jako dluh).
**S4 „Otevřené" = F-0061 (částečně) + F-0154 + F-0168** (F-0154 a F-0168 skupina C;
F-0168 = metrologická definice rozpočtu nejistoty, otevřít s reálnými daty z FPGA).
**+ F-0176** (druhý průchod modulu 24: GUM u rozlišení v rozpočtu nejistoty — skupina C,
rozhoduje se spolu s F-0168). Ostatní nálezy druhého průchodu (F-0171..F-0175, F-0177,
F-0178) opraveny 2026-09-26, ⬜ neověřeno na HW.

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
