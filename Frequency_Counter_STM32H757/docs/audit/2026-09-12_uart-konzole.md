# Audit: UART konzole — parser příkazů (2026-09-12)

- **Commit:** `8330b4c`
- **Jádro / doména:** CM7 / UartTask (priorita Normal 24, zásobník 4096 B,
  **záměrně mimo watchdog** — proto tu smí být blokující práce)
- **Soubory (čteny celé):** `CM7/Core/Src/freertos_task_uart.c` (**2 365 ř.**),
  z toho `UartTask_run` sama **1 966 ř.** Dohledáni konzumenti a protějšky nálezů:
  `scpi.c` (rámce `scpi_process`/`scpi_exec_one`), `datalog.c`
  (`datalog_set_period_s`), `freertos.c` (velikost zásobníku), `usb_console.c`
  (zdroj znaků), `fpga_freq.h` (`fpga_sim_set`).
- **Projité sekce checklistu:** **D** (souběh, zásobníky, blokující operace v tasku
  bez watchdogu), **E** (ošetření chyb, meze polí, **neošetřený vstup**).
- **Neprojité (a proč):** **A** (nekonfiguruje hodiny), **B** (mezijádrové věci jen
  čte přes hotové API), **C** (žádné DMA ani sdílené buffery), **F**, **G**, **H**.

## Souhrn

Poslední velký neauditovaný soubor projektu a zároveň **největší** (2 365 ř.).
Je to třetí vstup zvenčí po modulech 12 a 13 — a jediný, kde vstup přichází
z ruky člověka, ne po síti.

**Soubor je psaný nadprůměrně opatrně** a na řadě míst přímo ukazuje, že se
z minulých chyb poučil: `stacktest_overflow` je vyčleněná funkce *s vysvětlením*,
proč nesmí být lokálem (frame by narostl vždycky), `bgcheck` má buffer v `.bss`
s poznámkou „(UartTask má 4 kB)", `scpi ipc` má **všechno statické** a komentář
u toho počítá, kolik by to jinak stálo zásobníku. Skenování I2C ustupuje
scheduleru (F-0020), SD práce se obaluje `sd_blocking_begin/end` jako celek,
`eth` má pojistku proti kolizi s CM4.

Nálezy jsou proto o dvou věcech: **(1) hlavní vstupní buffer má 32 B a delší
příkaz se tiše utne a PROVEDE** (F-0073) — čtvrtý výskyt téže třídy za tři dny,
tentokrát na cestě, kudy chodí všechny příkazy; a **(2) poučení o zásobníku se
neuplatnilo všude** (F-0074) — `scpi ipc` má statické buffery, `scpi` ne, a na
desce to 2026-09-12 skončilo na **168 B volného zásobníku**.

**Verdikt: podmíněně funkční.** Za běžného používání (příkazy do 31 znaků)
funguje správně; obě hlavní vady se projeví až u delšího vstupu.

---

## Fáze oprav (2026-09-12) — 3 nálezy opraveny, 1 částečně, 1 odložen do TODO

| commit | nálezy | co se změnilo |
|---|---|---|
| `b1aa262` | F-0073, F-0075, F-0076 | `RX_BUF_SIZE` 32 → **96** a utnutý příkaz se **neprovede** (+ hláška a počítadlo v `status`); `fpgasim on` má meze už při akumulaci + strop 4 GHz; `fpgaraw` kontroluje `p` před `snprintf` |
| `scripts/` | F-0077 (b) | kontrola rámce `UartTask_run` nad `.elf` v `check_lessons.sh`, mez 1024 B (dnes 700 B) |
| TODO #243 | **F-0074**, F-0077 (a) | **odloženo rozhodnutím uživatele** — zásobník se nejdřív konzistentně zvětší v `.ioc`; statické lokály jsou konkurenční oprava téhož a nedělají se zároveň |

**Ověřovací řetězec (§F5.2):** `./scripts/build.sh Release CM7` **0 varování**,
`python tools/audit.py` **92 OK / 0 selhání / 2 s varováním** (baseline),
CM7 `.text` 599 760 → **600 104 B** (+344 B), `.bss` +64 B.
Symboly v obrazu: `nm` ukazuje `RxBuffer` **96 B** a `s_rx_trunc`/`s_rx_trunc_n`
v `.bss`; všechny tři nové řetězce jsou v `.elf`.
⬜ **NEOVĚŘENO NA HW** — čeká na flash a **power-cyklus**, spolu s F-0063 (okno
CHYBY) a `21ac04e`. `IPC_VERSION` se nemění.

🔴 **Vlastní chyba zachycená před commitem:** odmítnutí utnutého příkazu jsem
nejdřív napsal jako `continue;` v `for(;;)` — to by přeskočilo `sd_export_service()`,
`datalog_erase_service()`, `membench_service()`, `qspi_req_service()` i `osDelay(1)`
na konci smyčky. Přepsáno na první člen `else if` řetězu, zapsáno jako **L-0033**.

🔴 **Kontrola rámce napoprvé nefungovala** (vracela 0 B — awk četl `m[2]` místo
`m[3]`). Odhalila to až **pozitivní kontrola**, která je podle **L-0020** součástí
té lekce, ne volitelný doplněk. Ověřeno obojím směrem: mez 256 B zakřičí, neexistující
symbol mlčí a nespadne.

---

### F-0073 [S3] Příkaz delší než 31 znaků se tiše utne — a **provede** se zkrácený

- **Místo:** `CM7/Core/Src/freertos_task_uart.c:53` (`RX_BUF_SIZE 32`),
  `:375-376` (`RxBuffer`/`RxIndex`), `:2346-2352` (příjem znaku),
  `:426` (ukončení řádku)
- **Popis:** Při plném bufferu se znak **zahodí bez jakékoli stopy**:
  ```c
  else {
      if (RxIndex < sizeof(RxBuffer) - 1) {
          putchar(rxChar);          /* echo je UVNITŘ podmínky */
          RxBuffer[RxIndex++] = rxChar;
      }
      /* else: nic — ani echo, ani hláška, ani příznak */
  }
  ```
  Enter pak provede to, co v bufferu zbylo, jako plnohodnotný příkaz.
- **Důkaz:**
  1. `RX_BUF_SIZE` je **32**, takže se vejde **31 znaků** příkazu.
  2. **Reálné příkazy, které tu mez překročí** (obojí jsou legitimní tvary
     z vlastního SCPI stromu, ne umělé):
     | příkaz | délka | co se doopravdy provede |
     |---|---|---|
     | `scpi SENSe:FREQuency:APERture 10` | **32** | `… APERture 1` → hradlo **1 s místo 10 s** |
     | `scpi CALCulate:LIMit:LOWer 10000` | **32** | `… LOWer 1000` → mez **1000 místo 10000** |
     | `scpi SENSe:FREQuency:APERture 1` | 31 | projde správně (těsně) |
     `SENSe:FREQuency:APERture` je přitom **povinný alias** pro `GATE`, protože
     ho hledají VISA/IVI ovladače (viz `scpi.c`, komentář u `APERture`) — tedy
     přesně ten tvar, který nástroj pošle.
  3. **Uživatel nemá jak poznat, že se něco ztratilo.** Echo je uvnitř téže
     podmínky, takže po 31. znaku se přestane vypisovat; odpověď na zkrácený
     příkaz je přitom normální („OK" nebo hodnota). Žádné počítadlo, žádná hláška.
  4. 🔑 **Interakce s F-0069, kterou je potřeba znát:** v modulu 13 se
     `scpi_process_ctx` zvětšil `sub[]` na 96 B, aby se složená zpráva neutínala.
     Na cestě z konzole je to **nedosažitelné** — sem se víc než 31 znaků nikdy
     nedostane. Ta oprava má smysl jen pro CM4 (TCP 5025 a HTTP mají
     `rxbuf[2048]`), což je dobře, ale u konzole zůstává mez o dva řády nižší.
- **Dopad:** **Tichá záměna nastavené hodnoty** — přístroj potvrdí, že příkaz
  proběhl, a nastaví něco jiného. Je to **čtvrtá instance téže třídy** po F-0056
  (`/api/log`), F-0058 (`hdr_len`) a F-0069 (složená SCPI zpráva), a z nich
  nejhorší co do následku: u předchozích šlo o zobrazení nebo o odpověď, tady
  o **konfiguraci měření**.
  ⚠️ Dosažitelné jen z lokální konzole (USB CDC), tedy vyžaduje fyzický přístup —
  proto S3, ne S2.
- **Reprodukce:** `scpi SENSe:FREQuency:APERture 10` na konzoli, pak
  `scpi SENS:FREQ:GATE?` → vrátí `1`, ne `10`. Echo se zastaví na 31. znaku.
- **Návrh opravy:** Dvě části, obě malé:
  1. **`RX_BUF_SIZE` 32 → 96** (sladit s `sub[]` v `scpi_process_ctx`, ať mez
     drží na obou koncích téže cesty). Cena je 64 B v `.bss`, ne na zásobníku —
     `RxBuffer` je `static` (`:375`).
  2. **Přetečení přestat dělat tiše:** při plném bufferu nastavit příznak a na
     Enter příkaz **odmítnout** hláškou (`ERR command too long`) místo provedení,
     plus počítadlo do `status` (L-0017). To je podstatnější než samotné zvětšení —
     jakákoli mez jde překročit, ale výsledkem nesmí být provedení něčeho jiného.
- **Riziko opravy:** nízké. ⚠️ Ověřit, že `RxBuffer` je opravdu `static`
  (je, `:375`) — jako lokál v `UartTask_run` by +64 B šlo na zásobník, kterého
  je podle F-0074 nedostatek.
- **Vztah k lekcím:** **`L-0026`** (rozpočet pevného bufferu a konec tichého
  ořezu) — čtvrtý výskyt; **`L-0017`** (tichý přeskok jen s počítadlem);
  nově **`L-0033`** (odmítnutí patří do větvení, ne do řízení smyčky).
- **Stav:** **opraveno 2026-09-12** (`b1aa262`) — `RX_BUF_SIZE` 32 → **96**
  (slaďuje mez se `sub[96]` ve `scpi_process_ctx`), přetečení nastaví `s_rx_trunc`
  a **řekne to** (`[prilis dlouhy prikaz - zbytek radku se ignoruje]`), Enter
  takový řádek **odmítne** (`ERR prikaz delsi nez 95 znaku - NEPROVEDEN`) a počet
  odmítnutí je v `status` (`KONZOLE: N prikazu odmitnuto`).
  ⚠️ **Opraveno jinak, než nález navrhoval v jedné věci:** odmítnutí je **první
  člen existujícího `else if` řetězu**, ne `continue` ve smyčce — `continue` by
  přeskočil i `sd_export_service()`, `datalog_erase_service()`, `membench_service()`,
  `qspi_req_service()` a `osDelay(1)` na konci `for(;;)`. Zachyceno před commitem,
  zapsáno jako **L-0033**.
  ⬜ **Neověřeno na HW** (čeká na flash + power-cyklus).

---

### F-0074 [S3] `scpi` drží buffery na zásobníku, ačkoli `scpi ipc` o pár řádků výš je má statické — a komentář u něj vysvětluje proč

- **Místo:** `freertos_task_uart.c:1204` (`char resp[128]` v obsluze `scpi`)
  vs. `:1127-1135` (obsluha `scpi ipc`); `:1710` (`uint8_t buf[512]`
  v `qspispeed`); rámce v `scpi.c` (`scpi_process` 492 B, `scpi_exec_one` 268 B)
- **Popis:** Obsluha `scpi ipc` má **všechny** buffery `static` a komentář u toho říká:
  > *„⚠️ VSE staticke, nic na stack UartTasku. `scpi_src_t` ma ~200 B a dva
  > odpovedni buffery dalsich 320 — dohromady pres 500 B na tasku, ktery mel
  > pri mereni 1580 B volnych. Projekt uz jednou na presne tohle doplatil."*
  
  Obsluha běžného **`scpi`** o 70 řádků níž má `char resp[128]` na zásobníku —
  a volá `scpi_process`, jehož rámec je **492 B** (drží `scpi_src_t src` lokálně).
  Tedy přesně ta situace, před kterou ten komentář varuje, jen o dvě obsluhy vedle.
- **Důkaz:**
  1. **Změřeno na desce 2026-09-12** (po flashi modulu 13): `status` →
     **`stack Uart free 168 B`** ze 4096 B, a to při běžném `scpi …`, **bez**
     `selftest`. F-0055 přitom pracoval s 976 B.
  2. **Řetěz rámců** (objdump nad obrazem CM7):
     `UartTask_run` 700 B → `scpi_process` **492 B** → `scpi_process_ctx` →
     `scpi_exec_one` **268 B** ≈ **1,6 kB jen v rámcích**, k tomu `resp[128]`
     a printf řetěz (`_svfiprintf_r` 112 B).
  3. **Vzor je v souboru zavedený a jinde dodržený:** `bgcheck` má
     `static uint32_t bgsum[120]` s poznámkou „(.bss, ne stack (UartTask ma 4 kB))"
     (`:1884`), `stats` má `static TaskStatus_t ta[12], tb[12]` (`:1951`),
     `datalog csv` má `static datalog_rec_t bulk[]` (`:1353`). Výjimky jsou
     `scpi` a `qspispeed` (`uint8_t buf[512]`, `:1710`).
  4. Dílčí úleva už proběhla (`21ac04e` přesunul `sub`/`rb` ve `scpi_process_ctx`
     do `.bss`, −140 B), ale **`scpi_process` je mimo tenhle soubor** a drží
     nejvíc.
- **Dopad:** 168 B rezervy je na hranici, kde F-0055 (přetečení při `selftest`)
  přestává být hypotetické. **Není to nová vada**, je to zpřesnění F-0055 o to,
  že nestačí řešit selftest — SCPI cesta spotřebuje skoro totéž.
  ⚠️ `qspispeed` s 512 B je nejhorší jednotlivý lokál v souboru, ale běží
  samostatně (ne zároveň se SCPI), takže sám o sobě vrchol netvoří.
- **Reprodukce:** `scpi *IDN?` a hned `status` → řádek `stack Uart free`.
  Ověřeno na desce 2026-09-12.
- **Návrh opravy:** V tomhle souboru je to triviální (`static char resp[128]`,
  `static uint8_t buf[512]`) a ušetří ~640 B. **Podstatnou část ale drží
  `scpi_process` (492 B) v `scpi.c`** — tam je oprava „dát `scpi_src_t` do `.bss`",
  což je bezpečné ze stejného důvodu jako u `sub`/`rb` (funkce není reentrantní,
  na CM7 ji volá jen UartTask, na CM4 jen hlavní smyčka).
  ⚠️ To už je zásah do modulu 13 a **souvisí s rozhodnutím o F-0055** — proto to
  patří do skupiny B, ne A.
- **Riziko opravy:** nízké (v tomto souboru), střední u `scpi_process`
  (sdílený s CM4, ale tam je zásobník 44 kB, takže by šlo jen o `.bss` +200 B).
- **Vztah k lekcím:** **`L-0012`** (dvě symetrické instance — jedna poučená,
  druhá ne) a **`L-0022`** (u obrany musí být vyjmenované, kde platí; ten komentář
  u `scpi ipc` popisuje pravidlo, které se o dvě obsluhy vedle neuplatnilo).
- **Stav:** **opraveno 2026-09-20** na vyslovne zadani uzivatele.
  `resp[128]` (obsluha `scpi`) a `buf[512]` (`qspispeed`) jsou nove `static`, tedy
  v `.bss` — presne jak to uz dela obsluha `scpi ipc` o par set radku vys, vcetne
  zduvodneni v komentari. Dolozeno v obrazu: `resp.14` 128 B a `buf.1`/`buf.11` 512 B
  v `.bss`.
  🔑 Duvod je **merіtelny, ne kosmeticky**: `UartTask_run` je JEDNA funkce o ~2000
  radcich, takze GCC rezervuje ramec pri vstupu a lokal KTERÉKOLI vetve zvedne ramec
  VSEM cestam (**L-0035**). Tim se zaroven dorovnava cast (a) nalezu **F-0077**.
  ⚠️ **Prijate riziko, ktere je potreba znat pri HW pruchodu:** TODO #243 pred timhle
  varovalo — je to **druha konkurencni oprava tehož problemu** jako zvetseni
  zasobniku UartTasku v `.ioc` (1024 -> 2048 slov, tedy 8192 B), a to jeste NEBYLO
  overeno na HW. Kdyz `selftest` z konzole ted projde, **nepozna se, ktera z tech dvou
  zmen pomohla**. Uzivatel tu meritelnost vedome obetoval. Do `STATUS.md` #243 to je
  zapsane, aby se to pri vyhodnoceni nezapomnelo.
  ⬜ **neovereno na HW** (kriterium: `selftest` z konzole dobehne „16/16 PASS" bez
  resetu a `status` -> `Reset:` nehlasi `stack:UartTask`; `stats` -> volny stack
  UartTasku).

---

### F-0075 [S3] `fpgasim on <Hz>` nemá mez → přetypování `double` mimo rozsah `uint64_t` (UB)

- **Místo:** `freertos_task_uart.c:1757-1765` (parsování),
  `:1767` a `:1780` (`(uint64_t)(hz * 100000.0)`)
- **Popis:** Kmitočet se parsuje ručně bez jakékoli meze:
  ```c
  while (*p >= '0' && *p <= '9') { hz = hz * 10.0 + (*p - '0'); p++; }
  if (hz < 1.0) hz = 10000000.0;      /* jen DOLNÍ mez */
  fpga_sim_set(1, hz, noi, dr);
  char hb[32]; fpga_freq_format_val((uint64_t)(hz * 100000.0), hb, sizeof hb);
  ```
  Kontroluje se jen dolní hranice. `uint64_t` pojme do ~1,8·10¹⁹, takže
  `hz * 1e5` musí zůstat pod tím → **`hz` nad ~1,8·10¹⁴ je nedefinované chování**,
  ne jen špatné číslo.
- **Důkaz:** `fpgasim on 99999999999999999999` (20 číslic) dá `hz ≈ 1·10²⁰`,
  po vynásobení `1·10²⁵` — o šest řádů nad rozsahem `uint64_t`.
  🔑 Projekt tenhle přesný problém **jinde ošetřený má** a dokonce ho vysvětluje:
  `fmt_scpi_hz_d` (`scpi.c:105-112`) má rozsahovou pojistku s komentářem
  *„nad ~1,8e14 už je přetypování double na uint64 NEDEFINOVANÉ chování"*,
  a `fmt_scpi_period_s` (`:125-131`) taky. `fpga_freq_format_val` ji nemá
  a volající jí nic nehlídá.
- **Dopad:** Lokální konzole, tedy vyžaduje fyzický přístup a úmysl. Prakticky
  vznikne nesmyslná hodnota v emulátoru (který je sám o sobě diagnostika), ne pád.
  ⚠️ Emulovaná hodnota ale jde do `s_freq_n`, datalogu (`DATALOG_F_SIM`) a IPC
  snapshotu, takže nesmysl se rozšíří dál.
- **Reprodukce:** `fpgasim on 99999999999999999999`, pak `fpgasim` a `status`.
- **Návrh opravy:** Horní mez při parsování — přirozená hranice je strop
  tvarovače: `if (hz > 4.0e9) hz = 4.0e9;` (stejná mez jako `fmt_scpi_hz_d`),
  plus zastavit akumulaci po ~15 číslicích, aby `double` nepřetekl dřív.
  Totéž patří k `noi`/`dr` (`:1762-1764`), které mez taky nemají.
- **Riziko opravy:** nízké; `fpgasim` je diagnostický nástroj a rozumné hodnoty
  jsou o deset řádů níž.
- **Vztah k lekcím:** **`L-0030`** (mez odvoď z rozsahu cílového typu) —
  tohle je její druhý výskyt, tentokrát u `double → uint64_t` místo exponentu;
  **`L-0012`** (`fmt_scpi_hz_d` pojistku má, `fpga_freq_format_val` ne);
  nově **`L-0034`** (mez ověř PŘED použitím hodnoty).
- **Stav:** **opraveno 2026-09-12** (`b1aa262`) — mez je **uvnitř akumulační
  smyčky** (`if (hz < 1.0e12)`, obdobně `noi`/`dr`), takže k přetečení nedojde už
  při čtení číslic, plus strop `if (hz > 4.0e9) hz = 4.0e9;` shodný s
  `fmt_scpi_hz_d` (scpi.c:112) a ležící nad stropem tvarovače 1,4 GHz — nic
  platného tedy neodřízne. ⬜ **Neověřeno na HW.**

---

### F-0076 [S4] `fpgaraw`: `p += snprintf(...)` bez kontroly — latentní podtečení `size_t`

- **Místo:** `freertos_task_uart.c:1628-1633`
- **Popis:**
  ```c
  char line[64];
  int p = 0;
  for (int i = 0; i < 64; i++) {
      p += snprintf(line + p, sizeof(line) - p, "%02X ", rx[i]);
      if ((i & 0xF) == 0xF) { printf("[%02d] %s\n", i - 15, line); p = 0; }
  }
  ```
  `p` se akumuluje z návratové hodnoty `snprintf` bez kontroly. Kdyby kdy `p`
  překročilo `sizeof(line)`, výraz `sizeof(line) - p` **podteče** (je to `size_t`)
  na obrovské číslo a `snprintf` dostane nesmyslnou kapacitu.
- **Důkaz:** Dnes je to **nedosažitelné**: 16 bajtů × 3 znaky = 48 B + NUL,
  `line` má 64 B, takže `p` nepřekročí 48 a před 17. bajtem se vždy vynuluje.
  Rezerva je ale jen 16 B — stačilo by změnit formát na `"%02X:"` se čtyřmi znaky
  (16 × 4 = 64 = přesně kapacita) a podmínka by se zlomila.
  Kontrast: `uart_i2c4_probe` (`:162`) **tutéž konstrukci ošetřenou má**
  (`if (n >= 0 && (size_t)n < sizeof line)`), takže vzor v souboru existuje.
- **Dopad:** Dnes žádný. Je to vada připravená na příští úpravu výpisu — přesně
  ta třída, kterou F-0058 popsal u `hdr_len`.
- **Reprodukce:** staticky; dosažitelné až po změně formátu.
- **Návrh opravy:** Převzít vzor z `uart_i2c4_probe`: před přičtením ověřit
  `p >= 0 && (size_t)p < sizeof line`.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **`L-0015`** (mez vynutit na rozhraní), **`L-0012`**
  (v témže souboru je ošetřená i neošetřená instance); nově **`L-0034`**.
- **Stav:** **opraveno 2026-09-12** (`b1aa262`) — `if (p >= 0 && (size_t)p < sizeof line)`
  před použitím, kapacita `sizeof(line) - (size_t)p`. ⬜ **Neověřeno na HW.**

---

### F-0077 [S4] `UartTask_run` je jediná funkce o 1 966 řádcích — každý nový lokál zvedá rámec všem cestám

- **Místo:** `freertos_task_uart.c:400-2365`
- **Popis:** Celý parser je jeden `if / else if` řetěz uvnitř jedné funkce.
  Kromě čitelnosti to má **měřitelný** důsledek pro zásobník: GCC rezervuje rámec
  při vstupu do funkce, takže lokál kterékoli větve ovlivňuje **všechny** cesty.
- **Důkaz:** 🔑 **Ten mechanismus je v souboru už jednou popsaný, protože jednou
  kousl** — komentář u `stacktest_overflow` (`:381-390`):
  > *„GCC rezervuje frame VSECH lokalu funkce uz pri vstupu, takze 3600B `waste`
  > jako local v UartTask_run nafoukl JEHO ramec na 4904 B > 4096 B stack ->
  > task pretekal VZDY (uz za normalniho provozu) … HardFault s poskozenym
  > ramcem pri PRVNIM USB znaku."*
  
  Dnes je rámec `UartTask_run` **700 B** (objdump), protože GCC sloty lokálů
  v disjunktních větvích sdílí — ale to je vlastnost optimalizátoru, ne záruka.
  Největší lokály: `buf[512]` (`qspispeed`), `ab[280]` (`autocal`),
  `resp[128]`/`gbuf[128]`, `line[160]` (`datalog csv`).
  ⚠️ Komentář `:1411` navíc tvrdí „ramec UartTask_run ~1,3 kB", zatímco měření
  dává 700 B — číslo se rozešlo s realitou.
- **Dopad:** Žádný přímý; je to **riziková struktura**, ne vada. V kombinaci
  s F-0074 (168 B volných) je ale podstatné, že se rámec může zvednout přidáním
  lokálu do libovolného nového příkazu — a nikdo si toho nevšimne, dokud task
  nepřeteče.
- **Reprodukce:** staticky (`objdump -d` → `sub sp, #N` u `UartTask_run`).
- **Návrh opravy:** **Nerozdělovat celou funkci** — to je velký refaktor bez
  funkčního přínosu a proti pravidlu „zařízení, které funguje, je hodnota".
  Úměrné jsou dvě věci: (a) velké lokály příkazů udělat `static`, jak to už dělá
  `bgcheck`/`stats`/`scpi ipc` (viz F-0074), čímž rámec přestane záviset na
  optimalizátoru; (b) do `check_lessons.sh` přidat kontrolu, že rámec
  `UartTask_run` nepřekročil mez (např. 1024 B), aby se posun projevil při
  překladu, ne až přetečením na desce.
- **Riziko opravy:** nízké u (a) i (b).
- **Vztah k lekcím:** **`L-0016`** (mez i měřidlo její rezervy se navrhují
  společně — rámec se dnes neměří ničím); nově **`L-0035`** (rámec je vlastnost
  celé funkce) a **`L-0020`** (kontrola bez pozitivní kontroly je jen zelená).
- **Stav:** **opraveno 2026-09-20** (cast (a) dokoncena; rozdeleni funkce
  zustava zamerne NEUDELANE).
  - ✅ **(b) meridlo** bylo hotove uz 2026-09-12: `scripts/check_lessons.sh` meri ramec
    `UartTask_run` primo v `.elf` a krici nad 1024 B.
  - ✅ **(a) velke lokaly do `.bss`** hotovo nyni spolu s **F-0074**: `resp[128]`
    a `buf[512]`. Ramec tim prestal zaviset na tom, jak se optimalizator rozhodne
    lokaly prekryt.
  - 🔴 **Rozdeleni `UartTask_run` (1 966 radku) se NEDELA a je to vedome rozhodnuti**,
    jak nalez sam doporucoval: velky refaktor bez funkcniho prinosu, proti pravidlu
    „zarizeni, ktere funguje, je hodnota". Meridlo z (b) navic posun ramce ohlasi pri
    prekladu, takze riziko je pokryte jinak.
  ⬜ **neovereno na HW** (stejne kriterium jako F-0074).

---

## Co bylo zkontrolováno a je v pořádku

Tenhle seznam je u tohohle souboru delší než obvykle, protože většina rizikových
míst **ošetřená je** — a v několika případech s vysvětlením, které se hodilo i mně.

**Meze a vstup:**
- `RxIndex` má mez (`:2347`); přetečení **nepřepisuje paměť** — vada je v tom, co
  se pak provede (F-0073), ne v bezpečnosti zápisu ✅
- Prázdný řádek se ignoruje (`:435`) s doloženým důvodem (CRLF terminály) ✅
- Backspace ošetřen včetně podtečení `RxIndex > 0` (`:420`) ✅
- Všechny prefixové příkazy mají oddělovací guard, takže se nepřekrývají:
  `scpi` (`:1198`), `eth` (`:846`), `sd` (`:1236`), `datalog` (`:1315`) — ověřeno
  i na záludném případě **`sdrtr` vs `sd`**: `RxBuffer[2] == 'r'` guardem propadne
  a trefí správnou větev ✅
- Pořadí větví je správné (`datalog store`/`interval` **před** obecným `datalog`,
  `sdramlog`/`sdraminit`/`sdram write` před `sd`) ✅
- `dec_parse` (`:172-187`) má mez na celou část i na počet desetin ✅
- `uart_i2c4_probe` (`:162`) kontroluje `n` před dalším `snprintf` ✅
- `errlog dump N` omezuje na 200, `sdramlog dump N` taky (`:1075`) ✅
- `backlight` (`:1590`) má mez `v < 1000` i clamp 5..255 ✅
- `d2ddt` (255), `rpipe` (2), `sdrtr` (41..8191) — všechny meze ✅
- **Prověřeno a ZAMÍTNUTO:** `datalog interval 0` → `86400u / per` vypadalo na
  dělení nulou, ale `datalog_set_period_s` clampuje na 1..3600 (`datalog.c`)
  a `datalog_period_s()` má fallback, takže `per` nikdy není 0 ✅

**Zásobník (hlavní osa tohoto souboru):**
- `stacktest_overflow` je `noinline` **s vysvětlením proč** (`:381-390`) ✅
- `bgcheck` `static uint32_t bgsum[120]` (`:1884`), `stats`
  `static TaskStatus_t ta/tb[12]` (`:1951`), `datalog csv`
  `static datalog_rec_t bulk[]` (`:1353`), `scpi ipc` vše statické (`:1133-1135`) ✅
  — výjimky jsou F-0074

**Blokující práce a souběh (sekce D):**
- Veškerá dlouhá práce je **záměrně** tady, protože UartTask není pod watchdogem,
  a u každého místa je to napsané: `scanner` ~2,5 s, `touchloop` 15 s, `enc` 10 s,
  `membench`, `datalog erase`, `qspispeed`, SD operace, `panel` ✅
- `uart_i2c4_probe` **ustupuje scheduleru** (`osDelay(1)`, `:159`) s odkazem na
  F-0020 a vysvětlením, proč by jinak vyhladověl UiTask ✅
- I2C skeny drží mutex **jen na jeden probe**, mezi adresami pouští ✅
- SD blok obalen `sd_blocking_begin/end` jako **celek** (`:1243`/`:1312`), ne po
  funkcích — poučení z F-0026 ✅
- Zápisy do cizích domén jdou přes **požadavek**, ne přímo: `g_si5356_clr_req`
  (I2C1 vlastní SensorsTask), `g_screen_req` (kreslí UiTask), `g_sd_req`,
  `g_stats_reset_req`, `g_datalog_store_req` ✅
- `freq` i `rtc` čtou sdílené texty **pod `taskENTER_CRITICAL()`** (`:1426`,
  `:1516`) — přesně to, co v `SYST:DATE?` chybělo (F-0068, opraveno) ✅
- `qspi_req_service` má `g_qspi_req_busy` a běží z téhož tasku ✅

**Diagnostika:**
- `eth` má pojistku proti kolizi s CM4 na MDIO (`:857`) i s vysvětlením, jak se
  kolize pozná (`0xFFFF`) ✅
- `status` vypisuje i **nulové** hodnoty tam, kde ticho by znamenalo „nevím"
  (`GPIO HLIDAC: 0 oprav`, `:2294`) — a komentář u toho říká, že chybějící řádek
  už jednou vedl k mylnému závěru ✅
- `stacktest` vyžaduje `stacktest yes`, `sd format` vyžaduje `format yes yes` ✅

## Nezkontrolováno / omezení tohoto běhu

- **Nespouštěl jsem `selftest`** (F-0055) ani `stacktest`, `sd format`,
  `datalog erase`, `qspitest`/`qspispeed`/`storetest` — jsou destruktivní nebo
  resetují desku.
- **Chování při zahlcení konzole jsem neměřil.** `scanner` vypíše 127 řádků,
  `status` ~40; `_write` má mutex a timeout 100 ms na řádek. Statisticky to
  vychází pod sekundu, ale skutečnou dobu drženého `uartTxMutexHandle` (a tím
  blokování `printf` z jiných tasků) jde zjistit jen měřením.
- **`usb_console.c` (zdroj znaků) je mimo tento modul** — čítal jsem z něj jen
  cestu `CDC_Receive_FS → UartRxQueue`.
- Nálezy jsou statické; F-0074 je jediný **podložený měřením z desky**
  (`stack Uart free 168 B`, 2026-09-12).
