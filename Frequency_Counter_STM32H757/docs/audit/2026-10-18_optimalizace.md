# Analýza optimalizace přístroje jako celku (2026-10-18, v0.30.1 na desce, v0.30.3 v repu)

Zdroje: `docs/PAMET_FUNKCI.md` (mapa linkeru), `stats`, `status full`, `tdc`, `regr` z běžící desky (uptime 5 476 s), `ab_test/MANIFEST.md`. Nic se tímto dokumentem neměnilo.

## 1. Zdroje podle hrdla (od nejtěsnějšího)

| # | Zdroj | Stav | Rezerva | Hrdlo? |
|---|---|---|---|---|
| 1 | FPGA logika (CLS) | 85 % (strop brány), registry 65 %, rezerva setup 0,060 ns | prakticky žádná | **ANO** – blokuje jakoukoli novou funkci ve FPGA |
| 2 | TDC řetěz | 268 tapů, max tap 248; rezerva A 16 (5,9 %), B 14 (5,2 %) | ~15 tapů | **ANO** – fyzikální, zhoršuje se s teplotou |
| 3 | Halda FreeRTOS (CM7) | 32 kB, volných 5,7 kB, min-ever stejně | → 48 kB od v0.30.3 (+16 kB) | vyřešeno, čeká na flash |
| 4 | DTCM_APP | 51,1 / 64 kB | 13 kB | ne |
| 5 | CM4 RAM (SRAM2) | 86,8 / 128 kB (68 %), z toho `s_hconn` 41 kB (5 slotů × 8,4 kB) | 41 kB | ne, ale web je největší spotřebitel |
| 6 | Flash CM7 / CM4 | 739 / 1024 kB (72 %), 301 / 1024 kB (29 %) | 285 / 723 kB | ne |
| 7 | CPU CM7 | 10–11 % (UiTask 5 %, I2C4 2 %, IDLE 90 %) | 89 % | ne |
| 8 | SDRAM | 1 107 kB z 4 096 kB + log 8 MB | velká | ne |
| 9 | Stacky tasků | nejmenší: FpgaTask 628 B, IDLE 428 B, DacTask 808 B | úzké | **okrajově** |
| 10 | W25Q datalog | 243 277 / 696 960 záznamů (35 %) | ~45 dní kruhu | ne |

## 2. Co optimalizovat – podle poměru přínos / riziko

### A. Má smysl a je levné
1. **Halda 48 kB** – hotovo (v0.30.3). Až se po flashi potvrdí `Heap: free ≈ 22 kB`, má smysl zvednout i FpgaTask (+512 B) a DacTask; bez měření ne.
2. **CM4 `s_hconn` 41 kB** – 5 spojení po 8,4 kB. Vyplatí se jen kdyby došla RAM CM4 (dnes 41 kB volných) – **nechat**.
3. **Fonty 250 kB flash** – největší jednotlivá položka flash (34 %). Rezerva flash je 285 kB, takže **není důvod sloučit** `mono_14`/`mono_16` (kvalita).

### B. Má smysl, ale vyžaduje měření
4. **Zamítání regrese 2,5 %** (`regr`: použito 85 454, zamítnuto 2 174 z 87 628 oken). Zamítnutá okna spadnou na dvoubodový odhad (σ asi 4× horší), takže nesou téměř celý šum čítače (8 % oken = 94 % rozptylu, viz analýza 10h logu). Zjistit PROČ (mez konzistence 50 ppb × min. značek 8 – pro 10 MHz a 0,25 s okno?). Je to **nejlevnější cesta ke zlepšení měření**, nezabírá logiku FPGA.
5. **Kanál B bez regrese** (σ značky ≈ 125 ps vs. A ≲ 5 ps). Do FPGA se to nevejde (CLS 85 %); jedině **dopočet na STM32** z už přenášených značek, pokud je rámec nese (ověřit ve `parse_data`, kolik značek B přijde).
6. **IDLE stack 428 B / DacTask 808 B / Tmr Svc 904 B** – hlídat při přidávání funkcí; po zvětšení haldy lze zvětšit o 256 B každý.

### C. Nedělat (cena > přínos)
7. **FPGA logika:** nic nepřidávat, dokud se neuvolní místo. Žádný z dnešních požadavků (podlaha, ADRET, GPS koax) ji nepotřebuje.
8. **CPU:** při 10 % zátěži je jakákoli optimalizace výkonu zbytečná; pozor jen na pravidlo „spin > 10 ms“ v hlídaných taskech.
9. **SPA 195 kB:** v CM4 flash je dost místa (723 kB). Minifikace by zkomplikovala pravidla editace (žádné `"`/`\`), úspora zanedbatelná.

## 3. Měření, kde "optimalizace" znamená přesnost, ne zdroje
- Podlaha čítače u kanálu A je **≥ 4× lepší než model 105 ps** → `FREQ_TDC_PS`/`MP_TDC_PS` pro A by šla zpřesnit po ověření s tichým zdrojem (Rb / druhé OCXO). Do té doby necháváme konzervativní hodnotu (poslední platná číslice nezměněna).
- Nejistota reference ve stavu LOCK je odhad z tabulky, než se nasbírá 30 min LOCK (`ref_meas=0` → 1).

## 4. Pořadí doporučených kroků
1. Flash v0.30.3 + power-cyklus; ověřit `Heap` (≈ 22 kB), `stats` stacky, podlahu Allanu.
2. Zjistit příčinu zamítání regrese (bod 4) – bez změny FPGA.
3. Po 30 min LOCK ověřit `ref_meas=1`.
4. Zapsat výsledek měření s tichým zdrojem; teprve potom zvažovat změnu `MP_TDC_PS`.
