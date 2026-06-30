<!-- Stručně: CO a PROČ. Detaily níž. -->

## Co PR dělá


## Checklist
- [ ] Buildnul jsem z **STM32CubeIDE** (CM7) bez chyb (toolchain není v CI).
- [ ] Ověřeno na **HW** (nebo popsáno, proč ne).
- [ ] Držel jsem **regen-safe pattern** (vlastní logika v USER CODE / vlastních `.c`, ne v generovaných sekcích).

## ⚠️ Sáhl jsem na CubeMX / sdílené?
- [ ] **NE** — žádný `.ioc` / `main.c` init / `gpio.c` / DSI / linker / Drivers.
- [ ] **ANO** — pak:
  - [ ] Jsem domluvený jako **IOC owner** (nikdo jiný teď neregeneruje).
  - [ ] Prošel jsem **`CUBEMX_CHECKLIST.md`** a ověřil rizikové hodnoty (DSI Burst+RGB565, PB12 High, PA10 pull-up, defaultTask stack 384, ADC ClockPrescaler, …).
  - [ ] Aktualizoval jsem `CUBEMX_CHECKLIST.md` / `CLAUDE.md`, pokud se konfigurace změnila.

## Dotčené moduly / oblasti


## Pozn. pro review

<!-- Commity ukončuj trailerem:  Co-Authored-By: Reserved <reserved@local>  (nikdy Claude) -->
