# AUDIT.md — jak v tomhle projektu auditovat, aby se chyby neopakovaly

> **Role dokumentu.** `CLAUDE.md` = fakta o kódu. `SKILL.md` = metoda hledání příčin.
> **Tenhle soubor = metoda ověřování**: co, čím a kdy kontrolovat, aby vada nevznikla
> nebo se chytila dřív, než se dostane na desku.
> Vznikl 2026-09-08 po sérii chyb, které **prošly čistým buildem i čistým auditem**.

---

## ⛔ Ústřední zjištění: „build 0 varování + audit 92/0" NEZNAMENÁ funkční kód

Tohle je jádro celého dokumentu. Následující vady **prošly** kompletním
`./scripts/build.sh` (0 varování) **i** `python tools/audit.py` (92 OK, 0 selhání):

| vada | co ji propustilo | čím se odhalila |
|---|---|---|
| `app_gpsdo_handle_encoder` nikdo nevolal → celá Fáze A encoderu mrtvá (#106) | linker ji zahodil `--gc-sections`, build i selftest hlásily OK | `--print-gc-sections`, velikost obrazu |
| `errlog` (250 řádků) linker zahodil — obraz narostl o **16 B** | dtto | porovnání velikosti `.text` před/po |
| Inicializace SDRAM **zahazovala všech 6 návratových hodnot** | syntakticky bezvadný kód | čtení kódu při hledání jiné vady |
| `qspi_recv_fast` uvedl QUADSPI do `BUSY` → boot loop | registrový kód, překladač nemá co namítat | sonda: `s_step`, `g_uptime_s`, `RSR` |
| ETH TX předával DMA **adresu z CM4 aliasu** → DHCP nikdy neprošlo | typy sedí, `void*` je `void*` | `MMC TX_PACKET_COUNT`, obsah TX deskriptoru |
| `PG8` / `PG11` ztratily AF závodem jader | není to chyba v kódu, ale v souběhu | `gpio_guard_tick`, čtení `MODER`/`AFR` |

🔑 **Společný jmenovatel: překladač kontroluje, jestli je kód správně NAPSANÝ.
Neřekne, jestli se do obrazu dostal, jestli běží, jestli na tu adresu někdo
dosáhne a jestli výsledek někdo čte.** Audit, který končí u překladače, měří
tu nejlevnější třídu vad — a v tomhle projektu ani jednou netrefil tu drahou.

---

## Vrstvy auditu — co která chytí a co NE

Řazeno podle ceny. Každá vrstva má **strop**, který se nedá překonat víc dat ze
stejné vrstvy — musí se přidat další.

| # | vrstva | chytí | **nechytí** |
|---|---|---|---|
| 1 | překladač (`-Wall -Wextra -Wshadow`) | typy, stínění, formáty, neinicializované | mrtvý kód, adresy, souběhy, logiku |
| 2 | `-fanalyzer` | NULL deref, únik, out-of-bounds po cestách | vše, co závisí na HW nebo běhu |
| 3 | **linker / velikost obrazu** | 🔑 **kód, který nikdo nevolá** | jestli se opravdu provede |
| 4 | `nm` na `.elf` | přítomnost symbolu, velikost těla (2 B = `while(1)`) | co ten kód dělá |
| 5 | čtení kódu proti invariantu | zahozené návratové hodnoty, rozpojené konstanty | to, co je jen v HW |
| 6 | **selftest na cíli** | čistě logické jádro (CRC, parsery, formát) | HW cesty, časování |
| 7 | **UART instrumentace** (`status`, čítače) | co se doopravdy děje za běhu, bez haltu | jednorázové jevy před startem konzole |
| 8 | **ladicí sonda** | registry, deskriptory, paměť v okamžiku vady | ⚠️ halt zabíjí I2C4 |
| 9 | **kontrolovaný pokus** (klid + zásah) | příčinnost | nic, když chybí kontrolní větev (SKILL §6o) |

⚠️ **Vrstvy 3 a 4 v tomhle projektu chyběly úplně** a stály nejvíc: dvakrát se
dodával kód, který v obrazu nebyl.

---

## Taxonomie vad, které se tu opravdu staly

Není to obecný seznam — každá položka má v projektu doložený výskyt.

| třída | výskyty | čím se hlídá |
|---|---|---|
| **A. Kód není v obrazu** | `handle_encoder` (#106), `errlog` | velikost `.text` před/po, `nm` |
| **B. Tiché selhání** (zahozená návratová hodnota) | SDRAM init 6×, `HAL_ETH_Transmit`, `osMessageQueuePut` u GPS, `HAL_UART_ErrorCallback` bez čítačů | čtení kódu, čítače v `status` |
| **C. Adresa nedosažitelná pro mastera** | ETH TX (CM4 alias) | `nm` + znalost, který master co vidí |
| **D. Sdílený prostředek bez vlastníka** | GPIOG/A/B/C mezi jádry, QSPI 2 writeři | `gpio_guard_tick`, HSEM |
| **E. Rozpojený invariant** | `REFRESH_COUNT` ↔ SDCLK, `DATALOG_PERIOD_S` ↔ stage Allanovy pyramidy, 4× kopie CRC16 | `_Static_assert`, jeden zdroj pravdy |
| **F. Blokující operace ve špatném tasku** | `datalog_init`/`errlog_erase` v UiTasku (watchdog) | seznam „co smí kde běžet" |
| **G. Nedosažitelný handler** | 4× `while(1)` fault, `assert_failed` | `nm` (velikost 2 B) + kontrola `SHCSR`/CSS |
| **H. Dokumentace se rozešla s kódem** | auto-dim „vyvráceno" jen v commitu, zákaz sondy | vyvrácení psát NA MÍSTO tvrzení (§6p) |

---

## Povinné minimum podle druhu změny

**Každá změna** (bez výjimky):
1. `./scripts/build.sh Release CM7` → **0 varování**
2. `python tools/audit.py` → **92 OK, 0 selhání, 2 soubory s varováním** (baseline = generovaný CubeMX kód)
3. 🔑 **Velikost `.text` před a po.** Když přidáváš kód a obraz neroste, linker ho zahodil.

**Nová funkce / modul** navíc:
4. `nm --print-size` → symbol je v obrazu a má rozumnou velikost
5. má **volajícího** (enum bez volajícího = mrtvý kód)
6. čistě logické jádro → **selftest**

**Zásah do HW cesty** (GPIO, DMA, hodiny, paměť) navíc:
7. Které **mastery** na tu adresu sahají? DMA vidí D2 jen na `0x30xxxxxx`.
8. Kdo ještě konfiguruje **tentýž port/registr**? (druhé jádro!)
9. Kontroluje se **návratová hodnota** každého kroku sekvence?

**Zásah do bootovní cesty** navíc:
10. ⚠️ **Nedávej tam kód, který neumíš ověřit.** `qspi_recv_fast` = registrový kód
    v bootu kvůli výkonu, který nikde nechyběl → boot loop.
11. Co se stane, když ten krok **selže**? Zamrzne, nebo se to dozvíme?

**Změna, která mění chování při chybě** navíc:
12. 🔴 **Je ta reakce vhodná, když příčina TRVÁ?** Reset při trvalé vadě = smyčka.
    Stalo se dvakrát: CSS→NMI→reset a `Error_Handler`→reset.

---

## Mezery, které máme DNES, a co doplnit

`tools/audit.py` dnes umí jen vrstvy 1–2. Konkrétní, proveditelná rozšíření:

| # | kontrola | proč | poznámka k implementaci |
|---|---|---|---|
| 1 | **velikost `.text` proti předchozímu buildu** | dvakrát se dodal kód mimo obraz | uložit `.size` do `build/last_size.txt`, porovnat |
| 2 | **`nm`: handler s tělem ≤ 4 B** | `while(1)` stub = tiché zamrznutí | seznam očekávaných handlerů |
| 3 | **zahozená návratová hodnota** u funkcí vracejících `HAL_StatusTypeDef` | třída B | ⚠️ **naivní grep dá šum**: napočítal 96 volání, ale `HAL_GPIO_WritePin`, `HAL_Delay` a `HAL_NVIC_*` vracejí `void`. Musí se filtrovat podle **návratového typu z hlaviček HAL**, jinak je to nepoužitelné (SKILL §7e — ověř měřítko) |
| 4 | **párovost `osMutexAcquire`/`Release`** | dnes 49 / 52 — rozdíl je nejspíš legitimní (více větví), ale nikdo to nehlídá | per-funkce, ne globálně |
| 5 | **duplicitní konstanty** | CRC16 byla 4×, `REFRESH_COUNT` 2× | `_Static_assert` tam, kde to jde |
| 6 | **adresy pro DMA** | ETH TX alias | `nm` na známé buffery: `DscrTab`, `RX_POOL` = `3004xxxx` |
| 7 | **enum bez volajícího** | 4 druhy `ERRLOG_K_*` byly mrtvé | grep na `X(` mimo definici |

⚠️ **Body 3 a 4 se nesmí dodat jako hrubý grep.** Kontrola, která hlásí šum, se
začne ignorovat — a pak je horší než žádná, protože vytváří dojem pokrytí.

---

## Co ani rozšířený audit NEVIDÍ

Doplněno 2026-09-08 po prvním kritickém průchodu. Automat hlásil **0 nálezů**
a ruční čtení kódu přesto našlo **tři vady** — všechny v čerstvě přidaném kódu:

| nález | proč to nástroj nechytil |
|---|---|
| CM4 hlásil kindy 3–6, CM7 uměl pojmenovat jen `1` → zbytek se vypsal jako „Error_Handler" | **konzistence dvou stran** protokolu se staticky neověří |
| `BKP10R` se nikde nemazal → `Error_Handler` restartoval **jednou za život desky**, ne za pokus o start | **komentář tvrdil něco jiného než kód** |
| `g_qspi_req_busy` se nastavoval, ale nikdo ho nečetl | kontrola mrtvého kódu umí **jen funkce, ne proměnné** |

**Zbývající slepá místa, se kterými je nutné počítat:**

1. **Nepoužité globální proměnné.** `nm` je nerozliší — proměnná je v obrazu,
   i když ji nikdo nečte. Spolehlivá kontrola by musela počítat čtení proti
   zápisům napříč moduly; naivní verze by šuměla, takže **zatím není**.
2. **Konzistence obou stran rozhraní.** Že CM4 posílá kind 5 a CM7 ho umí
   pojmenovat, nikdo neověří. Kde to jde, patří tam `_Static_assert`
   (`sens_valid` masky to tak mají — 14 assertů); u výčtů to takhle nejde.
3. **Komentář, který lže o chování.** Kontrola pozná jen odkaz na *neexistující*
   symbol. Že komentář slibuje „jednou za power-cyklus" a kód dělá „jednou za
   život", pozná **jen člověk při čtení**.
4. **Souběhy a pořadí.** Že `datalog_set_store` z `syscfg_load` rozjede init ve
   dvou úlohách, žádný statický nástroj neřekne.
5. **Vhodnost reakce na chybu.** Že reset při *trvalé* příčině vyrobí smyčku,
   je vlastnost návrhu, ne kódu.

🔑 **Důsledek pro praxi: „audit = spustit `audit.py`" NESTAČÍ.** Nástroj zvládá
mechanické třídy (1–4 z tabulky vrstev). Body 5 a 9 — čtení proti invariantu
a kontrolovaný pokus — jsou pořád ruční práce, a právě ony chytají to nejdražší.
Nástroj má **zúžit** prostor, ne nahradit přemýšlení.

## Kadence

| kdy | co |
|---|---|
| **každá změna** | body 1–3 povinného minima |
| **před flashem** | u CM4 navíc `DscrTab`=`30040000`, `RX_POOL`=`3004xxxx`, `ram_heap`=`1002xxxx`; při změně `IPC_VERSION` **obě banky** |
| **po flashi** | `status` — a přečíst ho **celý**, ne jen řádek, kvůli kterému se flashovalo |
| **při nálezu vady** | 🔑 **prohledat celou TŘÍDU** (SKILL §6l) — v této relaci porušeno 4× |
| **jednou za relaci** | projít `STATUS.md` položky se stavem ⬜, jestli některá nezastarala |

---

## Jak poznat, že audit funguje

Ne podle počtu nálezů — ten roste i u špatného auditu. Měřítka jsou:

1. **Kolik vad se chytilo PŘED flashem** proti tomu, kolik nahlásil uživatel.
   Dnešní poměr je špatný: reset smyčku, mrtvý errlog i ETH alias nahlásil provoz.
2. **Kolik chyb se OPAKOVALO.** `STATUS.md` má 17 položek s markerem kolotoče.
   Klesající počet je jediný důkaz, že se metoda učí.
3. **Kolik „oprav" muselo být odvoláno.** V této relaci tři (#215, #216, CSS).
   Odvolání není ostuda — ale opakované odvolání téhož je.

⚠️ **Past na závěr:** audit, který vždycky projde, neměří nic. Když `audit.py`
hlásí 92/0 dvacetkrát za sebou, není to důkaz kvality — je to signál, že se ptá
na věci, které už dávno platí. Přidávej kontroly tam, kde vady **skutečně
vznikly** (tabulka výše), ne tam, kde se dobře měří.
