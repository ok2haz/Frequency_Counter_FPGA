/* Host test fpga_spi_dma.c: HAL SPI DMA, semafor a cache jsou nahrazene modely. Overuje logiku prenosu, NE DMA na kremiku. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define FPGA_DMA_SECTION
#define FPGA_DMA_IN_ISR() (0)
static uint32_t cyc; 
#define FPGA_DMA_CYCLES() (cyc += 1000u)
#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"
static int mode;                 /* 0 OK (callback hned), 1 bez callbacku (timeout), 2 chybovy callback, 3 start selze */
static int sem_cnt, aborts, cleans, invals, running = 2; static const uint8_t *last_tx_dma; static uint8_t *last_rx_dma;
static SPI_HandleTypeDef hspi2; static DMA_HandleTypeDef dtx, drx;
osKernelState_t osKernelGetState(void){return running;}
osSemaphoreId_t osSemaphoreNew(uint32_t m,uint32_t i,const void*a){(void)m;(void)a;sem_cnt=(int)i;return (void*)1;}
osStatus_t osSemaphoreAcquire(osSemaphoreId_t s,uint32_t t){(void)s;(void)t; if(sem_cnt>0){sem_cnt--;return osOK;} return osErrorTimeout;}
osStatus_t osSemaphoreRelease(osSemaphoreId_t s){(void)s; if(sem_cnt<1) sem_cnt++; return osOK;}
osMutexId_t osMutexNew(const void*a){(void)a;return (void*)1;} osStatus_t osMutexAcquire(osMutexId_t m,uint32_t t){(void)m;(void)t;return 0;} osStatus_t osMutexRelease(osMutexId_t m){(void)m;return 0;}
void SCB_CleanDCache_by_Addr(uint32_t*a,int32_t n){(void)a;(void)n;cleans++;} void SCB_InvalidateDCache_by_Addr(uint32_t*a,int32_t n){(void)a;(void)n;invals++;}
void fpga_spi_dma_cplt_isr(SPI_HandleTypeDef *h); void fpga_spi_dma_err_isr(SPI_HandleTypeDef *h);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef*h,const uint8_t*t,uint8_t*r,uint16_t n){
  if(mode==3) return HAL_ERROR; last_tx_dma=t; last_rx_dma=r;
  for(int i=0;i<n;i++) r[i]=(uint8_t)~t[i];               /* "FPGA" vrati invertovana data */
  if(mode==0) fpga_spi_dma_cplt_isr(h); else if(mode==2) fpga_spi_dma_err_isr(h); return HAL_OK; }
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef*h){(void)h;aborts++;return HAL_OK;}
#include "../fpga_spi_dma.c"
static int fails; static void ck(int c,const char*m){printf("  %s %s\n",c?"OK   ":"CHYBA",m); if(!c)fails++;}
int main(void){
  fpga_spi_dma_stats_t s; uint8_t tx[1024], rx[1024];
  for(int i=0;i<1024;i++) tx[i]=(uint8_t)(i*13+1);
  ck(!fpga_spi_dma_init(&hspi2), "bez propojenych DMA handlu -> init odmitne");
  hspi2.hdmatx=&dtx; hspi2.hdmarx=&drx;
  running=0; ck(!fpga_spi_dma_init(&hspi2), "pred schedulerem -> init odmitne"); running=2;
  ck(fpga_spi_dma_init(&hspi2) && fpga_spi_dma_ready(), "init OK");
  ck(!addr_dma_ok((void*)0x20001000u) && !addr_dma_ok((void*)0x00000100u) && addr_dma_ok((void*)0x24000100u) && addr_dma_ok((void*)0x30000100u),
     "kontrola adres: DTCM a ITCM odmitnuty, AXI SRAM a SRAM1-3 povoleny");
  mode=0; memset(rx,0,sizeof rx); int c0=cleans, i0=invals;
  int ok=fpga_spi_dma_txrx(tx,rx,128,100); int good=1; for(int i=0;i<128;i++) if(rx[i]!=(uint8_t)~tx[i]) good=0;
  ck(ok && good, "prenos 128 B: data z DMA zkopirovana do volajiciho");
  ck(cleans==c0+1 && invals==i0+2, "cache: clean TX pred startem, invalidate RX pred i po");
  ck(last_tx_dma==s_tx && last_rx_dma==s_rx, "DMA pracuje s vlastnimi zarovnanymi buffery modulu");
  ck(((uintptr_t)s_tx % 32u)==0 && ((uintptr_t)s_rx % 32u)==0 && sizeof s_tx % 32u == 0, "buffery zarovnane na radek cache 32 B");
  ok=fpga_spi_dma_txrx(tx,rx,1024,100); ck(ok, "prenos 1024 B (ramec v3)");
  ck(!fpga_spi_dma_txrx(tx,rx,1025,100) && !fpga_spi_dma_txrx(tx,rx,0,100), "delka 0 nebo > max -> odmitnuto");
  sem_cnt=1; mode=1; int a0=aborts; ck(!fpga_spi_dma_txrx(tx,rx,128,5), "stary semafor zahozen, bez callbacku -> timeout");
  ck(aborts==a0+1, "timeout -> HAL_SPI_Abort");
  mode=0; ck(fpga_spi_dma_txrx(tx,rx,128,100), "dalsi prenos po timeoutu funguje (citac chyb za sebou vynulovan)");
  mode=2; a0=aborts; ck(!fpga_spi_dma_txrx(tx,rx,128,100) && aborts==a0+1, "chybovy callback -> false + Abort");
  mode=3; ck(!fpga_spi_dma_txrx(tx,rx,128,100), "start selze -> false");
  mode=1; (void)fpga_spi_dma_txrx(tx,rx,128,1); fpga_spi_dma_stats(&s);
  ck(s.disabled && !fpga_spi_dma_ready(), "3 chyby za sebou -> DMA vypnuto (dal blokujici prenos)");
  ck(s.ok==3 && s.timeout==2 && s.dma_err==1 && s.start_err==1 && s.rejected==2, "statistiky sedi");
  printf("VYSLEDEK DMA: %s\n", fails?"SELHALO":"VSE PROSLO"); return fails; }
