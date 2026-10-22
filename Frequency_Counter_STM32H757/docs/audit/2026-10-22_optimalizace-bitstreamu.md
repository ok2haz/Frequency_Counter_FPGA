# Optimalizace bitstreamu FPGA – rozbor stavu FW 0x0427 (2026-10-22)

Zdroje čísel: **[P&R]** `impl/pnr/Counter_FPGA.rpt.txt`, `Counter_FPGA_tr_content.html`, `Counter_FPGA.timing_paths`
z buildu 2026-10-21 21:39 (Gowin V1.9.12, GW1NR-LV9QN88PC6/I5; čísla shodná s vydáním `FW_0x0427_20261021-1938`
v `ab_test/MANIFEST.md`) · **[netlist]** `impl/gwsynthesis/Counter_FPGA.vg` přes `sim/res_breakdown.py` a nový
rozbor řídicích sad registrů (skript v předávacím balíčku) · **[ÚVAHA]** odhad, rozhodne P&R.
Nic se tímto dokumentem neměnilo. Navazuje na `2026-10-21_uspora-fpga-a-presun-do-stm.md` (S1–S7).

## 1. Výchozí stav [P&R]

| | hodnota |
|---|---|
| logika | 4 068 / 8 640 = **48 %** (2 687 LUT + 1 381 ALU) |
| registry | 4 013 / 6 693 = **60 %** |
| **CLS** | 3 374 / 4 320 = **79 %** (brána `fpga_release.py`: strop 85 %, varování od 82 %) |
| BSRAM / DSP | 5 / 26, 3 / 10 (ALU54D) |
| setup rezerva `clk_p0_100m` | **+0,215 ns** při nejistotě 0,9 ns (Fmax 102,19 MHz, 2 úrovně logiky) |
| hold rezerva | +0,408 ns |
| ostatní hodiny | `clk_ref_10m` Fmax 39,9 MHz (cíl 10), `spi_sck` 39,9 MHz (cíl 30), ring oscilátor 150 MHz (cíl 50) |
| volby P&R | `-place_option 2 -route_option 1 -route_maxfan 23 -correct_hold 1`, `device.cfg`: `compress = false`, `CRC_check = true` |

## 2. Kde jsou zdroje [netlist]

| blok | FF | LUT | poznámka |
|---|---|---|---|
| vzorkování TDC `q_r` + `qd_r` | **1 075** | – | fyzika (2 × 268 tapů × 2 stupně), nelze |
| zpožďovací řetěz TDC (`g.h.u`) | – | **536 ALU** | fyzika |
| dekodér značky `ld_c`, `pg_c` | – | **735** | fyzika |
| **multiplexer TX rámce `tb` (128:1)** | – | **540** | největší jediná položka logiky mimo TDC (= S4) |
| čas značky `ts_ps`, `ref_ts`, `tr1/tr2`, `tick_hi` | 230 | – | |
| zámky regrese `sy/y0/sx/x0` A+B (`lat_a/lat_b`) | 392 | – | surové součty 0x0426 |
| zámky výsledku okna `dt_a_lat`, `dt_b_hold`, `h_dt_*`, `periods_*` | ~420 | – | dvojí zachycení (S5 zamítnuto – roztržený rámec) |
| okno `win_recip` (`snap`, `r_dt`, `r_periods`, `count_*`) | ~240 | – | |

**Řídicí sady registrů** (dva FF v jedné CLS musí sdílet CLK, CE i SR): **206 sad**, z toho 76 s jediným FF;
116 sad do 3 FF obsahuje celkem 163 FF. To vysvětluje jen malou část rozdílu CLS (≤ ~2 p. b. [ÚVAHA]).

## 3. Proč je CLS 79 %, když logika je 48 %

GW1NR-9 má v CLS 2 LUT; registry jen ve 3 ze 4 CLS buňky CFU (6 480 FF na 4 320 CLS, [P&R]). Dolní mez pro
4 068 logických buněk a 4 010 FF je ~2 050–2 100 CLS; obsazeno 3 374 (≈ +60 %) [ÚVAHA]. Pravděpodobné příčiny
v pořadí podle váhy: (a) řetězy ALU (TDC 2 × 268 + aritmetika ~780) se kladou souvisle a strhávají okolní CLS;
(b) TDC má vynucené rozmístění (každý kanál v jednom řádku CFU, `check_tdc_placement.py`); (c) rozprostření
kvůli časování (`place_option 2`); (d) různorodé řídicí sady (kap. 2). **Kolik z toho je (c), ukáže jen sweep
voleb P&R (O3)** – nic jiného nemění CLS bez zásahu do RTL.

## 4. Časování: kritická cesta je vedení, ne logika [P&R]

| # | z → do | slack | z toho |
|---|---|---|---|
| 1 | `u_tdca/lut_q3[15]` → `ts_ps[34]` | +0,215 | Q 2,79 ns → LUT `n1368` vstup 5,58 (**2,8 ns vedení**) → výstup 6,61 → D 10,82 (**4,2 ns vedení**); logika ~1,5 ns |
| 2, 5, 6, 8–10 | `lut_q3[15]` → další bity `ts_ps` | +0,43 … +1,0 | totéž |
| 3, 11 | `lut_q3[15]` → `u_wra/add_519` (odečet okna) | +0,436 | totéž přes `n1376` |
| 4 | `boot_cnt[2]` → `boot_done` CE | +0,599 | porovnání 24bit čítače do CE |
| 7 | `rt_dly[1]` → `rt_run` RESET | +0,800 | |

Zdroj: `tdc.v:503` – `ts_ps <= {lut_q3[15] ? tr2 : tr1, lut_q3[13:0]}` (DUAL = 0). **Jeden bit `lut_q3[15]`
(část `amt` z kalibrační tabulky) vybírá 34bitové slovo** → fan-out ~34 + odbočka do odečtu okna. 82 % zpoždění je
vedení k rozptýleným cílům. 🔑 **Tohle je nejlevnější páka na rezervu časování** a rezerva je to, co rozhoduje,
jestli se do FPGA vejde další logika (v3, třetí kanál) – CLS je dnes 79 %, ale +0,215 ns je těsné.

## 5. Možnosti optimalizace

| # | co | zisk | cena / riziko | ověření |
|---|---|---|---|---|
| **O1** | **Výběr `tr1/tr2` o takt dřív:** k `lut_q3 <= lut_q2` přidat registr `trsel <= lut_q2[15] ? tr2 : tr1` (34 b), v `ts_ps` použít `trsel`; nebo replikovat `lut_q3[15]` 4–6× (`syn_preserve`), každá kopie budí ~8 bitů | časování: cesta 1–3, 5, 6, 8–11 ztratí dlouhé vedení; [HYPOTÉZA] +1 až +3 ns rezervy | +34 FF (≈ +0,5 p. b.) nebo +5 FF. ⚠️ **`tr1/tr2` jsou posuvný registr, posouvá se KAŽDÝ takt** (`tdc.v:497–499`), takže výběr o takt dřív musí brát o stupeň mladší hodnotu: `trsel <= lut_q2[15] ? tr1 : tick_ps[47:14]` (DUAL = 0, EXT_TR = 0; `tr*_ext` obdobně) – prostý přesun `lut_q3 → lut_q2` by dal značku o takt (10 ns) vedle. Replikace `lut_q3[15]` tuhle past nemá | `tdc`, `tdcinl*`, `n1`, `topregr`, `topselfchk`, `stmcal`; P&R `fpga_release.py` |
| **O2** | `boot_done` / `rt_run`: porovnání čítače registrovat (bit „hotovo“ o takt dřív) | cesty 4 a 7 | ~2 FF, triviální | `topselfchk`, `refguard` |
| **O3** | **Sweep voleb P&R** bez změny RTL: `place_option 0–3 × route_option 0–2` (12 běhů), případně `route_maxfan` | ukáže, kolik CLS a rezervy dávají volby; [ÚVAHA] rezerva ±0,5 ns, CLS ±2 p. b. | jen čas (~30 s na běh, [P&R] 27 s); každý běh projít bránou | skript nad `build.tcl` (Windows), tabulka do tohoto dokumentu |
| **O4** | **TX rámec bez multiplexeru 128:1 + zámky okna do BSRAM** (pole se při uzavření okna zapíšou do stagingové RAM, skladač jen kopíruje do TX RAM) | [ÚVAHA] −540 LUT, −300 až −400 FF ≈ **−8 až −10 p. b. CLS** | velký zásah do `spi_app.v` (časování TX RAM, CDC do PHY – L-0133, L-0141); **je to přesně skladač v3** | `txequiv`, `link`, `topregr`, `topselfchk`, `stmcal` + deska |
| **O5** | S3: odečet `dt = ts − ref_ts` do STM | −2 p. b. (rozbor 2026-10-21) | změna významu 48b pole | `regr*`, `topregr`; STM parser |
| **O6** | Sjednotit CE/SR malých řídicích sad (163 FF ve 116 sadách) | ≤ 2 p. b. [ÚVAHA] | ruční práce po souborech, malý přínos | P&R |
| **O7** | Bitstream: `compress = true` v `device.cfg` | kratší načtení z vnitřní flash [ÚVAHA] | žádná změna logiky; nutný zátěžový test nahrávání (historie „FPGA se nahraje špatně“, L-0141) | `tools/fpga_load_test.py` N×, `status` → `FPGA BUILD` |
| – | zkrátit řetěz TDC, snížit hodiny | – | **ne**: rezerva řetězu B za studena 3 tapy (#297), hodiny 125 MHz už selhaly (L-0152) | – |

## 6. Doporučené pořadí

1. **O3** (sweep voleb) – zdarma, dá měřítko; bez něj nejde říct, kolik z +60 % CLS je rozprostření kvůli časování.
2. **O1 + O2** – malé změny RTL, cílí na jedinou dnešní slabinu časování; spolu s přestavbou bitstreamu pro #328.
3. Rozhodnutí **O4 vs. v3:** O4 je skladač rámce v3 – dělat ho samostatně má smysl jen tehdy, když se v3 odkládá.
   Pokud se jde do v3, O4 je jeho součástí (E2) a rozbor kap. 3/4 platí jako vstup pro zkušební P&R.
4. O5, O6 až podle potřeby; O7 kdykoli, s testem nahrávání.

⚠️ Každá varianta = nový bitstream přes `tools/fpga_release.py --build` (brána: nejistota 0,9 ns, CLS ≤ 85 %, piny,
rozmístění TDC, identita) a `tools/fpga_load_test.py`; RTL změny navíc celá sada simulací (27 testů, Icarus 13 i starší).
