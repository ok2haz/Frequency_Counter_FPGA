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
#define FW_VERSION_MINOR 10
#define FW_VERSION_PATCH 0
#define FW_VERSION_STR   "v0.10.0"
#define FW_VERSION_FULL  FW_NAME " " FW_VERSION_STR
#endif /* VERSION_H */
