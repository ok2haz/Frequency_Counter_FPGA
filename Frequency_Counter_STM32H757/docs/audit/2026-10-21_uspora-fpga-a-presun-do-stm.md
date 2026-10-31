# Úspora zdrojů FPGA a co přesunout do STM (stav FW 0x0426, 2026-10-21)

Zdroje čísel: **[P&R]** `impl/pnr/Counter_FPGA.rpt.txt` 0x0426 · **[netlist]** `sim/res_breakdown.py` nad `Counter_FPGA.vg` (rozpad podle jmen, hierarchie je zploštělá, shody jmen jsou přibližné) · **[v3 modul]** samostatná syntéza modulů v3 (`v3/v3_syn_report_2026-10-21.html`) · **[ÚVAHA]** odhad ±50 %, rozhodne P&R. Nic se tímto dokumentem neměnilo.

## 1. Kde jsme

| | 0x0425 (vydaný) | 0x0426 (dělení v STM) |
|---|---|---|
| CLS | 3 635 / 4 320 = **85 %** | **85 %** (první běh 84 %) |
| registry | 66 % | 64 % |
| setup rezerva | +0,98 ns | +1,5 ns |
| brána `fpga_release.py` | strop 85 %, varování od 82 % | totéž |

**[P&R] 0x0426:** logika 4 556 (3 026 LUT + **1 530 ALU**) = 53 %, registry 4 237 (64 %), BSRAM 5/26, DSP 3/10, **CLS 85 %**.

🔑 **CLS (85 %) je výrazně nad logikou (53 %) i registry (64 %).** Z 4 556 buněk logiky a 4 237 registrů by při dokonalém slučování stačilo kolem 2 300 CLS, obsazeno je 3 635 (+58 %). Důvod neznám (nejspíš řetězy ALU, různé řídicí signály CE/SR registrů a roztažené rozmístění kvůli časování). Praktický důsledek: **nejdražší jsou aritmetické bity (ALU: čítače, sčítačky, odečítačky, komparátory) a registry s vlastním povolením**, ne holé LUT.
Pravidlo palce z těchto dvou běhů ([ÚVAHA]): **1 procentní bod CLS ≈ 50 registrů + ≈ 55 buněk logiky**, odebraných společně.

## 2. Z čeho je FPGA složena [netlist 0x0426]

| blok | FF | poznámka |
|---|---|---|
| vzorkování TDC (`q_r`, `qd_r`) | 1 075 (25 %) | 2 × 268 tapů × 2 stupně; fyzika, nelze |
| dekodér / tabulka / čas značky (`ld_c` 507 LUT, `pg_c` 198 LUT, `ts_ps`, `tr1/tr2`, `tick_hi`, `lut_q3`) | 354 FF + ~800 LUT | fyzika |
| **kalibrace TDC** (`cnt`, `cum`, `xv`, `hcur`, `hist_*`, `lutv`, `scan_k`, `clr_k`, `lfsr`, `d_*`, `cal_tmo`) | **573** + ALU | **kandidát na přesun** |
| okno `win_recip` (`snap`, `r_periods`, `r_dt`, `ref_ts`, `count_*`, `ccnt`, `cdd`, `csnap`, `age`) | 539 | část lze zjednodušit |
| regrese `regr_acc` (sy/sx akumulátory, x0, y0, výstupní zámky) | 694 | v3 nahradí `seg_acc` |
| zachytávací registry rámce (`h_*`, `dt_a_lat`, `dt_b_hold`, `periods_*`) | 387 | dvojí zachycení |
| SPI / PHY / rámec | 262 + **`tb` 629 LUT** | multiplexer 128:1 je největší jediná položka LUT |
| hlídače (hodiny, koherence, ref, wdog, 1PPS) | 97 | nutné |
| ostatní | 256 | |

ALU: **536 z 1 530 je zpožďovací řetěz TDC** (2 × 268), zbylých ~1 000 je aritmetika (čítače period a hran, akumulátory, odečty časů, komparátory kalibrace).

## 3. Kandidáty — co přesunout do STM a co ne

CLS úspory jsou **[ÚVAHA]**, podle pravidla palce z kap. 1.

| # | co | co se ušetří ve FPGA | CLS | cena a riziko | doporučení |
|---|---|---|---|---|---|
| **S1** | **Výpočet kalibrační tabulky ze histogramu v STM** (FPGA jen sbírá histogram, STM spočte LUT a zapíše přes `LUT_WRITE`) | `cum`, `xv`, `hcur`, `lutv`, `scan_k`, `xs14`, sčítačky a komparátory, ~300 FF + ~300 buněk logiky | **−6 až −7 p. b.** | nový kanál STM→FPGA (378 položek × 16 b ≈ 760 B = 7 SPI rámců); FPGA musí po zapnutí běžet na nominální tabulce, dokud STM nezapíše a neověří (`LUT_ID` echo + CRC); **bez STM je FPGA hůř použitelná**; navazuje na historii „FPGA se někdy nahraje špatně" (L-0141, L-0143) | **ano, největší páka.** Navíc zlepší kvalitu: STM sečte víc sběrů (INL 4,9 → 2,4 ps podle v3 návrhu), umí teplotní tabulky a přepočet uložených dat |
| **S2** | **Statistiky řetězu `d_ovf`, `d_peak`, `d_nz`, `d_last` počítat v STM z histogramu** | ~134 FF + komparátory/čítače | **−2 až −3** | STM musí po každé kalibraci načíst celý histogram (dnes 2 × 256..512 transferů, ~0,2 s jednou; lepší bulk); `status`/`tdc` rezerva řetězu se počítá z `last` + `ovf` | **ano, po S1** (stejná infrastruktura) |
| **S3** | **Odečet `dt = ts − ref_ts` a detekci aliasu přesunout do STM** (FPGA pošle absolutní čas uzavírací hrany, STM odečte předchozí; rozbalení modulo 2⁴⁸ ≈ 172 s) | `ref_ts` 48 FF, 48bit odečítačka (48 ALU), `age` čítač, `alias`, ~100 FF + ~70 buněk | **−2** | změna významu 48b pole; hradlo 1 s a alias nad 171,8 s se ošetří v STM; v3 ji beztak ruší | **ano, nízká priorita** (zmizí s v3) |
| **S4** | **Rámec bez 128:1 multiplexeru** (každé pole se zapíše do TX RAM jednou při změně, žádné `tb` kombinační větvení) | z `tb` 629 LUT zbude ~200–300 | **−3 až −5** | čistě FPGA, bez STM; časování TX RAM při 100 MHz a CDC do domény PHY (L-0133, L-0141); **v3 s 1024 B rámcem ho stejně přepracuje** | až ve v3 |
| **S5** | **Jeden stupeň zachycení výsledku okna místo dvou** (`*_lat` v 10 MHz doméně a pak `h_*`) | ~200 FF | **−3 až −4** | CDC mezi 100 a 10 MHz musí zůstat bezpečné; pokrývá `dt_a/b`, `periods_a/b`, `ce/cs`, `bub`, `ovl` | **ano, FPGA-only**, nízké riziko při dobré simulaci (`topselfchk`) |
| **S6** | Regrese přes histogram kódů (STM: `Σ lut[k]·h[k]`) místo `Σ(t−t0)` ve FPGA — varianta z `2026-10-18_fpga-vs-stm.md` | `sy` akumulátory, odečty času, ~400–500 FF | **−6 až −9** | histogram segmentu 2 × 512 × 19 b do BRAM + bulk (~40 rámců na okno); alternativa k v3 | **ne** — v3 dosáhne téhož surovými součty bez BRAM |
| **S7** | Zkrácení řetězu TDC (Z10: ~190 FF + ~125 LUT na kanál) | ~380 FF + ~250 LUT | −7 | **rezerva řetězu A 16, B 18 tapů z 268; v chladu se řetěz zrychlí**; sleduje `tdc` → `max tap` | **ne** |
| — | vzorkování TDC, dekodér, kontrolní počítání hran (`ccnt/cdd`), hlídač hodin a koherence, SPI PHY | — | — | fyzika a časování, STM je nedohoní | **nechat** |

**Součet realisticky dosažitelného bez v3:** S1 + S2 + S5 ≈ **−11 až −14 p. b.** (CLS 85 → ~72 %). S3 + S4 se přidají jen ve v3.

## 4. Vztah k v3

- **v3 už S3, S4 a S6 řeší** (spojité segmenty, surové součty, žádná okna, žádný 128:1 multiplexer). Pokud se v3 dělat bude, **není důvod dělat S3/S4 zvlášť**.
- **[v3 modul] syntéza nových bloků** (2× `seg_acc` + 2× `pin_cnt` + `ides_pps`): **983 FF, 1 001 buněk logiky (447 LUT + 546 ALU), 4 × ALU54D, IDES8, rPLL, CLKDIV, 0 BSRAM**, Fmax 105 MHz (syntéza). Odhad CLS ≈ +12 p. b. Dále kruh záznamů (~10 BSRAM), skladač 1024 B s CRC bloků, časovač segmentů, absolutní čítače period.
- **Návrh druhé instance (cíl 68 % CLS) není podložený měřením**; s dnešním stavem 85 % a změřenými novými bloky vychází v3 na 85 % jen tehdy, když zmizí aspoň ~20 p. b. z dnešních bloků (okna, regrese, zámky, multiplexer, hradlo). **Teprve zkušební P&R (kap. 5) to rozhodne.**
- **S1 a S2 platí i pro v3** (kalibrace je stejná) a uvolní místo v obou případech, proto jsou nejbezpečnější investice.

## 5. Doporučené pořadí

1. **Dokončit a ověřit 0x0426** na desce (opravená regrese, `phrec dump`, ADEV z fáze).
2. **S5** (jedno zachycení) — malé, FPGA-only, simulace `topselfchk`/`txequiv`.
3. **S1 + S2** (kalibrace a statistiky do STM): STM→FPGA `LUT_WRITE`/`LUT_SWAP`, ověření `LUT_ID`, nominální tabulka po zapnutí, `tdc cal` na STM; po tom odstranit z FPGA kalibrační aritmetiku. Největší úspora, největší přínos kvality.
4. **Zkušební P&R v3** (`seg_acc` + `pin_cnt` + `ides_pps` + kruh místo oken a regrese) s uvolněným místem z kroků 2–3; až podle výsledku rozhodnout o plném v3.

## 6. Co přesun do STM nepřinese

- **Vyšší přesnost sama o sobě.** Podlaha přístroje s regresí je ADEV 2,9·10⁻¹¹ @ 0,25 s (vlastní reference), omezuje ji zdroj, TDC šum a rezerva řetězu, ne to, kde se dělí. Přesun slouží k **uvolnění CLS** (třetí TDC kanál, regrese kanálu B, 1PPS s IDES8, rezerva časování) a k **přesnosti kalibrace** (S1: více sběrů, teplotní tabulky).
- **Stabilitu bitstreamu.** Chyba děličky regrese (L-0187) vznikla v logice, kterou simulace i STA vyhodnotily jako správnou; **čím méně aritmetiky ve FPGA, tím méně míst, kde křemík selže bez příznaku.** To je druhý, ne méně důležitý důvod pro S1 a S2.

## 7. Stav po realizaci (2026-10-21, doplněno)

| položka | stav | výsledek |
|---|---|---|
| FW 0x0426 (dělení regrese v STM) | ✅ na desce | CLS 85 → 84–85 % (dělička pryč, zámky surových součtů zůstaly); odstraněna chyba 2¹⁶ v součtu indexů (L-0187) |
| **S1 + S2** (kalibrace + statistiky řetězu do STM), FW 0x0427 | ✅ na desce | **CLS 79 %, registry 60 %** (z 85 / 64), STM kalibruje za 360 ms, selftest 16/16 |
| S5 (jedno zachycení výsledku okna) | ❌ zamítnuto | úspora ~200 FF = ~2 p. b., ale riziko roztrženého rámce bez příznaku (okna A a B se uzavírají současně; `h_*` je ochrana) |
| S3 (odečet `dt` do STM) | ⬜ odloženo | ~2 p. b.; zmizí s v3 |
| S4 (rámec bez multiplexeru 128:1) | ⬜ odloženo | ve v3 |

Zbývá do cíle ~68 % (v3): rozpočet z návrhu druhé instance je optimistický; po 0x0427 je volných ~6 p. b. více pro zkušební P&R v3 (`Frequency_Counter_FPGA_Module/v3/`, nezapojeno).
