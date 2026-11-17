# Analýza grafiky (displej + web) a návrhy vylepšení — 2026-10-18

> **Rozsah:** jen analýza a návrhy, **nic z toho není implementováno** (zadání uživatele). Vychází ze čtení kódu
> (`screen_main.c`, `app_gpsdo.c`, `libui`, `httpd_min.c`) a z dnešních oprav grafů (v0.28.0). Vizuálně nic neověřeno
> (displej ani prohlížeč se při tomto průchodu nepoužily) — u návrhů je proto odhad nákladu, ne měření.
> Hodnocení: **přínos** (pro čtení měření) / **cena** (práce a riziko).

## 0. Omezení, která návrhy musí respektovat (z `CLAUDE.md`)

- Kreslí výhradně `UiTask`; žádný spin > ~10 ms (watchdog). Opakované překreslení celého grafu = milisekundy → drž ho na změny dat.
- Každý partial redraw začíná clearem (REPLACE nebo neprůhledný); kreslení přes `prim_internal_blend_px` (AA, oblouky, průhledné výplně) obchází `mark_dirty`, a spoléhá se na předchozí clear.
- Co běží na DMA2D souběžně se scan‑outem, má kopírovat lineárně (plná šířka); úzké obdélníky strided → podtečení LTDC.
- Velké fonty jsou subsetované (chybějící glyf se tiše přeskočí); `sans_14/16/18`, `mono_14/16/18/20/22` mají plný charset (včetně `±`, `τ`, horních indexů).
- Web: SPA je jeden C řetězec, bez `"` a `\`, bez regexů, atributy v `innerHTML` bez uvozovek (**a `/>` jen s mezerou před** — chyba z dnešního dne).
- Dvě úplné ovládací cesty (dotyk i encoder); cíl dotyku ≥ 60 px.

## 1. Co je v grafech dobře (nerozbíjet)

- Dekadické osy s vedlejšími logaritmickými čarami, popisky na dekádách, jednotka osy jednou (detail Allanu a náhled už stejné od v0.27.2).
- Konfidenční pás z EDF podle lokálního typu šumu (displej), podlaha čítače jako odlišená čára, typ šumu pod tabulkou.
- Čitelné stavy (LED tečky, pilulky), pás stavu smyčky pod grafem GPSDO (displej i web).
- Odečet pod kurzorem na webu (`.cx` + `.tip`), skrývání křivek v legendě, tmavé/světlé/tiskové schéma, `tabular-nums`.

## 2. Návrhy — displej

| # | Návrh | Přínos | Cena / riziko |
|---|---|---|---|
| G1 | **Značky událostí na časových grafech** (Trend, Grafy, GPSDO TIE, Spektrogram): svislá čárka + zkratka pro *reset statistiky*, *brána reference zavřena/otevřena*, *FLL→LOCK / HOLD*, *změna hradla/kanálu*, *ztráta signálu*, *výpadek GPS fixu*. Zdroje už existují (`errlog`, `gpsdo_get`, `g_stats_refgate_en`, `screen_main_stats_reset`). | **Vysoký** — skoky v grafu dnes vypadají jako HW jev (přesně ten problém, který řešila lekce L‑0170 u Allanu) | Střední: společný „event ring“ (čas + typ) a kreslení čárek v hotových grafech; čárky jsou tenké svislé čáry (lineární, nízké riziko podtečení) |
| G2 | **Referenční sklony v Allanu**: lehké čárkované přímky −1 (bílý PM), −½ (bílý FM), 0 (flicker), +½ (random walk) kotvené v poslední bodě, s popiskem; barva úseku křivky podle klasifikovaného šumu (`noise_desc` už typ určuje) | Vysoký — čtenář hned vidí, co oscilátor dělá, a ne jen číslo | Nízká–střední: čistá geometrie nad `pts[]`, jen ve velkém okně |
| G3 | **Maska/limit v Allanu**: uživatelská čára (např. datasheet OCXO nebo požadavek) a PASS/FAIL na bod; navázat na Math/Limity | Střední — přístroj by vyhodnotil sám, ne oko | Střední: persistence limitu, UI nastavení |
| G4 | **Měřená podlaha A−B** v Allanu (kap. 12.4 `docs/FUNKCE.md`): druhá čára = ADEV rozdílu kanálů při shodném signálu | Vysoký pro metrologii — ověří model 105 ps | Střední: druhá pyramida (nebo online ADEV z rozdílu), +1 křivka; nutné rozlišit kanály s a bez regrese |
| G5 | **Histogram**: sloupce nad 2σ jinou barvou (web už má `hb2`), šrafování ±1σ/±2σ, popisek N a šířky binu, kumulativní křivka; osa x v ppt vedle Hz | Střední | Nízká (po rozšíření ringu, viz FUNKCE 12.3) |
| G6 | **Spektrogram: perceptuální paleta** (viridis/cividis) místo dnešní modrá‑azurová‑zelená‑žlutá‑červená („jet“, nerovnoměrný jas, pro deuteranopii špatně rozlišitelná), číselná stupnice u barevné legendy, časová osa v minutách, dotykový odečet sloupce | Střední | Nízká: tabulka 64 barev místo `wf_heat` |
| G7 | **Odečet dotykem v grafech** (svislá čára + hodnota): dnes jen web | Střední | Střední až vysoké: čára se musí kreslit nad grafem bez problikávání → sdílet vzor `tap_flash` (obrys přes hotový obsah) a při změně dat zrušit |
| G8 | **Tvar nesoucí význam, ne jen barva**: stavy OK/WARN/BAD mají kromě barvy ikonu/znak (✓ ! ✕ nebo tvar LED) — platí i pro heat mapy a pilulky; KONTRAST schéma to zčásti dělá | Střední (barvoslepí, odlesk na slunci) | Nízká |
| G9 | **Kvalita měření u velkého čísla**: malá lišta/tečka „důvěra“ odvozená z rozpočtu nejistoty (`mp_budget`) nebo ze stavu LOCK + reference | Střední — číslo se dnes čte bez informace o stavu zdroje | Střední: nová malá plocha v zóně velkého čísla (clear oblast `redraw_freq` končí přesně na `SCR_MAIN_GRID_Y`) |
| G10 | **Fyzikální holdover kužel**: osa v ns za hodinu, ne bezrozměrná „šířka podle stavu“ | Vysoký, až bude stárnutí OCXO změřeno (STATUS #303/#311) | Závislé na datech; nelze dřív |

## 3. Návrhy — web

| # | Návrh | Přínos | Cena / riziko |
|---|---|---|---|
| W1 | **Propojený kurzor** přes všechny časové grafy (frekvence, teploty, Vc/DAC, GPSDO TIE, Kvalita GPS): jeden čas = svislá čára ve všech kartách, bublina jen v té, nad kterou je myš | **Vysoký** — porovnání příčin a následků (teplota ↔ Vc ↔ kmitočet) bez ručního sledování | Střední: odečet je dnes per‑graf (`HOV[kind]`); přibude sdílený čas a odvození x→čas |
| W2 | **Výběr rozsahu tažením** (brush) a zoom v detailu místo samých presetů okna | Střední | Střední |
| W3 | **Značky událostí** (stejné jako G1) z `/api/errlog` a stavu `gpsdo` | Vysoký (viz G1) | Střední: endpoint už existuje, graf musí umět svislé značky |
| W4 | **Export z detailu**: PNG (SVG→canvas) a CSV (data už jsou v JS) | Střední | Nízká; pozor na pravidla SPA (žádné `"`/`\`) |
| W5 | **Konfidenční pás Allanu** i na webu (v kódu SPA jsem ho nenašel, displej ho má z EDF) | Střední — web a displej ať říkají totéž | Střední: EDF podle typu šumu se musí přenést do JS a držet v souladu s firmwarem (`stat_test`) |
| W6 | **Stejná křivka, jiný styl**: rozlišit řady i tvarem čáry (plná/čárkovaná/tečkovaná), nejen barvou; popisky přímo u konce čáry místo legendy | Střední (barvoslepí, tisk) | Nízká |
| W7 | **Mobilní zobrazení detailu**: dnes jsou `@media (max-width:640px)` pro mřížky; ověřit detail (overlay) a odečet dotykem (bez hoveru) | Střední — nelze posoudit bez prohlížeče | Nízká–střední, **nejdřív ověřit** v prohlížeči/emulaci |
| W8 | **Karta Spektrogram jako skutečný spektrogram** (FFT × čas) vedle pásu odchylky; web má dost dat (≈ 4 vzorky/s) | Střední | Střední: JS FFT už existuje (`pnFft`) |

## 4. Doporučené pořadí

1. **G1/W3 značky událostí** (největší informační zisk, data existují).
2. **W1 propojený kurzor** (jen web, nízké riziko pro přístroj).
3. **G2 referenční sklony** a **G4 měřená podlaha A−B** (metrologicky nejcennější; G4 vyžaduje druhou pyramidu).
4. **G6 paleta spektrogramu**, **G5 histogram** (drobné, po rozšíření ringů).
5. G7 odečet dotykem až po vyzkoušení vzoru na jednom grafu.

## 5. Co jsem neposoudil (a proč)

- Skutečný vzhled — žádný screenshot displeje ani webu; veškeré úsudky o čitelnosti jsou z kódu a konstant.
- Čitelnost barev v jednotlivých tématech, hustota popisků na malém displeji (800×480, 8,54 px/mm), chování na mobilu.
- Dopad na CPU (UiTask) — žádné měření `stats` v této relaci; odhady nákladu jsou hrubé.
