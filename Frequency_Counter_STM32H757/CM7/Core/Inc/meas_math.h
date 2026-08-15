#pragma once
/**
 * @file    meas_math.h
 * @brief   Math (Mx+B, NULL/relativní) + limitní pass/fail test nad měřenou
 *          veličinou. STATUS.md #43 (Math Mx+B) + #44 (limit/pass-fail).
 *
 * Čistě logická vrstva (žádný HW, žádný sdílený stav uvnitř funkcí) — počítá
 * transformovanou hodnotu a verdikt PASS/FAIL. Idiom projektu: testuje se na
 * targetu přes `selftest` (`meas_math_selftest`, 8. test).
 *
 * Transformace:   Y = math_en ? (m*X + b) : X;   pak Y -= null_ref  (když null_en).
 * NULL zachytává referenci PŘED odečtem (`meas_math_capture_null`) → po zapnutí
 * ukazuje odchylku od té reference (relativní režim, na benchi „null then measure").
 *
 * Limity se vyhodnocují nad výslednou Y (absolutní meze lo/hi). Verdikt OFF, když
 * limity vypnuté; hranice jsou INKLUZIVNÍ do PASS (Y==lo i Y==hi = PASS).
 *
 * ⚠️ Aplikuje se na kmitočet měřený FPGA (dnes SIMULACE headline — viz CLAUDE.md;
 * `screen_main_freq_hz()`). Plný smysl až po reálném SPI linku (#2).
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MEAS_OFF = 0, MEAS_PASS, MEAS_LO, MEAS_HI } meas_verdict_t;

typedef struct {
    /* Math (Y = m*X + b, pak relativní odečet null_ref). */
    uint8_t math_en;    /* 1 = aplikuj m*X + b (jinak passthrough) */
    uint8_t null_en;    /* 1 = relativní režim (odečti null_ref) */
    double  m;          /* scale (default 1.0) */
    double  b;          /* offset [Hz] (default 0.0) */
    double  null_ref;   /* zachycená reference [Hz] (viz meas_math_capture_null) */
    /* Limity nad výslednou Y. */
    uint8_t limit_en;   /* 1 = vyhodnocuj pass/fail */
    uint8_t alarm_en;   /* 1 = FAIL → pípnutí (přes alarm.c, respektuje mute) */
    double  lo, hi;     /* meze [Hz] */
} meas_cfg_t;

/** Živá konfigurace (mění UiTask v okně MATH; čte UiTask + alarm.c). */
extern meas_cfg_t       g_meas_cfg;
/** Poslední verdikt (píše UiTask ve stats_sample, čte alarm.c pro edge). */
extern volatile uint8_t g_meas_verdict;

/** Výchozí: m=1, b=0, vše vypnuté. */
void            meas_math_defaults(meas_cfg_t *c);
/** Y = transformace X (bez ohledu na limity). */
double          meas_math_apply(const meas_cfg_t *c, double x);
/** Verdikt nad výslednou hodnotou y (OFF když limity vypnuté). */
meas_verdict_t  meas_limit_eval(const meas_cfg_t *c, double y);
/** Zachytí referenci = Y(X) BEZ null (math se aplikuje) a zapne null_en. */
void            meas_math_capture_null(meas_cfg_t *c, double x);
/** Pure-logic unit test (vektory apply/null/limit) — 1 = PASS. */
int             meas_math_selftest(void);

#ifdef __cplusplus
}
#endif
