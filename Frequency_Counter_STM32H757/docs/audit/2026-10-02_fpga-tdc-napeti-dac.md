# Audit: dnešní změny — FPGA TDC/`win_recip`/`spi_slave_phy`, napěťové kanály ADS1115, driver AD5693R  (2026-10-02)

- **Commit:** `HEAD a6db669` (oprava napěťových kanálů) **+ necommitnutý pracovní strom**:
  `Frequency_Counter_FPGA_Module/src/{top.v, spi_app.v, spi_slave_phy.v}`,
  `CM7/Core/Inc/ad5693.h`, `CM7/Core/Src/freertos_task_sensors.c`, `CM7/Core/Src/freertos_task_uart.c`
- **Jádro / doména:** FPGA GW1NR-9 (`clk_p0_100m`, `clk_ref_10m`); CM7 (SensorsTask, UartTask);
  CM4 (web SPA + SCPI/TCP — jen dopad sdílených dat)
- **HAL:** STM32H7 HAL 1.11.6 (`stm32h7xx_hal.c:52-54`)
- **Projité sekce checklistu:** A (napájení — VBAT), B (dual-core: sdílený `scpi.c`/`calib.h` na CM4,
  data ze snapshotu), D (souběh: žádost/odpověď `g_dac`), E (chybové kódy I2C), F (konfigurace
  projektu `.cproject`), G (I2C1, ADC3). **FPGA část je mimo STM32 checklist** — prošla ručním
  rozborem RTL, **syntetizovaným netlistem** (`impl/gwsynthesis/Counter_FPGA.vg`) a timing reportem.
- **Neprojité (a proč):** C (DMA/cache — změny se jí nedotýkají), H (errata — kromě rozdílu
  hlaviček H747/H757 v F-0214)

## Souhrn

Audit dnešní práce, ne modulu od nuly. **Nejzávažnější zjištění: carry-chain TDC v bitstreamu
neexistuje.** Syntéza zredukovala `{0,{15{sig}}}+1` na invertor a jeden klopný obvod (v netlistu
`carry_tdc` není jediná ALU buňka), `fine` je trvale 1 a událost se navíc spouští na **sestupné**
hraně. Měřený kmitočet je přesto správný — číselně stejný jako u nahrazeného `coarse_edge_detect`
(hrubé rozlišení 10 ns) — ale komentáře, mapa protokolu a CONST tvrdí rozlišení 625 ps, které
neexistuje a s 15 tapy existovat nemůže (F-0203). Kontrakt FPGA→STM (`gate_time_ns`) dnes vychází
přesně **jen díky tomu, že TDC nefunguje** (F-0204). Opravy časování (`win_recip`, `spi_slave_phy`)
jsou správné. Oprava napětí (a6db669) funguje na CM7, ale **web a SCPI dál hlásí AIN2 jako 12 V**
(F-0209). Mimo dnešní kód: **projekt se od 2026-09-28 překládá jako STM32H747** (F-0214).

**Verdikt po oblastech:**
- FPGA měření kmitočtu: **funkční** (hrubě, 10 ns) · carry-chain TDC: **nefunkční** (není v netlistu)
- `win_recip` pipeline + `spi_slave_phy` fanout: **funkční** (rozbor + timing 0 porušení, 114,5 MHz)
- napěťové kanály (a6db669): **podmíněně funkční** (CM7 OK; CM4 web/SCPI rozpor; riziko starého CALIB blobu; HW AIN3)
- driver AD5693R: **podmíněně funkční** (čip neosazen — ověřitelné až po osazení)

| S1 | S2 | S3 | S4 |
|---|---|---|---|
| 3 | 4 | 8 | 2 |

---

### F-0201 [S1] Carry-chain TDC syntéza odstranila — v netlistu není žádný carry řetěz

- **Místo:** `Frequency_Counter_FPGA_Module/src/top.v:416-419` (`carry_tdc`: `sig_rep`, `chain_sum`)
- **Popis:** `{chain_cout, chain_sum} = {1'b0, {15{sig_in}}} + 1` je pro syntézu logicky triviální:
  bity `chain_sum[14:1]` jsou vždy 0 (všechny operandy jsou tentýž bit), `chain_sum[0] = ~sig_in`.
  Atributy `(* keep *)` zachovávají jména sítí, ne aritmetiku — syntéza výraz zjednodušila.
- **Důkaz:** syntetizovaný netlist `impl/gwsynthesis/Counter_FPGA.vg`, `module carry_tdc`
  (ř. 11–93): **3× DFF, DFFE, DFFR, LUT2, 2× LUT3, INV — žádná ALU buňka**; jediné registry
  `tap_ff_0`, `tap_ff_prev_0`, `fine_0`, `sig_rise`, `state`. Bity `tap_ff[14:1]` a `fine[3:1]`
  neexistují (konstanta 0).
- **Dopad:** Žádné sub-taktové rozlišení. `fine` je při každé události 1 (= `popcount(tap_ff)` při
  `tap_ff[0]=1`), takže `ev_ts = {tick_p0, 4'b0001}` a v rozdílech se ruší → měření je hrubé
  10 ns, **číselně totožné s HEAD** (`coarse_edge_detect`). Žádná regrese hodnot, ale celá
  dnešní „Fáze A1" nic nepřinesla a komentáře tvrdí opak (`top.v:102-103`, `:255-262`,
  `spi_app.v:31-33`: „nese skutečný 4b popcount kód, LSB=625 ps").
- **Reprodukce:** deterministické — `grep` buněk v `module carry_tdc` souboru `Counter_FPGA.vg`.
- **Návrh opravy:** carry řetěz se nedá spolehlivě vynutit chováním (`+`); instanciovat primitivy
  `ALU` (Gowin `prim_syn.v`) s explicitně řetězeným `CIN`/`COUT` a po syntéze **vždy ověřit
  v `.vg`**, že řetěz existuje. Ale viz F-0203 — samotné vynucení řetězu nestačí.
- **Riziko opravy:** střední (nová RTL bez simulátoru; placement řetězu).
- **Vztah k lekcím:** nová lekce po opravě — „`keep` nebrání logickému zjednodušení; o tom, co je
  v bitstreamu, rozhoduje netlist, ne zdroják" (příbuzné L-0035: měř nad `.elf`, ne nad zdrojem).
- **Stav:** **opraveno 2026-10-03** (`f968517`): 2 řetězy po 256 přímo instancovaných `ALU` (`tdc.v`). Netlist ověřen skriptem `Frequency_Counter_FPGA_Module/sim/check_tdc_netlist.py`: 512 `ALU`, všechny `I0=VCC, I1=GND`, hlava řetězu = `sig_eff`, všech 512 `SUM` vzorkováno. ⬜ neověřeno na křemíku

---

### F-0202 [S1] `carry_tdc` spouští událost na sestupné hraně (polarita obrácená)

- **Místo:** `top.v:435-436` (`rising_now`, `falling_now`), `top.v:451` (komentář stavů)
- **Popis:** Kódování `chain_sum` je obrácené proti záměru: vstup **LOW → `0x0001`** (nenulové),
  ustálený **HIGH → `0x0000`**. `rising_now = (prev == 0) && (now != 0)` tedy detekuje přechod
  HIGH→LOW, tj. **sestupnou hranu**. Komentář `top.v:451` („0 = potvrzeno LOW") platí naopak.
  Nahrazený `coarse_edge_detect` (HEAD, `git show HEAD:…/top.v:375-385`) spouštěl na **náběžné**.
- **Důkaz:** `15×1 + 1 = 0x8000` → `chain_sum = 0`, `chain_cout = 1`; `0 + 1 = 0x0001`.
  V netlistu: `tap_ff_0 = ~sig_in` (INV + DFF), událost = `!tap_ff_prev_0 & tap_ff_0`.
- **Dopad:** Pro kmitočet nevadí (perioda mezi sestupnými hranami = perioda). Vadí pro:
  (a) **skutečný TDC** — na sestupné hraně se v řetězu `+1` přenosy ruší paralelně
  (`COUT = CIN & I0`, při `I0→0` padá hned), nic se nešíří, takže sestupná hrana **nenese
  interpolační informaci**; (b) **plánovanou Fázi B** — GPS 1PPS (PIN33) je časová značka na
  náběžné hraně; sestupná je posunutá o šířku pulzu; (c) budoucí měření časového intervalu A↔B.
- **Reprodukce:** deterministické; rozbor výše + netlist.
- **Návrh opravy:** detekovat přechod z „LOW vzoru" (`0x0001`) do „ne-LOW", a vrátit komentáře.
  Opravovat **jen spolu s rozhodnutím o F-0203** (jinak opravíte polaritu obvodu, který nic neměří).
- **Riziko opravy:** nízké samo o sobě; pozor na invariant F-0207.
- **Vztah k lekcím:** nová lekce po opravě.
- **Stav:** **opraveno** (`f968517`): událost = `t0 & ~t0p`, kde `t0` je vzorek přímého vstupu tapu 0 (náběžná hrana); potvrzeno simulací `sim/tb_tdc.sv`. ⬜ neověřeno na křemíku

---

### F-0203 [S1] 15 tapů nemůže pokrýt periodu 10 ns — měřítko „LSB = 625 ps" je fiktivní

- **Místo:** `top.v:113-114` (`ev_ts = {tick_p0, fine}`), `top.v:164-165` + `:178,187` (CONST 1,6e14),
  `spi_app.v:550-552` (`gate_ns = dt·5/8`), `top.v:407-409` (`TAPS = 15`)
- **Popis:** Spojení `{tick_p0, fine}` s `fine` 0..15 předpokládá, že 16 kódů rovnoměrně dělí
  celou periodu `clk_p0_100m` (10 ns / 16 = 625 ps). Jeden prvek carry řetězu ale trvá desítky ps
  — sám projekt počítá s rozlišením carry interpolátoru **~50–100 ps** (`PHASE_CAL_DESIGN.md:57`).
  15 tapů tedy pokryje ~0,75–1,5 ns z 10 ns: u ~85–90 % hran by řetěz při vzorku už doběhl
  a `fine` = 0, zbytek by dal kód s **nelineárním** a nekalibrovaným měřítkem.
  TAPS=15 bylo zvoleno podle rozpočtu S1/S2 (`spi_app.v:657-666`), ne podle pokrytí periody.
  Kalibrace (ring oscilátor + histogram hustoty kódů, plán A1) nebyla implementována
  (`meas_hist_zero`, `cal_mode_nc`, `top.v:263-265`).
- **Důkaz:** výše; starý 4fázový vernier měl krok 2,5 ns, takže krátký řetěz by mu stačil —
  s jedinou 100 MHz referencí musí řetěz pokrýt celých 10 ns (≈100–200 tapů při 50–100 ps).
- **Dopad:** I po opravě F-0201/F-0202 by TDC dával kódy, které **nejsou** 625ps jednotky →
  CONST, `gate_ns` i S1/S2 by počítaly s nepravdivým měřítkem. Architektonická, ne lokální vada.
- **Reprodukce:** výpočet; `HYPOTÉZA — ověřit` skutečné zpoždění tapu na GW1NR-9 až s funkčním
  řetězem (histogram hustoty kódů).
- **Návrh opravy (rozhodnutí):** (a) **vrátit `coarse_edge_detect`** (HEAD: 2FF synchronizér,
  náběžná hrana, 2,5ns jednotka s `fine=0`) a TDC dělat jako samostatný projekt; nebo
  (b) skutečný TDC: ≥100–200 tapů z primitiv `ALU`, 2. stupeň registru, histogram/ring osc.,
  LUT korekce, **přepočet rozpočtu S1/S2 pro širší `fine`** a protokol nesoucí Δt v tikách (F-0204).
- **Riziko opravy:** (a) nízké — návrat k ověřenému stavu; (b) vysoké — velká RTL bez simulátoru.
- **Vztah k lekcím:** —
- **Stav:** **opraveno variantou (b)** (`f968517`): 256 tapů, STA model 57 ps/tap = 14,5 ns na řetěz, kalibrace code density ve FPGA (ring oscilátor + LFSR dělič, jednotka T_clk/16384). ⚠️ Na křemíku může být řetěz rychlejší než STA model: kalibrace to ohlásí (`tdc` → „za koncem řetězu", `tdc_status` bit4/5 SHORT). ⬜ neověřeno na křemíku

---

### F-0204 [S2] Kontrakt `gate_time_ns` vs. STM: dnes přesný jen díky nefunkčnímu TDC

- **Místo:** FPGA `spi_app.v:552` (`gate_ns = floor(dt·0,625)` ns); STM `fpga_freq.h:160-161`
  (`FPGA_TICK_PS 2500`, `FPGA_TICKS_PER_S 4e8`), `fpga_freq.c:601-605` (`dt_ticks = round(gate_ns/2,5)`),
  emulátor `fpga_freq.c:336-339` (rámce s 2,5ns tiky), selftest `fpga_freq.c:791`,
  `meas_present.h:143` (`MP_TDC_PS 2500` „Si5356 4 fáze")
- **Popis:** FPGA dnes posílá okno jako **floor v ns z 625ps tiků**; STM z něj rekonstruuje tiky
  **2,5 ns** (oprava F-0186). Funguje to jen proto, že `fine` je konstantní (F-0201): `dt` je pak
  násobek 16, `gate_ns = 10·Δtick` je celé číslo a `round(gate_ns/2,5) = 4·Δtick` přesně.
- **Důkaz:** výše; žádná strana neví, jaký tik ta druhá používá (CAPS beze změny, F-0205).
- **Dopad:** **Latentní:** jakmile `fine` začne nést hodnoty (oprava F-0201/F-0203), STM tiky
  zaokrouhlí na 2,5 ns s chybou až ~0,9 tiku → systematická odchylka hi-res kmitočtu až
  **~9·10⁻⁹ při hradle 0,25 s** (třída F-0186). A ani `FPGA_TICK_PS = 625` to nespraví: floor
  v celých ns (1 ns) je hrubší než tik (0,625 ns), přesný Δt se z `gate_time_ns` zrekonstruovat
  **nedá**. `fpga_freq_hires_mul()` to neodhalí (chyba ≪ 0,1 %). Navíc `MP_TDC_PS = 2500`
  nadhodnocuje rozlišení nové desky 4× (skutečně 10 ns) → `freq_uncertain_frac()` a rozpočet
  nejistoty v okně ANALÝZA ukazují ~0,6 číslice víc „důvěryhodných" (platí od přechodu na novou
  desku, dnešní změna to nezměnila).
- **Reprodukce:** `HYPOTÉZA — ověřit` po zprovoznění TDC: porovnat `fpga_freq_hires_hz` s přesným
  `edges/(k·625 ps)` z `r_dt` (např. přes CAL report S1/S2) na emulátoru s proměnným `fine`.
- **Návrh opravy:** protokol musí nést Δt **v tikách** (nebo ps) — přesně jak už žádá CLAUDE.md
  („Nová deska (carry chain) má jiný tik → protokol v2 musí nést Δt v ticích nebo ps"); tik
  ohlásit v CAPS/FW; upravit STM (`FPGA_TICK_PS` z rámce, `MP_TDC_PS` podle desky) a **emulátor
  podle RTL** (L-0099). Do té doby (varianta a) z F-0203) zůstat u 2,5ns jednotky.
- **Riziko opravy:** střední (kontrakt obou stran + emulátor + selftest).
- **Vztah k lekcím:** L-0099 (emulátor podle RTL), F-0186.
- **Stav:** **opraveno**: rámec nese dt v jednotkách T_clk/16384 (abs 118/101), STM z něj počítá `fpga_meas_t.gate_ps` (±0,5 ps) a všechny výpočty jedou z ps; `gate_time_ns` je jen informativní (`dt·5>>13`); emulátor `fpgasim` složen podle RTL včetně kvantizačního šumu TDC (L-0099). ⬜ neověřeno na HW

---

### F-0205 [S3] `FW_VERSION`/`CAPS` nezvednuty, ač se změnila sémantika bitstreamu

- **Místo:** `spi_app.v:96` (`FW_VERSION = 16'h0300` + pravidlo „bump při KAŽDÉ změně bitstreamu"),
  `spi_app.v:105` (`CAPS = 16'h0013`)
- **Popis:** Dnešní diff mění jednotku tiku (2,5 ns → 625 ps), šířku `fine` (2 → 4 b) a rozložení
  bajtů 24 (CAL) a 100 (DATA). `git diff HEAD -- spi_app.v` nemění `FW_VERSION` ani `CAPS`.
- **Důkaz:** `git diff HEAD` nad `FW_VERSION|CAPS` je prázdný.
- **Dopad:** STM (`status` → řádek FW/CAPS) nerozliší dnešní bitstream od HEAD, přestože jinak
  kóduje pole. Porušené pravidlo v kódu samotném.
- **Návrh opravy:** bump `FW_VERSION` (0x0301) při jakékoli nové verzi bitstreamu; vyhradit bit
  CAPS pro „Δt jednotka/TDC aktivní" až s F-0204.
- **Riziko opravy:** nízké. · **Stav:** **opraveno**: `FW_VERSION` 0x0400, `CAPS` 0x0023 (bit5 = dt). ⬜

---

### F-0206 [S3] Rozhodovací logika hrany čte první synchronizační stupeň (HEAD měl 2 FF)

- **Místo:** `top.v:428-436`, `top.v:454-465`
- **Popis:** `rising_now`/`falling_now` i `fine` se počítají z `tap_ff` = **první** registr za
  asynchronním vstupem; `tap_ff_prev` je až druhý stupeň. HEAD (`coarse_edge_detect`) rozhodoval
  z druhého stupně (`s1`). Metastabilní `tap_ff` se větví do `sig_rise`, `state` i `fine`, které se
  mohou rozhodnout různě.
- **Důkaz:** výše; netlist: `tap_ff_0` krmí LUT `rising_now_s2` i D vstup `fine_0`.
- **Dopad:** Při ~8 ns rezervy na vyřešení metastability je MTBF prakticky neomezené — **dnes
  nízký**. U skutečného TDC je ale metastabilita teploměrového kódu záměrná (vzorkuje se hrana
  uvnitř řetězu) a druhý stupeň před dekódováním je nutný. Odchylka od konvence projektu
  (3stupňové synchronizéry v `spi_slave_phy.v:47-56`).
- **Návrh opravy:** podle F-0203 — u varianty (a) zmizí sama, u (b) přidat stupeň před popcount.
- **Riziko opravy:** nízké. · **Stav:** **opraveno/zmírněno**: `t0` je jeden FF a čte ho jen krátká logika (rezerva > 5 ns => MTBF prakticky neomezená); tapy se dekódují až po 5 taktech zmrazení, takže metastabilní okrajové tapy jsou do té doby ustálené. ⬜

---

### F-0207 [S3] Invariant „≥ 2 takty mezi událostmi" drží jiný modul, než na který odkazuje komentář

- **Místo:** `spi_app.v:684-688` (odvolává se na „oversampler … debounce"), `top.v:451-465`
  (skutečný zdroj garance), `spi_app.v:657-666` (rozpočet S1/S2 počítá s „33,3 MHz")
- **Popis:** Dnešní rozdělení S1 na 28+28 b (stage 1 / 1b) je správné **jen** pokud dvě události
  `sig_rise` nejsou blíž než 2 takty — rozbor časové osy T→T+1→T+2 ukazuje, že rezerva je přesně
  nulová (další `acc_d` nejdřív v T+3). Garanci dnes dává stavový automat `carry_tdc` (opětovné
  ozbrojení vyžaduje takt s `tap_ff == 0`), ale komentář v `win_recip` cituje odstraněný
  oversampler. Rozpočet S1/S2 uvádí max. 33,3 MHz; `carry_tdc` dovolí události každé 2 takty
  (50 MHz).
- **Důkaz:** přepočet pro 50 MHz a okno 1 s (T = 1,6e9 tiků, N = 5e7): S1 ≈ N·T/2 = 4,0e16
  < 2⁵⁶ = 7,2e16 (×1,8); S2 ≈ N²·T/6 = 6,7e23 < 2⁸⁰ = 1,21e24 (×1,8) — **meze platí**, jen
  komentář je zastaralý (a vzorec „S1_max·N_max/2" pro S2 nadhodnocuje).
- **Dopad:** Kdo upraví opětovné ozbrojení v `carry_tdc` (např. při opravě F-0202), může tiše
  rozbít S1 — bez simulátoru a bez jakékoli kontroly.
- **Návrh opravy:** zapsat invariant k `carry_tdc` (zdroj garance) + v `win_recip` levný čítač
  „událost dřív než za 2 takty" → příznak do `error_flags`.
- **Riziko opravy:** nízké. · **Stav:** **zaniklo**: S1/S2 akumulátory (Λ/Ω) odstraněny, invariant už nikdo nepotřebuje.

---

### F-0208 [S4] Zastaralé komentáře po dnešních změnách

- **Místo / popis:**
  - `top.v:249` — „bit2 = Δt ≥ 2^33 ticků" (dnes bit 35, kód na `:209` je správně)
  - `top.v:102-103`, `:255-262`, `spi_app.v:31-33` — tvrdí reálná `fine` data (viz F-0201)
  - `spi_slave_phy.v:33` — `clk // 10 MHz (clk_ref_10m)`; ve skutečnosti `clk_p0_100m` (`top.v:311-312`)
  - `pins.cst:12-15` — „dnes jen hrubé čítání … viz coarse_edge_detect" (modul nahrazen)
  - `spi_app.v:657-666` — 33,3 MHz (viz F-0207)
  - `CM7/Core/Src/datalog.c:492-493` — „AIN1 je mV z AD8307" (dnes VBUS, viz a6db669)
- **Dopad:** čtenář dostane nepravdivý obraz; u `fine` přesně to vedlo k přehlédnutí F-0201.
- **Návrh opravy:** jeden `docs:` commit. · **Riziko:** žádné. · **Stav:** **částečně opraveno**: komentáře v `top.v`/`tdc.v`/`spi_app.v`/`spi_slave_phy.v`/`pins.cst` přepsány; zbývá `datalog.c:492` (AIN1).

---

### F-0209 [S2] Web (CM4) a SCPI dál vydávají AIN2 (+3V3) za větev 12 V; VBUS nejde přečíst vůbec

- **Místo:** web SPA `CM4/LWIP/App/httpd_min.c:477` (`v12_mv`), `:1359` (legenda „12 V"),
  `:1640` (`NOMV={p0:12000,…}`), `:2042`, `:2064`, `:2572-2573` (`railSt`), `:2679-2680`
  (karta „12 V", `railSt(s.v12_mv,12000)`); SCPI `CM7/Core/Src/scpi.c:718-722` (`MEAS:VOLT:ALL?`),
  `:730` (`MEAS:VOLT?` výchozí `P12`), `:1137` (`v_12v_mv` ← `SENS_ADS2`)
- **Popis:** Oprava a6db669 přejmenovala AIN2 na +3V3 v UI na CM7, ale stejný fakt na CM4 a v SCPI
  zůstal. Web hodnotí 3 300 mV proti nominálu 12 000 mV: `railSt` → |3300−12000|/12000 = 0,725
  → **„bad" (červeně)**, graf odchylky ukáže ≈ −8,7 V. `MEAS:VOLT?` bez argumentu vrátí
  3,30 jako „12 V". Skutečný VBUS (AIN1) se do snapshotu sice plní (`rf_mv`), ale publikuje se
  jen s bitem RF, který se po a6db669 nikdy nenastaví → přes web/SCPI **nedostupný**.
- **Důkaz:** výše (řádky); `railSt` na `:2573`.
- **Dopad:** deterministicky: webová karta napájení trvale hlásí poruchu 12V větve; vzdálený
  skript s `MEAS:VOLT?` čte jinou veličinu, než jakou příkaz jmenuje.
- **Návrh opravy (rozhodnutí — API):** SCPI `P3V3` + `VBUS` (a `P12` ponechat jako alias, nebo
  zrušit?); web: přejmenovat řadu + nominál 3300; VBUS do snapshotu = **nové pole → `IPC_VERSION`
  bump + flash obou bank** (nebo nový bit platnosti pro `rf_mv`).
- **Riziko opravy:** střední (veřejné rozhraní + IPC).
- **Vztah k lekcím:** L-0110 (deset míst, tady jedenácté a dvanácté), L-0012.
- **Stav:** otevřeno — **rozhodnout**

---

### F-0210 [S2] Starý kalibrační blob tiše přepíše opravené zisky AIN2/AIN3

- **Místo:** `CM7/Core/Src/calib.c:45` (magic `CAL1` beze změny), `:81-92` (`calib_load`)
- **Popis:** `gain_12v`/`gain_5v` se z W25Q načtou bez jakékoli kontroly rozsahu (na rozdíl od
  strmosti AD8307, F-0165). Kdokoli dřív stiskl v okně Kalibrace **ULOŽIT**, má v blobu zisky
  4,768/1,971 — a ty po bootu přebijí nové výchozí 2,0/1,4545.
- **Důkaz:** `calib.c:91-92` přiřazuje bez podmínky; magic se nezměnil.
- **Dopad:** na takovém přístroji oprava a6db669 **nepůsobí**: AIN2 ukáže ≈ 3,3·4,768/2 = 7,9 V,
  AIN3 ≈ 6,8 V → přeškrtnutý kmitočet (`warn_rail_bad`) zůstane.
- **Reprodukce:** `HYPOTÉZA — ověřit`: `sensors` → AIN2/AIN3 proti multimetru; poměr ≈2,4× / ≈1,36×
  = starý blob.
- **Návrh opravy:** při načtení kontrolovat rozsah (stejné meze jako `KALIB_ROWS`: 1,0–3,0 / 1,0–2,0),
  mimo → výchozí hodnoty; alternativně bump magic `CAL2`.
- **Riziko opravy:** nízké. · **Vztah k lekcím:** F-0165. · **Stav:** otevřeno

---

### F-0211 [S2] HW: dělič AIN3 dává na vstup ADS1115 napětí nad jeho VDD

- **Místo:** netlist `FPGA_Module_2_1`: R57 = 10k (+5V → AIN3), R58 = 22k (AIN3 → GND); U8 ADS1115
  pin 8 VDD = **+3V3**
- **Popis:** V_AIN3 = 5,0 V · 22/32 = **3,44 V > VDD 3,3 V**. Dnešní firmware (gain 1,4545) to
  správně přepočítá, ale vstup leží mimo provozní rozsah převodníku (GND…VDD).
- **Důkaz:** netlist (kicad-cli export 2026-10-02); zisk v `calib.c:34`.
- **Dopad:** horní část rozsahu měření může být nelineární (vstupní ochranné struktury); při
  +5V ≈ 5,24 V dosáhne vstup VDD+0,3 V. `HYPOTÉZA — ověřit` přesné meze v datasheetu
  TI ADS111x (Absolute Maximum Ratings / analog input range). Původní „rev2" záměr byl 15k/10k
  (2,0 V).
- **Návrh opravy (HW):** prohodit/změnit odpory (např. R57 = 22k, R58 = 10k → 1,5625 V, zisk 3,2)
  a upravit `CALIB_DEFAULT_GAIN_5V`.
- **Riziko opravy:** nízké (osazení + jedna konstanta). · **Stav:** otevřeno — **rozhodnout (HW)**

---

### F-0212 [S3] 10Hz „RF fast-path" na AIN1 už nemá konzumenta

- **Místo:** `CM7/Core/Src/freertos_task_sensors.c:412-440` (`if (sub != 0)`)
- **Popis:** Rychlá cesta existovala kvůli živému RF bargrafu; ten je po a6db669 vypnutý
  (`RF_LEVEL_HW_PRESENT = 0`). Dál běží 8 převodů/s VBUS — každý 2 transakce I2C1 pod mutexem
  + `osDelay(9)` — a navíc dopočítává min/max/průměr SENS_ADS1 jiným tempem než ostatní kanály.
- **Dopad:** zbytečný provoz I2C1 (sdílí ho nově i DAC) a CPU; žádná funkční vada.
- **Návrh opravy:** obalit `#if RF_LEVEL_HW_PRESENT` (nebo běh podmínit).
- **Riziko opravy:** nízké. · **Stav:** otevřeno

---

### F-0213 [S3] VBAT můstek je zapnutý trvale (HYPOTÉZA k hlášení „VBAT měří špatně")

- **Místo:** `ADC_CCR_VBATEN` se nastavuje v `freertos_task_sensors.c:60`, `:407`
  a `freertos_task_uart.c:1948`; **nikde se nenuluje**
- **Popis:** Odporový dělič VBAT/4 je připojený na baterii po celou dobu běhu. ST pro tuto funkci
  doporučuje zapínat můstek jen na dobu převodu (zatěžuje záložní článek). Poměr děliče má navíc
  v datasheetu toleranci.
- **Důkaz:** grep výše. Vzorec převodu (`freertos_task_sensors.c` blok ADC3) je ratiometrický
  k VREFINT, takže chybu VREF+ ani zesílení ADC nepřenáší — zbývají poměr děliče, kalibrace
  VREFINT a skutečné napětí na pinu VBAT.
- **Dopad:** `HYPOTÉZA — ověřit`: (1) DS12930 (H747/H757), tabulka „VBAT monitoring
  characteristics" — odpor můstku a chyba poměru Q; (2) multimetr na BT1 vs `sensors` VBAT
  a `adcraw` (syrové hodnoty — **od uživatele zatím chybí**); (3) proud z článku za běhu.
  Neověřeno, zda to je příčina hlášené chyby.
- **Návrh opravy:** po změření rozhodnout; varianta: `VBATEN` jen kolem převodu VBAT.
- **Riziko opravy:** nízké–střední (doba ustálení můstku před převodem). · **Stav:** otevřeno — čeká na data

---

### F-0214 [S3] Projekt se od 2026-09-28 překládá jako STM32H747, ne H757

- **Místo:** `CM7/.cproject` a `CM4/.cproject` (necommitnuté: `target_mcu` STM32H747BITx,
  define `STM32H747xx`); `Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h757xx.h` **smazán**,
  `stm32h747xx.h` **nesledovaný**; `H757_LED.ioc:250,282,391,796` dál STM32H757BITx
- **Popis:** Změna cílového MCU v IDE se propsala do `.cproject` a výměny hlavičky. Vygenerované
  makefily (`CM7/Release/Core/Src/subdir.mk`, `CM4/…`) obsahují `-DSTM32H747xx` a mají čas
  **2026-09-28 21:03** → **všechny Release buildy od té doby** (včetně ověřovacích buildů dnešních
  oprav) jsou H747. Moje dřívější kontrola zkoumala jen `.ioc`, proto to přehlédla.
- **Důkaz:** `git diff HEAD` nad `.cproject`; porovnání hlaviček (normalizovaná jména): rozdíl je
  **jen** CRYP/HASH + jméno IRQ 80 (`HASH_RNG_IRQn` vs `RNG_IRQn`); HAL CRYP/HASH/RNG jsou
  vypnuté (`stm32h7xx_hal_conf.h:44,55,69`), startup zůstal `startup_stm32h757bitx.s`.
- **Dopad:** **Funkčně ekvivalentní** pro tento firmware (nic nepoužívá krypto/RNG). Riziko je
  konfigurační: rozpor s `.ioc` a CLAUDE.md, smazaný sledovaný soubor se může omylem commitnout,
  příští regen/reload projektu to může přepnout zpět.
- **Návrh opravy:** vrátit oba `.cproject` a `stm32h757xx.h` z HEAD, smazat `stm32h747xx.h`,
  v IDE Close/Open Project, přeložit, ověřit `-DSTM32H757xx` v `subdir.mk`. **Jen se souhlasem**
  (mění to stav IDE uživatele).
- **Riziko opravy:** nízké. · **Stav:** otevřeno — **rozhodnout**

---

### F-0215 [S3] AD5693R: zápis control registru při sondě předpokládá neověřený stav po zapnutí

- **Místo:** `CM7/Core/Src/freertos_task_sensors.c:266-288` (`ad5693_probe`, zápis `:276`),
  `CM7/Core/Inc/ad5693.h:26,32-35`
- **Popis:** Hlavička tvrdí, že zápis GAIN ×2 „výstupem nehne", protože DAC startuje na nule. Kdyby
  startoval na středu (neověřeno — datasheet nebyl dostupný), zápis GAIN ×2 výstup **zdvojnásobí**
  (1,25 → 2,5 V) = skok ladění OCXO hned při bootu. Forma readbacku je také hypotéza (Linux posílá
  před opakovaným STARTem 3 B, driver 1 B).
- **Dopad:** nelze nastat dřív než po osazení čipu.
- **Reprodukce:** po osazení **před prvním `dac` zápisem**: `dac` (readback) + `sensors` AIN0 hned
  po studeném startu.
- **Návrh opravy:** odložit do osazení; podle výsledku případně psát control registr až po
  přečtení kódu.
- **Riziko opravy:** nízké. · **Stav:** odloženo do osazení U12

---

### F-0216 [S3] AD5693R: po studeném startu není definované ladicí napětí OCXO

- **Místo:** `ad5693.h` (záměrně „kód se nikdy nezapisuje automaticky"), `ad5693_service()`
- **Popis:** Po osazení bude OCXO po každém zapnutí na okraji ladicího rozsahu (≈0 V), dokud někdo
  ručně nezapíše kód. Bez osazeného čipu je to dnes stejné (vstup U15 visí, VC ≈ 0,1 V).
- **Dopad:** reference přístroje je po zapnutí rozladěná o celý ladicí rozsah OCXO.
- **Návrh opravy (rozhodnutí):** výchozí kód při studeném startu (střed 2,5 V?) × čekat na
  perzistenci kódu (Fáze B, syscfg).
- **Riziko opravy:** nízké, ale je to politika — každý zápis přeladí referenci. · **Stav:** otevřeno — **rozhodnout**

---

### F-0217 [S4] Rozsahy zobrazení OCXO Vc se rozcházejí; `dac` ukazuje starou hodnotu AIN0

- **Místo:** `CM7/app/app_gpsdo.c:1541` (GRAFY: 0–3 300 mV, nominál 1 650),
  `:1858` (PŘEHLED: 0–3,3 V, nominál 1,65), ale `:4242` (`OCXO_VC_FS_MV 5000`, budík Holdover);
  `CM7/Core/Src/freertos_task_uart.c:672`
- **Popis:** Rozsah DAC/snímání je 0–5 V (GAIN ×2, dělič AIN0 na 0–5 V); dvě okna řežou na 3,3 V
  s nominálem 1,65 V, budík má 0–5 V. `dac` tiskne AIN0 hned po zápisu, přičemž RC 47k/1µ
  (τ = 47 ms) a vzorkování 2 Hz znamenají, že hodnota je stará.
- **Návrh opravy:** sjednotit rozsahy na 0–5 V; u `dac` poznamenat stáří vzorku (nebo počkat ~0,6 s).
- **Riziko opravy:** žádné. · **Stav:** otevřeno

---

## Co bylo zkontrolováno a je v pořádku

- **`spi_slave_phy.v` `rx_done`/`rx_armed`** (`:85,94,113,124-134,145-150`): ekvivalence s
  `bit_in != 1024` / `bit_in == 0` platí — `bit_in` se mění jen v shift bloku (+1, max 1024) a v
  `cs_rise` (→0, textově později = vyhrává při souběhu); příznaky přesně tam. Timing změřen:
  **0 setup / 0 hold porušení, `clk_p0_100m` Fmax 114,508 MHz**, registry 80 %, logika 56 %.
- **`win_recip` rozdělení S1 (28+28 b)** (`spi_app.v:713-790`): časová osa událostí s odstupem 2
  takty — stage 1 čte `s1` usazené z předchozí stage 1b; `s1_hi_d` i dolní S2 čtou `s1` PŘED
  zápisem této události (stejná sémantika „bod i−1" jako dřív); `ts_rel_d` není v T+2 přepsané;
  `r_s1` dokončené na téže hraně jako `r_s2`/`res_tgl`; CDC do 10 MHz (`top.v:149-155,201-214`)
  — data stabilní ≥3 takty před vzorkem. Odvození Σi·tᵢ = N·S1 − S2 potvrzeno.
- **`recip_calc`** (`spi_app.v:541-573`): CONST = 1e5/625 ps = 1,6e14; `gate_ns = floor(dt·5/8)`;
  zaokrouhlení `dt>>1`; `periods·CONST` < 2²⁶·2^47,2 < 2⁸⁰; `dt_ovf` na bitu 35 (`top.v:209`).
- **`tx_b[24]`** (`spi_app.v:289`) leží jen ve větvi CAL — DATA `edge_count` (bajty 20–27) nepoškozuje.
- **Rozpočet S1/S2 při skutečné max. četnosti událostí 50 MHz** (F-0207): platí s rezervou ×1,8.
- **Driver AD5693R vs. protokol** (Linux `ad5686.c`, `ad5696-i2c.c`): příkazy 0x3/0x4, bity
  control registru (D12 REF, D11 GAIN, D14:13 PD), 16 b bez posunu, adresa 0x4C z netlistu (A0=GND).
- **Sonda AD5693R neruší I2C1:** NACK při zápisu končí v HAL jako `HAL_I2C_ERROR_AF`
  (`stm32h7xx_hal_i2c.c` ~7179-7231), `TIMEOUT` jen bez STOPF; `IsDeviceReady` by nastavil
  TIMEOUT (~3368) → `i2c1_recover_if_wedged()` by běžel (L-0112) — driver ho nepoužívá.
- **Hradlo RF (a6db669)** pokrývá všechna místa převodu na dBm: `scpi.c` (2), `ipc.c` (1),
  `app_gpsdo.c` (4); CM4 web/SCPI dědí `SCPI_V_RF`/`IPC_V_RF` ze snapshotu.
- **`sens_mv`** (`datalog.c:452-459`) odmítne |v| > 32 V → VBUS nepřeteče `int16`.
- **Necommitnuté soubory mimo dnešní práci** (`main.c`, `rtc.c`, `fmc.c`, …) se liší **jen
  konci řádků** — žádná obsahová změna ADC3/PWR/VBAT.
- **`phase_status`** (nová deska posílá 0) STM jen zobrazuje (`app_gpsdo.c:6222-6223`), nic nehradlí.

## Nezkontrolováno / omezení tohoto běhu

- **Žádný běh na HW, žádný Verilog simulátor** — FPGA jen syntéza + netlist + timing; STM jen
  build (0 varování) + `tools/audit.py` 92/0/2.
- **Datasheety nedostupné** (analog.com timeout): stav AD5693R po zapnutí, forma readbacku;
  meze ADS1115 a VBAT můstku STM32H7 citovány z paměti → označeny HYPOTÉZA.
- **VBAT:** příčina hlášené chyby neurčena — chybí `adcraw`, `sensors` a údaj multimetru.
- **Vstupy nad ~50 MHz** na přímé cestě se aliasují bez příznaku (předděličku ÷10 přes relé zatím
  nic nepřepíná) — architektonický limit existující už před dnešní změnou, mimo rozsah.

## Triáž pro fázi oprav (§F5.0)

| skupina | nálezy | poznámka |
|---|---|---|
| **A — opravit hned** | F-0205, F-0208, F-0210, F-0212, F-0217 | malé, chování v normálním provozu nemění |
| **B — rozhodnout** | **F-0201/F-0202/F-0203** (+F-0204, F-0206, F-0207) | doporučení: **varianta (a)** — vrátit `coarse_edge_detect` (2,5ns jednotka, náběžná hrana, 2FF) a TDC dělat jako samostatný projekt |
| | F-0209 | názvy veřejného API + `IPC_VERSION` |
| | F-0211 | HW: odpory děliče AIN3 |
| | F-0214 | vrátit `.cproject` + hlavičku H757 (mění stav IDE) |
| | F-0216 | výchozí ladicí napětí OCXO po studeném startu |
| | F-0213 | až po datech od uživatele |
| **C — odložit** | F-0215 | do osazení U12 |
