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
