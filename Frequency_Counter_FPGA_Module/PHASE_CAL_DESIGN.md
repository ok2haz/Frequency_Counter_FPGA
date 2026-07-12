# Návrh: kalibrace fází TDC a rozvoj měřicích metod

Váže se na `BOARD_V20_CHECKLIST.md` (piny, SDC, Si5356). Ověřeno proti Gowin knihovnám
(`IDE/bin/prim_syn.v`, `IDE/data/hardware_core/gw1n/prim_syn.v`) a PnR reportu GW1NR-9C.

## 0. TL;DR — odpověď na otázku „zpožďovačky v jednom řádku/sloupci?"

**Pro kalibraci fází NEstavět zpožďovací linku ve fabricu vůbec.** GW1N má v každém IOB
hotovou primitivu **IODELAY** (128 tapů à ~25–30 ps, rozsah ~3,2–3,8 ns, dynamicky laditelná
porty SDTAP/SETN/VALUE) — je monotónní z konstrukce křemíku, takže otázka umístění do
řádku/sloupce odpadá. Fabric LUT řetěz je pro posun hodin nevhodný i v jednom sloupci:
routing dominuje nad LUT zpožděním (krok nemonotónní) a PVT drift ±20 % by kalibraci
průběžně rozlaďoval.

Jeden sloupec dává smysl až u **carry-chain TDC interpolátoru** (budoucí stupeň 3) — tam se
používá ALU carry řetěz, který na Gowinu přirozeně běží vertikálně jedním sloupcem CLS.

**Hlavní doporučení: dvoustupňové řešení.** Nejdřív numerická kalibrace (LUT korekce z
histogramu hustoty kódů — žádný zásah do hodin, statistická přesnost jednotky ps), IODELAY
deskew až jako stupeň 2, pokud se ukáže skew > ~0,5 ns nebo bude potřeba fyzicky uniformní
mřížka binů.

## 0b. Teplotní (ne)závislost — finální řešení [IMPLEMENTOVÁNO FW 0x0200]

Na dotaz „lepší a méně teplotně závislý způsob": ano — místo kompenzace delay
linkami se měření udělalo na zpožděních NEZÁVISLÉ:

1. **Λ/Ω regresní čítač (implementováno)**: přes VŠECHNY hrany okna se akumuluje
   S1=Σt, S2=ΣS1 (`win_recip`, pipeline 34b sub → 56b add → 40+40b add s carry
   registrem); STM spočte LSQ slope. Kvantizace i nelinearita binů (přesně to,
   co teplota mění) se průměruje ~1/√N → při N=10⁶ příspěvek FPGA ~1×10⁻¹¹,
   limitem je GPSDO, ne elektronika. Drift binů o desítky ps je pod rozlišením.
2. **Background tracking (implementováno)**: histogram fine kódů běží trvale na
   měřeném signálu (CAL report TYPE 0xA0) → hranice binů se stopují živě, žádná
   jednorázová kalibrace nestárne. RO (SET_CONFIG 0x02) jen když signál chybí.
3. **IODELAY deskew (stupeň 2 níže) se RUŠÍ z doporučení** — tapy jsou samy PVT
   závislé (přesně ten problém). Nouzovka, jen kdyby bring-up ukázal skew
   srovnatelný s binem; pak ale spíš rovnou bod 4.
4. **Výhled: IDES4/IDES8 front-end** (primitivy pro GW1N ověřeny v Gowin
   knihovně): vzorkování jedním 200MHz DDR clockem na GCLKT_4 → 4 vzorky/10 ns
   z JEDNÉ hodinové cesty; skew čtyř stromů zmizí, teplota = common-mode,
   zbývá jen duty-cycle FCLK (malý, stabilní, kryje ho bod 2). Uvolní piny
   34/40/33. Kandidát na FW 0x03xx po oživení desky v2.0.

Stav implementace stupně 0 (FW 0x0200, protokol v2 dle
`FPGA_PROTOCOL_V2_NAVRH.md`): histogram 4×24b, fine_first/last, ring oscilátor
+ mux (SET_CONFIG 0x02), Λ akumulátory, CAL report **TYPE 0xA0** (ne 0x81 z
původního návrhu níže), DATA rezerva 100..117 = fine+S1+S2 (caps bit4).

## 1. Měřicí metody — přehled a rozpočet chyb

| Metoda | Kvantizace Δt | σ(f)/f @ 0,25 s okno | Stav |
|---|---|---|---|
| 4fázové reciproké čítání (dnes) | 2,5 ns (bin) | ~1,4×10⁻⁸ | ✅ běží (top.v) |
| + numerická kalibrace binů (LUT) | 2,5 ns, bez systematiky | ~1×10⁻⁸, průměrovatelné √N | **stupeň 1 — navrženo zde** |
| + IODELAY deskew | biny fyzicky 2,5 ns | jako výše, čistší | stupeň 2 (volitelný) |
| + carry-chain interpolátor | ~50–100 ps | ~3–6×10⁻¹⁰ | stupeň 3 (po oživení v2.0) |
| vernier TDC (tdc_vernier_4phase.v) | — | — | ❌ ZRUŠIT: potřebuje 2. kmitočet (U7 v2.0 odstraněn), rPLL jitter ~100+ ps ho degraduje |
| GPS timestamp (pin 48) | 10 ns abs. | vazba na UTC, Allan OCXO | roadmap (checklist §3) |

Chyba binu se do kmitočtu propaguje jen přes PRVNÍ a POSLEDNÍ hranu okna: Δt_err ≤ 2×(chyba
hranice binu). Po kalibraci (σ_b ≈ jednotky ps) je reziduum ~1×10⁻¹¹ — hluboko pod GPSDO.

## 2. Stupeň 0 — sběr dat pro kalibraci (malé RTL, udělat vždy)

### 2.1 Histogram jemných kódů (nahrazuje fine_seen)
```verilog
// v top.v, doména clk_p0_100m; nahrazuje fine_seen4 (fine_seen = |hist[i])
reg [23:0] hist [0:3];        // hrany pin28 podle fine kódu
reg [23:0] hist_lat [0:3];
always @(posedge clk_p0_100m) begin
    if (gate_tick) begin
        hist_lat[0]<=hist[0]; hist_lat[1]<=hist[1];
        hist_lat[2]<=hist[2]; hist_lat[3]<=hist[3];
        hist[0]<=0; hist[1]<=0; hist[2]<=0; hist[3]<=0;
    end else if (rise4)
        hist[fine4] <= hist[fine4] + 24'd1;
end
```
Náklad: ~100 FF + dekodér (FPGA má 54 % FF volných). CDC do 10 MHz: stávající ph_tgl
handshake (latch je stabilní celé okno).

### 2.2 fine_first / fine_last okna (pro korekci Δt v STM)
`win_recip` už drží `ref_ts` (první hrana) a při výsledku zná poslední `ev_ts` — vyvést 2+2 bity:
```verilog
output reg [1:0] r_fine_first,   // = ref_ts[1:0] při res_tgl
output reg [1:0] r_fine_last     // = ev_ts[1:0]  při res_tgl
```
Do rámce: byte 60 = {2'b0, fine_last, 2'b0, fine_first} (dnes volný).

### 2.3 Vestavěný asynchronní zdroj — ring oscilátor
Hustota kódů vyžaduje zdroj nesouvislý se 100 MHz. Nezávislý na vstupním signálu → RO:
```verilog
module ring_osc (input wire en, output wire out);   // ~15-25 MHz po /2
    (* keep = "true" *) wire [12:0] n;
    assign n[0] = en & ~n[12];                       // NAND enable
    genvar i;
    generate for (i=1;i<13;i=i+1) begin : g
        (* keep = "true" *) LUT1 #(.INIT(2'b01)) u (.I0(n[i-1]), .F(n[i]));
    end endgenerate
    reg d2 = 0; always @(posedge n[12]) d2 <= ~d2;   // /2: 50% duty, < 40 MHz strop debounce
    assign out = d2;
endmodule
```
Mux před `u_os4`: `sig_cal = cal_mode ? ro_out : sig_in4;` — kalibrace při startu / při
ztrátě signálu (řídí STM povelem); `u_os16` běží dál. Skutečný kmitočet RO je jedno,
důležitá je nesoudělnost (LUT RO ji zaručí).

### 2.4 Rozšíření SPI protokolu (spi_app.v, VERSION → 0x02)
| TYPE | Směr | Význam |
|---|---|---|
| 0x0A GET_CAL | STM→FPGA | příští TX rámec bude 0x81 |
| 0x0B CAL_START / 0x0C CAL_STOP | STM→FPGA | mux RO místo pin28 |
| 0x0D TAP_SET | STM→FPGA | payload: phase_id(1B), op(1B: set/inc/dec), value(1B) — stupeň 2 |
| 0x81 CAL_DATA | FPGA→STM | hist_lat[0..3] 4×3B, fine_first/last, tap[3×1B], cal_mode, RO čítač 3B |

## 3. Stupeň 1 — numerická kalibrace (STM32, žádný zásah do hodin)

1. STM: CAL_START → sbírá K oken CAL_DATA → N = Σ hist ≥ 10⁶ hran (RO ~20 MHz → 1 okno
   0,25 s dá 5×10⁶; stačí JEDNO okno, σ_b = 10 ns·√(p(1−p)/N) ≈ **2 ps**).
2. Šířky binů: p_k = hist_k/N. Hranice (vůči vzorkovací hraně p0): b₁ = 10ns·p₁,
   b₂ = 10ns·(p₁+p₂), b₃ = 10ns·(p₁+p₂+p₃). Nominál 2,5/5,0/7,5 ns; odchylka = skew fáze.
3. Korekční LUT: c_k = střed binu k vůči hraně p0 (na Δt působí jen rozdíly, konstanta se ruší).
4. Korekce měření: FPGA dál počítá s mřížkou 2,5 ns; STM zpřesní
   `gate_ns_corr = gate_ns + (c[fine_last] − c[fine_first])` a `f = periods·PRESC/gate_ns_corr`.
   (edge_count i gate_ns už v rámci jsou — chybí jen fine kódy = §2.2.)
5. Re-kalibrace: při startu, při ztrátě signálu, při ΔT > ~5 °C na TMP117 (fabric insertion
   drift ~desítky ps/10 °C).

Výhody: nulové riziko (hodiny nedotčené), plná pravda end-to-end (měří skutečné vzorkovací
okamžiky FF přes reálné clock stromy), jednotky ps přesnost.

## 4. Stupeň 2 — IODELAY deskew (volitelný, po vyhodnocení stupně 1)

Vložit mezi IBUF a fabric u fázových vstupů **34/40/33** (pin 35 = GCLKT_4 zůstává bez
zpoždění jako reference):

```verilog
module phase_deskew (
    input  wire clk_in,          // z IBUF pinu fáze
    input  wire ctl_clk,         // clk_ref_10m (SPI povely)
    input  wire tap_pulse,       // 1 takt na TAP_SET inc/dec
    input  wire tap_dir,         // SETN
    output wire clk_dly,
    output wire at_limit         // DF
);
    // GW1N-9C: primitiva IODELAY (příp. IODELAYA/C - rozhodne syntéza, řízení shodné)
    IODELAY #(.C_STATIC_DLY(0)) u_dly (
        .DI(clk_in), .DO(clk_dly),
        .SDTAP(1'b1),            // dynamický režim
        .SETN(tap_dir), .VALUE(tap_pulse), .DF(at_limit)
    );
endmodule
```
- Krok ~25–30 ps, 128 tapů → rozsah ~3,2–3,8 ns > celý bin ✓ (hodnotu tapu ověřit v DS117
  a přeměřit histogramem — tapy jsou samy o sobě PVT závislé, proto smyčka).
- Regulační smyčka: histogram (§3, pravda) → STM PI krok → TAP_SET; cíl b_k = 2,5/5,0/7,5 ns.
  Konverguje za jednotky iterací; poté LUT korekce ≈ 0 (nechat zapnutou — kryje reziduum).
- **Kázeň při přepínání tapů**: změna zpoždění na běžících hodinách = riziko glitche →
  tapovat jen v cal módu, výsledkové okno po změně zahodit.
- STA: IODELAY nemění SDC (waveform posuny z checklistu platí); runtime posun řeší kalibrace.

## 5. Stupeň 3 — carry-chain TDC interpolátor (budoucnost; tady platí „jeden sloupec")

Cíl ~50–100 ps single-shot → 3–6×10⁻¹⁰ za 0,25 s okno:
- sig_in → vstup carry řetězu (ALU/CLS), 64–128 elementů; zachytávat FF bankou v clk_p0.
- **Placement: jeden souvislý carry řetěz = přirozeně jeden sloupec CLS** (Gowin řetězí
  vertikálně). Zapsat jako jediné sčítání `{co, taps} = {1'b0, sig_rep} + konstanta` nebo
  explicitní ALU primitivy; LOC jen na první buňku; vstup z pinu co nejkratší.
- Krok elementu ~15–40 ps (změřit), teploměrový kód s bublinami → bubble-tolerant dekodér
  (počet jedniček, ne pozice první), kalibrace hustotou kódů úplně stejně jako §3 (týž RO!).
- Až po oživení desky v2.0 a stupňů 0/1 — infrastruktura (RO, histogram, CAL rámec) se
  beze změny znovu použije, jen s více biny.

## 6. Optimalizace stávajícího kódu

1. `fine_seen4` → nahradit histogramem §2.1 (víc informace, −8 FF logiky navíc netřeba;
   `phase_status` bajt zachovat: fine_seen_k = |hist_lat[k]|≠0).
2. `win_recip`: + `r_fine_first/last` (§2.2) — 4 FF.
3. `phase_oversampler`: beze změny — dekodér first_high i debounce jsou korektní; strop
   40 MHz na pinu je dokumentovaný a s preskalerem /4 stačí (160 MHz vstup).
4. `recip_calc`/`divu_seq`: beze změny (80 taktů @10 MHz = 8 µs, bez vlivu).
5. `spi_slave_phy`: až se přidá flash-bridge (checklist §6), přepnout `.clk` na `clk_p0_100m`
   (SCK do ~20 MHz, bulk 700 kB za ~0,3 s) — vyžaduje CDC rámců 100M↔10M (toggle handshake
   už existuje, rozšířit na oba směry).
6. `tdc_vernier_4phase.v` + `coarse_counter.v`: nejsou v .gprj a metoda se ruší (U7 pryč) —
   přesunout do `attic/` nebo smazat, testbench `tb_recip_calc`/`tb_phase_oversampler` zůstává.

## 7. Postup nasazení a verifikace

1. **RTL stupeň 0**: histogram + fine_first/last + RO + mux + TYPE 0x0A/0x0B/0x0C/0x81.
   Sim: rozšířit `tb_phase_oversampler` o kontrolu histogramu proti stimulu se známým
   rozložením hran; nový `tb_cal_frame` (CRC + layout 0x81).
2. **STM**: výpočet b_k + LUT korekce + zobrazení šířek binů v ps (System Health / konzole).
3. **Bring-up v2.0**: změřit skutečný skew (očekávám 100–500 ps na fabricových fázích 34/40/33
   vůči GCLK 35) → rozhodnout stupeň 2.
4. **Stupeň 2** jen při skew > ~0,5 ns: 3× phase_deskew + TAP_SET + regulační smyčka.
5. Dlouhodobě: re-kalibrace navázat na TMP117 (ΔT trigger) a logovat b_k do blob store
   (trend = diagnostika stárnutí/teploty).
