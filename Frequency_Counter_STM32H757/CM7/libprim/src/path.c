/**
 * @file path.c
 * @brief Composite path: build ops, flatten to a polyline, stroke or fill.
 *
 * Allocation happens only in prim_path_create()/destroy() (explicit lifecycle,
 * outside the render hot path). Stroke/fill flatten into a fixed-size scratch
 * polyline — no allocation per call.
 */

#include <prim/path.h>
#include <prim/shapes.h>
#include <math.h>
#include <stdlib.h>
#include "internal/path_impl.h"
#include "internal/bezier.h"
#include "internal/rasterizer.h"

#define PATH_MAX_FLAT PRIM_RASTER_MAX_PTS

prim_path_t *prim_path_create(int16_t max_ops)
{
    if (max_ops <= 0) max_ops = 16;
    prim_path_t *p = (prim_path_t *)malloc(sizeof(*p));
    if (p == NULL) return NULL;
    p->ops = (prim_path_op_t *)malloc((size_t)max_ops * sizeof(prim_path_op_t));
    if (p->ops == NULL) { free(p); return NULL; }
    p->op_count = 0;
    p->op_capacity = max_ops;
    p->current_point = (prim_point_t){0, 0};
    p->start_point = (prim_point_t){0, 0};
    p->is_closed = false;
    return p;
}

void prim_path_destroy(prim_path_t *p)
{
    if (p == NULL) return;
    free(p->ops);
    free(p);
}

static void push(prim_path_t *p, prim_path_op_t op)
{
    if (p == NULL || p->op_count >= p->op_capacity) return;
    p->ops[p->op_count++] = op;
}

void prim_path_move_to(prim_path_t *p, prim_point_t pt)
{
    push(p, (prim_path_op_t){.kind = PRIM_OP_MOVE, .p0 = pt});
    if (p) { p->current_point = pt; p->start_point = pt; }
}

void prim_path_line_to(prim_path_t *p, prim_point_t pt)
{
    push(p, (prim_path_op_t){.kind = PRIM_OP_LINE, .p0 = pt});
    if (p) p->current_point = pt;
}

void prim_path_close(prim_path_t *p)
{
    push(p, (prim_path_op_t){.kind = PRIM_OP_CLOSE});
    if (p) { p->current_point = p->start_point; p->is_closed = true; }
}

/* Flatten the path into a polyline. Returns point count. */
static int16_t flatten(const prim_path_t *p, prim_point_t *out, int16_t cap)
{
    int16_t n = 0;
    prim_point_t cur = {0, 0};
    prim_point_t start = {0, 0};
    for (int16_t i = 0; i < p->op_count; i++) {
        const prim_path_op_t *op = &p->ops[i];
        switch (op->kind) {
        case PRIM_OP_MOVE:
            cur = op->p0; start = op->p0;
            if (n < cap) out[n++] = cur;
            break;
        case PRIM_OP_LINE:
            cur = op->p0;
            if (n < cap) out[n++] = cur;
            break;
        case PRIM_OP_QUAD: {
            int16_t segs = prim_internal_quad_seg_count(cur, op->p1, op->p0, 32);
            for (int16_t s = 1; s <= segs && n < cap; s++) {
                int32_t t = (int32_t)s * 256 / segs;
                out[n++] = prim_internal_quad_eval(cur, op->p1, op->p0, t);
            }
            cur = op->p0;
            break;
        }
        case PRIM_OP_ARC: {
            int16_t segs = (int16_t)(abs(op->a1) / 6 + 2);
            for (int16_t s = 0; s <= segs && n < cap; s++) {
                float a = (op->a0 + (float)op->a1 * s / segs) * 0.0174532925f;
                out[n++] = (prim_point_t){
                    (int16_t)(op->p1.x + op->radius * cosf(a)),
                    (int16_t)(op->p1.y - op->radius * sinf(a))};
            }
            if (n > 0) cur = out[n - 1];
            break;
        }
        case PRIM_OP_CLOSE:
            cur = start;
            if (n < cap) out[n++] = start;
            break;
        }
    }
    return n;
}

void prim_path_fill(prim_path_t *p, prim_color_t color)
{
    if (p == NULL || p->op_count == 0) return;
    prim_point_t pts[PATH_MAX_FLAT];
    int16_t n = flatten(p, pts, PATH_MAX_FLAT);
    prim_internal_fill_polygon(pts, n, color);
}
