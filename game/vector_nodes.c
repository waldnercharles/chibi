#include "vector_nodes.h"

#include "../core/arena.h"
#include "components.h"

#include <assert.h>
#include <math.h>

static pin_value normalize(const pin_value *inputs, int count, arena *memory)
{
    assert(count == 1 && inputs[0].type == PIN_STRUCT && inputs[0].ptr);
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

static pin_spec inputs[] = {
    { .name = "Value", .type = PIN_STRUCT, .type_name = "CF_V2" },
};
static pin_spec outputs[] = {
    { .name = "Result", .type = PIN_STRUCT, .type_name = "CF_V2" },
};
static const node_access pure = { .flags = NODE_EFFECT_PURE };

node_desc NODE_Normalize_Vector_2 = {
    .name = "Normalize Vector 2",
    .input_count = 1,
    .output_count = 1,
    .inputs = inputs,
    .outputs = outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = normalize,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "normalize direction unit length vector",
    .access = &pure,
};

static pin_value direction_to(const pin_value *inputs, int count, arena *memory)
{
    assert(count == 2 && inputs[0].type == PIN_STRUCT && inputs[0].ptr);
    assert(inputs[1].type == PIN_STRUCT && inputs[1].ptr);
    CF_V2 origin = *(const CF_V2 *)inputs[0].ptr;
    CF_V2 target = *(const CF_V2 *)inputs[1].ptr;
    CF_V2 delta = { target.x - origin.x, target.y - origin.y };
    pin_value value = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &delta };
    return normalize(&value, 1, memory);
}

static pin_spec direction_inputs[] = {
    { .name = "Origin", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Target", .type = PIN_STRUCT, .type_name = "CF_V2" },
};
static pin_spec direction_outputs[] = {
    { .name = "Direction", .type = PIN_STRUCT, .type_name = "CF_V2" },
};

node_desc NODE_Direction_To = {
    .name = "Direction To",
    .input_count = 2,
    .output_count = 1,
    .inputs = direction_inputs,
    .outputs = direction_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = direction_to,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "direction target origin normalize unit vector",
    .access = &pure,
};

static pin_value atan2_value(const pin_value *inputs, int count, arena *)
{
    assert(count == 2 && inputs[0].type == PIN_F32 && inputs[1].type == PIN_F32);
    return (pin_value){ .type = PIN_F32, .f = atan2f(inputs[0].f, inputs[1].f) };
}

static pin_spec angle_inputs[] = {
    { .name = "Y", .type = PIN_F32 },
    { .name = "X", .type = PIN_F32 },
};
static pin_spec angle_outputs[] = { { .name = "Radians", .type = PIN_F32 } };

node_desc NODE_Atan2 = {
    .name = "Atan2",
    .input_count = 2,
    .output_count = 1,
    .inputs = angle_inputs,
    .outputs = angle_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = atan2_value,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "angle direction rotation radians",
    .access = &pure,
};
