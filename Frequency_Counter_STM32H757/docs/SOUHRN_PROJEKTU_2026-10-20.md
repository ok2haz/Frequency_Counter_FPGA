# GPSDO + reciproční čítač — souhrn rozsahu a odhad pracnosti (2026-10-20)

## Stav
- **Firmware STM32H757** (CM7 + CM4) v0.31.0, **FPGA** Gowin GW1NR-9 FW 0x0425 (simulace + P&R brána PASS, na desce neověřeno).
- Funkce: GPSDO smyčka (FLL/PLL, DAC OCXO, holdover), reciproční čítač s carry-chain TDC a regresí, ADEV/MDEV/HDEV, displej 800×480 s dotykem a enkodérem, web + SCPI přes Ethernet, datalog (W25Q, SD), diagnostika.
- Historie: 796 commitů od 2025-11-22, 328 položek TODO/STATUS, 182 zapsaných lekcí.

## Rozsah kódu (řádky včetně komentářů)

| část | řádků |
|---|---|
| STM CM7 jádro (Core, část generuje CubeMX) | 35 600 |
| STM UI (app, libui, libprim) | 19 600 |
| STM CM4 (Ethernet, web, SCPI) | 9 400 |
| FPGA RTL (Verilog) | 3 200 |
| FPGA simulace (testbenche) | 2 400 |
| nástroje (Python, PowerShell, JS) | 8 200 |
| **kód celkem** | **≈ 78 400** |
| (generované fonty, nezapočteno) | 16 200 |
| dokumentace (Markdown) | 72 700 |

Odhad vlastního ručně psaného kódu bez generovaného a bez komentářů: **≈ 45 000 řádků**.

## Odhad pracnosti týmu 5 programátorů
Metoda COCOMO II (pro 45 000 řádků; pro vestavěné systémy s FPGA je spíš horní polovina rozsahu):

| model | člověkoměsíců | 5 lidí |
|---|---|---|
| jednoduchý projekt (organic) | ~130 | ~26 měsíců |
| střední (semi-detached) | ~210 | ~42 měsíců |
| vestavěný systém (embedded) | ~350 | ~70 měsíců |

**Realistický odhad: 2,5–3,5 roku** pro tým 5 lidí (vývojář FW, FPGA, HW/měření, UI/web, test). Nezahrnuje návrh a výrobu desek.

## Náklady (ČR, 2026)

| položka | měsíčně |
|---|---|
| senior vývojář (hrubá mzda ~100 000 Kč + odvody 33,8 %) | ~134 000 Kč |
| 5 vývojářů | ~670 000 Kč |
| režie (vybavení, licence, měřicí technika, kancelář) ~30 % | ~200 000 Kč |
| **tým celkem** | **~870 000 Kč/měsíc** |

| doba | náklad |
|---|---|
| 30 měsíců | ~26 mil. Kč |
| 42 měsíců | ~37 mil. Kč |

Při dodavatelské sazbě 1 200–1 600 Kč/h vychází **35–55 mil. Kč**.

## Výhrady
- Počet řádků je hrubé měřítko. Komentáře tvoří velkou část a část kódu generuje CubeMX.
- Mzdy a sazby jsou orientační odhad pro ČR, ne nabídka.
- COCOMO nezohledňuje měření a ladění na hardwaru, které je u metrologického přístroje velká část práce.
