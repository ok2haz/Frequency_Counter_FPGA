#pragma once
/**
 * @file path_impl.h
 * @brief Private definition of prim_path_t.
 */

#include <prim/types.h>
#include <prim/path.h>

/* ⚠️ QUAD (kvadraticka bezier) a ARC operace byly odstraneny 2026-08-13 spolu
 * s `prim_path_quad_to()` / `prim_path_arc()`, ktere je jako jedine vyrabely a
 * ktere nikdy nikdo nezavolal. Zustaly by jinak jako nedosazitelne vetve UVNITR
 * zive `flatten()`, kde je `--gc-sections` odstranit NEUMI (na rozdil od celych
 * nepouzitych funkci) — tedy skutecne mrtve misto ve flash.
 * Oblouky v UI kresli `prim_draw_arc()` (shapes.c), ktereho se to netyka.
 * Vypadlo tim i pole `p1` (bylo jen ridici bod krivky / stred oblouku), takze
 * operace se zmensila z 20 na 8 B — cesty se alokuji na halde, takze je to
 * primo uspora RAM na kazdou cestu. */
typedef enum {
    PRIM_OP_MOVE,
    PRIM_OP_LINE,
    PRIM_OP_CLOSE,
} prim_path_op_kind_t;

typedef struct {
    prim_path_op_kind_t kind;
    prim_point_t        p0;     /**< target / line end */
} prim_path_op_t;

struct prim_path {
    prim_path_op_t *ops;
    int16_t         op_count;
    int16_t         op_capacity;
    prim_point_t    current_point;
    prim_point_t    start_point;
    bool            is_closed;
};
