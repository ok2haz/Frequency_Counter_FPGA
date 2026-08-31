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
