# Audit: MPU / cache / linker  (2026-09-09)

- **Commit:** `303beb6` (branch `audit/2026-09-09-hodiny-pwr`)
- **Jádro / doména:** CM7 (D1/D2/D3 + externí SDRAM) i CM4
- **Projité sekce checklistu:** C (celá), B (sdílená paměť a její atributy), A (hodiny SRAM domén),
  F (linker, VTOR), okrajově G (DMA2D, ETH buffery)
- **Neprojité (a proč):** D (přerušení/RTOS) — patří modulu 4; E mimo chybové cesty MPU;
  H (errata) — bez vazby na tento modul.
- **Soubory čtené celé:** `CM7/STM32H757BITX_FLASH.ld`, `CM7/Core/Src/main.c` (`MPU_Config`),
  `CM7/Core/Inc/sdram_log.h`. **Cíleně:** `CM7/Core/Src/sdram_log.c`,
  `CM7/app/hal/stm32/prim_stm32_hal.c` (cache kolem DMA2D), `CM7/Core/Src/screenshot.c`,
  `CM7/Core/Src/membench.c`, `CM4/Core/Src/main.c`, `CM4/STM32H757BITX_FLASH.ld`,
  `Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c` (VTOR).
- **Umístění objektů: dohledáno v obrazu** (`arm-none-eabi-nm --print-size` nad
  `CM7/Release/H757_LED_CM7.elf` a `CM4/Release/H757_LED_CM4.elf`), ne odhadem ze zdrojáku.

## Souhrn

Mapa paměti je promyšlená a **linker skript je nejlépe dokumentovaný soubor projektu** — nese
kompletní rozvrh 32 MB SDRAM včetně rezerv. Čtyři MPU oblasti jsou správně zarovnané, nepřekrývají
se, MPU se zapíná před cache a atributy odpovídají tomu, kdo do které paměti sahá (WT tam, kde čte
LTDC; non-cacheable + shareable pro IPC; WBWA tam, kde čte jen CPU). Všechny tři adresní kontroly
předepsané pro ETH na CM4 vycházejí.

Nálezy jsou dvě skupiny. První je **tichost**: MPU oblast 3 se spoléhá na hodnotu, kterou nenastavuje
(F-0008), a `sdram_log_invalidate()` nabízí operaci, která by v dnešním uspořádání zahodila data
(F-0011) — obojí dnes nekousne, ale ani jedno by se neohlásilo. Druhá je **rozejitá dokumentace**:
komentář u MPU oblasti i `CLAUDE.md` uvádějí u datové cache měření dvojnásobnou velikost a
čtyřnásobnou kapacitu, než jaká je v kódu (F-0009, F-0010).

Samostatně stojí F-0013: objekt, který je ze všech nejcitlivější na otevřenou vadu retence SDRAM
(#237/#238), je zároveň jediný, který se po zápisu už nikdy nepřepisuje.

**Verdikt: funkční.** Žádný nález nebrání provozu ani neodpovídá pozorované vadě displeje;
jsou to latentní pasti a nesrovnalosti dokumentace.

---

### F-0008 [S3] MPU oblast 3 nenastavuje `Enable` — spoléhá na zbytek po oblasti 2

- **Místo:** `CM7/Core/Src/main.c:176-186` (funkce `MPU_Config()`)
- **Popis:** Bloky oblastí 0, 1 a 2 začínají `MPU_InitStruct.Enable = MPU_REGION_ENABLE;`.
  Blok oblasti 3 tenhle řádek **nemá** a začne rovnou `Number`, takže povolení přebírá z hodnoty,
  kterou ve struktuře nechal blok oblasti 2.
- **Důkaz:** `main.c:118` (`Enable` u oblasti 0), `:134` (oblast 1), `:153` (oblast 2) —
  a `main.c:176` `MPU_InitStruct.Number = MPU_REGION_NUMBER3;` bez předchozího `Enable`.
  Ostatní pole (`SubRegionDisable`, `TypeExtField`, …) blok oblasti 3 nastavuje kompletně,
  takže vynechání není záměrná úspora, ale opomenutí.
- **Dopad:** Dnes žádný — oblast 2 nechá ve struktuře `MPU_REGION_ENABLE`, takže se oblast 3
  povolí. Je to ale **tiché**: kdyby někdo mezi oblasti 2 a 3 vložil blok s `Enable = DISABLE`
  (nebo pořadí přeházel), oblast 3 by se přestala aktivovat a `.measlog` by spadl do defaultní
  mapy, kde je `0xA0000000–0xDFFFFFFF` **Device paměť**. Log by dál fungoval, jen by sekvenční
  čtení bylo řádově pomalejší — a nic by to neohlásilo. Přesně ta třída, kterou má projekt
  zdokumentovanou jako nejdražší.
- **Reprodukce:** Staticky doložitelné ze zdrojáku. Za běhu ověřitelné čtením `MPU->RBAR`/`RASR`
  pro oblast 3, ale to není potřeba — dnes je stav správný.
- **Návrh opravy:** Doplnit `MPU_InitStruct.Enable = MPU_REGION_ENABLE;` na začátek bloku
  oblasti 3. Jednořádková změna, nulové riziko.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** nová lekce po opravě (třída „nastavení převzaté ze zbytku po předchozím
  volání“ — týž vzor jako `hi2c1.ErrorCode` sticky u SD).
- **Stav:** opraveno 2026-09-10 — doplněn `Enable = MPU_REGION_ENABLE`; funkčně no-op (oblast 2 tam tu hodnotu nechávala), odstraněna možnost tichého vypadnutí.

---

### F-0009 [S4] Komentář u MPU oblasti 3 uvádí 16 MB, kód nastavuje 8 MB

- **Místo:** `CM7/Core/Src/main.c:166-178`
- **Popis:** Komentář nad oblastí 3 zmiňuje velikost **16 MB třikrát** (v nadpisu bloku a
  v závěrečném „16 MB @0xC1000000 sedí“), zatímco kód nastavuje `MPU_REGION_SIZE_8MB`.
- **Důkaz:** `main.c:166` („datova cache mereni v SDRAM (`sdram_log.c`), 16 MB @0xC1000000“),
  `main.c:175` („Region MUSI byt mocnina 2 a prirozene zarovnany: 16 MB @0xC1000000 sedi“)
  vs. `main.c:178` `MPU_InitStruct.Size = MPU_REGION_SIZE_8MB;`.
  **Kód a linker se přitom shodují na 8 MB:** `STM32H757BITX_FLASH.ld:79`
  `SDRAM_LOG … LENGTH = 8M`, `sdram_log.c:22` `#define SDRAM_LOG_BYTES (8u*1024u*1024u)`,
  a v obrazu `s_buf` @`0xC1000000` velikost `0x800000` (= 8 MB), `_emeaslog` = `0xC1800000`.
  Chybný je tedy **jen komentář**.
- **Dopad:** Čtenář, který podle komentáře plánuje rozsah dat, počítá s dvojnásobkem.
  Zavádí to zejména proto, že komentář zdůvodňuje zarovnání — a to zdůvodnění je pro 16 MB
  jiné než pro 8 MB.
- **Reprodukce:** Přímé porovnání řádků výše.
- **Návrh opravy:** Přepsat komentář na 8 MB. Velikost oblasti neměnit — 8 MB odpovídá
  linkeru i poli.
- **Riziko opravy:** nulové (komentář).
- **Vztah k lekcím:** `L-0008` (komentář popisuje něco jiného než kód pod ním).
- **Stav:** opraveno 2026-09-10 — komentář srovnán na 8 MB (`docs:` commit).

---

### F-0010 [S4] `CLAUDE.md` uvádí u datové cache měření 16 B/záznam a ~3 dny historie; skutečnost je 32 B a ~18 hodin

- **Místo:** `CLAUDE.md`, sekce „Datová cache měření (`sdram_log.c`…)“ vs.
  `CM7/Core/Inc/sdram_log.h:60-71`, `CM7/Core/Src/sdram_log.c:22-23`
- **Popis:** Dokumentace tvrdí „16 MB v SDRAM = **1 048 576 vzorků po 16 B**; při dnešní kadenci
  FPGA (~4 měření/s) to je **~3 dny** souvislé historie“ a „**Záznam 16 B** = `{seq, t_ms, f_uhz}`“.
  Struktura je ve skutečnosti **32 B** a region **8 MB**.
- **Důkaz:** `sdram_log.h:70` — pole `reserved` s komentářem „*0 — dorovnani na 32 B, misto pro
  budouci pole*“; `sdram_log.c:23` `#define SDRAM_LOG_CAP (SDRAM_LOG_BYTES / sizeof(sdram_log_rec_t))`
  s komentářem `/* 262 144 */`. Z toho `sizeof = 8 MB / 262 144 = 32 B`.
  Kapacita 262 144 vzorků při ~4 měřeních/s = 65 536 s ≈ **18,2 hodiny**, ne ~3 dny.
  Rozdíl je **4×** (poloviční region × dvojnásobný záznam).
- **Dopad:** Není to porucha, ale falešná premisa pro to, kvůli čemu cache existuje — Allanova
  odchylka na dlouhých τ, spektrogram a proklady. Kdo si podle `CLAUDE.md` naplánuje analýzu na
  τ v řádu dnů, zjistí až na desce, že data nejsou.
- **Reprodukce:** Výpočet výše z uvedených řádků.
- **Návrh opravy:** Srovnat `CLAUDE.md` (32 B, 8 MB, 262 144 vzorků, ~18 h). Zvážit poznámku, že
  linker má hned nad logem 8 MB volných a rozšíření je jeden řádek + `SDRAM_LOG_BYTES`
  (`STM32H757BITX_FLASH.ld:77-78` to samo nabízí).
- **Riziko opravy:** nulové (dokumentace).
- **Vztah k lekcím:** `L-0006` (číslo v dokumentaci se musí odvodit z kódu, ne opsat).
- **Stav:** opraveno 2026-09-10 — `CLAUDE.md` srovnána: 32 B/záznam, 8 MB, 262 144 vzorků, ~18 h (`docs:` commit).

---

### F-0011 [S3] `sdram_log_invalidate()` nemá volajícího a v dnešním uspořádání by zahodil nejnovější záznamy

- **Místo:** `CM7/Core/Src/sdram_log.c:273-276`, kontrakt v `CM7/Core/Inc/sdram_log.h:36-40,118`
- **Popis:** Funkce dělá `SCB_InvalidateDCache_by_Addr(s_buf, SDRAM_LOG_BYTES)` nad celým 8MB
  polem. Oblast leží v MPU oblasti 3, která je **Write-Back Write-Allocate**, a plní ji **CPU**
  (FpgaTask). Invalidace bez předchozího `clean` na WB oblasti **zahodí dirty řádky**, tedy
  právě ty nejčerstvější záznamy, které ještě nedošly do SDRAM.
- **Důkaz:** `main.c:180-185` — oblast 3 `IsCacheable = MPU_ACCESS_CACHEABLE` a
  `IsBufferable = MPU_ACCESS_BUFFERABLE` (= WBWA). `sdram_log.c:275` volá invalidaci bez
  `SCB_CleanDCache_by_Addr`. Producent je CPU: `sdram_log_put()` volá FpgaTask (viz `CLAUDE.md`,
  sekce datové cache). Grep přes `CM7` najde **jen deklaraci a definici, žádné volání**.
- **Dopad:** Dnes nulový (nikdo nevolá). Je to ale nabitá past: hlavička konzumenta k volání
  přímo **vybízí** a podmínku „až bude log plnit DMA“ nese jen prozaický komentář v hlavičce —
  u samotné funkce žádná pojistka ani poznámka není. Kdo se bude řídit hlavičkou dnes, tiše
  přijde o poslední vzorky (v ringu se to projeví jako nesouvislá data, ne jako chyba).
- **Reprodukce:** `HYPOTÉZA — ověřit:` naplnit log, zavolat `sdram_log_invalidate()` a porovnat
  `sdram_log_get(0)` před a po; očekává se ztráta posledních záznamů. Bez HW nelze změřit.
- **Návrh opravy:** Buď (a) doplnit do funkce guard/komentář, že se smí volat **jen** když log
  plní DMA, a do té doby ji nechat prázdnou; nebo (b) udělat ji bezpečnou bezpodmínečně —
  `SCB_CleanInvalidateDCache_by_Addr()` místo samotné invalidace (na CPU-plněném logu je clean
  zbytečný, ale neškodný, a odstraní celou třídu chyby).
- **Riziko opravy:** nízké u (b); (a) je jen komentář.
- **Vztah k lekcím:** `L-0002` (ke každé cache operaci patří ta správná polovina páru).
- **Stav:** opraveno 2026-09-10 — `SCB_CleanInvalidateDCache_by_Addr` místo samotné invalidace; clean je u CPU-plněného logu nutný a u DMA neškodný.

---

### F-0012 [S3] `bg_cache` a glyph atlas leží v Device paměti — nezarovnaný přístup by byl UsageFault

- **Místo:** sekce `.sdram` v `CM7/STM32H757BITX_FLASH.ld:53-57,196-204`; obsah dohledaný v obrazu
- **Popis:** Sekce `.sdram` je záměrně **mimo všechny MPU oblasti**, takže platí defaultní mapa,
  kde je `0xA0000000–0xDFFFFFFF` **Device paměť**. Na Cortex-M7 je každý **nezarovnaný** přístup
  do Device paměti chybou (UsageFault) **bez ohledu na `CCR.UNALIGN_TRP`** — na rozdíl od Normal
  paměti, kde projde. V téhle sekci leží 1,1 MB obrazových dat.
- **Důkaz:** obsah `.sdram` z obrazu (`nm --print-size`):
  `s_glyph_atlas` @`0xC0800000` (0x40000 = 256 kB), **`bg_cache` @`0xC0840000` (0xBB800 = 768 000 B
  = 800×480×2, tedy celá obrazovka v RGB565)**, `g_mask_b` @`0xC08FB800` (0xC800),
  `g_mask_a` @`0xC0908000` (0xC800). Záměr je doložený komentářem v linkeru (`:75-76`):
  „`.sdram` MUSI zustat mimo cacheable MPU region — glyph atlas z nej cte primo DMA2D
  a Device pamet je koherentni bez udrzby cache“.
- **Dopad:** Volba je pro koherenci s DMA2D správná a dnes nic nepadá. Riziko je v tom, co se sem
  smí psát dál: jakýkoli optimalizovaný přístup, který nad RGB565 daty (2 B/px) sáhne 32bitově na
  adresu dělitelnou dvěma, ale ne čtyřmi, skončí **UsageFault** — a protože `SCB->SHCSR` se nikde
  nezapisuje, eskaluje na HardFault. Projevilo by se to jako náhodný pád při kreslení, ne jako
  chyba paměti.
- **Reprodukce:** `HYPOTÉZA — ověřit:` (1) staticky projít cesty, které čtou/zapisují `bg_cache`
  a `g_mask_*`, jestli některá nepoužívá 32bitové přístupy nebo `memcpy` s lichým posunem;
  (2) na HW zapnout `SCB->SHCSR` UsageFault a nechat běžet plný redraw — dnes by fault eskaloval
  do HardFaultu s `CFSR` bitem UNALIGNED, což crash black-box zaznamená.
- **Návrh opravy:** Neměnit umístění (koherence s DMA2D je důležitější). Doplnit **poznámku
  k sekci `.sdram` v linkeru i v `CLAUDE.md`**, že do ní patří jen data, ke kterým se přistupuje
  zarovnaně, a proč. Případně zpřísnit tím, že se `SCB->SHCSR` UsageFault zapne (dnes je vypnutý,
  takže se nezarovnaný přístup nedozvíme přesně, jen jako HardFault).
- **Riziko opravy:** nulové u dokumentace; zapnutí UsageFault je střední (mění chování při chybě).
- **Vztah k lekcím:** nová lekce po opravě.
- **Stav:** opraveno 2026-09-10 **jen dokumentací** (`docs:`) — umístění se nemění (koherence s DMA2D), ale u sekce `.sdram` je nově napsané, že do Device paměti smí jen zarovnané přístupy a proč.

---

### F-0013 [S3] Objekt nejcitlivější na otevřenou vadu retence SDRAM je zároveň jediný, který se nikdy nepřepisuje

- **Místo:** `bg_cache` @`0xC0840000` (768 000 B) v sekci `.sdram`
- **Popis:** Otevřená vada #237/#238 je degradace retence buněk SDRAM po studeném startu
  (změřeno 1 048 646 chybných bitů). Framebuffery jsou proti ní částečně chráněné tím, že se
  **přepisují každý snímek** — zápis buňku obnoví. `bg_cache` se naproti tomu zapíše **jednou**
  v `screen_main_init()` a pak už se jen čte, takže se na ní rozpad projeví plnou silou.
- **Důkaz:** umístění a velikost z obrazu (viz F-0012). `CLAUDE.md` k #138 sám uvádí:
  *„Framebuffery vadu maskovaly, rozpadala se **`bg_cache`** — zapsaná jednou v `screen_main_init`
  a pak už jen čtená → partial redraw blitoval poškozené pozadí → problikávání celé plochy.“*
  Sekce `.sdram` je Device paměť, takže obsah není držený ani v cache — každé čtení jde do buněk.
- **Dopad:** Vysvětluje, proč se vada projevuje jako problikávání celé plochy, a ne jako šum
  v jednotlivých snímcích. Není to nová příčina — je to **expozice**, která z otevřeného
  #237/#238 dělá viditelnou poruchu displeje.
- **Reprodukce:** ✅ **Nástroj doplněn 2026-09-09 — UART `bgcheck`.** `membench` totiž tenhle
  rozsah měřit **nemůže**: `membench.c:348` má `0xC0800000` v seznamu nedotknutelných, protože
  se do `.sdram` nesmí psát. `bgcheck` to obejde bez zápisu — `bg_cache` se zapisuje jednou
  a pak už se jen čte, takže se jeho obsah nesmí měnit; test ho po blocích sečte, počká 1 s
  a sečte znovu. ⬜ Zbývá spustit na desce po power-cyklu.
  🔑 **Tohle je hlavní výsledek modulu:** naměřených „1 048 646 chybných bitů retence“ (#238)
  pochází z **jiné paměti** (`0xC0400000`, MPU region 1) než ta, na které problikávání
  skutečně závisí. Rozsah, o který jde, nebyl dosud změřen nikdy.
- **Návrh opravy:** Neopravovat v tomto modulu (příčina je v paměti, ne v MPU). Levná zmírnění
  k rozvaze při řešení #238: (a) periodicky `bg_cache` přepsat — obnoví buňky stejně jako
  framebuffery a stojí jeden blit za delší dobu; (b) při vstupu na hlavní obrazovku ji
  regenerovat, ne jen číst. ⚠️ Obojí je **náplast na symptom** — kořen je refresh/retence SDRAM.
- **Riziko opravy:** střední — periodický přepis zasahuje do vykreslovací cesty, kde má projekt
  historii regresí (guard „obsah je stejný“).
- **Vztah k lekcím:** —
- **Stav:** ✅ **uzavřeno 2026-09-10 — a hypotéza byla ŠPATNĚ.** `bg_cache` se nerozpadala;
  špatně se **četla**. Příčina byla čtecí cesta FMC (`ReadPipeDelay = 0` + nikdy nezapnutá
  I/O kompenzační cela), ne retence SDRAM. Po opravě `membench` **0 chybných bitů**,
  `bgcheck` čistý, displej po power-cyklu v pořádku.
  🔑 Nástroj `bgcheck` z tohoto nálezu byl přesto klíčový — ukázal, že se rozpadá i paměť,
  na kterou `membench` nevidí, čímž vyloučil „je to jen scratch region“. **Ale jeho verdikt
  neumí odlišit „paměť se změnila“ od „čtení je nespolehlivé“** — to je teď v `CLAUDE.md`
  napsané u něj i u kroku 1. Viz L-0011.

---

## Co bylo zkontrolováno a je v pořádku

**C. MPU, cache, umístění bufferů**
- **MPU se konfiguruje PŘED zapnutím cache:** `main.c:211` `MPU_Config()`, teprve `:217`
  `SCB_EnableICache()` a `:220` `SCB_EnableDCache()`. Správné pořadí.
- **Všechny čtyři oblasti jsou mocnina 2 a přirozeně zarovnané** (ověřeno výpočtem):
  R0 `0xC0000000`/4 MB, R1 `0xC0400000`/4 MB (`0xC0400000 = 0x301 × 0x400000`),
  R2 `0x38000000`/64 kB, R3 `0xC1000000`/8 MB (`= 0x182 × 0x800000`). **Žádné dvě se nepřekrývají.**
- **Atributy odpovídají tomu, kdo do paměti sahá:** R0 framebuffery **Write-Through** (LTDC čte
  přímo ze SDRAM, WT nemá dirty řádky → invalidace po DMA2D je bezpečná), R1 scratch a R3 log
  **WBWA** (čte jen CPU), R2 IPC **non-cacheable + shareable**.
- **`d2d_inval()` je bezpečná díky volbě WT** (`prim_stm32_hal.c:94-100`): invalidace nezarovnaná
  na 32 B by na Write-Back oblasti mohla zahodit sousední dirty data, na WT není co ztratit —
  a přesně to komentář u funkce říká. Cíl DMA2D je framebuffer v R0.
- **L-0001 (DMA buffer v DTCM) je splněno konstrukcí:** linker do `DTCMRAM` **nelinkuje žádnou
  sekci** — `.data`/`.bss`/heap/stack jdou do `RAM_D1` (AXI SRAM, `0x24000000`). Ověřeno v mapě:
  `.bss` končí na `0x2402C408`.
- **Rezerva v RAM_D1 je velká:** `.bss` končí `0x2402C408`, `_estack = 0x24080000`
  → **~343 kB** volných pro stack a heap.

**B. Sdílená paměť a dvě jádra**
- **Všechny tři adresní kontroly pro ETH na CM4 vycházejí** (z obrazu CM4):
  `DMARxDscrTab` = `30040000`, `DMATxDscrTab` = `30040060`, `memp_memory_RX_POOL_base` = `300400c0`
  (systémové adresy pro DMA), `ram_heap` = `100283f0` (záměrně v aliasu CM4; TX cestu překládá
  `eth_dma_addr()`).
- **CM4 nekonfiguruje MPU ani cache** — a je to v pořádku: Cortex-M4 na H7 **nemá D-cache**,
  takže koherence IPC stojí na non-cacheable oblasti na straně CM7 (R2) a na `__DMB()`.
  Ověřeno grepem: v `CM4/Core/Src/main.c` není `MPU_Config`, `HAL_MPU_Enable` ani `SCB_Enable*Cache`.
- **`.ipc_shared` je vyhrazená celá D3 doména** (`FLASH.ld:228-236`, `>RAM_D3`), adresa `0x38000000`
  se shoduje s MPU oblastí 2.

**F. Linker a vektory**
- **Rozvrh 32 MB SDRAM je v linkeru zapsaný celý** (`FLASH.ld:66-78`) včetně 12 MB rezervy
  a shoduje se s MPU oblastmi i s obrazem. Toto je v projektu **vzor, jak takovou mapu držet.**
- **`.measlog` má vlastní MEMORY region**, takže hladový wildcard `*(.sdram*)` ho nemůže spolknout
  a linker hlídá přetečení. Kontrola v kódu je navíc dvojí: `sdram_log.c:173` porovnává
  `_emeaslog − _smeaslog` proti `SDRAM_LOG_BYTES`.
- **`USER_VECT_TAB_ADDRESS` je záměrně zakomentovaný**
  (`system_stm32h7xx_dualcore_boot_cm4_cm7.c:93`), takže `SCB->VTOR` nenastavuje software ani na
  jednom jádře a boot řídí option byty (`BOOT_CM7_ADD0` / `BOOT_CM4_ADD0`). Že remap funguje,
  je doložené provozem: obě jádra obsluhují přerušení a CM4 běží z banky 2.
- **`HAL_MPU_ConfigRegion()` vrací `void`** (`stm32h7xx_hal_cortex.h:312`), takže čtyři volání bez
  kontroly návratové hodnoty **nejsou** porušením `L-0003`.

**Kontrola proti `LESSONS.md`:** L-0001 ✓ (nic v DTCM), L-0002 ✓ u DMA2D (WT), ⚠️ viz F-0011,
L-0003 ✓ (`HAL_MPU_ConfigRegion` je void), L-0004 — `d2d_wait()` má guard `2000000` iterací, ✓,
L-0005 ✓ (tento běh kód nemění), L-0006 ⚠️ viz F-0010, L-0007 ✓ (netýká se), L-0008 ⚠️ viz F-0009,
L-0009 ✓ (netýká se), L-0010 ✓ (žádná změna kódu, tedy není co ověřovat na HW).

## Nezkontrolováno / omezení tohoto běhu

- **Bez HW.** F-0011, F-0012 a F-0013 jsou označené `HYPOTÉZA` a mají u sebe konkrétní způsob
  ověření na desce.
- **Nezarovnané přístupy do `.sdram` (F-0012) nebyly dohledány vyčerpávajícím způsobem** — bylo
  by potřeba projít celou vykreslovací cestu `libprim`/`libui`, což je rozsah dalšího modulu.
  Audit doložil jen to, že ta data v Device paměti **leží**.
- **Neposuzován obsah `RAM_D2` na straně CM7:** linker tam nic neumisťuje, ale `membench` a UART
  `ram write` do `0x30001000` píší absolutní adresou. Patří to k auditu `membench`.
- **`CM4/STM32H757BITX_RAM.ld` a `CM7/STM32H757BITX_RAM.ld` neprojité** — v buildu se nepoužívají
  (staví se `*_FLASH.ld`); pokud se někdy použijí, musí projít týmž auditem.
