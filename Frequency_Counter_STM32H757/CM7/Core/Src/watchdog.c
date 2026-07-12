/**
 * @file    watchdog.c
 * @brief   IWDG1 watchdog + heartbeat kontrola kritickych tasku. Viz watchdog.h.
 *
 * IWDG1 bezi z LSI (~32 kHz). Prescaler /64 -> 500 Hz, reload 2000 -> ~4 s timeout.
 * Registrovy pristup (KR/PR/RLR) — HAL_IWDG modul je v hal_conf VYPNUTY, tohle je
 * nezavisle a regen-safe.
 *
 * Heartbeat: UiTask a FpgaTask volaji watchdog_kick_* (zapisi HAL_GetTick()).
 * watchdog_supervise() (defaultTask) obnovi IWDG jen kdyz oba heartbeaty jsou
 * mladsi nez WDG_STALL_MS. Po startu je grace okno (tasky se rozjizdi).
 */
#include "watchdog.h"
#include "stm32h7xx_hal.h"

#define IWDG_KEY_UNLOCK   0x5555u
#define IWDG_KEY_RELOAD   0xAAAAu
#define IWDG_KEY_START    0xCCCCu
#define IWDG_PR_DIV64     4u          /* prescaler /64 -> 32 kHz/64 = 500 Hz */
#define IWDG_RELOAD_4S    2000u       /* 2000/500 Hz = 4 s */

#define WDG_STALL_MS      2500u       /* task bez heartbeatu déle -> povazuj za mrtvy */
#define WDG_GRACE_MS      8000u       /* po init: bezpodmineny refresh (boot tasku) */

static volatile unsigned int s_ui_ms;
static volatile unsigned int s_fpga_ms;
static unsigned int s_init_ms;
static unsigned char s_ready;

void watchdog_init(void)
{
#ifdef DEBUG
    __HAL_DBGMCU_FREEZE_IWDG1();       /* na breakpointu neresetuj */
#endif
    s_init_ms = HAL_GetTick();
    s_ui_ms = s_fpga_ms = s_init_ms;

    /* Kanonicka sekvence dle RM0399 (stejne jako HAL_IWDG_Init): nejdriv START —
     * to HW zapne LSI a rozbehne SR (PVU/RVU) propagaci; PR/RLR update by se bez
     * bezicich hodin nikdy nepotvrdil. Nez se PR/RLR propaguji (~µs), citac bezi
     * na defaultu /4 + 0xFFF (~0,5 s) — bohate pokryto nasledujicim RELOAD. */
    IWDG1->KR  = IWDG_KEY_START;       /* spust (uz nelze zastavit krome resetu) */
    IWDG1->KR  = IWDG_KEY_UNLOCK;      /* odemkni PR/RLR */
    IWDG1->PR  = IWDG_PR_DIV64;
    IWDG1->RLR = IWDG_RELOAD_4S;
    for (uint32_t i = 0; i < 100000u && IWDG1->SR != 0u; i++) { }   /* PVU/RVU (bounded) */
    IWDG1->KR  = IWDG_KEY_RELOAD;      /* nahraj citac z noveho RLR */
    s_ready = 1;
}

void watchdog_kick_ui(void)   { s_ui_ms = HAL_GetTick(); }
void watchdog_kick_fpga(void) { s_fpga_ms = HAL_GetTick(); }

void watchdog_supervise(void)
{
    if (!s_ready) return;
    unsigned int now = HAL_GetTick();

    /* Startup grace: dokud se tasky rozjizdi, obnovuj bezpodmienecne. */
    if ((now - s_init_ms) < WDG_GRACE_MS) {
        IWDG1->KR = IWDG_KEY_RELOAD;
        return;
    }
    /* Obnov jen kdyz oba kriticke tasky nedavno koply. */
    if ((now - s_ui_ms) < WDG_STALL_MS && (now - s_fpga_ms) < WDG_STALL_MS) {
        IWDG1->KR = IWDG_KEY_RELOAD;
    }
    /* jinak: nechame IWDG vyprset -> HW reset */
}
