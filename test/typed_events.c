#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/events.h"
#include "graph/document/serialize.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/instance.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_typed_events()
{
    assert(game_host_event_reserved("Begin"));
    assert(game_host_event_reserved("Tick"));
    assert(!game_host_event_reserved("Signal"));
    behavior_asset *reserved = behavior_asset_new("Begin");
    reserved->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    node *reserved_event = graph_add_node(reserved->graph, &NODE_Event);
    assert(graph_event_apply(reserved->graph, reserved_event, reserved) == -1);
    behavior_asset_free(reserved);
    behavior_asset *definition = behavior_asset_new("Signal");
    definition->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    behavior_variable *parameter = behavior_asset_add_variable(
        definition,
        "Amount",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 23 },
        "Payload"
    );
    uint64_t parameter_id = parameter->id;
    behavior_asset *asset = behavior_asset_new("Local events");
    behavior_variable *result = behavior_asset_add_variable(
        asset,
        "Result",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        NULL,
        NULL
    );
    uint64_t result_id = result->id;
    graph *g = asset->graph;
    node *begin = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, begin, "Begin"));
    node *call = graph_add_node(g, &NODE_CallEvent);
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_event_apply(g, call, definition));
    assert(!graph_event_apply(g, event, definition));
    assert(graph_event_matches(call, definition));
    node *delay = graph_add_node(g, &NODE_Delay);
    assert(!graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 0.25 }));
    node *set = behavior_asset_add_set_node(asset, result_id);
    assert(!graph_connect_exec(g, begin, 0, call, 0, 0));
    assert(!graph_connect_exec(g, event, 0, delay, 0, 0));
    assert(!graph_connect_exec(g, delay, 0, set, 0, 0));
    assert(!graph_connect_data(g, event, 1, set, 1));
    /* An untagged wired payload must not disappear during signature refresh. */
    event->output_pins[1].declaration_id = 0;
    char *untagged = behavior_asset_save(asset);
    assert(graph_event_apply(g, event, definition) == -1);
    char *unchanged = behavior_asset_save(asset);
    assert(!strcmp(untagged, unchanged));
    free(untagged);
    free(unchanged);
    event->output_pins[1].declaration_id = parameter_id;
    assert(graph_add_pin(g, event, true, &(pin_spec){ .name = "Unused", .type = PIN_I32 }) >= 0);
    assert(!graph_event_matches(event, definition));
    assert(!graph_event_apply(g, event, definition));
    assert(event->output_count == 2);
    assert(graph_event_matches(event, definition));
    uint64_t pin_id = event->output_pins[1].id;
    assert(!behavior_asset_rename_variable(definition, parameter_id, "Value"));
    assert(!graph_event_apply(g, call, definition));
    assert(!graph_event_apply(g, event, definition));
    assert(event->output_pins[1].id == pin_id);
    assert(!strcmp(event->output_pins[1].name, "Value"));
    char *json = behavior_asset_save(asset);
    behavior_asset *copy = behavior_asset_load(json);
    assert(copy);
    free(json);
    behavior_asset_free(copy);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    flow_error error = { 0 };
    assert(behavior_instance_dispatch(instance, "Begin", (flow_inputs){ 0 }, &error));
    pin_value value;
    assert(!behavior_instance_get_variable(instance, result_id, &value));
    assert(value.i == 0);
    assert(behavior_instance_tick(instance, 1) == FLOW_OK);
    assert(!behavior_instance_get_variable(instance, result_id, &value));
    assert(value.i == 23);
    behavior_instance_destroy(instance);
    assert(!behavior_asset_retype_variable(definition, parameter_id, PIN_F32, "float"));
    json = behavior_asset_save(asset);
    assert(graph_event_apply(g, event, definition) == -1);
    char *after = behavior_asset_save(asset);
    assert(!strcmp(json, after));
    free(json);
    free(after);
    behavior_asset_add_variable(definition, "Second", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "Payload");
    assert(!graph_event_apply(g, call, definition));
    assert(graph_move_pin(g, call, false, 1, 2) == 2);
    assert(!graph_event_matches(call, definition));
    assert(!graph_event_apply(g, call, definition));
    assert(graph_event_matches(call, definition));
    behavior_asset_free(asset);
    behavior_asset_free(definition);
    puts("typed event signatures, delayed payloads and atomic edits: ok");
}
