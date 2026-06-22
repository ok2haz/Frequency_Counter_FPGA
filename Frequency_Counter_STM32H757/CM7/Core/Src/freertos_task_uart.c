/*
 * freertos_task_uart.c
 *
 * UART command processor (UartTask_run, volaná ze StartUartTask stubu) — vyčleněno z freertos.c.
 * Čte znaky z UartRxQueue, skládá řádky a vyhodnocuje příkazy. Seznam příkazů
 * viz CLAUDE.md "UART příkazy".
 */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "usart.h"        /* huart1, RxByte */
#include "i2c.h"          /* hi2c1, hi2c4 */
#include "ft5x06.h"
#include "fpga_freq.h"
#include "si5356.h"
#include "freertos_shared.h"

/* ── Lokální makra (jen pro tento task) ────────────────────────────────── */
#define RX_BUF_SIZE       32
#define RAM_BASE          0x30000000UL
#define SDRAM_BASE        0xC0000000UL
#define TEST_OFFSET       0x00001000UL   /* RAM_D2 test (bezpecne mimo struktury) */
#define SDRAM_TEST_OFFSET 0x00400000UL   /* SDRAM test @0xC0400000 = MPU region 1 (WB
                                          * scratch), MIMO triple-buffer region 0 (4MB)
                                          * FB0/FB1/FB2/canvas. Drive bylo 0x1C0000 = uvnitr
                                          * region 0 -> kolidovalo by s FB1/FB2. */

#define LCD_WIDTH         800
#define LCD_HEIGHT        480
#define FB_ADDR           0xC0000000u   /* framebuffer @ SDRAM (RGB565), shodne s PRIM_FB_ADDR */

/*
 * MAKE_RGB - RGB565 (16bpp): R[5] G[6] B[5]. Bere 8-bit slozky a oreze na 5/6/5.
 * Spravne barvy na displeji pri DSI BURST + RGB565 (viz dsihost.c).
 */
#define MAKE_RGB(r, g, b)  ((uint16_t)((((uint16_t)(r) & 0xF8) << 8) | \
                           (((uint16_t)(g) & 0xFC) << 3) | \
                           (((uint16_t)(b) & 0xF8) >> 3)))

extern DSI_HandleTypeDef hdsi;   /* prikaz testDSI */

/* Format float na 2 desetinna mista bez %f (nano printf nemusi umet float). */
static void fmt_f2(char *b, size_t n, float v)
{
	long w = (long)v;
	long f = (long)((v - (float)w) * 100.0f);
	if (f < 0) f = -f;
	snprintf(b, n, "%ld.%02ld", w, f);
}

/* ── Stav UART command procesoru (privátní pro tento task) ─────────────── */
char RxBuffer[RX_BUF_SIZE];
uint8_t RxIndex = 0;

uint32_t *ram_buf   = (uint32_t *)(RAM_BASE + TEST_OFFSET);
uint32_t *sdram_buf = (uint32_t *)(SDRAM_BASE + SDRAM_TEST_OFFSET);

/* Volano ze StartUartTask stubu ve freertos.c (CubeMX-regen-safe). */
void UartTask_run(void *argument)
{
	uint8_t rxChar;


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
				  const sensor_stat_t *s = &g_sensors[SENS_T48];
				  char v[16];
				  fmt_f2(v, sizeof(v), s->last);
				  printf("TEPLOTA: %s C%s\n", v, s->valid ? "" : " (STALE - chyba cteni)");
			  }
			  else if (strcmp(RxBuffer, "sensors") == 0) {
				  static const char *const nm[SENS_COUNT] = {
					  "TMP117 0x48", "TMP117 0x49", "TMP117 0x4A",
					  "ADS AIN0", "ADS AIN1", "ADS AIN2(12V)", "ADS AIN3(5V)" };
				  static const char *const un[SENS_COUNT] = {
					  "C", "C", "C", "mV", "mV", "mV", "mV" };
				  printf("=== SENZORY: last/min/max/avg [unit] stav  chyby ===\n");
				  for (int i = 0; i < SENS_COUNT; i++) {
					  const sensor_stat_t *s = &g_sensors[i];
					  char a[16], b[16], c[16], d[16];
					  fmt_f2(a, sizeof(a), s->last);
					  fmt_f2(b, sizeof(b), s->min);
					  fmt_f2(c, sizeof(c), s->max);
					  fmt_f2(d, sizeof(d), s->mean);
					  printf("%-13s %s/%s/%s/%s %s  %s  err=%lu strk=%u n=%lu\n",
						     nm[i], a, b, c, d, un[i], s->valid ? "OK " : "ERR",
						     (unsigned long)s->err_total, (unsigned)s->err_streak,
						     (unsigned long)s->samples);
					  osDelay(2);
				  }
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
				  uint16_t *fb = (uint16_t *)FB_ADDR;
				  uint16_t red = MAKE_RGB(0xFF, 0x00, 0x00);
				  for(int i = 0; i < (LCD_WIDTH * LCD_HEIGHT); i++) {
					  fb[i] = red;
				  }
				  /* Vyhodit cache, aby LTDC videl ciste data v SDRAM (RGB565 = 2 byty/px) */
				  SCB_CleanDCache_by_Addr((uint32_t*)FB_ADDR, LCD_WIDTH * LCD_HEIGHT * 2);
				  printf("TEST RED - OK (RGB565)\n");
			  }

			  else if (strcmp(RxBuffer, "test") == 0) {
				  /* 3 svisle pruhy R/G/B podle X (kazdy radek obsahuje 3 ruzne barvy) */
				  uint16_t *pixelPtr = (uint16_t *)FB_ADDR;
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
				  SCB_CleanDCache_by_Addr((uint32_t*)FB_ADDR, LCD_WIDTH * LCD_HEIGHT * 2);
				  printf("TEST - OK (3 svisle pruhy podle X, RGB565)\n");
			  }

			  else if (strcmp(RxBuffer, "touch") == 0) {
				  /* Jednorazove cteni stavu doteku */
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
				  printf("gpsdo-ui v0.2-diag\r\n");
			  }
			  else if (strcmp(RxBuffer, "help") == 0) {
				  printf("ping | screen main | clear | version | help | ui | freq | stats | status | sensors | temperature\r\n");
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
				  printf("=== STATS (CPU za 1s, DWT@480MHz) ===\n");
				  printf("TASK             CPU PRI STACKFREE ST\n");
				  uint32_t idle_pct = 0;
				  for (UBaseType_t i = 0; i < nb; i++) {
					  uint32_t prev = 0;
					  for (UBaseType_t j = 0; j < na; j++)
						  if (ta[j].xHandle == tb[i].xHandle) { prev = ta[j].ulRunTimeCounter; break; }
					  uint32_t d = tb[i].ulRunTimeCounter - prev;
					  uint32_t pct = (uint32_t)(((uint64_t)d * 100u) / total);
					  /* stav: X=run R=ready B=blocked S=susp D=deleted */
					  char st = "XRBSD?"[(tb[i].eCurrentState <= eDeleted) ? tb[i].eCurrentState : 5];
					  if (tb[i].pcTaskName[0] == 'I' && tb[i].pcTaskName[1] == 'D') idle_pct = pct;
					  printf("%-16s %2lu%% %2lu  %5lu B %c\n", tb[i].pcTaskName,
							 (unsigned long)pct, (unsigned long)tb[i].uxCurrentPriority,
							 (unsigned long)(tb[i].usStackHighWaterMark * 4u), st);
					  osDelay(2);
				  }
				  printf("--- CPU load: %lu%% | tasku: %lu\n",
						 (unsigned long)(idle_pct <= 100 ? 100u - idle_pct : 0u), (unsigned long)nb);
				  printf("Heap: %lu B free, %lu B min-ever\n",
						 (unsigned long)xPortGetFreeHeapSize(),
						 (unsigned long)xPortGetMinimumEverFreeHeapSize());
				  printf("Uptime: %lu s\n", (unsigned long)(HAL_GetTick() / 1000u));
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
}
