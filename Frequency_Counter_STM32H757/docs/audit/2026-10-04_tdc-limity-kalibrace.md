# TDC — limity, měření, kalibrace (2026-10-04)

> Navazuje na `2026-10-04_tdc-analyza-presnosti.md`. FW 0x040C na desce, kanál A
> buzený generátorem ~10 MHz, warm-up. Měřeno přes konzoli (`tdc`, `tdc hist`,
> `tools/tdc_dt_sigma.py`) a ETH `/api/state`. Sondou se neměřilo (halt zabíjí I2C4).

## 1. Jak TDC měří (shrnutí)
Carry-chain TDC, 512 ALU/kanál, vzorkuje se každý 2. stupeň (STRIDE 2 → 256 kódů).
Přesný čas dostane jen hrana na hranici okna; hrany uvnitř se jen počítají. Jednotka
času T_clk/16384 = 0,6103515625 ps. Kalibrace **hustotou kódů** běží ve FPGA (ring
oscilátor + LFSR, tabulka šířek binů v BRAM) při bootu a na `tdc cal`.

## 2. Naměřené limity (deska, 2026-10-04)

### 2.1 Rozlišení a pokrytí řetězu — ZMĚŘENO, OK
`tdc` CAL report:
```
TDC A: kodu 84/256 (nejvyssi 134), za koncem retezu 0
TDC B: kodu 95/256 (nejvyssi 134), za koncem retezu 0
status 0x03 (cal A:1 B:1 fail:0 busy:0, retez kratky A:0 B:0)
```
- Řetěz pokrývá celou periodu 10 ns („za koncem řetězu 0", „krátký 0") — **délka řetězu není problém**.
- Medián šířky obsazeného binu ~80–94 ps (z histogramu), prázdné biny uvnitř = bubliny carry (kalibrace je zvládá).
- `tdc_ps = 57` = STA model 57 ps/tap (křemík je rychlejší, ~32 ps/tap).

### 2.2 Dominantní mez — OBŘÍ BIN kód 134
`tdc hist` (2²⁰ událostí/kanál, data `2026-10-04_tdc-hist.txt`):

| | kanál A | kanál B |
|---|---|---|
| kód 134 (poslední) | **204 189 = 19,5 %** všech hran | **159 393 = 15,2 %** |
| kódy 34–133 | ~80 % rovnoměrně | ~85 % |

**~1 z 5 hran spadne do saturovaného kódu 134.** Mechanismus: hrany, které dorazí
těsně před vzorkovací hranou hodin, zachytí vzorky `q`, ale spouštěč `t0` (delší
cesta ~3,1 ns, placement přes půl čipu) ne → spouští se o takt později, kdy je hrana
~10 ns dál → dekodér vrátí saturovaný vysoký kód. Je to **časování trigger vs. vzorky**,
ne délka řetězu ani vada paměti.

### 2.3 σ okna — ZMĚŘENO
`tools/tdc_dt_sigma.py` (30 rámců, kanál A, generátor 10 MHz, warm-up):
```
σ okna = 1133 ps (min −1248, max +4330 ps)
|chyba| medián 182 ps, 90. percentil 1248 ps
σ f = 4,53·10⁻⁹
```
- **„Dobré" hrany: medián 182 ps** (zahrnuje i kvantizaci ~80 ps·√2 a drift generátoru/OCXO za 0,25 s).
- **Když hrana okna trefí kód 134: chyba ~2–4 ns** → 90. percentil 1248 ps, max 4330 ps.
- Dřívější běh (doc z rána) dával σ 1624 ps; rozptyl mezi běhy = kolik hranic okna
  padlo do kódu 134. **Bez obřího binu by σ okna bylo ~50–80 ps** (medián dobrých hran ×√2).

### 2.4 Ostatní meze
- **Rozsah:** jedna hrana v řetězu → přímo ~30 MHz; nad tím předdělička / počítání period.
- **Offset kanálů A↔B** (STA): pin→hlava A 6,23 ns, B 6,14 ns. Pro frekvenci se krátí; pro TI A−B ne → nutná kalibrace offsetu (zatím neměřeno, CH_B nebuzen).
- **Teplota:** šířky binů = skutečná zpoždění křemíku, driftují ~0,1–0,3 %/°C. Kalibrace platí jen poblíž teploty, při které proběhla → viz §4.

## 3. Teploty (ETH, warm-up)
OCXO 48 °C (ovenizovaný, stabilní), **FPGA 37,5 °C** (relevantní pro TDC), deska 30,6 °C.
Během warm-upu FPGA roste ~30 → 38 °C → kalibrace z bootu rychle zastarává.

## 4. Kalibrace

### 4.1 Hustota kódů (stávající, FPGA)
Běží při bootu (~0,2 s) a na `tdc cal`. Měří skutečná zpoždění stupňů → tabulka šířek binů.
Platí pro teplotu v okamžiku kalibrace.

### 4.2 NOVÉ: teplotní rekalibrace (CM7, 2026-10-04) — IMPLEMENTOVÁNO
`freertos_task_fpga.c`: FpgaTask hlídá FPGA teplotu (`g_sensors[SENS_T4A]`, TMP117 0x4A)
a při driftu **≥ 3 °C** od poslední kalibrace pošle `fpga_freq_tdc_cal_start()`
(neblokující — cal běží ve FPGA ~0,5 s, měření se na tu dobu zastaví). Rate-limit 30 s
proti thrashingu na prahu. Loguje `TDC: teplotni rekalibrace, FPGA X.X -> Y.Y C`;
počítadlo `g_tdc_recal_count`, teplota poslední cal `g_tdc_cal_temp_c10`.
- **Proč 3 °C:** kompromis mezi driftem (~0,1–0,3 %/°C × 3 °C ≈ 0,3–0,9 % posun binů)
  a počtem přerušení měření (warm-up 30→38 °C = ~2–3 rekalibrace, pak klid).
- **Bezpečné:** jen posílá povel; pokud 0x4A není osazen (`.valid == 0`), rekalibrace se
  netriggeruje (fail-safe). ⬜ Neověřeno na HW (čeká na flash + warm-up).

## 5. Zpřesnění měření — cesta (NEIMPLEMENTOVÁNO)
Hlavní výhra = **odstranit obří bin** (19,5 % hran → z 1,1–1,6 ns na ~50–80 ps σ okna).
Je to RTL oprava časování `t0`/`q`. ⚠️ Pokus **0x040E to zkusil naslepo dvakrát a oba
zhoršil** (metastabilita → mrtvý CH_A; pak dvojnásobná brána) — leží ve `git stash`.
**Musí se iterovat s živým `tdc hist` po každé verzi** (ne naslepo). Směr: srovnat
placement/latenci spouštěče se vzorky tak, aby obě viděly hranu v tomtéž taktu, bez
rozbití hrubého počítání hran/brány (to rozbila 0x040E).
Ověřovací smyčka: build bitstreamu → flash → `tdc hist` (cíl: kód 134 < ~1 %) →
`tdc_dt_sigma.py` (cíl: σ okna 50–80 ps).

## 6. Nástroje
- `tools/tdc_dt_sigma.py N` — σ okna z N rámců (2026-10-04 opraveno čtecí okno 0,6→1,3 s a práh rámce 128→124 B, jinak sbíral 0 rámců).
- `tools/tdc_hist_analysis.py` — rozbor histogramu z `tdc hist` výpisu.
- `tdc` / `tdc cal` / `status` řádek `TDC:`.
