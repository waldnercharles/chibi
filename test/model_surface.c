#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "render/arena3d.h"
#include "render/mesh_surface.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < .0001f);
}
static void set_component(
    ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *component,
    const void *value
)
{
    void *current = ecs_component_data(world, entity, component->id);
    if (current) {
        memcpy(current, value, component->element_size);
        return;
    }
    assert(ecs_world_record_add(world, entity, component->id, value, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
}
static node *sphere(behavior_asset *asset, float radius)
{
    node *shape = graph_add_node(asset->graph, &NODE_ModelSphere);
    pin_value value = { .type = PIN_F32, .f = radius };
    assert(graph_set_pin_default(asset->graph, shape, 0, 0, &value) == 0);
    return shape;
}
static behavior_asset *sphere_asset(const char *name, float radius, node **primitive)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    *primitive = sphere(asset, radius);
    node *output = model_graph_add_output(asset->graph);
    assert(graph_connect_data(asset->graph, *primitive, 0, output, 0) == 0);
    return asset;
}
static void vector(graph *g, node *n, int pin, float x, float y, float z)
{
    float components[] = { x, y, z };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = components };
    assert(graph_set_pin_default(g, n, 0, pin, &value) == 0);
}
static behavior_asset *offset_box()
{
    behavior_asset *asset = behavior_asset_new("ModelSurfaceOffsetBox");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    node *box = graph_add_node(g, &NODE_ModelBox);
    vector(g, box, 0, 1, 2, 3);
    node *translate = graph_add_node(g, &NODE_ModelTranslate);
    vector(g, translate, 1, 2, 3, 4);
    assert(graph_connect_data(g, box, 0, translate, 0) == 0);
    node *rotate = graph_add_node(g, &NODE_ModelRotate);
    vector(g, rotate, 1, 0, 0, 1.5707963268f);
    assert(graph_connect_data(g, translate, 0, rotate, 0) == 0);
    node *output = model_graph_add_output(g);
    assert(graph_connect_data(g, rotate, 0, output, 0) == 0);
    return asset;
}
static behavior_asset *empty_intersection()
{
    behavior_asset *asset = behavior_asset_new("ModelSurfaceEmpty");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    node *a = sphere(asset, 1), *b = sphere(asset, 1);
    node *translate = graph_add_node(g, &NODE_ModelTranslate);
    vector(g, translate, 1, 100, 0, 0);
    assert(graph_connect_data(g, b, 0, translate, 0) == 0);
    node *intersection = graph_add_node(g, &NODE_ModelIntersection);
    assert(graph_connect_data(g, a, 0, intersection, 0) == 0);
    assert(graph_connect_data(g, translate, 0, intersection, 1) == 0);
    node *output = model_graph_add_output(g);
    assert(graph_connect_data(g, intersection, 0, output, 0) == 0);
    return asset;
}
static float distance(int id, float x, float y, float z)
{
    shader_sample_context context = { .object_position = { x, y, z } };
    float result[SHADER_OUTPUT_MAX][4];
    shader_error error = { 0 };
    assert(render_shader_sample(id, &context, NULL, result, &error));
    return result[0][0];
}
static void catalog_generation()
{
    node *primitive;
    behavior_asset *asset = sphere_asset("ModelSurfaceGeneration", 3, &primitive);
    shader_error error = { 0 };
    assert(render_shader_upsert(asset, &error));
    int id = render_shader_find(asset->name);
    assert(id && render_shader_is_model(id) && render_shader_is_shape(id));
    unsigned revision = render_shader_revision();
    assert(render_shader_upsert(asset, &error));
    assert(render_shader_revision() == revision);
    pin_value radius = { .type = PIN_F32, .f = -1 };
    assert(graph_set_pin_default(asset->graph, primitive, 0, 0, &radius) == 0);
    assert(!render_shader_upsert(asset, &error));
    assert(error.node == primitive->id && strstr(error.message, "positive"));
    assert(render_shader_revision() == revision);
    assert(render_shader_find(asset->name) == id);
    near(distance(id, 0, 0, 0), -3);
    float min[3], max[3];
    bool empty;
    assert(render_shader_model_bounds(id, NULL, min, max, &empty, &error));
    assert(!empty);
    near(max[1], 3);
    radius.f = 4;
    assert(graph_set_pin_default(asset->graph, primitive, 0, 0, &radius) == 0);
    assert(render_shader_upsert(asset, &error));
    assert(render_shader_find(asset->name) == id);
    near(distance(id, 0, 0, 0), -4);
    behavior_asset *raw = behavior_asset_new(asset->name);
    raw->domain = BEHAVIOR_DOMAIN_SHAPE;
    raw->bounds[0] = raw->bounds[1] = 1;
    shader_graph_add_output(raw->graph, true);
    assert(!render_shader_upsert(raw, &error));
    assert(strstr(error.message, "unique"));
    near(distance(id, 0, 0, 0), -4);
    behavior_asset_free(raw);
    behavior_asset_free(asset);

    raw = behavior_asset_new("ModelSurfaceRawCollision");
    raw->domain = BEHAVIOR_DOMAIN_SHAPE;
    raw->bounds[0] = raw->bounds[1] = 1;
    shader_graph_add_output(raw->graph, true);
    assert(render_shader_upsert(raw, &error));
    id = render_shader_find(raw->name);
    assert(!render_shader_is_model(id));
    asset = sphere_asset(raw->name, 2, &primitive);
    assert(!render_shader_upsert(asset, &error));
    assert(strstr(error.message, "unique"));
    assert(render_shader_find(raw->name) == id && !render_shader_is_model(id));
    behavior_asset_free(asset);
    behavior_asset_free(raw);
}
static behavior_asset *instance_asset(const char *name, const char *target, node **instance)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    *instance = graph_add_node(asset->graph, &NODE_ModelInstance);
    (*instance)->reference_asset = graph_own_str(asset->graph, target, strlen(target));
    node *output = model_graph_add_output(asset->graph);
    assert(graph_connect_data(asset->graph, *instance, 0, output, 0) == 0);
    return asset;
}
static void instance_override()
{
    behavior_asset *asset = behavior_asset_new("ModelOverrideSource");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    node *dimension = graph_add_node(asset->graph, &NODE_ModelParameter);
    assert(graph_set_node_reference(asset->graph, dimension, 0, "Radius") == 0);
    float settings[] = { 2, 1, 5 };
    for (int i = 0; i < 3; i++) {
        pin_value value = { .type = PIN_F32, .f = settings[i] };
        assert(graph_set_pin_default(asset->graph, dimension, 0, i, &value) == 0);
    }
    node *primitive = sphere(asset, 1);
    assert(graph_connect_data(asset->graph, dimension, 0, primitive, 0) == 0);
    node *output = model_graph_add_output(asset->graph);
    assert(graph_connect_data(asset->graph, primitive, 0, output, 0) == 0);
    node *inherited_instance, *override_instance;
    behavior_asset *inherited = instance_asset("ModelOverrideInherited", asset->name, &inherited_instance);
    behavior_asset *overridden = instance_asset("ModelOverrideLocal", asset->name, &override_instance);
    pin_spec override = { .name = "Radius",
                          .type = PIN_F32,
                          .declaration_id = dimension->id,
                          .default_value = { .type = PIN_F32, .f = 4 } };
    assert(graph_add_pin(overridden->graph, override_instance, 0, &override) == 0);
    const behavior_asset *sources[] = { inherited, overridden, asset };
    shader_error error = { 0 };
    assert(render_shader_models_upsert(sources, 3, &error));
    int normal_id = render_shader_find(inherited->name),
        override_id = render_shader_find(overridden->name);
    near(distance(normal_id, 0, 0, 0), -2);
    near(distance(override_id, 0, 0, 0), -4);
    pin_value value = { .type = PIN_F32, .f = 3 };
    assert(graph_set_pin_default(asset->graph, dimension, 0, 0, &value) == 0);
    assert(render_shader_upsert(asset, &error));
    near(distance(normal_id, 0, 0, 0), -3);
    near(distance(override_id, 0, 0, 0), -4);
    assert(render_shader_model_remove(inherited->name, &error));
    assert(render_shader_model_remove(overridden->name, &error));
    assert(render_shader_model_remove(asset->name, &error));
    behavior_asset_free(overridden);
    behavior_asset_free(inherited);
    behavior_asset_free(asset);
}
static void model_dependencies()
{
    node *primitive, *instance_b, *instance_c;
    behavior_asset *a = sphere_asset("ModelDependencyA", 2, &primitive);
    behavior_asset *b = instance_asset("ModelDependencyB", a->name, &instance_b);
    behavior_asset *c = instance_asset("ModelDependencyC", b->name, &instance_c);
    const behavior_asset *sources[] = { c, b, a };
    shader_error error = { 0 };
    assert(render_shader_models_upsert(sources, 3, &error));
    int ids[] = { render_shader_find(a->name),
                  render_shader_find(b->name),
                  render_shader_find(c->name) };
    near(distance(ids[1], 3, 0, 0), 1);
    near(distance(ids[2], 0, 0, 0), -2);
    pin_value radius = { .type = PIN_F32, .f = 3 };
    assert(graph_set_pin_default(a->graph, primitive, 0, 0, &radius) == 0);
    assert(render_shader_upsert(a, &error));
    assert(render_shader_find(a->name) == ids[0]);
    assert(render_shader_find(b->name) == ids[1]);
    assert(render_shader_find(c->name) == ids[2]);
    near(distance(ids[1], 3, 0, 0), 0);
    near(distance(ids[2], 0, 0, 0), -3);
    float min[3], max[3];
    bool empty;
    assert(render_shader_model_bounds(ids[2], NULL, min, max, &empty, &error));
    near(max[0], 3);
    unsigned revision = render_shader_revision();
    assert(!render_shader_model_remove(a->name, &error));
    assert(strstr(error.message, a->name));
    assert(render_shader_revision() == revision);
    near(distance(ids[2], 0, 0, 0), -3);
    node *cycle_instance;
    behavior_asset *cycle = instance_asset(a->name, c->name, &cycle_instance);
    assert(!render_shader_upsert(cycle, &error));
    assert(strstr(error.message, "cycle") || strstr(error.message, "Cycle"));
    assert(render_shader_revision() == revision);
    near(distance(ids[2], 0, 0, 0), -3);
    behavior_asset_free(cycle);
    node *missing_instance;
    behavior_asset *missing = instance_asset("ModelDependencyMissing", "DeletedModel", &missing_instance);
    assert(!render_shader_upsert(missing, &error));
    assert(error.node == missing_instance->id && strstr(error.message, "DeletedModel"));
    assert(!render_shader_find(missing->name));
    behavior_asset_free(missing);
    radius.f = 0;
    assert(graph_set_pin_default(a->graph, primitive, 0, 0, &radius) == 0);
    assert(!render_shader_upsert(a, &error));
    assert(render_shader_revision() == revision);
    near(distance(ids[1], 0, 0, 0), -3);
    assert(render_shader_model_rename("ModelDependencyA", "ModelDependencyRenamed", &error));
    assert(!render_shader_find("ModelDependencyA"));
    assert(render_shader_find("ModelDependencyRenamed") == ids[0]);
    near(distance(ids[2], 0, 0, 0), -3);
    assert(!render_shader_model_remove("ModelDependencyRenamed", &error));
    assert(render_shader_model_remove(c->name, &error));
    assert(!render_shader_find(c->name));
    assert(render_shader_find(b->name) == ids[1]);
    assert(render_shader_model_remove(b->name, &error));
    assert(render_shader_model_remove("ModelDependencyRenamed", &error));
    assert(!render_shader_find("ModelDependencyRenamed"));
    behavior_asset_free(c);
    behavior_asset_free(b);
    behavior_asset_free(a);
}
static void perspective_model_selection()
{
    node *primitive;
    behavior_asset *asset = sphere_asset("PerspectiveModelBounds", 2, &primitive);
    graph *g = asset->graph;
    node *translate = graph_add_node(g, &NODE_ModelTranslate);
    vector(g, translate, 1, 20, 6, 3);
    node *output = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->desc == &NODE_ModelOutput) {
            output = g->nodes[i];
        }
    }
    assert(output);
    assert(graph_connect_data(g, primitive, 0, translate, 0) == 0);
    assert(graph_connect_data(g, translate, 0, output, 0) == 0);
    shader_error error = { 0 };
    assert(render_shader_upsert(asset, &error));
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world);
    GameTransform transform = { -7, 8 };
    GameGeometry3D geometry = { .primitive = GAME_PRIMITIVE_BOX,
                                .width = .25f,
                                .height = .25f,
                                .depth = .25f };
    GameVisualTransform visual = { .scale = 1.5f };
    GameSdfSurface3D surface = { .enabled = true };
    GameShader binding = { .shape = asset->name };
    set_component(world, entity, &GAME_COMPONENT_GameTransform, &transform);
    set_component(world, entity, &GAME_COMPONENT_GameGeometry3D, &geometry);
    set_component(world, entity, &GAME_COMPONENT_GameVisualTransform, &visual);
    set_component(world, entity, &GAME_COMPONENT_GameSdfSurface3D, &surface);
    set_component(world, entity, &GAME_COMPONENT_GameShader, &binding);
    CF_V3 corners[8];
    assert(render_arena_entity_bounds(world, entity, corners));
    CF_V3 minimum = { 20, 6, 9.5f }, maximum = { 26, 12, 15.5f };
    for (int i = 0; i < 8; i++) {
        near(corners[i].x, i & 1 ? maximum.x : minimum.x);
        near(corners[i].y, i & 4 ? maximum.y : minimum.y);
        near(corners[i].z, i & 2 ? maximum.z : minimum.z);
    }
    render_arena_camera camera = render_arena_camera_make(23, -12.5f, 100, 80);
    CF_V3 projected = render_arena_project(&camera, (CF_V3){ 23, 9, 12.5f });
    ecs_entity picked = render_arena_pick(
        world,
        &camera,
        (CF_V2){ projected.x, projected.y }
    );
    assert(ecs_entity_to_u64(picked) == ecs_entity_to_u64(entity));
    /* This ray intersects the model's box proxy but misses its spherical surface. */
    projected = render_arena_project(&camera, (CF_V3){ 25.8f, 9, 15.3f });
    picked = render_arena_pick(world, &camera, (CF_V2){ projected.x, projected.y });
    assert(!ecs_entity_to_u64(picked));
    projected = render_arena_project(&camera, (CF_V3){ -7, 0, 8 });
    picked = render_arena_pick(world, &camera, (CF_V2){ projected.x, projected.y });
    assert(!ecs_entity_to_u64(picked));
    /* A raw primitive retains its manual dimensions and original picking path. */
    ecs_entity raw = ecs_world_create_entity(world);
    geometry.primitive = GAME_PRIMITIVE_SPHERE;
    geometry.width = geometry.height = geometry.depth = 2;
    set_component(world, raw, &GAME_COMPONENT_GameTransform, &transform);
    set_component(world, raw, &GAME_COMPONENT_GameGeometry3D, &geometry);
    picked = render_arena_pick(world, &camera, (CF_V2){ projected.x, projected.y });
    assert(ecs_entity_to_u64(picked) == ecs_entity_to_u64(raw));
    render_shader_model_stats_reset();
    assert(render_arena_entity_bounds(world, entity, corners));
    projected = render_arena_project(&camera, (CF_V3){ 23, 9, 12.5f });
    picked = render_arena_pick(world, &camera, (CF_V2){ projected.x, projected.y });
    assert(ecs_entity_to_u64(picked) == ecs_entity_to_u64(entity));
    assert(render_shader_model_stats().bounds_updates == 0);
    behavior_asset *empty = empty_intersection();
    assert(render_shader_upsert(empty, &error));
    binding.shape = empty->name;
    set_component(world, entity, &GAME_COMPONENT_GameShader, &binding);
    assert(!render_arena_entity_bounds(world, entity, corners));
    picked = render_arena_pick(world, &camera, (CF_V2){ projected.x, projected.y });
    assert(!ecs_entity_to_u64(picked));
    render_shader_clear_world(world);
    ecs_world_free(world);
    behavior_asset_free(empty);
    behavior_asset_free(asset);
    render_shader_catalog_clear();
    puts("Perspective model bounds, surface picking, raw primitive and snapshot reuse: PASS");
}

void test_model_surface()
{
    perspective_model_selection();
    catalog_generation();
    model_dependencies();
    instance_override();
    node *primitive;
    behavior_asset *asset = sphere_asset("ModelSurfaceBounds", 3, &primitive);
    shader_error diagnostic = { 0 };
    assert(render_shader_upsert(asset, &diagnostic));
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world);
    GameTransform position = { 10, 20 };
    GameGeometry3D geometry = { .width = .01f, .height = .02f, .depth = .03f, .elevation = 7 };
    GameSdfSurface3D marker = { .enabled = true };
    GameShader binding = { .shape = asset->name };
    GameVisualTransform pose = { .scale = 2 };
    set_component(world, entity, &GAME_COMPONENT_GameTransform, &position);
    set_component(world, entity, &GAME_COMPONENT_GameGeometry3D, &geometry);
    set_component(world, entity, &GAME_COMPONENT_GameSdfSurface3D, &marker);
    set_component(world, entity, &GAME_COMPONENT_GameShader, &binding);
    set_component(world, entity, &GAME_COMPONENT_GameVisualTransform, &pose);
    render_mesh_surface surface = { 0 };
    char error[256];
    assert(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    assert(acount(surface.parts) == 1);
    near(surface.min.x, 4);
    near(surface.max.x, 16);
    near(surface.min.y, 1);
    near(surface.max.y, 13);
    near(surface.min.z, 14);
    near(surface.max.z, 26);
    near(surface.parts[0].visual.part.dimensions.x, 6);
    near(surface.parts[0].visual.part.dimensions.y, 6);
    near(surface.parts[0].visual.part.dimensions.z, 6);
    near(surface.parts[0].sdf_center.x, 0);
    behavior_asset *offset = offset_box();
    char *source = behavior_asset_save(offset);
    behavior_asset *reloaded = behavior_asset_load(source);
    assert(reloaded && reloaded->domain == BEHAVIOR_DOMAIN_MODEL);
    assert(render_shader_upsert(reloaded, &diagnostic));
    binding.shape = reloaded->name;
    GameOrientation orientation = { .radians = 1.5707963268f };
    set_component(world, entity, &GAME_COMPONENT_GameShader, &binding);
    set_component(world, entity, &GAME_COMPONENT_GameOrientation, &orientation);
    assert(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    assert(acount(surface.parts) == 1);
    /* A box of half-size (1,2,3), offset (2,3,4), then quarter-turned about Z
     * has center (-3,2,4) and half-size (2,1,3). Entity heading turns it about
     * negative Y; scale 2 and world translation (10,7,20) give these bounds. */
    near(surface.parts[0].sdf_center.x, -3);
    near(surface.parts[0].sdf_center.y, 2);
    near(surface.parts[0].sdf_center.z, 4);
    near(surface.min.x, -4);
    near(surface.max.x, 8);
    near(surface.min.y, 9);
    near(surface.max.y, 13);
    near(surface.min.z, 10);
    near(surface.max.z, 18);
    CF_V3 proxy_center = cf_m4_transform_point(
        render_arena_visual_model(&surface.parts[0].visual),
        (CF_V3){ 0 }
    );
    near(proxy_center.x, 2);
    near(proxy_center.y, 11);
    near(proxy_center.z, 14);
    behavior_asset *empty = empty_intersection();
    assert(render_shader_upsert(empty, &diagnostic));
    binding.shape = empty->name;
    set_component(world, entity, &GAME_COMPONENT_GameShader, &binding);
    assert(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    assert(acount(surface.parts) == 0);
    CF_V2 lo, hi;
    assert(!render_mesh_surface_bounds(world, entity, NULL, &lo, &hi, error, sizeof error));
    binding.shape = asset->name;
    set_component(world, entity, &GAME_COMPONENT_GameShader, &binding);
    behavior_asset_free(empty);
    behavior_asset_free(reloaded);
    behavior_asset_free(offset);
    free(source);
    pose.scale = 0;
    set_component(world, entity, &GAME_COMPONENT_GameVisualTransform, &pose);
    assert(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    assert(acount(surface.parts) == 0);
    pose.scale = -1;
    set_component(world, entity, &GAME_COMPONENT_GameVisualTransform, &pose);
    assert(!render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    assert(strstr(error, "positive bounds"));
    render_mesh_surface_free(&surface);
    ecs_world_free(world);
    behavior_asset_free(asset);
    render_shader_catalog_clear();
    puts("Model surface authoritative/off-center bounds, transforms, empty intersection, catalog generations and collisions: PASS");
}
