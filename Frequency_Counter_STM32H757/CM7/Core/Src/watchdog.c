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
#include "flightrec.h"   /* flightrec_dump — kontext pred resetem (#18) */
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
static unsigned char s_stall_logged;   /* black-box se zapisuje jen jednou za beh */

/* Crash black-box pro STALL (kind 3) — stejny format jako FreeRTOS hooky ve
 * freertos_hooks.c (BKP_DR3 magic+kind, DR4/DR5 jmeno tasku), takze to po resetu
 * precte tentyz kod v MX_RTC_Init -> g_crash_text = "stall:UiTask".
 * ⚠️ Bez tohoto zaznamu je prosty IWDG reset NEDIAGNOSTIKOVATELNY: vis jen ze
 * watchdog stekl, ne KTERY task prestal koupat. Prime zapisy RTC->BKPxR (HAL tu
 * nevolat — bezime ve smycce defaultTask a HAL_RTCEx_* zamyka backup domenu). */
static void stall_blackbox(const char *name)
{
    PWR->CR1 |= PWR_CR1_DBP;
    unsigned int n0 = 0, n1 = 0;
    for (int i = 0; i < 8 && name[i]; i++) {
        if (i < 4) n0 |= (unsigned int)(unsigned char)name[i] << (8 * i);
        else       n1 |= (unsigned int)(unsigned char)name[i] << (8 * (i - 4));
    }
    RTC->BKP4R = n0;
    RTC->BKP5R = n1;
    RTC->BKP3R = 0xC7A50000u | 3u;   /* RTC_CRASH_MAGIC | kind 3 = stall (magic naposled) */
}

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
    int ui_stale   = ((now - s_ui_ms)   >= WDG_STALL_MS);
    int fpga_stale = ((now - s_fpga_ms) >= WDG_STALL_MS);
    if (!ui_stale && !fpga_stale) {
        IWDG1->KR = IWDG_KEY_RELOAD;
        return;
    }
    /* Nechame IWDG vyprset -> HW reset. Nez k tomu dojde (~1,5 s zbyva), zapis
     * DO ZALOHOVANE domeny KDO se zasekl — po restartu to ukaze System Health
     * ("stall:UiTask") a UART `status`. Jen jednou: opakovany zapis pri 100 Hz
     * by nic nepridal a zbytecne sahal do BKP. */
    if (!s_stall_logged) {
        s_stall_logged = 1;
        stall_blackbox(ui_stale ? (fpga_stale ? "BOTH" : "UiTask") : "FpgaTask");
        /* Black-box rekne KDO se zasekl, flight recorder CO SE DELO PREDTIM —
         * teprve dohromady je z toho diagnostika (viz flightrec.h a #18).
         * Bezime v defaultTask se zivym schedulerem a cilovy sektor je uz
         * smazany, takze je to jen zapis stranek (jednotky ms) — do vyprseni
         * IWDG (~1,5 s) se to pohodlne vejde. */
        flightrec_dump("stall");
    }
}
