#include "graph/model/behavior_asset.h"
#include "graph/shader/shader_graph.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FLOAT_PIN(label)                                                       \
    {                                                                          \
        .name = label, .type = PIN_F32, .default_value = {.type = PIN_F32 }    \
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
    { .name = "x", .type = PIN_F32, .type_name = "float", .offset = 0, .size = 4 },
    { .name = "y", .type = PIN_F32, .type_name = "float", .offset = 4, .size = 4 },
    { .name = "z", .type = PIN_F32, .type_name = "float", .offset = 8, .size = 4 },
    { .name = "w", .type = PIN_F32, .type_name = "float", .offset = 12, .size = 4 },
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
static const type_desc *const types[] = { &vec2_type, &vec3_type, &vec4_type };
static pin_spec scalar_out[] = { FLOAT_PIN("Value") };
static pin_spec any_out[] = { ANY_PIN("Value") };
static pin_spec position_out[] = { VECTOR_PIN("Position", 2) };
static pin_spec vector2_out[] = { VECTOR_PIN("Value", 2) };
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
static pin_spec vector4_in[] = { FLOAT_PIN("X"),
                                 FLOAT_PIN("Y"),
                                 FLOAT_PIN("Z"),
                                 FLOAT_PIN("W") };
static pin_spec material[] = {
    VECTOR_PIN("Scene", 4),         VECTOR_PIN("Emission", 4),
    VECTOR_PIN("Albedo", 4),        VECTOR_PIN("Absorption", 4),
    VECTOR_PIN("Border Scene", 4),  VECTOR_PIN("Border Emission", 4),
    VECTOR_PIN("Border Albedo", 4), VECTOR_PIN("Border Absorption", 4)
};
static pin_spec shape[] = { FLOAT_PIN("Distance") };
#define NODE(symbol, label, count, pins, outs)                                 \
    static node_desc symbol = { .name = "Shader " label,                       \
                                .input_count = count,                          \
                                .inputs = pins,                                \
                                .output_count = 1,                             \
                                .outputs = outs,                               \
                                .category = "Shader",                          \
                                .display_class = NODE_DISPLAY_PURE }
NODE(constant, "Constant", 0, NULL, any_out);
NODE(parameter, "Parameter", 0, NULL, any_out);
NODE(position, "Position", 0, NULL, position_out);
NODE(world_position, "WorldPosition", 0, NULL, position_out);
NODE(time_node, "Time", 0, NULL, scalar_out);
NODE(add, "Add", 2, binary, any_out);
NODE(subtract, "Subtract", 2, binary, any_out);
NODE(multiply, "Multiply", 2, binary, any_out);
NODE(divide, "Divide", 2, binary, any_out);
NODE(minimum, "Min", 2, binary, any_out);
NODE(maximum, "Max", 2, binary, any_out);
NODE(absolute, "Abs", 1, unary, any_out);
NODE(sine, "Sin", 1, unary, any_out);
NODE(cosine, "Cos", 1, unary, any_out);
NODE(fraction, "Fract", 1, unary, any_out);
NODE(length_node, "Length", 1, unary, scalar_out);
NODE(dot, "Dot", 2, binary, scalar_out);
NODE(mix_node, "Mix", 3, ternary, any_out);
NODE(select_node, "Select", 3, selection, any_out);
NODE(circle_node, "Circle", 2, circle, scalar_out);
NODE(box_node, "Box", 2, box, scalar_out);
NODE(union_node, "Union", 2, sdf_pair, scalar_out);
NODE(intersection, "Intersection", 2, sdf_pair, scalar_out);
NODE(difference, "Difference", 2, sdf_pair, scalar_out);
NODE(smooth_node, "SmoothUnion", 3, smooth, scalar_out);
NODE(vector2, "Vector2", 2, vector2_in, vector2_out);
NODE(vector4, "Vector4", 4, vector4_in, vector4_out);
NODE(x_node, "X", 1, unary, scalar_out);
NODE(y_node, "Y", 1, unary, scalar_out);
NODE(z_node, "Z", 1, unary, scalar_out);
NODE(w_node, "W", 1, unary, scalar_out);
static node_desc material_output = { .name = "Shader Material Output",
                                     .input_count = 8,
                                     .inputs = material,
                                     .category = "Shader",
                                     .display_class = NODE_DISPLAY_FUNCTION };
static node_desc shape_output = { .name = "Shader Shape Output",
                                  .input_count = 1,
                                  .inputs = shape,
                                  .category = "Shader",
                                  .display_class = NODE_DISPLAY_FUNCTION };
static node_desc *const nodes[] = {
    &constant,    &parameter,    &position,       &time_node,
    &add,         &subtract,     &multiply,       &divide,
    &minimum,     &maximum,      &absolute,       &sine,
    &cosine,      &fraction,     &length_node,    &dot,
    &mix_node,    &select_node,  &circle_node,    &box_node,
    &union_node,  &intersection, &difference,     &smooth_node,
    &vector2,     &vector4,      &x_node,         &y_node,
    &z_node,      &w_node,       &world_position, &material_output,
    &shape_output
};
const registry_pack SHADER_PACK = { .name = "shader",
                                    .nodes = nodes,
                                    .node_count = sizeof nodes / sizeof *nodes,
                                    .types = types,
                                    .type_count = 3 };
node *shader_graph_add_output(graph *g, bool shape_graph)
{
    node *n = graph_add_node(g, shape_graph ? &shape_output : &material_output);
    if (shape_graph) {
        return n;
    }
    for (int i = 0; i < 8; i++) {
        float *value = graph_own_block(g, 4 * sizeof(float));
        value[0] = value[1] = value[2] = (i == 0 || i == 2 || i == 4 || i == 6) ? 1 : 0;
        value[3] = (i == 0 || i == 2 || i == 4 || i == 6) ? 1 : 0;
        n->input_pins[i].default_value.ptr = value;
    }
    return n;
}

bool shader_graph_init_parameter(graph *g, node *n)
{
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
