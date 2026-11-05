# Audit: vyšetření `FONTY: preskocenych glyfu 6` (2026-09-29)

- **Zdroj:** UART `status` na živé desce (COM10, po power-cyklu z předchozí
  relace tohoto dne) hlásil `FONTY: preskocenych glyfu 6  <== NEKDE CHYBI TEXT
  (subsetovany font)` — počítadlo `s_missing_glyphs` (`libprim/src/text.c`,
  audit F-0034/L-0017) je nenulové.
- **Jádro / doména:** CM7 — `libui/tools/font_gen/gen_fonts.js` (definice
  znakové sady `ui_font_mono_25`), `app/app_gpsdo.c` (volající kód)
- **Metoda:** statická — vypsány všechny call-sites `ui_font_mono_25`
  (jediný podezřelý z pěti omezených fontů — `sans_32`/`mono_30`/`mono_75`/
  `mono_52` mají charset ověřený proti jejich jediným/known konzumentům),
  každý string obsah zkontrolován proti přesné znakové sadě z `gen_fonts.js`.

## Souhrn

**Nalezena příčina: F-0200.** `app_gpsdo.c:8146` (`tick_animdemo()`, dlaždice
č. 4 „eased číslo" v okně **PŘÍKLADY ANIMACÍ**, `s_view=25`) formátuje
znaménkové číslo pomocí `"%+ld"` — `+` se u kladných hodnot vynucuje
explicitně. Vykresluje se fontem `ui_font_mono_25`, jehož znaková sada
(`gen_fonts.js:47`: `'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz
:.,-/()=?Δ'`) **obsahuje `-`, ale ne `+`**. Ověřeno i v kompilovaném fontu
(`ui_font_mono_25.c:490-495`): tabulka glyfů skáče z kódového bodu `41`
(`)`) rovnou na `44` (`,`) — `42` (`*`) i `43` (`+`) chybí.

Chybějící glyf se v `prim_draw_text` **tiše přeskočí** (`if (g == NULL)
continue;`, `text.c:138`) — u kladné hodnoty tedy demo ukáže číslo **bez**
znaménka (`123` místo `+123`), zatímco záporná hodnota vypadá správně
(`-123`, protože `-` v sadě je). Vizuálně nenápadné (demo tak jako tak jen
demonstruje jinou animaci na každé dlaždici), ale je to přesně ta třída
chyby, na kterou počítadlo existuje.

**Verdikt: S4 (kosmetický, izolovaný).** Postižené místo je čistě **interní
demo/testovací obrazovka** („ověřit každou animaci v izolaci" — CLAUDE.md),
ne produkční měřicí UI. Žádný uživatelsky relevantní údaj (kmitočet,
statistika, kalibrace) tímhle fontem/formátem neprochází.

---

### F-0200 [S4] Demo tik „eased číslo" (okno PŘÍKLADY ANIMACÍ) ztrácí `+` — chybí v `ui_font_mono_25`

- **Místo:** `CM7/app/app_gpsdo.c:8146` (formát) + `CM7/libui/tools/font_gen/gen_fonts.js:46-47`
  (znaková sada `ui_font_mono_25`).
- **Popis:** `snprintf(buf, sizeof buf, "%+ld", lround_f(v));` vynucuje `+`
  u kladných hodnot; `ui_font_mono_25` tenhle znak nemá (na rozdíl od `-`,
  který v sadě je od 2026-08-29 kvůli jinému, tehdy nalezenému nálezu).
- **Důkaz:**
  - Znaková sada v generátoru (`gen_fonts.js:47`): žádný `+`.
  - Kompilovaný font (`ui_font_mono_25.c:490-495`): glyfy `41→44` bez
    mezistupně `42`/`43`.
  - Jediný výskyt `"%+` formátovacího řetězce v celém CM7 (`grep -rn "%\+"
    CM7 --include=*.c`) — žádné jiné místo touhle třídou netrpí.
  - Živé počítadlo na desce (`status` → `FONTY: preskocenych glyfu 6`) —
    hodnota odpovídá tomu, že se demo dlaždice v době běhu (uptime ~347 s
    od power-cyklu) otevřela a několikrát aktualizovala kladnou hodnotu
    (cíl se mění pseudonáhodně v rozsahu −1000..+999 každých ~1,7 s, `f %
    34u == 1u`).
  - **Kontrastní příklad správného řešení téže třídy v tomtéž souboru:**
    `app_gpsdo.c:3907-3909` má explicitní komentář „Font: `mono_30` (charset
    `0123456789,.+-`), NE `mono_52`" — u okna ODCHYLKA×N byl přesně tenhle
    problém (znaménkové číslo + omezený font) už jednou vyřešen správně,
    volbou fontu, který `+`/`-` obsahuje. Demo tik #4 tenhle precedent
    nedodržel.
- **Dopad:** Kosmetický, jen na demo/testovací obrazovce (`s_view=25`,
  dostupná z Nastavení → Animace → „PŘÍKLADY ANIMACÍ"). Kladná hodnota se
  zobrazí bez znaménka. Nulový dopad na měření, kalibraci nebo bezpečnost
  přístroje. Vedlejší efekt: počítadlo `FONTY: preskocenych glyfu` zůstává
  nenulové, což maskuje případný **budoucí** skutečně závažný výskyt téže
  třídy (L-0017 — „tichý přeskok je přípustný jen s počítadlem", ale
  počítadlo, které nikdy neklesne na 0, přestává být užitečným signálem).
- **Reprodukce:** Menu → Nastavení → dlaždice „ANIMACE" → tlačítko
  „PŘÍKLADY ANIMACÍ >" → počkat, až dlaždice č. 4 (zleva dole, „eased
  číslo") ukáže kladnou hodnotu → chybí `+` před číslicemi. Nebo `status`
  → sledovat, že `preskocenych glyfu` roste při otevřeném demu.
- **Návrh opravy (dvě rovnocenné varianty, různá cena):**
  1. **Přepnout font tohoto jednoho `prim_draw_text` volání** na font
     s plným charsetem obsahujícím `+` (např. `ui_font_mono_22`, který má
     `.glyph_count` odpovídající plné sadě). Nejnižší riziko — mění se jeden
     řádek v `app_gpsdo.c`, žádný zásah do generovaných fontů, žádná
     nutnost přegenerovat binární font a ověřovat velikost obrazu. Vizuálně
     mírně menší číslice (22 px místo 25 px) na demo dlaždici — nepodstatné.
  2. **Přidat `+` (případně i `*`) do znakové sady `ui_font_mono_25`**
     v `gen_fonts.js` a přegenerovat font (`node gen_fonts.js`). Konzistentní
     s tím, jak se stejná třída chyby řešila 2026-08-29 (rozšíření sady
     o malá písmena a interpunkci). Cena: běh Node.js generátoru, ověření
     `glyph_count` +1, narůst `.text` o pár desítek bajtů (nová bitmapa
     jednoho glyfu), plný `build.sh`/`audit.py` řetězec — víc pohyblivých
     částí, ale řeší to systémověji (kdyby přibyl další znaménkový výpis
     tímhle fontem, `+` už tam bude).
- **Riziko opravy:** nízké u varianty (1), nízké až střední u varianty (2)
  (font pipeline zásah, i když dobře zdokumentovaný a už jednou úspěšně
  provedený).
- **Vztah k lekcím:** `L-0017` (tichý přeskok je přípustný jen s
  počítadlem — počítadlo tady udělalo přesně svou práci, umožnilo nález).
  Souvisí s historickým F-0034 (15 neviditelných řetězců, stejná třída,
  stejný mechanismus `if (g == NULL) continue;`).
- **Stav:** otevřeno (skupina A — obě navržené varianty jsou jednoznačné,
  malé, nemění chování ničeho jiného; čeká na volbu varianty a schválení).

---

## Co bylo zkontrolováno a je v pořádku

- **`ui_font_sans_32`** (`Hzsmunp`, 7 glyfů) — jediné volací místo
  (`screen_main.c:893`, jednotka velkého čísla) prokazatelně vykresluje jen
  „Hz" nebo periodovou jednotku (s/ms/us/ns/ps) — všechny znaky v sadě.
- **`ui_font_mono_30`** (`0123456789,.+-`) — obsahuje `+` i `-`; použití
  u ODCHYLKA×N (`app_gpsdo.c:3907-3909`) je **správný** precedent, jak se
  má znaménkové číslo s omezeným fontem řešit.
- **`ui_font_mono_75`/`ui_font_mono_52`** — jen číslice (+`DGOPS` u 75 pro
  splash „GPSDO"), žádné volací místo nezkouší jiný znak.
- **Všech ~24 volacích míst `ui_font_mono_25`** kromě nalezeného — prošla
  ručně (window_chrome tituly ×47, stavové popisky, formátovaná čísla se
  `,`/`.`/`-`/`(`/`)`, heslo generátor) — žádné jiné nepoužívá znak mimo
  sadu.

## Nezkontrolováno / omezení tohoto běhu

- **Přesný historický průběh 6 výskytů** — počítadlo je kumulativní od
  bootu (uptime ~347 s v době čtení), nejde zpětně dokázat, že VŠECH 6 bylo
  z tohohle jednoho místa (jen že je to jediné nalezené místo, které tuhle
  třídu vady vůbec může způsobit — `grep` na `"%\+` v celém CM7 dal jediný
  výskyt).
- **Oprava neproběhla** — čeká na volbu varianty (1) vs. (2) a schválení.
