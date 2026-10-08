#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/document/library.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/runtime.h"
#include "test/instance_bank_fixture.h"
#include "test/pending_fixture.h"
#include "test/suite_internal.h"
#include "test/whole_array_op_fixture.h"
#include "test/whole_function_fixture.h"
#include "test/whole_managed_fixture.h"
#include "test/whole_multigate_fixture.h"
#include "test/whole_name_fixture.h"
#include "test/whole_owned_array_fixture.h"
#include "test/whole_particle_fixture.h"
#include "test/whole_random_fixture.h"
#include "test/whole_reaction_fixture.h"
#include "test/whole_reflected_fixture.h"
#include "test/whole_retrigger_fixture.h"
#include "test/whole_rhythm_fixture.h"
#include "test/whole_search_fixture.h"
#include "test/whole_state_fixture.h"
#include "test/whole_switch_fixture.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static behavior_asset *counter_asset()
{
    behavior_asset *asset = behavior_asset_new("CompiledCounter");
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "Count",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, event, "Count") == 0);
    node *get = behavior_asset_add_get_node(asset, variable->id);
    node *add = graph_add_node(g, &NODE_Add);
    assert(graph_set_pin_type(g, add, 0, 0, PIN_F64, "double") == 0);
    assert(graph_set_pin_type(g, add, 0, 1, PIN_F64, "double") == 0);
    assert(graph_set_pin_type(g, add, 1, 0, PIN_F64, "double") == 0);
    node *first = behavior_asset_add_set_node(asset, variable->id);
    node *delay = graph_add_node(g, &NODE_Delay);
    node *last = behavior_asset_add_set_node(asset, variable->id);
    assert(graph_set_pin_default(g, add, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1 }) == 0);
    assert(graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1 }) == 0);
    assert(graph_connect_data(g, get, 0, add, 0) == 0);
    assert(graph_connect_data(g, add, 0, first, 1) == 0);
    assert(graph_connect_data(g, add, 0, last, 1) == 0);
    assert(graph_connect_exec(g, event, 0, first, 0, 0) == 0);
    assert(graph_connect_exec(g, first, 0, delay, 0, 0) == 0);
    assert(graph_connect_exec(g, delay, 0, last, 0, 0) == 0);
    return asset;
}

static void compiled_counter(behavior_backend backend)
{
    behavior_asset *asset = counter_asset();
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(instance, backend) == 0);
    if (backend != BEHAVIOR_ORACLE) {
        const behavior_graph_program *program = behavior_program_graph(
            behavior_program_cached(asset),
            asset->graph
        );
        node *event = NULL, *delay = NULL;
        for (int i = 0; i < acount(asset->graph->nodes); i++) {
            node *n = asset->graph->nodes[i];
            if (n->desc == &NODE_Event) {
                event = n;
            }
            if (n->desc == &NODE_Delay) {
                delay = n;
            }
        }
        assert(event && delay && !behavior_program_diagnostic(behavior_program_cached(asset)));
        const behavior_function_diagnostic *d = behavior_program_function_diagnostic(program, event);
        assert(!d && behavior_program_function(program, event));
    }
    uint64_t eval_before = eval_oracle_calls();
    uint64_t flow_before = flow_oracle_calls();
    flow_error error = { 0 };
    flow_run *first = behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error);
    if (!first) {
        fprintf(stderr, "compiled counter: %s\n", error.message);
    }
    assert(first && flow_run_status(first) == FLOW_SUSPENDED);
    assert(flow_run_backend(first) == backend);
    flow_run *second = behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error);
    assert(second && flow_run_status(second) == FLOW_SUSPENDED);
    pin_value value;
    assert(
        behavior_instance_get_variable(instance, asset->variables[0].id, &value) == 0 &&
        value.d == 2
    );
    assert(behavior_instance_tick(instance, .5) == FLOW_OK);
    assert(behavior_instance_tick(instance, 1) == FLOW_OK);
    assert(flow_run_status(first) == FLOW_COMPLETED && flow_run_status(second) == FLOW_COMPLETED);
    assert(
        behavior_instance_get_variable(instance, asset->variables[0].id, &value) == 0 &&
        value.d == 4
    );
    assert(behavior_instance_release_activation(instance, first) == 0);
    assert(behavior_instance_release_activation(instance, second) == 0);
    behavior_instance_set_trace_enabled(instance, 0);
    first = behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error);
    assert(first && flow_run_status(first) == FLOW_SUSPENDED);
    int traversals = 0;
    flow_run_traversals(first, &traversals);
    assert(traversals == 0);
    assert(behavior_instance_stop_activation(instance, first) == FLOW_OK);
    assert(behavior_instance_tick(instance, 2) == FLOW_OK);
    assert(
        behavior_instance_get_variable(instance, asset->variables[0].id, &value) == 0 &&
        value.d == 5
    );
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval_oracle_calls() == eval_before && flow_oracle_calls() == flow_before);
        const graph_ir *ir = behavior_graph_ir(
            behavior_program_graph(behavior_program_cached(asset), asset->graph)
        );
        assert(acount(ir->blocks) && acount(ir->values) && acount(ir->control));
        for (int i = 0; i < acount(ir->blocks); i++) {
            assert(ir->blocks[i].graph_id == asset->graph->graph_id);
            assert(ir->blocks[i].node_id && ir->blocks[i].pin_id);
        }
    } else {
        assert(eval_oracle_calls() > eval_before && flow_oracle_calls() > flow_before);
    }
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void whole_function_set_watches(behavior_backend backend)
{
    if (backend == BEHAVIOR_SLJIT && !graph_sljit_available()) {
        return;
    }

    pin_value initial_items[] = { { .type = PIN_F64, .type_name = "double", .d = 10.5 } };
    pin_array initial_set = { .element_type = PIN_F64,
                              .type_name = "double",
                              .count = 1,
                              .capacity = 1,
                              .items = initial_items };
    pin_value initial = { .type = PIN_F64,
                          .type_name = "double",
                          .container = PIN_CONTAINER_SET,
                          .ptr = &initial_set };
    pin_value item = { .type = PIN_I32, .type_name = "int", .i = 7 };
    pin_value offset = { .type = PIN_F64, .type_name = "double", .d = 0.5 };
    behavior_asset *asset = behavior_asset_new("Watched owned set append");
    behavior_variable *set_variable = behavior_asset_add_variable(
        asset,
        "Items",
        PIN_F64,
        "double",
        PIN_CONTAINER_SET,
        &initial,
        ""
    );
    behavior_variable *item_variable = behavior_asset_add_variable(
        asset,
        "Next",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &item,
        ""
    );
    behavior_variable *offset_variable = behavior_asset_add_variable(
        asset,
        "Offset",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        &offset,
        ""
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Append"));
    node *get_set = behavior_asset_add_get_node(asset, set_variable->id);
    node *get_item = behavior_asset_add_get_node(asset, item_variable->id);
    node *get_offset = behavior_asset_add_get_node(asset, offset_variable->id);
    node *sum = graph_add_node(g, &NODE_Add);
    assert(sum);
    for (int pin = 0; pin < 2; pin++) {
        assert(!graph_set_pin_type(g, sum, 0, pin, PIN_F64, "double"));
    }
    assert(!graph_set_pin_type(g, sum, 1, 0, PIN_F64, "double"));
    node *add = graph_add_node(g, registry_node_by_name("Set Add"));
    assert(add);
    assert(!graph_set_pin_type(g, add, 0, 0, PIN_F64, "double"));
    assert(!graph_set_pin_container(g, add, 0, 0, PIN_CONTAINER_SET));
    assert(!graph_set_pin_type(g, add, 0, 1, PIN_F64, "double"));
    assert(!graph_set_pin_type(g, add, 1, 0, PIN_F64, "double"));
    assert(!graph_set_pin_container(g, add, 1, 0, PIN_CONTAINER_SET));
    node *set = behavior_asset_add_set_node(asset, set_variable->id);
    assert(!graph_connect_data(g, get_set, 0, add, 0));
    assert(!graph_connect_data_conversion(g, get_item, 0, sum, 0, &NODE_To_Real));
    assert(graph_input_conversion(g, sum, 0) == &NODE_To_Real);
    assert(!graph_connect_data(g, get_offset, 0, sum, 1));
    assert(!graph_connect_data(g, sum, 0, add, 1));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));

    assert(behavior_function_owned_set_add_candidate(asset, g, add));
    assert(!graph_set_pin_watch(g, sum, 0, 0, 1));
    assert(!graph_set_pin_watch(g, sum, 1, 0, 1));
    assert(!graph_set_pin_watch(g, add, 0, 1, 1));
    assert(!behavior_function_owned_set_add_candidate(asset, g, add));
    assert(!graph_set_pin_watch(g, get_item, 1, 0, 1));
    assert(!graph_set_pin_watch(g, get_set, 1, 0, 1));
    assert(!graph_set_pin_watch(g, add, 0, 0, 1));
    assert(!graph_set_pin_watch(g, add, 1, 0, 1));
    assert(!graph_set_pin_watch(g, set, 0, 1, 1));
    assert(!graph_set_pin_watch(g, set, 1, 1, 1));

    behavior_program_publish(asset);
    const behavior_function *function = behavior_program_function(
        behavior_program_graph(behavior_program_cached(asset), g),
        event
    );
    assert(function);
    for (int i = 0; i < acount(function->instructions); i++) {
        const behavior_function_instruction *instruction = &function->instructions[i];
        assert(instruction->op != BF_STORE_VALUE || instruction->offset != BF_STORE_VALUE_SET_ADD);
    }

    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    behavior_instance_set_trace_enabled(instance, 1);
    arena snapshot_storage = { 0 };
    pin_value before;
    assert(!behavior_instance_copy_variable(instance, set_variable->id, &snapshot_storage, &before));
    const pin_array *snapshot = before.ptr;
    assert(snapshot && snapshot->count == 1 && snapshot->items[0].d == 10.5);

    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Append", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value watched;
    assert(flow_run_watch_output(run, get_set, 0, &watched) == 0);
    const pin_array *getter_value = watched.ptr;
    assert(watched.type == PIN_F64 && watched.container == PIN_CONTAINER_SET);
    assert(getter_value && getter_value->count == 1 && getter_value->items[0].d == 10.5);

    assert(flow_run_watch_input(run, add, 0, &watched) == 0);
    const pin_array *add_set_input = watched.ptr;
    assert(watched.type == PIN_F64 && watched.container == PIN_CONTAINER_SET);
    assert(add_set_input && add_set_input->count == 1 && add_set_input->items[0].d == 10.5);
    assert(flow_run_watch_output(run, get_item, 0, &watched) == 0);
    assert(watched.type == PIN_I32 && watched.i == 7);
    assert(flow_run_watch_input(run, sum, 0, &watched) == 0);
    assert(watched.type == PIN_F64 && watched.d == 7.0);
    assert(flow_run_watch_output(run, sum, 0, &watched) == 0);
    assert(watched.type == PIN_F64 && watched.d == 7.5);
    assert(flow_run_watch_input(run, add, 1, &watched) == 0);
    assert(watched.type == PIN_F64 && watched.d == 7.5);

    assert(flow_run_watch_output(run, add, 0, &watched) == 0);
    const pin_array *added = watched.ptr;
    assert(watched.type == PIN_F64 && watched.container == PIN_CONTAINER_SET);
    assert(
        added && added->count == 2 && added->items[0].d == 10.5 &&
        added->items[1].d == 7.5
    );
    assert(flow_run_watch_input(run, set, 1, &watched) == 0);
    const pin_array *set_input = watched.ptr;
    assert(watched.type == PIN_F64 && watched.container == PIN_CONTAINER_SET);
    assert(set_input && set_input->count == 2 && set_input->items[1].d == 7.5);
    assert(flow_run_watch_output(run, set, 1, &watched) == 0);
    const pin_array *set_output = watched.ptr;
    assert(watched.type == PIN_F64 && watched.container == PIN_CONTAINER_SET);
    assert(set_output && set_output->count == 2 && set_output->items[1].d == 7.5);

    const pin_array *current = instance->execution.variables[0].ptr;
    assert(
        current && current->count == 2 && current->items[0].d == 10.5 &&
        current->items[1].d == 7.5
    );
    assert(snapshot->count == 1 && snapshot->items[0].d == 10.5);
    arena_free(&snapshot_storage);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

typedef enum
{
    SET_WATCH_UNWATCHED,
    SET_WATCH_GETTER_OUTPUT,
    SET_WATCH_ADD_SET_INPUT,
    SET_WATCH_ADD_ITEM_INPUT,
    SET_WATCH_ADD_OUTPUT,
    SET_WATCH_SETTER_INPUT,
    SET_WATCH_SETTER_OUTPUT,
    SET_WATCH_ITEM_PRODUCER_OUTPUT,
    SET_WATCH_CASE_COUNT
} set_watch_case;

typedef struct
{
    behavior_asset *asset;
    behavior_variable *set_variable;
    node *event, *get_set, *sum, *add, *set;
} set_watch_fixture;

static set_watch_fixture whole_function_set_watch_fixture()
{
    pin_value initial_items[] = { { .type = PIN_F64, .type_name = "double", .d = 10.5 } };
    pin_array initial_set = { .element_type = PIN_F64,
                              .type_name = "double",
                              .count = 1,
                              .capacity = 1,
                              .items = initial_items };
    pin_value initial = { .type = PIN_F64,
                          .type_name = "double",
                          .container = PIN_CONTAINER_SET,
                          .ptr = &initial_set };
    pin_value item = { .type = PIN_I32, .type_name = "int", .i = 7 };
    pin_value offset = { .type = PIN_F64, .type_name = "double", .d = 0.5 };
    behavior_asset *asset = behavior_asset_new("Independent set pin watch");
    behavior_variable *set_variable = behavior_asset_add_variable(
        asset,
        "Items",
        PIN_F64,
        "double",
        PIN_CONTAINER_SET,
        &initial,
        ""
    );
    behavior_variable *item_variable = behavior_asset_add_variable(
        asset,
        "Next",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &item,
        ""
    );
    behavior_variable *offset_variable = behavior_asset_add_variable(
        asset,
        "Offset",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        &offset,
        ""
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(event && !graph_set_event_name(g, event, "Append"));
    node *get_set = behavior_asset_add_get_node(asset, set_variable->id);
    node *get_item = behavior_asset_add_get_node(asset, item_variable->id);
    node *get_offset = behavior_asset_add_get_node(asset, offset_variable->id);
    node *sum = graph_add_node(g, &NODE_Add);
    assert(sum);
    for (int pin = 0; pin < 2; pin++) {
        assert(!graph_set_pin_type(g, sum, 0, pin, PIN_F64, "double"));
    }
    assert(!graph_set_pin_type(g, sum, 1, 0, PIN_F64, "double"));
    node *add = graph_add_node(g, registry_node_by_name("Set Add"));
    assert(add);
    assert(!graph_set_pin_type(g, add, 0, 0, PIN_F64, "double"));
    assert(!graph_set_pin_container(g, add, 0, 0, PIN_CONTAINER_SET));
    assert(!graph_set_pin_type(g, add, 0, 1, PIN_F64, "double"));
    assert(!graph_set_pin_type(g, add, 1, 0, PIN_F64, "double"));
    assert(!graph_set_pin_container(g, add, 1, 0, PIN_CONTAINER_SET));
    node *set = behavior_asset_add_set_node(asset, set_variable->id);
    assert(!graph_connect_data(g, get_set, 0, add, 0));
    assert(!graph_connect_data_conversion(g, get_item, 0, sum, 0, &NODE_To_Real));
    assert(graph_input_conversion(g, sum, 0) == &NODE_To_Real);
    assert(!graph_connect_data(g, get_offset, 0, sum, 1));
    assert(!graph_connect_data(g, sum, 0, add, 1));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));
    assert(behavior_function_owned_set_add_candidate(asset, g, add));
    return (set_watch_fixture){ asset, set_variable, event, get_set,
                                sum,   add,          set };
}

static bool whole_function_has_owned_set_add(const behavior_function *function)
{
    for (int i = 0; i < acount(function->instructions); i++) {
        const behavior_function_instruction *instruction = &function->instructions[i];
        if (instruction->op == BF_STORE_VALUE &&
            instruction->offset == BF_STORE_VALUE_SET_ADD) {
            return true;
        }
    }
    return false;
}

static bool same_traversal_edge(const flow_traversal *a, const flow_traversal *b)
{
    return a->from_id == b->from_id && a->from_pin_id == b->from_pin_id &&
           a->to_id == b->to_id && a->to_pin_id == b->to_pin_id && a->kind == b->kind;
}

static bool same_traversal(const flow_traversal *a, const flow_traversal *b)
{
    return same_traversal_edge(a, b) && a->timestamp == b->timestamp &&
           a->activation == b->activation;
}

static const behavior_instance_activity *whole_function_find_activity(
    const behavior_instance *instance,
    const flow_traversal *traversal
)
{
    int capacity = 0;
    uint64_t generation = 0;
    const behavior_instance_activity
        *table = behavior_instance_activity_table(instance, &capacity, &generation);
    for (int i = 0; table && i < capacity; i++) {
        if (table[i].count && same_traversal_edge(&table[i].last, traversal)) {
            return &table[i];
        }
    }
    return NULL;
}

static void whole_function_assert_activity_matches(
    const behavior_instance *expected,
    const behavior_instance *actual
)
{
    int expected_capacity = 0, actual_capacity = 0;
    uint64_t expected_generation = 0, actual_generation = 0;
    const behavior_instance_activity *expected_table = behavior_instance_activity_table(
        expected,
        &expected_capacity,
        &expected_generation
    );
    const behavior_instance_activity *
        actual_table = behavior_instance_activity_table(actual, &actual_capacity, &actual_generation);
    int expected_used = 0, actual_used = 0;
    for (int i = 0; expected_table && i < expected_capacity; i++) {
        if (!expected_table[i].count) {
            continue;
        }
        expected_used++;
        const behavior_instance_activity
            *found = whole_function_find_activity(actual, &expected_table[i].last);
        assert(found && found->count == expected_table[i].count);
        assert(same_traversal(&found->last, &expected_table[i].last));
    }
    for (int i = 0; actual_table && i < actual_capacity; i++) {
        actual_used += actual_table[i].count != 0;
    }
    assert(expected_used == actual_used);
}

static void whole_function_owned_set_add_traversals()
{
    set_watch_fixture fixture = whole_function_set_watch_fixture();
    graph *g = fixture.asset->graph;
    assert(!graph_set_pin_watch(g, fixture.set, 0, 1, 1));
    assert(behavior_function_owned_set_add_candidate(fixture.asset, g, fixture.add));
    behavior_program_publish(fixture.asset);
    const behavior_function *function = behavior_program_function(
        behavior_program_graph(behavior_program_cached(fixture.asset), g),
        fixture.event
    );
    assert(function && function->pin_watches && whole_function_has_owned_set_add(function));

    const behavior_backend backends[] = {
        BEHAVIOR_ORACLE,
        BEHAVIOR_SLJIT,
        BEHAVIOR_BYTECODE,
    };
    flow_traversal oracle_traversals[BEHAVIOR_ASSET_INSTANCE_TRACE_MAX];
    int oracle_count = 0;
    behavior_instance *oracle_instance = NULL;
    for (int i = 0; i < (int)(sizeof backends / sizeof backends[0]); i++) {
        behavior_backend backend = backends[i];
        if (backend == BEHAVIOR_SLJIT && !graph_sljit_available()) {
            continue;
        }
        behavior_instance *instance = behavior_instance_create(fixture.asset, NULL, NULL, NULL);
        assert(instance && !behavior_instance_set_backend(instance, backend));
        behavior_instance_set_trace_enabled(instance, 1);
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(instance, "Append", (flow_inputs){ 0 }, &error);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        if (backend != BEHAVIOR_ORACLE) {
            assert(run->whole_function_executed && flow_run_backend(run) == backend);
            assert(whole_function_has_owned_set_add(function));
        }
        int count = 0;
        const flow_traversal *traversals = flow_run_traversals(run, &count);
        assert(count > 0 && count <= BEHAVIOR_ASSET_INSTANCE_TRACE_MAX);
        if (backend == BEHAVIOR_ORACLE) {
            oracle_count = count;
            memcpy(oracle_traversals, traversals, (size_t)count * sizeof *traversals);
            oracle_instance = instance;
        } else {
            if (count != oracle_count) {
                fprintf(stderr, "owned set trace mismatch for backend %d: oracle=%d compiled=%d\n", backend, oracle_count, count);
                for (int j = 0; j < oracle_count; j++) {
                    fprintf(
                        stderr,
                        "  oracle[%d] %llu:%llu -> %llu:%llu kind=%d "
                        "step=%lu\n",
                        j,
                        (unsigned long long)oracle_traversals[j].from_id,
                        (unsigned long long)oracle_traversals[j].from_pin_id,
                        (unsigned long long)oracle_traversals[j].to_id,
                        (unsigned long long)oracle_traversals[j].to_pin_id,
                        oracle_traversals[j].kind,
                        oracle_traversals[j].activation
                    );
                }
                for (int j = 0; j < count; j++) {
                    fprintf(
                        stderr,
                        "  compiled[%d] %llu:%llu -> %llu:%llu kind=%d "
                        "step=%lu\n",
                        j,
                        (unsigned long long)traversals[j].from_id,
                        (unsigned long long)traversals[j].from_pin_id,
                        (unsigned long long)traversals[j].to_id,
                        (unsigned long long)traversals[j].to_pin_id,
                        traversals[j].kind,
                        traversals[j].activation
                    );
                }
            }
            assert(count == oracle_count);
            for (int j = 0; j < count; j++) {
                assert(same_traversal(&traversals[j], &oracle_traversals[j]));
            }
            whole_function_assert_activity_matches(oracle_instance, instance);
            behavior_instance_destroy(instance);
        }
        if (backend == BEHAVIOR_ORACLE) {
            const flow_traversal *get_to_add = NULL, *add_to_set = NULL;
            for (int j = 0; j < count; j++) {
                const flow_traversal *traversal = &traversals[j];
                if (traversal->kind != GRAPH_EDGE_DATA) {
                    continue;
                }
                if (traversal->from_id == fixture.get_set->id &&
                    traversal->from_pin_id == fixture.get_set->output_pins[0].id &&
                    traversal->to_id == fixture.add->id &&
                    traversal->to_pin_id == fixture.add->input_pins[0].id) {
                    assert(!get_to_add);
                    get_to_add = traversal;
                }
                if (traversal->from_id == fixture.add->id &&
                    traversal->from_pin_id == fixture.add->output_pins[0].id &&
                    traversal->to_id == fixture.set->id &&
                    traversal->to_pin_id == fixture.set->input_pins[1].id) {
                    assert(!add_to_set);
                    add_to_set = traversal;
                }
            }
            assert(get_to_add && add_to_set);
            assert(add_to_set < get_to_add);
            assert(whole_function_find_activity(instance, get_to_add));
            assert(whole_function_find_activity(instance, add_to_set));
        }
    }
    assert(oracle_instance);
    behavior_instance_destroy(oracle_instance);
    behavior_asset_free(fixture.asset);

    set_watch_fixture untraced = whole_function_set_watch_fixture();
    behavior_program_publish(untraced.asset);
    const behavior_function *untraced_function = behavior_program_function(
        behavior_program_graph(
            behavior_program_cached(untraced.asset),
            untraced.asset->graph
        ),
        untraced.event
    );
    assert(untraced_function && whole_function_has_owned_set_add(untraced_function));
    behavior_instance *instance = behavior_instance_create(untraced.asset, NULL, NULL, NULL);
    assert(instance && !behavior_instance_set_backend(instance, BEHAVIOR_BYTECODE));
    behavior_instance_set_trace_enabled(instance, 0);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Append", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED && run->whole_function_executed);
    int traversal_count = 0;
    flow_run_traversals(run, &traversal_count);
    assert(traversal_count == 0);
    behavior_instance_destroy(instance);
    behavior_asset_free(untraced.asset);
}

static void assert_watched_f64_set(const pin_value *value, int count)
{
    assert(value->type == PIN_F64 && value->container == PIN_CONTAINER_SET);
    const pin_array *set = value->ptr;
    assert(set && set->count == count);
    if (count > 0) {
        assert(set->items[0].d == 10.5);
    }
    if (count > 1) {
        assert(set->items[1].d == 7.5);
    }
}

static void whole_function_set_watch_case(behavior_backend backend, set_watch_case watch_case)
{
    if (backend == BEHAVIOR_SLJIT && !graph_sljit_available()) {
        return;
    }
    set_watch_fixture fixture = whole_function_set_watch_fixture();
    graph *g = fixture.asset->graph;
    switch (watch_case) {
        case SET_WATCH_GETTER_OUTPUT:
            assert(!graph_set_pin_watch(g, fixture.get_set, 1, 0, 1));
            break;
        case SET_WATCH_ADD_SET_INPUT:
            assert(!graph_set_pin_watch(g, fixture.add, 0, 0, 1));
            break;
        case SET_WATCH_ADD_ITEM_INPUT:
            assert(!graph_set_pin_watch(g, fixture.add, 0, 1, 1));
            break;
        case SET_WATCH_ADD_OUTPUT:
            assert(!graph_set_pin_watch(g, fixture.add, 1, 0, 1));
            break;
        case SET_WATCH_SETTER_INPUT:
            assert(!graph_set_pin_watch(g, fixture.set, 0, 1, 1));
            break;
        case SET_WATCH_SETTER_OUTPUT:
            assert(!graph_set_pin_watch(g, fixture.set, 1, 1, 1));
            break;
        case SET_WATCH_ITEM_PRODUCER_OUTPUT:
            assert(!graph_set_pin_watch(g, fixture.sum, 1, 0, 1));
            break;
        case SET_WATCH_UNWATCHED:
        case SET_WATCH_CASE_COUNT: break;
    }

    bool expected_owned = watch_case == SET_WATCH_UNWATCHED ||
                          watch_case == SET_WATCH_SETTER_INPUT ||
                          watch_case == SET_WATCH_SETTER_OUTPUT ||
                          watch_case == SET_WATCH_ITEM_PRODUCER_OUTPUT;
    assert(behavior_function_owned_set_add_candidate(fixture.asset, g, fixture.add) == expected_owned);
    behavior_program_publish(fixture.asset);
    const behavior_function *function = behavior_program_function(
        behavior_program_graph(behavior_program_cached(fixture.asset), g),
        fixture.event
    );
    assert(function);
    assert(function->pin_watches == (watch_case != SET_WATCH_UNWATCHED));
    assert(whole_function_has_owned_set_add(function) == expected_owned);

    behavior_instance *instance = behavior_instance_create(fixture.asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    behavior_instance_set_trace_enabled(instance, watch_case != SET_WATCH_UNWATCHED);
    arena snapshot_storage = { 0 };
    pin_value before;
    assert(
        !behavior_instance_copy_variable(instance, fixture.set_variable->id, &snapshot_storage, &before)
    );
    const pin_array *snapshot = before.ptr;
    assert(snapshot && snapshot->count == 1 && snapshot->items[0].d == 10.5);

    uint64_t eval_before = eval_oracle_calls(), flow_before = flow_oracle_calls();
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Append", (flow_inputs){ 0 }, &error);
    if (!run || flow_run_status(run) != FLOW_COMPLETED || !run->whole_function_executed) {
        fprintf(
            stderr,
            "independent set watch case %d backend %d failed: run=%p status=%d "
            "whole=%d error=%d %s runtime_error=%d %s\n",
            watch_case,
            backend,
            (void *)run,
            run ? flow_run_status(run) : -1,
            run ? run->whole_function_executed : 0,
            error.code,
            error.message,
            run ? run->error.code : -1,
            run ? run->error.message : NULL
        );
    }
    assert(run && flow_run_status(run) == FLOW_COMPLETED && run->whole_function_executed);
    assert(flow_run_backend(run) == backend);
    assert(eval_oracle_calls() == eval_before && flow_oracle_calls() == flow_before);

    pin_value watched;
    switch (watch_case) {
        case SET_WATCH_GETTER_OUTPUT:
            assert(flow_run_watch_output(run, fixture.get_set, 0, &watched) == 0);
            assert_watched_f64_set(&watched, 1);
            break;
        case SET_WATCH_ADD_SET_INPUT:
            assert(flow_run_watch_input(run, fixture.add, 0, &watched) == 0);
            assert_watched_f64_set(&watched, 1);
            break;
        case SET_WATCH_ADD_ITEM_INPUT:
            assert(flow_run_watch_input(run, fixture.add, 1, &watched) == 0);
            assert(watched.type == PIN_F64 && watched.d == 7.5);
            break;
        case SET_WATCH_ADD_OUTPUT:
            assert(flow_run_watch_output(run, fixture.add, 0, &watched) == 0);
            assert_watched_f64_set(&watched, 2);
            break;
        case SET_WATCH_SETTER_INPUT:
            assert(flow_run_watch_input(run, fixture.set, 1, &watched) == 0);
            assert_watched_f64_set(&watched, 2);
            break;
        case SET_WATCH_SETTER_OUTPUT:
            assert(flow_run_watch_output(run, fixture.set, 1, &watched) == 0);
            assert_watched_f64_set(&watched, 2);
            break;
        case SET_WATCH_ITEM_PRODUCER_OUTPUT:
            assert(flow_run_watch_output(run, fixture.sum, 0, &watched) == 0);
            assert(watched.type == PIN_F64 && watched.d == 7.5);
            break;
        case SET_WATCH_UNWATCHED:
        case SET_WATCH_CASE_COUNT: break;
    }

    pin_value current;
    assert(!behavior_instance_get_variable(instance, fixture.set_variable->id, &current));
    assert_watched_f64_set(&current, 2);
    assert(snapshot->count == 1 && snapshot->items[0].d == 10.5);
    arena_free(&snapshot_storage);
    behavior_instance_destroy(instance);
    behavior_asset_free(fixture.asset);
}

static void whole_function_scalar_watch_case(behavior_backend backend, bool watch_output)
{
    if (backend == BEHAVIOR_SLJIT && !graph_sljit_available()) {
        return;
    }
    pin_value left_value = { .type = PIN_I32, .type_name = "int", .i = 10 };
    pin_value right_value = { .type = PIN_I32, .type_name = "int", .i = 5 };
    pin_value result_value = { .type = PIN_I32, .type_name = "int", .i = 0 };
    behavior_asset *asset = behavior_asset_new("Single scalar pin watch");
    behavior_variable *left = behavior_asset_add_variable(
        asset,
        "Left",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &left_value,
        ""
    );
    behavior_variable *right = behavior_asset_add_variable(
        asset,
        "Right",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &right_value,
        ""
    );
    behavior_variable *result = behavior_asset_add_variable(
        asset,
        "Result",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &result_value,
        ""
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(event && !graph_set_event_name(g, event, "Add"));
    node *get_left = behavior_asset_add_get_node(asset, left->id);
    node *get_right = behavior_asset_add_get_node(asset, right->id);
    node *add = graph_add_node(g, &NODE_Add);
    assert(add);
    for (int pin = 0; pin < 2; pin++) {
        assert(!graph_set_pin_type(g, add, 0, pin, PIN_I32, "int"));
    }
    assert(!graph_set_pin_type(g, add, 1, 0, PIN_I32, "int"));
    node *set = behavior_asset_add_set_node(asset, result->id);
    assert(!graph_connect_data(g, get_left, 0, add, 0));
    assert(!graph_connect_data(g, get_right, 0, add, 1));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));
    assert(!graph_set_pin_watch(g, add, watch_output, 0, 1));

    behavior_program_publish(asset);
    const behavior_function *function = behavior_program_function(
        behavior_program_graph(behavior_program_cached(asset), g),
        event
    );
    assert(function && function->pin_watches && function->watches);

    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    behavior_instance_set_trace_enabled(instance, 1);
    uint64_t eval_before = eval_oracle_calls(), flow_before = flow_oracle_calls();
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Add", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED && run->whole_function_executed);
    assert(flow_run_backend(run) == backend);
    assert(eval_oracle_calls() == eval_before && flow_oracle_calls() == flow_before);

    pin_value watched;
    if (watch_output) {
        assert(flow_run_watch_output(run, add, 0, &watched) == 0);
        assert(watched.type == PIN_I32 && watched.i == 15);
    } else {
        assert(flow_run_watch_input(run, add, 0, &watched) == 0);
        assert(watched.type == PIN_I32 && watched.i == 10);
    }
    assert(behavior_instance_get_variable(instance, result->id, &watched) == 0);
    assert(watched.type == PIN_I32 && watched.i == 15);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void whole_function_managed_string_watch(behavior_backend backend)
{
    if (backend == BEHAVIOR_SLJIT && !graph_sljit_available()) {
        return;
    }
    pin_value initial_items[] = {
        { .type = PIN_STR, .type_name = "const char *", .str = "seed" }
    };
    pin_array initial_set = { .element_type = PIN_STR,
                              .type_name = "const char *",
                              .count = 1,
                              .capacity = 1,
                              .items = initial_items };
    pin_value initial = { .type = PIN_STR,
                          .type_name = "const char *",
                          .container = PIN_CONTAINER_SET,
                          .ptr = &initial_set };
    pin_value item = { .type = PIN_STR, .type_name = "const char *", .str = "original" };
    behavior_asset *asset = behavior_asset_new("Managed string item watch");
    behavior_variable *set_variable = behavior_asset_add_variable(
        asset,
        "Items",
        PIN_STR,
        "const char *",
        PIN_CONTAINER_SET,
        &initial,
        ""
    );
    behavior_variable *item_variable = behavior_asset_add_variable(
        asset,
        "Next",
        PIN_STR,
        "const char *",
        PIN_CONTAINER_SINGLE,
        &item,
        ""
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(event && !graph_set_event_name(g, event, "Append"));
    node *get_set = behavior_asset_add_get_node(asset, set_variable->id);
    node *get_item = behavior_asset_add_get_node(asset, item_variable->id);
    node *add = graph_add_node(g, registry_node_by_name("Set Add"));
    assert(add);
    assert(!graph_set_pin_type(g, add, 0, 0, PIN_STR, "const char *"));
    assert(!graph_set_pin_container(g, add, 0, 0, PIN_CONTAINER_SET));
    assert(!graph_set_pin_type(g, add, 0, 1, PIN_STR, "const char *"));
    assert(!graph_set_pin_type(g, add, 1, 0, PIN_STR, "const char *"));
    assert(!graph_set_pin_container(g, add, 1, 0, PIN_CONTAINER_SET));
    node *set = behavior_asset_add_set_node(asset, set_variable->id);
    assert(!graph_connect_data(g, get_set, 0, add, 0));
    assert(!graph_connect_data(g, get_item, 0, add, 1));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));
    assert(!graph_set_pin_watch(g, get_item, 1, 0, 1));
    assert(behavior_function_owned_set_add_candidate(asset, g, add));

    behavior_program_publish(asset);
    const behavior_function *function = behavior_program_function(
        behavior_program_graph(behavior_program_cached(asset), g),
        event
    );
    assert(function && function->pin_watches && whole_function_has_owned_set_add(function));
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    behavior_instance_set_trace_enabled(instance, 1);
    uint64_t eval_before = eval_oracle_calls(), flow_before = flow_oracle_calls();
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Append", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED && run->whole_function_executed);
    assert(flow_run_backend(run) == backend);
    assert(eval_oracle_calls() == eval_before && flow_oracle_calls() == flow_before);

    pin_value watched;
    assert(flow_run_watch_output(run, get_item, 0, &watched) == 0);
    assert(watched.type == PIN_STR && !strcmp(watched.str, "original"));
    pin_value replacement = { .type = PIN_STR,
                              .type_name = "const char *",
                              .str = "replacement" };
    assert(!behavior_instance_set_variable(instance, item_variable->id, &replacement));
    assert(flow_run_watch_output(run, get_item, 0, &watched) == 0);
    assert(watched.type == PIN_STR && !strcmp(watched.str, "original"));
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void whole_function_scalar_watches(behavior_backend backend)
{
    if (backend == BEHAVIOR_SLJIT && !graph_sljit_available()) {
        return;
    }

    pin_value left_value = { .type = PIN_I32, .type_name = "int", .i = 10 };
    pin_value right_value = { .type = PIN_I32, .type_name = "int", .i = 5 };
    pin_value result_value = { .type = PIN_I32, .type_name = "int", .i = 0 };
    behavior_asset *asset = behavior_asset_new("Watched scalar expression");
    behavior_variable *left = behavior_asset_add_variable(
        asset,
        "Left",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &left_value,
        ""
    );
    behavior_variable *right = behavior_asset_add_variable(
        asset,
        "Right",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &right_value,
        ""
    );
    behavior_variable *result = behavior_asset_add_variable(
        asset,
        "Result",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &result_value,
        ""
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Add"));
    node *get_left = behavior_asset_add_get_node(asset, left->id);
    node *get_right = behavior_asset_add_get_node(asset, right->id);
    node *add = graph_add_node(g, &NODE_Add);
    assert(add);
    for (int pin = 0; pin < 2; pin++) {
        assert(!graph_set_pin_type(g, add, 0, pin, PIN_I32, "int"));
    }
    assert(!graph_set_pin_type(g, add, 1, 0, PIN_I32, "int"));
    node *set = behavior_asset_add_set_node(asset, result->id);
    assert(!graph_connect_data(g, get_left, 0, add, 0));
    assert(!graph_connect_data(g, get_right, 0, add, 1));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));

    assert(!graph_set_pin_watch(g, get_left, 1, 0, 1));
    assert(!graph_set_pin_watch(g, add, 0, 0, 1));
    assert(!graph_set_pin_watch(g, add, 1, 0, 1));
    assert(!graph_set_pin_watch(g, set, 0, 1, 1));
    assert(get_left->output_pins[0].flags & PIN_FLAG_WATCH);
    assert(add->input_pins[0].flags & PIN_FLAG_WATCH);

    behavior_program_publish(asset);
    const behavior_function *function = behavior_program_function(
        behavior_program_graph(behavior_program_cached(asset), g),
        event
    );
    assert(function);
    assert(function->watches);

    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    behavior_instance_set_trace_enabled(instance, 1);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Add", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);

    pin_value watched;
    assert(flow_run_watch_output(run, get_left, 0, &watched) == 0);
    assert(watched.type == PIN_I32 && watched.i == 10);
    assert(flow_run_watch_input(run, add, 0, &watched) == 0);
    assert(watched.type == PIN_I32 && watched.i == 10);
    assert(flow_run_watch_output(run, add, 0, &watched) == 0);
    assert(watched.type == PIN_I32 && watched.i == 15);
    assert(flow_run_watch_input(run, set, 1, &watched) == 0);
    assert(watched.type == PIN_I32 && watched.i == 15);
    assert(behavior_instance_get_variable(instance, result->id, &watched) == 0);
    assert(watched.type == PIN_I32 && watched.i == 15);

    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

void test_behavior_unavailable()
{
    assert(!graph_sljit_available());
    behavior_asset *asset = counter_asset();
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    uint64_t eval_before = eval_oracle_calls(), flow_before = flow_oracle_calls();
    flow_error error;
    assert(!behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error));
    assert(error.code == FLOW_ERR_COMPILE && strstr(error.message, "requires SLJIT"));
    assert(behavior_instance_backend(instance) == BEHAVIOR_UNAVAILABLE);
    assert(eval_oracle_calls() == eval_before && flow_oracle_calls() == flow_before);
    assert(behavior_instance_set_backend(instance, BEHAVIOR_ORACLE) == 0);
    assert(behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error));
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    puts("PASS: native JIT unavailable rejects dispatch without oracle fallback");
}

static void generation_guards()
{
    behavior_asset *asset = counter_asset();
    graph *function = behavior_asset_add_function(asset, "Value", 1);
    node *ret = graph_node_by_id(function, function->return_id);
    pin_spec value = { .name = "Value", .kind = PIN_DATA, .type = PIN_F64, .type_name = "double" };
    assert(graph_add_pin(function, ret, 0, &value) == 0);
    behavior_program_publish(asset);
    uint64_t before = behavior_asset_revision(asset);
    assert(
        graph_set_pin_default(function, ret, 0, 0, &(pin_value){ .type = PIN_F64, .d = 7 }) == 0
    );
    assert(behavior_asset_revision(asset) != before);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(behavior_instance_backend(instance) != BEHAVIOR_UNAVAILABLE);
    assert(
        graph_set_pin_default(function, ret, 0, 0, &(pin_value){ .type = PIN_F64, .d = 9 }) != 0
    );
    behavior_instance_destroy(instance);
    assert(
        graph_set_pin_default(function, ret, 0, 0, &(pin_value){ .type = PIN_F64, .d = 9 }) == 0
    );
    behavior_asset_free(asset);
}

static void numeric_regions(pin_type type)
{
    const char *name = type == PIN_F32 ? "float" : "double";
    behavior_asset *a = behavior_asset_new("NumericRegions");
    behavior_variable *v = behavior_asset_add_variable(a, "X", type, name, PIN_CONTAINER_SINGLE, NULL, "");
    uint64_t id = v->id;
    node *get = behavior_asset_add_get_node(a, id);
    const node_desc *ops[] = { &NODE_Add, &NODE_Subtract, &NODE_Multiply, &NODE_Divide };
    node *nodes[4];
    for (int i = 0; i < 4; i++) {
        nodes[i] = graph_add_node(a->graph, ops[i]);
        for (int j = 0; j < 2; j++) {
            assert(graph_set_pin_type(a->graph, nodes[i], 0, j, type, name) == 0);
        }
        assert(graph_set_pin_type(a->graph, nodes[i], 1, 0, type, name) == 0);
    }
    /* Shared values survive both left/right reuse and later operations. */
    assert(
        graph_set_pin_binding(
            a->graph,
            nodes[0],
            0,
            0,
            &(pin_binding){ .kind = PIN_BINDING_VARIABLE, .variable_id = id }
        ) == 0
    );
    assert(graph_connect_data(a->graph, get, 0, nodes[0], 1) == 0);
    assert(graph_connect_data(a->graph, nodes[0], 0, nodes[1], 0) == 0);
    assert(graph_connect_data(a->graph, get, 0, nodes[1], 1) == 0);
    assert(graph_connect_data(a->graph, nodes[0], 0, nodes[2], 0) == 0);
    assert(graph_connect_data(a->graph, nodes[1], 0, nodes[2], 1) == 0);
    assert(graph_connect_data(a->graph, nodes[2], 0, nodes[3], 0) == 0);
    assert(graph_connect_data(a->graph, nodes[0], 0, nodes[3], 1) == 0);
    node *sink = behavior_asset_add_set_node(a, id);
    assert(graph_connect_data(a->graph, nodes[3], 0, sink, 1) == 0);
    behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    const behavior_graph_program
        *program = behavior_program_graph(behavior_program_cached(a), a->graph);
    if (behavior_default_backend() == BEHAVIOR_SLJIT) {
        assert(behavior_program_input_is_direct(program, sink, 1));
    }
    eval_context ctx = { .instance = instance,
                         .runtime = runtime_store_create(a->graph) };
    arena storage = { 0 };
    double cases[] = { 0., -0., 1., -1., .125, 1e10, INFINITY, -INFINITY, NAN };
    for (int i = 0; i < 9; i++) {
        pin_value x = { .type = type, .type_name = name };
        if (type == PIN_F32) {
            x.f = (float)cases[i];
        } else {
            x.d = cases[i];
        }
        assert(behavior_instance_set_variable(instance, id, &x) == 0);
        pin_value expected;
        runtime_store_reset_memo(ctx.runtime);
        assert(graph_eval_output(a->graph, nodes[3], 0, &storage, &expected, &ctx) == 0);
        for (int mode = 0; mode < 3; mode++) {
            behavior_instance_set_trace_enabled(instance, mode == 2);
            runtime_store_reset_memo(ctx.runtime);
            pin_value actual;
            behavior_backend backend = mode == 1 ? BEHAVIOR_BYTECODE
                                                 : behavior_default_backend();
            assert(behavior_program_input(program, sink, 1, &storage, &actual, &ctx, backend) == 0);
            double want = type == PIN_F32 ? expected.f : expected.d;
            double got = type == PIN_F32 ? actual.f : actual.d;
            assert(
                (isnan(want) && isnan(got)) ||
                (want == got && signbit(want) == signbit(got))
            );
            pin_value stored = runtime_node_inputs(
                runtime_store_node(ctx.runtime, sink)
            )[1];
            assert(stored.type == type);
            assert(
                memcmp(&stored.d, &actual.d, type == PIN_F32 ? sizeof(float) : sizeof(double)) == 0
            );
        }
        behavior_instance_set_trace_enabled(instance, 0);
    }
    arena_free(&storage);
    runtime_store_free(ctx.runtime);
    behavior_instance_destroy(instance);
    behavior_asset_free(a);
}

static void whole_function_control(behavior_backend backend)
{
    behavior_asset *a = whole_function_fixture();
    graph *g = a->graph;
    uint64_t x = a->variables[0].id, y = a->variables[1].id;
    node *event = NULL, *loop = NULL, *branch = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->desc == &NODE_Event) {
            event = g->nodes[i];
        }
        if (g->nodes[i]->desc == registry_node_by_name("WhileLoop")) {
            loop = g->nodes[i];
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->desc == registry_node_by_name("Branch")) {
            branch = g->nodes[i];
        }
    }
    assert(event && loop && branch);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value actual;
    assert(behavior_instance_get_variable(bi, x, &actual) == 0 && actual.f == 4);
    assert(behavior_instance_get_variable(bi, y, &actual) == 0 && actual.f == 8);
    assert(flow_run_steps(run) == 14);
    if (backend != BEHAVIOR_ORACLE) {
        assert(!flow_run_input_value(run, loop, 1));
        behavior_instance_set_trace_enabled(bi, 1);
        flow_run *inspected = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
        assert(inspected && flow_run_status(inspected) == FLOW_COMPLETED);
        assert(
            flow_run_watch_input(inspected, branch, 1, &actual) == 0 && !actual.b
        );
        assert(flow_run_input_value(inspected, loop, 1));
        int count = 0;
        assert(flow_run_traversals(inspected, &count) && count > 0);
    }
    if (backend != BEHAVIOR_ORACLE) {
        const behavior_function *f = behavior_program_function(
            behavior_program_graph(behavior_program_cached(a), g),
            event
        );
        assert(
            f && !behavior_program_function_diagnostic(
                     behavior_program_graph(behavior_program_cached(a), g),
                     event
                 )
        );
        pin_value variables[2] = { { .type = PIN_F32 }, { .type = PIN_F32 } };
        behavior_function_context ctx = { .variables = variables, .budget = 5 };
        assert(behavior_function_run(f, &ctx, backend == BEHAVIOR_SLJIT) == FLOW_ERR_STEP_BUDGET);
        assert(ctx.steps == 5 && ctx.node_id == loop->id && variables[0].f == 1);
        behavior_instance_set_trace_enabled(bi, 0);
        flow_options options = { .instance = bi, .step_budget = 5 };
        flow_run *limited = flow_run_start(g, event, (flow_inputs){ 0 }, &options, &error);
        assert(limited && flow_run_resume(limited) == FLOW_ERR_STEP_BUDGET);
        assert(flow_run_current_node(limited) == loop && flow_run_error(limited)->node == loop);
        int count = 0;
        const flow_call_frame *frames = flow_run_call_stack(limited, &count);
        assert(count == 1 && frames[0].node_id == loop->id);
        flow_run_free(limited);
    }
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void whole_function_library(behavior_backend backend, bool effectful)
{
    behavior_asset *lib = behavior_asset_new("WholeMath");
    lib->domain = BEHAVIOR_DOMAIN_LIBRARY;
    pin_spec spec = { .name = "Value", .type = PIN_F64, .type_name = "double", .kind = PIN_DATA };
    graph *identity = behavior_asset_add_function(lib, "Identity", 1);
    node *entry = graph_node_by_id(identity, identity->entry_id);
    node *ret = graph_node_by_id(identity, identity->return_id);
    assert(graph_add_pin(identity, entry, 1, &spec) == 0);
    assert(graph_add_pin(identity, ret, 0, &spec) == 0);
    assert(graph_connect_data(identity, entry, 0, ret, 0) == 0);
    graph *wrapper = behavior_asset_add_function(lib, "Wrapper", 1);
    entry = graph_node_by_id(wrapper, wrapper->entry_id);
    ret = graph_node_by_id(wrapper, wrapper->return_id);
    assert(graph_add_pin(wrapper, entry, 1, &spec) == 0);
    assert(graph_add_pin(wrapper, ret, 0, &spec) == 0);
    node *inner = behavior_asset_add_function_call_node_to_graph(lib, wrapper, identity);
    assert(graph_connect_data(wrapper, entry, 0, inner, 0) == 0);
    assert(graph_connect_data(wrapper, inner, 0, ret, 0) == 0);
    graph *target = wrapper;
    if (effectful) {
        target = behavior_asset_add_function(lib, "Forward", 0);
        entry = graph_node_by_id(target, target->entry_id);
        ret = graph_node_by_id(target, target->return_id);
        assert(graph_add_pin(target, entry, 1, &spec) >= 0);
        assert(graph_add_pin(target, ret, 0, &spec) >= 0);
        node *call = behavior_asset_add_function_call_node_to_graph(lib, target, wrapper);
        assert(graph_connect_data(target, entry, 1, call, 0) == 0);
        assert(graph_connect_data(target, call, 0, ret, 1) == 0);
        assert(graph_connect_exec(target, entry, 0, ret, 0, 0) == 0);
    }
    behavior_asset *a = behavior_asset_new("WholeLibrary");
    uint64_t id = behavior_asset_add_variable(a, "Result", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, event, "Run") == 0);
    node *call = behavior_asset_add_function_call_node_to_graph(a, a->graph, target);
    node *set = behavior_asset_add_set_node(a, id);
    assert(
        graph_set_pin_default(a->graph, call, 0, effectful, &(pin_value){ .type = PIN_F64, .d = 42 }) == 0
    );
    assert(graph_connect_exec(a->graph, event, 0, effectful ? call : set, 0, 0) == 0);
    if (effectful) {
        assert(graph_connect_exec(a->graph, call, 0, set, 0, 0) == 0);
    }
    assert(graph_connect_data(a->graph, call, effectful, set, 1) == 0);
    const behavior_asset *catalog[] = { lib };
    char message[256];
    behavior_asset *linked = behavior_asset_link_libraries(a, catalog, 1, message, sizeof message);
    assert(linked);
    behavior_instance *bi = behavior_instance_create(linked, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
    assert(
        run && flow_run_status(run) == FLOW_COMPLETED &&
        flow_run_steps(run) == (effectful ? 3 : 2)
    );
    pin_value result;
    assert(behavior_instance_get_variable(bi, id, &result) == 0 && result.d == 42);
    if (backend != BEHAVIOR_ORACLE) {
        if (effectful) {
            assert(run->whole_function_executed && !acount(run->children));
        } else {
            node *linked_event = graph_node_by_id(linked->graph, event->id);
            assert(behavior_program_function(
                behavior_program_graph(behavior_program_cached(linked), linked->graph),
                linked_event
            ));
        }
    }
    if (!effectful) {
        node *linked_call = graph_node_by_id(linked->graph, call->id);
        arena storage = { 0 };
        eval_context ctx = { .instance = bi,
                             .runtime = runtime_store_create(linked->graph) };
        uint64_t allocations = runtime_store_creations();
        int status = backend == BEHAVIOR_ORACLE
                       ? graph_eval_output(linked->graph, linked_call, 0, &storage, &result, &ctx)
                       : behavior_program_output(
                             behavior_program_graph(
                                 behavior_program_cached(linked),
                                 linked->graph
                             ),
                             linked_call,
                             0,
                             &storage,
                             &result,
                             &ctx,
                             backend
                         );
        assert(!status && result.d == 42);
        assert(runtime_store_creations() - allocations == (backend == BEHAVIOR_ORACLE ? 2 : 0));
        runtime_store_free(ctx.runtime);
        arena_free(&storage);
    }
    behavior_instance_destroy(bi);
    behavior_asset_free(linked);
    behavior_asset_free(a);
    behavior_asset_free(lib);
}

static void assert_real(double actual, double expected)
{
    assert(isnan(expected) ? isnan(actual) : actual == expected);
    if (expected == 0) {
        assert(signbit(actual) == signbit(expected));
    }
}

static void whole_function_arguments(behavior_backend backend)
{
    behavior_asset *a = whole_arguments_fixture();
    /* A function with a result must reach Return to produce it. */
    graph *empty = behavior_asset_add_function(a, "Missing return", 0);
    pin_spec result = { .name = "Value",
                        .type = PIN_F64,
                        .type_name = "double",
                        .kind = PIN_DATA };
    assert(graph_add_pin(empty, graph_node_by_id(empty, empty->return_id), 0, &result) >= 0);
    node *missing = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, missing, "Missing") == 0);
    node *call = behavior_asset_add_function_call_node(a, empty);
    assert(graph_connect_exec(a->graph, missing, 0, call, 0, 0) == 0);
    graph *g = a->graphs[1];
    node *entry = graph_node_by_id(g, g->entry_id),
         *ret = graph_node_by_id(g, g->return_id);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    assert(behavior_instance_set_activation_pool(bi, 1) == 0);
    double cases[] = { NAN, INFINITY, -INFINITY,    -0.,
                       0.,  -1.e-40,  1.0000000001, 16777217. };
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            pin_value args[] = { { .type = PIN_F64, .d = cases[x] },
                                 { .type = PIN_F32, .f = (float)cases[y] },
                                 { .type = PIN_BOOL, .b = x % 2 } };
            flow_error error = { 0 };
            flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 3 }, &error);
            assert(run && flow_run_status(run) == FLOW_COMPLETED);
            float small = (float)args[0].d < args[1].f ? (float)args[0].d
                                                       : args[1].f;
            double wide = (double)args[1].f > args[0].d ? (double)args[1].f
                                                        : args[0].d;
            assert_real(bi->execution.variables[0].d, wide);
            assert_real(bi->execution.variables[1].f, small);
            assert(bi->execution.variables[2].b == args[2].b);
            if (backend == BEHAVIOR_ORACLE) {
                flow_run *leaf = run->children[0]->children[0];
                assert(flow_run_steps(leaf) == 3 && leaf->returned == ret);
            } else {
                assert(run->whole_function_executed && !acount(run->children));
                assert(flow_run_steps(run) == 5);
            }
            assert(behavior_instance_release_activation(bi, run) == 0);
        }
    }
    /* Calling the leaf directly exercises entry defaults and both argument
     * coercions; the normal call boundary has already normalized these. */
    pin_value args[] = { { .type = PIN_F32, .f = 5.5f },
                         { .type = PIN_F64, .d = -7.125 },
                         { .type = PIN_I32, .i = 0 } };
    flow_options options = { .instance = bi, .step_budget = 3 };
    for (int count = 0; count <= 3; count++) {
        flow_error error = { 0 };
        flow_run *run = flow_run_start(g, entry, (flow_inputs){ args, count }, &options, &error);
        assert(run && flow_run_resume(run) == FLOW_OK);
        assert_real(flow_run_input_value(run, ret, 1)->d, count ? 5.5 : 3.25);
        assert_real(flow_run_input_value(run, ret, 2)->f, count >= 2 ? -7.125 : -2.5);
        assert(flow_run_input_value(run, ret, 3)->b);
        flow_run_free(run);
    }
    options.step_budget = 2;
    flow_error error = { 0 };
    flow_run *limited = flow_run_start(g, entry, (flow_inputs){ 0 }, &options, &error);
    assert(limited && flow_run_resume(limited) == FLOW_ERR_STEP_BUDGET);
    assert(flow_run_error(limited)->node == ret);
    int count = 0;
    const flow_call_frame *stack = flow_run_call_stack(limited, &count);
    assert(count > 0 && stack[count - 1].node_id == ret->id);
    flow_run_free(limited);
    flow_run *failed = behavior_instance_dispatch(bi, "Missing", (flow_inputs){ 0 }, &error);
    assert(failed && flow_run_status(failed) == FLOW_FAILED);
    assert(flow_run_error(failed)->code == FLOW_ERR_INVALID_GRAPH);
    assert(strstr(flow_run_error(failed)->message, "without Return"));
    assert(behavior_instance_release_activation(bi, failed) == 0);
    behavior_instance_set_trace_enabled(bi, 1);
    flow_run *inspected = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
    assert(inspected && flow_run_status(inspected) == FLOW_COMPLETED);
    flow_run *leaf = inspected->children[0]->children[0];
    assert(!leaf->whole_function_executed);
    pin_value watched;
    assert(flow_run_watch_input(leaf, ret, 1, &watched) == 0 && watched.d == 3.25);
    assert(flow_run_traversals(leaf, &count) && count > 0);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void whole_function_comparisons()
{
    const char *names[] = { "Less",    "Less Or Equal",
                            "Greater", "Greater Or Equal",
                            "Equal",   "Not Equal" };
    float cases[] = { NAN, INFINITY, -INFINITY, -1, -0.f, 0, 1 };
    for (int op = 0; op < 6; op++) {
        behavior_asset *a = behavior_asset_new("WholeComparison");
        uint64_t x = behavior_asset_add_variable(a, "X", PIN_F32, "float", PIN_CONTAINER_SINGLE, NULL, "")
                         ->id;
        uint64_t y = behavior_asset_add_variable(a, "Result", PIN_BOOL, "bool", PIN_CONTAINER_SINGLE, NULL, "")
                         ->id;
        node *event = graph_add_node(a->graph, &NODE_Event);
        assert(graph_set_event_name(a->graph, event, "Run") == 0);
        node *get = behavior_asset_add_get_node(a, x);
        node *compare = graph_add_node(a->graph, registry_node_by_name(names[op]));
        node *set = behavior_asset_add_set_node(a, y);
        assert(graph_connect_data(a->graph, get, 0, compare, 0) == 0);
        assert(graph_connect_data(a->graph, compare, 0, set, 1) == 0);
        assert(graph_connect_exec(a->graph, event, 0, set, 0, 0) == 0);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, 0);
        assert(behavior_program_function(
            behavior_program_graph(behavior_program_cached(a), a->graph),
            event
        ));
        for (int mode = 0; mode < 3; mode++) {
            assert(
                behavior_instance_set_backend(
                    bi,
                    (behavior_backend[]){ behavior_default_backend(),
                                          BEHAVIOR_BYTECODE,
                                          BEHAVIOR_ORACLE }[mode]
                ) == 0
            );
            for (int i = 0; i < 7; i++) {
                float v = cases[i];
                bool want = (bool[]){ v < 0,  v <= 0, v > 0,
                                      v >= 0, v == 0, v != 0 }[op];
                assert(
                    behavior_instance_set_variable(bi, x, &(pin_value){ .type = PIN_F32, .f = v }) == 0
                );
                flow_error error = { 0 };
                flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
                assert(run && flow_run_status(run) == FLOW_COMPLETED);
                pin_value actual;
                assert(behavior_instance_get_variable(bi, y, &actual) == 0 && actual.b == want);
                assert(behavior_instance_release_activation(bi, run) == 0);
            }
        }
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
}

static void whole_function_scopes(behavior_backend backend)
{
    behavior_asset *a = whole_scopes_fixture();
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    assert(behavior_instance_set_activation_pool(bi, 1) == 0);
    for (int i = 0; i < 32; i++) {
        bi->execution.variables[0].f = i;
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        assert(bi->execution.variables[0].f == 99 && bi->execution.variables[1].f == i + 101);
        assert(flow_run_steps(run) == 4);
        if (backend != BEHAVIOR_ORACLE) {
            assert(run->whole_function_executed && !acount(run->children));
        }
        assert(behavior_instance_release_activation(bi, run) == 0);
    }
    node *event = NULL;
    for (int i = 0; i < acount(a->graph->nodes); i++) {
        if (a->graph->nodes[i]->desc == &NODE_Event) {
            event = a->graph->nodes[i];
        }
    }
    assert(event);
    graph *f = a->graphs[1];
    node *ret = graph_node_by_id(f, f->return_id);
    bi->execution.variables[0].f = 0;
    bi->execution.variables[1].f = 0;
    flow_options options = { .instance = bi, .step_budget = 3 };
    flow_error error = { 0 };
    flow_run *limited = flow_run_start(a->graph, event, (flow_inputs){ 0 }, &options, &error);
    assert(limited && flow_run_resume(limited) == FLOW_ERR_STEP_BUDGET);
    /* The first callee reaches its Return after using its own three steps. */
    assert(flow_run_steps(limited) == 2 && flow_run_error(limited)->steps == 3);
    assert(flow_run_error(limited)->node == ret && flow_run_current_node(limited) == ret);
    assert(bi->execution.variables[0].f == 99 && bi->execution.variables[1].f == 0);
    int count = 0;
    const flow_call_frame *frames = flow_run_call_stack(limited, &count);
    assert(count >= 2 && frames[count - 1].graph == f && frames[count - 1].node_id == ret->id);
    assert(frames[count - 2].graph == a->graph);
    flow_run_free(limited);
    options.call_depth = 64;
    bi->execution.variables[0].f = 0;
    limited = flow_run_start(a->graph, event, (flow_inputs){ 0 }, &options, &error);
    assert(limited && flow_run_resume(limited) == FLOW_ERR_STEP_BUDGET);
    assert(strstr(flow_run_error(limited)->message, "call depth"));
    assert(bi->execution.variables[0].f == 0 && flow_run_steps(limited) == 2);
    flow_run_free(limited);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void count_data_edge(const node *, int, const node *, int, void *user)
{
    (*(int *)user)++;
}

static void pure_unused_return(behavior_backend backend)
{
    behavior_asset *a = behavior_asset_new("Unused pure return");
    uint64_t result = behavior_asset_add_variable(a, "Result", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
    graph *f = behavior_asset_add_function(a, "Sample", 1);
    node *ret = graph_node_by_id(f, f->return_id);
    pin_spec output = { .name = "Value",
                        .kind = PIN_DATA,
                        .type = PIN_F64,
                        .type_name = "double",
                        .default_value = { .type = PIN_F64, .d = 42 } };
    assert(graph_add_pin(f, ret, 0, &output) >= 0);
    output.name = "Sample";
    assert(graph_add_pin(f, ret, 0, &output) >= 0);
    node *random = graph_add_node(f, registry_node_by_name("Random Real"));
    assert(graph_connect_data(f, random, 0, ret, 1) == 0);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, event, "Run") == 0);
    node *call = behavior_asset_add_function_call_node(a, f);
    node *set = behavior_asset_add_set_node(a, result);
    assert(graph_connect_data(a->graph, call, 0, set, 1) == 0);
    assert(graph_connect_exec(a->graph, event, 0, set, 0, 0) == 0);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    if (backend != BEHAVIOR_ORACLE) {
        assert(behavior_program_function(
            behavior_program_graph(behavior_program_cached(a), a->graph),
            event
        ));
        assert(!behavior_program_function_diagnostic(
            behavior_program_graph(behavior_program_cached(a), a->graph),
            event
        ));
    }
    srand(2718);
    (void)rand();
    int expected_next = rand();
    srand(2718);
    flow_error error = { 0 };
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(bi->execution.variables[0].d == 42 && rand() == expected_next);
    if (backend != BEHAVIOR_ORACLE) {
        assert(run->whole_function_executed);
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void whole_pure_calls(behavior_backend backend)
{
    behavior_asset *a = whole_pure_fixture();
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    node *call = a->graph->nodes[0];
    const behavior_graph_program
        *program = behavior_program_graph(behavior_program_cached(a), a->graph);
    graph *wrapper = a->graphs[2];
    node *inner = wrapper->nodes[2];
    assert(inner->desc->pure_kind == NODE_PURE_FUNCTION);
    if (backend != BEHAVIOR_ORACLE) {
        for (int i = 1; i <= 2; i++) {
            graph *f = a->graphs[i];
            assert(behavior_program_function(
                behavior_program_graph(behavior_program_cached(a), f),
                graph_node_by_id(f, f->entry_id)
            ));
        }
    }
    arena storage = { 0 };
    eval_context ctx = { .instance = bi,
                         .runtime = runtime_store_create(a->graph),
                         .backend = backend };
    double cases[] = { -0.,       0.,  .25,      -3.,      1.e-40,
                       16777217., NAN, INFINITY, -INFINITY };
    for (int inspect = 0; inspect < 2; inspect++) {
        int edges = 0;
        ctx.on_data_edge = inspect ? count_data_edge : NULL;
        ctx.on_data_edge_user = &edges;
        for (int depth_case = 0; depth_case < 4; depth_case++) {
            ctx.call_depth = (unsigned[]){ 0, 62, 63, 64 }[depth_case];
            for (int k = 0; k < 9; k++) {
                runtime_store_reset_memo(ctx.runtime);
                bi->execution.variables[0].d = cases[k];
                uint64_t allocated = runtime_store_creations(),
                         oracle = eval_oracle_calls();
                pin_value value;
                int result = backend == BEHAVIOR_ORACLE
                               ? graph_eval_output(a->graph, call, 0, &storage, &value, &ctx)
                               : behavior_program_output(program, call, 0, &storage, &value, &ctx, backend);
                if (depth_case >= 2) {
                    assert(result == NODE_EVAL_INVALID);
                    assert(ctx.status.node == (depth_case == 2 ? inner : call));
                    assert(ctx.status.pin == 0);
                    assert(strstr(ctx.status.message, "pure function call depth limit"));
                    continue;
                }
                assert(!result);
                assert_real(value.d, (cases[k] + .25) + cases[k] * 1.25);
                const pin_value *outputs = runtime_node_outputs_const(
                    runtime_store_node(ctx.runtime, call)
                );
                assert_real(outputs[1].d, (cases[k] + .25) * (cases[k] * 1.25));
                if (backend != BEHAVIOR_ORACLE) {
                    assert(oracle == eval_oracle_calls());
                    assert(runtime_store_creations() - allocated == (inspect ? 2 : 0));
                } else {
                    assert(runtime_store_creations() - allocated == 2);
                }
            }
        }
        assert(!inspect || edges > 0);
    }
    runtime_store_free(ctx.runtime);
    arena_free(&storage);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void dispatch_empty(behavior_instance *instance, const char *name)
{
    flow_error error;
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(instance, name, (flow_inputs){ 0 }, &error);
    assert(run && !error.code && flow_run_status(run) == FLOW_COMPLETED);
    if (instance->execution.backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    assert(!behavior_instance_release_activation(instance, run));
}
static void event_generations(behavior_backend backend)
{
    behavior_asset *asset = behavior_asset_new("Event generations");
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(!graph_set_event_name(asset->graph, event, "Old"));
    behavior_program_publish(asset);
    assert(!graph_set_event_name(asset->graph, event, "Current"));
    behavior_backend previous = behavior_debug_backend(BEHAVIOR_ORACLE);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_debug_backend(previous);
    assert(!behavior_instance_set_backend(instance, backend));
    assert(!behavior_instance_set_activation_pool(instance, true));
    flow_error error;
    assert(!behavior_instance_dispatch(instance, "Old", (flow_inputs){ 0 }, &error));
    assert(error.code == FLOW_ERR_UNKNOWN_EVENT);
    dispatch_empty(instance, "Current");

    behavior_asset *next = behavior_asset_new("Event generations");
    node *updated = graph_add_node(next->graph, &NODE_Event);
    assert(!graph_set_event_name(next->graph, updated, "Current"));
    behavior_program_publish(next);
    assert(!graph_set_event_name(next->graph, updated, "Updated"));
    assert(behavior_instance_can_update(instance, next) == INSTANCE_UPDATE_READY);
    behavior_instance_update(instance, next);
    behavior_asset_free(asset);
    assert(!behavior_instance_dispatch(instance, "Current", (flow_inputs){ 0 }, &error));
    assert(error.code == FLOW_ERR_UNKNOWN_EVENT);
    dispatch_empty(instance, "Updated");

    assert(!behavior_instance_pool_release(instance));
    assert(!graph_set_event_name(next->graph, updated, "Reused"));
    assert(behavior_instance_pool_reuse(instance, next, NULL, NULL, NULL) == instance);
    assert(!behavior_instance_dispatch(instance, "Updated", (flow_inputs){ 0 }, &error));
    assert(error.code == FLOW_ERR_UNKNOWN_EVENT);
    dispatch_empty(instance, "Reused");
    behavior_instance_destroy(instance);
    behavior_asset_free(next);
}
void test_compiled_behaviors()
{
    whole_function_scalar_watches(BEHAVIOR_SLJIT);
    whole_function_scalar_watches(BEHAVIOR_BYTECODE);
    whole_function_set_watches(BEHAVIOR_SLJIT);
    whole_function_set_watches(BEHAVIOR_BYTECODE);
    whole_function_owned_set_add_traversals();
    for (set_watch_case watch_case = SET_WATCH_UNWATCHED;
         watch_case < SET_WATCH_CASE_COUNT;
         watch_case++) {
        whole_function_set_watch_case(BEHAVIOR_SLJIT, watch_case);
        whole_function_set_watch_case(BEHAVIOR_BYTECODE, watch_case);
    }
    whole_function_scalar_watch_case(BEHAVIOR_SLJIT, false);
    whole_function_scalar_watch_case(BEHAVIOR_SLJIT, true);
    whole_function_scalar_watch_case(BEHAVIOR_BYTECODE, false);
    whole_function_scalar_watch_case(BEHAVIOR_BYTECODE, true);
    whole_function_managed_string_watch(BEHAVIOR_SLJIT);
    whole_function_managed_string_watch(BEHAVIOR_BYTECODE);
    test_pending_backend(BEHAVIOR_SLJIT);
    test_pending_backend(BEHAVIOR_BYTECODE);
    test_pending_backend(BEHAVIOR_ORACLE);
    test_behavior_batches();
    event_generations(BEHAVIOR_SLJIT);
    event_generations(BEHAVIOR_BYTECODE);
    event_generations(BEHAVIOR_ORACLE);
    test_compiled_components();
    test_compiled_loops();
    test_compiled_switches(NULL);
    test_compiled_states(NULL);
    test_compiled_multigate(NULL);
    test_compiled_conversions();
    test_compiled_selects();
    test_compiled_aliases();
    test_compiled_stores();
    test_managed_reuse();
    test_compiled_arrays();
    test_compiled_queries();
    test_compiled_commands();
    test_compiled_suspensions();
    test_compiled_delays();
    test_compiled_retrigger(NULL);
    instance_bank_smoke(BEHAVIOR_ORACLE);
    instance_bank_smoke(BEHAVIOR_BYTECODE);
    instance_bank_smoke(BEHAVIOR_SLJIT);
    test_compiled_recursion(NULL);
    test_compiled_async(NULL);
    test_compiled_aggregates();
    test_compiled_math();
    test_compiled_motion(NULL);
    test_compiled_random(NULL);
    test_compiled_data(NULL);
    test_compiled_material(NULL);
    test_compiled_particles(NULL);
    test_compiled_managed(NULL);
    test_owned_arrays(NULL);
    test_array_ops(NULL);
    test_compiled_names(NULL);
    test_compiled_search(NULL);
    test_compiled_reaction(NULL);
    test_compiled_rhythm(NULL);
    test_compiled_reflected(NULL);
    test_compiled_integers();
    pure_unused_return(behavior_default_backend());
    pure_unused_return(BEHAVIOR_BYTECODE);
    pure_unused_return(BEHAVIOR_ORACLE);
    whole_pure_calls(behavior_default_backend());
    whole_pure_calls(BEHAVIOR_BYTECODE);
    whole_pure_calls(BEHAVIOR_ORACLE);
    whole_function_scopes(behavior_default_backend());
    whole_function_scopes(BEHAVIOR_BYTECODE);
    whole_function_scopes(BEHAVIOR_ORACLE);
    whole_function_arguments(behavior_default_backend());
    whole_function_arguments(BEHAVIOR_BYTECODE);
    whole_function_arguments(BEHAVIOR_ORACLE);
    whole_function_comparisons();
    for (int effectful = 0; effectful <= 1; effectful++) {
        whole_function_library(behavior_default_backend(), effectful);
        whole_function_library(BEHAVIOR_BYTECODE, effectful);
        whole_function_library(BEHAVIOR_ORACLE, effectful);
    }
    whole_function_control(behavior_default_backend());
    whole_function_control(BEHAVIOR_BYTECODE);
    whole_function_control(BEHAVIOR_ORACLE);
    numeric_regions(PIN_F32);
    numeric_regions(PIN_F64);
    generation_guards();
    behavior_backend native = behavior_default_backend();
    compiled_counter(native);
    compiled_counter(BEHAVIOR_BYTECODE);
    compiled_counter(BEHAVIOR_ORACLE);
    behavior_backend previous = behavior_debug_backend(BEHAVIOR_BYTECODE);
    run_runtime_execution_checks();
    behavior_debug_backend(BEHAVIOR_ORACLE);
    run_runtime_execution_checks();
    behavior_debug_backend(previous);
    puts("PASS: compiled native, bytecode and oracle counters, waits, cancellation, source maps and linked runtime contracts");
}
