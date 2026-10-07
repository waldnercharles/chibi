#include "graph/document/serialize.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static void near_value(float actual, float expected)
{
    assert(fabsf(actual - expected) < .00001f);
}
void test_model_material_scope()
{
    render_shader_catalog_clear();
    behavior_asset *material = behavior_asset_new("ScopePaint");
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = material->graph;
    node *roughness = graph_add_node(g, registry_node_by_name("Shader Parameter"));
    assert(!graph_set_node_reference(g, roughness, 0, "Roughness"));
    assert(!graph_set_pin_type(g, roughness, true, 0, PIN_F32, NULL));
    pin_value value = { .type = PIN_F32, .f = .2f };
    assert(!graph_set_pin_default(g, roughness, true, 0, &value));
    assert(!graph_connect_data(g, roughness, 0, shader_graph_add_output(g, false), 2));
    shader_error error = { 0 };
    assert(render_shader_upsert(material, &error));
    int id = render_shader_find(material->name);
    const char *text = "shape.Radius 9\n"
                       "material.ImageSlot.Roughness 0.35\n"
                       "MirrorSlot.Roughness 0.65\n";
    render_shader_parameter_cache *cache = NULL;
    float values[32][4] = { { 0 } };
    for (int i = 0; i < 4; i++) {
        assert(render_shader_material_parameters_scoped_cached(
            id,
            NULL,
            (ecs_entity){ 0 },
            text,
            "ImageSlot",
            values,
            &cache,
            &error
        ));
        near_value(values[0][0], .35f);
        assert(render_shader_material_parameters_scoped_cached(
            id,
            NULL,
            (ecs_entity){ 0 },
            text,
            "MirrorSlot",
            values,
            &cache,
            &error
        ));
        near_value(values[0][0], .65f);
    }
    float before[32][4];
    memcpy(before, values, sizeof values);
    const char *invalid[] = { "material.MirrorSlot.Roughness \"bad\"\n",
                              "(array)MirrorSlot.Roughness 0.2 0.3\n",
                              "MirrorSlot.Unknown 0.2\n",
                              "Roughness 0.2\n"
                              "MirrorSlot.Roughness 0.3\n",
                              "malformed {" };
    for (int i = 0; i < 5; i++) {
        assert(!render_shader_material_parameters_scoped_cached(
            id,
            NULL,
            (ecs_entity){ 0 },
            invalid[i],
            "MirrorSlot",
            values,
            &cache,
            &error
        ));
        assert(!memcmp(before, values, sizeof values));
    }
    assert(render_shader_material_parameters_scoped_cached(
        id,
        NULL,
        (ecs_entity){ 0 },
        text,
        "MirrorSlot",
        values,
        &cache,
        &error
    ));
    near_value(values[0][0], .65f);
    assert(render_shader_material_parameters_scoped_cached(
        id,
        NULL,
        (ecs_entity){ 0 },
        "Roughness 0.4\n",
        "ImageSlot",
        values,
        &cache,
        &error
    ));
    near_value(values[0][0], .4f);
    assert(render_shader_material_parameters_scoped_cached(
        id,
        NULL,
        (ecs_entity){ 0 },
        "material.Roughness 0.45\n",
        "ImageSlot",
        values,
        &cache,
        &error
    ));
    near_value(values[0][0], .45f);
    render_shader_parameters_cached(id, NULL, (ecs_entity){ 0 }, text, values, &cache);
    near_value(values[0][0], .2f);
    assert(render_shader_material_parameters_scoped_cached(
        id,
        NULL,
        (ecs_entity){ 0 },
        text,
        "ImageSlot",
        values,
        &cache,
        &error
    ));
    near_value(values[0][0], .35f);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world),
               other = ecs_world_create_entity(world);
    GameShader binding = { .material = material->name };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameShader.id, &binding, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    const node_desc *setter = registry_node_by_name("Set Material Parameter");
    assert(setter && setter->host_call);
    pin_value inputs[] = { { .type = PIN_HANDLE,
                             .type_name = "ecs.entity",
                             .u64 = ecs_entity_to_u64(entity) },
                           { .type = PIN_STR, .str = "Roughness" },
                           { .type = PIN_F32, .f = .9f } };
    assert(!setter->host_call->invoke(world, inputs, NULL));
    assert(
        render_shader_material_parameters_scoped_cached(id, world, entity, text, "ImageSlot", values, &cache, &error)
    );
    near_value(values[0][0], .9f);
    assert(
        render_shader_material_parameters_scoped_cached(id, world, other, text, "ImageSlot", values, &cache, &error)
    );
    near_value(values[0][0], .35f);
    render_shader_clear_world(world);
    ecs_world_free(world);
    render_shader_parameter_cache_free(cache);
    behavior_asset_free(material);
    render_shader_catalog_clear();
}
