/**
 * @file theme.c
 * @brief Runtime palety: TMAVA (default, = puvodni konstanty) + SVETLA.
 *
 * Prepnuti = zmena ukazatele g_ui_theme (ui_theme_select). Volajici pak MUSI
 * zneplatnit predrenderovane cache (bg_cache — screen_main_invalidate) a
 * prekreslit; jinak zustane stary podklad. Zadne dalsi zavislosti.
 */

#include <ui/theme.h>

static const ui_theme_t THEME_DARK = {
    .bg0        = PRIM_RGB(0x06, 0x09, 0x0E),
    .bg1        = PRIM_RGB(0x0A, 0x0F, 0x17),
    .bg_card    = PRIM_RGB(0x0B, 0x10, 0x18),
    .hdr_top    = PRIM_RGB(0x08, 0x0C, 0x12),
    .hdr_bot    = PRIM_RGB(0x06, 0x09, 0x0E),
    .line       = PRIM_RGB(0x15, 0x20, 0x30),
    .line_hi    = PRIM_RGB(0x1F, 0x2D, 0x42),
    .axis       = PRIM_RGB(0x5A, 0x68, 0x78),
    .ink        = PRIM_RGB(0xE3, 0xED, 0xF7),
    .ink2       = PRIM_RGB(0x9A, 0xA6, 0xB4),
    .ink3       = PRIM_RGB(0x7C, 0x88, 0x96),
    .ink4       = PRIM_RGB(0x3A, 0x48, 0x58),
    .ink5       = PRIM_RGB(0x1E, 0x27, 0x36),
    .acc        = PRIM_RGB(0x38, 0xBD, 0xF8),
    .acc_soft   = PRIM_RGB(0x7D, 0xD3, 0xFC),
    .ok         = PRIM_RGB(0x34, 0xD3, 0x99),
    .ok_soft    = PRIM_RGB(0x86, 0xEF, 0xAC),
    .ok_bg      = PRIM_RGB(0x0C, 0x1E, 0x15),
    .ok_border  = PRIM_RGB(0x1D, 0x4D, 0x2E),
    .warn       = PRIM_RGB(0xFB, 0xBF, 0x24),
    .bad        = PRIM_RGB(0xF8, 0x71, 0x71),
    .violet     = PRIM_RGB(0xA7, 0x8B, 0xFA),
    .btn_run_top    = PRIM_RGB(0x0F, 0x2C, 0x1C),
    .btn_run_bot    = PRIM_RGB(0x0A, 0x1D, 0x11),
    .btn_run_border = PRIM_RGB(0x2E, 0x64, 0x42),
    .btn_act_top    = PRIM_RGB(0x0F, 0x22, 0x3A),
    .btn_act_bot    = PRIM_RGB(0x0A, 0x16, 0x26),
    .btn_act_border = PRIM_RGB(0x2A, 0x4A, 0x6E),
    /* STOP = cervena obdoba RUN (stejne jasy, jen cerveny odstin). */
    .btn_stop_top    = PRIM_RGB(0x2C, 0x0F, 0x14),
    .btn_stop_bot    = PRIM_RGB(0x1D, 0x0A, 0x0E),
    .btn_stop_border = PRIM_RGB(0x64, 0x2E, 0x36),
    .freq_stop_bg    = PRIM_RGBA(0xF8, 0x71, 0x71, 38),   /* ~15 % — "lehce cervenne" */
};

/* Svetla paleta: stejne role, prevracene jasy; akcenty ztmavene kvuli kontrastu
 * na svetlem podkladu (sky/emerald/amber/red v "600" odstinech). */
static const ui_theme_t THEME_LIGHT = {
    .bg0        = PRIM_RGB(0xE9, 0xEE, 0xF3),
    .bg1        = PRIM_RGB(0xDD, 0xE5, 0xEC),
    .bg_card    = PRIM_RGB(0xF7, 0xFA, 0xFC),
    .hdr_top    = PRIM_RGB(0xD8, 0xE0, 0xE8),
    .hdr_bot    = PRIM_RGB(0xE9, 0xEE, 0xF3),
    .line       = PRIM_RGB(0xC0, 0xCB, 0xD6),
    .line_hi    = PRIM_RGB(0xA8, 0xB6, 0xC4),
    .axis       = PRIM_RGB(0x64, 0x72, 0x80),
    .ink        = PRIM_RGB(0x14, 0x1E, 0x28),
    .ink2       = PRIM_RGB(0x38, 0x46, 0x54),
    .ink3       = PRIM_RGB(0x5A, 0x68, 0x76),
    .ink4       = PRIM_RGB(0x94, 0xA2, 0xB0),
    .ink5       = PRIM_RGB(0xC4, 0xCE, 0xD8),
    .acc        = PRIM_RGB(0x02, 0x84, 0xC7),
    .acc_soft   = PRIM_RGB(0x03, 0x69, 0xA1),
    .ok         = PRIM_RGB(0x05, 0x96, 0x69),
    .ok_soft    = PRIM_RGB(0x10, 0xB9, 0x81),
    .ok_bg      = PRIM_RGB(0xDA, 0xF2, 0xE6),
    .ok_border  = PRIM_RGB(0x7F, 0xCF, 0xA8),
    .warn       = PRIM_RGB(0xB4, 0x53, 0x09),
    .bad        = PRIM_RGB(0xDC, 0x26, 0x26),
    .violet     = PRIM_RGB(0x7C, 0x3A, 0xED),
    .btn_run_top    = PRIM_RGB(0xD2, 0xEB, 0xDC),
    .btn_run_bot    = PRIM_RGB(0xC2, 0xE3, 0xCF),
    .btn_run_border = PRIM_RGB(0x59, 0xA8, 0x77),
    .btn_act_top    = PRIM_RGB(0xD4, 0xE4, 0xF5),
    .btn_act_bot    = PRIM_RGB(0xC3, 0xD8, 0xEE),
    .btn_act_border = PRIM_RGB(0x6E, 0x96, 0xC2),
    .btn_stop_top    = PRIM_RGB(0xF6, 0xD7, 0xD7),
    .btn_stop_bot    = PRIM_RGB(0xEF, 0xC7, 0xC7),
    .btn_stop_border = PRIM_RGB(0xC9, 0x7A, 0x7A),
    /* Na svetlem podkladu staci nizsi alfa (tmava cervena vic "prekryva"). */
    .freq_stop_bg    = PRIM_RGBA(0xDC, 0x26, 0x26, 30),
};

const ui_theme_t *g_ui_theme = &THEME_DARK;

void ui_theme_select(int light) { g_ui_theme = light ? &THEME_LIGHT : &THEME_DARK; }
