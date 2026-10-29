#include "graph/modeling/model_compile.h"
#include "types/array.h"

#define FLOAT(label, number)                                                   \
    {                                                                          \
        .name = label, .type = PIN_F32, .default_value = {                     \
            .type = PIN_F32,                                                   \
            .f = number                                                        \
        }                                                                      \
    }
#define VECTOR(label, data)                                                    \
    {                                                                          \
        .name = label, .type = PIN_STRUCT, .type_name = "ShaderVec3",          \
        .default_value = {                                                     \
            .type = PIN_STRUCT,                                                \
            .type_name = "ShaderVec3",                                         \
            .ptr = data                                                        \
        }                                                                      \
    }
#define GEOMETRY(label)                                                        \
    { .name = label,                                                           \
      .type = PIN_HANDLE,                                                      \
      .type_name = "Geometry3D",                                               \
      .default_value = { .type = PIN_HANDLE, .type_name = "Geometry3D" } }
static float ones[3] = { 1, 1, 1 };
static pin_spec sampled_inputs[] = { { .name = "Image",
                                       .type = PIN_STR,
                                       .flags = PIN_FLAG_SETTING,
                                       .default_value = { .type = PIN_STR } },
                                     VECTOR("Half Size", ones),
                                     { .name = "Resolution",
                                       .type = PIN_I32,
                                       .flags = PIN_FLAG_SETTING,
                                       .default_value = { .type = PIN_I32, .i = 2 } },
                                     FLOAT("Range", 1),
                                     FLOAT("Lipschitz", 2) };
static pin_spec sphere_inputs[] = { FLOAT("Radius", 1) };
static pin_spec box_inputs[] = { VECTOR("Half Size", ones) };
static pin_spec rounded_inputs[] = { VECTOR("Half Size", ones), FLOAT("Radius", .1f) };
static pin_spec cylinder_inputs[] = { FLOAT("Radius", 1), FLOAT("Half Height", 1) };
static pin_spec capsule_inputs[] = { FLOAT("Radius", 1), FLOAT("Half Segment", 1) };
static pin_spec torus_inputs[] = { FLOAT("Major Radius", 1),
                                   FLOAT("Minor Radius", .25f) };
static pin_spec translate_inputs[] = { GEOMETRY("Geometry"), VECTOR("Offset", NULL) };
static pin_spec rotate_inputs[] = { GEOMETRY("Geometry"), VECTOR("Angles", NULL) };
static pin_spec scale_inputs[] = { GEOMETRY("Geometry"), VECTOR("Factors", ones) };
static pin_spec reflect_inputs[] = {
    GEOMETRY("Geometry"),
    { .name = "Axis",
      .type = PIN_ENUM,
      .type_name = "ModelAxis",
      .default_value = { .type = PIN_ENUM, .type_name = "ModelAxis" } }
};
static pin_spec pair_inputs[] = { GEOMETRY("A"), GEOMETRY("B") };
static pin_spec smooth_inputs[] = { GEOMETRY("A"),
                                    GEOMETRY("B"),
                                    FLOAT("Smoothness", .25f) };
static pin_spec geometry_output[] = { GEOMETRY("Geometry") };
static pin_spec output_inputs[] = { GEOMETRY("Geometry") };
#define SOLID(symbol, label, pins, group, text)                                \
    node_desc symbol = { .name = label,                                        \
                         .input_count = sizeof pins / sizeof *pins,            \
                         .inputs = pins,                                       \
                         .output_count = 1,                                    \
                         .outputs = geometry_output,                           \
                         .category = group,                                    \
                         .description = text,                                  \
                         .display_class = NODE_DISPLAY_PURE }
SOLID(NODE_ModelSampledSdf, "Model Sampled SDF", sampled_inputs, "Model/Primitives", "A dense signed-distance lattice in an immutable RG16 PNG atlas. Samples include the box endpoints. Negative is inside; metadata and a positive outer border are validated before publication. Image must use a content-addressed .sdf.png name.");
SOLID(NODE_ModelSphere, "Model Sphere", sphere_inputs, "Model/Primitives", "A centered sphere with a positive radius below sqrt(FLT_MAX / 3) in local project units.");
SOLID(NODE_ModelBox, "Model Box", box_inputs, "Model/Primitives", "A centered box with positive outer half sizes along X, Y and Z.");
SOLID(NODE_ModelRoundedBox, "Model Rounded Box", rounded_inputs, "Model/Primitives", "A rounded box whose outer half sizes remain fixed; Radius is between zero and the smallest half size.");
SOLID(
    NODE_ModelCylinder,
    "Model Cylinder",
    cylinder_inputs,
    "Model/Primitives",
    "A capped cylinder along Y, with positive radius and half height."
);
SOLID(NODE_ModelCapsule, "Model Capsule", capsule_inputs, "Model/Primitives", "A capsule around a centered Y-axis segment, with positive radius and nonnegative half segment length.");
SOLID(NODE_ModelTorus, "Model Torus", torus_inputs, "Model/Primitives", "A ring torus around Y, with Major Radius greater than positive Minor Radius.");
SOLID(
    NODE_ModelTranslate,
    "Model Translate",
    translate_inputs,
    "Model/Transforms",
    "Moves the complete geometry by Offset in local project units."
);
SOLID(NODE_ModelRotate, "Model Rotate", rotate_inputs, "Model/Transforms", "Rotates the geometry in radians around X, then Y, then Z.");
SOLID(NODE_ModelScale, "Model Scale", scale_inputs, "Model/Transforms", "Scales geometry by positive Factors; nonuniform scale provides a conservative distance bound.");
SOLID(
    NODE_ModelReflect,
    "Model Reflect",
    reflect_inputs,
    "Model/Transforms",
    "Reflects geometry through the origin across the selected axis."
);
SOLID(NODE_ModelUnion, "Model Union", pair_inputs, "Model/Solids", "Combines both solids by their minimum signed distance.");
SOLID(
    NODE_ModelIntersection,
    "Model Intersection",
    pair_inputs,
    "Model/Solids",
    "Keeps their shared volume; disjoint bounds produce an empty model."
);
SOLID(NODE_ModelDifference, "Model Difference", pair_inputs, "Model/Solids", "Removes B from A; the cut remains within A's bounds.");
SOLID(NODE_ModelSmoothUnion, "Model Smooth Union", smooth_inputs, "Model/Solids", "Joins A and B with positive Smoothness; bounds include the polynomial blend envelope.");
static pin_spec part_inputs[] = {
    GEOMETRY("Geometry"),
    { .name = "Material",
      .type = PIN_HANDLE,
      .type_name = "ModelMaterialSlot",
      .default_value = { .type = PIN_HANDLE, .type_name = "ModelMaterialSlot" } }
};
static pin_spec linear_inputs[] = { GEOMETRY("Geometry"),
                                    { .name = "Count",
                                      .type = PIN_I32,
                                      .flags = PIN_FLAG_SETTING,
                                      .default_value = { .type = PIN_I32, .i = 1 } },
                                    VECTOR("Step", NULL) };
static pin_spec radial_inputs[] = {
    GEOMETRY("Geometry"),
    { .name = "Count",
      .type = PIN_I32,
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_I32, .i = 1 } },
    FLOAT("Angle Step", 1.57079632679f),
    { .name = "Axis",
      .type = PIN_ENUM,
      .type_name = "ModelAxis",
      .default_value = { .type = PIN_ENUM, .type_name = "ModelAxis", .e = 1 } }
};
#define PARAMETER_PIN(label, value)                                            \
    {                                                                          \
        .name = label, .type = PIN_F32,                                        \
        .flags = PIN_FLAG_SETTING | PIN_FLAG_WILDCARD, .group = 1,             \
        .default_value = {                                                     \
            .type = PIN_F32,                                                   \
            .f = value                                                         \
        }                                                                      \
    }
static pin_spec parameter_inputs[] = { PARAMETER_PIN("Default", 1),
                                       PARAMETER_PIN("Minimum", .001f),
                                       PARAMETER_PIN("Maximum", 1000) };
static pin_spec parameter_output[] = { { .name = "Value",
                                         .type = PIN_F32,
                                         .flags = PIN_FLAG_WILDCARD,
                                         .group = 1,
                                         .default_value = { .type = PIN_F32 } } };
SOLID(
    NODE_ModelPart,
    "Model Part",
    part_inputs,
    "Model/Organization",
    "Names a complete construction without changing its stable node identity."
);
SOLID(
    NODE_ModelLinearArray,
    "Model Linear Array",
    linear_inputs,
    "Model/Reuse",
    "Unions 1 to 64 instances at index times Step; Count is structural."
);
SOLID(NODE_ModelRadialArray, "Model Radial Array", radial_inputs, "Model/Reuse", "Unions 1 to 64 instances rotated about the origin by index times Angle Step around Axis.");
node_desc NODE_ModelInstance = {
    .name = "Model Instance",
    .output_count = 1,
    .outputs = geometry_output,
    .category = "Model/Reuse",
    .description = "Instances a referenced model; sparse named overrides "
                   "inherit unspecified current defaults.",
    .display_class = NODE_DISPLAY_PURE
};
node_desc NODE_ModelParameter = {
    .name = "Model Parameter",
    .input_count = 3,
    .inputs = parameter_inputs,
    .output_count = 1,
    .outputs = parameter_output,
    .category = "Model/Parameters",
    .description = "Declares a named numeric dimension with a default and "
                   "finite valid range; identity is its stable node ID.",
    .display_class = NODE_DISPLAY_PURE
};
node_desc NODE_ModelOutput = {
    .name = "Model Output",
    .input_count = 1,
    .inputs = output_inputs,
    .category = "Model",
    .description = "Publishes one unevaluated Geometry3D construction as a "
                   "bounded model.",
    .display_class = NODE_DISPLAY_FUNCTION
};
#define PROFILE(label)                                                         \
    { .name = label,                                                           \
      .type = PIN_HANDLE,                                                      \
      .type_name = "Profile2D",                                                \
      .default_value = { .type = PIN_HANDLE, .type_name = "Profile2D" } }
#define VECTOR2(label, data)                                                   \
    {                                                                          \
        .name = label, .type = PIN_STRUCT, .type_name = "ShaderVec2",          \
        .default_value = {                                                     \
            .type = PIN_STRUCT,                                                \
            .type_name = "ShaderVec2",                                         \
            .ptr = data                                                        \
        }                                                                      \
    }
static float profile_ones[2] = { 1, 1 };
static float triangle_points[3][2] = { { -1, -1 }, { 1, -1 }, { 0, 1 } };
static pin_value triangle_items[] = {
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = triangle_points[0] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = triangle_points[1] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = triangle_points[2] }
};
static pin_array triangle = { .element_type = PIN_STRUCT,
                              .type_name = "ShaderVec2",
                              .count = 3,
                              .capacity = 3,
                              .items = triangle_items };
static pin_spec profile_output[] = { PROFILE("Profile") };
static pin_spec circle_inputs[] = { FLOAT("Radius", 1), VECTOR2("Center", NULL) };
static pin_spec rectangle_inputs[] = { VECTOR2("Half Size", profile_ones),
                                       VECTOR2("Center", NULL) };
static pin_spec polygon_inputs[] = { { .name = "Vertices",
                                       .type = PIN_STRUCT,
                                       .type_name = "ShaderVec2",
                                       .container = PIN_CONTAINER_ARRAY,
                                       .flags = PIN_FLAG_SETTING,
                                       .default_value = {
                                           .type = PIN_STRUCT,
                                           .type_name = "ShaderVec2",
                                           .container = PIN_CONTAINER_ARRAY,
                                           .ptr = &triangle } } };
#define PROFILE_NODE(symbol, label, pins, text)                                \
    node_desc symbol = { .name = label,                                        \
                         .input_count = sizeof pins / sizeof *pins,            \
                         .inputs = pins,                                       \
                         .output_count = 1,                                    \
                         .outputs = profile_output,                            \
                         .category = "Model/Profiles",                         \
                         .description = text,                                  \
                         .display_class = NODE_DISPLAY_PURE }
PROFILE_NODE(
    NODE_ModelProfileCircle,
    "Model Profile Circle",
    circle_inputs,
    "A circle in profile coordinates with positive Radius and numeric Center."
);
PROFILE_NODE(NODE_ModelProfileRectangle, "Model Profile Rectangle", rectangle_inputs, "A rectangle in profile coordinates with positive Half Size and numeric Center.");
PROFILE_NODE(NODE_ModelProfilePolygon, "Model Profile Polygon", polygon_inputs, "A structural list of 3 to 32 finite vertices in strictly convex counterclockwise order, excluding the repeated closing point.");
static pin_spec extrude_inputs[] = { PROFILE("Profile"), FLOAT("Half Depth", 1) };
static pin_spec revolve_inputs[] = { PROFILE("Profile") };
SOLID(NODE_ModelExtrude, "Model Extrude", extrude_inputs, "Model/Solids", "Extrudes profile X,Y coordinates with centered positive half depth along Z.");
SOLID(NODE_ModelRevolve, "Model Revolve", revolve_inputs, "Model/Solids", "Revolves profile radius,Y coordinates fully around Y; all radial coordinates must be nonnegative.");
static pin_spec material_slot_output[] = {
    { .name = "Slot",
      .type = PIN_HANDLE,
      .type_name = "ModelMaterialSlot",
      .default_value = { .type = PIN_HANDLE, .type_name = "ModelMaterialSlot" } }
};
node_desc NODE_ModelMaterialSlot = {
    .name = "Model Material Slot",
    .output_count = 1,
    .outputs = material_slot_output,
    .category = "Model/Materials",
    .description = "Declares a stable named material binding with a default "
                   "Material asset; connect Slot to a Part Material input.",
    .display_class = NODE_DISPLAY_PURE
};

static pin_spec profile_offset_inputs[] = { PROFILE("Profile"),
                                            FLOAT("Distance", .1f) };
static pin_spec profile_pair_inputs[] = { PROFILE("A"), PROFILE("B") };
PROFILE_NODE(NODE_ModelProfileOffset, "Model Profile Offset", profile_offset_inputs, "Offsets the signed profile field outward by Distance; negative values erode.");
PROFILE_NODE(NODE_ModelProfileUnion, "Model Profile Union", profile_pair_inputs, "Combines two profile fields by minimum distance.");
PROFILE_NODE(
    NODE_ModelProfileIntersection,
    "Model Profile Intersection",
    profile_pair_inputs,
    "Keeps the overlap of two profile fields."
);
PROFILE_NODE(
    NODE_ModelProfileDifference,
    "Model Profile Difference",
    profile_pair_inputs,
    "Cuts profile B from A, including closed holes."
);
static pin_spec rounded_extrude_inputs[] = { PROFILE("Profile"),
                                             FLOAT("Half Depth", 1),
                                             FLOAT("Radius", .1f) };
static pin_spec beveled_extrude_inputs[] = { PROFILE("Profile"),
                                             FLOAT("Half Depth", 1),
                                             FLOAT("Width", .1f) };
SOLID(
    NODE_ModelRoundedExtrude,
    "Model Rounded Extrude",
    rounded_extrude_inputs,
    "Model/Solids",
    "Rounds extrusion rims while preserving the outer footprint and half "
    "depth; positive Radius cannot exceed Half Depth."
);
SOLID(NODE_ModelBeveledExtrude, "Model Beveled Extrude", beveled_extrude_inputs, "Model/Solids", "Chamfers extrusion rims at 45 degrees; positive Width cannot exceed Half Depth.");
static pin_spec offset_inputs[] = { GEOMETRY("Geometry"), FLOAT("Distance", .1f) };
static pin_spec shell_inputs[] = {
    GEOMETRY("Geometry"),
    FLOAT("Thickness", .1f),
    { .name = "Side",
      .type = PIN_ENUM,
      .type_name = "ModelShellSide",
      .default_value = { .type = PIN_ENUM, .type_name = "ModelShellSide" } }
};
SOLID(NODE_ModelOffset, "Model Offset", offset_inputs, "Model/Solids", "Offsets a signed distance field; thickness is measured in field units, including conservative fields after nonuniform scaling.");
SOLID(NODE_ModelShell, "Model Shell", shell_inputs, "Model/Solids", "Turns a field into a band of full Thickness, centered, outside, or inside its zero surface.");
static float path_points[2][3] = { { 0, -1, 0 }, { 0, 1, 0 } };
static pin_value path_items[] = {
    { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = path_points[0] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = path_points[1] }
};
static pin_array path_array = { .element_type = PIN_STRUCT,
                                .type_name = "ShaderVec3",
                                .items = path_items,
                                .count = 2,
                                .capacity = 2 };
#define POINTS(label, typename, data)                                          \
    {                                                                          \
        .name = label, .type = PIN_STRUCT, .type_name = typename,              \
        .container = PIN_CONTAINER_ARRAY, .flags = PIN_FLAG_SETTING,           \
        .default_value = {                                                     \
            .type = PIN_STRUCT,                                                \
            .type_name = typename,                                             \
            .container = PIN_CONTAINER_ARRAY,                                  \
            .ptr = data                                                        \
        }                                                                      \
    }
#define SETTING_INT(label, value)                                                     \
    {                                                                                 \
        .name = label, .type = PIN_I32, .flags = PIN_FLAG_SETTING, .default_value = { \
            .type = PIN_I32,                                                          \
            .i = value                                                                \
        }                                                                             \
    }
#define CLOSED                                                                 \
    {                                                                          \
        .name = "Closed", .type = PIN_BOOL, .flags = PIN_FLAG_SETTING,         \
        .default_value = {                                                     \
            .type = PIN_BOOL                                                   \
        }                                                                      \
    }
static pin_spec tube_inputs[] = { POINTS("Points", "ShaderVec3", &path_array),
                                  FLOAT("Radius", .1f),
                                  CLOSED };
SOLID(NODE_ModelTubeSweep, "Model Tube Sweep", tube_inputs, "Model/Paths", "A circular tube around a finite polyline, with spherical joints and caps; at most 32 segments. This is not an arbitrary transported profile.");
static float cubic_points[4][3] = { { 0, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
static pin_value cubic_items[] = {
    { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = cubic_points[0] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = cubic_points[1] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = cubic_points[2] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = cubic_points[3] }
};
static pin_array cubic_array = { .element_type = PIN_STRUCT,
                                 .type_name = "ShaderVec3",
                                 .items = cubic_items,
                                 .count = 4,
                                 .capacity = 4 };
static pin_spec bezier_tube_inputs[] = { POINTS("Control Points", "ShaderVec3", &cubic_array),
                                         FLOAT("Radius", .1f),
                                         SETTING_INT("Steps", 4),
                                         CLOSED };
SOLID(NODE_ModelBezierTube, "Model Bezier Tube", bezier_tube_inputs, "Model/Paths", "Cubic controls form a bounded sampled polyline tube; Steps is per cubic and the total cannot exceed 32 segments.");
static float bezier_circle[12][2] = {
    { 1, 0 },  { 1, .55228475f },   { .55228475f, 1 },
    { 0, 1 },  { -.55228475f, 1 },  { -1, .55228475f },
    { -1, 0 }, { -1, -.55228475f }, { -.55228475f, -1 },
    { 0, -1 }, { .55228475f, -1 },  { 1, -.55228475f }
};
static pin_value bezier_circle_items[] = {
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[0] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[1] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[2] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[3] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[4] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[5] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[6] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[7] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[8] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[9] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[10] },
    { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = bezier_circle[11] }
};
static pin_array bezier_circle_array = { .element_type = PIN_STRUCT,
                                         .type_name = "ShaderVec2",
                                         .items = bezier_circle_items,
                                         .count = 12,
                                         .capacity = 12 };
static pin_spec bezier_profile_inputs[] = {
    POINTS("Control Points", "ShaderVec2", &bezier_circle_array),
    SETTING_INT("Steps", 4)
};
PROFILE_NODE(NODE_ModelProfileBezier, "Model Profile Bezier", bezier_profile_inputs, "Closed cubic controls use three values per span and implicit closing P0; the bounded sampled polygon defines the modeled surface.");
static pin_spec around_rotate_inputs[] = { GEOMETRY("Geometry"),
                                           VECTOR("Pivot", NULL),
                                           VECTOR("Angles", NULL) };
static pin_spec around_scale_inputs[] = { GEOMETRY("Geometry"),
                                          VECTOR("Pivot", NULL),
                                          VECTOR("Factors", ones) };
SOLID(NODE_ModelRotateAround, "Model Rotate Around", around_rotate_inputs, "Model/Transforms", "Rotates about an explicit point in the incoming model frame, in radians X then Y then Z.");
SOLID(NODE_ModelScaleAround, "Model Scale Around", around_scale_inputs, "Model/Transforms", "Scales about an explicit point in the incoming model frame, with positive Factors.");
static pin_spec anchor_inputs[] = { VECTOR("Position", NULL), VECTOR("Angles", NULL) };
static pin_spec anchor_outputs[] = {
    { .name = "Anchor",
      .type = PIN_HANDLE,
      .type_name = "Anchor3D",
      .default_value = { .type = PIN_HANDLE, .type_name = "Anchor3D" } },
    VECTOR("Position", NULL),
    VECTOR("Angles", NULL)
};
node_desc NODE_ModelAnchor = {
    .name = "Model Anchor",
    .inputs = anchor_inputs,
    .input_count = 2,
    .outputs = anchor_outputs,
    .output_count = 3,
    .category = "Model/Organization",
    .description = "A stable named attachment frame in explicit asset "
                   "coordinates; Position and Angles can use model parameters.",
    .display_class = NODE_DISPLAY_PURE
};
static pin_spec attach_inputs[] = {
    GEOMETRY("Geometry"),
    { .name = "Source Anchor",
      .type = PIN_HANDLE,
      .type_name = "Anchor3D",
      .default_value = { .type = PIN_HANDLE, .type_name = "Anchor3D" } },
    { .name = "Target Anchor",
      .type = PIN_HANDLE,
      .type_name = "Anchor3D",
      .default_value = { .type = PIN_HANDLE, .type_name = "Anchor3D" } }
};
SOLID(NODE_ModelAttach, "Model Attach", attach_inputs, "Model/Transforms", "Rigidly maps Source Anchor to Target Anchor using the same authoritative model parameters and reusable instance scope.");
static const enum_value_desc shell_sides[] = {
    { 0, "MODEL_SHELL_CENTERED", "Centered" },
    { 1, "MODEL_SHELL_OUTWARD", "Outward" },
    { 2, "MODEL_SHELL_INWARD", "Inward" }
};
static const enum_desc shell_side_type = { .name = "ModelShellSide",
                                           .values = shell_sides,
                                           .value_count = 3 };
static const enum_value_desc axes[] = { { 0, "MODEL_AXIS_X", "X" },
                                        { 1, "MODEL_AXIS_Y", "Y" },
                                        { 2, "MODEL_AXIS_Z", "Z" } };
static const enum_desc axis_type = { .name = "ModelAxis",
                                     .values = axes,
                                     .value_count = 3 };
static const enum_desc *const enums[] = { &axis_type, &shell_side_type };
static node_desc *const nodes[] = { &NODE_ModelSampledSdf,
                                    &NODE_ModelSphere,
                                    &NODE_ModelBox,
                                    &NODE_ModelRoundedBox,
                                    &NODE_ModelCylinder,
                                    &NODE_ModelCapsule,
                                    &NODE_ModelTorus,
                                    &NODE_ModelTranslate,
                                    &NODE_ModelRotate,
                                    &NODE_ModelScale,
                                    &NODE_ModelReflect,
                                    &NODE_ModelUnion,
                                    &NODE_ModelIntersection,
                                    &NODE_ModelDifference,
                                    &NODE_ModelSmoothUnion,
                                    &NODE_ModelOutput,
                                    &NODE_ModelPart,
                                    &NODE_ModelInstance,
                                    &NODE_ModelParameter,
                                    &NODE_ModelLinearArray,
                                    &NODE_ModelRadialArray,
                                    &NODE_ModelProfileCircle,
                                    &NODE_ModelProfileRectangle,
                                    &NODE_ModelProfilePolygon,
                                    &NODE_ModelExtrude,
                                    &NODE_ModelRevolve,
                                    &NODE_ModelMaterialSlot,
                                    &NODE_ModelProfileOffset,
                                    &NODE_ModelProfileUnion,
                                    &NODE_ModelProfileIntersection,
                                    &NODE_ModelProfileDifference,
                                    &NODE_ModelRoundedExtrude,
                                    &NODE_ModelBeveledExtrude,
                                    &NODE_ModelOffset,
                                    &NODE_ModelShell,
                                    &NODE_ModelTubeSweep,
                                    &NODE_ModelBezierTube,
                                    &NODE_ModelProfileBezier,
                                    &NODE_ModelRotateAround,
                                    &NODE_ModelScaleAround,
                                    &NODE_ModelAnchor,
                                    &NODE_ModelAttach };
const registry_pack MODEL_PACK = { .name = "model",
                                   .nodes = nodes,
                                   .node_count = sizeof nodes / sizeof *nodes,
                                   .enums = enums,
                                   .enum_count = 2 };
node *model_graph_add_output(graph *g)
{
    return graph_add_node(g, &NODE_ModelOutput);
}
