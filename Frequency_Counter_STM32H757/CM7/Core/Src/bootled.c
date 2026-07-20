/**
 * @file    bootled.c
 * @brief   Boot-time HW selftest diagnostika na LED_1 (PG3). Viz bootled.h.
 */
#include "bootled.h"
#include "main.h"

static volatile uint8_t s_step = 0;

/* LED_1 je aktivni v LOW (stejna konvence jako existujici bring-up bliknuti
 * v fmc.c po SDRAM initu: RESET=svit, SET=tma). */
static void led_set(uint8_t on)
{
    HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* Nezavisle na MX_GPIO_Init poradi (Error_Handler muze v teorii spadnout i
 * pred nim, viz SystemClock_Config) - idempotentni, bezpecne zavolat znovu. */
static void led_gpio_init(void)
{
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LED_1_Pin;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_1_GPIO_Port, &gpio);
}

/* DWT cyklovy citac - nezavisly na SysTick (Error_Handler vypina IRQ, takze
 * HAL_Delay/HAL_GetTick by uz netikaly). */
static void dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_ms(uint32_t ms)
{
    uint32_t ticks = ms * (SystemCoreClock / 1000u);
    uint32_t start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) { __NOP(); }
}

static void delay_us(uint32_t us)
{
    uint32_t ticks = us * (SystemCoreClock / 1000000u);
    uint32_t start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) { __NOP(); }
}

/* ── Pipani soubezne s blikanim ─────────────────────────────────────────────
 * ⚠️ NEPOUZIVA beeper.c! Ten generuje ton pres TIM7 IRQ, jenze bootled_fail()
 * bezi z Error_Handler(), kde jsou PRERUSENI VYPNUTA (presne proto tenhle modul
 * pouziva i DWT misto HAL_Delay). TIM7 by tedy nikdy netikl a beeper by mlcel.
 * Ton se proto bit-banguje primo na PH9 stejnym DWT casovanim jako blikani.
 * Duvod pridani: kdyz selze panel, LED je jediny vystup — a v krabicce nemusi
 * byt videt. Pipani projde i zavrenym pristrojem.
 * Pozn.: mute z Nastaveni (g_sound_muted) se ZAMERNE neuplatnuje — tohle neni
 * UX zvuk, ale hlaseni poruchy pri bootu (a pri ranem Error_Handleru nemusi byt
 * BKP s nastavenim jeste ani nactena). */
#define BOOT_BEEP_PORT   GPIOH
#define BOOT_BEEP_PIN    GPIO_PIN_9
#define BOOT_BEEP_HALF_US 625u   /* pulperioda -> 800 Hz (shodne s beeper.c) */

static void beep_gpio_init(void)
{
    __HAL_RCC_GPIOH_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = BOOT_BEEP_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOOT_BEEP_PORT, &gpio);
    HAL_GPIO_WritePin(BOOT_BEEP_PORT, BOOT_BEEP_PIN, GPIO_PIN_RESET);   /* idle low */
}

/* Pipa 800 Hz po dobu ms (blokujici, DWT casovani). Konci vzdy v idle low. */
static void beep_ms(uint32_t ms)
{
    uint32_t halves = (ms * 1000u) / BOOT_BEEP_HALF_US;   /* 150 ms -> 240 pulperiod */
    uint8_t lvl = 0;
    for (uint32_t i = 0; i < halves; i++) {
        lvl ^= 1u;
        HAL_GPIO_WritePin(BOOT_BEEP_PORT, BOOT_BEEP_PIN,
                          lvl ? GPIO_PIN_SET : GPIO_PIN_RESET);
        delay_us(BOOT_BEEP_HALF_US);
    }
    HAL_GPIO_WritePin(BOOT_BEEP_PORT, BOOT_BEEP_PIN, GPIO_PIN_RESET);
}

/* LED a ton jdou SOUBEZNE: svit + pipnuti 150 ms, pak tma + ticho 150 ms.
 * Pocet bliknuti/pipnuti = poradove cislo kroku, ktery selhal (bootled_step). */
static void blink_pattern(uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        led_set(1);
        beep_ms(150);     /* misto delay_ms(150) — stejna delka, navic ton */
        led_set(0);
        delay_ms(150);
    }
}

void bootled_step(uint8_t step)
{
    s_step = step;
}

void bootled_blink_once(uint8_t count)
{
    led_gpio_init();
    beep_gpio_init();
    dwt_enable();
    blink_pattern(count);
}

void bootled_fail(void)
{
    led_gpio_init();
    beep_gpio_init();
    dwt_enable();
    for (;;) {
        blink_pattern(s_step);
        delay_ms(800);
    }
}
