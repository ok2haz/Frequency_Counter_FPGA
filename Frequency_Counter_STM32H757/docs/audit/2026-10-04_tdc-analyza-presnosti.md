# Analýza přesnosti TDC — co je změřené, co jen simulované (2026-10-04)

> Vznik: otázka uživatele „odkud jsi přišel na 30 ps? jak jsi to měřil? a jak jsi zjistil,
> že se kanály shodují?". Krátká odpověď: **obě čísla jsou ze simulace, ne z desky.**
> V shrnutí z 2026-10-04 08:xx jsem je napsal jako vlastnost návrhu bez té výhrady —
> to bylo zavádějící. Tento dokument odděluje simulaci od měření a dává čísla z desky.

## 1. Odkud je „~30 ps" — SIMULACE

**Zdroj:** `Frequency_Counter_FPGA_Module/sim/tb_tdc.sv` (Icarus Verilog), běh 2026-10-03/04:

```
CAL A: valid=1 ovf=0 peak=299 nz=156 last=156 | B: valid=1 ovf=0 peak=287 nz=156 last=156
PRESNOST A: n=24 chyba dt: stred=-1.0 ps sigma=27.6 ps | B: n=24 stred=-1.0 ps sigma=32.3 ps | rozdil stredu A-B=0.0 ps
```

**Jak se to počítá:** testbench generuje IDEÁLNÍ obdélník se známou periodou `TSIG = 97 123 ps`
(`tb_tdc.sv:16`), FPGA změří 24 oken; pro každé okno je chyba = `dt_změřené − počet_period × TSIG`.
`sigma` = směrodatná odchylka těch 24 chyb.

**Co simulace NEOBSAHUJE (a proto to číslo o desce nic neříká):**

| předpoklad simulace | skutečnost |
|---|---|
| zpoždění stupně `ALU` = náhodně 32 ± 12 ps, rovnoměrně, nezávisle (`sim/gowin_models.v:25-31`) | 32 ps je jen **průměr** odvozený z prvního CAL reportu z desky (256 stupňů ≈ 8,3 ns); rozložení, „bubliny", skoky mezi sloupci logiky a teplotu model nezná |
| hodiny 100 MHz ideální, bez jitteru | reálné hodiny ze Si5356 |
| vstupní signál ideální, bez šumu | generátor + vstupní obvody |
| cesty k registrům vzorků bez rozdílu zpoždění | na čipu se cesta k registru spouštěče (`t0`) a k registrům vzorků (`q`) liší — viz kap. 4 |

24 oken je navíc málo na odhad σ (nejistota σ při n = 24 je ~±15 %).

## 2. Odkud je „kanály se shodují, A−B 0,0 ps" — SIMULACE

Totéž `tb_tdc.sv`: oba kanály dostanou **tentýž ideální signál**, B posunutý o 3 333 ps
(`tb_tdc.sv:17,85`), a porovnají se **střední hodnoty** chyb obou kanálů. Oba kanály v simulaci
používají **tentýž model** zpoždění (jen jiné náhodné hodnoty), takže shoda středů je očekávaná
a o symetrii skutečných kanálů na čipu **nevypovídá**.

**Na desce kanál B dosud neměřil vůbec** — na CH_B není připojený signál. Z desky máme jen
kalibrační histogram kanálu B (kap. 3.2), ne jeho měření.

## 3. Co je skutečně ZMĚŘENO na desce

### 3.1 Chyba délky okna (kanál A), FW 0x040C, 2026-10-04

**Metoda** (`tools/tdc_dt_sigma.py`, čte přes UART `fpgaraw` 80 po sobě jdoucích DATA rámců):
z každého rámce `edge_count` (abs 20) a `dt_a` (abs 118, jednotky T/16384); kmitočet generátoru
`f_ref` = medián `edges/dt`; chyba okna = `dt − edges / f_ref`.

**Výsledek:** generátor 10 MHz na CH_A, 80 oken po 0,25 s:

| veličina | hodnota |
|---|---|
| σ chyby okna | **1 624 ps** |
| min / max | −1 086 / +4 142 ps |
| medián \|chyby\| | 780 ps |
| 90. percentil \|chyby\| | 3 452 ps |
| σ kmitočtu | 0,065 Hz = 6,5·10⁻⁹ |

Prvních 20 chyb [ps]: `2382 −463 2383 −928 2383 −1045 2382 −630 2643 −689 2642 −897 2536 −630 780 −1086 780 −783 780 −783`
— chyby se **střídají a opakují stejné hodnoty**, tedy jde o systematickou chybu převodu
(hrany padají opakovaně do týchž chybných binů), ne o náhodný šum.

**Výhrady metody:** obsahuje i nestabilitu generátoru a OCXO za 0,25 s (u běžného laboratorního
generátoru řádově 10⁻¹⁰ až 10⁻⁹ → desítky až stovky ps). Na úrovni ~1,6 ns ale dominuje TDC.
Pro FW 0x040A (2026-10-03) dala tatáž metoda σ ≈ 1,9 ns (60 oken).

### 3.2 Kalibrační histogram, FW 0x040C + CM7 `tdc hist`, 2026-10-04

N = 2²⁰ = 1 048 576 kalibračních událostí na kanál. Surová data: `2026-10-04_tdc-hist.txt`, výpočet: `python tools/tdc_hist_analysis.py`. Šířka binu = podíl událostí × 10 000 ps.

| | kanál A | kanál B |
|---|---|---|
| rozsah kódů | 34..134 | 28..134 |
| obsazených kódů | 84 | 96 |
| prázdných kódů uvnitř rozsahu | 17 | 11 |
| medián šířky obsazeného binu | 94 ps | 80 ps |
| **největší bin** | **kód 134: 1 947 ps** | **kód 134: 1 520 ps** |
| další největší | 244, 242, 221 ps | 265, 238, 228 ps |
| kvantizační σ jedné hrany (z histogramu) | 251 ps | 175 ps |
| — **bez** největšího binu | **39 ps** | **37 ps** |
| podíl největšího binu na rozptylu | **98 %** | **96 %** |

(Kvantizační σ = Σ pᵢ·wᵢ²/12, kde pᵢ = podíl událostí v binu, wᵢ = jeho šířka; okno = 2 hrany → ×√2.)

## 4. Výklad

1. **Celou nepřesnost dělá JEDEN bin — poslední obsazený kód 134**, široký 1,5–1,9 ns.
   Bez něj by kvantizace jedné hrany byla ~38 ps (okno ~54 ps) — to je řádově to, co dávala
   simulace. Řetěz sám tedy rozlišení má; ztrácí se v jednom místě.
2. **Kódy 0..27 (B), resp. 0..33 (A) nejsou obsazené vůbec**, a šířka obřího binu zhruba odpovídá
   časovému úseku, který by tyto chybějící kódy pokryly (~2 ns). **HYPOTÉZA:** hrany, které na
   začátek řetězu dorazí těsně před vzorkovací hranou hodin, zachytí registry vzorků `q`
   (`tdc.v:121`), ale ještě ne registr spouštěče `t0` (`tdc.v:123`, samostatný registr na `s[0]`
   s jinou cestou). Spouštěč je pak zaregistruje až o takt později, kdy je hrana o 10 ns dál —
   a dekodér (`lead32`, `tdc.v:261-288`) vrátí saturovaný kód 134. Takové hrany pak dostanou
   chybný čas až o desítky stovek ps. To by vysvětlovalo prázdné nízké kódy, obří poslední bin
   i střídající se chyby okna řádu ±2 ns v kap. 3.1.
   **Ověření:** (a) v reportu P&R porovnat zpoždění `s[0] → t0` a `s[0..] → q[0..]`;
   (b) odvodit spouštěč přímo ze vzorkovaného teploměrového kódu (`q`) místo samostatného `t0`,
   takže spouštění i vzorkování uvidí tutéž hranu v tomtéž taktu; (c) znovu `tdc hist`
   (cíl: bez obřího binu, obsazené i nízké kódy) a `tools/tdc_dt_sigma.py` (cíl: σ okna řádu 50–100 ps).
3. **Prázdné kódy uprostřed rozsahu** (17 / 11) jsou typické „bubliny" carry řetězu; kalibrace
   je zvládá (bin nulové šířky), rozlišení snižují jen mírně (medián 80–94 ps).

## 5. Co z toho plyne pro dřívější tvrzení

| tvrzení | stav |
|---|---|
| „TDC drží přesnost ~30 ps" | ❌ **jen simulace**; na desce σ okna **1,6 ns** (FW 0x040C) |
| „kanály A a B se shodují (A−B 0,0 ps)" | ❌ **jen simulace**; na desce kanál B neměřil, symetrie **neověřena** |
| „řetěz pokrývá periodu" (0 za koncem) | ✅ změřeno (CAL report, histogram) |
| rozlišení řetězu bez obřího binu ~80–94 ps/kód | ✅ změřeno (histogram) |

## 6. Jak symetrii kanálů změřit na desce

Přivést **tentýž signál na CH_A i CH_B** (rozbočení, stejně dlouhé kabely) a v každém okně
porovnat `dt_a` s `dt_b` (rámec nese oba, abs 118 a 101). Rozdíl středů = offset kanálů
(zahrnuje i rozdíl cest pin → řetěz, STA: 6,23 vs 6,14 ns), rozptyl rozdílu = nezávislý odhad
přesnosti obou kanálů dohromady — bez vlivu stability generátoru, protože ta se v rozdílu vyruší.
To je lepší metoda než kap. 3.1 a měla by být standardním testem.
