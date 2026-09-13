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
/* ⚠️ 2026-09-12 presunuto sem z generovane casti: regen ho smazal. */
#include "../../../CM7/Core/Inc/ipc_shared.h"   /* g_ipc — crash black-box CM4 (v14) */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */
#include "tim.h"
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
/* ⚠️ 2026-09-12 PRESUNUTO SEM z generovane casti (crash black-box CM4).
 * Do te doby to lezelo mezi generovanymi handlery a regenerace CubeMX to
 * smazala, prestoze handlery obe funkce dal volaji -> CM4 se neslinkovalo.
 * Text je doslova puvodni, jen se prestehoval. */
/* 🔴 Na CM7 uz tyhle ctyri handlery zapisuji crash black-box; na CM4 zustaly
 * neme (2 B v obrazu = jedina instrukce `b .`). Odhalil to rozsireny audit
 * — a je to ukazkove poruseni §6l: opravil jsem tridu jen na jednom jadre.
 * CM4 na BKP registry nedosahne (nema povolene hodiny RTC), takze se stav
 * ulozi do IPC bloku, odkud ho CM7 vypise ve `status`.
 * ⚠️ NERESETOVAT: `NVIC_SystemReset()` z CM4 shodi CELY pristroj, a presne
 * proto je IWDG2 vypnuty. Zustane se stat, CM7 to uvidi jako `stall:CM4`. */
static void cm4_fault_note(uint8_t kind)
{  g_ipc.cm4.cm4_fault_pc   = 0u;
  g_ipc.cm4.cm4_fault_lr   = 0u;
  g_ipc.cm4.cm4_fault_cfsr = SCB->CFSR;
  __DMB();
  g_ipc.cm4.cm4_fault_kind = kind;   /* 3 NMI, 4 MemMan, 5 BusFlt, 6 UsgFlt */
  __DMB();
}

/* Zachyti stav faultu do sdilene pameti, aby ho CM7 mohl ohlasit (v14).
 * ⚠️ CM4 na BKP registry nedosahne (nema povolene hodiny RTC), takze jedina
 * cesta ven z faultu je IPC blok v SRAM4. */
void cm4_fault_capture(uint32_t *frame);
void cm4_fault_capture(uint32_t *frame)
{  g_ipc.cm4.cm4_fault_pc   = frame[6];
  g_ipc.cm4.cm4_fault_lr   = frame[5];
  g_ipc.cm4.cm4_fault_cfsr = SCB->CFSR;
  __DMB();
  g_ipc.cm4.cm4_fault_kind = 1u;    /* magic naposled: necely zapis = neplatny */
  __DMB();
  /* ⚠️ ZAMERNE se NERESETUJE: `NVIC_SystemReset()` z CM4 shodi CELY pristroj
   * (displej i mereni) — a prave proto je IWDG2 vypnuty. Zustane se stat, CM7
   * to uvidi jako `stall:CM4` a ted uz i s duvodem. */
  for (;;) { }
}

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/

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
  cm4_fault_note(3u);   /* NMI -> IPC blok; CM7 to hlasi ve `status` */

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
  cm4_fault_note(4u);   /* MemManage -> IPC blok; CM7 to hlasi ve `status` */

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
  cm4_fault_note(5u);   /* BusFault -> IPC blok; CM7 to hlasi ve `status` */

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
  cm4_fault_note(6u);   /* UsageFault -> IPC blok; CM7 to hlasi ve `status` */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
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

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */
    if (beepTicks > 0)
    {
        beepTicks--;
        if (beepTicks == 0)
        {
            HAL_TIM_PWM_Stop(&htim12, TIM_CHANNEL_2);
        }
    }
  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h7xx.s).                    */
/******************************************************************************/

/* USER CODE BEGIN 1 */

/* HardFault: crash black-box CM4 (PC/LR/CFSR -> IPC blok cm4, viz cm4_fault_capture
 * v USER CODE 0). Handler je ZAMERNE TADY, v USER CODE 1, a v `.ioc` je u HardFault
 * (Context1/CM4, NVIC2.HardFault_IRQn) odskrtnute "Generate IRQ handler" (pole 6 =
 * false, stejne jako uz drive na CM7 u NVIC1.HardFault_IRQn). Diky tomu CubeMX
 * tuhle funkci NEGENERUJE a regen ji uz nemuze prepsat.
 * ⚠️⚠️ ANI JEDNA POJISTKA SAMA O SOBE NESTACI (zjisteno 2026-09-13 realnym regenem):
 * kdyz je handler MIMO USER CODE, i s vypnutym "Generate handler" ho CubeMX pri
 * regenu celý SMAZAL (rozpoznal svuj puvodni doxygen komentar + jmeno funkce a
 * uklidil "nepouzivany" handler) — presne to se stalo pred timto zapisem. Musi
 * platit OBOJI naraz: vypnuty flag v .ioc (jinak by CubeMX psal svuj stub PRES
 * tohle) A umisteni v USER CODE (jinak to CubeMX i s vypnutym flagem odstrani).
 *
 * MUSI byt `naked`: prolog bezne C funkce posune MSP, takze `frame[6]` uz necte
 * exception frame, ale prolog (vracelo by to VEROHODNE VYPADAJICI, ale spatne
 * PC/LR). EXC_RETURN bit2 (`tst lr, #4`) rozlisuje, ktery zasobnik se pouzil. */
__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile (
    "tst  lr, #4            \n"
    "ite  eq                \n"
    "mrseq r0, msp          \n"
    "mrsne r0, psp          \n"
    "b    cm4_fault_capture \n"
  );
}

/* USER CODE END 1 */
