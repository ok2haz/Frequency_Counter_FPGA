# Spolupráce na projektu — workflow

Tenhle projekt má **specifické riziko: STM32CubeMX regeneraci.** `Generate Code`
přepisuje hand-tuned konfiguraci (DSI, GPIO, init, Drivers) a dva současné regeny
= merge peklo v generovaném kódu. Workflow je kolem toho postavený.

## 0) Než začneš — přečti centrální pravidla
- **`Frequency_Counter_STM32H757/CLAUDE.md`** = bible projektu (architektura, hodiny,
  DSI/displej, FPGA protokol, RTC, ADC, FreeRTOS tasky, hard-won konfigurace).
- **`Frequency_Counter_STM32H757/CUBEMX_CHECKLIST.md`** = co ověřit po každé regeneraci.
- Používáš-li **Claude Code**, čte `CLAUDE.md` automaticky → držíš stejnou kulturu jako druhý.

## 1) ⚠️ Pravidlo „IOC owner" (nejdůležitější)
**Jen JEDEN člověk pouští CubeMX `Generate Code`** a commituje `.ioc` + generované
soubory. Druhý pracuje **výhradně v USER CODE blocích / vlastních `.c` modulech**
(regen-safe pattern, který projekt používá).

Když potřebuješ novou periferii / změnu v `.ioc`:
1. Napiš IOC ownerovi.
2. Owner ji zaregeneruje na samostatné větvi, projde `CUBEMX_CHECKLIST.md`, **mergne první**.
3. Ty si pak svou větev **rebasneš** na novou main.

## 2) Oba rovnocenní — bez pevného vlastnictví
**Oba můžete sahat na cokoli.** Per-modulové vlastnictví záměrně neřešíme.
Aby ale práce nekolidovala, drž se dvou věcí:

- **Neformálně:** než začneš větší práci, řekni druhému, na čem děláš (ať neoba
  saháte na ten samý soubor). Projekt je hezky rozdělený (`libprim`/`libui`/`app`
  vs. `fpga_freq`/`gps`/senzory) — využijte to jako přirozené hranice.
- **⚠️ Tvrdě (jediné pravidlo):** **sdílené / CubeMX-generované soubory** vyžadují
  koordinaci a review toho druhého — viz níže.

**Sdílené soubory** (kde dva současné zásahy bolí): `H757_LED.ioc` + generované,
`main.c` init, `freertos.c` (task table + globály), `gpio.c`, `dsihost.c`, `ltdc.c`,
`fmc.c`, `*.ld`, `Drivers/`, `CLAUDE.md`, `CUBEMX_CHECKLIST.md`.
Na ně `.github/CODEOWNERS` přidá **oba jako reviewery** → PR na sdílený soubor
musí schválit ten druhý (autor sám sebe neschválí). Ostatní soubory owner nemají
→ měníte je volně.

## 3) Větve a PR
- `main` = **vždy buildovatelný a stabilní**. Na GitHubu zapnout branch protection
  (require PR + review od Code Owners) → vynutí pravidla 1 a 2 samo.
- Práce na **feature větvích** (`feat/...`, `fix/...`), pak PR.
- PR na **sdílené soubory** = povinný review druhého. Na vlastní modul rozhoduje vlastník.
- Před PR si **rebasni na aktuální main** (ne merge commit), ať je historie čistá.

## 4) Konce řádků / formát
- **`.gitattributes` to řeší** (repo = LF). Varování `CRLF will be replaced by LF`
  při commitu je **normální** (CubeMX píše CRLF, git normalizuje na LF) — neřeš ho.
- `.editorconfig` sjednocuje charset (UTF-8 kvůli diakritice), konce řádků, závěrečný
  newline. **Odsazení: drž styl sousedního kódu** (generované = 2 mezery, app/ = 4, některé
  tasky = taby) — záměrně se nereformátuje.

## 5) Commity
- Stručná zpráva, klidně česky (jako stávající historie).
- **Trailer: `Co-Authored-By: Reserved <reserved@local>`** — nikdy ne Claude/AI atribuce.
- Commituj/pushuj jen vlastní hotovou práci; sdílené soubory přes PR.

## 6) Build / test
- Build i flash **jen z STM32CubeIDE** (CM7 → Build → Run, config CM7). Toolchain
  není v CI → každý ověří lokálně + ideálně na HW před PR (viz PR checklist).
- **První build po klonu / na novém stroji → nejdřív §8** (import projektu). Špatný
  import = stovky `undefined reference` na `HAL_*`/`os*` při linku.

## 7) ⚠️ Dual-core flash — NAFLASHUJ OBĚ JÁDRA
STM32H757 = **dvě jádra ve dvou flash bankách**. Displej + veškerá logika běží na
**CM7 (bank1 `@0x08000000`)**, ale CM7 na startu čeká, až nabootuje **CM4
(bank2 `@0x08100000`)** — dual-core HSEM/D2 handshake. **Když naflashuješ jen CM7,
CM4 v bank2 nenaběhne** → dřív to znamenalo tichý zásek v `Error_Handler` **před**
inicializací displeje = **černá obrazovka** (klasické „jednomu jde, druhému ne").

**Firmware je od teď odolný** (CM7 pokračuje degradovaně, ukáže „CM4 (D2): ABSENT"
v System Health + amber SYS pill + UART `[BOOT] CM4 nenabehl`), ale **správně je
flashnout obě banky:**

- **CubeIDE:** spusť build+flash pro **CM7 i CM4 projekt** (dvě Run konfigurace), ne jen CM7.
- **CubeProgrammer:** načti oba `.elf` (CM7 `@0x08000000`, CM4 `@0x08100000`) → Program.
  Nebo naflashuj jeden **combined image** — viz `tools/make_release_image.ps1`.
- **Option bytes** (CubeProgrammer, tab OB): **`BCM7=1` A `BCM4=1`** (boot obou jader),
  `nSWBOOT0`/`BOOT_ADD0/1` na defaultech. Repo spoléhá na hardwarový boot obou jader.

**Diagnostika tmavého displeje:** připoj UART (USART1, 115200 8N1) a pošli `ping`.
- ticho → CM7 nedojel = dual-core boot (option bytes / neflashnutá bank2).
- `pong` → CM7 běží, problém je panel/backlight (I2C4/ATTINY — viz `CLAUDE.md`).

## 8) ⚠️ Import projektu do CubeIDE — JAK SPRÁVNĚ (jinak nejde slinkovat)

**Příznak špatného importu** (typicky po čerstvém klonu na jiném stroji): projekt se
přeloží, ale **link spadne na stovkách chyb** typu

```
undefined reference to `HAL_GPIO_Init' / `osDelay' / `vTaskDelay' / `xPortGetFreeHeapSize'
(HAL_GetTick): Unknown destination type (ARM/Thumb) in ./Core/Src/....o
dangerous relocation: unsupported relocation
```

**Příčina:** CM7 (i CM4) projekt **neobsahuje HAL/FreeRTOS/USB zdrojáky fyzicky** — má na
ně **~55 „linked resources"** (`CM7/.project`) s cestou **`PARENT-1-PROJECT_LOC/...`**,
tedy *„o úroveň výš od projektu"*:

```
CM7/Drivers/stm32h7xx_hal_gpio.c  ->  ../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
CM7/Middlewares/FreeRTOS/tasks.c  ->  ../Middlewares/Third_Party/FreeRTOS/Source/tasks.c
```

Složky `CM7/Drivers/` a `CM7/Middlewares/` jsou proto **prázdné** (nejsou ani v gitu) —
jsou to jen virtuální kontejnery pro ty odkazy. **Když se CM7 naimportuje samostatně
nebo se zkopíruje do workspace, `PARENT-1` ukáže jinam → odkazy se nerozlousknou →
HAL a FreeRTOS se vůbec nepřeloží → každý `HAL_*`/`os*`/`vTask*` symbol je undefined.**
(Hlášky `Unknown destination type (ARM/Thumb)` a `dangerous relocation` jsou jen následek:
linker u nedefinovaného symbolu neví, jestli je cíl ARM nebo Thumb.) `Core/Src/*.c` se
přeloží bez potíží — ty v CM7 leží fyzicky, což ten obrázek dokresluje.

**Správný postup:**
1. Smaž projekty z workspace (File → Delete, **NEzaškrtávat** „Delete contents on disk").
2. **File → Import → General → Existing Projects into Workspace**
3. **Select root directory:** složka **`Frequency_Counter_STM32H757`** (ta s `.ioc`) —
   **NE** `CM7`.
4. ✅ **zaškrtnout „Search for nested projects"** → musí najít **tři** projekty:
   `H757_LED`, `H757_LED_CM4`, `H757_LED_CM7`.
5. ❌ **NEzaškrtávat „Copy projects into workspace"** — kopie rozbije `PARENT-1` odkazy
   (tohle je nejčastější příčina).
6. `H757_LED_CM7` → Build.

**Kontrola, že je import dobře:** v Project Exploreru pod
`H757_LED_CM7 → Drivers/STM32H7xx_HAL_Driver` musí být soubory s **ikonkou odkazu (šipka)**.
Prázdná složka = odkazy se nerozlouskly → zopakuj import dle bodů výše.

**Po `git pull` do už otevřeného projektu:** nové soubory (např. `calib.c`, `syscfg.c`)
se do buildu dostanou až po **F5 (Refresh)** na projektu — jinak linker hlásí undefined
reference na jejich funkce.
