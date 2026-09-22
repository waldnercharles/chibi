#include "graph/nodes/compare_nodes.h"

#include <assert.h>

static pin_spec real_inputs[] = {
    { .name = "A", .type = PIN_F32, .type_name = "float" },
    { .name = "B", .type = PIN_F32, .type_name = "float" },
};
static pin_spec integer_inputs[] = {
    { .name = "A", .type = PIN_I32, .type_name = "int" },
    { .name = "B", .type = PIN_I32, .type_name = "int" },
};
static pin_spec compare_outputs[] = {
    { .name = "Result", .type = PIN_BOOL, .type_name = "bool" },
};
static const node_access pure = { .flags = NODE_EFFECT_PURE };

#define REAL_COMPARE(NAME, LABEL, OP, KEYWORDS)                                  \
    static pin_value NAME##_value(const pin_value *inputs, int count, arena *)   \
    {                                                                            \
        assert(count == 2);                                                      \
        assert((inputs[0].type == PIN_F32 || inputs[0].type == PIN_F64) &&      \
               (inputs[1].type == PIN_F32 || inputs[1].type == PIN_F64));        \
        assert(inputs[0].container == PIN_CONTAINER_SINGLE);                     \
        assert(inputs[1].container == PIN_CONTAINER_SINGLE);                     \
        double left = inputs[0].type == PIN_F64 ? inputs[0].d : inputs[0].f;     \
        double right = inputs[1].type == PIN_F64 ? inputs[1].d : inputs[1].f;    \
        return (pin_value){ .type = PIN_BOOL, .b = left OP right };              \
    }                                                                            \
    node_desc NODE_##NAME = {                                                    \
        .name = LABEL,                                                           \
        .input_count = 2,                                                        \
        .output_count = 1,                                                       \
        .inputs = real_inputs,                                                   \
        .outputs = compare_outputs,                                              \
        .pure_kind = NODE_PURE_CALL,                                             \
        .fn = NAME##_value,                                                      \
        .display_class = NODE_DISPLAY_PURE,                                      \
        .category = "Math/Comparison",                                           \
        .keywords = KEYWORDS,                                                    \
        .access = &pure,                                                         \
    };

#define INTEGER_COMPARE(NAME, LABEL, OP, KEYWORDS)                               \
    static pin_value NAME##_value(const pin_value *inputs, int count, arena *)   \
    {                                                                            \
        assert(count == 2);                                                      \
        assert(inputs[0].type == PIN_I32 && inputs[1].type == PIN_I32);          \
        assert(inputs[0].container == PIN_CONTAINER_SINGLE);                     \
        assert(inputs[1].container == PIN_CONTAINER_SINGLE);                     \
        return (pin_value){ .type = PIN_BOOL, .b = inputs[0].i OP inputs[1].i }; \
    }                                                                            \
    node_desc NODE_##NAME = {                                                    \
        .name = LABEL,                                                           \
        .input_count = 2,                                                        \
        .output_count = 1,                                                       \
        .inputs = integer_inputs,                                                \
        .outputs = compare_outputs,                                              \
        .pure_kind = NODE_PURE_CALL,                                             \
        .fn = NAME##_value,                                                      \
        .display_class = NODE_DISPLAY_PURE,                                      \
        .category = "Math/Comparison",                                           \
        .keywords = KEYWORDS,                                                    \
        .access = &pure,                                                         \
    };

REAL_COMPARE(Less, "Less", <, "compare less below <")
INTEGER_COMPARE(Integer_Less, "Integer Less", <, "compare less below integer <")
REAL_COMPARE(Less_Or_Equal, "Less Or Equal", <=, "compare less equal at most <= ≤")
INTEGER_COMPARE(Integer_Less_Or_Equal, "Integer Less Or Equal", <=, "compare less equal at most integer <= ≤")
REAL_COMPARE(Greater, "Greater", >, "compare greater above >")
INTEGER_COMPARE(Integer_Greater, "Integer Greater", >, "compare greater above integer >")
REAL_COMPARE(Greater_Or_Equal, "Greater Or Equal", >=, "compare greater equal at least >= ≥")
INTEGER_COMPARE(Integer_Greater_Or_Equal, "Integer Greater Or Equal", >=, "compare greater equal at least integer >= ≥")
REAL_COMPARE(Equal, "Equal", ==, "compare equal equality == =")
INTEGER_COMPARE(Integer_Equal, "Integer Equal", ==, "compare equal equality integer == =")
REAL_COMPARE(Not_Equal, "Not Equal", !=, "compare not equal inequality != ≠")
INTEGER_COMPARE(Integer_Not_Equal, "Integer Not Equal", !=, "compare not equal inequality integer != ≠")

#undef REAL_COMPARE
#undef INTEGER_COMPARE

static pin_value has_any_flags(const pin_value *inputs, int count, arena *)
{
    assert(count == 2);
    assert(inputs[0].type == PIN_U32 && inputs[1].type == PIN_U32);
    assert(inputs[0].container == PIN_CONTAINER_SINGLE && inputs[1].container == PIN_CONTAINER_SINGLE);
    return (pin_value){ .type = PIN_BOOL, .b = (inputs[0].u & inputs[1].u) != 0 };
}

static pin_spec flag_inputs[] = {
    { .name = "Value", .type = PIN_U32, .type_name = "unsigned int" },
    { .name = "Flags", .type = PIN_U32, .type_name = "unsigned int" },
};

node_desc NODE_Has_Any_Flags = {
    .name = "Has Any Flags",
    .input_count = 2,
    .output_count = 1,
    .inputs = flag_inputs,
    .outputs = compare_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = has_any_flags,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Logic/Flags",
    .keywords = "bits mask contains any category",
    .access = &pure,
};

static pin_spec bool_inputs[] = {
    { .name = "A", .type = PIN_BOOL, .type_name = "bool" },
    { .name = "B", .type = PIN_BOOL, .type_name = "bool" },
};
static pin_spec unary_bool_inputs[] = {
    { .name = "Value", .type = PIN_BOOL, .type_name = "bool" },
};
static pin_spec bool_output = { .name = "Result", .type = PIN_BOOL, .type_name = "bool" };

static pin_value and_value(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_BOOL && inputs[1].type == PIN_BOOL);
    return (pin_value){ .type = PIN_BOOL, .b = inputs[0].b && inputs[1].b };
}
static pin_value or_value(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_BOOL && inputs[1].type == PIN_BOOL);
    return (pin_value){ .type = PIN_BOOL, .b = inputs[0].b || inputs[1].b };
}
static pin_value not_value(const pin_value *inputs, int count, arena *)
{
    assert(count == 1 && inputs[0].type == PIN_BOOL);
    return (pin_value){ .type = PIN_BOOL, .b = !inputs[0].b };
}

node_desc NODE_And = {
    .name = "And", .input_count = 2, .output_count = 1, .inputs = bool_inputs,
    .outputs = &bool_output, .pure_kind = NODE_PURE_CALL, .fn = and_value,
    .display_class = NODE_DISPLAY_PURE, .category = "Logic", .keywords = "and &&",
    .access = &pure,
};
node_desc NODE_Or = {
    .name = "Or", .input_count = 2, .output_count = 1, .inputs = bool_inputs,
    .outputs = &bool_output, .pure_kind = NODE_PURE_CALL, .fn = or_value,
    .display_class = NODE_DISPLAY_PURE, .category = "Logic", .keywords = "or ||",
    .access = &pure,
};
node_desc NODE_Not = {
    .name = "Not", .input_count = 1, .output_count = 1, .inputs = unary_bool_inputs,
    .outputs = &bool_output, .pure_kind = NODE_PURE_CALL, .fn = not_value,
    .display_class = NODE_DISPLAY_PURE, .category = "Logic", .keywords = "not !",
    .access = &pure,
};
