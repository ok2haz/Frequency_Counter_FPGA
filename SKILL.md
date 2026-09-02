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

**Důkaz:** optimalizace trendu (guard „pixelově identické → přeskoč") způsobila
problikávání. Guard byl **jeden stav, ale framebuffery jsou tři** — obsah zůstal
jen v tom, do kterého se zrovna kreslilo.

**Jak to dělat:** každý skip-guard musí přeskakovat **až po tolika vykresleních,
kolik je bufferů**. Nespoléhej na copy-forward: kopíruje dirty z posledních dvou
snímků, a když se kvůli přeskakování neflipuje, historie z dosahu vypadne.
Obecněji: **cache/skip vždy počítej vůči počtu konzumentů**, ne vůči jednomu.

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
- [ ] Jak to vypadá, **když ta kontrola nic nedělá**?
- [ ] Neodvozuji konstantu, kterou mi může někdo **deklarovat**?
- [ ] Není ten fakt **na dvou místech**?
- [ ] Přeskakuje můj guard až po **tolika vykresleních, kolik je bufferů**?
- [ ] Je nový zdroják v `subdir.mk` **i** v `objects.list`?
- [ ] Rozlišil jsem v hlášení **ověřené od hypotézy**?
- [ ] Má každý nový prvek UI **zdroj dat**?
- [ ] Je nález v **TODO**?
