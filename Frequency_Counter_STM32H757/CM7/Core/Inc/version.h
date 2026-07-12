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
#define FW_NAME          "gpsdo-ui"
#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 3
#define FW_VERSION_PATCH 0
#define FW_VERSION_STR   "v0.3.0"
#define FW_VERSION_FULL  FW_NAME " " FW_VERSION_STR
#endif /* VERSION_H */
