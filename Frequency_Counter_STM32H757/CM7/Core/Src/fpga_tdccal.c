/* fpga_tdccal.c -- kalibracni tabulka TDC pocitana v STM (FW >= 0x0427). Viz fpga_tdccal.h.
 * GENEROVANO z fpga_tdccal.c.in (zlate vektory z FPGA simulace sim/tb_lutequiv.sv doplni gen_tdccal.py). */
#include "fpga_tdccal.h"
#include "fpga_freq.h"

uint64_t fpga_tdccal_lut(const uint32_t *hist, uint32_t nc, uint32_t log2n, uint8_t dual, uint16_t *lut)
{
    uint64_t cum = 0u;
    const uint32_t sh = (log2n > 13u) ? (log2n - 13u) : 0u;
    for (uint32_t a = 0; a < nc; a++) {
        const uint64_t h  = hist[a];
        const uint64_t xv = 2u * cum + h;                       /* (2*cum_pred + hist) */
        cum += h;
        uint64_t x = xv >> sh;
        if (x > 16383u) x = 16383u;                             /* saturace (x = 2N) */
        const uint16_t neg = (uint16_t)((0x4000u - (uint16_t)x) & 0x3FFFu);
        const uint32_t r   = dual ? (uint32_t)(((a >> (30u - (uint32_t)__builtin_clz(nc))) & 1u) == 0u) : 1u;   /* sada R (horni bit adresy 0) dostava +T */
        const uint32_t amt = r + ((x != 0u) ? 1u : 0u);
        lut[a] = (uint16_t)((amt << 14) | neg);
    }
    return cum;
}

void fpga_tdccal_stats(const uint32_t *hist, uint32_t nc, fpga_tdccal_stats_t *s)
{
    s->ovf = (nc != 0u) ? hist[nc - 1u] : 0u; s->peak = 0u; s->nz = 0u; s->last = 0u; s->total = 0u;
    for (uint32_t a = 0; a < nc; a++) {
        const uint32_t h = hist[a];
        s->total += h;
        if (h != 0u) { s->nz++; s->last = (uint16_t)a; if (h > s->peak) s->peak = h; }
    }
}

uint16_t fpga_tdccal_chk(uint8_t ch, uint16_t addr, uint16_t neg)
{
    return (uint16_t)((uint16_t)(((uint16_t)(ch & 1u) << 14) | (uint16_t)((addr & 0x1FFu) << 5)) ^ (uint16_t)(neg & 0x3FFFu));
}

static const uint16_t k_gold_hist[256] = {
    0, 0, 0, 128, 151, 146, 160, 159, 209, 207, 165, 148, 171, 138, 155, 160,
    165, 178, 238, 204, 145, 212, 187, 217, 266, 168, 134, 190, 122, 240, 206, 187,
    202, 232, 231, 196, 143, 159, 161, 193, 218, 186, 173, 225, 188, 116, 193, 186,
    123, 144, 223, 179, 216, 217, 295, 279, 255, 248, 266, 250, 295, 289, 299, 146,
    257, 262, 231, 244, 256, 202, 221, 208, 159, 246, 271, 185, 162, 164, 161, 178,
    267, 223, 192, 266, 163, 266, 173, 210, 292, 166, 248, 237, 194, 157, 215, 262,
    206, 229, 237, 186, 183, 237, 219, 158, 193, 179, 211, 210, 165, 158, 206, 242,
    205, 220, 227, 233, 210, 204, 247, 199, 215, 220, 252, 213, 208, 211, 265, 302,
    208, 230, 246, 273, 166, 232, 276, 235, 252, 223, 171, 209, 155, 239, 160, 309,
    249, 238, 251, 229, 317, 246, 271, 209, 271, 206, 193, 191, 260, 275, 108, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const uint16_t k_gold_lut[256] = {
    0x4000, 0x4000, 0x4000, 0xBFE0, 0xBF9B, 0xBF50, 0xBF04, 0xBEB4, 0xBE58, 0xBDF0, 0xBD93, 0xBD45, 0xBCF5, 0xBCA8, 0xBC5F, 0xBC10,
    0xBBBF, 0xBB69, 0xBB01, 0xBA92, 0xBA3B, 0xB9E2, 0xB97E, 0xB919, 0xB8A0, 0xB834, 0xB7E8, 0xB797, 0xB749, 0xB6EF, 0xB67F, 0xB61D,
    0xB5BC, 0xB54F, 0xB4DC, 0xB471, 0xB41C, 0xB3D1, 0xB381, 0xB328, 0xB2C1, 0xB25C, 0xB203, 0xB19F, 0xB138, 0xB0EC, 0xB09F, 0xB040,
    0xAFF3, 0xAFB0, 0xAF54, 0xAEF0, 0xAE8D, 0xAE21, 0xADA1, 0xAD11, 0xAC8C, 0xAC0E, 0xAB8D, 0xAB0C, 0xAA84, 0xA9F2, 0xA95F, 0xA8F0,
    0xA88B, 0xA809, 0xA78E, 0xA717, 0xA69A, 0xA628, 0xA5BE, 0xA553, 0xA4F7, 0xA492, 0xA411, 0xA39F, 0xA348, 0xA2F6, 0xA2A5, 0xA250,
    0xA1E1, 0xA167, 0xA0FF, 0xA08C, 0xA021, 0x9FB6, 0x9F48, 0x9EE8, 0x9E6B, 0x9DF8, 0x9D91, 0x9D18, 0x9CAC, 0x9C54, 0x9BF7, 0x9B80,
    0x9B0B, 0x9A9E, 0x9A2A, 0x99C0, 0x9964, 0x98FB, 0x9889, 0x982A, 0x97D3, 0x9776, 0x9714, 0x96AB, 0x964D, 0x95FC, 0x95A1, 0x9531,
    0x94C2, 0x9457, 0x93E8, 0x9375, 0x9306, 0x929E, 0x922E, 0x91BE, 0x9157, 0x90EA, 0x9074, 0x9000, 0x8F96, 0x8F2E, 0x8EB7, 0x8E29,
    0x8DA9, 0x8D3C, 0x8CC5, 0x8C43, 0x8BD5, 0x8B72, 0x8AF3, 0x8A73, 0x89F9, 0x8983, 0x8920, 0x88C1, 0x8866, 0x8804, 0x87A0, 0x872B,
    0x869F, 0x8625, 0x85AB, 0x8533, 0x84AB, 0x841E, 0x839D, 0x8325, 0x82AD, 0x8235, 0x81D2, 0x8172, 0x8101, 0x807B, 0x801B, 0x8001,
    0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001,
    0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001,
    0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001,
    0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001,
    0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001,
    0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001,
};


int fpga_tdccal_selftest(void)
{
    static uint32_t h[256], h20[256];
    static uint16_t lut[256];
    int ok = 1;
    for (int i = 0; i < 256; i++) { h[i] = k_gold_hist[i]; h20[i] = (uint32_t)k_gold_hist[i] << 5; }
    /* 1) zlate vektory z FPGA simulace (2^15 udalosti) */
    ok &= (fpga_tdccal_lut(h, 256u, 15u, 0u, lut) == (uint64_t)32768u);
    for (int i = 0; i < 256; i++) ok &= (lut[i] == k_gold_lut[i]);
    /* 2) tatez data v meritku vyroby (2^20): shift 7 misto 2 -> stejna tabulka */
    ok &= (fpga_tdccal_lut(h20, 256u, FPGA_TDC_CAL_LOG2, 0u, lut) == ((uint64_t)32768u << 5));
    for (int i = 0; i < 256; i++) ok &= (lut[i] == k_gold_lut[i]);
    /* 3) statistiky retezu */
    {   fpga_tdccal_stats_t s;
        fpga_tdccal_stats(h, 256u, &s);
        ok &= (s.nz == 156u && s.last == 158u && s.peak == 317u && s.ovf == 0u && s.total == 32768u);
    }
    /* 4) hranice: prazdny histogram (vse 0 -> x = 0 -> {1, 0}); DUAL: sada F (horni bit adresy 1) ma amt o 1 mensi */
    {   static uint32_t z[4] = { 0u, 0u, 0u, 0u }; static uint16_t zl[4];
        (void)fpga_tdccal_lut(z, 4u, 15u, 0u, zl);
        for (int i = 0; i < 4; i++) ok &= (zl[i] == 0x4000u);
        static uint32_t d[4] = { 16384u, 16384u, 16384u, 16384u }; static uint16_t dl[4];
        (void)fpga_tdccal_lut(d, 4u, 16u, 1u, dl);               /* 2^16 udalosti, DUAL: adresy 0,1 = sada R, 2,3 = sada F */
        ok &= ((dl[0] >> 14) == 2u && (dl[1] >> 14) == 2u && (dl[2] >> 14) == 1u + 0u && (dl[3] >> 14) == 1u + 0u);
    }
    /* 5) kontrolni slovo shodne s FPGA: ch=1, addr=0x1FF, neg=0x3FFF -> 0x4000 | 0x3FE0 ^ 0x3FFF */
    ok &= (fpga_tdccal_chk(1u, 0x1FFu, 0x3FFFu) == (uint16_t)((0x4000u | 0x3FE0u) ^ 0x3FFFu));
    ok &= (fpga_tdccal_chk(0u, 0u, 0u) == 0u);
    return ok;
}
