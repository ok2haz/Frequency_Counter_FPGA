# Audit: matematické funkce měření — přezkum dnešních oprav (F-0186..F-0194)  (2026-09-27)

- **Commit:** `72bf839` (HEAD větve `audit/2026-09-09-hodiny-pwr` v době auditu)
- **Jádro / doména:** CM7 (`fpga_freq.c`, `screen_main.c`, `app_gpsdo.c`, `ipc.c`,
  `ipc_scpi.c`) + CM4 sdílený `httpd_min.c` (JS SPA)
- **Projité sekce checklistu:** C (buffery — jen kontrola nových `static` polí),
  D (souběh — nové globály `s_poll_gap`/`s_seq_*`, `s_seed_*`), E (chybové stavy —
  akumulátory, gap detekce)
- **Neprojité (a proč):** A, B, F, G, H — dnešní změny nesahají na hodiny/PWR,
  dual-core boot, Flash/bootloader ani periferie mimo SPI2/I2C1 už dříve auditované

## Souhrn

Tohle NENÍ audit modulu 24 od nuly (ten proběhl třikrát 2026-09-26 + kriticky
2026-09-27, viz `2026-09-26_matematika-mereni*.md` a `2026-09-27_matematika-kriticky.md`).
Je to **cílený přezkum kódu, který vznikl z TĚCH auditů** — commity `98c4394..72bf839`
(F-0186 až F-0194, 775 vložených řádků v 11 souborech) — protože ten kód sám dosud
žádným strukturovaným F3 průchodem neprošel (jen vlastním selftestem a HW testem).
Přesně to je třída, kde se podle `L-0105` (dnešní vlastní regrese F-0189) chyby
schovávají nejvíc.

**Verdikt: podmíněně funkční.** Jádrová matematika (fázová pyramida F-0187, hi-res
z ticků F-0186, souvislost SEQUENCE F-0193) byla ověřena i numericky (selftest běžel
a prošel na reálném křemíku dnes) a ruční rozbor indexování/hranic nenašel chybu.
**Jeden nový nález S2**: druhý akumulátor měření (`FPGA_ACC_DATALOG`) není chráněný
žádnou z dnešních oprav (`fpga_stat_break`/`fpga_stat_flush` čistí jen
`FPGA_ACC_STATS`) — při změně měřeného signálu uprostřed 10s periody datalogu
vznikne záznam s `freq_avg=1`, který je fyzikálně nesmyslným průměrem dvou různých
kmitočtů, ale tváří se jako platné měření.

---

### F-0195 [S2] Datalogový akumulátor (`FPGA_ACC_DATALOG`) není chráněný proti změně signálu uprostřed periody — F-0188/F-0193 opravily jen živou statistiku

- **Místo:**
  - `CM7/Core/Src/fpga_freq.c:697` (`fpga_stat_break`) a `:705` (`fpga_stat_flush`) —
    obě funkce dělají `memset(&s_acc[FPGA_ACC_STATS], 0, ...)`, tedy **jen index 0**.
  - `CM7/Core/Inc/fpga_freq.h:154-156` — `FPGA_ACC_STATS=0` (živá statistika,
    `fpga_stat_pop`), `FPGA_ACC_DATALOG=1` (datalog, `fpga_acc_take`), `FPGA_ACC_N=2`.
  - `CM7/app/screens/screen_main.c:1160-1166` (`freq_advance`, F-0188) — na
    `sig_change` volá `fpga_stat_flush()`, ne obecné „vyprázdni VŠECHNY akumulátory".
  - `CM7/Core/Src/freertos_task_fpga.c:82-89` (F-0193) — `fpga_stat_break()` při
    díře v SEQUENCE, stejná jednostrannost.
  - **Spotřebitel:** `CM7/Core/Src/datalog.c:481` (`sample()`,
    `fpga_acc_take(FPGA_ACC_DATALOG, &hz, NULL)` → `r->freq_hz`, `r->freq_avg=1`) —
    přesně to pole, které `app_gpsdo.c` `seed_rec_usable()` (F-0189) bere jako
    důvěryhodný „průměr za periodu" pro rekonstrukci Allanovy pyramidy.
- **Popis:** Oba akumulátory (`s_acc[FPGA_ACC_STATS]` a `s_acc[FPGA_ACC_DATALOG]`)
  plní **tatáž** `fpga_acc_add()` z FpgaTasku identicky. F-0188 (nulování při změně
  signálu) a F-0193 (nulování při díře v SEQUENCE) obě řeší **jen** index 0. Když se
  měřený signál změní (jiný zdroj připojený k čítači, nebo automatické přepnutí
  /4↔/16 kolem 380 MHz) **uprostřed** 10s datalogové periody, `FPGA_ACC_DATALOG`
  dál tiše sčítá cykly a čas z obou stran přechodu — reciproký průměr
  `hz = cyc_e5·1e7/gate_ps` pak není průměr JEDNOHO signálu, ale směs dvou různých
  nominálních kmitočtů (např. při přechodu 10→12 MHz uprostřed periody vyjde číslo
  blízko 11 MHz), a přesto se zapíše s `freq_avg=1` — příznakem, který dosud
  znamenal „čistý průměr za celou periodu".
- **Dopad:** Dva různé důsledky:
  1. **Trvalý, exportovaný artefakt** — `datalog csv`/`MMEM:DATA?`/web `/api/log`
     ukáže při každé změně signálu jeden nesmyslný bod (ne šum, ne mezera — číslo,
     které vypadá jako platné měření). Uživatel čtoucí surová data o tom neví.
  2. **Rekonstrukce (F-0189):** `seed_rec_usable()` tenhle záznam **obvykle** odmítne
     (namíchaná ~11 MHz se od reference 10 MHz i 12 MHz liší o víc než práh 1e-4) —
     ale ne VŽDY: dost blízké kmitočty (např. 10,0000 → 10,0005 MHz, změna menší než
     rozdíl vah obou stran) mohou dát mix, který PROJDE prahem `screen_main_signal_match`
     proti jedné z front referencí, a pak vstoupí do pyramidy jako platný bod.
  ⚠️ **Sekvenční díra (F-0193) sama o sobě NENÍ stejně závažná** — pokud FpgaTask jen
  zmešká pár měření TÉHOŽ signálu (např. při `fpgaloop`), reciproký průměr přes kratší,
  ale stále souvislý úsek zůstává nezkresleným odhadem TÉHOŽ kmitočtu (chybí jen
  přesnost, ne správnost). Nález je tedy hlavně o **změně signálu**, ne o díře.
- **Reprodukce:** `fpgasim on 10000000`, počkat < 10 s (uprostřed datalogové periody),
  `fpgasim on 10000500` (malá změna, aby prošla prahem 1e-4 proti aspoň jedné straně),
  počkat na uzavření periody, `datalog dump` — očekávaný `freq_avg=1` záznam s
  kmitočtem mezi 10 000 000 a 10 000 500 Hz, který neodpovídá ŽÁDNÉMU skutečnému
  měření. HYPOTÉZA jen v tom, že jsem to nezkoušel přímo na desce (vyžaduje
  desetiny sekund přesně mířený zásah do 10s okna) — mechanismus (dva akumulátory,
  jedna funkce čistí jen jeden) je doložený přímo ze zdrojového kódu.
- **Návrh opravy (A):** `fpga_stat_break()`/`fpga_stat_flush()` rozšířit na
  `for (i = 0; i < FPGA_ACC_N; i++) memset(&s_acc[i], 0, sizeof s_acc[i]);`
  (case pojmenovat např. `fpga_acc_break_all`/`_flush_all`, nebo prostě rozšířit
  stávající funkce — sémanticky to samé „okna už nenavazují" platí pro OBA
  spotřebitele). Vedlejší efekt: datalogový záznam z periody, kdy došlo ke změně
  signálu, dostane `freq_avg=0` (okamžitý vzorek) místo zavádějícího `=1` — to je
  navíc PŘESNĚ existující, už otestovaná cesta (`datalog.c` sama umí zaznamenat
  „bez průměru").
- **Riziko opravy:** nízké — stejný vzor (`memset` na `s_acc[FPGA_ACC_STATS]`) se
  jen zopakuje pro druhý index; žádná změna formátu dat, žádný bump `IPC_VERSION`.
- **Vztah k lekcím:** `L-0101` (nulování musí vyprázdnit i to, co je „na cestě" —
  tady je nové, že „na cestě" jsou DVA nezávislé akumulátory, ne jeden), `L-0106`
  (druhé místo se stejným polem existuje a nikdo ho neprošel systematicky),
  `L-0107` (nová lekce z této opravy).
- **Stav:** ✅ opraveno 2026-09-27 — `fpga_stat_break()`/`fpga_stat_flush()`
  (`CM7/Core/Src/fpga_freq.c`) teď iterují `for (i = 0; i < FPGA_ACC_N; i++)`
  místo pevného indexu `FPGA_ACC_STATS`. Přesně podle návrhu opravy výše.
  Build Release CM7 0 varování, `tools/audit.py` 92 OK/0/2 (baseline), `.text`
  629640 → 629672 B (+32 B, dvě `memset` volání místo jednoho — ověřeno
  v disassembly `H757_LED_CM7.list`). `IPC_VERSION` beze změny (čistě CM7,
  žádný sdílený layout). ⬜ **neověřeno na HW** (mechanismus vyžaduje zásah
  do 10s okna, viz Reprodukce výše).

---

## Co bylo zkontrolováno a je v pořádku

- **F-0186 (`fpga_freq_dt_ticks`, hi-res z ticků):** zaokrouhlení `round(gate_ns/2,5ns)`
  ověřeno na lichých/sudých `dt` i na hranici 21,5s okna; přetečení `uint64` vyloučeno
  guardem `edges·mul <= 4e9` (`fpga_freq_hires_mul`). Emulátor teď skládá rámec podle
  `spi_app.v` bit za bit (floor hradla, náhodná fáze) — verifikováno numericky HW
  selftestem dnes (`fpga: select hystereze + ticky okna + souvislost SEQ selftest OK`).
- **F-0187 (fázová pyramida, `adev_feed_into`/`adev_ring_kind`):** ručně odvozeno, že
  `sg->acc` v okamžiku výpočtu `E` drží přesně `C_j` (součet PŘEDCHOZÍCH podbloků,
  protože `sg->acc += v` běží AŽ PO použití pro `acc_e`) — dekompozice `E=Σ(C_j+E_j)/100`
  sedí. Indexování `bm`/`adev_at`/`adev_e_at` pro `pb` (stage>0) čte Ybar i E ze
  STEJNÉHO okna `[bm, bm+Mm)`, takže nemůže dojít k posunu mezi oběma poli. `e_n`
  počítá VŽDY souvislou řadu od nejnovějšího dozadu (reset na 0 při jediné neplatné
  E), takže `adev_base_n(sg, e_n)` neukazuje do „díry". `screen_main_stats_reset()`
  dělá `memset(s_adev, 0, sizeof s_adev)` — CELOU strukturu, tedy i nová pole
  (`eph`/`e_n`/`acc_e`/`acc_e_ok`) se vynulují automaticky, žádné riziko
  „zapomenutého pole při resetu". Ověřeno i numericky (selftest, 1e-5 shoda s
  nezávislou definicí MDEV nad poli faze pro m=1/3/9).
- **F-0189 (index „od nejnovějšího" se za běhu rekonstrukce posouvá):** ověřeno
  ručním odvozením na `datalog_read_bulk`'s `from_newest` (relativní k ŽIVÉMU
  `s_head`, ne k snapshotu z počátku rekonstrukce) — posun indexu při přírůstku
  nových záznamů PŘESKOČÍ přesně tolik záznamů, kolik jich mezitím přibylo (ne
  zdvojí), a mezera je vždy jen jednotky vzorků (≪ `SEED_GAP_EXTRA_S`=120s), tedy
  pod prahem řezu — komentář v kódu je přesný.
- **F-0190/F-0191 (web JS, `ingestM`/`mTau0`/`allan_band_fill`):** `k=(q-lastSeq-1)>>>0`
  správně zachytí i wrap/reset SEQUENCE jako obrovskou mezeru (JS `>>>0` převádí
  přes `ToInt32` na 32bit unsigned). `mTau0` dělitel `n-1+inner` nemůže být 0 —
  volající (`drawStab`/`drawPn`) mají guard `n>=4`/`n>=PN_N` PŘED voláním. `mGrid`
  ukončuje vnější smyčku korektně (mantisa 1 je ve všech třech hustotách, takže
  `3*d<=N` selže současně s `3*M[i]*d<=N` pro všechny `M[i]>=1`).
- **F-0193 (`fpga_seq_gap`, injekce `fpgasim fault gap`):** hranice `FPGA_SEQ_GAP_MAX`
  a `0xFFFFFFFF` sentinel ověřeny selftestem i dnešním HW testem (`diry 1
  (zmeskano 3 mereni), resync 1` po `fpgasim fault gap`).
- **F-0194 (`ipc_scpi_src_from_snap` `selftest_pass`):** nový test v `ipc_selftest`
  nastavuje `g_selftest_res` na 1 i 2 a ověřuje obě hodnoty přes `ipc_stamp`+
  `ipc_scpi_src_from_snap` nad LOKÁLNÍ instancí (žádný závod s `g_ipc`).
- **Souběh nových globálů:** `s_poll_gap`/`s_seq_gaps`/`s_seq_missed`/`s_seq_resync`
  (fpga_freq.c) i `s_seed_*` (app_gpsdo.c) mají jediného zapisovatele (FpgaTask,
  resp. UiTask) a jsou bez `volatile` — to je nekonzistentní se sekcí D checklistu
  doslovně, ale je to STEJNÝ, už dřív auditovaný vzor jako existující `g_rx_crc`/
  `s_link_ok` ve stejném souboru (nezavedeno dnešními změnami, není nový nález).

## Nezkontrolováno / omezení tohoto běhu

- **F-0195 není ověřeno na HW** — vyžaduje zásah do 10s okna s přesností na
  desetiny sekundy; označeno HYPOTÉZA v nálezu.
- Nekontroloval jsem znovu SPA `dom_check`/`json_kontrakt`/velikost `SPA_HTML`
  v ELF (to už prošlo `check.py --build` při F-0190 fixu a od té doby se
  `httpd_min.c` nezměnil).
- Nekontroloval jsem znovu selftesty samotné (F-0186/F-0187/F-0193 vektory) — ty
  už běžely na HW dnes (viz `docs/audit/2026-09-27_hw-test-modul24.md`).
