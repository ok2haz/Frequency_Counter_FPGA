# E0 — implementace (FW 0x0425 + STM) a zadání pro Claude CLI — 2026-10-20

Fáze: **kód napsaný, NEPŘELOŽENÝ v Gowinu, NESIMULOVANÝ v HDL.** Ověřeno jen behaviorálními modely v Pythonu
(`model/`) a syntaktickou kontrolou C (gcc s náhradními hlavičkami HAL). Úkolem CLI je simulace, P&R a deska.

Rozhodnutí uživatele 2026-10-20, ze kterých dokument vychází:
- **rozsah 0,1 Hz – ~400 MHz** (cíl aspoň 200 MHz), **kmitočet se nikde nenastavuje** (automatický rozsah);
- **v3 = spojité segmenty 25 ms**;
- **postup: nejdřív E0 na desku, potom v3**;
- **maskování značek při CS zapnuté**, s možností vypnout;
- **pin 86 až při práci na HW** → TODO na konci.

Návrh v3 (protokol 1024 B, segmenty, automatický rozsah, analýzy) je v `2026-10-20_v3_navrh_a_analyzy.md`.

---

## 1. Obsah balíčku

| cesta | co |
|---|---|
| `fpga/top.v`, `fpga/tdc.v`, `fpga/spi_app.v` | FW 0x0425 (celé soubory) |
| `stm/fpga_freq.c/.h`, `stm/fpga_phrec.c/.h` | STM: diagnostika, kontroly, adaptivní mez regrese, fázový záznam |
| `stm/freertos_task_fpga.c`, `stm/gpsdo.h`, `stm/meas_present.h` | GPSDO: šum 1PPS za běhu, historie v ps |
| `stm/fpga_crc.c/.h`, `stm/fpga_spi_dma.c/.h` | CRC (SW tabulka / HW jednotka) a SPI přes DMA — **zadání v `2026-10-20_CRC_DMA_zadani_CLI.md`** |
| `patches/*.diff` | unified diff proti FW 0x0424 / dnešnímu STM |
| `model/m_tdc_ld.py`, `m_win_count.py`, `m_coh_mon.py`, `m_extra.py`, `m_audit4.py`, `vlint.py` (+ `.out`) | referenční modely, strukturní lint a výsledky |
| `test/t_fpga.c`, `test/t_gpsdo.c`, `test/t_phrec2.c`, `test/stubs.c` (+ `.out`) | testy STM na hostu (gcc) — postup v 8.2, 9.2 |
| `v3_navrh/` | NÁVRHY pro v3 (nezapojené): `seg_acc.v`, `pin_cnt.v`, `ides_pps.v`, `fpga_range.c/.h` |

`fpga_phrec.c` je nový soubor → přidat do projektu STM (`Close → Open Project`, L-0012 / CLAUDE.md).
`coh_mon` je ve `spi_app.v` → `.gprj` FPGA se nemění.

---

## 2. Změny FPGA (FW 0x0424 → 0x0425)

### 2.1 `tdc.v` (`tdc_chan`)

| změna | proč | cena |
|---|---|---|
| **oprava N1**: `ld = ldok & ~(ec & arm_r)`, `ldok <= ~trig_ok & (idle_r \| fz==3 \| fz==4)` | dříve `ld = idle_r & …` = 0 po celé zmrazení; hrana se vzorkem přesně 5 taktů za předchozí spuštěnou dostala vzorek předchozí hrany | 1 FF, `ld` dál 1 LUT |
| výstup `bub_o` (registr v taktu `dv1`) | bublina: nejvyšší prošlý tap (po 8) > kód + 8 → víc hran v řetězu / zákmit vstupu (O4) | 1 FF + 10b srovnání |

Multicycle `qd → code_r` (SDC, 5/4) platí dál: `qd` se nově může změnit na téže hraně, na které `code_r` zachytí
předchozí výsledek (neblokující přiřazení), další zachycení je ≥ 5 taktů po nahrání.

### 2.2 `spi_app.v`

**`win_recip`:**

| změna | proč |
|---|---|
| **oprava close+2**: `count_lo <= {7'd0, inc}` v `cl_q2`, `cy_q` blokovaný `~cl_q2` | hrana v taktu nulování se ztrácela nad 33,3 MHz; kontrola `\|ccd\| ≤ 1` ji propustila |
| `alias_lim` (vstup) místo pevných 250 | při hradle 1 s: 250 s > 171,8 s přetečení 48b `r_dt` → okno 171,8–250 s se tiše zabalilo |
| čítače `bubc`/`ovlc` → výstupy `r_bub[7:0]`, `r_ovl[3:0]` | bubliny a značky pořízené při aktivním CS (diagnostika O4); zachycení v `tsc_q` včetně uzavírací značky |
| `want_seg = (seg_a\|seg_b) & ~(mask_cs & cs_act)` | při aktivním SPI se vnitřní značky nespouští: rušení přenosem se do regrese nedostane (SPI je asynchronní k signálu → průměr se nevychýlí) |

**`spi_app` (rámec v2, kompatibilní):**

| abs | obsah | caps |
|---|---|---|
| 10 | hlídač koherence: bit0 platné, bit1 poslední epocha OK, bit2 od nahrání chyba, bit3 vybraná cesta | bit14 |
| 11 | počet epoch 102,4 µs s chybou koherence (sat. 255) | bit14 |
| 57 / 58 | bubliny okna, primární / sekundární slot (snímek s měřením) | bit14 |
| 59 | `{sekundární[3:0], primární[3:0]}` značky okna při aktivním CS | bit14 |

- `SET_CONFIG 0x05`: bit0 `mask_cs` (výchozí **1**), bit1 `led_hb` (výchozí **0** → LED na PIN29 stojí).
- `FW_VERSION = 0x0425`, `CAPS = 0xFEE2` (REGR) / `0xFF62` (bit14 diagnostika, bit15 xm v 1/4096 hrany — audit 4).

**Nový modul `coh_mon`** (přísný hlídač koherence 100 ↔ 10 MHz):
- `tgl10` (překlápí se každý takt 10 MHz) vzorkují dvě cesty, náběžná a sestupná hrana 100 MHz. Pevný fázový poměr hodin
  může jednu cestu držet trvale na přechodu (9/11), obě zároveň ne.
- Epocha = 1024 překlopení (102,4 µs). Chyba epochy = odchylka intervalu od 10 v **obou** cestách.
- Čítač chybných epoch v Grayově kódu (roste nejvýš o 1) → bezpečně do 10 MHz domény.
- Zachytí chybějící / navíc pulz 100 MHz, zákmit 10 MHz, fázový skok ≥ 10 ns (5–10 ns jen částečně, viz 8.3). Nezachytí jitter.
- Cena ~35 FF, ~35 LUT.

### 2.3 `top.v`

| změna | poznámka |
|---|---|
| CS do domény 100 MHz (`csa_s`, 2 FF), `mask_cs_en` (2 FF), `base_win` (2×3 FF) | kvazistatické / úrovně |
| `alias_lim = (base_win == 2) ? 170 : 250` | 1 s: 170 s < 171,8 s |
| latch `r_bub`/`r_ovl` A i B s výsledkem okna, mux primární/sekundární | stejně jako `ccd` |
| instance `coh_mon`, `en = ok_s[1] & boot_done`, Gray → bin v 10 MHz | abs 10/11 |
| `led_tx` stojí, pokud `led_hb_en = 0` | PIN29 je v **bance 2** s CH_A/CH_B/1PPS/hodinami |
| parametr `OSC_DIV` (výchozí 8 = beze změny), `OSC_TMO = 250·8/OSC_DIV` do `ref_guard` a `t10_tmo` | pokus H8: `define OSC_DIV 16` |
| volitelný port `bl_pwm` = 0 (`ifdef PIN86_BL_OFF`) | **jen po ověření HW, viz TODO** |

### 2.4 SDC (doplnit ručně)

```
// FW 0x0425: CS SPI je v domene 100 MHz jen uroven pro diagnostiku/maskovani (2 FF), cesta z pinu se nezkouma
set_false_path -from [get_ports {spi_cs_n}] -to [get_regs {csa_s*}]
```
Ostatní nové přechody (`tgl10`, `mask_cs_en`, `base_win` → 100 MHz) pokrývá `set_clock_groups -asynchronous`.
`coh_mon` má cestu sestupná → náběžná hrana (5 ns, jen vodič), STA ji časuje sama.

---

## 3. Změny STM

### 3.1 `fpga_freq.c/.h`

| změna | detail |
|---|---|
| čtení abs 10/11/57–59 (caps bit14) | `diag0_ok`, `coh_st`, `coh_cnt`, `bub_p/s`, `ovl_p/s` |
| surové `dt_units` | přesný součet časů bez zaokrouhlení na ps (fázový záznam) |
| `FPGA_CHK_FMAX` | okno s f > 33 MHz (FW < 0x0425) / > 40 MHz (FW ≥ 0x0425) se odmítne — do zavedení v3 počítání každé periody |
| `FPGA_CHK_COH` | nárůst `coh_cnt` = okno se odmítne jako chyba hodin |
| **adaptivní mez přijetí regrese** | `max(6·σ_d, 5·10⁻⁹)`, shora 5·10⁻⁸; σ_d = EWMA\|d\|·1,2533 ze VŠECH oken s regresí (\|d\| ořez 5·10⁻⁸, viz 8.3 V1); prvních 32 oken pevná mez; `fpga_freq_regr_limit()` |
| `fpga_freq_set_diag(mask_cs, led_hb)` | SET_CONFIG 0x05 |
| sekundární slot nad mezí | `freq16 = 0`, `edges_b = 0` (kritická analýza 2, K2) |
| `fpga_freq_check_stats2()` | počty FMAX / COH |
| fázový záznam (`fpga_phrec_feed` v `fpga_freq_poll`) | každé nové měření; `FPGA_STATS_USE_CC` (výchozí **0**) přepne délku okna pro statistiku na kmitočet mezi těžišti |
| `fpga_phrec_selftest()` při `fpga_freq_init` | výsledek `fpga_freq_phrec_selftest_ok()`; neblokuje SPI |

### 3.2 `fpga_phrec.c/.h` (nový, E1)

- Fázový bod = těžiště segmentu A okna; čas a index se sčítají **celočíselně** (dt v T/16384, ym v 1/16, xm v 1/256).
  Hraniční značka okna se v rozdílu těžišť vyruší.
- `f_cc` = kmitočet mezi těžišti sousedních oken; `gate_cc_ps` tak, aby `edge_count / gate_cc_ps = f_cc`.
  Součet přes okna pak teleskopuje → ADEV z bílého PM má sklon 1/τ.
- Kruh 2048 bodů (16 kB), `fpga_phrec_adev(m)`, `fpga_phrec_mdev(m)`.
- Úsek se přeruší: díra v SEQ, odmítnuté okno, okno bez regrese, změna hradla/kanálu/hodin.
- **Selftest přeložen a spuštěn (gcc):** prošel; ADEV/teorie 0,97 (m=1) a 1,00 (m=16); chyba f_cc 4,5·10⁻¹⁰ při σ 20 ps.

### 3.3 GPSDO (`freertos_task_fpga.c`, `gpsdo.h`, `meas_present.h`)

| slabina | řešení |
|---|---|
| `GPSDO_PM_NS = 30` (hypotéza) | (FLL zůstává na 30 ns, viz 7.1 K4) `gp_pm_update(e)`: σ_x = EWMA\|Δe\|·1,2533/√6 (Δe = druhá diference fáze), ořez 5σ, platí po 64 diferencích, meze 0,3–30 ns, start 2,9 ns. FLL (`gp_core_t.pm_ns`) i `mp_ref_ppb` (`mp_ref_in_t.pm_ns`) berou změřenou hodnotu; `gpsdo_pm_ns()`, `gpsdo_status_t.pm_ns` |
| historie `x_ns` int16 | `s_h_x` int32 **ps** (±2,1 ms); `gpsdo_hist_pt_t.x_ps` (nový kód) + `x_ns` (kompatibilita displej/web); `gp_ref_core` a doporučení τ počítají z ps; selftest upraven |

Syntaktická kontrola gcc: v upravených řádcích bez chyb (zbývající chyby jen z náhradních hlaviček v nezměněném kódu).

---

## 4. Ověření modely (Python, cyklově podle RTL)

| model | výsledek |
|---|---|
| `m_tdc_ld.py` (N1) | dnešní RTL: chybný vzorek 1,2 % (16,7 MHz), 44 % (18), **100 % (20)**, 20 % (22,5); oprava: **0** v 10–33 MHz |
| `m_win_count.py` (close+2) | dnešní RTL: chybná okna 6 % (34 MHz), 60 % (40), 96 % (49); oprava: **0** do 49 MHz |
| `m_coh_mon.py` | 0 falešných poplachů (fáze na přechodu v P i v N, jitter); 3/3 vložené chyby zachyceny |

---

## 5. Zadání pro Claude CLI

### 5.1 Simulace (sim/, Icarus) — nové testy, každý s NEGATIVNÍ kontrolou proti FW 0x0424

1. **`tb_n1.sv`**: `tdc_chan` se `want_seg = 1`, vstup 16,7 / 18 / 20 / 22,5 MHz, náhodná fáze. Referenční model vzorku
   (jako `m_tdc_ld.py`): kód každé značky = dekódovaný vzorek z jejího vlastního spuštění. 0x0424 musí selhat, 0x0425 projít.
2. **`tb_close2.sv`**: `win_recip`, hrany po 2,04 / 2,5 / 2,94 / 3,0 taktu, náhodná hradla; každé okno `snap` = referenční
   počet a `ccd = 0`.
3. **`tb_alias.sv`**: `base_win = 2`, okno bez hrany > 170 hradel → `r_dt_alias = 1`; 0x0424 (250) musí selhat.
4. **`tb_coh.sv`**: `coh_mon` s modelem hodin: fáze `tgl10` na náběžné / sestupné hraně, jitter; vložit chybějící a navíc
   pulz → přesně tolik chybných epoch; bez vložení 0.
5. **`tb_diag.sv`**: bublina (teploměr s mezerou > 8 tapů) → `r_bub`; CS během značek → `r_ovl`; `mask_cs` → žádná
   vnitřní značka při CS.
6. Celá stávající sada (`tb_regr_full`, `tb_top_regr`, `tb_pps`, `tb_tdc`, `txequiv` …) beze změny výsledků;
   `txequiv` doplnit o bajty 10/11/57–59; **regresní testy upravit na xm v 1/4096 hrany** (audit 4, A1).
7. **`tb_coh_ref.sv`**: `coh_mon` vypnutý při `ref_act = 1` a 168 ms po přepnutí reference (A3).

### 5.2 Sestavení a brána

- `build.tcl` (identita bitstreamu), `tools/fpga_release.py`: **CLS ≤ 85 %, TNS 0, skutečná rezerva setup ≥ 0,64 ns** (L-0141).
- Kontrola v reportu: PRIMARY ≤ 8, LW (dnes 8/8 — nové sítě nesmí žádat LW), `check_tdc_placement.py`.
- Varianty bitstreamu: (a) výchozí, (b) `define OSC_DIV 16` (pokus H8).

### 5.3 Deska (každý krok proti 0x0424 na stejném signálu, střídavé bloky)

| test | kritérium |
|---|---|
| 10 MHz vlastní reference 1 h | ADEV a zamítání regrese beze změny; `CITANI HRAN 0/0`; `coh_cnt = 0` |
| generátor 1–40 MHz (krok 1 MHz, střída 50 %) | žádné `FPGA_CHK_FMAX` pod 40 MHz; f správně; `ccd = 0` |
| 17–25 MHz s regresí | regrese přijatá; rozdíl f_regr − f_2pt bez systematiky |
| `mask_cs` on/off (SET_CONFIG 0x05), 2 × 30 min střídavě | podíl skoků, `ovl`; rozhodne hypotézu SPI |
| bubliny: externí GPSDO × vlastní reference | korelace `bub` se skoky → hypotéza vícenásobného překlopení |
| `OSC_DIV` 16 × 8 | šum dvoubodového okna, podíl skoků |
| `led_hb` on/off | totéž |

### 5.4 STM — dokončit integraci (není v balíčku)

- UART: `fpga diag [mask on|off] [led on|off]` → `fpga_freq_set_diag`; řádky `status`:
  koherence (`coh_st`, `coh_cnt`), bubliny/ovl posledního okna, `check_stats2`, `fpga_freq_regr_limit()`,
  `gpsdo_pm_ns()`, `fpga_phrec_info` + ADEV z fáze pro m = 4, 40, 400.
- Web `/api/fpga`: totéž.
- Emulátor (`sim_build_frame`): caps bit14 a nová pole (L-0012 — sourozenci).
- Displej/web: přejít z `x_ns` na `x_ps`.
- `FPGA_STATS_USE_CC = 1` až po E1 (offline porovnání ADEV z f_regr a z fáze na stejných datech).

### 5.5 Přijetí a návrat

- Přijato: všechny simulace PASS (včetně negativních kontrol), brána P&R, deska podle 5.3.
- Návrat: bitstream 0x0424 (`ab_test/`) + STM bez `FPGA_CHK_FMAX`/adaptivní meze (diff v `patches/`).

---

## 7. Kritická analýza 2 (2026-10-20, po implementaci)

### 7.1 Nalezené chyby v mém vlastním kódu — opraveno v balíčku

| # | problém | dopad | oprava |
|---|---|---|---|
| K1 | `FPGA_CHK_FMAX` odmítal i **emulátor** (`fpgasim on 1400000000` testuje formátování do GHz) | rozbité testy UI | kontrola jen mimo emulaci (`!s_sim_on`) |
| K2 | mez přímého měření jen pro **primární slot**; kanál B nad mezí by ukazoval špatný kmitočet bez příznaku | tichá chyba B | sekundární slot nad mezí → `freq16 = 0`, `edges_b = 0`, počítá se do `check_stats2` |
| K3 | adaptivní mez regrese se učila jen z PŘIJATÝCH oken → při vzrůstu šumu zdroje by odmítala vše a už se nezvětšila (**uzamčení**) | regrese trvale vypnutá | po 16 odmítnutích za sebou návrat na zahřívání (pevná mez 5·10⁻⁸); vlastní čítač `s_rg_adapt_n` |
| K4 | práh měření K ve FLL ze změřeného σ_x (2,9 ns) — model šumu okna FLL předpokládá **bílý PM**, GPS má na 16–256 s korelované putování | riziko falešných divergencí (STATUS #318) | FLL zůstává na 30 ns (`GPSDO_PM_NS_FLL`); změřené σ_x jen do prezentace a diagnostiky; přepínač `GPSDO_FLL_USE_MEASURED_PM` až po ověření na desce |

### 7.2 Na co jsme zapomněli (doplněno do TODO)

| # | co | kam |
|---|---|---|
| Z1 | `fpga_phrec` má 16 kB statických dat — ověřit v mapě linkeru, do které RAM padnou (DTCM 128 kB); případně atribut sekce do AXI SRAM | F6 |
| Z2 | STM může mít seznam známých `FW_VERSION`/`CAPS` (selftest, `status`) — doplnit 0x0425 / bit14 | F7 |
| Z3 | dokumentace projektu: `CLAUDE.md` (protokol, meze, 1PPS „~30 ns“ je vyvrácené), `TDC_MATEMATIKA.md` (N1, close+2, mez 40 MHz), `LESSONS.md` (nové lekce) | F8 |
| Z4 | přeprogramování generátoru Si5356 (CLK2–7) za běhu: pokud sekvence obsahuje soft reset, rozkmitá i CLK0/1 → hlídač koherence to správně odmítne, ale měření se přeruší | T11 |
| Z5 | `PULL_MODE=UP` na `ch_a`/`ch_b` (report P&R): proti 120 Ω zanedbatelné (4–18 mV), ale bez signálu drží vstup nahoře; zvážit `NONE` | T12 |
| Z6 | E0 stále hlásí SIGNAL_LOST pod ~0,4 Hz (hlídač FPGA 2,5 s); řeší až v3 (hlídač v STM podle očekávané periody) | F4 |
| Z7 | `txequiv` a `fpgaraw` dekódování musí znát bajty 10/11/57–59 | 5.1/5.4 |

---

## 8. Optimalizace 3 a kritická validace (testy) — 2026-10-20

### 8.1 Brána CLS ≤ 85 % — E0 musí přidané FF kompenzovat

E0 přidává odhadem ~180 FF a ~100 LUT (hlídač koherence, čítače, snímky, synchronizace). Při 85 % by to bránu překročilo
(snímek regrese 226 FF stál 6 % CLS). Proto v 0x0425 odchází, co STM nepoužívá:

| odstraněno | úspora | dopad na rámec / STM | návrat |
|---|---|---|---|
| `gate_div` (gate_ns, abs 28–35) + `h_gate` | ~110 FF, ~50 ALU | bajty 0; STM s caps bit5 gate_ns stejně přepočítává z dt (`parse_data`) | `define KEEP_GATE_DIV` |
| 64b čas 10 MHz → 32 b (abs 36–39; 40–43 = 0) | ~64 FF, 32 ALU | `timestamp_ticks` STM jen čte; přeteče po 429 s | `define TS10M_64` |
| ladicí bajty příjmu (abs 66, 67, 107, 112–117, 124, 125) včetně čítačů PHY | ~100 FF | bajty 0 (`fpgaraw` je ukáže jako nuly) | parametr `DBG_RX = 1` |
| `coh_mon`: odchylky 8b → 3b | ~10 FF | — | — |

Odhad: E0 celkem ≈ −100 FF a ±0 logiky proti 0x0424 → CLS ~84–85 %. **Rozhodne P&R.**

### 8.2 Výsledky testů (vše spustitelné, výstupy v `test/` a `model/`)

| test | co ověřuje | výsledek |
|---|---|---|
| `model/vlint.py` | strukturní lint: všechny instance proti portům modulů, nezapojené vstupy, duplicity, deklarace nových signálů před použitím | **0 chyb** (10 instancí) |
| `test/t_fpga` T1 | původní selftesty driveru (CRC, hystereze, okna, miscount, regrese, SEQ, rezerva TDC) po změnách | **prošly** |
| T2 | selftest fázového záznamu | **prošel** |
| T3 | adaptivní mez regrese: stabilní šum, odlehlé okno, skok šumu zdroje | **prošel po opravě** (viz 8.3) |
| T4 | mez přímého měření 39/45 MHz (0x0425), 35 MHz (0x0424), kanál B 45/20 MHz, nárůst `coh_cnt` | **prošel** |
| `test/t_gpsdo` T5 | odhad σ_x 1PPS (0,4 / 2 / 5 ns, s kroky DAC a 1 % odlehlých) | **prošel**: poměr 1,06 / 1,01 / 1,09 (po zúžení ořezu, viz 8.3) |
| T6 | `gp_ref_core` int16 ns (stará) × int32 ps (nová) na stejných datech | **shoda na 1e-12**; nová rozliší fázi ±0,3 ns (stará 0) |
| `fpga_range` selftest | automatický rozsah 0,1 Hz – 420 MHz nahoru i dolů, bez kmitání | **prošel** (200 MHz → P 8, 400 MHz → P 16) |
| `model/m_extra.py` A | oprava N1 při přepínání spouštění (maskování CS) | **0/150 963** chybných vzorků |
| B | maskování CS (2 % segmentu ve STÁLE stejné fázi, nejhorší případ) nevychýlí sklon | vychýlení pod 0,2 σ jednoho okna (~0) |
| C | `coh_mon`: zákmit 10 MHz, fázový skok 6 / 3 ns | zákmit zachycen; **skok 6 ns NEzachycen** (viz 8.3) |

### 8.3 Co testy odhalily — opraveno nebo opraveno v tvrzení

| # | nález | oprava |
|---|---|---|
| V1 | **adaptivní mez se učila jen z přijatých oken** → useknuté rozdělení, σ podhodnocená; po skoku šumu zdroje trvale ~15 % oken odmítnuto (test T3: 171/200) | σ se učí ze **všech** oken s platnou regresí (|d| ořezané na 5·10⁻⁸), rozhoduje mez spočtená před započtením okna → 200/200 |
| V2 | odhad σ_x 1PPS s ořezem 5σ nadhodnocoval o 10–16 % (kroky DAC, odlehlé hrany) | ořez 3σ → 1–9 % (konzervativní směr) |
| V3 | tvrzení „`coh_mon` pozná fázový skok > ~5 ns“ **neplatí**: pravidlo „obě cesty“ (ochrana proti metastabilitě) zachytí skok spolehlivě až od 10 ns; 5–10 ns jen když překročí vzorkovací okamžik obou cest | tvrzení opraveno (kód i dokumenty). Cílem jsou chybějící/navíc pulzy (= 10 ns), ty zachytí vždy. Citlivější pravidlo („odchylka na dosud čisté cestě“) by při pomalém teplotním posunu fáze dávalo falešné poplachy — nezavádět |

### 8.4 Co testy NEpokrývají (zůstává pro CLI a desku)

- Skutečné HDL chování (Gowin syntéza, `initial`, šířky, časování) — jen simulace a P&R.
- Integraci STM v cílovém prostředí (FreeRTOS, mapa paměti F6, UART/web výpis).
- Fyziku: O4, strop pinu, rušiče modulu.

---

## 9. Audit 4 a kritická validace (2026-10-20)

### 9.1 Nálezy a opravy

| # | nález | jak odhalen | oprava |
|---|---|---|---|
| A1 | **xm s rozlišením 1/256 hrany omezuje fázový záznam i regresi nad ~16 MHz** (nepravidelné značkování → zlomek těžiště): při 21 MHz ADEV(1 s) z fáze 8,7·10⁻¹¹ místo ~6·10⁻¹² | test T9 (`t_phrec2`) | FPGA dělí s **1/4096 hrany** (`dvx = {8'd0, sx, 12'd0}`, `xm = x0·4096 + xq`; šířky beze změny, stejný počet kroků děličky, xm ≤ 2³⁸ v 40b poli). **CAPS bit15** = `FPGA_CAP_XM12`; STM `fpga_freq_regr_hz2(…, xdiv)` a `fpga_phrec` berou dělitel podle caps. Výsledek T9: 5,8·10⁻¹² (15× lépe) |
| A2 | `gate_cc_ps` zaokrouhlené na ps v každém okně → v Σ náhodná procházka 0,29 ps·√m (po 20 000 oknech ~41 ps = nad cílovou ADEV z fáze na dlouhých τ) | test T8 | přenos zbytku zaokrouhlení do dalšího okna → Σ odchylka −0,17 ps po 20 000 oknech |
| A3 | `coh_mon` nemá smysl při **externí referenci**: REF_10MHz (PIN42) je vždy surový OCXO, 100 MHz se zamkne na externí vstup → hodiny nekoherentní | revize HW (`HW_REFERENCE.md`, U19) + model `m_audit4` (1) | hlídač běží jen při OCXO referenci a až ~168 ms po změně reference (přechod Si5356); STM chybu koherence při ext. referenci ignoruje. Model ukázal, že při běžném rozdílu kmitočtů by poplach stejně nevznikl (skluzy vidí každá cesta jindy) — vypnutí je hlavně správná sémantika a ochrana při přeladění |
| A4 | `OSC_TMO` by při `OSC_DIV < 8` přetekl 8 b | revize | pojistka (max 255) |

### 9.2 Ověřená tvrzení (nová)

| test | výsledek |
|---|---|
| Grayův čítač chybných epoch: 1 bit na krok, zpětný převod 0–255 | OK |
| přiřazení bublin a značek při CS ke správnému oknu (uzavírací značka přijde až po uzavření), 2000 náhodných běhů | OK |
| celá sada znovu: lint 0 chyb, T1–T4, T5–T6, T8–T9, `fpga_range`, `fpga_phrec`, modely N1 / close+2 / coh / extra | **vše prošlo** |

### 9.3 Omezení, která audit potvrdil (nejde opravit v E0)

- **Šířka impulzu v přímém režimu:** vysoká i nízká úroveň musí mít ≥ 10,5 ns na **jakémkoli** kmitočtu (např. 25 MHz se
  střídou 20 % = 8 ns NEsplňuje). E0 porušení nepozná spolehlivě (jen nepřímo přes `ccd`/bubliny). Řeší v3: P ≥ 2 pro
  úzké impulzy (první KO překlopí každou náběžnou hranu).
- **Hlídač koherence** nepozná malou chybu kmitočtu 100 MHz (to hlídá 640 ± 4 = ±0,6 % a STM přes stav Si5356).
- **Rozlišení xm 1/4096** dává při 40 MHz ~1,4 ps rms na těžiště — pod šumem těžiště; přesné až v3 (surové součty).

---

## 10. Optimalizace 5 (2026-10-20)

| změna | soubor | přínos | ověřeno |
|---|---|---|---|
| CRC přes půlbajtovou tabulku; bitová verze zůstává jako reference, selftest je porovná na 64 pseudonáhodných rámcích 1–1024 B | `fpga_freq.c` | 1,6× (PC), odhad 2–3× (CM7) | T1 prošel; „123456789“ = 0x29B1 |
| kruh fázového záznamu `float` | `fpga_phrec.c/.h` | −8 kB RAM | selftest, T8, T9 prošly beze změny výsledků |
| volba `define SHARE_TR`: tick − 1/− 2 sdílené oběma kanály (`tdc_chan.EXT_TR`) | `tdc.v`, `top.v` | −68 FF | lint 0 chyb; **P&R musí ověřit časování** (registry budí oba řádky TDC) |

Postup pro CLI: nejdřív výchozí build; pokud CLS > 85 %, build s `SHARE_TR`; teprve potom ostatní volby (`DBG_RX`, `KEEP_GATE_DIV`, `TS10M_64` vrací původní stav).

---

## 6. TODO

### HW (až při práci na hardwaru)

| # | úkol | rozhodne |
|---|---|---|
| T1 | **Pin 86 (BL_PWM → povolení LP3320 na modulu)**: změřit napětí na C25 modulu (2,2 µF/50 V). > 5 V = měnič podsvitu běží naprázdno → build s `define PIN86_BL_OFF` a do `pins.cst`: `IO_LOC "bl_pwm" 86; IO_PORT "bl_pwm" IO_TYPE=LVCMOS33 DRIVE=4;` | multimetr |
| T2 | Osciloskop na VCCO2 (3,3 V z TMI7003C, 1,5 MHz / PFM) a +1V2 (za LDO ME6211) modulu | zvlnění |
| T3 | Pin 25: strmost a zákmity vstupu (dnes HC04 do 120 Ω, ~1 V/ns) | H1 |
| T4 | Měření s odpojeným USB (BL702) | rušení |
| T5 | Síla/strmost výstupu Si5356 pro hodiny FPGA — POZOR na kompromis (pomalejší hrana = víc jitteru); správně sériová terminace u zdroje | odraz na odbočce pinu 35 → J4 |
| T6 | Atribut `HYSTERESIS` na CH_A/B (200/400 mV) — pokusný bitstream | vícenásobné překlopení |
| T7 | Oscilátor 27 MHz (U16) — případně odpájet | spektrum chyby značky |
| T8 | **400 MHz**: LVCMOS33 to pravděpodobně nedá (Gowin Fmax 150/300 MHz, výstupní údaj). Cesty: diferenciální vstup LVDS25 na párech 25/26, 27/28 (vstupní napětí jen 0–2,15 V!) nebo dělička na vstupní desce | H2 / vstupní deska |
| T9 | Tepelná stabilizace modulu (H4) | drift tabulky |
| T10 | Rychlý vstupní budič u pinu (H1) | O4 |
| T11 | Ověřit, zda změna generátoru Si5356 (CLK2–7) ruší CLK0/1 (hlídač koherence, `coh_cnt`) | sekvence I2C |
| T12 | `PULL_MODE=NONE` na CH_A/CH_B (pokusný bitstream, s T6) | pins.cst |

### FW / data

| # | úkol |
|---|---|
| F1 | dodat `tdc hist` A/B za tepla a za studena (Z10) |
| F2 | dodat kód ADEV/MDEV na STM (integrace E1) |
| F3 | E1: záznam všech oken (periods, dt, n, xm, ym) → offline ADEV dnes × z fáze → rozhodnout `FPGA_STATS_USE_CC` |
| F4 | v3 podle `2026-10-20_v3_navrh_a_analyzy.md` (E2–E6) |
| F5 | zkušební P&R `ides_pps.v` (IDES8 na IOB23A, HCLK, rPLL z GCLK) — hodina práce, rozhodne E4 |
| F6 | mapa linkeru: kam padne 16 kB `fpga_phrec` (DTCM?) |
| F7 | STM: seznamy známých FW_VERSION / CAPS doplnit o 0x0425 / bit14 |
| F8 | dokumentace projektu (CLAUDE.md, TDC_MATEMATIKA.md, LESSONS.md) podle E0 |
| F9 | po 1 týdnu dat na desce: rozhodnout `GPSDO_FLL_USE_MEASURED_PM` (šum okna FLL změřit, ne modelovat) |
| F10 | HW CRC a SPI DMA: CubeMX, linker, callbacky, testy D1–D8 (`2026-10-20_CRC_DMA_zadani_CLI.md`) |
| F11 | úklid mrtvých modulů `phase_oversampler`, `phase_check`, `tdc_vernier_4phase.v`, `coarse_counter.v` → `legacy/` (tamtéž, kap. 5) |
