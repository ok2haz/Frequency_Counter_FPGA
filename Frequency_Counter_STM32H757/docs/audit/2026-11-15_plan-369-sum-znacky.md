# Plán #369: nižší šum značky TDC bez rozbití intervalu A→B (2026-11-15)

Fáze: PLÁN, priorita zvýšena uživatelem 2026-11-15. Kód FPGA se podle tohoto plánu zatím nemění (krok 0 = #382 je
samostatná změna FW 0x050B). Co nejde doložit z kódu nebo měření, je označeno HYPOTÉZA.

## 1. Výchozí stav (doloženo)

| co | hodnota | zdroj |
|---|---|---|
| bitstream ve flash FPGA | zkušební `TEST_FW_0x050A_v3_appOscS6` (APP_OSC) | `docs/MERENI_A_KALIBRACE.md` t384, FPGA DIAG „koherence neplatné“ |
| doména aplikace / SPI s APP_OSC | vnitřní oscilátor ~10,4 MHz, nesoudělný se 100 MHz | `Frequency_Counter_FPGA_Module/src/top_v3.v:44-64` |
| doména aplikace bez APP_OSC | pin REF_10MHz (`clk_ref_10m`), koherentní se 100 MHz, v SDC asynchronní, obecné vedení ~2,2 ns | F-0289 (`docs/audit/2026-11-14_fpga-bitstream-a-rizeni-ocxo.md`) |
| šum značky, stojící fáze | APP_OSC 73–107 ps, bez APP_OSC 30–48 ps (3×) | t384 |
| šum značky, točící se fáze | APP_OSC 126 ps, bez APP_OSC 115 ps (−9 %, vládne e(φ)) | t384 |
| interval A→B bez APP_OSC | **rozbitý**: σ 3,7–56 ns, skoky o ~10 period během SPI/CS, u obou kladných seedů | t386/t387 |
| interval A→B s APP_OSC | σ 98–186 ps podle odstupu A–B | t381 |
| časování bez APP_OSC | 3/8 seedů záporných, nejlepší +0,141 ns; kritické cesty kalibrační tabulka TDC (`u_tdca/lut_lut_0_0_s`) a FIFO okna B (`u_sb/fh`) | t386/t387, `appOsc-tradeoff` |
| přeslech z kanálu B | ~260 ps rms na značkách A i při P 16, B vypnut → 123 ps | t390 |
| chyba podle fáze e(φ) | 300–650 ps š–š, společná hrana taktu ~80 ps | t374–t376 |

## 2. Příčina rozbitého A→B bez APP_OSC

HYPOTÉZA: hodiny 10 a 100 MHz jsou fyzicky koherentní se stálou fází, ale křížení domén je navržené pro asynchronní
hodiny (2 FF, Gray, přepínací bit). Padne-li hrana 10 MHz do okna nastavení/přidržení synchronizátoru 100 MHz, chyba
nenastane náhodně jednou za čas, ale **pokaždé ve stejném místě** (fáze neujíždí) → vadný přenos stavu během SPI/CS a
skoky o celé periody. S APP_OSC fáze ujíždí, chyby jsou vzácné a náhodné. Podporuje to F-0289 (fáze domény aplikace
vůči 100 MHz není řízená) a L-0272 (těsná rezerva < ~0,3 ns = občasná funkční chyba na křemíku).

Ověření (bez desky): STA s `create_generated_clock` 10 MHz odvozeným od 100 MHz ukáže skutečné cesty mezi doménami;
simulace `tb_top_v3` s pevnou fází 10/100 MHz (sweep 0–10 ns) má skoky A→B reprodukovat v úzkém okně fáze.

## 3. Cíl

Bitstream, kde doména aplikace/SPI běží **synchronně ze 100 MHz** (povolení hodin, clock enable), bez vnitřního
oscilátoru a bez asynchronního křížení domén, s kritérii:

- interval A→B: σ ≤ dnešních 98–186 ps, bez skoků o periody (t381 mapa 0–100 ns),
- šum značky při stojící fázi ≤ 50 ps (t384), při točící se fázi s vypnutým B ≤ dnešních ~110–120 ps (t390),
- časování: ≥ 7 z 8 seedů s rezervou ≥ 0,3 ns (L-0272), CLS ≤ 85 % (brána `fpga_release.py`),
- 0 chyb SPI/CRC za 8 h, koherence 100/10 MHz platná.

## 4. Kroky

0. ✅ **Vypnutí nepoužitého kanálu (#382, FW 0x050B + STM v0.45.20):** pin_cnt3 `sel` 7 = řetěz TDC bez hran,
   STM ho posílá nemeřenému kanálu při jednokanálovém měření. ⬜ sestavení bitstreamu a ověření na desce.
1. **Analýza bez změny RTL** (~1 den):
   a. registry a LUT podle hodinové domény v APP_OSC a non-APP_OSC sestavení (`tools/syn_hier.py`, `sim/res_breakdown.py`);
   b. seznam křížení `clk_app` ↔ `clk_p0_100m` v `top_v3.v` (`ring_raddr`, `raw_k/q`, `csg_s*`, `fe_s`, `bd_s`, `ra_s/rp_s/rl_s`,
      SPI PHY `spi_slave_phy_v3`) a jejich typ (2 FF / Gray / přepínací bit);
   c. STA non-APP_OSC s generovanými hodinami 10 MHz (krok 2 výše);
   d. simulace s pevnou fází 10/100 MHz: najít okno fáze, kde se A→B rozbije.
2. **Doména aplikace ze 100 MHz s clock enable** (návrh F-0289): `clk_app` = `clk_p0_100m`, `app_ce` 1/10; registry
   aplikace s CE, `set_multicycle_path` 10/9 pro cesty CE → CE; křížení domén se stane synchronním (synchronizátory
   lze ponechat jako zpoždění, nebo odstranit). SPI PHY zůstává na SCK. Dvě varianty povolení:
   - A: pevná fáze CE → aktivita aplikace v pevné fázi 100 ns → systematická chyba podle fáze (měřitelná, korigovatelná),
   - B: rozprostřená fáze CE (pseudonáhodně) → šum místo systematiky.
   Rozhodne měření v kroku 5.
3. **Uvolnění časování 100 MHz** (souběžně s 2): F-0286 `ld` rozložit na kopie po ~32 tapech a `clear_*` na lokální
   registry; kalibrační tabulka TDC a FIFO okna B dostanou registr navíc (kritické cesty t386/t387).
4. **Sestavení a brána:** `python tools/pnr_seeds.py run <var> 8` + `report`, přijmout jen ≥ 7/8 seedů s rezervou ≥ 0,3 ns;
   vydání `tools/rel_abl.py`, záznam do `ab_test/MANIFEST.md`.
5. **Ověření na desce v SRAM (ne flash):** t384 (stojící fáze), t390 (točící se fáze, B vypnut i zapnut), t381 (mapa σ A→B
   0–100 ns), `spi err`, `fpga diag` (koherence), noc 8 h. Srovnání s APP_OSC se stejnými kabely a stejnou teplotou.
6. **Flash FPGA** až po souhlasu uživatele.

## 5. Rizika

- CLS: CE a multicycle nemusí uvolnit dost logiky; non-APP_OSC se zavíral hůř než APP_OSC.
- Varianta A přenese aktivitu aplikace do e(φ) jako složku s periodou 100 ns (#347).
- Změna rozmístění kolem řetězců TDC může zhoršit přeslech (#367: řetězce A a B jsou v sousedních řadách R26/R27).

## 6. Souvislosti

#347, #362, #367, #382, F-0286, F-0287, F-0288, F-0289, L-0252, L-0272; měření t374–t387, t390.
