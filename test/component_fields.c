#include "assets/prototype.h"
#include "graph/compiler/compile.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    behavior_asset *asset;
    node *set;
} field_fixture;

static field_fixture fixture(const char *name, pin_value value, bool wired_target)
{
    field_fixture f = { .asset = behavior_asset_new("Component field") };
    assert(behavior_asset_set_owner_type(f.asset, "ecs.entity") == 0);
    graph *g = f.asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, event, "Set") == 0);
    const node_desc *desc = registry_node_by_name(name);
    assert(
        desc && desc->access && desc->access->write_count == 1 &&
        desc->access->read_count == 1
    );
    assert(!(desc->access->flags & NODE_EFFECT_ENTITY_SCOPED));
    f.set = graph_add_node(g, desc);
    assert(graph_set_pin_default(g, f.set, 0, 1, &value) == 0);
    assert(graph_connect_exec(g, event, 0, f.set, 0, 0) == 0);
    assert(f.set->input_pins[2].flags & PIN_FLAG_DEFAULT_SELF);
    if (wired_target) {
        pin_spec target = { .name = "Target", .type = PIN_HANDLE, .type_name = "ecs.entity" };
        assert(graph_add_pin(g, event, 1, &target) == 1);
        assert(graph_connect_data(g, event, 1, f.set, 2) == 0);
    }
    uint64_t id = f.set->id;
    char *json = behavior_asset_save(f.asset);
    assert(json);
    behavior_asset_free(f.asset);
    f.asset = behavior_asset_load(json);
    free(json);
    assert(f.asset);
    f.set = graph_node_by_id(f.asset->graph, id);
    assert(f.set && (f.set->input_pins[2].flags & PIN_FLAG_DEFAULT_SELF));
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(f.asset, &report) == 0);
    assert(!report.effects.parallel_eligible);
    compile_report_free(&report);
    return f;
}

static behavior_instance *instance(field_fixture *f, ecs_world *world, ecs_entity owner)
{
    pin_value handle = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) };
    behavior_instance *result = behavior_instance_create(f->asset, &handle, NULL, NULL);
    assert(result && behavior_instance_bind_service(result, "ecs.world", world) == 0);
    return result;
}

static flow_run *dispatch(behavior_instance *instance, const pin_value *target)
{
    flow_error error;
    flow_inputs inputs = { .values = target, .count = target ? 1 : 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Set", inputs, &error);
    assert(run);
    return run;
}

static void health(ecs_world *world, ecs_entity owner, ecs_entity other)
{
    field_fixture f = fixture("Set_GameHealth_current", (pin_value){ .type = PIN_I32, .i = 12 }, false);
    behavior_instance *runtime = instance(&f, world, owner);
    flow_run *run = dispatch(runtime, NULL);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    assert(GAME_GameHealth_get_const(world, owner)->current == 12);
    assert(GAME_GameHealth_get_const(world, owner)->maximum == 200);
    assert(GAME_GameHealth_get_const(world, other)->current == 40);
    pin_value snapshot;
    assert(flow_run_watch_output(run, f.set, 1, &snapshot) == 0);
    GAME_GameHealth_get(world, owner)->current = 99;
    assert(((const GameHealth *)snapshot.ptr)->current == 12);
    assert(((const GameHealth *)snapshot.ptr)->maximum == 200);
    behavior_instance_destroy(runtime);
    behavior_asset_free(f.asset);

    f = fixture("Set_GameHealth_current", (pin_value){ .type = PIN_I32, .i = 7 }, true);
    runtime = instance(&f, world, owner);
    pin_value target = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(other) };
    run = dispatch(runtime, &target);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    assert(GAME_GameHealth_get_const(world, owner)->current == 99);
    assert(GAME_GameHealth_get_const(world, other)->current == 7);
    assert(GAME_GameHealth_get_const(world, other)->maximum == 75);
    target.u64 = 0;
    run = dispatch(runtime, &target);
    assert(flow_run_status(run) == FLOW_FAILED);
    assert(GAME_GameHealth_get_const(world, owner)->current == 99);
    ecs_entity empty = ecs_world_create_entity(world);
    target.u64 = ecs_entity_to_u64(empty);
    run = dispatch(runtime, &target);
    assert(flow_run_status(run) == FLOW_FAILED && !GAME_GameHealth_has(world, empty));
    assert(ecs_world_destroy_entity(world, empty) == ECS_OK);
    run = dispatch(runtime, &target);
    assert(flow_run_status(run) == FLOW_FAILED);
    behavior_instance_destroy(runtime);
    behavior_asset_free(f.asset);
}

static void handle_field(ecs_world *world, ecs_entity owner, ecs_entity other)
{
    const type_desc *type = GAME_COMPONENT_GameCollider.type;
    int index = 0;
    while (index < type->field_count &&
           strcmp(type->fields[index].name, "ignore_entity")) {
        index++;
    }
    assert(index < type->field_count);
    const type_field *field = &type->fields[index];
    assert(field->type == PIN_HANDLE && !strcmp(field->type_name, "ecs.entity"));
    const node_desc *make = registry_node_by_name("Make_GameCollider");
    const node_desc *split = registry_node_by_name("Break_GameCollider");
    assert(!strcmp(make->inputs[index].type_name, "ecs.entity"));
    assert(!strcmp(split->outputs[index].type_name, "ecs.entity"));
    pin_value handle = { .type = PIN_HANDLE, .type_name = "ecs.entity" };
    field_fixture f = fixture("Set_GameCollider_ignore_entity", handle, false);
    node *event = f.asset->graph->nodes[0];
    pin_spec parameter = { .name = "Other", .type = PIN_HANDLE, .type_name = "ecs.entity" };
    assert(graph_add_pin(f.asset->graph, event, 1, &parameter) == 1);
    assert(graph_connect_data(f.asset->graph, event, 1, f.set, 1) == 0);
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(f.asset, &report) == 0);
    compile_report_free(&report);
    behavior_instance *runtime = instance(&f, world, owner);
    handle.u64 = ecs_entity_to_u64(other);
    flow_run *run = dispatch(runtime, &handle);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    const GameCollider *collider = GAME_GameCollider_get_const(world, owner);
    assert(collider->ignore_entity == handle.u64 && collider->radius == 12);
    pin_value read;
    pin_read_field(collider, field, &read);
    assert(read.type == PIN_HANDLE && !strcmp(read.type_name, "ecs.entity"));
    assert(read.u64 == handle.u64);
    behavior_instance_destroy(runtime);
    behavior_asset_free(f.asset);

    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[256];
    game_prototype *prototype = game_prototype_load(
        "name \"Owner\"\n"
        "components {\n"
        "    GameCollider {\n"
        "        radius 12\n"
        "        ignore_entity #null\n"
        "    }\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    assert(prototype);
    assert(game_prototype_write_field(prototype, "GameCollider", "ignore_entity", handle) == 0);
    char *json = game_prototype_save(prototype);
    game_prototype_free(prototype);
    prototype = game_prototype_load(json, &registry, error, sizeof error);
    free(json);
    assert(prototype);
    assert(game_prototype_read_field(prototype, "GameCollider", "ignore_entity", &read) == 0);
    assert(read.type == PIN_HANDLE && read.u64 == 0);
    game_prototype_free(prototype);
    assert(!game_prototype_load(
        "name \"Stale\"\n"
        "components {\n"
        "    GameCollider {\n"
        "        ignore_entity 123\n"
        "    }\n"
        "}\n",
        &registry,
        error,
        sizeof error
    ));
}

static void vector_field(ecs_world *world, ecs_entity owner)
{
    CF_V2 direction = { 3, -4 };
    pin_value value = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &direction };
    field_fixture f = fixture("Set_GameInput_move", value, false);
    behavior_instance *runtime = instance(&f, world, owner);
    flow_run *run = dispatch(runtime, NULL);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    const GameInput *input = GAME_GameInput_get_const(world, owner);
    assert(input->move.x == 3 && input->move.y == -4);
    behavior_instance_destroy(runtime);
    behavior_asset_free(f.asset);
    input = GAME_GameInput_get_const(world, owner);
    assert(input->move.x == 3 && input->move.y == -4);

    f = fixture("Set_GameTransform_x", (pin_value){ .type = PIN_F32, .f = 17 }, false);
    runtime = instance(&f, world, owner);
    run = dispatch(runtime, NULL);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    assert(GAME_GameTransform_get_const(world, owner)->x == 17);
    assert(GAME_GameTransform_get_const(world, owner)->y == -9);
    behavior_instance_destroy(runtime);
    behavior_asset_free(f.asset);
}

static void ui_fields(ecs_world *world, ecs_entity owner)
{
    field_fixture f = fixture("Set_GameUi_order", (pin_value){ .type = PIN_I32, .i = 3 }, false);
    node *get = graph_add_node(f.asset->graph, registry_node_by_name("Get_GameUi"));
    assert(graph_expose_output_fields(f.asset->graph, get, 0) == 0);
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(f.asset, &report) == 0);
    compile_report_free(&report);
    GameUi *ui = GAME_GameUi_get(world, owner);
    ui->order = 9;
    ui->document = "Rally";
    ui->hidden = true;
    behavior_instance *runtime = instance(&f, world, owner);
    assert(flow_run_status(dispatch(runtime, NULL)) == FLOW_COMPLETED);
    ui = GAME_GameUi_get(world, owner);
    assert(ui->order == 3 && ui->hidden && !strcmp(ui->document, "Rally"));
    behavior_instance_destroy(runtime);
    behavior_asset_free(f.asset);
    puts("ui fields PASS: expanded getter compiles; changing order preserves document and visibility");
}

static void reflected_color_fields()
{
    const type_field *color = pin_type_field_path(GAME_COMPONENT_GameAppearance.type, "color");
    const type_field *category = pin_type_field_path(GAME_COMPONENT_GameCollider.type, "category");
    assert(color && color->color && category && !category->color);
    const type_desc *material = GAME_COMPONENT_GameMaterial.type;
    for (int i = 0; i < material->field_count; i++) {
        if (material->fields[i].type == PIN_U32) {
            assert(material->fields[i].color);
        }
    }
    const node_desc *set = registry_node_by_name("Set_GameAppearance_color");
    assert(set && (set->inputs[1].flags & PIN_FLAG_COLOR));
    behavior_asset *asset = behavior_asset_new("Color fields");
    graph *g = asset->graph;
    node *get = graph_add_node(g, registry_node_by_name("Get_GameAppearance"));
    assert(graph_expose_output_fields(g, get, 0) == 0);
    bool exposed = false;
    for (int i = 0; i < get->output_count; i++) {
        if (!strcmp(get->output_pins[i].name, "color")) {
            assert(get->output_pins[i].flags & PIN_FLAG_COLOR);
            exposed = true;
        }
    }
    assert(exposed);
    node *make = graph_add_node(g, registry_node_by_name("Make_GameAppearance"));
    bool constructed = false;
    for (int i = 0; i < make->input_count; i++) {
        if (!strcmp(make->input_pins[i].name, "color")) {
            assert(make->input_pins[i].flags & PIN_FLAG_COLOR);
            constructed = true;
        }
    }
    assert(constructed);
    behavior_asset_free(asset);
    assert(!strcmp(registry_enum_label("GameFluidKind", GAME_FLUID_LIQUID), "Liquid"));
    assert(!strcmp(registry_enum_label("GameHRCToneMapping", GAME_HRC_TONE_AGX), "AgX"));
    assert(!strcmp(registry_enum_label("GameHRCUpscale", GAME_HRC_UPSCALE_EDGE_AWARE), "Edge aware"));
}

void test_component_fields()
{
    reflected_color_fields();
    assert(registry_node_by_name("Set_GameHealth"));
    assert(registry_node_by_name("SetEntity_GameHealth"));
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameInput, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameCollider, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameUi, NULL) == ECS_OK);
    ecs_entity owner = ecs_world_create_entity(world);
    ecs_entity other = ecs_world_create_entity(world);
    assert(GAME_GameHealth_add(world, owner, 0) == ECS_OK);
    assert(GAME_GameHealth_add(world, other, 0) == ECS_OK);
    assert(GAME_GameTransform_add(world, owner, 0) == ECS_OK);
    assert(GAME_GameInput_add(world, owner, 0) == ECS_OK);
    assert(GAME_GameCollider_add(world, owner, 0) == ECS_OK);
    assert(GAME_GameUi_add(world, owner, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameHealth_get(world, owner) = (GameHealth){ 73, 200 };
    *GAME_GameHealth_get(world, other) = (GameHealth){ 40, 75 };
    *GAME_GameTransform_get(world, owner) = (GameTransform){ 3, -9 };
    GAME_GameCollider_get(world, owner)->radius = 12;
    health(world, owner, other);
    vector_field(world, owner);
    handle_field(world, owner, other);
    ui_fields(world, owner);
    ecs_world_free(world);
    puts("component fields PASS: Self, explicit and invalid targets, preserved members, structs and serialization");
}
