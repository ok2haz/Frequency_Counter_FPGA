# Audit: funkce FPGA v3 E4, E5, DSP a PSRAM (2026-10-23)

- **Commit auditované verze:** FPGA `c08be7a` (FW 0x0505), STM `7cd97ed` (v0.39.0, tag), dokumentace `6634f50`. Rozsah
  změn: `0a5a989..7cd97ed` (FW 0x0502 až 0x0505, v0.37.0 až v0.39.0) plus oprava `clear` a TSPIPE ze stejného dne.
- **Jádro / doména:** FPGA GW1NR-9 (`src/v3_core.v`, `top_v3.v`, `app_v3.v`, `timing_v3.sdc`); STM CM7 (`fpga_v3.c/.h`,
  `fpga_freq.c/.h`, `freertos_task_uart.c`, `freertos_task_fpga.c`).
- **Postup:** dva nezávislé průchody (FPGA, STM) podle `AUDIT_PLAYBOOK.md` kap. 3 (datové cesty, souběh, CDC, nekonečné
  čekání, rozpory kód ↔ specifikace). Každý nález jsem ověřil v kódu a kde to šlo, reprodukoval simulací na původním
  RTL (model a test upravené, RTL původní).
- **Neprojité:** P&R a deska (Windows / hardware), viz konec dokumentu.

## Souhrn

Nejzávažnější je řadič PSRAM: s reálným chováním RWDS by čtení bylo posunuté o bajt a kalibrace by zvolila špatnou
latenci, takže hluboký záznam by vracel nesmysly (F-0262, S1). Model v testbenchi to schoval. Druhá skupina je
protokol hlubokého záznamu: odmítnutý příkaz nešel poznat, takže STM mohlo vrátit stará data nebo jiný kanál
(F-0266, F-0272), a jedna chyba čtení zasekla PSRAM až do nové konfigurace FPGA (F-0268).

**Verdikt:** po opravách podmíněně funkční. Simulace a host testy procházejí; PSRAM, DSP a IDES8 zbývá ověřit P&R
a deskou. Opraveno 15 nálezů (1× S1, 9× S2, 5× S3), dalších 8 je zapsaných k P&R a k pozdějšímu zvážení.

| nález | sev. | oblast | stav |
|---|---|---|---|
| F-0262 | S1 | PSRAM čtení: RWDS při CA = falešný první bajt | opraveno `cb14596` |
| F-0263 | S3 | PSRAM kalibrace latence jen 16..23 a parita | opraveno `74456e9` |
| F-0264 | S2 | PSRAM pevná fáze vzorkování čtení | opraveno `02ae245` |
| F-0265 | S2 | CDC: přepínač T_RAW / CAL 0x02 ve stejném taktu jako parametry | opraveno `99e7404` |
| F-0266 | S2 | raw_cap3: režimy se nevylučují, odmítnutí nejde poznat, STOP chybí | opraveno `a6f78fd` |
| F-0267 | S3 | okno RAW: dvě zápisová místa do BSRAM | opraveno `c67da4b` |
| F-0268 | S2 | načítání okna se zasekne při chybě čtení | opraveno `ba6ae59` |
| F-0269 | S2 | kanál B z pinu A nese P_B | opraveno `abd3856` |
| F-0270 | S2 | mezery v RAW záznamu bez příznaku | opraveno `019a45a` |
| F-0271 | S2 | SDC `report_timing` na registry DSP → TA2003 | opraveno `1f1e44c` |
| F-0272 | S2 | STM: RAW / hluboký záznam bez potvrzení (stará data, jiný kanál) | opraveno `5a2c727` |
| F-0273 | S2 | STM: automatický rozsah pod 16 kHz zůstane na vysokém P | opraveno `c76f304` |
| F-0274 | S3 | STM: `watchdog_kick_fpga` z UartTask | opraveno `c891969` |
| F-0275 | S3 | STM: v2 nenuluje `presc_log2` | opraveno `21a6758` |
| F-0276 | S3 | STM: `%.3f` s nano.specs | opraveno `12f559f` |
| F-0277..F-0284 | S3/S4 | časování, šířky, P&R, drobnosti | otevřeno (P&R / později) |
| – | test | model PSRAM: pauza RWDS v dávce (mutace „čtení bez RWDS“ byla ekvivalentní) | opraveno `ecfcb6c` |

---

### F-0262 [S1] PSRAM: čtení bere konec příznaku latence jako první bajt

- **Místo:** `Frequency_Counter_FPGA_Module/src/v3_core.v` `psram_hb3`, stav `S_DATA` větev čtení (`rs[1] ^ rs[0]`).
- **Popis:** řadič bral bajt při každé změně vzorku RWDS od konce CA. HyperBus ale od CS# = 0 drží RWDS jako příznak
  latence (pevná dvojitá latence, výchozí CR0 → RWDS = 1) a před prvními daty ho stáhne do 0. Sestupná hrana příznaku
  se tak započítala jako bajt a všechna data byla o bajt posunutá.
- **Důkaz:** obecná specifikace HyperBus (RWDS během CA = indikace latence; při čtení data hranově zarovnaná s RWDS,
  první bajt s náběžnou hranou). Model `hb_psram_model` měl RWDS během CA ve stavu Z, takže se chyba neprojevila.
- **Dopad:** deterministicky při každém čtení. Kalibrace buď selže (`bad`), nebo, jako v simulaci, najde latenci
  posunutou o bajt, která posun kompenzuje jen u vzoru. Data oken pak nesouvisí.
- **Reprodukce:** model upravený na RWDS = 1 od CS# po celou CA a přes latenci čtení, původní RTL: `topv3deep` FAIL,
  kalibrace zvolí W0+3 místo W0+4 a rozdíly značek z oken nesouhlasí s periodou.
- **Oprava:** `rarm`, tedy bajty se počítají až po prvním vzorku RWDS = 0 v datové fázi. Model teď RWDS řídí jako PSRAM.
  Nová mutace „čtení bez zbrojení RWDS“.
- **Riziko opravy:** nízké. Při jednoduché latenci (RWDS = 0 během CA) se zbrojí hned.
- **Vztah k lekcím:** L-0207.
- **Stav:** opraveno v `cb14596`.

### F-0263 [S3] PSRAM: kalibrace latence zápisu pokrývá jen 4 skutečné latence

- **Místo:** `psram_hb3` `W0 = 16`, `ctry` 3 b (`wait_b = W0 .. W0+7`).
- **Popis:** latence HyperBus je v celých taktech CK, tedy 2 bajty. Z 8 zkoušených hodnot proto mohla sedět jen každá
  druhá, a pokryté byly jen 4 latence. Výchozí pevná dvojitá latence 6 nebo 7 taktů vychází podle výpočtu na ~22 nebo
  ~26 bajtů od konce CA, tedy na hranici rozsahu nebo mimo něj.
- **Dopad:** „kalibrace selhala“ (DEEP_ST bit1) a PSRAM nepoužitelná, pokud skutečná latence vyjde mimo 16..23.
- **Reprodukce:** samostatný test řadiče s latencí modelu 26 dal `bad` (zapsáno v `2026-10-23_funkce-fpga.md`).
- **Oprava:** W0 = 4 a 5bitový index, tedy 4..35 bajtů: latence 3..7 taktů, jednoduchá i dvojitá. Timeout čtení
  300 taktů, aby CS# zůstal pod tCSM 4 µs i při chybě.
- **Riziko opravy:** nízké. Kalibrace trvá déle: nejvýš 64 pokusů, řádově stovky µs po startu.
- **Vztah k lekcím:** L-0208.
- **Stav:** opraveno v `74456e9`.

### F-0264 [S2] PSRAM: pevná fáze vzorkování čtení

- **Místo:** `psram_hb3`: `rs <= {rs[0], ps_rwds}; dqs <= ps_dq;` na náběžné hraně `clk`; CK z FF na sestupné hraně.
- **Popis:** PSRAM mění DQ/RWDS za tCKD po hraně CK. Vzorek na náběžné hraně `clk` leží 5 ns po hraně CK, takže při tCKD
  kolem 5 ns padne do změny dat. Datasheet udává tCKD v řádu 1..7 ns podle rychlosti; na desce ověřit.
- **Dopad:** závisí na čipu a teplotě. Chybné bity při čtení a kalibrace selže nebo projde náhodně.
- **Reprodukce:** model s výstupem neplatným (X) 4..6 ns po hraně CK. S pevnou fází 0 kalibrace nenajde shodu
  (mutace „jen fáze čtení 0“ v `topv3deep2`).
- **Oprava:** druhá sada vzorků na sestupné hraně `clk` (`rwn`/`dqn`). Kalibrace zkusí fázi 0 a pak fázi 1; volba je
  v hlavičce 129 bit5. Nový test `topv3deep2` vyžaduje fázi 1.
- **Riziko opravy:** nízké. Půltaktová cesta `negedge` → `posedge` (5 ns) na 9 bitech.
- **Vztah k lekcím:** L-0209.
- **Stav:** opraveno v `02ae245`. **Zbývá:** časování IO PSRAM po P&R (IOB registry, zpoždění), viz F-0279.

### F-0265 [S2] CDC: přepínací bit T_RAW a kalibrace 0x02 ve stejném taktu jako parametry

- **Místo:** `src/app_v3.v` dekódování `T_RAW` (`raw_ch/raw_mode/raw_arg` a `raw_tgl <= ~raw_tgl` v jednom taktu)
  a `SET_CONFIG 0x02` (`cal_mask` a `cal_tgl`). Cíl: `raw_cap3` (přepínač 3 FF, parametry 2 FF), `top_v3` kalibrace
  (`ct_s` 3 FF, `cm1/cm2` 2 FF).
- **Popis:** přepínač i parametry se mění ve stejném taktu 10 MHz. Když metastabilita posune jeden bit parametru o takt
  proti přepínači, cíl převezme starý režim, kanál nebo masku.
- **Dopad:** vzácně a náhodně (metastabilita). RAW jiného kanálu nebo režimu, kalibrace špatného kanálu.
- **Reprodukce:** HYPOTÉZA. Simulace metastabilitu nemodeluje, nález je z kontroly kódu.
- **Oprava:** přepínač se překlopí 3 takty 10 MHz po parametrech (`raw_dly`/`cal_dly`, stejný vzor jako `lw_dly`
  u LUT_WRITE).
- **Riziko opravy:** nízké. Start se zpozdí o 300 ns.
- **Vztah k lekcím:** L-0210.
- **Stav:** opraveno v `99e7404`.

### F-0266 [S2] raw_cap3: režimy se nevylučují, odmítnutí nejde poznat, běžící záznam nejde zastavit

- **Místo:** `raw_cap3`, start při `rs[2] ^ rs[1]`; hlavička 127 byla 0.
- **Popis:**
  - Režim 0 (RAW 512) se spustil i během hlubokého záznamu nebo načítání okna. Přepnul `ch` běžícímu záznamu a psal do
    téhož BSRAM jako okno.
  - Režim 1 a 2 se při běhu odmítl potichu, takže STM nemělo jak poznat, že čte stará data.
  - Hluboký záznam bez signálu neskončil, a po timeoutu STM byla PSRAM blokovaná až do N značek.
  - Režim 1 nenuloval `wdone`.
- **Dopad:** deterministicky při souběhu příkazů (např. `fpga raw a` během `fpga raw b deep`, nebo opakování po
  timeoutu). Výsledek je záznam jiného kanálu, poškozené okno nebo stará data.
- **Reprodukce:** `topv3deep` (nové kroky): režim 0 a 2 během záznamu. Na RTL bez opravy se režim 0 provede (mutace
  „režim 0 bez výluky“).
- **Oprava:**
  - Režim 0 se provede jen bez `drun`/`wl`; režim 1 a 2 ukončí režim 0.
  - Režim 1 při běžícím záznamu = STOP (zbytek FIFO se dopíše).
  - Režim 1 nuluje `wdone`.
  - Bajt 127 = `{kanál, 0, počet přijatých příkazů}`. Počet je v Grayově kódu, protože se čte v 10 MHz bez
    synchronizace a nikdy tak není roztržený.
- **Riziko opravy:** nízké. Bajt 127 byl rezerva = 0. FW < 0x0505 ho nemá a STM to rozezná podle CAPS bit21.
- **Vztah k lekcím:** L-0211.
- **Stav:** opraveno v `a6f78fd` (FPGA) a `5a2c727` (STM, F-0272).

### F-0267 [S3] Okno RAW: dvě zápisová místa do BSRAM

- **Místo:** `raw_cap3`, `mem[n[8:0]] <= ts` (režim 0) a `mem[widx[8:0]] <= {asm_, p_rbyte}` (okno).
- **Popis:** dvě přiřazení do `mem` s různou adresou a daty v jednom bloku. GowinSynthesis nemusí takový vzor odvodit
  jako BSRAM se zápisovým portem.
- **Dopad:** 512 × 48 b = 24 kbit v logice se do GW1NR-9 vedle TDC nevejde, nebo P&R selže. HYPOTÉZA, ověří až P&R.
- **Oprava:** jeden zápis `mem[m_w2 ? widx : n]`. Podmínky jsou shodné, výluku zajišťuje F-0266.
- **Riziko opravy:** nízké (`topv3`, `topv3deep` beze změny výsledků).
- **Vztah k lekcím:** L-0212.
- **Stav:** opraveno v `c67da4b`.

### F-0268 [S2] Načítání okna z PSRAM se zasekne při chybě čtení

- **Místo:** `raw_cap3`, konec okna `if (wb == 24 && p_free && widx == 512)`.
- **Popis:** když PSRAM v některé dávce neodpoví (timeout čtení `psram_hb3`, porucha RWDS), `widx` 512 nedosáhne a
  `wl` zůstane 1 navždy. Každý další režim 1 i 2 se pak odmítne.
- **Dopad:** PSRAM nepoužitelná až do nové konfigurace FPGA, a STM bez F-0272 čeká na `WDONE` a vrátí chybu stránky.
- **Reprodukce:** `topv3deep`: model `dead = 1` (neodpovídá) → okno. Na RTL bez opravy se test zasekne (mutace „okno
  čeká na všechny bajty“).
- **Oprava:** konec po dokončení 24. dávky. Neúplné okno: DEEP_ST bit7, 0 značek.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0213.
- **Stav:** opraveno v `ba6ae59`.

### F-0269 [S2] Kanál B ze zdroje „pin A“ nese log2 P_B

- **Místo:** `src/top_v3.v`: `sig_b = (bs2[0] ? ch_a : div_b) ^ bs2[1]`, `u_sb .presc(pr_b)`.
- **Popis:** B z pinu A jde mimo předděličku (P = 1), ale záznam B nesl P_B. STM stahuje P_B v tom režimu na 0
  (`s3_range_ctl`), ovšem až po ozvěně a dvou shodných rámcích. Segmenty mezi koncem držení B (2 hranice) a změnou P
  tak mají špatné měřítko X.
- **Dopad:** deterministicky při přepnutí na šířku pulzu nebo zpoždění A/B s P_B > 0. Několik segmentů B má špatný
  kmitočet a čas.
- **Reprodukce:** `topv3pw` s P_B = 4 na pinu B a pak B z pinu A, původní RTL: FAIL (N ~ f·L, ST/SX, T0, dvoubodově u B).
- **Oprava:** `pr_b_eff = bs2[0] ? 0 : pr_b` do `seg_chan3` B. Mutace „znacka P_B“.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0214.
- **Stav:** opraveno v `abd3856`.

### F-0270 [S2] Mezery v RAW záznamu bez příznaku

- **Místo:** `top_v3.v` `want_a/b = ~hold & ~(mk_s & csa_s)`, `raw_cap3 .tv_a(tsv_a & ~hold_a)`. Maska při CS je ve
  výchozím stavu zapnutá (`app_v3` `mask_cs_en = 1`).
- **Popis:** při hold (kalibrace, změna P) a během každé transakce SPI s maskou TDC neznačkuje. RAW pak obsahuje rozdíl
  2P místo P a nic to nehlásí. Hluboký záznam trvá přes mnoho dotazů FpgaTasku, takže mezery má prakticky vždy.
- **Dopad:** deterministicky s maskou. Statistika rozdílů (σ, min/max) je zkreslená a uživatel neví proč.
- **Oprava:** DEEP_ST bit6 = během záznamu (režim 0 nebo 1) kanál aspoň chvíli neznačkoval. STM hlásí „v záznamu jsou
  mezery … `fpga diag mask off`“. Test: maska zapnutá → bit6, vypnutá → 0. Mutace „bez příznaku mezer“.
- **Riziko opravy:** nízké. Masku sama oprava nemění; vypnout ji je volba uživatele.
- **Vztah k lekcím:** L-0215.
- **Stav:** opraveno v `019a45a` (FPGA) a `5a2c727` (STM).

### F-0271 [S2] SDC odkazuje na registry součtů DSP, které syntéza může vstřebat do ALU54D

- **Místo:** `src/timing_v3.sdc`: `report_timing … -to [get_regs {u_sa/g_dsp.sx_d* …}]`.
- **Popis:** se `syn_dspstyle = "dsp"` se akumulátor mapuje do ALU54D včetně registru, takže registry `sx_d`/`st_d`
  v logice nemusí existovat. Odkaz na neexistující objekt je v Gowin chyba TA2003 a běh skončí.
- **Důkaz:** komentáře `timing_v3.sdc` ř. 5, 56, 71 a LESSONS (TA2003 u `calm_s`, `src_l`, `qd_f`).
- **Dopad:** sestavení FW 0x0504/0x0505 může skončit chybou, ještě než se cokoli změří. HYPOTÉZA, závisí na syntéze.
- **Oprava:** řádek odstraněn, místo něj komentář proč. Polohu DSP ukáže zpráva o zdrojích / `.rpt`.
- **Riziko opravy:** žádné (jen report).
- **Vztah k lekcím:** L-0216.
- **Stav:** opraveno v `1f1e44c`.

### F-0272 [S2] STM: RAW / hluboký záznam bez potvrzení přijetí

- **Místo:** `CM7/Core/Src/fpga_freq.c` `fpga_freq_v3_raw_deep`, `fpga_freq_v3_raw`.
- **Popis:**
  - Po startu STM čekalo na `RUN = 0 && DEEP_N != 0`. Když FPGA start odmítlo (běží jiný záznam nebo okno), nebo se
    rámec složil dřív, než se start projevil, vzalo STM DEEP_N a okna předchozího záznamu, klidně jiného kanálu.
  - Okna čekala na `WDONE && win == w`. To splní i staré okno se stejným číslem.
  - Po timeoutu záznam běžel dál a další volání se odmítlo.
- **Dopad:** stará nebo cizí data bez chybového hlášení (deterministicky při souběhu / opakování po timeoutu).
- **Oprava:**
  - `s3_raw_cmd` čte bajt 127 před příkazem a po něm a vrátí „odmítnuto“ (−5), když se počet nezmění; FW bez CAPS
    bit21 posílá beze změny.
  - Hluboký záznam: běžící předchozí záznam nejdřív STOP. Po timeoutu STOP a přečte se, co se zapsalo (návrat 0).
    Na konci se kontroluje kanál (bit7).
  - Okno: potvrzení příkazu, pak `WDONE`, a chyba okna (bit7) = −1.
  - RAW 512 také s potvrzením a `ri.dst` (mezery).
  - Dotaz během záznamu po 100 ms místo 20 ms. Každý dotaz je transakce SPI a s maskou mezera (recenzent F9).
- **Riziko opravy:** střední (víc transakcí kolem příkazu). Host test (13) bajt 127, mutace „bajt 127 se nečte“.
- **Vztah k lekcím:** L-0211, L-0215.
- **Stav:** opraveno v `5a2c727`.

### F-0273 [S2] STM: automatický rozsah pod 16 kHz zůstane na vysokém P

- **Místo:** `fpga_freq.c` `s3_range_ctl` → `fpga_v3_range_select(cur, FPGA_V3_PROBE_HZ(probe))`, kde `f ≤ 0` znamená
  „beze změny“.
- **Popis:** sonda počítá hrany /16 za 1 ms, takže PROBE = 0 znamená f < 16 kHz, ne „bez odhadu“. Po vysokém kmitočtu
  (P = 16) a přechodu na 1 kHz zůstalo P = 16: 62 hran/s, segment 25 ms má 1–2 hrany.
- **Dopad:** deterministicky, měření pod 16 kHz po předchozím vysokém kmitočtu je výrazně horší, dokud uživatel nedá
  `fpga presc … 0`.
- **Oprava:** `fpga_v3_range_probe(cur, probe)` vrací pro probe 0 hodnotu P = 1. Kompromis: po návratu vysokého
  kmitočtu ze „žádného signálu“ je pár segmentů neplatných (kontrolní počítání), než automatika P zvedne. Host test
  (11), mutace „sonda 0 beze změny“.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** L-0217.
- **Stav:** opraveno v `c76f304`.

### F-0274 [S3] STM: `watchdog_kick_fpga` z UartTask

- **Místo:** `fpga_freq_v3_raw_deep`, smyčka oken (`watchdog_kick_fpga()` po každém okně).
- **Popis:** hluboký záznam běží v UartTask, ale kopal slot FpgaTasku. FpgaTask během čtení dál běží a kope sám (zámek
  SPI se drží jen na transakci), takže kopání z UartTask nebylo potřeba. Navíc by schovalo zaseknutý FpgaTask až na
  desítky sekund čtení.
- **Oprava:** řádek odstraněn.
- **Vztah k lekcím:** L-0218.
- **Stav:** opraveno v `c891969`.

### F-0275 [S3] STM: v2 nenuluje `presc_log2`

- **Místo:** `fpga_freq.c` `parse_data` (v2) nastavuje `pps_*` (L-0204), ale `presc_log2` ne. `fpga_meas_t` se nenuluje.
- **Dopad:** pojistka přímého měření (`edge_count … / (1 << presc_log2)`, `fpga_freq.c:1355`) může u v2 dělit odpadem.
- **Oprava:** `m->presc_log2 = 0u`.
- **Vztah k lekcím:** L-0219 (opakování L-0204).
- **Stav:** opraveno v `21a6758`.

### F-0276 [S3] STM: `%.3f` v UART s nano.specs

- **Místo:** `freertos_task_uart.c` `fpga v3`: „sonda %.3f MHz“ (v0.38.0).
- **Důkaz:** link `--specs=nano.specs` bez `-u _printf_float`. Newlib-nano float přeskočí, takže se vytiskne prázdné pole.
- **Oprava:** sonda v kHz celým číslem (`probe × 16`). Nové pravidlo `scripts/zakazane_vzory.txt` (L-0220); po opravě
  0 výskytů ve firmware.
- **Stav:** opraveno v `12f559f`.

---

### F-0277 [S3] Dlouhé jednotaktové cesty v raw_cap3 a psram_hb3

- **Místo:** `raw_cap3`: `avail` (13b součet pěti členů), `wrem`/`wlim` (32b odečtení a porovnání), `ar2[10:0] * 3072`
  ve startu okna. `psram_hb3`: `wait_b` v porovnání `cnt`.
- **Dopad:** HYPOTÉZA, porušení 10 ns po P&R. Parametry jsou kvazistatické (`ar2` se mění jen příkazem), takže většina
  cest je fakticky multicyklová.
- **Návrh:** po P&R zkontrolovat v `timing_v3` reportu. Při porušení registrovat `avail` a `wlim` o takt dřív (start
  okna stejně čeká na `p_free`).
- **Stav:** otevřeno (P&R).

### F-0278 [S3] `st_d` 56 b vs. ALU54D 54 b

- **Místo:** `seg_chan3` `g_dsp`: `reg [55:0] st_d` se `syn_dspstyle`.
- **Popis:** ALU54D má 54bitový akumulátor, takže 56 b může syntéza rozdělit, nebo DSP nevezme. Zúžení ale nejde: ST
  při 33 MHz a P = 1 dosáhne ~2^53,9 (825 000 značek × průměr 2^34,25), tedy těsně pod 2^54. Ruční P = 1 nad 33 MHz
  by 54 b přeteklo.
- **Stav:** otevřeno. Šířku neměnit; po P&R ověřit, kolik ALU54D a CLS součty vzaly.

### F-0279 [S3] PSRAM: vývody a IO nejsou ověřené P&R

- **Místo:** `top_v3.v` porty `O_psram_*`/`IO_psram_*` (jména podle IP Gowin, bez `.cst`), CK z FF na sestupné hraně,
  vzorky na obou hranách.
- **Návrh:** první P&R s PSRAM. Zkontrolovat přiřazení vyhrazených vývodů, IOB registry pro DQ/RWDS, zpoždění CK ↔ DQ.
  Na desce `fpga v3`: PSRAM připravena, latence a fáze čtení.
- **Stav:** otevřeno (P&R, deska).

### F-0280 [S3] STM: jemný čas 1PPS a epocha z aktuální hlavičky

- **Místo:** `fpga_freq.c:1073-1074` (`fpga_v3_pps_fine(h, 0, …)`, `FPGA_V3_PPS_EPOCH(h->pps_status)`).
- **Popis:** epocha a PCLK stav jsou z hlavičky rámce, ne z okamžiku hrany. Při novém zamčení PLL mezi hranou a rámcem
  může jedna hrana dostat novou epochu.
- **Dopad:** jedno zbytečné nebo chybějící přerušení diferencování 1PPS, a jen při novém zamčení PLL.
- **Stav:** otevřeno (nízké riziko). Řešení by vyžadovalo epochu v záznamu PPS (protokol).

### F-0281 [S3] STM: přerušení statistiky při změně P je společné pro oba kanály

- **Místo:** `fpga_freq.c:2706` (`s3_brk = 1u` při příkazu P kteréhokoli kanálu).
- **Popis:** změna P_B přeruší i statistiku A. Okna se přes změnu P neskládají (grp podle `p_log2` segmentu), takže
  chyba měření nevzniká, jen zbytečné přerušení.
- **Stav:** otevřeno (S3, kosmetické).

### F-0282 [S3] STM: `s3_h` se čte v UartTask během zápisu ve FpgaTask

- **Místo:** `fpga_freq_v3_info` (UartTask) čte `s3_h`, a FpgaTask ji přepisuje v `fpga_v3_parse`.
- **Dopad:** výpis `fpga v3` může vzácně ukázat roztrženou hlavičku. Jen diagnostika, žádné řízení.
- **Stav:** otevřeno.

### F-0283 [S3] STM: `fpga_freq_ev_pair` nekontroluje ozvěnu 0x09

- **Místo:** `fpga_freq.c:2856-2875`: po `v3_cfg(FPGA_V3_CFG_BSRC, …)` se nastaví `s3e_bsrc` bez kontroly `cfg_echo`.
- **Dopad:** při ztraceném příkazu (CRC) STM páruje A/B, jako by B bylo z pinu A.
- **Stav:** otevřeno.

### F-0284 [S4] Drobnosti

- T_RAW p0 bity 2:1 = 3 se mapuje na režim 0. Lépe by bylo „neznámý = neprovedeno“ (STM ho neposílá).
- Po změně P je neplatný o segment víc, než je nutné (hold 2 hranice + segment s bit1).
- `s3_mseq` se zvyšuje mimo zámek SPI v `s3_cfg_any` (emulace).
- Rozpětí `span_ps` přes přetečení 48 b osy u záznamu delšího než ~172 s (při 699 050 značkách a f < 4 kHz).
- Host test nepokrývá `s3_raw_cmd` (závisí na SPI). Pokrývá ho až zkouška na desce.
- **Stav:** otevřeno, hromadně v F6.

---

## Co bylo zkontrolováno a je v pořádku

- `pps_ides3` / `pps_cap3`: CDC přes přepínač a držení 48 b ~1 s. Atomické zveřejnění PSEQ/PTS/hrubý/`d`.
- `pin_cnt3`: sonda v doméně 100 MHz přes 3 FF z c[3]; změna P jen na `seg_tick`, 2 segmenty hold.
- `seg_chan3` SUM_DSP: hodnoty shodné s variantou v logice (`-DSIM_SUM_FAB`), DRAIN = 6 platí.
- FIFO a serializátor hlubokého záznamu: přetečení = příznak a zahození, počet `dn` jen z přijatých značek.
- STM: parsování hlavičky 124..131 (host test 13), RAW stránky společně s RAW 512, `fpga_v3_pps_fine` (CAPS + PCLK +
  PFLAGS), GPSDO se zlomkem taktu.

## Nezkontrolováno / omezení tohoto běhu

- Vše je ověřené jen simulací (Icarus 13) a host testy. **Chybí P&R** (Windows: časování, BSRAM/DSP odvození, IO PSRAM,
  IDES8 na IOB23A) **a deska**.
- Model PSRAM je podle obecné specifikace HyperBus, ne podle datasheetu vestavěné PSRAM GW1NR-9 (není v repu).
- Metastabilitu (F-0265) simulace neukáže.

## Ověření po opravách

- FPGA (Icarus 13): `topv3`, `topv3ovf`, `topv3cal`, `topv3pw`, `topv3presc`, `topv3deep`, `topv3deep2`, `phyv3`
  PASS; celá regrese `sim/run.ps1` (v2 + v3) 36/36 PASS.
- Mutace FPGA 35/35 (nové: čtení bez zbrojení RWDS, jen fáze čtení 0, režim 0 bez výluky, bez STOP, okno čeká na
  všechny bajty, značka P_B u B z pinu A, bez příznaku mezer).
- 🔴 **Nalezeno při tom:** po F-0262 byla mutace „čtení bez RWDS“ (po prvním bajtu brát každý takt) s modelem
  ekvivalentní, protože model dával data přesně bajt za takt. Model teď uprostřed dávky čtení 2 hrany nic nevydá a RWDS
  nepřepne (`PSTALL`, `ecfcb6c`). Mutace je zachycená a `topv3deep`/`topv3deep2` procházejí (L-0207).
- STM: host test PASS, mutace 36/36, Release CM7 + CM4 0 varování, `audit.py` 97 / 0 / 2 (výchozí stav).
