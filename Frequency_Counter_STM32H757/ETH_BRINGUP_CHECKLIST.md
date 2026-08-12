# Ethernet + SCPI + web na CM4 — etapový plán

> Rozpracování `NAVRH_ARCHITEKTURA_CM7_CM4.md` §5/§6/§6a do **spustitelných etap**.
> Váže se na `STATUS.md` #24/#25/#26, `DUALCORE_BRINGUP_CHECKLIST.md`.
>
> Přidáno 2026-08-11, **přepracováno po auditu téhož dne** (viz §10 — co a proč se změnilo).

> 🔴 **MÍRA JISTOTY (čti kriticky):** ETH na této desce **nikdy neběželo**. Ověřeno je jen,
> že PHY je osazený (schéma list 4/7) a že CM4 bootuje a mluví přes IPC. Čísla paměti jsou
> **odhady** z §2 návrhu, ne měření. Kroky ber jako kontrolní body, ne jako recept.

---

## 0. Řídicí princip

**Každý nevratný závazek až po levném důkazu.**

V plánu jsou přesně dva nevratné závazky — **regenerace `.ioc`** (může rozbít právě
stabilizovaný build obou jader) a **volba RTOS vs. holá smyčka** (určuje tvar celé
síťové vrstvy). Oba jsou proto zařazené **až za** etapy, které stojí skoro nic a přinesou
data: F0 ověří hardware bez jediné změny v projektu, F1 připraví diagnostiku, F3 udělá
minimální regen.

Vedlejší efekt: nejlevnější únik z celého plánu je hned po **F0**.

---

## 1. Přehled etap

```
F0 diagnostika ─→ F1 IPC v4 ─→ F2 paměť ─→ F3 CubeMX(ETH) ─→ F4 ROZHODNUTÍ ─→ F5 link+ping
   z CM7            příprava      pravidlo     minimální        RTOS/smyčka       = M9
   BEZ REGENU       bez ETH                                                         │
                                                                                    ▼
                                                        F6 SCPI (M10) ─→ F7 web (M11) ─→ F8 služby (M12)
```

| F | Cíl | Nevratné? | Riziko | Rozsah | Hotovo, když |
|---|---|---|---|---|---|
| **F0** | Pinmapa + PHY žije + REF_CLK měří 50 MHz | ne | 🔴 HW nejistoty | S | přečteno PHY ID, změřen ref. clock |
| **F1** | IPC v4: stav linky/IP → displej | ne | bump verze | S | Health ukazuje `NET: down` |
| **F2** | Adresování DMA bufferů, clocky | ne | — (řešeno návrhem) | S | pravidlo zapsané, `.map` ověřen |
| **F3** | CubeMX: **jen ETH** na CM4 | ⚠️ **ANO** | regen rozbije jádra | M | obě jádra se přeloží a bootují |
| **F4** | Rozhodnout RTOS vs. smyčka | ⚠️ **ANO** | — | S | rozhodnutí zapsané i s důvodem |
| **F5** | Link + DHCP + ping (**M9**) | ne | ETH DMA, izolace jader | L | `ping` projde, `ping -f` nehne displejem |
| **F6** | SCPI TCP 5025 (**M10**) | ne | — jen práce | M | VISA `*IDN?`, reálné veličiny |
| **F7** | Web + `/api/*` (**M11**) | ne | flash rozpočet | L | stránka v prohlížeči |
| **F8** | mDNS, SNTP (**M12**) | ne | — | S | `gpsdo.local` se najde |

**Rozsah:** S = hodiny, M = den, L = víc dní. Relativní, ne kalendářní.

---

## 2. F0 — Diagnostika z CM7 (⭐ nejdůležitější etapa)

**Bez jediné změny v `.ioc`, bez regenerace, bez rizika pro funkční build.** Zodpoví dvě
ze tří hardwarových neznámých pomocí infrastruktury, která funguje dnes.

Klíčové zjištění, na kterém to stojí: **SMI/MDIO je clockované signálem MDC, tedy
nezávisle na 50 MHz RMII ref. hodinách.** PHY odpovídá na MDIO, i kdyby `nINTSEL` byl
špatně. Obě otázky se tak dají řešit odděleně.

- [ ] **Vytáhnout pinmapu ze schématu** (`Ethernet.kicad_sch`, list 4/7) — v repu **není**,
      dokumenty znají jen názvy signálů. Potřebné: `MDC`, `MDIO`, `ETH_RES`, `ETH_REF_CLK`,
      `ETH_INT`. Zapsat do `NAVRH…md` §6a.1 (dnes tam jsou jen jména).
- [ ] **UART/CDC příkaz `eth`** na CM7 — přesně v idiomu `scanner` / `si5356` / `fpgaraw`:
  - pulz `ETH_RES` low ≥100 µs, pak pár ms
  - **bit-bang SMI** na MDC/MDIO (GPIO je přístupné z obou jader, nezávisle na tom, komu
    ETH v `.ioc` patří)
  - **scan adres 0–31**, číst PHY ID (reg 2 a 3) → očekáváno `0x0007C130` / `0x0007C131`
  - vypsat nalezenou PHYAD a BMSR (stav linku)
- [ ] **Změřit `ETH_REF_CLK`** → musí být **50 MHz**. Timerem v external-clock režimu
      (pokud pin sedí na TIM vstupu), jinak osciloskopem. **Tím se ověří strap `nINTSEL`**
      (režim „REF_CLK OUT"), který §6a.1 sám označuje jen jako *předpokládaný*.
- [ ] ✅ **Výstup:** víme, že PHY žije, známe jeho adresu, a víme, že MAC dostane hodiny.

⚠️ Příkaz `eth` je bring-up rezidum — nechat ho jako diagnostiku (jako `fpgaraw`), ale
označit komentářem, že bit-bang MDIO je jen pro F0 a produkčně to dělá HAL z CM4.

---

## 3. F1 — IPC v4 dopředu (příprava, ne důsledek)

CM4 nemá konzoli ani displej. Bez tohohle kroku budeš M9 ladit jen debuggerem —
proto **patří sem, ne až za rozjetou síť**. Na ETH nezávisí ani trochu.

Dnes `ipc_cm4_status_t` nese jen `magic`, `heartbeat`, `cm4_cpu_pct`, `cm4_uptime_s`.

- [ ] Rozšířit `ipc_cm4_status_t` o **stav linky** (down/up, rychlost, duplex) a **IP adresu**.
- [ ] Zvednout **`IPC_VERSION` 3 → 4** (obě strany ověřují `magic`+`version`+`size`;
      nesouhlas → IPC off a degradovaný běh).
- [ ] Zobrazit v okně **System Health**; CM4 zatím hlásí natvrdo `down`.
- [ ] ⚠️ Zvážit `NET` chip v headeru, ale **pozor na `HDR_PILL_LIMIT`** — řada pilulek je
      už dnes na rozpočtu (viz `CLAUDE.md`); Health je bezpečnější místo.
- [ ] Zapsat v4 do `CLAUDE.md` a `STATUS.md`.

✅ **Kritérium:** Health ukazuje `NET: down` a přežije to reset. Zobrazovací řetěz je
odladěný dřív, než ho budeš potřebovat.

---

## 4. F2 — Paměť: pravidlo místo ověřování

Rozpočet CM4 (160 KB, SRAM2+3), odhady z §2 návrhu:

| Položka | Odhad |
|---|---|
| ETH deskriptory + RX/TX buffery | ~32 KB |
| lwIP heap + pbuf pool | 32–64 KB |
| Stacky (F5: 1 kontext, F8: 4) | 4–20 KB |
| **Rezerva ze 160 KB** | **44–92 KB** |

- [ ] 🔴 **PRAVIDLO: DMA-viditelné buffery adresuj vždy systémovou adresou `0x3002xxxx`.**
      CM4 linkuje RAM na `0x10020000` (alias), CM7 vidí tutéž paměť na `0x30020000`.
      ETH DMA je **sběrnicový master, ne CPU** — přes alias by mohl zapisovat jinam, než
      CPU čte (projev: link up, ale RX nikdy nepřijde). **Tímhle pravidlem riziko mizí
      definičně**, není potřeba luštit RM0399 jako bránu.
      ⚠️ Sekce nesmí kolidovat s běžnou RAM CM4 — je to **stejná fyzická paměť**.
- [ ] **Povolit SRAM2/3 clock z CM4** — `RCC_C2_AHB2ENR` (per-core registr!).
- [ ] Ověřit v `.map`, kde deskriptory reálně skončily. **Nespoléhat, že to sedlo.**
- [ ] ⚠️ CM4 **nemá D-cache** → žádná cache maintenance kolem ETH DMA. Hlavní důvod, proč
      ETH patří sem a ne na CM7 — **nezahazuj tu výhodu** přesunem bufferů do D1.
- [ ] Kdyby bylo těsno: **přerozdělit SRAM1** (CM7 drží 128 KB, které linker nevyužívá —
      jediný uživatel je diagnostický `ram write/read`).

---

## 5. F3 — CubeMX: jen ETH (⚠️ první nevratný krok)

Záměrně **minimální regen**: žádné LWIP, žádný FreeRTOS. Menší změna = menší šance,
že se rozbije funkční stav.

- [ ] Přepnout kontext na **CortexM4** a přiřadit mu **ETH (RMII)**. Nic víc.
- [ ] ⚠️ **Odškrtnout aspirační IP CM4** (`DUALCORE_BRINGUP_CHECKLIST.md` §5):
      `OPENAMP_M4`, `USB_DEVICE_M4`, `USB_HOST_M4`, `FATFS_M4`, `PDM2PCM_M4`, `WWDG2`, `VREFBUF`.
- [ ] `IWDG2` v `.ioc` **nezapínat** — `iwdg2.c` je ruční registrová implementace, HAL modul vypnutý.
- [ ] Ověřit, že **USER CODE bloky přežily**: beep, LED_2, `ipc_cm4_init()`, `iwdg2_init()`, smyčka.
- [ ] ⚠️ **Zdiffovat `.ioc` a `CM7/Core/Src/gpio.c`** — ETH piny nesmí přepsat nic z CM7 (FMC/LTDC/DSI).
- [ ] Ověřit, že se **nic nerozbilo**: displej běží, `4:OK`, selftest projde.
- [ ] `HAL_ETH_ReadPHYRegister` → potvrdit PHY ID **z CM4** (nejen z CM7 v F0).

---

## 6. F4 — Rozhodnutí RTOS vs. smyčka (⚠️ druhý nevratný krok)

Až **teď**, s daty. Do téhle chvíle byly F0–F3 na volbě nezávislé.

**Stav argumentů (po auditu, viz §10):**

| Pro **smyčku** (`NO_SYS=1`) | Pro **FreeRTOS** |
|---|---|
| Nic v aplikaci neblokuje — data jsou v IPC snapshotu, `scpi_process` je čistá transformace | ✅ **Ekosystém H7+lwIP je RTOS-first** — generovaný `ethernetif.c` počítá se semaforem a input taskem, referencí pro `NO_SYS` je míň |
| `httpd`, `mdns`, `sntp` z lwIP jsou **všechny `NO_SYS`-kompatibilní** | Pod zátěží (flood) jde prioritizovat SCPI proti webu |
| `iwdg2_kick()` zůstane triviální (jeden kick, jedna smyčka) | Rozdíl v RAM je **~16 KB ze 160 KB** — nerozhoduje |

**Rozhodovací kritéria, která budeš mít v ruce po F3:**
- [ ] Jak vypadá generovaný `ethernetif.c` — jde z něj `NO_SYS` udělat bez přepisu?
- [ ] Kolik ladění spolykalo F3 — když H7 ETH dělal potíže, ber šlapanou cestu (FreeRTOS).
- [ ] Rozhodnutí **zapsat i s důvodem** (sem, do §6), ať se k němu nemusíš vracet.

**Výchozí doporučení, pokud nic nerozhodne:** FreeRTOS s **jediným NetTaskem** v F5 —
chová se jako smyčka, ale sedí na standardním portu lwIP a další tasky jde přidat bez přepisu.

---

## 7. F5 — Link + DHCP + ping (**M9 hotovo**)

- [ ] lwIP init, `ethernetif`, `netif` up (jeden kontext — task nebo smyčka dle F4).
- [ ] Autonegotiace → `HAL_ETH_SetMACConfig` dle Speed/Duplex.
- [ ] DHCP jako výchozí, **fallback na statickou IP po ~5 s** (přístroj musí být dostupný i bez DHCP).
- [ ] Hlásit stav linky a IP přes **IPC v4** (řetěz je hotový z F1) → hned vidíš, co se děje.
- [ ] `iwdg2_kick()` — jeden kontext, zatím stačí prostý kick (viz §9).
- [ ] ✅ **Kritérium:** `ping` z PC projde.
- [ ] ✅ **Test izolace jader:** `ping -f` (flood) → **displej na CM7 se nesmí hnout**,
      žádný watchdog reset, `g_rtos_cpu_pct` CM7 beze změny. Hlavní důkaz, že rozdělení
      jader dává smysl.

---

## 8. F6–F8 — Aplikační vrstva

### F6 — SCPI na TCP 5025 (**M10**)

🔴 **Gate na #2 platí PER PŘÍKAZ, ne per etapu.** Zlaté pravidlo zakazuje servírovat
*simulaci*, ne servírovat cokoli:

| Smí hned (reálná data dnes) | Čeká na #2 (dnes simulace) |
|---|---|
| `*IDN?`, `SYST:ERR?`, `SYST:VERS?` | `MEAS:FREQ?` |
| `MEAS:VOLT?`, `SYST:TEMP?`, `MEAS:POW?` | `CALC:ADEV?`, `CALC:DRIF?`, `CALC:OFFS?` |
| `SYST:GPS:STAT?`, `STAT:OPER:COND?` | statistiky odvozené z headline |

Neimplementované vracej jako chybu do `SYST:ERR?`, **ne jako vymyšlené číslo**.

- [ ] `scpi_src_load_cm4()` — plní **tentýž `scpi_src_t`** z IPC snapshotu. Jádro `scpi.c`
      se nemění, ověřeně se kompiluje jako `-DCORE_CM4`.
- [ ] `set_cfg` → **cmd ring** (`IPC_CMD_CFG_SET`, klíče `IPC_CFG_*`).
- [ ] ⚠️ **BLOKUJÍCÍ: sjednotit `SCPI_CFG_*` a `IPC_CFG_*`.** Dnes jsou to dva paralelní
      enumy, které musí sedět 1:1 **bez jakéhokoli compile-time hlídání** (nález z revize
      kódu). Přidat `_Static_assert` **dřív, než na tom poběží síťové zápisy** — jinak
      přeházené pořadí tiše zapíše `LIM:LOW` do `null_ref`.
- [ ] ✅ Kritérium: `*IDN?` a `MEAS:VOLT?` z PC vrátí totéž co USB CDC na CM7.

### F7 — Webserver (**M11**)

- [ ] Statická SPA v bank2 flash (dnes je v ní 8 KB z 1 MB).
- [ ] `/api/meas`, `/api/status` = JSON ze snapshotu; start **pollingem 1 Hz**, WebSocket později.
- [ ] Stahování logů: **IPC file-read okno** (§3 návrhu plánuje 4 KB, **zatím neexistuje**)
      → CM7 plní přes `datalog_read_back()`. Další rozšíření = **IPC v5**.
      Záměrně **ne přes FatFs** — funguje pak stejně pro W25Q i SD.

### F8 — Služby (**M12**)

- [ ] mDNS (`gpsdo.local`, `_scpi-raw._tcp`) = LXI-lite.
- [ ] SNTP jako **cross-check** času proti GPS (ne jako zdroj — GPS je přesnější).

---

## 9. Průřezová témata

### Watchdog CM4

| Etapa | Model |
|---|---|
| dnes / F5 | `iwdg2_kick()` v jednom kontextu — triviálně správné |
| F6+ (pokud FreeRTOS) | **heartbeat + supervisor** jako na CM7 (`watchdog.c`) |

⚠️ Nekopíruj `watchdog.c` doslova — CM7 verze píše **crash black-box do BKP**, což je
„příčina resetu CM7". CM4 do něj sahat nesmí.

🔴 **Reset scope IWDG2 je pořád neověřený** (`DUALCORE_BRINGUP_CHECKLIST.md` §8). Při ladění
boot-loopu 2026-08-11 se `IWDG2RSTF` (bit 27) ani jednou neasertoval, takže o něm nevíme nic
nového. **Ověřit dřív, než na CM4 poběží něco, co se musí umět zotavit.**

### Verzování IPC

| Verze | Obsah | Kdy |
|---|---|---|
| v3 | senzory, kalibrace, Math/limit cfg mirror | hotovo |
| **v4** | + stav linky a IP (CM4→CM7) | **F1** (dopředu, nezávisle na ETH) |
| **v5** | + file-read okno pro stahování logů | F7 |

### Bezpečnost — rozhodnout před F6

⚠️ SCPI i web umí přes cmd ring **měnit konfiguraci přístroje** (`CALC:` SETy → `g_meas_cfg`),
a po síti to bude **bez autentizace**. Volba:
- jen důvěryhodná LAN, nebo
- **read-only jako výchozí** a zápis za přepínačem v UI.

Není to teoretické — je to měřicí přístroj, kterému může kdokoli na LAN přenastavit limity.

### Diagnostika bez konzole

CM4 nemá UART ani displej. Do F1 (a při jakémkoli pádu i potom) je jediná cesta **debugger** —
postup v `memory/debug-bez-konzole.md` (`ST-LINK_gdbserver -g` = attach bez resetu).
Pro CM4 vybrat **jiný access port než pro CM7** (`-m <apID>`) — hodnotu ověřit.

---

## 10. Únikové cesty

| Kdy | Když se nepovede | Kam ustoupit |
|---|---|---|
| **F0** | PHY neodpoví ani po scanu 0–31 | HW problém (strapy/hodiny). **Zatím jsi neinvestoval nic** — SCPI zůstává na USB CDC, které funguje dnes. |
| **F0** | REF_CLK není 50 MHz | Strap `nINTSEL` → HW úprava, nebo externí 50 MHz oscilátor |
| **F3** | Regen rozbije build | `git checkout` na `.ioc` a generované soubory; F0/F1/F2 zůstávají použitelné |
| **F5** | `ping -f` shodí displej | Izolace jader nefunguje dle předpokladu → přehodnotit umístění ETH |
| **F5** | RAM nestačí | Přerozdělit SRAM1 z CM7 (128 KB leží ladem) |
| **F7** | Flash bank2 nestačí na SPA | Assets komprimovat, nebo do W25Q (64 MB) |

**Nejlevnější únik je po F0** — do té chvíle je investovaný jen jeden diagnostický UART příkaz.

---

## 11. Co se tímto plánem NEMĚNÍ

- `scpi.c` jádro — už teď data-source nezávislé (`scpi_src_t`), CM4 backend je jen další plnič.
- IPC snapshot CM7→CM4 — v3 nese plnou sadu instrument-state, na SCPI stačí.
- CM7 — kromě `ipc_cm4_status_t` (v4), zobrazení stavu sítě a diagnostického `eth` příkazu
  se ho to nedotkne.

**Hlavní hodnota dosavadní práce na IPC v2/v3: síť je transport, ne přepis aplikace.**

---

## 12. Historie: co změnil audit (2026-08-11)

První verze plánu měla pořadí `E0 brány → E1 CubeMX → E2 paměť → E3 PHY → E4 link`.
Audit našel čtyři vady:

| # | Nález | Oprava |
|---|---|---|
| 1 | **Největší riziko (regen) předcházelo prvnímu důkazu** — `.ioc` se měnil dřív, než bylo jasné, jestli ETH hardware vůbec žije | Nová etapa **F0**: diagnostika **z CM7** bit-bangem MDIO, bez regenu. SMI je clockované MDC → nepotřebuje REF_CLK, takže obě HW otázky jdou zodpovědět odděleně. |
| 2 | **Riziko adresy DMA řešeno ověřováním** („ověřit v RM0399") | Změněno na **pravidlo návrhu**: DMA buffery vždy na systémové adrese `0x3002xxxx`. Riziko mizí definičně. |
| 3 | **Volba RTOS/smyčka udělaná předem** na základě ekosystémových argumentů | Odsunuta do **F4**, protože F0–F3 jsou na ní nezávislé. Z původních tří argumentů pro FreeRTOS obstál v auditu **jen jeden** (ekosystém); „lwIP si nese vlastní souběžnost" neplatí — `NO_SYS=1` je přesně na to navržený, a `httpd`/`mdns`/`sntp` jsou `NO_SYS`-kompatibilní. |
| 4 | **Gate na #2 byl per etapa** | Zjemněn na **per příkaz** — reálné veličiny (napětí, teploty, GPS) můžou jít po síti hned, čeká jen headline kmitočet a odvozené statistiky. |

Dále: **IPC v4 přesunuto z E4b (důsledek) do F1 (příprava)** — na ETH nezávisí a bez něj
se M9 ladí jen debuggerem. A doplněn nález, že **pinmapa ETH v repu vůbec neexistuje**
(dokumenty znají jen názvy signálů), takže je to první konkrétní úkol.
