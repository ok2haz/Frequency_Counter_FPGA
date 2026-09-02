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
#include "adc.h"          /* hadc3 — MCU teplota jadra / VDDA / VBAT (interni kanaly) */
#include "ads1115.h"
#include "si5356.h"       /* si5356_read_status (reg 218: LOS_CLKIN/PLL_LOL/SYS_CAL) */
#include "calib.h"        /* g_calib.gain_12v/gain_5v — editovatelna kalibrace (okno Kalibrace) */
#include "sensor_hist.h"  /* sensor_hist_feed — kratkodoba RAM historie (okno Grafy #31) */
#include "freertos_shared.h"

/* ── ADC3 factory kalibrace (system memory) ───────────────────────────────
 * ⚠️ Na STM32H7 jsou VSECHNY tyto hodnoty 16-BITOVE (overeno `adcraw`:
 * VREFINT_CAL=24291 = 1.22V @ 16-bit). Pozor: LL makra __LL_ADC_CALC_* u VREFINT
 * predpokladaji 12-bit kalibraci -> daji spatne VDDA. Proto pocitame RUCNE v
 * 16-bit (VREF_CHARAC = 3300 mV). */
#define ADC_TS_CAL1     (*(volatile uint16_t *)0x1FF1E820UL)   /* teplota @ 30 °C  (16-bit) */
#define ADC_TS_CAL2     (*(volatile uint16_t *)0x1FF1E840UL)   /* teplota @ 110 °C (16-bit) */
#define ADC_VREFINT_CAL (*(volatile uint16_t *)0x1FF1E860UL)   /* VREFINT @ 3.3V   (16-bit) */
#define ADC_VREF_CHARAC 3300u                                  /* VREF+ pri tovarni kalibraci [mV] (TS_CAL/VREFINT_CAL @ 3,3 V) */

/* ⚠️ ADC3 channel preselection: na H7 musi mit kazdy kanal bit v PCSEL, jinak je
 * analogovy vstup ODPOJENY a railuje (zjisteno: HAL_ADC_ConfigChannel ho na teto
 * verzi NENASTAVIL -> PCSEL=0). Interni kanaly: VBAT=17, TEMPSENSOR=18, VREFINT=19. */
#define ADC3_INT_PCSEL  ((1u << 17) | (1u << 18) | (1u << 19))

/* SW prumerovani: ADC interni kanaly (vysokoimpedancni zdroje + sum reference)
 * jednorazove kolisaji -> prumer N vzorku snizi sum o ~sqrt(N) (16 -> 4x).
 * HW oversampling zamerne nepouzivame (format Ratio je na H757 mezi HAL cestami
 * nejednoznacny). ~16*262us = ~4 ms/kanal @ Low prio, 2x/s -> ~2,7 % CPU. */
#define ADC3_AVG_N   16

/* Precte JEDEN ADC3 kanal (single conversion, rank1), prumer ADC3_AVG_N vzorku.
 * Scan polling vsech 3 najednou na H7 internim kanalum RAILOVAL na 0xFFFF -> citame
 * po jednom. SensorsTask init prepne hadc3 na ScanConvMode=DISABLE, NbrOfConversion=1. */
static int adc3_read_chan(uint32_t channel, uint32_t *out)
{
    ADC_ChannelConfTypeDef sc = {0};
    sc.Channel      = channel;
    sc.Rank         = ADC_REGULAR_RANK_1;
    sc.SamplingTime = ADC_SAMPLETIME_810CYCLES_5;
    sc.SingleDiff   = ADC_SINGLE_ENDED;
    sc.OffsetNumber = ADC_OFFSET_NONE;
    if (HAL_ADC_ConfigChannel(&hadc3, &sc) != HAL_OK) return 0;
    /* ⚠️ Pojistky (HAL je na teto H7 verzi nenastavil -> kanaly railovaly 0xFFFF),
     * pisi se dokud ADEN=0 (pred Start): (a) interni analogove cesty v ADC3 common,
     * (b) channel preselection PCSEL — bez nej je analogovy vstup odpojeny. */
    ADC3_COMMON->CCR |= (ADC_CCR_VREFEN | ADC_CCR_TSEN | ADC_CCR_VBATEN);
    ADC3->PCSEL |= (1u << ((ADC3->SQR1 >> 6) & 0x1Fu));   /* preselect kanal z rank1 (SQR1) */

    uint32_t sum = 0; int n = 0;
    for (int i = 0; i < ADC3_AVG_N; i++) {
        if (HAL_ADC_Start(&hadc3) != HAL_OK) break;
        if (HAL_ADC_PollForConversion(&hadc3, 20) == HAL_OK) { sum += HAL_ADC_GetValue(&hadc3); n++; }
        HAL_ADC_Stop(&hadc3);
    }
    if (n == 0) return 0;
    *out = sum / (uint32_t)n;
    return 1;
}

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
    s->err_last_ms = HAL_GetTick();   /* cas posledni chyby -> "uptime od posledni" (UART sensors) */
    if (s->err_streak < 0xFFFF) s->err_streak++;
    s->valid = 0;   /* 'last' zustava -> matematika/statistika ignoruji podle valid */
}

void sensor_stat_reset_all(void)
{
    for (int i = 0; i < SENS_COUNT; i++) {
        sensor_stat_t *s = &g_sensors[i];
        /* ⚠️ `samples` se NESMI vynulovat (chyba z prvni verze, 2026-08-17).
         * Neni to jen citac vzorku statistiky — cely zbytek kodu ho cte jako
         * "ma tenhle senzor VUBEC nejakou hodnotu?" na 13 mistech, mj.:
         *   datalog.c sens_c100/sens_mv -> DATALOG_INVALID16 (do logu by se
         *     na jeden tick zapsaly sentinely misto teplot/Vc/RF!),
         *   app_gpsdo.c draw_sensors_values -> "---" v ZIVE hodnote (tj. cela
         *     obrazovka Senzory, na ktere to tlacitko je, problikne prazdnotou),
         *   System Health "Power supplies" -> "Unkn", hbar_value -> "--",
         *   GRAFY bargrafy -> seda/nula, autocal -> AC_NA.
         * Misto toho restartujeme akumulaci OD AKTUALNI HODNOTY: min=max=mean=last
         * a samples=1. To je presne to, co dela lazy-init v sensor_update() pri
         * prvnim vzorku, takze navazujici running mean pocita spravne. */
        if (s->samples == 0) continue;         /* jeste nikdy nic neprecetl -> neni co resetovat */
        s->min = s->max = s->mean = s->last;
        s->samples = 1;
    }
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

/* Back-off interval podle poctu po sobe jdoucich SELHANI cele I2C1 (mrtvy bus):
 * 3x @500ms (normal) -> 3x @1s -> 2x @2s -> @10s. Setri CPU pri odpojene FPGA
 * desce, pri uspechu reset -> chytne se do ~0,5 s. */
static uint32_t i2c1_backoff_ms(uint32_t streak)
{
    if (streak < 3) return 500;     /* 3 pokusy normalne (streak 0,1,2) */
    if (streak < 6) return 1000;    /* 3 pokusy @ 1 s (streak 3,4,5) */
    if (streak < 8) return 2000;    /* 2 pokusy @ 2 s (streak 6,7) */
    return 10000;                   /* pak @ 10 s */
}

/* ── ADS1115 per-kanal PGA ────────────────────────────────────────────────
 * Kazda konverze prepisuje Config registr (kvuli MUX) -> PGA per kanal je zdarma.
 * Rev2 dle SKUTECNE osazenych delicu ve schematu v2.0 (netlist 2026-07-27):
 *   AIN0 OCXO_VC_Sense: R51=15k  / R52=10k  (0-5V  -> 2.00V) -> +-2.048V ✓
 *   AIN1 RF_Level (AD8307): R53=1k ser.     (~0.25-2.6V)     -> +-4.096V
 *          ⚠️ R54=10k je STALE OSAZEN -> zatezuje vystup AD8307 (25mV/dB do int.
 *             12,5k) a srazi strmost. Pro presnost R54 -> DNP (viz TODO §7).
 *   AIN2 VBUS: R55=100k / R56=4k99 (0-40V -> 1.90V)          -> +-2.048V ✓
 *   AIN3 +5V:  R57=15k  / R58=10k  (0-5V  -> 2.00V)          -> +-2.048V ✓
 * AIN0/AIN2/AIN3 mapovany na ~2V -> +-2.048V; jen AIN1 (AD8307, ~2.6V) je +-4.096V.
 * Az bude v2.0 deska osazena, prepni REV2 na 1 + prepocitat g_calib:
 *   gain_12v pro delic 100k/4k99 (=x21.0), gain_5v pro 15k/10k (=x2.5, drive 8k2/10k),
 *   + pridat skalovani AIN0 x2.5 (OCXO_VC) — viz BOARD_V20 §7.2. */
#ifndef ADS1115_HW_DIVIDERS_REV2
#define ADS1115_HW_DIVIDERS_REV2 0    /* 0 = stara deska (delice 5k1/10k) */
#endif

#if ADS1115_HW_DIVIDERS_REV2
static const ads1115_pga_t k_ads_pga[4] = {
    ADS1115_PGA_2V048,   /* AIN0 OCXO_VC_Sense (15k/10k -> 2.00V) */
    ADS1115_PGA_4V096,   /* AIN1 RF_Level (AD8307, ~2.6V) */
    ADS1115_PGA_2V048,   /* AIN2 VBUS (100k/4k99 -> 1.90V @ 40V) */
    ADS1115_PGA_2V048,   /* AIN3 +5V (15k/10k -> 2.00V) */
};
#else
static const ads1115_pga_t k_ads_pga[4] = {   /* stara deska: vse +-4.096V */
    ADS1115_PGA_4V096, ADS1115_PGA_4V096, ADS1115_PGA_4V096, ADS1115_PGA_4V096,
};
#endif

/* Volano ze StartI2C4 stubu ve freertos.c (CubeMX-regen-safe). */
void SensorsTask_run(void *argument)
{
  (void)argument;              /* signaturu urcuje CMSIS-RTOS, parametr nepouzivame */
  uint8_t rawData[2];
  int16_t tempRaw;

  // Smycka 10 Hz: kazdy tik RF_Level fast-path (AIN1 -> zivy bargraf), plny
  // sber senzoru jen kazdy 5. tik (= 2x/s jako drive). (Touch poll je v UiTasku —
  // presun do tohoto tasku zpusoboval selhani touch + free-run I2C4, viz historie.)
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(100);   // 10 Hz (RF fast-path)
  uint8_t sub = 0;                                    // 0 = plny sber, 1..4 = jen RF
  xLastWakeTime = xTaskGetTickCount();
  uint32_t   i2c1_streak = 0;                         // po sobe jdouci selhani cele I2C1
  TickType_t i2c1_last   = 0;                         // tick posledniho pokusu o I2C1
  uint32_t   i2c1_iv     = 0;                          // back-off interval [ticks], 0 = hned
  uint32_t   cyc         = 0;                          // citac cyklu (ADC3 jen kazdy 2. -> 1 Hz)

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

  // ⚠️ ADC3 prescaler: generated MX_ADC3_Init VYNECHAL ClockPrescaler (i kdyz .ioc
  // ma DIV8) -> ADC bezel na 25 MHz (PRESC=DIV1) + BOOST nastaveny na nizsi rozsah
  // -> vysokoimpedancni VREFINT/teplota se nestihly ustalit a RAILOVALY na 0xFFFF.
  // DIV8 -> 3.125 MHz, HAL dopocita spravny BOOST -> interni kanaly cti spravne.
  hadc3.Init.ClockPrescaler  = ADC_CLOCK_ASYNC_DIV8;
  hadc3.Init.ScanConvMode    = ADC_SCAN_DISABLE;
  hadc3.Init.NbrOfConversion = 1;
  HAL_ADC_Init(&hadc3);
  HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
  // Zapnout interni analogove cesty (VREFINT/teplota/VBAT) + channel preselection
  // (oboji HAL nenastavil) + stabilizace cidla (~120us). PCSEL/CCR jdou psat (ADEN=0).
  ADC3_COMMON->CCR |= (ADC_CCR_VREFEN | ADC_CCR_TSEN | ADC_CCR_VBATEN);
  ADC3->PCSEL |= ADC3_INT_PCSEL;
  osDelay(2);

  for(;;) {
	// === RF_Level fast-path (AIN1, ~10 Hz): na NE-sweep ticich, jen kdyz je I2C1
	// zdrava (streak==0 — pri mrtvem busu nehammerovat, sweep ridi back-off).
	// Jedine misto rychleho zapisu SENS_ADS1 mimo sweep (porad jediny writer task).
	if (sub != 0) {
	  if (i2c1_streak == 0) {
		int started = 0;
		if (osMutexAcquire(i2c1MutexHandle, 50) == osOK) {
		  started = ads1115_start(&hi2c1, 1, k_ads_pga[1]);   /* AIN1 = RF_Level */
		  osMutexRelease(i2c1MutexHandle);
		}
		if (started) {
		  osDelay(9);                                      /* 128 SPS konverze */
		  int16_t raw; int got = 0;
		  if (osMutexAcquire(i2c1MutexHandle, 50) == osOK) {
			got = ads1115_read_raw(&hi2c1, &raw);
			osMutexRelease(i2c1MutexHandle);
		  }
		  if (got) sensor_update(SENS_ADS1, (float)ads1115_raw_to_mv(raw, k_ads_pga[1]));
		  /* selhani zde NEpocitame do back-offu ani sensor_fail — vyhodnoti sweep */
		}
	  }
	  if (++sub >= 5) sub = 0;
	  vTaskDelayUntil(&xLastWakeTime, xFrequency);
	  continue;
	}
	sub = 1;

	// === TMP117 @ 0x48 na I2C4 (displej). Mutex: I2C4 sdili touch + backlight ===
	// TMP117 drzi pointer, ale HAL_I2C_Mem_Read je nejjistejsi.
	/* ⚠️ BACK-OFF NA I2C4 (2026-08-13). Sbernici sdili touch (UiTask, ~15-30 Hz),
	 * podsviceni a tenhle TMP117. Kdyz sbernice umre, KAZDE cteni tady vycerpa cely
	 * HAL timeout, a to POD MUTEXEM — touch se pak k busu casto vubec nedostane.
	 * Proto stejny vzor jako u I2C1: pri opakovanem selhani cist ridceji.
	 * Timeout zkracen 100 -> 20 ms: samotne cteni 2 B @100 kHz trva ~0,3 ms, takze
	 * 100 ms se uplatnilo VYHRADNE pri poruse — a prave tam nejvic skodilo. */
	static uint32_t i2c4_streak = 0;    /* po sobe jdouci selhani cteni 0x48 */
	static uint32_t i2c4_skip   = 0;    /* kolik cyklu jeste preskocit */
	HAL_StatusTypeDef i2cStatus = HAL_ERROR;
	if (i2c4_skip > 0) {
	  i2c4_skip--;                      /* back-off: tenhle cyklus se na bus nesaha */
	  i2cStatus = HAL_BUSY;             /* != HAL_OK -> sensor_fail nize (drzi posl. dobrou) */
	} else if (osMutexAcquire(i2c4MutexHandle, 100) == osOK) {
	  i2cStatus = HAL_I2C_Mem_Read( &hi2c4, TMP117_ADDR, TMP117_REG_TEMP, I2C_MEMADD_SIZE_8BIT, rawData, 2, 20);
	  osMutexRelease(i2c4MutexHandle);
	}
	if (i2cStatus == HAL_OK) { i2c4_streak = 0; }
	else if (i2c4_skip == 0) {
	  if (i2c4_streak < 100) i2c4_streak++;
	  /* Cyklus je 500 ms -> 3x normalne, pak 1 s, 2 s, nakonec 10 s (jako I2C1). */
	  i2c4_skip = (i2c4_streak < 3) ? 0 : (i2c4_streak < 6) ? 1 : (i2c4_streak < 8) ? 3 : 19;
	}
	if (i2cStatus == HAL_OK) {
	  // MSB v rawData[0], LSB v rawData[1]; 0.0078125 °C/LSB
	  tempRaw = (int16_t)((rawData[0] << 8) | rawData[1]);
	  sensor_update(SENS_T48, (float)tempRaw * TMP117_RESOLUTION);
	} else {
	  sensor_fail(SENS_T48);   /* drzi posledni dobrou hodnotu, valid=0, loguje */
	}

	// === FPGA deska na I2C1: TMP117 0x49/0x4A + ADS1115 — s BACK-OFF pri mrtvem busu ===
	// 0x48 vyse (I2C4) cte vzdy 2x/s; tento I2C1 blok se pri trvalem selhani zpomaluje.
	// wrap-safe gate (unsigned rozdil; prezije preteceni ticku ~49,7 dne -> 100 dni OK)
	if ((uint32_t)(xTaskGetTickCount() - i2c1_last) >= i2c1_iv) {
	  i2c1_last = xTaskGetTickCount();
	  int any_ok = 0;   // jakekoli HAL_OK -> bus zije (NACK absentniho 0x4A se nepocita)
	  if (osMutexAcquire(i2c1MutexHandle, 100) == osOK) {
		uint8_t tb[2];
		if (HAL_I2C_Mem_Read(&hi2c1, (0x49 << 1), 0x00, I2C_MEMADD_SIZE_8BIT, tb, 2, 50) == HAL_OK) {
		  sensor_update(SENS_T49, (float)((int16_t)((tb[0] << 8) | tb[1])) * TMP117_RESOLUTION); any_ok = 1;
		} else sensor_fail(SENS_T49);
		if (HAL_I2C_Mem_Read(&hi2c1, (0x4A << 1), 0x00, I2C_MEMADD_SIZE_8BIT, tb, 2, 50) == HAL_OK) {
		  sensor_update(SENS_T4A, (float)((int16_t)((tb[0] << 8) | tb[1])) * TMP117_RESOLUTION); any_ok = 1;
		} else sensor_fail(SENS_T4A);
		/* Si5356 reference: status reg 218 (LOS_CLKIN / PLL_LOL / SYS_CAL) -> diagnostika */
		uint8_t si_st;
		if (si5356_read_status(&hi2c1, &si_st)) { g_si5356_status = si_st; g_si5356_ok = 1; any_ok = 1; }
		else                                    { g_si5356_ok = 0; }
		osMutexRelease(i2c1MutexHandle);
	  } else {
		sensor_fail(SENS_T49);
		sensor_fail(SENS_T4A);
		g_si5356_ok = 0;
	  }
	  i2c1_recover_if_wedged();   /* zaseknuty bus uvolni PRED ADS (jinak kaskada) */

	  for (uint8_t ch = 0; ch < 4; ch++) {
		sensor_id_t sid = (sensor_id_t)(SENS_ADS0 + ch);
		int started = 0;
		if (osMutexAcquire(i2c1MutexHandle, 100) == osOK) {
		  started = ads1115_start(&hi2c1, ch, k_ads_pga[ch]);
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
		  int32_t mv = ads1115_raw_to_mv(raw, k_ads_pga[ch]);
		  /* AIN2 = 12V vetev pres odporovy delic, AIN3 = 5V vetev pres delic ->
			 skutecne napeti. Gain je editovatelna kalibrace (g_calib, okno
			 Kalibrace); vychozi = datasheet pomer (13417/2814, 4978/2526).
			 ⚠️ Kratke okno pri bootu pred calib_load() (UiTask) jede na vychozich
			 hodnotach z calib.c — kosmeticke, diagnosticke cteni ~1 Hz. */
		  if      (ch == 2) mv = (int32_t)((float)mv * g_calib.gain_12v + 0.5f);
		  else if (ch == 3) mv = (int32_t)((float)mv * g_calib.gain_5v  + 0.5f);
		  sensor_update(sid, (float)mv); any_ok = 1;
		} else {
		  sensor_fail(sid);
		}
	  }
	  i2c1_recover_if_wedged();   /* jednou za cyklus po ADS (max 2 recovery/cyklus) */

	  /* back-off: uspech -> reset na normal; jinak rampa intervalu (viz i2c1_backoff_ms). */
	  if (any_ok) i2c1_streak = 0;
	  else if (i2c1_streak < 1000) i2c1_streak++;
	  i2c1_iv = pdMS_TO_TICKS(i2c1_backoff_ms(i2c1_streak));
	}

	// === ADC3: MCU teplota jadra + VDDA (z VREFINT) + VBAT (interni kanaly) ===
	// Cteni po jednom kanalu (adc3_read_chan) — scan polling na H7 railoval.
	// Rucni 16-bit prepocet (kalibrace jsou 16-bit). Nezavisle na I2C.
	// ⚠️ JEN KAZDY 2. CYKLUS (1 Hz): jadro/VREF/VBAT se meni pomalu -> 2 Hz zbytecne;
	// setri ~1,3 % CPU (3 kanaly x 16 vzorku busy-poll ~12,6 ms se pulnou frekvenci).
	if ((cyc++ & 1u) == 0u) {
	uint32_t rt = 0, rv = 0, rb = 0;
	int a_t = adc3_read_chan(ADC_CHANNEL_TEMPSENSOR, &rt);
	int a_v = adc3_read_chan(ADC_CHANNEL_VREFINT,    &rv);
	int a_b = adc3_read_chan(ADC_CHANNEL_VBAT,       &rb);
	if (a_v && rv) {
	  /* vref = MERENA VREF+ [mV] = VREFINT_CAL * 3300 / VREFINT_DATA. Formula je
	   * reference-agnosticka -> vraci SKUTECNE VREF+ at uz je to VDDA (3300) nebo
	   * VREFBUF (~2500). Zobrazuje se jako "VREF" (na teto desce ~2,5 V z VREFBUF). */
	  uint32_t vref = ADC_VREF_CHARAC * (uint32_t)ADC_VREFINT_CAL / rv;
	  sensor_update(SENS_VDDA, (float)vref);
	  if (a_t) {
		/* TS_CAL jsou merene pri VREF+=3,3 V -> rt prepocti na 3,3V-ekvivalent
		 * (rt * vref/3300), jinak by teplota byla mimo (VREFBUF != 3,3 V). */
		float ts   = (float)rt * (float)vref / (float)ADC_VREF_CHARAC;
		int   span = (int)ADC_TS_CAL2 - (int)ADC_TS_CAL1;   /* 30..110 °C */
		float tc   = span ? ((ts - (float)ADC_TS_CAL1) * 80.0f / (float)span + 30.0f) : 0.0f;
		/* 🔴 JEDINA teplota, ktera se FILTRUJE — a je to zamer, ne nedbalost jinde.
		 * Zmereno 2026-09-01: dva odecty par sekund po sobe daly 52,17 a 48,9 °C
		 * (rozptyl 3,3 °C), zatimco TMP117 na desce drzel 0,5 °C za cely beh.
		 * Neni to zavada: cidlo v kremiku ma nekalibrovanou presnost v jednotkach
		 * °C a jde navic pres VREFINT, takze se scita sum OBOU prevodu. Bez filtru
		 * to zaplevelilo min/max (41,8/52,2 proti 31,1/31,6 u ostatnich radku) a
		 * na prehledu kanalu to vypadalo jako porucha, prestoze `err=0`.
		 *
		 * ⚠️ Filtrovat SE SMI PRAVE PROTO, ze na teto hodnote nic nevisi: je to
		 * indikator „jak je horky kremik", ne merici vstup. Necte ji zadny alarm,
		 * zadny prah ani warm-up kriterium (to jede z OCXO 0x49), takze zpozdeni
		 * radu sekund nikomu nevadi. U ANALOGOVYCH vetvi (ADS, VREF, VBAT) by to
		 * bylo NEPRIPUSTNE — ty do mereni mluvi a filtr by zakryl skutecny
		 * vypadek napajeni. Nekopirovat to sem na jine senzory.
		 *
		 * IIR alfa = 1/8 pri 1 Hz -> casova konstanta ~8 s. To je hluboko pod
		 * tepelnou setrvacnosti pouzdra, takze skutecny nabeh po startu
		 * (~42 -> 52 °C behem minut) projde nezkresleny; potlaci se jen sum. */
		static float s_core_flt = 0.0f;
		static uint8_t s_core_seen = 0u;
		if (!s_core_seen) { s_core_flt = tc; s_core_seen = 1u; }
		else              { s_core_flt += (tc - s_core_flt) * 0.125f; }
		sensor_update(SENS_CORE_T, s_core_flt);
	  } else sensor_fail(SENS_CORE_T);
	  if (a_b) {
		uint32_t vbat = (uint32_t)((uint64_t)rb * vref / 65535u) * 4u;   /* vnitrni delic /4 */
		sensor_update(SENS_VBAT, (float)vbat);
	  } else sensor_fail(SENS_VBAT);
	} else {
	  sensor_fail(SENS_CORE_T); sensor_fail(SENS_VDDA); sensor_fail(SENS_VBAT);
	}
	}   /* konec ADC3 bloku (1 Hz) */

	// Kratkodoba historie senzoru (RAM decimacni pyramida, okno Grafy #31).
	// Volano z 2 Hz plneho sweepu; uvnitr decimuje na SENSOR_HIST_BASE_S.
	sensor_hist_feed();

	// Cekani na dalsi cyklus (presne 500 ms od posledniho probuzeni)
	vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}
