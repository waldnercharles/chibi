#include "ecs/ecs.h"
#include "graph/model/registry.h"
#include "graph/nodes/state_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/update.h"
#include "test/whole_state_fixture.h"

#include <assert.h>
#include <stdio.h>

static pin_spec boolean[] = { { .name = "Closed", .type = PIN_BOOL, .type_name = "bool" } };
static pin_spec integer[] = { { .name = "Limit", .type = PIN_I32, .type_name = "int" } };
static const char *invoke(void *user, const pin_value *args, pin_value *out)
{
    whole_state_service *service = user;
    assert(service);
    int index = args[0].type == PIN_I32;
    if (++service->calls[index] == service->fail_at[index]) {
        return "state input failed";
    }
    *out = *args;
    out->type_name = index ? "int" : "bool";
    return NULL;
}
static const char *read_input(const eval_context *ctx, const pin_value *args, arena *, pin_value *out)
{
    return invoke(eval_context_service(ctx, "test.state"), args, out);
}
static const node_host_call call = { .service = "test.state", .invoke = invoke };
static node_desc boolean_input = { .name = "Test State Boolean",
                                   .input_count = 1,
                                   .inputs = boolean,
                                   .output_count = 1,
                                   .outputs = boolean,
                                   .pure_kind = NODE_PURE_READ,
                                   .read_inputs = read_input,
                                   .host_call = &call };
static node_desc integer_input = { .name = "Test State Limit",
                                   .input_count = 1,
                                   .inputs = integer,
                                   .output_count = 1,
                                   .outputs = integer,
                                   .pure_kind = NODE_PURE_READ,
                                   .read_inputs = read_input,
                                   .host_call = &call };
static node *variable(behavior_asset *a, graph *g, int index, bool set)
{
    const behavior_variable *v = &a->variables[index];
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, true, set ? 1 : 0, v->type, v->type_name));
    if (set) {
        assert(!graph_set_pin_type(g, n, false, 1, v->type, v->type_name));
    }
    return n;
}
behavior_asset *whole_state_fixture(int kind, bool effects)
{
    const node_desc *descs[] = { &NODE_DoOnce, &NODE_DoN, &NODE_FlipFlop, &NODE_Gate };
    int type = kind & 3;
    behavior_asset *a = behavior_asset_new("Compiled state");
    behavior_asset_add_variable(a, "Hits", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    behavior_asset_add_variable(
        a,
        "Last",
        type == 2 ? PIN_BOOL : PIN_I32,
        type == 2 ? "bool" : "int",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    graph *g = behavior_asset_add_function(a, "State", false);
    pin_spec action = { .name = "Action", .type = PIN_I32, .type_name = "int" };
    assert(graph_add_pin(g, g->nodes[0], true, &action) == 1);
    assert(graph_add_pin(g, g->nodes[0], true, boolean) == 2);
    assert(graph_add_pin(g, g->nodes[0], true, integer) == 3);
    node *state = graph_add_node(g, descs[type]);
    node *choose = graph_add_node(g, &NODE_SwitchInt);
    pin_spec extra = { .name = "3", .type = PIN_FLOW, .kind = PIN_EXEC };
    assert(graph_add_pin(g, choose, true, &extra) == 3);
    extra.name = "Default";
    assert(graph_add_pin(g, choose, true, &extra) == 4);
    assert(!graph_connect_data(g, g->nodes[0], 1, choose, 1));
    for (int pin = 0; pin < state->input_count; pin++) {
        if (state->input_pins[pin].kind == PIN_EXEC) {
            assert(!graph_connect_exec(g, choose, pin, state, pin, 0));
            continue;
        }
        int parameter = type == 1 ? 3 : 2;
        node *input = g->nodes[0];
        int output = parameter;
        if (effects) {
            input = graph_add_node(g, type == 1 ? &integer_input : &boolean_input);
            output = 0;
            assert(!graph_connect_data(g, g->nodes[0], parameter, input, 0));
        }
        assert(!graph_connect_data(g, input, output, state, pin));
    }
    node *get = variable(a, g, 0, false), *set = variable(a, g, 0, true);
    node *add = graph_add_node(g, &NODE_Add);
    for (int pin = 0; pin < 2; pin++) {
        assert(!graph_set_pin_type(g, add, false, pin, PIN_I32, "int"));
    }
    assert(!graph_set_pin_type(g, add, true, 0, PIN_I32, "int"));
    assert(!graph_set_pin_default(g, add, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 }));
    assert(!graph_connect_data(g, get, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, set, 1));
    for (int pin = 0; pin < state->output_count; pin++) {
        if (state->output_pins[pin].kind == PIN_EXEC) {
            assert(!graph_connect_exec(g, state, pin, set, 0, 0));
        }
    }
    node *tail = set;
    if (kind & WHOLE_STATE_WAIT) {
        tail = graph_add_node(g, &NODE_Delay);
        assert(
            !graph_set_pin_default(g, tail, false, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(g, set, 0, tail, 0, 0));
    }
    if (type == 1 || type == 2) {
        node *last = variable(a, g, 1, true);
        assert(!graph_connect_data(g, state, type == 1 ? 1 : 2, last, 1));
        assert(!graph_connect_exec(g, tail, 0, last, 0, 0));
    }
    node *first = choose;
    if (kind & WHOLE_STATE_LOOP) {
        first = graph_add_node(g, &NODE_ForLoop);
        assert(
            !graph_set_pin_default(g, first, false, 2, &(pin_value){ .type = PIN_I32, .i = 2 })
        );
        assert(!graph_connect_exec(g, first, 0, choose, 0, 0));
    }
    node *sequence = graph_add_node(g, &NODE_Sequence);
    assert(!graph_connect_exec(g, g->nodes[0], 0, sequence, 0, 0));
    assert(!graph_connect_exec(g, sequence, 0, first, 0, 0));
    assert(!graph_connect_exec(g, sequence, 1, g->nodes[1], 0, 0));
    if (kind & WHOLE_STATE_CALL) {
        graph *outer = behavior_asset_add_function(a, "Outer", false);
        for (int pin = 1; pin < 4; pin++) {
            assert(
                graph_add_pin(outer, outer->nodes[0], true, &g->nodes[0]->output_pins[pin]) == pin
            );
        }
        node *call = behavior_asset_add_function_call_node_to_graph(a, outer, g);
        for (int pin = 1; pin < 4; pin++) {
            assert(!graph_connect_data(outer, outer->nodes[0], pin, call, pin));
        }
        assert(!graph_connect_exec(outer, outer->nodes[0], 0, call, 0, 0));
        assert(!graph_connect_exec(outer, call, 0, outer->nodes[1], 0, 0));
    }
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, event, "Tick"));
    node *tick = behavior_asset_add_function_call_node(a, whole_state_root(a));
    assert(
        !graph_set_pin_default(a->graph, tick, false, 3, &(pin_value){ .type = PIN_I32, .i = 2 })
    );
    assert(!graph_connect_exec(a->graph, event, 0, tick, 0, 0));
    return a;
}
graph *whole_state_root(behavior_asset *a)
{
    return a->graphs[acount(a->graphs) - 1];
}
void whole_state_smoke()
{
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int kind = 0; kind < WHOLE_STATE_COUNT; kind++) {
        behavior_asset *a = whole_state_fixture(kind, false);
        graph *g = whole_state_root(a);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        for (int round = 0; round < 2; round++) {
            pin_value args[] = { { .type = PIN_I32 },
                                 { .type = PIN_BOOL },
                                 { .type = PIN_I32, .i = 2 } };
            flow_options options = { .instance = bi };
            flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 3 }, &options, NULL);
            int result = flow_run_resume(run);
            while (result == FLOW_SUSPENDED_RESULT) {
                result = flow_run_tick_at(run, flow_run_wake_time(run));
            }
            assert(!result && run->whole_function_executed && !acount(run->children));
            flow_run_free(run);
        }
        int executions = kind & WHOLE_STATE_LOOP ? 6 : 2;
        assert(
            bi->variables[0].i == ((kind & 3) == 0   ? 1
                                   : (kind & 3) == 1 ? 2
                                                     : executions)
        );
        if (!(kind & WHOLE_STATE_WAIT)) {
            assert(!behavior_instance_rebind(bi, NULL));
            behavior_batch *batch = NULL;
            for (int round = 0; round < 2; round++) {
                flow_run *failed;
                assert(behavior_instance_dispatch_batch(bi, "Tick", &batch, &failed) && !failed);
            }
            assert(
                bi->variables[0].i == ((kind & 3) == 0   ? 1
                                       : (kind & 3) == 1 ? 2
                                                         : executions)
            );
            behavior_batch_free(batch);
            assert(!behavior_instance_rebind(bi, NULL));
            behavior_instance *other = behavior_instance_create(a, NULL, NULL, NULL);
            behavior_instance_set_trace_enabled(other, false);
            ecs_world *ecs = ecs_world_new();
            behavior_update_binding **bindings = NULL;
            node *entry;
            const behavior_function
                *function = behavior_program_batch_event(a->program, "Tick", &entry);
            assert(function);
            behavior_update_row rows[2];
            for (int row = 0; row < 2; row++) {
                behavior_instance *instance = row ? other : bi;
                rows[row] = (behavior_update_row){
                    .instance = instance,
                    .binding = behavior_update_prepare(&bindings, instance, function, entry),
                    .entity = ecs_entity_to_u64(ecs_world_create_entity(ecs))
                };
            }
            void *code = behavior_update_compile();
            bool native = behavior_default_backend() == BEHAVIOR_SLJIT;
            assert(native == (code != NULL));
            for (int round = 0; round < 2; round++) {
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
            assert(
                bi->variables[0].i == ((kind & 3) == 0   ? 1
                                       : (kind & 3) == 1 ? 2
                                                         : executions)
            );
            assert(other->variables[0].i == bi->variables[0].i);
            behavior_update_free_code(code);
            behavior_update_free_bindings(bindings);
            ecs_world_free(ecs);
            behavior_instance_destroy(other);
        }
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    printf(
        "PASS: %s whole persistent DoOnce/DoN/FlipFlop/Gate, loops, calls and "
        "waits; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
