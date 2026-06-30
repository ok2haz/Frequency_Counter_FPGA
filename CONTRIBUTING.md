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

## 2) Rozdělení modulů (minimalizuje kolize)
Projekt je čistě rozdělený — držte se hranic:

| Oblast | Soubory |
|---|---|
| **Display / UI** | `CM7/libprim/`, `CM7/libui/`, `CM7/app/` (+ `screens/`, `hal/`) |
| **FPGA / GPS / senzory** | `fpga_freq.c`, `gps.c`, `si5356.c`, `ads1115.c`, `freertos_task_sensors.c`, `freertos_task_fpga.c` |
| **⚠️ Sdílené (koordinovat / PR review)** | `H757_LED.ioc` + generované, `freertos.c` (task table + globály), `main.c` init, `*.ld`, `Drivers/`, `CLAUDE.md`, `CUBEMX_CHECKLIST.md` |

Vlastnictví je vidět i v `.github/CODEOWNERS` (doplň GitHub handle kamaráda).

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
