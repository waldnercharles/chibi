#include "compare_nodes.h"
/* core/const_nodes.c -- corrected core literal nodes, registered as EXTRA_NODES.
 *
 * CAT-30: the core literals are Bool, Byte, Integer, Integer64, Real, Name,
 * and String. Each is pure and has exactly one typed "Value" data input
 * carrying the authored default and one typed return output. The output is
 * the input, so an unconnected literal evaluates to its authored default.
 */
#include "arena.h"
#include "conv_nodes.h"
#include "fmt_nodes.h"
#include "math.h"
#include "node.h"
#include "pin_conv.h"
#include "registry.h"

enum
{
    GENERIC_ADD,
    GENERIC_SUB,
    GENERIC_MUL,
    GENERIC_DIV,
    GENERIC_MIN,
    GENERIC_MAX,
    GENERIC_MOD
};

static pin_value literal_identity(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    return inputs[0];
}

static pin_value numeric_op(pin_value a, pin_value b, int operation)
{
    pin_value result = a;
    switch (a.type) {
        case PIN_F32:
            result.f = operation == GENERIC_MUL ? a.f * b.f
                     : operation == GENERIC_SUB ? a.f - b.f
                     : operation == GENERIC_DIV ? a.f / b.f
                     : operation == GENERIC_MIN ? (a.f < b.f ? a.f : b.f)
                     : operation == GENERIC_MAX ? (a.f > b.f ? a.f : b.f)
                     : operation == GENERIC_MOD ? fmodf(a.f, b.f)
                                                : a.f + b.f;
            break;
        case PIN_F64:
            result.d = operation == GENERIC_MUL ? a.d * b.d
                     : operation == GENERIC_SUB ? a.d - b.d
                     : operation == GENERIC_DIV ? a.d / b.d
                     : operation == GENERIC_MIN ? (a.d < b.d ? a.d : b.d)
                     : operation == GENERIC_MAX ? (a.d > b.d ? a.d : b.d)
                     : operation == GENERIC_MOD ? fmod(a.d, b.d)
                                                : a.d + b.d;
            break;
        case PIN_I32:
            result.i = operation == GENERIC_MUL ? a.i * b.i
                     : operation == GENERIC_SUB ? a.i - b.i
                     : operation == GENERIC_MIN ? (a.i < b.i ? a.i : b.i)
                     : operation == GENERIC_MAX ? (a.i > b.i ? a.i : b.i)
                     : operation == GENERIC_MOD ? a.i % b.i
                                                : a.i + b.i;
            break;
        case PIN_I64:
            result.i64 = operation == GENERIC_MUL ? a.i64 * b.i64
                       : operation == GENERIC_SUB ? a.i64 - b.i64
                       : operation == GENERIC_MIN ? (a.i64 < b.i64 ? a.i64 : b.i64)
                       : operation == GENERIC_MAX ? (a.i64 > b.i64 ? a.i64 : b.i64)
                       : operation == GENERIC_MOD ? a.i64 % b.i64
                                                  : a.i64 + b.i64;
            break;
        default: result.type = PIN_FLOW; break;
    }
    return result;
}

static pin_value generic_binary(const pin_value *inputs, int input_count, arena *a, int operation)
{
    (void)input_count;
    pin_value left = inputs[0];
    pin_value right = inputs[1];
    if (left.type != PIN_STRUCT) {
        return numeric_op(left, right, operation);
    }
    const type_desc *type = registry_type_by_name(left.type_name);
    if (!type || !left.ptr || (right.type == PIN_STRUCT && !right.ptr)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    void *memory = arena_alloc(a, type->size);
    if (!memory) {
        return (pin_value){ .type = PIN_STRUCT, .type_name = type->name };
    }
    for (int i = 0; i < type->field_count; i++) {
        pin_value l;
        pin_value r = right;
        pin_read_field(left.ptr, &type->fields[i], &l);
        if (right.type == PIN_STRUCT) {
            pin_read_field(right.ptr, &type->fields[i], &r);
        }
        pin_write_field(memory, &type->fields[i], numeric_op(l, r, operation));
    }
    return (pin_value){ .type = PIN_STRUCT, .type_name = type->name, .ptr = memory };
}

static pin_value generic_add(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_ADD);
}

static pin_value generic_multiply(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_MUL);
}

static pin_value generic_subtract(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_SUB);
}

static pin_value generic_divide(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_DIV);
}

static pin_value generic_min(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_MIN);
}

static pin_value generic_max(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_MAX);
}

static pin_value generic_modulo(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_MOD);
}

static pin_value generic_clamp(const pin_value *inputs, int input_count, arena *a)
{
    pin_value value = inputs[0];
    pin_value low = inputs[1];
    pin_value high = inputs[2];
    pin_value bounded = { value.type };
    const pin_value pair[] = { value, high };
    bounded = generic_binary(pair, 2, a, GENERIC_MIN);
    const pin_value result[] = { bounded, low };
    return generic_binary(result, 2, a, GENERIC_MAX);
}

static pin_value generic_negate(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    pin_value result = inputs[0];
    switch (result.type) {
        case PIN_F32: result.f = -result.f; break;
        case PIN_F64: result.d = -result.d; break;
        case PIN_I32: result.i = -result.i; break;
        case PIN_I64: result.i64 = -result.i64; break;
        default: result.type = PIN_FLOW; break;
    }
    return result;
}

static pin_value generic_abs(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    pin_value result = inputs[0];
    switch (result.type) {
        case PIN_F32: result.f = fabsf(result.f); break;
        case PIN_F64: result.d = fabs(result.d); break;
        case PIN_I32: result.i = result.i < 0 ? -result.i : result.i; break;
        case PIN_I64:
            result.i64 = result.i64 < 0 ? -result.i64 : result.i64;
            break;
        default: result.type = PIN_FLOW; break;
    }
    return result;
}

static int generic_add_fast(void *output, const void *const *inputs)
{
    const float *a = inputs[0];
    const float *b = inputs[1];
    float *result = output;
    result[0] = a[0] + b[0];
    result[1] = a[1] + b[1];
    return 0;
}

static int generic_subtract_fast(void *output, const void *const *inputs)
{
    const float *a = inputs[0];
    const float *b = inputs[1];
    float *result = output;
    result[0] = a[0] - b[0];
    result[1] = a[1] - b[1];
    return 0;
}

static int generic_divide_fast(void *output, const void *const *inputs)
{
    const float *a = inputs[0];
    const float *b = inputs[1];
    float *result = output;
    result[0] = a[0] / b[0];
    result[1] = a[1] / b[0];
    return 0;
}

static int generic_multiply_fast(void *output, const void *const *inputs)
{
    const float *value = inputs[0];
    const float *scale = inputs[1];
    float *result = output;
    result[0] = value[0] * scale[0];
    result[1] = value[1] * scale[0];
    return 0;
}

static int generic_min_fast(void *output, const void *const *inputs)
{
    const float *a = inputs[0];
    const float *b = inputs[1];
    float *result = output;
    result[0] = a[0] < b[0] ? a[0] : b[0];
    result[1] = a[1] < b[1] ? a[1] : b[1];
    return 0;
}

static int generic_max_fast(void *output, const void *const *inputs)
{
    const float *a = inputs[0];
    const float *b = inputs[1];
    float *result = output;
    result[0] = a[0] > b[0] ? a[0] : b[0];
    result[1] = a[1] > b[1] ? a[1] : b[1];
    return 0;
}

static int generic_modulo_fast(void *output, const void *const *inputs)
{
    const float *a = inputs[0];
    const float *b = inputs[1];
    float *result = output;
    result[0] = fmodf(a[0], b[0]);
    result[1] = fmodf(a[1], b[1]);
    return 0;
}

static int generic_add_direct(void *output, const void *a, const void *b)
{
    const void *inputs[] = { a, b };
    return generic_add_fast(output, inputs);
}

static int generic_multiply_direct(void *output, const void *value, const void *scale)
{
    const void *inputs[] = { value, scale };
    return generic_multiply_fast(output, inputs);
}

static int generic_subtract_direct(void *output, const void *a, const void *b)
{
    const void *inputs[] = { a, b };
    return generic_subtract_fast(output, inputs);
}

static int generic_divide_direct(void *output, const void *a, const void *b)
{
    const void *inputs[] = { a, b };
    return generic_divide_fast(output, inputs);
}

static int generic_min_direct(void *output, const void *a, const void *b)
{
    const void *inputs[] = { a, b };
    return generic_min_fast(output, inputs);
}

static int generic_max_direct(void *output, const void *a, const void *b)
{
    const void *inputs[] = { a, b };
    return generic_max_fast(output, inputs);
}

static int generic_modulo_direct(void *output, const void *a, const void *b)
{
    const void *inputs[] = { a, b };
    const float *left = inputs[0];
    const float *right = inputs[1];
    float *result = output;
    result[0] = fmodf(left[0], right[0]);
    result[1] = fmodf(left[1], right[1]);
    return 0;
}

static int generic_clamp_direct(void *output, const void *value, const void *low, const void *high)
{
    const float *v = value;
    const float *lo = low;
    const float *hi = high;
    float *result = output;
    result[0] = v[0] < lo[0] ? lo[0] : v[0] > hi[0] ? hi[0] : v[0];
    result[1] = v[1] < lo[1] ? lo[1] : v[1] > hi[1] ? hi[1] : v[1];
    return 0;
}

#define GENERIC_UNARY_DESC(VAR, NAME, FN, KEYWORDS, JIT)                                                  \
    static pin_spec VAR##_inputs[] = {                                                                    \
        { "Value", PIN_F32, "float", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { 0 }, 0, PIN_FLAG_WILDCARD, 1 }  \
    };                                                                                                    \
    static pin_spec VAR##_outputs[] = {                                                                   \
        { "Result", PIN_F32, "float", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { 0 }, 0, PIN_FLAG_WILDCARD, 1 } \
    };                                                                                                    \
    node_desc VAR = { .name = NAME,                                                                       \
                      .input_count = 1,                                                                   \
                      .output_count = 1,                                                                  \
                      .inputs = VAR##_inputs,                                                             \
                      .outputs = VAR##_outputs,                                                           \
                      .pure_kind = NODE_PURE_CALL,                                                        \
                      .jit_kind = JIT,                                                                    \
                      .fn = FN,                                                                           \
                      .display_class = NODE_DISPLAY_PURE,                                                 \
                      .category = "Math",                                                                 \
                      .keywords = KEYWORDS };

static pin_spec NODE_Clamp_inputs[] = {
    { "Value", PIN_F32, "float", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { 0 }, 0, PIN_FLAG_WILDCARD, 1 },
    { "Min", PIN_F32, "float", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { 0 }, 0, PIN_FLAG_WILDCARD, 2 },
    { "Max", PIN_F32, "float", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { 0 }, 0, PIN_FLAG_WILDCARD, 3 },
};
static pin_spec NODE_Clamp_outputs[] = {
    { "Result", PIN_F32, "float", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { 0 }, 0, PIN_FLAG_WILDCARD, 1 },
};
node_desc NODE_Clamp = { .name = "Clamp",
                         .input_count = 3,
                         .output_count = 1,
                         .inputs = NODE_Clamp_inputs,
                         .outputs = NODE_Clamp_outputs,
                         .pure_kind = NODE_PURE_CALL,
                         .jit_kind = NODE_JIT_CLAMP,
                         .jit_flags = NODE_JIT_FLAG_FAST_MATH,
                         .fn = generic_clamp,
                         .direct = { .three = generic_clamp_direct },
                         .direct_count = 4,
                         .display_class = NODE_DISPLAY_PURE,
                         .category = "Math",
                         .keywords = "clamp bound constrain limit" };

#define GENERIC_BINARY_DESC(VAR, NAME, FN, KEYWORDS, JIT, FAST, DIRECT)                                    \
    static pin_spec VAR##_inputs[] = {                                                                     \
        { "A", PIN_F32, "float", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { 0 }, 0, PIN_FLAG_WILDCARD, 1 },      \
        { "B", PIN_F32, "float", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { 0 }, 0, PIN_FLAG_WILDCARD, 2 },      \
    };                                                                                                     \
    static pin_spec VAR##_outputs[] = {                                                                    \
        { "Result", PIN_F32, "float", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { 0 }, 0, PIN_FLAG_WILDCARD, 1 }, \
    };                                                                                                     \
    node_desc VAR = {                                                                                      \
        .name = NAME,                                                                                      \
        .input_count = 2,                                                                                  \
        .output_count = 1,                                                                                 \
        .inputs = VAR##_inputs,                                                                            \
        .outputs = VAR##_outputs,                                                                          \
        .pure_kind = NODE_PURE_CALL,                                                                       \
        .jit_kind = JIT,                                                                                   \
        .jit_flags = NODE_JIT_FLAG_FAST_MATH,                                                              \
        .fn = FN,                                                                                          \
        .fast = FAST,                                                                                      \
        .direct = { .two = DIRECT },                                                                       \
        .direct_count = 3,                                                                                 \
        .display_class = NODE_DISPLAY_PURE,                                                                \
        .category = "Math",                                                                                \
        .keywords = KEYWORDS,                                                                              \
    };

GENERIC_BINARY_DESC(NODE_Add, "Add", generic_add, "add plus vector", NODE_JIT_ADD, generic_add_fast, generic_add_direct)
GENERIC_BINARY_DESC(
    NODE_Multiply,
    "Multiply",
    generic_multiply,
    "multiply mul times vector",
    NODE_JIT_MUL,
    generic_multiply_fast,
    generic_multiply_direct
)
GENERIC_BINARY_DESC(
    NODE_Subtract,
    "Subtract",
    generic_subtract,
    "subtract minus vector",
    NODE_JIT_SUB,
    generic_subtract_fast,
    generic_subtract_direct
)
GENERIC_BINARY_DESC(NODE_Divide, "Divide", generic_divide, "divide over vector", NODE_JIT_DIV, generic_divide_fast, generic_divide_direct)
GENERIC_BINARY_DESC(NODE_Min, "Min", generic_min, "min minimum vector", NODE_JIT_MIN, generic_min_fast, generic_min_direct)
GENERIC_BINARY_DESC(NODE_Max, "Max", generic_max, "max maximum vector", NODE_JIT_MAX, generic_max_fast, generic_max_direct)
GENERIC_BINARY_DESC(
    NODE_Modulo,
    "Modulo",
    generic_modulo,
    "mod modulo remainder",
    NODE_JIT_NONE,
    generic_modulo_fast,
    generic_modulo_direct
)
GENERIC_UNARY_DESC(NODE_Negate, "Negate", generic_negate, "negate negative minus", NODE_JIT_NEG)
GENERIC_UNARY_DESC(NODE_Abs, "Abs", generic_abs, "abs absolute", NODE_JIT_ABS)

#define LITERAL_DESC(VAR, NAME, PTYPE, PNAME)                                  \
    static pin_spec VAR##_inputs[] = {                                         \
        { "Value", PTYPE, PNAME, PIN_DATA },                                   \
    };                                                                         \
    static pin_spec VAR##_outputs[] = {                                        \
        { "Value", PTYPE, PNAME, PIN_DATA },                                   \
    };                                                                         \
    node_desc VAR = {                                                          \
        .name = NAME,                                                          \
        .input_count = 1,                                                      \
        .output_count = 1,                                                     \
        .inputs = VAR##_inputs,                                                \
        .outputs = VAR##_outputs,                                              \
        .pure_kind = NODE_PURE_CALL,                                           \
        .fn = literal_identity,                                                \
        .display_class = NODE_DISPLAY_LITERAL,                                 \
    };

static pin_spec NODE_DeltaTime_outputs[] = {
    { "Seconds", PIN_F32, "float", PIN_DATA },
};
node_desc NODE_DeltaTime = {
    .name = "Delta Time",
    .output_count = 1,
    .outputs = NODE_DeltaTime_outputs,
    .pure_kind = NODE_PURE_DELTA,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Time",
    .keywords = "delta time seconds timestep",
};

LITERAL_DESC(NODE_Bool, "Bool", PIN_BOOL, "bool")
LITERAL_DESC(NODE_Byte, "Byte", PIN_U8, "byte")
LITERAL_DESC(NODE_Integer, "Integer", PIN_I32, "int")
LITERAL_DESC(NODE_Integer64, "Integer64", PIN_I64, "int64")
LITERAL_DESC(NODE_Real, "Real", PIN_F64, "double")
LITERAL_DESC(NODE_Name, "Name", PIN_NAME, "name")
LITERAL_DESC(NODE_String, "String", PIN_STR, "const char *")

node_desc *const EXTRA_NODES[] = {
    &NODE_Greater_Or_Equal,
    &NODE_Integer_Greater_Or_Equal,
    &NODE_Has_Any_Flags,
    &NODE_DeltaTime,
    &NODE_Bool,
    &NODE_Byte,
    &NODE_Integer,
    &NODE_Integer64,
    &NODE_Real,
    &NODE_Name,
    &NODE_String,
    &NODE_To_String,
    &NODE_Add,
    &NODE_Multiply,
    &NODE_Subtract,
    &NODE_Divide,
    &NODE_Min,
    &NODE_Max,
    &NODE_Clamp,
    &NODE_Modulo,
    &NODE_Negate,
    &NODE_Abs,
    &NODE_To_Real,
    &NODE_To_Integer64,
    &NODE_Format_Text,
};
const int EXTRA_NODE_COUNT = sizeof EXTRA_NODES / sizeof EXTRA_NODES[0];

const registry_pack CORE_EXTRA_PACK = {
    .name = "core.extra",
    .nodes = EXTRA_NODES,
    .node_count = sizeof EXTRA_NODES / sizeof EXTRA_NODES[0],
};
