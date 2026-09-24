# Audit: vykreslovací řetězec (libprim + libui + HAL most)  (2026-09-10)

- **Commit:** `ef89f45` (větev `audit/2026-09-09-hodiny-pwr`), pracovní strom bez změn v kódu
- **Jádro / doména:** CM7 / D1 + SDRAM (framebuffery v MPU oblasti 0)
- **Projité sekce checklistu:** C (DMA/cache/umístění bufferů — těžiště), D (souběh, jen v rozsahu
  „kdo kreslí"), E (ošetření chyb), G (DMA2D/LTDC jako periferie)
- **Neprojité (a proč):** A, B, F (modul nekonfiguruje hodiny, nemluví s CM4, nesahá na Flash),
  H (errata — DMA2D/LTDC errata pro rev V nedohledána, viz „Nezkontrolováno")

## ⚠️ Rozsah: modul 8 rozdělen

Modul 8 byl v `AUDIT_STATUS.md` zapsaný jako „aplikační logika UI — `app_gpsdo.c`,
`screens/*`, `libui`", což je **14 375 řádků** (bez fontů). To je 4× víc než modul 7
a jedno sezení to nemůže přečíst celé, aniž by audit sklouzl k povrchnosti — což
`AUDIT_PLAYBOOK.md` §5 zakazuje. Použit **týž postup jako u modulu 6**, který se ze
stejného důvodu rozdělil na 6 + 7:

| # | nový rozsah | řádků |
|---|---|---|
| **8** | **vykreslovací řetězec** — `prim_stm32_hal.c`, `libprim/*`, `libui/*` (bez fontů) | **2 247** |
| 9 | hlavní obrazovka — `screens/*` | 3 272 |
| 10 | aplikační okna, navigace, model fokusu — `app_gpsdo.c` | 9 188 |

Modul 8 je soudržná jednotka: vykreslovací engine nezávislý na aplikaci, a leží v něm
triple buffering, dirty-rect copy-forward a koherence s DMA2D — tedy třída vad, kterou
má projekt zdokumentovanou opakovaně (STATUS #88, #97, #137, #141, #139).

## Přečtené soubory

Celé: `CM7/app/hal/stm32/prim_stm32_hal.c` (373), `CM7/libprim/src/fill.c` (201),
`CM7/libprim/src/fb.c` (120), `CM7/libprim/src/text.c` (165),
`CM7/libui/src/theme.c` (174), `CM7/libui/src/bargraph.c` (132).
Cíleně: `screen_main.c` (`blit_bg_region`, `freq_tint_if_stopped`,
`screen_main_redraw_freq_area` — kvůli dosažitelnosti F-0032),
`libprim/include/prim/types.h` (`PRIM_RGB`/`PRIM_RGBA`/`PRIM_A`),
`docs/ARCHITECTURE.md` §2–§3 (mapa paměti a MPU).

## Souhrn

Vykreslovací řetězec je **nadprůměrně dobře promyšlený** a to je věcné zjištění, ne
zdvořilost: tři framebuffery, flip při vblanku přes `LTDC_SRCR_VBR` (ne
`HAL_LTDC_SetAddress`, který by trhal), non-blocking čekání na *předchozí* flip,
dirty-rect copy-forward s dedup, který prokazatelně nikdy nezvětší kopírovanou množinu
mimo dirty, a mrtvý čas DMA2D změřený na desce proti podtečení LTDC. Několik
netriviálních invariantů je v komentářích doložených měřením, ne domněnkou.

**Verdikt: funkční.** Nenašel jsem nic, co by dnes kreslilo špatně. Všechny čtyři
nálezy jsou **latentní pasti a chybějící diagnostika** — tedy třída „funguje, dokud
někdo nenapíše nový kód podle pravidla, které tiše neplatí". Nejzávažnější je
**F-0032**: invariant „každý partial redraw musí začít clear" je v `CLAUDE.md`
napsaný jako absolutní, ale `prim_fill_rect` ho splní jen pro neprůhledné barvy.

---

### F-0032 [S3] `prim_fill_rect` s alfou < 255 obchází `mark_dirty` — invariant „clear na začátku" tiše neplatí

- **Místo:** `CM7/libprim/src/fill.c:30-46` (`prim_fill_rect`), `:16-28` (`sw_fill`)
- **Popis:** `prim_fill_rect` jde přes DMA2D backend (a tedy přes `mark_dirty`) **jen**
  když je `blend == PRIM_BLEND_REPLACE` **nebo** `PRIM_A(color) == 0xFF`. Pro
  `PRIM_BLEND_OVER` s poloprůhlednou barvou spadne do `sw_fill`, který zapisuje přímo
  do `fb->pixels` — **bez jakéhokoli záznamu do dirty setu**.
- **Důkaz:**
  - `fill.c:37-38`: podmínka fast-path `(blend == PRIM_BLEND_REPLACE || PRIM_A(color) == 0xFFu)`.
  - `fill.c:16-28`: `sw_fill` píše `row[x] = ...` přímo, `mark_dirty` nikde.
  - `prim_stm32_hal.c:70-82`: `mark_dirty` se volá **výhradně** z `d2d_fill` (`:105`)
    a `d2d_blit_ex` (`:123`), tedy jen z DMA2D cesty.
  - `prim_stm32_hal.c:9-12` (hlavička souboru) tvrdí: „Protoze KAZDA zmena zacina
    fill/blit (clear) … je dirty set = sjednoceni fill/blit obdelniku == vsechny zmeny."
    Ta věta pro poloprůhledný fill **neplatí**.
  - Táž absolutní formulace je v `CLAUDE.md` (ZLATÁ PRAVIDLA / Rendering): „**Každý
    partial redraw MUSÍ začít clear (fill/blit REPLACE)**". Slovo `REPLACE` tam je,
    ale o řádek dál se mluví obecně o „fill/blit" a `sw_fill` cesta pojmenovaná není.
- **Dopad:** Dnes **žádný** — a je poctivé to říct rovnou. V celém projektu existuje
  jediná poloprůhledná barva: `theme.c:57,95,135` `freq_stop_bg` s alfou **72 / 58 / 96**.
  Její jediný volající je `screen_main.c:1091` (`freq_tint_if_stopped`) a **oba** jeho
  volající mají bezprostředně před ním neprůhledný `blit_bg_region(freq_clear_area())`
  (`screen_main.c:2704`, resp. plný render přes `render_body_number`), který dirty rect
  označí a je **širší** než tintovaná zóna. Past je tedy latentní.
  Kdyby ale někdo přidal poloprůhledné podbarvení jako *první* operaci partial redrawu
  (což je přesně to, co dnešní pravidlo dovoluje), oblast se nezkopíruje dopředu a
  vznikne **problikávání přes tři buffery** — třída vady, která na tomhle projektu
  stála tři kola ladění (STATUS #88, #141).
- **Reprodukce:** Staticky doložitelné z uvedených řádků. Na HW by se projevilo
  přidáním `prim_fill_rect(r, <alfa<255>, PRIM_BLEND_OVER)` jako jediného clearu
  partial redrawu a sledováním, jestli oblast bliká v rytmu tří snímků.
- **Návrh opravy:** Nejlevnější a nejbezpečnější je **oznámit dirty i ze softwarové
  cesty**, ne měnit chování volajících: v `prim_fill_rect` zavolat backendový
  `mark_dirty` i před `sw_fill`. Backend ho ale dnes nevystavuje — `mark_dirty` je
  `static` v `prim_stm32_hal.c` a `prim_dma2d_backend_t` má jen `fill_rect/blit/wait/
  draw_glyph`. Minimální varianta je tedy **rozšířit backend o `mark_dirty(dst,w,h)`**
  a volat ho z `sw_fill` (a případně z `prim_internal_blend_px`, viz „Souvislosti").
  Levnější, ale slabší varianta: nechat kód být a **opravit pravidlo** v `CLAUDE.md`
  a v hlavičce `prim_stm32_hal.c` na „clear musí být REPLACE **nebo neprůhledný**",
  ať se o ten předpoklad nikdo neopře.
- **Riziko opravy:** nízké u varianty s dokumentací, **střední** u rozšíření backendu —
  `sw_fill` běží i v hostitelském buildu (`PRIM_HOST_BUILD`), kde backend není, takže
  volání musí být podmíněné; a přidání dirty rectů navíc může u alfa-kreslení zvýšit
  počet kopírovaných obdélníků (strop `MAX_DIRTY` 48 → dřívější pád do `dfull`).
- **Vztah k lekcím:** nová lekce po opravě — „pravidlo, které platí jen pro část
  vstupů, musí tu podmínku nést v názvu nebo ve své vlastní kontrole".
- **Stav:** opraveno 2026-09-10 (dokumentací, ne kódem) — vědomě zvolena **varianta „opravit pravidlo“**, ne rozšíření backendu. Důvod: zařízení funguje, past je latentní (jediná poloprůhledná barva má před sebou neprůhledný blit) a 🔴 **oprava kódem by zhoršila F-0036** — víc dirty obdélníků znamená dřívější pád do `dfull`, tedy víc celoobrazovkových přenosů, a právě ty přetahovaly hlídací mez. Upřesněno na dvou místech: `CLAUDE.md` (ZLATÁ PRAVIDLA / Rendering) a hlavička `prim_stm32_hal.c`, obojí včetně toho, že `prim_internal_blend_px` (AA rohy, arc, glow) `mark_dirty` obchází **vždy**.

---

### F-0033 [S3] Chyby DMA2D se mažou, aniž by je kdo přečetl; obě čekací smyčky mají tichý timeout

- **Místo:** `CM7/app/hal/stm32/prim_stm32_hal.c:85-91` (`d2d_wait`), `:300-301`
  (čekání na dokončení flipu v `prim_stm32_present`)
- **Popis:** `d2d_wait()` po každém přenosu **zahodí** příznak chyby přenosu
  (`CTEIF`), aniž by ho kdokoli otestoval. Zároveň je hlídací smyčka omezená
  počítadlem, ale její vypršení se nikam nezaznamená a kód pokračuje, jako by
  přenos doběhl.
- **Důkaz:**
  - `:90` `DMA2D->IFCR = DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTEIF | DMA2D_IFCR_CCTCIF;`
    — mažou se tři příznaky, **žádný se předtím nečte**.
    `grep "DMA2D->ISR\|TEIF\|CEIF"` nad souborem vrací **jen tenhle jeden řádek**.
  - `DMA2D_IFCR_CCEIF` (konfigurační chyba) se **nemaže vůbec**.
  - `:88-89` `uint32_t guard = 0; while ((DMA2D->CR & DMA2D_CR_START) && ++guard < 2000000u) {}`
    — po vyčerpání se pokračuje a `d2d_fill`/`d2d_blit_ex` vzápětí přepíší registry
    a znovu nastaví `START` **nad možná stále běžícím přenosem**.
  - `:300-301` totéž pro `while ((LTDC->SRCR & LTDC_SRCR_VBR) && ++guard < 4000000u)`.
  - Protipól ve stejném souboru: podtečení FIFO LTDC **se počítá** (`g_ltdc_underrun`,
    `:289-296`) a vypisuje ve `status`. Chyby DMA2D nikoli — asymetrie diagnostiky.
- **Dopad:** Chyba přenosu DMA2D (typicky špatná adresa/konfigurace) se projeví jako
  **poškozený nebo nevykreslený obdélník** a nezanechá žádnou stopu. Protože se do
  téhož framebufferu dál copy-forwarduje, může se poškození držet přes snímky a
  vypadat jako vada paměti nebo panelu — tedy přesně směr, kterým se na tomhle projektu
  už několikrát chybně vyšetřovalo (viz tabulka „HW OBVINĚN — A BYL NEVINNÝ").
  ⚠️ Za normálního provozu se to neděje; význam je diagnostický, ne funkční.
- **Reprodukce:** `HYPOTÉZA — ověřit:` vynutit chybu přenosu (např. dočasně předat
  `d2d_fill` adresu mimo namapovanou SDRAM) a ověřit, že se to nikde neprojeví.
  Staticky je doložené, že se příznak jen maže.
- **Návrh opravy:** Přesně v idiomu, který projekt už používá u `g_ltdc_underrun`:
  před vymazáním přečíst `DMA2D->ISR`, a při `TEIF`/`CEIF` inkrementovat
  `g_d2d_errors`; totéž pro vypršení obou hlídacích smyček (`g_d2d_timeouts`,
  `g_ltdc_flip_timeouts`). Doplnit jeden řádek do `status`. Nemění se chování,
  jen přestane být ticho.
  ⚠️ Doplnit i `DMA2D_IFCR_CCEIF` do masky mazání, ať nezůstane viset.
- **Riziko opravy:** nízké — přidává se čtení registru a počítadla, kreslicí cesta
  se nemění. ⚠️ `d2d_wait` je v horké cestě (volá se 2× na každý fill/blit), takže
  čtení `ISR` musí zůstat jediné, ne v cyklu.
- **Vztah k lekcím:** `L-0003` (ignorovaný výsledek) a `L-0004` (čekací smyčka bez
  ošetření vypršení) — obojí je tu splněné jen napůl: timeout existuje, reakce ne.
- **Stav:** opraveno 2026-09-10, ⬜ **neověřeno na HW ve smyslu účinku** (počítadla na desce běží a hned naměřila F-0036, viz výše). Opraveno **tím způsobem, jak nález navrhoval**: `d2d_wait()` teď `ISR` přečte (jednou, kvůli horké cestě) a při `TEIF`/`CEIF` zvedne `g_d2d_errors`; vypršení obou hlídacích smyček zvedne `g_d2d_timeouts`, resp. `g_ltdc_flip_timeouts`. Do masky mazání doplněn `CCEIF`, který se dřív nemazal vůbec. Nový řádek `DMA2D:` ve `status`.

---

### F-0034 [S3] Chybějící glyf se tiše přeskočí a za běhu to nejde zjistit

- **Místo:** `CM7/libprim/src/text.c:125` (`prim_draw_text`), `:90` (`prim_text_width`)
- **Popis:** Když font nemá glyf pro daný znak, `prim_draw_text` ho **přeskočí bez
  jakékoli stopy** (`if (g == NULL) continue;`). Text prostě zmizí a nic to nehlásí.
- **Důkaz:**
  - `text.c:124-125`: `const prim_glyph_t *g = prim_internal_glyph(font, cp);
    if (g == NULL) continue;`
  - `text.c:89-90` (`prim_text_width`): `if (g) w += g->advance;` — šířka chybějící
    glyf taky ignoruje, takže kresba a měření **zůstávají v souladu** (to je správně
    a je to důvod, proč se vada neprojeví posunutým textem, ale úplným zmizením).
  - `CLAUDE.md` dokládá, že to není teorie: audit 2026-08-29 našel **15 takto
    neviditelných řetězců** (mj. splash „GPSDO" a text modalu „Opravdu restartovat?").
    Většina velkých fontů je subsetovaná (`mono_75`/`mono_52` jen číslice,
    `sans_32` jen `Hzsmunp`).
  - Regrese je živá i do budoucna: `CLAUDE.md` varuje, že při další regeneraci fontů
    „ten charset nesmí spadnout zpět na `'Hz'`" a navrhuje ruční kontrolu
    `grep glyph_count`. **Ruční kontrola po regeneraci je ale slabá vrstva** —
    přesně ta, o které lekce `L-0007` říká, že ji nikdo nespouští.
- **Dopad:** Chybějící popisek v UI, který se nijak neohlásí. Nejde o nefunkčnost
  přístroje, ale o vadu, která **projde všemi dnešními kontrolami** (překladač,
  `audit.py`, selftest) a odhalí ji jen to, že si někdo všimne prázdného místa
  na displeji.
- **Reprodukce:** Vykreslit velkým subsetovaným fontem řetězec s malým písmenem,
  např. `prim_draw_text(..., "Test", &ui_font_mono_75, ...)` → nevykreslí se nic.
- **Návrh opravy:** Runtime počítadlo v `libprim` (`prim_text_missing_glyphs()`),
  které `prim_draw_text` inkrementuje na `g == NULL`, a jeden řádek ve `status`.
  Tím se z „někdo si toho všimne" stane měřitelný údaj — týž idiom jako
  `g_ltdc_underrun` nebo `GPIO HLIDAC`, a `CLAUDE.md` sám říká, že **nový čítač do
  `status` je levnější než jedna špatná oprava**.
  ⚠️ Počítadlo musí být v `libprim` bez závislosti na `ui/*` (vrstvové pravidlo),
  a nesmí nic tisknout z kreslicí cesty (`printf` v UiTasku už jednou přetekl stack).
- **Riziko opravy:** nízké — jeden inkrement v už existující větvi.
- **Vztah k lekcím:** nová lekce po opravě — třída „tichý přeskok místo chyby";
  příbuzné `L-0003`.
- **Stav:** opraveno 2026-09-10, ⬜ neověřeno na HW (čítadlo hlásí 0 — což je očekávaný stav, ne důkaz funkčnosti; ověří až první subsetovaný font, který něco zahodí). Opraveno podle návrhu: `s_missing_glyphs` v `libprim/src/text.c` + `prim_text_missing_glyphs()` a řádek `FONTY:` ve `status`. ⚠️ Počítá se **jen** ve `prim_draw_text`, ne ve `prim_text_width` — ta se při zarovnání CENTER/RIGHT volá na týž řetězec navíc a chyby by se zdvojily.

---

### F-0035 [S3] Mezi zápisy CPU do framebufferu a startem DMA2D chybí `__DSB()`

- **Místo:** `CM7/app/hal/stm32/prim_stm32_hal.c:102-116` (`d2d_fill`), `:120-136`
  (`d2d_blit_ex`), `:193-217` (`d2d_draw_glyph`)
- **Popis:** Všechny tři funkce nastaví registry DMA2D a spustí přenos, aniž by
  předtím vložily bariéru. DMA2D přitom v témže snímku čte oblasti, do kterých
  bezprostředně předtím zapisoval **procesor** (CPU antialiasing textu, `sw_fill`,
  `prim_internal_blend_px`) — a u `d2d_draw_glyph` je framebuffer dokonce přímo
  vstupem (`BGMAR = dst`, `:206`).
- **Důkaz:**
  - `:113` `DMA2D->CR |= DMA2D_CR_START;` — bezprostředně předchází jen zápisy
    do registrů DMA2D, žádné `__DSB()`; totéž `:133` a `:213`.
  - `grep "__DSB\|__DMB"` nad `prim_stm32_hal.c` → **žádný výskyt**.
  - Framebuffery leží v MPU oblasti 0 jako **Normal, Write-Through**
    (`docs/ARCHITECTURE.md` §3, `main.c MPU_Config`). WT znamená, že zápis jde do
    paměti (nevzniká dirty řádek), **ale neruší to zápisovou frontu jádra** —
    pořadí Normal zápisu vůči následnému Device zápisu (registr DMA2D) není na
    ARMv7-M garantované bez `DSB`.
  - `CHECKLIST_STM32H7.md` sekce C tenhle bod výslovně obsahuje: „Kombinace
    `SCB_EnableDCache()` + zapisování do periferních struktur v RAM bez barrier
    (`__DSB()`, `__DMB()`) — zkontroluj pořadí operací."
  - Protipól: projekt bariéry jinde **používá** vědomě (IPC přes SRAM4 stojí podle
    `CLAUDE.md` „jen na `__DMB()`"), takže to není nevědomost, ale mezera zrovna tady.
- **Dopad:** Teoreticky by DMA2D mohl přečíst pozadí o pár zápisů starší, což by se
  projevilo jako **ojedinělý pixelový artefakt na hraně velkého textu** — tedy vada,
  kterou nikdo nereprodukuje a svede ji na SDRAM. ⚠️ Prakticky je pravděpodobnost
  nízká: mezi zápisem a startem přenosu leží několik funkčních volání a spin
  `d2d_wait()`, takže fronta zápisů se stihne vyprázdnit. **Neoznačuji to za
  příčinu žádného pozorovaného jevu.**
- **Reprodukce:** `HYPOTÉZA — ověřit:` staticky to dokázat nelze a na desce jde jen
  o statistiku artefaktů. Rozumnější než měřit je bariéru prostě doplnit — je zadarmo.
- **Návrh opravy:** `__DSB()` před `DMA2D->CR |= DMA2D_CR_START;` ve všech třech
  funkcích. Jedna instrukce, v horké cestě zanedbatelná proti samotnému přenosu.
- **Riziko opravy:** velmi nízké — bariéra nemůže nic rozbít, jen zpomalit.
  ⚠️ Platí pravidlo 7 `CLAUDE.md`: bariéry se **nepřidávají ani neodebírají** naslepo;
  tady se přidává tam, kde ji checklist vyžaduje.
- **Vztah k lekcím:** nová lekce po opravě, nebo rozšíření `L-0002` o případ
  „Device-mapovaná periferie čte Normal paměť, do které psal CPU".
- **Stav:** opraveno 2026-09-10, ⬜ neověřeno na HW (bariéra nemá pozorovatelný projev — viz `Dopad`). Opraveno podle návrhu: `__DSB()` před `DMA2D->CR |= START` ve všech třech funkcích. Ověřeno **v disassembly**, ne jen překlademem: `str [r4,#68]` (NLR) → `dsb sy` → `ldr/orr #1/str [r4,#0]` (CR |= START).

---

### F-0036 [S2] Hlídací mez v `d2d_wait()` vyprší na LEGITIMNÍM přenosu a DMA2D se pak přeprogramuje za běhu

- **Místo:** `CM7/app/hal/stm32/prim_stm32_hal.c` — `d2d_wait()` (mez `D2D_WAIT_GUARD`),
  volající `d2d_fill()`, `d2d_blit_ex()`, `d2d_draw_glyph()`
- **Popis:** Hlídací smyčka má mez **2 000 000 iterací**, což je při 480 MHz zhruba
  **12 ms**. Celoobrazovkový přenos (768 000 B) s mrtvým časem DMA2D `AMTCR.DT = 240`
  trvá řádově **stejně dlouho**. Mez tedy nechrání jen proti zatuhlému hardwaru —
  **vyprší i při normální práci**, a volající pak rovnou přepíše registry DMA2D a
  znovu nastaví `START` nad přenosem, který ještě běží.
- **Důkaz (změřeno na desce, ne odvozeno):** nález odhalilo až počítadlo přidané
  opravou F-0033. `status` po 25 s běhu: `DMA2D: chyb 0, timeout 14 | flip timeout 0`.
  Kontrolovaný pokus — čtení, vynucený plný redraw (`ui`), čtení, znovu:

  | `UI kresleni` flip | `DMA2D timeout` |
  |---|---|
  | 274 | 14 |
  | 336 | 23 |
  | 487 | 25 |

  Počítadlo **roste s kreslením** a nejvíc přiroste přes plný redraw (+9 na 62 flipů),
  při běžném provozu pomaleji (+2 na 151 flipů). `chyb` (TEIF/CEIF) zůstává 0 a
  `LTDC podteceni` také 0, takže nejde o chybu přenosu ani o hladovění sběrnice —
  přenos prostě **trvá déle než mez**.
  ⚠️ Počítání je ověřené proti off-by-one: `while ((DMA2D->CR & DMA2D_CR_START) &&
  ++guard < D2D_WAIT_GUARD)` — při čistém dokončení se `++guard` díky zkrácenému
  vyhodnocení ani neprovede, takže normální průběh se nezapočítá.
- **Dopad:** Přepis konfiguračních registrů DMA2D za běhu přenosu není definovaná
  operace — výsledkem je poškozený nebo neúplný obdélník. Protože se do téhož
  bufferu dál copy-forwarduje, může se poškození držet přes snímky.
  ⚠️ **Netvrdím, že to způsobuje kterýkoli pozorovaný jev.** Displej podle
  uživatele funguje, `LTDC podteceni` je 0 a `chyb` 0. Je to defekt doložený
  měřením vlastního počítadla, ne diagnóza problikávání.
  ⚠️ Vada je v kódu **od zavedení hlídací meze**, není to regrese z tohoto auditu —
  oprava F-0033 ji jen zviditelnila. To je přesně to, kvůli čemu se počítadlo přidávalo.
- **Reprodukce:** `status` → řádek `DMA2D:`; pak `ui` (vynucený plný redraw) a `status`
  znovu. Počítadlo `timeout` naroste.
- **Návrh opravy:** Rozlišit dvě věci, které dnes měří jedna konstanta:
  1. **Ochrana proti zatuhnutí** má být řádově nad nejdelším legitimním přenosem,
     ne na jeho úrovni. Nejčistší je mez v **milisekundách** (`HAL_GetTick()`, např.
     100 ms), jak to projekt dělá jinde (`sd_export.c`, `w25q.c`). ⚠️ `d2d_wait` je
     ale v horké cestě (2× na každý fill/blit), takže tick se nesmí číst v každé
     iteraci — vzor: hrubý spin počet a teprve po jeho vyčerpání kontrola času.
  2. Když mez přesto vyprší, **nepřeprogramovávat periferii naslepo** — buď přenos
     zrušit (`DMA2D->CR &= ~DMA2D_CR_START` + počkat), nebo operaci zahodit.
  ⚠️ Prosté zvýšení konstanty je nejlevnější a odstraní falešné poplachy, ale
  ponechá druhou půlku vady (co dělat při skutečném zatuhnutí).
- **Riziko opravy:** střední. Mění se chování v nejteplejší kreslicí cestě; špatně
  zvolená mez buď vrátí falešné poplachy, nebo prodlouží zatuhnutí UiTasku a tím
  ohrozí heartbeat watchdogu (2,5 s).
- **Vztah k lekcím:** `L-0004` (čekací smyčka) — mez existovala, ale byla zvolená
  bez vztahu k nejdelšímu legitimnímu přenosu; a nová lekce: **„hlídací mez, která
  se dá splést s normálním provozem, není ochrana, ale generátor tichých chyb"**.
- **Stav:** opraveno 2026-09-10 (commit `d3a099e`), ⬜ neověřeno na HW po power-cyklu.
  Opraveno **variantou 2** (mez v ms + zrušení přenosu + čítač nejdelšího čekání), ne
  pouhým zvýšením konstanty. 🔑 **A měření hned vyvrátilo můj vlastní odhad:** čítač
  `g_d2d_wait_max_cyc` ukázal, že nejdelší legitimní čekání je **~61 ms**, ne ~12 ms,
  jak jsem v nálezu odhadoval — byl jsem 5× vedle. První verze opravy měla proto mez
  100 ms (rezerva jen 1,6×) a byla by **horší než původní stav**, protože nově se při
  vypršení přenos ruší. Po změření zvýšeno na **500 ms** = ~8× nad naměřeným maximem
  a zároveň 5× pod 2,5 s, které má na heartbeat UiTask.
  Na desce po opravě: `chyb 0, timeout 0 | flip timeout 0 | max cekani 56.305 ms
  (mez 500)`, po třech vynucených plných redrawech 57.110 ms; před opravou `timeout 14`
  a rostoucí.

---

## Dodatek (2026-09-21) — nález mimo původní audit

Uživatel nahlásil problikávání hlavní obrazovky při RUN/STOP, při přechodu do
menu a zpět a při přepínání FREQ/PERIODA. Než jsem nález zapsal formálně, proběhlo
v konverzaci pět pokusů o opravu (buffer-„settling" teorie nad `screen_main_redraw_freq()`)
— všechny odloženy do `git stash` **beze změny kódu ve stromu**, protože žádný z nich
vadu neodstranil a teorie, na které stály, byla po přečtení `prim_stm32_present()`
vyvrácena (historie `prev`/`cur` dirty rectů se posouvá jen při skutečném flipu, ne
podle času — viz „Co bylo zkontrolováno" výše, `:328-332`). Uživatel pak sám
požádal o bisect (`git checkout 9edb7dc -- .`, build 2026-09-06): vada je přítomna
i tam, tedy **není regrese posledních ~15 dnů vývoje**. Následuje řádný nález podle
tohoto formátu, ne další pokus o opravu.

> 🔑 **ROZŘEŠENO 2026-09-22 — příčina byla jinde, než celý nález níže hádal.**
> Není to velikost překreslení ani propustnost sběrnice: **všechna podtečení
> vznikala uvnitř `copy_forward_dedup()`, protože kopíroval ÚZKÉ obdélníky
> STRIDED přístupem** (`FGOR`/`OOR` = `800 − w`), což v SDRAM přepíná řádky a
> bere LTDC propustnost, na které stojí plnění FIFO. Plný render (`ui`) byl
> přitom vždy čistý, protože kopíruje **lineárně** jedním přenosem — a to i
> když přenáší 768 kB, tedy podstatně VÍC dat. Oprava = kopírovat
> **plnošířkové pásy slité po ose Y** (`FGOR`=`OOR`=0). Naměřeno: **38 → 0**
> podtečení na 20 stisků RUN/STOP. Text nálezu níže je ponechán tak, jak
> vznikal, včetně dvou mylných stop (velikost burstu, fáze vůči vblanku) —
> záznam o tom, kudy cesta nevedla, viz L-0076.

### F-0140 [S2] `copy_forward_dedup()` kopíruje dirty obdélníky STRIDED přístupem a bere LTDC propustnost → poškozený snímek při každém RUN/STOP

- **Místo:** `CM7/app/screens/screen_main.c:2738-2773` (`screen_main_redraw_freq_area`),
  volající z `CM7/app/app_gpsdo.c:8642` (fyzický dotek RUN/STOP → `present_now()` hned),
  `CM7/app/app_gpsdo.c:7830-7836` (vzdálený SCPI `INIT`/`ABOR` → `s_dirty=1`, flip
  odložen do `app_gpsdo_flush()` — viz „present coalescing", `app_gpsdo.c:114-119`).
- **Popis:** `screen_main_redraw_freq_area()` je jediné místo, které při RUN/STOP
  (a při změně formátu FREQ/PERIODA nebo magnitudy) vykreslí zónu velkého čísla
  celou najednou: jeden neprůhledný `blit_bg_region(freq_clear_area())`
  (`freq_clear_area()` vrací až `FREQ_MAX_W+20` × 88 px = **800×88 px** ořezaných na
  šířku panelu, `screen_main.c:1106-1111`, v kódu komentované jako „~68 kB" jeden
  přenos) následovaný `ui_big_number_render(&s_num)` s `prim_set_glyph_accel(1)` —
  tedy až `NUM_SEG_MAX`=12 dalších DMA2D přenosů (HW glyph blend) v tomtéž tiku
  (`screen_main.c:2761-2765`). To je jednoznačně nejtěžší jednorázová DMA2D zátěž
  v celém redraw řetězci hlavní obrazovky mimo plný render.
  Naměřeno dnes (SCPI `INIT`/`ABOR` opakovaně, čteno přes `status` →
  `LTDC: podtečení FIFO N / M flipů`, `freertos_task_uart.c:2584-2591`): burst
  toggle dal **52 podtečení na 1000 flipů** na aktuálním HEAD i na bisectnutém
  buildu z 2026-09-06. Zvýšení `d2ddt` na maximum (255, UART `d2ddt 255`) snížilo
  poměr na **15/1000** — pokles, ne nula. Vypnutí glyph accelu jen pro tuhle
  funkci (dočasná úprava, viz níže) snížilo na **43/1000** — taky pokles, ne nula.
  Jednotlivé izolované přepnutí (jeden `INIT` nebo jeden `ABOR`, ne burst) dalo
  0–1 podtečení na test.
- **Důkaz:** čísla výše jsou z dnešního měření v této konverzaci (UART `status` po
  sérii SCPI příkazů), ne odvozená ze zdrojáku — ale **nejsou zapsaná do souboru
  s daty ani do skriptu**, takže je momentálně nelze znovu vytáhnout jinak než
  opakováním postupu níže. To je slabina tohoto nálezu a je uvedená v
  „Nezkontrolováno". Mechanismus podtečení (LTDC čte scanline v reálném čase,
  DMA2D soutěží o tutéž SDRAM sběrnici) je zdokumentovaný projektově (`CLAUDE.md`
  „LTDC podtečení FIFO... příčina: `copy-forward` běží na DMA2D SOUBĚŽNĚ se
  skenováním panelu z téže SDRAM") a `g_ltdc_underrun` čte skutečný `LTDC->ISR`
  bit `FUIF` (`prim_stm32_hal.c:397-399`), ne odhad.
  🔴 **Existující `D2D_DEADTIME_DEFAULT=240` (`prim_stm32_hal.c:43-56`) byl
  změřen `tools/ltdc_knee.ps1` PROTI JINÉMU vzoru zátěže** — komentář u konstanty
  říká výslovně „vynucené plné překreslení přes `ui`" (celoobrazovkový blit,
  768 000 B). Tenhle nález ukazuje, že hodnota, která u plného redrawu dává **0**
  podtečení už od `d2ddt=212`, **NEDÁVÁ nulu** u `screen_main_redraw_freq_area()`
  ani na stropu 255 — ačkoli ten přenáší méně bajtů (~140 KB vs. 768 KB). To je
  paradox, který tenhle nález **nevysvětluje** (viz Nezkontrolováno) — pravděpodobný
  rozdíl je v tom, že jde o **~13 diskrétních DMA2D transakcí v jednom tiku**
  (1 blit + až 12 glyfů) místo jednoho souvislého přenosu, ale to je `HYPOTÉZA`,
  ne změřený fakt.
- **Dopad:** Podtečení FIFO LTDC znamená podle vlastní dokumentace projektu
  **poškozený snímek na panelu** — přesně ten vizuální jev, který uživatel
  popisuje jako „problikne". Korelace (stejná operace, stejný měřitelný
  vedlejší efekt, mizí se sníženou zátěží DMA2D) je silná, ale **není to důkaz
  jediné příčiny**: nebyl proveden přímý test „podtečení nastalo PRÁVĚ v tom
  snímku, který uživatel označil jako problikující" (na to by bylo potřeba
  časové razítko podtečení vs. okamžik doteku, což `g_ltdc_underrun` dnes nenese).
  ⚠️ Nejde o ztrátu dat ani o nefunkčnost měření — jde o vizuální artefakt, který
  se dle popisu uživatele objevuje opakovaně, ne trvale (odtud S2 „nestabilita",
  ne S1).
- **Reprodukce:** `status` → přečti `LTDC: podtečení FIFO` (nebo napřed `d2ddt 0`
  reset čítače), přepni RUN/STOP (dotykem nebo `scpi INIT`/`scpi ABOR`) N-krát,
  `status` znovu → poměr naroste. `HYPOTÉZA — ověřit`: totéž se stejnou metodikou
  jako `tools/ltdc_knee.ps1`, ale vynucující konkrétně `screen_main_redraw_freq_area()`
  (ne `ui`), aby šel dohledat skutečný zlom pro tenhle vzor zátěže — dnešní `255`
  je jen horní mez rozsahu, ne nalezený zlom.
- **Nevyřešená otázka (HYPOTÉZA, neměřeno):** uživatel hlásí problikávání při
  **jednotlivém** fyzickém doteku jako „porad" (vždy), zatímco jednotlivé SCPI
  přepnutí dalo 0–1 podtečení na test. Cesty se liší architektonicky:
  fyzický dotek volá `present_now()` **synchronně hned** (`app_gpsdo.c:8645`),
  vzdálený SCPI příkaz jen nastaví `s_dirty=1` a flip odloží do
  `app_gpsdo_flush()` na ~30Hz bráně („present coalescing", `app_gpsdo.c:114-119`) —
  což může bez dalšího měření znamenat jak víc, tak míň kumulované DMA2D zátěže
  před flipem, podle toho, co se do stejné brány stihne přimíchat. Nebylo změřeno,
  jestli fyzický dotek dává vyšší poměr podtečení než SCPI — to je klíčová chybějící
  data pro rozhodnutí, jestli je tenhle nález celou příčinou, nebo jen její částí.
- **Návrh opravy (proveden, viz Stav):** Zúžit DMA2D přenos pro RUN/STOP toggle
  na SKUTEČNOU aktuální zónu čísla (`freq_area()`) místo pevné „maximální možné"
  (`freq_clear_area()`) — bezpečné JEN pro tenhle konkrétní volající, protože
  RUN/STOP nemění formát/magnitudu čísla (na rozdíl od FREQ/PERIODA a change-of-
  -magnitude případů, kde `freq_clear_area()` zůstává nutná kvůli „duchům" po
  stranách, viz `screen_main.c:2741-2760` — past se tedy neotevřela, protože
  se nová úzká varianta nikdy nevolá tam, kde geometrie hrozí měnit). Kombinováno
  s vypnutím `prim_set_glyph_accel` jen pro tenhle redraw (CPU rasterizace
  rozprostírá bus provoz do víc menších transakcí místo jednoho DMA2D burstu).
- **Riziko opravy:** nízké. Nová funkce `screen_main_redraw_freq_tint()` je čistě
  aditivní (žádná změna chování `screen_main_redraw_freq_area()`, která zůstává
  pro format-change případ beze změny), volá se jen ze dvou míst, kde geometrie
  prokazatelně nemůže spadnout mimo `freq_area()`.
- **Vztah k lekcím:** `L-0004`/STATUS #200 (mrtvý čas DMA2D jako obrana proti
  podtečení) — tenhle nález ukazuje, že hodnota obhájená pro jeden vzor zátěže
  (`ui`) se nesmí bez opětovného měření považovat za platnou pro jiný vzor.
  **Nová L-0076**: sdílený fyzický prostředek (SDRAM sběrnice) má strop, který
  žádná kombinace SW pák nepřekročí na nulu — umělý burst test je nástroj na
  odhalení jevu, ne automaticky důkaz, že vysvětluje konkrétní hlášení uživatele.
- **Stav:** ✅ **OPRAVENO 2026-09-22 — příčina nalezena měřením, ne úvahou.**

  **Skutečná příčina:** `copy_forward_dedup()` kopíroval jednotlivé dirty
  obdélníky, tedy `d2d_blit_ex` se šířkou < 800 px → `FGOR`/`OOR` = `800 − w`
  = **strided přístup do SDRAM**. Každý řádek kopie začíná v jiné SDRAM řadě,
  takže se řady neustále přepínají; LTDC, které čte snímek na panel sekvenčně,
  o tu propustnost přijde, FIFO podteče a na panel jde **poškozený snímek**.
  Proto to bylo deterministické (každý stisk) a proto na to `d2ddt` ani
  zúžení překreslení nestačilo — obojí mění objem dat, ne vzor přístupu.

  **Oprava:** copy-forward kopíruje **plnošířkové pásy slité po ose Y**
  (`prim_stm32_hal.c`, `copy_forward_dedup`). Pás na plnou šířku má
  `FGOR`=`OOR`=0, tedy **lineární** přístup — přesně jako plná kopie, která
  byla vždy čistá. Kopíruje se tím víc bajtů, ale mnohem levnějším vzorem.
  ⚠️ Kopírovat víc než dirty je bezpečné: `front` je nejnovější hotový snímek
  a `back` je o dva snímky starší **všude**, takže pixel navíc může `back` jen
  přiblížit k `front`. Slévá se **jen po ose Y**, takže nehrozí past, před
  kterou varoval původní zákaz „žádný bbox-merge" (ten spojoval i přes Y, a
  kvůli dvěma malým obdélníkům na opačných koncích by kopíroval skoro celý
  snímek).

  **Naměřeno na desce** (injektor `tap 1`, viz níže; `d2ddt` na výchozích 240):

  | test | před opravou | po opravě |
  |---|---|---|
  | 20× RUN/STOP | **38** podtečení (93/1000 flipů) | **0** |
  | 40× RUN/STOP + 30× plný render | — | **0 / 902 flipů** |
  | STOP + 4 s klidu, `fbdiff` | FB0 mimo o 12 538 px | **shoda** |
  | CPU UiTask / celkem | — | 25 % / 33 % |

  🔑 **Rozhodlo to měření, které do té doby neexistovalo** — rozklad podtečení
  na fáze `present()`: `kresleni 0 | cekani 0 | flip 0 | COPY-FORWARD 38`.
  Do té chvíle se podtečení četlo jen jednou za flip, takže nešlo odlišit
  „aplikace kreslí moc" od „copy-forward bere sběrnici" — tedy dvě úplně jiné
  opravy. Druhý klíčový údaj byl paradox, který každou předchozí teorii
  vyvracel: **plný render (`ui`) kopíruje 768 kB a má podtečení NULA**, zatímco
  mnohem menší RUN/STOP překreslení podtékalo vždy.

  **Ověřovací řetězec F5.2:** build 0 varování, `audit.py` 92 OK / 0 / 2
  (GCC 14.3), `.text` 609 456 → 610 952 B, symboly dohledány v `.elf`.
  ✅ **POTVRZENO UŽIVATELEM NA DISPLEJI 2026-09-22** — problikávání při RUN/STOP
  je pryč. ⚠️ Ověřeno po flashi + SW resetu, **ne po plném power-cyklu**
  (pravidlo 4b / L-0010): studený start je jiný stav, takže při nejbližším
  odpojení napájení se to hodí zkontrolovat znovu.

  **Trvale přidaná diagnostika** (zůstává, je to levnější než další špatná oprava):
  - `fbdiff` — porovná všechny tři framebuffery + rozklad podtečení po fázích
    + počítadla, kolikrát byl který buffer cílem copy-forwardu. Odliší
    **poškozený snímek při scan-outu** od **nesouladu bufferů**.
  - `tap <0-4>` — injektor doteku: provede stisk tlačítka patky přesně toutéž
    cestou jako prst (stejný task, stejné místo smyčky). Bez něj vyžadovalo
    každé měření uživatele u desky, protože vzdálené SCPI `INIT`/`ABOR` vadu
    nereprodukuje (flip nechává na ~30Hz koalescujícím gate).

  **Předchozí stav (2026-09-21, ponecháno jako záznam slepých uliček):**
  ⬜ neověřeno uživatelem na skutečném fyzickém doteku (jen přes SCPI/UART). `screen_main_redraw_freq_tint()`
  (`screen_main.c`, deklarace `screen_main.h`) nahradila `screen_main_redraw_freq_area()`
  na obou voláních RUN/STOP (`app_gpsdo.c:7834` SCPI, `app_gpsdo.c:8642` fyzický
  dotek); `screen_main_redraw_freq_area()` beze změny pro format-change případ.
  Ověřovací řetězec F5.2: build 0 varování, `audit.py` 92 OK/0/2 (baseline),
  `.text` 609456→609520 B (+64 B), symbol `screen_main_redraw_freq_tint`
  dohledán v `.elf` (`nm`, adresa `0803ee58`, odlišná od `screen_main_redraw_freq_area`
  na `0803ed34` — nesplynuly inlinem). Naflashováno + SW reset, ověřeno `status`
  na běžící desce.
  **Naměřeno PO opravě** (tentýž burst postup jako výše, `d2ddt` resetuje čítače):
  | scénář | `d2ddt` | podtečení/1000 flipů | pro srovnání PŘED opravou |
  |---|---|---|---|
  | burst 20× RUN/STOP | 240 (výchozí) | **27** | 52 |
  | burst 20× RUN/STOP | 255 (strop registru) | **17** | 15 (d2ddt=255 samotné, bez zúžení) |
  | **1× izolovaný RUN nebo STOP** | 240 | **0** (0–2 z tisíců flipů) | stejné i PŘED opravou |
  🔑 **Oprava je reálná a bezriziková (burst ~poloviční), ale NENÍ „ideální" ve
  smyslu nuly pod umělým burstem** — kombinace obou pák (užší zóna + bez glyph
  akcelerace) u `d2ddt=255` dala prakticky totéž jako `d2ddt=255` samotné (17 vs.
  15/1000), což ukazuje na **fyzický strop sdílené SDRAM sběrnice**, ne na
  zbývající rezervu v SW pákách — viz L-0076.
  ⚠️ **Klíčové zjištění, které mění rámec celého nálezu:** jediné izolované
  přepnutí (skutečné použití, ne umělý burst 20× za sebou) bylo **čisté (0
  podtečení) PŘES SCPI cestu JIŽ PŘED touto opravou** — tedy pro reálné, jednotlivé
  zmáčknutí tlačítka LTDC podtečení neukazuje žádný problém, opravený ani
  neopravený kód. **Fyzický dotek jsem nemohl otestovat** (žádný UART hook, který
  by vyvolal `app_gpsdo_handle_touch()` přímo — jen skutečný prst na displeji).
  **Zbývá tedy ověřit uživatelem na desce**: je hlášené problikávání po
  naflashování téhle opravy pryč? Pokud ANO, oprava (spolu s tím, že šlo o
  burst-scénář, ne o single-tap) věc uzavírá. Pokud PŘETRVÁVÁ i po jediném
  klepnutí, LTDC podtečení podle dnešních dat **není** vysvětlením a hledání
  musí pokračovat jinam (fyzický dotek má jiné časování než SCPI — `present_now()`
  hned místo ~30Hz koalescence, viz `app_gpsdo.c:114-119` a `:8645` — což se
  bez skutečného doteku nedalo ověřit).

---

## Fáze oprav (F5) — 2026-09-10

Uživatel schválil **skupinu A**. Opraveny **F-0033**, **F-0034**, **F-0035**;
**F-0032** zůstává otevřený (čeká na rozhodnutí mezi rozšířením backendu a opravou
pravidla). Ověřovací řetězec §F5.2: build **0 varování**, `tools/audit.py`
**92 OK / 0 selhání / 2 s varováním**, `.text` **597 520 → 597 832 B**,
symboly `g_d2d_errors`/`g_d2d_timeouts`/`g_ltdc_flip_timeouts`/`prim_text_missing_glyphs`
a oba nové řetězce dohledány v `.elf`. Bariéra F-0035 ověřena **v disassembly**:
`str [r4,#68]` (NLR) → `dsb sy` → `ldr/orr #1/str [r4,#0]` (CR |= START).

🔑 **Oprava F-0033 okamžitě odhalila F-0036** (viz výše) — počítadlo, které mělo jen
přestat mlčet, našlo do 25 s běhu 14 vypršení hlídací meze. To je doklad, že nález
F-0033 nebyl kosmetický.

---

## Souvislosti s dřívějšími nálezy (nezakládám je znovu)

- **`prim_internal_blend_px` obchází `mark_dirty`** (`fb.c:114-120`) — dokumentováno
  v `CLAUDE.md`. Ověřeno, že to platí i pro **zaoblené rohy**: `prim_fill_rect_rounded`
  (`fill.c:106-135`) kreslí střed třemi fillami, ale **rohové čtverce leží mimo ně**
  a plní je `aa_corner` → `prim_internal_blend_px`, tedy bez označení. Funguje to jen
  díky předchozímu clearu. Je to táž třída jako F-0032 a **měly by se opravit společně**.
- **DMA2D glyph blend nedělá `mark_dirty`** (`prim_stm32_hal.c:150`) — vědomé,
  dokumentované, spoléhá na clear před textem. Táž třída.
- **Cache glyfů nemá eviction** (`:179-191`) — dokumentované; po naplnění
  (96 položek / 256 kB) glyfy padají na CPU, což je bezpečná degradace.
- **Mrtvý čas DMA2D 240** (`:45`) — hodnota **změřená na desce** (`tools/ltdc_knee.ps1`),
  se zlomem kolem 208 a ověřená oběma směry. Nezakládám nález; naopak je to vzor,
  jak se má konstanta v tomhle projektu obhajovat.

## Co bylo zkontrolováno a je v pořádku

**Triple buffering a bezpečnost flipu** (`prim_stm32_present`, `:291-329`): flip se
zadává přes `LTDC->SRCR = LTDC_SRCR_VBR` (při vblanku, netrhá), čeká se na **předchozí**
flip, ne na aktuální. Po flipu se copy-forwarduje z nového `front` do nového `back` —
ověřeno rozborem indexů, že **ani jeden z nich není buffer právě scanovaný LTDC**
(to je přesně to, co třetí buffer kupuje). ✓

**Dedup copy-forwardu** (`copy_forward_dedup`, `:260-281`): tvrzení v komentáři, že
kopírovaná množina je **přesně** sjednocení vstupů, jsem ověřil — `keep` drží jen
původní obdélníky (nikdy zvětšené), rect se přidá jen když ho žádný držený nekryje.
Žádný bbox-merge. ✓ Ověřena i **mez pole**: `keep[2 * MAX_DIRTY]` = 96 a maximální
počet vložení je `nd_prev + nd_cur` ≤ 48 + 48 = 96 → sedí přesně, bez přetečení. ✓

**Ořez geometrie před dirty rectem:** `prim_fill_rect` (`fill.c:34`) i `prim_blit`
(`fill.c:181`) volají `prim_internal_clip_rect` **před** předáním do backendu, takže
`mark_dirty` nikdy nedostane obdélník přesahující framebuffer. `prim_internal_clip_rect`
(`fb.c:72-90`) navíc ořezává i proti rozměrům cíle, ne jen proti clipu. ✓

**Cache maintenance je na správné straně:** `d2d_inval` (`:95-100`) invaliduje **po**
zápisu DMA2D, ne před — a jen na WT oblasti, kde není co ztratit (žádné dirty řádky).
Copy-forward invalidaci **záměrně vynechává** (`do_inval = 0`, `:238-242`) s doloženým
zdůvodněním, že ty pixely CPU ve starém stavu nikdy nečte. ✓ To je opak chyby, kterou
projekt udělal u SD (`sd_diskio`) — tam se invalidovalo u CPU/FIFO cesty a data se
zahazovala.

**Glyph atlas v `.sdram` = Device paměť:** plní se **bajtovými** zápisy
(`glyph_tile`, `:187`), takže nehrozí UsageFault z nezarovnaného přístupu, kterým
je Device paměť na Cortex-M7 nebezpečná (audit F-0012). ✓

**HW glyph cesta se použije jen na neořezaný glyf** (`text.c:134-137`): kontroluje se
`cr.x == gx0 && cr.y == gy0 && cr.w == g->w && cr.h == g->h`, tedy že se obdélník
ořezem vůbec nezměnil. ✓ A jen pro neprůhlednou barvu (`:118`), protože HW cesta
moduluje jen coverage.

**Dekodér UTF-8** (`text.c:21-48`): pokračovací bajty se testují proti `'\0'`, takže
useknutá sekvence na konci řetězce nepřečte za terminátor; neplatný lead byte posune
o 1 bajt a vrátí U+FFFD. `prim_text_width` a `prim_draw_text` sdílejí **tentýž**
dekodér, takže se nemohou rozejít. ✓

**Palety** (`theme.c:145-174`): `build_variants` je chráněná `s_variants_built`,
`ui_theme_select` má `default:` pro index mimo rozsah. `g_ui_theme` čte i zapisuje
výhradně UiTask. ✓

**Ochrana proti zatuhnutí:** obě čekací smyčky (`d2d_wait`, čekání na flip) **mají**
horní mez, takže UiTask nemůže viset navždy — chybí jen reakce na vypršení (F-0033). ✓

## Nezkontrolováno / omezení tohoto běhu

- **Nečetl jsem celé libprim** — `glow.c` (157), `gradient.c` (91), `path.c` (96),
  `shapes.c` (176) a `libui` komponenty mimo `bargraph.c`/`theme.c`. Rozhodl jsem se
  pro hloubku na cestě dat (fill → dirty → copy-forward → flip) místo šířky; zbytek
  jsou konzumenti týchž primitiv a spadají pod F-0032 stejným způsobem.
- **Errata DMA2D/LTDC pro rev V** nedohledána (sekce H); revize silikonu je v projektu
  stejně jen `HYPOTÉZA`.
- **Nic z toho neběželo na HW jako důsledek tohoto auditu** — kód nebyl měněn
  (`git status` na `*.c`/`*.h` prázdný).
- **Dodatek 2026-09-21 (F-0140):** čísla podtečení jsou z ruční relace v
  konverzaci, ne z uloženého skriptu/logu — nejde je bez opakování postupu
  znovu vytáhnout. Chybí měření na fyzickém doteku (jen SCPI). Nebylo
  prošetřeno, jestli stejná příčina vysvětluje i hlášené problikávání při
  MENU navigaci a FREQ/PERIODA toggle (jiné volající, dnes neměřeno).
- **Neposuzována vizuální stránka** (rozměry, čitelnost, layout) — to je `UI_SIZES.md`
  a zadání UI, ne vykreslovací řetězec.
