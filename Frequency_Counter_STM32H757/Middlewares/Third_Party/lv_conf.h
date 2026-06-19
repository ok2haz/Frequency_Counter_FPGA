/**
 * @file    lv_conf.h
 * @brief   LVGL v9 konfigurace pro GPSDO UI (STM32H757, RGB565 framebuffer).
 *
 * Nastavuje jen to podstatne; ostatni makra doplni lv_conf_internal.h defaulty.
 * Build: definuj LV_CONF_INCLUDE_SIMPLE (a tento soubor na include path), nebo
 * nastav LV_CONF_PATH. Slad s VERZI vlozene LVGL (ciluje v9.x); pri jine verzi
 * srovnej s dodanym lv_conf_template.h.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Barevna hloubka 16 bit (RGB565) - shoda s LTDC framebufferem. */
#define LV_COLOR_DEPTH        16

/* Pamet pro LVGL objekty (staticka obrazovka ~80 objektu). Builtin alokator. */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE           (64U * 1024U)

/* Cas: pouzivame lv_tick_set_cb(HAL_GetTick) v lv_port_disp_init. */
#define LV_DEF_REFR_PERIOD    16          /* ~60 FPS strop refresh */

/* OS: lv_timer_handler volame rucne z UiTask (zadny vestaveny RTOS wrapper). */
#define LV_USE_OS             LV_OS_NONE

/* Log/asserty minimalne (bring-up muzes zapnout). */
#define LV_USE_LOG            0
#define LV_USE_ASSERT_NULL    1
#define LV_USE_ASSERT_MALLOC  1

/* DPI (800x480 4,3") */
#define LV_DPI_DEF            130

/* === Fonty ===
 * montserrat_14 = vychozi/fallback (placeholder dokud nejsou JBMono/Inter, viz theme.h).
 * Po vygenerovani vlastnich fontu je pridej do buildu a v theme.h zapni FONTS_READY. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

/* Widgety pouzite na obrazovce (label/line + zaklad). Ostatni default. */
#define LV_USE_LABEL          1
#define LV_USE_LINE           1
#define LV_USE_IMAGE          1

#endif /* LV_CONF_H */
