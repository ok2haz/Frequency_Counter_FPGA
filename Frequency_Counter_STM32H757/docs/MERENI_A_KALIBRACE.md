# Měření a kalibrace — deník

Sem patří **výsledky měření na desce a stav kalibrací**: co se měřilo, jak, s jakým firmwarem a co z toho plyne.
Teorie (matematika TDC, regrese, chybový rozpočet) je v `docs/TDC_MATEMATIKA.md`, přesnost ADS1115 v
`docs/audit/2026-10-11_ads1115-presnost.md`, lekce z chyb v `docs/LESSONS.md`.

## Pravidla zápisu

1. Každý záznam má **datum, verzi FPGA FW (+ čas sestavení bitstreamu) a verzi CM7**, zapojení vstupů a referenci.
2. Uveď **nástroj a postup** tak, aby šel záznam zopakovat (UART příkaz, endpoint, délka měření, počet oken).
3. Čísla jen změřená. Co je odhad nebo výklad, označ **HYPOTÉZA** a napiš, čím ji ověřit.
4. Starší záznamy se **nepřepisují**. Když se ukáže, že byly špatně, připiš pod ně opravu s datem.
5. Stav kalibrací (kap. 3) drž aktuální. Je to jediné místo, kde je vidět, co je zkalibrované a co ne.

---

## 1. Měřicí sestava a nástroje

### 1.1 Sestava (od 2026-10-11)

| Položka | Stav |
|---|---|
| Vstup A a B | 10 MHz z **externího OCXO GPSDO** (oba kanály stejný signál, přes T-kus). **Od 2026-10-18 (upřesnění uživatele):** každý kanál má **vlastní externí budič 7404** → rozdíl A−B zahrnuje i nesoulad budičů a prahů (TDC kanály se tím neoddělí od budičů) |
| Reference FPGA | **OCXO na desce** (Si5356 CLKIN 10 MHz → CLK0/1 do FPGA). Externí referenční vstup bez hran (`ref`) |
| Ladění OCXO | **nikdo neřídí** — AD5693R (0x4C) není osazen, uzel Vc přes R78 100k visí |
| Hradlo | 0,25 s (výchozí), kanál A primární |

### 1.2 Nástroje

| Nástroj | Co dává | Pozor |
|---|---|---|
| UART `status` | FW + čas sestavení FPGA, hlídač hodin, samokontrola, `CITANI HRAN`, spike-reject | konzole po resetu CM7 někdy mlčí (pak web) |
| UART `tdc`, `tdc hist`, `tdc cal` | kalibrace TDC: počet kódů, největší bin, `max tap`, histogram | `tdc cal` na chvíli zastaví měření |
| UART `regr` | regrese: oken použito / zamítnuto, f_regr − f_2pt | |
| UART `sdramlog dump N` | **N po sobě jdoucích oken** (kmitočet A) — jediný souvislý zdroj | výpis nad ~200 řádků konzole zahazuje |
| web `GET /api/fpga` (`tools/fpga_diag.py`) | rámec FPGA (okna A/B, edges, regrese, diag) | ~2 Hz = **každé druhé okno** (ne souvislá řada) |
| web `GET /api/log` | datalog 10 s průměrů, dny zpět | min/max obálka po 2 h |

**Rozklad chyby délky okna** (použito 2026-10-11): očekávaná délka = `edges · P`, kde P je medián `gate_ps/edges`.
Odchylka okna proti ní, u obou kanálů zvlášť. **Společná** odchylka A i B = signál nebo reference.
**Rozdílová** A−B = chyba TDC jednoho kanálu. Přepočet: 1 ns na 0,25 s = 4·10⁻⁹ = 0,04 Hz při 10 MHz.

---

## 2. Deník měření

### 2026-10-11 — FPGA FW 0x0422 (125 MHz, sestaveno 12:24 UTC), CM7 v0.14.0

Deska po power-cyklu, sestava 1.1.

- **Souvislá řada** (`sdramlog dump`, 200 oken): **ADEV(0,25 s) = 1,4·10⁻⁸**, ADEV(1 s) 3,8·10⁻⁹, ADEV(4 s) 9,1·10⁻¹⁰.
  Jen **105/200 oken** v ±0,5 ns od mediánu. Chyby délky okna jsou **kvantované**: shluk kolem **+4 ns (= T/2 při 125 MHz)**,
  menší kolem **+8 až 9,5 ns a −7,5 ns (= T)**. Opakují se s rozestupy 4-4-2 okna (záznějová fáze signálu proti hodinám)
  a jsou **jednostranné** — nejde o šum ani o modulaci, ale o chybu časové značky podle fáze hrany.
- **A−B** (web, 241 oken): jádro σ 0,13 ns, ale **19 % oken** s chybou jen v jednom kanálu, rostoucí pilovitě s fází (−1,6 → −6,5 ns).
  Tři okna s A přesně −100 ns (jedna hrana; vyřazeno kontrolou počtu hran).
- **Regrese**: zamítnuto **1207 / 1207** oken. `ym` má jen hodnoty 17,176 / 31,250 / 45,324 ms → chyba přesně **±2^53** v součtu Σ(t−t0).
  Aritmetika RTL je přitom správná (`sim/tb_regr_big.sv`, 312 500 hran na segment: PASS).
- **Kalibrace TDC** (`tdc hist`): A 129 kódů, B 130 kódů, průměrný bin 62 ps, největší **257 ps (A, k=161) / 182 ps (B, k=39)**,
  za koncem řetězu 0, `max tap` 200 → zdravá. (UART `tdc` tehdy vypisoval 321 ps — počítal s periodou 10 ns; opraveno, bere kmitočet z rámce.)
- **Závěr:** na 125 MHz je chyba v přiřazení hrany k taktu (T/2, T). Kalibrace ani logika regrese za ní nejsou. → bisect na 100 MHz.

### 2026-10-11 — FPGA FW 0x0423 (100 MHz, sestaveno 17:03 UTC), CM7 se Si5356 CLK0/1 = 100 MHz

Bisect: RTL beze změny, **jen hodiny 125 → 100 MHz** (L-0154). CM7 nahrán bez power-cyklu desky (OCXO teplý).

- **Regrese použita ve 239/240 a 294/300 oknech.** Segmenty přesně: x̄ = 312 500 / 2 187 502, ȳ = 31,250 / 218,750 ms.
- **A−B**: medián **0,010 ns**, jádro σ 0,31 ns, odlehlých **3 %** (na 125 MHz 19 %).
- **Okno A** (regrese) i **B** (dvoubodově), lineární drift odečten: σ reziduí **2,0–2,3·10⁻⁹**, ADEV(párů po 0,5 s) 1,7–2,1·10⁻⁹,
  a to **stejně v A i B** → zbytek je **společný** (signál nebo reference), ne TDC.
- **Závěr:** vada T/2 a T zmizela, regrese funguje → příčinou byl přechod na 125 MHz.
- ⚠️ **Otevřené:** společná nestabilita ~2·10⁻⁹ je ~30× víc než dřív na 100 MHz se stejnými zdroji (5–7·10⁻¹¹, `TDC_MATEMATIKA.md` kap. 10, STATUS #274).
  **HYPOTÉZA** (seřazeno podle pravděpodobnosti, nic z toho není ověřené):
  1. plovoucí uzel Vc OCXO (AD5693R chybí) chytá rušení — v ten den se měnil takt I2C1 (50 → 400 kHz) a děliče ADS (R54, R58);
  2. OCXO rozhozený resetem CM7 těsně před měřením;
  3. GPSDO sám.
  **Ověření:** souvislá řada (`sdramlog dump`, ne web) po ustálení; pak pokus s I2C1 zpět na 50 kHz při jinak stejném stavu;
  spektrum společné složky (2 Hz = takt čtení senzorů).

> 🔴 **OPRAVA 2026-10-11 (po power-cyklu):** bod „Okno A (regrese) i B … σ reziduí 2,0–2,3·10⁻⁹ … společný zbytek" a bod „Otevřené" výše
> **neplatí**. Řada, ze které vznikly, byla vypočtená z `edges / gate_ps` v `/api/fpga`; tam se u části snímků `gate_ps` nedostane
> z přijaté regrese (web ukazoval `regr_used` 182/300, kdežto firmware sám hlásí 12117/12133 = 99,9 %), takže „A (regrese)" byla ve skutečnosti
> dvoubodová řada. Nejde o „30× horší oscilátory". Správné hodnoty jsou v dalším záznamu. Pro stabilitu **použij `sdramlog dump`
> nebo `/api/state`** (jdou přes přijatá měření), ne `/api/fpga`.

### 2026-10-11 večer — po POWER-CYKLU, FPGA FW 0x0423 (100 MHz), CM7 v0.14.0, uptime 7–13 min

Studený start v pořádku: `FW:0x0423`, `FPGA HODINY 100M: OK`, kalibrace TDC A:1 B:1, `REGR: pouzita`, samokontrola 0 zamítnutí, UART zase odpovídá.

- **Rozjezd OCXO po zapnutí:** prvních ~50 s (uptime 24–75 s): drift **128 ppb/h**, σ 2·10⁻⁸, autokorelace 0,97 → vyhodnocuje se drift, ne šum. Měřit až po ustálení (≥ 7 min).
- **Výsledek (regrese, kanál A), `sdramlog dump 200`, 2× po 199 souvislých oknech, uptime ≥ 7 min:**
  σ reziduí po odečtení driftu **3,7·10⁻¹⁰ / 2,5·10⁻¹⁰** (0,09 / 0,06 ns za 0,25 s), **všech 198/199 oken v ±0,5 ns**, bez odlehlých;
  **ADEV(0,25 s) 1,1·10⁻¹⁰, ADEV(0,5 s) 8·10⁻¹¹, ADEV(1 s) 8–9·10⁻¹¹**, ADEV(4 s) 1,5–2,1·10⁻¹⁰ (lag-1 autokorelace 0,8–0,9 = pomalé tápání oscilátorů, ne šum měření).
  Drift −5 až −40 ppb/h. `/api/state` (100 s, každé 2. okno): bílá složka (rozdíl sousedních/√2) 2,0·10⁻¹⁰.
  Historicky (100 MHz, 2026-10-09) ADEV(0,25 s) 5,4–6,5·10⁻¹¹ → dnes asi **2× horší**, stejný řád.
- **Dvoubodový odhad** (kanál B, který regresi nemá, a `f_2pt` ve `status`): σ **2,2–3,2·10⁻⁹** (0,56–0,8 ns za okno, `/api/state` kanál B).
  Autoritativní číslo z firmwaru za **12 133 oken**: `REGR: f_regr − f_2pt` střed −0,03 ppb, **σ = 1,77 ppb**. Dokumentace čekala ~0,5 ppb
  (`TDC_MATEMATIKA.md`, ADEV dvoubodově 7,6·10⁻¹⁰) → dnes **~2,5–3× horší**.
  Z A−B (jádro σ 0,31 ns → ~0,22 ns na kanál) plyne, že větší část dvoubodového šumu (~0,4–0,5 ns) je **společná** oběma kanálům,
  tj. nepochází z TDC. **HYPOTÉZA:** šum hrany vstupního signálu (tvarovač, 74HC04) nebo reference; regrese ho zprůměruje (~312 000 hran).
  **Ověření:** stejné měření s hradlem 1 s (σ okna by měl klesat s 1/τ, kdyby šlo o šum hran) a s jiným zdrojem na vstupu.
- **Regrese za provozu:** 12 117 použito, 16 zamítnuto (0,13 %).
- **`CITANI HRAN`:** 17 oken s hranou navíc za 762 s (0,14 %) — vyřazeno, mimo statistiku; metastabilita (`status` ji označuje).
- **Závěr:** výsledek zobrazený i ukládaný (regrese) je na úrovni oscilátorů (~1·10⁻¹⁰ na 0,25 s). Zbytkový rozptyl **dvoubodového** odhadu
  (~2·10⁻⁹) je 2,5–3× nad dokumentací a je hlavně společný oběma kanálům — o něj jde, jen když se regrese zamítne (0,13 % oken) nebo pro kanál B.

### 2026-10-11 večer — proč je výsledek horší než 9. 10.? (kontrolované pokusy, jedna deska, jeden signál, tentýž večer)

Veličina: `REGR: f_regr − f_2pt` σ [ppb] z UART `regr` po `regr reset` a ~90–100 s (≈1500 oken). Regrese šum prakticky nepřidává, takže σ ≈ šum
dvoubodového odhadu. 9. 10. (FW 0x041A, 40 654 oken): **0,46–0,49 ppb**. Odpovídá šumu jedné časové značky ≈ 105 ps (σ_f = √2·σ_m/G).

| pokus | σ [ppb] | oken použito / zamítnuto | závěr |
|---|---|---|---|
| FW 0x0423, generátor Si5356 CLK2–7 **ZAP** (50/150/200 MHz) | 1,15 | 1499 / 0 | základ |
| FW 0x0423, generátor **VYP** (OEB = 0E) | 1,23 | 1691 / 0 | **není příčina** (rozdíl v rámci chyby) |
| FW **0x041F** (starší RTL, 100 MHz) nahraný do SRAM | 2,06 | 1618 / 40 | **není příčina**, naopak horší |
| FW 0x0423 znovu (SRAM) | 1,09 | 1654 / 4 | opakovatelnost ±0,1 |
| FW 0x0423, celý večer (12 133 oken) | 1,77 | 12 117 / 16 | delší doba = víc výkyvů |

- Dnes ≈ **1,1–2,0 ppb ⇒ šum časové značky ≈ 0,2–0,3 ns** (proti 105 ps). Z A−B plyne, že nezávislá část na kanál je jen ~0,15 ns; zbytek je společný.
- **Vyloučeno měřením:** výstupy generátoru Si5356 (CLK2–7), změny RTL mezi 0x041A/0x041F a 0x0423 (starší bitstream je dnes stejně špatný), kalibrace TDC (zdravá),
  hodiny 100 vs 125 MHz (to byla jiná vada).
- **Nevyloučeno (HYPOTÉZY, neověřeno):**
  1. **Vstup:** dnes je 10 MHz z GPSDO na **A i B** (T-kus, dvojí zátěž zdroje). Nevím, jestli 9. 10. byl signál jen na A, ani čím byl buzený.
     Ověření: odpojit B a opakovat `regr reset` + 100 s. ✅ **POTVRZENO** (viz záznam noc, s B odpojeným 0,45 ppb, jako 9. 10.).
  2. **Šum I2C1:** od 2026-10-11 běží I2C1 na 400 kHz (dřív 50 kHz) a SensorsTask čte ADS1115 častěji/jinak; SCL/SDA jsou na FPGA desce blízko hodin.
     Ověření: I2C1 zpět na 50 kHz (`i2c.c` Timing `0x70303AEE`), jinak beze změny.
  3. **Změna zapojení desky mezi 9. 10. a dnes** (R54 5k1, R58 10k, výměna děličů ADS, Vc visí bez AD5693R). Ověření: porovnat s fotkou/stavem desky z 9. 10.
  4. **Různá metodika:** 9. 10. šlo o ADEV z 40 654 oken a rozdíl `f_regr − f_2pt` z jiného času; dnešní σ závisí na délce měření (viz řádek 12 133 oken).
- Pozn.: dálkově nešlo přepnout hradlo (SCPI `-203 Command protected`, `tap 1` nezměnil `set_gate_idx`), proto není ověřena závislost šumu na délce okna.
- Stav po pokusech: generátor ZAP, FW 0x0423 (SRAM i flash), nic uloženo do konfigurace.

### 2026-10-11 noc — po power-cyklu, signál JEN NA KANÁLU A (B odpojen), FW 0x0423, uptime ~9–24 min

Řízení jedné proměnné: stejný bitstream, stejná deska, stejný zdroj, po ustálení OCXO; změna = odpojen kanál B. UART po power-cyklu mlčí → měřeno přes web
(`tools/fpga_diag.py`: rámec nese `gate_ps` = délka po regresi a `gate2_ps` = **dvoubodová** délka téhož okna, takže f_regr a f_2pt jdou spočítat ze stejného snímku).

- **σ(f_regr − f_2pt) = 0,45 ppb** (335 snímků s přijatou regresí), střed +0,05 ppb. **9. 10.: 0,46–0,49 ppb → shoda.**
  S oběma kanály zapojenými to bylo 1,1–2,0 ppb.
- Dvoubodová řada A: bílá složka (rozdíl sousedních oken/√2) **5,5·10⁻¹⁰** (9. 10.: ADEV dvoubodově 7,6·10⁻¹⁰), σ po detrendu 6,4·10⁻¹⁰ (0,16 ns/okno).
- Výsledek (regrese) σ 3,5·10⁻¹⁰ po detrendu na 100 s — **beze změny** proti zapojení A+B (regrese ten šum zprůměruje).
- **Závěr:** zhoršení dvoubodového odhadu o 2,5–4× způsobilo **zapojení signálu na B současně s A** (T-kus / zátěž zdroje, přeslech nebo odraz na vstupní straně).
  Kanál A sám je v pořádku a je tak dobrý jako 9. 10. HYPOTÉZA o příčině uvnitř „B zapojený": zátěž zdroje (jedna z poloviček T-kusu bez dobrého
  přizpůsobení), přeslech vstupů A↔B v modulu, nebo rušení z druhého tvarovače. **Ověření:** zapojit B a A přes rozbočovač/oddělovací zesilovač místo T-kusu;
  případně B jen na vzdálený/slabší zdroj; porovnat σ(f_regr − f_2pt) (cíl ≤ 0,5 ppb).
- ⚠️ Zbytek proměnných se mezi oběma měřeními nezměnil jen v tom, že obě proběhla po power-cyklu s uptime ≥ 7 min; `regr_used` ve snímcích web 335/400 je artefakt
  (viz oprava výše), firmware hlásí ~99,9 %.

### 2026-10-12 — KANÁL B SÁM (A odpojen), po power-cyklu, FW 0x0423 (100 MHz), uptime 8–10 min

B je primární kanál (`set_chan 1`, uloženo v nastavení; nové měření spouští primární kanál, takže s odpojeným A a primárním A by se neměřilo). **B nemá regresi**
(`u_wrb` je `REGR(0)`) → všechna čísla níže jsou dvoubodová. Zdroj: `sdramlog dump 200` (2× 199 souvislých oken, UART), `tdc`, `tdc hist`, `status`.

- **Počítání hran:** okno s hranou navíc 0, s chybějící 0, `SAMOKONTROLA` 0 zamítnutí, `spike-reject` 0 oken, `SEQ` bez děr. Čisté.
- **Šum (dvoubodový):** σ reziduí po detrendu **1,01·10⁻⁹ / 1,11·10⁻⁹** (0,25 / 0,28 ns za 0,25 s), 191–186 z 199 oken v ±0,5 ns, všech 199 v ±1 ns, max 3,2·10⁻⁹.
  ADEV(0,25 s) **1,12·10⁻⁹**, (0,5 s) 5,2·10⁻¹⁰, (1 s) 3,9·10⁻¹⁰, (2 s) 1,4·10⁻¹⁰ → klesá přibližně s 1/τ (bílý šum). Lag-1 autokorelace −0,21 (dvoubodové okna sdílejí hranu).
  Šum jedné časové značky ≈ σ_okna/√2 ≈ **0,18–0,20 ns**.
- **Srovnání s A samotným** (záznam 2026-10-11 noc, dvoubodově): σ 6,4·10⁻¹⁰ (0,16 ns/okno), bílá složka 5,5·10⁻¹⁰ → **B sám je ~1,6–1,8× hlučnější než A sám.**
  Stejná velikost šumu jako nezávislá část na kanál z A−B při zapojení A+B (~0,22 ns/okno). Jádro σ A−B při A+B (0,31 ns) tedy zhruba odpovídá součtu obou.
  ⚠️ Zbývá nevyjasněno: A při A+B bylo 1,1–2,0 ppb (≈ 0,28–0,5 ns), tedy hlučnější než A i B samotné → při současném zapojení se kanály navzájem ruší (viz TODO #293).
- **Kalibrace TDC B** (`tdc hist`, studený start, cal při 35,4 °C): 160 kódů (k 2..258), průměrný bin 62,5 ps (A 157 kódů, 63,7 ps), největší bin **172 ps** (1,7 % periody; A 302 ps),
  za koncem řetězu 0, 17 kódů s binem < 5 ps (tapy „mrtvé" ~10 %, A 15). Rozložení šířky po 25 kódech 0,85–1,17 ns (rovnoměrné, bez zhroucené oblasti). **Kalibrace B je zdravá.**
- 🔴 **Rezerva řetězu je menší, než se psalo:** nejvyšší kód B **258 ze 268 tapů** (A 252) → rezerva **10 tapů = 3,7 %** (A 6 %), ne 6–8 %. `max tap` 256 (B) / 248 (A).
  Studený start je nejhorší případ (rychlejší křemík = víc tapů za 10 ns). Při dalším zchlazení nebo vyšším napětí jádra hrozí „za koncem řetězu" a zkreslení (`status` → `retez kratky`).
  Dřívější výrok „max tap 200" byl z 125 MHz (8 ns). Sledovat `tdc` po studeném startu a v chladné místnosti; řešení (delší řetěz na dalším řádku CFU, nebo 125 MHz) je v rozporu se zjištěním L-0154.

#### Opakování téhož dne (uptime 83–95 min, bez resetu, A pořád odpojen, B primární)

- **Šum B se opakuje:** 3× `sdramlog dump 200` (okna se částečně překrývají → nejsou nezávislé): σ **0,96 / 1,02 / 0,96·10⁻⁹**, ADEV(0,25 s) 0,90–0,92·10⁻⁹, 187–194 z 199 oken v ±0,5 ns.
  **Nezávislá řada z webu `/api/state`:** 961 souvislých oken (240 s), σ **1,01·10⁻⁹** (0,25 ns/okno), bílá složka (rozdíl sousedních/√2) **0,98·10⁻⁹**, 907/961 oken v ±0,5 ns. Shoda s ranním měřením (1,0–1,1·10⁻⁹).
- **Rekalibrace TDC za tepla je velmi reprodukovatelná** (7× `tdc cal`, FPGA teplota z TMP117 35,8 °C): A nejvyšší kód vždy **252**, B vždy **250**; největší bin A 251–259 ps, B 177–180 ps; počet kódů A 157–158, B 153–159.
- **Studený start dal jinou kalibraci než rekalibrace za tepla:** nejvyšší kód B **258** (po rekalibraci 250), největší bin A **302 ps** (po rekalibraci 255). Teplota z TMP117 (deska FPGA) se přitom skoro nezměnila
  (35,4 → 35,8 °C), ale křemík se mezitím zahřál (self-heating); desková teplota to nevidí a `teplot. rekal` se kvůli tomu nespustí.
  **HYPOTÉZA:** kalibrace při startu je pořízena na studeném čipu (rychlejší tapy, dosah +8 tapů) a po zahřátí je tabulka staršího stavu. Důsledek: pomalu se měnící systematická chyba časové značky řádu desetin ns?
  Měřením neověřeno: změna kmitočtu po rekalibraci (+6 až +8 mHz) je v řádu driftu OCXO (+120 ppb/h → ~+7 mHz za 5 min), takže se od něj nedá oddělit.
  **Ověření:** po startu dvakrát `tdc cal` v 1., 30. a 90. minutě a porovnat nejvyšší kód a rozdíl mediánu kmitočtu přes přerušení; nebo čidlo teploty křemíku (není). **Návrh:** automatická rekalibrace po ustálení (např. 20 min po startu).
- **Rezerva řetězu B (po rekalibraci za tepla): 250 z 268 tapů → 18 tapů (6,7 %); při studeném startu jen 10 tapů (3,7 %).** A: 252 → 16 tapů (6,0 %) v obou případech.

### 2026-10-13 — A i B po power-cyklu, FW 0x0423, CM7 v0.15.0, hradlo 0,25 s

> 🔴 **OPRAVA (uživatel, 2026-10-13):** zapojení bylo **VŽDY JEN PŘES T-KUS**. Rozbočovač −6 dB ani útlumový článek −6 dB **nebyl** použit, protože s ním (i s útlumem −6 dB
> na jednom vstupu) **kanál vůbec neměří** — signál je pod prahem vstupu. Slova „rozbočovač" v záznamu níže a v ostatních dnešních záznamech čti jako **T-kus**. Závěr
> „rozbočovač je horší než T-kus" **neplatí**; platí: tři běhy se stejným T-kusem dávají 1,1–2,5 ppb (rozptyl mezi běhy), A sám 0,45 ppb. Rozbor vstupní cesty viz „Vstupní cesta" níže.

Signál na obou kanálech (edges A i B = 2 500 002). Kanál A primární, regrese aktivní.

| měření (uptime) | σ(f_regr − f_2pt) | oken použito / zamítnuto |
|---|---|---|
| 8 min | **2,19 ppb** | 1646 / 12 |
| 10 min | **2,27 ppb** | 1658 / 0 |
| 30 min | **2,53 ppb** | 1654 / 4 |

- A−B (90 s, web): medián **+0,168 ns** (T-kus 0,01 ns → rozdíl délek kabelů z rozbočovače), jádro σ **0,45 ns**, **24 % odlehlých** (> 1 ns, T-kus 3 %).
- **Závěr: rozbočovač je horší než T-kus, ne lepší** (A sám 0,45 ppb; T-kus 1,1–1,8 ppb; rozbočovač 2,2–2,5 ppb). Šum dvoubodového odhadu tedy neroste s tím, že jsou kanály propojené,
  ale s **tím, jak je vstupní signál rozdělen/zatížen**.
  **HYPOTÉZA (nejpravděpodobnější):** vstupní tvarovač (74HC04 přes 120 Ω) dostává při rozdělení méně amplitudy/strmosti (odporový rozbočovač −6 dB, T-kus dvojí zátěž); jitter prahu ~ šum / strmost.
  Souvisí s dřívějším nálezem L-0137 (vstup pod prahem LVCMOS33 dal falešné hrany).
  **Ověření:** (a) A SÁM přes 6dB útlumový článek (simulace půlové amplitudy) — když σ vzroste z 0,45 na ~2 ppb, je to amplituda; (b) změřit amplitudu na pinu FPGA osciloskopem;
  (c) **aktivní rozbočovač / buffer** (CMOS fan-out, 3,3 V), nebo A a B z oddělených výstupů GPSDO.
- **Rekalibrace TDC po startu (A+B zapojeno, hradlo 0,25 s):** boot (uptime 2 min): A 252 kódů, největší bin **311 ps**, B 250; po `tdc cal` v 14. min A **301 ps**; po `tdc cal` v 30. min A **259 ps**, B beze změny (250, bin 170–175 ps).
  Největší bin A (k=161) tedy po studeném startu ~300 ps a za ~30 min se ustálí na ~255–260 ps (před tím při dvou studených startech 302 a 311 ps, za tepla po 90 min 251–259 ps). Nejvyšší kód B tentokrát při startu **250**, ne 258 (předchozí 258 se neopakovalo).
  Medián kmitočtu před/po rekalibraci: 14. min +1,7 mHz, 30. min +6,8 mHz — v obou případech v řádu driftu OCXO (−250 až +65 ppb/h), vliv studené tabulky se **neprokázal**.
  **Závěr:** drobná tepelná nestabilita největšího binu A (+50 ps) po startu; dopad na měření nezjištěn. Automatická rekalibrace po ~30 min je levná pojistka, ale nepotvrzená nutnost.

### 2026-10-13 — VALIDACE TDC ŘETĚZCE KANÁLU B (je v něčem jiný než A? měří stejně?)

**Staticky (bitstream FW 0x0423, `impl/pnr`):**
- Rozmístění (`sim/check_tdc_placement.py`): A v řadě R26, B v R27, oba tapy 0..267, stejné sloupce **C2[1] .. C46[4]**, souvislé → **PASS**.
- Netlist (`sim/check_tdc_netlist.py`): 2 × 268 stupňů carry (536), 2 hlavy (CIN = `sig_eff`) → **OK** (skript měl zastaralé očekávání 288 stupňů, opraveno na fyzických `TDC_NTAP` = 268).
- RTL: oba kanály jsou tatáž `tdc_chan` se shodnými parametry; liší se jen `want_seg` (A dostává značky vnitřních hran pro regresi, B ne) a že `win_recip` B nemá regresi.
  Na přesnost časové značky hraničních hran to nemá mít vliv (A to dělá navíc, B ne).

**Z kalibračních histogramů** (`tdc hist`, boot a rekalibrace za tepla, `tools/tdc_hist_analysis.py`, `ab_chain.py`):

| | A | B |
|---|---|---|
| obsazených kódů | 157 | 153–160 |
| poslední kód | 252 | 250–258 |
| průměrný / medián obsazeného binu | 63,7 / 64 ps | 62,5–65,4 / 68 ps |
| sd šířky binu | 45,8 ps | 39–41 ps |
| největší bin | **262–311 ps** (k161, k215 = 233–240) | **172–180 ps** (k161, k39, k191) |
| prázdné kódy uvnitř | 94 | 96 |
| kvantizační σ jedné hrany / okna | 31 / 44 ps | 27 / 39 ps |

Řetězce jsou statisticky rovnocenné. **Společné rysy:** velký bin na k=161 u obou (A 262–311, B 172–180) — struktura křemíku (hranice sloupců dlaždic), ne chyba jednoho kanálu;
korelace šířek binů po 8 kódech r = 0,44–0,64. Rozdíl časových os A−B (kumulativní zpoždění k-tého tapu) max 240–340 ps, sd 93–113 ps — fyzický rozdíl zpoždění řetězců,
který je vlastní kalibrace každého řetězce srovná (kalibrace je per kanál). **Rozdíl:** A má větší největší bin (k161) než B.

**Dynamicky** (A i B přes rozbočovač, hradlo 0,25 s, uptime ~65 min, 395 oken se STEJNÝM počtem hran, `abdecomp.py` z `/api/fpga`: dvoubodové délky oken `gate2_ps` a `dt_b_ps`):
- σ chyby okna **A 1042 ps, B 1075 ps**, σ(A − B) **176 ps**, korelace **r = 0,99**. Obě okna se uzavírají touž hranou, takže **~99 % chyby je společná oběma kanálům**
  (σ jedné hrany ~740 ps společná; nezávislá část A ≈ 0, B ≈ 160 ps).
- **Závěr o řetězci B:** měří stejně jako A (r = 0,99, rozdíl oken 176 ps ≪ společná chyba 1,04 ns); rozdíl B−A tvoří ~160 ps/hrana nezávislého šumu navíc u B
  (v souladu s B samotným ~1,6–1,8× hlučnějším než A samotný) — **nejde o chybu TDC**, spíš o vstupní cestu B / přizpůsobení.
- **Důležité:** dominantní šum (~0,74 ns/hrana) je **společný a vzniká PŘED oběma TDC** (signál / hrana na vstupu, nebo sdílená časová základna), a **jen při zapojení A+B** (A sám 0,11 ns/hrana).
  Společný šum plyne ze zatížení zdroje: aditivní šum na sdíleném průběhu napětí posouvá práh obou komparátorů stejně (šum/strmost), a po rozdělení klesne amplituda i strmost
  (pod prahem VIH 2,0 V tvarovače strmost mizí → jitter strmě roste). **HYPOTÉZA, vysvětluje i horší výsledek rozbočovače (−6 dB) než T-kusu.**
  **Test:** A sám přes 6dB útlum; změřit amplitudu na pinu FPGA; aktivní rozbočovač.

### Vstupní cesta (schéma `FPGA_Module_2_1`, netlist 2026-10-13) a její rezerva amplitudy

- **CH_A:** SMA `J1` pin 1 → net `CH_A` → **R19 120 Ω proti GND** a rovnou **pin 25 FPGA** (LVCMOS33). **CH_B:** `J2` → R20 120 Ω → pin 27. **Žádný zesilovač, tvarovač ani komparátor** (74HC04 ze starší verze v tomto schématu není).
  Vstup je tedy **stejnosměrně vázaný, zakončený 120 Ω** a práh určuje samotný vstup FPGA LVCMOS33 (VIL ≤ 0,8 V, VIH ≥ 2,0 V, typicky se překlápí kolem 1,4–1,65 V).
- **Důsledek:** hrana musí mít na pinu **dostatečný swing a strmost přes práh**. Jitter hrany ≈ (šum na průběhu) / (strmost v bodě prahu).
  - A samotný: zdroj do 120 Ω → swing (dřívější měření 0–2,86 V, L-0137) → práh leží kolem poloviny, strmost je dobrá → 0,45 ppb.
  - **T-kus:** zdroj vidí 120 Ω ‖ 120 Ω = **60 Ω** → swing klesne o **~23–31 %** (podle výstupní impedance zdroje 50–100 Ω), práh je pak blíž špičce a strmost v bodě prahu výrazně klesne.
  - **−6 dB** (rozbočovač nebo útlum): swing poloviční (např. 2,86 → 1,4 V) → **hrana práh vůbec nepřekročí** → kanál neměří (pozorováno).
  Pozorované chování (A sám 0,45 ppb, T-kus 1,1–2,5 ppb s ~99 % společným šumem, −6 dB bez měření) **odpovídá hypotéze „malá rezerva amplitudy/strmosti na vstupu"** — je to nejpravděpodobnější příčina,
  ale amplituda na pinu **nebyla změřena** (⬜ osciloskop; neznámá je i výstupní úroveň a impedance GPSDO).
- **Možné opravy (od nejlevnější):** (1) větší swing: **R19/R20 zvětšit** (např. 220–470 Ω) nebo vypustit, je-li zdroj CMOS 3,3 V (10 MHz, krátké kabely → odrazy nevadí); pozor na přepětí (> 3,6 V) a záporné špičky u sinusového zdroje;
  (2) **každý vstup z vlastního výstupu GPSDO** místo T-kusu; (3) **aktivní fan-out buffer** s 3,3 V CMOS výstupy (např. 74LVC1G17 / 74AUP) — nebo plánovaný vstupní modul s komparátorem a nastavitelným prahem (MAX9601, MCP4728);
  (4) před změnou **změřit amplitudu a strmost na pinu** a zjistit typ a úroveň výstupu GPSDO (sinus dBm / CMOS).

### Rezerva TDC řetězu po zapnutí, 100 MHz (2026-10-14, audit `2026-10-10_volba-kmitoctu-hodin.md` kap. 10.4 krok 3 / M2)

Sestava: FPGA FW 0x0423 (build 2026-10-11 17:03 UTC), CM7 `ef07ca0`+`a30b0b4`; **A = externí GPSDO, B odpojen, T-kus nepoužit**; power-cyklus, záznam `tools/tdc_rez_log.ps1`
(`tdc cal;tdc;sensors` v uptime 87 s až 5406 s), data `docs/audit/data/2026-10-14_tdc_rez_M2.csv` (+ `.raw.txt`).
- **Boot kalibrace** (uptime 22 s, TMP117 0x4A 35,7 °C – deska byla před power-cyklem teplá, nejde o studený start): A 252 (rezerva 16, OK), **B 257 (rezerva 11, POZOR)**, za koncem 0/0.
- **Všech 8 ručních `tdc cal` v 87–5406 s: A last 252, max tap 248; B last 250, max tap 248; události za koncem 0; rezerva A 16 / B 18 tapů.** Teploty za 90 min: 0x4A 35,6 → 36,6 °C, 0x49 (OCXO) 45,6 → 46,8 °C, 0x48 28,8 → 29,4 °C.
  Největší bin A 235–275 ps (k161), B 167–181 ps.
- **Závěr:** při teplotě čipu ±1 °C je řetěz stabilní (žádný drift `last`); rezerva A i B ≥ 16 tapů (~6 %) – nad prahem POZOR (15 tapů) se jen těsně drží. **Boot kalibrace B (257) se liší od všech
  pozdějších (250)** – příčina neurčená (kalibrace se zřejmě liší stavem po náběhu, ne teplotou: teplota se mezi nimi nezměnila). Dřívější výklad „258 za studena / 250 za tepla = teplota" je tím
  zpochybněn. ⬜ Neověřeno: skutečně studený start (M3, viz níže – selhal), druhý power-cyklus.
- Rozhodovací pravidlo (audit 10.4): 100 MHz zůstává, dokud nejmenší rezerva při nejchladnějším startu nespadne pod ~8 tapů nebo se neobjeví události za koncem.

#### M1: 10 po sobě jdoucích `tdc cal` (2026-10-14 22:03–22:09, teplý start, uptime 1110–1467 s)

Sestava stejná jako M2 (A = externí GPSDO, B odpojen), skript `tools/tdc_m1_repeat.ps1` (`tdc cal;tdc` á ~40 s), data `docs/audit/data/2026-10-14_tdc_M1.csv` (+ `.raw.txt`).
Teploty za 6 min: OCXO 46,6 → 46,7 °C, deska 29,5 °C, 0x4A 36,1 → 36,3 °C.
- **A: last 252 ve všech 10 během, B: last 250 ve všech 10** (rozptyl 0); události za koncem 0/0; rezerva A 16 / B 18 tapů ve všech 10. Největší bin A 225–251 ps, B 162–181 ps.
- **Jediná odchylka:** běh 7, A max tap 264 (jinde 248), přitom `last` 252, události za koncem 0 a rezerva 16 – ojedinělý zásah za nejvyšší kód, který rezervu nezměnil.
- **Závěr:** rozdíl B 257 (boot) vs 250 (všechny pozdější) **není statistický rozptyl** – opakovaná kalibrace za ustálené teploty dává pokaždé stejný kód. Zbývá, že 257 byla specifická pro **první kalibraci po bootu**
  (HYPOTÉZA, jak ověřit: zachytit boot kalibraci č. 1 po teplém i studeném startu hned po zapnutí, dokud ji nepřepíše `tdc cal`).

#### M3: studený start (2026-10-14, chladnutí 55 min) – SELHAL, bez dat o rezervě

OCXO 25,1 °C, deska 24,7 °C, power-cyklus. Si5356 se nezamkl (`si5356_status = 28` = LOS_XTAL + **LOS_CLKIN + PLL_LOL**), hodiny FPGA v `ZOTAVENI`, kalibrace TDC neproběhla ani za 19 min (OCXO už 44–45 °C),
`freq_hz = null`. Po druhém power-cyklu s teplým OCXO (45 °C): `si5356_status = 0`, měření běží. Podrobnosti a postup ověření: `STATUS.md` TODO #295; surová data `docs/audit/data/2026-10-14_tdc_rez_M3_boot.raw.txt`.
Rezerva TDC za studeného startu tedy **změřena není**.

#### M3b: studený start podruhé (2026-10-15 16:50, deska chladla přes noc) – Si5356 nezamkl, re-init v 61 s pomohl, rezerva B 3–6 tapů

Sestava jako M1/M2 (A = externí GPSDO, B odpojen), FW 0x0423 (build 2026-10-11 17:03), CM7 ef07ca0+a30b0b4. Skripty `tools/cold_start_log.ps1` (`/api/state` á 5 s) a `tools/cold_start_auto.ps1` (scénář);
data `docs/audit/data/2026-10-15_M3b_studeny.csv` (+`.raw.jsonl`), `2026-10-15_M3b_auto.txt`, `2026-10-15_M3b_boot_manual.raw.txt`, `2026-10-15_M3b_tdc_cold.raw.txt`.
- **Teploty při zapnutí:** OCXO 23,85 °C, deska 23,9 °C. Ohřev OCXO 24 → 36 °C za 190 s.
- **Si5356:** `si5356_status = 28` (LOS_CLKIN + PLL_LOL) od uptime 11 s, `seq_meas = 0`. V uptime 61 s UART `si5356` (re-init) → `status218 = 0x00`, o 5 s později `si5356_status = 0`, měření běží
  (10000016 → 10000008 Hz). Uptime souvislý, žádný reset. **Potvrzuje hypotézu (a) z TODO #295**; spontánní zamknutí vylučuje M3 (28 po 19 min). 2 studené starty ze 2 selhaly (M3, M3b).
- **Boot kalibrace č. 1 (hned po zotavení, FPGA 23,9 °C):** rezerva **A 14 / B 3** tapů z 268, za koncem 0/0; nejvyšší kód A 254 / **B 265**, max tap A 248 / **B 264**, největší bin A 200 ps / B 160 ps.
- **Ruční `tdc cal` (uptime ~100 s):** A 14 / **B 6** (A last 254, B last 262, max tap B 264). **Teplotní rekalibrace č. 7 (cal při 26,9 °C):** A 16 / **B 6**.
- **Proti teplému startu (M1/M2): A 252 / B 250, rezerva 16 / 18.** Za studena je řetěz B o ~12–15 tapů delší (rychlejší hradla), rezerva B klesá z 18 na 3–6 (1–2 %). Události za koncem řetězu 0.
- **Závěr:** pravidlo kap. 10.4 („≤ ~4 tapy" → plán 110 MHz) splnila boot kalibrace B (3); všechny studené hodnoty B (3/6/6) jsou pod 8. Zatím 1 pokus při ~24 °C okolí. TODO #297; rozhodnutí o 110 MHz patří uživateli.
- ⬜ **Neověřeno:** zápis `ERRLOG_K_TDC` (k=13) – `/api/errlog?from=0` opět `n:0`, `total` 4873 beze změny (viz #296).

### Kritérium „teplota neustálená" (`warmup_ready`) proti naměřené frekvenci OCXO (2026-10-15, STATUS #299)

Otázka: je podmínka `uptime >= 300 s` **a** `|dT/dt| < 0,08 °C/min` (teplota TMP117 0x49, okno 120 s) opodstatněná jako ukazatel „OCXO ještě není stabilní"?
Data: M3b (studený start, A = externí GPSDO, `freq_hz` z `/api/state` á 5 s, 457 vzorků, 2381 s) a M3c (OCXO už horké, reset STM, 357 vzorků). Skript a výstup: `docs/audit/data/2026-10-15_warmup_an.py`, `…_warmup_analyza.txt`.
`freq_hz` = rozdíl GPSDO − OCXO (OCXO je zároveň časová základna čítače), takže jeho změna je posun OCXO. 1 Hz = 100 ppb.

| veličina | studený start (M3b) |
|---|---|
| frekvence do ±10 ppb od ustálené hodnoty | **od uptime ~160 s** (T_ocxo 34 °C, sklon +4,8 °C/min) |
| `warmup_ready()` natrvalo pravda | **až v 1092 s** (sklon < 0,08 °C/min), tj. ~7× později |
| zbytkový posun po 1092 s | 10 → 0 ppb se táhne ještě do ~2050 s, při sklonu 0,00–0,04 °C/min |
| T_ocxo (TMP117) za tu dobu | 26,7 → 46,2 °C; časová konstanta zahřívání pouzdra ~210 s |

- **Datasheet (Bliley 6891):** ±1 ppm za 2 min, ±0,1 ppm za 2,5 min, ±0,03 ppm za 5 min, ±0,01 ppm za 15 min (vztaženo k 30 min po zapnutí). Naměřeno (M3b) je lepší nebo shodné: ±10 ppb od ~160 s. **Pravidlo `uptime >= 300 s` se datasheetem i měřením obhájit dá** (±30 ppb).
- **Sklon teploty z TMP117 měří zahřívání pouzdra a desky, ne regulaci pece.** Pec uvnitř OCXO dosáhne pracovního bodu za ~2–3 min; pouzdro a deska se prohřívají dalších ~15 min, na frekvenci to (podle dat) skoro nemá vliv. Datasheet udává teplotní stabilitu ±0,005 ppm v −5…+50 °C okolí (< 0,1 ppb/°C); v datech je změna 36 → 46 °C doprovázena posunem ~10 ppb, což by byl ~1 ppb/°C, ale **nelze oddělit od ustalování a stárnutí** (Vc se zároveň posouvá o 27 mV; příčina neurčena).
- **Šum kritéria:** v M3c (teplo) leží sklon v pásu −0,02…+0,06 °C/min, práh 0,08 nekmitá; o hlášce tam rozhoduje jen 300 s. M3c ale ukázalo, že `uptime` je uptime **STM, ne OCXO**: po resetu STM při horkém OCXO svítí „neustálená" 300 s, přestože posun frekvence je < 1,5 ppb od prvního vzorku (107 s).
- **Kritérium pro něco jiného:** práh „pod 0,1 °C/min" pochází ze zadání **kalibračního průvodce offsetu vstupního modulu** (`citac_zadani_UI.md` kap. 10, krok 1/5 „Teplota ustálená"), kde jde o teplotu **modulu** (TMP117 0x4A) a drift analogového vstupu, ne o OCXO. FW ho převzal pro OCXO (0x49) a text varování „kalibrace zamčená" slibuje zámek, který neexistuje.

**Závěr [V, N = 1 studený start]:** pro hlášku „OCXO se zahřívá" je podmínka sklonu teploty **neopodstatněná** (prodlužuje hlášku o ~13 min za hranici 300 s, kdy už je frekvence ~2 min v ±10 ppb) a zbytek 10 ppb ji nezachytí. Podmínka `uptime >= 300 s` je opodstatněná. Pro budoucí zámek kalibrace vstupního modulu by sklon dával smysl, ale na teplotě **modulu** (0x4A).
**Neověřeno:** druhý studený start (M4 dá další řadu), prvních 160 s (měření se překrývá s re-initem Si5356 v 61 s, výchylky ±400 ppb v 67–98 s jsou pravděpodobně artefakt, ne OCXO – **HYPOTÉZA**), závislost na okolní teplotě (jen ~24 °C).

**Změna kritéria po rozboru (2026-10-15, `warmup_ready`, necommitnuto, ⬜ neověřeno na HW):** hláška i TFOM/web zůstávají, podmínka je nově: platná T49; ΔT (T49−T48) ≥ 10 °C, je-li T48 platná;
sklon T49 < 0,3 °C/min (dřív 0,08; odvození: ≤ 1 ppb / 10 min při vazbě 3× nad datasheet 0,09 ppb/°C); uptime ≥ 900 s po zapnutí napájení (POR/BOR), jinak ≥ 300 s.
Přehrání záznamů (`docs/audit/data/2026-10-15_warmup_nove_pravidlo.txt`): M3b studený start hotovo od 904 s (dřív 1092 s), M3c teplý 304 s beze změny, simulovaná mrtvá pec (ΔT 1 °C, sklon 0) nikdy (dřív po 300 s).
Text hlášky změněn na „TEPLOTA NEUSTALENA  - OCXO se zahriva, presnost snizena" (slib „kalibrace zamčená" odstraněn, nic se nezamykalo) — viz STATUS #299.

#### M4: studený start potřetí (2026-10-16 16:19, deska chladla přes noc), CM7 v0.16.2 – Si5356 zamkl sám, re-init se nespustil

Sestava podle plánu #300 (A = externí GPSDO, B měl být přes T-kus na tentýž signál; **zapojení B uživatel v tomto běhu nepotvrdil**, rezerva B se měří z kalibrace TDC bez ohledu na signál; hradlo 1 s), FW 0x0423 (build 2026-10-11 17:03), CM7 v0.16.2. `tools/cold_start_log.ps1` (45 min plánováno, záznam do uptime 2293 s = 38 min, pak deska vypnuta) + `tools/cold_start_auto.ps1`;
data `docs/audit/data/2026-10-16_M4_studeny.csv` (+`.raw.jsonl`), `2026-10-16_M4_auto.txt`, rozbor `2026-10-16_M4_analyza.txt` (skript `m4_an.py` v scratchpadu, ne v repu).
- **Teploty při zapnutí:** OCXO 23,83 °C, deska (T48) 23,87 °C, FPGA 24,09 °C. Ohřev OCXO: 30,5 °C za 119 s, 40,1 °C za 302 s, 45,2 °C za 900 s, 46,7 °C za 2198 s.
- **Si5356:** `si5356_status = 0` od prvního vzorku (uptime 8 s), ani jeden řádek s nenulovým stavem, `si_reinit = 0` celou dobu. **Auto re-init (v0.16.0) se nespustil, protože nebylo co opravovat → jeho funkce zůstává NEOVĚŘENA.**
  Z 3 studených startů (M3, M3b, M4) selhaly 2, tj. vada je přerušovaná; M4 ji nevyprovokoval. Ve `status` v uptime ~11 s: `REFERENCE: auto re-init Si5356 zadny (zamceno od startu)`.
- **errlog (#296) ověřen:** `total` 4873 → 4974; po bootu přibyly záznamy `power-on`, `Si5356 ztrata 10MHz reference (LOS_CLKIN)` (sticky z náběhu, stejné razítko jako boot; `status` ukazuje sticky 247 = 0) a **`ERRLOG_K_TDC` (k=13): „rezerva TDC retezu A=14T POZOR, B=3T KRITICKE"**. První potvrzený zápis tohoto druhu.
- **Frekvence (`freq_hz` = GPSDO − OCXO, 1 Hz = 100 ppb):** ustálená 10 000 008,3148 Hz, σ 1,3 ppb (uptime ≥ 1800 s, N = 98). V ±100 ppb od uptime 130 s, **v ±10 ppb od 144 s** (posledně M3b: od 160 s), v ±3 ppb pořád občas mimo (σ 1,3 ppb). Odchylka od ustálené: +15 570 ppb v 28 s, +3 374 ppb v 59 s, −373 ppb v 99 s, +2 ppb v 160 s.
- **`warmup_ready` (nové pravidlo) přehráno nad CSV:** sklon T49 za 120 s: 2,55 °C/min v 302 s, 0,55 v 600 s, **0,115 v 900 s** (< 0,3), ΔT (T49 − T48) = 17 °C (≥ 10) → podmínka je splněna **v uptime 900 s** (shodně s M3b: 904 s). ⬜ Skutečné zhasnutí hlášky na displeji nepozorováno.
- **Rezerva TDC (A / B z 268 tapů) podle kalibrace:**

| kalibrace | uptime | T_FPGA | A | B |
|---|---|---|---|---|
| č. 1–4 | 14–160 s | 24,1–28,2 °C | 14 | **3** |
| č. 5 | 242 s | 29,9 °C | 16 | 6 |
| č. 6 | 494 s | 32,9 °C | 16 | 10 |
| č. 7 | 1591 s | 35,8 °C | 16 | **18** |

  Za koncem řetězu 0 / 0 po celou dobu, `ovf` 0 / 0. Rezerva B je 3 v celém pásmu 24–28 °C, pak roste ~2 tapy/°C (6 při 29,9 °C, 10 při 32,9 °C, 18 při 35,8 °C), tj. není lineární; studená boot kalibrace č. 1–4 (B = 3) je shodná s M3b (3–6).
  **Závěr (N = 2 studené starty při ~24 °C okolí):** B za studena 3–6 tapů, tj. pod prahem 8 z kap. 10.4; po ~10 min je B ≥ 10 a po zahřátí 18. Rozhodnutí o plánu 110 MHz (TODO #297) zůstává na uživateli; data ukazují, že problém je omezen na prvních ~4 min po studeném zapnutí.
- **Ztráta měření při kalibracích:** hradlo 1 s → 0,9978 měření/s, za 2279 s chybí **6 měření**, a to přesně v krocích, kde se změnila kalibrace TDC: č. 3 (−1), č. 5 (−1), č. 6 (−2), č. 7 (−2). Žádné jiné díry (`seq_meas` jinak +5 za 5 s). Tj. teplotní rekalibrace stojí 1–2 okna; to je očekávaná cena, ne chyba.
- **Samokontrola FPGA (`status`):** po prvním `status` (uptime ~15 s) „zamítnuto oken: čtení 1, hodiny 0, délka okna 0 ⇒ `<== FPGA MĚŘÍ ŠPATNĚ`", ve druhém (~150 s) stejně 1 a navíc `CITANI HRAN: s chybějící 5 (nezobrazeno)`. Hodiny 0, délka 0. ⬜ **Neověřeno:** příčina zamítnutého okna č. 1 (jednorázové při náběhu, nebo opakovatelné); konečný stav počítadel po 38 min nezachycen (deska vypnuta dřív, než šel pustit `status`). Při příštím běhu přidat `status` na konci.
- **Neověřeno:** auto re-init Si5356 (nebylo vyprovokováno), zhasnutí hlášky „neustálená" na displeji, příčina zamítnutého okna a 5 oken s chybějící hranou. Zaměření příštího studeného startu (M5): `status` na konci běhu, delší záznam, jiná okolní teplota.

### 2026-10-16 večer — AD5693R poprvé osazen: ladění OCXO přes DAC (CM7 v0.16.2, FPGA FW 0x0423, uptime 130–280 s, OCXO 45–46 °C)

Osazení: U12 `AD5693RBRMZ-RL7`, C55 10 nF X7R, C56 neosazen, R78 100k, C68 1 µF film (dočasně), R73 1k0, C61 1 µF film, R72 82R, C59 100 nF X7R. `scan1` našel 0x4C; `dac`: `ctrl OK`, kód 0, readback 0.
Měřeno `dac mv <mV>` po 12 s ustálení, pak 10 vzorků `freq_hz` z `/api/state` (hradlo 1 s); `freq_hz` = měřený kmitočet GPSDO vůči OCXO jako časové základně, 1 Hz = 100 ppb. Skript a výstup: scratchpad `dac_step.ps1`, `dac_step2.txt`.

| zápis DAC | AIN0 (pin VC) | `freq_hz` (průměr 10 vzorků) | změna proti předchozímu |
|---|---|---|---|
| 0 mV (kód 0) | 56 mV | 10 000 008,4075 Hz | – |
| 1000 mV (kód 13107) | 1034 mV | 10 000 004,0951 Hz | −4,312 Hz |
| 2000 mV (kód 26214) | 2015 mV | 9 999 999,4844 Hz | −4,611 Hz |
| zpět 0 mV | 56 mV | 10 000 008,4128 Hz | +8,928 Hz (rozdíl od první nuly 5 mHz = 0,5 ppb) |

- **Readback sedí pro nenulové kódy** (13107 i 26214 vrátily přesně zapsanou hodnotu, `readback nesedi 0`, chyb I2C 0). Hypotéza z `ad5693.h` o tvaru readbacku (NOP + opakovaný START místo čistého čtení) tím padá: **není potřeba měnit kód**.
- **Zapnutí na zero-scale potvrzeno** (kód 0, readback 0, před prvním zápisem). Frekvence s DAC na nule (10 000 008,41 Hz) je shodná s dřívějším stavem bez čipu (10 000 008,36 Hz, M4), tj. osazení samo ladění nezměnilo.
- **Strmost EFC:** 4,31 Hz/V (0→1 V) a 4,61 Hz/V (1→2 V) vůči zapsanému napětí, tj. 0,43–0,46 ppm/V. Leží v rozsahu 2,5–5,5 Hz/V, který předpokládá #277. Jeden LSB (76,3 µV) = 0,33–0,35 mHz = 3,3–3,5·10⁻¹¹ relativně. Zvýšení Vc zvyšuje kmitočet OCXO (`freq_hz` klesá).
- **OCXO je při Vc ≈ 1,89 V naladěn na GPSDO** (lineární interpolace úseku 1–2 V: `freq_hz` = 0 při 1,0 + 4,0951/4,6107 = 1,888 V, tj. kód ≈ 24 746 = 1888 × 65536 / 5000). Při Vc = 0 je OCXO o 0,84 ppm níž. ⚠️ Platí pro teplotu 46 °C a ještě se zahřívající OCXO, jen orientačně. Kód se do OCXO sám nezapisuje (smyčka #82 a perzistence kódu nejsou).
- **Chyba AIN0 vůči zapsanému napětí:** AIN0 ≈ 56 mV + 0,98·V_dac (56 / 1034 / 2015 mV pro 0 / 1000 / 2000 mV). **Offset 56 mV při nule** může být v měřicí cestě (U14, ADS1115, dělič) nebo ve vstupu OCXO; z dat nelze určit. **Sklon 0,98** by odpovídal vstupní impedanci EFC ~50 kΩ za 1,08 kΩ (R73 + R72), ale stejně dobře tolerancím děliče AIN0 (±1 %). Datasheet OCXO (Bliley 6891) udává vstupní impedanci EFC **10–100 kΩ**, s čímž je ~50 kΩ v souladu (úbytek na R73 + R72 = 1,08 kΩ tedy 1–10 %; servo ho vyrovná, jen strmost v Hz/V za DAC je o to menší). Rozhodl by multimetr přímo na pinu VC při kódu 0 a při 2000 mV.
- **Čas ustálení RC (R78 100k × C68 1 µF) neměřen:** AIN0 se čte ~2× za sekundu, první čtení po zápisu bylo vždy ještě staré (80 mV, resp. 1034 mV, resp. 1960 mV); po 12 s sedí.
- **Pozorování při startu (⬜ nevysvětleno):** po zapnutí měla každá z kanálů ADS1115 AIN0–3 jednu chybu čtení (`err=1..4`, `last=` ≈ uptime) a pak žádné další (`strk=0`, nerostou). Při předchozím startu bez čipu (M4) byly počty 0. Možná souvislost s první sondou/zápisem control registru DAC v prvních sekundách na I2C1. Ověřit při příštím power-cyklu.

### Dlouhodobě — offset reference (datalog, 2026-10-04 až 2026-10-11)

Měřený kmitočet externího GPSDO proti OCXO na desce: **10 000 008,40 Hz (±0,03 Hz v 2h průměrech)**, tj. OCXO desky je o
**−0,84 ppm** pomalejší než GPSDO. Za týden stabilní. Občasné špičky −44 až −50 Hz v min/max obálce (10-09, 10-10, 10-11) zatím nevysvětlené.

---

### 2026-10-16/17 noc — 10 h základní měření smyčky GPSDO (CM7/CM4 v0.18.0, IPC 25, FPGA FW 0x0424, gate 1 s, kanál A = externí GPSDO)

Data `docs/audit/data/2026-10-17_noc.csv*` (zapisovač `tools/gpsdo_log.ps1`, analýza `python tools/analyze_gpsdo_log.py <csv>`). Běh 21:37–07:37, 17 907 řádků po 2 s, 0× `NA`, 0 resetů desky, `errlog` beze změny (4998), smyčka celou dobu `LOCK` (`locked` = 1), `pps_rej` stále 22 (z rozběhu). Rozběh (první hodina) z vyhodnocení odříznut. Smyčka byla po celou dobu **ZAPNUTÁ**, tedy měří se OCXO **řízené** podle GPS, ne volně běžící OCXO.

| Veličina (od 2. hodiny) | Výsledek |
|---|---|
| Kmitočet interního OCXO proti externímu GPSDO (CH A, gate 1 s) | střed **−0,004 ppb**, hodinové střední −0,014…+0,009 ppb, trend **−0,0001 ppb/h**; šum jednotlivého vzorku σ 0,77 ppb (rozlišení hradla ~0,15 ppb + rozptyl 10–50 s, viz níže) |
| Fáze `xr_ns` proti GPS 1PPS | hodinová σ 4–8 ns (první hodina 22 ns), rozpětí 23–40 ns za hodinu; celkem −150…−25 ns, příznak zamčení se nikdy nepřepnul (práh 200 ns) |
| TDEV fáze (smyčka zapnutá) | 1,4 ns @ 20 s … 4,2 ns @ 5000 s |
| ADEV kmitočtu proti externímu GPSDO | 1,8·10⁻¹⁰ @ 2 s, **vrchol 6·10⁻¹⁰ @ 20 s**, 4,0·10⁻¹¹ @ 1000 s, 1,0·10⁻¹¹ @ 4000 s; firmware ADEV obdobně (9·10⁻¹⁰ @ 20 s, 5,4·10⁻¹¹ @ 1000 s, 6,7·10⁻¹² @ 5000 s) |
| Kód DAC | 24 832 → 24 896 (hodinové střední), sklon klesá z 12,7 na 4,4 LSB/h |

**Závěry (s výhradami):**
- ✅ **Obě reference se po celou noc shodují** (střed −0,004 ppb, bez trendu). Rozdíl nikdy nedosáhl úrovně, kterou by šlo oddělit od šumu měření. Konec běhu s hodnotou +1,25 ppb (`10000000,01249`) byl jen šum jednoho vzorku (σ 0,77 ppb), ne odchylka.
- ⚠️ **Dlouhé τ (≥ 1000 s) měří smyčku, ne OCXO:** ADEV klesá jako 1/τ (bílý fázový šum — fáze je vázaná k GPS). Stabilitu samotného OCXO ukáže až běh se `gpsdo off` a drženým DAC (druhý běh).
- ⚠️ **Vrchol ADEV 6–9·10⁻¹⁰ při 20–50 s nelze z těchto dat přiřadit:** může jít o šum 1PPS prošlý předfiltrem smyčky (τ/30 ≈ 33 s), o vlastní časovou konstantu externího GPSDO, nebo o OCXO. ⬜ HYPOTÉZA; rozhodne běh bez smyčky a/nebo změna `gpsdo tau`.
- 🔑 **Fáze neleží na nule, ale úměrně driftu OCXO:** hodinový střed fáze (−125 → −40 ns) je přímo úměrný hodinovému sklonu kódu DAC (≈ −9,3 ns na 1 LSB/h, stálé po celou noc). To odpovídá PLL 2. řádu sledujícímu frekvenční rampu: konstantní fázová chyba = rampa/ω_n², ω_n ≈ 1/τ = 10⁻³ s⁻¹ (měřeno 1/ω_n² ≈ 9,9·10⁵ s²). Není to chyba; fáze se vrací k nule úměrně tomu, jak OCXO přestává driftovat.
- ⚠️ **Drift OCXO v tomto okně ≈ 5 ppb/den** (regrese kódu na čas 150 LSB/den × 3,4·10⁻¹¹ = 5,1 ppb/den; na konci 4,4 LSB/h ≈ 3,6 ppb/den), tedy ~4–5× nad datasheetovým stárnutím 1 ppb/den. **Nelze oddělit** teplotu (OCXO −0,4 °C, deska −0,8 °C, r(kód, teplota) = −0,95) od stárnutí po zapnutí (OCXO běželo před startem jen ~2 h). Regrese dává tempco jen −0,21 ppb/°C a čas 4,9 ppb/den, ale s kolinearitou. Pro #303 (holdover) použít jako **horní mez**, ne jako stárnutí; změřit znovu po dnech provozu bez přerušení.
- Zapisovač: 5 mezer v čase > 3 s (max 5 s), 221 skoků `seq_meas` jiných než 1–2 (1,2 %) — zpoždění HTTP odpovědí, ne ztráta dat desky.

Co zbývá: ✅ běh se `gpsdo off` a drženým DAC (viz níže, jen 1,9 h), ⬜ opakování po dnech (stárnutí), ⬜ zapsat kód DAC do datalogu (stárnutí bez PC), ⬜ znovu sestavený bitstream z commitnutých zdrojů (FW 0x0424 byl sestaven z necommitnutých).

### 2026-10-17 dopoledne — FREE-RUN: smyčka GPSDO VYPNUTÁ (`gpsdo off`), DAC drží kód 24 899 (CM7/CM4 v0.18.0, IPC 25, FPGA FW 0x0424, gate 1 s, kanál A = externí GPSDO)

Data `docs/audit/data/2026-10-17_free.csv*`. Start 08:06:10 (OCXO předtím ~10 h v provozu, bez přerušení napájení), **zastaveno uživatelem 09:59:33 po 1,9 h** (plán byl 10 h, `-MaxMin 600`). 3 385 řádků po 2 s, 0× `NA`, 0 resetů desky, stav smyčky celou dobu `OFF`, kód DAC beze změny (24 899,35), uptime 39 357 → 46 160 s. Fáze `xr_ns` se v OFF nepočítá (nuly), proto z tohoto běhu fáze nejde.

| Veličina (po odříznutí první hodiny) | Výsledek |
|---|---|
| Kmitočet OCXO (kód drží) proti externímu GPSDO | střed **+0,118 ppb** (1. hodina +0,078, 2. hodina +0,118), σ jednotlivého vzorku 0,69 ppb |
| Trend kmitočtu | −0,015 ppb/h, **v rámci nejistoty** (hodinové střední má σ ≈ 0,1 ppb, protože šum je barevný s korelací ~100 s) |
| ADEV proti externímu GPSDO (jen data free-runu) | 1,75·10⁻¹⁰ @ 2 s, 2,2·10⁻¹⁰ @ 4 s, 4,1·10⁻¹⁰ @ 10 s, **vrchol 5,2·10⁻¹⁰ @ 20 s**, 4,5·10⁻¹⁰ @ 50 s, 2,6·10⁻¹⁰ @ 100 s, 1,5·10⁻¹⁰ @ 200 s, 6,8·10⁻¹¹ @ 500 s |
| Spektrum kmitočtu (rovnoměrná mřížka 2 s, po detrendu) | široké pásmo **T = 30–300 s** (cca 90 % výkonu), žádná jedna čára; nejvyšší maxima se mezi oběma běhy neopakují (91, 144, 90, 258, 40 s vs 157, 118, 185, 94, 63 s) |
| Korelace s teplotou OCXO / desky / FPGA / MCU, počtem družic, HDOP | všude \|r\| < 0,05 |

**Závěry (s výhradami):**
- ✅ **DAC držený na posledním kódu smyčky dává kmitočet shodný s GPS na ~0,1 ppb** (+0,12 ppb proti −0,004 ppb se zapnutou smyčkou). `gpsdo off` tedy kmitočet nerozhodí (kód zůstane).
- 🔑 **Vrchol ADEV na 20–50 s a jeho šířka (30–300 s) jsou STEJNÉ se smyčkou zapnutou i vypnutou** (rms 0,75 ppb v obou během, ADEV do 500 s v rámci šumu totožná). Tím je vyloučena smyčka (předfiltr τ/30 ≈ 33 s) jako příčina. **Nekoreluje s žádnou teplotou ani s GPS.** Zbývají ⬜ HYPOTÉZY: šum externího GPSDO (jeho vlastní smyčka), nebo měřicí řetězec čítače. Ověření: změřit týž signál druhým kanálem / proti třetí referenci, nebo vyměnit roli (OCXO měřit externím čítačem).
- ⚠️ **Drift OCXO z kódu DAC (5 ppb/den) tímto během NENÍ potvrzen ani vyvrácen:** při driftu 4,4 LSB/h × 3,4·10⁻¹¹ = 0,15 ppb/h by za 1,9 h vyšlo ~0,3 ppb, což je pod rozlišením (hodinové střední ±0,1 ppb, rozdíl dvou hodin ±0,17 ppb). Naměřených −0,015 ppb/h je s oběma hodnotami slučitelných. Dlouhé τ nad ~1 h potřebuje běh desítky hodin.
- ⚠️ **Firmwarové ADEV z `/api/stab` NEPLATÍ pro free-run ve vyšších τ:** ring stage obsahuje i data předchozí noci se smyčkou (n = 44 u τ 1000 s ≈ 12 h zpět), takže hodnoty od ~500 s (5,7·10⁻¹¹ @ 1000 s, 2,2·10⁻¹¹ @ 5000 s) míchají oba režimy. Pro čistý free-run bylo nutné restartovat ADEV (`CALC:AVER:CLE`) před startem.
- Zapisovač: 0 mezer > 3 s, skoků `seq_meas` jiných než 1–2 je 43 z 3 385 (1,3 %).

**Příčina hrbolu ADEV na 20–50 s (analýza nočního běhu, 2026-10-17, skript `bump_cause.py` v scratchpadu):** čítač měří OCXO proti externímu GPSDO (`y_c`), současně smyčka měří OCXO proti GPS 1PPS (`xr_ns`, `y_x` = sklon fáze v bloku). Po první hodině, 16 200 vzorků po 2 s, vše ve stavu LOCK:

| τ bloku | rozptyl `y_c` [ppb²] | rozptyl `y_x` [ppb²] | korelace |
|---|---|---|---|
| 20 s | 0,41 | 0,05 | 0,00 |
| 50 s | 0,17 | 0,011 | 0,02 |
| 100 s | 0,059 | 0,003 | 0,07 |
| 200 s | 0,019 | 0,001 | 0,10 |

ADEV čítače 0,60 / 0,45 / 0,29 ppb na 20 / 50 / 100 s proti 0,24 / 0,11 / 0,06 ppb u fáze OCXO–GPS (ta navíc obsahuje šum 1PPS, je to tedy horní mez OCXO).
- ✅ **Hrbol NENÍ v OCXO:** rozptyl kmitočtu OCXO proti GPS je na 20–100 s 8–20× menší než rozptyl, který vidí čítač, a obě řady spolu nekorelují (vysvětleno 0–1 %).
- ✅ **Hrbol NENÍ ze smyčky:** je i ve free-runu (viz výše) a smyčka s τ 1000 s na 20–100 s prakticky nemění kmitočet (korekce ~3·10⁻¹¹).
- ✅ **Není z teploty ani z GPS** (|r| < 0,05, viz výše).
- Zbývá **externí GPSDO** nebo **měřicí řetězec čítače**. Pro externí GPSDO mluví tvar: vrchol u 20–50 s a pokles ∝ 1/τ nad ním (3·10⁻¹⁰ @ 100 s, 1,5·10⁻¹⁰ @ 200 s, 6,8·10⁻¹¹ @ 500 s) odpovídá OCXO přisvorkovanému smyčkou o τ řádu desítek sekund k zašuměné 1PPS vlastního přijímače. ⬜ **HYPOTÉZA**, přímo neprokázaná.
- Test „fáze hrany vůči mřížce 10 ns" (kvantizační vzor čítače) vyšel nulově (vysvětleno 0,02 % rozptylu), ale fáze se integruje z šumných kmitočtů, takže **není průkazný**.
- ✅ **Rozhodující pokus proveden (2026-10-17 11:12–14:12, hradlo 0,25 s, smyčka LOCK, `docs/audit/data/2026-10-17_gate025b.csv`, každé měření `-OnNew`, 96 221 dotazů, 42 744 měření, 2× `NA`, 0 resetů; ADEV firmwaru vynulované na začátku):**

  | τ | hradlo 1 s (noc, CSV / firmware) | hradlo 1 s (free-run, CSV) | **hradlo 0,25 s (CSV / firmware)** |
  |---|---|---|---|
  | 10 s | 4,1·10⁻¹⁰ / 5,7·10⁻¹⁰ | 4,1·10⁻¹⁰ | **3,7·10⁻¹⁰ / 5,0·10⁻¹⁰** |
  | 20 s | 6·10⁻¹⁰ / 9·10⁻¹⁰ | 5,2·10⁻¹⁰ | **4,7·10⁻¹⁰ / 6,5·10⁻¹⁰** |
  | 50 s | — / 8·10⁻¹⁰ | 4,5·10⁻¹⁰ | **3,0·10⁻¹⁰ / 3,8·10⁻¹⁰** |
  | 100 s | — / 3·10⁻¹⁰ | 2,6·10⁻¹⁰ | **2,0·10⁻¹⁰ / 2,1·10⁻¹⁰** |

  Chyba čítače v časové oblasti by při 4× kratším hradle zvedla hrbol **4×** (na ~2·10⁻⁹). Hrbol místo toho zůstal na stejné úrovni (v rámci rozptylu krátkých běhů ±30 %). 🔑 **Hrbol tedy NENÍ kvantizace ani TDC čítače.** Spolu s předchozím (není v OCXO, není ze smyčky, nekoreluje s teplotou ani GPS) zbývá **externí GPSDO** jako měřený signál — ⬜ přímo nepotvrzeno (k tomu třetí reference, STATUS #312 bod 3).
  🔑 **Nezávislé měření stejného externího GPSDO (2018, uživatel, <https://ok2haz.ok2kld.cz/ok2haz/2018/12/citac-s-gps-normalem-3-stabilita/>):** GPSDO s u-blox NEO-7M a OCXO Trimble 34310-T, měřeno **proti Rb normálu** Efratom FRK-LLN-1A1B1A komparátorem Adret 4110, Stable32 (overlapping ADEV). Výsledek: **GPSDO při τ < 100 s ≈ 3·10⁻¹⁰**; volné OCXO (pevné ladicí napětí) 1–100 s ≈ 3·10⁻¹²; Rb za 1 den ≈ 1·10⁻¹². Integrátor smyčky R18 100 kΩ + C24 (100 µF, v názvu grafu 10 µF) = **časová konstanta ~10 s**; kolísání GPS hodin ~22 ns p-p. Srovnání s naším měřením: ADEV vůči externímu GPSDO 3–6·10⁻¹⁰ na 10–50 s (hradlo 1 s i 0,25 s) a 2·10⁻¹⁰ @ 100 s — **řádově i úrovní stejné jako publikovaná stabilita GPSDO**, a vrchol u 20–50 s odpovídá smyčce s τ ~ 10 s, která propouští šum 1PPS přijímače (~6 ns rms → ADEV ≈ √3·σx/τ ≈ 5·10⁻¹⁰ @ 20 s). Tím je hrbol s vysokou pravděpodobností **šum externího GPSDO**, ne čítače ani našeho OCXO. ✅ **Potvrzeno uživatelem (2026-10-17):** článek popisuje TENTÝŽ externí zdroj, který dnes měříme; měřicí přístroj (FPGA + STM32) je od té doby kompletně přepracovaný, **GPS přijímač je stejný typ (NEO-7M) na obou stranách**. ⚠️ Publikovaná tabulka hodnot po τ není v textu — jen „< 100 s ≈ 3·10⁻¹⁰". ✅ **Antény jsou ODDĚLENÉ (uživatel 2026-10-17):** externí GPSDO má malou anténu za oknem (vidí ~160° obzoru), náš přístroj kvalitní anténu na střeše (360°). Chyby GPS obou přijímačů jsou tedy nezávislé — kovariance (#312 krok 3) je použitelná a vyšší šum externího GPSDO (horší geometrie, víc multipathu) tomu odpovídá. Nevyřešeno zůstává **náš** přebytek fáze OCXO–GPS ~4·10⁻¹¹ @ 100 s (#312): tohle měření se týká externího zdroje, ne našeho OCXO.
  ⚠️ Vedlejší pozorování (✅ vysvětleno níže, odstavec „Anomálie ADEV @ 1 s“): ADEV firmwaru při τ = 1 s je s hradlem 0,25 s **3,9·10⁻¹¹**, s hradlem 1 s v noci 1,07·10⁻¹⁰. Vzorek 1 s se skládá ze 4 oken po sobě, takže by měl mít stejný šum TDC jako jedno 1s okno. ⬜ Ověřit (regresní blok FPGA se u delšího okna chová jinak? jiný počet hran v řetězu?).
  Fáze proti GPS (smyčka): σ hodinová 7–9 ns, TDEV 0,9 ns @ 20 s … 6 ns @ 2000 s; kód DAC +4,4 LSB za 2 h (1,6 ppb/den, s teplotou r = −0,64).
- ✅ **Opakování (2026-10-17 15:19–16:48, `docs/audit/data/2026-10-17_120min.csv`, hradlo 0,25 s, v0.20.1, smyčka LOCK celou dobu):** plánovaných 120 min, logger skončil po 88 min (21 209 řádků, 0 mezer > 3 s, 5 přeskočených `seq`, 0 resetů). Analýza po odříznutí prvních 20 min (`analyze_gpsdo_log.py … 1200`, n = 16 421):
  | τ | ADEV vůči externímu GPSDO (CSV) | ADEV firmwaru | TDEV fáze OCXO–GPS |
  |---|---|---|---|
  | 2 s | 8,4·10⁻¹¹ | 8,6·10⁻¹¹ | 3,0 ns |
  | 10 s | 2,3·10⁻¹⁰ | 3,5·10⁻¹⁰ | 1,0 ns |
  | 20 s | **2,9·10⁻¹⁰** | 4,5·10⁻¹⁰ | 0,8 ns |
  | 50 s | 2,0·10⁻¹⁰ | 3,1·10⁻¹⁰ | 0,9 ns |
  | 100 s | 1,8·10⁻¹⁰ | 2,0·10⁻¹⁰ | 1,4 ns |
  | 200 s | 1,0·10⁻¹⁰ | 1,1·10⁻¹⁰ | 2,2 ns |
  | 500 s | 3,1·10⁻¹¹ | 3,1·10⁻¹¹ | 3,1 ns |
  | 1000 s | 1,5·10⁻¹¹ | (n = 4) | 6,4 ns |
  Střední rozdíl proti externímu GPSDO **+0,006 ppb**, sklon −0,012 ppb/h (nulový); šum jednotlivého 0,25s měření sd 0,39 ppb. **Hrbol na 20–50 s je znovu přítomen na stejné úrovni** (3–5·10⁻¹⁰) jako ve všech předchozích bězích — potvrzuje, že jde o šum externího GPSDO (viz výše), ne o náš čítač ani OCXO. Na τ ≥ 1000 s nemá 88 min dostatek nezávislých bodů (n = 4–55), proto je tam jen orientační. Kód DAC +39 LSB/den (1,3 ppb/den), koreluje s teplotou desky (r = −0,70), s teplotou OCXO jen r = 0,16; tempco 0,16 ppb/°C je nespolehlivé (čas a teplota kolineární). ADEV firmwaru @ 1 s = 6,0·10⁻¹¹ (hradlo 0,25 s), v rozptylu ostatních běhů (viz níže).
- ✅ **„Anomálie“ ADEV @ 1 s (3,9·10⁻¹¹ s hradlem 0,25 s vs 1,07·10⁻¹⁰ s hradlem 1 s) VYSVĚTLENA — není to vlastnost hradla, ale dva náhodné snímky kolísající veličiny** (rozbor 2026-10-17, `scratchpad adev1s.py` nad sloupcem `sigma1s` všech CSV). Firmware počítá ADEV(1 s) jen z **posledních 120 vzorků** (stage 0, `n=120`), tedy z posledních 2 min, a měřený externí GPSDO má krátkodobý šum, který se v čase mění. Rozdělení `sigma1s` přes celý běh (po odříznutí 1/5 začátku):
  | běh | hradlo | p10 | medián | p90 |
  |---|---|---|---|---|
  | noc | 1 s | 3,4·10⁻¹¹ | **9,0·10⁻¹¹** | 1,8·10⁻¹⁰ |
  | gate025b | 0,25 s | 2,9·10⁻¹¹ | **8,0·10⁻¹¹** | 1,5·10⁻¹⁰ |
  | 120min | 0,25 s | 1,9·10⁻¹¹ | **4,1·10⁻¹¹** | 9,1·10⁻¹¹ |

  Mediány nocí (1 s) a gate025b (0,25 s) se liší jen o 12 % a obě porovnávané hodnoty leží uvnitř pásma p10–p90 svého běhu. Nezávisle z CSV (1s průměry ze 4 měření 0,25 s, klouzavé okno 120 s): gate025b medián 8,8·10⁻¹¹ (p10–p90 3,9·10⁻¹¹–1,8·10⁻¹⁰), 120min 7,0·10⁻¹¹ — týž rozptyl ×4–5, takže jde o vlastnost signálu, ne firmwaru. Z nočního CSV (hradlo 1 s) ADEV(1 s) spočítat nejde: logger četl po 2 s a `seq` skáče o 2 (každé druhé měření chybí). ⚠️ Zbytek: v běhu 120min je medián firmwaru 1,7× pod CSV (gate025b sedí) — pravděpodobně různé vážení (firmware vzorkovaný řádky loggeru, CSV okny), neověřeno.

### 2026-10-17 večer — dither DAC (v0.21.0/v0.21.1) a oprava FLL (#318): JEN SIMULACE, NA DESCE NEZMĚŘENO (CM7 v0.21.1, FPGA FW 0x0424 beze změny, IPC v26)

Žádné měření na desce — firmware v0.21.0/v0.21.1 zatím nebyl flashnut. Zapsáno proto, aby bylo jasné, **co se předpokládá a čím se to má ověřit**. Všechna čísla níže jsou ze simulace na hostu, ne z přístroje.

- **Dither DAC (STATUS #317, v0.21.0; výchozí 100 Hz od v0.21.1).** Zápis kódu smyčky dělá `DacTask` (5 ms takt, dělič 2 → 100 Hz; `dac dither 200` vrátí maximum). Simulace `dither_sim2.py` (RC 220 ms + 1 ms, K 3,4·10⁻¹¹/LSB):

  | dither | zvlnění Vc (p-p) | ADEV 0,25–10 s |
  |---|---|---|
  | 2 Hz (do v0.20.1) | 30·10⁻¹² | až 8·10⁻¹² |
  | 200 Hz | ≤ 0,8·10⁻¹² | ≤ 2,4·10⁻¹³ |
  | 200 Hz se zpožděnými zápisy 0–3 ms nebo 2 % vynechaných kroků | ≤ 5·10⁻¹² | ≤ 4·10⁻¹³ |
  | 100 Hz | HYPOTÉZA: skoro stejně jako 200 Hz (simulace 100 Hz „skoro stejně dobrá“, samostatná tabulka nebyla zapsána) | — |
  | 50 Hz | 2–3× horší než 200 Hz | — |

  🔑 Z dosavadních měření (ADEV firmwaru 8·10⁻¹¹ @ 2 s, hrbol 3–5·10⁻¹⁰ @ 20–50 s) **dither nic nevysvětluje ani nezmění**: jeho příspěvek je i při 2 Hz o 1–2 řády pod tím, co se měří (hrbol pochází z externího GPSDO, viz výše). Zlepšení se proto na ADEV čítače **nemusí vůbec projevit**; smysl má jako odstranění známého zdroje šumu v OCXO na 0,25–10 s.
- **Oprava FLL (STATUS #318, v0.21.1).** Příčina `gpsdo test` FAIL: K se měřilo i z malého kroku (−73 LSB), jehož očekávaná změna (1,2·10⁻⁹) je pod šumem rozdílu dvou 16s oken (~3,7·10⁻⁹). Host simulace `st_fix_sim.py` (model: sum hrany 30 ns, kvantizace 10 ns, Y0 −8,4·10⁻⁷, 12 000 s, 20 seedů):

  | skutečné K modelu | před opravou | po opravě |
  |---|---|---|
  | 3,1·10⁻¹¹ | 20/20 | 20/20 |
  | 1,7·10⁻¹¹ | 16/20 | **20/20** |
  | 2,3·10⁻¹¹ | 18/20 | **20/20** |
  | 1,2·10⁻¹⁰ | 20/20 | 20/20 |
  | 1,5·10⁻¹¹ | 0/20 | 0/20 (z principu: 52 429 LSB × 1,5·10⁻¹¹ < 8,4·10⁻⁷, rozsah EFC nestačí) |

  Zastavení při špatném znaménku K a při nedostatečném rozsahu: 10/10 před i po. ⚠️ Model šumu 1PPS (30 ns RMS) je **HYPOTÉZA** (datasheet NEO-7M v repu není); skutečný šum na desce lze odhadnout z rozptylu `e` ve smyčce (`status` → `GPSDO:` sawtooth rozptyly).
- **Co se musí změřit po flashi CM7 a power-cyklu (zapsat sem jako nový záznam):**
  1. `gpsdo test` → PASS (dosud na desce nikdy nebyl PASS).
  2. `dac dither test` → PASS; `dac` → dither 100 Hz, `kroky bez mutexu` řádově ≤ 1 %, `chyb I2C` neroste.
  3. `stats` → DacTask stackfree > 300 B, CPU ~1–2 %, halda ≥ 5 kB; `status` → `SEQ FPGA` díry 0.
  4. A/B ditheru `dac dither 2` / `100` / `200` po 30–60 min při stejném hradle 0,25 s (smyčka LOCK, kanál A = externí GPSDO, `analyze_gpsdo_log.py`): srovnávat **mediány `sigma1s`** (kolísá až 5×) a ADEV CSV @ 0,25–10 s; očekávání z modelu: rozdíl pod hranicí rozlišení dosavadních běhů (viz výše), pozitivní výsledek není zaručen.
  5. Chování FLL při rozběhu smyčky po power-cyklu (log `GPSDO:` stav, počet oken do LOCK; dosud LOCK za ~280 s při K modelu 3,1·10⁻¹¹).

### 2026-10-17 večer — v0.21.1 NA DESCE po power-cyklu: krátké ověření (uptime 581–594 s, smyčka LOCK od 288 s, FPGA FW 0x0424, IPC v26)

Nástroj: UART `uartq.ps1 -Port COM10` (`gpsdo test`, `dac dither test`, `dac`, `stats`, `status`). Jde o kontrolu funkce, **ne o měření stability** (A/B ditheru zatím neproběhlo).

| kontrola | výsledek |
|---|---|
| `version` | gpsdo-ui v0.21.1 |
| `gpsdo test` (#318) | ✅ **PASS (48 ms)** (do v0.21.0 FAIL pro K modelu 1,7·10⁻¹¹) |
| `dac dither test` | ✅ PASS |
| `dac` | dither 100 Hz (DacTask 5 ms), zápisů 28 459 za ~580 s (≈ 50/s: zapisuje se jen při změně kódu, viz zlomek povelu), **chyb I2C 0, readback nesedí 0, kroky bez mutexu 0, vynecháno po chybě 0** |
| `stats` | DacTask stackfree **808 B** z 1024, CPU 0 % (zaokrouhleno), IDLE 64 %, celkem 36 %, halda **5 744 B volných** (min-ever totéž) |
| smyčka po power-cyklu | LOCK po **288 s**, y +0,05…+0,15 ppb, fáze +3…+5 ns, kód 24 920,08 → 24 920,01, K 3,40·10⁻¹¹; 1PPS přijato 555 / odmítnuto 21 / díry 0 / roztržená čtení 0 |
| `status` | `SEQ FPGA` díry 0, `LTDC` podtečení 0/8022, `GPIO HLÍDAČ` 0 oprav, `I2C4` zdravá, CM4 alive, link FPGA OK (CRC err 0), hodiny 100M výpadků 0 |

- Pin VC (AIN0) 1 918 mV při kódu 24 920 (1 901 mV): rozdíl 17 mV odpovídá děliči/ADS (stejně jako dříve).
- ⚠️ `FPGA SAMOKONTROLA: zamítnuto oken: čítání 1` s hláškou „FPGA MĚŘÍ ŠPATNĚ" — 1 okno za 594 s, hodiny 0, délka okna 0. Změna FPGA ani počítání v této verzi nebyla; **příčina nevyšetřena** (HYPOTÉZA: jednorázové okno při náběhu, `FPGA REFERENCE: OCXO | externí vstup: bez hran` — na vstupu nemusel být signál). Ověřit, jestli počítadlo roste při připojeném signálu.
- ⬜ Zbývá: A/B ditheru 2 / 100 / 200 Hz (30–60 min každý), dlouhodobá stabilita smyčky, stack `FpgaTask` 628 B (beze změny proti dřívějšku).

### 2026-10-17 večer — #312: šum OCXO, nebo putování času GPS na 50–300 s? Rozbor stávajících logů (bez nového měření): NEROZHODNUTO

Nástroj `python tools/cov312.py <csv> <ořez_s> [τ,…]` (volitelně `W0`/`W1` = okno v hodinách). Data: noc (`2026-10-17_noc.csv`, 9 h po ořezu 1 h, hradlo 1 s), `gate025b` (2,7 h), `120min` (1,1 h); vše smyčka LOCK, τ smyčky 1000 s.

- **Krok 1, datasheet OCXO** (Bliley 6891 / NVG47A1282, `datasheets/`): krátkodobá stabilita jen **≤ 1·10⁻¹⁰ pro 0,1–1 s**, fázový šum −120 dBc/Hz @ 10 Hz … −145 dBc/Hz @ 10 kHz. **ADEV na 10–1000 s v datasheetu není** → krok nic nerozhoduje.
- **Podlaha záznamu fáze:** ADEV fáze OCXO–GPS na 10 s je 5,3·10⁻¹⁰ = bílý fázový šum σx ≈ 3,0 ns, což je **kvantizace hrubého čítače 10 ns / √12 = 2,9 ns**, ne GPS. Extrapolace této podlahy (∝ 1/τ) dává 5,3·10⁻¹¹ @ 100 s, 2,6·10⁻¹¹ @ 200 s, 1,05·10⁻¹¹ @ 500 s; naměřeno 6,9 / 4,0 / 1,7·10⁻¹¹ → **přebytek 4,4 / 3,0 / 1,3·10⁻¹¹** (shoda s dřívějším „~4·10⁻¹¹ @ 100 s“).
- **Krok 2, vazba na stav GPS** (ADEV fáze po hodinách noci proti počtu družic, HDOP, teplotě OCXO): ADEV fáze @ 100 s je v hodinách 5,3–7,9·10⁻¹¹, @ 50 s 9,7–14,9·10⁻¹¹; korelace s počtem družic r = −0,37, s HDOP −0,06, s teplotou OCXO −0,08 (n = 9, nevýznamné). ⚠️ Rozsah podmínek byl malý (družic 8,9–10,7, HDOP 0,80–1,01), takže data **nemají sílu** cokoli vyloučit.
- **Krok 3, kovariance s nezávislým zdrojem.** Model: čítač měří externí GPSDO (E) proti OCXO (O): y_c = y_E − y_O; smyčka měří OCXO proti naší GPS (G1): y_x = y_O − y_G1; cov(y_c, y_x) = −var(O) − cov(E, G1), kde cov(E, G1) ≥ 0 (společné chyby GPS) → **var(O) ≤ −cov** (horní mez, ne odhad). Výsledek z noci (jednotky 10⁻¹¹):

  | τ | n párů | ADEV čítače | ADEV fáze | korelace | −cov [ppb²] ± SE | **OCXO ≤ (95 %)** |
  |---|---|---|---|---|---|---|
  | 10 s | 3 238 | 46,5 | 52,7 | −0,007 | 0,0017 ± 0,0030 | 8,7 |
  | 20 s | 1 618 | 60,7 | 26,6 | −0,003 | 0,0004 ± 0,0053 | 10,4 |
  | 50 s | 646 | 44,6 | 13,0 | −0,050 | 0,0029 ± 0,0031 | 9,5 |
  | 100 s | 322 | 29,1 | 6,9 | +0,005 | −0,0001 ± 0,0015 | 5,3 |
  | 200 s | 160 | 16,7 | 4,0 | +0,052 | −0,0003 ± 0,0005 | 2,6 |
  | 500 s | 63 | 6,2 | 1,7 | −0,173 | 0,0002 ± 0,0002 | 2,3 |

  Proti samotné fázi (6,9·10⁻¹¹ @ 100 s, 13·10⁻¹¹ @ 50 s, 26·10⁻¹¹ @ 20 s) mez zpřesňuje jen málo (5,3 / 9,5 / 10,4·10⁻¹¹); nevylučuje přitom přebytek 4,4·10⁻¹¹ @ 100 s.
- 🔴 **Mez je nespolehlivá, protože se kovariance mezi okny nechová jako šum s uvedenou SE.** Hodinová okna noci na 50 s: −cov = 0,0010 / 0,0119 / 0,0045 / 0,0028 / **−0,0194** (poslední má opačné znaménko, než model připouští; SE oken 0,002–0,007), na 100 s od −0,0056 do +0,0059. Běh `gate025b` (den, 11–14 h) dává významnou zápornou kovarianci **−0,021 ± 0,006 @ 50 s (r = −0,25)** a −0,0096 ± 0,0028 @ 100 s (r = −0,25), zatímco `120min` (15–16:30) a noc nulovou. Možné příčiny (⬜ HYPOTÉZY, nerozlišeno): společná chyba GPS u obou přijímačů přes den (ionosféra, družicové hodiny; to by tvrdilo „GPS"), nestacionární šum čítače (první hodina `gate025b` má ADEV čítače 118·10⁻¹¹ @ 50 s proti 29–45·10⁻¹¹ jinde), nebo skutečný šum OCXO jen v některých podmínkách.
- **Závěr: z dosavadních dat NELZE rozhodnout, jestli přebytek fáze 3–4·10⁻¹¹ na 100–200 s pochází z OCXO, nebo z GPS.** Horní mez OCXO: **< 1·10⁻¹⁰ pro 10–50 s, ≲ 5·10⁻¹¹ @ 100 s, ≲ 3·10⁻¹¹ @ 200–500 s** (noc, orientačně); nic lepšího než tato mez se z těchto dat vytáhnout nedá. `gpsdo_tau_advise` proto **zůstává dolní mezí** (nepřepisuje se na doporučení).
- **Co by rozhodlo (nové měření, zbývá z #312):** (a) nezávislá reference lepší než GPS na 100 s (Rb nebo druhé stabilní OCXO) a free-run OCXO proti ní ≥ 2 h; (b) několik dnů/nocí se širším rozsahem podmínek GPS pro kovarianci (jedna noc nestačí, okna spolu nesouhlasí); (c) smyčka s jiným τ (200 / 1000 / 5000 s, ≥ 6 h každá); (d) jiná anténa. Levnější než (a) a (d): zapsat fázi OCXO–GPS i při `gpsdo off`, čímž vznikne free-run záznam vůči GPS bez vlivu smyčky; není to rozhodující, ale odpadne podezření, že přebytek dělá smyčka. ✅ **Naimplementováno v0.22.0** (pasivní fázový monitor v OFF, `xr_ns` se sčítá dál, DAC se neřídí; `.text` +112 B), 🟡 na desce krátce ověřeno (2026-10-17, v0.22.0): v OFF se `xr_ns` mění (4 / −2 / 0 ns po 0 / 20 / 40 s), `gpsdo on` naskočí do FLL; free-run měření ≥ 2 h zatím neproběhlo; STATUS #319 = po měření rozhodnout, zda to nevrátit. Postup měření: `gpsdo off`, logger `tools/gpsdo_log.ps1` ≥ 2 h (sloupec `gp_xr_ns` je v OFF platný, `.phase.csv` ale bere fázi jen v LOCK), pak `cov312.py`/HDEV nad `gp_xr_ns`.

### 2026-10-18 — 10h běh smyčky GPSDO (noc 17.→18. 10., LOCK celých 10 h): rozšířená analýza

Podrobně `docs/audit/2026-10-18_noc10h-analyza.md` (nástroj `tools/analyze_noc_ext.py`, data `docs/audit/data/2026-10-18_noc10h.csv.gz`). Podmínky: hradlo 0,25 s, τ smyčky 1000 s, vstup A = externí GPSDO, **běh začal 16 min po power-cyklu**; 0 resetů, 1PPS odmítnuto 0, družic 8–12, HDOP 0,7–1,3.

| Veličina | Výsledek |
|---|---|
| offset čítač − externí GPSDO | **3·10⁻¹³ ± 2,3·10⁻¹²** (hodinové průměry −0,008…+0,012 ppb, bez trendu) |
| ADEV / MDEV fáze OCXO−GPS (po opravě kvantizace σx = 2,89 ns) | 1,1·10⁻¹¹ / 5,7·10⁻¹² @ 1000 s; 6,1·10⁻¹² / 3,2·10⁻¹² @ 2048 s; 3,5·10⁻¹² / 1,7·10⁻¹² @ 4096 s; přebytek nad kvantizací MDEV 3,0·10⁻¹¹ @ 128 s |
| reprodukovatelnost proti předchozí noci | MDEV @ 64…4096 s shodné na ~10–15 % |
| TIE proti 1PPS | sd 9,5 ns, pp 63 ns, 94 % času v ±20 ns, MTIE 43 ns @ 1000 s, 58 ns @ 10 000 s |
| hrbol ADEV vůči externímu GPSDO | 7,9·10⁻¹⁰ @ 20 s; spektrum: plošina 2–30 mHz, pokles ×21 za oktávu nad 30 mHz (roh ≈ 25 mHz), žádné tóny |
| firmware ADEV vs numpy | τ ≤ 50 s shoda 1,00; 100–5000 s do 1–12 % |
| filtr izolovaných odlehlých vzorků | 8 z 35 457 (0,023 %), 0,4–0,6 ppb |
| DAC kód | 24 918 → 24 926 (0,76 LSB/h = 26 ppt/h), ⚠️ obsahuje zahřívání po power-cyklu; předchozí noc 6 LSB/h |

- **#312:** křížové spektrum y_čítač × y_fáze nemá v žádném pásmu průkaznou koherenci (≤ 0,07 při mezi 0,21/0,05). Za hypotézy „přebytek je z OCXO“ je očekávaná koherence² 0,001–0,004, k detekci by bylo potřeba 2–7 dní souvislého záznamu → **z uspořádání s externím GPSDO se #312 nerozhodne, rozhodne Rb (M2, #313)**. Kladné korelace z `cov312.py` (+0,16…+0,28 @ 100–500 s) spektrum nepotvrzuje. Lekce L-0174.
- **Holdover:** horní mez samotného lineárního driftu (7,2·10⁻¹⁵/s) = 47 ns po 1 h, 1,7 µs po 6 h; skutečné stárnutí potřebuje záznam po ≥ 2–3 dnech bez power-cyklu.

### 2026-10-18 — záložní baterie CR2032: úbytek diody a pin VBAT (STATUS #322, krok 1)

Multimetrem (≥ 10 MΩ) při běžném provozu (smyčka v LOCK, můstek VBAT zapnutý trvale, firmware v0.27.2):

| Veličina | Hodnota |
|---|---|
| napětí baterie (BT1+ proti GND) | **3,046 V** |
| napětí na pinu 8 CPU (VBAT) | **2,971 V** |
| úbytek na diodě D3 (B0520LW) | **0,0756 V** (3,046 − 2,971 = 0,075 V; shoda) |
| firmware (`sensors`, `/api/state`) | 2 984–2 992 mV, průměr 2 988 mV, min 2 972 / max 3 004 mV za 12,5 h; **o 17 mV víc než multimetr** (0,6 %, v toleranci ADC) |

- **Důsledek:** baterie není ~3,1–3,2 V (odhad z 2026‑10‑18), ale **3,046 V** — nová CR2032 má ~3,2–3,3 V, tahle už je odebraná, ale s velkou rezervou nad prahem 2,70 V. Korekce na baterii vůči ADC = 3 046 − 2 988 = **+58 mV** (dioda 75,6 mV mínus chyba ADC 17 mV) → korekce vůči ADC by byla 58 mV. **Výchozí `vbat_corr_mv` v CAL3 je ale PŘESNĚ ÚBYTEK DIODY 75,6 mV** (rozhodnutí uživatele): 17 mV je chyba ADC (zisk/offset ADC3 + VREFBUF), jiná veličina, kterou jedno srovnání s multimetrem nerozliší na offset a zisk; zobrazená baterie je proto o ~17 mV vyšší než multimetr (3,064 vs 3,046 V). Vyladit na multimetr = 58,6 mV v okně Kalibrace (krok 0,1 mV).
- ⚠️ V_F platí pro proud dnešního trvale zapnutého můstku. Po v0.29.0 teče proud jen při převodu (1× / 600 s), takže střední zátěž článku klesne; V_F při převodu zůstane stejné — **zopakovat měření po flashi** (kap. 2.4 postupu #322) a doplnit µA‑metrem proud před/po. Pozor: 75,6 mV je u Schottkyho diody B0520LW hluboko pod hodnotami při mA — ukazuje to na proud řádu jednotek až desítek µA (⬜ HYPOTÉZA, rozhodne µA‑metr).

### 2026-10-18 — HW testy dnešního firmwaru (v0.27.2) a nálezy

Podrobně `docs/audit/2026-10-18_hw-testy.md` (19 testů, 17 ✅, 2 nálezy: chybějící glyf na displeji, překrývání dávek historie na webu). Po 12,5 h: žádný reset, smyčka LOCK, DAC 2,08 M zápisů bez chyby, síť 22/22 PASS, selftest 16/16.

### 2026-10-18 — Šum jednoho okna čítače: lze posunout poslední platnou číslici? (data: 10h log + 12 min A/B na v0.30.0)

Nástroje: `tools/counter_floor_check.py` (10h log), `tools/ab_capture.py` + `tools/ab_analyze.py` (A a B současně z `/api/state`, data `docs/audit/data/2026-10-18_AB_capture_v0.30.0.npz`).

| τ [s] | model 105 ps (√3·σ/τ) | A, 10h log (zdroj: externí GPSDO) | A, 12 min | B (bez regrese) | A−B |
|---|---|---|---|---|---|
| 0,25 | 7,3·10⁻¹⁰ | **1,85·10⁻¹⁰** | 2,7·10⁻¹⁰ | 8,7·10⁻¹⁰ | 8,5·10⁻¹⁰ |
| 0,5 | 3,6·10⁻¹⁰ | 1,49·10⁻¹⁰ | 2,0·10⁻¹⁰ | 4,6·10⁻¹⁰ | 4,4·10⁻¹⁰ |
| 1 | 1,8·10⁻¹⁰ | 1,34·10⁻¹⁰ | 2,0·10⁻¹⁰ | 2,8·10⁻¹⁰ | 2,3·10⁻¹⁰ |
| 2 | 9,1·10⁻¹¹ | 1,53·10⁻¹⁰ | 2,6·10⁻¹⁰ | 2,8·10⁻¹⁰ | 1,3·10⁻¹⁰ |
| 10 | 1,8·10⁻¹¹ | 5,2·10⁻¹⁰ | 8,7·10⁻¹⁰ | 8,7·10⁻¹⁰ | **3,8·10⁻¹¹** |

- ⚠️ **Poznámka 2026-10-18 odpoledne:** původní tvrzení „na této hladině měří čítač hlavně zdroj“ POTVRDILO měření s kanálem A na vlastní referenci (ADEV 0,25 s = 2,6·10⁻¹¹, záznam „A = vlastní reference“ níže). Mezitím zvažovaná domněnka, že se přístroj od 2026-10-08 (5,4-6,5·10⁻¹¹) zhoršil, neplatí.
- **Kanál A je ≥ 4× lepší než model** (ADEV 0,25 s ≤ 1,85·10⁻¹⁰ i s šumem zdroje proti 7,3·10⁻¹⁰; jádro rozdělení rozdílů sousedních oken 3,4·10⁻¹¹, tedy σ značky ≲ 5 ps). Model 105 ps platí pro **dvoubodový odhad**; A má regresi (`REGR: použita`), proto je model pro A konzervativní už od 2026‑10‑08 (komentář u `MP_TDC_PS`).
- **Kanál B model potvrzuje:** 8,7·10⁻¹⁰ @ 0,25 s ⇒ σ značky ≈ 125 ps (B regresi nemá). A−B je proto omezeno kanálem B, ne A, a **nerozliší** šum A pod ~10⁻⁹.
- **Křivka A není 1/τ**, ale plochá ~1,4–1,9·10⁻¹⁰ do 2 s, pak hrbol externího GPSDO (kap. 4 analýzy 10h běhu) ⇒ **na této hladině měří čítač hlavně zdroj**. 8 % oken má skoky > 6× jádro a nesou 94 % rozptylu rozdílů (rovnoměrně přes `seq mod 4`, takže nejde o sekundovou hranu); 10 396 skoků, žádný společný s B (B je na zjištění příliš hlučné — nelze rozhodnout, zda zdroj, nebo TDC).
- **A−B @ 10 s = 3,8·10⁻¹¹** (společný šum zdroje se odečetl: A samotné má 8,7·10⁻¹⁰) — to je **horní mez šumu přístroje** na 10 s a je zhruba 2× model pro dva kanály (√2·1,8·10⁻¹¹) ⇒ na dlouhých τ model nepodhodnocuje.
- **Závěr k poslední platné číslici:** pro TENTO zdroj ji nelze posunout — omezuje ji šum zdroje, ne přístroj. U 10 MHz a hradla 0,25 s vychází hranice na 10⁻² Hz v modelu i při 4× lepším rozlišení (1,85·10⁻³ Hz leží mezi 10⁻³ a 10⁻²). Posunout by ji šlo jen: delším hradlem / průměrováním (√N), nebo tišším zdrojem (Rb, druhé OCXO), na kterém by se prokázal skutečný práh A. **Nic jsem v kódu neměnil.**

### 2026-10-18 odpoledne — v0.30.5 NA DESCE po power-cyklu: kontrola SPI 12,5 MHz, stav, dither, rozbor FPGA vs STM

Nástroj: UART `uartq.ps1 -Port COM10`. CM7 v0.30.5 (`17b1193`), FPGA FW 0x0424 (build 2026-10-16 16:00 UTC, git `bb5adfb+`), IPC v28. `Reset: power-on`, měřeno v uptime 40-520 s.

| kontrola | výsledek |
|---|---|
| `spi` | SPI2 kernel 200 MHz, **SCK 12,5 MHz**, CRC err **0** (stupně 100/50/25/12,5/6,25/3,125 MHz), po power-cyklu totéž |
| `status` | `SEQ FPGA` díry 0 / resync 0, `CITANI HRAN` 0/0, hodiny 100M výpadků 0, `LTDC` podtečení 1 / 6144 flipů (náběh), `DMA2D` chyb 0, CM4 alive, síť UP 10.0.0.106, `GPIO HLÍDAČ` 0 oprav |
| `selftest` | 16/16 PASS |
| `stats` | CPU 33-39 %, **UiTask 29-31 %** (IDLE 66 %); stacky: IDLE 428 B, DacTask 808 B, FpgaTask 1 232 B, I2C4 1 068 B; halda 21 616 B volných (48 kB od v0.30.3) |
| GPSDO | LOCK po ~2 min od startu, y −0,09…+0,01 ppb, fáze −23…+2 ns, kód ≈ 24 926; `tau min.` zatím bez dat (potřeba 180 bodů LOCK) |
| `FPGA SAMOKONTROLA` | zamítnuto oken: čtení **1**, hodiny 0, délka 0. Hodnota se za 5+ minut **nezměnila** (jednorázové okno při náběhu; příznak „FPGA MĚŘÍ ŠPATNĚ“ svítí jen proto, že počítadlo není nula) |
| `tdc` | největší bin A 174 ps / B 144 ps, za koncem řetězu 0, maxtap 248; **rezerva řetězu A 16 tapů (OK), B 14 tapů (POZOR, práh 15)**, stejné jako před power-cyklem |
| `regr` | oken 7 342, použito 6 715, **zamítnuto 627 (8,5 %)**; přírůstek za ~2 min: 1 792 oken / 85 zamítnuto (4,7 %). V dlouhém běhu téhož dne dopoledne 2,5 % (2 174 / 87 628). σ(f_regr − f_2pt) **0,104 ppb** (dříve 0,46 ppb) |

- 🔑 **Dither DAC jede na 100 Hz, ověřeno měřením.** `status` hlásí 100 Hz; zápisy se počítají jen při změně kódu, takže jejich počet závisí na zlomku povelu. Při zlomku 0,07 (kód 24 926,07) je očekávání 2·0,07·f ≈ 14/s při 100 Hz a 28/s při 200 Hz; naměřeno **≈ 17,5 zápisu/s** (27 707 → 27 777 za ~4 s). 200 Hz je vyloučeno, 100 Hz sedí. Starší poznámky „dither 200 Hz“ v CLAUDE.md, `ad5693.h`, `freertos_shared.h`, `gpsdo.h` a `HW_REFERENCE.md` opraveny (L-0180). **Text hlášky `dac dither <Hz>` stále říká „vychozi 200“** (`freertos_task_uart.c:2314`): je to chování, ne komentář, čeká na samostatný `fix:` commit.
- ⚠️ **ZÁVĚR TOHOTO ODSTAVCE BYL VYVRÁCEN měřením téhož dne (záznam „A = vlastní reference“ níže):** přístroj se nezhoršil, dnešních 1,85·10⁻¹⁰ je šum zdroje. Odstavec ponechán jako historie úvahy.
- 🔴 **Historická měření BEZ ditheru a co z nich plyne.** AD5693R byl osazen až 2026-10-16, takže měření z 2026-10-08 až 10-11 (FW 0x041A, regrese, 40 654 oken, `docs/TDC_MATEMATIKA.md` kap. 10) proběhla bez DAC a bez ditheru. Dither proto **nemůže být zdrojem šumu značky ~105 ps** (ten je z doby před DAC) ani limitu ADEV 5,4-6,5·10⁻¹¹ z té doby. Zároveň platí: tehdejší ADEV(0,25 s) s regresí **5,4-6,5·10⁻¹¹ a nula zamítnutých oken** je ~3× lepší než dnešních **1,85·10⁻¹⁰ a 2,5-8,5 % zamítnutých** (10h log). Zdroj byl v obou případech externí GPSDO, takže dnešní hodnota **není podlaha zdroje**, jak se dosud psalo (záznam „Šum jednoho okna čítače“ výše); zdroj nebo přístroj už jednou dosáhl 3× lepšího výsledku. Dither takto velký rozdíl vysvětlit nemůže: simulace dává zvlnění Vc ≤ 0,8·10⁻¹² p-p při 200 Hz, a degradace je soustředěná do vzácných oken (8 % oken nese 94 % rozptylu), ne rozložená rovnoměrně jako šum DAC. Rozdíly mezi tehdy a dnes, které se **nerozlišily**: (a) δ = +813 ppb tehdy proti δ ≈ 0 za zamčené smyčky (fáze hrany vůči hodinám stojí a případná vada TDC trefuje stále stejný kód); (b) bitstream 0x041A proti 0x0424; (c) smyčka + DAC zapnuté. ⬜ HYPOTÉZA: bod (a). Neověřeno.
- ⚠️ **Zamítání regrese 4,7-8,5 % vs 2,5 %:** mez 5·10⁻⁸ odpovídá 12,5 ns v délce okna (1 takt = 10 ns = 40 ppb, 2 takty = 80 ppb). Příčina **neznámá**. Kontrola: do `regr` přidat rozdělení |f_regr/f₂ − 1| zamítnutých oken (jen STM, bez FPGA).
- **Navržený rozhodující pokus (zatím NEPROVEDEN):** smyčku vypnout (`gpsdo off`) a ručním `dac <kód>` naladit OCXO na δ ≈ +0,8 ppm (±24 000 LSB od zámku; 1 LSB = 3,4·10⁻¹¹), čímž se zopakují podmínky z 2026-10-08, a to s dnešním firmwarem, bez ditheru (pevný kód). Pokud zamítání klesne k 0 a ADEV(0,25 s) k ~6·10⁻¹¹, příčinou je zamčená (stojící) fáze, ne DAC ani dither. Ze stejné dvojice lze udělat A/B „dither 100 Hz × pevný kód“. ⚠️ `dac <kód>` **ukládá kód do flash** (obnova po startu): po pokusu `gpsdo on` a počkat na LOCK (kód se při zámku znovu uloží do 10 min), do té doby nedělat power-cyklus.
- ⚠️ **UiTask 29-31 % CPU** proti 5 % v `docs/audit/2026-10-18_optimalizace.md` (měřeno v0.30.1, uptime 5 476 s). Neprověřeno, co se změnilo (jiné okno, animace, glyph akcelerace po power-cyklu?).
- **Vstup CH_A je externí GPSDO** (uživatel 2026-10-18), časovou základnu tvoří vnitřní OCXO, takže v měřeném ADEV je šum **obou** oscilátorů + přístroje. Test oddělení: na CH_A zapojit **vlastní 10 MHz** téhož OCXO, které taktuje Si5356. Společná časová základna vykrátí šum OCXO a zbude podlaha přístroje. ⚠️ Při přesně shodném kmitočtu je δ = 0 a fáze stojí (jako O10), takže **podlaha vyjde optimisticky**. Lepší varianta: 10 MHz ze **Si5356 (okno GENERÁTOR, CLK2…7)** s malým známým offsetem (řádu 10⁻⁶): tentýž OCXO, ale δ ≠ 0; přidává jitter PLL Si5356 (ps).
- **Co přístroj sám NEUVIDÍ:** Vc čte ADS1115 po 125 µV a 2-10 Hz. Citlivost OCXO 4,4 Hz/V = 4,4·10⁻¹³ na µV; 5,4·10⁻¹¹ odpovídá ~120 µV šumu na Vc, tedy jednomu kroku ADS. Rušení Vc a napájení OCXO na této úrovni z `sensors`/`adcraw`/datalogu poznat nejde; vyžaduje osciloskop (1 mV/div, AC), zvukovou kartu nebo lineární napájení OCXO.
- **Rozbor FPGA vs STM** (co jde přesunout, odhady, pořadí kroků): `docs/audit/2026-10-18_fpga-vs-stm.md`. FPGA zatím beze změny (rozhodnutí uživatele 2026-10-18).

### 2026-10-18 odpoledne - 120 min: kanál A = VLASTNÍ reference (tentýž OCXO, který taktuje Si5356), kanál B = externí GPSDO

Podmínky: v0.30.5, FPGA FW 0x0424, hradlo 0,25 s, kanál A, `INIT:CONT 1`; smyčka GPSDO v LOCK celou dobu (kód 24 923-24 929, dither 100 Hz), OCXO 48,2-48,5 °C, FPGA 37,9-38,3 °C, deska 31-32 °C, družic 10-12. Start 14:31 (uptime 583 s, LOCK 302 s), konec 16:31. Data: `docs/audit/data/2026-10-18_A-vlastni_B-externi.csv` (+ `.stab.jsonl`, `.events.txt`, `.phase.csv`, `.log`); 28 487 řádků, diry 62 míst / 314 měření (1,09 %), resetů přístroje 0, `SEQ FPGA` díry 0. Nástroj `python tools/ab_analyze_csv.py <csv> [orez_s]` (nový). Logger `gpsdo_log.ps1` má nově sloupec `freq_b_hz`.

| τ [s] | ADEV A (vlastní ref.) | ADEV B (ext. GPSDO, 2 body) | ADEV A-B |
|---|---|---|---|
| 0,25 | **2,85·10⁻¹¹** | 9,35·10⁻¹⁰ | 9,35·10⁻¹⁰ |
| 1 | 1,60·10⁻¹¹ | 2,67·10⁻¹⁰ | 2,67·10⁻¹⁰ |
| 10 | 5,85·10⁻¹² | 4,19·10⁻¹⁰ | 4,19·10⁻¹⁰ |
| 100 | 1,46·10⁻¹² | 2,76·10⁻¹⁰ | 2,76·10⁻¹⁰ |
| 400 | 6,1·10⁻¹³ | 8,8·10⁻¹¹ | 8,8·10⁻¹¹ |

(Po ořezu prvních 10 min: A 2,6·10⁻¹¹ @ 0,25 s, 5,6·10⁻¹² @ 10 s.)

- 🔑 **Podlaha přístroje (s regresí) je ≲ 2,9·10⁻¹¹ @ 0,25 s a klesá přibližně jako τ^-0,5 až τ^-0,6 (bílý FM), 6·10⁻¹³ @ 400 s.** To je ~25× lépe než dvoubodový model (7,3·10⁻¹⁰) a ~6,5× lépe než 1,85·10⁻¹⁰ z 10h logu s externím zdrojem. Dnešní ADEV 1,85·10⁻¹⁰ je proto **šum zdroje (externí GPSDO × OCXO, Vc, smyčka)**, ne přístroj. Hypotéza o 3× zhoršení proti 2026-10-08 (5,4-6,5·10⁻¹¹) je vyvrácena. ⚠️ Podlaha je **optimistická**: společná časová základna a δ = 0 znamenají stojící fázi, kde se deterministická chyba kvantizace vykrátí (O10); s nesoudělným signálem bude horší. Spodní mez pro „přístroj na 10 MHz z vlastního OCXO“ je ale 2,9·10⁻¹¹.
- **Kanál B** (externí GPSDO proti OCXO, bez regrese): ADEV 9,35·10⁻¹⁰ @ 0,25 s ⇒ σ značky ≈ 135 ps (shodně s dřívějšími ~125 ps); hrbol 4,2-5,7·10⁻¹⁰ @ 10-20 s je externí GPSDO (7,9·10⁻¹⁰ @ 20 s dříve). A-B = B, protože A je o řád tišší. Korelace skoků A a B 0,016, souběžný skok 1.
- **Skoky v A** (> 6× jádro 2,4·10⁻¹¹): 143 z 28 487 (0,50 % oken), nesou 39 % rozptylu rozdílů (po ořezu 10 min: 89, 0,34 %, 29 %). Medián velikosti 2,6·10⁻¹⁰ = 66 ps v délce okna, tedy **nejde o skluz o takt (10 ns)**. Časově: **0-10 min záznamu 2,25 % (54 skoků), 10-20 min 0, dál 0,04-0,92 % po desetiminutovkách** (shluky 20-40 a 60-70 min). Teplota FPGA, OCXO ani desky nemá trend (FPGA 38,3 → 37,9 °C), takže to není ohřev. Příčina neznámá.
- 🔴 **Zamítání regrese (`regr` po záznamu):** rámců 144 579 (čítač počítá rámce při 20 Hz, ne okna: 144 579 / 7 200 s ≈ 20/s), použito 144 333, **zamítnuto 246 = 0,17 %**; σ(f_regr − f_2pt) = 0,216 ppb. V prvních minutách po power-cyklu to bylo 4,6-8,5 %, dopoledne za dlouhého běhu 2,5 %. Zamítání je tedy jev začátku běhu (PŘEKONÁNO) **OPRAVENO téhož dne (záznam „A = externí GPSDO, GPSDO OFF“ níže): zamítání NEZÁVISÍ na stáří běhu, ale na zdroji.** Vysoké hodnoty 4,6-8,5 % byly naměřeny s externím GPSDO na kanálu A, nízkých 0,17 % s vlastní referencí; s externím zdrojem je i po 78 min běhu 3,1 %.
- `FPGA SAMOKONTROLA`: čtení 2, hodiny 0, délka okna 1; **po celých 120 min beze změny** (hodnoty z doby přepojování vstupu, ne z měření). `CITANI HRAN` 0/0, `LTDC` podtečení 2 / 25 687 flipů, TDC rezerva A 16 / B 15 tapů, `TDC` bin A 163 ps / B 138 ps, bez rekalibrace.
- ⚠️ **NÁLEZ v `errlog` (před záznamem, ne z něj):** `#5044 CRASH HardFault 240000` těsně po `#5043 BOOT duvod=SW res` (14:02:52), po záznamu `#5042 kanál B → A` (13:46, uptime 6 721 s). Tedy jeden HardFault s následným softwarovým resetem na dříve běžícím firmwaru (verze v tu chvíli neznámá, mohlo jít o přechod na v0.30.5). Příčina nevyšetřena (`status` ukáže CFSR/BFAR jen po resetu a černou skříňku čte při startu). Sledovat; při opakování zapsat jako nález.

**Co z toho plyne a co dál:**
1. Přístroj sám (s regresí) není hrdlem měření stability 10 MHz: podlaha ≲ 2,9·10⁻¹¹ @ 0,25 s. Omezuje zdroj a OCXO. Dalším krokem je tedy **pokus na kanálu A s externím GPSDO: smyčka `gpsdo off` + pevný kód (bez ditheru a řízení), 60-120 min.** Pokud se ADEV(0,25 s) zlepší z 1,85·10⁻¹⁰ směrem k ~5·10⁻¹¹, šum do OCXO přidává smyčka nebo dither (Vc); jinak je to zdroj. (Pozor: `dac <kód>` ukládá kód do flash; po pokusu `gpsdo on` a počkat na LOCK.)
2. ~~Zamítání regrese je jev prvních minut po zapnutí~~ (vyvráceno, viz záznam „A = externí GPSDO, GPSDO OFF“): v `regr` přidat rozdělení |f_regr/f₂ − 1| a zjistit, čím se externí signál liší (úroveň, strmost, šum hrany na vstupu).
3. Rozbor FPGA vs STM (`docs/audit/2026-10-18_fpga-vs-stm.md`) tím neztrácí ani nezískává: omezení přesnosti nebylo v FPGA matematice.

### 2026-10-18 večer - 60 min: kanál A = EXTERNÍ GPSDO (týž zdroj jako B), GPSDO (smyčka) OFF, pevný kód DAC 24 927, bez ditheru

Podmínky: v0.30.5, FW 0x0424, hradlo 0,25 s, kanál A, smyčka OFF celých 60 min (stav 4 690-4 718 s od vypnutí), DAC beze změny (zápisů 455 160 před i po), OCXO 48,2-48,5 °C, deska 31,4-31,6 °C, družic 9-12. Odchylka OCXO proti externímu GPSDO +1,1·10⁻¹⁰ (volná, bez drift trendu do 1 h). Start 16:55, konec 17:55. Data: `docs/audit/data/2026-10-18_A-ext_gpsdo-OFF.csv` (+ `.log`, `.events.txt`, `.phase.csv`, `.stab.jsonl`). V CSV chybí 8,0 % oken (163 míst, 1 153 měření; logger hlásil „NEODPOVIDA“, FPGA: `SEQ FPGA` díra 1), proto jsou v ADEV doplněna lineární interpolací; dříve 1,1 %. Nástroje `ab_analyze_csv.py`, `ab_phase_jumps.py` (nový).

| τ [s] | ADEV A | ADEV B | ADEV A-B |
|---|---|---|---|
| 0,25 | **1,41·10⁻¹⁰** | 8,26·10⁻¹⁰ | 8,1·10⁻¹⁰ |
| 1 | 1,04·10⁻¹⁰ | 2,53·10⁻¹⁰ | 2,5·10⁻¹⁰ |
| 10 | 3,57·10⁻¹⁰ | 3,59·10⁻¹⁰ | 7,8·10⁻¹¹ |
| 20 | 4,62·10⁻¹⁰ | 4,61·10⁻¹⁰ | 5,5·10⁻¹¹ |
| 100 | 2,48·10⁻¹⁰ | 2,55·10⁻¹⁰ | 2,7·10⁻¹¹ |
| 400 | 9,2·10⁻¹¹ | 8,7·10⁻¹¹ | 1,9·10⁻¹¹ |

Srovnání ADEV A při 0,25 s: **vlastní reference 2,85·10⁻¹¹** | **externí zdroj, smyčka OFF 1,41·10⁻¹⁰** | externí zdroj, smyčka LOCK (10h log) 1,85·10⁻¹⁰.

- ⚠️ **UPŘESNĚNO (otázka uživatele „jak to, že je lepší bez řízení DAC a ditheru?“):** níže uvedený závěr je příliš silný. Výsledek (OFF 1,41 vs LOCK 1,85·10⁻¹⁰; skoky 4,9 vs 8,3 %) JE v souladu s tím, že smyčka/DAC/dither šum přidává (~25 %), jen nejde o řízený A/B (různá denní doba, stav GPS). Další náznak: v 10h logu (`tools/ab_dither_activity.py`, aktivita ditheru z zlomku kódu) mají okna s největší aktivitou ditheru (zlomek ~0,5) 10,5 % skoků proti 6-8 % jinde, ale zlomek kódu se vyvíjí pomalým driftem kódu (0,76 LSB/h), takže je zaměněn s denní dobou. Simulace přitom dává zvlnění od ditheru ~100× pod touto úrovní. **Nerozhodnuto, nutný A/B po blocích** (viz „Další kroky“).
- 🔑 **Šum A je dán převážně SIGNÁLEM zdroje (podíl smyčky/ditheru je nejvýše desítky procent, neprokázáno).** Vypnutí smyčky (bez ditheru, pevný kód) zlepší ADEV(0,25 s) jen z 1,85 na 1,41·10⁻¹⁰ (~25 %, přitom mezi běhy se liší i denní doba a stav GPS), zatímco přepnutí A na vlastní referenci ho srazí 5× na 2,85·10⁻¹¹. Zbylý rozdíl je vlastnost externího signálu (nebo jeho zapojení: kabel, úroveň, strmost, šum hrany ve vstupu čítače). Hrbol 3,6-4,6·10⁻¹⁰ @ 10-20 s je v A i B stejný (shoda 0,01-0,2 %), tedy společný zdroj; A-B ho odečte (5,5·10⁻¹¹ @ 20 s).
- **Skoky v A** (> 6× jádro, jádro 4,7·10⁻¹¹): **647 z 13 084 oken (4,9 %) nese 91 % rozptylu**, medián 6,1·10⁻¹⁰ = 153 ps v délce okna. Proti vlastní referenci 0,5 % / 39 % je to 10× víc. 10h log s externím zdrojem a zapnutou smyčkou: 8,3 % oken. V B se skoky nejsou vidět (jádro B 1,15·10⁻⁹), korelace dA/dB 0,18, souběžných skoků 0.
- **Zamítání regrese** (`regr`): rámců 74 767, zamítnuto 2 321 = **3,1 %**, σ(f_regr − f_2pt) 0,41 ppb (vlastní reference: 0,17 %, 0,22 ppb). Tím se **opravuje** předchozí závěr („jev začátku běhu“): zamítání je dáno zdrojem. Bylo 4,6-8,5 % v prvních minutách s externím zdrojem (kanál A), 2,5 % dopoledne s externím zdrojem, 3,1 % teď, 0,17 % s vlastní referencí.
- **Fáze hrany:** skoky jsou rozloženy po celé periodě hodin 100 MHz, 2,0-7,6 % v 20 binech (jeden záznam 39,7 period hodin, 10h log 6 period: 7,5-11 %); χ² 60 a 117 při 19 stupních volnosti je zvýšeno autokorelací shluků, ale **žádná fáze nemá skoky nulové a žádná je nenese většinově**. Hypotéza „vada TDC v konkrétním kódu při stojící fázi“ tím odpadá; hlavním činitelem je signál.
- **Co je tedy hrdlem při měření externího zdroje:** kvalita (zapojení) vstupu a zdroje, ne matematika FPGA, ne smyčka GPSDO, ne dither, ne Vc (Vc se na A s vlastní referencí neprojeví, s externí jen přes OCXO a to v tomto pokusu bylo statické).
- `FPGA SAMOKONTROLA` po testu beze změny proti začátku (čtení 3, délka okna 2), `CITANI HRAN` 0/0, rezerva TDC A 16 / B 15 tapů, `LTDC` podtečení 2 / 35 245.

**Další kroky (návrh):** (1) zjistit, čím se vstup od externího zdroje liší od vlastní reference: osciloskop na pinu 25 (úroveň 0-2,86 V? strmost, šum v okolí prahu ~1,4 V), délka a zakončení kabelu (120 Ω proti GND), zkušebně druhý zdroj nebo jiná cesta signálu; (2) řízený A/B zapnuto/vypnuto po blocích 20 min pro čistý vliv smyčky (v tomto pokusu jsou běhy z různé denní doby); (3) do `regr` přidat rozdělení |f_regr/f₂ − 1|.

### 2026-10-18 21:35 - 2026-10-19 15:44 - 18 h automaticky: faze 1 (smycka/DAC/dither, 4 h) + faze 2 (volny beh OCXO, 14 h)

Skript `tools\dlouhe_mereni.ps1` (bez zasahu), firmware v0.30.6, FW FPGA 0x0424, hradlo 0,25 s, kanal A i B = externi GPSDO (okenni antena), nase NEO-7M na strese (viz pamet `gps-antennas-setup`). Preflight: cekani na LOCK 301 s, pak plan od 21:41:32. Data `docs/audit/data/2026-10-18_2135_dlouhe*` (+ `_faze1_smycka.csv`, `_faze2_gps.csv`, `.raw.csv`, `.blocks.csv`, `.snap.csv`, `.sats.jsonl`, ...). Vyhodnoceni `tools/dlouhe_analyza.py <predpona>`, `tools/dlouhe_hodiny.py <predpona>`. **Predchozi pokus z 21:21 (2026-10-18_2121_dlouhe) byl smazan** - pral se s mou zkusebni instanci (viz `.POZNAMKY`, pozn. v run.log).

**Zdravi behu (212 snimku po 5 min):** 0 resetu pristroje, `CRC err` 0, `SEQ FPGA` 1 diry za 18 h (1 mereni), `FPGA SAMOKONTROLA` beze zmeny (cteni 1, hodiny 0, delka 0), `errlog` beze zmeny (5058), TDC rezerva A 16 / B 14 tapu po celou dobu, heap 21 616 B, nejmensi stacky nezmeneny (FpgaTask 1 288, DacTask 808, IDLE 428 B), CPU 7-25 %, `LTDC` podteceni 44 -> 195 (za 18 h), regrese kumulativne zamitnuto 2,9 % (32 937 ze 1,13 M ramcu). NEO-7M: `gps ack` TMODE2 NAK (0/2), GNSS ACK (GPS-only), TP5/MSG ACK po celou dobu.

**FAZE 1 - vliv smycky/DAC/ditheru (12 bloku po 20 min, od kazdeho 5 min pryc, 3 cykly OFF/D200/D100/D2):**

| blok | ADEV(0,25 s) prumer ± sd (3 bloky) | skoky > 6x jadro [%] | zamitnuti regrese [%] |
|---|---|---|---|
| OFF (smycka vypnuta, pevny kod) | 1,06·10⁻¹⁰ ± 5,8·10⁻¹¹ | 3,5 ± 4,1 | 1,2 ± 1,0 |
| D200 (smycka ON, dither 200 Hz) | 1,10·10⁻¹⁰ ± 6,8·10⁻¹¹ | 4,5 ± 4,0 | 1,5 ± 1,3 |
| D100 (vychozi) | 2,04·10⁻¹⁰ ± 5,0·10⁻¹¹ | 9,1 ± 4,4 | 5,0 ± 3,8 |
| D2 | 1,01·10⁻¹⁰ ± 4,3·10⁻¹¹ | 2,9 ± 2,1 | 1,5 ± 1,1 |

- 🔑 **Zadny pruvazny vliv smycky, DAC ani ditheru.** Rozdil mezi bloky stejneho typu je stejne velky jako rozdil mezi typy: tri bloky OFF maji 8,5 / 17,2 / 6,2·10⁻¹¹ (faktor 2,8), D100 ve trech cyklech 2,2 / 1,5 / 2,5·10⁻¹⁰, a poradi D200 < D100 < D2 se neprojevuje (D200 a D2 se blizi OFF, D100 je nejhorsi). Souhrnne ON (9 bloku) proti OFF (3): 1,38 vs 1,06·10⁻¹⁰, pomer 1,3, t = 0,8, **nevyznamne**; shoduje se s rozdilem OFF/LOCK z 2026-10-18 (1,41 vs 1,85), takze nelze vylouèit maly vliv ≲ 30 %, ale prokazan neni.
- 🔑 **Smycka ON dosahne podlahy pristroje:** blok D200 ve 00:41 (smycka ON, dither 200 Hz) dal ADEV(0,25 s) **3,2·10⁻¹¹** a 0 % skoku, tedy podlahu pristroje (2,9·10⁻¹¹ z mereni s vlastni referenci). Smycka+DAC+dither tedy sum nad 3·10⁻¹¹ pri 0,25 s nepridavaji; zbytek dela zdroj. Rozptyl 20minutovych bloku (3,2·10⁻¹¹ az 2,5·10⁻¹⁰) je vlastnost externiho GPSDO s okenni antenou, ne smycky.

**FAZE 2 - volne bezici OCXO (smycka OFF 14,04 h; kod DAC pevny 24 941; chybi 0,08 % oken):**
- **OCXO vs nase strešni GPS (`xr_ns`, ADEV fáze R):** 4,4·10⁻⁹ @ 1 s, 5,3·10⁻¹⁰ @ 10 s, 6,8·10⁻¹¹ @ 100 s, 3,9·10⁻¹¹ @ 200 s, 1,8·10⁻¹¹ @ 500 s, **1,2·10⁻¹¹ @ 1000 s**, 1,4·10⁻¹¹ @ 2000 s, 2,5·10⁻¹¹ @ 5000 s. Do 10 s je to kvantizace hrubeho citace 1PPS (10 ns/√12: 5,0·10⁻⁹ @ 1 s, 5,0·10⁻¹⁰ @ 10 s); nad 100 s je nad podlahou prebytek 4,6·10⁻¹¹ @ 100 s, 3,0·10⁻¹¹ @ 200 s, 1,5·10⁻¹¹ @ 500 s, 1,1·10⁻¹¹ @ 1000 s - tedy **horni mez pro OCXO + nasi GPS dohromady** (nerozlisuje je). Stejne jako v 10h behu se smyckou (1,1·10⁻¹¹ @ 1000 s): volny OCXO neomezuje smycku s τ 1000 s.
- **Externi GPSDO (A):** ADEV A = ADEV D (ext. proti nasi GPS) s presnosti na ~1 % pro vsechna τ ≤ 2000 s (napr. 100 s: 7,13·10⁻¹⁰ vs 7,14·10⁻¹⁰): OCXO a nase GPS jsou o rad tissi nez externi zdroj. Externi GPSDO ma ADEV 3,6·10⁻¹⁰ @ 1 s, 2,4·10⁻⁹ @ 10 s (vcetne epizody, viz nize), 7,1·10⁻¹⁰ @ 100 s, 7,8·10⁻¹¹ @ 1000 s, **o rad horsi nez OCXO proti nasi strese**. Hodinove ADEV A(10 s) 4,2-9,8·10⁻¹⁰ (hodiny 1 a 3 s 13 % skoku, ostatni 4-8 %); R(10 s) po celou dobu 4,7-5,6·10⁻¹⁰ (= podlaha). **Epizoda 13:41-14:41:** A(1 s) 1,2·10⁻⁹, A(10 s) 8,9·10⁻⁹, D(10 s) 8,9·10⁻⁹, zatimco R(10 s) normalni (4,7·10⁻¹⁰), nase GPS 11 druzic: porucha na strane externiho GPSDO.
- ⚠️ **Tri-rohy klobouk (3CH) OCXO / ext / nase GPS NELZE spolehlive vyhodnotit:** ADEV(A) ≈ ADEV(D) a rozdil a² - d² (kterym OCXO vychazi) je pod statistickou chybou cca 10 % z a² (napr. 1000 s: OCXO 1,1·10⁻¹¹ z rozdilu 8·10⁻²³ pri chybe 7·10⁻²²), takze cisla v tabulce `dlouhe_analyza.py` pro OCXO (4·10⁻¹¹ @ 1 s ... 1,1·10⁻¹¹ @ 1000 s) maji **horsi presnost nez jejich hodnota**. Externi GPSDO s okenni antenou je na to prilis hlucny; #312 (OCXO nebo GPS) se timto neroziresi (potrebuje Rb / druhe OCXO / lepsi referenci, #313).
- **OCXO volny beh (stari, #303):** frekvence proti nasi GPS z +0,13·10⁻¹⁰ klesla na −3,05·10⁻¹⁰ za 13 h: **drift −2,1·10⁻¹¹ / h (−5,1·10⁻¹⁰ / den)**, zpomaluje (−0,45·10⁻¹⁰/h v 1. hod., ~−0,2·10⁻¹⁰/h v 8.-12. hod.). Teplota OCXO 47,93 → 47,35 °C (−0,6 °C), deska 30,7 → 30,0 °C. Fázový posun za 13,9 h −8 968 ns; rezidua po kvadratickem fitu sd 89 ns, p-p 500 ns. (Obsahuje dosah po power-cyklu/retrace a tepelny drift - rozlisit nelze.) Holdover odhad: po 1 h od zamceni ~38 ns jen z driftu, dalsi z poc. offsetu.

**Zaver:** (1) smycka GPSDO, DAC ani dither nejsou hrdlem (TODO #326 uzavren: zadny prukazny vliv, pri smycce ON se dosahne podlahy pristroje); (2) mereni externiho zdroje s okennim GPS je omezeno JEHO sumem (hrbol 4-10·10⁻¹⁰ @ 10-20 s, epizody az 9·10⁻⁹), ne prístrojem; (3) OCXO + nase strecha dava ≲ 1,1·10⁻¹¹ @ 1000 s a ≲ 4,6·10⁻¹¹ @ 100 s (horni mez) - bez zmeny proti drivejsi; (4) #312 se bez nezavisle reference neroziresi; (5) NEO-7M nepodporuje survey-in ani GLONASS (NAK), dalsi zlepseni GPS strany tedy znamena timing prijimac (NEO-M8T / ZED-F9T).

### 2026-10-19 21:50 - 2026-10-20 07:50 - 10 h automaticky: UBX-CFG-NAV5 dynModel Stationary vs Portable (nase GPS, strecha)

Skript `tools\dlouhe_mereni.ps1 -Phase1Min 0 -Phase2Min 600 -Phase2Names "NS,NA,NS,NA"` (bez zasahu), firmware **v0.30.7** (nova UART `gps nav5 stationary|auto`, UBX-CFG-NAV5 mask=dyn), smycka OFF po celou dobu (volny beh OCXO, kod DAC pevny), kanal A i B = externi GPSDO. 4 bloky po 2,5 h (NS=Stationary, NA=Portable, 2 cykly kvuli dennimu/nocnimu casu), kazdy vyhodnocen od +15 min. Data `docs/audit/data/2026-10-19_nav5*`. Vyhodnoceni `tools/dlouhe_nav5.py <predpona>` (novy).

🔴 **Na rozdil od CFG-TMODE2 (survey-in, 2026-10-18) NEO-7M CFG-NAV5 PRIJIMA** (`gps ack` -> `NAV5 n/0 ACK`, 0 NAK za celou noc) - je to standardni zprava, kterou podporuje kazdy u-blox prijimac, ne jen timing rada.
⚠️ **Prvni pokus mel chybu (off-by-one v parsovani UART retezce, `RxBuffer[10]` misto `[9]`): obe volby `stationary`/`auto` posilaly Portable.** Odhaleno zivym testem na HW pred spustenim noci (`gps ack` ukazal dynModel Portable u obou), opraveno pred buildem v0.30.7, znovu overeno ACK pro oba smery pred startem.

Zdravi behu: 0 resetu, `CRC err` 0, `SEQ FPGA` 0 diry, `errlog` beze zmeny (5073), TDC rezerva A 16 / B 14 tapu, `NAV5 8/0 ACK` na konci. Pocet druzic/HDOP srovnatelny mezi bloky (9,7-11,4 druzic, HDOP 0,81-0,87) - neni silne zkresleno geometrii.

**ADEV faze R (OCXO proti nasi GPS, `xr_ns`), geometricky prumer 2 bloku kazdeho typu:**

| tau [s] | NS (Stationary) | NA (Portable) | pomer NS/NA |
|---|---|---|---|
| 1 | 4,79·10⁻⁹ | 5,00·10⁻⁹ | 0,96 |
| 10 | 5,38·10⁻¹⁰ | 5,04·10⁻¹⁰ | 1,07 |
| 20 | 2,68·10⁻¹⁰ | 2,68·10⁻¹⁰ | 1,00 |
| 100 | 7,07·10⁻¹¹ | 6,88·10⁻¹¹ | 1,03 |
| 200 | 4,24·10⁻¹¹ | 3,96·10⁻¹¹ | 1,07 |
| 500 | 1,82·10⁻¹¹ | 2,21·10⁻¹¹ | 0,82 |
| 1000 | 1,29·10⁻¹¹ | 2,01·10⁻¹¹ | 0,64 |

- 🔴 **Zadne zlepseni pri tau 1-200 s** (pomer 0,96-1,07, v sumu). Podle N8UR melo zlepseni prijit prave od tau ~30 s vyse - zde se neprojevilo.
- ⚠️ **Pri tau 500-1000 s vypada NS lepe (pomer 0,82 a 0,64), ale NENI to prukazne:** rozptyl MEZI dvema bloky STEJNEHO typu je stejne velky jako rozdil mezi typy (NS: 1,68 a 0,99·10⁻¹¹ pri 1000 s, faktor 1,7; NA: 2,93 a 1,38·10⁻¹¹, faktor 2,1). S n=2 na typ a timto rozptylem nejde rozhodnout, jestli je to NAV5, nebo noc/den (bloky NS byly v 21:50 a 02:50, NA v 00:20 a 05:20).
- **Zaver: NAV5 Stationary na teto desce a s timto GPS nema prokazatelny vliv.** Prinos pro #312/#326 zadny; zustava vraceno na vychozi (Portable). Pripadne dalsi overeni (vic cyklu, delsi bloky) ma nizkou prioritu - externi GPSDO (okenni antena) je o rad hlucnejsi nez obe varianty NAV5 dohromady (viz 2026-10-18/19 zaznam: ADEV A ~2-9·10⁻¹⁰ proti R ~5·10⁻¹⁰-1,3·10⁻¹¹).
- 🔧 **Nalezena a opravena chyba parsovani UART retezce** (viz vyse) - lekce zapsana jako L-0181.

## 3. Stav kalibrací

| Co | Stav | Hodnota / poznámka | Datum |
|---|---|---|---|
| **TDC code density** (A, B) | ✅ automaticky po startu, po výpadku hodin a při FAIL znovu (L-0153); po power-cyklu A:1 B:1 | 100 MHz: rezerva řetězu 268 tapů ~6–8 % (~250 tapů na 10 ns) — sledovat `tdc` → `max tap` i v teple | 2026-10-11 |
| **Skew A−B** | ✅ změřeno, korekce netřeba | 0,010 ns (100 MHz, stejný signál na A i B) | 2026-10-11 |
| **Offset reference proti GPSDO** | ✅ dolaďuje smyčka GPSDO (AD5693R osazen 2026-10-16) | po power-cyklu 2026-10-18: LOCK za ~2 min, y −0,09…+0,01 ppb, kód ≈ 24 926 (1 902 mV); původní −0,84 ppm z 2026-10-11 už neplatí | 2026-10-18 |
| **ADS AIN0 Vc** (gain_vc) | ⬜ výchozí 2,5 | AD5693R je osazen a Vc budí smyčka GPSDO. `status`: pin VC (AIN0) 1 919 mV při kódu DAC 1 902 mV (+17 mV, stejně jako 2026-10-17) — rozdíl je dělič/offset ADS, ne chyba DAC; kalibraci proti multimetru zatím nikdo neprovedl | 2026-10-18 |
| **ADS AIN1 VBUS** (gain_vbus) | ⬜ výchozí 20,608 (R54 = 5k1) | čte 11,99 V; ověřit multimetrem | — |
| **ADS AIN2 +3V3** (gain_12v) | ⬜ výchozí 2,0 | ověřit multimetrem | — |
| **ADS AIN3 +5V** (gain_5v) | ⬜ výchozí 2,0 (R58 = 10k) | čte 5,03–5,17 V; **zkalibrovat** v průvodci (Kalibrace → PRŮVODCE) | — |
| **AD8307 RF** | ➖ na desce není (RF_LEVEL_HW_PRESENT 0) | — | — |

### 2026-10-27/28 noc — A/B bitstreamů `796c009` (nový) vs `bd20e77` (starý), stejná denní doba 22:00–05:00 (CM7/CM4 v0.42.0)

Kanál A = externí GPSDO, hradlo 0,25 s, `tools/ab_noc.ps1`. Medián ADEV z 30min úseků: τ=1 s 1,26·10⁻¹⁰ vs 1,19·10⁻¹⁰ (poměr 1,06), τ=16 s 6,16·10⁻¹⁰ vs 5,73·10⁻¹⁰ (1,07), τ=64 s 3,63·10⁻¹⁰ vs 3,79·10⁻¹⁰ (0,96). Rozdíl neprokázán, bitstream `796c009` zůstává. Podrobnosti `docs/AB_TEST_BITSTREAM.md`.

### A−B kanálů jednoho zdroje: vývoj v0.42.2 – v0.42.10 (2026-10-28, FPGA FW 0x0508 `796c009`, hradlo 0,25 s)

Oba vstupy na tomtéž zdroji 10 MHz, A−B = rozdíl délek oken [ps]. Podrobnosti v L-0246 a L-0247.

| verze | změna | σ(A−B) MAD | ocas |
|---|---|---|---|
| v0.42.2 | B jen dvoubodově | 172–211 ps | – |
| v0.42.3 | LSQ i pro B (L-0246) | 13–15 ps | 67 z 3 179 oken > 1 ns (až 146 ns) |
| v0.42.5 | kontrola těžiště segmentu, vadné okno dvoubodově | 19,3 ps | 0 z 2 402 > 1 ns, 12 > 500 ps |
| v0.42.7 – v0.42.10 | oprava X0 o 64·k (v0.42.7), vadný segment z LSQ vyřazen (v0.42.8) | viz noc níže | páry ±6,4/12,8/19,2 µs zmizely (v0.42.10) |

### 2026-10-28 21:26 – 2026-10-29 13:26 — noční A−B 16 h (CM7 v0.42.10, FW 0x0508 `796c009`, `tools/ab_night.py`)

Data `noc_logs/ab_20261028_2126/` (ab.csv, cykly.csv, x0_velke.csv, uart.log, report.txt). Bez restartu, bez výpadku webu, CRC err 0, díry SEQ 1.

- **A−B:** 229 152 oken za 15,92 h, medián 0, **σ(MAD) 14,8 ps** (po hodinách 11,9–22,2 ps). |A−B| > 100 / 200 / 500 ps / 1 ns / 1 µs: **14 660 / 7 836 / 873 / 186 / 0**. Největší +14,7 ns (seq 103180), pak −7,9 ns a ~5,7 ns (3×). Z oken > 200 ps jde 6 992 na vrub B, 844 A.
- **Čítače desky:** vadné segmenty A **0** z 2 284 123, B **170 695 (7,5 %)**, z toho SX mimo meze 163 110; okna B bez LSQ 14 762 (6,4 %); X0 B opraveno 3 593, bez opravy 0.
- 🔑 **Podpis vady (rozbor 993 zapsaných X0 a 766 snímků segmentů):** chybné X0 je VŽDY správné X0 s vynulovanými bity **6, 7, 32, 33** indexu (libovolná kombinace, žádný bit se nenastaví, 993/993). SX má týž podpis: −k·2³² při přesných spodních 32 b (747 z 913 snímků mimo meze), jinak −64·k. Chyba tedy vzniká v indexu značky (`xm`/FIFO) před DSP, viz L-0247.
- **Ocas vs epizody (po 10 min):** r(oken s vadným segmentem, > 200 ps) = 0,75; r(opravy X0, > 500 ps) = 0,26; okna bez LSQ s ocasem > 500 ps nekorelují. Okna 3–14,7 ns jsou většinou osamocená (B), původ nerozhodnut.
- **Následek:** v0.42.11 opravuje SX z mezí / podle X0 a zpřísňuje mez těžiště na 800 ps (⬜ neověřeno na HW). Zkušební bitstream se stejnými zdroji a jiným rozmístěním (F0a) má ukázat, jestli se množina vadných bitů přesune.

### 2026-10-29 20:52 – 2026-10-30 05:15 — noční A−B 8,4 h (CM7 v0.42.11, zkušební bitstream F0a `b3fbc32`, `tools/ab_night.py`)

Data `noc_logs/ab_20261029_2052/` (test ukončen v 05:15 podle plánu, report jen nad kopií). Bez restartu (uptime 32 115 s), CRC err 0, díry SEQ 1, teplotní rekalibrace TDC 0, rezerva řetězu A 25 / B 28 tapů beze změny.

- **Kanál B je s F0a čistý:** vadné segmenty B **0** z 1 202 360 (s `796c009` 7,5 %), SX mimo meze 0, opravy X0 0, okna B z LSQ 100 %. Vada indexu B (L-0247) je tedy vlastnost rozmístění, ne zdrojů.
- **Kanál A:** 257 vadných segmentů (289 oken), **všechny v jediném shluku 21:43–22:03**, potom 7 h nic. Podpis: příznak bubliny 0x04 / 0x0C, N o ~1 700 nižší, SX v mezích, X0 v pořádku, T0 končí na …0001 / …8001 (hrana na hraně taktu). Okna s vadným segmentem se vyřadila z LSQ a A−B v té hodině bylo nejlepší z noci (σ 16,3 ps, 0 oken > 500 ps).
- **A−B:** 120 490 oken, medián −1,0 ps, **σ(MAD) 20,8 ps** (po hodinách 16,3–31,1), > 100 / 200 / 500 ps / 1 ns / 1 µs: 8 039 / 3 216 / 800 / 104 / 0, největší ±1,8 ns. Ocas je **úniku šumu zdroje** (L-0248): r(log neklid zdroje, log σ(A−B)) = 0,79, f_A − f_B = 0,721·(f_A(n) − f_A(n−1)) s r = 0,83, korelace f_B(n) s f_A(n−1) = 0,68. Po odečtení úniku (`leakfix.py`) σ 18,0 ps a > 1 ns jen **4** (místo 104). Nejhorší hodina 3 (23:52–00:52, σ 31 ps, 87 > 1 ns) = nejneklidnější zdroj.
- **Vedlejší nález:** smyčka GPSDO byla celou noc `OFF - FLL divergovala` (od ~145 s po startu, kód DAC držen 24 944). A−B se tím neovlivní (společná reference), ale reference běžela volně; k prověření zvlášť.
- **Následek:** v0.42.12 (mřížka oken a párování podle buňky, L-0248) k nahrání; očekávané σ(A−B) ~18 ps bez ocasu nad 1 ns. ⬜ neověřeno na HW.

### 2026-10-30 05:34 – 14:34 — A−B 9 h (CM7 v0.42.12, bitstream F0a `b3fbc32`, `tools/ab_night.py --hours 9`)

Data `noc_logs/ab_20261030_0534/`, rozbor nad kopií. Flash v0.42.12 + power-cyklus, bez restartu (uptime 31 924 s), CRC err 0, díry SEQ 1, teplotní rekalibrace TDC 0, rezerva řetězu A 25 / B 28 tapů. Smyčka GPSDO tentokrát **LOCK celou dobu** (FLL → LOCK za ~180 s, kód 24 950–24 960); divergence FLL z předchozí noci se neopakovala.

- **Párování oken je souběžné (L-0248 opraveno):** korelace f_B(n) s f_A(n+j): j = −1 **0,097**, j = 0 **0,877**, j = +1 0,095 (předtím maximum v j = −1, 0,68). Únik f_A − f_B = **0,045**·(f_A(n) − f_A(n−1)), r = 0,20 (předtím 0,721, r = 0,83); odečet úniku už σ téměř nemění (23,7 → 23,0 ps).
- **A−B:** 129 563 oken za 9,00 h, medián 0,0 ps, **σ(MAD) 23,7 ps** (po hodinách 16,3–35,6). |A−B| > 100 / 200 / 500 ps / 1 ns / 1 µs: **7 370 / 916 / 0 / 0 / 0**, největší ±336 ps. Předchozí noc (v0.42.11) 8 039 / 3 216 / **800 / 104** / 0 při 120 490 oknech – ocas nad 500 ps zmizel celý.
- **Proč σ nekleslo:** zdroj byl přes den neklidnější (minut se sd f_A > 1,6 ppb: 150 proti 74 v noci; 09:40–09:55 skoky −17 a −21 ppb = hodina 4, σ 35,6 ps). Ve stejném pásmu neklidu je σ stejné nebo lepší: 7,8 / 10,4 / 16,3 / 25,6 / 52,3 ps proti 13,0 / 14,8 / 17,8 / 26,7 / 53,0 ps (`srcnoise.py`).
- **Zbylá závislost na neklidu zdroje** (r = 0,75) už není únik mezi okny (korelace se sousedním oknem ~0,1). ⚠️ HYPOTÉZA: při rychle se měnící fázi signálu proti taktu 100 MHz projde hrana za okno víc kódů TDC a rozdíl nelinearit A a B se neprůměruje; ověřit σ(A−B) po minutách proti periodě průchodu fáze (`beat.py`) a proti neklidu zdroje odděleně.
- **Kanál B:** vadné segmenty **0** z 1 274 450, SX mimo meze 0, opravy X0 0, okna B z LSQ 100 % (127 700 / 127 700).
- **Kanál A:** 280 vadných segmentů (371 oken), opět **jediný shluk ~06:55–07:15** (předtím 21:43–22:03), jinak nic. Hodina se shlukem měla σ 20,8 ps a 0 oken > 500 ps, tj. vyřazení z LSQ funguje. Spouštěč shluku nerozhodnut (fáze zdroje proti taktu v té době nijak zvláštní, perioda průchodu 4–130 s).
- **Závěr:** v0.42.12 ✅ ověřeno na HW po power-cyklu (L-0248). Otevřené: shluk vadných segmentů A s F0a, zbylá závislost σ(A−B) na neklidu zdroje.

#### Rozbor téhož běhu: σ(A−B) je rozdíl nelinearit TDC A a B v závislosti na fázi proti taktu (2026-10-30)

Skripty `docs/audit/data/2026-10-29_kanal_b/tdc_sweep.py`, `tdc_dfit.py`, `tdc_dtvar.py`, `tdc_dcmp.py` (`python -I <skript> <ab.csv>`). Pojmy: **sweep** = o kolik se fáze hrany proti taktu 100 MHz posune za okno (= (f − 10 MHz)/10 MHz · 0,25 s, z průměru A a B); **D(φ)** = rozdíl časových chyb TDC A a B jako funkce fáze hrany v periodě taktu 10 ns.

- **Proč to jde vidět:** 10 MHz je soudělné se 100 MHz, takže všechny hrany jednoho segmentu (25 ms) padnou na tutéž fázi a chyba TDC se uvnitř segmentu nezprůměruje. Chyba okna LSQ je pak ≈ D(φ na konci) − D(φ na začátku), tedy ≈ D′(φ)·sweep.
- **σ(A−B) roste lineárně se sweep** (`tdc_sweep.py`): |sweep| < 25 ps → **4,4 ps**, 25–50 → 7,4, 50–100 → 11,9, 100–200 → 20,8, 200–400 → 38,5, 400–800 → 71, 800–1600 → 126 ps; nad 1,6 ns klesá (92 ps, sweep pokryje větší část periody). Zakřivení kmitočtu mezi okny (FM) má při pevném sweep jen malý vliv.
- **D je deterministická funkce fáze:** fit D Fourierovou řadou (20 harmonických, perioda 10 ns) v blocích 600 oken, fáze rekonstruovaná součtem sweep: na lichých oknech, která fit neviděl, vysvětlí **R² 0,56** (σ 23,7 → 15,1 ps); se zamíchanou fází R² −0,02. Delší bloky fungují hůř (2 400 oken R² 0,13), protože rekonstruovaná fáze bloudí, ne protože by se D měnila.
- **D je stabilní:** tvar z 215 bloků po 2,5 min zarovnaný kruhovým posunem: korelace se společným vzorem medián **0,99** (kontrola se zamíchanou fází 0,52). Vzor má **p-p 230 ps, rms 61 ps**, je hladký (hlavně 1. až 4. harmonická periody 10 ns), největší strmost 0,18 ps/ps.
- **Tentýž tvar i v noci 2026-10-29** (týž bitstream F0a, mezi běhy power-cyklus a nová kalibrace TDC): korelace tvarů **0,976**, amplituda v noci 1,34× (noc ještě s únikem L-0248).
- **Co to znamená:** σ(A−B) dnes neměří šum TDC (ten je při klidné fázi ~4 ps), ale **deterministickou fázově závislou chybu ±100 ps**, kterou kalibrace hustotou kódů neodstraní. Z A−B je vidět jen rozdíl A − B; jak se dělí mezi kanály, rozhodnout nejde. Stejná chyba jednoho kanálu se u signálu soudělného se 100 MHz s malým posunem projeví v ADEV jako fázová chyba ±~100 ps s periodou průchodu fáze (např. posun 1·10⁻¹⁰ → 100 s → příspěvek ~1·10⁻¹² @ 100 s).
- ⚠️ HYPOTÉZA o původu (hladký průběh, vázaný na fázi taktu, ne na jednotlivé tapy): přepínání logiky na hranách 100 MHz moduluje zpoždění vstupní cesty nebo řetězu, případně zkresluje rozložení zásahů kruhového oscilátoru při kalibraci. Rozhodne až pokus níže.
- **Rozhodující pokus (bez změny firmwaru):** místo externího GPSDO pustit na A i B (přes tentýž dělič) signál **nesoudělný se 100 MHz**, např. z generátoru Si5356 10,000 040 MHz (posun 4 ppm = fáze projde 10 period taktu za segment, D se v segmentu zprůměruje). Hypotéza předpovídá σ(A−B) ≈ 4–5 ps nezávisle na čemkoli; když ne, je D jinde.

#### 2026-10-30 16:15 – 17:15 — A−B 1 h, generátor Si5356 10 000 040 Hz (rozhodující pokus)

- **Zapojení:** Si5356 CLK2 → vstup A, CLK4 → vstup B, oba 10 000 040 Hz (+4 ppm proti 10 MHz). CLK2/3 a CLK4/5 mají každá svůj MultiSynth, VCO 2,2 GHz je společné. Proti návrhu výše tedy nejde o jeden signál přes dělič, ale o dva výstupy, takže mezi A a B může přibýt rozdílový jitter MultiSynthů. CM7 v0.42.12, bitstream F0a `b3fbc32`, deska bez restartu od 16:14. Data jsou v `noc_logs/ab_20261030_1615/` (`tools/ab_night.py --hours 1 --warmup 0`).
- **Výsledek:** 14 380 oken, median A−B 0,0 ps, **σ(MAD) 4,4 ps**. Žádné okno nemá |A−B − med| > 100 ps, největší odchylka je ±20 ps. Vadné segmenty: A 0, B 0. Opravy X0: 0.
- **Srovnání:** dopoledne se signálem z externího GPSDO 10 MHz vyšlo σ 23,7 ps a 7 370 oken nad 100 ps. Ve stejném běhu dala okna s |sweep| < 25 ps 4,4 ps. Teď, kdy fáze projde za segment 10 period taktu (100 ns), vychází **4,4 ps ve všech oknech**. Je to dolní okraj předpovědi 4–5 ps a rozdílový jitter MultiSynthů se v něm neprojevil.
- ⚠️ Regresory `tdc_sweep.py` jsou pro tento běh bez významu. Sweep je ≈ 1 µs a téměř konstantní, takže jeho kolísání je jen šum měření samotného kmitočtu. Sklon A−B na sweep (t +46) je proto korelace šumu sama se sebou: cov(eA − eB, eA + eB) = var eA − var eB. Nejde o důkaz D.
- **Závěr:** potvrzuje se, že σ(A−B) se signálem soudělným se 100 MHz tvoří fázově závislá chyba TDC D(φ), ne šum TDC ani zdroj. Když se fáze uvnitř segmentu protočí přes celou periodu, D se zprůměruje a zbude šum ~4 ps (≈ 3 ps na kanál). Původ D zůstává HYPOTÉZA, oprava se teprve volí (STATUS #347). Generátor zůstává na 10 000 040 Hz (uloženo v syscfg).
- **Vedlejší nález (rozbor `tdc_gensweep.py`, 2026-10-30): soustavná odchylka proti přesné pravdě.** Generátor i takt FPGA jsou ze stejného VCO, takže správný výsledek je přesně 10 000 040 Hz. Naměřený průměr je přesto vyšší: A **+1,75·10⁻¹²**, B **+2,28·10⁻¹²** (SE 1,4 a 1,0·10⁻¹³, tedy +0,44 / +0,57 ps na okno 0,25 s). Po čtvrthodinách kolísá: A +0,35 / +1,8 / +3,6 / +1,3·10⁻¹², B +1,5 / +2,8 / +3,4 / +1,4·10⁻¹². Kmitočet v `ab.csv` je kvantovaný po 4·10⁻¹² (délka okna v celých ps, `fpga_v3.c:1172`, zaokrouhlení, ne useknutí), medián A je 0 a B +4·10⁻¹². Příčina je neznámá (STATUS #348). Test z kroku 0 průměrnou chybu vypisuje u každého běhu.

#### 2026-10-30 18:03 – 18:46 — test s ujíždějící fází, 10 000 000,041 Hz (CM7 v0.42.13, F0a `b3fbc32`)

- **Zapojení a průběh:** CLK_2 → A, CLK_4 → B, oba `gen … 10000000.041` (P1 27647, P2 213052243, P3 213076844, R 1, stav SHODA). Proti pravdě je posun fáze 1 025 ps za okno. 10 463 oken (0,73 h, test ukončen ručně), vadné segmenty 0, X0 opraveno 0. Data `noc_logs/ab_20261030_1803/`. Rozbor `tdc_gensweep.py`, `tdc_gensplit.py`.
- **S periodou fáze 10 ns** (takt) se vysvětlí jen část: A σ 80 → 18 ps, B 98 → 19 ps (R² 0,86 / 0,90). Tvar se po bloky 10 min nemění (korelace 1,000). Zbytek ~16 ps ale není funkce fáze mod 10 ns: okna se stejnou fází (vzdálená o 400) spolu nekorelují. Jeho spektrum má čáry v rozestupu 0,0821 Hz, což odpovídá posunu fáze o 50 ns.
- 🔑 **S periodou fáze 100 ns** (10 taktů, tedy poloha hrany vůči 10 MHz) vysvětlí model téměř všechno: A σ 80 → **7,1 ps**, B 98 → **7,0 ps** (R² 0,99 / 1,00, K 150, testovací okna). Chyba časové značky je tedy deterministická funkce polohy hrany v rámci 100 ns.
  - **Složka periodická v 10 ns** (TDC v rámci taktu): A p-p 213 / rms 57 ps, B p-p 289 / rms 91 ps. Tvary A a B jsou různé (korelace 0,11).
  - **Složka vázaná na 100 ns:** A p-p 353 / rms 81 ps, B p-p 359 / rms 78 ps. Tvar A i B je podobný: propad ~−190 ps široký ~15–20 ns a slabší propad o ~50 ns dál. U B je posunutý o ~30 ns, což je dané neznámou vzájemnou fází výstupů CLK2 a CLK4. Korelace složek A a B −0,37 (posun), v A−B se proto nevyruší.
- **Kandidát na zdroj periody 100 ns (HYPOTÉZA):** do FPGA vede přímo `REF_10MHz` z OCXO (pin 42, `pins.cst:22`) a hodinuje doménu `clk_ref_10m` (SPI PHY, kruhové paměti, stav – `top_v3.v:100`–`529`). Propady ~50 ns od sebe by odpovídaly náběžné a sestupné hraně 10 MHz. Druhá možnost je vazba vstupu CLKIN 10 MHz uvnitř Si5356 do výstupů generátoru, tedy artefakt zdroje, ne čítače. Rozliší pokus s externím OCXO jako signálem.
- **Důsledek pro návrh opravy:** kalibrace hustotou kódů (i přes vstupní pin) opraví jen složku periodickou v 10 ns. Složku vázanou na 100 ns neuvidí, protože histogram kódů nerozlišuje, ve kterém z 10 taktů hrana přišla.
- **Průměrná chyba okna:** A −0,17 ps (−6,9·10⁻¹³), B +0,30 ps (+1,2·10⁻¹²), STATUS #348.

#### 2026-10-30 19:11 – 19:51 — externí OCXO (TTL) jen na A, rozlišovací pokus

- **Zapojení:** externí OCXO (TTL) jen na A, bez T-kusu, generátor CLK2/CLK4 vypnutý. Kmitočet 10 000 002,996 Hz (+3,0·10⁻⁷). 9 654 oken, 27 vadných segmentů A za 40 min. Data `noc_logs/ab_20261030_1911/`. Rozbor `tdc_extsweep.py`: pravda neznámá, fáze z vyhlazeného naměřeného kmitočtu. Metodu jsem ověřil na běhu s generátorem bez znalosti pravdy: vyšly tytéž harmonické složky 100 ns, korelace tvaru 1,00.
- ⚠️ Pokus s T-kusem na A i B dával ~1 % falešných hran (sonda 10 096 / 10 112 kHz, vadný segment v každém okně). TTL výstup do dvou zakončení 49R9 (25 Ω) nedá dost velkou úroveň, stejně jako L-0137. S jedním zakončením sonda ukazovala 10 000 kHz.
- **Výsledek:**

  | model chyby okna | R² | σ okna |
  |---|---|---|
  | perioda fáze 10 ns (K 3 i 6) | −0,01 | 144 → 144 ps |
  | perioda fáze 100 ns (K 10 / 30 / 60) | 0,99 | 144 → 14 / 9 / 10 ps |
  | kontrola se zamíchanou fází | −0,04 až −0,26 | beze změny |

  I s nezávislým zdrojem je chyba závislá na poloze hrany v rámci 100 ns a nevysvětlí ji samotná nelinearita TDC v rámci taktu. Podporuje to zdroj v čítači (FPGA / deska / `REF_10MHz`), ne artefakt generátoru.
- ⚠️ **Tvar se s během s generátorem porovnat nedá.** Posun fáze 74,9 ns za okno je 0,749 P, takže harmonické 2, 6 a 10 se posouvají skoro přesně o půl cyklu za okno a fit je od sebe špatně rozliší. Dominantní vyšla k = 6 (~400 ps), s generátorem k = 1, 2 (63 / 76 ps). Tvary bloků se se vzorem shodují jen s korelací 0,74–0,94. Pro srovnání tvaru je potřeba posun ≤ 0,1 Hz, nebo ~1,5 Hz (0,37 P za okno).

#### 2026-10-30 22:12 – 2026-10-31 06:10 — test s ujíždějící fází přes noc, zkušební bitstream APP_OSC (CM7 v0.42.13)

- **Sestava:** bitstream `ab_test/TEST_FW_0x0508_v3_appOscNoDSPb_20261030-2035.fs` (md5 `7269831c…`, zdroj `218e4da`, varianta `appOsc+noDSPb`). Doména aplikace a SPI běží z vnitřního OSC (250/24 MHz ≈ 10,4 MHz), pin 42 (`REF_10MHz`) je nevyužitý a součty segmentů B jsou v logice místo DSP. CLK_2 → A, CLK_4 → B, oba 10 000 000,041 Hz (P1 27647, P2 213052243, P3 213076844, R 1). Data `noc_logs/ab_20261030_2212/`.
- **Provoz:** 114 678 oken za 7,97 h, 1 130 290 segmentů na kanál, **0 vadných segmentů A i B**, bez restartu (uptime 2 090 → 30 339 s). Kalibrace TDC jen jednou při startu (36,8 °C). V `seq` je 5 děr (výpadek spojení s webem ve 4:20).
- **σ(A−B) MAD 148 ps** (F0a 18:03: 174 ps). Chyba okna jednoho kanálu A 68 / B 96 ps (F0a 80 / 98 ps). ⚠️ Při posunu 102,5 ps na segment je chyba okna úměrná strmosti E, ne jejímu rms, takže tato čísla bitstreamy srovnávají špatně. Srovnání dělá až rozklad E.
- **Rozklad E** (perioda 100 ns, K 130, `tdc_gensplit.py`, `tdc_genstab.py`):

  | rms [ps] | F0a A | F0a B | APP_OSC A | APP_OSC B |
  |---|---|---|---|---|
  | složka vázaná na 100 ns | 81 | 78 | **26** | **27** |
  | složka periodická v 10 ns | 58 | 91 | **96** | **121** |
  | celkem | 100 | 120 | 99 | 124 |
  | zbytek okna po odečtení modelu | 7 | 7 | 8,6 | 5,4 |

- 🔑 **Složka vázaná na 100 ns klesla na třetinu.** U F0a ji z poloviny tvořil schod po celých taktech: posun času v jednom až dvou z deseti taktů o −180 ps, které `tdc_gencyc.py` popíše jako offset taktu, zbytek klesne z 81 na 41 ps. To odpovídá doméně `clk_ref_10m` na pinu 42. S APP_OSC schod zmizel (offsety taktů jen ±13 ps).
  - Zbyly dvě úzké dvojité špičky (A +102 / −95 ps, 3 ns od sebe, šířka v polovině výšky ~2 ns), 50 ns od sebe a s opačným znaménkem. A i B mají tentýž tvar (korelace 0,93 po posunu o vzájemnou fázi CLK2/CLK4).
  - HYPOTÉZA: náběžná a sestupná hrana 10 MHz, která na desce zůstala. Buffer U21 ji vede přes R89 33R na pin 42 (na konektoru modulu U6 pin 16, vedle REF_100MHz na pinu 14), přes R87/R62 do CLKIN Si5356 (U9), přes R90 na J5.11 do STM a přes R88 do U22. Ověří se vyjmutím R89, protože APP_OSC pin 42 nepotřebuje.
- **Složka 10 ns se zvětšila** (A 58 → 96, B 91 → 121 ps) a je teď hlavní chybou. Celkové rms je podobné jako u F0a. Tvar se mezi bitstreamy liší (korelace po zarovnání A 0,66, B 0,86). Oba mají propad −110 až −230 ps a za ním během 1,5–2 ns vzestup o +170 až +380 ps. Proč je větší, je zatím neznámé. Kandidáti: jiné rozmístění a vedení vstupu k řetězu po novém P&R, nebo kalibrace z ring oscilátoru, která na vstupní cestu nesedí (STATUS #347, krok 1).
- **Stálost v čase:** model z kteréhokoli 2h bloku vysvětlí ostatní bloky se zbytkem 8,6–9,0 ps (A) a 5,4–5,7 ps (B). Korelace tvaru prvního a posledního bloku je 1,000 u složky 10 ns a 0,996 / 0,999 u složky 100 ns, rms rozdílu 1–2,5 ps (`tdc_genstab.py`). V rámci jednoho zapnutí a jedné kalibrace je chyba pevná a tabulka korekcí by vydržela celou noc. Mezi kalibracemi a zapnutími to ověřené není.
- ⚠️ **Slepá harmonická testu (L-0250).** Při posunu 102,5 ps na segment chyba okna na harmonickou k = 140 (perioda 100 ns, tedy 14. harmonická složky 10 ns) nereaguje vůbec (|H| = 0,001). Slabá je i odezva na k 1–4. S K 150 proto fit dal složce 10 ns 105–163 ps podle bloku (a F0a 107 ps), přestože okna předpovídal stejně dobře. S K ≤ 130 vychází stabilně 96 ps. `tdc_gensweep.py` slepé harmonické nově hlásí. Čísla z běhu 18:03 (K 100) platí.
- **Průměrná chyba okna (STATUS #348):** A −0,05·10⁻¹² (po 2h blocích +0,04 / −0,13 / +0,01 / −0,13), B +0,56·10⁻¹² (+0,94 / +0,59 / +0,56 / +0,14). S F0a bylo A −0,7 / B +1,2·10⁻¹² (18:03) a +1,8 / +2,3·10⁻¹² (16:15). U A odchylka zmizela, u B je menší, ale trvá a přes noc klesá.
- ⚠️ **Simulace varianty APP_OSC neprošly** (`topv3` 657 chyb, `topv3cal` 38, přechody dat z domény 100 MHz do domény aplikace bez handshaku). Na desce chybu neukázala kalibrace (součet histogramu kontroluje STM) ani segmenty. K vydání to nestačí.
- **Závěr:** hlavním zdrojem složky 100 ns byla doména `clk_ref_10m` na pinu 42. Zbyla zhruba třetina s jiným tvarem. Hlavní chybou je teď složka 10 ns (~100–120 ps rms). Je deterministická a přes noc stálá a model po ní nechá 5–9 ps na okno. Oprava tedy může chybu okna zmenšit zhruba desetkrát.

#### Postup: test TDC s ujíždějící fází (krok 0 návrhu kalibrace, v0.42.13)

Účel: chyba časové značky **každého kanálu zvlášť a absolutně** jako funkce fáze hrany proti taktu 100 MHz. Pravdu dávají registry MultiSynthu, takže výsledek nezávisí na OCXO ani na šumu zdroje.

1. Zapojení: Si5356 CLK_2 → A, CLK_4 → B.
2. UART `gen 2 10000000.041` a `gen 4 10000000.041`, potom `gen` → opsat `P1 P2 P3 R` obou dvojic a zkontrolovat stav `SHODA`. Zlomek Hz se neukládá, po power-cyklu je potřeba ho zadat znovu.
   - Proč ,041: posun fáze 1 025 ps za okno, segmenty na mřížce 2,5 ps, perioda 10 ns projde za 2,4 s.
   - Nevhodné: ,040 (posun přesně P/10, vzor fáze se opakuje po 10 oknech, L-0249). Nevhodné jsou i kmitočty, u kterých se fáze posune o víc než ~2,5 ns za segment, například 10 000 040 Hz.
3. `python -I tools/ab_night.py --hours 1 --warmup 0`.
4. **Jedním příkazem (od 2026-10-31):** `python -I docs/audit/data/2026-10-29_kanal_b/tdc_geneval.py noc_logs/<běh>`. Běh zkopíruje do `noc_logs/_rozbor/<běh>`, registry generátoru vezme z výpisu `gen` v `uart.log` (`ab_night.py` ho loguje každý cyklus). Pak pustí report a všechny rozbory níže a vypíše souhrn: rms složek, chybu okna, stálost a srovnání tvaru s F0a (`gen1803`) a nocí APP_OSC (`noc0610`).
   Jednotlivě: rozbor nad kopií dat `python -I docs/audit/data/2026-10-29_kanal_b/tdc_gensweep.py <ab.csv> --a P1 P2 P3 R [--b …] --out <předpona>`. Pusť ho dvakrát: s periodou 10 ns (výchozí) a s `--P 100000 --K 130` (poloha vůči 10 MHz). Rozklad na obě složky dělá `tdc_gensplit.py <ab.csv> P1 P2 P3 R 130`, stálost po blocích `tdc_genstab.py` a rozklad složky 100 ns na offsety taktů a úzké špičky `tdc_gencyc.py`.
   - ⚠️ K drž pod slepou harmonickou, kterou `tdc_gensweep.py` hlásí jako `!! … slepe harmonicke` (L-0250). Při posunu 102,5 ps na segment je to k = 140 při periodě 100 ns a k = 14 při periodě 10 ns. Harmonické nad ní změří jen druhý běh s jiným posunem.
   - Výstup: R², σ před a po odečtení tvaru, průměrná chyba okna (absolutní přesnost), tvar E_A, E_B, E_A−E_B po 500 ps a korelace E_A s E_B.
   - Srovnání s tvarem D z běhu s GPSDO: `tdc_dtvar.py <ab.csv 9h> ref.npy`, pak `tdc_dcmp.py <předpona>_AB.npy ref.npy`.
5. Stejný test zopakovat při 1 MHz (`gen 2 1000000.001`, posun 250 ps za okno) a 25 MHz (`gen 2 25000000.023`, posun 230 ps za okno), případně s předděličkou. Tím se ukáže, zda chyba závisí na kmitočtu a na cestě signálu.

#### Postup: kalibrace TDC ze vstupního pinu (krok 1, STM v0.43.0 + FPGA FW 0x0509)

Účel: odstranit složku chyby periodickou v 10 ns (po kalibraci z ring oscilátoru ~100–120 ps rms, noc 2026-10-30). Histogram kódů se sbírá ze signálu na vstupu, tedy stejnou cestou jako měření, místo z ring oscilátoru.

1. Zapojení jako u testu s ujíždějící fází: Si5356 CLK_2 → A, CLK_4 → B.
2. `tdc cal pin gen`: generátor se po dobu kalibrace přepne na 11 603 104 Hz a pak se vrátí původní nastavení.
   - 100 MHz / 11 603 104 Hz = 8,618…, takže se fáze hrany posune o 0,618 taktu na hranu (zlatý řez). Za 2^20 hran pokryje takt rovnoměrně.
   - Bez `gen` musí být na vstupu nesoudělný signál 0,5–19 MHz s předděličkou P = 1. STM rovnoměrnost ověří z kmitočtu (`fpga_v3_calpin_cover`, odchylka obsazení osminy taktu ≤ 10 ‰).
   - Nevhodné: 10 MHz z GPSDO a testovací ,041 Hz (soudělné se 100 MHz).
3. Výsledek vypíše `tdc cal`: „histogram z pinu PROŠEL (kódů …, odchylka osminy … ‰), platná tabulka z VSTUPNÍHO PINU“. `status` → `TDC KALIBRACE (STM) … zdroj A pin, B pin`.
   - Neprošlý histogram (`fpga_v3_calpin_check`: málo kódů, nerovnoměrný) se nezapíše a STM spustí kalibraci z ring oscilátoru.
4. Ověření: test s ujíždějící fází (postup výše, 1 h stačí, rozbor `tdc_gensplit.py … 130` a `tdc_genstab.py`). Složka 10 ns má klesnout z ~96 / 121 ps rms.
   - ⚠️ Teplotní rekalibrace (CM7 při změně teploty FPGA o ≥ 3 °C) a power-cyklus kalibrují z ring oscilátoru a tabulku z pinu přepíšou. Během testu hlídat zdroj ve `status`.
5. Totéž se dá bez nového bitstreamu ZMĚŘIT (ne opravit): `docs/audit/data/2026-10-29_kanal_b/tdc_rawpin.py sber` (RAW záchyty po 512 značkách s generátorem 11 603 104 Hz), pak `rozbor <adresář> --ref <adresář s tdcref_A/B.npy z tdc_gencyc.py>`. Hustota naměřených jemných časů dá chybu tabulky na vstupní cestě a porovná ji s tvarem z testu s ujíždějící fází. Rozbor je ověřený na syntetice (`syntest`).

### 2026-11-01 — první měření s FW 0x050A (zkušební bitstream APP_OSC + součty B v DSP, `TEST_FW_0x050A_v3_appOsc_20261031-2153.fs`, CM7 v0.43.0)

Zdroj: Si5356 CLK_2 → A, CLK_4 → B, oba 10 000 000,041 Hz (P1 27647, P2 213052243, P3 213076844, R 1). Data `noc_logs/ab_20261101_1606/` (1 h) a `noc_logs/ab_20261101_1747/` (20 min), rozbor `tdc_geneval.py`. ⬜ Vše po power-cyklu, krátce po náběhu (deska zahřátá ~1 h).

- **Oprava součtů B v DSP (L-0252) funguje na křemíku:** `fpga v3 seg` 0 vadných segmentů z 1565 (B) a 1590 (A), okna B bez LSQ 0 z 160, max odchylka B 197 ps (s appOsc@2/@3 100 % vadných, 165 ms). Od startu testu 0 vadných i SX mimo meze na obou kanálech; kumulativní počty „oken s vadným segmentem" od zapnutí jsou na A a B shodné (6 / 7 z 47 519).
- **Kalibrace TDC z pinu (`tdc cal pin gen`) na desce ODMÍTNUTA** oběma kanály jako nerovnoměrná: A kódů 149 / odchylka osmin −135 ‰, B kódů 151 / +520 ‰ (mez ±100 ‰), kontrola pokrytí fáze před sběrem 0 ‰. Tabulka zůstala z ring oscilátoru, generátor se vrátil. Příčina neznámá.
- **Hustota kódů z pinu přes RAW (`tdc_rawpin.py sber`, 400 záchytů, ~205 000 značek/kanál):** A špička–špička 421 ps (rms 77), B 857 ps (rms 217). Po přehození kabelů (CLK_2 → B, CLK_4 → A) 486 / 97 u A a 744 / 186 u B: anomálie **zůstala u kanálu B**, nepatří generátoru. Tvar se s testem s ujíždějící fází shoduje slabě (korelace 0,27–0,54). Tato metoda je s testem s ujíždějící fází v rozporu (RAW: B horší, test: A horší) a jako ukazatel ji nepovažuji za spolehlivou.
- **A−B s ujíždějící fází:**

| běh | σ(A−B) MAD | složka 10 ns A / B | 100 ns A / B |
|---|---|---|---|
| F0a `b3fbc32` (2026-10-29) | 174 ps | 58 / 91 ps | 81 / 78 ps |
| APP_OSC 0x0508 + noDSPb (2026-10-31, 8 h) | 148 ps | 96 / 121 ps | 26 / 27 ps |
| **0x050A, 1 h, kalibrace č. 5 (36,7 °C)** | **526 ps** | **274** / 94 ps | 31 / 27 ps |
| **0x050A, 20 min, po `tdc cal` (kal. č. 7)** | **377 ps** | **214** / 94 ps | 28 / 26 ps |

  Stálost tvaru 10 ns během běhu +1,000 (pevná tabulka). Kanál B je reprodukovatelný (94 ps, korelace tvaru s APP_OSC 0,86–0,95). **Chyba kanálu A závisí na konkrétní kalibraci z ring oscilátoru** (274 → 214 ps po nové kalibraci) a je u tohoto bitstreamu výrazně horší než u dřívějších (58–96 ps). Úzké špičky 100 ns složky z APP_OSC zůstaly (26–31 ps).
- **Závěr:** součty B v DSP jsou opravené, ale celkový σ(A−B) je horší (377–526 ps proti 148–174 ps), a to kvůli složce 10 ns kanálu A. HYPOTÉZA: kvalita kalibrace z ring oscilátoru je závislá na rozmístění a mezi kalibracemi nahodilá; ověření = opakovat `tdc cal` několikrát a sledovat složku 10 ns A, nebo kalibrace z pinu s volnější mezí. Kalibrace z pinu zatím nepomůže, protože ji kontrola odmítá.

- 🔑 **Opakovaná kalibrace z ring oscilátoru (2026-11-01, FW 0x050A, 36,7 °C, 10min testy A−B s ujíždějící fází po každém `tdc cal`, `noc_logs/_rozbor/opak_cal.txt`):**

| kalibrace | σ(A−B) MAD | složka 10 ns A | složka 10 ns B |
|---|---|---|---|
| č. 5 (1 h) | 526 ps | 274 ps | 94 ps |
| č. 7 (20 min) | 377 ps | 214 ps | 94 ps |
| č. 10 | 196 ps | 185 ps | 93 ps |
| č. 13 | 316 ps | 221 ps | 94 ps |
| č. 16 | 439 ps | 247 ps | 94 ps |

  Kanál B vychází pokaždé stejně (93–94 ps), **chyba kanálu A se mezi kalibracemi mění** (185–274 ps) při téže desce, teplotě a bitstreamu → kalibrace z ring oscilátoru u A není opakovatelná. Výběr nejlepší z N by byla loterie; opakovatelnou kalibraci má dát jen kalibrace z pinu, ta se ale na desce odmítá (viz výše). Nevíme, jestli jsou dřívější hodnoty F0a (58 ps) a APP_OSC 0x0508 (96 ps), každá z jediné kalibrace po zapnutí, reprezentativní, nebo šťastné; rozhodne opakování `tdc cal` na starém bitstreamu.
- 🔑 **Srovnání na téže desce (2026-11-01, 36,6 °C, kabely beze změny, 10 min zahřátí, 3× `tdc cal` + 10min test):** bitstream `TEST_FW_0x0508_v3_appOscNoDSPb_20261030-2035.fs` (součty B v logice) dal σ(A−B) MAD 126 / 95 / 110 ps, složka 10 ns **A 115 / 114 / 111 ps**, B 93 / 96 / 96 ps (`noc_logs/_rozbor/opak_cal.txt`; 0x050A v `opak_cal_050A.txt`). **0x050A je tedy na kanálu A skutečně horší (185–274 ps) a jeho kalibrace A není opakovatelná, u 0x0508 je A stabilní.** Kanál B je v obou stejný. Oprava součtů B v DSP (L-0252) sama funguje; zhoršení A přinesl jiný P&R. HYPOTÉZA: rozmístění TDC řetězu / ring oscilátoru kanálu A v sestavení `appOsc@3`; ověření = srovnat polohu řetězu A (`sim/check_tdc_placement.py`, `tools/floorplan_svg.py`) mezi `appOsc@3` a `appOsc+noDSPb@0`, případně jiný seed 0x050A.
- ⚠️ **Bitstream `TEST_FW_0x0509_v3_appOscNoDSPb_20261031-1702.fs` má na desce vadu indexu kanálu B** (třída L-0247): `fpga v3 seg` 1015 vadných z 1083 segmentů B (A 1 z 1112), X0 ztrácí bity, SX mimo meze 899. Pro měření nepoužívat. Vada je vlastnost rozmístění (0x0508 noDSPb se stejnou logikou součtů B ji nemá).
- 🔑 **Seed @6 (`TEST_FW_0x050A_v3_appOscS6_20261031-2155.fs`, `ch_a_d` jen BR jako @3, ale hodiny RO `u_ro/n[12]` v TL místo BL), 36,8 °C, 3× `tdc cal` + 10 min:** σ(A−B) MAD **73 / 67 / 59 ps**, složka 10 ns **A 85 / 88 / 92 ps** (stabilní), B 119 / 115 / 115 ps (`noc_logs/_rozbor/opak_cal_S6.txt`). Nejlepší výsledek testu s ujíždějící fází dosud. Rozdíl proti @3 je hlavně poloha hodin RO (nahoře / dole u řetězců R26/R27) → podporuje HYPOTÉZU, že hodiny RO ve spodních kvadrantech zhoršují kalibraci kanálu A. ⬜ Protipokus seed @2 (`ch_a_d` BR+BL, RO BL).
- ❌ **Protipokus seed @2 (`TEST_FW_0x050A_v3_appOscS2_20261031-2153.fs`, `ch_a_d` BR+BL, RO BL), 37,1 °C, 1× `tdc cal` + 10 min (deska ~15 min po zapnutí):** složka 10 ns **A 80 ps**, B 92 ps, σ(A−B) MAD 122 ps (stálost 100 ns jen 0,61 = náběh; `noc_logs/_rozbor/opak_cal1.txt`). Kanál A je dobrý i s hodinami RO dole → **hypotéza o poloze RO / pokrytí `ch_a_d` NEPLATÍ.** Špatné bylo jen sestavení @3 (příčina neznámá); S2 i S6 mají A 80–92 ps. Na noc vybrán S6 (3 shodné běhy po zahřátí, σ 59–73 ps).
- 🔴 **OPRAVA POPISKŮ (2026-11-02): ve všech testech z 2026-11-01 a 2026-11-02 je „A“ fyzicky kanál B a naopak.** `tools/ab_night.py` zapisuje `f_primary` = `freq_hz` = kanál vybraný tlačítkem CHAN (`channel` v SSE, `httpd_min.c` `chAB()`), a ve všech těchto bězích bylo `channel = 1` (CHAN = B). Rozbory (`tdc_gensweep.py` a další) berou `f_primary` jako A. Testy z 2026-10-28..30 měly převážně `channel = 0` (popisky správně), `ab_20261030_2138`..`2158` mají 1. Výsledky výše z 2026-11-01 („kanál A u @3 špatný“, „S6 A 85–92 ps“, „S2 A 80 ps“) tedy platí pro **fyzický kanál B**. Tím mizí rozpor s RAW rozborem z pinu (`tdc_rawpin.py`: špatný B, i po prohození kabelů). Hypotéza o globální síti `ch_a_d` byla postavená na špatném kanálu.
- 🟡 **Noc 2026-11-01/02, S6 (`TEST_FW_0x050A_v3_appOscS6_20261031-2155.fs`), 15,5 h, 1 kalibrace po zapnutí (37,3 °C), `noc_logs/ab_20261101_2106` (rozbor `noc_logs/_rozbor/ab_20261101_2106`):** 223 063 oken, σ(A−B) MAD **188 ps**, po hodinách 185–190 ps (měření v rámci jedné kalibrace velmi stálé). Složka 10 ns fyzicky **B 209 ps**, A 124 ps; složka 100 ns 27 / 27 ps; po odečtení složky 10 ns zbývá chyba okna jen **17 / 15 ps**. Vadné segmenty A/B 0 / 0 z 2 221 621 → **oprava součtů B v DSP (FW 0x050A, L-0252) na desce potvrzena** (⬜ jen se seedem S6; power-cyklus ano).
- 🟡 **Ráno po noci (uptime ~16 h), 3× `tdc cal` + `tdc hist` + 10 min (`noc_logs/_rozbor/cal_hist/`):** složka 10 ns fyzicky **B 204 / 197 / 196 ps**, A 108–110 ps. Tabulky kalibrací jsou mezi sebou shodné (rozdíl převodní funkce kanálu A 24–27 ps rms), noční proti ranním 73 ps rms. Tentýž bitstream S6 dal včera po jiném zapnutí B 85–92 ps. **Chyba kanálu B se tedy mění mezi zapnutími, ne mezi kalibracemi ani mezi sestaveními.** Souhrn (fyzicky B): @3 185–247 ps, 0x0508 noDSPb 111–115, S6 85–92, S2 80, S6 noc 209, S6 ráno 196–204.
- 🔑 **Tvar chyby (porovnání `tdcref_*.npy`, skript `shape_cmp.py` ve scratchpadu):** po kruhovém posunu má fyzicky B v dobrém a špatném zapnutí tentýž tvar (korelace +0,93), ve špatném je jen ~2× větší; převládá **1. harmonická periody 10 ns** (sinus na periodě taktu). V dobrém běhu mají A a B téměř stejný tvar (korelace +0,94, rozdíl 41 ps); v nočním běhu se tvary kanálů rozešly a posun mezi nimi je ~2,5 ns (v dobrém ~0,4 ns). HYPOTÉZA: chyba závisí na **fázovém odstupu signálů A a B** (vzájemné ovlivnění kanálů v řetězcích / napájení), který se ustaví při každém startu generátorů. Ověření bez power-cyklu: 10 min jen s jedním kanálem (druhý generátor vypnutý) a pak několik odstupů A−B.
- 🔑 **Vzájemné ovlivnění kanálů (2026-11-02, S6, JEDNA kalibrace po celou dobu, 10 min na etapu, `noc_logs/_rozbor/vazba_ab/souhrn.txt`, popisky už fyzické po `333ed78`):**

  | etapa | A 10 ns | B 10 ns | σ(A−B) MAD |
  |---|---|---|---|
  | 1 oba (CHAN B) | 111 | 196 | 165 |
  | 2 jen B (`gen 2 off`) | – | **95** (zbytek 9 ps) | – |
  | 3 A znovu zapnut | 110 | 196 | 163 |
  | 4b jen A (`gen 4 off`, CHAN A) | **79** (zbytek 9 ps) | – | – |
  | 5 B znovu zapnut (po etapě 4) | 110 | 195 | 163 |
  | 7 B znovu zapnut (po etapě 4b), CHAN A | **60** | **102** | **101** |
  | 8 beze změny, CHAN B | 60 | 102 | 102 |
  | 9 beze změny, CHAN A | 60 | 103 | 101 |
  | 6 A 10 000 000,141 Hz (posun A−B se točí) | chyba B není funkcí vlastní fáze (R² 0,27; s rozdílem fází A−B s periodou 100 ns 0,63) | | 267 |

  Závěr: **chyba každého kanálu závisí na poloze hran druhého kanálu**; posun A−B se ustaví při zapnutí generátoru (etapy 5 a 7 = tentýž krok `gen 4 off` → `gen 4 10000000.041`, výsledek 195 vs 102 ps) a tedy i při zapnutí desky — odtud „chyba se mění mezi zapnutími“. CHAN vliv nemá (8 vs 9). Výsledky dřívějších sestavení (@3, S2, S6, 0x0508) tedy nesrovnávaly sestavení, ale náhodný posun A−B. HYPOTÉZA (neověřeno): vazba může být v **generátoru** (výstupy CLK2/CLK4 téhož Si5356, i CLK0/1 = takt FPGA a CLK6/7 200 MHz), ne v čítači — test s generátorem jako „pravdou“ ji od vazby ve FPGA neodliší. ⬜ Rozlišit: jeden výstup generátoru rozbočený do A i B (pevný posun délkou kabelu), `gen 6 off`, posun A−B zapisovat (`fpga ab`).
- 🟡 **Prohozené kabely (2026-11-02 večer, CLK_2 → B, CLK_4 → A, tatáž kalibrace jako test vazby, `vazba_ab/souhrn.txt` etapy P1–P5):** A (z CLK4) **104 ps**, B (z CLK2) **91 ps**, σ(A−B) 166–172 ps (chyby kanálů mírně proti sobě, korelace −0,24). Třikrát `gen 4 off` → `gen 4 10000000.041` (P2–P4): **beze změny** (104 / 91 ps). `gen 6 off` (200 MHz): beze změny (103 / 90 ps). Horší byl ve všech třech stavech kanál napájený z **CLK4** (před prohozením B 196 / 102 ps proti A 110 / 60 ps, po prohození A 104 proti B 91) → spíš vlastnost výstupu generátoru, ne kanálu; rozdíl po prohození je ale malý (13 ps). Oprava výkladu etapy 7: vypnutí a zapnutí výstupu (`gen n off/on`) posun A−B **nemění** (MultiSynth běží dál, mění se jen OEB); posun se změnil etapou 6 (A 10 min na +0,1 Hz → fáze odjela). Posun A−B jde tedy nastavit **řízeně**: krátké rozladění CLK2 (např. +0,01 Hz na 10 s = posun 10 ns).
- 🔑 **Noc 2026-11-02/03 (S6, 17,5 h, kabely prohozené CLK_2 → B, CLK_4 → A; `noc_logs/_rozbor/noc18/`, skript `noc18.py`, rozbor `scan_eval.py` + `noc18_prof.py`):** posun A−B se točil plynule rozladěním jednoho generátoru o +0,001 Hz (25 ps/okno, 100 ns za ~17 min, ~20 průchodů na fázi), okna rozdělena podle posunu po 1 ns, v každém koši fit chyby kanálu podle vlastní fáze (K 12 — K 20 v koších diverguje na slepé harmonické 14 při posunu 1025 ps/okno).
  - **Fixní posun:** F0 A 104 / B 89 ps, σ(A−B) 175 ps; F4 (nová kalibrace) A 107 / B 92 ps, σ 182 ps. **Jen A** 75 ps, **jen B** 97 ps (F2).
  - **Chyba podle posunu A−B (1 ns koše):** A 71–222 ps, B 70–248 ps, **σ(A−B) MAD 60–262 ps**. Mimo úzká pásma je chyba kanálu ≈ hodnota bez druhého kanálu (75–100 ps); vazba přidává **úzké hrby ~6–10 ns široké** (A u posunu ~20–24 a ~84–88 ns, B u ~32–36 a ~96–98 ns, osa posunu má neznámou konstantu). Střední chyba okna se s posunem mění jen o ~20 ps p-p (relativně ~8·10⁻¹¹ na okno 0,25 s), tedy vazba mění hlavně tvar, ne střední kmitočet.
  - **Opakovatelnost:** F1 vs F5 (tatáž osa, jiná kalibrace) korelace profilu B +0,96, A +0,85, rozdíl 9 / 18 ps rms → profil je vlastnost posunu, **kalibrace ho nemění**. F3 (rozladěn druhý generátor, posun jede opačně) po kruhovém posunu osy sedí na F1 (korelace σ(A−B) +0,81, B +0,86, A +0,78) → záleží jen na vzájemném posunu, ne na tom, který generátor se hýbe.
  - Zdroj vazby (Si5356 vs vstup/FPGA) tím **nerozlišen** — obě možnosti závisí jen na posunu. ⬜ Rozhodující test: jeden kanál z nezávislého zdroje (externí GPSDO), druhý z generátoru; hrby na kanálu z generátoru = vazba v čítači.
- ❌ **Opravy 2026-11-03 (validace auditu dat, `docs/audit/2026-11-03_validace-auditu-dat.md`):** (1) „Horší byl ve všech třech stavech kanál napájený z CLK4“ (záznam prohozených kabelů) **neplatí** — byl vyvozen ze stavů s vazbou. Bez druhého kanálu: A 79 ps (z CLK2) / 75 ps (z CLK4), B 95 ps (z CLK4) / 97 ps (z CLK2) → vlastní chyba patří **kanálu**. (2) „Tabulky kalibrací … rozdíl 24–27 ps rms“ (ráno po noci S6) je artefakt `hist_cmp.py` (průměr obsahoval vadný noční `hist_0`, součty A 1 048 585 / B 1 052 754 ≠ 2²⁰); párově A 7–9 ps, B 8–19 ps (Poissonova mez 5,6 ps). (3) Posun A−B mění jen rozladění a power-cyklus, ne restart výstupu.
- 🔑 **Noc 2026-11-03/04 (noc16b, 18,5 h, FW 0x050A S6, CM7 v0.43.2, kabely CLK_2 → B, CLK_4 → A; `noc_logs/_rozbor/noc16b/souhrn.txt`, skripty `noc16b.py` + `noc16b_ext.py`, rozbor `scan_eval.py`, `val_lines.py`):**
  - **S0:** `tdc cal` při 37,4 °C.
  - **S1 (šířka pulzu, B = pin A invertovaný):** B−A −1,217 ns, σ 50 ps (n 651), mnoho zahozených startů (652 → 949). Je to zpoždění invertované cesty, ne střída. Šířku pulzu tímto postupem změřit nelze; ⬜ zopakovat jinak.
  - **S2 (5 MHz, A ,042 / B ,041 Hz, 6 h, 85 940 oken, posun A−B projel 21,6 periody 200 ns, koše 2 ns):** rozbor s K 12 u B diverguje (při kroku fáze ~2,05 ns/okno je slepá harmonická k ≈ 5), proto K 3 a K 4 (shodné výsledky, `S2_5MHz_scanK3.txt`, `…K4.txt`). Složka 10 ns: **A 33–137 ps** s hrby u posunu ~44–46, ~54–58 a ~90–94 ns, ve druhé půlce periody (100–200 ns) plochá ~60–80 ps; **B 24–90 ps** bez výrazných hrbů. ⚠️ **Opraveno 2026-11-04:** K 4 kanál B podfitovalo – zbytek fitu klesá i s K 5 (A 45 → 37 ps, B 47 → 42 ps), 5. harmonická je skutečná (`S2_5MHz_scanK5.txt`; `scan_eval.py` od teď volí K pod první slepou harmonickou sám). Spolehlivější ukazatel než složka 10 ns je **zbytek** v koši: A rušené při posunu A−B ~28–100 ns (zbytek 50–100 ps, jinde 17–30), **B rušené také**, v užším pásmu ~−40…+12 ns (zbytek 65–102 ps, jinde 27–50). Věta „B bez hrbů“ tedy neplatí. σ(A−B) MAD 107–357 ps. Hrby leží jen v jedné půlce periody 200 ns a neškálují čistě s periodou (proti 10 MHz ×2) → vazba závisí spíš na **úrovni nebo typu hrany druhého signálu** než na pevném zpoždění (HYPOTÉZA).
  - **S3 (10 MHz ,041 oba, `gpsdo off`, posun 1PPS `gps tp z`, 40 min na krok):** čára j32 (1 s) [ps]: z0 0,12 · z25 0,23 · z50 0,52 · z75 0,57 · z100 0,58 · z125 0,75 · z150 0,44 · z175 0,64 · z200 0,38 · z225 0,38 · z250 0,29 · 1PPS vyp 0,27. Vzor tříd mod 4 (součet rozpětí A+B): 0,42 / 0,61 / 1,36 / 1,67 / 1,89 / 1,92 / 1,31 / 1,71 / 1,05 / 1,09 ps (z0…z225), hlavně v kanálu A, až ±1 ps. ⚠️ Řádek „GPS TP“ v `souhrn.txt` ukazuje „posun 0 ms“ (regex chytil výpis po `gps tp def`); `S3_*_uart.txt` potvrzují, že posun se nastavil.
  - **S4:** datalog vyp/zap: j32 0,18 / 0,28 ps → bez vlivu.
  - **E1 (opakování, rozhoduje časový/teplotní konfund S3):** z125 znovu velké (j32 0,68 ps, A tříd +0,58 −0,81 −0,11 +0,39), z375 = z125 + 250 ms velké (j32 0,44, A +0,64 −0,61 −0,13 −0,16), z0 malé (j32 0,12, A −0,04 +0,09 +0,20 +0,22). **E2** (`gpsdo on`, TP výchozí, datalog vyp): j32 0,07 ps. → ✅ **Čára 1 s a vzor tříd závisí na poloze 1PPS v okně 250 ms, ne na čase běhu.** Malé při 0 ms (hrana 1PPS na začátku okna), velké při 50–175 ms. Posun o celé okno (375 ms) vzor **neotočil** o jednu třídu (třídy 0/1 stejné znaménko) → mechanismus „chyba okna, ve kterém leží hrana 1PPS“ to celé nevysvětluje (HYPOTÉZA otevřená). ⚠️ **Opraveno 2026-11-04: závěr o neotočení byl chybou rozboru.** `val_lines.py` počítal třídu jako `(seq − seq[s0]) mod 4`, tedy od prvního okna KAŽDÉHO běhu – třídy různých etap nebyly srovnatelné. Nově třída vůči hraně 1PPS (fáze oken proti UTC z `t_unix`, parametr `z=`; `utc_cls.py`): z125 A +0,58 −0,81 −0,11 +0,39, **z375 A +0,64 −0,61 −0,13 −0,16**, S3 z075…z225 stejný tvar (+0,35…+1,04 / −0,10…−0,93) → ✅ **vzor jde s 1PPS.** Tvar „+x v jednom okně, −x v následujícím“ = **chyba časové značky SPOLEČNÉ HRANICE dvou oken** (jedno okno delší, druhé kratší), hlavně kanál A, ~0,3–1 ps; velikost závisí na poloze hrany 1PPS proti hranici. Kde přesně v okně je to nejhorší, rozhodne `noc17` (latence SSE v `t_unix` neznámá). HYPOTÉZA mechanismu: činnost vyvolaná 1PPS (STM: smyčka/SPI, FPGA: `pps_cap`/IDES) posune nebo zaruší zachycení značky na hranici.
  - Střední chyba okna ve všech etapách |·| < 0,12 ps (< 5·10⁻¹³).
  - **Ověření posunu 1PPS (2026-11-04, `noc17/kratky`, `fpga ev`: čas hrany na ose FPGA a čas od 1PPS):** `gps tp z` 0…475 ms po 25 ms posouvá hranu 1PPS přesně o −25 ms na krok (pulz o z **dřív**, TP5 `userConfigDelay`), i přes 250 ms. Pozor: osa FPGA i číslo segmentu v `fpga ev` přetékají, rozdíly mezi snímky platí jen uvnitř souvislého úseku.
  - 💡 Provozní důsledek: výchozí stav (TP posun 0) je ten dobrý; amplituda vlivu ~0,1–0,75 ps na okno 0,25 s (~3·10⁻¹² relativně) je pod šumem kanálu (5,4–5,7 ps rms).
- 🔑 **Noc 2026-11-04/05 (noc17, 22 h, FW 0x050A, CM7 v0.43.2, kabely CLK_4 → A, CLK_2 → B, oba ,041 Hz, `gpsdo off`; `noc_logs/_rozbor/noc17/{kratky,noc}/souhrn.txt`, souhrn `noc17_eval.py` → `noc17/vyhodnoceni.txt`):**
  - **N1, délka pulzu 1PPS při z125 (j32 A):** 10 ms 0,31 · 50 ms 0,37 · 100 ms 0,54 a 0,60 (noc16b 0,75 a 0,68) · 200 ms 0,71 ps; při z0: 100 ms 0,12 · 200 ms 0,34. Kanál B 0,02–0,22 bez trendu. Pozadí bez 1PPS (noc16b) 0,07–0,27. → ✅ **Rozhoduje doba vysoké úrovně pulzu, ne hrana.** 10ms pulz dá úroveň pozadí. Činnost spouštěná hranou (STM, `pps_cap`) by na délce pulzu nezávisela, takže hypotéza z E1 výše neplatí.
  - **N2, poloha z250–475 proti noc16b S3 z0–225 (pulz 100 ms):** z | z+250 = 0,12 | 0,22–0,28 · 0,23 | 0,12–0,16 · 0,52 | 0,19–0,36 · 0,57 | 0,38 · 0,58 | 0,43 · 0,54–0,75 | 0,44–0,53 · 0,43 | 0,55 · 0,64 | 0,58–0,74 · 0,38 | 0,47 · 0,38 | 0,34. → profil se opakuje po 250 ms. Rozdíly do ~0,3 ps dává vektorový součet s pozadím, jehož fáze se o 250 ms neotáčí.
  - **Poloha hrany 1PPS v segmentu** (−dp mod 25 ms z `fpga ev`): **7,60 ms** celou noc (7,604 → 7,596 ms za 22 h, tj. −0,1 ppb = volný OCXO). Kroky z po 25 ms tedy mění jen to, který segment okna hranu obsahuje. Z N1 vychází, že ovlivněná hranice okna leží při z125 asi 70–95 ms za náběžnou hranou (50ms pulz ji nepokryje, 100ms ano). Vliv je velký uvnitř pulzu a doznívá desítky ms po sestupné hraně. Přesnou polohu vůči hranici okna z logů určit nejde, protože osy `fpga ev` přetékají (L-0256). Rozhodne až výstup STATUS #354.
  - HYPOTÉZA mechanismu: proud do zátěže 1PPS (LED přijímače, buffer) ovlivní přes napájení nebo zem zpoždění cesty CLK4 → A, a to na straně zdroje, nebo vstupu A. Rozhodne prohození kabelů (STATUS #352).
  - Vzor tříd má za 1 s nulový součet (hranice posunutá o +δ = jedno okno delší, následující kratší). Na τ ≥ 1 s se vliv tedy neprojeví, při 0,25 s jde o ~1–2 % rozptylu okna.
  - ⚠️ Poloha mřížky oken proti 1PPS je po každém startu jiná: počátek buněk dává první záznam, který STM uvidí (`fpga_v3.c:594`). Věta „výchozí stav (TP posun 0) je ten dobrý“ výše tedy platí jen pro tento běh desky.
  - **N3, 6 h, A ,042 / B ,041, `gps tp off`, sken posunu A−B (koše 5 ns, `N3_10MHz_bez1PPS_scan_bin5.txt`):** složka 10 ns A 55–153 ps, B 96–159 ps; zbytek A 16–53 ps, B 20–61 ps; A−B MAD 108–202 ps (max při posunu 10 ns, dál při 30, 70 a 20 ns); kolísání střední chyby p-p 2,8 / 2,3 ps. noc18 s 1PPS: A 37–139 ps, B 63–171 ps, MAD 104–185 ps, p-p 4,7 / 3,9 a 3,4 / 2,9 ps. → ✅ **Hrby A−B nejsou od 1PPS.** ⚠️ Vstup 1PPS přitom nebyl tichý: přišlo ~4 240 falešných hran za 6 h (STATUS #355).
  - **N4 (opakování z050, z300, z125, z000 po N3) NEPOROVNATELNÉ:** přeprogramování generátoru (na N3 a zpět) posunulo fázi A−B ze **7,0 ns** (všech 46 snímků `fpga ev` v K, N1 a N2: 6,6–7,3 ns mod 100 ns) na **29,0 ns** (28,7–29,2 ns, za 2,75 h beze změny, kmitočty tedy shodné). V tomto stavu:
    - zbytek rms A 5,4 → 7,3 ps, B 5,6 → 8,2 ps; σ(MAD) A−B 94 → 144 ps;
    - čára 1 s je i v kanálu B, s opačnou fází než v A (vazba A → B závislá na posunu);
    - střední chyba okna A +0,09 až +0,27 ps, B −0,25 až +0,01 ps.
    Průměrné kmitočty obou kanálů sedí na ±0,01 mHz. Posun mediánu A−B v `report.txt` (+5 → −25 ps) je jen jiný tvar rozdělení. Kanál B navíc od přeprogramování generátoru potřebuje v každém segmentu opravu X0 o −12 160 hran (STATUS #353).
  - Střední chyba okna v N1 a N2 (16 etap, A−B 7 ns): A +0,06 ps, B +0,06 ps (2,3·10⁻¹³). Mezi etapami se liší o ±0,15 ps, víc než SE etapy 0,055 ps (k #348).
  - Etapa z175 se nezměřila. V N4 ji vyloučil časový limit a samostatná dohra se spustila ze špatného adresáře (L-0257). Pokrývá ji ale noc16b S3 z175 (0,64 ps) ≈ z425 (0,58 a 0,74 ps).
  - Doplněno 2026-11-05 večer: uživatel změřil průběh napětí (3,3 V) při 1PPS – je v pořádku, napájení jako cesta vlivu je vyloučené. HYPOTÉZA o proudu do zátěže 1PPS přes napájení tím padá; zbývá vazba po desce nebo uvnitř FPGA.
  - Opravy v0.44.1 (⬜ neověřeno na HW, STATUS #352–#354, ověření nocí `noc19.py`): výchozí pulz 1PPS 10 ms; mřížka oken se zarovnává na 1PPS (1PPS na 400 ‰ okna = 100 ms u 0,25 s, `fpga v3 pps`), poloha 1PPS v okně je ve výpisu `fpga v3`; skutečný skok X0 (zápis generátoru vypne výstup na ~1,2 ms) se už neopravuje donekonečna (L-0258).
- 🔑 **Noc 2026-11-05/06 (noc19, 13 etap po 36 min, CM7 v0.44.2, FW 0x050A, kabely CLK_4 → A, CLK_2 → B, oba ,041 Hz, `gpsdo off`; `noc_logs/_rozbor/noc19/souhrn.txt`, skript `noc19.py`, rozbor `noc19_cary.py` → `noc19/cary.txt`; STATUS #358–#360):**
  - **1PPS v kanálu A (pulz 10 ms):** j32 A 0,08–0,34 ps ve všech etapách = pozadí (bez 1PPS 0,07–0,27). Nezáleží na poloze 1PPS v okně (V2: 0–150 ms; p000 má hranici okna uvnitř pulzu), na náběžné hraně vůči hranici segmentu (F1–F5: 0,2–24,2 ms; F4 má značku pod vysokou úrovní) ani na pulzu 1 / 10 / 20 ms. → ✅ Výchozí pulz 10 ms (v0.44.1) vliv odstraňuje. Zarovnání mřížky (v0.44.2) drží polohu přesně na cíli (18 posunů, X0 0); pro pulz 10 ms potřeba není.
  - **Čáry jsou z času FPGA, ne z 1PPS:** konec okna leží vůči UTC sekundě na τ = −(poloha 1PPS + z) mod 250 ms (přesně, z `fpga v3`); t_unix určuje jen bod mřížky. V této soustavě má každá čára ve všech 13 etapách stálou fázi na skutečné frekvenci: B 2,5 Hz → j48 1,56–1,69 ps (sd fáze 2° za 8 h), B 5 Hz → j32 0,45–0,66 ps (dosavadní „čára 1 s v B“), A/B 1,25 Hz → j40 0,42–0,86 ps v protifázi, A 0,3125 a 0,9375 Hz → j10/j30 ~0,5 ps, B 0,625 Hz → j20. Periody = 8, 16, 32, 64, 128 segmentů; model vlastní fáze (K 260) je neodstraní. HYPOTÉZA: kruh záznamů v BSRAM (adresa = bity čísla segmentu). Do souhrnu noc19 se psaly jen j32/j64, nejsilnější čára (j48 B) v něm tedy chyběla.
  - **Střední chyba okna A podle parity posunu mřížky:** sudý +0,13 ps (5 etap), lichý −0,19 ps (8 etap), 13 ze 13; B +0,05 / −0,01 ps. LSQ přes těžiště segmentů nevyruší složku střídající se po segmentech (sudý počet bodů) → posun sklonu se znaménkem parity. Návrh: regresor parity v LSQ (STATUS #360).
  - ⚠️ Třídy UTC z t_unix (`val_lines.py`) přeskočí o okno, když latence SSE + odchylka hodin PC projde nulou (p200, p400, p500: −1 až −19 ms; večer +42 až +77 ms, p600 +135 ms). Fáze proto počítat z polohy 1PPS, ne z t_unix.
  - SEQ FPGA: díry = 1 na posun mřížky + 2 na `gen`, během měření 0 (STATUS #357, opraveno v0.44.3).
  - Restart v 06:07 (`--od V2_p700_t10`, přidání kontrol K1/K2) čekal na výchozí start 21:00 → V2_p700…K2 neměřeno, deska bez návratu do výchozího stavu (L-0261).
- 🔑 **Noc 2026-11-06/07 (noc20, 12 etap po 34 min, 22:42–05:46, CM7 v0.45.0, FW 0x050A, oba kanály z generátoru ,041 Hz (kabely jako noc19), `gpsdo off`, 1PPS pulz 10 ms; `noc_logs/_rozbor/noc20/souhrn.txt`).** Etapy: P1–P8 = člen parity LSQ ZAP/VYP při `fpga v3 pps 400` (posun mřížky 3 seg, lichý) a `500` (4 seg, sudý), dvakrát; K1/K2 = `gps tp off`; T1 = `fpga ab on`; V5 = reference.
  - **Zdraví běhu:** SPI 512 179 transakcí / 0 chyb, díry SEQ 0, I2C4 0 chyb, samokontrola 0 zamítnutých oken, TDC rezerva A 23 / B 28 tapů beze změny, teplotní rekalibrace 0×, chybějící okna 2 (K1). Okno ab_night: 8 139 oken na etapu, A−B sigma(MAD) ~190 ps.
  - **Člen parity (#360) ověřen:** střední chyba okna A [ps] – VYP: p400 +0,043 / +0,124, p500 −0,049 / −0,103 (rozdíl lichá − sudá +0,16 ± 0,07); ZAP: p400 −0,031 / +0,057, p500 +0,003 / −0,027 (rozdíl +0,025 ± 0,07, kritérium < 0,1 ps splněno). Odhad c (A) −0,07 → −0,13 ps (SE 0,017 po 25 432 oknech), B +0,12 → +0,02 ps; 0,61·c ≈ 0,08 ps sedí na polovinu rozdílu bez členu. Znaménko je proti noc19 opačné – parita mřížky se mění s každým startem (#348). B bez rozdílu (±0,08).
  - **Bez soustavné chyby:** průměr středních chyb 12 etap A +0,007 ps (3·10⁻¹⁴), B +0,003 ps (1·10⁻¹⁴) na okno 0,25 s. Zbytek rms A 8,8 / B 7,3 ps; jen P1 (prvních 35 min po zapnutí) 12,3 / 10,2 ps.
  - **Čáry B (#359) beze změny ve všech etapách, i bez 1PPS a s členem parity i bez něj:** j48 (0,67 s = alias 2,5 Hz) 1,61–1,94 ps, j40 (0,8 s) 0,77–1,02 ps, j32 (1 s = alias 5 Hz) 0,52–0,74 ps; šum spektra 0,08 ps. Třídy UTC B (+0,2 −0,6 −0,2 +0,6) stejné v K1/K2. Pozor: `gps tp off` vstup neutiší (#355) – K1 PSEQ +604, z toho 526 odmítnuto, K2 +272/195.
  - **Interval A→B (T1, `fpga ab on`, 34 min):** 30 424 intervalů (~15/s, tj. ~37 % segmentů), střední 27,583 ns, σ 353,7 ps, min 26,664 / max 28,577 ns. σ je řádově nad šumem, který dává zbytek LSQ okna (7–9 ps). HYPOTÉZA: fázové kolísání mezi dvěma zlomkovými děliči Si5356 (CLK2, CLK4 na ,041 Hz) – STATUS #364.
  - **σ značky TDC za běhu (#350):** ve všech etapách „koherentní signál“, odhad se nepočítá (generátor ,041 Hz je s taktem 100 MHz koherentní) – očekávané; ověřit na 11 603 104 Hz.

### 2026-11-06 18:43 – 20:57 — osciloskop RTM3004 + aktivní sonda RT-ZS10 (zem pružinou), rozvod hodin a 1PPS

Snímky: `Frequency_Counter_FPGA_Module/Measurement/*.PNG`. Místa podle uživatele a `FPGA_Module_2_1/GPSDO.kicad_sch`.

| místo | signál | Vpp | náběh | střída | poznámka |
|---|---|---|---|---|---|
| U16 MIC920 výstup (oddělovač OCXO) | 10 MHz | 1,87 V (0,85–2,72 V) | 22,8 ns, 65 V/µs | 46,5 % | lichoběžník; na prahu U17 strmost ~0,065 V/ns → 1 mV šumu ≈ 15 ps |
| U17 74LVC1G17 výstup (větev OCXO) | 10 MHz | 3,47 V | 3,0–3,6 ns | 50,4 % | rychle do 2,6 V, pak pomalý dojezd ~8 ns (zátěž) |
| U22 LMK1C1104 (J11–J14) bez 3,3 pF | 10 MHz | 3,62–3,69 V | 0,58–0,60 ns | 49,8–50,2 % | překmit ~+0,25 V |
| U22 LMK1C1104 s 3,3 pF (C88–C95) | 10 MHz | 3,57–3,70 V | 1,2–1,4 ns | 49,6–49,8 % | překmit beze změny, hrana 2× pomalejší |
| Si5356 generátor CLK2/CLK4 | 10 / 100 / 200 MHz | 3,42–3,45 V | 0,66–0,80 ns | 49,6–50 % | čisté, i 200 MHz v plném rozkmitu |
| pin modulu FPGA (Tang Nano), 100 MHz ref. | 100 MHz | 3,33 V | 2,4 ± 0,27 ns | 47,6 % | **schod na hraně ~1,3–1,5 V** (odraz), tj. v oblasti prahu LVCMOS33 |
| vstup U7 (J3 pin 2), 1PPS z NEO-7M | 1 Hz | 3,2 V, překmit do 4,17 V | 1,7 ns | 1,00 % | **pulz 10,0 ms = konfigurace TP5 ověřena** (v0.44.1); peak detect ~0,6 Vpp VF šumu na obou úrovních |
| výstup U7 → FPGA PIN33 | 1 Hz | 3,6 V | 0,62 ns | 1,00 % | zákmit do 2,65 V (nad VIH); ~0,3 Vpp šumu |

- Statistika osciloskopu `f` σ 217 kHz u 100 MHz je rozlišení měření periody přístrojem, ne jitter hodin.
- Schod na 100 MHz u pinu modulu je u sériově zakončeného vedení na mezilehlém bodě očekávaný; na kuličce FPGA (nedostupná) může být hrana čistá. Kdyby schod v oblasti prahu zůstal i na vstupu, zvyšuje citlivost hodin TDC na šum (STATUS #362).
- Generátor CLK2/CLK4 je koherentní s referencí: jako zdroj pro odhad σ značky (`TDC SUM ZNACKY`) se při 10/100/200 MHz zneplatní, použitelný je nesoudělný kmitočet (např. 11 603 104 Hz z `tdc cal gen`).

### 2026-11-07 08:42 – 11:16 — t365: interval A→B z generátoru CLK4 (A) / CLK2 (B), CM7 v0.45.1, 6 etap po 25 min

Skript `noc_logs/_rozbor/t365.py`, souhrn `noc_logs/_rozbor/t365/souhrn.txt`. Každou minutu `fpga ab 64`; „rychlý šum“ = sd rozdílů
sousedních intervalů / √2, „pomalý posun“ = sd středů minutových snímků. I = přesně 10 MHz (koherentní, fáze hrany vůči taktu stojí),
Z = +0,041 Hz (fáze se otočí za 24 s), Q = +0,25 Hz (za 4 s). Mezi etapami nový `gen`, takže se mění i fáze mezi CLK2 a CLK4.

| etapa | intervalů | střed | σ | rychlý šum | pomalý posun | tempo |
|---|---|---|---|---|---|---|
| I1 10 MHz | 30 088 | 46,433 ns | 123,3 ps | 120,2 ps | 16,7 ps | 20,00/s |
| Z1 ,041 Hz | 30 087 | 55,774 ns | 232,3 ps | 129,8 ps | 74,9 ps | 20,00/s |
| Q1 ,25 Hz | 30 086 | 7,871 ns | 206,5 ps | 154,0 ps | 92,6 ps | 20,00/s |
| I2 10 MHz | 30 087 | 1,240 ns | **30,2 ps** | 27,6 ps | 5,5 ps | 20,00/s |
| Z2 ,041 Hz | 30 097 | 13,086 ns | 199,2 ps | 128,4 ps | 63,2 ps | 20,00/s |
| Q2 ,25 Hz | 30 089 | 65,529 ns | 249,5 ps | 199,8 ps | 63,7 ps | 20,00/s |

- **Stojící fáze: šum závisí na tom, kde fáze stojí.** I2 = 30 ps, I1 = 123 ps, obojí stejný kmitočet; liší se jen poloha hran
  A a B vůči taktu (po `gen`). Šum značky tedy není konstantní, ale závisí na kódu TDC – podpora #347 (chyba závislá na fázi;
  příčinou rozdílu může být šířka binů v místě, kam hrany padnou, nebo blízkost hranice taktu). 30 ps je dolní mez, co sestava umí.
- **Rotující fáze:** rychlý šum Z 128–130 ps (obě opakování shodně), Q 154–200 ps → s rychlejší rotací roste, jako v t364.
  Pomalý posun 63–93 ps při rotaci proti 5–17 ps při stojící fázi = chyba značky závislá na fázi se při rotaci promítá do pomalé složky.
- **Střed se po každém `gen` mění** (1,2 / 7,9 / 13,1 / 46,4 / 55,8 / 65,5 ns): fáze CLK2–CLK4 po přeprogramování není stálá
  (potvrzeno, viz t364). Kalibrace zpoždění A↔B jen T-kusem z jednoho výstupu.
- **Tempo přesně 20,00 intervalu/s ve všech etapách, 0 zahozených startů** (40 segmentů/s). Ověření hypotézy CS (STATUS #364):
pokus 2026-11-07 11:17–11:21 (gen ,041 Hz, každý případ ~62 s od `fpga ab on`): `spi poll 40` + maska CS zap. → 1 239 intervalů (~20/s);
  `spi poll 100` (10 transakcí/s) → 1 857 (~30/s); `spi poll 40` + `fpga diag mask off` → 1 237 (~20/s). **Potvrzeno: každá SPI transakce
  stojí jeden segment (40 − transakcí/s; při `spi poll 40` jde reálně ~20 transakcí/s).** Mechanismus přes příznak CS (FLAGS bit3) tím
  ale potvrzen NENÍ: vypnutí masky tempo nezměnilo – buď `fpga diag mask` na FW v3 nepůsobí, nebo se segment ztrácí jinde
  (např. na hranici dávky záznamů mezi dvěma čteními). Další krok: v `fpga_v3_pr_rec` spočítat zahozené segmenty podle důvodu
  (CS / INVALID / mezera / pořadí), pak opravit.

### 2026-11-07 12:08 – 12:41 — t366: šum intervalu A→B při stojící fázi podle délky intervalu (CM7 v0.45.1)

Skript `noc_logs/_rozbor/t366.py`, souhrn `noc_logs/_rozbor/t366/souhrn.txt`. 12× `gen 2/4` přes 10 000 001 Hz na přesně 10 MHz
(každý `gen` = náhodně jiná fáze CLK2–CLK4), 20 s ustálení, 2 min `fpga ab` (~2 450 intervalů). Otázka z t365 (σ 30 vs 123 ps):
roste šum s počtem period taktu mezi A a B (hodiny), nebo závisí jen na místě v TDC řetězu?

| interval | period taktu 10 ns | σ | rychlý šum |
|---|---|---|---|
| 2,547 ns | 0,25 | 67 ps | 65 ps |
| 2,903 ns | 0,29 | 64 ps | 53 ps |
| 7,025 ns | 0,70 | 89 ps | 72 ps |
| 16,1 / 19,7 ns | 1,6 / 2,0 | 147 / 131 ps | 141 / 110 ps |
| 56,3 – 60,6 ns (7 běhů) | 5,6 – 6,1 | 113 – 136 ps | 103 – 139 ps |
| 90,4 ns | 9,0 | 107 ps | 110 ps |
| (t365 I2) 1,24 ns | 0,12 | 30 ps | 28 ps |

- **Rozhoduje, jestli A a B leží ve stejné periodě taktu, ne délka intervalu ani poloha v řetězu.** Do jedné periody 30–89 ps,
  od 1,6 periody 107–147 ps a dál už nestoupá (2 periody 131 ps, 9 period 107 ps). Poloha hran v řetězu to není: 60,397 ns
  (hrany v periodě jen 0,4 ns od sebe) dává 132 ps, kdežto 2,547 ns (2,5 ns od sebe) 67 ps.
- Výklad (HYPOTÉZA): každá hrana taktu, proti které se značka měří, má vlastní nezávislou chybu (bílá, nehromadí se). Ve stejné
  periodě se odečte, v různých ne: σ_takt ≈ √((130² − 65²)/2) ≈ **80 ps na hranu**. Na Si5356 je to mnoho; kandidáti: schod na hraně
  100 MHz u pinu modulu v oblasti prahu (#362, šum → čas), PLL/rozvod hodin ve FPGA, nebo hranice hrubého a jemného času v TDC.
- Uvnitř jedné periody zbývá složka závislá na poloze (30–89 ps, #347).
- Ověření: T-kus z jednoho výstupu a dvě délky kabelu (zpoždění < 10 ns a > 10 ns); jestli kód FPGA vede 100 MHz přes PLL;
  osciloskopem hrana 100 MHz co nejblíž FPGA.

### 2026-11-07 13:09 – 15:18 — t368/t369/t370: interval A→B při posunu A a B o ~6 ns, externí OCXO přes T-kus (CM7 v0.45.2 – v0.45.4)

Zapojení: externí OCXO (10 000 002,99 Hz proti naší referenci) přes T-kus na A i B, B přes +1 m RG174. Reference t367 se stejně
dlouhými kabely (zpoždění ~0,45 ns): rychlý šum ~103 ps. Skripty `noc_logs/_rozbor/t368.py`, `t369.py`, `t370.py`,
rozbor složení podle fáze `t368_fold.py`, souhrny v `noc_logs/_rozbor/t368/`, `t369/`, `t370/`.

| běh | nastavení | intervalů | střed | σ |
|---|---|---|---|---|
| t368 S1 | `spi poll 40`, maska CS zap., 60 min | 150 310 | 93,651 ns | 1 631 ps |
| t368 S2 | `spi poll 100`, 15 min | 37 568 | 93,729 ns | 1 632 ps |
| t368 S3 | `spi poll 40`, maska CS vyp., 15 min | 37 574 | 93,680 ns | 1 634 ps |
| t369 | v0.45.3, 10 min | 24 212 | 93,485 ns | 1 653 ps (vše „přes hranu taktu“) |
| t370 | v0.45.4, 20 min | 48 418 | 93,487 ns | 1 656 ps |

- **SPI na tom nemá podíl:** S1–S3 shodné; páry všechny platné, 0 mezer, 0 „A bez B“, 0 zahozených kvůli CS; FLAGS bit3 = podíl
  segmentů s transakcí (50 % při poll 40, 25 % při poll 100, i s maskou vypnutou – L-0267).
- **Chyba je deterministická, ne šum:** hodnoty se opakují s periodou 4 záznamů (např. 95,5 / 92,6 / 91,8 / 93,4 ns). Fáze 10 MHz
  proti taktu 100 MHz se točí 29,9×/s, při 40 záznamech/s je to 0,75 otáčky na záznam (`t368_fold.py`: r = 0,7500).
- **t370 (v0.45.4, interval podle fáze STARTU v periodě taktu, 16 dílů):** odchylka středu od celku −1,87 ns (fáze 0,25–0,31)
  až +2,24 ns (fáze 0,75), tedy **špička–špička ~4,1 ns**; uvnitř dílu σ 100–300 ps, v přechodu (0,375–0,5) 590–970 ps.
- **Naměřené fáze startu ani stopu nejsou rovnoměrné**, přestože skutečná fáze se točí rovnoměrně: díl 0,375 má 10 633 intervalů,
  díly 0,875 a 0,9375 žádný. Fáze stopu sleduje fázi startu nelineárně (start 351 → stop 495 ‰, 543 → 162 ‰). Interval = 9 period
  + (fáze stopu − fáze startu), takže celá chyba je v naměřených fázích hran, které se kupí u ~0,35–0,45 periody taktu.
- Kalibrace TDC z ring oscilátoru přitom vypadá zdravě (`tdc`: největší bin A 195 ps, B 150 ps, rezerva řetězu 23 / 28 tapů) a
  INL ±2 ns by v hustotě kódů vidět byla. `tdc cal pin` na tomto signálu kontrola správně odmítla (10 MHz je skoro soudělné se
  100 MHz, pokrytí fáze 275 ‰), tabulka beze změny.
- Výklad (HYPOTÉZA): **rušení synchronní s taktem 100 MHz na vstupu posouvá průchod hrany prahem**; časová chyba = napětí rušení /
  strmost hrany. Externí OCXO dává pomalou hranu (sinus), proto ±2 ns; generátor Si5356 (strmé CMOS hrany) dával tutéž složku
  10 ns jen ~100–200 ps (1. harmonická periody taktu, #347, viz výše). Se stejně dlouhými kabely mají A a B stejnou fázi a chyba
  se v rozdílu odečte (t367: 103 ps). Táž podstata jako „schod na hraně 100 MHz v oblasti prahu“ (#362) a „~80 ps na hranu“ (t366).
- Ověření: (1) přehodit +1 m kabel na A – tvar chyby se zrcadlí, (2) OCXO přes tvarovač (strmá hrana) – chyba musí klesnout
  úměrně strmosti, (3) změnit amplitudu sinusu (útlumový článek) – chyba roste s 1/amplituda, (4) osciloskopem 100 MHz na vstupu.

### 2026-11-07 15:44 – 16:04 — t370b: totéž s EXTERNÍM GPSDO místo slabého OCXO (CM7 v0.45.4)

Uživatel: OCXO dávalo slabý signál, nahrazeno externím GPSDO (10 MHz, −0,85 ppb proti naší referenci → fáze proti taktu se otočí
~1× za 12 s), B dál +1 m RG174. `t370.py 20` (⚠️ přepsal `t370/snimky.txt` běhu s OCXO; jeho čísla jsou výše a v `souhrn.txt`).

| | OCXO (t370) | externí GPSDO (t370b) |
|---|---|---|
| interval | 93,49 ns (B předbíhalo) | **5,964 ns** (B pozdější o 1 m) |
| σ celkem | 1 656 ps | **292 ps** |
| ve stejné periodě / přes hranu | – / 1 656 ps | 180 / 236 ps (n 17 067 / 31 365) |
| odchylka středu podle fáze startu | −1,87 … +2,24 ns | **−0,44 … +0,32 ns** |
| σ uvnitř dílu fáze | 100 – 970 ps | **84 – 171 ps** |
| počty v dílech (16 dílů) | 0 – 10 633 | 2 272 – 4 509 (rovnoměrné) |

- **Slabý signál OCXO byl hlavní příčinou ±2 ns** (pomalá hrana → velká časová chyba z rušení / prahu). S GPSDO je rozkmit 5,4× menší.
- **Zbývá složka podle fáze ~0,76 ns špička–špička**, tvar 1. harmonické periody taktu (minimum u fáze 0, maximum u 0,5). Při
  posunu A–B o δ = 0,6 periody dává chyba na hranu e(φ) = a·sin(2πφ) v rozdílu 2a·sin(πδ) ≈ 1,9·a → **a ≈ 200 ps na hranu**,
  společně oběma kanálům (se stejně dlouhými kabely se odečte, t367 103 ps). Tutéž velikost měla „složka 10 ns“ z testů
  s ujíždějící fází (#347, 85–220 ps).
- Po odečtení tvaru zbývá σ ~100 ps = tatáž hodnota jako se stejnými kabely → **náhodná část je v pořádku, dominuje systematická
  nelinearita převodu fáze**.
- Proč ji kalibrace z pinu dosud neopravila: `fpga_v3_calpin_check` porovnává osminy histogramu z pinu **s tabulkou z ring
  oscilátoru** a odmítá nad ±100 ‰. Nelinearita 200 ps na hranu sama dává ±126 ‰ (derivace 0,2 ns·2π/10 ns), takže kontrola
  odmítala právě případ, kdy je kalibrace potřeba (dřívější −135 / +520 ‰). **v0.45.5: `tdc cal pin gen mez <100..900>`** povolí
  volnější mez jen pro jednu kalibraci (výchozí 100 ‰ beze změny). ⬜ neověřeno na HW.
- Další krok: kabely CLK_2 → A, CLK_4 → B (generátor Si5356), `tdc cal pin gen mez 400`, pak zpět GPSDO s +1 m na B a `t370.py 20`:
  rozkmit podle fáze musí klesnout z ~0,76 ns.

### 2026-11-07 16:15 – 16:35 — t370c: externí GPSDO, kabely PROHOZENÉ (+1 m na A) (CM7 v0.45.5)

Surová data předchozího běhu (+1 m na B) zachována v `noc_logs/_rozbor/t370/snimky_gpsdo_b1m.txt`, tento běh v `t370/snimky.txt`.
Interval 94,504 ns (A pozdější → páruje se s další hranou B), σ 309 ps (t370b 292 ps), 48 422 intervalů, 0 chyb a mezer.

| fáze startu [‰] | 0 | 62 | 125 | 187 | 250 | 312 | 375 | 437 | 500 | 562 | 625 | 687 | 750 | 812 | 875 | 937 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| +1 m na B (δ ≈ 0,60) [ps] | −444 | −343 | −245 | −279 | −273 | −16 | +201 | +302 | +318 | +296 | +260 | +204 | +171 | +19 | −198 | −383 |
| +1 m na A (δ ≈ 0,45) [ps] | −264 | −285 | −270 | −268 | −347 | −300 | −31 | +258 | +436 | +424 | +321 | +267 | +337 | +232 | −36 | −256 |

- Tvar i velikost (špička–špička 0,76 / 0,78 ns) zůstaly, náběh u fáze ~0,33–0,40, sestup ~0,81–0,88. **Chyba nepatří jednomu
  kanálu, kabelu ani odrazu na T-kusu**: je to SPOLEČNÁ funkce fáze hrany vůči taktu e(φ). V rozdílu e(φ+δ) − e(φ) rozhoduje jen
  δ mod 1 periody (0,60 a 0,45 jsou si blízko), proto se tvar nezrcadlil (můj předpoklad zrcadlení byl chybný).
- Tvar je spíš „schodovitý“ než čistý sinus (ploché úseky ±0,3 ns) → e(φ) může mít skok ~0,6–0,7 ns v jednom místě periody
  (HYPOTÉZA; místo hranice hrubého a jemného času / konec řetězu?). Rozhodne kalibrace z pinu (`tdc cal pin gen mez 400`,
  kabely CLK_2 → A, CLK_4 → B) a opakování t370b: chyba musí klesnout, nebo se ukáže, že tabulka TDC není příčinou.

### 2026-11-07 16:45 – 18:05 — t371/t372/t370d: kalibrace TDC z pinu (`tdc cal pin gen mez 400`, v0.45.5) a její účinek

- **`tdc cal pin gen mez 400` PROŠLA** (Si5356 CLK_2 → A, CLK_4 → B, 11 603 104 Hz, pokrytí fáze 0 ‰): A −124 ‰, B −319 ‰ proti
  tabulce z ring oscilátoru (opakování po nové kalibraci z ring oscilátoru: A −147 ‰, B +136 ‰), největší bin 217 / 178 ps.
  S výchozí mezí 100 ‰ by byla opět odmítnuta (L-0268).
- t371 (A→B ze Si5356 na 11,6 MHz, tabulka pin / ring / pin, 3 min každá): **nerozhodné** – generátor je zavěšený na naší
  referenci, fáze startu obsadily jen 8–9 dílů z 16 (mříž), rozkmit 825 / 575 / 682 ps. ⚠️ Skript na konci nastavil generátor na
  10 000 000,041 Hz místo původních 10 000 000,000 Hz; vráceno ručně.
- t372 (A = externí GPSDO, B = Si5356, pokus o přímý tvar e(φ) proti přímce v čase): **metoda nefunguje** – relativní fáze obou
  GPSDO putuje o jednotky ns za desítky sekund (56,4 → 59,0 → 53,4 ns za minutu, střední rozdíl jen ~5·10⁻¹¹), takže nezávislá
  „pravda“ chybí. Tvar chyby se dá měřit jen s A i B ze stejného zdroje (putování se v rozdílu odečte).
- **t370d: GPSDO přes T-kus, B +1 m, tabulka z PINU (po běhu stále `zdroj A pin, B pin`), 40 min, 96 852 intervalů:**

| | t370b (tabulka ring osc.) | t370d (tabulka z pinu) |
|---|---|---|
| σ celkem | 292 ps | **237 ps** |
| ve stejné periodě / přes hranu | 180 / 236 ps | **157 / 176 ps** |
| odchylka podle fáze startu | −444 … +318 ps (0,76 ns) | **−282 … +263 ps (0,55 ns)** |
| σ uvnitř dílu | 84 – 171 ps | 89 – 149 ps |

- Kalibrace z pinu chybu **zmenšila o ~28 %, ale neodstranila**; tvar zůstal (minimum u fáze 0,06–0,25, maximum 0,44–0,75).
  HYPOTÉZA: zbytek závisí na signálu (kalibrováno strmými CMOS hranami Si5356, měřeno hranou z GPSDO) – rušení taktem na vstupu
  × strmost hrany, jak ukázalo slabé OCXO. Ověření: kalibrace z pinu přímo signálem GPSDO není možná (10 MHz je skoro soudělné),
  ale A→B se Si5356 se ZÁMĚRNĚ rozladěným generátorem (např. 10 000 001 Hz na obou, T-kus z jednoho výstupu, +1 m) změří tvar
  pro strmou hranu; když tam rozkmit zmizí, zbytek u GPSDO patří tvaru jeho hrany.

### 2026-11-07 — t370e: Si5356 rozladěný o +0,041 Hz, CLK_2 → A, CLK_4 → B, kabely STEJNĚ DLOUHÉ (oprava: ne +1 m), tabulka TDC z pinu (CM7 v0.45.5)

Uživatel: Si5356 nemá výkon na T-kus, proto dva výstupy (stejné VCO, pevný vzájemný odstup). Generátor 10 000 000,041 Hz (na vstupu
10 000 000,039 Hz, fáze proti taktu ~0,4 otáčky/s), 20 min, potom vrácen na 10 000 000,000 Hz; tabulka po běhu stále `pin / pin`.
Surová data GPSDO + pin zachována v `t370/snimky_gpsdo_b1m_pin.txt`.

| | GPSDO, ring osc. (t370b) | GPSDO, pin (t370d) | **Si5356, pin (t370e)** |
|---|---|---|---|
| interval | 5,964 ns | 5,842 ns | 96,827 ns (B předbíhá o 3,17 ns = jen odstup výstupů CLK_2/CLK_4) |
| σ celkem | 292 ps | 237 ps | **128 ps** |
| rozkmit podle fáze startu | 0,76 ns | 0,55 ns | **0,28 ns** (−149 … +129 ps) |
| σ uvnitř dílu | 84 – 171 ps | 89 – 149 ps | 88 – 127 ps |

- **Se strmými hranami Si5356 je chyba podle fáze 2× menší než u GPSDO se stejnou tabulkou** a celkové σ (128 ps) je skoro jen
  náhodná složka (~100 ps uvnitř dílu). Tvar už není schod ±0,3 ns, zbývá nepravidelné zvlnění ±0,15 ns.
- Pořadí OCXO (slabý) 4,1 ns → GPSDO 0,55 ns → Si5356 0,28 ns odpovídá HYPOTÉZE, že chyba podle fáze je převážně **vlastnost
  vstupní hrany** (rušení taktem / práh × strmost), ne TDC. ⚠️ Odstup δ se mezi běhy liší (0,58 / 0,68 periody), vliv na
  amplitudu 1. harmonické ~5 %, závěr nemění.
- ⚠️ **Oprava (uživatel):** v t370e byly koaxy stejně dlouhé, +1 m na B je zapojen až pro t370f. Posun A–B (0,68 periody)
  dal sám odstup výstupů Si5356, takže A a B ležely v různých fázích taktu a srovnání s GPSDO (0,58) platí.
- Důsledek: přesnost A−B závisí na strmosti vstupního signálu; pro sinus/slabý signál je potřeba tvarovač (komparátor) před FPGA,
  což plánuje vstupní modul (`SY100ELT23L`). Kalibrace z pinu se strmým signálem opravuje část společnou TDC.

### 2026-11-07 19:17 – 20:05 — t370f + t373: Si5356 (2 výstupy), B skutečně +1 m RG174, po power-cyklu (CM7 v0.45.5)

Po power-cyklu: `version` v0.45.5, reset power-on, `fpga v3 test` PASS, `selftest` 16/16 PASS, SCPI/HTTP CM4 PASS, tabulka po
startu z ring oscilátoru; `tdc cal pin gen mez 400` prošla napoprvé (A −125, B −128 ‰). Generátor +0,041 Hz. Každé nové
nastavení `gen` dává náhodný odstup výstupů CLK_2/CLK_4, takže skutečný posun A–B (δ = interval mod 10 ns) je pokaždé jiný.

| etapa | tabulka TDC | interval | δ [periody] | σ | rozkmit podle fáze | σ v dílu |
|---|---|---|---|---|---|---|
| t370f, 20 min | pin | 0,563 ns | 0,06 | 118 ps | 0,25 ns | 79 – 104 ps |
| t373 R, 15 min | ring osc. (`tdc cal`) | 90,242 ns | 0,02 | 142 ps | 0,22 ns | – |
| t373 P2, 10 min | pin (nová, A −206, B −175 ‰) | 31,986 ns | 0,20 | 212 ps | 0,40 ns | – |
| (t370e, stejné kabely) | pin | 96,827 ns | 0,68 | 128 ps | 0,28 ns | 88 – 127 ps |

- **Kabel +1 m se v intervalu neprojevil předvídatelně** – převládá náhodný odstup výstupů Si5356 po každém `gen`. Dvakrát vyšlo
  δ ≈ 0 (A a B ve stejné fázi), takže srovnání tabulek pin / ring v t373 **nerozhoduje** (R i t370f mají δ blízko 0).
- σ a rozkmit nezávisí na δ monotónně (δ 0,20 → 212 ps, δ 0,68 → 128 ps) → chyba podle fáze NENÍ čistá 1. harmonická; pro
  tvar e(φ) je potřeba řízené δ.
- Kalibrace z pinu proti tabulce z ring oscilátoru: −124/−319, −147/+136, −125/−128, −206/−175 ‰ (A/B) – odchylka se mění
  hlavně s tabulkou z ring oscilátoru, proti které se měří (L-0268).
- Další krok (návrh): řízený posun fáze výstupu Si5356 (registry fáze MultiSynthu, krok Tvco/128 = 3,55 ps) a sweep δ po
  0,05 periody, 2 min na bod → přímo tvar chyby rozdílu e(φ+δ) − e(φ) pro strmou hranu, pin vs ring.

### 2026-11-07 20:23 – 22:16 — t374–t376: chyba časování JEDNOHO kanálu proti koherentnímu generátoru, přeslech mezi řetězci (CM7 v0.45.6/v0.45.7)

**Metoda (nová, v0.45.6 `fpga ev raw`):** generátor Si5356 běží ze stejného VCO jako takt FPGA (CLK0 = VCO/22), takže skutečné
hrany leží PŘESNĚ na přímce t_u = a + b·x (x = absolutní index hrany, který FPGA posílá). Odchylka značky od přímky podle fáze
taktu (t_u & 0x3FFF) = chyba časování JEDNOHO kanálu e_c(φ), bez druhého kanálu a bez náhodného odstupu výstupů. Perioda
z přímky 163 839,9993 jednotek = přesně podle děliče MultiSynthu (10 MHz + 0,041 Hz), poměr B/A − 1 = ±2·10⁻¹². Skripty
`noc_logs/_rozbor/t374.py` (sběr přes trvale otevřený COM10, 0 ztracených událostí), `t374_xtalk.py`, `t375*.py`, `t376*.py`.
Zapojení: CLK_2 → A, CLK_4 → B (+1 m RG174).

| etapa (8 / 5 min) | tabulka TDC | e_A špička–špička | e_B š–š | šum v dílu A / B | σ(B−A) → po korekci z 1. půlky dat |
|---|---|---|---|---|---|
| t374 A_sam (B vyp.) | pin | 422 ps | – | 95 ps | – |
| t374 B_sam (A vyp.) | pin | – | 297 ps | 101 ps | – |
| t374 AB (oba +0,041 Hz, odstup 0,48 periody) | pin | 500 ps | 306 ps | 98 / 105 ps | 224 → **159 ps** |
| t374 AB_jiny (B +0,113 Hz) | pin | 507 ps | 397 ps | **211 / 226 ps** | – |
| t374 AB_ring (odstup 0,33) | ring osc. | 295 ps | 439 ps | 96 / 101 ps | 223 → **153 ps** |
| t375 G_on / G_off (CLK6/7 200 MHz zap / vyp) | ring osc. | 335 / 338 ps | – | 95,2 / 95,2 ps | – |

- **Rozdíl B−A tehož segmentu je beze zbytku vysvětlen modelem e_B(φ_B) − e_A(φ_A)** (16 dílů, shoda ±10 ps ve všech etapách
  se stejným kmitočtem). Korekce odhadnutá z 1. poloviny dat zlepšila 2. polovinu z ~224 na 153–159 ps ≈ náhodná složka
  (√(98² + 105²) = 144 ps).
- **Značky při SPI transakci (CS) mají stejný šum jako bez ní** (např. 148 / 150 ps) a **CLK6/7 (200 MHz = 2× takt) vliv nemá**.
- **Přeslech mezi kanály (t374 AB_jiny, `t374_xtalk.py`):** běží-li B na jiném kmitočtu, šum značek uvnitř dílu stoupne
  u OBOU kanálů z ~100 na 211–226 ps. Chyba A podle odstupu nejbližší hrany B je hladká funkce ±330 ps se DVĚMA cykly na periodu
  100 ns (minimum ~5–7 ns po hraně B, maximum ~25–29 ns po ní, náběžná i sestupná hrana B). t376 `t376_2d.py`: je to skoro jen
  funkce vzájemné fáze A–B (rms 170 ps z 174 ps), na fázi vůči taktu závisí málo.
- **t376 – kde přeslech vzniká (fáze A vůči průběhu B z přesné periody B, nepotřebuje značky B):**

| režim | rms přeslechu na A | šum v dílu |
|---|---|---|
| pinB (normálně) | 174 ps (š–š 648 ps) | 119 ps |
| skew (`fpga pw cal`: TDC B měří pin A, signál B dál na pinu B) | **33 ps** (jen dvě úzké špičky ±100 ps, když hrana B padne na hranu A) | 97 ps |
| Boff (CLK4/5 vypnut) | 8 ps | 93 ps |

  → **přeslech vzniká uvnitř FPGA činností řetězce TDC kanálu B**, ne fyzickou přítomností signálu na pinu B, ne na desce ani
  v Si5356. HYPOTÉZA: řetězec se přepíná na obou hranách 10MHz signálu (20 MHz) a zvlnění napájení / země jádra mění zpoždění
  sousedního řetězce. Ověření: FPGA instance (rozmístění řetězců, napájení, omezení aktivity řetězce mimo časované hrany).
- **skew = oba řetězce na TÝCHŽ hranách:** B − A σ **70 ps** a jen ±60 ps podle fáze (š–š 120 ps). Náhodná složka značky
  (~95–105 ps proti generátoru) je tedy z ~50 ps nezávislá v každém řetězci a z ~80 ps SPOLEČNÁ oběma (stejná hrana taktu) –
  přesně „~80 ps na hranu taktu“ z t366 (ve stejné periodě taktu se odečte). Kandidát: hrana 100 MHz na pinu FPGA (#362).
  Velká chyba podle fáze (300–650 ps) je u obou řetězců na týchž hranách podobná (rms rozdílu tabulek 60–66 ps).
- **Tabulky e_A při stejné kalibraci TDC jsou stálé** (fw `fpga lin cal`, Boff, pinB, skew: rms rozdílu 16–48 ps), **e_B
  ne**: kanál B (pin B, výstup CLK4, 1 m kabel) měl při `fpga lin cal` rozkmit 658 ps, jinde 297–439 ps; řetězec B měřící pin A
  má chybu podobnou A. Chyba kanálu závisí i na cestě signálu / tvaru hrany (srov. slabé OCXO ±2 ns).
- **t375b – korekce ve firmwaru (v0.45.7 `fpga lin cal 300` + `fpga lin on`) obecně NEPOMÁHÁ:** V1 (odstup 0,63) σ 232 →
  234 ps, V2 (0,73) 194 → 183 ps. Firmware korekci uplatňuje přesně podle návrhu (změřené „s korekcí“ = „bez“ − předpověď
  z tabulek, shoda na desítky ps), ale předpověď z tabulek zachycených při jiném nastavení generátoru na skutečnou chybu nesedí
  (hlavně kanál B). Korekce se proto sama nezapíná (`fpga lin on` jen pro pevné uspořádání, ve kterém byla tabulka změřena).
- t375 (první pokus) neplatný: skript čekal na konec `fpga lin cal` jen 3 s ticha (průběh se vypisuje po 30 s), poslal další
  příkazy a vyprázdněním vstupu zahodil výsledek; kalibrace navíc odmítla 120 s jako málo dat (počty v dílech se liší až 5×,
  ~150 diskrétních hodnot TDC na periodu) → výchozí 300 s, mez 20 v dílu (L-0270).
- **Rozpočet chyby jednorázového intervalu A–B se strmými hranami (CM7 v0.45.7, FPGA 0x050A):** náhodně ~50 ps na řetězec
  + ~80 ps společně (hrana taktu); systematicky e_c(φ) 300–650 ps š–š (závisí na kalibraci TDC i cestě signálu); přeslech
  B → A ±330 ps podle vzájemné fáze. Se slabou / pomalou hranou e_c(φ) roste (OCXO ±2 ns).

### 2026-11-07 22:52 – 23:17 — t378: odezva kanálu A na JEDNU hranu signálu B (přeslech mezi řetězci, CM7 v0.45.8)

A = CLK_2 10 MHz + 0,041 Hz (přesná přímka), B = CLK_4 na nízkém kmitočtu, takže hrany B jsou daleko od sebe. Odchylka značek
A (minus e_A(φ)) podle času od poslední náběžné hrany B (z přímky značek B). `noc_logs/_rozbor/t378.py`, profily po 5 ns
v `t378/<etapa>_odezva.txt`. 8 min na etapu, 19 250 značek A.

| B | perioda | profil po 10 ns od náběžné hrany B [ps] (průměr profilu = 0) |
|---|---|---|
| 1 MHz | 1000 ns | −13 **+134 +343 +282 +83** −15 +51 **+193 +179 +72** −19 … (~−20 až 500 ns) … +28 **+111 +274 +323 +110** −47 … |
| 2 MHz | 500 ns | −40 **+122 +318 +258 +64** −36 +23 **+174 +170 +48** … (~−50) … +67 **+261 +302 +70** … |
| 5 MHz | 200 ns | −94 +47 **+230 +171** −26 −137 −77 +78 +64 −63 −76 −19 **+193 +247** +19 −118 … (překryv) |

- **Po náběžné hraně B je značka A pozdě**: hrb ~+360 ps (proti klidu) s vrcholem 20–30 ns po hraně, druhý ~+210 ps v 70–90 ns,
  po ~100 ns klid. **Po sestupné hraně jen první hrb** (~+350 ps v 20–35 ns). Žádné záporné zákmity – jen zpoždění.
- Výklad (HYPOTÉZA): první hrb = přepnutí řetězce B a jeho 268 vzorkovacích FF (je u obou hran), druhý = zpracování
  ČASOVANÉ hrany (dekodér, tabulka, součty v DSP – jen náběžná). Proudový impulz se přes napájení FPGA projeví jako pokles
  napětí se zpožděním ~25 ns a zpomalí řetězec A nebo rozvod taktu. Ověření: osciloskop na napájení jádra Tang Nano při
  signálu na B; t379 (vzdálenost řetězců na čipu).
- **Důsledek i pro měření kmitočtu:** běží-li na A a B různé kmitočty, přeslech moduluje značky A rozdílovým kmitočtem
  (spur / zvýšená podlaha ADEV kanálu A, když je na B jiný signál) – patří do #367.
- Rozložení na čipu dnes: řetězy A R26, B R27 (sousední řádky, `pins.cst`). Zkušební bitstreamy s B v R9 / R20 jsou
  sestavené (časování i rozmístění PASS), ale nahrát je nešlo: JTAG kabel Tang Nano není připojen k PC → `t379_fpga.py`.

### 2026-11-07 23:17 – 2026-11-08 05:15 — t377: korekce `fpga lin` při STÁLÉM odstupu A–B, 6 h (CM7 v0.45.8)

Generátor nastaven JEDNOU (CLK_2 → A i CLK_4 → B 10 MHz + 0,041 Hz, B o 1 m delší koax) a celou noc se nepřeprogramoval,
takže odstup výstupů zůstal stejný. Každou hodinu `fpga lin cal 300 keep` (nová tabulka při témže odstupu), pak 5× [5 min
`fpga ab` korekce VYP, 5 min ZAP]. 7 cyklů, 32 bloků VYP + 32 ZAP po ~11 640 intervalech. `noc_logs/_rozbor/t377.py`,
souhrn `t377/souhrn.txt`. Teplota desky 29,3 → 28,4 °C, OCXO 47,4 → 46,8 °C, modul FPGA ~37,2 °C; kalibrace TDC během noci
žádná (stále č. 2).

| | korekce VYP | korekce ZAP |
|---|---|---|
| střední interval A→B | 0,275 ns (0,258 … 0,293, sd 10,5 ps) | 0,294 ns (0,286 … 0,306, sd 5,2 ps) |
| σ intervalu | **104,0 ps** (98,4 … 108,5) | **94,4 ps** (86,9 … 100,3) |
| rozkmit středu podle fáze startu (16 dílů) | **203 ps** (197 … 209) | **22 ps** (16 … 30) |

- **Korekce funguje, pokud se nezmění zapojení ani nastavení generátoru:** rozkmit podle fáze klesl ~9×. Odstraněná část
  σ je √(104,0² − 94,4²) = 44 ps; zbylých ~94 ps je náhodný šum značek (šum uvnitř dílu tabulky 98–112 ps na kanál).
- **Chyba podle fáze je stabilní v čase:** profil bez korekce (16 dílů) se během 6 h liší od průměru nejvýš o **6 ps**
  (C01 +111 +71 +35 +80 … proti C07 +114 +70 +32 +80 …), při změně teploty desky o 0,9 °C. Hodinová rekalibrace tabulky
  tedy není nutná; tabulka z C01 by platila celou noc (tabulky C01..C07: A 367–397 ps, B 433–459 ps špička-špička).
- Se zapnutou korekcí zůstává ve zbytku malý systematický člen: nejvyšší díl fáze startu je vždy +11 … +19 ps a rozkmit
  během hodiny po kalibraci mírně roste (C06 23 → 30 ps). Je to pod šumem; HYPOTÉZA: lineární interpolace tabulky
  (64 dílů po 156 ps) nezachytí skok chyby u přechodu fáze taktu 16383 → 0.
- σ v čase kolísá s periodou ~20 min o ±4 ps (VYP 108/100/107/102/107, ZAP 99/91/100/90/97), nezávisle na čísle bloku –
  příčina neznámá (TODO, kandidáti: smyčka GPSDO / dither DAC, teplotní regulace OCXO).
- Střed se s korekcí posune o +19 ps (HYPOTÉZA: rozdíl středních hodnot tabulek A a B váhovaných rozložením fází).
  Tabulka odstraňuje jen závislost na fázi, absolutní nulu intervalu neurčuje.

### 2026-11-08 05:16 – 05:52 — t380: korekce `fpga lin` při VELKÉM odstupu A–B (CM7 v0.45.8)

Jako t377, ale výstup CLK4 přepsán tak, aby A a B byly v různé fázi taktu: interval A→B 83,14 ns (mod 10 ns = 3,14 ns,
hned první pokus). `fpga lin cal 300 keep` (A 267 ps, B 445 ps špička-špička), pak 3× [5 min VYP, 5 min ZAP], ~11 640
intervalů na blok. `noc_logs/_rozbor/t380.py`, `t380/souhrn.txt`.

| | korekce VYP | korekce ZAP |
|---|---|---|
| σ intervalu | **236,7 ps** (234 … 240) | **162,5 ps** (155 … 168) |
| rozkmit středu podle fáze startu | **558 ps** | **35 ps** (29 … 39) |
| střed | 83,152 ns | 83,131 ns |

- **Při odstupu mimo stejnou fázi taktu je chyba podle fáze ~2,7× větší než v t377** (558 proti 203 ps): chyby kanálů
  A a B se už nevyruší, protože každý kanál je v jiné části své tabulky. Korekce ji stáhne ~16× a odstraní
  √(236,7² − 162,5²) = **172 ps** z σ.
- **Zbytkový náhodný šum je ale 162 ps proti 94 ps v t377.** Rozdíl √(162,5² − 94,4²) = 132 ps připadá na to, že značky
  A a B leží v RŮZNÝCH periodách taktu (83 ns = 8 period): šum hrany taktu, který se v t377 (obě značky v téže periodě)
  vyrušil, se tu sčítá. Sedí to s dřívějším odhadem společného šumu ~80 ps na značku (√2·80 ≈ 113 ps) –
  **šum hodin FPGA (nebo jejich rozvodu/napájení) je dnes největší zdroj chyby intervalu delšího než jedna perioda.**
  ⚠️ **Vyvráceno 2026-11-08** (záznam „osciloskop: hodiny 100 MHz…“ níže): přechod přes sousední hranu taktu přidá ≤ 31 ps,
  rozhoduje čas mezi značkami (přeslech t378 / napájení), ne počet hran taktu.
  HYPOTÉZA, ověření: osciloskop na CLK0 (Si5356 → FPGA) a na napájení jádra Tang Nano; měřit totéž s odstupem
  1, 2, 4, 8 period (jestli zbytek roste s délkou intervalu = fázový šum, nebo je konstantní = jitter hrany).
- Interval je 83 ns, ne ~3 ns: `fpga ab` párovala hranu A s hranou B o 8 period později – pozůstatek po přepisu CLK4
  (TODO: zkontrolovat pravidlo párování při odstupu blízko celé periody).

### 2026-11-08 08:45 – 12:02 — osciloskop: hodiny 100 MHz a napájení modulu Tang Nano + rozbor t377 „přes hranu taktu“ (STATUS #368, #369)

Snímky `Frequency_Counter_FPGA_Module/Measurement/` (RTM3004, aktivní sonda RT-ZS10, zem pružinkou), deska po power-cyklu,
zátěž jako v t380 nebyla nastavena. K tomu nový rozbor už uložených dat (t377 bloky, t374/t376 surové značky).

**Hodiny 100 MHz (Si5356 CLK0 → R66 33 Ω → pin 35 modulu):**

| místo | tr 10–90 % | schod na hraně | poznámka |
|---|---|---|---|
| výstup Si5356 (6. 11., `Si5356-out-100MHz`) | 0,66 ns | žádný | čistá hrana, 4 GV/s |
| R66 na straně U9 (`R66_CLK100MHz-*`) | 1,76 – 1,95 ns | **2,3 V po ~0,8 ns** | dělič R_out + R66 proti Z0 do návratu odrazu |
| pin 35 na hlavičce modulu (`FPGA_PIN35_CLK100MHz-*`) | 2,0 – 2,7 ns | **1,55 – 1,65 V po ~0,4 ns** (sestupná ~1,7 V) | Vpp 3,3–3,4 V, střída 47,6–48,6 % |

- Výklad (HYPOTÉZA): vedení má ~0,4 ns jedním směrem. Hlavička modulu je UPROSTŘED vedení (k pouzdru QFN vede ještě spoj
  modulu ~0,2 ns), takže schod 1,6 V je dopadající vlna před návratem odrazu od vstupu FPGA. Na vývodu čipu (otevřený konec)
  se vlna zdvojí → hrana bez schodu. Z výšek schodů Z0 ~75 Ω, R_out ~45 Ω (předpoklad: R66 měřen na straně U9). Ověření:
  hrot přímo na vývod 35 pouzdra QFN.
- Statistika 3 101 průběhů (6. 11., `FPGA_Ref_100MHz_clk`): σ periody ~22 ps = mez osciloskopu (svislý šum / strmost),
  skutečný jitter je menší.
- Snímky „Jitter_*“ jsou spouštěné na téže hraně (ne hrana o 80 ns dál). Šířka obálky na hranách odpovídá svislému šumu
  (~0,15 V š–š / ~1,5 V/ns ≈ 100 ps š–š), jitter z nich vyčíst nejde.

**t377 znovu: intervaly přes SOUSEDNÍ hranu taktu** (odstup 0,28 ns, start v posledních ~3 % periody; 32 + 32 bloků, n ~280–390 / blok):

| | ve stejné periodě | přes hranu taktu |
|---|---|---|
| korekce VYP: σ | 104 ps | **92 ps** (podmnožina fází s menší chybou) |
| korekce ZAP: σ | 94 ps | **99 ps** |
| korekce VYP / ZAP: střed | 0,274 / 0,288 ns | **0,314 / 0,335 ns (+40 / +47 ps)** |

- **Přechod přes hranu taktu přidá nejvýš √(99² − 94²) ≈ 31 ps → nezávislý bílý šum hrany taktu ≤ ~22 ps.** Hypotéza
  „~80 ps na hranu taktu“ (t366, t376, t380, STATUS #368) tím **NEPLATÍ**. Kdyby každá hrana měla 80 ps, intervaly přes hranu
  by měly √(94² + 113²) ≈ 147 ps.
- **Nová systematická chyba:** interval přes hranu taktu má střed o **+40 až +47 ps** výš (s korekcí i bez ní). Sedí s TODO
  „zbytek +15 ps v nejvyšším dílu fáze“ (t377). Chyba je soustředěná u přechodu fáze 16383 → 0 (hranice jemného/hrubého času nebo
  konce kalibrační tabulky TDC). TODO #369.
- Co tedy zvyšuje šum delších intervalů (t366: do 1 periody 30–89 ps, od 1,6 periody 107–147 ps; t380 83 ns: 163 ps)?
  **Čas mezi značkami, ne počet hran taktu.** Kandidát: přeslech z t378. Hrana jednoho kanálu zpozdí značky druhého o
  +360 ps za 20–30 ns a o +210 ps za 70–90 ns. V t380 leží značka B 83 ns po A (2. hrb) a značka A 17 ns po předchozí hraně B
  (náběh 1. hrbu). Velikost hrbu závisí na datech (počet překlopených FF) → náhodná složka. V t366 má každý delší interval
  aspoň jednu značku v hrbu, krátké (< 10 ns) ani jednu. Totéž by vysvětlil širokopásmový šum napájení jádra s korelační
  dobou ~10 ns. Rozhodne mapa σ podle odstupu (TODO #369, t381).

**Napájení modulu (AC, hrot na pinu modulu, zem pružinkou):**

| větev | 50 Hz š–š | další | VF šum |
|---|---|---|---|
| 1,2 V jádro | 18 – 23 mV (HR 22,8 mV) | – | peak detect 69 mV š–š, σ 13 – 14,5 mV (vč. 50 Hz) |
| 3,3 V (pin modulu, 3,40 V DC) | ~40 mV | pila ~20 kHz ~15 mV š–š (regulátor v PFM?) | σ 3 – 4 mV, ojedinělý zákmit ~20 mV |
| 5 V (pin 31) | ~35 mV | čáry 10 / 30 / 100 MHz ve FFT | σ 9 – 10 mV |

- **50 Hz NA ZPOŽDĚNÍ TDC NEPŮSOBÍ** (`scratchpad hum50.py` nad t374 A_sam / B_sam / AB, t376 Boff: značky jednoho kanálu proti
  koherentnímu generátoru po 25 ms, fs 40 Hz → 50/150 Hz se zrcadlí na 10 Hz, 100 Hz na 20 Hz). Čára na 10 Hz ≤ 4,5 ps
  amplitudy při pozadí 1,3 ps, na 20 Hz ≤ 5,5 ps. Skutečných 20 mV na jádře by podle odhadu citlivosti zpoždění CMOS
  (~0,08 %/mV, HYPOTÉZA) dalo desítky ps. 50 Hz na všech třech větvích včetně 1,2 V za regulátorem je proto nejspíš zemní smyčka
  osciloskop ↔ deska. Ověření: nulové měření (hrot na GND u pružinky).
- VF šum 1,2 V (σ ~13 mV) je na 10 mV/dílek s děličem sondy 10:1 blízko šumu osciloskopu – bez nulového měření se nedá
  oddělit. Rozhodující snímek z plánu (1,2 V spouštěné hranou B, průměrování) zatím chybí.
- Vedlejší: ve stejných datech jsou pomalé složky 0,010–0,025 Hz (20–45 ps, kanál B víc) a liché harmonické rozmítání fáze
  0,041 Hz (0,45 / 0,53 / 0,62 / 0,86 Hz, ~12 ps = jemná struktura e(φ) pod rozlišením 64 dílů). Na intervaly A–B vliv nemají.

### 2026-11-08 11:18 – 12:01 — t381: šum intervalu A→B podle odstupu A–B 0–100 ns (CM7 v0.45.8)

CLK_2 → A, CLK_4 → B (+1 m RG174), oba 10 MHz + 0,041 Hz, korekce `fpga lin` vypnuta. 30× přepis CLK4 (každý = jiný odstup,
tu vyšel krok ~1 ns a cyklus ~20 ns), pokaždé 75 s `fpga ab` (~3 040 intervalů). σ uvnitř dílu fáze = medián σ 16 dílů fáze
startu (bez chyby podle fáze). `noc_logs/_rozbor/t381.py`, `t381/souhrn.txt`.

| odstup d (B po A; A je 100 − d po B) | 8–10 ns | 11–13 ns | 28–33 ns | 48–53 ns | 58 ns | 68–73 ns | 88–94 ns |
|---|---|---|---|---|---|---|---|
| σ uvnitř dílu fáze | 100 – 117 | 124 – 143 | **149 – 177** | 133 – 165 | 162 | **154 – 186** | 98 – 143 (většinou ~105) |
| rozkmit podle fáze (š–š) | 216 – 357 | 305 – 634 | 337 – 838 | 380 – 449 | 379 | 356 – 861 | 257 – 422 |

- **Šum závisí na odstupu A–B se dvěma maximy (~30 a ~70 ns) a minimy u ~10 a ~90 ns**, souměrně podle d ↔ 100 − d. Sedí to na
  hrby přeslechu z t378 (hrana jednoho kanálu zpozdí značky druhého za 20–30 a 70–90 ns). Konstantní šum hrany taktu by dal
  plochý průběh → **potvrzeno: šum intervalů nad ~10 ns dělá přeslech mezi kanály uvnitř FPGA** (STATUS #369 bod 1 hotov).
- Nejlépe ~100 ps (≈ t377 ve stejné periodě). Nejhůř 186 ps a systematická chyba podle fáze až 861 ps (d ≈ 73 ns).
- Plochých ~135–145 ps u d ≈ 50 ns: obě značky jsou mezi hrby, ale ne v klidu (profil t378 tam +51 / +83 ps).
- Důsledky: pro jednorázový interval je přesnost dnes funkcí vzájemné fáze signálů A a B. Náprava je ve FPGA (t379 rozmístění,
  aktivita po zásahu nezávislá na datech). Ze strany STM jen omezení spínání nepoužitého kanálu (t383, předdělička).

### 2026-11-08 12:02 – 12:20 — t382: šum značky jednoho kanálu při stojící fázi – značky jsou NÁHODNĚ OPOŽDĚNÉ (CM7 v0.45.9)

Jen kanál A (CLK_2), CLK4/5 vypnut. INT0..2 = `gen 2 10000000` (celočíselný dělič, fáze proti taktu stojí, 3 různé fáze),
FRAC = +0,041 Hz. Surové značky `fpga ev raw` po 25 ms proti přímce generátoru. `noc_logs/_rozbor/t382.py`, rozklad
`scratchpad int_levels.py`.

| etapa | σ celkem | rozložení značek (odchylka od nejčastější hodnoty, podíl) |
|---|---|---|
| INT0 | 98,9 ps | 0 (63 %), +110 (19 %), +214 (10 %), +328 (6 %), +162 (1,4 %) |
| INT1 | 107,4 ps | 0 (45 %), +85 (30 %), +183 (13 %), +328 (11 %), +265 (1,7 %) |
| INT2 | 72,9 ps | 0 (73 %), +168 (12 %), +69 (3,7 %), +106 (3,2 %), −53 (2,3 %), +256 (2,2 %), −126 (1,9 %) |
| FRAC | 126,4 ps | σ uvnitř dílu fáze 93 ps |

- **Při stojící fázi dává značka jen několik diskrétních hodnot (kódy TDC ~100 ps od sebe), každou s rozptylem < 6 ps.** Šum
  ~100 ps tedy není gaussovský šum hrany: značka je buď „čistá“, nebo **opožděná o 85–330 ps** (ojediněle dřívější).
  Sousední vzorky nezávislé (lag-1 ≈ 0), bez závislosti na SPI transakci (záznamy při CS ±2 ps), bez signálu na B.
- Generátor (zlomkový MultiSynth) to není – celočíselný dělič má tentýž obraz.
- HYPOTÉZA: asynchronní spínání uvnitř FPGA. Nahraný bitstream je zkušební varianta **APP_OSC** (FPGA DIAG „koherence
  neplatné“): doména aplikace a SPI běží z vnitřního oscilátoru ~10 MHz asynchronně ke 100 MHz (`top_v3.v:61`), takže její
  proudové impulzy padají na hrany v náhodné fázi (hrb zpoždění ~30 ns po impulzu, t378). Ověření: t384 (stejné etapy
  s řádným bitstreamem bez APP_OSC, jen do SRAM).

### 2026-11-08 12:41 – 12:58 — t383: předdělička nepoužitého kanálu B proti přeslechu (CM7 v0.45.9) → ochrana v0.45.10

A = CLK_2 10 MHz + 0,041 Hz, B = CLK_4 10 MHz + 0,113 Hz (vzájemná fáze se točí = nesouvisející signál). Surové značky A,
σ uvnitř dílu fáze (64 dílů). `noc_logs/_rozbor/t383.py`, `t383/souhrn.txt`.

| předdělička B | σ uvnitř dílu | přebytek nad B vypnuto (kvadraticky) |
|---|---|---|
| 1 (dosud vždy u 10 MHz) | 211 ps | 189 ps |
| 4 | 143 ps | 108 ps |
| **16** | **128 ps** | **87 ps** |
| B vypnuto | 94 ps | – |

- Předdělička krmí řetěz TDC až signálem c[p−1] → řetěz B spíná P× méně často a přeslech na A klesne. Zbytek (87 ps) dělá
  vstupní čítač na plném kmitočtu (c[0]) a sonda /16 v doméně 100 MHz.
- **v0.45.10 – ochrana proti přeslechu (`fpga presc guard on|off`, výchozí ZAP):** v automatu dostane kanál, který se právě
  neměří (≠ zvolený kanál UI), P = 16, když jeho sonda ukazuje ≥ 1 MHz (vypnutí pod 0,5 MHz). Neuplatní se při událostech
  (`fpga ev`), párování (`fpga ab/pw`), `fpga lin cal` a sběru kalibrace TDC; `tdc cal pin` ji na 30 s pozdrží. Ověřeno
  na desce: P_B 1/16 při měření A, při `fpga ev on` 1/1, `guard off` 1/1, `fpga v3` řádek „ochrana proti přeslechu“.
  Cena: nečinný kanál měří s P 16 (méně hran v okně, ⬜ okno DVOJKANÁL a web ukazují B s menším rozlišením).

### 2026-11-08 13:05 – 13:27 — t385: přínos ochrany proti přeslechu pro kmitočet kanálu A (CM7 v0.45.10)

A = CLK_2 10 MHz + 0,041 Hz, B = CLK_4 10 MHz + 0,113 Hz. Bloky 5 min střídavě `fpga presc guard on` / `off`, kmitočet A
z `sdramlog dump` (brána 249 ms; dump vrátil jen posledních 200 měření = 50 s na blok). 2 ze 3 cyklů (3. přerušen kvůli
restartu PC). `noc_logs/_rozbor/t385.py`, `t385/souhrn.txt`.

| | ochrana ZAP (P_B 16) | ochrana VYP (P_B 1) |
|---|---|---|
| ADEV(0,25 s) | 4,51 / 5,19 ·10⁻¹⁰ | 5,61 / 6,27 ·10⁻¹⁰ |
| ADEV(1 s) | 2,21 / 2,24 ·10⁻¹⁰ | 2,64 / 2,61 ·10⁻¹⁰ |
| σ kmitočtu | 4,20 / 4,96 mHz | 5,42 / 5,97 mHz |

- **Ochrana zlepší ADEV kanálu A o ~17–20 % (0,25 s) a ~15 % (1 s)**, když je na druhém kanálu nesouvisející signál; obě
  dvojice bloků shodně. Malý vzorek (200 měření na blok) – opakovat s delším výpisem (`sdramlog dump` vrací nejvýš 200?).

### 2026-11-08 13:42 – 13:56 — t384: APP_OSC je hlavní zdroj náhodných zpoždění značek (CM7 v0.45.10)

Odpověď na t382 (při stojící fázi je značka opožděna o diskrétní 85–330 ps). Řádný bitstream **FW 0x0508 bez APP_OSC**
(doména aplikace/SPI z 10 MHz, koherentní se 100 MHz) nahrán do **SRAM** FPGA (`programmer_cli --run 2`, flash FPGA
nedotčena), změřeny značky kanálu A (B vypnut), pak rekonfigurace z flash (= návrat k APP_OSC). `noc_logs/_rozbor/t384.py`,
offline rozbor `scratchpad t384_ofs.py` (per-blokový fit, oříznuté glitche z rozjezdu generátoru).

| etapa | FW 0x0508 BEZ APP_OSC (SRAM) | flash APP_OSC (t382) |
|---|---|---|
| stojící fáze σ (3 nastavení) | **30 / 30 / 48 ps** | 99 / 107 / 73 ps |
| žebřík zpoždění (stojící fáze) | do +120 ps | **do +320 ps** |
| točící se fáze (FRAC) σ | 115 ps | 126 ps |

- **APP_OSC zhruba ZTROJNÁSOBUJE šum značky při stojící fázi** (30–48 proti 73–107 ps) a prodlužuje žebřík diskrétních
  zpoždění z ±120 na +320 ps. Potvrzeno: doména aplikace z vnitřního oscilátoru (asynchronní ke 100 MHz) je hlavní zdroj
  náhodných zpoždění z t382. Bez ní je značka z velké části „čistá“.
- Šum točící se fáze klesl jen o ~9 % (115 proti 126 ps), protože ten je ovládán chybou podle fáze e(φ) (žebřík 300–650 ps
  přejetý přes 64 dílů), ne diskrétními zpožděními.
- ⚠️ **Deska má dnes ve flash FPGA zkušební APP_OSC bitstream** (`TEST_FW_0x050A_v3_appOscS6`, FPGA DIAG „koherence 100/10 MHz
  neplatné“). Řádná cesta je bez APP_OSC. **Doporučení: sestavit a do flash FPGA nahrát řádný bitstream 0x050A BEZ APP_OSC**
  (dnes vydán jen 0x0508 b3fbc32; 0x0509/0x050A existují jen jako appOsc zkušební) a znovu změřit σ intervalu A→B.
- Technika: každé spuštění `programmer_cli` nechá viset proces držící FTDI kanál → další spuštění „Cable failed to open via
  the channel“. Před každým během programátor zabít (`Stop-Process programmer_cli`); rekonfigurace z flash `--run 1` vrací
  desku do výchozího stavu bez power-cyklu.

### 2026-11-08 14:00 – 14:56 — t386/t387: řádný bitstream BEZ APP_OSC – nižší šum značky, ale ROZBITÝ interval A→B (CM7 v0.45.10)

Na t384 (0x0508 bez APP_OSC do SRAM snížil šum značky) navázáno sestavením **řádného 0x050A bez APP_OSC** (`gw_sh
v3/build_v3.tcl`, bez `define APP_OSC`; brána `fpga_release.py --v3` PASS: setup +0,042 ns i s nejistotou 0,9 ns, CLS 80 %,
rozmístění TDC PASS, identita 60747e3). Seedový sweep `pnr_seeds run base 8`: **non-APP_OSC se zavírá špatně** – 3 z 8 seedů
záporná rezerva, medián +0,045 ns, nejlepší **@6 +0,141 ns**; systematická kritická cesta `u_tdca/lut_lut_0_0_s` (vyhledání
v kalibrační tabulce TDC) a `u_sb/fh` (FIFO okna kanálu B). Oba kladné seedy nahrány do SRAM a změřeny (t386 = seed +0,042,
t387 = @6 +0,141).

| metrika | flash APP_OSC | t386 (+0,042) | t387 @6 (+0,141) |
|---|---|---|---|
| šum značky uvnitř dílu (INT, stojící fáze) | ~24 ps | 7,8 / 29 / 25 ps | **40 / 3,4 / 20 ps** |
| šum značky FRAC (točící se fáze) | 93 ps | 150 ps | 102 ps |
| interval A→B, σ (6 odstupů) | **98–186 ps** | 22–56 **ns** | 3,7–37 **ns** |
| koherence 100/10 MHz (DIAG) | neplatné | OK | OK |
| TDC kalibrace | OK | OK | OK (největší bin A 292 ps, B 219 ps) |

- **Non-APP_OSC sníží šum značky (koherence obnovena, stojící fáze 3–40 ps proti 24 ps), ALE ROZBIJE interval A→B:**
  σ v desítkách ns, odlehlé hodnoty skoky o ~10 period taktu (ab2: interval 77 ns, max 177 ns). Vzniká, i když je TDC
  kalibrovaný a bez obřího binu. **Platí pro OBA kladné seedy** (0,042 i 0,141 ns) → není to výkyv seedu.
- Příčina: non-APP_OSC natlačí na doménu 100 MHz TDC, FIFO oken i SPI PHY. Půlka značek padá do SPI transakce (CS); na
  přetížené doméně se hrana během SPI aktivity občas přepočítá → párování A↔B chytne hranu o ~10 period dál. APP_OSC tomu
  předchází tím, že doménu aplikace/SPI odsune na vnitřní oscilátor – za cenu ~3× vyššího šumu značky.
- **VERDIKT: non-APP_OSC 0x050A NENÍ použitelná náhrada** – hlavní funkce přístroje (interval A→B) se rozbije. APP_OSC ve
  flash zůstává. Nižší šum značky bez rozbití A→B by vyžadoval zmenšit logiku na 100 MHz doméně (rezerva pro cesty TDC / okna /
  PHY) = větší zásah do FPGA (budoucí práce). Vydané zkušební bitstreamy: `ab_test/FW_0x050A_v3_20261108-1301_60747e3.fs`
  a `ab_test/TEST_FW_0x050A_v3_base_noAppOsc_s6_20261108-1332.fs` (NEnahrávat do flash FPGA).

### 2026-11-08 22:04 – 2026-11-09 15:50 — noc1109: 18hodinový běh, hradla 100 s / 10 s / 1 s, kanál A = externí GPSDO, B = CLK_2 (CM7 v0.45.14)

Jeden skript držel COM10 (`noc_logs/_rozbor/noc1109.py`, výstup `noc1109/`: `log.csv` 219 řádků po 5 min, `bloky.txt`, hodinové `raw_*.txt`,
rozbor `noc1109_rozbor.py`). Deska po power-cyklu 21:38, GPSDO od startu v LOCK. Bez zásahu, bez resetu.

**Robustnost (celých 18 h):** 0 resetů, GPSDO ve stavu LOCK celou dobu (y −0,29 … +0,12 ppb, od ustálení |y| ≤ 0,14 ppb, TIE −109 … +28 ns,
po ustálení ±28 ns), kód DAC 24969 → 24982 (+13 LSB za 18 h ≈ 2,5·10⁻¹¹/h), volný zásobník všech úloh konstantní (FpgaTask 1392 B, Ui 5512,
default 1720, Uart 5360, I2C4 924), halda min 21104 B beze změny (žádný únik), SEQ FPGA díry 0, GPS fix 2 s 8–12 družicemi, OCXO 46,7 → 45,9 °C,
deska 28,9 → 27,5 °C. SELFTEST 16/16 na konci. Žádný záznam `stall:` / IWDG.

**Ověření nových hradel:**
| hradlo | kontrola | výsledek |
|---|---|---|
| 100 s | čas do prvního vzorku po přepnutí (3 pokusy) | **210 s, 305 s, 305 s** (2–3 délky okna, ne ≤ 115 s) |
| 100 s | pyramida se mezi vzorky neláme (oprava `ADEV_GAP_S`) | **✅ prokázáno**: po 70 vzorcích po 100 s (2,5 h) jsou v pyramidě všechny, řádek pro τ = 1000 s (stage 1) existuje; se starou mezí 30 s by se pyramida lámala při každém vzorku |
| 100 s / 10 s | počet vzorků proti očekávání | **~77 % (100 s: 70 z ~91) a ~82 % (10 s: 591 z ~718)**; u 1 s 97 % (A) a ~100 % (B) → ⬜ viz STATUS #373 |
| 10 s | `tau0 = 10000 ms`, hromadění | ✅ |

⚠️ **Čtení `status full` při dlouhém hradle:** řádky `STATISTIKA: ADEV(1/10/100/1000 s)` mají PEVNÉ popisky (`TS[]` ve `freertos_task_uart.c`), nikoli skutečné τ.
Při hradle 100 s je `ADEV(1 s)` ve skutečnosti τ = 100 s a `ADEV(10 s)` τ = 1000 s, při 10 s je `ADEV(1 s)` τ = 10 s (STATUS #374).

**ADEV (relativní, jednotky 10⁻¹⁵ z `bloky.txt`):**
| blok | kanál / signál | τ = 1 s | 10 s | 100 s | 1000 s |
|---|---|---|---|---|---|
| PODLAHA_B 15:02–15:32 (1 s) | B = CLK_2 (náš OCXO, koherentní) | **9 250** | **1 459** | **428** | – |
| HLAVNI_1S 03:32–15:00 (1 s) | A = externí GPSDO | 7 137 537 | 28 314 062 | 34 502 964 | 39 888 528 |
| HRADLO10 (hradlo 10 s) | A | – | 2,5·10⁻⁹ | 9,2·10⁻¹⁰ | – |
| HRADLO100 (hradlo 100 s) | A | – | – | 2,6·10⁻⁹ | 3,5·10⁻¹⁰ |
| ZAKLAD_B 22:04–22:35 (1 s) | B = CLK_2 | 4 173 273 | 20 912 110 | 13 317 922 | – |
| KONEC 15:32–15:48 (hradlo 0,25 s) | A | 5 925 319 | 43 250 748 | 46 403 876 | – |

- **Podlaha přístroje (kanál B, 15:02):** ADEV 9,3·10⁻¹² @ 1 s, 1,5·10⁻¹² @ 10 s, 4,3·10⁻¹³ @ 100 s – shoduje se s dřívějšími měřeními a s predikcí
  √2·tdc/τ.
- 🔴 **Kanál A (externí GPSDO) má šum o ~3 řády horší než B (3·10⁻⁹ … 7·10⁻⁹ @ 1 s) a ADEV z 1s vzorků ROSTE s τ (7·10⁻⁹ → 4·10⁻⁸ @ 1000 s).**
  Přímá měření dlouhými hradly (2,6·10⁻⁹ @ 100 s, 3,5·10⁻¹⁰ @ 1000 s) vycházejí o řád lépe než pyramida z 1s vzorků v téže τ. Z těchto dat se
  **nedá vyvodit stabilita našeho zamčeného OCXO proti externímu GPSDO**: dominuje šum cesty A (nebo samotného zdroje), ne stabilita reference.
  ⚠️ I kanál B byl 22:04–22:35 špatný (4,2·10⁻⁹) a v 15:02 dobrý (9·10⁻¹²) při téže konfiguraci (A externí, ochrana ÷16 na A) → příčina souvisí s ČASEM,
  ne jen s kanálem. HYPOTÉZY (žádná neověřena): amplituda / hrana externího signálu na vstupu A (dříve ±2 ns u slabého signálu, t370), přeslech A→B při
  nekoherentních signálech (t374/t378), nestabilita samotného externího GPSDO během dne.
- **Rozhodující levné měření:** (1) osciloskop na vstupu A (amplituda, hrana, DC), (2) PROHODIT kabely (externí GPSDO na B, CLK_2 na A) a opakovat 1 h –
  ukáže, zda šum následuje signál, nebo kanál.

**Záznam flight recorderu `stack (pretekl zasobnik tasku)`, dump při uptime 13870 s:** byl v přístroji uložený už před touto nocí (`status` ho hlásil 8. 11.
odpoledne, před úpravami hradla); neodpovídá žádnému resetu této noci a `stack_min` v něm je 920 B (nejhorší úloha I2C4), přetečení z něj nevyčtu.
Příčinu neznám (STATUS #375).

### 2026-11-09 19:42 – 2026-11-10 15:50 — noc1110: kabely PROHOZENY (A = CLK_2, B = externí GPSDO), ochrana přeslechu ZAP/VYP (CM7 v0.45.14)

`noc_logs/_rozbor/noc1110.py` (výstup `noc1110/`, rozbor `noc1110_rozbor.py`): 17 bloků po 70 min, hradlo 1 s, `meas reset` na začátku každého,
cyklicky A+ochrana ZAP, B+ZAP, A+VYP, B+VYP; každých 5 min i skutečný kmitočet kanálu. 0 resetů, GPSDO celou dobu LOCK (y −0,16 … +0,21 ppb),
SELFTEST 16/16, zásobníky, halda a SEQ díry beze změny jako v noci 1109.

**1) Šum SLEDUJE SIGNÁL, ne kanál.** Náš CLK_2 je na kanálu A čistý, externí GPSDO na B je o 2–3 řády horší:
| signál (kanál) | ADEV 1 s | 10 s | 100 s |
|---|---|---|---|
| náš CLK_2 (A), ochrana ZAP, 5 bloků | **3,1·10⁻¹¹** (28,6–33,6) | 6,3·10⁻¹² | 1,9·10⁻¹² |
| náš CLK_2 (A), ochrana VYP, 4 bloky | 1,18·10⁻¹⁰ (90–142) | 2,5·10⁻¹¹ | 6,6·10⁻¹² |
| externí GPSDO (B), 8 bloků | 3,4·10⁻⁹ … 7,9·10⁻⁹ | 1,4·10⁻⁸ … 3,3·10⁻⁸ | 1,2·10⁻⁸ … 3,8·10⁻⁸ |
Noc 1109 (A = externí) tedy NEmohla měřit šum přístroje: kanál A byl v pořádku, vadil signál.

**2) Externí GPSDO nemá stabilní kmitočet.** Kanál B v čase (ppb proti 10 MHz): −172,5 (20:53, 21:43) → −80 (23:39) → −5 (01:35) → +30 (02:25) → +2 (04:20) →
+40 (06:16) → −171,9 (07:06) → −172,8 (09:02) → −0,5 (10:58) → −172,5 (11:48) → +0,8 (13:44); rozsah kmitočtu 1,7–2,8 Hz v každém bloku B, sd 0,91 Hz za noc.
Uvnitř stavu je stabilní (1·10⁻¹⁰ za 2 min), pak skočí. Hypotéza (neověřená): relokace / ztráta zámku externího GPSDO (okenní anténa, malý horizont, viz paměť
`gps-antennas-setup`). Náš GPSDO ve stejné době drží |y| ≤ 0,21 ppb → jako reference je dnes použitelný náš, ne externí.

**3) Ochrana proti přeslechu (`fpga presc guard`, v0.45.10) je velmi účinná:** na měřeném kanálu s naším signálem sníží ADEV **3,8× @ 1 s, 4,0× @ 10 s, 3,5× @ 100 s**
(5 bloků ZAP proti 4 blokům VYP, rozdíl větší než rozptyl uvnitř skupin, drží se přes noc). t385 (−15 až 20 %) ukázal jen slabý případ s jinou konfigurací.
Zbytek: s ochranou je ADEV(1 s) našeho signálu 3·10⁻¹¹, o ~3× nad podlahou 9,3·10⁻¹² z 9. 11. (B, jiná konfigurace) → přeslech od asynchronního signálu
nezmizí zcela. ⬜ Neřešeno: proč byl 8. 11. 22:04 kanál B (náš signál, externí na A) špatný (4,2·10⁻⁹), když ho tuto noc nic podobného nepostihlo.
