# Audit modulu 24 — matematické funkce měření (průřezový)

**Datum:** 2026-09-26
**Fáze:** F3 (přezkum kódu) — **bez editace kódu**
**Jádro:** CM7 (+ sdílený `scpi.c` a `httpd_min.c` na CM4 u převodu dBm)
**Branch:** `audit/2026-09-09-hodiny-pwr`

## Proč tenhle modul vznikl

Na žádost uživatele („audit, kontrola, optimalizace kódu a aktualizace
komentářů s ohledem na matematické funkce měření"). Matematika měření je
rozprostřená přes moduly 9, 11, 17 a 20 a nikdy nebyla auditovaná jako celek —
tedy s otázkou, jestli jsou **vzorce správně**, jestli **numerika drží** a jestli
**zobrazená čísla znamenají to, co tvrdí popisek**. Podle pravidel 3 a 4 je tohle
jen F3; optimalizace (F5) a komentáře (F6) až po triáži.

## Rozsah — co bylo čteno

| soubor | úsek | co obsahuje |
|---|---|---|
| `CM7/Core/Src/meas_math.c` | celý (75 ř.) | Math Mx+B, NULL, limity |
| `CM7/Core/Src/phase_noise.c` + `.h` | celé | FFT, PSD, ℒ(f) |
| `CM7/Core/Src/meas_present.c` + `.h` | celé (logika, selftest zběžně) | Welford, filtry, rozpočet nejistoty, proklad |
| `CM7/app/screens/screen_main.c` | 700–1100, 1150–1820, 2850–2990 | hi-res dlouhé dělení, nejisté číslice, vzorkování, ADEV/MDEV/HDEV/TDEV/MTIE, pyramidy, konfidenční pás, `fmt_frac`, histogram |
| `CM7/app/app_gpsdo.c` | 380–490, 2055–2120, 4350–4460, 4895–5100, 8253–8315 | `fmt_fixed`/`fmt_hz`/`fmt_sdec`, self-survey, okno ANALÝZA, vzorkování statistiky |
| `CM7/Core/Src/fpga_freq.c` | 497–560 | `fpga_freq_hires_mul`, `hires_uhz` |
| `CM7/Core/Src/rtc.c` | 370–530 | disciplinace LSE |
| `CM7/Core/Src/scpi.c`, `ipc.c`, `calib.c`, `datalog.c`, `freertos_task_ui.c`, `CM4/LWIP/App/httpd_min.c` | cílené úseky | převody dBm, meze vstupu, kadence vzorkování |

**Buffery v `.map`:** modul nezavádí DMA ani sdílené buffery; velké pracovní pole
jsou `static` (`pn_selftest`, `screen_main_phase_noise` — ověřeno ve zdroji).
`pn_compute` má `re[64]`/`im[64]` na zásobníku (1 KB), volá se jen z UiTasku
(volno 5520 B dle `status` 2026-09-25).

**Metoda nad rámec čtení:** tři nálezy stojí na **výpočtu, ne na úsudku** —
únik DC Hannovým oknem (DFT obou variant okna), regulační smyčka LSE (simulace
přesně podle aritmetiky kódu) a počty členů overlapping estimátorů (indexy
dosazené do NIST SP1065). Výsledky jsou citované u nálezů.

---

### F-0158 [S2] Disciplinace LSE nekonverguje — běžící průměr míchá okna z různých kalibrací a smyčka osciluje ±10 ppm

- **Místo:** `CM7/Core/Src/rtc.c:480` (běžící průměr), `:484-488` (skládání
  korekce), `:393-398` (`rtc_lse_reset`).
- **Popis:** Drift krystalu se měří v 8min oknech a průměruje běžícím průměrem
  `s_lse_ppm_avg`. Od 12 oken se nejvýš 1×/h zapíše korekce vzorcem
  `rtc_lse_apply_calib(s_lse_ppm_avg - s_lse_cal_ppm)` s komentářem
  *„Korekce se SKLÁDÁ: CALR už nějakou drží a průměr měří drift PO ní."*
  To platí **jen tehdy, když průměr obsahuje výhradně okna naměřená pod
  aktuální kalibrací**. Průměr se ale po zápisu korekce **nenuluje** — takže po
  první korekci obsahuje směs oken sprzed ní (měří `D`) a po ní (měří `D + C`),
  a vzorec odečte jen tu nejnovější `C`.
- **Důkaz:**
  - `rtc_lse_reset()` (jediné místo, které nuluje `s_lse_n`/`s_lse_ppm_avg`)
    volá **jen UART příkaz** — `grep` vrací jediného volajícího
    `freertos_task_uart.c:1956`. `rtc_lse_apply_calib` ani `rtc_try_sync`
    průměr nenulují (`rtc_try_sync:519` nuluje jen fázovou referenci).
  - **Simulace přesně podle aritmetiky kódu** (okno 8 min každých 10 min kvůli
    resyncu, zápis od 12 oken a nejvýš 1×/h, kvantizace 0,95367432 ppm, strop
    511, krystal se skutečným driftem +10 ppm, bez šumu):

    | čas | zbytek podle kódu | s nulováním průměru po korekci |
    |---|---|---|
    | 3 h | −6,21 ppm | +0,46 ppm |
    | 6 h | −10,03 ppm | +0,46 ppm |
    | 12 h | +6,19 ppm | +0,46 ppm |
    | 24 h | −3,35 ppm | +0,46 ppm |
    | 96 h | +4,28 ppm | +0,46 ppm |

    Kód **neustálí ani po 4 dnech** a zbytek je řádově stejný jako drift bez
    jakékoli korekce. S nulováním se ustálí okamžitě na kvantizační mez
    (±0,48 ppm).
- **Dopad:** Při dostupné GPS to skryje resync každých 10 min (chyba ≤ 6 ms).
  Projeví se to **v holdoveru** (ztráta GPS): RTC poběží s náhodně nastavenou
  korekcí — a `RTC_CALR` žije v backup doméně, takže **špatná korekce přežije
  i reset** (`CLAUDE.md`, sekce „Disciplinace LSE"). Druhotně: `rtc_lse_ppm()`,
  který UART `rtc cal` hlásí jako „drift krystalu", je průměr přes různé
  kalibrace, tedy **není drift ani zbytek** — diagnostika, která tvrdí něco,
  co neměří (L-0011).
- **Reprodukce:** staticky + simulací (viz Důkaz). Na HW: `rtc cal` po ≥ 2 h
  s GPS a pak sledovat `rtc_lse_calib_ppm()` přes několik hodin — podle kódu
  se bude přelévat, místo aby se ustálilo.
- **Návrh opravy:** Po **úspěšném** zápisu v `rtc_lse_apply_calib` vynulovat
  `s_lse_n`, `s_lse_ppm_avg` a fázovou referenci (`s_lse_have_ref = 0`), aby
  další okno začalo až pod novou kalibrací. ⚠️ `RTC_CALR` se uplatní až od
  dalšího 32s cyklu — první okno po zápisu by mělo začít až po něm (jinak
  prvních ≤ 32 s běží pod starou hodnotou; při 480s okně je to ≤ 7 % okna).
  Pak platí i komentář o skládání.
- **Riziko opravy:** nízké — dvě přiřazení v jedné funkci, chování ověřené
  simulací; ⚠️ mění ale regulační smyčku, takže ověřit na HW (viz Reprodukce).
- **Vztah k lekcím:** **L-0011** (diagnostika tvrdí, co neměří), **L-0028**
  (komentář „průměr měří drift PO ní" popisuje obranu, kterou kód nedělá),
  **L-0069** (periodický plán — příbuzné: stav akumulátoru musí odpovídat
  režimu, ve kterém vznikl).
- **Stav:** otevřeno.

---

### F-0159 [S3] Okno ANALÝZA počítá „Nejistotu U" a „Platných cifer" z hradla, které si vymyslelo — a to ŽIVĚ, dokud neběží FPGA

- **Místo:** `CM7/Core/Src/meas_present.c:187` (`if (gate_s <= 0.0) gate_s = 1.0;`),
  `CM7/app/app_gpsdo.c:5016-5023` (volání), `:5032` (U), `:5068-5072` (číslice).
- **Popis:** Když měření neběží (SIM / bez linku), je `gate = 0`. Řádek
  „rozlišení" to poctivě přizná (`-- (bez mereni)`, ř. 5041-5048). Jenže
  `mp_budget` při `gate ≤ 0` **tiše dosadí 1 s** a z tohoto vymyšleného hradla
  spočítá rozlišovací člen, který jde do `u_tot` — a z něj se zobrazí
  **„Nejistota U: +-X Hz (…, k=2)"** i **„Platných cifer: N"**. Řádek číslic
  k tomu vytiskne `hradlo 0,0 s`, tedy číslo odvozené z 1 s vedle údaje 0 s.
  Druhá, drobnější vada v témže řádku: hradlo se formátuje na **1 desetinu**
  s komentářem *„Hradlo je z pevné sady 0,1/1/10/100 s"* (ř. 5069-5070). To
  platilo, dokud se bralo nastavení z UI; od opravy #83 jde o **skutečné**
  hradlo z rámce (~0,25 s), takže tentýž údaj je v jednom okně jednou
  `0,250 s` (ř. 5043) a jednou `0,3 s`.
- **Důkaz:** `meas_present.c:187`; komentář `app_gpsdo.c:5014-5015` tvrdí
  *„gate = 0 a rozpočet to prizna"* — přizná to jen jeden ze tří řádků,
  které z rozpočtu čerpají. `status` z 2026-09-25 hlásí `FPGA: link NOLINK`,
  takže **tahle cesta běží teď**.
- **Dopad:** Uživatel vidí rozšířenou nejistotu a počet platných číslic, které
  **nejsou z ničeho změřené**, a navíc ve stejně důvěryhodné podobě jako
  změřené. Přesně ta vada, kvůli které vznikla oprava řádku rozlišení —
  jen se nepřenesla na sourozence.
- **Reprodukce:** okno ANALÝZA bez běžícího FPGA linku (dnešní stav desky).
- **Návrh opravy:** Při `gate <= 0` zobrazit u „Nejistota U" i „Platných cifer"
  `--` stejně jako u rozlišení. Čistší varianta: `mp_budget` hradlo **nedosazuje**
  a vrací příznak platnosti (`o->valid`), aby volající nemohl zapomenout.
  Formát hradla v řádku číslic sjednotit s řádkem rozlišení (3 desetiny) a
  opravit zastaralou premisu v komentáři.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0012** (oprava řádku rozlišení se nepřenesla na dva
  sousední řádky téhož okna), **L-0028** (komentář slibuje, že to rozpočet
  přizná), **L-0018** (tentýž údaj ve dvou formátech).
- **Stav:** otevřeno.

---

### F-0160 [S3] Limitní tester propustí NaN jako PASS — a NaN je dosažitelné třemi SCPI příkazy; SCPI formátovač je proti NaN ošetřený, jeho UI dvojče ne

- **Místo:** `CM7/Core/Src/meas_math.c:26-32` (`meas_limit_eval`),
  `CM7/app/app_gpsdo.c:2069` (`fmt_hz`) a `:2089` (`fmt_sdec`),
  `CM7/Core/Src/ipc.c:567-573` (`ipc_cfg_apply` bez kontroly konečnosti).
- **Popis:** `meas_limit_eval` vrací `LO` pro `y < lo`, `HI` pro `y > hi`,
  jinak **PASS**. Pro NaN jsou obě porovnání nepravdivá ⇒ **PASS**. Tester
  tedy selhává do **propuštění**, ne do odmítnutí.
- **Důkaz — cesta k NaN je deterministická podle IEEE 754:**
  1. `scpi_num` přijme exponent do 308 (`scpi.c`, `if (e > 308) … fail`), takže
     `CALC:MATH:M 1E308` projde; `ipc_cfg_apply` ho uloží bez kontroly
     (`c->m = argd;`, ř. 567).
  2. `CALC:MATH:STAT ON` → `y = m·x + b` = `1e308 · 1e7` = **+Inf**.
  3. `CALC:NULL:ACQ` → `null_ref = +Inf` a `null_en = 1` → `y = Inf − Inf` =
     **NaN** → `meas_limit_eval` → **PASS**, alarm limitu mlčí.
  Projekt o velkých vstupech ví: `fmt_scpi_hz_d` (`scpi.c:111-119`) v komentáři
  píše, že *„`CALC:MATH:M 1e30` … `scpi_num` bez reptání přečte"*, a je proti
  NaN ošetřený formou `!(hz > -4.0e9 && hz < 4.0e9)`. Jeho UI dvojče `fmt_hz`
  ale testuje `a >= 4.2e9`, což NaN **nechytí** → `(uint32_t)NaN` je UB
  a na Cortex-M7 (`VCVT` saturuje NaN na 0) vyjde věrohodná **„0.00000 Hz"**.
  Totéž `fmt_sdec`.
- **Dopad:** Pass/fail tester, který pro nesmyslnou hodnotu hlásí PASS, je
  u měřicího přístroje špatný směr selhání. Okno MATH k tomu ukáže nulu místo
  neplatné hodnoty. Vyžaduje to záměrně extrémní M, takže ne náhodnou chybu
  uživatele — ale jde to zvenku a bez varování.
- **Reprodukce:** staticky (viz Důkaz). Na HW **záměrně neprovedeno**:
  `ipc_cfg_apply` by změněnou konfiguraci uložil do syscfg flash uživatele.
- **Návrh opravy:** (a) `meas_limit_eval` fail-safe — NaN → FAIL (např.
  `if (!(y >= c->lo)) return MEAS_LO; if (!(y <= c->hi)) return MEAS_HI;`);
  (b) `fmt_hz`/`fmt_sdec` převést na formu `!(a < 4.2e9)` jako SCPI dvojče
  a pro NaN tisknout „--"; (c) volitelně odmítat nekonečné hodnoty už na vstupu
  (`ipc_cfg_apply` / SCPI set). Pro konečné hodnoty se chování nemění.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0012** (NaN ošetření v SCPI formátovači se
  nepřeneslo na UI dvojče), **L-0034** (konverze `double`→int mimo rozsah je
  UB, ne oříznutí), **L-0016**.
- **Stav:** otevřeno.

---

### F-0161 [S3] Fázový šum ℒ(f): bez odečtu střední hodnoty a se symetrickým Hannem prosákne kmitočtový offset do nízkých binů

- **Místo:** `CM7/Core/Src/phase_noise.c:56-62` (okno), `CM7/app/screens/screen_main.c:1355-1357`
  (plnění `chron` a volání).
- **Popis:** `y = (f − f0)/f0`, kde `f0` je **jmenovitý** kmitočet
  (`screen_main.c:1152`), takže `y` nese stejnosměrnou složku = kmitočtový
  offset oscilátoru. `pn_compute` ji před oknem **neodečte** a použije
  **symetrický** Hann `0,5·(1 − cos(2π·i/(N−1)))`, jehož DFT má na rozdíl od
  periodického (`2π·i/N`) nenulový únik i daleko od DC.
- **Důkaz — DFT obou oken (N = 64), únik DC do binu k vůči binu 0:**

  | bin | f [Hz] | symetrický (kód) | periodický |
  |---|---|---|---|
  | 1 | 0,0156 | −5,8 dB | −6,0 dB |
  | 2 | 0,0312 | −45,2 dB | −∞ |
  | 6 | 0,0938 | **−66,7 dB** | −∞ |

  Okno ANALÝZA čte bin nejbližší 0,1 Hz = **bin 6**. Únik se tam vyrovná šumu,
  když offset přesáhne ~330× σ vzorku (Σw ≈ 31,5, Σw² ≈ 23,6).
- **Dopad — poctivě:** **dnes je vada maskovaná.** Rozlišení čítače je
  `√2·2,5 ns / 0,25 s ≈ 1,4·10⁻⁸`, takže šum vzorků je srovnatelný s offsetem
  nedisciplinovaného OCXO a únik v binu 6 leží 20–50 dB **pod** šumem. Aktivní
  bude s **novou deskou** (TDC ~22 ps, `CLAUDE.md` „Nová revize desky"), kde
  šum vzorků klesne ~100×: při offsetu 10⁻⁸ vyjde ℒ(0,094 Hz) nadhodnocený
  o ~10 dB, při 10⁻⁷ o ~30 dB — věrohodně vypadající číslo, které měří offset,
  ne šum. Biny 1–5 jsou zasažené už dnes, jen se nezobrazují.
  ⚠️ Selftest to odhalit nemůže: testuje čistý tón s **nulovou** střední hodnotou.
- **Reprodukce:** výpočtem (viz Důkaz). Na HW: `fpgasim on` s kmitočtem
  odlišným od jmenovitého (konstantní offset, zanedbatelný šum) → ℒ(f)
  v okně ANALÝZA ukáže únik místo šumu.
- **Návrh opravy:** před oknem odečíst střední hodnotu (lépe i lineární trend)
  posledních `PN_NFFT` vzorků; okno přepnout na **periodický** Hann; do
  `pn_selftest` přidat případ „tón + velký DC offset → ℒ na binu k0 se nezmění".
- **Riziko opravy:** nízké. Pro data s nulovým průměrem se výsledek změní jen
  o rozdíl ENBW obou oken (~0,07 dB).
- **Vztah k lekcím:** **L-0039** (pozitivní kontrola musí obsahovat vadu, kvůli
  které vznikla — selftest DC nemá), **L-0011**.
- **Stav:** otevřeno.

---

### F-0162 [S3] Self-survey hlásí rozptyl fixů jako „konvergenci" — ta hodnota s počtem vzorků neklesá, a zelená „OK" nic neříká o přesnosti průměru

- **Místo:** `CM7/app/app_gpsdo.c:4351` (komentář), `:4381-4385` (výpočet),
  `:4452` (zobrazení a barva), `:4410` (persistence `g_survey_spread`).
- **Popis:** Komentář tvrdí *„horizontální rozptyl [m] = konvergence (klesá
  s N)"* a hodnota se srovnává s UBX `svinAccLimit` (2,5 m). Kód ale počítá
  `sqrt(m2/n)` — **směrodatnou odchylku jednotlivých fixů**. Ta s rostoucím N
  neklesá, konverguje k rozptylu fixů daného místa. S N klesá **směrodatná chyba
  průměru** `σ/√N` — a právě té odpovídá `svinAccLimit`.
- **Důkaz:** `app_gpsdo.c:4382-4385` (`sqrt(s_survey.m2lat / n)`, žádné `/√n`);
  `:4452` obarví „Rozptyl H:" zeleně při `< 2.0f` m.
- **Dopad:** Stabilní GPS svítí zeleně už po pár fixech, zašuměné místo
  nezezelená ani po hodinách — barva závisí na místě, ne na délce průzkumu.
  Uložená hodnota se pak tváří jako kvalita zprůměrované polohy, kterou ale
  nepopisuje. ⚠️ Pozor i na druhou stranu: fixy GPS jsou silně autokorelované
  (chyba se mění v minutách až hodinách), takže ani `σ/√N` s počtem fixů za
  sekundu přesnost průměru poctivě neodhadne — efektivní N je mnohem menší.
- **Reprodukce:** staticky.
- **Návrh opravy — rozhodnutí:** (a) počítat `σ/√N_eff` (N_eff např. z doby
  průzkumu / korelační doby, konzervativně), nebo (b) nechat rozptyl fixů,
  ale přejmenovat ho, zrušit „OK" sémantiku a opravit komentář. Obě jsou
  poctivé; liší se v tom, co má okno sdělit.
- **Riziko opravy:** nízké (zobrazení), ale mění význam uložené hodnoty.
- **Vztah k lekcím:** **L-0031** (tatáž funkce už jednou tvrdila přesnost,
  kterou vstup nenesl), **L-0028**, **L-0011**.
- **Stav:** otevřeno.

---

### F-0163 [S3] `fmt_fixed` hlídá mez hodnoty jen pro `d ≥ 1` — pro `d = 0`, NaN a Inf přetypování přeteče; díra v opravě F-0053

- **Místo:** `CM7/app/app_gpsdo.c:452-458` (smyčka `while (d > 0)`), `:400`
  (`fixed_split`: `(int32_t)(v * scale ± 0.5)`).
- **Popis:** Oprava F-0053 zavedla druhou mez — *„`|v| · 10^d < 2,15e9`,
  jinak int32 přeteče"* — a smyčku, která `d` zmenšuje, dokud se hodnota
  nevejde. Smyčka ale běží `while (d > 0)`, takže **případ `d = 0` nikdy
  neotestuje**. Pro |v| ≥ 2,15·10⁹ skončí na `d = 0` a `fixed_split` provede
  `(int32_t)(v + 0.5)` — **přetečení, tedy UB**. NaN projde smyčkou taky
  (`NaN * scale < 2e9f` je nepravda → `d` klesne na 0) a skončí stejně.
  Když je `decimals` od začátku 0, počítadlo `s_fmt_clamped` se **nezvedne**
  (`d != decimals` je nepravda), takže přesně ta tichá vada, kvůli které
  počítadlo vzniklo, zůstane tichá.
  Druhá část: skutečná mez **přesnosti** není 2·10⁹, ale mantisa `float`
  (2²⁴ ≈ 1,68·10⁷). Nad ní má `v * scale` krok větší než 1, takže výstup má
  **vymyšlené koncové číslice** — tiše a věrohodně.
- **Důkaz:** `app_gpsdo.c:452` `while (d > 0)`; `:458` `if (d != decimals)`;
  `fixed_split` na `:400`. Na Cortex-M7 `VCVT` saturuje (Inf → INT32_MAX,
  NaN → 0), takže místo pádu vyjde „2147483647" nebo „0".
- **Dopad:** Dnes latentní — volající předávají teploty, napětí, dBm, ppm
  (vše ≪ 1,68·10⁷). Past je tam, kde `decimals` není literál (4 volající,
  sama funkce je vyjmenovává) a u hodnot z výpočtu, kde může vzniknout Inf
  (viz F-0165 — převod dBm bez pojistky dělí strmostí).
- **Reprodukce:** staticky.
- **Návrh opravy:** Kontrolovat i `d = 0` (`|v| < 2e9f`) a konečnost
  (`!(av < 2e9f)` chytí NaN i Inf) → tisknout „--" a **zvednout počítadlo**;
  do komentáře zapsat, že mez přesnosti je 2²⁴, ne 2·10⁹.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0064** (právě „druhá mez na hodnotě" — tady
  vynucená jen napůl), **L-0034** (UB konverze), **L-0017** (tichý přeskok
  bez počítadla), **L-0031** (datový typ je taky mez).
- **Stav:** otevřeno.

---

### F-0164 [S3] `fmt_frac`: normalizační smyčka bez meze — pro +Inf se UiTask navždy zasekne a watchdog desku resetuje

- **Místo:** `CM7/app/screens/screen_main.c:1544-1545`.
- **Popis:** Dvě smyčky normalizují mantisu: dolní
  `while (a < 1.0f && e < 30)` **mez má**, horní
  `while (a >= 10.0f) { a /= 10.0f; e--; }` **nemá**. Pro `a = +Inf` platí
  `Inf / 10 = Inf`, takže smyčka **nikdy neskončí**. Pro NaN obě smyčky
  přeskočí a `(int)NaN` je UB.
- **Důkaz:** `screen_main.c:1545`. `fmt_frac` volá UiTask při kreslení
  (Offset/σ/Drift, histogram, trend) — heartbeat by přestal tikat a
  `watchdog_supervise` by zapsal `stall:UiTask` a nechal IWDG resetovat.
- **Dopad:** Dnes latentní — `y` je vždy konečné (`screen_main_frac_dev`
  hlídá `f0 > 0`). Jakmile by se do statistiky dostalo Inf (např. budoucí
  zdroj vzorků), projeví se to jako „náhodný reset při kreslení", tedy
  nejhůř dohledatelná třída.
- **Reprodukce:** staticky.
- **Návrh opravy:** `while (a >= 10.0f && e > -40)` a předem `if (!isfinite(v))`
  → „--".
- **Riziko opravy:** nulové pro konečné vstupy.
- **Vztah k lekcím:** **L-0030** (počet iterací nesmí záviset na vstupu bez
  meze), **L-0004**, **L-0034**.
- **Stav:** otevřeno.

---

### F-0165 [S3] Převod AD8307 mV→dBm je v projektu 8×, se třemi různými politikami pro neplatnou strmost

- **Místo:** `CM7/app/app_gpsdo.c:1865-1866`, `:3705-3706`, `:5502`, `:7681-7682`,
  `:7907`; `CM7/Core/Src/scpi.c:701-702`, `:817-818`; `CM4/LWIP/App/httpd_min.c:463-465`.
- **Popis:** Tentýž vzorec `dBm = mV / slope + intercept` je napsaný osmkrát
  a pro neplatnou strmost se kopie chovají třemi způsoby:

  | politika | kopie |
  |---|---|
  | **„nevím"** (`null` / `9.91E37`) při `slope <= 1.0` | `httpd_min.c:463`, `scpi.c:701` (`MEAS:POW?`) |
  | **tiše dosadit 25 mV/dB** při `slope < 1e-3` | `app_gpsdo.c:1865`, `:3705`, `:7681`, `scpi.c:817` (`MMEM:DATA?`) |
  | **žádná pojistka** | `app_gpsdo.c:5502`, `:7907` |

  K tomu dva různé prahy (`> 1.0` vs `< 1e-3`).
- **Důkaz:** výše uvedené řádky; `calib_load` (`calib.c:69-74`) přebírá strmost
  z flash **bez kontroly rozsahu** (jen magic + CRC úložiště). UI ji drží na
  10–40 mV/dB (`app_gpsdo.c:3984`), takže nula je dnes prakticky nedosažitelná.
- **Dopad:** Při rozbité kalibraci (layout bez nového magicu, kolize CRC16)
  by `MEAS:POW?` poctivě řekl „nevím", **export datalogu přes `MMEM:DATA?`
  vyrobil věrohodné dBm z výchozí strmosti** a dvě okna UI dělila nulou
  (→ Inf → `fmt_fixed`, viz F-0163). Hlavní vada je ale strukturální: kopie
  už se rozešly, a `httpd_min.c:460` to výslovně řeší komentářem *„TOTOŽNÝ
  vzorec i podmínka jako `MEAS:POWer?`"* — synchronizace komentářem drží dvě
  kopie z osmi.
- **Reprodukce:** staticky.
- **Návrh opravy — rozhodnutí:** jedna funkce `ad8307_mv_to_dbm(mv, slope,
  intercept, *out) -> bool` ve sdílené hlavičce (používají ji obě jádra) s
  **jednou** politikou. Doporučeno „nevím" (už ji mají web a `MEAS:POW?`) —
  ale to je volba politiky, proto skupina B. Plus kontrola rozsahu v `calib_load`.
- **Riziko opravy:** nízké, ale dotýká se 8 míst na dvou jádrech (CM4 přeložit;
  layout IPC se nemění).
- **Vztah k lekcím:** **L-0018** (dvě pravdy čekající, až se rozejdou — tady
  osm, už rozejité), **L-0020**, **L-0012**.
- **Stav:** otevřeno.

---

### F-0166 [S4] Overlapping ADEV/HDEV/MDEV zahazují poslední platný člen — smyčky nesedí s vzorcem ve vlastním komentáři

- **Místo:** `CM7/app/screens/screen_main.c:1496` (HDEV), `:1510` (MDEV),
  `:1524` (ADEV); vzorce v komentáři `:1471-1473`.
- **Popis:** Komentář uvádí vzorce NIST SP1065 s počty členů `M−2m+1` (ADEV),
  `M−3m+1` (HDEV), `M−3m+2` (MDEV). Po dosazení indexů (0-indexováno) je
  poslední platné `j` = `M−2m`, `M−3m`, `M−3m+1`. Podmínky smyček
  (`j + 2m <= M−1`, `j + 3m <= M−1`, `j + 3m − 1 <= M−1`) ale končí o jedna
  dřív ⇒ **o jeden člen méně ve všech třech**.
- **Důkaz:** příklad ADEV, `m = 1`, `M = 24`: člen `j = 22` používá `y[22]`
  a `y[23]` (platné), smyčka ho vynechá. Normalizace se dělí skutečným `n`
  (`:1505`, `:1519`, `:1531`), takže odhady jsou **nezkreslené**.
- **Dopad:** Na dlouhých τ (M = 24, m = 5) je to 7–10 % členů — tedy právě
  tam, kde je dat nejméně a kde overlapping varianta měla podle komentáře
  (`:1466-1469`) „vytěžit maximum z týchž dat". Mírně širší nejistota, žádná
  systematická chyba. Druhotně: estimátor vrátí 0 i pro `M = 2m` resp. `3m`,
  kdy je právě jeden platný člen.
- **Reprodukce:** staticky (dosazení indexů).
- **Návrh opravy:** podmínky na `j <= M−2m`, `j <= M−3m`, `j <= M−3m+1`
  a vstupní meze na `M >= 2m` resp. `3m`. **Nutně spolu s F-0167** (počet
  členů pro pás se počítá zvlášť).
- **Riziko opravy:** nízké — zobrazené σ se změní jen v rámci statistické
  nejistoty.
- **Vztah k lekcím:** **L-0028** (komentář s vzorcem vs kód).
- **Stav:** otevřeno.

---

### F-0167 [S4] Konfidenční pás Allanova grafu: druhý nezávislý výpočet počtu členů (u HDEV už nesedí) a přeceněná jistota u overlapping odhadu

- **Místo:** `CM7/app/screens/screen_main.c:1660-1664` (`ns`), `:1797-1800`
  (šířka pásu), komentáře `:1641-1642`, `:1657-1659`.
- **Popis:** Tři věci:
  1. **L-0018:** počet členů pro šířku pásu se počítá **znovu** vzorcem
     `(ADEV) ? M−2m : M−3m+1`, nezávisle na estimátoru. Pro HDEV estimátor
     udělá `M−3m` členů, `ns` tvrdí `M−3m+1`. Po opravě F-0166 by se to
     rozešlo i u ADEV a MDEV.
  2. **Přeceněná jistota:** komentář *„U overlapping variant je jich řádově
     víc … pás je proto užší, a to oprávněně"* (`:1658-1659`). Překrývající se
     členy jsou ale **korelované** — efektivní počet stupňů volnosti je menší
     než počet členů. Pro white FM podle SP1065 (M = 24, m = 5): EDF ≈ 5,1 ⇒
     relativní 1σ ≈ 1/√(2·5,1) ≈ 0,31; kód kreslí 0,8/√14 ≈ 0,21 ⇒ **pás
     ~1,5× užší, než odpovídá datům**. U m = 1 to vychází správně (0,18 vs 0,17).
  3. Komentáře si protiřečí: `:1642` „~1/sqrt(2*ns)", `:1797` „~0,8/sqrt(páru)".
- **Důkaz:** výše; výpočet EDF podle SP1065 pro overlapping ADEV, white FM.
- **Dopad:** Pás je vizuální efekt (`FX_ALLAN_CONF`), ale nese informaci
  o jistotě — a na dlouhých τ ji přeceňuje.
- **Reprodukce:** staticky.
- **Návrh opravy:** (A) estimátor vrací počet členů výstupním parametrem
  → jediný zdroj; (B) rozhodnout model EDF — white FM podle SP1065 je
  rozumný konzervativní výchozí bod, ale typ šumu je volba.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0018**, **L-0011**, **L-0028**.
- **Stav:** otevřeno.

---

### F-0168 [S4] Rozpočet nejistoty kombinuje σy(1 s) s rozlišením při skutečném hradle — nesouměřitelné τ a pravděpodobně dvakrát započtené rozlišení

- **Místo:** `CM7/app/app_gpsdo.c:5017` (`sigma = screen_main_adev_1s()`),
  `:5023`; `CM7/Core/Src/meas_present.c:190-197`.
- **Popis:** Člen stability je vždy σy **při τ = 1 s**, zatímco člen rozlišení
  se počítá z **aktuálního** hradla (u FPGA 0,25 s). A σy@1s se počítá ze
  vzorků, které samy mají šum rozlišení 0,25s hradla — s TDC 2,5 ns je to
  ~1,4·10⁻⁸, tedy pravděpodobně víc než stabilita OCXO. Rozlišení by tak bylo
  v rozpočtu **dvakrát** (jednou explicitně, jednou uvnitř σy).
- **Důkaz:** staticky doložitelná je jen nesouměřitelnost τ. Velikost
  dvojího započtení je **HYPOTÉZA**.
- **Dopad:** U by vycházelo až ~√2× nadsazené. Směr je bezpečný (přeceněná
  nejistota), ale číslo je v okně, které existuje právě kvůli němu.
- **Reprodukce:** **HYPOTÉZA — ověřit na HW** s běžícím FPGA: v okně ANALÝZA
  porovnat „rozlišení" a „stabilita"; když jsou si blízko, jde o šum čítače,
  ne oscilátoru, a dvojí započtení platí.
- **Návrh opravy — rozhodnutí:** jde o metrologickou definici rozpočtu (co je
  nejistota *odečtu* vs. nestabilita *měřeného*), tedy skupina B.
- **Riziko opravy:** —
- **Vztah k lekcím:** **L-0036** (veličiny musí mít souměřitelnou časovou
  základnu), **L-0011**.
- **Stav:** otevřeno.

---

### F-0169 [S4] Lineární proklad: naivní vzorec se surovými sumami (bezpečný jen díky dnešním volajícím) a pevný práh „neprůkazné" nezávislý na počtu bodů

- **Místo:** `CM7/Core/Src/meas_present.c:219-248`; práh `CM7/app/app_gpsdo.c:4964`.
- **Popis:** Dvě věci:
  1. `n·Σxx − (Σx)²` a `n·Σxy − Σx·Σy` jsou učebnicově nestabilní, když má
     `x` velký offset vůči rozptylu nebo `y` velkou hodnotu vůči změně
     (katastrofické odečítání). **Dnešní volající jsou v bezpečí** — drift má
     `x` relativní (hodiny od nejnovějšího záznamu, `app_gpsdo.c:4943`),
     tempco `x` ≈ 51 °C a `y` je Vc v mV. Hlavička ale nabízí funkci obecně pro
     „drift/aging" a předpoklad nikde nestojí: proklad kmitočtu (`y` ~10⁷ Hz,
     drift ~10⁻³ Hz/den) by ztratil prakticky všechny platné číslice.
  2. „Neprůkazné" při `|r| < 0,5` bez ohledu na počet bodů. Při n ≈ 200
     (decimace v `ana_recompute`) je `r = 0,3` průkazné na p < 10⁻⁴, a při
     n = 4 není průkazné ani `r = 0,9`.
- **Důkaz:** výše.
- **Dopad:** Dnes (1) nic, (2) kosmeticky zavádějící verdikt.
- **Reprodukce:** staticky.
- **Návrh opravy:** (1) akumulovat relativně k prvnímu bodu (posun x0, y0) —
  levné a obecně stabilní; (2) rozhodnout kritérium průkaznosti (t-test
  `r·√(n−2)/√(1−r²)`) — skupina B.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **L-0031** (datový typ a numerika jako mez).
- **Stav:** otevřeno.

---

## Co bylo zkontrolováno a je v pořádku

- **Kadence vzorkování (L-0036):** `app_gpsdo_tick_stats_sample` běží **1×/s**
  (`freertos_task_ui.c:587`, `>= 1000` ms) a při reálném měření bere nejnovější
  hodnotu jen při nové `g_freq_seq`. Při FPGA ~4 měření/s je tedy **τ0 = 1 s
  správně** a `fs = 1.0` v `screen_main_phase_noise` taky. Neplatí jen pro
  měření pomalejší než 1/s — to kód i `CLAUDE.md` poctivě přiznávají (TODO #27).
- **Hi-res dlouhé dělení** (`screen_main.c:742-769`): mez přetečení
  `edges × mul × 1e9` **existuje** v `fpga_freq_hires_mul` (`fpga_freq.c:527`,
  `edges > 4e9 / MUL` → přeskočit) a škáluje se s násobitelem, takže přežije
  i budoucí `÷10`. `rem × 10` nepřeteče (`rem < gate_ns`).
- **Nejisté číslice** (`freq_uncertain_frac`) používají tentýž vzorec rozlišení
  (`√2·tdc/gate`) jako `mp_budget` a sdílenou konstantu `MP_TDC_PS` = 2,5 ns.
- **Vzorce ADEV/HDEV/MDEV** a jejich normalizace odpovídají NIST SP1065;
  **TDEV = τ·MDEV/√3** je exaktní; **MTIE** je poctivě označený jako odhad.
- **Welford** (`mp_stats_add`, self-survey) je algoritmicky správně; vstup
  survey je od F-0070 v `e7`.
- **Filtr PRŮMĚR** v double: spočítáno, že i při 400 MHz je chyba součtu
  16 hodnot ~6·10⁻⁸ Hz, pod rozlišením displeje.
- **`mp_nominal_auto`** je robustní i vůči `log10` těsně pod celou dekádou
  (přichytávací hodnota 10,0 zachrání `floor` o jedna níž).
- **Proklad driftu nebere záznamy bez času:** `datalog.c:455` zapisuje
  `t_unix = 0`, dokud není RTC synchronizované, a `ana_recompute` nuly
  vyřazuje — takže se do prokladu nedostane stará epocha RTC s obří pákou.
- **Histogram:** škálování Gaussovy křivky `N·binW/(sd·√(2π))` je správně.
- **Konstanty LSE:** krok 0,95367432 ppm = 2⁻²⁰, `CALP` = 512 kroků = 488 ppm;
  `RECALPF` řešený bez spinu; fázová reference se po resyncu nuluje.
- **SCPI formátovač `fmt_scpi_hz_d`** je vůči NaN i přetečení ošetřený vzorově.

**Drobnosti pod úrovní nálezu** (zapsané, ať se příště neotvírají):
`sd` histogramu dělí `n`, `mp_stats_sd` dělí `n−1` (při n = 120 rozdíl 0,4 %);
`fmt_hz` pro −0,000001 vytiskne „−0.00000"; drift metodou dvou půlek má při
lichém počtu vzorků během plnění ringu chybu vzdálenosti těžišť ≤ 1,6 %;
1Hz tik (`last = HAL_GetTick()`) se opožďuje o jitter smyčky (~0,5 % τ0);
`ana_recompute` odečítá časy v `uint32_t`, což přeteče jen při ručním
posunu času zpět během synchronizovaného běhu.

## Checklist — relevantní položky

| sekce | položka | verdikt |
|---|---|---|
| A | konstanty odvozené z hodin (TDC, LSE, kadence) | OK — `MP_TDC_PS`, kroky CALR a 1Hz tik ověřené |
| D | sdílený stav (`g_meas_cfg`) | pokryto dřívějšími F-0052/F-0096; nově jen chybějící kontrola konečnosti → F-0160 |
| E | návratové hodnoty / neplatné vstupy | **NÁLEZY** F-0160, F-0163, F-0164, F-0165 |
| E | čekací smyčky s mezí | **NÁLEZ** F-0164 |
| G | numerická správnost výstupů | **NÁLEZY** F-0158, F-0159, F-0161, F-0162, F-0166–F-0169 |
| H | warningy | beze změny — build 0 varování, `audit.py` 92/0/2 (2026-09-25) |

## Shrnutí

**Verdikt: podmíněně funkční.**

12 nálezů: **1× S2, 7× S3, 4× S4**. Vzorce jsou až na výjimky správně a
napsané s neobvyklou péčí (NIST SP1065, exaktní TDEV, poctivě označený MTIE,
ošetřené přetečení u hi-res dělení). Vady jsou jinde:

1. **v regulační smyčce** (F-0158) — kód dělá správnou věc se špatně
   udržovaným stavem, a simulace ukazuje, že nekonverguje;
2. **v tom, co se vydává za změřené** (F-0159, F-0162, F-0167, F-0168) —
   čísla, která popisek nebo barva prezentují jinak, než jak vznikla;
3. **v neplatných hodnotách** (F-0160, F-0163, F-0164, F-0165) — NaN, Inf
   a nulová strmost mají v každé kopii jiné ošetření, a kde ho nemají, končí
   to PASSem, vymyšlenou nulou nebo zaseknutým UiTaskem.

🔑 Dvě z nejzávažnějších vad (F-0159, F-0160) jsou **L-0012** — oprava existuje,
jen se nepřenesla na sourozence (sousední řádek téhož okna, UI dvojče SCPI
formátovače). Při opravách je proto potřeba vždy projít všechny kopie, ne jen
tu, na kterou nález ukazuje.
