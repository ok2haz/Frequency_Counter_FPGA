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
  * @brief This function handles Hard fault interrupt.
  */
/* USER CODE BEGIN HardFault_capture */
/* Zachyceni kontextu HardFaultu. ⚠️ PREPSANO 2026-08-13 — puvodni verze mela
 * dve vady, kvuli kterym vracela nepouzitelne cislo:
 *  1) Handler byl bezna C funkce, takze mu GCC vygeneroval PROLOG. Ten posune
 *     MSP, takze `ldr [msp, #20]` uz necetl exception frame, ale prolog. Pro
 *     fault v handler modu (SysTick/PendSV) tedy vychazela nahodna data —
 *     opakovane napr. adresa `pxReadyTasksLists`, coz svadelo k honeni prizraku.
 *     -> handler je ted `naked` a ukazatel na ramec predava do C funkce.
 *  2) Ukladalo se stacknute LR = navrat do VOLAJICIHO. Uzitecnejsi je stacknute
 *     **PC** = presna instrukce, kde to spadlo (`addr2line` na .elf).
 * Nove se uklada PC (DR4), CFSR (DR5) a BFAR (DR7) — BFAR je adresa, ktera
 * BusFault zpusobila, tedy "co se sahalo". */
void hard_fault_capture(uint32_t *frame);
void hard_fault_capture(uint32_t *frame)
{
  uint32_t cfsr = SCB->CFSR;
  PWR->CR1 |= PWR_CR1_DBP;          /* povol zapis do backup domeny */
  RTC->BKP3R = 0xC7A50000u | 4u;    /* RTC_CRASH_MAGIC | kind 4 = HardFault */
  RTC->BKP4R = frame[6];            /* stacknute PC = kde to spadlo */
  RTC->BKP5R = cfsr;
  RTC->BKP7R = (cfsr & (1u << 15)) ? SCB->BFAR : 0u;   /* BFARVALID -> adresa */
  NVIC_SystemReset();
}
/* USER CODE END HardFault_capture */

__attribute__((naked)) void HardFault_Handler(void)
{
  /* Bez prologu -> `msp`/`psp` ukazuje PRESNE na exception frame.
   * EXC_RETURN bit2 rozlisuje, ktery zasobnik se pouzil. */
  __asm volatile (
    "tst  lr, #4             \n"
    "ite  eq                 \n"
    "mrseq r0, msp           \n"
    "mrsne r0, psp           \n"
    "b    hard_fault_capture \n"
  );
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
/* USER CODE END 1 */
