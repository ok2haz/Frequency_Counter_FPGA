/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Zachyceni kontextu HardFaultu do crash black-boxu (BKP registry, kind 4).
 * ⚠️ Tento helper je ZAMERNE v `USER CODE BEGIN 0` -> PREZIJE regeneraci z .ioc.
 * Naproti tomu samotny `HardFault_Handler` nize je `naked` (mimo USER CODE) a
 * regen ho PREPISE na prazdny `while(1)` — po kazde regeneraci ho vrat (viz
 * CUBEMX_CHECKLIST). Historie oprav (commit b5f8411): handler MUSI byt `naked`,
 * jinak jeho prolog posune MSP a `frame[6]` uz necte exception frame, ale prolog. */
void hard_fault_capture(uint32_t *frame);
void hard_fault_capture(uint32_t *frame)
{
  uint32_t cfsr = SCB->CFSR;
  PWR->CR1 |= PWR_CR1_DBP;          /* povol zapis do backup domeny */
  RTC->BKP3R = 0xC7A50000u | 4u;    /* RTC_CRASH_MAGIC | kind 4 = HardFault */
  RTC->BKP4R = frame[6];            /* stacknute PC = kde to spadlo (addr2line) */
  RTC->BKP5R = cfsr;
  RTC->BKP7R = (cfsr & (1u << 15)) ? SCB->BFAR : 0u;   /* BFARVALID -> adresa */
  RTC->BKP8R = frame[5];            /* stacknute LR = ODKUD se skocilo (caller!) */
  RTC->BKP9R = SCB->HFSR;           /* HFSR: bit1 VECTTBL, bit30 FORCED, bit31 DEBUGEVT */
  NVIC_SystemReset();
}
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim6;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h7xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles TIM6 global interrupt, DAC1_CH1 and DAC1_CH2 underrun error interrupts.
  */
void TIM6_DAC_IRQHandler(void)
{
  /* USER CODE BEGIN TIM6_DAC_IRQn 0 */

  /* USER CODE END TIM6_DAC_IRQn 0 */
  HAL_TIM_IRQHandler(&htim6);
  /* USER CODE BEGIN TIM6_DAC_IRQn 1 */

  /* USER CODE END TIM6_DAC_IRQn 1 */
}

/**
  * @brief This function handles USB OTG FS global interrupt.
  */
void OTG_FS_IRQHandler(void)
{
  /* USER CODE BEGIN OTG_FS_IRQn 0 */

  /* USER CODE END OTG_FS_IRQn 0 */
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
  /* USER CODE BEGIN OTG_FS_IRQn 1 */

  /* USER CODE END OTG_FS_IRQn 1 */
}

/* USER CODE BEGIN 1 */
/* TIM7 - generuje 1600 Hz pro beeper (PH9, 800 Hz ton) */
extern TIM_HandleTypeDef htim7;
void TIM7_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim7);
}

/* ⚠️⚠️ SDMMC1 — PRIDANO 2026-08-13, bez tohohle byl FatFs STRUKTURALNE ROZBITY.
 *
 * `sd_diskio.c` (ST) provadi kazde cteni/zapis pres `BSP_SD_ReadBlocks_DMA()`
 * a pak ceka na zpravu ve fronte `SDQueueID`. Tu zpravu posila JEDINE retezec
 *     SDMMC1_IRQHandler -> HAL_SD_IRQHandler -> HAL_SD_RxCpltCallback
 *                       -> BSP_SD_ReadCpltCallback -> osMessageQueuePut
 * Jenze `SDMMC1_IRQHandler` v projektu NEEXISTOVAL (ve startupu je `__weak`
 * napojeny na `Default_Handler`) a NVIC nebyl povoleny — takze zprava nemohla
 * NIKDY prijit. Dusledek: kazdy `f_mount`/`f_read` cekal celych `SD_TIMEOUT`
 * = **30 sekund**, pak vratil chybu a nechal `hsd1.State` viset v BUSY.
 * Kdyz takove volani padlo do defaultTasku (auto-mount po vlozeni karty), nestihl
 * `watchdog_supervise()` a desku shodil IWDG (~4 s) — presne pozorovane
 * "po `sd fs` je vzdy reset".
 *
 * Priorita 5 = `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`; nizsi (cislo)
 * nesmi byt, obsluha vola FreeRTOS API (`osMessageQueuePut`).
 * NVIC se povoluje v `BSP_SD_Init()` (sd_export.c) — regen-safe, mimo `.ioc`.
 * Blokujici cesta `datalog_sd.c` (`HAL_SD_ReadBlocks`/`WriteBlocks`, FIFO polling)
 * zadne preruseni nepovoluje, takze se s touhle obsluhou nebije. */
extern SD_HandleTypeDef hsd1;
void SDMMC1_IRQHandler(void)
{
  HAL_SD_IRQHandler(&hsd1);
}

/* HardFault: crash black-box (PC/LR/CFSR/BFAR/HFSR -> BKP registry).
 *
 * Handler je ZAMERNE TADY, v USER CODE 1, a v `.ioc` je u HardFault odskrtnute
 * "Generate IRQ handler" (`NVIC1.HardFault_IRQn` pole 6 = false, stejne jako to
 * ma FreeRTOS u PendSV/SysTick). Diky tomu ho CubeMX NEGENERUJE a regen ho uz
 * nemuze prepsat - do 2026-08-16 zil mimo USER CODE a `Generate Code` ho smazal
 * pokazde (15. i 16. 8.), takze crash black-box HardFaultu byl tise nefunkcni.
 *
 * MUSI byt `naked`: prolog bezne C funkce posune MSP, takze `frame[6]` uz necte
 * exception frame, ale prolog (kvuli tomu vracel drive nesmyslne adresy).
 * EXC_RETURN bit2 rozlisuje, ktery zasobnik se pouzil. */
__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile (
    "tst  lr, #4             \n"
    "ite  eq                 \n"
    "mrseq r0, msp           \n"
    "mrsne r0, psp           \n"
    "b    hard_fault_capture \n"
  );


}

/* USER CODE END 1 */
