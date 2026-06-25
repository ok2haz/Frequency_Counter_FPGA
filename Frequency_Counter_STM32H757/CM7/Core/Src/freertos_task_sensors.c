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
#include "si5356.h"       /* si5356_read_status (reg 218: LOS_CLKIN/PLL_LOL/SYS_CAL) */
#include "freertos_shared.h"

/* Definice pro TMP117 */
#define TMP117_ADDR         (0x48 << 1)
#define TMP117_REG_TEMP     0x00
#define TMP117_REG_CONFIG   0x01
#define TMP117_RESOLUTION   0.0078125f
/* Config: MOD=00 continuous, CONV=011 (cyklus 500 ms), AVG=01 (8 prumeru)
 * -> 2 cerstve vzorky/s (default 0x0220 = 1 s cyklus). */
#define TMP117_CFG_2HZ      0x01A0u

/* Jednorazove nastavi TMP117 na 500ms konverzni cyklus (2x/s). */
static void tmp117_set_2hz(I2C_HandleTypeDef *hi2c, uint16_t addr8)
{
    uint8_t cfg[2] = { (uint8_t)(TMP117_CFG_2HZ >> 8), (uint8_t)(TMP117_CFG_2HZ & 0xFF) };
    HAL_I2C_Mem_Write(hi2c, addr8, TMP117_REG_CONFIG, I2C_MEMADD_SIZE_8BIT, cfg, 2, 100);
}

/* ── Statistika senzoru (zapis g_sensors[], viz sensor_stat.h) ──────────── */
/* ⚠️ ŽÁDNÝ printf zde! Běží v SensorsTasku s malým stackem (~512–1024 B);
 * printf/HAL_UART_Transmit by stack PŘETEKL -> HardFault/boot-loop (zjištěno
 * 2026-06-21: log se uřízl uprostřed "SENZOR TMP117@0x4"). Stav chyby je vidět
 * na displeji (červený "!") i přes UART příkaz `sensors` (běží v UartTasku). */

void sensor_update(sensor_id_t id, float value)
{
    if (id >= SENS_COUNT) return;
    sensor_stat_t *s = &g_sensors[id];

    s->last = value;
    if (s->samples == 0) {
        s->min = s->max = s->mean = value;   /* lazy init prvnim vzorkem */
        s->samples = 1;
    } else {
        if (value < s->min) s->min = value;
        if (value > s->max) s->max = value;
        s->samples++;
        s->mean += (value - s->mean) / (float)s->samples;  /* running mean, bez overflow */
    }
    s->err_streak = 0;
    s->valid = 1;
}

void sensor_fail(sensor_id_t id)
{
    if (id >= SENS_COUNT) return;
    sensor_stat_t *s = &g_sensors[id];

    s->err_total++;
    if (s->err_streak < 0xFFFF) s->err_streak++;
    s->valid = 0;   /* 'last' zustava -> matematika/statistika ignoruji podle valid */
}

void sensor_stat_reset(sensor_id_t id)
{
    if (id >= SENS_COUNT) return;
    sensor_stat_t *s = &g_sensors[id];
    s->samples = 0;
    s->min = s->max = s->mean = 0.0f;
}

/* ── I2C1 recovery (robustnost: vypadek 1 senzoru nesmi shodit zbytek sbernice) ──
 * Pokud nejaky cip drzi SDA / bus se zasekne, dalsi cteni na I2C1 timeoutuji a
 * kaskadou padaji vsechny (vc. ADS). Recovery: 9 SCL pulzu uvolni slave drzici
 * SDA, pak re-init peripherálu. PB8=SCL, PB9=SDA (I2C1, BEZ ATTINY -> bezpecne,
 * na rozdil od I2C4). Spousti se JEN pri "wedge" chybe (BERR/ARLO/TIMEOUT), NE
 * pri pouhem NACK (chybejici cip 0x4A) -> absentni cip nezpusobi re-init. */
static void i2c1_delay(void) { for (volatile int i = 0; i < 2000; i++) { } }   /* ~par us */

static void i2c1_recover(void)
{
    HAL_I2C_DeInit(&hi2c1);                                    /* uvolni AF piny + error */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_9; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP;        /* SDA vstup */
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_8; g.Mode = GPIO_MODE_OUTPUT_OD; g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;                             /* SCL open-drain out */
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    for (int i = 0; i < 9; i++) {                              /* 9 pulzu -> slave pusti SDA */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET); i2c1_delay();
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);   i2c1_delay();
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_SET) break;        /* SDA uvolneno */
    }
    /* ⚠️ re-init BEZ MX_I2C1_Init (ta ma Error_Handler = nekonecna smycka pri
     * selhani HAL_I2C_Init -> pri ODPOJENEM/plovoucim busu by zamrzl CELY program).
     * Selhani re-initu tu jen tise -> zkusi se dalsi cyklus. hi2c1.Init zustava z bootu. */
    __HAL_RCC_I2C1_CLK_ENABLE();
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9; g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW; g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &g);                                  /* PB8/9 zpet na I2C AF */
    if (HAL_I2C_Init(&hi2c1) == HAL_OK) {
        HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);
        HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0);
    }
}

/* Recover JEN pri skutecnem zaseknuti sbernice (ne NACK absentniho cipu).
 * Pod mutexem -> nekoliduje s UART prikazy (si5356/scan1), ktere taky sahaji na
 * I2C1 z UartTasku. Pri neziskani mutexu se preskoci (zkusi pristi cyklus). */
static void i2c1_recover_if_wedged(void)
{
    uint32_t e = HAL_I2C_GetError(&hi2c1);
    if (!(e & (HAL_I2C_ERROR_BERR | HAL_I2C_ERROR_ARLO | HAL_I2C_ERROR_TIMEOUT))) return;
    if (osMutexAcquire(i2c1MutexHandle, 50) == osOK) {
        i2c1_recover();
        osMutexRelease(i2c1MutexHandle);
    }
}

/* Volano ze StartI2C4 stubu ve freertos.c (CubeMX-regen-safe). */
void SensorsTask_run(void *argument)
{
  uint8_t rawData[2];
  int16_t tempRaw;

  // Senzory 2x/s. (Touch poll je v UiTasku — presun do tohoto tasku zpusoboval
  // selhani touch + free-run I2C4 pri saturaci, viz historie.)
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(500);   // 2x za sekundu
  xLastWakeTime = xTaskGetTickCount();

  // Jednorazove: vsechny 3 TMP117 na 500ms konverzni cyklus (cerstve 2x/s).
  if (osMutexAcquire(i2c4MutexHandle, osWaitForever) == osOK) {
    tmp117_set_2hz(&hi2c4, TMP117_ADDR);          // 0x48 (I2C4)
    osMutexRelease(i2c4MutexHandle);
  }
  if (osMutexAcquire(i2c1MutexHandle, 200) == osOK) {
    tmp117_set_2hz(&hi2c1, (0x49 << 1));          // 0x49 (I2C1)
    tmp117_set_2hz(&hi2c1, (0x4A << 1));          // 0x4A (I2C1)
    osMutexRelease(i2c1MutexHandle);
  }

  for(;;) {
	// === TMP117 @ 0x48 na I2C4 (displej). Mutex: I2C4 sdili touch + backlight ===
	// TMP117 drzi pointer, ale HAL_I2C_Mem_Read je nejjistejsi.
	HAL_StatusTypeDef i2cStatus = HAL_ERROR;
	if (osMutexAcquire(i2c4MutexHandle, osWaitForever) == osOK) {
	  i2cStatus = HAL_I2C_Mem_Read( &hi2c4, TMP117_ADDR, TMP117_REG_TEMP, I2C_MEMADD_SIZE_8BIT, rawData, 2, 100);
	  osMutexRelease(i2c4MutexHandle);
	}
	if (i2cStatus == HAL_OK) {
	  // MSB v rawData[0], LSB v rawData[1]; 0.0078125 °C/LSB
	  tempRaw = (int16_t)((rawData[0] << 8) | rawData[1]);
	  sensor_update(SENS_T48, (float)tempRaw * TMP117_RESOLUTION);
	} else {
	  sensor_fail(SENS_T48);   /* drzi posledni dobrou hodnotu, valid=0, loguje */
	}

	// === FPGA deska na I2C1: TMP117 0x49/0x4A + ADS1115 4 kanaly ===
	if (osMutexAcquire(i2c1MutexHandle, 100) == osOK) {
	  uint8_t tb[2];
	  if (HAL_I2C_Mem_Read(&hi2c1, (0x49 << 1), 0x00, I2C_MEMADD_SIZE_8BIT, tb, 2, 50) == HAL_OK)
		sensor_update(SENS_T49, (float)((int16_t)((tb[0] << 8) | tb[1])) * TMP117_RESOLUTION);
	  else
		sensor_fail(SENS_T49);
	  if (HAL_I2C_Mem_Read(&hi2c1, (0x4A << 1), 0x00, I2C_MEMADD_SIZE_8BIT, tb, 2, 50) == HAL_OK)
		sensor_update(SENS_T4A, (float)((int16_t)((tb[0] << 8) | tb[1])) * TMP117_RESOLUTION);
	  else
		sensor_fail(SENS_T4A);
	  /* Si5356 reference: status reg 218 (LOS_CLKIN / PLL_LOL / SYS_CAL) -> diagnostika */
	  uint8_t si_st;
	  if (si5356_read_status(&hi2c1, &si_st)) { g_si5356_status = si_st; g_si5356_ok = 1; }
	  else                                    { g_si5356_ok = 0; }
	  osMutexRelease(i2c1MutexHandle);
	} else {
	  /* I2C1 sbernice nedostupna -> oba TMP117 na ni jako chyba */
	  sensor_fail(SENS_T49);
	  sensor_fail(SENS_T4A);
	  g_si5356_ok = 0;
	}
	i2c1_recover_if_wedged();   /* zaseknuty bus uvolni PRED ADS (jinak kaskada) */

	for (uint8_t ch = 0; ch < 4; ch++) {
	  sensor_id_t sid = (sensor_id_t)(SENS_ADS0 + ch);
	  int started = 0;
	  if (osMutexAcquire(i2c1MutexHandle, 100) == osOK) {
		started = ads1115_start(&hi2c1, ch);
		osMutexRelease(i2c1MutexHandle);
	  }
	  if (!started) { sensor_fail(sid); continue; }

	  osDelay(9);   /* 128 SPS -> ~7.8 ms konverze */
	  int16_t raw;
	  int got = 0;
	  if (osMutexAcquire(i2c1MutexHandle, 100) == osOK) {
		got = ads1115_read_raw(&hi2c1, &raw);
		osMutexRelease(i2c1MutexHandle);
	  }
	  if (got) {
		int32_t mv = ads1115_raw_to_mv(raw);
		/* AIN2 = 12V vetev pres odporovy delic (real 13.417V @ 2.814V na ADS),
		   AIN3 = 5V vetev pres delic (real 4.978V @ 2.526V na ADS) -> skutecne napeti */
		if      (ch == 2) mv = (int32_t)((int64_t)mv * 13417 / 2814);
		else if (ch == 3) mv = (int32_t)((int64_t)mv * 4978  / 2526);
		sensor_update(sid, (float)mv);
	  } else {
		sensor_fail(sid);
	  }
	}
	i2c1_recover_if_wedged();   /* jednou za cyklus po ADS (max 2 recovery/cyklus s tim pred ADS) */

	// Cekani na dalsi cyklus (presne 500 ms od posledniho probuzeni)
	vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}
