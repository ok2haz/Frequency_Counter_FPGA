#pragma once
/**
 * @file    scpi.h
 * @brief   SCPI-99 parser + dispatch (STATUS.md #25) — transportně nezávislý.
 *
 * `scpi_process()` zpracuje JEDNU programovou zprávu (bez CRLF; smí obsahovat
 * víc jednotek oddělených ';') a zapíše odpověď do `out`. Nezná transport →
 * stejné jádro pro **USB CDC konzoli hned** i **TCP 5025 na CM4 později**.
 * Idiom projektu: pure-logic + selftest na targetu (`scpi_selftest`).
 *
 * Podporuje SCPI krátkou/dlouhou formu (MEAS ↔ MEASure), case-insensitive,
 * dvojtečkovou hierarchii (SYSTem:ERRor?), `?` dotazy a **složené zprávy**
 * (jednotky oddělené `;`, IEEE 488.2 — odpovědi dotazů spojené `;`).
 *
 * IEEE 488.2 common commands:
 *   *IDN?  *OPC  *OPC?  *WAI  *TST?  *RST  *CLS
 *   *ESR?  *ESE <m>|*ESE?  *SRE <m>|*SRE?  *STB?   (minimální status model)
 *
 * Podporované příkazy (dotazy vrací hodnotu, akce nic):
 *   SYSTem:VERSion?  SYSTem:UPTime?  SYSTem:ERRor?(=…:NEXT?)  SYSTem:ERRor:COUNt?
 *   SYSTem:TEMPerature? [OCXO|BOARD|MCU|FPGA]  (default OCXO 0x49)
 *   SYSTem:GPS:STATus?  SYSTem:GPS:TIME?  SYSTem:GPS:POSition?
 *   MEASure:FREQuency?  (=FETCh:FREQuency?)  :DIV16?  :ALL?  (reálný /4, /16, oba)
 *   MEASure:VOLTage? [P12|P5|VC|VREF|VBAT]  (default P12)   MEASure:POWer?  (RF dBm)
 *   SENSe:FREQuency:GATE?  SENSe:FREQuency:CHANnel?          (poslední FPGA rámec)
 *   CALCulate:DATA?  CALCulate:LIMit:FAIL?         (Math Mx+B/NULL + limit, viz meas_math)
 *   STATus:OPERation:CONDition?  STATus:QUEStionable:CONDition?
 *
 * SET příkazy (akce, bez výstupu; každý má i `?` dotaz na readback):
 *   CALCulate:MATH:STATe ON|OFF   CALCulate:MATH:M <num>   CALCulate:MATH:B <num>
 *   CALCulate:NULL:STATe ON|OFF   CALCulate:NULL:ACQuire   (zachytí referenci z živého X)
 *   CALCulate:LIMit:STATe ON|OFF  CALCulate:LIMit:LOWer <hz>   CALCulate:LIMit:UPPer <hz>
 *   Argumenty: číslo (`1e6`, `-2.5`, `1.5E-3`, i s jednotkou `10.1MHZ`/`100KHZ`/`1GHZ`)
 *   nebo bool (`ON/OFF/1/0`). Chybný arg → −224.
 *   ⚠️ SET zapisují `g_meas_cfg` z UartTasku → commit celé cfg pod krátkou kritickou
 *   sekcí (UiTask nikdy nevidí roztržený double).
 *
 * ⚠️ **Chybová fronta** (`SYSTem:ERRor?`) + status registry jsou PER-SESSION
 * (`scpi_ctx_t`) — každý transport má vlastní kontext, takže USB CDC a budoucí
 * souběžný TCP 5025 na CM4 mají nezávislé fronty i status. `scpi_process` bez ctx
 * používá jediný sdílený default (USB konzole); TCP volá `scpi_process_ctx` s
 * vlastním kontextem per spojení. `*CLS` frontu i status daného ctx maže.
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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Kontext JEDNÉ SCPI session: chybová fronta + IEEE 488.2 status registry.
 *  Každý transport (USB CDC teď, TCP 5025 na CM4 pak) má vlastní instanci →
 *  nezávislé fronty i status. Zeroed = čistý stav (viz scpi_ctx_init). */
#define SCPI_ERRQ_N 8
typedef struct {
    int      err_q[SCPI_ERRQ_N];   /* kruhová fronta chybových kódů */
    uint8_t  err_head, err_count;
    uint8_t  esr;                  /* Standard Event Status Register (latched) */
    uint8_t  ese;                  /* Event Status Enable (*ESE) */
    uint8_t  sre;                  /* Service Request Enable (*SRE) */
} scpi_ctx_t;

/** Vynuluje kontext (prázdná fronta, ESR/ESE/SRE = 0). Volat před 1. použitím
 *  vlastního kontextu (např. při navázání TCP spojení). */
void scpi_ctx_init(scpi_ctx_t *ctx);

/** Zpracuje jednu programovou zprávu v daném kontextu (bez CRLF; víc jednotek
 *  přes ';'). Odpověď (vč. '\0') do out; vrací délku bez '\0' (0 = žádná). */
size_t scpi_process_ctx(scpi_ctx_t *ctx, const char *line, char *out, size_t out_sz);

/** Jako scpi_process_ctx, ale nad SDÍLENÝM default kontextem (USB CDC konzole,
 *  jediná session). Zachováno kvůli stávajícímu volajícímu (freertos_task_uart.c). */
size_t scpi_process(const char *line, char *out, size_t out_sz);

/** Pure-logic unit test (parser: case, krátká/dlouhá forma, hierarchie, status
 *  model, compound, jednotky) — 1 = PASS. Netestuje HW hodnoty (jen že odpoví). */
int scpi_selftest(void);

#ifdef __cplusplus
}
#endif
