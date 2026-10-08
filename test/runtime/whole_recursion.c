#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_async_fixture.h"
#include "test/whole_managed_fixture.h"
#include "test/whole_recursion_fixture.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int status;
    double wake;
    uintptr_t steps;
    unsigned depth;
    const node *failed;
    int error_pin;
    uintptr_t error_steps;
} recursive_stop;

typedef struct
{
    behavior_function *function;
    void *code, *inspection, *dispatch;
} saved_code;

static saved_code *generated_group(behavior_function *f, void *library, const char *name)
{
    saved_code *saved = NULL;
    for (int i = 0; i <= acount(f->definitions); i++) {
        behavior_function *part = i ? f->definitions[i - 1] : f;
        apush(
            saved,
            ((saved_code){ part,
                           part->artifact->code,
                           part->artifact->inspection_code,
                           part->artifact->dispatch })
        );
        char symbol[128];
        if (i) {
            snprintf(symbol, sizeof symbol, "%s_callee_%d", name, i);
        } else {
            snprintf(symbol, sizeof symbol, "%s", name);
        }
        part->artifact->code = part->artifact->inspection_code = dlsym(library, symbol);
        part->artifact->dispatch = NULL;
        assert(part->artifact->code);
    }
    return saved;
}

static void restore_group(saved_code *saved)
{
    for (int i = 0; i < acount(saved); i++) {
        saved[i].function->artifact->code = saved[i].code;
        saved[i].function->artifact->inspection_code = saved[i].inspection;
        saved[i].function->artifact->dispatch = saved[i].dispatch;
    }
    afree(saved);
}

static void graph_case(behavior_asset *asset, graph *g, behavior_function *f, int mode, int n, int budget)
{
    pin_value argument = { .type = PIN_I32, .i = n };
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, BEHAVIOR_ORACLE));
    flow_options options = { .instance = instance, .step_budget = (unsigned)budget };
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &argument, 1 }, &options, NULL);
    assert(run);
    recursive_stop *expected = NULL;
    int status = flow_run_resume(run);
    for (;;) {
        const flow_error *error = flow_run_error(run);
        apush(
            expected,
            ((recursive_stop){ .status = status,
                               .wake = flow_run_wake_time(run),
                               .steps = flow_run_steps(run),
                               .depth = flow_run_call_depth(flow_run_active_leaf(run)),
                               .failed = error ? error->node : NULL,
                               .error_pin = error ? error->pin : -1,
                               .error_steps = error ? error->steps : 0 })
        );
        assert(acount(expected) < 100);
        if (status != FLOW_SUSPENDED_RESULT) {
            break;
        }
        status = flow_run_tick_at(run, flow_run_wake_time(run));
    }
    if (!status) {
        pin_value value;
        assert(!flow_run_watch_input(run, g->nodes[1], 1, &value));
        assert(value.i == n * (n + 1) / 2);
    }
    flow_run_free(run);
    behavior_instance_destroy(instance);

    behavior_function_activation activation = { 0 };
    behavior_function_activation_init(f, &activation);
    pin_value results[] = { { 0 }, { .type = PIN_I32 } };
    behavior_function_context context = { .activation = &activation,
                                          .arguments = &argument,
                                          .argument_count = 1,
                                          .results = results,
                                          .budget = (uintptr_t)budget };
    arena storage = { 0 };
    behavior_function_bind_storage(&context, &storage);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int i = 0; i < acount(expected); i++) {
        int actual = behavior_function_run(f, &context, mode == 1 || (mode == 2 && i % 2 == 0));
        assert(actual == expected[i].status && context.steps == expected[i].steps);
        if (actual == FLOW_SUSPENDED_RESULT) {
            const behavior_function *active = f;
            behavior_function_context *leaf = behavior_function_active_context(&context, &active);
            unsigned depth = leaf->call_depth +
                             (unsigned)active->frames[leaf->activation->frame].depth;
            assert(context.wake_time == expected[i].wake && depth == expected[i].depth);
            context.now = context.wake_time;
        }
        if (actual < 0) {
            const behavior_function *active = f;
            behavior_function_context *leaf = behavior_function_active_context(&context, &active);
            const behavior_function_source
                *source = behavior_function_source_at(active, leaf->error_source);
            assert(
                graph_node_by_id(behavior_asset_find_graph(asset, source->graph_id), leaf->node_id) ==
                expected[i].failed
            );
            assert(
                leaf->error_pin == expected[i].error_pin &&
                leaf->error_steps == expected[i].error_steps
            );
            unsigned depth = leaf->call_depth +
                             (unsigned)active->frames[leaf->error_frame].depth;
            assert(depth == expected[i].depth);
        }
        if (!actual) {
            assert(results[1].i == n * (n + 1) / 2);
        }
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    afree(expected);
}

static behavior_function *lower_graph(behavior_asset *asset, int kind)
{
    graph *g = whole_recursion_root(asset, kind);
    behavior_function_diagnostic diagnostic;
    behavior_function *f = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
    if (!f) {
        fprintf(stderr, "recursive kind=%d: %s\n", kind, diagnostic.reason);
    }
    assert(f && behavior_function_has_invocations(f));
    assert(acount(f->definitions) == !!(kind & (WHOLE_RECURSION_OUTER | WHOLE_RECURSION_LIBRARY)));
    return f;
}

static void runtime_graph(int kind, int control, int n, int budget, void *library)
{
    behavior_asset *asset = whole_recursion_graph(kind);
    graph *g = whole_recursion_root(asset, kind);
    if (control == 7) {
        for (int i = 1; i < acount(asset->graphs); i++) {
            graph *f = asset->graphs[i];
            for (int j = 0; j < acount(f->nodes); j++) {
                if (f->nodes[j]->desc == &NODE_Delay) {
                    assert(!graph_remove_edge(f, f->nodes[j], 0, f->nodes[1], 0));
                }
            }
        }
    }
    if (control == 6) {
        for (int i = 0; i < acount(asset->graphs); i++) {
            graph *f = asset->graphs[i];
            if (f->entry_id) {
                assert(!graph_set_breakpoint(f, graph_node_by_id(f, f->entry_id), true));
            }
        }
    }
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        saved_code *saved = NULL;
        if (mode == 3) {
            char symbol[64];
            snprintf(symbol, sizeof symbol, "authored_recursion_%d", kind);
            behavior_function *f = (behavior_function *)behavior_program_function(
                behavior_program_graph(behavior_program_cached(asset), g),
                g->nodes[0]
            );
            assert(f);
            saved = generated_group(f, library, symbol);
        }
        behavior_instance_set_trace_enabled(bi, control != 0 && control != 4);
        behavior_instance_set_breakpoints_enabled(bi, control == 6);
        whole_managed record = { "recursive label", { "nested text", 3.25f }, 42 };
        pin_value item = { .type = PIN_STRUCT, .type_name = "ManagedRecord", .ptr = &record };
        pin_array records = { .element_type = PIN_STRUCT,
                              .type_name = "ManagedRecord",
                              .count = 1,
                              .capacity = 1,
                              .items = &item };
        pin_value member = { .type = PIN_NAME, .type_name = "name", .str = "recursive name" };
        pin_array names = { .element_type = PIN_NAME,
                            .type_name = "name",
                            .count = 1,
                            .capacity = 1,
                            .items = &member };
        pin_value arguments[] = {
            { .type = PIN_I32, .i = n },
            { .type = PIN_STR, .type_name = "const char *", .str = "recursive text" },
            item,
            { .type = PIN_STRUCT,
              .type_name = "ManagedRecord",
              .container = PIN_CONTAINER_ARRAY,
              .ptr = &records },
            { .type = PIN_NAME, .type_name = "name", .container = PIN_CONTAINER_SET, .ptr = &names }
        };
        int argument_count = kind & WHOLE_RECURSION_VALUES ? 5 : 1;
        if (!n) {
            for (int i = 1; i < argument_count; i++) {
                arguments[i].ptr = NULL;
            }
        }
        flow_options options = { .instance = bi, .step_budget = (unsigned)budget };
        flow_run *run = flow_run_start(
            g,
            g->nodes[0],
            (flow_inputs){ arguments, argument_count },
            &options,
            NULL
        );
        assert(run);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = control == 2 ? flow_run_step(run) : flow_run_resume(run);
        int at = 0, waits = 0;
        for (;;) {
            if (mode) {
                assert(run->whole_function_executed && !acount(run->children));
            }
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
            if (!mode) {
                apush(expected, snapshot);
            } else {
                assert(at < acount(expected));
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
            }
            assert(++at < 300);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                break;
            }
            if (state == FLOW_SUSPENDED) {
                assert(flow_run_tick_at(run, flow_run_wake_time(run) - .01) == FLOW_SUSPENDED_RESULT);
                if (control >= 2 && control <= 5) {
                    assert(!flow_run_pause(run));
                }
                if (control == 4 || control == 5) {
                    behavior_instance_set_trace_enabled(bi, control == 4 || waits % 2 == 0);
                }
                waits++;
                result = flow_run_tick_at(run, flow_run_wake_time(run));
            } else {
                assert(state == FLOW_PAUSED);
                result = control == 3 ? flow_run_step_over(run) : flow_run_step(run);
            }
        }
        if (mode) {
            assert(
                at == acount(expected) && eval == eval_oracle_calls() &&
                flow == flow_oracle_calls()
            );
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
        restore_group(saved);
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    behavior_asset_free(asset);
}

static void runtime_graphs(void *library)
{
    for (int kind = 0; kind < WHOLE_RECURSION_COUNT; kind++) {
        for (int control = 0; control < 7; control++) {
            runtime_graph(kind, control, 3, 1000, library);
        }
        runtime_graph(kind, 1, 0, 1000, library);
        runtime_graph(kind, 1, 65, 1000, library);
        if (kind & WHOLE_RECURSION_WAIT) {
            runtime_graph(kind, 7, 3, 1000, NULL);
        }
        for (int budget = 1; budget <= 4; budget++) {
            runtime_graph(kind, 1, 3, budget, library);
        }
    }
    puts("PASS: recursive runtime frame, wait, debugger and source snapshot parity; zero compiled oracle calls");
}

static void runtime_host(int kind, int control, int fail_at, void *library)
{
    behavior_asset *asset = whole_recursion_graph(kind | WHOLE_RECURSION_WAIT);
    whole_recursion_add_host(asset);
    whole_recursion_add_event(asset, whole_recursion_root(asset, kind));
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        saved_code *saved = NULL;
        if (mode == 3) {
            char symbol[64];
            snprintf(symbol, sizeof symbol, "runtime_recursive_host_%d", kind);
            behavior_function *f = (behavior_function *)behavior_program_function(
                behavior_program_graph(behavior_program_cached(asset), asset->graph),
                asset->graph->nodes[0]
            );
            assert(f);
            saved = generated_group(f, library, symbol);
        }
        behavior_instance_set_trace_enabled(bi, control != 0 && control != 4);
        whole_async_capture capture = { .fail_at = fail_at, .immediate = control == 5 };
        if (fail_at >= 0) {
            assert(!behavior_instance_bind_service(bi, "test.async", &capture));
        }
        pin_value input = { .type = PIN_I32, .i = 3 };
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ &input, 1 }, NULL);
        assert(run);
        int result = flow_run_error(run)->code;
        if (!result) {
            result = flow_run_status(run) == FLOW_SUSPENDED ? FLOW_SUSPENDED_RESULT
                                                            : FLOW_OK;
        }
        int at = 0;
        for (;;) {
            if (mode) {
                assert(run->whole_function_executed && !acount(run->children));
            }
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
            if (!mode) {
                apush(expected, snapshot);
            } else {
                assert(at < acount(expected));
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
            }
            assert(++at < 300);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED || state == FLOW_IDLE) {
                break;
            }
            if (state == FLOW_PAUSED) {
                result = control == 3 ? flow_run_step_over(run) : flow_run_step(run);
                continue;
            }
            assert(state == FLOW_SUSPENDED);
            if (!flow_run_waiting_for_host(run)) {
                result = flow_run_tick_at(run, flow_run_wake_time(run));
                continue;
            }
            int index = acount(capture.requests) - 1;
            assert(index >= 0 && capture.arguments[index] == index);
            flow_host_request *request = capture.requests[index];
            assert(flow_host_run(request) == run);
            if (control == 6 && index == 1) {
                flow_host_cancel(request);
                result = FLOW_OK;
                continue;
            }
            if (control == 2 || control == 3 || control == 4) {
                assert(!flow_run_pause(run));
            }
            if (control == 4) {
                behavior_instance_set_trace_enabled(bi, index % 2 == 0);
            }
            if (!capture.immediate) {
                assert(!flow_run_ready(run, 1000));
                assert(flow_host_complete(
                    request,
                    1,
                    (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 100 + index }
                ));
            }
            result = flow_host_resume(request);
        }
        if (flow_run_status(run) == FLOW_COMPLETED) {
            assert(bi->execution.variables[0].i == 6 && capture.calls == 4);
        }
        if (mode) {
            assert(
                at == acount(expected) && eval == eval_oracle_calls() &&
                flow == flow_oracle_calls()
            );
        }
        behavior_instance_destroy(bi);
        whole_async_clear(&capture);
        restore_group(saved);
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    behavior_asset_free(asset);
}

static void runtime_hosts(void *library)
{
    for (int kind = 0; kind < WHOLE_RECURSION_COUNT; kind++) {
        if (kind & (WHOLE_RECURSION_WAIT | WHOLE_RECURSION_VALUES |
                    WHOLE_RECURSION_LIBRARY)) {
            continue;
        }
        for (int control = 0; control < 7; control++) {
            runtime_host(kind, control, 0, library);
        }
        runtime_host(kind, 1, -1, library);
        runtime_host(kind, 1, 1, library);
        runtime_host(kind, 1, 3, library);
    }
    puts("PASS: recursive behavior dispatch, retained host requests, cancellation and paused completion parity; zero compiled oracle calls");
}

static void graphs(void *library)
{
    for (int kind = 0; kind < WHOLE_RECURSION_COUNT; kind++) {
        behavior_asset *asset = whole_recursion_graph(kind);
        graph *g = whole_recursion_root(asset, kind);
        behavior_function *f = lower_graph(asset, kind);
        assert(behavior_function_compile(f));
        void *saved[2][2] = { 0 };
        if (library) {
            for (int i = 0; i <= acount(f->definitions); i++) {
                behavior_function *definition = i ? f->definitions[i - 1] : f;
                saved[i][0] = definition->artifact->code;
                saved[i][1] = definition->artifact->inspection_code;
                char name[64];
                if (i) {
                    snprintf(name, sizeof name, "authored_recursion_%d_callee_%d", kind, i);
                } else {
                    snprintf(name, sizeof name, "authored_recursion_%d", kind);
                }
                definition->artifact->code = definition->artifact
                                                 ->inspection_code = dlsym(library, name);
                assert(definition->artifact->code);
            }
        }
        for (int mode = 0; mode < 3; mode++) {
            if (kind & WHOLE_RECURSION_VALUES) {
                whole_recursion_values(f, mode, false);
                whole_recursion_values(f, mode, true);
                continue;
            }
            int values[] = { 0, 1, 7, 31, 65 };
            int budgets[] = { 1, 2, 3, 4, 5, 1000 };
            for (int i = 0; i < 5; i++) {
                for (int j = 0; j < 6; j++) {
                    graph_case(asset, g, f, mode, values[i], budgets[j]);
                }
            }
        }
        if (library) {
            for (int i = 0; i <= acount(f->definitions); i++) {
                behavior_function *definition = i ? f->definitions[i - 1] : f;
                definition->artifact->code = saved[i][0];
                definition->artifact->inspection_code = saved[i][1];
            }
        }
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    puts("PASS: authored recursive function lowering, immutable definitions, oracle return/wait/depth/budget parity and native/portable switching; zero compiled oracle calls");
}

static void failed_call(bool native, bool bad_argument)
{
    behavior_function *f = whole_recursion_ir(false, 4096);
    behavior_function *child = whole_recursion_ir(false, 4096);
    f->calls[0].function = child;
    for (int i = 0; i < acount(child->instructions); i++) {
        if (child->instructions[i].op == BF_RETURN) {
            child->instructions[i].op = BF_END;
        }
    }
    if (bad_argument) {
        f->data_values[0].initial.type = PIN_STRUCT;
        f->data_values[0].initial.type_name = "UnregisteredRecursiveArgument";
    }
    if (native) {
        assert(behavior_function_compile(f) && behavior_function_compile(child));
    }
    behavior_function_activation activation = { 0 };
    behavior_function_activation_init(f, &activation);
    pin_value arguments[] = { { .type = PIN_I32, .i = bad_argument ? 2 : 1 },
                              { .type = PIN_STR, .str = "retained" } };
    pin_value results[3] = { { 0 }, { .type = PIN_I32, .i = 987 }, { .type = PIN_STR } };
    behavior_function_context context = { .activation = &activation,
                                          .arguments = arguments,
                                          .argument_count = 2,
                                          .results = results,
                                          .budget = 10 };
    arena storage = { 0 };
    behavior_function_bind_storage(&context, &storage);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    int status = behavior_function_run(f, &context, native);
    assert(status == (bad_argument ? FLOW_ERR_ARGUMENT : FLOW_ERR_INVALID_GRAPH));
    const behavior_function *active = f;
    assert(behavior_function_active_context(&context, &active) == &context && active == f);
    assert(
        context.failure ==
        (bad_argument ? BEHAVIOR_FUNCTION_ARGUMENT : BEHAVIOR_FUNCTION_NO_RETURN)
    );
    assert(context.node_id == 2 && context.error_steps == 1);
    assert(results[1].i == 987 && eval_oracle_calls() == eval && flow_oracle_calls() == flow);
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    behavior_function_free(f);
    behavior_function_free(child);
}

int whole_recursion_audit()
{
    behavior_asset *asset = whole_recursion_graph(0);
    graph *g = asset->graphs[1];
    graph *root = asset->graph;
    node *event = graph_add_node(root, &NODE_Event);
    assert(!graph_set_event_name(root, event, "Run"));
    node *call = behavior_asset_add_function_call_node(asset, g);
    uint64_t id = behavior_asset_add_variable(asset, "Result", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    node *set = behavior_asset_add_set_node(asset, id);
    assert(
        !graph_set_pin_default(root, call, false, 1, &(pin_value){ .type = PIN_I32, .i = 7 })
    );
    assert(!graph_connect_exec(root, event, 0, call, 0, 0));
    assert(!graph_connect_exec(root, call, 0, set, 0, 0));
    assert(!graph_connect_data(root, call, 1, set, 1));
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, BEHAVIOR_ORACLE));
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, NULL);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value result;
    assert(!behavior_instance_get_variable(instance, id, &result) && result.i == 28);
    behavior_instance_destroy(instance);
    behavior_function_diagnostic diagnostic;
    behavior_function *function = behavior_function_lower(asset, g, g->nodes[0], &diagnostic);
    if (!function) {
        printf(
            "UNCOVERED: graph=%llu node=%llu pin=%d: %s\n",
            (unsigned long long)diagnostic.graph->graph_id,
            (unsigned long long)diagnostic.node->id,
            diagnostic.pin,
            diagnostic.reason
        );
    }
    bool lowered = function != NULL;
    behavior_function_free(function);
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, false);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, NULL);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(!behavior_instance_get_variable(instance, id, &result) && result.i == 28);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    bool compiled = lowered && run->whole_function_executed;
    if (!compiled) {
        const behavior_function_diagnostic *d = behavior_program_function_diagnostic(
            behavior_program_graph(behavior_program_cached(asset), root),
            event
        );
        assert(d);
        printf(
            "UNCOVERED: Scene entry graph=%llu node=%llu pin=%d: %s\n",
            (unsigned long long)d->graph->graph_id,
            (unsigned long long)d->node->id,
            d->pin,
            d->reason
        );
    }
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    return compiled ? 0 : 1;
}

void test_compiled_recursion(const char *library)
{
    void *handle = library ? dlopen(library, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!library || handle);
    runtime_graphs(handle);
    runtime_hosts(handle);
    for (int native = 0; native < 2; native++) {
        failed_call(native, false);
        failed_call(native, true);
    }
    for (int waits = 0; waits < 2; waits++) {
        behavior_function *f = whole_recursion_ir(waits, 4096),
                          *other = whole_recursion_ir(waits, 4096);
        assert(behavior_function_compile(f) && behavior_function_compile(other));
        void *native = f->artifact->code, *other_native = other->artifact->code;
        if (handle) {
            f->artifact->code = dlsym(handle, waits ? "recursive_wait" : "recursive_sync");
            other->artifact->code = f->artifact->code;
            assert(f->artifact->code);
        }
        for (int mutual = 0; mutual < 2; mutual++) {
            f->calls[0].function = mutual ? other : f;
            other->calls[0].function = f;
            for (int mode = 0; mode < 3; mode++) {
                for (int n = 0; n < 8; n++) {
                    whole_recursion_case(f, mode, n, 10, 0, false);
                }
                whole_recursion_case(f, mode, 8, 0, 0, false);
                whole_recursion_case(f, mode, 8, 1, 0, false);
                whole_recursion_case(f, mode, 8, 10, 4094, false);
                if (waits) {
                    whole_recursion_case(f, mode, 8, 10, 0, true);
                }
            }
        }
        if (!waits) {
            whole_recursion_case(f, 0, 2048, 10, 0, false);
            whole_recursion_case(f, 1, 2048, 10, 0, false);
        }
        f->artifact->code = native;
        other->artifact->code = other_native;
        behavior_function_free(f);
        behavior_function_free(other);
    }
    graphs(handle);
    if (handle) {
        dlclose(handle);
    }
    puts("PASS: compiled call IR self/mutual recursion, live values, owned returns, waits, cancellation, depth/budget errors and backend switches; zero oracle calls");
}

void emit_recursion_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int waits = 0; waits < 2; waits++) {
        behavior_function *f = whole_recursion_ir(waits, 4096);
        behavior_function_write_c(f, out, waits ? "recursive_wait" : "recursive_sync");
        behavior_function_free(f);
    }
    for (int kind = 0; kind < WHOLE_RECURSION_COUNT; kind++) {
        behavior_asset *asset = whole_recursion_graph(kind);
        behavior_function *f = lower_graph(asset, kind);
        char name[64];
        snprintf(name, sizeof name, "authored_recursion_%d", kind);
        behavior_function_write_c(f, out, name);
        behavior_function_free(f);
        behavior_asset_free(asset);
    }
    for (int kind = 0; kind < WHOLE_RECURSION_COUNT; kind++) {
        if (kind & (WHOLE_RECURSION_WAIT | WHOLE_RECURSION_VALUES |
                    WHOLE_RECURSION_LIBRARY)) {
            continue;
        }
        behavior_asset *asset = whole_recursion_graph(kind | WHOLE_RECURSION_WAIT);
        whole_recursion_add_host(asset);
        whole_recursion_add_event(asset, whole_recursion_root(asset, kind));
        behavior_program *program = behavior_program_publish(asset);
        const behavior_function *f = behavior_program_function(
            behavior_program_graph(program, asset->graph),
            asset->graph->nodes[0]
        );
        assert(f);
        char symbol[64];
        snprintf(symbol, sizeof symbol, "runtime_recursive_host_%d", kind);
        behavior_function_write_c(f, out, symbol);
        behavior_asset_free(asset);
    }
    assert(!fclose(out));
}
