# Platné číslice nad 13 míst: rozbor a plán (2026-10-27)

Dotaz: jde měřit na více než 13 platných míst? Vybraná cesta: lepší rozlišení čítače (FPGA), obecný signál různých kmitočtů.
Podklady: `docs/TDC_MATEMATIKA.md` kap. 9, 10, 12, `meas_present.h` (`MP_TDC_PS`, `MP_REGR_FLOOR_REL`), stav desky 2026-10-27 (UART `tdc`, `status`, web `/api/stab`).

## 1. Kolik je dnes

| veličina | hodnota | zdroj |
|---|---|---|
| σ jedné časové značky | ≈ 105 ps (80–160 ps podle estimátoru) | TDC_MATEMATIKA kap. 9 |
| kvantizace tabulky | σ_q = 27–30 ps | tamtéž (z histogramu) |
| největší bin kalibrace | A 293 ps, B 215 ps (zdravý stav < ~200 ps) | UART `tdc` 2026-10-27 |
| podlaha jednoho okna 0,25 s s regresí | 2,9·10⁻¹¹ (změřeno) | `MP_REGR_FLOOR_REL` |
| teoretický limit regrese | 1,4·10⁻¹² | TDC_MATEMATIKA kap. 9 |
| ADEV z fáze @ 1 s (fázový záznam) | 3,1·10⁻¹² | CLAUDE.md, FW 0x0426 |
| ADEV měřeného signálu @ 1 s (`/api/stab`, 2026-10-27 05:00, kanál A = externí GPSDO) | **1,46·10⁻¹⁰** | `sy1e15` = 145 555 |

Převod na číslice u 10 MHz (8 celých): 13 platných míst = 5 desetin = 10⁻¹² relativně = 10 µHz; 15 míst = 10⁻¹⁴.
Jedno okno tedy dnes unese ~10,5 číslice (2,9·10⁻¹¹), fázový záznam vlastní reference dává ~12,5 (3,1·10⁻¹² @ 1 s); 14 a více míst vyžaduje 10⁻¹³ a lepší.
**Pozor na skutečný signál:** u dnešního vstupu (externí GPSDO s okenní anténou) je ADEV @ 1 s 1,5·10⁻¹⁰, tedy o řád až dva horší než čítač. Pro tento zdroj je limitem zdroj, ne čítač, a lepší TDC by na zobrazených číslicích nic nezměnilo. Zlepšení rozlišení čítače se projeví jen u stabilnějších signálů (vlastní OCXO/GPSDO desky, vodíkový maser, rubidium).

## 2. Co limituje

1. **Šum časové značky ~105 ps je 3,5× větší než kvantizace tabulky (30 ps).** Zdroj není znám (otevřený bod O4): podezření vstupní hrana (strmost, šum napájení HC04), jitter hodin Si5356, vlastní TDC. Teoretická regrese by při σ = 105 ps dala 1,4·10⁻¹², naměřená podlaha je ale 20× horší, takže je v řetězu další nezachycený člen (kalibrace nestabilní: největší bin 311 vs 198 ps, O3, dnes 293 ps).
2. **Reference:** zamčené GPSDO má ADEV fáze OCXO–GPS ≈ 1,1·10⁻¹¹ @ 1000 s (`MP_REF_PPB_LOCK`), tj. absolutní správnost nad ~11 číslic je omezená referencí, ne čítačem. Relativní rozlišení (opakovatelnost proti téže referenci) může být lepší.
3. **Nízké kmitočty:** počet hran v okně je malý, podlahu dává `σ·√(2/n)/(0,75·G)` s n = f·G/8; pro < 1 kHz rozlišení poklesne úměrně kmitočtu.

## 3. Cesty a odhad přínosu (od nejlevnější)

| cesta | změna | odhad | riziko |
|---|---|---|---|
| A. Průměrování oken N (σ/√N) a delší hradlo 10 s, 100 s | firmware (UI, bez FPGA) | 1 → 10 s: ~3×; 100 s: ~10× (bílý šum značky) | žádné; pomalejší obnova |
| B. Najít zdroj šumu značky (O4) | měření, pak případně HW/FPGA | až 20× (rozdíl teorie a naměřené podlahy) | potřebuje signál na A i B současně a osciloskop |
| C. Stabilní kalibrace TDC (O3), vyhlazení bin 293 ps | FPGA/STM (tabulku počítá STM, FW 0x0427) | zlepšení INL, řádově desítky % | střední; rezerva časování FPGA tenká |
| D. Regrese i pro kanál B (O6) | FPGA, +11–13 % CLS | B na úroveň A | CLS 79 % → ~90 % překročí strop brány 85 % |

## 4. Měření, která rozhodnou (nejdřív B)

1. Tentýž signál (rozbočka 50 Ω) na CH_A i CH_B: `dt_a − dt_b` dává čistý šum TDC bez vlivu zdroje a reference. Pokud je jeho σ ≈ 105 ps/√2, je zdroj v TDC/hodinách; pokud mnohem menší, je to vstupní hrana/zdroj.
2. Osciloskop na vstupním pinu: strmost a šum hrany (podezření napájení HC04).
3. Dva různé kmitočty (např. 1 MHz a 100 MHz) pro ověření závislosti na počtu hran (O9).
4. Stabilita kalibrace: `tdc cal` opakovaně v různých teplotách, sledovat největší bin (O3).

Dokud bod 1 neproběhne, nelze rozhodnout mezi C a D. Cesta A je bez hardwaru dostupná hned.
