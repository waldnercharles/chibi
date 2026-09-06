#include "../core/arena.h"
#include "../core/array.h"
#include "../core/compile.h"
#include "../core/eval.h"
#include "../core/graph_jit_sljit.h"
#include "../core/instance.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../game/generated.h"
#include "../game/system.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static node *add(graph *graph, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    assert(desc);
    node *node = graph_add_node(graph, desc);
    assert(node);
    return node;
}

static int field(const node *node, uint64_t source, const char *name)
{
    for (int i = 0; i < node->output_count; i++) {
        const pin_spec *pin = &node->output_pins[i];
        if (pin->field_source == source && pin->field_name &&
            strcmp(pin->field_name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void number(graph *graph, node *node, int input, float value)
{
    assert(
        graph_set_pin_default(
            graph,
            node,
            0,
            input,
            &(pin_value){ .type = PIN_F32, .type_name = "float", .f = value }
        ) == 0
    );
}

static void compile(behavior_asset *asset)
{
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    if (errors) {
        for (int i = 0; i < acount(report.items); i++) {
            fprintf(stderr, "field outputs: %s\n", report.items[i].message);
        }
    }
    assert(errors == 0);
    compile_report_free(&report);
}

static behavior_instance *instance(behavior_asset *asset, ecs_world *world, ecs_entity entity)
{
    pin_value owner = { .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .u64 = ecs_entity_to_u64(entity) };
    behavior_instance *result = behavior_instance_create(asset, &owner, NULL, NULL);
    assert(result);
    assert(behavior_instance_bind_service(result, "ecs.world", world) == 0);
    return result;
}

static void verify_reads(
    behavior_asset *asset,
    ecs_world *world,
    ecs_entity entity,
    uint64_t owner_id,
    uint64_t target_id,
    uint64_t aggregate_id,
    uint64_t check_id
)
{
    behavior_instance *runtime = instance(asset, world, entity);
    eval_context context;
    eval_context_init(&context);
    context.instance = runtime;
    context.services = runtime->services;
    arena memory;
    arena_init(&memory, 16);
    node *readers[] = { graph_node_by_id(asset->graph, owner_id),
                        graph_node_by_id(asset->graph, target_id) };
    for (int i = 0; i < 2; i++) {
        node *reader = readers[i];
        pin_value value;
        assert(graph_eval_output(asset->graph, reader, 0, &memory, &value, &context) == 0);
        assert(value.type == PIN_STRUCT && strcmp(value.type_name, "GameTransform") == 0);
        const GameTransform *position = value.ptr;
        assert(position->x == 17 && position->y == -4);
        int x = field(reader, reader->output_pins[0].id, "x");
        assert(x >= 0);
        assert(graph_eval_output(asset->graph, reader, x, &memory, &value, &context) == 0);
        assert(value.type == PIN_F32 && value.f == 17);
    }
    pin_value aggregate, check;
    assert(
        graph_eval_output(
            asset->graph,
            graph_node_by_id(asset->graph, aggregate_id),
            0,
            &memory,
            &aggregate,
            &context
        ) == 0
    );
    assert(strcmp(aggregate.type_name, "GameInput") == 0);
    const GameInput *input = aggregate.ptr;
    assert(input->move.x == 17 && input->move.y == -4);
    assert(
        graph_eval_output(asset->graph, graph_node_by_id(asset->graph, check_id), 0, &memory, &check, &context) ==
        0
    );
    assert(check.type == PIN_BOOL && check.b);
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(runtime);
}

static void exposed_reads(ecs_world *world, ecs_entity entity)
{
    behavior_asset *asset = behavior_asset_new("Field reads");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *graph = asset->graph;
    node *owner = add(graph, "Get_GameTransform");
    node *target = add(graph, "GetEntity_GameTransform");
    assert(
        graph_set_pin_default(
            graph,
            target,
            0,
            0,
            &(pin_value){ .type = PIN_HANDLE,
                          .type_name = "ecs.entity",
                          .u64 = ecs_entity_to_u64(entity) }
        ) == 0
    );
    uint64_t whole_id = owner->output_pins[0].id;
    assert(graph_expose_output_fields(graph, owner, 0) == 0);
    assert(graph_expose_output_fields(graph, target, 0) == 0);
    assert(owner->output_count == 3 && owner->output_pins[0].id == whole_id);
    int x = field(owner, whole_id, "x"), y = field(owner, whole_id, "y");
    assert(x >= 0 && y >= 0);
    uint64_t x_id = owner->output_pins[x].id, y_id = owner->output_pins[y].id;
    uint64_t counter = graph->id_counter;
    assert(graph_expose_output_fields(graph, owner, 0) == 0 && graph->id_counter == counter);
    node *aggregate = add(graph, "Make_GameInput");
    node *check = add(graph, "Greater Or Equal");
    assert(graph_connect_data(graph, owner, 0, aggregate, 0) == 0);
    assert(graph_connect_data(graph, owner, x, check, 0) == 0);
    number(graph, check, 1, 10);
    compile(asset);
    verify_reads(
        asset,
        world,
        entity,
        owner->id,
        target->id,
        aggregate->id,
        check->id
    );
    char *json = behavior_asset_save_json(asset);
    assert(json);
    behavior_asset *loaded = behavior_asset_load_json(json);
    if (!loaded) {
        fprintf(stderr, "field output reload: %s\n", graph_last_error());
    }
    assert(loaded);
    free(json);
    /* Runtime handles are deliberately not serialized; bind this test target. */
    assert(
        graph_set_pin_default(
            loaded->graph,
            graph_node_by_id(loaded->graph, target->id),
            0,
            0,
            &(pin_value){ .type = PIN_HANDLE,
                          .type_name = "ecs.entity",
                          .u64 = ecs_entity_to_u64(entity) }
        ) == 0
    );
    node *copy = graph_node_by_id(loaded->graph, owner->id);
    assert(copy->output_pins[0].id == whole_id);
    assert(copy->output_pins[field(copy, whole_id, "x")].id == x_id);
    assert(copy->output_pins[field(copy, whole_id, "y")].id == y_id);
    compile(loaded);
    verify_reads(
        loaded,
        world,
        entity,
        owner->id,
        target->id,
        aggregate->id,
        check->id
    );
    assert(graph_hide_output_fields(loaded->graph, copy, 0) == 0);
    assert(copy->output_count == 2 && field(copy, whole_id, "y") == -1);
    assert(copy->output_pins[field(copy, whole_id, "x")].id == x_id);
    compile(loaded);
    verify_reads(
        loaded,
        world,
        entity,
        owner->id,
        target->id,
        aggregate->id,
        check->id
    );
    assert(graph_expose_output_fields(loaded->graph, copy, 0) == 0);
    assert(copy->output_count == 3 && copy->output_pins[0].id == whole_id);
    assert(copy->output_pins[field(copy, whole_id, "x")].id == x_id);
    assert(graph_expose_output_fields(loaded->graph, copy, field(copy, whole_id, "x")) == -1);
    behavior_asset_free(loaded);
    behavior_asset_free(asset);
}

static void set_snapshots(ecs_world *world, ecs_entity entity)
{
    behavior_asset *asset = behavior_asset_new("Set snapshots");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    pin_value zero = { .type = PIN_F32, .type_name = "float" };
    uint64_t x_id = behavior_asset_add_variable(asset, "X", PIN_F32, "float", PIN_CONTAINER_SINGLE, &zero, "Test")
                        ->id;
    uint64_t y_id = behavior_asset_add_variable(asset, "Y", PIN_F32, "float", PIN_CONTAINER_SINGLE, &zero, "Test")
                        ->id;
    graph *graph = asset->graph;
    node *event = add(graph, "Event");
    assert(graph_set_event_name(graph, event, "Test") == 0);
    node *first_value = add(graph, "Make_GameTransform");
    node *second_value = add(graph, "Make_GameTransform");
    number(graph, first_value, 0, 17);
    number(graph, first_value, 1, -4);
    number(graph, second_value, 0, 99);
    number(graph, second_value, 1, 88);
    node *first = add(graph, "Set_GameTransform");
    node *second = add(graph, "Set_GameTransform");
    assert(graph_expose_output_fields(graph, first, 1) == 0);
    int x = field(first, first->output_pins[1].id, "x");
    assert(x >= 0);
    node *split = add(graph, "Break_GameTransform");
    node *remember_x = behavior_asset_add_set_node(asset, x_id);
    node *remember_y = behavior_asset_add_set_node(asset, y_id);
    assert(graph_connect_data(graph, first_value, 0, first, 1) == 0);
    assert(graph_connect_data(graph, second_value, 0, second, 1) == 0);
    assert(graph_connect_data(graph, first, x, remember_x, 1) == 0);
    assert(graph_connect_data(graph, first, 1, split, 0) == 0);
    assert(graph_connect_data(graph, split, 1, remember_y, 1) == 0);
    assert(graph_connect_exec(graph, event, 0, first, 0, 0) == 0);
    assert(graph_connect_exec(graph, first, 0, second, 0, 0) == 0);
    assert(graph_connect_exec(graph, second, 0, remember_x, 0, 0) == 0);
    assert(graph_connect_exec(graph, remember_x, 0, remember_y, 0, 0) == 0);
    compile(asset);
    behavior_instance *runtime = instance(asset, world, entity);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(runtime, "Test", (flow_inputs){ 0 }, &error);
    if (!run) {
        fprintf(stderr, "set field snapshots: %s\n", error.message);
    }
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value observed;
    assert(behavior_instance_get_variable(runtime, x_id, &observed) == 0 && observed.f == 17);
    assert(behavior_instance_get_variable(runtime, y_id, &observed) == 0 && observed.f == -4);
    const GameTransform *position = GAME_GameTransform_get_const(world, entity);
    assert(position->x == 99 && position->y == 88);
    behavior_instance_destroy(runtime);
    behavior_asset_free(asset);
}

static void system_fields(ecs_world *world, ecs_entity entity)
{
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) == ECS_OK);
    assert(GAME_GameMovement_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    for (int scenario = 0; scenario < 8; scenario++) {
        int mode = scenario % 4;
        if (mode == GAME_SYSTEM_EXEC_SJLIT && !graph_sljit_available()) {
            continue;
        }
        bool dataflow = scenario >= 4;
        behavior_asset *asset = behavior_asset_new("System field");
        assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
        graph *graph = asset->graph;
        node *read = add(graph, "Get_GameTransform");
        assert(graph_expose_output_fields(graph, read, 0) == 0);
        int x = field(read, read->output_pins[0].id, "x");
        node *make = add(graph, "Make_GameMovement");
        node *set = add(graph, "Set_GameMovement");
        graph->dataflow = dataflow;
        node *tick = add(graph, "Event");
        assert(graph_set_event_name(graph, tick, "Tick") == 0);
        if (dataflow) {
            assert(graph_remove_pin(graph, set, 0, 0) == 0);
            assert(graph_remove_pin(graph, set, 1, 0) == 0);
            assert(graph_remove_pin(graph, set, 1, 0) == 0);
        } else {
            assert(graph_connect_exec(graph, tick, 0, set, 0, 0) == 0);
        }
        assert(graph_connect_data(graph, read, x, make, 0) == 0);
        assert(graph_connect_data(graph, make, 0, set, dataflow ? 0 : 1) == 0);
        game_system *system = game_system_new(world, asset);
        assert(system && !compile_report_blocked(game_system_compile(system)));
        assert(game_system_uses_bytecode(system));
        GAME_GameTransform_get(world, entity)->x = 19.0f + mode;
        GAME_GameMovement_get(world, entity)->speed = 0;
        flow_error error = { 0 };
        int result = game_system_tick_mode(system, 0, 0.016, &error, (game_system_exec_mode)mode);
        if (result < 0) {
            fprintf(stderr, "system field mode %d: %s\n", mode, error.message);
        }
        assert(result >= 0);
        assert(GAME_GameMovement_get_const(world, entity)->speed == 19.0f + mode);
        game_system_free(system);
    }
}

static void invalid_field_metadata()
{
    behavior_asset *asset = behavior_asset_new("Invalid field");
    node *scalar = add(asset->graph, "Integer");
    assert(graph_expose_output_fields(asset->graph, scalar, 0) == -1);
    node *array = add(asset->graph, "Make_GameTransform");
    assert(graph_set_pin_container(asset->graph, array, 1, 0, PIN_CONTAINER_ARRAY) == 0);
    assert(graph_expose_output_fields(asset->graph, array, 0) == -1);
    assert(graph_remove_node(asset->graph, array) == 0);
    node *read = add(asset->graph, "Make_GameTransform");
    assert(graph_expose_output_fields(asset->graph, read, 0) == 0);
    int x = field(read, read->output_pins[0].id, "x");
    read->output_pins[x].field_source = UINT64_MAX;
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) != 0);
    bool diagnosed = false;
    for (int i = 0; i < acount(report.items); i++) {
        diagnosed |= report.items[i].rule == CMPL_RULE_FIELD_PROJECTION;
    }
    assert(diagnosed);
    compile_report_free(&report);
    behavior_asset_free(asset);
}

static void copied_fields()
{
    behavior_asset *asset = behavior_asset_new("Copied fields");
    graph *graph = asset->graph;
    node *make = add(graph, "Make_GameTransform");
    number(graph, make, 0, 7);
    number(graph, make, 1, 9);
    assert(graph_expose_output_fields(graph, make, 0) == 0);
    uint64_t whole_id = make->output_pins[0].id;
    int x = field(make, whole_id, "x");
    assert(x >= 0);
    uint64_t x_id = make->output_pins[x].id;
    node *check = add(graph, "Greater Or Equal");
    node *aggregate = add(graph, "Make_GameInput");
    number(graph, check, 1, 5);
    assert(graph_connect_data(graph, make, x, check, 0) == 0);
    assert(graph_connect_data(graph, make, 0, aggregate, 0) == 0);
    node *selection[] = { make, check, aggregate };
    char *json = graph_save_nodes_json(graph, selection, 3);
    assert(json);
    int first = acount(graph->nodes);
    assert(graph_append_json(graph, json, 400, 200) == 0);
    free(json);
    assert(acount(graph->nodes) == first + 3);
    node *copy = NULL, *copied_check = NULL, *copied_aggregate = NULL;
    for (int i = first; i < acount(graph->nodes); i++) {
        node *n = graph->nodes[i];
        if (n->desc == make->desc) {
            copy = n;
        }
        if (n->desc == check->desc) {
            copied_check = n;
        }
        if (n->desc == aggregate->desc) {
            copied_aggregate = n;
        }
    }
    assert(copy && copied_check && copied_aggregate);
    assert(copy->id != make->id && copy->output_pins[0].id != whole_id);
    int copied_x = field(copy, copy->output_pins[0].id, "x");
    assert(copied_x >= 0 && copy->output_pins[copied_x].id != x_id);
    assert(copy->output_pins[copied_x].field_source != whole_id);
    node *producer;
    int output;
    assert(graph_input_source(graph, copied_check, 0, &producer, &output));
    assert(producer == copy && output == copied_x);
    assert(graph_input_source(graph, copied_aggregate, 0, &producer, &output));
    assert(producer == copy && output == 0);
    number(graph, make, 0, -100);
    number(graph, make, 1, -200);
    compile(asset);
    arena memory;
    arena_init(&memory, 16);
    eval_context context;
    eval_context_init(&context);
    pin_value value;
    assert(graph_eval_output(graph, copied_check, 0, &memory, &value, &context) == 0);
    assert(value.type == PIN_BOOL && value.b);
    assert(graph_eval_output(graph, copy, copied_x, &memory, &value, &context) == 0);
    assert(value.type == PIN_F32 && value.f == 7);
    assert(graph_eval_output(graph, copied_aggregate, 0, &memory, &value, &context) == 0);
    assert(value.type == PIN_STRUCT && strcmp(value.type_name, "GameInput") == 0);
    const GameInput *input = value.ptr;
    assert(input->move.x == 7 && input->move.y == 9);
    eval_context_free(&context);
    arena_free(&memory);
    behavior_asset_free(asset);
}

static void verify_break_fields(behavior_asset *asset, uint64_t break_id)
{
    node *split = graph_node_by_id(asset->graph, break_id);
    assert(split);
    int x = field(split, split->output_pins[0].id, "x");
    assert(x >= 0);
    arena memory;
    arena_init(&memory, 16);
    eval_context context;
    eval_context_init(&context);
    pin_value value;
    assert(graph_eval_output(asset->graph, split, x, &memory, &value, &context) == 0);
    assert(value.type == PIN_F32 && value.f == 3);
    assert(graph_eval_output(asset->graph, split, 0, &memory, &value, &context) == 0);
    assert(value.type == PIN_STRUCT && strcmp(value.type_name, "CF_V2") == 0);
    const CF_V2 *vector = value.ptr;
    assert(vector->x == 3 && vector->y == 4);
    eval_context_free(&context);
    arena_free(&memory);
}

static void break_fields()
{
    behavior_asset *asset = behavior_asset_new("Break fields");
    graph *graph = asset->graph;
    node *vector = add(graph, "Make_CF_V2");
    number(graph, vector, 0, 3);
    number(graph, vector, 1, 4);
    node *input = add(graph, "Make_GameInput");
    node *split = add(graph, "Break_GameInput");
    assert(graph_connect_data(graph, vector, 0, input, 0) == 0);
    assert(graph_connect_data(graph, input, 0, split, 0) == 0);
    uint64_t whole_id = split->output_pins[0].id;
    assert(graph_expose_output_fields(graph, split, 0) == 0);
    assert(split->output_count == 3 && split->output_pins[0].id == whole_id);
    compile(asset);
    verify_break_fields(asset, split->id);
    char *json = behavior_asset_save_json(asset);
    assert(json);
    behavior_asset *loaded = behavior_asset_load_json(json);
    assert(loaded);
    free(json);
    compile(loaded);
    verify_break_fields(loaded, split->id);
    behavior_asset_free(loaded);
    behavior_asset_free(asset);
}

void test_field_outputs()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameTransform_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform_get(world, entity) = (GameTransform){ 17, -4 };
    exposed_reads(world, entity);
    set_snapshots(world, entity);
    system_fields(world, entity);
    invalid_field_metadata();
    copied_fields();
    break_fields();
    ecs_world_free(world);
    puts("PASS: direct output fields preserve aggregate wires, stable IDs, serialized reads and Set snapshots");
}
