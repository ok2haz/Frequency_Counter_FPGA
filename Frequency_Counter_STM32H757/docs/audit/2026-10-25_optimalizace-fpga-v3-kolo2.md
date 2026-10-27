# Optimalizace FPGA v3 (FW 0x0507), kolo 2: plocha CLS a zbylé cesty časování

Zdroj: externí instance, balíček `exchange/ukol_2026-10-25_fpga_opt2_vystup.zip` (zadání `ukol_2026-10-25_fpga_opt2.zip`).
Její zpráva a rozbor kanálu B jsou v `docs/audit/ext/` beze změny (`2026-10-25_opt2_ZPRAVA.md`, `2026-10-25_opt2_ROZBOR_KANAL_B.md`,
`2026-10-25_opt2_POZOR_ROZBOR.txt`), nepoužité patche třídy B a kanál B v `docs/audit/ext/opt2_patche_B_K/`.
Tento dokument je MOJE ověření a rozhodnutí; čísla externí instance jsou označená jako její.

## 1. Výsledek

| | 0x0507 vydané (`7fadfec`) | řada 0001–0009 + oprava bit4 (`28c71c9`) |
|---|---|---|
| CLS | 3 672 (85 %) | **3 313–3 382** (77–78 %), vydaný bitstream 78 % |
| LUT / ALU / FF | 3 301 / 1 595 / 4 286 | 2 887 / 1 403 / 3 790 |
| BSRAM | 16 / 26 | **24 / 26** (výchozí sestavení) |
| ALU54D | 6 / 10 | **8 / 10** |
| sestavení s kladnou rezervou (8 sestavení, `pnr_seeds`) | 6 z 8, medián +0,07 ns | **7 z 8, medián +0,099 ns** |
| vydaný bitstream: setup / hold / Fmax | +0,012 / +0,557 ns / 100,12 MHz | **+0,119 / +0,415 ns / 104,42 MHz** |

Vydáno: `ab_test/FW_0x0507_v3_20261025-1613_28c71c9.fs` (md5 `caac6920338eaeb2999a66c8df9d51d6`), brána `fpga_release.py --v3 --build --tries 8`
prošla v prvním sestavení. `FW_VERSION` zůstává 0x0507 (protokol beze změny), identita = čas + git v `status` → `FPGA BUILD`.
⬜ **NEOVĚŘENO NA DESCE.**

## 2. Co se vzalo

Commity v repu (každý krok samostatně, logika bez komentářových změn):

| commit | obsah | typ |
|---|---|---|
| `859bd40` | oprava falešného FLAGS bit4 (kontrolní počítání hran při `snap` v taktu přenosu čítače X), test `ccdcarry` | fix |
| `5465deb` | 0001 kruh záznamů v 16 SDPB bez výběru slova | plocha |
| `bda8b6f` | 0002 součty SX/ST v ALU54D přímo z `xm − x0`, `tm − t0` | plocha |
| `4635960` | 0003 `tdc_chan3` (v3 kopie `tdc_chan`), `ts_hq` bez výběru `tr1/tr2` | plocha |
| `cdf8aac` | 0004 počet značek N v DSP | plocha |
| `06c6213` | 0005 neúčinná hradla `pfl_l`, `q >= 84` | plocha |
| `086f3c2` | 0006 příznaky saturace `d_l` registrované s `dd_r` | časování |
| `46d9017` | 0007 `o_bub/o_ovl` z registrovaného `take` | časování |
| `b03947c`, `28c71c9` | 0008 `tb_prim_chk`, 0009 nástroje `yosys_abc4.py`, `run_doplnek.py` | test |
| `827f34d` | záznam vydání v `ab_test/MANIFEST.md` | chore |

Patche byly aplikovány na naši větev přes `git apply --3way` (jediný konflikt: `sim/run.ps1`, můj řádek `ccdcarry` vedle jejich komentáře; vyřešeno ponecháním obojího).
Autorství commitů je naše; externí instance je uvedena v těle commitu.

## 3. Co jsem ověřil sám (a čím se liší od tvrzení externí instance)

| kontrola | výsledek |
|---|---|
| Primitiva SDPB (zápis 32 b / čtení 8 b) a ALU54D proti **oficiálnímu** `prim_sim.v` z Gowin 1.9.12 (`tb_prim_chk`) | PASS jak s modelem autora, tak s oficiálním; negativní kontrola (obrácené pořadí bajtů, opačné znaménko B) test shodí (2 042 a 1 980 chyb) |
| `tdc_chan3` proti `tdc_chan` z `tdc.v` | liší se jen jménem a blokem `ts_hq` (`diff` modulů), test `tdc3eq` PASS |
| Stopy `trace_v3.py` řady proti **základu na tomtéž stroji** (8 testů `topv3*`) | **8 z 8 bit po bitu shodné**; totéž platí pro konečný stav `28c71c9` (řada + oprava bit4) proti řadě, tedy 8 z 8 i proti základu; autorovy otisky u mě neplatí (jiný Icarus: 12 devel proti 13), proto jsem pustil vlastní běh základu |
| Ostatní testy: `holdinv`, `ppsv3`, `phyv3`, `segdsp`, `tdc3eq`, `primchk` | PASS |
| Oprava bit4: `tb_ccd_carry` | bez opravy FAIL (cdd = 256), s opravou PASS |
| Rozmístění TDC řetězců (`check_tdc_placement.py`) | PASS ve všech 8 sestaveních |
| 8 sestavení P&R řady bez opravy: CLS 3 307–3 361, setup +0,004 až +0,18 ns, 0 porušených cest | 8 z 8 |
| 8 sestavení řada + oprava (stav `28c71c9`): CLS 3 313–3 382 | 7 z 8, `@4` −0,283 ns na cestě `tvr_a → u_raw/run` (náhodná cesta, 1 z 8, ne systematická podle L-0233) |

**Nespuštěno:** mutační sady `mut_top_v3.py` (autor 13,7 h CPU na rychlejším stroji, odhad u nás ~8 h) a `mut_pps_v3.py`. Výsledky mutací v této zprávě
(59 zachyceno + 2 ekvivalentní z 61; 23 + 3 z 26) jsou tvrzení externí instance. Dále nespuštěno: P&R s `-DDEEP_RAW` (autor uvádí BSRAM 26/26 bez rezervy).

## 4. Falešný FLAGS bit4 (potvrzeno, opraveno)

`seg_chan3` čte `xcnt[9:0]` ve `snap`, ale čítač X má registrovaný přenos: v taktu `xcy = 1` je `xlo` už 0 a `xhi` ještě starý, takže `xcnt` je o 256 menší.
Takový `snap` dá `cdd = ±256`, tedy falešný `ccd_bad`, a to ve dvou po sobě jdoucích záznamech (`xprev` se posune). STM okno zamítne
(`FPGA_CHK_COUNT`, `status` → `FPGA SAMOKONTROLA: zamitnuto oken: citani`).

**Základní hodnota na desce před opravou (v0.40.6, 2026-10-25 17:00): 57 zamítnutých oken „citani" za 26 960 s = 7,6/h.**
Po nahrání nového bitstreamu má tento čítač růst výrazně pomaleji (zbylé přírůstky by byly skutečně chybějící hrany). Pozor: nepředpokládám, že to vysvětluje
+24 ppm na kanálu B, falešný flag zahodí okno, nezmění kmitočet.

## 5. Co se nevzalo a proč

| položka | důvod |
|---|---|
| B1 / B1p (dlouhé časovače na 1ms základně) | mění doby náběhu o ≤ 1 ms; cíl CLS je splněn i bez nich, takže změna chování není potřeba |
| B2 (čítače RX_CRC_ERR / RX_LEN_ERR na 8 b) | užitek až se změnou STM (`fpga_v3.c`) |
| B3 (`create_clock` pin_a/pin_b 2,5 → 3,333 ns) | změna specifikace (strop předděličky 300 MHz), ne optimalizace; k rozhodnutí uživatele |
| K1 (pin A do výběru zdroje B jen v režimu B) | logicky shodné, jen pokus k kanálu B; bez měření nemá smysl |
| K2 (`CLOCK_LOC … LOCAL_CLOCK` pro piny kanálů v `pins.cst`) | mění zamčený `pins.cst`; pokus k vadnému kanálu B, vyžaduje měření na desce |

## 6. Rizika a otevřené body

1. **Křemík:** chování SDPB 32/8 b a ALU54D je ověřeno jen simulačními modely (oficiálním i autorovým), ne na desce. Selže-li mapování bajtů SDPB, rámce ponesou přeházené bajty; `fpga v3 test` a prvních pár rámců to ukáže ihned.
2. **Kopie `tdc_chan3` v `v3_core.v`:** budoucí změna `tdc_chan` v `tdc.v` se musí promítnout i do kopie (rozdíl je jediný blok `ts_hq`); hlídá ji test `tdc3eq`, který musí zůstat v sadě.
3. **BSRAM 24 z 26** (výchozí), s `-DDEEP_RAW` údajně 26/26: bez rezervy, neověřeno P&R.
4. **Rezerva časování** vydaného bitstreamu je +0,119 ns (WARN pod 0,2 ns), lepší než předchozí +0,012 ns, ale stále malá; vždy hodnotit 8 sestaveními.

## 7. Ověření na desce (po flashi a power-cyklu)

1. `status`: `FPGA BUILD` odpovídá `28c71c9`, hlavička `UZKA`, `SEQ FPGA: diry` bez přírůstku, CRC err beze změny oproti 5,7/h.
2. `fpga v3 test` PASS, `selftest` 16/16.
3. Kanál A (externí GPSDO) a kanál B: porovnat σy@1s a kmitočet s referencí (hodinový běh); žádná změna přesnosti není povolená.
4. `FPGA SAMOKONTROLA: zamitnuto oken: citani` po 5+ h: čekáme výrazně pod 7,6/h.
5. Kanál B: +24 ppm je beze změny očekávaná, pokud je příčina ve vstupu; změna by naznačila vliv sítě LW/PRIMARY (K2).

## 8. Dokončené ověření simulací a mutacemi (2026-10-27, čistá kopie z HEAD `048f4ab`, FW 0x0508)

- **Regresní sada v3 (`sim/run.ps1`):** `phyv3`, `csglitch`, `ccdcarry`, `holdinv`, `ppsv3`, `topv3`, `topv3ovf`, `topv3cal`, `topv3pw` všechny PASS (nejdelší `topv3` 79 min při souběhu s mutacemi). Poznámka: `primchk`, `segdsp` a `tdc3eq` v `run.ps1` neexistují, jejich pokrytí zajišťují režimy 7, 9 a 10 v `mut_top_v3.py`.
- **Mutace 1PPS (`mut_pps_v3.py`):** 23 zachyceno, ekvivalentní zdůvodněné, nezachycená 0.
- **Mutace celého návrhu (`mut_top_v3.py`):** 59 zachyceno, 2 ekvivalentní zdůvodněné (`znacka: ts_valid o takt driv`, `1PPS: seq > 1 vzdy`), **nezachycená 0**. Běh trval ≈ 18 h kvůli 6 paralelním simulacím s režimy DEEP_RAW a PSRAM (jednotlivé mutace 1–4 h).
- Závěr: kolo 2 + FW 0x0508 jsou po stránce simulací a negativních kontrol uzavřeny; zbývá jen ověření na desce (noc 2026-10-26/27: viz `2026-10-26_spi-noc-vyhodnoceni.md` kap. 8).
