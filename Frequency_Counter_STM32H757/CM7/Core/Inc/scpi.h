#pragma once
/**
 * @file    scpi.h
 * @brief   SCPI-99 parser + dispatch (STATUS.md #25) — transportně nezávislý.
 *
 * `scpi_process()` zpracuje JEDEN řádek příkazu (bez CRLF) a zapíše odpověď do
 * `out`. Nezná transport → stejné jádro pro **USB CDC konzoli hned** i **TCP 5025
 * na CM4 později** (jen dvě volání téhož `scpi_process`). Idiom projektu:
 * pure-logic + selftest na targetu (`scpi_selftest`).
 *
 * Podporuje SCPI krátkou/dlouhou formu (MEAS ↔ MEASure), case-insensitive,
 * dvojtečkovou hierarchii (SYSTem:ERRor?) a `?` dotazy.
 *
 * ⚠️ **Zlaté pravidlo (STATUS.md):** `MEASure:FREQuency?` vrací REÁLNÝ FPGA
 * kmitočet (`fpga_freq_get_last`), NE simulovaný headline. Bez platného měření
 * (link down / SIGNAL_LOST) vrací SCPI „not-a-number" `9.91E37`. Ostatní dotazy
 * (teploty, GPS, stav) vracejí reálná data z `g_sensors`/`gps_get`.
 *
 * ⚠️ Žádný `%f`/`%E` — čísla přes celočíselnou extrakci (float-printf je v nano
 * newlibu vypnutý, viz fmt_hz v app vrstvě).
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Zpracuje jeden SCPI řádek (bez CRLF). Odpověď (vč. '\0') do out; vrací její
 *  délku bez '\0' (0 = žádná odpověď — např. SET/akce jako *RST). */
size_t scpi_process(const char *line, char *out, size_t out_sz);

/** Pure-logic unit test (parser: case, krátká/dlouhá forma, hierarchie, neznámý
 *  příkaz) — 1 = PASS. Netestuje HW hodnoty (jen že příkaz odpoví). */
int scpi_selftest(void);

#ifdef __cplusplus
}
#endif
