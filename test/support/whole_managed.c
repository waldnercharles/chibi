#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/whole_managed_fixture.h"

#include <assert.h>
#include <string.h>

static pin_spec inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { .name = "Value", .type = PIN_STRUCT, .type_name = "ManagedRecord" }
};
static pin_spec outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
    { .name = "Value", .type = PIN_STRUCT, .type_name = "ManagedRecord" }
};
static int set_record(node *, flow_context *ctx)
{
    ecs_world *world = flow_context_service(ctx, "ecs.world");
    behavior_instance *bi = flow_context_instance(ctx);
    if (!world || !bi) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "component Set requires an entity and ecs.world");
    }
    pin_value input;
    int result = flow_context_eval_data(ctx, 1, &input);
    if (result) {
        return result;
    }
    if (!input.ptr) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "component Set requires a value");
    }
    whole_managed *record = ecs_component_data(
        world,
        ecs_entity_from_u64(behavior_instance_owner(bi).u64),
        WHOLE_MANAGED_COMPONENT.id
    );
    if (!record) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "entity lacks component");
    }
    *record = *(whole_managed *)input.ptr;
    record->label = ecs_world_intern_string(world, record->label);
    record->nested.text = ecs_world_intern_string(world, record->nested.text);
    pin_value *out = flow_context_output_value(ctx, 1);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .type_name = "ManagedRecord",
                        .ptr = flow_context_scratch_alloc(ctx, sizeof *record) };
    memcpy(out->ptr, record, sizeof *record);
    return flow_context_continue(ctx, 0);
}
static node_desc set_node = { .name = "Set ManagedRecord",
                              .inputs = inputs,
                              .input_count = 2,
                              .outputs = outputs,
                              .output_count = 2,
                              .flow_kind = NODE_FLOW_CUSTOM,
                              .flow = set_record,
                              .component_kind = NODE_COMPONENT_SET_SELF,
                              .component = &WHOLE_MANAGED_COMPONENT };
static const type_desc *const types[] = { &WHOLE_MANAGED_INNER_TYPE,
                                          &WHOLE_MANAGED_TYPE };
static node_desc *const nodes[] = { &set_node };
const registry_pack WHOLE_MANAGED_PACK = { .name = "Managed values",
                                           .types = types,
                                           .type_count = 2,
                                           .nodes = nodes,
                                           .node_count = 1 };

static node *variable(behavior_asset *a, graph *g, bool write)
{
    node *n = graph_add_node(g, write ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, a->variables[0].id, "Source"));
    assert(!graph_set_pin_type(g, n, 1, write ? 1 : 0, PIN_STRUCT, "ManagedRecord"));
    if (write) {
        assert(!graph_set_pin_type(g, n, 0, 1, PIN_STRUCT, "ManagedRecord"));
    }
    return n;
}
behavior_asset *whole_managed_fixture(bool waits, bool components)
{
    behavior_asset *a = behavior_asset_new("Managed values");
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    for (int i = 0; i < 3; i++) {
        assert(behavior_asset_add_variable(
            a,
            (const char *[]){ "Source", "Result", "Label" }[i],
            i == 2 ? PIN_NAME : PIN_STRUCT,
            i == 2 ? NULL : "ManagedRecord",
            PIN_CONTAINER_SINGLE,
            NULL,
            ""
        ));
    }
    graph *capture = behavior_asset_add_function(a, "Capture", false);
    graph *hold = behavior_asset_add_function(a, "Hold", false);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, event, "Run"));
    pin_spec record = inputs[1], label = { .name = "Label", .type = PIN_NAME };
    assert(graph_add_pin(a->graph, event, 1, &record) == 1);
    for (int i = 0; i < 2; i++) {
        graph *g = i ? hold : capture;
        assert(graph_add_pin(g, g->nodes[0], 1, &record) == 1);
        assert(graph_add_pin(g, g->nodes[1], 0, &record) == 1);
        assert(graph_add_pin(g, g->nodes[1], 0, &label) == 2);
    }
    node *tail = hold->nodes[0];
    if (waits) {
        node *delay = graph_add_node(hold, &NODE_Delay);
        assert(
            !graph_set_pin_default(hold, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(hold, tail, 0, delay, 0, 0));
        tail = delay;
    }
    node *value = hold->nodes[0];
    if (components) {
        node *set = graph_add_node(hold, &set_node);
        assert(!graph_connect_data(hold, value, 1, set, 1));
        assert(!graph_connect_exec(hold, tail, 0, set, 0, 0));
        tail = value = set;
    }
    assert(!graph_expose_output_fields(hold, value, 1));
    int field = -1;
    for (int i = 2; i < value->output_count; i++) {
        if (value->output_pins[i].field_name &&
            !strcmp(value->output_pins[i].field_name, "label")) {
            field = i;
        }
    }
    assert(field >= 0);
    assert(!graph_connect_data(hold, value, 1, hold->nodes[1], 1));
    assert(!graph_connect_data(hold, value, field, hold->nodes[1], 2));
    assert(!graph_connect_exec(hold, tail, 0, hold->nodes[1], 0, 0));
    node *store = variable(a, capture, true);
    node *get = variable(a, capture, false);
    node *call = behavior_asset_add_function_call_node_to_graph(a, capture, hold);
    assert(!graph_connect_data(capture, capture->nodes[0], 1, store, 1));
    assert(!graph_connect_exec(capture, capture->nodes[0], 0, store, 0, 0));
    node *again = variable(a, capture, true);
    assert(!graph_connect_data(capture, get, 0, again, 1));
    assert(!graph_connect_exec(capture, store, 0, again, 0, 0));
    assert(!graph_connect_exec(capture, again, 0, call, 0, 0));
    assert(!graph_connect_data(capture, get, 0, call, 1));
    assert(!graph_connect_exec(capture, call, 0, capture->nodes[1], 0, 0));
    assert(!graph_connect_data(capture, call, 1, capture->nodes[1], 1));
    assert(!graph_connect_data(capture, call, 2, capture->nodes[1], 2));
    call = behavior_asset_add_function_call_node(a, capture);
    assert(!graph_connect_data(a->graph, event, 1, call, 1));
    assert(!graph_connect_exec(a->graph, event, 0, call, 0, 0));
    tail = call;
    for (int i = 1; i < 3; i++) {
        node *set = behavior_asset_add_set_node(a, a->variables[i].id);
        assert(!graph_connect_data(a->graph, call, i, set, 1));
        assert(!graph_connect_exec(a->graph, tail, 0, set, 0, 0));
        tail = set;
    }
    node *label_get = behavior_asset_add_get_node(a, a->variables[2].id);
    node *label_set = behavior_asset_add_set_node(a, a->variables[2].id);
    assert(!graph_connect_data(a->graph, label_get, 0, label_set, 1));
    assert(!graph_connect_exec(a->graph, tail, 0, label_set, 0, 0));
    return a;
}
void whole_managed_smoke()
{
    behavior_asset *a = whole_managed_fixture(true, true);
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &WHOLE_MANAGED_COMPONENT, NULL));
    ecs_entity entity = ecs_world_create_entity(world);
    assert(!ecs_world_record_add(world, entity, WHOLE_MANAGED_COMPONENT.id, NULL, 0));
    assert(!ecs_world_apply_commands(world));
    pin_value owner = { .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .u64 = ecs_entity_to_u64(entity) };
    behavior_instance *bi = behavior_instance_create(a, &owner, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    assert(!behavior_instance_bind_service(bi, "ecs.world", world));
    whole_managed record = { "old", { "nested", 3.25f }, 7 };
    pin_value arg = { .type = PIN_STRUCT, .type_name = "ManagedRecord", .ptr = &record };
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ &arg, 1 }, NULL);
    assert(run && flow_run_status(run) == FLOW_SUSPENDED && run->whole_function_executed);
    record = (whole_managed){ "new", { "replacement", 9 }, 13 };
    assert(!behavior_instance_set_variable(bi, a->variables[0].id, &arg));
    assert(!behavior_instance_tick(bi, .125));
    assert(flow_run_status(run) == FLOW_COMPLETED);
    whole_managed *actual = bi->execution.variables[1].ptr;
    assert(!strcmp(actual->label, "old") && !strcmp(actual->nested.text, "nested"));
    assert(actual->nested.weight == 3.25f && actual->count == 7);
    assert(!strcmp(bi->execution.variables[2].str, "old"));
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_instance_destroy(bi);
    const whole_managed *component = ecs_component_data_const(
        world,
        entity,
        WHOLE_MANAGED_COMPONENT.id
    );
    assert(!strcmp(component->label, "old") && !strcmp(component->nested.text, "nested"));
    ecs_world_free(world);
    behavior_asset_free(a);
    printf(
        "PASS: %s managed structs and strings survive nested waits, variable "
        "overwrite and component ownership; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
