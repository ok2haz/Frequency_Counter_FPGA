# Zpráva: optimalizace FPGA v3 (FW 0x0506) beze změny funkce — časování a plocha

> **Původ:** zpráva externí instance (`exchange/ukol_2026-10-24_fpga_opt_vystup.zip`, ZPRAVA.md), přenesená beze změny
> od kapitoly 1. Kapitola 0 je integrace do repa hlavní instancí (2026-10-24).

## 0. Integrace do repa (hlavní instance, 2026-10-24)

**Commity** (větev `audit/2026-09-09-hodiny-pwr`, nepushnuto):

| commit | obsah |
|---|---|
| `acfaa97` … `d1a4976` | řada 0001..0013 beze změn (`git apply --index`, jeden commit na patch; `sim/run_ps1_doplnek.txt` necommitován) |
| `d37ed5c` | fix N1 – maska prvních 3 slov PCLK v `pps_ides3` (`wn`), `u_ipps/wn*` ve false_path (L-0230) |
| `a3f2853` | fix N2 – `fa <= 0` při hrubé hraně v `pps_cap3` (L-0231) |
| `b4fe908` | test `tb_pps_v3` (N1/N2 výchozí kontroly), `mut_pps_v3.py` – kotvy na tvar `pps_cap3` po řadě (`pub/pub_b`, `fok/fok_b`) |
| `83f1451` | test `tb_top_v3`: B = 13,300161 MHz, mutace `ts_valid` o 2 takty dřív (režim 8), ekvivalentní o 1 takt, `holdinv` v `run.ps1`, jména adresářů mutací (L-0228, L-0229) |

**Kontroly při integraci:**
- Výsledné `src/app_v3.v`, `top_v3.v`, `v3_core.v` a nové soubory v `sim/`, `tools/` po řadě = `soubory/` z balíčku (bez rozdílu
  konců řádků).
- Mutace `tb_top_v3` nad stavem PŘED řadou (Icarus 12, mut6, 38 mutací): 35 zachyceno, nezachyceny tytéž, které uvádí kap. 4.3
  (`FIFO hloubky 1`, `znacka: ts_valid o takt driv`) + `1PPS: seq > 1 vzdy` (ekvivalentní, kap. 4.3). **Hypotéza z POZOR bod 9
  („Icarus 12 je chytí") se nepotvrdila** – jde o mezeru testu: po registru `tvd` (FW 0x0506) kanál B 16,1 MHz neplní druhou
  položku FIFO. Ověřeno samostatně: B 13,300161 MHz → základ PASS, FIFO hloubky 1 FAIL; `ts_valid <= tv_y` (o 2 takty) při
  B 16,1 MHz FAIL.
- `mut_pps_v3.py` nad sloučeným RTL (řada + N1 + N2): 20 zachyceno + 3 ekvivalentní, bez mutace PASS. Čtyři kotvy se po řadě
  rozešly s kódem (L-0224) a jsou přepsané; mutace nad zdvojenými registry (`fok/fok_b`, `pub/pub_b`) zasahuje obě kopie.

**Výsledky úplné verifikace** (Icarus 12, Windows, `sim_par7`): 8× `topv3*`, `holdinv`, `phyv3`, `ppsv3` – 11/11 PASS.
Mutace `tb_top_v3` nad tímto stavem zrušeny (nahradil ho FW 0x0507, mutace poběží nad finálním RTL).

**Změřeno v P&R** (8 sestavení, `tools/pnr_seeds.py`, L-0233): CLS 3 753–3 824 (−31 proti 3 809, odhad byl −100 až −140),
ALU54D 8 z 10 (DSP kroky fungují), setup 2/8, medián −0,298 ns (bez zlepšení proti 0x0506). Pokračování a vydaný bitstream:
`2026-10-24_fw0507-casovani.md`.

**Nepokryto:** skutečné CLS, rezerva časování a počet ALU54D (8 z 10) – zkušební bitstream
`python tools/fpga_release.py --v3 --build --test-cls 90 --tries K`; deska (power-cyklus). ⬜ neověřeno na HW.

Úkol `ukol_2026-10-24_fpga_opt`, zpracováno 2026-10-24 na macOS (Apple M5 Pro, 15 jader). Základ: `base/` (commit
`1d76707` + neuložené změny FW 0x0506), součty beze změny (`HOTOVO.txt`). Gowin tu není, proto se CLS a rezerva
neměří. Měří se náhradní metriky yosysu a shoda chování se dokazuje stopou výstupů.

## 1. Shrnutí

- **Řada 8 optimalizačních kroků** (`git_patche/` 0002–0008, 0010) + 5 testovacích commitů (stopa, mutace,
  invarianty, cílený test). Všechny kroky jsou **přesné**: stejné chování v každém taktu, žádná odchylka podle
  kap. 2 bod 7, **žádná nová výjimka SDC**, FW 0x0506 beze změny.
- **Časování (5 kroků):** nejhorší cesty z tabulky kap. 1 zadání se zkrátily o úroveň logiky nebo o fan-out:
  - `win → nofine`: cesta zmizela, do `nofine` je 3 místo 4 úrovní a síť povolení s fan-outem 130 je rozdělená
    na 2 sítě;
  - `lw_n_q → lut DI`: kopie registrů u každého kanálu;
  - `tvd → t0 (CE)`: 5 místo 7 vstupů a zvláštní CE pro `x0`;
  - `ohA → wa_a`: fan-out bitu 32 → 16;
  - `cal_valid/busy_ra → hold_a_s`: 2 → 1 úroveň.

  Cesty `dd_r → d_l` a `DSP → wa_a` zůstávají (přesná úprava by stála 68 ALU nebo 64 FF, rozbor v kap. 5).
- **Plocha (3 kroky):** 4 čítače do volných DSP (ALU54D: 4 → 8 z 10):
  - `tick_hi`: změřeno hlavní instancí, −54 CLS;
  - `ts10` a `seq_tx` (doména 10 MHz): odhad;
  - `seq` v `pps_cap3` (výstup čte jen doména 10 MHz): odhad.

  Odhad celkem −100 až −140 CLS **[ODHAD, kromě tick_hi neměřeno]**. Kopie registrů z časovacích kroků přidávají
  ~52 FF (~+8 CLS podle L-0221).
- **Shoda chování:** stopa (každý bajt MISO/MOSI každé transakce + všechny výstupy `top_v3` s časem) je
  **bajt po bajtu shodná s `base/` ve všech 8 testech `topv3*`** na finální verzi a na každém kroku řady.
  Kontrola měřítka 4.2 (a)–(c) je doložená. Všechny testy PASS (8× `topv3*`, nový `holdinv`, `phyv3`).
  Mutace: 48 vzorů (38 původních, 3 kotvy přepsané, 10 nových): **45 zachyceno, 1 ekvivalentní, 2 nezachycené**.
  Nezachycené jsou původní `FIFO hloubky 1` a `znacka: ts_valid o takt driv`; nezachytí je ani `base/` s původním
  skriptem. Obě mění výstup a stopa je odhalí; jde o mezeru kontrol testbenche (kap. 4.3). Ekvivalentní je původní
  `1PPS: seq > 1 vzdy`.
- **Zamítnuto s rozborem** (kap. 5): `tmr` (není přesné), přestavba multiplexeru hlavičky (yosys bez úspory),
  `dX`, odstranění `tm`, zúžení FIFO, přesná saturace `d_l`, výjimka SDC pro DSP → `wa_a`.

## 2. Metodika

### 2.1 Nástroje

| | verze |
|---|---|
| Icarus Verilog | 13.0 (stable) (v13_0) — Homebrew. Hlavní stroj má 12.0 devel; testbenche nepoužívají nic nového ze SV |
| Yosys | 0.70+post (git sha1 0c11ea098be065e585a90a06e7b7da49936cf231) — Homebrew |
| Python | 3.9.6 |

Tento stroj je asi 6–12× rychlejší než hlavní: `topv3` 15 min, `topv3cal` 9 min, `topv3pw` 3–4 min (tabulka kap. 4.1).

### 2.2 Stopa (`sim/trace_v3.py`, `tb_top_v3.sv` pod `-DTRACE`)

- Každý bajt každé transakce: čas, pořadí transakce, index bajtu, MOSI, MISO. Zapisuje ho úloha `xfer`, přes
  kterou jdou všechny transakce.
- Změny výstupů `led_tx`, `fpga_ok`, `ref_ctrl`, `O_psram_*`, `IO_psram_*` (rozlišená sběrnice, tedy i řízení
  FPGA). Zapisují se přes `$fstrobe` (hodnoty na konci časového kroku); `trace_v3.py` sloučí řádky beze změny stavu.
- `bl_pwm` existuje jen s `PIN86_BL_OFF`, které žádný test nedefinuje.
- `trace_v3.py run` pustí testy z tabulky `run.ps1` (stejné soubory a `defs`) s modely `gowin_models_trace.v`.
  `trace_v3.py cmp` porovná normalizované stopy a vypíše první rozdíl (test, čas, signál, obě hodnoty).
- Testy v režimu stopy dávají PASS (`base/` i finální verze, `logy/stopy_*.txt`).

### 2.3 Determinismus modelů (`sim/gowin_models_trace.v`)

Kopie `gowin_models.v`, ve které náhodné zpoždění ALU (initial) a jitter LUT1/LUT2 (always) nebere `$urandom`.
Místo toho se generuje `$random(sd)` s **vlastním semínkem instance**: FNV-1a 32 b z `%m` bez prvního prvku (jména
testbenche). Rozdělení je stejné (střed, ±rozptyl, zlom řetězu `SIM_BREAK_IDX`). Ověřeno:
- stejná zpoždění při jiném jménu testbenche;
- různá zpoždění mezi instancemi.

### 2.4 Kontrola měřítka (4.2)

| | porovnání | test(y) | výsledek |
|---|---|---|---|
| (a) | `base` × `base`, dva běhy | všech 8 | viz kap. 4.2 (první běh s TB po 0001, druhý s finálním TB a `-DNO_OPT_INV`) |
| (b) | `base` × `base` + neškodná instance (nepoužitý registr s `initial` a nepoužitá ALU s náhodným zpožděním, vložená v `top_v3` PŘED instancemi TDC) | `topv3`, `topv3cal`, `topv3deep`, `topv3pw` | **shodné** (otisky = `base`) |
| (b′) | totéž s PŮVODNÍMI modely (`gowin_models.v` jako modely stopy) | `topv3pw` | **liší se** (`56eb40…` × `4417734…`), oba PASS → bez deterministických modelů by porovnání nedokazovalo nic |
| (c) | `base` × `base` + mutace z `mut_top_v3.py` | `B zdroj ignorovan` (režim 3, `topv3pw`); `rozdil t: bez vypujcky` (režim 0, `topv3`) | **liší se** (a test FAIL) |
| (c′) | `base` × `base` + `1PPS: seq > 1 vzdy` | `topv3` | **shodné**: ekvivalentní mutant (kap. 4.3) |

### 2.5 Náhradní metriky (`tools/yosys_v3.ys`, `tools/yosys_stat.py`)

- `synth_gowin -family gw1n` nad sadou souborů z `Counter_FPGA_v3.gprj`.
- Výstup: tabulka LUT1–4, ALU, MUX2_*, FF podle typu, řídicí sady, BSRAM, DSP, hloubka (úrovně LUT/MUX, ALU zvlášť)
  pro koncové body z kap. 1 zadání a 20 sítí s největším větvením.
- `yosys_stat.py hier` = rozpad po modulech bez zploštění. Zploštěné celkové číslo kolísá: abc9 mapuje globálně
  a drobná změna posune i nesouvisející buňky, např. LUT1 −167 a MUX −167 po změně jen v `pps_cap3`. Proto se
  kroky hodnotí po modulech.
- Yosys **slučuje kopie registrů** (ignoruje `syn_preserve`) a **neumí `syn_dspstyle`**: přírůstek FF z kopií
  a úsporu DSP neukáže, obojí je v tabulce odhadnuto ručně.
- Yosys `base/`: 3 913 LUT, 512 MUX, 1 871 ALU, 4 788 FF (Gowin: 3 681 LUT, 1 814 ALU, 4 450 FF).

### 2.6 Invarianty a cílený test

- `tb_top_v3.sv` (blok `ifndef NO_OPT_INV`) v **každém taktu** kontroluje, že každá kopie a každý předpočítaný
  registr se rovná původnímu výrazu: `wnz == (win != 0)`, `*_b == *`, `fnz == (fn != 0)`, `nfc_x == ~freeze & ~clear_x`,
  `first_b == first`, `ohAh == ohA`, kopie zápisu tabulky, `hx_x` a `hold_x_s == hold_x`. To pokrývá i stavy, které se
  do výstupů nepromítnou.
- `tb_hold_inv.sv` (~1 min) vynutí ztrátu hodin (nový náběh `boot_done`), kterou žádný test `topv3*` nevyvolá,
  a dále změnu předděličky obou kanálů a zdroje B.
- Dvě negativní varianty `hx` (bez `boot_tc`, bez `ok_s`) test chytí; druhou chytí jen on. Jsou v mutacích jako
  režim 7.

## 3. Řada kroků

Yosys = rozpad po modulech (LUT/MUX/ALU/FF) a hloubka koncových bodů (úrovně LUT); `base` → krok.

| # | patch | typ | co | yosys | koncové body | odhad Gowin | riziko |
|---|---|---|---|---|---|---|---|
| 1 | 0002 | čas | `pps_cap3`: `wnz` místo `win != 0`; povolení zveřejnění ve 2 kopiích (`pub`, `pub_b` z kopií `pend/wnz/fok/wok`) | pps_cap3 120/47/76/237 → 121/45/76/241 | `win→nofine` 3 → cesta zmizela; `*→nofine` 4→3; `*→pts_l` 4→3 | nejhorší cesta (9,4 ns, −0,38) o úroveň kratší a z registrů; fan-out 130 → 2 sítě ~67/~98 | nízké |
| 2 | 0003 | čas | `lut_wr_sync3` (v3 kopie, `spi_app.v` zamčený): výstupní registry adresy/dat zvlášť pro kanál A a B | lut_wr_sync3 3/0/0/56 (yosys kopie slučuje; Gowin +24 FF) | hloubka beze změny (OR `lw_neg != 0` je v zamčeném `tdc.v`) | cesta 9,2 ns (−0,11): kratší vedení, registr u svého BSRAM | nízké |
| 3 | 0004 | čas | `seg_chan3`: `fnz` registr; `v3_ring`: `nfc_a/b` = registr `~freeze & ~clear_x`; kopie `first_b` | seg_chan3 343/45/266/534 → 345/43/266/535; v3_ring +1 MUX +1 FF | CE `t0`: 7 → 5 vstupů (abc ho skládá do 3 úrovní kvůli sdílení `take`) | cesta 9,0 ns (+0,03): CE z 5 registrů (LUT5) a síť 88 FF rozdělená na `t0` (48) a `x0` (40) | nízké |
| 4 | 0005 | čas | `v3_ring`: kopie `ohAh/ohBh` pro bity 31:16 slova záznamu | v3_ring FF +16, LUT −2 | hloubka 4 beze změny | cesty `ohA→wa_a` 9,0 ns (−0,32/−0,28): fan-out 32 → 16 | nízké |
| 5 | 0006 | plocha | `tick_hi` do DSP (`syn_dspstyle`) | — (yosys neumí) | — | **−54 CLS, +0,006 ns (změřeno hlavní instancí, dTick)** | nízké |
| 6 | 0007 | plocha | `ts10`, `seq_tx` (10 MHz) do DSP | — | — | −2×(32 FF + 32 ALU), **−30 až −60 CLS [ODHAD]**; doména 100 ns → časování bez rizika | nízké |
| 7 | 0008 | plocha | `seq` v `pps_cap3` do DSP (vnitřní `seq_c`) | — | — | −32 FF −32 ALU, **−15 až −30 CLS [ODHAD]**; z DSP nevede časovaná cesta 100 MHz | nízké |
| 8 | 0010 | čas | `top_v3`: `hx_a/hx_b` = místní část podmínky hold z příštích hodnot; `hold_x_s` = LUT3 | top_v3 169/17/216/375 → 176/18/216/379 | `cal_valid,busy_ra→hold_a_s` 2→1 | cesta (v jiných sestaveních mezi nejhoršími) o úroveň kratší | nízké |

Celkem (yosys po modulech, výchozí sestavení): 5 826/1 589/1 884/4 947 → 5 836/1 585/1 884/4 974
(`logy/yosys_hier_vychozi.txt`; s `-DDEEP_RAW` stejné rozdíly, `logy/yosys_hier_deep.txt`). Zploštěné celkové
číslo (`logy/yosys_cmp_vychozi.txt`): LUT 3 913 → 3 822, MUX 512 → 417, FF 4 788 → 4 815. Pokles LUT/MUX je šum abc9
(viz 2.5); podstatná je tabulka po modulech.

Testovací commity: 0001 (stopa + yosys), 0009 a 0012 a 0013 (mutace), 0011 (invarianty + `tb_hold_inv`).

## 4. Výsledky

### 4.1 Testy na finální verzi (`logy/testy_final.txt`, normální modely `gowin_models.v`)

`run_sim.py` nad finálním stavem (`run.ps1` + `run_ps1_doplnek.txt`), 10 testů paralelně. Časy platí pro souběžné
zatížení stroje, proto je u `base/` zvlášť stejný běh pro srovnání.

| test | finální | čas | `base/` | čas |
|---|---|---|---|---|
| topv3 | PASS | 14,6 min | PASS | 15,0 min |
| topv3full | PASS | 14,8 min | PASS | 15,1 min |
| topv3cal | PASS | 9,2 min | PASS | 9,0 min |
| topv3presc | PASS | 11,0 min | PASS | 11,3 min |
| topv3deep | PASS | 9,7 min | PASS | 9,6 min |
| topv3deep2 | PASS | 9,7 min | PASS | 9,5 min |
| topv3ovf | PASS | 4,7 min | PASS | 3,6 min |
| topv3pw | PASS | 4,2 min | PASS | 3,2 min |
| holdinv (nový) | PASS | 0,8 min | — | — |
| phyv3 (beze změny PHY) | PASS | 2 s | PASS | 2 s |

Výpisy normálních testů `topv3`, `topv3pw` a `topv3cal` jsou mezi `base/` a finální verzí **doslova shodné**.
Normální modely tedy dávají v obou verzích stejná zpoždění TDC; přidané registry nezměnily pořadí volání `$urandom`.
`holdinv`: 305 299 taktů, 2× pokles `boot_done`, 2× `pchg_a`, `pchg_b`, `bchg`, 12 hranic segmentu, invarianty
bez chyby.

### 4.2 Stopy (`logy/stopy_porovnani.txt`)

| test | řádků stopy | sha256 stopy (16 znaků) | `base` × finální | `base` × `base` (a) |
|---|---|---|---|---|
| topv3 | 56 326 | 0c07e6e72ea81259 | SHODA | SHODA |
| topv3full | 66 208 | bea3d8c8d2d5e086 | SHODA | SHODA |
| topv3cal | 58 370 | 2a5942b02d84688d | SHODA | SHODA |
| topv3presc | 18 434 | 2be1f7e232ac225d | SHODA | SHODA |
| topv3deep | 312 869 | 10436eee4c3647ea | SHODA | SHODA |
| topv3deep2 | 365 496 | e196b8b89d6a6c97 | SHODA | SHODA |
| topv3ovf | 13 314 | 6d3a1de2e00de8f7 | SHODA | SHODA |
| topv3pw | 24 578 | 15577baa99b4e59e | SHODA | SHODA |

Všech 8 testů dává v režimu stopy PASS (`base/` i finální verze s invarianty).

Kroky řady proti `base/` (`trace_v3.py cmp`), vše **SHODA**:

| krok | testy |
|---|---|
| 1 (0002) | topv3, topv3full |
| 2 (0003) | topv3, topv3cal (výrobní zápis tabulky TDC) |
| 3 (0004) | topv3, topv3ovf, topv3presc |
| 4 (0005) | topv3, topv3ovf |
| 5, 6, 7 (0006–0008) | topv3 |
| 8 (0010) | topv3, topv3presc, topv3pw |
| finální (0013) | všech 8 |

Kontrola měřítka (`logy/meritko.txt`):
- (a) dva běhy `base`: 8× SHODA.
- (b) + neškodná instance: 4× SHODA. (b′) totéž s původními modely: ROZDÍL, takže deterministické modely jsou nutné.
- (c) mutace „B zdroj ignorovan“: ROZDÍL, test FAIL. Mutace „rozdil t: bez vypujcky“: ROZDÍL v transakci 1,
  bajt 726 (MISO 0x2a × 0x0a).
- (c′) „1PPS: seq > 1 vzdy“: SHODA (ekvivalentní mutant).

Stopa každého testu v tomto projektu se tedy na změnu chování rozejde a na změnu struktury bez změny chování ne.

### 4.3 Mutace (`logy/mutace_final.txt`, `logy/mutace_nezachycene_base.txt`)

`python3 sim/mut_top_v3.py <prace> <vystup> -j 10` na finální verzi, 48 mutací, 87 min.

- `--selftest` a kontrola před během: každá kotva je v kódu právě jednou.
- **45 ZACHYCENO, 1 EKVIVALENTNÍ, 2 NEZACHYCENO.**

| skupina | výsledek |
|---|---|
| 35 původních (včetně 3 s přepsanou kotvou: `znacka v taktu clear se pricte`, `freeze se nepouzije`, `B z pinu A: znacka P_B`) | ZACHYCENO |
| `1PPS: seq > 1 vzdy` (původní) | **EKVIVALENTNÍ** už v `base/`: `pfl_p = pps_gt1 ? pps_pfl_p : 0` a při `seq = 1` je `pps_pfl_p` = `pfl_l` před první hranou = 0, takže hradlo nemá pozorovatelný účinek. Stopa `topv3` s mutací je shodná s `base` (kap. 2.4 c′). |
| `FIFO hloubky 1`, `znacka: ts_valid o takt driv` (původní) | **NEZACHYCENO — stejně i nad `base/`** (viz níže) |
| 10 nových (`1PPS: wnz…`, `kopie povoleni bez wnz_b`, `tabulka B: kopie adresy…` (režim 2), `FIFO: fnz…`, `kopie first_b…`, `nfc: v taktu clear 1`, `nfc: pri seg_tick…`, `horni pulslovo…`, `hold_s: hx bez boot_tc`, `hold_s: hx bez ok_s` (režim 7 = `tb_hold_inv`)) | ZACHYCENO (invariant v testu) |

**Dvě nezachycené mutace jsou mezera kontrol `tb_top_v3`, která existovala už v `base/`; optimalizace s ní nesouvisí:**
- Původní `base/sim/mut_top_v3.py` nad původním `base/` (Icarus 13.0) je nezachytí taky. `FIFO hloubky 1`
  nezachytí ani běh s `TB_NFR=14` jako v `run.ps1`.
- Normální modely dávají v `base/` i ve finální verzi stejná zpoždění TDC (výpisy testů jsou doslova shodné,
  kap. 4.1). Na tomto stroji tedy jde o vlastnost testu, ne o rozdíl verzí.
- Obě mutace **mění výstup** a porovnání stop je odhalí:
  - `FIFO hloubky 1`: X0 záznamu o 1 vedle (transakce 4, bajt 296 = blok 1, záznam 1, bajt 7);
  - `ts_valid o takt driv`: N záznamu o 1 (transakce 7, bajt 417 = blok 1, záznam 5, bajt 4).

  Kontroly testbenche (N ≈ f·L, T0 proti X0 s tolerancí) takovou odchylku nevidí.
- **HYPOTÉZA:** na hlavním stroji (Icarus 12) může `$urandom` dát jiná zpoždění TDC a mutace tam mohou být
  zachycené (podle zadání bylo všech 38 zachyceno). Doporučení: zpřísnit kontrolu X0/N v `tb_top_v3`
  (přesná hodnota proti modelu hran). To je změna testu mimo tuto řadu.

## 5. Zkoušeno, zamítnuto, proč

| nápad | výsledek | proč |
|---|---|---|
| `tmr`: 4 dlouhé časovače na společné 1ms základně (−47 CLS změřeno hlavní instancí) | **mimo řadu** | Není přesné: doba se mění až o 1 ms podle fáze společné základny (boot 2^24 taktů = 167,77 ms → 166–167 ms, timeout kalibrace, `rt_dly`, `coh_hold`). V `SIM_TOP` je boot 2 000 taktů (20 µs) ≪ 1 ms okno, takže varianta z `abl_cls.py` by simulaci úplně změnila. Přesná varianta potřebuje předděličku pro každý časovač, a tím úspora mizí. Patří do ⚠️ změny chování s vlastním testem časů (nezpracováno). |
| Přestavba multiplexeru hlavičky na výběr 32b slova podle `bb[7:2]` + bajt podle `bb[1:0]` | zamítnuto | Yosys: LUT −57, MUX +42 (≈ −15 buněk, v šumu). Synteza `case (bb)` optimalizuje sama. Kužel hlavičky (−1 057 LUT, −808 FF, když `hb = 0`) tvoří hlavně registry polí, které protokol potřebuje. |
| Posuvné registry po skupinách bajtů hlavičky | zamítnuto | U živých polí (`pps_*`, `probe_*`, `st_*`, `raw_*`, `pra_s/prb_s`) mění takt čtení, tedy odchylka. U stálých polí (`seq_tx`, `a_ts`, `head_m1`, `rec_ack`, čítače RX…) je registr 8 FF/bajt dražší než položka multiplexeru (~4 LUT/bajt). |
| Konstanty hlavičky do BSRAM | zamítnuto | Konstanty (MAGIC, BUILD, FW, CAPS…) jsou v INIT LUT multiplexeru. Výstup BSRAM by byl další vstup multiplexeru a čtení má takt latence. |
| `dX` (`xhi` do DSP) | nezařazeno | Změřeno hlavní instancí: −22 CLS, ale rezerva −0,51 ns proti −0,38 ns. Cesta X → FIFO je v doméně 100 MHz a je v rozporu s cílem 1. Volné DSP zůstávají 2. |
| `shTR` | nezařazeno | Změřeno: 0 (Gowin registry sloučí sám). |
| Odstranění `tm` v `seg_chan3` (rozdíl `t − t0` přímo z `ts_ps`, držené ≥ 5 taktů) | zamítnuto | Přesné, −80 FF. Ale `ts_ps` přichází z TDC (daleko) přímo do 20b odčítačky; dnes je `tm` místní registr. Časování by se zhoršilo. |
| Zúžení FIFO `fh/fs` | zamítnuto | X0 potřebuje plných 40 b z hlavy FIFO. Rekonstrukce horních bitů z `xcnt` potřebuje příznak přenosu a složitou logiku; úspora ~50 FF. |
| Přesná saturace `d_l` (cesta `dd_r → d_l`, −0,23 ns) | nezpracováno | Dvoustupňová roura příznaků se liší jen při zveřejnění v taktu E+2 po hrubé hraně. To nastává jen u zastaralého jemného času (souběžný úkol N1/N2), ale bit po bitu to není. Přesná varianta (příznaky `dd ± 128` z vlastních odčítaček ve stejném taktu jako `dd_r`) stojí 2×34 ALU. Možnost: ⚠️ změna mimo řadu, nebo ponechat. |
| DSP `DOUT → u_ring/wa_a` (−0,17 ns) | nezpracováno | Součty jsou stálé od V+4 a první zápis je v E+7, takže jde o multicycle z konstrukce. Výjimku SDC by šlo obhájit (rozbor zde, invariant „`v2` = 0 a `clear` = 0 v taktu před zápisem `wa_a`“ by šel do testu). Cíl je ale buňka DSP a `get_regs`/`get_cells` na DSP v Gowinu jsem nemohl vyzkoušet (L-0223). RTL alternativa: registr DSP slova vybraného o takt dřív (+32 FF/kanál). Rozhodnutí na hlavní instanci. |
| `pseq_p` jako registr (místo 32b dekrementu v `app_v3`) | nezpracováno | Přesné, ale potřebuje nový port `pps_cap3` (zakázáno kvůli souběžné instanci). |
| `head_m1` jako registr | nezpracováno | `rec_head` je sudý: `head_m1 = {rec_head[31:1], 1}` po přičtení. Saturace při přetečení 2^32 potřebuje detekci jedniček. Úspora 32 ALU za +32 FF +~10 LUT je zanedbatelná. |
| Čítače 100 MHz s registrovaným porovnáním do DSP (`boot_cnt`, `cal_tmo`, `rt_dly`, `coh_hold`, `pwc`) | nezpracováno | Cesta DSP → strom porovnání → FF v 100 MHz je stejného typu jako u `dX` (horší rezerva). |
| Redundantní hradla `pfl_l` (`pps_nz`) a `pfl_p` (`pps_gt1`) v `app_v3` | jen zjištění | Obě hradla jsou bez účinku (pole jsou 0, dokud hradlo platí). Odstranění by ušetřilo jen ~16 LUT; `seq_gt1` je port `pps_cap3`. Proto je mutace `1PPS: seq > 1 vzdy` ekvivalentní (kap. 4.3). |

## 6. Co zůstalo nepokryté

- Skutečné CLS a rezerva v 8 sestaveních na krok: nemá je kdo změřit (Gowin jen na Windows). Účinek časovacích
  kroků je odhad z hloubky a fan-outu; L-0222 ukazuje, že kolísání ±1,5 ns je větší než jeden krok.
- Úspora DSP kroků 0007/0008 (3 čítače) je odhad. Ověřit, že GowinSynthesis čítače s povolením (`seq_tx`, `seq_c`)
  opravdu dá do ALU54D (8 DSP v reportu).
- Cesty `dd_r → d_l` a `DSP → wa_a` zůstávají (kap. 5).
- Icarus 12: neověřeno.
- Přírůstek FF z kopií v Gowinu (~52 FF) je odhad (yosys kopie slučuje).
