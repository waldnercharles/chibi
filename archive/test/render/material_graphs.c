#include "material_graphs.h"

#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/modeling/model_compile.h"
#include "graph/shader/shader_graph.h"
#include "runtime/images/catalog.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

type_catalog_pack test_material_graph_types()
{
    return (type_catalog_pack){ .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
}

static const node_desc *pack_node(const char *name)
{
    for (int i = 0; i < SHADER_PACK.node_count; i++) {
        if (!strcmp(SHADER_PACK.nodes[i]->name, name)) {
            return SHADER_PACK.nodes[i];
        }
    }
    return NULL;
}

static behavior_asset *new_material(const char *name)
{
    behavior_asset *material = behavior_asset_new(name);
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    return material;
}

static bool publish(behavior_asset *material, bool built)
{
    shader_error error = { 0 };
    built = built && render_shader_upsert(material, &error);
    behavior_asset_free(material);
    return built;
}

/* Adds a Shader Parameter named `name` with reference id `slot`, distinct
 * for each parameter of a graph, and a vector if `vector`. */
static node *add_parameter(graph *g, int slot, const char *name, bool vector)
{
    node *parameter = graph_add_node(g, pack_node("Shader Parameter"));
    bool typed = parameter &&
                 !graph_set_node_reference(g, parameter, (uint64_t)slot, name) &&
                 (!vector ||
                  !graph_set_pin_type(g, parameter, 1, 0, PIN_STRUCT, "ShaderVec3"));
    return typed ? parameter : NULL;
}

bool test_register_surface(const char *name, float roughness, float metallic)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    pin_value values[] = { { .type = PIN_F32, .f = roughness },
                           { .type = PIN_F32, .f = metallic } };
    return publish(
        material,
        !graph_set_pin_default(g, output, 0, 2, &values[0]) &&
            !graph_set_pin_default(g, output, 0, 3, &values[1])
    );
}

bool test_register_ramp(const char *name, int count, const float from[4], const float to[4])
{
    assert(count >= 2 && count <= SHADER_RAMP_MAX);
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *ramp = graph_add_node(g, pack_node("Shader Color Ramp"));
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    for (int i = 0; i < count; i++) {
        float t = (float)i / (count - 1);
        stops[i].position = t;
        for (int c = 0; c < 4; c++) {
            stops[i].color[c] = from[c] + (to[c] - from[c]) * t;
        }
    }
    pin_value factor = { .type = PIN_F32, .f = .25f };
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    return publish(
        material,
        ramp && shader_node_ramp_set(g, ramp, stops, count) &&
            !graph_set_pin_default(g, ramp, 0, 0, &factor) &&
            !graph_connect_data(g, ramp, 0, output, 0) &&
            !graph_set_pin_default(g, output, 0, 6, &unlit)
    );
}

bool test_register_coordinate(const char *name, const char *input)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *source = graph_add_node(g, pack_node(input));
    node *x = graph_add_node(g, pack_node("Shader X"));
    node *ramp = graph_add_node(g, pack_node("Shader Color Ramp"));
    shader_ramp_stop stops[] = { { .position = 0, .color = { 0, 0, 0, 1 } },
                                 { .position = 1, .color = { 1, 1, 1, 1 } } };
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    return publish(
        material,
        source && x && ramp && shader_node_ramp_set(g, ramp, stops, 2) &&
            !graph_connect_data(g, source, 0, x, 0) &&
            !graph_connect_data(g, x, 0, ramp, 0) &&
            !graph_connect_data(g, ramp, 0, output, 0) &&
            !graph_set_pin_default(g, output, 0, 6, &unlit)
    );
}

bool test_register_normal_map(const char *name, const float color[3], shader_normal_convention convention)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *map = graph_add_node(g, pack_node("Shader Normal Map"));
    float rgb[3] = { color[0], color[1], color[2] };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = rgb };
    pin_value green = { .type = PIN_I32, .i = convention };
    return publish(
        material,
        map && !graph_set_pin_default(g, map, 0, 0, &value) &&
            !graph_set_pin_default(g, map, 0, 4, &green) &&
            !graph_connect_data(g, map, 0, output, 4)
    );
}

shader_normal_convention test_normal_convention(const char *label)
{
    for (int i = 0; i < SHADER_PACK.enum_count; i++) {
        const enum_desc *conventions = SHADER_PACK.enums[i];
        if (strcmp(conventions->name, "ShaderNormalConvention")) {
            continue;
        }
        for (int j = 0; j < conventions->value_count; j++) {
            if (!strcmp(conventions->values[j].label, label)) {
                return (shader_normal_convention)conventions->values[j].value;
            }
        }
    }
    assert(false);
    return SHADER_NORMAL_DIRECTX;
}

/* Reads every image as one white pixel. */
static bool white_pixel(void *, const char *, shader_image_view *view, char *, size_t)
{
    unsigned char *pixel = xmalloc(4);
    memset(pixel, 255, 4);
    *view = (shader_image_view){ .pixels = pixel, .width = 1, .height = 1 };
    return true;
}

static node *image_node(graph *g, int index)
{
    node *image = graph_add_node(g, pack_node("Shader Image Texture"));
    char key[64];
    snprintf(key, sizeof key, "content/images/test-%d.png", index);
    pin_value value = {
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = key, .length = (uint32_t)strlen(key) }
    };
    assert(image && !graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_IMAGE, &value));
    return image;
}

/* A Color Ramp of white, read a quarter of the way along. */
static node *white_ramp(graph *g)
{
    node *ramp = graph_add_node(g, pack_node("Shader Color Ramp"));
    shader_ramp_stop stops[] = { { .position = 0, .color = { 1, 1, 1, 1 } },
                                 { .position = 1, .color = { 1, 1, 1, 1 } } };
    pin_value factor = { .type = PIN_F32, .f = .25f };
    bool built = ramp && shader_node_ramp_set(g, ramp, stops, 2) &&
                 !graph_set_pin_default(g, ramp, 0, 0, &factor);
    return built ? ramp : NULL;
}

bool test_register_images(const char *name, int count, bool ramp)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *sum = image_node(g, 0);
    bool built = true;
    for (int i = 1; i < count + ramp; i++) {
        node *term = i < count ? image_node(g, i) : white_ramp(g);
        node *add = graph_add_node(g, pack_node("Shader Add"));
        built = built && term && add &&
                !graph_set_pin_type(g, add, 0, 0, PIN_STRUCT, "ShaderVec3") &&
                !graph_set_pin_type(g, add, 0, 1, PIN_STRUCT, "ShaderVec3") &&
                !graph_set_pin_type(g, add, 1, 0, PIN_STRUCT, "ShaderVec3") &&
                !graph_connect_data(g, sum, 0, add, 0) &&
                !graph_connect_data(g, term, 0, add, 1);
        sum = add;
    }
    render_image_loader loader = render_images_loader();
    render_images_set_loader((render_image_loader){ .read = white_pixel });
    built = publish(material, built && !graph_connect_data(g, sum, 0, output, 0));
    render_images_set_loader(loader);
    return built;
}

bool test_register_occlusion(const char *name, float occlusion)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    pin_value value = { .type = PIN_F32, .f = occlusion };
    return publish(material, !graph_set_pin_default(g, output, 0, SHADER_SURFACE_OCCLUSION, &value));
}

bool test_register_alpha(const char *name, shader_alpha_mode mode, float opacity, float cutoff)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    pin_value values[] = { { .type = PIN_BOOL, .b = true },
                           { .type = PIN_I32, .i = mode },
                           { .type = PIN_F32, .f = opacity },
                           { .type = PIN_F32, .f = cutoff } };
    return publish(
        material,
        !graph_set_pin_default(g, output, 0, SHADER_SURFACE_UNLIT, &values[0]) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_ALPHA_MODE, &values[1]) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_OPACITY, &values[2]) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_ALPHA_CUTOFF, &values[3])
    );
}

bool test_register_half_mask(const char *name)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *position = graph_add_node(g, pack_node("Shader ObjectPosition"));
    node *z = graph_add_node(g, pack_node("Shader Z"));
    pin_value values[] = { { .type = PIN_I32, .i = SHADER_ALPHA_MASK },
                           { .type = PIN_F32, .f = 0 } };
    return publish(
        material,
        position && z && !graph_connect_data(g, position, 0, z, 0) &&
            !graph_connect_data(g, z, 0, output, SHADER_SURFACE_OPACITY) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_ALPHA_MODE, &values[0]) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_ALPHA_CUTOFF, &values[1])
    );
}

bool test_register_view_mask(const char *name)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *view = graph_add_node(g, pack_node("Shader ViewDirection"));
    node *x = graph_add_node(g, pack_node("Shader X"));
    node *negate = graph_add_node(g, pack_node("Shader Multiply"));
    pin_value values[] = { { .type = PIN_F32, .f = -1 },
                           { .type = PIN_I32, .i = SHADER_ALPHA_MASK },
                           { .type = PIN_F32, .f = .5f } };
    return publish(
        material,
        view && x && negate && !graph_connect_data(g, view, 0, x, 0) &&
            !graph_connect_data(g, x, 0, negate, 0) &&
            !graph_set_pin_default(g, negate, 0, 1, &values[0]) &&
            !graph_connect_data(g, negate, 0, output, SHADER_SURFACE_OPACITY) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_ALPHA_MODE, &values[1]) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_ALPHA_CUTOFF, &values[2])
    );
}

bool test_register_unknown_opacity(const char *name)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *uv = graph_add_node(g, pack_node("Shader UV"));
    node *u = graph_add_node(g, pack_node("Shader X"));
    node *scales[2] = { graph_add_node(g, pack_node("Shader Multiply")),
                        graph_add_node(g, pack_node("Shader Multiply")) };
    node *fraction = graph_add_node(g, pack_node("Shader Fract"));
    pin_value values[] = { { .type = PIN_BOOL, .b = true },
                           { .type = PIN_I32, .i = SHADER_ALPHA_MASK },
                           { .type = PIN_F32, .f = .5f },
                           { .type = PIN_F32, .f = 1e38f } };
    return publish(
        material,
        uv && u && scales[0] && scales[1] && fraction &&
            !graph_connect_data(g, uv, 0, u, 0) &&
            !graph_connect_data(g, u, 0, scales[0], 0) &&
            !graph_set_pin_default(g, scales[0], 0, 1, &values[3]) &&
            !graph_connect_data(g, scales[0], 0, scales[1], 0) &&
            !graph_set_pin_default(g, scales[1], 0, 1, &values[3]) &&
            !graph_connect_data(g, scales[1], 0, fraction, 0) &&
            !graph_connect_data(g, fraction, 0, output, SHADER_SURFACE_OPACITY) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_UNLIT, &values[0]) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_ALPHA_MODE, &values[1]) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_ALPHA_CUTOFF, &values[2])
    );
}

bool test_register_relief(const char *name, test_relief relief)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    float base[3] = { .6f, .6f, .6f },
          normal[3] = { relief.tilt[0], 1, relief.tilt[1] };
    pin_value values[] = {
        { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = base },
        { .type = PIN_F32, .f = relief.roughness },
        { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = normal },
        { .type = PIN_F32, .f = relief.height },
        { .type = PIN_F32, .f = relief.slope }
    };
    bool built = !graph_set_pin_default(g, output, 0, SHADER_SURFACE_BASE_COLOR, &values[0]) &&
                 !graph_set_pin_default(g, output, 0, SHADER_SURFACE_ROUGHNESS, &values[1]) &&
                 !graph_set_pin_default(g, output, 0, SHADER_SURFACE_NORMAL, &values[2]);
    if (!relief.slope) {
        return publish(
            material,
            built && !graph_set_pin_default(g, output, 0, SHADER_SURFACE_HEIGHT, &values[3])
        );
    }
    node *position = graph_add_node(g, pack_node("Shader WorldPosition"));
    node *x = graph_add_node(g, pack_node("Shader X"));
    node *scale = graph_add_node(g, pack_node("Shader Multiply"));
    node *offset = graph_add_node(g, pack_node("Shader Add"));
    return publish(
        material,
        built && position && x && scale && offset &&
            !graph_connect_data(g, position, 0, x, 0) &&
            !graph_connect_data(g, x, 0, scale, 0) &&
            !graph_set_pin_default(g, scale, 0, 1, &values[4]) &&
            !graph_connect_data(g, scale, 0, offset, 0) &&
            !graph_set_pin_default(g, offset, 0, 1, &values[3]) &&
            !graph_connect_data(g, offset, 0, output, SHADER_SURFACE_HEIGHT)
    );
}

bool test_register_world_step(const char *name)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *world = graph_add_node(g, pack_node("Shader WorldPosition"));
    node *x = graph_add_node(g, pack_node("Shader X"));
    node *step = graph_add_node(g, pack_node("Shader Step"));
    node *color = graph_add_node(g, pack_node("Shader Vector3"));
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    return publish(
        material,
        world && x && step && color && !graph_connect_data(g, world, 0, x, 0) &&
            !graph_connect_data(g, x, 0, step, 0) &&
            !graph_connect_data(g, step, 0, color, 0) &&
            !graph_connect_data(g, color, 0, output, SHADER_SURFACE_BASE_COLOR) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_UNLIT, &unlit)
    );
}

bool test_register_tint(const char *name, const float tint[3])
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *parameter = add_parameter(g, 0, "Tint", true);
    float color[3] = { tint[0], tint[1], tint[2] };
    pin_value values[] = { { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = color },
                           { .type = PIN_BOOL, .b = true } };
    return publish(
        material,
        parameter && !graph_set_pin_default(g, parameter, 1, 0, &values[0]) &&
            !graph_connect_data(g, parameter, 0, output, SHADER_SURFACE_BASE_COLOR) &&
            !graph_set_pin_default(g, output, 0, SHADER_SURFACE_UNLIT, &values[1])
    );
}

bool test_register_circle_shape(const char *name, float radius)
{
    behavior_asset *shape = behavior_asset_new(name);
    shape->domain = BEHAVIOR_DOMAIN_SHAPE;
    shape->bounds[0] = shape->bounds[1] = radius * 2;
    graph *g = shape->graph;
    node *output = shader_graph_add_output(g, true);
    node *position = graph_add_node(g, pack_node("Shader Position"));
    node *circle = graph_add_node(g, pack_node("Shader Circle"));
    pin_value value = { .type = PIN_F32, .f = radius };
    return publish(
        shape,
        position && circle && !graph_set_pin_default(g, circle, 0, 1, &value) &&
            !graph_connect_data(g, position, 0, circle, 0) &&
            !graph_connect_data(g, circle, 0, output, 0)
    );
}

bool test_register_parameters(const char *name)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    static const char *const names[] = { "Tint", "Glow", "Roughness", "Height" };
    static const int pins[] = { SHADER_SURFACE_BASE_COLOR,
                                SHADER_SURFACE_EMISSION,
                                SHADER_SURFACE_ROUGHNESS,
                                SHADER_SURFACE_HEIGHT };
    bool built = true;
    for (int i = 0; i < 4; i++) {
        node *parameter = add_parameter(g, i, names[i], i < 2);
        built = built && parameter &&
                !graph_connect_data(g, parameter, 0, output, pins[i]);
    }
    return publish(material, built);
}

bool test_register_radius_shape(const char *name, float bounds)
{
    behavior_asset *shape = behavior_asset_new(name);
    shape->domain = BEHAVIOR_DOMAIN_SHAPE;
    shape->bounds[0] = shape->bounds[1] = bounds;
    graph *g = shape->graph;
    node *output = shader_graph_add_output(g, true);
    node *position = graph_add_node(g, pack_node("Shader Position"));
    node *circle = graph_add_node(g, pack_node("Shader Circle"));
    node *radius = add_parameter(g, 0, "Radius", false);
    return publish(
        shape,
        position && circle && radius &&
            !graph_connect_data(g, position, 0, circle, 0) &&
            !graph_connect_data(g, radius, 0, circle, 1) &&
            !graph_connect_data(g, circle, 0, output, 0)
    );
}

bool test_register_model_sphere(const char *name, float radius)
{
    behavior_asset *model = behavior_asset_new(name);
    model->domain = BEHAVIOR_DOMAIN_MODEL;
    node *sphere = graph_add_node(model->graph, &NODE_ModelSphere);
    node *output = model_graph_add_output(model->graph);
    pin_value value = { .type = PIN_F32, .f = radius };
    return publish(
        model,
        !graph_set_pin_default(model->graph, sphere, 0, 0, &value) &&
            !graph_connect_data(model->graph, sphere, 0, output, 0)
    );
}
