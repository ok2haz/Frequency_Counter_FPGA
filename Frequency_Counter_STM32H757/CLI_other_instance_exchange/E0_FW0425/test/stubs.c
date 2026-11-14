#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"
#include "i2c.h"
#include "sensor_stat.h"
#include <string.h>
static GPIO_TypeDef g_gpio[9]; static SPI_TypeDef g_spi[3]; static DWT_Type g_dwt; static CoreDebug_Type g_cd;
GPIO_TypeDef *GPIOA=&g_gpio[0],*GPIOB=&g_gpio[1],*GPIOC=&g_gpio[2],*GPIOD=&g_gpio[3],*GPIOE=&g_gpio[4],*GPIOF=&g_gpio[5],*GPIOG=&g_gpio[6],*GPIOH=&g_gpio[7],*GPIOI=&g_gpio[8];
SPI_TypeDef *SPI1=&g_spi[0],*SPI2=&g_spi[1],*SPI3=&g_spi[2]; DWT_Type *DWT=&g_dwt; CoreDebug_Type *CoreDebug=&g_cd;
I2C_HandleTypeDef hi2c1; sens_stat_t g_sensors[16];
uint8_t g_test_rx[128]; int g_test_rx_on = 0; uint32_t g_tick = 0;
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef*h){(void)h;return HAL_OK;} HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef*h){(void)h;return HAL_OK;}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef*h,const uint8_t*t,uint8_t*r,uint16_t n,uint32_t to){(void)h;(void)t;(void)to; if(g_test_rx_on) memcpy(r,g_test_rx,n); else memset(r,0,n); return HAL_OK;}
void HAL_GPIO_Init(GPIO_TypeDef*g,GPIO_InitTypeDef*i){(void)g;(void)i;} void HAL_GPIO_WritePin(GPIO_TypeDef*g,uint16_t p,int s){(void)g;(void)p;(void)s;}
int HAL_GPIO_ReadPin(GPIO_TypeDef*g,uint16_t p){(void)g;(void)p;return 0;} uint32_t HAL_GetTick(void){return g_tick+=10;} void HAL_Delay(uint32_t d){(void)d;}
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t x){(void)x;return 200000000u;} uint32_t HAL_RCC_GetPCLK1Freq(void){return 100000000u;}
osMutexId_t osMutexNew(const osMutexAttr_t*a){(void)a;return (void*)1;} osStatus_t osMutexAcquire(osMutexId_t m,uint32_t t){(void)m;(void)t;return 0;}
osStatus_t osMutexRelease(osMutexId_t m){(void)m;return 0;} uint32_t osKernelGetTickCount(void){return g_tick;} osStatus_t osDelay(uint32_t d){(void)d;return 0;} int osKernelGetState(void){return 2;}
uint16_t gpsdo_sim_dac_code(void){return 0;}
SPI_HandleTypeDef hspi2;
volatile uint16_t g_ads_raw_last[8];
volatile uint32_t g_si5356_err_cnt;
