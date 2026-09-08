#include "graph/document/extract.h"

#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/value_array.h"
#include "support/arena.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
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

static void connect(graph *graph, node *from, node *to, int input)
{
    assert(graph_connect_data(graph, from, 0, to, input) == 0);
}

static void real(graph *graph, node *node, int input, float value)
{
    assert(graph_set_pin_type(graph, node, 0, input, PIN_F64, "double") == 0);
    pin_value number = { .type = PIN_F64, .type_name = "double", .d = value };
    assert(graph_set_pin_default(graph, node, 0, input, &number) == 0);
}

static pin_value evaluate(behavior_asset *asset, uint64_t node_id)
{
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    arena memory;
    arena_init(&memory, 128);
    pin_value value, copy;
    assert(
        graph_eval_output(asset->graph, graph_node_by_id(asset->graph, node_id), 0, &memory, &value, &context) ==
        0
    );
    assert(pin_value_copy(&copy, &value) == 0);
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
    return copy;
}

static void expect_real(behavior_asset *asset, uint64_t node_id, float expected)
{
    pin_value value = evaluate(asset, node_id);
    assert(value.type == PIN_F64 && fabs(value.d - expected) < 0.0001);
    pin_value_destroy(&value);
}

static behavior_asset *roundtrip(const behavior_asset *asset)
{
    char *json = behavior_asset_save_json(asset);
    assert(json);
    behavior_asset *copy = behavior_asset_load_json(json);
    if (!copy) {
        fprintf(stderr, "extracted asset reload: %s\n", graph_last_error());
    }
    assert(copy);
    free(json);
    return copy;
}

static void arithmetic_extraction()
{
    behavior_asset *asset = behavior_asset_new("Arithmetic");
    graph *caller = asset->graph;
    node *source = add(caller, "Real");
    node *sum = add(caller, "Add");
    node *product = add(caller, "Multiply");
    node *result = add(caller, "Add");
    real(caller, source, 0, 2.5f);
    real(caller, sum, 1, 1.5f);
    real(caller, product, 1, 3.0f);
    connect(caller, source, sum, 0);
    connect(caller, source, product, 0);
    connect(caller, sum, result, 0);
    connect(caller, product, result, 1);
    uint64_t result_id = result->id, source_id = source->id;
    expect_real(asset, result_id, 11.5f);
    node *selected[] = { sum, product };
    assert(behavior_asset_can_extract_function(asset, caller, selected, 2) == NULL);
    char *before = behavior_asset_save_json(asset);
    uint64_t counter = caller->id_counter;
    behavior_asset *extracted = NULL;
    uint64_t call_id = 0;
    const char *error = behavior_asset_extract_function(asset, caller, selected, 2, "Compute", &extracted, &call_id);
    if (error) {
        fprintf(stderr, "extract arithmetic: %s\n", error);
    }
    assert(!error && extracted && call_id);
    char *after = behavior_asset_save_json(asset);
    assert(strcmp(before, after) == 0 && caller->id_counter == counter);
    free(before);
    free(after);
    node *call = graph_node_by_id(extracted->graph, call_id);
    graph *function = behavior_asset_find_graph_by_name(extracted, "Compute");
    assert(function && function->kind == GRAPH_FUNCTION && call);
    assert(call->input_count == 1 && call->output_count == 2);
    assert(call->input_pins[0].type == PIN_F64);
    assert(strcmp(call->input_pins[0].type_name, "double") == 0);
    for (int i = 0; i < call->output_count; i++) {
        assert(call->output_pins[i].type == PIN_F64);
        assert(call->output_pins[i].container == PIN_CONTAINER_SINGLE);
        assert(call->output_pins[i].name && call->output_pins[i].name[0]);
    }
    assert(strcmp(call->output_pins[0].name, call->output_pins[1].name) != 0);
    assert(!graph_node_by_id(extracted->graph, sum->id));
    assert(!graph_node_by_id(extracted->graph, product->id));
    node *first_source, *second_source;
    int first_pin, second_pin;
    node *consumer = graph_node_by_id(extracted->graph, result_id);
    assert(graph_input_source(extracted->graph, consumer, 0, &first_source, &first_pin));
    assert(graph_input_source(extracted->graph, consumer, 1, &second_source, &second_pin));
    assert(first_source == call && second_source == call && first_pin != second_pin);
    expect_real(extracted, result_id, 11.5f);
    real(extracted->graph, graph_node_by_id(extracted->graph, source_id), 0, 4.0f);
    expect_real(extracted, result_id, 17.5f);
    behavior_asset *loaded = roundtrip(extracted);
    expect_real(loaded, result_id, 17.5f);
    behavior_asset_free(loaded);
    behavior_asset_free(extracted);
    behavior_asset_free(asset);
}

static void rejected(behavior_asset *asset, graph *caller, node *const *selection, int count, const char *name)
{
    char *before = behavior_asset_save_json(asset);
    uint64_t counter = asset->graph->id_counter;
    behavior_asset *result = NULL;
    uint64_t call = 0;
    assert(behavior_asset_extract_function(asset, caller, selection, count, name, &result, &call));
    assert(!result);
    char *after = behavior_asset_save_json(asset);
    assert(strcmp(before, after) == 0);
    assert(asset->graph->id_counter == counter);
    free(before);
    free(after);
}

static void rejected_extractions()
{
    behavior_asset *asset = behavior_asset_new("Rejected");
    graph *caller = asset->graph;
    node *first = add(caller, "Add");
    node *outside = add(caller, "Multiply");
    node *second = add(caller, "Add");
    node *sink = add(caller, "Real");
    node *event = add(caller, "Event");
    connect(caller, first, outside, 0);
    connect(caller, outside, second, 0);
    connect(caller, second, sink, 0);
    node *cycle[] = { first, second };
    assert(behavior_asset_can_extract_function(asset, caller, cycle, 2));
    rejected(asset, caller, cycle, 2, "Cycle");
    rejected(asset, caller, NULL, 0, "Empty");
    rejected(asset, caller, &event, 1, "Execution");
    rejected(asset, caller, &sink, 1, "No outputs");
    rejected(asset, caller, &first, 1, "");
    assert(behavior_asset_add_function(asset, "Taken", 1));
    rejected(asset, caller, &first, 1, "Taken");
    behavior_asset *other = behavior_asset_new("Other");
    node *foreign = add(other->graph, "Real");
    rejected(asset, caller, &foreign, 1, "Foreign");
    behavior_asset_free(other);
    behavior_asset_free(asset);
}

static void array_extraction()
{
    behavior_asset *asset = behavior_asset_new("Array");
    graph *caller = asset->graph;
    node *source = add(caller, "Reroute Data");
    node *selected = add(caller, "Reroute Data");
    node *sink = add(caller, "Reroute Data");
    node *nodes[] = { source, selected, sink };
    for (int i = 0; i < 3; i++) {
        for (int output = 0; output < 2; output++) {
            assert(graph_set_pin_type(caller, nodes[i], output, 0, PIN_I32, "int") == 0);
            assert(graph_set_pin_container(caller, nodes[i], output, 0, PIN_CONTAINER_ARRAY) == 0);
        }
    }
    pin_array *array = pin_array_new(PIN_I32, "int", 2);
    assert(
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 7 }) == 0
    );
    assert(
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 11 }) == 1
    );
    pin_value values = { .type = PIN_I32,
                         .type_name = "int",
                         .container = PIN_CONTAINER_ARRAY,
                         .ptr = array };
    assert(graph_set_pin_default(caller, source, 0, 0, &values) == 0);
    pin_array_free(array);
    connect(caller, source, selected, 0);
    connect(caller, selected, sink, 0);
    pin_value baseline = evaluate(asset, sink->id);
    const pin_array *original = baseline.ptr;
    assert(baseline.container == PIN_CONTAINER_ARRAY && original && original->count == 2);
    assert(original->items[0].i == 7 && original->items[1].i == 11);
    pin_value_destroy(&baseline);
    behavior_asset *extracted = NULL;
    uint64_t call_id;
    const char *error = behavior_asset_extract_function(asset, caller, &selected, 1, "Values", &extracted, &call_id);
    if (error) {
        fprintf(stderr, "Array extraction: %s (serializer: %s)\n", error, graph_last_error());
    }
    assert(!error);
    node *call = graph_node_by_id(extracted->graph, call_id);
    assert(call->input_count == 1 && call->output_count == 1);
    assert(call->input_pins[0].type == PIN_I32 && call->output_pins[0].type == PIN_I32);
    assert(strcmp(call->input_pins[0].type_name, "int") == 0);
    assert(call->input_pins[0].container == PIN_CONTAINER_ARRAY);
    assert(call->output_pins[0].container == PIN_CONTAINER_ARRAY);
    behavior_asset *loaded = roundtrip(extracted);
    pin_value value = evaluate(loaded, sink->id);
    const pin_array *result = value.ptr;
    assert(value.container == PIN_CONTAINER_ARRAY && result && result->count == 2);
    assert(result->items[0].i == 7 && result->items[1].i == 11);
    pin_value_destroy(&value);
    behavior_asset_free(loaded);
    behavior_asset_free(extracted);
    behavior_asset_free(asset);
}

static void converted_boundaries()
{
    behavior_asset *asset = behavior_asset_new("Converted boundaries");
    graph *caller = asset->graph;
    node *source = add(caller, "Integer");
    node *integer = add(caller, "Integer");
    node *decimal = add(caller, "Real");
    node *integer_sink = add(caller, "Real");
    node *decimal_sink = add(caller, "Real");
    node *sum = add(caller, "Add");
    pin_value value = { .type = PIN_I32, .type_name = "int", .i = 42 };
    assert(graph_set_pin_default(caller, source, 0, 0, &value) == 0);
    connect(caller, source, integer, 0);
    assert(graph_connect_data_conversion(caller, source, 0, decimal, 0, &NODE_To_Real) == 0);
    assert(graph_connect_data_conversion(caller, integer, 0, integer_sink, 0, &NODE_To_Real) == 0);
    connect(caller, decimal, decimal_sink, 0);
    connect(caller, integer_sink, sum, 0);
    connect(caller, decimal_sink, sum, 1);
    expect_real(asset, sum->id, 84);
    node *selection[] = { integer, decimal };
    behavior_asset *extracted = NULL;
    uint64_t call_id;
    const char *error = behavior_asset_extract_function(asset, caller, selection, 2, "Convert", &extracted, &call_id);
    if (error) {
        fprintf(stderr, "extract converted boundaries: %s\n", error);
    }
    assert(!error && extracted);
    node *call = graph_node_by_id(extracted->graph, call_id);
    assert(call->input_count == 1 && call->input_pins[0].type == PIN_I32);
    assert(call->output_count == 2);
    assert(
        graph_input_conversion(
            extracted->graph,
            graph_node_by_id(extracted->graph, integer_sink->id),
            0
        ) == &NODE_To_Real
    );
    expect_real(extracted, sum->id, 84);
    behavior_asset *loaded = roundtrip(extracted);
    expect_real(loaded, sum->id, 84);
    behavior_asset_free(loaded);
    behavior_asset_free(extracted);
    behavior_asset_free(asset);
}

static void function_counter_roundtrip()
{
    behavior_asset *asset = behavior_asset_new("Function IDs");
    graph *function = behavior_asset_add_function(asset, "Identity", 1);
    node *entry = graph_node_by_id(function, function->entry_id);
    node *result = graph_node_by_id(function, function->return_id);
    pin_spec argument = { .name = "Value", .type = PIN_I32, .type_name = "int", .kind = PIN_DATA };
    assert(graph_add_pin(function, entry, 1, &argument) == 0);
    assert(graph_add_pin(function, result, 0, &argument) == 0);
    assert(graph_connect_data(function, entry, 0, result, 0) == 0);
    assert(behavior_asset_add_function_call_node(asset, function));
    char *before = behavior_asset_save_json(asset);
    behavior_asset *loaded = behavior_asset_load_json(before);
    assert(loaded);
    char *after = behavior_asset_save_json(loaded);
    assert(strcmp(before, after) == 0);
    free(before);
    free(after);
    behavior_asset_free(loaded);
    pin_value zero = { .type = PIN_I32, .type_name = "int" };
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "Sparse",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &zero,
        "Test"
    );
    assert(variable);
    variable->id = 5000;
    graph_comment_box *box = graph_add_comment_box(function, "Sparse IDs", 0, 0, 100, 100);
    assert(box);
    box->id = 6000;
    result->input_pins[0].id = 7000;
    function->edges[0].to_pin_id = 7000;
    asset->graph->id_counter = 1;
    before = behavior_asset_save_json(asset);
    loaded = behavior_asset_load_json(before);
    assert(loaded && loaded->graph->id_counter == 7001);
    node *next = add(loaded->graph, "Integer");
    assert(next->id == 7001 && next->input_pins[0].id > 7001);
    behavior_asset_free(loaded);
    free(before);
    behavior_asset_free(asset);
}

static double extraction_time(void *user)
{
    return *(double *)user;
}

static void execution_extraction()
{
    behavior_asset *asset = behavior_asset_new("Execution extraction");
    graph *g = asset->graph;
    pin_value initial = { .type = PIN_I32, .type_name = "int", .i = 0 };
    uint64_t first_id = behavior_asset_add_variable(asset, "First", PIN_I32, "int", PIN_CONTAINER_SINGLE, &initial, "")
                            ->id;
    uint64_t second_id = behavior_asset_add_variable(asset, "Second", PIN_I32, "int", PIN_CONTAINER_SINGLE, &initial, "")
                             ->id;
    node *event = add(g, "Event");
    assert(graph_set_event_name(g, event, "Go") == 0);
    pin_spec input = { .name = "Number", .type = PIN_I32, .type_name = "int", .kind = PIN_DATA };
    assert(graph_add_pin(g, event, 1, &input) == 1);
    node *wait = add(g, "Delay");
    pin_value duration = { .type = PIN_F64, .type_name = "double", .d = 1 };
    assert(graph_set_pin_default(g, wait, 0, 1, &duration) == 0);
    node *first = behavior_asset_add_set_node(asset, first_id);
    node *second = behavior_asset_add_set_node(asset, second_id);
    assert(graph_connect_exec(g, event, 0, wait, 0, 0) == 0);
    assert(graph_connect_exec(g, wait, 0, first, 0, 0) == 0);
    assert(graph_connect_exec(g, first, 0, second, 0, 0) == 0);
    assert(graph_connect_data(g, event, 1, first, 1) == 0);
    node *read = behavior_asset_add_get_node(asset, first_id);
    assert(graph_connect_data(g, read, 0, second, 1) == 0);
    node *selection[] = { first, wait, read };
    behavior_asset *result = NULL;
    uint64_t call_id = 0;
    const char *error = behavior_asset_extract_function(asset, g, selection, 3, "Store later", &result, &call_id);
    if (error) {
        fprintf(stderr, "execution extraction: %s\n", error);
    }
    assert(!error && result && call_id);
    behavior_asset *loaded = roundtrip(result);
    double time = 0;
    behavior_instance *instance = behavior_instance_create(loaded, NULL, extraction_time, &time);
    assert(instance);
    pin_value number = { .type = PIN_I32, .type_name = "int", .i = 42 };
    flow_error failure;
    flow_run *run = behavior_instance_dispatch(instance, "Go", (flow_inputs){ &number, 1 }, &failure);
    assert(run && flow_run_status(run) == FLOW_SUSPENDED);
    pin_value value;
    assert(behavior_instance_get_variable(instance, second_id, &value) == 0 && value.i == 0);
    time = 2;
    assert(flow_run_tick_at(run, time) == FLOW_OK);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    assert(behavior_instance_get_variable(instance, first_id, &value) == 0 && value.i == 42);
    assert(behavior_instance_get_variable(instance, second_id, &value) == 0 && value.i == 42);
    behavior_instance_destroy(instance);
    behavior_asset_free(loaded);
    behavior_asset_free(result);
    behavior_asset_free(asset);
}

void test_extract()
{
    execution_extraction();
    function_counter_roundtrip();
    arithmetic_extraction();
    rejected_extractions();
    array_extraction();
    converted_boundaries();
    puts("PASS: extraction preserves pure calculations, defaults, fanout and arrays; rejected boundaries leave the document unchanged");
}
