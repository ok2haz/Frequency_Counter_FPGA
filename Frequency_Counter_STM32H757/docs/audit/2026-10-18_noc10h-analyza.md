# Rozšířená analýza 10h běhu smyčky GPSDO (noc 2026-10-17 → 18) — 2026-10-18

> **Rozsah:** jen analýza hotového záznamu (`docs/audit/data/2026-10-18_noc10h.csv`, 2,4 MB jako `.csv.gz`), na desku se nesahalo, firmware se nemění.
> **Nástroje:** `tools/analyze_gpsdo_log.py`, `tools/cov312.py` (beze změny) a nový `tools/analyze_noc_ext.py`
> (`python tools/analyze_noc_ext.py <csv|csv.gz> [ořez_s] [srovnání.csv]`). Úplný výstup: `docs/audit/data/2026-10-18_noc10h_analyza.txt`.
> **Stav všeho níže:** ⬜ jeden běh, jedna noc. Co číslo NEdokazuje, je napsáno u něj.

## 0. Podmínky běhu

| | |
|---|---|
| Doba | 2026-10-17 22:29:21 → 2026-10-18 08:29:21 (10,00 h), uptime 964 → 36 964 s |
| Konfigurace | smyčka GPSDO **LOCK celých 10 h** (zamčeno ~280 s po startu přístroje, tj. 684 s před začátkem logu), τ smyčky 1000 s, K 3,4·10⁻¹¹/LSB, sawtooth `−q`, hradlo čítače 0,25 s, vstup A = externí GPSDO |
| Událost před startem | `errlog`: **power‑cyklus 22:13:17** (16 min před startem logu) → běh zachytil **zahřívání OCXO po zapnutí** (viz 5) |
| Integrita | 0 resetů přístroje, 1× „NEODPOVÍDÁ“ (03:02:37, 3 s), 1PPS odmítnuto **0** (čítač `gp_pps_rej` zůstal 26 jako na začátku), 0 děr 1PPS, družic 8–12, HDOP 0,7–1,3 |
| Měření čítače | 144 001 oken 0,25 s, **chybí 430 (0,30 %)** — 55× zastavení čtení loggerem o 1–3 s (23 z nich v 1. hodině), nesouvisí s dotazy `/api/stab`; příčina nevyšetřena, na výsledky nemá vliv |

## 1. Hlavní výsledky (shrnutí)

1. **Dva nezávislé GPS řetězce souhlasí na 3·10⁻¹³ ± 2,3·10⁻¹²** (průměr hodinových průměrů čítač − externí GPSDO po ořezu 1 h; hodinové průměry −0,008 … +0,012 ppb, bez trendu −0,0008 ppb/h). Přesnost absolutního kmitočtu je tedy v rámci toho, co GPS dává, na úrovni jednotek 10⁻¹².
2. **Stabilita OCXO řízeného GPS (fáze proti 1PPS):** ADEV 1,1·10⁻¹¹ @ 1000 s, 6,1·10⁻¹² @ 2048 s, 3,5·10⁻¹² @ 4096 s (MDEV 5,7 / 3,2 / 1,7·10⁻¹²). **Mezi dvěma nocemi se shoduje na ~10–15 %** (MDEV @ 64…4096 s, viz 2) — výsledek je reprodukovatelný.
3. **Chyba času proti 1PPS:** sd 9,5 ns, rozpětí 63 ns (hodiny 1–9; po hodině 2 sd 5–10 ns), 94 % času v ±20 ns, MTIE 43 ns @ 1000 s, 58 ns @ 10 000 s. ⚠️ je to proti 1PPS NAŠEHO přijímače, ne proti UTC.
4. **Otázka #312 (OCXO, nebo GPS?) se z tohoto uspořádání NEDÁ rozhodnout ani delším během.** Čítač měří externí GPSDO, které šumí 10× víc než zkoumaný přebytek; očekávaná koherence při platnosti hypotézy „je to OCXO“ je 0,001–0,004, k detekci by bylo třeba **~1 300–2 300 nezávislých bloků = 2–7 dní** souvislého záznamu. Rozhodne referenční Rb (M2, #313): ADEV čítače při 100 s s Rb je ~2·10⁻¹², tedy o řád pod hledaným přebytkem, a vyjde za hodiny.
5. **Hrbol ADEV vůči externímu GPSDO (8·10⁻¹⁰ @ 20 s) je pásmově omezený šum jeho smyčky** — spektrum má plošinu 2–30 mHz a strmý pokles nad ~30 mHz (21× během jedné oktávy), tj. roh ≈ 25 mHz (časová konstanta ~6 s). Sedí na publikované měření téhož zdroje (smyčka τ ~ 10 s, 2018). Žádné diskrétní tóny (> 12× lokálního mediánu): do čítače neprosakuje 1PPS, dither DAC ani obnova SDRAM.
6. **Firmware ADEV (pyramida v0.23–0.27) souhlasí s nezávislým výpočtem** (numpy z 1s průměrů): τ ≤ 50 s shoda 1,00; 100–5000 s firmware 0–12 % pod výpočtem z celého běhu a do 1–4 % od výpočtu po ořezu 1. hodiny (jediná výjimka 200 s, +9 %).
7. **Filtr izolovaných odlehlých vzorků zasahuje zanedbatelně:** firmware vyřadil 8 z 35 457 vzorků (0,023 %), nezávislá simulace pravidla našla 5 (+ 4 shluky/skoky, firmware `rm = 4`), velikost 0,4–0,6 ppb → vliv na ADEV(1 s) řádově jednotky %.
8. **DAC kód a stárnutí:** kód 24 918 → 24 926 LSB (**0,76 LSB/h = 26 ppt/h = 0,62 ppb/den**). ⚠️ **NEní to stárnutí OCXO v ustáleném stavu** — běh začal 16 min po zapnutí a teplota OCXO klesala 47,9 → 47,3 °C; předchozí noc byl sklon **6 LSB/h (150 LSB/den)**, tedy ~8× vyšší, a klesá. Na holdover (#303/#311) se z toho dá použít jen horní mez (kap. 6).

## 2. Faze OCXO−GPS: kvantizace, přebytek, typ šumu

Fáze `xr` (OCXO proti 1PPS, po opravě sawtooth) je na mřížce 1 s (podle `gp_t_s`, doplněno interpolací 0,16 %). **Šum bílé fáze z kvantizace vychází σx = 2,89 ns, přesně 10 ns/√12** (z ADEV @ 2, 4, 8 s); do τ ≈ 8 s je záznam fáze zcela omezený kvantizací 10 ns. Přebytek = √(σ² − σ²_kvant); u MDEV je kvantizace potlačená silněji (τ^−1,5), takže je přesnější:

| τ [s] | ADEV | MDEV | TDEV [ns] | ADEV kvantizace | **přebytek (MDEV)** |
|---|---|---|---|---|---|
| 16 | 3,35·10⁻¹⁰ | 1,09·10⁻¹⁰ | — | 3,13·10⁻¹⁰ | 7,6·10⁻¹¹ |
| 64 | 9,78·10⁻¹¹ | 4,38·10⁻¹¹ | 1,6 | 7,83·10⁻¹¹ | 4,3·10⁻¹¹ |
| 128 | 5,77·10⁻¹¹ | 2,99·10⁻¹¹ | 2,2 | 3,92·10⁻¹¹ | 3,0·10⁻¹¹ |
| 256 | 3,45·10⁻¹¹ | 1,85·10⁻¹¹ | 2,7 | 1,96·10⁻¹¹ | 1,9·10⁻¹¹ |
| 512 | 1,78·10⁻¹¹ | 9,2·10⁻¹² | 2,7 | 9,8·10⁻¹² | 9,1·10⁻¹² |
| 1024 | 1,09·10⁻¹¹ | 5,7·10⁻¹² | 3,4 | 4,9·10⁻¹² | 5,7·10⁻¹² |
| 2048 | 6,10·10⁻¹² | 3,2·10⁻¹² | 3,8 | 2,5·10⁻¹² | 3,2·10⁻¹² |
| 4096 | 3,54·10⁻¹² | 1,7·10⁻¹² | 3,9 | 1,2·10⁻¹² | 1,7·10⁻¹² |

- **Nezávislost na ořezu rozběhu:** ořez 1 h / 2 h / 4 h dává MDEV @ 128 s 2,99 / 2,93 / 2,99·10⁻¹¹ a @ 1024 s 5,7 / 5,8 / 6,2·10⁻¹² — zahřívání (kap. 5) stabilitu fáze na těchto τ neovlivňuje.
- **Hodinová okna** (hodiny 1–9): MDEV @ 100 s = 2,2–4,1·10⁻¹¹ (průměr 3,3, sd 0,6), @ 500 s = 0,4–1,5·10⁻¹¹. Korelace s počtem družic r = −0,23 (100 s) / +0,08 (500 s), s HDOP +0,12 / −0,05 (n = 9, **bez průkazné vazby**; rozsah podmínek úzký: družic 8,7–11,4, HDOP 0,77–1,03).
- **Reprodukovatelnost mezi nocemi** (MDEV nyní / předchozí noc, 10⁻¹¹): 64 s 4,38 / 4,62; 128 s 2,99 / 2,87; 256 s 1,85 / 1,62; 512 s 0,92 / 0,82; 1024 s 0,57 / 0,62; 2048 s 0,32 / 0,33; 4096 s 0,17 / 0,19. Shoda ~10–15 % napříč dvěma nocemi s odlišnými podmínkami GPS (jiné družice, jiná teplota, jiný čas po zapnutí).
- **Typ šumu přebytku:** spektrum fáze nad kvantizací má v pásmu 1–16 mHz sklon S_x ∝ f^−1,4…−1,9 (blízko bílého FM, −2) a MDEV přebytku klesá se sklonem −0,7 (mezi bílým FM −0,5 a flicker PM −1). Co z toho plyne o původu (OCXO / GPS) viz 4.

**Kvantizace 10 ns a smyčka:** v xr tvoří kvantizace při τ = 1000 s ADEV 4,9·10⁻¹² (~20 % rozptylu proti naměřeným 1,09·10⁻¹¹), při 4096 s 1,2·10⁻¹² (~12 %). Zlepšení 1PPS na 5 ns / 2,5 ns (#313 bod 3, IDDR) by tedy zmenšilo rozptyl fáze při 1000 s nejvýš o ~15 %; zbytek je přebytek. **Priorita IDDR je nízká**, pokud se #312 nerozhodne ve prospěch OCXO.

## 3. Čítač proti externímu GPSDO

- **Okno 0,25 s:** sd 1,02 ppb (široké chvosty jsou pomalá složka externího GPSDO, ne výpadky).
- **ADEV (1 s průměry, přes celý běh):** 1,4·10⁻¹⁰ @ 1 s, 3,4·10⁻¹⁰ @ 5 s, 5,9·10⁻¹⁰ @ 10 s, **7,9·10⁻¹⁰ @ 20 s (vrchol)**, 6,3·10⁻¹⁰ @ 50 s, 4,6·10⁻¹⁰ @ 100 s, 2,4·10⁻¹⁰ @ 200 s, 1,0·10⁻¹⁰ @ 500 s, 5,3·10⁻¹¹ @ 1000 s, 2,9·10⁻¹¹ @ 2000 s, 1,0·10⁻¹¹ @ 5000 s. Hrbol je stejný jako při hradle 1 s i 0,25 s v předchozích bězích, takže není z čítače (`docs/MERENI_A_KALIBRACE.md`).
- **PSD y (Welch, 30 segmentů po 2048 s, rozlišení 0,49 mHz), 10⁻²⁰/Hz:** 1261 (1–2 mHz), 3410 (2–4), 4905 (4–8), 3091 (8–16), 1905 (16–30), **89 (30–60)**, 5,1 (60–120), 3,6, 3,2, 2,2, 1,6 (1–2 Hz). Plošina do ~30 mHz a pokles o faktor 21 v jedné oktávě = nízkopropustná smyčka s rohem ≈ 25 mHz; nad 60 mHz zůstává plochý podklad (2–5·10⁻²⁰/Hz, bez f²‑nárůstu kvantizace).
- **Tóny:** žádný vrchol > 12× lokální medián v celém pásmu do 2 Hz.

## 4. Otázka #312 — co data říkají a co ne

**Křížové spektrum** y_čítač × y_fáze (14 segmentů po 4096 s, resp. 62 po 1024 s; mez 95 % pro koherenci² 0,21, resp. 0,05): koherence² ≤ 0,07 ve všech pásmech od 0,2 mHz do 250 mHz (nejvyšší 0,068 v 0,2–0,5 mHz, Re/√ = −0,23; v pásmu 1–2 mHz 0,04 s kladným znaménkem). **Žádná průkazná kovariance v žádném pásmu**, tedy ani znaménko se interpretovat nedá. Kladné korelace z `cov312.py` (+0,16 … +0,28 při 100–500 s, 2,4–2,6 σ) spektrum NEpotvrzuje; nesedí ani na očekávaný záporný model (cov = −var(O) − cov(E,G)), takže jde nejspíš o náhodu při mnoha testech, nikoli o sdílení smyčkou. Horní mez z `cov312.py` (OCXO ≤ 0,6–1,4·10⁻¹⁰ pro 10–50 s, ≤ 1·10⁻¹¹ pro 200–500 s) je proto opět jen výpočet z SE, ne měření šumu.

**Proč metoda nemá citlivost** (výpočet v `analyze_noc_ext.py`, sekce 8): za hypotézy H_O (celý přebytek fáze je z OCXO) je koherence² = var(O)² / (var_čítač · var_fáze):

| τ [s] | ADEV čítače | ADEV fáze | přebytek | koherence² při H_O | potřebných bloků (~3/koh²) | doba |
|---|---|---|---|---|---|---|
| 64 | 5,2·10⁻¹⁰ | 9,8·10⁻¹¹ | 4,3·10⁻¹¹ | 0,0013 | 2 300 | 1,7 dne |
| 128 | 3,2·10⁻¹⁰ | 5,8·10⁻¹¹ | 3,0·10⁻¹¹ | 0,0022 | 1 340 | 2,0 dne |
| 256 | 1,6·10⁻¹⁰ | 3,5·10⁻¹¹ | 1,9·10⁻¹¹ | 0,0037 | 820 | 2,4 dne |
| 512 | 9,3·10⁻¹¹ | 1,8·10⁻¹¹ | 9,1·10⁻¹² | 0,0025 | 1 190 | 7,0 dne |

Dnešní běh má ~250 nezávislých bloků při 128 s, tj. mez detekce koherence je ~0,02, což je 10× nad očekáváním. **Žádný 10h záznam z tohoto uspořádání nic nerozhodne; 2–7 dní nepřetržitě by to jen hraničně.** Referenční Rb dává přímo ADEV(OCXO) bez kovariancí.

**Nepřímé argumenty (⬜ HYPOTÉZA, ne důkaz):**
- Pro GPS: přebytek je reprodukovatelný na ~10–15 % mezi dvěma nocemi (kap. 2) a nekoreluje s teplotou OCXO (r +0,10…+0,17 z `cov312.py`, n = 9 hodin); sklon (−0,7 u MDEV) odpovídá spíše pomalému plazení času přijímače než šumu krystalu.
- Pro OCXO: nic z dat; datasheet má jen horní mez ≤ 1·10⁻¹⁰ pro 0,1–1 s, ADEV 10–1000 s neuvádí. Publikované měření podobného OCXO (Trimble 34310‑T, volný běh) dává ≈ 3·10⁻¹² v rozsahu 1–100 s (článek 2018, jiný kus!) — to je o řád pod dnešním přebytkem (3·10⁻¹¹ @ 128 s), ale nemá vazbu na tento kus Bliley 6891.

`gpsdo_tau_advise` proto **zůstává dolní mezí** (nepřepisovat na doporučení).

## 5. DAC kód, teplota, stárnutí

- 30min bloky: kód 24 920,2 → 24 926,2; t_ocxo 47,75 → 47,32 °C; t_board 30,83 → 30,01 °C. Šum kódu po odečtení trendu sd 0,44 LSB; sklon v čase **0,76 ± 0,04 LSB/h (26 ppt/h)**.
- ⚠️ **Teplotu a čas nejde z jednoho běhu oddělit:** korelace T_ocxo~čas r = −0,970 (VIF 17), T_board~čas −0,854. Modely: jen teplota −17,4 ± 1,6 LSB/°C (0,59 ppb/°C — fyzikálně nepravděpodobně vysoké pro OCXO), teplota+čas +5,3 ± 3,3 LSB/°C a 0,98 ± 0,14 LSB/h (znaménko teploty opačné, nespolehlivé). Přírůstky po 30 min: dkód~dT_ocxo −0,27, dkód~dT_board −0,11 (n = 17, nevýznamné).
- **Zahřívání po power-cyklu:** běh začal 16 min po zapnutí (`errlog` 22:13:17), teplota pláště OCXO klesala z 47,9 °C (přeběh topení) k 47,3 °C a deska z 31,3 na 30,0 °C — tedy i tepelné ustalování celé desky. **0,76 LSB/h proto není stárnutí, ale horní mez stárnutí + tepelný dozvuk.** Předchozí noc: 64 LSB za 10 h (6 LSB/h), tedy ~8× víc o den dřív.
- **Dopad na holdover (jen horní mez, ⬜):** kdyby se po ztrátě GPS držel poslední kód a kmitočet v okamžiku ztráty byl dokonale správný, samotný lineární drift 7,2·10⁻¹⁵/s dá časovou chybu 0,5·a·t² = **47 ns po 1 h, 1,7 µs po 6 h**; zbytek (chyba kmitočtu při ztrátě, teplota) přibývá. Použitelný model driftu (#303/#311) potřebuje záznam po ≥ 2–3 dnech nepřetržitého napájení bez power-cyklu.
- Fáze: ustálená chyba smyčky (PLL 2. řádu vs. drift) odpovídá řádu: střední xr −21 ns v hodině 0 → −4 ns v hodině 9, předchozí noc −125 → −40 ns (rychlejší drift). ⬜ HYPOTÉZA, neodzkoušeno modelem.

## 6. Doporučení (pro rozhodnutí uživatele)

1. **Nepouštět další noční běh jen kvůli #312.** Rozhodne **M2 = Rb normál na CH A** (#313): 2× 2 h (`gpsdo off` volný běh / `gpsdo on`), ADEV(OCXO) přímo; podlaha čítače při 100 s s regresí ~2·10⁻¹², tedy o řád pod přebytkem 3·10⁻¹¹.
2. **IDDR 1PPS (#313 bod 3) odložit**, dokud #312 nepotvrdí OCXO — zlepšení nejvýš ~15 % rozptylu fáze @ 1000 s.
3. **Holdover model (#303/#311):** pro odhad stárnutí použít záznam z delšího nepřetržitého napájení; do datalogu přidáno `dac_code` už je (v0.19.0). Při dalším běhu neprovádět power-cyklus 48 h před začátkem.
4. **Logger:** 55× zastavení čtení o 1–3 s (1. hodina 23×) — případně zjistit, jestli je to PC, nebo odezva CM4; na výsledky nemá vliv.
5. **Zapsat `docs/audit/data/2026-10-18_noc10h_analyza.txt` jako referenční výstup** pro srovnání příštích běhů (`analyze_noc_ext.py <nový.csv> 3600 docs/audit/data/2026-10-18_noc10h.csv.gz`).

## 7. Co tato analýza neposoudila

- Absolutní přesnost vůči UTC (měřeno jen proti 1PPS přijímače a proti externímu GPSDO, které je samo GPS řízené).
- Vliv jiné antény / umístění (multipath) a jiného τ smyčky — žádný z těchto běhů nebyl.
- Teplotní koeficient OCXO (potřebuje řízenou změnu teploty nebo aspoň běh s větším rozsahem).
- Příčinu 55 zastavení loggeru.
