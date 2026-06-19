/**
 * @file test_pill.c
 * @brief Smoke test: pill measures a width and paints into a host buffer.
 */

#include <ui/ui.h>
#include <prim/prim.h>
#include <assert.h>
#include <string.h>

#define W 200
#define H 40

static prim_pixel_t buf[W * H];
static prim_fb_t fb;

static int any_set(void)
{
    for (int i = 0; i < W * H; i++) if (buf[i]) return 1;
    return 0;
}

int main(void)
{
    memset(buf, 0, sizeof(buf));
    prim_fb_init(&fb, buf, W, H, W * sizeof(prim_pixel_t));
    prim_set_target(&fb);
    prim_reset_clip();

    ui_pill_t p = {.x = 4, .y = 4, .variant = UI_PILL_OK, .value = "GNSS LOCK",
                   .has_led = true};
    int16_t w = ui_pill_measure(&p);
    assert(w > 0 && w < W);

    ui_pill_render(&p);
    assert(p.computed_width == w);
    assert(any_set());                 /* something was drawn */

    return 0;
}
