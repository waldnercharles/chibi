#include "graph/nodes/vector_nodes.h"

#include "graph/model/native_definition.h"
#include "runtime/vectors.h"

static pin_spec inputs[] = { NATIVE_VECTOR_VALUE_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec outputs[] = { NATIVE_VECTOR_VECTOR_OUTPUTS(GRAPH_NATIVE_PORT) };

node_desc NODE_Normalize_Vector_2 = {
    .name = NATIVE_NORMALIZE_NAME,
    .source_name = NATIVE_NORMALIZE_SOURCE,
    .method_name = NATIVE_NORMALIZE_METHOD,
    .input_count = 1,
    .output_count = 1,
    .inputs = inputs,
    .outputs = outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_normalize,
    .execution_call = &NATIVE_normalize_CALL,
    .jit_kind = NODE_JIT_V2_NORMALIZE,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "normalize direction unit length vector",
    .description = "Scales Value to length 1, or returns zero when its length "
                   "is zero or not finite.",
    .access = &NATIVE_VECTOR_ACCESS,
};

static pin_spec pair_inputs[] = { NATIVE_VECTOR_PAIR_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec scalar_outputs[] = { NATIVE_VECTOR_SCALAR_OUTPUTS(GRAPH_NATIVE_PORT) };

node_desc NODE_Vector_Length = {
    .name = NATIVE_VECTOR_LENGTH_NAME,
    .input_count = 1,
    .output_count = 1,
    .inputs = inputs,
    .outputs = scalar_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_vector_length,
    .execution_call = &NATIVE_vector_length_CALL,
    .jit_kind = NODE_JIT_V2_LENGTH,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "length magnitude size speed vector",
    .description = "Returns the magnitude of Value, its distance from the "
                   "origin.",
    .access = &NATIVE_VECTOR_ACCESS,
};

node_desc NODE_Vector_Dot = {
    .name = NATIVE_VECTOR_DOT_NAME,
    .input_count = 2,
    .output_count = 1,
    .inputs = pair_inputs,
    .outputs = scalar_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_vector_dot,
    .execution_call = &NATIVE_vector_dot_CALL,
    .jit_kind = NODE_JIT_V2_DOT,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "dot product projection facing vector",
    .description = "Returns A.x * B.x + A.y * B.y, positive when A and B point "
                   "the same way.",
    .access = &NATIVE_VECTOR_ACCESS,
};

node_desc NODE_Vector_Distance = {
    .name = NATIVE_VECTOR_DISTANCE_NAME,
    .input_count = 2,
    .output_count = 1,
    .inputs = pair_inputs,
    .outputs = scalar_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_vector_distance,
    .execution_call = &NATIVE_vector_distance_CALL,
    .jit_kind = NODE_JIT_V2_DISTANCE,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "distance between points range vector",
    .description = "Returns the straight-line distance between points A and B.",
    .access = &NATIVE_VECTOR_ACCESS,
};

static pin_spec direction_inputs[] = { NATIVE_VECTOR_DIRECTION_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec direction_outputs[] = { NATIVE_VECTOR_DIRECTION_OUTPUTS(GRAPH_NATIVE_PORT) };

node_desc NODE_Direction_To = {
    .name = NATIVE_DIRECTION_TO_NAME,
    .source_name = NATIVE_DIRECTION_TO_SOURCE,
    .input_count = 2,
    .output_count = 1,
    .inputs = direction_inputs,
    .outputs = direction_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_direction_to,
    .execution_call = &NATIVE_direction_to_CALL,
    .jit_kind = NODE_JIT_V2_DIRECTION,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "direction target origin normalize unit vector",
    .description = "Returns the unit vector from Origin toward Target, or zero "
                   "when they coincide.",
    .access = &NATIVE_VECTOR_ACCESS,
};

static pin_spec angle_inputs[] = { NATIVE_VECTOR_ANGLE_INPUTS(GRAPH_NATIVE_PORT) };

/* The unit vector at an angle in radians (Godot's Vector2.from_angle). */
node_desc NODE_Direction_From_Angle = {
    .name = NATIVE_DIRECTION_FROM_ANGLE_NAME,
    .input_count = 1,
    .output_count = 1,
    .inputs = angle_inputs,
    .outputs = direction_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_direction_from_angle,
    .execution_call = &NATIVE_direction_from_angle_CALL,
    .jit_kind = NODE_JIT_V2_FROM_ANGLE,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Math/Vector",
    .keywords = "angle heading radians polar cos sin unit vector",
    .description = "Returns the unit vector (cos Angle, sin Angle) for Angle "
                   "in radians.",
    .access = &NATIVE_VECTOR_ACCESS,
};

static pin_spec random_range_vector_inputs[] = {
    NATIVE_VECTOR_RANDOM_INPUTS(GRAPH_NATIVE_PORT)
};
static pin_spec random_range_vector_outputs[] = {
    NATIVE_VECTOR_RANDOM_OUTPUTS(GRAPH_NATIVE_PORT)
};

node_desc NODE_Random_Range_Vector = {
    .name = NATIVE_RANDOM_RANGE_VECTOR_NAME,
    .jit_kind = NODE_JIT_RANDOM_VECTOR,
    .input_count = 2,
    .output_count = 1,
    .inputs = random_range_vector_inputs,
    .outputs = random_range_vector_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_random_range_vector,
    .execution_call = &NATIVE_random_range_vector_CALL,
    .category = "Math/Random",
    .keywords = "rand random range vector min max sample",
    .description = "Picks each component at random between Min and Max, which "
                   "default to 0 and 1.",
    .access = &NATIVE_VECTOR_RANDOM_ACCESS,
};
