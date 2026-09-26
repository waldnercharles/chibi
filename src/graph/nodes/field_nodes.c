#include "graph/nodes/field_nodes.h"

#include "graph/compiler/field_ir.h"

static const node_access FIELD_ACCESS = { .flags = NODE_EFFECT_PURE };
static pin_spec VALUE_OUT[] = { { .name = "Value", .type = PIN_F32 } };
static pin_spec IN_CONSTANT[] = { { .name = "Value", .type = PIN_F32 } };
static node_desc NODE_CONSTANT = {
    .name = "Field Constant",
    .category = "Procedural Field",
    .description = "Returns Value at every sample.",
    .access = &FIELD_ACCESS,
    .input_count = 1,
    .inputs = IN_CONSTANT,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static node_desc NODE_X = { .name = "Field X",
                            .category = "Procedural Field",
                            .description = "Returns the X coordinate at the "
                                           "center of the sampled cell.",
                            .access = &FIELD_ACCESS,
                            .input_count = 0,
                            .output_count = 1,
                            .outputs = VALUE_OUT };
static node_desc NODE_Y = { .name = "Field Y",
                            .category = "Procedural Field",
                            .description = "Returns the Y coordinate at the "
                                           "center of the sampled cell.",
                            .access = &FIELD_ACCESS,
                            .input_count = 0,
                            .output_count = 1,
                            .outputs = VALUE_OUT };
static pin_spec IN_PARAMETER[] = { { .name = "Name", .type = PIN_STR } };
static node_desc NODE_PARAMETER = {
    .name = "Field Parameter",
    .category = "Procedural Field",
    .description = "Returns the named field parameter; Name must be a literal "
                   "naming a declared parameter.",
    .access = &FIELD_ACCESS,
    .input_count = 1,
    .inputs = IN_PARAMETER,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_ADD[] = { { .name = "A", .type = PIN_F32 },
                             { .name = "B", .type = PIN_F32 } };
static node_desc NODE_ADD = { .name = "Field Add",
                              .category = "Procedural Field",
                              .description = "Returns A plus B.",
                              .access = &FIELD_ACCESS,
                              .input_count = 2,
                              .inputs = IN_ADD,
                              .output_count = 1,
                              .outputs = VALUE_OUT };
static pin_spec IN_SUBTRACT[] = { { .name = "A", .type = PIN_F32 },
                                  { .name = "B", .type = PIN_F32 } };
static node_desc NODE_SUBTRACT = { .name = "Field Subtract",
                                   .category = "Procedural Field",
                                   .description = "Returns A minus B.",
                                   .access = &FIELD_ACCESS,
                                   .input_count = 2,
                                   .inputs = IN_SUBTRACT,
                                   .output_count = 1,
                                   .outputs = VALUE_OUT };
static pin_spec IN_MULTIPLY[] = { { .name = "A", .type = PIN_F32 },
                                  { .name = "B", .type = PIN_F32 } };
static node_desc NODE_MULTIPLY = { .name = "Field Multiply",
                                   .category = "Procedural Field",
                                   .description = "Returns A times B.",
                                   .access = &FIELD_ACCESS,
                                   .input_count = 2,
                                   .inputs = IN_MULTIPLY,
                                   .output_count = 1,
                                   .outputs = VALUE_OUT };
static pin_spec IN_DIVIDE[] = { { .name = "A", .type = PIN_F32 },
                                { .name = "B", .type = PIN_F32 } };
static node_desc NODE_DIVIDE = {
    .name = "Field Divide",
    .category = "Procedural Field",
    .description = "Returns A divided by B; a B of 0 fails evaluation.",
    .access = &FIELD_ACCESS,
    .input_count = 2,
    .inputs = IN_DIVIDE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_MIN[] = { { .name = "A", .type = PIN_F32 },
                             { .name = "B", .type = PIN_F32 } };
static node_desc NODE_MIN = {
    .name = "Field Min",
    .category = "Procedural Field",
    .description = "Returns the smaller of A and B; on distances, the union of "
                   "two shapes.",
    .access = &FIELD_ACCESS,
    .input_count = 2,
    .inputs = IN_MIN,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_MAX[] = { { .name = "A", .type = PIN_F32 },
                             { .name = "B", .type = PIN_F32 } };
static node_desc NODE_MAX = {
    .name = "Field Max",
    .category = "Procedural Field",
    .description = "Returns the larger of A and B; on distances, the "
                   "intersection of two shapes.",
    .access = &FIELD_ACCESS,
    .input_count = 2,
    .inputs = IN_MAX,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_ABS[] = { { .name = "Value", .type = PIN_F32 } };
static node_desc NODE_ABS = {
    .name = "Field Abs",
    .category = "Procedural Field",
    .description = "Returns the absolute value of Value.",
    .access = &FIELD_ACCESS,
    .input_count = 1,
    .inputs = IN_ABS,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_NEGATE[] = { { .name = "Value", .type = PIN_F32 } };
static node_desc NODE_NEGATE = {
    .name = "Field Negate",
    .category = "Procedural Field",
    .description = "Returns Value with its sign flipped; on a distance, swaps "
                   "inside and outside.",
    .access = &FIELD_ACCESS,
    .input_count = 1,
    .inputs = IN_NEGATE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_SIN[] = { { .name = "Value", .type = PIN_F32 } };
static node_desc NODE_SIN = {
    .name = "Field Sin",
    .category = "Procedural Field",
    .description = "Returns the sine of Value, in radians.",
    .access = &FIELD_ACCESS,
    .input_count = 1,
    .inputs = IN_SIN,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_COS[] = { { .name = "Value", .type = PIN_F32 } };
static node_desc NODE_COS = {
    .name = "Field Cos",
    .category = "Procedural Field",
    .description = "Returns the cosine of Value, in radians.",
    .access = &FIELD_ACCESS,
    .input_count = 1,
    .inputs = IN_COS,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_FLOOR[] = { { .name = "Value", .type = PIN_F32 } };
static node_desc NODE_FLOOR = {
    .name = "Field Floor",
    .category = "Procedural Field",
    .description = "Rounds Value down to the nearest whole number.",
    .access = &FIELD_ACCESS,
    .input_count = 1,
    .inputs = IN_FLOOR,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_CLAMP[] = { { .name = "Value", .type = PIN_F32 },
                               { .name = "Min", .type = PIN_F32 },
                               { .name = "Max", .type = PIN_F32 } };
static node_desc NODE_CLAMP = {
    .name = "Field Clamp",
    .category = "Procedural Field",
    .description = "Limits Value to between Min and Max; Min above Max fails "
                   "evaluation.",
    .access = &FIELD_ACCESS,
    .input_count = 3,
    .inputs = IN_CLAMP,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_REMAP[] = { { .name = "Value", .type = PIN_F32 },
                               { .name = "In Min", .type = PIN_F32 },
                               { .name = "In Max", .type = PIN_F32 },
                               { .name = "Out Min", .type = PIN_F32 },
                               { .name = "Out Max", .type = PIN_F32 } };
static node_desc NODE_REMAP = {
    .name = "Field Remap",
    .category = "Procedural Field",
    .description = "Maps Value linearly from [In Min, In Max] to [Out Min, Out "
                   "Max] without clamping; equal In Min and In Max fail.",
    .access = &FIELD_ACCESS,
    .input_count = 5,
    .inputs = IN_REMAP,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_NOISE[] = { { .name = "X", .type = PIN_F32 },
                               { .name = "Y", .type = PIN_F32 },
                               { .name = "Seed Offset", .type = PIN_F32 } };
static node_desc NODE_NOISE = {
    .name = "Field Noise",
    .category = "Procedural Field",
    .description = "Returns smooth value noise from -1 to 1 at (X, Y), one "
                   "lattice cell per unit; Seed Offset shifts the seed.",
    .access = &FIELD_ACCESS,
    .input_count = 3,
    .inputs = IN_NOISE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_FRACTAL[] = { { .name = "X", .type = PIN_F32 },
                                 { .name = "Y", .type = PIN_F32 },
                                 { .name = "Seed Offset", .type = PIN_F32 },
                                 { .name = "Octaves", .type = PIN_F32 },
                                 { .name = "Lacunarity", .type = PIN_F32 },
                                 { .name = "Gain", .type = PIN_F32 } };
static node_desc NODE_FRACTAL = {
    .name = "Field Fractal",
    .category = "Procedural Field",
    .description = "Averages Octaves (1-16) noise layers, multiplying "
                   "coordinates by Lacunarity and weight by Gain (0-1) per "
                   "layer; -1 to 1.",
    .access = &FIELD_ACCESS,
    .input_count = 6,
    .inputs = IN_FRACTAL,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_HASH[] = { { .name = "X", .type = PIN_F32 },
                              { .name = "Y", .type = PIN_F32 },
                              { .name = "Seed Offset", .type = PIN_F32 } };
static node_desc NODE_HASH = {
    .name = "Field Hash",
    .category = "Procedural Field",
    .description = "Returns a random value from -1 to 1, constant across each "
                   "unit cell of (X, Y); Seed Offset shifts the seed.",
    .access = &FIELD_ACCESS,
    .input_count = 3,
    .inputs = IN_HASH,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_CIRCLE[] = { { .name = "X", .type = PIN_F32 },
                                { .name = "Y", .type = PIN_F32 },
                                { .name = "Radius", .type = PIN_F32 } };
static node_desc NODE_CIRCLE = {
    .name = "Field Circle",
    .category = "Procedural Field",
    .description = "Returns the signed distance from (X, Y) to a circle of "
                   "Radius at the origin; negative inside.",
    .access = &FIELD_ACCESS,
    .input_count = 3,
    .inputs = IN_CIRCLE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_BOX[] = { { .name = "X", .type = PIN_F32 },
                             { .name = "Y", .type = PIN_F32 },
                             { .name = "Half Width", .type = PIN_F32 },
                             { .name = "Half Height", .type = PIN_F32 } };
static node_desc NODE_BOX = {
    .name = "Field Box",
    .category = "Procedural Field",
    .description = "Returns the signed distance from (X, Y) to an "
                   "origin-centered box of Half Width by Half Height; negative "
                   "inside.",
    .access = &FIELD_ACCESS,
    .input_count = 4,
    .inputs = IN_BOX,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_CAPSULE[] = {
    { .name = "X", .type = PIN_F32 },     { .name = "Y", .type = PIN_F32 },
    { .name = "AX", .type = PIN_F32 },    { .name = "AY", .type = PIN_F32 },
    { .name = "BX", .type = PIN_F32 },    { .name = "BY", .type = PIN_F32 },
    { .name = "Radius", .type = PIN_F32 }
};
static node_desc NODE_CAPSULE = {
    .name = "Field Capsule",
    .category = "Procedural Field",
    .description = "Returns the signed distance from (X, Y) to the segment "
                   "from (AX, AY) to (BX, BY) widened by Radius; negative "
                   "inside.",
    .access = &FIELD_ACCESS,
    .input_count = 7,
    .inputs = IN_CAPSULE,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_ROTATE_X[] = { { .name = "X", .type = PIN_F32 },
                                  { .name = "Y", .type = PIN_F32 },
                                  { .name = "Angle", .type = PIN_F32 } };
static node_desc NODE_ROTATE_X = {
    .name = "Field Rotate X",
    .category = "Procedural Field",
    .description = "Returns X of the point (X, Y) rotated by -Angle radians; "
                   "shapes sampled there turn by Angle.",
    .access = &FIELD_ACCESS,
    .input_count = 3,
    .inputs = IN_ROTATE_X,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_ROTATE_Y[] = { { .name = "X", .type = PIN_F32 },
                                  { .name = "Y", .type = PIN_F32 },
                                  { .name = "Angle", .type = PIN_F32 } };
static node_desc NODE_ROTATE_Y = {
    .name = "Field Rotate Y",
    .category = "Procedural Field",
    .description = "Returns Y of the point (X, Y) rotated by -Angle radians; "
                   "shapes sampled there turn by Angle.",
    .access = &FIELD_ACCESS,
    .input_count = 3,
    .inputs = IN_ROTATE_Y,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_SMOOTH_UNION[] = { { .name = "A", .type = PIN_F32 },
                                      { .name = "B", .type = PIN_F32 },
                                      { .name = "Radius", .type = PIN_F32 } };
static node_desc NODE_SMOOTH_UNION = {
    .name = "Field Smooth Union",
    .category = "Procedural Field",
    .description = "Merges distances A and B with a rounded blend Radius wide; "
                   "Radius 0 gives a sharp union.",
    .access = &FIELD_ACCESS,
    .input_count = 3,
    .inputs = IN_SMOOTH_UNION,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_SMOOTH_SUBTRACT[] = { { .name = "A", .type = PIN_F32 },
                                         { .name = "B", .type = PIN_F32 },
                                         { .name = "Radius", .type = PIN_F32 } };
static node_desc NODE_SMOOTH_SUBTRACT = {
    .name = "Field Smooth Subtract",
    .category = "Procedural Field",
    .description = "Cuts distance B out of A with a rounded blend Radius wide; "
                   "Radius 0 gives a sharp cut.",
    .access = &FIELD_ACCESS,
    .input_count = 3,
    .inputs = IN_SMOOTH_SUBTRACT,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_SELECT[] = { { .name = "Condition", .type = PIN_F32 },
                                { .name = "Positive", .type = PIN_F32 },
                                { .name = "Negative", .type = PIN_F32 } };
static node_desc NODE_SELECT = {
    .name = "Field Select",
    .category = "Procedural Field",
    .description = "Returns Positive when Condition is above 0, otherwise "
                   "Negative.",
    .access = &FIELD_ACCESS,
    .input_count = 3,
    .inputs = IN_SELECT,
    .output_count = 1,
    .outputs = VALUE_OUT
};
static pin_spec IN_OUTPUT[] = { { .name = "Value", .type = PIN_F32 } };
static node_desc NODE_OUTPUT = {
    .name = "Field Output",
    .category = "Procedural Field",
    .description = "Marks the field's final value, where negative is solid; "
                   "each field needs exactly one.",
    .access = &FIELD_ACCESS,
    .input_count = 1,
    .inputs = IN_OUTPUT,
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
                                          &NODE_OUTPUT };
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
