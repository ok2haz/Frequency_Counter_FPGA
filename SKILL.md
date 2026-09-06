# SKILL.md — jak v tomhle projektu pracovat, aby se chyby neopakovaly

> **Role dokumentu:** `CLAUDE.md` popisuje **fakta o kódu** („jak to funguje dnes"),
> `STATUS.md` drží **úkoly**, tenhle soubor drží **metodu** — čím se tady ověřuje,
> jaká třída chyb se opakuje a jak ji poznat dřív, než stojí den ladění.
>
> 🔴 **Do tohohle souboru NEPATŘÍ konkrétní hodnoty registrů, adresy ani pinout.**
> Ta fakta žijí v `CLAUDE.md` / `docs/HW_REFERENCE.md` a duplikovat je sem by
> vyrobilo přesně to rozejití, před kterým projekt varuje. Odkazuj, necituj.

Dokument vznikl 2026-08-31 revizí toho, co se v projektu skutečně stalo — každý bod
má konkrétní důkaz, ne obecnou radu.

---

## 1. Měřicí nástroj sám může být příčinou poruchy

**Důkaz:** čtení ladicí sondou (`STM32_Programmer_CLI -r32`) za běhu **haltuje cíl**.
Když halt padne doprostřed transakce na I2C4, ATTINY (bit-bang slave, který musí
sledovat každou transakci) ztratí synchronizaci a **celá sběrnice umře natrvalo**.
Naměřeno: 0 chyb → 6 → 13 → 22 po třech čteních, pak `scanner` nenajde nic.

Část „náhodných" úmrtí I2C4, které jsem celý den vyšetřoval, si vyrobilo samo
vyšetřování. A `CLAUDE.md` k tomu ještě chvíli obsahovala větu, že sonda je na
stavová data v pořádku — tedy dokument aktivně licencoval tu chybu.

**Jak to dělat:** než něco změříš, zeptej se, **co ten akt měření mění**. Diagnostiku
běžícího přístroje dělej přes UART (`status`, `stats`, `sensors`, `scanner`,
`flightrec`), sonda jen na flash a reset. Závěry o příčině dělej **jen z běhů, kde
se nesahalo měřidlem**.

## 2. Negativní a pozitivní kontrola, ne jen „vyšlo to"

Dvakrát mě zachránila:

- **Pozitivní:** `-fanalyzer` je s `-fsyntax-only` **tiše vypnutý**. Vypadalo to
  jako „0 nálezů", přitom analyzátor vůbec neběžel. Odhalila to až úmyslná chyba
  (`int*q=0; return *q;`), která **musí** dát varování.
- **Negativní:** tvrzení „sonda zabíjí I2C4" jsem doložil 640s během jen přes UART
  → 0 chyb. Bez toho by to byla korelace, ne příčina.

**Jak to dělat:** u každé kontroly si ověř, že **umí selhat**. Když nemáš, jak ji
donutit ohlásit chybu, nevíš, jestli běží.

## 3. Nejdražší je tiché selhání — ptej se „jak vypadá, když to nic nedělá?"

Tahle třída se v projektu opakovala nejčastěji:

| Tiché selhání | Jak se projevilo |
|---|---|
| `%f` v `printf` (nano.specs bez float) | nevytiskne **nic** — dBm ukazovalo prázdno |
| chybějící glyf v subsetovaném fontu | `prim_draw_text` ho **tiše přeskočí** → 15 neviditelných řetězců včetně splash loga |
| `-fanalyzer` + `-fsyntax-only` | analyzátor mlčí, protože neběží |
| zdroják v `subdir.mk`, ale ne v `objects.list` | `.o` je správný, ale linker ho nevidí → `undefined reference` |
| `ALIAS_PROBES` příliš malé | test hlásil „bez aliasu", aniž se podíval do pásma, kde se alias podezírá |
| nastavení brány, které nikam nevede | a přitom **krmí výpočet nejistoty** → okno hlásí 400× lepší číslo |

**Jak to dělat:** u každého guardu, testu a formátovacího řetězce si polož otázku
**„jak to vypadá, když tahle kontrola nedělá nic?"**. Když je odpověď „stejně jako
když projde", je to chyba čekající na svůj den.

## 4. Neodvozuj konstantu, když ti ji může někdo deklarovat nebo ověřit

**Důkaz:** násobitel `edge_count` byl natvrdo ×4. Emulátor ale počítá periody
neděleného signálu → `fpgasim on 10000000` hlásil **40 MHz**. Opraveno commitem
`a6c0128` — a **při psaní datové cache jsem tutéž chybu udělal znovu**, protože
jsem si násobitel odvodil sám.

**Jak to dělat:** takové číslo patří do **jedné funkce, která ho ověřuje** proti
autoritativnímu údaji (`fpga_freq_hires_mul`), a při neshodě raději degraduje na
horší, ale poctivý výsledek. Ještě lépe: ať ho **producent deklaruje** v datech
(pole `presc` v protokolu v3). Kdo si ho odvodí sám, zopakuje tu chybu potřetí.

## 5. Duplicitní fakt se dřív nebo později rozejde

Nalezeno v jedné session: TDC 2500 ps na třech místech, brána počítaná ze dvou
různých zdrojů (jedno poctivě z rámce, druhé z UI nastavení), inicializace encoderu
ve dvou souborech, konstanta 3 (počet framebufferů) na cestě k duplikaci.

**Jak to dělat:** jeden zdroj, vystavený **funkcí** (`prim_stm32_fb_count()`,
`encoder_div()`), ne zkopírovanou konstantou. Když fakt musí být na dvou místech,
napiš do obou, kde je to druhé.

## 6. „Je to stejné, nekresli" je pod vícenásobným bufferováním past

**Důkaz:** guard trendu („pixelově identické → přeskoč") byl **jeden stav, ale
framebuffery jsou tři** — obsah zůstal jen v tom, do kterého se zrovna kreslilo.
⚠️ **OPRAVENO 2026-09-05:** dřív tu stálo, že tenhle guard problikávání
**způsobil**. Nezpůsobil. Byla to skutečná chyba (a stejnou měly další tři
guardy), ale **příčinou problikávání byl 4,7× pomalý refresh SDRAM** — guard jen
sundal masku (viz §6d). Ta záměna stála tři kola ladění ve špatné vrstvě;
nech ji tu zapsanou jako varování, ne jako fakt o příčině.

**Jak to dělat:** každý skip-guard musí přeskakovat **až po tolika vykresleních,
kolik je bufferů**. Nespoléhej na copy-forward: kopíruje dirty z posledních dvou
snímků, a když se kvůli přeskakování neflipuje, historie z dosahu vypadne.
Obecněji: **cache/skip vždy počítej vůči počtu konzumentů**, ne vůči jednomu.

## 6d. Symptom po změně X neznamená, že X je příčina — X mohla jen sundat masku

**Důkaz (celý případ „displej problikává", 2026-08-30 až 2026-09-05):**
`REFRESH_COUNT = 1835` byl v `fmc.c` **od úplně prvního commitu** a znamenal, že
se matice SDRAM obnoví za **304 ms místo 64 ms — 4,7× mimo spec**. Roky to
nevadilo, protože **framebuffery se přepisují každý snímek a LTDC je nepřetržitě
čte** — obojí řádky implicitně obnovuje. Když 2026-08-30 přibyl guard „obsah je
stejný → nekresli", **ubylo přepisování** a vada se poprvé projevila.

Symptom se objevil hned po té změně, takže se hledalo tam:
- tři kola ladění šla do **vykreslovacího kódu** (#88, #107, přepracování guardů),
- vznikla **HW hypotéza #72** (studený spoj na `PF15`/`FMC_A9`), která by vedla
  k **přepracování desky**,
- skutečná příčina ležela v jednom `#define`, který měl **v komentáři spočítanou
  správnou hodnotu**.

**Jak to dělat:** když se porucha objeví po změně X, ptej se nejen *„co X
rozbilo"*, ale hlavně *„co X přestalo dělat"*. Optimalizace, které něco dělají
**méně často** (skip, cache, lazy, dirty-rect), typicky **odkrývají latentní vady
pod sebou** — samy nic nerozbijí. Konkrétně u displeje: **než sáhneš na kreslení,
změř paměť** (`membench`, řádek retence).

## 6e. Jedno čisté měření neruší výpočet

**Důkaz:** u `REFRESH_COUNT` byl v komentáři **spočítaný správný výsledek (371)**,
zdůvodnění i **návod, čím to změřit**. Hodnota se přesto nechala na 1835, protože
**jedno** měření retence vrátilo „0 chybných bitů". O dvanáct dní později dal
tentýž test **1 048 646 chybných bitů** a displej byl černý.

**Jak to dělat:** když výpočet říká „je to řádově mimo spec" a jedno měření říká
„ok", **vyhrává výpočet** — měření mohlo minout okno (retence závisí na čase,
teplotě i na tom, co mezitím paměť přepisovalo). Buď měř opakovaně a za různých
podmínek, nebo hodnotu rovnou oprav. **Rozpor mezi výpočtem a jedním vzorkem
nezavírej ve prospěch vzorku.**

## 6f. Diagnostika, která čte z rozbitého média, lže

**Důkaz:** `membench` hlásil „PŘEKRYV/CIZÍ ZÁPIS do kontrolní buňky — vzdálenost
nelze určit". Z toho vzniklo podezření **#72** na studený spoj adresní linky,
které viselo otevřené týdny. Žádný překryv neexistoval — **kontrolní buňka jen
vyhasla**, což vypadá stejně jako by ji někdo přepsal. Po opravě refreshe hlášení
zmizelo úplně.

**Jak to dělat:** verdikt, který stojí na tom, že si médium **pamatuje**, co jsi
tam zapsal, je platný **jen nad médiem s ověřenou retencí**. Drž pořadí:
**nejdřív retence, teprve pak alias/překryv**. Totéž platí obecně — diagnostika
postavená nad vadnou vrstvou generuje falešné nálezy o vrstvě pod ní.

## 6g. „Dřív to šlo" ⇒ bisect je PRVNÍ krok, ne poslední

**Důkaz (2026-09-04):** uživatel řekl „v minulosti to bylo ok, ještě včera".
Bisect jsem **nabídl dvakrát a neudělal**; místo toho jsem vyprodukoval tři chybné
hypotézy (animace — vyvrátil uživatel; `.sdram` NOLOAD — vyvrátil jsem si sám;
hladovění LTDC — nedoloženo). Když se bisect konečně udělal, dal odpověď **za
jedno kolo**: „včerejší" stav byl **horší** (černý displej místo problikávání),
takže o regresi vůbec nešlo a celá otázka byla položená špatně.

**Jak to dělat:** `git stash` je levný a plně vratný. Jedno kolo flash+test dá
**tvrdou** odpověď, kterou žádná hypotéza nedá. Udělej ho hned — a ber vážně
i výsledek „starý stav je horší", ten otázku přeformuluje.

## 6h. Korelace 1:1 není mechanismus

**Důkaz:** naměřil jsem `flip +109` a `LTDC podtečení +109` za 5 s a prohlásil to
za **důkaz**, že DMA2D hladoví LTDC. Přitom je to stejně dobře konzistentní s tím,
že jedno podtečení je **běžný artefakt přehození `CFBAR`** při každém flipu. Z
korelace jsem udělal příčinu a postavil na ní opravu, která problém nevyřešila.

**Jak to dělat:** dokonalá korelace říká *„souvisí to s flipem"*, ne *„flip to
způsobuje hladověním"*. Než z korelace uděláš mechanismus, měj **kontrolní
experiment**, který jím hýbe (u téhle: změnit mrtvý čas DMA2D a sledovat, jestli
počet klesá). Bez něj to piš jako hypotézu, ne jako zjištění.

## 6b. Překryv musí mít jednoho vlastníka pixelu — a kreslit se jako poslední

**Důkaz (2026-09-01):** varovný pruh §12 jsem umístil pod hlavičku, kde má hlavní
obrazovka titulní řádek, a kreslil ho z 2Hz tiku. Jenže titulek a velké číslo se
kreslí 20×/s, takže se vrstvy přebíjely — pruh „překrýval kmitočet a nevykresloval
se správně".

**Jak to dělat:** u každého překryvu si odpověz na dvě otázky:
1. **Kdo ještě kreslí do těch pixelů?** Když někdo, urči jediného vlastníka
   (u nás globální příznak `g_warn_over_uncert` — buď σ+N, nebo varování, nikdy oba).
2. **Kreslí se překryv jako poslední v snímku?** Když ne, vyhraje ten, kdo kreslí
   častěji. Správné místo je těsně před flipem, ne v tiku.

Bonus: kreslení před každým flipem zároveň zaručí, že překryv má **každý buffer**
— tedy tentýž problém, který řeší §6.

## 7. Generované soubory, které nejsou v gitu, jsou past při každém novém zdrojáku

`subdir.mk` **i** `objects.list` (response file linkeru) generuje IDE a ani jeden
není ve verzování. Přidání souboru do prvního stačí na *překlad*, ale ne na
slinkování.

**Jak to dělat:** po přidání zdrojáku zkontroluj **oba** seznamy — `scripts/build.sh`
na to už kontrolu má a hlásí ji. **Nefiltruj si výstup buildu jen na `error`**;
zrovna tuhle hlášku jsem si takhle zahodil a stálo mě to dvě kola překladu.

## 7b. Úspěšný build **nedokazuje**, že tvá změna dorazila do kódu

**Důkaz (2026-08-31):** skript, který měl vložit volání `app_gpsdo_handle_encoder()`
do UiTasku, spadl na pozdějším assertu **před zápisem souboru**. Změna se ztratila.
Build prošel, selftest 16/16, deska běžela — a **celá Fáze A byla mrtvá**, protože
nevolanou funkci `--gc-sections` prostě zahodí. Odhalil to až audit mrtvého kódu
(`nm` proti `.elf`) o několik hodin později. Diagnostický příkaz `enc` přitom fungoval,
protože volá `encoder_poll()` přímo — testování „přes UART" by chybu nikdy nenašlo.

**Jak to dělat:** po každé skriptované úpravě **ověř výsledek v souboru**, ne jen
návratový kód skriptu. U nové funkce zkontroluj, že je **v `.elf`** (`nm | grep`),
ne jen že se přeložila. Skript, který mění soubor, musí zapisovat **před** jakoukoli
další kontrolou, ne po ní.

## 7c. Sken mrtvého kódu má dvě třídy falešně pozitivních

`nm` symbolů v `.o` proti `.elf` najde, co linker zahodil — ale **ne všechno zahozené
je mrtvé**:

| Vypadá mrtvě | Ve skutečnosti |
|---|---|
| `scpi_gate_s` | **použitá, jen inlinovaná** — vnější kopii `--gc-sections` zahodí (3 volání uvnitř TU) |
| `ws_panel_set_portc` | vypnutá **přepínačem** `I2C4_RECOVERY_TOUCHES_ATTINY 0`, tedy kód čekající na výsledek experimentu |
| `prim_text_height` | **veřejné API knihovny** — že ji tahle aplikace nepoužívá, není důvod ji mazat |

**Jak to dělat:** u každého nálezu se zeptej **proč** zmizel, ne jen že zmizel.
Smaž jen to, kde umíš ukázat, že to nikdo nevolá **a nemá volat**.

## 7d. Kontrola, která neproběhla, hlásí nulu nálezů

`-fanalyzer` je s `-fsyntax-only` tiše vypnutý — to už tu stálo. **Je to jen jeden
případ obecnějšího jevu**, na který jsem 2026-09-01 naletěl znovu, jinou cestou:
auditní průchod hlásil „81 souborů, 0 varování", ale **57 z nich se vůbec
nepřeložilo** (`fatal error: adc.h`), protože jsem vzal flagy z jednoho
`subdir.mk` pro všechny adresáře. Nula varování byla pravdivá a zároveň bezcenná.

⚠️ Konkrétně u tohohle projektu: flagy ber ze `subdir.mk` **toho** adresáře, ale
spouštěj z **`CM?/Release`** — `make` běží odtud a `-I../Core/Inc` je relativní
k němu, ne k adresáři se `subdir.mk`.

**Pravidlo:** u každého nástroje, který „nic nenašel", si nejdřív ověř, že vůbec
běžel — počtem úspěšně zpracovaných vstupů, ne počtem nálezů. A drž pozitivní
kontrolu (schválně vadný vstup, který nález vyvolat musí).

Platí to i mimo kompilátor: prázdný výstup gerpu v souboru, který neexistuje,
vypadá stejně jako čistý soubor.

## 7e. Ověř nejdřív MĚŘÍTKO, teprve pak jím měř

§7d říká „drž pozitivní kontrolu". 2026-09-05 jsem ji poprvé opravdu udělal — a
okamžitě našla chybu **v samotném ověřovacím nástroji**.

Skript, kterým jsem po každé editaci kontroloval webovou SPA, vytahoval
servírované HTML z obřího C řetězcového literálu regexem `"(...)"`. Jenže
uvozovka v `/* komentáři */` **mimo** literál je pro překladač nic (komentář
zmizí dřív, než se parsují řetězce), zatímco regex ji vzal jako začátek řetězce
a od té chvíle posunul páry. Nástroj tedy vracel **jiné HTML, než se doopravdy
servíruje** — a právě jím jsem třikrát prohlásil, že je vše v pořádku.

🔴 **To je horší třída chyby než ta původní.** Rozbitý kód je vidět. Rozbité
měřítko *maskuje* rozbitý kód a přitom vypadá jako důkaz opaku — každé „ověřeno"
udělané tím nástrojem je zpětně bezcenné, včetně těch, která tehdy prošla
právem.

**Postup, který to odhalil (a stojí těch pět minut vždycky):**
1. Napiš kontrolu.
2. **Nastraž do vstupu přesně tu chybu, kvůli které kontrola vzniká.**
3. Ověř, že ji kontrola nahlásí — a **který krok** ji nahlásil.
4. Vrať vstup do pořádku a teprve pak nástroji věř.

Krok 3 má vlastní hodnotu: u mě past nechytila „rychlá cesta", ale až build.
Tím jsem se dozvěděl, že rychlá cesta pokrývá jinou třídu chyb, než jsem
myslel — což je informace, kterou by úspěšný běh nikdy nedal.

**Zobecnění:** nástroj, který napodobuje chování něčeho jiného (preprocesoru,
parseru, protistrany), musí být testovaný proti tomu vzoru, ne jen „vypadat, že
funguje". A pokud existuje kontrola, která měří **skutečný výstup** místo
mezistavu, je vždycky nadřazená — u SPA je to velikost symbolu ve slinkovaném
`.elf` (`nm`) proti velikosti extrakce: jediná, která rozejití chytí definitivně,
protože se ptá obrazu, ne mého skriptu.


### 7e/2. Pět selhání testu, ani jedno chyba v kódu

Doplněk k §7e z téhož dne. Během jedné session spadlo pět mnou psaných testů
a **ani jeden nález nebyl skutečný**:

| co test hlásil | skutečná příčina |
|---|---|
| MDEV/ADEV = 0,19 místo 0,707 | LCG generátor měl mřížkovou korelaci — „bílý" šum bílý nebyl |
| `drawAlarm` má mrtvou větev na `.verdict` | kontrola matchovala **můj vlastní komentář**, který tam vysvětluje, proč větev chybí |
| průměrování segmentů nesnižuje rozptyl ℒ(f) | měřil jsem rozptyl přes biny, ale ten nese i deterministický sklon −20 dB/dek — měřil jsem **sklon**, ne rozptyl |
| `niceStep` dává mantisu 0,5 | mantisa počítaná přes `Math.round(log10)`; pro krok 5 vyjde `round(0,699)=1` → 0,5. Patří tam `floor` |
| `saveUi` neukládá | harness neměl nové proměnné, a protože je funkce v `try/catch`, chyběla i výjimka — selhalo to **tiše** |

Dvě opakující se příčiny stojí za zapamatování:

1. **Test měří jinou vlastnost, než si myslí.** Typicky když veličina obsahuje
   dvě složky (šum + trend, náhodu + determinismus) a já potlačím jen jednu.
2. **Harness nemá úplný scope.** U kódu obaleného `try/catch` se to neprojeví
   jako výjimka, ale jako tichý no-op — tedy přesně jako vada kódu.

**Pravidlo:** když test „odhalí" chybu v učebnicové matematice, nebo v kódu,
který se právě nezměnil, je pravděpodobnostně na vině test. Než sáhneš do kódu,
nech si vypsat **konkrétní vstup a mezivýsledek** — u všech pěti to odpověď dalo
na první pokus.

**Dovětek 2026-09-06 — kontrola se dá umlčet právě tou vadou, kterou hledá.**
Tři skripty hledaly konec kontrolovaného úseku jako „první řádek končící `;`".
Rozbitý řádek přitom typicky vypadá `… ||w;` — končí středníkem a chybí mu
závěrečná uvozovka. Sám si tedy **uřízl rozsah kontroly** a zbytek souboru
(1600 řádků) se neprověřil: nástroj hlásil nula nálezů a měl pravdu o úseku,
který si sám zmenšil. Horší polovina: extraktor při nenalezeném konci nechával
`end = start` a **tiše vracel nula literálů**, takže rozbitá deklarace prošla
jako v pořádku.

Dvě pravidla z toho:

1. **Hranice kontrolovaného úseku nesmí být odvozená ze vzorku, který může být
   poškozený.** Když hledám konec, musí to být vzor, který vada nemůže omylem
   vyrobit (tady `";`, ne `;`).
2. **„Nenašel jsem, kde skončit" je CHYBA, ne prázdný výsledek.** Každý tichý
   fallback na „nic jsem nenašel" je v ověřovacím nástroji lež.

Obojí odhalila **pozitivní kontrola** — podruhé v řadě (poprvé neodstraněné
komentáře v extraktoru). To už není náhoda: nastražit chybu je u vlastního
nástroje jediný způsob, jak zjistit, co doopravdy kontroluje.


⚠️ Neplyne z toho „testům nevěř". Plyne z toho, že **test je taky kód, který
nikdo neotestoval** — a platí pro něj totéž, co §7d říká o nástrojích:
ověř, že měří, co má, dřív než mu uvěříš verdikt.


## 7f. Limit odvozený z „co posílá náš klient" neplatí pro cizího klienta

Webový server v přístroji měl `HTTPD_RXBUF_MAX = 700` na celý blok HTTP
hlaviček. To číslo dává smysl, když si člověk představí požadavek, jaký by
napsal sám (`GET /api/state HTTP/1.1` + `Host:` + pár řádků). Jenže klientem
je **cizí prohlížeč**, a ten k tomu přidá `User-Agent`, `Accept`, `sec-ch-ua*`,
`Sec-Fetch-*`, `Accept-Language`, `Accept-Encoding` — dohromady 800–1000 B.
Server by poctivě vrátil 431 a uživatel by viděl prázdné okno.

Zákeřné je, že **to projde s `curl` i s Firefoxem** a spadne to na Chrome. Kdo
testuje tím, čím vyvíjí, chybu nepotká.

**Pravidlo:** u každého limitu si napiš, **kdo je na druhé straně** a jestli
jeho velikost určuješ ty, nebo cizí software. Když cizí, dimenzuj podle
nejukecanějšího známého klienta a přidej rezervu — je to `.bss`, ne peníze.

Stejná otázka platí pro časy (timeout smí být krátký jen tam, kde tempo určuji
já), pro počty spojení a pro délku vstupní řádky.

## 7g. Komentář „ošetřeno" není důkaz — u vstupu ze sítě to přečti řádek po řádku

SCPI server nad TCP měl u ošetření příliš dlouhé řádky komentář: *„zahoď a čekej
na další LF/CR, ať se neprocesuje uříznutý příkaz."* Kód pod ním ale jen
vynuloval `rxlen`, takže se okamžitě začalo plnit znovu a **ocas dlouhé řádky se
provedl jako příkaz** — `<100 znaků smetí>SENS:FREQ:GATE 100
` skutečně
přestavil bránu. Komentář popisoval úmysl, ne chování.

Pro tuhle třídu (parsování vstupu z venku) nestačí grep ani analyzátor:
`-fanalyzer` tam nevidí chybu, protože přetečení pole se nekoná — mění se jen
**význam** dat. A protože komentář tvrdil opak, přeskočila to i lidská kontrola.

**Pravidlo:** kód, který přijímá data zvenčí (síť, sériová linka, soubor),
se čte **řádek po řádku a proti komentáři se ověřuje**, ne skrz něj. Komentář
u takového místa ber jako tvrzení k ověření, ne jako popis.

## 6i. Než hledáš složitou příčinu, ověř, že součástka vůbec dostává hodiny

2026-09-06 nešel displej: po power-resetu černý, po soft-resetu blikající.
Než se našla příčina, prošlo se: časování FMC, `REFRESH_COUNT`, pořadí
inicializace vůči PLL2, MPU atributy a cacheovatelnost, údržba D-cache
v `membench`, konflikty pinů, geometrie SDRAM, LTDC porche, DMA2D arbitráž.
Všechno bylo v pořádku.

Příčina: **`PG8` = `FMC_SDCLK` byl v ANALOGOVÉM režimu.** SDRAM nedostávala
hodiny, takže nepřijala jediný příkaz, neobnovovala se a četla samé nuly.
Framebuffer plný nul = černá v RGB565 = černý displej.

Nejlevnější a nejzákladnější kontrola přišla úplně poslední.

**Pravidlo:** u paměti nebo periferie, která „nereaguje“, ověř v tomhle pořadí:
napájení → **hodiny** → výběrový/povolovací signál → adresa/data → časování →
software. Konfigurační registry řadiče můžou být dokonale správné a přitom
k čipu nevede takt; řadič o tom neví a nic nehlásí.

⚠️ Konkrétní podpis „chybí hodiny“ v `membench`: vzor `0x00` má **nula chyb**
a všechny ostatní vzory selžou. To není vada buněk — to je „všechno čte nuly“.
Vada buněk dá chyby rozeseté napříč vzory.

## 6j. Nediagnostikuj z dat, o kterých tvá vlastní dokumentace říká, že jsou nedůvěryhodná

CLAUDE.md u `membench` výslovně stojí: *„verdikt o překryvu je NEDŮVĚRYHODNÝ,
dokud retence není 0“*. Retence nula nebyla — a já přesto z hlášky
`PREKRYV/CIZI ZAPIS do kontrolni bunky` odvodil, které adresní linky jsou
vadné (`PG2`, `PG5`), zapsal to do STATUS jako nález a poslal uživatele
prozvánět piny, které byly v pořádku.

Ta věta v dokumentaci tam byla právě proto, že se tou pastí už jednou prošlo.

**Pravidlo:** než z diagnostického výstupu něco odvodíš, přečti si, za jakých
podmínek ten výstup platí — a ověř, že platí. Nástroj, který hlásí i to, že
jeho vlastní verdikt může být falešný, tím dává **podmínku, ne poznámku pod
čarou**.

## 6k. Z toho, že kód běží, neplyne, že jeho zápisy dorazily

Během ladění jsem tvrdil, že framebuffery jsou v pořádku, protože „displej
kreslí“ — čítač `flip` rostl a UiTask normálně běžel. Jenže displej byl
černý: UiTask kreslil do paměti, která nic neukládala. Čítač dokazoval, že
běží KÓD, ne že dorazila DATA.

**Pravidlo:** živost úlohy (čítače, heartbeat, uptime) je důkaz o vykonávání,
ne o účinku. Když jde o obsah paměti, ověř obsah — ideálně cestou, která
obchází cache i tu úlohu (sonda, jiný master, zpětné čtení jinou periferií).

## 7h. Neprůkazné měření není důkaz opaku

Sweep `d2ddt` (mrtvý čas DMA2D) dal pro hodnoty 0, 8 i 32 shodně **1,000
podtečení na snímek**. Málem jsem to zapsal jako „soupeření DMA2D o sběrnici
vyloučeno“. Ve skutečnosti se čítač inkrementuje **nejvýš jednou za
`prim_stm32_present`**, takže 1,000 znamenalo „na stropu“ — kdyby škrcení
snížilo podtečení z tisíce na jedno, naměřím pořád 1,000.

Když se pak měřilo v režimu, kde čítač nesaturuje, ukázalo se, že DMA2D je
příčina **rozhodující**: zlom je u mrtvého času ~208 a nad ním je podtečení
nulové. Kdybych ten první výsledek zapsal jako „vyloučeno“, hledám příčinu
dodnes jinde.

**Pravidlo:** rozlišuj „naměřil jsem, že vliv není“ od „měřidlo nemá
rozlišení“. U každého čítače si napiš jeho **strop a jednotku** (co přesně
jeden tik znamená) dřív, než z něj vyvodíš závěr. Hodnota, která se rovná
stropu, je signál o měřidle, ne o systému.

## 6l. Když najdeš vadu, prohledej celou její TŘÍDU, ne jen ten jeden výskyt

2026-09-06 jsem našel, že `PG8` (`FMC_SDCLK`) ztratil konfiguraci, opravil ho
a šel dál. Původce jsem nenašel a zapsal to jako otevřené. **O kolo později se
ukázalo, že úplně stejně přišel o konfiguraci `PG11` (`ETH_TX_EN`)** — jiný pin
téhož portu, tatáž příčina, jiný projev (místo černého displeje deska nedostala
IP). Kdybych po nálezu na PG8 prošel zbytek `GPIOG`, měl jsem obojí naráz
a ušetřil celé kolo ladění.

**Pravidlo:** po každém nálezu si polož otázku *„kde jinde přesně tohle mohlo
nastat?“* a **projdi ty výskyty hned**, dokud máš čerstvý přístup k HW i hlavu
v problému. Konkrétně:
- stejný registr / stejný port / stejná periferie,
- stejný vzor v kódu (`grep` na tutéž konstrukci),
- stejná sdílená struktura mezi jádry/tasky.

⚠️ Zvlášť to platí, když **neznáš původce**. Neznámá příčina znamená, že
nemůžeš vyloučit další oběti — a „opravím ten, o kterém vím“ je pak jen
odklad.

## 6m. „Periferie hlásí úspěch“ pokrývá jen její vlastní úsek řetězu

Při hledání vady Ethernetu jsem přečetl TX deskriptor DMA: `OWN=0`, `FD|LD`
nastaveno, **žádný chybový bit**, délka 350 B (přesně DHCP DISCOVER). Uzavřel
jsem z toho, že vysílání funguje. Nefungovalo: `ETH_TX_EN` (PG11) ztratil
alternativní funkci, takže PHY nikdy nedostal povolení vysílat a na drát nešel
ani jeden rámec. **DMA přitom hlásila úspěch naprosto po právu** — svůj úsek
(přečíst buffer, předat MAC) zvládla.

**Pravidlo:** úspěšné hlášení periferie je důkaz o **jejím** úseku, ne o celé
cestě. U řetězu `CPU → DMA → MAC → PIN → PHY → drát` musíš mít důkaz z místa
za tím podezřelým článkem — u sítě je to protistrana (ARP záznam, odpověď),
ne vlastní deskriptor.

Souvisí s §6k („kód běží“ ≠ „zápisy dorazily“); tohle je jeho hardwarová
varianta: „DMA dokončila“ ≠ „signál opustil čip“.

## 6n. Sdílený registr mezi dvěma jádry je nezámkový read-modify-write

Obě zmíněné vady mají společnou příčinu: `GPIOG` konfigurují **obě jádra**
(CM7 kvůli FMC a QUADSPI, CM4 kvůli ETH a LED), a `HAL_GPIO_Init` dělá nad
`MODER`/`AFR` **čtení, úpravu a zápis bez zámku**. Ztracený zápis pak tiše
vrátí cizí pin do původního stavu.

Podpis je charakteristický a stojí za zapamatování: **ztratí se jen JEDNA
položka** (u PG8 `MODER`, u PG11 `AFR`), zatímco druhý pin z téhož volání
`HAL_GPIO_Init` přežije. Vada buňky ani rozbitá pájka takhle nevypadají.

**Pravidlo:** u víceprocesorového čipu si u každé sdílené periferie napiš,
**kdo všechno do ní zapisuje**. Když je jich víc než jeden a zápis není
atomický, je to závod bez ohledu na to, jak nepravděpodobný se zdá — GPIO
konfigurační registry, hodinové enable registry (`RCC_*ENR`) a NVIC jsou
typické. Řešením je serializace (na STM32H7 `HSEM`), ne naděje.

## 6o. Test bez kontrolní větve neměří nic — hlavně když „vada" umí přijít sama

2026-08-30 jsem chtěl vědět, jestli čtení ladicí sondou zabíjí I2C4. Napsal jsem
test: ověř zdravou sběrnici → N čtení sondou → sleduj chyby. Vyšlo „po 1. čtení
6 chyb, po 3. čtení sběrnice mrtvá". Zapsal jsem to do CLAUDE.md jako **změřený
zákon** a na jeho základě si zakázal sondu úplně.

**Ten test ale nikdy nezměřil kontrolní situaci: zdravou sběrnici ponechanou
stejnou dobu O SAMOTĚ.** A přitom jsem **týž den** zapsal, že sběrnice umírá sama
po 7 s, 136 s a ~2016 s. Celé pozorování trvalo ~15 s. Sběrnice hynoucí sama
během minut vyrobí naměřená čísla i bez jediného haltu — **souběh je úplný**.
Uživatel na to přišel prostou námitkou „dřív to šlo i se sondou“ a historie mu
dala za pravdu.

Cena: měsíc práce s uměle zakázaným nástrojem a **falešně uzavřená otázka** — protože
„už víme, že to dělá sonda“ zastavilo hledání skutečné příčiny.

**Pravidlo:** než z pokusu uděláš závěr „**A** způsobuje **B**“, zeptej se
**„co dělá B, když A neudělám?“** a změř to. Zvlášť nesmlouvavě, když:
- **B umí nastat samo** (intermitentní vada, degradace, závod),
- pozorovací okno je **kratší** než typický interval mezi samovolnými výskyty,
- **A a B se dějí ve stejném období** z jiného důvodu (u mě: auto-dim se spouští
  po době **bez doteku** — tedy právě když ladím; korelace se sondou vznikne
  sama od sebe).

⚠️ A když takový nezajištěný závěr **zapíšeš jako pravidlo do dokumentace**,
zafixuješ chybu pro všechny další relace. Do dokumentace patří i **jak** to bylo
změřeno, aby to šlo přezkoumat — „naměřeno“ bez postupu je jen tvrzení.
Souvisí s §6e (jedno čisté měření neruší výpočet) a §6j (nezávěruj z dat, o kterých
sám píšeš, že jsou nedůvěryhodná).

## 6p. Vyvrácenou hypotézu zapiš tam, kde se hledá — ne do commit message

2026-09-06 jsem u mrtvé I2C4 navrhl jako hlavního podezřelého **zápis jasu do
ATTINY při auto-dimu**. Uživatel odpověděl „auto-dim už jsme řešili“ — a měl
pravdu: ta hypotéza byla **vyvrácena zátěžovým testem už 2026-08-31**
(40 ověřených zápisů → 0 chyb).

**Proč se vrátila:** důkaz proti ní ležel v **komentáři ve zdrojáku** a
v **commit message**. V CLAUDE.md — tedy tam, kam se při hledání příčiny dívám —
zůstalo **původní tvrzení, že zápis jasu tu poruchu způsobuje**. Přečetl jsem
zastaralou verzi a poslušně zopakoval uzavřenou větev. **Horší než ztracený čas:
navrhl jsem uživateli jako „nový hlavní podezřelý“ něco, co sám vyvrátil.**

**Pravidla:**
1. **Vyvrácení patří na místo tvrzení**, ne vedle něj. Když měření něco zabije,
   **přepiš ten původní odstavec** — nestačí přidat poznámku jinam. Dvě verze
   pravdy v jednom dokumentu znamenají, že se ta špatná dřív nebo později použije.
2. U vady, která se řeší opakovaně, veď **jeden autoritativní seznam
   „co už bylo vyloučeno a čím se to ví“** a měj pravidlo, že **nová hypotéza
   musí nejdřív vysvětlit, proč neplatí nic z něj**.
3. **Commit message a komentář ve zdrojáku nejsou dokumentace.** Jsou to archivy.
   Co má ovlivnit příští rozhodnutí, musí být v souboru, který se čte na začátku.

⚠️ Signál, že tohle děláš: uživatel ti řekne **„to už jsme řešili“**. To není
netrpělivost — je to hlášení, že dokumentace neudržela závěr. Reakce není omluva,
ale **oprava toho dokumentu**, aby se stopa nemohla vrátit potřetí.

## 8. Odděl, co je ověřené, od toho, co je hypotéza — a podle toho se chovej

Problikávání trendu jsem opravil mechanismem, který jsem uměl odůvodnit, ale
**nepotvrdil okem**. Správná reakce nebyla „tak opravím i těch dalších 120 míst
se stejným vzorem", ale **počkat na potvrzení** a teprve pak sáhnout na běžící kód.

**Jak to dělat:** v hlášení i v TODO výslovně říkej „ověřeno / neověřeno". Zásah do
funkčního kódu na základě nepotvrzené hypotézy je horší než odložený zásah.

## 9. Zadání a realita: co nemá zdroj dat, se nekreslí

Zadání UI chce průběh hradla, prahy obou kanálů a σ u odečtu. Tři čtvrtiny toho
dnes nemá odkud brát data (chybí vstupní modul, brána se do FPGA nedostane).
Nakreslit to teď = šedé placeholdery, tedy **horší obrazovka než dnešní**.

**Jak to dělat:** u každého prvku zadání dohledej **zdroj dat**. Když chybí, patří
to do fáze, ne do sprintu. A když se něco kreslí z parametru, který na měření nemá
vliv, je to lež — ne kosmetika.

## 6c. Dva renderery jednoho prvku se VŽDY rozejdou

Dvakrát za jeden den, stejná třída:

- **#113** — velké číslo kreslí plný render i partial redraw; „předchozí obdélník"
  pro úklid si zapisoval jen ten druhý, takže po plném renderu zůstávali duchové.
- **#117** — statistickou kartu kreslí plný redraw (s hlavičkou) a 20Hz update
  hodnoty (bez ní). `ui_card_inner_rect()` přičítá výšku hlavičky **jen když je
  `header_label` nastavený**, takže rychlá cesta počítala vnitřek o 26 px výš,
  clearem smazala popisek a hodnotu nakreslila do hlavičkového řádku.

V obou případech u té odchýlené cesty stál komentář tvrdící, že geometrie je
shodná. Komentář sdílení nezajišťuje — **kód musí sdílet ten výpočet**.

**Pravidlo:** jakmile prvek kreslí víc než jedna funkce, vyčleň *geometrii i
popisky* do jednoho helperu, který obě volají. A pozor na API, které mění
výsledek podle toho, co je vyplněné ve vstupní struktuře (`ui_card_inner_rect`) —
takové rozhraní si o tuhle chybu říká.

## 9b. Jedna veličina = jeden typografický jazyk

Hlavní obrazovka měla pro kmitočet propracovaná pravidla (tisíce tečkou, poslední
**důvěryhodná** číslice modře podtržená, číslice **pod rozlišením hradla** menším
fontem a šedě). Okna měřicích funkcí kreslila tentýž údaj plochým fontem bez
jakéhokoli rozlišení důvěryhodnosti — takže **jedno číslo mluvilo na dvou místech
dvěma jazyky** a v oknech působilo všech 5 desetin stejně platně.

To není kosmetika: ztlumené číslice jsou **nesená informace** („tohle už neměříme,
to je šum"). Když ji jedno okno má a druhé ne, uživatel se z rozdílu naučí nesprávnou
věc — nebo si přestane všímat obojího.

**Pravidlo:** jakmile má veličina někde typografii nesoucí význam, vyčleň ji do
**sdíleného helperu** a použij ji všude, kde se ta veličina objeví. Ne kopií —
sdílením. Měřítko se smí lišit (`mono_75` vs `mono_30`), význam ne.

⚠️ Odvozený parametr (kolik číslic je nejistých) se musí počítat **stejným
kritériem**, ne odhadem. A pozor na transformace: u „odchylka ×N" se rozlišení
násobí **stejným činitelem** jako hodnota — bez toho by při ×1M vypadalo
důvěryhodně číslo, které je celé pod rozlišením hradla.

## 10. Ptej se tam, kde odpověď mění práci

Osvědčilo se: nabídnout **3 varianty s důsledky a doporučením**, ne otevřenou otázku.
Rozhodnutí typu „kdo je I²C master", „dva kanály v jednom záznamu, nebo dva ringy",
„encoder vs. dotyk" mění strukturu kódu — odhad by se zapsal do dokumentu, který má
být zdrojem pravdy.

Naopak se **neptej** na věci s rozumným výchozím řešením; ty rozhodni a napiš proč.

## 11. Nález okamžitě do TODO

Pravidlo uživatele (2026-08-31): **každý nález jde hned do `STATUS.md`**, i když se
opravuje vzápětí; opravené se nemažou, jen označí ✅ i s příčinou.

**Proč:** nález, který zůstane v konverzaci nebo v commit message, se ztratí — a
tenhle projekt už dvakrát zopakoval zdokumentovanou chybu.

## 12. Drobnost, která opakovaně stála čas

Víceřádkové shell heredocy si v tomhle prostředí **rozbíjejí escape sekvence**
(`\r\n` se změní na skutečný konec řádku a rozsekne řetězcový literál). Stalo se
nejméně šestkrát.

**Jak to dělat:** kód s escape sekvencemi piš přes `Write`/`Edit`, nebo skript
ulož jako soubor a teprve pak spusť. Po každé hromadné úpravě zkontroluj, že se
nerozpadly literály.

---

## Rychlý kontrolní seznam před „hotovo"
- [ ] Ověřil jsem to **měřením**, nebo jen usoudil? A co to měření samo mění?
- [ ] Umí moje kontrola **selhat**? Zkusil jsem ji donutit?
- [ ] Dostává ta součástka vůbec **hodiny a napájení**? (§6i — nejlevnější kontrola patří první, ne poslední.)
- [ ] Prohledal jsem celou **třídu** nálezu, nebo jen ten jeden pin/soubor/výskyt? (§6l)
- [ ] Změřil jsem i **kontrolní** variantu — co se stane, když zásah NEudělám? (§6o)
- [ ] Není tahle hypotéza už **vyvrácená**? Prošel jsem seznam vyloučených příčin? (§6p)
- [ ] Mám důkaz **za** podezřelým článkem řetězu, ne jen hlášení periferie o sobě? (§6m)
- [ ] Nepíše do toho registru **druhé jádro**? (§6n)
- [ ] Znám **strop a jednotku** čítače, ze kterého vyvozuji závěr? (§7h)
- [ ] **Ověřovací nástroj, který jsem napsal, jsem otestoval nastraženou chybou** (§7e) — a vím, *který krok* ji chytil.
- [ ] Jak to vypadá, **když ta kontrola nic nedělá**?
- [ ] Neodvozuji konstantu, kterou mi může někdo **deklarovat**?
- [ ] Není ten fakt **na dvou místech**?
- [ ] Přeskakuje můj guard až po **tolika vykresleních, kolik je bufferů**?
- [ ] Objevila se porucha po změně X? Ptal jsem se, **co X přestalo dělat**, ne jen co rozbilo?
- [ ] Řekl uživatel „**dřív to šlo**"? → udělal jsem **bisect**, nebo jen hádám?
- [ ] Je můj „důkaz" jen **korelace**? Mám kontrolní experiment, který s ní hýbe?
- [ ] Zavírám rozpor mezi **výpočtem a jedním měřením** ve prospěch měření?
- [ ] Nestojí moje diagnostika nad **médiem, které neprošlo retencí**?
- [ ] **Displej zlobí → `membench` DŘÍV než kreslicí kód.**
- [ ] Je nový zdroják v `subdir.mk` **i** v `objects.list`?
- [ ] Rozlišil jsem v hlášení **ověřené od hypotézy**?
- [ ] Má každý nový prvek UI **zdroj dat**?
- [ ] Je nález v **TODO**?
