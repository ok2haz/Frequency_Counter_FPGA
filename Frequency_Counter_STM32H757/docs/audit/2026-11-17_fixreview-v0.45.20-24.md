# Audit: přezkum změn v0.45.20 … v0.45.24 (vypnutí kanálu, K DAC, readback DAC, HW rev. 2.1, jednorázové děje, `view`)  (2026-11-17)

- **Commit:** 7209368 (v0.45.24); přezkoumané kódové commity `9f6115d`, `e5f48da`, `5dbc057`, `07c8085`, `04f8940`, `4f8686d`, `7209368`
- **Jádro / doména:** CM7 (`fpga_freq.c`, `fpga_v3.c/.h`, `app_gpsdo.c`, `screen_main.c`, `freertos_task_sensors.c`, `freertos_task_uart.c`,
  `main.c`, `alarm.c`, `autocal.c`, `scpi.c`, `ipc.c`, `ipc_scpi.c`), CM4 (`httpd_min.c` – JSON + SPA)
- **Projité sekce checklistu:** A (pořadí PH1: ODR před režimem, hodiny GPIOH a VREFBUF před zápisem registrů, HSE bypass),
  D (sdílený stav: `s_pw_skew_*`/`s_ti_off_*` pod PRIMASK, `g_dual_web`, `s3_dual_req`, `g_dac.*`; readback pod mutexem I2C1),
  E (návratové hodnoty `ad5693_readback`, `HAL_SYSCFG_EnableVREFBUF`, čekací smyčky s mezí), G (I2C1 readback, ADC3/VREF+), B jen IPC
  (klíč `IPC_CFG_DUAL`, bit `IPC_F_DUAL`, `_Static_assert`).
- **Neprojité (a proč):** C (žádný nový DMA buffer), F (flash), H (errata) – změny se jich netýkají; `.map` – nový buffer žádný.
- **Precedent:** `/audit-modul` bez jména, žádný `nezačato` → přezkum posledních kódových změn (jako F-0155/F-0199/F-0225/F-0295).

## Souhrn
Sedm commitů od přezkumu 2026-11-14: vypnutí neměřeného kanálu (FW 0x050B), K DAC 3,85e-11, readback DAC za běhu serva,
požadavky HW rev. 2.1 (PH1, VREFBUF, VREF+, VBAT chybí), jednorázové děje (interval A→B ze souhrnu B, uložené zpoždění, nula
intervalu, okna UDÁLOSTI a SPOJITÉ, web DUAL) a příkaz `view`. Většina změn je správná a část už ověřená na desce
(interval A→B 602 / 51 zahozených, `SENS:FREQ:DUAL` SHODA, VREFBUF `CSR=0x09`, readback 24966 = kód).
**Nejzávažnější nález F-0297 [S2]:** NULA intervalu A–B (tlačítko i `fpga ab zero`) bere průměr intervalů „první hrana B po A“,
který je při zpoždění B–A kolem nuly nebo záporném dvojznačný o celou periodu; simulace nad skutečným `fpga_v3_ab_feed` dává
nulu 0,4 ns až 99,5 ns místo zpoždění, a uloží se bez kontroly rozptylu. Dále AUTO-CAL ukáže při vadné VREF+ zelené „OK“ (F-0298),
tlačítko ZRUŠIT KAL. nic nezruší (F-0299) a čtyři drobnosti S4. **Verdikt: podmíněně funkční** (nula A–B a AUTO-CAL VREF). **Fáze oprav proběhla téhož dne (v0.45.25), viz konec zápisu.**

---

### F-0297 [S2] NULA intervalu A–B je dvojznačná o periodu, když je zpoždění B–A kolem nuly nebo záporné; ukládá se bez kontroly

- **Místo:** `CM7/app/app_gpsdo.c:10217` (tlačítko NULA A-B: `fpga_freq_ti_offset_set(ei.ab_mean, 1)` při `ab_n >= 8`),
  `CM7/Core/Src/freertos_task_uart.c:3276-3284` (`fpga ab zero`: průměr posledních ≤ 64 intervalů, `n >= 8`);
  definice intervalu `CM7/Core/Src/fpga_v3.c:453-475` (`ab_burst`) a `fpga_v3_ab_feed` (stop = PRVNÍ hrana B po startu A);
  odečet `CM7/app/screens/screen_main.c:1228` (`t = mean - corr_ps`), UART `uart_ab_show`, okno UDÁLOSTI.
- **Popis:** nula má být zpoždění cest δ = (B − A) při témž signálu na obou vstupech. Interval „první hrana B po hraně A“ ale
  nabývá hodnot jen v (0, perioda]: pro δ < 0 vyjde perioda − |δ|, pro |δ| v řádu šumu směs „≈ 0“ a „≈ perioda“. Ochrana
  `FPGA_V3_AB_GUARD_U` (305 ps) to neodstraní: zahodí jen start, jehož předchozí hrana B leží ve STEJNÉM segmentu
  (`prev_ok` při `k <= 1 - P` je vždy pravda, předchozí hrana z minulého segmentu se nekontroluje), a nad 305 ps nechá projít
  hodnotu ≈ perioda. Nula se pak spočte jako prostý průměr a uloží bez kontroly σ ani rozsahu min/max.
- **Důkaz:** simulace `docs/audit/sim/2026-11-17_null_wrap.c` (výstup `…_null_wrap.txt`) volá SKUTEČNOU `fpga_v3_ab_feed`
  (přeloženo spolu s `fpga_v3.c` jako host test), 10 MHz na A i B, 20 000 segmentů po 25 ms, náhodná fáze vůči hranici segmentu:

  | δ (B−A) | σ značky | přijato (≈0 / ≈perioda) | NULA = průměr | σ |
  |---|---|---|---|---|
  | +500 ps | 70 ps | 20 000 / 0 | 499,4 ps ✓ | 99 ps |
  | +200 ps | 70 ps | 19 574 / 0 | 205,7 ps (+6 ps zkreslení) | 93 ps |
  | 0 | 70 ps | 10 027 / 33 | **405,6 ps** | 5,7 ns |
  | −90 ps | 70 ps | 3 646 / 337 | **8,48 ns** | 27,7 ns |
  | −200 ps | 70 ps | 408 / 2 888 | **87,3 ns** | 32,8 ns |
  | −500 ps | 70 ps | 0 / 19 503 | **99,49 ns** (σ vypadá zdravě) | 93 ps |
  | +500 ps | 250 ps | 18 454 / 212 | **1,68 ns** | 10,5 ns |

  Při σ značky 250 ps (σ intervalu ~350 ps, řádově jako šířka pulzu na desce 342–458 ps) je nula špatně i pro δ = +500 ps.
  STA starší FW uvádí rozdíl cest pin→hlava A 6,23 ns / B 6,14 ns (CLAUDE.md, FW 0x0400), tedy δ ≈ −90 ps – přesně oblast,
  kde se výsledek rozpadá. Znaménko δ na dnešní desce včetně rozbočovače a kabelů je **HYPOTÉZA**.
- **Dopad:** uložená nula se odečítá od intervalu A–B a fáze na hlavní obrazovce, v UART i v okně UDÁLOSTI. Při δ < 0 je nula
  o celou periodu kalibračního signálu mimo (10 MHz: −100 ns na každém dalším měření) a σ přitom vypadá normálně; při δ ≈ 0 je
  náhodná (0,4–87 ns). Deterministické pro dané zapojení, bez varování. Měření kmitočtu se netýká.
- **Reprodukce:** host: `docs/audit/sim/2026-11-17_null_wrap.c` (překlad jako `tests/host/t_fpga_v3.c`). HW: rozbočovač na A i B,
  `fpga ab on`, `fpga ab` → když `max` ≈ perioda signálu nebo σ ≫ 1 ns, nula by byla špatně.
- **Návrh opravy:** nulu počítat v periodické doméně: každý interval rozbalit do (−T/2, T/2] podle periody signálu A (zná ji
  okno i `fx_eval`), průměrovat rozbalené hodnoty a nulu odmítnout, když je σ nad mezí (např. 1 ns) nebo n malé; totéž pro UART
  i tlačítko (dnes tlačítko bere průměr od vynulování, UART průměr posledních ≤ 64 – sjednotit). Volitelně ukládat k nule P obou
  kanálů a při změně předděličky ji zneplatnit (**HYPOTÉZA**: zpoždění cesty se může s P lišit, neměřeno).
- **Riziko opravy:** nízké (jen cesta nastavení nuly; měření intervalu se nemění).
- **Vztah k lekcím:** L-0271 (fáze je periodická, veličina nad ní není spojitá), L-0295 (kalibrace jen pro cestu měření),
  L-0193 (scénáře testu z rozsahu přístroje – host test `ab_burst` nemá scénář B = A ± šum přes hranice segmentu).
- **Stav:** opraveno 2026-11-17 v `6cc9cd3` (v0.45.25), **jinak, nez nalez navrhoval**: misto prevodu intervalu do (-T/2, T/2] (prevedene intervaly maji diru -305..0 ps od ochrany; pri delta -90 ps by nula vysla ~+30 ps) se v `ab_burst` PRED ochranou pocita znamenkova vzdalenost nejblizsi hrany B (`fpga_v3_ab_t.nr`), nula = jeji prumer pri n >= 100 a sigma <= 1 ns (`fpga_freq_ti_zero_eval`, jedno misto pro tlacitko i UART); host test (delta -500..+500 ps) + 5 mutaci. Zneplatneni nuly pri zmene predelicky (HYPOTEZA) neprovedeno. Neovereno na HW

### F-0298 [S3] AUTO-CAL při VREF+ mimo meze hlásí „VREF --“ a celkově zelené OK

- **Místo:** `CM7/Core/Src/freertos_task_sensors.c:989` (v0.45.22, #385: `sensor_fail(SENS_VDDA)` při VREF+ mimo 2400–2600 mV),
  `CM7/Core/Src/autocal.c:23-27` (`check()`: neplatný senzor → `AC_NA`), `autocal.c:108-109` a `CM7/app/app_gpsdo.c:10946-10948`
  (nejhorší verdikt ignoruje `AC_NA`).
- **Popis:** do v0.45.22 se VREF+ mimo pásmo ohodnotila `ac_verdict` (±4 % PASS, ±8 % WARN, jinak FAIL). Od #385 je senzor
  VREF při téže hodnotě neplatný, `check()` vrátí NA a souhrn i barva tlačítka AUTO-CAL ho přeskočí. VBAT je v tu chvíli také
  neplatná (NA). Výsledek: „AUTO-CAL: VREF -- 3V3 OK 5V OK VBAT -- [OK]“ zeleně.
- **Důkaz:** pásmo `VREF_OK_MIN/MAX_MV` (2400–2600, `sensor_stat.h`) je přesně PASS pásmo `check(SENS_VDDA, 2500, 0.04)`
  (`autocal.c:32`), takže každá hodnota, kterou by AUTO-CAL hodnotil WARN/FAIL, se stane neplatnou dřív, než ji vidí.
- **Dopad:** samokontrola dá zelenou právě při poruše, kterou #384/#385 hlídají (VREFBUF nenaběhl, VDDA vypadla). Ostatní
  ukazatele ji vidí (`status` řádek `VREFBUF:`, SYS pilulka přes `err_streak`), takže je to rozpor, ne úplné utajení.
- **Reprodukce:** staticky z kódu; HW: `vrefbuf` vypnout nelze z UART, ověřit až na rev. 2.1 nebo simulací v `selftest`.
- **Návrh opravy:** v `autocal_run` hodnotit VREF z `last`, i když je senzor neplatný kvůli mezi (`samples > 0`), nebo
  `g_vref_bad_n`/poslední `g_vref_bad_mv` převést na FAIL; NA nechat jen pro „nikdy neměřeno / chyba čtení ADC“.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0291 (hodnota proti referenci platná až po ověření reference – oprava byla správná pro odvozené
  veličiny, ne pro samotnou referenci), L-0017 (tichý přeskok jen s počítadlem / viditelně).
- **Stav:** opraveno 2026-11-17 v `77e1512`, tak jak nalez navrhoval (VREF+ mimo meze se hodnoti z `last`). Neovereno na HW

### F-0299 [S3] ZRUŠIT KAL. (okno UDÁLOSTI) a `fpga pw clear` během kalibrace uložené zpoždění nezruší

- **Místo:** `CM7/app/app_gpsdo.c:10221` (tlačítko v režimu KAL. ZPOŽDĚNÍ → `fpga_freq_pw_skew_clear()`),
  `CM7/Core/Src/fpga_freq.c:1469-1471` (`s3e_rec`: při párování SKEW a `sk.n >= 50` se uložená hodnota přepíše z běžící statistiky).
- **Popis:** tlačítko ZRUŠIT KAL. je jen v režimu 2, kde FPGA páruje v režimu SKEW. `fpga_freq_pw_skew_clear()` vynuluje kopii,
  ale statistika `s3e_pr.sk` dál běží, a FpgaTask ji při dalším záznamu (≤ 25 ms) uloží znovu.
- **Důkaz:** pořadí `fpga_freq_pw_skew_clear` (nuluje jen `s_pw_skew_*`) proti `s3e_rec` (podmínka `pr == FPGA_EVP_SKEW &&
  s3e_pr.sk.n >= SKEW_MIN_N` při každém záznamu).
- **Dopad:** uživatel kalibraci „zruší“, řádek „korekce“ ji do 0,5 s ukáže znovu a šířka pulzu se dál opravuje. UART
  `fpga pw clear` funguje jen mimo režim kalibrace.
- **Reprodukce:** okno UDÁLOSTI → KALIBROVAT > → počkat na 50 vzorků → ZRUŠIT KAL. → řádek „korekce: ulozeno …“ zůstane.
- **Návrh opravy:** zrušení spojit s restartem statistiky SKEW (`fpga_freq_ev_pair_req(FPGA_EVP_SKEW)`) nebo s přepnutím
  párování mimo SKEW; případně ukládat jen při odchodu z režimu kalibrace.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0101 (když nuluješ stav, který plní jiný task, vyprázdni i to, z čeho se plní).
- **Stav:** opraveno 2026-11-17 v `c587cae`: priznak `s_pw_skew_blk` blokuje ulozeni az do restartu statistiky SKEW; navic tlacitko ZRUSIT KAL. kalibraci ukonci a vrati okno na SIRKU PULZU. Neovereno na HW

### F-0300 [S4] „BEZ DAT“ v okně UDÁLOSTI kreslené fontem `mono_30`, který nemá písmena ani mezeru

- **Místo:** `CM7/app/app_gpsdo.c:4476`.
- **Popis:** `mono_30` má jen `0123456789,.+-` (`CM7/libui/tools/font_gen/gen_fonts.js:51`, `glyph_count = 14`, bez mezery);
  `prim_draw_text` chybějící glyf přeskočí (`CM7/libprim/src/text.c:141`). Text je neviditelný a každé vykreslení přičte 7
  k počítadlu `FONTY`.
- **Důkaz:** výše; na desce (v0.45.23, uptime 5350 s) `status` → `FONTY: preskocenych glyfu 7`, poslední chybějící znak ale
  U+2014 ve fontu s ascentem 14, takže těchto 7 z „BEZ DAT“ prokazatelně není (viz Omezení).
- **Dopad:** místo velkého čísla prázdná plocha; důvod je vidět v řádku pod ním. Znečišťuje diagnostiku `FONTY`.
- **Návrh opravy:** `sans_18` / `mono_22` (plná sada), nebo „--“ v `mono_30`.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0178 (před řetězcem ve fontu `mono_25/30/52/75`, `sans_32` ověř sadu).
- **Stav:** opraveno 2026-11-17 v `5f8f09e` (`mono_25`, ma velka pismena i mezeru). Neovereno na HW

### F-0301 [S4] Hodnoty `kv_row_live` v nových oknech jsou delší než mazací box (ocas textu se nemaže)

- **Místo:** `CM7/app/app_gpsdo.c:5283-5290` (`kv_row_live`: box 380 px od `DG_LLBL + 250` = x 280, `mono_18` = 11 px/znak, tedy
  nejvýš 34 znaků); `app_gpsdo.c:4584` (SPOJITÉ „zaznamy / mezery“), `app_gpsdo.c:4499` (UDÁLOSTI „korekce: zadna (NULA A-B pri
  spolecnem signalu)“).
- **Popis:** `"%lu / %lu (zmeskano %lu), preruseni %lu"` má 35 znaků od 10 000 přijatých záznamů (~2 min po startu, 80 záznamů/s)
  a 37 znaků (407 px) od 1 000 000; „zadna (NULA A-B pri spolecnem signalu)“ má 38 znaků (418 px, do x 698). Text za x 660 se
  před překreslením nemaže.
- **Důkaz:** šířky spočteny z `ui_font_mono_18.c` (advance 11 u všech glyfů) a délek řetězců; vizuální projev **HYPOTÉZA**
  (ověřit `view 56` / `view 55` + `screenshot` po flashi v0.45.24).
- **Dopad:** v okně SPOJITÉ se konec řádku přepisuje každé 0,5 s bez mazání (AA hrany tmavnou), při přibývající číslici nebo
  změně „preruseni“ zůstanou zbytky; v okně UDÁLOSTI po nastavení nuly nebo změně režimu zůstane vpravo zbytek „alu)“.
- **Návrh opravy:** kratší text (např. `N / mezer G (M), prer. R`), nebo širší mazací box pro tato okna.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0284 (partial redraw musí zahrnout vše, co kreslí), CLAUDE.md „Každý partial redraw MUSÍ začít clear“.
- **Stav:** opraveno 2026-11-17 v `5f8f09e`: SPOJITE radek rozdelen na `zaznamy:` a `mezery / preruseni:` (log SDRAM o radek niz, `VYPNUT %.26s`), UDALOSTI `zadna (NULA A-B, spolecny signal)` (33 znaku). Neovereno na displeji

### F-0302 [S4] Webový popis kódu DAC uvádí starou strmost „cca 3,4e-11“

- **Místo:** `CM4/LWIP/App/httpd_min.c:2409` (SPA: `(1 LSB = 0,076 mV = cca 3,4e-11 relativne)`).
- **Popis:** F-0293 (v0.45.21) změnil `GPSDO_KDAC_DEFAULT` na 3,85e-11; text ve webu zůstal.
- **Důkaz:** `grep -rn "3,4e-11" CM4/LWIP/App/httpd_min.c`; ostatní výskyty 3,4e-11 v `freertos_task_fpga.c` jsou vstupy
  syntetických testů (`gp_ref_core`), ne výchozí hodnota.
- **Dopad:** uživatel webu přepočítává kód DAC o 12 % jinak, než počítá přístroj.
- **Návrh opravy:** text bez čísla (odkaz na K ze smyčky), nebo 3,85e-11; `docs/`/SPA řetězec, logika beze změny.
- **Riziko opravy:** nízké (SPA → `tools/spa/check.py`).
- **Vztah k lekcím:** L-0180 (při změně výchozí hodnoty grep staré hodnoty), L-0191 (i řetězce zobrazené uživateli).
- **Stav:** opraveno 2026-11-17 v `6c3c044`: strmost z `/api/state` `gpsdo.kdac_e13` (K smycky), ne pevne cislo. Neovereno v prohlizeci

### F-0303 [S4] `view <n>` bez kontroly rozsahu a nové příkazy chybí v `help`

- **Místo:** `CM7/Core/Src/freertos_task_uart.c:2297-2311` (`view`), `freertos_task_uart.c:1990` (`help`).
- **Popis:** `view 300` uloží `g_shot_view_req = 300`, UiTask volá `render_view((uint8_t)300)` = okno 44, UART přitom hlásí
  „VIEW 300: vykresleno“; neznámé číslo tiše vykreslí hlavní obrazovku (`render_view` `default:`). V `help` chybí `view`,
  `fpga ab zero|clear`, `fpga pw clear`.
- **Dopad:** jen diagnostika (zavádějící hlášení při překlepu, horší dohledatelnost příkazů).
- **Návrh opravy:** odmítnout `v >= S_VIEW_MAX` (nebo vypsat skutečně vykreslené okno), doplnit `help`.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0181 (parametr za prefixem ověřit, obě větve vyzkoušet).
- **Stav:** opraveno 2026-11-17 v `8b99c39`: `view` nad 255 odmitne, hlasi skutecne vykreslene okno (`g_shot_view_got`); `help` doplnen. Neovereno na HW

### F-0304 [S4] VYNUL. (UDÁLOSTI) a NUL totaliz. (SPOJITÉ) volají `tap_flash` bez překreslení tlačítka

- **Místo:** `CM7/app/app_gpsdo.c` obsluha doteku okna 55 (`EV_RST_RECT`) a 56 (`CT_NUL_RECT`), v0.45.23; `tap_flash_r` `app_gpsdo.c:147-154`.
- **Popis:** `tap_flash` kreslí 2px obrys přímo do framebufferu (rohy přes `prim_internal_blend_px` mimo dirty rect) a podle CLAUDE.md
  (animace, „Flash tlačítek“) se smí volat jen u prvku, který se po akci hned překreslí. Obě tlačítka se po akci nepřekreslují.
- **Důkaz:** obsluha končí `return true` bez `ui_button_render` daného obdélníku; nalezeno při psaní okna AUTOKALIBRACE (F5 tohoto přezkumu).
- **Dopad:** obrys může zůstat v jednom ze tří framebufferů a při jeho střídání problikávat (**HYPOTÉZA** – na displeji neověřeno).
- **Návrh opravy:** `tap_flash` vypustit (odezvou je změna hodnot v okně), nebo tlačítko po akci překreslit.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** pravidlo CLAUDE.md o `tap_flash`; L-0081 (stejný vzor jinde): `tap_flash` bez okamžitého překreslení mají i
  starší `REF_CLR_RECT` (Reference), `MEMB_RUN_RECT` (Benchmark), `ACC_TOGGLE_RECT`/`ACC_NEWPASS_RECT` (Přístup) a `DL_STORE_RECT`
  (Datalog) – mimo rozsah přezkumu, neověřeno, zda je překreslí jiná cesta (kandidáti k ověření, ne nález).
- **Stav:** opraveno 2026-11-17 v `ba50893` (`tap_flash` vypuštěn), lekce L-0302. Neověřeno na displeji

---

## Co bylo zkontrolováno a je v pořádku

- **`9f6115d` (podtržení velkého čísla):** `from--` posune začátek mazání na předchozí segment, záře podtržení se už neusekne
  ani nesčítá. Zbývá okrajový případ podtržení v PRVNÍM segmentu (`from == 0`), kde se 4 px vlevo dál přičítají – nastane jen
  při nejistotě v celé části (F-0177), neřešeno.
- **`e5f48da` (vypnutí kanálu):** P 7 jen bez blokace ochrany a bez požadavku UI; návrat z P 7 přes `fpga_v3_range_select`
  (`cur` se ořízne na `MAXLOG2`, `fpga_v3.c:30`); B = pin A přebíjí na P 0; emulátor a selftest 5b; `fpga_freq_dual_req` volá jen
  UiTask (`s3_dual_req` volatile). Na desce s FW 0x050A správně hlásí „CAPS bit25 ne -> jen P 16“.
- **`5dbc057` (K 3,85e-11):** výchozí K, komentáře selftestu, host test `t_gpsdo_core.py` vyjímá kód ze zdroje (L-0280);
  zbylé 3,4e-11 v CM7 jsou vstupy testů.
- **`07c8085` (readback DAC):** readback v SensorsTasku pod `i2c1MutexHandle`, DacTask zapisuje i aktualizuje `g_dac.code` pod
  týmž mutexem (`freertos_task_sensors.c:517-519`, `462`) → falešný nesoulad nevzniká; na desce `readback 24966` = kód, kontrol 1.
- **`04f8940` (HW rev. 2.1):** PH1 – ODR před režimem výstupu, `HSE_BYPASS` (`main.c:652`) uvolňuje PH1, CM4 ani `gpio.c` GPIOH1
  nekonfigurují; VREFBUF – hodiny `VREFEN` před zápisem CSR, opakování nejvýš 10×, čekání HAL 10 ms v nehlídaném SensorsTasku;
  VBAT chybí – alarm, AUTO-CAL, Diagnostika, okno PRAHY, `status`, web. Na desce `VREFBUF: CSR=0x09 VRR ok`.
- **`4f8686d` (jednorázové děje):** matematika `ab_burst` (k, mez `1 - P`, `k <= N - 1`, chyba interpolace ≈ šum první značky);
  host test + 10 mutací; ukládání zpoždění a nuly pod PRIMASK; řetěz `SENS:FREQ:DUAL` (parser, dotaz, CM7, CM4 zrcadlo,
  `_Static_assert` klíče, směrování v `ipc_service_rings`, bit `IPC_F_DUAL`); SPA (`check.py`, `SPA_HTML` v `nm` = extrakce + 1);
  `MEAS_LIST` 5 × 62 px (≥ 60 px, končí na y 410, patka 417); `S_VIEW_MAX` a `s_focus_of`; `render_view` 55/56; tlačítka nepřekrývají
  `BACK_RECT` (650..783); `fmt_sdec` meze; invarianty `dchg`; fonty `card_freq_draw` (hlava `mono_30` jen číslice, znaménko a tečka,
  nejistý ocas číslice `mono_25`, jednotka `sans_18`). Na desce: interval A→B 602 / 51 zahozených (dříve 531 / 531), `scpi ipc`
  SHODA, korekce šířky pulzu odečtena.
- **`7209368` (`view`):** čekání na UiTask s mezí 2 s, jen v UartTasku; `screenshot all` zná 52–56.

## Nezkontrolováno / omezení tohoto běhu

- **Vzhled oken 55/56 a menu 3 × 5 na displeji** – snímek přes UART půjde až po flashi v0.45.24 (`view`); F-0301 je zatím výpočet.
- **Web přepínač KANÁLY v prohlížeči** – ověřeno jen `scpi ipc` (SHODA).
- **Stopa mimo rozsah:** na desce `FONTY: preskocenych glyfu 7`, poslední chybějící znak U+2014 (dlouhá pomlčka) ve fontu
  s ascentem 14 (`mono_14` nebo `sans_14`), a `FORMAT: omezenych desetin 19`. Nové řetězce v0.45.20–24 jsou ASCII a `fmt_fixed`
  nepoužívají, takže jde o starší kód; doporučeno samostatné hledání (kandidáti: řetězce s „—“ v `app_gpsdo.c`).
- **Znaménko a velikost δ (B − A) při rozbočovači** na dnešní desce – neměřeno (F-0297).

---

## Fáze oprav (F5) 2026-11-17 — na pokyn uživatele („oprav věci, co jsi psal hned“ + F-0297 „počítej nulu, jak jsi navrhoval“)

| nález | commit | ověření |
|---|---|---|
| F-0298 AUTO-CAL VREF | `77e1512` | build 0 varování, `.text` 801040 → 801104 |
| F-0299 ZRUŠIT KAL. | `c587cae` | `.text` → 801128, `s_pw_skew_blk` v ELF |
| F-0300 + F-0301 texty oken | `5f8f09e` | `.text` → 801208, nové řetězce v ELF |
| F-0302 SPA K | `6c3c044` | `tools/spa/check.py` vše OK, `SPA_HTML` v nm 197258 = extrakce + 1, CM4 0 varování |
| F-0303 `view` + `help` | `8b99c39` | `.text` → 801424, nové řetězce v ELF |
| F-0304 `tap_flash` bez překreslení (nalezeno při F5) | `ba50893` | build 0 varování |
| F-0297 nula A–B + v0.45.25 | `6cc9cd3` (tag `v0.45.25`) | host test `t_fpga_v3` PASS, `mut_fpga_v3.py` všechny mutace zachyceny (5 nových), `.text` → 802312, `audit.py` 97/0/2 |

Host test F-0297 (10 MHz na A i B, B = A + δ, šum ±120 ps, 4000 segmentů): nejbližší hrana dává −500,5 / −90,3 / −0,6 / +88,5 /
+501,7 ps pro δ = −500 / −90 / 0 / +90 / +500 ps, kdežto dřívější průměr intervalů +99 496 / +4 509 / +82 / +126 / +502 ps.
⚠️ První verze měla podmínku na `k` (start nejvýš P period před T0) a šum podle ní vyřadil 3 ze 4000 startů; podmínka je teď na
indexu nejbližší hrany (`kn >= -P`), hlídá to mutace „dosah podle k“.
⬜ **Na desce:** flash v0.45.25 (CM7; CM4 kvůli SPA), power-cyklus; rozbočovač na A i B → `fpga ab on`, `fpga ab` (řádek
„nejblizsi hrana B - start … -> nula lze nastavit“), `fpga ab zero`, okno UDÁLOSTI (řádek „korekce“), `view 55` / `view 56` +
`screenshot`, ZRUŠIT KAL. po kalibraci, AUTO-CAL.

**Navazující zadání téhož dne (v0.45.26, `4266fb7`, tag):** okno **AUTOKALIBRACE** (s_view=57, MĚŘENÍ 15. dlaždice) – samokontrola
VREF+/+3V3/+5V/VBAT živě (`autocal_eval`, `g_autocal` beze změny), SPUSTIT = AUTO-CAL + kalibrace TDC obou kanálů
(`fpga_freq_tdc_cal_req`, SET_CONFIG posílá FpgaTask), stav TDC a rezerva řetězu, uložené zpoždění B–A a nula A–B s tlačítky do okna
UDÁLOSTI. Build 0 varování, `.text` 802312 → 804392, `audit.py` 97/0/2. ⬜ neověřeno na HW.
