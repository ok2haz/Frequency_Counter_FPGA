#pragma once
/**
 * @file path_impl.h
 * @brief Private definition of prim_path_t.
 */

#include <prim/types.h>
#include <prim/path.h>

typedef enum {
    PRIM_OP_MOVE,
    PRIM_OP_LINE,
    PRIM_OP_QUAD,
    PRIM_OP_ARC,
    PRIM_OP_CLOSE,
} prim_path_op_kind_t;

typedef struct {
    prim_path_op_kind_t kind;
    prim_point_t        p0;     /**< target / line end / quad end */
    prim_point_t        p1;     /**< quad control / arc center */
    int16_t             radius; /**< arc */
    int16_t             a0;     /**< arc start deg */
    int16_t             a1;     /**< arc sweep deg */
} prim_path_op_t;

struct prim_path {
    prim_path_op_t *ops;
    int16_t         op_count;
    int16_t         op_capacity;
    prim_point_t    current_point;
    prim_point_t    start_point;
    bool            is_closed;
};
