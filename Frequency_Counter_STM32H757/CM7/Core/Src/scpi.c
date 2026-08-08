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
#include "meas_math.h"         /* g_meas_cfg — CALCulate subsystem */
#include "FreeRTOS.h"          /* taskENTER_CRITICAL — atomický commit g_meas_cfg */
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ── Keyword matcher (SCPI krátká/dlouhá forma, case-insensitive) ────────────
 * pat = keyword s VELKÝMI (povinné) + malými (volitelné) písmeny, např. "MEASure".
 * Vstup matchne, pokud (a) délka je [povinné..plné] a (b) case-insensitive shoda
 * na celé délce vstupu. */
static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static int kw_match(const char *in, int in_len, const char *pat, int pat_len)
{
    /* Povinne = vse KROME koncove male abbreviace (ne "vedouci velka") — jinak by
     * '*' u common commandu (*IDN) dal mand=0 a matchnul i zkraceniny (*ID?, *?). */
    int opt = 0;
    while (opt < pat_len && pat[pat_len - 1 - opt] >= 'a' && pat[pat_len - 1 - opt] <= 'z') opt++;
    int mand = pat_len - opt;
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

/* double Hz -> "±d.ddddd" (5 des. míst, integer extrakce; pro CALC:DATA?). */
static void fmt_scpi_hz_d(double hz, char *out, size_t n)
{
    int neg = (hz < 0.0); if (neg) hz = -hz;
    uint64_t x = (uint64_t)(hz * 100000.0 + 0.5);
    uint32_t whole = (uint32_t)(x / 100000u);
    uint32_t frac  = (uint32_t)(x % 100000u);
    snprintf(out, n, "%s%lu.%05lu", neg ? "-" : "", (unsigned long)whole, (unsigned long)frac);
}

/* stupně -> "±d.dddddd" (6 des. míst; pro SYST:GPS:POS?). */
static void fmt_scpi_deg6(float v, char *out, size_t n)
{
    int neg = (v < 0.0f); if (neg) v = -v;
    int32_t ud = (int32_t)(v * 1000000.0f + 0.5f);   /* mikro-stupně */
    snprintf(out, n, "%s%ld.%06ld", neg ? "-" : "", (long)(ud / 1000000), (long)(ud % 1000000));
}

/* ── Chybová fronta (SCPI-99 SYSTem:ERRor?) ──────────────────────────────────
 * ⚠️ Modulový stav = JEDNA session. Dnes stačí (jediný transport = USB CDC).
 * Až přibude souběžný TCP 5025 na CM4, chybová fronta se musí přesunout do
 * per-session kontextu (scpi_process je jinak čistá funkce). */
#define SCPI_ERRQ_N 8
static int     s_err_q[SCPI_ERRQ_N];
static uint8_t s_err_head, s_err_count;

static const char *scpi_err_msg(int code)
{
    switch (code) {
        case 0:    return "No error";
        case -100: return "Command error";
        case -113: return "Undefined header";
        case -222: return "Data out of range";
        case -224: return "Illegal parameter value";
        case -230: return "Data corrupt or stale";
        case -350: return "Queue overflow";
        default:   return "Error";
    }
}
static void scpi_err_push(int code)
{
    if (s_err_count >= SCPI_ERRQ_N) {              /* plná fronta -> overflow marker */
        s_err_q[(s_err_head + SCPI_ERRQ_N - 1) % SCPI_ERRQ_N] = -350;
        return;
    }
    s_err_q[(s_err_head + s_err_count) % SCPI_ERRQ_N] = code;
    s_err_count++;
}
static int scpi_err_pop(void)                       /* 0 = prázdná (No error) */
{
    if (s_err_count == 0) return 0;
    int code = s_err_q[s_err_head];
    s_err_head = (uint8_t)((s_err_head + 1) % SCPI_ERRQ_N);
    s_err_count--;
    return code;
}
static void scpi_err_clear(void) { s_err_head = 0; s_err_count = 0; }

/* Poslední REÁLNÝ (ne simulovaný) kmitočet /4 v Hz. @return 1 = platné. */
static int scpi_real_freq_hz(double *hz)
{
    fpga_meas_t m;
    if (fpga_freq_get_last(&m) && (m.measurement_status & 0x01u)
        && !(m.error_flags & FPGA_ERR_SIGNAL_LOST)) {
        *hz = (double)m.frequency_x100000 / 100000.0;
        return 1;
    }
    return 0;
}

/* ── Parsování argumentů SET příkazů (bez libc, testovatelné) ─────────────────
 * Číslo: znaménko, celá/desetinná část, exponent (e/E). *ok=0 pokud žádná číslice. */
static double scpi_num(const char *s, int *ok)
{
    while (*s == ' ' || *s == '\t') s++;
    double sign = 1.0;
    if (*s == '-') { sign = -1.0; s++; } else if (*s == '+') s++;
    double v = 0.0; int digits = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); s++; digits++; }
    if (*s == '.') {
        s++; double f = 0.1;
        while (*s >= '0' && *s <= '9') { v += (double)(*s - '0') * f; f *= 0.1; s++; digits++; }
    }
    if (*s == 'e' || *s == 'E') {
        s++; int es = 1;
        if (*s == '-') { es = -1; s++; } else if (*s == '+') s++;
        int e = 0; while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
        while (e-- > 0) v = (es > 0) ? v * 10.0 : v * 0.1;
    }
    if (ok) *ok = (digits > 0);
    return v * sign;
}

/* Bool: ON/OFF/1/0 (case-insensitive). *ok=0 při neznámém. */
static int scpi_bool(const char *s, int *ok)
{
    while (*s == ' ' || *s == '\t') s++;
    if (ok) *ok = 1;
    if ((up(s[0]) == 'O' && up(s[1]) == 'N') || s[0] == '1') return 1;
    if ((up(s[0]) == 'O' && up(s[1]) == 'F' && up(s[2]) == 'F') || s[0] == '0') return 0;
    if (ok) *ok = 0;
    return 0;
}

/* Aplikuje CALC SET (hdr, arg) na PŘEDANOU cfg (bezstavové → testovatelné lokální
 * cfg v selftestu). @return 1 = hlavička rozpoznána (setter). *err=1 při chybném
 * argumentu. NULL:ACQuire a *:STATe? dotazy řeší scpi_process (potřebují živý stav). */
static int scpi_calc_set(meas_cfg_t *c, const char *hdr, const char *arg, int *err)
{
    int ok = 0; *err = 0;
    if (hdr_match(hdr, "CALCulate:MATH:STATe"))  { int v = scpi_bool(arg, &ok); if (ok) c->math_en  = (uint8_t)v; else *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:MATH:M"))      { double v = scpi_num(arg, &ok); if (ok) c->m  = v; else *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:MATH:B"))      { double v = scpi_num(arg, &ok); if (ok) c->b  = v; else *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:NULL:STATe"))  { int v = scpi_bool(arg, &ok); if (ok) c->null_en  = (uint8_t)v; else *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:LIMit:STATe")) { int v = scpi_bool(arg, &ok); if (ok) c->limit_en = (uint8_t)v; else *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:LIMit:LOWer")) { double v = scpi_num(arg, &ok); if (ok) c->lo = v; else *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:LIMit:UPPer")) { double v = scpi_num(arg, &ok); if (ok) c->hi = v; else *err = 1; return 1; }
    return 0;
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
    while (*line == ' ' || *line == '\t') line++;
    const char *arg = line;   /* zbytek řádku = argument SET příkazu (může být "") */

    /* ── Common commands (IEEE 488.2) ──────────────────────────────────────── */
    if (hdr_match(hdr, "*IDN") && is_query) {
        snprintf(out, out_sz, "OK2HAZ,GPSDO-Counter,0,%s", FW_VERSION_FULL);
        return strlen(out);
    }
    if (hdr_match(hdr, "*OPC") && is_query) { snprintf(out, out_sz, "1"); return strlen(out); }
    if (hdr_match(hdr, "*RST") && !is_query) { return 0; }   /* no-op (nesahá na HW) */
    if (hdr_match(hdr, "*CLS") && !is_query) { scpi_err_clear(); return 0; }  /* clear status/err */

    /* ── SYSTem ────────────────────────────────────────────────────────────── */
    if (hdr_match(hdr, "SYSTem:VERSion") && is_query) { snprintf(out, out_sz, "1999.0"); return strlen(out); }
    if (hdr_match(hdr, "SYSTem:ERRor") && is_query) {
        int code = scpi_err_pop();
        snprintf(out, out_sz, "%d,\"%s\"", code, scpi_err_msg(code));
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
    if (hdr_match(hdr, "SYSTem:GPS:TIME") && is_query) {      /* UTC hh:mm:ss (RMC) */
        gps_data_t g; gps_get(&g);
        if (g.valid) snprintf(out, out_sz, "%02u:%02u:%02u",
                              (unsigned)g.hour, (unsigned)g.minute, (unsigned)g.second);
        else         snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:GPS:POSition") && is_query) {  /* lat,lon,alt (°,°,m) */
        gps_data_t g; gps_get(&g);
        if (g.valid) {
            char la[16], lo[16];
            fmt_scpi_deg6(g.lat_deg, la, sizeof la);
            fmt_scpi_deg6(g.lon_deg, lo, sizeof lo);
            snprintf(out, out_sz, "%s,%s,%d", la, lo, (int)g.alt_m);
        } else snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }

    /* ── MEASure ──────────────────────────────────────────────────────────────
     * ⚠️ REÁLNÝ FPGA kmitočet (ne simulovaný headline). Bez platného měření -> NaN. */
    if (hdr_match(hdr, "MEASure:FREQuency:DIV16") && is_query) {  /* pin27 /16 větev */
        fpga_meas_t m;
        int ok = fpga_freq_get_last(&m) && (m.measurement_status & 0x01u)
                 && !(m.error_flags & FPGA_ERR_SIGNAL_LOST)
                 && !(m.status2 & FPGA_ST2_DIV16_ERR);
        if (ok) fmt_scpi_hz(m.freq16_x100000, out, out_sz);
        else    snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if (hdr_match(hdr, "MEASure:FREQuency") && is_query) {
        fpga_meas_t m;
        int ok = fpga_freq_get_last(&m) && (m.measurement_status & 0x01u)
                 && !(m.error_flags & FPGA_ERR_SIGNAL_LOST);
        if (ok) fmt_scpi_hz(m.frequency_x100000, out, out_sz);
        else    snprintf(out, out_sz, "9.91E37");             /* SCPI not-a-number */
        return strlen(out);
    }

    /* ── CALCulate (Math Mx+B/NULL + limitní pass/fail, viz meas_math.c) ────────
     * Aplikuje živou konfiguraci `g_meas_cfg` na REÁLNÝ /4 kmitočet. */
    if (hdr_match(hdr, "CALCulate:DATA") && is_query) {       /* Y = math(X) [Hz] */
        double hz;
        if (scpi_real_freq_hz(&hz)) fmt_scpi_hz_d(meas_math_apply(&g_meas_cfg, hz), out, out_sz);
        else                        snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if (hdr_match(hdr, "CALCulate:LIMit:FAIL") && is_query) { /* 0=v mezích/off, 1=FAIL */
        double hz;
        if (scpi_real_freq_hz(&hz)) {
            meas_verdict_t v = meas_limit_eval(&g_meas_cfg, meas_math_apply(&g_meas_cfg, hz));
            snprintf(out, out_sz, "%d", (v == MEAS_LO || v == MEAS_HI) ? 1 : 0);
        } else snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    /* CALC config readback (dotazy k SET příkazům níže). Konzistentní snapshot cfg. */
    if (is_query && (hdr_match(hdr, "CALCulate:MATH:STATe") || hdr_match(hdr, "CALCulate:MATH:M") ||
                     hdr_match(hdr, "CALCulate:MATH:B") || hdr_match(hdr, "CALCulate:NULL:STATe") ||
                     hdr_match(hdr, "CALCulate:LIMit:STATe") || hdr_match(hdr, "CALCulate:LIMit:LOWer") ||
                     hdr_match(hdr, "CALCulate:LIMit:UPPer"))) {
        meas_cfg_t c; taskENTER_CRITICAL(); c = g_meas_cfg; taskEXIT_CRITICAL();
        if      (hdr_match(hdr, "CALCulate:MATH:STATe"))  snprintf(out, out_sz, "%u", (unsigned)c.math_en);
        else if (hdr_match(hdr, "CALCulate:MATH:M"))      fmt_scpi_hz_d(c.m,  out, out_sz);
        else if (hdr_match(hdr, "CALCulate:MATH:B"))      fmt_scpi_hz_d(c.b,  out, out_sz);
        else if (hdr_match(hdr, "CALCulate:NULL:STATe"))  snprintf(out, out_sz, "%u", (unsigned)c.null_en);
        else if (hdr_match(hdr, "CALCulate:LIMit:STATe")) snprintf(out, out_sz, "%u", (unsigned)c.limit_en);
        else if (hdr_match(hdr, "CALCulate:LIMit:LOWer")) fmt_scpi_hz_d(c.lo, out, out_sz);
        else                                             fmt_scpi_hz_d(c.hi, out, out_sz);
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

    /* ── CALC SET příkazy (akce, žádný výstup; zápis g_meas_cfg z UartTasku) ─────
     * Parsuje se nad LOKÁLNÍ kopií, commit celé cfg pod krátkou kritickou sekcí →
     * UiTask nikdy nevidí roztržený double (task switch je během copy maskovaný). */
    if (!is_query) {
        if (hdr_match(hdr, "CALCulate:NULL:ACQuire")) {   /* zachyť referenci z živého X */
            double hz;
            if (scpi_real_freq_hz(&hz)) {
                meas_cfg_t tmp; taskENTER_CRITICAL(); tmp = g_meas_cfg; taskEXIT_CRITICAL();
                meas_math_capture_null(&tmp, hz);
                taskENTER_CRITICAL(); g_meas_cfg = tmp; taskEXIT_CRITICAL();
                return 0;
            }
            scpi_err_push(-230);
            snprintf(out, out_sz, "-230,\"Data corrupt or stale\"");
            return strlen(out);
        }
        meas_cfg_t tmp; taskENTER_CRITICAL(); tmp = g_meas_cfg; taskEXIT_CRITICAL();
        int err = 0;
        if (scpi_calc_set(&tmp, hdr, arg, &err)) {
            if (err) { scpi_err_push(-224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out); }
            taskENTER_CRITICAL(); g_meas_cfg = tmp; taskEXIT_CRITICAL();
            return 0;   /* akce OK → ticho */
        }
    }

    /* Neznámý příkaz — do chybové fronty (SYST:ERR?) + inline odpověď pro konzoli. */
    scpi_err_push(-113);
    snprintf(out, out_sz, "-113,\"Undefined header\"");
    return strlen(out);
}

/* ── Selftest (parser: case, krátká/dlouhá forma, hierarchie, neznámý) ──────── */
int scpi_selftest(void)
{
    int ok = 1; char b[80];
    scpi_err_clear();   /* čistý start (selftest běží i za provozu) */

    scpi_process("*IDN?", b, sizeof b);            ok &= (strncmp(b, "OK2HAZ,", 7) == 0);
    scpi_process("*idn?", b, sizeof b);            ok &= (strncmp(b, "OK2HAZ,", 7) == 0);  /* case */
    scpi_process("*OPC?", b, sizeof b);            ok &= (strcmp(b, "1") == 0);
    ok &= (scpi_process("*RST", b, sizeof b) == 0);                                        /* akce = 0 */

    scpi_process("SYST:ERR?", b, sizeof b);        ok &= (strncmp(b, "0,", 2) == 0);        /* prázdná fronta */
    scpi_process("SYSTem:ERRor?", b, sizeof b);    ok &= (strncmp(b, "0,", 2) == 0);        /* dlouhá forma */
    scpi_process("system:version?", b, sizeof b);  ok &= (strcmp(b, "1999.0") == 0);        /* case+dlouhá */

    /* Nové dotazy — jen že odpoví (bez HW/fixu vrací NaN 9.91E37). */
    ok &= (scpi_process("MEAS:FREQ?", b, sizeof b) > 0);
    ok &= (scpi_process("MEAS:FREQ:DIV16?", b, sizeof b) > 0);   /* /16 větev */
    ok &= (scpi_process("SYST:GPS:TIME?", b, sizeof b) > 0);
    ok &= (scpi_process("SYST:GPS:POS?", b, sizeof b) > 0);      /* krátká forma POSition */
    ok &= (scpi_process("CALC:DATA?", b, sizeof b) > 0);         /* Math Y=m*X+b */
    ok &= (scpi_process("CALC:LIM:FAIL?", b, sizeof b) > 0);     /* limit pass/fail */

    /* Chybová fronta: neznámý příkaz -> -113 inline i do fronty; SYST:ERR? popne. */
    scpi_err_clear();
    { size_t nn = scpi_process("FOO:BAR?", b, sizeof b);          ok &= (nn > 0 && b[0] == '-'); }
    scpi_process("*ID?", b, sizeof b);             ok &= (b[0] == '-');   /* zkracenina NESMI matchnout *IDN */
    scpi_process("?", b, sizeof b);                ok &= (b[0] == '-');   /* prázdná hlavička != *IDN */
    scpi_process("SYST:TEMPXY?", b, sizeof b);     ok &= (b[0] == '-');   /* delší než plná forma neprojde */
    { size_t nn = scpi_process("SYST:ERR", b, sizeof b); ok &= (nn > 0 && b[0] == '-'); }  /* dotaz bez '?' */
    /* Ve frontě je teď 5× -113 (FOO,*ID,?,TEMPXY,SYST:ERR). Popni první + prázdno na konci. */
    scpi_process("SYST:ERR?", b, sizeof b);        ok &= (strncmp(b, "-113,", 5) == 0);     /* pop = -113 */
    scpi_err_clear();
    scpi_process("SYST:ERR?", b, sizeof b);        ok &= (strncmp(b, "0,", 2) == 0);        /* po *CLS/clear prázdno */

    /* Parsování argumentů (bez libc). */
    { int q; ok &= (scpi_num("1000000", &q) > 999999.0 && scpi_num("1000000", &q) < 1000001.0 && q); }
    { int q; double v = scpi_num("1e6", &q);   ok &= (q && v > 999999.0 && v < 1000001.0); }
    { int q; double v = scpi_num("-2.5", &q);  ok &= (q && v < -2.499 && v > -2.501); }
    { int q; double v = scpi_num("1.5E-3", &q); ok &= (q && v > 0.00149 && v < 0.00151); }
    { int q; scpi_num("abc", &q);              ok &= (q == 0); }                 /* bez číslice */
    { int q; ok &= (scpi_bool("ON", &q) == 1 && q); }
    { int q; ok &= (scpi_bool("off", &q) == 0 && q); }
    { int q; ok &= (scpi_bool("1", &q) == 1 && q); }
    { int q; scpi_bool("maybe", &q);           ok &= (q == 0); }                 /* neznámý */

    /* CALC setter nad LOKÁLNÍ cfg (nešahá na reálný g_meas_cfg). */
    {
        meas_cfg_t c; meas_math_defaults(&c);
        int err = 1;
        ok &= (scpi_calc_set(&c, "CALCulate:MATH:M", "2", &err) == 1 && err == 0 && c.m > 1.99 && c.m < 2.01);
        ok &= (scpi_calc_set(&c, "CALC:MATH:B", "100", &err) == 1 && err == 0 && c.b > 99.9 && c.b < 100.1);
        ok &= (scpi_calc_set(&c, "CALC:MATH:STAT", "ON", &err) == 1 && err == 0 && c.math_en == 1);
        ok &= (scpi_calc_set(&c, "CALC:LIM:LOW", "9.9e6", &err) == 1 && err == 0 && c.lo > 9.8e6 && c.lo < 10.0e6);
        ok &= (scpi_calc_set(&c, "CALC:LIM:UPP", "10.1e6", &err) == 1 && err == 0 && c.hi > 10.0e6 && c.hi < 10.2e6);
        ok &= (scpi_calc_set(&c, "CALC:LIM:STAT", "1", &err) == 1 && c.limit_en == 1);
        scpi_calc_set(&c, "CALC:MATH:M", "xyz", &err); ok &= (err == 1);          /* chybný arg -> err */
        ok &= (scpi_calc_set(&c, "CALC:BOGUS:X", "1", &err) == 0);                /* neznámý header */
        /* Math+limit vektor: Y = 2*X+100; X=10e6 -> 20,000,100 = nad hi -> FAIL HI. */
        double y = meas_math_apply(&c, 10000000.0);
        ok &= (y > 20000099.0 && y < 20000101.0);
        ok &= (meas_limit_eval(&c, y) == MEAS_HI);
    }

    scpi_err_clear();   /* neponech chyby ze selftestu v reálné frontě */
    return ok;
}
