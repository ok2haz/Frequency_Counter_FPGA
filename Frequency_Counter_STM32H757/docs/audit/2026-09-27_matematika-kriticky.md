# Audit modulu 24 — matematika měření, KRITICKÝ PRŮCHOD

**Datum:** 2026-09-27
**Fáze:** F3 (přezkum kódu) — **bez editace kódu**
**Commit auditované verze:** `a30e2a4`
**Jádro:** CM7 + CM4 (SPA) + zdroj FPGA (`../Frequency_Counter_FPGA_Module/src/`) jako důkaz kontraktu
**Předchozí průchody:** [`…-mereni.md`](2026-09-26_matematika-mereni.md), [`…-2.md`](2026-09-26_matematika-mereni-2.md), [`…-3.md`](2026-09-26_matematika-mereni-3.md)

## Zadání a metoda

Na žádost uživatele („proveď kritický audit matematiky"). Tentokrát se nehledá, co
chybí, ale **co je špatně v tom, co už je hotové** — včetně vlastních oprav
z posledních dvou dnů (F-0171…F-0185, hustota Allanova grafu), které na desce ještě
neběžely. Každá změna se posuzuje otázkou „za jakých podmínek dá věrohodně vypadající
špatné číslo". Tvrzení stojí na kódu a simulacích; kde šlo o chování FPGA, čte se
přímo jeho zdroj (`spi_app.v`), ne dokumentace.

| skript (`sim/`) | co měří |
|---|---|
| [`2026-09-27_gate_floor.js`](sim/2026-09-27_gate_floor.js) | model čítače podle `spi_app.v`: chyba hi-res kmitočtu z hradla zaokrouhleného dolů na ns |
| [`2026-09-27_mdev_pyramida.js`](sim/2026-09-27_mdev_pyramida.js) | MDEV z decimační pyramidy proti přesnému MDEV z plných dat; kontrola ADEV/HDEV |
| [`2026-09-27_reset_fronta.js`](sim/2026-09-27_reset_fronta.js) | kolik vzorků starého signálu projde do pyramidy vynulované při změně signálu |
| [`2026-09-27_edf_ring60.js`](sim/2026-09-27_edf_ring60.js) | platnost vzorců EDF pro ring 60 a m = 1…9 (Monte Carlo) |

---

### F-0186 [S2] Hi-res kmitočet dělí hradlem, které FPGA ZAOKROUHLUJE DOLŮ na celé ns — systematická chyba 0 až +2·10⁻⁹ ve všem, co z něj vychází

- **Místo:**
  - FPGA: `../Frequency_Counter_FPGA_Module/src/spi_app.v:507`
    `gate_ns <= ({30'd0, dt_q} * 64'd5) >> 1;` — Δt v ticích 2,5 ns × 2,5, posun
    vpravo = **floor**; u lichého `dt_q` chybí 0,5 ns. Autoritativní kmitočet se přitom
    počítá z **přesného** `dt_q` se zaokrouhlením (`spi_app.v:515`).
  - STM: `CM7/Core/Src/fpga_freq.c:547` (`fpga_freq_hires_hz`), `:535`
    (`fpga_freq_hires_uhz` → `sdram_log`), `:593` (`fpga_acc_add` sčítá `gate_ns`
    → vzorky statistiky i datalog), `CM7/app/screens/screen_main.c:742`
    (`freq_frame_to_lsb` → velké číslo, 7 desetin), `CM7/Core/Src/meas_present.c:43`
    (`mp_period_sample_s` → perioda). Všechny dělí `edges·mul` přímo `gate_ns`.
- **Popis:** STM bere `gate_time_ns` jako přesné Δt. Není — je to floor tickového Δt.
  Pravidlo, jak to napravit, je přitom **sepsané** v `FPGA_PROTOCOL_V2_NAVRH.md:154-156`
  („`dt_ticks = round(dt_ns·2/5)` — bezztrátová rekonstrukce ticků; … `dt_ns` je floor,
  jinak systematika ~−1e-9") — ale driver ho neimplementuje. Emulátor `fpgasim`
  vyrábí `gate_ns` jako libovolné celé ns (`fpga_freq.c:314`), floor nemodeluje, takže
  vadu **nemůže odhalit žádný test** a všechny dosavadní ověřovací běhy ji minuly.
- **Důkaz (simulace `sim/2026-09-27_gate_floor.js`, model čítače podle `spi_app.v`):**

  | vstup | x1e5 z FPGA | hi-res dnes | hi-res z ticků |
  |---|---|---|---|
  | 10 MHz asynchronní (12 % lichých dt) | +5·10⁻¹² | **+2,5·10⁻¹⁰** | +5·10⁻¹² |
  | 10 MHz koherentní s referencí | 0 | 0 | 0 |
  | sken 10 MHz ± 5 Hz (201 kmitočtů) | — | **−5·10⁻¹¹ … +2,0·10⁻⁹** | ±3·10⁻¹⁰ (šum MC) |

  Podíl lichých `dt` (a tedy velikost chyby) závisí na přesném kmitočtu — pro asynchronní
  signál může být kdekoli mezi 0 a +2·10⁻⁹. Průměrování ji **neodstraní** (Σcyklů/Σhradel
  má tutéž relativní chybu), takže roste její váha právě v dlouhých průměrech.
- **Dopad:** u 10 MHz až **20 mHz** systematicky v hodnotě, kterou přístroj zobrazuje na
  7 desetin (100 nHz) a od F-0180 posílá SCPI/webem/do datalogu s 15 platnými číslicemi.
  Hi-res je tak u asynchronního signálu **horší** než obyčejné `x100000` (to má jen
  zaokrouhlení 10 µHz). Týká se offsetu, driftu, Math/limitů, rozpočtu nejistoty (chyba
  v něm není), datalogu i periody. ADEV skoro ne (konstantní posun se v rozdílech odečte).
- **Reprodukce:** na HW s reálnou FPGA: `status`/`freq` proti `fpgaraw` — `freq_x100000`
  vs. `edges·4·1e9/gate_ns`; nebo `fpgasim` upravený na floor.
- **Návrh opravy (A):** ve všech pěti místech dělit **rekonstruovanými ticky**:
  `ticks = (gate_ns·2 + 2) / 5` (celočíselně = round(gate_ns·2/5)),
  `Δt = ticks · FREQ_TDC_PS` — jedna sdílená funkce (`fpga_freq_dt_ps()`), aby se
  pravidlo nerozešlo; `fpgasim` musí `gate_ns` vyrábět stejně jako FPGA (floor z ticků),
  jinak test vadu zase neuvidí. ⚠️ Nová deska (carry chain) má jiný tick → protokol v2
  musí nést Δt v ticích nebo ps, ne floor v ns.
- **Vztah k lekcím:** L-0012 (dvojčata), L-0018, L-0094 (přesnost vůči variaci).
- **Stav:** otevřeno.

---

### F-0187 [S3] MDEV a TDEV z decimační pyramidy nejsou standardní MDEV — u fázového šumu 3× až 10× vysoko a bílý PM se klasifikuje jako blikavý

- **Místo:** `CM7/app/screens/screen_main.c:1653` (`adev_ring_kind`, větev MDEV nad
  ringem stage), `:1940` (`adev_floor_base` MDEV s `m` stage), `adev_points`/`noise_desc`
  (klasifikace typu šumu ze sklonu MDEV), TDEV = τ·MDEV/√3.
- **Popis:** Stage s drží průměry kmitočtu po 10ˢ s, tedy **fázi podvzorkovanou po 10ˢ s**.
  MDEV nad ní průměruje m bodů fáze po 10ˢ s, standardní MDEV (τ0 = 1 s) průměruje
  n = m·10ˢ bodů po 1 s. ADEV a HDEV potřebují jen průměry kmitočtu přes τ, takže jim
  podvzorkování nevadí — MDEV ano, a nejvíc u fázového šumu, tedy přesně tam, kde má
  bílý a blikavý PM rozlišovat. Komentář u `adev_floor_base` to zná („pyramida počítá
  MDEV nad průměry stage, proto m a ne τ/1 s"), ale jen jako důvod pro podlahu; že pak
  nejde o MDEV, nikde nestojí.
- **Důkaz (`sim/2026-09-27_mdev_pyramida.js`, 200 000 vzorků, poměr pyramida/přesně):**

  | šum | stage 0 (τ 1–9 s) | stage 1 (τ 10–90 s) | stage 2 (τ 100–900 s) |
  |---|---|---|---|
  | bílý PM | 1,00 | **3,2** | **10** |
  | blikavý PM | 1,00 | 1,1–1,9 | 1,2–2,3 |
  | bílý FM | 1,00 | 1,0–1,4 | 1,0–1,4 |

  Sklon MDEV (body 1-2-5, τ 1…500 s) u bílého PM: pyramida **−1,06 → „blikavý PM"**,
  přesně −1,50 → „bílý PM" (práh −1,25). Kontrola: ADEV i HDEV z pyramidy sedí na ±3 %.
- **Dopad:** křivky MDEV/TDEV mají na hranici každé dekády skok o √10 (s hustotou 9 bodů
  na dekádu je to viditelná pila), typ šumu „bílý PM" (typicky podlaha čítače) se hlásí
  jako blikavý, EDF v pásu nejistoty bere špatné α. Web počítá MDEV z plných dat, takže
  **displej a web ukazují pro tatáž data různé MDEV/TDEV** (L-0018).
- **Návrh (B — rozhodnout):** (a) paralelní pyramida **průměrů fáze** po blocích 10ˢ:
  průměr fáze přes n = m·10ˢ bodů je průměr m blokových průměrů, takže MDEV jde spočítat
  **přesně** (s posunem po 10ˢ místo po 1 s); cena ~2,9 kB RAM a druhý feed;
  (b) MDEV/TDEV počítat jen ze stage 0 (τ < 10 s) a nad tím je nezobrazovat; typ šumu
  určovat ze stage 0 + ADEV; (c) nechat, ale přejmenovat a nepoužívat pro klasifikaci.
  Doporučuji (a) — MDEV je jediný rozlišovač bílého a blikavého PM.
- **Vztah k lekcím:** L-0088 (estimátor ověřovat nezávislou referencí), L-0018.
- **Stav:** otevřeno.

---

### F-0188 [S3] Nulování statistiky při změně signálu (F-0183) nezahodí vzorky starého signálu ve frontě — první 1–2 vzorky nové pyramidy jsou cizí a doživají v ní až týdny

- **Místo:** `CM7/app/screens/screen_main.c:1123-1125` (detekce změny → `screen_main_stats_reset`),
  `:1577` (`screen_main_stats_reset` nesahá na frontu `fpga_stat` ani na rozpracovaný
  akumulátor), `CM7/app/app_gpsdo.c:8393-8394` (`fpga_stat_pop` → `stats_sample_hz`
  bez kontroly), `CM7/Core/Src/fpga_freq.c:570` (vzorek skládaný přes hranici změny).
- **Popis:** Detekce běží na posledním měření (~20 Hz), vzorky statistiky se odebírají
  1×/s z fronty, kterou plní FpgaTask. V okamžiku nulování ve frontě leží hotový vzorek
  starého signálu a rozpracovaný vzorek je směs obou. Oba se po nulování vloží do nové
  pyramidy s y ≈ Δf/f proti NOVÉMU nominálu.
- **Důkaz (`sim/2026-09-27_reset_fronta.js`, 2000 přepnutí 10 → 12 MHz v náhodné fázi):**
  cizí vzorky v nové pyramidě: 0 ve **7** případech, 1 v 1282, 2 v 711; |y| až **0,167**.
  Decimací se vzorek propíše do všech stage s vahou 1/10ˢ a drží se po dobu ringu:
  stage 1 600 s, stage 2 1,7 h, stage 3 17 h, stage 4 7 dní (tam pořád ~1,7·10⁻⁵).
  Totéž plochý ring (σ@1s, histogram, 120 s) a trend pyramida (až 97 dní).
- **Dopad:** po přepnutí měřeného zdroje bez výpadku signálu je dlouhé τ ADEV a trend na
  hodiny až dny nesmyslné. F-0183 tu cestu otevřel pro změny uvnitř dekády; pro změnu řádu
  a /4↔/16 platila i dřív (od fronty #27).
- **Návrh (A):** při odběru z fronty zahodit vzorek, jehož kmitočet se liší od reference
  signálu (`s_freq_ref_hz`) o víc než týž práh 10⁻⁴ — jedna podmínka, pokryje hotové
  i smíšené vzorky a nezávisí na časování tasků. Alternativa: `fpga_stat_break` +
  vyprázdnění fronty přes požadavek z UiTasku do FpgaTasku.
- **Vztah k lekcím:** L-0095 (důsledek sdílí podmínku s příčinou), L-0092.
- **Stav:** otevřeno.

---

### F-0189 [S3] Rekonstrukce pyramidy z datalogu slepuje záznamy přes časové mezery a z jiného signálu

- **Místo:** `CM7/app/app_gpsdo.c:8331-8346` (smyčka rekonstrukce), `:8296` (`seed_worth_it`),
  `:8343` (jediná podmínka na signál: nominál > 0).
- **Popis:** Záznamy se sypou do stage 1 jeden za druhým. Nekontroluje se:
  (1) **souvislost v čase** (`t_unix`) — přeskočené záznamy (bez linku, `freq_avg = 0`
  při výpadku signálu, vypnutý log) a celé mezery mezi sezeními (vypnutý přístroj, dny)
  se slepí, jako by šly po sobě; ADEV ale vyžaduje souvislá, rovnoměrná data a mezi
  sezeními leží náběh OCXO a stárnutí; (2) **stejný signál** — záznam se vloží, kdykoli
  je znám nějaký nominál, i kdyby log patřil jinému zdroji. Nominál může být i ze SIM
  fallbacku (FPGA ještě neodpovídá) — pak první reálné měření (F-0183) celou rekonstrukci
  smaže, nebo naopak rekonstrukce proběhne proti nominálu jiného signálu.
  Drobnost: během rekonstrukce (~minuty) přibývají nové záznamy a index „od nejnovějšího"
  se posouvá → každý nový záznam způsobí jeden zdvojený (≈ 50 z 240 000).
  Živá cesta přitom při výpadku signálu pyramidu **nuluje** (REAL→SIM) — rekonstrukce
  se tedy chová jinak než živé měření.
- **Důkaz:** statický (smyčka čte jen `freq`, `flags`, `freq_avg`, nominál; `t_unix` ani
  kmitočet proti referenci se nečtou).
- **Dopad:** dlouhé τ (10³–10⁵ s, stage 3–5, s ringem 60 až 69 dní dat) po restartu
  obsahují náběhy a skoky předchozích sezení — věrohodně vypadající „flicker/drift".
- **Návrh (B — rozhodnout):** sypat jen **poslední souvislý úsek** (zpětně od nejnovějšího
  záznamu, konec u první mezery > 2 periody nebo u změny kmitočtu > 10⁻⁴), a jen když
  je od něj do teď krátká mezera (warm reset); spustit až po prvním reálném měření
  (reference známá). Politika (jak dlouhá mezera je ještě „souvislá") je rozhodnutí.
- **Vztah k lekcím:** L-0092 (okno průměrování/rozestup), L-0095.
- **Stav:** otevřeno.

---

### F-0190 [S3] Web: Allan v záložním 1 Hz pollu (a po výpadku SSE) počítá z nesouvislých vzorků — mrtvá doba F-0171 na webu

- **Místo:** `CM4/LWIP/App/httpd_min.c:2607-2609` (SPA: vzorek do `M` při JAKÉKOLI změně
  `seq_meas`), SSE `:4109` (jen tam chodí každé měření).
- **Popis:** V režimu SSE přijde každé měření (~4/s) a vzorky navazují. SPA má ale záložní
  1 Hz poll (bez SSE, přes proxy, po vyčerpání spojení); pak dostane jen poslední měření
  za sekundu — vzorek 0,25 s s rozestupem ~1 s, tedy mrtvá doba 75 %. `seq_meas` se kontroluje
  jen na změnu, ne na souvislost (`seq === lastSeq + 1`), takže to SPA nepozná. Totéž po
  přerušení SSE.
- **Důkaz:** statický + kvantifikace z F-0171 (`sim/2026-09-26_mrtva_doba.js`): ADEV 2×
  (bílý FM) až 23× (bílý PM) vysoko a šum dostane sklon bílého FM.
- **Návrh (A):** při `seq_meas !== lastSeq + 1` buffer `M` zahodit (stejně jako při změně
  brány) a v poll režimu Allan/drift/ℒ(f) nepočítat, s důvodem v `aWarn`.
- **Vztah k lekcím:** L-0092, L-0012 (oprava displeje, dvojče web ne).
- **Stav:** otevřeno.

---

### F-0191 [S4] Pás nejistoty Allanova grafu blenduje sloupce ve vrcholech dvakrát — s 9 body na dekádu viditelné pruhy

- **Místo:** `CM7/app/screens/screen_main.c:2163-2170` (`allan_band_fill`: každý úsek kreslí
  sloupce `c = 0…cols` včetně obou krajů).
- **Popis:** Koncový sloupec úseku i je počáteční sloupec úseku i+1 → poloprůhledná výplň
  (α 0x22) se tam nanese dvakrát, u bodů na stejném pixelu třikrát. Při 18 bodech to byly
  nenápadné čárky, při 54 (hustota 9) je v kartě každý třetí sloupec pásu tmavší.
- **Návrh (A):** v úsecích i ≥ 2 začínat od `c = 1`, bod na stejném pixelu přeskočit.
- **Stav:** otevřeno.

---

### F-0192 [S4] Detekce změny signálu (F-0183) porovnává `x100000` — pod ~0,1 Hz je jeden LSB větší než práh 10⁻⁴

- **Místo:** `CM7/app/screens/screen_main.c:1123` (`hz_now` z `x100000`).
- **Popis:** LSB 10 µHz je relativně 10⁻⁵/f. Dnešní deska má předdělič /4 a strop okna
  21,5 s, takže f_min ≈ 0,19 Hz a LSB 5,3·10⁻⁵ (rezerva 2×). Nová deska měří přímo
  (f_min ≈ 0,047 Hz, LSB 2,1·10⁻⁴) → každé překlopení zaokrouhlení by statistiku nulovalo.
- **Návrh (A/C):** porovnávat `fpga_freq_hires_hz()` (po F-0186 z ticků), s `x100000`
  jen jako zálohou; do té doby latentní.
- **Stav:** otevřeno.

---

## Ověřeno a v pořádku (neotevírat)

- **EDF v novém rozsahu** (ring 60, m = 1…9): vzorce Howe–Allan–Barnes proti Monte Carlu
  do **15 %** (nejhůř blikavý PM m = 1), tedy ve stejné toleranci jako dosud
  (`sim/2026-09-27_edf_ring60.js`).
- **ADEV a HDEV z pyramidy** = přesné na ±3 % (kontrola v `sim/2026-09-27_mdev_pyramida.js`).
- **Nejistota na webu nezávisí na hustotě bodů:** `sigmaAtTau(gate)` se ptá na τ = hradlo,
  což je první bod mřížky v každé hustotě.
- **SSE doručí každé měření:** `httpd_min_poll` ~20 Hz, měření po 250 ms.
- **`fmt_scpi_hz_sig`**: ≤ 8,4·10⁻¹⁵ na 0,5 Hz…3,9 GHz (`sim/2026-09-27_fmt_hz_sig.js`).
- **ℒ(f) z průměrů kmitočtu:** odezva průměrujícího čítače (sinc²) dává na 0,1 Hz
  −0,14 dB, kde čte okno ANALÝZA; u Nyquista −3,9 dB. Je to vlastnost čítače, ne chyba
  výpočtu — kdyby se ℒ(f) mělo číst až k Nyquistovi (web), patří k tomu korekce nebo
  poznámka, ne oprava.
- **Index ringu a sousedé EDF** po zhuštění (`sim/2026-09-27_allan_hustota.js`).

## Shrnutí

**Verdikt: podmíněně funkční.** 7 nálezů: **1× S2, 4× S3, 2× S4.**
Nejzávažnější je **F-0186**: hi-res kmitočet — tedy číslo, kterým se přístroj chlubí
(7 desetin na displeji, 15 platných číslic ven) — nese u asynchronního signálu
systematickou chybu až 2·10⁻⁹, protože FPGA posílá hradlo zaokrouhlené dolů a STM ho
bere jako přesné; pravidlo, jak to napravit, je sepsané v návrhu protokolu, jen
neimplementované, a emulátor vadu maskuje. **F-0187** zpochybňuje MDEV/TDEV nad 10 s
a tím rozlišení bílého/blikavého PM. **F-0188** a **F-0189** jsou hraniční případy
životního cyklu statistiky (přepnutí signálu, restart), které po mých opravách F-0183
a #27 zůstaly otevřené.

## Návrh triáže

| skupina | nálezy | proč |
|---|---|---|
| **A — opravit hned** | F-0186 (Δt z ticků + věrný emulátor), F-0188 (filtr vzorků po nulování), F-0190 (web: souvislost `seq_meas`), F-0191 (pás), F-0192 (hi-res v detekci) | lokální, jednoznačné |
| **B — rozhodnout** | F-0187 (MDEV: fázová pyramida / jen stage 0 / přejmenovat), F-0189 (politika souvislosti rekonstrukce) | návrh výpočtu a politika |
| **C — odložit** | — | |
