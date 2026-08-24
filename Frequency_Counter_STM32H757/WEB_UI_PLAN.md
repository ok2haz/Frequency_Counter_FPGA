# Webové rozhraní na CM4 — audit a etapový plán

> Zadání: *„ovládat zařízení přes web stejně jako z displeje"* (STATUS #26, etapa **F7**).
> Psáno 2026-08-22, po dokončení F3+F5 (ETH + lwIP + DHCP, kód hotový, **HW neověřeno**).
> Navazuje na `ETH_BRINGUP_CHECKLIST.md`, `NAVRH_ARCHITEKTURA_CM7_CM4.md` §6a.

---

## 1. Audit — co dnes existuje a co chybí

### 1.1 🔴 NÁLEZ Č. 1: ovládací cesta CM4→CM7 je z 90 % NEIMPLEMENTOVANÁ

Tohle je nejdůležitější zjištění celého auditu a mění pořadí prací. Dokumentace působí,
že cmd ring ovládání umí — **neumí**.

`ipc_service_rings()` (ipc.c):
```c
case IPC_CMD_NOP:      break;
case IPC_CMD_CFG_SET:  if (!ipc_cfg_apply(...)) r.status = 1u; break;
default:               r.status = 1u; break;   /* GATE/RUN/CHAN/LOG dozraje s CM4 */
```
`ipc_cfg_apply()` implementuje **jen 8 klíčů**: `MATH_EN/M/B`, `NULL_EN/ACQ`, `LIM_EN/LO/HI`.
`IPC_CFG_GATE`, `IPC_CFG_CHAN`, `IPC_CFG_RUN` **v enumu jsou, ale spadnou do `default:
return 0`** → CM7 je odmítne se `status = 1`.

**Důsledek: web by dnes uměl nastavit Math a limity — a nic víc.** Žádné RUN/STOP,
žádná brána, žádný kanál, žádný datalog. Přitom právě to je „ovládání přístroje".

⚠️ **CLAUDE.md tohle tvrdí nepřesně:** *„Klíče `SCPI_CFG_GATE/CHAN/RUN` mají protějšky
`IPC_CFG_*` …, takže je CM4 pošle přes cmd ring beze změny."* O enumu to platí,
o funkčnosti ne. **Opravit při W1.**

**Dobrá zpráva:** strojovna na CM7 hotová je. SCPI přes USB tytéž věci ovládá mostem
`g_ui_cfg_req` + `g_ui_cfg_req_pend` → `screen_main_apply_cfg_req()` v UiTasku
(stav měření vlastní UiTask, SCPI běží v UartTasku). `ipc_cfg_apply` musí jen sáhnout
na tentýž most. Je to malá, uzavřená práce — ale **musí předcházet webu**.

### 1.2 ✅ Datová cesta CM7→CM4 je hotová a ověřená

IPC snapshot v6 nese plný instrument-state: kmitočet (/4 i /16), brána, kanál, GPS,
všech 10 senzorů + `sens_valid` maska, kalibrace AD8307, Si5356, ADEV body, Math cfg,
zdraví, uptime. **Že to stačí, je už dokázané** — `ipc_scpi_src_from_snap()` plní tentýž
`scpi_src_t` a UART `scpi ipc <cmd>` porovnává odpověď se `scpi <cmd>` → SHODA/ROZDIL.

`GET /api/state` je tedy **jen serializace snapshotu do JSON**, žádná nová data.

### 1.3 HTTP server: vendorovaný lwIP `httpd` vs. vlastní

lwIP `src/apps/http/` (httpd.c, fs.c, makefsdata) v balíku **je**, ale do projektu jsem
ho **nekopíroval**.

| | lwIP httpd | vlastní minimální |
|---|---|---|
| statické soubory | přes `makefsdata` → `fsdata.c` (externí nástroj v build kroku) | stránka jako `const char[]` v `.rodata` |
| JSON API | přes CGI/SSI — pro REST nemotorné | přirozené |
| kód navíc | ~2500 řádků cizího | ~300 vlastních |
| testovatelnost | mimo náš selftest | parser i JSON = pure-logic → **`selftest` na cíli** |

**Doporučení: vlastní.** Je to týž důvod, proč je hand-rolled SCPI (*„0 vendored kódu,
testovatelné"*) — a `makefsdata` by zavlekl další krok do buildu, který už teď dělá potíže.

### 1.4 Rozpočty — nejsou problém

| | využito | k dispozici |
|---|---|---|
| CM4 flash (bank2) | 84,6 KB | **1024 KB** → ~939 KB volných |
| CM4 RAM (SRAM2) | ~30 KB | 128 KB |
| `.eth_dma` (SRAM3) | 12,7 KB | 32 KB |

SPA o desítkách KB se ztratí. ⚠️ Zkontrolovat jen `MEM_SIZE` (dnes 14 KB) a
`TCP_SND_BUF`/`MEMP_NUM_TCP_PCB` v `lwipopts.h` proti počtu souběžných spojení.

### 1.5 🔴 NÁLEZ Č. 2: bezpečnost — nerozhodnuto, a teď to začíná pálit

Dnes je jediné ovládání zvenčí **SCPI přes USB** = fyzický přístup ke kabelu.
**Web je LAN a bez jakékoli autentizace.** Kdokoli v síti by mohl zmáčknout STOP
uprostřed týdenního měření stability.

`ETH_BRINGUP_CHECKLIST.md` §9 to má jako *„Bezpečnost — rozhodnout před F6"*, a F6 (SCPI
na TCP) se ještě nestalo. **Tohle je rozhodnutí uživatele, ne moje** — viz W0.

### 1.6 🟡 NÁLEZ Č. 3: „stejně jako z displeje" nejde vzít doslova

Displej má ~40 oken. Většina z nich je ale **vlastnost displeje**, ne přístroje —
vzhled, jas, auto-dim, animace, efekty, screensaver. Vzdáleně nedávají smysl.

A část věcí dnes **vzdáleně nejde vůbec**, protože žijí na CM7 bez IPC cesty:
kalibrace (zápis W25Q z UiTasku), sestavy/setup profily, SD export, self-survey,
průvodce kalibrací. Doplňovat je znamená pro každou zvlášť rozšířit cmd ring.

**Rozumný rozsah webu = přístroj, ne zrcadlo UI:** měření + jeho konfigurace
(RUN/gate/kanál/Math/limity/datalog) + čtení diagnostiky (senzory, GPS, zdraví, ADEV).

### 1.7 ⚠️ Připomínka: headline je pořád simulace (#2)

Kmitočet na hlavní obrazovce je `freq_step()` random walk. SCPI to řeší zlatým pravidlem:
`MEAS:FREQ?` vrací **reálný FPGA** (`fpga_freq_get_last`), a když není, **NaN `9.91E37`** —
raději nic než hezká lež. **Web musí držet totéž pravidlo**, jinak bude přes síť
prezentovat simulaci jako měření.

---

## 2. Architektura: ovládání jde přes SCPI (rozhodnuto 2026-08-22)

**SCPI se použije jako JEDINÁ ovládací plocha** — pro USB, pro TCP 5025 i pro web.
Web nedostane vlastní příkazovou sadu.

**Proč to dává smysl:**
1. **SCPI na ethernetu je stejně požadavek** (#25/F6). Musí tedy na CM4 vzniknout tak jako tak
   — web na něm pak jede **za nula řádků navíc** v ovládací vrstvě.
2. **`scpi.c` je na to už postavené.** Je transportně i datově nezávislé (`scpi_src_t`),
   má **per-session kontext** (`scpi_ctx_t` — vznikl přesně kvůli souběžným TCP spojením)
   a **ověřeně se překládá jako `-DCORE_CM4`**.
3. **Čtecí půlka pro CM4 už existuje a je ověřená:** `ipc_scpi_src_from_snap()` plní
   `scpi_src_t` ze snapshotu a UART `scpi ipc <cmd>` porovnává výsledek s `scpi <cmd>`
   → SHODA/ROZDIL. Runtime důkaz, ne domněnka.
4. **Jedna validace místo dvou.** Vlastní `/api/cmd` by potřeboval vlastní kontrolu
   rozsahů a mohl by se od SCPI rozejít. SCPI má ~130 assertů v `selftest`.
5. Přidání příkazu prospěje **naráz USB, TCP i webu**.

**Kde SCPI naopak NEPOUŽÍT: hromadné čtení pro dashboard.**
Panel obnovovaný 1×/s by přes SCPI potřeboval ~15 round-tripů. Agregáty (`SYST:TEMP:ALL?`,
`MEAS:VOLT:ALL?`) sice existují právě kvůli tomu, ale prohlížeč stejně chce **jeden JSON**.
⚠️ Ten JSON se ale serializuje **z téhož `scpi_src_t`/snapshotu**, takže se od SCPI
**nemůže sémanticky rozejít** — stejná data, jiné kódování.

```
             ┌── USB CDC ──┐
ovládání ────┼── TCP 5025 ─┼──> scpi_process_ctx() ──> set_cfg ──> cmd ring ──> CM7
             └── POST /api/scpi ┘
čtení (web) ──> GET /api/state ──> JSON  <── serializace TÉHOŽ snapshotu
```

### 2.1 Co k tomu chybí (změřeno, ne odhad)

| # | Chybí | Rozsah | Stav |
|---|---|---|---|
| 1 | `ipc_cfg_apply` na CM7: `GATE/CHAN/RUN` + `IPC_CMD_LOG` (nález 1.1) | S | ✅ W1 |
| 2 | CM4 `set_cfg` callback → cmd ring (**zapisovací půlka SCPI**) | S | ✅ W2 (`ipc_scpi_set_cfg`, zatím volá jen CM7 test) |
| 3 | `scpi.c` přeložit do obrazu CM4 | S | ✅ W2 (+ `meas_math.c`, explicitní `SCPI/subdir.mk`) |
| 3b | *(objeveno při #3)* `scpi.c` mimo `#if CORE_CM7` sahalo na CM7 globály (RTC/jas) | S | ✅ W2 (`-241 Hardware missing`) |
| 4 | TCP server 5025 na CM4 (raw API, per spojení `scpi_ctx_t`) | M | ✅ W3 |
| 5 | CM4 `read_log` → potřebuje IPC okno pro čtení datalogu | M (lze odložit) | — (W6) |
| 6 | HTTP server + JSON `/api/state` + `/api/scpi` | M | ✅ W4 |
| 7 | Basic Auth (jméno/heslo z okna PŘÍSTUP) + SPA stránka | L | ✅ W5 |

✅ **Parser NULL callbacky hlídá** (`if (src->set_cfg && ...)`, `if (!src->read_log || ...)`)
→ dokud #5 není hotové, `MMEM:DATA?` přes TCP/HTTP jen **selže**, nespadne.

---

## 3. Etapový plán (přeuspořádaný: SCPI/TCP PŘED webem)

⚠️ **Změna pořadí proti první verzi.** SCPI přes TCP je **levnější důkaz celého řetězce**
(lwIP → SCPI → cmd ring → přístroj) než web: testuje se `nc`/PuTTY/VISA, bez jediného
řádku HTML a JSON. Když to funguje, web už jen přidává prezentaci. Když ne, je hned jasné,
že problém je v síti nebo v ovládací cestě — ne v prohlížeči.

```
W0 ROZHODNUTÍ ─→ W1 ovládací cesta ─→ W2 SCPI na CM4 ─→ W3 TCP 5025 ─→ W4 HTTP+JSON ─→ W5 SPA ─→ W6 dotažení
   bezpečnost      (BEZ site!)          (BEZ site!)       = F6           read-only        ovládání
```

| W | Cíl | Rozsah | Hotovo, když |
|---|---|---|---|
| **W0** | ✅ Autentizace (jméno/heslo, okno PŘÍSTUP) | S | hotovo 2026-08-23 |
| **W1** | ✅ `IPC_CFG_GATE/CHAN/RUN/LOG` → most `g_ui_cfg_req` | S | hotovo 2026-08-23 (`ipccmd` UART test) |
| **W2** | ✅ `scpi.c`+`meas_math.c` v obrazu CM4, `ipc_scpi.c` sdílené | S | hotovo 2026-08-23 — CM4 `scpi_selftest()` PASS přes IPC v7 |
| **W3** | ✅ **TCP 5025** (F6) | M | hotovo 2026-08-23 (kód) — ⬅ **HW test čeká** |
| **W4** | ✅ HTTP kostra + `GET /api/state` + `POST /api/scpi` | M | hotovo 2026-08-23 (kód) — ⬅ **HW test čeká** |
| **W5** | ✅ SPA (HTML/CSS/JS v flash, poll 1 Hz) | L | hotovo 2026-08-23 (kód) — ⬅ **HW test čeká, PLÁN DOKONČEN** |
| **W6** | SSE místo pollu, mDNS `gpsdo.local`, stažení logu | L | — |

### W0 — ✅ Rozhodnutí (hotovo 2026-08-23)

- [x] **Autentizace.** Zvoleno: jméno/heslo v novém okně **PŘÍSTUP** (`s_view=42`,
      tlačítko v okně SÍŤ) + samostatný přepínač **ZAP/VYP** pro *ovládání* (čtení je
      vždy povolené). Heslo se **negeneruje ručně** — přístroj nemá klávesnici — ale
      vygeneruje samo (tlačítko NOVÉ HESLO) a jen zobrazí; opisuje se do prohlížeče.
      Persist v syscfg (magic `SCFC`→`SCFD`), globály `g_web_ctrl_en/g_web_user/g_web_pass`.
      ⚠️ Generátor NENÍ kryptografický (app vrstva nesmí tahat HAL/DWT) — mixuje uptime
      v okamžiku stisku, šum ADC senzorů a předchozí heslo. Na LAN dost, ne pro internet.
- [x] **Rozsah ovládání** (viz 1.6): RUN/STOP, brána, kanál, Math+limity, datalog on/off.
      Vzhled/jas/animace **ne**. Kalibrace/sestavy/SD **až kdyby byl důvod** (každé = nový klíč).
- [ ] **Zlaté pravidlo dat** (viz 1.7): `null` v JSON pro neplatné/nedostupné hodnoty,
      **nikdy simulace**. Bity `sens_valid` už ve snapshotu jsou — použít je. (Platí od W4.)

### W1 — ✅ Ovládací cesta (hotovo 2026-08-23)

Odstranilo nález 1.1. Ověřeno bez sítě, bez SCPI, bez webu.

- [x] `ipc_ui_cfg_apply()` (nová funkce v `ipc.c`, odděleně od čisté `ipc_cfg_apply`) —
      `IPC_CFG_GATE/CHAN/RUN` zapisují do `g_ui_cfg_req`+`g_ui_cfg_req_pend`
      **přesně jako `scpi_src_set_cfg_cm7`** (tentýž kódovací vzor, ne druhá cesta).
- [x] `IPC_CMD_LOG` → `datalog_set_enabled()`; legacy `IPC_CMD_GATE/CHAN/RUNSTOP` taky zapojené.
- [x] ⚠️ **Vlákna:** `ipc_service()` běží v defaultTasku, stav měření vlastní UiTask →
      zapisuje se jen požadavek, aplikuje UiTask. `ipc_ui_cfg_apply` je záměrně NEčistá
      (sahá na globály) → nesmí se volat z `ipc_selftest`.
- [x] **Kritérium splněno jinak, než plán čekal:** místo `ipctest` vznikl UART příkaz
      **`ipccmd run 0|1 | gate 0..3 | chan 0|1 | log 0|1`** — pošle příkaz přesně tou
      cestou, kterou později použije CM4 (cmd ring → `ipc_service`), počká na odpověď
      z resp ringu a vypíše OK/ERR. Ověřitelné jedním řádkem přes UART.
- [x] 🔴 **Doplněno po HW testu přes web (2026-08-23) — SLEPÝ READBACK, `IPC_VERSION` 10→11.**
      Zápis fungoval od začátku, ale **`SENS:FREQ:GATE?`/`CHAN?` přes TCP/HTTP vracely pořád
      `0.1` a `0`**, takže to vypadalo jako „brána a vstup nejdou nastavit". Snapshot totiž
      nesl jen `channel_id`/`gate_ns` = **co ohlásil FPGA rámec** (při mrtvém SPI linku nula),
      ale **ne NASTAVENÍ** (`g_ui_cfg`): `ipc_scpi_src_from_snap` proto `set_gate_idx` neplnil
      vůbec (0 z `memset`) a `set_chan` bral z `channel_id`. USB cesta byla přitom správně
      (`scpi_src_load_cm7` dekóduje tytéž bity z `g_ui_cfg`) → **dvě různé pravdy o tomtéž
      přístroji**. Opraveno polem `ui_cfg` ve snapshotu (bývalý `_pad_s`, velikost beze změny);
      obě jádra ho dekódují stejným výrazem. RUN se to netýkalo, protože ten se odvozoval
      z `IPC_F_RUNNING`, které se **plnilo z `g_ui_cfg`** — proto RUN/STOP fungoval a mátl.
      **Poučení: readback musí číst NASTAVENÍ, ne poslední naměřenou hodnotu.**
- [x] Opraveno přeceněné tvrzení v CLAUDE.md (bod 1.1 → „IPC_CFG_GATE/CHAN/RUN jsou jen
      v enumu" bylo pravda do W1, teď je funkční).

### W2 — ✅ SCPI jádro skutečně BĚŽÍ na CM4 (hotovo 2026-08-23)

- [x] `scpi.c` + `meas_math.c` (fyzicky `CM7/Core/Src/`) se linkují i do CM4 obrazu —
      explicitní `SCPI/subdir.mk` (CM4 `Core/Src` je pattern-rule adresář vázaný na
      `../Core/Src/%.c`, takže soubory mimo něj potřebují vlastní explicitní pravidla,
      stejný idiom jako lwIP), `-I CM7/Core/Inc`. CM4 obraz **84,6 → 118,6 KB**.
- [x] **Čtecí i zapisovací půlka přesunuty do `ipc_scpi.c`** (nový soubor, linkuje se
      do OBOU jader): `ipc_scpi_src_from_snap()` (přesunuto z `ipc.c` beze změny logiky)
      + nové `ipc_scpi_set_cfg()` — přesná signatura `scpi_src_t.set_cfg`, validuje
      lokálně (brána/kanál v rozsahu) a pošle `IPC_CMD_CFG_SET`; **nečeká na odpověď**
      (žádné `osDelay`/`HAL_Delay` v souboru sdíleném oběma jádry — busy-wait v obsluze
      TCP by byl horší než prostě nevědět hned).
- [x] 🔴 **Skutečný nález, ne jen wiring:** `scpi.c` mělo **jen dva** `#if defined(CORE_CM7)`
      bloky (includy + CM7 backend), ale `SYSTem:DATE/TIME` a `DISPlay:BRIGhtness` byly
      **mimo** — sahaly přímo na `g_rtc_text`, `g_rtc_set_*`, `g_brightness`,
      `g_sys_cfg_dirty`. Tvrzení „jádro ověřeně kompiluje jako `-DCORE_CM4`" platilo jen
      pro izolovaný test mimo obraz CM4; ve skutečném CM4 firmwaru build spadl. Opraveno
      přidáním `#else` větví → CM4 vrací **SCPI-99 `-241 "Hardware missing"`** (nový kód
      v `scpi_err_msg`, doteď nepoužitý) — věcně správně, displej i RTC jsou fyzicky
      jen na CM7 a IPC cestu mít nemají (nejsou „přístroj", viz 1.6).
- [x] **Kritérium (upravené — bez TCP se nedá spustit `scpi ipc SET` ze sítě):** CM4
      spustí `scpi_selftest()` jednou při bootu (pure-logic, fabrikuje si vlastní
      `scpi_src_t`, bezpečné bez ETH/lwIP) a výsledek publikuje přes **IPC v7**
      (`scpi_selftest_ok`) → UART `status` na CM7 ukáže `SCPI(CM4): selftest PASS`.
      CM4 nemá konzoli, takže IPC je jediný kanál k ověření zvenčí — stejný idiom jako
      PHY ID u F3.
      Navíc zapojeno na CM7 straně: `scpi ipc` (UART test příkaz) teď nastavuje
      `src_ipc.set_cfg = ipc_scpi_set_cfg`, takže `scpi ipc CALC:MATH:M 2` skutečně
      prochází cmd ringem (dřív by SET tiše selhal na NULL callbacku).
- [x] `--gc-sections` z CM4 obrazu **zahodil** `ipc_scpi_set_cfg`/`ipc_scpi_src_from_snap`
      (nikdo je tam ještě nevolá za běhu) — očekávané, ověřeno `nm`. Skutečně linknuté
      budou až s W3, kdy TCP server přiřadí `src.set_cfg`.
- **Návrat:** odebrat `SCPI/subdir.mk` z CM4 makefile + volání `scpi_selftest()` v `main.c`.

### W3 — ✅ TCP 5025 (F6) — kód hotový 2026-08-23, HW test čeká

- [x] Nový soubor `CM4/LWIP/App/scpi_tcp.c` (+ `.h`): raw lwIP API (`tcp_new/bind(5025)/
      listen_with_backlog/accept/recv/err`). Statický pool **4 spojení** (`SCPI_TCP_MAX_CONN`,
      `MEMP_NUM_TCP_PCB=10` má rezervu), žádný malloc navíc mimo to, co lwIP dělá pro
      pbuf/pcb interně. Per spojení **vlastní `scpi_ctx_t`** (proto existuje — souběžná
      spojení nesmí sdílet chybovou frontu/stavové registry).
- [x] **`scpi_src_t` se plní ZA BĚHU pro KAŽDÝ příkaz** (ne jen jednou při připojení) —
      `ipc_cm4_read()` čte aktuální snapshot, `ipc_scpi_src_from_snap()` ho převede.
      Klient může poslat druhý příkaz o minuty později, snapshot se mezitím mění ~2 Hz.
- [x] **W0 aplikováno přes IPC v8** (`web_ctrl_en`, bývalý `_pad_cfg` — rozšíření zdarma,
      velikost struktury beze změny, ale `IPC_VERSION` stejně bumpnuto na 8 kvůli detekci
      nesouladu bank): `src.set_cfg = (have && snap.web_ctrl_en) ? ipc_scpi_set_cfg : NULL`.
      Když je ovládání zakázané, **`set_cfg` zůstává NULL** → SET tiše selže existující
      ochranou parseru (`-230 "Data corrupt or stale"`) — žádná nová chybová cesta.
      ⚠️ Vypínač řídí i TCP 5025, ne jen web — jméno/heslo (HTTP Basic) přijde až s W4/W5.
- [x] `src.read_log = NULL` (MMEM:DATA? — #26, odloženo; selže čistě).
- [x] Odpověď: `\r\n` terminátor (SCPI-99 socket transport konvence), `TCP_WRITE_FLAG_COPY`
      (buffer je sdílený `static`, lwIP si ho musí zkopírovat před dalším příkazem).
      Bez fronty/`tcp_sent` pacing — `TCP_SND_BUF` (4×MSS ≈ 5,8 kB) je o řád větší než
      max. odpověď (200 B), takže jediný `tcp_write` vždy stačí; W4 (velké HTML) bude
      potřebovat chunkované odesílání, tohle ne.
- [x] Server naslouchá **hned po `netif_add`**, nezávisle na stavu linky/DHCP — spojení
      prostě nikdo nenaváže, dokud není IP, ale socket je připravený od startu.
- **Kritérium (nezměněno, čeká na HW):** `*IDN?` a `MEAS:FREQ?` z PC přes `nc`/PuTTY/VISA
      na port 5025 vrátí totéž, co USB. `ping -f` souběžně nesmí ovlivnit odezvu.
      Testovací postup: `HW_OVERENI_PRUCHOD.md` §7d.
- **Návrat:** nevolat `scpi_tcp_init()` z `lwip_app_init()`.

⚠️ **Build past objevená cestou:** `scpi.c`/`meas_math.c`/`ipc_scpi.c` (W2) jsem nejdřív
zapojil jen ručně do generovaného `Debug/SCPI/subdir.mk`, bez `<link>` v `CM4/.project` —
IDE o nich nevědělo, takže KAŽDÝ normální build z IDE (ne jen Close/Open) přegeneroval
`makefile`/`objects.list` bez nich. Opraveno přidáním `<link>` **pod jménem `Core/Src/scpi.c`**
(ne novým adresářem) — po jednom Close/Open je IDE převzalo do už spravovaného `Core/Src`
a našlo si `-I CM7/Core/Inc` samo z `.cproject`. `Debug/SCPI/` je teď mrtvý adresář, smazaný.
Poučení pro `CUBEMX_CHECKLIST.md`: **cizí soubory linkovat pod jméno existující spravované
složky, ne pod nový název** — obchází to nutnost, aby se IDE učilo o nové složce.

### W4 — ✅ HTTP kostra + `GET /api/state` + `POST /api/scpi` — kód hotový 2026-08-23, HW test čeká

- [x] Nový soubor `CM4/LWIP/App/httpd_min.c` (+ `.h`): raw lwIP API na portu **80**,
      statický pool **3 spojení** (méně než SCPI's 4 — prohlížeč typicky drží 1–2 najednou).
      `Connection: close` na každé odpovědi (žádný keep-alive — zjednodušuje stav na jedno
      spojení = jeden požadavek, stejně jako `scpi_tcp.c`).
- [x] **Odesílání jedním `tcp_write`, bez `tcp_sent` pacing** — záměrná zjednodušující volba,
      ne opomenutí: `TCP_SND_BUF` (4×MSS ≈ 5,8 kB) je o řád větší než max. odpověď
      (JSON ~1 kB, SCPI řádek ~200 B). ⚠️ Až přijde W5 (statická SPA stránka, řádově
      desítky KB), **tahle zkratka přestane stačit** — bude potřeba chunkované
      odesílání přes `tcp_sent`, přesně jak plán původně předpokládal.
- [x] **Strop 3 spojení** — čtvrté se slušně odmítne (`tcp_abort`), nezůstane viset.
      Bez timeoutu na zapomenutá spojení (HTTP/1.1 `Connection: close` znamená, že
      server sám zavírá hned po odpovědi — nemůže tedy "viset" na zapomenutém klientovi).
- [x] **Parser požadavku = čistá funkce** (`httpd_parse_request`, žádná síť, žádné
      globály) → **`httpd_min_selftest()`**, 5 testovacích vektorů (kompletní GET,
      kompletní POST s `Content-Length` case-insensitive, neúplná hlavička → "potřeba
      víc dat", poškozený request-line → -1, příliš dlouhá cesta se ořízne místo pádu).
      Spouští se jednou při bootu CM4 (vedle `scpi_selftest()`) a výsledek jde do **IPC v9**
      (`httpd_selftest_ok` — další znovupoužitý padding bajt, `ipc_cm4_status_t` pořád
      stejná velikost) → `status` → `HTTP(CM4): selftest PASS`.
- [x] `GET /api/state`: JSON stavěný **přes `scpi_src_t`** (`ipc_scpi_src_from_snap`),
      NE přímo ze syrového snapshotu — validita polí (`SCPI_V_*` bity → `null` v JSON)
      je tak **doslova stejná logika jako SCPI dotazy**, ne druhá kopie. Čísla bez `%f`:
      Hz přes nově vystavené `fmt_scpi_hz_d` (sdíleno se `scpi.c`, včetně jeho ochrany
      proti přetečení), teploty/napětí přímou celočíselnou aritmetikou (už jsou
      pre-škálované v `scpi_src_t`, žádná float konverze potřeba).
- [x] `POST /api/scpi`: tělo (max `HTTPD_BODY_MAX`=96 B, ochrana proti nadměrnému
      `Content-Length`) jde **stejnou cestou jako TCP 5025** — živý snapshot,
      `set_cfg = ipc_scpi_set_cfg` jen když `web_ctrl_en` (W0), `read_log = NULL`.
      Žádná druhá ovládací sada, přesně jak žádá §2 plánu.
- **Kritérium (nezměněno, čeká na HW):** `curl -d "MEAS:FREQ?" http://<IP>/api/scpi`
      vrátí totéž co USB; `GET /api/state` souhlasí s displejem i s `MEAS:FREQ?`.
      Testovací postup: `HW_OVERENI_PRUCHOD.md` §7e.
- **Návrat:** nevolat `httpd_min_init()` z `lwip_app_init()`.

### W5 — ✅ SPA — HOTOVO, ověřeno na HW 2026-08-23

- [x] **`SPA_HTML[]`** (~12 kB) v `.rodata` CM4 — HTML+CSS+JS pohromadě, žádné externí
      zdroje (žádný CDN, žádný build krok). ⚠️ **Bez jediné dvojité uvozovky uvnitř** —
      HTML atributy i JS řetězce záměrně jen s jednoduchými, aby šel celý blok zapsat
      jako C řetězcový literál (sousední řetězce se v C spojují) bez escapování.
      ⚠️ **A bez zpětných lomítek** — `\d`/`\B` v JS regexu by C překladač vzal jako
      neznámou escape sekvenci, takže v JS **nejsou žádné regulární výrazy** (oddělovač
      tisíců psaný ručně). Atributy skládané v JS přes `innerHTML` jsou **bez uvozovek**
      (`class=cell`), takže dvouhodnotová třída (`class='v na'`) nejde zapsat — stav se
      proto nese `data-` atributem (`data-st=bad`, `data-na=1`) a CSS ho čte selektorem
      `[data-st=bad]`. Události přes `addEventListener` + `data-` atributy, ne `onclick=`.
- [x] **Přepracovaný vzhled (2026-08-23)** — velký headline kmitočtu, stavové pilulky
      (FPGA link / GPS / reference / RUN), segmentové přepínače brány a vstupu
      **zvýrazňující skutečný stav ze snapshotu** (`set_gate_idx`/`set_chan`), karty
      měření / teploty / napájení, SCPI konzole.
- [x] **Dashboard s grafy (2026-08-24)** — stránka 12 → **29 kB**, CM4 obraz 145 → 162 kB.
      **4 živé grafy** z klientské historie (`H`, kruhový buffer 3600 vzorků = 1 h při 1 Hz),
      okno 1/5/15/60 min; GPS donut, bargrafy s nominálem, konzole s logem, přepínač
      tmavého/světlého vzhledu, indikátor stáří dat.
      - Kmitočet se kreslí jako **odchylka od průměru okna** — v absolutních Hz by na
        10 MHz nebylo vidět nic. Napájení jako **% od nominálu**, jinak by 12 V zploštilo
        ostatní větve na nulu.
      - ⚠️ **Historie žije jen v prohlížeči** (F5 = reset). Delší historie by musela z
        datalogu přes `MMEM:DATA?` — to je kandidát na W6, ne dnešní stav.
      - **SVG bez uvozovkových potíží:** `viewBox='0 0 100 100'` + `preserveAspectRatio='none'`
        + `vector-effect:non-scaling-stroke`; popisky os jsou HTML overlay (SVG `<text>` by se
        roztažením zdeformoval), body se plní `setAttribute`, tedy mimo `innerHTML`.
      - **`rf_dbm` nově v JSON** — počítá **server** týmž vzorcem i podmínkou jako
        `MEAS:POWer?`; klientský přepočet by znamenal druhou kopii kalibrace AD8307.
      - Ověřeno staticky: žádné chybějící `id`, žádná nedefinovaná CSS proměnná,
        vyvážené závorky, **syntaxe JS zkontrolovaná parserem** (`cscript //E:JScript`
        nad kódem obaleným do nevolané funkce).
      ⚠️ Když kmitočet chybí, stránka **vypíše důvod** (STOP / SPI link DOWN / ztráta
      signálu) — bez toho vypadá správné `null` (zlaté pravidlo, 1.7) jako porucha webu.
      ⚠️ **Displej ukazuje SIMULACI** (`freq_step()`, STATUS #2), web reálná data →
      bez FPGA desky displej kmitočet má a web správně `null`. Není to chyba webu.
- [x] **Přihlášení se ověřuje** — `login()` pošle `*IDN?` a přečte `auth_debug.match`
      z `/api/state`, takže špatné heslo se pozná hned. Dřív se jen tiše uložilo do
      `localStorage` („login saved") a chyba se projevila až u prvního SET jako `-230`.
- [x] 🔴 **Past `placeholder` vs. `value` (HW test 2026-08-23).** První verze měla pole
      jména jako `placeholder=admin` — vypadá to jako předvyplněná hodnota, ale `value`
      je prázdné. `auth()` navíc při prázdném jménu vracela `null`, takže prohlížeč
      **neposlal žádnou hlavičku** a stránka hlásila „Neplatné jméno nebo heslo",
      přestože heslo bylo správně. Poznalo se to podle `auth_debug.header_present=false`.
      Opraveno: pole má **`value=admin`** (uživatelské jméno je stejně napevno „admin",
      okno PŘÍSTUP ho jen zobrazuje), `auth()` pošle hlavičku, jakmile je vyplněné
      cokoli z dvojice, a hláška teď **rozliší** „prohlížeč neposlal přihlášení" od
      „neplatné jméno nebo heslo" (u druhé i očekávanou/přijatou délku v bajtech).
      ⚠️ Nová stránka používá jiné klíče `localStorage` (`gu`/`gp`) než původní
      (`gpsdo_u`/`gpsdo_p`), takže se dřív uložené jméno nepřeneslo — to past ještě
      zhoršilo. **Poučení: `placeholder` není předvyplněná hodnota.**
- [x] Poll `/api/state` 1×/s (`fetch`+`setInterval`), ovládání přes `/api/scpi`
      (RUN→`INIT`, STOP→`ABOR`, brána→`SENS:FREQ:GATE <s>`, kanál→`SENS:FREQ:CHAN <n>`).
- [x] **Basic Auth na zápisové cestě** — přihlašovací jméno/heslo z okna PŘÍSTUP teď
      proudí přes **IPC v10** (`web_user`/`web_pass`, skutečný růst snapshotu o 36 B,
      ne recyklovaný padding — poprvé od v8). SPA ukládá jméno/heslo do
      `localStorage`, posílá `Authorization: Basic base64(user:pass)` na každý
      `POST /api/scpi`. Server (`check_auth` v `httpd_min.c`) dekóduje a **bajtově**
      porovná se snapshotem — žádný speciální případ pro prázdné heslo (prázdné
      nikdy neprojde, přesná shoda řetězců to zaručuje sama).
      ⚠️ **TCP 5025 Basic Auth nemá** (VISA raw socket nezná HTTP hlavičky) — spoléhá
      jen na `web_ctrl_en`. Pro `POST /api/scpi` je tedy podmínka **`web_ctrl_en`
      A SOUČASNĚ platné jméno/heslo** — pro TCP 5025 jen `web_ctrl_en`.
- [x] UI skryje/zablokuje ovládací tlačítka, když `web_ctrl_en=false` v `/api/state`.
- [x] 🔴 **Asynchronní odesílání po částech (`tcp_sent`), NE jeden `tcp_write` jako W3/W4.**
      SPA stránka (4,3 kB) je pod `TCP_SND_BUF` (~5,8 kB), ale návrh je obecný a
      nezávisí na tom, že se to tentokrát vejde: každé spojení má frontu
      (hlavička→tělo, `pump_send()` volaný z `on_accept` i z `tcp_sent` callbacku).
      **Tělo JSON/SCPI je vždy ve buffer vlastněném danou connection** (`c->bodybuf`),
      **NE ve sdíleném static scratch bufferu jako ve W3/W4** — se sdíleným bufferem
      by při souběžném rozesílání dvou spojení jedno přepsalo tělo druhého uprostřed
      posílání. SPA stránka naopak ukazuje přímo do `.rodata` konstanty (bezpečné
      sdílet mezi spojeními, nikdy se nemění).
- [x] Selftest (`httpd_min_selftest`) rozšířen o base64 dekodér a porovnání
      credentials (dobré heslo → projde, špatné → zamítne, chybějící hlavička → zamítne).
- **Kritérium (nezměněno, čeká na HW):** z mobilu na LAN jde přečíst stav i zmáčknout
      STOP (po zadání jména/hesla). Testovací postup: `HW_OVERENI_PRUCHOD.md` §7f.

### W6 — Dotažení
SSE/WebSocket místo pollu, mDNS (`gpsdo.local`, STATUS #30), stažení datalogu.
⚠️ Log **přes `datalog_read_back()`**, ne přes FatFs — viz STATUS #26 (jinak by se rvali
dva vlastníci FatFs).

---

## 3. Stav (2026-08-23) a co dál

**W0, W1, W2 HW-OVĚŘENÉ** (§7b+7c v `HW_OVERENI_PRUCHOD.md`): degradace bez ETH OK, PHY ID
čte CM4, okno PŘÍSTUP OK, `ipccmd run/gate/chan` OK (`run` mělo mezitím opravenou chybějící
validaci rozsahu — `run 9` dřív tiše prošlo jako "běž", teď vrací ERR jako GATE/CHAN),
`SCPI(CM4): selftest PASS` OK, link+DHCP+ping OK.

**W3 (TCP 5025 = F6) — kód hotový, HW test čeká.** `CM4/LWIP/App/scpi_tcp.c`: raw lwIP
server, 4 souběžná spojení, `scpi_src_t` se čte živě z IPC pro každý příkaz. Testovací
postup: `HW_OVERENI_PRUCHOD.md` §7d.

**W4 (HTTP + `/api/state` + `/api/scpi`) — kód hotový, HW test čeká.** `CM4/LWIP/App/httpd_min.c`:
raw HTTP/1.1 server port 80, 3 spojení, `Connection: close`. `GET /api/state` staví JSON
přes `scpi_src_t` (stejná validita jako SCPI, ne druhá logika), `POST /api/scpi` jde
stejnou cestou jako TCP 5025. Parser požadavků je čistá funkce s vlastním selftestem
(`httpd_min_selftest`) → **IPC v9** → `status` → `HTTP(CM4): selftest PASS`.
Testovací postup: `HW_OVERENI_PRUCHOD.md` §7e.

**W5 (SPA) — kód hotový, HW test čeká.** `CM4/LWIP/App/httpd_min.c` doplněn o
`SPA_HTML[]` (~4,3 kB, `.rodata`) servírovanou na `GET /`, přihlašovací jméno/heslo
přes **IPC v10** (`web_user`/`web_pass` — skutečný růst snapshotu, ne recyklovaný
padding) a **asynchronní odesílání po částech přes `tcp_sent`** (jednoduchý
jednorázový `tcp_write` z W3/W4 stačil jen na malé JSON/SCPI odpovědi, ne na celou
stránku). Testovací postup: `HW_OVERENI_PRUCHOD.md` §7f.

**PLÁN W0–W5 JE TÍM KÓDEM DOKONČEN.** Zbývá jediné: **HW ověření celého řetězu** —
nic z F3/F5/W0–W5 dosud neběželo na skutečném hardwaru. Doporučení beze změny už
z minula: projít `HW_OVERENI_PRUCHOD.md` §7b→7c→7d→7e→7f **v tomhle pořadí** (každá
vrstva stojí na té předchozí, takže chyba se dá připsat vždy jen jedné vrstvě).
W6 (SSE/mDNS/stažení logu) zůstává jako volitelné dotažení až po HW ověření.

🔴 **`IPC_VERSION` 10 → obě banky.** Rostlo v této session 6→7→8→9→10 (scpi_selftest_ok,
web_ctrl_en, httpd_selftest_ok, web_user/web_pass) — než půjdeš na HW, ověř `status`,
že nehlásí `⚠ IPC NESOULAD`.
