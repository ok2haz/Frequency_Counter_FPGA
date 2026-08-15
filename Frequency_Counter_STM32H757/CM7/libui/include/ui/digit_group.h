#pragma once
/**
 * @file digit_group.h
 * @brief Run of digit segments at different certainty levels, with underline.
 */

#include <prim/text.h>
#include <ui/api.h>

typedef enum {
    UI_DIGIT_CERTAIN,
    UI_DIGIT_SIGMA,
    UI_DIGIT_FLOOR,
} ui_digit_level_t;

typedef struct {
    const char *text;
    ui_digit_level_t level;
    bool with_underline;
} ui_digit_segment_t;

typedef struct {
    int16_t x, y;                   /**< baseline origin */
    const prim_font_t *font;
    const ui_digit_segment_t *segments;
    int16_t segment_count;
} ui_digit_group_t;


/** Ink color pro danou uroven jistoty (CERTAIN/SIGMA/FLOOR) — verejny obal nad
 *  internim ui_level_color, aby si app vrstva mohla spocitat "normalni" barvu
 *  segmentu (napr. pro navrat po docasnem zvyrazneni), aniz by znala libui
 *  interni layout.h. */
