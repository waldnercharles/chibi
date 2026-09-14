#include "graph/compiler/behavior_scalar.h"
#include "graph/model/registry.h"
#include "graph/nodes/array_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/whole_command_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pin_spec command_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Integer", .type = PIN_I32, .type_name = "int" },
    { .name = "Real", .type = PIN_F64, .type_name = "double" },
    { .name = "Float", .type = PIN_F32, .type_name = "float" },
    { .name = "Handle", .type = PIN_HANDLE, .type_name = "ecs.entity" },
};
static pin_spec command_outputs[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
void whole_command_clear(whole_command_capture *c)
{
    afree(c->values);
    arena_free(&c->storage);
    *c = (whole_command_capture){ 0 };
}
static int capture(void *service, const char *reference, const pin_value *values, int count)
{
    whole_command_capture *c = service;
    assert(count == 4 && !strcmp(reference, "Capture"));
    c->calls++;
    for (int i = 0; i < count; i++) {
        uint64_t bits = 0;
        memcpy(&bits, &values[i].d, behavior_scalar_size(values[i].type));
        c->checksum += bits;
        if (!c->record) {
            continue;
        }
        pin_value v = values[i];
        if (v.type_name) {
            char *name = arena_alloc(&c->storage, strlen(v.type_name) + 1);
            strcpy(name, v.type_name);
            v.type_name = name;
        }
        apush(c->values, v);
    }
    return !c->fail_at || c->calls == c->fail_at ? c->error : 0;
}
static const node_command capture_command = {
    .service = "test.commands",
    .missing_service = "missing command service",
    .missing_reference = "missing command name",
    .failure_message = "command failed",
    .invoke = capture
};
static int capture_flow(node *n, flow_context *ctx)
{
    whole_command_capture *c = flow_context_service(ctx, "test.commands");
    if (!c) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, capture_command.missing_service);
    }
    if (!n->reference_name || !n->reference_name[0]) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, capture_command.missing_reference);
    }
    pin_value values[4];
    for (int i = 0; i < 4; i++) {
        int status = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (status < 0) {
            return status;
        }
    }
    int status = capture(c, n->reference_name, values, 4);
    return status < 0
             ? flow_context_fail(ctx, status, capture_command.failure_message)
             : flow_context_continue(ctx, 0);
}
static node_desc capture_node = { .name = "Capture command",
                                  .input_count = 5,
                                  .inputs = command_inputs,
                                  .output_count = 1,
                                  .outputs = command_outputs,
                                  .flow_kind = NODE_FLOW_CUSTOM,
                                  .flow = capture_flow,
                                  .command = &capture_command };
const node_desc *whole_command_node()
{
    return &capture_node;
}
static void connect(graph *g, node *from, int out, node *to, int in)
{
    int status = graph_connect_data(g, from, out, to, in);
    if (status) {
        fprintf(
            stderr,
            "connect %s.%d -> %s.%d: %d\n",
            from->desc->name,
            out,
            to->desc->name,
            in,
            status
        );
    }
    assert(!status);
}
static void next(graph *g, node *from, int out, node *to)
{
    assert(!graph_connect_exec(g, from, out, to, 0, 0));
}
static node *variable(behavior_asset *a, graph *g, int index, bool set)
{
    const behavior_variable *v = &a->variables[index];
    node *n = graph_add_node(g, set ? &NODE_SetVariable : &NODE_GetVariable);
    assert(!graph_set_node_reference(g, n, v->id, v->name));
    assert(!graph_set_pin_type(g, n, 1, set ? 1 : 0, v->type, v->type_name));
    if (set) {
        assert(!graph_set_pin_type(g, n, 0, 1, v->type, v->type_name));
    }
    return n;
}
behavior_asset *whole_command_fixture(int kind, bool missing_reference, bool bad_input)
{
    behavior_asset *a = behavior_asset_new("Commands");
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    graph *g = behavior_asset_add_function(a, "Send", false);
    node *entry = g->nodes[0], *ret = g->nodes[1],
         *send = graph_add_node(g, &capture_node);
    if (!missing_reference) {
        assert(!graph_set_node_reference(g, send, 0, "Capture"));
    }
    for (int i = 1; i < 5; i++) {
        assert(graph_add_pin(g, entry, 1, &command_inputs[i]) == i);
        assert(graph_add_pin(g, ret, 0, &command_inputs[i]) == i);
        connect(g, entry, i, send, i);
        connect(g, entry, i, ret, i);
        assert(behavior_asset_add_variable(
            a,
            command_inputs[i].name,
            command_inputs[i].type,
            command_inputs[i].type_name,
            PIN_CONTAINER_SINGLE,
            NULL,
            ""
        ));
    }
    node *start = entry;
    if (kind == WHOLE_COMMAND_STORES || kind == WHOLE_COMMAND_BINDINGS) {
        for (int i = 1; i < 5; i++) {
            node *save = variable(a, g, i - 1, true),
                 *overwrite = variable(a, g, i - 1, true);
            connect(g, entry, i, save, 1);
            assert(!graph_remove_edge(g, entry, i, send, i));
            assert(!graph_remove_edge(g, entry, i, ret, i));
            connect(g, save, 1, ret, i);
            if (kind == WHOLE_COMMAND_STORES) {
                connect(g, save, 1, send, i);
            }
            if (kind == WHOLE_COMMAND_BINDINGS) {
                pin_binding binding = { .kind = PIN_BINDING_VARIABLE,
                                        .variable_id = a->variables[i - 1].id };
                assert(!graph_set_pin_binding(g, send, 0, i, &binding));
            }
            next(g, start, 0, save);
            if (kind == WHOLE_COMMAND_STORES) {
                next(g, save, 0, overwrite);
                start = overwrite;
            }
            if (kind == WHOLE_COMMAND_BINDINGS) {
                start = save;
            }
        }
    }
    if (kind == WHOLE_COMMAND_LITERAL) {
        const char *name = "quoted \"kind\"\\\nname";
        node *targets[] = { send, ret };
        for (int i = 0; i < 2; i++) {
            assert(!graph_remove_edge(g, entry, 2, targets[i], 2));
            assert(!graph_set_pin_type(g, targets[i], 0, 2, PIN_F64, name));
            assert(
                !graph_set_pin_default(g, targets[i], 0, 2, &(pin_value){ .type = PIN_F64, .d = 2.75 })
            );
        }
    }
    if (kind == WHOLE_COMMAND_MIXED) {
        node *add = graph_add_node(g, &NODE_Add);
        assert(!graph_set_pin_type(g, add, 0, 0, PIN_F32, "float"));
        assert(!graph_set_pin_type(g, add, 0, 1, PIN_F64, "double"));
        assert(!graph_set_pin_type(g, add, 1, 0, PIN_F32, "float"));
        connect(g, entry, 3, add, 0);
        connect(g, entry, 2, add, 1);
        for (int i = 2; i <= 3; i++) {
            assert(!graph_remove_edge(g, entry, i, send, i));
            assert(!graph_remove_edge(g, entry, i, ret, i));
            connect(g, add, 0, send, i);
            connect(g, add, 0, ret, i);
        }
    }
    if (kind == WHOLE_COMMAND_PRODUCERS) {
        node *sine = graph_add_node(g, &NODE_Sine),
             *delta = graph_add_node(g, &NODE_DeltaTime),
             *self = graph_add_node(g, &NODE_Self);
        assert(!graph_set_pin_type(g, self, 1, 0, PIN_HANDLE, "ecs.entity"));
        node *sources[] = { sine, delta, self };
        connect(g, entry, 2, sine, 0);
        for (int i = 2; i <= 4; i++) {
            assert(!graph_remove_edge(g, entry, i, send, i));
            assert(!graph_remove_edge(g, entry, i, ret, i));
            connect(g, sources[i - 2], 0, send, i);
            connect(g, sources[i - 2], 0, ret, i);
        }
    }
    if (bad_input) {
        node *divide = graph_add_node(g, &NODE_Divide);
        for (int i = 0; i < 2; i++) {
            assert(!graph_set_pin_type(g, divide, 0, i, PIN_I32, "int"));
        }
        assert(!graph_set_pin_type(g, divide, 1, 0, PIN_I32, "int"));
        connect(g, entry, 1, divide, 0);
        assert(!graph_set_pin_default(g, divide, 0, 1, &(pin_value){ .type = PIN_I32 }));
        assert(!graph_remove_edge(g, entry, 1, send, 1));
        connect(g, divide, 0, send, 1);
    }
    if (kind == WHOLE_COMMAND_LOOP || kind == WHOLE_COMMAND_ARRAY) {
        node *loop = graph_add_node(g, kind == WHOLE_COMMAND_LOOP ? &NODE_ForLoop : &NODE_ForEach);
        next(g, start, 0, loop);
        next(g, loop, 0, send);
        next(g, loop, kind == WHOLE_COMMAND_LOOP ? 2 : 3, ret);
        if (kind == WHOLE_COMMAND_LOOP) {
            assert(
                !graph_set_pin_default(g, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 3 })
            );
            assert(!graph_remove_edge(g, entry, 1, send, 1));
            connect(g, loop, 1, send, 1);
        } else {
            pin_spec array = { .name = "Values",
                               .type = PIN_F64,
                               .type_name = "double",
                               .container = PIN_CONTAINER_ARRAY };
            assert(graph_add_pin(g, entry, 1, &array) == 5);
            connect(g, entry, 5, loop, 1);
            assert(!graph_remove_edge(g, entry, 2, send, 2));
            connect(g, loop, 1, send, 2);
        }
    } else {
        next(g, start, 0, send);
        next(g, send, 0, ret);
    }
    if (kind == WHOLE_COMMAND_WRAPPED) {
        graph *wrapper = behavior_asset_add_function(a, "Wrapped", false);
        for (int i = 1; i < 5; i++) {
            assert(graph_add_pin(wrapper, wrapper->nodes[0], 1, &command_inputs[i]) == i);
            assert(graph_add_pin(wrapper, wrapper->nodes[1], 0, &command_inputs[i]) == i);
        }
        node *call = behavior_asset_add_function_call_node_to_graph(a, wrapper, g);
        for (int i = 1; i < 5; i++) {
            connect(wrapper, wrapper->nodes[0], i, call, i);
            connect(wrapper, call, i, wrapper->nodes[1], i);
        }
        next(wrapper, wrapper->nodes[0], 0, call);
        next(wrapper, call, 0, wrapper->nodes[1]);
    }
    return a;
}
void whole_command_smoke()
{
    behavior_asset *a = whole_command_fixture(WHOLE_COMMAND_LOOP, false, false);
    graph *g = a->graphs[1];
    pin_value owner = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 123 };
    behavior_instance *bi = behavior_instance_create(a, &owner, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    whole_command_capture c = { 0 };
    assert(!behavior_instance_bind_service(bi, "test.commands", &c));
    flow_options options = { .instance = bi };
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ 0 }, &options, NULL);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    assert(run && !flow_run_resume(run) && run->whole_function_executed && c.calls == 4);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    printf("PASS: %s whole command loop and direct host calls; zero oracle calls\n", behavior_backend_name(behavior_default_backend()));
    flow_run_free(run);
    whole_command_clear(&c);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}
