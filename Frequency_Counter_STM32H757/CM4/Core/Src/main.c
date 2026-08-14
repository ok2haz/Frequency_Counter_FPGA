/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ipc_cm4.h"   /* IPC konzument: cte snapshot CM7->CM4 + publikuje heartbeat */
#include "iwdg2.h"     /* nezavisly watchdog CM4 (~4 s); zaseknuta smycka -> reset CM4 */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint32_t beepTicks = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
  /*HW semaphore Clock enable*/
  __HAL_RCC_HSEM_CLK_ENABLE();
  /* Activate HSEM notification for Cortex-M4*/
  HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(HSEM_ID_0));
  /*
  Domain D2 goes to STOP mode (Cortex-M4 in deep-sleep) waiting for Cortex-M7 to
  perform system initialization (system clock config, external memory configuration.. )
  */
  HAL_PWREx_ClearPendingEvent();
  HAL_PWREx_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFE, PWR_D2_DOMAIN);
  /* Clear HSEM flag */
  __HAL_HSEM_CLEAR_FLAG(__HAL_HSEM_SEMID_TO_MASK(HSEM_ID_0));

/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* ⚠️ PER-CORE RCC (audit #23, 2026-08-11) — CM4 si musi povolit hodiny ve SVE
   * domene (RCC_C2_*ENR), ne v domene CM7.
   *
   * Past: `__HAL_RCC_XXX_CLK_ENABLE()` zapisuje VZDY do `RCC->xxxENR` = registr
   * CPU1 (CM7) — makra NEJSOU prepinana podle `CORE_CM4`, C2 varianty jsou
   * samostatna rodina `__HAL_RCC_C2_XXX_CLK_ENABLE()`. Generovany kod CubeMX
   * (MX_GPIO_Init, HAL_TIM_Base_MspInit, HAL_MspInit) pouziva plain varianty,
   * takze CM4 dosud povoloval hodiny CM7. Dnes to "funguje", protoze periferie
   * je clockovana, kdyz je bit v KTEREMKOLI z obou registru — jenze prirazeni
   * domene rozhoduje pri low-power a autonomnim behu D2. S ETH by to kouslo.
   *
   * Reseni je regen-safe: generovany kod NEmenime (prepsal by ho regen), jen
   * tady navic povolime tytez hodiny i v C2 domene. Redundantni zapis nevadi.
   * Musi byt PRED MX_GPIO_Init/MX_TIM12_Init. */
  __HAL_RCC_C2_GPIOG_CLK_ENABLE();    /* LED_2 (PG7) */
  __HAL_RCC_C2_TIM12_CLK_ENABLE();    /* beeper PWM */
  __HAL_RCC_C2_HSEM_CLK_ENABLE();     /* boot gate + budouci notifikace */
  __HAL_RCC_C2_SYSCFG_CLK_ENABLE();
  /* D2 SRAM2+3 = domaci RAM CM4 (linker `RAM @0x10020000/160K`). Dnes bezi na
   * reset-default, ale ETH deskriptory a lwIP heap tu poblezi -> vlastnit je
   * explicitne (DUALCORE_BRINGUP_CHECKLIST.md §4). */
  __HAL_RCC_C2_D2SRAM2_CLK_ENABLE();
  __HAL_RCC_C2_D2SRAM3_CLK_ENABLE();
  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM12_Init();
  /* USER CODE BEGIN 2 */
  Beep(420, 200);
  HAL_Delay(400);
  Beep(840, 200);
  HAL_Delay(400);
  Beep(1680, 200);
  HAL_Delay(400);
  ipc_cm4_init();   /* IPC: reset lokalniho stavu pred ctenim snapshotu */
  /* ⚠️⚠️ IWDG2 ZAMERNE VYPNUTY (zmereno na HW 2026-08-13) ─────────────────────
   * Reset scope IWDG2 je **SYSTEM-WIDE, ne per-core**. Overeno primo: docasna
   * CM4, ktera po 20 s prestala krmit watchdog, shodila CELY pristroj —
   * uptime CM7 pak cykloval 3 -> 13 -> 22 -> 8 -> 17 s, tedy reset kazdych ~24 s.
   * Predpoklad "CPU2/CM4-only" z DUALCORE_BRINGUP_CHECKLIST §8 tedy NEPLATI.
   *
   * Zapnuty IWDG2 by znamenal, ze zaseknuta CM4 (ktera dnes dela temer nic)
   * shodi i displej a mereni. To je vyrazne horsi nez zaseknuta CM4 samotna,
   * proto se nepouziva — presne jak pro tenhle pripad predepisuje CLAUDE.md.
   *
   * Zaseknuti CM4 se NEZTRATI: CM7 ho vidi pres heartbeat (`ipc_cm4_alive`),
   * loguje `stall:CM4` a pocita `g_cm4_stall_count` (UART `status`).
   * Az bude na CM4 bezet ETH/SCPI, da se pridat cileny restart CM4 z CM7
   * (drzet ho v resetu pres RCC) — to uz je ale jina vec nez nezavisly watchdog.
   * iwdg2_init(); */
  (void)iwdg2_init;   /* ponechano prelozene, at je to jednorazove vratitelne */

  /* DWT cyklovy citac pro mereni VLASTNI zateze CM4 (idle-based) -> publikuje se
   * pres IPC heartbeat jako "4:xx%" v CM7 headeru. Clock-agnosticky: pocita se
   * pomer busy_cyc / total_cyc, netreba znat SystemCoreClock (na CM4 nemusi byt
   * spravne, protoze CM4 nevola SystemClock_Config). */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
  uint32_t s_cm4_busy_cyc = 0, s_cm4_win_cyc0 = 0, s_cm4_win_ms = 0, s_cm4_pct = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  uint32_t cyc0 = DWT->CYCCNT;   /* start mereni work-cyklu teto iterace */
	  iwdg2_kick();   /* obnov watchdog CM4 (smycka ~800 ms << 4 s timeout) */
	  /* IPC: dokud CM7 neorazitkuje snapshot, zkousej overit hlavicku. */
	  if (!ipc_cm4_ready()) ipc_cm4_check();
	  /* Precti aktualni snapshot (seqlock) + publikuj heartbeat pro CM7 (liveness / "4:xx%").
	   * ⚠️ Duveryhodne JEN kdyz CM7 zije (snapshot seq roste) — jinak jsou to stara data. */
	  ipc_snapshot_t snap;
	  int have = ipc_cm4_ready() && ipc_cm4_cm7_alive(HAL_GetTick()) && ipc_cm4_read(&snap);
	  ipc_cm4_heartbeat(s_cm4_pct, HAL_GetTick() / 1000u);   /* posledni zmerena vlastni zatez [%] */

	  /* Zmer VLASTNI zatez: work-cykly teto iterace (vse KROME nasledneho HAL_Delay,
	   * ktere je "idle"). Kazdou ~1 s spocitej pomer busy/total -> s_cm4_pct. CM4 dnes
	   * dela skoro nic -> ~0 %; s ETH/SCPI to naskoci samo. */
	  s_cm4_busy_cyc += DWT->CYCCNT - cyc0;
	  uint32_t cm4_now = HAL_GetTick();
	  if (cm4_now - s_cm4_win_ms >= 1000u) {
		  uint32_t total = DWT->CYCCNT - s_cm4_win_cyc0;   /* celkove cykly v okne (unsigned wrap OK) */
		  s_cm4_pct = total ? (uint32_t)((uint64_t)s_cm4_busy_cyc * 100u / total) : 0u;
		  if (s_cm4_pct > 100u) s_cm4_pct = 100u;
		  s_cm4_busy_cyc = 0u; s_cm4_win_cyc0 = DWT->CYCCNT; s_cm4_win_ms = cm4_now;
	  }

	  /* LED_2 = VIDITELNY dukaz mezijaderneho ctení: sviti trvale pri GPS fixu ze
	   * snapshotu CM7; jinak (bez IPC / bez fixu) pomalu blika = holy heartbeat. */
	  if (have && (snap.flags & IPC_F_GPS_VALID)) {
		  HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_SET);
		  HAL_Delay(800);
	  } else {
		  HAL_Delay(400);
		  HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_SET);
		  HAL_Delay(400);
		  HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_RESET);
	  }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_FMC|RCC_PERIPHCLK_ADC
                              |RCC_PERIPHCLK_SPI2|RCC_PERIPHCLK_LTDC;
  PeriphClkInitStruct.PLL2.PLL2M = 1;
  PeriphClkInitStruct.PLL2.PLL2N = 20;
  PeriphClkInitStruct.PLL2.PLL2P = 1;
  PeriphClkInitStruct.PLL2.PLL2Q = 1;
  PeriphClkInitStruct.PLL2.PLL2R = 2;
  PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_3;
  PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
  PeriphClkInitStruct.PLL3.PLL3M = 1;
  PeriphClkInitStruct.PLL3.PLL3N = 17;
  PeriphClkInitStruct.PLL3.PLL3P = 2;
  PeriphClkInitStruct.PLL3.PLL3Q = 2;
  PeriphClkInitStruct.PLL3.PLL3R = 7;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_3;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 4096;
  PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
  PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL3;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void Beep(uint32_t frequency, uint32_t duration_ms)
{
    uint32_t period = 1000000 / frequency;
    uint32_t pulse = period / 2;

    __HAL_TIM_SET_AUTORELOAD(&htim12, period - 1);
    __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, pulse);

    HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);

    beepTicks = duration_ms;
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
