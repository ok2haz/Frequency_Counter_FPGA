# Audit: bitstream FPGA v3 (FW 0x050A) a princip řízení OCXO (2026-11-14)

- **Zadání (uživatel 2026-11-13):** revize bitstreamu FPGA (chyby časování, optimalizace běhu, využití kapacit),
  přesnost měření a jitter; revize principu řízení OCXO s ohledem na dlouhodobou stabilitu a přesnost měření.
- **Commit:** `b4e8a51` (větev `audit/2026-09-09-hodiny-pwr`). **Fáze auditu: kód se neměnil.**
- **Auditované bitstreamy:**
  - ve flash FPGA: `ab_test/TEST_FW_0x050A_v3_appOscS6_20261031-2155.fs` (varianta APP_OSC, `abl/appOsc@6`;
    STATUS #369: „deska dnes běží appOscS6“, „APP_OSC ve flash zůstává“),
  - řádné vydání: `ab_test/FW_0x050A_v3_20261108-1301_60747e3.fs` (bez APP_OSC, reporty `v3/impl/pnr/`, sestavení
    2026-11-08 14:02).
- **Přečteno:** `src/top_v3.v`, `src/v3_core.v` (bez PSRAM, není v bitstreamu), `src/tdc.v` (řetěz, dekodér, ring
  oscilátor), `src/spi_app.v` (`ref_guard`, `coh_mon`), `src/timing_v3.sdc`, `src/pins.cst`, `v3/build_v3.tcl`,
  reporty P&R a STA obou sestavení, `tools/fpga_release.py` (jen seznam kontrol);
  `CM7/Core/Src/freertos_task_fpga.c:370-1055` (smyčka GPSDO), `freertos_task_sensors.c:423-502` (dither DAC),
  `CM7/Core/Inc/meas_present.h:185-232` (nejistota reference).
- **Nepřečteno:** `src/app_v3.v` a `src/spi_slave_phy_v3.v` jen cíleně (handshake zápisu tabulky TDC); STM strana
  protokolu v3 (`fpga_v3.c`) ne. Nic nebylo měřeno na desce kromě běžícího nočního záznamu (UART, bez zásahu).
- **Co je měření a co výpočet:** čísla z reportů P&R/STA a z kódu jsou doložená; mechanismy označené HYPOTÉZA
  nejsou ověřené a u každé je uvedeno, čím je ověřit.

## Souhrn

Bitstream je v STA čistý (0 porušení setup/hold) a funkčně dobře členěný; měřicí jádro je odolné vůči chybě
časovače segmentu (kmitočet se počítá ze značek, ne z délky segmentu). Slabá místa: (1) STA hlásí porušenou
minimální šířku pulzu na předděličkách a brána vydání ji nečte, (2) řídicí signály s velkým větvením jdou po
dlouhých vodičích (LW), u kterých projekt už jednou doložil optimistickou STA, (3) měřený signál kanálu A je na
globální hodinové síti a kanál B ne, (4) rezerva časování je +0,04 / +0,10 ns. Přesnost u „kulatých“ kmitočtů
omezuje fázově závislá chyba TDC (STATUS #347), ne rozlišení.

Smyčka GPSDO je v principu správná (FLL → PLL 2. řádu, oprava sawtooth, anti-windup, pojistky proti divergenci).
Slabá místa pro dlouhodobou stabilitu a pro poctivost udávané přesnosti: (5) disciplinace nehlídá kvalitu řešení
GNSS, (6) nejistota reference v LOCK nezahrnuje zbytkovou odchylku kmitočtu při dosedání smyčky, (7) holdover
zmrazí okamžitý povel bez extrapolace stárnutí a teploty, (8) odhad stárnutí z kódu DAC zkresluje nelinearita DAC.

## Využití kapacit (reporty P&R)

| zdroj | řádné vydání `60747e3` | flash `appOsc@6` | poznámka |
|---|---|---|---|
| logika (LUT+ALU) | 4500 / 8640 (53 %) | 4409 (52 %) | |
| registry | 3995 / 6771 (59 %) | 3889 (58 %) | |
| **CLS** | **3442 / 4320 (80 %)** | 3379 (79 %) | strop brány 85 % |
| **BSRAM** | **24 / 26 (93 %)** | 24 / 26 | kruh záznamů 16 bloků (`v3_core.v:406-412`) |
| DSP (ALU54D) | 8 / 10 (80 %) | 8 / 10 | |
| globální hodiny PRIMARY | 7 / 8 | 6 / 8 | z toho `ch_a_d` a `u_ro/n[12]` (F-0287, F-0288) |
| dlouhé vodiče LW | 6 / 8 | 6 / 8 | `ch_b_d`, `clear_a/b`, `u_tdca/ld`, `u_tdcb/ld`, `rdy_h` |
| I/O | 13 / 71 | – | |
| Fmax `clk_p0_100m` (s nejistotou 0,9 ns) | 100,42 MHz, setup +0,042 ns | 101,05 MHz, +0,104 ns | |
| hold | +0,453 ns | – | |

Omezující zdroje jsou CLS (rezerva 5 procentních bodů do stropu brány) a BSRAM (2 volné bloky). Kruh záznamů
zabírá 16 bloků SDPB, každý naplněný ze 44 % (256 dvojic × 32 b z 18 kb); je to vědomá výměna BSRAM za ~420 LUT
(komentář `v3_core.v:346-354`). Další kanál TDC nebo hlubší RAW se proto do tohoto čipu bez ústupku nevejde.

Nejhorší cesty (řádné vydání, `Counter_FPGA_v3.tr` tabulka 3.1.1.1): časovač segmentu `u_ring/chi` (+0,042 ns,
zpoždění 8,66 ns u 11bitového odpočtu = převážně vedení), jemný čas 1PPS `u_ipps/bw → f_ts` (+0,166), povolení
indexu hrany kanálu B `hold_b_s → u_sb/fh` (+0,230) a data téhož registru `u_sb/tvd → u_sb/fh` (+0,372).
První dvě nejsou v měřicí cestě kmitočtu; `fh` je (index hrany přiřazený značce).

---

## Část A – bitstream FPGA

### F-0285 [S3] STA hlásí porušenou minimální šířku pulzu na předděličkách vstupů a brána vydání ji nekontroluje

- **Místo:** `src/timing_v3.sdc:29-30` (`create_clock pin_a/pin_b -period 3.33`), `src/v3_core.v:541-542`
  (čítač `c` taktovaný přímo pinem), `tools/fpga_release.py` (kontroly setup, hold, CLS, piny, identita).
- **Popis:** obě sestavení mají v oddílu 3.2 „Minimum Pulse Width Table“ záporné rezervy; brána vydání tento
  oddíl nečte, takže bitstream prošel jako „PASS“.
- **Důkaz:** `v3/impl/pnr/Counter_FPGA_v3.tr` oddíl 3.2 i `abl/appOsc@6/.../Counter_FPGA_v3.tr`:
  `pin_b` Low Pulse Width slack −1,859 ns (skutečná šířka −0,609 ns, požadováno 1,250 ns) na `u_pcb/c_0..3`;
  `pin_a` −1,190 ns (Low) a −0,565 ns (High) na `u_pca/c_*`. Rozklad (oddíl 3.4): sestupná hrana pomalým rohem
  1,665 + 2,314 (IBUF) + 3,128 (vedení) = 7,107 ns, náběžná rychlým rohem 3,330 + 1,392 + 1,776 = 6,498 ns.
  V `tools/fpga_release.py` se řetězce „pulse“/„MPW“ nevyskytují (grep).
- **Dopad:** STA nezaručuje předděličku na cílových 300 MHz. Z modelu nástroje vychází zaručená půlperioda
  ≥ 1,25 + 2,27 = 3,52 ns (B) a ≥ 2,86 ns (A), tedy ~142 MHz (B) a ~175 MHz (A) při střídě 50 %; deklarovaný
  přímý rozsah vstupu je 200 MHz. Model je pesimistický (obě hrany jdou stejnou cestou téhož čipu, nástroj je
  počítá v opačných rozích), skutečná mez bude výš. Chybu předděličky samokontrola nezachytí: kontrolní počítání
  (`v3_core.v:192-210`) i sonda (`v3_core.v:549`) berou týž vydělený signál.
- **Reprodukce:** HYPOTÉZA — ověřit generátorem 100–300 MHz na CH_A a CH_B s P = 16 (`fpga presc a|b 4`):
  najít kmitočet, od kterého naměřená hodnota nesouhlasí s nastavenou (krok 10 MHz, pak 1 MHz kolem zlomu).
  V komentáři SDC (`timing_v3.sdc:24-28`) je totéž jako neprovedený bod.
- **Návrh:** (a) do brány přidat čtení oddílu 3.2 (aspoň WARN s vypsáním sítě a rezervy); (b) po změření zapsat
  skutečný strop do SDC a do specifikace; (c) omezení `pin_a/pin_b` nastavit na změřený strop s rezervou, ne 300 MHz.
- **Riziko opravy:** nízké (nástroj a dokumentace; RTL beze změny).

### F-0286 [S2] Řídicí signály TDC a segmentů s velkým větvením jsou vedeny po LW; STA na nich už jednou vyšla optimisticky

- **Místo:** `src/v3_core.v:1259` (`wire ld = ldok & ~(ec & arm_r)`, výstup LUT → povolení 269 registrů `qd_r`,
  `src/tdc.v:125`), `src/v3_core.v:295-296` (`clear_a`, `clear_b`).
- **Popis:** `u_tdca/ld` a `u_tdcb/ld` (zmrazení vzorků řetězu, jednotaktová cesta) a `clear_a`/`clear_b` umístil
  P&R na dlouhé vodiče. `ld` je kombinační výstup, ne registr.
- **Důkaz:** oba reporty `.rpt.txt` oddíl 6: `u_tdca/ld | LW`, `u_tdcb/ld | LW`, `clear_a | LW`, `clear_b | LW`.
  `.tr` oddíl 3.5: `u_tdca/ld` větvení 269, nejhorší rezerva 3,183 ns (vydání) / 2,465 ns (flash), `clear_b`
  větvení 100, rezerva 2,812 / 2,933 ns, zpoždění sítě až 4,9 ns. Lekce L-0252 (`docs/LESSONS.md:5821-5828`):
  cesta `clear_b` po LW do CE/RESET bloku DSP měla v STA +0,97 ns a na křemíku selhávala; pravidlo „cesta po LW
  do řídicího vstupu může být v STA optimistická o 1–2 ns“. Nejistota hodin 0,9 ns (`timing_v3.sdc:42`) je už
  v uvedených rezervách započtena.
- **Dopad:** při optimismu 1–2 ns zbývá na `ld` skutečná rezerva ~0,5–2 ns, závislá na sestavení a teplotě.
  Když část registrů `qd_r` dostane povolení pozdě, načte vzorek dalšího taktu (hrana už řetězem prošla): vznikne
  teploměr složený ze dvou taktů, tedy chybný kód nebo bublina (`v3_core.v:1311-1312`) a značka mimo o stovky ps
  až ns. Projevilo by se jako řídké odlehlé značky rostoucí s teplotou. U `clear_*` je po opravě L-0252 cílem
  lokální registr `clr_d` (`v3_core.v:237-238`), ostatní cíle (`first`, `x0`, `t0`, `o_bub`) jdou dál po LW.
- **Reprodukce:** HYPOTÉZA — ověřit: (1) počet segmentů s příznakem bubliny (FLAGS bit2, `fpga v3`) a podíl
  zamítnutých značek při zahřátí FPGA o 15–20 °C proti klidu; (2) `tools/dsp_sta.py`-obdobný výpis skutečné cesty
  `ld` z `.tr` (podíl vedení); (3) zkušební sestavení s `ld` rozloženým na kopie (viz návrh) a porovnání šumu
  značky při stojící fázi (postup t382).
- **Návrh:** `ld` vytvořit zvlášť pro každý blok ~32 tapů (kopie LUT4 ze čtyř registrů `ldok`, `dR`, `dRp`,
  `arm_r`, atribut `syn_keep`), aby žádná kopie neměla větvení pro LW; stejně `clear_*` → lokální registry
  u spotřebitelů jako `clr_d`. Cena řádově 16 LUT. Alternativa: zakázat těmto sítím LW omezením v `pins.cst`
  (syntaxe `CLOCK_LOC` podle UG101 — neověřeno).
- **Riziko opravy:** střední (změna rozmístění v okolí řetězu TDC; nutné `pnr_seeds` a `check_tdc_placement.py`).

### F-0287 [S3] Měřený signál kanálu A je na globální hodinové síti, kanál B na LW — proti záměru v RTL

- **Místo:** `src/v3_core.v:529-531` (komentář: „hodiny pinu NE na globální síť … OVĚŘIT po P&R“),
  `src/top_v3.v:256-259`.
- **Popis:** síť `ch_a_d` (vstup CH_A za IBUF) je PRIMARY, `ch_b_d` je LW; kvadranty PRIMARY se liší podle
  sestavení.
- **Důkaz:** `.rpt.txt` oddíl 6: vydání `ch_a_d | PRIMARY | TR TL`, flash `ch_a_d | PRIMARY | BR`; obojí
  `ch_b_d | LW`. Log P&R: `WARN (PR1014) Generic routing resource will be used to clock signal 'ch_a_d'` a totéž
  pro `ch_b_d`. STATUS #347 už eviduje „kanál A u 0x050A horší (globální sítě ch_a_d / RO)“.
- **Dopad:** cesta pin → předdělička (a podle směrování i pin → hlava řetězu) má u A a B jiný typ vedení, tedy
  jiné zpoždění, jinou teplotní závislost a jinou vazbu na sousední globální sítě (100 MHz, OSC, SCK). Rozdíl
  A−B a šum značky se proto mění sestavení od sestavení, což ztěžuje srovnávání bitstreamů (STATUS #347, #369).
  Navíc `ch_a_d` blokuje jednu z osmi sítí PRIMARY.
- **Reprodukce:** doloženo reportem; vliv na šum HYPOTÉZA — ověřit: sestavení s oběma vstupy mimo globální sítě
  a měření t366/t382 (šum značky A proti B).
- **Návrh:** vynutit pro `ch_a_d` i `ch_b_d` stejný, lokální typ vedení (omezení v `pins.cst`, syntaxi ověřit
  v UG101), nebo oddělit síť do řetězu (přímo z IBUF) od sítě hodin předděličky (kopie přes LUT s `syn_keep`).
- **Riziko opravy:** střední (rozmístění u pinů 25/27; ověřit `pnr_seeds`).

### F-0288 [S4] Ring oscilátor nemá definované hodiny a zabírá globální síť PRIMARY

- **Místo:** `src/tdc.v:166-185`, `src/timing_v3.sdc` (chybí `create_clock` pro `u_ro/n[12]`).
- **Důkaz:** log P&R `WARN (TA1132) 'u_ro/n[12]' was determined to be a clock but was not created`; `.tr` 2.2:
  `u_ro/n[12]` perioda 20 ns (výchozí odhad nástroje); `.rpt.txt` oddíl 6: `u_ro/n[12] | PRIMARY | TR` (TL ve
  flash).
- **Dopad:** dělič s LFSR (`tdc.v:185-197`) je časován na odhadnutých 50 MHz; skutečný kmitočet kruhu 13 LUT není
  změřen. Fmax této logiky je 156–178 MHz, rezerva tedy pravděpodobně stačí. Kruh na globální síti vedle sítě
  100 MHz je kandidát na vazbu (strhávání) při sběru histogramu — STATUS #347 má nerovnoměrnost ring oscilátoru
  vůči taktu jako otevřenou hypotézu; tento nález k ní přidává konkrétní místo, ne důkaz.
- **Reprodukce:** HYPOTÉZA — ověřit: změřit kmitočet kruhu (sonda `probe` s dočasným přivedením `ro_sig`, nebo
  počet událostí kalibrace za čas) a porovnat tabulku z ring oscilátoru s tabulkou ze vstupního pinu
  (`tdc cal pin gen`, FW ≥ 0x0509).
- **Návrh:** `create_clock` pro `u_ro/n[12]` se změřenou periodou a rezervou; kruh mimo PRIMARY.
- **Riziko opravy:** nízké.

### F-0289 [S2] Sestavení bez APP_OSC: koherentní hodiny 10 a 100 MHz jsou v SDC asynchronní a fáze domény aplikace není řízená

- **Místo:** `src/timing_v3.sdc:31` (`set_clock_groups -asynchronous` včetně `clk_ref_10m` a `clk_p0_100m`),
  `src/pins.cst:22` (pin 42 = IOB41B, není GCLK), `src/top_v3.v:64` (`clk_app = clk_ref_10m`).
- **Popis:** obě hodiny jsou z téhož OCXO (pevný poměr 1 : 10), ale žádný přechod mezi nimi se nečasuje a 10 MHz
  se na globální síť dostává obecným vedením.
- **Důkaz:** log P&R: `WARN (PR1014) Generic routing resource will be used to clock signal 'clk_ref_10m_d'`;
  `.tr` 3.5: `clk_ref_10m_d` větvení 705, zpoždění sítě 2,210 ns. Komentář `src/spi_app.v:1302`: „poměr fází
  obou hodin je PEVNÝ, takže synchronizátor může sedět trvale na přechodu“. Měření STATUS #347: složka chyby
  značky vázaná na 100 ns (A 81, B 78 ps rms), kterou kalibrace hustotou kódů neopraví; STATUS #369: řádné
  sestavení 0x050A bez APP_OSC má na desce rozbitý interval A→B „během SPI/CS“ u obou kladných seedů, ačkoli STA
  prošla.
- **Dopad:** (a) spínání ~700 registrů a čtení BSRAM v doméně aplikace leží vždy ve stejné poloze vůči vzorkovací
  hraně 100 MHz → rušení se neprůměruje a projeví se jako systematická chyba vázaná na 100 ns; (b) poloha se mění
  se zpožděním sítě 10 MHz, tedy s každým sestavením a s teplotou; (c) synchronizátory mohou trvale pracovat
  v okně metastability. Varianta APP_OSC (ve flash) koherenci ruší, za cenu skutečně asynchronních přechodů
  a ~3× většího šumu jedné značky (STATUS #369), který se ale v segmentu průměruje.
- **Reprodukce:** mechanismus (a) je HYPOTÉZA podložená měřením #347 a #369; ověřit: dvě sestavení bez APP_OSC
  lišící se jen zpožděním `clk_ref_10m` (např. vložený buffer) a porovnat tvar složky 100 ns (test s ujíždějící
  fází).
- **Návrh (k rozhodnutí, větší zásah):** doménu aplikace taktovat `clk_p0_100m` s povolením jednou za ~10 taktů,
  jehož okamžik je pseudonáhodně rozmítán (LFSR, např. 9–11 taktů). Všechny cesty jsou pak časované jedněmi
  hodinami (žádný asynchronní přechod, žádný OSC v měřicí cestě) a aktivita aplikace zůstává vůči signálu
  nekoherentní jako u APP_OSC. Cena: vícetaktové výjimky pro registry s tímto povolením, Fmax domény aplikace je
  dnes 44 MHz.
- **Riziko opravy:** vysoké (přepis taktování `app_v3` a PHY; simulace `topv3*`, `pnr_seeds`, měření na desce).

### Poznámky k časování bez čísla nálezu

- **Rezerva +0,042 ns (vydání) a +0,104 ns (flash)** je pod prahem varování brány (0,2 ns). Nejhorší cesta vydání
  je odpočet segmentu `u_ring/chi_0 → chi_10` se zpožděním 8,66 ns u 11bitového čítače: logika je krátká, zbytek
  je vedení. Časovač segmentu nemá vliv na kmitočet (značky nesou vlastní čas), ale stojí rezervu celého návrhu.
  Návrh: čítač segmentu do volného bloku DSP (jako `tick_hi`, `top_v3.v:80`) nebo jako posuvný registr s LFSR.
- **Doména PCLK jemného času 1PPS** je časovaná (`u_ipps/u_div/CLKOUT.default_gen_clk`, Fmax 114 MHz, rezerva
  ≥ 1,2 ns); varování TA1132 znamená jen, že hodiny odvodil nástroj. Bod „OVĚŘIT v .tr“ v `timing_v3.sdc:110`
  je tímto splněn.
- **Přechody 10 MHz → 100 MHz s daty** (zápis tabulky TDC, kalibrace, RAW) mají přepínací bit zpožděný o tři takty
  aplikace za daty (`app_v3.v:307-312`), handshake je v pořádku. Vícebitové hodnoty `pr_a_req`, `bsrc` jdou přes
  2 FF bez handshake (`top_v3.v:231`, `:241`); roztržená hodnota by platila jeden segment, který je po změně
  stejně neplatný (`top_v3.v:343-349`).

### Přesnost měření a jitter – stav podle měření v projektu

| veličina | hodnota | zdroj |
|---|---|---|
| krok řetězu TDC | ~37 ps (268 tapů na ~10,45 ns) | `tdc.v:27`, CLAUDE.md |
| šum jedné značky, stojící fáze | 30–48 ps bez APP_OSC, 73–107 ps s APP_OSC | STATUS #369 (t384) |
| σ(A−B), oba kanály z téhož nesoudělného zdroje | 4,4 ps | STATUS #347 |
| chyba závislá na fázi v taktu 10 ns | A 57, B 91 ps rms (p-p 230 ps v rozdílu) | STATUS #347 |
| chyba vázaná na 100 ns | A 81, B 78 ps rms | STATUS #347 |
| přeslech mezi kanály podle odstupu A–B | σ intervalu 98–186 ps | STATUS #369 (t381) |
| jemný čas 1PPS | krok 1,25 ns (σ 0,36 ns) | `v3_core.v:559` |

Rozlišení není omezující; omezující je systematická chyba závislá na poloze hrany. U signálu soudělného se
100 MHz (10 MHz, 5 MHz, 1 MHz, 1PPS) padají všechny hrany segmentu do téhož kódu, chyba se neprůměruje a při
pomalém posuvu fáze se převádí na chybu kmitočtu úměrnou strmosti chybové křivky. Postup schválený ve STATUS #347
(kalibrace ze vstupního pinu, pak korekce v STM) tento audit nemění.

**Návrh k posouzení (není nález, možná už zvažováno):** hodiny TDC zvolit nesoudělné s kulatými kmitočty
(Si5356 CLK0/1 např. o několik set ppm vedle 100 MHz). Hrany 10 MHz pak projdou celou periodu taktu mnohokrát
za segment a chyba závislá na kódu se zprůměruje v každém segmentu. Cena: kmitočet hodin v rámci je dnes celé
číslo MHz (abs 99), zlomkový MultiSynth má vyšší jitter (měřeno t382), hlídač koherence a APP_OSC by ztratily
smysl. Před rozhodnutím změřit šum značky při zlomkovém nastavení CLK0.

---

## Část B – princip řízení OCXO

### Hodnocení principu

Řetěz je: 1PPS → IDES8 ve FPGA (1,25 ns) → rozdíl hran v taktech OCXO → oprava sawtooth z UBX-TIM-TP →
FLL (okna 16–256 s, měření K) → PLL 2. řádu (ζ 0,7, τ 1000 s, předfiltr τ/30) → kód DAC v Q16.16 → dither 100 Hz
→ AD5693R → RC → EFC. To je pro OCXO s 1PPS z navigačního přijímače přiměřený návrh a kód mu odpovídá
(`freertos_task_fpga.c:472-549`). Tři vlastnosti určují dosažitelnou stabilitu:

1. **Přenos šumu GPS do kmitočtu.** Proporcionální zisk kp = 2ζ/τ = 1,4·10⁻³ s⁻¹ (`:539`). Putování fáze 1PPS
   o 5–10 ns na stovkách sekund dá změny kmitočtu OCXO 0,7–1,4·10⁻¹¹. To souhlasí se změřenou ADEV fáze
   1,1·10⁻¹¹ @ 1000 s (STATUS #324). Při τ = 1000 s je tedy stabilita na 100–3000 s dána přijímačem, ne OCXO.
2. **Fázová chyba při driftu.** Smyčka 2. řádu sleduje lineární drift D s ustálenou chybou fáze D·τ². Pro
   změřené stárnutí 1,7·10⁻¹⁰/den (D = 2·10⁻¹⁵ s⁻¹) je to 2 ns při τ = 1000 s a 50 ns při τ = 5000 s; po zapnutí
   (5 ppb/den) 58 ns a 1,4 µs. Prodloužit τ kvůli bodu 1 proto bez stavu driftu nejde (STATUS #313 bod 4 —
   tento audit potvrzuje výpočtem).
3. **Rozlišení akčního členu.** 1 LSB = 3,4·10⁻¹¹; zlomek dodává dither. Smyčka na K nezávisí kriticky
   (chyba K mění ζ a τ odmocninou).

Doporučený směr (k rozhodnutí, vyžaduje simulaci a měření): třetí stav smyčky = drift (Kalmanův filtr fáze /
kmitočet / drift, nebo PI s druhým integrátorem) a τ volené podle změřené stability OCXO (otevřený #312). Tím se
τ smí prodloužit na tisíce sekund bez fázové chyby a holdover dostane odhad driftu zdarma.

### F-0290 [S3] Disciplinace nehlídá kvalitu řešení GNSS: stačí 2D fix

- **Místo:** `CM7/Core/Src/freertos_task_fpga.c:888` (`int fix = sim || (g.valid && g.fix_mode >= 2);`),
  `:393` a `:528` (odlehlá hrana až od 3·10⁻⁷).
- **Popis:** smyčka přijímá 1PPS při jakémkoli platném fixu včetně 2D, bez meze na počet družic, DOP nebo stav
  časového řešení.
- **Důkaz:** uvedené řádky; `gps.c` neparsuje pole `flags` zprávy TIM-TP (grep `raim|timeBase|qErrInvalid` bez
  shody v parseru). Přijímač NEO-7M nemá režim pevné polohy (STATUS: `gps_survey_in_cmd` „NEO-7M nejspíš NAKne“).
- **Dopad:** při zhoršené geometrii (zastínění, 3–4 družice) má čas přijímače chybu stovek ns, která se mění
  pomalu; mez odlehlých hran chytí jen skoky nad 300 ns za sekundu. Putování 300 ns projde do povelu jako
  kp·Δx ≈ 4·10⁻¹⁰ a stav zůstane LOCK. Pro přístroj, který svou referencí měří, je to chyba měření bez varování.
- **Reprodukce:** HYPOTÉZA — ověřit z datalogu (pole `sats`, `hdop10`, `flags` se ukládají): korelace |fáze|
  a |y| smyčky s HDOP a počtem družic; případně pokus se zastíněnou anténou.
- **Návrh:** podmínka disciplinace: 3D fix, počet družic ≥ 5, HDOP pod mezí (např. 3), s hysterezí; jinak HOLD
  s důvodem. Dlouhodobě časový přijímač s pevnou polohou (uvedeno už v zadání desky jako možnost NEO-M8).
- **Riziko opravy:** nízké až střední (riziko zbytečných přechodů do HOLD při příliš přísné mezi — meze odvodit
  z datalogu).

### F-0291 [S3] Nejistota reference ve stavu LOCK nezahrnuje zbytkovou odchylku kmitočtu při dosedání smyčky

- **Místo:** `CM7/Core/Src/freertos_task_fpga.c:546` (`locked` po 60 s při |yf| < 1·10⁻⁹ a |xf| < 200 ns),
  `:517-520` (přechod FLL → LOCK při |y| < 5·10⁻¹⁰), `CM7/Core/Inc/meas_present.h:185` (`MP_REF_PPB_LOCK 0.01`)
  a větev `GPSDO_LOCK` v `mp_ref_ppb()`; `freertos_task_fpga.c:604-618` (`gp_ref_core`, ADEV z druhých diferencí).
- **Popis:** jakmile je `locked` = 1, bere se nejistota reference z ADEV fáze (nebo 0,01 ppb, dokud historie
  nestačí). ADEV z druhých diferencí fáze konstantní odchylku kmitočtu nevidí.
- **Důkaz:** smyčka vstupuje do LOCK s odchylkou až 5·10⁻¹⁰ (`:517`), dosednutí smyčky s τ = 1000 s trvá
  několik τ; `locked` se nastaví po 60 s (`:546`); `gp_ref_core` počítá jen `x[i+2m] − 2x[i+m] + x[i]` (`:613`),
  lineární průběh fáze dá nulu; do historie ≥ 300 s platí konstanta 0,01 ppb (`meas_present.h:185`).
- **Dopad:** v první hodině po zamčení (studený start, návrat z holdoveru přes FLL) může být skutečná odchylka
  reference až 0,5 ppb, zatímco hlavní číslo, okno ANALÝZA i web (`gpsdo_ref_ppb`, `:1005-1016`) uvádějí
  0,01–0,1 ppb. Počet platných číslic je pak nadhodnocen až o 1–2 řády. V ustáleném stavu je údaj správný.
- **Reprodukce:** ověřit na desce: po `gpsdo off` → rozladit kód o ~15 LSB → `gpsdo on`; po zamčení porovnat
  `ref_ppb` s odchylkou proti nezávislému zdroji nebo se sklonem fáze v historii smyčky.
- **Návrh:** k ADEV přičíst kvadraticky odhad střední odchylky kmitočtu z téže historie (sklon fáze za poslední
  ~τ/3) a `locked` pro účely nejistoty uznat až po uplynutí ~3 τ od vstupu do LOCK; do té doby
  `MP_REF_PPB_LOCKING` nebo |sklon|.
- **Riziko opravy:** nízké (prezentace nejistoty; smyčku nemění). Zvážit dopad na ukládání kódu do flash
  (`freertos_task_sensors.c:452` používá stejný příznak).

### F-0292 [S3] Holdover zmrazí okamžitý povel; chybí extrapolace stárnutí a teploty a model nejistoty nemá počáteční ofset

- **Místo:** `CM7/Core/Src/freertos_task_fpga.c:966-969` (přechod do HOLD), `:975-976` (povel = `s_gc.code`),
  `:541` (`code = code_lock − (kp·xf + integ)/K`), `meas_present.h` větev `GPSDO_HOLD`.
- **Popis:** v HOLD zůstane poslední hodnota povelu včetně proporcionální složky, která sleduje okamžitou fázi
  GPS; drift se neextrapoluje.
- **Důkaz:** uvedené řádky. Rozptyl proporcionální složky: kp·σ(xf) = 1,4·10⁻³ × 5–10 ns = 0,7–1,4·10⁻¹¹
  (25–50 ns/h). Naměřené holdovery: −100 ns/h (holdover1110), −120 ns/h (noc1111), y −0,03 až −0,08 ppb
  (STATUS #379); dva 10h běhy s různým tvarem i znaménkem (STATUS #303). Model nejistoty: √(u0² + (drift·t)²)
  s u0 = ADEV v LOCK (~0,011 ppb) a driftem z kódu DAC (~0,007 ppb/h) dá po 1 h 0,013 ppb, změřeno 0,03–0,08 ppb.
- **Dopad:** počáteční chyba kmitočtu holdoveru je náhodná v řádu 10⁻¹¹ a nejistota udávaná v HOLD je proti
  měření 2–6× nižší. Podíl zmrazeného povelu a retrace na naměřených číslech z dat rozlišit nejde.
- **Reprodukce:** HYPOTÉZA — ověřit bez zásahu: z historie smyčky (kód po 10 s) u zaznamenaných holdoverů
  porovnat kód v okamžiku HOLD s průměrem za poslední 1–3 τ; rozdíl × K je počáteční ofset z tohoto mechanismu.
- **Návrh:** při vstupu do HOLD nastavit povel na přímku proloženou kódem za posledních N·τ, extrapolovanou
  na aktuální čas (průměruje šum GPS a nese sklon stárnutí), a v HOLD ji dál extrapolovat; po změření tempco
  (#311) přidat teplotní člen. Do modelu nejistoty přidat změřený počáteční ofset (dnes ~0,05 ppb).
- **Riziko opravy:** střední (chování v holdoveru; simulace v `gpsdo_selftest` a noční běh).

### F-0293 [S3] Odhad stárnutí z kódu DAC zkresluje nelinearita DAC; platí i pro drift v modelu holdoveru

- **Místo:** `CM7/Core/Src/freertos_task_fpga.c:619-632` (`gp_ref_core`: sklon kódu × K = drift),
  metoda v `docs/MERENI_A_KALIBRACE.md` „aging1112“ a STATUS #303.
- **Popis:** převod kód → kmitočet se bere jako lineární se stálým K = 3,4·10⁻¹¹/LSB. Skutečná velikost
  jednotlivých kroků DAC se liší (diferenciální nelinearita), interpolace ditherem je lineární jen uvnitř kroku.
- **Důkaz:** AD5693R je 16bitový převodník se zaručenou monotonií, tedy DNL do ±1 LSB, INL v jednotkách LSB
  (hodnoty z datasheetu AD5693R — v repu není, ověřit); aging1112 stojí na změně kódu o ~3,4 LSB za 21,7 h,
  noční záznam 2026-11-13/14 má zatím změnu ~2,2 LSB.
- **Dopad:** při změně kódu o 2–4 LSB může být chyba odhadu D desítky procent jen z DNL, nezávisle na
  kvantizaci záznamu. Chyba klesá s rozsahem kódu (při ≥ 35 LSB řádově INL/rozsah ≈ jednotky procent). Je to
  další důvod pro běh ≥ 7 dní (#303) vedle kvantizace datalogu; záznam kódu na 0,01 LSB z UART tento zdroj chyby
  neodstraní.
- **Reprodukce:** HYPOTÉZA — ověřit: změřit K lokálně v okolí pracovního kódu (kroky ±1, ±2, ±4, ±8 LSB při
  `gpsdo off`, odezva kmitočtu proti GPS po ustálení, každý krok ≥ 30 min) a porovnat s 3,4·10⁻¹¹.
- **Návrh:** u výsledků stárnutí uvádět rozsah kódu a nejistotu z DNL; pro krátké běhy vztahovat drift
  k napětí Vc změřenému ADC jen jako kontrolu (rozlišení ADS1115 je ~4 LSB DAC, samo nestačí).
- **Riziko opravy:** nulové (metodika měření).

---

## Stav po triáži (2026-11-14)

Uživatel schválil navržené pořadí. Provedeno téhož dne:

| Nález | Stav | Commit | Ověření |
|---|---|---|---|
| F-0291 | opraveno v kódu (v0.45.15) | `7405947` | host test `tests/host/t_gpsdo_ref.py`, model dosedání nad změřeným šumem (podhodnocení 46× → nejvýš 1,2×), L-0276; ⬜ neověřeno na HW |
| F-0290 | opraveno v kódu (v0.45.15) | `098bdf8` | host test + 16 mutací, meze z datalogu 21 h LOCK, L-0277; ⬜ neověřeno na HW |
| F-0285 | brána vydání opravena | `64bb7d9` | selftest brány, reporty v2 i v3 (A 175 MHz, B 142 MHz jako WARN), L-0278; ⬜ skutečný strop předděličky neměřen |
| F-0286 | výchozí měření, RTL beze změny | – | běh 36,3 h (flash appOscS6, modul 35 °C): bubliny okna P 0 / S 0 z 523 431 oken → dopad se při pokojové teplotě neprojevuje; ⬜ test v teple |
| F-0292 | opraven model nejistoty (v0.45.16); povel v holdoveru beze změny | `5ca59de` | dvě noci LOCK po 9,8 h (`docs/audit/sim/2026-11-14_f0292_*.py`), host test + 26 mutací, L-0279; ⬜ neověřeno holdoverem na desce |
| F-0287, F-0288, F-0289, F-0293 | otevřeno | – | – |

Odchylky od návrhu v nálezech:

- F-0291: sklon fáze se nepřičítá celý, jen jeho přebytek nad 2 × ADEV. Celý kvadratický součet by údaj v ustáleném stavu
  zvedal trvale (medián 1,15×, 95. percentil 1,9×), protože sklon pod touto mezí je tentýž šum, který ADEV už obsahuje.
  Pravidlo „`locked` až po 3 τ“ se nepoužilo; nahrazuje ho měřený sklon a přenos nejistoty přes změnu stavu.
- F-0286: návrh rozložit `ld` se neprovedl, protože výchozí měření dopad nepotvrdilo. Zůstává test v teple.
- F-0292: návrh zmrazit v holdoveru přímku proloženou kódem se neprovedl. Nad dvěma nočními záznamy je okamžitý kód
  nejlepší předpověď potřebného kódu na další 1–3 h; průměr i přímka přes poslední 1–2 h vycházejí hůř. Opraven je jen
  model nejistoty (člen náhodné procházky kódu). Část nálezu o proporcionální složce (0,7–1,4·10⁻¹¹) platí, ale je
  menší než putování potřebného kódu (2,7–3,8·10⁻¹¹ za hodinu).

Na desce (2026-11-14 09:13): v0.45.17 v obou bankách, po resetu SELFTEST 16/16, `gpsdo test` PASS, brána GNSS OK.
Verze v0.45.16 měla špatnou očekávanou mez v `mp_selftest` (SELFTEST 15/16), opraveno v `1e1fad2` (L-0280).

## Co nebylo ověřeno

- Nic z části A nebylo měřeno na desce v rámci tohoto auditu; deska běží v nerušeném LOCK (noční záznam).
- Směrování sítí (`sig_eff`, `ld`) je posouzeno z tabulek reportu, ne z mapy rozmístění.
- Hodnoty DNL/INL AD5693R, meze IDES8/rPLL pro GW1NR-9 C6/I5 a syntaxe `CLOCK_LOC` nejsou ověřeny v datasheetech.
- STM strana výpočtu kmitočtu ze segmentů (`fpga_v3.c`) nebyla v rozsahu.

## Doporučené pořadí (k triáži uživatelem)

1. F-0291 (nejistota reference při dosedání) a F-0290 (kvalita GNSS) — malé změny v STM, přímý dopad na
   poctivost udávané přesnosti.
2. F-0285 — doplnit bránu a změřit strop předděličky generátorem.
3. F-0286 — zkušební sestavení s rozloženým `ld`, měření šumu značky a bublin v teple.
4. F-0292 — holdover s extrapolací (po vyhodnocení nočního běhu a tempco).
5. F-0287, F-0288, F-0289 a návrh nesoudělných hodin — větší zásahy do FPGA, rozhodnout podle výsledku 3.
