/**
 * @file    si5356.h
 * @brief   Silicon Labs Si5356A clock generator @ I2C1 0x70 (FPGA deska).
 *
 * Aplikuje kompletni register map z ClockBuilder Pro (CBPro) + oficialni
 * SiLabs "apply" proceduru (disable outputs -> soft reset -> enable outputs).
 * Register map je prevzata z uzivatelova I2C trace (cil: 100 MHz + fazovy posun);
 * frekvence/faze jsou DANE TOUTO MAPOU (tj. CBPro konfiguraci), driver je jen aplikuje.
 *
 * Pri zmene konfigurace: v CBPro nastav vystupy/fazi, exportuj "C Code Header"
 * a nahrad obsah REGMAP[] v si5356.c (format {addr, val, mask} je stejny).
 */
#ifndef SI5356_H
#define SI5356_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define SI5356_I2C_ADDR   (0x70u << 1)   /* 7-bit 0x70 -> HAL 8-bit */

/* ── Bitove masky stavu (AN565) ─────────────────────────────────────────────
 * 🔴 JEDINY ZDROJ. Do 2026-09-02 byly tytez masky definovane DVAKRAT
 * (`SI5356_*` v `app_gpsdo.c`, `SI_*` v `screen_main.c`) — presne ten druh
 * duplicity, pred kterou projekt varuje: jednou uz se u tohohle cipu bitova
 * mapa rozesla s realitou (bit2 vs bit3, viz [[si5356-los-clkin]]).
 *
 * ⚠️ TYTEZ pozice plati pro ZIVY registr 218 (0xDA) I pro STICKY 247 (0xF7).
 * ⚠️ `SI5356_LOS_XTAL` se na teto desce NEHODNOTI — krystal XA/XB neni osazen
 *    (piny uzemnene), takze bit je TRVALE 1; status `0x04` je normalni stav.
 * ⚠️ `SI5356_PLL_LOL` se pri FYZICKE ztrate vstupu NEasertuje (AN565: LOL je
 *    rozdil >5000 ppm na PFD, ne odpojeny vstup) -> ztratu reference hlasi
 *    `SI5356_LOS_CLKIN`. */
#define SI5356_SYS_CAL    (1u << 0)
#define SI5356_LOS_XTAL   (1u << 2)
#define SI5356_LOS_CLKIN  (1u << 3)
#define SI5356_PLL_LOL    (1u << 4)

/** Bity, ktere na teto desce znamenaji SKUTECNY problem s referenci. */
#define SI5356_FAULT_MASK (SI5356_LOS_CLKIN | SI5356_PLL_LOL)

/** Aplikuje register map + apply proceduru. Volat jednou (boot) na I2C1.
 *  @return true pokud vsechny I2C zapisy ACKovaly. */
bool si5356_init(I2C_HandleTypeDef *hi2c);

/** Precte status reg 218 (0xDA). Bitova mapa dle AN565: bit0=SYS_CAL (probiha
 *  kalibrace), bit2=LOS_XTAL (krystal XA/XB — na teto desce NEOSAZEN, piny
 *  uzemnene -> bit je TRVALE 1, benigni), bit3=LOS_CLKIN (ztrata 10 MHz na
 *  pinu 4 — SKUTECNA ztrata reference; pozor, PLL_LOL se pri fyzicke ztrate
 *  vstupu neasertuje), bit4=PLL_LOL (rozdil >5000 ppm na PFD). */
bool si5356_read_status(I2C_HandleTypeDef *hi2c, uint8_t *status);

/* ── STICKY stav (registr 247 = 0xF7) ───────────────────────────────────────
 * 🔴 PROC to existuje: registr 218 je ZIVY, takze kratky vypadek reference mezi
 * dvema cteními (polling 2x/s) je pro firmware NEVIDITELNY. U kmitoctoveho
 * normalu je to metrologicky podstatne — kolisani BEHEM mereni znehodnocuje
 * vysledek, a pristroj o nem dosud nemel jak vedet.
 *
 * AN565 (overeno primo v `Si5356A_AN565.pdf`): 247 je STICKY verze 218 se
 * SHODNYMI bitovymi pozicemi, typ R/W, a plati *„Only a soft or POR reset or
 * writing a 0 to this bit will clear it."* Sticky bit tedy podrzi i
 * mikrosekundovy glitch a polling 2x/s ho spolehlive zachyti.
 *
 * ⚠️ INTR pin se kvuli tomu zapojovat NEMUSI — pridal by jen latenci (us vs
 * 500 ms), ne detekci. (A na teto desce ma zustat NC bez pull-upu, viz STATUS #103.) */
#define SI5356_REG_STICKY  0xF7u   /* 247 */

/* 🔴 JEDNORAZOVE ARMOVANI po startu. ZMERENO na desce 2026-09-02: hned po bootu
 * hlasi sticky **LOS_CLKIN + PLL_LOL + SYS_CAL**, zatimco zivy registr 218 je
 * cisty (`0x04` = jen neosazeny krystal). Je to HISTORIE ZE ZASYNCHRONIZOVANI —
 * `si5356_init` dela soft reset a nez se PLL chytne, vsechny tri bity legitimne
 * probliknou. Overeno: po vynulovani zustava sticky 0 i po desitkach sekund.
 *
 * Bez armovani by tedy pristroj kricel "vypadek reference" po KAZDEM zapnuti —
 * a uzivatel by se to naucil ignorovat, cimz by hlaseni ztratilo smysl
 * (tatáz chyba jako falesne `SBERNICE MRTVA`, STATUS #114).
 *
 * ⚠️ Armuje se BEZ OHLEDU na zivy stav: kdyby reference chybela i po arm case,
 * hlasi to ZIVY registr 218 (SYS pilulka cervena). Sticky ma jediny ukol —
 * zachytit to, co mezi dvema ctenimi ziveho registru zmizi. */
#define SI5356_STICKY_ARM_MS  5000u

/** Precte sticky registr 247. Bitove pozice = stejne jako u `si5356_read_status`. */
bool si5356_read_sticky(I2C_HandleTypeDef *hi2c, uint8_t *sticky);

/** Vynuluje sticky bity uvedene v `mask` (AN565: maze se ZAPISEM NULY).
 *  Read-modify-write, takze ostatni sticky bity zustanou zachovane. */
bool si5356_clear_sticky(I2C_HandleTypeDef *hi2c, uint8_t mask);

#endif /* SI5356_H */
