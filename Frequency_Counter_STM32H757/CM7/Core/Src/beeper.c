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

/* ⚠️ `volatile`: stav cte i `alarm_tick` z defaultTasku, zatimco boot melodie
 * bezi v UiTasku (audit F-0103). */
static volatile bool s_on = false;
static bool s_ready = false;   /* TIM7 se podarilo nastavit (audit F-0111) */

/* 1 = prave hraje blokujici boot melodie z UiTasku. `alarm_tick` (defaultTask)
 * se po tu dobu pipaku NEDOTKNE, takze mimo tohle okno je defaultTask JEDINY
 * volajici `beeper_set`/`beeper_tone` — a `s_on` uz nema dva zapisovatele
 * (audit F-0103). */
static volatile uint8_t s_melody_busy = 0;

bool beeper_ready(void)       { return s_ready; }
bool beeper_melody_busy(void) { return s_melody_busy != 0u; }

bool beeper_init(void)
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
    /* 🔴 Vyhodnoceno (audit F-0111): kdyz TIM7 nenabehne, je pristroj TRVALE NEMY
     * — a s nim i alarm na ztratu 10MHz reference a na vybehnuti OCXO z pasma,
     * tedy prave to, kvuli cemu zvuk existuje. Uzivatel to nema jak poznat:
     * `beep test` taky nic neudela a odpovi, jako by pipl. `Error_Handler` tu
     * ZAMERNE nevolame — nemy pipak neni duvod shodit merici pristroj; stav
     * se jen zverejni (`beeper_ready()` -> `status`). */
    if (HAL_TIM_Base_Init(&htim7) != HAL_OK) return false;

    HAL_NVIC_SetPriority(TIM7_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    s_ready = true;
    return true;
}

void beeper_set(bool on)
{
    if (on == s_on) return;
    s_on = on;
    /* ⚠️ Navraty `HAL_TIM_Base_Start_IT`/`Stop_IT` se ignoruji VEDOME (L-0003):
     * selhat mohou jen pri nekonzistentnim `htim7.State`, a ten je hlidany uz
     * pri initu (`beeper_ready()`). Vyhodnocovat je tady by nemelo co ohlasit —
     * volajici (`pattern_service`) na to nema jak reagovat. */
    if (on) {
        __HAL_TIM_SET_AUTORELOAD(&htim7, BEEP_ARR_800HZ);   /* alarm = pevnych 800 Hz */
        (void)HAL_TIM_Base_Start_IT(&htim7);
    } else {
        (void)HAL_TIM_Base_Stop_IT(&htim7);
        HAL_GPIO_WritePin(BEEP_PORT, BEEP_PIN, GPIO_PIN_RESET);   /* idle low */
    }
}

/* Tón zadane frekvence [Hz] (0 = ticho). TIM7 update = 2x freq -> ctvercovy signal. */
void beeper_tone(uint16_t freq_hz)
{
    if (freq_hz == 0) { beeper_set(false); return; }
    /* 🔴 MEZ PRED VYPOCTEM (audit F-0110, lekce L-0015/L-0030). TIM7 ma 16bitovy
     * `ARR`; pro `freq_hz < 8` vyjde `1e6/(2*f)` nad 65535 a zapis se TISE orizne
     * — misto 7 Hz by zaznelo ~85 Hz. Dnes to nikdo nevolal (jediny volajici je
     * `beeper_boot_melody` s 784-1568 Hz), ale funkce je v hlavicce a je to tataz
     * trida pasti jako `fmt_fixed(dec >= 4)`, ktera uz jednou kousla. */
    if (freq_hz < BEEPER_FREQ_MIN_HZ) freq_hz = BEEPER_FREQ_MIN_HZ;
    if (freq_hz > BEEPER_FREQ_MAX_HZ) freq_hz = BEEPER_FREQ_MAX_HZ;
    uint32_t arr = 1000000u / (2u * (uint32_t)freq_hz);   /* 1 MHz timer clock */
    if (arr) arr--;
    __HAL_TIM_SET_AUTORELOAD(&htim7, arr);
    if (!s_on) {
        s_on = true;
        __HAL_TIM_SET_COUNTER(&htim7, 0);
        (void)HAL_TIM_Base_Start_IT(&htim7);   /* viz zduvodneni v `beeper_set` */
    }
}

/* Boot melodie: kratky vzestupny C-dur arpeggio + rozlozeni ("power-on" jingle).
 * Blokujici (osDelay) — vola se JEDNOU pri startu z UiTasku (grace watchdogu kryje). */
void beeper_boot_melody(void)
{
    static const struct { uint16_t f, ms; } NOTES[] = {
        { 784, 95 }, { 1047, 95 }, { 1319, 95 }, { 1568, 190 },   /* G5 C6 E6 G6 */
    };
    /* 🔴 Vzajemne vylouceni s `alarm_tick` (audit F-0103). Melodie bezi
     * v UiTasku a je blokujici; `alarm_tick` bezi v defaultTasku 100x/s a taky
     * vola `beeper_set`. Bez tohohle priznaku mely `s_on` i stav TIM7 DVA
     * zapisovatele, a ztraceny zapis do `s_on` by je rozesel se skutecnym stavem
     * periferie — nejhur tak, ze TIM7 bezi, ale `s_on == false`, takze
     * `beeper_set(false)` se vrati na prvni radce a PIPAK TROUBI SOUVISLE.
     * Dosazitelne to bylo pres `s_click_req` (dotek behem bootu) a pres mute
     * vetev `alarm_tick`.
     * ⚠️ Neni to zamek: kdyz je defaultTask uz UVNITR `pattern_service`, jeden
     * ton se muze uriznout. Cena plneho zamku (mutex kolem pipaku) je vyssi nez
     * ta vada; presun melodie do defaultTasku by zase sahal na casovani startu
     * (CLAUDE.md 4c). */
    s_melody_busy = 1;
    for (unsigned i = 0; i < sizeof(NOTES) / sizeof(NOTES[0]); i++) {
        beeper_tone(NOTES[i].f);
        osDelay(NOTES[i].ms);
        beeper_set(false);
        osDelay(14);                 /* kratka pauza mezi tony (artikulace) */
    }
    s_melody_busy = 0;
}

bool beeper_is_on(void)
{
    return s_on;
}

void beeper_isr_toggle(void)
{
    HAL_GPIO_TogglePin(BEEP_PORT, BEEP_PIN);
}
