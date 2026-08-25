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
#define FW_NAME          "gpsdo-ui"
#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 7
#define FW_VERSION_PATCH 0
#define FW_VERSION_STR   "v0.7.0"
#define FW_VERSION_FULL  FW_NAME " " FW_VERSION_STR
#endif /* VERSION_H */
