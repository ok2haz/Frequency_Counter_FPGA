# Audit modulu 24 — matematické funkce měření, TŘETÍ PRŮCHOD (přesnost, platnost, rychlost)

**Datum:** 2026-09-26
**Fáze:** F3 (přezkum kódu) — **bez editace kódu**
**Commit auditované verze:** `09872e1`
**Jádro:** CM7 + CM4 (IPC snapshot, `/api/state`, SPA)
**Předchozí průchody:** [`…-mereni.md`](2026-09-26_matematika-mereni.md), [`…-mereni-2.md`](2026-09-26_matematika-mereni-2.md)

## Zadání a metoda

Na žádost uživatele („pokračuj s matematikou a její validitou a přesností /
rychlostí / optimalizací"). Tentokrát se sleduje **řetězec přesnosti** kmitočtu
od rámce FPGA ke každému konzumentovi (displej, web, SCPI, datalog, datová
cache), **výpočetní náročnost** míst, která běží často, a zbylé **předpoklady**.
Nálezy stojí na simulaci/měření; skripty v `sim/`:

| skript | co měří |
|---|---|
| [`sim/2026-09-26_kvantizace_10uHz.js`](sim/2026-09-26_kvantizace_10uHz.js) | ADEV z hodnot zaokrouhlených na 10 µHz proti přesným; šum = podlaha čítače (TDC 2,5 ns i 22 ps) |
| [`sim/2026-09-26_web_mdev_rychlost.js`](sim/2026-09-26_web_mdev_rychlost.js) | čas webové `mdev()` vytažené ze SPA proti verzi s prefixovými součty + shoda výsledků |

## Řetězec přesnosti kmitočtu (stav po F-0171..F-0179)

| místo | nese | krok | poznámka |
|---|---|---|---|
| rámec FPGA | `frequency_x100000` + `edge_count`/`gate_ns` | 10 µHz / přesně | hi-res z dvojice hrany/hradlo |
| displej, statistika | `fpga_stat_pop` (Σcyklů/Σhradel), `double` | přesně | F-0171, F-0179 |
| **IPC snapshot → web, TCP SCPI** | `freq_x100000` | **10 µHz** | F-0180 |
| **USB SCPI `MEAS:FREQ?`** | `fmt_scpi_hz_d` z `x100000` | **10 µHz** | F-0180 |
| **datalog** | `freq_x100000` (průměr za periodu) | **10 µHz** | F-0180 |
| datová cache `sdram_log` | µHz | 1 µHz | F-0181 (zatím bez konzumenta) |

---

### F-0180 [S3] Web, SCPI i datalog dostávají kmitočet jen po 10 µHz — při nižších kmitočtech zaokrouhlení převýší podlahu čítače a statistika mimo displej je vysoko nebo nulová

- **Místo:** `CM7/Core/Inc/ipc_shared.h:157-159` (snapshot nese jen `*_x100000`),
  `CM4/LWIP/App/httpd_min.c:390-396,429` (`jnum_hz` → `fmt_scpi_hz_d` = 5 desetin),
  `CM7/Core/Src/scpi.c:111-126` (`fmt_scpi_hz_d`: `%05lu`), `CM7/Core/Src/datalog.c`
  (`freq_x100000`, i po F-0172 jen s krokem 10 µHz).
- **Popis:** Displej od F-0171 počítá z přesného součtu cyklů a hradel. Všechny
  ostatní cesty ven — webový dashboard (ADEV/MDEV/drift/ℒ(f) počítané v JS),
  SCPI `MEAS:FREQ?` (pro externí software typu TimeLab) i datalog (rekonstrukce
  Allanovy pyramidy, CSV, `MMEM:DATA?`) — dostávají kmitočet zaokrouhlený na
  10 µHz. Relativně je to 1·10⁻¹² při 10 MHz, ale 1·10⁻⁸ při 1 kHz, zatímco
  podlaha čítače je relativní a na kmitočtu nezávisí (5·10⁻⁹ při 0,25 s s TDC
  2,5 ns, 4,4·10⁻¹¹ s TDC 22 ps nové desky).
- **Důkaz (simulace `sim/2026-09-26_kvantizace_10uHz.js`, signál = podlaha čítače,
  poměr ADEV zaokrouhleno/přesně):**

  | vstup | TDC 2,5 ns: τ 0,25 / 2,5 / 25 s | TDC 22 ps: τ 0,25 / 2,5 / 25 s |
  |---|---|---|
  | 10 MHz | 1,00 / 1,00 / 1,00 | 1,00 / 1,00 / 1,00 |
  | 1 MHz | 1,00 / 1,00 / 1,00 | 1,00 / 1,02 / 1,19 |
  | 100 kHz | 1,00 / 1,00 / 1,00 | 1,24 / 2,33 / 6,99 |
  | 10 kHz | 1,00 / 1,02 / 1,17 | **0 / 0 / 0** |
  | 1 kHz | 1,15 / 2,08 / 5,94 | **0 / 0 / 0** |

  „0" = šum je menší než krok, zaokrouhlené hodnoty jsou konstantní.
- **Dopad:** web a displej ukazují pro tatáž data různá čísla (L-0018); SCPI
  dává méně číslic, než ukazuje displej (7 desetin); rekonstruovaná pyramida
  z datalogu má při nízkých kmitočtech jinou podlahu než živá. S novou deskou
  se to týká už pásma pod ~1 MHz.
- **Reprodukce:** simulace; na HW `fpgasim on 1000` → porovnat σy na webu a displeji.
- **Návrh opravy (rozhodnutí — mění IPC a formát záznamu):**
  (a) snapshot nese **`double freq_hz_hires`** (CM7 spočítá z `fpga_stat`/hi-res
  dvojice) → `IPC_VERSION` 18 → 19, **flash obou bank**; `/api/state` a SCPI
  `MEAS:FREQ?` tisknou **9 desetin** (nHz, integer extrakce jako dnes);
  (b) datalog: kmitočet v **nHz** s příznakem jednotky v bitu 62 (bit 63 =
  `freq_avg` už je obsazený; 1,4 GHz = 1,4·10¹⁸ nHz < 2⁶²) — staré záznamy dál
  x1e5; čtečky dostanou přesnou hodnotu přes jednu funkci.
  Obojí je formát/rozhraní → skupina B.
- **Riziko opravy:** střední (IPC bump, formát datalogu, JSON kontrakt — `json_kontrakt.py`).
- **Vztah k lekcím:** L-0094 (přesnost vůči VARIACI), L-0018, L-0012 (displej opravený, dvojčata ne).
- **Stav:** otevřeno.

---

### F-0181 [S4] Datová cache `sdram_log` ukládá µHz — pro novou desku při nízkých kmitočtech hrubě

- **Místo:** `CM7/Core/Src/freertos_task_fpga.c` (`fpga_freq_hires_uhz` → `sdram_log_put`),
  `CM7/Core/Inc/sdram_log.h` (`f_uhz`).
- **Popis:** 1 µHz je při 1 kHz relativně 10⁻⁹ — nad podlahou nové desky
  (4,4·10⁻¹¹ při 0,25 s) i dnešní při delších τ. Cache zatím **nemá konzumenta**
  (plán: přesné MTIE/TIE a plně překryvná ADEV, bod 5), takže jde o latentní
  vadu vstupních dat budoucí analýzy. Záznam má 32 B s polem `reserved` — místo
  na přesnější reprezentaci (např. surové `edges·mul` a `gate_ns`, tedy přesně).
- **Návrh:** ukládat přesnou dvojici (cykly, hradlo) místo zaokrouhleného µHz —
  rozhodnout spolu s bodem 5 (skupina C, až bude konzument).
- **Stav:** otevřeno.

---

### F-0182 [S4] Webová `mdev()` je O(N²) a běží ~4× za sekundu — prefixové součty ji zrychlí 18× se stejnými výsledky

- **Místo:** `CM4/LWIP/App/httpd_min.c` SPA — `mdev()` (vnitřní smyčka přes m),
  volaná z `noiseDesc` VŽDY a z `stabPoints` u metriky TDEV; `drawStab` běží při
  každé zprávě SSE (~4×/s), buffer `MAXM = 2000` měření.
- **Důkaz (`sim/2026-09-26_web_mdev_rychlost.js`, funkce vytažená ze SPA):**
  N = 2000: **2,56 ms** proti **0,14 ms** (18×) na PC, výsledky i počty členů
  shodné (rel. rozdíl < 10⁻⁹). Na mobilu typicky 5–10× pomaleji → desítky ms
  na zprávu jen za MDEV. Navíc se `adev()` počítá dvakrát (`lastAdev` a
  `stabPoints`).
- **Návrh (skupina A):** MDEV přes prefixové součty fáze
  (`S_j = ΔC(3m) − 2ΔC(2m) + ΔC(m)`), jeden výpočet ADEV sdílený; do
  `stat_test.js` kontrola shody s referencí (už existuje) + rychlostní případ.
- **Stav:** otevřeno.

---

### F-0183 [S4] Pyramida se nenuluje při změně τ0 v rámci téže dekády kmitočtu — při nízkých kmitočtech mísí vzorky různé délky

- **Místo:** `CM7/app/screens/screen_main.c` — reset statistiky jen při změně
  počtu celých číslic / REAL↔SIM (`freq_advance`); `tau0_scale()` bere EMA τ0.
- **Popis:** Od #27 mají vzorky délku K·hradlo. Nad ~1 kHz je to vždy ~1,000 s,
  pod ~100 Hz ale délka závisí na kmitočtu (hradlo se protahuje až o periodu).
  Změní-li se vstup v rámci téže dekády (40 Hz → 60 Hz), pyramida dál sčítá
  vzorky s jiným τ0 a `tau0_scale` ukazuje jejich průměr.
- **Návrh (skupina A):** vynulovat statistiku, když se τ0 nového vzorku liší od
  průměru o víc než 2 % (a hlásit to v `status full`).
- **Stav:** otevřeno.

---

## Ověřeno a v pořádku (neotevírat)

- **Výpočetní náročnost firmwaru:** ADEV/MDEV/HDEV nad ringy stage (M ≤ 24,
  m ≤ 5) jsou stovky operací na bod, i s klasifikací typu šumu (bod 4) jde
  o jednotky tisíc operací na render 1×/s — zanedbatelné. `stats_adev(1)` nad
  120 vzorky se volá několikrát za sekundu, O(120). Hi-res dělení headline
  (7 desetin dlouhým dělením) a akumulátor měření jsou O(1).
- **Přesnost ve firmwaru:** statistika v `double` (F-0179), headline z dvojice
  hrany/hradlo (7 desetin), perioda z dvojice (`mp_period_sample_s`), Welford
  v `double`, proklad centrovaný (F-0169), ℒ(f) po odečtu průměru v `double`.
- **Platnost:** EDF podle typu šumu (Monte Carlo do ~12 %), podlaha čítače
  (simulace do 0,6 %), τ0 z počtu měření (simulace).

## Shrnutí

**Verdikt: podmíněně funkční** (beze změny). 4 nálezy: **1× S3, 3× S4**.
Displej je po předchozích průchodech na plné přesnosti; **všechny cesty ven
(web, SCPI, datalog) jsou o řády hrubší** — to je hlavní zbývající rozdíl mezi
tím, co přístroj změří, a tím, co z něj dostane uživatel mimo displej.

## Návrh triáže

| skupina | nálezy | proč |
|---|---|---|
| **A — opravit hned** | F-0182 (web mdev), F-0183 (reset při změně τ0) | lokální, bez změny rozhraní |
| **B — rozhodnout** | F-0180 (IPC `double` + 9 desetin v JSON/SCPI; datalog nHz s příznakem) | mění IPC (`IPC_VERSION`, flash obou bank) a formát záznamu |
| **C — odložit** | F-0181 (`sdram_log` přesná dvojice) | až s konzumentem (bod 5) |
