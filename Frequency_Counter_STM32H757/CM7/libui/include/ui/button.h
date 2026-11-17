#pragma once
/**
 * @file button.h
 * @brief Footer button: gradient body, border, one or two text lines.
 */

#include <prim/types.h>
#include <ui/api.h>

typedef enum {
    UI_BUTTON_NORMAL,
    UI_BUTTON_ACTIVE,
    UI_BUTTON_RUN,
    UI_BUTTON_STOP,   /**< cervena — destruktivni/zastavujici akce (footer STOP) */
} ui_button_variant_t;

typedef struct {
    prim_rect_t rect;
    ui_button_variant_t variant;
    const char *label;
    const char *value;              /**< NULL = single line; else two lines */
} ui_button_t;

UI_API void ui_button_render(const ui_button_t *btn);

/** Pozorovatel VYKRESLENYCH tlacitek. Nastavuje app vrstva; libui ho zavola pro
 *  kazde tlacitko, ktere projde `ui_button_render`.
 *
 *  Proc: model fokusu (encoder) potrebuje znat tlacitka aktualniho okna. Vyjmenovat
 *  je rucne u ~45 oken by byla duplicita, ktera by se rozesla pri prvni zmene
 *  layoutu. `ui_button_render` je jedine hrdlo, kterym VSECHNA tlacitka prochazeji,
 *  takze se seznam sestavi sam pri kresleni okna.
 *  ⚠️ libui tim neziskava zavislost na app — jen vola callback, ktery dostane. */
typedef void (*ui_button_observer_t)(const prim_rect_t *rect);
UI_API void ui_button_set_observer(ui_button_observer_t obs);
