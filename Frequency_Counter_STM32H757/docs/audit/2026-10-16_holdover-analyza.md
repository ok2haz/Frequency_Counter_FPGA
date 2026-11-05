# Holdover: za jak dlouho OCXO ztratí přesnost (analýza 2026-10-16, STATUS #303)

> **Účel:** podklad pro rozhodnutí, jaký čas holdoveru a pro jakou přesnost má přístroj ukazovat na hlavní obrazovce.
> **Stav důkazů:** celý výpočet je **model z datasheetu**, ne měření. Co je měřeno a co ne, je v kap. 4. Označení `HYPOTÉZA` = nelze dokázat ze zdrojů v repu.

## 1. Co přístroj ukazuje dnes (důkaz z kódu)

- `holdover_cone_draw` (`CM7/app/app_gpsdo.c:4482`) kreslí kužel, jehož šířka roste jako `0,30 + t/600 s · 0,20` (strop 0,92). Komentář u funkce říká „Ilustrativní model". **Šířka neodpovídá žádné veličině** (ani ppb, ani µs).
- TFOM (`mp_tfom`, `meas_present.c:115`) má pro holdover **pevnou úroveň 6** bez ohledu na dobu bez GPS.
- Hlavní obrazovka ukazuje jen amber „H" v hlavičce (`!g.valid && g.fixes>0`), **žádný čas ani přesnost**.
- Nic z toho nezná, jak dobře byl OCXO před ztrátou GPS doladěn. A doladit se zatím nedá: smyčka (#82) ani vstup (1PPS do FPGA, #36/#262) neexistují.

## 2. Vstupy z datasheetu (Bliley 6891, NVG47A1282, `datasheets/Bliley_6891__NVG47A1282_RoHS-datasheet.pdf`)

| veličina | hodnota ze specifikace |
|---|---|
| teplotní stabilita | ±0,005 ppm v −5…+50 °C; ±0,020 ppm v −30…+70 °C |
| stárnutí | ±1,0 ppb/den; ±0,3 ppm/rok |
| napájení | ±1,0 ppb při změně 1 % |
| krátkodobá stabilita | ±1·10⁻¹⁰ pro 0,1…1 s |
| rozsah EFC | ±0,5 ppm min … ±1,1 ppm max; 2,5…5,5 Hz/V; 0…4 V; kladná strmost |
| vstupní impedance EFC | **10 kΩ … 100 kΩ** (tím je zodpovězeno z mého dřívějšího „neznámá"; naměřený sklon 0,98 AIN0 odpovídá ~50 kΩ) |
| zahřátí (vůči 30 min) | ±1 ppm za 2 min, ±0,1 ppm za 2,5 min, ±0,03 ppm za 5 min, ±0,01 ppm za 15 min |

Datasheet **neuvádí**, po jaké době od výroby platí 1 ppb/den (u nového kusu bývá stárnutí vyšší). Teplotní koeficient z datasheetu jako přímku neplatí (je to obálka přes −5…+50 °C); pro model beru **horní mez 0,18 ppb/°C** (10 ppb p-p / 55 °C). Dřívější dokumenty projektu používají 0,09 ppb/°C (±5 ppb / 55 °C) — jde o tentýž údaj počítaný jako „±" a „p-p".

## 3. Model a tabulky

Nezávislé složky chyby po ztrátě GPS (čas `t` od posledního disciplinovaného okamžiku):

- relativní odchylka kmitočtu `ε(t) = ε0 + εT + D·t`
- časová chyba `x(t) = x0 + (ε0 + εT)·t + D·t²/2`

`ε0` = zbytková chyba doladění v okamžiku ztráty (kvalita smyčky), `εT = ΔT · 0,18 ppb/°C` (kolísání teploty pláště), `D` = stárnutí, `x0` = 30 ns (fázová chyba 1PPS vůči OCXO v okamžiku ztráty, ⬜ HYPOTÉZA: údaj pro NEO-7M není v repu). Skript: `docs/audit/data/2026-10-16_holdover_model.py`, výstup `…_model.txt`.

Tři scénáře (parametry jsou **předpoklady**, ne měření):

| scénář | ε0 | stárnutí D | ΔT |
|---|---|---|---|
| **A** optimistický | 3·10⁻¹¹ | 0,3 ppb/den | 0,5 °C |
| **B** podle specifikace | 1·10⁻¹⁰ | 1,0 ppb/den | 1 °C |
| **C** pesimistický | 1·10⁻⁹ | 3,0 ppb/den | 2 °C |

### 3a. Čas do překročení přesnosti KMITOČTU |ε| ≤ cíl

| scénář | 1·10⁻⁸ | 5·10⁻⁹ | 1·10⁻⁹ | 5·10⁻¹⁰ | 1·10⁻¹⁰ | 5·10⁻¹¹ |
|---|---|---|---|---|---|---|
| A | 32,9 dne | 16,3 dne | 2,9 dne | 30,4 h | nelze | nelze |
| **B** | **9,7 dne** | **4,7 dne** | **17,3 h** | **5,3 h** | nelze | nelze |
| C | 2,9 dne | 29,1 h | nelze | nelze | nelze | nelze |

„nelze" = cíl je už v okamžiku ztráty překročen (ε0 + teplota je větší než cíl). U 10⁻¹⁰ rozhoduje **teplota pláště**: ΔT = 1 °C dá 1,8·10⁻¹⁰, tedy víc než cíl.

### 3b. Čas do překročení přesnosti ČASU |x| ≤ cíl (od okamžiku ztráty GPS)

| scénář | 1 µs | 10 µs | 100 µs | 1 ms |
|---|---|---|---|---|
| A | 2,0 h | 13,5 h | 2,4 dne | 8,4 dne |
| **B** | **54 min** | **6,6 h** | **30,4 h** | **4,5 dne** |
| C | 12 min | 112 min | 12,8 h | 2,4 dne |

### 3c. Dnešní stav (bez doladění, Vc = 0 V)

OCXO je o **8,4·10⁻⁷** vedle (naměřeno 2026-10-16: `freq_hz` = 10 000 008,4 Hz proti GPSDO). Časová chyba 1 µs vznikne za ~1 s, kmitočet 10⁻⁶ je „v toleranci". **Žádná z hodnot v 3a/3b dnes neplatí**, platí až po doladění smyčkou (bez ní je holdover bez smyslu: není co držet).

### 3d. Co z toho plyne pro řetězec smyčky (odpověď na „jak přesně")

| článek | řád | poznámka |
|---|---|---|
| krok DAC (AD5693R, LSB 76 µV) | **3,3–3,5·10⁻¹¹** (naměřeno strmost 4,3–4,6 Hz/V) | bez ditheru nelze doladit pod ~±2·10⁻¹¹; dither + RC (#277) dává 10⁻¹² |
| TDC kanál C (FPGA) | ~25 ps na jedno měření (σ dt z `tdc`, ~23–26 ps) | pro smyčku nepodstatné, dominuje GPS |
| GPS 1PPS (NEO-7M) | **~30 ns RMS** ⬜ HYPOTÉZA (ověřit v datasheetu u-blox, není v repu); s korekcí sawtooth (`UBX-TIM-TP`, #38) lepší | tento člen určuje, jak rychle se dá OCXO doladit |
| odhad kmitočtu z 1PPS za dobu τ | σy ≈ √2·σx/τ: **τ = 100 s: 4·10⁻¹⁰; 1000 s: 4·10⁻¹¹; 10 000 s: 4·10⁻¹²** (σx = 30 ns) | regrese přes více bodů je lepší; řád odhadu, ne měření |

Tedy: **ε0 ≈ 10⁻¹⁰ je dosažitelné** (scénář B) až po ~100–300 s průměrování a stabilní smyčce; 3·10⁻¹¹ (scénář A) vyžaduje τ ≳ 1000 s, dither a sawtooth korekci.

## 4. Co je změřeno a co NE (a jak změřit — TODO #303)

| parametr | stav | jak změřit |
|---|---|---|
| strmost EFC | ✅ změřeno 2026-10-16: 4,3–4,6 Hz/V (v souladu s 2,5–5,5) | — |
| stárnutí D | ⬜ NEZMĚŘENO | Vc na pevný kód (DAC drží), `freq_hz` vs externí GPSDO v datalogu ≥ 7 dní (lépe 30), regrese lineární + log(t); teplota se loguje (OCXO i deska) → dělit podle #42 |
| teplotní koeficient | ⬜ NEZMĚŘENO | tatáž řada: regrese `freq` na `T_ocxo`/`T_deska` po odečtení trendu; případně řízená změna okolní teploty (krabice/vnější ohřev ±3 °C) |
| ε0 (kvalita doladění) | ⬜ nelze (není smyčka) | po #82: zbytek `time_error_ps` slope v lock stavu, ADEV(τ) proti externí referenci |
| x0 (fáze 1PPS) | ⬜ nelze (není TIC) | po #36/#262: TI A−C s 1PPS na obou |
| skutečný holdover | ⬜ nelze | po smyčce: odpojit GPS anténu (nebo `gps off`) při běžícím externím GPSDO na CH A, logovat `freq_hz` a čas, zjistit, kdy |ε|/|x| překročí třídy z 3a/3b; opakovat při různé teplotě |
| napájení | ⬜ | ±1 ppb/1 %: sledovat 5 V vs `freq` |
| zahřátí | ✅ částečně: M3b/M4 (v ±10 ppb od ~145–160 s) | — |

## 5. Návrh zobrazení a rozhodnutí na vás

**Návrh (k rozhodnutí, nic se zatím nemění):**
1. Na hlavní obrazovce při holdoveru ukázat **uplynulý čas** od ztráty GPS (`H 01:23:45`) a **odhad času do překročení cílové třídy**. Cílová třída = jedno nastavení v okně Holdover.
2. Odhad počítat podle **scénáře B** (konzervativní), dokud nebudou změřeny D, teplotní koeficient a ε0; po měření přepočítat parametry (a rozdíl A/B/C zmizí).
3. Barva podle poměru uplynulého času k odhadu: zelená < 50 %, amber 50–100 %, červená > 100 %.
4. **Bez doladěné smyčky holdover nezobrazovat jako „přesný"** (3c): ukázat „HOLDOVER bez doladění" nebo vůbec ne.
5. Kužel v okně Holdover převést na skutečný obal `ε0 + D·t` ve ppb nebo na časovou chybu v µs podle volby třídy.

**✅ Rozhodnuto uživatelem 2026-10-16:** hlavní veličina = **ppb** (tabulka 3a). Odhad času holdoveru se **nezobrazí, dokud nebude změřeno stárnutí**. Pak se rozptyl přepočítá ze změřených hodnot a tabulka se nabídne k rozhodnutí znovu (STATUS #303). Smyčka a měření 1PPS: STATUS #305.

**Původní otázky (zbývá: třídy, TFOM):**
- které veličině dáte přednost jako *hlavní číslo*: **časová chyba (µs)** (praxe u GPSDO/telekomunikací) nebo **kmitočet (ppb)**,
- které třídy nabídnout (návrh: 1 µs / 10 µs / 100 µs / 1 ms, resp. 10⁻⁸ / 10⁻⁹ / 10⁻¹⁰),
- zda zobrazovat odhad ze scénáře B už teď (před měřením), nebo až po 7–30 dnech měření D,
- zda k tomu patří i TFOM (úroveň 6 pevně → odvodit z poměru času k odhadu).

**Dopad na kód (pro odhad práce, až rozhodnete):** nová funkce `holdover_estimate(class)` v Core (čistá logika + `selftest`), konfigurace třídy do `syscfg` (výplň, bez bumpu magicu), řádek na hlavní obrazovce (`screen_main`, partial redraw s `gate_same`), `/api/state` pole `holdover_s` + `holdover_est_s` (IPC bump → obě banky), SCPI `SYST:HOLD?`. Realistický odhad: střední změna, blokovaná jen rozhodnutím výše; **smyčka (#82) a 1PPS (#36) jsou na zobrazení nezávislé**, ale bez nich je číslo jen model.
