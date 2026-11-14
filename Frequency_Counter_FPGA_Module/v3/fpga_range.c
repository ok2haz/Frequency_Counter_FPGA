#include "fpga_range.h"
uint8_t fpga_range_select(uint8_t cur, double f)
{
    if (!(f > 0.0)) return cur;                         /* bez odhadu nic nemenit */
    if (cur > FPGA_RANGE_MAXLOG2) cur = FPGA_RANGE_MAXLOG2;
    while (cur < FPGA_RANGE_MAXLOG2 && f / (double)(1u << cur) > FPGA_RANGE_UP_HZ) cur++;
    while (cur > 0u && f / (double)(1u << (cur - 1u)) < FPGA_RANGE_DOWN_HZ) cur--;
    return cur;
}
int fpga_range_selftest(void)
{
    int ok = 1;
    /* rozmitani nahoru a dolu: f/P vzdy <= 33 MHz (krome stropu /16) a zadne kmitani na mezi */
    uint8_t p = 0u;
    for (double f = 0.1; f < 420e6; f *= 1.01) {
        uint8_t q = fpga_range_select(p, f);
        double fp = f / (double)(1u << q);
        if (q < FPGA_RANGE_MAXLOG2 && fp > FPGA_RANGE_UP_HZ) ok = 0;
        if (fpga_range_select(q, f) != q) ok = 0;       /* stabilni */
        p = q;
    }
    for (double f = 420e6; f > 0.1; f /= 1.01) {
        uint8_t q = fpga_range_select(p, f);
        double fp = f / (double)(1u << q);
        if (q < FPGA_RANGE_MAXLOG2 && fp > FPGA_RANGE_UP_HZ) ok = 0;
        if (q > 0u && f / (double)(1u << (q - 1u)) < FPGA_RANGE_DOWN_HZ) ok = 0;
        p = q;
    }
    ok &= (fpga_range_select(0u, 10e6) == 0u) && (fpga_range_select(0u, 200e6) == 3u) && (fpga_range_select(0u, 400e6) == 4u);
    ok &= (fpga_range_select(2u, 30e6) == 1u || fpga_range_select(2u, 30e6) == 2u);  /* uvnitr hystereze se drzi */
    ok &= (fpga_range_select(3u, 0.0) == 3u);
    return ok;
}
