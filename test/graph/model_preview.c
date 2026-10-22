#include "authoring/preview.h"
#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/modeling/model_compile.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <assert.h>
#include <string.h>

static behavior_asset *model(const char *name)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_MODEL;
    return a;
}

static void scalar(graph *g, node *n, int pin, float number)
{
    pin_value value = { .type = PIN_F32, .f = number };
    assert(!graph_set_pin_default(g, n, false, pin, &value));
}

static void vector(graph *g, node *n, int pin, float x, float y, float z)
{
    float xyz[3] = { x, y, z };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = xyz };
    assert(!graph_set_pin_default(g, n, false, pin, &value));
}

static void connect(graph *g, node *from, node *to, int pin)
{
    assert(!graph_connect_data(g, from, 0, to, pin));
}

static void publish(graph *g, node *from)
{
    connect(g, from, model_graph_add_output(g), 0);
}

static node *part(graph *g, const char *name, float radius)
{
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    scalar(g, sphere, 0, radius);
    node *part = graph_add_node(g, &NODE_ModelPart);
    assert(!graph_set_node_reference(g, part, 0, name));
    connect(g, sphere, part, 0);
    return part;
}

static behavior_asset *material(const char *name, float red, float green, float blue)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *output = shader_graph_add_output(a->graph, false);
    vector(a->graph, output, 0, red, green, blue);
    return a;
}

static node *slot(graph *g, const char *name, const char *material)
{
    node *n = graph_add_node(g, &NODE_ModelMaterialSlot);
    assert(!graph_set_node_reference(g, n, 0, name));
    n->reference_asset = graph_own_str(g, material, strlen(material));
    return n;
}

TEST_CASE(live_rename_updates_applied_sources)
{
    ecs_world *ecs = ecs_world_new();
    preview_world *world = preview_world_new(ecs);
    behavior_asset *paint = material("LiveRenamePaint", 1, 0, 0);
    preview_definition *paint_definition = preview_definition_new(paint->name, paint);
    REQUIRE(!preview_add_definition(world, paint_definition));
    behavior_asset *base = model("LiveRenameBase");
    node *body = part(base->graph, "Body", 2);
    node *paint_slot = slot(base->graph, "Paint", paint->name);
    connect(base->graph, paint_slot, body, 1);
    publish(base->graph, body);
    preview_definition *base_definition = preview_definition_new(base->name, base);
    REQUIRE(!preview_add_definition(world, base_definition));
    behavior_asset *parent = model("LiveRenameParent");
    node *instance = graph_add_node(parent->graph, &NODE_ModelInstance);
    instance->reference_asset = graph_own_str(parent->graph, base->name, strlen(base->name));
    pin_spec override = { .name = "Paint",
                          .type = PIN_STR,
                          .type_name = "MaterialAsset",
                          .declaration_id = paint_slot->id,
                          .default_value = {
                              .type = PIN_STR,
                              .type_name = "MaterialAsset",
                              .text = &(const pin_string){
                                  .bytes = paint->name,
                                  .length = (uint32_t)strlen(paint->name) } } };
    REQUIRE(graph_add_pin(parent->graph, instance, false, &override) == 0);
    publish(parent->graph, instance);
    uint64_t instance_id = instance->id;
    preview_definition *parent_definition = preview_definition_new(parent->name, parent);
    REQUIRE(!preview_add_definition(world, parent_definition));
    REQUIRE(!preview_rename_definition(world, "LiveRenameBase", "LiveRenamedBase"));
    REQUIRE(!strcmp(base_definition->working->name, "LiveRenamedBase"));
    REQUIRE(!strcmp(base_definition->applied->name, "LiveRenamedBase"));
    REQUIRE(!strcmp(instance->reference_asset, "LiveRenamedBase"));
    node *applied_instance = graph_node_by_id(parent_definition->applied->graph, instance_id);
    REQUIRE(applied_instance && !strcmp(applied_instance->reference_asset, "LiveRenamedBase"));
    REQUIRE(!preview_rename_definition(world, "LiveRenamePaint", "LiveRenamedPaint"));
    REQUIRE(!strcmp(paint_definition->working->name, "LiveRenamedPaint"));
    REQUIRE(!strcmp(paint_definition->applied->name, "LiveRenamedPaint"));
    REQUIRE(!strcmp(pin_text(instance->input_pins[0].default_value).bytes, "LiveRenamedPaint"));
    applied_instance = graph_node_by_id(parent_definition->applied->graph, instance_id);
    REQUIRE(
        applied_instance &&
        !strcmp(pin_text(applied_instance->input_pins[0].default_value).bytes, "LiveRenamedPaint")
    );
    REQUIRE(!preview_remove_definition(world, "LiveRenameParent"));
    REQUIRE(!preview_remove_definition(world, "LiveRenamedBase"));
    REQUIRE(!preview_remove_definition(world, "LiveRenamedPaint"));
    preview_world_free(world);
    ecs_world_free(ecs);
    return true;
}

TEST_CASE(live_model_registration_names_its_node)
{
    ecs_world *ecs = ecs_world_new();
    preview_world *world = preview_world_new(ecs);
    behavior_asset *asset = model("LiveMissingPaint");
    node *body = part(asset->graph, "Body", 2);
    node *paint = slot(asset->graph, "Paint", "LiveAbsentPaint");
    connect(asset->graph, paint, body, 1);
    publish(asset->graph, body);
    preview_definition *definition = preview_definition_new(asset->name, asset);
    REQUIRE(preview_add_definition(world, definition));
    REQUIRE(acount(definition->diagnostics.items) == 1);
    REQUIRE(definition->diagnostics.items[0].node_id == paint->id);
    preview_definition_free(definition);
    preview_world_free(world);
    ecs_world_free(ecs);
    return true;
}

void test_model_preview()
{
    test_graph_packs_install();
    RUN_TEST_CASE(live_rename_updates_applied_sources);
    RUN_TEST_CASE(live_model_registration_names_its_node);
    authoring_shader_catalog_clear();
    test_graph_packs_restore();
}
