/**
 * @file    si5356.c
 * @brief   Si5356A clock generator driver (I2C1 0x70). Viz si5356.h.
 */
#include "si5356.h"
#include <stdio.h>

#define ADDR        SI5356_I2C_ADDR
#define REG_PAGE    0xFF        /* 0x00 = page 0, 0x01 = page 1 */
#define REG_OEB_ALL 0xE6        /* bit4 (0x10) = OEB_ALL: 1 = vsechny vystupy OFF */
#define REG_E2      0xE2        /* apply pulse (bit2 0x04) */
#define REG_SOFTRST 0xF6        /* bit1 (0x02) = SOFT_RESET */
#define REG_STATUS  0xDA        /* 218 (AN565): bit0 SYS_CAL, bit2 LOS_XTAL (bez
                                 * krystalu trvale 1 — XA/XB uzemnene), bit3
                                 * LOS_CLKIN (ztrata 10 MHz, pin 4), bit4 PLL_LOL.
                                 * ⚠️ drivejsi komentar "bit2=LOS_CLKIN" byl spatne. */

typedef struct { uint8_t addr; uint8_t val; uint8_t mask; } si_reg_t;

/* === Register map (ClockBuilder Pro export, AN428/JumpStart) ===
 * Zdroj: Si5356-RevB-Registers.h (CBPro 4.19, 2026-10-10). Vstup 10 MHz,
 * VCO 2,2 GHz (N=220), vsechny vystupy 3,3 V CMOS, BEZ fazovych offsetu:
 *   CLK0/1 = 100 MHz  (MS 22)      - REFERENCE FPGA, pevne
 *   CLK2/3 =  50 MHz  (MS 44)      vystupy na konektory, meni je okno
 *   CLK4/5 = 150 MHz  (MS 14 2/3)  GENERATOR (viz si5356_gen_* nize)
 *   CLK6/7 = 200 MHz  (MS 11)
 * (Do 2026-10-10 tu bylo 4x 100 MHz s fazemi 0/90/180/270 pro 4fazovy vernier;
 * ten nahradil carry-chain TDC ve FPGA, faze uz nikdo nepouziva.)
 * 2026-10-11 (FPGA FW 0x0423): CLK0/1 zpet 125 -> 100 MHz. Export CBPro byl pro 125 MHz
 * (MS A 17 3/5: P1 0x6CC, P2 4, P3 5); pro 100 MHz se lisi JEN MS A = registry 53..62:
 * P1 = 128*22 - 512 = 2304 = 0x900, P2 = 0, P3 = 1 (shodne s puvodnim exportem 4x 100 MHz).
 *
 * Adresy DECIMALNE (1:1 s CBPro exportem - usnadnuje budouci re-sync; diff proti
 * "C Code Header" z CBPro). Polozky s mask==0x00 z exportu jsou VYNECHANY (CBPro
 * "do not write" - read-only/rezervovane). mask 0xFF = primy zapis, mask<0xFF =
 * read-modify-write (wr_masked zachova nemaskovane bity). Page se prepina reg 255.
 * Pri zmene konfigurace: znovu exportuj z CBPro a nahrad tuto tabulku (vynech mask 0). */
static const si_reg_t REGMAP[] = {
  {  6,0x04,0x1D}, { 27,0x70,0x80}, { 28,0x37,0xFF}, { 29,0x20,0xFF}, { 30,0xA8,0xFF},
  { 31,0xC0,0xFF}, { 32,0xC0,0xFF}, { 33,0xC0,0xFF}, { 34,0xC0,0xFF}, { 35,0x00,0xFF},
  { 36,0x13,0x1F}, { 37,0x13,0x1F}, { 38,0x13,0x1F}, { 39,0x13,0x1F}, { 40,0xF7,0xFF},
  { 41,0x5E,0x7F}, { 42,0x37,0x3F}, { 47,0x14,0x3C}, { 48,0x31,0x7F}, { 49,0x95,0x7F},
  { 50,0xDE,0xC0}, { 52,0x10,0x0C}, { 53,0x00,0xFF}, { 54,0x09,0xFF}, { 55,0x00,0xFF},
  { 56,0x00,0xFF}, { 57,0x00,0xFF}, { 58,0x00,0xFF}, { 59,0x01,0xFF}, { 60,0x00,0xFF},
  { 61,0x00,0xFF}, { 62,0x00,0x3F}, { 63,0x10,0x0C}, { 64,0x00,0xFF}, { 65,0x14,0xFF},
  { 66,0x00,0xFF}, { 67,0x00,0xFF}, { 68,0x00,0xFF}, { 69,0x00,0xFF}, { 70,0x01,0xFF},
  { 71,0x00,0xFF}, { 72,0x00,0xFF}, { 73,0x00,0x3F}, { 74,0x10,0x0C}, { 75,0x55,0xFF},
  { 76,0x05,0xFF}, { 77,0x04,0xFF}, { 78,0x00,0xFF}, { 79,0x00,0xFF}, { 80,0x00,0xFF},
  { 81,0x03,0xFF}, { 82,0x00,0xFF}, { 83,0x00,0xFF}, { 84,0x00,0x3F}, { 85,0x10,0x0C},
  { 86,0x80,0xFF}, { 87,0x03,0xFF}, { 88,0x00,0xFF}, { 89,0x00,0xFF}, { 90,0x00,0xFF},
  { 91,0x00,0xFF}, { 92,0x01,0xFF}, { 93,0x00,0xFF}, { 94,0x00,0xFF}, { 95,0x00,0x3F},
  { 97,0x00,0xFF}, { 98,0x6C,0xFF}, { 99,0x00,0xFF}, {100,0x00,0xFF}, {101,0x00,0xFF},
  {102,0x00,0xFF}, {103,0x01,0xFF}, {104,0x00,0xFF}, {105,0x00,0xFF}, {106,0x80,0x3F},
  {107,0x00,0xFF}, {108,0x00,0x7F}, {110,0x40,0xC0}, {111,0x00,0xFF}, {112,0x00,0x7F},
  {114,0x40,0xC0}, {115,0x00,0xFF}, {116,0x80,0x7F}, {118,0x40,0xC0}, {119,0x00,0xFF},
  {120,0x00,0xFF}, {122,0x40,0xC0}, {129,0x00,0x0F}, {130,0x00,0x0F}, {144,0x00,0x80},
  {158,0x00,0x0F}, {159,0x00,0x0F}, {181,0x00,0x0F}, {203,0x00,0x0F}, {230,0x00,0xFF},
  /* --- PAGE 1 --- */
  {REG_PAGE,0x01,0xFF},
  { 31,0x00,0xFF}, { 32,0x00,0xFF}, { 33,0x01,0xFF}, { 34,0x00,0xFF}, { 35,0x00,0xFF},
  { 36,0x90,0xFF}, { 37,0x31,0xFF}, { 38,0x00,0xFF}, { 39,0x00,0xFF}, { 40,0x01,0xFF},
  { 41,0x00,0xFF}, { 42,0x00,0xFF}, { 43,0x00,0x0F}, { 47,0x00,0xFF}, { 48,0x00,0xFF},
  { 49,0x01,0xFF}, { 50,0x00,0xFF}, { 51,0x00,0xFF}, { 52,0x90,0xFF}, { 53,0x31,0xFF},
  { 54,0x00,0xFF}, { 55,0x00,0xFF}, { 56,0x01,0xFF}, { 57,0x00,0xFF}, { 58,0x00,0xFF},
  { 59,0x00,0x0F}, { 63,0x00,0xFF}, { 64,0x00,0xFF}, { 65,0x01,0xFF}, { 66,0x00,0xFF},
  { 67,0x00,0xFF}, { 68,0x90,0xFF}, { 69,0x31,0xFF}, { 70,0x00,0xFF}, { 71,0x00,0xFF},
  { 72,0x01,0xFF}, { 73,0x00,0xFF}, { 74,0x00,0xFF}, { 75,0x00,0x0F}, { 79,0x00,0xFF},
  { 80,0x00,0xFF}, { 81,0x00,0xFF}, { 82,0x00,0xFF}, { 83,0x00,0xFF}, { 84,0x90,0xFF},
  { 85,0x31,0xFF}, { 86,0x00,0xFF}, { 87,0x00,0xFF}, { 88,0x01,0xFF}, { 89,0x00,0xFF},
  { 90,0x00,0xFF}, { 91,0x00,0x0F},
  /* --- PAGE 0 --- */
  {REG_PAGE,0x00,0xFF},
};
#define REGMAP_N  (sizeof(REGMAP) / sizeof(REGMAP[0]))

static bool wr(I2C_HandleTypeDef *h, uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(h, ADDR, reg, I2C_MEMADD_SIZE_8BIT, &val, 1, 50) == HAL_OK;
}
static bool rd(I2C_HandleTypeDef *h, uint8_t reg, uint8_t *val)
{
    return HAL_I2C_Mem_Read(h, ADDR, reg, I2C_MEMADD_SIZE_8BIT, val, 1, 50) == HAL_OK;
}
static bool wr_masked(I2C_HandleTypeDef *h, uint8_t reg, uint8_t val, uint8_t mask)
{
    if (mask == 0x00) return true;          /* CBPro "do not write" - preskocit */
    if (mask == 0xFF) return wr(h, reg, val);
    uint8_t cur;
    if (!rd(h, reg, &cur)) return false;
    cur = (uint8_t)((cur & ~mask) | (val & mask));
    return wr(h, reg, cur);
}

/* ── GENERATOR: volne nastavitelne vystupy CLK2/3, CLK4/5, CLK6/7 ─────────────
 * CLK0/1 = 100 MHz reference FPGA a MENI SE JEN pres REGMAP (nikdy odtud).
 * Kazda dvojice ma vlastni MultiSynth (MS B/C/D) + R delic; VCO je spolecny
 * (10 MHz x N=220 = 2,2 GHz) a nemeni se, takze je kazda dvojice nezavisla.
 *
 * Vypocet je PRESNY (zadne zaokrouhlovani): pomer VCO/fms je racionalni cislo
 *   128 * VCO / fms = q + rem/fms,   P1 = q - 512,   P2/P3 = rem/fms (zkraceno),
 * a protoze fms <= 200 MHz < 2^30, vejde se jmenovatel P3 do 30 bitu. Pro 100,
 * 125, 150, 50 a 200 MHz vychazi TYZ P1/P2/P3 jako v exportech CBPro (kontrola v
 * docs/audit/2026-10-10_si5356-generator.md). Registry: P1 B+0,B+1,B+2[1:0];
 * P2 B+2[7:2],B+3..B+5; P3 B+6..B+8,B+9[5:0] (AN565, overeno proti exportu).
 *
 * 🔴 Zmena BEZ soft resetu: SOFT_RESET (reg 246) shazuje VSECHNY vystupy vcetne
 * CLK0/1 = casova zakladna FPGA. Proto se meni jen jedna dvojice: vystup se
 * zablokuje bitem OEB_x (reg 230), prepisou se R + MS a OEB_x se uvolni. */
#define GEN_MS_BASE(d)   (uint8_t)(53u + 11u * (d))      /* MS A=53, B=64, C=75, D=86 */
#define GEN_R_REG(d)     (uint8_t)(31u + (d))            /* RADIV..RDDIV */
#define GEN_OEB(d)       (uint8_t)(1u << (d))            /* reg 230 bit0..3 */
#define GEN_R_IN_MS      0xC0u                           /* RxDIV_IN = 110 (vlastni MS) */

volatile si5356_gen_t g_si_gen[SI5356_GEN_N] = {
    { 50000000u,  1u, 0u, SI5356_GEN_OK },     /* CLK2/3 */
    { 150000000u, 1u, 0u, SI5356_GEN_OK },     /* CLK4/5 */
    { 200000000u, 1u, 0u, SI5356_GEN_OK },     /* CLK6/7 */
};

/* Stav po (re)inicializaci cipu = to, co zapsal REGMAP (viz export CBPro). */
void si5356_gen_defaults(void)
{
    static const uint32_t def_hz[SI5356_GEN_N] = { 50000000u, 150000000u, 200000000u };
    for (unsigned i = 0; i < SI5356_GEN_N; i++) {
        g_si_gen[i].freq_hz = def_hz[i];
        g_si_gen[i].on      = 1u;
        g_si_gen[i].pend    = 0u;
        g_si_gen[i].state   = SI5356_GEN_OK;
    }
}

static uint32_t gcd32(uint32_t a, uint32_t b)
{
    while (b != 0u) { uint32_t t = a % b; a = b; b = t; }
    return a;
}

bool si5356_gen_calc(uint32_t f_hz, si5356_ms_t *m)
{
    if (f_hz < SI5356_GEN_F_MIN_HZ || f_hz > SI5356_GEN_F_MAX_HZ) return false;
    uint8_t rexp = 0;
    while (((uint64_t)f_hz << rexp) < SI5356_MS_F_MIN_HZ) rexp++;    /* nejvyse 5 */
    const uint64_t fms = (uint64_t)f_hz << rexp;                     /* 5..200 MHz */
    const uint64_t num = (uint64_t)SI5356_VCO_HZ * 128u;
    const uint64_t q   = num / fms;
    const uint32_t rem = (uint32_t)(num % fms);                      /* < fms < 2^30 */
    const uint32_t den = (uint32_t)fms;
    uint32_t p2 = 0u, p3 = 1u;
    if (rem != 0u) {
        const uint32_t g = gcd32(rem, den);
        p2 = rem / g;
        p3 = den / g;
    }
    m->rexp = rexp;
    m->p1   = (uint32_t)q - 512u;
    m->p2   = p2;
    m->p3   = p3;
    return true;
}

/* Kmitocet [Hz], ktery cip z techto parametru skutecne vyrobi (zpetny prepocet
 * pro kontrolu v `gen`): 128*VCO / ((P1+512) + P2/P3) / R. `double` staci
 * (vysledek < 2^32, relativni chyba ~1e-16); cely 64b zlomek by pretekl. */
uint32_t si5356_gen_actual_hz(const si5356_ms_t *m)
{
    const double d = ((double)m->p1 + 512.0) * (double)m->p3 + (double)m->p2;
    const double f = (double)SI5356_VCO_HZ * 128.0 * (double)m->p3 / d;
    return (uint32_t)(f / (double)(1u << m->rexp) + 0.5);
}

void si5356_gen_pack(const si5356_ms_t *m, uint8_t b[10])
{
    b[0] = (uint8_t)(m->p1);
    b[1] = (uint8_t)(m->p1 >> 8);
    b[2] = (uint8_t)(((m->p2 & 0x3Fu) << 2) | ((m->p1 >> 16) & 0x03u));
    b[3] = (uint8_t)(m->p2 >> 6);
    b[4] = (uint8_t)(m->p2 >> 14);
    b[5] = (uint8_t)(m->p2 >> 22);
    b[6] = (uint8_t)(m->p3);
    b[7] = (uint8_t)(m->p3 >> 8);
    b[8] = (uint8_t)(m->p3 >> 16);
    b[9] = (uint8_t)((m->p3 >> 24) & 0x3Fu);
}

bool si5356_gen_apply(I2C_HandleTypeDef *hi2c, unsigned ch, uint32_t f_hz, bool on)
{
    if (ch >= SI5356_GEN_N) return false;
    si5356_ms_t m;
    if (!si5356_gen_calc(f_hz, &m)) return false;
    const unsigned d = ch + 1u;                          /* driver B/C/D (A = reference) */
    uint8_t b[10];
    si5356_gen_pack(&m, b);

    bool ok = true;
    ok &= wr_masked(hi2c, REG_OEB_ALL, GEN_OEB(d), GEN_OEB(d));      /* vystup OFF */
    ok &= wr(hi2c, GEN_R_REG(d), (uint8_t)(GEN_R_IN_MS | (m.rexp << 2)));
    for (unsigned i = 0; i < 9u; i++) ok &= wr(hi2c, (uint8_t)(GEN_MS_BASE(d) + i), b[i]);
    ok &= wr_masked(hi2c, (uint8_t)(GEN_MS_BASE(d) + 9u), b[9], 0x3Fu);
    if (on) ok &= wr_masked(hi2c, REG_OEB_ALL, 0x00u, GEN_OEB(d));  /* vystup ON */
    return ok;
}

void si5356_gen_request(unsigned ch, uint32_t f_hz, bool on)
{
    if (ch >= SI5356_GEN_N) return;
    g_si_gen[ch].freq_hz = f_hz;
    g_si_gen[ch].on      = on ? 1u : 0u;
    __DMB();
    g_si_gen[ch].pend    = 1u;       /* az POSLEDNI: obsluha vidi uz hotove hodnoty */
}

/* Obsluha zadosti — vola VYHRADNE SensorsTask (vlastnik I2C1) pod i2c1Mutex. */
void si5356_gen_service(I2C_HandleTypeDef *hi2c)
{
    for (unsigned ch = 0; ch < SI5356_GEN_N; ch++) {
        if (!g_si_gen[ch].pend) continue;
        const uint32_t f  = g_si_gen[ch].freq_hz;
        const bool     on = g_si_gen[ch].on != 0u;
        g_si_gen[ch].pend = 0u;          /* nova zadost behem zapisu ji nastavi znovu */
        si5356_ms_t probe;
        if (!si5356_gen_calc(f, &probe)) { g_si_gen[ch].state = SI5356_GEN_ERR_RANGE; continue; }
        g_si_gen[ch].state = si5356_gen_apply(hi2c, ch, f, on) ? SI5356_GEN_OK : SI5356_GEN_ERR_I2C;
    }
}

bool si5356_gen_readback(I2C_HandleTypeDef *hi2c, unsigned ch, uint8_t regs[12])
{
    if (ch >= SI5356_GEN_N) return false;
    const unsigned d = ch + 1u;
    bool ok = rd(hi2c, GEN_R_REG(d), &regs[0]);
    for (unsigned i = 0; i < 10u; i++) ok &= rd(hi2c, (uint8_t)(GEN_MS_BASE(d) + i), &regs[1 + i]);
    ok &= rd(hi2c, REG_OEB_ALL, &regs[11]);
    return ok;
}

bool si5356_read_sticky(I2C_HandleTypeDef *hi2c, uint8_t *sticky)
{
    /* ⚠️ Bez prepinani stranky — stejne jako `si5356_read_status`: cip zustava
     * po `si5356_init` na page 0 a cteni 218 tak funguje prokazatelne (status
     * `0x04` na desce). 247 je na teze strance, takze se drzi tehoz vzoru. */
    return rd(hi2c, SI5356_REG_STICKY, sticky);
}

bool si5356_clear_sticky(I2C_HandleTypeDef *hi2c, uint8_t mask)
{
    uint8_t v;
    if (!rd(hi2c, SI5356_REG_STICKY, &v)) return false;
    /* AN565: sticky bit se maze ZAPISEM NULY. Bity mimo `mask` zapisujeme
     * nezmenene (tedy 1, kdyz jsou nastavene), takze zustanou platne. */
    return wr(hi2c, SI5356_REG_STICKY, (uint8_t)(v & (uint8_t)~mask));
}

bool si5356_read_status(I2C_HandleTypeDef *hi2c, uint8_t *status)
{
    return rd(hi2c, REG_STATUS, status);
}

bool si5356_init(I2C_HandleTypeDef *hi2c)
{
    uint8_t probe;
    if (!rd(hi2c, 0x00, &probe)) {
        printf("Si5356A: NACK na 0x70 (neni pripojen?)\n");
        return false;
    }

    /* 1) Zapsat kompletni register map (vc. prepinani stranek pres 0xFF) */
    bool ok = true;
    for (unsigned i = 0; i < REGMAP_N; i++)
        if (!wr_masked(hi2c, REGMAP[i].addr, REGMAP[i].val, REGMAP[i].mask)) ok = false;

    /* 2) Apply procedura (SiLabs / CBPro): vystupy off -> pulse -> soft reset -> on.
     * Navrat KAZDEHO kroku se scita do `ok` — hlavne posledni "OEB_ALL = 0" zapina
     * vystupy (CLK0/1 = 100 MHz reference); kdyby NACKnul, zustanou OFF, FPGA by nemel casovou zakladnu
     * a bez teto kontroly by init presto vratil OK (audit F-0113). */
    ok &= wr_masked(hi2c, REG_OEB_ALL, 0x10, 0x10);   /* OEB_ALL = 1 (vystupy OFF) */
    ok &= wr_masked(hi2c, REG_E2,      0x04, 0x04);
    ok &= wr_masked(hi2c, REG_E2,      0x00, 0x04);
    ok &= wr_masked(hi2c, REG_SOFTRST, 0x02, 0x02);   /* SOFT_RESET pulse */
    ok &= wr_masked(hi2c, REG_SOFTRST, 0x00, 0x02);
    HAL_Delay(25);                               /* cas na re-lock PLL po resetu */
    ok &= wr_masked(hi2c, REG_OEB_ALL, 0x00, 0x10);   /* OEB_ALL = 0 (vystupy ON) */

    si5356_gen_defaults();       /* cip ma ted vystupy z REGMAP, ne z okna Generator */

    uint8_t st = 0;
    rd(hi2c, REG_STATUS, &st);
    printf("Si5356A init: zapisy=%s, status218=0x%02X%s%s%s%s\n",
           ok ? "OK" : "CHYBA", st,
           (st & 0x01) ? " SYS_CAL" : "",
           (st & 0x04) ? " LOS_XTAL(bez krystalu, ok)" : "",
           (st & 0x08) ? " LOS_CLKIN!" : "",
           (st & 0x10) ? " PLL_LOL!" : "");
    return ok;
}
