# Využití dalších funkcí GW1NR-9 ve v3 (2026-10-23)

Zadání uživatele (2026-10-23): „optimalizace a využití všech funkcí FPGA“, vybrány všechny čtyři navržené bloky a
„dbej na optimální rozmístění bloků na FPGA“. Navazuje na `2026-10-23_rozlozeni-fpga.md` (mapa, TSPIPE, sweep P&R).
Všechno je ověřené jen simulací (Icarus 13) a host testy. **P&R a deska zbývají (Windows).**

| blok | FW / STM | stav |
|---|---|---|
| E4: 1PPS přes rPLL + IDES8 | 0x0502 / v0.37.0 | ✅ simulace, ⬜ P&R, deska |
| E5: `pin_cnt` + automatický rozsah | 0x0503 / v0.38.0 | ✅ simulace, ⬜ P&R, deska |
| součty segmentů v DSP | 0x0504 / – | ✅ simulace, ⬜ P&R |
| PSRAM pro hluboký záznam | 0x0505 / v0.39.0 | ✅ simulace (model), ⬜ P&R, deska |

## Co projekt z čipu používá (před těmito bloky)

Logika a ALU (řetězy TDC 2 × 268), BSRAM (v3 odhadem ~16/26), OSC (hlídač reference). **Nevyužité:** rPLL 0/2,
IDES/CLKDIV, DSP (ve v3), PSRAM 64 Mbit v pouzdře, User Flash. Dříve vědomě zamítnuto (nevracet se): fáze z rPLL pro TDC
(jitter ~100 ps), IODELAY deskew (PVT ±20 %), vernier TDC, IDES na signálových vstupech, hodiny 125 MHz (L-0152).

## E4: jemný čas 1PPS (FW 0x0502, STM v0.37.0)

**Proč:** hrubý čítač 100 MHz kvantuje 1PPS po 10 ns (σ 2,9 ns), což je víc než šum přijímače po korekci sawtooth.
IDES8 dává 8 vzorků za takt (DDR na 400 MHz), krok 1,25 ns, σ kvantizace 0,36 ns.

**Návrh (`src/v3_core.v` `pps_ides3`, `pps_cap3`):**
- rPLL 100 → 400 MHz (VCO 800 MHz) z GCLK `clk_p0_100m`, CLKDIV /4 → PCLK 100 MHz, IDES8 na pinu 1PPS.
- Čas = `{pcnt, poloha, 11'b0}`: `pcnt` počítá takty PCLK od uvolnění CLKDIV. V resetu CLKDIV PCLK stojí, takže
  asynchronní reset logiky PCLK se uvolní bez metastability. V doméně `clk` se jednou za sekundu přičte `t_rel` (tick
  v taktu uvolnění). PTS tak leží na ose `tick_hi` až na pevný posun (latence uvolnění + fáze PLL, v simulaci `d` = −1
  takt).
- Posun se může změnit **jen při novém zamčení PLL**, proto je v `PPS_STATUS` bity 7:4 epocha (počet uvolnění mod 16).
  STM při změně epochy nebo přechodu jemný ↔ hrubý čas začne diferencování 1PPS znovu.
- `pps_cap3` zveřejní PSEQ, PTS, hrubý čas a `d` **najednou**, až přijde jemný čas (~4 takty po hrubém, čeká nejvýš
  31 taktů; bez něj hrubý × 16384 a PFLAGS bit1 = 0). STM tedy nikdy nevidí nový PSEQ se starým PTS.

**Rozmístění:**
- IDES8 je v IOB pinu 1PPS (33 = IOB23A, spodní okraj), CLKDIV na HCLK téže strany.
- Logika PCLK je pár desítek buněk (`pcnt` 34 b, detekce hrany, `p_ts`); placer ji dá k pinu.
- Do domény `clk` jde jen přepínací bit a 48 b držených ~1 s (`set_false_path` v `timing_v3.sdc`). Žádná dlouhá
  časovaná cesta přes čip tedy nevzniká.
- Kontrola po P&R: `timing_v3.sdc` nově reportuje cesty z `u_ipps/pcnt*`, takže se buňky PCLK objeví v mapě
  `tools/floorplan_svg.py`. Mají ležet u spodního okraje kolem C23.
- ⬜ Ověřit v `.tr`, že Gowin odvodil hodiny rPLL a CLKDIV sám. Kdyby ne, doplnit `create_generated_clock` (návrh je
  v komentáři SDC).

**Hlavička (PROTOKOL_V3 kap. 3.1):**
- bajt 50 `PPS_STATUS`: bit0 PCLK běží, bit1 platné, bit2 poslední hrana bez jemného času při zamčené PLL, bity 7:4 epocha;
- PFLAGS bit1 = jemný čas;
- bajt 79/95 `d` = jemný − hrubý [takty] (int8);
- CAPS bit17.

**STM (v0.37.0):**
- `fpga_v3_pps_fine()`: jemný čas jen s CAPS bit17 + PCLK běží + PFLAGS bit1, jinak hrubý čítač.
- `fpga_meas_t` / `fpga_pps_t` nesou zlomek taktu, příznak a epochu. Parser v2 je výslovně nuluje (`parse_data`
  nenuluje strukturu, L-0204).
- GPSDO počítá interval `dticks + Δzlomek`.
- Emulátor v3 dává jemný čas.
- `fpga v3` vypíše stav 1PPS.

**Ověření [SIM]:**
- 1PPS s periodou 1 ms + 900 ps (poloha hrany v taktu se mění): 12 hran, jemný čas proti skutečné hraně max 600 ps vůči
  první (mez 1,25 ns), pokryto všech 8 poloh IDES8, `d` stále −1, epocha 1.
- S jinou fází PLL (`-DSIM_PLL_PS=4300`) PASS: max 700 ps, `d` −2..−1 (okno jednoho taktu), polohy 7/8. `topv3ovf`, `topv3cal`, `topv3pw` PASS.
- Mutace FPGA 20/20 (nové: obrácené pořadí bitů IDES8, bez `t_rel`, jemný čas se nepoužije), STM host 28/28 (nové: `d` se nečte, jemný i bez PCLK, jemný i bez CAPS, zlomek z horních bitů).
- Překlad CM7 + CM4 0 varování, `audit.py` 97 / 0 / 2 (výchozí stav).

**⬜ Na desce:**
1. **Pořadí bitů IDES8:** model předpokládá Q0 = nejdřívější vzorek. Na desce musí být `d` stálé a rozdíl jemný − hrubý
   × 16384 rovnoměrně v okně jednoho taktu. Při opačném pořadí se poloha zrcadlí a jemný čas skáče až o 8,75 ns
   (mutace „IDES8: obrácené pořadí bitů“). Oprava je parametr `Q0_FIRST`.
2. Gowin může IDES8 na IOB23A s HCLK z rPLL odmítnout. Zkušební P&R (F5 z návrhu v3); záloha je IDDR (2,5 ns) nebo IDES4.
3. `fpga v3`: jemný čas, PCLK běží, epocha se mezi restarty mění jen při novém zamčení PLL.
4. GPSDO: odhad šumu 1PPS (`gpsdo_pm_ns`) by měl klesnout ze ~2,9 ns k šumu přijímače.

## E5: předdělička vstupů a automatický rozsah (FW 0x0503, STM v0.38.0)

**Proč:** TDC značkuje nejvýš ~20 Mznaček/s a přímé počítání hran v doméně 100 MHz končí u ~40 MHz. Nad tím se musí
dělit; uživatel kmitočet nenastavuje, P volí STM ze sondy.

**Návrh (`src/v3_core.v` `pin_cnt3`, `top_v3.v`, `app_v3.v`):**
- 4b čítač taktovaný přímo pinem; výstup P = 1 (pin) až 16 (c[3]) jde do řetězu TDC i do kontrolního počítání.
- Sonda c[3] (÷16) v doméně 100 MHz vždy, okno 1 ms → PROBE u16 (do ~800 MHz; okno segmentu by u16 přeteklo nad 42 MHz).
- `SET_CONFIG 0x07` (10 MHz) → 2 FF → platí od `seg_tick`; po změně 2 segmenty neplatné (hold, FLAGS bit0, první s bit1).
- log2 P segmentu (převzato v `clear`) jde do bajtu CH záznamu 6:4. FLAGS má bity 0–6 obsazené specifikací, takže tam
  místo není; první verze patche to zkusila a test na to upozornil dřív, než se to commitlo.
- Kanál B ze zdroje „pin A“ (jednorázové děje) bere pin A přímo, protože šířka pulzu a zpoždění mají smysl jen při P = 1;
  STM v tom režimu drží P_B = 1.

**Rozmístění:**
- c[3:0] patří k IOB pinu (25 = IOB8A, 27 = IOB11A) a k hlavě řetězu TDC (R26/R27, C2), tedy krátké lokální vedení
  u levého spodního rohu.
- Hodiny pinu nemají jít na globální síť (PRIMARY je ve v3 skoro plná): `timing_v3.sdc` je deklaruje jako `pin_a`/`pin_b`
  (2,5 ns, asynchronní skupiny), report cest z `u_pc?/c*` je ukáže v mapě.
- ⬜ Po P&R zkontrolovat, že Gowin nepovýšil `ch_a`/`ch_b` na GCLK (jinak omezení v `.cst`) a kde leží `u_pca`/`u_pcb`.

**STM (v0.38.0):**
- `fpga_v3_range_select` (z návrhu `v3/fpga_range.c`): nahoru f/P > 33 MHz, dolů f/(P/2) < 27 MHz, strop ÷16.
- Automatika: příkaz až po shodě dvou rámců (sonda se čte živě) a až když předchozí změna platí. `fpga presc a|b auto|0..4`.
- Okna: hrany × P (2pt, LSQ, těžiště pro fázový záznam). Okno se nikdy neskládá přes změnu P, statistika se přeruší.
- Samokontrola: mez přímého měření na dělený kmitočet; pojistka rozsahu ze sondy (sonda / P > 40 MHz → okno se nebere).
- Události BURST v periodách pinu. Emulátor `fpgasim v3` umí předděličku jako RTL.

**Ověření [SIM]:**
- `tb_top_v3` režim 4 (`sim/run.ps1 topv3presc`, B 143,3 MHz, které přímo přes TDC nejde): PROBE A 625 / B 8956 (čekáno
  625,0 / 8956,3), PRESC 3, záznamy B s P = 8 (16) i 16 (12) platné, 3 neplatné kolem přepnutí, A celou dobu P = 1,
  T0 proti pravdě max 51 / 114 ps.
- 🔴 **Nalezeno při tom:** testbench počítal pravdu s nezaokrouhlenou půlperiodou, Icarus ale zaokrouhluje `#(real)` na
  1 fs → drift až 0,7 fs na periodu (při 143 MHz −362 ps v T0). Oprava v testbenchi (L-0205); `topv3` teď T0 A max 102 ps
  (dřív 181 ps).
- `topv3`, `topv3pw` PASS. Mutace FPGA 23/23 (nové: záznam bez P, sonda /8, prohozený kanál 0x07).
- STM host test (11) rozsah, (12) emulátor B 143,3 MHz s P 8/16 (max |e| 9,9e-10), mutace 32/32; CM7+CM4 0 varování, audit 97/0/2.

## Součty segmentů v DSP (FW 0x0504)

**Proč:** ve v3 se DSP nepoužívalo; SX (48 b) a ST (56 b) dvou kanálů zabírají v logice řetězy ALU a ~200 FF.
**Návrh:** `seg_chan3` `SUM_DSP = 1` – součet jedním stupněm s atributem `syn_dspstyle = "dsp"` (GowinSynthesis → ALU54D).
Kdyby syntéza DSP nevzala, zůstane jednostupňový součet v logice (řetěz ~3–4 ns při 10 ns). Varianta `SUM_DSP = 0`
(dosavadní) zůstává, simulace ji vybere `-DSIM_SUM_FAB`.
**Rozmístění:** DSP leží v řádku R19 (mezi BSRAM R10 a řetězy R26/R27). Vstupy jsou registrované rozdíly `dx_r`/`dt_r`
a výstup čte zapisovač kruhu až po DRAIN, takže dlouhé vedení do DSP nikdy nemá logiku. ~~`timing_v3.sdc` reportuje cesty
do `sx_d`/`st_d` → buňky v mapě.~~ Odstraněno auditem (F-0271): registry v ALU54D nemusí existovat → TA2003; polohu DSP
ukáže zpráva o zdrojích / `.rpt`.
**Ověření [SIM]:** všech 5 režimů v3 PASS s DSP, `topv3` se `SIM_SUM_FAB` se shodnými výsledky (T0 A 102 / B 157 ps);
mutace DSP 2/2 (bez vynulování, přičítání o takt dřív) + 7 souvisejících zachyceno.

## PSRAM: hluboký RAW záznam (FW 0x0505, STM v0.39.0)

**Proč:** RAW_CAPTURE měl 512 značek (BSRAM). Vestavěná PSRAM (64 Mbit, 2 čipy x8) dá až 699 050 značek jednoho kanálu
(4 MB, 6 B na značku) – dlouhé souvislé sledy pro jednorázové děje a analýzu jitteru.
**Návrh (`v3_core.v`):**
- `psram_hb3`: HyperBus bez PLL. DDR takt = `clk` 100 MHz, CK 50 MHz z FF na sestupné hraně (hrana uprostřed bajtu,
  5 ns na obě strany), CA 6 bajtů, dávka 128 B (CS# < 4 µs). Čtení se zarovná podle přepínání RWDS (na přesné latenci
  nezáleží), latenci zápisu řadič po startu najde zkouškou W0 .. W0+7 a výsledek hlásí (bajt 129); bez shody DEEP_ST bit1.
- `raw_cap3` režim 1: FIFO 512 značek (BSRAM) → serializátor po bajtech → dávky do PSRAM (~80 MB/s); režim 2: okno
  512 značek z PSRAM do BSRAM, čte se stránkami druhu 3 jako RAW 512 (STM i protokol beze změny formátu).
**Rozmístění:** vývody PSRAM jsou vyhrazené (čip v pouzdře); řadič ~150 buněk, FIFO a okno v BSRAM (+2 bloky). Jediná
cesta k vývodům je registr → pin, CK z FF na sestupné hraně; nic nevede přes čip k TDC.
**STM:** `fpga_freq_v3_raw_deep` (start, čekání na konec, okna po 512, statistika rozdílů navazuje přes okna), společné
čtení stránek `s3_raw_pages` s RAW 512, `fpga raw a|b deep <N> [dump]`, `fpga v3` vypíše PSRAM (připravena, latence,
poslední záznam, přetečení).
**Ověření [SIM]:**
- samostatný test řadiče s modelem: latence modelu 17 / 20 / 23 → nalezena správně, 512/512 bajtů zpět; 26 (mimo
  rozsah) → `bad`;
- `tb_top_v3` režim 5 (`sim/run.ps1 topv3deep`): kalibrace latence zápisu (W0 + 4 = 20 jako model), 1100 značek A, 3 okna přes stránky druhu 3, max |rozdíl − P| 55 ps i přes hranici oken; `topv3`, `topv3presc`, `topv3ovf`, `topv3cal`, `topv3pw`, `phyv3` PASS;
- mutace FPGA 28/28 (nové: čtení bez RWDS, pevná latence zápisu, okno bez posunu adresy);
- STM: host test (13) hlavička PSRAM, mutace 34/34, CM7 + CM4 0 varování, audit 97/0/2.
- 🔴 **Nalezeno při tom:** složení CA mělo 45 místo 48 bitů (výplň 8 místo 11 b) → R/W# bylo vždy 0 a všechno se
  provedlo jako zápis; Verilog doplní chybějící bity nulami bez varování (L-0206). Zachytil to samostatný test dřív, než
  se řadič zapojil.
**⬜ Na desce / P&R (priorita):** jména vývodů a že je Gowin připojí k čipu bez `.cst`; zda CK z FF na sestupné hraně
projde (jinak ODDR); skutečná latence (po auditu 4..35 bajtů, jinak DEEP_ST bit1); časování čtení (po auditu volba ze
dvou fází, `fpga v3` vypíše fázi); propustnost při 10–20 Mznačkách/s.
**Audit téhož dne** (`2026-10-23_v3-funkce-audit.md`): RWDS při CA (S1, F-0262), rozsah a parita latence (F-0263), fáze
čtení (F-0264), výluka režimů + potvrzení T_RAW (F-0266/F-0272), zaseknutí okna (F-0268), mezery (F-0270) – opraveno.
