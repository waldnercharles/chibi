#include "ecs/ecs.h"
#include "graph/model/registry.h"
#include "graph/nodes/state_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/update.h"
#include "test/support/report.h"
#include "test/whole_multigate_fixture.h"

#include <assert.h>
#include <stdio.h>

static pin_spec integer[] = { { "Value", PIN_I32, "int" }, { "Id", PIN_I32, "int" } };
static pin_spec boolean[] = { { "Value", PIN_BOOL, "bool" }, { "Id", PIN_I32, "int" } };
static const char *invoke(void *user, const pin_value *args, pin_value *out)
{
    whole_multigate_service *s = user;
    assert(s->count < 1024);
    s->calls[s->count++] = args[1].i;
    if (s->count == s->fail_at) {
        return "MultiGate input failed";
    }
    *out = args[0];
    out->type_name = out->type == PIN_I32 ? "int" : "bool";
    return NULL;
}
static const char *read_input(const eval_context *ctx, const pin_value *args, arena *, pin_value *out)
{
    return invoke(eval_context_service(ctx, "test.multigate"), args, out);
}
static const node_host_call call = { .service = "test.multigate", .invoke = invoke };
static node_desc integer_input = { .name = "Test MultiGate Integer",
                                   .input_count = 2,
                                   .inputs = integer,
                                   .output_count = 1,
                                   .outputs = integer,
                                   .pure_kind = NODE_PURE_READ,
                                   .read_inputs = read_input,
                                   .host_call = &call };
static node_desc boolean_input = { .name = "Test MultiGate Boolean",
                                   .input_count = 2,
                                   .inputs = boolean,
                                   .output_count = 1,
                                   .outputs = boolean,
                                   .pure_kind = NODE_PURE_READ,
                                   .read_inputs = read_input,
                                   .host_call = &call };

static node *variable(behavior_asset *a, graph *g, int index, bool set)
{
    const behavior_variable *v = &a->variables[index];
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, true, set ? 1 : 0, PIN_I32, "int"));
    if (set) {
        assert(!graph_set_pin_type(g, n, false, 1, PIN_I32, "int"));
    }
    return n;
}
behavior_asset *whole_multigate_fixture(int outputs, int kind, bool effects)
{
    behavior_asset *a = behavior_asset_new("Compiled MultiGate");
    behavior_asset_add_variable(a, "Selected", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    behavior_asset_add_variable(a, "Hits", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    graph *g = behavior_asset_add_function(a, "Select", false);
    pin_spec inputs[] = { { "Reset", PIN_BOOL, "bool" },
                          { "Loop", PIN_BOOL, "bool" },
                          { "Random", PIN_BOOL, "bool" },
                          { "Start", PIN_I32, "int" } };
    for (int i = 0; i < 4; i++) {
        assert(graph_add_pin(g, g->nodes[0], true, &inputs[i]) == i + 1);
    }
    node *multi = graph_add_node(g, &NODE_MultiGate);
    while (multi->output_count > outputs) {
        assert(!graph_remove_pin(g, multi, true, multi->output_count - 1));
    }
    while (multi->output_count < outputs) {
        char name[24];
        snprintf(name, sizeof name, "Out%d", multi->output_count);
        pin_spec spec = { .name = name, .type = PIN_FLOW, .kind = PIN_EXEC };
        assert(graph_add_pin(g, multi, true, &spec) >= 0);
    }
    node *choose = graph_add_node(g, &NODE_Branch);
    assert(!graph_connect_data(g, g->nodes[0], 1, choose, 1));
    assert(!graph_connect_exec(g, choose, 0, multi, 1, 0));
    assert(!graph_connect_exec(g, choose, 1, multi, 0, 0));
    for (int pin = 2; pin < 5; pin++) {
        node *from = g->nodes[0];
        int output = pin;
        if (effects) {
            from = graph_add_node(g, pin == 4 ? &integer_input : &boolean_input);
            assert(!graph_connect_data(g, g->nodes[0], pin, from, 0));
            assert(
                !graph_set_pin_default(g, from, false, 1, &(pin_value){ .type = PIN_I32, .i = pin })
            );
            output = 0;
        }
        assert(!graph_connect_data(g, from, output, multi, pin));
    }
    node *get = variable(a, g, 1, false), *hit = variable(a, g, 1, true);
    node *add = graph_add_node(g, &NODE_Add);
    assert(!graph_set_pin_type(g, add, false, 0, PIN_I32, "int"));
    assert(!graph_set_pin_type(g, add, false, 1, PIN_I32, "int"));
    assert(!graph_set_pin_type(g, add, true, 0, PIN_I32, "int"));
    assert(!graph_set_pin_default(g, add, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 }));
    assert(!graph_connect_data(g, get, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, hit, 1));
    node *tail = hit;
    if (kind & WHOLE_MULTIGATE_WAIT) {
        tail = graph_add_node(g, &NODE_Delay);
        assert(
            !graph_set_pin_default(g, tail, false, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(g, hit, 0, tail, 0, 0));
    }
    for (int i = 0; i < outputs; i++) {
        node *selected = variable(a, g, 0, true);
        assert(
            !graph_set_pin_default(g, selected, false, 1, &(pin_value){ .type = PIN_I32, .i = i })
        );
        assert(!graph_connect_exec(g, multi, i, selected, 0, 0));
        assert(!graph_connect_exec(g, selected, 0, hit, 0, 0));
    }
    node *first = choose;
    if (kind & WHOLE_MULTIGATE_LOOP) {
        first = graph_add_node(g, &NODE_ForLoop);
        assert(
            !graph_set_pin_default(g, first, false, 2, &(pin_value){ .type = PIN_I32, .i = 2 })
        );
        assert(!graph_connect_exec(g, first, 0, choose, 0, 0));
    }
    node *seq = graph_add_node(g, &NODE_Sequence);
    assert(!graph_connect_exec(g, g->nodes[0], 0, seq, 0, 0));
    assert(!graph_connect_exec(g, seq, 0, first, 0, 0));
    assert(!graph_connect_exec(g, seq, 1, g->nodes[1], 0, 0));
    if (kind & WHOLE_MULTIGATE_CALL) {
        graph *outer = behavior_asset_add_function(a, "Outer", false);
        for (int i = 0; i < 4; i++) {
            assert(graph_add_pin(outer, outer->nodes[0], true, &inputs[i]) == i + 1);
        }
        node *nested = behavior_asset_add_function_call_node_to_graph(a, outer, g);
        for (int i = 1; i <= 4; i++) {
            assert(!graph_connect_data(outer, outer->nodes[0], i, nested, i));
        }
        assert(!graph_connect_exec(outer, outer->nodes[0], 0, nested, 0, 0));
        assert(!graph_connect_exec(outer, nested, 0, outer->nodes[1], 0, 0));
    }
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, event, "Tick"));
    node *tick = behavior_asset_add_function_call_node(a, whole_multigate_root(a));
    assert(
        !graph_set_pin_default(a->graph, tick, false, 2, &(pin_value){ .type = PIN_BOOL, .b = true })
    );
    assert(!graph_connect_exec(a->graph, event, 0, tick, 0, 0));
    return a;
}
graph *whole_multigate_root(behavior_asset *a)
{
    return a->graphs[acount(a->graphs) - 1];
}
void whole_multigate_smoke()
{
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int kind = 0; kind < WHOLE_MULTIGATE_COUNT; kind++) {
        behavior_asset *a = whole_multigate_fixture(5, kind, false);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        for (int round = 0; round < 8; round++) {
            flow_run *run = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, NULL);
            assert(run && run->whole_function_executed);
            while (flow_run_status(run) == FLOW_SUSPENDED) {
                assert(behavior_instance_tick(bi, flow_run_wake_time(run)) >= 0);
            }
            assert(flow_run_status(run) == FLOW_COMPLETED);
            assert(!behavior_instance_release_activation(bi, run));
        }
        assert(bi->variables[1].i == (kind & WHOLE_MULTIGATE_LOOP ? 24 : 8));
        assert(bi->variables[0].i == (kind & WHOLE_MULTIGATE_LOOP ? 3 : 2));
        if (!(kind & WHOLE_MULTIGATE_WAIT)) {
            assert(!behavior_instance_rebind(bi, NULL));
            behavior_batch *batch = NULL;
            for (int round = 0; round < 8; round++) {
                flow_run *failed;
                assert(behavior_instance_dispatch_batch(bi, "Tick", &batch, &failed) && !failed);
            }
            assert(bi->variables[1].i == (kind & WHOLE_MULTIGATE_LOOP ? 24 : 8));
            behavior_batch_free(batch);
            assert(!behavior_instance_rebind(bi, NULL));
            behavior_instance *other = behavior_instance_create(a, NULL, NULL, NULL);
            behavior_instance_set_trace_enabled(other, false);
            ecs_world *ecs = ecs_world_new();
            behavior_update_binding **bindings = NULL;
            node *entry;
            const behavior_function *f = behavior_program_batch_event(a->program, "Tick", &entry);
            assert(f);
            behavior_update_row rows[2];
            for (int row = 0; row < 2; row++) {
                behavior_instance *instance = row ? other : bi;
                rows[row] = (behavior_update_row){
                    .instance = instance,
                    .binding = behavior_update_prepare(&bindings, instance, f, entry),
                    .entity = ecs_entity_to_u64(ecs_world_create_entity(ecs))
                };
            }
            void *code = behavior_update_compile();
            bool native = behavior_default_backend() == BEHAVIOR_SLJIT;
            assert(native == (code != NULL));
            for (int round = 0; round < 8; round++) {
                int result;
                behavior_update_row *end = native
                                             ? ((behavior_update_fn)code)(
                                                   rows,
                                                   rows + 2,
                                                   (const ecs_execution_view *)ecs,
                                                   &result
                                               )
                                             : behavior_update_run_c(
                                                   rows,
                                                   rows + 2,
                                                   (const ecs_execution_view *)ecs,
                                                   &result,
                                                   NULL
                                               );
                assert(end == rows + 2 && result == FLOW_OK);
            }
            assert(bi->variables[1].i == (kind & WHOLE_MULTIGATE_LOOP ? 24 : 8));
            assert(other->variables[1].i == bi->variables[1].i);
            assert(bi->node_states[0].str != other->node_states[0].str);
            behavior_update_free_code(code);
            behavior_update_free_bindings(bindings);
            ecs_world_free(ecs);
            behavior_instance_destroy(other);
        }
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    test_note(
        "PASS: %s MultiGate, nested calls, loops, waits and persistent "
        "selection; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
