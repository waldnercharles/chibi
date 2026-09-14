#include "graph/nodes/vector_nodes.h"

#include "support/arena.h"

#include <assert.h>
#include <cute_math.h>
#include <cute_math3d.h>
#include <math.h>
#include <stdlib.h>

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
static const node_access random_access = { .flags = NODE_EFFECT_HOST_SINK };

node_desc NODE_Normalize_Vector_2 = {
    .name = "Normalize Vector 2",
    .input_count = 1,
    .output_count = 1,
    .inputs = inputs,
    .outputs = outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = normalize,
    .jit_kind = NODE_JIT_V2_NORMALIZE,
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
    .jit_kind = NODE_JIT_V2_DIRECTION,
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
    .jit_kind = NODE_JIT_ATAN2_F32,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "angle direction rotation radians",
    .access = &pure,
};

static CF_V3 random_range_vector_min = { 0, 0, 0 };
static CF_V3 random_range_vector_max = { 1, 1, 1 };

static pin_spec random_range_vector_inputs[] = {
    { .name = "Min",
      .type = PIN_STRUCT,
      .type_name = "CF_V3",
      .default_value = { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &random_range_vector_min } },
    { .name = "Max",
      .type = PIN_STRUCT,
      .type_name = "CF_V3",
      .default_value = { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &random_range_vector_max } },
};
static pin_spec random_range_vector_outputs[] = {
    { .name = "Result", .type = PIN_STRUCT, .type_name = "CF_V3" },
};

static pin_value random_range_vector(const pin_value *inputs, int count, arena *memory)
{
    assert(
        count == 2 && inputs[0].type == PIN_STRUCT && inputs[0].ptr &&
        inputs[1].type == PIN_STRUCT && inputs[1].ptr
    );
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

node_desc NODE_Random_Range_Vector = {
    .name = "Random Range Vector",
    .jit_kind = NODE_JIT_RANDOM_VECTOR,
    .input_count = 2,
    .output_count = 1,
    .inputs = random_range_vector_inputs,
    .outputs = random_range_vector_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = random_range_vector,
    .category = "Math/Random",
    .keywords = "rand random range vector min max sample",
    .access = &random_access,
};
