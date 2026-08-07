/**
 * @file    scpi.c
 * @brief   SCPI-99 parser + dispatch (#25). Transportně nezávislý; viz scpi.h.
 */
#include "scpi.h"
#include "version.h"           /* FW_VERSION_FULL — *IDN? */
#include "fpga_freq.h"         /* fpga_freq_get_last — REÁLNÝ kmitočet (ne sim) */
#include "gps.h"               /* gps_get — SYST:GPS:STAT? */
#include "sensor_stat.h"       /* g_sensors[] — SYST:TEMP? */
#include "freertos_shared.h"   /* g_spi_ok, g_si5356_status/_ok — STAT:OPER? */
#include <string.h>
#include <stdio.h>

/* ── Keyword matcher (SCPI krátká/dlouhá forma, case-insensitive) ────────────
 * pat = keyword s VELKÝMI (povinné) + malými (volitelné) písmeny, např. "MEASure".
 * Vstup matchne, pokud (a) délka je [povinné..plné] a (b) case-insensitive shoda
 * na celé délce vstupu. */
static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static int kw_match(const char *in, int in_len, const char *pat, int pat_len)
{
    int mand = 0;
    while (mand < pat_len && pat[mand] >= 'A' && pat[mand] <= 'Z') mand++;
    if (in_len < mand || in_len > pat_len) return 0;
    for (int i = 0; i < in_len; i++)
        if (up(in[i]) != up(pat[i])) return 0;
    return 1;
}

/* Match celé hlavičky (dvojtečková hierarchie) proti vzoru — keyword po keywordu. */
static int hdr_match(const char *hdr, const char *pattern)
{
    for (;;) {
        int hl = 0; while (hdr[hl]     && hdr[hl]     != ':') hl++;
        int pl = 0; while (pattern[pl] && pattern[pl] != ':') pl++;
        if (!kw_match(hdr, hl, pattern, pl)) return 0;
        int hdr_more = (hdr[hl]     == ':');
        int pat_more = (pattern[pl] == ':');
        if (hdr_more != pat_more) return 0;   /* různá hloubka */
        if (!hdr_more) return 1;              /* obě skončila shodně */
        hdr += hl + 1; pattern += pl + 1;
    }
}

/* uint64 Hz*1e5 -> "12345678.90123" (bez float printf; Hz < 4.29e9 -> uint32). */
static void fmt_scpi_hz(uint64_t x100000, char *out, size_t n)
{
    uint32_t whole = (uint32_t)(x100000 / 100000u);   /* Hz (do ~1.4 GHz < 4.29e9) */
    uint32_t frac  = (uint32_t)(x100000 % 100000u);
    snprintf(out, n, "%lu.%05lu", (unsigned long)whole, (unsigned long)frac);
}

/* float -> "d.dd" na 2 des. místa (bez float printf), se znaménkem. */
static void fmt_scpi_f2(float v, char *out, size_t n)
{
    int neg = (v < 0.0f); if (neg) v = -v;
    int t = (int)(v * 100.0f + 0.5f);
    snprintf(out, n, "%s%d.%02d", neg ? "-" : "", t / 100, t % 100);
}

size_t scpi_process(const char *line, char *out, size_t out_sz)
{
    if (line == NULL || out == NULL || out_sz == 0) return 0;
    out[0] = '\0';

    while (*line == ' ' || *line == '\t') line++;

    /* Hlavička = do mezery / '?' / konce. */
    char hdr[48]; int n = 0, is_query = 0;
    while (*line && *line != ' ' && *line != '\t' && *line != '?' && n < (int)sizeof(hdr) - 1)
        hdr[n++] = *line++;
    hdr[n] = '\0';
    if (*line == '?') { is_query = 1; line++; }
    /* (argumenty za mezerou zatím nezpracováváme — první řez je jen dotazy + akce) */

    /* ── Common commands (IEEE 488.2) ──────────────────────────────────────── */
    if (hdr_match(hdr, "*IDN") && is_query) {
        snprintf(out, out_sz, "OK2HAZ,GPSDO-Counter,0,%s", FW_VERSION_FULL);
        return strlen(out);
    }
    if (hdr_match(hdr, "*OPC") && is_query) { snprintf(out, out_sz, "1"); return strlen(out); }
    if (hdr_match(hdr, "*RST") && !is_query) { return 0; }   /* no-op (nesahá na HW) */
    if (hdr_match(hdr, "*CLS") && !is_query) { return 0; }   /* clear (stub error queue) */

    /* ── SYSTem ────────────────────────────────────────────────────────────── */
    if (hdr_match(hdr, "SYSTem:VERSion") && is_query) { snprintf(out, out_sz, "1999.0"); return strlen(out); }
    if (hdr_match(hdr, "SYSTem:ERRor") && is_query) {
        /* Stub error queue — plná fronta bude s error handlingem (viz #25 TODO). */
        snprintf(out, out_sz, "0,\"No error\"");
        return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:TEMPerature") && is_query) {   /* OCXO (0x49) [°C] */
        const sensor_stat_t *s = &g_sensors[SENS_T49];
        if (s->valid) fmt_scpi_f2(s->last, out, out_sz);
        else          snprintf(out, out_sz, "9.91E37");       /* NaN = neplatné čtení */
        return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:GPS:STATus") && is_query) {    /* fix_mode,num_sat */
        gps_data_t g; gps_get(&g);
        snprintf(out, out_sz, "%u,%u", (unsigned)g.fix_mode, (unsigned)g.num_sat);
        return strlen(out);
    }

    /* ── MEASure ──────────────────────────────────────────────────────────────
     * ⚠️ REÁLNÝ FPGA kmitočet (ne simulovaný headline). Bez platného měření -> NaN. */
    if (hdr_match(hdr, "MEASure:FREQuency") && is_query) {
        fpga_meas_t m;
        int ok = fpga_freq_get_last(&m) && (m.measurement_status & 0x01u)
                 && !(m.error_flags & FPGA_ERR_SIGNAL_LOST);
        if (ok) fmt_scpi_hz(m.frequency_x100000, out, out_sz);
        else    snprintf(out, out_sz, "9.91E37");             /* SCPI not-a-number */
        return strlen(out);
    }

    /* ── STATus ───────────────────────────────────────────────────────────────
     * Kondiční slovo: bit0 FPGA link, bit1 GPS lock, bit2 reference lock. */
    if (hdr_match(hdr, "STATus:OPERation:CONDition") && is_query) {
        gps_data_t g; gps_get(&g);
        unsigned w = 0;
        if (g_spi_ok)                        w |= 1u << 0;
        if (g.valid && g.fix_mode >= 2)      w |= 1u << 1;
        if (g_si5356_ok && !(g_si5356_status & (1u << 3)) && !(g_si5356_status & (1u << 4)))
                                             w |= 1u << 2;    /* bit3 LOS_CLKIN, bit4 PLL_LOL */
        snprintf(out, out_sz, "%u", w);
        return strlen(out);
    }

    /* Neznámý příkaz — SCPI chyba (zjednodušeně; plná fronta = TODO #25). */
    snprintf(out, out_sz, "-113,\"Undefined header\"");
    return strlen(out);
}

/* ── Selftest (parser: case, krátká/dlouhá forma, hierarchie, neznámý) ──────── */
int scpi_selftest(void)
{
    int ok = 1; char b[80];

    scpi_process("*IDN?", b, sizeof b);            ok &= (strncmp(b, "OK2HAZ,", 7) == 0);
    scpi_process("*idn?", b, sizeof b);            ok &= (strncmp(b, "OK2HAZ,", 7) == 0);  /* case */
    scpi_process("*OPC?", b, sizeof b);            ok &= (strcmp(b, "1") == 0);
    ok &= (scpi_process("*RST", b, sizeof b) == 0);                                        /* akce = 0 */

    scpi_process("SYST:ERR?", b, sizeof b);        ok &= (b[0] == '0');                     /* krátká forma */
    scpi_process("SYSTem:ERRor?", b, sizeof b);    ok &= (b[0] == '0');                     /* dlouhá forma */
    scpi_process("system:version?", b, sizeof b);  ok &= (strcmp(b, "1999.0") == 0);        /* case+dlouhá */

    { size_t nn = scpi_process("FOO:BAR?", b, sizeof b);                                    /* neznámý */
      ok &= (nn > 0 && b[0] == '-'); }
    { size_t nn = scpi_process("MEAS:FREQ?", b, sizeof b); ok &= (nn > 0); }                /* odpoví (NaN bez HW) */
    { size_t nn = scpi_process("SYST:ERR", b, sizeof b); ok &= (nn > 0 && b[0] == '-'); }   /* dotaz bez '?' = neznámý */

    return ok;
}
