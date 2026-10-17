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
| Vstup A a B | 10 MHz z **externího OCXO GPSDO** (oba kanály stejný signál, přes T-kus) |
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
  🔑 **Nezávislé měření stejného externího GPSDO (2018, uživatel, <https://ok2haz.ok2kld.cz/ok2haz/2018/12/citac-s-gps-normalem-3-stabilita/>):** GPSDO s u-blox NEO-7M a OCXO Trimble 34310-T, měřeno **proti Rb normálu** Efratom FRK-LLN-1A1B1A komparátorem Adret 4110, Stable32 (overlapping ADEV). Výsledek: **GPSDO při τ < 100 s ≈ 3·10⁻¹⁰**; volné OCXO (pevné ladicí napětí) 1–100 s ≈ 3·10⁻¹²; Rb za 1 den ≈ 1·10⁻¹². Integrátor smyčky R18 100 kΩ + C24 (100 µF, v názvu grafu 10 µF) = **časová konstanta ~10 s**; kolísání GPS hodin ~22 ns p-p. Srovnání s naším měřením: ADEV vůči externímu GPSDO 3–6·10⁻¹⁰ na 10–50 s (hradlo 1 s i 0,25 s) a 2·10⁻¹⁰ @ 100 s — **řádově i úrovní stejné jako publikovaná stabilita GPSDO**, a vrchol u 20–50 s odpovídá smyčce s τ ~ 10 s, která propouští šum 1PPS přijímače (~6 ns rms → ADEV ≈ √3·σx/τ ≈ 5·10⁻¹⁰ @ 20 s). Tím je hrbol s vysokou pravděpodobností **šum externího GPSDO**, ne čítače ani našeho OCXO. ⚠️ Předpoklad: článek popisuje TENTÝŽ přístroj, který dnes měříme (potvrdit), a publikovaná tabulka hodnot po τ není v textu — jen „< 100 s ≈ 3·10⁻¹⁰". Nevyřešeno zůstává **náš** přebytek fáze OCXO–GPS ~4·10⁻¹¹ @ 100 s (#312): tohle měření se týká externího zdroje, ne našeho OCXO.
  ⚠️ Vedlejší pozorování (nevysvětleno): ADEV firmwaru při τ = 1 s je s hradlem 0,25 s **3,9·10⁻¹¹**, s hradlem 1 s v noci 1,07·10⁻¹⁰. Vzorek 1 s se skládá ze 4 oken po sobě, takže by měl mít stejný šum TDC jako jedno 1s okno. ⬜ Ověřit (regresní blok FPGA se u delšího okna chová jinak? jiný počet hran v řetězu?).
  Fáze proti GPS (smyčka): σ hodinová 7–9 ns, TDEV 0,9 ns @ 20 s … 6 ns @ 2000 s; kód DAC +4,4 LSB za 2 h (1,6 ppb/den, s teplotou r = −0,64).

## 3. Stav kalibrací

| Co | Stav | Hodnota / poznámka | Datum |
|---|---|---|---|
| **TDC code density** (A, B) | ✅ automaticky po startu, po výpadku hodin a při FAIL znovu (L-0153); po power-cyklu A:1 B:1 | 100 MHz: rezerva řetězu 268 tapů ~6–8 % (~250 tapů na 10 ns) — sledovat `tdc` → `max tap` i v teple | 2026-10-11 |
| **Skew A−B** | ✅ změřeno, korekce netřeba | 0,010 ns (100 MHz, stejný signál na A i B) | 2026-10-11 |
| **Offset reference proti GPSDO** | ⬜ jen změřeno, nelze doladit | −0,84 ppm; chybí AD5693R (ladění Vc) | 2026-10-11 |
| **ADS AIN0 Vc** (gain_vc) | ⬜ výchozí 2,5 | Vc nikdo nebudí (AD5693R neosazen) — kalibrace nemá smysl | — |
| **ADS AIN1 VBUS** (gain_vbus) | ⬜ výchozí 20,608 (R54 = 5k1) | čte 11,99 V; ověřit multimetrem | — |
| **ADS AIN2 +3V3** (gain_12v) | ⬜ výchozí 2,0 | ověřit multimetrem | — |
| **ADS AIN3 +5V** (gain_5v) | ⬜ výchozí 2,0 (R58 = 10k) | čte 5,03–5,17 V; **zkalibrovat** v průvodci (Kalibrace → PRŮVODCE) | — |
| **AD8307 RF** | ➖ na desce není (RF_LEVEL_HW_PRESENT 0) | — | — |
