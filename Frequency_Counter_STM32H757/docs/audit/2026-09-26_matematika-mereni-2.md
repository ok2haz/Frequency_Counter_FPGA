# Audit modulu 24 — matematické funkce měření, DRUHÝ PRŮCHOD

**Datum:** 2026-09-26
**Fáze:** F3 (přezkum kódu) — **bez editace kódu**
**Commit auditované verze:** `3398954` (po opravách F-0158..F-0170)
**Jádro:** CM7 + SPA v obrazu CM4 (`httpd_min.c`)
**Branch:** `audit/2026-09-09-hodiny-pwr`
**Předchozí průchod:** [`2026-09-26_matematika-mereni.md`](2026-09-26_matematika-mereni.md)

## Proč druhý průchod

Na žádost uživatele („ještě jednou analyzuj matematiku") po dokončení oprav
F-0158..F-0170. Tentokrát se neptám jen „je vzorec správně", ale **„platí
předpoklady, za kterých je vzorec správně"** — mrtvá doba vzorkování,
nezávislost reziduí, typ rozdělení v rozpočtu nejistoty — a znovu procházím
**vlastní opravy** a **JS dvojčata v SPA**, která první průchod pokryl jen
částečně (F-0170 se objevil až v F6).

## Metoda

Dva nálezy stojí na **simulaci, ne na úsudku**; skripty jsou uložené, aby šly
zopakovat (Node z `tools/node-v20.18.1-win-x64/`):

| skript | co měří |
|---|---|
| [`sim/2026-09-26_mrtva_doba.js`](sim/2026-09-26_mrtva_doba.js) | ADEV ze vzorkování displeje (1 z 4 měření 0,25 s za sekundu) a z datalogu (1 měření za 10 s) proti vzorkování bez mrtvé doby; bílý FM i bílý PM, 20 000 s dat |
| [`sim/2026-09-26_ttest_autokorelace.js`](sim/2026-09-26_ttest_autokorelace.js) | podíl „průkazných" prokladů bez driftu (falešné poplachy) a s driftem (detekce) pro bílý šum, AR(1) ρ = 0,9 a náhodnou procházku; n = 20 a 200, 4 000 opakování |

Znovu přepočítáno a **v pořádku** (neotevírat): znaménka a skládání korekce
LSE včetně oprav F-0158 (`rtc.c:432-531`), interakce okna 480 s s resyncem
10 min, vzorce overlapping ADEV/MDEV/HDEV (SP1065) a jejich meze po F-0166,
EDF pro bílý FM (`screen_main.c:1669`), tabulka kritických t (df 1..30),
převod AD8307 `P = V/slope + intercept`, periodický Hann a normalizace PSD
po F-0161 (`phase_noise.c:44-106`), Welford v self-survey, hi-res dělení.

---

### F-0171 [S2] Statistika displeje má mrtvou dobu 75 %: bere jedno měření 0,25 s za sekundu, ostatní tři zahodí — σy vychází 2× až 23× vysoko a šum přístroje se tváří jako bílý FM

- **Místo:** `CM7/Core/Src/freertos_task_ui.c:587-590` (1Hz tik),
  `CM7/app/app_gpsdo.c:8343-8349` (`app_gpsdo_tick_stats_sample` — vzorek jen
  při nové `g_freq_seq`, ale nejvýš jeden za sekundu),
  `CM7/app/screens/screen_main.c:1200-1212` (`stats_sample` bere `s_freq_n` =
  poslední jednotlivé měření).
- **Popis:** FPGA dává ~4 měření/s, každé s hradlem 0,25 s. Statistika se ale
  vzorkuje **1×/s** a bere jen **nejnovější** měření. Vzorek `y` tedy není
  průměr za 1 s, ale průměr za 0,25 s, a mezi vzorky je 0,75 s, kterou nic
  nepokrývá. Všechno za tím počítá s „τ0 = 1 s": ADEV/MDEV/HDEV/TDEV/MTIE,
  σy@1s (karta, tabulka, prahový monitor `g_adev_1s`, rozpočet nejistoty),
  histogram, drift metodou dvou půlek a ℒ(f).
  🔴 **První průchod tohle prohlásil za „v pořádku"** (oddíl „Co bylo
  zkontrolováno", bod *Kadence vzorkování*): rozestup 1 s je správně, ale
  **okno průměrování ne** — a ADEV závisí na obojím.
- **Důkaz (simulace `sim/2026-09-26_mrtva_doba.js`):**

  | šum | τ = 1 s | 2 s | 5 s | 10 s | 20 s | 50 s | log-log sklon 1..50 s |
  |---|---|---|---|---|---|---|---|
  | bílý FM (displej / správně) | 2,02 | 1,98 | 2,01 | 1,98 | 1,92 | 1,96 | −0,51 (správně −0,50) |
  | bílý PM (displej / správně) | 3,29 | 4,64 | 7,29 | 10,39 | 14,68 | 22,69 | **−0,51 (správně −1,00)** |

  Bílý FM: vzorek 0,25 s má 4× větší rozptyl než průměr 1 s → σy všude 2×.
  Bílý PM (kvantizace TDC = **podlaha přístroje**): s mrtvou dobou jsou
  sousední vzorky nezávislé, takže průměrováním klesá σy jen jako τ^−½ místo
  τ^−1 — **šum přístroje dostane sklon bílého FM** a klasifikace šumu
  (`noiseName`/sklon) ho pojmenuje jako vlastnost oscilátoru.
- **Dopad:** Dnes latentní (FPGA neběží, headline je SIM). S reálnými daty
  bude hlavní výstup přístroje — Allanova křivka — systematicky vysoko a na
  dlouhých τ o řád; podlaha přístroje se vydá za bílý FM oscilátoru. Web bere
  **každé** měření (`seq_meas`, SSE), takže pro tatáž data ukáže jiná čísla
  než displej (L-0018). Týká se i ℒ(f): vzorky 0,25 s po 1 s → 4× vyšší Sy
  pro bílý FM (+6 dB) a alias šumu z pásma 0,5–2 Hz.
- **Reprodukce:** simulace výše; na HW až s FPGA (nebo `fpgasim`, pokud
  emulátor generuje šum — HYPOTÉZA, neověřeno).
- **Návrh opravy:** v tiku 1 s **sečíst všechna nová měření** od minulého
  vzorku: `Σ(edges·mul)` a `Σgate_ns` → `f = Σ(edges·mul) / Σgate_ns` =
  přesný reciproký kmitočet sjednocení oken (dnešní hi-res dělení to umí,
  jen dostane součty). Pokud jsou okna FPGA navazující, mrtvá doba zmizí;
  pokud ne, aspoň klesne na zbytek. ⚠️ **Jestli FPGA dává navazující okna,
  je otázka kontraktu nové desky — HYPOTÉZA, ověřit ve firmwaru FPGA.**
  Stejný akumulátor potřebuje F-0172.
- **Riziko opravy:** střední — mění vstup celé statistiky; nutné zachovat
  výjimku SIM a přechody REAL↔SIM (reset pyramidy).
- **Vztah k lekcím:** L-0036 (kadence je vlastnost přenosu), L-0088
  (ověřovat nezávislou referencí), L-0011 (můj první průchod převzal
  „τ0 = 1 s" bez ověření okna).
- **Stav:** otevřeno.

---

### F-0172 [S3] Datalog ukládá okamžité měření 0,25 s, rekonstrukce Allanovy pyramidy ho vkládá jako 10s průměr — „převod je exaktní" neplatí, σy z historie 3,3× nad živými

- **Místo:** `CM7/Core/Src/datalog.c:452` (`sample`: `r->freq_x100000 =
  m.frequency_x100000` = poslední rámec), `CM7/app/screens/screen_main.c:1304-1333`
  (komentář *„převod je exaktní — žádné převzorkování, žádná změna τ0"*,
  `screen_main_adev_seed_10s` → stage 1).
- **Popis:** Stage 1 živé pyramidy drží průměr 10 vzorků po 1 s (i s mrtvou
  dobou F-0171 je to aspoň 10 oken). Záznam datalogu je **jedno** měření 0,25 s
  každých 10 s. Rozestup sedí, **okno průměrování ne** — a do jedné pyramidy
  se míchají vzorky dvou různých vzorkovacích funkcí.
- **Důkaz (simulace, tentýž skript):** datalog / živá stage 1 = **3,25**
  (bílý FM, ≈ √10) a **3,0** (bílý PM); datalog / správně 6,5× (FM) až 74×
  (PM při τ = 50 s).
- **Dopad:** po restartu (rekonstrukce z logu) je Allanova křivka na τ ≥ 10 s
  ~3× vysoko a jakmile ji začnou přepisovat živé vzorky, **skočí** — vypadá to
  jako změna oscilátoru. Totéž platí pro export CSV a `MMEM:DATA?`: sloupec
  „freq" je okamžitý vzorek 0,25 s, ne hodnota za periodu logu, a nikde to
  není řečeno.
- **Reprodukce:** simulace; na HW s FPGA: restart po dni běhu → srovnat σy(10 s)
  před a po přepsání stage 1 živými vzorky.
- **Návrh opravy:** datalog zapisuje **průměr za periodu** ze stejného
  akumulátoru `Σedges/Σgate` jako F-0171 (formát záznamu se nemění, mění se
  význam pole — zapsat do dokumentace formátu). Do té doby rekonstrukci buď
  vypnout, nebo ji v komentáři přestat vydávat za exaktní.
- **Riziko opravy:** nízké–střední; staré záznamy v logu mají starý význam
  (nejde je odlišit — zvážit bit ve `flags`).
- **Vztah k lekcím:** L-0028 (tvrzení v komentáři je testovatelné),
  L-0018, L-0036.
- **Stav:** otevřeno.

---

### F-0173 [S3] t-test průkaznosti (F-0169) předpokládá nezávislá rezidua — u autokorelovaných dat z datalogu hlásí „průkazný drift" v 63–91 % případů bez driftu

- **Místo:** `CM7/Core/Src/meas_present.c:278-292` (`mp_fit_significant`),
  `CM7/app/app_gpsdo.c:4968-4990` (vstup: Vc a teplota z datalogu, každý
  `stride`-tý záznam), `:5012`; web `CM4/LWIP/App/httpd_min.c:2401` (`fitSig`),
  `:2761`.
- **Popis:** t-test korelace platí pro **nezávislá** rezidua. Ladicí napětí
  a teplota OCXO se mění pomalu (tepelné časové konstanty, putování), takže
  body vybrané z datalogu po `stride` jsou silně autokorelované. Test pak
  bere n = 200 bodů jako 200 nezávislých informací, i když jich nese řádově
  méně.
- **Důkaz (simulace `sim/2026-09-26_ttest_autokorelace.js`, 4 000 opakování,
  podíl „průkazné"):**

  | data (n = 200) | staré \|r\| ≥ 0,5 | **dnešní t-test** | t-test s n_eff |
  |---|---|---|---|
  | bílý šum bez driftu | 0 % | 4 % | 4 % |
  | AR(1) ρ = 0,9 bez driftu | 7 % | **63 %** | 7 % |
  | náhodná procházka bez driftu | 66 % | **91 %** | 28 % |
  | bílý šum + drift | 100 % | 100 % | 100 % |
  | náhodná procházka + drift | 100 % | 100 % | 82 % |

  (n = 20: AR 48/54/20 %, RW 65/69/33 %.)
  🔴 **Moje doporučení u F-0169 to pro tuto třídu dat zhoršilo** — t-test je
  správný pro nezávislá data (řádek 1), ale data okna ANALÝZA takhle nevypadají.
- **Dopad:** okno ANALÝZA (a web) označí putování Vc nebo teploty za
  „průkazný drift/tempco" a nakreslí/vypíše směrnici jako měření.
- **Reprodukce:** simulace; na HW: datalog s ustáleným OCXO (bez skutečného
  driftu) → ANALÝZA hlásí drift bez „neprůkazné".
- **Návrh opravy:** efektivní počet bodů podle lag-1 autokorelace reziduí
  (Bretherton et al. 1999): `n_eff = n·(1−ρ1)/(1+ρ1)`, `df = n_eff − 2`.
  Levné (jeden průchod reziduí), dá se přidat do `mp_fit_t` (součty pro ρ1
  vyžadují druhý průchod nebo uložení bodů — okno ANALÝZA body stejně čte
  z logu). **Stejně ve webu** + případ do `tools/spa/stat_test.js`.
  ⚠️ I s korekcí zůstává náhodná procházka v 28 % „průkazná" — přímka
  principiálně neodliší drift od RW; popisek by měl říkat „neodporuje
  šumu", ne „drift je průkazný".
- **Riziko opravy:** nízké (zobrazovaný verdikt).
- **Vztah k lekcím:** L-0088 (vylepšení až po pozitivní kontrole — tu jsem
  u F-0169 udělal jen pro nezávislá data), L-0089, L-0018 (web + displej).
- **Stav:** otevřeno.

---

### F-0174 [S3] Web počítá rozpočet nejistoty z NASTAVENÉ brány a neznámou tiše nahradí 1 s — dvojče STATUS #83 a F-0159, které firmware už opravil

- **Místo:** `CM4/LWIP/App/httpd_min.c:3395` (`gate=+s.set_gate_s||1`),
  `:3396-3420` (`drawUnc`); firmware pro srovnání `CM7/app/app_gpsdo.c:5068-5072`
  (`screen_main_gate_actual_s()` + `bd.valid`).
- **Popis:** Nastavená brána se do FPGA vůbec nedostane (STATUS #83); firmware
  proto počítá rozlišení ze **skutečného** `gate_ns` z rámce a bez něj ukáže
  „--". Web používá `set_gate_s`, a když chybí, dosadí 1 s. `/api/state` přitom
  skutečné `gate_ns` nese (`httpd_min.c:431`).
- **Dopad:** při nastavené bráně 100 s web hlásí rozlišení a nejistotu **400×
  lepší**, než odpovídá skutečnému hradlu 0,25 s; web a displej ukážou pro
  totéž měření jiné U (L-0018). Navíc web bere σ při τ = brána, displej
  σy(1 s) — dvě různé definice téhož rozpočtu (souvisí s F-0168).
- **Reprodukce:** staticky; na HW s FPGA: nastavit GATE 100 s, porovnat
  „Nejistota U" na displeji a na webu.
- **Návrh opravy:** `gate = s.gate_ns/1e9` (skutečné), při 0/chybějícím „--";
  definici `u_sta` sjednotit s firmwarem (rozhodnutí F-0168).
- **Riziko opravy:** nízké (SPA, `check.py --build`).
- **Vztah k lekcím:** L-0012 (sourozenec na druhém jádře — potřetí v tomto
  modulu), L-0018, L-0038.
- **Stav:** otevřeno.

---

### F-0175 [S4] ℒ(f): web pořád se symetrickým Hannem a bez odečtu průměru segmentu (dvojče F-0161); firmware ukazuje jediný periodogram na 0,1 dB

- **Místo:** web `CM4/LWIP/App/httpd_min.c:3253` (okno `2πi/(PN_N−1)`),
  `:3284` (odečte se průměr celého bufferu, ne segmentu), `:3217-3225`
  (komentář *„bin po binu jako přístroj"*); firmware
  `CM7/app/screens/screen_main.c:1349-1357`, `CM7/app/app_gpsdo.c:5150-5159`.
- **Popis:** (1) Web má přesně vadu, kterou F-0161 opravil ve firmwaru:
  symetrický Hann a v každém Bartlettově segmentu zůstane jeho lokální
  odchylka od globálního průměru → únik DC do nízkých binů. (2) Komentář
  tvrdí, že biny webu a přístroje sedí „bin po binu" — platí jen při stejném
  `fs`; web má `fs = 1/τ0` ≈ 4 Hz, přístroj 1 Hz, takže „nejbližší 0,1 Hz"
  je na webu 0,125 Hz a na displeji 0,094 Hz. (3) Firmware počítá ℒ z
  **jediného** periodogramu 64 vzorků (rozptyl odhadu ~±5,6 dB, χ² se 2
  stupni volnosti) a tiskne ho na 0,1 dB bez údaje o nejistotě; web průměruje
  segmenty a na rozptyl upozorňuje.
- **Dopad:** web a displej dávají pro tatáž data jiné ℒ(f) na jiném offsetu;
  číslo na displeji předstírá přesnost, kterou nemá.
- **Reprodukce:** staticky.
- **Návrh opravy:** web: periodický Hann + odečet průměru každého segmentu
  (+ případ do `stat_test.js` proti `pn_compute`); firmware: průměrovat
  sousední biny nebo Welch s překryvem 50 % (120 vzorků → 2–3 segmenty)
  a zobrazit na celé dB; komentář o „bin po binu" opravit.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0012, L-0018, L-0088.
- **Stav:** otevřeno.

---

### F-0176 [S4] Rozpočet nejistoty sčítá MEZ rozlišení (√2·tdc/gate) se směrodatnými odchylkami a vydává součet za u (k = 1) — podle GUM je standardní nejistota kvantizace 3,5× menší

- **Místo:** `CM7/Core/Src/meas_present.c:193-201` (`u_res_rel = √2·tdc/gate`,
  kvadratický součet s `sigma_y` a `ref_ppb`), web `httpd_min.c:3396`.
- **Popis:** Kvantizace každé hrany hradla je rovnoměrně rozložená chyba
  šířky jednoho kroku TDC; podle GUM (obdélníkové rozdělení ±a) je její
  standardní nejistota `a/√3`, tj. `tdc/√12` na hranu a `tdc/√6` pro dvě
  hrany. Rozpočet ale bere `√2·tdc` — mez „±1 krok na obou hranách", tedy
  rozlišení čítače, ne směrodatnou odchylku — a sčítá ji kvadraticky
  s σy jako u (k = 1). Poměr `√2 / (1/√6) = √12 ≈ 3,46`.
- **Dopad:** dnes (TDC 2,5 ns, hradlo 0,25 s → 1,4·10⁻⁸ proti σy ~10⁻¹¹)
  rozlišení rozpočet vždy ovládne, takže **U je nadsazené ~3,5×** a „Platných
  cifer" vychází asi o půl cifry méně. Chyba je na konzervativní straně.
  ⚠️ Zobrazení nejistých číslic (`freq_uncertain_frac`) smí mez používat dál —
  tam jde o rozlišení, ne o u.
- **Reprodukce:** výpočtem.
- **Návrh opravy:** v rozpočtu `u_res = tdc/(√6·gate)` (s poznámkou o typu B,
  obdélník), mez √2·tdc/gate ponechat jen pro číslice. **Rozhodnout spolu
  s F-0168** (metrologická definice rozpočtu) — proto skupina C.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0089 (číslo se vydává za něco, čím není).
- **Stav:** otevřeno.

---

### F-0177 [S4] Modré podtržení „poslední důvěryhodné číslice" je vynucené aspoň na desetinách Hz — nad ~7 MHz tvrdí přesnost, kterou měření nemá

- **Místo:** `CM7/app/screens/screen_main.c:888-904` (`freq_uncertain_frac`,
  `if (nc < 1) nc = 1;` na ř. 901).
- **Popis:** Funkce umí označit za nejisté jen **desetiny** a vždy nechá
  aspoň jednu jako důvěryhodnou. Rozlišení `√2·tdc/gate·f` při hradle 0,25 s
  a TDC 2,5 ns překročí 0,1 Hz už nad **7,07 MHz**.
- **Dopad:** podtržení tvrdí přesnost 0,1 Hz při skutečném rozlišení
  0,14 Hz (10 MHz), 1,4 Hz (100 MHz) a ~20 Hz (1,4 GHz, větev /16) — u 1,4 GHz
  o dva řády. Latentní do běhu FPGA; s novou deskou (TDC ~22 ps) se mez
  posune k ~800 MHz, ale nezmizí.
- **Reprodukce:** výpočtem; `fpgasim on 100000000` → podtržení na 0,1 Hz.
- **Návrh opravy:** když rozlišení překročí 0,1 Hz, přesunout podtržení do
  celé části (nebo aspoň ztlumit všechny desetiny a podtrhnout jednotky Hz);
  vyžaduje zásah do `num_layout` — rozhodnutí o vzhledu.
- **Riziko opravy:** střední (layout velkého čísla).
- **Vztah k lekcím:** L-0089.
- **Stav:** otevřeno.

---

### F-0178 [S4] σy(1 s) v okně ALLAN dvakrát a z různých dat: graf z ringu 24 vzorků, tabulka vedle z ringu 120 vzorků

- **Místo:** `CM7/app/screens/screen_main.c:1688` (graf: `adev_stage_kind(0,1,…)`,
  ring stage 0 `ADEV_RING` = 24), `:3029` (tabulka: `stats_adev(1)` nad
  plochým ringem `STAT_N` = 120); obojí v okně ALLAN (`app_gpsdo.c:2515-2516`).
- **Popis:** Pro τ = 1 s je estimátor stejný (při m = 1 je overlapping
  a non-overlapping totéž), ale **okno dat** jiné — graf a tabulka vedle sebe
  tak ukazují dvě různá čísla pro tutéž veličinu. Pro τ ≥ 10 s berou obě
  místa totéž (`adev_stage`).
- **Dopad:** kosmetický, ale mate: uživatel porovná bod grafu s tabulkou
  a nesouhlasí. Na rozptylu odhadu z 24 vs 120 vzorků se liší o desítky %.
- **Návrh opravy:** tabulka pro τ = 1 s z `adev_stage(0,1)` (konzistentní
  s grafem), nebo naopak graf z plochého ringu; jeden zdroj.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0018.
- **Stav:** otevřeno.

---

## Komentářový dluh (F6, bez vlivu na chování)

- `CM7/app/app_gpsdo.c:5094-5096` — *„`mp_budget` při neznámém hradle tiše
  dosadí 1 s a číslo by pak vypadalo stejně důvěryhodně"*: po F-0159 už
  výsledek nese `valid = 0`; a na ř. 5093 překlep „zeneka".
- `CM7/app/screens/screen_main.c:1309-1314` — „převod je exaktní" (součást
  F-0172, opravit spolu s ní).
- `CM4/LWIP/App/httpd_min.c:3219-3221` — „bin po binu jako přístroj" (součást
  F-0175).

## Oprava prvního průchodu

V [`2026-09-26_matematika-mereni.md`](2026-09-26_matematika-mereni.md), oddíl
„Co bylo zkontrolováno a je v pořádku", bod **Kadence vzorkování**, stojí,
že τ0 = 1 s je správně. **Rozestup ano, okno průměrování ne** — viz F-0171.
Bod byl opraven poznámkou odkazující sem.

## Shrnutí

**Verdikt: podmíněně funkční** (beze změny; nové vady jsou latentní do běhu
FPGA, s výjimkou F-0173, které platí už dnes nad datalogem Vc/teploty).

8 nálezů: **1× S2, 3× S3, 4× S4**.

1. **Vzorkování** (F-0171, F-0172) — vzorce jsou správně, ale dostávají
   vzorky jiné vzorkovací funkce, než předpokládají; jeden akumulátor
   `Σedges/Σgate` opraví obojí.
2. **Předpoklady statistiky** (F-0173, F-0176) — t-test a GUM použité mimo
   své předpoklady; F-0173 jde proti mému doporučení u F-0169.
3. **Dvojčata** (F-0174, F-0175, F-0178) — potřetí v modulu L-0012: web
   nesdílí opravy firmwaru a displej sám se sebou nesouhlasí.
4. **Prezentace** (F-0177) — podtržení tvrdí přesnost nad rozlišením.

🔑 Pro příště: v prvním průchodu jsem ověřil **vzorce** a **rozestup vzorků**,
ale ne **okno, přes které vzorek průměruje**. U čítače jsou to tři různé
věci a ADEV závisí na všech.

## Návrh triáže (F5.0)

| skupina | nálezy | proč |
|---|---|---|
| **A — opravit hned** | F-0174, F-0175 (web), F-0178 | jednoznačné, malý diff, firmware už správnou verzi má |
| **B — rozhodnout** | F-0171 + F-0172 (akumulátor; otázka navazujících oken FPGA a významu pole v datalogu), F-0173 (n_eff + formulace verdiktu), F-0177 (vzhled velkého čísla) | politika / kontrakt / vzhled |
| **C — odložit** | F-0176 (spolu s F-0168) | metrologická definice rozpočtu, až s reálnými daty |
