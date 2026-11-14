/* Host test generatoru Si5356 (si5356_gen_calc_frac, v0.42.13): zlomek Hz v mHz.
 * cc -std=c11 -O2 -Wall -Wextra -Itests/host/si5356_stub -ICM7/Core/Inc tests/host/t_si5356_gen.c -lm -o /tmp/t_si5356_gen && /tmp/t_si5356_gen
 * Kontroly:
 *  1) cele Hz (mhz = 0): P1/P2/P3/R TOTOZNE s puvodnim vypoctem (do v0.42.12) pro exporty CBPro
 *     a 300 000 kmitoctu v celem rozsahu -- dosavadni nastaveni se nesmi zmenit ani o bit;
 *  2) se zlomkem: skutecny kmitocet z P1/P2/P3 se lisi od zadaneho o < 1 uHz, P2 < P3 <= 2^30 - 1,
 *     P1 se vejde do 18 bitu;
 *  3) meze (200 MHz + zlomek, mhz > 999);
 *  4) si5356_gen_apply + si5356_gen_readback nad atrapou registru: zapsano = zakodovano, vystup ZAP. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../../CM7/Core/Src/si5356.c"

static int fails;
#define CK(c) do { if (!(c)) { printf("FAIL radek %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* ---- atrapa I2C: registry cipu (jen stranka 0 staci, gen nepise 0xFF) ---- */
static uint8_t s_reg[256];
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t dev, uint16_t reg, uint16_t sz,
                                    uint8_t *d, uint16_t n, uint32_t tmo)
{
    (void)h; (void)dev; (void)sz; (void)tmo;
    for (uint16_t i = 0; i < n; i++) s_reg[(reg + i) & 0xFFu] = d[i];
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t dev, uint16_t reg, uint16_t sz,
                                   uint8_t *d, uint16_t n, uint32_t tmo)
{
    (void)h; (void)dev; (void)sz; (void)tmo;
    for (uint16_t i = 0; i < n; i++) d[i] = s_reg[(reg + i) & 0xFFu];
    return HAL_OK;
}
void HAL_Delay(uint32_t ms) { (void)ms; }

/* ---- puvodni vypocet (v0.42.12, si5356.c) jako reference ---- */
static uint32_t ref_gcd32(uint32_t a, uint32_t b) { while (b != 0u) { uint32_t t = a % b; a = b; b = t; } return a; }
static int ref_calc(uint32_t f_hz, si5356_ms_t *m)
{
    if (f_hz < SI5356_GEN_F_MIN_HZ || f_hz > SI5356_GEN_F_MAX_HZ) return 0;
    uint8_t rexp = 0;
    while (((uint64_t)f_hz << rexp) < SI5356_MS_F_MIN_HZ) rexp++;
    const uint64_t fms = (uint64_t)f_hz << rexp;
    const uint64_t num = (uint64_t)SI5356_VCO_HZ * 128u;
    const uint64_t q   = num / fms;
    const uint32_t rem = (uint32_t)(num % fms);
    const uint32_t den = (uint32_t)fms;
    uint32_t p2 = 0u, p3 = 1u;
    if (rem != 0u) { const uint32_t g = ref_gcd32(rem, den); p2 = rem / g; p3 = den / g; }
    m->rexp = rexp; m->p1 = (uint32_t)q - 512u; m->p2 = p2; m->p3 = p3;
    return 1;
}

/* skutecny kmitocet v long double (x86: 64bit mantisa) */
static long double actual_ld(const si5356_ms_t *m)
{
    const long double d = ((long double)m->p1 + 512.0L) * (long double)m->p3 + (long double)m->p2;
    return (long double)SI5356_VCO_HZ * 128.0L * (long double)m->p3 / d / (long double)(1u << m->rexp);
}

static uint64_t rng_s = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17; return (uint32_t)(rng_s >> 11); }

static int same(const si5356_ms_t *a, const si5356_ms_t *b)
{
    return a->rexp == b->rexp && a->p1 == b->p1 && a->p2 == b->p2 && a->p3 == b->p3;
}

int main(void)
{
    si5356_ms_t a, b;

    /* 1) cele Hz: shoda s puvodnim vypoctem */
    static const uint32_t cb[] = { 100000000u, 125000000u, 150000000u, 50000000u, 200000000u,
                                   10000000u, 10000040u, 1000000u, 1000001u, 4999999u, 5000000u, 30000000u };
    for (unsigned i = 0; i < sizeof cb / sizeof cb[0]; i++) {
        CK(ref_calc(cb[i], &a) && si5356_gen_calc_frac(cb[i], 0u, &b) && same(&a, &b));
    }
    CK(si5356_gen_calc_frac(150000000u, 0u, &b) && b.p1 == 1365u && b.p2 == 1u && b.p3 == 3u);   /* MS 14 2/3 (CBPro) */
    unsigned nd = 0;
    for (unsigned i = 0; i < 300000u; i++) {
        const uint32_t f = SI5356_GEN_F_MIN_HZ + rnd() % (SI5356_GEN_F_MAX_HZ - SI5356_GEN_F_MIN_HZ + 1u);
        if (!ref_calc(f, &a) || !si5356_gen_calc_frac(f, 0u, &b) || !same(&a, &b)) nd++;
    }
    CK(nd == 0u);
    printf("1) cele Hz: 300 000 nahodnych + %u pevnych kmitoctu, rozdil proti puvodnimu vypoctu %u\n",
           (unsigned)(sizeof cb / sizeof cb[0]), nd);

    /* 2) zlomek: presnost a meze poli */
    long double emax = 0.0L; unsigned bad = 0, approx = 0;
    for (unsigned i = 0; i < 300000u; i++) {
        uint32_t f = SI5356_GEN_F_MIN_HZ + rnd() % (SI5356_GEN_F_MAX_HZ - SI5356_GEN_F_MIN_HZ);
        const uint16_t mhz = (uint16_t)(rnd() % 1000u);
        if (!si5356_gen_calc_frac(f, mhz, &b)) { bad++; continue; }
        const long double want = (long double)f + (long double)mhz / 1000.0L;
        const long double e = fabsl(actual_ld(&b) - want);
        if (e > emax) emax = e;
        if (e > 1e-6L) bad++;
        if (!(b.p2 < b.p3 && b.p3 <= SI5356_MS_P3_MAX && b.p1 < (1u << 18))) bad++;
        if (e > 1e-12L * want) approx++;
    }
    CK(bad == 0u);
    printf("2) zlomek: 300 000 nahodnych, chyb %u, nejvetsi odchylka %.3e Hz, aproximovanych (> 1e-12) %u\n",
           bad, (double)emax, approx);

    /* pripad pouziti: 10 000 000,040 Hz */
    CK(si5356_gen_calc_frac(10000000u, 40u, &b));
    const uint64_t uhz = si5356_gen_actual_uhz(&b);
    printf("   10 000 000,040 Hz: R=%u P1=%u P2=%u P3=%u -> %.9f Hz (gen_actual_uhz %llu)\n",
           1u << b.rexp, b.p1, b.p2, b.p3, (double)actual_ld(&b), (unsigned long long)uhz);
    CK(uhz == 10000000040000ull);
    CK(fabsl(actual_ld(&b) - 10000000.040L) < 1e-7L);
    CK(si5356_gen_calc_frac(1000000u, 1u, &b) && fabsl(actual_ld(&b) - 1000000.001L) < 1e-7L);   /* R = 8 */
    CK(b.rexp == 3u);

    /* 3) meze */
    CK(!si5356_gen_calc_frac(200000000u, 1u, &b));
    CK(si5356_gen_calc_frac(199999999u, 999u, &b));
    CK(!si5356_gen_calc_frac(10000000u, 1000u, &b));
    CK(!si5356_gen_calc_frac(999999u, 999u, &b));

    /* 4) zapis do atrapy cipu a kontrolni cteni (jako `gen`) */
    I2C_HandleTypeDef h = {0};
    for (unsigned ch = 0; ch < SI5356_GEN_N; ch++) {
        memset(s_reg, 0, sizeof s_reg);
        s_reg[230] = 0x0Fu;                                  /* vsechny OEB nastavene */
        CK(si5356_gen_apply(&h, ch, 10000000u + ch, (uint16_t)(40u + ch), true));
        uint8_t want[10], got[12];
        CK(si5356_gen_calc_frac(10000000u + ch, (uint16_t)(40u + ch), &b));
        si5356_gen_pack(&b, want);
        CK(si5356_gen_readback(&h, ch, got));
        int ok = got[0] == (uint8_t)(0xC0u | ((unsigned)b.rexp << 2));
        for (unsigned i = 0; i < 9u; i++) ok = ok && got[1 + i] == want[i];
        ok = ok && (got[10] & 0x3Fu) == want[9];
        CK(ok);
        CK((got[11] & (1u << (ch + 1u))) == 0u);           /* vystup ZAP */
        CK((got[11] & 1u) == 1u);                           /* reference CLK0/1 (OEB_A) netknuta */
    }
    /* zadost se zlomkem projde obsluhou */
    si5356_gen_request_frac(0u, 10000000u, 40u, true);
    si5356_gen_service(&h);
    CK(g_si_gen[0].pend == 0u && g_si_gen[0].state == SI5356_GEN_OK && g_si_gen[0].mhz == 40u);
    si5356_gen_request(0u, 10000000u, true);                /* cele Hz zlomek vynuluje */
    CK(g_si_gen[0].mhz == 0u);

    printf("%s (%d chyb)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
