# Studie CLS bitstreamu v3 a optimalizace (FW 0x0505 → 0x0506), 2026-10-23

Měřeno Gowinem V1.9.12 na Windows (GW1NR-9C, 4 320 CLS). Nástroje: `tools/abl_cls.py` (ablace a sweep voleb), `tools/syn_hier.py`
(zdroje po modulech z `*_syn_rsc.xml`), `tools/cs_model.py` (řídicí sady), `sim/res_breakdown.py`, `tools/floorplan_svg.py`,
brána `tools/fpga_release.py --v3`. Adresář `abl/` (kopie zdrojů a výstupy P&R) se neverzuje.

## 1. Výchozí stav (FW 0x0505 z `exchange`)

| | hodnota |
|---|---|
| CLS | **3 961 / 4 320 = 92 %** (brána: 85 %) |
| registry / logika | 5 215 (81 % FF) / 6 335 (4 207 LUT + 2 128 ALU, 74 %) |
| P&R | **selhal: 77 nezapojených sítí** (`PR0004`), bitstream nevznikl |
| v2 (0x0427) pro srovnání | CLS 79 %, registry 4 237, logika 4 556 |

Příčina neúspěchu nebyl jeden blok: v3 přidává proti v2 `u_app` (skladač 1024 B, +320 reg, +530 LUT), PSRAM + hluboký záznam
(`u_raw`, 338 reg), jemný čas 1PPS (`u_ipps` + `u_pps`, 435 reg), předděličky se sondou, součty segmentů a kruh, a z v2 ubylo jen
okno a regrese.

## 2. Rozpad po modulech (syntéza, výchozí stav → po optimalizaci)

| modul | registry | ALU | LUT | poznámka |
|---|---|---|---|---|
| `u_tdca` / `u_tdcb` (řetěz + dekodér) | 833 / 766 | 332 / 332 | 534 / 524 | beze změny proti v2, ~860 CLS (21 %) |
| `u_app` | 724 → 615 | 376 → 360 | 1 349 → 1 247 | z toho hlavičkový multiplexer ≈ 790 LUT |
| `u_sa` / `u_sb` | 535 / 486 → 480 / 431 | 212 → 156 | 104 | součty ST v DSP |
| `u_raw` | 455 (PSRAM) → **26** | 110 → 0 | 661 → 78 | hluboký záznam jen s `-DDEEP_RAW` |
| `u_ring` | 177 → 197 | 26 | 393 → 455 | jednotkový výběr slova |
| `u_pps` | 280 → 213 | 73 | 76 | bez hrubého času a kopie `f_buf` |
| `u_ipps` | 155 → 88 | 67 → 23 | 24 → 90 | čítače jemného času 34 → 8 b |
| `u_pca` + `u_pcb` | 112 → 78 | 32 → 0 | 71 → 57 | společné okno sondy |

## 3. Ablace (celé P&R, bez dalších změn; CLS / reg / logika)

| varianta | CLS | reg | logika | poznámka |
|---|---|---|---|---|
| základ (0x0505) | 3 961 | 5 215 | 6 335 | neprošlo routováním |
| bez PSRAM a hlubokého záznamu | 3 822 | 4 716 | 5 425 | **−139 CLS**, −499 reg |
| bez jemného času 1PPS (`u_ipps`) | 3 923 | 4 940 | 6 147 | −38 CLS |
| bez předděličky a sond | 3 912 | 5 103 | 6 271 | −49 CLS |
| bez DSP součtů | 3 968 | 5 289 | 6 440 | DSP ušetřilo 74 reg, 105 ALU |
| bez `TSPIPE` | 3 928 | 5 117 | 6 335 | −33 CLS, ale časování −8 ns (tam se vrátit nelze) |
| hlavička zjednodušená (jen měření) | 3 233 | 3 757 | 4 196 | horní mez: co by dalo odstranění všech stavových polí |

## 4. Zjištění

1. **CLS není lineární v registrech ani v logice.** Od ~4 450 reg / 5 400 logiky vzhoru se plocha drží na 3 800–3 970 CLS
   (placer s rostoucím tlakem balí hustěji), takže odebrat 100 registrů ušetří jen ~15 CLS. Teprve velký zásah
   (hlavička −850 reg → −600 CLS) CLS opravdu sníží. Model „CLS = součet řídicích sad / k" (`cs_model.py`) se nepotvrdil.
2. **Čím se CLS dá reálně snížit, jsou celé funkce** (PSRAM −139, 1PPS jemný čas −38 až −57, předdělička −49 až −53), ne drobné
   úpravy registrů. Pro 85 % (3 672 CLS) by bylo z 3 834 potřeba dalších ~−160 CLS, tj. vzdát se aspoň dvou z těchto funkcí
   nebo hlavičkového multiplexeru.
3. **Časování je citlivé na každé sestavení.** Stejný zdroj dává rezervu od −1,5 ns do +0,01 ns podle `BLD_TIME` (identita
   bitstreamu je vstup P&R); 79 % zpoždění je vedení. V pěti po sobě jdoucích sestaveních prošlo jedno (viz kap. 6).
   `place_option 0`, `route_option 2`, `timing_driven`, `maxfan`, `replicate_resources`, `retiming` pro CLS nic nepřinesly
   (`maxfan`/`hold`/`rep` dávají totéž).
4. **Dlouhé cesty tvořily málo bloků**: výběr slova zápisu kruhu (fan-out 251), 22b odpočet segmentu, 34b odečet jemného času, CE
   součtů (`hold_a`, fan-out 13), `clear` (fan-out 280), `ts_valid` z `tdc_chan`.

## 5. Provedené úpravy (FW 0x0506; funkčně shodné, protokol beze změny kromě bodů označených ⚠️)

| # | úprava | účinek |
|---|---|---|
| E1 | `raw_cap3l`: RAW_CAPTURE jen v režimu 0; PSRAM a hluboký záznam jen s `-DDEEP_RAW` (CAPS bit21 podle toho) | −499 reg, −139 CLS; ⚠️ výchozí bitstream nemá hluboký záznam |
| E2 | `a_head`/`a_first` a `a_page`/`a_kind`/`a_pno` bez snímků (`rec_head`/`rec_ack`/`page_*` se při skládání nemění; událost kruhu ve `hpend`) | −90 reg |
| E3 | výběr slova zápisu kruhu jednotkovým kódem `ohA`/`ohB` | fan-out 251 → 33, rezerva −3,6 → ok |
| E4 | 22b odpočet segmentu → 2 × 11 b s registrovanými nulami | cesta −5,3 ns zmizela |
| E5 | `pps_cap3`: rozdíl `dd_r` registrovaný; příznaky `seq_nz`/`seq_gt1`; podmínka zveřejnění `fok`/`wok` registrovaná; bez `f_buf`, bez hrubého času (bajty 100..107 = 0 ⚠️, STM bere `PTS >> 14`) | −115 reg |
| E6 | `pps_ides3`: `pcnt`/`t_rel`/`p_ts` 34 → 8 b, plný čas se složí z `tick` při příjmu (3 takty) | −78 reg, −26 ALU |
| E7 | `pin_cnt3`: společné okno sondy, registrovaná saturace | −34 reg, −32 ALU |
| E8 | `seg_chan3`: ST jen 54 b v DSP (ALU54D; mez ST ≤ 1,0·10¹⁶ < 2⁵⁴ z odstupu značek ≥ 5 taktů), `ts_valid` o takt zpožděný (`tvd`), kopie `clear_a`/`clear_b`, `hold_a_s`/`hold_b_s` | −108 reg, −145 logiky |
| E9 | `timing_v3.sdc`: `false_path` do `cp1` (asynchronní vzorkování pinu), z `u_ipps/rdy` | odstraní jediný porušený koncový bod |

## 6. Výsledek

| | 0x0505 | 0x0506 |
|---|---|---|
| CLS | 3 961 (92 %), neroutuje | **3 834–3 862 (89 %)**, routuje |
| registry | 5 215 | 4 449 |
| logika | 6 335 | 5 484 (3 669 LUT + 1 815 ALU) |
| časování (rezerva po nejistotě 0,9 ns) | nelze (neroutuje) | od −1,5 do **+0,012 ns** podle sestavení (1 z 5 prošlo), Fmax 100,1 MHz |
| rozmístění TDC (`check_tdc_placement`) | – | PASS ve všech 5 |
| brána `fpga_release.py --v3` | – | **FAIL jen v CLS (89 % > 85 %)**, ostatní kontroly PASS v pokusu 5 |

Bitstream se proto **nevydává** (brána). Rozhodnutí na uživateli viz kap. 8.

## 7. Nálezy auditu úprav a stávajícího návrhu

- ⚠️ **Minimální šířka pulzu `pin_cnt3` (nález, neopraveno).** P&R hlásí `MPW` záporné (−2,27 ns u `pin_b`, −1,6 ns u `pin_a`,
  tj. šířka pulzu −1,0 / −0,36 ns proti požadovaným 1,25 ns) pro `u_pca/c_*`, `u_pcb/c_*`: čítače přímo taktované vstupním
  pinem nesplní `create_clock pin_a/pin_b -period 2.5` (400 MHz). Brána MPW nehlídá. Skutečný strop předděličky je
  tedy nižší než 400 MHz a není změřen; do měření na desce neslibovat rozsah nad ~200 MHz [HYPOTÉZA].
- **Brána nehlídá ani nepřiřazené vývody PSRAM** (`O_psram_*` bez `pins.cst`; při `-DDEEP_RAW` je třeba je přiřadit).
- Mutace `sim/mut_top_v3.py`: 6 vzorů se po refaktoru rozešlo s kódem (`mut_top_v3.py` je opraveno); kontrola, že vzor existuje
  přesně jednou, patří do `--selftest` (dosud jen při běhu).
- ST 54 b: mez dokázána odstupem značek ≥ 5 taktů a N ≤ 5·10⁵; při porušení (značka každé 4 takty) by ST přetekla, kontrolní
  počítání hran a příznak segmentu to nepoznají [doplnit příznak přetečení, pokud se odstup někdy zkrátí].
- STM: `fpga_v3.c` bere hrubý čas 1PPS z `PTS >> 14` (bajty 100..107 = 0 od FW 0x0506); starší FPGA (0x0500–0x0505) dává totéž.

## 8. Rozhodnutí k CLS 85 %

Funkce a jejich cena v CLS (měřeno): PSRAM −139 (už vypnuto), jemný čas 1PPS −38…−57, předdělička + sonda −49…−53,
hlavičkový multiplexer (celý) ~−600 (nelze), předchozí 1PPS v hlavičce (`pts_p`, 64 reg) ~−40 (vyžaduje STM historii).
Možnosti: (A) zvýšit limit CLS pro v3 na 90 % s podmínkou časování ≥ 0 a PASS rozmístění TDC; (B) vypnout jednu až dvě z funkcí;
(C) přejít na třetí revizi děliče a hlavičky (pole stavů z BSRAM). Doporučení: (A) pro první ověření v3 na desce,
(B) pro vydání, pokud by se časování při 89 % ukázalo v teple nespolehlivé.
