#pragma once
/**
 * @file theme.h
 * @brief Central palette — the single source of truth for color (PUBLIC API).
 *
 * No literal hex color is allowed anywhere else in the repo (lint check). All
 * values are ARGB8888 compositing colors (libprim packs them to RGB565 on
 * write). Roles follow the GPSDO v9 reference mockup.
 */

#include <prim/types.h>

/* ── Background ─────────────────────────────────────────────── */
#define UI_COLOR_BG_0           PRIM_RGB(0x06, 0x09, 0x0E)
#define UI_COLOR_BG_1           PRIM_RGB(0x0A, 0x0F, 0x17)
#define UI_COLOR_BG_CARD        PRIM_RGB(0x0B, 0x10, 0x18)
#define UI_COLOR_BG_HEADER_TOP  PRIM_RGB(0x08, 0x0C, 0x12)
#define UI_COLOR_BG_HEADER_BOT  PRIM_RGB(0x06, 0x09, 0x0E)

/* ── Lines and borders ──────────────────────────────────────── */
#define UI_COLOR_LINE           PRIM_RGB(0x15, 0x20, 0x30)
#define UI_COLOR_LINE_HI        PRIM_RGB(0x1F, 0x2D, 0x42)
#define UI_COLOR_AXIS_INK       PRIM_RGB(0x5A, 0x68, 0x78)

/* ── Text ───────────────────────────────────────────────────── */
#define UI_COLOR_INK            PRIM_RGB(0xE3, 0xED, 0xF7)
#define UI_COLOR_INK_2          PRIM_RGB(0x9A, 0xA6, 0xB4)
#define UI_COLOR_INK_3          PRIM_RGB(0x6B, 0x77, 0x85)
#define UI_COLOR_INK_4          PRIM_RGB(0x3A, 0x48, 0x58)
#define UI_COLOR_INK_5          PRIM_RGB(0x1E, 0x27, 0x36)

/* ── Accents ────────────────────────────────────────────────── */
#define UI_COLOR_ACC            PRIM_RGB(0x38, 0xBD, 0xF8)
#define UI_COLOR_ACC_SOFT       PRIM_RGB(0x7D, 0xD3, 0xFC)
#define UI_COLOR_OK             PRIM_RGB(0x34, 0xD3, 0x99)
#define UI_COLOR_OK_SOFT        PRIM_RGB(0x86, 0xEF, 0xAC)
#define UI_COLOR_OK_BG          PRIM_RGB(0x0C, 0x1E, 0x15)
#define UI_COLOR_OK_BORDER      PRIM_RGB(0x1D, 0x4D, 0x2E)
#define UI_COLOR_WARN           PRIM_RGB(0xFB, 0xBF, 0x24)
#define UI_COLOR_BAD            PRIM_RGB(0xF8, 0x71, 0x71)
#define UI_COLOR_VIOLET         PRIM_RGB(0xA7, 0x8B, 0xFA)

/* ── Buttons ────────────────────────────────────────────────── */
#define UI_COLOR_BTN_RUN_TOP    PRIM_RGB(0x0F, 0x2C, 0x1C)
#define UI_COLOR_BTN_RUN_BOT    PRIM_RGB(0x0A, 0x1D, 0x11)
#define UI_COLOR_BTN_RUN_BORDER PRIM_RGB(0x2E, 0x64, 0x42)
#define UI_COLOR_BTN_ACT_TOP    PRIM_RGB(0x0F, 0x22, 0x3A)
#define UI_COLOR_BTN_ACT_BOT    PRIM_RGB(0x0A, 0x16, 0x26)
#define UI_COLOR_BTN_ACT_BORDER PRIM_RGB(0x2A, 0x4A, 0x6E)
