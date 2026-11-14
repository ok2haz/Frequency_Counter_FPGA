/* fpga_spi_dma.h -- prenos ramce FPGA pres SPI2 + DMA (audit 2026-10-20, optimalizace 6).
 * HOTOVE a jiste: logika prenosu (kopie do DMA bufferu, udrzba D-cache, start, cekani na semafor s casovym limitem,
 * HAL_SPI_Abort pri chybe, pocitani, vypnuti po 3 chybach za sebou), kontroly (delka, adresa bufferu mimo DTCM/ITCM,
 * propojene DMA handly, volani jen pod schedulerem a mimo preruseni).
 * DOKONCI CLI (zavisi na projektu CubeMX): DMA streamy SPI2 RX/TX + DMAMUX + NVIC (priorita <= configMAX_SYSCALL!),
 * sekce linkeru FPGA_DMA_SECTION, napojeni HAL_SPI_TxRxCpltCallback / HAL_SPI_ErrorCallback.
 * Vychozi FPGA_SPI_USE_DMA = 0 (fpga_freq.c), chovani beze zmeny. Navod: 2026-10-20_CRC_DMA_zadani_CLI.md. */
#ifndef FPGA_SPI_DMA_H
#define FPGA_SPI_DMA_H
#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_hal.h"
#ifndef FPGA_SPI_DMA_MAX
#define FPGA_SPI_DMA_MAX 1024u            /* nejdelsi ramec (v3); v2 = 128 */
#endif
#define FPGA_SPI_DMA_FAIL_OFF 3u          /* tolik chyb ZA SEBOU -> DMA vypnout (dal blokujici prenos) */
typedef struct { uint32_t ok, start_err, timeout, dma_err, rejected, cyc_last, cyc_max; uint8_t ready, disabled; } fpga_spi_dma_stats_t;
bool fpga_spi_dma_init(SPI_HandleTypeDef *h);             /* true = DMA pouzitelne */
bool fpga_spi_dma_ready(void);
/* Jedna datova faze transakce (CS ridi volajici). false = prenos neprobehl / neplatny -> volajici ukonci transakci
 * (CS high) a zopakuje az PRISTI transakci (nikdy ne dalsi faze pod stejnym CS: FPGA by videla delsi ramec). */
bool fpga_spi_dma_txrx(const uint8_t *tx, uint8_t *rx, uint16_t n, uint32_t timeout_ms);
void fpga_spi_dma_cplt_isr(SPI_HandleTypeDef *h);         /* volat z HAL_SPI_TxRxCpltCallback */
void fpga_spi_dma_err_isr(SPI_HandleTypeDef *h);          /* volat z HAL_SPI_ErrorCallback */
void fpga_spi_dma_stats(fpga_spi_dma_stats_t *o);
#endif
