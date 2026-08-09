/**
 * @file    iwdg2.c
 * @brief   IWDG2 nezavisly watchdog CM4 (viz iwdg2.h). Registrovy pristup,
 *          zrcadlo CM7 watchdog.c (IWDG1) — stejne parametry (~4 s).
 */
#include "iwdg2.h"
#include "stm32h7xx_hal.h"

#define IWDG_KEY_UNLOCK   0x5555u
#define IWDG_KEY_RELOAD   0xAAAAu
#define IWDG_KEY_START    0xCCCCu
#define IWDG_PR_DIV64     4u          /* prescaler /64 -> 32 kHz/64 = 500 Hz */
#define IWDG_RELOAD_4S    2000u       /* 2000/500 Hz = 4 s */

void iwdg2_init(void)
{
#ifdef DEBUG
    __HAL_DBGMCU_FREEZE2_IWDG2();      /* na breakpointu neresetuj CM4 (APB4FZ2) */
#endif
    /* Kanonicka sekvence dle RM0399 (stejne jako HAL_IWDG_Init a CM7 watchdog_init):
     * nejdriv START — ten HW zapne LSI a rozbehne SR (PVU/RVU) propagaci; PR/RLR
     * update by se bez bezicich hodin nikdy nepotvrdil. */
    IWDG2->KR  = IWDG_KEY_START;       /* spust (uz nelze zastavit krome resetu) */
    IWDG2->KR  = IWDG_KEY_UNLOCK;      /* odemkni PR/RLR */
    IWDG2->PR  = IWDG_PR_DIV64;
    IWDG2->RLR = IWDG_RELOAD_4S;
    for (uint32_t i = 0; i < 100000u && IWDG2->SR != 0u; i++) { }   /* PVU/RVU (bounded) */
    IWDG2->KR  = IWDG_KEY_RELOAD;      /* nahraj citac z noveho RLR */
}

void iwdg2_kick(void) { IWDG2->KR = IWDG_KEY_RELOAD; }
