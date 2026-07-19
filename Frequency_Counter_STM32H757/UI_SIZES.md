# UI_SIZES — přehled velikostí prvků UI (ideál vs. realita)

**Účel: po KAŽDÉ změně layoutu aktualizovat.** Tenhle soubor je jediné místo, kde se
velikosti prvků hodnotí ve fyzických jednotkách — pixely na 4,3" panelu klamou.

## Fyzika panelu

| Veličina | Hodnota |
|---|---|
| Panel | Waveshare **4,3"** IPS, 800×480 px |
| Hustota | **8,54 px/mm** (217 DPI) → **1 mm = 8,54 px** |
| Aktivní plocha | ≈ **93,7 × 56,2 mm** |
| Dotykový cíl — minimum | **7 mm = 60 px** (pohodlně 9 mm = 77 px) |
| Cap height textu | ≈ 0,72 × `ascent` fontu |
| Čitelnost | cap ≥ 1,9 mm (16′ na 40 cm) pohodlná; 1,35 mm čitelné z ~30–35 cm |

## Dotykové cíle (stav 2026-07-19)

| Prvek | px (š×v) | mm (výška) | Ideál | Stav |
|---|---|---|---|---|
| Pilulky GNSS/SYS — vizuál | výška **46** | **5,4** | 7 mm | ⚠ zvětšeno z 30→36→42→46; strop = header 56 px (4 px rezerva ke spodní lince, dál už nejde) |
| Pilulky GNSS/SYS — hit-slop (`pt_in_pill`) | výška ~52 | 6,1 | 7 mm | ⚠ těsně pod, přes celý header (clampnuto, nezávislé na vizuální výšce) |
| Footer hlavní obrazovky (PERIOD/RUN/GATE/CHAN/MENU) | 133–200 × 61 | 7,1 | 7 mm | ✓ |
| Okenní footer (ZPĚT 133×61, DIAGRAM 160, ALLAN/HISTOGRAM 180–220…) | ×61 | 7,1 | 7 mm | ✓ |
| Menu dlaždice | 248×76 | 8,9 | 7 mm | ✓ (bylo 248×88/10,3 mm; zmenšeno 2026-07-19 kvůli 4. řádku, pořád nad minimem) |
| Restart (footer menu) | 170×61 | 7,1 | 7 mm | ✓ |
| Potvrzení restartu ANO/NE | 150×64 | 7,5 | 7 mm | ✓ (ACTIVE = NE, bezpečná volba) |
| Allan karta (tap → okno ALLAN) | 364×242 | 28,3 | 7 mm | ✓✓ (přes celou výšku mřížky) |
| Trend mini (tap → fullscreen trend) | 398×100 | 11,7 | 7 mm | ✓✓ |
| GNSS karta Družice (tap bar↔sky) | 502×318 | 37 | 7 mm | ✓✓ |
| Ovladače Nastavení / Čas (mute, jas ±, dim ±, TZ ±…) | ×64 | **7,5** | 7 mm | ✓ (bylo 56/6,6 mm) |
| Kalibrace `−`/`+` — vizuál | 60×60 | **7,0** | 7 mm | ✓ (bylo 50×34/4,0 mm; přeskládáno — viz log) |
| Trend okno `−`/`+` | 90×61 | 7,1 | 7 mm | ✓ |
| Selftest SPUSTIT / Kalibrace ULOŽIT | 180–220×61 | 7,1 | 7 mm | ✓ |

## Typografie (kde se co používá)

| Font | cap px | cap mm | Použití | Stav |
|---|---|---|---|---|
| mono_75 | ~55 | 6,5 | headline kmitočet | ✓ |
| mono_52 | ~38 | 4,5 | fade číslice headline | ✓ |
| mono_25 | ~19 | 2,2 | čas v headeru, nadpisy oken | ✓ |
| mono_22 | ~16 | 1,9 | popisky tlačítek | ✓ |
| mono_18 / sans_18 | ~13 | 1,5 | **TODO #11(2b) HOTOVO 2026-07-19** — bump z mono_16/sans_16 na ~48 místech napříč Diagnostikou/GPS/Health/Senzory/Pamětí/O přístroji/boot splash/Referencí/Kalibrací/Čítačem/komunikačním diagramem (uzly + OCXO/RF popisky) — každé ověřeno tabulkou fontů (šířka `advance` I skutečná výška `oy` glyfu, ne nominální ascent fontu, viz log níže) | ✓ |
| mono_16 / sans_16 | ~11,5 | **1,35** | tělo hustých oken, osy velkého Allan grafu, **labely i hodnoty pilulek** (od 2026-07-19 sjednoceno — bylo label mono_14). **6 míst vědomě NEbumpnuto** (viz log) — obsah tam přetéká i při současném fontu | ⚠ čitelné z ~30–35 cm u nebumpnutých míst |
| mono_14 | ~10 | **1,2** | osy grafů/náhledů, SPI status řádek (úmyslně — 38 znaků), popisky diagramu | ⚠ jen pomocné texty |

**Pravidlo:** hodnoty, které uživatel ČTE, nesmí být pod mono_16; pod mono_14 nesmí být nic.

## Změnový log

- **2026-07-19 (6. vlna — kompletní revize)**: křížová kontrola celého necommitnutého balíku (žádné z toho neběželo na HW).
  - **Nález 1 (chyba, opravena)**: rozpočtem řady pilulek NENÍ levý okraj textu hodin (x=674, proti kterému jsem dosud ověřoval), ale **levý okraj clear zón sekundového redrawu času/data** (x=648 resp. 644, `screen_main_redraw_time`) — a datová zóna (pás y 35–53) s pilulkami výšky 46 nově koliduje i svisle. Řada v nejhorším stavu končila na 667 → tik hodin by umazával ocas HOLD pilulky. Oprava: **`HDR_PILL_LIMIT` (640) + fit-check `hdr_pill_fit`** v `render_header` (komentář „only the pills that fit" to sliboval odjakživa, ale kód to nikdy nedělal) + mezery pilulek zpět na kompaktní **4/5** (prostornější 5/6 z 5. vlny by nechalo i TYPICKOU řadu končit na 647 — vracím vlastní chybný závěr 5. vlny; uvolněné okraje tedy reálně financují větší font labelů, ne mezery).
  - **Nález 2 (priorita pilulek)**: simulace všech stavů přes tabulky fontů ukázala, že v holdover stavech („ACQUIRE"/„NO GNSS" + „SYS ERR") by fit-check vyřadil právě **HOLD** — indikátor holdoveru. **HOLD přesunut před CAL** (CAL = statický placeholder „4 min"), takže při přetlaku vypadne postradatelný CAL. Ověřeno simulací: typicky 6/6 pilulek (řada končí 636 ≤ 640), v červených dlouhých stavech 5/6 bez CAL.
  - **Nález 3 (pre-existující chyba, opravena)**: `fmt_fixed`/`fmt_temp`/`fmt_minmax` ztrácely znaménko u hodnot −0,99..−0,01 (celá část 0 → `%ld` mínus nevytiskne; „−0,5 °C" → „0.50 C"). Chyba existovala ve všech 4 původních `fmt_*` kopiích už před sjednocením. Opraveno explicitní `-` předponou.
  - **CubeMX regen (mezitím proběhl)**: ověřeno, že všechny Core diffy jsou jen konce řádků (0 obsahových změn), všechny USER CODE bloky přežily (stack-overflow config, VREFBUF, PB12 SET, rtc app vrstva, UartTask 4 KB z .ioc — regen-safe návrh fungoval); jediná ztráta = komentář na generovaném řádku `freertos.c` (očekávané, hodnota drží z .ioc). **`Backup/` složky CubeMX přidány do `.gitignore`** (70 `.bak` souborů, plně redundantní s gitem).
  - **Úklid**: mrtvá `SCR_MAIN_SMALL_CARD_H` odstraněna; zastaralý komentář u Menu (staré souřadnice sloupců) opraven; CLAUDE.md hlavní-obrazovka odstavec aktualizován (PILL_H 46, fit-check, A/B default-stará).
  - **Kompilační sweep VŠECH zdrojáků** app + libui (vč. 13 fontů) + libprim: 0 chyb, 0 varování mimo známou `dchg` množinu (parita s baseline potvrzena diffem množin).
- **2026-07-19 (5. vlna — okraje obrazovky)**: systematická kontrola obvodu 800×480 na nevyužité pixely. ⚠️ Pozn.: závěr o návratu mezer pilulek na 5/6 **revidován 6. vlnou** (viz výše) — platí 4/5 + fit-check.
  - **Header**: levý okraj řady pilulek 5→**2 px** (`SCR_MAIN_HEADER_X`), pravý okraj hodin 12→**6 px** (nová konstanta `SCR_MAIN_CLOCK_MARGIN`, dřív odvozeno z `UI_DIM_PADDING_X/2` — nesdílet dál se sdíleným paddingem, ten se používá i pro nesouvisející title řádek). Uvolněných 9 px se vrátilo do `UI_DIM_PILL_GAP` 4→**5** a `UI_DIM_PILL_INNER_GAP` 5→**6** (zpět na původní prostornější hodnoty, které musely ustoupit při zvětšení fontu labelu minulou vlnu) — ověřeno tabulkou fontů: řada pilulek v nejhorším reálném stavu ("GNSS FIX"+"SYS ERR") končí na x=667, hodiny začínají na x=674 → **7 px rezerva** (bylo 8, ale mezery jsou teď prostornější).
  - **Menu grid**: sloupce měly nesymetrický okraj — 24 px vlevo, jen **4 px vpravo** (548+248=796 z 800) — čistý nevyužitý pruh. Přepočteno na symetrických **14/14 px** (x=14/276/538); šířka dlaždic beze změny (56 volných px bylo už "spravedlivě" rozdělených na 3 sloupce + 2 mezery, ne že by zbývalo navíc na větší dlaždice).
  - **Zkontrolováno a ponecháno beze změny** (už symetrické/bez odpadu): okna (`DG_MX=18` oba okraje), footer hlavní obrazovky i oken (`pad=12` oba okraje).
- **2026-07-19 (4. vlna — #11(2b) + #12 dokončení + Menu 3×4)**:
  - **Menu rozšířeno 3×3→3×4** (9→12 dlaždic): dlaždice zmenšeny 88→**76 px** (10,3→8,9 mm, pořád nad 7 mm), rozteč řádků 100→86 (h76+gap10), aby se 4. řádek vešel před footer (68/154/240/326, konec 402, 15 px rezerva). **Řádek 4 = 3× "Placeholder N"** (`ACT_PLACEHOLDER`, no-op; tap na ně záměrně nedělá `nav_push`, protože nikam nenaviguje).
  - **Pilulky zvětšeny podruhé**: `UI_DIM_PILL_H` 42→**46 px** (4,9→5,4 mm) — 4 px rezerva ke spodní lince headeru (56 px), dál už nejde bez zvětšení headeru. Šířku neovlivňuje (na výšce nezávisí).
  - **Karta Allan** (na hlavní obrazovce): font 14→**16** (sjednoceno s fullscreen oknem ALLAN, které už mělo 16); levá rezerva na Y popisky přepočtena 36→**58 px** (šířka "10⁻¹⁰" při mono_16 = 47 px + 6 px mezera k ose, ověřeno tabulkou fontů).
  - **#12 (A4) dokončeno**: nalezen skutečný duplicitní pár, který minulá revize přehlédla — Čítač i Čas měly identickou geometrii karty `{18,62,764,340}` → pojmenováno `DG_CARD_FULL_C`. Zbylé 3 výjimky (O přístroji, Kalibrace, Komunikace) mají opravdu unikátní obsah, nesjednocovat.
  - **#11(2b) — globální bump mono_16→18/sans_16→18**: ~48 míst v `app_gpsdo.c` + `screen_main.c` (Diagnostika, GPS, Health, Senzory, Paměť, O přístroji, boot splash, Reference, Kalibrace, `kv_row` helper, Čítač, histogram/trend overlaye, komunikační diagram — uzly `cd_node` i OCXO/RF popisky `cd_label_x`). Každé místo ověřeno tabulkou fontů: **šířka** (`advance` součet vs. box) i **skutečná výška glyfu** (`oy` konkrétního znaku — NE nominální `ascent` fontu, který je worst-case pro celou znakovou sadu včetně diakritiky a zbytečně by zamítl bezpečné případy). Komunikační diagram měl box výšky přesně 22–24 px; přepočet přes reálné `oy` (14 u velkých písmen/číslic na mono_18/sans_18, ne nominálních 16/18) ukázal 2–6 px rezervu → bezpečně bumpnuto (OCXO popisek navíc rozšířen 116→124 px, měl jen 1 px vodorovné rezervy).
  - **6 míst vědomě NEbumpnuto** (komentář `TODO #11(2b)` u každého): GPS "Vet:/Fix:" čítače a Diagnostiky `g_freq_info` (SPI info řádek) — oba přetékají svůj box i při současném mono_16/sans_16 při extrémních hodnotách, bump by to jen zhoršil; GPS HDOP/PDOP řádek (mono_18 by přetekl i při jednociferných hodnotách — box bez clipu, přetečení by bylo vizuálně viditelné, ne tiché); Health "Reset:" řádek (dokumentovaný 34znakový worst-case sedí na mono_16 přesně, na mono_18 by přetekl); main-old větev `draw_stat_card` (zamrzlá referenční obrazovka, viz níže — na ní se záměrně nedělají žádné další úpravy).
  - **Main old/new**: `s_layout_old` default přepnut na `true` — **stará (pre-4,3") obrazovka je teď výchozí/zamrzlá referenční verze**, nové úpravy hlavní obrazovky cílí na `render_body_grid_v2`/`_v2` funkce (přepnutí na NEW = tlačítko "Main SW").
- **2026-07-19 (oprava + pilulky)**:
  - **Bug fix okno Čas**: tlačítko AUTO CET/CEST po přepnutí varianty (NORMAL↔ACTIVE) nechávalo v rozích "duchy" (čtverečky ze starší barvy) — `prim_fill_rect_rounded` kreslí zaoblené rohy přes `aa_corner`→`prim_internal_blend_px`, který zapisuje přímo do framebufferu a **obchází `mark_dirty`** (ten volá jen DMA2D `d2d_fill`/`d2d_blit_ex`). Rohy se tedy nikdy neoznačí jako "dirty" a při triple bufferingu (copy-forward jen označených oblastí) se nezkopírují dopředu — projeví se to JEN u tlačítek, která mění variantu/barvu při partial redrawu bez předchozího `blit_bg_region`/clear (jediné takové v appce = `cas_upd_mode`; footer tlačítka mají `blit_bg_region` už zabudované, ostatní partial-redraw tlačítka drží stále stejnou variantu). Oprava: `prim_fill_rect(TZ_AUTO_RECT, BG_CARD, REPLACE)` před `ui_button_render` — REPLACE jde DMA2D cestou, která `mark_dirty` volá, takže následný AA blend rohů spadá do už označené oblasti.
  - **Pilulky v headeru**: label font mono_14→**mono_16** (sjednoceno s hodnotou — labely "HDOP"/"CAL"/"HOLD" byly nejmenší text v celém UI). Aby se 6 pilulek pořád vešlo před hodiny i v nejhorším reálném stavu ("GNSS FIX" + "SYS ERR" současně), `UI_DIM_PILL_GAP` 5→4 a `UI_DIM_PILL_INNER_GAP` 6→5 (ověřeno součtem šířek z tabulek fontů: řada končí na x=660, hodiny začínají na x=668 → 8 px rezerva).
- **2026-07-19 (dočasné, k odstranění)** — pro vizuální A/B srovnání starého (pre-4,3") a nového layoutu hlavní mřížky na HW: footer tlačítko slotu 0 (normálně PERIOD/FREQ) je dočasně **"Main SW"** a přepíná `render_body_grid_v1` (53% Allan, offset karty mono_16, signal 43 px) vs. `render_body_grid_v2` (aktuální, popsáno níže). Viz `screen_main_toggle_layout()` v `screen_main.c` + STATUS.md TODO #14 (postup odstranění).
- **2026-07-19 (TODO #11(1b) HOTOVO)** — vizuální zvětšení ovladačů na ≥7 mm:
  - **Nastavení**: MUTE/BR±/AUTODIM±/THEME/LANG 56→**64 px** (7,5 mm); karty Jas a Auto-dim narostly o 8 px (108/110, bylo 100/102, Auto-dim posunut 266→274) — čerpáno ze 42 px volného prostoru před patičkou. Track/procenta jasu a hodnota auto-dim prodlevy re-centrovány o +4 px na nový střed tlačítek.
  - **Čas**: TZ_AUTO/TZ_MINUS/TZ_PLUS 56→**64 px**; hodnota zóny re-centrována +4 px.
  - **Kalibrace**: kompletně přeskládáno — tlačítka `−`/`+` 50×34→**60×60 px** (7,0 mm), řádky 104/144/184/224 (rozteč 40) → **110/176/242/308 (rozteč 66)**. Uvolněné místo umožnilo posunout 2 readonly řádky (VREF/TDC) na 346/374 a status na 402; samostatný řádek "Zmena se projevi..." zrušen — sloučen do výchozí (prázdné) hlášky status řádku. Karta zvětšena 320→**348 px** (62–410, těsně před patičkou).
- **2026-07-19 (3. vlna)** — RF bargraf zúžen na **pravou část jen** (398×54, dřív 776×54 přes celou šířku) → Allan karta se roztáhla na **364×242 přes celou výšku mřížky** (dřív 364×176). Pilulky zvětšeny **36→42 px** (4,2→4,9 mm) — strop bez kolize se spodní linkou headeru (6 px rezerva).
- **2026-07-19 (2. vlna)** — hybridní mřížka hlavní obrazovky: vlevo Allan 364×176 s plnými osami (vyšší než mezikrok 100 px, nižší než původních 242; `SCR_MAIN_GRID_LEFT_RATIO` 53→47 % kvůli šířce statistik), vpravo statistiky 3× 125×64 (mono_18) + mini trend 398×100, dole RF bargraf 776×54 přes celou šířku. Karta i okno ALLAN sdílí renderer `allan_plot(area, big)`.
- **2026-07-19** — layout pro 4,3" (1. vlna):
  - pilulky `UI_DIM_PILL_H` 30→**36 px** (3,5→4,2 mm); hit-slop GNSS/SYS beze změny (~52 px)
  - statistiky: hodnoty mono_16→**mono_18**; RF bargraf 353×43→**776×54**
  - nové okno **ALLAN** (s_view=23): velký log-log graf + σy(τ) tabulka; sesterské přepínání ALLAN↔HISTOGRAM tlačítky v patce (bez nav_push); titulek okna jen ASCII (mono_25 nemá σ/τ)
  - dřívější stejný den: hit-slop pilulek + Kalibrace `−`/`+`, poznámky sans_16+INK_4→sans_18+INK_3, System Health stavové řádky mono_14→mono_16
- **Zbývá (TODO #11 ve `STATUS.md`):** vizuální zvětšení ovladačů Nastavení/Čas (56→64 px) a Kalibrace (přeskládat řádkování), příp. globální posun těla textu o stupeň (57 míst, nutný audit šířek `dtext` boxů).
