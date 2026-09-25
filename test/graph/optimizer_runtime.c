#include "graph/authoring/optimize.h"
#include "graph/document/extract.h"
#include "graph/model/local.h"
#include "graph/shader/shader_graph.h"
#include "runtime/systems/system.h"
#include "test/suite_internal.h"

#include <assert.h>

static uint64_t variable(behavior_asset *asset, const char *name)
{
    behavior_variable *v = behavior_asset_add_variable(
        asset,
        name,
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .type_name = "float" },
        NULL
    );
    assert(v);
    return v->id;
}

static node *setter(behavior_asset *a, uint64_t id)
{
    return behavior_asset_add_set_node(a, id);
}
static void exec_edge(graph *g, node *a, int output, node *b)
{
    assert(graph_connect_exec(g, a, output, b, 0, 0) == 0);
}
static void data_edge(graph *g, node *a, int output, node *b, int input)
{
    assert(graph_connect_data(g, a, output, b, input) == 0);
}
static void default_value(graph *g, node *n, int pin, pin_value value)
{
    assert(graph_set_pin_default(g, n, 0, pin, &value) == 0);
}
static node *add(graph *g, const char *name)
{
    const node_desc *d = registry_node_by_name(name);
    assert(d);
    return graph_add_node(g, d);
}

static void activation_differential()
{
    behavior_asset *asset = behavior_asset_new("Optimizer timing");
    graph *g = asset->graph;
    uint64_t x = variable(asset, "X");
    uint64_t first = variable(asset, "First"),
             second = variable(asset, "Second"), third = variable(asset, "Third");
    node *event = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, event, "Go");
    node *loop = graph_add_node(g, &NODE_ForLoop);
    default_value(g, loop, 2, (pin_value){ .type = PIN_I32, .i = 1 });
    node *set_x = setter(asset, x), *first_set = setter(asset, first),
         *write = setter(asset, x);
    node *second_set = setter(asset, second), *third_set = setter(asset, third);
    node *branch = graph_add_node(g, &NODE_Branch),
         *delay = graph_add_node(g, &NODE_Delay);
    default_value(g, branch, 1, (pin_value){ .type = PIN_BOOL, .b = true });
    default_value(g, delay, 1, (pin_value){ .type = PIN_F64, .d = 0.1 });
    default_value(g, write, 1, (pin_value){ .type = PIN_F32, .f = 9 });
    exec_edge(g, event, 0, loop);
    exec_edge(g, loop, 0, set_x);
    exec_edge(g, set_x, 0, branch);
    exec_edge(g, branch, 0, first_set);
    exec_edge(g, first_set, 0, write);
    exec_edge(g, write, 0, delay);
    exec_edge(g, delay, 0, second_set);
    exec_edge(g, second_set, 0, third_set);
    assert(graph_connect_data_conversion(g, loop, 1, set_x, 1, &NODE_To_Real) == 0);
    node *get = behavior_asset_add_get_node(asset, x);
    node *delta = graph_add_node(g, &NODE_DeltaTime);
    node *sum = add(g, "Add");
    data_edge(g, get, 0, sum, 0);
    data_edge(g, delta, 0, sum, 1);
    data_edge(g, sum, 0, first_set, 1);
    data_edge(g, sum, 0, second_set, 1);
    data_edge(g, sum, 0, third_set, 1);
    uint64_t sum_id = sum->id;
    char *json = behavior_asset_save_json(asset);
    behavior_asset *original = behavior_asset_load_json(json);
    free(json);
    assert(original);
    graph_optimize_report report = { 0 };
    assert(graph_optimize(g, NULL, &report) == 3);
    test_check(
        report.folded_inputs == 2 && report.created_aliases == 1 &&
            acount(asset->variables) == 4,
        "variable and Delta Time folds compose with a computed local without "
        "adding state"
    );
    graph_optimize_report_free(&report);
    // Add watches after optimization to exercise local traversal and capture.
    graph_set_pin_watch(g, sum, 1, 0, 1);
    graph_set_pin_watch(original->graph, graph_node_by_id(original->graph, sum_id), 1, 0, 1);
    behavior_instance *instances[2] = {
        behavior_instance_create(original, NULL, NULL, NULL),
        behavior_instance_create(asset, NULL, NULL, NULL)
    };
    flow_run *runs[2];
    for (int i = 0; i < 2; i++) {
        assert(instances[i]);
        behavior_instance_set_delta_seconds(instances[i], .25);
        flow_error error = { 0 };
        runs[i] = behavior_instance_dispatch(instances[i], "Go", (flow_inputs){ 0 }, &error);
        assert(runs[i]);
        assert(flow_run_tick_at(runs[i], 0) >= 0);
        test_check(flow_run_status(runs[i]) == FLOW_SUSPENDED, "loop suspends on its authored delay");
    }
    for (int step = 1; step <= 3; step++) {
        for (int i = 0; i < 2; i++) {
            behavior_instance_set_delta_seconds(instances[i], .25 + step * .5);
            if (flow_run_status(runs[i]) != FLOW_COMPLETED) {
                assert(flow_run_tick_at(runs[i], step) >= 0);
            }
        }
        uint64_t ids[] = { x, first, second, third };
        for (int p = 0; p < 4; p++) {
            pin_value a, b;
            assert(!behavior_instance_get_variable(instances[0], ids[p], &a));
            assert(!behavior_instance_get_variable(instances[1], ids[p], &b));
            test_check(a.f == b.f, "original and optimized activations agree across branches, loops, writes, time changes, and delays");
        }
    }
    test_check(
        flow_run_status(runs[0]) == FLOW_COMPLETED &&
            flow_run_status(runs[1]) == FLOW_COMPLETED,
        "both graphs complete the same loop"
    );
    int count = 0;
    const flow_traversal *traversals = flow_run_traversals(runs[1], &count);
    bool seen = false;
    for (int i = 0; i < count; i++) {
        seen |= traversals[i].kind == GRAPH_EDGE_DATA &&
                traversals[i].from_id == sum_id &&
                traversals[i].to_id == first_set->id;
    }
    pin_value a, b;
    test_check(
        seen &&
            !flow_run_watch_output(runs[0], graph_node_by_id(original->graph, sum_id), 0, &a) &&
            !flow_run_watch_output(runs[1], sum, 0, &b) && a.f == b.f,
        "local traversal and watched output retain original node and pin "
        "provenance"
    );
    for (int i = 0; i < 2; i++) {
        behavior_instance_destroy(instances[i]);
    }
    behavior_asset_free(original);
    behavior_asset_free(asset);
}

static void extraction()
{
    behavior_asset *asset = behavior_asset_new("Local extraction");
    graph *g = asset->graph;
    node *sum = add(g, "Add"), *a = add(g, "Add"), *b = add(g, "Add"),
         *c = add(g, "Add");
    default_value(g, sum, 0, (pin_value){ .type = PIN_F32, .f = 11 });
    data_edge(g, sum, 0, a, 0);
    data_edge(g, sum, 0, b, 0);
    data_edge(g, sum, 0, c, 0);
    assert(graph_create_local(g, sum, 0, "Sum"));
    node *end = add(g, "Add");
    data_edge(g, a, 0, end, 0);
    node *selection[] = { a };
    uint64_t call = 0;
    behavior_asset *extracted = NULL;
    const char *error = behavior_asset_extract_function(asset, g, selection, 1, "Part", &extracted, &call);
    if (error) {
        fprintf(stderr, "extraction: %s\n", error);
    }
    test_check(!error && !graph_validate_locals(g), "extracting a local consumer turns its boundary dependency into a function input");
    char *json = behavior_asset_save_json(extracted ? extracted : asset);
    behavior_asset_free(extracted);
    behavior_asset *roundtrip = behavior_asset_load_json(json);
    test_check(roundtrip != NULL, "extraction with locals survives serialization");
    behavior_asset_free(roundtrip);
    free(json);
    node *group[] = { sum, a, b };
    extracted = NULL;
    error = behavior_asset_extract_function(asset, g, group, 3, "Group", &extracted, &call);
    bool valid = !error && extracted;
    for (int i = 0; valid && i < acount(extracted->graphs); i++) {
        valid &= !graph_validate_locals(extracted->graphs[i]);
    }
    test_check(valid, "extraction remaps internal locals and expands locals crossing the result boundary");
    behavior_asset_free(extracted);
    behavior_asset_free(asset);
}

static void shader_locals()
{
    behavior_asset *asset = behavior_asset_new("Shader locals");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *constant = add(g, "Shader Constant");
    shader_graph_init_node(g, constant);
    assert(graph_set_pin_type(g, constant, 1, 0, PIN_F32, "float") == 0);
    assert(
        graph_set_pin_default(g, constant, 1, 0, &(pin_value){ .type = PIN_F32, .f = .3f }) == 0
    );
    node *output = add(g, "Shader Surface Output");
    shader_graph_init_node(g, output);
    const int channels[] = { 2, 3, 5 };
    for (int i = 0; i < 3; i++) {
        data_edge(g, constant, 0, output, channels[i]);
    }
    shader_graph before, after;
    shader_graph_init(&before);
    shader_graph_init(&after);
    shader_error error;
    bool valid = shader_graph_from_graph(g, &before, &error);
    if (!valid) {
        fprintf(stderr, "shader local fixture: %s\n", error.message);
    }
    assert(valid);
    graph_optimize_report report = { 0 };
    assert(graph_optimize(g, NULL, &report) == 1);
    assert(shader_graph_from_graph(g, &after, &error));
    shader_sample_context sample = { 0 };
    float a[8][4], b[8][4];
    assert(shader_graph_eval_outputs(&before, &sample, NULL, a, &error));
    assert(shader_graph_eval_outputs(&after, &sample, NULL, b, &error));
    test_check(!memcmp(a, b, sizeof a) && acount(before.nodes) == acount(after.nodes), "shader locals lower through the original scope cache and preserve output samples");
    graph_optimize_report_free(&report);
    shader_graph_free(&before);
    shader_graph_free(&after);
    behavior_asset_free(asset);
}

static void system_locals()
{
    behavior_asset *asset = behavior_asset_new("Optimizer system");
    behavior_asset_set_owner_type(asset, "ecs.entity");
    graph *g = asset->graph;
    node *source = add(g, "Add");
    default_value(g, source, 0, (pin_value){ .type = PIN_F32, .f = 3 });
    node *uses[3];
    for (int i = 0; i < 3; i++) {
        uses[i] = add(g, "Add");
        data_edge(g, source, 0, uses[i], 0);
    }
    node *sum = add(g, "Add"), *total = add(g, "Add");
    data_edge(g, uses[0], 0, sum, 0);
    data_edge(g, uses[1], 0, sum, 1);
    data_edge(g, sum, 0, total, 0);
    data_edge(g, uses[2], 0, total, 1);
    node *make = add(g, "Make_GameMovement"), *set = add(g, "Set_GameMovement"),
         *event = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, event, "Tick");
    data_edge(g, total, 0, make, 0);
    data_edge(g, make, 0, set, 1);
    exec_edge(g, event, 0, set);
    char *json = behavior_asset_save_json(asset);
    behavior_asset *optimized = behavior_asset_load_json(json);
    free(json);
    graph_optimize_report report = { 0 };
    assert(graph_optimize(optimized->graph, NULL, &report) == 1);
    graph_optimize_report_free(&report);
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    GameMovement movement = { 0 };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameMovement.id, &movement, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_system *systems[] = { game_system_new(world, asset),
                               game_system_new(world, optimized) };
    for (int i = 0; i < 2; i++) {
        assert(systems[i] && !compile_report_blocked(game_system_compile(systems[i])));
        test_check(game_system_uses_bytecode(systems[i]), "system with local dependencies retains compiled bytecode support");
        for (int mode = GAME_SYSTEM_EXEC_GENERIC_BYTECODE; mode <= GAME_SYSTEM_EXEC_SJLIT;
             mode++) {
            if (mode == GAME_SYSTEM_EXEC_TYPED_BYTECODE &&
                !game_system_uses_typed_bytecode(systems[i])) {
                continue;
            }
            if (mode == GAME_SYSTEM_EXEC_SJLIT && !game_system_uses_sljit(systems[i])) {
                continue;
            }
            GAME_GameMovement_get(world, entity)->speed = 0;
            flow_error error = { 0 };
            assert(game_system_tick_mode(systems[i], 0, .1, &error, mode) == 0);
            test_check(GAME_GameMovement_get(world, entity)->speed == 9, "system register lowering evaluates local fanout identically in each available backend");
        }
        game_system_free(systems[i]);
    }
    ecs_world_free(world);
}

void test_optimizer_runtime()
{
    activation_differential();
    extraction();
    shader_locals();
    system_locals();
}
