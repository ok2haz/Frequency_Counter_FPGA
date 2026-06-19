/*
 * freertos_task_sensors.c
 *
 * I2C senzorový task (SensorsTask_run, volaná ze StartI2C4 stubu) — vyčleněno z freertos.c.
 * TMP117 @ 0x48 na I2C4 (displej) + FPGA deska na I2C1 (TMP117 0x49/0x4A,
 * ADS1115 4 kanály). Vzorkuje 1×/s, zapisuje do g_* globálů pro UI/UART.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"

#include "i2c.h"          /* hi2c1, hi2c4 */
#include "ads1115.h"
#include "freertos_shared.h"

/* Definice pro TMP117 */
#define TMP117_ADDR         (0x48 << 1)
#define TMP117_REG_TEMP     0x00
#define TMP117_RESOLUTION   0.0078125f

/* Volano ze StartI2C4 stubu ve freertos.c (CubeMX-regen-safe). */
void SensorsTask_run(void *argument)
{
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
}
