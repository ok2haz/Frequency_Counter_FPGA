#pragma once
/**
 * @file    iwdg2.h
 * @brief   IWDG2 = nezavisly watchdog jadra CM4 (zrcadlo IWDG1 na CM7, viz watchdog.c).
 *
 * LSI ~32 kHz, prescaler /64 -> 500 Hz, reload 2000 -> ~4 s timeout. Registrovy
 * pristup (KR/PR/RLR); HAL_IWDG modul je v CM4 hal_conf VYPNUTY -> nezavisle a
 * regen-safe. CM4 je bare-metal (bez RTOS) -> zadny heartbeat, jen prosty kick
 * v kazde iteraci hlavni smycky. Kdyz se CM4 smycka zasekne (nekope), IWDG2 -> reset.
 *
 * ⚠️ RESET SCOPE (OVERIT NA HW pred spolehanim): IWDG2 je na H7 dual-core parovan
 * s domenou CPU2 (CM4) — samostatny reset flag `RCC_RSR_IWDG2RSTF` (bit 27) oddeleny
 * od IWDG1 (bit 26). PREDPOKLAD: IWDG2 resetuje JEN CM4, ne cely system (jinak by
 * dve nezavisle IWDG nemely smysl). To je nutne pro princip NAVRH §11.4 — zaseknuty
 * CM4 NESMI shodit CM7/displej. Kdyby se pri overeni ukazal SYSTEM reset, IWDG2 na
 * CM4 NEpouzivat a misto nej resit CM4 recovery z CM7 (RCC hold/release CPU2).
 *
 * ⚠️ Volat AZ po beep melodii (blokuje ~1,2 s) — init az tesne pred smyckou, aby
 * startup neprodleva nespotrebovala timeout. V DEBUG buildu freeze na breakpointu.
 */
#ifdef __cplusplus
extern "C" {
#endif

/** Spusti IWDG2 (~4 s). Volat jednou v main USER CODE 2, po beep, pred smyckou. */
void iwdg2_init(void);

/** Obnovi citac IWDG2. Volat v kazde iteraci hlavni smycky CM4 (perioda << 4 s). */
void iwdg2_kick(void);

#ifdef __cplusplus
}
#endif
