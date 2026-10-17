#ifndef CHIBI_FIELD_NATIVE_H
#define CHIBI_FIELD_NATIVE_H

#include "execution/host.h"

/* Static scalar F32 capabilities: the host call a pure library function makes
 * for each field node. Operations evaluate as field_op_value with seed 0, so
 * outside a field program Seed Offset is the whole seed; a field that inlines
 * the function evaluates them with its own seed. Field X, Y, Z and Parameter
 * read a field program's sample and fail outside one. Field Output belongs to
 * the field. Callers supply typed result headers; these kernels write payloads
 * only and retain no arguments. */
#define FIELD_NATIVE_OPERATIONS(CALL)                                          \
    CALL(ADD)                                                                  \
    CALL(SUBTRACT)                                                             \
    CALL(MULTIPLY)                                                             \
    CALL(DIVIDE)                                                               \
    CALL(MIN)                                                                  \
    CALL(MAX)                                                                  \
    CALL(ABS)                                                                  \
    CALL(NEGATE)                                                               \
    CALL(SIN)                                                                  \
    CALL(COS)                                                                  \
    CALL(FLOOR)                                                                \
    CALL(CLAMP)                                                                \
    CALL(REMAP)                                                                \
    CALL(NOISE)                                                                \
    CALL(FRACTAL)                                                              \
    CALL(HASH)                                                                 \
    CALL(CIRCLE)                                                               \
    CALL(BOX)                                                                  \
    CALL(CAPSULE)                                                              \
    CALL(ROTATE_X)                                                             \
    CALL(ROTATE_Y)                                                             \
    CALL(SMOOTH_UNION)                                                         \
    CALL(SMOOTH_SUBTRACT)                                                      \
    CALL(SELECT)                                                               \
    CALL(CELL_X)                                                               \
    CALL(CELL_Y)                                                               \
    CALL(CELL_EDGE)
#define FIELD_NATIVE_SAMPLES(CALL) CALL(X) CALL(Y) CALL(Z) CALL(PARAMETER)
#define FIELD_NATIVE_CALLS(CALL)                                               \
    CALL(CONSTANT) FIELD_NATIVE_SAMPLES(CALL) FIELD_NATIVE_OPERATIONS(CALL)

extern const node_access FIELD_NATIVE_ACCESS;
#define FIELD_NATIVE_DECLARE(OP) extern const node_host_call FIELD_##OP##_CALL;
FIELD_NATIVE_CALLS(FIELD_NATIVE_DECLARE)
#undef FIELD_NATIVE_DECLARE

#endif
