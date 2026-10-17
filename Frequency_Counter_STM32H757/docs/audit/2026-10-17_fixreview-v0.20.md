# Audit: přezkum změn v0.18.1 → v0.20.0  (2026-10-17)

- **Commit:** `ecf0a82` (kód beze změny od tagu `v0.20.0` = `8b16ae9`; po něm jen dokumentace)
- **Rozsah:** `git diff v0.18.0..v0.20.0` — datalog `dac_code` (v0.19.0), počítadla spojení CM4 (v0.19.0),
  `GET /api/raw` + IPC v26 kanál `raw` + `sdram_log_read_abs` + `tools/raw_fetch.py` (v0.20.0),
  `gpsdo_tau_advise` / „tau min.“ (v0.20.0), web graf Vc s vlastním měřítkem os (v0.20.0), SPA #309/#310 (v0.18.1).
- **Proč tento cíl:** `/audit-modul` přišel bez jména modulu a žádný modul se stavem `nezačato` nezbývá →
  podle precedentu (2026-09-25, 2026-09-29) přezkum posledních kódových změn. Všechny jsou z dnešního sezení a nikdo je nepřezkoumal.
- **Jádro / doména:** CM7 (D1: AXI SRAM, SDRAM `.measlog`; D3: SRAM4 `.ipc_shared`) + CM4 (D2: SRAM2, lwIP)
- **HAL:** STM32H7xx HAL 1.11.6 (`stm32h7xx_hal.c:52-54`), GCC 14.3.1
- **Projité sekce checklistu:** B (sdílená paměť, bariéry), C (umístění bufferů, cache), D (souběh, zásobníky, watchdog), E (návratové hodnoty, čekání)
- **Neprojité (a proč):** A (hodiny/napájení se neměnily), F (interní Flash se nezapisuje), G (konfigurace periferií beze změny), H (errata se netýkají)
- **Ověřovací stav vstupu:** v0.19.0 běží na desce po power-cyklu (2026-10-17 10:11): `selftest` 16/16 PASS (včetně rozšířeného `datalog_selftest`), `datalog dump` ukazuje `Dac=`, `/api/state` má blok `conn`. **v0.20.0 na desce není** (logger běží, flash odložen na pokyn uživatele).

## Souhrn

Změny přidávají diagnostiku a export dat; měřicí cestu (FpgaTask, `gpsdo_tick`, `ad5693_servo`) nemění. Největší riziko
je v novém exportu surových měření: **číslo epochy logu měření není jednoznačné přes restart** (F-0238), takže klient,
který navazuje kurzorem, po restartu desky tiše slepí dvě různé řady. Druhý nález (F-0237) je o jedna mimo v detekci
přepsání záznamu v `sdram_log` — nová funkce ho zdědila po dvou starších. Zrcadlo datalogu na SD po upgradu míchá
11- a 12-sloupcové řádky pod jednou hlavičkou (F-0239). Ostatní (F-0240, F-0241) jsou S4.
**Verdikt: podmíněně funkční** — export funguje do prvního restartu desky během navazování; zbytek funkční.

---

### F-0237 [S3] Detekce přepsání záznamu v `sdram_log` je o jedna mimo (a dvojčata nemají bariéru)

- **Místo:** `CM7/Core/Src/sdram_log.c:307` (`sdram_log_read_abs`, nová), dvojčata `:255` (`sdram_log_get`) a `:276` (`sdram_log_read_back`)
- **Popis:** Platnost zkopírovaného záznamu s absolutním indexem `a` se ověřuje podmínkou `s_head - a > SDRAM_LOG_CAP`.
  Slot záznamu `a` ale producent přepisuje zápisem indexu `a + CAP`, a to **ve chvíli, kdy `s_head == a + CAP`**
  (hlava se zvedne až po zápisu, `sdram_log.c:230-236`). Rozepsaný slot proto dává `s_head - a == CAP` a projde jako platný.
  Správně má být `>=`. `sdram_log_get` a `sdram_log_read_back` navíc mezi kopií `s_buf` (nevolatilní) a kontrolou
  volatilního `s_head` nemají kompilátorovou bariéru (`read_abs` ji má, `:304`), takže překladač smí čtení dat přesunout
  za kontrolu.
- **Důkaz:** producent `sdram_log_put`: `h = s_head; r = &s_buf[h & mask]; …zápis…; __DMB(); s_head = h + 1u;`
  (`sdram_log.c:230-236`). Nejstarší dostupný index je `h - CAP` (`win_of`, `:283`) a jeho slot je **přesně ten, který
  producent zapíše jako další**. Kontrola: `:255`, `:276`, `:307` — všechny `> SDRAM_LOG_CAP`.
- **Dopad:** jen po přetočení ringu (262 144 záznamů ≈ 18 h při 4 měřeních/s) a jen pro čtení **nejstaršího** záznamu.
  To je přesně první dotaz `/api/raw` s kurzorem starším než ring (`trunc`), nebo `sdramlog dump` celé historie.
  Čtenář (defaultTask nebo UartTask) a producent (FpgaTask) mají stejnou prioritu, takže preempce uprostřed kopie
  možná je. Výsledek je jeden roztržený záznam (starý `seq`, nový kmitočet apod.) **bez jakéhokoli hlášení**.
  Pravděpodobnost na jeden dotaz je malá (okno = doba zápisu 32 B během kopie), deterministická reprodukce není.
- **Reprodukce:** HYPOTÉZA — ověřit: po přetočení (`sdramlog` → `wrapped 1`) v cyklu stahovat `/api/raw?from=0`
  a kontrolovat, že `seq` v první položce odpovídá `t_ms`; nebo syntetický test s producentem v cyklu a kontrolou
  konzistence `seq`/`t_ms`.
- **Návrh opravy:** ve všech třech kontrolách `>=` místo `>`; v `get` a `read_back` `__DMB()` mezi kopií a kontrolou
  (jako v `read_abs`). Selftest: `idx`/okno už kryje `win_of`; doplnit čistou funkci „je záznam `a` platný při hlavě
  `h`" s hraničním případem `h == a + CAP`.
- **Riziko opravy:** nízké — hranice se posune o jeden záznam (nejstarší se zahodí o 250 ms dřív).
- **Vztah k lekcím:** `L-0012` (dvojče — vada je ve třech čtecích funkcích), `L-0017` (tichá chyba), F-0118 (původní kontrakt čtenář/producent).
- **Stav:** otevřeno

---

### F-0238 [S2] Číslo epochy logu měření se po každém restartu CM7 vrací na 1 — kurzor `/api/raw` po restartu tiše navazuje na cizí řadu

- **Místo:** `CM7/Core/Src/sdram_log.c:38` (`static volatile uint32_t s_epoch;` v `.bss`) a `:229` (první záznam
  epochy → `s_epoch++`); `CM7/Core/Src/ipc.c:594` (nová epocha se pozná jen podle `req_epoch != w.epoch`);
  `tools/raw_fetch.py:39-51` (`resume` bere epochu a kurzor z posledního řádku CSV)
- **Popis:** `s_epoch` začíná po každém bootu na 0, takže první epocha každého běhu má číslo 1. Klient drží
  `(epoch, from)` = `(1, N)`. Po restartu desky má nová epocha zase číslo 1, server proto **nepozná, že je řada jiná**.
  Dokud nová hlava nedosáhne `N`, vrací prázdné odpovědi (`a0 >= h`, `sdram_log.c:300`). Pak vydá záznamy od indexu
  `N` **nové** řady. Klient tím zahodí prvních `N` záznamů nového běhu a navíc je zapíše za stará data jako jednu
  souvislou řadu.
- **Důkaz:** `.bss` se nuluje při každém startu (`s_epoch` v `.bss`, nm `CM7/Release/H757_LED_CM7.elf`). Jediná
  kontrola nové řady je `ipc.c:594`. `raw_fetch.py` při navázání posílá `epoch` z CSV a o restartu desky nemá jak
  vědět. Pole `seq` z FPGA restart CM7 neodhalí: FPGA se při resetu STM32 neresetuje a `seq` pokračuje.
- **Dopad:** deterministický, jakmile se deska restartuje (power-cyklus, flash, IWDG) během dlouhého stahování
  s `--follow` nebo mezi dvěma navazujícími běhy `raw_fetch.py`. Analýza (ADEV, drift) pak běží přes nehlášený skok mezi
  dvěma běhy přístroje a přes chybějící úsek. Hlášeno to není (`newep` = 0, `trunc` = 0).
- **Reprodukce:** staticky jistá cesta (viz důkaz). Na HW: `raw_fetch.py --follow`, po ~10 min power-cyklus desky,
  sledovat CSV — epocha zůstane 1, `abs` pokračuje bez mezery a chybí prvních ~2400 záznamů po bootu.
- **Návrh opravy (minimální):** (a) epocha jednoznačná přes boot: počáteční hodnotu `s_epoch` při `sdram_log_init`
  odvodit z neopakující se veličiny, např. RNG periferie (H7 ji má) nebo `HAL_GetUIDw0() ^ DWT->CYCCNT` při náběhu
  (stačí, aby se dvě po sobě jdoucí hodnoty lišily s vysokou pravděpodobností). (b) Na serveru navíc pojistka:
  `req_epoch == w.epoch && req_from > w.head` je ve stejné epoše nemožné → hlásit `newep` a začít od nejstaršího.
  (c) `raw_fetch.py`: `head < cur` hlásit jako novou řadu. Stačí (a) + (b); (c) je obrana klienta.
- **Riziko opravy:** nízké — mění se jen hodnota čísla epochy, ne formát; klienti stejně berou epochu z odpovědi.
- **Vztah k lekcím:** `L-0103` („Nové" není „navazující": u počítadla kontroluj souvislost, ne jen změnu),
  `L-0102` (historie se smí navázat jen přes souvislý úsek téhož zdroje), `L-0104`.
- **Stav:** otevřeno

---

### F-0239 [S3] Zrcadlo datalogu na SD po upgradu firmwaru přidává 12sloupcové řádky pod 11sloupcovou hlavičku

- **Místo:** `CM7/Core/Src/datalog_sd.c:605-622` (stejná karta → `FA_OPEN_ALWAYS` a připisování na konec bez
  kontroly hlavičky), `:624-631` (hlavička jen při vytvoření souboru); nový sloupec `dac_code` v `sd_export.c:276-291`
- **Popis:** v0.19.0 přidala do řádku CSV sloupec `dac_code`. Zrcadlo `GPSDOLOG.CSV` na téže kartě (`same_card`,
  `datalog_sd.c:603`) se po upgradu jen otevře a připisuje, takže soubor dostane pod starou hlavičkou
  (11 sloupců) nové řádky s 12 sloupci.
- **Důkaz:** větev `same_card` (`:605-622`) volá jen `mirror_recover_seq_from_file` a `f_lseek` na konec; hlavičku
  zapisuje jen větev `else` (`:629`). Hlavička `sd_export_csv_header` (`sd_export.c:286-292`) má od v0.19.0 sloupec navíc.
- **Dopad:** jen se zapnutým zrcadlem (`s_mirror_en`, opt-in, `datalog_sd.c:467`) a vloženou kartou, deterministicky při
  prvním běhu nové verze. Nástroje, které čtou CSV podle hlavičky (pandas, Excel), buď selžou, nebo poslední sloupec
  zahodí. Přístroj sám nic nerozbije, `mirror_recover_seq_from_file` čte jen první sloupec.
- **Reprodukce:** staticky jistá cesta. Na HW: karta se zrcadlem z v0.18.x, flash v0.19.0+, `sd` → otevřít `GPSDOLOG.CSV`.
- **Návrh opravy:** při otevření existujícího souboru přečíst první řádek a porovnat s `sd_export_csv_header`; při
  neshodě soubor přejmenovat (`GPSDOLOG.OLD` / s číslem) a založit nový s aktuální hlavičkou. Obecně: změna formátu
  řádku = změna hlavičky = nový soubor.
- **Riziko opravy:** nízké až střední (`f_rename` na FatFs, nutná zkouška na kartě).
- **Vztah k lekcím:** `L-0026` (nové pole v záznamu přerostlo konzumenta, o kterém nikdo nevěděl), `L-0105` (změna významu → projdi všechny, kdo čtou).
- **Stav:** otevřeno

---

### F-0240 [S4] `/api/state` `conn.http.used` vždy započítává i spojení, které dotaz obsluhuje

- **Místo:** `CM4/LWIP/App/httpd_min.c:134-142` (`http_slots_used`), `:644-650` (blok `conn`)
- **Popis:** `used` se počítá při sestavování odpovědi, kdy slot dotazu je obsazený, takže hodnota je vždy ≥ 1
  a logger (`tools/gpsdo_log.ps1`, sloupec `http_used`) ukazuje o jedno víc, než kolik je „cizích" klientů.
  Nikde to není napsané.
- **Důkaz:** `s_hconn[i].pcb != NULL` pro aktuální spojení během `build_state_json`.
- **Dopad:** jen výklad čísla; `peak` a `rej` jsou v pořádku.
- **Návrh opravy:** dokumentovat („včetně tohoto dotazu") v komentáři a v `gpsdo_log.ps1`, nebo vracet i `self:1`.
- **Riziko opravy:** žádné.
- **Vztah k lekcím:** `L-0089` (popisek tvrdil vlastnost, kterou veličina nemá).
- **Stav:** otevřeno

---

### F-0241 [S4] „tau min." po změně `gpsdo tau` míchá data z různých smyček

- **Místo:** `CM7/Core/Src/freertos_task_fpga.c:770-775` (`gpsdo_set_tau` nemění historii ani stav), `:550-589`
  (`gpsdo_tau_advise` bere poslední souvislý úsek LOCK), `:532` (mez `tau_loop/3` z AKTUÁLNÍHO τ)
- **Popis:** změna τ v LOCK nepřeruší úsek LOCK ani historii, takže dalších až 2 h počítá `tau min.` z fáze, kterou
  zčásti tvarovala předchozí smyčka. Mez hodnoceného pásma `τ/3` přitom bere nové τ.
- **Důkaz:** `gpsdo_set_tau` jen přepíše `s_g_tau`, `s_gc.tau` se převezme v dalším kroku (`:717`); stav ani
  historie se nemění.
- **Dopad:** po zkrácení τ (např. 1000 → 200 s) může fáze z doby τ = 1000 s ukazovat menší přebytek → mez vyjde
  výš, než odpovídá nové smyčce. Jen zobrazení, smyčka se nemění.
- **Návrh opravy:** v `gpsdo_tau_advise` brát úsek až od poslední změny τ (čas změny uložit v `gpsdo_set_tau`),
  případně do té doby ukazovat „počkej".
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** `L-0101` (nulování musí vyprázdnit i to, co je „na cestě").
- **Stav:** otevřeno

---

## Co bylo zkontrolováno a je v pořádku

- **Datalog `dac_code` (v0.19.0):** starý `pack_rec` buffer nuluje (`datalog.c:177`), takže staré záznamy mají v bajtech
  30..31 opravdu nuly → „nezaznamenáno". `0xFFFF` (smazaný slot) se mapuje taky na „nezaznamenáno" (`:230-231`). CRC kryje
  dál 0..27, formát 32 B beze změny. `dac_code_now` (`:470-479`) čte `g_gpsdo_drive`/`g_gpsdo_code_q16` ve stejném tasku,
  který je zapisuje (defaultTask, `freertos_task_fpga.c:741-742`), a `drive` je jen s `dac_ok` (`:738`). Emulace nic
  nezapisuje (L-0160). **Ověřeno na HW:** `selftest` 16/16 a `datalog dump` s `Dac=`.
- **Řádek CSV:** nejdelší řádek ~102 B proti bufferům 160 B (`datalog_sd.c:629/668`, `sd_export.c:1345`, `freertos_task_uart.c:1966`).
- **Počítadla spojení:** `s_http_rej`/`s_rej` saturují (L-0059), `peak` ≤ `HTTPD_MAX_CONN`/`SCPI_TCP_MAX_CONN`.
- **IPC v26:** blok `raw` leží na KONCI `ipc_shared_t` za `gpsdo` (`ipc_shared.h:662`), takže detekce nesouladu bank
  (`cm4_ipc_version` v bloku `cm4`) funguje. `_Static_assert` na velikost ≤ 64 KB a na offsety `ipc_raw_xfer_t`
  (`resp_gate_ns` 40, `rec` 56; explicitní výplň místo implicitní). CM7 nuluje blok v `ipc_stamp` (handshake, jako `log`/`errlog`).
  Bariéry: CM4 `IPC_DMB()` před `req_gen`, CM7 `IPC_DMB()` před `resp_gen`. Paralelní požadavek odmítá 503
  a při časovém limitu se zastaralá odpověď nedoručí (shoda `defer_gen`).
- **Umístění bufferů (z `.elf`):** `s_buf` `0xC1000000` (`.measlog`, 8 MB, MPU 3 cacheable, čte jen CM7 → koherentní);
  `ipc_raw_service` `s_tmp` `0x24018b20` (3 200 B, `.bss` AXI); `gpsdo_tau_advise` `s_x` `0x2400faf8` (2 880 B);
  selftest `x` `0x2400eef8`; `g_ipc` `0x38000000`–`0x38010000` (`.ipc_shared`). CM4 SDRAM nečte (L-0002/C-checklist).
- **Rozpočet JSON `/api/raw`:** `_Static_assert` 100 × 56 + 208 < 6144 (`httpd_min.c:93-95`). µHz < 2⁵³ do ~9 GHz.
- **`gpsdo_tau_advise`:** dva volající (UiTask, UartTask) se statickými buffery — výpočet dělá jen držitel `s_busy`,
  výsledek se kopíruje v kritické sekci (L-0023). CPU < 1 ms (~5 000 iterací), žádný spin > 10 ms. Měřicí cesta
  (`gpsdo_tick`) jen čtená přes `gpsdo_hist_at`/`gpsdo_get`. Selftest jádra ověřen v Pythonu věrnou kopií
  (stejný xorshift): bílý PM → dolní mez 200 s; bílý FM 3e-10 → průsečík 114 s.
- **Web graf Vc:** `spec('vc').spans` z každé řady zvlášť, hranice na celých kódech DAC; skrytá řada = žádná osa;
  `ser_test`/`vcaxes_test`/řetězec `check.py --build` OK, `SPA_HTML` v ELF = extrakce + 1.
- **Build/audit:** obě jádra 0 varování, `tools/audit.py` 92/0/2 (mrtvé funkce jen dřívější), ETH `DscrTab` `30040000`, `ram_heap` `1002b188`.

## Nezkontrolováno / omezení tohoto běhu

- **Nic z v0.20.0 neběželo na desce** (flash odložen kvůli běžícímu měření): `/api/raw`, `raw_fetch.py` proti
  skutečné desce, řádek „tau min." v okně GPSDO TIE (rozložení textu na y = 388 jen podle geometrie), `gpsdo test`
  s novým testem, graf Vc v prohlížeči.
- Souběh v F-0237 je odvozený z kódu, ne změřený.
- `tools/analyze_gpsdo_log.py` a `gpsdo_log.ps1 -OnNew` ověřeny jen na PC (zkušební běh 24 s proti desce v0.19.0).
- SPA změny #309 (výběr řádků tabulky Allan) jen JS testy a kontrolní řetězec; vizuálně neověřeno.
