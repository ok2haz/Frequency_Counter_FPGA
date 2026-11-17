/* fpga_spi_dma.c -- viz fpga_spi_dma.h. Audit 2026-10-20, optimalizace 6. */
#include "fpga_spi_dma.h"
#include "cmsis_os2.h"
#include <string.h>

/* DMA1/DMA2 (D2) NEVIDI DTCM (0x2000_0000..0x2001_FFFF) ani ITCM -> buffery musi lezet v AXI SRAM (0x2400_0000) nebo
 * SRAM1..3 (0x3000_0000). Nazev sekce zavisi na linker skriptu projektu -> CLI ho MUSI zadat (jinak chyba prekladu). */
/* 2026-10-22 (krok 7): bez vlastni sekce -- .bss lezi v RAM_D1 (AXI SRAM 0x2400_0000, linker STM32H757BITX_FLASH.ld), kterou
 * DMA1 vidi; addr_dma_ok() to overi za behu (kdyby se .bss nekdy presunula do DTCM, DMA se nezapne). */
#ifndef FPGA_DMA_SECTION
#define FPGA_DMA_SECTION
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
static DMA_HandleTypeDef      s_dma_rx, s_dma_tx;
static uint8_t                s_hw = 0u;              /* fpga_spi_dma_hw_init probehl */
static volatile uint8_t       s_on = 0u;              /* zapnuto za behu (vychozi ne) */
#ifndef FPGA_DMA_IRQ_PRIO
#define FPGA_DMA_IRQ_PRIO 6u                           /* >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5): ISR smi volat RTOS */
#endif
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
    return s_on && s_st.ready && !s_st.disabled && osKernelGetState() == osKernelRunning && !FPGA_DMA_IN_ISR();
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

/* ── 2026-10-22 (krok 7): HW konfigurace regen-safe (misto CubeMX) ──
 * DMA1 Stream0 = SPI2_RX (periferie -> pamet, priorita HIGH), Stream1 = SPI2_TX (MEDIUM): RX >= TX, aby nevznikl
 * overrun. Bajty, bez FIFO, NORMAL. NVIC: oba streamy + SPI2 global (HAL SPI na H7 pri DMA dokoncuje prenos v
 * preruseni SPI, EOT). Pri blokujicim prenosu (HAL_SPI_TransmitReceive) jsou preruseni SPI vypnuta (IER = 0). */
bool fpga_spi_dma_hw_init(SPI_HandleTypeDef *h)
{
    if (s_hw) return true;
    if (h == NULL) return false;
    __HAL_RCC_DMA1_CLK_ENABLE();
    s_dma_rx.Instance                 = DMA1_Stream0;
    s_dma_rx.Init.Request             = DMA_REQUEST_SPI2_RX;
    s_dma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    s_dma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_dma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    s_dma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_dma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    s_dma_rx.Init.Mode                = DMA_NORMAL;
    s_dma_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    s_dma_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&s_dma_rx) != HAL_OK) return false;
    s_dma_tx = s_dma_rx;
    s_dma_tx.Instance                 = DMA1_Stream1;
    s_dma_tx.Init.Request             = DMA_REQUEST_SPI2_TX;
    s_dma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    s_dma_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    if (HAL_DMA_Init(&s_dma_tx) != HAL_OK) return false;
    __HAL_LINKDMA(h, hdmarx, s_dma_rx);
    __HAL_LINKDMA(h, hdmatx, s_dma_tx);
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, FPGA_DMA_IRQ_PRIO, 0u); HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, FPGA_DMA_IRQ_PRIO, 0u); HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    HAL_NVIC_SetPriority(SPI2_IRQn,         FPGA_DMA_IRQ_PRIO, 0u); HAL_NVIC_EnableIRQ(SPI2_IRQn);
    s_hw = 1u;
    return true;
}

void fpga_spi_dma_enable(bool on)
{
    if (!on) { s_on = 0u; return; }
    if (s_h == NULL || !s_hw) return;
    s_fail_run = 0u; s_st.disabled = 0u;
    s_on = 1u;
}
bool fpga_spi_dma_enabled(void) { return s_on != 0u; }

/* obsluhy preruseni (silne symboly proti weak ve startupu; CubeMX je negeneruje, dokud se DMA/SPI2 IRQ nezapne v .ioc --
 * kdyby se to nekdy stalo, linker ohlasi dvoji definici, nic se tise nezmeni) */
void DMA1_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&s_dma_rx); }
void DMA1_Stream1_IRQHandler(void) { HAL_DMA_IRQHandler(&s_dma_tx); }
void SPI2_IRQHandler(void)         { if (s_h != NULL) HAL_SPI_IRQHandler(s_h); }
/* callbacky HAL SPI (jediny SPI v projektu s prerusenim je SPI2 -> rozliseni handlu stejne dela *_isr) */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h) { fpga_spi_dma_cplt_isr(h); }
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h)    { fpga_spi_dma_err_isr(h); }
