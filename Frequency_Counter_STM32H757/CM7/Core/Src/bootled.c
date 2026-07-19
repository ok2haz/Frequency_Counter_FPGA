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

static void blink_pattern(uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        led_set(1);
        delay_ms(150);
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
    dwt_enable();
    blink_pattern(count);
}

void bootled_fail(void)
{
    led_gpio_init();
    dwt_enable();
    for (;;) {
        blink_pattern(s_step);
        delay_ms(800);
    }
}
