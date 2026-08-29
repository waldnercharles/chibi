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
#include "node.h"
#include "pin_conv.h"
#include "registry.h"

enum
{
    GENERIC_ADD,
    GENERIC_SUB,
    GENERIC_MUL,
    GENERIC_DIV
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
                                                : a.f + b.f;
            break;
        case PIN_F64:
            result.d = operation == GENERIC_MUL ? a.d * b.d
                     : operation == GENERIC_SUB ? a.d - b.d
                     : operation == GENERIC_DIV ? a.d / b.d
                                                : a.d + b.d;
            break;
        case PIN_I32:
            result.i = operation == GENERIC_MUL ? a.i * b.i
                     : operation == GENERIC_SUB ? a.i - b.i
                                                : a.i + b.i;
            break;
        case PIN_I64:
            result.i64 = operation == GENERIC_MUL ? a.i64 * b.i64
                       : operation == GENERIC_SUB ? a.i64 - b.i64
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
    if (!type || !left.ptr || !right.ptr) {
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

LITERAL_DESC(NODE_Bool, "Bool", PIN_BOOL, "bool")
LITERAL_DESC(NODE_Byte, "Byte", PIN_U8, "byte")
LITERAL_DESC(NODE_Integer, "Integer", PIN_I32, "int")
LITERAL_DESC(NODE_Integer64, "Integer64", PIN_I64, "int64")
LITERAL_DESC(NODE_Real, "Real", PIN_F64, "double")
LITERAL_DESC(NODE_Name, "Name", PIN_NAME, "name")
LITERAL_DESC(NODE_String, "String", PIN_STR, "const char *")

node_desc *const EXTRA_NODES[] = {
    &NODE_Bool,    &NODE_Byte,         &NODE_Integer,     &NODE_Integer64,
    &NODE_Real,    &NODE_Name,         &NODE_String,      &NODE_To_String,
    &NODE_Add,     &NODE_Multiply,     &NODE_Subtract,    &NODE_Divide,
    &NODE_To_Real, &NODE_To_Integer64, &NODE_Format_Text,
};
const int EXTRA_NODE_COUNT = sizeof EXTRA_NODES / sizeof EXTRA_NODES[0];

const registry_pack CORE_EXTRA_PACK = {
    .name = "core.extra",
    .nodes = EXTRA_NODES,
    .node_count = sizeof EXTRA_NODES / sizeof EXTRA_NODES[0],
};
