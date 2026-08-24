# Ověření na HW — jeden průchod

> Zhuštěná verze checklistu z `STATUS.md` (A0/A/B/C/D/E) do **jednoho sezení s přístrojem**.
> Účel: odbavit ověřovací dluh — **11 funkcí je napsaných, ale nikdy neběželo na displeji**.
> Pořadí je zvolené tak, aby se přístroj nemusel restartovat víckrát než nutně.
>
> Vytvořeno 2026-08-11 po opravě boot-loopu, **aktualizováno 2026-08-15** (reorganizace Nastavení,
> SD zprovozněno, CM4 běží). Legenda: ✅ funguje · ⚠️ funguje s výhradou · ❌ nefunguje
>
> 🔴🔴 **CELÝ PRŮCHOD DĚLEJ BEZ AKTIVNÍ LADICÍ SONDY** — po flashi *Terminate* debug session,
> *Run → Remove All Breakpoints* a **úplný power-cycle**. Připojený debugger rozbíjí boot
> handshake CM7↔CM4 (CM4 pak hlásí „off") a leftover breakpointy dělají falešné HardFaulty
> (`HF@…`, `HFSR` bit31 DEBUGEVT). Zjištěno 2026-08-14 — stálo to týdny mylné diagnózy.

---

## 0. Boot (nejdřív ověř, že oprava drží)

- [ ] Naběhne displej, **žádná smyčka resetů**
- [ ] Boot splash → hlavní obrazovka do ~2 s
- [ ] Boot melodie (vzestupné arpeggio) — pokud není mute
- [ ] UART/USB CDC: `SELFTEST: 13/13 PASS`
- [ ] UART `status` → `Reset: power-on` (ne `WATCHDOG!`), prázdný crash black-box
- [ ] Header, CPU blok vpravo: **`7:xx%` (CM7) + `4:xx%` (CM4)** — CM4 běží a mluví přes IPC.
      CM4 dnes skoro nic nedělá → čekej **`4:0%`**; `4:--` = IPC ticho, `4:off` = nenaběhl
- [ ] CM4 LED_2 bliká ~1,25 Hz

⚠️ Když je `4:--`, CM4 běží, ale IPC mlčí → `DUALCORE_BRINGUP_CHECKLIST.md` §3.

### 0b. SD karta — ✅ ZPROVOZNĚNA 2026-08-14 (jen regresní kontrola)

Root cause tehdejšího „nejede přenos" byl **vypnutý `HardwareFlowControl`** (viz STATUS #28/#69).
Dnes už jen ověř, že to drží:

- [ ] **Boot BEZ vložené karty** → displej naběhne normálně, **žádné blikání LED_1**
- [ ] **Boot S vloženou kartou** → totéž
- [ ] Okno Datalog ukazuje backend **`W25Q`** — to je **záměr** (SD = jen export, `DATALOG_SD_RAW_OK=0`)
- [ ] UART `sd init` → krok `[c2]` musí ukázat **`HWFC_EN bit17=1`**, kroky `[d]`/`[d2]` **OK + `…55 AA`**
- [ ] UART `sd fs` → `FAT32`, `sd mount` → OK, `sd test` → shoda + rychlosti

---

## 1. Hlavní obrazovka

- [ ] Velký kmitočet se hýbe (**zatím simulace** — reálný je až po #1/#2)
- [ ] Header: čas tiká 1×/s, datum, label zóny, GNSS/SYS/SAT/HDOP pilulky
- [ ] ⏸ **RF bargraf dole = REÁLNÝ** (AD8307 přes ADS1115 AIN1) — **ODLOŽENO: ověřit až bude
      připojená plná FPGA deska** (bez ní na AIN1 nic smysluplného není)
- [ ] Footer **RUN/STOP**: při běhu nabízí červené „STOP", po stisku zelené „RUN"
- [ ] Při STOP: **zóna velkého čísla se podbarví červeně** (~15 %)
- [ ] Micro-flash tlačítka při stisku (2px accent obrys, ~3 tiky)
- [ ] Slot 0 footeru = **PERIOD/FREQ** přepínač (A/B „Main SW" odstraněn 2026-08-19, #14 — natrvalo v2: vlevo Allan graf, vpravo 3 statistiky, mini trend, RF bar)
- [ ] Tap na Allan → okno ALLAN; tap na trend → fullscreen trend

---

## 2. Nová okna (#31, #47) — reálná data, tady se pozná pravda

**System Health → GRAFY** (s_view=29):
- [ ] 4 teplotní série (STM/MCU/OCXO/FPGA) sdílejí osu, legenda ukazuje aktuální hodnoty
- [ ] Dolní graf OCXO Vc, autoscale
- [ ] Vpravo 5 vertikálních bargrafů (12V/5V/REF/BAT/Vc) s **nominálním markerem** (#32)
- [ ] Footer −/+ přepíná presety 3 min … 7 dní
- [ ] ⚠️ Dlouhá okna (6 h+) se kreslí jen při vstupu/změně presetu, ne periodicky — **to je záměr**
- [ ] „PŘEHLED >" přepne na sesterské okno

**PŘEHLED KANÁLŮ** (s_view=30):
- [ ] Horizontální bargrafy, segmentovaný track
- [ ] Barevné markery: **AKT** výplň · **REF** nominál · **MIN** violet · **MAX** červená (peak-hold)
- [ ] RF řádek v dBm (přes `g_calib`)
- [ ] „< GRAFY" vrátí zpět; **BACK z obou vede do System Health**, ne mezi sebou

---

## 3. Průchod okny z Menu + Nastavení

Otevřít, ověřit obsah, **BACK musí vést tam, odkud jsi přišel**.

⚠️ **Reorganizace 2026-08-13:** Menu = **nástroje**, Nastavení = **konfigurace**. Z Menu se do
Nastavení přesunuly **Čas, Alarmy, Kalibrace, Animace**. V Menu (3×4) tak dnes jsou:
Diagnostika · Nastavení · System Health · Čítač · Holdover · Datalog · **4× volný slot „-"**
· Math/Limity · Status ribbon; **Restart = footer** vedle ZPĚT.
- [ ] Tap na volný slot „-" **NEsmí nikam navigovat** (je to `ACT_FREE` no-op — jinak by se
      BACK zanořoval do prázdna)

- [ ] Diagnostika → dvousloupcová, teploty vlevo (labely dle umístění), komunikace vpravo
  - [ ] Footer: DIAGRAM → blokové schéma (pulzující LED u BAD/WARN uzlů), PAMĚŤ, SELFTEST
- [ ] **Nastavení = ČISTÝ ROZCESTNÍK** (reorganizace 2026-08-13) — mřížka **3×4 přes celou šířku**,
      stejná geometrie jako Menu. Žádné přímé ovládače tu **už nejsou**:
      DISPLEJ · Vzhled · Jazyk · ALARMY · CAS · SIT · KALIBRACE · REFERENCE · ANIMACE · SESTAVY ·
      O PRISTROJI · SD KARTA. (Vzhled a Jazyk jsou **přepínače** — label nese stav.)
  - [ ] **DISPLEJ** (s_view=36, NOVÉ): jas −/+ (bar plynule dojíždí, **HW jas okamžitě**),
        auto-dim zap/vyp + prodleva, Vzhled (TMAVÉ/SVĚTLÉ)
  - [ ] **SÍŤ** (s_view=35, NOVÉ): DHCP zap/vyp, statická IP/maska/brána (výběr pole a oktetu, −/+ s wrapem).
        ⚠️ Dnes se to **jen ukládá** — ETH je blokované HW (PHY 10 MHz); okno to na první kartě přiznává
  - [ ] **SD KARTA** (s_view=37, NOVÉ): živý stav (karta/FS/místo/**Rychlost**), **PŘIPOJIT↔ODPOJIT**
        (label = akce), **TEST** (integrita + rychlost zápisu/čtení), **EXPORT CSV**,
        **FORMAT** = ⚠️ dvojí potvrzení („FORMAT" → „POTVRDIT 1/2" → „SMAZAT! 2/2", auto-zrušení po 6 s)
  - [ ] **SESTAVY >** (#54): slot −/+, ULOŽIT → NAČÍST → aplikuje i téma a jas
- [ ] System Health → stacky tasků, I2C chybovost, „Power supplies: OK"
- [ ] **Čítač** → syrový detail FPGA (dnes bez linku = NOLINK, očekávané)
- [ ] **Holdover** (#52) → stav WARMUP/LOCK/HOLDOVER + OCXO řádek `45.2 C +0.03/m`, fialově dokud náběh
  - [ ] OCXO budík (`FX_OCXO_GAUGE`) — půlkruh s barevnými zónami
- [ ] **Datalog** → backend `W25Q`, počet záznamů roste (10 s/záznam)
- [ ] **Alarmy** → počitadla + **zvuk/mute je nově TADY** (přesunuto z Nastavení — patří k tomu, co umlčuje)
- [ ] **Kalibrace** (#68, ⬅ nově z Nastavení) → −/+ mění hodnoty živě; **AUTO-CAL** → PASS/WARN/FAIL do status řádku
- [ ] **Čas** (⬅ nově z Nastavení) → AUTO CET/CEST ↔ ruční, −/+ posun, živý UTC i lokální čas
- [ ] **Math/Limity** (#43/#44) → MATH zap, M cyklus, B ±, NULL; pásmo ± → verdikt PASS/FAIL LO/HI
- [ ] **Animace** (#33, ⬅ nově z Nastavení) → přepínač ZAP/VYP, RF bar plynule dojíždí
  - [ ] **PŘÍKLADY ANIMACÍ** → 6 dlaždic, všechny se hýbou i při vypnutých animacích
  - [ ] **EFEKTY >** → 6 přepínačů, každý viditelně mění vzhled
- [ ] Status ribbon → LED chipy GPS/FPGA/REF/SENS

---

## 4. GPS okno + GLONASS (A0 — napsáno bez HW, ověřit přednostně)

- [ ] GNSS pilulka → GPS okno, živé (~2×/s)
- [ ] FIX řádek + **TP 100 kHz** (s fixem) / **10 Hz** (bez fixu)
- [ ] Karta Družice: **tap přepíná bargraf C/N0 ↔ polární sky plot**
- [ ] **PRN nese prefix souhvězdí**: `G05` GPS, `R68` GLONASS, `E12` Galileo, `C07` BeiDou
- [ ] UART `gps glonass` → pak musí přibýt `R..` družice (⚠️ NEO-7M může NAKnout — pak jen GPS, není to chyba)
- [ ] **SURVEY** (#53) → START, rozptyl [m] klesá s N; STOP uloží → po restartu se ukáže

---

## 5. SCPI přes USB CDC (#25)

Přes terminál na CDC portu — **tyhle vrací reálná data**:

- [ ] `*IDN?` → `OK2HAZ,...`
- [ ] `SYST:TEMP? 1` → teplota OCXO
- [ ] `MEAS:VOLT? 12` a `? 5` → napájecí větve
- [ ] `MEAS:POW?` → RF v dBm
- [ ] `SYST:GPS:STAT?`
- [ ] `SYST:ERR?` → po chybném příkazu vrátí chybu, ne nesmysl
- [ ] ⚠️ `MEAS:FREQ?` — dnes ze simulace, **neber jako platné**

---

## 6. Persistence (vyžaduje power-cycle, ne jen reset)

Změň jas, téma, mute, časovou zónu, Math/limity, pak **odpoj napájení** a zapni:

- [ ] Vše se vrátilo (syscfg blob ve W25Q CONFIG, magic „SCF8")
- [ ] ⚠️ Změna + power-cycle **do 1,5 s** se ztratí — to je známý debounce, ne chyba
- [ ] Kalibrace (ULOŽIT) přežije
- [ ] Uložená sestava přežije
- [ ] Datalog pokračuje ve stejné sekvenci (`seq` neskočil na 0)

---

## 7. Zvuk, alarmy, watchdog

- [ ] `beep test` pípne; `beep off` umlčí i test (a řekne to)
- [ ] Odpoj GPS anténu → po ztrátě fixu **2 pípnutí**; připoj → **1 pípnutí**
- [ ] Limit FAIL (nastav pásmo tak, aby se porušilo) → **4 pípnutí**
- [ ] Menu → Restart → ANO → čistý restart
- [ ] 🔴 **`stacktest yes`** — záměrně přeteče stack UartTasku → IWDG reset
      → po restartu `status` musí ukázat `stack:UartTask` (#10, viz níže)

---

## 7b. Ethernet — F3 + F5 (2026-08-22, **celé napsané bez HW**)

⚠️ **Nejnedůvěryhodnější část průchodu.** ETH na téhle desce nikdy neběželo a lwIP je
~90 KB kódu, který nikdo nespustil. Ber to jako bring-up, ne jako regresní test.

🔴 **Nutné před testem:** flashnout **OBĚ banky** (`IPC_VERSION` 5→6), ověřovat
**po power-cyklu a BEZ ladicí sondy** (sonda maskuje CM4).

**0) Degradace — testuj JAKO PRVNÍ, bez kabelu i bez čehokoli v zásuvce.**
Tohle je nejdůležitější test celé etapy: ETH nesmí zabít CM4.
- [ ] Přístroj nabootuje, displej běží, `status` → `CM4: alive (IPC heartbeat)`.
- [ ] `status` → `ETH(CM4): init OK, PHY ID 0x0007C131 (LAN8742A)` ⇒ **F3 splněno**
      (PHY čte CM4 přes HAL/MDIO, ne CM7 bit-bangem).
      Když `init no` → neběží RMII REF_CLK; **přístroj i tak musí normálně měřit**.
- [ ] Header ukazuje `4:xx%` (ne `4:off` / `4:--`). `4:--` = nesoulad bank → flashni obě.
- [ ] Nech běžet ~1 min: uptime v `status` **roste monotónně** (žádný skrytý reset).
      ⚠️ Kdyby CM4 cyklicky padala, podezřelý je NULL callback PHY driveru — ošetřeno
      `s_if_ok` v `lwip_app.c`, ale právě tohle je ta cesta, co nikdy neběžela.

**1) Link + DHCP** (kabel do switche s DHCP serverem)
- [ ] Okno **Nastavení → SÍŤ**: `Link: UP 100 Mbit full`, `IP adresa:` skutečná adresa
      (accent barvou). Mezitím `ceka na DHCP...`.
- [ ] `status` → `NET: UP 100 Mbit full, IP 10.0.0.x`.
- [ ] **Vytáhni kabel** → do ~1 s `Link: DOWN`, IP se přepne na `--` (⚠️ **nesmí** dál
      svítit stará adresa) a `System Health` → `CM4:OK NET:down`.
- [ ] **Zapoj zpátky** → link UP a DHCP adresa se vrátí (ověřuje link callback:
      DHCP se startuje až při UP, ne natvrdo).

**2) Ping — kritérium F5**
- [ ] `ping <IP>` z PC projde, RTT řádově jednotky ms.
      ⚠️ Kdyby RTT bylo ~800 ms, hlavní smyčka CM4 se nepřestavěla (viz F5 v checklistu).

**3) Izolace jader — hlavní důkaz, že dělení na CM7/CM4 dává smysl**
- [ ] `ping -f` (flood, Linux; na Windows `ping -t -l 1400`) několik minut:
      **displej se nesmí hnout**, žádný watchdog reset, `g_rtos_cpu_pct` CM7 beze změny
      (`stats`, ⚠️ **bez sondy** — ta měření CPU nafukuje).
- [ ] `status` → CM4 CPU % naskočí (dnes ~0 %) = provoz opravdu obsluhuje CM4.

**Co ještě NEJDE (a je to tak zapsané v UI):** DHCP ZAP/VYP a statická IP se v okně SÍŤ
jen ukládají — IPC snapshot ta pole nenese, takže jede vždy DHCP.

---

## 7c. Vzdálený přístup — W0+W1+W2 (2026-08-23, **celé napsané bez HW**)

Navazuje na 7b (musí projít napřed — bez ETH/lwIP nemá smysl pokračovat). Testuje se
z větší části **bez sítě** — to je záměr, ne náhrada za 7c-4.

🔴 **`IPC_VERSION` se posunulo 6→7 od 7b** (přidán `scpi_selftest_ok`). Pokud jsi mezitím
flashoval jen jednu banku, `status` bude hlásit `⚠ IPC NESOULAD` — přeflashni obě.

**0) Okno PŘÍSTUP (Nastavení → SÍŤ → PŘÍSTUP >)**
- [ ] Výchozí stav po prvním bootu: `Ovládání: ZAKÁZÁNO`, heslo `(zatím žádné)`.
- [ ] Stiskni přepínač → přepne na `POVOLENO` **a heslo se samo vygeneruje** (8 znaků,
      jen číslice + VELKÁ písmena, bez `0/O/1/I`). Vypni a zapni znovu → heslo **zůstává**
      (negeneruje se znovu, dokud nesáhneš na NOVÉ HESLO).
- [ ] NOVÉ HESLO → jiný řetězec než předtím.
- [ ] Restart přístroje (ne power-cycle) → `Ovládání` i heslo **přežijí** (syscfg persist,
      magic `SCFD`). Power-cycle bez čekání na debounce (~1,5 s) by mohl ztratit
      **poslední** změnu — to je známé a zdokumentované chování syscfg, ne bug tady.

**1) Ovládací most (`ipccmd`) — bez SCPI, bez sítě**
Nejrychlejší a nejjednodušší test, dělej ho jako první po 7c-0.
- [ ] Na hlavní obrazovce spusť měření (RUN), pak přes UART: `ipccmd run 0` → **displej
      se přepne na STOP**. `ipccmd run 1` → zpátky na RUN.
- [ ] `ipccmd gate 2` → footer ukáže bránu odpovídající indexu 2 (10 s). Zkus i `0`/`1`/`3`.
- [ ] `ipccmd chan 1` → přepne kanál (pokud je druhý zapojený; jinak jen ověř, že příkaz
      vrátí `OK`, ne `ERR`).
- [ ] `ipccmd gate 9` (mimo rozsah 0..3) → musí vrátit `ERR prikaz odmitnut`, **ne**
      spadnout a **ne** potichu nic neudělat.
- [ ] Vytáhni USB (přeruš UartTask) — na displeji nic z 7c-0/1 nesmí zůstat rozbité.

**2) SCPI na CM4 — selftest při bootu**
- [ ] `status` po každém bootu → `SCPI(CM4): selftest PASS`. `FAIL` nebo trvalé
      `jeste nedobehl` (i po >5 s uptime) je závada — CM4 se s tím nikdy nesetkalo naostro.
- [ ] Ověř na CM7 straně, že SCPI SET přes IPC most teď skutečně prochází (dřív by tiše
      selhalo na NULL callbacku): `scpi ipc CALC:MATH:M 2` → **žádná chyba**, pak
      `scpi CALC:MATH:M?` (přímá CM7 cesta) → do ~1 s ukáže `2` (latence cmd ringu
      ~10 ms + `ipc_service` tik). Vrať zpět `scpi ipc CALC:MATH:M 1`.
- [ ] `scpi ipc SYST:DATE?` a `scpi ipc DISP:BRIG?` → obojí musí vrátit `-241,"Hardware
      missing"` (ne pád, ne prázdnou odpověď) — to je ta oprava CORE_CM7 guardu z W2.

**Co ještě NEJDE:** TCP 5025 (W3) — SCPI na CM4 zatím běží jen jako interní selftest,
žádný socket zvenčí. Web (W4/W5) nemá kam se připojit, dokud W3 nevznikne.

---

## 7d. TCP 5025 — W3 (2026-08-23, **celé napsané bez HW**)

Navazuje na 7c (musí projít napřed). 🔴 `IPC_VERSION` 7→8 (`web_ctrl_en`) — přeflashni
obě banky, jinak `⚠ IPC NESOULAD`.

**0) Základ — spojení a `*IDN?`**
- [ ] Zjisti IP z okna SÍŤ nebo `status` (`NET: UP … IP a.b.c.d`).
- [ ] `nc <IP> 5025` (nebo PuTTY raw na port 5025), pošli `*IDN?<Enter>` →
      odpověď stejná jako přes USB (`scpi *IDN?`).
- [ ] `MEAS:FREQ?` → hodnota souhlasí s displejem (nebo NaN `9.91E37`, pokud FPGA
      link neběží — to je správně, ne chyba).
- [ ] Otevři **druhé** spojení současně (druhý `nc`) → obě fungují nezávisle
      (oddělené `scpi_ctx_t`, oddělená chybová fronta: zkus `SYST:ERR?` v jednom
      po chybě v druhém — nesmí se ovlivnit).
- [ ] Páté souběžné spojení (přes `SCPI_TCP_MAX_CONN=4`) → server ho slušně odmítne
      (spojení se hned zavře), **nezůstane viset**.

**1) W0 — vypínač ovládání se musí projevit i na TCP**
- [ ] V okně PŘÍSTUP nastav `Ovládání: ZAKÁZÁNO`. Přes `nc`: `CALC:MATH:M 2` →
      musí vrátit `-230,"Data corrupt or stale"` (ne ticho, ne pád), `SYST:ERR?`
      teď ukáže tu chybu ve frontě.
- [ ] Přepni na `POVOLENO`. **Do ~2 s** (throttle IPC publish) stejný příkaz →
      žádná chyba, `CALC:MATH:M?` ukáže `2`.
- [ ] Čtení (`MEAS:FREQ?`, `SYST:TEMP?` …) funguje **v obou stavech** vypínače —
      ten řídí jen zápis.

**2) Izolace jader s TCP zátěží**
- [ ] Pošli desítky příkazů rychle za sebou (skript/smyčka) souběžně s `ping -f` →
      displej na CM7 se nesmí hnout, `stats` (bez sondy) beze změny.

**Co ještě NEJDE:** HTTP/web (W4/W5) — port 5025 je jen raw SCPI socket (VISA-styl),
žádný prohlížeč se sem nepřipojí.

---

## 7e. HTTP `/api/state` + `/api/scpi` — W4 (2026-08-23, **celé napsané bez HW**)

Navazuje na 7d (musí projít napřed). 🔴 `IPC_VERSION` 8→9 (`httpd_selftest_ok`) —
přeflashni obě banky.

**0) Selftest parseru**
- [ ] `status` → `HTTP(CM4): selftest PASS`.

**1) `GET /api/state`**
- [ ] `curl http://<IP>/api/state` → validní JSON (zkontroluj okem nebo `| python -m json.tool`).
- [ ] Hodnoty souhlasí s displejem **a** s odpovědí SCPI (`MEAS:FREQ?` přes `nc` z §7d).
- [ ] Vytáhni kabel FPGA (nebo jinak znevalidni měření) → `"freq_hz":null`, ne `0` a ne
      stará hodnota — to je zlaté pravidlo v akci.
- [ ] `curl http://<IP>/` → krátký textový popis API (ne 404).
- [ ] `curl http://<IP>/neexistuje` → `404 Not Found`.

**2) `POST /api/scpi`**
- [ ] `curl -d "*IDN?" http://<IP>/api/scpi` → stejná odpověď jako USB/TCP 5025.
- [ ] `curl -d "MEAS:FREQ?" http://<IP>/api/scpi` → souhlasí s `/api/state`.
- [ ] V okně PŘÍSTUP nastav `ZAKÁZÁNO` → `curl -d "CALC:MATH:M 2" ...` vrátí SCPI chybu
      (`-230`), ne ticho. Zpátky na `POVOLENO` → projde.
- [ ] Pošli tělo bez `Content-Length` (např. `curl --http1.0 -X POST --data-binary @-`
      s vynechanou hlavičkou, nebo ruční `nc`) → `411 Length Required`, ne pád.
- [ ] Pošli tělo delší než 96 B → `411` (přes `HTTPD_BODY_MAX`), ne přetečení bufferu.

**3) Souběh se SCPI/5025 a izolace jader**
- [ ] Otevři TCP 5025 (`nc`) i HTTP (`curl`) současně → obě fungují nezávisle
      (jiný port, jiný pool spojení).
- [ ] `curl` smyčka + `ping -f` současně → displej se nesmí hnout.

**Co ještě NEJDE:** SPA (W5) — `GET /` vrací jen holý text, žádná stránka s tlačítky.

---

## 7f. SPA + Basic Auth — W5 (2026-08-23, **celé napsané bez HW**)

Navazuje na 7e (musí projít napřed). 🔴 `IPC_VERSION` 9→10 (`web_user`/`web_pass`) —
přeflashni obě banky.

**0) Stránka se vůbec načte**
- [ ] `curl http://<IP>/` → HTML (ne holý text jako dřív), obsahuje `<title>GPSDO citac</title>`.
- [ ] Otevři `http://<IP>/` v **mobilním** prohlížeči na stejné LAN → stránka se vykreslí,
      karty se stavem (kmitočet, teploty, napájení) se do ~1 s naplní a pak samy
      aktualizují (poll 1×/s). Layout je responzivní (mřížka `auto-fill`).
- [ ] Necháno otevřené několik minut → žádné vizuální „zamrznutí" (mrtvé spojení by
      se projevilo jako přestávka v aktualizacích; zkontroluj i konzoli prohlížeče
      na chyby `fetch`).

**1) Ovládání bez přihlášení**
- [ ] V okně PŘÍSTUP nastav `ZAKÁZÁNO` → tlačítka RUN/STOP/SET GATE na stránce jsou
      **vizuálně vypnutá** (`disabled`, přečti si `web_ctrl_en:false` v `/api/state`).
- [ ] `POVOLENO`, ale v prohlížeči **nezadávej** jméno/heslo → klikni RUN → zpráva
      pod tlačítky ukáže SCPI chybu (ne ticho, ne pád) — čtení dál funguje.

**2) Přihlášení a ovládání**
- [ ] Zadej stejné jméno/heslo jako v okně PŘÍSTUP na displeji → **PŘIHLÁSIT**
      → zelené „Přihlášeno, ovládání povoleno."
- [ ] Zadej **špatné** heslo → PŘIHLÁSIT → červené **„Neplatné jméno nebo heslo."**
      (od 2026-08-23 se přihlášení ověřuje `*IDN?` + `auth_debug.match`; dřív se jen
      tiše uložilo a chyba se poznala až u prvního SET).
- [ ] Klikni RUN → **na displeji přístroje se měření spustí**. STOP → zastaví.
- [ ] Přepni bránu (0,1/1/10/100 s) → footer na displeji ukáže novou hodnotu
      (do ~1 s, latence cmd ringu) **a segment na webu se zvýrazní** — zvýraznění se
      bere ze snapshotu (`set_gate_idx`), takže potvrzuje SKUTEČNÝ stav přístroje,
      ne jen to, na co se kliklo. Totéž vstup A/B (`set_chan`).
      ⚠️ Kdyby brána i vstup zůstaly viset na `0,1 s`/`A`, je to ten **slepý readback**
      opravený v IPC v11 → zkontroluj, že jsou **obě banky** flashnuté (v `status`
      nesmí být `⚠ IPC NESOULAD`).
- [ ] Obnov stránku (F5) → jméno se předvyplní z `localStorage`, heslo zůstává uložené
      (ověř dalším klikem RUN bez opětovného zadávání).
- [ ] **Chybí-li kmitočet** (velké `--`), stránka pod ním **napíše důvod** — „Měření je
      zastavené", „Není spojení s FPGA deskou (SPI link DOWN)" nebo „FPGA nehlásí žádný
      vstupní signál". ⚠️ Bez připojené FPGA desky je `--` **správně**: displej ukazuje
      simulaci (STATUS #2), web reálná data.
- [ ] **SCPI konzole**: `MEAS:FREQ?` → hodnota nebo `9.91E37`; `SYST:ERR?` → `0,"No error"`.
      Odpovědi se řadí do logu pod konzolí (nejnovější nahoře), VYCISTIT ho smaže.

**2b) Grafy a vzhled (přepracováno 2026-08-24)**
- [ ] Po ~2 s se rozjedou **4 grafy** (kmitočet / teploty / OCXO Vc / napájení) — do té doby
      v nich stojí „ceka na data...". Teploty a Vc musí jet i **bez FPGA desky**.
- [ ] Přepínač **okna grafu** (1/5/15/60 min) překreslí grafy okamžitě, i bez čekání na poll.
- [ ] ⚠️ **Historie je jen v prohlížeči** — po F5 grafy začínají znovu od nuly. Je to záměr
      (delší historie by musela z datalogu), ne chyba.
- [ ] Tlačítko **VZHLED** cykluje **JANTAR → MODRÁ → SVĚTLÁ** a přežije F5 (`localStorage`).
      ⚠️ Při přepnutí se musí přebarvit **i křivky v grafech** — kdyby zůstaly původní,
      je někde natvrdo zadaný `stroke=` místo CSS třídy.
- [ ] **Klik na kterýkoli graf** otevře detailní okno: velký graf, **popsané osy**
      (vlevo hodnoty, dole stáří vzorku), tabulka statistik a vysvětlivka.
      Zavírá se **Esc**, klikem mimo i tlačítkem. Za otevřeného okna se graf **dál
      aktualizuje** (poll běží).
- [ ] V detailu Allanova grafu jsou na ose Y **dekády** (`1e-10`…) a na ose X hodnoty **τ**;
      tabulka ukazuje σy pro každé τ + počet párů.
- [ ] Vpravo v hlavičce „Ovladani" běží stáří dat („aktualni" / „pred N s"); po odpojení
      kabelu se do pár sekund objeví `spojeni selhalo` a pilulka FPGA zčervená.
- [ ] **GPS donut** ukazuje počet družic (plný kruh = 12), barva podle fixu.
- [ ] Bargrafy napájení mají **značku nominálu** a barví se zeleně/žlutě/červeně podle
      odchylky; VBAT pod 2,6 V zežloutne.
- [ ] `RF` řádek ukazuje **dBm** (ne mV) — musí souhlasit s `MEAS:POW?` v konzoli.
      Bez platné kalibrace slope vrací JSON `null` a zobrazí se mV.

**2c) Allan a drift (klientský výpočet z reálných měření, 2026-08-24)**
⚠️ **Vyžaduje běžící SPI link na FPGA** — bez něj `freq_hz` je `null`, nepřibývají měření
a obě karty zůstanou na `ceka na mereni z FPGA...`. To je správně, ne chyba.
- [ ] S běžícím měřením: po ~4 měřeních naskočí σy a drift; karta Allan ukáže **τ0**
      (skutečný rozestup měření) a **n** (počet vzorků), tabulka τ / σy / počet párů.
- [ ] ⚠️ **σy(τ) porovnej s oknem ALLAN na displeji** — nemusí sedět přesně (jiné okno,
      jiné τ0), ale **řádově ano**. Kdyby web ukazoval řádově lepší stabilitu, znamená to,
      že se do výpočtu dostávají opakované vzorky (chyba v `seq_meas` cestě).
- [ ] **Přepni bránu** (např. 1 s → 10 s) → obě karty se **vynulují a začnou znovu**
      (Allan potřebuje rovnoměrné rozestupy; míchat dvě τ0 nelze). τ0 se pak ustálí
      na nové hodnotě.
- [ ] **Drift**: u krátkého okna bude `|r| < 0,5` a musí se objevit varování
      „proklad je NEPRUKAZNY" a **žlutá čára prokladu se v grafu kmitočtu nekreslí**.
      Po delším běhu (desítky minut) by se r mělo zvednout a čára naskočit.
- [ ] **Nominál**: pole se předvyplní zaokrouhleným prvním měřením (10 MHz), offset se
      pak zobrazí v ppb. Tlačítko **= AKTUALNI** nastaví nominál na aktuální průměr →
      offset spadne k nule.
- [ ] Nech běžet ~30 min a zkontroluj, že se **σy s rostoucím τ zmenšuje** (u dobré
      reference) a že poslední řádek tabulky přestane hlásit „jen N paru".

**3) Souběh a izolace jader**
- [ ] Otevři stránku ve **dvou** prohlížečích/kartách současně → oba pollují nezávisle.
- [ ] Otevři čtvrté a páté souběžné HTTP spojení (např. 5 karet najednou, port 80 má
      strop 3) → nadbytečná se slušně odmítnou, ostatní fungují dál.
- [ ] Poll SPA + `ping -f` současně → displej na CM7 se nesmí hnout.

**Co ještě NEJDE:** W6 (SSE/WebSocket místo pollu, mDNS `gpsdo.local`, stažení
datalogu) — volitelné dotažení, ne blokující.

---

## 7g. Benchmark pamětí (okno PAMETI / UART `membench`)

⚠️ Destruktivní **jen** pro vyhrazený SDRAM scratch (`0xC0400000`) a W25Q scratch
(`W25Q_BENCH_BASE`, za flight recorderem) — data přístroje, datalog ani kalibrace se
nedotkne. Interní FLASH se **jen čte**.

- [ ] `membench` přes UART → tabulka **6 řádků**, poslední řádek `MEMBENCH: OK (celkem 0 chybnych bitu)`.
- [ ] 🔴 **Během ani po testu se NESMÍ objevit `stall:CM4`.** Kdyby ano, ve sloupci „vysledek"
      bude u viníka `SHODIL CM4!` — znamená to, že ta oblast **není volná** a někdo v ní má data.
      ⚠️ **IWDG2 je vypnutý, takže CM4 už sama nenaběhne** — přijdeš o ETH, SCPI i web až do
      resetu desky. Ověř `status` → `CM4: alive`.
      ⚠️ Do 2026-08-23 to padalo na cíli **SRAM1 D2**, protože tam měla lwIP natvrdo haldu
      (`LWIP_RAM_HEAP_POINTER 0x30004000`). Opraveno; kdyby se to vrátilo, zkontroluj
      `nm H757_LED_CM4.elf | grep ram_heap` → musí být `1002xxxx`, ne `3000xxxx`.
- [ ] **Nastavení → PAMETI > → BENCHMARK** → stejné hodnoty jako z UART; během běhu se ve
      stavovém řádku střídají cíle a vzory („SDRAM: vzor 55/AA"), po doběhu „hotovo, bez chyb".
- [ ] Během běhu **displej nezamrzne a nepřijde IWDG reset** (běh trvá ~5 s v UartTasku;
      kdyby vyhladověl UiTask, deska se restartuje → v `status` by přibyl `stall:UiTask`).
- [ ] Ověř `status` po doběhu: uptime **roste dál** (žádný reset).
- [ ] Sloupec **„testovano"** ukazuje `blok / kapacita` — u SDRAM musí být **`4/32 MB`**
      (ne jen velikost bloku). Kdyby tam bylo jediné číslo, běží starý firmware.
      ⚠️ Během SDRAM fáze může displej krátce trhat (LTDC čte framebuffer z téhož čipu) —
      je to očekávané.
- [ ] Řádové kontroly rychlostí (hrubé, jen jestli něco nesedí o řád):
      DTCM a AXI SRAM nejrychlejší (stovky MB/s), SRAM1/D3 pomalejší, SDRAM řádově desítky
      MB/s, W25Q jednotky MB/s čtení a výrazně méně zápis (erase).
      ⚠️ **SRAM1 D2 / SRAM4 D3 s nulovou rychlostí nebo samé chyby** = nejspíš nezapnuté
      hodiny domény, ne vadná paměť (`__HAL_RCC_D2SRAM1_CLK_ENABLE` v `membench_run`).
- [ ] `selftest` → **14/14 PASS** (přibyl test vzorů benchmarku, index #13).
- [ ] **Když něco hlásí chybné bity, čti rozlišovací řádky pod tabulkou — v tomhle pořadí:**
      1. `ADRESNI LINKY: kolize na offsetu ...` → vadná **adresace**, ne buňky. Dvě různé
         adresy míří do téže buňky; vzory dat by to samy nikdy neodhalily.
      2. `podle vzoru: ... adresa=N ...` → selže-li **jen** vzor „adresa", potvrzuje bod 1;
         selžou-li **všechny**, jsou vadná data / rozpadá se obsah.
      3. `prvni chyba @...: cekano X, precteno Y` → **náhodné** bity = rozpad obsahu;
         **cizí platná hodnota** (jiný index) = překryv adres.
      4. `retence po 1 s: N chybnych bitu` (jen SDRAM) → **nenulové = paměť neudrží obsah**,
         tedy příliš pomalý refresh (`REFRESH_COUNT` v `fmc.c`).
         ⚠️ Změřeno 2026-08-23: **0 chyb** → refresh je v pořádku.
      5. `maska bitu 0x...` → ukazuje na konkrétní datovou linku (např. samý bit 7).

**🟠 Nález u SDRAM, který se PŘESTAL opakovat (2026-08-23): `ADRESY SE OPAKUJI po 2048 kB`.**
Objevil se jen v bězích, kde zároveň **padala CM4** (rozjeté jádro vidí SDRAM na `0xC0000000`
a mohlo do ní psát); po opravě haldy lwIP už to nehlásí. **Není to potvrzená vada HW.**
- [ ] Při každém `membench` sleduj, jestli se řádek `ADRESY SE OPAKUJI` nevrátí — probe je levný
      (dvě slova, žádná zátěž sběrnice) a běží před každým testem.
- [ ] Kdyby se vrátil: odpovídalo by to **nefunkční adresní lince `FMC_A9` = `PF15`** (firmware je
      v pořádku — `PF15` je v `.ioc` i v `HAL_GPIO_Init(GPIOF, …)`), tedy **prozvonit spoj
      `PF15` → A9 na SDRAM** a zkontrolovat pájku. Intermitentní chování sedí na studený spoj.
- [ ] ⚠️ **Důsledek, kdyby se potvrdilo:** `FB2` (`0xC0200000`) a `FB0` (`0xC0000000`) se liší
      právě jen v `HADDR[21]` → **sdílely by tutéž paměť**. Triple buffering by byl fakticky
      double. Než se sáhne na cokoli v zobrazovacím řetězci, ověř tohle.
- [ ] ⚠️ Během testu **se displej nesmí rozsypat** (velikost SDRAM bloku snížena 4 MB → 512 kB
      právě proto — LTDC potřebuje z téže SDRAM ~46 MB/s a dlouhý test mu bral pásmo).
- [ ] Kdyby test hlásil `kolize s 0x...!` a SDRAM se přeskočila, je to **správné chování**
      pojistky: testovaný blok by přepisoval framebuffer. Neobcházet — vyřešit HW.

## 7h. Rozložení hlavní obrazovky (okno DISPLEJ)

- [ ] **Nastavení → DISPLEJ >** → karta „Rozlozeni hlavni obrazovky", tlačítko ukazuje
      `HYBRIDNI` (výchozí).
- [ ] Stisk → `KLASICKE`; ZPĚT až na hlavní obrazovku → Allan graf je **širší** (53 % místo
      47 %), čísla Offset/σ/Drift **menší** (mono_16), RF bargraf nižší.
      ⚠️ V klasickém rozložení se čísla statistik i trend mění **skokem** (bez plynulého
      dojezdu) — to je záměr, je to zamrzlá větev.
- [ ] Zpět na `HYBRIDNI` → původní vzhled, plynulý dojezd čísel se vrátí.
- [ ] **Restart přístroje** → zvolené rozložení přežije (syscfg, magic `SCFE`).
      ⚠️ První boot po tomhle flashi načte výchozí nastavení (změna magicu SCFD→SCFE).

## 8. Co po průchodu zapsat

Ke každé ověřené položce v `STATUS.md` přepnout **🔶 → ✅** (#31, #32, #33, #43, #44, #47, #52, #53, #54, #67, #68).

⚠️ **K #10 (detekce přetečení stacku):** audit 2026-08-11 zjistil, proč hook při
boot-loopu mlčel — `configASSERT` shodil systém s vypnutými přerušeními **dřív, než
nastalo další přepnutí kontextu**, a metoda 2 kontroluje vzorek právě tam. Detekce
tedy není rozbitá, jen ji šlo předběhnout. `stacktest yes` výše je jiný případ
(pomalé přetékání) a **měl by** ji spustit — proto ten test.
