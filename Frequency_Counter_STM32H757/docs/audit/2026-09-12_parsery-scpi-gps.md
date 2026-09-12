# Audit: parsery nedůvěryhodného vstupu — SCPI + NMEA  (2026-09-12)

- **Commit:** `9a6041e`
- **Jádro / doména:** `scpi.c` běží na **OBOU jádrech** (`CM4/Release/Core/Src/scpi.o` ověřeno),
  `gps.c` jen CM7 / USART1
- **Soubory (čteny celé):** `CM7/Core/Src/scpi.c` (1472 ř.), `CM7/Core/Src/gps.c` (511 ř.)
  = **1 983 ř.** Dohledány i konzumenti nálezů: `CM7/Core/Src/rtc.c` (`gps_time_sane`,
  `g_rtc_text`), `CM7/app/app_gpsdo.c` (`survey_accumulate`), `CM4/LWIP/App/scpi_tcp.c`
  a `httpd_min.c` (volající na CM4), `CM7/Core/Src/freertos.c` (`g_rtc_text`).
- **Projité sekce checklistu:** **D** (souběh — `s_gps` vs. `gps_get`, `g_rtc_text`
  mezi tasky), **E** (ošetření chyb, meze polí, **neošetřený vstup** — hlavní osa).
- **Neprojité (a proč):** **A** (modul nekonfiguruje hodiny; `gps_init` jen přenastaví
  baudrate), **B** (žádná mezijádrová struktura — `scpi_src_t` plní backendy, audit
  modulu 3), **C** (žádné DMA ani vlastní buffery v paměti pro periferie),
  **F**, **G**, **H** (netýká se).

## Souhrn

Navazuje na modul 12 stejnou osou: **kód, který zpracovává data zvenčí.** Rozdíl je,
že tyhle dva parsery jsou dostupné **ze tří transportů** (USB CDC, TCP 5025, HTTP
`/api/scpi`) a `gps.c` navíc z antény.

Obě jádra a všechny transporty sdílejí **jeden** parser, což je dobré rozhodnutí —
ale znamená, že jedna vada v `scpi_num()` je vada na všech třech vstupech naráz.
A právě tam je nejzávažnější nález: **`1E2147483647` roztočí smyčku na dvě miliardy
iterací a na CM4 (kde `double` jde softwarově) tím zablokuje celé jádro na minuty** —
bez autorizace, protože argument se parsuje **dřív**, než se zkontroluje oprávnění
(F-0064).

Parsery jsou jinak psané opatrně a **žádné přetečení bufferu jsem nenašel** —
`tokenize`, `gsv_feed`, `nmea_coord`, `memcpy` ve složené zprávě i chybová fronta mají
meze ověřené (viz „Co bylo zkontrolováno"). Zbytek nálezů je **chybějící validace
rozsahu**: NMEA věta bez checksumu projde, souřadnice se nekontrolují proti ±90/±180
a `SYST:DATE?` před prvním GPS fixem vrací `-3333,-33,-33` jako platné datum.

**Verdikt: podmíněně funkční.** Za normálního provozu (u-blox posílá korektní NMEA,
klient posílá rozumné příkazy) běží obojí správně; všechny nálezy potřebují buď
poškozený/nepřátelský vstup, nebo stav před prvním fixem.

---

### F-0064 [S2] `scpi_num()`: exponent bez meze — `1E2147483647` zablokuje CM4 na minuty, a to bez autorizace

- **Místo:** `CM7/Core/Src/scpi.c:258-263` (`scpi_num`, exponent),
  volající `:869` (`scpi_calc_parse` → `scpi_num`) vs. kontrola oprávnění `:871`/`:876`
- **Popis:** Exponent se přečte do `int` bez jakékoli meze a pak se aplikuje
  **iterativně**:
  ```c
  int e = 0; while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
  while (e-- > 0) v = (es > 0) ? v * 10.0 : v * 0.1;
  ```
  Dvě vady v jednom: (a) počet iterací je **plně pod kontrolou odesílatele**,
  (b) `e = e * 10 + …` u delšího exponentu **přeteče `int`** = signed overflow, tedy UB.
- **Důkaz:**
  1. **Doba běhu na CM4 je řádu minut.** `CM4/Release/Core/Src/subdir.mk` má
     **`-mfpu=fpv4-sp-d16`** — Cortex-M4 FPU je **single precision**, takže každé
     `v * 10.0` (double) jde přes softwarový `__aeabi_dmul`, tedy desítky cyklů.
     `1E2147483647` (deset číslic, `INT_MAX` přesně, **bez** přetečení) →
     2 147 483 647 iterací × ~50 cyklů / 240 MHz ≈ **450 s**.
     Že `v` po ~308 iteracích přeteče na `inf`, smyčku nezkrátí — běží dál celá.
  2. **Je to dosažitelné BEZ autorizace.** `scpi_calc_parse` (`:315-329`) zavolá
     `scpi_num` **při parsování hlavičky**, tedy na řádku `:869`, kdežto oprávnění
     se testuje teprve na `:871` (`src->set_cfg`) a `:876` (`ctrl_locked`).
     `CALC:MATH:M 1E2147483647` tedy nejdřív protočí smyčku a **pak** vrátí
     `-203 Command protected`. Na TCP 5025 navíc autorizace není vůbec
     (`scpi_tcp.c:76-80`).
  3. **Zasažené jádro je CM4.** `scpi_tcp.c:127` (`process_line`) i
     `httpd_min.c:3736` (`scpi_process_ctx`) běží **z lwIP callbacku v hlavní
     smyčce CM4**, která publikuje IPC heartbeat 5×/s. CM7 hlásí mrtvou CM4 po
     ~3 s → `stall:CM4` + `g_cm4_stall_count`. **IWDG2 je záměrně vypnutý**
     (systém-wide reset scope), takže se CM4 sama nezotaví — jen dokončí smyčku.
  4. `scpi.o` **je** v obrazu CM4 (ověřeno `find CM4/Release -name scpi.o`).
- **Dopad:** Kdokoli se sítí na přístroj může jedním krátkým řetězcem odstavit
  konektivitu (web, SCPI/TCP, mDNS) na minuty a vyvolat falešné `stall:CM4`
  v diagnostice. Na CM7 (USB konzole) visí jen UartTask — ten není hlídaný
  watchdogem, takže reset nehrozí, ale konzole nereaguje. **Není to pád ani
  přepis paměti**, je to vyčerpání času.
  ⚠️ Signed overflow v `e` je UB: u delšího exponentu (`1E99999999999`) GCC
  typicky zabalí do negativa a smyčka se neprovede — na výsledku se tedy nedá
  spoléhat ani v dobrém, ani ve zlém.
- **Reprodukce:** `printf 'CALC:MATH:M 1E2147483647\n' | nc <ip> 5025` (nebo
  `POST /api/scpi` s tímtéž tělem, **bez** hlavičky `Authorization`) a sledovat
  `status` na CM7 → `CM4: … stall`. Levnější varianta bez sítě: UART
  `scpi CALC:MATH:M 1E2147483647` — konzole ztuhne (na CM7 kratší dobu, 480 MHz
  a hardwarový double).
- **Návrh opravy:** Mez na exponent **a** neiterativní aplikace:
  ```c
  int e = 0;
  while (*s >= '0' && *s <= '9') { if (e < 1000) e = e * 10 + (*s - '0'); s++; }
  if (e > 308) { if (ok) *ok = 0; return 0.0; }   /* mimo rozsah double */
  ```
  a násobení nechat iterativní jen do 308 (to je ≤ 308 operací, zanedbatelné),
  nebo použít tabulku dekád. **Strop 308** je hranice `double`, takže žádnou
  platnou hodnotu neodřízne; `*ok = 0` vede na existující `-224`, tedy bez nové
  chybové cesty.
  ⚠️ **Pořadí parsování vs. oprávnění bych NEMĚNIL** — SCPI-99 chce chybu příkazu
  (rozsah) nahlásit **před** chybou provedení, a to je tady správně (viz komentář
  `:678-679` u `INPut`). Oprava patří do `scpi_num`, ne do dispatchu.
- **Riziko opravy:** nízké. `scpi_num` je krytá selftestem (`:1303-1312`), takže
  regrese v konverzi se pozná hned. Doplnit vektor `1E999` → `*ok == 0`.
- **Vztah k lekcím:** **`L-0015`** (modul zná svou mez — `double` má 308 dekád —
  a musí ji vynutit na rozhraní, ne ji jen předpokládat) a **`L-0026`**
  (rozpočet, který závisí na vstupu, musí být ohraničený).
- **Stav:** otevřeno

---

### F-0065 [S3] NMEA checksum je nepovinný — věta bez `*HH` projde bez jakékoli kontroly integrity

- **Místo:** `CM7/Core/Src/gps.c:245-258` (`parse_line`)
- **Popis:** Kontrola checksumu je celá uvnitř `if (star)`:
  ```c
  char *star = strchr(l, '*');
  if (star) { … if (cs != given) return; *star = '\0'; }
  /* else: žádná kontrola — pokračuje se na tokenizaci a parsování */
  ```
  Věta **bez** `*HH` se tedy zpracuje, jako by byla ověřená.
- **Důkaz:** `:250-258` — mezi `if (star) {…}` a `char *f[24]; tokenize(...)` není
  žádná `else` větev ani `return`. NMEA 0183 přitom checksum u `$`-vět vyžaduje
  a u-blox ho vždy posílá, takže **jeho absence znamená poškozený nebo cizí rámec** —
  tedy přesně ten případ, kdy se věta zahodit má.
- **Dopad:** Ztratí-li se konec věty (ORE/overrun na USART1 — `usart.c` na ORE dělá
  `AbortReceive` + re-arm, takže ke ztrátě bajtů dochází), projde zbytek bez kontroly
  a naplní `s_gps` neověřenými hodnotami. Ty jdou dál do RTC synchronizace
  (`rtc.c:474`), do IPC snapshotu, na web a do datalogu.
  ⚠️ Samo o sobě to **není** kritické — `rtc.c:42-48 gps_time_sane()` rozsahy
  validuje (rok 2024–2099, hour ≤ 23, min ≤ 59, sec ≤ 60) a to většinu nesmyslů
  zachytí. **Nezachytí ale souřadnice** (viz F-0067) a nezachytí hodnoty, které
  do rozsahu náhodou padnou.
  🔑 **Ve dvojici s F-0066 je to injekční cesta**, ne jen tolerance k šumu.
- **Reprodukce:** poslat na RX USART1 `$GPRMC,120000.00,A,5007.7104,N,01430.5000,E,0,0,120926,,,A`
  **bez** `*HH` a `\r\n` na konci → pozice i čas se přijmou.
- **Návrh opravy:** `if (star == NULL) return;` před blok kontroly — tedy věta bez
  checksumu se zahodí. ⚠️ Ověřit, že u-blox neposílá žádnou větu bez checksumu
  (u NMEA výstupu neposílá; UBX rámce nezačínají `$`, takže do `parse_line` nejdou).
- **Riziko opravy:** nízké, ale **patří k HW ověření**: kdyby některý přijímač
  posílal proprietární větu bez checksumu, přestala by se zpracovávat. Dnes se
  zpracovávají jen RMC/GGA/GSA/GSV, takže dopad by byl vidět hned (`gps` přes UART).
- **Vztah k lekcím:** **`L-0028`** (obrana, která je napsaná, ale podmíněná tak, že
  se u nepřátelského vstupu neprovede).
- **Stav:** otevřeno

---

### F-0066 [S3] Po přetečení NMEA řádku se nezahazuje do konce řádku — tatáž vada, jakou `scpi_tcp.c` opravil 2026-09-06

- **Místo:** `CM7/Core/Src/gps.c:396-397` (`gps_feed_char`)
- **Popis:**
  ```c
  if (s_len < sizeof(s_line) - 1) s_line[s_len++] = c;
  else s_len = 0;                     /* preteceni -> reset (vadny ramec) */
  ```
  Po přetečení se `s_len` vynuluje a **hned se začne plnit znovu z prostředka
  vstupu**. Zbytek příliš dlouhého rámce se tím stane kandidátem na samostatnou
  „větu" — místo aby se zahodil až do nejbližšího `\r`/`\n`.
- **Důkaz:** `:396-397` neobsahuje žádný příznak „zahazuj". Porovnej s
  `CM4/LWIP/App/scpi_tcp.c:132-143`, kde je **tatáž situace vyřešená správně**
  a komentář u toho popisuje přesně tenhle scénář:
  > *„🔴 Radek delsi nez buffer se zahazuje CELY, az do konce radku. Do 2026-09-06
  > se tu jen nulovalo `rxlen`, takze se hned zacalo plnit znovu a OCAS prilis
  > dlouheho radku se provedl jako prikaz."*
  Stejná třída, jiný soubor, **neopravená** — přesně to, před čím varuje **L-0012**.
- **Dopad:** Sám o sobě malý: aby ocas prošel, musel by začínat `$` a mít platný
  (nebo podle **F-0065** žádný) checksum. Standardní NMEA věta má `$` jen na
  začátku, takže při běžném šumu ocas spadne na `if (l[0] != '$') return`.
  🔴 **Ve dvojici s F-0065 je to ale úplná injekční cesta:** vstup delší než 95 B,
  jehož ocas začíná `$GPRMC,…` bez checksumu, se přijme jako platná věta —
  bez jakékoli kontroly integrity.
- **Reprodukce:** `HYPOTÉZA — ověřit:` poslat na USART1 RX ≥96 B smetí bez `\r\n`,
  za tím `$GPRMC,…` bez checksumu, a sledovat `gps` / `gpsraw` přes UART.
  ⚠️ `gpsraw` ukáže `s_last_raw`, tedy co parser dostal — to je pro ověření to
  správné měřidlo.
- **Návrh opravy:** Převzít vzor ze `scpi_tcp.c`: příznak `s_drop`, který se nastaví
  při přetečení a ruší se **až** na `\r`/`\n` (tam, kde se dnes dělá `s_len = 0`).
  Dvě řádky kódu, chování u korektního vstupu se nemění.
- **Riziko opravy:** nízké. ⚠️ Podle **L-0012** v témže commitu doložit, že
  `scpi_tcp.c` (sesterská instance) už opravená je — je, od 2026-09-06.
- **Vztah k lekcím:** **`L-0012`** (symetrické instance) a **`L-0017`** (tichý
  přeskok bez počítadla — dnes se neví, kolikrát se rámec zahodil).
- **Stav:** otevřeno

---

### F-0067 [S3] `nmea_coord()` nevaliduje rozsah souřadnic → `fmt_scpi_deg6()` přeteče `int32_t` (UB)

- **Místo:** `CM7/Core/Src/gps.c:62-79` (`nmea_coord`),
  `CM7/Core/Src/scpi.c:197-202` (`fmt_scpi_deg6`), volající `scpi.c:588-589`
- **Popis:** `nmea_coord` kontroluje **tvar** (tečka, délka stupňové části 1–6
  číslic), ale **ne hodnotu**: vrátí cokoli až do `999999 + 59/60` stupně. Výsledek
  jde beze změny do `s_gps.lat_deg` a odtud do `fmt_scpi_deg6`, kde:
  ```c
  int32_t ud = (int32_t)(v * 1000000.0f + 0.5f);
  ```
  Pro `v = 999999` je `v * 1e6 = 9,99e11`, což **mimo rozsah `int32_t`** (2,147e9) —
  přetypování floatu mimo rozsah cílového typu je **nedefinované chování**, ne jen
  špatné číslo.
- **Důkaz:** `gps.c:70` povoluje `dlen` až 6 (`if (dlen <= 0 || dlen > 6) return 0`),
  takže stupňová část může být `999999`. Zeměpisná šířka přitom nikdy nepřekročí 90
  a délka 180 — mez pro odmítnutí tedy existuje a je jednoznačná.
  `scpi.c:588` předává `src->gps_lat_deg` (float z `nmea_coord`) přímo.
  ⚠️ Kontrast: `fmt_scpi_hz_d` (`:105-112`) i `fmt_scpi_period_s` (`:125-131`) mají
  rozsahovou pojistku **a v komentáři vysvětlenou** právě proti tomuhle; `fmt_scpi_deg6`
  a `fmt_scpi_f2` ji nemají. Je to tedy mezera v jinak zavedeném vzoru (**L-0012**).
- **Dopad:** `SYSTem:GPS:POSition?` (dotaz — **bez autorizace**) nad poškozenými daty
  = UB v aritmetice. Prakticky to na Cortex-M7 dá saturovanou/zabalenou hodnotu, ne
  pád, ale je to nedefinované a projeví se jako nesmyslná pozice servírovaná jako
  měření. Dosažitelné jen přes F-0065/F-0066 (jinak checksum větu zahodí).
  ⚠️ `fmt_coord` v `gps.c:407-413` má **tutéž** konstrukci (`int32_t ud`) a používá
  ji `gps_format_status` pro UART — tedy druhá instance téhož.
- **Reprodukce:** `HYPOTÉZA — ověřit:` vpravit větu `$GPRMC,,A,99999999.0000,N,…`
  (bez checksumu, viz F-0065) a zavolat `scpi SYST:GPS:POS?`.
- **Návrh opravy:** Validovat v **`nmea_coord`**, tedy u zdroje, ne u každého
  formátovače: po výpočtu `if (val > 90.0f || val < -90.0f) return 0.0f;` —
  s tím, že mez se předá parametrem (90 pro šířku, 180 pro délku), protože funkce
  dnes neví, co počítá. Tím se opraví **všichni** konzumenti (SCPI, UART, IPC, web)
  jedním zásahem.
- **Riziko opravy:** nízké. Neplatná souřadnice se změní na 0, což je tatáž hodnota,
  jakou funkce vrací při všech ostatních neplatných vstupech (`:64`, `:66`, `:68`).
- **Vztah k lekcím:** **`L-0012`** (dvě instance téhož formátování: `fmt_scpi_deg6`
  a `fmt_coord`), **`L-0015`** (mez vynutit na rozhraní).
- **Stav:** otevřeno

---

### F-0068 [S3] `SYST:DATE?` / `SYST:TIME?` ignorují `g_rtc_synced` — před prvním GPS fixem vrací `-3333,-33,-33` jako platné datum

- **Místo:** `CM7/Core/Src/scpi.c:439-447` (`SYSTem:DATE?`), `:466-472` (`SYSTem:TIME?`);
  buffer `CM7/Core/Src/freertos.c:155`
- **Popis:** Oba dotazy čtou `g_rtc_text` **čistou aritmetikou nad znaky**, bez
  kontroly, jestli v něm už je čas:
  ```c
  int yy = (rt[0]-'0')*1000 + (rt[1]-'0')*100 + (rt[2]-'0')*10 + (rt[3]-'0');
  ```
- **Důkaz:** `freertos.c:155` inicializuje
  `volatile char g_rtc_text[24] = "---------- --:--:--";`
  Před prvním GPS syncem jsou tam tedy pomlčky a `('-' - '0') == -3`:
  - `SYST:DATE?` → `yy = -3333`, `mo = -33`, `dd = -33` → **`"-3333,-33,-33"`**
  - `SYST:TIME?` → `hh = mm = ss = -33` → **`"-33,-33,-33"`**
  Čtení samo je v mezích pole (19 znaků + NUL v `char[24]`), takže **to není přetečení** —
  je to nesmyslná hodnota vydávaná za platnou.
  🔑 **Je to jediné místo v celém souboru, které při neplatných datech nevrací
  `9.91E37`.** `SYST:GPS:TIME?` (`:579-583`), `SYST:TEMP?`, `MEAS:VOLT?` i
  `MEAS:FREQ?` všechny testují bit platnosti a jinak vrátí SCPI NaN. Příznak
  `g_rtc_synced` navíc **existuje** a UI ho používá (ztlumené „no GPS").
- **Dopad:** VISA/IVI klient dostane záporné datum jako platnou odpověď a nemá jak
  poznat, že přístroj čas nezná. Při automatizovaném sběru se to zapíše do dat.
  ⚠️ **Druhá, menší část téhož nálezu:** čtení `g_rtc_text` **není atomické**, zatímco
  zápis v `rtc.c:576` kritickou sekci má. Roztržené čtení přes hranici sekundy je
  tedy možné, ale dopad je malý (mění se koncové číslice, obě varianty jsou platný
  čas). Zmiňuji to, aby se to při opravě vyřešilo zároveň — je to tatáž třída jako
  **F-0038** (`rtc_time_date` bez ochrany proti přehoupnutí), opravená v modulu 9.
- **Reprodukce:** studený start bez antény → `scpi SYST:DATE?` přes UART.
  **Ověřitelné bez HW i výpočtem** z inicializátoru na `freertos.c:155`.
- **Návrh opravy:** Na začátek obou větví přidat
  `if (!g_rtc_synced) { snprintf(out, out_sz, "9.91E37"); return strlen(out); }` —
  stejný idiom jako `SYST:GPS:TIME?` o sto řádků výš, takže žádný nový vzor.
  Atomicitu vyřešit kopií pod `taskENTER_CRITICAL()` do lokálního `char[24]`.
  ⚠️ Alternativa „vracet čas i bez syncu" by byla legitimní politika (RTC tiká z LSE
  i bez GPS), ale pak **nesmí** vracet pomlčky jako čísla — a stejně by chtěla
  příznak, který klientovi řekne, že zdroj není disciplinovaný.
- **Riziko opravy:** nízké; mění odpověď jen ve stavu, kdy je dnes prokazatelně
  nesmyslná. ⚠️ Ověřit, že SPA/web `SYST:DATE?` nepoužívá k něčemu, co by `9.91E37`
  rozbilo (grep před opravou).
- **Vztah k lekcím:** **`L-0018`** (dvě místa počítající touž věc — čas se do SCPI
  dostává přes textový buffer místo přes `scpi_src_t`, kde je validita řešená bitem)
  a **`L-0011`** (hodnota z diagnostiky není totéž co měření).
- **Stav:** otevřeno

---

### F-0069 [S3] Složená SCPI zpráva: `sub[56]` tiše utne příkaz **i s argumentem** → provede se jiná hodnota, než klient poslal

- **Místo:** `CM7/Core/Src/scpi.c:901-906` (`scpi_process_ctx`, rozklad podle `;`),
  odpověď `:910` (`char rb[64]`)
- **Popis:** Složená zpráva se krájí do `char sub[56]`:
  ```c
  char sub[56];
  while (*line) {
      int k = 0;
      while (*line && *line != ';' && k < (int)sizeof(sub) - 1) sub[k++] = *line++;
      sub[k] = '\0';
      while (*line && *line != ';') line++;      /* zbytek se ZAHODÍ */
  ```
  Delší jednotka se **utne na 55 znaků a zbytek se zahodí** — bez chyby. Utnutá
  jednotka se pak provede.
- **Důkaz:** `:904` má podmínku `k < sizeof(sub)-1`, `:906` přeskočí zbytek do `;`.
  Nikde se nevrací chyba ani nenastavuje příznak. Že se to **týká i argumentu**, je
  podstatné: `CALCulate:MATH:M 1234567890123456789012345678901234567890;*IDN?`
  má 55. znak uprostřed čísla, takže se nastaví **zkrácená hodnota** `M`, a klient
  dostane odpověď na `*IDN?`, tedy potvrzení, že „vše proběhlo".
  ⚠️ `char rb[64]` (`:910`) utne i **odpověď** dílčího dotazu — `SYST:ERR:ALL?`
  ve složené zprávě se tím zkrátí, ačkoli samostatně se vejde (volající dává
  `out_sz` z `bodybuf`, tedy 6144 B na CM4).
- **Dopad:** Tichá záměna hodnoty. Je to **třetí instance téže třídy** v tomto
  projektu za dva dny: `build_log_json` (F-0056) a `hdr_len` (F-0058) měly stejný
  vzorec „nevešlo se → pokračuj s tím, co je". Tady je dopad nejhorší, protože se
  netýká zobrazení, ale **nastavení přístroje**.
  ⚠️ Jak pravděpodobné to je: jednotka nad 55 znaků je u tohohle SCPI stromu
  neobvyklá (nejdelší hlavička `SENSe:FREQuency:GATE:ACTual` = 27 znaků), takže
  za normálního provozu se to nestane. Proto S3, ne S2.
- **Reprodukce:** `scpi CALC:MATH:M 1111111111111111111111111111111111111111;*IDN?`
  a hned `scpi CALC:MATH:M?` → vrátí zkrácenou hodnotu, ne poslanou.
- **Návrh opravy:** Při utnutí jednotku **odmítnout**, ne provést:
  detekovat, že se zastavilo na mezi (`k == sizeof(sub)-1` a `*line` není `;`/NUL),
  a vrátit `-100 "Command error"` do fronty místo provedení.
  `sub[56]` současně zvětšit na velikost, která pokryje reálný nejdelší příkaz
  s argumentem (`SENSe:FREQuency:APERture 1.0E-1` + rezerva → 96 B) a svázat ji
  `_Static_assert`em s `rb[]`, aby se ty dva rozpočty nemohly rozejít.
- **Riziko opravy:** nízké; `sub`/`rb` jsou na stacku volajícího — na CM4 je
  `process_line` frame 680 B proti ~44 kB volného zásobníku (změřeno v modulu 12),
  takže +80 B je bez dopadu.
- **Vztah k lekcím:** **`L-0026`** (rozpočet pevného bufferu ohraničit `_Static_assert`em
  a ořez nikdy nedělat tiše) — tohle je její třetí výskyt, takže lekce funguje jako
  hledací vzor, ne jen jako záznam.
- **Stav:** otevřeno

---

### F-0070 [S3] Souřadnice se nesou ve `float` — self-survey nemůže konvergovat pod ~0,42 m, ať běží jak chce dlouho

- **Místo:** `CM7/Core/Src/gps.c:36-49` (`atof_simple`), `:62-79` (`nmea_coord`),
  `gps.h` (`float lat_deg/lon_deg`); konzument `CM7/app/app_gpsdo.c:4255-4267`
  (`survey_accumulate`)
- **Popis:** NMEA souřadnice se parsují a ukládají ve `float`. Self-survey (#53)
  z nich pak Welfordem počítá průměr a **horizontální rozptyl v metrech**, který má
  být měřítkem konvergence („klesá s N").
- **Důkaz:** `float` má 24bitovou mantisu. Pro hodnotu ~50,12° je exponent 5, tedy
  **ULP = 2⁻¹⁸ = 3,81·10⁻⁶ stupně**. Jeden stupeň šířky ≈ 111 320 m →
  **kvantizační krok 0,42 m**; v délce na 50° zeměpisné šířky
  (× cos 50° ≈ 0,64) **0,27 m**.
  `survey_accumulate` (`:4261`) sice akumuluje v `double`
  (`double lat = g.lat_deg;`), ale **kvantizace už je ve vstupu** — přesností
  akumulátoru se nevrátí.
  ⚠️ Týž strop platí pro `gps_lat_e7`/`gps_lon_e7` v IPC snapshotu (deklarované
  v jednotkách 10⁻⁷ stupně = ~1 cm): spodní dvě dekády jsou kvantizační šum, ne
  informace. Web je servíruje na 7 desetin (`httpd_min.c:368-375`), takže
  **zobrazuje o dva řády větší přesnost, než jaká v datech je**.
- **Dopad:** Rozptyl v okně SURVEY se zastaví kolem ~0,3–0,4 m a dál neklesá, i po
  hodinách. Uživatel to přečte jako „anténa/přijímač nejde lépe", přitom je to
  **kvantizace datového typu**. Nic nepadá a pro GPSDO (kde jde o čas, ne o polohu)
  to nemá vliv na měření.
- **Reprodukce:** `HYPOTÉZA — ověřit na HW:` nechat SURVEY běžet ≥1 h se stabilním
  fixem a sledovat, jestli se rozptyl zastaví kolem 0,3–0,4 m. Předpověď je
  **číselná**, takže se dá potvrdit i vyvrátit.
  Bez HW se dá ověřit aspoň to, že dvě po sobě jdoucí různé NMEA souřadnice
  lišící se o 1·10⁻⁷ stupně dají **identický** `float`.
- **Návrh opravy:** Tři cesty, liší se cenou — **patří do skupiny B (rozhodnutí)**:
  1. **Nechat a zdokumentovat** (nejlevnější): do okna SURVEY a k `gps.h` napsat,
     že mez rozptylu je ~0,4 m daná typem. Zařízení funguje, měření času se to
     netýká.
  2. **Parsovat souřadnice do celočíselných `int32_t` mikrostupňů nebo 10⁻⁷ stupně**
     (přesné, ~1 cm) a `float` nechat jen pro zobrazení. Dotkne se `gps.h`,
     `gps.c`, `ipc_shared.h` (**= bump `IPC_VERSION` → přeflashovat obě banky**),
     SCPI a webu.
  3. **`double` v `gps_data_t`** — jednodušší než (2), ale double na CM4 je
     softwarový (viz F-0064) a struktura se kopíruje v kritické sekci.
- **Riziko opravy:** varianta 1 nulové, 2 vysoké (mezijádrový kontrakt), 3 střední.
- **Vztah k lekcím:** **`L-0006`** (u konstanty/veličiny odvozené z něčeho uveď zdroj
  a jeho rozlišení) — tady chybí, že rozlišení polohy je dané typem, ne přijímačem.
- **Stav:** otevřeno — **čeká na rozhodnutí** (tři varianty výše)

---

### F-0071 [S4] `s_gps.hdop` plní `parse_gga` i `parse_gsa` — dvě pravdy, a bez fixu ji GSA vynuluje na „0,0"

- **Místo:** `CM7/Core/Src/gps.c:126` (`parse_gga`), `:142` (`parse_gsa`)
- **Popis:** Tatáž proměnná se plní ze dvou různých NMEA vět:
  `parse_gga` z pole 8 ($xxGGA), `parse_gsa` z pole 16 ($xxGSA). Která vyhraje,
  závisí na pořadí v dávce přijímače.
- **Důkaz:** `:126` `s_gps.hdop = hd;` a `:142` `s_gps.hdop = hdop;` — žádná
  z nich netestuje platnost té druhé. U multi-GNSS přijímače chodí GSA **víckrát
  za cyklus** (per souhvezdí), takže se přepisuje opakovaně.
  ⚠️ Bez fixu je DOP pole v GSA prázdné a `atof_simple("")` (`:38`) vrací **0.0** →
  `s_gps.hdop = 0`, což vypadá jako **perfektní přesnost**, ne jako „neznámo".
- **Dopad:** Malý a dnes většinou zakrytý: web hodnotu filtruje
  (`httpd_min.c:443-445` bere jen `h10 > 0 && h10 < 2550`, jinak `null`) a datalog
  má sentinel `hdop10 == 255` (`app_gpsdo.c:5013-5015`). Zbývá živé zobrazení
  v GPS okně a `gps_format_status`, kde 0 projde jako hodnota.
  🔑 Hlavní problém je **strukturální**: dvě místa počítající touž veličinu se
  dřív nebo později rozejdou (**L-0018**), a už teď se u nich nedá říct, která je
  autoritativní.
- **Reprodukce:** studený start bez antény → `gps` přes UART; HDOP 0,00 místo „--".
- **Návrh opravy:** Zvolit **jeden** zdroj (GSA má DOP jako svůj účel, GGA ho nese
  jen odvozeně) a druhý zápis odstranit; prázdné/nulové DOP ukládat jako
  **sentinel neplatnosti**, ne jako 0 — stejný idiom, jaký už datalog používá
  (`hdop10 == 255`). Nebo zavést `hdop_valid` bit vedle hodnoty.
- **Riziko opravy:** nízké, ale dotkne se všech konzumentů HDOP → projít grepem.
- **Vztah k lekcím:** **`L-0018`** (slučovat, ne opravovat obě instance),
  **`L-0017`** (neplatnost musí být rozeznatelná, ne zamaskovaná nulou).
- **Stav:** otevřeno

---

### F-0072 [S4] `d2()` nevaliduje, že jsou to číslice — poškozená věta může dát čas v platném rozsahu

- **Místo:** `CM7/Core/Src/gps.c:51` (`d2`), volající `:98` a `:100` (`parse_rmc`)
- **Popis:** `d2()` odečítá `'0'` bez kontroly, že znak je číslice:
  ```c
  static uint8_t d2(const char *s) { return (uint8_t)((s[0]-'0')*10 + (s[1]-'0')); }
  ```
  Volá se po `strlen(f[1]) >= 6`, takže **čtení je v mezích** — chybí jen validace
  obsahu.
- **Důkaz:** `d2("0:")` = `0*10 + (58-48)` = **10**, tedy hodnota, kterou žádná
  kontrola rozsahu neodmítne. `d2("ab")` dá 540 → `(uint8_t)` → 28, což už
  `gps_time_sane` zachytí.
- **Dopad:** **Malý, a je to tak hlavně díky obraně jinde** — `rtc.c:42-48`
  `gps_time_sane()` validuje rok 2024–2099, hour ≤ 23, min ≤ 59, sec ≤ 60 a do RTC
  pustí jen to, co projde. Zbývá úzké okno hodnot, které jsou nesmyslné, ale
  v rozsahu; ty se dostanou do `s_gps` a na displej.
  🔑 Zapisuji to **hlavně proto, že ta obrana není u `d2` vidět** — kdo bude `d2`
  používat na novém místě (třeba pro parsování data z jiné věty), tam validaci
  mít nebude a nedozví se, že na ni má myslet.
- **Reprodukce:** věta `$GPRMC,0:0:0:,A,…` bez checksumu (viz F-0065) → čas 10:10:10.
- **Návrh opravy:** Buď validaci do `d2` (`if (s[0]<'0'||s[0]>'9'||…) return 0xFF;`
  a volající hodnotu odmítne), nebo — levněji a přesněji — **komentář u `d2`,
  který říká, že validaci obsahu dělá `gps_time_sane()` v `rtc.c`, a že nový
  volající si ji musí zařídit sám**. Vzhledem k tomu, že jde o S4 a obrana existuje,
  je druhá varianta úměrnější.
- **Riziko opravy:** nízké.
- **Vztah k lekcím:** **`L-0022`** (u obrany musí být vyjmenované, kdo ji volá —
  tady je obrana v jiném souboru a `d2` o ní mlčí).
- **Stav:** otevřeno

---

## Co bylo zkontrolováno a je v pořádku

U parserů je tenhle seznam podstatný: hledal jsem hlavně přetečení a **nenašel žádné**.

**Meze polí a bufferů (sekce E):**
- `tokenize` (`gps.c:82-90`): podmínka `n < max` se testuje **před** zápisem, takže
  poslední zapsaný index je `max-1`. Prověřeno dopočtem i pro hraniční případ
  (čárka na pozici, kdy `n == max-1`). `char *f[24]`, `max = 24` ✅
- `nmea_coord` (`:62-79`): `dlen` omezené na 1..6, `degbuf[8]` → `memcpy` max 6 B
  a `degbuf[6] = '\0'` ✅
- `gsv_feed` (`:189`): `i + 3 < nf` ohraničuje čtení `f[i..i+3]`, `acc_n[c] <
  GPS_MAX_SATS` ohraničuje zápis ✅
- `parse_rmc`/`parse_gga`/`parse_gsa`: všechny mají `if (nf < N) return` odpovídající
  nejvyššímu použitému indexu (10, 10, 18) ✅; `f[4][0]` u prázdného pole čte `'\0'`,
  což je platné (tokenize NUL-terminuje) ✅
- `ubx_send` (`:275-288`): `if (n > 64) return`, `f[80]`, skutečné maximum
  `6 + 64 + 2 = 72` ✅
- `gps_feed_char` (`:396`): `s_len < sizeof(s_line)-1` ✅ (chování **po** přetečení
  je F-0066, ne přetečení samo)
- `scpi.c` `hdr[48]` (`:383`) a `sub[56]`/`rb[64]` (`:901-911`): všechny mají
  mez v podmínce cyklu; utnutí je F-0069, ne přepis ✅
- `memcpy(out + total, rb, cpy)` (`:916`): `room = out_sz - total - 1`,
  `cpy = min(rn, room)`, pak `out[total] = '\0'` → nejvyšší index `out_sz-1` ✅
- `scpi_err_push` (`:225-235`): při plné frontě přepíše **poslední** položku na
  `-350 Queue overflow` (chování podle SCPI-99) a nezvětšuje `err_count` ✅
- `scpi_bool` (`:283-291`): krátké vyhodnocení `&&` zaručí, že se `s[1]`/`s[2]`
  čtou jen když předchozí znak nebyl NUL ✅
- `SYSTem:ERRor:ALL?` (`:524-543`): ořez je ošetřený **a zarovnaný zpět**
  (`out[t] = '\0'`), aby návratová délka odpovídala obsahu — a komentář u toho
  vysvětluje, proč 32 B na položku stačí (nejdelší je 31) ✅

**Rozsahové pojistky u formátování (přesně ta třída, kterou hledá F-0067):**
- `fmt_scpi_hz_d` (`:103-118`): mez ±4e9 **a chytá NaN** zápisem `!(a && b)` ✅
- `fmt_scpi_period_s` (`:123-141`): mez `1/hz > 1e4` proti přetečení uint64 ✅
- `fmt_scpi_f6` (`:182-188`): **žádnou pojistku nemá**, ale jediný volající
  (`:734`) mu dává `scpi_gate_s()`, což je tabulka `{0.1, 1, 10, 100}` indexovaná
  `idx & 3` → UB **není dosažitelné**. Kontroloval jsem to cíleně, protože to
  vypadalo jako nález; není.

**Souběh (sekce D):**
- `s_gps` se zapisuje ve všech čtyřech `parse_*` pod `taskENTER_CRITICAL()`
  a čte v `gps_get` (`:400-405`) taktéž ✅. Výpočty (`nmea_coord`, `atof_simple`)
  jsou **před** kritickou sekcí, takže se v ní nedrží zbytečně dlouho ✅
- `s_line`/`s_len`/`s_gsv` má jediný kontext (defaultTask drain) — komentář
  `:152-156` to tvrdí a dohledal jsem, že `gps_feed_char` opravdu volá jen
  drain v defaultTasku ✅
- `s_raw_bytes` je `volatile uint32_t` s **jedním** zapisovatelem → inkrement
  nemůže ztratit hodnotu, 32bitové čtení je na ARM atomické ✅
- `s_last_raw` se zapisuje i čte pod kritickou sekcí (`:387-390`, `:503-508`) ✅
- `scpi_ctx_t` je **per-session** (každé TCP spojení má vlastní), takže chybová
  fronta ani status registry se mezi klienty nemíchají ✅ — to je návrhově správné
  a `scpi_tcp.c:187` to dodržuje
- `datalog_read_back` ze SCPI (`:962`) je blokující QSPI, ale na CM7 běží
  z UartTasku (nehlídaný watchdogem) a na CM4 je `read_log = NULL` ✅

**Dvoujádrovost:**
- HW-vázané příkazy (`DISPlay:*`, `SYST:DATE/TIME`) mají `#if defined(CORE_CM7)`
  s `#else` větví vracející `-241 "Hardware missing"` (`:457-462`, `:482-484`,
  `:511-515`) ✅ — a `scpi_selftest` má podle zápisu z 2026-08-30 tentýž guard,
  takže dvoujádrový test nepadá na správném chování
- `scpi_gate_s` je vystavená a sdílená s `httpd_min.c`, aby brána neexistovala
  ve dvou tabulkách ✅ (přesně proti L-0018)

## Nezkontrolováno / omezení tohoto běhu

- **Nespouštěl jsem `selftest`** — podle **F-0055** deterministicky přeteče zásobník
  UartTasku a resetuje desku. `scpi_selftest` (101 kontrol) jsem proto čítal jen
  staticky; jeho vektory pro `scpi_num` (`:1303-1312`) **neobsahují** velký exponent,
  což je přesně důvod, proč F-0064 test nezachytil.
- **Nic z toho neběželo na HW.** F-0064 má napsanou reprodukci přes síť i přes UART,
  F-0065/F-0066/F-0067 vyžadují vpravení NMEA na RX USART1 (potřebuje generátor nebo
  přepojení), F-0070 potřebuje hodinu běhu se stabilním fixem.
- **`gps.h` a `scpi.h` jsem čítal jen v rozsahu, který nálezy potřebují**
  (typy `gps_data_t`, `scpi_src_t`, `SCPI_V_*`) — plné rozhraní `scpi_src_t`
  auditoval modul 3 (IPC).
- **UBX odesílací cesta je prověřená jen staticky.** `gps_config_timepulse`
  (100 kHz / 10 Hz) má smluvní dopad na GPSDO smyčku a její správnost jde ověřit
  **jen měřením na TIMEPULSE pinu** — obsah rámce jsem zkontroloval proti komentáři,
  ale ne proti datasheetu u-blox.
- **Neauditoval jsem `freertos_task_uart.c`** (2 365 ř.), tedy parser UART konzole,
  přes který se `scpi_process` volá. Je to největší neauditovaný soubor v projektu
  a zaslouží si vlastní modul (14).
