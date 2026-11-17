/* fpga_crc.c -- viz fpga_crc.h. Audit 2026-10-20, optimalizace 6. */
#include "fpga_crc.h"
#include <stddef.h>

static const uint16_t k_nib[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF };

uint16_t fpga_crc16_sw(const uint8_t *d, int n)
{
    uint16_t c = 0xFFFFu;
    for (int i = 0; i < n; i++) {
        c = (uint16_t)((c << 4) ^ k_nib[(c >> 12) ^ (d[i] >> 4)]);
        c = (uint16_t)((c << 4) ^ k_nib[(c >> 12) ^ (d[i] & 0x0Fu)]);
    }
    return c;
}

static fpga_crc_stats_t s_st;
static uint8_t s_last_hw;                 /* posledni fpga_crc16 sel pres HW */

#if FPGA_CRC_USE_HW
#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"
/* Pristup k jednotce pres makra (host test je prepise emulatorem). Registry a bity: RM0399, kap. CRC:
 * CR.POLYSIZE[4:3] = 01 -> 16 b; REV_IN[6:5] = 00, REV_OUT[7] = 0 (bez reflexe); CR.RESET[0] nacte INIT do DR.
 * Zapis 8b do DR zpracuje jeden bajt (stejne to dela HAL CRC_Handle_8); vysledek 16b polynomu je v DR[15:0]. */
#ifndef FPGA_CRC_HW_SETUP
#define FPGA_CRC_HW_SETUP()  do { CRC->POL = 0x1021u; CRC->INIT = 0xFFFFu; CRC->CR = CRC_CR_POLYSIZE_0 | CRC_CR_RESET; } while (0)
#define FPGA_CRC_HW_WR8(b)   (*(volatile uint8_t *)(volatile void *)&CRC->DR = (uint8_t)(b))
#define FPGA_CRC_HW_RD()     ((uint16_t)(CRC->DR & 0xFFFFu))
#define FPGA_CRC_CLK_EN()    __HAL_RCC_CRC_CLK_ENABLE()
#define FPGA_CRC_IN_ISR()    (__get_IPSR() != 0u)
#endif
static osMutexId_t s_mtx;

static uint16_t hw_crc_locked(const uint8_t *d, int n)
{
    FPGA_CRC_HW_SETUP();
    for (int i = 0; i < n; i++) FPGA_CRC_HW_WR8(d[i]);
    return FPGA_CRC_HW_RD();
}
static int hw_try_lock(void)
{
    if (FPGA_CRC_IN_ISR()) return 0;                                   /* z preruseni vzdy SW */
    if (osKernelGetState() != osKernelRunning) return 1;               /* pred schedulerem: jedno vlakno */
    if (s_mtx == NULL) return 0;
    return osMutexAcquire(s_mtx, 0) == osOK;                           /* bez cekani; obsazeno -> SW */
}
static void hw_unlock(void)
{
    if (osKernelGetState() == osKernelRunning && s_mtx != NULL) osMutexRelease(s_mtx);
}
#endif

void fpga_crc_init(void)
{
    s_st.hw_on = 0u; s_st.selftest_ok = 0u;
#if FPGA_CRC_USE_HW
    FPGA_CRC_CLK_EN();
    if (s_mtx == NULL && osKernelGetState() == osKernelRunning) s_mtx = osMutexNew(NULL);
    /* selftest: "123456789" (0x29B1) + 32 pseudonahodnych bloku 1..1024 B, HW musi = SW */
    static const uint8_t v9[9] = { '1','2','3','4','5','6','7','8','9' };
    int ok = 0;
    if (hw_try_lock()) {
        ok = (hw_crc_locked(v9, 9) == 0x29B1u) && (fpga_crc16_sw(v9, 9) == 0x29B1u);
        uint8_t buf[256]; uint32_t r = 0x5EEDu;
        for (int t = 0; t < 32 && ok; t++) {
            int n = 1 + (int)(r % 256u);
            for (int i = 0; i < n; i++) { r = r * 1664525u + 1013904223u; buf[i] = (uint8_t)(r >> 24); }
            ok = (hw_crc_locked(buf, n) == fpga_crc16_sw(buf, n));
        }
        hw_unlock();
    }
    s_st.selftest_ok = (uint8_t)ok;
    s_st.hw_on = (uint8_t)ok;
#else
    s_st.selftest_ok = 1u;
#endif
}

uint16_t fpga_crc16(const uint8_t *d, int n)
{
#if FPGA_CRC_USE_HW
    if (s_st.hw_on && hw_try_lock()) {
        uint16_t c = hw_crc_locked(d, n);
        hw_unlock();
        s_st.hw_calls++; s_last_hw = 1u;
        return c;
    }
    if (s_st.hw_on) s_st.hw_busy++;
#endif
    s_st.sw_calls++; s_last_hw = 0u;
    return fpga_crc16_sw(d, n);
}

bool fpga_crc16_check(const uint8_t *d, int n, uint16_t expected)
{
    uint16_t c = fpga_crc16(d, n);
    if (c == expected) return true;
    if (s_last_hw) {                                  /* pojistka: HW rekla "nesedi" -> overit SW */
        uint16_t c2 = fpga_crc16_sw(d, n);
        if (c2 != c) { s_st.mismatch++; s_st.hw_on = 0u; }   /* HW a SW se lisi -> HW trvale vypnout */
        return c2 == expected;
    }
    return false;
}

void fpga_crc_stats(fpga_crc_stats_t *o) { if (o) *o = s_st; }
