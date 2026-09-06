#include "compare_nodes.h"

#include <assert.h>

static pin_value greater_or_equal(const pin_value *inputs, int count, arena *)
{
    assert(count == 2);
    assert(inputs[0].type == PIN_F32 && inputs[1].type == PIN_F32);
    assert(inputs[0].container == PIN_CONTAINER_SINGLE);
    assert(inputs[1].container == PIN_CONTAINER_SINGLE);
    return (pin_value){ .type = PIN_BOOL, .b = inputs[0].f >= inputs[1].f };
}

static pin_spec inputs[] = {
    { .name = "A", .type = PIN_F32, .type_name = "float" },
    { .name = "B", .type = PIN_F32, .type_name = "float" },
};
static pin_spec outputs[] = {
    { .name = "Result", .type = PIN_BOOL, .type_name = "bool" },
};
static const node_access pure = { .flags = NODE_EFFECT_PURE };

node_desc NODE_Greater_Or_Equal = {
    .name = "Greater Or Equal",
    .input_count = 2,
    .output_count = 1,
    .inputs = inputs,
    .outputs = outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = greater_or_equal,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Comparison",
    .keywords = "compare greater equal at least threshold >=",
    .access = &pure,
};

static pin_value integer_greater_or_equal(const pin_value *inputs, int count, arena *)
{
    assert(count == 2);
    assert(inputs[0].type == PIN_I32 && inputs[1].type == PIN_I32);
    assert(inputs[0].container == PIN_CONTAINER_SINGLE);
    assert(inputs[1].container == PIN_CONTAINER_SINGLE);
    return (pin_value){ .type = PIN_BOOL, .b = inputs[0].i >= inputs[1].i };
}

static pin_spec integer_inputs[] = {
    { .name = "A", .type = PIN_I32, .type_name = "int" },
    { .name = "B", .type = PIN_I32, .type_name = "int" },
};

node_desc NODE_Integer_Greater_Or_Equal = {
    .name = "Integer Greater Or Equal",
    .input_count = 2,
    .output_count = 1,
    .inputs = integer_inputs,
    .outputs = outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = integer_greater_or_equal,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Comparison",
    .keywords = "compare greater equal integer threshold >=",
    .access = &pure,
};

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
    .outputs = outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = has_any_flags,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Logic/Flags",
    .keywords = "bits mask contains any category",
    .access = &pure,
};
