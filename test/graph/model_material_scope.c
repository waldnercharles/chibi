#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "render/shader_catalog.h"
#include "render/shader_parameters.h"
#include "runtime/component_catalog.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <math.h>
#include <string.h>

static bool near_value(float actual, float expected)
{
    return fabsf(actual - expected) < .00001f;
}

/* A model whose Radius parameter sizes two parts, painted with `material`
 * through the slots ImageSlot and MirrorSlot. */
static behavior_asset *painted_pair(const char *material)
{
    behavior_asset *a = behavior_asset_new("ScopePair");
    a->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = a->graph;
    node *radius = graph_add_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, radius, 0, "Radius"));
    const float range[] = { 1, .5f, 10 };
    for (int i = 0; i < 3; i++) {
        pin_value value = { .type = PIN_F32, .f = range[i] };
        assert(!graph_set_pin_default(g, radius, false, i, &value));
    }
    node *pair = graph_add_node(g, &NODE_ModelUnion);
    const char *slots[] = { "ImageSlot", "MirrorSlot" };
    for (int i = 0; i < 2; i++) {
        node *sphere = graph_add_node(g, &NODE_ModelSphere),
             *part = graph_add_node(g, &NODE_ModelPart),
             *slot = graph_add_node(g, &NODE_ModelMaterialSlot);
        assert(!graph_set_node_reference(g, part, 0, slots[i]));
        assert(!graph_set_node_reference(g, slot, 0, slots[i]));
        slot->reference_asset = graph_own_str(g, material, strlen(material));
        assert(!graph_connect_data(g, radius, 0, sphere, 0));
        assert(!graph_connect_data(g, sphere, 0, part, 0));
        assert(!graph_connect_data(g, slot, 0, part, 1));
        assert(!graph_connect_data(g, part, 0, pair, i));
    }
    assert(!graph_connect_data(g, pair, 0, model_graph_add_output(g), 0));
    return a;
}

/* Prepares `text` as the parameters of a ScopePair binding whose entity
 * material is ScopePaint, as project preparation does before a scene draws. */
static bool prepare(const char *text)
{
    Shader binding = { .shape = "ScopePair",
                       .material = "ScopePaint",
                       .parameters = (char *)text };
    shader_error error = { 0 };
    return authoring_shader_binding_prepare(&binding, &error);
}

/* Slot-scoped material parameters resolve from a binding's text and from
 * Set Material Parameter, and a refused text leaves the values as they were. */
TEST_CASE(scoped_material_parameters)
{
    authoring_shader_catalog_clear();
    behavior_asset *material = behavior_asset_new("ScopePaint");
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = material->graph;
    node *roughness = graph_add_node(g, registry_node_by_name("Shader Parameter"));
    REQUIRE(!graph_set_node_reference(g, roughness, 0, "Roughness"));
    REQUIRE(!graph_set_pin_type(g, roughness, true, 0, PIN_F32, NULL));
    pin_value value = { .type = PIN_F32, .f = .2f };
    REQUIRE(!graph_set_pin_default(g, roughness, true, 0, &value));
    REQUIRE(!graph_connect_data(g, roughness, 0, shader_graph_add_output(g, false), 2));
    shader_error error = { 0 };
    REQUIRE(render_shader_upsert(material, &error));
    int id = render_shader_find(material->name);
    behavior_asset *pair = painted_pair(material->name);
    const behavior_asset *models[] = { pair };
    REQUIRE(render_shader_models_upsert(models, 1, &error));
    const char *text = "shape.Radius 9\n"
                       "material.ImageSlot.Roughness 0.35\n"
                       "MirrorSlot.Roughness 0.65\n";
    REQUIRE(prepare(text));
    render_shader_parameter_cache *cache = NULL;
    float values[32][4] = { { 0 } };
    for (int i = 0; i < 4; i++) {
        REQUIRE(render_shader_material_parameters_scoped_cached(
            id,
            NULL,
            (ecs_entity){ 0 },
            text,
            "ImageSlot",
            values,
            &cache,
            &error
        ));
        REQUIRE(near_value(values[0][0], .35f));
        REQUIRE(render_shader_material_parameters_scoped_cached(
            id,
            NULL,
            (ecs_entity){ 0 },
            text,
            "MirrorSlot",
            values,
            &cache,
            &error
        ));
        REQUIRE(near_value(values[0][0], .65f));
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
        REQUIRE(!prepare(invalid[i]));
        REQUIRE(!render_shader_material_parameters_scoped_cached(
            id,
            NULL,
            (ecs_entity){ 0 },
            invalid[i],
            "MirrorSlot",
            values,
            &cache,
            &error
        ));
        REQUIRE(!memcmp(before, values, sizeof values));
    }
    REQUIRE(render_shader_material_parameters_scoped_cached(
        id,
        NULL,
        (ecs_entity){ 0 },
        text,
        "MirrorSlot",
        values,
        &cache,
        &error
    ));
    REQUIRE(near_value(values[0][0], .65f));
    /* Both slots paint with ScopePaint, so its unscoped names are ambiguous. */
    REQUIRE(!prepare("Roughness 0.4\n"));
    REQUIRE(!prepare("material.Roughness 0.45\n"));
    REQUIRE(render_shader_material_parameters_scoped_cached(
        id,
        NULL,
        (ecs_entity){ 0 },
        text,
        "ImageSlot",
        values,
        &cache,
        &error
    ));
    REQUIRE(near_value(values[0][0], .35f));
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world),
               other = ecs_world_create_entity(world);
    Shader binding = { .material = material->name };
    REQUIRE(ecs_world_record_add(world, entity, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    pin_value inputs[] = {
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = ecs_entity_to_u64(entity) },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "Roughness",
                                       .length = sizeof("Roughness") - 1 } },
        { .type = PIN_F32, .f = .9f }
    };
    REQUIRE(!RENDER_MATERIAL_PARAMETER_CALL.invoke(world, inputs, NULL));
    REQUIRE(
        render_shader_material_parameters_scoped_cached(id, world, entity, text, "ImageSlot", values, &cache, &error)
    );
    REQUIRE(near_value(values[0][0], .9f));
    REQUIRE(
        render_shader_material_parameters_scoped_cached(id, world, other, text, "ImageSlot", values, &cache, &error)
    );
    REQUIRE(near_value(values[0][0], .35f));
    render_shader_clear_world(world);
    ecs_world_free(world);
    render_shader_parameter_cache_free(cache);
    behavior_asset_free(pair);
    behavior_asset_free(material);
    authoring_shader_catalog_clear();
    return true;
}

static void teardown()
{
    authoring_shader_catalog_clear();
}

void test_model_material_scope()
{
    test_graph_packs_install();
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(scoped_material_parameters);
    pu_clear_setup();
    test_graph_packs_restore();
}
