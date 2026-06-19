/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>            /* printf v MX_FREERTOS_Init */
#include "freertos_shared.h"  /* sdilene globaly + task prototypy (tasky jsou ve freertos_task_*.c) */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* Makra tasku (RX_BUF_SIZE, TMP117_*, LCD_*, MAKE_RGB, …) jsou v jejich
 * freertos_task_*.c, kde se používají. */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* Pozn.: RxBuffer/RxIndex a ram_buf/sdram_buf jsou privatni ve freertos_task_uart.c.
 * Globaly nize jsou sdilene (extern ve freertos_shared.h) -> definice zustavaji zde. */
float g_CurrentTemperature = 0.0f;   /* TMP117 @ 0x48 na I2C4 (displej) */

/* FPGA deska na I2C1 */
float   g_temp49 = 0.0f;             /* TMP117 @ 0x49 */
float   g_temp4A = 0.0f;             /* TMP117 @ 0x4A */
int32_t g_ads_mv[4] = {0, 0, 0, 0};  /* ADS1115 v mV: AIN0/1 primo, AIN2=12V vetev, AIN3=5V vetev (po prepoctu delice) */

/* Mutex pro sdileni I2C4 sbernice (TMP117 task + dotykove UI + backlight) */
osMutexId_t i2c4MutexHandle;

/* Mutex pro I2C1 sbernici (sensor task + scan1 prikaz) */
osMutexId_t i2c1MutexHandle;

/* Mutex pro UART TX (printf z vice tasku se nesmi michat) - pouziva _write v main.c */
osMutexId_t uartTxMutexHandle;

/* Pozadavek na obrazovku: UART nastavi, UiTask obslouzi (libprim/libui neni
 * thread-safe -> kresli VYHRADNE UiTask). 3 = "screen main", 4 = "clear". */
volatile uint8_t g_screen_req  = 0;

/* Naformatovany kmitocet z FPGA (FpgaTask zapise, UiTask vykresli) */
volatile char    g_freq_text[48] = "----------,-----Hz";
volatile char    g_freq_info[64] = "";                 /* vedlejsi udaje (gate/edges/ch) */
volatile uint8_t g_freq_dirty = 1;
volatile uint8_t g_freq_stale = 0;                     /* 1 = SEQ se dlouho nehnula (mozna ztrata signalu) -> UI ztlumi */

/* Stav SPI + komunikace s FPGA (FpgaTask zapise, UiTask vykresli) */
volatile char    g_spi_text[64] = "SPI WAIT";
volatile uint8_t g_spi_ok    = 0;                      /* 1 = link ziva -> zelene */
volatile uint8_t g_spi_dirty = 1;

/* Manualne vytvorene tasky (NEJSOU v .ioc): handle + attributes drzime v USER CODE,
 * jinak by je CubeMX regen smazal (osThreadNew jsou v USER CODE RTOS_THREADS). */
osThreadId_t UiTaskHandle;
const osThreadAttr_t UiTask_attributes = {
  .name = "UiTask",
  .stack_size = 2048 * 4,                /* 8 KB: render plne obrazovky (libprim/libui) se do 4 KB nevejde */
  .priority = (osPriority_t) osPriorityBelowNormal,
};
osThreadId_t FpgaTaskHandle;
const osThreadAttr_t FpgaTask_attributes = {
  .name = "FpgaTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,                 /* defaultTask je v podstate idle (jen yield) */
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for UartTask */
osThreadId_t UartTaskHandle;
const osThreadAttr_t UartTask_attributes = {
  .name = "UartTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for I2C4Task */
osThreadId_t I2C4TaskHandle;
const osThreadAttr_t I2C4Task_attributes = {
  .name = "I2C4Task",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for UartRxQueue */
osMessageQueueId_t UartRxQueueHandle;
const osMessageQueueAttr_t UartRxQueue_attributes = {
  .name = "UartRxQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartUartTask(void *argument);
void StartI2C4(void *argument);
void StartUiTask(void *argument);
void StartFpgaTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  i2c4MutexHandle  = osMutexNew(NULL);   /* chrani I2C4 (TMP117 + touch + backlight) */
  i2c1MutexHandle  = osMutexNew(NULL);   /* chrani I2C1 (FPGA deska: TMP117 x2, ADS1115) */
  uartTxMutexHandle = osMutexNew(NULL);  /* serializuje printf/_write */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of UartRxQueue */
  UartRxQueueHandle = osMessageQueueNew (64, sizeof(uint8_t), &UartRxQueue_attributes);  /* 1 byte/prvek = shodne s RxByte i rxChar (driv uint16_t -> stack overflow) */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of UartTask */
  UartTaskHandle = osThreadNew(StartUartTask, NULL, &UartTask_attributes);

  /* creation of I2C4Task */
  I2C4TaskHandle = osThreadNew(StartI2C4, NULL, &I2C4Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  UiTaskHandle = osThreadNew(StartUiTask, NULL, &UiTask_attributes);
  if (UiTaskHandle == NULL) {
    printf("[ERR] UiTask se nevytvoril - malo FreeRTOS heapu (configTOTAL_HEAP_SIZE)\n");
  }
  FpgaTaskHandle = osThreadNew(StartFpgaTask, NULL, &FpgaTask_attributes);
  if (FpgaTaskHandle == NULL) {
    printf("[ERR] FpgaTask se nevytvoril - malo FreeRTOS heapu\n");
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(5);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartUartTask */
/**
* @brief Function implementing the UartTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUartTask */
void StartUartTask(void *argument)
{
  /* USER CODE BEGIN StartUartTask */
  UartTask_run(argument);   /* implementace ve freertos_task_uart.c (regen-safe) */
  /* USER CODE END StartUartTask */
}

/* USER CODE BEGIN Header_StartI2C4 */
/**
* @brief Function implementing the I2C4Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartI2C4 */
void StartI2C4(void *argument)
{
  /* USER CODE BEGIN StartI2C4 */
  SensorsTask_run(argument);   /* implementace ve freertos_task_sensors.c (regen-safe) */
  /* USER CODE END StartI2C4 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* USER CODE END Application */
