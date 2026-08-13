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
#include <stdlib.h>       /* atoi — argument prikazu "sd export [N]" */
#include <string.h>
#include <stdbool.h>

#include "usart.h"        /* huart1, RxByte */
#include "gps.h"          /* gps_format_status (prikaz "gps") */
#include "i2c.h"          /* hi2c1, hi2c4 */
#include "ft5x06.h"
#include "fpga_freq.h"
#include "w25q.h"          /* W25Q512 QSPI flash — bring-up prikazy qspiid/qspitest */
#include "w25q_store.h"    /* genericky blob store — prikaz storetest */
#include "w25q_map.h"      /* region mapa (CONFIG/CALIB/DATA) */
#include "si5356.h"
#include "adc.h"          /* hadc3 — debug prikaz `adcraw` */
#include "freertos_shared.h"
#include "alarm.h"          /* alarm_test — UART "beep" */
#include "screens/screen_main.h"   /* screen_main_selftest — UART "selftest" */
#include "version.h"        /* FW_VERSION_FULL — UART "version" (== displej) */
#include "sd_export.h"      /* UART "sd mount/unmount/export" — SD jako export (#28) */
#include "datalog.h"        /* UART "datalog [on|off|erase|dump]" */
#include "screenshot.h"     /* UART "screenshot" — export obrazovky do BMP */
#include "autocal.h"        /* UART "autocal" — self-check / autokalibrace */
#include "scpi.h"           /* UART "scpi <cmd>" — SCPI-99 parser (#25) */

/* ── Lokální makra (jen pro tento task) ────────────────────────────────── */
#define RX_BUF_SIZE       32
/* QSPI prikazy (qspiid/qspitest/storetest/qspispeed) sahaji na W25Q, kterou sdili
 * i syscfg auto-save (defaultTask) a calib_save (UiTask) -> cela operace pod
 * qspiMutexHandle. Timeout velkorysy: prikaz je manualni diagnostika a klidne
 * pocka i na bezici erase (~400 ms/sektor). */
#define QSPI_CMD_LOCK_MS  2000u
#define RAM_BASE          0x30000000UL
#define SDRAM_BASE        0xC0000000UL
#define TEST_OFFSET       0x00001000UL   /* RAM_D2 test (bezpecne mimo struktury) */
#define SDRAM_TEST_OFFSET 0x00400000UL   /* SDRAM test @0xC0400000 = MPU region 1 (WB
                                          * scratch), MIMO triple-buffer region 0 (4MB,
                                          * FB0/FB1/FB2). Drive bylo 0x1C0000 = uvnitr
                                          * region 0 -> kolidovalo by s FB1/FB2. */

extern DSI_HandleTypeDef hdsi;   /* prikaz testDSI */

/* Task handles (definovane ve freertos.c) — volny stack v prikazu `status`. */
extern osThreadId_t defaultTaskHandle, UartTaskHandle, I2C4TaskHandle,
                    UiTaskHandle, FpgaTaskHandle;

/* Format float na 2 desetinna mista bez %f (nano printf nemusi umet float). */
static void fmt_f2(char *b, size_t n, float v)
{
	long w = (long)v;
	long f = (long)((v - (float)w) * 100.0f);
	if (f < 0) f = -f;
	snprintf(b, n, "%ld.%02ld", w, f);
}

/* ══════════════ ETH bring-up, etapa F0 — diagnostika PHY z CM7 ══════════════
 * Cil: zodpovedet dve HW neznamé JESTE PRED tim, nez se sahne na `.ioc`
 * (viz ETH_BRINGUP_CHECKLIST.md §2). Zamerne se tu tedy NIC neregeneruje a
 * NIC se nepridava do CubeMX — vsechny piny si tenhle kod konfiguruje SAM,
 * stejnym regen-safe idiomem jako CS ve `fpga_freq_init` nebo PE3 v `datalog_sd`.
 *
 * ⚠️ BRING-UP REZIDUUM (jako `fpgaraw`): bit-bang SMI je JEN pro F0. Produkcne
 * bude MDIO obsluhovat HAL z CM4 (`HAL_ETH_ReadPHYRegister`). Nemazat ale —
 * diagnostika "odpovida PHY?" nezavisla na tom, komu ETH v `.ioc` patri, je
 * pri kazdem dalsim problemu se sítí to prvni, po cem sahnes.
 *
 * PINMAPA — vytazena ze schematu `STM32H747BIT.pdf` list 4/7 (Ethernet) + 2/7
 * (CPU) dne 2026-08-13. Do te doby v repu CHYBELA (dokumenty znaly jen nazvy
 * signalu), takze tohle je jeji prvni zapis do kodu:
 *
 *   ETH_REF_CLK  PA1    ETH_MDC   PC1     ETH_TX_EN  PG11
 *   ETH_MDIO     PA2    ETH_RXD0  PC4     ETH_TXD0   PG13
 *   ETH_CRS_DV   PA7    ETH_RXD1  PC5     ETH_TXD1   PB13
 *   ETH_RES      PG14   ETH_INT   PG12
 *
 * Vsech 11 pinu je v `.ioc` VOLNYCH (overeno) a odpovida standardnimu RMII
 * mapovani STM32H7 (AF11), takze regen v F3 je bude umet priradit beze zmen.
 *
 * PHY = **LAN8742A** (U4). Strapy ze schematu: `RXER/PHYAD0` stazen k GND pres
 * R20 -> ocekavana **PHYAD = 0**. `INT/REFCLKO` (pin 14) jde pres seriovych 33 R
 * do `ETH_REF_CLK` a na `XTAL1/CLKIN` sedi externi 25 MHz oscilator -> PHY ma
 * 50 MHz REF_CLK GENEROVAT. To ale zavisi na strapu `nINTSEL` (LED2), ktery se
 * ze schematu jednoznacne precist neda — proto `eth clk`.
 *
 * Proc to jde bez 50 MHz hodin: **SMI je clockovane MDC**, tedy nezavisle na
 * RMII ref. hodinach. PHY odpovi na MDIO, i kdyby byl `nINTSEL` spatne. */
#define ETH_MDC_PORT   GPIOC
#define ETH_MDC_PIN    GPIO_PIN_1
#define ETH_MDIO_PORT  GPIOA
#define ETH_MDIO_PIN   GPIO_PIN_2
#define ETH_RES_PORT   GPIOG
#define ETH_RES_PIN    GPIO_PIN_14
#define ETH_REFCLK_PIN GPIO_PIN_1        /* PA1 */

/* DWT cyklovy citac (uz bezi kvuli runtime statistikam) — ne NOP smycka. */
static void eth_delay_us(uint32_t us)
{
	uint32_t start = DWT->CYCCNT;
	uint32_t ticks = us * (SystemCoreClock / 1000000u);
	while ((DWT->CYCCNT - start) < ticks) { __NOP(); }
}

/* MDC half-perioda. Spec dovoluje 2,5 MHz; jdeme hluboko pod to (~250 kHz),
 * protoze pri cteni drzi MDIO jen slaby vnitrni pull-up. */
#define ETH_MDC_HALF_US 2u

static void eth_mdio_dir(int out)
{
	GPIO_InitTypeDef g = {0};
	g.Pin   = ETH_MDIO_PIN;
	g.Mode  = out ? GPIO_MODE_OUTPUT_PP : GPIO_MODE_INPUT;
	g.Pull  = out ? GPIO_NOPULL : GPIO_PULLUP;
	g.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(ETH_MDIO_PORT, &g);
}

static void eth_pins_init(void)
{
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	GPIO_InitTypeDef g = {0};
	g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
	g.Pin = ETH_MDC_PIN;  HAL_GPIO_Init(ETH_MDC_PORT, &g);
	g.Pin = ETH_RES_PIN;  HAL_GPIO_Init(ETH_RES_PORT, &g);
	HAL_GPIO_WritePin(ETH_MDC_PORT, ETH_MDC_PIN, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(ETH_RES_PORT, ETH_RES_PIN, GPIO_PIN_SET);   /* reset neaktivni */
	eth_mdio_dir(1);
}

/* Jeden takt MDC. PHY vzorkuje MDIO na NABEZNE hrane, data vystavuje na sestupne
 * -> pri cteni vzorkujeme az po nabezne hrane. */
static int eth_mdc_clock(void)
{
	eth_delay_us(ETH_MDC_HALF_US);
	HAL_GPIO_WritePin(ETH_MDC_PORT, ETH_MDC_PIN, GPIO_PIN_SET);
	eth_delay_us(ETH_MDC_HALF_US);
	int bit = (HAL_GPIO_ReadPin(ETH_MDIO_PORT, ETH_MDIO_PIN) == GPIO_PIN_SET);
	HAL_GPIO_WritePin(ETH_MDC_PORT, ETH_MDC_PIN, GPIO_PIN_RESET);
	return bit;
}

static void eth_smi_write_bits(uint32_t val, int n)
{
	for (int i = n - 1; i >= 0; i--) {
		HAL_GPIO_WritePin(ETH_MDIO_PORT, ETH_MDIO_PIN,
		                  ((val >> i) & 1u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
		(void)eth_mdc_clock();
	}
}

/* IEEE 802.3 clause 22 read: PRE(32x1) ST(01) OP(10) PHYAD(5) REGAD(5) TA(Z0) DATA(16).
 * @return registr, nebo 0xFFFF kdyz PHY neodpovi (MDIO drzi pull-up nahore). */
static uint16_t eth_phy_read(uint8_t phyad, uint8_t reg)
{
	eth_mdio_dir(1);
	eth_smi_write_bits(0xFFFFFFFFu, 32);              /* preamble */
	eth_smi_write_bits(0x1u, 2);                      /* ST = 01 */
	eth_smi_write_bits(0x2u, 2);                      /* OP = 10 (read) */
	eth_smi_write_bits(phyad & 0x1Fu, 5);
	eth_smi_write_bits(reg   & 0x1Fu, 5);
	eth_mdio_dir(0);                                  /* turnaround: linku pousti host */
	(void)eth_mdc_clock();                            /* TA bit 1 (Z) */
	(void)eth_mdc_clock();                            /* TA bit 2 (PHY vystavi 0) */
	uint16_t v = 0;
	for (int i = 0; i < 16; i++) v = (uint16_t)((v << 1) | eth_mdc_clock());
	(void)eth_mdc_clock();                            /* idle */
	return v;
}

/* ── Stav UART command procesoru (privátní pro tento task) ─────────────── */
static char RxBuffer[RX_BUF_SIZE];
static uint8_t RxIndex = 0;

static uint32_t *ram_buf   = (uint32_t *)(RAM_BASE + TEST_OFFSET);
static uint32_t *sdram_buf = (uint32_t *)(SDRAM_BASE + SDRAM_TEST_OFFSET);

/* ── stacktest: ZAMERNE pretece stack UartTasku (test detekce -> IWDG reset) ──
 * ⚠️ MUSI byt VLASTNI (noinline) funkce, ne local `waste[]` v UartTask_run:
 * GCC rezervuje frame VSECH lokalu funkce uz pri vstupu, takze 3600B `waste`
 * jako local v UartTask_run nafoukl JEHO ramec na 4904 B > 4096 B stack ->
 * task pretekal VZDY (uz za normalniho provozu), ne jen pri stacktestu. To
 * zpusobovalo HardFault s poskozenym ramcem pri PRVNIM USB znaku (echo cesta
 * do USB HAL prohloubila stack az za hranice) — viz STATUS #34, 2026-07-22.
 * Jako samostatna funkce se 3600 B alokuje AZ pri volani -> ramec UartTask_run
 * spadl zpet na ~1,3 kB a task se do 4 kB v pohode vejde; pretece se JEN kdyz
 * uzivatel spusti "stacktest yes" (na to zustala funkce zachovana). */
__attribute__((noinline)) static void stacktest_overflow(void)
{
  volatile char waste[3600];
  for (unsigned i = 0; i < sizeof(waste); i++) waste[i] = (char)i;
  osDelay(1);            /* yield -> kontrola stack patternu -> hook */
  printf("STACKTEST: hook se NEOZVAL (%d) - detekce NEFUNGUJE!\n", (int)waste[0]);
}

/* Volano ze StartUartTask stubu ve freertos.c (CubeMX-regen-safe). */
void UartTask_run(void *argument)
{
	uint8_t rxChar;


	printf("UART task ready\n");

	/* USART1 RX uz nenahazujeme zde — USART1 je vyhrazen pro GPS (NEO-7M),
	 * RX nahodi gps_init() a bajty jdou do GpsRxQueue. Konzole bere RX z USB CDC
	 * (CDC_Receive_FS -> usb_console_on_rx -> UartRxQueue). */
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
				  printf("=== SENZORY: last/min/max/avg [unit] stav  chyby ===\n");
				  for (int i = 0; i < SENS_COUNT; i++) {
					  const sensor_stat_t *s = &g_sensors[i];
					  char a[16], b[16], c[16], d[16], le[16];
					  fmt_f2(a, sizeof(a), s->last);
					  fmt_f2(b, sizeof(b), s->min);
					  fmt_f2(c, sizeof(c), s->max);
					  fmt_f2(d, sizeof(d), s->mean);
					  /* "uptime od posledni chyby" — jak davno byla posledni chyba cteni. */
					  if (s->err_total) snprintf(le, sizeof(le), "%lus",
						     (unsigned long)((HAL_GetTick() - s->err_last_ms) / 1000u));
					  else              snprintf(le, sizeof(le), "-");
					  printf("%-13s %s/%s/%s/%s %s  %s  err=%lu strk=%u last=%s n=%lu\n",
						     g_sensor_desc[i].label, a, b, c, d, g_sensor_desc[i].unit, s->valid ? "OK " : "ERR",
						     (unsigned long)s->err_total, (unsigned)s->err_streak, le,
						     (unsigned long)s->samples);
					  osDelay(2);
				  }
			  }
			  else if (strcmp(RxBuffer, "scanner") == 0) {
				  uint8_t devices_found = 0;
				  uint8_t result = 0;

				  /* ⚠️ Per-adresa POD i2c4Mutex (audit 2026-07-10: drive bez mutexu ->
				   * kolize s touch pollem UiTasku na temze HAL handle). Mutex se drzi
				   * jen na jeden probe, mezi adresami se pousti (touch/TMP117 dychaji). */
				  for (uint16_t i = 1; i < 128; i++) {
					  if (osMutexAcquire(i2c4MutexHandle, 100) == osOK) {
						  result = HAL_I2C_IsDeviceReady( &hi2c4, (uint16_t)(i << 1), 3, 10);
						  osMutexRelease(i2c4MutexHandle);
					  } else {
						  result = HAL_BUSY;
					  }
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
				  printf(FW_VERSION_FULL "\r\n");   /* jedina definice ve version.h (== displej) */
			  }
			  /* ETH F0: `eth` = reset + sken SMI adres + dekod PHY; `eth clk` = zmer REF_CLK.
			   * BLOKUJE stovky ms (sken 32 adres x 2 registry) — UartTask neni pod watchdogem. */
			  else if (strncmp(RxBuffer, "eth", 3) == 0 &&
			           (RxBuffer[3] == '\0' || RxBuffer[3] == ' ')) {
				  const char *sub = (RxBuffer[3] == ' ') ? &RxBuffer[4] : "";

				  if (strcmp(sub, "clk") == 0) {
					  /* ETH_REF_CLK (PA1) pres TIM2_CH2 v rezimu externich hodin.
					   * TIM2 je 32bit a v `.ioc` VOLNY. Pri 50 MHz / 100 ms = 5e6 kroku. */
					  __HAL_RCC_TIM2_CLK_ENABLE();
					  GPIO_InitTypeDef g = {0};
					  g.Pin = ETH_REFCLK_PIN; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL;
					  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = GPIO_AF1_TIM2;
					  HAL_GPIO_Init(GPIOA, &g);

					  TIM2->CR1   = 0;
					  TIM2->PSC   = 0;
					  TIM2->ARR   = 0xFFFFFFFFu;
					  TIM2->CCMR1 = (TIM2->CCMR1 & ~(0x3u << 8)) | (0x1u << 8);  /* CC2S=01: IC2 na TI2 */
					  TIM2->CCER &= ~(TIM_CCER_CC2P | TIM_CCER_CC2NP);           /* nabezna hrana */
					  TIM2->SMCR  = (0x6u << 4) | 0x7u;                          /* TS=110 (TI2FP2), SMS=111 */
					  TIM2->CNT   = 0;
					  uint32_t c0 = DWT->CYCCNT;
					  TIM2->CR1  |= TIM_CR1_CEN;
					  uint32_t win = SystemCoreClock / 10u;                      /* presne 100 ms z DWT */
					  while ((DWT->CYCCNT - c0) < win) { __NOP(); }
					  uint32_t cnt = TIM2->CNT;
					  TIM2->CR1 &= ~TIM_CR1_CEN;

					  uint32_t khz = cnt / 100u;      /* kroku za 100 ms -> kHz */
					  printf("ETH REF_CLK (PA1): %lu kroku/100ms -> %lu.%03lu MHz\r\n",
					         (unsigned long)cnt, (unsigned long)(khz / 1000u),
					         (unsigned long)(khz % 1000u));
					  if (cnt == 0)
						  printf("  => ZADNE HODINY. Strap nINTSEL (LED2) nejspis NENI v rezimu REF_CLK OUT,\r\n"
						         "     nebo nebezi 25 MHz oscilator na XTAL1/CLKIN. Overit osciloskopem.\r\n");
					  else if (khz > 49000u && khz < 51000u)
						  printf("  => OK, 50 MHz. MAC dostane hodiny, nINTSEL je spravne.\r\n");
					  else
						  printf("  => MIMO 50 MHz (+/-2%%). Zkontrolovat oscilator a strapy.\r\n");
				  }
				  else {
					  printf("ETH F0 (PHY LAN8742A, bit-bang SMI z CM7):\r\n");
					  printf("  piny: MDC=PC1 MDIO=PA2 RES=PG14 REF_CLK=PA1 (ze schematu 4/7+2/7)\r\n");
					  eth_pins_init();

					  /* Hardwarovy reset PHY: >=100 us low, pak >=1 ms na nabeh. */
					  HAL_GPIO_WritePin(ETH_RES_PORT, ETH_RES_PIN, GPIO_PIN_RESET);
					  eth_delay_us(200);
					  HAL_GPIO_WritePin(ETH_RES_PORT, ETH_RES_PIN, GPIO_PIN_SET);
					  osDelay(10);

					  int found = -1;
					  for (uint8_t a = 0; a < 32; a++) {
						  uint16_t id1 = eth_phy_read(a, 2);
						  uint16_t id2 = eth_phy_read(a, 3);
						  if (id1 == 0xFFFF || (id1 == 0 && id2 == 0)) continue;   /* prazdno */
						  printf("  PHYAD %2u: ID1=0x%04X ID2=0x%04X (OUI 0x%08lX)\r\n",
						         (unsigned)a, id1, id2,
						         (unsigned long)(((uint32_t)id1 << 16) | id2));
						  if (found < 0) found = a;
					  }

					  if (found < 0) {
						  printf("  => ZADNY PHY NEODPOVIDA na SMI.\r\n");
						  printf("     Zkontroluj: napajeni PHY (+3V3), ETH_RES (PG14) nedrzi v resetu,\r\n");
						  printf("     zapojeni MDC/MDIO, pull-up na MDIO. SMI nezavisi na REF_CLK,\r\n");
						  printf("     takze mlceni NENI vysvetlitelne spatnym nINTSEL.\r\n");
					  } else {
						  uint16_t bmcr = eth_phy_read((uint8_t)found, 0);
						  uint16_t bmsr = eth_phy_read((uint8_t)found, 1);
						  uint16_t scsr = eth_phy_read((uint8_t)found, 31);  /* LAN8742A special status */
						  printf("  nalezen PHY na adrese %d (ocekavana 0 dle strapu PHYAD0->GND)\r\n", found);
						  printf("  BMCR=0x%04X  AN=%s speed=%s duplex=%s\r\n", bmcr,
						         (bmcr & (1u << 12)) ? "on" : "off",
						         (bmcr & (1u << 13)) ? "100M" : "10M",
						         (bmcr & (1u <<  8)) ? "full" : "half");
						  printf("  BMSR=0x%04X  link=%s AN-done=%s\r\n", bmsr,
						         (bmsr & (1u << 2)) ? "UP" : "down",
						         (bmsr & (1u << 5)) ? "ano" : "ne");
						  printf("  SCSR=0x%04X  (bity 4:2 = vysledna rychlost/duplex po AN)\r\n", scsr);
						  printf("  => PHY ZIJE. Zbyva overit hodiny: `eth clk`\r\n");
					  }
				  }
			  }
			  else if (strcmp(RxBuffer, "help") == 0) {
				  printf("ping | screen main | clear | version | help | ui | freq | gps | gpsraw | gps glonass | rtc | adcraw | stats | status | sensors | temperature | beep [on|off|test] | selftest | scpi <cmd> | datalog [on|off|erase|dump] | screenshot | autocal | stacktest | eth [clk]\r\n");
			  }
			  else if (strcmp(RxBuffer, "selftest") == 0) {
				  /* Ciste-logicke unit testy (zadny HW, zadny sdileny stav) — bezpecne za
				   * behu. Bezi i automaticky pri bootu (defaultTask); vysledek v Health. */
				  run_selftests();
			  }
			  else if (strncmp(RxBuffer, "scpi", 4) == 0 &&
			           (RxBuffer[4] == ' ' || RxBuffer[4] == '\0')) {
				  /* SCPI-99 pres USB konzoli (#25): "scpi <prikaz>" -> scpi_process.
				   * Prefix "scpi " je jen pro SDILENOU konzoli (aby nekolidoval s
				   * "version" apod.); dedikovany TCP 5025 na CM4 bude volat scpi_process
				   * primo bez prefixu. Napr.: scpi *IDN?  |  scpi MEAS:FREQ? */
				  const char *arg = (RxBuffer[4] == ' ') ? &RxBuffer[5] : "";
				  char resp[128];
				  size_t rn = scpi_process(arg, resp, sizeof resp);
				  if (rn) printf("%s\r\n", resp);   /* dotaz -> odpoved; akce (*RST) -> ticho */
				  else    printf("\r\n");
			  }
			  else if (strcmp(RxBuffer, "screenshot") == 0) {
				  /* Export obrazovky do BMP pres USB CDC (~1,15 MB, sekundy). ROZPRACOVANO
				   * — best-effort tok; UartTask neni hlidan watchdogem, smi blokovat. */
				  screenshot_emit_bmp();
			  }
			  else if (strcmp(RxBuffer, "autocal") == 0) {
				  autocal_run();
				  char ab[280];
				  autocal_format_full(ab, sizeof ab);
				  printf("%s", ab);
			  }
			  /* SD karta = EXPORTNI medium (W25Q zustava autoritativni, viz sd_export.h).
			   * ⚠️ mount i export BLOKUJI (HAL_SD_Init desitky-stovky ms, zapis sekundy) —
			   * proto jsou tady v UartTasku, ktery NENI hlidan watchdogem. Z defaultTask/
			   * UiTask je NEVOLAT. Detekce karty bezi levne v defaultTasku (sd_export_tick). */
			  else if (strncmp(RxBuffer, "sd", 2) == 0 && (RxBuffer[2] == '\0' || RxBuffer[2] == ' ')) {
				  const char *arg = RxBuffer[2] == ' ' ? &RxBuffer[3] : "";
				  if (strcmp(arg, "mount") == 0) {
					  printf("SD: mountuji...\n");
					  printf("SD: %s (%s)\n", sd_export_mount() ? "OK" : "FAIL", sd_export_state_str());
				  } else if (strcmp(arg, "unmount") == 0) {
					  sd_export_unmount();
					  printf("SD: odmountovano (%s)\n", sd_export_state_str());
				  } else if (strncmp(arg, "export", 6) == 0) {
					  uint32_t n = (uint32_t)atoi(arg[6] == ' ' ? &arg[7] : "");   /* 0 = vse */
					  printf("SD: exportuji %s do GPSDO.CSV, cekej...\n", n ? "cast logu" : "cely log");
					  int32_t w = sd_export_run(n);
					  if (w < 0) printf("SD: export FAIL (%s)\n", sd_export_state_str());
					  else       printf("SD: export OK, %ld zaznamu\n", (long)w);
				  } else if (strcmp(arg, "diag") == 0) {
					  sd_export_diag();
				  } else if (strcmp(arg, "test") == 0) {
					  sd_export_selftest();
				  } else if (strncmp(arg, "det invert", 10) == 0) {
					  const char *a2 = arg[10] == ' ' ? &arg[11] : "";
					  datalog_sd_det_invert(strcmp(a2, "off") != 0);
					  printf("SD: polarita detekce = %s\n", datalog_sd_det_inverted()
					         ? "OBRACENA (HIGH = karta)" : "vychozi (LOW = karta)");
					  printf("SD: PE3=%s -> %s\n", datalog_sd_det_raw() ? "HIGH" : "LOW",
					         datalog_sd_detect_status() ? "KARTA" : "prazdno");
				  } else if (strcmp(arg, "det") == 0) {
					  /* Diagnostika card-detect pinu bez debuggeru. Dle zapojeni J13
					   * (spinac DET_A=GND / DET_B=PE3 + 47k pull-up) ma byt
					   * LOW = karta vlozena. Zkus vysunout/zasunout a porovnej. */
					  printf("SD: PE3 syrove = %s   polarita = %s\n",
					         datalog_sd_det_raw() ? "HIGH" : "LOW",
					         datalog_sd_det_inverted() ? "OBRACENA (HIGH=karta)" : "vychozi (LOW=karta)");
					  printf("SD: vyhodnoceno=%s  debounced=%s  force=%s\n",
					         datalog_sd_detect_status() ? "KARTA" : "prazdno",
					         datalog_sd_card_present() ? "vlozena" : "chybi",
					         datalog_sd_det_forced() ? "ZAPNUTO" : "vypnuto");
				  } else if (strncmp(arg, "force", 5) == 0) {
					  const char *a2 = arg[5] == ' ' ? &arg[6] : "";
					  datalog_sd_det_force(strcmp(a2, "off") != 0);
					  printf("SD: override detekce %s\n",
					         datalog_sd_det_forced() ? "ZAPNUT (detekce se ignoruje)" : "vypnut");
				  } else {
					  printf("SD: karta %s, stav: %s%s\n",
					         datalog_sd_card_present() ? "VLOZENA" : "chybi", sd_export_state_str(),
					         datalog_sd_det_forced() ? "  [force]" : "");
					  printf("SD: prikazy: sd diag | sd test | sd det [invert on|off] | sd force [on|off] | sd mount | sd unmount | sd export [N]\n");
				  }
			  }
			  else if (strncmp(RxBuffer, "datalog", 7) == 0) {
				  const char *arg = RxBuffer[7] == ' ' ? &RxBuffer[8] : "";
				  if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
					  datalog_set_enabled(arg[1] == 'n');
					  printf("DATALOG: logovani %s\n", datalog_enabled() ? "ZAPNUTO" : "VYPNUTO");
				  } else if (strcmp(arg, "erase") == 0) {
					  /* Destruktivni + dlouhe (erase celeho DATA regionu). Drzi QSPI
					   * mutex uvnitr; UartTask NENI hlidan watchdogem, takze smi cekat. */
					  printf("DATALOG: mazu cely log, cekej...\n");
					  printf("DATALOG: erase %s\n", datalog_erase_all() ? "OK" : "FAIL");
				  } else if (strcmp(arg, "dump") == 0) {
					  /* Poslednich 10 zaznamu, nejnovejsi prvni (rychla kontrola obsahu). */
					  datalog_rec_t r;
					  for (uint32_t i = 0; i < 10u; i++) {
						  if (!datalog_read_back(i, &r)) break;
						  /* Kmitocet pres fpga_freq_format_val — u64 se do printf
						   * nedava (%llu nemusi nano-printf umet, stejny duvod jako
						   * jinde v projektu: zadny float/64b format v konzoli). */
						  char fb[32];
						  fpga_freq_format_val(r.freq_x100000, fb, sizeof fb);
						  printf("#%lu t=%lu f=%s Toc=%d Vc=%d fl=0x%02X sat=%u\n",
							     (unsigned long)r.seq, (unsigned long)r.t_unix, fb,
							     (int)r.t_ocxo_c100, (int)r.ocxo_vc_mv,
							     (unsigned)r.flags, (unsigned)r.sats);
					  }
				  } else {
					  char db[80];
					  datalog_format_status(db, sizeof db);
					  printf("%s\n", db);
					  printf("  (datalog on|off|erase|dump)\n");
				  }
			  }
			  else if (strcmp(RxBuffer, "stacktest yes") == 0) {
				  /* STATUS.md TODO #10: overeni, ze detekce preteceni zasobniku funguje
				   * CELOU cestou (hook -> crash_blackbox -> BKP_DR3..5 -> MX_RTC_Init ->
				   * Health "Reset:"). Zapnuti (configCHECK_FOR_STACK_OVERFLOW=2) bylo
				   * dosud overene jen staticky preprocesorem — sama detekce se ozve az
				   * pri skutecnem preteceni.
				   * Zamerne prepise stack UartTasku a hned yielduje: FreeRTOS pri
				   * prepnuti kontextu porovna stack pattern -> vApplicationStackOverflowHook
				   * -> zapis do BKP -> __disable_irq() + spin -> zadne heartbeaty ->
				   * IWDG resetne do ~4 s. Po bootu MUSI byt videt "Reset: ... stack:UartTask"
				   * (System Health / UART `status`).
				   * ⚠️ Vyzaduje presne "stacktest yes" — samotne "stacktest" jen vypise
				   * napovedu, aby to neslo spustit omylem/preklepem.
				   * ⚠️ Bez VBAT baterie testuj WARM resetem (BKP neprezije power-cycle). */
				  /* Velikost: UartTask 4096 B, ramec UartTask_run ~1,3 kB + echo/USB cesta
				   * ~1 kB -> stacktest_overflow() pridá 3616 B -> spolehlive pretece dno
				   * zasobniku (prepise 20B watermark -> hook). Zamerne v samostatne funkci
				   * (viz komentar u stacktest_overflow) — jako local v UartTask_run by tech
				   * 3600 B nafouklo ramec VZDY a task by pretekal i za normalu (byl to
				   * puvod HardFaultu pri prvnim USB znaku). */
				  printf("STACKTEST: pretekam stack UartTasku, ceka se IWDG reset (~4 s)...\n");
				  stacktest_overflow();
			  }
			  else if (strcmp(RxBuffer, "stacktest") == 0) {
				  printf("STACKTEST: zamerne pretece stack a vyvola IWDG reset.\n");
				  printf("  Potvrd prikazem: stacktest yes\n");
			  }
			  else if (strcmp(RxBuffer, "freq") == 0) {
				  char fbuf[48];
				  taskENTER_CRITICAL();
				  strncpy(fbuf, (const char *)g_freq_text, sizeof(fbuf) - 1);
				  fbuf[sizeof(fbuf) - 1] = '\0';
				  taskEXIT_CRITICAL();
				  printf("FREQ: %s\n", fbuf);
			  }
			  else if (strcmp(RxBuffer, "gps") == 0) {
				  char gbuf[96];
				  gps_format_status(gbuf, sizeof(gbuf));
				  printf("GPS: %s\n", gbuf);
			  }
			  else if (strcmp(RxBuffer, "gpsraw") == 0) {
				  char gbuf[128];
				  gps_format_raw(gbuf, sizeof(gbuf));
				  printf("GPSRAW: %s\n", gbuf);
			  }
			  else if (strcmp(RxBuffer, "gps glonass") == 0) {
				  /* Zapne GPS+SBAS+QZSS+GLONASS (UBX-CFG-GNSS). Best-effort — NEO-7M
				   * muze NAKnout; parser GLGSV zvlada tak jako tak. Overit pres
				   * "gpsraw" (mely by prijit i $GLGSV vety). */
				  gps_config_gnss();
				  printf("GPS: UBX-CFG-GNSS odeslano (GPS+SBAS+QZSS+GLONASS), overit gpsraw\n");
			  }
			  else if (strcmp(RxBuffer, "adcraw") == 0) {
				  /* Diag ADC3: raw 3 internich kanalu (cteno po jednom jako SensorsTask)
				   * + spocitane hodnoty. VREF = merena VREF+ (na teto desce VREFBUF ~2,5 V). */
				  static const uint32_t ch[3] = { ADC_CHANNEL_TEMPSENSOR,
						  ADC_CHANNEL_VREFINT, ADC_CHANNEL_VBAT };
				  uint32_t r[3] = {0};
				  for (int i = 0; i < 3; i++) {
					  ADC_ChannelConfTypeDef sc = {0};
					  sc.Channel = ch[i]; sc.Rank = ADC_REGULAR_RANK_1;
					  sc.SamplingTime = ADC_SAMPLETIME_810CYCLES_5;
					  sc.SingleDiff = ADC_SINGLE_ENDED; sc.OffsetNumber = ADC_OFFSET_NONE;
					  HAL_ADC_ConfigChannel(&hadc3, &sc);
					  ADC3_COMMON->CCR |= (ADC_CCR_VREFEN | ADC_CCR_TSEN | ADC_CCR_VBATEN);
					  ADC3->PCSEL |= (1u << ((ADC3->SQR1 >> 6) & 0x1Fu));
					  if (HAL_ADC_Start(&hadc3) == HAL_OK &&
						  HAL_ADC_PollForConversion(&hadc3, 20) == HAL_OK)
						  r[i] = HAL_ADC_GetValue(&hadc3);
					  HAL_ADC_Stop(&hadc3);
				  }
				  uint32_t rt = r[0], rv = r[1], rb = r[2];
				  uint16_t vc = *(volatile uint16_t *)0x1FF1E860UL;   /* VREFINT_CAL */
				  uint16_t t1 = *(volatile uint16_t *)0x1FF1E820UL;   /* TS_CAL1 30C */
				  uint16_t t2 = *(volatile uint16_t *)0x1FF1E840UL;   /* TS_CAL2 110C */
				  printf("ADCRAW raw: temp=%lu vref=%lu vbat=%lu  (CAL VREFINT=%u TS1=%u TS2=%u)\n",
						 (unsigned long)rt, (unsigned long)rv, (unsigned long)rb, vc, t1, t2);
				  if (rv && (t2 != t1)) {
					  uint32_t vref = 3300u * vc / rv;                 /* merena VREF+ [mV] */
					  uint32_t ts   = rt * vref / 3300u;               /* 3,3V-ekvivalent */
					  int32_t  tcx10 = ((int32_t)ts - (int32_t)t1) * 800 / ((int32_t)t2 - (int32_t)t1) + 300;
					  uint32_t vbat = (uint32_t)((uint64_t)rb * vref / 65535u) * 4u;
					  printf("  -> VREF=%lumV  TEMP=%ld.%01ld C  VBAT=%lumV\n",
							 (unsigned long)vref, (long)(tcx10 / 10), (long)(tcx10 < 0 ? -tcx10 % 10 : tcx10 % 10),
							 (unsigned long)vbat);
				  } else {
					  printf("  -> VREF cteni selhalo (rail?) — zkontroluj VREFBUF/VREF+\n");
				  }
				  /* Stav VREFBUF: CSR bit0=ENVR bit1=HIZ bit3=VRR(ready) bity6:4=VRS.
				   * Spravne (SCALE0, ready) = ENVR+VRR -> 0x...9. CCR = trim (nenulovy). */
				  printf("  VREFBUF CSR=0x%08lX CCR=0x%08lX  ADC CCR=0x%08lX PCSEL=0x%08lX\n",
						 (unsigned long)VREFBUF->CSR, (unsigned long)VREFBUF->CCR,
						 (unsigned long)ADC3_COMMON->CCR, (unsigned long)ADC3->PCSEL);
			  }
			  else if (strcmp(RxBuffer, "rtc") == 0) {
				  char rbuf[24];
				  uint8_t sy;
				  taskENTER_CRITICAL();
				  strncpy(rbuf, (const char *)g_rtc_text, sizeof(rbuf) - 1);
				  rbuf[sizeof(rbuf) - 1] = '\0';
				  sy = g_rtc_synced;
				  taskEXIT_CRITICAL();
				  printf("RTC: %s UTC %s\n", rbuf, sy ? "(GPS sync)" : "(volny beh, bez GPS)");
			  }
			  else if (strcmp(RxBuffer, "beep") == 0 || strcmp(RxBuffer, "beep test") == 0) {
				  alarm_test();
				  printf("BEEP: test%s\n", g_sound_muted ? " (POZOR: zvuk je vypnuty v Nastaveni)" : "");
			  }
			  else if (strcmp(RxBuffer, "beep on") == 0) {
				  g_sound_muted = 0; g_sys_cfg_dirty = 1;
				  printf("BEEP: zvuk ZAPNUT\n");
			  }
			  else if (strcmp(RxBuffer, "beep off") == 0) {
				  g_sound_muted = 1; g_sys_cfg_dirty = 1;
				  printf("BEEP: zvuk VYPNUT (mute)\n");
			  }
			  else if (strncmp(RxBuffer, "backlight ", 10) == 0) {
				  /* Test jasu z konzole (diagnostika ATTINY runtime zapisu): nastavi
				   * g_brightness, HW zapis udela UiTask pod mutexem (stejna cesta
				   * jako Nastaveni/auto-dim). Sleduj pak touch + `temperature`. */
				  int v = 0; const char *p = &RxBuffer[10];
				  while (*p >= '0' && *p <= '9' && v < 1000) { v = v * 10 + (*p - '0'); p++; }
				  if (v > 255) v = 255;
				  if (v < 5)   v = 5;      /* nikdy uplna tma */
				  g_brightness = (uint8_t)v; g_sys_cfg_dirty = 1;
				  printf("BACKLIGHT: %d (aplikuje UiTask; pri aktivnim dimu az po probuzeni)\n", v);
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
			  else if (strcmp(RxBuffer, "qspiid") == 0) {
				  /* Bring-up krok 1: JEDEC ID. Cekame EF 40 20 (W25Q512JV). Nevyzaduje init. */
				  uint32_t id = 0; int got = 0;
				  if (osMutexAcquire(qspiMutexHandle, QSPI_CMD_LOCK_MS) == osOK) {
					  id = w25q_read_jedec(); got = 1;
					  osMutexRelease(qspiMutexHandle);
				  }
				  if (!got) printf("QSPI: sbernice zaneprazdnena, zkus znovu\n");
				  else printf("QSPI JEDEC ID: %06lX  (%s)\n", (unsigned long)id,
					     (id == W25Q_JEDEC_ID) ? "W25Q512JV OK" : "NEODPOVIDA (cekam EF4020)");
			  }
			  else if (strcmp(RxBuffer, "qspitest") == 0) {
				  /* Bring-up krok 2: init + erase sektoru 0 + zapis/cteni vzorku + verify.
				   * DESTRUKTIVNI na sektor 0. Cela sekvence pod QSPI zamkem — erase+write+read
				   * musi projit vcelku (jinak by mezi ne vlezl syscfg auto-save z defaultTask). */
				  int got = 0, init_ok = 0, e = 0, w = 0, r = 0, ok = 0;
				  if (osMutexAcquire(qspiMutexHandle, QSPI_CMD_LOCK_MS) == osOK) {
					  got = 1;
					  if (w25q_init()) {
						  init_ok = 1;
						  uint8_t wr[16], rd[16];
						  for (int i = 0; i < 16; i++) wr[i] = (uint8_t)(0xA0 + i);
						  e = w25q_erase_sector(0);
						  w = w25q_write(0, wr, sizeof(wr));
						  r = w25q_read(0, rd, sizeof(rd));
						  ok = (e && w && r && memcmp(wr, rd, sizeof(wr)) == 0);
					  }
					  osMutexRelease(qspiMutexHandle);
				  }
				  if (!got)          printf("QSPI: sbernice zaneprazdnena, zkus znovu\n");
				  else if (!init_ok) printf("QSPI: init/JEDEC FAIL (zkontroluj qspiid)\n");
				  else printf("QSPI test: erase=%d write=%d read=%d verify=%s\n",
					     e, w, r, ok ? "OK" : "MISMATCH");
			  }
			  else if (strcmp(RxBuffer, "storetest") == 0) {
				  /* Test genericke store vrstvy na CONFIG regionu (base 0x0 -> hlida i
				   * base-0 edge case). Destruktivni, region zatim nevyuzity.
				   * Init -> write blob -> read+verify -> 2. write (rotace sektoru). */
				  if (osMutexAcquire(qspiMutexHandle, QSPI_CMD_LOCK_MS) != osOK) {
					  printf("QSPI: sbernice zaneprazdnena, zkus znovu\n");
				  } else if (!w25q_init()) {
					  printf("STORE: QSPI init FAIL (zkontroluj qspiid)\n");
					  osMutexRelease(qspiMutexHandle);
				  } else {
					  w25q_store_t st;
					  w25q_store_init(&st, W25Q_CONFIG_BASE, W25Q_CONFIG_SECTORS);
					  printf("STORE init: active=0x%06lX seq=%lu\n",
						     (unsigned long)st.active, (unsigned long)st.seq);
					  const char *msg = "GPSDO store test blob";
					  uint32_t mlen = (uint32_t)strlen(msg) + 1;
					  char rd[64];
					  bool w = w25q_store_write(&st, msg, mlen);
					  uint32_t n = w25q_store_read(&st, rd, sizeof(rd));
					  int ok = (w && n == mlen && strcmp(rd, msg) == 0);
					  uint32_t prev = st.active;
					  printf("STORE w/r: w=%d len=%lu verify=%s seq=%lu active=0x%06lX\n",
						     (int)w, (unsigned long)n, ok ? "OK" : "FAIL",
						     (unsigned long)st.seq, (unsigned long)st.active);
					  w25q_store_write(&st, msg, mlen);   /* 2. zapis -> seq++, rotace sektoru */
					  printf("STORE 2nd: seq=%lu active=0x%06lX (rotace:%s)\n",
						     (unsigned long)st.seq, (unsigned long)st.active,
						     (st.active != prev) ? "OK" : "NE");
					  osMutexRelease(qspiMutexHandle);
				  }
			  }
			  else if (strcmp(RxBuffer, "qspispeed") == 0) {
				  /* Propustnost cteni pri aktualni SCK (60 MHz quad). Zapise 64 KB pattern
				   * do DATA regionu, pak CASOVANY read (DWT) + verify. ⚠️ DESTRUKTIVNI na
				   * DATA[0..64KB). Zapis je pomaly (erase+PP ~1-2 s), meri se jen cteni. */
				  if (osMutexAcquire(qspiMutexHandle, QSPI_CMD_LOCK_MS) != osOK) {
					  printf("QSPI: sbernice zaneprazdnena, zkus znovu\n");
				  } else if (!w25q_init()) {
					  printf("QSPI speed: init FAIL\n");
					  osMutexRelease(qspiMutexHandle);
				  } else {
					  uint8_t buf[512];
					  uint32_t base = W25Q_DATA_BASE, total = 64u * 1024u;
					  for (uint32_t a = 0; a < total; a += W25Q_SECTOR_SIZE) w25q_erase_sector(base + a);
					  for (uint32_t off = 0; off < total; off += sizeof(buf)) {
						  for (int i = 0; i < (int)sizeof(buf); i++) buf[i] = (uint8_t)((off + i) & 0xFF);
						  w25q_write(base + off, buf, sizeof(buf));
					  }
					  uint32_t t0 = DWT->CYCCNT; int ok = 1;
					  for (uint32_t off = 0; off < total && ok; off += sizeof(buf)) {
						  if (!w25q_read(base + off, buf, sizeof(buf))) { ok = 0; break; }
						  for (int i = 0; i < (int)sizeof(buf); i++)
							  if (buf[i] != (uint8_t)((off + i) & 0xFF)) { ok = 0; break; }
					  }
					  uint32_t us = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000000u);
					  uint32_t kbps = us ? (uint32_t)((uint64_t)total * 1000u / us) : 0;
					  printf("QSPI speed: read %lu KB in %lu us -> %lu KB/s verify=%s\n",
						     (unsigned long)(total / 1024u), (unsigned long)us,
						     (unsigned long)kbps, ok ? "OK" : "FAIL");
					  osMutexRelease(qspiMutexHandle);
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
				  /* Diagnostika restartu + zdravi tasku. Drive to vypisovalo jen
				   * "RUNNING" (nepouzitelne pri honu na nahodny watchdog reset) —
				   * pricina resetu byla dostupna JEN v okne System Health. */
				  printf("RUNNING %s  uptime %lus\n", FW_VERSION_FULL, (unsigned long)g_uptime_s);
				  printf("Reset: %s%s%s\n", (const char *)g_reset_text,
					     g_crash_text[0] ? "  " : "", (const char *)g_crash_text);
				  printf("  RSR=0x%08lX%s\n", (unsigned long)g_reset_rsr,
					     g_reset_bad ? "  <-- WATCHDOG/CRASH" : "");
				  printf("Heap: free %lu B, min-ever %lu B   CPU %lu%%\n",
					     (unsigned long)g_rtos_heap_free, (unsigned long)g_rtos_heap_min,
					     (unsigned long)g_rtos_cpu_pct);
				  /* CM4 (D2): nenabootoval / bezi+mluvi (IPC heartbeat) / D2 ready ale ticho;
				   * stall x<n> = kolikrat se CM4 zasekl po ozivu (zotaveni pres jeho IWDG2). */
				  printf("CM4: %s, stall x%lu\n",
					     g_cm4_absent ? "ABSENT (nenabootoval - bank2/BCM4)" :
					     (g_cm4_alive ? "alive (IPC heartbeat)" : "SILENT (D2 ready, IPC ticho)"),
					     (unsigned long)g_cm4_stall_count);
				  /* Volny stack kritickych tasku — maly zbytek = kandidat na
				   * preteceni (a tim i na "stall"/HardFault). */
				  static const struct { const char *n; osThreadId_t *h; } TL[] = {
					  {"default", &defaultTaskHandle}, {"Uart", &UartTaskHandle},
					  {"I2C4",    &I2C4TaskHandle},    {"Ui",   &UiTaskHandle},
					  {"Fpga",    &FpgaTaskHandle},
				  };
				  for (unsigned i = 0; i < sizeof(TL) / sizeof(TL[0]); i++) {
					  if (*TL[i].h == NULL) continue;
					  printf("  stack %-7s free %lu B\n", TL[i].n,
						     (unsigned long)osThreadGetStackSpace(*TL[i].h));
				  }
				  /* FPGA link + CRC chyby (pocet + jak davno byla posledni). */
				  if (fpga_freq_crc_count())
					  printf("FPGA: link %s, CRC err %lu, last %lus ago\n",
						     fpga_freq_link_ok() ? "OK" : "NOLINK",
						     (unsigned long)fpga_freq_crc_count(),
						     (unsigned long)fpga_freq_crc_last_age_s());
				  else
					  printf("FPGA: link %s, CRC err 0\n", fpga_freq_link_ok() ? "OK" : "NOLINK");
				  char db[80];
				  datalog_format_status(db, sizeof db);
				  printf("%s\n", db);
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
