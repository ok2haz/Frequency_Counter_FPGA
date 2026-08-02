/**
 * @file    autocal.c
 * @brief   Autokalibrace / self-check — viz autocal.h. ROZPRACOVÁNO.
 */
#include "autocal.h"
#include "sensor_stat.h"   /* g_sensors[] */
#include <stdio.h>

autocal_t g_autocal;

/* Čistý verdikt hodnoty vůči nominálu: |err| ≤ band = PASS, ≤ 2·band = WARN,
 * jinak FAIL (band = tol_frac·nom). Pure — testováno v autocal_selftest. */
ac_result_t ac_verdict(float val, float nom, float tol_frac)
{
    float band = nom * tol_frac; if (band < 0) band = -band;
    float err = val - nom;       if (err < 0) err = -err;
    if (err <= band)        return AC_PASS;
    if (err <= 2.0f * band) return AC_WARN;
    return AC_FAIL;
}

/* Verdikt senzoru; neplatný (bez vzorku / stale) = AC_NA. */
static ac_result_t check(sensor_id_t id, float nom, float tol_frac)
{
    const sensor_stat_t *s = &g_sensors[id];
    if (!s->samples || !s->valid) return AC_NA;
    return ac_verdict(s->last, nom, tol_frac);
}

void autocal_run(void)
{
    g_autocal.vref   = check(SENS_VDDA, 2500.0f, 0.04f);   /* VREFBUF ~2,5 V ±4 % */
    g_autocal.rail12 = check(SENS_ADS2, 12000.0f, 0.05f);  /* 12V ±5 % */
    g_autocal.rail5  = check(SENS_ADS3, 5000.0f, 0.05f);   /* 5V ±5 % */
    /* VBAT: baterie „slabá" pod ~2,5 V (jinak OK); ne symetrická tolerance. */
    {
        const sensor_stat_t *b = &g_sensors[SENS_VBAT];
        g_autocal.vbat = (!b->samples || !b->valid) ? AC_NA
                        : (b->last >= 2500.0f) ? AC_PASS
                        : (b->last >= 2200.0f) ? AC_WARN : AC_FAIL;
    }
    /* Staged (mění koeficienty / vyžadují externí referenci nebo reinit) — zatím NA. */
    g_autocal.adc_selfcal = AC_NA;   /* HW self-cal ADC3 — koordinovaný reinit v SensorsTasku */
    g_autocal.timebase    = AC_NA;   /* offset ref vůči GPS — ⬅ reálné měření (#2) */
    g_autocal.rf          = AC_NA;   /* AD8307 slope/intercept — externí RF reference */
    g_autocal.ran = 1;
}

static const char *rs(ac_result_t r)
{
    switch (r) { case AC_PASS: return "OK"; case AC_WARN: return "WARN";
                 case AC_FAIL: return "FAIL"; default: return "--"; }
}

void autocal_format_full(char *buf, int n)
{
    if (!g_autocal.ran) autocal_run();
    snprintf(buf, (size_t)n,
             "AUTO-CAL (self-check):\r\n"
             "  VREF 2V5 : %s\r\n"
             "  12V vetev: %s\r\n"
             "  5V vetev : %s\r\n"
             "  VBAT     : %s\r\n"
             "  ADC self-cal: %s (staged)\r\n"
             "  Timebase/GPS: %s (staged, ceka na mereni)\r\n"
             "  RF gain  : %s (staged, ext. reference -> okno Kalibrace)\r\n",
             rs(g_autocal.vref), rs(g_autocal.rail12), rs(g_autocal.rail5),
             rs(g_autocal.vbat), rs(g_autocal.adc_selfcal), rs(g_autocal.timebase),
             rs(g_autocal.rf));
}

int autocal_selftest(void)
{
    /* Verdikt pásma (band = 4 % z 2500 ≈ 100; PASS ≤100, WARN ≤200, FAIL >200).
     * Hodnoty voleny mimo hranice (float tol není přesně 100 → boundary křehká). */
    if (ac_verdict(2500.0f, 2500.0f, 0.04f) != AC_PASS) return 0;   /* err 0    */
    if (ac_verdict(2550.0f, 2500.0f, 0.04f) != AC_PASS) return 0;   /* err 50   */
    if (ac_verdict(2650.0f, 2500.0f, 0.04f) != AC_WARN) return 0;   /* err 150  */
    if (ac_verdict(2750.0f, 2500.0f, 0.04f) != AC_FAIL) return 0;   /* err 250  */
    if (ac_verdict(2250.0f, 2500.0f, 0.04f) != AC_FAIL) return 0;   /* -250 sym */
    return 1;
}

const char *autocal_summary(void)
{
    static char s[64];
    if (!g_autocal.ran) return "AUTO-CAL: nespusteno";
    /* Nejhorší verdikt z verifikačních kroků → celkový stav. */
    ac_result_t worst = AC_PASS;
    ac_result_t chk[4] = { g_autocal.vref, g_autocal.rail12, g_autocal.rail5, g_autocal.vbat };
    for (int i = 0; i < 4; i++) if (chk[i] > worst && chk[i] != AC_NA) worst = chk[i];
    snprintf(s, sizeof s, "AUTO-CAL: VREF %s  12V %s  5V %s  VBAT %s  [%s]",
             rs(g_autocal.vref), rs(g_autocal.rail12), rs(g_autocal.rail5),
             rs(g_autocal.vbat), rs(worst));
    return s;
}
