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

/* Dosazeny stav po initu — cte ho `iwdg2_config_ok()` (audit F-0137). */
static uint8_t s_iwdg2_pr_ok, s_iwdg2_rlr_ok;

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
    /* >> VYSLEDEK CEKANI SE VYHODNOCUJE (audit F-0137). Ohranicena smycka tu byla
     * spravne, ale jeji vysledek se ZAHAZOVAL a pokracovalo se bezpodminecne — tedy
     * presne nalez F-0104, ktery uz byl opraveny na CM7 (`watchdog.c`, commit
     * `76bf1f6`), jen se oprava nepreneslana dvojce (lekce L-0012).
     * >> PROC to neni kosmetika: kdyby se `PVU`/`RVU` nepropagovaly, PR ani RLR by
     * se neuplatnily a platily by RESET DEFAULTY `PR=0`/`RLR=0xFFF`, tedy timeout
     * ~0,5 s misto 4 s. Na CM4 je to horsi nez na CM7: jadro nema konzoli, takze
     * kratky timeout by se projevil jako nevysvetlitelne resety.
     * >> `IWDG2` je dnes ZAMERNE vypnuty (`iwdg2_init` se z `CM4/main.c` nevola,
     * protoze reset scope IWDG2 je system-wide) — tohle je tedy priprava na chvili,
     * kdy se zapne, a zduvodneni v `main.c` s tim vyslovne pocita. */
    uint32_t guard = 100000u;
    while (guard-- && IWDG2->SR != 0u) { }          /* PVU/RVU (ohranicene) */
    IWDG2->KR  = IWDG_KEY_RELOAD;                  /* nahraj citac z noveho RLR */
    /* Overeni DOSAZENEHO STAVU z registru (vzor L-0055 / L-0009): kdyz se hodnoty
     * nepropagovaly, hlas to navenek misto tiche zmeny timeoutu na 0,5 s. */
    s_iwdg2_pr_ok  = (IWDG2->PR  == IWDG_PR_DIV64);
    s_iwdg2_rlr_ok = (IWDG2->RLR == IWDG_RELOAD_4S);
}

/* 1 = `PR`/`RLR` se po initu opravdu propagovaly (audit F-0137). Pri 0 bezi IWDG2
 * na reset defaultech, tedy ~0,5 s misto 4 s — a tise. */
int iwdg2_config_ok(void) { return (s_iwdg2_pr_ok && s_iwdg2_rlr_ok) ? 1 : 0; }

void iwdg2_kick(void) { IWDG2->KR = IWDG_KEY_RELOAD; }
