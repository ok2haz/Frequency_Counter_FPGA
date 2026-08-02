/**
 * @file    meas_math.c
 * @brief   Math (Mx+B, NULL) + limitní pass/fail — viz meas_math.h.
 */
#include "meas_math.h"

/* Živý stav (čte/píše UiTask; alarm.c čte g_meas_verdict + g_meas_cfg.alarm_en). */
meas_cfg_t       g_meas_cfg      = { .m = 1.0, .b = 0.0 };   /* zbytek 0 = vypnuto */
volatile uint8_t g_meas_verdict  = MEAS_OFF;

void meas_math_defaults(meas_cfg_t *c)
{
    c->math_en = 0; c->null_en = 0; c->m = 1.0; c->b = 0.0; c->null_ref = 0.0;
    c->limit_en = 0; c->alarm_en = 0; c->lo = 0.0; c->hi = 0.0;
}

double meas_math_apply(const meas_cfg_t *c, double x)
{
    double y = c->math_en ? (c->m * x + c->b) : x;
    if (c->null_en) y -= c->null_ref;
    return y;
}

meas_verdict_t meas_limit_eval(const meas_cfg_t *c, double y)
{
    if (!c->limit_en) return MEAS_OFF;
    if (y < c->lo)    return MEAS_LO;    /* meze inkluzivní do PASS */
    if (y > c->hi)    return MEAS_HI;
    return MEAS_PASS;
}

void meas_math_capture_null(meas_cfg_t *c, double x)
{
    meas_cfg_t t = *c;
    t.null_en = 0;                       /* referenci ber PŘED odečtem */
    c->null_ref = meas_math_apply(&t, x);
    c->null_en  = 1;
}

/* ── Pure-logic unit test (8. položka selftestu) ────────────────────────────── */
static int feq(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-6; }

int meas_math_selftest(void)
{
    meas_cfg_t c; meas_math_defaults(&c);

    /* 1) Passthrough když math vypnuté. */
    if (!feq(meas_math_apply(&c, 12345.678), 12345.678)) return 0;

    /* 2) Scale + offset: Y = 2*X + 100. */
    c.math_en = 1; c.m = 2.0; c.b = 100.0;
    if (!feq(meas_math_apply(&c, 10.0), 120.0)) return 0;

    /* 3) NULL: reference = Y(10) = 120 → po zapnutí Y(10) == 0. */
    meas_math_capture_null(&c, 10.0);
    if (!c.null_en || !feq(c.null_ref, 120.0)) return 0;
    if (!feq(meas_math_apply(&c, 10.0), 0.0)) return 0;
    /* posun X o +5 → Y = (2*15+100) - 120 = +10. */
    if (!feq(meas_math_apply(&c, 15.0), 10.0)) return 0;

    /* 4) Limity vypnuté → OFF. */
    if (meas_limit_eval(&c, 0.0) != MEAS_OFF) return 0;

    /* 5) Limity ±1: PASS/HI/LO + inkluzivní hranice. */
    c.limit_en = 1; c.lo = -1.0; c.hi = 1.0;
    if (meas_limit_eval(&c,  0.0) != MEAS_PASS) return 0;
    if (meas_limit_eval(&c,  2.0) != MEAS_HI)   return 0;
    if (meas_limit_eval(&c, -2.0) != MEAS_LO)   return 0;
    if (meas_limit_eval(&c,  1.0) != MEAS_PASS) return 0;   /* == hi → PASS */
    if (meas_limit_eval(&c, -1.0) != MEAS_PASS) return 0;   /* == lo → PASS */

    return 1;
}
