# Analyza duplicit v menu UI (2026-11-14)

Faze: AUDIT, kod se nemeni. Zdroj: `CM7/app/app_gpsdo.c` (tabulky `MENU_ITEMS`, `MEAS_ITEMS`,
`TOOLS_ITEMS`, dlazdice `render_settings`, obsluha dotyku `app_gpsdo_handle_touch`).
Kazde tvrzeni ma odkaz na radek; co se nepodarilo dolozit je oznaceno HYPOTEZA.

## 1. Mapa (kdo kam vede)

| rozcestnik | polozky |
|---|---|
| MENU (12) `:3309` | Nastaveni, Diagnostika, System Health, Mereni (+ RESTART, NAPOVEDA) |
| NASTAVENI (7) `:2899` | Displej, Alarmy, Cas, Sit, Kalibrace, Animace, Sestavy, O pristroji, Generator, GPS (+ prepinac Jazyk) |
| MERENI (44) `:3326` | Funkce, Citac detail, Prezentace, Dvojkanal, Analyza, Histogram, Datalog, Kvalita GPS, Math/Limity, Holdover, GPSDO TIE, Odchylka xN |
| NASTROJE (48) `:3351` (z footeru Diagnostiky) | Blok. schema, Pamet, Selftest, Benchmark, SD karta, Reference, Chyby |
| System Health (3) footer `:10027` | Senzory, Grafy (-> Prehled kanalu) |

## 2. Duplicity CESTY (totez okno, vic vstupu)

| okno | vstupy | radky | verdikt |
|---|---|---|---|
| HISTOGRAM (6) | dlazdice MERENI + zalozka v ALLAN | `:3335`, `:10398` | zamer (sesterska okna), ale dlazdice je nadbytecna |
| KVALITA GPS (38) | dlazdice MERENI + tlacitko v GPS okne | `:3337`, `:10266` | dva vstupy do GPS tematu |
| ALLAN (23) | tap na kartu hlavni obrazovky + zalozka | `:9924`, `:10398` | v poradku (dlazdice zrusena) |
| CITAC (19) / PREZENTACE (34) / ANALYZA (41) | dlazdice MERENI + rotace tlacitky `< CITAC`, `MERENI >`, `ANALYZA >` | `:3330-3334`, `:10062-10112` | tri vstupy do trojice, ktera je navic v rotaci |
| FREKVENCE/PERIODA, kanal A/B | patka hlavni obrazovky (PERIOD, CHAN) + FUNKCE (49) + PREZENTACE `FREKV./PERIODA` | `:8571-8574`, `:6874` | **tri ovladace tehoz stavu** (`st.mode`/`st.chan`) |
| GENERATOR/REFERENCE | dva ruzna okna na jednom cipu Si5356 | `:4607`, `:4752` | HYPOTEZA: slouceni do jednoho okna (overit na desce, ze CLK0/1 a CLK2..7 jdou rozumne zobrazit spolu) |

## 3. Duplicity OBSAHU (tatez data na vic mistech) - doloženo v kodu

| data | kde se zobrazuji |
|---|---|
| teploty STM board / MCU jadro / OCXO / FPGA board | DIAGNOSTIKA `:902-905`, SENZORY `:1489-1492`, GRAFY `:1863-1868`, PREHLED KANALU |
| napeti 12V/5V/VREF/VBAT/Vc | DIAGNOSTIKA, SENZORY, GRAFY (bargrafy), PREHLED KANALU |
| RTOS heap free/min | DIAGNOSTIKA `:859`, HEALTH `:1222`, PAMET `:2427` |
| Uptime | DIAGNOSTIKA `:935`, HEALTH `:1315`, O PRISTROJI `:2703` |
| verdikt Selftest | HEALTH `:1337`, O PRISTROJI `:2704`, boot splash `:3058`, okno SELFTEST |
| stav reference / zamceni | REFERENCE (14), HOLDOVER (16), GPSDO TIE (45), HEALTH, blokove schema (21), pilulky hlavicky |
| GPS stav | GPS okno (2), KVALITA GPS, NASTAVENI GPS (53), SELF-SURVEY (32), CAS (zivy sync) |

Nejvetsi duplicita: **DIAGNOSTIKA vs SENZORY** - SENZORY je podmnozina leveho sloupce
DIAGNOSTIKY (aktualni hodnoty tehoz seznamu 10 senzoru; DIAGNOSTIKA navic ukazuje min/max).

## 4. Kolize NAZVU (stejne slovo, ruzny vyznam)

- **NUL**: Math/Limity (relativni Y, `meas_math_capture_null`), Odchylka xN (`f0 := f`, `:4384`),
  ADRET (`:4482`). Tri ruzne operace, stejny popisek.
- **RESET**: Senzory `RESET MIN/MAX` (`:1480`), Prezentace `RESET` (`:6879`), Alarmy `RESET STATISTIK`
  (`:6488`), Allan reset statistik. Ctyri ruzne rozsahy pusobnosti, uzivatel je nerozezna.
- **ULOZIT**: Kalibrace, Sestavy, Pruvodce - ruzne cile (CALIB blob / slot sestavy).
- **MERENI**: rozcestnik `MERENI` (44) vs okno `MERENI prezentace` (34) vs `MERENI >` (tlacitko v CITAC, vede na 34).
- **CITAC**: dlazdice `CITAC DETAIL` vs hlavni obrazovka (drivejsi `Citac`) vs tlacitko `< CITAC` v ANALYZA (vede na 19).
- **MENU > NASTAVENI > DISPLEJ** vs `DISPLEJ` jako nazev okna a zaroven prikaz SCPI `DISP:`.

## 5. Doporuceni (po odsouhlaseni, poradi podle prinosu/rizika)

1. **Sloucit SENZORY do DIAGNOSTIKY** (nebo naopak) - odstrani cely rozcestnik v Health a jedno okno. Nizke riziko.
2. **Jedno misto pro RTOS heap/uptime/selftest**: Health je autorita, Diag/O pristroji odkazuji. Stredni (zmena layoutu).
3. **Odstranit dlazdici HISTOGRAM z MERENI** (zustane zalozka v ALLAN) a **KVALITA GPS** nechat jen v GPS okne -
   uvolni 2 dlazdice z 12 (MERENI by sneslo vetsi radky, ktere maji dnes 76 px, tj. nad minimem 60 px, ale blizko).
4. **Prejmenovat** NUL (NUL f0 / NUL Y / NUL faze) a RESET (RESET MIN/MAX, RESET STAT., RESET ALLAN). Nulove riziko, jen texty.
5. **PERIODA/FREKV. v PREZENTACI** zrusit nebo napojit na tentyz stav jako patka (dnes `s_meas_mode` je ODDELENY stav od
   `st.mode` - HYPOTEZA, overit v `meas_present`/`screen_main_set_func`, zda se pri zmene synchronizuji).
6. Dvoji cesta ovladani (dotyk + encoder) musi zustat: pri odebrani dlazdice zkontrolovat `focus_store`, `s_focus_of[]`
   a tabulky `*_ITEMS` (poradi po sloupcich = poradi encoderu).

## 6. Co jsem NEoveril

- Zda `s_meas_mode` (Prezentace) a `st.mode` (patka) jsou skutecne dva stavy - cten jen handler, ne obe strany.
- Obsah okna PREHLED KANALU vs DIAGNOSTIKA na zivem displeji (vzhled neprozkoumavan, jen kod a popisky).
- Pocet dotyku k cili z hlavni obrazovky (nemereno, jen odvozeno z `nav_push`).

## 7. Provedeno (v0.45.19, commity da63ff8 + d97c488, na desce neoveřeno)

- Přejmenováno: `NUL f0` (Odchylka xN, ADRET), `NUL Y ZAP/VYP` (Math), `RST STAT.` (Prezentace), `RESET ALARMU` (Alarmy).
  Allan zůstal `RESET` (88 px tlačítko, kontext okna) a Senzory/Diagnostika `RESET MIN/MAX`.
- Okno SENZORY (s_view=4) zrušeno: nic z něj nechybí v DIAGNOSTICE; `RESET MIN/MAX` je ve footeru Diagnostiky
  (`DIAG_SENS_RESET_RECT`), Health má jen `GRAFY` na místě bývalého SENZORY.
- Web SPA nesahat (tlačítko `NUL (f0 = ted)` zůstává; mění se jen displej).
- Při ověření na displeji zkontrolovat: footer Diagnostiky (2 tlačítka), footer Health, tlačítka s novými popisky (šířka).
