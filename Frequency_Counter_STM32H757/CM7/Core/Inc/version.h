#ifndef VERSION_H
#define VERSION_H
/**
 * @file    version.h
 * @brief   JEDINA definice verze firmware — sdili UART ("version") i displej
 *          (okno "O pristroji" + boot splash). Drive se lisily (UART v0.2-diag
 *          vs displej v0.1) -> sjednoceno sem.
 *
 * ⚠️ VERZOVANI (numericke, KONZISTENTNE s Git): SemVer MAJOR.MINOR.PATCH. Kazde
 * zvyseni verze = commit + `git tag vX.Y.Z` na TOMTEZ commitu, aby verze na
 * displeji/UART presne odpovidala git tagu (dohledatelnost buildu podle verze).
 *   - PATCH: opravy/drobnosti  - MINOR: nove featury  - MAJOR: zlom API/HW.
 */
/* v0.40.0 (2026-10-24) = UZKA HLAVICKA v3 (priprava FW 0x0507, CAPS bit22, PROTOKOL_V3 kap. 3.1b): SEQ, REC_HEAD (pocet),
 * REC_FIRST a PSEQ jen 16 b -> STM rozbali na 32 b (fpga_v3_unwrap, restart FPGA podle skoku SEQ), predchozi 1PPS
 * z historie STM, RING_FILL dopocitany, BUB/OVL z FLAGS zaznamu; TS10M a RX_LAST se uz nectou. Starsi FPGA beze zmeny.
 * Emulator `fpgasim v3 on ... uzka` (citace startuji tesne pod 2^16), `fpga v3` vypise druh hlavicky.
 * Oprava: FW_VERSION_STR od v0.39.1 hlasil "v0.39.0". */
/* v0.39.1 (2026-10-23) = AUDIT funkci FPGA v3 (docs/audit/2026-10-23_v3-funkce-audit.md): T_RAW s potvrzenim (bajt 127,
 * FW 0x0505) -- odmitnuty prikaz uz nevrati stara data jineho zaznamu / kanalu, bezici zaznam se zastavi, okno s chybou
 * cteni a mezery v zaznamu se hlasi; automaticky rozsah pod 16 kHz (sonda 0) vrati P = 1; bez kopani watchdogu FPGA
 * z UartTask; v2 nuluje log2 P; `fpga v3` sonda v kHz bez %f (nano.specs). */
/* v0.39.0 (2026-10-23) = HLUBOKY RAW ZAZNAM do vestavene PSRAM FPGA v3 (FW 0x0505): `fpga raw a|b deep <N> [dump]` = az
 * 699 050 po sobe jdoucich znacek kanalu, cteni po oknech 512 znacek (T_RAW rezim 2 + stranky druhu 3), statistika
 * navazuje pres okna; hlavicka 124..131 (DEEP_N, DEEP_ST, latence zapisu PSRAM, okno); `fpga v3` vypise stav PSRAM.
 * Cteni RAW stranek je spolecna funkce s RAW 512 (s3_raw_pages). */
/* v0.38.0 (2026-10-23) = PREDELICKA FPGA v3 (FW 0x0503, etapa E5): vstupy nad prime pocitani (~40 MHz) pres pin_cnt3 ve
 * FPGA (P = 1..16). Automaticky rozsah ze sondy /16 (fpga_v3_range_select: nahoru f/P > 33 MHz, dolu f/(P/2) < 27 MHz),
 * prikaz az po shode dvou ramcu a az kdyz predchozi zmena plati; B z pinu A = P 1. Okna v periodach (hrany x P), preruseni
 * pri zmene P, mez primeho mereni na deleny kmitocet, udalosti BURST v periodach pinu; emulator s predelickou jako RTL;
 * UART `fpga presc a|b auto|0..4`, `fpga v3` vypise predelicku a sondu. */
/* v0.37.0 (2026-10-23) = JEMNY CAS 1PPS z FPGA v3 (FW 0x0502, etapa E4): IDES8 + rPLL ve FPGA, PTS krok 1,25 ns (sigma
 * 0,36 ns misto 2,9 ns hrubeho citace). fpga_v3_pps_fine (jemny jen s CAPS bit17 + PCLK bezi + PFLAGS bit1), fpga_pps_t
 * nese zlomek taktu, priznak a epochu; GPSDO pocita interval se zlomkem a pri zmene osy (epocha, jemny/hruby) zacne
 * znovu; emulator v3 dava jemny cas; UART fpga v3 vypise stav 1PPS. v2 beze zmeny (zlomek 0). */
/* v0.36.0 (2026-10-23) = JEDNORAZOVE DEJE (protokol v3, FW 0x0501; docs/PROTOKOL_V3.md kap. 10.5): udalosti ze zaznamu
 * segmentu (presny cas 1. a 2. hrany segmentu, pri N >= 3 souhrn) -- UART `fpga ev on|off|<n>` (kruh 256, cas proti 1PPS),
 * `fpga ab on|off|<n>` (interval start A / stop B), `fpga pw on|cal|off|<n>` (sirka pulzu: SET_CONFIG 0x09 B = pin A
 * invertovany, kalibrace zpozdeni A/B s B = pin A), `fpga raw a|b [dump]` (RAW_CAPTURE 512 znacek); emulator
 * `fpgasim v3 on <Hz_A> [Hz_B] [faze_B_ns]`. Cas rozbalen spolecne pro A i B (jedna osa FPGA), statistika Welford. */
/* v0.35.1 (2026-10-23) = v3: SPI transakce nejdriv po 40 ms (FpgaTask vola poll pri hradle 50 ms po 10 ms -> drive az ~90
 * transakci 1 KB/s, ~7 % CPU a ~5 % casu maskovane znacky pri CS). Kazde okno se dal precte (fronta 8 oken, kruh FPGA
 * 6,4 s); model smycky FpgaTasku v host testu: hradlo 50 ms 1198/1198 oken souvisle, 22,7 transakci/s. */
/* v0.35.0 (2026-10-22) = PROTOKOL v3 v ovladaci (docs/PROTOKOL_V3.md kap. 10, kroky 4c/6/7): detekce verze FPGA prvni
 * transakci 1024 B (v2 dal beze zmeny, v3 = FW 0x0500): ACK + prijem zaznamu (fpga_v3_link_take: prekryv ramcu, mezera,
 * reset FPGA), okna ze segmentu -> fpga_meas_t (kanal a hradlo voli STM, druhy kanal = sekundarni slot, fazovy zaznam
 * z teziste prvniho segmentu), ztrata signalu v STM; kalibrace TDC v3 (stranky histogramu, LUT_WRITE davkou, LUT_CNT_B,
 * potvrzeni s maskou kanalu); emulator `fpgasim v3 on [Hz_A] [Hz_B]`; UART `fpga v3`; SPI2 DMA regen-safe (fpga_spi_dma.c,
 * DMA1 Stream0/1, zapina se `spi dma on`, vychozi vypnuto). Nove .c: fpga_spi_dma.c -> Close->Open Project. IPC v29. */
/* v0.34.1 (2026-10-22) = fpga_v3 segmenty -> okno i pri NIZKEM kmitoctu (perioda > L = 25 ms, segmenty N = 0): okno se
 * prodlouzi do prvni hrany po K segmentech (reciprocni citac), ztrata signalu = mezera bez hrany > 3 ocekavane periody, nejvys
 * 2,5 s (jako v2; docs/PROTOKOL_V3.md kap. 7). Drive kazdy prazdny segment okno zahodil -> pod ~40 Hz zadna okna.
 * Selftest krok 9 (2 Hz, 0,3 Hz). Ovladac FPGA dal v2 beze zmeny. */
/* v0.34.0 (2026-10-22) = protokol v3 krok 4 (docs/PROTOKOL_V3.md kap. 8): novy modul fpga_v3.c/.h -- ramec 1024 B (CRC bloku,
 * zaznam segmentu 31 B, MOSI ACK/SET_CONFIG/LUT_WRITE), parser (n_ok do prvniho vadneho bloku -> REC_ACK), emulator FPGA v3
 * (kruh, ACK, preteceni) a vrstva segmenty -> okno (dvoubodove + LSQ pres teziste segmentu, rozbaleni X0 40 b / T0 48 b).
 * UART `fpga v3 test` = selftest modulu; host test tests/host/t_fpga_v3.c + mutace mut_fpga_v3.py. Ovladac FPGA zatim
 * BEZE ZMENY (v2, 128 B) -- zapojeni do fpga_freq je krok 4c. Novy .c: Close->Open Project. IPC v29 beze zmeny. */
/* v0.33.2 (2026-10-22) = priprava na SCK 25 MHz (protokol v3, docs/PROTOKOL_V3.md kap. 2): vystupy SPI2 SCK (PI1) a MOSI (PB15)
 * na rychlost MEDIUM ve fpga_freq_init (regen-safe, jen OSPEEDR pod gpio_cfg_lock; CubeMX dava LOW); UART `spi` vypisuje
 * rychlost pinu. Vychozi takt SCK zustava 12,5 MHz -- 25 MHz se zkousi `spi 25`. */
/* v0.33.1 (2026-10-22) = opravy z auditu v0.21-v0.22: (1) F-0259 dither DAC: akumulator sigma-delta se potvrdi az po
 * uspesnem zapisu (drive se posunul i pri selhani); (2) `dac dither <Hz>` hlasil „vychozi 200", vychozi je 100 Hz;
 * (3) F-0261: okno Citac „E-A/E-B" misto „E/4, E/16", selftest #1 „FPGA ramec + regrese". Prelozeno na macOS (GCC 14.3): CM7 text 708 632 B (+24 proti v0.33.0), audit 95/0/2. */
/* v0.33.0 (2026-10-21) = FPGA FW 0x0427: kalibracni tabulku TDC pocita a zapisuje STM (fpga_tdccal.c, caps bit4 FPGA_CAP_STMCAL): FPGA po
 * sberu histogramu ceka, FpgaTask (fpga_freq_tdccal_poll) precte histogramy, spocita tabulky, zapise (TYPE 0x0A LUT_WRITE), overi
 * pocitadlo+soucet a potvrdi (SET_CONFIG 0x06). ovf/peak/nz/last (CAL report) se pocitaji z histogramu. Novy .c: Close->Open Project. */
/* v0.32.0 (2026-10-21) = FPGA FW 0x0426: regresni blok nese SUROVE SOUCTY segmentu (caps bit3 FPGA_CAP_RAWREG), teziste xm/ym
 * dopocita STM (rg_raw_to_mean, celociselne); delic ve FPGA odstranen -- na krzemiku daval chybne dolni bity podilu x zavisle
 * na rozmisteni v P&R (L-0187: DIAG_SX build ukazal presny sum(x-x0) a chybny podil). edge_count je od 0x0426 u32.
 * Starsi FW (bez bitu 3) se cte beze zmeny. Selftest #1: ramec 0x0426 (rozlozeni, xm/ym, regrese). */
/* v0.31.3 (2026-10-21) = posledni platna cislice hlavniho cisla podle SKUTECNE presnosti: (1) podlaha z regrese (mp_res_floor_rel,
 * namerena 2,9e-11 @ 0,25 s, konzervativne skalovana podle hradla a poctu znacek), driv vzdy dvoubodova 5,9e-10 -> displej skryval
 * 1-2 platne cislice; hystereze priznaku regrese (g_freq_regr); totez pro karty v oknech (card_relres). (2) kmitocet pro
 * Math/Odchylku xN si nechava plne rozliseni zdroje i pri zkracenem formatu; podtrzeni plati i pri 0/1 desetine.
 * Zobrazeni cislic beze zmeny: za posledni platnou jedna velka (SIGMA), dalsi male (FLOOR). */
/* v0.31.2 (2026-10-21) = (1) UART `phrec dump [n]`: diagnosticky kruh 64 krmeni fazoveho zaznamu se surovymi vstupy (ADEV z faze 50x nad
 * statistikou); (2) sum odectu 1PPS: dolni mez = kvantizace hrubeho citace krok/sqrt(12) (2,89 ns), driv 0,3 ns -> nejistota reference
 * prilis optimisticka; (3) `status` FPGA SAMOKONTROLA: cas/poradi posledniho zamitnuti, varovani jen za zamitnuti po rozbehu (> 40 mereni). */
/* v0.31.1 (2026-10-21) = opravy po flashi v0.31.0 (L-0186): (1) CRC selftest pri startu FpgaTasku mel buf[1024] na zasobniku -> volnych
 * 36 B z 2560, nove buf[FR_LEN]; (2) selftest #1 (regrese) padal po nauceni adaptivni meze (~5 ppb) -> bezi s vychozi mezi a stav vraci. */
/* v0.31.0 (2026-10-20) = integrace E0 (FPGA FW 0x0425, zadani druhe instance CLI_other_instance_exchange/E0_FW0425): diagnostika O4
 * (bubliny, znacky pri CS, abs 57..59) a hlidac koherence 100/10 MHz (abs 10/11, caps bit14), xm 1/4096 hrany (caps bit15),
 * kontroly FPGA_CHK_FMAX/COH, ADAPTIVNI mez prijeti regrese, fazovy zaznam fpga_phrec (E1, novy soubor), CRC pres pulbajtovou tabulku
 * (fpga_crc, novy soubor; HW jednotka jen s FPGA_CRC_USE_HW=1), GPSDO: sum 1PPS za behu (gpsdo_pm_ns), historie faze v ps.
 * UART `fpga diag [mask|led on|off]`, `status` radky FPGA DIAG / KONTROLY E0 / REGR MEZ / FAZOVY ZAZNAM, `regr` ukazuje aktualni mez.
 * Emulator: caps bit14. S FPGA 0x0424 bezi dal (vse podle caps). SPI DMA (fpga_spi_dma) NENI zapnuto (FPGA_SPI_USE_DMA=0).
 * IPC v29 beze zmeny. ⬜ Neovereno na desce. */
/* v0.30.7 (2026-10-18) = UBX-CFG-NAV5 dynModel stationary (STATUS #312/#326 navazujici test): UART `gps nav5 stationary|auto`.
 * Na rozdil od CFG-TMODE2 (survey-in, NEO-7M NAKuje) je NAV5 standardni zprava podporovana kazdym u-blox prijimacem; meni jen dynModel
 * (mask=bit0, RAM modulu, bez periodickeho obnoveni). Cil: podle nezavisleho mereni (N8UR) muze zlepsit ADEV 1PPS od tau ~30 s. Chovani
 * ostatniho firmwaru beze zmeny. FPGA beze zmeny, IPC v29. ⬜ Neovereno na desce (ACK/NAK, vliv na xr_ns). */
/* v0.30.6 (2026-10-18) = GPS z konzole pro automaticke dlouhe mereni: UART `gps gnss gps|multi` (GLONASS vyp/zap, UBX-CFG-GNSS), `gps survey on|off` (UBX-CFG-TMODE2 3600 s / 2,5 m)
 * a `gps ack` (ACK/NAK modulu na TMODE2, GNSS, TP5, MSG; dosud se odpovedi jen pocitaly). Chovani za behu beze zmeny, nic se nepersistuje. FPGA beze zmeny, IPC v29. */
/* v0.30.5 (2026-10-18) = vychozi takt SPI2 ke FPGA 12,5 MHz (drive 3,125; zadani 10 MHz, kernel 200 MHz dava stupne 6,25 / 12,5). UART `spi 6` vrati 6,25. */
/* v0.30.4 (2026-10-18) = UART `spi [MHz]` = takt SPI2 ke FPGA za behu (strop FPGA_SCK_MAX_HZ 10 -> 40 MHz, vychozi 5 MHz beze zmeny; #314), zasobnik FpgaTask 2048 -> 2560 B. */
/* v0.30.3 (2026-10-18) = halda FreeRTOS 32 -> 48 kB (volnych bylo 5,7 kB, z haldy jsou i zasobniky tasku); .ioc srovnano. */
/* v0.30.2 (2026-10-18) = osa Y Allanu (displej i web) se natahne i k podlaze citace (nejvys 7,5 dekady pod krivku), cara uz nekonci u 10 s. */
/* v0.30.1 (2026-10-18) = opravy po flashi v0.30.0: (1) `status` nasel chybejici znak `%` v mono_25 (subset) -> okno NASTAVENI GPS bez %; (2) web: detail Allanu byl PRAZDNY, kdyz pristroj
 * po zapnuti ma jen 1 bod Allanu (pyramida ceka na ustaleni reference ~15 min) -> web pocita vlastni Allan, detail vysvetli proc je prazdny; prepinace ADEV/TDEV/MTIE a BODY/DEK jsou
 * nove i V DETAILU (karta je pod overlayem neprichytna); legendy krivek (Teploty, Napeti, OCXO Vc) jsou viditelne prepinace (chipy) s napovedou; (3) displej: Citac detail bez fazi a popisu
 * pinu 27/28 (kanaly A/B), ppm pod 1 -> ppb na desetiny (MERENI, Odchylka xN, ANALYZA, web). IPC v29 beze zmeny. ⬜ Neovereno na HW. */
/* v0.30.0 (2026-10-18) = (1) nejistota reference z ZMERENYCH hodnot misto odhadu: FLL = |y| posledniho okna smycky (merene proti GPS) + sum okna,
 * LOCK = ADEV faze OCXO-GPS @ <= 1000 s z historie smycky, HOLD = ADEV pri vstupu + drift kodu DAC (LS pres <= 2 h LOCK) x K; nahradni tabulka jen kdyz mereni chybi
 * (`gpsdo_ref_ppb`, IPC v29: `ipc_gpsdo_t` 40 -> 48 B, web `ref_ppb_x100` + `ref_meas`, okno ANALYZA ukazuje „zmereno/odhad“); (2) korekce baterie PRESNE s ubytkem diody
 * (75,6 mV; chyba ADC 17 mV se neodecita, je to jina velicina). Flashnout OBE banky, IPC v29. ⬜ Neovereno na HW. */
/* v0.29.0 (2026-10-18) = (1) NASTAVENI GPS (s_view=53, dlazdice `GPS >`): delka a rychlostni faktor anteniho koaxu -> zpozdeni L/(c*VF) do UBX-CFG-TP5
 * antCableDelay (persist ve vycpavce syscfg, magic beze zmeny); (2) ADRET: FAZE (s_view=54, tlacitko FAZE > v Odchylce xN): fazovy graf integrovany z 1 s vzorku,
 * Df/f +- u (autokorelace), posuv faze, rozchod za 24 h; (3) HISTOGRAM: vlastni ring 3600 vzorku, rozsah median +-4 sigma_MAD, citac mimo rozsah, odecet trendu
 * (tlacitko DETREND/SUROVY), median kvickselektem; (4) PRESNOST: nejistota reference podle STAVU smycky (`mp_ref_ppb`: LOCK 0,01 ppb ... volny chod 1 ppm) pro hlavni cislo,
 * okno ANALYZA i web (`ref_ppb_x100`), nejiste cislice PERIODY z rozliseni misto pevnych 2; (5) VBAT (STATUS #322): mustek jen pri prevodu, 1x/600 s (1 s v okne Kalibrace),
 * nabijeni VBE=0 explicitne, korekce na baterii (dioda D3, kalibrace CAL3, vychozi 75,6 mV z mereni), prahy pro CR2032 (nominal 3,0 V, vyhrada 2,70 V); (6) WEB: interval
 * 6 h a oprava prekryvu davek historie (klient posilal serveru cele okno misto 1/4); (7) `status`: posledni chybejici glyf, radek VBAT nabijeni; tools/mem_report.py.
 * Flashnout OBE banky; IPC v28 beze zmeny. ⬜ Neovereno na HW (na desce bezela v0.27.2). */
/* v0.28.0 (2026-10-18) = displej (CM7) + web (CM4): spektrogram na displeji drzi historii (ring 720 s v screen_main, plni se i za zavreneho okna,
 * skala +-3 sigma okna); podlaha citace v Allanu bez omezeni 1e-11 (kde opousti osu, znacka); FUNKCE MERENI: odemcene Frekvence B / Perioda B (CHAN);
 * histogram: vyberova sigma (n-1); web: nahled spektrogramu a histogramu (height=100/> = nulova vyska), detail Kvality GPS, kuzel holdoveru.
 * Flashnout OBE banky (kvuli konzistenci SPA s firmwarem); IPC v28 beze zmeny. */
/* v0.27.2 (2026-10-18) = web (CM4): dlazdice `VETEV /16` -> `KANAL B` (od FW 0x0400 je freq16 kanal B, deleni na desce neni) a text Dvojkanal; detail Allanu ma STEJNE osy jako nahled
 * (dekadicke + vedlejsi log cary, popisky na dekadach, podlaha citace, metrika podle nahledu). Flashnout jen CM4; IPC v28 beze zmeny. */
/* v0.27.1 (2026-10-18) = audit v0.27.0 (docs/audit/2026-10-18_filtr-odlehlych-v0.27.md): F-0254 selftest filtru zapisoval za konec pole (`hamp_push` ma parametr `nst`, kanarek v testu),
 * F-0255 prah filtru 5 -> 7 MAD (na realnych datech 0,02-0,05 % misto 0,04-0,17 %), F-0256 atomicke cteni pro UartTask, F-0257 test vypnuti filtru za behu. IPC v28 beze zmeny. */
/* v0.27.0 (2026-10-18) = filtr IZOLOVANYCH odlehlych vzorku pred Allan pyramidou (Hampel 31 vzorku, 5 MAD, vyrazeny vzorek = dira; skok a shluk 2+ se nevyrazuje),
 * surova pyramida `s_adev_raw` vedle filtrovane, pohled RAW (tlacitko VSE/OKNO/RAW), pocet vyrazenych v tabulce ALLAN / `status full` / webu, `meas outlier on|off`.
 * IPC v28 (hlavicka ipc_stab_t: mode, outl, outn) -> FLASHNOUT OBE BANKY. */
/* v0.26.2 (2026-10-18) = F-0252 kratsi test pohledu OKNO (3 600 misto 6 000 vlozeni) + `selftest` vypisuje delku v ms, F-0253 logger gpsdo_log.ps1: sloupec `sigma1s_cum`,
 * soubor s jinou hlavickou se prejmenuje. IPC v27 beze zmeny. */
/* v0.26.1 (2026-10-18) = audit zmen v0.23-v0.26 (docs/audit/2026-10-18_zmeny-v0.23-v0.26.md): F-0248 debounce zavreni brany Allanu (10 s), F-0249 mez selftestu okna,
 * F-0250 hlaska 'Reference se ustaluje', F-0251 napoveda UART. IPC v27 beze zmeny. */
/* v0.26.0 (2026-10-18) = audit A2 V5: UART `allan log` = overlapping ADEV/MDEV/HDEV s krokem tau0' (1 s) nad CELYM .measlog (~18 h, proudovy vypocet,
 * 2 x 3001 double v .bss, ~0,3 s v UartTasku); selftest proudoveho Allanu == primy vypocet SP1065. IPC v27 beze zmeny. */
/* v0.25.1 (2026-10-18) = audit A2 V9 / STATUS #320: pyramidy statistiky (s_adev, s_tr, s_y; ~22,7 kB) v DTCM (horni 64 kB, sekce .dtcm_bss, linker DTCM_APP;
 * spodnich 64 kB zustava testovaci oblasti membench), nulovani v main() USER CODE 2. Linker zmenen -> po flashi overit `selftest`, `membench`. */
/* v0.25.0 (2026-10-18) = audit A2 V3: druha sada akumulatoru se zapominanim (okno ~600 clenu kazde stage) a prepinac pohledu VSE / OKNO v okne ALLAN
 * (tlacitko vedle RESET; hlavicka tabulky 'sigma_y(tau) okno'). Nepersistuje se. IPC v27 beze zmeny. */
/* v0.24.0 (2026-10-18) = audit A2 V6/V7: sedma stage Allan pyramidy (tau do 9·10^6 s), `ipc_stab_pt_t.nterm` 32 b, IPC_STAB_PTS 63. IPC v27 -> FLASHNOUT OBE BANKY. */
/* v0.23.1 (2026-10-17) = audit A2: V1 brana Allan pyramidy (jen ustalena reference: OCXO dohrato + smycka OFF/WAIT/HOLD/LOCK, `meas gate on|off`),
 * V2 zadny clen pres mezeru (>30 s bez vzorku, STOP/RUN, napojeni rekonstrukce), V4 snapshot/UART sigma_y(1 s) = karta (`g_adev_1s_cum`). IPC v26 beze zmeny. */
/* v0.23.0 (2026-10-17) = A2 (STATUS #321): ADEV/MDEV/HDEV z akumulatoru clenu od resetu (n roste, ne okno 60); sigma_y(1 s) na karte/tabulce/grafu/webu
 * z akumulatoru, prahovy monitor dal z okna 120 s (`screen_main_adev_1s_live`). Web: text o poctu clenu (CM4). IPC v26 beze zmeny. */
/* v0.22.1 (2026-10-17) = web (CM4): detail grafu OCXO Vc ma vlevo kod DAC [LSB], vpravo napeti (jako nahled). Flashnout jen CM4 (IPC v26 beze zmeny). */
/* v0.22.0 (2026-10-17) = pasivni fazovy monitor ve stavu OFF (STATUS #319): po `gpsdo off` se fazova chyba OCXO proti GPS 1PPS dal scita
 * (`xr_ns`), DAC se neridi; zaznam volne bezici reference vuci GPS pro #312. TODO #319: rozhodnout, zda to vratit. FPGA bitstream beze zmeny, IPC v26. */
/* v0.21.1 (2026-10-17) = oprava #318: FLL v core_step meri K z kroku kodu jen kdyz krok presahuje sum oken (5 sigma) -> konec falesne
 * divergence a `gpsdo test` FAIL; vychozi rychlost ditheru DAC 100 Hz (`dac dither 200` vrati maximum). Bitstream FPGA beze zmeny, IPC v26. */
/* v0.21.0 (2026-10-17) = dither DAC OCXO 200 Hz (STATUS #317): zapis kodu smycky GPSDO dela novy DacTask (AboveNormal, 5 ms) misto
 * 2 Hz v SensorsTasku; zvlneni Vc klesne ~40x (simulace: 30e-12 -> <= 0,8e-12 p-p, ADEV 0,25 s ze 8e-12 na <= 2,4e-13). UART `dac dither
 * [2..200|test]`, `status` radek `DAC OCXO: dither`. IPC beze zmeny (v26) -> staci flashnout CM7. ⬜ Neovereno na desce (`stats` DacTask
 * stack/CPU, `dac dither test`, `dac` kroky bez mutexu, A/B ADEV dither 2 Hz vs 200 Hz). */
/* v0.20.1 (2026-10-17) = opravy z prezkumu v0.20.0 (docs/audit/2026-10-17_fixreview-v0.20.md): F-0237 detekce prepsani
 * v sdram_log (>= CAP), F-0238 cislo epochy jednoznacne pres restart + pojistka kurzoru, F-0239 SD zrcadlo jineho formatu
 * se prejmenuje, F-0241 tau min. po zmene tau. IPC beze zmeny (v26); CM4 jen komentar -> staci CM7, ale z v0.19.x
 * flashnout OBE banky (IPC v26). ⬜ Neovereno na desce. */
/* v0.20.0 (2026-10-17) = (1) web `GET /api/raw`: surova mereni ze sdram_log od kurzoru (epocha + absolutni index), IPC v26 kanal `raw`,
 * PC `tools/raw_fetch.py` (STATUS #308 E); (2) `tau min.` smycky GPSDO = dolni mez z HDEV faze proti bilemu sumu 1PPS, UART `gpsdo`
 * + okno GPSDO TIE, jen zobrazeni (#308 C); (3) web graf Vc: kazda osa sve meritko (vlevo kod DAC, vpravo Vc). IPC v26 ->
 * FLASHNOUT OBE BANKY. ⬜ Neovereno na desce (`selftest` vc. `gpsdo test`, `/api/raw`, okno GPSDO TIE, graf Vc v prohlizeci). */
/* v0.19.0 (2026-10-17) = (1) kod DAC OCXO v zaznamu datalogu (bajty 30..31, mimo CRC; stare zaznamy = nezaznamenano), UART `datalog dump` + CSV `dac_code`
 * (STATUS #311, stari OCXO bez PC); (2) CM4: pocitadla vytizeni spojeni v `/api/state` blok `conn` (STATUS #307). Format datalogu je zpetne kompatibilni,
 * IPC beze zmeny (v25). FLASHNOUT OBE BANKY (CM7 datalog + CM4 web/pocitadla). ⬜ Neovereno na desce (spustit `selftest`, `datalog dump`). */
/* v0.18.1 (2026-10-17) = web (CM4): graf OCXO Vc ma druhou krivku POVEL DAC (STATUS #310, legenda jde skryvat) a tabulka karty ALLAN ukazuje
 * 6 radku rozlozenych pres cely rozsah tau vcetne nejdelsiho (STATUS #309). Jen SPA v CM4; IPC beze zmeny (v25), CM7 beze zmeny kodu - FLASHNOUT CM4
 * (CM7 jen kvuli cislu verze). ⬜ Neovereno v prohlizeci/na desce. */
/* v0.18.0 (2026-10-16) = GRAF TIE A STAVU SMYCKY GPSDO (STATUS #37): displej okno s_view=45 "GPSDO TIE" (dlazdice v MERENI, driv placeholder
 * TI 1PPS) - prepinac TIE / y / KOD, osa z bodu LOCK, pas stavu OFF/WAIT/FLL/LOCK/HOLD, historie 2 h po 10 s; web karta GPSDO (stejny
 * prepinac + pas stavu). IPC v25 (`ipc_gpsdo_t.x_raw_ns`, blok 36 -> 40 B na konci struktury). FLASHNOUT OBE BANKY (CM7 + CM4).
 * FPGA bitstream beze zmeny (FW 0x0424). ⬜ Neovereno na desce (vzhled oken, chovani historie po resetu). */
/* v0.17.1 (2026-10-16) = web: RESET ALLAN / RESET TREND rekne, PROC je zamceny (zapis z webu vyzaduje zapnute vzdalene ovladani na pristroji
 * + prihlaseni; na desce bylo web_ctrl_en = False). Funkcne beze zmeny firmwaru CM7, meni se jen web v CM4 (staci nahrat CM4; CM7 jen kvuli cislu verze).
 * Bez zmeny IPC (v24). */
/* v0.17.0 (2026-10-16) = SMYCKA GPSDO + DAC LADENI OCXO (STATUS #305, #304, #306). FPGA FW 0x0424 (bajty 52..56 = GPS 1PPS, hruby
 * citac 100 MHz bez TDC, caps bit13), IPC v24 (dac_* ve snapshotu, blok gpsdo). (1) Smycka: FLL s merenim K + PLL 2. radu (tau 1000 s),
 * HOLD pri ztrate 1PPS, sawtooth korekce z UBX-TIM-TP (znamenko voli sama), kod AD5693R do flash jen pri zamceni; UART `gpsdo`,
 * `status` GPSDO:, vychozi ZAPNUTO (rucni `dac` ji vypne). (2) Obnova posledniho kodu DAC po startu (syscfg dac_code, bez zmeny
 * magic). (3) Web: osy Vc (kod DAC vlevo, napeti vpravo, ADC/DAC LSB pod grafem), RESET ALLAN / RESET TREND (i na displeji, dvoji
 * stisk), zapinani krivek legendou, svetlejsi pomocne osy. ⬜ NEOVERENO NA DESCE (smycka, FPGA 0x0424, web). FLASHNOUT OBE BANKY
 * (IPC v24) + bitstream FPGA FW 0x0424; bez bitstreamu smycka stoji ve WAIT (bezpecne). */
/* v0.16.2 (2026-10-15) = oprava trvaleho zaznamniku chyb (STATUS #296, L-0159): poskozeny (nedopsany, nesmazany) slot flash se bral
 * za hlavu logu a kazdy boot prepsal zaznamy minuleho (nejnovejsi zaznamy necitelne, `total` stal). Slot ma nove 3 stavy, hlava = prvni
 * SMAZANY slot, zapis jen do smazaneho, poskozeny slot ve cteni je placeholder POSK; narust prodlevy opakujici se udalosti (60 s az 30 min).
 * `errlog dump` ma diagnosticky radek. Bez zmeny IPC (v23), CM4 beze zmeny. ⬜ Zapis ERRLOG_K_TDC/REFRST neni na desce videt (M4). */
/* v0.16.1 (2026-10-15) = oprava USB konzole (STATUS #298, L-0158): vysila se jen pri pritomnem hostu (DTR / data od hosta),
 * zasekly IN prenos se rusi na HW vcetne TX FIFO (drive zpozdena konzole o jeden prenos po kazde pauze bez hosta), pri zavrenem
 * portu se zahazuje nejstarsi; `status` ma radek KONZOLE: host. Skripty v adresari tools nastavuji DtrEnable. Bez zmeny IPC (v23).
 * ⬜ Neovereno po power-cyklu. CM4 beze zmeny, staci nahrat CM7. */
/* v0.16.0 (2026-10-15) = studeny start a diagnostika bez konzole. Bitstream FPGA beze zmeny (0x0423, 100 MHz).
 * (1) Automaticky re-init Si5356 pri trvale ztrate zamku reference (studeny start dvakrat nezamkl, #295, L-0156);
 * `status` REFERENCE: auto re-init, errlog druh REFRST. (2) IPC v23: `/api/state` nese rezervu TDC retezu (tdc_rez_*,
 * tdc_lvl_*, tdc_ovf_*) a `si_reinit`, aby sla rezerva cist pres ETH i kdyz USB konzole mlci (#297/#298).
 * (3) `warmup_ready`: prah sklonu 0,3 C/min, minimum 900 s po zapnuti napajeni (300 s po resetu), povinna platna teplota a
 * pec topi; nova hlaska "TEPLOTA NEUSTALENA - OCXO se zahriva, presnost snizena" (#299, L-0157).
 * Zadne zmeny mereni ani protokolu FPGA. ⬜ Neovereno na desce: studeny start (M4, #300). FLASHNOUT OBE BANKY (IPC v23). */
/* v0.15.0 (2026-10-13) = FPGA FW 0x0423 (referencni hodiny FPGA 100 MHz; pokus se 125 MHz z 0x0421/0x0422 je VRACEN, L-0154),
 * Si5356 CLK0/1 = 100 MHz + okno GENERATOR (CLK2/3, CLK4/5, CLK6/7, UART `gen`), ADS1115 PGA AIN0-2 +-2,048 V a prevod float
 * (+ kalibrace gain_vc/gain_vbus, blob CAL2), I2C1 400 kHz, `GET /api/fpga` a IPC v22 (40 slov diagnostiky FPGA),
 * UART `tdc` pocita sirku binu z kmitoctu hodin v ramci, LED FPGA_OK, opakovani kalibrace TDC po cal_fail.
 * Overeno na desce po power-cyklu: FW 0x0423 + CM7, regrese pouzita ~99,9 %, ADEV(0,25 s) 1,1e-10 (kanal A, regrese); denik mereni
 * docs/MERENI_A_KALIBRACE.md. ⬜ Neovereno: dvoubodovy sum pri zapojeni A+B, kalibrace TDC po zahrati (studeny start 3,7 % rezervy retezu).
 * FLASHNOUT OBE BANKY (IPC v22) + bitstream 0x0423. MINOR se do v0.14.0 nezvysil (zustal 13) -> srovnano. */
/* v0.14.0 (2026-10-07) = FPGA FW 0x0411: regresni blok (STM: `regr`, `status` REGR:), TDC 0x0410
 * (bez slepoty spoustece), IPC v20. Overeno jen simulaci/P&R, NE na HW.
 */
/* v0.13.0 (2026-10-06) = mereni: chybne napocitane okno + duveryhodne cislice +
 * Allan osa + statistika webu z pristroje.
 * (1) +4 Hz SKOK pri mereni vlastni reference = HRANA NAVIC v okne (metastabilita
 * detekce hrany ve FPGA). STM ho pozna (`fpga_freq_miscount`), nezobrazi a
 * nepocita; `status` -> CITANI HRAN. FPGA FW 0x040F pocita hrany za
 * synchronizatorem (oprava pricin). (2) Pocet duveryhodnych cislic ma podlahu
 * √2·tdc/gate -- synchronni signal mel σy ~0 a displej tvrdil vsech 7 desetin.
 * (3) Allan osa Y auto-range pro vsechny metriky (driv pevne 10⁻¹⁰..10⁻⁶).
 * (4) IPC v20: web bere ADEV/MDEV z firmwarove pyramidy (`/api/stab`).
 * ⬜ Neovereno na HW; FLASHNOUT OBE BANKY (IPC v20) + bitstream 0x040F. */
/* v0.12.2 (2026-10-05) = web: headline podtrhava TOTEZ cislo co displej + 3 grafy.
 * (1) WEB HEADLINE = DISPLEJ: firmware servíruje NAMERENOU σy@1s (`sy1e15` v
 * /api/state, z g_adev_1s = tyz zdroj co displej, pres sigma_tau[0] ve snapshotu).
 * fmtFreqHtml ji pouzije misto vlastni klientske sigmaAtTau(1) -> u hranice dekady
 * uz nepodtrhava jine cislo. IPC_VERSION beze zmeny (sigma_tau[0] existovalo).
 * (2) FAZOVY SUM jde MAXIMALIZOVAT (klik -> fullscreen, drawZoomPn log-log) a ma
 * popsanou osu X (offset [Hz] + svisla mrizka gxPn). (3) ALLAN/faz.sum maji
 * VEDLEJSI LOG mrizku (mantisy 2..9 v dekade, .gm/.gvm) - carkovane log pozadi.
 * ⬜ Neovereno v prohlizeci; flashnout CM4 (web) + CM7 (sy1e15). */
/* v0.12.1 (2026-10-05) = fix: Allan graf se zase kresli (revert break-on-spike).
 * v0.12.0 zavedlo spike = fpga_stat_break na ~19,5 % oken (obri bin). To ale
 * VYHLADOVELO Allan pyramidu (1s vzorek se skoro nikdy nedokoncil bez preruseni)
 * -> graf se na displeji nekreslil a g_adev_1s ~0; a mezeru v pyramide stejne
 * neoznacilo (break nuluje jen akumulator). Byla to prekorekce. Revert na v0.11.0:
 * vadne okno se jen VYRADI z akumulatoru, vzorek se dopocte ze zbylych -> Allan
 * jede dal, mezera je jen sub-vzorkova. Vadne okno se znaci SDRAM_LOG_F_SPIKE v
 * ulozene rade. Prava oprava stlaceni = odstranit obri bin ve FPGA (B1a #267),
 * zvolena priorita uzivatelem. ⬜ Neovereno na HW. */
/* v0.12.0 (2026-10-05) = konzistence web/displej + spravnost Allan + B1a diag.
 * WEB: headline (fmtFreqHtml) podtrhaval posledni "duveryhodnou" cislici NAPEVNO
 * na 3. desetine, bez ohledu na rozliseni -> pri horsim rozliseni nez 0,001 Hz
 * jine cislo nez displej. Ted pocita hranici ze SKUTECNEHO rozliseni (namerena
 * sigma_y@1s z klientskeho ADEV, fallback sqrt2*tdc/gate) = jako firmware
 * freq_uncertain_frac; 7 desetin jako displej; vybledle cifry kontrastnejsi.
 * ALLAN SPRAVNOST: spike-reject (v0.11.0) vyrazene okno obriho binu jen nepridal
 * do akumulatoru, ale NEPRERUSIL -> vzorky pred/po spiku se slepily a rada se
 * casove "stlacila". Ted spike = MEZERA v case (fpga_stat_break) + priznak
 * SDRAM_LOG_F_SPIKE v ulozene rade (rekonstrukce ho vezme jako mezeru).
 * B1a (FPGA FW 0x040D): CAL report nese d_maxtap (nejvyssi KDY navzorkovany tap)
 * k diagnoze obriho binu -- `tdc`: maxtap~last = retez fyzicky konci, maxtap>>last
 * = bubliny nad prvni nulou (jina oprava). STATUS #267.
 * ⬜ Vse neovereno na HW po power-cyklu; flashnout CM4 (web) + CM7 + bitstream 0x040D. */
/* v0.11.0 (2026-10-04) = oprava USB CDC konzole + zpresneni mereni TDC.
 * KONZOLE: deadlock TX ringu pri plnem bufferu (drzeny blok se neuvolnil ->
 * avail==0 -> vse zahazovano), ISR-safe drain z CDC callbacku (HAL_GetTick
 * misto osKernelGetTickCount, ktery je z IRQ nebezpecny), ring 1->4 KB.
 * Pricina „mrtve konzole" nalezena merenim (RX jel, TX mrtvy). TDC: cesta A
 * spike-reject oken poskozenych obrim binem (kod 134 = ~19,5 % hran) ze
 * statistiky/Allan (sigma okna 880->314 ps, ~2,6x, prah 3e-9 zmereny);
 * teplotni rekalibrace (CM7 posle `tdc cal` pri driftu FPGA teploty >=3 C);
 * zobrazovane cifry z NAMERENE sigma_y@1s misto teoretickeho TDC/hradlo
 * (display uz nelze); `gatestat <ms>` laditelne prumerovani 250 ms..100 s +
 * nastaveni FPGA okna (SET_CONFIG 0x01). Analyzy docs/audit/2026-10-04_*.
 * ⬜ TDC zmeny neovereny na HW po power-cyklu; FPGA bitstream beze zmeny (0x040C). */
/* v0.10.0 (2026-10-01) = SPI protokol FPGA migrovan v1->v2: ramec 64B->128B,
 * CRC16 nad byte 0..125 (bylo 0..61), 4 nova pole fpga_meas_t (fw_version/
 * caps/clk_status/win_count). Kriticky zachycen L-0012 vzor v miste vzniku:
 * UART diagnostika (`fpgaloop`/`fpgaraw`) mela vlastni `rx[64]` nezavisle
 * na FR_LEN, opraveno soucasne (zasobnik by se jinak prepsal o 64 B). FPGA
 * strana (Frequency_Counter_FPGA_Module, bitstream FW_VERSION 0x0300):
 * RTL pro novy dvoukanalovy vstupni modul (CH_A/CH_B, jednohodinove hrube
 * citani misto 4fazoveho vernieru) + SPI PHY presunuta z 10 MHz na 100 MHz
 * (clk_p0_100m) s rucne navrzenym CDC vuci 10MHz aplikaci (spolehlivy SCK
 * strop ~2 MHz -> ~20 MHz). `status` nove vypisuje FW/CAPS/CLK/WIN z
 * posledniho DATA ramce. F-0200: demo dlazdice "eased cislo" ztracela
 * znamenko '+' (chybejici glyf v mono_25). ⬜ Cela v2 migrace + FPGA RTL
 * NEOVERENY NA HW -- nova deska jeste neni zapojena, stara mluvi jen v1
 * a je mrtva (RX0:FF). Window stream/SET_CONFIG/CAL report (volitelne
 * casti protokolu v2) STM strana zatim nepouziva. */
/* v0.9.0 (2026-09-13) = tři nezávislé rychlosti I2C4 podle zařízení (ATtiny
 * 50 kHz — bit-bang slave, CPU 1 MHz je limit; FT5x06 75 kHz; TMP117 400 kHz;
 * `i2c4_speed_select()`), okno PAMĚŤ ukazuje FLASH/RAM OBOU jader (IPC v16),
 * a okno CHYBY (errlog) přehlednější — sloupec DETAIL s čitelnou větou
 * (`errlog_fmt_detail()`) nahradil holá čísla `a/b`, přidáno záhlaví sloupců.
 * Errlog nově dostupný i na webu: nový IPC kanál `ipc_errlog_xfer_t` +
 * `GET /api/errlog?n=&from=` + karta [ CHYBY / LOG ] v SPA (stránkování
 * NOVEJSI/STARSI). Web nezná význam `a`/`b`/`sub` — CM7 posílá hotovou
 * větu, jeden zdroj pravdy pro displej i web. IPC v17. */
/* ⚠️ v0.6.0 (2026-08-22) = přechod HSE 10 → 25 MHz (X1/TCXO). PLL1/2/3 přepočteny
 * (VCO identická, mění se jen vstupní dělič M + DSI NDIV), výstupy beze změny.
 * TENTO A VYŠŠÍ FW NENABĚHNE NA DESCE S 10 MHz HSE (PLL se nezamkne). + 5 barevných
 * schémat (KONTRAST), odstranění A/B větve hlavní obrazovky.
 * v0.7.0 (2026-08-25) = kompletní ETH/lwIP na CM4 (F3/F5, DHCP), SCPI přes TCP 5025
 * + HTTP, webový dashboard (W0–W5) + rozšíření v12 (ovládání, mDNS gpsdo.local, SSE,
 * alarmy, GPS sky plot, dlouhá historie 24h/7d/30d + CSV). VBAT prah na CR2032 3,3 V.
 * IPC v12. ⚠️ Kód webu v12 na CM4 čeká na reflash OBOU bank na v12. */
/* v0.8.1 (2026-09-06) = PG11 (ETH_TX_EN) ztracel alternativni funkci -> MAC
 * odeslal, DMA deskriptor se dokoncil bez chyby, ale PHY nikdy nedostal
 * povoleni vysilat -> deska nedostala IP z DHCP. Stejny podpis jako PG8:
 * na GPIOG sahaji OBE JADRA neatomickym read-modify-write. Pridan hlidac
 * `gpio_guard_tick()` (1 Hz z defaultTask), ktery PG8/PG11/PG13 kontroluje,
 * opravuje a POCITA zasahy (`status` -> radek GPIO HLIDAC).
 *
 * v0.8.0 (2026-09-06) = DVĚ VADY DISPLEJE UZAVŘENY NA HW (viz STATUS „PROČ NEŠEL
 * DISPLEJ"): (1) `PG8`/`FMC_SDCLK` byl v ANALOGOVÉM režimu → SDRAM bez hodin
 * četla samé nuly → černý displej po power-resetu, `membench` 10,5 M chyb,
 * `sdramlog` sám vypnutý; `MX_FMC_Init` pin nově potvrzuje před inicializační
 * sekvencí. (2) Podtečení LTDC FIFO při každém flipu (copy-forward na DMA2D
 * souběžně se skenováním panelu) → probliknutí při každém překreslení; mrtvý
 * čas DMA2D 8 → 240 (zlom změřen na ~208). Dále: SCPI `INPut[n]:` + `APERture`
 * jako alias `GATE`, web (osy s hezkým dělením a rám grafu, karty DVOJKANÁL /
 * LINKA / REFERENCE / RF vstup, fázový šum, alarmy, Math počítaný klientem),
 * timeouty a use-after-free v HTTP/SCPI serverech na CM4. IPC v13. */
#define FW_NAME          "gpsdo-ui"
#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 42
#define FW_VERSION_PATCH 1
/* retezec se ODVOZUJE z cisel (v0.39.1 hlasil "v0.39.0", protoze se zvysilo jen PATCH) */
#define FW_VER_S_(x)     #x
#define FW_VER_S(x)      FW_VER_S_(x)
#define FW_VERSION_STR   "v" FW_VER_S(FW_VERSION_MAJOR) "." FW_VER_S(FW_VERSION_MINOR) "." FW_VER_S(FW_VERSION_PATCH)
#define FW_VERSION_FULL  FW_NAME " " FW_VERSION_STR
#endif /* VERSION_H */
