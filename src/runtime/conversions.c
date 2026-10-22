#include "runtime/conversions.h"

#include "support/arena.h"
#include "types/array.h"
#include "types/text.h"
#include "types/value_format.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
pin_value native_conversion_To_String(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    pin_value result;
    bool textual = inputs[0].container == PIN_CONTAINER_SINGLE &&
                   (inputs[0].type == PIN_STR || inputs[0].type == PIN_NAME);
    if (textual) {
        pin_string text = pin_text(inputs[0]);
        return pin_value_text(a, &result, text.bytes, text.length)
                 ? (pin_value){ .type = PIN_FLOW }
                 : result;
    }
    const char *text = pin_value_to_string(&inputs[0]);
    return pin_value_text(a, &result, text, strlen(text))
             ? (pin_value){ .type = PIN_FLOW }
             : result;
}

pin_value native_conversion_To_Real(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    pin_value r = { .type = PIN_F64, .type_name = "double", .d = 0.0 };
    switch (inputs[0].type) {
        case PIN_BOOL: r.d = inputs[0].b ? 1.0 : 0.0; break;
        case PIN_U8: r.d = (double)inputs[0].u8; break;
        case PIN_I32: r.d = (double)inputs[0].i; break;
        case PIN_I64: r.d = (double)inputs[0].i64; break;
        default: break; /* input is retyped to one of these before use */
    }
    return r;
}

pin_value native_conversion_To_Name(const pin_value *inputs, int count, arena *storage)
{
    assert(count == 1 && inputs[0].type == PIN_STR);
    (void)count;
    const char *text;
    if (pin_text_cstring(storage, inputs[0], &text, NULL)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, text, size);
    return (pin_value){ .type = PIN_NAME, .type_name = "name", .str = copy };
}

/* A bool reads as 1 or 0. */
static double real_value(const pin_value *value)
{
    assert(value->type == PIN_BOOL || value->type == PIN_F32 || value->type == PIN_F64);
    return value->type == PIN_BOOL ? value->b
         : value->type == PIN_F32  ? value->f
                                   : value->d;
}

/* Test bounds before casting: converting an out-of-range real is undefined.
 * In particular, INT64_MAX rounds up to 2^63 when represented as double. */
static int64_t real_integer(double value, int64_t minimum, int64_t maximum)
{
    if (isnan(value)) {
        return 0;
    }
    if (value <= (double)minimum) {
        return minimum;
    }
    if (value >= (double)maximum) {
        return maximum;
    }
    return (int64_t)value;
}

pin_value native_conversion_To_Byte(const pin_value *inputs, int, arena *)
{
    return (pin_value){ .type = PIN_U8,
                        .type_name = "byte",
                        .u8 = (uint8_t)real_integer(real_value(inputs), 0, UINT8_MAX) };
}

pin_value native_conversion_To_Integer(const pin_value *inputs, int, arena *)
{
    return (pin_value){ .type = PIN_I32,
                        .type_name = "int",
                        .i = (int32_t)real_integer(real_value(inputs), INT32_MIN, INT32_MAX) };
}

pin_value native_conversion_To_Integer64(const pin_value *inputs, int, arena *)
{
    int64_t value = inputs->type == PIN_I32
                      ? inputs->i
                      : real_integer(real_value(inputs), INT64_MIN, INT64_MAX);
    return (pin_value){ .type = PIN_I64, .type_name = "int64", .i64 = value };
}

static pin_value vector_splat(const pin_value *input, arena *storage, const char *type, int width)
{
    float value = 0;
    switch (input->type) {
        case PIN_F32: value = input->f; break;
        case PIN_F64: value = (float)input->d; break;
        case PIN_U8: value = (float)input->u8; break;
        case PIN_I32: value = (float)input->i; break;
        case PIN_I64: value = (float)input->i64; break;
        default: assert(false); break;
    }
    float *result = arena_alloc(storage, (size_t)width * sizeof *result);
    for (int i = 0; i < width; i++) {
        result[i] = value;
    }
    return (pin_value){ .type = PIN_STRUCT, .type_name = type, .ptr = result };
}

#define VECTOR(TYPE, WIDTH, NAME, SOURCE, DESCRIPTION)                                          \
    pin_value native_conversion_Splat_##TYPE(const pin_value *input, int count, arena *storage) \
    {                                                                                           \
        assert(count == 1);                                                                     \
        (void)count;                                                                            \
        return vector_splat(input, storage, #TYPE, WIDTH);                                      \
    }
NATIVE_CONVERSION_VECTORS(VECTOR)
#undef VECTOR
#define ADAPTER(KEY)                                                                                        \
    static const char *KEY##_call(arena *storage, const pin_value *arguments, int count, pin_value *result) \
    {                                                                                                       \
        pin_value value = native_conversion_##KEY(arguments, count, storage);                               \
        if (value.type == PIN_FLOW ||                                                                       \
            (value.type == PIN_STRUCT &&                                                                    \
             value.container == PIN_CONTAINER_SINGLE && !value.ptr))                                        \
            return "Native value operation failed";                                                         \
        return pin_value_snapshot_to_arena(storage, result, &value)                                         \
                 ? "Cannot retain native value result"                                                      \
                 : NULL;                                                                                    \
    }                                                                                                       \
    const node_host_call NATIVE_CONVERSION_##KEY##_CALL = { .value = KEY##_call };
#define SCALAR(KEY, NAME, SOURCE, INPUT, INPUT_NAME, RESULT, OUTPUT, OUTPUT_NAME) \
    ADAPTER(KEY)
#define VECTOR(TYPE, WIDTH, NAME, SOURCE, DESCRIPTION) ADAPTER(Splat_##TYPE)
NATIVE_CONVERSION_SCALARS(SCALAR)
NATIVE_CONVERSION_VECTORS(VECTOR)
const node_access NATIVE_CONVERSION_ACCESS = { .flags = NODE_EFFECT_PURE };
