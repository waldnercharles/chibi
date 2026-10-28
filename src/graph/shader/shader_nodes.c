#include "graph/authoring/graph_edit.h"
#include "graph/model/behavior_asset.h"
#include "graph/shader/shader_graph.h"
#include "types/array.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FLOAT_PIN(label)                                                       \
    {                                                                          \
        .name = label, .type = PIN_F32, .default_value = {.type = PIN_F32 }    \
    }
#define FLOAT_DEFAULT(label, number)                                           \
    {                                                                          \
        .name = label, .type = PIN_F32, .default_value = {                     \
            .type = PIN_F32,                                                   \
            .f = number                                                        \
        }                                                                      \
    }
#define VECTOR_PIN(label, w)                                                   \
    {                                                                          \
        .name = label, .type = PIN_STRUCT, .type_name = "ShaderVec" #w,        \
        .default_value = {                                                     \
            .type = PIN_STRUCT,                                                \
            .type_name = "ShaderVec" #w                                        \
        }                                                                      \
    }
#define ANY_PIN(label)                                                         \
    {                                                                          \
        .name = label, .type = PIN_F32, .flags = PIN_FLAG_WILDCARD,            \
        .group = 1, .default_value = {                                         \
            .type = PIN_F32                                                    \
        }                                                                      \
    }
static const type_field fields[] = {
    { .name = "x", .type = PIN_F32, .type_name = "number", .offset = 0, .size = 4 },
    { .name = "y", .type = PIN_F32, .type_name = "number", .offset = 4, .size = 4 },
    { .name = "z", .type = PIN_F32, .type_name = "number", .offset = 8, .size = 4 },
    { .name = "w", .type = PIN_F32, .type_name = "number", .offset = 12, .size = 4 },
};
static const type_desc vec2_type = { .name = "ShaderVec2",
                                     .size = 8,
                                     .fields = fields,
                                     .field_count = 2 };
static const type_desc vec3_type = { .name = "ShaderVec3",
                                     .size = 12,
                                     .fields = fields,
                                     .field_count = 3 };
static const type_desc vec4_type = { .name = "ShaderVec4",
                                     .size = 16,
                                     .fields = fields,
                                     .field_count = 4 };
static const type_field ramp_fields[] = { { .name = "Position",
                                            .type = PIN_F32,
                                            .type_name = "number",
                                            .offset = offsetof(shader_ramp_stop, position),
                                            .size = sizeof(float) },
                                          { .name = "Color",
                                            .type = PIN_STRUCT,
                                            .type_name = "ShaderVec4",
                                            .offset = offsetof(shader_ramp_stop, color),
                                            .size = 4 * sizeof(float) } };
static const type_desc ramp_type = { .name = "ShaderRampStop",
                                     .size = sizeof(shader_ramp_stop),
                                     .fields = ramp_fields,
                                     .field_count = 2 };
static const type_desc *const types[] = { &vec2_type, &vec3_type, &vec4_type, &ramp_type };
static pin_spec scalar_out[] = { FLOAT_PIN("Value") };
static pin_spec any_out[] = { ANY_PIN("Value") };
static pin_spec position_out[] = { VECTOR_PIN("Position", 2) };
static pin_spec uv_out[] = { VECTOR_PIN("UV", 2) };
static pin_spec vector2_out[] = { VECTOR_PIN("Value", 2) };
static pin_spec vector3_out[] = { VECTOR_PIN("Value", 3) };
static pin_spec influence_out[] = { VECTOR_PIN("Color", 3) };
static pin_spec influence_in[] = { VECTOR_PIN("Position", 3) };
static pin_spec vector4_out[] = { VECTOR_PIN("Value", 4) };
static pin_spec unary[] = { ANY_PIN("Value") };
static pin_spec binary[] = { ANY_PIN("A"), ANY_PIN("B") };
static pin_spec ternary[] = { ANY_PIN("A"), ANY_PIN("B"), ANY_PIN("Alpha") };
static pin_spec selection[] = { ANY_PIN("False"), ANY_PIN("True"), FLOAT_PIN("Condition") };
static pin_spec circle[] = { VECTOR_PIN("Position", 2), FLOAT_PIN("Radius") };
static pin_spec box[] = { VECTOR_PIN("Position", 2), VECTOR_PIN("Half Size", 2) };
static pin_spec sdf_pair[] = { FLOAT_PIN("A"), FLOAT_PIN("B") };
static pin_spec smooth[] = { FLOAT_PIN("A"), FLOAT_PIN("B"), FLOAT_PIN("Smoothness") };
static pin_spec vector2_in[] = { FLOAT_PIN("X"), FLOAT_PIN("Y") };
static pin_spec vector3_in[] = { FLOAT_PIN("X"), FLOAT_PIN("Y"), FLOAT_PIN("Z") };
static pin_spec limits[] = { ANY_PIN("Value"), FLOAT_PIN("Min"), FLOAT_DEFAULT("Max", 1) };
static pin_spec threshold[] = { ANY_PIN("Value"), FLOAT_DEFAULT("Edge", 0.5f) };
static pin_spec noise_in[] = { ANY_PIN("Position"), FLOAT_DEFAULT("Scale", 1) };
static pin_spec vector4_in[] = { FLOAT_PIN("X"),
                                 FLOAT_PIN("Y"),
                                 FLOAT_PIN("Z"),
                                 FLOAT_PIN("W") };
static pin_spec surface[] = {
    { .name = "Base Color",
      .type = PIN_STRUCT,
      .type_name = "ShaderVec3",
      .flags = PIN_FLAG_COLOR,
      .default_value = { .type = PIN_STRUCT, .type_name = "ShaderVec3" } },
    { .name = "Emission",
      .type = PIN_STRUCT,
      .type_name = "ShaderVec3",
      .flags = PIN_FLAG_COLOR,
      .default_value = { .type = PIN_STRUCT, .type_name = "ShaderVec3" } },
    FLOAT_DEFAULT("Roughness", 0.5f),
    FLOAT_PIN("Metallic"),
    VECTOR_PIN("Normal", 3),
    FLOAT_DEFAULT("Opacity", 1),
    { .name = "Unlit", .type = PIN_BOOL, .default_value = { .type = PIN_BOOL } },
    /* World units of relief above the surface; 2D shading casts its shadows. */
    FLOAT_PIN("Height"),
    /* The share of ambient and sky light that reaches the surface. */
    FLOAT_DEFAULT("Occlusion", 1),
    { .name = "Alpha Mode",
      .type = PIN_I32,
      .type_name = "ShaderAlphaMode",
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_I32, .i = SHADER_ALPHA_BLEND } },
    FLOAT_DEFAULT("Alpha Cutoff", .5f)
};
_Static_assert(sizeof surface / sizeof *surface == SHADER_SURFACE_INPUT_COUNT, "surface pin schema");
static pin_spec mapping2[] = { VECTOR_PIN("Position", 2),
                               VECTOR_PIN("Offset", 2),
                               FLOAT_PIN("Rotation"),
                               VECTOR_PIN("Scale", 2) };
static pin_spec mapping3[] = { VECTOR_PIN("Position", 3),
                               VECTOR_PIN("Offset", 3),
                               VECTOR_PIN("Rotation", 3),
                               VECTOR_PIN("Scale", 3) };
static pin_spec star_in[] = { VECTOR_PIN("Direction", 3),
                              FLOAT_DEFAULT("Density", 10),
                              FLOAT_DEFAULT("Brightness", 1) };
static pin_spec fresnel_in[] = {
    { .name = "Normal",
      .type = PIN_STRUCT,
      .type_name = "ShaderVec3",
      .default_value = { .type = PIN_STRUCT, .type_name = "ShaderVec3" } },
    { .name = "View Direction",
      .type = PIN_STRUCT,
      .type_name = "ShaderVec3",
      .default_value = { .type = PIN_STRUCT, .type_name = "ShaderVec3" } },
    FLOAT_DEFAULT("Power", 5),
    FLOAT_DEFAULT("Base", .04f)
};
static pin_spec remap_in[] = { ANY_PIN("Value"),
                               VECTOR_PIN("From Range", 2),
                               VECTOR_PIN("To Range", 2),
                               { .name = "Clamp",
                                 .type = PIN_BOOL,
                                 .default_value = { .type = PIN_BOOL, .b = true } } };
static pin_spec gradient_in[] = { VECTOR_PIN("Position", 2),
                                  { .name = "Type",
                                    .type = PIN_I32,
                                    .flags = PIN_FLAG_SETTING,
                                    .default_value = { .type = PIN_I32 } },
                                  FLOAT_DEFAULT("Scale", 1),
                                  FLOAT_PIN("Offset") };
#define TEXTURE_POSITION                                                       \
    { .name = "Position",                                                      \
      .type = PIN_STRUCT,                                                      \
      .type_name = "ShaderVec3",                                               \
      .flags = PIN_FLAG_WILDCARD,                                              \
      .group = 1,                                                              \
      .default_value = { .type = PIN_STRUCT, .type_name = "ShaderVec3" } }
static pin_spec fractal_in[] = { TEXTURE_POSITION,
                                 FLOAT_DEFAULT("Scale", 1),
                                 FLOAT_DEFAULT("Detail", 4),
                                 FLOAT_DEFAULT("Roughness", .5f) };
static pin_spec voronoi_in[] = { TEXTURE_POSITION,
                                 FLOAT_DEFAULT("Scale", 1),
                                 FLOAT_DEFAULT("Randomness", 1) };
static pin_spec voronoi_out[] = { FLOAT_PIN("Distance"), VECTOR_PIN("Cell Color", 3) };
static pin_spec ramp_in[] = {
    FLOAT_DEFAULT("Factor", .5f),
    { .name = "Stops",
      .type = PIN_STRUCT,
      .type_name = "ShaderRampStop",
      .container = PIN_CONTAINER_ARRAY,
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_STRUCT,
                         .type_name = "ShaderRampStop",
                         .container = PIN_CONTAINER_ARRAY } },
    { .name = "Interpolation",
      .type = PIN_I32,
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_I32 } }
};
static pin_spec color_alpha_out[] = {
    { .name = "Color",
      .type = PIN_STRUCT,
      .type_name = "ShaderVec3",
      .flags = PIN_FLAG_COLOR,
      .default_value = { .type = PIN_STRUCT, .type_name = "ShaderVec3" } },
    FLOAT_PIN("Alpha")
};
static pin_spec image_in[] = {
    VECTOR_PIN("UV", 2),
    { .name = "Image",
      .type = PIN_STR,
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_STR } },
    { .name = "Color Space",
      .type = PIN_I32,
      .type_name = "ShaderImageColorSpace",
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_I32, .i = SHADER_IMAGE_SRGB } },
    { .name = "Filter",
      .type = PIN_I32,
      .type_name = "ShaderImageFilter",
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_I32, .i = SHADER_IMAGE_BILINEAR } },
    { .name = "Wrap",
      .type = PIN_I32,
      .type_name = "ShaderImageWrap",
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_I32, .i = SHADER_IMAGE_REPEAT } }
};
_Static_assert(sizeof image_in / sizeof *image_in == SHADER_IMAGE_INPUT_COUNT, "image pin schema");
static pin_spec projection_in[] = { VECTOR_PIN("Position", 3),
                                    VECTOR_PIN("Scale", 2),
                                    VECTOR_PIN("Offset", 2),
                                    { .name = "Axis",
                                      .type = PIN_I32,
                                      .type_name = "ShaderMappingAxis",
                                      .flags = PIN_FLAG_SETTING,
                                      .default_value = { .type = PIN_I32, .i = 1 } } };
static pin_spec normal_map_in[] = { VECTOR_PIN("Color", 3),
                                    VECTOR_PIN("UV", 2),
                                    FLOAT_DEFAULT("Strength", 1),
                                    FLOAT_DEFAULT("Distance", .001f),
                                    { .name = "Convention",
                                      .type = PIN_I32,
                                      .type_name = "ShaderNormalConvention",
                                      .flags = PIN_FLAG_SETTING,
                                      .default_value = { .type = PIN_I32 } } };
static pin_spec bump_in[] = { FLOAT_PIN("Height"),
                              FLOAT_DEFAULT("Strength", 1),
                              FLOAT_DEFAULT("Distance", .001f),
                              VECTOR_PIN("Normal", 3) };
static pin_spec triplanar_in[] = {
    VECTOR_PIN("Position", 3),
    FLOAT_DEFAULT("Sharpness", 4),
    FLOAT_DEFAULT("Distance", .001f),
    { .name = "Image",
      .type = PIN_STR,
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_STR } },
    { .name = "Color Space",
      .type = PIN_I32,
      .type_name = "ShaderImageColorSpace",
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_I32, .i = SHADER_IMAGE_SRGB } },
    { .name = "Filter",
      .type = PIN_I32,
      .type_name = "ShaderImageFilter",
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_I32, .i = SHADER_IMAGE_BILINEAR } },
    { .name = "Wrap",
      .type = PIN_I32,
      .type_name = "ShaderImageWrap",
      .flags = PIN_FLAG_SETTING,
      .default_value = { .type = PIN_I32, .i = SHADER_IMAGE_REPEAT } }
};
static pin_spec decal_in[] = { VECTOR_PIN("Position", 3),
                               VECTOR_PIN("Half Size", 3),
                               FLOAT_DEFAULT("Feather", .01f),
                               FLOAT_DEFAULT("Facing", -1) };
static pin_spec shape[] = { FLOAT_PIN("Distance") };
#define NODE(symbol, label, count, pins, outs, text)                           \
    static node_desc symbol = { .name = "Shader " label,                       \
                                .input_count = count,                          \
                                .inputs = pins,                                \
                                .output_count = 1,                             \
                                .outputs = outs,                               \
                                .category = "Shader",                          \
                                .description = text,                           \
                                .display_class = NODE_DISPLAY_PURE }
NODE(constant, "Constant", 0, NULL, any_out, "Returns a fixed authored value, as a scalar, vector or color.");
NODE(parameter, "Parameter", 0, NULL, any_out, "Returns a named material input: its default, unless the entity's Shader or Set Material Parameter overrides it.");
NODE(position, "Position", 0, NULL, position_out, "Returns the local 2D position in authored units, Y up; the local XZ plane on a 3D solid.");
NODE(uv, "UV", 0, NULL, uv_out, "Returns normalized surface coordinates, 0 to 1 across the shape, with (0, 0) at the top-left.");
NODE(world_position, "WorldPosition", 0, NULL, vector3_out, "Returns the world-space position; 2D shapes map onto the XZ plane at zero height.");
NODE(surface_influence, "Surface Influence", 1, influence_in, influence_out, "Returns the color of scene Surface Influence activity at Position, which defaults to world position; black with none.");
NODE(object_position, "ObjectPosition", 0, NULL, vector3_out, "Returns the position in the solid's authored units, before actor rotation and translation; 2D maps to XZ.");
NODE(part_position, "PartPosition", 0, NULL, vector3_out, "Returns position in the winning model part coordinate frame; ordinary surfaces use object coordinates.");
NODE(normal, "Normal", 0, NULL, vector3_out, "Returns the world-space surface normal; +Y for 2D shapes.");
NODE(view_direction, "ViewDirection", 0, NULL, vector3_out, "Returns the unit direction from the surface toward the camera; +Y for 2D shapes.");
NODE(time_node, "Time", 0, NULL, scalar_out, "Returns simulation time in seconds.");
NODE(add, "Add", 2, binary, any_out, "Adds A and B per component; a scalar applies to every component of a vector.");
NODE(subtract, "Subtract", 2, binary, any_out, "Subtracts B from A per component; a scalar applies to every component of a vector.");
NODE(multiply, "Multiply", 2, binary, any_out, "Multiplies A by B per component; a scalar scales every component of a vector.");
NODE(divide, "Divide", 2, binary, any_out, "Divides A by B per component, returning 0 where B is 0.");
NODE(minimum, "Min", 2, binary, any_out, "Returns the smaller of A and B per component.");
NODE(maximum, "Max", 2, binary, any_out, "Returns the larger of A and B per component.");
NODE(absolute, "Abs", 1, unary, any_out, "Returns the absolute value of each component.");
NODE(sine, "Sin", 1, unary, any_out, "Returns the sine of each component, in radians.");
NODE(cosine, "Cos", 1, unary, any_out, "Returns the cosine of each component, in radians.");
NODE(fraction, "Fract", 1, unary, any_out, "Returns each component minus its floor, from 0 up to 1 even for negative values.");
NODE(floor_node, "Floor", 1, unary, any_out, "Rounds each component down to the nearest whole number.");
NODE(sqrt_node, "Sqrt", 1, unary, any_out, "Returns the square root of each component, treating negative values as 0.");
NODE(sign_node, "Sign", 1, unary, any_out, "Returns -1, 0 or 1 per component for negative, zero or positive values.");
NODE(length_node, "Length", 1, unary, scalar_out, "Returns the Euclidean length of Value; the absolute value of a scalar.");
NODE(dot, "Dot", 2, binary, scalar_out, "Returns the dot product of A and B, which must have the same width.");
NODE(mix_node, "Mix", 3, ternary, any_out, "Blends from A to B by Alpha per component without clamping; Alpha 0 gives A and 1 gives B.");
NODE(select_node, "Select", 3, selection, any_out, "Returns True when Condition is nonzero, otherwise False.");
NODE(circle_node, "Circle", 2, circle, scalar_out, "Returns the signed distance from Position to a circle of Radius at the origin; negative inside.");
NODE(box_node, "Box", 2, box, scalar_out, "Returns the signed distance from Position to an origin-centered box of Half Size; negative inside.");
NODE(union_node, "Union", 2, sdf_pair, scalar_out, "Combines signed distances A and B into their union by taking the smaller.");
NODE(intersection, "Intersection", 2, sdf_pair, scalar_out, "Keeps the overlap of signed distances A and B by taking the larger.");
NODE(difference, "Difference", 2, sdf_pair, scalar_out, "Cuts signed distance B out of A.");
NODE(smooth_node, "SmoothUnion", 3, smooth, scalar_out, "Merges signed distances A and B with a rounded blend Smoothness wide; 0 gives a sharp union.");
NODE(vector2, "Vector2", 2, vector2_in, vector2_out, "Builds a 2D vector from X and Y.");
NODE(vector3, "Vector3", 3, vector3_in, vector3_out, "Builds a 3D vector or color from X, Y and Z.");
NODE(vector4, "Vector4", 4, vector4_in, vector4_out, "Builds a 4D vector from X, Y, Z and W.");
NODE(normalize_node, "Normalize", 1, unary, any_out, "Scales Value to unit length; a zero vector stays zero.");
NODE(clamp_node, "Clamp", 3, limits, any_out, "Limits each component of Value to between Min and Max, taken in either order.");
NODE(saturate_node, "Saturate", 1, unary, any_out, "Clamps each component of Value to the range 0 to 1.");
NODE(step_node, "Step", 2, threshold, any_out, "Returns 1 per component where Value is at least Edge, otherwise 0.");
NODE(smoothstep_node, "Smoothstep", 3, limits, any_out, "Eases each component of Value from 0 at Min to 1 at Max along an S-curve; equal bounds act as a step.");
NODE(noise_node, "Noise", 2, noise_in, scalar_out, "Returns smooth value noise from 0 to 1 at Position times Scale, for 1D to 3D positions.");
NODE(x_node, "X", 1, unary, scalar_out, "Returns the X component of Value.");
NODE(y_node, "Y", 1, unary, scalar_out, "Returns the Y component of Value, which needs at least two components.");
NODE(z_node, "Z", 1, unary, scalar_out, "Returns the Z component of Value, which needs at least three components.");
NODE(w_node, "W", 1, unary, scalar_out, "Returns the W component of Value, which needs four components.");
NODE(mapping_node, "Mapping", 4, mapping2, vector2_out, "Scales, rotates by Rotation degrees, then offsets a 2D Position, which defaults to UV.");
NODE(mapping3d_node, "Mapping3D", 4, mapping3, vector3_out, "Scales, rotates by Rotation degrees about X, Y then Z, then offsets Position, which defaults to object position.");
NODE(fresnel_node, "Fresnel", 4, fresnel_in, scalar_out, "Returns Base facing the view, rising to 1 edge-on with sharpness Power; Normal and View Direction default to the surface's.");
NODE(remap_node, "Remap", 4, remap_in, any_out, "Maps Value from From Range to To Range per component, clamping to To Range when Clamp is on.");
NODE(gradient_node, "Gradient", 4, gradient_in, scalar_out, "Returns an X, Y, Radial or Angular ramp of Position per Type, times Scale plus Offset; Position defaults to UV.");
NODE(star_node, "Star Field", 3, star_in, vector3_out, "Returns point stars along Direction, each about a pixel wide at any field of view: Density per square degree (0-100); Brightness is the brightest star's peak where a pixel spans 0.03 degrees, and stars keep their energy at other sizes. Direction defaults to World Position, the direction looked along in a sky material.");
NODE(fractal_node, "Fractal Noise", 4, fractal_in, scalar_out, "Returns 0 to 1 fractal value noise at Position times Scale; Detail sets 1 to 8 octaves and Roughness their falloff.");
static node_desc voronoi_node = {
    .name = "Shader Voronoi",
    .input_count = 3,
    .inputs = voronoi_in,
    .output_count = 2,
    .outputs = voronoi_out,
    .category = "Shader",
    .description = "Returns Distance to the nearest cell feature and its "
                   "random Cell Color at Position times Scale; Randomness "
                   "jitters features.",
    .display_class = NODE_DISPLAY_PURE
};
static node_desc ramp_node = {
    .name = "Shader Color Ramp",
    .input_count = 3,
    .inputs = ramp_in,
    .output_count = 2,
    .outputs = color_alpha_out,
    .category = "Shader",
    .description = "Maps Factor through the Stops to Color and Alpha, using "
                   "linear, constant or smoothstep Interpolation.",
    .display_class = NODE_DISPLAY_PURE
};
static node_desc image_node = {
    .name = "Shader Image Texture",
    .input_count = SHADER_IMAGE_INPUT_COUNT,
    .inputs = image_in,
    .output_count = 2,
    .outputs = color_alpha_out,
    .category = "Shader",
    .description = "Samples Image at UV, which defaults to surface UV, "
                   "returning linear Color and Alpha; (0, 0) is the top-left.",
    .display_class = NODE_DISPLAY_PURE
};
NODE(planar_node, "Planar Mapping", 4, projection_in, uv_out, "Projects Position, defaulting to PartPosition, onto the selected axis plane, then scales and offsets UV.");
NODE(cylindrical_node, "Cylindrical Mapping", 4, projection_in, uv_out, "Maps Position, defaulting to PartPosition, around the selected axis; U wraps one revolution and V follows the axis.");
NODE(normal_map_node, "Normal Map", 5, normal_map_in, vector3_out, "Decodes linear RGB normal data into a world-space normal using the UV expression; Distance is world-unit derivative spacing. OpenGL's green tilts up the image, DirectX's down. OpenGL Tangent uses unit orthogonal axes, independent of UV density.");
NODE(bump_node, "Bump", 4, bump_in, vector3_out, "Perturbs the world normal from six nearby evaluations of Height; Height and Distance are world units. Nested sampled derivative expressions are unsupported.");
NODE(decal_node, "Decal Mask", 4, decal_in, scalar_out, "Masks Position inside Half Size with an inward Feather. Facing -1 disables face rejection; otherwise keeps normals facing mapped +Z above the threshold.");
static node_desc triplanar_node = {
    .name = "Shader Triplanar Texture",
    .input_count = 7,
    .inputs = triplanar_in,
    .output_count = 2,
    .outputs = color_alpha_out,
    .category = "Shader",
    .description = "Blends three samples of one image in Position space, "
                   "defaulting to PartPosition. Sharpness controls axis "
                   "blending; Distance is world-unit derivative spacing. Use "
                   "Linear color space for data textures.",
    .display_class = NODE_DISPLAY_PURE
};
static const enum_value_desc mapping_axes[] = { { 0, "SHADER_MAPPING_X", "X" },
                                                { 1, "SHADER_MAPPING_Y", "Y" },
                                                { 2, "SHADER_MAPPING_Z", "Z" } };
static const enum_value_desc normal_conventions[] = {
    { SHADER_NORMAL_DIRECTX, "SHADER_NORMAL_DIRECTX", "DirectX (-Y)" },
    { SHADER_NORMAL_OPENGL, "SHADER_NORMAL_OPENGL", "OpenGL (+Y)" },
    { SHADER_NORMAL_OPENGL_TANGENT, "SHADER_NORMAL_OPENGL_TANGENT", "OpenGL Tangent (+Y)" }
};
static const enum_value_desc image_spaces[] = {
    { SHADER_IMAGE_SRGB, "SHADER_IMAGE_SRGB", "sRGB color" },
    { SHADER_IMAGE_LINEAR, "SHADER_IMAGE_LINEAR", "Linear data" }
};
static const enum_value_desc image_filters[] = {
    { SHADER_IMAGE_NEAREST, "SHADER_IMAGE_NEAREST", "Nearest" },
    { SHADER_IMAGE_BILINEAR, "SHADER_IMAGE_BILINEAR", "Bilinear" }
};
static const enum_value_desc image_wraps[] = {
    { SHADER_IMAGE_REPEAT, "SHADER_IMAGE_REPEAT", "Repeat" },
    { SHADER_IMAGE_CLAMP, "SHADER_IMAGE_CLAMP", "Clamp" }
};
static const enum_value_desc alpha_modes[] = {
    { SHADER_ALPHA_BLEND, "SHADER_ALPHA_BLEND", "Blend" },
    { SHADER_ALPHA_OPAQUE, "SHADER_ALPHA_OPAQUE", "Opaque" },
    { SHADER_ALPHA_MASK, "SHADER_ALPHA_MASK", "Mask" }
};
static const enum_desc mapping_axis_type = { .name = "ShaderMappingAxis",
                                             .values = mapping_axes,
                                             .value_count = 3 };
static const enum_desc normal_convention_type = {
    .name = "ShaderNormalConvention",
    .values = normal_conventions,
    .value_count = 3
};
static const enum_desc image_space_type = { .name = "ShaderImageColorSpace",
                                            .values = image_spaces,
                                            .value_count = 2 };
static const enum_desc image_filter_type = { .name = "ShaderImageFilter",
                                             .values = image_filters,
                                             .value_count = 2 };
static const enum_desc image_wrap_type = { .name = "ShaderImageWrap",
                                           .values = image_wraps,
                                           .value_count = 2 };
static const enum_desc alpha_mode_type = { .name = "ShaderAlphaMode",
                                           .values = alpha_modes,
                                           .value_count = 3 };
static const enum_desc *const enums[] = {
    &mapping_axis_type, &normal_convention_type, &image_space_type,
    &image_filter_type, &image_wrap_type,        &alpha_mode_type
};
static node_desc surface_output = {
    .name = "Shader Surface Output",
    .input_count = SHADER_SURFACE_INPUT_COUNT,
    .inputs = surface,
    .category = "Shader",
    .description = "Receives the material's final shading; a zero Normal keeps "
                   "the geometry normal, Height adds world-unit relief, and "
                   "Occlusion dims ambient light. Alpha Mode Blend mixes by "
                   "Opacity, Opaque ignores it, and Mask keeps pixels whose "
                   "Opacity reaches Alpha Cutoff.",
    .display_class = NODE_DISPLAY_FUNCTION
};
static node_desc shape_output = {
    .name = "Shader Shape Output",
    .input_count = 1,
    .inputs = shape,
    .category = "Shader",
    .description = "Receives the shape's signed Distance to its outline: "
                   "negative inside, zero on the edge, positive outside.",
    .display_class = NODE_DISPLAY_FUNCTION
};
static node_desc *const nodes[] = { &constant,
                                    &parameter,
                                    &position,
                                    &time_node,
                                    &add,
                                    &subtract,
                                    &multiply,
                                    &divide,
                                    &minimum,
                                    &maximum,
                                    &absolute,
                                    &sine,
                                    &cosine,
                                    &fraction,
                                    &floor_node,
                                    &sqrt_node,
                                    &sign_node,
                                    &length_node,
                                    &dot,
                                    &mix_node,
                                    &select_node,
                                    &circle_node,
                                    &box_node,
                                    &union_node,
                                    &intersection,
                                    &difference,
                                    &smooth_node,
                                    &vector2,
                                    &vector4,
                                    &x_node,
                                    &y_node,
                                    &z_node,
                                    &w_node,
                                    &world_position,
                                    &surface_influence,
                                    &surface_output,
                                    &shape_output,
                                    &vector3,
                                    &normalize_node,
                                    &clamp_node,
                                    &saturate_node,
                                    &step_node,
                                    &smoothstep_node,
                                    &noise_node,
                                    &uv,
                                    &object_position,
                                    &part_position,
                                    &normal,
                                    &view_direction,
                                    &mapping_node,
                                    &mapping3d_node,
                                    &fresnel_node,
                                    &remap_node,
                                    &gradient_node,
                                    &fractal_node,
                                    &voronoi_node,
                                    &ramp_node,
                                    &image_node,
                                    &planar_node,
                                    &cylindrical_node,
                                    &normal_map_node,
                                    &bump_node,
                                    &triplanar_node,
                                    &decal_node,
                                    &star_node };
const registry_pack SHADER_PACK = { .name = "shader",
                                    .nodes = nodes,
                                    .node_count = sizeof nodes / sizeof *nodes,
                                    .types = types,
                                    .type_count = sizeof types / sizeof *types,
                                    .enums = enums,
                                    .enum_count = sizeof enums / sizeof *enums };
node *shader_graph_add_output(graph *g, bool shape_graph)
{
    node *n = graph_add_node(g, shape_graph ? &shape_output : &surface_output);
    if (n) {
        shader_graph_init_node(g, n);
    }
    return n;
}

static const shader_ramp_stop default_stops[] = { { 0, { 0, 0, 0, 1 } },
                                                  { 1, { 1, 1, 1, 1 } } };
int shader_node_ramp_get(const node *n, shader_ramp_stop out[SHADER_RAMP_MAX])
{
    if (n->desc != &ramp_node || n->input_count != 3) {
        return 0;
    }
    const pin_spec *pin = &n->input_pins[1];
    if (pin->type != PIN_STRUCT || pin->container != PIN_CONTAINER_ARRAY ||
        !pin->type_name || strcmp(pin->type_name, "ShaderRampStop")) {
        return 0;
    }
    const pin_array *array = pin->default_value.ptr;
    if (!array) {
        memcpy(out, default_stops, sizeof default_stops);
        return 2;
    }
    if (array->element_type != PIN_STRUCT || !array->type_name ||
        strcmp(array->type_name, "ShaderRampStop") || array->count < 2 ||
        array->count > SHADER_RAMP_MAX) {
        return 0;
    }
    for (int i = 0; i < array->count; i++) {
        pin_value element = pin_array_get(array, i);
        const pin_value *value = &element;
        if (value->type != PIN_STRUCT || !value->ptr || !value->type_name ||
            strcmp(value->type_name, "ShaderRampStop")) {
            return 0;
        }
        memcpy(&out[i], value->ptr, sizeof out[i]);
    }
    return array->count;
}
bool shader_node_ramp_set(graph *g, node *n, const shader_ramp_stop *stops, int count)
{
    if (n->desc != &ramp_node || !shader_ramp_valid(stops, count)) {
        return false;
    }
    pin_value items[SHADER_RAMP_MAX];
    for (int i = 0; i < count; i++) {
        items[i] = (pin_value){ .type = PIN_STRUCT,
                                .type_name = "ShaderRampStop",
                                .ptr = (void *)&stops[i] };
    }
    pin_array array = { .element_type = PIN_STRUCT,
                        .type_name = "ShaderRampStop",
                        .count = count,
                        .capacity = count,
                        .items = items };
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = "ShaderRampStop",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = &array };
    return graph_set_pin_default(g, n, 0, 1, &value) == 0;
}
bool shader_graph_init_node(graph *g, node *n)
{
    if (n->desc == &planar_node || n->desc == &cylindrical_node) {
        float scale[2] = { 1, 1 };
        pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = scale };
        return graph_set_pin_default(g, n, 0, 1, &value) == 0;
    }
    if (n->desc == &normal_map_node || n->desc == &decal_node) {
        float value3[3] = { .5f, .5f, n->desc == &normal_map_node ? 1 : .05f };
        pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = value3 };
        return graph_set_pin_default(g, n, 0, n->desc == &normal_map_node ? 0 : 1, &value) ==
               0;
    }
    if (n->desc == &surface_output) {
        const int vectors[] = { 0, 1, 4 };
        for (int j = 0; j < 3; j++) {
            int i = vectors[j];
            float color[3] = { i == 0, i == 0, i == 0 };
            pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = color };
            if (graph_set_pin_default(g, n, 0, i, &value)) {
                return false;
            }
        }
        return true;
    }
    if (n->desc == &ramp_node) {
        return shader_node_ramp_set(g, n, default_stops, 2);
    }
    if (n->desc == &mapping_node || n->desc == &mapping3d_node) {
        float ones[3] = { 1, 1, 1 };
        pin_value value = { .type = PIN_STRUCT,
                            .type_name = n->desc == &mapping_node ? "ShaderVec2" : "ShaderVec3",
                            .ptr = ones };
        return graph_set_pin_default(g, n, 0, 3, &value) == 0;
    }
    if (n->desc == &remap_node) {
        float range[2] = { 0, 1 };
        pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = range };
        return graph_set_pin_default(g, n, 0, 1, &value) == 0 &&
               graph_set_pin_default(g, n, 0, 2, &value) == 0;
    }
    if (strcmp(n->desc->name, "Shader Parameter")) {
        return true;
    }
    bool used[SHADER_PARAMETER_MAX] = { 0 };
    int count = g->owner ? acount(g->owner->graphs) : 1;
    for (int i = 0; i < count; i++) {
        const graph *current = g->owner ? g->owner->graphs[i] : g;
        for (int j = 0; j < acount(current->nodes); j++) {
            const node *other = current->nodes[j];
            if (other != n && !strcmp(other->desc->name, "Shader Parameter") &&
                other->reference_id < SHADER_PARAMETER_MAX) {
                used[other->reference_id] = true;
            }
        }
    }
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        if (used[i]) {
            continue;
        }
        char name[32];
        snprintf(name, sizeof name, "Parameter %d", i + 1);
        return graph_set_node_reference(g, n, (uint64_t)i, name) == 0;
    }
    return false;
}
