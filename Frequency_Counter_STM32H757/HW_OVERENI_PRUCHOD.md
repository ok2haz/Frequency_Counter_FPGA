# Ověření na HW — jeden průchod

> Zhuštěná verze checklistu z `STATUS.md` (A0/A/B/C/D/E) do **jednoho sezení s přístrojem**.
> Účel: odbavit ověřovací dluh — **11 funkcí je napsaných, ale nikdy neběželo na displeji**.
> Pořadí je zvolené tak, aby se přístroj nemusel restartovat víckrát než nutně.
>
> Vytvořeno 2026-08-11 po opravě boot-loopu. Legenda: ✅ funguje · ⚠️ funguje s výhradou · ❌ nefunguje

---

## 0. Boot (nejdřív ověř, že oprava drží)

- [ ] Naběhne displej, **žádná smyčka resetů**
- [ ] Boot splash → hlavní obrazovka do ~2 s
- [ ] Boot melodie (vzestupné arpeggio) — pokud není mute
- [ ] UART/USB CDC: `SELFTEST: 13/13 PASS`
- [ ] UART `status` → `Reset: power-on` (ne `WATCHDOG!`), prázdný crash black-box
- [ ] Header, CPU blok vpravo: **`4:OK` zeleně** (CM4 běží a mluví přes IPC)
- [ ] CM4 LED_2 bliká ~1,25 Hz

⚠️ Když je `4:--`, CM4 běží, ale IPC mlčí → `DUALCORE_BRINGUP_CHECKLIST.md` §3.

### 0b. SDMMC1 — 🔴 test „boot bez karty" (nové 2026-08-11)

Po zapnutí SDMMC1 v `.ioc` je tohle **nejdůležitější jednotlivý test**. CubeMX generuje
v `MX_SDMMC1_SD_Init()` na selhání `Error_Handler()` = `bootled_fail()` = mrtvý přístroj;
`HAL_SD_Init` selže vždy bez karty. Vyřazeno `return;`em v USER CODE (viz `CUBEMX_CHECKLIST.md`).

- [ ] **Boot BEZ vložené karty** → displej naběhne normálně, **žádné blikání LED_1**
- [ ] **Boot S vloženou kartou** → totéž (`DATALOG_SD_RAW_OK=0` → SD se ani nezkouší)
- [ ] Okno Datalog ukazuje backend **`W25Q`** (ne SD) — to je zatím správně
- [ ] ⚠️ Kartu **nepoužívej takovou, o jejíž obsah nechceš přijít** — až se RAW zapne, přepíše se od MBR

---

## 1. Hlavní obrazovka

- [ ] Velký kmitočet se hýbe (**zatím simulace** — reálný je až po #1/#2)
- [ ] Header: čas tiká 1×/s, datum, label zóny, GNSS/SYS/SAT/HDOP pilulky
- [ ] **RF bargraf dole = REÁLNÝ** (AD8307 přes ADS1115 AIN1) — zakryj/odpoj vstup, musí klesnout
- [ ] Footer **RUN/STOP**: při běhu nabízí červené „STOP", po stisku zelené „RUN"
- [ ] Při STOP: **zóna velkého čísla se podbarví červeně** (~15 %)
- [ ] Micro-flash tlačítka při stisku (2px accent obrys, ~3 tiky)
- [ ] Slot 0 footeru („Main SW") přepne na **v2 layout**: vlevo Allan graf, vpravo 3 statistiky, mini trend, RF bar
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

## 3. Průchod okny z Menu

Otevřít, ověřit obsah, **BACK musí vést tam, odkud jsi přišel**:

- [ ] Diagnostika → dvousloupcová, teploty vlevo (labely dle umístění), komunikace vpravo
  - [ ] Footer: DIAGRAM → blokové schéma (pulzující LED u BAD/WARN uzlů), PAMĚŤ, SELFTEST
- [ ] Nastavení → jas −/+ (bar plynule dojíždí, **HW jas okamžitě**), mute, auto-dim, téma, jazyk
  - [ ] **SESTAVY >** (#54): slot −/+, ULOŽIT → NAČÍST → aplikuje i téma a jas
- [ ] System Health → stacky tasků, I2C chybovost, „Power supplies: OK"
- [ ] **Čítač** → syrový detail FPGA (dnes bez linku = NOLINK, očekávané)
- [ ] **Holdover** (#52) → stav WARMUP/LOCK/HOLDOVER + OCXO řádek `45.2 C +0.03/m`, fialově dokud náběh
  - [ ] OCXO budík (`FX_OCXO_GAUGE`) — půlkruh s barevnými zónami
- [ ] **Datalog** → backend `W25Q`, počet záznamů roste (10 s/záznam)
- [ ] Alarmy → počitadla + živý mute stav
- [ ] Kalibrace (#68) → −/+ mění hodnoty živě; **AUTO-CAL** → PASS/WARN/FAIL do status řádku
- [ ] Čas → AUTO CET/CEST ↔ ruční, −/+ posun, živý UTC i lokální čas
- [ ] **Math/Limity** (#43/#44) → MATH zap, M cyklus, B ±, NULL; pásmo ± → verdikt PASS/FAIL LO/HI
- [ ] **Animace** (#33) → přepínač ZAP/VYP, RF bar plynule dojíždí
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

## 8. Co po průchodu zapsat

Ke každé ověřené položce v `STATUS.md` přepnout **🔶 → ✅** (#31, #32, #33, #43, #44, #47, #52, #53, #54, #67, #68).

⚠️ **K #10 (detekce přetečení stacku):** audit 2026-08-11 zjistil, proč hook při
boot-loopu mlčel — `configASSERT` shodil systém s vypnutými přerušeními **dřív, než
nastalo další přepnutí kontextu**, a metoda 2 kontroluje vzorek právě tam. Detekce
tedy není rozbitá, jen ji šlo předběhnout. `stacktest yes` výše je jiný případ
(pomalé přetékání) a **měl by** ji spustit — proto ten test.
