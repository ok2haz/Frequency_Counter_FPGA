# Kritický audit — TDC (FW 0x0410/0x0411), regresní blok, zobrazení a web (2026-10-07)

Rozsah: vše, co vzniklo v sezení „měření vlastní reference → obří bin → STRIDE 1 → regrese“.
Metoda: znovu jsem prošel kód, výsledky simulací, P&R a dokumentaci a u každého tvrzení hledal,
**čím bylo doloženo** (měření na desce / simulace / úvaha). Nálezy jsou seřazené podle toho, co
může nejvíc uškodit. Kód se v této fázi nemění (pravidlo 4); opravy jsou jen doporučení.

Legenda důkazu: **[HW]** změřeno na desce, **[SIM]** jen simulace (model), **[P&R]** výstup Gowin,
**[ÚVAHA]** nic z toho. Cokoli bez **[HW]** je `⬜ neověřeno na HW`.

---

## 0. Souhrn jednou větou

Simulace, překlad, P&R a statická analýza jsou čisté, ale **žádná z architektonických změn TDC
(0x0410, 0x0411) nebyla na křemíku ověřena**, příčina dvou pozorovaných vad (skoky ±9,28 ns,
miscount ~2 %) **není uzavřená**, a regresní blok stojí 11 procentních bodů plochy FPGA a prakticky
nulovou časovou rezervu za zisk, který **neplatí pro hlavní použití přístroje** (GPSDO 10 MHz).

---

## 1. Nálezy — vysoká závažnost

### A-01 Nic z TDC změn není ověřeno na křemíku; model ověřuje sám sebe  `[SIM]`
- `sim/gowin_models.v` je model ALU/řetězu napsaný podle **mého pochopení** křemíku (průměr kroku,
  rozptyl, zlom řetězu). Obří bin jsem v něm reprodukoval, opravu otestoval ve stejném modelu →
  test dokazuje, že **oprava funguje v modelu, který jsem vyladil, aby vadu měl**. To je platný
  regresní test, ale **ne důkaz o křemíku**. (Lekce L-0131 níže.)
- Slepota spouštěče (1,87 ns kanál A, ~5,3 ns kanál B) je **odvozená z měření obřího binu**, ne
  změřená přímo. Alternativní vysvětlení (jiný zdroj hrany v kódu ~134) jsem nevyloučil.
- **Co by hypotézu vyvrátilo (po flashi 0x0410):** `tdc` → největší bin > ~200 ps, nebo minimální
  neprázdný kód > ~3 u A či B; `tools/tdc_beat.py` s šumem značky výrazně nad 215 ps.
- Kanál B **nebyl na křemíku nikdy měřen** (měření byla na CH_A).

### A-02 Skoky ±9,28 ns a miscount ~2 % nejsou uzavřeny  `[SIM]`
- Skoky ±9,28 ns se v simulaci **nepodařilo reprodukovat**. Hypotéza (nesoulad spouštěč × vzorek o
  takt) je **HYPOTÉZA**, nikoli nález. Architektura 0x0410 se jí vyhýbá, ale bez reprodukce nevíme,
  zda ji skutečně odstranila.
- `fpga_freq_miscount` hlásil ~2 % oken i na 0x040F, kdy jsem tvrdil, že synchronizátor +4 Hz
  opraví. Tvrzení bylo nedostatečné (L-0127). Stav pro 0x0410/0x0411: **neznámý**.
- Ověření: 60 min stejné měření jako `docs/audit/2026-10-07_tdc-vlastni-reference.md` kap. 4,
  porovnat `miscount` a spike-reject (23 % oken na 0x040E).

### A-03 Časová rezerva P&R 0x0411 je prakticky nulová  `[P&R]`
- Výsledek (`scripts` běh 2026-10-07 20:24, `gw_sh build.tcl`, STRIDE 1 / DUAL 0):
  `clk_p0_100m` Fmax **100,165 MHz** při 100 MHz, setup slack **+0,017 ns**, TNS 0, hold 0.
  Kritická cesta: `u_wra/trig_q → u_wra/cy_q` (staré `win_recip`, ne regrese).
- 0x0410 mělo ~102 MHz → regrese rezervu spolkla. **17 ps je v rámci šumu P&R mezi běhy**: změna
  jakékoli nezávislé logiky nebo jiné seed může časování rozbít, a to **tiše** (bitstream se vyrobí).
- Mezistav během vývoje ukázal, jak snadno se to rozpadne: první verze `regr_acc` 53 MHz /
  TNS −2460, po pipeline 68,7 MHz (24bitový čítač přeložen na 11 LUT místo ALU), po rozdělení čítače
  93,6 MHz (64bitový akumulátor), po rozdělení akumulátoru 100,165 MHz. **Simulace časování nevidí.**
- Doporučení: před každým flashem kontrolovat `Counter_FPGA_tr_content.html` (Fmax ≥ 100, TNS 0);
  nepřidávat do `clk_p0_100m` další logiku bez nové rezervy.

### A-04 Regrese zabírá rezervu, kterou potřebuje #262 (kanál C pro 1PPS)  `[P&R]`
- CLS: 0x0410 **68 %**, 0x0411 **79 %** (+11 pp; logika 52 %, registry 58 %, DSP 70 %, BSRAM 20 %).
- `STATUS.md` #262 (třetí `tdc_chan` na 1PPS = **vstup regulační smyčky GPSDO**) už při 0x040A
  stálo na CLS 88 % „nevejde se". Úspory z 0x0410 (CLS 73→68 %) byly přesně rezerva pro #262;
  regrese ji z velké části snědla **pro funkci, která nemá ověřený přínos** (A-05).
- Doporučení: buď parametr `REGR` v `top.v` s výchozí hodnotou 0 (modul se při 0 nesyntetizuje),
  nebo regresi odstranit, dokud se na reálném signálu neprokáže zisk.

### A-05 Zisk regrese neplatí pro hlavní použití; mimo simulaci neměřen  `[SIM]`
- Regrese zlepšuje odhad kmitočtu jen tehdy, když fáze hrany vůči hodinám TDC „prohazuje" (vstup
  **není** násobkem 100 MHz). Měření GPSDO 10 MHz × OCXO 10 MHz — tedy **základní scénář přístroje** —
  tuto podmínku nesplňuje: fáze se opakuje, chyba značky je konstantní a průměr ji nezlepší.
  (Je to uvedeno i v hlavičce `regr_acc`.)
- Zisk ~3× je z `tb_regr_e2e` (TSIG 97,123 ns, model křemíku). Dřívější tvrzení „až 3 řády" bylo
  **přehnané** a stažené; platí „v simulaci ~3× u nesoudělného signálu".
- Dopad: 11 pp plochy + 17 ps rezervy za zisk u úzké třídy signálů.

### A-06 Mez konzistence regrese 3·10⁻⁷ je řádově příliš volná  `[ÚVAHA]`  `CM7/Core/Inc/fpga_freq.h` `FPGA_REGR_MAX_REL`
- Šum dvoubodového odhadu: σ ≈ √2·215 ps / 0,25 s ≈ **1,2·10⁻⁹**. Mez 3·10⁻⁷ je **~250 σ**.
- Důsledek: chybný regresní kmitočet s relativní chybou až 300 ppb (3 Hz na 10 MHz!) se přijme a
  **nahradí přesný dvoubodový odhad**. Ochrana proti poruše tedy prakticky nefunguje; zamítne jen
  hrubé chyby (selftest: 10⁻⁶).
- Doporučení: mez ~5·10⁻⁸ (≈40 σ) nebo adaptivně z běžného rozptylu rozdílu; změnit selftest vektor
  `sc == 1` (1·10⁻⁸ musí zůstat přijato) a přidat vektor ~1·10⁻⁷ (zamítnout). Opravit až po souhlasu.

---

## 2. Nálezy — střední závažnost

### B-01 Test závodu v `regr_acc` nechytá to, co má  `[SIM]`
- Stav `S_W` (čekání 5 taktů na doběh pipeline po konci segmentu) jsem přidal po úvaze o závodu
  „značka těsně před koncem segmentu". Negativní kontrola (verze bez čekání) **testem prošla
  stejně** → test závod neprokazuje, `S_W` je obranná rezerva, ne ověřená oprava.
- Skutečná drobná chyba nalezená při rozboru: `n >= 2` se posuzovalo na hodnotě před započtením
  poslední značky (segment s přesně 2 značkami by se zahodil). Opraveno přesunem testu na konec
  `S_W`; test `trial == 1` (2 značky, druhá těsně před koncem) to pokrývá.
- Počítadla `n` (3 × 8 b) a `sy` (2 × 32 b) mají registrovaný přenos; hodnoty jsou stabilní až po
  3 taktech od poslední značky. Správnost stojí na tom, že `S_W` čeká 5 → **vazba není hlídaná
  assertem** (`S_W` by se musel zkrátit a nikdo by to nepoznal).

### B-02 Zastaralé komentáře a dokumenty  `[ÚVAHA]`
- `spi_app.v` ř. 9 a 48 (window stream, `FW_VERSION 0x0400, CAPS 0x0023`) a ř. 376 („window stream").
- `CM7/Core/Inc/fpga_freq.h:58` (`caps bit0=window stream`) a `:60` (`win_count … dnes nevyuzito`).
- `CM7/Core/Src/freertos_task_uart.c` ~3217 (komentář „window stream … jeste nejsou zapojene").
- `FPGA_PROTOCOL_V2_NAVRH.md:80,200` (window stream jako součást protokolu).
- `CLAUDE.md` ~1077–1100: popis TDC (`STRIDE = 2`, 256 FF/kanál, `FW:0x0408 CAPS:0x0023`) neodpovídá
  0x0410/0x0411 (STRIDE 1, 320 stupňů, CAPS 0x00E2). `docs/HW_REFERENCE.md` rámec bajty 68–96.
- Pravidlo 3 (logika ≠ komentáře v jednom commitu): opravy komentářů jdou do samostatného `docs:`
  commitu — **dosud neprovedeno**.

### B-03 Diagnostická počítadla regrese nejsou chráněná  `CM7/Core/Src/fpga_freq.c` `fpga_freq_regr_stat`
- Zapisuje `FpgaTask` (`parse_data`), čte UartTask bez kritické sekce → možné roztržené čtení
  `double`/`uint32`. Jen diagnostika (`regr`), žádný dopad na měření. Stejný vzor jako ostatní
  statistiky v modulu (konzistentní, ale nesprávný).

### B-04 `gate_ps` nahrazený efektivní délkou N/f_regr  `[ÚVAHA]`
- Všichni konzumenti (hi-res, akumulátory statistiky, datalog, IPC, SCPI) dostanou **uměle
  vypočtenou** délku okna. Je to záměr (rozdíl proti skutečné délce ≲ 10⁻⁸), ale `gate_ps` už **není
  měřená veličina**. Cokoli, co později bude brát `gate_ps` jako čas (např. τ₀ nebo účtování času),
  zdědí chybu až 10⁻⁸. `gate2_ps` (dvoubodová délka) existuje, ale nikdo ji nečte.

### B-05 Výchozí hodnoty `want_seg` / segmenty jsou nastavené podle jedné geometrie  `[SIM]`
- Segmenty A = [G/16, 3G/16), B = [13G/16, 15G/16) jsou pevné (`top.v`). E2E test používá zkrácené
  hradlo 25 µs a stejné poměry; skutečné hradlo 0,25 s a 10⁵× delší segmenty (n až stovky tisíc)
  nebyly simulovány — **šířky akumulátorů (sy 64 b, sx 48 b, n 24 b) jsou dimenzovány úvahou**
  (max ~390 000 značek na segment při mrtvé době 80 ns), ne testem.

### B-05b Test e2e vyžadoval hradlo delší než dělení  `[SIM]`
- Po přepsání na dvoutaktové dělení `tb_regr_e2e` s hradlem 2500 taktů **nikdy neskončil** (po konci segmentu B zbývá G/16 = 156
  taktů, dělení potřebuje ~290) — chyba v testu, ne v návrhu; při hradle 0,25 s je rezerva 1,5 M taktů. Hradlo v testu zvětšeno na
  6000. Nedokumentovaná vazba: **G/16 musí být > ~300 taktů**, jinak výsledek regrese přijde až do dalšího okna (stará data).
  Zisk v tomto testu (14×) je nadsazený krátkým hradlem a malým `n`; **na desce s hradlem 0,25 s čekej výrazně méně** (A-05).

### B-06 Zobrazení a web bez vizuálního ověření  `[ÚVAHA]`
- Oprava osy Allanova grafu (`16e7a5e`) a `/api/stab` + SPA `drawStab` (IPC v20) jsou přeložené a
  prošly `tools/spa/check.py`/`audit.py`, ale **nebyly zobrazeny na displeji ani v prohlížeči**.
  Chování auto-rozsahu (hystereze, podlaha ≥ 1e-11) je ověřeno čtením kódu.
- Při editaci SPA se znovu projevila známá past (rozbitý C literál po rozpadu `\n` v heredocu,
  opraveno skriptem v souboru). Řetězec `check.py --build` + `nm SPA_HTML` je jediná obrana.

### B-07 Bitstreamy nejsou reprodukovatelně archivované
- 0x0410 (`md5 493c0f3e…`) a 0x0411 (`md5 1c6d4037…`) existují jako kopie ve scratchpadu relace;
  `impl/pnr/Counter_FPGA.fs` se přepisuje každým během. Při dalším buildu v Gowin IDE vznikne jiný
  výsledek (L-0122: IDE použije vlastní volby pinů, ne `build.tcl`). Binárky jsem do gitu nedával
  bez souhlasu.
- Programátor držel uživatelovo Gowin Programmer GUI; nahrání na desku zůstává na uživateli.

---

## 3. Nálezy — nízká závažnost / poznámky

- **C-01** Při `TDC_DUAL = 0` zůstává v `tdc_chain` nepoužívaná větev (`q_f`, `qd_f`, `dF`, …);
  syntéza hlásí varování EX1998 a logiku zahodí. Funkčně neškodné, ale šum v logu; vyčistit
  `generate` podmínkou.
- **C-02** `FPGA_REGR_MIN_N = 8` je odhad bez podkladu (nižší počet značek dává slabý průměr).
- **C-03** `feat` commity (`4667d7c`, `b761a15`, `845ee2e`) nesou i komentáře k novému kódu; samostatné
  opravy komentářů ke starému kódu se dělají zvlášť. Dodrženo v duchu, ne doslova.
- **C-04** `tools/.audit_sizes.txt` a pracovní soubory (`*.vvp`, `tools/__pycache__`) zůstávají
  neverzované — záměrně nebyly přidány.

---

## 4. Co je dobře ověřeno (a čím)

| tvrzení | důkaz |
|---|---|
| Celá regrese simulací (12 testů: coarse, gatediv, tdc, tdcinl, tdcinl_dual, regre2e, regracc, decequiv256/320, txequiv, phase, link) | `sim/run.ps1`, PASS; po poslední úpravě `regr_acc` znovu `regracc`, `regre2e`, `link`, `txequiv` = PASS (commit `bbf5865`) |
| Dekodér a TX multiplexer ekvivalentní předchozí verzi | `tb_dec_equiv`, `tb_tx_equiv` (náhodné i hraniční vektory) |
| Řetěz ALU kompletní, carry průchod, 2 × 320 stupňů | `sim/check_tdc_netlist.py` nad `Counter_FPGA.vg` |
| STM build bez varování, statická analýza beze změny | `./scripts/build.sh Release CM7` = 0 varování; `python tools/audit.py` = 92 OK / 0 / 2 |
| Offsety regresního bloku FPGA ↔ STM | bajty 68–96 = `p+56…p+84`, shodné v `spi_app.v` a `parse_data` |
| Změna je v obraze | `.text` CM7 641 208 B (Release) |

---

## 5. Doporučené pořadí dalších kroků

1. **Flash 0x0410 bez regrese** a změřit `tdc` (A-01) — bez toho je vše ostatní spekulace.
2. Rozhodnout o regresi (A-04/A-05): `REGR` jako parametr s výchozí 0, nebo odstranit.
3. Je-li regrese ponechána: zúžit mez (A-06) a ověřit zisk na nesoudělném signálu na desce.
4. Měřit miscount a spike-reject na 0x0410 (A-02), kanál B poprvé (A-01).
5. Samostatný `docs:` commit s opravou zastaralých komentářů (B-02).
6. Archivovat bitstream, který se skutečně nahrál (B-07), s hashem pro kalibraci.

---

## 6. Dodatek: první měření FW 0x0411 + STM v0.14.0 na desce (2026-10-07 večer, [HW])

Uživatel nahrál bitstream 0x0411 i STM a hlásí, že chyba je větší. Diagnostika přes COM10 (CDC) a ETH, bez zásahu do GUI programátoru.

### D-01 NOVÝ NÁLEZ (vysoká): počet hran v okně je trvale o ~0,9 % vyšší  `[HW]`
- `fpgaraw`: `edge_count` = 0x2675EF = 2 520 559 při `dt_a` = 0,250000 s (správně); očekáváno 2 500 002. Přebytek 13 800–35 700 hran
  na okno (průměr ~20 500, sd 3 300, autokorelace ≈ −0,1), takže `freq_hz` = 10 000 008,4 Hz + k·4 Hz, k ≈ 26 000.
  `freq_hz` na displeji/webu ukazuje 10,08–10,13 MHz. `CITANI HRAN` to **nezachytí** (hlídá jen ±1 hranu).
- Před nahráním (FW 0x040F) bylo `navic` 12 z 14 620 oken (0,08 %). Příčina **neznámá**: kandidáti jsou (a) 0x0410 (zdroj hran z `q_r[KD]`
  místo vzorku tapu 0), (b) 0x0411 (regrese: ~600 000 vnitřních značek/okno, `ld` přepíná každých 100 ns), (c) změna zapojení/signálu.
- `tdc` na desce: největší bin A = 311 ps (3,1 %), B = 224 ps (2,2 %); obří bin (1,4/4,8 ns) **zmizel**; 162/161 neprázdných kódů z 512,
  řetěz končí na tapu ~248 z 320. Obří-bin část 0x0410 tedy na křemíku **funguje** (A mírně nad 200 ps).
- Pozn.: test přepnutí hradla (1 s / 0,1 s / 0,25 s) nerozlišuje nic, protože hradlo FPGA se z STM **nikdy nenastavuje**
  (`fpga_freq_set_window()` nemá volajícího). Tlačítko GATE, web a SCPI mění jen softwarové průměrování; okno FPGA je vždy 0,25 s.

### D-02 NOVÝ NÁLEZ (vysoká): regresní blok na křemíku nefunguje  `[HW]`
- `regr`: 1286 oken s blokem, **použito 0, zamítnuto 1286**, `f_regr` = 0.
- Rámec: `n_A` = 312 499 (přesně počet hran za 31,25 ms, tedy A je v pořádku), `n_B` = 295 968 **ve všech oknech stejné**, `ym_B` = `0x013520C197C0`
  **bit za bitem stejné ve více oknech** (průměr ~300 000 časových značek se nemůže opakovat) a `ym_A` má nesmyslná horní slova (0x7C99…, ≈ 5,2 s místo ~0,03 s).
  `xm_B` ≈ 2,13·10⁶ místo ~2,19·10⁶ (ukazatel hran za B zaostává o ~57 000).
- Simulace to nevidí (e2e prošel). Prokazuje to A-01 (model ≠ křemík) a že `S_W`/pipeline nebyla jediná nejistota; příčina **neznámá**, kandidát: časy vnitřních značek
  (`ts_ps` při `ts_valid` u značek za sebou po 10 taktech) a/nebo `ref_ts`.

### Opravy provedené v této fázi (nic z toho není ověřeno na desce)
| nález | oprava | commit |
|---|---|---|
| A-04 regrese spolkla plochu | `TDC_REGR` parametr, výchozí 0 → **FW 0x0412**: CLS 65 % (0x0411: 79 %), Fmax 103,1 MHz, slack +0,299 ns | `0aa7d34` |
| A-06 mez 3e-7 | `FPGA_REGR_MAX_REL` 5e-8, selftest vektor 1e-7 (zamítnout) | `439bbb9` |
| B-03 roztržené čtení | `fpga_freq_regr_stat` pod PRIMASK | `439bbb9` |
| `status`: `okno %lu ps` oříznuto na 32 b (ukazovalo 891 µs místo 250 ms) | tisk v µs z 64bitové hodnoty | `439bbb9` |

### Otevřeno a postup (A/B na desce; JTAG blokuje otevřené Gowin Programmer GUI)
Připraveno v `Frequency_Counter_FPGA_Module/ab_test/`: `FW_0x040F_baseline.fs`, `FW_0x0410_bez_regrese.fs`, `FW_0x0412_REGR_vypnuta.fs`.
1. Nahrát 0x0412 → `status` (`FW:0x0412 CAPS:0x0062`), `fpgaraw` → `edge_count` = 2 500 002 ± 1? **Ano** = vinen regresní blok (0x0411). **Ne** → krok 2.
2. Nahrát 0x040F → pokud je počet hran správný, vinen je zdroj hran 0x0410 (`q_r[KD]`); pokud ne, vstup/zapojení.
3. Teprve podle výsledku opravovat; D-02 řešit jen pokud se regrese ponechá.
