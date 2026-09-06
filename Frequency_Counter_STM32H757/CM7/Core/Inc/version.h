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
/* ⚠️ v0.6.0 (2026-08-22) = přechod HSE 10 → 25 MHz (X1/TCXO). PLL1/2/3 přepočteny
 * (VCO identická, mění se jen vstupní dělič M + DSI NDIV), výstupy beze změny.
 * TENTO A VYŠŠÍ FW NENABĚHNE NA DESCE S 10 MHz HSE (PLL se nezamkne). + 5 barevných
 * schémat (KONTRAST), odstranění A/B větve hlavní obrazovky.
 * v0.7.0 (2026-08-25) = kompletní ETH/lwIP na CM4 (F3/F5, DHCP), SCPI přes TCP 5025
 * + HTTP, webový dashboard (W0–W5) + rozšíření v12 (ovládání, mDNS gpsdo.local, SSE,
 * alarmy, GPS sky plot, dlouhá historie 24h/7d/30d + CSV). VBAT prah na CR2032 3,3 V.
 * IPC v12. ⚠️ Kód webu v12 na CM4 čeká na reflash OBOU bank na v12. */
/* v0.8.0 (2026-09-06) = DVĚ VADY DISPLEJE UZAVŘENY NA HW (viz STATUS „PROČ NEŠEL
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
#define FW_VERSION_MINOR 8
#define FW_VERSION_PATCH 0
#define FW_VERSION_STR   "v0.8.0"
#define FW_VERSION_FULL  FW_NAME " " FW_VERSION_STR
#endif /* VERSION_H */
