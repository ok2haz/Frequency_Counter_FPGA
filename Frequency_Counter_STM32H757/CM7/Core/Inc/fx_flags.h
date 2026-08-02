#pragma once
/**
 * @file fx_flags.h
 * @brief Bitmaska grafickych efektu g_fx_enabled (okno Animace -> subokno EFEKTY).
 *
 * Bezzavislostni header (jen <stdint.h>) — includuje ho firmware (freertos.c,
 * syscfg.c pres freertos_shared.h) i app vrstva (screen_main.c, app_gpsdo.c).
 * Kazdy bit = jeden vizualni efekt, ktery jde samostatne ZAP/VYP; efekt take
 * potrebuje kod, ktery ho kresli (fallback = puvodni vzhled bez efektu).
 *
 * Persist JEN v syscfg flash blobu (SCF5) — NE v BKP. (Historicky: efektu bylo 7,
 * v DR6 pod RTC_SYSCFG2_MAGIC volne jen bity 10:15 = 6 -> nevesly se. Po odstraneni
 * Headline glow je efektu 6, ale persist zustava flash-only kvuli konzistenci.)
 * syscfg_load cte flash i pri warm resetu a aplikuje g_fx_enabled vzdy. Default
 * vse ZAP. Definice globalu je ve freertos.c.
 */
#include <stdint.h>

#define FX_WATERFALL   (1u<<0)   /* spektrogram odchylky kmitoctu (okno s_view=26) */
#define FX_ALLAN_CONF  (1u<<1)   /* konfidencni pas kolem ADEV krivky (Allan) */
#define FX_HOLD_CONE   (1u<<2)   /* holdover drift-prediction kuzel */
#define FX_OCXO_GAUGE  (1u<<3)   /* analogovy budik OCXO ladiciho napeti (Holdover) */
#define FX_SPARK_FILL  (1u<<4)   /* gradientni vypln pod trend sparkline */
#define FX_SYS_XFADE   (1u<<5)   /* eased prolinani barvy SYS pilulky */
/* (1u<<6) bylo FX_HEAD_GLOW — efekt odstranen 2026-07-26 (na prani uzivatele).
 * Bit se uz necte; pripadny zbytkovy 1 ve starem ulozenem blobu je neskodny. */
#define FX_ALL         0x3Fu     /* vsech 6 efektu ZAP */

extern volatile uint16_t g_fx_enabled;
