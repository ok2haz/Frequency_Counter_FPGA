#pragma once
/**
 * @file    autocal.h
 * @brief   Autokalibrace / self-check přístroje. ROZPRACOVÁNO.
 *
 * Klasická „self-cal" funkce laboratorního přístroje. Dnes dělá **verifikaci
 * (guard-band check)** toho, co lze ověřit bez externí reference:
 *   - VREF (~2,5 V VREFBUF), napájecí větve 12V/5V, záložní baterie VBAT.
 * Kroky měnící koeficienty jsou **staged** (jasně označené, zatím se neprovádějí):
 *   - `adc_selfcal` = HW self-cal ADC3 (vyžaduje koordinovaný reinit v SensorsTasku),
 *   - `timebase`    = offset reference vůči GPS (⬅ reálné měření #2),
 *   - `rf`          = AD8307 slope/intercept (⬅ externí RF reference → okno Kalibrace).
 *
 * Bezpečné: čte jen `g_sensors[]`, nic nezapisuje do HW. Volá se z UART `autocal`
 * nebo tlačítka AUTO-CAL v okně Kalibrace (UiTask).
 */
#include <stdint.h>

typedef enum { AC_NA = 0, AC_PASS, AC_WARN, AC_FAIL } ac_result_t;

typedef struct {
    uint8_t     ran;         /* 1 = autocal_run() proběhl */
    ac_result_t vref;        /* VREF ~2,5 V */
    ac_result_t rail12;      /* 12V větev */
    ac_result_t rail5;       /* 5V větev */
    ac_result_t vbat;        /* záložní baterie */
    ac_result_t adc_selfcal; /* ADC3 HW self-cal (staged) */
    ac_result_t timebase;    /* GPS ref offset (staged, ⬅ #2) */
    ac_result_t rf;          /* RF gain (staged, ext. reference) */
} autocal_t;

extern autocal_t g_autocal;

/** Provede verifikační kroky z g_sensors; staged kroky nastaví na AC_NA. */
void        autocal_run(void);
/** Víceřádkový report (UART `autocal`). */
void        autocal_format_full(char *buf, int n);
/** Jednořádkový souhrn (status řádek okna Kalibrace). */
const char *autocal_summary(void);
/** Čistý verdikt hodnoty vůči nominálu (PASS/WARN/FAIL) — testovatelný. */
ac_result_t ac_verdict(float val, float nom, float tol_frac);
/** Pure-logic unit test verdiktu (součást UART `selftest`). 1 = PASS. */
int         autocal_selftest(void);
