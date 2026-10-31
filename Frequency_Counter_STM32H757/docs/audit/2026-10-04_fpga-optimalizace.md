# FPGA strana: co optimalizovat a upravit (2026-10-04)

> Navazuje na `2026-10-04_presnost-analyza.md` (CM7 trojice hotova). Tady FPGA RTL.
> Zdroje GW1NR-9: **Logic 47 %, Register 38 %, CLS 64 %, BSRAM 20 %** → místo je.
> ⚠️ FPGA změny v tomto projektu selhaly 2× (0x040E) — každou ověřovat živým `tdc hist`.

## A. Co FPGA UŽ umí, ale CM7 to NEVYUŽÍVÁ (nízké riziko, zisk hned)

### A1. Konfigurovatelné okno 100 ms / 250 ms / 1 s — NEPOUŽITO
`spi_app.v:17`: SET_CONFIG `0x01` arg 0/1/2 = 100 ms / **250 ms (default)** / 1 s.
CM7 (`fpga_freq.c`) posílá SET_CONFIG jen pro kalibraci (`0x02`), okno nenastavuje → běží 250 ms.
- **Zisk:** 1 s okno = **4× lepší syrové rozlišení** na zdroji (TDC chyba fixní, dělí se delším oknem) + 4× méně SPI rámců.
- **Cena:** pomalejší obnova (1 měření/s místo 4).
- **Jak:** přidat `fpga_freq_set_window(0..2)` (SET_CONFIG 0x01) a napojit na CM7 `gatestat` —
  pod ~1 s nechat 250 ms okno a akumulovat v CM7, od 1 s přepnout FPGA okno. **Žádná RTL změna**, jen použít.

### A2. Gap-free window stream — NEPOUŽITO
`spi_app.v:9`: rámec nese **2 poslední uzavřená okna** (`window[0..1]`, +win_seq/edges/dt_ns),
a okno N+1 **začíná hranou, kterou skončilo okno N** → souvislé, bez mrtvé doby.
CM7 čte jen `win_count` (abs 65), ale **data oken ignoruje** (`fpga_freq.h:60` „dnes nevyužito").
- **Zisk:** STM může sčítat `Σedges / Σdt` přes libovolně dlouhé τ **bez mrtvé doby** → čistý
  Allan na VŠECH τ (dnes se akumuluje per-rámec s modelem; window stream je exaktní a souvislý).
- **Cena:** úprava CM7 akumulátoru (`fpga_acc`), aby bral dvojici oken z rámce. Střední práce, nízké riziko (čistě CM7).

## B. FPGA RTL změny (vyšší riziko, nutná živá iterace)

### B1. Giant bin kód 134 (19,5 % hran) — HLAVNÍ, ale mechanismus NEJISTÝ
- Jednoduchá hypotéza „pozdní spouštěč" je **vyvrácena** (0x040E to sladil, σ se nezlepšila).
- Možné příčiny (nerozlišené): (a) **bublina** v teploměru (nemonotónní 111010…) → dekodér
  `lead32` bere první 0 špatně; (b) **metastabilita** vzorkovacích FF u hrany hodin; (c) **pomalý
  stupeň** na přechodu řad GW1NR-9 (placement) → hrany se tam hromadí.
- 🔑 **První krok NENÍ fix, ale INSTRUMENTACE** (nízké riziko): FPGA zachytí **surový teploměr**
  pro událost s max kódem a vystaví ho (rozšířit CAL report / debug rámec). Z pár vzorků se pozná:
  - bublina (nemonotónní vzor) → **bublinově tolerantní dekodér** (počítat jedničky, ne vedoucí; Wave-Union),
  - metastabilita (náhodný vzor) → synchronizér / jiné vzorkování,
  - pomalý stupeň (čistý saturovaný vzor) → re-place řetězu / přeskočit vadný tap v kalibraci.
- **Teprve podle toho** cílená oprava. Odhad po opravě: σ okna ~54 ps (16×), žádné zahazování (cesta A padá),
  čistá i syrová data. ⚠️ Vysoké riziko/iterace — dělat samostatně, ne na konci dlouhého sezení.

### B2. Code-density kalibrace — drobná vylepšení
- Víc kalibračních událostí / lepší statistika na bin (dnes 2²⁰). BSRAM je z 20 % → místo na jemnější tabulku.
- Zahrnout teplotní rekalibraci i do FPGA (dnes ji spouští CM7 — viz feat teplotní rekal).

### B3. Délka/STRIDE řetězu
- Dnes 512 ALU, STRIDE 2 (256 tapů), křemík ~32 ps/tap → ~16,6 ns pokrytí (perioda 10 ns, rezerva OK).
- STRIDE 1 (vzorkovat každý stupeň) = 2× jemnější kód, ale 2× víc FF (reg 38 % → ~76 %, ještě se vejde)
  a 2× delší dekodér. Zisk jen když giant bin zmizí (jinak ho to nevyřeší).

### B4. Dvoukanál A−B
- CH_B dnes nebuzen (chybí vstupní modul). Po osazení: stejný signál na A i B → **rozdíl vyruší drift
  reference** → nezávislý odhad σ TDC + offset kanálů. RTL pro dva kanály UŽ existuje (`meas_dt_b_ps`).

## C. Pořadí (přínos / riziko / práce)

| # | opatření | strana | přínos | riziko |
|---|---|---|---|---|
| A1 | použít 1 s okno (SET_CONFIG 0x01) | CM7 | ×4 syrové rozlišení, méně SPI | nízké |
| A2 | gap-free window stream do akumulátoru | CM7 | čistý Allan všech τ, bez mrtvé doby | nízké |
| B1a | **instrumentace giant binu** (dump teploměru) | FPGA | odemkne diagnózu | nízké |
| B1b | cílený fix giant binu dle diagnózy | FPGA | ×16 σ, čistá data | vysoké, iterace |
| B4 | A−B dvoukanál | FPGA+HW | nezávislý odhad / offset | HW |

🔑 **Doporučení:** nejdřív **A1 + A2** (čistě CM7, hned a bezpečně vytěží, co FPGA už nabízí),
pak **B1a instrumentace** (bezpečně zjistí příčinu giant binu), a **až s diagnózou** B1b fix.
Slepé RTL změny giant binu (B1b bez B1a) = opakování 0x040E.
