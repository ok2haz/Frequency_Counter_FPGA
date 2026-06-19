/**
 * @file chart.c
 * @brief Log-log chart rendering (grid, floor line, curve, cursor).
 */

#include <ui/chart.h>
#include <ui/theme.h>
#include <ui/fonts.h>
#include <prim/prim.h>

void ui_chart_loglog_render_grid(const ui_chart_loglog_t *c)
{
    if (c == NULL || c->x_decade_count <= 0 || c->y_decade_count <= 0) return;
    const prim_rect_t *in = &c->inner;

    /* Vertical decade lines + x tick labels. */
    for (int16_t i = 0; i <= c->x_decade_count; i++) {
        int16_t x = (int16_t)(in->x + (int32_t)i * in->w / c->x_decade_count);
        prim_draw_line((prim_point_t){x, in->y},
                       (prim_point_t){x, (int16_t)(in->y + in->h)}, 1,
                       UI_COLOR_LINE);
        if (c->x_tick_labels && c->x_tick_labels[i]) {
            prim_draw_text((prim_point_t){x, (int16_t)(in->y + in->h + 14)},
                           c->x_tick_labels[i], &ui_font_mono_14,
                           UI_COLOR_INK_4, PRIM_ALIGN_CENTER);
        }
    }
    /* Horizontal decade lines + y tick labels. */
    for (int16_t j = 0; j <= c->y_decade_count; j++) {
        int16_t y = (int16_t)(in->y + (int32_t)j * in->h / c->y_decade_count);
        prim_draw_line((prim_point_t){in->x, y},
                       (prim_point_t){(int16_t)(in->x + in->w), y}, 1,
                       UI_COLOR_LINE);
        if (c->y_tick_labels && c->y_tick_labels[j]) {
            prim_draw_text((prim_point_t){(int16_t)(in->x - 6), (int16_t)(y + 4)},
                           c->y_tick_labels[j], &ui_font_mono_14,
                           UI_COLOR_INK_4, PRIM_ALIGN_RIGHT);
        }
    }
    if (c->x_label) {
        prim_draw_text((prim_point_t){(int16_t)(in->x + in->w),
                                      (int16_t)(in->y + in->h + 28)},
                       c->x_label, &ui_font_sans_14, UI_COLOR_INK_3,
                       PRIM_ALIGN_RIGHT);
    }
}

void ui_chart_loglog_render_floor(const ui_chart_loglog_t *c, int16_t y_pixel,
                                  const char *label)
{
    if (c == NULL) return;
    const prim_rect_t *in = &c->inner;
    prim_draw_line_dashed((prim_point_t){in->x, y_pixel},
                          (prim_point_t){(int16_t)(in->x + in->w), y_pixel},
                          1, UI_COLOR_WARN, 6, 4);
    if (label) {
        prim_draw_text((prim_point_t){(int16_t)(in->x + 6), (int16_t)(y_pixel - 4)},
                       label, &ui_font_sans_14, UI_COLOR_WARN, PRIM_ALIGN_LEFT);
    }
}

void ui_chart_loglog_render_curve(const ui_chart_loglog_t *c,
                                  const prim_point_t *points, int16_t count,
                                  prim_color_t stroke_color, bool fill_below)
{
    if (c == NULL || points == NULL || count < 2) return;
    const prim_rect_t *in = &c->inner;

    if (fill_below) {
        int16_t base = (int16_t)(in->y + in->h);
        for (int16_t i = 1; i < count; i++) {
            prim_point_t a = {(int16_t)(in->x + points[i - 1].x),
                              (int16_t)(in->y + points[i - 1].y)};
            prim_point_t b = {(int16_t)(in->x + points[i].x),
                              (int16_t)(in->y + points[i].y)};
            /* Vertical fill columns between curve and base (cheap area fill). */
            for (int16_t x = a.x; x <= b.x; x++) {
                int16_t y = (b.x == a.x) ? a.y
                    : (int16_t)(a.y + (int32_t)(b.y - a.y) * (x - a.x) / (b.x - a.x));
                prim_fill_rect((prim_rect_t){x, y, 1, (int16_t)(base - y)},
                               PRIM_ALPHA(stroke_color, 0x18), PRIM_BLEND_OVER);
            }
        }
    }
    for (int16_t i = 1; i < count; i++) {
        prim_point_t a = {(int16_t)(in->x + points[i - 1].x),
                          (int16_t)(in->y + points[i - 1].y)};
        prim_point_t b = {(int16_t)(in->x + points[i].x),
                          (int16_t)(in->y + points[i].y)};
        prim_draw_line(a, b, 2, stroke_color);
    }
}

void ui_chart_loglog_render_cursor(const ui_chart_loglog_t *c, prim_point_t pos,
                                   const char *label)
{
    if (c == NULL) return;
    const prim_rect_t *in = &c->inner;
    prim_draw_line_dashed((prim_point_t){pos.x, in->y},
                          (prim_point_t){pos.x, (int16_t)(in->y + in->h)},
                          1, UI_COLOR_ACC, 4, 4);
    prim_fill_circle(pos, 3, UI_COLOR_ACC);
    if (label) {
        prim_draw_text((prim_point_t){(int16_t)(pos.x + 6), (int16_t)(in->y + 12)},
                       label, &ui_font_sans_14, UI_COLOR_ACC_SOFT, PRIM_ALIGN_LEFT);
    }
}
