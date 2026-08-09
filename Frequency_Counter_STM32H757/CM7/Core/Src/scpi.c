/**
 * @file    scpi.c
 * @brief   SCPI-99 parser + dispatch (#25). Transportně nezávislý; viz scpi.h.
 */
#include "scpi.h"
#include "version.h"           /* FW_VERSION_FULL — *IDN? */
#include "fpga_freq.h"         /* fpga_freq_get_last — REÁLNÝ kmitočet (ne sim) */
#include "gps.h"               /* gps_get — SYST:GPS:STAT? */
#include "sensor_stat.h"       /* g_sensors[] — SYST:TEMP?, MEAS:VOLT?/POW? */
#include "calib.h"             /* g_calib — AD8307 dBm (MEAS:POWer? RF) */
#include "freertos_shared.h"   /* g_spi_ok, g_si5356_status/_ok, g_selftest_res, g_uptime_s */
#include "meas_math.h"         /* g_meas_cfg — CALCulate subsystem */
#include "datalog.h"           /* datalog_get_status/read_back — MMEMory subsystem */
#include "FreeRTOS.h"          /* taskENTER_CRITICAL — atomický commit g_meas_cfg */
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>   /* atoi — selftest kontroly status registrů */

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

/* nanosekundy -> "d.dddddd" sekundy (6 des. míst; pro SENS:FREQ:GATE?). */
static void fmt_scpi_sec_ns(uint64_t ns, char *out, size_t n)
{
    uint32_t whole = (uint32_t)(ns / 1000000000u);         /* celé sekundy */
    uint32_t us    = (uint32_t)((ns % 1000000000u) / 1000u); /* mikrosekundy = 6 des. míst */
    snprintf(out, n, "%lu.%06lu", (unsigned long)whole, (unsigned long)us);
}

/* stupně -> "±d.dddddd" (6 des. míst; pro SYST:GPS:POS?). */
static void fmt_scpi_deg6(float v, char *out, size_t n)
{
    int neg = (v < 0.0f); if (neg) v = -v;
    int32_t ud = (int32_t)(v * 1000000.0f + 0.5f);   /* mikro-stupně */
    snprintf(out, n, "%s%ld.%06ld", neg ? "-" : "", (long)(ud / 1000000), (long)(ud % 1000000));
}

/* ── Chybová fronta + status registry (SCPI-99 SYSTem:ERRor? + IEEE 488.2) ─────
 * PER-SESSION: veškerý stav je ve `scpi_ctx_t` (scpi.h). Každý transport má svůj
 * kontext → nezávislé fronty i status (USB CDC teď, souběžný TCP 5025 na CM4 pak).
 * `scpi_process` bez ctx používá jediný sdílený default (USB konzole).
 * ESR = Standard Event Status Register (latched, *ESR? čte a maže); ESE/SRE enable. */
static scpi_ctx_t s_default_ctx;   /* zero-init při načtení = čistý stav (fronta prázdná) */

void scpi_ctx_init(scpi_ctx_t *ctx)
{
    if (ctx) memset(ctx, 0, sizeof *ctx);
}

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
static void scpi_err_push(scpi_ctx_t *c, int code)
{
    /* Latch do ESR: −1xx = Command Error (bit5), −2xx = Execution Error (bit4). */
    if (code <= -100 && code > -200) c->esr |= 0x20u;
    else if (code <= -200 && code > -300) c->esr |= 0x10u;
    if (c->err_count >= SCPI_ERRQ_N) {             /* plná fronta -> overflow marker */
        c->err_q[(c->err_head + SCPI_ERRQ_N - 1) % SCPI_ERRQ_N] = -350;
        return;
    }
    c->err_q[(c->err_head + c->err_count) % SCPI_ERRQ_N] = code;
    c->err_count++;
}
static int scpi_err_pop(scpi_ctx_t *c)              /* 0 = prázdná (No error) */
{
    if (c->err_count == 0) return 0;
    int code = c->err_q[c->err_head];
    c->err_head = (uint8_t)((c->err_head + 1) % SCPI_ERRQ_N);
    c->err_count--;
    return code;
}
static void scpi_err_clear(scpi_ctx_t *c) { c->err_head = 0; c->err_count = 0; }

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
 * Číslo: znaménko, celá/desetinná část, exponent (e/E), volitelná Hz jednotka. */
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
    /* Volitelná frekvenční jednotka (case-insensitive, i s mezerou): k/M/G × Hz.
     * Např. "100KHZ"->1e5, "10.1MHZ"->1.01e7, "1GHZ"->1e9. Bez jednotky (nebo "HZ")
     * = ×1. Milli/mikro (m/u) se ZÁMĚRNĚ nerozlišují — přístroj má jen Hz parametry,
     * takže „M" je vždy mega (kdyby přibyl čas/gate, doplní se tabulka jednotek). */
    while (*s == ' ' || *s == '\t') s++;
    switch (up(*s)) {
        case 'K': v *= 1e3; break;
        case 'M': v *= 1e6; break;
        case 'G': v *= 1e9; break;
        default:  break;                 /* 'H'(Hz) / nic / neznámé → ×1 */
    }
    if (ok) *ok = (digits > 0);
    return v * sign;
}

/* Case-insensitive shoda vedoucího tokenu argumentu (do mezery/konce) s klíčem
 * (klíč VELKÝMI). Pro selektory dotazů typu SYST:TEMP? OCXO|BOARD|MCU|FPGA. */
static int kw_ci(const char *arg, const char *key)
{
    while (*arg == ' ' || *arg == '\t') arg++;
    int i = 0;
    while (key[i]) { if (up(arg[i]) != up(key[i])) return 0; i++; }
    return (arg[i] == '\0' || arg[i] == ' ' || arg[i] == '\t');
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

/* Zpracuje JEDNU programovou jednotku (bez ';'). Compound rozdělí scpi_process.
 * Stav (chyby/status) je v `c` = kontext session. */
static size_t scpi_exec_one(scpi_ctx_t *c, const char *line, char *out, size_t out_sz)
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
    if (hdr_match(hdr, "*OPC")) {                             /* Operation Complete */
        if (is_query) { snprintf(out, out_sz, "1"); return strlen(out); }
        c->esr |= 0x01u;                                       /* synchronní → OPC hned */
        return 0;
    }
    if (hdr_match(hdr, "*WAI") && !is_query) { return 0; }    /* no-op (vše synchronní) */
    if (hdr_match(hdr, "*TST") && is_query) {                 /* self-test dotaz: 0=PASS */
        snprintf(out, out_sz, "%d", (g_selftest_res == 1) ? 0 : 1);
        return strlen(out);
    }
    if (hdr_match(hdr, "*RST") && !is_query) { return 0; }    /* no-op (nesahá na HW) */
    if (hdr_match(hdr, "*CLS") && !is_query) { scpi_err_clear(c); c->esr = 0; return 0; }  /* clear status */
    if (hdr_match(hdr, "*ESR")) {                             /* Event Status Register */
        if (is_query) { snprintf(out, out_sz, "%u", (unsigned)c->esr); c->esr = 0; return strlen(out); }
    }
    if (hdr_match(hdr, "*ESE")) {                             /* Event Status Enable */
        if (is_query) { snprintf(out, out_sz, "%u", (unsigned)c->ese); return strlen(out); }
        int ok = 0; double v = scpi_num(arg, &ok);
        if (ok) { c->ese = (uint8_t)v; return 0; }
        scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out);
    }
    if (hdr_match(hdr, "*SRE")) {                             /* Service Request Enable */
        if (is_query) { snprintf(out, out_sz, "%u", (unsigned)c->sre); return strlen(out); }
        int ok = 0; double v = scpi_num(arg, &ok);
        if (ok) { c->sre = (uint8_t)v; return 0; }
        scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out);
    }
    if (hdr_match(hdr, "*STB") && is_query) {                 /* Status Byte */
        unsigned stb = 0;
        if (c->err_count > 0)      stb |= 0x04u;              /* EAV: fronta chyb neprázdná */
        if (c->esr & c->ese)        stb |= 0x20u;              /* ESB: povolený ESR bit */
        if (stb & c->sre)          stb |= 0x40u;              /* MSS: povolený summary bit */
        snprintf(out, out_sz, "%u", stb); return strlen(out);
    }

    /* ── SYSTem ────────────────────────────────────────────────────────────── */
    if (hdr_match(hdr, "SYSTem:VERSion") && is_query) { snprintf(out, out_sz, "1999.0"); return strlen(out); }
    if (hdr_match(hdr, "SYSTem:UPTime") && is_query) {       /* doba běhu [s] */
        snprintf(out, out_sz, "%lu", (unsigned long)g_uptime_s);
        return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:ERRor:COUNt") && is_query) {   /* počet chyb ve frontě */
        snprintf(out, out_sz, "%u", (unsigned)c->err_count);
        return strlen(out);
    }
    if ((hdr_match(hdr, "SYSTem:ERRor") ||                    /* SCPI-99 + IEEE 488.2 alias */
         hdr_match(hdr, "SYSTem:ERRor:NEXT")) && is_query) {
        int code = scpi_err_pop(c);
        snprintf(out, out_sz, "%d,\"%s\"", code, scpi_err_msg(code));
        return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:TEMPerature") && is_query) {   /* [°C], volitelně čidlo */
        sensor_id_t id = SENS_T49;                            /* default = OCXO (0x49) */
        if      (kw_ci(arg, "BOARD") || kw_ci(arg, "STM")) id = SENS_T48;   /* deska */
        else if (kw_ci(arg, "MCU"))                        id = SENS_CORE_T; /* jádro */
        else if (kw_ci(arg, "FPGA"))                       id = SENS_T4A;    /* FPGA (neosazen) */
        const sensor_stat_t *s = &g_sensors[id];
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

    /* ── MEASure / FETCh ────────────────────────────────────────────────────────
     * ⚠️ REÁLNÝ FPGA kmitočet (ne simulovaný headline). Bez platného měření -> NaN. */
    if (hdr_match(hdr, "MEASure:FREQuency:ALL") && is_query) {    /* obě větve: f4,f16 */
        fpga_meas_t m; char f4[24], f16[24];
        if (fpga_freq_get_last(&m) && (m.measurement_status & 0x01u)
            && !(m.error_flags & FPGA_ERR_SIGNAL_LOST)) {
            fmt_scpi_hz(m.frequency_x100000, f4, sizeof f4);
            if (!(m.status2 & FPGA_ST2_DIV16_ERR)) fmt_scpi_hz(m.freq16_x100000, f16, sizeof f16);
            else                                   snprintf(f16, sizeof f16, "9.91E37");
        } else { snprintf(f4, sizeof f4, "9.91E37"); snprintf(f16, sizeof f16, "9.91E37"); }
        snprintf(out, out_sz, "%s,%s", f4, f16);
        return strlen(out);
    }
    if (hdr_match(hdr, "MEASure:FREQuency:DIV16") && is_query) {  /* pin27 /16 větev */
        fpga_meas_t m;
        int ok = fpga_freq_get_last(&m) && (m.measurement_status & 0x01u)
                 && !(m.error_flags & FPGA_ERR_SIGNAL_LOST)
                 && !(m.status2 & FPGA_ST2_DIV16_ERR);
        if (ok) fmt_scpi_hz(m.freq16_x100000, out, out_sz);
        else    snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if ((hdr_match(hdr, "MEASure:FREQuency") ||              /* /4 primár (=FETCh:FREQuency?) */
         hdr_match(hdr, "FETCh:FREQuency")) && is_query) {
        fpga_meas_t m;
        int ok = fpga_freq_get_last(&m) && (m.measurement_status & 0x01u)
                 && !(m.error_flags & FPGA_ERR_SIGNAL_LOST);
        if (ok) fmt_scpi_hz(m.frequency_x100000, out, out_sz);
        else    snprintf(out, out_sz, "9.91E37");             /* SCPI not-a-number */
        return strlen(out);
    }

    /* Napětí napájecích větví (REÁLNÁ HW data z ADS1115/ADC3, ve voltech).
     * P12=12V (AIN2), P5=5V (AIN3), VC=OCXO ladicí (AIN0), VREF (~2,5 V), VBAT.
     * g_sensors drží mV (12V/5V už po gain z kalibrace) → /1000 = V. */
    if (hdr_match(hdr, "MEASure:VOLTage") && is_query) {
        sensor_id_t id = SENS_ADS2;                          /* default = 12V větev */
        if      (kw_ci(arg, "P5")  || kw_ci(arg, "5V"))   id = SENS_ADS3;
        else if (kw_ci(arg, "VC")  || kw_ci(arg, "OCXO")) id = SENS_ADS0;
        else if (kw_ci(arg, "VREF"))                      id = SENS_VDDA;
        else if (kw_ci(arg, "VBAT") || kw_ci(arg, "BAT")) id = SENS_VBAT;
        const sensor_stat_t *s = &g_sensors[id];
        if (s->valid) fmt_scpi_f2(s->last / 1000.0f, out, out_sz);   /* mV → V */
        else          snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    /* RF vstupní výkon [dBm] z AD8307 log-detektoru (AIN1) přes živou kalibraci. */
    if (hdr_match(hdr, "MEASure:POWer") && is_query) {
        const sensor_stat_t *s = &g_sensors[SENS_ADS1];
        float slope = g_calib.ad8307_slope_mv_db;
        if (s->valid && slope > 1.0f)
            fmt_scpi_f2(s->last / slope + g_calib.ad8307_intercept_dbm, out, out_sz);
        else
            snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }

    /* ── SENSe (parametry měření z FPGA — REÁLNÁ data z posledního rámce) ─────── */
    if (hdr_match(hdr, "SENSe:FREQuency:GATE") && is_query) {  /* hradlovací okno [s] */
        fpga_meas_t m;
        if (fpga_freq_get_last(&m) && m.gate_time_ns) fmt_scpi_sec_ns(m.gate_time_ns, out, out_sz);
        else                                          snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if (hdr_match(hdr, "SENSe:FREQuency:CHANnel") && is_query) { /* aktivní kanál (id) */
        fpga_meas_t m;
        if (fpga_freq_get_last(&m)) snprintf(out, out_sz, "%u", (unsigned)m.channel_id);
        else                        snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }

    /* ── MMEMory (datalog W25Q/SD — REÁLNÁ logovaná data, ne simulace) ───────────
     * Kruhový log stability (32 B/10 s). freq je REÁLNÝ z FPGA (0 dokud #2), teploty/
     * Vc/GPS reálné. CATalog? = souhrn, DATA:COUNt? = počet, DATA? <n> = n-tý od konce. */
    if (hdr_match(hdr, "MMEMory:DATA:COUNt") && is_query) {   /* počet záznamů */
        datalog_status_t st; datalog_get_status(&st);         /* jen cache, bez flash I/O */
        snprintf(out, out_sz, "%lu", (unsigned long)st.records);
        return strlen(out);
    }
    if (hdr_match(hdr, "MMEMory:CATalog") && is_query) {      /* souhrn logu */
        datalog_status_t st; datalog_get_status(&st);
        snprintf(out, out_sz, "\"%s\",%lu,%lu,%lu,%lu,%u",
                 st.backend ? st.backend : "--",
                 (unsigned long)st.records, (unsigned long)st.capacity_rec,
                 (unsigned long)st.last_seq, (unsigned long)st.write_errors,
                 (unsigned)(st.wrapped ? 1u : 0u));
        return strlen(out);
    }
    if (hdr_match(hdr, "MMEMory:DATA") && is_query) {         /* n-tý záznam od nejnovějšího */
        int nok = 0; double nn = scpi_num(arg, &nok);
        if (!nok || nn < 0.0) {
            scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out);
        }
        datalog_rec_t r;                                      /* ⚠️ čte W25Q pod QSPI mutexem (blokující ~ms) */
        if (!datalog_read_back((uint32_t)nn, &r)) {
            scpi_err_push(c, -222); snprintf(out, out_sz, "-222,\"Data out of range\""); return strlen(out);
        }
        /* CSV: seq,t_unix,freq[Hz],t_ocxo[C],t_board[C],vc[mV],rf[dBm],flags,sats,hdop
         * (neplatné 16b hodnoty → NaN 9.91E37; HDOP 255=n/a → NaN). */
        char fq[24], to[12], tb[12], rf[12], hd[12];
        fmt_scpi_hz(r.freq_x100000, fq, sizeof fq);
        if (r.t_ocxo_c100 == DATALOG_INVALID16)  snprintf(to, sizeof to, "9.91E37"); else fmt_scpi_f2(r.t_ocxo_c100  / 100.0f, to, sizeof to);
        if (r.t_board_c100 == DATALOG_INVALID16) snprintf(tb, sizeof tb, "9.91E37"); else fmt_scpi_f2(r.t_board_c100 / 100.0f, tb, sizeof tb);
        if (r.rf_dbm10 == DATALOG_INVALID16)     snprintf(rf, sizeof rf, "9.91E37"); else fmt_scpi_f2(r.rf_dbm10 / 10.0f, rf, sizeof rf);
        if (r.hdop10 == 255u)                    snprintf(hd, sizeof hd, "9.91E37"); else fmt_scpi_f2(r.hdop10 / 10.0f, hd, sizeof hd);
        snprintf(out, out_sz, "%lu,%lu,%s,%s,%s,%d,%s,%u,%u,%s",
                 (unsigned long)r.seq, (unsigned long)r.t_unix, fq, to, tb,
                 (int)r.ocxo_vc_mv, rf, (unsigned)r.flags, (unsigned)r.sats, hd);
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
        meas_cfg_t mc; taskENTER_CRITICAL(); mc = g_meas_cfg; taskEXIT_CRITICAL();
        if      (hdr_match(hdr, "CALCulate:MATH:STATe"))  snprintf(out, out_sz, "%u", (unsigned)mc.math_en);
        else if (hdr_match(hdr, "CALCulate:MATH:M"))      fmt_scpi_hz_d(mc.m,  out, out_sz);
        else if (hdr_match(hdr, "CALCulate:MATH:B"))      fmt_scpi_hz_d(mc.b,  out, out_sz);
        else if (hdr_match(hdr, "CALCulate:NULL:STATe"))  snprintf(out, out_sz, "%u", (unsigned)mc.null_en);
        else if (hdr_match(hdr, "CALCulate:LIMit:STATe")) snprintf(out, out_sz, "%u", (unsigned)mc.limit_en);
        else if (hdr_match(hdr, "CALCulate:LIMit:LOWer")) fmt_scpi_hz_d(mc.lo, out, out_sz);
        else                                             fmt_scpi_hz_d(mc.hi, out, out_sz);
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
    /* Questionable = co dělá měření PODEZŘELÝM (inverzní logika k OPER:COND).
     * bit0 kmitočet (link/měření), bit1 čas (GPS fix), bit2 reference (Si5356). */
    if (hdr_match(hdr, "STATus:QUEStionable:CONDition") && is_query) {
        gps_data_t g; gps_get(&g);
        fpga_meas_t m; unsigned w = 0;
        int fresh = fpga_freq_get_last(&m);
        if (!g_spi_ok || !fresh || !(m.measurement_status & 0x01u)
            || (m.error_flags & (FPGA_ERR_SIGNAL_LOST | FPGA_ERR_MEAS)))  w |= 1u << 0;
        if (!(g.valid && g.fix_mode >= 2))                                w |= 1u << 1;
        if (!g_si5356_ok || (g_si5356_status & ((1u << 3) | (1u << 4))))  w |= 1u << 2;
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
            scpi_err_push(c, -230);
            snprintf(out, out_sz, "-230,\"Data corrupt or stale\"");
            return strlen(out);
        }
        meas_cfg_t tmp; taskENTER_CRITICAL(); tmp = g_meas_cfg; taskEXIT_CRITICAL();
        int err = 0;
        if (scpi_calc_set(&tmp, hdr, arg, &err)) {
            if (err) { scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out); }
            taskENTER_CRITICAL(); g_meas_cfg = tmp; taskEXIT_CRITICAL();
            return 0;   /* akce OK → ticho */
        }
    }

    /* Neznámý příkaz — do chybové fronty (SYST:ERR?) + inline odpověď pro konzoli. */
    scpi_err_push(c, -113);
    snprintf(out, out_sz, "-113,\"Undefined header\"");
    return strlen(out);
}

size_t scpi_process_ctx(scpi_ctx_t *ctx, const char *line, char *out, size_t out_sz)
{
    if (ctx == NULL || line == NULL || out == NULL || out_sz == 0) return 0;
    out[0] = '\0';

    /* Rychlá cesta: bez ';' = jedna jednotka → byte-identické s dřívějškem. */
    if (strchr(line, ';') == NULL) return scpi_exec_one(ctx, line, out, out_sz);

    /* Složená zpráva (IEEE 488.2): jednotky oddělené ';', odpovědi dotazů spojené
     * ';'. Akce (bez výstupu) nepřidávají oddělovač. ⚠️ Zjednodušení: BEZ SCPI
     * path-compression (každá jednotka = plná hlavička) a bez detekce ';' uvnitř
     * řetězcových argumentů (žádný takový příkaz zatím nemáme). Typické užití =
     * dávka common-commandů, např. "*CLS;*ESE 32;*SRE 8;*ESR?". */
    size_t total = 0;
    char sub[56];
    while (*line) {
        int k = 0;
        while (*line && *line != ';' && k < (int)sizeof(sub) - 1) sub[k++] = *line++;
        sub[k] = '\0';
        while (*line && *line != ';') line++;   /* zahoď případný přetečený zbytek jednotky */
        if (*line == ';') line++;

        const char *t = sub; while (*t == ' ' || *t == '\t') t++;
        if (*t == '\0') continue;               /* prázdná jednotka (např. koncový ';') */

        char rb[64];
        size_t rn = scpi_exec_one(ctx, sub, rb, sizeof rb);
        if (rn == 0) continue;                  /* akce → žádná odpověď, žádný oddělovač */
        if (total && total + 1 < out_sz) out[total++] = ';';
        size_t room = (total + 1 < out_sz) ? out_sz - total - 1 : 0;
        size_t cpy  = (rn < room) ? rn : room;
        memcpy(out + total, rb, cpy);
        total += cpy;
        out[total] = '\0';
        if (cpy < rn) break;                    /* došel výstupní buffer → utni */
    }
    return total;
}

/* Bez ctx = sdílený default kontext (USB CDC konzole; jediná session). */
size_t scpi_process(const char *line, char *out, size_t out_sz)
{
    return scpi_process_ctx(&s_default_ctx, line, out, out_sz);
}

/* ── Selftest (parser: case, krátká/dlouhá forma, hierarchie, status, compound) ── */
int scpi_selftest(void)
{
    int ok = 1; char b[80];
    scpi_ctx_t x; scpi_ctx_init(&x);   /* lokální session (selftest běží i za provozu) */

    scpi_process_ctx(&x, "*IDN?", b, sizeof b);            ok &= (strncmp(b, "OK2HAZ,", 7) == 0);
    scpi_process_ctx(&x, "*idn?", b, sizeof b);            ok &= (strncmp(b, "OK2HAZ,", 7) == 0);  /* case */
    scpi_process_ctx(&x, "*OPC?", b, sizeof b);            ok &= (strcmp(b, "1") == 0);
    ok &= (scpi_process_ctx(&x, "*RST", b, sizeof b) == 0);                                        /* akce = 0 */

    scpi_process_ctx(&x, "SYST:ERR?", b, sizeof b);        ok &= (strncmp(b, "0,", 2) == 0);        /* prázdná fronta */
    scpi_process_ctx(&x, "SYSTem:ERRor?", b, sizeof b);    ok &= (strncmp(b, "0,", 2) == 0);        /* dlouhá forma */
    scpi_process_ctx(&x, "system:version?", b, sizeof b);  ok &= (strcmp(b, "1999.0") == 0);        /* case+dlouhá */

    /* Nové dotazy — jen že odpoví (bez HW/fixu vrací NaN 9.91E37). */
    ok &= (scpi_process_ctx(&x, "MEAS:FREQ?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, "MEAS:FREQ:DIV16?", b, sizeof b) > 0);   /* /16 větev */
    ok &= (scpi_process_ctx(&x, "SYST:GPS:TIME?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, "SYST:GPS:POS?", b, sizeof b) > 0);      /* krátká forma POSition */
    ok &= (scpi_process_ctx(&x, "CALC:DATA?", b, sizeof b) > 0);         /* Math Y=m*X+b */
    ok &= (scpi_process_ctx(&x, "CALC:LIM:FAIL?", b, sizeof b) > 0);     /* limit pass/fail */
    ok &= (scpi_process_ctx(&x, "SYST:UPT?", b, sizeof b) > 0);          /* uptime [s] */
    ok &= (scpi_process_ctx(&x, "MEAS:VOLT? P12", b, sizeof b) > 0);     /* 12V větev [V] */
    ok &= (scpi_process_ctx(&x, "MEAS:VOLT? P5",  b, sizeof b) > 0);     /* 5V větev */
    ok &= (scpi_process_ctx(&x, "MEAS:VOLT?",     b, sizeof b) > 0);     /* default = 12V */
    ok &= (scpi_process_ctx(&x, "MEAS:POW?",      b, sizeof b) > 0);     /* RF dBm (AD8307) */
    ok &= (scpi_process_ctx(&x, "SENS:FREQ:GATE?", b, sizeof b) > 0);    /* hradlo [s] */
    ok &= (scpi_process_ctx(&x, "SENS:FREQ:CHAN?", b, sizeof b) > 0);    /* kanál */
    ok &= (scpi_process_ctx(&x, "MMEM:CAT?", b, sizeof b) > 0);          /* datalog souhrn (cache) */
    ok &= (scpi_process_ctx(&x, "MMEM:DATA:COUN?", b, sizeof b) > 0);    /* počet záznamů */

    /* Chybová fronta: neznámý příkaz -> -113 inline i do fronty; SYST:ERR? popne. */
    scpi_ctx_init(&x);
    { size_t nn = scpi_process_ctx(&x, "FOO:BAR?", b, sizeof b);          ok &= (nn > 0 && b[0] == '-'); }
    scpi_process_ctx(&x, "*ID?", b, sizeof b);             ok &= (b[0] == '-');   /* zkracenina NESMI matchnout *IDN */
    scpi_process_ctx(&x, "?", b, sizeof b);                ok &= (b[0] == '-');   /* prázdná hlavička != *IDN */
    scpi_process_ctx(&x, "SYST:TEMPXY?", b, sizeof b);     ok &= (b[0] == '-');   /* delší než plná forma neprojde */
    { size_t nn = scpi_process_ctx(&x, "SYST:ERR", b, sizeof b); ok &= (nn > 0 && b[0] == '-'); }  /* dotaz bez '?' */
    scpi_process_ctx(&x, "SYST:ERR?", b, sizeof b);        ok &= (strncmp(b, "-113,", 5) == 0);     /* pop = -113 */
    scpi_ctx_init(&x);
    scpi_process_ctx(&x, "SYST:ERR?", b, sizeof b);        ok &= (strncmp(b, "0,", 2) == 0);        /* po clear prázdno */

    /* Izolace session: chyba v jednom kontextu neovlivní druhý (jádro per-session). */
    {
        scpi_ctx_t a, c2; scpi_ctx_init(&a); scpi_ctx_init(&c2);
        scpi_process_ctx(&a, "BOGUS?", b, sizeof b);                     /* -113 do fronty a */
        scpi_process_ctx(&c2, "SYST:ERR:COUN?", b, sizeof b); ok &= (b[0] == '0');  /* c2 čistý */
        scpi_process_ctx(&a,  "SYST:ERR:COUN?", b, sizeof b); ok &= (b[0] == '1');  /* a má chybu */
    }

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

    /* Frekvenční jednotky v parseru čísel (k/M/G × Hz, "HZ" = ×1). */
    { int q; double v = scpi_num("100KHZ", &q);  ok &= (q && v > 99999.0 && v < 100001.0); }
    { int q; double v = scpi_num("10.1MHZ", &q); ok &= (q && v > 10.09e6 && v < 10.11e6); }
    { int q; double v = scpi_num("1GHZ", &q);    ok &= (q && v > 0.999e9 && v < 1.001e9); }
    { int q; double v = scpi_num("2M", &q);      ok &= (q && v > 1.999e6 && v < 2.001e6); }
    { int q; double v = scpi_num("50HZ", &q);    ok &= (q && v > 49.9 && v < 50.1); }

    /* CALC setter nad LOKÁLNÍ cfg (nešahá na reálný g_meas_cfg). */
    {
        meas_cfg_t c; meas_math_defaults(&c);
        int err = 1;
        ok &= (scpi_calc_set(&c, "CALCulate:MATH:M", "2", &err) == 1 && err == 0 && c.m > 1.99 && c.m < 2.01);
        ok &= (scpi_calc_set(&c, "CALC:MATH:B", "100", &err) == 1 && err == 0 && c.b > 99.9 && c.b < 100.1);
        ok &= (scpi_calc_set(&c, "CALC:MATH:STAT", "ON", &err) == 1 && err == 0 && c.math_en == 1);
        ok &= (scpi_calc_set(&c, "CALC:LIM:LOW", "9.9e6", &err) == 1 && err == 0 && c.lo > 9.8e6 && c.lo < 10.0e6);
        ok &= (scpi_calc_set(&c, "CALC:LIM:UPP", "10.1MHZ", &err) == 1 && err == 0 && c.hi > 10.09e6 && c.hi < 10.11e6);  /* unit i v SET */
        ok &= (scpi_calc_set(&c, "CALC:LIM:STAT", "1", &err) == 1 && c.limit_en == 1);
        scpi_calc_set(&c, "CALC:MATH:M", "xyz", &err); ok &= (err == 1);          /* chybný arg -> err */
        ok &= (scpi_calc_set(&c, "CALC:BOGUS:X", "1", &err) == 0);                /* neznámý header */
        double y = meas_math_apply(&c, 10000000.0);                              /* Y=2*X+100; X=10e6 -> nad hi */
        ok &= (y > 20000099.0 && y < 20000101.0);
        ok &= (meas_limit_eval(&c, y) == MEAS_HI);
    }

    /* IEEE 488.2 status model + ERR:COUN? + ERR:NEXT? + MEAS:FREQ:ALL?. */
    scpi_ctx_init(&x);
    scpi_process_ctx(&x, "FOO?", b, sizeof b);                                   /* -113 -> CME */
    scpi_process_ctx(&x, "SYST:ERR:COUN?", b, sizeof b); ok &= (b[0] == '1');    /* 1 chyba ve frontě */
    scpi_process_ctx(&x, "*STB?", b, sizeof b);          ok &= (atoi(b) & 0x04); /* EAV */
    scpi_process_ctx(&x, "*ESR?", b, sizeof b);          ok &= (atoi(b) & 0x20); /* CME latch */
    scpi_process_ctx(&x, "*ESR?", b, sizeof b);          ok &= (atoi(b) == 0);   /* čtení smazalo */
    scpi_process_ctx(&x, "SYST:ERR:NEXT?", b, sizeof b); ok &= (strncmp(b, "-113,", 5) == 0);  /* NEXT alias popne */
    scpi_process_ctx(&x, "*ESE 32", b, sizeof b);
    scpi_process_ctx(&x, "*ESE?", b, sizeof b);          ok &= (atoi(b) == 32);
    scpi_process_ctx(&x, "CALC:MATH:M xyz", b, sizeof b);                        /* -224 -> EXE */
    scpi_process_ctx(&x, "*ESR?", b, sizeof b);          ok &= (atoi(b) & 0x10); /* EXE latch */
    scpi_process_ctx(&x, "*CLS", b, sizeof b);
    scpi_process_ctx(&x, "*ESR?", b, sizeof b);          ok &= (atoi(b) == 0);   /* *CLS smazal ESR */
    scpi_process_ctx(&x, "SYST:ERR:COUN?", b, sizeof b); ok &= (b[0] == '0');    /* *CLS smazal frontu */
    ok &= (scpi_process_ctx(&x, "MEAS:FREQ:ALL?", b, sizeof b) > 0 && strchr(b, ',') != NULL);

    /* Common commands: *OPC (event -> ESR bit0), *WAI (no-op), *TST? (0/1). */
    scpi_process_ctx(&x, "*CLS", b, sizeof b);
    scpi_process_ctx(&x, "*OPC", b, sizeof b);
    scpi_process_ctx(&x, "*ESR?", b, sizeof b);          ok &= (atoi(b) & 0x01);  /* OPC latch */
    ok &= (scpi_process_ctx(&x, "*WAI", b, sizeof b) == 0);                       /* akce = 0 */
    ok &= (scpi_process_ctx(&x, "*TST?", b, sizeof b) > 0);                       /* 0=PASS / 1 */

    /* Selektor čidla + FETCh alias + questionable (bez HW jen že odpoví). */
    ok &= (scpi_process_ctx(&x, "SYST:TEMP? BOARD", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, "SYST:TEMP? MCU",   b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, "FETC:FREQ?",       b, sizeof b) > 0);            /* FETCh alias */
    ok &= (scpi_process_ctx(&x, "STAT:QUES:COND?",  b, sizeof b) > 0);

    /* Compound (IEEE 488.2 ';'): jednotky se provedou popořadě, dotazy spojí ';',
     * akce oddělovač nepřidá. */
    scpi_ctx_init(&x);
    scpi_process_ctx(&x, "*ESE 24;*ESE?", b, sizeof b);  ok &= (strcmp(b, "24") == 0);    /* obě jednotky */
    scpi_process_ctx(&x, "*ESE?;*ESE?",   b, sizeof b);  ok &= (strcmp(b, "24;24") == 0); /* ';' join */
    scpi_process_ctx(&x, "*ESE 8;*ESE?",  b, sizeof b);  ok &= (strcmp(b, "8") == 0);     /* akce bez ';' */
    return ok;
}
