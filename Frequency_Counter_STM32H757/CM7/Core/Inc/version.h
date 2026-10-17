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
#define FW_VERSION_MINOR 26
#define FW_VERSION_PATCH 0
#define FW_VERSION_STR   "v0.26.0"
#define FW_VERSION_FULL  FW_NAME " " FW_VERSION_STR
#endif /* VERSION_H */
