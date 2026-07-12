#pragma once
/**
 * @file theme.h
 * @brief Central palette — the single source of truth for color (PUBLIC API).
 *
 * No literal hex color is allowed anywhere else in the repo (lint check). All
 * values are ARGB8888 compositing colors (libprim packs them to RGB565 on
 * write). Roles follow the GPSDO v9 reference mockup.
 *
 * RUNTIME THEMES: barvy nejsou konstanty, ale pole aktivni palety (theme.c —
 * TMAVA default / SVETLA). Makra UI_COLOR_* zustavaji jako call-site API, jen
 * derefuji `g_ui_theme` -> prepnuti tematu NEVYZADUJE zmenu volajicich.
 * ⚠️ Proto UI_COLOR_* NELZE pouzit ve file-scope `static const` inicializatorech.
 * Po prepnuti je nutne zneplatnit predrenderovane cache (screen_main_invalidate).
 */

#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    /* pozadi */
    prim_color_t bg0, bg1, bg_card, hdr_top, hdr_bot;
    /* linky */
    prim_color_t line, line_hi, axis;
    /* text */
    prim_color_t ink, ink2, ink3, ink4, ink5;
    /* akcenty */
    prim_color_t acc, acc_soft, ok, ok_soft, ok_bg, ok_border, warn, bad, violet;
    /* tlacitka */
    prim_color_t btn_run_top, btn_run_bot, btn_run_border;
    prim_color_t btn_act_top, btn_act_bot, btn_act_border;
} ui_theme_t;

UI_API extern const ui_theme_t *g_ui_theme;   /* aktivni paleta (default tmava) */

/** Prepne paletu (0 = tmava, 1 = svetla). Volajici MUSI nasledne zneplatnit
 *  predrenderovane cache (bg_cache) a prekreslit obrazovku. */
UI_API void ui_theme_select(int light);
UI_API int  ui_theme_is_light(void);

/* ── Background ─────────────────────────────────────────────── */
#define UI_COLOR_BG_0           (g_ui_theme->bg0)
#define UI_COLOR_BG_1           (g_ui_theme->bg1)
#define UI_COLOR_BG_CARD        (g_ui_theme->bg_card)
#define UI_COLOR_BG_HEADER_TOP  (g_ui_theme->hdr_top)
#define UI_COLOR_BG_HEADER_BOT  (g_ui_theme->hdr_bot)

/* ── Lines and borders ──────────────────────────────────────── */
#define UI_COLOR_LINE           (g_ui_theme->line)
#define UI_COLOR_LINE_HI        (g_ui_theme->line_hi)
#define UI_COLOR_AXIS_INK       (g_ui_theme->axis)

/* ── Text ───────────────────────────────────────────────────── */
#define UI_COLOR_INK            (g_ui_theme->ink)
#define UI_COLOR_INK_2          (g_ui_theme->ink2)
#define UI_COLOR_INK_3          (g_ui_theme->ink3)
#define UI_COLOR_INK_4          (g_ui_theme->ink4)
#define UI_COLOR_INK_5          (g_ui_theme->ink5)

/* ── Accents ────────────────────────────────────────────────── */
#define UI_COLOR_ACC            (g_ui_theme->acc)
#define UI_COLOR_ACC_SOFT       (g_ui_theme->acc_soft)
#define UI_COLOR_OK             (g_ui_theme->ok)
#define UI_COLOR_OK_SOFT       (g_ui_theme->ok_soft)
#define UI_COLOR_OK_BG          (g_ui_theme->ok_bg)
#define UI_COLOR_OK_BORDER      (g_ui_theme->ok_border)
#define UI_COLOR_WARN           (g_ui_theme->warn)
#define UI_COLOR_BAD            (g_ui_theme->bad)
#define UI_COLOR_VIOLET         (g_ui_theme->violet)

/* ── Buttons ────────────────────────────────────────────────── */
#define UI_COLOR_BTN_RUN_TOP    (g_ui_theme->btn_run_top)
#define UI_COLOR_BTN_RUN_BOT    (g_ui_theme->btn_run_bot)
#define UI_COLOR_BTN_RUN_BORDER (g_ui_theme->btn_run_border)
#define UI_COLOR_BTN_ACT_TOP    (g_ui_theme->btn_act_top)
#define UI_COLOR_BTN_ACT_BOT    (g_ui_theme->btn_act_bot)
#define UI_COLOR_BTN_ACT_BORDER (g_ui_theme->btn_act_border)
