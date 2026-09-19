#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "support/arena.h"
#include "test/whole_data_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

behavior_asset *whole_data_fixture(const node_desc *desc, bool event_arguments)
{
    behavior_asset *a = behavior_asset_new(desc->name);
    graph *g = behavior_asset_add_function(a, "Query", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    node *query = graph_add_node(g, desc);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, event, "Run"));
    for (int i = 0; i < query->input_count; i++) {
        assert(graph_add_pin(g, entry, 1, &query->input_pins[i]) == i + 1);
        if (event_arguments) {
            assert(graph_add_pin(a->graph, event, 1, &query->input_pins[i]) == i + 1);
        }
        assert(!graph_connect_data(g, entry, i + 1, query, i));
    }
    for (int i = 0; i < query->output_count; i++) {
        assert(graph_add_pin(g, ret, 0, &query->output_pins[i]) == i + 1);
        assert(!graph_connect_data(g, query, i, ret, i + 1));
        const pin_spec *p = &query->output_pins[i];
        assert(
            behavior_asset_add_variable(a, p->name, p->type, p->type_name, PIN_CONTAINER_SINGLE, NULL, "")
        );
    }
    assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
    node *call = behavior_asset_add_function_call_node(a, g), *previous = call;
    assert(!graph_connect_exec(a->graph, event, 0, call, 0, 0));
    for (int i = 0; event_arguments && i < query->input_count; i++) {
        assert(!graph_connect_data(a->graph, event, i + 1, call, i + 1));
    }
    for (int i = 0; i < query->output_count; i++) {
        node *set = behavior_asset_add_set_node(a, a->variables[i].id);
        assert(!graph_connect_data(a->graph, call, i + 1, set, 1));
        assert(!graph_connect_exec(a->graph, previous, 0, set, 0, 0));
        previous = set;
    }
    return a;
}

static pin_spec inputs[] = {
    { .name = "Record", .type = PIN_STRUCT, .type_name = "WholeRecord" },
    { .name = "Gain", .type = PIN_F64, .type_name = "double" },
    { .name = "Flags", .type = PIN_U32, .type_name = "unsigned int" },
    { .name = "Target", .type = PIN_HANDLE, .type_name = "ecs.entity" }
};
static pin_spec outputs[] = {
    { .name = "Record", .type = PIN_STRUCT, .type_name = "WholeRecord" },
    { .name = "Found", .type = PIN_BOOL, .type_name = "bool" },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Previous", .type = PIN_STRUCT, .type_name = "WholeRecord" }
};
static const char *invoke(void *user, const pin_value *args, pin_value *out)
{
    whole_data_service *service = user;
    if (!service) {
        return "test data service missing";
    }
    if (++service->calls == service->fail_at) {
        return "test data call failed";
    }
    whole_record input = args[0].ptr ? *(const whole_record *)args[0].ptr
                                     : (whole_record){ 0 };
    whole_record *record = out[0].ptr;
    *record = input;
    record->weight = input.weight * args[1].d + service->bias;
    record->position.x = (float)(input.position.x + service->bias);
    record->tag = (uint8_t)args[2].u;
    out[1].b = args[0].ptr != NULL && (args[2].u & 1);
    out[2].u64 = out[1].b ? args[3].u64 : 0;
    *(whole_record *)out[3].ptr = input;
    return NULL;
}
static const char *read_data(const eval_context *ctx, const pin_value *args, arena *storage, pin_value *out)
{
    for (int i = 0; i < 4; i++) {
        out[i] = (pin_value){ .type = outputs[i].type,
                              .type_name = outputs[i].type_name };
        if (out[i].type == PIN_STRUCT) {
            out[i].ptr = arena_alloc(storage, sizeof(whole_record));
        }
    }
    return invoke(eval_context_service(ctx, "test.data"), args, out);
}
static const node_access access = { .flags = NODE_EFFECT_WORLD_READ };
static const node_host_call call = { .service = "test.data", .invoke = invoke };
static node_desc data_node = { .name = "Test Native Data",
                               .input_count = 4,
                               .inputs = inputs,
                               .output_count = 4,
                               .outputs = outputs,
                               .pure_kind = NODE_PURE_READ,
                               .read_inputs = read_data,
                               .access = &access,
                               .host_call = &call };
behavior_asset *whole_data_nested(bool event_arguments)
{
    behavior_asset *a = whole_data_fixture(&data_node, event_arguments);
    graph *g = a->graphs[1];
    node *first = g->nodes[2], *second = graph_add_node(g, &data_node),
         *ret = g->nodes[1];
    assert(!graph_connect_data(g, first, 0, second, 0));
    for (int i = 1; i < 4; i++) {
        assert(!graph_connect_data(g, g->nodes[0], i + 1, second, i));
    }
    for (int i = 0; i < 4; i++) {
        assert(!graph_remove_edge(g, first, i, ret, i + 1));
        assert(!graph_connect_data(g, second, i, ret, i + 1));
    }
    if (!event_arguments) {
        whole_record record = { .tag = 7,
                                .position = { 1, 2 },
                                .weight = 3,
                                .active = true,
                                .stamp = INT64_MIN + 3 };
        pin_value args[] = {
            { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &record },
            { .type = PIN_F64, .d = 2 },
            { .type = PIN_U32, .u = 3 },
            { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 77 }
        };
        for (int i = 0; i < 4; i++) {
            assert(!graph_set_pin_default(a->graph, a->graph->nodes[1], 0, i + 1, &args[i]));
        }
    }
    return a;
}
typedef struct
{
    int calls, fail_at;
    double sum;
} action_capture;
static const char *action(void *user, const pin_value *args, pin_value *)
{
    action_capture *capture = user;
    if (!capture) {
        return "missing action service";
    }
    assert(args[0].type == PIN_STRUCT && !strcmp(args[0].type_name, "WholeRecord"));
    assert(args[1].type == PIN_STR && !strcmp(args[1].str, "borrowed label"));
    const whole_record *record = args[0].ptr;
    assert(record && record->stamp == INT64_MIN + 23);
    capture->sum += record->weight;
    return ++capture->calls == capture->fail_at ? "action failed after write" : NULL;
}
static int action_flow(node *, flow_context *ctx)
{
    pin_value args[2];
    for (int i = 0; i < 2; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &args[i]);
        if (result < 0) {
            return result;
        }
    }
    const char *error = action(flow_context_service(ctx, "test.action"), args, NULL);
    return error ? flow_context_fail(ctx, FLOW_ERR_NODE, error)
                 : flow_context_continue(ctx, 0);
}
static void action_smoke()
{
    pin_spec in[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
                      { .name = "Record", .type = PIN_STRUCT, .type_name = "WholeRecord" },
                      { .name = "Label", .type = PIN_STR } };
    pin_spec out[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
    const node_host_call call = { .service = "test.action", .invoke = action };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    const node_desc desc = { .name = "Typed host action",
                             .inputs = in,
                             .input_count = 3,
                             .outputs = out,
                             .output_count = 1,
                             .flow = action_flow,
                             .flow_kind = NODE_FLOW_CUSTOM,
                             .host_call = &call,
                             .access = &access };
    behavior_asset *a = behavior_asset_new("Typed actions");
    node *event = graph_add_node(a->graph, &NODE_Event), *previous = event;
    assert(!graph_set_event_name(a->graph, event, "Run"));
    whole_record record = { .weight = 3, .stamp = INT64_MIN + 23 };
    for (int i = 0; i < 2; i++) {
        node *n = graph_add_node(a->graph, &desc);
        assert(!graph_set_pin_default(
            a->graph,
            n,
            0,
            1,
            &(pin_value){ .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &record }
        ));
        assert(
            !graph_set_pin_default(a->graph, n, 0, 2, &(pin_value){ .type = PIN_STR, .str = "borrowed label" })
        );
        assert(!graph_connect_exec(a->graph, previous, 0, n, 0, 0));
        previous = n;
    }
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    assert(!behavior_instance_set_activation_pool(bi, true));
    action_capture capture = { 0 };
    assert(!behavior_instance_bind_service(bi, "test.action", &capture));
    behavior_batch *batch = NULL;
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int batched = 0; batched < 2; batched++) {
        for (int fail = 0; fail <= 2; fail++) {
            for (int repeat = 0; repeat < 3; repeat++) {
                capture = (action_capture){ .fail_at = fail };
                flow_run *run = NULL;
                uint64_t allocations = behavior_function_data_allocations();
                if (batched) {
                    assert(behavior_instance_dispatch_batch(bi, "Run", &batch, &run));
                } else {
                    run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, NULL);
                    assert(run);
                }
                assert(capture.calls == (fail ? fail : 2) && capture.sum == capture.calls * 3);
                if (repeat) {
                    assert(allocations == behavior_function_data_allocations());
                }
                if (run) {
                    assert(
                        run->whole_function_executed &&
                        flow_run_status(run) == (fail ? FLOW_FAILED : FLOW_COMPLETED)
                    );
                    if (fail) {
                        assert(
                            flow_run_error(run)->code == FLOW_ERR_NODE &&
                            !strcmp(flow_run_error(run)->message, "action failed after write")
                        );
                    }
                    assert(!behavior_instance_release_activation(bi, run));
                }
            }
        }
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_batch_free(batch);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    printf(
        "PASS: %s typed synchronous host effects, borrowed strings/structs, "
        "partial failures and reused banks; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
static void whole_format_smoke()
{
    behavior_asset *asset = behavior_asset_new("WholeFormatText");
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, event, "Score"));
    pin_spec points = { "Points", PIN_F64, "double", PIN_DATA };
    pin_spec player = { "Player", PIN_STR, "const char *", PIN_DATA };
    assert(graph_add_pin(g, event, 1, &points) == 1);
    assert(graph_add_pin(g, event, 1, &player) == 2);
    node *format = graph_add_node(g, registry_node_by_name("Format Text"));
    pin_spec argument = { .name = "Arg1",
                          .type = PIN_FLOW,
                          .kind = PIN_DATA,
                          .flags = PIN_FLAG_WILDCARD };
    assert(graph_add_pin(g, format, 0, &argument) == 2);
    argument.name = "Arg2";
    assert(graph_add_pin(g, format, 0, &argument) == 3);
    assert(
        !graph_set_pin_default(g, format, 0, 0, &(pin_value){ .type = PIN_STR, .str = "RALLY {:.0f} / {}" })
    );
    assert(!graph_connect_data(g, event, 1, format, 1));
    assert(!graph_connect_data(g, event, 2, format, 2));
    behavior_variable *text = behavior_asset_add_variable(
        asset,
        "Text",
        PIN_STR,
        "const char *",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    uint64_t id = text->id;
    node *set = behavior_asset_add_set_node(asset, id);
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));
    assert(!graph_connect_data(g, format, 0, set, 1));
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int i = 1; i <= 3; i++) {
        pin_value values[] = { { .type = PIN_F64, .d = i * 100.0 },
                               { .type = PIN_STR, .str = "pilot" } };
        flow_run *run = behavior_instance_dispatch(instance, "Score", (flow_inputs){ values, 2 }, NULL);
        assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
        assert(!behavior_instance_release_activation(instance, run));
        pin_value result;
        char expected[64];
        snprintf(expected, sizeof expected, "RALLY %d / pilot", i * 100);
        assert(!behavior_instance_get_variable(instance, id, &result));
        assert(!strcmp(result.str, expected));
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    puts("whole Format Text PASS: fixed format, mixed variadic values, unused spare and retained output; zero oracle calls");
}

void whole_data_smoke()
{
    whole_format_smoke();
    behavior_asset *a = whole_data_nested(false);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    assert(!behavior_instance_set_activation_pool(bi, true));
    whole_data_service service = { .bias = .25 };
    assert(!behavior_instance_bind_service(bi, "test.data", &service));
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(), allocations = 0;
    for (int i = 0; i < 3; i++) {
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, NULL);
        assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_COMPLETED);
        const whole_record *r = bi->variables[0].ptr,
                           *previous = bi->variables[3].ptr;
        assert(r && previous && r->weight == 12.75 && previous->weight == 6.25);
        assert(r->position.x == 1.5f && r->stamp == INT64_MIN + 3);
        assert(
            bi->variables[1].b && bi->variables[2].u64 == 77 &&
            service.calls == 2 * (i + 1)
        );
        assert(!behavior_instance_release_activation(bi, run));
        if (!i) {
            allocations = behavior_function_data_allocations();
        }
        assert(allocations == behavior_function_data_allocations());
    }
    behavior_batch *batch = NULL;
    flow_run *failed = NULL;
    for (int i = 0; i < 3; i++) {
        assert(behavior_instance_dispatch_batch(bi, "Run", &batch, &failed) && !failed);
        if (!i) {
            allocations = behavior_function_data_allocations();
        }
        assert(allocations == behavior_function_data_allocations());
        assert(((const whole_record *)bi->variables[0].ptr)->weight == 12.75);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_batch_free(batch);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    printf(
        "PASS: %s nested host data calls, plain struct snapshots and "
        "pooled/batched buffers; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
    action_smoke();
}
