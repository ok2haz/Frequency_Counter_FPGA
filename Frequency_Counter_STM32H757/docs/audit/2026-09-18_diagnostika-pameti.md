# Audit: diagnostika paměti (modul 19)  (2026-09-18)

- **Commit:** `0a1036d` (větev `audit/2026-09-09-hodiny-pwr`), pracovní strom bez změn v kódu
- **Jádro / doména:** CM7. `membench` běží v **UartTasku** (nehlídaný watchdogem),
  `sdram_log_put` v **FpgaTasku**, čtenáři kdekoli.
- **Soubory (1 269 ř. vč. hlaviček):** `membench.c` (722) + `.h` (116),
  `sdram_log.c` (307) + `.h` (124).
- **Projité sekce checklistu:** C (cache, umístění bufferů, MPU), D (souběh, RTOS),
  E (ošetření chyb, meze), G (FMC/SDRAM, QSPI).
- **Neprojité (a proč):** A (modul hodiny nekonfiguruje; jen čte `SystemCoreClock`
  pro přepočet kB/s), B (neběží na CM4 — jen *čte* `g_ipc.cm4.heartbeat`),
  F (do interní Flash **záměrně jen čte**), H (errata — bez revize silikonu).

## Proč zrovna tenhle modul

`membench` není jen další modul — je to **měřidlo**, podle kterého se v tomhle projektu
rozhoduje o hardwaru. Jeho verdikty už dvakrát poslaly vyšetřování špatným směrem
(falešný „adresy se opakují po 256 kB", verdikt „rozpad obsahu (refresh?)" → **L-0011**),
takže chyba *v něm* je dražší než chyba v kódu, který měří. Audit proto cílil hlavně na
otázku **„může tenhle nástroj něco tvrdit, aniž by to změřil?"** — a dvakrát může.

## Souhrn

Oba moduly jsou **napsané výjimečně poctivě**: vzory jsou čisté funkce indexu (žádná
druhá kopie dat), cache maintenance je správně označená jako otázka *správnosti*, ne
rychlosti, rychlost se měří odděleně od ověřování a bere se **minimum z N průchodů**
(rušení může měření jen zpomalit), SDRAM má před testem reverzibilní bezpečnostní sondu
a `sdram_log` si vlastní region ověří sám a při vadě se **nezapne**. Všechny cíle
`membench` jsem doložil jako skutečně volné (viz „Co bylo zkontrolováno").

**Verdikt: podmíněně funkční.** Nefunkčnost žádná; dvě vady ale patří do té nejdražší
třídy pro diagnostický nástroj — **tvrdí něco, co neměřily**: `membench` umí vypsat
„framebuffery se navzájem NEpřekrývají", aniž tu kontrolu provedl (**F-0116**), a
bezpečnostní seznam chráněných oblastí **neobsahuje měřicí log** přidaný 2026-08-30
(**F-0115**). Zrcadlově k tomu `sdram_log` nekontroluje alias na `bg_cache`
(**F-0117**) — obě strany téže mezery.

---

### F-0115 [S3] `SDRAM_PROTECTED[]` neobsahuje `.measlog` (8 MB @`0xC1000000`) — bezpečnostní sonda `membench` na měřicí log vůbec nesáhne

- **Místo:** `CM7/Core/Src/membench.c:343-349` (`SDRAM_PROTECTED[]`),
  použití `:367-374` (`sdram_safety_check`)
- **Popis:** Seznam oblastí SDRAM, se kterými se testovací blok nesmí překrývat, má
  pět položek: FB0/FB1/FB2, canvas pool a `.sdram`. **Chybí v něm `.measlog`** — datová
  cache měření (`sdram_log.c`), která do SDRAM přibyla 2026-08-30, tedy **po** vzniku
  toho seznamu.
- **Důkaz:**
  - `membench.c:343-349` — pět položek, nejvyšší `0xC0800000`.
  - `CM7/STM32H757BITX_FLASH.ld:79`: `SDRAM_LOG (xrw) : ORIGIN = 0xC1000000, LENGTH = 8M`
    — tedy oblast **uvnitř téhož 32MB čipu** (`SDRAM_TOTAL` = 32 MB, `membench.c:67`),
    která v seznamu není.
  - `sdram_safety_check()` (`:367`) probíhá **jen** položky toho pole; `sdram_alias_span()`
    (`:354-359`) skenuje navíc jen offsety 64 kB…2 MB od `0xC0400000`, tedy nikdy nedosáhne
    na vzdálenost `0xC1000000 − 0xC0400000` = 12 MB.
  - Hlavička modulu si přitom pravidlo sama ukládá (`membench.h:11-15`):
    *„testuje se VÝHRADNĚ paměť, kterou nikdo jiný nepoužívá … Když přibude cíl, MUSÍ se
    doložit, že je opravdu volný."* Obrácený směr — přibude-li nový **uživatel** SDRAM,
    musí se doplnit do seznamu — nikde napsaný není.
- **Dopad:** Při vadné adresní lince (což je přesně scénář, kvůli kterému ta sonda
  existuje) může testovací blok `0xC0400000` mířit do `.measlog`. `membench` pak do
  měřicího logu zapíše pět vzorů přes 512 kB a **tiše zničí až ~18 h naměřených dat**.
  ⚠️ **A nikdo se to nedozví:** `sdram_log` si region ověřuje **jen jednou při bootu**
  (`sdram_log_init` z FpgaTasku), takže poškození za běhu neodhalí — analýza (Allan,
  spektrogram, proklad) by pak počítala z nesmyslů, které vypadají jako platná data.
  Deterministické, jakmile alias existuje; bez aliasu se nestane nic.
- **Reprodukce:** `HYPOTÉZA — ověřit:` staticky doložitelné z kódu (seznam vs. linker).
  Na HW by se projevilo jen při skutečné vadě adresní linky; levnější kontrola je
  spustit `membench` a pak `sdramlog` — počet záznamů a `seq` nesmí po benchmarku
  vykazovat díru.
- **Návrh opravy:** doplnit `0xC1000000` do `SDRAM_PROTECTED[]` (jeden řádek). Poctivěji
  a v duchu hlavičky: adresy nebrat natvrdo, ale z **linker symbolů** (`_smeaslog`,
  `_emeaslog` už existují a `sdram_log.c:16-17` je používá) — pak se seznam nemůže
  rozejít s rozložením paměti. ⚠️ U `.sdram` totéž (`_ssdram`/`_esdram`, pokud existují).
- **Riziko opravy:** nízké — přidání položky jen **rozšíří** množinu případů, kdy se
  test přeskočí; nemůže způsobit falešnou chybu.
- **Vztah k lekcím:** **`L-0026`** zobecněně (přibude-li položka, přepočítej/doplň to,
  co ji má hlídat), **`L-0012`** (symetrická instance → viz **F-0117**, kde chybí opačný směr).
- **Stav:** **opraveno 2026-09-18** v `caa08b4` (společně s F-0117), ⬜ **neověřeno na HW**.
  Provedeno **poctivější variantou z návrhu**: adresa se nebere natvrdo, ale z linker
  symbolu `_smeaslog` — seznam se tak nemůže rozejít s mapou paměti potřetí. Doloženo
  v obrazu: pole `SDRAM_PROTECTED` v `.rodata` má **šest** položek a šestá je
  `000000c1` = `0xC1000000`, přičemž `nm` potvrzuje `_smeaslog = c1000000`.
  ⚠️ `.sdram` zůstává natvrdo — ta sekce v linkeru **neexportuje symboly** a přidat je
  by znamenalo sáhnout na `.ld` (pravidlo 6, bez souhlasu ne).

---

### F-0116 [S3] `membench` vypíše „framebuffery se navzájem NEpřekrývají", i když tu kontrolu vůbec neprovedl

- **Místo:** `CM7/Core/Src/membench.c:390-397` (měření `fb_alias` pod `if (span)`),
  `:412-413` (druhý zdroj `alias_off`), výpis
  `CM7/Core/Src/freertos_task_uart.c:1333-1334`
- **Popis:** `fb_alias` (sdílejí FB0↔FB2 / FB1↔canvas paměť?) se měří **pouze když
  `sdram_alias_span()` našel překryv**. `alias_off` se ale může stát nenulovým i **druhou
  cestou** — z `addr_lines_test()` uvnitř `bench_ram`. V té kombinaci UART vytiskne
  uklidňující větu o měření, které neproběhlo.
- **Důkaz:**
  - `membench.c:390` — `if (span) { … r->fb_alias = …; }`. Když `span == 0`, zůstává
    `fb_alias = 0` z `memset` v `set_target` (`:575`).
  - `membench.c:412-413` — `uint32_t al = addr_lines_test(…); if (al) r->alias_off = al;`
    → **druhý, nezávislý zdroj** nenulového `alias_off`.
  - 🔴 **Ty dvě sondy pokrývají jiné rozsahy:** `sdram_alias_span` (`:356`) jde
    `off = 64 kB … 2 MB`, kdežto `addr_lines_test` (`:260`) probíhá `o = 1 … words`,
    tedy **od 4 B**. Perioda překryvu **pod 64 kB** (vadný nízký adresní bit, např.
    `HADDR[10]` = 4 kB) je proto pro první sondu neviditelná a pro druhou viditelná.
  - `freertos_task_uart.c:1333-1334`:
    ```c
    else if (r->alias_off && !r->fb_alias)
        printf("      (framebuffery se ale navzajem NEprekryvaji — zobrazeni tim netrpi)\n");
    ```
    Podmínka je splněna přesně v tom případě výše → vytiskne se **tvrzení bez měření**.
- **Dopad:** Nástroj v jediném výpisu napíše *„ADRESY SE OPAKUJI po 4 kB"* a hned pod tím
  *„framebuffery se ale navzájem NEpřekrývají — zobrazení tím netrpí"*. To je **aktivní
  falešné uklidnění** u té nejdražší otázky, kterou modul klade (viz `membench.h:75-78`:
  *„Tohle je ta opravdu drahá otázka — kdyby to platilo, triple buffering je fakticky
  double"*). Vada nízkého adresního bitu se přitom projeví **uvnitř každého** framebufferu,
  takže je to případ, kdy je ta věta nejen nedoložená, ale nejspíš i nepravdivá.
  ⚠️ Projekt na téhle třídě už stál: „HW OBVINĚN — A BYL NEVINNÝ" i **L-0011**.
- **Reprodukce:** `HYPOTÉZA — ověřit:` staticky doložitelné. Vyvolat lze podstrčením
  `span = 0` při nenulovém `addr_lines_test` (na zdravé desce nenastane ani jedno).
- **Návrh opravy:** měřit `fb_alias` **bezpodmínečně** (čtyři reverzibilní jednoslovné
  sondy, cena zanedbatelná), nebo — je-li žádoucí to nechat podmíněné — zavést
  **třetí stav** (`fb_alias_checked`) a uklidňující větu tisknout jen tehdy, když se
  měřilo. ⚠️ Samotné přesunutí `if (span)` nestačí: `alias_off` se plní až **po**
  `sdram_safety_check`, takže měření musí být buď vždy, nebo až na konci `bench_ram`.
- **Riziko opravy:** nízké. Sonda `cells_alias` je reverzibilní a už se dnes spouští
  5× (chráněné oblasti) při každém běhu, takže čtyři další nejsou nový druh rizika.
- **Stav:** **opraveno 2026-09-19** — obě varianty zároveň (**1 + 2**), jak rozhodl
  uživatel, protože každá řeší jinou polovinu problému:
  - **měří se bezpodmínečně** a **jako první věc** v `sdram_safety_check()`, tedy
    ještě před kontrolou chráněných oblastí. Díky tomu je odpověď k dispozici
    i tehdy, když se celý test SDRAM přeskočí (`skipped`) — což je přesně stav,
    kdy je podezření na alias nejsilnější. Podmínka `if (span)` zmizela;
  - přibyl **třetí stav `fb_alias_checked`**, takže `fb_alias == 0` se už nedá
    přečíst jako „neměřilo se". Uklidňující větu smí výpis vytisknout **výhradně**
    když je příznak 1; jinak napíše *„překryv MEZI framebuffery se NEMĚŘIL — o
    zobrazení tenhle běh neříká nic"*.
  ⚠️ Zároveň se rozpletlo fragilní zřetězení `if (bit1) … else if (…)` ve výpisu —
  uklidňující větev visela v `else` u testu bitu 1, takže správně fungovala jen
  shodou okolností. Teď je to samostatné `if`.
  Z opravy vznikla **`L-0060`**. ⬜ **neověřeno na HW** (kritérium je v HW checklistu
  u modulu 19: na zdravé desce musí přijít `fb_alias` = 0 **a** `checked` = 1, tedy
  žádná z obou nových vět se neobjeví).
- **Vztah k lekcím:** **`L-0011`** (hlášku nástroje ber jako pozorování, ne diagnózu —
  tady nástroj vydává diagnózu, kterou neměřil), **`L-0017`** (tiché přeskočení bez
  příznaku), **`L-0028`** (věta, která tvrdí vlastnost, již kód neověřuje).

---

### F-0117 [S3] `sdram_log` kontroluje alias jen na framebuffery, ne na `bg_cache` (`.sdram`) — přitom právě ta dělá viditelnou vadu

- **Místo:** `CM7/Core/Src/sdram_log.c:98-119` (`aliases_framebuffer`), seznam `:100`
- **Popis:** Sonda „nemapuje se log náhodou na obraz?" testuje tři adresy
  (`0xC0000000`, `0xC0100000`, `0xC0200000`). **Nekontroluje `.sdram` @`0xC0800000`**
  (4 MB — `bg_cache`, glyph atlas, `g_mask_a/b`) ani canvas pool @`0xC0300000`.
- **Důkaz:**
  - `sdram_log.c:100` — `static const uint32_t FB[] = { 0xC0000000u, 0xC0100000u, 0xC0200000u };`
  - `CM7/STM32H757BITX_FLASH.ld:57` — `SDRAM (xrw) : ORIGIN = 0xC0800000, LENGTH = 4M`
    (sekce `.sdram`), tedy oblast uvnitř téhož čipu, která v seznamu chybí.
  - Vzdálenost `0xC1000000 − 0xC0800000` = **8 MB**, tedy přesně jeden adresní bit
    (`HADDR[23]`) — což je tatáž třída vady, jakou celá kontrola hledá.
  - Vnitřní alias test (`region_selfcheck`, `:133`) jde po mocninách dvou **uvnitř**
    regionu, nejvýš do 2^20 slov = 4 MB, takže na tuhle vzdálenost nedosáhne.
- **Dopad:** Kdyby se log mapoval na `.sdram`, přepisoval by **`bg_cache`** — pozadí
  předrenderované jednou v `screen_main_init()` a pak už jen čtené. Projev: partial
  redraw blituje poškozené pozadí → **problikávání celé plochy**. To je symptom, který
  se v tomhle projektu hledal **tři kola ve vykreslovacím kódu** (STATUS #138/#141) a
  jehož skutečná příčina byla nakonec v paměti. Tedy: kontrola existuje právě proto, aby
  se tahle záměna neopakovala, a zrovna na tu oblast nesahá.
  ⚠️ Nižší pravděpodobnost než u FB (log se plní ~4×/s po 32 B, ne souvisle), ale
  `bg_cache` se **nikdy nepřepisuje**, takže jednou poškozené pozadí zůstane poškozené.
- **Reprodukce:** `HYPOTÉZA — ověřit:` staticky doložitelné (seznam vs. linker).
- **Návrh opravy:** doplnit `0xC0800000` (a pro úplnost `0xC0300000`) do pole `FB[]`
  a upravit hlášku, aby nemluvila jen o „FB". Ideálně adresy brát z linker symbolů,
  ne natvrdo (viz F-0115). Sonda je reverzibilní, takže rozšíření je levné.
  ⚠️ Pozor na pořadí: `aliases_framebuffer()` běží **před** `region_selfcheck()`, tedy
  ještě než se do regionu cokoli zapíše — to zachovat.
- **Riziko opravy:** nízké; sonda vrací původní obsah a na `bg_cache` se v okamžiku
  volání (start FpgaTasku) navíc ještě nekreslí.
- **Vztah k lekcím:** **`L-0012`** (symetrická instance — `membench` má opačnou mezeru,
  viz **F-0115**; obě se mají opravit spolu), **`L-0011`**.
- **Stav:** **opraveno 2026-09-18** v `caa08b4` (společně s F-0115), ⬜ **neověřeno na HW**.
  Doplněn `canvas` (`0xC0300000`) i `.sdram` (`0xC0800000`); seznam je nově tabulka
  `{adresa, jméno}`, takže hláška uvádí **jméno oblasti**, ne index. Funkce
  přejmenována `aliases_framebuffer` → `aliases_reserved_sdram` (staré jméno by po
  rozšíření lhalo). Doloženo v obrazu: řetězcový pool `FB0\0FB1\0FB2\0canvas\0.sdram`
  a starý formát `"ALIAS na FB%u"` je pryč (0 výskytů), nový `"ALIAS na %s"` přítomen.

---

### F-0118 [S3] `sdram_log_reset()` je druhý zapisovatel `s_head`, ačkoli hlavička deklaruje jediného producenta

- **Místo:** `CM7/Core/Src/sdram_log.c:271` (`sdram_log_reset`), kontrakt
  `CM7/Core/Inc/sdram_log.h:31-35`, volající `CM7/Core/Src/freertos_task_uart.c:1371`
- **Popis:** Hlavička říká: *„jeden producent (FpgaTask, `sdram_log_put`) a libovolně
  mnoho čtenářů. Ring je bezzámkový."* `sdram_log_reset()` ale `s_head` **zapisuje** a
  volá se z **UartTasku** — tedy druhý zapisovatel do stavu, jehož bezzámkovost na
  jediném producentovi stojí.
- **Důkaz:**
  - `sdram_log.c:271` — `void sdram_log_reset(void) { s_head = 0; }`
  - `freertos_task_uart.c:1371` — `sdram_log_reset();` (příkaz `sdramlog reset`, UartTask)
  - `freertos_task_fpga.c:87` — `sdram_log_put(…)` (FpgaTask)
  - `sdram_log.c:206-212` — producent dělá `uint32_t h = s_head; … s_head = h + 1u;`,
    tedy **read-modify-write** s bodem preempce uprostřed.
- **Dopad:** Když reset padne mezi načtení `h` a zápis `h + 1`, producent hodnotu
  **obnoví** a reset se tiše ztratí — uživatel vidí, že `sdramlog reset` „nic neudělal".
  ⚠️ **Poškození dat to nezpůsobí:** záznam se zapíše do platného slotu a čtenáři mají
  kontrolu `s_head - abs > CAP`, která nekonzistenci zahodí. Dopad je tedy nízký; vážné
  je, že **deklarovaný invariant neplatí** a příští úprava se o něj může opřít.
- **Reprodukce:** `HYPOTÉZA — ověřit:` okno je dvě instrukce; v praxi spíš neopakovatelné.
  Ze zdrojáku je to ale doložitelné bez měření (dva volající, dvě úlohy).
- **Návrh opravy:** stejný vzor, jaký modul `membench` už používá (`g_membench_req`):
  UART jen nastaví příznak `s_reset_req = 1` a **producent** ho na začátku `sdram_log_put`
  vyhodnotí (`if (s_reset_req) { s_reset_req = 0; s_head = 0; }`). Tím zůstane `s_head`
  ve vlastnictví jediné úlohy. ⚠️ Cena: reset se projeví až s příštím vzorkem — u mrtvého
  linku by tedy „nezabral"; pokud to vadí, doplnit fallback po timeoutu.
- **Riziko opravy:** nízké, ale **mění pozorovatelné chování** (reset už není okamžitý) —
  proto to není čistě mechanická oprava.
- **Vztah k lekcím:** **`L-0054`** („jeden vlastník" je tvrzení o VŠECH volajících, ne
  o tom hlavním), **`L-0023`**.
- **Stav:** **opraveno 2026-09-19** — varianta **(b)**, kterou rozhodl uživatel:
  požadavek + fallback po timeoutu. (Nález navrhoval jen (a); (b) je (a) plus to,
  co (a) rozbíjelo.)
  - `sdram_log_reset()` je nahrazená trojicí `sdram_log_reset_request()` /
    `_done()` / `_force()`. Příznak `s_reset_req` konzumuje **výhradně producent**
    na začátku `sdram_log_put`, **před** `h = s_head` — tím `s_head` zůstává ve
    vlastnictví jediné úlohy a deklarovaný invariant zase platí.
  - ⚠️ Čistá varianta (a) měla vadu, kterou nález sám pojmenoval: při mrtvém SPI
    linku producent nepřijde a reset **by nezabral nikdy**. UART proto po
    `_request()` čeká do 300 ms (producent polluje 20 Hz = ~6 příležitostí) a když
    se neozve, zavolá `_force()` a **vypíše to jinak**: „vynulovano PRIMO —
    producent se za 300 ms neozval (mereni nebezi?)". Tím se pozorovatelné chování
    nezhoršilo a slovo „vynulováno" neznamená dvakrát něco jiného.
  - ⚠️ Čekání s `osDelay` je u **volajícího**, ne v modulu — `sdram_log.c` zůstává
    bez závislosti na scheduleru. Běží to v UartTasku, který watchdog nehlídá.
  - ⬜ **neověřeno na HW.**

---

### F-0119 [S4] Hlavička `sdram_log.h` pořád tvrdí 16 MB / 16 B / 1 048 576 záznamů — oprava F-0010 minula tři místa

- **Místo:** `CM7/Core/Inc/sdram_log.h:11` a `:90`,
  `CM7/Core/Src/freertos_task_uart.c:1358`
- **Popis:** F-0010 (modul 2) opravil kapacitu datové cache na **8 MB / 32 B / 262 144
  záznamů**. Tři místa zůstala na staré hodnotě — a jedno z nich je **úvodní odstavec
  téže hlavičky**, která o dvacet řádků níž uvádí správné číslo.
- **Důkaz:**
  - `sdram_log.h:11` — *„16 MB v SDRAM staci na **1 048 576 zaznamu** po 16 B."*
  - `sdram_log.h:56` — *„32 B -> 262 144 zaznamu na 8 MB."* (správně, **týž soubor**)
  - `sdram_log.h:90` — *„Levne: zapis 16 B."*
  - `sdram_log.c:22-23` — `SDRAM_LOG_BYTES (8u*1024u*1024u)`, `SDRAM_LOG_CAP` = 262 144;
    `_Static_assert(sizeof(sdram_log_rec_t) == 32u, …)` (`:32`) to i vynucuje.
  - `STM32H757BITX_FLASH.ld:79` — `LENGTH = 8M`.
  - `freertos_task_uart.c:1358` — *„Datova cache mereni v SDRAM (16 MB @0xC1000000)"*.
- **Dopad:** Není to kosmetika — je to **premisa pro „jak dlouhá τ jde z logu spočítat"**
  (4× nadhodnocená historie: ~18 h vs. deklarované ~3 dny). Přesně kvůli tomuhle číslu
  F-0010 vznikl; příští čtenář hlavičky dostane zas tu špatnou hodnotu, protože ji najde
  **dřív** než tu správnou.
- **Reprodukce:** přečíst `sdram_log.h` odshora.
- **Návrh opravy:** `docs:` commit — srovnat tři místa na 8 MB / 32 B / 262 144 a
  doplnit odhad pokrytí (~18 h při ~4 vzorcích/s). ⚠️ Zvážit, jestli kapacitu
  v komentáři neuvádět **vůbec** a odkázat na `SDRAM_LOG_CAP` — duplicitní číslo se
  rozešlo už podruhé (**L-0014**).
- **Riziko opravy:** žádné (jen komentáře).
- **Vztah k lekcím:** **`L-0014`** (souhrn/duplicitní číslo se rozejde se zdrojem pravdy),
  **`L-0039`** (oprava musí pokrýt *každý* výskyt, kvůli kterému vznikla).
- **Stav:** **opraveno 2026-09-18** v `339c596` (`docs:`), ⬜ **neověřeno na HW** (nelze —
  jsou to komentáře). Srovnána **všechna tři** místa a navíc u čísla nově stojí, že
  **zdroj pravdy je `SDRAM_LOG_CAP`**, ne komentář — aby se to nerozešlo potřetí.
  Doplněn i odhad pokrytí (~18 h při ~4 měřeních/s). `.text` 605936 B před i po
  (binárně ověřeno, že jde opravdu jen o komentáře).

---

### F-0120 [S4] `bit_errors` nese i hodnoty, které nejsou počtem chybných bitů

- **Místo:** `CM7/Core/Src/membench.c:508` (`bench_iflash`), `:565` + `:657` (`bench_w25q`
  a součet v `membench_run`), definice pole `CM7/Core/Inc/membench.h:51`
- **Popis:** `bit_errors` je dokumentovaný jako *„pocet chybnych BITU pres vsechny
  vzory"*. Dvě cesty do něj ale dávají něco jiného: `bench_iflash` používá `1` jako
  **sentinel** pro „nestabilní čtení", a u `bench_w25q` se při selhání přenosu cíl označí
  jako `skipped`, ale částečně nasbírané `bit_errors` se přesto přičtou do součtu.
- **Důkaz:**
  - `membench.c:508` — `{ snprintf(r->msg, …, "CTENI NESTABILNI!"); r->bit_errors = 1; }`
    — jedna nestabilní **dvojice součtů** přes 256 kB, ne jeden chybný bit.
  - `membench.c:552-559` — `note_error(...)` plní `bit_errors` uvnitř čtecí smyčky;
    `:565` — při `fail` se nastaví `r->skipped = 1` a funkce se vrátí **bez** vynulování
    už započtených chyb.
  - `membench.c:657` — `s_st.total_bit_errors += r[i].bit_errors;` běží pro **každý** cíl,
    tedy i pro `skipped`.
  - Důsledek na výpis: `:665` — závěrečná fáze je `"NALEZENY CHYBY"` podle
    `total_bit_errors`.
- **Dopad:** Souhrnné číslo „N chybných bitů" může obsahovat položku, která bitem není,
  a cíl označený jako *přeskočený* do něj přesto přispěje. Uživatel dostane součet, který
  nejde sečíst zpátky z řádků tabulky. Dnes to nikoho nesvede na scestí (`msg` u řádku to
  vysvětluje), ale je to měřidlo míchající jednotky.
- **Reprodukce:** nestabilní čtení Flash (nelze vyvolat) nebo chyba QSPI přenosu
  uprostřed vzoru → tabulka ukáže `skipped`, souhrn přesto nenulový.
- **Návrh opravy:** minimální — u `bench_w25q` při `fail` vynulovat `r->bit_errors`
  (výsledek je stejně neplatný), a `total_bit_errors` sčítat jen přes cíle s `tested`.
  Pro Flash zavést samostatný příznak (`unstable`) místo sentinelu v počtu bitů.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **`L-0011`** (číslo, které vypadá jako měření, ale mísí jednotky),
  **`L-0017`**.
- **Stav:** **opraveno 2026-09-20 — v plnem rozsahu** (predchozi davka resila jen obsazene casti).
  Sentinel `bit_errors = 1` u interni FLASH je zruseny a nahrazeny samostatnym
  priznakem **`unstable`**. `bit_errors` tim nese uz VYHRADNE pocet bitu.
  ⚠️ **Oprava se zamerne delala naraz ve vsech CTYRECH mistech**, protoze kdyby se
  jedno minulo, vznikl by **zeleny radek s poplasnou hlaskou** — tedy prave to riziko,
  kvuli kteremu byla predtim vedome odlozena:
  1. `membench.h` — nove pole `unstable` + `any_unstable` ve stavu; prepsan komentar
     u `bit_errors`, ktery do te doby rikal, ze `unstable` bylo ZAMITNUTO;
  2. `membench.c` — `bench_iflash` nastavi `unstable`, souhrn pres cile plni
     `any_unstable` (do souctu bitu se NEpricita, to byla puvodni vada) a `msg` ma
     vetev „CTENI NESTABILNI!" i bez nenuloveho `bit_errors`;
  3. UART verdikt — `NALEZENY CHYBY` i pri `any_unstable`, s pripojenym
     „+ NESTABILNI CTENI FLASH";
  4. okno PAMETI — radek zcervena i pri `unstable` (bez teto vetve by po zruseni
     sentinelu zustal ZELENY s hlaskou o nestabilnim cteni).
  ⬜ **neovereno na HW** (kriterium: `membench` -> radek interni FLASH „cteni stabilni"
  a souhrn „OK" bez pripony).

---

### F-0121 [S4] Komentář nad `YIELD_WORDS` uvádí 64 kB, konstanta je 32 kB

- **Místo:** `CM7/Core/Src/membench.c:218-222`
- **Popis:** Blokový komentář zdůvodňuje volbu velikosti bloku větou *„64 kB je
  kompromis: ~0,5 ms práce na blok"*, ale hned následující `#define` je **32 kB**
  a jeho vlastní řádkový komentář to i říká.
- **Důkaz:**
  ```c
  * ... 64 kB je kompromis: ~0,5 ms prace na blok. */
  #define YIELD_WORDS  (8u * 1024u)       /* 32 kB — jemneji kvuli pasmu pro LTDC */
  ```
  `8 * 1024` slov × 4 B = **32 kB**. Dva komentáře u jedné konstanty si odporují.
- **Dopad:** Žádný na běh. Při ladění propustnosti (kolik času drží UartTask CPU) dá
  ten starší komentář dvojnásobné číslo — a je to přesně veličina, kvůli které se blok
  kdysi zmenšoval (hladovění UiTasku po benchmarku).
- **Reprodukce:** přečíst `membench.c:218-222`.
- **Návrh opravy:** `docs:` — srovnat větu na 32 kB, případně nechat jen řádkový
  komentář u `#define` (jedna hodnota, jedno místo).
- **Riziko opravy:** žádné.
- **Vztah k lekcím:** **`L-0014`**.
- **Stav:** **opraveno 2026-09-18** v `339c596` (`docs:`). Komentář nově uvádí 32 kB
  a výslovně říká, že **platná hodnota je u `#define`** (jedna hodnota, jedno místo).

---

## Fáze oprav — skupina A (2026-09-18)

| commit | nálezy | co se změnilo |
|---|---|---|
| `caa08b4` | **F-0115** + **F-0117** + F-0120 (část) | chráněné oblasti SDRAM na **obou** stranách; `.measlog` z linker symbolu, `.sdram`+canvas do `sdram_log`; neplatné částečné výsledky QSPI se nulují a souhrn sčítá jen proběhlé cíle |
| `339c596` | **F-0119** + **F-0121** + F-0120 (dokumentace) | zastaralá čísla (16 MB/16 B/1 048 576 → 8 MB/32 B/262 144 na třech místech; 64 kB → 32 kB) a zdůvodnění ponechaného sentinelu u `bit_errors` |

**Otevřené zůstávají F-0116 a F-0118 (skupina B)** — obě vyžadují **rozhodnutí
o variantě**, ne kód:
- **F-0116:** měřit `fb_alias` bezpodmínečně (4 sondy navíc při každém běhu), nebo
  zavést třetí stav `fb_alias_checked` a uklidňující větu tisknout jen po měření.
- **F-0118:** oprava přes příznak (vzor `g_membench_req`) **mění pozorovatelné
  chování** — `sdramlog reset` by se projevil až s příštím vzorkem, tedy při mrtvém
  linku „nezabere".

**Ověřovací řetězec:** `build.sh Release CM7` 0 varování; `audit.py` 92 OK / 0 selhání
/ 2 s varováním; `.text` 605848 → **605936** (+88) u `fix:`, a **605936 → 605936**
u `docs:` (binární důkaz, že šlo opravdu jen o komentáře).

🔑 **Co ověřit na desce** (⬜ zatím neproběhlo): `membench` musí doběhnout jako dřív
(řádek SDRAM `OK`, retence 0) — nové položky v chráněném seznamu smí test nejvýš
**přeskočit**, ne nahlásit chybu; `sdramlog` po bootu musí hlásit `ready` (kdyby nová
kontrola `.sdram`/canvas falešně zahlásila alias, log by se **nezapnul** a `fail[]` by
to řekl). Pak `membench` a hned `sdramlog` — počet záznamů a `seq` nesmí mít díru.

## Co bylo zkontrolováno a je v pořádku

Pro nástroj, jehož verdikty rozhodují o podezření na HW, je tenhle seznam stejně
důležitý jako nálezy.

**🔑 Všechny cíle `membench` jsou doložitelně VOLNÉ** (krok 3 procesu — ne z komentáře;
CLAUDE.md k tomuhle modulu výslovně varuje, že *„linker sem nic neumisťuje" NENÍ důkaz*,
protože pevná adresa v middlewaru se v mapfile neprojeví — právě tak SRAM1 kdysi shodila CM4):

| cíl | jak doloženo |
|---|---|
| **DTCM** `0x20000000` | `.map` má jen deklaraci regionu, žádnou sekci; `_estack = ORIGIN(RAM_D1)+LENGTH` = `0x24080000` (`.ld:39,47`), tedy **hlavní zásobník není v DTCM**; grep na `0x2000xxxx` ve zdrojích obou jader nenašel uživatele (3 shody = komentář, hash klíč, konstanta ETH aliasu) |
| **SRAM1 D2** `0x30001000` | `nm` nad **oběma** obrazy: v `0x3000xxxx` **žádný symbol**; `.map` CM7 bez sekce; linker `.ld:50` říká „CM7 do RAM_D2 nic nelinkuje"; jediné absolutní adresy ve zdrojích = UART `ram write` (dokumentovaná diagnostika) a `#pragma location = 0x30000100`, který je pod `#if defined(__ICCARM__)` → **pro GCC neaktivní** (`ethernetif.c:90-92`), GCC větev dává `.Rx_PoolSection` a `audit.py` ho potvrzuje na `0x300400c0` (SRAM3) |
| **AXI SRAM** | vlastní `static` buffer `s_axi_buf` `__attribute__((aligned(32)))` — netestuje cizí paměť |
| **SDRAM scratch** `0xC0400000` | mimo region 0 (FB) i mimo `.sdram` (`0xC0800000`); navíc **reverzibilní sonda před každým během** (`sdram_safety_check`) — s výhradou **F-0115** |
| **FLASH bank1** | **jen čtení**, zápis/erase záměrně nikde |
| **W25Q** | vyhrazený `W25Q_BENCH_BASE` pod QSPI mutexem |
| **SRAM4/D3** | **záměrně netestována** (žije tam IPC) — a je u toho poctivě napsané, že odebrání D3 pád CM4 **nevyřešilo**, takže se to nesmí citovat jako příčina |

**C — cache (nejdůležitější u měřidla paměti):** `cache_clean`/`cache_invalidate`
(`membench.c:202-213`) zarovnávají adresu dolů a délku nahoru na 32 B, jak CMSIS
vyžaduje. Pořadí je správně: **clean po zápisu, invalidate před čtením** (`:469`), a
u retence **clean PŘED čekáním, invalidate AŽ POTOM** (`:296-298`) — jinak by se měřila
retence cache, ne DRAM. ✅ `flush_word` v `sdram_log.c` předává nezarovnanou adresu
(`&base[words-1]`, offset 28 v řádce), ale to je v pořádku: `DCIMVAC`/`DCCMVAC` adresují
**řádku** a CMSIS 5 velikost dopočítává z `addr & 31`, takže se ošetří právě ta jedna
řádka. ✅

**E — meze a přetečení:**
- `kbs_from_cycles` (`:182-189`) počítá v `uint64_t`; nejhorší případ
  256 kB × 480 MHz ≈ 1,26e14 ≪ 2^64. Dělení nulou ošetřeno (`cycles == 0 → 0`). ✅
- `addr_lines_test` vrací `v * 4u` jen pro `v < words` a mocninu dvou (`:273`) — jinak
  `MEMBENCH_ALIAS_UNKNOWN`. **To je přesně obrana proti falešnému verdiktu**, který
  projekt už jednou vyrobil (cizí zápis do kontrolní buňky → nesmyslná „vzdálenost"). ✅
- `gate_changed` (`sdram_log.c:187-192`): `d * 10u` v `uint64_t`, nejhůř ~1e12. ✅
  Prah 10 % správně odděluje presety brány od ppm kolísání.
- `idx_of` / `read_back` (`:216-254`): `abs ≥ oldest` je zaručeno přes `idx_of`, smyčka
  vždy terminuje, index se maskuje (kapacita je mocnina 2, hlídá `_Static_assert`). ✅
- `retention_test` přičítá chyby **přes `note_error`**, takže `pat_err[]` sedí na
  `bit_errors`; volající to ví a nepřičítá `retain_err` znovu (`:637`). ✅

**D — souběh:** `s_head` se zvedá **až po** zápisu záznamu s `__DMB()` mezi tím
(`sdram_log.c:211-212`); čtenáři ověřují `s_head - abs > CAP` **po** kopii (`:231`, `:252`),
tedy ve správném pořadí. Reset epochy (`s_head = 0`) čtenáře nerozbije — odečet
v `uint32_t` podteče na hodnotu > CAP a dávka se korektně zahodí. ✅ (Výjimka = **F-0118**.)
Snapshot `membench_state()` je jen pro čtení a UART ho vypisuje přes `%.Ns`, takže
přepis řetězce za běhu kreslení neteče mimo pole. ✅

**Odolnost měření proti rušení:** rychlost se měří na **pevném** bloku bez `osDelay`,
ve **3 průchodech s minimem** (`SPEED_PASSES`, `:246`) — správná statistika pro sdílenou
sběrnici, kde rušení může jen zpomalit. Smyčka je **rozbalená po 8** a čtecí součet je
seskupený po čtveřicích, obojí **změřeno, ne odhadnuto**, a komentář poctivě uvádí, že
měření vyvrátilo původní úvahu autora. ✅ `s_read_sink` je `volatile` v souborovém
rozsahu, jinak by optimalizátor čtecí smyčku zahodil a „rychlost" by byla fikce. ✅

**W25Q:** erase **před každým vzorem** (NOR umí jen 1→0), erase je **mimo** měřený čas
(`t0` až za ním, `:543`), mutex s timeoutem 2 s a korektní `osMutexRelease` na všech
cestách včetně chybových (`:529`, `:563`). ✅

**Selftesty:** oba modulu mají pure-logic selftest zapojený do `selftest`
(`membench_selftest` ověřuje generátory vzorů i `note_error` včetně toho, že si první
chyba pamatuje **hodnoty**; `sdram_log_selftest` ověřuje indexování před i po přetočení).
`membench_selftest` navíc explicitně testuje, že `PAT_ADDR` vrací **různé** hodnoty pro
různé indexy — kdyby vracel konstantu, adresní vady by se nikdy nenašly. ✅ To je přesně
pozitivní kontrola měřidla ve smyslu **L-0020**.

**`sdram_log` obrana při vadné paměti:** při selhání `region_selfcheck` se log
**nezapne** a důvod je v `fail[]`; každý zahozený vzorek zvedne `dropped` — tedy tichý
přeskok **s počítadlem** (**L-0017**). ✅ `sdram_log_invalidate` dělá **Clean+Invalidate**
(oprava F-0011), ne holou invalidaci, která by zahodila špinavé řádky. ✅
`may_alias` u `sdram_word_t` (`:96`) je zdůvodněný a nutný — bez něj by GCC směl
považovat zápis přes slovo za nesouvisející se záznamy a **test aliasu by tiše přestal
fungovat**. ✅

## Nezkontrolováno / omezení tohoto běhu

- **Nic z tohoto modulu neběželo na HW v rámci auditu** — nálezy jsou statické.
  F-0115…F-0117 jsou doložitelné ze zdrojáku + linkeru; na desce se projeví **jen při
  skutečné vadě adresní linky**, což je stav, který dnes (po opravě čtecí cesty FMC,
  `membench` 0 chybných bitů) nenastává.
- **Skutečné hodnoty propustnosti** (kB/s) nelze staticky posoudit; posuzovala se jen
  metodika měření.
- **`sdram_log_read_back()` nemá dnes volajícího** (konzument je plánovaný, STATUS #62)
  — kód je pročtený a indexování kryje selftest, ale **na reálných datech neběžel**.
  `audit.py` ho zná jako záměrně mrtvý.
- **Nepřezkoumáno do hloubky:** zobrazení výsledků v okně PAMETI (`s_view=43`) — patří
  do modulu 11 (aplikační okna); tady se kontroloval jen UART výpis, protože právě přes
  něj se formulují verdikty o HW.
