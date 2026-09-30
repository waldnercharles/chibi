#include "ecs/ecs.h"
#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/update.h"
#include "test/support/report.h"
#include "test/whole_aggregate_fixture.h"
#include "test/whole_batch_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static node *variable(behavior_asset *asset, graph *g, int index, bool set)
{
    const behavior_variable *v = &asset->variables[index];
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, 1, set ? 1 : 0, v->type, v->type_name));
    if (set) {
        assert(!graph_set_pin_type(g, n, 0, 1, v->type, v->type_name));
    }
    return n;
}
static node *advance(graph *g, node *source, int pin)
{
    node *add = graph_add_node(g, &NODE_Add),
         *delta = graph_add_node(g, &NODE_DeltaTime);
    for (int i = 0; i < 2; i++) {
        assert(!graph_set_pin_type(g, add, 0, i, PIN_F64, "double"));
    }
    assert(!graph_set_pin_type(g, add, 1, 0, PIN_F64, "double"));
    assert(!graph_connect_data(g, source, pin, add, 0));
    assert(!graph_connect_data(g, delta, 0, add, 1));
    return add;
}
behavior_asset *whole_batch_fixture(bool components)
{
    behavior_asset *a = behavior_asset_new(
        components ? "Component Tick batch" : "Counter Tick batch"
    );
    if (components) {
        assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    }
    behavior_asset_add_variable(a, "Total", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "");
    graph *g = behavior_asset_add_function(a, "Advance", false);
    node *get = variable(a, g, 0, false), *set = variable(a, g, 0, true);
    node *add = advance(g, get, 0);
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, g->nodes[0], 0, set, 0, 0));
    node *tail = set;
    if (components) {
        behavior_asset_add_variable(a, "Target", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE, NULL, "");
        node *record = graph_add_node(g, &WHOLE_NODE_GetRecord);
        assert(!graph_expose_output_fields(g, record, 0));
        int weight = -1;
        for (int i = 1; i < record->output_count; i++) {
            if (!strcmp(record->output_pins[i].field_name, "weight")) {
                weight = i;
            }
        }
        assert(weight >= 0);
        node *value = advance(g, record, weight);
        node *write = graph_add_node(g, &WHOLE_NODE_SetWeight);
        node *target = variable(a, g, 1, false);
        assert(!graph_connect_data(g, target, 0, write, 2));
        assert(!graph_connect_data(g, value, 0, write, 1));
        assert(!graph_connect_exec(g, tail, 0, write, 0, 0));
        tail = write;
    }
    assert(!graph_connect_exec(g, tail, 0, g->nodes[1], 0, 0));
    node *begin = graph_add_node(a->graph, &NODE_Event),
         *tick = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, begin, "Begin"));
    assert(!graph_set_event_name(a->graph, tick, "Tick"));
    node *initial = variable(a, a->graph, 0, true);
    assert(
        !graph_set_pin_default(a->graph, initial, 0, 1, &(pin_value){ .type = PIN_F64, .d = 10. })
    );
    assert(!graph_connect_exec(a->graph, begin, 0, initial, 0, 0));
    node *call = behavior_asset_add_function_call_node(a, g);
    assert(!graph_connect_exec(a->graph, tick, 0, call, 0, 0));
    return a;
}
void whole_batch_smoke()
{
    whole_bound_components_smoke();
    behavior_asset *asset = whole_batch_fixture(false);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, false);
    behavior_instance_set_delta_seconds(instance, .125);
    uint64_t batches = behavior_batch_dispatches(), eval = eval_oracle_calls(),
             flow = flow_oracle_calls();
    uint64_t stores = runtime_store_creations();
    flow_run *failed;
    behavior_batch *batch = NULL;
    assert(behavior_instance_dispatch_batch(instance, "Begin", &batch, &failed) && !failed);
    for (int i = 0; i < 32; i++) {
        assert(behavior_instance_dispatch_batch(instance, "Tick", &batch, &failed) && !failed);
    }
    assert(instance->variables[0].d == 14. && !acount(instance->activations));
    assert(behavior_batch_dispatches() == batches + 33);
    ecs_world *ecs = ecs_world_new();
    ecs_entity entity = ecs_world_create_entity(ecs);
    behavior_update_binding **bindings = NULL;
    node *entry;
    const behavior_function *function = behavior_program_batch_event(asset->program, "Tick", &entry);
    behavior_update_row rows[2] = {
        { .instance = instance,
          .binding = behavior_update_prepare(&bindings, instance, function, entry),
          .entity = ecs_entity_to_u64(entity) },
        /* A dead entity must end the span before any body executes. */
        { .instance = instance, .entity = 0 }
    };
    rows[1].binding = rows[0].binding;
    void *code = behavior_function_system_entry(function);
    bool native = behavior_default_backend() == BEHAVIOR_SLJIT;
    assert(native == (code != NULL));
    for (int i = 0; i < 32; i++) {
        int result;
        behavior_update_row *
            end = native
                    ? ((
                          behavior_update_fn
                      )code)(rows, rows + 2, (const ecs_execution_view *)ecs, &result)
                    : behavior_update_run_c(rows, rows + 2, (const ecs_execution_view *)ecs, &result, NULL);
        assert(end == rows + 1 && result == FLOW_OK);
    }
    assert(instance->variables[0].d == 18. && !acount(instance->activations));
    behavior_update_free_bindings(bindings);
    ecs_world_free(ecs);
    assert(stores == runtime_store_creations());
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_instance_destroy(instance);
    behavior_batch_free(batch);
    behavior_asset_free(asset);
    test_note(
        "PASS: %s synchronous Tick batch and prepared outer loop; no "
        "activation runtime stores or "
        "oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
