#include "graph/nodes/compare_nodes.h"
/* graph/nodes/const_nodes.c -- corrected core literal nodes, registered as EXTRA_NODES.
 *
 * CAT-30: the core literals are Bool, Byte, Integer, Integer64, Real, Name,
 * and String. Each is pure and has exactly one typed "Value" data input
 * carrying the authored default and one typed return output. The output is
 * the input, so an unconnected literal evaluates to its authored default.
 */
#include "graph/model/node.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/fmt_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "support/arena.h"
#include "support/math.h"

#include <float.h>
#include <limits.h>
#include <string.h>
static const node_access pure = { .flags = NODE_EFFECT_PURE };

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
    if (a.type == PIN_F32 && b.type == PIN_F64) {
        bool subnormal = (a.f != 0.0f && fabsf(a.f) < FLT_MIN) ||
                         (b.d != 0.0 && fabs(b.d) < FLT_MIN);
        if (subnormal) {
            a.type = PIN_F64;
            a.d = (double)a.f;
        } else {
            b.type = PIN_F32;
            b.f = (float)b.d;
        }
    } else if (a.type == PIN_F64 && b.type == PIN_F32) {
        b.type = PIN_F64;
        b.d = (double)b.f;
    }
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
            if ((operation == GENERIC_DIV || operation == GENERIC_MOD) &&
                (b.i == 0 || (a.i == INT_MIN && b.i == -1))) {
                return (pin_value){ .type = PIN_FLOW };
            }
            result.i = operation == GENERIC_MUL
                         ? (int32_t)((uint32_t)a.i * (uint32_t)b.i)
                     : operation == GENERIC_SUB
                         ? (int32_t)((uint32_t)a.i - (uint32_t)b.i)
                     : operation == GENERIC_DIV ? a.i / b.i
                     : operation == GENERIC_MIN ? (a.i < b.i ? a.i : b.i)
                     : operation == GENERIC_MAX ? (a.i > b.i ? a.i : b.i)
                     : operation == GENERIC_MOD
                         ? a.i % b.i
                         : (int32_t)((uint32_t)a.i + (uint32_t)b.i);
            break;
        case PIN_I64:
            if ((operation == GENERIC_DIV || operation == GENERIC_MOD) &&
                (b.i64 == 0 || (a.i64 == INT64_MIN && b.i64 == -1))) {
                return (pin_value){ .type = PIN_FLOW };
            }
            result.i64 = operation == GENERIC_MUL
                           ? (int64_t)((uint64_t)a.i64 * (uint64_t)b.i64)
                       : operation == GENERIC_SUB
                           ? (int64_t)((uint64_t)a.i64 - (uint64_t)b.i64)
                       : operation == GENERIC_DIV ? a.i64 / b.i64
                       : operation == GENERIC_MIN ? (a.i64 < b.i64 ? a.i64 : b.i64)
                       : operation == GENERIC_MAX ? (a.i64 > b.i64 ? a.i64 : b.i64)
                       : operation == GENERIC_MOD
                           ? a.i64 % b.i64
                           : (int64_t)((uint64_t)a.i64 + (uint64_t)b.i64);
            break;
        default: result.type = PIN_FLOW; break;
    }
    return result;
}

static const type_field *find_field(const type_desc *type, const char *name)
{
    for (int i = 0; i < type->field_count; i++) {
        if (!strcmp(type->fields[i].name, name)) {
            return &type->fields[i];
        }
    }
    return NULL;
}

static pin_value struct_binary(pin_value aggregate, pin_value other, int operation, arena *a, int aggregate_left)
{
    const type_desc *type = registry_type_by_name(aggregate.type_name);
    const type_desc *other_type = other.type == PIN_STRUCT
                                    ? registry_type_by_name(other.type_name)
                                    : NULL;
    if (!type || !type->field_count || !aggregate.ptr ||
        (other.type == PIN_STRUCT && !other_type) ||
        (other.type == PIN_STRUCT && !other.ptr)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    void *memory = arena_alloc(a, type->size);
    if (!memory) {
        return (pin_value){ .type = PIN_STRUCT, .type_name = type->name };
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        pin_value left;
        pin_value right;
        pin_read_field(aggregate.ptr, field, aggregate_left ? &left : &right);
        if (other.type == PIN_STRUCT) {
            const type_field *peer = find_field(other_type, field->name);
            if (!peer) {
                return (pin_value){ .type = PIN_FLOW };
            }
            pin_read_field(other.ptr, peer, aggregate_left ? &right : &left);
        } else if (aggregate_left) {
            right = other;
        } else {
            left = other;
        }
        pin_value result = numeric_op(left, right, operation);
        if (result.type == PIN_FLOW) {
            return result;
        }
        if (field->type == PIN_F32 && result.type == PIN_F64) {
            result.type = PIN_F32;
            result.f = (float)result.d;
        }
        pin_write_field(memory, field, result);
    }
    return (pin_value){ .type = PIN_STRUCT, .type_name = type->name, .ptr = memory };
}

static pin_value generic_binary(const pin_value *inputs, int input_count, arena *a, int operation)
{
    (void)input_count;
    pin_value left = inputs[0];
    pin_value right = inputs[1];
    if (left.type != PIN_STRUCT && right.type != PIN_STRUCT) {
        return numeric_op(left, right, operation);
    }
    return left.type == PIN_STRUCT ? struct_binary(left, right, operation, a, 1)
                                   : struct_binary(right, left, operation, a, 0);
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
        case PIN_I32:
            result.i = (int32_t)((uint32_t)0 - (uint32_t)result.i);
            break;
        case PIN_I64:
            result.i64 = (int64_t)((uint64_t)0 - (uint64_t)result.i64);
            break;
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
        case PIN_I32:
            result.i = result.i < 0 ? (int32_t)((uint32_t)0 - (uint32_t)result.i)
                                    : result.i;
            break;
        case PIN_I64:
            result.i64 = result.i64 < 0
                           ? (int64_t)((uint64_t)0 - (uint64_t)result.i64)
                           : result.i64;
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

#define GENERIC_UNARY_DESC(VAR, NAME, FN, KEYWORDS, DESCRIPTION, JIT)                                     \
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
                      .access = &pure,                                                                    \
                      .pure_kind = NODE_PURE_CALL,                                                        \
                      .jit_kind = JIT,                                                                    \
                      .fn = FN,                                                                           \
                      .display_class = NODE_DISPLAY_PURE,                                                 \
                      .category = "Math",                                                                 \
                      .keywords = KEYWORDS,                                                               \
                      .description = DESCRIPTION };

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
                         .access = &pure,
                         .pure_kind = NODE_PURE_CALL,
                         .jit_kind = NODE_JIT_CLAMP,
                         .jit_flags = NODE_JIT_FLAG_FAST_MATH,
                         .fn = generic_clamp,
                         .direct = { .three = generic_clamp_direct },
                         .direct_count = 4,
                         .display_class = NODE_DISPLAY_PURE,
                         .category = "Math",
                         .keywords = "clamp bound constrain limit",
                         .description = "Limits Value to the range from Min to "
                                        "Max, per component for vectors." };

#define GENERIC_BINARY_DESC(VAR, NAME, FN, KEYWORDS, DESCRIPTION, JIT, FAST, DIRECT)                       \
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
        .access = &pure,                                                                                   \
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
        .description = DESCRIPTION,                                                                        \
    };

GENERIC_BINARY_DESC(
    NODE_Add,
    "Add",
    generic_add,
    "add plus vector",
    "Sums A and B as reals, integers or vectors; a scalar applies to every "
    "vector component.",
    NODE_JIT_ADD,
    generic_add_fast,
    generic_add_direct
)
GENERIC_BINARY_DESC(
    NODE_Multiply,
    "Multiply",
    generic_multiply,
    "multiply mul times vector",
    "Multiplies A by B as reals, integers or vectors; a scalar scales every "
    "vector component.",
    NODE_JIT_MUL,
    generic_multiply_fast,
    generic_multiply_direct
)
GENERIC_BINARY_DESC(
    NODE_Subtract,
    "Subtract",
    generic_subtract,
    "subtract minus vector",
    "Takes B from A as reals, integers or vectors; a scalar applies to every "
    "vector component.",
    NODE_JIT_SUB,
    generic_subtract_fast,
    generic_subtract_direct
)
GENERIC_BINARY_DESC(
    NODE_Divide,
    "Divide",
    generic_divide,
    "divide over vector",
    "Divides A by B as reals, integers or vectors; integers truncate toward "
    "zero and fail when B is 0.",
    NODE_JIT_DIV,
    generic_divide_fast,
    generic_divide_direct
)
GENERIC_BINARY_DESC(
    NODE_Min,
    "Min",
    generic_min,
    "min minimum vector",
    "Returns the smaller of A and B, per component for vectors.",
    NODE_JIT_MIN,
    generic_min_fast,
    generic_min_direct
)
GENERIC_BINARY_DESC(
    NODE_Max,
    "Max",
    generic_max,
    "max maximum vector",
    "Returns the larger of A and B, per component for vectors.",
    NODE_JIT_MAX,
    generic_max_fast,
    generic_max_direct
)
GENERIC_BINARY_DESC(
    NODE_Modulo,
    "Modulo",
    generic_modulo,
    "mod modulo remainder",
    "Returns the remainder of A divided by B, taking the sign of A; fails when "
    "an integer B is 0.",
    NODE_JIT_NONE,
    generic_modulo_fast,
    generic_modulo_direct
)
GENERIC_UNARY_DESC(
    NODE_Negate,
    "Negate",
    generic_negate,
    "negate negative minus",
    "Flips the sign of a real or integer Value.",
    NODE_JIT_NEG
)
GENERIC_UNARY_DESC(NODE_Abs, "Abs", generic_abs, "abs absolute", "Returns the absolute value of a real or integer Value.", NODE_JIT_ABS)

#define LITERAL_DESC(VAR, NAME, PTYPE, PNAME, DESCRIPTION)                     \
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
        .access = &pure,                                                       \
        .pure_kind = NODE_PURE_CALL,                                           \
        .fn = literal_identity,                                                \
        .display_class = NODE_DISPLAY_LITERAL,                                 \
        .description = DESCRIPTION,                                            \
    };

static pin_spec NODE_DeltaTime_outputs[] = {
    { "Seconds", PIN_F32, "float", PIN_DATA },
};
node_desc NODE_DeltaTime = {
    .name = "Delta Time",
    .output_count = 1,
    .outputs = NODE_DeltaTime_outputs,
    .access = &pure,
    .pure_kind = NODE_PURE_DELTA,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Time",
    .keywords = "delta time seconds timestep",
    .description = "Returns the current update's time step in seconds.",
};

LITERAL_DESC(NODE_Bool, "Bool", PIN_BOOL, "bool", "Returns Value as a constant true or false.")
LITERAL_DESC(NODE_Byte, "Byte", PIN_U8, "byte", "Returns Value as a constant whole number from 0 to 255.")
LITERAL_DESC(NODE_Integer, "Integer", PIN_I32, "int", "Returns Value as a constant 32-bit signed whole number.")
LITERAL_DESC(NODE_Integer64, "Integer64", PIN_I64, "int64", "Returns Value as a constant 64-bit signed whole number.")
LITERAL_DESC(NODE_Real, "Real", PIN_F64, "double", "Returns Value as a constant double-precision number.")
LITERAL_DESC(NODE_Name, "Name", PIN_NAME, "name", "Returns Value as a constant name.")
LITERAL_DESC(NODE_String, "String", PIN_STR, "const char *", "Returns Value as a constant text string.")

static node_desc *const EXTRA_NODES[] = {
#define MATH_NODE_ENTRY(name) &NODE_##name,
    MATH_EXPRESSION_NODES(MATH_NODE_ENTRY)
#undef MATH_NODE_ENTRY
#define MATH_NODE_ENTRY(name) &NODE_##name,
            MATH_OPERATION_NODES(MATH_NODE_ENTRY)
#undef MATH_NODE_ENTRY
        & NODE_Greater_Or_Equal,
    &NODE_Integer_Greater_Or_Equal,
    &NODE_Less,
    &NODE_Integer_Less,
    &NODE_Less_Or_Equal,
    &NODE_Integer_Less_Or_Equal,
    &NODE_Greater,
    &NODE_Integer_Greater,
    &NODE_Equal,
    &NODE_Integer_Equal,
    &NODE_Not_Equal,
    &NODE_Integer_Not_Equal,
    &NODE_Has_Any_Flags,
    &NODE_And,
    &NODE_Or,
    &NODE_Not,
    &NODE_DeltaTime,
    &NODE_Bool,
    &NODE_Byte,
    &NODE_Integer,
    &NODE_Integer64,
    &NODE_Real,
    &NODE_Name,
    &NODE_String,
    &NODE_To_String,
    &NODE_To_Name,
    &NODE_Splat_CF_V2,
    &NODE_Splat_CF_V3,
    &NODE_Splat_ShaderVec2,
    &NODE_Splat_ShaderVec3,
    &NODE_Splat_ShaderVec4,
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
    &NODE_To_Byte,
    &NODE_To_Integer,
    &NODE_Format_Text,
};

const registry_pack CORE_EXTRA_PACK = {
    .name = "core.extra",
    .nodes = EXTRA_NODES,
    .node_count = sizeof EXTRA_NODES / sizeof EXTRA_NODES[0],
};
