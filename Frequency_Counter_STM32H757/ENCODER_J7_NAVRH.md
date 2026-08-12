# Encoder + J7 LED/tlačítka + AlarmMgr (#29) — příprava

> Rozpracování `STATUS.md` #29 do zadání. **Nezávislé na #1/#2 i na dvoujádru** —
> dá se dělat kdykoli. Přidáno 2026-08-11.
>
> 🔴 **MÍRA JISTOTY:** pinout je převzatý ze `STATUS.md` a **nebyl ověřen proti schématu**.
> Existence tlačítek na J7 je otevřená otázka (viz §1). Ber to jako zadání, ne jako recept.

---

## 0. Hlavní zjištění: TIM1 je ta snadná část

Kvadraturní dekodér v HW je ~20 řádků a stojí 0 % CPU. **Skutečná práce je jinde:**
celé UI je dnes výhradně dotykové (`app_gpsdo_handle_touch(x, y)` → hit-test podle
souřadnic). Enkodér ale nemá souřadnice — potřebuje **model fokusu**, který v aplikaci
zatím **vůbec neexistuje**.

To je jádro #29. Kdo to podcení, skončí u enkodéru, který umí jen měnit jas.

---

## 1. Hardware — co ověřit dřív než cokoli jiného

✅ **OVĚŘENO ZE SCHÉMATU 2026-08-11** (`STM32H747BIT.pdf` list 2/7 „CPU") + křížově proti `.ioc`:

| Signál | Pin | Stav |
|---|---|---|
| `ENCODER_CH1` | **PA8** (pin 146) → TIM1_CH1 | ✅ v `.ioc` volný |
| `ENCODER_CH2` | **PA9** (pin 147) → TIM1_CH2 | ✅ v `.ioc` volný |
| `ENCODER_CH3` | **PC13** (tlačítko) | ✅ v `.ioc` volný |
| Konektor | **J2 „Encoder"** (3 signály + GND/3V3) | na desce osazený |

🎉 **Riziko „PA9 = USB VBUS" je VYVRÁCENO.** USB je na téhle desce vyvedené jako
`PA11 = USB_OTG_FS_DM` a `PA12 = USB_OTG_FS_DP`; **VBUS sense se nepoužívá**. PA9 je
volný pro enkodér. Tím padá hlavní blokátor #29 a TIM1 CH1/CH2 je použitelné tak,
jak `STATUS.md` předpokládal.

- [ ] ⚠️ **PC13 přesto ověřit prakticky** — má na H7 omezený drive a sdílí funkci s
      WKUP/TAMPER. Jako *vstup* s pull-upem je to v pořádku, ale hlídej, ať se z něj
      omylem nestane výstup.
- [ ] J7 LED ×6 (PWR/LOCK/DISC/ALARM/LOG/LINK) — piny a aktivní úroveň dohledat
      (na listu 2/7 jsou vidět `LED1`–`LED5` a poznámka *„Zkontrolovat pinout a smysl LED 5–8"*,
      takže to není dořešené ani ve schématu).
- [ ] J7 tlačítka — ⚠️ **pořád nepotvrzeno, že existují**.
- [ ] Ověřit, jestli jsou A/B **hardwarově odrušené** (RC filtr). Pokud ne, počítej
      se softwarovým filtrem — TIM má vlastní (`IC1Filter`), viz §2.
- [ ] Rozhodnout, jestli J7 tlačítka vůbec budou. **Bez nich má enkodér jen jedno
      tlačítko (PC13)** a navigační model musí vystačit s „otoč + klikni".

---

## 2. TIM1 v kvadratuře — konfigurace

```c
/* Enkodér = HW kvadratura, 0 % CPU: TIM1 počítá hrany sám, my jen čteme CNT. */
htim1.Init.Period = 0xFFFF;                   /* volný běh, čteme rozdíl */
sConfig.EncoderMode = TIM_ENCODERMODE_TI12;   /* x4 dekódování (obě hrany obou kanálů) */
sConfig.IC1Filter = 0x0F;                     /* max filtr — mechanické enkodéry drncají */
sConfig.IC2Filter = 0x0F;
HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL);
```

- **Čtení = rozdíl CNT**, ne absolutní hodnota: `int16_t d = (int16_t)(cnt - prev); prev = cnt;`
  Přetečení uint16 se tím vyřeší samo (znaménkový rozdíl).
- **Detenty:** mechanické enkodéry dávají typicky **4 kroky na klapnutí**. Dělit 4,
  jinak jedno klapnutí přeskočí 4 položky. Ověřit na konkrétním kusu.
- ⚠️ **TIM1 nepoužívat jinde** a nepovolovat v `.ioc` pro nic jiného — stejné pravidlo
  jako u TIM7 (beeper), viz `CLAUDE.md`.

**Kde to číst:** enkodér nepotřebuje vlastní task. Nejlevnější je přičíst ho do
**UiTasku** vedle pollingu dotyku (~15 Hz je na navigaci dost) — CNT se čte i tak
bezztrátově, protože počítá HW.

---

## 3. Model fokusu — tady je ta práce

Dnes: `app_gpsdo_handle_touch(x, y)` → každé okno si dělá vlastní hit-test proti
obdélníkům (`ANIM_TOGGLE_RECT`, `TZ_AUTO_RECT`, …).

Enkodér potřebuje **seznam fokusovatelných prvků** a `s_focus_idx`. Tři možné cesty:

| Varianta | Jak | Pro | Proti |
|---|---|---|---|
| **A. Per-okno tabulka** | Každé okno vystaví `focus_rects[]` + handler indexu | Malý zásah, jde dělat postupně okno po okně | Duplicita — 30+ oken |
| **B. Sdílený registr prvků** | Okno při renderu registruje prvky (`ui_focus_add(rect, id)`) | Jedno místo, funguje všude stejně | Vyžaduje zásah do každého `render_*` |
| **C. Enkodér jen jako „virtuální dotek"** | Otočení posouvá kurzor po mřížce, klik = `handle_touch(x,y)` středu | **Nulový zásah do oken** | Neintuitivní, kurzor po obrazovce |

**Doporučení: B, ale zaváděné postupně.** `ui_focus_add()` volané v renderu je
levné a přirozeně sedí k tomu, jak okna už dnes kreslí prvky. Do oken, kde
ještě registrace není, enkodér prostě nic nedělá — degradace bez rozbití.

**Minimální užitečný začátek (pokud chceš rychlý výsledek):** enkodér obsluhuje
jen **hlavní obrazovku** (footer tlačítka) a **Nastavení** (jas, prodleva auto-dim).
To pokryje „ovládání bez doteku" pro nejčastější operace a ověří celý řetěz.

⚠️ **Interakce s auto-dim:** první *otočení* má stejně jako první dotek jen probudit
displej, ne provést akci (`AUTODIM_LEVEL`, viz `CLAUDE.md`). Nezapomenout, jinak
uživatel v šeru omylem něco přepne.

---

## 4. J7 LED — zrcadlo stavu

6 LED, každá čte existující globál. **Žádná nová logika**, jen mapování:

| LED | Zdroj | Sviť když |
|---|---|---|
| PWR | — | napájení (natvrdo on) |
| LOCK | `gps_get()` | `valid && fix_mode >= 2` |
| DISC | holdover stav | disciplinace běží (dnes: fix + FPGA link) |
| ALARM | `g_meas_verdict`, `g_alarm_*` | FAIL nebo aktivní alarm |
| LOG | `datalog_get_status()` | logování zapnuté |
| LINK | `g_spi_ok` | FPGA link žije |

- Aktualizovat **1×/s v UiTasku** (změnová detekce, ne slepý zápis).
- ⚠️ Respektovat auto-dim? **Ne** — stavové LED mají svítit i při ztlumeném displeji.

---

## 5. AlarmMgr — modul, ne task

STATUS to říká výslovně. Dnešní `alarm.c` už dělá hranovou detekci (FPGA stale,
GPS lock, limit pass/fail) a je jediný volající `beeper_set()`. **AlarmMgr = rozšíření
téhož modulu**, ne nová vrstva:

- [ ] Vyvést stav alarmu do **struktury** (co je aktivní, od kdy, jak závažné) místo
      dnešních jednotlivých počitadel — potřebuje to LED ALARM, okno Alarmy i SCPI
      `STAT:OPER:COND?`.
- [ ] **Potvrzení/utišení** (acknowledge): dnes jde jen globální mute. Užitečné je
      „tenhle alarm beru na vědomí" bez umlčení ostatních.
- [ ] ⚠️ Zachovat `alarm_tick()` v defaultTasku a **neblokující** vzor (fáze přes
      `HAL_GetTick`) — defaultTask krmí watchdog.

---

## 6. Doporučené pořadí

1. **Ověřit HW** (§1) — hlavně PA9 vs USB VBUS. Bez toho nemá smysl psát řádek.
2. **TIM1 + čtení delty** → UART příkaz `enc` vypisující kroky = důkaz, že to jede.
3. **J7 LED** — nejjednodušší viditelný výsledek, žádná nová logika.
4. **Fokus na 2 oknech** (hlavní + Nastavení) → ověří koncept, než se sáhne na zbytek.
5. **AlarmMgr** — až bude LED ALARM, aby bylo kam stav vyvést.
6. Rozšířit fokus na ostatní okna postupně.

Kroky 2–3 jsou samostatně užitečné i kdyby fokus (§3) nikdy nedozrál.
