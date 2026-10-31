/* fpga_spi_dma.c -- viz fpga_spi_dma.h. Audit 2026-10-20, optimalizace 6. */
#include "fpga_spi_dma.h"
#include "cmsis_os2.h"
#include <string.h>

/* DMA1/DMA2 (D2) NEVIDI DTCM (0x2000_0000..0x2001_FFFF) ani ITCM -> buffery musi lezet v AXI SRAM (0x2400_0000) nebo
 * SRAM1..3 (0x3000_0000). Nazev sekce zavisi na linker skriptu projektu -> CLI ho MUSI zadat (jinak chyba prekladu). */
#ifndef FPGA_DMA_SECTION
#error "Definuj FPGA_DMA_SECTION, napr. __attribute__((section(\".RAM_D2\"))) -- sekce mimo DTCM (viz navod)"
#endif
#define FPGA_DMA_BUF ((FPGA_SPI_DMA_MAX + 31u) & ~31u)          /* cele radky cache (32 B) */
static uint8_t s_tx[FPGA_DMA_BUF] __attribute__((aligned(32))) FPGA_DMA_SECTION;
static uint8_t s_rx[FPGA_DMA_BUF] __attribute__((aligned(32))) FPGA_DMA_SECTION;

#ifndef FPGA_DMA_IN_ISR
#define FPGA_DMA_IN_ISR()  (__get_IPSR() != 0u)
#endif
#ifndef FPGA_DMA_CYCLES
#define FPGA_DMA_CYCLES()  (DWT->CYCCNT)
#endif

static SPI_HandleTypeDef     *s_h;
static osSemaphoreId_t        s_sem;
static volatile uint8_t       s_err;
static fpga_spi_dma_stats_t   s_st;
static uint32_t               s_fail_run;

static int addr_dma_ok(const void *p)
{
    uint32_t a = (uint32_t)(uintptr_t)p;
    if (a < 0x00010000u) return 0;                              /* ITCM */
    if (a >= 0x20000000u && a < 0x20020000u) return 0;          /* DTCM 128 kB */
    return 1;
}

static void cache_clean(void *p, uint32_t n)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache_by_Addr((uint32_t *)p, (int32_t)((n + 31u) & ~31u));
#else
    (void)p; (void)n;
#endif
}
static void cache_inval(void *p, uint32_t n)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr((uint32_t *)p, (int32_t)((n + 31u) & ~31u));
#else
    (void)p; (void)n;
#endif
}

bool fpga_spi_dma_init(SPI_HandleTypeDef *h)
{
    s_st.ready = 0u;
    if (h == NULL || h->hdmatx == NULL || h->hdmarx == NULL) return false;   /* CubeMX: __HAL_LINKDMA chybi */
    if (!addr_dma_ok(s_tx) || !addr_dma_ok(s_rx)) return false;              /* sekce padla do DTCM/ITCM */
    if (osKernelGetState() != osKernelRunning) return false;                 /* semafor jen pod schedulerem */
    if (s_sem == NULL) s_sem = osSemaphoreNew(1u, 0u, NULL);
    if (s_sem == NULL) return false;
    s_h = h; s_fail_run = 0u; s_st.disabled = 0u; s_st.ready = 1u;
    return true;
}

bool fpga_spi_dma_ready(void)
{
    return s_st.ready && !s_st.disabled && osKernelGetState() == osKernelRunning && !FPGA_DMA_IN_ISR();
}

static bool fail(uint32_t *ctr)
{
    (*ctr)++;
    if (++s_fail_run >= FPGA_SPI_DMA_FAIL_OFF) s_st.disabled = 1u;   /* dal blokujici prenos */
    return false;
}

bool fpga_spi_dma_txrx(const uint8_t *tx, uint8_t *rx, uint16_t n, uint32_t timeout_ms)
{
    if (!fpga_spi_dma_ready() || tx == NULL || rx == NULL || n == 0u || n > FPGA_SPI_DMA_MAX) { s_st.rejected++; return false; }
    uint32_t c0 = FPGA_DMA_CYCLES();
    memcpy(s_tx, tx, n);
    cache_clean(s_tx, n);                         /* data TX z cache do RAM, kde je cte DMA */
    cache_inval(s_rx, n);                         /* zadne spinave radky RX, ktere by pozdeji prepsaly data z DMA */
    while (osSemaphoreAcquire(s_sem, 0u) == osOK) { }   /* zahodit pripadne stare uvolneni */
    s_err = 0u;
    if (HAL_SPI_TransmitReceive_DMA(s_h, s_tx, s_rx, n) != HAL_OK) return fail(&s_st.start_err);
    if (osSemaphoreAcquire(s_sem, timeout_ms) != osOK) { (void)HAL_SPI_Abort(s_h); return fail(&s_st.timeout); }
    if (s_err)                                        { (void)HAL_SPI_Abort(s_h); return fail(&s_st.dma_err); }
    cache_inval(s_rx, n);                         /* cist az data, ktera zapsalo DMA */
    memcpy(rx, s_rx, n);
    uint32_t dc = FPGA_DMA_CYCLES() - c0;
    s_st.cyc_last = dc; if (dc > s_st.cyc_max) s_st.cyc_max = dc;
    s_st.ok++; s_fail_run = 0u;
    return true;
}

void fpga_spi_dma_cplt_isr(SPI_HandleTypeDef *h)
{
    if (h != NULL && h == s_h && s_sem != NULL) (void)osSemaphoreRelease(s_sem);
}

void fpga_spi_dma_err_isr(SPI_HandleTypeDef *h)
{
    if (h != NULL && h == s_h && s_sem != NULL) { s_err = 1u; (void)osSemaphoreRelease(s_sem); }
}

void fpga_spi_dma_stats(fpga_spi_dma_stats_t *o) { if (o) *o = s_st; }
