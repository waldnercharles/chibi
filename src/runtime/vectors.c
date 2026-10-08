#include "runtime/vectors.h"

#include "support/arena.h"
#include "support/cos_sin.h"
#include "types/array.h"

#include <assert.h>
#include <cute_math.h>
#include <math.h>
#include <stdlib.h>

const CF_V3 NATIVE_VECTOR_MIN = { 0, 0, 0 }, NATIVE_VECTOR_MAX = { 1, 1, 1 };

pin_value native_normalize(const pin_value *inputs, int count, arena *memory)
{
    assert(count == 1 && inputs[0].type == PIN_STRUCT && inputs[0].ptr);
    (void)count;
    CF_V2 value = *(const CF_V2 *)inputs[0].ptr;
    CF_V2 *result = arena_alloc(memory, sizeof *result);
    *result = (CF_V2){ 0 };
    // Double precision preserves direction for very large and subnormal floats.
    double length = hypot((double)value.x, (double)value.y);
    if (length > 0 && isfinite(length)) {
        result->x = (float)(value.x / length);
        result->y = (float)(value.y / length);
    }
    return (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = result };
}

static CF_V2 v2_input(const pin_value *value)
{
    assert(value->type == PIN_STRUCT && value->ptr);
    return *(const CF_V2 *)value->ptr;
}

pin_value native_vector_length(const pin_value *inputs, int count, arena *)
{
    assert(count == 1);
    (void)count;
    CF_V2 v = v2_input(&inputs[0]);
    return (pin_value){ .type = PIN_F64, .d = hypot((double)v.x, (double)v.y) };
}

pin_value native_vector_dot(const pin_value *inputs, int count, arena *)
{
    assert(count == 2);
    (void)count;
    CF_V2 a = v2_input(&inputs[0]), b = v2_input(&inputs[1]);
    /* Separate steps, as every backend computes it, never a fused multiply-add. */
    double x = (double)a.x * b.x;
    double y = (double)a.y * b.y;
    return (pin_value){ .type = PIN_F64, .d = x + y };
}

pin_value native_vector_distance(const pin_value *inputs, int count, arena *)
{
    assert(count == 2);
    (void)count;
    CF_V2 a = v2_input(&inputs[0]), b = v2_input(&inputs[1]);
    return (pin_value){ .type = PIN_F64,
                        .d = hypot((double)a.x - b.x, (double)a.y - b.y) };
}

pin_value native_direction_to(const pin_value *inputs, int count, arena *memory)
{
    assert(count == 2 && inputs[0].type == PIN_STRUCT && inputs[0].ptr);
    (void)count;
    assert(inputs[1].type == PIN_STRUCT && inputs[1].ptr);
    CF_V2 origin = *(const CF_V2 *)inputs[0].ptr;
    CF_V2 target = *(const CF_V2 *)inputs[1].ptr;
    CF_V2 delta = { target.x - origin.x, target.y - origin.y };
    pin_value value = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &delta };
    return native_normalize(&value, 1, memory);
}

pin_value native_direction_from_angle(const pin_value *inputs, int count, arena *memory)
{
    assert(count == 1 && inputs[0].type == PIN_F64);
    (void)count;
    CF_V2 *result = arena_alloc(memory, sizeof *result);
    cos_sin pair = cos_sin_compute((float)inputs[0].d);
    *result = (CF_V2){ pair.c, pair.s };
    return (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = result };
}

pin_value native_random_range_vector(const pin_value *inputs, int count, arena *memory)
{
    assert(
        count == 2 && inputs[0].type == PIN_STRUCT && inputs[0].ptr &&
        inputs[1].type == PIN_STRUCT && inputs[1].ptr
    );
    (void)count;
    CF_V3 min = *(const CF_V3 *)inputs[0].ptr;
    CF_V3 max = *(const CF_V3 *)inputs[1].ptr;
    if (!isfinite(min.x) || !isfinite(min.y) || !isfinite(min.z) ||
        !isfinite(max.x) || !isfinite(max.y) || !isfinite(max.z)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    if (max.x < min.x) {
        float swap = min.x;
        min.x = max.x;
        max.x = swap;
    }
    if (max.y < min.y) {
        float swap = min.y;
        min.y = max.y;
        max.y = swap;
    }
    if (max.z < min.z) {
        float swap = min.z;
        min.z = max.z;
        max.z = swap;
    }
    double x = (double)rand() / ((double)RAND_MAX + 1);
    double y = (double)rand() / ((double)RAND_MAX + 1);
    double z = (double)rand() / ((double)RAND_MAX + 1);
    CF_V3 *result = arena_alloc(memory, sizeof *result);
    *result = (CF_V3){
        (float)fma((double)min.x, 1 - x, (double)max.x * x),
        (float)fma((double)min.y, 1 - y, (double)max.y * y),
        (float)fma((double)min.z, 1 - z, (double)max.z * z),
    };
    return (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = result };
}

#define CALL(KERNEL, NAME, SOURCE, METHOD, INPUTS, OUTPUTS, RANDOM)                                            \
    static const char *KERNEL##_call(arena *storage, const pin_value *arguments, int count, pin_value *result) \
    {                                                                                                          \
        pin_value value = native_##KERNEL(arguments, count, storage);                                          \
        if (value.type == PIN_FLOW ||                                                                          \
            (value.type == PIN_STRUCT &&                                                                       \
             value.container == PIN_CONTAINER_SINGLE && !value.ptr)) {                                         \
            return "Native value operation failed";                                                            \
        }                                                                                                      \
        return pin_value_snapshot_to_arena(storage, result, &value)                                            \
                 ? "Cannot retain native value result"                                                         \
                 : NULL;                                                                                       \
    }                                                                                                          \
    const node_host_call NATIVE_##KERNEL##_CALL = { .value = KERNEL##_call };
NATIVE_VECTOR_OPERATIONS(CALL)
#undef CALL
const node_access NATIVE_VECTOR_ACCESS = { .flags = NODE_EFFECT_PURE };
const node_access NATIVE_VECTOR_RANDOM_ACCESS = { .flags = NODE_EFFECT_HOST_SINK |
                                                           NODE_EFFECT_RANDOM };
