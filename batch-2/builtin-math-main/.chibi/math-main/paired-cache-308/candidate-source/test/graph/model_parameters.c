#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/modeling/model_compile.h"
#include "render/shader_catalog.h"
#include "render/shader_parameters.h"
#include "runtime/component_catalog.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static behavior_asset *model(const char *name)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    return asset;
}
static void scalar(graph *g, node *n, int pin, float value)
{
    pin_value v = { .type = PIN_F32, .f = value };
    assert(!graph_set_pin_default(g, n, false, pin, &v));
}
static void vector(graph *g, node *n, int pin, const float xyz[3])
{
    pin_value v = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = (void *)xyz };
    assert(!graph_set_pin_default(g, n, false, pin, &v));
}
static void connect(graph *g, node *from, node *to, int pin)
{
    assert(!graph_connect_data(g, from, 0, to, pin));
}
static node *parameter(graph *g, const char *name, float value, float minimum, float maximum)
{
    node *n = graph_add_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, n, 0, name));
    scalar(g, n, 0, value);
    scalar(g, n, 1, minimum);
    scalar(g, n, 2, maximum);
    return n;
}
static node *vector_parameter(
    graph *g,
    const char *name,
    const float value[3],
    const float minimum[3],
    const float maximum[3]
)
{
    node *n = graph_add_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, n, 0, name));
    assert(!graph_set_pin_type(g, n, false, 0, PIN_STRUCT, "ShaderVec3"));
    vector(g, n, 0, value);
    vector(g, n, 1, minimum);
    vector(g, n, 2, maximum);
    return n;
}
static void publish(graph *g, node *source)
{
    connect(g, source, model_graph_add_output(g), 0);
}
static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < .0001f;
}
static int slot(const model_program *program, const node *parameter)
{
    for (int i = 0; i < acount(program->parameters); i++) {
        if (!program->parameters[i].hidden &&
            program->parameters[i].node == parameter->id) {
            return program->parameters[i].slot;
        }
    }
    assert(false);
    return -1;
}
static float sample(const model_program *program, const float payload[32][4], const float point[3])
{
    shader_sample_context context = { 0 };
    memcpy(context.object_position, point, 3 * sizeof(float));
    float values[SHADER_OUTPUT_MAX][4];
    shader_error error = { 0 };
    assert(shader_graph_eval_outputs(&program->shader, &context, payload, values, &error));
    return values[0][0];
}
static bool contains(const model_bounds *bounds, const float point[3])
{
    bool inside = !bounds->empty;
    for (int i = 0; i < 3; i++) {
        inside = inside && point[i] >= bounds->min[i] - .00001f &&
                 point[i] <= bounds->max[i] + .00001f;
    }
    return inside;
}
TEST_CASE(transformed_sphere)
{
    behavior_asset *asset = model("DynamicEllipsoid");
    graph *g = asset->graph;
    node *radius = parameter(g, "Radius", 1, .5f, 3);
    node *offset = vector_parameter(
        g,
        "Offset",
        (float[3]){ 0, 0, 0 },
        (float[3]){ -10, -10, -10 },
        (float[3]){ 10, 10, 10 }
    );
    node *factors = vector_parameter(
        g,
        "Scale",
        (float[3]){ 1, 1, 1 },
        (float[3]){ .25f, .25f, .25f },
        (float[3]){ 3, 3, 3 }
    );
    node *angles = vector_parameter(
        g,
        "Rotation",
        (float[3]){ 0, 0, 0 },
        (float[3]){ -3.2f, -3.2f, -3.2f },
        (float[3]){ 3.2f, 3.2f, 3.2f }
    );
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    connect(g, radius, sphere, 0);
    node *scale = graph_add_node(g, &NODE_ModelScale);
    connect(g, sphere, scale, 0);
    connect(g, factors, scale, 1);
    node *rotate = graph_add_node(g, &NODE_ModelRotate);
    connect(g, scale, rotate, 0);
    connect(g, angles, rotate, 1);
    node *translate = graph_add_node(g, &NODE_ModelTranslate);
    connect(g, rotate, translate, 0);
    connect(g, offset, translate, 1);
    publish(g, translate);
    model_program program;
    model_program_init(&program);
    shader_error error = { 0 };
    REQUIRE(model_compile(asset, &program, &error));
    const shader_node *identity = program.shader.nodes;
    int nodes = acount(program.shader.nodes);
    float incoming[32][4], payload[32][4];
    model_bounds bounds;
    REQUIRE(model_program_parameters(&program, incoming, &error));
    REQUIRE(model_program_prepare(&program, incoming, payload, &bounds, &error));
    REQUIRE(near(sample(&program, payload, (float[3]){ 0, 0, 0 }), -1));
    int r = slot(&program, radius), o = slot(&program, offset),
        s = slot(&program, factors), a = slot(&program, angles);
    incoming[r][0] = 2;
    memcpy(incoming[o], (float[3]){ 3, -2, 1 }, 3 * sizeof(float));
    memcpy(incoming[s], (float[3]){ 2, 1, .5f }, 3 * sizeof(float));
    memcpy(incoming[a], (float[3]){ 0, 0, 1.57079632679f }, 3 * sizeof(float));
    REQUIRE(model_program_prepare(&program, incoming, payload, &bounds, &error));
    float supports[][3] = { { 3, 2, 1 },  { 3, -6, 1 }, { 1, -2, 1 },
                            { 5, -2, 1 }, { 3, -2, 0 }, { 3, -2, 2 } };
    for (int i = 0; i < 6; i++) {
        REQUIRE(near(sample(&program, payload, supports[i]), 0));
        REQUIRE(contains(&bounds, supports[i]));
    }
    REQUIRE(near(sample(&program, payload, (float[3]){ 3, 4, 1 }), .5f));
    REQUIRE(near(bounds.min[0], 1));
    REQUIRE(near(bounds.max[1], 2));
    REQUIRE(near(bounds.max[2], 2));
    /* Independent forward construction: sequential plane rotations of points
     * on an ellipsoid, compared with the compiled inverse distance mapping. */
    float general[3] = { .4f, -.6f, .8f };
    memcpy(incoming[a], general, sizeof general);
    for (int change = 0; change < 3; change++) {
        float radius_value = change == 0 ? .5f : change == 1 ? 1 : 3;
        incoming[r][0] = radius_value;
        REQUIRE(model_program_prepare(&program, incoming, payload, &bounds, &error));
        for (int direction = 0; direction < 12; direction++) {
            double theta = direction * 3.141592653589793 / 6;
            double x = radius_value * 2 * cos(theta),
                   y = radius_value * sin(theta), z = 0;
            double py = cos(general[0]) * y - sin(general[0]) * z;
            double pz = sin(general[0]) * y + cos(general[0]) * z;
            y = py;
            z = pz;
            double px = cos(general[1]) * x + sin(general[1]) * z;
            pz = -sin(general[1]) * x + cos(general[1]) * z;
            x = px;
            z = pz;
            px = cos(general[2]) * x - sin(general[2]) * y;
            py = sin(general[2]) * x + cos(general[2]) * y;
            float point[3] = { (float)px + 3, (float)py - 2, (float)z + 1 };
            REQUIRE(near(sample(&program, payload, point), 0));
            REQUIRE(contains(&bounds, point));
        }
    }
    /* Returning to defaults must replace every derived transform slot. */
    REQUIRE(model_program_prepare(&program, NULL, payload, &bounds, &error));
    REQUIRE(near(sample(&program, payload, (float[3]){ 0, 0, 0 }), -1));
    REQUIRE(near(sample(&program, payload, (float[3]){ 4, 0, 0 }), 3));
    REQUIRE(near(bounds.min[0], -1));
    REQUIRE(near(bounds.max[1], 1));
    REQUIRE(program.shader.nodes == identity && acount(program.shader.nodes) == nodes);
    float previous[32][4];
    memcpy(previous, payload, sizeof previous);
    model_bounds previous_bounds = bounds;
    incoming[r][0] = 4;
    REQUIRE(!model_program_prepare(&program, incoming, payload, &bounds, &error));
    REQUIRE(error.node == radius->id);
    REQUIRE(!memcmp(previous, payload, sizeof previous));
    REQUIRE(!memcmp(&previous_bounds, &bounds, sizeof bounds));
    REQUIRE(program.shader.nodes == identity);
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}
TEST_CASE(changing_intersection)
{
    behavior_asset *asset = model("DynamicIntersection");
    graph *g = asset->graph;
    node *radius = parameter(g, "Radius", 1, .5f, 3);
    node *a = graph_add_node(g, &NODE_ModelSphere),
         *b = graph_add_node(g, &NODE_ModelSphere);
    connect(g, radius, a, 0);
    connect(g, radius, b, 0);
    node *translated = graph_add_node(g, &NODE_ModelTranslate);
    vector(g, translated, 1, (float[3]){ 3, 0, 0 });
    connect(g, b, translated, 0);
    node *intersection = graph_add_node(g, &NODE_ModelIntersection);
    connect(g, a, intersection, 0);
    connect(g, translated, intersection, 1);
    publish(g, intersection);
    model_program program;
    model_program_init(&program);
    shader_error error = { 0 };
    REQUIRE(model_compile(asset, &program, &error));
    float incoming[32][4], payload[32][4];
    model_bounds bounds;
    REQUIRE(model_program_parameters(&program, incoming, &error));
    REQUIRE(model_program_prepare(&program, incoming, payload, &bounds, &error));
    REQUIRE(bounds.empty);
    REQUIRE(near(sample(&program, payload, (float[3]){ 1.5f, 0, 0 }), .5f));
    incoming[slot(&program, radius)][0] = 2;
    REQUIRE(model_program_prepare(&program, incoming, payload, &bounds, &error));
    REQUIRE(contains(&bounds, (float[3]){ 1.5f, 0, 0 }));
    REQUIRE(near(sample(&program, payload, (float[3]){ 1.5f, 0, 0 }), -.5f));
    REQUIRE(model_program_prepare(&program, NULL, payload, &bounds, &error));
    REQUIRE(bounds.empty);
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}
TEST_CASE(invalid_derived_dimensions)
{
    behavior_asset *asset = model("DynamicRoundedBox");
    graph *g = asset->graph;
    node *half = vector_parameter(
        g,
        "Half Size",
        (float[3]){ 1, 1, 1 },
        (float[3]){ .1f, .1f, .1f },
        (float[3]){ 4, 4, 4 }
    );
    node *radius = parameter(g, "Rounding", .2f, 0, 3);
    node *box = graph_add_node(g, &NODE_ModelRoundedBox);
    connect(g, half, box, 0);
    connect(g, radius, box, 1);
    publish(g, box);
    model_program program;
    model_program_init(&program);
    shader_error error = { 0 };
    REQUIRE(model_compile(asset, &program, &error));
    float incoming[32][4], payload[32][4], previous[32][4];
    model_bounds bounds;
    REQUIRE(model_program_parameters(&program, incoming, &error));
    REQUIRE(model_program_prepare(&program, incoming, payload, &bounds, &error));
    memcpy(previous, payload, sizeof previous);
    model_bounds previous_bounds = bounds;
    memcpy(incoming[slot(&program, half)], (float[3]){ .1f, .1f, .1f }, 3 * sizeof(float));
    REQUIRE(!model_program_prepare(&program, incoming, payload, &bounds, &error));
    REQUIRE(error.node == box->id);
    REQUIRE(!memcmp(previous, payload, sizeof previous));
    REQUIRE(!memcmp(&previous_bounds, &bounds, sizeof bounds));
    REQUIRE(near(sample(&program, payload, (float[3]){ 1, 0, 0 }), 0));
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}
TEST_CASE(runtime_snapshots)
{
    behavior_asset *asset = model("RuntimeParameterCacheStudy");
    graph *g = asset->graph;
    node *radius = parameter(g, "Radius", 1, .5f, 8);
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    connect(g, radius, sphere, 0);
    publish(g, sphere);
    shader_error error = { 0 };
    REQUIRE(render_shader_upsert(asset, &error));
    int id = render_shader_find(asset->name);
    unsigned revision = render_shader_revision();
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    enum
    {
        COUNT = 48
    };
    ecs_entity entities[COUNT];
    char text[COUNT][64];
    float payload[32][4], min[3], max[3];
    bool empty;
    render_shader_model_stats_reset();
    for (int i = 0; i < COUNT; i++) {
        entities[i] = ecs_world_create_entity(world);
        snprintf(text[i], sizeof text[i], "Radius %.8g\n", 1 + i / 16.0);
        Shader binding = { .shape = asset->name, .parameters = text[i] };
        REQUIRE(
            ecs_world_record_add(world, entities[i], COMPONENT_Shader.id, &binding, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(render_shader_parameters_validate(id, text[i], &error));
        REQUIRE(
            render_shader_model_snapshot(id, world, entities[i], text[i], payload, min, max, &empty, &error)
        );
        REQUIRE(near(max[0], 1 + i / 16.0f));
    }
    render_model_stats first = render_shader_model_stats();
    REQUIRE(first.compilations == 0 && first.bounds_updates == COUNT);
    for (int i = 0; i < COUNT; i++) {
        REQUIRE(
            render_shader_model_snapshot(id, world, entities[i], text[i], payload, min, max, &empty, &error)
        );
        REQUIRE(near(max[2], 1 + i / 16.0f));
    }
    render_model_stats second = render_shader_model_stats();
    REQUIRE(second.bounds_updates == first.bounds_updates);
    REQUIRE(second.cache_hits >= first.cache_hits + COUNT);
    Transform pose = { 100, -50 };
    REQUIRE(ecs_world_record_add(world, entities[0], COMPONENT_Transform.id, &pose, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(
        render_shader_model_snapshot(id, world, entities[0], text[0], payload, min, max, &empty, &error)
    );
    REQUIRE(near(max[0], 1));
    REQUIRE(render_shader_model_stats().bounds_updates == second.bounds_updates);

    float previous_snapshot[32][4], previous_min[3], previous_max[3];
    memcpy(previous_snapshot, payload, sizeof previous_snapshot);
    memcpy(previous_min, min, sizeof previous_min);
    memcpy(previous_max, max, sizeof previous_max);
    /* Preparation refuses a radius past its range, so no snapshot reads it. */
    REQUIRE(!render_shader_parameters_validate(id, "Radius 100\n", &error));
    REQUIRE(error.node == radius->id);
    REQUIRE(
        !render_shader_model_snapshot(id, world, entities[1], "Radius 100\n", payload, min, max, &empty, &error)
    );
    REQUIRE(!memcmp(previous_snapshot, payload, sizeof previous_snapshot));
    REQUIRE(!memcmp(previous_min, min, sizeof previous_min));
    REQUIRE(!memcmp(previous_max, max, sizeof previous_max));
    REQUIRE(
        render_shader_model_snapshot(id, world, entities[1], text[1], payload, min, max, &empty, &error)
    );
    REQUIRE(near(max[0], 1.0625f));
    REQUIRE(render_shader_model_stats().bounds_updates == second.bounds_updates);

    pin_value inputs[3] = {
        { .type = PIN_HANDLE,
          .type_name = "ecs.entity",
          .u64 = ecs_entity_to_u64(entities[0]) },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "Radius",
                                       .length = sizeof("Radius") - 1 } },
        { .type = PIN_F32, .f = 2.5f }
    };
    REQUIRE(!RENDER_SHAPE_PARAMETER_CALL.invoke(world, inputs, NULL));
    REQUIRE(
        render_shader_model_snapshot(id, world, entities[0], text[0], payload, min, max, &empty, &error)
    );
    REQUIRE(near(max[0], 2.5f));
    float previous[32][4];
    memcpy(previous, payload, sizeof previous);
    uint64_t hash = render_shader_overrides_hash(world);
    render_model_stats before_invalid = render_shader_model_stats();
    const pin_string *valid_name = inputs[1].text;
    const char nul_name[] = "Radius\0shadow";
    const pin_string invalid_name = { nul_name, sizeof nul_name - 1 };
    inputs[1].text = &invalid_name;
    const char *invalid_text = RENDER_SHAPE_PARAMETER_CALL.invoke(world, inputs, NULL);
    REQUIRE(invalid_text && !strcmp(invalid_text, "String contains embedded NUL"));
    REQUIRE(render_shader_overrides_hash(world) == hash);
    inputs[1].text = valid_name;
    inputs[2].f = 100;
    REQUIRE(RENDER_SHAPE_PARAMETER_CALL.invoke(world, inputs, NULL));
    REQUIRE(render_shader_overrides_hash(world) == hash);
    REQUIRE(
        render_shader_model_snapshot(id, world, entities[0], text[0], payload, min, max, &empty, &error)
    );
    REQUIRE(!memcmp(previous, payload, sizeof previous));
    REQUIRE(near(max[0], 2.5f));
    REQUIRE(render_shader_model_stats().bounds_updates == before_invalid.bounds_updates);
    REQUIRE(render_shader_model_stats().compilations == 0);
    REQUIRE(render_shader_revision() == revision);

    ecs_entity old = entities[0];
    REQUIRE(ecs_world_destroy_entity(world, old) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    ecs_entity reused = ecs_world_create_entity(world);
    REQUIRE(reused.slot == old.slot && ecs_entity_to_u64(reused) != ecs_entity_to_u64(old));
    Shader binding = { .shape = asset->name, .parameters = "Radius 6\n" };
    REQUIRE(render_shader_parameters_validate(id, binding.parameters, &error));
    REQUIRE(ecs_world_record_add(world, reused, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(
        render_shader_model_snapshot(id, world, reused, binding.parameters, payload, min, max, &empty, &error)
    );
    REQUIRE(near(max[0], 6));
    render_shader_clear_world(world);
    /* This oldest variant is absent from the bounded shared cache. A cleared
     * caller must prepare it again rather than reuse the old entity entry. */
    render_model_stats before_clear_probe = render_shader_model_stats();
    REQUIRE(
        render_shader_model_snapshot(id, world, entities[1], text[1], payload, min, max, &empty, &error)
    );
    REQUIRE(near(max[0], 1.0625f));
    REQUIRE(render_shader_model_stats().bounds_updates == before_clear_probe.bounds_updates + 1);
    render_shader_clear_world(world);
    ecs_world_free(world);
    behavior_asset_free(asset);
    return true;
}
TEST_CASE(scoped_runtime_identity)
{
    behavior_asset *base = model("ScopedParameterDefinition"),
                   *parent = model("ScopedParameterPair");
    node *radius = parameter(base->graph, "Radius", 1, .5f, 8);
    node *sphere = graph_add_node(base->graph, &NODE_ModelSphere);
    connect(base->graph, radius, sphere, 0);
    publish(base->graph, sphere);
    graph *g = parent->graph;
    node *pair = graph_add_node(g, &NODE_ModelUnion);
    node *instances[2];
    for (int i = 0; i < 2; i++) {
        node *instance = graph_add_node(g, &NODE_ModelInstance);
        instances[i] = instance;
        instance->reference_asset = graph_own_str(g, base->name, strlen(base->name));
        pin_spec override = { .name = "Radius",
                              .type = PIN_F32,
                              .declaration_id = radius->id,
                              .default_value = { .type = PIN_F32, .f = 2 + i } };
        REQUIRE(graph_add_pin(g, instance, false, &override) == 0);
        node *translated = graph_add_node(g, &NODE_ModelTranslate);
        vector(g, translated, 1, (float[3]){ i ? 10 : -10, 0, 0 });
        connect(g, instance, translated, 0);
        connect(g, translated, pair, i);
    }
    publish(g, pair);
    const behavior_asset *catalog[] = { base, parent };
    shader_error error = { 0 };
    model_program metadata;
    model_program_init(&metadata);
    REQUIRE(model_compile_with_catalog(parent, catalog, 2, &metadata, &error));
    REQUIRE(render_shader_models_upsert(catalog, 2, &error));
    int id = render_shader_find(parent->name);
    ecs_world *world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &COMPONENT_Shader, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    Shader binding = { .shape = parent->name };
    REQUIRE(ecs_world_record_add(world, entity, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    for (int i = 0; i < 2; i++) {
        char suffix[32];
        snprintf(
            suffix,
            sizeof suffix,
            "/r%llu",
            (unsigned long long)instances[i]->id
        );
        const char *name = NULL;
        for (int j = 0; j < acount(metadata.parameters); j++) {
            const model_parameter *p = &metadata.parameters[j];
            size_t length = strlen(p->scope), tail = strlen(suffix);
            if (!p->hidden && p->node == radius->id && !strcmp(p->asset, base->name) &&
                length >= tail && !strcmp(p->scope + length - tail, suffix)) {
                REQUIRE(!name);
                name = p->name;
            }
        }
        REQUIRE(name);
        pin_value inputs[3] = {
            { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = ecs_entity_to_u64(entity) },
            { .type = PIN_STR,
              .text = &(const pin_string){ .bytes = name,
                                           .length = (uint32_t)strlen(name) } },
            { .type = PIN_F32, .f = 4 + i }
        };
        const char *failure = RENDER_SHAPE_PARAMETER_CALL.invoke(world, inputs, NULL);
        if (failure) {
            fprintf(stderr, "Set Shape Parameter '%s': %s\n", name, failure);
        }
        REQUIRE(!failure);
    }
    model_program_free(&metadata);
    REQUIRE(!graph_set_node_reference(base->graph, radius, 0, "Size"));
    REQUIRE(render_shader_models_upsert(catalog, 2, &error));
    float payload[32][4], min[3], max[3], values[SHADER_OUTPUT_MAX][4];
    bool empty;
    REQUIRE(
        render_shader_model_snapshot(id, world, entity, NULL, payload, min, max, &empty, &error)
    );
    for (int i = 0; i < 2; i++) {
        shader_sample_context context = { .object_position = { i ? 10 : -10, 0, 0 } };
        REQUIRE(render_shader_sample(id, &context, payload, values, &error));
        REQUIRE(near(values[0][0], -4 - i));
    }
    render_shader_clear_world(world);
    ecs_world_free(world);
    behavior_asset_free(base);
    behavior_asset_free(parent);
    return true;
}
TEST_CASE(nested_instance_parameters)
{
    behavior_asset *leaf = model("NestedParameterLeaf"),
                   *wrapper = model("NestedParameterWrapper"),
                   *parent = model("NestedParameterPair");
    node *radius = parameter(leaf->graph, "Radius", 1, .5f, 8);
    node *sphere = graph_add_node(leaf->graph, &NODE_ModelSphere);
    connect(leaf->graph, radius, sphere, 0);
    publish(leaf->graph, sphere);
    node *inner = graph_add_node(wrapper->graph, &NODE_ModelInstance);
    inner->reference_asset = graph_own_str(wrapper->graph, leaf->name, strlen(leaf->name));
    pin_spec override = { .name = "Radius",
                          .type = PIN_F32,
                          .declaration_id = radius->id,
                          .default_value = { .type = PIN_F32, .f = 2 } };
    REQUIRE(graph_add_pin(wrapper->graph, inner, false, &override) == 0);
    publish(wrapper->graph, inner);
    graph *g = parent->graph;
    node *pair = graph_add_node(g, &NODE_ModelUnion);
    for (int i = 0; i < 2; i++) {
        node *outer = graph_add_node(g, &NODE_ModelInstance);
        outer->reference_asset = graph_own_str(g, wrapper->name, strlen(wrapper->name));
        node *translated = graph_add_node(g, &NODE_ModelTranslate);
        vector(g, translated, 1, (float[3]){ i ? 10 : -10, 0, 0 });
        connect(g, outer, translated, 0);
        connect(g, translated, pair, i);
    }
    publish(g, pair);
    const behavior_asset *catalog[] = { leaf, wrapper, parent };
    model_program program;
    model_program_init(&program);
    shader_error error = { 0 };
    REQUIRE(model_compile_with_catalog(parent, catalog, 3, &program, &error));
    REQUIRE(acount(program.parameters) == 2);
    const model_parameter *a = &program.parameters[0], *b = &program.parameters[1];
    REQUIRE(a->node == radius->id && b->node == radius->id);
    REQUIRE(!strcmp(a->asset, leaf->name) && !strcmp(b->asset, leaf->name));
    REQUIRE(strcmp(a->scope, b->scope) && strcmp(a->name, b->name));
    char identities[2][128];
    for (int i = 0; i < 2; i++) {
        REQUIRE(strlen(program.parameters[i].scope) < sizeof identities[i]);
        strcpy(identities[i], program.parameters[i].scope);
    }
    float incoming[32][4], payload[32][4];
    model_bounds bounds;
    REQUIRE(model_program_parameters(&program, incoming, &error));
    incoming[a->slot][0] = 3;
    incoming[b->slot][0] = 4;
    REQUIRE(model_program_prepare(&program, incoming, payload, &bounds, &error));
    REQUIRE(near(sample(&program, payload, (float[3]){ -10, 0, 0 }), -3));
    REQUIRE(near(sample(&program, payload, (float[3]){ 10, 0, 0 }), -4));
    REQUIRE(!graph_set_node_reference(leaf->graph, radius, 0, "Size"));
    REQUIRE(model_compile_with_catalog(parent, catalog, 3, &program, &error));
    REQUIRE(acount(program.parameters) == 2);
    for (int i = 0; i < 2; i++) {
        REQUIRE(!strcmp(identities[i], program.parameters[i].scope));
    }
    REQUIRE(model_program_prepare(&program, incoming, payload, &bounds, &error));
    REQUIRE(near(sample(&program, payload, (float[3]){ -10, 0, 0 }), -3));
    REQUIRE(near(sample(&program, payload, (float[3]){ 10, 0, 0 }), -4));
    model_program_free(&program);
    behavior_asset_free(leaf);
    behavior_asset_free(wrapper);
    behavior_asset_free(parent);
    return true;
}
static void teardown()
{
    authoring_shader_catalog_clear();
}

void test_model_parameters()
{
    test_graph_packs_install();
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(transformed_sphere);
    RUN_TEST_CASE(changing_intersection);
    RUN_TEST_CASE(invalid_derived_dimensions);
    RUN_TEST_CASE(runtime_snapshots);
    RUN_TEST_CASE(scoped_runtime_identity);
    RUN_TEST_CASE(nested_instance_parameters);
    pu_clear_setup();
    test_graph_packs_restore();
}
