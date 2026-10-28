#include "execution/random.h"
#include "execution/scalar.h"
#include "runtime/math_native.h"
#include "support/cos_sin.h"
#include "types/array.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION)                              \
    pin_value native_math_##KEY(const pin_value *inputs, int count, arena *)   \
    {                                                                          \
        assert(count == 1 && inputs[0].type == PIN_F64);                       \
        (void)count;                                                           \
        return (pin_value){ .type = PIN_F64, .d = FN(inputs[0].d) };           \
    }
NATIVE_MATH_UNARIES(UNARY)
#undef UNARY
#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION)                                \
    pin_value native_math_F32_##KEY(const pin_value *inputs, int count, arena *) \
    {                                                                            \
        assert(count == 1 && inputs[0].type == PIN_F32);                         \
        (void)count;                                                             \
        return (pin_value){ .type = PIN_F32, .f = FN##f(inputs[0].f) };          \
    }
NATIVE_MATH_UNARIES(UNARY)
#undef UNARY
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)                  \
    pin_value native_math_##KEY(const pin_value *inputs, int count, arena *)          \
    {                                                                                 \
        assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64); \
        (void)count;                                                                  \
        return (pin_value){ .type = PIN_F64, .d = FN(inputs[0].d, inputs[1].d) };     \
    }
NATIVE_MATH_BINARIES(BINARY)
#undef BINARY
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)                  \
    pin_value native_math_F32_##KEY(const pin_value *inputs, int count, arena *)      \
    {                                                                                 \
        assert(count == 2 && inputs[0].type == PIN_F32 && inputs[1].type == PIN_F32); \
        (void)count;                                                                  \
        return (pin_value){ .type = PIN_F32, .f = FN##f(inputs[0].f, inputs[1].f) };  \
    }
NATIVE_MATH_BINARIES(BINARY)
#undef BINARY

pin_value native_math_RandomReal(const pin_value *, int count, arena *)
{
    assert(count == 0);
    (void)count;
    /* Rounding to f32 can reach 1; keep the result below it. */
    float value = (float)((double)rand() / ((double)RAND_MAX + 1));
    return (pin_value){ .type = PIN_F32, .f = value < 1 ? value : nextafterf(1, 0) };
}

pin_value native_math_RandomRangeReal(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64);
    (void)count;
    double min = inputs[0].d;
    double max = inputs[1].d;
    if (!isfinite(min) || !isfinite(max)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    if (max < min) {
        double swap = min;
        min = max;
        max = swap;
    }
    return (pin_value){
        .type = PIN_F64,
        .d = behavior_random_range(min, max),
    };
}

pin_value native_math_F32_RandomRangeReal(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F32 && inputs[1].type == PIN_F32);
    (void)count;
    float min = inputs[0].f;
    float max = inputs[1].f;
    if (!isfinite(min) || !isfinite(max)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    if (max < min) {
        float swap = min;
        min = max;
        max = swap;
    }
    return (pin_value){ .type = PIN_F32, .f = (float)behavior_random_range(min, max) };
}

#define ADAPTER(KEY)                                                                              \
    static const char *KEY##_call(arena *storage, const pin_value *in, int count, pin_value *out) \
    {                                                                                             \
        pin_value value = native_math_##KEY(in, count, storage);                                  \
        if (value.type == PIN_FLOW) return "Native value operation failed";                       \
        return pin_value_snapshot_to_arena(storage, out, &value)                                  \
                 ? "Cannot retain native value result"                                            \
                 : NULL;                                                                          \
    }                                                                                             \
    const node_host_call NATIVE_MATH_##KEY##_CALL = { .value = KEY##_call };
#define UNARY(KEY, NAME, SOURCE, FN, DESCRIPTION)                              \
    ADAPTER(KEY) ADAPTER(F32_##KEY)
#define BINARY(KEY, NAME, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)           \
    ADAPTER(KEY) ADAPTER(F32_##KEY)
#define VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM) ADAPTER(KEY)
NATIVE_MATH_UNARIES(UNARY)
NATIVE_MATH_BINARIES(BINARY)
NATIVE_MATH_VALUES(VALUE)
#define FLOAT_VALUE(KEY, NAME, SOURCE, INPUTS, RANDOM) ADAPTER(F32_##KEY)
NATIVE_MATH_F32_VALUES(FLOAT_VALUE)
ADAPTER(RandomReal)
#undef FLOAT_VALUE
#undef UNARY
#undef BINARY
#undef VALUE
#undef ADAPTER
const node_access NATIVE_MATH_ACCESS = { .flags = NODE_EFFECT_PURE };
const node_access NATIVE_MATH_RANDOM_ACCESS = { .flags = NODE_EFFECT_HOST_SINK |
                                                         NODE_EFFECT_RANDOM };
