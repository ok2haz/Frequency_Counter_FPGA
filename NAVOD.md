# GPSDO / čítač kmitočtu — uživatelský návod

Autoři: **OK2HAZ & OK2JNJ**  ·  Firmware: **gpsdo-ui v0.4.0**  ·  Návod aktualizován 2026-07-20

---

## ⚠️ Stav vývoje — přečti si první

Přístroj je **ve vývoji** a tento návod popisuje i funkce, které zatím nejsou dokončené.
Co platí k firmwaru v0.4.0:

| Funkce | Stav |
|---|---|
| Displej, dotykové ovládání, menu, okna | ✅ funguje |
| GPS příjem, čas z GNSS, RTC | ✅ funguje |
| Teploty, napájecí větve, úroveň RF vstupu | ✅ funguje (reálná data) |
| Zvuková signalizace, alarmy, watchdog | ✅ funguje |
| Záznam dat (datalog) | ✅ funguje |
| **Měřený kmitočet na hlavní obrazovce** | ❌ **zatím SIMULACE** |

> **Velké číslo na hlavní obrazovce zatím NEUKAZUJE měřený kmitočet.** Spojení mezi řídicím
> procesorem a měřicí FPGA není zprovozněné, takže hlavní obrazovka, graf trendu, Allanova
> deviace i histogram zobrazují **náhodně generovaná data kolem 10 MHz**. Slouží k ladění
> vzhledu, ne k měření. Totéž platí pro záznam do datalogu — ukládá se do něj skutečná
> hodnota z FPGA, což je zatím nula.
>
> Stav této práce sleduje `STATUS.md`, položky **#1 a #2**.

Vše ostatní v návodu popisuje reálně funkční chování.

---

## 1. Co přístroj je

Měřič kmitočtu s referencí disciplinovanou z GPS (GPSDO). Skládá se ze dvou desek:

- **Řídicí deska (STM32H757)** — displej, ovládání, GPS, senzory, záznam dat.
- **Měřicí deska (FPGA GW1NR-9)** — vlastní čítač kmitočtu (reciproká metoda se čtyřfázovým
  interpolátorem), vstupní tvarovač a předdělič.

Reference 10 MHz z OCXO se rozvádí přes generátor Si5356A na čtyři hodinové signály 100 MHz
posunuté o 0/90/180/270°, které FPGA používá pro jemné rozlišení času (krok 2,5 ns).

**Přesnost měření je dána přesností té 10MHz reference** — proto GPS disciplinace.

---

## 2. Připojení

| Konektor | Popis |
|---|---|
| **RF vstup** | Měřený signál. Rozsah řetězce je až ~1,4 GHz. |
| **GPS anténa** | Pro disciplinaci reference a přesný čas. Bez antény přístroj měří dál, ale volným během. |
| **USB** | Servisní konzole (virtuální sériový port). Viz kapitola 8. |
| **Napájení** | Přístroj hlídá větve 12 V a 5 V, viz okno Diagnostika. |

> 📌 **Doplnit:** typy konektorů, přesný napájecí rozsah, maximální dovolená úroveň na RF vstupu
> a typ GPS antény (aktivní/pasivní). Tyto údaje zatím nejsou v dokumentaci projektu ověřeny
> a záměrně je zde neuvádím odhadem — hrozilo by poškození přístroje.

---

## 3. První spuštění

Po zapnutí:

1. **Zvukový signál** — krátké stoupavé arpeggio (pokud není zvuk vypnutý).
2. **Úvodní obrazovka** — logo GPSDO, verze firmwaru, datum sestavení a výsledek
   vnitřního testu (`Selftest: PASS`). Drží se asi 1,4 sekundy.
3. **Hlavní obrazovka.**

Přístroj si pamatuje nastavení (jas, zvuk, téma, časová zóna) z minulého zapnutí.

### Když se displej nerozsvítí

Pokud selže start, přístroj to hlásí **blikáním LED a současně pípáním** — počet bliknutí
a pípnutí v jedné skupině říká, který krok selhal. Skupina se opakuje donekonečna.
Zapiš si počet a nahlas ho; sám o sobě jde o poruchu, kterou uživatel neopraví.

Pípání funguje i se zavřeným přístrojem, kdy LED není vidět.

### GPS

První zaměření polohy trvá typicky **několik minut** (studený start i přes 15 minut).
Do té doby ukazuje hlavička `no GPS` a čas běží od nuly. Po prvním zaměření se hodiny
srovnají a dál se dorovnávají každých 10 minut.

---

## 4. Hlavní obrazovka

### Horní lišta

Vlevo je řada barevných „pilulek" se stavem, vpravo čas a datum.

| Pilulka | Význam |
|---|---|
| **GNSS** | Stav GPS: zaměřeno / hledá / bez signálu. **Klepnutím** se otevře okno GPS. |
| **SYS** | Souhrnné zdraví přístroje. **Klepnutím** se otevře okno System Health. |
| **SAT** | Počet použitých družic. |
| **HDOP** | Kvalita geometrie družic (nižší = lepší). |
| **HOLD** | Svítí jantarově, když se ztratilo GPS zaměření a přístroj jede v holdoveru. |
| **CAL** | Informativní údaj o kalibraci. |

Barva **SYS** je nejrychlejší kontrola stavu:

- 🟢 **SYS OK** — vše v pořádku.
- 🟡 **SYS !** — něco je degradované, ale přístroj funguje (např. ztráta signálu na vstupu,
  chyba senzoru, zotavení po restartu).
- 🔴 **SYS ERR** — kritická chyba, typicky **ztráta 10MHz reference** nebo neúspěšný vnitřní test.
  Měření v tomto stavu není důvěryhodné.

Vpravo je čas a pod ním datum se zkratkou časové zóny. Čas se zobrazuje v **místní zóně**;
dokud není srovnaný z GPS, ukazuje `--:--:--`. Vlevo od času se objeví **přeškrtnutý reproduktor**,
když je vypnutý zvuk.

Hodiny běží plynule i po ztrátě GPS — přístroj má vlastní zálohovaný oscilátor.

### Velké číslo

Měřený kmitočet ve formátu `123.456.789,01234 Hz` — tečky oddělují tisíce, čárka desetinnou část,
vždy 5 desetinných míst.

- **Zešedne**, když se ztratí signál na vstupu nebo spojení s měřicí deskou.
- **Podbarví se lehce červeně**, když je měření zastavené tlačítkem STOP.

### Dolní část

| Prvek | Popis |
|---|---|
| **Allanova deviace** (vlevo) | Graf stability σy(τ) v log-log měřítku. **Klepnutím** se zvětší na celou obrazovku. |
| **Offset / σy 1s / Drift** | Tři okénka se statistikou. |
| **Trend** | Průběh odchylky v čase. **Klepnutím** se zvětší (okno až 60 dní). |
| **RF signál** | Sloupcový graf úrovně vstupního signálu v dBm. **Reálný údaj.** |

### Tlačítka dole

| Tlačítko | Funkce |
|---|---|
| **Main SW** | ⚠️ **Dočasné.** Přepíná mezi dvěma verzemi rozvržení hlavní obrazovky (OLD/NEW) — pozůstatek ladění vzhledu. V budoucí verzi zde bude přepínač režimu měření (kmitočet / perioda). |
| **RUN / STOP** | Spouští a zastavuje měření. Tlačítko ukazuje **akci, která se provede**: když měření běží, svítí červené **STOP**; když stojí, zelené **RUN**. Při zastaveném měření je velké číslo podbarvené červeně. |
| **GATE** | Délka měřicího okna (0,1 / 1 / 10 / 100 s). |
| **CHAN** | Volba kanálu (CH A / CH B). |
| **MENU** | Otevře hlavní menu. |

### Spořič displeje

Po nastavené době nečinnosti displej ztlumí jas a ukáže velké hodiny. **První dotek pouze
probudí** displej — nespustí žádnou akci, takže se nedá omylem něco přepnout.

---

## 5. Menu

Tlačítko **MENU** otevře rozcestník s dvanácti dlaždicemi. Tlačítkem **ZPĚT** se vždy vrátíš
tam, odkud jsi přišel.

| Dlaždice | Co ukazuje |
|---|---|
| **Diagnostika** | Technický přehled: teploty, napětí, stav komunikace s FPGA, reference, systém. |
| **Nastavení** | Jas, zvuk, spořič, vzhled, jazyk. |
| **System Health** | Zdraví systému: paměť, zátěž procesoru, chybovost sběrnic, příčina posledního restartu. |
| **Čítač** | Syrový detail měření z FPGA — pro ladění, ne pro běžné použití. |
| **Holdover** | Stav disciplinace: WARMUP / LOCK / HOLDOVER / NO LOCK. |
| **Datalog** | Záznam měření, viz kapitola 7. |
| **Alarmy** | Které stavy jsou hlídané a kolikrát nastaly. |
| **Kalibrace** | Kalibrační konstanty, viz kapitola 6. |
| **Čas** | Časová zóna. |
| **Placeholder 1–3** | Zatím prázdné, připravené pro budoucí funkce. |
| **RESTART** (dole) | Restart přístroje, s potvrzením. |

Některá okna se otevírají z místa, kam patří, ne z menu:

- **GPS** a **System Health** — klepnutím na pilulku v horní liště.
- **Allan** a **Trend** — klepnutím na příslušnou kartu na hlavní obrazovce.
- **Senzory** — z okna System Health.
- **O přístroji** a **Reference** — z okna Nastavení.
- **Paměť** a **Selftest** — z okna Diagnostika.
- **Histogram** — z okna Allan (a zpět).

### Okno Holdover

Nejdůležitější okno pro posouzení, jestli se dá měření věřit:

| Stav | Význam |
|---|---|
| **WARMUP** | Přístroj se rozbíhá (první ~3 minuty). Reference ještě není ustálená. |
| **LOCK** | Reference je disciplinovaná z GPS. **Tady měř.** |
| **HOLDOVER** | GPS zaměření se ztratilo, přístroj drží poslední známé nastavení reference. Přesnost se pomalu zhoršuje. |
| **NO LOCK** | Nikdy nedošlo k zaměření — zkontroluj anténu. |

---

## 6. Nastavení a kalibrace

### Nastavení

| Položka | Rozsah |
|---|---|
| **Zvuk** | Zapnuto / vypnuto (ztlumí i alarmy a testovací pípnutí). |
| **Jas** | 25–255. Nikdy nejde na nulu, aby zůstalo vidět na ovládání. |
| **Spořič** | Zapnuto/vypnuto + prodleva 15–600 s. |
| **Vzhled** | Tmavé / světlé schéma. |
| **Jazyk** | Česky / English (zatím jen připravené, texty jsou české). |

Nastavení se ukládá do vnitřní paměti a **přežije i odpojení napájení**.

> ⚠️ Po aktualizaci na v0.4.0 se nastavení **jednou vrátí na výchozí hodnoty** (změnil se formát
> uloženého záznamu). Přenastav si je znovu — od té chvíle už drží.

### Časová zóna

RTC běží vždy v UTC, zóna je jen zobrazovací posun.

- **AUTO CET/CEST** — automatický letní čas podle evropského pravidla.
- **Ruční** — posun −12 až +14 hodin.

Místní čas se ukazuje na hlavní obrazovce a ve spořiči. **UTC zůstává** v okně GPS,
v diagnostice a na konzoli.

### Kalibrace

V okně Kalibrace se tlačítky `−` / `+` mění:

- **AD8307 slope / intercept** — přepočet úrovně RF vstupu na dBm.
- **Zesílení větví 12 V a 5 V** — přepočet měřených napájecích napětí.

Změna se projeví **okamžitě**, ale do paměti se uloží až tlačítkem **ULOŽIT**.

> Výchozí hodnoty jsou katalogové, ne změřené na konkrétním kusu. Údaj o úrovni RF vstupu
> je proto zatím orientační (`STATUS.md` #7).

---

## 7. Záznam dat (datalog)

Přístroj umí průběžně zaznamenávat stabilitu do vnitřní paměti.

- **Perioda:** jeden záznam za 10 sekund.
- **Kapacita:** přibližně **600 dní**, pak se přepisují nejstarší záznamy (záznam nikdy „nedojde").
- **Obsah:** kmitočet, teplota OCXO a desky, ladicí napětí, úroveň RF, stav GPS.

V okně **Datalog** je vidět stav, počet záznamů, kapacita a tlačítko **ZAPNOUT / VYPNOUT**.
Nastavení se pamatuje přes vypnutí.

Záznam přežije restart i výpadek napájení — pozice zápisu se po zapnutí dohledá sama.

> 📌 **Omezení dnešní verze:** data se dají číst jen po jednotlivých záznamech přes konzoli
> (posledních 10). Hromadný export zatím není hotový. Do doby zprovoznění měření (#2)
> je navíc zaznamenaný kmitočet nulový.

---

## 8. Servisní konzole

Přístroj se po připojení **USB** kabelu hlásí jako **virtuální sériový port**. Stačí libovolný
terminál (PuTTY, Tera Term, `screen`); u virtuálního portu nezáleží na nastavené rychlosti.
Příkazy se potvrzují Enterem.

### Užitečné příkazy

| Příkaz | Co udělá |
|---|---|
| `help` | Vypíše seznam příkazů. |
| `version` | Verze firmwaru (shodná s údajem na displeji). |
| `status` | **Souhrn stavu** — verze, doba běhu, **příčina posledního restartu**, paměť, zátěž procesoru, stav záznamu. První místo, kam se podívat po nečekaném restartu. |
| `freq` | Poslední změřený kmitočet. |
| `gps` | Stav GPS (zaměření, družice, poloha, čas). |
| `rtc` | Čas z RTC a zda je srovnaný z GPS. |
| `sensors` | Všech 10 senzorů včetně minim, maxim a chybovosti. |
| `temperature` | Teplota. |
| `selftest` | Spustí vnitřní testy (bezpečné za běhu). |
| `datalog` | Stav záznamu. `datalog on` / `off` zapne a vypne, `datalog dump` vypíše posledních 10 záznamů. |
| `beep on` / `beep off` | Zapne a vypne zvuk. |
| `screen main` | Překreslí hlavní obrazovku. |

### ⚠️ Příkazy, které něco zničí nebo restartují

Používej jen vědomě:

| Příkaz | Následek |
|---|---|
| `datalog erase` | **Smaže celý záznam.** Nevratné. |
| `qspitest`, `storetest` | Destruktivní testy paměti. |
| `stacktest yes` | **Záměrně způsobí restart přístroje** (servisní test ochrany). |

---

## 9. Zvuková signalizace

| Zvuk | Význam |
|---|---|
| Stoupavé arpeggio | Zapnutí přístroje. |
| **3 krátká pípnutí** | **Ztráta signálu na vstupu.** |
| **2 pípnutí** | **Ztráta GPS zaměření.** |
| **1 pípnutí** | Obnovení předchozího stavu. |
| Opakované skupiny pípnutí + blikání LED | Porucha při startu (viz kapitola 3). |

Přístroj **při zapnutí nikdy nepípá alarmem** — první zaměření ani první navázání spojení
se nehlásí. Alarm se ozve jen při skutečné *ztrátě* něčeho, co už fungovalo. Provoz bez
antény na stole tedy nepípá.

Vypnutý zvuk (mute) umlčí i alarmy.

---

## 10. Řešení problémů

| Projev | Co s tím |
|---|---|
| **Displej nesvítí** | Viz kapitola 3 — počítej bliknutí a pípnutí. |
| **`--:--:--` místo času** | Ještě nebylo GPS zaměření. Zkontroluj anténu, počkej i 15 minut. |
| **🔴 SYS ERR** | Nejčastěji ztráta 10MHz reference. Otevři **Diagnostiku**, karta *Reference Si5356*. Měření v tomto stavu nepoužívej. |
| **Kmitočet je šedý** | Není signál na vstupu nebo neběží spojení s měřicí deskou. |
| **Přístroj se sám restartoval** | Připoj konzoli a napiš `status` — vypíše příčinu (`WATCHDOG!`, `stall:UiTask` apod.). Tento údaj nahlas, je klíčový pro diagnostiku. |
| **Zapomenuté nastavení po aktualizaci** | Očekávané u v0.4.0, viz kapitola 6. |
| **Ztracené GPS zaměření** | Okno **Holdover** ukáže, jak dlouho přístroj jede bez GPS. |

Přístroj má **hlídací obvod**, který ho při zatuhnutí sám restartuje (do ~4 sekund).
Ojedinělý samovolný restart proto neznamená ztrátu funkce, ale měl by se nahlásit.

---

## 11. Technické údaje

| Parametr | Hodnota |
|---|---|
| Rozsah měření | až ~1,4 GHz (řetězec tvarovače a předděliče) |
| Metoda | reciproká, čtyřfázová interpolace, jemný krok 2,5 ns |
| Měřicí okno | 0,25 s (interní), zobrazená perioda volitelná |
| Zobrazení | 5 desetinných míst v Hz |
| Reference | OCXO 10 MHz, disciplinovaný z GPS |
| Displej | 4,3", 800×480, dotykový |
| Měření úrovně RF | AD8307, rozsah −80 až +10 dBm |
| Záznam dat | 64 MB, ~600 dní při 10 s / záznam |
| Hodiny reálného času | zálohované, s automatickým letním časem |

---

## Poznámka k tomuto návodu

Toto je **první verze** a některé části zatím chybí:

- konektory, napájecí rozsah a **maximální dovolená úroveň na RF vstupu** (kapitola 2),
- postup kalibrace úrovně RF proti známému zdroji,
- hromadný export dat ze záznamu,
- popis režimu měření periody (až nahradí dočasné tlačítko *Main SW*).

Chybějící údaje jsem **záměrně nedoplnil odhadem** — u dovolené úrovně na vstupu by odhad
mohl znamenat zničený přístroj.
