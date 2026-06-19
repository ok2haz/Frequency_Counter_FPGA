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
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "ft5x06.h"
#include "fpga_freq.h"
#include "ads1115.h"
#include "si5356.h"
#include "app_gpsdo.h"        /* GPSDO obrazovka z primitiv (libprim/libui) */
#include "i2c.h"
#include <stdbool.h>
#include <math.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define RX_BUF_SIZE 32
#define RAM_BASE  0x30000000UL
#define SDRAM_BASE  0xC0000000UL
#define TEST_OFFSET 0x00001000UL   // RAM_D2 test (bezpecne mimo struktury)
#define SDRAM_TEST_OFFSET 0x001C0000UL  // SDRAM test: ZA FB1+FB2 (0xC01C0000), uvnitr 2MB WT regionu -> neprepisuje framebuffer
#define TEST_SIZE   1024           // 1024 × uint32_t = 4 kB

// Definice pro TMP117
#define TMP117_ADDR         (0x48 << 1)
#define TMP117_REG_TEMP     0x00
#define TMP117_RESOLUTION   0.0078125f

#define LCD_WIDTH   800
#define LCD_HEIGHT  480
extern DSI_HandleTypeDef hdsi;
extern LTDC_HandleTypeDef hltdc;

/*
 * MAKE_RGB - RGB565 (16bpp): R[5] G[6] B[5]. Bere 8-bit slozky a oreze na 5/6/5.
 * Spravne barvy na displeji pri DSI BURST + RGB565 (viz dsihost.c).
 *   uint16_t red   = MAKE_RGB(0xFF, 0x00, 0x00);  -> displej cervena
 *   uint16_t green = MAKE_RGB(0x00, 0xFF, 0x00);  -> displej zelena
 *   uint16_t blue  = MAKE_RGB(0x00, 0x00, 0xFF);  -> displej modra
 */
#define MAKE_RGB(r, g, b)  ((uint16_t)((((uint16_t)(r) & 0xF8) << 8) | \
                           (((uint16_t)(g) & 0xFC) << 3) | \
                           (((uint16_t)(b) & 0xF8) >> 3)))

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
char RxBuffer[RX_BUF_SIZE];
uint8_t RxIndex = 0;

uint32_t *ram_buf = (uint32_t *)(RAM_BASE + TEST_OFFSET);
uint32_t *sdram_buf = (uint32_t *)(SDRAM_BASE + SDRAM_TEST_OFFSET);

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

/* Semafor pro asynchronni (IT) I2C4 cteni — pozustatek po starem touch UI.
 * Nova obrazovka cte dotek pollovanim (ft5x06_read_touch), takze nevyuzity. */
osSemaphoreId_t i2cDoneSemHandle;

/* Pozadavek na obrazovku: UART nastavi, UiTask obslouzi (libprim/libui neni
 * thread-safe -> kresli VYHRADNE UiTask). 3 = "screen main", 4 = "clear". */
volatile uint8_t g_screen_req  = 0;
/* Pozustatky po starem gfx/LVGL UI — uz se necti (mrtve, lze pozdeji odebrat
 * i s jejich zapisy v prikazu "ui"). */
volatile uint8_t g_ui_enabled = 1;
volatile uint8_t g_screen_mode = 0;
volatile uint8_t g_prim_mode   = 0;
volatile uint8_t g_ui_calreq  = 0;

/* Naformatovany kmitocet z FPGA (FpgaTask zapise, UiTask vykresli) */
volatile char    g_freq_text[48] = "----------,-----Hz";
volatile char    g_freq_info[64] = "";                 /* vedlejsi udaje (gate/edges/ch) */
volatile uint8_t g_freq_dirty = 1;
volatile uint8_t g_freq_stale = 0;                     /* 1 = SEQ se dlouho nehnula (mozna ztrata signalu) -> UI ztlumi */

/* Stav SPI + komunikace s FPGA (FpgaTask zapise, UiTask vykresli) */
volatile char    g_spi_text[64] = "SPI WAIT";
volatile uint8_t g_spi_ok    = 0;                      /* 1 = link ziva -> zelene */
volatile uint8_t g_spi_dirty = 1;

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
/* Definitions for UiTask (dotykove UI) */
osThreadId_t UiTaskHandle;
const osThreadAttr_t UiTask_attributes = {
  .name = "UiTask",
  .stack_size = 2048 * 4,                /* 8 KB: LVGL v9 render plne obrazovky se do 4 KB nevejde */
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for FpgaTask (SPI2 citac kmitoctu) */
osThreadId_t FpgaTaskHandle;
const osThreadAttr_t FpgaTask_attributes = {
  .name = "FpgaTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
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
  i2cDoneSemHandle = osSemaphoreNew(1, 0, NULL);   /* binarni, init 0 */
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
	uint8_t rxChar;
	extern I2C_HandleTypeDef hi2c4;


	printf("UART task ready\n");

	HAL_UART_Receive_IT(&huart1, &RxByte, 1);
  /* Infinite loop */
  for(;;)
  {
	  if (osMessageQueueGet(UartRxQueueHandle, &rxChar, NULL, osWaitForever) == osOK) {		// cte zpravu - znak UartRxQueueHandle
	      if (rxChar == '\b' || rxChar == 0x7F) {				// Vizuální smazání znaku v terminálu:
	    	  if (RxIndex > 0) {
	    		  RxIndex--;
	    		  printf("\b \b");								// \b (zpět), mezera (přemazání), \b (zpět na novou pozici)
	    	  }
	      } else if (rxChar == '\r' || rxChar == '\n' ) {		// kdyz je Enter, bude zkoumat obsah zpravy
	    	  printf("\n");									// Echo Enteru
			  RxBuffer[RxIndex] = '\0';							// na konec zpravy da 0, aby fungovalo porovnavani funkci strcmp
			  RxIndex = 0;

			  if (strcmp(RxBuffer, "led on") == 0) {
				  HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_RESET);
				  printf("LED ON - OK \n");
			  }
			  else if (strcmp(RxBuffer, "led off") == 0) {
				  HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_SET);
				  printf("LED OFF - OK \n");
			  }
			  else if (strcmp(RxBuffer, "ram write") == 0) {
				  for (size_t i = 0; i < 20000; i++) {
					  ram_buf[i] = 0xA5A50000UL | i;
				  }
				  printf("RAM WRITE - OK \n");
			  }
			  else if (strcmp(RxBuffer, "ram read") == 0) {
				  for (size_t i = 0; i < 400; i++) {
					  printf("Adresa: 0x%08lX, Data: 0x%08lX \n", (unsigned long)i, (unsigned long)ram_buf[i]);
					  osDelay(2);
				  }
				  printf("RAM READ - OK \n");
			  }
			  else if (strcmp(RxBuffer, "sdram write") == 0) {
				  for (size_t i = 0; i < 200000; i++) {
					  sdram_buf[i] = 0xA5A50000UL | i;
				  }
				  printf("SDRAM WRITE - OK \n");
			  }
			  else if (strcmp(RxBuffer, "sdram read") == 0) {
				  for (size_t i = 0; i < 20000; i++) {
					  printf("Adresa: 0x%08lX, Data: 0x%08lX \n", (unsigned long)i, (unsigned long)sdram_buf[i]);
					  osDelay(2);
				  }
				  printf("SDRAM READ - OK \n");
			  }
			  else if (strcmp(RxBuffer, "temperature") == 0) {
				  int32_t celcast = (int32_t)g_CurrentTemperature;
				  int32_t descast = (int32_t)((g_CurrentTemperature - celcast) * 100);
				  printf("TEPLOTA: %ld.%02ld C \n", celcast, descast);
			  }
			  else if (strcmp(RxBuffer, "scanner") == 0) {
				  uint8_t devices_found = 0;
				  uint8_t result = 0;

				  for (uint16_t i = 1; i < 128; i++) {
					  result = HAL_I2C_IsDeviceReady( &hi2c4, (uint16_t)(i << 1), 3, 10);
					  if (result == HAL_OK) {
						  printf( "Adresa zarizeni: 0x%02X (7-bit) | 0x%02X (8-bit)\n", i, (i << 1));
						  devices_found++;
					  } else if (result == HAL_BUSY) {
						  printf("Sbernice I2C je zaneprazdnena (Adresa 0x%02X)!\n", i);
					  } else {
						  printf("Adresa zarizeni: 0x%02X nic nedela\n", i);
					  }
					  osDelay(2);
				  }
				  if (devices_found == 0) {
					  printf("Zadne I2C zarizeni nenalezeno. \n");
				  } else {
					  printf("Skenovani dokonceno. Pocet zarizeni: %d\n", devices_found);
				  }
			  }
			  else if (strcmp(RxBuffer, "testDSI") == 0) {
				  uint8_t id_bytes[3] = {0};
				  HAL_StatusTypeDef status;
				  printf("Odesilam DCS Short Read (0x04)...\r\n");
				  status = HAL_DSI_Read(&hdsi, 0, id_bytes, 3, DSI_DCS_SHORT_PKT_READ, 0x04, NULL);
				  if (status == HAL_OK) {
				      printf("ID Displeje: %02X %02X %02X\n", id_bytes[0], id_bytes[1], id_bytes[2]);
				  } else if (status == HAL_TIMEOUT) {
				      printf("Chyba: Timeout (Displej neodpovídá na BTA)\n");
				  } else {
				      printf("Chyba pri cteni: %d\n", status);
				  }


			  }

			  else if (strcmp(RxBuffer, "testRED") == 0) {
				  uint16_t *fb = (uint16_t *)0xC0000000;
				  uint16_t red = MAKE_RGB(0xFF, 0x00, 0x00);
				  for(int i = 0; i < (LCD_WIDTH * LCD_HEIGHT); i++) {
					  fb[i] = red;
				  }
				  /* Vyhodit cache, aby LTDC videl ciste data v SDRAM (RGB565 = 2 byty/px) */
				  SCB_CleanDCache_by_Addr((uint32_t*)0xC0000000, LCD_WIDTH * LCD_HEIGHT * 2);
				  printf("TEST RED - OK (RGB565)\n");
			  }

			  else if (strcmp(RxBuffer, "test") == 0) {
				  /* 3 svisle pruhy R/G/B podle X (kazdy radek obsahuje 3 ruzne barvy) */
				  uint16_t *pixelPtr = (uint16_t *)0xC0000000;
				  uint16_t red   = MAKE_RGB(0xFF, 0x00, 0x00);
				  uint16_t green = MAKE_RGB(0x00, 0xFF, 0x00);
				  uint16_t blue  = MAKE_RGB(0x00, 0x00, 0xFF);

				  for (uint32_t y = 0; y < LCD_HEIGHT; y++) {
				      for (uint32_t x = 0; x < LCD_WIDTH; x++) {
				          if (x < 266)      *pixelPtr = red;     // Cervena tretina
				          else if (x < 533) *pixelPtr = green;   // Zelena tretina
				          else              *pixelPtr = blue;    // Modra tretina

				          pixelPtr++;
				      }
				  }
				  /* Vyhodit cache, aby LTDC videl ciste data v SDRAM (RGB565 = 2 byty/px) */
				  SCB_CleanDCache_by_Addr((uint32_t*)0xC0000000, LCD_WIDTH * LCD_HEIGHT * 2);
				  printf("TEST - OK (3 svisle pruhy podle X, RGB565)\n");
			  }

			  else if (strcmp(RxBuffer, "touch") == 0) {
				  /* Jednorazove cteni stavu doteku */
				  extern I2C_HandleTypeDef hi2c4;
				  ft5x06_touch_t t;
				  int ok = 0;
				  if (osMutexAcquire(i2c4MutexHandle, osWaitForever) == osOK) {
					  ok = ft5x06_read_touch(&hi2c4, &t);
					  osMutexRelease(i2c4MutexHandle);
				  }
				  if (!ok) {
					  printf("TOUCH: I2C cteni selhalo\n");
				  } else if (!t.valid) {
					  printf("TOUCH: zadny dotek (num=%u)\n", t.num_touches);
				  } else {
					  const char *evt = (t.event == 0) ? "DOWN" :
					                    (t.event == 1) ? "UP"   :
					                    (t.event == 2) ? "CONTACT" : "NONE";
					  printf("TOUCH: X=%u Y=%u event=%s id=%u num=%u\n",
					         t.x, t.y, evt, t.id, t.num_touches);
				  }
			  }

			  else if (strcmp(RxBuffer, "touchloop") == 0) {
				  /* Polling po dobu 15 sekund. Tisne se KAZDA zmena stavu.
				   * Polling kazdych 50 ms (= ~300 vzorku za 15s). */
				  extern I2C_HandleTypeDef hi2c4;
				  printf("TOUCHLOOP: 15s polling, dotykej se displeje...\n");
				  ft5x06_touch_t last = {0};
				  for (int i = 0; i < 300; i++) {
					  ft5x06_touch_t t;
					  int ok = 0;
					  if (osMutexAcquire(i2c4MutexHandle, 50) == osOK) {
						  ok = ft5x06_read_touch(&hi2c4, &t);
						  osMutexRelease(i2c4MutexHandle);
					  }
					  if (ok) {
						  /* Tisknout pouze pri zmenach (down/up nebo posun > 5 pixelu) */
						  bool changed = (t.valid != last.valid);
						  if (t.valid && last.valid) {
							  int dx = (int)t.x - (int)last.x;
							  int dy = (int)t.y - (int)last.y;
							  if (dx > 5 || dx < -5 || dy > 5 || dy < -5) changed = true;
						  }
						  if (changed) {
							  if (t.valid) {
								  printf("[%4d] X=%u Y=%u\n", i*50, t.x, t.y);
							  } else {
								  printf("[%4d] UP\n", i*50);
							  }
							  last = t;
						  }
					  }
					  vTaskDelay(pdMS_TO_TICKS(50));
				  }
				  printf("TOUCHLOOP: konec\n");
			  }

			  else if (strcmp(RxBuffer, "scan1") == 0) {
				  printf("=== I2C1 scan (FPGA deska) ===\n");
				  uint8_t found = 0;
				  for (uint16_t i = 1; i < 128; i++) {
					  HAL_StatusTypeDef r = HAL_BUSY;
					  if (osMutexAcquire(i2c1MutexHandle, 50) == osOK) {
						  r = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(i << 1), 2, 5);
						  osMutexRelease(i2c1MutexHandle);
					  }
					  if (r == HAL_OK) {
						  const char *n = (i == 0x48) ? " ADS1115" :
						                  (i == 0x49 || i == 0x4A) ? " TMP117" :
						                  (i == 0x70 || i == 0x71) ? " Si5356A" : "";
						  printf("I2C1 0x%02X%s\n", i, n);
						  found++;
					  }
					  osDelay(1);
				  }
				  printf("I2C1 hotovo, zarizeni: %d\n", found);
			  }
			  else if (strcmp(RxBuffer, "ui") == 0) {
				  g_screen_mode = 0;          /* opustit LVGL screen-mode -> gfx UI */
					  g_prim_mode = 0;            /* opustit i primitives screen */
				  g_ui_enabled = 1;
				  g_screen_req = 3;   /* ui = znovu vykreslit hlavni obrazovku */
					  printf("OK\r\n");
			  }
			  else if (strcmp(RxBuffer, "ping") == 0) {
				  printf("pong\r\n");
			  }
			  else if (strcmp(RxBuffer, "screen main") == 0) {
				  g_screen_req = 3;           /* UiTask vykresli (libprim/libui neni thread-safe) */
				  printf("OK\r\n");
			  }
			  else if (strcmp(RxBuffer, "clear") == 0) {
				  g_screen_req = 4;
				  printf("OK\r\n");
			  }
				  else if (strcmp(RxBuffer, "version") == 0) {
				  printf("gpsdo-ui v0.1\r\n");
			  }
			  else if (strcmp(RxBuffer, "help") == 0) {
				  printf("ping | screen main | clear | version | help | ui | freq | stats | status\r\n");
			  }
			  else if (strcmp(RxBuffer, "freq") == 0) {
				  char fbuf[48];
				  taskENTER_CRITICAL();
				  strncpy(fbuf, (const char *)g_freq_text, sizeof(fbuf) - 1);
				  fbuf[sizeof(fbuf) - 1] = '\0';
				  taskEXIT_CRITICAL();
				  printf("FREQ: %s\n", fbuf);
			  }
			  else if (strcmp(RxBuffer, "si5356") == 0) {
				  /* Re-init Si5356A (aplikuje register map) + vypise status. */
				  if (osMutexAcquire(i2c1MutexHandle, 300) == osOK) {
					  si5356_init(&hi2c1);
					  osMutexRelease(i2c1MutexHandle);
				  } else {
					  printf("Si5356A: I2C1 busy\n");
				  }
			  }
			  else if (strcmp(RxBuffer, "fpgaloop") == 0) {
				  /* Hustá série přenosů pro scope/LA (trigger na CS↓). ~3 s. */
				  printf("FPGA loop: ~2000 prenosu (scope na pinech FPGA: CS56/SCK55/MISO57)...\n");
				  uint8_t rx[64];
				  for (int n = 0; n < 2000; n++) {
					  fpga_freq_raw_xfer(rx);
					  osDelay(1);
				  }
				  printf("FPGA loop: hotovo (posl. RX0=0x%02X)\n", rx[0]);
			  }
			  else if (strcmp(RxBuffer, "fpgaraw") == 0) {
				  /* Bring-up diagnostika: jeden prenos + vypis vsech 64 prijatych bajtu.
				   * Cekame: byte0=A5 byte1=01 byte2=80. Same FF/00 = MISO nebudi (FPGA mlci). */
				  uint8_t rx[64];
				  bool ok = fpga_freq_raw_xfer(rx);
				  printf("FPGA raw xfer HAL:%s\n", ok ? "OK" : "ERR");
				  char line[64];
				  int p = 0;
				  for (int i = 0; i < 64; i++) {
					  p += snprintf(line + p, sizeof(line) - p, "%02X ", rx[i]);
					  if ((i & 0xF) == 0xF) { printf("[%02d] %s\n", i - 15, line); p = 0; }
				  }
			  }
			  else if (strcmp(RxBuffer, "stats") == 0) {
				  static TaskStatus_t ta[12], tb[12];
				  uint32_t t0 = 0, t1 = 0;
				  UBaseType_t na = uxTaskGetSystemState(ta, 12, &t0);
				  osDelay(1000);
				  UBaseType_t nb = uxTaskGetSystemState(tb, 12, &t1);
				  uint32_t total = t1 - t0;
				  if (total == 0) total = 1;
				  printf("=== CPU za 1s (DWT @480MHz) ===\n");
				  printf("TASK             CPU  STACK_FREE\n");
				  for (UBaseType_t i = 0; i < nb; i++) {
					  uint32_t prev = 0;
					  for (UBaseType_t j = 0; j < na; j++)
						  if (ta[j].xHandle == tb[i].xHandle) { prev = ta[j].ulRunTimeCounter; break; }
					  uint32_t d = tb[i].ulRunTimeCounter - prev;
					  uint32_t pct = (uint32_t)(((uint64_t)d * 100u) / total);
					  printf("%-16s %2lu%%  %lu B\n", tb[i].pcTaskName,
							 (unsigned long)pct, (unsigned long)(tb[i].usStackHighWaterMark * 4u));
				  }
			  }
			  else if (strcmp(RxBuffer, "status") == 0)  {
				  printf("RUNNING\n");
			  }
			  else {
				  printf("ERR unknown command\r\n");
			  }
		  }
		  else {
			  if (RxIndex < sizeof(RxBuffer) - 1) {					// prijaty znak neni Enter, tak jej ulozi do zpravy
				  putchar(rxChar);
				  fflush(stdout);									// Vynutíme zobrazení bez čekání na \n
				  RxBuffer[RxIndex++] = rxChar;
			  }
		  }
	  }
    osDelay(1);
  }
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
  extern I2C_HandleTypeDef hi2c4;
  uint8_t rawData[2];
  int16_t tempRaw;

  // Inicializace času pro přesné vzorkování
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(1000); // 1 sekunda
  xLastWakeTime = xTaskGetTickCount();

  for(;;) {
	// Čtení 2 bytů z registru 0x00 (Temperature Register)
	// TMP117 automaticky inkrementuje nebo drží pointer, ale HAL_I2_Mem_Read je nejjistější
	// Mutex: I2C4 sdili dotykove UI (FT5x06) + backlight
	HAL_StatusTypeDef i2cStatus = HAL_ERROR;
	if (osMutexAcquire(i2c4MutexHandle, osWaitForever) == osOK) {
	  i2cStatus = HAL_I2C_Mem_Read( &hi2c4, TMP117_ADDR, TMP117_REG_TEMP, I2C_MEMADD_SIZE_8BIT, rawData, 2, 100);
	  osMutexRelease(i2c4MutexHandle);
	}
	if (i2cStatus == HAL_OK) {
	  // Konverze: MSB je v rawData[0], LSB v rawData[1]
	  tempRaw = (int16_t)((rawData[0] << 8) | rawData[1]);

	  // Výpočet na stupně Celsia
	  // Používáme float pro zachování přesnosti 0.0078°C
	g_CurrentTemperature = (float)tempRaw * TMP117_RESOLUTION;

	  /* Deterministický výpočet:
	  Teplota = (Hodnota_v_registru) * 0.0078125 */
	} else {
	  // Heuristika: Zde by měl následovat error handling (např. logování nebo re-inicializace I2C)
	  // Prozatím signalizujeme chybu např. hodnotou mimo rozsah
	  // g_currentTemperature = -999.0f;
	}
    // Čekání na další cyklus (přesně 1s od posledního probuzení)
	// === FPGA deska na I2C1: TMP117 0x49/0x4A + ADS1115 4 kanaly ===
	if (osMutexAcquire(i2c1MutexHandle, 100) == osOK) {
	  uint8_t tb[2];
	  if (HAL_I2C_Mem_Read(&hi2c1, (0x49 << 1), 0x00, I2C_MEMADD_SIZE_8BIT, tb, 2, 50) == HAL_OK)
		g_temp49 = (float)((int16_t)((tb[0] << 8) | tb[1])) * TMP117_RESOLUTION;
	  if (HAL_I2C_Mem_Read(&hi2c1, (0x4A << 1), 0x00, I2C_MEMADD_SIZE_8BIT, tb, 2, 50) == HAL_OK)
		g_temp4A = (float)((int16_t)((tb[0] << 8) | tb[1])) * TMP117_RESOLUTION;
	  osMutexRelease(i2c1MutexHandle);
	}
	for (uint8_t ch = 0; ch < 4; ch++) {
	  int started = 0;
	  if (osMutexAcquire(i2c1MutexHandle, 100) == osOK) {
		started = ads1115_start(&hi2c1, ch);
		osMutexRelease(i2c1MutexHandle);
	  }
	  if (started) {
		osDelay(9);   /* 128 SPS -> ~7.8 ms konverze (task spi) */
		int16_t raw;
		if (osMutexAcquire(i2c1MutexHandle, 100) == osOK) {
		  if (ads1115_read_raw(&hi2c1, &raw)) {
			    int32_t mv = ads1115_raw_to_mv(raw);
			    /* AIN2 = 12V vetev pres odporovy delic (real 13.417V @ 2.814V na ADS),
			       AIN3 = 5V vetev pres delic (real 4.978V @ 2.526V na ADS) -> skutecne napeti */
			    if      (ch == 2) mv = (int32_t)((int64_t)mv * 13417 / 2814);
			    else if (ch == 3) mv = (int32_t)((int64_t)mv * 4978  / 2526);
			    g_ads_mv[ch] = mv;
			  }
		  osMutexRelease(i2c1MutexHandle);
		}
	  }
	}
	vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
  /* USER CODE END StartI2C4 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* === Run-time stats casova baze: DWT cycle counter (480 MHz) === */
void RunTimeStats_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
uint32_t RunTimeStats_GetCount(void)
{
  return DWT->CYCCNT;
}


/**
 * @brief GPSDO UI task: kresli obrazovku z primitiv (libprim/libui) a obsluhuje
 *        dotyk tlacitek. Bezi VYHRADNE zde (knihovny nejsou thread-safe).
 */
void StartUiTask(void *argument)
{
  (void)argument;

  /* Boot rovnou do GPSDO obrazovky z primitiv (libprim/libui). Ta se kresli
   * VYHRADNE z tohoto tasku (knihovny nejsou thread-safe). Stary gfx/touch UI
   * byl odstranen — po startu bezi jen tato obrazovka. */
  g_screen_req = 3;

  for (;;) {
    /* UART prikazy "screen main"/"clear"/"ui" nastavi g_screen_req; zde se
     * obslouzi (req 3 = hlavni obrazovka, 4 = smazani). */
    uint8_t req = g_screen_req;
    if (req) {
      g_screen_req = 0;
      HAL_LTDC_SetAddress(&hltdc, 0xC0000000u, 0);   /* LTDC scanuje nas FB */
      if (req == 4) app_gpsdo_clear();
      else          app_gpsdo_render_main();
    }

    /* Dotek pro tlacitka (FT5x06 @ I2C4, pod sdilenym mutexem). Panel je
     * zrcadleny v X i Y -> screen = (799-x, 479-y). Hranove spousteni
     * (reaguje jen na nabeznou hranu doteku, ne na drzeni). */
    static uint8_t was_down = 0;
    ft5x06_touch_t t;
    if (osMutexAcquire(i2c4MutexHandle, 20) == osOK) {
      uint8_t ok = ft5x06_read_touch(&hi2c4, &t);
      osMutexRelease(i2c4MutexHandle);
      if (ok && t.valid && !was_down) {
        app_gpsdo_handle_touch((int16_t)(799 - t.x), (int16_t)(479 - t.y));
      }
      was_down = (uint8_t)(ok && t.valid);
    }

    /* Obnova zivych hodnot v diagnostice ~2x/s (na hlavni obrazovce no-op). */
    static uint32_t last_tick = 0;
    if (HAL_GetTick() - last_tick >= 500) {
      last_tick = HAL_GetTick();
      app_gpsdo_tick();
    }

    osDelay(40);
  }
}

/**
 * @brief SPI2 citac kmitoctu z FPGA. Inicializuje SPI, polluje ~10 Hz,
 *        nove mereni naformatuje do g_freq_text pro UiTask.
 */
void StartFpgaTask(void *argument)
{
  /* Kontrakt bod 1/8: nebudit SPI piny, dokud FPGA nedokonci konfiguraci z flash
   * po power-on/resetu. CS uz drzime vysoko (idle); jeste pridame prodlevu, aby
   * GW1NR-9 stihl nabootovat drive, nez na nej zacneme clockovat. */
  osDelay(250);

  fpga_freq_init();

  fpga_meas_t m;
  uint32_t fails = 0;
  uint8_t  lost  = 0;
  for (;;) {
    if (fpga_freq_poll(&m)) {
      fails = 0;
      /* Vyber zobrazovany zdroj: /4 (nejlepsi rozliseni) dokud bez chyby a pod ~380 MHz,
       * jinak /16 (vyssi rozsah). freq*_x100000 uz ma delicku zahrnutou -> jen /100000. */
      int use16 = 0;
      uint64_t v = fpga_freq_select(&m, &use16);
      char buf[48], ibuf[64];
      fpga_freq_format_val(v, buf, sizeof(buf));
      fpga_freq_format_info(&m, use16, ibuf, sizeof(ibuf));
      taskENTER_CRITICAL();
      strncpy((char *)g_freq_text, buf, sizeof(g_freq_text) - 1);
      g_freq_text[sizeof(g_freq_text) - 1] = '\0';
      strncpy((char *)g_freq_info, ibuf, sizeof(g_freq_info) - 1);
      g_freq_info[sizeof(g_freq_info) - 1] = '\0';
      g_freq_dirty = 1;
      taskEXIT_CRITICAL();
    } else if (!fpga_freq_link_ok()) {
      /* zadny platny ramec -> FPGA mozna bootl pozdeji / resetoval; po ~3 s znovu START
       * (20 Hz polling -> 60 iteraci = ~3 s) */
      if (++fails >= 60) { fpga_freq_restart(); fails = 0; }
    } else {
      fails = 0;   /* link zije, jen zatim neni nove mereni */
    }

    /* Ztrata signalu: autoritativni z FPGA (error_flags bit1 SIGNAL_LOST, watchdog ~2.5 s)
     * nebo mrtvy link. Nahrazuje drivejsi SEQ-staleness heuristiku - ta falesne hlasila
     * stale u nizkych kmitoctu, kde se reciproke okno legitimne protahne. Pri ztrate UI
     * ztlumi kmitocet na sedou. */
    uint8_t l = (!fpga_freq_link_ok() || fpga_freq_signal_lost()) ? 1 : 0;
    if (l != lost) {
      lost = l;
      taskENTER_CRITICAL();
      g_freq_stale = l;
      g_freq_dirty = 1;     /* prekreslit kmitocet v jine barve (ztlumeny / zluty) */
      taskEXIT_CRITICAL();
    }

    /* Stav SPI/komunikace -> displej (prekreslit jen pri zmene textu/linky) */
    char sbuf[64];
    fpga_freq_format_status(sbuf, sizeof(sbuf));
    uint8_t ok = fpga_freq_link_ok() ? 1 : 0;
    taskENTER_CRITICAL();
    if (ok != g_spi_ok || strncmp((const char *)g_spi_text, sbuf, sizeof(g_spi_text)) != 0) {
      strncpy((char *)g_spi_text, sbuf, sizeof(g_spi_text) - 1);
      g_spi_text[sizeof(g_spi_text) - 1] = '\0';
      g_spi_ok = ok;
      g_spi_dirty = 1;
    }
    taskEXIT_CRITICAL();

    osDelay(50);   /* ~20 Hz cteni */
  }
}

/* ──────────────────────────────────────────────────────────────────────────
 *  FreeRTOS diagnostické hooky (configCHECK_FOR_STACK_OVERFLOW=2,
 *  configUSE_MALLOC_FAILED_HOOK=1). Drive tiché přepsání paměti → teď se to
 *  zachytí. Globály jsou viditelné v debuggeru; po zachycení se zastaví IRQ a
 *  zacyklí (breakpoint sem), aby chyba nezmizela.
 * ────────────────────────────────────────────────────────────────────────── */
volatile char     g_overflow_task[configMAX_TASK_NAME_LEN] = {0};
volatile uint32_t g_malloc_failed = 0;

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  for (int i = 0; i < configMAX_TASK_NAME_LEN - 1 && pcTaskName[i]; i++) {
    g_overflow_task[i] = pcTaskName[i];
  }
  __disable_irq();
  for (;;) { /* breakpoint zde: g_overflow_task = jméno tasku co přetekl stack */ }
}

void vApplicationMallocFailedHook(void)
{
  g_malloc_failed++;
  __disable_irq();
  for (;;) { /* breakpoint zde: došel FreeRTOS heap (configTOTAL_HEAP_SIZE) */ }
}

/* USER CODE END Application */
