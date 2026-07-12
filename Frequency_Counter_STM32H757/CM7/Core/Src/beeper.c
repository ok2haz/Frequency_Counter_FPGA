/**
 * @file    beeper.c
 * @brief   Pasivni beeper PH9, 800 Hz pres TIM7. Viz beeper.h.
 *
 * TIM7 kernel = 240 MHz (APB1 timer clock). PSC=239 -> 1 MHz, ARR=624 -> update
 * 1600 Hz. Kazdy update prepne PH9 -> ctvercovy signal 800 Hz.
 */
#include "beeper.h"
#include "stm32h7xx_hal.h"
#include "cmsis_os2.h"   /* osDelay — boot melodie (task kontext) */

#define BEEP_ARR_800HZ  624u   /* 1 MHz / 625 = 1600 Hz update -> 800 Hz toggle */

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
        __HAL_TIM_SET_AUTORELOAD(&htim7, BEEP_ARR_800HZ);   /* alarm = pevnych 800 Hz */
        HAL_TIM_Base_Start_IT(&htim7);
    } else {
        HAL_TIM_Base_Stop_IT(&htim7);
        HAL_GPIO_WritePin(BEEP_PORT, BEEP_PIN, GPIO_PIN_RESET);   /* idle low */
    }
}

/* Tón zadane frekvence [Hz] (0 = ticho). TIM7 update = 2x freq -> ctvercovy signal. */
void beeper_tone(uint16_t freq_hz)
{
    if (freq_hz == 0) { beeper_set(false); return; }
    uint32_t arr = 1000000u / (2u * (uint32_t)freq_hz);   /* 1 MHz timer clock */
    if (arr) arr--;
    __HAL_TIM_SET_AUTORELOAD(&htim7, arr);
    if (!s_on) {
        s_on = true;
        __HAL_TIM_SET_COUNTER(&htim7, 0);
        HAL_TIM_Base_Start_IT(&htim7);
    }
}

/* Boot melodie: kratky vzestupny C-dur arpeggio + rozlozeni ("power-on" jingle).
 * Blokujici (osDelay) — vola se JEDNOU pri startu z UiTasku (grace watchdogu kryje). */
void beeper_boot_melody(void)
{
    static const struct { uint16_t f, ms; } NOTES[] = {
        { 784, 95 }, { 1047, 95 }, { 1319, 95 }, { 1568, 190 },   /* G5 C6 E6 G6 */
    };
    for (unsigned i = 0; i < sizeof(NOTES) / sizeof(NOTES[0]); i++) {
        beeper_tone(NOTES[i].f);
        osDelay(NOTES[i].ms);
        beeper_set(false);
        osDelay(14);                 /* kratka pauza mezi tony (artikulace) */
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
