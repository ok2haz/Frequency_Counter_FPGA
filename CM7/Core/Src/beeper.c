/**
 * @file    beeper.c
 * @brief   Pasivni beeper PH9, 800 Hz pres TIM7. Viz beeper.h.
 *
 * TIM7 kernel = 240 MHz (APB1 timer clock). PSC=239 -> 1 MHz, ARR=624 -> update
 * 1600 Hz. Kazdy update prepne PH9 -> ctvercovy signal 800 Hz.
 */
#include "beeper.h"
#include "stm32h7xx_hal.h"

TIM_HandleTypeDef htim7;

#define BEEP_PORT   GPIOH
#define BEEP_PIN    GPIO_PIN_9

static bool s_on = false;

void beeper_init(void)
{
    /* PH9 jako vystup, idle low */
    __HAL_RCC_GPIOH_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = BEEP_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BEEP_PORT, &g);
    HAL_GPIO_WritePin(BEEP_PORT, BEEP_PIN, GPIO_PIN_RESET);

    /* TIM7 @ 1600 Hz update */
    __HAL_RCC_TIM7_CLK_ENABLE();
    htim7.Instance               = TIM7;
    htim7.Init.Prescaler         = 239;    /* 240 MHz / 240 = 1 MHz */
    htim7.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim7.Init.Period            = 624;    /* 1 MHz / 625 = 1600 Hz -> toggle -> 800 Hz */
    htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim7);

    HAL_NVIC_SetPriority(TIM7_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
}

void beeper_set(bool on)
{
    if (on == s_on) return;
    s_on = on;
    if (on) {
        HAL_TIM_Base_Start_IT(&htim7);
    } else {
        HAL_TIM_Base_Stop_IT(&htim7);
        HAL_GPIO_WritePin(BEEP_PORT, BEEP_PIN, GPIO_PIN_RESET);   /* idle low */
    }
}

bool beeper_is_on(void)
{
    return s_on;
}

void beeper_isr_toggle(void)
{
    HAL_GPIO_TogglePin(BEEP_PORT, BEEP_PIN);
}
