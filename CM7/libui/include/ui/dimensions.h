#pragma once
/**
 * @file dimensions.h
 * @brief Layout dimensions — PUBLIC API. No magic numbers elsewhere.
 */

#define UI_DIM_SCREEN_W            800
#define UI_DIM_SCREEN_H            480

#define UI_DIM_HEADER_H            56
#define UI_DIM_FOOTER_H            64
#define UI_DIM_BODY_Y              UI_DIM_HEADER_H
#define UI_DIM_BODY_H              (UI_DIM_SCREEN_H - UI_DIM_HEADER_H - UI_DIM_FOOTER_H)

#define UI_DIM_PADDING_X           25
#define UI_DIM_PADDING_Y           13

#define UI_DIM_PILL_RADIUS         21
#define UI_DIM_PILL_H              30
#define UI_DIM_PILL_PAD_X          12
#define UI_DIM_PILL_PAD_Y          6
#define UI_DIM_PILL_GAP            5
#define UI_DIM_PILL_INNER_GAP      6

#define UI_DIM_CARD_RADIUS         16
#define UI_DIM_CARD_PAD_X          6
#define UI_DIM_CARD_PAD_Y          9

#define UI_DIM_BUTTON_RADIUS       14
#define UI_DIM_BUTTON_H            60
#define UI_DIM_BUTTON_GAP          10
