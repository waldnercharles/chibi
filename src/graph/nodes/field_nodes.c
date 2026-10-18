#include "graph/nodes/field_nodes.h"

#include "graph/model/native_definition.h"
#include "runtime/fields/definition.h"
#include "runtime/fields/ir.h"
#include "runtime/fields/native.h"

static const node_access FIELD_ACCESS = { .flags = NODE_EFFECT_PURE };
static pin_spec VALUE_OUT[] = { FIELD_VALUE_OUT_PORTS(GRAPH_NATIVE_PORT) };
static pin_spec IN_CONSTANT[] = { FIELD_IN_CONSTANT_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_CONSTANT = {
    .name = FIELD_NODE_CONSTANT_NAME,
    .category = "Procedural Field",
    .description = "Returns Value at every sample.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_CONSTANT_CALL,
    .input_count = 1,
    .inputs = IN_CONSTANT,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static node_desc NODE_X = { .name = FIELD_NODE_X_NAME,
                            .category = "Procedural Field",
                            .description = "Returns the X coordinate at the "
                                           "center of the sampled cell.",
                            .access = &FIELD_ACCESS,
                            .host_call = &FIELD_X_CALL,
                            .input_count = 0,
                            .output_count = 1,
                            .outputs = VALUE_OUT };
static node_desc NODE_Y = { .name = FIELD_NODE_Y_NAME,
                            .category = "Procedural Field",
                            .description = "Returns the Y coordinate at the "
                                           "center of the sampled cell.",
                            .access = &FIELD_ACCESS,
                            .host_call = &FIELD_Y_CALL,
                            .input_count = 0,
                            .output_count = 1,
                            .outputs = VALUE_OUT };
static node_desc NODE_Z = { .name = FIELD_NODE_Z_NAME,
                            .category = "Procedural Field",
                            .description = "Returns the depth behind the play "
                                           "plane, positive away from the "
                                           "camera; 2D bakes sample at zero.",
                            .access = &FIELD_ACCESS,
                            .host_call = &FIELD_Z_CALL,
                            .input_count = 0,
                            .output_count = 1,
                            .outputs = VALUE_OUT };
static pin_spec IN_PARAMETER[] = { FIELD_IN_PARAMETER_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_PARAMETER = {
    .name = FIELD_NODE_PARAMETER_NAME,
    .category = "Procedural Field",
    .description = "Returns the named field parameter; Name must be a literal "
                   "naming a declared parameter.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_PARAMETER_CALL,
    .input_count = 1,
    .inputs = IN_PARAMETER,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_ADD[] = { FIELD_IN_ADD_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_ADD = { .name = FIELD_NODE_ADD_NAME,
                              .category = "Procedural Field",
                              .description = "Returns A plus B.",
                              .access = &FIELD_ACCESS,
                              .host_call = &FIELD_ADD_CALL,
                              .input_count = 2,
                              .inputs = IN_ADD,
                              .output_count = 1,
                              .outputs = VALUE_OUT };
static pin_spec IN_SUBTRACT[] = { FIELD_IN_SUBTRACT_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_SUBTRACT = { .name = FIELD_NODE_SUBTRACT_NAME,
                                   .category = "Procedural Field",
                                   .description = "Returns A minus B.",
                                   .access = &FIELD_ACCESS,
                                   .host_call = &FIELD_SUBTRACT_CALL,
                                   .input_count = 2,
                                   .inputs = IN_SUBTRACT,
                                   .output_count = 1,
                                   .outputs = VALUE_OUT };
static pin_spec IN_MULTIPLY[] = { FIELD_IN_MULTIPLY_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_MULTIPLY = { .name = FIELD_NODE_MULTIPLY_NAME,
                                   .category = "Procedural Field",
                                   .description = "Returns A times B.",
                                   .access = &FIELD_ACCESS,
                                   .host_call = &FIELD_MULTIPLY_CALL,
                                   .input_count = 2,
                                   .inputs = IN_MULTIPLY,
                                   .output_count = 1,
                                   .outputs = VALUE_OUT };
static pin_spec IN_DIVIDE[] = { FIELD_IN_DIVIDE_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_DIVIDE = {
    .name = FIELD_NODE_DIVIDE_NAME,
    .category = "Procedural Field",
    .description = "Returns A divided by B; a B of 0 fails evaluation.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_DIVIDE_CALL,
    .input_count = 2,
    .inputs = IN_DIVIDE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_MIN[] = { FIELD_IN_MIN_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_MIN = {
    .name = FIELD_NODE_MIN_NAME,
    .category = "Procedural Field",
    .description = "Returns the smaller of A and B; on distances, the union of "
                   "two shapes.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_MIN_CALL,
    .input_count = 2,
    .inputs = IN_MIN,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_MAX[] = { FIELD_IN_MAX_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_MAX = {
    .name = FIELD_NODE_MAX_NAME,
    .category = "Procedural Field",
    .description = "Returns the larger of A and B; on distances, the "
                   "intersection of two shapes.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_MAX_CALL,
    .input_count = 2,
    .inputs = IN_MAX,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_ABS[] = { FIELD_IN_ABS_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_ABS = {
    .name = FIELD_NODE_ABS_NAME,
    .category = "Procedural Field",
    .description = "Returns the absolute value of Value.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_ABS_CALL,
    .input_count = 1,
    .inputs = IN_ABS,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_NEGATE[] = { FIELD_IN_NEGATE_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_NEGATE = {
    .name = FIELD_NODE_NEGATE_NAME,
    .category = "Procedural Field",
    .description = "Returns Value with its sign flipped; on a distance, swaps "
                   "inside and outside.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_NEGATE_CALL,
    .input_count = 1,
    .inputs = IN_NEGATE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_SIN[] = { FIELD_IN_SIN_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_SIN = {
    .name = FIELD_NODE_SIN_NAME,
    .category = "Procedural Field",
    .description = "Returns the sine of Value, in radians.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_SIN_CALL,
    .input_count = 1,
    .inputs = IN_SIN,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_COS[] = { FIELD_IN_COS_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_COS = {
    .name = FIELD_NODE_COS_NAME,
    .category = "Procedural Field",
    .description = "Returns the cosine of Value, in radians.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_COS_CALL,
    .input_count = 1,
    .inputs = IN_COS,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_FLOOR[] = { FIELD_IN_FLOOR_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_FLOOR = {
    .name = FIELD_NODE_FLOOR_NAME,
    .category = "Procedural Field",
    .description = "Rounds Value down to the nearest whole number.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_FLOOR_CALL,
    .input_count = 1,
    .inputs = IN_FLOOR,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_CLAMP[] = { FIELD_IN_CLAMP_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_CLAMP = {
    .name = FIELD_NODE_CLAMP_NAME,
    .category = "Procedural Field",
    .description = "Limits Value to between Min and Max; Min above Max fails "
                   "evaluation.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_CLAMP_CALL,
    .input_count = 3,
    .inputs = IN_CLAMP,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_REMAP[] = { FIELD_IN_REMAP_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_REMAP = {
    .name = FIELD_NODE_REMAP_NAME,
    .category = "Procedural Field",
    .description = "Maps Value linearly from [In Min, In Max] to [Out Min, Out "
                   "Max] without clamping; equal In Min and In Max fail.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_REMAP_CALL,
    .input_count = 5,
    .inputs = IN_REMAP,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_NOISE[] = { FIELD_IN_NOISE_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_NOISE = {
    .name = FIELD_NODE_NOISE_NAME,
    .category = "Procedural Field",
    .description = "Returns smooth value noise from -1 to 1 at (X, Y), one "
                   "lattice cell per unit; Seed Offset shifts the seed.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_NOISE_CALL,
    .input_count = 3,
    .inputs = IN_NOISE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_FRACTAL[] = { FIELD_IN_FRACTAL_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_FRACTAL = {
    .name = FIELD_NODE_FRACTAL_NAME,
    .category = "Procedural Field",
    .description = "Averages Octaves (1-16) noise layers, multiplying "
                   "coordinates by Lacunarity and weight by Gain (0-1) per "
                   "layer; -1 to 1.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_FRACTAL_CALL,
    .input_count = 6,
    .inputs = IN_FRACTAL,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_HASH[] = { FIELD_IN_HASH_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_HASH = {
    .name = FIELD_NODE_HASH_NAME,
    .category = "Procedural Field",
    .description = "Returns a random value from -1 to 1, constant across each "
                   "unit cell of (X, Y); Seed Offset shifts the seed.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_HASH_CALL,
    .input_count = 3,
    .inputs = IN_HASH,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_CIRCLE[] = { FIELD_IN_CIRCLE_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_CIRCLE = {
    .name = FIELD_NODE_CIRCLE_NAME,
    .category = "Procedural Field",
    .description = "Returns the signed distance from (X, Y) to a circle of "
                   "Radius at the origin; negative inside.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_CIRCLE_CALL,
    .input_count = 3,
    .inputs = IN_CIRCLE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_BOX[] = { FIELD_IN_BOX_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_BOX = {
    .name = FIELD_NODE_BOX_NAME,
    .category = "Procedural Field",
    .description = "Returns the signed distance from (X, Y) to an "
                   "origin-centered box of Half Width by Half Height; negative "
                   "inside.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_BOX_CALL,
    .input_count = 4,
    .inputs = IN_BOX,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_CAPSULE[] = { FIELD_IN_CAPSULE_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_CAPSULE = {
    .name = FIELD_NODE_CAPSULE_NAME,
    .category = "Procedural Field",
    .description = "Returns the signed distance from (X, Y) to the segment "
                   "from (AX, AY) to (BX, BY) widened by Radius; negative "
                   "inside.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_CAPSULE_CALL,
    .input_count = 7,
    .inputs = IN_CAPSULE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_ROTATE_X[] = { FIELD_IN_ROTATE_X_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_ROTATE_X = {
    .name = FIELD_NODE_ROTATE_X_NAME,
    .category = "Procedural Field",
    .description = "Returns X of the point (X, Y) rotated by -Angle radians; "
                   "shapes sampled there turn by Angle.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_ROTATE_X_CALL,
    .input_count = 3,
    .inputs = IN_ROTATE_X,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_ROTATE_Y[] = { FIELD_IN_ROTATE_Y_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_ROTATE_Y = {
    .name = FIELD_NODE_ROTATE_Y_NAME,
    .category = "Procedural Field",
    .description = "Returns Y of the point (X, Y) rotated by -Angle radians; "
                   "shapes sampled there turn by Angle.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_ROTATE_Y_CALL,
    .input_count = 3,
    .inputs = IN_ROTATE_Y,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_SMOOTH_UNION[] = { FIELD_IN_SMOOTH_UNION_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_SMOOTH_UNION = {
    .name = FIELD_NODE_SMOOTH_UNION_NAME,
    .category = "Procedural Field",
    .description = "Merges distances A and B with a rounded blend Radius wide; "
                   "Radius 0 gives a sharp union.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_SMOOTH_UNION_CALL,
    .input_count = 3,
    .inputs = IN_SMOOTH_UNION,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_SMOOTH_SUBTRACT[] = { FIELD_IN_SMOOTH_SUBTRACT_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_SMOOTH_SUBTRACT = {
    .name = FIELD_NODE_SMOOTH_SUBTRACT_NAME,
    .category = "Procedural Field",
    .description = "Cuts distance B out of A with a rounded blend Radius wide; "
                   "Radius 0 gives a sharp cut.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_SMOOTH_SUBTRACT_CALL,
    .input_count = 3,
    .inputs = IN_SMOOTH_SUBTRACT,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_SELECT[] = { FIELD_IN_SELECT_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_SELECT = {
    .name = FIELD_NODE_SELECT_NAME,
    .category = "Procedural Field",
    .description = "Returns Positive when Condition is above 0, otherwise "
                   "Negative.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_SELECT_CALL,
    .input_count = 3,
    .inputs = IN_SELECT,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_OUTPUT[] = { FIELD_IN_OUTPUT_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_OUTPUT = {
    .name = FIELD_NODE_OUTPUT_NAME,
    .category = "Procedural Field",
    .description = "Marks the field's final value, where negative is solid; "
                   "each field needs exactly one.",
    .access = &FIELD_ACCESS,
    .input_count = 1,
    .inputs = IN_OUTPUT,
    .output_count = 1,
    .outputs = VALUE_OUT
};
/* Cells are the Voronoi diagram of sites one unit apart on a triangular
 * lattice, each moved at most Jitter (0 to 0.25): hexagons at zero. */
static pin_spec IN_CELL[] = { FIELD_IN_CELL_PORTS(GRAPH_NATIVE_PORT) };
static node_desc NODE_CELL_X = {
    .name = FIELD_NODE_CELL_X_NAME,
    .category = "Procedural Field",
    .description = "Returns the X of the site of the cell holding (X, Y), on a "
                   "triangular lattice of unit spacing whose sites move up to "
                   "Jitter (0-0.25); Seed Offset shifts the seed.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_CELL_X_CALL,
    .input_count = 4,
    .inputs = IN_CELL,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static node_desc NODE_CELL_Y = {
    .name = FIELD_NODE_CELL_Y_NAME,
    .category = "Procedural Field",
    .description = "Returns the Y of the site of the cell holding (X, Y), on a "
                   "triangular lattice of unit spacing whose sites move up to "
                   "Jitter (0-0.25); Seed Offset shifts the seed.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_CELL_Y_CALL,
    .input_count = 4,
    .inputs = IN_CELL,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static node_desc NODE_CELL_EDGE = {
    .name = FIELD_NODE_CELL_EDGE_NAME,
    .category = "Procedural Field",
    .description = "Returns the distance from (X, Y) to the boundary of its "
                   "cell, on a triangular lattice of unit spacing whose sites "
                   "move up to Jitter (0-0.25); Seed Offset shifts the seed.",
    .access = &FIELD_ACCESS,
    .host_call = &FIELD_CELL_EDGE_CALL,
    .input_count = 4,
    .inputs = IN_CELL,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static node_desc *const FIELD_NODES[] = { &NODE_CONSTANT,
                                          &NODE_X,
                                          &NODE_Y,
                                          &NODE_PARAMETER,
                                          &NODE_ADD,
                                          &NODE_SUBTRACT,
                                          &NODE_MULTIPLY,
                                          &NODE_DIVIDE,
                                          &NODE_MIN,
                                          &NODE_MAX,
                                          &NODE_ABS,
                                          &NODE_NEGATE,
                                          &NODE_SIN,
                                          &NODE_COS,
                                          &NODE_FLOOR,
                                          &NODE_CLAMP,
                                          &NODE_REMAP,
                                          &NODE_NOISE,
                                          &NODE_FRACTAL,
                                          &NODE_HASH,
                                          &NODE_CIRCLE,
                                          &NODE_BOX,
                                          &NODE_CAPSULE,
                                          &NODE_ROTATE_X,
                                          &NODE_ROTATE_Y,
                                          &NODE_SMOOTH_UNION,
                                          &NODE_SMOOTH_SUBTRACT,
                                          &NODE_SELECT,
                                          &NODE_OUTPUT,
                                          &NODE_Z,
                                          &NODE_CELL_X,
                                          &NODE_CELL_Y,
                                          &NODE_CELL_EDGE };
const registry_pack FIELD_NODE_PACK = { .name = "core.fields",
                                        .nodes = FIELD_NODES,
                                        .node_count = FIELD_OP_COUNT };

int field_node_opcode(const node_desc *desc)
{
    for (int i = 0; i < FIELD_OP_COUNT; i++) {
        if (desc == FIELD_NODES[i]) {
            return i;
        }
    }
    return -1;
}
