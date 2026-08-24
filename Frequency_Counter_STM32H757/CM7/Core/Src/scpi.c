/**
 * @file    scpi.c
 * @brief   SCPI-99 parser + dispatch (#25). Transportně I DATA-SOURCE nezávislý:
 *          handlery čtou z `scpi_src_t` (viz scpi.h). Backend (CM7 globály / CM4
 *          IPC snapshot) ho naplní. Zde je jádro + CM7 backend (`#if CORE_CM7`).
 */
#include "scpi.h"              /* scpi_src_t, scpi_ctx_t, SCPI_V_*, SCPI_CFG_*, meas_math/datalog typy */
#include "version.h"          /* FW_VERSION_FULL — *IDN? */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>           /* atoi — selftest kontroly status registrů */

#if defined(CORE_CM7)
#include "fpga_freq.h"        /* fpga_freq_get_last, FPGA_ERR_* — CM7 backend */
#include "gps.h"              /* gps_get */
#include "sensor_stat.h"      /* g_sensors[] */
#include "calib.h"            /* g_calib */
#include "freertos_shared.h"  /* g_spi_ok, g_si5356_*, g_selftest_res, g_uptime_s */
#include "datalog.h"          /* datalog_get_status/read_back */
#include "FreeRTOS.h"         /* taskENTER_CRITICAL — atomický snímek g_meas_cfg */
#include "task.h"
#endif

/* ── Keyword matcher (SCPI krátká/dlouhá forma, case-insensitive) ────────────
 * pat = keyword s VELKÝMI (povinné) + malými (volitelné) písmeny, např. "MEASure". */
static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static int kw_match(const char *in, int in_len, const char *pat, int pat_len)
{
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

/* ── Formátování bez float printf (nano newlib) ─────────────────────────────── */
/* uint64 Hz*1e5 -> "12345678.90123" (Hz < 4.29e9 -> uint32). */
static void fmt_scpi_hz(uint64_t x100000, char *out, size_t n)
{
    uint32_t whole = (uint32_t)(x100000 / 100000u);
    uint32_t frac  = (uint32_t)(x100000 % 100000u);
    snprintf(out, n, "%lu.%05lu", (unsigned long)whole, (unsigned long)frac);
}
/* float -> "±d.dd" (2 des. místa). */
static void fmt_scpi_f2(float v, char *out, size_t n)
{
    int neg = (v < 0.0f); if (neg) v = -v;
    int t = (int)(v * 100.0f + 0.5f);
    snprintf(out, n, "%s%d.%02d", neg ? "-" : "", t / 100, t % 100);
}
/* double Hz -> "±d.ddddd" (5 des. míst; CALC:DATA?/readbacky).
 * ⚠️ Vystaveno (ne static) — sdílí ji `httpd_min.c` (W4) pro JSON čísla, aby
 * přetečením ošetřená logika (viz komentář uvnitř) nebyla ve dvou kopiích. */
void fmt_scpi_hz_d(double hz, char *out, size_t n)
{
    /* ⚠️ Rozsahová pojistka. Vstup NENÍ jen měřený kmitočet — jde sem i `m`, `b`,
     * `lo`, `hi` a výsledek `m·x+b`, tedy hodnoty, které uživatel nastavuje
     * PŘÍMO příkazem (`CALC:MATH:M 1e30`) a `scpi_num` je bez reptání přečte.
     * Nad ~4,3e9 by tiše přetekl `whole` (uint32), nad ~1,8e14 už je přetypování
     * double na uint64 NEDEFINOVANÉ chování. Mez 4e9 je nad stropem tvarovače
     * (1,4 GHz), takže žádnou platnou hodnotu neodřízne.
     * Zápis přes `!(… && …)` chytá i NaN (porovnání s NaN jsou vždy false). */
    if (!(hz > -4.0e9 && hz < 4.0e9)) { snprintf(out, n, "9.91E37"); return; }
    int neg = (hz < 0.0); if (neg) hz = -hz;
    uint64_t x = (uint64_t)(hz * 100000.0 + 0.5);
    uint32_t whole = (uint32_t)(x / 100000u);
    uint32_t frac  = (uint32_t)(x % 100000u);
    snprintf(out, n, "%s%lu.%05lu", neg ? "-" : "", (unsigned long)whole, (unsigned long)frac);
}
/* Perioda [s] z kmitočtu — 15 des. míst (femtosekundové rozlišení).
 * Proč tolik: při 1,4 GHz (strop tvarovače) je perioda 714 ps, takže i
 * pikosekundový krok by byl 0,14 % — pro čítač nepoužitelné. Formátuje se
 * celočíselnou extrakcí (nano.specs nemá %f). */
static void fmt_scpi_period_s(double hz, char *out, size_t n)
{
    /* ⚠️ Horní mez periody NENÍ kosmetika: `1/hz * 1e15` pro hz < ~5,5e-5
     * přeteče uint64 (max 1,845e19) a přetypování double mimo rozsah je
     * NEDEFINOVANÉ chování, ne jen špatné číslo. Dosažitelné je to — stačí
     * rámec s `freq4_x100000` v jednotkách (10⁻⁵ Hz), třeba po poškození dat.
     * 10 000 s je bezpečně pod mezí (1e19) a zároveň hluboko za tím, co čítač
     * vůbec umí změřit (reciproké okno přeteče už nad ~21,5 s). */
    if (hz <= 0.0 || (1.0 / hz) > 1.0e4) { snprintf(out, n, "9.91E37"); return; }
    uint64_t fs   = (uint64_t)(1.0 / hz * 1e15 + 0.5);    /* perioda ve femtosekundách */
    uint64_t frac = fs % 1000000000000000ull;
    /* ⚠️ Zlomek (15 cifer) se NEVEJDE do `unsigned long` (32 bit na ARM) a
     * newlib-nano neumí `%llu` — proto se tiskne po dvou 32bitových půlkách,
     * stejný důvod, proč se všude jinde castuje na `unsigned long`. */
    snprintf(out, n, "%lu.%07lu%08lu",
             (unsigned long)(fs / 1000000000000000ull),
             (unsigned long)(frac / 100000000ull),        /* horních 7 cifer */
             (unsigned long)(frac % 100000000ull));       /* dolních 8 cifer */
}
/* Rozparsuje tri cela cisla oddelena carkami ("2026,8,15"). @return 1 = OK.
 * Bezstavove — testovatelne bez HW (soucast `scpi_selftest`). */
static int scpi_parse3(const char *s, int *a, int *b, int *cc)
{
    if (!s) return 0;
    int v[3] = {0, 0, 0}, k = 0, seen = 0, neg = 0;
    for (; *s && k < 3; s++) {
        if (*s == '-') { neg = 1; continue; }
        if (*s >= '0' && *s <= '9') { v[k] = v[k] * 10 + (*s - '0'); seen = 1; continue; }
        if (*s == ',') { if (!seen) return 0; if (neg) v[k] = -v[k]; k++; seen = 0; neg = 0; continue; }
        if (*s == ' ') continue;
        return 0;                       /* nepovoleny znak */
    }
    if (!seen || k != 2) return 0;      /* musi byt presne tri cisla */
    if (neg) v[2] = -v[2];
    *a = v[0]; *b = v[1]; *cc = v[2];
    return 1;
}

/* Index brany 0..3 -> sekundy. Tabulka je ZDROJ PRAVDY pro SCPI i pro prevod
 * opacnym smerem (`scpi_gate_idx_from_s`); UI ma tytez hodnoty jako popisky.
 * ⚠️ Vystaveno (ne static) — sdili ji JSON v `httpd_min.c`, aby web neukazoval
 * branu z vlastni kopie tabulky (dve tabulky = dve pravdy, viz `fmt_scpi_hz_d`). */
double scpi_gate_s(uint8_t idx)
{
    static const double G[4] = {0.1, 1.0, 10.0, 100.0};
    return G[idx & 3];
}
/* Sekundy -> index brany. @return 0..3, nebo -1 kdyz hodnota neodpovida presetu.
 * Tolerance 1 % kryje zapis "0.1" i "1E-1". */
int scpi_gate_idx_from_s(double sec)
{
    for (int i = 0; i < 4; i++) {
        double g = scpi_gate_s((uint8_t)i), d = sec - g;
        if (d < 0) d = -d;
        if (d <= g * 0.01) return i;
    }
    return -1;
}
/* double -> "d.dddddd" (6 des. mist, bez %f — newlib-nano). */
static void fmt_scpi_f6(double v, char *out, uint32_t out_sz)
{
    long ip = (long)v;
    long fp = (long)((v - (double)ip) * 1000000.0 + 0.5);
    if (fp < 0) fp = -fp;
    snprintf(out, out_sz, "%ld.%06ld", ip, fp);
}
/* ns -> "d.dddddd" sekundy (6 des. míst; SENS:FREQ:GATE?). */
static void fmt_scpi_sec_ns(uint64_t ns, char *out, size_t n)
{
    uint32_t whole = (uint32_t)(ns / 1000000000u);
    uint32_t us    = (uint32_t)((ns % 1000000000u) / 1000u);
    snprintf(out, n, "%lu.%06lu", (unsigned long)whole, (unsigned long)us);
}
/* stupně -> "±d.dddddd" (6 des. míst; SYST:GPS:POS?). */
static void fmt_scpi_deg6(float v, char *out, size_t n)
{
    int neg = (v < 0.0f); if (neg) v = -v;
    int32_t ud = (int32_t)(v * 1000000.0f + 0.5f);
    snprintf(out, n, "%s%ld.%06ld", neg ? "-" : "", (long)(ud / 1000000), (long)(ud % 1000000));
}

/* ── Chybová fronta + status registry (PER-SESSION, scpi_ctx_t) ─────────────── */
static const char *scpi_err_msg(int code)
{
    switch (code) {
        case 0:    return "No error";
        case -100: return "Command error";
        case -113: return "Undefined header";
        case -222: return "Data out of range";
        case -224: return "Illegal parameter value";
        case -230: return "Data corrupt or stale";
        case -241: return "Hardware missing";
        case -350: return "Queue overflow";
        default:   return "Error";
    }
}
void scpi_ctx_init(scpi_ctx_t *ctx) { if (ctx) memset(ctx, 0, sizeof *ctx); }

static void scpi_err_push(scpi_ctx_t *c, int code)
{
    if (code <= -100 && code > -200) c->esr |= 0x20u;         /* CME (bit5) */
    else if (code <= -200 && code > -300) c->esr |= 0x10u;    /* EXE (bit4) */
    if (c->err_count >= SCPI_ERRQ_N) {
        c->err_q[(c->err_head + SCPI_ERRQ_N - 1) % SCPI_ERRQ_N] = -350;
        return;
    }
    c->err_q[(c->err_head + c->err_count) % SCPI_ERRQ_N] = code;
    c->err_count++;
}
static int scpi_err_pop(scpi_ctx_t *c)
{
    if (c->err_count == 0) return 0;
    int code = c->err_q[c->err_head];
    c->err_head = (uint8_t)((c->err_head + 1) % SCPI_ERRQ_N);
    c->err_count--;
    return code;
}
static void scpi_err_clear(scpi_ctx_t *c) { c->err_head = 0; c->err_count = 0; }

/* ── Parsování argumentů (bez libc, testovatelné) ───────────────────────────── */
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
    while (*s == ' ' || *s == '\t') s++;
    switch (up(*s)) {                       /* k/M/G × Hz (jen Hz doména) */
        case 'K': v *= 1e3; break;
        case 'M': v *= 1e6; break;
        case 'G': v *= 1e9; break;
        default:  break;
    }
    if (ok) *ok = (digits > 0);
    return v * sign;
}
/* Case-insensitive shoda vedoucího tokenu argumentu s klíčem (selektory dotazů). */
static int kw_ci(const char *arg, const char *key)
{
    while (*arg == ' ' || *arg == '\t') arg++;
    int i = 0;
    while (key[i]) { if (up(arg[i]) != up(key[i])) return 0; i++; }
    return (arg[i] == '\0' || arg[i] == ' ' || arg[i] == '\t');
}
/* Bool: ON/OFF/1/0. *ok=0 při neznámém. */
static int scpi_bool(const char *s, int *ok)
{
    while (*s == ' ' || *s == '\t') s++;
    if (ok) *ok = 1;
    if ((up(s[0]) == 'O' && up(s[1]) == 'N') || s[0] == '1') return 1;
    if ((up(s[0]) == 'O' && up(s[1]) == 'F' && up(s[2]) == 'F') || s[0] == '0') return 0;
    if (ok) *ok = 0;
    return 0;
}

/* ── CALC config: apply klíče na cfg (pure) + parse hlavičky na klíč+hodnotu ──── */
/* Aplikuje SCPI_CFG_* klíč. NULL_ACQ potřebuje freq_hz>0. @return 1 = OK. */
int scpi_cfg_apply(meas_cfg_t *c, uint8_t key, uint32_t vu, double vd, double freq_hz)
{
    switch (key) {
        case SCPI_CFG_MATH_EN:  c->math_en  = vu ? 1u : 0u; return 1;
        case SCPI_CFG_MATH_M:   c->m  = vd; return 1;
        case SCPI_CFG_MATH_B:   c->b  = vd; return 1;
        case SCPI_CFG_NULL_EN:  c->null_en  = vu ? 1u : 0u; return 1;
        case SCPI_CFG_NULL_ACQ: if (freq_hz > 0.0) { meas_math_capture_null(c, freq_hz); return 1; } return 0;
        case SCPI_CFG_LIM_EN:   c->limit_en = vu ? 1u : 0u; return 1;
        case SCPI_CFG_LIM_LO:   c->lo = vd; return 1;
        case SCPI_CFG_LIM_HI:   c->hi = vd; return 1;
        default:                return 0;
    }
}
/* Rozpozná CALC SET hlavičku → klíč + hodnota (vu bool / vd double). @return 1 =
 * rozpoznáno. *err=1 při chybném argumentu. Bezstavové (testovatelné). */
static int scpi_calc_parse(const char *hdr, const char *arg, uint8_t *key, uint32_t *vu, double *vd, int *err)
{
    int ok = 0; *err = 0;
    if (hdr_match(hdr, "CALCulate:MATH:STATe"))  { *key = SCPI_CFG_MATH_EN;  *vu = (uint32_t)scpi_bool(arg, &ok); if (!ok) *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:MATH:M"))      { *key = SCPI_CFG_MATH_M;   *vd = scpi_num(arg, &ok);           if (!ok) *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:MATH:B"))      { *key = SCPI_CFG_MATH_B;   *vd = scpi_num(arg, &ok);           if (!ok) *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:NULL:STATe"))  { *key = SCPI_CFG_NULL_EN;  *vu = (uint32_t)scpi_bool(arg, &ok); if (!ok) *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:NULL:ACQuire")){ *key = SCPI_CFG_NULL_ACQ; return 1; }   /* akce, bez arg */
    if (hdr_match(hdr, "CALCulate:LIMit:STATe")) { *key = SCPI_CFG_LIM_EN;   *vu = (uint32_t)scpi_bool(arg, &ok); if (!ok) *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:LIMit:LOWer")) { *key = SCPI_CFG_LIM_LO;   *vd = scpi_num(arg, &ok);           if (!ok) *err = 1; return 1; }
    if (hdr_match(hdr, "CALCulate:LIMit:UPPer")) { *key = SCPI_CFG_LIM_HI;   *vd = scpi_num(arg, &ok);           if (!ok) *err = 1; return 1; }
    /* ── Instrument SET (2026-08-15). Na rozdil od CALC nejdou do `meas_cfg_t`,
     * ale do stavu mereni — backend je obslouzi zvlast (viz `set_cfg`). ── */
    if (hdr_match(hdr, "SENSe:FREQuency:GATE"))    { *key = SCPI_CFG_GATE; *vd = scpi_num(arg, &ok);            if (!ok) *err = 1; return 1; }
    if (hdr_match(hdr, "SENSe:FREQuency:CHANnel")) { *key = SCPI_CFG_CHAN; *vu = (uint32_t)scpi_num(arg, &ok);  if (!ok) *err = 1; return 1; }
    /* INITiate[:IMMediate] = spustit mereni, ABORt = zastavit (oboji bez parametru). */
    if (hdr_match(hdr, "INITiate:IMMediate") || hdr_match(hdr, "INITiate")) { *key = SCPI_CFG_RUN; *vu = 1u; return 1; }
    if (hdr_match(hdr, "ABORt"))                                           { *key = SCPI_CFG_RUN; *vu = 0u; return 1; }
    return 0;
}

/* ── SCPI-99 OPERation / QUEStionable CONDition ──────────────────────────────
 * Okamzity stav (ne latched). Bitove pozice jsou nase (SCPI je pro pristroj
 * nedefinuje), ale MUSI byt stabilni — klient si je mapuje natvrdo:
 *   OPER  bit0 = FPGA link ziva, bit1 = GPS fix, bit2 = reference zamcena
 *   QUES  bit0 = mereni neduveryhodne, bit1 = bez GPS fixu, bit2 = ztrata reference
 * Sdili je `:CONDition` dotaz i latchovani do `:EVENt` (viz `scpi_status_latch`). */
static uint16_t scpi_oper_cond(const scpi_src_t *src)
{
    uint16_t w = 0;
    if (src->spi_ok)                                                      w |= 1u << 0;
    if ((src->valid & SCPI_V_GPS) && src->gps_fix_mode >= 2)              w |= 1u << 1;
    if (src->si5356_ok && !(src->si5356_status & ((1u << 3) | (1u << 4)))) w |= 1u << 2;
    return w;
}
static uint16_t scpi_ques_cond(const scpi_src_t *src)
{
    uint16_t w = 0;
    if (!src->spi_ok || !(src->valid & SCPI_V_FREQ) || src->freq_err)      w |= 1u << 0;
    if (!((src->valid & SCPI_V_GPS) && src->gps_fix_mode >= 2))           w |= 1u << 1;
    if (!src->si5356_ok || (src->si5356_status & ((1u << 3) | (1u << 4)))) w |= 1u << 2;
    return w;
}
/* Zlatchuj NABEZNE HRANY condition do EVENt registru (SCPI transition filter,
 * defaultne pozitivni). Vola se na zacatku kazdeho zpracovani prikazu, takze
 * kratka udalost mezi dvema dotazy klienta nezapadne. */
static void scpi_status_latch(scpi_ctx_t *c, const scpi_src_t *src)
{
    uint16_t o = scpi_oper_cond(src), q = scpi_ques_cond(src);
    c->oper_ev |= (uint16_t)(o & ~c->oper_prev);
    c->ques_ev |= (uint16_t)(q & ~c->ques_prev);
    c->oper_prev = o; c->ques_prev = q;
}

/* Kmitočet /4 [Hz] ze zdroje (0 = neplatné). */
static double src_freq_hz(const scpi_src_t *s)
{
    return (s->valid & SCPI_V_FREQ) ? (double)s->freq4_x100000 / 100000.0 : 0.0;
}

/* Zpracuje JEDNU programovou jednotku (bez ';'). Stav v `c`, data ve `src`. */
static size_t scpi_exec_one(scpi_ctx_t *c, scpi_src_t *src, const char *line, char *out, size_t out_sz)
{
    if (line == NULL || out == NULL || out_sz == 0) return 0;
    out[0] = '\0';
    while (*line == ' ' || *line == '\t') line++;

    char hdr[48]; int n = 0, is_query = 0;
    while (*line && *line != ' ' && *line != '\t' && *line != '?' && n < (int)sizeof(hdr) - 1)
        hdr[n++] = *line++;
    hdr[n] = '\0';
    if (*line == '?') { is_query = 1; line++; }
    while (*line == ' ' || *line == '\t') line++;
    const char *arg = line;

    /* ── Common commands (IEEE 488.2) ──────────────────────────────────────── */
    if (hdr_match(hdr, "*IDN") && is_query) {
        snprintf(out, out_sz, "OK2HAZ,GPSDO-Counter,0,%s", FW_VERSION_FULL); return strlen(out);
    }
    if (hdr_match(hdr, "*OPC")) {
        if (is_query) { snprintf(out, out_sz, "1"); return strlen(out); }
        c->esr |= 0x01u; return 0;
    }
    if (hdr_match(hdr, "*WAI") && !is_query) { return 0; }
    if (hdr_match(hdr, "*TST") && is_query) {
        snprintf(out, out_sz, "%d", src->selftest_pass ? 0 : 1); return strlen(out);
    }
    if (hdr_match(hdr, "*RST") && !is_query) { return 0; }
    if (hdr_match(hdr, "*CLS") && !is_query) { scpi_err_clear(c); c->esr = 0; return 0; }
    if (hdr_match(hdr, "*ESR")) {
        if (is_query) { snprintf(out, out_sz, "%u", (unsigned)c->esr); c->esr = 0; return strlen(out); }
    }
    if (hdr_match(hdr, "*ESE")) {
        if (is_query) { snprintf(out, out_sz, "%u", (unsigned)c->ese); return strlen(out); }
        int ok = 0; double v = scpi_num(arg, &ok);
        if (ok) { c->ese = (uint8_t)v; return 0; }
        scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out);
    }
    if (hdr_match(hdr, "*SRE")) {
        if (is_query) { snprintf(out, out_sz, "%u", (unsigned)c->sre); return strlen(out); }
        int ok = 0; double v = scpi_num(arg, &ok);
        if (ok) { c->sre = (uint8_t)v; return 0; }
        scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out);
    }
    if (hdr_match(hdr, "*STB") && is_query) {
        unsigned stb = 0;
        if (c->err_count > 0)          stb |= 0x04u;   /* bit2: chybova fronta */
        if (c->ques_ev & c->ques_ena)  stb |= 0x08u;   /* bit3: QUEStionable summary */
        if (c->esr & c->ese)           stb |= 0x20u;   /* bit5: ESB */
        if (c->oper_ev & c->oper_ena)  stb |= 0x80u;   /* bit7: OPERation summary */
        if (stb & c->sre)              stb |= 0x40u;   /* bit6: MSS/RQS */
        snprintf(out, out_sz, "%u", stb); return strlen(out);
    }

    /* ── SYSTem ────────────────────────────────────────────────────────────── */
    if (hdr_match(hdr, "SYSTem:VERSion") && is_query) { snprintf(out, out_sz, "1999.0"); return strlen(out); }
    /* ── SYSTem:DATE / SYSTem:TIME (SCPI-99) ─────────────────────────────────
     * Dotaz cte RTC (uz naformatovany v `g_rtc_text` = "YYYY-MM-DD HH:MM:SS",
     * plni ho defaultTask). SET zapise jen POZADAVEK — RTC registry vlastni
     * VYHRADNE defaultTask (`rtc_app_tick`), SCPI bezi v UartTasku.
     * ⚠️ Rucne nastaveny cas prezije jen do dalsiho GPS fixu: GPS je autoritativni
     * a `rtc_try_sync` ho prepise. Ma tedy smysl jen bez antény. */
    if (hdr_match(hdr, "SYSTem:DATE")) {
#if defined(CORE_CM7)
        if (is_query) {
            /* `g_rtc_text` = "YYYY-MM-DD HH:MM:SS" (plni defaultTask). Cteme z nej
             * primo aritmetikou - zadne pomocne buffery ani terminatory. */
            const volatile char *rt = g_rtc_text;
            int yy = (rt[0]-'0')*1000 + (rt[1]-'0')*100 + (rt[2]-'0')*10 + (rt[3]-'0');
            int mo = (rt[5]-'0')*10 + (rt[6]-'0');
            int dd = (rt[8]-'0')*10 + (rt[9]-'0');
            snprintf(out, out_sz, "%d,%d,%d", yy, mo, dd);
            return strlen(out);
        }
        int yy = 0, mm = 0, dd = 0;
        if (scpi_parse3(arg, &yy, &mm, &dd) &&
            yy >= 2000 && yy <= 2099 && mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31) {
            g_rtc_set_y = (uint16_t)yy; g_rtc_set_mo = (uint8_t)mm; g_rtc_set_d = (uint8_t)dd;
            g_rtc_set_pend |= 0x01u;          /* az nakonec -> defaultTask cte hotove hodnoty */
            return 0;
        }
        scpi_err_push(c, -222); snprintf(out, out_sz, "-222,\"Data out of range\""); return strlen(out);
#else
        /* ⚠️ RTC registry vlastni VYHRADNE CM7 defaultTask a IPC snapshot dnes cas
         * nese jen jako hotovy `rtc_unix`, ne editovatelny — bez pridaneho okna
         * v cmd ringu na CM4 nastavit nejde. Hardware missing, ne nase chyba. */
        scpi_err_push(c, -241); snprintf(out, out_sz, "-241,\"Hardware missing\""); return strlen(out);
#endif
    }
    if (hdr_match(hdr, "SYSTem:TIME")) {
#if defined(CORE_CM7)
        if (is_query) {
            const volatile char *rt = g_rtc_text;
            int hh = (rt[11]-'0')*10 + (rt[12]-'0');
            int mm = (rt[14]-'0')*10 + (rt[15]-'0');
            int ss = (rt[17]-'0')*10 + (rt[18]-'0');
            snprintf(out, out_sz, "%d,%d,%d", hh, mm, ss);
            return strlen(out);
        }
        int hh = 0, mi2 = 0, ss = 0;
        if (scpi_parse3(arg, &hh, &mi2, &ss) &&
            hh >= 0 && hh <= 23 && mi2 >= 0 && mi2 <= 59 && ss >= 0 && ss <= 59) {
            g_rtc_set_h = (uint8_t)hh; g_rtc_set_mi = (uint8_t)mi2; g_rtc_set_s = (uint8_t)ss;
            g_rtc_set_pend |= 0x02u;
            return 0;
        }
        scpi_err_push(c, -222); snprintf(out, out_sz, "-222,\"Data out of range\""); return strlen(out);
#else
        scpi_err_push(c, -241); snprintf(out, out_sz, "-241,\"Hardware missing\""); return strlen(out);
#endif
    }
    /* *OPT? — seznam osazenych voleb (IEEE 488.2). Prazdny retezec = zadne volby;
     * my hlasime, co pristroj realne umi, aby si ovladac nemusel hadat. */
    if (hdr_match(hdr, "*OPT") && is_query) {
        snprintf(out, out_sz, "GPSDO,DATALOG,SD"); return strlen(out);
    }
    /* CONFigure:FREQuency — u citace jedina merena funkce, takze je to fakticky
     * potvrzeni rezimu. Pripadny parametr (ocekavany kmitocet/rozsah) ignorujeme:
     * merime reciprocne pres celý rozsah, takze rozsah nastavovat netreba. */
    if (hdr_match(hdr, "CONFigure:FREQuency") && !is_query) { return 0; }
    if (hdr_match(hdr, "CONFigure") && is_query) { snprintf(out, out_sz, "\"FREQ\""); return strlen(out); }
    /* DISPlay:BRIGhtness 0..100 [%] -> `g_brightness` 0..255 (HW backlight aplikuje
     * UiTask). ⚠️ Clamp na 25..255 jako v UI — uplna tma by displej "ztratila"
     * a uzivatel by nemel jak jas vratit zpet dotykem. */
    if (hdr_match(hdr, "DISPlay:BRIGhtness")) {
#if defined(CORE_CM7)
        if (is_query) { snprintf(out, out_sz, "%u", (unsigned)((g_brightness * 100u + 127u) / 255u)); return strlen(out); }
        int ok = 0; double v = scpi_num(arg, &ok);
        if (ok && v >= 0.0 && v <= 100.0) {
            uint32_t b = (uint32_t)(v * 255.0 / 100.0 + 0.5);
            if (b < 25u)  b = 25u;
            if (b > 255u) b = 255u;
            g_brightness = (uint8_t)b; g_sys_cfg_dirty = 1;   /* persist (BKP + syscfg blob) */
            return 0;
        }
        scpi_err_push(c, -222); snprintf(out, out_sz, "-222,\"Data out of range\""); return strlen(out);
#else
        /* Displej (a jeho jas) je fyzicky pripojeny jen na CM7; CM4 na nej nema
         * IPC cestu ani duvod ji mit — vzhled neni "prístroj" (viz WEB_UI_PLAN.md 1.6). */
        scpi_err_push(c, -241); snprintf(out, out_sz, "-241,\"Hardware missing\""); return strlen(out);
#endif
    }
    /* SCPI-99 povinné. Bez něj VISA/IVI ovladače při inicializaci dostanou -113
     * a hned si zaplní chybovou frontu. */
    if (hdr_match(hdr, "SYSTem:CAPability") && is_query) {
        snprintf(out, out_sz, "\"COUNTER\""); return strlen(out);
    }
    /* Vyprázdní CELOU frontu najednou (SCPI-99 21.8.3) — jeden dotaz místo
     * smyčky `SYST:ERR?` dokud nepřijde 0. */
    if (hdr_match(hdr, "SYSTem:ERRor:ALL") && is_query) {
        /* Nejdelší možná položka: `;-224,"Illegal parameter value"` = 31 znaků
         * (nejdelší hláška má 23) + NUL. Dřív tu bylo 24 — na tuhle položku by
         * to nestačilo a spoléhalo by se na ořezovou větev níž. */
        #define SCPI_ERR_ITEM_MAX 32u
        size_t t = 0;
        do {
            int code = scpi_err_pop(c);
            int w = snprintf(out + t, out_sz - t, "%s%d,\"%s\"",
                             t ? ";" : "", code, scpi_err_msg(code));
            /* Nevešlo se → zahoď nedopsanou položku. ⚠️ `snprintf` do bufferu
             * UŽ zapsal ořezek, takže se musí ručně zaříznout zpátky na `t` —
             * jinak by návratová délka nesouhlasila s obsahem a volající
             * (který `out` tiskne jako řetězec) by vypsal půlku položky. */
            if (w <= 0 || (size_t)w >= out_sz - t) { out[t] = '\0'; break; }
            t += (size_t)w;
            if (code == 0) break;                 /* 0 = fronta prázdná, konec */
        } while (c->err_count > 0 && t + SCPI_ERR_ITEM_MAX < out_sz);
        #undef SCPI_ERR_ITEM_MAX
        return t;
    }
    if (hdr_match(hdr, "SYSTem:UPTime") && is_query) {
        snprintf(out, out_sz, "%lu", (unsigned long)src->uptime_s); return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:ERRor:COUNt") && is_query) {
        snprintf(out, out_sz, "%u", (unsigned)c->err_count); return strlen(out);
    }
    if ((hdr_match(hdr, "SYSTem:ERRor") || hdr_match(hdr, "SYSTem:ERRor:NEXT")) && is_query) {
        int code = scpi_err_pop(c);
        snprintf(out, out_sz, "%d,\"%s\"", code, scpi_err_msg(code)); return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:TEMPerature") && is_query) {   /* [°C], volitelně čidlo */
        int16_t c100 = src->t_ocxo_c100; uint32_t vb = SCPI_V_T_OCXO;   /* default OCXO */
        if      (kw_ci(arg, "BOARD") || kw_ci(arg, "STM")) { c100 = src->t_board_c100; vb = SCPI_V_T_BOARD; }
        else if (kw_ci(arg, "MCU"))                        { c100 = src->t_mcu_c100;   vb = SCPI_V_T_MCU; }
        else if (kw_ci(arg, "FPGA"))                       { c100 = src->t_fpga_c100;  vb = SCPI_V_T_FPGA; }
        if (src->valid & vb) fmt_scpi_f2(c100 / 100.0f, out, out_sz);
        else                 snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    /* Všechny teploty jedním dotazem — pro web/TCP klienta je 1 round-trip
     * místo 4 znát rozdíl; pořadí OCXO,BOARD,MCU,FPGA. */
    if (hdr_match(hdr, "SYSTem:TEMPerature:ALL") && is_query) {
        char t[4][16];
        const int16_t   v[4] = { src->t_ocxo_c100, src->t_board_c100, src->t_mcu_c100, src->t_fpga_c100 };
        const uint32_t  m[4] = { SCPI_V_T_OCXO,    SCPI_V_T_BOARD,    SCPI_V_T_MCU,    SCPI_V_T_FPGA };
        for (int i = 0; i < 4; i++) {
            if (src->valid & m[i]) fmt_scpi_f2(v[i] / 100.0f, t[i], sizeof t[i]);
            else                   snprintf(t[i], sizeof t[i], "9.91E37");
        }
        snprintf(out, out_sz, "%s,%s,%s,%s", t[0], t[1], t[2], t[3]); return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:GPS:STATus") && is_query) {
        snprintf(out, out_sz, "%u,%u", (unsigned)src->gps_fix_mode, (unsigned)src->gps_num_sat); return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:GPS:TIME") && is_query) {
        if (src->valid & SCPI_V_GPS) snprintf(out, out_sz, "%02u:%02u:%02u",
                                    (unsigned)src->gps_hour, (unsigned)src->gps_min, (unsigned)src->gps_sec);
        else snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if (hdr_match(hdr, "SYSTem:GPS:POSition") && is_query) {
        if (src->valid & SCPI_V_GPS) {
            char la[16], lo[16];
            fmt_scpi_deg6(src->gps_lat_deg, la, sizeof la);
            fmt_scpi_deg6(src->gps_lon_deg, lo, sizeof lo);
            snprintf(out, out_sz, "%s,%s,%d", la, lo, (int)src->gps_alt_m);
        } else snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }

    /* ── MEASure / FETCh (reálný FPGA kmitočet, ne sim) ─────────────────────── */
    if (hdr_match(hdr, "MEASure:FREQuency:ALL") && is_query) {
        char f4[24], f16[24];
        if (src->valid & SCPI_V_FREQ) fmt_scpi_hz(src->freq4_x100000, f4, sizeof f4); else snprintf(f4, sizeof f4, "9.91E37");
        if (src->valid & SCPI_V_DIV16) fmt_scpi_hz(src->freq16_x100000, f16, sizeof f16); else snprintf(f16, sizeof f16, "9.91E37");
        snprintf(out, out_sz, "%s,%s", f4, f16); return strlen(out);
    }
    if (hdr_match(hdr, "MEASure:FREQuency:DIV16") && is_query) {
        if (src->valid & SCPI_V_DIV16) fmt_scpi_hz(src->freq16_x100000, out, out_sz);
        else                           snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    /* READ? = INITiate + FETCh? (SCPI-99). Merit u nas bezi kontinualne, takze
     * "INIT" znamena zapnout RUN, kdyz stoji — a hned vratit posledni hodnotu.
     * Ovladace (VISA/IVI) tenhle jednoradkovy tvar cekaji jako zakladni zpusob mereni. */
    if (hdr_match(hdr, "READ") && is_query) {
        if (!src->set_running && src->set_cfg) (void)src->set_cfg(src, SCPI_CFG_RUN, 1u, 0.0);
        if (src->valid & SCPI_V_FREQ) fmt_scpi_hz(src->freq4_x100000, out, out_sz);
        else                          snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if ((hdr_match(hdr, "MEASure:FREQuency") || hdr_match(hdr, "FETCh:FREQuency")) && is_query) {
        if (src->valid & SCPI_V_FREQ) fmt_scpi_hz(src->freq4_x100000, out, out_sz);
        else                          snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    /* Perioda = 1/f. Pro čítač je to základní veličina, kterou SCPI tree doteď
     * neuměl. Jde ze STEJNÉHO reálného kmitočtu jako MEAS:FREQ? (ne simulace). */
    if (hdr_match(hdr, "MEASure:PERiod") && is_query) {
        fmt_scpi_period_s(src_freq_hz(src), out, out_sz); return strlen(out);
    }
    /* 1 = poslednímu měření NELZE věřit (ztráta signálu / chyba / neplatné).
     * Klient tak nemusí odvozovat důvěryhodnost z 9.91E37. */
    if (hdr_match(hdr, "MEASure:FREQuency:STALe") && is_query) {
        snprintf(out, out_sz, "%d", (!(src->valid & SCPI_V_FREQ) || src->freq_err) ? 1 : 0);
        return strlen(out);
    }
    /* Všechny napájecí větve jedním dotazem: 12V,5V,VC,VREF,VBAT. */
    if (hdr_match(hdr, "MEASure:VOLTage:ALL") && is_query) {
        char t[5][16];
        const uint16_t v[5] = { src->v_12v_mv, src->v_5v_mv, src->ocxo_vc_mv, src->vref_mv, src->vbat_mv };
        const uint32_t m[5] = { SCPI_V_V12,    SCPI_V_V5,    SCPI_V_VC,       SCPI_V_VREF,  SCPI_V_VBAT };
        for (int i = 0; i < 5; i++) {
            if (src->valid & m[i]) fmt_scpi_f2(v[i] / 1000.0f, t[i], sizeof t[i]);
            else                   snprintf(t[i], sizeof t[i], "9.91E37");
        }
        snprintf(out, out_sz, "%s,%s,%s,%s,%s", t[0], t[1], t[2], t[3], t[4]); return strlen(out);
    }
    if (hdr_match(hdr, "MEASure:VOLTage") && is_query) {      /* [V], výběr větve */
        uint16_t mv = src->v_12v_mv; uint32_t vb = SCPI_V_V12;   /* default 12V */
        if      (kw_ci(arg, "P5")  || kw_ci(arg, "5V"))   { mv = src->v_5v_mv;   vb = SCPI_V_V5; }
        else if (kw_ci(arg, "VC")  || kw_ci(arg, "OCXO")) { mv = src->ocxo_vc_mv; vb = SCPI_V_VC; }
        else if (kw_ci(arg, "VREF"))                      { mv = src->vref_mv;   vb = SCPI_V_VREF; }
        else if (kw_ci(arg, "VBAT") || kw_ci(arg, "BAT")) { mv = src->vbat_mv;   vb = SCPI_V_VBAT; }
        if (src->valid & vb) fmt_scpi_f2(mv / 1000.0f, out, out_sz);
        else                 snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if (hdr_match(hdr, "MEASure:POWer") && is_query) {       /* RF [dBm] přes AD8307 kalibraci */
        if ((src->valid & SCPI_V_RF) && src->ad8307_slope_mv_db > 1.0f)
            fmt_scpi_f2(src->rf_mv / src->ad8307_slope_mv_db + src->ad8307_intercept_dbm, out, out_sz);
        else snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }

    /* ── SENSe (parametry z posledního FPGA rámce) ─────────────────────────── */
    /* ⚠️ GATE?/CHAN? vraci NASTAVENOU hodnotu, ne udaj z posledniho FPGA ramce —
     * SCPI kontrakt je "co zapisu, to precte zpet" (`SENS:FREQ:GATE 1` -> `?` -> 1).
     * Skutecne zmerene okno (kolisa kolem nominalu) je na `SENS:FREQ:GATE:ACTual?`. */
    if (hdr_match(hdr, "SENSe:FREQuency:GATE:ACTual") && is_query) {
        if ((src->valid & SCPI_V_FRAME) && src->gate_ns) fmt_scpi_sec_ns(src->gate_ns, out, out_sz);
        else                                             snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if (hdr_match(hdr, "SENSe:FREQuency:GATE") && is_query) {
        fmt_scpi_f6(scpi_gate_s(src->set_gate_idx), out, out_sz); return strlen(out);
    }
    if (hdr_match(hdr, "SENSe:FREQuency:CHANnel") && is_query) {
        snprintf(out, out_sz, "%u", (unsigned)src->set_chan); return strlen(out);
    }
    /* Bezi mereni? (readback pro INITiate/ABORt) */
    if (hdr_match(hdr, "INITiate:CONTinuous") && is_query) {
        snprintf(out, out_sz, "%u", (unsigned)src->set_running); return strlen(out);
    }

    /* ── MMEMory (datalog) ─────────────────────────────────────────────────── */
    if (hdr_match(hdr, "MMEMory:DATA:COUNt") && is_query) {
        snprintf(out, out_sz, "%lu", (unsigned long)src->dl_records); return strlen(out);
    }
    if (hdr_match(hdr, "MMEMory:CATalog") && is_query) {
        snprintf(out, out_sz, "\"%s\",%lu,%lu,%lu,%lu,%u",
                 src->dl_backend ? src->dl_backend : "--",
                 (unsigned long)src->dl_records, (unsigned long)src->dl_capacity_rec,
                 (unsigned long)src->dl_last_seq, (unsigned long)src->dl_write_errors,
                 (unsigned)src->dl_wrapped);
        return strlen(out);
    }
    if (hdr_match(hdr, "MMEMory:DATA") && is_query) {         /* n-tý záznam od nejnovějšího */
        int nok = 0; double nn = scpi_num(arg, &nok);
        if (!nok || nn < 0.0) {
            scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out);
        }
        datalog_rec_t r;
        if (!src->read_log || !src->read_log(src, (uint32_t)nn, &r)) {
            scpi_err_push(c, -222); snprintf(out, out_sz, "-222,\"Data out of range\""); return strlen(out);
        }
        char fq[24], to[12], tb[12], rf[12], hd[12], vb[12];
        fmt_scpi_hz(r.freq_x100000, fq, sizeof fq);
        if (r.t_ocxo_c100 == DATALOG_INVALID16)  snprintf(to, sizeof to, "9.91E37"); else fmt_scpi_f2(r.t_ocxo_c100  / 100.0f, to, sizeof to);
        if (r.t_board_c100 == DATALOG_INVALID16) snprintf(tb, sizeof tb, "9.91E37"); else fmt_scpi_f2(r.t_board_c100 / 100.0f, tb, sizeof tb);
        /* ⚠️ `rf_mv` jsou SYROVE mV, ne dBm x10 — prevod stejnym vzorcem jako
         * `MEAS:POW?` (AD8307 slope/intercept z kalibrace). Do 2026-08-18 se tu
         * delilo deseti a 571 mV vyslo jako "57,1" v poli, ktere se tvari jako
         * dBm (spravne -61,2). Guard na slope: 0 by delilo nulou. */
        if (r.rf_mv == DATALOG_INVALID16) {
            snprintf(rf, sizeof rf, "9.91E37");
        } else {
            float slope = src->ad8307_slope_mv_db; if (slope < 1e-3f) slope = 25.0f;
            fmt_scpi_f2((float)r.rf_mv / slope + src->ad8307_intercept_dbm, rf, sizeof rf);
        }
        if (r.hdop10 == 255u)                    snprintf(hd, sizeof hd, "9.91E37"); else fmt_scpi_f2(r.hdop10 / 10.0f, hd, sizeof hd);
        /* VBAT [V]; zaznamy z doby pred 2026-08-17 ho nemaji -> SCPI NaN. */
        if (r.vbat_mv == DATALOG_INVALID16)      snprintf(vb, sizeof vb, "9.91E37"); else fmt_scpi_f2(r.vbat_mv / 1000.0f, vb, sizeof vb);
        snprintf(out, out_sz, "%lu,%lu,%s,%s,%s,%d,%s,%u,%u,%s,%s",
                 (unsigned long)r.seq, (unsigned long)r.t_unix, fq, to, tb,
                 (int)r.ocxo_vc_mv, rf, (unsigned)r.flags, (unsigned)r.sats, hd, vb);
        return strlen(out);
    }

    /* ── CALCulate (Math Mx+B/NULL + limity nad zdrojovou cfg) ─────────────── */
    if (hdr_match(hdr, "CALCulate:DATA") && is_query) {
        double hz = src_freq_hz(src);
        if (hz > 0.0) fmt_scpi_hz_d(meas_math_apply(&src->meas, hz), out, out_sz);
        else          snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if (hdr_match(hdr, "CALCulate:LIMit:FAIL") && is_query) {
        double hz = src_freq_hz(src);
        if (hz > 0.0) {
            meas_verdict_t v = meas_limit_eval(&src->meas, meas_math_apply(&src->meas, hz));
            snprintf(out, out_sz, "%d", (v == MEAS_LO || v == MEAS_HI) ? 1 : 0);
        } else snprintf(out, out_sz, "9.91E37");
        return strlen(out);
    }
    if (is_query && (hdr_match(hdr, "CALCulate:MATH:STATe") || hdr_match(hdr, "CALCulate:MATH:M") ||
                     hdr_match(hdr, "CALCulate:MATH:B") || hdr_match(hdr, "CALCulate:NULL:STATe") ||
                     hdr_match(hdr, "CALCulate:LIMit:STATe") || hdr_match(hdr, "CALCulate:LIMit:LOWer") ||
                     hdr_match(hdr, "CALCulate:LIMit:UPPer"))) {
        const meas_cfg_t *mc = &src->meas;   /* src->meas = konzistentní snímek zdroje */
        if      (hdr_match(hdr, "CALCulate:MATH:STATe"))  snprintf(out, out_sz, "%u", (unsigned)mc->math_en);
        else if (hdr_match(hdr, "CALCulate:MATH:M"))      fmt_scpi_hz_d(mc->m,  out, out_sz);
        else if (hdr_match(hdr, "CALCulate:MATH:B"))      fmt_scpi_hz_d(mc->b,  out, out_sz);
        else if (hdr_match(hdr, "CALCulate:NULL:STATe"))  snprintf(out, out_sz, "%u", (unsigned)mc->null_en);
        else if (hdr_match(hdr, "CALCulate:LIMit:STATe")) snprintf(out, out_sz, "%u", (unsigned)mc->limit_en);
        else if (hdr_match(hdr, "CALCulate:LIMit:LOWer")) fmt_scpi_hz_d(mc->lo, out, out_sz);
        else                                             fmt_scpi_hz_d(mc->hi, out, out_sz);
        return strlen(out);
    }

    /* ── STATus ────────────────────────────────────────────────────────────── */
    /* Měřená funkce (SCPI-99 SENSe). Čítač umí jedinou → konstanta, ale ovladače
     * se na to ptají a bez odpovědi by dostaly -113. */
    if (hdr_match(hdr, "SENSe:FUNCtion") && is_query) {
        snprintf(out, out_sz, "\"FREQ\""); return strlen(out);
    }
    /* Reference: GPSDO má interní OCXO (rozváděný Si5356), externí vstup není. */
    if (hdr_match(hdr, "SENSe:ROSCillator:SOURce") && is_query) {
        snprintf(out, out_sz, "INT"); return strlen(out);
    }
    /* 1 = reference v pořádku. ⚠️ Hodnotí se LOS_CLKIN (bit3) a PLL_LOL (bit4);
     * bit2 = LOS_XTAL je na této desce TRVALE 1 (krystal XA/XB neosazen, piny
     * uzemněné) — kdyby se počítal, hlásila by reference chybu pořád. */
    if (hdr_match(hdr, "SENSe:ROSCillator:LOCKed") && is_query) {
        int lk = (src->si5356_ok && !(src->si5356_status & ((1u << 3) | (1u << 4)))) ? 1 : 0;
        snprintf(out, out_sz, "%d", lk); return strlen(out);
    }
    /* SCPI-99 povinné. OPERation/QUEStionable ENABLE registry zatím nemáme
     * (jen 488.2 ESE/SRE), takže je to fakticky no-op — ale MUSÍ se přijmout,
     * jinak inicializační sekvence `*RST;*CLS;STAT:PRES` skončí chybou. */
    /* PRESet dle SCPI-99: enable registry na 0 (event/condition se NEmazou). */
    if (hdr_match(hdr, "STATus:PRESet") && !is_query) { c->oper_ena = 0; c->ques_ena = 0; return 0; }
    if (hdr_match(hdr, "STATus:OPERation:CONDition") && is_query) {
        snprintf(out, out_sz, "%u", (unsigned)scpi_oper_cond(src)); return strlen(out);
    }
    if (hdr_match(hdr, "STATus:QUEStionable:CONDition") && is_query) {
        snprintf(out, out_sz, "%u", (unsigned)scpi_ques_cond(src)); return strlen(out);
    }
    /* EVENt (= holy `STAT:OPER?`): latched, CTENI MAZE. */
    if ((hdr_match(hdr, "STATus:OPERation:EVENt") || hdr_match(hdr, "STATus:OPERation")) && is_query) {
        snprintf(out, out_sz, "%u", (unsigned)c->oper_ev); c->oper_ev = 0; return strlen(out);
    }
    if ((hdr_match(hdr, "STATus:QUEStionable:EVENt") || hdr_match(hdr, "STATus:QUEStionable")) && is_query) {
        snprintf(out, out_sz, "%u", (unsigned)c->ques_ev); c->ques_ev = 0; return strlen(out);
    }
    if (hdr_match(hdr, "STATus:OPERation:ENABle")) {
        if (is_query) { snprintf(out, out_sz, "%u", (unsigned)c->oper_ena); return strlen(out); }
        int ok = 0; double v = scpi_num(arg, &ok);
        if (ok) { c->oper_ena = (uint16_t)v; return 0; }
        scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out);
    }
    if (hdr_match(hdr, "STATus:QUEStionable:ENABle")) {
        if (is_query) { snprintf(out, out_sz, "%u", (unsigned)c->ques_ena); return strlen(out); }
        int ok = 0; double v = scpi_num(arg, &ok);
        if (ok) { c->ques_ena = (uint16_t)v; return 0; }
        scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out);
    }

    /* ── CALC SET (akce; přes src->set_cfg → g_meas_cfg na CM7 / cmd ring na CM4) ── */
    if (!is_query) {
        uint8_t key; uint32_t vu = 0; double vd = 0.0; int err = 0;
        if (scpi_calc_parse(hdr, arg, &key, &vu, &vd, &err)) {
            if (err) { scpi_err_push(c, -224); snprintf(out, out_sz, "-224,\"Illegal parameter value\""); return strlen(out); }
            if (src->set_cfg && src->set_cfg(src, key, vu, vd)) return 0;   /* OK → ticho */
            scpi_err_push(c, -230); snprintf(out, out_sz, "-230,\"Data corrupt or stale\""); return strlen(out);
        }
    }

    scpi_err_push(c, -113);
    snprintf(out, out_sz, "-113,\"Undefined header\"");
    return strlen(out);
}

size_t scpi_process_ctx(scpi_ctx_t *ctx, scpi_src_t *src, const char *line, char *out, size_t out_sz)
{
    if (ctx == NULL || src == NULL || line == NULL || out == NULL || out_sz == 0) return 0;
    out[0] = '\0';
    /* Zlatchuj hrany OPER/QUES condition -> EVENt. Musi to byt PRED vykonanim
     * prikazu, aby `STAT:QUES?` videl i udalost, ktera nastala tesne pred dotazem. */
    scpi_status_latch(ctx, src);
    if (strchr(line, ';') == NULL) return scpi_exec_one(ctx, src, line, out, out_sz);

    /* Složená zpráva (IEEE 488.2): jednotky ';', odpovědi dotazů spojené ';'. */
    size_t total = 0;
    char sub[56];
    while (*line) {
        int k = 0;
        while (*line && *line != ';' && k < (int)sizeof(sub) - 1) sub[k++] = *line++;
        sub[k] = '\0';
        while (*line && *line != ';') line++;
        if (*line == ';') line++;
        const char *t = sub; while (*t == ' ' || *t == '\t') t++;
        if (*t == '\0') continue;
        char rb[64];
        size_t rn = scpi_exec_one(ctx, src, sub, rb, sizeof rb);
        if (rn == 0) continue;
        if (total && total + 1 < out_sz) out[total++] = ';';
        size_t room = (total + 1 < out_sz) ? out_sz - total - 1 : 0;
        size_t cpy  = (rn < room) ? rn : room;
        memcpy(out + total, rb, cpy);
        total += cpy;
        out[total] = '\0';
        if (cpy < rn) break;
    }
    return total;
}

/* ══════════════════ CM7 backend (globály → scpi_src_t + akce) ═══════════════ */
#if defined(CORE_CM7)
/* Config SET na CM7: zapíše g_meas_cfg (kritická sekce) + zrcadlo src->meas. */
static int scpi_src_set_cfg_cm7(scpi_src_t *s, uint8_t key, uint32_t vu, double vd)
{
    /* ── Instrument SET (GATE/CHAN/RUN): NEjde do `g_meas_cfg`, ale do stavu mereni,
     * ktery vlastni UiTask. SCPI bezi v UartTasku -> zapiseme jen POZADAVEK a UiTask
     * ho aplikuje (`screen_main_apply_cfg_req`) vcetne prekresleni footeru.
     * Skladame na AKTUALNI `g_ui_cfg`, aby dva SETy za sebou nesmazaly jeden druhy. */
    if (key == SCPI_CFG_GATE || key == SCPI_CFG_CHAN || key == SCPI_CFG_RUN) {
        uint8_t cur = g_ui_cfg_req_pend ? g_ui_cfg_req : g_ui_cfg;
        if (key == SCPI_CFG_GATE) {
            int gi = scpi_gate_idx_from_s(vd);
            if (gi < 0) return 0;                       /* mimo presety -> -222 */
            cur = (uint8_t)((cur & ~(3u << 2)) | ((uint32_t)gi << 2));
            s->set_gate_idx = (uint8_t)gi;
        } else if (key == SCPI_CFG_CHAN) {
            if (vu > 1u) return 0;                      /* mame jen kanal 0/1 */
            cur = (uint8_t)((cur & ~(1u << 1)) | ((vu & 1u) << 1));
            s->set_chan = (uint8_t)vu;
        } else {
            cur = (uint8_t)((cur & ~(1u << 4)) | ((vu ? 1u : 0u) << 4));
            s->set_running = vu ? 1u : 0u;
        }
        g_ui_cfg_req = cur;
        g_ui_cfg_req_pend = 1;                          /* az teprve ted -> UiTask cte hotovou hodnotu */
        return 1;
    }
    double fhz = src_freq_hz(s);   /* pro NULL_ACQ */
    meas_cfg_t tmp; taskENTER_CRITICAL(); tmp = g_meas_cfg; taskEXIT_CRITICAL();
    if (!scpi_cfg_apply(&tmp, key, vu, vd, fhz)) return 0;
    taskENTER_CRITICAL(); g_meas_cfg = tmp; taskEXIT_CRITICAL();
    s->meas = tmp;                 /* aby compound SET→readback ve stejné zprávě sedělo */
    return 1;
}
static int scpi_src_read_log_cm7(scpi_src_t *s, uint32_t from_newest, datalog_rec_t *out)
{
    (void)s;
    return datalog_read_back(from_newest, out) ? 1 : 0;   /* ⚠️ blokující QSPI čtení */
}

/* ⚠️ `full == 0` naplni JEN to, co je zadarmo (globaly), a preskoci drahe cesty:
 * `gps_get()` (kopie ~200 B v kriticke sekci) a `fpga_freq_get_last()` (kopie
 * latche s vypnutymi IRQ). Pouziva se pro IEEE 488.2 spolecne prikazy (`*IDN?`
 * a spol.), ktere ze zdroje nectou NIC krome `selftest_pass` — viz `scpi_process`.
 * Sensory/kalibrace zustavaji i v levne variante: je to jen cteni globalu bez
 * zamku, tedy rove tak drahe jako ta podminka navic. */
static void scpi_src_load_cm7_ex(scpi_src_t *src, int full)
{
    /* Nastaveny stav mereni (SET/readback). Cte se z `g_ui_cfg` = tentyz zdroj,
     * ktery pouziva UI i persistence do BKP; kodovani: bit0 mode, bit1 chan,
     * bity2:3 gate, bit4 run. Kdyz ceka nas vlastni SET, uz ma prednost (aby
     * `SET;readback` v JEDNE zprave vratilo novou hodnotu, ne tu predchozi). */
    {
        uint8_t c = g_ui_cfg_req_pend ? g_ui_cfg_req : g_ui_cfg;
        src->set_chan     = (uint8_t)((c >> 1) & 1u);
        src->set_gate_idx = (uint8_t)((c >> 2) & 3u);
        src->set_running  = (uint8_t)((c >> 4) & 1u);
    }
    memset(src, 0, sizeof *src);
    src->selftest_pass = (g_selftest_res == 1);
    src->uptime_s      = g_uptime_s;
    if (!full) return;
    fpga_meas_t m;
    if (fpga_freq_get_last(&m)) {
        src->valid |= SCPI_V_FRAME;
        src->gate_ns        = (uint32_t)m.gate_time_ns;
        src->channel_id     = m.channel_id;
        src->freq4_x100000  = m.frequency_x100000;
        src->freq16_x100000 = m.freq16_x100000;
        if (m.error_flags & (FPGA_ERR_SIGNAL_LOST | FPGA_ERR_MEAS)) src->freq_err = 1;
        int fresh_ok = (m.measurement_status & 0x01u) && !(m.error_flags & FPGA_ERR_SIGNAL_LOST);
        if (fresh_ok)                                  src->valid |= SCPI_V_FREQ;
        if (fresh_ok && !(m.status2 & FPGA_ST2_DIV16_ERR)) src->valid |= SCPI_V_DIV16;
    }
    if (g_sensors[SENS_T49].valid)    { src->t_ocxo_c100  = (int16_t)(g_sensors[SENS_T49].last  * 100.0f); src->valid |= SCPI_V_T_OCXO; }
    if (g_sensors[SENS_T48].valid)    { src->t_board_c100 = (int16_t)(g_sensors[SENS_T48].last  * 100.0f); src->valid |= SCPI_V_T_BOARD; }
    if (g_sensors[SENS_CORE_T].valid) { src->t_mcu_c100   = (int16_t)(g_sensors[SENS_CORE_T].last* 100.0f); src->valid |= SCPI_V_T_MCU; }
    if (g_sensors[SENS_T4A].valid)    { src->t_fpga_c100  = (int16_t)(g_sensors[SENS_T4A].last  * 100.0f); src->valid |= SCPI_V_T_FPGA; }
    if (g_sensors[SENS_ADS0].valid)   { src->ocxo_vc_mv = (uint16_t)g_sensors[SENS_ADS0].last; src->valid |= SCPI_V_VC; }
    if (g_sensors[SENS_ADS1].valid)   { src->rf_mv      = (uint16_t)g_sensors[SENS_ADS1].last; src->valid |= SCPI_V_RF; }
    if (g_sensors[SENS_ADS2].valid)   { src->v_12v_mv   = (uint16_t)g_sensors[SENS_ADS2].last; src->valid |= SCPI_V_V12; }
    if (g_sensors[SENS_ADS3].valid)   { src->v_5v_mv    = (uint16_t)g_sensors[SENS_ADS3].last; src->valid |= SCPI_V_V5; }
    if (g_sensors[SENS_VDDA].valid)   { src->vref_mv    = (uint16_t)g_sensors[SENS_VDDA].last; src->valid |= SCPI_V_VREF; }
    if (g_sensors[SENS_VBAT].valid)   { src->vbat_mv    = (uint16_t)g_sensors[SENS_VBAT].last; src->valid |= SCPI_V_VBAT; }
    src->ad8307_slope_mv_db   = g_calib.ad8307_slope_mv_db;
    src->ad8307_intercept_dbm = g_calib.ad8307_intercept_dbm;
    gps_data_t g; gps_get(&g);
    src->gps_fix_mode = g.fix_mode; src->gps_num_sat = g.num_sat;
    if (g.valid) {
        src->valid |= SCPI_V_GPS;
        src->gps_hour = g.hour; src->gps_min = g.minute; src->gps_sec = g.second;
        src->gps_lat_deg = g.lat_deg; src->gps_lon_deg = g.lon_deg; src->gps_alt_m = g.alt_m;
    }
    src->spi_ok = g_spi_ok; src->si5356_status = g_si5356_status; src->si5356_ok = g_si5356_ok;
    /* (`selftest_pass` a `uptime_s` uz nastavila levna cast nahore.) */
    taskENTER_CRITICAL(); src->meas = g_meas_cfg; taskEXIT_CRITICAL();
    datalog_status_t st; datalog_get_status(&st);
    src->dl_backend      = st.backend ? st.backend : "--";
    src->dl_records      = st.records;      src->dl_capacity_rec = st.capacity_rec;
    src->dl_last_seq     = st.last_seq;     src->dl_write_errors = st.write_errors;
    src->dl_wrapped      = st.wrapped ? 1u : 0u;
    src->set_cfg  = scpi_src_set_cfg_cm7;
    src->read_log = scpi_src_read_log_cm7;
}

static scpi_ctx_t s_default_ctx;   /* USB CDC = jediná session */
void scpi_src_load_cm7(scpi_src_t *src) { scpi_src_load_cm7_ex(src, 1); }

size_t scpi_process(const char *line, char *out, size_t out_sz)
{
    /* Spolecne prikazy IEEE 488.2 (`*IDN?`, `*OPC?`, `*CLS`, `*ESR?`, `*ESE`,
     * `*STB?`, `*RST`, `*WAI`, `*TST?`) ctou ze `scpi_src_t` JEDINE
     * `selftest_pass`. Nema tedy smysl kvuli nim delat `gps_get` v kriticke
     * sekci a `fpga_freq_get_last` s vypnutymi IRQ.
     * ⚠️ Zamerne se to omezuje na `*` prikazy BEZ `;`. Sirsi maska "co ktery
     * prikaz potrebuje" by sla napsat, ale za cenu realneho rizika: spatne
     * urcena maska = tise SPATNA odpoved. Tady je nulove — mnozina poli, ktera
     * `*` prikazy ctou, je uzavrena a overitelna pohledem. Slozena zprava
     * muze obsahovat cokoli, proto se u ni nacita vse. */
    int cheap = (line != NULL && line[0] == '*' && strchr(line, ';') == NULL);
    scpi_src_t src; scpi_src_load_cm7_ex(&src, !cheap);
    return scpi_process_ctx(&s_default_ctx, &src, line, out, out_sz);
}
#endif /* CORE_CM7 */

/* ── Selftest (jádro: parser + status model + config apply, nad dummy zdrojem) ── */
/* Testovací set_cfg: aplikuje na src->meas (test freq pro NULL_ACQ). */
static int scpi_test_set_cfg(scpi_src_t *s, uint8_t key, uint32_t vu, double vd)
{
    /* Instrument klice (GATE/CHAN/RUN) nejdou do `meas_cfg_t` — v testu je aplikujeme
     * rovnou na `src`, aby slo overit SET->readback bez bezicich tasku. */
    if (key == SCPI_CFG_GATE) { int gi = scpi_gate_idx_from_s(vd); if (gi < 0) return 0;
                                s->set_gate_idx = (uint8_t)gi; return 1; }
    if (key == SCPI_CFG_CHAN) { if (vu > 1u) return 0; s->set_chan = (uint8_t)vu; return 1; }
    if (key == SCPI_CFG_RUN)  { s->set_running = vu ? 1u : 0u; return 1; }
    return scpi_cfg_apply(&s->meas, key, vu, vd, 1e7);
}

/* ── Který assert spadl ──────────────────────────────────────────────────────
 * `scpi_selftest()` je ~90 kontrol slitých do jednoho `ok`, takže „FAIL" sám
 * o sobě neřekne nic použitelného — a bez nativního kompilátoru na PC se test
 * nedá spustit jinde než na cíli. Proto si pamatuje ŘÁDEK prvního neúspěšného
 * assertu; UART `selftest` ho vypíše.
 *
 * Trik s makrem místo přepsání všech ~90 řádků na `CK(...)`: `ok` se uvnitř
 * funkce expanduje na `*scpi_st_chk(__LINE__)`, takže z `ok &= X` je
 * `*scpi_st_chk(L) &= X`. `scpi_st_chk` se dostane ke slovu PŘED zápisem, tedy
 * ještě vidí výsledek předchozího assertu — a když je nulový, zapamatuje si
 * jeho řádek. Nezávisí to na pořadí vyhodnocení `X` vs. `scpi_st_chk(L)`,
 * protože zápis do `ok` nastane až po obou. */
static int  s_st_ok = 1;      /* akumulátor (drží ho makro `ok`) */
static int  s_st_line;        /* řádek právě probíhajícího assertu */
static int  s_st_fail_line;   /* řádek PRVNÍHO neúspěšného assertu; 0 = žádný */

static int *scpi_st_chk(int line)
{
    if (!s_st_ok && !s_st_fail_line) s_st_fail_line = s_st_line;
    s_st_line = line;
    return &s_st_ok;
}

int scpi_selftest_fail_line(void) { return s_st_fail_line; }

int scpi_selftest(void)
{
    /* ⚠️ `src` je STATIC: `scpi_src_t` je velká struktura a `run_selftests()` běží
     * v defaultTasku (2560 B stack). Jako lokál dělal tenhle test 672 B rámec —
     * druhý největší po `gps_selftest`, který přesně takhle protrhl stack a přepsal
     * FreeRTOS heap (viz komentář u `static gsv_state_t st` v gps.c). Stejný vzor
     * jako `static ipc_shared_t t` v `ipc_selftest`; run_selftests je serializovaný. */
    s_st_ok = 1; s_st_line = 0; s_st_fail_line = 0;
    #define ok (*scpi_st_chk(__LINE__))
    char b[80];
    scpi_ctx_t x; scpi_ctx_init(&x);
    static scpi_src_t src; memset(&src, 0, sizeof src);   /* dummy: vše neplatné, defaultní cfg */
    meas_math_defaults(&src.meas);
    src.dl_backend = "--";
    src.set_cfg = scpi_test_set_cfg;   /* SET aplikuje na src.meas */
    src.read_log = NULL;

    scpi_process_ctx(&x, &src, "*IDN?", b, sizeof b);            ok &= (strncmp(b, "OK2HAZ,", 7) == 0);
    scpi_process_ctx(&x, &src, "*idn?", b, sizeof b);            ok &= (strncmp(b, "OK2HAZ,", 7) == 0);  /* case */
    scpi_process_ctx(&x, &src, "*OPC?", b, sizeof b);            ok &= (strcmp(b, "1") == 0);
    ok &= (scpi_process_ctx(&x, &src, "*RST", b, sizeof b) == 0);

    scpi_process_ctx(&x, &src, "SYST:ERR?", b, sizeof b);        ok &= (strncmp(b, "0,", 2) == 0);
    scpi_process_ctx(&x, &src, "SYSTem:ERRor?", b, sizeof b);    ok &= (strncmp(b, "0,", 2) == 0);
    scpi_process_ctx(&x, &src, "system:version?", b, sizeof b);  ok &= (strcmp(b, "1999.0") == 0);

    /* Dotazy — dummy zdroj (vše neplatné) → NaN, ale odpoví. */
    ok &= (scpi_process_ctx(&x, &src, "MEAS:FREQ?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "MEAS:FREQ:DIV16?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "SYST:GPS:TIME?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "SYST:GPS:POS?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "CALC:DATA?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "CALC:LIM:FAIL?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "SYST:UPT?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "MEAS:VOLT? P12", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "MEAS:VOLT? P5",  b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "MEAS:VOLT?",     b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "MEAS:POW?",      b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "SENS:FREQ:GATE?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "SENS:FREQ:CHAN?", b, sizeof b) > 0);

    /* ── Instrument SET + readback (2026-08-15). Do teto chvile bylo SCPI mimo
     * Math read-only, takze prave tohle je jadro noveho chovani. ── */
    scpi_process_ctx(&x, &src, "SENS:FREQ:GATE 10", b, sizeof b);
    ok &= (src.set_gate_idx == 2);                       /* 10 s = index 2 */
    scpi_process_ctx(&x, &src, "SENS:FREQ:GATE?", b, sizeof b);
    ok &= (strncmp(b, "10.000000", 9) == 0);             /* readback = nastavena hodnota */
    scpi_process_ctx(&x, &src, "SENS:FREQ:GATE 0.1", b, sizeof b);
    ok &= (src.set_gate_idx == 0);                       /* toleranci 1 % projde i "1E-1" */
    scpi_process_ctx(&x, &src, "SENS:FREQ:GATE 3.7", b, sizeof b);
    ok &= (src.set_gate_idx == 0);                       /* mimo preset -> SET se NEaplikuje */
    scpi_process_ctx(&x, &src, "SENS:FREQ:CHAN 1", b, sizeof b);
    ok &= (src.set_chan == 1);
    scpi_process_ctx(&x, &src, "SENS:FREQ:CHAN?", b, sizeof b);
    ok &= (b[0] == '1');
    scpi_process_ctx(&x, &src, "SENS:FREQ:CHAN 5", b, sizeof b);
    ok &= (src.set_chan == 1);                           /* mame jen 0/1 -> beze zmeny */
    /* INITiate / ABORt / INIT:CONT? */
    scpi_process_ctx(&x, &src, "ABOR", b, sizeof b);
    ok &= (src.set_running == 0);
    scpi_process_ctx(&x, &src, "INIT", b, sizeof b);
    ok &= (src.set_running == 1);
    scpi_process_ctx(&x, &src, "INIT:CONT?", b, sizeof b);
    ok &= (b[0] == '1');
    scpi_process_ctx(&x, &src, "ABOR", b, sizeof b);
    ok &= (src.set_running == 0);
    /* READ? = INIT + FETCh -> musi mereni ZAPNOUT i kdyz stalo */
    scpi_process_ctx(&x, &src, "READ?", b, sizeof b);
    ok &= (src.set_running == 1);

    /* ── STATus OPER/QUES: EVENt je latched a CTENI HO MAZE (2026-08-15) ── */
    src.spi_ok = 0;                                       /* vse spatne -> QUES bit0 nabezna hrana */
    scpi_process_ctx(&x, &src, "STAT:QUES:COND?", b, sizeof b);
    ok &= (b[0] != '0');                                  /* condition hlasi problem */
    scpi_process_ctx(&x, &src, "STAT:QUES?", b, sizeof b);
    ok &= (b[0] != '0');                                  /* event zlatchovan */
    scpi_process_ctx(&x, &src, "STAT:QUES?", b, sizeof b);
    ok &= (strcmp(b, "0") == 0);                          /* druhe cteni uz nuluje */
    scpi_process_ctx(&x, &src, "STAT:QUES:ENAB 7", b, sizeof b);
    scpi_process_ctx(&x, &src, "STAT:QUES:ENAB?", b, sizeof b);
    ok &= (strcmp(b, "7") == 0);
    scpi_process_ctx(&x, &src, "STAT:PRES", b, sizeof b); /* PRESet -> enable = 0 */
    scpi_process_ctx(&x, &src, "STAT:QUES:ENAB?", b, sizeof b);
    ok &= (strcmp(b, "0") == 0);
    scpi_process_ctx(&x, &src, "STAT:OPER:COND?", b, sizeof b);
    ok &= (b[0] >= '0' && b[0] <= '9');   /* vraci cislo, ne prazdno */

    /* *OPT? / CONF? / DISP:BRIG (readback v %) */
    scpi_process_ctx(&x, &src, "*OPT?", b, sizeof b);
    ok &= (strstr(b, "GPSDO") != NULL);
    scpi_process_ctx(&x, &src, "CONF?", b, sizeof b);
    ok &= (strncmp(b, "\"FREQ\"", 6) == 0);
    scpi_process_ctx(&x, &src, "DISP:BRIG 50", b, sizeof b);
    scpi_process_ctx(&x, &src, "DISP:BRIG?", b, sizeof b);
    ok &= (b[0] == '5');                                  /* ~50 % zpet */
    scpi_process_ctx(&x, &src, "DISP:BRIG 150", b, sizeof b);
    ok &= (strncmp(b, "-222", 4) == 0);                   /* mimo rozsah -> chyba */

    /* ── SYST:DATE/TIME: parser tri cisel + odmitnuti nesmyslu (2026-08-15) ── */
    { int a1 = 0, a2 = 0, a3 = 0;
      ok &= (scpi_parse3("2026,8,15", &a1, &a2, &a3) && a1 == 2026 && a2 == 8 && a3 == 15);
      ok &= (scpi_parse3(" 12 , 34 , 56 ", &a1, &a2, &a3) && a1 == 12 && a2 == 34 && a3 == 56);
      ok &= (scpi_parse3("2026,8", &a1, &a2, &a3) == 0);        /* jen dve cisla */
      ok &= (scpi_parse3("2026,8,15,1", &a1, &a2, &a3) == 0);   /* ctyri cisla */
      ok &= (scpi_parse3("2026,x,15", &a1, &a2, &a3) == 0); }   /* nepovoleny znak */
    scpi_process_ctx(&x, &src, "SYST:DATE 2026,13,1", b, sizeof b);
    ok &= (strncmp(b, "-222", 4) == 0);                   /* mesic 13 -> chyba */
    scpi_process_ctx(&x, &src, "SYST:TIME 25,0,0", b, sizeof b);
    ok &= (strncmp(b, "-222", 4) == 0);                   /* hodina 25 -> chyba */
    ok &= (scpi_process_ctx(&x, &src, "MMEM:CAT?", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "MMEM:DATA:COUN?", b, sizeof b) > 0);

    /* Platný zdroj: MEAS:FREQ? vrátí číslo (ne NaN). */
    {
        static scpi_src_t sv;   /* static ze stejneho duvodu jako `src` vyse */
        memset(&sv, 0, sizeof sv); meas_math_defaults(&sv.meas);
        sv.valid = SCPI_V_FREQ; sv.freq4_x100000 = 1000000000000ull;   /* 10 MHz ×1e5 */
        scpi_process_ctx(&x, &sv, "MEAS:FREQ?", b, sizeof b);
        ok &= (strncmp(b, "10000000.", 9) == 0);
        sv.valid |= SCPI_V_T_OCXO; sv.t_ocxo_c100 = 4512;              /* 45,12 °C */
        scpi_process_ctx(&x, &sv, "SYST:TEMP?", b, sizeof b);          ok &= (strncmp(b, "45.1", 4) == 0);

        /* Perioda ze stejneho kmitocu: 10 MHz -> 100 ns = 0,000000100000000 s. */
        scpi_process_ctx(&x, &sv, "MEAS:PER?", b, sizeof b);
        ok &= (strcmp(b, "0.000000100000000") == 0);
        /* Platny kmitocet bez chyby -> neni stale. */
        scpi_process_ctx(&x, &sv, "MEAS:FREQ:STAL?", b, sizeof b);     ok &= (strcmp(b, "0") == 0);
        sv.freq_err = 1;
        scpi_process_ctx(&x, &sv, "MEAS:FREQ:STAL?", b, sizeof b);     ok &= (strcmp(b, "1") == 0);
        /* Agregaty: OCXO plati, zbytek NaN -> presne 4 resp. 5 poli. */
        scpi_process_ctx(&x, &sv, "SYST:TEMP:ALL?", b, sizeof b);
        ok &= (strncmp(b, "45.12,9.91E37,9.91E37,9.91E37", 29) == 0);
        scpi_process_ctx(&x, &sv, "MEAS:VOLT:ALL?", b, sizeof b);
        { int commas = 0; for (const char *p = b; *p; p++) if (*p == ',') commas++; ok &= (commas == 4); }
        /* Reference: si5356_ok=0 -> nezamceno; ok + jen LOS_XTAL(bit2) -> zamceno
         * (bit2 je na teto desce trvale 1, nesmi se hodnotit). */
        scpi_process_ctx(&x, &sv, "SENS:ROSC:LOCK?", b, sizeof b);     ok &= (strcmp(b, "0") == 0);
        sv.si5356_ok = 1; sv.si5356_status = 0x04;
        scpi_process_ctx(&x, &sv, "SENS:ROSC:LOCK?", b, sizeof b);     ok &= (strcmp(b, "1") == 0);
        sv.si5356_status = 0x08;                                       /* LOS_CLKIN */
        scpi_process_ctx(&x, &sv, "SENS:ROSC:LOCK?", b, sizeof b);     ok &= (strcmp(b, "0") == 0);

        /* ── Rozsahove pojistky formatovacu (audit 2026-08-13) ──────────────
         * Obe cesty slo vyvolat BEZNYM prikazem a obe koncily pretypovanim
         * double mimo rozsah cile = NEDEFINOVANE chovani. Testuje se proto
         * hranicni chovani, ne jen stastna cesta. */
        sv.freq4_x100000 = 1;                    /* 1e-5 Hz -> perioda 1e5 s */
        scpi_process_ctx(&x, &sv, "MEAS:PER?", b, sizeof b);
        ok &= (strcmp(b, "9.91E37") == 0);       /* mimo rozsah, ne pretecena cislice */
        sv.freq4_x100000 = 1000000000000ull;     /* zpet na 10 MHz */

        sv.meas.math_en = 1; sv.meas.m = 1e30; sv.meas.b = 0.0;
        scpi_process_ctx(&x, &sv, "CALC:MATH:M?", b, sizeof b);  ok &= (strcmp(b, "9.91E37") == 0);
        scpi_process_ctx(&x, &sv, "CALC:DATA?",   b, sizeof b);  ok &= (strcmp(b, "9.91E37") == 0);
        meas_math_defaults(&sv.meas);            /* uklid pro pripadne dalsi pouziti */
    }

    /* Nove SCPI-99 povinne prikazy + SENSe konstanty. */
    scpi_ctx_init(&x);
    scpi_process_ctx(&x, &src, "SYST:CAP?",   b, sizeof b);  ok &= (strcmp(b, "\"COUNTER\"") == 0);
    scpi_process_ctx(&x, &src, "SENS:FUNC?",  b, sizeof b);  ok &= (strcmp(b, "\"FREQ\"") == 0);
    scpi_process_ctx(&x, &src, "SENS:ROSC:SOUR?", b, sizeof b); ok &= (strcmp(b, "INT") == 0);
    ok &= (scpi_process_ctx(&x, &src, "STAT:PRES", b, sizeof b) == 0);   /* prijmout, ticho */
    ok &= (scpi_process_ctx(&x, &src, "SYST:ERR?", b, sizeof b) > 0 && strncmp(b, "0,", 2) == 0);
    /* SYST:ERR:ALL? vyprazdni celou frontu jednim dotazem. */
    scpi_process_ctx(&x, &src, "FOO?", b, sizeof b);
    scpi_process_ctx(&x, &src, "BAR?", b, sizeof b);
    scpi_process_ctx(&x, &src, "SYST:ERR:ALL?", b, sizeof b);
    { int semi = 0; for (const char *p = b; *p; p++) if (*p == ';') semi++;
      /* Dve chyby = dve polozky, jeden oddelovac. ⚠️ ZADNA koncova "0,No error"
       * — ta se vraci JEN kdyz je fronta prazdna (viz assert nize). */
      ok &= (strncmp(b, "-113,", 5) == 0 && semi == 1); }
    scpi_process_ctx(&x, &src, "SYST:ERR:COUN?", b, sizeof b);   ok &= (b[0] == '0');
    /* Prazdna fronta -> jen "0,..." bez oddelovace. */
    scpi_process_ctx(&x, &src, "SYST:ERR:ALL?", b, sizeof b);
    ok &= (strncmp(b, "0,", 2) == 0 && strchr(b, ';') == NULL);

    /* Chybová fronta + izolace session. */
    scpi_ctx_init(&x);
    { size_t nn = scpi_process_ctx(&x, &src, "FOO:BAR?", b, sizeof b); ok &= (nn > 0 && b[0] == '-'); }
    scpi_process_ctx(&x, &src, "*ID?", b, sizeof b);             ok &= (b[0] == '-');
    scpi_process_ctx(&x, &src, "?", b, sizeof b);                ok &= (b[0] == '-');
    scpi_process_ctx(&x, &src, "SYST:TEMPXY?", b, sizeof b);     ok &= (b[0] == '-');
    { size_t nn = scpi_process_ctx(&x, &src, "SYST:ERR", b, sizeof b); ok &= (nn > 0 && b[0] == '-'); }
    scpi_process_ctx(&x, &src, "SYST:ERR?", b, sizeof b);        ok &= (strncmp(b, "-113,", 5) == 0);
    scpi_ctx_init(&x);
    scpi_process_ctx(&x, &src, "SYST:ERR?", b, sizeof b);        ok &= (strncmp(b, "0,", 2) == 0);
    {
        scpi_ctx_t a, c2; scpi_ctx_init(&a); scpi_ctx_init(&c2);
        scpi_process_ctx(&a,  &src, "BOGUS?", b, sizeof b);
        scpi_process_ctx(&c2, &src, "SYST:ERR:COUN?", b, sizeof b); ok &= (b[0] == '0');
        scpi_process_ctx(&a,  &src, "SYST:ERR:COUN?", b, sizeof b); ok &= (b[0] == '1');
    }

    /* Parsování argumentů + jednotky. */
    { int q; double v = scpi_num("1e6", &q);    ok &= (q && v > 999999.0 && v < 1000001.0); }
    { int q; double v = scpi_num("-2.5", &q);   ok &= (q && v < -2.499 && v > -2.501); }
    { int q; double v = scpi_num("1.5E-3", &q); ok &= (q && v > 0.00149 && v < 0.00151); }
    { int q; scpi_num("abc", &q);               ok &= (q == 0); }
    { int q; ok &= (scpi_bool("ON", &q) == 1 && q); }
    { int q; ok &= (scpi_bool("off", &q) == 0 && q); }
    { int q; scpi_bool("maybe", &q);            ok &= (q == 0); }
    { int q; double v = scpi_num("100KHZ", &q); ok &= (q && v > 99999.0 && v < 100001.0); }
    { int q; double v = scpi_num("10.1MHZ", &q);ok &= (q && v > 10.09e6 && v < 10.11e6); }
    { int q; double v = scpi_num("1GHZ", &q);   ok &= (q && v > 0.999e9 && v < 1.001e9); }

    /* CALC parse + apply (nad lokální cfg — abstrakce zdroje: klíč místo hlavičky). */
    {
        meas_cfg_t c; meas_math_defaults(&c);
        uint8_t key; uint32_t vu = 0; double vd = 0; int err = 1;
        ok &= (scpi_calc_parse("CALCulate:MATH:M", "2", &key, &vu, &vd, &err) == 1 && err == 0 && key == SCPI_CFG_MATH_M && vd > 1.99 && vd < 2.01);
        ok &= (scpi_calc_parse("CALC:MATH:STAT", "ON", &key, &vu, &vd, &err) == 1 && err == 0 && key == SCPI_CFG_MATH_EN && vu == 1);
        ok &= (scpi_calc_parse("CALC:LIM:UPP", "10.1MHZ", &key, &vu, &vd, &err) == 1 && err == 0 && key == SCPI_CFG_LIM_HI && vd > 1.009e7);
        scpi_calc_parse("CALC:MATH:M", "xyz", &key, &vu, &vd, &err); ok &= (err == 1);
        ok &= (scpi_calc_parse("CALC:BOGUS:X", "1", &key, &vu, &vd, &err) == 0);
        ok &= (scpi_cfg_apply(&c, SCPI_CFG_MATH_M, 0, 2.0, 0) == 1 && c.m > 1.99 && c.m < 2.01);
        ok &= (scpi_cfg_apply(&c, SCPI_CFG_MATH_B, 0, 100.0, 0) == 1 && c.b > 99.9 && c.b < 100.1);
        ok &= (scpi_cfg_apply(&c, SCPI_CFG_MATH_EN, 1, 0, 0) == 1 && c.math_en == 1);
        ok &= (scpi_cfg_apply(&c, SCPI_CFG_LIM_LO, 0, 9.9e6, 0) == 1 && c.lo > 9.8e6 && c.lo < 10.0e6);
        ok &= (scpi_cfg_apply(&c, SCPI_CFG_LIM_HI, 0, 1.01e7, 0) == 1);
        ok &= (scpi_cfg_apply(&c, SCPI_CFG_LIM_EN, 1, 0, 0) == 1 && c.limit_en == 1);
        ok &= (scpi_cfg_apply(&c, 0xEE, 0, 0, 0) == 0);                        /* neznámý klíč */
        ok &= (scpi_cfg_apply(&c, SCPI_CFG_NULL_ACQ, 0, 0, 0.0) == 0);         /* bez freq = nic */

        /* Absolutni vetev — jeste PRED zachycenim NULL. */
        double y = meas_math_apply(&c, 1e7);                                    /* 2*1e7+100 */
        ok &= (y > 20000099.0 && y < 20000101.0);
        ok &= (meas_limit_eval(&c, y) == MEAS_HI);                              /* nad hi = 10,1 MHz */

        /* ⚠️ Relativni vetev. `NULL:ACQuire` NEJEN zachyti referenci, ale rovnou
         * ZAPNE relativni rezim (`meas_math_capture_null` dela `null_en = 1`) —
         * proto po nem Y klesne na nulu. Puvodni verze testu tady cekala porad
         * 2*1e7+100 a padala (to byl ten "SCPI parser FAIL" pri startu);
         * chyba byla v ocekavani testu, ne v parseru. */
        ok &= (scpi_cfg_apply(&c, SCPI_CFG_NULL_ACQ, 0, 0, 1e7) == 1);
        ok &= (c.null_en == 1 && c.null_ref > 20000099.0 && c.null_ref < 20000101.0);
        double yr = meas_math_apply(&c, 1e7);
        ok &= (yr > -1e-6 && yr < 1e-6);                                        /* sam se sebou = 0 */
        ok &= (meas_limit_eval(&c, yr) == MEAS_LO);                             /* 0 je pod lo = 9,9 MHz */
        ok &= (meas_math_apply(&c, 1e7 + 5.0) > 9.99 && meas_math_apply(&c, 1e7 + 5.0) < 10.01);  /* +5 Hz -> +10 (M=2) */
    }

    /* SET přes handler (dummy src.set_cfg → src.meas) + readback ve stejné cestě. */
    scpi_ctx_init(&x); meas_math_defaults(&src.meas);
    ok &= (scpi_process_ctx(&x, &src, "CALC:MATH:M 2", b, sizeof b) == 0);       /* akce = ticho */
    scpi_process_ctx(&x, &src, "CALC:MATH:M?", b, sizeof b);  ok &= (strncmp(b, "2.", 2) == 0);
    scpi_process_ctx(&x, &src, "CALC:MATH:M xyz", b, sizeof b); ok &= (strncmp(b, "-224", 4) == 0);  /* chybný arg */

    /* IEEE 488.2 status model + compound.
     * ⚠️ POZOR NA `ok &= (vyraz & MASKA)`: akumulator `ok` se sluce BITOVYM AND
     * a startuje na 1, takze `1 & 0x04` = 0 -> assert by shodil vysledek i pri
     * SPLNENE podmince. Testy nad bitovymi maskami proto MUSI mit `!= 0`.
     * Presne tohle drzelo "SCPI parser FAIL" i po oprave NULL:ACQ. */
    scpi_ctx_init(&x);
    scpi_process_ctx(&x, &src, "FOO?", b, sizeof b);
    scpi_process_ctx(&x, &src, "SYST:ERR:COUN?", b, sizeof b); ok &= (b[0] == '1');
    scpi_process_ctx(&x, &src, "*STB?", b, sizeof b);          ok &= ((atoi(b) & 0x04) != 0);
    scpi_process_ctx(&x, &src, "*ESR?", b, sizeof b);          ok &= ((atoi(b) & 0x20) != 0);
    scpi_process_ctx(&x, &src, "*ESR?", b, sizeof b);          ok &= (atoi(b) == 0);
    scpi_process_ctx(&x, &src, "SYST:ERR:NEXT?", b, sizeof b); ok &= (strncmp(b, "-113,", 5) == 0);
    scpi_process_ctx(&x, &src, "*ESE 32", b, sizeof b);
    scpi_process_ctx(&x, &src, "*ESE?", b, sizeof b);          ok &= (atoi(b) == 32);
    scpi_process_ctx(&x, &src, "CALC:MATH:M xyz", b, sizeof b);
    scpi_process_ctx(&x, &src, "*ESR?", b, sizeof b);          ok &= ((atoi(b) & 0x10) != 0);
    scpi_process_ctx(&x, &src, "*CLS", b, sizeof b);
    scpi_process_ctx(&x, &src, "*ESR?", b, sizeof b);          ok &= (atoi(b) == 0);
    scpi_process_ctx(&x, &src, "SYST:ERR:COUN?", b, sizeof b); ok &= (b[0] == '0');
    ok &= (scpi_process_ctx(&x, &src, "MEAS:FREQ:ALL?", b, sizeof b) > 0 && strchr(b, ',') != NULL);

    scpi_process_ctx(&x, &src, "*CLS", b, sizeof b);
    scpi_process_ctx(&x, &src, "*OPC", b, sizeof b);
    scpi_process_ctx(&x, &src, "*ESR?", b, sizeof b);          ok &= ((atoi(b) & 0x01) != 0);
    ok &= (scpi_process_ctx(&x, &src, "*WAI", b, sizeof b) == 0);
    ok &= (scpi_process_ctx(&x, &src, "*TST?", b, sizeof b) > 0);

    ok &= (scpi_process_ctx(&x, &src, "SYST:TEMP? BOARD", b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "FETC:FREQ?",       b, sizeof b) > 0);
    ok &= (scpi_process_ctx(&x, &src, "STAT:QUES:COND?",  b, sizeof b) > 0);

    scpi_ctx_init(&x);
    scpi_process_ctx(&x, &src, "*ESE 24;*ESE?", b, sizeof b);  ok &= (strcmp(b, "24") == 0);
    scpi_process_ctx(&x, &src, "*ESE?;*ESE?",   b, sizeof b);  ok &= (strcmp(b, "24;24") == 0);
    scpi_process_ctx(&x, &src, "*ESE 8;*ESE?",  b, sizeof b);  ok &= (strcmp(b, "8") == 0);
    #undef ok

    /* Posledni assert uz zadny dalsi `scpi_st_chk` nenasleduje -> dovyhodnotit. */
    if (!s_st_ok && !s_st_fail_line) s_st_fail_line = s_st_line;
    return s_st_ok;
}
