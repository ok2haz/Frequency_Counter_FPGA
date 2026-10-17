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
#include "si5356.h"
#include "errlog.h"   /* udalosti: ztrata reference, vypadek senzoru */       /* si5356_read_status (218) + _sticky (247) */
#include "calib.h"        /* g_calib.gain_12v/gain_5v — editovatelna kalibrace (okno Kalibrace) */
#include "ad5693.h"       /* DAC ladeni OCXO — obsluha zadosti pod i2c1MutexHandle */
#include "gpsdo.h"        /* g_gpsdo_drive/_code_q16/_lockflag — povel smycky GPSDO pro AD5693R */
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
    /* ⚠️ Navratovou hodnotu vyhodnocujeme (L-0003, audit F-0022). Selhani NENI
     * fatalni — cidlo zustane ve vychozim prevodnim cyklu a MERI DAL, jen jinou
     * kadenci. Prave proto se to musi dat poznat: tiche jine vzorkovani by menilo
     * casovou konstantu teplotnich trendu, ze kterych se pocita warm-up OCXO. */
    if (HAL_I2C_Mem_Write(hi2c, addr8, TMP117_REG_CONFIG,
                          I2C_MEMADD_SIZE_8BIT, cfg, 2, 100) != HAL_OK) {
        g_tmp117_cfg_fail++;
    }
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
    /* ⚠️ Az od SERIE, ne od prvni chyby: jednotlive selhani cteni je bezne
     * (sdilena sbernice, kolize s touchem) a samo se zotavi. Trvala serie uz
     * znamena, ze senzor opravdu zmizel. Rate-limit errlogu resi zbytek. */
    if (s->err_streak == 20u) {
        (void)errlog_put(ERRLOG_K_SENSOR, (uint8_t)id, s->err_total, s->err_streak, "senzor");
    }
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
    /* 🔴 ODR MUSI byt 1 uz PRED prepnutim do OUTPUT_OD (audit F-0021, 2026-09-10).
     * `HAL_GPIO_Init` na `ODR` NESAHA, takze pin prevezme starou hodnotu — a ta je
     * po resetu 0, protoze do ni software nikdy nepsal (pin ridila periferie).
     * Pri opacnem poradi tedy SCL na par instrukci stahne k zemi, coz je falesna
     * hodinova hrana prave ve chvili, kdy je sbernice rozhozena.
     * ⚠️ Totez pravidlo a tataz oprava je v `i2c4_recover` (freertos_task_ui.c) —
     * tam to kdysi zpusobilo, ze SCL zustala dole NATRVALO. Sem se oprava
     * neprenesla; drz obe kopie v souladu. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_8; g.Mode = GPIO_MODE_OUTPUT_OD; g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;                             /* SCL open-drain out */
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);        /* pojistka po Init */
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

/* ── AD5693R: DAC ladeni OCXO (I2C1 0x4C) — popis a zapojeni v ad5693.h ──────
 * Implementace je ZDE, ne v novem ad5693.c: novy .c se do buildu nedostane bez
 * Close/Open Project v IDE (CLAUDE.md, mechanicke pravidlo 2). Vsechny funkce
 * volat jen ze SensorsTasku pod i2c1MutexHandle. */
volatile ad5693_state_t g_dac;

/* Kod k obnoveni po startu. Plni ho `syscfg_load` (z flash) a nize uspesny zapis
 * kodu; cte ho `syscfg` pri ukladani (shadow-diff -> flash po ~1,5 s klidu). */
volatile uint16_t g_dac_persist;
volatile uint8_t  g_dac_persist_loaded;

#define AD5693_I2C_TMO_MS  10u   /* 3 B pri ~50 kHz = ~0,8 ms */
#define AD5693_RESTORE_TRIES 5u  /* po tolika selhanich zapisu se obnova vzda */

static int ad5693_write3t(uint8_t cmd, uint16_t data, uint32_t tmo_ms)
{
    uint8_t b[3] = { cmd, (uint8_t)(data >> 8), (uint8_t)data };
    return HAL_I2C_Master_Transmit(&hi2c1, AD5693_ADDR8, b, 3, tmo_ms) == HAL_OK;
}

static int ad5693_write3(uint8_t cmd, uint16_t data)
{
    return ad5693_write3t(cmd, data, AD5693_I2C_TMO_MS);
}

/* 🔴 Sonda NOP zapisem, NE `HAL_I2C_IsDeviceReady`: ta pri chybejicim cipu
 * konci s `HAL_I2C_ERROR_TIMEOUT` a `i2c1_recover_if_wedged()` (vola se hned
 * po tomto bloku) by pak kazdych 10 s spoustel obnovu sbernice (9 pulzu SCL
 * + re-init) kvuli cipu, ktery jen neni osazeny. NACK na zapisu konci jako
 * `HAL_I2C_ERROR_AF`, ktery obnovu nespousti — stejne jako neosazeny 0x4A.
 * NOP (CMD 0) cip ignoruje, na vystup nesahne. */
static int ad5693_probe_nop(void)
{
    return ad5693_write3(AD5693_CMD_NOP, 0u);
}

static int ad5693_readback(uint16_t *code)
{
    uint8_t b[2];
    if (HAL_I2C_Mem_Read(&hi2c1, AD5693_ADDR8, AD5693_CMD_NOP, I2C_MEMADD_SIZE_8BIT,
                         b, 2, AD5693_I2C_TMO_MS) != HAL_OK) return 0;
    *code = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
    return 1;
}

/* Sonda + control registr + prevzeti aktualniho kodu (bez zapisu kodu). */
static void ad5693_probe(void)
{
    g_dac.probed   = 1u;
    g_dac.probe_ms = HAL_GetTick();
    if (!ad5693_probe_nop()) {
        g_dac.present = 0u;
        g_dac.ctrl_ok = 0u;
        return;
    }
    g_dac.present = 1u;
    g_dac.ctrl_ok = ad5693_write3(AD5693_CMD_CTRL, AD5693_CTRL_VALUE) ? 1u : 0u;
    if (!g_dac.ctrl_ok) g_dac.errors++;
    uint16_t rb;
    if (ad5693_readback(&rb)) {
        g_dac.code_rb = rb;
        g_dac.rb_ok   = 1u;
        /* Prevzit stav DAC (po resetu jen STM32 drzi DAC posledni kod) —
         * jen pokud jsme sami jeste nic nezapsali. */
        if (!g_dac.code_valid) { g_dac.code = rb; g_dac.code_valid = 1u; }
    } else {
        /* Readback je diagnostika s neoverenou formou (viz ad5693.h) — do
         * `errors` se nepocita, jinak by hypoteza o protokolu vypadala jako
         * porucha sbernice. Pozna se z `rb_ok = 0`. */
        g_dac.rb_ok = 0u;
    }
}

/* Po startu vrati do DAC kod z posledniho behu (`g_dac_persist`, nacteny ze
 * syscfg). Bezi jen jednou a teprve kdyz (a) syscfg je nactene, (b) cip je nalezen
 * a ma zapsany control registr (jinak by kod platil pri GAIN x1 = polovicni
 * napeti). Rucni zapis (`req_write`) ma prednost — viz volani nize. */
static void ad5693_restore(void)
{
    static uint8_t tries;
    if (g_dac.restore_done || !g_dac_persist_loaded) return;
    if (!g_dac.present || !g_dac.ctrl_ok) return;

    uint16_t want = g_dac_persist;
    g_dac.restore_code = want;
    if (want == 0u) {                                   /* nic ulozeno = vychozi 0 V */
        g_dac.restore = AD5693_RESTORE_NONE;
        g_dac.restore_done = 1u;
        return;
    }
    if (want > ad5693_mv_to_code(AD5693_RESTORE_MAX_MV)) {
        g_dac.restore = AD5693_RESTORE_RANGE;
        g_dac.restore_done = 1u;
        return;
    }
    if (g_dac.rb_ok && g_dac.code_rb != 0u) {
        /* Reset jen STM32: DAC je napajeny dal a drzi nejnovejsi kod, flash muze
         * byt o debounce okno starsi. Ponechat a flash srovnat podle DAC. */
        g_dac_persist = g_dac.code_rb;
        g_dac.restore = AD5693_RESTORE_KEPT;
        g_dac.restore_done = 1u;
        return;
    }
    if (!ad5693_write3(AD5693_CMD_WRITE_DAC, want)) {
        g_dac.errors++;
        if (++tries >= AD5693_RESTORE_TRIES) {
            g_dac.restore = AD5693_RESTORE_FAIL;
            g_dac.restore_done = 1u;
        }
        return;                                         /* jinak zkusi za 0,5 s */
    }
    g_dac.code       = want;
    g_dac.code_valid = 1u;
    g_dac.writes++;
    uint16_t rb;
    if (ad5693_readback(&rb)) {
        g_dac.code_rb = rb;
        g_dac.rb_ok   = 1u;
        if (rb != want) g_dac.rb_mismatch++;
        g_dac.restore = (rb == want) ? AD5693_RESTORE_DONE : AD5693_RESTORE_FAIL;
    } else {
        g_dac.rb_ok   = 0u;
        g_dac.restore = AD5693_RESTORE_DONE;            /* zapis prosel, readback je jen diagnostika */
    }
    g_dac.restore_done = 1u;
}

/* ── Dither DAC: smycka GPSDO (STATUS #305), rychlost 200 Hz (STATUS #317) ───────────────────────────
 * Smycka (gpsdo.h) posila povel kodu v Q16.16 (`g_gpsdo_code_q16`); zlomek kodu se realizuje ditherem 1. radu
 * (sigma-delta): stredni hodnota kodu je pak presna na 1/65536 LSB, za cenu strideni dvou sousednich kodu.
 * Od v0.21.0 dither bezi v DacTasku po 5 ms (200 Hz), ne v 500ms cyklu SensorsTasku (2 Hz). Duvod: RC pred
 * bufferem (R78 100k x C68 2,2 uF = 220 ms) na 2 Hz tlumi jen malo, takze OCXO videl zvlneni az 3e-11 p-p
 * (ADEV 0,25-1 s az 8e-12). Simulace (RC 220 ms + 1 ms, K 3,4e-11/LSB, scratchpad dither_sim2.py): 200 Hz da
 * zvlneni <= 0,8e-12 p-p a ADEV 0,25-10 s <= 2,4e-13; se zpozdenymi zapisy (0-3 ms) nebo 2 % vynechanych kroku
 * <= 4e-13. 100 Hz je skoro stejne dobre, 50 Hz uz 2-3x horsi. Cena: zapis 3 B @ 400 kHz ~0,1 ms = ~2 % sbernice
 * I2C1 a ~2 % CPU.
 * Vlakna: zapis kodu dela JEDINE DacTask (AboveNormal) pod i2c1MutexHandle; SensorsTask obsluhuje dal sondu,
 * obnovu a rucni zadosti (`ad5693_service`). Kdyby se DacTask nevytvoril (malo haldy), `ad5693_service` dithruje
 * po stare 2 Hz (zaloha). Podminky zapisu se kontroluji znovu UVNITR mutexu, aby dither neprepsal rucni zapis
 * (`dac <kod>`) ani obnovu po startu. Readback se u techto zapisu nedela; kontrolu konec-konec dava AIN0. */
#define DAC_DITH_PERIOD_MS   5u       /* zakladni perioda ditheru (200 Hz) = 5 ticku FreeRTOS */
#define DAC_DITH_BASE_HZ     200u
#define DAC_DITH_DEFAULT_HZ  100u     /* vychozi rychlost ditheru (2026-10-17: zatim 100 Hz, ne 200); `dac dither 200` vrati maximum */
#define DAC_DITH_I2C_TMO_MS  2u       /* zapis 3 B @ 400 kHz trva ~0,1 ms; delsi = sbernice stoji (nezdrzovat 5ms smycku) */
#define DAC_DITH_MUTEX_MS    2u       /* mutex drzi SensorsTask az ~3 ms; kdyz se neziska, krok se preskoci (kod dozapise dalsi) */
#define DAC_DITH_FAIL_MAX    3u       /* po tolika selhanich zapisu za sebou se zkousi jen 1x za DAC_DITH_RETRY_MS */
#define DAC_DITH_RETRY_MS    500u

volatile uint16_t g_dac_dith_div = DAC_DITH_BASE_HZ / DAC_DITH_DEFAULT_HZ;   /* 1 = 200 Hz, 2 = 100 Hz (vychozi) ... 100 = 2 Hz (`dac dither <Hz>`, nepersistuje se) */
volatile uint32_t g_dac_dith_miss;         /* kroky, kdy se nepodarilo ziskat mutex I2C1 */
volatile uint32_t g_dac_dith_skip;         /* kroky vynechane v back-offu po selhani zapisu */
static volatile uint8_t s_dith_alive;      /* 1 = DacTask bezi (SensorsTask pak sam nedithruje) */
static uint32_t s_dith_acc;                /* akumulator zlomku kodu (0..0xFFFF); meni ho jen servo pod mutexem */

/* Jeden krok sigma-delta 1. radu: q = povel Q16.16, *acc = akumulator zlomku. Vraci kod DAC pro tento krok.
 * Na 65536 krocich je soucet kodu presne q (acc se vraci na vychozi hodnotu), kod kolisa o nejvys 1 LSB. */
static uint32_t dither_next(uint32_t *acc, uint32_t q)
{
    uint32_t ci = q >> 16;
    *acc += q & 0xFFFFu;
    if (*acc >= 0x10000u) { *acc -= 0x10000u; if (ci < 0xFFFFu) ci++; }
    return ci;
}

/* Cista logika ditheru (UART `dac dither test`): pro ruzne povely musi soucet kodu za 65536 kroku dat presne q
 * a kod nesmi kolisat o vic nez 1 LSB. @return 1 = PASS. */
int ad5693_dither_selftest(void)
{
    static const uint32_t Q[] = { 0x00000000u, 0x00018000u, 0x00010001u, 0x0001FFFFu, 0x5F5E0123u, 0x9A800000u };
    int ok = 1;
    for (unsigned i = 0; i < sizeof Q / sizeof Q[0]; i++) {
        uint32_t acc = 0u, lo = 0xFFFFFFFFu, hi = 0u;
        uint64_t sum = 0u;
        for (uint32_t k = 0; k < 65536u; k++) {
            uint32_t c = dither_next(&acc, Q[i]);
            sum += c;
            if (c < lo) lo = c;
            if (c > hi) hi = c;
        }
        ok = ok && sum == (uint64_t)Q[i] && acc == 0u && (hi - lo) <= 1u;
    }
    return ok;
}

/* Podminky, za kterych smi dither zapisovat (kontroluje se pred mutexem levne i uvnitr mutexu znovu). */
static int ad5693_servo_ready(void)
{
    return g_gpsdo_drive && g_dac.present && g_dac.ctrl_ok && g_dac.restore_done &&
           !g_dac.req_write && !g_dac.req_probe;
}

/* Jeden krok ditheru. Volat POD i2c1MutexHandle. @return 0 = zapsano / neni co zapisovat, -1 = zapis selhal. */
static int ad5693_servo(uint32_t tmo_ms)
{
    static uint32_t s_persist_ms;
    static uint8_t  s_persist_init;
    int r = 0;
    if (!ad5693_servo_ready()) return 0;                       /* nejdriv obnova / rucni zadost */
    uint32_t q  = g_gpsdo_code_q16;
    uint32_t ci = dither_next(&s_dith_acc, q);
    if (!g_dac.code_valid || ci != g_dac.code) {
        if (ad5693_write3t(AD5693_CMD_WRITE_DAC, (uint16_t)ci, tmo_ms)) {
            g_dac.code = (uint16_t)ci; g_dac.code_valid = 1u; g_dac.writes++;
        } else {
            g_dac.errors++;
            r = -1;
        }
    }
    /* Kod do flash (obnova po startu, #304): jen pri zamceni a nejvys 1x za 10 min (wear W25Q). */
    uint32_t now = HAL_GetTick();
    if (g_gpsdo_lockflag && (!s_persist_init || (uint32_t)(now - s_persist_ms) >= 600000u)) {
        g_dac_persist  = (uint16_t)((q + 0x8000u) >> 16);
        s_persist_ms   = now;
        s_persist_init = 1u;
    }
    return r;
}

/* Rychlost ditheru [Hz]: delic zakladni periody 5 ms. Dopocita se nejblizsi delic, vraci skutecnou rychlost. */
uint16_t ad5693_dither_set_hz(uint16_t hz)
{
    if (hz < 2u) hz = 2u;
    if (hz > DAC_DITH_BASE_HZ) hz = DAC_DITH_BASE_HZ;
    g_dac_dith_div = (uint16_t)((DAC_DITH_BASE_HZ + hz / 2u) / hz);
    return (uint16_t)(DAC_DITH_BASE_HZ / g_dac_dith_div);
}
uint16_t ad5693_dither_hz(void)
{
    uint16_t d = g_dac_dith_div;
    return (uint16_t)(DAC_DITH_BASE_HZ / (d ? d : 1u));
}
int ad5693_dither_task_alive(void) { return s_dith_alive; }

/* DacTask: dither po 5 ms. Kazdy zapis bere mutex I2C1 s kratkym timeoutem; kdyz sbernici drzi SensorsTask, krok se
 * preskoci a dalsi zapise aktualni kod (zpozdeni hrany je jen chyba strid -- viz simulace). Pri opakovanem selhani
 * zapisu (zaseknuta sbernice) se zkousi jen 1x za 500 ms, aby 2ms timeouty nevyhladovely ostatni tasky; obnovu
 * sbernice dela SensorsTask (`i2c1_recover_if_wedged`) a vidi ji i v chybovem kodu `hi2c1`. */
void DacTask_run(void *argument)
{
    (void)argument;
    TickType_t t = xTaskGetTickCount();
    uint32_t n = 0u, fail = 0u, fail_ms = 0u;
    s_dith_alive = 1u;
    for (;;) {
        vTaskDelayUntil(&t, pdMS_TO_TICKS(DAC_DITH_PERIOD_MS));
        uint32_t dv = g_dac_dith_div;
        if (dv < 1u) dv = 1u;
        if (++n < dv) continue;
        n = 0u;
        if (!ad5693_servo_ready()) { fail = 0u; continue; }
        if (fail >= DAC_DITH_FAIL_MAX && (uint32_t)(HAL_GetTick() - fail_ms) < DAC_DITH_RETRY_MS) {
            g_dac_dith_skip++;
            continue;
        }
        if (osMutexAcquire(i2c1MutexHandle, DAC_DITH_MUTEX_MS) != osOK) { g_dac_dith_miss++; continue; }
        int r = ad5693_servo(DAC_DITH_I2C_TMO_MS);
        osMutexRelease(i2c1MutexHandle);
        if (r < 0) { if (fail < 255u) fail++; fail_ms = HAL_GetTick(); }
        else       { fail = 0u; }
    }
}

/* Volano kazdy cyklus I2C1 (~2 Hz) JAKO POSLEDNI transakce pred uvolnenim
 * mutexu — `i2c1_recover_if_wedged()` pak vidi chybovy kod prave odsud. */
static void ad5693_service(void)
{
    if (g_dac.req_probe ||
        (!g_dac.present && (!g_dac.probed ||
                            (uint32_t)(HAL_GetTick() - g_dac.probe_ms) >= AD5693_REPROBE_MS))) {
        ad5693_probe();
        if (g_dac.req_probe) {
            g_dac.req_result = !g_dac.present ? AD5693_RES_ABSENT
                             : g_dac.ctrl_ok  ? AD5693_RES_OK : AD5693_RES_I2C_ERR;
            g_dac.req_probe = 0u;     /* az po vysledku — UART ceka na tenhle flag */
        }
    }

    if (g_dac.req_write) {
        uint16_t code = g_dac.req_code;
        uint8_t  res;
        if (!g_dac.present) {
            res = AD5693_RES_ABSENT;
        } else if (!g_dac.ctrl_ok &&
                   !(g_dac.ctrl_ok = ad5693_write3(AD5693_CMD_CTRL, AD5693_CTRL_VALUE) ? 1u : 0u)) {
            /* Bez control registru by kod platil pri GAIN x1 = polovicni napeti. */
            g_dac.errors++;
            res = AD5693_RES_I2C_ERR;
        } else if (!ad5693_write3(AD5693_CMD_WRITE_DAC, code)) {
            g_dac.errors++;
            res = AD5693_RES_I2C_ERR;
        } else {
            g_dac.code       = code;
            g_dac.code_valid = 1u;
            g_dac.writes++;
            uint16_t rb;
            if (ad5693_readback(&rb)) {
                g_dac.code_rb = rb;
                g_dac.rb_ok   = 1u;
                if (rb == code) res = AD5693_RES_OK;
                else { g_dac.rb_mismatch++; res = AD5693_RES_VERIFY_FAIL; }
            } else {
                g_dac.rb_ok = 0u;
                res = AD5693_RES_OK;   /* zapis prosel; readback je jen diagnostika */
            }
        }
        if (res == AD5693_RES_OK) {
            g_dac_persist = code;         /* ulozi se do flash po ~1,5 s klidu (syscfg) */
            g_dac.restore_done = 1u;      /* rucni zapis ma prednost pred obnovou po startu */
        }
        g_dac.req_result = res;
        g_dac.req_write  = 0u;        /* az po vysledku — UART ceka na tenhle flag */
    }

    ad5693_restore();
    if (!s_dith_alive) (void)ad5693_servo(AD5693_I2C_TMO_MS);   /* zaloha 2 Hz, kdyz DacTask neni */
}

/* ── ADS1115 per-kanal PGA + gain ─────────────────────────────────────────
 * Kazda konverze prepisuje Config registr (kvuli MUX) -> PGA per kanal je zdarma.
 * 🔴 2026-10-02: SKUTECNE osazene delice OVERENY primo z netlistu FPGA_Module_2_1
 * (kicad-cli export, ne z komentare — predchozi "v2.0 rev2" tabulka byla
 * spekulativni a AIN1/AIN2 mela prohozene):
 *   AIN0 OCXO_VC_Sense: R51=15k(top) / R52=10k(GND) -> gain (15+10)/10 = 2,5
 *   AIN1 VBUS (hlavni napajeni PRED regulatory +3V3/+5V, ne RF_Level/AD8307 —
 *          ten na desce FYZICKY NENI, viz RF_LEVEL_HW_PRESENT v calib.h):
 *          R53=100k(top) / R54=4k99(GND) -> gain (100+4,99)/4,99 ~= 21,042
 *   AIN2 +3V3: R55=10k(top) / R56=10k(GND) -> gain 2,0 (g_calib.gain_12v)
 *   AIN3 +5V:  R57=10k(top) / R58=22k(GND) -> gain (10+22)/22 ~= 1,4545 (g_calib.gain_5v)
 * Vsechny ctyri kanaly jedou na +-4.096V (nejvetsi spolecny rozsah, zadny
 * neklipuje) — jemnejsi PGA per kanal by zpresnilo rozliseni, ale je to
 * samostatna optimalizace, ne oprava spravnosti. */
volatile int16_t g_ads_raw_last[4];             /* posledni surovy vysledek AINx (diagnostika) */
volatile uint32_t g_si5356_err_cnt = 0;   /* nepodareni cteni stavu Si5356 od bootu (diagnostika I2C1, /api/fpga) */

/* ── Automaticka re-inicializace Si5356 (STATUS #295, 2026-10-15) ─────────────────────────────────────────────────
 * ZMERENO (M3 2026-10-14, M3b 2026-10-15): po STUDENEM startu hlasi Si5356 trvale LOS_CLKIN + PLL_LOL (status 0x1C)
 * a sam se nezamkne ani za 19 min, prestoze OCXO uz dava 10 MHz (FPGA z nej bezi). `si5356_init` probehl v `main()`
 * driv, nez studeny OCXO dal platny vstup. Novy init (UART `si5356`) zamknul hned (M3b: 0x1C -> 0x00 v uptime 61 s).
 * Proto: trva-li FAULT (LOS_CLKIN nebo PLL_LOL) bez preruseni SI5356_REINIT_FIRST_MS, SensorsTask (vlastnik I2C1) init
 * zopakuje; dalsi pokusy s rozestupem 2x delsim az do SI5356_REINIT_MAX_MS (skutecne chybejici reference se tim
 * neopravi, jen se nema bombardovat I2C1). Zamek rozestup vrati na zacatek.
 * ⚠️ Soft reset v initu shodi i CLK0/1 = hodiny FPGA (L-0148). Pri FAULT uz neplati, FPGA vypadek pozna hlidac
 * (L-0146) a po navratu hodin sama prekalibruje TDC. Generator CLK2-7 vrati init na REGMAP, proto se jeho nastaveni
 * pred initem ulozi a znovu pozada (`si5356_gen_service` ho zapise ve stejnem bloku).
 * ⚠️ Zadny printf (zasobnik SensorsTasku, viz `sensor_update`): vysledek nese `status` (REFERENCE:) a errlog. */
#define SI5356_REINIT_FIRST_MS  10000u     /* FAULT trva bez preruseni -> prvni pokus */
#define SI5356_REINIT_MAX_MS    300000u    /* strop rozestupu pokusu */
volatile uint32_t g_si5356_reinit_cnt;     /* automaticke re-inity od startu (saturuje na 0xFFFF, L-0059) */
volatile uint32_t g_si5356_reinit_ms;      /* HAL tick posledniho pokusu (plati jen pri cnt > 0) */
volatile uint8_t  g_si5356_reinit_res;     /* SI5356_REINIT_RES_* posledniho pokusu */
volatile uint8_t  g_si5356_locked_once;    /* 1 = od startu aspon jednou bez FAULT */

/* Armovani sticky registru (viz `SI5356_STICKY_ARM_MS`). Na urovni souboru, protoze re-init PRED prvnim zamkem ho
 * odklada: sticky bity, ktere nastavi studeny start a re-init, jsou soucast nabehu (zadne mereni jeste neexistovalo,
 * FPGA meri az se zamcenou referenci), stejne jako bity z `si5356_init` v `main()`. Po prvnim zamku se NEodklada —
 * vypadek za behu je skutecny a sticky ho musi drzet. */
static uint8_t  s_si_armed;
static uint32_t s_si_arm_at = SI5356_STICKY_ARM_MS;

/* Vola se pod i2c1MutexHandle po kazdem uspesnem cteni statusu 218. */
static void si5356_lock_watch(uint8_t st)
{
    static uint8_t  s_fault;                           /* 1 = FAULT trva od `s_t0` */
    static uint32_t s_t0;                              /* zacatek FAULT nebo posledni pokus */
    static uint32_t s_iv = SI5356_REINIT_FIRST_MS;     /* rozestup do pristiho pokusu */
    const uint32_t now = HAL_GetTick();

    if ((st & SI5356_FAULT_MASK) == 0u) {
        s_fault = 0u;
        s_iv = SI5356_REINIT_FIRST_MS;
        g_si5356_locked_once = 1u;
        return;
    }
    if (!s_fault) { s_fault = 1u; s_t0 = now; return; }
    if ((uint32_t)(now - s_t0) < s_iv) return;

    const uint8_t before_lock = g_si5356_locked_once ? 0u : 1u;
    uint32_t gen_hz[SI5356_GEN_N];
    uint8_t  gen_on[SI5356_GEN_N];
    for (unsigned i = 0; i < SI5356_GEN_N; i++) { gen_hz[i] = g_si_gen[i].freq_hz; gen_on[i] = g_si_gen[i].on; }

    si5356_init_res_t r;
    (void)si5356_init_quiet(&hi2c1, &r);              /* vysledek nese `r` */

    for (unsigned i = 0; i < SI5356_GEN_N; i++)
        if (g_si_gen[i].freq_hz != gen_hz[i] || g_si_gen[i].on != gen_on[i])
            si5356_gen_request(i, gen_hz[i], gen_on[i] != 0u);

    if (g_si5356_reinit_cnt < 0xFFFFu) g_si5356_reinit_cnt++;
    g_si5356_reinit_ms  = now;
    g_si5356_reinit_res = !r.present                                    ? SI5356_REINIT_RES_NACK
                        : !r.writes_ok                                  ? SI5356_REINIT_RES_WRERR
                        : (r.st_ok && (r.st & SI5356_FAULT_MASK) == 0u) ? SI5356_REINIT_RES_LOCK
                        :                                                 SI5356_REINIT_RES_FAULT;
    if (before_lock) { s_si_armed = 0u; s_si_arm_at = HAL_GetTick() + SI5356_STICKY_ARM_MS; }
    (void)errlog_put(ERRLOG_K_REFINIT, before_lock ? 1u : 2u, g_si5356_reinit_cnt,
                     (uint32_t)st | ((uint32_t)r.st << 8) | ((uint32_t)r.writes_ok << 16) | ((uint32_t)r.st_ok << 17),
                     "Si5356");

    s_t0 = HAL_GetTick();                              /* init trva ~0,1 s; rozestup se meri od jeho konce */
    s_iv = (s_iv >= SI5356_REINIT_MAX_MS / 2u) ? SI5356_REINIT_MAX_MS : s_iv * 2u;
}

/* 2026-10-11: PGA PER VSTUP podle skutecneho rozsahu (vstup ADS = napeti * 1/gain delice):
 *   AIN0 OCXO Vc : +-2,048 V (EFC 0-4 V -> 1,6 V; DAC do 5 V -> 2,0 V; oriznuti od 5,12 V)
 *   AIN1 VBUS    : +-2,048 V (12 V -> 0,57 V; oriznuti od 43 V)
 *   AIN2 +3V3    : +-2,048 V (3,63 V -> 1,8 V; oriznuti od 4,1 V)
 *   AIN3 +5V     : +-4,096 V (5,5 V -> 3,8 V; vic nejde, delic dava 3,44 V u 5 V)
 * Prehled a presnosti: docs/audit/2026-10-11_ads1115-presnost.md. */
static const ads1115_pga_t k_ads_pga[4] = {
    ADS1115_PGA_2V048, ADS1115_PGA_2V048, ADS1115_PGA_2V048, ADS1115_PGA_4V096,
};

/* Fixni gainy pevne dane odporovym delicem (nejsou uzivatelsky editovatelne
 * jako gain_12v/gain_5v, protoze AIN0/AIN1 nejsou v kalibracnim pruvodci —
 * viz WIZ_BR v app_gpsdo.c, jen 2 vetve). */
/* Od 2026-10-11 jsou gainy AIN0 (OCXO Vc) a AIN1 (VBUS) EDITOVATELNE jako ostatni vetve (`g_calib.gain_vc`/`gain_vbus`,
 * okno Kalibrace + pruvodce); vychozi hodnoty z rezistoru jsou v calib.c. */

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
    i2c4_speed_select(I2C4_TIMING_TMP117_400KHZ);   // TMP117 = periferie, ne bit-bang
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
	// === VBUS fast-path (AIN1, ~10 Hz): na NE-sweep ticich, jen kdyz je I2C1
	// zdrava (streak==0 — pri mrtvem busu nehammerovat, sweep ridi back-off).
	// Jedine misto rychleho zapisu SENS_ADS1 mimo sweep (porad jediny writer task).
	// 🔴 2026-10-02: AIN1 = VBUS (viz k_ads_pga komentar vyse), ne RF_Level —
	// gain delice (R53/R54) se musi aplikovat i tady, jinak by fast-path
	// prepsal spravnou hodnotu ze sweepu syrovym mV za ~100 ms.
	if (sub != 0) {
	  if (i2c1_streak == 0) {
		int started = 0;
		if (osMutexAcquire(i2c1MutexHandle, 50) == osOK) {
		  started = ads1115_start(&hi2c1, 1, k_ads_pga[1]);   /* AIN1 = VBUS */
		  osMutexRelease(i2c1MutexHandle);
		}
		if (started) {
		  osDelay(9);                                      /* 128 SPS konverze */
		  int16_t raw; int got = 0;
		  if (osMutexAcquire(i2c1MutexHandle, 50) == osOK) {
			got = ads1115_read_raw(&hi2c1, &raw);
			osMutexRelease(i2c1MutexHandle);
		  }
		  if (got) {
			if (!ADS1115_RAW_SATURATED(raw)) {
			  float mv = ads1115_raw_to_mv_f(raw, k_ads_pga[1]) * g_calib.gain_vbus;
			  sensor_update(SENS_ADS1, mv);
			}
		  }
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
	/* ⚠️ Behem `i2cspeed` se na I2C4 nesaha (kontaminace mereni). Zamerne jako
	 * PRVNI CLEN tohohle retezu, ne `return`/`continue`: za blokem 0x48 nasleduji
	 * senzory na I2C1 a ADC3, ktere s merenim nemaji nic spolecneho (L-0033).
	 * Statistika senzoru se tim NEspini — `sensor_fail` ani streak se nize
	 * nezapocitavaji, takze `sensors` po mereni neukazuje falesne chyby. */
	static uint8_t sweep_prev = 0;
	if (sweep_prev && !g_i2c4_sweep) {
	  /* Sestupna hrana: pokus o zotaveni TMP117. Mereni nad ~125 kHz mu
	   * 2026-09-10 rozhodilo pointer/CONFIG tak, ze pomohl az power-cycle;
	   * tohle uz bezi na obnovenem taktu a nic nestoji. */
	  if (osMutexAcquire(i2c4MutexHandle, 100) == osOK) {
	    i2c4_speed_select(I2C4_TIMING_TMP117_400KHZ);
	    tmp117_set_2hz(&hi2c4, TMP117_ADDR);
	    osMutexRelease(i2c4MutexHandle);
	  }
	}
	sweep_prev = g_i2c4_sweep;
	HAL_StatusTypeDef i2cStatus = HAL_ERROR;
	if (g_i2c4_sweep) {
	  i2cStatus = HAL_BUSY;             /* bezi mereni -> na bus se nesaha */
	} else if (i2c4_skip > 0) {
	  i2c4_skip--;                      /* back-off: tenhle cyklus se na bus nesaha */
	  i2cStatus = HAL_BUSY;             /* != HAL_OK -> sensor_fail nize (drzi posl. dobrou) */
	} else if (osMutexAcquire(i2c4MutexHandle, 100) == osOK) {
	  i2c4_speed_select(I2C4_TIMING_TMP117_400KHZ);
	  i2cStatus = HAL_I2C_Mem_Read( &hi2c4, TMP117_ADDR, TMP117_REG_TEMP, I2C_MEMADD_SIZE_8BIT, rawData, 2, 20);
	  osMutexRelease(i2c4MutexHandle);
	}
	if (i2cStatus == HAL_OK) { i2c4_streak = 0; }
	else if (i2c4_skip == 0 && !g_i2c4_sweep) {
	  if (i2c4_streak < 100) i2c4_streak++;
	  /* Cyklus je 500 ms -> 3x normalne, pak 1 s, 2 s, nakonec 10 s (jako I2C1). */
	  i2c4_skip = (i2c4_streak < 3) ? 0 : (i2c4_streak < 6) ? 1 : (i2c4_streak < 8) ? 3 : 19;
	}
	if (i2cStatus == HAL_OK) {
	  // MSB v rawData[0], LSB v rawData[1]; 0.0078125 °C/LSB
	  tempRaw = (int16_t)((rawData[0] << 8) | rawData[1]);
	  sensor_update(SENS_T48, (float)tempRaw * TMP117_RESOLUTION);
	} else if (!g_i2c4_sweep) {
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
		if (si5356_read_status(&hi2c1, &si_st)) { g_si5356_status = si_st; g_si5356_ok = 1; any_ok = 1; si5356_lock_watch(si_st); }
		else                                    { g_si5356_ok = 0; g_si5356_err_cnt++; }
		/* 🔴 STICKY (reg 247) vedle ziveho stavu: podrzi i mikrosekundovy vypadek
		 * reference, ktery by mezi dvema cteními ziveho registru zmizel beze stopy.
		 * Latchuje se do `g_si5356_sticky` a na cipu se NEMAZE — dokud uzivatel
		 * nepozada, drzi cip i firmware tutez informaci ("stalo se to nekdy").
		 * ⚠️ `SI5356_LOS_XTAL` se maskuje pryc: krystal neni osazen, bit je trvale
		 * 1 a bez masky by hlaseni svitilo napord. */
		{
			/* Armovani po startu — viz `SI5356_STICKY_ARM_MS`; re-init pred prvnim zamkem ho odklada (`s_si_arm_at`). */
			if (!s_si_armed && (int32_t)(HAL_GetTick() - s_si_arm_at) > 0) {
				if (si5356_clear_sticky(&hi2c1, 0xFFu)) {
					g_si5356_sticky = 0;
					s_si_armed = 1;
				}
			}
			uint8_t stk;
			/* ⚠️ Zavorky jsou POVINNE — puvodni `if` byl jednoradkovy a pridani
			 * dalsiho prikazu bez nich by tise vypadlo z podminky. */
			if (si5356_read_sticky(&hi2c1, &stk)) {
				uint8_t prev_stk = g_si5356_sticky;
				g_si5356_sticky |= (uint8_t)(stk & ~SI5356_LOS_XTAL);
				/* 🔑 Nova ztrata reference = nejzavaznejsi udalost, jakou tenhle
				 * pristroj zna: presnost citace JE presnost te reference, takze
				 * mereni z te doby NEPLATI (v datalogu to znaci DATALOG_F_REF_LOSS). */
				if (g_si5356_sticky != prev_stk) {
					(void)errlog_put(ERRLOG_K_REF, g_si5356_sticky, stk, prev_stk, "Si5356");
				}
			}
			/* Vynulovani na zadost (UI/UART). I2C1 vlastni tenhle task, takze
			 * zapis smi udelat JEN on — stejny request/pend vzor jako jinde. */
			if (g_si5356_clr_req) {
				if (si5356_clear_sticky(&hi2c1, 0xFFu)) {
					g_si5356_sticky  = 0;
					g_si5356_clr_req = 0;
				}
				/* Pri neuspechu zadost zustava a zkusi se priste. */
			}
		}
		/* Generator Si5356 (okno GENERATOR / UART `gen`): zapis jedne dvojice vystupu.
		 * I2C1 vlastni tenhle task, takze zapis smi udelat JEN on (vzor `g_si5356_clr_req`). */
		si5356_gen_service(&hi2c1);
		/* DAC ladeni OCXO — POSLEDNI transakce bloku (viz ad5693_service). */
		ad5693_service();
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
		  g_ads_raw_last[ch] = raw;
		  float mv = ads1115_raw_to_mv_f(raw, k_ads_pga[ch]);   /* float: bez orezu pod 1 mV (viz ads1115.h) */
		  /* Vsechny 4 kanaly jdou pres odporovy delic -> skutecne napeti =
			 syrove mV * gain delice (viz k_ads_pga komentar vyse, 2026-10-02).
			 AIN2/AIN3 maji EDITOVATELNOU kalibraci (g_calib, okno Kalibrace) —
			 vychozi = dopocitano z osazenych hodnot (10k/10k, 10k/22k).
			 AIN0/AIN1 maji FIXNI gain (nejsou v kalibracnim pruvodci).
			 ⚠️ Kratke okno pri bootu pred calib_load() (UiTask) jede na vychozich
			 hodnotach z calib.c — kosmeticke, diagnosticke cteni ~1 Hz. */
		  if      (ch == 0) mv *= g_calib.gain_vc;
		  else if (ch == 1) mv *= g_calib.gain_vbus;
		  else if (ch == 2) mv *= g_calib.gain_12v;
		  else if (ch == 3) mv *= g_calib.gain_5v;
		  /* vstup na dorazu PGA = hodnota by se tise orizla -> neplatne misto zavadejiciho cisla */
		  if (ADS1115_RAW_SATURATED(raw)) sensor_fail(sid);
		  else { sensor_update(sid, mv); any_ok = 1; }
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
