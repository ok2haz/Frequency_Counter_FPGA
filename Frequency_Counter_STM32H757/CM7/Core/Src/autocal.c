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

/* v0.45.26: verdikty do PREDANE struktury, bez zapisu do g_autocal -- okno AUTOKALIBRACE (UiTask) je pocita zive 2x/s
 * a nesmi prepisovat stav, ktery cte UART `autocal` (F-0126: getter, ktery zapisuje, je past). autocal_run = totez do g_autocal. */
void autocal_eval(autocal_t *o)
{
    /* VREFBUF ~2,5 V ±4 %. F-0298: VREF+ mimo VREF_OK_MIN..MAX_MV (= presne pasmo PASS) oznaci SensorsTask za NEPLATNOU
     * (#385: teplota jadra a VBAT by staly na spatne referenci) a `check()` by z toho udelal "--" -- souhrn i barva tlacitka
     * AUTO-CAL by pak byly zelene prave pri teto poruse. Pro samokontrolu je to nalez: posledni hodnota mimo meze (zustava
     * v `last`) se hodnoti jako WARN / FAIL. "--" zustava jen pro nezmereno a pro chybu cteni ADC s hodnotou v mezich. */
    {
        const sensor_stat_t *v = &g_sensors[SENS_VDDA];
        o->vref = (v->samples && !v->valid &&
                   (v->last < (float)VREF_OK_MIN_MV || v->last > (float)VREF_OK_MAX_MV))
                  ? ac_verdict(v->last, 2500.0f, 0.04f) : check(SENS_VDDA, 2500.0f, 0.04f);
    }
    /* 🔴 2026-10-02: AIN2 je fyzicky +3V3, ne 12V (overeno netlistem
     * FPGA_Module_2_1, viz calib.c pro plne zduvodneni). Pole `rail12`
     * zustava jako historicky nazev. */
    o->rail12 = check(SENS_ADS2, 3300.0f, 0.05f);  /* +3V3 ±5 % */
    o->rail5  = check(SENS_ADS3, 5000.0f, 0.05f);   /* 5V ±5 % */
    /* VBAT = napeti BATERIE (pin + korekce, STATUS #322): OK od 2,70 V, vyhrada 2,40-2,70 V, kriticke pod 2,40 V; ne symetricka tolerance. */
    {
        const sensor_stat_t *b = &g_sensors[SENS_VBAT];
        /* STATUS #386: baterie chybi (pod VBAT_MISSING_MV) = vyhrada, ne kriticka vybita baterie. */
        o->vbat = (!b->samples || !b->valid) ? AC_NA
                 : vbat_missing(b) ? AC_WARN
                 : (b->last >= 2700.0f) ? AC_PASS
                 : (b->last >= 2400.0f) ? AC_WARN : AC_FAIL;
    }
    /* Staged (mění koeficienty / vyžadují externí referenci nebo reinit) — zatím NA. */
    o->adc_selfcal = AC_NA;   /* HW self-cal ADC3 — koordinovaný reinit v SensorsTasku */
    o->timebase    = AC_NA;   /* offset ref vůči GPS — ⬅ reálné měření (#2) */
    o->rf          = AC_NA;   /* AD8307 slope/intercept — HW na teto desce neni (RF_LEVEL_HW_PRESENT) */
    o->ran = 1;
}

void autocal_run(void)
{
    autocal_eval(&g_autocal);
}

static const char *rs(ac_result_t r)
{
    switch (r) { case AC_PASS: return "OK"; case AC_WARN: return "WARN";
                 case AC_FAIL: return "FAIL"; default: return "--"; }
}

/* >> DOTAZ, KTERY MUTOVAL (audit F-0126). `autocal_format_full` pri `!ran`
 * spoustela cele mereni, takze formatovaci funkce menila globalni stav — a volaji
 * ji DVE ulohy: UiTask (okno Kalibrace) a UartTask (`autocal`). Dopad je maly, oba
 * behy ctou tentyz `g_sensors[]` a nic se neuklada ani nerid, ale „getter, ktery
 * zapisuje" je presne ta vec, kterou pristi ctenar neocekava.
 * Ted se mereni nespousti odsud: kdyz jeste nebezelo, funkce to REKNE. Spustit ho
 * ma volajici (tlacitko AUTO-CAL, UART `autocal`), tedy tam, kde je to zamer. */
void autocal_format_full(char *buf, int n)
{
    if (!g_autocal.ran) {
        snprintf(buf, (size_t)n,
                 "AUTO-CAL (self-check):\r\n"
                 "  jeste nebezel — spust tlacitkem AUTO-CAL nebo prikazem `autocal`\r\n");
        return;
    }
    snprintf(buf, (size_t)n,
             "AUTO-CAL (self-check):\r\n"
             "  VREF 2V5 : %s\r\n"
             "  12V vetev: %s\r\n"
             "  5V vetev : %s\r\n"
             "  VBAT     : %s%s\r\n"
             "  ADC self-cal: %s (staged)\r\n"
             "  Timebase/GPS: %s (staged, ceka na mereni)\r\n"
             "  RF gain  : %s (staged, ext. reference -> okno Kalibrace)\r\n",
             rs(g_autocal.vref), rs(g_autocal.rail12), rs(g_autocal.rail5),
             rs(g_autocal.vbat), vbat_missing(&g_sensors[SENS_VBAT]) ? " (baterie chybi)" : "",
             rs(g_autocal.adc_selfcal), rs(g_autocal.timebase),
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
    snprintf(s, sizeof s, "AUTO-CAL: VREF %s  3V3 %s  5V %s  VBAT %s  [%s]",
             rs(g_autocal.vref), rs(g_autocal.rail12), rs(g_autocal.rail5),
             rs(g_autocal.vbat), rs(worst));
    return s;
}
